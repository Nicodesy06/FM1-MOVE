/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 Move device page: what FM1 Move adds on top of SLOOP's editor protocol code (web/editor.html, the part between
   PROTO-BEGIN and PROTO-END, inlined before this file by web/make_device.py; web/test_device.mjs runs both against the
   firmware compiled to WebAssembly). Plain script: it uses the protocol section's globals (CMD, req, parse, crc32,
   b64enc, b64dec, SMP, backupCapture, BackupError).

   FM1 Move speaks the same protocol (v10) with eight tracks. Its backup objects (firmware/src/editor.c):
     1 settings, 6..7 user preset banks, 8 FM6 patch bank, 9 SYN drum kits,
     16 the working project, 17..18 projects 1..2 (FM1M streams, docs/PROJECT_FORMAT.md),
     32..34 the user sample slots USR1..USR3 (no USR4: its flash holds the FM1M projects). */

const FM1 = {
  NTRK: 8,
  SLOTS: 3,                                        /* user sample slots USR1..USR3 */
  WORK: 16, PROJ0: 17, NPROJ: 2,                   /* backup objects of the projects (2 slots of 3 chunks: room for clips) */
  FORMAT: "fm1move-backup", VERSION: 1,
  RESTORE: [6, 7, 8, 9, 17, 18, 16, 1],           /* banks, the projects, the working project, the settings */
  SAMPLES: [32, 33, 34],
};
/* INFO -> is it FM1 Move (else SLOOP / Felucca or something older) */
const isMove = (info) => !!info && /FM1 MOVE/i.test(info.version || "") && info.ntrk === FM1.NTRK;
const moveVersion = (info) => ((info && info.version) || "").replace(/^FELUCCA\s+/i, "");

/* the eight tracks: kind from the engine byte (NENGINES = a drum track), names for the page */
function trackRows(info, tr) {
  return tr.tracks.map((t, i) => ({
    index: i,
    kind: t.engine >= info.nengines ? "drums" : "synth",
    engine: t.engine < info.nengines ? info.engines[t.engine] : "DRUMS",
    engineIndex: t.engine,
    preset: t.preset, level: t.level, mute: !!t.mute, selected: tr.sel === i,
  }));
}

