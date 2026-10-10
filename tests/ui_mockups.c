/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 Move UI on the host: the firmware's own design system (firmware/src/ds.c) and screens
 * (firmware/src/ui_move.c), fed with demo data, rendered through the band renderer into a 240 x 240
 * framebuffer and written as PPM files (tools/ui_mockups.py turns them into PNGs and a contact sheet).
 * It also checks the partial redraw: an unchanged frame sends nothing, a playhead step sends only its rows.
 *
 *   cc -O1 -Ibuild/gen -Ifirmware/src -o build/host/ui_mockups tests/ui_mockups.c && build/host/ui_mockups OUTDIR */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* --- the LCD and the strip canvas, as lcd.c / gfx.c provide them on the device --- */
static uint16_t fb[240 * 240];                 /* RGB565 (not swapped) */
static uint32_t blits, blit_px;
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t i, j;
    blits++;
    blit_px += w * h;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            uint16_t c = px[j * w + i];
            fb[(y + j) * 240 + x + i] = (uint16_t)((c >> 8) | (c << 8));
        }
}
#define CV_MAX (240u * 124u)
static uint16_t cv_px[CV_MAX];
static uint32_t cv_w, cv_h;
static void cv_begin(uint32_t w, uint32_t h, uint16_t bg)
{
    uint32_t i;
    cv_w = w;
    cv_h = h;
    for (i = 0; i < w * h; i++)
        cv_px[i] = (uint16_t)((bg >> 8) | (bg << 8));
}

#include "ds.c"
#include "ui_move.c"

