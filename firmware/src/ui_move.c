/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 Move screens. Each screen is a scene for the band renderer (ds.c) drawn only from the design-system
 * components, from one plain-data view model (mv). The firmware fills mv from the live state (the adapter,
 * ui_move_live.c); the host fills it with demo data and renders the very same code into PNG mockups
 * (tests/ui_mockups.c). Layout grid and tokens: docs/UI_DESIGN_SYSTEM.md.
 *
 * Screen zones (every screen):
 *   STATUS  y   0.. 23  track chip + name | Q G REC transport BPM
 *   TITLE   y  28.. 40  screen name | context (page dots, pattern, hints)
 *   BODY    y  44..175  the screen's content; a turned knob takes it over (the focus overlay)
 *   KNOBS   y 180..239  KNOB 1..4: label, value, gauge, in the order of the knobs on the panel
 */

enum { SC_PLAY, SC_TRACKS, SC_SEQ, SC_GROOVE, SC_CLIPS, SC_MIXER, SC_SOUND, SC_PERFORM, SC_PROJECT, SC_SETTINGS,
       SC_COUNT };
static const char *const SC_NAME[SC_COUNT] = {"PLAY", "TRACKS", "SEQUENCER", "GROOVE", "CLIPS", "MIXER", "SOUND",
                                              "PERFORM", "PROJECT", "SETTINGS"};
enum { MK_SYNTH, MK_DRUM, MK_MIDI };
enum { LVK_OFF, LVK_GHOST, LVK_SOFT, LVK_NORM, LVK_HARD };   /* a step's level as drawn */
enum { RF_ACC = 1, RF_SLIDE = 2, RF_TIE = 4 };                 /* a STEP page step's marks */

#define MV_NTRK 8
#define MV_NSTEP 16                            /* steps on screen: one page */
#define MV_ROWS 6                              /* list rows (PROJECT, SETTINGS, presets) */

typedef struct {
    char name[10];                             /* "BASS" */
    char sound[16];                            /* the preset */
    char engine[10];                           /* "ANALOG", "808 KIT", "CH 5" */
    uint8_t kind;                              /* MK_* */
    uint8_t mute, solo, arm;
    uint8_t act;                               /* activity 0..100 (meters) */
    uint8_t vol;                               /* 0..100 */
    int8_t pan;                                /* -50..50 */
    uint8_t clip;                              /* playing clip 0..7, 0xFF none */
    uint8_t queued;                            /* clip queued to launch, 0xFF none */
    uint8_t clips;                             /* bit per clip slot holding a pattern */
} mv_track_t;

typedef struct {
    const char *label;
    char value[12];
    int16_t ratio;                             /* 0..1000, -1 = no gauge */
    uint8_t bipolar;
} mv_knob_t;

typedef struct {
    uint8_t screen, page, npages;
    uint8_t sel;                               /* selected track 0..7 */
    mv_track_t t[MV_NTRK];
    /* transport and global state */
    uint8_t playing, rec, count_in;            /* count_in: beats left, 0 = none */
    uint16_t bpm10;
    uint8_t quant_on, groove_on;
    uint8_t bar, beat, bars;                   /* position (1-based), loop length in bars */
    uint8_t step16;                            /* the playing step inside the bar 0..15 */
    char key[10];                              /* "C MIN" */
    /* the four knobs */
    mv_knob_t k[4];
    int8_t hot;                                /* the knob being turned (focus overlay), -1 none */
    uint8_t nofocus;                           /* the screen stays while a knob turns (what it edits is drawn) */
    char hot_unit[6];
    /* SEQUENCER */
    uint8_t len, div_label_i, spage;           /* length, the page shown (16 steps) */
    char div[6];
    uint8_t st_lvl[MV_NSTEP];                  /* LVK_* */
    uint8_t st_lock[MV_NSTEP], st_micro[MV_NSTEP];
    uint8_t st_note[MV_NSTEP];                 /* synth: the step's (first) note, 0 = none */
    uint8_t st_gate[MV_NSTEP];                 /* synth: the note's length in steps (1..) */
    uint16_t lanes[MV_NSTEP];                  /* drums: lanes on, bit per lane */
    uint8_t lane;                              /* drums: the lane edited */
    char lane_name[10];
    int8_t held;                               /* the step held (SEQ + key), -1 none */
    /* SEQUENCER, STEP page (the cursor's 16 steps) */
    uint8_t cur;                               /* the cursor in the page 0..15 */
    uint8_t cur_abs;                           /* the cursor in the pattern */
    uint8_t roll_n[MV_NSTEP];                  /* notes in the step (a chord: up to 4) */
    uint8_t roll_nt[MV_NSTEP][4];
    uint8_t roll_fl[MV_NSTEP];                 /* RF_* */
    uint8_t roll_lo, roll_hi;                  /* the note range drawn (the whole pattern's) */
    /* GROOVE */
    char tmpl[12];
    uint8_t swing, quant, gamount, human;      /* percent */
    uint8_t gfocus;                            /* 0 swing .. 3 humanise: the hero value */
    int8_t g_off[8];                           /* the event offsets of 8 sixteenths, in 1/32 of a step (-16..16) */
    uint8_t g_vel[8];                          /* their velocities 0..127 */
    /* CLIPS */
    uint8_t scene;                             /* the scene playing (every track on clip k), 0xFF none */
    uint8_t blink;                             /* the blink phase (a clip asked for)  */
    /* SOUND */
    char cat[10];
    uint8_t preset_i, preset_n;
    char around[2][16];                        /* the previous and next preset names (browser) */
    uint8_t env[4];                            /* A D S R 0..100 */
    /* PERFORM */
    const char *fx[16];
    uint16_t fx_on;                            /* bit per effect held */
    /* PROJECT / SETTINGS lists */
    char row_l[MV_ROWS][16], row_r[MV_ROWS][16];
    uint8_t nrows, row_sel, row_cur;           /* rows, the selected row, the loaded one (PROJECT) */
    /* modal / toast */
    uint8_t modal;                             /* 1 = a destructive action waits for confirmation */
    char modal_t[24], modal_b[32], modal_ok[16];
    char toast[24];
    uint8_t toast_t;
} mv_t;
static mv_t mv;

