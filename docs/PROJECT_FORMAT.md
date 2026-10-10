# FM1 Move — Project format "FM1M" v1

Implementation: `firmware/src/projfmt.c` + `storage.c` (`st_save_staged`, the FM1M objects). Tests:
`tests/projfmt_test.c` (also under ASan + UBSan). Status: **format, storage and tests done on the host**; wiring
into the FM1 Move firmware build (save/load/autosave UI, legacy import) is part of the firmware integration step.

## 1. Goals and constraints

- Eight tracks, every track's kind, sound, pattern, locks, micro-timing, raw timing, conditions, groove and pad
  offsets; globals and the song groove.
- **No project-sized RAM buffer** (SLOOP keeps 4 × 3.8 KB of project copies; there is no room for 8-track copies).
- Torn writes (power loss) never leave a mixed or empty project.
- Old SLOOP projects (FUN1–FUN5) stay where they are, untouched, readable for import.

## 2. Stream (little endian)

```
"FM1M" u32 · version u8 (1) · tracks u8 · selected u8 · G count u8 · G values i16[...] · song groove
per track:
  'T' · kind u8 (0 synth, 1 drums, 2 MIDI) · engine u8 · preset u8
  common params: count u8 · presence bitmap · i16 for each value ≠ its default
  engine params: count u8 (8) · presence bitmap · i16 for each value ≠ the engine's default
  groove: qloose amt… u8×7 · seed u16 · user offsets i8×16 · user velocities i8×16
  pad offsets i8×16
  step bitmap u8×8 (steps holding notes, lanes, a tie, a nudge, a raw deviation or a condition)
  each such step:
     synth: (n | time<<3) · flags · vel · lvl · rat · n notes        (7 + n bytes with the 2 below)
     drums: lanes on u16 · one byte per lane on (level | ratchet<<2)  (4 + lanes bytes with the 2 below)
     (micro & 63) | condition<<6 · (raw deviation & 63)
  locks: count u8 · (step u8, param u8, value i16) each
'E'
```

Counted parameter groups make newer and older firmware read each other's streams (missing values take defaults,
extra values are skipped); globals are append-only (SLOOP's rule). Every value is clamped to its range on load.

## 3. Chunks and storage

The stream is split into at most **2 chunks** of 3 824 bytes (capacity **7 648 B**). Each chunk is one storage
object (CRC-32, A/B copies, commit header written last — SLOOP's `storage.c`) holding a 16-byte chunk header:
`"FMPK"`, generation u32, index, count, stream length.

Save: the next generation is one more than any copy of any chunk holds; the chunks after the first are written,
then **chunk 0 last** (the commit). Load: chunk 0's newest valid copy gives the generation; every other chunk must
be found with that generation in one of its copies; the whole stream is parsed once **without changing anything**
(pass 1) and only then applied with interrupts off (pass 2).

Flash (FM1MOVE): 4 project slots + autosave × 2 chunks × A/B = 20 sectors = 80 KiB at `0xE7000` (the region of
SLOOP's USR4 sample slot, so **FM1 Move has user sample slots USR1–USR3**). SLOOP's project area (`0x97000`) is not
written.

## 4. Measured (host)

| Project | Stream | Chunks |
| --- | --- | --- |
| a session: 8 tracks, 2 drum kits, heavy engines, locks, grooves | 1 105 B | 1 |
| eight full 64-step tracks (single notes, 4-lane drum steps, nudges, raw timing) | 5 059 B | 2 |
| theoretical densest: 4-note chords / 16-lane drums on all 64 steps of all 8 tracks + all 24 locks | 8 502 B | refused |

The densest case exceeds the capacity by 11 %: `fmp_save` returns −9 ("PROJECT TOO DENSE") and writes nothing; the
previous save stays valid. The UI will show the fill level (`fmp_measure`).

## 5. Verified (host, `tests/projfmt_test.c`)

| Check | Result |
| --- | --- |
| eight-track project saves and loads back exactly (fingerprint of every field) | pass |
| typical project in one chunk; same stream after a load | pass |
| densest project refused, nothing written; eight full 64-step tracks fit and round-trip | pass |
| power cut at each of the 27 flash writes of a two-chunk save: old or new project, never a mix or nothing | pass |
| a damaged chunk or an empty slot: load refused, working project untouched | pass |
| four heavy engines requested: the one without an arena loads as ANALOG, reported | pass |
| autosave hash: stable without edits, changes with one | pass |

## 6. Still to do

- Import of SLOOP FUN1–FUN5 projects into FM1M (tracks 1–4, the drum track kept as track 4, tracks 5–8 empty).
- UI: PROJECT screen actions, "too dense" warning with the fill level, autosave through `fmp_measure` hashing.
- Backup/restore over the web editor protocol for FM1M objects.
