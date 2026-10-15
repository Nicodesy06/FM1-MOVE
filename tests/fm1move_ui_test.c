/* SPDX-License-Identifier: GPL-3.0-only */
/* A virtual FM1 running FM1 Move: the real controls (ui_move_ctl.c), layers (ui_layers.c), screens (ui_move.c,
 * ds.c) and the eight-track engine (FM1MOVE=1), with the audio running between UI frames as on the device, the
 * panel, LEDs and flash simulated. It presses buttons, plays keys and turns knobs as a user would:
 *   navigation   every screen button opens its screen; the same button again its next page; HOME held the system
 *                pages; SELECT the track, PRESETS the sound, ALGORITHM the screen's navigator
 *   layers       SEL + key selects a track; GLO + keys mute / solo tracks 1..8; SEQ + keys sets steps; EDIT + OCT-
 *                undoes; FX + key punches in
 *   knobs        the knob strip edits what it shows; a turn opens the focus overlay, which closes by itself
 *   groove       swing, quantize strength, template (and its suggested swing), humanize from the GROOVE screen
 *   safety       a track's kind changes only after its button confirms; anything else cancels
 *   projects     save, change, load back through the PROJECT page (the real FM1M format on a simulated NOR)
 *   fuzz         thousands of frames of random use: no crash, every blit on the screen, budgets kept
 * Live screenshots of every screen: OUTDIR/live-*.ppm (tools/ui_mockups.py --live makes PNGs of them). */
#define FM1MOVE 1
#define main hostsim_main
#include "hostsim.c"
#undef main
#include <assert.h>

static uint16_t screen[240 * 240];
static uint32_t blits;
static void lcd_sync(void) {}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p)
{
    uint32_t i, j;
    assert(x + w <= 240 && y + h <= 240);
    blits++;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            screen[(y + j) * 240 + x + i] = p[j * w + i];
}
#include "../firmware/src/gfx.c"
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, j;
    assert(x + w <= 240 && y + h <= 240);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            screen[(y + j) * 240 + x + i] = swap16(c);
}
static int32_t encs[7];
static uint32_t fm1_ticks(void) { return fm1_ms * 1000u * 24u; }
#define FM1_TICKS_PER_US 24u
static int32_t fm1_enc_take(uint32_t e) { int32_t s = encs[e]; encs[e] = 0; return s; }
#define FM1_NCOL 11u
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL], fm1_led_bg[FM1_NCOL];
static volatile uint16_t fm1_led_bg_ns;
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
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
#include "../firmware/src/panel.c"
#include "../firmware/src/ui.c"
/* user presets: none (upreset.c is the firmware's) */
static int up_used(uint32_t k) { (void)k; return 0; }
static int up_load(uint32_t k) { (void)k; return 0; }
static int up_load_to(track_t *t, uint32_t k) { (void)t; (void)k; return 0; }
/* BUG-01 watch: preset_pos_of asks up_count, so every value song.sel holds while the UI looks presets up is seen
 * here; one other than the frame's own selection (at its start or end) is the race that sent keys to another track */