#define MV_TC(i) DS_TRK[(i) & 7u]
#define MV_SELC MV_TC(mv.sel)
/* layout */
#define Y_STATUS 0
#define Y_TITLE 38                             /* title baseline */
#define Y_BODY 44
#define Y_BODY_END 176
#define Y_KNOBS 180
#define MX 8                                   /* side margin */

/* ------------------------------------------------------------ shared parts --- */
static void mv_bpm(char *b)
{
    char *e = ds_itoa(b, mv.bpm10 / 10u);
    if (mv.bpm10 % 10u) {
        *e++ = '.';
        ds_itoa(e, mv.bpm10 % 10u);
    }
}

/* two-state badge: on = filled, off = outline (state by shape, not only colour) */
static void mv_badge(int32_t x, int32_t y, const char *s, int on)
{
    if (on)
        ds_chip(x, y, 15, 15, DS_TEXT1, DS_BG, &DS_F12, s);
    else
        ds_chip_line(x, y, 15, 15, DS_TEXT3, DS_BG, &DS_F12, s);
}

static void mv_status(void)
{
    const mv_track_t *t = &mv.t[mv.sel];
    char b[8];
    int32_t x = 232;
    ds_itoa(b, mv.sel + 1);
    ds_chip(MX, 4, 22, 16, MV_SELC, DS_BG, &DS_F12, b);
    ds_label(36, 17, 80, t->name, DS_TEXT1);
    /* right to left: BPM, transport, REC, G, Q */
    mv_bpm(b);
    x -= ds_text_w(&DS_F12, b);
    ds_text(&DS_F12, x, 17, b, DS_TEXT1);
    x -= 18;
    if (mv.count_in) {
        char c[2] = {(char)('0' + mv.count_in % 10u), 0};
        ds_chip(x - 2, 4, 16, 16, DS_WARN, DS_BG, &DS_F12, c);
    } else if (mv.playing) {
        ds_icon(IC_PLAY, x, 6, DS_PLAY, DS_BG);
    } else {
        ds_icon(IC_STOP, x, 6, DS_TEXT2, DS_BG);
    }
    x -= 20;
    if (mv.rec)
        ds_icon(IC_REC, x, 6, DS_REC, DS_BG);
    else if (t->arm)
        ds_icon(IC_ARM, x, 6, DS_REC, DS_BG);
    else
        ds_icon(IC_ARM, x, 6, DS_SURF2, DS_BG);
    x -= 21;
    mv_badge(x, 5, "G", mv.groove_on);
    x -= 19;
    mv_badge(x, 5, "Q", mv.quant_on);
}

/* page dots: the page shown filled, the others as rings */
static void mv_dots(int32_t xr, int32_t y, uint32_t n, uint32_t cur)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        int32_t x = xr - (int32_t)(n - i) * 10 + 2;
        if (i == cur)
            ds_disc(x, y, 6, DS_TEXT1);
        else
            ds_ring(x, y, 6, 1, DS_TEXT3);
    }
}

static void mv_title(const char *ctx)
{
    ds_label(MX, Y_TITLE, 110, SC_NAME[mv.screen % SC_COUNT], DS_TEXT2);
    if (mv.npages > 1) {
        mv_dots(232, Y_TITLE - 7, mv.npages, mv.page);
        if (ctx)
            ds_text_in(&DS_F12, 0, Y_TITLE, 232 - (int32_t)mv.npages * 10 - 6, DS_R, ctx, DS_TEXT3);
    } else if (ctx) {
        ds_text_in(&DS_F12, 0, Y_TITLE, 232, DS_R, ctx, DS_TEXT3);
    }
}

/* the knob strip: four cells under the four knobs; the one turned is inverted */
static void mv_knobs(void)
{
    uint32_t c;
    ds_hline(0, Y_KNOBS, DS_W, DS_LINE);
    for (c = 0; c < 4u; c++) {
        const mv_knob_t *k = &mv.k[c];
        int32_t x = (int32_t)c * 60, hot = mv.hot == (int8_t)c;
        uint16_t fg = hot ? DS_BG : DS_TEXT1, lab = hot ? DS_BG : DS_TEXT2;
        if (!k->label)
            continue;
        if (hot)
            ds_rrect(x + 2, Y_KNOBS + 4, 56, 54, 4, DS_TEXT1);
        else if (c)
            ds_vline(x, Y_KNOBS + 10, 40, DS_SURF2);
        ds_label(x + 6, Y_KNOBS + 17, 52, k->label, lab);
        ds_text_clip(ds_fit(k->value, 49, &DS_F22, &DS_F16), x + 7, Y_KNOBS + 41, 50, k->value, fg);
        ds_gauge(x + 7, Y_KNOBS + 48, 46, 3, k->ratio, k->bipolar, hot ? DS_BG : MV_SELC,
                 hot ? ds_mix(DS_TEXT1, DS_BG, 4) : DS_SURF2);
    }
}

