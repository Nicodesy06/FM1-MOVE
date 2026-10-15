/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 Move controls: the panel as docs/UI_NAVIGATION.md and docs/UI_MAPPING.md describe it, and the live view model
 * the screens (ui_move.c) draw. Replaces SLOOP's dispatcher (ui_input.c) and drawing (ui_draw.c, ui_studio.c ...)
 * in the FM1MOVE build; keeps SLOOP's engine-side behaviour: the layers' keys (seq.c, ui_layers.c), the record
 * arm, the transport, undo / redo, the octave, the drum dynamics.
 *
 *   top row    FX PERFORM   SEL TRACKS   ENV SOUND.ENV   LFO SOUND.LFO   EDIT SOUND   GLO MIXER
 *   bottom row HOME PLAY    SAVE CLIPS   ARP GROOVE      SEQ SEQUENCER   PLAY transport   REC record
 *   held       FX punch, EDIT erase, ARP roll, SEQ steps, SEL track / key, GLO mute / solo,
 *              SAVE (CLIPS) white keys 1..6 launch the track's clips, 9..14 the scenes (the next bar)
 *   SEQ again  the STEP page (synth tracks): SLOOP's step editor, a cursor over a piano roll; keys write the cursor's
 *              step (pressed together: a chord), letting go moves on; STEP / NOTE / TIME / FLAG on the knobs
 *   HOME held  the system pages: PROJECT, SETTINGS
 *   encoders   SELECT track (a button held: the tempo), PRESETS sound, ALGORITHM the screen's navigator, KNOB 1..4
 * Destructive actions ask first: a modal, the same button again confirms, anything else (or 4 s) cancels. */

#define CTL_TAP_MS 450u                         /* a press shorter than this, untouched: a tap */
#define CTL_HOT_MS 1200u                        /* the focus overlay stays this long after the last detent */
#define CTL_TOAST_MS 1500u
#define CTL_ARM_MS 4000u

static int32_t accel(uint32_t role, int32_t s, int32_t range)   /* fast turns go further (SLOOP's curve) */
{
    static uint32_t last[NE];
    uint32_t now = fm1_ms, dt = now - last[role % NE];
    int32_t m = range > 60 ? (dt < 30u ? 4 : dt < 60u ? 2 : 1) : 1;
    last[role % NE] = now;
    return s * m;
}
/* (SLOOP's drawing entry points ui.c still names: nothing draws them in FM1 Move) */
static void layer_screen_draw(void) {}
static void hold_screen_draw(void) {}

/* --------------------------------------------------------------- state --- */
static struct {
    uint8_t screen, menu;                       /* the screen; menu: the system pages (PROJECT / SETTINGS) open */
    uint8_t page[SC_COUNT];                     /* each screen's page */
    uint8_t row[SC_COUNT];                      /* each screen's list row / navigator */
    int8_t hot;                                 /* the knob turned last, -1 none */
    uint32_t hot_ms, toast_ms;
    char toast[24];
    /* a destructive action waiting for its confirmation */
    uint8_t arm, arm_btn, arm_arg;
    uint32_t arm_ms;
    char arm_t[24], arm_b[32], arm_ok[16];
    uint8_t kind_req;                           /* TRACKS: the kind KNOB 1 asks for (TK_COUNT = none) */
    uint8_t act[NTRK];                          /* the meters, decaying */
    uint8_t force;
    uint32_t t0[NB];                            /* button press times */
    uint8_t used[NB];                           /* a key / knob touched while the button was held */
    uint8_t layer_shown;
    uint32_t tempo_ms;                          /* the tempo was changed (ALGORITHM, a tap): its readout shows a while */
} ctl = {.hot = -1, .kind_req = TK_COUNT, .force = 1};

/* clip.c (included by projfmt.c, after this file) */
static void clip_launch(uint32_t ti, uint32_t k);
static void clip_scene(uint32_t k);
static int clip_copy(uint32_t ti, uint32_t from, uint32_t to);
static void clip_erase(uint32_t ti, uint32_t k);
static void clip_reset_track(uint32_t ti);
static void clip_init(void);
static void clip_tick(void);
static uint32_t clip_state(uint32_t ti, uint32_t k);
static uint32_t clip_playing(uint32_t ti);
static uint32_t clip_holds(uint32_t ti, uint32_t k);
static uint8_t clip_full;

enum { ACT_NONE, ACT_KIND, ACT_SAVE, ACT_LOAD, ACT_DELETE, ACT_COMMIT, ACT_CLIPCOPY, ACT_CLIPCLEAR };

static void ctl_toast(const char *a, const char *b)
{
    uint32_t n;
    str_cpy(ctl.toast, a, sizeof ctl.toast);
    n = str_len(ctl.toast);
    str_cpy(ctl.toast + n, b, sizeof ctl.toast - n);
    ctl.toast_ms = fm1_ms | 1u;
}
/* SLOOP's messages (ui_say) become toasts */
static void ctl_messages(void)
{
    if (ui.msg_t) {
        ctl_toast(ui.msg, "");
        ui.msg_t = 0;
    }
    if (clip_full) {                                     /* (clip.c: a store found no room; said once per refusal) */
        ctl_toast("CLIP MEMORY FULL", "");
        clip_full = 0;
    }
    if (ctl.toast_ms && fm1_ms - (ctl.toast_ms & ~1u) > CTL_TOAST_MS)
        ctl.toast_ms = 0;
}

static void ctl_arm(uint32_t act, uint32_t btn, uint32_t arg, const char *t, const char *b, const char *ok)
{
    ctl.arm = (uint8_t)act;
    ctl.arm_btn = (uint8_t)btn;
    ctl.arm_arg = (uint8_t)arg;
    ctl.arm_ms = fm1_ms;
    str_cpy(ctl.arm_t, t, sizeof ctl.arm_t);
    str_cpy(ctl.arm_b, b, sizeof ctl.arm_b);
    str_cpy(ctl.arm_ok, ok, sizeof ctl.arm_ok);
}

/* ------------------------------------------------------- knob bindings --- */
enum { KB_NONE, KB_TP, KB_GP, KB_MACRO, KB_GRV_Q, KB_GRV_AMT, KB_GRV_HUM, KB_SHIFT, KB_KIND, KB_KIT, KB_SET, KB_CLIP, KB_STEP };
typedef struct {
    uint8_t kind, id;
    const char *label;
} kbind_t;

static uint32_t sel_kind(void) { return trk_kind(TSEL); }
/* the SEQUENCER's STEP page is shown (synth tracks: the drum track keeps its grid) */
static int ctl_roll(void) { return !ctl.menu && ctl.screen == SC_SEQ && ctl.page[SC_SEQ] == 1u && !is_drum(TSEL); }

/* SETTINGS rows (ST_LABEL below); from ST_BATT on they only show something */
enum { ST_CLICK, ST_COUNTIN, ST_RECQ, ST_SYNC, ST_MIDIOUT, ST_MIDIIN, ST_LIGHTS, ST_KEYS, ST_USBAUDIO, ST_BATT, ST_AUDIO, ST_ABOUT, ST_COUNT };
/* what KNOB k does on the screen shown */
static kbind_t ctl_bind(uint32_t k)
{
    static const kbind_t MIX[4] = {{KB_TP, P_LEVEL, "VOLUME"}, {KB_TP, P_PAN, "PAN"}, {KB_TP, P_REV, "REVERB"},
                                   {KB_TP, P_DLY, "DELAY"}};
    static const kbind_t SEQ[4] = {{KB_TP, P_SDIV, "DIV"}, {KB_TP, P_SSWING, "SWING"}, {KB_TP, P_SLEN, "LENGTH"},
                                   {KB_SHIFT, 0, "SHIFT"}};
    static const kbind_t GRV[4] = {{KB_TP, P_SSWING, "SWING"}, {KB_GRV_Q, 0, "QUANT"}, {KB_GRV_AMT, 0, "GROOVE"},
                                   {KB_GRV_HUM, 0, "HUMAN"}};
    static const kbind_t TRK[4] = {{KB_KIND, 0, "TYPE"}, {KB_TP, P_TRANS, "TRANSP"}, {KB_TP, P_SLEN, "LENGTH"},
                                   {KB_TP, P_SDIV, "DIV"}};
    static const kbind_t ENV[4] = {{KB_TP, P_ATK, "ATTACK"}, {KB_TP, P_DEC, "DECAY"}, {KB_TP, P_SUS, "SUSTAIN"},
                                   {KB_TP, P_REL, "RELEASE"}};
    static const kbind_t LFO[4] = {{KB_TP, P_LRATE, "RATE"}, {KB_TP, P_LWAVE, "WAVE"}, {KB_TP, P_LD_PIT, "PITCH"},
                                   {KB_TP, P_LD_FLT, "FILTER"}};
    static const kbind_t SND[4] = {{KB_TP, P_DIST, "DRIVE"}, {KB_TP, P_CHOR, "CHORUS"}, {KB_TP, P_DLY, "DELAY"},
                                   {KB_TP, P_REV, "REVERB"}};
    /* SLOOP 2.6: LFO 2 and ENV SYNC; the INSERT (A B C named by its TYPE: label 0 = the descriptor's); ANALOG's TYPE */
    static const kbind_t SYN[4] = {{KB_TP, P_LSYNC, "LFO SYNC"}, {KB_TP, P_LTRIG, "LFO TRIG"}, {KB_TP, P_LPOL, "LFO POL"},
                                   {KB_TP, P_ESYNC, "ENV SYNC"}};
    static const kbind_t INS[4] = {{KB_TP, P_ITYPE, "INSERT"}, {KB_TP, P_IA, 0}, {KB_TP, P_IB, 0}, {KB_TP, P_IC, 0}};
    static const kbind_t IN2[4] = {{KB_TP, P_IMIX, "INS MIX"}, {KB_TP, P_FTYPE, "FLT TYPE"}, {KB_NONE, 0, 0}, {KB_NONE, 0, 0}};
    static const kbind_t TX1[4] = {{KB_GP, G_TXSRC, "TEXTURE"}, {KB_GP, G_TXLVL, "LEVEL"}, {KB_GP, G_TXTONE, "TONE"},
                                   {KB_GP, G_TXMOVE, "MOVE"}};
    static const kbind_t TX2[4] = {{KB_GP, G_TXRATE, "RATE"}, {KB_GP, G_TXWHEN, "PLAYS"}, {KB_GP, G_TXDUCK, "DUCK"},
                                   {KB_NONE, 0, 0}};
    static const kbind_t PRF[4] = {{KB_GP, G_FILT, "FILTER"}, {KB_GP, G_DUST, "DUST"}, {KB_GP, G_DUCK, "DUCK"},
                                   {KB_TP, P_TFLT, "TRK FLT"}};
    static const kbind_t DRM[4] = {{KB_KIT, 0, "KIT"}, {KB_TP, P_LEVEL, "LEVEL"}, {KB_TP, P_PAN, "PAN"},
                                   {KB_TP, P_TFLT, "FILTER"}};
    static const kbind_t NONE = {KB_NONE, 0, 0};
    k &= 3u;
    if (ctl.menu)
        return ctl.screen == SC_SETTINGS && !k && ctl.row[SC_SETTINGS] < ST_BATT ? (kbind_t){KB_SET, 0, "VALUE"} : NONE;   /* (BATTERY on: read only) */
    switch (ctl.screen) {
    case SC_PLAY:
        if (sel_kind() == TK_DRUM)
            return DRM[k];
        return (kbind_t){KB_MACRO, (uint8_t)k, 0};
    case SC_TRACKS: return TRK[k];
    case SC_SEQ:
        if (ctl_roll()) {
            static const char *const RL[4] = {"STEP", "NOTE", "TIME", "FLAG"};
            return (kbind_t){KB_STEP, (uint8_t)k, RL[k]};
        }
        return SEQ[k];
    case SC_GROOVE: return GRV[k];
    case SC_MIXER: return MIX[k];
    case SC_SOUND:
        if (sel_kind() == TK_DRUM) {                     /* the drum track: KIT, its INSERT, the INSERT's MIX */
            uint32_t pg = ctl.page[SC_SOUND] % 3u;
            return pg == 1u ? INS[k] : pg == 2u ? (k ? (kbind_t){KB_NONE, 0, 0} : IN2[0]) : DRM[k];
        }
        switch (ctl.page[SC_SOUND] % 7u) {
        case 1: return ENV[k];
        case 2: return LFO[k];
        case 3: return SYN[k];
        case 4: return SND[k];
        case 5: return INS[k];
        case 6: return IN2[k];
        default: return (kbind_t){KB_MACRO, (uint8_t)k, 0};
        }
    case SC_PERFORM:
        return ctl.page[SC_PERFORM] == 2u ? TX1[k] : ctl.page[SC_PERFORM] == 3u ? TX2[k] : PRF[k];
    case SC_CLIPS: return !k ? (kbind_t){KB_CLIP, 0, "CLIP"} : NONE;
    default: return NONE;
    }
}

