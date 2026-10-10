# CLAUDE.md — FM1 Move

Custom groovebox firmware for the **M-VAVE FM-1** (JieLi AC791N/WL82, pi32v2, 512 KiB SRAM, 1 MiB flash,
240 × 240 ST7789 SPI TFT, 27 keys, 14 buttons, 7 push-less encoders). Based on **SLOOP 2.5** (GPL-3.0-only),
itself based on Felucca. Branch for work: `fm1-move/dev`; `master` = untouched SLOOP 2.5 baseline.

Read first: `docs/DEVELOPMENT_PLAN.md` (milestones and status), `docs/HARDWARE_AUDIT.md`, `docs/MEMORY_BUDGET.md`,
`docs/CODEBASE_MAP.md`, `docs/UI_DESIGN_SYSTEM.md`, `docs/TEST_REPORT.md`.

## Non-negotiables

- **Eight tracks** architecturally (`NTRK 8`), kind per track (synth / drums / MIDI). Reduce polyphony, heavy-engine
  arenas or FX — never the track count.
- **Premium UI** is a first-class requirement: follow `docs/UI_DESIGN_SYSTEM.md` (tokens, type scale, components).
  No debug-looking screens, no tiny text (min 11 px cap labels, values ≥ 16 px), state never by colour alone.
- **Audio first**: nothing blocking, no flash access, no LCD, no allocation in the audio ISR (`events_block` and below).
- **Never claim** a feature, test or hardware result that was not actually implemented and run. Host simulation is
  not hardware testing — say which one.
- **Never flash a device** or touch bootloader/flash layout without explicit permission from the owner.
- Keep SLOOP's platform (HAL, boot guard, OTA loader, USB rescue, flash driver) unchanged unless a change is
  required and reviewed.

## Build and test

```sh
tools/get_deps.sh /home/user/deps        # once: JieLi toolchain + 3 SDK files; prints the exports below
export JIELI_TOOLCHAIN=/home/user/deps/jieli/toolchain AC79_SDK=/home/user/deps/ac79sdk
sh build.sh                              # -> build/felucca.bin, build/felucca.fwsc (fails on RAM/flash/register violations)
SOAK_MIN=2 SKIP_CPU_VALGRIND=1 sh tests/run_tests.sh   # host tests (needs build/ first)
```

(`build.sh` / `tests/run_tests.sh` lack the exec bit in this checkout: call them with `sh`.)
Known baseline failure: `target cost … fm1_alnk0_irq` (+54 %), present on untouched SLOOP 2.5 — see
`docs/TEST_REPORT.md`. Do not "fix" it with `BUDGET_UPDATE=1` without a measured reason.
`GOLDEN_UPDATE=1` only for an intended sound change, with the reason in the commit.

UI mockups (rendered by the firmware's own drawing code on the host): see `docs/UI_DESIGN_SYSTEM.md` §Tooling.

## Architecture in one breath

Unity build `firmware/src/felucca.c`. Audio ISR (ALNK0) renders 256-frame halves in 32-sample blocks:
`mix_block` → `events_block` (transport, MIDI, keys, `seq_tick` per track) → per-track render/insert → buses →
master. TIMER5 (10 kHz) scans the matrix and polls USB/UART. The main loop runs the UI (~60 fps cap) and writes
flash only when quiet. Big buffers live in `.pool`. Transport clock unit = 1 sample at 1 BPM (`BEAT_U = FS·60`):
every division is an integer number of units → no drift.

## Code conventions

- C99 subset for clang 4 / gcc host builds; no 64-bit divisions, no libc beyond `libc.c`, no malloc.
- Comments in plain English, explaining *why*; match SLOOP's comment density and naming (`trk_*`, `seq_*`, `ui_*`).
- New modules prefer pure functions with explicit inputs so `tests/` can compile them on the host.
- SPDX header on every new file: `/* SPDX-License-Identifier: GPL-3.0-only */`.
- Fonts/assets: only legally redistributable ones (Inter: SIL OFL 1.1, Terminus: OFL). Licence files in `assets/fonts/`.

## Hardware limits (measured)

App slot 581 564 B (16 KiB free at baseline); RAM 96 KiB (13 KiB free); pool 336 KiB (≥ 8 KiB must stay spare);
LCD SPI 12 MHz → a full frame ≈ 77 ms, so redraw only changed bands. No audio input, no TRS MIDI out, BT unused.

## Recovery

Hold OCT− at power-on → USB rescue (polled updater). Two failed boots → rescue automatically. OCT− + OCT+ 5 s while
stopped → mask-ROM UBOOT. Last resort: FM-1-transporter. Return to official firmware through the web installer.

## Milestone status

See the checklist in `docs/DEVELOPMENT_PLAN.md` (single source of truth). Update it and `docs/CHANGELOG.md` with
every checkpoint commit.