/* the focus overlay: the value being turned, alone and large, over the body */
static void mv_focus(void)
{
    const mv_knob_t *k = &mv.k[mv.hot & 3];
    const ds_font_t *f = ds_text_w(&DS_F64, k->value) <= 200 && k->value[0] >= '-' && k->value[0] <= '9' &&
                                 k->value[0] != '/'
                             ? &DS_F64
                             : &DS_F36;
    int32_t w = ds_text_w(f, k->value), uw = mv.hot_unit[0] ? ds_text_w(&DS_F22, mv.hot_unit) + 4 : 0;
    int32_t x = (DS_W - w - uw) / 2;
    {
        int32_t lw;
        ds_track = 1;
        lw = ds_text_w(&DS_F12, k->label);
        ds_text(&DS_F12, (DS_W - lw) / 2, 66, k->label, DS_TEXT2);
        ds_track = 0;
    }
    ds_text(f, x, 132, k->value, DS_TEXT1);
    if (uw)
        ds_text(&DS_F22, x + w + 4, 132, mv.hot_unit, DS_TEXT2);
    ds_gauge(20, 150, 200, 4, k->ratio, k->bipolar, MV_SELC, DS_SURF2);
}

/* ----------------------------------------------------------------- screens --- */
/* PLAY: the selected track and its sound, the bar as a clock of 16 steps, the eight tracks' activity */
static void sc_play(void)
{
    const mv_track_t *t = &mv.t[mv.sel];
    static const int16_t S16[16] = {0, 383, 707, 924, 1000, 924, 707, 383, 0, -383, -707, -924, -1000, -924, -707, -383};
    char b[16], *e;
    uint32_t i;
    e = ds_cat(b, "BAR ");
    e = ds_itoa(e, mv.bar);
    e = ds_cat(e, ".");
    ds_itoa(e, mv.beat);
    mv_title(b);
    ds_text(ds_fit(t->name, 150, &DS_F36, &DS_F22), MX, 84, t->name, DS_TEXT1);
    ds_text(&DS_F16, MX, 108, t->sound, MV_SELC);
    ds_icon(t->kind == MK_DRUM ? IC_DRUM : t->kind == MK_MIDI ? IC_MIDI : IC_SYNTH, MX, 116, DS_TEXT3, DS_BG);
    ds_text(&DS_F12, MX + 18, 127, t->engine, DS_TEXT3);
    /* the step clock: 16 dots round the bar number */
    for (i = 0; i < 16u; i++) {
        int32_t cx = 192 + S16[i] * 34 / 1000, cy = 86 - S16[(i + 12u) % 16u] * 34 / 1000;
        int32_t on = mv.playing && i == mv.step16, past = mv.playing && i < mv.step16;
        int32_t d = on ? 10 : i % 4u ? 4 : 6;
        ds_disc(cx - d / 2, cy - d / 2, d, on ? DS_TEXT1 : past ? MV_SELC : i % 4u ? DS_SURF2 : DS_TEXT3);
    }
    ds_itoa(b, mv.bar);
    ds_text_in(&DS_F22, 152, 94, 80, DS_C, b, DS_TEXT1);
    e = ds_cat(b, "OF ");
    ds_itoa(e, mv.bars);
    ds_text_in(&DS_F12, 152, 108, 80, DS_C, b, DS_TEXT3);
    /* the eight tracks: activity, mute / solo, the selected one underlined */
    for (i = 0; i < MV_NTRK; i++) {
        const mv_track_t *u = &mv.t[i];
        int32_t x = MX + (int32_t)i * 28, h = 4 + u->act * 22 / 100;
        uint16_t c = u->mute ? DS_TEXT3 : MV_TC(i);
        ds_rect(x, 136, 24, 26, DS_SURF1);
        if (u->mute)
            ds_rframe(x, 136, 24, 26, 2, 1, DS_TEXT3, DS_BG);
        else
            ds_rect(x + 2, 160 - h, 20, h, u->act ? c : DS_DIM(c));
        ds_itoa(b, (int32_t)i + 1);
        if (u->solo)
            ds_chip(x + 5, 124, 14, 10, DS_TEXT1, DS_BG, &DS_F12, "S");
        ds_text_in(&DS_F12, x, 177, 24, DS_C, u->mute ? "M" : b, i == mv.sel ? DS_TEXT1 : DS_TEXT3);
        if (i == mv.sel)
            ds_rect(x + 4, 164, 16, 2, DS_TEXT1);
    }
}

