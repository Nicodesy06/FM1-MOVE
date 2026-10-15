/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 Move UI audit: every screen and page, a synth and a drum track, no button or each button held (and SEQ + a step
 * key held), every knob and encoder and, with a button held, every key. The real controls, layers and screens (the
 * virtual FM1 of fm1move_ui_test.c) look for two kinds of fault:
 *   unlabelled  a knob cell given a value but no label: the screen draws nothing there, so what the knob edits is
 *               never said (SEQ held + KNOB 1 was one)
 *   silent      an action changed what is set (a parameter, a step, a setting, the selection...) and the screen did
 *               not change at all: no value, no toast, no overlay said so
 * Each fault is printed with its context; the exit status is their number. Host simulation only. */
#define FM1UI_NO_MAIN
#include <stdio.h>
static void clip_log(const char *s, int w);
#define DS_CLIP_LOG(s, w) clip_log(s, w)
#include "fm1move_ui_test.c"

static char clip_seen[64][24];
static uint32_t clip_n;
static void clip_log(const char *s, int w)       /* each label that did not fit, once */
{
    uint32_t i;
    char b[24];
    snprintf(b, sizeof b, "%s/%d", s, w);
    for (i = 0; i < clip_n; i++)
        if (!strcmp(clip_seen[i], b))
            return;
    if (clip_n < 64u) {
        strcpy(clip_seen[clip_n++], b);
        printf("audit: label clipped: \"%s\" in %d px\n", s, w);
    }
}
static uint32_t h32;
static void hb(const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--)
        h32 = (h32 ^ *b++) * 16777619u;
}
/* what is set (not what plays): the tracks' parameters, patterns, grooves, locks; the song's; the selection, the
 * cursor, the pens, the screen and its rows; the settings */
static int keying;                                   /* a key action: the pens follow what is played (no fault) */
#define NCOMP 12
static const char *const COMP_N[NCOMP] = {"track params", "pattern", "nudge/timing", "locks/fill", "groove", "sound",
                                          "song", "pens", "cursor/page", "screen/rows", "settings", "mutes"};
static uint32_t comp[NCOMP];
static uint32_t hdone(void) { uint32_t r = h32; h32 = 2166136261u; return r; }
static uint32_t state_hash(void)
{
    uint32_t i, w = lights_word(), all = 0;
    h32 = 2166136261u;
    for (i = 0; i < NTRK; i++) hb(trk[i].p, sizeof trk[i].p);
    comp[0] = hdone();
    for (i = 0; i < NTRK; i++) hb(trk[i].step, sizeof trk[i].step);
    comp[1] = hdone();
    for (i = 0; i < NTRK; i++) { hb(trk[i].micro, sizeof trk[i].micro); hb(trk[i].roff, sizeof trk[i].roff); hb(trk[i].pad_off, sizeof trk[i].pad_off); }
    comp[2] = hdone();
    for (i = 0; i < NTRK; i++) { hb(trk[i].lock, sizeof trk[i].lock); hb(trk[i].fill, sizeof trk[i].fill); }
    comp[3] = hdone();
    for (i = 0; i < NTRK; i++) hb(&trk[i].gv, sizeof trk[i].gv);
    hb(&song_groove, sizeof song_groove);
    comp[4] = hdone();
    for (i = 0; i < NTRK; i++) { hb(&trk[i].eng_req, 1); hb(&trk[i].preset, 1); hb(&trk[i].user, 1); hb(&trk[i].kind, 1); }
    comp[5] = hdone();
    hb(song.g, sizeof song.g); hb(&song.sel, 1); hb(&song.octave, 1); hb((const void *)&song.solo, 1);
    comp[6] = hdone();
    if (!keying) { hb(&pen_lane, 1); hb(&pen_n, 1); hb(pen_note, sizeof pen_note); }
    comp[7] = hdone();
    hb(&ui.cursor, sizeof ui.cursor); hb(&ui.step_page, sizeof ui.step_page); hb(&ui.lock_par, sizeof ui.lock_par);
    comp[8] = hdone();
    hb(&ctl.screen, 1); hb(ctl.page, sizeof ctl.page); hb(ctl.row, sizeof ctl.row); hb(&ctl.menu, 1);
    comp[9] = hdone();
    hb(&w, 4);
    comp[10] = hdone();
    hb((const void *)&drum_mute, sizeof drum_mute);
    comp[11] = hdone();
    for (i = 0; i < NCOMP; i++)
        all = all * 31u + comp[i];
    return all;
}

