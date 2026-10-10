/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 Move Groove Engine (firmware/src/groove.c and its use in seq.c), on the host.
 *   pure      the pipeline's arithmetic: defaults = SLOOP's nudge exactly, q = raw + s (grid - raw), template
 *             scaling, humanize deterministic and bounded, the window clamp, units, velocities, slots
 *   timing    where steps really fire (control blocks of 32 samples): quantize strength on recorded deviations,
 *             a live recording's raw deviation (with the latency compensation), swing applied once with a template,
 *             early events across the pattern's wrap, no drift over 64 bars with humanize, pad offsets late / early
 *   commit    GROOVE COMMIT plays as before (within the 1/64 rounding), undo puts everything back
 *   velocity  template accents reach the hits
 * Exit status: the number of failed checks. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("groove: %-74s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

/* ------------------------------------------------------------------ pure --- */
static void t_pure(void)
{
    groove_t g, z;
    int32_t r, s, ok = 1, m;
    uint32_t slot, step, bad = 0, diff = 0, i;
    memset(&z, 0, sizeof z);
    /* defaults: the nudge alone, as SLOOP */
    for (m = MICRO_MIN; m <= MICRO_MAX; m++)
        for (slot = 0; slot < 16u; slot++)
            ok &= groove_offset(&z, &z, 17, m, 0, slot, 1, slot, 0xFF) == m * 16;
    check(ok, "zeroed settings: offset = the nudge (raw ignored, strength 100 %)");
    ok = 1;
    for (m = MICRO_MIN; m <= MICRO_MAX; m++) {
        uint32_t sl;
        for (sl = 330750u; sl <= 21168000u; sl += 1234567u)
            ok &= groove_units(m * 16, sl) == (int32_t)(sl / 64u) * m;
    }
    check(ok, "a pure nudge converts to exactly SLOOP's units (slen / 64 x m)");
    /* quantize: q = raw + s (0 - raw) */
    memset(&g, 0, sizeof g);
    ok = 1;
    for (s = 0; s <= 100; s += 10) {
        groove_set_strength(&g, (uint32_t)s);
        for (r = -32; r <= 31; r++)
            ok &= groove_offset(&g, &z, r, 0, 0, 0, 0, 0, 0xFF) == r * 16 * (100 - s) / 100;
    }
    check(ok, "quantize: offset = raw x (1 - strength) for every raw and strength");
    /* template residual x amount, owned or followed */
    memset(&g, 0, sizeof g);
    g.own = 1;
    g.tmpl = GT_GARAGE;
    g.amt = 50;
    check(groove_offset(&g, &z, 0, 0, 0, 4, 0, 4, 0xFF) == 3 * 16 * 50 / 100 &&
              groove_offset(&g, &z, 0, 0, 0, 14, 0, 14, 0xFF) == -3 * 16 * 50 / 100,
          "template: the residual offset x amount (UK GARAGE at 50 %)");
    {
        groove_t sg;
        memset(&sg, 0, sizeof sg);
        sg.tmpl = GT_GARAGE;
        sg.amt = 100;
        memset(&g, 0, sizeof g);                      /* follows the song */
        check(groove_offset(&g, &sg, 0, 0, 0, 4, 0, 4, 0xFF) == 48, "a track follows the song's groove unless it owns one");
    }
    /* humanize */
    memset(&g, 0, sizeof g);
    g.hum_t = 100;
    g.seed = 77;
    for (step = 0; step < 64u; step++) {
        int32_t a = groove_offset(&g, &z, 0, 0, 0, step % 16u, 3, step, 0xFF);
        int32_t b = groove_offset(&g, &z, 0, 0, 0, step % 16u, 3, step, 0xFF);
        bad += a != b || a > GRV_HUM_MAX || a < -GRV_HUM_MAX;
        g.seed = 78;
        diff += groove_offset(&g, &z, 0, 0, 0, step % 16u, 3, step, 0xFF) != a;
        g.seed = 77;
    }
    check(!bad, "humanize: the same event always moves the same way, within 1/8 step");
    check(diff > 48u, "humanize: a new seed (REGEN) moves the events differently");
    g.hum_t = 25;
    for (step = 0, bad = 0; step < 64u; step++) {
        int32_t a = groove_offset(&g, &z, 0, 0, 0, 0, 3, step, 0xFF);
        bad += a > GRV_HUM_MAX / 4 || a < -GRV_HUM_MAX / 4;
    }
    check(!bad, "humanize at 25 %: within 1/32 step");
    /* the window */
    memset(&g, 0, sizeof g);
    g.qloose = 100;
    g.hum_t = 100;
    for (step = 0, bad = 0; step < 64u; step++) {
        int32_t a = groove_offset(&g, &z, 31, 31, 31, 0, 0, step, 0xFF), b = groove_offset(&g, &z, -32, -32, -32, 0, 0, step, 0xFF);
        bad += a > GRV_MAX || b < GRV_MIN;
    }
    check(!bad, "the sum stays inside the scheduling window (under half a step)");
    /* velocity */
    memset(&g, 0, sizeof g);
    g.own = 1;
    g.tmpl = GT_MPC;
    g.vamt = 100;
    check(groove_vel(&g, &z, 100, 0, 0, 0, 0) == 100 && groove_vel(&g, &z, 100, 1, 0, 1, 0) == 70,
          "velocity: the template's accents (MPC 16: the 1 at 100 %, the next 16th at 70 %)");
    g.hum_v = 100;
    for (i = 0, bad = 0; i < 500u; i++) {
        uint32_t v = groove_vel(&g, &z, 1u + i % 127u, i % 16u, 2, i, i % 16u);
        bad += v < 1u || v > 127u;
    }
    check(!bad, "velocity: always 1..127");
    check(groove_slot(5, 4) == 5 && groove_slot(3, 2) == 6 && groove_slot(17, 4) == 1, "slots: sixteenths of the bar");
}

/* --------------------------------------------------------------- timing --- */
static uint64_t blk;
static uint64_t hit_blk[4096];
static uint8_t hit_note[4096], hit_vel[4096];
static uint32_t nh;
static void run_block(void)
{
    int32_t out[CTL * 2];
    uint32_t a = drums.age, k;
    mix_block(out, CTL);
    if (drums.age != a)
        for (k = 0; k < NDRUM; k++)
            if (drums.v[k].age > a && nh < 4096u) {
                hit_blk[nh] = blk;
                hit_note[nh] = drums.v[k].note;
                hit_vel[nh] = drums.v[k].vel;
                nh++;
            }
    blk++;
}
static track_t *D;
static void reset(void)
{
    uint32_t i;
    host_tracks_init();
    for (i = 0; i < NTRK; i++)
        steps_clear(&trk[i]);
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    memset(&song_groove, 0, sizeof song_groove);
    D = TDRUM;
    memset(&D->gv, 0, sizeof D->gv);
    memset(D->pad_off, 0, sizeof D->pad_off);
    memset(D->micro, 0, sizeof D->micro);
    song.g[G_BPM] = 120;
    song.g[G_SWING] = 0;
    D->p[P_SSWING] = 0;
    D->p[P_SLEN] = 16;
    song.rec = 0;
    nh = 0;
    blk = 0;
}
#define STEP_BLK (FS * 60.0 / 120.0 / 4.0 / CTL)      /* a 1/16 at 120 BPM in blocks: 172.27 */
/* the block of the n-th hit of note in the log, -1 = none */
static int64_t nth(uint32_t note, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < nh; i++)
        if (hit_note[i] == note && !n--)
            return (int64_t)hit_blk[i];
    return -1;
}
static void play_bars(uint32_t bars)
{
    transport_req = 1;
    while (blk < (uint64_t)(STEP_BLK * 16.0 * bars) + 2u)
        run_block();
    transport_req = 2;
    run_block();
}

