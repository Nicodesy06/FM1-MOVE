# FM1 Move — Memory budget

Measured on the SLOOP 2.5 baseline (§1; FM1 Move builds in §1b–1c) with the JieLi toolchain (`jieli-linux-toolchains-20260730.1`, clang 4.0.1
pi32v2) via `nm -S build/felucca.elf`; struct sizes from the host (`track_t` has no pointers, so its host size
equals the target size, cross-checked: `trk` = 7 760 B = 4 × 1 940 on the target).

Legend: **[BUILD]** measured, **[EST]** estimate with stated uncertainty.

## 1. Baseline (SLOOP 2.5, four tracks)

| Region | Capacity | Used | Free | Largest users |
| --- | --- | --- | --- | --- |
| RAM .data+.bss | 98 304 | 84 820 | 13 484 | `rev_line` 17 368, `trk` 7 760, `up_bank` 6 160, `abuf` 4 096, `proj_tmp` 3 840, `st_buf` 3 840, `proj_v3_tmp` 2 584, `drw_v` 2 208, MIDI/USB rings 2 048 ×3, `drums` 1 820 |
| POOL | 344 064 | 333 948 | 10 116 (8 192 must stay spare) | `dly_buf` 131 072, `punch_ring` 65 536, `cv_px` 59 520 (UI canvas 240 × 124), `sl_buf` 32 768, `eng_arena` 25 776 (3 × 8 592), `cho_buf` 4 096, `autosave_buf` 3 840, `song_keep` 3 840, `vis_sg` 3 456 |
| NOINIT | 15 696 | ~15 000 | small | `proj_slot[4]` (4 × ~3.6 KiB), settings, panel, debug |
| Stacks | 24 KiB user + 8 KiB system | not measured | — | [TEST-HW] high-water mark |
| Flash app slot | 581 564 | 565 200 | **16 364** | `SMP_DATA` 303 054 (CC0 samples, ADPCM), `ui_draw` 31 478, `FONT_S_DATA` 21 504, `mix_block` 16 582, `DS_KITS` 11 648, `ed_handle` 11 582, `ui_input` 11 142 |
| Flash data store | 0x097000–0x0E0000 (292 KiB) | projects 4 × 2 sectors, user presets, sample slots | — | `storage.c` |

Per-track cost today: `track_t` = 1 940 B, of which `voice_t v[8]` = 672 B (84 B each), steps 640 B,
micro 64 B, locks 96 B + lock state 120 B.

## 1b. FM1MOVE build (eight tracks, the new UI, FM1M projects with clips)

Measured by `FM1MOVE=1 sh build.sh` after the clips, the STEP page and the layer pages (a125cb0) [BUILD]:

| Region | Capacity | Used | Free |
| --- | --- | --- | --- |
| Flash app slot (image) | 581 564 | 548 248 | 33 316 |
| RAM .data+.bss | 98 304 | 93 360 | 4 944 |
| POOL | 344 064 | 333 072 | **10 992** (≥ 8 192 must stay spare: kept) |

