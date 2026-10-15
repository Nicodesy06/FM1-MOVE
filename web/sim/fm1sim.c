/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 Move in the browser: the firmware's own sources (FM1MOVE=1: the eight-track engine, Groove Engine, controls,
 * screens, FM1M projects) compiled to WebAssembly, freestanding, with the hardware doubled as in the host tests
 * (tests/fm1move_ui_test.c): the panel's buttons, keys, knobs and LEDs come and go through the exports below, the
 * 240 x 240 screen is a framebuffer, the flash a 1 MiB array (the page keeps the project region in localStorage).
 *
 *   sim_init()                       power-on: the default eight-track project
 *   sim_audio(n)                     n stereo frames (a multiple of 32) at 44.1 kHz into sim_pcm (int16, L R)
 *   sim_ui(ms)                       one UI frame at time ms: input, LEDs, draw; returns 1 if the screen changed
 *   sim_set(buttons, notes)          the buttons held (bit per label B_*), the keys held (bit per key, 0 = F3)
 *   sim_turn(role, steps)            a knob (EN_*) turned, + = clockwise
 *   sim_led(id)                      0 off, 1 dim, 2 lit: buttons by label (0..13), keys 100 + key
 *   sim_fb() sim_pcm_ptr() sim_nor()  the framebuffer (RGB565), the audio buffer, the flash
 *   sim_flash_dirty()                1 once after the flash was written (the page saves the projects)
 * Built by tools/build_sim.sh into docs/sim/fm1sim.wasm. Not the device: no SPI timing, no audio deadline. */
#define FM1MOVE 1
#define FELUCCA_FLASH 1
#define __attribute__(x)
#include <stdint.h>
#include "felucca_tables.h"
#include "../../firmware/src/libc.c"
static struct { volatile uint32_t notes, buttons; } fm1_in;
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static uint8_t nor[0x100000];                        /* the flash (its XIP window reads it straight) */
#define SMP_USER_XIP(k) ((const uint8_t *)nor + SMP_USER_OFF((uint32_t)(k)))   /* (eng_sample.c, fm6_bank.c: the XIP */
#define FM6_BANK_XIP(copy) ((const uint8_t *)nor + st_sector(OBJ_FM6BANK, copy) + ST_PAYLOAD_OFF)   /* window) */
#include "../../firmware/src/core.h"
#include "../../firmware/src/engines.c"
#include "../../firmware/src/drums.c"
#include "../../firmware/src/params.c"
#include "../../firmware/src/voice.c"
#include "../../firmware/src/slicer.c"
#include "../../firmware/src/fx.c"
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "../../firmware/src/usb.c"
#include "../../firmware/src/midi_uart.c"
#include "../../firmware/src/seq.c"

/* the screen */
static uint16_t sim_screen[240 * 240];               /* RGB565, not swapped */
static uint8_t sim_changed;
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    if (x + w > 240u || y + h > 240u)
        return;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            uint16_t c = p[j * w + i];
            sim_screen[(y + j) * 240u + x + i] = (uint16_t)((c >> 8) | (c << 8));
        }
    sim_changed = 1;
}
#include "../../firmware/src/gfx.c"
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    for (j = 0; j < h && y + j < 240u; j++)
        for (i = 0; i < w && x + i < 240u; i++)
            sim_screen[(y + j) * 240u + x + i] = c;
    sim_changed = 1;
}
/* the panel */
static int32_t encs[7];
static uint32_t fm1_ticks(void) { return fm1_ms * 1000u * 24u; }
#define FM1_TICKS_PER_US 24u
static int32_t fm1_enc_take(uint32_t e) { int32_t s = encs[e]; encs[e] = 0; return s; }
#define FM1_NCOL 11u
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL], fm1_led_bg[FM1_NCOL];
static volatile uint16_t fm1_led_bg_ns;
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {      /* the FM-1's matrix (hal/fm1_input.h) */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
static void fm1_led_key(uint32_t id, int on) { (void)id; (void)on; }
static uint32_t edges_btn, notes_seen;
static uint32_t fm1_input_edges(int x) { uint32_t e = edges_btn; (void)x; edges_btn = 0; return e; }
static uint32_t fm1_input_note_edges(void) { uint32_t e = fm1_in.notes & ~notes_seen; notes_seen = fm1_in.notes; return e; }
static void fm1_wdt_feed(void) {}
#include "../../firmware/src/panel.c"
#include "../../firmware/src/ui.c"
#include "../../firmware/src/ds.c"
#include "../../firmware/src/ui_move.c"
#include "../../firmware/src/ui_layers.c"
#include "../../firmware/src/ui_move_ctl.c"

/* the flash: a 1 MiB array (nor, at the top) behind the storage layer and the flash driver calls (fm1_flash.h) */
static uint8_t flash_ok = 1, sim_dirty;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + (off & ~0xFFFu), 0xFF, 4096); sim_dirty = 1; return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    sim_dirty = 1;
    return 0;
}
static int fl_write(uint32_t off, const void *src, uint32_t n) { return st_prog(off, src, n); }
static int fl_erase4k_quiet(uint32_t off, uint32_t *took) { *took = 0; return st_erase(off); }
static void fl_inval(uint32_t off, uint32_t n) { (void)off; (void)n; }
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0x856014u; }      /* the FM-1's 1 MiB part */
#define FL_FAR(f) f
static void fl_plain_window_init(void) {}
#include "../../firmware/src/storage.c"
#include "../../firmware/src/upreset.c"
#include "../../firmware/src/project.c"