/* TRACKS: eight cards, 1-4 left, 5-8 right */
static void sc_tracks(void)
{
    uint32_t i;
    char b[4];
    mv_title("HOLD SEL + KEY 1-8");
    for (i = 0; i < MV_NTRK; i++) {
        const mv_track_t *t = &mv.t[i];
        int32_t x = i < 4u ? MX : 124, y = Y_BODY + (int32_t)(i % 4u) * 33, w = 108, sel = i == mv.sel;
        uint16_t c = t->mute ? DS_TEXT3 : MV_TC(i), bg = sel ? DS_SURF2 : DS_SURF1;
        ds_rrect(x, y, w, 30, 4, bg);
        if (sel)
            ds_rframe(x, y, w, 30, 4, 2, DS_TEXT1, bg);
        ds_rect(x + (sel ? 2 : 0), y + 6, 3, 18, c);
        ds_itoa(b, (int32_t)i + 1);
        ds_text_in(&DS_F16, x + 6, y + 21, 18, DS_C, b, sel ? DS_TEXT1 : c);
        ds_text_clip(&DS_F12, x + 28, y + 13, w - 50, t->name, DS_TEXT1);
        ds_text_clip(&DS_F12, x + 28, y + 26, w - 50, t->sound, DS_TEXT3);
        if (t->mute)
            ds_chip_line(x + w - 19, y + 8, 14, 14, DS_TEXT2, bg, &DS_F12, "M");
        else if (t->solo)
            ds_chip(x + w - 19, y + 8, 14, 14, DS_TEXT1, DS_BG, &DS_F12, "S");
        else
            ds_rect(x + w - 8, y + 26 - t->act * 20 / 100, 3, 2 + t->act * 20 / 100, t->act ? c : DS_SURF2);
        if (t->arm)
            ds_disc(x + w - 30, y + 11, 8, DS_REC);
    }
}

/* SEQUENCER: the pattern above (synth: a small piano roll, drums: the 16 lanes), the page's 16 steps below,
 * one per white key, grouped by beat */
static int32_t seq_x(uint32_t i) { return 11 + (int32_t)i * 13 + (int32_t)(i / 4u) * 6; }
static void sc_roll(void);
static void sc_seq(void)
{
    const mv_track_t *t = &mv.t[mv.sel];
    char b[32], *e;
    uint32_t i, j;
    uint16_t c = MV_SELC;
    int32_t ph = mv.playing ? (int32_t)mv.step16 : -1;
    if (mv.page == 1u && t->kind != MK_DRUM) {
        sc_roll();
        return;
    }
    e = b;
    if (t->kind == MK_DRUM) {
        e = ds_cat(e, mv.lane_name);
        e = ds_cat(e, "   ");
    }
    e = ds_cat(e, "LEN ");
    e = ds_itoa(e, mv.len);
    e = ds_cat(e, "  ");
    ds_cat(e, mv.div);
    mv_title(b);
    if (ph >= 0)
        ds_rect(seq_x((uint32_t)ph) - 1, Y_BODY, 13, 74, DS_SURF1);
    if (t->kind == MK_DRUM) {                  /* 16 lanes x 16 steps */
        for (j = 0; j < 16u; j++) {
            int32_t y = Y_BODY + 2 + (int32_t)j * 4 + (int32_t)(j / 4u);
            for (i = 0; i < MV_NSTEP; i++)
                if ((mv.lanes[i] >> j) & 1u)
                    ds_rect(seq_x(i) + 1, y, 9, 3, j == mv.lane ? DS_TEXT1 : DS_MID(c));
                else if (j == mv.lane)
                    ds_rect(seq_x(i) + 4, y + 1, 3, 1, DS_TEXT3);
        }
    } else {                                   /* the notes, scaled to the range in use */
        uint32_t lo = 127, hi = 0;
        for (i = 0; i < MV_NSTEP; i++)
            if (mv.st_note[i]) {
                if (mv.st_note[i] < lo) lo = mv.st_note[i];
                if (mv.st_note[i] > hi) hi = mv.st_note[i];
            }
        if (hi < lo)
            lo = hi = 60;
        if (hi - lo < 12u) {
            lo = lo > 4u ? lo - 4u : 0u;
            hi = lo + 12u;
        }
        for (j = 0; j <= (hi - lo) / 12u + 1u; j++)          /* octave lines */
            ds_hline(MX, Y_BODY + 70 - (int32_t)(j * 12u * 64u / (hi - lo + 1u)), 224, DS_SURF1);
        for (i = 0; i < MV_NSTEP; i++)
            if (mv.st_note[i]) {
                int32_t y = Y_BODY + 66 - (int32_t)((mv.st_note[i] - lo) * 62u / (hi - lo));
                int32_t w = 13 * (mv.st_gate[i] ? mv.st_gate[i] : 1) - 2;
                if (i + (mv.st_gate[i] ? mv.st_gate[i] : 1u) > MV_NSTEP)
                    w = seq_x(MV_NSTEP - 1u) + 11 - seq_x(i);
                ds_rrect(seq_x(i), y, w, 5, 2, (int32_t)i == ph ? DS_TEXT1 : c);
            }
    }
    /* the steps */
    for (i = 0; i < MV_NSTEP; i++) {
        int32_t x = seq_x(i), y = 124, h = 40, lv = mv.st_lvl[i];
        static const uint8_t FILL[5] = {0, 12, 22, 32, 40};
        int32_t fh = FILL[lv % 5u];
        ds_rrect(x, y, 11, h, 2, DS_SURF2);
        if (fh)
            ds_rrect(x, y + h - fh, 11, fh, 2, (int32_t)i == ph ? DS_TEXT1 : c);
        else if (!(i % 4u))
            ds_rect(x + 4, y + h - 6, 3, 3, DS_TEXT3);                /* beat marks on empty steps */
        if (mv.st_lock[i])
            ds_disc(x + 3, y + 3, 5, DS_TEXT1);
        if (mv.st_micro[i])
            ds_rect(x + (mv.st_micro[i] > 64 ? 8 : 1), y + 10, 2, 6, DS_TEXT1);
        if ((int32_t)i == mv.held)
            ds_rframe(x - 2, y - 2, 15, h + 4, 3, 2, DS_TEXT1, DS_BG);
        if ((int32_t)i == ph)
            ds_rect(x, 167, 11, 2, DS_TEXT1);
    }
    /* the pages of the pattern */
    for (i = 0; i < 4u; i++) {
        int32_t x = MX + (int32_t)i * 57;
        uint16_t pc = i == mv.spage ? DS_TEXT1 : i * 16u < mv.len ? DS_TEXT3 : DS_SURF1;
        ds_rect(x, 172, 53, 2, pc);
    }
}

