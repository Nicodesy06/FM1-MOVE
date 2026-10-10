# FM1 Move — Project format "FM1M" v2

Implementation: `firmware/src/projfmt.c` + `clip.c` (the clip section) + `storage.c` (`st_save_staged`, the FM1M
objects). Tests: `tests/projfmt_test.c`, `tests/clip_test.c` (both also under ASan + UBSan). Status: **format,
storage, the panel's PROJECT page, autosave and the editor protocol in the FM1MOVE build, verified in host simulation
only** (not yet on an FM-1). Import of SLOOP's four saved slots is still open (SLOOP's autosave is imported).

## 1. Goals and constraints

- Eight tracks and their clips (6 per track), every track's kind, sound, pattern, locks, micro-timing, raw timing, conditions, groove and pad
  offsets; globals and the song groove.
- **No project-sized RAM buffer** (SLOOP keeps 4 × 3.8 KB of project copies; there is no room for 8-track copies).
- Torn writes (power loss) never leave a mixed or empty project.
- Old SLOOP projects (FUN1–FUN5) stay where they are, untouched, readable for import.

## 2. Stream (little endian)

```
"FM1M" u32 · version u8 (2; 1 still loads) · tracks u8 · selected u8 · G count u8 · G values i16[...] · song groove
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
clips (version 2 only):
  'C' · clip count u8 (6)
  per track: clip playing u8 (0–5)
     per clip: length u16 (0 = empty) · that many bytes:
        'K' · LENGTH u8 · DIV u8 · the pattern (step bitmap, steps, locks: exactly as in a track record)
'E'
```

The "pattern" of a track record is the step bitmap, the steps and the locks (`fmp_put_pattern`). In version 2 a
track whose pattern is its current clip, stored in the bank, writes an **empty pattern** in its record (8 zero bytes
+ no locks: 9 bytes); the clip section carries it. A track whose pattern did not fit the bank (clip memory full)
writes it in its record as version 1 did, and on load that pattern wins over the clip's older stored version. A
clip's bytes are what the bank holds (`clip.c`, packed, the same encoding), so a load copies them straight in after
pass 1 has checked each one as a pattern of its track's kind, exactly `length` bytes long, all within the 8 KiB
bank. A reader takes up to 16 clips per track and skips those beyond its own 6.

Version 1 (FM1 Move 0.1) has no clip section: on load each track's pattern becomes its clip 1, clips 2–6 empty.

Counted parameter groups make newer and older firmware read each other's streams (missing values take defaults,
extra values are skipped); globals are append-only (SLOOP's rule). Every value is clamped to its range on load.

## 3. Chunks and storage

The stream is split into at most **3 chunks** of 3 824 bytes (capacity `FMP_MAX` **11 472 B**; FM1 Move 0.1: 2
chunks, 7 648 B). Each chunk is one storage
object (CRC-32, A/B copies, commit header written last — SLOOP's `storage.c`) holding a 16-byte chunk header:
`"FMPK"`, generation u32, index, count, stream length.

Save: the next generation is one more than any copy of any chunk holds; the chunks after the first are written,
then **chunk 0 last** (the commit). Load: chunk 0's newest valid copy gives the generation; every other chunk must
be found with that generation in one of its copies; the whole stream is parsed once **without changing anything**
(pass 1) and only then applied with interrupts off (pass 2).

Flash (FM1MOVE): **2 project slots + the autosave**, 3 chunks each: 10 objects × A/B copies = 20 sectors = 80 KiB
at `0xE7000` (the region of SLOOP's USR4 sample slot, so **FM1 Move has user sample slots USR1–USR3**). SLOOP's
project area (`0x97000`) is not written. Object map (`storage.c FMP_OBJ_MAP`):

| Slot | Chunks 0, 1, 2 in objects |
| --- | --- |
| project 1 (slot 0) | 0, 1, 2 |
| project 2 (slot 1) | 3, 4, 5 |
| autosave (slot 2) | 8, 9, 7 |
| spare | 6 |

Upgrade from FM1 Move 0.1 (4 projects + the autosave, 2 chunks each: project *n* in objects 2(n−1), 2(n−1)+1, the
autosave in 8, 9): **project 1 and the autosave keep their objects and still load**. Projects 2–4 are not carried
over: the new slot 2 reads as **empty** (the chunk it finds in object 3 is old project 2's chunk 1, which fails the
chunk index check), and old projects 3 and 4 sit in objects the new map uses as later chunks (4, 5, 7) or not at all
(6), so the panel and the device page cannot reach them (objects 3, 4, 5, 7 are overwritten by the next saves). Save them to files
from the device page with FM1 Move 0.1 before updating if they matter (`tests/projfmt_test.c`, "old layout").

## 4. Measured (host, version 1 stream, 2-chunk layout)

| Project | Stream | Chunks |
| --- | --- | --- |
| a session: 8 tracks, 2 drum kits, heavy engines, locks, grooves | 1 105 B | 1 |
| eight full 64-step tracks (single notes, 4-lane drum steps, nudges, raw timing) | 5 059 B | 2 |
| theoretical densest: 4-note chords / 16-lane drums on all 64 steps of all 8 tracks + all 24 locks | 8 502 B | refused |

With 2 chunks the densest case exceeded the capacity by 11 %. The capacity is now 11 472 B; a stream over it still
makes `fmp_save` return −9 ("PROJECT TOO DENSE") and write nothing, the previous save staying valid. A typical
16-step clip takes ~100–300 B of the clip section. Version 2 stream sizes have not been tabulated here yet
(`tests/projfmt_test.c` and `tests/clip_test.c` print them).

## 5. Verified (host, `tests/projfmt_test.c`)

| Check | Result |
| --- | --- |
| eight-track project saves and loads back exactly (fingerprint of every field) | pass |
| typical project in one chunk; same stream after a load | pass |
| densest project: fits in 3 chunks and round-trips, or is refused with nothing written; eight full 64-step tracks fit and round-trip | (see `TEST_REPORT.md`) |
| power cut at each flash write of a multi-chunk save: old or new project, never a mix or nothing | pass (2-chunk layout) |
| a damaged chunk or an empty slot: load refused, working project untouched | pass |
| four heavy engines requested: the one without an arena loads as ANALOG, reported | pass |
| autosave hash: stable without edits, changes with one | pass |
| FM1 Move 0.1 layout: project 1 and the autosave load; a slot over other projects' chunks reads as empty | new (see `TEST_REPORT.md`) |
| version 2: the clips save and load back byte for byte; a full bank: the pattern playing is still saved (`clip_test.c`) | new (see `TEST_REPORT.md`) |

## 6. Still to do

- Import of SLOOP's four saved slots (FUN1–FUN5) into FM1M (tracks 1–4, the drum track kept as track 4, tracks 5–8
  empty); SLOOP's working project is already imported at first start.
- A fill-level display on the PROJECT page (`fmp_measure`).
- Version 2 stream sizes for typical and dense projects with full clip banks (table in §4).

Done since v1: the PROJECT page (HOME held: save / load / delete, 2 slots), autosave through `fmp_measure` hashing,
backup / restore of the FM1M objects over the editor protocol (16 the working project, 17–18 projects 1–2; a restore
of up to 3 chunks is staged as `fmp_save` writes, chunk 0 last).
