/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 Move Groove Engine: where an event of a pattern plays and how hard. Pure functions of their arguments (no
 * globals, no clock), so the host tests check them alone; seq.c asks them for every step it fires.
 * Specification: docs/GROOVE_ENGINE.md.
 *
 * The pipeline, for an event of step `step` (its grid position comes from the transport and the track's DIV and
 * SWING, seq.c trk_grid: the swing is applied there, once):
 *   1. raw       the deviation recorded with the event (rec_note / rec_hit), never changed but by a commit
 *   2. quantize  q = raw + s (grid - raw): the step's grid is 0, so q = raw (1 - s), s = strength 0..100 %
 *   3. template  + amount x the template's residual offset for the event's sixteenth in the bar. A template holds
 *                no swing of its own: its swing is a suggested SWING value (groove_tmpl_t.swing) the UI sets when it
 *                is chosen, so groove and swing never move an event twice
 *   4. micro     + the step's manual nudge (SLOOP's micro[]) and, on a drum track, the lane's pad offset
 *   5. humanize  + a deterministic random offset: hash(seed, track, step, lane), the same every pass and after a
 *                save / load; a new seed (REGEN) draws new values
 *   6. schedule  the sum, clamped to just under half a step either way, in 1/1024 of the step: seq.c turns it into
 *                clock units and fires the step in the right control block (early: in the previous grid step,
 *                across the pattern's wrap too)
 * Velocity: the template's velocity map x amount, then humanize, bounded 1..127.
 * With the defaults (strength 100 %, STRAIGHT, amount 0, no humanize) the result is exactly SLOOP's nudge. */

#define GRV_ONE 1024                           /* offsets in 1/1024 of a step */
#define GRV_MIN (-512)                         /* the window seq_tick fires in: half a step early (SLOOP's -32/64) .. */
#define GRV_MAX 496                            /* .. just under half a step late (31/64) */
#define GRV_HUM_MAX 128                        /* humanize at 100 %: up to 1/8 of a step either way */

typedef struct {
    const char *name;
    uint8_t swing;                             /* the SWING it suggests (0..100, SLOOP's scale: 0 = 50 %) */
    int8_t off[16];                            /* residual timing per sixteenth of the bar, 1/64 of a step */
    uint8_t vel[16];                           /* velocity per sixteenth, % (100 = as written) */
} groove_tmpl_t;

enum { GT_STRAIGHT, GT_MPC, GT_HOUSE, GT_GARAGE, GT_BROKEN, GT_ROLLING, GT_LATIN, GT_USER, GT_COUNT };
/* The factory feels (USER: the project's own, groove_t.user_off / user_vel). Offsets are what is left after the
 * suggested swing: a positive value plays later. Velocity maps carry the accents that make the feel. */
static const groove_tmpl_t GROOVES[GT_COUNT] = {
    {"STRAIGHT", 0, {0}, {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100}},
    /* MPC 16: the classic 16th swing; downbeats strong, the swung 16ths soft */
    {"MPC 16", 32, {0}, {100, 70, 86, 72, 96, 70, 86, 72, 100, 70, 86, 72, 96, 70, 88, 76}},
    /* HOUSE: a light shuffle, off-beat eighths pushed a hair early and loud (the open hat), 16ths ghosted */
    {"HOUSE", 24, {0, 0, -2, 0, 0, 0, -2, 0, 0, 0, -2, 0, 0, 0, -2, 0},
     {100, 62, 112, 64, 96, 62, 112, 64, 100, 62, 112, 64, 96, 62, 112, 66}},
    /* UK GARAGE: a heavy shuffle, the snare (beats 2 and 4) laid back, the 16th before the one rushed */
    {"UK GARAGE", 64, {0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 3, 0, -3, 0},
     {104, 60, 84, 92, 100, 58, 80, 94, 104, 60, 84, 92, 100, 58, 96, 88}},
    /* BROKEN BEAT: light swing, irregular pushes and drags inside the bar */
    {"BROKEN", 12, {0, 0, 0, 4, 0, 0, -3, 0, 0, 0, 0, 5, 0, -2, 0, 0},
     {104, 66, 78, 96, 70, 84, 100, 64, 98, 66, 74, 100, 72, 92, 80, 70}},
    /* ROLLING TECH: straight, rolling 16th dynamics, off-beats slightly late (laid back) */
    {"ROLLING", 0, {0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 2, 0, 0, 0, 2, 0},
     {100, 72, 88, 78, 98, 72, 88, 78, 100, 72, 88, 78, 98, 72, 88, 80}},
    /* LATIN: the 3-2 son clave accented (1, 4, 7, 11, 13), the tresillo's pickups pulled early */
    {"LATIN", 8, {0, 0, 0, -2, 0, 0, -2, 0, 0, 0, 0, 0, 0, 0, 0, 0},
     {116, 66, 72, 112, 70, 66, 114, 70, 72, 66, 112, 66, 114, 70, 72, 66}},
    {"USER", 0, {0}, {100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100}},
};

/* A track's groove (and the song's, song_groove). All zero = the defaults (SLOOP's behaviour: on the grid, no
 * template, no humanize, following the song), so zeroed memory and old projects need no setting up. */
typedef struct {
    uint8_t qloose;                            /* 100 - quantize strength (0: strength 100 %, on the grid) */
    uint8_t tmpl;                              /* GT_* */
    uint8_t amt;                               /* template amount on timing 0..100 % */
    uint8_t vamt;                              /* template amount on velocity 0..100 % (a commit keeps it) */
    uint8_t hum_t, hum_v;                      /* humanize timing / velocity 0..100 % */
    uint8_t own;                               /* 1: its own template and amount; 0: the song's (the global groove) */
    uint16_t seed;                             /* humanize seed: REGEN draws a new one */
    int8_t user_off[16];                       /* GT_USER: offsets (1/64 step) and velocity changes (%, 0 = as is) */
    int8_t user_vel[16];
} groove_t;
static uint32_t groove_strength(const groove_t *g) { return g->qloose <= 100u ? 100u - g->qloose : 0u; }
static void groove_set_strength(groove_t *g, uint32_t s) { g->qloose = (uint8_t)(100u - (s > 100u ? 100u : s)); }

/* every value back inside its range (a project or the editor) */
static void groove_sane(groove_t *g)
{
    uint32_t i;
    if (g->qloose > 100u) g->qloose = 100;
    if (g->tmpl >= GT_COUNT) g->tmpl = GT_STRAIGHT;
    if (g->amt > 100u) g->amt = 100;
    if (g->vamt > 100u) g->vamt = 100;
    if (g->hum_t > 100u) g->hum_t = 100;
    if (g->hum_v > 100u) g->hum_v = 100;
    g->own = g->own != 0;
    for (i = 0; i < 16u; i++) {
        if (g->user_off[i] > 31) g->user_off[i] = 31;
        if (g->user_off[i] < -31) g->user_off[i] = -31;
        if (g->user_vel[i] < -99) g->user_vel[i] = -99;
    }
}

/* the template and amount in force: the track's, or the song's when it follows */
static void groove_tmpl_of(const groove_t *t, const groove_t *song, uint32_t *tmpl, uint32_t *amt,
                           const groove_t **user)
{
    const groove_t *g = t->own ? t : song;
    *tmpl = g->tmpl < GT_COUNT ? g->tmpl : GT_STRAIGHT;
    *amt = g->amt <= 100u ? g->amt : 100u;
    *user = g;
}
static uint32_t groove_vamt(const groove_t *t, const groove_t *song)
{
    const groove_t *g = t->own ? t : song;
    return g->vamt <= 100u ? g->vamt : 100u;
}

/* does the groove change anything beyond SLOOP's nudge? (the sequencer's fast path) */
static int groove_active(const groove_t *t, const groove_t *song)
{
    uint32_t tm, am;
    const groove_t *u;
    groove_tmpl_of(t, song, &tm, &am, &u);
    return t->qloose || ((am || groove_vamt(t, song)) && tm != GT_STRAIGHT) || t->hum_t || t->hum_v;
}

/* a deterministic hash of the event: -1024 .. 1023 (salt: timing, velocity) */
static int32_t groove_rnd(uint32_t seed, uint32_t trk_i, uint32_t step, uint32_t lane, uint32_t salt)
{
    uint32_t h = 2166136261u;
    h = (h ^ seed) * 16777619u;
    h = (h ^ (trk_i | lane << 8 | salt << 16)) * 16777619u;
    h = (h ^ step) * 16777619u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return (int32_t)(h & 2047u) - 1024;
}

static int32_t grv_clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* the offset of an event in 1/1024 of its step (see the top). raw64, nudge64, pad64: 1/64 of a step;
 * slot: its sixteenth in the bar (0..15); trk_i, step, lane: who it is (humanize) */
static int32_t groove_offset(const groove_t *t, const groove_t *song, int32_t raw64, int32_t nudge64, int32_t pad64,
                             uint32_t slot, uint32_t trk_i, uint32_t step, uint32_t lane)
{
    uint32_t tm, am;
    const groove_t *u;
    int32_t off, q = (int32_t)groove_strength(t);
    groove_tmpl_of(t, song, &tm, &am, &u);
    off = raw64 * 16 * (100 - q) / 100;                                  /* 2. quantize (raw x (1 - s)) */
    if (am && tm != GT_STRAIGHT)                                         /* 3. template residual */
        off += (tm == GT_USER ? u->user_off[slot & 15u] : GROOVES[tm].off[slot & 15u]) * 16 * (int32_t)am / 100;
    off += (nudge64 + pad64) * 16;                                       /* 4. micro: step nudge, pad offset */
    if (t->hum_t)                                                        /* 5. humanize */
        off += groove_rnd(t->seed, trk_i, step, lane, 1u) * (GRV_HUM_MAX * (int32_t)t->hum_t / 100) / 1024;
    return grv_clamp(off, GRV_MIN, GRV_MAX);                             /* 6. inside the scheduling window */
}

/* the velocity of an event written at vel (1..127) */
static uint32_t groove_vel(const groove_t *t, const groove_t *song, uint32_t vel, uint32_t slot, uint32_t trk_i,
                           uint32_t step, uint32_t lane)
{
    uint32_t tm, am;
    const groove_t *u;
    int32_t v = (int32_t)vel;
    groove_tmpl_of(t, song, &tm, &am, &u);
    am = groove_vamt(t, song);
    if (am && tm != GT_STRAIGHT) {
        int32_t pc = tm == GT_USER ? 100 + u->user_vel[slot & 15u] : GROOVES[tm].vel[slot & 15u];
        v = v * (100 + (pc - 100) * (int32_t)am / 100) / 100;
    }
    if (t->hum_v)
        v += groove_rnd(t->seed, trk_i, step, lane, 2u) * (24 * (int32_t)t->hum_v / 100) / 1024;
    return (uint32_t)grv_clamp(v, 1, 127);
}

/* clock units of an offset in a step of slen units. A pure nudge (a multiple of 16) gives exactly SLOOP's
 * (slen / 64) x m; finer parts are added below it (no 64-bit math, no overflow up to a 2-bar step) */
static int32_t groove_units(int32_t off1024, uint32_t slen)
{
    int32_t q = off1024 / 16, r = off1024 - q * 16, s64 = (int32_t)(slen / 64u);
    return s64 * q + s64 * r / 16;
}

/* the sixteenth of the bar a grid step falls on: step abs of a grid of den steps a beat (den 4: sixteenths) */
static uint32_t groove_slot(uint32_t abs, uint32_t den)
{
    uint32_t bar = den * 4u;
    return den ? (abs % bar) * 16u / bar : 0u;
}
