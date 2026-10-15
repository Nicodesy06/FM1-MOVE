# FM1 Move — Test report

All results below are **host simulation** (Linux x86-64, gcc) or **static analysis of the target build**. No
FM1 Move build has been run on an FM-1 yet.

## Environment

| Item | Value |
| --- | --- |
| Host | Linux 6.18 x86-64 (cloud container), gcc, Python 3 + Pillow 12.3, Node 22 |
| Target toolchain | `jieli-linux-toolchains-20260730.1` (clang 4.0.1 pi32v2) from `pkgman.jieliapp.com` |
| SDK files | `fw-AC79_AIoT_SDK` tag `AC79NN_SDK_V1.2.1_2023-12-13` (sparse: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin`) |
| Windows / WSL | not available in this environment; `build-sloop.ps1` / `tools/build_windows.py` not exercised |

## Baseline: SLOOP 2.5 unmodified (2026-10-10)

Build: **OK** — image 565 200 B, RAM .data+.bss 84 820 / 98 304 B, pool 333 948 / 344 064 B, package 610 019 B.

Host tests (`SOAK_MIN=2 SKIP_CPU_VALGRIND=1 tests/run_tests.sh`): 36 groups; **35 pass, 1 fails**:

- `regression: target cost of the render loops` — `fm1_alnk0_irq` static cost 268 vs budget 174 (+54 %, limit
  +10 %). Every other render function is within budget. This is a static instruction count of the ISR's loops in
  the disassembly; it fails on the untouched upstream tree with this toolchain build (the budget file predates the
  2.5 ISR / USB-audio changes or a different toolchain drop). It is **not** rewritten here; it stays a known
  baseline failure until it is re-baselined deliberately with a measured reason.
- Timed host CPU entries print "OVER BUDGET … a note, not a failure" (31 notes) because valgrind is not used
  (`SKIP_CPU_VALGRIND=1`); these are informational by design.

Golden renders: 182, 0 changed. ASan + UBSan stress: pass. Web tests: pass.

## FM1 Move changes

### M1 steps 1–3 + eight-track engine (host)

Build (SLOOP layout, FM1MOVE=0): OK, image 567 312 B, RAM 86 064 B (+1 244 B: per-track synth state for every
track), pool unchanged.

Host suite: **182 golden renders unchanged** after every M1 step (the four-track sound is bit-identical); the only
failure is still the baseline `fm1_alnk0_irq` static cost. New groups:

| Test | Result |
| --- | --- |
| FM1 Move UI (ten screens, label fit, partial redraw) | pass |
| UI contrast (WCAG after RGB565) | pass |
| Eight tracks, `FM1MOVE=1` (`tests/eight_track_test.c`) | pass (16 checks) |
| Eight tracks under ASan + UBSan | pass |

Eight-track session (2 drum tracks with different kits, ANALOG bass, DIGITAL chords 32 steps, LOFI lead 12 steps,
FM6 7 steps, GRAIN 64 steps, PHYS): every track audible (peaks 5 679 … 100 920); synth voices ≤ 8, drum voices ≤ 6;
output bounded and finite; bit-identical in a second process; a fourth heavy engine refused, freed after a switch;
hat choke per drum track; solo track 8 / mute track 7 by bit; eight protected leads + a ninth note keep the budget
(this check **fails on SLOOP's voice.c**, passes with the new last-resort steal); kind change leaves no voice; STOP
frees everything within 2 s. Host time per 32-sample block: 8 tracks ≈ 10 µs vs ≈ 3 µs for the first four (the
heavy engines are on tracks 5–8). Device cost: [TEST-HW].

### FM1MOVE firmware build + virtual FM1 (2026-10-10)

Build `FM1MOVE=1`: OK — image **518 836 B** (app slot 581 564 B: 62 KB free), RAM 92 560 / 98 304 B, pool
322 812 / 344 064 B (21 KB free, 8 KB kept spare). SLOOP layout build: OK, 567 872 B.

New host groups (all pass, each also under ASan + UBSan):

| Test | Checks |
| --- | --- |
| `groove_test` | 26 — pipeline arithmetic, measured firing times, wrap, 64-bar drift, pads, commit/undo |
| `projfmt_test` | 15 — round trip, capacity, power loss at every write, damage, heavy-engine limit |
| `fm1move_ui_test` (virtual FM1) | 36 + a 12 000-frame random-input fuzz: navigation, layers, knobs, groove, confirmations, projects |

Bugs the virtual FM1 found and fixed before any device run: a null-pointer dereference when a knob with no function
was turned (would have been a fault on the device); encoders moving one step per frame instead of one per detent; a
cancelled kind change leaving a stale request.

Golden renders: 182 unchanged. Known baseline failure unchanged (`fm1_alnk0_irq` static cost).
**No FM1 Move build has run on a physical FM-1.**

### Editor protocol in FM1MOVE + device page (2026-10-10)

Build `FM1MOVE=1` with the editor: OK — image **536 164 B** (45 KB free), RAM 92 928 / 98 304 B (the editor's
buffers and watch state in `.pool`: +368 B RAM), pool 324 688 / 344 064 B (19 KB free, 8 KB kept spare). SLOOP layout
build: OK, 567 920 B (+48 B: `st_crc32_more`).

`web/test_device.mjs` — the device page's code (SLOOP's protocol section + `web/device_lib.js`) against the FM1MOVE
firmware compiled to WebAssembly (`editor.c`, `project.c`, `projfmt.c`, `upreset.c`, `storage.c` on a simulated
flash): 27 checks, all pass — INFO / TRACK (eight tracks, kinds), a parameter the panel lacks (arp mode) through
TRACK_PARAM, user presets (store, list, get, put, erase), a drum kit uploaded into USR2, **USR4 refused** (its flash
is the FM1M projects'), PROJECT save / load (refused while playing, nothing written), a full backup (13 objects) and
restore (working project, preset, project slots, sample back), project files (export, import, a damaged and a
truncated stream refused whole, nothing written), a **dense 5 204 B project (two flash chunks) restored byte for
byte** as the working project and into a slot, FM6 bank write.

Browser (headless Chromium, the virtual FM1): every tab, a track's parameter pages, a preset stored, a drum kit built
from two WAV files and written to USR1, a project saved, a backup downloaded; no console errors; no horizontal
scroll at 390 px. Install wiring with a stubbed Web MIDI (no FM-1): the package downloads, passes its SHA-256 check,
SLOOP's updater runs and reports "FM-1 not found"; the FM1 Move button stays disabled until its warning is ticked.

Host suite: all groups pass except the known baseline `fm1_alnk0_irq` static cost (unchanged, fails on SLOOP 2.5
too); 182 golden renders unchanged; `fm1move_ui_test` 39 checks (new: any button held + SELECT sets the tempo).
**No FM1 Move build has run on a physical FM-1; the device page's USB path has not met a real FM-1.**


### Clips, scenes, STEP page, 2-slot flash layout (2026-10-10)

Commits 1d0f6f2 (flash layout), d50541f (clip engine), 32fb841 (clips UI), ced992f (STEP page).

Build `FM1MOVE=1`: OK — image **548 248 B** of the 581 564 B app slot (after the layer pages, a125cb0), RAM .data+.bss 93 360 / 98 304 B, pool
333 072 / 344 064 B (10 992 B spare: the 8 KiB rule kept; the clip bank is 8 KiB of it).

New and extended host tests (host simulation only):

| Test | What it checks |
| --- | --- |
| `clip_test` (new, + ASan / UBSan; in `tests/run_tests.sh`) | 25 checks with the real sequencer, mixer, project format and storage: a pattern stored as the clip playing and launched back exactly (steps, LENGTH, DIV); stopped: a launch at once; playing: the old clip through the last block of the bar, the new one in the block that starts the bar with its step 0 sounding on time, also for a LENGTH 5 clip launched after some bars; a step written in the outgoing clip's last block is kept; scenes switch every track in the same block, empty clips go quiet; STOP before the bar starts the queued clip; copy, erase, a new kind empties the track's clips; project version 2 saves and loads every clip byte for byte; a full bank refuses the store, and the pattern playing still saves with the project and loads back exactly |
| `projfmt_test` (extended) | FM1 Move 0.1 layout: project 1 and the autosave still load, a slot over other projects' chunks reads as empty; the densest project against the 3-chunk capacity |
| `fm1move_ui_test` (extended) | CLIPS: CLIPS held + white keys launch on the bar, a written step lands in the clip playing, EDIT copies (asks before replacing; another button cancels), KNOB 1 moves the cursor, REC asks then clears one clip and does not record, CLIPS held + key 9 launches scene 1, CLIPS tapped launches the cursor's clip, STOP before the bar starts it; STEP page: SEQ again opens it (drum track: grid only), key entry and chords, letting go moves on, STEP / NOTE / TIME / FLAG, ALGORITHM, EDIT clears a step |
| `web/test_device.mjs` (extended) | 2 project slots; a 7.8 KB three-chunk restore |

Full suite run (`SOAK_MIN=2 SKIP_CPU_VALGRIND=1 sh tests/run_tests.sh`, SLOOP-layout `build/`, host PC): every test passes
except the known baseline `target cost … fm1_alnk0_irq` (+54 %, unchanged, present on untouched SLOOP 2.5). The timed
CPU notes ("timed: a note, not a failure") came from a loaded machine. `node web/test_device.mjs`: all ok against the
rebuilt wasm firmware. Host simulation only: nothing here ran on an FM-1.
**No FM1 Move build has run on a physical FM-1.**


### Layer pages (STEPS, KEY) — host simulation
`tests/fm1move_ui_test.c`: SEQ just pressed shows nothing (no flash on a tap); held 0.3 s: STEPS with note / tie / accent marks and DIV · SWING · LENGTH values; SEQ + key still writes the step; let go: back to the screen; SEL held: KEY with a chord on every white key, each label fits its tile; KNOB 2 turns the scale two steps (the fixed base); the drum track's STEPS. All checks ok, also under ASan + UBSan. Not run on an FM-1.


### FM1 Move 0.3: SLOOP 2.6 merged, performance tempo (2026-10-14) — host simulation

Commit d7590be (upstream SLOOP `v2.6` merged into `fm1-move/dev`).

Build `FM1MOVE=1`: OK — image **569 828 B** of the 581 564 B app slot, RAM .data+.bss 96 496 / 98 304 B (1 808 B free),
pool 333 160 / 344 064 B (10 904 B spare: the 8 KiB rule kept). See `MEMORY_BUDGET.md` §1c.

`tests/fm1move_ui_test.c` (virtual FM1), new checks:

| Area | What it checks |
| --- | --- |
| Tempo | PLAY: ALGORITHM turns the tempo with no button held and the TEMPO readout shows, then goes; OCT− + OCT+ tapped in time sets the tempo and leaves the octave at 0 |
| THROWs | FX + OCT− held: REV THROW; with OCT+ too: ECHO THROW as well |
| Punch-in | FX + the first black key (F#): KILL LOW (27 effects) |
| PERFORM | page 2 the black keys' effects (4 pages); page 3 TEXTURE (OFF, then KNOB 1 picks a bed) |
| SOUND | page 4 SYNC (LFO SYNC / TRIG / POL, ENV SYNC), page 6 INSERT, page 7 INS MIX and ANALOG's FLT TYPE |
| INSERT limit | four tracks get an INSERT, a fifth stays dry and the panel says so |
| MIDI LEARN | GLO + C# turns it on; a knob, then CC 21: learned; the CC moves REVERB on the selected track |
| STEP page | KNOB 3 on a note sets its LEN |

Live renders: `docs/ui/live/18-tempo.png`, `11c-texture.png`, `11d-perform-black.png`, `09b-sound-insert.png`.

Full suite (`SOAK_MIN=2 SKIP_CPU_VALGRIND=1 sh tests/run_tests.sh`, SLOOP-layout `build/`, after the merge): run; every
test passes except the known baseline below.
SLOOP-layout golden renders: 182, identical to SLOOP 2.6's; the only failure is the known baseline
`target cost … fm1_alnk0_irq` (+54 %, unchanged).

Found by reading the code (the docs pass), then fixed: MIDI Program Change and 2.6's step entry from a MIDI keyboard
were queued by `seq.c` but drained only in SLOOP's `ui_input.c`; FM1 Move's controller now drains both. Checked in
`tests/fm1move_ui_test.c` (a chord from a MIDI keyboard on the STEP page, the next step on release; Program Change
loads the n-th sound).

Host simulation only: **no FM1 Move build has run on a physical FM-1.**

### 0.3.1: the testers' reports (host simulation)

Details: `docs/BUGFIX_0.3.1.md`. New checks, each shown to fail on the previous code first:
- `fm1move_ui_test`: no UI frame sets `song.sel` to another track (watched over the whole run, fuzz included); a heavy
  preset refused leaves the sound and the message; PRESETS neighbours by list position; CLIPS opens on the clip
  playing; a filter lock plays on its step while the loop runs; SETTINGS › AUDIO LOAD.
- `groove_test`: strength 0 / 25 / 50 / 75 / 100 % on real firing times; an overdub keeps the step's recorded timing;
  a hand-written step gets none; the free first take keeps its timing.
- `eight_track_test`: a fading voice re-struck stays in the budget (9 → 8); the overload cap lowers and recovers.
- `web/test_device.mjs`: FM-1 port names, the page uses the updater's list.
Full suite: every test passes except the known baseline `fm1_alnk0_irq` (+54 %, cost unchanged at 268); 182 golden
renders identical. Not run on an FM-1.

### STEP page MOVE and the battery (host simulation)

`tests/fm1move_ui_test.c`, new checks: ALGORITHM on a note moves it over the next chord with its tie (the chord slides
back, the tie stays with it), its nudge, fill condition and lock go with it, the toast reads MOVED TO STEP 3; back
again; further over a rest; never past step 1; EDIT + OCT− undoes the whole move (notes, nudge, condition, lock),
EDIT + OCT+ redoes it; on an empty step ALGORITHM moves the cursor. Battery: 3 bars at a full reading, 2, empty with a
BATTERY LOW toast said once, SETTINGS › BATTERY reads EMPTY, USB animates, no reading: nothing shown. All checks ok,
also under ASan + UBSan. Full suite run: every test passes except the known baseline `fm1_alnk0_irq` (+54 %); 182
golden renders unchanged. The battery thresholds are SLOOP's and have not been checked on an FM-1 with FM1 Move.
