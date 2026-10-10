/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 Move controls: the panel as docs/UI_NAVIGATION.md and docs/UI_MAPPING.md describe it, and the live view model
 * the screens (ui_move.c) draw. Replaces SLOOP's dispatcher (ui_input.c) and drawing (ui_draw.c, ui_studio.c ...)
 * in the FM1MOVE build; keeps SLOOP's engine-side behaviour: the layers' keys (seq.c, ui_layers.c), the record
 * arm, the transport, undo / redo, the octave, the drum dynamics.
 *
 *   top row    FX PERFORM   SEL TRACKS   ENV SOUND.ENV   LFO SOUND.LFO   EDIT SOUND   GLO MIXER
 *   bottom row HOME PLAY    SAVE CLIPS   ARP GROOVE      SEQ SEQUENCER   PLAY transport   REC record
 *   held       FX punch, EDIT erase, ARP roll, SEQ steps, SEL track / key, GLO mute / solo, SAVE (scenes, M3)
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
} ctl = {.hot = -1, .kind_req = TK_COUNT, .force = 1};

enum { ACT_NONE, ACT_KIND, ACT_SAVE, ACT_LOAD, ACT_DELETE, ACT_COMMIT };

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
enum { KB_NONE, KB_TP, KB_GP, KB_MACRO, KB_GRV_Q, KB_GRV_AMT, KB_GRV_HUM, KB_SHIFT, KB_KIND, KB_KIT, KB_SET };
typedef struct {
    uint8_t kind, id;
    const char *label;
} kbind_t;