/* SEQUENCER, STEP page (SLOOP's step editor): the cursor's 16 steps as a piano roll over the pattern's note range;
 * the cursor a raised column with a white mark, the playhead a tint and a bar. Under it the steps as cells: a note
 * filled, a tie a half bar, a rest empty; an accent a cap on top (shape, not only colour); a slide a ramp to the
 * next step */
#define RL_Y0 (Y_BODY + 4)
#define RL_H 96
static int32_t roll_y(uint32_t n)
{
    uint32_t span = (uint32_t)(mv.roll_hi - mv.roll_lo);
    return RL_Y0 + RL_H - 6 - (int32_t)((n - mv.roll_lo) * (RL_H - 8u) / (span ? span : 1u));
}
static void sc_roll(void)
{
    char b[24], *e;
    uint32_t i, j, n, span = (uint32_t)(mv.roll_hi - mv.roll_lo);
    uint16_t c = MV_SELC;
    int32_t ph = mv.playing && mv.step16 < MV_NSTEP ? (int32_t)mv.step16 : -1, semi = (int32_t)(RL_H - 8) / (int32_t)(span ? span : 1u);
    e = ds_cat(b, "STEP ");
    e = ds_itoa(e, mv.cur_abs + 1);
    e = ds_cat(e, " / ");
    ds_itoa(e, mv.len);
    mv_title(b);
    if (semi >= 4)                                       /* the black keys' rows, when there is room for them */
        for (n = mv.roll_lo; n <= mv.roll_hi; n++)
            if ((0x54Au >> (n % 12u)) & 1u)
                ds_rect(MX, roll_y(n) - semi / 2 + 2, 224, semi > 2 ? semi - 1 : 1, DS_SURF1);
    for (n = (mv.roll_lo + 11u) / 12u * 12u; n <= mv.roll_hi; n += 12u)   /* the Cs */
        ds_hline(MX, roll_y(n) + 2, 224, DS_LINE);
    if (ph >= 0)
        ds_rect(seq_x((uint32_t)ph) - 1, RL_Y0, 13, RL_H, DS_SURF1);
    if (mv.cur < MV_NSTEP) {
        ds_rect(seq_x(mv.cur) - 1, RL_Y0, 13, RL_H, DS_SURF2);
        ds_tri(seq_x(mv.cur) + 1, RL_Y0 + RL_H + 7, seq_x(mv.cur) + 9, RL_Y0 + RL_H + 7, seq_x(mv.cur) + 5,
               RL_Y0 + RL_H + 2, DS_TEXT1);
    }
    for (i = 0; i < MV_NSTEP && i < mv.len; i++) {
        int32_t x = seq_x(i), w;
        uint32_t g = mv.st_gate[i] ? mv.st_gate[i] : 1u;
        if (!mv.roll_n[i] || (mv.roll_fl[i] & RF_TIE))
            continue;
        w = 13 * (int32_t)g - 2 + (int32_t)((i % 4u + g - 1u) / 4u) * 6;
        if (i + g > MV_NSTEP)
            w = seq_x(MV_NSTEP - 1u) + 11 - x;
        for (j = 0; j < mv.roll_n[i] && j < 4u; j++) {
            int32_t y = roll_y(mv.roll_nt[i][j]);
            uint16_t nc = (int32_t)i == ph || i == mv.cur ? DS_TEXT1 : c;
            ds_rrect(x, y, w, 5, 2, nc);
            if (mv.roll_fl[i] & RF_ACC)
                ds_rect(x + 2, y - 3, 7, 2, nc);         /* accent: a cap */
        }
        if ((mv.roll_fl[i] & RF_SLIDE) && i + g < MV_NSTEP && mv.roll_n[i + g]) {   /* slide: a ramp into the next */
            int32_t y0 = roll_y(mv.roll_nt[i][0]) + 2, y1 = roll_y(mv.roll_nt[i + g][0]) + 2;
            ds_line(x + w - 2, y0, seq_x(i + g) + 2, y1, 2, DS_TEXT2);
        }
    }
    /* the steps */
    for (i = 0; i < MV_NSTEP; i++) {
        int32_t x = seq_x(i), y = 156;
        if (i >= mv.len) {
            ds_rect(x + 4, y + 6, 3, 1, DS_SURF1);
            continue;
        }
        if (i == mv.cur)
            ds_rframe(x - 2, y - 2, 15, 16, 3, 2, DS_TEXT1, DS_BG);
        if (mv.roll_fl[i] & RF_TIE)
            ds_rrect(x, y + 6, 11, 6, 2, DS_MID(c));
        else if (mv.roll_n[i])
            ds_rrect(x, y, 11, 12, 2, (int32_t)i == ph ? DS_TEXT1 : c);
        else
            ds_rframe(x, y, 11, 12, 2, 1, DS_SURF2, DS_BG);
        if (mv.roll_n[i] && (mv.roll_fl[i] & RF_ACC) && !(mv.roll_fl[i] & RF_TIE))
            ds_rect(x + 2, y + 2, 7, 2, DS_BG);
        if ((int32_t)i == ph)
            ds_rect(x, 171, 11, 2, DS_TEXT1);
    }
    for (i = 0; i < 4u; i++) {                           /* the pages of the pattern: the cursor's lit */
        int32_t x = MX + (int32_t)i * 57;
        ds_rect(x, 174, 53, 2, i == mv.spage ? DS_TEXT1 : i * 16u < mv.len ? DS_TEXT3 : DS_SURF1);
    }
}

