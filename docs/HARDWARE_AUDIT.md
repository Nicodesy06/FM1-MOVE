# FM1 Move — Hardware audit (M-VAVE FM-1)

Status: M0 audit, 2026-10-10. Every line is tagged with its source:

- **[CODE]**: verified in the SLOOP 2.5 source (`firmware/hal/*`, `firmware/src/*`). That source runs on the device; its authors report hardware tests.
- **[BUILD]**: measured in this repository with the real JieLi toolchain (`tools/build.py` output).
- **[DOC]**: stated in third-party documentation (SLOOP/Dinghy README, FM-1-RE, the M-VAVE manual). Not re-verified here.
- **[INFER]**: technical inference from the above.
- **[TEST-HW]**: a hypothesis that can only be settled on a physical FM-1. **None of the FM1 Move work has been run on hardware yet.**

## 1. SoC and CPU

| Item | Value | Source |
| --- | --- | --- |
| SoC | JieLi AC791N / WL82 family, LQFP48 (unit marking `C156211-11B8`) | [DOC] FM-1-RE `docs/reversing/01-hardware-map.md` |
| CPU | pi32v2 (JieLi ISA), clang 4.0.1 toolchain `pi32v2` | [CODE] `tools/build.py`, [BUILD] |
| Cores used by SLOOP | one (bare metal, no RTOS) | [CODE] `firmware/src/main.c` |
| Second core | The stock firmware links a FreeRTOS-derived **SMP** kernel (per-CPU `pxCurrentTCB[2]`, context-switch SWI for "cpu1"). The same RE notes also say `CPU_CORE_NUM 1`. Whether CPU1 exists and can be started from a custom image is **unknown** | [DOC] FM-1-RE `docs/io/02-rtos.md`; contradiction noted |
| CPU clock | Set by the SPL (`uboot.boot`) before the app runs; not measured | [INFER]; [TEST-HW] |
| Timer base | TIMER4 free-running at 24 MHz (`FM1_TICKS_PER_US`) | [CODE] `hal/fm1_time.h` |
| Integer divide | The div-by-0 trap is **off**; a 64-bit division pulls a libgcc call the build rejects | [CODE] `hal/fm1_irq.h`, `tools/build.py` |

## 2. Memory

| Region | Address / size | Use (SLOOP 2.5) | Source |
| --- | --- | --- | --- |
| Internal SRAM | 0x01C00000 – 0x01C80000 (512 KiB) | everything below | [CODE] `firmware/app.ld` |
| RAMTEXT | 0x01C00000, 24 KiB | flash driver (must run with XIP off) | [CODE] |
| (debug) | 0x01C06000 – 0x01C08000, 8 KiB | reserved "RAM-run debug" — candidate to reclaim | [CODE] comment, [INFER] |
| RAM (.data/.bss) | 0x01C08000, 96 KiB | 84 820 B used | [BUILD] |
| POOL (.pool) | 0x01C20000, 336 KiB | 333 948 B used, build demands ≥ 8 KiB spare | [BUILD] |
| Stacks | 0x01C74000 – 0x01C7C000 (user 24 KiB, system 8 KiB, guard bands) | | [CODE] |
| NOINIT | 0x01C7C000, 15.6 KiB | 4 project slots, settings, panel table, crash info | [CODE] |
| Reserved | 0x01C7FD50 – | boot info, mailbox, vectors | [CODE] |
| SPI NOR flash | 1 MiB (0x000000 – 0x0FFFFF) | | [CODE] `hal/fm1_flash.h` address map |
| App slot (XIP) | 0x02000120, 581 564 B | image 565 200 B → **16 364 B free** | [BUILD] |
| Data store | 0x097000 – 0x0E0000 | projects, user presets, samples (A/B sectors) | [CODE] `storage.c` |
| OTA staging / FM6 bank / USR4 / globals | 0x0E0000 – 0x0FF000 | | [CODE] |

There is no PSRAM/SDRAM. All memory is internal SRAM plus 1 MiB of flash. **This is the main constraint of
the project** (see `MEMORY_BUDGET.md`).

## 3. Audio

| Item | Value | Source |
| --- | --- | --- |
| Output | I2S (ALNK0) to an external codec, 24-bit words | [CODE] `hal/fm1_audio.h`, `src/audio.c` |
| Sample rate | 44 100 Hz | [CODE] `core.h FS`, [DOC] |
| Buffering | double buffer, 256 frames a half (5.8 ms); render in 32-sample control blocks (CTL) | [CODE] `audio.c`, `core.h` |
| Event resolution | one CTL block = 32 samples = 0.73 ms (events quantised to the block start) | [CODE] `seq.c events_block` |
| ISR priority | ALNK0 at 3, TIMER5 (input scan, USB, UART poll) at 4 and may nest | [CODE] `main.c` |
| Load metering | `song.cpu_q8`, `felucca_dbg.max_us`, `late` (deadline miss), shedding at > 85 % two halves in a row | [CODE] `audio.c`, `voice.c shed_voice` |
| Audio input | **none** (no line/mic in). USB audio is *output to the host* only | [CODE] `usb.c` (UAC IN = device → host), [DOC] |