static void t_quantize(void)
{
    static const int s_tab[3] = {100, 0, 50};
    uint32_t k;
    for (k = 0; k < 3u; k++) {
        double want, got;
        char w[96];
        reset();
        dstep_set(&D->dstep[0], 0, LV_NORM, 0);        /* kick on 1 */
        dstep_set(&D->dstep[1], 2, LV_NORM, 0);        /* snare on the next 16th, recorded 16/64 late */
        D->roff[1] = 16;
        groove_set_strength(&D->gv, (uint32_t)s_tab[k]);
        play_bars(1);
        want = STEP_BLK * (1.0 + 0.25 * (100 - s_tab[k]) / 100.0);
        got = (double)(nth(38, 0) - nth(36, 0));
        snprintf(w, sizeof w, "strength %d %%: the late snare plays %.1f blocks after the kick (want %.1f)", s_tab[k], got, want);
        check(got >= want - 1.0 && got <= want + 1.0, w);
    }
}

static void t_record(void)
{
    uint32_t b;
    int8_t want;
    reset();
    song.rec = (uint8_t)(1u << trk_index(D));
    transport_req = 1;
    /* play a snare ~ 1/8 of a 16th late on step 4 (as heard: the recording takes REC_LAT off) */
    b = (uint32_t)(STEP_BLK * 4.0 + STEP_BLK / 8.0 + 512.0 / CTL);
    while (blk < b)
        run_block();
    drum_input(D, 2, LV_NORM, 0, 1);
    run_block();
    transport_req = 2;
    run_block();
    want = 8;
    printf("groove: recorded raw deviation of step 4: %d / 64 (want ~%d)\n", D->roff[4], want);
    check(dstep_has(&D->dstep[4], 2) && D->roff[4] >= want - 2 && D->roff[4] <= want + 2,
          "a live hit keeps its raw deviation (latency-compensated) in its step");
    check(groove_offset(&D->gv, &song_groove, D->roff[4], D->micro[4], 0, 4, 0, 4, 0xFF) == 0,
          "... and plays on the grid at the default strength (100 %)");
}