/* GROOVE: the value in focus large, the other three beside it, the timing of two beats drawn as it plays */
static void sc_groove(void)
{
    static const char *const GL[4] = {"SWING", "QUANTIZE", "GROOVE", "HUMANIZE"};
    static const char *const GS[4] = {"SWING", "QUANT", "GROOVE", "HUMAN"};   /* (the side column) */
    const uint8_t gv[4] = {mv.swing, mv.quant, mv.gamount, mv.human};
    char b[8];
    uint32_t i, f = mv.gfocus & 3u;
    int32_t x, w;
    uint16_t c = MV_SELC;
    ds_label(MX, Y_TITLE, 100, "GROOVE", DS_TEXT2);
    w = ds_text_w(&DS_F12, mv.tmpl) + 14;
    ds_chip(232 - w, 27, w, 15, c, DS_BG, &DS_F12, mv.tmpl);
    /* hero */
    x = ds_label(MX, 60, 100, GL[f], DS_TEXT2);
    if (f == 0u)
        ds_text(&DS_F12, x + 6, 60, mv.div, DS_TEXT3);
    ds_itoa(b, gv[f]);
    x = ds_text(&DS_F64, MX - 3, 124, b, DS_TEXT1);
    ds_text(&DS_F22, x + 3, 124, "%", DS_TEXT2);
    /* the others */
    for (i = 1; i < 4u; i++) {
        uint32_t k = (f + i) & 3u;
        int32_t y = 50 + (int32_t)(i - 1u) * 25;
        ds_label(144, y + 9, 54, GS[k], DS_TEXT3);
        ds_cat(ds_itoa(b, gv[k]), "%");
        ds_text_in(&DS_F12, 144, y + 9, 88, DS_R, b, DS_TEXT1);
        ds_gauge(144, y + 14, 88, 2, gv[k] * 10, 0, DS_TEXT2, DS_SURF2);
    }
    /* timing: 8 sixteenths, the grid in grey, each event where it plays (height = velocity) */
    for (i = 0; i <= 8u; i++)
        ds_vline(MX + 4 + (int32_t)i * 27, i % 4u ? 140 : 134, i % 4u ? 30 : 36, i % 4u ? DS_SURF2 : DS_TEXT3);
    for (i = 0; i < 8u; i++) {
        int32_t gx = MX + 4 + (int32_t)i * 27, ex = gx + mv.g_off[i] * 27 / 32, h = 6 + mv.g_vel[i] * 24 / 127;
        if (mv.human)
            ds_rect(ex - 1 - mv.human / 8, 168 - h, 4 + mv.human / 4, h, DS_DIM(c));
        if (ex != gx)
            ds_hline(ex < gx ? ex : gx, 169, ex < gx ? gx - ex : ex - gx, DS_TEXT2);
        ds_rect(ex, 170 - h, 3, h, c);
    }
}

/* CLIPS: tracks across, the six clips down. The clip a track plays: filled (a play mark while the transport runs;
 * empty: a frame); asked for (the next bar): a frame with a blinking play mark; holding a pattern: dim with a bar;
 * empty: the surface. The cursor (the selected track's clip EDIT / REC act on): framed in white */
