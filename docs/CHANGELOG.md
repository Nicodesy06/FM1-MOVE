# FM1 Move — Changelog

## Unreleased (fm1-move/dev)

### FM1 Move 0.3.1 — the testers' reports (host simulation; awaiting hardware retest)
Full status, causes and the hardware test list: [docs/BUGFIX_0.3.1.md](BUGFIX_0.3.1.md).
- **Fixed: a key could play another track's sound (BUG-01).** The UI briefly made each synth track the selected one
  while naming its preset, and a key pressed in that moment went to that track. It no longer touches the selection.
- **Fixed: a GRAIN / FM6 / PHYS sound chosen with all three heavy-engine slots in use loaded a different sound** of
  the old engine (also by Program Change and the device page). The sound now stays and the panel says why.
- **Fixed: the PRESETS page named the wrong previous / next sounds.**
- **Fixed: the voice budget could leak to 10 voices (BUG-02)** when a note was struck again while its voice faded
  after a steal. **New overload policy:** when the FM-1 measures its audio running late, the voice it fades also
  lowers the voice limit for a while (≥ 4, one voice back a second), so the load is not refilled at once.
  **SETTINGS › AUDIO LOAD** shows the peak audio load and late buffers, for glitch reports.
- **Quantize:** the free first take keeps its timing (it was always fully quantized); an overdub keeps the timing of
  the notes already on a step; a step written by hand gets no stale recorded timing. Strength 0–100 % per track
  (GROOVE › KNOB 2) was already there; the manual now explains strength, grid (DIV) and REC QUANT apart.
- **Every control says what it does (UI audit):** a new host test, `tests/fm1move_ui_audit.c`, presses every
  button and turns every knob and encoder (and, with a button held, every key) on every screen and page, on a synth
  and a drum track, and fails on a knob cell without its label or value, on a change the screen does not show, and
  on a label cut short. What it found, fixed:
  - With a button held (FX, EDIT, ARP, SEQ, SEL, GLO) the knob strip showed the names but **no values**, and the big
    readout only a unit ("HZ"): SEQ held + KNOB 1 on the SEQUENCER screen changed the note without saying which. The
    strip now shows each knob's value on every screen (NOTE C#4, DUST 12, RATE 1/16, VOLUME -3.5 DB…).
  - On CLIPS, a held button's knobs were labelled LAUNCH / COPY / CLEAR; they now show what they edit.
  - EDIT held: SHIFT and TRANSP say how far (+2 STEPS, -1 ST); the keys say what they do (ERASE C4, ERASE KICK,
    MUTED SNARE, KEYS MUTE SOUNDS).
  - SEQ + a step held: ALGORITHM / PRESETS (the parameter lock) now say which parameter and its value (LOCK PIT +4).
  - PRESETS on a screen that does not show the sound now says which (SOUND FM BASS, KIT 808).
  - SEQUENCER SHIFT no longer hides the moving pattern behind its readout.
  - Drum step held: the ratchet value is shown.
  - Three labels were cut on screen: SOUND › SYNC "LFO SYNC" / "ENV SYNC" are now LFO / ENV (the page is SYNC), the
    KEY page's "SCALE LOCK" is now LOCK. `fm1move_ui_test` now fails on any label cut short.
- **REC QUANT under a bar:** SETTINGS › REC QUANT now also offers 1/16, 1/8, 1/4 (the next beat) and 1/2 bar, besides
  1, 2, 4, 8 bars and OFF; the toast says where the take starts or stops. Stored in the settings word's spare bits
  21–25 (a 0.3 setting reads as it was; 0.3 reads the new values as 1 BAR).
- **Clear a track:** SAVE opens CLIPS on the selected track's clip playing (SAVE, REC, REC); the question says
  NO UNDO. Read-only SETTINGS rows no longer show a VALUE knob.