/* a track parameter's descriptor and value pointer for binding b on track t (0 = not on this track) */
static const param_desc_t *ctl_tp(track_t *t, kbind_t b, int16_t **vp)
{
    uint32_t id = b.id;
    if (b.kind == KB_MACRO) {
        if (!is_synth(t))
            return 0;
        id = ENGINES[t->eng_req % NENGINES]->macro[b.id & 3u];
    } else if (b.kind == KB_KIT) {
        if (!is_drum(t))
            return 0;
        id = P_E0;
    } else if (b.kind != KB_TP) {
        return 0;
    }
    if (id >= P_COUNT || (id == P_FTYPE && (!is_synth(t) || t->eng_req != 0u)))
        return 0;                                        /* (FLT TYPE: ANALOG's filter only) */
    *vp = &t->p[id];
    return track_desc(t, id);
}

/* the groove the GROOVE screen edits: the selected track's (it becomes its own groove on the first edit) */
static groove_t *ctl_groove_own(void)
{
    groove_t *g = &TSEL->gv;
    if (!g->own) {                                       /* take the song's feel as the start */
        g->tmpl = song_groove.tmpl;
        g->amt = song_groove.amt;
        g->vamt = song_groove.vamt;
        memcpy(g->user_off, song_groove.user_off, sizeof g->user_off);
        memcpy(g->user_vel, song_groove.user_vel, sizeof g->user_vel);
        g->own = 1;
    }
    return g;
}

/* ------------------------------------------------------------ settings --- */
static const char *const ST_LABEL[ST_COUNT] = {"METRONOME", "COUNT-IN", "REC QUANT", "CLOCK IN", "MIDI OUT", "MIDI IN", "LIGHTS",
                                               "KEY LIGHTS", "USB AUDIO", "BATTERY", "AUDIO LOAD", "VERSION"};
/* the battery (main.c reads the divider on PB1 into song.batt_raw): SLOOP's three thresholds, 3 bars .. 0, -1 not read
 * yet; on a USB host the reading says little (it powers the FM-1), so it is shown as USB */
static int32_t ctl_batt(void)
{
    if (!song.batt_raw)
        return -1;
    return song.batt_raw >= 591 ? 3 : song.batt_raw >= 561 ? 2 : song.batt_raw >= 531 ? 1 : 0;
}
static int ctl_on_usb(void) { return usb.config && !usb.suspended; }
static uint32_t ctl_late0;                              /* felucca_dbg.late at power-on (it is kept across resets) */
static void ctl_batt_watch(void)                        /* empty: said once, again only after it was charged (2 bars) */
{
    static uint8_t said, late_set;
    int32_t l = ctl_batt();
    if (!late_set) {                                     /* (the first frame: AUDIO LOAD counts from this start) */
        late_set = 1;
        ctl_late0 = felucca_dbg.late;
    }
    if (ctl_on_usb() || l >= 2)
        said = 0;
    else if (l == 0 && !said) {
        said = 1;
        ctl_toast("BATTERY LOW", "");
    }
}
static void st_value(uint32_t r, char *b)
{
    static const char *const SYNC[3] = {"INT", "USB", "TRS"};
    static const char *const LV[4] = {"OFF", "LOW", "MID", "HIGH"};
    static const char *const KY[4] = {"OFF", "C KEYS", "WHITE", "ALL"};
    switch (r) {
    case ST_CLICK: str_cpy(b, N_CLICK[clamp(song.g[G_CLOCK], 0, 2)], 16); break;   /* OFF / REC (while recording) / ON */
    case ST_COUNTIN: str_cpy(b, rec_count ? "1 BAR" : "OFF", 16); break;
    case ST_RECQ: {                                      /* REC while playing: on the cycle of 1 .. 8 bars, or at once */
        static const char *const RQN[5] = {"OFF", "1 BAR", "2 BARS", "4 BARS", "8 BARS"};
        str_cpy(b, RQN[rec_q_idx()], 16);
        break;
    }
    case ST_SYNC: str_cpy(b, SYNC[lights_sync % 3u], 16); break;
    case ST_MIDIOUT: str_cpy(b, lights_mout ? "SEQ" : "KEYS", 16); break;
    case ST_MIDIIN: str_cpy(b, lights_min ? "CLOCK" : "NOTES", 16); break;
    case ST_LIGHTS: str_cpy(b, LV[lights_lvl % 4u], 16); break;
    case ST_KEYS: str_cpy(b, KY[lights_keys % 4u], 16); break;
    case ST_USBAUDIO: str_cpy(b, usb_full ? "FULL" : "MAIN", 16); break;
    case ST_AUDIO: {                                     /* (BUG-02) the render's peak load since power-on, % of its time,
                                                          * and the buffers that missed it: what a glitch report needs */
        uint32_t pk = felucca_dbg.max_us * 100u / (HALF_FRAMES * 1000000u / FS), l = felucca_dbg.late - ctl_late0;
        char *e = ds_itoa(b, (int32_t)(pk > 999u ? 999u : pk));
        str_cpy(e, "% LATE ", 8);
        ds_itoa(e + 7, (int32_t)(l > 999u ? 999u : l));
        break;
    }
    case ST_BATT: {
        static const char *const BN[4] = {"EMPTY", "LOW", "GOOD", "FULL"};
        int32_t l = ctl_batt();
        str_cpy(b, ctl_on_usb() ? "ON USB" : l < 0 ? "--" : BN[l], 16);
        break;
    }
    default: str_cpy(b, FELUCCA_VERSION, 16); break;
    }
}
static void st_turn(uint32_t r, int32_t s)
{
    int32_t d = s > 0 ? 1 : -1;
    switch (r) {
    case ST_CLICK:                                       /* the metronome (seq.c click_tick; the project keeps it) */
        song.g[G_CLOCK] = (int16_t)clamp(song.g[G_CLOCK] + d, 0, 2);
        break;
    case ST_COUNTIN:                                     /* REC armed, PLAY: one bar of clicks first (seq.c) */
        rec_count = (uint8_t)!rec_count;
        break;
    case ST_RECQ: {
        static const uint8_t RQ[5] = {0, 1, 2, 4, 8};
        rec_q = RQ[clamp((int32_t)rec_q_idx() + d, 0, 4)];
        break;
    }
    case ST_SYNC:
        lights_sync = (uint8_t)((lights_sync + 3 + d) % 3);
        song.g[G_SYNC] = lights_sync;
        break;
    case ST_MIDIOUT:
        lights_mout = (uint8_t)!lights_mout;
        song.g[G_MIDI] = lights_mout;
        break;
    case ST_MIDIIN:
        lights_min = (uint8_t)!lights_min;
        song.g[G_ROUTE] = lights_min;
        break;
    case ST_LIGHTS: lights_lvl = (uint8_t)((lights_lvl + LIGHTS_N + d) % LIGHTS_N); break;
    case ST_KEYS: lights_keys = (uint8_t)((lights_keys + KEYS_N + d) % KEYS_N); break;
    case ST_USBAUDIO: usb_full = (uint8_t)!usb_full; break;
    default: return;
    }
    settings_later = 1;                                  /* (saved once stopped: project.c) */
}

/* -------------------------------------------------------------- actions --- */
static int fmp_used(uint32_t slot);                      /* project.c (FM1MOVE) */
static int project_save_fmp(uint32_t slot);
static int project_load_fmp(uint32_t slot);
static void project_delete_fmp(uint32_t slot);

static void ctl_do(uint32_t act, uint32_t arg)
{
    char b[4] = {(char)('1' + (arg % 10u)), 0, 0, 0};
    switch (act) {
    case ACT_KIND:
        fm1_irq_off();
        trk_set_kind(&trk[arg % NTRK], ctl.kind_req);
        fm1_irq_on();
        if (ctl.kind_req == TK_SYNTH)
            set_engine_of(&trk[arg % NTRK], 0);
        if (ctl.kind_req == TK_DRUM)
            trk[arg % NTRK].p[P_E0] = DRUM_DEFAULT_KIT;
        clip_reset_track(arg % NTRK);                    /* (its clips were of the other kind) */
        ctl.kind_req = TK_COUNT;
        ctl_toast("TRACK ", b);
        break;
    case ACT_SAVE: {
        int rc = project_save_fmp(arg);
        ctl_toast(rc == -9 ? "PROJECT TOO DENSE" : rc ? "SAVE FAILED" : "SAVED ", rc ? "" : b);
        break;
    }
    case ACT_LOAD:
        ctl_toast(project_load_fmp(arg) ? "LOAD FAILED" : "LOADED ", b);
        break;
    case ACT_DELETE:
        project_delete_fmp(arg);
        ctl_toast("DELETED ", b);
        break;
    case ACT_CLIPCOPY: {                                 /* the clip playing on the selected track -> slot arg */
        b[0] = (char)('1' + (arg % NCLIP));
        ctl_toast(clip_copy(song.sel, clip_playing(song.sel), arg % NCLIP) ? "CLIP MEMORY FULL" : "COPIED TO CLIP ", b);
        clip_full = 0;                                   /* (said here) */
        break;
    }
    case ACT_CLIPCLEAR:
        b[0] = (char)('1' + (arg % NCLIP));
        clip_erase(song.sel, arg % NCLIP);
        ctl_toast("CLEARED CLIP ", b);
        break;
    case ACT_COMMIT:
        fm1_irq_off();
        groove_commit(TSEL);
        fm1_irq_on();
        ctl_toast("GROOVE COMMITTED", "");
        break;
    default:
        break;
    }
}

static void screen_go(uint32_t sc)
{
    if (!ctl.menu && ctl.screen == sc) {                 /* the same button again: the next page */
        static const uint8_t NP[SC_COUNT] = {1, 1, 2, 2, 1, 1, 7, 4, 1, 1};
        uint32_t np = sc == SC_SOUND && is_drum(TSEL) ? 3u : NP[sc];   /* (the drum track: KIT, INSERT, INSERT 2) */
        ctl.page[sc] = (uint8_t)((ctl.page[sc] + 1u) % np);
    } else if (sc == SC_CLIPS && clip_playing(song.sel) < NCLIP) {
        ctl.row[SC_CLIPS] = (uint8_t)clip_playing(song.sel);   /* (on the track's clip playing: REC, REC clears it) */
    }
    ctl.menu = 0;
    ctl.screen = (uint8_t)sc;
    ctl.hot = -1;
    song.seq_mode = sc == SC_SEQ;
}

/* STEP page MOVE (ALGORITHM on a note): the cursor's note - its chord, its ties, flags, velocity, nudge, raw timing,
 * condition and parameter locks - moves a step at a time, over the next note (with its ties) or rest, which slides
 * the other way; the cursor follows. One undo puts it back. The rotation: positions lo..hi turn left by k (the one
 * at lo + k goes to lo), everything a step owns with them. */
