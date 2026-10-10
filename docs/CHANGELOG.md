# FM1 Move — Changelog

## Unreleased (fm1-move/dev)

### M0 — foundation
- Baseline: SLOOP 2.5 imported with history (`master`).
- Audit docs: hardware, codebase map, feature matrix, memory budget, development plan, test report; `CLAUDE.md`.
- `tools/get_deps.sh`: fetches the JieLi toolchain and the three SDK files (sparse checkout).

### UI design system (host)
- `firmware/src/ds.c`: colour tokens (neutrals, 8 track colours, semantic), Inter type scale (OFL, 4-bit AA,
  RLE, glyph subsets, 27.4 KB), anti-aliased primitives, geometric icons, band renderer that sends only changed rows.
- `firmware/src/ui_move.c`: view model + ten screens (PLAY, TRACKS, SEQUENCER, GROOVE, CLIPS, MIXER, SOUND, PERFORM,
  PROJECT, SETTINGS), focus overlay, destructive-action modal, toast.
- `tests/ui_mockups.c` + `tools/ui_mockups.py`: the firmware UI code rendered on the host to 240 x 240 PNGs
  (`docs/ui/mockups/`), with label-fit and partial-redraw checks; `tools/ui_contrast.py` (WCAG after RGB565).
- Target compile check (JieLi clang -Os): 11.2 KB code + 27.9 KB constants. Not yet linked into the firmware.
- Docs: `UI_DESIGN_SYSTEM.md`, `UI_NAVIGATION.md`, `UI_MAPPING.md`.

### M1 — eight-track foundation
- Track kind per track (`FM1MOVE=1`: 8 tracks; `0`: SLOOP's 4), loops by kind, `trk_set_kind`.
- Heavy-engine arenas (GRAIN/FM6/PHYS) as a claimed pool of 3, refused with a message when all are taken.
- One drum engine for any number of drum tracks (owner-tagged voices, per-track kit, mix, filter, slicer, choke).
- Voice budget: last-resort steal so eight protected leads never push the shared budget over 8.
- `tests/eight_track_test.c` (+ ASan/UBSan) in the host suite.

### M2 — Groove Engine (host)
- `firmware/src/groove.c`: non-destructive quantize strength on recorded raw timing, 8 templates (residual timing +
  velocity/accent maps + suggested swing), swing applied once, micro-timing, drum pad offsets (early/late),
  deterministic humanize (seed), global groove with per-track override, groove commit with undo.
- `seq.c`: groove-aware firing time and velocity, raw deviation recorded with latency compensation, late pads.
- `tests/groove_test.c` (26 checks, + ASan/UBSan). SLOOP's 182 golden renders unchanged.
- `docs/GROOVE_ENGINE.md`.

### M1.4 — project format FM1M (host)
- `firmware/src/projfmt.c`: sparse streaming format (kinds, non-default params, compact steps, locks, grooves, pads),
  2 chunks of 3 824 B with generations, commit chunk last, two-pass validate-then-apply, no project-sized buffer.
- `storage.c`: `st_save_staged` (save from st_buf in place), FM1M objects in their own 80 KiB region (FM1MOVE).
- `seq.c`: `trk_silence` (shared by kind changes and loads).
- `tests/projfmt_test.c` (+ ASan/UBSan): round trip, capacity, power loss at every write, damage, heavy limit.
- `docs/PROJECT_FORMAT.md`.

### FM1MOVE firmware build (new UI integrated)
- `FM1MOVE=1 sh build.sh`: eight tracks, the FM1 Move UI (ds.c, ui_move.c, `ui_move_ctl.c` controls + live view
  model), FM1M projects with SLOOP autosave import, design-system boot screen and system screens; SLOOP's drawing
  code, Terminus font, logo, song sections and web editor left out. Image 518 836 B.
- Default project: drums (808), ACID 303, RHODES, SUPERSAW, WARM PAD, 8BIT ARP, a second drum kit, M1 PIANO.
- Layers for eight tracks: SEL + keys 1–8 select, GLO + keys 1–8 / 9–16 mute / solo, fill and tap tempo on black keys.
- `tests/fm1move_ui_test.c`: a virtual FM1 (36 checks + fuzz, ASan); live screenshots in `docs/ui/live/`.

### Editor protocol in FM1 Move, the device page, SLOOP parity
- `editor.c` in the FM1MOVE build (`FELUCCA_EDITOR` 1): eight tracks (every track's level its own, the solo mask in
  two bytes, DRUM_STEP on the selected / first drum track), PROJECT on the FM1M slots, version "FM1 MOVE 0.1".
  Backup objects: settings, user presets, FM6 bank, SYN kits, **16 the working project, 17–20 projects 1–4** (FM1M
  streams, read through a window: no project-sized buffer), USR1–USR3. A restored stream is staged chunk 0 in
  `proj_tmp`, chunk 1 in `st_buf`, parsed whole, then written as `fmp_save` writes (a cut-off restore leaves the old
  project); the autosave and the panel's project actions wait meanwhile (`fmp_hold`). **USR4 is refused** (its flash
  holds the projects). Editor buffers in `.pool`.
- `projfmt.c`: a RAM reader (`fmp_open_ram`), `fmp_parse_r`, `fmp_put_chunk`, `fmp_restore`, `fmp_read`, `fmp_stored`,
  `fmp_window`; the parser also checks the stream ends exactly at its length. `storage.c`: `st_crc32_more`.
- Panel: any function button held + SELECT sets the tempo (SLOOP's way; FM1 Move had only tap tempo).
- `docs/device/` (`web/device.html`, `web/device_lib.js`, `web/make_device.py`): install FM1 Move (experimental, behind
  an explicit warning), SLOOP 2.5 (stable) or the official V15 with SLOOP's updater unchanged; manage tracks and every
  parameter, user presets, sample slots and drum kits (WAV / any audio the browser decodes), the FM6 bank (DX7
  .syx), projects (files), backup / restore. A virtual FM1 mode runs the firmware in WebAssembly.
- `web/sim/fm1sim.c` now runs the real `upreset.c`, `project.c` and `editor.c` (power-on and autosave as `main.c`);
  the simulator page keeps the whole flash (shared with the device page).
- `web/test_device.mjs` (in `tests/run_tests.sh`), `docs/SLOOP_PARITY.md`.

### Browser test view (simulation)
- `web/sim/fm1sim.c` + `tools/build_sim.sh`: the FM1MOVE firmware sources (engines, sequencer, Groove Engine,
  project format, design system, controller) compiled freestanding to WebAssembly with stubbed LCD, encoders,
  LEDs and flash; no imports. Projects persist in the browser (localStorage holds the project flash region).
- `docs/sim/index.html`: simulated FM-1 panel (7 knobs, 12 buttons, OCT−/OCT+, 27 keys; mouse, touch, computer
  keyboard), 240 x 240 screen, audio via Web Audio. Checked in headless Chromium (render, transport, LEDs, autosave
  + reload, 390 px phone layout). Not a hardware test: timing, CPU load and the display bus differ.
