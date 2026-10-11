# FM1 Move — Control mapping per screen

Physical controls only (`HARDWARE_AUDIT.md` §5). K1–K4 = KNOB 1–4. Keys: the 16 white keys are numbered 1–16 left to
right; black keys follow the mode. "Held" = button held while touching keys/knobs (a layer).
Status: **implemented** in `firmware/src/ui_move_ctl.c` (FM1MOVE build) and exercised on the virtual FM1
(`tests/fm1move_ui_test.c`); not yet on hardware. Not yet wired: MIDI-out track kind, capture (REC + PLAY).

## Global

| Control | Function |
| --- | --- |
| SELECT | track 1–8; any function button held + SELECT: tempo (1 BPM a detent) |
| PRESETS | sound of the selected track |
| PLAY / REC | transport / record arm; REC + PLAY = capture. Playing: REC starts / ends the take on the next cycle boundary (SETTINGS › REC QUANT: 1 BAR default, 2, 4, 8 BARS, OFF = at once); REC again while it waits cancels |
| SEL held + keys 1–8 | select track; keys 9–16 and black keys: the key of the song. Held: the KEY page (the chord / note every white key plays, root filled, scale raised; CHORD · SCALE · KEYS · TRANSP with values) |
| GLO held + keys 1–8 / 9–16 | mute / solo track 1–8; black keys C#, D#, F#: fill while held, fill next bar, tap tempo |
| CLIPS (SAVE) held + white keys 1–6 | launch clip 1–6 of the selected track (playing: on the next bar; stopped: at once) |
| CLIPS (SAVE) held + white keys 9–14 | scene 1–6: clip k on every track (a track whose clip k is empty goes quiet) |
| FX held + keys 1–16 | punch-in effect while held |
| ARP held + keys | note repeat (roll) at the GROOVE grid |
| SEQ held + keys 1–16 | toggle steps of the shown page. Held: the STEPS page, a 4 × 4 grid of the page's steps (note, tie, accent, lock, playhead, held step), NOTE · DIV · SWING · LENGTH with values (not on the SEQUENCER) |
| EDIT held + keys | erase that note / lane as it plays; EDIT + OCT−/OCT+ undo / redo |
| HOME held | menu (PROJECT, SETTINGS: METRONOME OFF / REC / ON, COUNT-IN OFF / 1 BAR, clock, MIDI, lights, USB audio, version) |

## Per screen

| Screen | Keys (no layer) | ALGORITHM | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- | --- | --- |
| PLAY | play the track (notes / drum lanes) | — | macro 1 | macro 2 | macro 3 | macro 4 |
| TRACKS | play | track kind | KIND | MIDI CH | LENGTH | DIV |
| SEQUENCER (synth) | play; held step: edit it | step page | DIV | SWING | LENGTH | SHIFT |
| SEQUENCER · STEP (synth; SEQ again) | write the cursor's step (keys together: a chord); letting go moves on | cursor | STEP (cursor) | NOTE | TIME (NOTE / TIE / REST) | FLAG (– / ACC / SLIDE / A+S) |
| SEQUENCER, step held | — | — | NOTE / LEVEL | RATCHET | NUDGE | PROB / lock |
| SEQUENCER (drums) | play lanes | lane | as synth | | | |
| GROOVE | play | template | SWING | QUANTIZE strength | GROOVE amount | HUMANIZE (OCT+: micro-timing) |
| CLIPS | play | clip row (cursor) | CLIP (cursor) | LAUNCH: CLIPS | COPY: EDIT | CLEAR: REC |
| MIXER | play | — | VOLUME | PAN | REVERB send | DELAY send |
| SOUND · PRESET | play | engine | macro 1–4 | | | |
| SOUND · ENV | play | — | ATTACK | DECAY | SUSTAIN | RELEASE |
| SOUND · LFO | play | — | RATE | WAVE | DEPTH | DESTINATION |
| PERFORM | play | — | DJ FILTER | DUST | DUCK | TRACK FILTER |
| PROJECT | — | slot | — | — | — | — (buttons: SAVE save, EDIT load, REC delete; each asks when it would lose data) |
| SETTINGS | — | row | VALUE | — | — | — |

On the CLIPS screen: CLIPS again launches the cursor's clip; EDIT copies the clip playing into the cursor's slot (asks
first if that slot holds a pattern); REC clears the cursor's clip (asks) and does not arm recording there. Key LEDs while
CLIPS is held: keys 1–6 lit = playing, blinking = queued (next bar), dim = holds a pattern, off = empty; keys 9–14 lit =
that scene plays, dim = some track holds clip k.

On the STEP page (synth tracks only; the drum track keeps its grid): no button held, the keys write the cursor's step
(up to 4 notes; mono voice mode: the last key; chord mode: the chord as it sounds), not while recording or armed.
EDIT clears the cursor's step and moves on; EDIT + OCT− undoes one entry / one step edited. Knobs on CLIPS and on the
STEP page do not open the focus overlay (the edited thing stays visible).

Encoders have no push switch: nothing in FM1 Move needs a click. Confirmation is "same button again".