## 4. Display

| Item | Value | Source |
| --- | --- | --- |
| Panel | 1.54" 240 × 240 IPS TFT, ST7789-class controller | [CODE] `src/lcd.c` init sequence, [DOC] |
| Bus | SPI1 (PC9 CLK, PC10 DO, PC7 CS, PC8 D/C), write-only, DMA from RAM | [CODE] `hal/fm1_lcd_hw.h` |
| SPI clock | `lsb / (BAUD+1)`, BAUD 4 = **12 MHz** | [CODE] `lcd.c` comment |
| Pixel format | RGB565, byte-swapped in RAM; IPS inversion on | [CODE] |
| Full-frame cost | 115 200 B → ~77 ms at 12 MHz (~13 fps max for full redraws) | [INFER] from the clock |
| Backlight | PA2, active low, on/off only (no PWM in SLOOP) | [CODE] |
| Tearing | no TE pin wired in the HAL | [CODE]; [TEST-HW] whether one exists |

Consequence for the UI: full-screen redraws are expensive. Partial redraw (dirty bands) is mandatory, and a
faster SPI clock (BAUD 2 or 3) is a **[TEST-HW]** item, not an assumption.

## 5. Controls (physical panel)

Verified from the matrix map `hal/fm1_input.h` and the label table `src/panel.c` [CODE]:

| Control | Count | Notes |
| --- | --- | --- |
| Keys | 27 (F3–G5): 16 white, 11 black | white LED per key (dimmable glow layers) |
| Function buttons | 12: FX, SEL, ENV, LFO, EDIT, GLO / HOME, SAVE, ARP, SEQ, PLAY, REC | LED each; matrix ids remappable by calibration |
| Octave buttons | 2: OCT−, OCT+ | LED each |
| Encoders | 7 endless, **no push switch**: SELECT, ALGORITHM, PRESETS, KNOB 1–4 | detent = one quadrature cycle |
| MASTER | 1 potentiometer (ADC), volume | not assignable without breaking its print |
| Battery | ADC channel (divider) | [CODE] `main.c` |

There are **no pads**, no touch strip, no dedicated track buttons, no shift button and no encoder push. FM1 Move
must express its workflow with *hold-a-button + key/knob* combinations, as SLOOP does (see `UI_MAPPING.md`).

Physical layout [DOC: M-VAVE manual]: left panel MASTER, SELECT, PRESETS, ALGORITHM; centre the screen; right
panel KNOB 1–4 above two rows of six buttons; the keybed under it.

## 6. MIDI and USB

| Item | Value | Source |
| --- | --- | --- |
| USB | full-speed device: USB-MIDI in/out (class compliant), UAC1 audio *to host* 44.1/48 kHz, optional CDC serial | [CODE] `usb.c` |
| TRS MIDI | 3.5 mm **IN** only (UART1, 31 250 baud). No physical MIDI OUT is driven | [CODE] `hal/fm1_uart.h`, `midi_uart.c` |
| Clock | MIDI clock in (USB or TRS), start/stop/continue | [CODE] `seq.c mclk_*` |
| Bluetooth | present in the SoC; **never enabled** by SLOOP | [CODE] LICENSING.md, [DOC] |

So "MIDI OUT tracks" means **USB-MIDI out**. A TRS MIDI OUT is not available unless a hardware TX line is
found [TEST-HW].

## 7. Boot, update and recovery

| Item | Value | Source |
| --- | --- | --- |
| Boot | mask ROM → SPL `uboot.boot` → app at XIP 0x02000120 | [CODE] `tools/build.py`, [DOC] FM-1-RE |
| Package | `.fwsc` = app + OTA loader, built by `tools/fm1pkg_make.py` (identity string `FM-1_900`) | [CODE] |
| Update | over USB-MIDI SysEx (M-UPGRADE protocol); web installer or `tools/fm1_install.py` | [CODE] |
| USB rescue | hold OCT− at power-on, or two failed boots → polled USB updater (`recovery.c`) | [CODE] `bootguard.h` |
| UBOOT | OCT− + OCT+ 5 s (stopped) or SysEx → mask-ROM UBOOT | [CODE] `main.c` |
| Last-resort recovery | FM-1-transporter (external) | [DOC] SLOOP BUILDING.md |
| Bootloader | **never modified** by SLOOP or FM1 Move | policy |

FM-1-RE warns that ROM recovery and interrupted-write safety for the single-bank layout are not established
[DOC]. FM1 Move keeps SLOOP's update and rescue paths byte-for-byte and never writes to the device without
explicit permission (see `CLAUDE.md`).

## 8. Open hardware questions ([TEST-HW])

1. CPU clock and real audio-ISR cost per engine on the device (SLOOP's `target_budget` is a static proxy).
2. Can CPU1 be started from a custom image (see `DUAL_CORE_RESEARCH.md`)?
3. Highest stable LCD SPI clock (BAUD 2/3), and whether the panel exposes TE.
4. Backlight PWM (PA2 on a timer output?).
5. Free GPIO for a TRS MIDI OUT.
6. Readability of the new UI on the real panel (viewing angle, gamma, smallest text size).
