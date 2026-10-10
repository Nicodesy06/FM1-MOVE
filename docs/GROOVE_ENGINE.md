# FM1 Move — Groove Engine

Implementation: `firmware/src/groove.c` (pure functions), used by `firmware/src/seq.c` (`micro_units`, `seq_vel`,
`drum_vel`, `drum_step`, `seq_ratchets`, `rec_dev64`, `groove_commit`). Tests: `tests/groove_test.c` (26 checks, also
under ASan + UBSan). Status: **engine implemented and host-tested**; the GROOVE screen is designed
(`UI_DESIGN_SYSTEM.md`) and will drive these settings once the new UI is wired (task in `DEVELOPMENT_PLAN.md`);
the 8-track project format will store them. Not yet run on hardware.

## 1. Time base

The transport clock counts **units**: one unit = one sample at 1 BPM, so a beat is `BEAT_U = 44100 × 60` units at
any tempo and every division (1/4 … 1/32, triplets, 1/2, bars) is an integer number of units. The clock advances
`CTL × BPM` units per 32-sample control block; nothing is rounded, so **no cumulative drift** is possible (SLOOP's
design, kept). Events fire at the start of a control block: resolution 32 samples = **0.73 ms**, deterministic.

## 2. Data

| Where | What | Size |
| --- | --- | --- |
| `track_t.roff[64]` | raw timing: each step's recorded deviation from its grid, 1/64 step (−32…31) | 64 B |
| `track_t.micro[64]` | manual micro-timing (SLOOP's nudge), 1/64 step | (existing) |
| `track_t.pad_off[16]` | drum pad offsets, 1/64 step, early or late | 16 B |
| `track_t.gv` (`groove_t`) | quantize looseness, template, timing amount, velocity amount, humanize timing / velocity, own-or-follow, seed, the USER template | 40 B |
| `song_groove` | the global groove: template and amounts every track follows unless it owns one | 40 B |

All-zero settings mean SLOOP's behaviour (strength 100 %, STRAIGHT, no humanize, follow the song): zeroed memory
and old projects need no setup, and **the 182 golden renders of SLOOP are unchanged**.

## 3. The pipeline (per event)

```
grid position            transport × DIV, swung (SLOOP's grid_swing) — swing applied here, once
 1 raw                   roff[step]                                     recorded, never altered (except COMMIT)
 2 quantize              raw × (1 − strength)                           = raw + s · (grid − raw), grid = 0
 3 template              + timing amount × template.off[slot]           residual offsets: no swing inside
 4 micro-timing          + micro[step] (+ pad_off[lane] on drums)
 5 humanize              + hum_t × hash(seed, track, step, lane)        deterministic, ±1/8 step at 100 %
 6 schedule              clamp to [−32/64, +31/64] step → units → the control block it falls in
velocity                 written × (1 + vamt × (template.vel[slot] − 1)) + hum_v × hash(…), 1…127
```

- **Quantize** is non-destructive: the recorded deviation stays in `roff`; strength is a playback parameter, can
  change at any time (also while playing), and 100 % → exact grid. The grid is the track's step resolution (its
  DIV: 1/4, 1/8, 1/16, 1/32 and the triplet divisions), chosen when recording. Live notes are heard **immediately**
  (no latency added); the deviation is measured as heard (`REC_LAT` compensation, SLOOP's).
- **Swing vs templates.** Swing shapes the grid once (SLOOP's tested mechanism, 0–100 = 50–75 %, straight grids only).
  A template stores only *residual* timing beyond swing, plus a velocity/accent map, and a **suggested swing** that the
  UI applies when the template is chosen. Swing and groove therefore never displace an event twice.
- **Slot** = the event's sixteenth *within its pattern* (not the bar): a polymetric pattern keeps its feel on every
  pass and a commit bakes exactly what plays.
- **Humanize** depends only on (seed, track, step, lane): the same every loop and after save/load; **REGEN** = a new
  seed.
- **Early events**: a negative total makes the step fire in the previous grid step, across the pattern wrap too
  (SLOOP's `seq_tick` look-ahead of one step, kept). Steps always fire in order, never twice, never skipped.
- **Drum pads**: a drum step fires at its earliest pad; later pads (and their ratchets) fire at their own offset.

## 4. Templates

| # | Name | Suggested swing | Timing residuals (1/64 step) | Velocity map |
| --- | --- | --- | --- | --- |
| 0 | STRAIGHT | 0 (50 %) | none | flat |
| 1 | MPC 16 | 32 (58 %) | none | strong downbeats, soft swung 16ths |
| 2 | HOUSE | 24 (56 %) | off-beat 8ths −2 | off-beat 8ths 112 %, 16ths ~63 % |
| 3 | UK GARAGE | 64 (66 %) | snare (2, 4) +3, last 16th −3 | shuffled 16ths emphasised |
| 4 | BROKEN | 12 (53 %) | +4, −3, +5, −2 at 3, 6, 11, 13 | irregular |
| 5 | ROLLING | 0 | off-beats +2 (laid back) | rolling 16th dynamics |
| 6 | LATIN | 8 (52 %) | tresillo pickups −2 | 3-2 son clave accents |
| 7 | USER | 0 | per project | per project |

## 5. Groove commit

`groove_commit(t)`: each step's total timing (stages 1–5) becomes its `micro` nudge (rounded to 1/64 step), `roff`
→ 0, timing amount and humanize timing → 0; a track that followed the song takes the song's template as its own so
the **velocity feel stays**. One undo level (`groove_uncommit`). Verified: every hit replays within ±2 blocks (the
1/64 rounding); undo restores settings and raw timing.

## 6. Verified (host, `tests/groove_test.c`)

| Check | Result |
| --- | --- |
| zero settings = SLOOP nudge exactly; unit conversion exact for pure nudges | pass |
| quantize formula for every raw −32…31 and strength 0…100 | pass |
| a recorded late snare at strength 100 / 0 / 50 % fires 173 / 216 / 194 blocks after the kick (expected 172.3 / 215.3 / 193.8) | pass |
| a live hit keeps its raw deviation (7/64 for ~8/64 played), plays on the grid at 100 % | pass |
| swing 58 % alone vs swing + MPC 16: identical timing (200 blocks), template accents applied | pass |
| early step 1 fires before each bar line, once per bar, across the wrap | pass |
| 64 bars with 100 % humanize: same deviation every bar (no drift) | pass |
| pad +12/64 → 33 blocks late; pad −8/64 → 22 blocks early | pass |
| commit replays within rounding; undo restores | pass |
| velocities bounded 1…127; template accents | pass |

## 7. Limits (documented)

- Event time resolution: one control block (0.73 ms). Sample-offset scheduling inside a block is possible later; it
  would touch every engine's note-on.
- Offsets are bounded to just under half a step either way (the sequencer's window). Quantizing to a grid *coarser*
  than the recorded step resolution is a destructive edit (planned as a commit-style tool), not a playback parameter.
- Raw deviation and humanize are per step (a chord shares its step's deviation); drum pads add per-lane offsets.
- Velocity groove on drum steps acts on the played velocity (drum steps store 4 levels).