static struct { uint8_t trk, start, len, at; uint32_t sess; } smove;
static void step_rot(track_t *t, uint32_t lo, uint32_t hi, uint32_t k, int notes)   /* notes 0: what the undo leaves */
{
    uint32_t n = hi - lo + 1u, r, i, j, a, b, f;
    if (!k || k >= n)
        return;
    fm1_irq_off();
    for (r = 0; r < 3u; r++) {                           /* three reversals: [lo, lo+k), [lo+k, hi], [lo, hi] */
        a = r == 1u ? lo + k : lo;
        b = r == 0u ? lo + k - 1u : hi;
        for (i = a, j = b; i < j; i++, j--) {
            step_t x = t->step[i];
            int8_t m = t->micro[i], o = t->roff[i];
            if (notes) {
                t->step[i] = t->step[j];
                t->step[j] = x;
            }
            t->micro[i] = t->micro[j];
            t->micro[j] = m;
            t->roff[i] = t->roff[j];
            t->roff[j] = o;
            f = step_fill(t, i);
            step_fill_set(t, i, step_fill(t, j));
            step_fill_set(t, j, f);
        }
    }
    for (i = 0; i < NLOCK; i++)                          /* the locks: their step numbers turn the same way */
        if (t->lock[i].step != LOCK_FREE && t->lock[i].step >= lo && t->lock[i].step <= hi)
            t->lock[i].step = (uint8_t)(lo + (t->lock[i].step - lo + n - k) % n);
    fm1_irq_on();
    sync_reload = 1;
}
/* ALGORITHM s on the STEP page: 1 = the note moved (0: an empty step, the cursor moves instead) */
static int ctl_step_move(int32_t s)
{
    track_t *t = TSEL;
    uint32_t len = trk_len(t), p = ui.cursor % NSTEP, l, g;
    if (p >= len || !t->step[p].n || t->step[p].time == ST_TIE)
        return 0;
    l = note_len(t, p);
    if (!(smove.sess && undo.valid && !undo.undone && undo.sess == smove.sess && undo.trk == trk_index(t) &&
          smove.at == p && smove.len == l)) {            /* a new move: its own undo */
        smove.sess = (undo_sess += 4u) | 3u;
        smove.trk = (uint8_t)trk_index(t);
        smove.start = (uint8_t)p;
        smove.len = (uint8_t)l;
        undo_mark(t, smove.sess);
    }
    for (; s > 0; s--) {                                 /* later: over the next note (and its ties) or rest */
        if (p + l >= len)
            break;
        g = t->step[p + l].n && t->step[p + l].time == ST_NOTE ? note_len(t, p + l) : 1u;
        if (p + l + g > len)
            break;
        step_rot(t, p, p + l + g - 1u, l, 1);
        p += g;
    }
    for (; s < 0; s++) {                                 /* earlier: over the one before (a tie: its whole note) */
        if (!p)
            break;
        for (g = 1; p - g > 0 && t->step[p - g].time == ST_TIE; g++)
            ;
        if (!t->step[p - g].n || t->step[p - g].time != ST_NOTE || note_len(t, p - g) != g)
            g = 1;                                       /* (a lone tie or a rest: one step) */
        step_rot(t, p - g, p + l - 1u, g, 1);
        p -= g;
    }
    smove.at = (uint8_t)p;
    t->seq_active = 1;
    cursor_set((int32_t)p);
    {
        char b[8];
        ds_itoa(b, (int32_t)p + 1);
        ctl_toast("MOVED TO STEP ", b);
    }
    return 1;
}
/* EDIT + OCT-/OCT+ on a move: the notes come back with the undo (seq.c), what else the steps own here */
static void ctl_step_move_undo(int redo)
{
    track_t *t = &trk[smove.trk % NTRK];
    uint32_t s = smove.start, q = smove.at, l = smove.len;
    if (q > s)
        step_rot(t, s, q + l - 1u, redo ? l : q - s, 0);
    else if (q < s)
        step_rot(t, q, s + l - 1u, redo ? s - q : l, 0);
}

/* STEP page (SLOOP's STEP): the knobs edit the cursor's step. STEP the cursor, NOTE transposes it (an empty step:
 * the last note entered), TIME note / tie / rest, FLAG - / accent / slide / both */
static void ctl_step_edit(uint32_t k, int32_t s)
{
    track_t *t = TSEL;
    step_t *st = &t->step[ui.cursor % NSTEP];
    uint32_t i, f;
    static uint32_t sess, sess_at = 0xFFFFu;
    if (k) {                                             /* the edits of one step: one undo */
        if (sess_at != ui.cursor || !sess)
            sess = (undo_sess += 4u) | 3u;
        sess_at = ui.cursor;
        undo_mark(t, sess);
        t->seq_active = 1;
    }
    switch (k) {
    case 0:
        cursor_set((int32_t)ui.cursor + s);
        break;
    case 1:
        if (!st->n) {
            step_hand(t, ui.cursor);
            st->note[0] = (uint8_t)scale_snap(t, last_note);
            st->n = 1;
            st->time = ST_NOTE;
            break;
        }
        for (i = 0; i < st->n; i++)
            st->note[i] = (uint8_t)clamp((int32_t)scale_step(t, st->note[i], s), 1, 127);   /* (SCALE LOCK: in it) */
        st->time = ST_NOTE;
        last_note = st->note[0];
        break;
    case 2:                                              /* TIME; on a note: its LEN in steps (2.6, ui.c) */
        if (step_on(st)) {
            uint32_t was = note_len(t, ui.cursor % NSTEP), n;
            fm1_irq_off();
            n = note_set_len(t, ui.cursor % NSTEP, (uint32_t)clamp((int32_t)was + s, 1, NSTEP));
            fm1_irq_on();
            if (s > 0 && n == was)
                ctl_toast(ui.cursor + n < trk_len(t) ? "NOTE AHEAD" : "END OF PATTERN", "");
            break;
        }
        st->time = (uint8_t)clamp((int32_t)st->time + (s > 0 ? 1 : -1), ST_NOTE, ST_REST);
        break;
    default:
        f = (st->flags & SF_ACCENT ? 1u : 0u) | (st->flags & SF_SLIDE ? 2u : 0u);
        f = (uint32_t)clamp((int32_t)f + (s > 0 ? 1 : -1), 0, 3);
        st->flags = (uint8_t)((st->flags & ~(SF_ACCENT | SF_SLIDE)) | (f & 1u ? SF_ACCENT : 0u) | (f & 2u ? SF_SLIDE : 0u));
        break;
    }
}
/* (2.6) the STEP page from a MIDI keyboard (seq.c step_in_q): its notes on the selected track write the cursor's step
 * as the keys do (held together: a chord; every note up, keys too: the next step). Not while recording or armed */
static uint32_t ctl_mi_held[4];
static void ctl_midi_step_in(void)
{
    track_t *t = TSEL;
    int on_page = ctl_roll() && !song.rec && !rec_wait && !ft_on;
    if (step_in_lost) {                                  /* (the queue overflowed: start clean) */
        step_in_lost = 0;
        step_in_r = step_in_w;
        ctl_mi_held[0] = ctl_mi_held[1] = ctl_mi_held[2] = ctl_mi_held[3] = 0;
    }
    while (step_in_r != step_in_w) {
        uint32_t ev = step_in_q[step_in_r % STEP_IN_Q], note = ev & 127u, tr = (ev >> 8) % NTRK;
        step_t *st = &t->step[ui.cursor % NSTEP];
        step_in_r++;
        if (!(ev & 0x80u)) {
            ctl_mi_held[note >> 5] &= ~(1u << (note & 31u));
            continue;
        }
        if (!on_page)
            continue;
        if (&trk[tr] == t)
            note = scale_snap(t, (int32_t)note);         /* (SCALE LOCK: the keyboard's note into the scale) */
        if (&trk[tr] != t) {
            char b[2] = {(char)('1' + tr), 0};
            ctl_toast("MIDI PLAYS TRACK ", b);
            continue;
        }
        ctl_mi_held[note >> 5] |= 1u << (note & 31u);
        if (!ui.entry_open) {
            ui.entry_open = 1;
            undo_mark(t, (undo_sess += 4u) | 3u);
            st->n = 0;                                   /* (a new step: no recorded timing) */
            st->lvl = st->rat = 0;
            st->time = ST_NOTE;
            t->roff[ui.cursor % NSTEP] = 0;
            t->seq_active = 1;
        }
        if (t->p[P_VOICE]) {
            st->note[0] = (uint8_t)note;
            st->n = 1;
        } else if (st->n < 4u) {
            st->note[st->n++] = (uint8_t)note;
        }
        last_note = (uint8_t)note;
    }
}
/* STEP page entry, acid style (SLOOP's seq_entry): the keys pressed together (a mono part: the last one) become the
 * cursor's step; letting go of every key moves on. Not while recording or armed: the keys record live then */
static void ctl_step_entry(uint32_t pressed)
{
    track_t *t = TSEL;
    step_t *st = &t->step[ui.cursor % NSTEP];
    uint32_t k;
    if (song.rec || rec_wait || ft_on)
        return;
    for (k = 0; k < 27u; k++) {
        uint32_t note;
        if (!((pressed >> k) & 1u))
            continue;
        note = kb_map(t, k);
        if (note == KB_SILENT)
            continue;
        if (!ui.entry_open) {
            ui.entry_open = 1;
            undo_mark(t, (undo_sess += 4u) | 3u);
            st->n = 0;                                   /* (a new step: no recorded timing) */
            st->lvl = st->rat = 0;
            st->time = ST_NOTE;
            t->roff[ui.cursor % NSTEP] = 0;
            t->seq_active = 1;
        }
        if (t->p[P_CHORD] && !t->p[P_VOICE]) {           /* chord mode: the chord as it sounds, not only its root */
            uint8_t c[4];
            uint32_t m, j;
            if (kb_kind[k] == KS_NOTE && kb_n[k] && kb_root[k] == note) {
                m = kb_n[k] > 4u ? 4u : kb_n[k];
                memcpy(c, kb_nt[k], m);
            } else {
                m = chord_notes(t, note, c);
            }
            for (j = 0; j < m && st->n < 4u; j++)
                st->note[st->n++] = c[j];
        } else if (t->p[P_VOICE]) {
            st->note[0] = (uint8_t)note;
            st->n = 1;
        } else if (st->n < 4u) {
            st->note[st->n++] = (uint8_t)note;
        }
        last_note = (uint8_t)note;
    }
    if (ui.entry_open && !fm1_in.notes && !(ctl_mi_held[0] | ctl_mi_held[1] | ctl_mi_held[2] | ctl_mi_held[3]))
        cursor_set((int32_t)ui.cursor + 1);              /* (every key and MIDI note up: the next step) */
}

/* SEL + OCT-: FIT TO SCALE, the selected track's notes moved into the scale (rounded down, as the keys); one undo */
static void ctl_fit_scale(void)
{
    track_t *t = TSEL;
    uint32_t i, k, moved = 0;
    char b[16], *e;
    if (!scale_lock(t)) {
        ctl_toast(is_drum(t) ? "DRUMS: NO SCALE" : "SCALE LOCK IS OFF", "");
        return;
    }
    undo_mark(t, (undo_sess += 4u) | 3u);
    fm1_irq_off();
    for (i = 0; i < NSTEP; i++)
        for (k = 0; k < t->step[i].n && k < 4u; k++) {
            uint8_t n = (uint8_t)scale_snap(t, t->step[i].note[k]);
            moved += n != t->step[i].note[k];
            t->step[i].note[k] = n;
        }
    fm1_irq_on();
    e = ds_cat(b, N_NOTE[(uint32_t)t->p[P_ROOT] % 12u]);
    e = ds_cat(e, " ");
    ds_cat(e, N_SCALE[clamp(t->p[P_SCALE], 0, NSCALES - 1)]);
    ctl_toast(moved ? "FITTED TO " : "ALREADY IN ", b);
}

/* CLIPS: launch clip k of the selected track; playing, it starts on the next bar */
static void ctl_clip_launch(uint32_t k)
{
    char b[2] = {(char)('1' + k % NCLIP), 0};
    clip_launch(song.sel, k % NCLIP);
    ctl_toast(song.playing ? "NEXT BAR: CLIP " : "CLIP ", b);
}
/* SAVE (CLIPS) held + a key (ui_layers.c layer_key): white keys 1..6 the selected track's clips, 9..14 the scenes */
static void ctl_clip_key(int32_t w)
{
    char b[2] = {0, 0};
    if (w >= 0 && w < (int32_t)NCLIP) {
        ctl_clip_launch((uint32_t)w);
    } else if (w >= 8 && w < 8 + (int32_t)NCLIP) {
        b[0] = (char)('1' + (w - 8));
        clip_scene((uint32_t)(w - 8));
        ctl_toast(song.playing ? "NEXT BAR: SCENE " : "SCENE ", b);
    }
    ctl.used[B_SAVE] = 1;
}
/* CLIPS screen, EDIT: the clip playing copied into the clip the cursor is on (asks before replacing one) */
static void ctl_clip_copy(void)
{
    uint32_t to = ctl.row[SC_CLIPS] % NCLIP, from = clip_playing(song.sel);
    char t[24] = "REPLACE CLIP 1?";
    if (from == to) {
        ctl_toast("PICK ANOTHER CLIP", "");
    } else if (from >= NCLIP || !clip_holds(song.sel, from)) {
        ctl_toast("NOTHING TO COPY", "");
    } else if (clip_holds(song.sel, to)) {
        t[13] = (char)('1' + to);
        ctl_arm(ACT_CLIPCOPY, B_EDIT, to, t, "THE CLIP PLAYING GOES OVER IT", "EDIT COPY");
    } else {
        ctl_do(ACT_CLIPCOPY, to);
    }
}
/* CLIPS screen, REC: the clip the cursor is on cleared (asks) */
static void ctl_clip_clear(void)
{
    uint32_t k = ctl.row[SC_CLIPS] % NCLIP;
    char t[24] = "CLEAR CLIP 1?";
    if (!clip_holds(song.sel, k)) {
        ctl_toast("EMPTY CLIP", "");
        return;
    }
    t[11] = (char)('1' + k);
    ctl_arm(ACT_CLIPCLEAR, B_REC, k, t, "ITS PATTERN GOES, NO UNDO", "REC CLEAR");
}