#define CL_NROW 6u
static void sc_clips(void)
{
    char b[12], *e;
    uint32_t i, r;
    if (mv.scene < CL_NROW) {
        e = ds_cat(b, "SCENE ");
        ds_itoa(e, mv.scene + 1);
        mv_title(b);
    } else {
        mv_title("CLIPS");
    }
    for (i = 0; i < MV_NTRK; i++) {
        int32_t x = 26 + (int32_t)i * 26;
        ds_itoa(b, (int32_t)i + 1);
        ds_text_in(&DS_F12, x, 56, 24, DS_C, b, i == mv.sel ? DS_TEXT1 : MV_TC(i));
        if (i == mv.sel)
            ds_rect(x + 4, 59, 16, 2, DS_TEXT1);
    }
    for (r = 0; r < CL_NROW; r++) {
        int32_t y = 64 + (int32_t)r * 18;
        ds_itoa(b, (int32_t)r + 1);
        ds_text_in(&DS_F12, MX - 2, y + 13, 14, DS_C, b, r == mv.scene ? DS_TEXT1 : r == mv.row_sel ? DS_TEXT2 : DS_TEXT3);
        for (i = 0; i < MV_NTRK; i++) {
            const mv_track_t *t = &mv.t[i];
            int32_t x = 26 + (int32_t)i * 26;
            uint16_t c = MV_TC(i);
            uint32_t holds = (t->clips >> r) & 1u;
            if (i == mv.sel && r == mv.row_sel)          /* (first: its inside is the cell's ground) */
                ds_rframe(x - 2, y - 2, 28, 20, 4, 2, DS_TEXT1, DS_BG);
            if (t->queued == r) {
                ds_rframe(x, y, 24, 16, 3, 2, c, DS_BG);
                if (mv.blink)
                    ds_icon(IC_PLAY, x + 6, y + 2, c, DS_BG);
            } else if (t->clip == r && holds) {
                ds_rrect(x, y, 24, 16, 3, c);
                if (mv.playing)
                    ds_icon(IC_PLAY, x + 6, y + 2, DS_BG, c);
            } else if (t->clip == r) {
                ds_rframe(x, y, 24, 16, 3, 2, c, DS_BG);
                if (mv.playing)
                    ds_icon(IC_PLAY, x + 6, y + 2, c, DS_BG);
            } else if (holds) {
                ds_rrect(x, y, 24, 16, 3, DS_DIM(c));
                ds_rect(x + 4, y + 12, 16, 2, c);
            } else {
                ds_rrect(x, y, 24, 16, 3, DS_SURF1);
            }
        }
    }
}

/* MIXER: eight strips: pan, fader, mute / solo */
static void sc_mixer(void)
{
    uint32_t i;
    char b[4];
    mv_title("MASTER  -2 DB");
    for (i = 0; i < MV_NTRK; i++) {
        const mv_track_t *t = &mv.t[i];
        int32_t x = MX + (int32_t)i * 28, sel = i == mv.sel, fy, px;
        uint16_t c = t->mute ? DS_TEXT3 : MV_TC(i);
        if (sel)
            ds_rrect(x - 1, Y_BODY, 26, 130, 4, DS_SURF1);
        ds_itoa(b, (int32_t)i + 1);
        if (t->mute)
            ds_chip_line(x + 3, Y_BODY + 3, 18, 14, DS_TEXT3, sel ? DS_SURF1 : DS_BG, &DS_F12, b);
        else
            ds_chip(x + 3, Y_BODY + 3, 18, 14, c, DS_BG, &DS_F12, b);
        /* pan */
        ds_rect(x + 3, Y_BODY + 24, 18, 2, DS_SURF2);
        px = x + 12 + t->pan * 9 / 50;
        ds_rect(px - 1, Y_BODY + 21, 3, 8, DS_TEXT1);
        /* fader */
        ds_rect(x + 10, Y_BODY + 36, 4, 56, DS_SURF2);
        fy = Y_BODY + 92 - t->vol * 56 / 100;
        ds_rect(x + 10, fy, 4, Y_BODY + 92 - fy, c);
        ds_rect(x + 18, Y_BODY + 92 - t->act * 56 / 100, 2, t->act * 56 / 100, t->act ? DS_MID(c) : DS_SURF2);
        ds_rrect(x + 4, fy - 2, 16, 5, 2, sel ? DS_TEXT1 : DS_TEXT2);
        /* mute / solo: on = filled, off = outline */
        if (t->mute)
            ds_chip(x + 2, Y_BODY + 98, 20, 14, DS_TEXT2, DS_BG, &DS_F12, "M");
        else
            ds_chip_line(x + 2, Y_BODY + 98, 20, 14, DS_TEXT3, sel ? DS_SURF1 : DS_BG, &DS_F12, "M");
        if (t->solo)
            ds_chip(x + 2, Y_BODY + 114, 20, 14, DS_TEXT1, DS_BG, &DS_F12, "S");
        else
            ds_chip_line(x + 2, Y_BODY + 114, 20, 14, DS_TEXT3, sel ? DS_SURF1 : DS_BG, &DS_F12, "S");
    }
}

/* SOUND: the engine, the preset browser (previous, current, next), or the envelope */
static void sc_sound(void)
{
    static const char *const PG[4] = {"PRESET", "ENVELOPE", "LFO", "SENDS"};
    const mv_track_t *t = &mv.t[mv.sel];
    char b[12], *e;
    int32_t w;
    mv_title(PG[mv.page & 3u]);
    w = ds_text_w(&DS_F12, t->engine) + 14;
    ds_chip_line(MX, 48, w, 16, MV_SELC, DS_BG, &DS_F12, t->engine);
    ds_text(&DS_F12, MX + w + 8, 60, mv.cat, DS_TEXT3);
    e = ds_itoa(b, mv.preset_i + 1);
    e = ds_cat(e, " / ");
    ds_itoa(e, mv.preset_n);
    ds_text_in(&DS_F12, 0, 60, 232, DS_R, b, DS_TEXT3);
    if (mv.page == 0u) {
        ds_text(&DS_F16, MX, 88, mv.around[0], DS_TEXT3);
        ds_rrect(MX - 2, 96, 228, 36, 4, DS_SURF1);
        ds_rect(MX - 2, 102, 3, 24, MV_SELC);
        ds_text(ds_fit(t->sound, 210, &DS_F22, &DS_F16), MX + 8, 122, t->sound, DS_TEXT1);
        ds_text(&DS_F16, MX, 158, mv.around[1], DS_TEXT3);
    } else {                                   /* the envelope: attack, decay to sustain, hold, release */
        int32_t x0 = MX, yb = 166, yt = 80, xa, xd, xs, xr, ys;
        xa = x0 + mv.env[0] * 50 / 100;
        xd = xa + 6 + mv.env[1] * 50 / 100;
        ys = yb - mv.env[2] * (yb - yt) / 100;
        xs = xd + 50;
        xr = xs + 6 + mv.env[3] * 60 / 100;
        ds_hline(MX, yb, 224, DS_LINE);
        ds_line(x0, yb, xa, yt, 2, MV_SELC);
        ds_line(xa, yt, xd, ys, 2, MV_SELC);
        ds_line(xd, ys, xs, ys, 2, MV_SELC);
        ds_line(xs, ys, xr, yb, 2, MV_SELC);
        ds_disc(xa - 3, yt - 3, 7, DS_TEXT1);
        ds_disc(xd - 3, ys - 3, 7, DS_TEXT1);
        ds_disc(xs - 3, ys - 3, 7, DS_TEXT1);
    }
}