- **Installer:** Connect uses the updater's list of FM-1 port names (its copy missed SLOOP and Clipper).
- **Docs:** user sample slots hold 7.4 s (not 3.7); per-step filter locks, clear-track, quantize and glitch FAQ;
  beta status (community testers, no structured hardware QA).
- No change to the flash layout, project format, sounds, kits, samples, bootloader, OTA loader or USB rescue.

### Move a note on the STEP page; the battery (host simulation)
- **MOVE:** on the STEP page, ALGORITHM on a note moves the whole step one step later / earlier per click: chord,
  length and ties, accent / slide, velocity, nudge, raw timing, fill condition and parameter locks; the cursor follows,
  the toast says MOVED TO STEP n. It jumps over the next note (with its ties) or rest, which slides the other way, and
  stops at the pattern's ends. EDIT + OCT− undoes the whole move (EDIT + OCT+ redoes it), locks and nudges included.
  On an empty step ALGORITHM still moves the cursor; KNOB 1 always does.
- **Battery:** a three-bar cell in the status bar (SLOOP's reading of the PB1 divider and its thresholds: 3 FULL, 2 GOOD,
  1 LOW, 0 EMPTY, drawn in the warning colour); a BATTERY LOW toast once when it empties (again only after a charge);
  SETTINGS › BATTERY in words. On a USB host the bars fill in turn and SETTINGS says ON USB (no charge state is
  readable, so none is claimed). The simulator shows a full battery. The track name in the status bar is narrower.
- Checked in `tests/fm1move_ui_test.c` (move over a tied chord and a rest, back, the ends, undo / redo with nudge,
  condition and lock; battery levels, toast once, SETTINGS, USB, not read); not run on an FM-1. Image 573 764 B
  (+1 920), RAM unchanged.

### SCALE LOCK (host simulation)
- As Ableton's scale mode. SEL held: KNOB 2 the scale (16), white keys 9–16 / black keys the root, **KNOB 3 SCALE
  LOCK** = OFF / LOCK (SNAP: every key into the scale) / WHITE, now for every synth track at once. With it on (a scale
  other than CHR): the keys light the scale (roots lit, scale dim, the rest dark, held keys lit); NOTE on the STEP page,
  SEQ + KNOB 1 and a held step's NOTE step through the scale; MIDI step entry is put into it; the piano roll shades the
  rows out of it, marks the roots and reads the key in its title. **SEL + OCT−: FIT TO SCALE** (the selected track's
  notes rounded down into the scale; one undo). Chord mode already builds its chords from the scale. Drums untouched.
  Checked in `tests/fm1move_ui_test.c` (LEDs, keys, NOTE, roll, FIT + undo, MIDI); not run on an FM-1.

### FM1 Move 0.3 — SLOOP 2.6 inside (d7590be, host simulation)
- Upstream SLOOP 2.6 (isod89/sloop-fm1 tag `v2.6`) merged; version **FM1 MOVE 0.3**. The device page offers
  `docs/firmware/fm1move-0.3-dev.fwsc` (experimental) and `sloop-2.6.fwsc` (stable); the 0.2-dev and sloop-2.5
  packages are removed. Projects, settings and backups of 0.2 load as they are (the new parameters start off; the
  per-track parameter order is preserved; FM1 Move keeps REC QUANT in settings-word bits 21–23, 2.6's MIDI IN channel
  moves to bits 26–30).