/* a button tapped (released quickly, nothing touched while held) */
static void ctl_tap(uint32_t b)
{
    switch (b) {
    case B_HOME: screen_go(SC_PLAY); break;
    case B_FX: screen_go(SC_PERFORM); break;
    case B_SCL: screen_go(SC_TRACKS); break;
    case B_ENV: screen_go(SC_SOUND); ctl.page[SC_SOUND] = (uint8_t)(is_drum(TSEL) ? 0u : 1u); break;
    case B_LFO: screen_go(SC_SOUND); ctl.page[SC_SOUND] = (uint8_t)(is_drum(TSEL) ? 0u : 2u); break;
    case B_EDIT:
        if (ctl.menu && ctl.screen == SC_PROJECT) {
            char t[24] = "LOAD PROJECT 1?";
            t[13] = (char)('1' + ctl.row[SC_PROJECT]);
            if (fmp_used(ctl.row[SC_PROJECT]))
                ctl_arm(ACT_LOAD, B_EDIT, ctl.row[SC_PROJECT], t, "THE WORK IN PROGRESS GOES", "EDIT LOAD");
            else
                ctl_toast("EMPTY SLOT", "");
            break;
        }
        if (!ctl.menu && ctl.screen == SC_CLIPS) {
            ctl_clip_copy();
            break;
        }
        if (ctl_roll()) {                                /* STEP page: EDIT clears the cursor's step, moves on */
            undo_mark(TSEL, (undo_sess += 4u) | 3u);
            step_clear(&TSEL->step[ui.cursor % NSTEP]);
            cursor_set((int32_t)ui.cursor + 1);
            ctl_toast("STEP CLEARED", "");
            break;
        }
        screen_go(SC_SOUND);
        break;
    case B_GLO: screen_go(SC_MIXER); break;
    case B_SAVE:
        if (ctl.menu && ctl.screen == SC_PROJECT) {
            uint32_t r = ctl.row[SC_PROJECT];
            if (song.playing || transport_req) {
                ctl_toast("STOP BEFORE SAVE", "");
            } else if (fmp_used(r)) {
                char t[24] = "OVERWRITE 1?";
                t[10] = (char)('1' + r);
                ctl_arm(ACT_SAVE, B_SAVE, r, t, "THE SAVED PROJECT GOES", "SAVE AGAIN");
            } else {
                ctl_do(ACT_SAVE, r);
            }
            break;
        }
        if (!ctl.menu && ctl.screen == SC_CLIPS) {       /* CLIPS again: the clip the cursor is on */
            ctl_clip_launch(ctl.row[SC_CLIPS]);
            break;
        }
        screen_go(SC_CLIPS);
        break;
    case B_ARP: screen_go(SC_GROOVE); break;
    case B_SEQ:
        screen_go(SC_SEQ);
        if (is_drum(TSEL))
            ctl.page[SC_SEQ] = 0;                        /* (the drum track: its grid only) */
        else if (ctl.page[SC_SEQ])
            cursor_set(ui.cursor);                       /* (onto the pattern, a new entry) */
        break;
    default: break;
    }
}

/* ------------------------------------------------------------------ input --- */
static uint32_t ctl_btn_bit(uint32_t label) { return 1u << panel.btn[label]; }

static void ui_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), b, k, held = LY_PLAY, l;
    int32_t s;
    static uint32_t home_t0;
    static uint8_t home_hold_done;
    static uint32_t clip_ms;
    enc_hold = 0;
    if (pressed || notes)
        ui_input_ms = fm1_ms;
    if (fm1_ms - clip_ms >= 10u) {                       /* the clips follow their patterns (clip.c) */
        clip_ms = fm1_ms;
        clip_tick();
    }
    fm6_poll();
    ctl_messages();
    if (ctl.arm && fm1_ms - ctl.arm_ms > CTL_ARM_MS)
        ctl.arm = ACT_NONE;                              /* nobody confirmed: nothing happens */
    if (!ctl.arm)
        ctl.kind_req = TK_COUNT;                         /* (a kind asked for lives as long as its question) */
    /* a pending confirmation: its button confirms, any other cancels (and does only that) */
    if (ctl.arm && pressed) {
        uint32_t ok = pressed & ctl_btn_bit(ctl.arm_btn);
        uint32_t act = ctl.arm, arg = ctl.arm_arg;
        ctl.arm = ACT_NONE;
        if (ok)
            ctl_do(act, arg);
        else
            ctl_toast("CANCELLED", "");
        ctl.kind_req = TK_COUNT;
        for (b = 0; b < NB; b++)
            ctl.used[b] = 1;                             /* (the press is used: no tap on its release) */
        pressed = 0;
    }
    /* HOME: a tap goes to PLAY, held 0.7 s opens / turns the system pages */
    if ((fm1_in.buttons & ctl_btn_bit(B_HOME)) != 0u) {
        if (!home_t0) {
            home_t0 = fm1_ms | 1u;
            home_hold_done = 0;
        } else if (!home_hold_done && fm1_ms - (home_t0 & ~1u) > 700u) {
            home_hold_done = 1;
            if (!ctl.menu) {
                ctl.menu = 1;
                ctl.screen = SC_PROJECT;
            } else {
                ctl.screen = ctl.screen == SC_PROJECT ? SC_SETTINGS : SC_PROJECT;
            }
            ctl.hot = -1;
        }
    } else if (home_t0) {
        if (!home_hold_done)
            ctl_tap(B_HOME);
        home_t0 = 0;
    }
    pressed &= ~ctl_btn_bit(B_HOME);
    /* the layers: which function button is held (its keys run in seq.c, the UI ones come through lk_q) */
    for (l = LY_FX; l < LY_COUNT; l++)
        if ((fm1_in.buttons & ly_bit[l]) && held == LY_PLAY)
            held = l;
    punch.hold = (uint8_t)(held == LY_FX);
    ui.layer = (uint8_t)held;
    {   /* FX + OCT- / OCT+ held (2.6): REV THROW / ECHO THROW, every send full while held (fx.c) */
        uint32_t q = 0;
        if (held == LY_FX) {
            q = ((fm1_in.buttons >> panel.btn[B_OCTDN]) & 1u) | ((fm1_in.buttons >> panel.btn[B_OCTUP]) & 1u) << 1;
            if (q)
                ctl.used[B_FX] = 1;
        }
        thr_req = (uint8_t)q;
    }
    ml_poll();                                           /* MIDI LEARN: a CC came for the picked parameter (2.6) */
    pc_poll();                                           /* MIDI Program Change: a track's sound (2.6, ui.c) */
    ctl_batt_watch();
    ctl_midi_step_in();                                  /* the STEP page from a MIDI keyboard (2.6) */
    if (ins_full) {
        ins_full = 0;
        ctl_toast("INSERT: 4 TRACKS MAX", "");
    }
    while (lk_r != lk_w) {                               /* SEQ, SEL, GLO keys (seq.c lk_q) */
        uint32_t e = lk_q[lk_r % LKQ];
        lk_r++;
        if ((e >> 8) < LY_COUNT && (held != LY_PLAY || !((e >> 7) & 1u)))
            layer_key(e >> 8, e & 31u, (e >> 7) & 1u);
        for (b = 0; b < NB; b++)
            if (fm1_in.buttons & ctl_btn_bit(b))
                ctl.used[b] = 1;
    }
    if (notes)
        for (b = 0; b < NB; b++)
            if (fm1_in.buttons & ctl_btn_bit(b))
                ctl.used[b] = 1;
    /* buttons: press time, taps on release */
    for (b = 0; b < NB; b++) {
        uint32_t bit = ctl_btn_bit(b), down = (fm1_in.buttons & bit) != 0u;
        if (b == B_HOME || b == B_PLAY || b == B_REC || b == B_OCTDN || b == B_OCTUP)
            continue;
        if (pressed & bit) {
            ctl.t0[b] = fm1_ms | 1u;
            ctl.used[b] = 0;
        }
        if (!down && ctl.t0[b]) {
            if (!ctl.used[b] && fm1_ms - (ctl.t0[b] & ~1u) < CTL_TAP_MS)
                ctl_tap(b);
            ctl.t0[b] = 0;
        }
    }
    /* transport, record, octave */
    if (pressed & ctl_btn_bit(B_PLAY)) {
        if (!ft_owns_press())
            transport_req = song.playing ? 2 : 1;
    }
    if ((pressed & ctl_btn_bit(B_REC)) && ctl.menu && ctl.screen == SC_PROJECT) {   /* PROJECT: REC deletes (asks) */
        char t[24] = "DELETE PROJECT 1?";
        t[15] = (char)('1' + ctl.row[SC_PROJECT]);
        if (fmp_used(ctl.row[SC_PROJECT]))
            ctl_arm(ACT_DELETE, B_REC, ctl.row[SC_PROJECT], t, "IT CANNOT BE UNDONE", "REC DELETE");
        pressed &= ~ctl_btn_bit(B_REC);
    }
    if ((pressed & ctl_btn_bit(B_REC)) && !ctl.menu && ctl.screen == SC_CLIPS && held == LY_PLAY) {   /* CLIPS: clear */
        ctl_clip_clear();
        pressed &= ~ctl_btn_bit(B_REC);
    }
    if (!song.playing)
        rec_pend = 0;                                    /* (stopped: nothing waits for a cycle) */
    if (pressed & ctl_btn_bit(B_REC)) {
        if (!ft_owns_press()) {
            if (rec_pend) {                              /* REC again before the cycle: never mind */
                ctl_toast(rec_pend == REC_PEND_ON ? "REC CANCELLED" : "REC GOES ON", "");
                rec_pend = 0;
            } else if (song.rec && song.playing && rec_q) {   /* the take ends when the cycle closes */
                rec_pend = REC_PEND_OFF;
                ctl_toast("REC STOPS ON THE BAR", "");
            } else if (song.rec || rec_wait) {
                song.rec = 0;
                rec_wait = 0;
                ctl_toast("REC OFF", "");
            } else if (song.playing && rec_q) {         /* the take starts on the next cycle */
                rec_pend = REC_PEND_ON;
                ctl_toast("REC STARTS ON THE BAR", "");
            } else if (song.playing) {
                rec_begin();
            } else {
                rec_wait = 1;                            /* stopped: the first note (or PLAY) starts it */
            }
        }
    }
    if (held == LY_PLAY && (pressed & (ctl_btn_bit(B_OCTDN) | ctl_btn_bit(B_OCTUP))) &&
        (fm1_in.buttons & (ctl_btn_bit(B_OCTDN) | ctl_btn_bit(B_OCTUP))) == (ctl_btn_bit(B_OCTDN) | ctl_btn_bit(B_OCTUP))) {
        tap_tempo();                                     /* OCT- + OCT+ together, tapped in time: TAP TEMPO (the octave
                                                          * goes back to 0 as ever) */
        ctl.tempo_ms = fm1_ms | 1u;
    }
    if (held == LY_SCALE && (pressed & ctl_btn_bit(B_OCTDN))) {   /* SEL + OCT-: FIT TO SCALE */
        ctl.used[B_SCL] = 1;
        ctl_fit_scale();
        pressed &= ~ctl_btn_bit(B_OCTDN);
    }
    if (held == LY_ERASE && (pressed & (ctl_btn_bit(B_OCTDN) | ctl_btn_bit(B_OCTUP)))) {   /* EDIT + OCT: undo / redo */
        ctl.used[B_EDIT] = 1;
        int redo = !(pressed & ctl_btn_bit(B_OCTDN));
        int mvd = smove.sess && undo.valid && undo.sess == smove.sess && undo.trk == smove.trk;
        if (undo_swap(redo)) {
            if (mvd)
                ctl_step_move_undo(redo);
            ctl_toast(redo ? "REDO" : "UNDO", "");
        } else {
            ctl_toast(redo ? "NOTHING TO REDO" : "NOTHING TO UNDO", "");
        }
    } else if (held == LY_STEP && (pressed & (ctl_btn_bit(B_OCTDN) | ctl_btn_bit(B_OCTUP)))) {   /* SEQ + OCT: page */
        uint32_t pages = (trk_len(TSEL) + 15u) / 16u;
        ctl.used[B_SEQ] = 1;
        if (ui.step_held)
            (pressed & ctl_btn_bit(B_OCTDN)) ? steps_held_clear() : steps_held_fill();
        else
            ui.step_page = (uint8_t)((ui.step_page + ((pressed & ctl_btn_bit(B_OCTUP)) ? 1u : pages - 1u)) % pages);
    } else if (held != LY_FX && !is_drum(TSEL) && (pressed & (ctl_btn_bit(B_OCTDN) | ctl_btn_bit(B_OCTUP)))) {
        uint32_t both = ctl_btn_bit(B_OCTDN) | ctl_btn_bit(B_OCTUP);
        if ((fm1_in.buttons & both) == both)
            song.octave = 0;
        else if (pressed & ctl_btn_bit(B_OCTDN))
            song.octave = (int8_t)(song.octave > -3 ? song.octave - 1 : song.octave);
        else
            song.octave = (int8_t)(song.octave < 3 ? song.octave + 1 : song.octave);
    }
    if (ctl_roll()) {
        cursor_fix();                                    /* (LENGTH shorter, another track: onto its last step) */
        if (held == LY_PLAY)
            ctl_step_entry(notes);
    }
    /* the encoders */
    if (held != LY_PLAY) {                               /* a layer held: SLOOP's layer knobs (steps, punch, erase ..) */
        uint32_t lb = LAYER_BTN[held];
        ui.layer_used = 0;
        layer_knobs(held);
        if (ui.layer_used)
            ctl.used[lb] = 1;
        if (ui.hot_t) {
            ctl.hot = (int8_t)ui.hot_col;
            ctl.hot_ms = fm1_ms;
            ui.hot_t = 0;
        }
        if ((s = panel_enc(EN_SELECT)) != 0) {          /* SELECT with a button held: the tempo, one BPM a detent (as SLOOP) */
            ctl.tempo_ms = fm1_ms | 1u;
            char bpm[8];
            song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + s, GP[G_BPM].min, GP[G_BPM].max);
            ctl.used[lb] = 1;
            fmt_int(bpm, song.g[G_BPM]);
            ctl_toast("TEMPO ", bpm);
        }
        if (!(held == LY_STEP && ui.step_held)) {
            panel_enc(EN_ALGO);
            panel_enc(EN_PRESET);
        }
        return;
    }
    if ((s = panel_enc(EN_SELECT)) != 0 && !ft_on)       /* SELECT: the track, everywhere (one detent, one track) */
        track_select((uint32_t)clamp((int32_t)song.sel + s, 0, NTRK - 1));
    if ((s = panel_enc(EN_PRESET)) != 0) {               /* PRESETS: the sound, everywhere */
        if (is_drum(TSEL)) {
            TSEL->p[P_E0] = (int16_t)clamp(TSEL->p[P_E0] + s, 0, DRUM_KITS - 1);
        } else if (is_synth(TSEL)) {
            uint32_t total, cur = preset_pos(&total);
            if (total)
                preset_go((uint32_t)(((int32_t)cur + s % (int32_t)total + (int32_t)total) % (int32_t)total));
        }
    }
    if ((s = panel_enc(EN_ALGO)) != 0) {                 /* ALGORITHM: the screen's navigator */
        uint32_t sc = ctl.screen;
        if (!ctl.menu && (sc == SC_PLAY || sc == SC_TRACKS || sc == SC_MIXER || sc == SC_PERFORM)) {
            /* the screens with nothing to navigate: the TEMPO, one hand, no button (fast turns go further) */
            song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_ALGO, s, GP[G_BPM].max - GP[G_BPM].min),
                                           GP[G_BPM].min, GP[G_BPM].max);
            ctl.tempo_ms = fm1_ms | 1u;
        } else if (ctl.menu) {
            uint32_t n = sc == SC_PROJECT ? FMP_NPROJ : ST_COUNT;
            ctl.row[sc] = (uint8_t)clamp((int32_t)ctl.row[sc] + s, 0, (int32_t)n - 1);
        } else if (ctl_roll()) {                         /* STEP page: on a note MOVE it, else the cursor */
            if (!ctl_step_move(s))
                cursor_set((int32_t)ui.cursor + s);
        } else if (sc == SC_SEQ) {
            uint32_t pages = (trk_len(TSEL) + 15u) / 16u;
            ui.step_page = (uint8_t)clamp((int32_t)ui.step_page + s, 0, (int32_t)pages - 1);
        } else if (sc == SC_GROOVE) {                    /* the template (and the swing it suggests) */
            groove_t *g = ctl_groove_own();
            g->tmpl = (uint8_t)clamp((int32_t)g->tmpl + s, 0, GT_COUNT - 1);
            TSEL->p[P_SSWING] = GROOVES[g->tmpl].swing;
            if (!g->amt && g->tmpl != GT_STRAIGHT)
                g->amt = g->vamt = 100;
        } else if (sc == SC_SOUND && is_synth(TSEL)) {   /* the engine */
            select_engine((uint32_t)clamp((int32_t)TSEL->eng_req + s, 0, NENGINES - 1));
        } else if (sc == SC_CLIPS) {                     /* the clip cursor */
            ctl.row[sc] = (uint8_t)clamp((int32_t)ctl.row[sc] + s, 0, NCLIP - 1);
        }
    }
    for (k = 0; k < 4u; k++) {
        kbind_t kb;
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        kb = ctl_bind(k);
        ctl.hot = (int8_t)k;
        ctl.hot_ms = fm1_ms;
        switch (kb.kind) {
        case KB_TP:
        case KB_MACRO:
        case KB_KIT: {
            int16_t *vp;
            const param_desc_t *d = ctl_tp(TSEL, kb, &vp);
            if (d) {
                *vp = (int16_t)clamp(*vp + accel(EN_K1 + k, s, d->max - d->min), d->min, d->max);
                ml_knob(TSEL, (uint32_t)(vp - TSEL->p));      /* MIDI LEARN (2.6): this parameter */
                if (kb.kind == KB_MACRO && p_lockable(ENGINES[TSEL->eng_req % NENGINES]->macro[k]))
                    ui.lock_par = ENGINES[TSEL->eng_req % NENGINES]->macro[k];
            }
            break;
        }
        case KB_GP:
            song.g[kb.id] = (int16_t)clamp(song.g[kb.id] + accel(EN_K1 + k, s, GP[kb.id].max - GP[kb.id].min),
                                           GP[kb.id].min, GP[kb.id].max);
            break;
        case KB_GRV_Q:
            groove_set_strength(&TSEL->gv, (uint32_t)clamp((int32_t)groove_strength(&TSEL->gv) + accel(EN_K1 + k, s, 100), 0, 100));
            break;
        case KB_GRV_AMT: {
            groove_t *g = ctl_groove_own();
            g->amt = g->vamt = (uint8_t)clamp((int32_t)g->amt + accel(EN_K1 + k, s, 100), 0, 100);
            break;
        }
        case KB_GRV_HUM: {
            groove_t *g = &TSEL->gv;
            g->hum_t = g->hum_v = (uint8_t)clamp((int32_t)g->hum_t + accel(EN_K1 + k, s, 100), 0, 100);
            break;
        }
        case KB_SHIFT:
            pattern_rotate(TSEL, s);
            break;
        case KB_KIND: {                                  /* asks; SEL (the screen's button) confirms */
            uint32_t cur = ctl.kind_req < TK_COUNT ? ctl.kind_req : sel_kind();
            static const char *const KN[TK_COUNT] = {"SYNTH", "DRUMS", "MIDI"};
            char t[24] = "TRACK 1 TO ";
            cur = cur == TK_SYNTH ? TK_DRUM : TK_SYNTH;  /* (MIDI-out tracks: next milestone) */
            ctl.kind_req = (uint8_t)cur;
            t[6] = (char)('1' + song.sel);
            str_cpy(t + 11, KN[cur], sizeof t - 11);
            if (cur != sel_kind())
                ctl_arm(ACT_KIND, B_SCL, song.sel, t, "ITS PATTERN IS CLEARED", "SEL CONFIRM");
            break;
        }
        case KB_SET:
            st_turn(ctl.row[SC_SETTINGS], s);
            break;
        case KB_STEP:
            ctl_step_edit(kb.id, s);
            break;
        case KB_CLIP:
            ctl.row[SC_CLIPS] = (uint8_t)clamp((int32_t)ctl.row[SC_CLIPS] + s, 0, NCLIP - 1);
            break;
        default:
            break;
        }
    }
}