/* the editor's SysEx (usb.c's frame ring on the device): one request frame in, the replies out to the page */
static uint8_t sim_rxb[1024], sim_tx[8192];
static uint32_t sim_rx_n, sim_tx_n;
#define SXQ 256u                                     /* (usb.c's SysEx out ring: the pushes wait for half of it) */
static uint32_t so_w, so_r;
static int ota_frame_get(const uint8_t **p, uint32_t *n)
{
    if (!sim_rx_n)
        return 0;
    *p = sim_rxb;
    *n = sim_rx_n;
    return 1;
}
static void ota_frame_done(void) { sim_rx_n = 0; }
static int ota_wire_send(const uint8_t *p, uint32_t n)
{
    if (sim_tx_n + n > sizeof sim_tx)
        return -1;
    memcpy(sim_tx + sim_tx_n, p, n);
    sim_tx_n += n;
    return 0;
}
#include "../../firmware/src/editor.c"

/* ------------------------------------------------------------------ exports --- */
#define SIM_MAXF 4096u
static int16_t sim_pcm[2u * SIM_MAXF];
static uint8_t sim_ready;

/* an erased flash (the page then puts back what it kept: sim_store_base / sim_store_size) */
void sim_init(void)
{
    memset(nor, 0xFF, sizeof nor);
    sim_ready = 0;
}

/* a new project: main.c's felucca_init without the autosave */
static void sim_defaults(void)
{
    uint32_t i;
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    fm6_init();
    project_default_tracks();
    song.sel = 0;
    song.master_q12 = 3200;
    song.g[G_SYNC] = (int16_t)lights_sync;
    song.g[G_MIDI] = (int16_t)lights_mout;
    song.g[G_ROUTE] = (int16_t)lights_min;
}

/* power-on, as main.c: the settings, the user presets and the sample slots from the flash, then the project as
 * it was left (the autosave) */
void sim_boot(void)
{
    panel = PANEL_DEFAULT;
    persist_boot();
    settings_init();
    sim_defaults();
    autosave_resume();
    layers_init();
    song.batt_raw = 600;                                 /* (no battery in a browser: a full one, as main.c would read it) */
    ctl.force = 1;
    sim_ready = 1;
}

/* RESET on the page: a new, empty project (what is saved stays) */
void sim_new(void)
{
    fm1_irq_off();
    panic_req = (1u << NTRK) - 1u;
    sim_defaults();
    fm1_irq_on();
    ctl.force = 1;
}

/* a demo to start from: a beat on track 1, a bassline on 2, chords on 3, a lead on 4, percussion on 7 */
void sim_demo(void)
{
    static const uint8_t BASS[16] = {36, 0, 36, 48, 0, 36, 0, 43, 36, 0, 46, 0, 36, 48, 0, 41};
    static const uint8_t CH1[3] = {60, 63, 67}, CH2[3] = {58, 62, 65};
    static const uint8_t LEAD[16] = {72, 0, 0, 75, 0, 0, 79, 0, 77, 0, 75, 0, 72, 0, 70, 0};
    uint32_t i;
    fm1_irq_off();
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    for (i = 0; i < 16u; i++) {
        if (i % 4u == 0u) dstep_set(&trk[0].dstep[i], 0, LV_NORM, 0);
        if (i % 8u == 4u) dstep_set(&trk[0].dstep[i], 2, LV_HARD, 0);
        if (i % 2u == 0u) dstep_set(&trk[0].dstep[i], 4, i % 4u ? LV_SOFT : LV_NORM, 0);
        if (i == 14u) dstep_set(&trk[0].dstep[i], 5, LV_NORM, 0);
        if (i % 3u == 1u) dstep_set(&trk[6].dstep[i], 14, LV_GHOST, 0);
        if (BASS[i]) {
            uint8_t n = BASS[i];
            step_add(&trk[1], i, n, 100, LV_NORM, 0);
        }
        if (LEAD[i])
            step_add(&trk[3], i, LEAD[i], 90, LV_SOFT, 0);
    }
    trk[2].p[P_SLEN] = 32;
    for (i = 0; i < 3u; i++) {
        step_add(&trk[2], 0, CH1[i], 90, LV_SOFT, 0);
        step_add(&trk[2], 16, CH2[i], 90, LV_SOFT, 0);
    }
    trk[2].step[0].time = trk[2].step[16].time = ST_NOTE;
    for (i = 1; i < 12u; i++) {
        trk[2].step[i].time = ST_TIE;
        trk[2].step[16 + i].time = ST_TIE;
    }
    trk[3].p[P_LEVEL] = 92;
    trk[3].p[P_DLY] = 40;
    for (i = 0; i < NTRK; i++)
        trk[i].seq_active = 1;
    fm1_irq_on();
}