static uint32_t sel_kind(void) { return trk_kind(TSEL); }

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
    static const kbind_t PRF[4] = {{KB_GP, G_FILT, "FILTER"}, {KB_GP, G_DUST, "DUST"}, {KB_GP, G_DUCK, "DUCK"},
                                   {KB_TP, P_TFLT, "TRK FLT"}};
    static const kbind_t DRM[4] = {{KB_KIT, 0, "KIT"}, {KB_TP, P_LEVEL, "LEVEL"}, {KB_TP, P_PAN, "PAN"},
                                   {KB_TP, P_TFLT, "FILTER"}};
    static const kbind_t NONE = {KB_NONE, 0, 0};
    k &= 3u;
    if (ctl.menu)
        return ctl.screen == SC_SETTINGS && !k ? (kbind_t){KB_SET, 0, "VALUE"} : NONE;
    switch (ctl.screen) {
    case SC_PLAY:
        if (sel_kind() == TK_DRUM)
            return DRM[k];
        return (kbind_t){KB_MACRO, (uint8_t)k, 0};
    case SC_TRACKS: return TRK[k];
    case SC_SEQ: return SEQ[k];
    case SC_GROOVE: return GRV[k];
    case SC_MIXER: return MIX[k];
    case SC_SOUND:
        if (sel_kind() == TK_DRUM)
            return DRM[k];
        switch (ctl.page[SC_SOUND] & 3u) {
        case 1: return ENV[k];
        case 2: return LFO[k];
        case 3: return SND[k];
        default: return (kbind_t){KB_MACRO, (uint8_t)k, 0};
        }
    case SC_PERFORM: return PRF[k];
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
    if (id >= P_COUNT)
        return 0;
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
enum { ST_SYNC, ST_MIDIOUT, ST_MIDIIN, ST_LIGHTS, ST_KEYS, ST_USBAUDIO, ST_ABOUT, ST_COUNT };
static const char *const ST_LABEL[ST_COUNT] = {"CLOCK IN", "MIDI OUT", "MIDI IN", "LIGHTS", "KEY LIGHTS", "USB AUDIO",
                                               "VERSION"};
static void st_value(uint32_t r, char *b)
{
    static const char *const SYNC[3] = {"INT", "USB", "TRS"};
    static const char *const LV[4] = {"OFF", "LOW", "MID", "HIGH"};
    static const char *const KY[4] = {"OFF", "C KEYS", "WHITE", "ALL"};
    switch (r) {
    case ST_SYNC: str_cpy(b, SYNC[lights_sync % 3u], 16); break;
    case ST_MIDIOUT: str_cpy(b, lights_mout ? "SEQ" : "KEYS", 16); break;
    case ST_MIDIIN: str_cpy(b, lights_min ? "CLOCK" : "NOTES", 16); break;
    case ST_LIGHTS: str_cpy(b, LV[lights_lvl % 4u], 16); break;
    case ST_KEYS: str_cpy(b, KY[lights_keys % 4u], 16); break;
    case ST_USBAUDIO: str_cpy(b, usb_full ? "FULL" : "MAIN", 16); break;
    default: str_cpy(b, FELUCCA_VERSION, 16); break;
    }
}
static void st_turn(uint32_t r, int32_t s)
{
    int32_t d = s > 0 ? 1 : -1;
    switch (r) {
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
/* clip.c (included by projfmt.c, after this file) */
static void clip_launch(uint32_t ti, uint32_t k);
static void clip_scene(uint32_t k);
static int clip_copy(uint32_t ti, uint32_t from, uint32_t to);
static void clip_erase(uint32_t ti, uint32_t k);
static void clip_reset_track(uint32_t ti);
static void clip_init(void);
static void clip_tick(void);
static uint32_t clip_state(uint32_t ti, uint32_t k);
static uint8_t clip_full;
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
        static const uint8_t NP[SC_COUNT] = {1, 1, 1, 2, 1, 1, 4, 1, 1, 1};
        ctl.page[sc] = (uint8_t)((ctl.page[sc] + 1u) % NP[sc]);
    }
    ctl.menu = 0;
    ctl.screen = (uint8_t)sc;
    ctl.hot = -1;
    song.seq_mode = sc == SC_SEQ;
}

/* a button tapped (released quickly, nothing touched while held) */
static void ctl_tap(uint32_t b)
{
    switch (b) {
    case B_HOME: screen_go(SC_PLAY); break;
    case B_FX: screen_go(SC_PERFORM); break;
    case B_SCL: screen_go(SC_TRACKS); break;
    case B_ENV: screen_go(SC_SOUND); ctl.page[SC_SOUND] = 1; break;
    case B_LFO: screen_go(SC_SOUND); ctl.page[SC_SOUND] = 2; break;
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
        screen_go(SC_CLIPS);
        break;
    case B_ARP: screen_go(SC_GROOVE); break;
    case B_SEQ: screen_go(SC_SEQ); break;
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
    if (pressed & ctl_btn_bit(B_REC)) {
        if (!ft_owns_press()) {
            if (song.rec || rec_wait) {
                song.rec = 0;
                rec_wait = 0;
                ctl_toast("REC OFF", "");
            } else if (song.playing) {
                rec_begin();
            } else {
                rec_wait = 1;                            /* stopped: the first note (or PLAY) starts it */
            }
        }
    }
    if (held == LY_ERASE && (pressed & (ctl_btn_bit(B_OCTDN) | ctl_btn_bit(B_OCTUP)))) {   /* EDIT + OCT: undo / redo */
        ctl.used[B_EDIT] = 1;
        if (pressed & ctl_btn_bit(B_OCTDN))
            ctl_toast(undo_swap(0) ? "UNDO" : "NOTHING TO UNDO", "");
        else
            ctl_toast(undo_swap(1) ? "REDO" : "NOTHING TO REDO", "");
    } else if (held == LY_STEP && (pressed & (ctl_btn_bit(B_OCTDN) | ctl_btn_bit(B_OCTUP)))) {   /* SEQ + OCT: page */
        uint32_t pages = (trk_len(TSEL) + 15u) / 16u;
        ctl.used[B_SEQ] = 1;
        if (ui.step_held)
            (pressed & ctl_btn_bit(B_OCTDN)) ? steps_held_clear() : steps_held_fill();
        else
            ui.step_page = (uint8_t)((ui.step_page + ((pressed & ctl_btn_bit(B_OCTUP)) ? 1u : pages - 1u)) % pages);
    } else if (!is_drum(TSEL) && (pressed & (ctl_btn_bit(B_OCTDN) | ctl_btn_bit(B_OCTUP)))) {
        uint32_t both = ctl_btn_bit(B_OCTDN) | ctl_btn_bit(B_OCTUP);
        if ((fm1_in.buttons & both) == both)
            song.octave = 0;
        else if (pressed & ctl_btn_bit(B_OCTDN))
            song.octave = (int8_t)(song.octave > -3 ? song.octave - 1 : song.octave);
        else
            song.octave = (int8_t)(song.octave < 3 ? song.octave + 1 : song.octave);
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
        if (ctl.menu) {
            uint32_t n = sc == SC_PROJECT ? FMP_NPROJ : ST_COUNT;
            ctl.row[sc] = (uint8_t)clamp((int32_t)ctl.row[sc] + s, 0, (int32_t)n - 1);
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
        } else if (sc == SC_CLIPS) {
            ctl.row[sc] = 0;                             /* (one clip per track until M3) */
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
    ctl_led(nl, panel.btn[B_REC], song.rec != 0u || ft_on || (rec_wait && ((fm1_ms / 125u) & 1u)));
    if (is_drum(TSEL)) {
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
    } else if (ui.layer == LY_MIX) {                     /* GLO: 1-8 heard, 9-16 soloed */
        for (k = 0; k < NTRK; k++) {
            if (!trk[k].p[P_MUTE])
                keys |= 1u << key_of_white(k);
            if ((song.solo >> k) & 1u)
                keys |= 1u << key_of_white(8u + k);
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
static void mv_knob(uint32_t k)
{
    kbind_t kb = ctl_bind(k);
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
        if (kb.kind == KB_MACRO)
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
        uint32_t total, sel = song.sel, pos;
        song.sel = (uint8_t)i;                           /* (preset_pos reads the selected track) */
        pos = preset_pos(&total);
        song.sel = (uint8_t)sel;
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
        o->clips = (uint8_t)(track_empty(t) ? 0u : 1u);
        o->clip = (uint8_t)(song.playing && !track_empty(t) ? 0u : 0xFFu);
        o->queued = 0xFF;
    }
    mv.playing = song.playing;
    mv.rec = (uint8_t)(song.rec != 0u && song.playing);
    mv.count_in = (uint8_t)(ci_on ? 4u - ci_beat : 0u);
    mv.bpm10 = (uint16_t)(song.g[G_BPM] * 10);
    mv.quant_on = (uint8_t)(groove_strength(&ts->gv) > 0u);
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
            nm = preset_at((pos + total - 1u) % total, &kk) < NBANK ? BANK[preset_at((pos + total - 1u) % total, &kk)].name : "USER";
            str_cpy(mv.around[0], nm, sizeof mv.around[0]);
            nm = preset_at((pos + 1u) % total, &kk) < NBANK ? BANK[preset_at((pos + 1u) % total, &kk)].name : "USER";
            str_cpy(mv.around[1], nm, sizeof mv.around[1]);
        }
    } else {
        str_cpy(mv.cat, "KIT", sizeof mv.cat);
        mv.preset_i = (uint8_t)drum_kit_of(ts);
        mv.preset_n = DRUM_KITS;
    }
    mv.page = ctl.page[ctl.screen];
    mv.npages = ctl.screen == SC_SOUND ? 4u : ctl.screen == SC_GROOVE ? 1u : 1u;
    if (ctl.menu)
        mv.npages = 2, mv.page = ctl.screen == SC_SETTINGS;
    for (k = 0; k < 4u; k++) {
        static const uint8_t ID[4] = {P_ATK, P_DEC, P_SUS, P_REL};
        const param_desc_t *d = &TP[ID[k]];
        mv.env[k] = (uint8_t)((ts->p[ID[k]] - d->min) * 100 / (d->max - d->min ? d->max - d->min : 1));
    }
    /* PERFORM */
    for (i = 0; i < 16u && i < PUNCH_NFX; i++)
        mv.fx[i] = PUNCH_NAME[i];
    mv.fx_on = (uint16_t)(punch.cur >= 0 ? 1u << punch.cur : 0u);
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
        for (i = 0; i < ST_COUNT && i < MV_ROWS; i++) {
            str_cpy(mv.row_l[i], ST_LABEL[i], 16);
            st_value(i, mv.row_r[i]);
        }
        mv.nrows = ST_COUNT < MV_ROWS ? ST_COUNT : MV_ROWS;
        mv.row_sel = ctl.row[SC_SETTINGS] < MV_ROWS ? ctl.row[SC_SETTINGS] : MV_ROWS - 1u;
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
    ds_text_in(&DS_F12, 0, 226, DS_W, DS_C, "0.1  BASED ON SLOOP + FELUCCA", DS_TEXT3);
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
