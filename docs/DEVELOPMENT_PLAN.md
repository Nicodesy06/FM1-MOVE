# FM1 Move — Development plan

Living document. Milestones follow the master brief (M0–M7); each step is small, built, tested and committed.

## Principles for this codebase

1. **Keep SLOOP's platform intact**: HAL, boot guard, update loader, USB rescue, flash driver. They are
   hardware-verified upstream and are what makes an install recoverable.
2. **One step = one build + host tests.** `tools/build.py` must pass (memory / flash / register checks), and
   `tests/run_tests.sh` must stay green except the documented baseline exception (`TEST_REPORT.md`).
3. **No golden rewrite without a reason.** `GOLDEN_UPDATE=1` only for an intended sound change, with the reason in
   the commit message.
4. **Simulation ≠ hardware.** Every report separates host results from device results.

## M0 — Technical foundation (current)

- [x] Baseline chosen: SLOOP 2.5 (tag `v2.5`), imported with its history.
- [x] JieLi toolchain + 3 SDK files fetched; baseline firmware builds (565 200 B image).
- [x] Baseline host tests run (one known failure, see `TEST_REPORT.md`).
- [x] Audit documents: `HARDWARE_AUDIT.md`, `CODEBASE_MAP.md`, `FEATURE_MATRIX.md`, `MEMORY_BUDGET.md`.
- [x] `CLAUDE.md` for future sessions.
- [ ] Stack high-water measurement [TEST-HW].

## M1 — Eight-track foundation

Order (each a commit):

Status: steps 1–6 done on the host; the FM1MOVE firmware builds with eight tracks, the new UI and FM1M projects.
Open in M1: MIDI-out track kind, per-track MIDI channel parameter, import of SLOOP project slots 1–4 (the autosave is
imported). The editor protocol runs in FM1MOVE (eight tracks, FM1M project backup, USR1–3) and the device page
(`docs/device/`) manages tracks, presets, samples and kits, FM6, projects and backup; step / piano-roll editing there
is still open.

1. **Track kind.** `track_t.kind` (SYNTH, DRUM, MIDI); `is_drum(t)` reads it; `TDRUM` / `TRK_DRUM` uses replaced by
   "the drum track(s)". Still 4 tracks: behaviour identical, golden renders unchanged.
2. **Synth loops over kinds.** `NPART` loops (`voice.c`, `fx.c`, `seq.c`, `engines.c`) become loops over tracks whose
   kind is SYNTH. Engine arenas become a claimed pool (`N_ARENA = 3`).
3. **Drum engine per track.** The global `drums` state keeps one shared drum voice pool (6 voices, tagged with
   the owning track); kit, level, sends, filter per drum track.
4. **NTRK = 8.** Bit masks audited (`song.rec`, `song.solo`, `panic_req`, LEDs); per-track MIDI channel parameter.
   Default project: 1 DRUM + 7 SYNTH (bass, chords, lead, pad, … presets), all selectable.
5. **Project format FM1M v1** (sparse, multi-object), importer from FUN1–FUN5 (four tracks land on 1–4, others
   default). Never overwrite a legacy object without converting a copy first.
6. **Tests.** Eight simultaneous tracks render bounded with no hanging voice; voice budget across 8 tracks; mute/solo
   bits 0–7; project round trip with 8 tracks; legacy import; CPU proxy with 8 playing tracks.

Acceptance: 8 tracks play together in the host sim with documented polyphony (8 synth voices shared + drum
voices), and the firmware image fits.

## M2 — Groove Engine

`firmware/src/groove.c` (pure, host-testable, no globals): per-event pipeline

```
raw position (units)            ← recorded, never modified
→ quantise (grid, strength)      q = raw + s·(grid − raw)
→ groove template (offset, vel, accent per grid slot, × amount)
→ swing (only when the template is "straight": swing *is* a template, never both)
→ manual micro-timing (per note / step)
→ humanise (seeded hash of (track, step, pass): deterministic, stable after save/load)
→ scheduler: absolute unit position → block + sample offset
```

Integration: the sequencer reads the computed position for the next step inside a lookahead window of one step,
so early events (negative offsets) fire before the grid step, across the pattern wrap. Details: `GROOVE_ENGINE.md`.