/* ---- backup / restore ---- */
async function moveBackupCapture(rq, info, progress = () => {}) {
  const f = await backupCapture(rq, info, progress);
  return { ...f, format: FM1.FORMAT, version: FM1.VERSION };
}
/* a backup file -> Map(id -> bytes), each object checked; refuses SLOOP's backups (their projects are SLOOP's) */
function moveBackupObjects(file) {
  if (!file || file.format !== FM1.FORMAT || !(file.version >= 1) || !Array.isArray(file.objects))
    throw new BackupError("bkBad", file && file.format === "sloop-backup" ? "a SLOOP backup (not FM1 Move)" : "not an FM1 Move backup");
  const m = new Map();
  for (const o of file.objects) {
    const d = b64dec(o.data || "");
    if (![...FM1.RESTORE, ...FM1.SAMPLES].includes(o.id) || d.length !== o.len || crc32(d) !== (o.crc >>> 0))
      throw new BackupError("bkBad", `object ${o.id}: damaged`);
    m.set(o.id, d);
  }
  if (!m.has(FM1.WORK) || !m.has(1)) throw new BackupError("bkBad", "incomplete backup");
  return m;
}
/* one object through BK_PUT: begin, data, commit (the firmware checks it whole before it writes anything) */
async function putObject(rq, id, d, tick = () => {}) {
  let r = parse[CMD.BK_PUT](await rq(req.bkBegin(id, d.length, crc32(d)), { timeout: 1500, retries: 1 }));
  if (r.rc) throw new BackupError("bkWrite", `begin ${id} rc ${r.rc}`);
  for (let off = 0; off < d.length; off += 256) {
    r = parse[CMD.BK_PUT](await rq(req.bkData(id, off, d.subarray(off, off + 256)), { timeout: 1500, retries: 1 }));
    if (r.rc) throw new BackupError("bkWrite", `data ${id}+${off} rc ${r.rc}`);
    tick(Math.min(256, d.length - off));
  }
  r = parse[CMD.BK_PUT](await rq(req.bkCommit(id), { timeout: 5000, retries: 0 }));
  if (r.rc === 3) throw new BackupError("bkStop", "stop the FM-1 first");
  if (r.rc === 2) throw new BackupError("bkBad", `object ${id} refused (not valid for this firmware)`);
  if (r.rc) throw new BackupError("bkWrite", `commit ${id} rc ${r.rc}`);
}
/* one object read whole through BK_GET (LIST first: the device snapshots the working project's hash) */
async function getObject(rq, id, progress = () => {}) {
  const L = parse[CMD.BK_LIST](await rq(req.bkList(), { timeout: 5000, retries: 0 }));
  if (L.rc) throw new BackupError("bkRc" + L.rc, `LIST rc ${L.rc}`);
  const it = L.items.find((x) => x.id === id);
  if (!it) throw new BackupError("bkRead", `no object ${id}`);
  const data = new Uint8Array(it.len);
  for (let off = 0; off < it.len; off += 256) {
    const n = Math.min(256, it.len - off);
    const g = parse[CMD.BK_GET](await rq(req.bkGet(id, off, n), { timeout: 1500, retries: 2 }));
    if (g.rc || g.data.length !== n) throw new BackupError(g.rc === 5 ? "bkStale" : "bkRead", `GET ${id}+${off} rc ${g.rc}`);
    data.set(g.data, off);
    progress(off + n, it.len);
  }
  if (crc32(data) !== it.crc) throw new BackupError("bkStale", `object ${id}: CRC`);
  return data;
}
/* a slot into USRk+1: d = buildSlot's {hdr, data}, or a raw slot image (a backup object: header at 0, data at 512) */
async function uploadSlot(rq, k, d, tick = () => {}) {
  if (k < 0 || k >= FM1.SLOTS) throw new BackupError("bkWrite", `USR${k + 1}: FM1 Move has USR1..USR3`);
  let r = parse[CMD.SMP_BEGIN](await rq(req.smpBegin(k), { timeout: 1000, retries: 0 }));
  if (r.rc) throw new BackupError("bkWrite", `USR${k + 1} begin rc ${r.rc}`);
  const data = d.hdr ? d.data : d.subarray(SMP.DATA_OFF), hdr = d.hdr ? d.hdr : d.subarray(0, SMP.HDR_LEN);
  for (let off = 0; off < data.length; off += 256) {
    const w = parse[CMD.SMP_WRITE](await rq(req.smpWrite(k, SMP.DATA_OFF + off, data.subarray(off, off + 256)), { timeout: 1000, retries: 1 }));
    if (w.rc) throw new BackupError("bkWrite", `USR${k + 1} write rc ${w.rc}`);
    tick(Math.min(256, data.length - off));
  }
  r = parse[CMD.SMP_END](await rq(req.smpEnd(k, hdr), { timeout: 2000, retries: 0 }));
  if (r.rc) throw new BackupError("bkWrite", `USR${k + 1} end rc ${r.rc}`);
}
async function moveBackupRestore(rq, file, progress = () => {}) {
  const m = moveBackupObjects(file);
  const total = [...m.values()].reduce((s, d) => s + d.length, 0) || 1;
  let done = 0;
  const tick = (n) => { done += n; progress(done, total); };
  for (const id of FM1.RESTORE) if (m.has(id)) await putObject(rq, id, m.get(id), tick);
  for (const id of FM1.SAMPLES) {
    if (!m.has(id)) continue;
    const d = m.get(id), k = id - 32;
    if (!d.length) {
      const e = parse[CMD.SMP_ERASE](await rq(req.smpErase(k), { timeout: 2500, retries: 0 }));
      if (e.rc) throw new BackupError("bkWrite", `erase USR${k + 1} rc ${e.rc}`);
      continue;
    }
    await uploadSlot(rq, k, d, tick);
  }
  return true;
}

/* ---- projects: the four slots and the working project ---- */
async function projectSlots(rq) {
  const L = parse[CMD.BK_LIST](await rq(req.bkList(), { timeout: 5000, retries: 0 }));
  if (L.rc) throw new BackupError("bkRc" + L.rc, `LIST rc ${L.rc}`);
  const len = (id) => (L.items.find((x) => x.id === id) || { len: 0 }).len;
  return { work: len(FM1.WORK), slots: Array.from({ length: FM1.NPROJ }, (_, k) => len(FM1.PROJ0 + k)) };
}
/* a single project as a file (.fm1m.json): the FM1M stream, base64 */
async function projectExport(rq, info, slot) {        /* slot 0..3, or -1 = the working project */
  const id = slot < 0 ? FM1.WORK : FM1.PROJ0 + slot, d = await getObject(rq, id);
  return { format: "fm1move-project", version: 1, firmware: info.version, date: new Date().toISOString(),
    len: d.length, crc: crc32(d), data: b64enc(d) };
}
async function projectImport(rq, file, slot) {        /* into slot 0..3, or -1 = the working project (it plays now) */
  if (!file || file.format !== "fm1move-project" || typeof file.data !== "string") throw new BackupError("bkBad", "not an FM1 Move project");
  const d = b64dec(file.data);
  if (d.length !== file.len || crc32(d) !== (file.crc >>> 0)) throw new BackupError("bkBad", "damaged project file");
  await putObject(rq, slot < 0 ? FM1.WORK : FM1.PROJ0 + slot, d);
}
/* PROJECT (cmd 9): 0 load, 1 save, 2 query; status 1 = refused while it plays */
async function projectOp(rq, op, slot) {
  return parse[CMD.PROJECT](await rq([CMD.PROJECT, [op & 0x7F, slot & 3]], { timeout: 4000, retries: 0 }));
}
