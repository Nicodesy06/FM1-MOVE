// SPDX-License-Identifier: GPL-3.0-only
//
// The FM1 Move device page's logic against the FM1 Move firmware itself, compiled to WebAssembly (docs/sim/fm1sim.wasm,
// tools/build_sim.sh: the real editor.c, project.c, upreset.c, storage.c on a simulated flash). No browser, no
// hardware: what this proves is the protocol and the flash logic, not USB timing on an FM-1. Run from the repo root:
//   node web/test_device.mjs
// It uses SLOOP's editor protocol section (web/editor.html PROTO-BEGIN/END) and web/device_lib.js, as the page does.

import { readFileSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath } from "node:url";
import vm from "node:vm";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(66)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const HERE = fileURLToPath(new URL(".", import.meta.url));
const ROOT = join(HERE, "..");

const html = readFileSync(join(HERE, "editor.html"), "utf8");
const proto = html.slice(html.indexOf("/*PROTO-BEGIN*/"), html.indexOf("/*PROTO-END*/"));
const lib = readFileSync(join(HERE, "device_lib.js"), "utf8");
const E = vm.runInNewContext(proto + "\n" + lib + `
;({ CMD, req, parse, Link, crc32, buildSlot, SMP, FM6, FM1, isMove, moveVersion, trackRows, moveBackupCapture, moveBackupRestore,
   moveBackupObjects, projectSlots, projectExport, projectImport, projectOp, uploadSlot, getObject, bank, BackupError })`,
{ setTimeout, clearTimeout, setInterval, clearInterval, console, TextEncoder, TextDecoder, Blob, Response, queueMicrotask });

/* ---- the firmware, booted on an erased flash ---- */
const { instance } = await WebAssembly.instantiate(readFileSync(join(ROOT, "docs/sim/fm1sim.wasm")), {});
const X = instance.exports;
let ms = 1000;
const ui = (n = 1) => { for (let i = 0; i < n; i++) { ms += 16; X.sim_ui(ms); } };
X.sim_init();
X.sim_boot();
ui(3);

/* the page's link: a request into the device, its replies (and pushes) back after the current task */
let pushes = 0;
const link = new E.Link((d) => {
  const inner = Uint8Array.from(d.slice(1, -1));
  new Uint8Array(X.memory.buffer, X.sim_rx_ptr(), inner.length).set(inner);
  if (!X.sim_rx(inner.length)) throw new Error("device busy");
  const n = X.sim_tx_len(), out = new Uint8Array(X.memory.buffer, X.sim_tx_ptr(), n).slice();
  X.sim_tx_clear();
  queueMicrotask(() => {
    let s = 0;
    for (let i = 0; i < out.length; i++) if (out[i] === 0xF7) { link.receive(out.slice(s, i + 1)); s = i + 1; }
  });
}, { timeout: 500, onPush: () => pushes++ });
const rq = (r, o) => link.request(r, o);

/* INFO and the tracks */
const info = E.parse[E.CMD.INFO](await rq(E.req.info()));
ok(E.isMove(info) && info.ntrk === 8 && info.proto === 10 && E.moveVersion(info) === "FM1 MOVE 0.3", `INFO: ${info.version}, ${info.ntrk} tracks, protocol ${info.proto}`);
const tr = E.parse[E.CMD.TRACK](await rq(E.req.track()));
const rows = E.trackRows(info, tr);
ok(rows.length === 8 && rows[0].kind === "drums" && rows[6].kind === "drums" && rows[1].kind === "synth", "TRACK: eight tracks, drums on 1 and 7 (the default project)");

/* a parameter the panel does not reach (the arpeggiator's mode, track 4), through TRACK_PARAM */
{
  let amode = -1;
  for (let i = 0; i < info.pcount; i++) if (E.parse[E.CMD.DESC](await rq(E.req.desc(0, i))).label === "MODE") { amode = i; break; }
  const set = E.parse[E.CMD.TRACK_PARAM](await rq(E.req.trackParam(3, amode, 2)));
  const get = E.parse[E.CMD.TRACK_PARAM](await rq(E.req.trackParam(3, amode)));
  ok(amode >= 0 && set.value === 2 && get.value === 2, "TRACK_PARAM: the arp mode of track 4 set and read back");
}

/* user presets: store the sound of track 2, list, read, rename through PUT, erase */
{
  await rq(E.req.track(1));
  let rc = await E.bank.store(rq, 0, "MY BASS");
  const list = await E.bank.list(rq);
  const got = await E.bank.get(rq, info, 0);
  ok(rc === 0 && list.slots[0].used && list.slots[0].name === "MY BASS" && got.used && got.name === "MY BASS", "user presets: STORE, LIST, GET");
  got.name = "BASS 2";
  rc = await E.bank.put(rq, 1, got);
  const l2 = await E.bank.list(rq);
  ok(rc === 0 && l2.slots[1].name === "BASS 2", "user presets: PUT into another slot");
  rc = await E.bank.erase(rq, 1);
  ok(rc === 0 && !(await E.bank.list(rq)).slots[1].used, "user presets: ERASE");
}