/* -------------------------------------------------------------- the LEDs --- */
static uint8_t ctl_led_pos[41];
static void ctl_led_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < 41u; id++) {
        ctl_led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    ctl_led_pos[id] = (uint8_t)((p << 3) | r);
    }
}
static void ctl_led(uint8_t *nl, uint32_t id, int on)
{
    uint8_t q = ctl_led_pos[id % 41u];
    if (q != 0xFF && on)
        nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
}
static const uint8_t SC_BTN[SC_COUNT] = {B_HOME, B_SCL, B_SEQ, B_ARP, B_SAVE, B_GLO, B_EDIT, B_FX, B_HOME, B_HOME};

static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, dl[FM1_NCOL] = {0};
    uint32_t k, c, keys = 0, dim = 0, beat;
    static uint8_t ready;
    if (!ready) {
        ctl_led_init();
        ready = 1;
    }
    beat = song.playing && clk_pos < BEAT_U / 4u;
    ctl_led(nl, panel.btn[ui.layer != LY_PLAY ? LAYER_BTN[ui.layer] : SC_BTN[ctl.screen % SC_COUNT]], 1);
    ctl_led(nl, panel.btn[B_PLAY], (int)beat);
    ctl_led(nl, panel.btn[B_REC], (song.rec != 0u && !rec_pend) || ft_on || ((rec_wait || rec_pend) && ((fm1_ms / 125u) & 1u)));
    if (ui.layer == LY_FX) {                             /* FX held: the THROWs */
        ctl_led(nl, panel.btn[B_OCTDN], (thr_req & 1u) != 0u);
        ctl_led(nl, panel.btn[B_OCTUP], (thr_req & 2u) != 0u);
    } else if (is_drum(TSEL)) {
        ctl_led(nl, panel.btn[B_OCTDN], (fm1_in.buttons & dyn_bit[0]) != 0u);
        ctl_led(nl, panel.btn[B_OCTUP], (fm1_in.buttons & dyn_bit[1]) != 0u);
    } else {
        ctl_led(nl, panel.btn[B_OCTDN], song.octave < 0);
        ctl_led(nl, panel.btn[B_OCTUP], song.octave > 0);
    }
    if (ui.layer == LY_STEP) {                           /* the steps of the page, the playhead */
        const track_t *t = TSEL;
        uint32_t w, base = (uint32_t)ui.step_page * 16u, len = trk_len(t);
        for (w = 0; w < 16u && base + w < len; w++) {
            uint32_t on = is_drum(t) ? dstep_has(&t->dstep[base + w], pen_lane) : step_on(&t->step[base + w]);
            uint32_t ph = song.playing && t->seq_idx == base + w;
            if (on ^ ph)
                keys |= 1u << key_of_white(w);
            else
                dim |= 1u << key_of_white(w);
        }
    } else if (ui.layer == LY_SCALE) {                   /* SEL: the tracks */
        for (k = 0; k < NTRK; k++) {
            if (k == song.sel)
                keys |= 1u << key_of_white(k);
            else
                dim |= 1u << key_of_white(k);
        }
    } else if (ui.layer == LY_SONG) {                    /* CLIPS held: 1-6 the track's clips, 9-14 the scenes */
        uint32_t blink = (fm1_ms / 125u) & 1u, i, any, all;
        for (k = 0; k < NCLIP; k++) {
            uint32_t st = clip_state(song.sel, k), holds = clip_holds(song.sel, k);
            if (st == 3u ? blink : st == 2u)
                keys |= 1u << key_of_white(k);          /* playing: lit; asked for: blinking */
            else if (holds)
                dim |= 1u << key_of_white(k);
            for (i = 0, any = 0, all = 1; i < NTRK; i++) {
                any |= clip_holds(i, k);
                all &= clip_playing(i) == k;
            }
            if (all)
                keys |= 1u << key_of_white(8u + k);     /* the scene playing */
            else if (any)
                dim |= 1u << key_of_white(8u + k);
        }
    } else if (ui.layer == LY_MIX) {                     /* GLO: 1-8 heard, 9-16 soloed */
        for (k = 0; k < NTRK; k++) {
            if (!trk[k].p[P_MUTE])
                keys |= 1u << key_of_white(k);
            if ((song.solo >> k) & 1u)
                keys |= 1u << key_of_white(8u + k);
        }
    } else if (ui.layer == LY_PLAY && scale_lock(TSEL)) {   /* SCALE LOCK: the scale's keys glow, its roots lit, the
                                                          * keys held lit; the others stay dark */
        const track_t *t = TSEL;
        int white = t->p[P_QUANT] == 2 || t->p[P_CHORD];  /* (WHITE, chord mode: the white keys walk the scale from
                                                          * C = the root; the black keys play no note) */
        for (k = 0; k < 27u; k++) {
            int32_t n = 53 + (int32_t)k + t->p[P_TRANS];
            uint32_t pc = (53u + k) % 12u, isw = !((0x54Au >> pc) & 1u);
            int on = white ? (int)isw : in_scale(t, n), root = white ? isw && pc == 0u : (n - t->p[P_ROOT] + 1200) % 12 == 0;
            if ((fm1_in.notes >> k) & 1u || (on && root))
                keys |= 1u << k;
            else if (on)
                dim |= 1u << k;
        }
    }
    for (k = 0; k < 27u; k++) {
        ctl_led(nl, 14u + k, (int)((keys >> k) & 1u));
        ctl_led(dl, 14u + k, (int)((dim >> k) & 1u));
    }
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led[c] = nl[c];
        fm1_led_dim[c] = dl[c];
        fm1_led_bg[c] = 0;
    }
    fm1_led_bg_ns = LIGHTS_NS[lights_lvl % LIGHTS_N];
}

