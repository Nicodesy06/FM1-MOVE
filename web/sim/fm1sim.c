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
#define __attribute__(x)
#include <stdint.h>
#include "felucca_tables.h"
#include "../../firmware/src/libc.c"
static struct { volatile uint32_t notes, buttons; } fm1_in;
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
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
static int up_used(uint32_t k) { (void)k; return 0; }
static int up_load(uint32_t k) { (void)k; return 0; }
static uint32_t up_count(void) { return 0; }
static uint32_t up_nth(uint32_t n) { return n; }
static uint32_t up_rank(uint32_t s) { return s; }
static void up_name(uint32_t k, char *b) { (void)k; str_cpy(b, "USER", 8); }
static void up_slot_label(char *b, uint32_t k) { fmt_int(b, (int32_t)k + 1); }
static void up_ui(uint32_t op, uint32_t k) { (void)op; (void)k; }
static void settings_save(void) {}
static void project_save(uint32_t slot) { (void)slot; }
static void project_load(uint32_t slot) { (void)slot; }
static int project_used(uint32_t slot) { (void)slot; return 0; }
static void arrangement_save(void) {}
/* the flash */
static uint8_t nor[0x100000];
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
#include "../../firmware/src/storage.c"
static uint32_t fmp_obj(uint32_t slot, uint32_t chunk) { return OBJ_FMP0 + (slot % FMP_SLOTS) * 2u + chunk % 2u; }
#include "../../firmware/src/projfmt.c"
static int fmp_used(uint32_t slot) { fmp_r_t r; return !fmp_open(&r, slot); }
static int project_save_fmp(uint32_t slot)
{
    if (song.playing || transport_req)
        return -2;
    return fmp_save(slot);
}
static int project_load_fmp(uint32_t slot)
{
    if (song.playing || transport_req) {
        ui_message("STOP BEFORE LOAD");
        return -2;
    }
    panic_req = (1u << NTRK) - 1u;
    return fmp_load(slot, 0);
}
static void project_delete_fmp(uint32_t slot)
{
    uint32_t k, c;
    for (k = 0; k < FMP_NCHUNK; k++)
        for (c = 0; c < 2u; c++)
            st_erase(st_sector(fmp_obj(slot, k), c));
}
#include "../../firmware/src/ds.c"
#include "../../firmware/src/ui_move.c"
#include "../../firmware/src/ui_layers.c"
#include "../../firmware/src/ui_move_ctl.c"

/* ------------------------------------------------------------------ exports --- */
static uint32_t autosave_hash_sim, autosave_ms_sim;
static int audio_quiet_sim(void)                     /* nothing sounds (the autosave waits for it, as project.c) */
{
    uint32_t p, i;
    for (p = 0; p < NTRK; p++)
        for (i = 0; i < NVOICE; i++)
            if (trk[p].v[i].active)
                return 0;
    for (i = 0; i < NDRUM; i++)
        if (drums.v[i].active)
            return 0;
    return 1;
}
#define SIM_MAXF 4096u
static int16_t sim_pcm[2u * SIM_MAXF];
static uint8_t sim_ready;

void sim_init(void)
{
    uint32_t i;
    memset(nor, 0xFF, sizeof nor);
    panel = PANEL_DEFAULT;
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    song.g[G_BPM] = 120;
    song.master_q12 = 3200;
    fm6_init();
    layers_init();
    project_default_tracks();
    song.sel = 0;
    ctl.force = 1;
    sim_ready = 1;
}

/* the page restored the flash's project region: the working project comes back (as at power-on) */
void sim_resume(void)
{
    if (fmp_load(4u, 0) == 0)
        fmp_measure(&autosave_hash_sim);
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
    fm1_ms = ms;
    sim_changed = 0;
    ui_input();
    ui_leds();
    ui_draw();
    /* the autosave, as on the device: stopped, quiet, a few seconds after the last touch */
    if (!song.playing && !transport_req && ms - ui_input_ms > 2500u && ms - autosave_ms_sim > 5000u) {
        autosave_ms_sim = ms;
        fmp_measure(&h);
        if (h != autosave_hash_sim && audio_quiet_sim() && fmp_save(4u) == 0)
            autosave_hash_sim = h;
    }
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
uint32_t sim_fmp_size(void) { return 2u * FMP_SLOTS * 2u * ST_SECTOR; }
int sim_flash_dirty(void) { int d = sim_dirty; sim_dirty = 0; return d; }
uint32_t sim_bpm(void) { return (uint32_t)song.g[G_BPM]; }
uint32_t sim_playing(void) { return song.playing; }
