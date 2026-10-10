/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Engine table: order = PRESETS browsing order (and the engine numbers of the editor protocol). */
#include "dsp.c"
/* SLOOP 2.5: the big per-part state of GRAIN, FM6 and PHYS shares one arena per synth part (a part plays one
 * engine at a time: on a switch the old engine fades out, its voices end, then the new one starts, voice.c
 * engine_block). The first engine to ask for it after another one gets it cleared, as at power-on. */
static void *eng_arena_of(const track_t *t, uint32_t eng);
static inline int32_t voice_amp(int32_t s, const vmod_t *m, uint32_t i) { return mulq15(mulq15(s, amp_at(m, i)), VOICE_FS); }
static inline int32_t soft_knee(int32_t y, int32_t k)   /* linear up to k, then only the peaks saturate */
{
    int32_t a = y < 0 ? -y : y;
    if (a <= k)
        return y;
    a = k + (softclip((a - k) * 2) >> 1);
    return y < 0 ? -a : a;
}
/* (pitch_inc: felucca_tables.h, SLOOP 2.5: from the top octave) */
#include "eng_analog.c"
#include "eng_digital.c"
#include "eng_phase.c"
#include "eng_lofi.c"
#include "eng_sample.c"
#include "eng_formant.c"
#include "eng_trio.c"
#include "eng_drawbar.c"
#include "eng_grain.c"
#include "eng_fm6.c"            /* FM6: 6-operator FM, msfa ported (fm6_core.c, Apache-2.0); SLOOP 2.4 */
#include "eng_phys.c"           /* PHYS: physical models, from Felucca 1.0 (DaisySP, Rings: MIT); SLOOP 2.5 */
#include "eng_noise.c"          /* NOISE: from Felucca 1.0; SLOOP 2.5 */

typedef union {
    gr_part_t grain;
    fm6_note_t fm6[FM6_POLY];
    uint32_t phys[PHYS_ARENA / 4u];      /* eng_phys.c: two SYMP slots or three small ones */
} eng_arena_t;
/* FM1 Move: N_ARENA arenas shared by every track (any of the eight may be a synth). A track claims one the first
 * time its heavy engine asks (eng_arena_of) and gives it back when it plays a light engine again or stops being a
 * synth (eng_arena_release, voice.c engine_block); when all are taken a ninth heavy engine is refused before it
 * starts (eng_heavy_ok: set_engine_of keeps the old engine), so a render never finds no arena. The limit is on
 * heavy engines playing at once, never on tracks (docs/MEMORY_BUDGET.md section 3). */
#ifndef N_ARENA
#define N_ARENA 3u
#endif
static eng_arena_t eng_arena[N_ARENA] __attribute__((section(".pool")));
static uint8_t eng_arena_own[N_ARENA];          /* the engine + 1 whose state is in it, 0 = none */
static uint8_t eng_arena_trk[N_ARENA];          /* the track + 1 that holds it, 0 = free */
#ifdef ARENA_STATS
static uint32_t eng_arena_claims, eng_arena_bad;   /* host tests: claims, accesses by an engine the part does not play */
#endif
static int eng_heavy(uint32_t e) { return e == ENGI_GRAIN || e == ENGI_FM6 || e == ENGI_PHYS; }
static int eng_arena_find(const track_t *t)
{
    uint32_t a, id = (uint32_t)(t - trk) + 1u;
    for (a = 0; a < N_ARENA; a++)
        if (eng_arena_trk[a] == id)
            return (int)a;
    return -1;
}
/* may track t play engine e? (a light engine always; a heavy one: it holds an arena or one is free) */
static int eng_heavy_ok(const track_t *t, uint32_t e)
{
    uint32_t a;
    if (!eng_heavy(e) || eng_arena_find(t) >= 0)
        return 1;
    for (a = 0; a < N_ARENA; a++)
        if (!eng_arena_trk[a])
            return 1;
    return 0;
}
static void eng_arena_release(const track_t *t)
{
    int a = eng_arena_find(t);
    if (a >= 0)
        eng_arena_trk[a] = 0;                    /* (its contents stay: cleared by the next claim) */
}
/* claim an arena for track t now (the main loop, IRQ off, before it asks for a heavy engine): 0 = none free */
static int eng_arena_claim(const track_t *t)
{
    uint32_t k;
    if (eng_arena_find(t) >= 0)
        return 1;
    for (k = 0; k < N_ARENA; k++)
        if (!eng_arena_trk[k]) {
            eng_arena_trk[k] = (uint8_t)(t - trk + 1);
            eng_arena_own[k] = 0;
            return 1;
        }
    return 0;
}
static void *eng_arena_of(const track_t *t, uint32_t eng)
{
    int a = eng_arena_find(t);
    uint32_t k;
    if ((uint32_t)(t - trk) >= NTRK)
        return 0;
#ifdef ARENA_STATS
    if (t->engine != eng)
        eng_arena_bad++;
#endif
    if (a < 0) {
        for (k = 0; k < N_ARENA && eng_arena_trk[k]; k++)
            ;
        if (k == N_ARENA)
            return 0;                            /* (eng_heavy_ok refuses this first: callers still check) */
        a = (int)k;
        eng_arena_trk[a] = (uint8_t)(t - trk + 1);
        eng_arena_own[a] = 0;
    }
    if (eng_arena_own[a] != eng + 1u) {
        memset(&eng_arena[a], 0, sizeof eng_arena[a]);
        eng_arena_own[a] = (uint8_t)(eng + 1u);
#ifdef ARENA_STATS
        eng_arena_claims++;
#endif
    }
    return &eng_arena[a];
}
#if FELUCCA_SLICE
#include "eng_slice.c"
#endif

static const engine_t *const ENGINES[NENGINES] = {&ENG_ANALOG, &ENG_DIGITAL, &ENG_PHASE, &ENG_LOFI, &ENG_SAMPLE,
                                                    &ENG_FORMANT, &ENG_TRIO, &ENG_DRAWBAR, &ENG_GRAIN,
                                                    &ENG_FM6,    /* 9 (ENGI_FM6): always; SLICE after it (core.h) */
                                                    &ENG_PHYS, &ENG_NOISE,
#if FELUCCA_SLICE
                                                    &ENG_SLICE,
#endif
};
_Static_assert(ENGI_FM6 == 9u, "ENGINES[ENGI_FM6] is FM6");
_Static_assert(ENGI_GRAIN == 8u && ENGI_PHYS == 10u && ENGI_NOISE == 11u && ENGI_SLICE == 12u, "ENGINES[] order");

/* every factory sound as loud as the others: a level trim per preset, 1/2 dB, measured on a phrase
 * that fits the sound (tools/level_presets.py writes preset_trim.h); a track keeps it in P_ED_FX */
#include "preset_trim.h"
static int16_t preset_trim(uint32_t e, uint32_t pi)
{
    return e < PT_ENGINES && pi < PT_MAX ? PRESET_TRIM[e][pi] : 0;
}