/* -------------------------------------------------------- the view model --- */
static void mv_knob_b(uint32_t k, kbind_t kb);
static void mv_knob(uint32_t k) { mv_knob_b(k, ctl_bind(k)); }
static void mv_knob_b(uint32_t k, kbind_t kb)
{
    mv_knob_t *o = &mv.k[k];
    o->label = kb.label;
    o->value[0] = 0;
    o->ratio = -1;
    o->bipolar = 0;
    switch (kb.kind) {
    case KB_TP:
    case KB_MACRO:
    case KB_KIT: {
        int16_t *vp;
        const char *unit = 0;
        const param_desc_t *d = ctl_tp(TSEL, kb, &vp);
        if (!d) {
            o->label = 0;
            break;
        }
        if (kb.kind == KB_MACRO || !kb.label)
            o->label = d->label;
        if (kb.kind == KB_KIT)
            str_cpy(o->value, DRUM_KIT_NAMES[clamp(*vp, 0, DRUM_KITS - 1)], sizeof o->value);
        else
            param_format(d, *vp, o->value, &unit);
        o->ratio = (int16_t)(d->max > d->min ? ((int32_t)*vp - d->min) * 1000 / (d->max - d->min) : -1);
        o->bipolar = d->min < 0;
        if ((int32_t)k == ctl.hot)
            str_cpy(mv.hot_unit, unit ? unit : "", sizeof mv.hot_unit);
        break;
    }
    case KB_GP: {
        const char *unit = 0;
        const param_desc_t *d = &GP[kb.id];
        param_format(d, song.g[kb.id], o->value, &unit);
        o->ratio = (int16_t)((song.g[kb.id] - d->min) * 1000 / (d->max - d->min));
        o->bipolar = d->min < 0;
        break;
    }
    case KB_GRV_Q:
        ds_cat(ds_itoa(o->value, (int32_t)groove_strength(&TSEL->gv)), "%");
        o->ratio = (int16_t)(groove_strength(&TSEL->gv) * 10u);
        break;
    case KB_GRV_AMT: {
        const groove_t *g = TSEL->gv.own ? &TSEL->gv : &song_groove;
        ds_cat(ds_itoa(o->value, g->amt), "%");
        o->ratio = (int16_t)(g->amt * 10u);
        break;
    }
    case KB_GRV_HUM:
        ds_cat(ds_itoa(o->value, TSEL->gv.hum_t), "%");
        o->ratio = (int16_t)(TSEL->gv.hum_t * 10u);
        break;
    case KB_SHIFT:
        str_cpy(o->value, "< >", sizeof o->value);
        break;
    case KB_KIND: {
        static const char *const KN[TK_COUNT] = {"SYNTH", "DRUMS", "MIDI"};
        str_cpy(o->value, KN[(ctl.kind_req < TK_COUNT ? ctl.kind_req : sel_kind()) % TK_COUNT], sizeof o->value);
        break;
    }
    case KB_SET:
        st_value(ctl.row[SC_SETTINGS], o->value);
        break;
    case KB_STEP: {
        static const char *const TIME_N[3] = {"NOTE", "TIE", "REST"};
        static const char *const FLAG_N[4] = {"-", "ACC", "SLIDE", "A+S"};
        const step_t *st = &TSEL->step[ui.cursor % NSTEP];
        char *e;
        switch (kb.id & 3u) {
        case 0:
            e = ds_itoa(o->value, (int32_t)ui.cursor + 1);
            e = ds_cat(e, "/");
            ds_itoa(e, (int32_t)trk_len(TSEL));
            o->ratio = (int16_t)(trk_len(TSEL) > 1u ? ui.cursor * 1000u / (trk_len(TSEL) - 1u) : 0u);
            break;
        case 1:
            if (st->n && st->time == ST_NOTE) {
                note_name(o->value, st->note[0]);
                if (st->n > 1) {
                    e = ds_cat(o->value + str_len(o->value), "+");
                    ds_itoa(e, st->n - 1);
                }
            } else {
                str_cpy(o->value, "--", sizeof o->value);
            }
            break;
        case 2:
            if (step_on(st)) {                           /* a note: LEN, its steps (2.6) */
                o->label = "LEN";
                ds_itoa(o->value, (int32_t)note_len(TSEL, ui.cursor % NSTEP));
            } else {
                str_cpy(o->value, TIME_N[st->time % 3u], sizeof o->value);
            }
            break;
        default:
            str_cpy(o->value, FLAG_N[(st->flags & SF_ACCENT ? 1u : 0u) | (st->flags & SF_SLIDE ? 2u : 0u)], sizeof o->value);
            break;
        }
        break;
    }
    case KB_CLIP:
        ds_itoa(o->value, (int32_t)(ctl.row[SC_CLIPS] % NCLIP) + 1);
        o->ratio = (int16_t)((ctl.row[SC_CLIPS] % NCLIP) * 1000u / (NCLIP - 1u));
        break;
    default:
        o->label = 0;
        break;
    }
}

/* a track's name: its kind, or its sound's family */
static void mv_name(uint32_t i, char *b, uint32_t n)
{
    const track_t *t = &trk[i];
    if (is_drum(t)) {
        str_cpy(b, "DRUMS", n);
    } else if (trk_kind(t) == TK_MIDI) {
        str_cpy(b, "MIDI", n);
    } else {
        uint32_t total, pos = preset_pos_of(t, &total);  /* (never song.sel = i: the audio ISR routes the keys by it) */
        str_cpy(b, pos < total ? preset_kind(pos) : ENGINES[t->eng_req % NENGINES]->name, n);
    }
}
static void mv_sound(uint32_t i, char *b, uint32_t n)
{
    const track_t *t = &trk[i];
    if (is_drum(t))
        str_cpy(b, DRUM_KIT_NAMES[drum_kit_of(t)], n);
    else if (t->user && user_of(t) < UP_SLOTS)
        up_name(user_of(t), b);
    else {
        const engine_t *e = ENGINES[t->eng_req % NENGINES];
        str_cpy(b, e->npresets ? e->presets[t->preset % e->npresets].name : e->name, n);
    }
}

static uint32_t lvk(uint32_t lv)                         /* a step level -> the drawing's */
{
    return lv == LV_GHOST ? LVK_GHOST : lv == LV_SOFT ? LVK_SOFT : lv == LV_HARD ? LVK_HARD : LVK_NORM;
}

/* a layer held long enough (or used): SEQ shows STEPS, SEL shows KEY over the screen (SLOOP's layer pages). A tap
 * (the button's own screen) never flashes it; on the SEQUENCER the steps are the screen already */
#define CTL_LAYER_MS 250u
static void mv_layer(void)
{
    const track_t *ts = TSEL;
    uint32_t b = ui.layer == LY_STEP ? B_SEQ : ui.layer == LY_SCALE ? B_SCL : NB, i;
    char *e;
    if (b == NB || ctl.arm || (b == B_SEQ && !ctl.menu && ctl.screen == SC_SEQ) ||
        (!ctl.used[b] && (!ctl.t0[b] || fm1_ms - (ctl.t0[b] & ~1u) < CTL_LAYER_MS)))
        return;
    mv.layer = (uint8_t)(b == B_SEQ ? MVL_STEPS : MVL_KEY);
    if (b == B_SEQ) {
        uint32_t len = trk_len(ts), base = (uint32_t)ui.step_page * 16u;
        mv.lay_page = ui.step_page;
        mv.lay_pages = (uint8_t)((len + 15u) / 16u);
        mv.lay_held = ui.step_held;
        mv.lay_ph = (uint8_t)(song.playing && ts->seq_idx / 16u == ui.step_page ? ts->seq_idx % 16u : 0xFFu);
        e = mv.lay_ctx;
        if (is_drum(ts)) {
            e = ds_cat(e, LANE_NAME[pen_lane & 15u]);
            e = ds_cat(e, "  ");
        }
        e = ds_cat(e, "LEN ");
        ds_itoa(e, (int32_t)len);
        for (i = 0; i < 16u && base + i < len; i++) {
            const step_t *st = &ts->step[base + i];
            uint32_t fl = step_locked(ts, base + i) || ts->micro[base + i] ? 8u : 0u;
            if (!is_drum(ts)) {
                if (step_on(st)) {
                    note_name(mv.lay_lab[i], st->note[0]);
                    if (st->n > 1u) {
                        e = ds_cat(mv.lay_lab[i] + str_len(mv.lay_lab[i]), "+");
                        ds_itoa(e, st->n - 1);
                    }
                    fl |= st->flags & SF_ACCENT ? RF_ACC : 0u;
                } else if (st->time == ST_TIE) {
                    fl |= RF_TIE;
                }
            }
            mv.lay_fl[i] = (uint8_t)fl;
        }
        if (ui.step_held) {                              /* the first step held: its values */
            uint32_t w, idx;
            int32_t m;
            for (w = 0; w < 16u && !((ui.step_held >> w) & 1u); w++)
                ;
            idx = (base + w) % NSTEP;
            m = ts->micro[idx];
            if (is_drum(ts)) {
                static const char *const LVN[4] = {"GHOST", "SOFT", "NORM", "HARD"};
                const dstep_t *d = &ts->dstep[idx];
                mv.k[0].label = "SOUND";
                str_cpy(mv.k[0].value, LANE_SHORT[pen_lane & 15u], sizeof mv.k[0].value);
                str_cpy(mv.k[1].value, dstep_has(d, pen_lane) ? LVN[lvl_rank(dstep_lvl(d, pen_lane))] : "--",
                        sizeof mv.k[1].value);
            } else {
                const step_t *st = &ts->step[idx];
                if (step_on(st))
                    note_name(mv.k[0].value, st->note[0]);
                else
                    str_cpy(mv.k[0].value, "--", sizeof mv.k[0].value);
                mv.k[1].label = "VELO";                  /* (2.6: the step's velocity, 1..127) */
                if (step_on(st))
                    ds_itoa(mv.k[1].value, (int32_t)step_vel_shown(st));
                else
                    str_cpy(mv.k[1].value, "--", sizeof mv.k[1].value);
                e = ds_cat(mv.k[2].value, "X");
                ds_itoa(e, (int32_t)(st->rat & 3u) + 1);
            }
            e = mv.k[3].value;
            if (m > 0)
                *e++ = '+';
            ds_itoa(e, m);
            mv.k[3].ratio = (int16_t)((m - MICRO_MIN) * 1000 / (MICRO_MAX - MICRO_MIN));
            mv.k[3].bipolar = 1;
        } else {
            if (is_drum(ts))
                mv.k[0].label = "SOUND", str_cpy(mv.k[0].value, LANE_SHORT[pen_lane & 15u], sizeof mv.k[0].value);
            else
                note_name(mv.k[0].value, pen_note[0]);
            mv_knob_b(1, (kbind_t){KB_TP, P_SDIV, "DIV"});
            mv_knob_b(2, (kbind_t){KB_TP, P_SSWING, "SWING"});
            mv_knob_b(3, (kbind_t){KB_TP, P_SLEN, "LENGTH"});
        }
    } else {
        const track_t *kr = key_ref();
        uint32_t root = (uint32_t)kr->p[P_ROOT] % 12u, sc = (uint32_t)clamp(kr->p[P_SCALE], 0, NSCALES - 1);
        uint32_t mask = SCALE_MASK[sc], chord = !is_drum(ts) && ts->p[P_CHORD];
        e = ds_cat(mv.lay_ctx, N_NOTE[root]);
        e = ds_cat(e, " ");
        e = ds_cat(e, N_SCALE[sc]);
        if (chord) {
            e = ds_cat(e, "  ");
            e = ds_cat(e, N_CHORD[clamp(ts->p[P_CHORD], 0, 5)]);
        }
        if (scale_lock(ts))
            ds_cat(e, "  LOCK");
        mv.lay_pages = 1;
        for (i = 0; i < 16u; i++) {
            uint32_t k = key_of_white(i), pc = (53u + k) % 12u, in = (mask >> ((pc + 12u - root) % 12u)) & 1u;
            str_cpy(mv.lay_lab[i], N_NOTE[pc], sizeof mv.lay_lab[i]);
            if (chord) {                                 /* the chord this key plays */
                uint8_t c[4];
                uint32_t nt = kb_map(ts, k), m;
                if (nt != KB_SILENT && (m = chord_notes(ts, nt, c)) != 0u) {
                    uint32_t third = m > 1u ? (uint32_t)(c[1] - c[0]) : 4u;
                    static const char *const Q[8] = {"", "", "5", " MIN", "", " SUS", "", "5"};
                    pc = c[0] % 12u;
                    in = 1;
                    e = ds_cat(mv.lay_lab[i], N_NOTE[pc]);
                    ds_cat(e, ts->p[P_CHORD] == 5 ? "5" : Q[third & 7u]);
                }
            }
            mv.lay_st[i] = (uint8_t)(pc == root ? 2u : in ? 1u : 0u);
        }
        str_cpy(mv.k[0].value, is_drum(ts) ? "--" : N_CHORD[clamp(ts->p[P_CHORD], 0, 5)], sizeof mv.k[0].value);
        mv.k[0].ratio = (int16_t)(is_drum(ts) ? -1 : ts->p[P_CHORD] * 200);
        str_cpy(mv.k[1].value, N_SCALE[sc], sizeof mv.k[1].value);
        mv.k[1].ratio = (int16_t)(sc * 1000u / (NSCALES - 1u));
        {
            static const char *const QN[3] = {"OFF", "LOCK", "WHITE"};   /* (SNAP: the SCALE LOCK) */
            str_cpy(mv.k[2].value, is_drum(ts) ? "--" : QN[clamp(ts->p[P_QUANT], 0, 2)], sizeof mv.k[2].value);
            mv.k[2].label = "SCALE LOCK";
        }
        if (!is_drum(ts))
            mv_knob_b(3, (kbind_t){KB_TP, P_TRANS, "TRANSP"});
        else
            str_cpy(mv.k[3].value, "--", sizeof mv.k[3].value);
    }
}

