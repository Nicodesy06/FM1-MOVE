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