static uint32_t sel_seen, sel_glitch;
static uint32_t up_count(void) { sel_seen |= 1u << (song.sel & 31u); return 0; }
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
/* the flash: a simulated NOR under the real storage layer and project format */
static uint8_t nor[0x100000];
static uint8_t flash_ok = 1;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + (off & ~0xFFFu), 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage.c"
#include "../firmware/src/projfmt.c"
static int fmp_used(uint32_t slot) { fmp_r_t r; return !fmp_open(&r, slot); }
static int project_save_fmp(uint32_t slot) { return fmp_save(slot); }
static int project_load_fmp(uint32_t slot)
{
    if (song.playing || transport_req)
        return -2;
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
#include "../firmware/src/ds.c"
#include "../firmware/src/ui_move.c"
#include "../firmware/src/ui_layers.c"
static struct { uint32_t max_us, late; } felucca_dbg;   /* (audio.c's diagnostics: no render deadline here) */
#include "../firmware/src/ui_move_ctl.c"

static const char *outdir;
static void ppm(const char *name)
{
    char path[512];
    FILE *f;
    uint32_t i;
    snprintf(path, sizeof path, "%s/live-%s.ppm", outdir, name);
    f = fopen(path, "wb");
    assert(f);
    fprintf(f, "P6\n240 240\n255\n");
    for (i = 0; i < 240u * 240u; i++) {
        uint16_t p = swap16(screen[i]);
        uint8_t rgb[3] = {(uint8_t)((p >> 11) * 255 / 31), (uint8_t)(((p >> 5) & 63) * 255 / 63), (uint8_t)((p & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static void frame(void)
{
    static int32_t o[CTL * 2];
    uint32_t q;
    for (q = 0; q < 22u; q++)
        mix_block(o, CTL);
    {
        uint32_t s0 = song.sel;
        sel_seen = 0;
        ui_input();
        ui_leds();
        ui_draw();
        if (sel_seen & ~((1u << s0) | (1u << (song.sel & 31u))))
            sel_glitch++;
    }
    fm1_ms += 16;
}
static void frames(uint32_t n) { while (n--) frame(); }
static uint32_t BT(uint32_t b) { return 1u << panel.btn[b]; }
static void press(uint32_t b) { edges_btn |= BT(b); fm1_in.buttons |= BT(b); frame(); }
static void release(uint32_t b) { fm1_in.buttons &= ~BT(b); frame(); }
static void tap(uint32_t b) { press(b); release(b); }
static void hold_ms(uint32_t b, uint32_t ms) { press(b); frames(ms / 16u); release(b); }
static void keyw(uint32_t w) { uint32_t k = key_of_white(w); fm1_in.notes |= 1u << k; frame(); fm1_in.notes &= ~(1u << k); frame(); }
static void turn(uint32_t role, int32_t s) { encs[panel.enc[role]] += s * panel.dir[role]; frame(); }
static int fails;
static void check(int ok, const char *what) { printf("fm1ui: %-74s %s\n", what, ok ? "ok" : "FAIL"); fails += !ok; }

int main(int argc, char **argv)
{
    uint32_t i, k;
    outdir = argc > 1 ? argv[1] : "build/host";
    memset(nor, 0xFF, sizeof nor);
    panel = PANEL_DEFAULT;
    host_tracks_init();
    song.g[G_BPM] = 122;
    song.batt_raw = 600;                                  /* (a full battery, as main.c would read it: in every picture) */
    layers_init();
    project_default_tracks();
    frames(4);

    /* power-on */
    check(is_drum(&trk[0]) && is_drum(&trk[6]) && is_synth(&trk[1]) && NTRK == 8u,
          "power-on: eight tracks, drums on 1 and 7, synths elsewhere");
    for (i = 0, k = 0; i < NTRK; i++)
        k += is_synth(&trk[i]) && eng_heavy(trk[i].eng_req);
    check(k == 0u, "power-on: light engines only (the heavy-engine arenas are free)");
    check(ctl.screen == SC_PLAY && mv.screen == SC_PLAY, "power-on: the PLAY screen");

    /* a beat, so the screens show something */
    for (i = 0; i < 16u; i++) {
        if (i % 4u == 0u) dstep_set(&trk[0].dstep[i], 0, LV_NORM, 0);
        if (i % 2u == 0u) dstep_set(&trk[0].dstep[i], 4, LV_SOFT, 0);
        if (i % 8u == 4u) dstep_set(&trk[0].dstep[i], 2, LV_HARD, 0);
        if (i % 3u == 0u) {
            uint8_t n = (uint8_t)(36 + (i % 5u) * 2u);
            put_step(&trk[1], i, 1, &n, ST_NOTE, 0);
        }
    }
    trk[0].seq_active = trk[1].seq_active = 1;
    tap(B_PLAY);
    frames(60);
    check(song.playing, "PLAY starts the transport");
    ppm("01-play");

    /* every screen from its button */
    {
        static const struct { uint8_t btn, screen; const char *name; } NAV[] = {
            {B_SCL, SC_TRACKS, "02-tracks"}, {B_SEQ, SC_SEQ, "03-sequencer"}, {B_ARP, SC_GROOVE, "05-groove"},
            {B_SAVE, SC_CLIPS, "07-clips"}, {B_GLO, SC_MIXER, "08-mixer"}, {B_EDIT, SC_SOUND, "09-sound"},
            {B_FX, SC_PERFORM, "11-perform"}, {B_HOME, SC_PLAY, "01b-play"}};
        uint32_t n, ok = 1;
        for (n = 0; n < sizeof NAV / sizeof NAV[0]; n++) {
            tap(NAV[n].btn);
            frames(8);
            ok &= ctl.screen == NAV[n].screen && !ctl.menu;
            if (NAV[n].screen == SC_SEQ) {
                turn(EN_SELECT, -8);              /* the drum track for its screenshot */
                frames(4);
                ppm("04-sequencer-drums");
                turn(EN_SELECT, 1);
                frames(4);
            }
            ppm(NAV[n].name);
        }
        check(ok, "every screen button opens its screen");
    }
    tap(B_ENV);
    frames(4);
    check(ctl.screen == SC_SOUND && ctl.page[SC_SOUND] == 1u, "ENV opens SOUND on its envelope page");
    ppm("10-sound-env");
    tap(B_EDIT);
    tap(B_EDIT);
    check(ctl.screen == SC_SOUND && ctl.page[SC_SOUND] == 3u, "the screen's button again: its next page");

    /* SELECT, SEL + key */
    tap(B_HOME);
    turn(EN_SELECT, -10);
    check(song.sel == 0u, "SELECT turned left stops at track 1");
    turn(EN_SELECT, 3);
    check(song.sel == 3u, "SELECT: one track a detent");
    press(B_SCL);
    keyw(5);
    release(B_SCL);
    check(song.sel == 5u && ctl.screen == SC_PLAY, "SEL held + white key 6: track 6, no screen change (no tap)");

    /* a button held + SELECT: the tempo (SLOOP's way; SELECT alone stays the track) */
    {
        int16_t bpm0 = song.g[G_BPM];
        uint8_t sel0 = song.sel;
        press(B_GLO);
        turn(EN_SELECT, 5);
        release(B_GLO);
        check(song.g[G_BPM] == bpm0 + 5 && song.sel == sel0 && ctl.screen == SC_PLAY,
              "GLO held + SELECT: the tempo, one BPM a detent (no track change, no tap)");
        press(B_FX);
        turn(EN_SELECT, -500);
        release(B_FX);
        check(song.g[G_BPM] == GP[G_BPM].min, "... any button held, and it stops at the lowest tempo");
        press(B_FX);
        turn(EN_SELECT, bpm0 - GP[G_BPM].min);
        release(B_FX);
        check(song.g[G_BPM] == bpm0, "... and back");
    }

    /* GLO + keys: mute / solo */
    press(B_GLO);
    keyw(2);
    keyw(8 + 4);
    release(B_GLO);
    check(trk[2].p[P_MUTE] && (song.solo & (1u << 4)) && ctl.screen == SC_PLAY, "GLO + key 3 mutes track 3, + key 13 solos track 5");
    press(B_GLO);
    keyw(2);
    keyw(8 + 4);
    release(B_GLO);
    check(!trk[2].p[P_MUTE] && !song.solo, "... and the same keys again undo both");

    /* SEQ + keys on the drum track: steps */
    turn(EN_SELECT, -10);
    pen_lane = 2;
    {
        uint32_t before = dstep_has(&trk[0].dstep[1], 2);
        press(B_SEQ);
        keyw(1);
        release(B_SEQ);
        check(dstep_has(&trk[0].dstep[1], 2) != before, "SEQ + white key 2: the drum step 2 toggles (the lane played last)");
        press(B_EDIT);
        press(B_OCTDN);
        release(B_OCTDN);
        release(B_EDIT);
        check(dstep_has(&trk[0].dstep[1], 2) == before, "EDIT + OCT-: undo puts the step back");
    }

    /* the step sequencer without recording, on a synth track: SEQ held + a white key writes a step (the pen note:
     * SEQ held + KNOB 1), a step key held + KNOB 1 changes its note, the same key again clears it */
    turn(EN_SELECT, 1);
    {
        track_t *t = &trk[1];
        steps_clear(t);
        ui.step_page = 0;
        press(B_SEQ);
        turn(EN_K1, 62 - (int32_t)pen_note[0]);       /* the pen: D4 */
        keyw(4);
        release(B_SEQ);
        check(!song.rec && step_on(&t->step[4]) && t->step[4].note[0] == 62 && t->step[4].time == ST_NOTE,
              "SEQ + KNOB 1 + white key 5: a D4 step written on track 2, without recording");
        press(B_SEQ);
        fm1_in.notes |= 1u << key_of_white(4);         /* step 5 held */
        frame();
        turn(EN_K1, 3);
        fm1_in.notes &= ~(1u << key_of_white(4));
        frame();
        release(B_SEQ);
        check(step_on(&t->step[4]) && t->step[4].note[0] == 65, "... step 5 held + KNOB 1: its note up 3 (F4), the step stays");
        press(B_SEQ);
        keyw(4);
        release(B_SEQ);
        check(!step_on(&t->step[4]), "... SEQ + the same key again: the step is cleared");
    }
    turn(EN_SELECT, -1);

    /* knobs and the focus overlay */
    tap(B_GLO);
    turn(EN_SELECT, 1);
    {
        int16_t v0 = trk[1].p[P_LEVEL];
        turn(EN_K1, -5);
        check(trk[1].p[P_LEVEL] == v0 - 5 && mv.hot == 0, "MIXER: KNOB 1 turns track 2's volume and takes the screen");
        ppm("08b-mixer-focus");
        frames(90);
        check(mv.hot < 0, "... the focus overlay closes by itself");
    }

    /* PRESETS */
    {
        uint8_t p0 = trk[1].preset, e0 = trk[1].eng_req;
        turn(EN_PRESET, 1);
        check(trk[1].preset != p0 || trk[1].eng_req != e0, "PRESETS: the next sound of the selected synth track");
        turn(EN_SELECT, -1);
        p0 = (uint8_t)trk[0].p[P_E0];
        turn(EN_PRESET, 1);
        check(trk[0].p[P_E0] == p0 + 1, "PRESETS on a drum track: the next kit");
        turn(EN_SELECT, 1);
    }

    /* GROOVE */
    tap(B_ARP);
    turn(EN_K2, -40);
    check(groove_strength(&trk[1].gv) == 60u, "GROOVE: KNOB 2 sets the quantize strength (100 % -> 60 %)");
    turn(EN_ALGO, 3);
    check(trk[1].gv.own && trk[1].gv.tmpl == GT_GARAGE && trk[1].p[P_SSWING] == GROOVES[GT_GARAGE].swing,
          "GROOVE: ALGORITHM picks the template, its swing comes with it");
    turn(EN_K4, 10);
    check(trk[1].gv.hum_t == 10u, "GROOVE: KNOB 4 humanize");
    frames(90);
    ppm("05b-groove-live");

    /* a destructive change asks */
    tap(B_SCL);
    turn(EN_K1, 1);
    check(ctl.arm == ACT_KIND && mv.modal, "TRACKS: KNOB 1 asks before track 2 becomes drums (modal)");
    ppm("14-confirm-live");
    tap(B_GLO);
    check(!ctl.arm && is_synth(&trk[1]), "... another button cancels: still a synth");
    tap(B_SCL);
    turn(EN_K1, 1);
    tap(B_SCL);
    check(is_drum(&trk[1]) && track_empty(&trk[1]), "... SEL confirms: track 2 is drums, its pattern empty");
    turn(EN_K1, 1);
    tap(B_SCL);
    check(is_synth(&trk[1]), "... and back to a synth");

    /* projects through the system pages */
    tap(B_PLAY);
    frames(10);
    hold_ms(B_HOME, 800);
    check(ctl.menu && ctl.screen == SC_PROJECT, "HOME held: the PROJECT page");
    frames(100);                                          /* (the toast and the knob focus gone: a clean picture) */
    ppm("12-project-live");
    {
        int16_t bpm = song.g[G_BPM];
        tap(B_SAVE);
        frames(4);
        check(fmp_used(0) && !mv.modal, "PROJECT: SAVE saves slot 1 (empty: no question)");
        song.g[G_BPM] = 99;
        trk[0].p[P_E0] = 3;
        tap(B_EDIT);
        check(ctl.arm == ACT_LOAD, "PROJECT: EDIT asks before loading over the work in progress");
        tap(B_EDIT);
        frames(4);
        check(song.g[G_BPM] == bpm && trk[0].p[P_E0] != 3, "... EDIT again loads slot 1 back exactly");
        tap(B_SAVE);
        check(ctl.arm == ACT_SAVE, "PROJECT: SAVE over a used slot asks first");
        tap(B_FX);
        check(!ctl.arm, "... and another button cancels it");
    }
    hold_ms(B_HOME, 800);
    check(ctl.menu && ctl.screen == SC_SETTINGS, "HOME held again: SETTINGS");
    frames(100);                                          /* (the toast and the knob focus gone: a clean picture) */
    ppm("13-settings-live");
    /* SETTINGS: the metronome and the count-in */
    {
        uint32_t n0;
        check(!strcmp(mv.row_l[0], "METRONOME") && !strcmp(mv.row_r[0], "OFF") && !song.playing,
              "SETTINGS: METRONOME first, OFF");
        turn(EN_K1, 1);                                   /* (one value a detent event) */
        turn(EN_K1, 1);
        check(song.g[G_CLOCK] == 2 && !strcmp(mv.row_r[0], "ON"), "... KNOB 1: OFF, REC, ON");
        n0 = click_n;
        tap(B_PLAY);
        frames(70);
        check(song.playing && click_n >= n0 + 2u, "... ON: it clicks on every beat while it plays");
        tap(B_PLAY);
        frames(4);
        turn(EN_K1, -1);
        n0 = click_n;
        tap(B_PLAY);
        frames(70);
        check(song.g[G_CLOCK] == 1 && click_n == n0, "... REC: silent while it only plays");
        tap(B_PLAY);
        frames(4);
        turn(EN_ALGO, 1);
        turn(EN_K1, 1);
        check(ctl.row[SC_SETTINGS] == 1u && rec_count && !strcmp(mv.row_r[1], "1 BAR"), "COUNT-IN: 1 BAR");
        tap(B_REC);
        n0 = click_n;
        tap(B_PLAY);
        frames(4);
        check(ci_on && !song.rec && click_n > n0, "... REC armed, PLAY: a bar of clicks first, not recording yet");
        frames(140);
        check(!ci_on && song.playing && song.rec && click_n >= n0 + 4u, "... then it records, the metronome on (REC mode)");
        tap(B_REC);
        tap(B_PLAY);
        frames(4);
        turn(EN_K1, -1);
        turn(EN_ALGO, -1);
        turn(EN_K1, -1);
        check(!rec_count && song.g[G_CLOCK] == 0 && !song.playing && !song.rec, "... both back to OFF");
        turn(EN_ALGO, 2);
        check(!strcmp(mv.row_l[2], "REC QUANT") && !strcmp(mv.row_r[2], "1 BAR"), "REC QUANT: 1 BAR");
        turn(EN_K1, 1);
        turn(EN_K1, 1);
        check(rec_q == 4u && !strcmp(mv.row_r[2], "4 BARS"), "... KNOB 1: 2 BARS, 4 BARS");
        {
            uint32_t w = lights_word();
            rec_q = 0;
            lights_from_word(w);
            check(rec_q == 4u, "... kept with the settings");
            lights_from_word(w & ~(7u << 21));
            check(rec_q == 1u, "... settings saved before it: 1 BAR");
        }
        turn(EN_ALGO, -2);
        turn(EN_ALGO, 20);
        check(!strcmp(mv.row_l[mv.row_sel], "VERSION") && !strcmp(mv.row_r[mv.row_sel], FELUCCA_VERSION),
              "the list scrolls to its last row, VERSION");
        turn(EN_ALGO, -20);
    }
    tap(B_HOME);
    check(!ctl.menu && ctl.screen == SC_PLAY, "HOME tapped: back to PLAY");

    /* REC while playing, on the cycle (SETTINGS > REC QUANT, 1 BAR by default) as Ableton's quantisation */
    {
        uint32_t n, ok;
        check(rec_q == 1u, "REC QUANT: 1 BAR by default");
        tap(B_PLAY);
        frames(40);                                       /* (inside the first bar) */
        tap(B_REC);
        check(song.playing && !song.rec && rec_pend == REC_PEND_ON, "playing, REC mid-bar: the take waits for the bar");
        for (n = 0; n < 400u && !song.rec; n++)
            frame();
        check(song.rec && (clk_beat & 3u) == 0u, "... and starts on the next bar's first beat");
        frames(30);
        tap(B_REC);
        check(song.rec && rec_pend == REC_PEND_OFF, "REC during the take mid-bar: it goes on to the end of the bar");
        for (n = 0; n < 400u && song.rec; n++)
            frame();
        check(!song.rec && (clk_beat & 3u) == 0u, "... and ends as the bar closes");
        rec_q = 4;
        frames(20);
        tap(B_REC);
        for (n = 0; n < 1200u && !song.rec; n++)
            frame();
        ok = song.rec && (clk_beat & 3u) == 0u && ((clk_beat >> 2) % 4u) == 0u;
        frames(140);
        tap(B_REC);
        frames(4);
        check(ok && song.rec, "4 BARS: the take starts on a 4-bar boundary; REC after a bar does not stop it yet");
        for (n = 0; n < 1200u && song.rec; n++)
            frame();
        check(!song.rec && (clk_beat & 3u) == 0u && ((clk_beat >> 2) % 4u) == 0u, "... it stops when the 4 bars close");
        tap(B_REC);
        tap(B_REC);
        frames(2);
        check(!rec_pend && !song.rec, "REC twice before the cycle: cancelled");
        tap(B_REC);
        tap(B_PLAY);
        frames(4);
        check(!song.playing && !rec_pend && !song.rec, "STOP drops a REC that waits");
        rec_q = 0;
        tap(B_PLAY);
        frames(40);
        tap(B_REC);
        check(song.rec, "REC QUANT OFF: REC records at once (as SLOOP)");
        tap(B_REC);
        tap(B_PLAY);
        frames(4);
        rec_q = 1;
    }

    /* FX + key: punch-in while held */
    tap(B_PLAY);
    frames(30);
    press(B_FX);
    {
        uint32_t kk = key_of_white(4);
        fm1_in.notes |= 1u << kk;
        frames(6);
        check(punch.cur == 4 || punch.req == 4, "FX held + white key 5: the fifth punch-in effect plays");
        ppm("11b-perform-held");
        fm1_in.notes &= ~(1u << kk);
        frames(6);
    }
    release(B_FX);

    /* the STEP page: SEQ again; keys write the cursor's step and move on, the knobs edit it, EDIT clears it */
    track_select(3);
    tap(B_SEQ);
    if (ctl.page[SC_SEQ] != 1u)
        tap(B_SEQ);
    frames(2);
    check(ctl_roll() && mv.npages == 2u && mv.page == 1u, "SEQ again on a synth track: the STEP page");
    cursor_set(0);
    steps_clear(TSEL);
    TSEL->p[P_VOICE] = 0;                                 /* (poly: chords) */
    keyw(0);
    check(step_on(&TSEL->step[0]) && TSEL->step[0].n == 1 && ui.cursor == 1u, "a key writes the cursor's step, letting go moves on");
    {
        uint32_t k1 = key_of_white(0), k2 = key_of_white(2);
        fm1_in.notes |= 1u << k1;
        frame();
        fm1_in.notes |= 1u << k2;
        frame();
        check(ui.cursor == 1u, "... still on the step while a key is held");
        fm1_in.notes &= ~((1u << k1) | (1u << k2));
        frame();
    }
    check(TSEL->step[1].n == 2 && ui.cursor == 2u, "two keys together: a chord in one step");
    turn(EN_K3, -1);                                      /* (an empty step is a rest: one down, a tie) */
    check(TSEL->step[2].time == ST_TIE && !strcmp(mv.k[2].value, "TIE"), "TIME: the step becomes a tie");
    turn(EN_K1, -2);
    check(ui.cursor == 0u && !strcmp(mv.k[0].value, "1/16"), "STEP: the cursor (1/16)");
    {
        uint32_t n0 = TSEL->step[0].note[0];
        turn(EN_K2, 2);
        turn(EN_K4, 1);
        check(TSEL->step[0].note[0] == n0 + 2u && (TSEL->step[0].flags & SF_ACCENT), "NOTE transposes it, FLAG accents it");
    }
    {                                                     /* (a phrase in the rest of the page, for the picture) */
        static const uint8_t PH[12] = {67, 0, 70, 72, 0, 74, 72, 70, 0, 67, 65, 62};
        uint32_t w;
        for (w = 0; w < 12u; w++)
            if (PH[w]) {
                uint8_t n = PH[w];
                put_step(TSEL, 4u + w, 1, &n, ST_NOTE, 0);
            }
        TSEL->step[5].time = ST_TIE;
        TSEL->step[9].flags |= SF_SLIDE;
        TSEL->step[13].flags |= SF_ACCENT;
    }
    frames(100);                                          /* (the toast and the knob focus gone: a clean picture) */
    ppm("03b-sequencer-step");
    {                                                     /* ALGORITHM on a note: MOVE it (all it owns), one undo */
        uint32_t n0 = TSEL->step[0].note[0], c1 = TSEL->step[1].note[1];
        TSEL->micro[0] = 5;
        step_fill_set(TSEL, 0, FC_FILL);
        lock_set(TSEL, 0, P_ED_FLT, -30);
        TSEL->step[2].time = ST_TIE;                      /* (the chord on step 2 lasts two steps) */
        cursor_set(0);
        turn(EN_ALGO, 1);
        check(ui.cursor == 2u && TSEL->step[2].note[0] == n0 && (TSEL->step[2].flags & SF_ACCENT) &&
              TSEL->step[0].note[1] == c1 && TSEL->step[1].time == ST_TIE, "ALGORITHM moves the note over the next one, ties and all");
        check(TSEL->micro[2] == 5 && TSEL->micro[0] == 0 && step_fill(TSEL, 2) == FC_FILL && lock_find(TSEL, 2, P_ED_FLT, 0) >= 0 &&
              lock_find(TSEL, 0, P_ED_FLT, 0) < 0, "... its nudge, condition and locks with it");
        check(!strcmp(ctl.toast, "MOVED TO STEP 3"), "... a toast says where");
        turn(EN_ALGO, -1);
        check(ui.cursor == 0u && TSEL->step[0].note[0] == n0 && TSEL->step[1].note[1] == c1 && TSEL->step[2].time == ST_TIE &&
              TSEL->micro[0] == 5, "back again");
        turn(EN_ALGO, 2);
        check(ui.cursor == 3u && TSEL->step[3].note[0] == n0 && TSEL->step[2].time == ST_REST, "further: over the rest that follows");
        turn(EN_ALGO, -20);
        check(ui.cursor == 0u && TSEL->step[0].note[0] == n0, "never past the first step");
        turn(EN_ALGO, 2);
        press(B_EDIT);
        press(B_OCTDN);
        release(B_OCTDN);
        release(B_EDIT);
        check(TSEL->step[0].note[0] == n0 && TSEL->step[1].note[1] == c1 && TSEL->micro[0] == 5 && TSEL->micro[3] == 0 &&
              step_fill(TSEL, 0) == FC_FILL && lock_find(TSEL, 0, P_ED_FLT, 0) >= 0, "EDIT + OCT-: the move undone, everything back");
        press(B_EDIT);
        press(B_OCTUP);
        release(B_OCTUP);
        release(B_EDIT);
        check(TSEL->step[3].note[0] == n0 && TSEL->micro[3] == 5 && lock_find(TSEL, 3, P_ED_FLT, 0) >= 0, "EDIT + OCT+: redone");
        press(B_EDIT);
        press(B_OCTDN);
        release(B_OCTDN);
        release(B_EDIT);
        lock_del(TSEL, 0, P_COUNT);
        TSEL->micro[0] = 0;
        step_fill_set(TSEL, 0, FC_NORM);
        TSEL->step[2].time = ST_REST;
        cursor_set(3);
        turn(EN_ALGO, -2);
        check(ui.cursor == 1u && TSEL->step[0].note[0] == n0, "on an empty step ALGORITHM moves the cursor");
    }
    tap(B_EDIT);
    check(!step_on(&TSEL->step[1]) && ui.cursor == 2u && ctl.screen == SC_SEQ, "EDIT clears the cursor's step and moves on");
    turn(EN_SELECT, -3);
    tap(B_SEQ);
    tap(B_SEQ);
    check(ctl.screen == SC_SEQ && !ctl_roll() && ctl.page[SC_SEQ] == 0u, "the drum track: its grid only");
    track_select(3);
    steps_clear(TSEL);

    /* SEQ held: the STEPS page over any screen; SEL held: the KEY page (the chords on the keys); a tap never shows them */
    tap(B_HOME);
    track_select(1);
    steps_clear(TSEL);
    {
        uint8_t n = 48;
        put_step(TSEL, 0, 1, &n, ST_NOTE, 0);
        put_step(TSEL, 4, 1, &n, ST_NOTE, 0);
        TSEL->step[5].time = ST_TIE;
        TSEL->step[4].flags |= SF_ACCENT;
        TSEL->seq_active = 1;
    }
    press(B_SEQ);
    check(mv.layer == MVL_NONE && ctl.screen == SC_PLAY, "SEQ just pressed: no STEPS page yet (a tap does not flash it)");
    frames(20);
    check(mv.layer == MVL_STEPS && !strcmp(mv.lay_lab[0], "C3") && (mv.lay_fl[5] & RF_TIE) && (mv.lay_fl[4] & RF_ACC) &&
          !strcmp(mv.k[1].label, "DIV") && mv.k[1].value[0] && mv.k[3].value[0],
          "SEQ held: STEPS, the 16 steps as a grid (note, tie, accent), DIV / SWING / LENGTH with their values");
    frames(100);
    ppm("16-steps-held");
    keyw(2);
    check(step_on(&TSEL->step[2]), "... SEQ + key 3: step 3 written as before");
    release(B_SEQ);
    frames(2);
    check(mv.layer == MVL_NONE && ctl.screen == SC_PLAY, "SEQ let go: back to the screen (no tap: it was used)");
    TSEL->p[P_CHORD] = 1;
    for (i = 0; i < NTRK; i++)
        if (is_synth(&trk[i]))
            trk[i].p[P_SCALE] = 2, trk[i].p[P_ROOT] = 0;   /* (C minor, for the picture) */
    {
        int16_t sc0 = key_ref()->p[P_SCALE];
        uint32_t ok = 1;
        press(B_SCL);
        frames(20);
        for (i = 0; i < 16u; i++)
            ok &= mv.lay_lab[i][0] && ds_text_w(&DS_F12, mv.lay_lab[i]) <= LT_W - 6;
        check(mv.layer == MVL_KEY && ok && !strcmp(mv.k[0].value, "TRIAD"),
              "SEL held: KEY, the chord of every white key (each label fits), CHORD TRIAD");
        turn(EN_K2, 2);
        check(key_ref()->p[P_SCALE] == clamp(sc0 + 2, 0, NSCALES - 1), "... KNOB 2 turns the scale (two detents: two scales)");
        turn(EN_K2, -2);
        frames(100);
        ppm("17-key-held");
        release(B_SCL);
        frames(2);
    }
    check(mv.layer == MVL_NONE && ctl.screen == SC_PLAY, "SEL let go: back to the screen");
    TSEL->p[P_CHORD] = 0;
    steps_clear(TSEL);
    track_select(0);
    press(B_SEQ);
    frames(120);
    check(mv.layer == MVL_STEPS && mv.lay_lab[0][0] == 0, "the drum track's STEPS: the lane's hits by level");
    ppm("16b-steps-drums");
    release(B_SEQ);
    frames(2);

    /* clips: CLIPS held + keys launch on the next bar, scenes, the cursor's copy and clear */
    track_select(1);
    {
        uint8_t n = 48;
        put_step(&trk[1], 4, 1, &n, ST_NOTE, 0);
        trk[1].seq_active = 1;
    }
    tap(B_SAVE);
    check(ctl.screen == SC_CLIPS && song.playing && clip_holds(1, 0) && clip_playing(1) == 0,
          "CLIPS: track 2's pattern is its clip 1, playing");
    press(B_SAVE);
    keyw(1);
    release(B_SAVE);
    check(ctl.screen == SC_CLIPS && clip_state(1, 1) == 3 && mv.t[1].queued == 1 && clip_playing(1) == 0,
          "CLIPS held + white key 2: clip 2 asked for (next bar), clip 1 still plays, no tap");
    ppm("07c-clips-queued");
    frames(140);
    check(clip_playing(1) == 1 && track_empty(&trk[1]) && mv.t[1].clip == 1, "... on the bar: clip 2 (empty) plays");
    {
        uint8_t n = 60;
        put_step(&trk[1], 0, 1, &n, ST_NOTE, 0);
        frames(3);
    }
    check(clip_holds(1, 1), "a step written: clip 2 holds it");
    turn(EN_ALGO, 2);
    tap(B_EDIT);
    check(ctl.row[SC_CLIPS] == 2 && clip_holds(1, 2) && !ctl.arm, "EDIT: the clip playing copied to the cursor's empty clip 3");
    turn(EN_ALGO, -2);
    tap(B_EDIT);
    check(ctl.arm == ACT_CLIPCOPY && mv.modal, "EDIT over a clip that holds a pattern asks first");
    tap(B_FX);
    check(!ctl.arm && ctl.screen == SC_CLIPS && clip_holds(1, 0), "... another button cancels (clip 1 kept)");
    turn(EN_K1, 2);
    check(ctl.row[SC_CLIPS] == 2, "KNOB 1 moves the clip cursor too");
    tap(B_REC);
    check(ctl.arm == ACT_CLIPCLEAR && !song.rec && !rec_wait, "REC on CLIPS asks before clearing the cursor's clip (no recording)");
    tap(B_REC);
    check(!clip_holds(1, 2) && clip_holds(1, 1), "... REC again clears clip 3 only");
    press(B_SAVE);
    keyw(8);
    release(B_SAVE);
    frames(140);
    for (i = 0, k = 1; i < NTRK; i++)
        k &= clip_playing(i) == 0;
    check(k && mv.scene == 0 && clip_holds(1, 0), "CLIPS held + white key 9: scene 1 on every track on the bar");
    frames(100);                                          /* (the toast and the knob focus gone: a clean picture) */
    ppm("07b-clips-live");
    tap(B_SAVE);
    check(clip_state(1, 2) == 3, "CLIPS tapped on CLIPS: the cursor's clip is asked for");
    tap(B_PLAY);
    frames(4);
    check(!song.playing && clip_playing(1) == 2, "... STOP before the bar: it starts at once");
    tap(B_PLAY);
    frames(10);

    /* SLOOP 2.6 on the FM1 Move panel, and the performance tempo */
    {
        uint32_t both = BT(B_OCTDN) | BT(B_OCTUP), n;
        int16_t bpm0;
        tap(B_HOME);
        frames(100);
        bpm0 = song.g[G_BPM];
        turn(EN_ALGO, 3);
        check(song.g[G_BPM] == bpm0 + 3 && mv.tempo_ov, "PLAY: ALGORITHM turns the TEMPO (no button), its readout large");
        ppm("18-tempo");
        frames(100);
        check(!mv.tempo_ov, "... the readout goes after a moment");
        for (n = 0; n < 4u; n++) {                        /* OCT- + OCT+ tapped together every 512 ms: ~117 BPM */
            edges_btn |= both;
            fm1_in.buttons |= both;
            frame();
            fm1_in.buttons &= ~both;
            frames(31);
        }
        check(song.g[G_BPM] >= 116 && song.g[G_BPM] <= 118 && song.octave == 0, "OCT- + OCT+ tapped in time: TAP TEMPO");
        song.g[G_BPM] = bpm0;
        press(B_FX);
        press(B_OCTDN);
        check(thr_req == 1u, "FX + OCT- held: REV THROW");
        press(B_OCTUP);
        check(thr_req == 3u, "... and OCT+: ECHO THROW too");
        release(B_OCTDN);
        release(B_OCTUP);
        release(B_FX);
        check(thr_req == 0u && ctl.screen != SC_PERFORM, "... let go: the sends back, FX did not open its screen");
        press(B_FX);
        {
            uint32_t kb = 1u;                             /* F#: the first black key, KILL LOW */
            fm1_in.notes |= 1u << kb;
            frames(6);
            check(punch.cur == 16 || punch.req == 16, "FX + the first black key: KILL LOW (27 punch-ins)");
            tap(B_HOME);
            fm1_in.notes &= ~(1u << kb);
            frames(6);
        }
        release(B_FX);
        frames(4);
        tap(B_FX);
        tap(B_FX);
        frames(2);
        check(ctl.screen == SC_PERFORM && ctl.page[SC_PERFORM] == 1u && mv.npages == 4u, "PERFORM page 2: the black keys' effects");
        frames(100);                                      /* (the TEMPO readout of the taps gone) */
        ppm("11d-perform-black");
        tap(B_FX);
        frames(2);
        check(!strcmp(mv.k[0].label, "TEXTURE") && !strcmp(mv.k[0].value, "OFF"), "PERFORM page 3: TEXTURE, OFF");
        turn(EN_K1, 1);
        check(song.g[G_TXSRC] == 1 && strcmp(mv.k[0].value, "OFF"), "... KNOB 1: a texture bed");
        frames(100);
        ppm("11c-texture");
        turn(EN_K1, -1);
        tap(B_FX);
        tap(B_FX);
        check(ctl.page[SC_PERFORM] == 0u, "... FX again: round to the white keys");
        track_select(1);
        tap(B_EDIT);
        while (ctl.page[SC_SOUND] != 5u)
            tap(B_EDIT);
        frames(2);
        check(!strcmp(mv.k[0].label, "INSERT") && !strcmp(mv.pgname, "INSERT"), "SOUND page 6: the track's INSERT");
        turn(EN_K1, 1);
        check(TSEL->p[P_ITYPE] == 1 && mv.k[1].label && mv.k[1].value[0], "... TYPE on: A B C named by the type");
        frames(100);
        ppm("09b-sound-insert");
        tap(B_EDIT);
        frames(2);
        check(!strcmp(mv.k[0].label, "INS MIX") && !strcmp(mv.k[1].label, "FLT TYPE"), "SOUND page 7: INSERT MIX, ANALOG's filter TYPE");
        TSEL->p[P_ITYPE] = 0;
        tap(B_EDIT);
        tap(B_EDIT);
        tap(B_EDIT);
        tap(B_EDIT);
        frames(2);
        check(ctl.page[SC_SOUND] == 3u && !strcmp(mv.k[0].label, "LFO SYNC") && !strcmp(mv.k[3].label, "ENV SYNC"),
              "SOUND page 4: LFO SYNC / TRIG / POL, ENV SYNC");
        {
            uint32_t k2, got = 0;
            for (k2 = 0; k2 < 5u; k2++) {
                trk[k2].p[P_ITYPE] = 1;
                got += ins_slot(&trk[k2]) != 0;
            }
            check(got == INS_SLOTS && ins_full, "INSERT: four tracks at once, a fifth stays dry and the panel says so");
            for (k2 = 0; k2 < 5u; k2++) {
                trk[k2].p[P_ITYPE] = 0;
                ins_of[k2] = 0;
            }
            frames(2);
            check(!ins_full, "... (the message shown)");
        }
        press(B_GLO);
        {
            uint32_t kc = 8u;                             /* C#: MIDI LEARN */
            fm1_in.notes |= 1u << kc;
            frames(2);
            fm1_in.notes &= ~(1u << kc);
            frames(2);
        }
        release(B_GLO);
        check(mlu.on, "GLO + C#: MIDI LEARN on");
        tap(B_EDIT);
        while (ctl.page[SC_SOUND] != 4u)
            tap(B_EDIT);
        turn(EN_K4, 1);                                   /* REVERB picked */
        check(ml_arm, "... a knob turned: its parameter waits for a CC");
        midi_learned(21, 64);
        frames(2);
        check(!ml_arm && ml_count() == 1u, "... CC 21 learned");
        track_select(2);
        midi_learned(21, 127);
        check(trk[2].p[P_REV] == TP[P_REV].max, "... CC 21 moves REVERB on the selected track");
        ml_toggle();
        ml_clear_all();
        track_select(3);
        tap(B_SEQ);
        if (ctl.page[SC_SEQ] != 1u)
            tap(B_SEQ);
        steps_clear(TSEL);
        cursor_set(0);
        keyw(0);
        cursor_set(0);
        turn(EN_K3, 2);
        check(note_len(TSEL, 0) == 3u && !strcmp(mv.k[2].label, "LEN"), "STEP page: KNOB 3 on a note sets its LEN (2.6)");
        steps_clear(TSEL);
        cursor_set(0);
        step_in_push(64u | 0x80u | song.sel << 8);        /* a MIDI keyboard on the selected track: E4 and G4 together */
        step_in_push(67u | 0x80u | song.sel << 8);
        frame();
        check(TSEL->step[0].n == 2 && ui.cursor == 0u, "STEP page from a MIDI keyboard: two notes held, a chord");
        step_in_push(64u | song.sel << 8);
        step_in_push(67u | song.sel << 8);
        frames(2);
        check(ui.cursor == 1u, "... both let go: the next step");
        steps_clear(TSEL);
        {
            uint32_t total, before;
            before = preset_pos(&total);
            pc_req[song.sel] = (uint8_t)((before + 5u) % total + 1u);
            frames(2);
            check(preset_pos(&total) == (before + 5u) % total, "MIDI Program Change: the n-th sound of PRESETS");
        }
        tap(B_HOME);
    }

    /* SCALE LOCK (as Ableton's scale mode): SEL held, KNOB 2 the scale, KNOB 3 LOCK; the keys light the scale */
    {
        uint32_t i2, ok = 1;
        int16_t sc0 = 0;
        track_select(1);
        for (i2 = 0; i2 < NTRK; i2++)
            if (is_synth(&trk[i2]))
                trk[i2].p[P_ROOT] = 0, trk[i2].p[P_SCALE] = 1, trk[i2].p[P_CHORD] = 0;   /* (C MAJ) */
        press(B_SCL);
        frames(20);
        turn(EN_K2, 1);                                   /* MAJ -> MIN */
        turn(EN_K3, 1);                                   /* KEYS: LOCK */
        frames(2);
        for (i2 = 0; i2 < NTRK; i2++)
            if (is_synth(&trk[i2]))
                ok &= trk[i2].p[P_QUANT] == 1 && trk[i2].p[P_SCALE] == 2;
        check(ok && !strcmp(mv.k[2].value, "LOCK") && strstr(mv.lay_ctx, "LOCK") != 0,
              "SEL held: KNOB 3 SCALE LOCK on every synth track (C MIN), the KEY page says LOCK");
        release(B_SCL);
        frames(4);
        {
            uint8_t q;
            uint32_t lit, dimv, k2;
            int good = 1;
            for (k2 = 7u; k2 <= 11u; k2++) {              /* C4 C# D Eb E: lit (root), off, dim, dim, off */
                static const int8_t WANT[5] = {2, 0, 1, 1, 0};
                q = ctl_led_pos[14u + k2];
                lit = (fm1_led[q >> 3] >> (q & 7u)) & 1u;
                dimv = (fm1_led_dim[q >> 3] >> (q & 7u)) & 1u;
                good &= WANT[k2 - 7u] == 2 ? (int)lit : WANT[k2 - 7u] == 1 ? (dimv && !lit) : (!lit && !dimv);
            }
            check(good, "... the keys: the root lit, the scale's notes glow, the others dark");
        }
        check((kb_map(TSEL, 8u) % 12u) == 0u && (kb_map(TSEL, 11u) % 12u) == 3u, "... a key out of the scale plays the scale's note below (C#->C, E->Eb)");
        tap(B_SEQ);
        if (ctl.page[SC_SEQ] != 1u)
            tap(B_SEQ);
        steps_clear(TSEL);
        {
            uint8_t n = 60, e = 64;
            put_step(TSEL, 0, 1, &n, ST_NOTE, 0);
            put_step(TSEL, 3, 1, &e, ST_NOTE, 0);
        }
        cursor_set(0);
        turn(EN_K2, 1);
        turn(EN_K2, 1);
        check(TSEL->step[0].note[0] == 63, "STEP page: NOTE moves through the scale (C -> D -> Eb)");
        check(mv.roll_lock && mv.roll_root == 0u && ((mv.roll_mask >> 3) & 1u) && !((mv.roll_mask >> 4) & 1u),
              "... the piano roll shades the rows out of the scale");
        frames(100);
        ppm("19-scale-lock-roll");
        press(B_SCL);
        press(B_OCTDN);
        release(B_OCTDN);
        release(B_SCL);
        check(TSEL->step[3].note[0] == 63, "SEL + OCT-: FIT TO SCALE moves E into C minor (Eb)");
        press(B_EDIT);
        press(B_OCTDN);
        release(B_OCTDN);
        release(B_EDIT);
        check(TSEL->step[3].note[0] == 64, "... EDIT + OCT- undoes it");
        cursor_set(1);
        step_in_push(61u | 0x80u | song.sel << 8);
        frame();
        step_in_push(61u | song.sel << 8);
        frames(2);
        check(TSEL->step[1].note[0] == 60, "... a MIDI keyboard's C# on the STEP page writes C");
        steps_clear(TSEL);
        press(B_SCL);
        frames(2);
        turn(EN_K3, -1);
        release(B_SCL);
        frames(2);
        check(!scale_lock(TSEL) && kb_map(TSEL, 8u) % 12u == 1u, "SCALE LOCK OFF: every key its own note again");
        (void)sc0;
        tap(B_HOME);
    }

    /* the battery (main.c: song.batt_raw), in the status bar and SETTINGS; empty: a toast, once */
    {
        int32_t raw0 = song.batt_raw;
        tap(B_HOME);
        frames(2);
        check(mv.batt == 3u && !mv.batt_usb, "battery: full, three bars in the status bar");
        song.batt_raw = 570;
        frames(2);
        check(mv.batt == 2u, "... 2 bars");
        song.batt_raw = 520;
        frames(2);
        check(mv.batt == 0u && !strcmp(ctl.toast, "BATTERY LOW"), "... empty: no bars, a BATTERY LOW toast");
        frames(200);
        song.batt_raw = 535;
        frames(2);
        song.batt_raw = 520;
        frames(2);
        check(!mv.toast_t, "... said once (again only after a charge)");
        ppm("15-battery-low");
        ctl.menu = 1;
        ctl.screen = SC_SETTINGS;
        ctl.row[SC_SETTINGS] = ST_BATT;
        frames(2);
        {
            uint32_t r, hit = 0;
            for (r = 0; r < mv.nrows; r++)
                hit |= !strcmp(mv.row_l[r], "BATTERY") && !strcmp(mv.row_r[r], "EMPTY");
            check(ctl.menu && hit, "SETTINGS: BATTERY says EMPTY");
        }
        felucca_dbg.max_us = (HALF_FRAMES * 1000000u / FS) / 2u;   /* (a peak at half the render's time, 3 late halves) */
        felucca_dbg.late = 3;
        ctl.row[SC_SETTINGS] = ST_AUDIO;
        frames(2);
        {
            uint32_t r, hit = 0;
            for (r = 0; r < mv.nrows; r++)
                hit |= !strcmp(mv.row_l[r], "AUDIO LOAD") && !strcmp(mv.row_r[r], "50% LATE 3");
            check(hit, "SETTINGS: AUDIO LOAD shows the peak render load and the late buffers (BUG-02 reports)");
        }
        ppm("13b-settings-audio");
        felucca_dbg.max_us = 0;
        felucca_dbg.late = 0;
        usb.config = 1;
        usb.suspended = 0;
        frames(2);
        check(mv.batt_usb && mv.batt >= 1u && mv.batt <= 3u, "on USB: the bars fill in turn (USB powers it)");
        usb.config = 0;
        song.batt_raw = 0;
        frames(2);
        check(mv.batt == MV_BATT_NONE, "not read yet: no battery shown");
        song.batt_raw = raw0;
        ctl.row[SC_SETTINGS] = 0;
        ctl.menu = 0;
        ctl.screen = SC_PLAY;
        frames(100);
    }

    /* BUG-01: the UI never makes another track the selected one, not even for a moment (the audio ISR routes the
     * keys, MIDI and auditions by song.sel); a heavy preset refused (all arenas in use) loads nothing else; the
     * PRESETS browser names the entries next to the sound by their list position */
    {
        uint32_t i, n, total, pos, sel0 = song.sel, bad = 0, tried = 0, heavy[3] = {ENGI_PHYS, ENGI_FM6, ENGI_GRAIN};
        check(sel_glitch == 0u, "BUG-01: no UI frame so far set song.sel to another track (keys stay on the selected one)");
        tap(B_HOME);
        frames(2);
        for (i = 0; i < 3u; i++)                          /* tracks 2, 4, 5 take the three heavy-engine arenas */
            set_engine_of(&trk[i == 0 ? 1u : i + 2u], heavy[i]);
        track_select(2);
        frames(2);
        {
            uint32_t e0 = TSEL->eng_req, p0 = TSEL->preset, k;
            for (n = 0; n < NBANK; n++) {
                if (!eng_heavy(BANK[n].e))
                    continue;
                tried++;
                preset_go(n);
                bad += TSEL->eng_req != e0 || TSEL->preset != p0;
            }
            check(tried > 0u && !bad, "BUG-01: a heavy preset refused (3 arenas in use) leaves the sound as it was");
            check(!strncmp(ui.msg, "HEAVY ENGINES", 13), "... and the panel says HEAVY ENGINES ALL IN USE");
            check(preset_go_to(TSEL, 0) == 0 && preset_at(0, &k) == TSEL->eng_req, "... a light preset still loads");
        }
        for (i = 0; i < 3u; i++)
            set_engine_of(&trk[i == 0 ? 1u : i + 2u], 0);
        tap(B_EDIT);                                      /* SOUND, PRESET page */
        frames(2);
        pos = preset_pos(&total);
        check(!strcmp(mv.around[0], BANK[(pos + total - 1u) % total].name) && !strcmp(mv.around[1], BANK[(pos + 1u) % total].name),
              "PRESETS browser: the previous and next names are the list's neighbours");
        tap(B_HOME);
        track_select(sel0);
        frames(2);
    }

    /* UX-02: SAVE opens CLIPS on the selected track's clip playing, so SAVE, REC, REC clears that track (asks first) */
    tap(B_HOME);
    ctl.row[SC_CLIPS] = 5;
    tap(B_SAVE);
    frames(2);
    check(ctl.screen == SC_CLIPS && ctl.row[SC_CLIPS] == clip_playing(song.sel), "UX-02: CLIPS opens on the selected track's clip playing");
    tap(B_HOME);
    frames(2);

    /* FEAT-01: a step's filter lock (SEQ + step held, ALGORITHM the parameter, PRESETS the value) plays on that step
     * only, while the loop plays: the track filter (every kind) and the sound's filter page are lockable */
    {
        track_t *t = &trk[1];
        uint32_t f, on2 = 0, off_else = 1, base;
        uint8_t n = 48;
        check(p_lockable(P_TFLT) && p_lockable(P_ED_FLT) && p_lockable(P_LD_FLT), "FEAT-01: the filters are lock targets");
        track_select(1);
        steps_clear(t);
        put_step(t, 0, 1, &n, ST_NOTE, 0);
        put_step(t, 2, 1, &n, ST_NOTE, 0);
        put_step(t, 4, 1, &n, ST_NOTE, 0);
        t->p[P_SLEN] = 8;
        base = (uint32_t)(t->p[P_TFLT] + 64);
        fm1_irq_off();
        lock_set(t, 2, P_TFLT, -40);
        fm1_irq_on();
        if (song.playing) {
            tap(B_PLAY);
            frames(2);
        }
        tap(B_PLAY);
        for (f = 0; f < 200u; f++) {
            frame();
            if (t->seq_idx == 2u)
                on2 |= t->p[P_TFLT] == -40;
            else if (t->seq_idx == 4u || t->seq_idx == 5u)
                off_else &= (uint32_t)(t->p[P_TFLT] + 64) == base;
        }
        tap(B_PLAY);
        frames(2);
        check(on2 && off_else && !song.playing, "FEAT-01: a filter lock on step 3 plays there, the filter comes back after it");
        lock_del(t, 2, P_COUNT);
        steps_clear(t);
        t->p[P_SLEN] = 16;
    }

    /* fuzz: random use */
    {
        uint32_t seed = 12345u, f, over = 0;
        static const uint8_t BTNS[] = {B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY,
                                       B_REC, B_OCTDN, B_OCTUP};
        for (f = 0; f < 12000u; f++) {
            uint32_t r;
            seed = seed * 1103515245u + 12345u;
            r = (seed >> 8) % 100u;
            if (r < 6u) {
                uint32_t b = BTNS[(seed >> 16) % sizeof BTNS];
                if (b == B_HOME && ((seed >> 4) & 7u) == 0u) {
                    hold_ms(b, 800);
                } else if (fm1_in.buttons & BT(b)) {
                    fm1_in.buttons &= ~BT(b);
                } else {
                    edges_btn |= BT(b);
                    fm1_in.buttons |= BT(b);
                }
            } else if (r < 16u) {
                fm1_in.notes ^= 1u << ((seed >> 12) % 27u);
            } else if (r < 30u) {
                encs[(seed >> 14) % 7u] += (int32_t)((seed >> 20) % 7u) - 3;
            }
            if ((f & 255u) == 0u)
                fm1_in.buttons = 0, fm1_in.notes = 0;
            frame();
            over += busy_now() > NVOICE;
        }
        fm1_in.buttons = fm1_in.notes = 0;
        frames(20);
        check(!over, "12000 frames of random use: no crash, every blit on the screen, voice budget kept");
        check(sel_glitch == 0u, "BUG-01: in all of it, no frame set song.sel to another track");
    }
    printf("fm1ui: frames rendered %u, blits %u\n", ds_frames, blits);
    printf(fails ? "fm1ui: %d FAILED\n" : "fm1ui: all checks ok\n", fails);
    return fails;
}