static int16_t sat16(int32_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
int sim_audio(uint32_t n)
{
    static int32_t out[2u * CTL];
    uint32_t f, i;
    if (!sim_ready)
        return 0;
    if (n > SIM_MAXF)
        n = SIM_MAXF;
    for (f = 0; f + CTL <= n; f += CTL) {
        mix_block(out, CTL);
        for (i = 0; i < 2u * CTL; i++)
            sim_pcm[2u * f + i] = sat16(out[i]);
    }
    return (int)f;
}

int sim_ui(uint32_t ms)
{
    uint32_t h;
    if (!sim_ready)
        return 0;
    (void)h;
    fm1_ms = ms;
    sim_changed = 0;
    ed_service();                                    /* (main.c's loop) */
    ui_input();
    ui_leds();
    ui_draw();
    autosave_tick();                                 /* the working project into flash, when quiet (project.c) */
    return sim_changed;
}

void sim_set(uint32_t buttons, uint32_t notes)
{
    uint32_t b, m = 0;
    for (b = 0; b < NB; b++)
        if ((buttons >> b) & 1u)
            m |= 1u << panel.btn[b];
    edges_btn |= m & ~fm1_in.buttons;
    fm1_in.buttons = m;
    fm1_in.notes = notes & 0x7FFFFFFu;
}

void sim_turn(uint32_t role, int32_t steps)
{
    if (role < NE)
        encs[panel.enc[role]] += steps * panel.dir[role];
}

int sim_led(uint32_t id)
{
    uint32_t mid = id >= 100u ? 14u + (id - 100u) : (id < NB ? panel.btn[id] : 255u);
    uint8_t q;
    if (mid > 40u)
        return 0;
    q = ctl_led_pos[mid];
    if (q == 0xFF)
        return 0;
    if (fm1_led[q >> 3] & (1u << (q & 7u)))
        return 2;
    return (fm1_led_dim[q >> 3] & (1u << (q & 7u))) ? 1 : 0;
}

uint16_t *sim_fb(void) { return sim_screen; }
int16_t *sim_pcm_ptr(void) { return sim_pcm; }
uint8_t *sim_nor(void) { return nor; }
uint32_t sim_fmp_base(void) { return FMP_FLASH_BASE; }
uint32_t sim_fmp_size(void) { return FMP_OBJS * 2u * ST_SECTOR; }
uint32_t sim_store_base(void) { return 0x97000u; }   /* everything the firmware keeps: projects, presets, samples, */
uint32_t sim_store_size(void) { return 0x100000u - 0x97000u; }   /* the FM6 bank, the settings */

/* the editor's SysEx: the page writes one request (the bytes between F0 and F7) at sim_rx_ptr and calls sim_rx(n);
 * the device answers at once (ed_service), the replies (whole F0..F7 messages, pushes too) wait at sim_tx_ptr */
uint8_t *sim_rx_ptr(void) { return sim_rxb; }
int sim_rx(uint32_t n)
{
    if (!sim_ready || sim_rx_n || n > sizeof sim_rxb)
        return 0;
    sim_rx_n = n;
    ed_service();
    return 1;
}
uint8_t *sim_tx_ptr(void) { return sim_tx; }
uint32_t sim_tx_len(void) { return sim_tx_n; }
void sim_tx_clear(void) { sim_tx_n = 0; }
int sim_flash_dirty(void) { int d = sim_dirty; sim_dirty = 0; return d; }
uint32_t sim_bpm(void) { return (uint32_t)song.g[G_BPM]; }
uint32_t sim_playing(void) { return song.playing; }