/* a sample slot as a drum kit: build, upload into USR2, SMP_INFO; USR4 is refused (its flash is the projects') */
{
  const tone = (f, n) => Float32Array.from({ length: n }, (_, i) => 0.6 * Math.sin(2 * Math.PI * f * i / SMP.RATE) * Math.exp(-i / (n / 4)));
  const SMP = E.SMP;
  const slot = E.buildSlot("TESTKIT", [{ s: tone(80, 6000), root: 36, lo: 0, hi: 36 }, { s: tone(400, 4000), root: 38, lo: 37, hi: 127 }]);
  await E.uploadSlot(rq, 1, slot);
  const si = E.parse[E.CMD.SMP_INFO](await rq(E.req.smpInfo()));
  ok(si.nslots === 3 && si.slots[1].zones === 2 && si.slots[1].name === "TESTKIT", `samples: USR2 uploaded (${si.nslots} slots offered)`);
  let refused = false;
  try { await rq(E.req.smpBegin(3), { timeout: 200, retries: 0, quiet: true }); } catch (e) { refused = /timeout/.test(e.message); }
  ok(refused, "samples: SMP_BEGIN on USR4 gets no answer (FM1 Move offers USR1..3)");
}

/* projects: save the working project into slot 2, then a backup, then change everything, then restore */
{
  let p = await E.projectOp(rq, 1, 1);
  ok(!p.busy && p.used, "PROJECT: saved into slot 2");
  const slots = await E.projectSlots(rq);
  ok(slots.work > 0 && slots.slots[1] > 0 && slots.slots[0] === 0, `projects: working ${slots.work} B, slot 2 ${slots.slots[1]} B, slot 1 empty`);
  const file = await E.moveBackupCapture(rq, info);
  const ids = file.objects.map((o) => o.id);
  ok(file.format === "fm1move-backup" && [1, 6, 7, 8, 9, 16, 17, 18, 32, 33, 34].every((i) => ids.includes(i)) && ![0, 19, 20, 35].some((i) => ids.includes(i)),
    `backup: ${file.objects.length} objects, ${file.objects.reduce((s, o) => s + o.len, 0)} B (no USR4, no SLOOP projects)`);

  /* the working project changes (track 2's level, track 4 becomes empty of its arp), a preset is erased */
  const lvl0 = E.parse[E.CMD.TRACK_PARAM](await rq(E.req.trackParam(1, 0))).value;
  await rq(E.req.trackParam(1, 0, 3));
  await E.bank.erase(rq, 0);
  ui(2);
  await E.moveBackupRestore(rq, file);
  ui(2);
  const lvl1 = E.parse[E.CMD.TRACK_PARAM](await rq(E.req.trackParam(1, 0))).value;
  const l3 = await E.bank.list(rq);
  const slots2 = await E.projectSlots(rq);
  ok(lvl1 === lvl0 && lvl0 !== 3, `restore: the working project is back (track 2 level ${lvl1})`);
  ok(l3.slots[0].used && l3.slots[0].name === "MY BASS", "restore: the user preset is back");
  ok(slots2.slots[1] === slots.slots[1] && slots2.slots[0] === 0, "restore: project slot 2 back, slot 1 still empty");
  const si = E.parse[E.CMD.SMP_INFO](await rq(E.req.smpInfo()));
  ok(si.slots[1].name === "TESTKIT", "restore: USR2 back");

  /* a project file: export slot 2, import it into slot 1, load slot 1 */
  const pf = await E.projectExport(rq, info, 1);
  await E.projectImport(rq, pf, 0);
  const s3 = await E.projectSlots(rq);
  p = await E.projectOp(rq, 0, 0);
  ok(s3.slots[0] === pf.len && !p.busy, `project file: slot 2 exported (${pf.len} B), imported into slot 1, loaded`);

  /* a damaged stream is refused whole: nothing written */
  const bad = Uint8Array.from(atob(pf.data), (c) => c.charCodeAt(0));
  bad[0] ^= 0x55;                                   /* (its magic: a value out of range would only be clamped) */
  let refused = false;
  try { await E.projectImport(rq, { ...pf, data: Buffer.from(bad).toString("base64"), crc: E.crc32(bad) }, 0); } catch (e) { refused = e.code === "bkBad"; }
  const s4 = await E.projectSlots(rq);
  ok(refused && s4.slots[0] === pf.len, "project file: a damaged stream is refused, slot 1 untouched");
  refused = false;
  const cut = bad.slice(0, bad.length - 1);
  cut[0] ^= 0x55;                                   /* (the magic right again, the end mark gone) */
  try { await E.projectImport(rq, { ...pf, data: Buffer.from(cut).toString("base64"), len: cut.length, crc: E.crc32(cut) }, 0); } catch (e) { refused = e.code === "bkBad"; }
  ok(refused && (await E.projectSlots(rq)).slots[0] === pf.len, "project file: a truncated stream is refused too");

  /* a SLOOP backup is not taken for an FM1 Move one */
  let sloop = false;
  try { E.moveBackupObjects({ ...file, format: "sloop-backup" }); } catch (e) { sloop = /SLOOP/.test(e.message); }
  ok(sloop, "restore: a SLOOP backup file is refused with its reason");
}

