# FM1 Move — Control mapping per screen

Physical controls only (`HARDWARE_AUDIT.md` §5). K1–K4 = KNOB 1–4. Keys: the 16 white keys are numbered 1–16 left to
right; black keys follow the mode. "Held" = button held while touching keys/knobs (a layer).
Status: **implemented** in `firmware/src/ui_move_ctl.c` (FM1MOVE build) and exercised on the virtual FM1
(`tests/fm1move_ui_test.c`, host simulation); not yet on hardware. Not yet wired: MIDI-out track kind, capture (REC + PLAY),
a MIDI LEARN clear, MIDI Program Change (SLOOP 2.6 applies it in `ui_input.c`, not in the FM1MOVE build).

## Global

| Control | Function |
| --- | --- |
| SELECT | track 1–8; any function button held + SELECT: tempo (1 BPM a detent) |
| ALGORITHM (PLAY, TRACKS, MIXER, PERFORM) | the tempo, no button held (fast turns go further); the BPM shows large ~1.5 s ("TEMPO"; "FOLLOWS CLOCK IN" when CLOCK IN is USB / TRS) |
| OCT− + OCT+ together | tapped in time: TAP TEMPO (the octave returns to 0 as before) |
| PRESETS | sound of the selected track |
| PLAY / REC | transport / record arm; REC + PLAY = capture. Playing: REC starts / ends the take on the next cycle boundary (SETTINGS › REC QUANT: 1/16, 1/8, 1/4, 1/2, 1 BAR default, 2, 4, 8 BARS, OFF = at once); REC again while it waits cancels |
| SEL held + keys 1–8 | select track; keys 9–16 and black keys: the key of the song. Held: the KEY page (the chord / note every white key plays, root filled, scale raised; CHORD · SCALE · SCALE LOCK · TRANSP with values). SCALE LOCK (KNOB 3: OFF / LOCK / WHITE) applies to every synth track: the keys light the scale (root lit, scale dim, others dark), note knobs and MIDI step entry stay in it. SEL + OCT−: FIT TO SCALE (the selected track's notes into the scale; undo EDIT + OCT−) |
| GLO held + keys 1–8 / 9–16 | mute / solo track 1–8; black keys F#3 fill while held, G#3 fill next bar, A#3 tap tempo, C#4 MIDI LEARN on / off (turn a panel knob, send a CC; up to 16; a learned CC moves its parameter on the selected track) |
| CLIPS (SAVE) held + white keys 1–6 | launch clip 1–6 of the selected track (playing: on the next bar; stopped: at once) |
| CLIPS (SAVE) held + white keys 9–14 | scene 1–6: clip k on every track (a track whose clip k is empty goes quiet) |
| FX held + keys 1–16 | punch-in effect while held (16 on the white keys) |
| FX held + black keys F#3 … F#5 | the 11 SLOOP 2.6 effects: KILL LOW, KILL MID, KILL HIGH, REWIND, OCT UP, FLANGER, AUTO-PAN, FREEZE, LOOP 8T, MONO, FADE OUT |
| FX held + OCT− / OCT+ | REV THROW / ECHO THROW: every send full while held (the OCT LEDs show them) |
| ARP held + keys | note repeat (roll) at the GROOVE grid |
| SEQ held + keys 1–16 | toggle steps of the shown page. Held: the STEPS page, a 4 × 4 grid of the page's steps (note, tie, accent, lock, playhead, held step), NOTE · DIV · SWING · LENGTH with values (not on the SEQUENCER) |
| EDIT held + keys | erase that note / lane as it plays; EDIT + OCT−/OCT+ undo / redo; EDIT + a black key on a drum track: the white keys mute / unmute lanes instead (again: erase) |
| HOME held | menu (PROJECT, SETTINGS: METRONOME OFF / REC / ON, COUNT-IN OFF / 1 BAR, clock, MIDI, lights, USB audio, version) |

## Per screen

| Screen | Keys (no layer) | ALGORITHM | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- | --- | --- |
| PLAY | play the track (notes / drum lanes) | tempo | macro 1 | macro 2 | macro 3 | macro 4 |
| TRACKS | play | tempo | TYPE | TRANSP | LENGTH | DIV |
| SEQUENCER (synth) | play; held step: edit it | step page | DIV | SWING | LENGTH | SHIFT |
| SEQUENCER · STEP (synth; SEQ again) | write the cursor's step (keys together: a chord); letting go moves on | cursor | STEP (cursor) | NOTE | TIME (NOTE / TIE / REST); on a note: LEN (steps it lasts) | FLAG (– / ACC / SLIDE / A+S) |
| SEQUENCER, step held (SEQ held) | — | — | NOTE (drums: SOUND) | LEVEL (synth: VELO 1–127) | RATCHET | NUDGE |
| SEQUENCER (drums) | play lanes | lane | as synth | | | |
| GROOVE | play | template | SWING | QUANTIZE strength | GROOVE amount | HUMANIZE (OCT+: micro-timing) |
| CLIPS | play | clip row (cursor) | CLIP (cursor) | LAUNCH: CLIPS | COPY: EDIT | CLEAR: REC |
| MIXER | play | tempo | VOLUME | PAN | REVERB send | DELAY send |
| SOUND · PRESET | play | engine | macro 1–4 | | | |
| SOUND · ENVELOPE | play | engine | ATTACK | DECAY | SUSTAIN | RELEASE |
| SOUND · LFO | play | engine | RATE | WAVE | to PITCH | to FILTER |
| SOUND · SYNC | play | engine | LFO SYNC | LFO TRIG | LFO POL | ENV SYNC |
| SOUND · SENDS | play | engine | DRIVE | CHORUS | DELAY | REVERB |
| SOUND · INSERT | play | engine | INSERT (type) | A | B | C (named by the type) |
| SOUND · INSERT 2 | play | engine | INS MIX | FLT TYPE (LP / BP / HP, ANALOG only) | — | — |
| SOUND (drum track) | play | — | KIT page: KIT, LEVEL, PAN, FILTER · INSERT page: as synth · INSERT 2: INS MIX | | | |
| PERFORM 1 · HOLD FX + KEY | play | tempo | DJ FILTER | DUST | DUCK | TRACK FILTER |
| PERFORM 2 · BLACK KEYS | play | tempo | DJ FILTER | DUST | DUCK | TRACK FILTER |
| PERFORM 3 · TEXTURE | play | tempo | TEXTURE (source) | LEVEL | TONE | MOVE |
| PERFORM 4 · TEXTURE 2 | play | tempo | RATE | PLAYS | DUCK | — |
| PROJECT | — | slot | — | — | — | — (buttons: SAVE save, EDIT load, REC delete; each asks when it would lose data) |
| SETTINGS | — | row | VALUE | — | — | — |

On the CLIPS screen: CLIPS again launches the cursor's clip; EDIT copies the clip playing into the cursor's slot (asks
first if that slot holds a pattern); REC clears the cursor's clip (asks) and does not arm recording there. Key LEDs while
CLIPS is held: keys 1–6 lit = playing, blinking = queued (next bar), dim = holds a pattern, off = empty; keys 9–14 lit =
that scene plays, dim = some track holds clip k.

On the STEP page (synth tracks only; the drum track keeps its grid): no button held, the keys write the cursor's step
(up to 4 notes; mono voice mode: the last key; chord mode: the chord as it sounds), not while recording or armed.
EDIT clears the cursor's step and moves on; EDIT + OCT− undoes one entry / one step edited / one move. ALGORITHM on a
note MOVES it (the whole step: ties, flags, velocity, nudge, condition, locks) one step per click over the next note or
rest, the cursor following; on an empty step it moves the cursor. Knobs on CLIPS and on the
STEP page do not open the focus overlay (the edited thing stays visible).

SOUND pages (EDIT again turns them; ENV and LFO jump to theirs): synth tracks PRESET → ENVELOPE → LFO → SYNC → SENDS →
INSERT → INSERT 2; drum tracks KIT → INSERT → INSERT 2. Pages without a picture show the four values large. INSERT: up
to 4 tracks at once (4 shared states); a 5th stays dry and the panel says "INSERT: 4 TRACKS MAX". PERFORM pages (FX
tapped again): 1 the white-key effects, 2 the black-key effects (tiles named F#, G#…; an effect held shows its page),
3 TEXTURE, 4 TEXTURE 2.

Encoders have no push switch: nothing in FM1 Move needs a click. Confirmation is "same button again".