- **27 punch-in effects:** FX held + the 11 black keys F#3…F#5: KILL LOW / MID / HIGH, REWIND, OCT UP, FLANGER,
  AUTO-PAN, FREEZE, LOOP 8T, MONO, FADE OUT (white keys 1–16 as before). PERFORM has 4 pages (FX again turns them):
  1 the white-key effects, 2 the black-key effects (tiles named by key F#, G#…; an effect held shows its page),
  3 TEXTURE (source, LEVEL, TONE, MOVE), 4 TEXTURE 2 (RATE, PLAYS, DUCK).
- **REV THROW / ECHO THROW:** FX held + OCT− (reverb) / OCT+ (echo): every send full while held; the OCT lights show it.
- **INSERT on every track** (SOFT HARD FOLD FUZZ CRUSH PHASR FLANG CHOR; A B C named by the type; dry / wet MIX).
  FM1 Move: **4 INSERT states shared by the 8 tracks** (8 × 580 B do not fit the RAM): a 5th track stays dry and the
  panel says "INSERT: 4 TRACKS MAX". A drum track's INSERT runs on its mono mix, before pan and sends.
- **SOUND pages**, synth tracks: PRESET, ENVELOPE, LFO, **SYNC** (LFO SYNC, LFO TRIG, LFO POL, ENV SYNC), SENDS,
  **INSERT** (TYPE, A, B, C), **INSERT 2** (INS MIX, FLT TYPE: ANALOG's filter LP / BP / HP, ANALOG only). Drum
  tracks: KIT, INSERT, INSERT 2 (MIX). Pages without a picture show the four values large.
- **TEXTURE** bed (VINYL, TAPE, RAIN, WIND, RADIO, ROOM or a user sample slot), saved with the project.
- **STEP page:** KNOB 3 on a note step = LEN (steps it lasts; "NOTE AHEAD" / "END OF PATTERN"), on an empty step TIME
  as before. SEQ held + a synth step held: KNOB 2 = VELO (1–127).
- **Live drum mutes:** EDIT + a black key on a drum track switches the white keys from erasing to muting lanes.
- **MIDI LEARN:** GLO held + black key C# toggles it; turn a panel knob, send a CC: learned (up to 16, kept with the
  settings). FM1 Move: a learned CC moves its parameter **on the selected track** (as the panel knobs). No panel item
  clears them yet.
- **GLO held black keys** (FM1 Move): F# fill while held, G# fill next bar, A# tap tempo, C# MIDI LEARN. (Earlier
  docs said C# / D# / F# for fill / next bar / tap: that was wrong.)
- **MIDI:** sustain pedal (CC 64), pitch bend (range BEND, device page › MIDI 2), MIDI IN on one channel (device
  page). **Program Change** loads a track's sound (the n-th of PRESETS; kit n on a drum track) and a **MIDI keyboard**
  writes the STEP page's cursor step (a chord held together, all up: the next step): FM1 Move's controller drains both
  (SLOOP does it in `ui_input.c`, which the FM1MOVE build leaves out). FM1 Move keeps track *n* on channel *n* and drum tracks on DRUMS CH
  (10); SLOOP 2.6's CH1–CH3 do not apply (not on the device page). The MIDI settings are the FM-1's, not the
  project's (a project load leaves them).
- **Engine fixes from 2.6:** look-ahead limiter, no click on a preset load over held notes, FREEZE level, the
  synth-kit kick whistle, arp latch (#191), delay TIME 1/4D.
- SLOOP-only (not in FM1 Move): SLOOP's offline web editor, song section erase, the GLO › MIDI page.

### Performance tempo (host simulation)
- **ALGORITHM turns the tempo** on PLAY, TRACKS, MIXER and PERFORM with no button held (fast turns go further); the
  BPM shows in large numerals for ~1.5 s ("TEMPO"; "FOLLOWS CLOCK IN" when CLOCK IN is USB / TRS).
- **OCT− + OCT+ pressed together, tapped in time = TAP TEMPO** (the octave goes back to 0 as before). Any button held +
  SELECT and GLO + A# still work.

### Memory (FM1MOVE build, 0.3)
- Image 569 828 / 581 564 B; RAM .data+.bss 96 496 / 98 304 B (1 808 B free); pool 333 160 / 344 064 B (10 904 B
  spare). RAM found for 2.6: INSERT in 4 shared slots; SLOOP 1.x project import converts in the idle flash buffer
  (2.6 KiB back); the MIDI note-routing table packed to nibbles (1 KiB back).

### Tests (host simulation)
- `fm1move_ui_test`: ALGORITHM tempo and its readout, OCT tap tempo, THROWs, a black-key punch-in, PERFORM pages and
  TEXTURE, SOUND pages SYNC / INSERT / INSERT 2, the 4-slot INSERT limit, MIDI LEARN (a CC moves REVERB on the selected
  track), STEP page LEN. Live renders `18-tempo`, `11c-texture`, `11d-perform-black`, `09b-sound-insert`.
- SLOOP-layout suite: 182 golden renders identical to SLOOP 2.6's; the only failure is the known baseline
  `fm1_alnk0_irq` (+54 %). Nothing here ran on an FM-1.

### REC on the cycle, as Ableton (host simulation)
- Playing, REC starts the take on the next cycle's first beat and REC during a take ends it when the cycle closes
  (never mid-bar); REC again while it waits cancels, STOP drops it; the REC light blinks while it waits. The cycle:
  SETTINGS › REC QUANT 1 BAR (default), 2, 4, 8 BARS (counted from PLAY), OFF = at once as SLOOP. Done in the audio
  interrupt on the bar, before that block's steps (seq.c rec_cycle_bar); kept with the settings (word bits 21–23).

### Metronome and count-in on the panel (host simulation)
- SETTINGS gains METRONOME (OFF / REC / ON: `G_CLOCK`, saved with the project) and COUNT-IN (OFF / 1 BAR: REC armed,
  PLAY counts a bar first; kept with the settings). FM1 Move had no panel route to either. The SETTINGS list now
  scrolls (9 rows, 6 shown).

### Simulator: HOLD mode (web)
- HOLD · OFF / ON in the simulator's header. On: a click (or F1–F12, [ ] ) on a function button latches it down; a
  second click lets it go, so one hand can hold FX, SEL or ENV while the other turns knobs or plays keys. Turning HOLD
  off releases everything. Only buttons latch; keys and knobs do not.

### Layer pages: STEPS and KEY (host simulation)
- SEQ held shows **STEPS** over any screen (SLOOP's layer page): the page's 16 steps as a 4 × 4 grid (note names,
  ties, accents, lock / nudge dots, the playhead, the held step framed; drum tracks: the sound's hits by level), the
  knobs with their values (NOTE · DIV · SWING · LENGTH; a step held: NOTE · LEVEL · RATCHET · NUDGE). Not on the
  SEQUENCER. SEL held shows **KEY**: the track chips (keys 1–8) and the chord (chord mode) or note every white key
  plays, root filled, scale raised; CHORD · SCALE · KEYS · TRANSP with values. Shown after 0.25 s held (a tap never
  flashes them) or at once when a key / knob is used.
- FM1MOVE build: image 548 248 B, RAM 93 360 / 98 304 B, pool 333 072 / 344 064 B (unchanged).
- Fix: SEL held + KNOB 2 (SCALE) took track 1's scale as its base, a drum track in FM1 Move, so it moved one step at
  most; it now follows the selected synth (or the first one).

### Clips, scenes, the STEP page (host simulation)
- Flash (1d0f6f2): **2 project slots + the autosave** (was 4 + autosave), 3 chunks of 3 824 B each (`FMP_MAX`
  11 472 B, was 2 chunks): room for the clips. Same 80 KiB region at `0xE7000`, 10 objects × A/B; map slot 1 {0,1,2},
  slot 2 {3,4,5}, autosave {8,9,7}, object 6 spare. FM1 Move 0.1's project 1 and autosave still load; its projects
  2–4 are not carried over (slot 2 reads as empty). PROJECT page, editor (objects 16–18) and device page: 2 slots; a
  restore of up to 3 chunks is staged as `fmp_save` writes.
- `firmware/src/clip.c` (d50541f): 8 tracks × 6 clips in one packed 8 KiB `.pool` bank (a 16-step pattern ~100–300
  B). Everything that writes a pattern writes the clip playing; the main loop stores it every 10 ms; a switch stores
  the outgoing clip first. Launch: playing, in the audio interrupt at the start of the next bar, the new clip on its
  step 0 whatever its LENGTH / DIV; stopped, at once; STOP before the bar starts the queued clip at once. Scenes:
  clip k on every track. Full bank: the store is refused ("CLIP MEMORY FULL"), the clip keeps its last version, the
  pattern playing still saves in its track record. A kind change empties the track's clips.
- FM1M stream **version 2**: a clip section after the tracks ('C', count, per track the clip playing and each clip's
  length + 'K', LENGTH, DIV, the pattern); version 1 projects load with each pattern as clip 1.
- Clips UI (32fb841): CLIPS (SAVE) held + white keys 1–6 launch the selected track's clips, 9–14 scenes 1–6; key
  LEDs show playing / queued (blinking) / holds / empty. CLIPS screen: 8 × 6 grid, all rows visible, cursor
  (ALGORITHM or KNOB 1), title "SCENE n"; CLIPS again launches, EDIT copies the clip playing into the cursor's slot,
  REC clears it (both ask first when a pattern would go). No focus overlay on CLIPS.
- SEQUENCER STEP page (ced992f, SLOOP's STEP page back): SEQ again on a synth track; piano roll + step cells, title
  "STEP n / LEN"; KNOB STEP / NOTE / TIME / FLAG (accent, slide: settable from the panel again); acid-style key
  entry (chords up to 4), EDIT clears a step, EDIT + OCT− undoes one entry. The drum track keeps its grid.
- Version **FM1 MOVE 0.2** (the project layout changed); the device page offers `docs/firmware/fm1move-0.2-dev.fwsc`
  (the 0.1-dev package is removed). Experimental: built and simulated on the host, never run on an FM-1.
- `tests/clip_test.c` (25 checks, + ASan / UBSan) in `tests/run_tests.sh`; `fm1move_ui_test` gained clips and STEP
  page checks; `projfmt_test` the 0.1-layout checks. Live renders `docs/ui/live/03b-sequencer-step.png`,
  `07b-clips-live.png`, `07c-clips-queued.png`.
- FM1MOVE build: image 545 160 B, RAM 93 168 / 98 304 B, pool 333 072 / 344 064 B (10 992 B spare). All verified in
  host simulation only; not run on an FM-1.

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

### Site check (all pages)
- Device page: engine, sound, user-preset load / store and the parameter labels acted on the track selected **on the
  FM-1**, which the panel can change: a change could land on another track. The page now selects its track on the
  device before each of them (checked: with the panel moved to track 6, an engine change on the page changes track 2
  only). A user preset's Load goes to the track chosen in the Store row. A tab opened while a job ran stayed empty: it
  now loads after the job. Caches are cleared when another device connects.
- Simulator and manual: PLAY / REC on Space / Enter (F11 / F12 belong to the browser).
- Checked: every page loads without errors, no broken internal link or anchor, external links exist, layout audit
  clean at 8 widths, device test and browser checks pass.

### Device page: connect fix (reported by testers)
- Connect looked only for the "Felucca" MIDI port (SLOOP / FM1 Move), so an FM-1 on the official firmware got "No FM-1
  found" with a working cable. It now probes the ports the updater probes, asks the FM-1 its identity and says what to
  do (official firmware: install from Install, no connect needed; update mode: press Install again; nothing found: the
  MIDI ports the browser sees and what to check). Install's "not found" lists the ports too. Checked in Chromium with
  fake Web MIDI (none, official firmware, update mode, other gear); not yet with a real FM-1.

### Manual
- `docs/manual/`: quick start and every feature (panel, screens, layers, tracks, sequencer, recording, Groove Engine,
  sounds, projects, settings, MIDI, the device page, install, recovery, coming from SLOOP, questions), in the app's
  theme, with the firmware's own screen renders; linked from the README, the device page and the simulator. Layout
  audited at 8 widths (`tools/web_layout_audit.cjs`).

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
