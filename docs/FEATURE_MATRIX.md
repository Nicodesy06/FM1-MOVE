# FM1 Move — Feature matrix

Status values: **SLOOP** = present in the SLOOP 2.5 baseline (verified in code and host tests);
**FM1M** = implemented and host-tested in FM1 Move (with the commit); **PART** = partial; **PLAN** = planned,
not implemented; **N/A** = not possible on this hardware. Nothing is "done on hardware" yet: no FM1 Move build has
run on a device.

Reference products: Groove OS (8 tracks, groove features), Ableton Move (workflow), Felucca/SLOOP (base).

## Tracks and engines

| Feature | Status | Notes |
| --- | --- | --- |
| 8 independent tracks | PLAN (M1) | SLOOP: 3 synth + 1 drum |
| Track kind configurable (synth / drums / MIDI out) | PLAN (M1) | SLOOP: fixed roles |
| Per-track pattern, length 1–64, division | SLOOP | `P_SLEN`, `P_SDIV` |
| Per-track volume, pan, mute, solo | SLOOP | four tracks |
| Per-track MIDI channel | PART | SLOOP: fixed map (1–3 parts, DRUMS channel) |
| Global voice allocation, priority, stealing, shedding | SLOOP | 8-voice budget across parts, bass/lead protected |
| 12 synth engines, 153 presets | SLOOP | FM6 with DX7 import, PHYS, NOISE… |
| Drum kits (16 sounds, levels, ratchets, choke hat) | SLOOP | one drum track |
| Sample playback, user samples (USR1–4), CHOP in editor | SLOOP | no audio input on the device |
| MIDI OUT track (USB) | PART | SLOOP sends the sequencer to USB MIDI globally (`G_MIDI`) |

## Timing, groove

| Feature | Status | Notes |
| --- | --- | --- |
| Sample-clock transport, no drift | SLOOP | units of 1 sample at 1 BPM; events at 32-sample (0.73 ms) blocks |
| Swing global + per track, 50–75 % | SLOOP | reads 0–100; straight grids only |
| Micro timing per step (±½ step, 1/64) | SLOOP | per step, not per note / lane |
| Non-destructive quantise with strength 0–100 % | PLAN (M2) | SLOOP records quantised (destructive) |
| Quantise grids 1/4 … 1/32, triplets | PART | division grids exist; quantise-on-record only |
| Groove templates (offset + velocity + accent) | PLAN (M2) | |
| Groove amount, per-track override | PLAN (M2) | |
| Humanise timing / velocity with deterministic seed | PLAN (M2) | |
| Groove commit with undo | PLAN (M2) | |

## Sequencer

| Feature | Status | Notes |
| --- | --- | --- |
| 64 steps, chords (4 notes), tie, slide, accent | SLOOP | |
| Velocity levels (ghost/soft/norm/hard), ratchet ×1–4 | SLOOP | |
| Parameter locks (24 per track) | SLOOP | |
| Fill conditions | SLOOP | FILL / NOT FILL |
| Probability, A:B conditions | PLAN | memory: 1 byte/step |
| Flam | PLAN | |
| Gate length per step | PLAN | track gate exists (`P_SGATE`) |
| Live record, overdub, step record, count-in, metronome | SLOOP | |
| Undo / redo (one level, one track) | SLOOP | |
| Copy / paste / shift / double / halve | PART | shift, length ×2/½ exist |
| Polyrhythm (per-track length) | SLOOP | |

## Clips, scenes, capture

| Feature | Status | Notes |
| --- | --- | --- |
| Song sections A–D, quick chain, song mode | SLOOP | four sections |
| Clips per track (several patterns per track) | PLAN (M3) | memory-bound: see MEMORY_BUDGET |
| Scenes (one clip per track) and quantised launch | PART | sections ≈ scenes |
| Retroactive capture | PLAN (M3) | ring of recent key events |

## Mixer, FX, performance

| Feature | Status | Notes |
| --- | --- | --- |
| Sends: chorus, tempo delay, reverb | SLOOP | shared buses |
| Per-track drive, slicer, filter | SLOOP | |
| Master DJ filter, DUST, sequenced kick ducking, limiter | SLOOP | |
| 16 punch-in FX (loops, stutter, tape stop…) | SLOOP | |
| Scale / chord mode, CHORD+, strum, voice leading, arpeggiator, note repeat | SLOOP | |

## System

| Feature | Status | Notes |
| --- | --- | --- |
| USB MIDI in/out, TRS MIDI in, MIDI clock in | SLOOP | no TRS MIDI out (N/A without hardware) |
| MIDI clock **out** | PLAN | |
| Project save/load, CRC, A/B copies, autosave, migration of old formats | SLOOP | 4 tracks |
| 8-track project format | PLAN (M1) | `PROJECT_FORMAT.md` |
| USB update, rescue, return to official | SLOOP | kept unchanged |
| Dual-core | PLAN (M6) research | unknown if possible |

## UI

| Feature | Status | Notes |
| --- | --- | --- |
| FM1 Move design system (tokens, fonts, components) | see `UI_DESIGN_SYSTEM.md` | |
| Screens PLAY, TRACKS, SEQUENCER, GROOVE, CLIPS, MIXER, SOUND, PERFORM, PROJECT, SETTINGS | see `UI_DESIGN_SYSTEM.md` §status | |
| Dirty-band partial redraw | PLAN | SLOOP: lazy per-element redraw |
