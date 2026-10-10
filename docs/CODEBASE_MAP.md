# FM1 Move — Codebase map

Baseline: **SLOOP 2.5** (`master` = tag `v2.5`, commit `fa9ce57`), GPL-3.0-only, itself based on Felucca by
Leo Kuroshita / Hügelton Instruments. FM1 Move work happens on `fm1-move/dev`.

Why SLOOP 2.5: it is the newest tagged release, it builds reproducibly with the public JieLi toolchain
(verified here, see `TEST_REPORT.md`), and its host test suite passes unchanged. There is no beta branch upstream;
every tag is a release.

## Build model

One compilation unit: `firmware/src/felucca.c` `#include`s every `.c` file in a fixed order (the HAL is
header-only). No dynamic allocation; large buffers are placed in `.pool` with
`__attribute__((section(".pool")))`. `tools/build.py` compiles, links (`firmware/app.ld`), generates the
tables, fonts, icons, samples and drum kits into `build/gen/`, packages the `.fwsc`, and **fails the build** on:
RAM overflow, pool headroom < 8 KiB, image larger than the app slot, calls out of `.ram_text`, ROM references,
register access outside `hal/`, 64-bit divisions.

## Directory map

| Path | Role |
| --- | --- |
| `firmware/hal/` | Header-only hardware layer: LCD SPI, input matrix/encoders/LEDs, I2S audio, ADC, flash (SFC), USB SIE, UART, timers, IRQ, guards |
| `firmware/src/felucca.c` | Unity build, feature flags (`FELUCCA_FLASH/OTA/CDC/UAC/UART`) |
| `firmware/src/main.c` | Boot (`fm1_cstart`), main loop (UI ~60 fps max), TIMER5 ISR (10 kHz scan, USB/UART polls) |
| `firmware/src/audio.c` | ALNK0 ISR: renders a 256-frame half in 32-sample blocks, load metering, shedding trigger |
| `firmware/src/core.h` | Data model: `track_t`, `voice_t`, `step_t`, `dstep_t`, `song_t`, parameters, transport clock units |
| `firmware/src/seq.c` | Keyboard, layers, scales/chords, arpeggiator, note repeat, sequencer, recording, transport, MIDI in/clock (runs in the audio ISR) |
| `firmware/src/voice.c` | Voice allocation (8-voice budget shared by the synth parts), stealing, mono/legato/unison, envelopes, engine switch, per-track render |
| `firmware/src/engines.c`, `eng_*.c` | 12 engines (ANALOG, FM4, FM6, PHASE, TRIO, DRAWBAR, FORMANT, GRAIN, LOFI, SAMPLE, PHYS, NOISE); big state in a per-part arena (`eng_arena[NPART]`) |
| `firmware/src/drums.c`, `drum_synth.c` | The drum track: 16 lanes, 6 own voices, sampled/synth/user kits, click |
| `firmware/src/fx.c` | Per-track insert (drive, slicer, filter), level/pan/mute fades, sends, chorus/delay/reverb, master DUST/DUCK/DJ filter/limiter, `mix_block` |
| `firmware/src/punch.c`, `slicer.c` | Punch-in FX (64 KiB loop ring), per-track slicer |
| `firmware/src/params.c` | Parameter descriptors (`TP[]`, `GP[]`), pages (`PAGES[]`), formatting |
| `firmware/src/project.c`, `storage.c`, `upreset.c` | Project formats FUN1–FUN5, autosave, A/B flash objects with CRC32, user presets, settings |
| `firmware/src/arranger*.c` | Song sections A–D and the arrangement clock |
| `firmware/src/lcd.c`, `gfx.c` | ST7789 driver; strip canvas renderer (`cv_px` 240 × 124, 4-bit anti-aliased fonts) |
| `firmware/src/ui*.c` | The SLOOP UI: state (`ui.c`), pages (`ui_draw.c`), the four-track "studio" screens (`ui_studio.c`), layers (`ui_layers.c`), menu, song, visualiser, input dispatch (`ui_input.c`) |
| `firmware/src/usb.c`, `midi_uart.c` | USB-MIDI, UAC1 audio to host, CDC; TRS MIDI IN |
| `firmware/src/editor*.c`, `console.c` | Web-editor SysEx protocol, serial console |
| `firmware/src/ota.c`, `recovery.c`, `firmware/loader/` | M-UPGRADE update entry, USB rescue, update loader |
| `tests/` | Host tests (gcc): sequencer, UI pages, projects, storage, soak/stress (ASan/UBSan), golden render hashes, CPU budget |
| `tools/` | Build, packaging, generators (fonts, tables, samples, kits), installer |
| `web/` | Web installer and editor (Web MIDI) |

