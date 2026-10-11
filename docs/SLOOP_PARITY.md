# FM1 Move and SLOOP 2.5: what was kept, what changed, what is gone

FM1 Move is built on SLOOP 2.5. The sound and sequencer code is shared, not rewritten: **every engine and every
sequencer feature of SLOOP still runs in FM1 Move**. What changed is the panel (a new interface), the number of
tracks (eight, any kind), the projects (a new format) and the song arranger (removed, replaced by clips and
scenes). This page lists it feature by feature, from the code (`firmware/src/felucca.c` build switches,
`ui_move_ctl.c`, `ui_layers.c`, `seq.c`, `editor.c`), not from memory.

Status words: **kept** = works as in SLOOP; **changed** = works, differently; **web** = works, but only from the
device page (`docs/device/`), not from the FM-1's panel yet; **gone** = not in FM1 Move. Everything here is verified
in host simulation only: no FM1 Move build has run on an FM-1 yet.

## Sequencer — all kept

| SLOOP feature | FM1 Move | How on FM1 Move |
| --- | --- | --- |
| Step entry without recording (stopped or playing) | kept | SEQ held + white keys 1–16 write / clear steps: synth tracks the last note or chord played (or SEQ held + KNOB 1 picks the note), drum tracks the last sound played (or KNOB 1 picks the lane); a step key held: KNOB 1 note, 2 level, 3 ratchet, 4 nudge |
| 64 steps per track, own length and division (polymeter, 1/32 … 2 bars) | kept (8 tracks) | SEQUENCER: KNOB 1 DIV, KNOB 3 LENGTH; SEQ held: KNOB 2–4 |
| STEP page: a cursor over the pattern, acid-style entry (keys together = the cursor's step, letting go moves on) | kept | SEQ again on the SEQUENCER screen of a synth track: piano roll + step cells; KNOB 1 STEP (or ALGORITHM), 2 NOTE, 3 TIME (NOTE / TIE / REST), 4 FLAG; EDIT clears the step; not while recording or armed. The drum track keeps its grid only |
| Chords up to 4 notes a step, ties | kept | recorded from the keys (several keys or chord mode: chords; a note held over steps: ties) |
| Accent and slide flags on a step | kept | STEP page: KNOB 4 FLAG (– / ACC / SLIDE / A+S), as on SLOOP's STEP page (the device page does not edit them yet) |
| Step level (ghost/soft/norm/hard) and ratchet ×1–×4 | kept | SEQ held + step held: KNOB 2 LEVEL, KNOB 3 RATCHET; OCT− / OCT+ held while playing drums |
| MPC swing 0–100 | kept | SEQUENCER KNOB 2 or GROOVE KNOB 1 |
| Parameter locks (24 per track) | kept | SEQ held + step held: PRESETS value, ALGORITHM parameter |
| Micro timing (nudge, 1/64 step) | kept | SEQ held + step held: KNOB 4 |
| Fill conditions (fill only / no fill), fill held, fill next bar | kept | SEQ held + step held: OCT+; GLO held + black keys C# (fill held), D# (next bar) — SLOOP used white keys 9/10 |
| Live recording, overdub, latency compensation (~12 ms taken back) | kept | REC |
| Note repeat (roll) at 1/8 … 1/64 | kept | ARP held + keys; KNOB 1 rate |
| Erase a note / lane as it plays | kept | EDIT held + keys |
| Shift, length ×2 / ½, transpose of a pattern | kept | EDIT held: KNOB 1–3 (SEQUENCER KNOB 4 shifts too) |
| Undo / redo | kept | EDIT + OCT− / OCT+ |
| Drum grid with the keys (keys = steps of one lane) | kept | SEQ held + keys on a drum track (lane = the sound played last, or KNOB 1) |
| Step pages beyond 16 | kept | SEQUENCER: ALGORITHM; SEQ held: OCT− / OCT+ or black keys |
| Layer pages: SEQ held shows the steps (4 × 4), SEL held the chords on the keys | kept: STEPS and KEY pages in FM1 Move's style, over any screen (shown after 0.25 s held, or at once when a key or knob is used) |
| Tap tempo | changed | GLO held + black key F# (SLOOP: the last white key) |
| Set the tempo exactly | changed | any function button held + SELECT, one BPM a detent (as SLOOP's "inside a layer: tempo"); the web: GLOBAL › BPM |
| REC screen: mode free / tempo, length 1-2-4 bars, start on note / count-in | count-in **kept** (SETTINGS › COUNT-IN); mode and length **gone** from the panel | the REC button arms and records; mode and length keep their stored value but no FM1 Move page sets them yet |
| Metronome (GLO › GLOBAL › CLICK: OFF / REC / ON) | kept: SETTINGS › METRONOME; also the device page (Global › CLICK) | |
| Hold REC ~2 s to clear the track | **gone** | EDIT held + keys while stopped erases from the whole pattern; changing a track's kind clears it |
| *New in FM1 Move* | — | Groove Engine: quantise strength, templates, humanize, per-track groove, commit with undo (GROOVE screen) |

## Tracks and sound — engines kept, panel reaches part, the web reaches all

| SLOOP feature | FM1 Move |
| --- | --- |
| 3 synth parts + 1 drum track | **changed:** 8 tracks, each synth or drums (MIDI-out kind: next milestone) |
| 12 engines, 153 factory sounds, browsing by kind | kept (PRESETS knob; SOUND: ALGORITHM picks the engine) |
| Engine pages EDIT 1 / EDIT 2 (8 engine parameters) | panel: the engine's 4 macros (PLAY / SOUND KNOB 1–4); **web:** all 8 |
| ENV (A D S R) | kept (ENV button: SOUND·ENV) |
| ENV DEST (filter, pitch punch, shape) | **web** |
| LFO rate, wave + depth to pitch, filter | kept (LFO button: SOUND·LFO) |
| LFO phase, fade, depth to shape / amp | **web** |
| Drive, chorus, delay, reverb sends | kept (SOUND page 4, MIXER for delay / reverb) |
| Track filter (LP ← → HP) | kept (PERFORM KNOB 4, FX held + KNOB 4) |
| SLICER (gate / stutter, pattern, rate, depth) | **web** |
| Delay time, feedback, colour, mix; reverb size, damping; chorus rate, depth (global) | **web** |
| Arpeggiator (mode, rate, octaves, gate, swing, probability, hold, order) | **web** (the engine plays it; SLOOP's ARP button is GROOVE in FM1 Move, ARP held is still note repeat) |
| Voice mode POLY / MONO / LEGATO / UNISON, glide, glide mode, priority, allocation, detune | **web** |
| Key and scale for all synths, scale snap, chord mode | kept: SEL held + KNOB 1 CHORD, 2 SCALE, 3 KEYS, 4 TRANSPOSE; the key: SEL held + white keys 9–16 or black keys (keys 1–8 select tracks now) |
| CHORD+ (black keys change the chord), STRUM, VOICE LEAD | CHORD+ kept (engine side); STRUM and VOICE LEAD **web** |
| Master DUST, DUCK, DJ filter, limiter | kept (PERFORM screen, FX held + KNOB 1–3) |
| 16 punch-in effects | kept (FX held + keys) |
| 37 drum kits, ghost / hard hits, black keys double the white | kept (PRESETS on a drum track, or the web) |
| Drum level / reverb / delay sends | **changed:** per drum track (MIXER), not global (several drum tracks) |
| SYN1–SYN4 synthesised user kits | kept (play); their editor is SLOOP's editor only (not on the FM1 Move device page yet) |
| User sample slots USR1–USR4, KIT USR3+4 | **changed:** USR1–USR3 (USR4's flash holds FM1M projects); USR4 and USR3+4 still appear in the kit list but play nothing |
| FM6 + DX7 patches, bank B1–B27, cartridge import | kept (bank managed on the device page) |
| User presets (32) | kept; storing / loading from the panel is **web** for now (SLOOP's USER page is gone; PRESETS still browses them) |

## Songs and projects

| SLOOP feature | FM1 Move |
| --- | --- |
| Sections A–D, quick chain, song mode, SONG REC, the song page (`FELUCCA_ARRANGER`) | **gone** — replaced by clips and scenes: 6 clips per track, 6 scenes, launched on the next bar (CLIPS held + white keys 1–6 / 9–14, CLIPS screen). No song chain and no capture yet |
| 4 project slots (SLOOP format FUN5) | **changed:** 2 slots in the FM1M format (eight tracks, groove, clips) on HOME held › PROJECT, and on the device page |
| Autosave and power-on resume | kept (FM1M, an internal third slot) |
| SLOOP's working project at first start | imported (tracks 1–4) |
| SLOOP's 4 saved project slots | not imported yet (still in flash, untouched) |
| TOOLS: clear sequence, init sound, new project | **gone** from the panel |

## Interface and settings

| SLOOP feature | FM1 Move |
| --- | --- |
| SLOOP's pages, graphs, tiles, splash | **changed:** the FM1 Move design system, 10 screens (`docs/UI_NAVIGATION.md`) |
| Visualiser (12 styles), 5 colour palettes, ZOOM | **gone** |
| Settings: clock in, MIDI out (keys / seq), MIDI in (notes / clock), lights, key lights, USB audio level, version | kept (HOME held › SETTINGS) |
| Settings: speaker low cut, USB serial | **gone** from the panel (the stored values still apply) |
| Settings: NOTES (playing notes light their keys), colour, zoom | **gone** (FM1 Move's lights and screens do not use them) |
| Hardware calibration (panel table) | **gone** from the panel — the stored table still applies; a unit whose keys answer wrongly needs SLOOP to recalibrate |
| PLAY light on the beat, REC light, layers' key lights | kept |

## MIDI, USB, update

| SLOOP feature | FM1 Move |
| --- | --- |
| USB MIDI in / out, TRS MIDI in, MIDI clock in, CCs | kept |
| MIDI channels | **changed:** channel *n* plays synth track *n* (1–8); the drum channel (10 by default; device page › DRUMS › CH) plays drum track 1; any other channel plays the selected track |
| Sequencer to MIDI out | kept (SETTINGS › MIDI OUT = SEQ) |
| USB audio input (44.1 / 48 kHz) | kept (platform unchanged) |
| Installer, update loader, CRC checks, USB rescue (OCT− at power-on), boot-loop guard | kept, unchanged |
| SLOOP's web editor (piano roll, MIDI files, CHOP, drum synth, song) | **changed:** SLOOP's editor speaks four tracks; FM1 Move has its own device page: install, every track parameter, user presets, sample slots and drum kits, FM6 bank, projects, backup. Not there yet: step / piano-roll editing, MIDI files, CHOP, the drum synth |

## Where the gaps go

The "web" rows are reachable today from the device page. Bringing the most used ones onto the panel (arpeggiator,
voice mode, REC options, user preset store) and capture are the next steps (`docs/DEVELOPMENT_PLAN.md`).
