# FM1 Move — Control mapping per screen

Physical controls only (`HARDWARE_AUDIT.md` §5). K1–K4 = KNOB 1–4. Keys: the 16 white keys are numbered 1–16 left to
right; black keys follow the mode. "Held" = button held while touching keys/knobs (a layer).
Status: **implemented** in `firmware/src/ui_move_ctl.c` (FM1MOVE build) and exercised on the virtual FM1
(`tests/fm1move_ui_test.c`); not yet on hardware. Not yet wired: SAVE + keys scenes and CLIPS (M3), MIDI-out track
kind, capture (REC + PLAY).

## Global

| Control | Function |
| --- | --- |
| SELECT | track 1–8 |
| PRESETS | sound of the selected track |
| PLAY / REC | transport / record arm; REC + PLAY = capture |
| SEL held + keys 1–8 | select track |
| GLO held + keys 1–8 / 9–16 | mute / solo track 1–8 |
| SAVE held + keys 1–8 | launch scene 1–8 (quantised) |
| FX held + keys 1–16 | punch-in effect while held |
| ARP held + keys | note repeat (roll) at the GROOVE grid |
| SEQ held + keys 1–16 | toggle steps of the shown page |
| EDIT held + keys | erase that note / lane as it plays; EDIT + OCT−/OCT+ undo / redo |
| HOME held | menu (PROJECT, SETTINGS) |

## Per screen

| Screen | Keys (no layer) | ALGORITHM | K1 | K2 | K3 | K4 |
| --- | --- | --- | --- | --- | --- | --- |
| PLAY | play the track (notes / drum lanes) | — | macro 1 | macro 2 | macro 3 | macro 4 |
| TRACKS | play | track kind | KIND | MIDI CH | LENGTH | DIV |
| SEQUENCER (synth) | play; held step: edit it | step page | DIV | SWING | LENGTH | SHIFT |
| SEQUENCER, step held | — | — | NOTE / LEVEL | RATCHET | NUDGE | PROB / lock |
| SEQUENCER (drums) | play lanes | lane | as synth | | | |
| GROOVE | play | template | SWING | QUANTIZE strength | GROOVE amount | HUMANIZE (OCT+: micro-timing) |
| CLIPS | play | clip row | SCENE | LAUNCH quantise | CLIP LENGTH | FOLLOW |
| MIXER | play | — | VOLUME | PAN | REVERB send | DELAY send |
| SOUND · PRESET | play | engine | macro 1–4 | | | |
| SOUND · ENV | play | — | ATTACK | DECAY | SUSTAIN | RELEASE |
| SOUND · LFO | play | — | RATE | WAVE | DEPTH | DESTINATION |
| PERFORM | play | — | DJ FILTER | DUST | DUCK | TRACK FILTER |
| PROJECT | — | slot | — | — | — | — (buttons: SAVE save, EDIT load, REC delete; each asks when it would lose data) |
| SETTINGS | — | row | VALUE | — | — | — |

Encoders have no push switch: nothing in FM1 Move needs a click. Confirmation is "same button again".