static int fails;
static void check(int ok, const char *what)
{
    printf("ui_mockups: %-66s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        fails++;
}

static void save(const char *dir, const char *name)
{
    char p[512];
    FILE *f;
    uint32_t i;
    snprintf(p, sizeof p, "%s/%s.ppm", dir, name);
    f = fopen(p, "wb");
    if (!f) {
        perror(p);
        fails++;
        return;
    }
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t c = fb[i];
        uint8_t rgb[3] = {(uint8_t)((c >> 11) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63),
                          (uint8_t)((c & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void kn(uint32_t i, const char *label, const char *value, int ratio, int bip)
{
    mv.k[i].label = label;
    snprintf(mv.k[i].value, sizeof mv.k[i].value, "%s", value);
    mv.k[i].ratio = (int16_t)ratio;
    mv.k[i].bipolar = (uint8_t)bip;
}

static void trk(uint32_t i, const char *name, const char *sound, const char *eng, int kind, int act, int vol, int pan)
{
    mv_track_t *t = &mv.t[i];
    snprintf(t->name, sizeof t->name, "%s", name);
    snprintf(t->sound, sizeof t->sound, "%s", sound);
    snprintf(t->engine, sizeof t->engine, "%s", eng);
    t->kind = (uint8_t)kind;
    t->act = (uint8_t)act;
    t->vol = (uint8_t)vol;
    t->pan = (int8_t)pan;
    t->clip = 0xFF;
    t->queued = 0xFF;
}

/* a session in progress: eight tracks, playing, bar 3 of 4 */
static void demo(void)
{
    static const char *const FX[16] = {"LOOP 4", "LOOP 8", "LOOP 16", "LOOP 32", "STUTTER", "REVERSE", "TAPE",
                                       "HALF", "LP SWP", "HP SWP", "PHONE", "CRUSH", "ALIAS", "GATE", "ECHO",
                                       "WOBBLE"};
    uint32_t i;
    memset(&mv, 0, sizeof mv);
    trk(0, "DRUMS", "909 KIT", "DRUMS", MK_DRUM, 92, 80, 0);
    trk(1, "BASS", "ACID 303", "ANALOG", MK_SYNTH, 70, 74, -6);
    trk(2, "CHORDS", "DUB CHORD", "TRIO", MK_SYNTH, 0, 62, 12);
    trk(3, "LEAD", "SUPERSAW", "TRIO", MK_SYNTH, 35, 58, 20);
    trk(4, "KEYS", "RHODES", "FM6", MK_SYNTH, 48, 66, -18);
    trk(5, "PAD", "STR CLOUD", "GRAIN", MK_SYNTH, 22, 50, 0);
    trk(6, "PERC", "AFRO KIT", "DRUMS", MK_DRUM, 64, 60, 30);
    trk(7, "EXT", "MIDI OUT", "CH 5", MK_MIDI, 40, 70, 0);
    mv.t[5].mute = 1;
    mv.t[3].solo = 0;
    for (i = 0; i < 8u; i++) {
        mv.t[i].clips = (uint8_t)(0x3Fu >> (i % 3u) & ~(i == 5u ? 0x30u : 0u));
        mv.t[i].clip = i == 5u ? 0xFFu : (uint8_t)(i % 2u);
    }
    mv.t[2].queued = 2;
    mv.t[6].clip = 0xFF;
    mv.scene = 0xFF;
    mv.blink = 1;
    mv.sel = 1;
    mv.playing = 1;
    mv.bpm10 = 1240;
    mv.quant_on = 1;
    mv.groove_on = 1;
    mv.bar = 3;
    mv.beat = 2;
    mv.bars = 4;
    mv.step16 = 6;
    mv.hot = -1;
    mv.held = -1;
    snprintf(mv.key, sizeof mv.key, "C MIN");
    for (i = 0; i < 16u; i++)
        mv.fx[i] = FX[i];
}

static void knobs_macro(void)
{
    kn(0, "CUTOFF", "64", 504, 0);
    kn(1, "RESO", "38", 300, 0);
    kn(2, "ENV", "+22", 670, 1);
    kn(3, "DRIVE", "12", 95, 0);
}

static void render(const char *dir, const char *name)
{
    char w[96];
    uint32_t c0 = ds_clipped;
    mv_render(1);
    save(dir, name);
    snprintf(w, sizeof w, "%s: every label fits its box", name);
    check(ds_clipped == c0, w);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    uint32_t i, rows;
    static const uint8_t NOTES[16] = {36, 0, 36, 48, 0, 36, 0, 43, 36, 0, 46, 0, 36, 48, 0, 41};
    static const uint8_t LV[16] = {4, 0, 2, 3, 0, 3, 0, 4, 3, 0, 2, 0, 4, 3, 0, 1};

    /* PLAY */
    demo();
    knobs_macro();
    render(dir, "01_play");

    /* TRACKS */
    demo();
    mv.screen = SC_TRACKS;
    mv.t[1].arm = 1;
    kn(0, "TYPE", "SYNTH", -1, 0);
    kn(1, "MIDI CH", "2", 66, 0);
    kn(2, "LENGTH", "32", 500, 0);
    kn(3, "DIV", "1/16", 400, 0);
    render(dir, "02_tracks");

    /* SEQUENCER: synth */
    demo();
    mv.screen = SC_SEQ;
    mv.len = 32;
    mv.spage = 0;
    snprintf(mv.div, sizeof mv.div, "1/16");
    for (i = 0; i < 16u; i++) {
        mv.st_note[i] = NOTES[i];
        mv.st_lvl[i] = NOTES[i] ? LV[i] : 0;
        mv.st_gate[i] = 1;
    }
    mv.st_gate[7] = 2;
    mv.st_lock[3] = 1;
    mv.st_micro[10] = 70;
    kn(0, "DIV", "1/16", 400, 0);
    kn(1, "SWING", "58", 580, 0);
    kn(2, "LENGTH", "32", 500, 0);
    kn(3, "SHIFT", "0", 500, 1);
    render(dir, "03_sequencer");

    /* SEQUENCER: drums, a step held */
    demo();
    mv.screen = SC_SEQ;
    mv.sel = 0;
    mv.len = 16;
    snprintf(mv.div, sizeof mv.div, "1/16");
    for (i = 0; i < 16u; i++) {
        mv.lanes[i] = (uint16_t)((i % 4u == 0u ? 1u : 0u) | (i % 8u == 4u ? 4u : 0u) | (i % 2u ? 0u : 16u) |
                                 (i == 14u ? 32u : 0u) | (i % 3u == 1u ? 1u << 13 : 0u));
        mv.st_lvl[i] = (mv.lanes[i] & 16u) ? (i % 4u ? LVK_SOFT : LVK_HARD) : 0;
    }
    mv.lane = 4;
    snprintf(mv.lane_name, sizeof mv.lane_name, "HAT");
    mv.held = 6;
    mv.st_lock[6] = 1;
    kn(0, "LEVEL", "SOFT", 500, 0);
    kn(1, "RATCHET", "X2", 333, 0);
    kn(2, "NUDGE", "-4", 437, 1);
    kn(3, "PROB", "75%", 750, 0);
    render(dir, "04_sequencer_drums");

    /* GROOVE */
    demo();
    mv.screen = SC_GROOVE;
    snprintf(mv.tmpl, sizeof mv.tmpl, "MPC 16");
    snprintf(mv.div, sizeof mv.div, "1/16");
    mv.swing = 58;
    mv.quant = 75;
    mv.gamount = 40;
    mv.human = 12;
    {
        static const int8_t OFF[8] = {0, 5, 0, 5, -1, 6, 1, 4};
        static const uint8_t VEL[8] = {120, 70, 100, 64, 116, 72, 96, 60};
        memcpy(mv.g_off, OFF, 8);
        memcpy(mv.g_vel, VEL, 8);
    }
    kn(0, "SWING", "58%", 580, 0);
    kn(1, "QUANT", "75%", 750, 0);
    kn(2, "GROOVE", "40%", 400, 0);
    kn(3, "HUMAN", "12%", 120, 0);
    render(dir, "05_groove");

    /* GROOVE while KNOB 1 turns: the value takes the body */
    mv.hot = 0;
    snprintf(mv.k[0].value, sizeof mv.k[0].value, "62");
    mv.k[0].ratio = 620;
    snprintf(mv.hot_unit, sizeof mv.hot_unit, "%%");
    render(dir, "06_groove_focus");

    /* CLIPS */
    demo();
    mv.screen = SC_CLIPS;
    mv.row_sel = 1;
    kn(0, "CLIP", "2", 200, 0);
    kn(1, "LAUNCH", "CLIPS", -1, 0);
    kn(2, "COPY", "EDIT", -1, 0);
    kn(3, "CLEAR", "REC", -1, 0);
    render(dir, "07_clips");

    /* MIXER */
    demo();
    mv.screen = SC_MIXER;
    mv.t[0].solo = 0;
    kn(0, "VOLUME", "74", 740, 0);
    kn(1, "PAN", "L6", 440, 1);
    kn(2, "REVERB", "20", 200, 0);
    kn(3, "DELAY", "35", 350, 0);
    render(dir, "08_mixer");

    /* SOUND: preset browser */
    demo();
    mv.screen = SC_SOUND;
    mv.npages = 4;
    mv.page = 0;
    snprintf(mv.cat, sizeof mv.cat, "BASS");
    mv.preset_i = 4;
    mv.preset_n = 24;
    snprintf(mv.around[0], 16, "SUB BASS");
    snprintf(mv.around[1], 16, "REESE");
    knobs_macro();
    render(dir, "09_sound");

    /* SOUND: envelope page */
    mv.page = 1;
    mv.env[0] = 8;
    mv.env[1] = 40;
    mv.env[2] = 55;
    mv.env[3] = 50;
    kn(0, "ATTACK", "4 MS", 80, 0);
    kn(1, "DECAY", "320", 400, 0);
    kn(2, "SUSTAIN", "55", 550, 0);
    kn(3, "RELEASE", "180", 500, 0);
    render(dir, "10_sound_env");

    /* PERFORM: STUTTER held */
    demo();
    mv.screen = SC_PERFORM;
    mv.fx_on = 1u << 4;
    kn(0, "FILTER", "OFF", 500, 1);
    kn(1, "DUST", "0", 0, 0);
    kn(2, "DUCK", "30", 300, 0);
    kn(3, "TRK FLT", "LP 40", 300, 1);
    render(dir, "11_perform");

    /* PROJECT (as the firmware lists it: 2 slots, the autosave keeps the work in progress) */
    demo();
    mv.screen = SC_PROJECT;
    mv.playing = 0;
    mv.npages = 2;
    mv.page = 0;
    snprintf(mv.row_l[0], 16, "1  PROJECT");
    snprintf(mv.row_r[0], 16, "SAVED");
    snprintf(mv.row_l[1], 16, "2  EMPTY");
    mv.row_r[1][0] = 0;
    mv.nrows = 2;
    mv.row_sel = 0;
    mv.row_cur = 0xFF;
    kn(0, "SAVE", "SAVE", -1, 0);
    kn(1, "EDIT", "LOAD", -1, 0);
    mv.k[2].label = mv.k[3].label = 0;
    render(dir, "12_project");

    /* SETTINGS (the firmware's rows, ui_move_ctl.c ST_LABEL) */
    demo();
    mv.screen = SC_SETTINGS;
    mv.npages = 2;
    mv.page = 1;
    mv.playing = 0;
    {
        static const char *const L[6] = {"CLOCK IN", "MIDI OUT", "MIDI IN", "LIGHTS", "KEY LIGHTS", "USB AUDIO"};
        static const char *const R[6] = {"USB", "SEQ", "NOTES", "LOW", "WHITE", "MAIN"};
        for (i = 0; i < 6u; i++) {
            snprintf(mv.row_l[i], 16, "%s", L[i]);
            snprintf(mv.row_r[i], 16, "%s", R[i]);
        }
        mv.nrows = 6;
        mv.row_sel = 0;
    }
    kn(0, "VALUE", "USB", -1, 0);
    mv.k[1].label = mv.k[2].label = mv.k[3].label = 0;
    render(dir, "13_settings");

    /* a destructive action asks first */
    demo();
    mv.screen = SC_SEQ;
    mv.len = 32;
    snprintf(mv.div, sizeof mv.div, "1/16");
    mv.modal = 1;
    snprintf(mv.modal_t, sizeof mv.modal_t, "CLEAR TRACK 2?");
    snprintf(mv.modal_b, sizeof mv.modal_b, "32 STEPS AND 3 LOCKS");
    snprintf(mv.modal_ok, sizeof mv.modal_ok, "REC CLEAR");
    knobs_macro();
    render(dir, "14_confirm");

    /* a toast */
    demo();
    mv.screen = SC_CLIPS;
    mv.row_sel = 1;
    mv.toast_t = 30;
    snprintf(mv.toast, sizeof mv.toast, "NEXT BAR: CLIP 3");
    kn(0, "CLIP", "2", 200, 0);
    kn(1, "LAUNCH", "CLIPS", -1, 0);
    kn(2, "COPY", "EDIT", -1, 0);
    kn(3, "CLEAR", "REC", -1, 0);
    render(dir, "15_toast");

    /* partial redraw: nothing changed -> no render, no rows; one playhead step -> only the rows it touches */
    demo();
    mv.screen = SC_SEQ;
    mv.len = 16;
    snprintf(mv.div, sizeof mv.div, "1/16");
    for (i = 0; i < 16u; i++) {
        mv.st_note[i] = NOTES[i];
        mv.st_lvl[i] = NOTES[i] ? LV[i] : 0;
    }
    knobs_macro();
    mv_render(1);
    rows = ds_rows_sent;
    mv_render(0);
    check(ds_rows_sent == rows, "an unchanged frame sends no row");
    mv.step16 = 7;
    mv_render(0);
    printf("ui_mockups: playhead step on SEQUENCER: %u of 240 rows sent\n", (unsigned)(ds_rows_sent - rows));
    check(ds_rows_sent - rows < 140u, "a playhead step sends less than the whole screen");
    rows = ds_rows_sent;
    mv.bpm10 = 1250;
    mv_render(0);
    printf("ui_mockups: a BPM change: %u rows sent\n", (unsigned)(ds_rows_sent - rows));
    check(ds_rows_sent - rows <= 24u, "a BPM change sends only the status bar");
    rows = ds_rows_sent;
    mv.hot = 1;
    snprintf(mv.k[1].value, sizeof mv.k[1].value, "40");
    mv_render(0);
    printf("ui_mockups: knob turn (focus overlay on): %u rows sent\n", (unsigned)(ds_rows_sent - rows));
    rows = ds_rows_sent;
    snprintf(mv.k[1].value, sizeof mv.k[1].value, "41");
    mv.k[1].ratio = 410;
    mv_render(0);
    printf("ui_mockups: knob turn (next value): %u rows sent\n", (unsigned)(ds_rows_sent - rows));
    check(ds_rows_sent - rows < 120u, "the next value of a turned knob sends under half the screen");
    printf("ui_mockups: fonts %u B\n", (unsigned)DS_FONT_BYTES);
    printf(fails ? "ui_mockups: FAILED\n" : "ui_mockups: all checks ok\n");
    return fails != 0;
}