Clips (`clip.c`): the bank `clip_mem` 8 192 B + offset / length tables 2 × 8 × 6 × 2 = 192 B, all in `.pool`; the
per-track state (clip playing, clip asked for, hash, flags) ~60 B of RAM. The bank is packed: a 16-step pattern
takes ~100–300 B, so 6 clips × 8 tracks of dense patterns do not all fit; a store that finds no room is refused
("CLIP MEMORY FULL") and the clip keeps its last stored version. The project stream (`FMP_MAX` 11 472 B, 3 chunks)
is sized for the tracks plus a full bank. Flash: 2 project slots + the autosave × 3 chunks (+1 spare object) × A/B =
20 sectors, 80 KiB at `0xE7000` (SLOOP's USR4 sample slot).

## 1c. FM1 Move 0.3 (SLOOP 2.6 merged)

Measured by `FM1MOVE=1 sh build.sh` after the SLOOP 2.6 merge (d7590be) [BUILD]:

| Region | Capacity | Used | Free |
| --- | --- | --- | --- |
| Flash app slot (image) | 581 564 | 569 828 | 11 736 |
| RAM .data+.bss | 98 304 | 96 496 | **1 808** |
| POOL | 344 064 | 333 160 | **10 904** (≥ 8 192 must stay spare: kept) |

What made SLOOP 2.6 fit the RAM:
- **INSERT in 4 shared states** (`fx.c` `INS_SLOTS`): one state is ~580 B, 8 would not fit. A track takes a free slot
  while its INSERT is on; a 5th track stays dry and the panel says "INSERT: 4 TRACKS MAX".
- **SLOOP 1.x project import** converts in the idle flash buffer instead of its own (2.6 KiB back).
- **MIDI note-routing table packed to nibbles** (1 KiB back).

After the STEP page MOVE and the battery indicator [BUILD]: image 573 764 B (7 800 B free), RAM 96 544 B (1 760 free),
pool unchanged.

FM1 Move 0.3.1 (testers' fixes) [BUILD]: image 574 676 B (6 888 free), RAM 96 576 B (1 728 free), pool unchanged.

1 808 B of RAM is the tightest margin so far: a new feature has to bring its RAM back (or live in `.pool`).

## 2. What eight tracks cost

| Item | Four tracks | Eight tracks | Delta | Notes |
| --- | --- | --- | --- | --- |
| `trk[]` | 7 760 | 15 520 | **+7 760 RAM** | [BUILD-derived] |
| new per-track fields (kind, MIDI ch, groove, quantise, clip refs) | — | ~8 × 24 | +~200 RAM | [EST ±100] |
| per-track filters / fades (`tflt`, mix state) | 4 × small | 8 × small | +~400 RAM | [EST ±200] |
| engine arenas (GRAIN/FM6/PHYS big state) | 3 × 8 592 pool | see policy | 0 to +43 KiB pool | see §3 |
| drum tracks (16 lanes, kit) | 1 global `drums` (1 820) | shared drum voice pool + per-track kit index | +~300 RAM | [EST] |
| project in RAM (`proj_tmp`, `autosave_buf`, `song_keep`) | 3 × 3 840 | 3 × ~7 400 (dense) | **+~10.7 KiB** | dense format does not fit — see §4 |
| `proj_slot[4]` in NOINIT | ~14.6 KiB | ~29 KiB dense | **does not fit** | §4 |

RAM after `trk[]` alone: 84 820 + 7 760 = 92 580 of 98 304 (5.7 KiB left) — feasible, but the project buffers
must change.

## 3. Engine arena policy

Each synth part owns an 8 592-byte arena for the engines with large state (GRAIN, FM6, PHYS share it; a track
plays one engine at a time). Eight synth-capable tracks with private arenas would need 68.7 KiB of pool.

Decision: a **pool of `N_ARENA` arenas** claimed by a track when it selects a heavy engine and released when it
leaves it. Light engines (ANALOG, FM4, PHASE, TRIO, DRAWBAR, FORMANT, LOFI, SAMPLE, NOISE) need none. When all
arenas are taken, the engine change is refused with an explicit message ("3 HEAVY ENGINES IN USE"). `N_ARENA`
starts at 3 (today's memory, zero cost) and grows to 4–5 once the new UI frees canvas memory (§5).
This limits **heavy engines**, never the number of tracks.

## 4. Project storage

Format 5 stores dense 10-byte steps for 64 steps × 4 tracks in one 3 840-byte flash object. Eight dense tracks
are ~7.4 KiB. Plan (`PROJECT_FORMAT.md`): a new format "FM1M" with **sparse steps** (only non-empty steps,
index + payload) and a project split over up to N flash objects (header object + track objects), CRC32 each,
A/B copies as today. The 4 `.noinit` RAM slots go away: slots live in flash only; one working-project mirror stays
in pool for autosave. Expected RAM: −14.6 KiB NOINIT, −3.8 KiB `song_keep` reuse.

## 5. Where memory can come from (ranked by risk)

| Source | Gain | Risk / cost |
| --- | --- | --- |
| New UI band renderer: 240 × 40 strip (19 200 B) instead of the 240 × 124 canvas | **~40 KiB pool** | the old UI must go first (it draws 124-row graphs) |
| Reclaim the 8 KiB "RAM-run debug" window 0x01C06000–0x01C08000 | 8 KiB | breaks RAM-run debugging only; [TEST-HW] |
| `proj_v3_tmp` (only to read formats 1–3) moved into a pool area shared with load-time buffers | 2.5 KiB RAM | none at runtime |
| Visualiser buffers (`vis_*`, `scope_*`, ~8 KiB) if the visualiser is dropped or reduced | ~8 KiB | feature cut, product decision |
| Delay line (`dly_buf`, 1.49 s mono-equivalent) | up to 64 KiB | changes the longest delay times; last resort |

## 6. Flash

16 KiB free in the app slot is the tightest budget. The FM1 Move UI needs new fonts (anti-aliased Inter, OFL)
and new drawing code. Plan: the new UI **replaces** SLOOP's drawing code (`ui_draw.c`, `ui_studio.c`,
`ui_vis.c` ≈ 70 KiB of code + Terminus 21 KiB) behind a build switch, so the image shrinks before it grows.
Fonts are stored 4 bpp with run-length coding and **only the glyphs used** (digits-only for the 48 px numerals).
Every build prints the image size; `tools/build.py` refuses an image that does not fit.

## 7. Stack

Not yet measured. To do: paint the stacks with a pattern at boot and report the high-water mark on the serial
console ([TEST-HW]); on the host, the soak test under ASan catches overflows of host stacks only.