static void mv_fill(void)
{
    const track_t *ts = TSEL;
    uint32_t i, k;
    memset(&mv, 0, sizeof mv);
    mv.screen = ctl.screen;
    mv.sel = song.sel;
    for (i = 0; i < NTRK && i < MV_NTRK; i++) {
        track_t *t = &trk[i];
        mv_track_t *o = &mv.t[i];
        uint32_t a = (uint32_t)t->peak * 100u / 20000u;
        t->peak = 0;
        if (a > 100u)
            a = 100u;
        ctl.act[i] = (uint8_t)(a > ctl.act[i] ? a : ctl.act[i] > 6u ? ctl.act[i] - 6u : 0u);
        o->act = ctl.act[i];
        mv_name(i, o->name, sizeof o->name);
        mv_sound(i, o->sound, sizeof o->sound);
        str_cpy(o->engine, is_drum(t) ? "DRUMS" : ENGINES[t->eng_req % NENGINES]->name, sizeof o->engine);
        o->kind = (uint8_t)trk_kind(t);
        o->mute = (uint8_t)(t->p[P_MUTE] != 0);
        o->solo = (uint8_t)((song.solo >> i) & 1u);
        o->arm = (uint8_t)(((song.rec >> i) & 1u) || (rec_wait && i == song.sel));
        o->vol = (uint8_t)(t->p[P_LEVEL] * 100 / 127);
        o->pan = (int8_t)(t->p[P_PAN] * 50 / 64);
        for (k = 0, o->clips = 0, o->queued = 0xFF; k < NCLIP; k++) {
            o->clips |= (uint8_t)(clip_holds(i, k) << k);
            if (clip_state(i, k) == 3u)
                o->queued = (uint8_t)k;
        }
        o->clip = (uint8_t)clip_playing(i);
    }
    mv.playing = song.playing;
    mv.rec = (uint8_t)(song.rec != 0u && song.playing);
    mv.count_in = (uint8_t)(ci_on ? 4u - ci_beat : 0u);
    mv.bpm10 = (uint16_t)(song.g[G_BPM] * 10);
    mv.tempo_ov = (uint8_t)(ctl.tempo_ms && fm1_ms - (ctl.tempo_ms & ~1u) < CTL_HOT_MS + 300u);
    mv.tempo_ext = (uint8_t)(lights_sync != 0u);
    mv.quant_on = (uint8_t)(groove_strength(&ts->gv) > 0u);
    {
        int32_t l = ctl_batt();
        mv.batt_usb = (uint8_t)ctl_on_usb();
        mv.batt = mv.batt_usb ? (uint8_t)(1u + (fm1_ms / 600u) % 3u) : l < 0 ? MV_BATT_NONE : (uint8_t)l;
    }
    mv.groove_on = (uint8_t)(groove_active(&ts->gv, &song_groove) || ts->p[P_SSWING] || song.g[G_SWING]);
    mv.bar = (uint8_t)(song.playing ? (clk_beat / 4u) % 99u + 1u : 1u);
    mv.beat = (uint8_t)(song.playing ? clk_beat % 4u + 1u : 1u);
    {
        uint32_t lenb = trk_len(ts) * div_units(trk_div(ts)) / BEAT_U;
        mv.bars = (uint8_t)((lenb + 3u) / 4u ? (lenb + 3u) / 4u : 1u);
        if (song.playing)
            mv.bar = (uint8_t)((clk_beat / 4u) % mv.bars + 1u);
    }
    mv.step16 = (uint8_t)((clk_beat % 4u) * 4u + clk_pos * 4u / BEAT_U);
    mv.held = -1;
    mv.hot = ctl.hot >= 0 && fm1_ms - ctl.hot_ms < CTL_HOT_MS ? ctl.hot : -1;
    for (k = 0; k < 4u; k++)
        mv_knob(k);
    mv.nofocus = (uint8_t)(ctl_roll() || (!ctl.menu && ctl.screen == SC_CLIPS));
    if (ui.layer != LY_PLAY) {                           /* a layer held: what its knobs do */
        static const char *const LK[LY_COUNT][4] = {
            {0}, {"FILTER", "DUST", "DUCK", "TRK FLT"}, {"SHIFT", "LENGTH", "TRANSP", 0}, {"RATE", 0, 0, 0},
            {"NOTE", "DIV", "SWING", "LENGTH"}, {"CHORD", "SCALE", "KEYS", "TRANSP"}, {"VOLUME", "PAN", "REVERB", "DELAY"},
            {0}};
        static const char *const LKH[4] = {"NOTE", "LEVEL", "RATCHET", "NUDGE"};
        for (k = 0; k < 4u; k++) {
            mv.k[k].label = ui.layer == LY_STEP && ui.step_held ? LKH[k] : LK[ui.layer % LY_COUNT][k];
            mv.k[k].value[0] = 0;
            mv.k[k].ratio = -1;
        }
    }
    /* SEQUENCER */
    if (ctl_roll() && ui.layer == LY_PLAY)
        ui.step_page = ui.bank;                          /* (the STEP page shows the cursor's 16 steps) */
    mv.len = (uint8_t)trk_len(ts);
    mv.spage = ui.step_page;
    {
        static const char *const DIVN[NDIV_STEP] = {"1/4", "1/8", "1/16", "1/32", "1/8T", "1/16T", "1/2", "1 BAR", "2 BAR"};
        str_cpy(mv.div, DIVN[trk_div(ts) % NDIV_STEP], sizeof mv.div);
    }
    mv.lane = pen_lane;
    str_cpy(mv.lane_name, LANE_NAME[pen_lane & 15u], sizeof mv.lane_name);
    for (i = 0; i < MV_NSTEP; i++) {
        uint32_t idx = (uint32_t)ui.step_page * 16u + i;
        if (idx >= trk_len(ts))
            break;
        if (is_drum(ts)) {
            const dstep_t *d = &ts->dstep[idx];
            mv.lanes[i] = (uint16_t)dstep_mask(d);
            mv.st_lvl[i] = (uint8_t)(dstep_has(d, pen_lane) ? lvk(dstep_lvl(d, pen_lane)) : LVK_OFF);
        } else {
            const step_t *st = &ts->step[idx];
            if (step_on(st)) {
                mv.st_note[i] = st->note[0];
                mv.st_lvl[i] = (uint8_t)((st->flags & SF_ACCENT) ? LVK_HARD : lvk(st->lvl & 3u));
                mv.st_gate[i] = 1;
                for (k = idx + 1u; k < trk_len(ts) && ts->step[k].time == ST_TIE && mv.st_gate[i] < 16u; k++)
                    mv.st_gate[i]++;
            }
        }
        mv.st_lock[i] = (uint8_t)step_locked(ts, idx);
        mv.st_micro[i] = (uint8_t)(ts->micro[idx] ? (ts->micro[idx] > 0 ? 70 : 10) : 0);
        if ((ui.step_held >> i) & 1u && mv.held < 0)
            mv.held = (int8_t)i;
    }
    if (song.playing && ts->seq_idx / 16u == ui.step_page)
        mv.step16 = (uint8_t)(ts->seq_idx % 16u);
    else if (ctl_roll() && song.playing)
        mv.step16 = 0xFF;                                /* (the playhead is on another page) */
    if (ctl_roll()) {                                    /* the STEP page: notes, marks, the range of the pattern */
        uint32_t lo = 127, hi = 0, len = trk_len(ts);
        mv.cur = (uint8_t)(ui.cursor % 16u);
        mv.cur_abs = ui.cursor;
        for (i = 0; i < len; i++)
            for (k = 0; k < ts->step[i].n && k < 4u; k++) {
                lo = ts->step[i].note[k] < lo ? ts->step[i].note[k] : lo;
                hi = ts->step[i].note[k] > hi ? ts->step[i].note[k] : hi;
            }
        if (lo > hi)
            lo = hi = last_note;
        if (hi - lo < 12u) {                             /* at least an octave, the notes in the middle */
            uint32_t pad = (12u - (hi - lo)) / 2u;
            lo = lo > pad ? lo - pad : 0u;
            hi = lo + 12u;
        }
        mv.roll_lock = (uint8_t)scale_lock(ts);           /* (SCALE LOCK: the scale's rows, the root's) */
        mv.roll_mask = (uint16_t)(((scale_mask(ts) << ((uint32_t)ts->p[P_ROOT] % 12u)) |
                                   (scale_mask(ts) >> (12u - (uint32_t)ts->p[P_ROOT] % 12u))) & 0xFFFu);
        mv.roll_root = (uint8_t)((uint32_t)ts->p[P_ROOT] % 12u);
        if (mv.roll_lock) {
            char *e2 = ds_cat(mv.key, N_NOTE[mv.roll_root]);
            e2 = ds_cat(e2, " ");
            ds_cat(e2, N_SCALE[clamp(ts->p[P_SCALE], 0, NSCALES - 1)]);
        }
        mv.roll_lo = (uint8_t)lo;
        mv.roll_hi = (uint8_t)(hi > 127u ? 127u : hi);
        for (i = 0; i < MV_NSTEP; i++) {
            uint32_t idx = (uint32_t)ui.step_page * 16u + i;
            const step_t *st = &ts->step[idx % NSTEP];
            if (idx >= len)
                break;
            mv.roll_fl[i] = (uint8_t)((st->flags & SF_ACCENT ? RF_ACC : 0u) | (st->flags & SF_SLIDE ? RF_SLIDE : 0u) |
                                      (st->time == ST_TIE ? RF_TIE : 0u));
            if (st->time == ST_TIE)
                mv.roll_n[i] = 1;
            else if (step_on(st))
                for (k = 0; k < st->n && k < 4u; k++)
                    mv.roll_nt[i][mv.roll_n[i]++] = st->note[k];
        }
    }

    mv_layer();
    /* GROOVE */
    {
        const groove_t *g = ts->gv.own ? &ts->gv : &song_groove;
        uint32_t sw = (uint32_t)clamp(ts->p[P_SSWING] + song.g[G_SWING], 0, 100);
        str_cpy(mv.tmpl, ts->gv.own ? GROOVES[g->tmpl % GT_COUNT].name : "SONG", sizeof mv.tmpl);
        if (!ts->gv.own && song_groove.tmpl)
            str_cpy(mv.tmpl, GROOVES[song_groove.tmpl % GT_COUNT].name, sizeof mv.tmpl);
        mv.swing = (uint8_t)(50u + sw / 4u);
        mv.quant = (uint8_t)groove_strength(&ts->gv);
        mv.gamount = g->amt;
        mv.human = ts->gv.hum_t;
        for (i = 0; i < 8u; i++) {                       /* where eight sixteenths play: swing + template + humanize */
            int32_t off = groove_offset(&ts->gv, &song_groove, 0, 0, 0, i, song.sel, i, 0xFFu) / 32;
            if (i & 1u)
                off += (int32_t)(sw * 32u / 200u);
            mv.g_off[i] = (int8_t)clamp(off, -16, 16);
            mv.g_vel[i] = (uint8_t)groove_vel(&ts->gv, &song_groove, 100, i, song.sel, i, 0xFFu);
        }
    }
    /* SOUND */
    if (is_synth(ts)) {
        uint32_t total, pos = preset_pos(&total), kk;
        mv.preset_i = (uint8_t)pos;
        mv.preset_n = (uint8_t)total;
        str_cpy(mv.cat, pos < total ? preset_kind(pos) : "", sizeof mv.cat);
        if (total) {
            const char *nm;
            uint32_t j;
            for (j = 0; j < 2u; j++) {                   /* the list's previous and next entries (by list index) */
                uint32_t n = (pos + (j ? 1u : total - 1u)) % total;
                if (n < NBANK) {
                    nm = BANK[n].name;
                    str_cpy(mv.around[j], nm, sizeof mv.around[j]);
                } else if (preset_at(n, &kk) == NENGINES && kk < UP_SLOTS) {
                    char ub[13];
                    up_name(kk, ub);
                    str_cpy(mv.around[j], ub, sizeof mv.around[j]);
                } else {
                    str_cpy(mv.around[j], "USER", sizeof mv.around[j]);
                }
            }
        }
    } else {
        str_cpy(mv.cat, "KIT", sizeof mv.cat);
        mv.preset_i = (uint8_t)drum_kit_of(ts);
        mv.preset_n = DRUM_KITS;
    }
    mv.page = ctl.page[ctl.screen];
    mv.npages = ctl.screen == SC_SOUND ? (is_drum(ts) ? 3u : 7u) : ctl.screen == SC_SEQ && !is_drum(ts) ? 2u
                                                                  : ctl.screen == SC_PERFORM ? 4u : 1u;
    if (ctl.screen == SC_SOUND) {                        /* (a drum track after a synth: its pages) */
        static const char *const PGS[7] = {"PRESET", "ENVELOPE", "LFO", "SYNC", "SENDS", "INSERT", "INSERT 2"};
        static const char *const PGD[3] = {"KIT", "INSERT", "INSERT 2"};
        mv.page = (uint8_t)(mv.page % mv.npages);
        str_cpy(mv.pgname, is_drum(ts) ? PGD[mv.page % 3u] : PGS[mv.page % 7u], sizeof mv.pgname);
    }
    if (ctl.screen == SC_PERFORM && mv.page < 2u && punch.cur >= 0)
        mv.page = (uint8_t)(punch.cur >= 16 ? 1u : 0u); /* (an effect held: the keys it is on) */
    if (ctl.menu)
        mv.npages = 2, mv.page = ctl.screen == SC_SETTINGS;
    for (k = 0; k < 4u; k++) {
        static const uint8_t ID[4] = {P_ATK, P_DEC, P_SUS, P_REL};
        const param_desc_t *d = &TP[ID[k]];
        mv.env[k] = (uint8_t)((ts->p[ID[k]] - d->min) * 100 / (d->max - d->min ? d->max - d->min : 1));
    }
    /* PERFORM */
    for (i = 0; i < MV_NFX && i < PUNCH_NFX; i++) {     /* (the black keys' names short enough for a tile) */
        static const char *const SHORT[11] = {"KILL LO", "KILL MD", "KILL HI", "REWIND", "OCT UP", "FLANGE", "PANNER",
                                              "FREEZE", "LOOP 8T", "MONO", "FADE"};
        mv.fx[i] = i < 16u ? PUNCH_NAME[i] : SHORT[(i - 16u) % 11u];
    }
    mv.fx_on = punch.cur >= 0 ? 1u << punch.cur : 0u;
    /* CLIPS */
    mv.scene = 0xFF;
    for (k = 0; k < NCLIP; k++) {
        for (i = 0; i < NTRK && clip_playing(i) == k; i++)
            ;
        if (i == NTRK)
            mv.scene = (uint8_t)k;                       /* every track on clip k */
    }
    mv.blink = (uint8_t)((fm1_ms / 250u) & 1u);
    if (!ctl.menu && ctl.screen == SC_CLIPS) {
        mv.row_sel = ctl.row[SC_CLIPS];
        mv.k[1].label = "LAUNCH", str_cpy(mv.k[1].value, "CLIPS", 12);
        mv.k[2].label = "COPY", str_cpy(mv.k[2].value, "EDIT", 12);
        mv.k[3].label = "CLEAR", str_cpy(mv.k[3].value, "REC", 12);
    }
    /* PROJECT / SETTINGS */
    if (ctl.menu && ctl.screen == SC_PROJECT) {
        for (i = 0; i < FMP_NPROJ; i++) {
            char *e = mv.row_l[i];
            *e++ = (char)('1' + i);
            str_cpy(e, fmp_used(i) ? "  PROJECT" : "  EMPTY", 14);
            str_cpy(mv.row_r[i], fmp_used(i) ? "SAVED" : "", 16);
        }
        mv.nrows = FMP_NPROJ;
        mv.row_sel = ctl.row[SC_PROJECT];
        mv.row_cur = 0xFF;
        mv.k[0].label = "SAVE", str_cpy(mv.k[0].value, "SAVE", 12);
        mv.k[1].label = "EDIT", str_cpy(mv.k[1].value, "LOAD", 12);
        mv.k[2].label = 0;
        mv.k[3].label = 0;
    } else if (ctl.menu) {
        uint32_t sel = ctl.row[SC_SETTINGS], first = sel >= MV_ROWS ? sel - MV_ROWS + 1u : 0u;   /* (the list scrolls) */
        for (i = 0; i < MV_ROWS && first + i < ST_COUNT; i++) {
            str_cpy(mv.row_l[i], ST_LABEL[first + i], 16);
            st_value(first + i, mv.row_r[i]);
        }
        mv.nrows = (uint8_t)i;
        mv.row_sel = (uint8_t)(sel - first);
    }
    /* modal, toast */
    if (ctl.arm) {
        mv.modal = 1;
        str_cpy(mv.modal_t, ctl.arm_t, sizeof mv.modal_t);
        str_cpy(mv.modal_b, ctl.arm_b, sizeof mv.modal_b);
        str_cpy(mv.modal_ok, ctl.arm_ok, sizeof mv.modal_ok);
    }
    if (ctl.toast_ms) {
        mv.toast_t = 1;
        str_cpy(mv.toast, ctl.toast, sizeof mv.toast);
    }
}