static void t_swing_once(void)
{
    int64_t a1, a2, v1, v2;
    uint32_t i;
    reset();
    D->p[P_SSWING] = 32;                              /* 58 % */
    for (i = 0; i < 16u; i++)
        dstep_set(&D->dstep[i], 4, LV_NORM, 0);       /* 16th hats */
    play_bars(1);
    a1 = nth(42, 1) - nth(42, 0);
    v1 = hit_vel[1];
    reset();
    D->p[P_SSWING] = 32;
    D->gv.own = 1;
    D->gv.tmpl = GT_MPC;                              /* MPC 16 carries no residual timing: swing stays the swing */
    D->gv.amt = 100;
    D->gv.vamt = 100;
    for (i = 0; i < 16u; i++)
        dstep_set(&D->dstep[i], 4, LV_NORM, 0);
    play_bars(1);
    a2 = nth(42, 1) - nth(42, 0);
    v2 = hit_vel[1];
    printf("groove: swung 16th: %lld blocks (swing alone), %lld (swing + MPC 16); velocities %lld / %lld\n",
           (long long)a1, (long long)a2, (long long)v1, (long long)v2);
    check(a1 == a2 && a1 > (int64_t)(STEP_BLK + 10.0), "swing + a template: the swing is applied once (same timing)");
    check(v2 < v1, "... and the template adds its accents (the swung 16th is softer)");
}

static void t_early_wrap(void)
{
    uint32_t bar, bad = 0, n36 = 0, i;
    reset();
    dstep_set(&D->dstep[0], 0, LV_NORM, 0);
    D->micro[0] = -24;                                /* the kick on 1, early: it fires in the previous bar's last 16th */
    D->gv.own = 1;
    D->gv.hum_t = 50;
    play_bars(8);
    for (i = 0; i < nh; i++)
        n36 += hit_note[i] == 36 && hit_blk[i] < (uint64_t)(STEP_BLK * (16.0 * 8.0 - 0.5));   /* (bar 9's comes early too) */
    for (bar = 1; bar < 8u; bar++) {
        int64_t b = nth(36, bar), at = (int64_t)(STEP_BLK * 16.0 * bar);
        bad += b < 0 || b >= at;                     /* before its bar line */
    }
    check(n36 == 8u && !bad, "an early step 1 fires before each bar line, once a bar, across the wrap");
}

