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