/* PERFORM: the sixteen punch-in effects as the keys play them (hold FX + key) */
static void sc_perform(void)
{
    uint32_t i;
    char b[4];
    mv_title("HOLD FX + KEY");
    for (i = 0; i < 16u; i++) {
        int32_t x = 6 + (int32_t)(i % 4u) * 58, y = Y_BODY + (int32_t)(i / 4u) * 32, on = (mv.fx_on >> i) & 1u;
        ds_rrect(x, y, 54, 29, 4, on ? DS_TEXT1 : DS_SURF1);
        ds_itoa(b, (int32_t)i + 1);
        ds_text(&DS_F12, x + 5, y + 12, b, on ? DS_BG : DS_TEXT3);
        ds_label(x + 3, y + 25, 50, mv.fx[i] ? mv.fx[i] : "", on ? DS_BG : DS_TEXT1);
    }
}

/* a list (PROJECT, SETTINGS): the selected row inverted */
static void mv_list(int mark_cur)
{
    uint32_t i;
    for (i = 0; i < mv.nrows && i < MV_ROWS; i++) {
        int32_t y = Y_BODY + (int32_t)i * 22, sel = i == mv.row_sel;
        uint16_t fg = sel ? DS_BG : DS_TEXT1, sub = sel ? DS_BG : DS_TEXT3;
        if (sel)
            ds_rrect(MX - 2, y, 228, 20, 4, DS_TEXT1);
        ds_text(&DS_F16, MX + 6, y + 15, mv.row_l[i], mark_cur ? fg : sel ? DS_BG : DS_TEXT2);
        ds_text_in(&DS_F16, 0, y + 15, 226, DS_R, mv.row_r[i], mark_cur ? sub : fg);
        if (mark_cur && i == mv.row_cur)
            ds_disc(MX - 6 + 2, y + 7, 6, sel ? DS_BG : DS_PLAY);
    }
}

static void sc_project(void) { mv_title("8 TRACKS"); mv_list(1); }
static void sc_settings(void) { mv_title(0); mv_list(0); }

/* a destructive action: what it does, which button confirms (the same one again), anything else cancels */
static void mv_modal(void)
{
    ds_rect(0, Y_TITLE - 12, DS_W, Y_KNOBS - Y_TITLE + 12, DS_BG);   /* the body gives way: the question alone */
    ds_rrect(14, 52, 212, 120, 6, DS_SURF1);
    ds_rframe(14, 52, 212, 120, 6, 1, DS_LINE, DS_SURF1);
    ds_rect(14 + 6, 52, 200, 3, DS_REC);
    ds_text_in(ds_fit(mv.modal_t, 196, &DS_F22, &DS_F16), 14, 92, 212, DS_C, mv.modal_t, DS_TEXT1);
    ds_text_in(&DS_F12, 14, 114, 212, DS_C, mv.modal_b, DS_TEXT2);
    ds_chip(26, 134, 92, 24, DS_REC, DS_BG, &DS_F12, mv.modal_ok);
    ds_chip_line(122, 134, 92, 24, DS_TEXT2, DS_SURF1, &DS_F12, "ELSE CANCEL");
}

static void mv_toast(void)
{
    int32_t w = ds_text_w(&DS_F12, mv.toast) + 24;
    ds_rrect((DS_W - w) / 2, 148, w, 22, 11, DS_TEXT1);
    ds_text_in(&DS_F12, (DS_W - w) / 2, 163, w, DS_C, mv.toast, DS_BG);
}

static void (*const SCENES[SC_COUNT])(void) = {sc_play, sc_tracks, sc_seq, sc_groove, sc_clips,
                                                sc_mixer, sc_sound, sc_perform, sc_project, sc_settings};

static void mv_scene(void)
{
    mv_status();
    if (mv.hot >= 0 && !mv.modal && !mv.nofocus && mv.k[mv.hot & 3].label) {   /* (a knob with nothing to do opens nothing) */
        mv_title(0);
        mv_focus();
    } else {
        SCENES[mv.screen % SC_COUNT]();
    }
    mv_knobs();
    if (mv.toast_t && !mv.modal)
        mv_toast();
    if (mv.modal)
        mv_modal();
}

/* draw the screen: only rows that changed reach the panel */
static void mv_render(int force) { ds_paint(mv_scene, ds_hash(2166136261u, &mv, sizeof mv) | 1u, force); }