static uint16_t snap[240 * 240];
static uint32_t faults, actions, unlabelled_seen;
static char ctxs[96];
static int unlabelled(char *why, uint32_t n)
{
    uint32_t c;
    for (c = 0; c < 4u; c++)
        if (!mv.k[c].label && mv.k[c].value[0]) {
            snprintf(why, n, "KNOB %u shows \"%s\" with no label (not drawn)", c + 1u, mv.k[c].value);
            return 1;
        } else if (mv.k[c].label && !mv.k[c].value[0]) {
            snprintf(why, n, "KNOB %u says %s but shows no value", c + 1u, mv.k[c].label);
            return 1;
        }
    return 0;
}
static const char *shots;                            /* AUDIT_SHOTS=dir: before / after pictures of each fault */
static void fault(const char *what, const char *detail)
{
    faults++;
    if (shots) {
        char n[512];
        FILE *f;
        snprintf(n, sizeof n, "%s/fault%03u.raw", shots, faults);
        if ((f = fopen(n, "wb")) != 0) {
            fwrite(snap, 1, sizeof snap, f);
            fwrite(screen, 1, sizeof screen, f);
            fclose(f);
        }
    }
    printf("audit: %-44s %-26s %s\n", ctxs, what, detail);
}
static void do_key(int32_t k);
/* one action, while what holds it is held: look before and after */
static void act(const char *name, void (*fn)(int32_t), int32_t arg)
{
    uint32_t h0, h1, c0[NCOMP], i;
    char why[160];
    frames(2);
    keying = fn == do_key;
    h0 = state_hash();
    memcpy(c0, comp, sizeof c0);
    memcpy(snap, screen, sizeof snap);
    fn(arg);
    frames(2);
    actions++;
    h1 = state_hash();
    keying = 0;
    if (unlabelled(why, sizeof why)) {
        fault(name, why);
        unlabelled_seen++;
    } else if (h1 != h0 && !memcmp(snap, screen, sizeof snap)) {
        char *e = why + snprintf(why, sizeof why, "SILENT:");
        for (i = 0; i < NCOMP; i++)
            if (c0[i] != comp[i])
                e += snprintf(e, (size_t)(why + sizeof why - e), " %s", COMP_N[i]);
        fault(name, why);
    }
}
static void do_enc(int32_t a) { turn((uint32_t)(a >> 4), (a & 15) == 1 ? 1 : -1); }
static void do_key(int32_t k)
{
    keying = 1;
    fm1_in.notes |= 1u << k;
    frame();
    fm1_in.notes &= ~(1u << (uint32_t)k);
    frame();
}

static const char *const ENC_N[NE] = {"SELECT", "ALGORITHM", "PRESETS", "KNOB 1", "KNOB 2", "KNOB 3", "KNOB 4"};
static const char *const BTN_N[] = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC",
                                    "OCT-", "OCT+"};
static const char *const SC_N[SC_COUNT] = {"PLAY", "TRACKS", "SEQUENCER", "GROOVE", "CLIPS", "MIXER", "SOUND", "PERFORM",
                                           "PROJECT", "SETTINGS"};

/* every encoder both ways, then (a button held) every key */
static void sweep(int with_keys)
{
    uint32_t e, k;
    char nm[32];
    for (e = 0; e < NE; e++) {
        if (e == EN_SELECT && !fm1_in.buttons)
            continue;                                /* (SELECT alone changes the track) */
        snprintf(nm, sizeof nm, "%s +", ENC_N[e]);
        act(nm, do_enc, (int32_t)(e << 4 | 1));
        snprintf(nm, sizeof nm, "%s -", ENC_N[e]);
        act(nm, do_enc, (int32_t)(e << 4 | 2));
    }
    if (with_keys)
        for (k = 0; k < 27u; k++) {
            frames(100);                             /* (the TEMPO readout of SELECT gone: it covers the screen) */
            snprintf(nm, sizeof nm, "key %u", k);
            act(nm, do_key, (int32_t)k);
        }
}

