# FM1 Move — Navigation map

The FM-1 has 12 function buttons in two rows, OCT− / OCT+, 7 push-less encoders and 27 keys (`HARDWARE_AUDIT.md` §5).
FM1 Move gives the two rows one meaning each, so the panel is learnable at a glance:

```
 TOP ROW    = SOUND & MIX      FX        SEL       ENV       LFO       EDIT      GLO
                               PERFORM   TRACKS    SOUND·ENV SOUND·LFO SOUND     MIXER
 BOTTOM ROW = TIME & ARRANGE   HOME      SAVE      ARP       SEQ       PLAY      REC
                               PLAY      CLIPS     GROOVE    SEQUENCER transport record
```

The printed labels stay meaningful where they can (SEL selects tracks, SEQ is the sequencer, GLO the global mix,
EDIT/ENV/LFO the sound, PLAY/REC the transport). GROOVE sits on ARP: in the time row, between CLIPS and SEQUENCER,
next to the transport — where the hand already is while a pattern plays. The screen title and the lit button LED
always say where you are.

## Screen map

```
                         ┌──────────── HOME (tap) ────────────┐
                         ▼                                     │
   SEL ─▶ TRACKS     ◀─▶ PLAY ◀─▶  SEQ ─▶ SEQUENCER            │  any screen button
   GLO ─▶ MIXER                    ARP ─▶ GROOVE               │  goes straight to its
   EDIT ─▶ SOUND (PRESET · ENV · LFO · SENDS)                  │  screen: depth 1
   ENV ─▶ SOUND·ENV    LFO ─▶ SOUND·LFO                        │
   FX ─▶ PERFORM       SAVE ─▶ CLIPS                           │
   HOME held ─▶ MENU: PROJECT · SETTINGS ─────────────────────┘
```

- Every screen is **one press** away; pressing the button of the current screen again goes to its next page
  (SOUND: PRESET → ENV → LFO → SENDS; SETTINGS: MIDI → SYNC → SYSTEM).
- PROJECT and SETTINGS (rare, non-musical) live behind **HOME held** (≥ 0.5 s), as SLOOP's menu does.
- No screen is deeper than two levels (screen → page). There are no nested menus for musical functions.

## Always available (every screen)

| Control | Action |
| --- | --- |
| SELECT | select track 1–8 (one detent each, no wrap); with any function button held: the tempo, one BPM a detent (as SLOOP) |
| PRESETS | browse the selected track's sounds (preset, kit, or MIDI program) |
| ALGORITHM | the screen's navigator: step page, clip row, list row, envelope stage, groove template |
| KNOB 1–4 | the four parameters in the knob strip |
| PLAY | start / stop |
| REC | arm / disarm recording (count-in per settings) |
| SEL held + white key 1–8 | select that track directly (fastest track change) |
| GLO held + key 1–8 / 9–16 | mute / solo tracks 1–8 |
| EDIT + OCT− / OCT+ | undo / redo |
| OCT− / OCT+ | octave (synth tracks); ghost / hard hits held (drum tracks) |

## Performance paths (target: ≤ 2 actions, no menu)

| Task | Gesture |
| --- | --- |
| select a track | SELECT one detent, or SEL + key |
| mute / solo | GLO held + key |
| change swing | ARP (GROOVE) → KNOB 1 |
| change pattern / clip | SAVE held + key 1–8 (launch scene, quantised — M3, not yet implemented) |
| record | REC (armed) → PLAY |
| capture what you just played | REC + PLAY together (M3, not yet implemented) |
| duplicate a clip | CLIPS: EDIT + key of the target slot (M3, not yet implemented) |
| apply an effect | FX held + key |
| change the sound | PRESETS, or KNOB 1–4 on PLAY (macros) |

## Destructive actions

Clear track, clear pattern, delete project, overwrite project, groove commit: a modal states the consequence;
**the same button again confirms**, any other button cancels, nothing happens after 4 s.