## M3 — Workflow (clips, scenes, capture, locks)

Clip = a pattern slot per track (6 per track, sparse). Scene = one clip per track; launch quantised to the bar. Capture = ring buffer of
the last N key events (target 512 events ≈ 16 bars of dense playing; exact numbers documented with the
implementation).

Status: clips, scenes, quantised launch and the STEP page done and verified **in host simulation only**
(`tests/clip_test.c`, `tests/fm1move_ui_test.c`); not yet run on an FM-1.

- [x] Flash layout for clips: 2 project slots + the autosave, 3 chunks each (`FMP_MAX` 11 472 B); FM1 Move 0.1's
  project 1 and autosave still load (`PROJECT_FORMAT.md`).
- [x] Clips: 8 tracks × 6 clips (`firmware/src/clip.c`), one 8 KiB packed bank in `.pool`; everything that writes a
  pattern writes the clip playing; full bank refused with "CLIP MEMORY FULL", nothing playing lost.
- [x] Scenes: clip k on every track (an empty clip: that track goes quiet).
- [x] Launch quantised to the bar: in the audio interrupt at the start of the bar, the new clip on its step 0;
  stopped, at once.
- [x] Project stream version 2 carries the clips; version 1 projects load (pattern → clip 1).
- [x] Panel: CLIPS held + white keys 1–6 / 9–14, CLIPS screen (8 × 6 grid, cursor, launch, copy, clear), key LEDs.
- [x] SEQUENCER STEP page (SLOOP's step editor restored): piano roll, STEP / NOTE / TIME / FLAG, acid-style key entry;
  accent and slide editable from the panel again.
- [x] Layer pages as on SLOOP (a125cb0): STEPS (SEQ held, 4 × 4 steps) and KEY (SEL held, the chord / note of
  every white key), knob values shown.
- [ ] Capture (ring buffer of the last key events).
- [ ] Follow actions, a launch quantise other than the bar (once planned on the CLIPS knob strip).
- [ ] Hardware: launch timing, LEDs, CLIPS screen on a device [TEST-HW].

## M4 — Premium UI (starts now, in parallel)

1. [x] `docs/UI_DESIGN_SYSTEM.md`, `UI_NAVIGATION.md`, `UI_MAPPING.md`.
2. [x] Design-system C module (`firmware/src/ds.c`): tokens, Inter fonts (OFL) rasterised by `tools/gen_dsfont.py`,
   band renderer, components.
3. [x] Screens rendered **by that firmware code** on the host into 240 × 240 PNG mockups (`tests/ui_mockups.c`), plus
   automatic contrast, label-fit and partial-redraw checks.
4. [x] Wire to live data; replace SLOOP's drawing code behind a build switch (`FM1MOVE=1`); flash/RAM measured.
5. [x] Virtual FM1: navigation tested on simulated controls (`tests/fm1move_ui_test.c`).
6. [ ] Hardware: readability, frame time, real controls [TEST-HW] — needs the owner's device.

## M5 — Sound & performance, M6 — Dual-core R&D, M7 — Stabilisation

As in the master brief. M6 starts with reading the AC79 SDK's CPU1 start sequence (`cpu/wl82`) in a separate
experimental build that never ships; see `DUAL_CORE_RESEARCH.md` when it exists.

## Risks

| Risk | Impact | Mitigation |
| --- | --- | --- |
| Flash: 16 KiB free | new UI / features do not fit | replace old UI code, glyph subsets, RLE fonts; trim CC0 sample sets only as a documented last resort |
| RAM: 13 KiB free | 8 tracks + clips | sparse project, band renderer, arena pool; clip bank 8 KiB in `.pool` (10 992 B pool spare) |
| CPU with 8 tracks | dropouts | voice budget stays global (8 synth voices); per-track insert cost measured; shedding policy unchanged |
| No hardware in the loop | regressions only visible on device | keep platform code untouched; ask the owner to test builds; never flash without permission |
| UI rewrite breaks the tested input logic | usability regressions | keep `ui_input.c` behaviour tests (`ui_pages_test.c`) green while the drawing layer changes |