static uint32_t sel_home;                            /* the track of this pass (SEL + a key selects another) */
static void settle(void)
{
    if (song.sel != sel_home)
        track_select(sel_home);
    fm1_in.buttons = 0;
    fm1_in.notes = 0;
    frames(120);                                     /* (toasts and overlays gone) */
    if (song.playing) {
        transport_req = 2;
        frames(4);
    }
    song.rec = 0;
    rec_wait = 0;
    rec_pend = 0;
    ctl.arm = 0;
    frames(4);
}

int main(int argc, char **argv)
{
    static const uint8_t TRK_KIND[2] = {1, 0};      /* track 2 synth, track 1 drums */
    uint32_t kind, sc, pg, b;
    (void)argc;
    (void)argv;
    setvbuf(stdout, 0, _IONBF, 0);                   /* (hostsim forks: no line printed twice) */
    outdir = "/tmp";
    shots = getenv("AUDIT_SHOTS");
    memset(nor, 0xFF, sizeof nor);
    panel = PANEL_DEFAULT;
    host_tracks_init();
    song.g[G_BPM] = 122;
    song.batt_raw = 600;
    layers_init();
    project_default_tracks();
    frames(4);
    for (kind = 0; kind < 2u; kind++) {
        uint8_t n = 48;
        track_select(TRK_KIND[kind]);
        sel_home = TRK_KIND[kind];
        if (!is_drum(TSEL))
            put_step(TSEL, 0, 1, &n, ST_NOTE, 0);
        else
            dstep_set(&TSEL->dstep[0], 0, LV_NORM, 0);
        /* every screen and page, nothing held: the encoders */
        for (sc = 0; sc < SC_COUNT; sc++) {
            static const uint8_t NP[SC_COUNT] = {1, 1, 2, 2, 1, 1, 7, 4, 1, 1};
            uint32_t np = sc == SC_SOUND && is_drum(TSEL) ? 3u : NP[sc];
            for (pg = 0; pg < np; pg++) {
                settle();
                ctl.menu = (uint8_t)(sc == SC_PROJECT || sc == SC_SETTINGS);
                ctl.screen = (uint8_t)sc;
                ctl.page[sc] = (uint8_t)pg;
                song.seq_mode = sc == SC_SEQ;
                frames(4);
                snprintf(ctxs, sizeof ctxs, "%s %s p%u", is_drum(TSEL) ? "drums" : "synth", SC_N[sc], pg + 1u);
                sweep(0);
            }
        }
        /* a button held on every screen (page 1): the encoders; on PLAY every key too; SEQ + a step key held too */
        for (sc = 0; sc < SC_PROJECT; sc++)
            for (b = 0; b < NB; b++) {
                if (b == B_PLAY || b == B_REC)
                    continue;                        /* (the transport: its own tests) */
                settle();
                ctl.menu = 0;
                ctl.screen = (uint8_t)sc;
                ctl.page[sc] = 0;
                song.seq_mode = sc == SC_SEQ;
                frames(2);
                snprintf(ctxs, sizeof ctxs, "%s %s, %s held", is_drum(TSEL) ? "drums" : "synth", SC_N[sc], BTN_N[b]);
                fm1_in.buttons |= BT(b);
                edges_btn |= BT(b);
                frames(20);                          /* (a layer page shows after 0.25 s) */
                sweep(sc == SC_PLAY);
                fm1_in.buttons &= ~BT(b);
                frames(2);
            }
        settle();
        ctl.screen = SC_PLAY;
        snprintf(ctxs, sizeof ctxs, "%s PLAY, SEQ + step 1 held", is_drum(TSEL) ? "drums" : "synth");
        fm1_in.buttons |= BT(B_SEQ);
        edges_btn |= BT(B_SEQ);
        frames(20);
        fm1_in.notes |= 1u << key_of_white(0);
        frames(3);
        sweep(0);
        fm1_in.notes = 0;
        fm1_in.buttons = 0;
        frames(2);
    }
    if (ds_clipped) {
        faults++;
        printf("audit: %u labels did not fit (clipped)\n", ds_clipped);
    }
    printf("audit: %u actions, %u faults (%u knob cells without a label or a value)\n", actions, faults, unlabelled_seen);
    return (int)faults;
}
