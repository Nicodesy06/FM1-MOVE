# FM1 Move 0.3.1 — testers' reports: status, causes, fixes, validation

Source: the FM1 Move Reddit thread (r/MVaveFM1) as summarised in the owner's brief of 2026-10-15. Every fix below is
**verified in host simulation only** (the firmware's own C code compiled for the computer, and the same code in
WebAssembly). **Nothing in this release has been run on an FM-1 by the maintainer.** Items marked [HW] need a
physical FM-1 to confirm.

## 1. Status, issue by issue

| ID | Report | Status | Hardware |
| --- | --- | --- | --- |
| BUG-01 | A key sometimes plays another preset | **Root cause found and reproduced on the host; fixed** (+ two related defects) | needs retest [HW] |
| BUG-02 | Rare glitches with PHYS + FM6 + ~3 tracks, drum transients | **Partly explained**: a voice-budget leak (host-reproduced, fixed) and overload handling that did not hold (fixed by policy); whether the device overruns is unknown without hardware | needs retest + AUDIO LOAD reading [HW] |
| BUG-03 | Installer said "No FM-1 found" (later solved by the reporter) | **Already fixed** (4e0fb9d); a remaining gap closed (Connect's port list missed SLOOP / Clipper names); regression tests added | install / connect retest [HW] |
| UX-01 | Quantize too aggressive; choose amount and grid | **Strength 0–100 % already existed** (per track, GROOVE › KNOB 2, non-destructive). Three timing defects found and fixed; grid = the track's DIV (documented); default left at 100 % | timing feel [HW] |
| UX-02 | Clearing a track hard to find | **Improved**: SAVE opens CLIPS on the selected track's clip playing (SAVE, REC, REC); the question says NO UNDO; manual + FAQ | — |
| FEAT-01 | Per-step filter? | **Already exists** (parameter locks; the filters are lock targets); verified while playing; manual FAQ | — |
| REG-01 | Clips "in development" | **Exists**; existing clip tests pass (`clip_test`, `fm1move_ui_test`) | scene changes under load [HW] |
| FEAT-02 | Longer / compressed samples | **Investigated, not implemented.** The manual's "3.7 s" was wrong: a slot holds **7.4 s** (81 408 B of 4-bit IMA ADPCM, mono, 22.05 kHz). MP3 decoding in the audio path is not proposed (see §5) | — |
| DOC-01 | Trust, stale "never run on an FM-1" text | **Updated** (manual, README, device page): community testers run it; no structured hardware QA; this report and the changelog | — |

## 2. Root causes and changes

### BUG-01 — a key plays another track's sound
- **Cause (main, intermittent).** Every UI frame, `mv_name()` (`ui_move_ctl.c`) wanted each synth track's preset
  family and did it by setting `song.sel` to that track, calling `preset_pos()` (a scan of 153 + 32 entries) and
  setting it back — without the IRQ off. The audio interrupt routes the keys by `song.sel` (`seq.c` `key_down`), as
  well as MIDI-in, Program Change targets and auditions. A key pressed while the interrupt fell in that window
  played **another track** with its preset (host repro: a key on RHODES played ACID 303). The store was confirmed in
  the target build's disassembly. Fix: `preset_pos_of(track)` (`ui.c`); the UI never writes `song.sel` but to select.
- **Cause 2 (deterministic).** Choosing a GRAIN / FM6 / PHYS sound while the three heavy-engine arenas were in use:
  the engine change was refused but the preset index (of the new engine) was applied to the old engine — another
  sound loaded (all 42 heavy entries, host). Fix: `preset_go`, `preset_go_to` (Program Change) and the editor's
  `ED_PRESET` stop when the engine was refused; "HEAVY ENGINES ALL IN USE" stays on screen.
- **Display.** The SOUND page's previous / next names were looked up by engine number (151 of 153 wrong); now by
  list position (user presets by name).
- Tests (`tests/fm1move_ui_test.c`): every value `song.sel` takes during each UI frame is watched (no frame may
  show another track; checked over the whole run including 12 000 fuzz frames); a refused heavy preset leaves the
  sound; the browser's neighbours. All three fail on the previous code and pass now.

### BUG-02 — glitches under load
- **Budget leak (host-reproduced).** A note struck again while its voice was fading out after a steal took that
  voice back without making room: up to 10 voices rendered for a budget of 8 (14 711 of 16 000 blocks over budget
  in a dense re-struck scene, host). Fix: `voice.c` makes room first. Test: `eight_track_test` (9 → 8 voices).
- **Overload handling did not hold.** On two late-ish halves (> 85 %) the firmware fades one voice, but the next
  note refilled the slot at once. Now a shed **lowers the voice limit to what still sounds** (never under 4), and it
  comes back one voice a second with no overload. Documented in the manual (*Voices and heavy load*); it only acts
  when the device measures that its audio ran late. The host never runs late, so sounds and the 182 golden renders
  are unchanged.
- **A way to report it.** SETTINGS › AUDIO LOAD (read only): peak audio load since power-on and the number of audio
  buffers that came late. (The USB console needs USB SERIAL, which FM1 Move does not offer on the panel.)
- Host measurements (x86, relative, not the device): PHYS costs ~1.3–2.5× an ANALOG voice by model, FM6 / GRAIN
  ~1.3×; the tester-like scene (2 drum tracks + PHYS + FM6 + 3 light synths) averages 2.2× SLOOP's heaviest regress
  mix, 3.4× in its worst half; track filters (+34 %), FLANGER inserts (+19 %) and DIST (+14 %) are large shares.
- **Not changed (need hardware first):** acknowledging the audio half before rendering (would turn a small overrun
  into no glitch, but depends on the ALNK0 pending-bit behaviour); cost-weighted voices (weights must be measured on
  the device); moving the clip store out of the bar-line interrupt (~3 blocks of work on a scene change, host).

### BUG-03 — installer detection
- The 0.3 fix (Connect identifies an FM-1 on any firmware) is kept. Gap found: the device page kept its own copy of
  the FM-1 port-name list, which lacked "SLOOP" and "Clipper" (Install's updater had them), so Connect could say
  "No FM-1 found" for a port Install would use. Now the page uses the updater's list (`web/device.html`,
  `web/make_device.py`). Tests in `web/test_device.mjs`: names probed (FM-1, Felucca, SLOOP, Clipper, USB-MIDI,
  OTA, composite) and not other gear; no stale copy in the page; the built page exports the list.

### UX-01 — quantize
What exists (verified): per-track strength 0–100 % (GROOVE › KNOB 2), non-destructive (each step keeps its recorded
deviation, `roff`, up to ±½ step), saved in projects; 0 / 25 / 50 / 75 / 100 % checked on real firing times
(`groove_test`). Default 100 % (SLOOP's behaviour) — **not changed**: a moderate default is a proposal for user
testing. Defects found and fixed (`seq.c`, `ui_layers.c`, `ui_move_ctl.c`):
1. **The free first take** of an empty project landed fully on the grid, whatever the strength (its timing was not
   kept). It now keeps each note's timing like any take.
2. **Overdubs** replaced the timing of the notes already on a step with the new note's (a hat overdub moved the
   kick). The first note on a step now keeps its timing.
3. **A step written by hand** on an empty step inherited an old take's timing and played off the grid below 100 %.
Each has a test in `groove_test` that fails on the previous code.

Grid: the track's step (DIV, 1/32 … 2 bars, triplets). A grid coarser than the step with partial strength would need
offsets beyond ±½ step (the scheduler's window and `roff`'s range) and a project-format change; not done. Note
lengths and ratchets stay on the step grid. Not done either: groove settings in the browser editor.

## 3. Validation matrix

| What | Host simulation (this release) | FM-1 hardware |
| --- | --- | --- |
| Keys always play the selected track (UI frame never changes `song.sel`) | pass (incl. 12 000 fuzz frames) | **retest wanted** |
| Heavy preset refused → sound unchanged, message shown | pass | retest wanted |
| PRESETS neighbours named right | pass | — |
| Voice budget holds when a fading voice is re-struck | pass (9 → 8) | retest wanted |
| Overload cap lowers on shed, recovers | pass (unit) | **only the device can overload** |
| AUDIO LOAD shows peak and late | pass (unit; the simulator reads 0 %) | readings wanted |
| Quantize 0/25/50/75/100 %, overdub, hand entry, free take | pass | timing feel wanted |
| Installer port detection | pass (static + wasm protocol) | install / connect wanted |
| Clear-track path, filter locks while playing | pass | — |
| Full suite (SLOOP layout) | see TEST_REPORT.md | — |

## 4. What needs a physical FM-1

1. **BUG-01:** eight very different presets on eight tracks; play keys stopped and playing, switch tracks and
   presets fast, after clip and project changes, with MIDI in. Any key on the wrong sound? Also try a 4th heavy
   engine: the sound must stay and the panel say HEAVY ENGINES ALL IN USE.
2. **BUG-02:** the tester's project (PHYS + FM6 + 3 tracks + dense drums). After a glitch: SETTINGS › AUDIO LOAD.
   LATE > 0 = the audio ran out of time (then: did the voice cap make it stop?); LATE = 0 = another cause (gain, a
   sound's own transient). Report the peak %. These readings also calibrate the next steps of §2.
3. **Quantize:** record at 50 % and 0 %, overdub, first free take: does it feel right?
4. **Installer:** fresh browser, Install from SLOOP / official / Clipper firmware; Connect after; permission denied.
5. **Battery bars** (0.3 thresholds from SLOOP), the USB animation.
6. **Update safety:** projects, autosave, user sounds, kits, samples and FM6 bank survive the 0.3 → 0.3.1 update
   (no flash layout or format change in this release).

## 5. FEAT-02 — longer samples (feasibility only)
Today: 3 user slots × 80 KiB (+ USR4's flash holds the projects), 4-bit IMA ADPCM at 22.05 kHz mono = 7.4 s each;
decoding costs a few operations per sample. MP3 / low-bitrate codecs would need a frame decoder (tens of KiB of code
and RAM — the app slot has ~8 KiB free and RAM ~1.7 KiB), a decode per voice in the audio interrupt (the place
BUG-02 lives), and lose sample-exact loops and cheap pitching. Not proposed. Cheaper options to measure later: a
lower sample rate per slot (11 kHz = 14.8 s, darker), or slot sharing (one kit across slots).

## 6. Data and recovery
No change to the flash layout, the project format (FM1M), presets, kits, samples, the FM6 bank, the bootloader,
the OTA loader or the USB rescue. Version string FM1 MOVE 0.3.1; the editor protocol is unchanged.
