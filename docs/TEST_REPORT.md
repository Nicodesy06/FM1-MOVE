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