/* a dense project (two flash chunks: chunk 1 staged in st_buf on a restore): every step of the six synth tracks */
{
  let slen = -1;
  for (let i = 0; i < info.pcount && slen < 0; i++) if (E.parse[E.CMD.DESC](await rq(E.req.desc(0, i))).label === "LEN") slen = i;
  for (const t of [1, 2, 3, 4, 5, 7]) {
    await rq(E.req.trackParam(t, slen, 64));        /* (64 steps) */
    for (let i = 0; i < 64; i++)
      await rq(E.req.trackStep(t, i, { n: 4, notes: [48 + (i % 12), 52 + (i % 7), 55 + t, 60 + (i % 5)], time: 0, flags: i & 3, vel: 64 + i, lvl: i & 3, rat: (i >> 2) & 3 }));
  }
  for (const t of [0, 6]) {                         /* the two drum tracks: every lane on every step (DRUM_STEP: the selected one) */
    await rq(E.req.track(t));
    await rq(E.req.trackParam(t, slen, 64));
    for (let i = 0; i < 64; i++)
      await rq(E.req.drumStep(i, { on: 0xFFFF, lvl: Array.from({ length: 16 }, (_, l) => (l + i) & 3), rat: Array.from({ length: 16 }, (_, l) => (l * i) & 3) }));
  }
  const big = await E.projectExport(rq, info, -1);
  let p = await E.projectOp(rq, 1, 0);
  const s = await E.projectSlots(rq);
  ok(big.len > 2 * 3824 && s.slots[0] === big.len, `dense project: ${big.len} B (three chunks) saved into slot 1`);
  const file = await E.moveBackupCapture(rq, info);
  await E.projectOp(rq, 0, 1);                      /* load the small slot 2: the working project changes */
  const small = await E.projectSlots(rq);
  await E.moveBackupRestore(rq, file);
  const back = await E.projectExport(rq, info, -1), s3 = await E.projectExport(rq, info, 0);
  ok(small.work < 3824 && back.crc === big.crc && back.len === big.len, "dense project: restored as the working project, byte for byte");
  ok(s3.crc === big.crc, "dense project: slot 1 restored byte for byte");
  await E.projectImport(rq, big, 1);
  const s1 = await E.projectExport(rq, info, 1);
  ok(s1.crc === big.crc, "dense project: imported into slot 2 as a project file");
}

/* PROJECT while playing: refused, nothing done */
{
  const press = () => { X.sim_set(1 << 10, 0); ui(1); X.sim_set(0, 0); ui(1); X.sim_audio(1024); };   /* PLAY (B_PLAY = 10) */
  const before = await E.projectExport(rq, info, 1);
  press();
  const p = await E.projectOp(rq, 1, 1);
  const after = await E.projectExport(rq, info, 1);
  ok(X.sim_playing() && p.busy && after.crc === before.crc, "PROJECT: save refused while it plays (nothing written)");
  press();
  ok(!X.sim_playing(), "transport stopped again");
}

/* FM6: list and store a patch in bank slot B1 */
{
  const l = E.parse[E.CMD.FM6_LIST](await rq(E.req.fm6List()));
  const v = E.FM6.init ? E.FM6.init() : null;
  let rc = -1;
  if (v) rc = E.parse[E.CMD.FM6_PUT](await rq(E.req.fm6Put(E.FM6.TARGET.BANK, 0, E.FM6.pack(v)), { timeout: 3000 })).rc;
  const l2 = E.parse[E.CMD.FM6_LIST](await rq(E.req.fm6List()));
  ok(l.factory > 0 && rc === 0 && l2.slots[l2.factory].used, `FM6: ${l.factory} factory patches, B1 stored`);
}

link.close();
console.log(failed ? `\n${failed} FAILED` : "\nall ok");
process.exit(failed ? 1 : 0);