## Signal and control path

```
TIMER5 10 kHz ─ fm1_input_tick (matrix, encoders, LEDs) ─ usb_poll / uart_midi_poll → midi_in_q
main loop      ─ ui_input (keys → kb queue, buttons, knobs → params) ─ ui_draw (lazy, strip blits)
ALNK0 ISR      ─ for each 32-sample block: mix_block
                  └ events_block: transport, MIDI queue, keys, seq_tick ×NTRK, arp, rolls, click, clock advance
                  └ mix_part ×NPART (engine render → insert → level/pan → sends) + drums
                  └ fx_buses → DUST → punch → DJ filter → master → I2S
```

UI ↔ audio coupling: the UI writes parameters (`int16_t p[]`) and posts requests (`transport_req`,
`panic_req`, key queue); the ISR reads them at block boundaries. Pattern edits from the UI are bracketed with
`fm1_irq_off()/on()`.

## Where four tracks are assumed (M1 work list)

Counted with `grep NTRK|NPART|TRK_DRUM|TDRUM|is_drum` (files with hits):
`seq.c` 68, `ui_studio.c` 38, `ui_input.c` 28, `editor.c` 26, `ui_layers.c` 24, `project.c` 23, `eng_fm6.c` 15,
`ui_draw.c` 13, `voice.c` 11, `ui.c` 10, `fx.c` 7, `core.h` 7, plus single hits elsewhere.

Structural assumptions, by kind:

| Assumption | Where | Plan (see `DEVELOPMENT_PLAN.md`) |
| --- | --- | --- |
| `NTRK 4`, `NPART 3`, the drum track is index 3 (`TRK_DRUM`, `TDRUM`, `is_drum(t) = t == TDRUM`) | `core.h`, everywhere | track **type** per track (`t->kind`: SYNTH, DRUM, MIDI); `is_drum(t)` reads the kind |
| one global drum machine (`drums` struct, 6 voices, one kit = `TDRUM->p[P_E0]`) | `drums.c` | per-drum-track kit; a shared drum voice pool keyed by track |
| synth parts are `trk[0..NPART-1]`, arenas `eng_arena[NPART]` | `voice.c`, `engines.c`, `fx.c` | loop over tracks of kind SYNTH; heavy-engine arenas become a pool of N claimed on demand |
| `song.rec`, `song.solo`, `panic_req` are bit-per-track in `uint8_t` | `core.h`, `seq.c` | fits 8 bits; audit the shifts (`1u << NTRK`) |
| MIDI channel map: ch 1–3 → parts, DRUMS ch → drum track | `seq.c trk_midi_ch, midi_track` | per-track MIDI channel parameter |
| UI: four colours `TE_COL[4]`, `& 3u` indexing, "keys 1–4 mute, 5–8 solo" on GLO, four columns = four tracks | `ui_studio.c`, `ui_layers.c` | new UI (FM1 Move design system), 8 track colours |
| project format: `proj_trk_t t[NTRK]` in one 3 840-byte flash object; 4 slots in `.noinit` | `project.c`, `storage.c` | format FM1M v1: sparse steps, multi-object; see `PROJECT_FORMAT.md` |
| undo holds one track's `step_t[64]` | `ui.c`, `seq.c` | unchanged (per selected track) |
| editor SysEx addresses tracks 0..3 | `editor.c`, `web/` | extend with a protocol version bump |
| arranger scenes hold 4 tracks' patterns | `arranger_scene.c`, `project.c` | sections hold 8 |

## Real-time components

Runs in the audio ISR (must stay bounded, no flash, no LCD): `events_block` and everything it calls (`seq.c`),
`voice.c`, engines, `drums.c`, `fx.c`, `punch.c`, `slicer.c`, `uac_tap`. Runs nested in it: TIMER5 scan and
`uac_service`. Runs in the main loop: UI, LCD, flash writes (with audio silenced for the erase window, and only
when quiet: `autosave_tick`), SysEx editor, console.
