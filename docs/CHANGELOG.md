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