static void t_no_drift(void)
{
    uint32_t bar, bad = 0;
    int64_t dev0 = 0;
    reset();
    dstep_set(&D->dstep[0], 0, LV_NORM, 0);
    D->gv.own = 1;
    D->gv.hum_t = 100;
    D->gv.seed = 1234;
    play_bars(64);
    for (bar = 0; bar < 64u; bar++) {
        int64_t b = nth(36, bar), dev = b - (int64_t)(STEP_BLK * 16.0 * bar + 0.5);
        if (!bar)
            dev0 = dev;
        bad += b < 0 || dev < dev0 - 1 || dev > dev0 + 1;
    }
    check(!bad, "64 bars with humanize: the same deviation every bar (no drift, no accumulation)");
}

static void t_pads(void)
{
    double late, early;
    reset();
    dstep_set(&D->dstep[0], 0, LV_NORM, 0);           /* kick */
    dstep_set(&D->dstep[0], 4, LV_NORM, 0);           /* hat, +12/64 late */
    D->pad_off[4] = 12;
    play_bars(1);
    late = (double)(nth(42, 0) - nth(36, 0));
    reset();
    dstep_set(&D->dstep[4], 0, LV_NORM, 0);           /* kick on beat 2 */
    dstep_set(&D->dstep[4], 2, LV_NORM, 0);           /* snare, -8/64 early */
    D->pad_off[2] = -8;
    play_bars(1);
    early = (double)(nth(36, 0) - nth(38, 0));
    printf("groove: hat pad +12/64: %.0f blocks after the kick; snare pad -8/64: %.0f blocks before it\n", late, early);
    check(late >= STEP_BLK * 12 / 64 - 1 && late <= STEP_BLK * 12 / 64 + 1, "a late pad plays after the others of its step");
    check(early >= STEP_BLK * 8 / 64 - 1 && early <= STEP_BLK * 8 / 64 + 1 && nth(38, 0) < (int64_t)(STEP_BLK * 4.0),
          "an early pad plays before its step and before the others");
}

static void t_commit(void)
{
    static uint64_t a[4096];
    uint32_t i, na, bad = 0;
    reset();
    for (i = 0; i < 16u; i++) {
        dstep_set(&D->dstep[i], i % 4u ? 4u : 0u, LV_NORM, 0);
        D->roff[i] = (int8_t)((int32_t)(i * 7u % 23u) - 11);
    }
    groove_set_strength(&D->gv, 40);
    D->gv.own = 1;
    D->gv.tmpl = GT_BROKEN;
    D->gv.amt = 80;
    D->gv.hum_t = 60;
    play_bars(2);
    na = nh;
    memcpy(a, hit_blk, sizeof(uint64_t) * na);
    groove_commit(D);
    check(D->gv.amt == 0 && D->gv.hum_t == 0 && !D->roff[3], "commit: the groove's timing is now the steps' nudges");
    {
        groove_t keep = D->gv;
        int8_t mk[NSTEP];
        memcpy(mk, D->micro, sizeof mk);
        reset();
        D->gv = keep;
        memcpy(D->micro, mk, sizeof mk);
        for (i = 0; i < 16u; i++)
            dstep_set(&D->dstep[i], i % 4u ? 4u : 0u, LV_NORM, 0);
    }
    play_bars(2);
    for (i = 0; i < na && i < nh; i++)
        bad += hit_blk[i] + 2u < a[i] || hit_blk[i] > a[i] + 2u;
    check(nh == na && !bad, "commit: every hit plays where it did (within the 1/64-step rounding)");
    check(groove_uncommit() && D->gv.amt == 80 && D->gv.hum_t == 60 && D->gv.tmpl == GT_BROKEN && D->roff[3] != 0,
          "undo of a commit restores the groove and the raw timing");
}

int main(void)
{
    t_pure();
    t_quantize();
    t_record();
    t_swing_once();
    t_early_wrap();
    t_no_drift();
    t_pads();
    t_commit();
    printf(fails ? "groove: %d FAILED\n" : "groove: all checks ok\n", fails);
    return fails;
}