/* the main loop's frame: the live state into the view model, the rows that changed onto the panel */
static void ui_draw(void)
{
    int force = ctl.force || ui.force;
    mv_fill();
    mv_render(force);
    ctl.force = 0;
    ui.force = 0;
}

/* ---------------------------------------------------- panel setup --- */
/* (SLOOP's hardware calibration, OCT- + OCT+ held at power-on, written with the design system's font) */
/* 30 s without input: give up and keep the old table (a stuck key cannot hang the boot) */
#define SETUP_IDLE_MS 30000u
static void panel_setup(void)
{
    uint32_t i, used = 0, t0 = fm1_ms;
    const panel_t old = panel;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 10, 240, &FONT_S, "HARDWARE CALIBRATION", DS_TEXT1, 1);
    draw_text_box(0, 30, 240, &FONT_S, "TEACH EACH BUTTON AND KNOB", DS_TEXT2, 1);
    while (fm1_in.buttons) {                             /* wait for OCT-/OCT+ release */
        fm1_wdt_feed();
        if (fm1_ms - t0 > SETUP_IDLE_MS)
            goto timeout;
    }
    fm1_input_edges(0);
    for (i = 0; i < NB; i++) {
        uint32_t p = 0, id;
        draw_text_box(0, 80, 240, &FONT_S, "PRESS", DS_TEXT2, 1);
        draw_text_box(0, 100, 240, &FONT_S, B_NAME[i], DS_TEXT1, 1);
        t0 = fm1_ms;
        while (!(p & ~used)) {
            fm1_wdt_feed();
            p |= fm1_input_edges(0);
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
        }
        for (id = 0; id < 14u; id++)
            if (((p & ~used) >> id) & 1u)
                break;
        panel.btn[i] = (uint8_t)id;
        used |= 1u << id;
    }
    used = 0;
    for (i = 0; i < NE; i++) {
        uint32_t e;
        int32_t st = 0;
        draw_text_box(0, 80, 240, &FONT_S, "TURN RIGHT", DS_TEXT2, 1);
        draw_text_box(0, 100, 240, &FONT_S, E_NAME[i], DS_TEXT1, 1);
        for (e = 0; e < 7u; e++)
            fm1_enc_take(e);
        t0 = fm1_ms;
        for (;;) {
            fm1_wdt_feed();
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
            for (e = 0; e < 7u; e++)
                if (!((used >> e) & 1u) && (st = fm1_enc_take(e)) != 0)
                    break;
            if (e < 7u)
                break;
        }
        panel.enc[i] = (uint8_t)e;
        panel.dir[i] = (int8_t)(st > 0 ? 1 : -1);
        used |= 1u << e;
        fm1_delay_ms(300);
        fm1_enc_take(e);
    }
    panel.magic = PANEL_MAGIC;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    layers_init();
    ui.force = 1;
    return;
timeout:
    panel = old;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.force = 1;
    ui_message("SETUP CANCELLED");
}

/* the boot screen: the name, what it is, the version (drawn with the design system, no bitmap) */
static void fm1move_splash_scene(void)
{
    uint32_t i;
    ds_text_in(&DS_F36, 0, 116, DS_W, DS_C, "FM1 MOVE", DS_TEXT1);
    for (i = 0; i < 8u; i++)
        ds_rrect(56 + (int32_t)i * 16, 134, 12, 4, 2, DS_TRK[i]);
    ds_track = 1;
    ds_text_in(&DS_F12, 0, 162, DS_W, DS_C, "8-TRACK GROOVEBOX", DS_TEXT2);
    ds_track = 0;
    ds_text_in(&DS_F12, 0, 226, DS_W, DS_C, "0.3  BASED ON SLOOP 2.6 + FELUCCA", DS_TEXT3);
}
static void fm1move_splash(void)
{
    ds_paint(fm1move_splash_scene, 0, 1);
    lcd_sync();
}

/* ------------------------------------------------- the new project (8 tracks) --- */
/* power-on and TOOLS > NEW: track 1 drums (808), then bass, chords, lead, pad, pluck, a second drum kit, keys:
 * light engines only (the three heavy-engine arenas stay free for the user's choices) */
static void project_default_tracks(void)
{
    static const uint8_t KIND[NTRK] = {TK_DRUM, TK_SYNTH, TK_SYNTH, TK_SYNTH, TK_SYNTH, TK_SYNTH, TK_DRUM, TK_SYNTH};
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        fm1_irq_off();
        trk_silence(t);
        t->kind = (uint8_t)(KIND[i] + 1u);
        track_defaults(t);
        memset(&t->gv, 0, sizeof t->gv);
        memset(t->pad_off, 0, sizeof t->pad_off);
        fm1_irq_on();
        if (KIND[i] == TK_SYNTH) {
            static const char *const SOUND[NTRK] = {0, "ACID 303", "RHODES", "SUPERSAW", "WARM PAD", "8BIT ARP", 0,
                                                    "M1 PIANO"};
            uint32_t n;
            bank_resolve();
            for (n = 0; n < NBANK && !(SOUND[i] && str_eq(BANK[n].name, SOUND[i])); n++)
                ;
            if (n < NBANK && bank_pi[n] != 0xFFu) {     /* the sound by name (the bank's order may change) */
                set_engine_of(t, BANK[n].e);
                apply_preset_to(t, bank_pi[n]);
            } else {
                set_engine_of(t, TRK_DEF[i][0]);
                apply_preset_to(t, TRK_DEF[i][1]);
            }
            t->engine = t->eng_req;
        } else {
            t->p[P_E0] = (int16_t)(i ? DRUM_SAMPLED + 2u : DRUM_DEFAULT_KIT);
            t->p[P_REV] = 16;
        }
        steps_clear(t);
    }
    memset(&song_groove, 0, sizeof song_groove);
    clip_init();                                         /* (every clip empty, clip 1 playing) */
}
