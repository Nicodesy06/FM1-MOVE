/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 Move: the eight-track engine (FM1MOVE=1) on the host, the same sources as the firmware.
 *   audible   eight tracks of mixed kinds play together (two drum tracks, five synths on light and heavy engines,
 *             one more synth), polymetric lengths 16 / 12 / 32 / 7 / 64: every track is heard
 *   budget    synth voices never above NVOICE, drum voices never above NDRUM, no voice left after STOP
 *   drums     two drum tracks: their own kits and voices; a closed hat chokes only its own track's open hat
 *   arenas    three heavy engines hold the three arenas, a fourth is refused, a release frees one
 *   mute/solo tracks 5..8 by bit: the soloed track alone is heard, a muted one is silent
 *   output    bounded, finite, deterministic (two renders, one hash)
 *   cost      host time per block, eight tracks against the first four (information; the device is [TEST-HW])
 * Writes build/host/eight-tracks.wav when given a path. Exit status: the number of failed checks. */
#define FM1MOVE 1
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(int ok, const char *what)
{
    printf("eight: %-74s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static uint32_t drum_voices(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < NDRUM; i++)
        n += drums.v[i].active != 0;
    return n;
}

static void drum_put(track_t *t, uint32_t i, uint32_t lane, uint32_t lvl)
{
    dstep_set(&t->dstep[i % NSTEP], lane, lvl, 0);
    t->seq_active = 1;
}

/* the session: eight tracks, eight bars at 124 BPM */
static void session(void)
{
    static const uint8_t BASS[16] = {36, 0, 36, 48, 0, 36, 0, 43, 36, 0, 46, 0, 36, 48, 0, 41};
    static const uint8_t LEAD[12] = {72, 0, 0, 75, 0, 0, 79, 0, 75, 0, 72, 0};
    static const uint8_t CH1[3] = {60, 63, 67}, CH2[3] = {58, 62, 65};
    track_t *t;
    uint32_t i, k;
    memset(trk, 0, sizeof trk);
    memset(&drums, 0, sizeof drums);
    drums.set = -2;
    memset(eng_arena_trk, 0, sizeof eng_arena_trk);
    host_tracks_init();
    song.g[G_BPM] = 124;
    song.solo = 0;
    for (k = 0; k < NTRK; k++)
        trk[k].kind = 0;
    trk[6].kind = TK_DRUM + 1u;                       /* track 7: a second drum track */
    trk[0].p[P_E0] = DRUM_DEFAULT_KIT;                /* track 1: 808 */
    trk[6].p[P_E0] = DRUM_SAMPLED + 2u;               /* track 7: another synthesised kit */
    trk[6].p[P_PAN] = 30;
    /* drums 1: kick, snare, hats (open hat on the off-beat of beat 4) */
    t = &trk[0];
    for (i = 0; i < 16u; i++) {
        if (i % 4u == 0u) drum_put(t, i, 0, LV_NORM);
        if (i % 8u == 4u) drum_put(t, i, 2, LV_HARD);
        if (i % 2u == 0u) drum_put(t, i, 4, LV_SOFT);
    }
    drum_put(t, 14, 5, LV_NORM);
    /* drums 2: percussion, 12 steps (3 against 4), with closed hats that must not choke track 1's open hat */
    t = &trk[6];
    t->p[P_SLEN] = 12;
    for (i = 0; i < 12u; i++) {
        if (i % 3u == 0u) drum_put(t, i, 14, LV_NORM);
        if (i % 3u == 2u) drum_put(t, i, 4, LV_GHOST);
    }
    drum_put(t, 15 % 12u, 4, LV_HARD);
    /* synths */
    host_preset(&trk[1], 0, 7);                       /* 2 ANALOG bass, mono */
    for (i = 0; i < 16u; i++) {
        uint8_t n = BASS[i];
        put_step(&trk[1], i, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, 0);
    }
    host_preset(&trk[2], 1, 0);                       /* 3 DIGITAL chords, 32 steps */
    trk[2].p[P_SLEN] = 32;
    put_step(&trk[2], 0, 3, CH1, ST_NOTE, 0);
    put_step(&trk[2], 16, 3, CH2, ST_NOTE, 0);
    host_preset(&trk[3], 3, 0);                       /* 4 LOFI lead, 12 steps */
    trk[3].p[P_SLEN] = 12;
    for (i = 0; i < 12u; i++) {
        uint8_t n = LEAD[i];
        put_step(&trk[3], i, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, 0);
    }
    host_preset(&trk[4], ENGI_FM6, 0);                /* 5 FM6 keys (heavy) */
    trk[4].p[P_SLEN] = 7;
    for (i = 0; i < 7u; i += 3u) {
        uint8_t n = (uint8_t)(64 + i);
        put_step(&trk[4], i, 1, &n, ST_NOTE, 0);
    }
    host_preset(&trk[5], ENGI_GRAIN, 0);              /* 6 GRAIN pad (heavy), 64 steps */
    trk[5].p[P_SLEN] = 64;
    put_step(&trk[5], 0, 2, CH1, ST_NOTE, 0);
    put_step(&trk[5], 32, 2, CH2, ST_NOTE, 0);
    host_preset(&trk[7], ENGI_PHYS, 0);               /* 8 PHYS pluck (heavy) */
    for (i = 2; i < 16u; i += 4u) {
        uint8_t n = (uint8_t)(55 + i);
        put_step(&trk[7], i, 1, &n, ST_NOTE, 0);
    }
    for (k = 0; k < NTRK; k++)
        trk[k].seq_active = 1;
}

static uint32_t peak[NTRK], vmax, dmax, nonfinite, clip;
static uint64_t hash;
static FILE *wav;
static void run(uint32_t blocks)
{
    int32_t out[2 * CTL];
    uint32_t b, i, k;
    for (b = 0; b < blocks; b++) {
        for (k = 0; k < NTRK; k++)
            trk[k].peak = 0;
        mix_block(out, CTL);
        for (k = 0; k < NTRK; k++)
            if ((uint32_t)trk[k].peak > peak[k])
                peak[k] = (uint32_t)trk[k].peak;
        if (busy_now() > vmax)
            vmax = busy_now();
        if (drum_voices() > dmax)
            dmax = drum_voices();
        for (i = 0; i < 2u * CTL; i++) {
            int32_t v = out[i];
            hash = (hash ^ (uint32_t)v) * 1099511628211ull;
            if (v > 32767 || v < -32768)
                clip++;
            if (v != v)
                nonfinite++;
        }
        if (wav)
            for (i = 0; i < CTL; i++)
                wav_put(wav, out[2 * i], out[2 * i + 1]);
    }
}
static void zero_stats(void)
{
    memset(peak, 0, sizeof peak);
    vmax = dmax = clip = nonfinite = 0;
    hash = 1469598103934665603ull;
}

#define BAR_BLOCKS(bpm) ((uint32_t)(FS * 240u / (bpm) / CTL))

int main(int argc, char **argv)
{
    uint32_t k, quiet = 1, bars8 = 8u * BAR_BLOCKS(124);
    uint64_t h1, t0, t8, t4;
    char what[96];
    if (argc > 1 && !strcmp(argv[1], "--hash")) {   /* the determinism check: the session's hash only */
        session();
        zero_stats();
        transport_req = 1;
        run(bars8);
        printf("%llx\n", (unsigned long long)hash);
        return 0;
    }
    if (argc > 1) {
        wav = fopen(argv[1], "wb");
        if (wav)
            wav_hdr(wav, bars8 * CTL);
    }
    printf("eight: NTRK %u, synth voice budget %u, drum voices %u, heavy-engine arenas %u\n", NTRK, NVOICE, NDRUM,
           (unsigned)N_ARENA);
    check(NTRK == 8u, "the build has eight tracks");

    /* every track heard */
    session();
    zero_stats();
    transport_req = 1;
    t0 = now_ns();
    run(bars8);
    t8 = now_ns() - t0;
    if (wav)
        fclose(wav), wav = 0;
    for (k = 0; k < NTRK; k++) {
        snprintf(what, sizeof what, "track %u (%s) is heard (peak %u)", k + 1u,
                 is_drum(&trk[k]) ? "drums" : ENGINES[trk[k].engine]->name, peak[k]);
        check(peak[k] > 300u, what);
    }
    check(vmax <= NVOICE, "synth voices never above the shared budget");
    check(dmax <= NDRUM, "drum voices never above NDRUM");
    check(!clip && !nonfinite, "the output is bounded and finite");
    h1 = hash;
    {   /* the same session in a fresh process (the same power-on state: delay and reverb lines empty) */
        int fd[2];
        uint64_t h2 = 0;
        pid_t pid;
        if (pipe(fd) == 0 && (pid = fork()) == 0) {
            close(fd[0]);
            /* (the child is a copy of this process after the render: rebuild the power-on state by exec) */
            dup2(fd[1], 1);
            execl("/proc/self/exe", "eight_track_test", "--hash", (char *)0);
            _exit(127);
        } else if (pid > 0) {
            char b[64] = {0};
            close(fd[1]);
            if (read(fd[0], b, sizeof b - 1) > 0)
                h2 = strtoull(b, 0, 16);
            close(fd[0]);
            waitpid(pid, 0, 0);
        }
        check(h2 == h1, "a second process renders the session bit for bit (deterministic)");
    }

    /* the arenas: tracks 5, 6, 8 play FM6, GRAIN, PHYS */
    check(eng_arena_find(&trk[4]) >= 0 && eng_arena_find(&trk[5]) >= 0 && eng_arena_find(&trk[7]) >= 0,
          "the three heavy engines each hold an arena");
    check(!eng_heavy_ok(&trk[1], ENGI_FM6) && eng_heavy_ok(&trk[1], 0),
          "a fourth heavy engine is refused, a light one is not");
    trk[5].eng_req = 0;                               /* track 6 to ANALOG: after its fade the arena is free */
    run(BAR_BLOCKS(124));
    check(trk[5].engine == 0 && eng_arena_find(&trk[5]) < 0 && eng_heavy_ok(&trk[1], ENGI_FM6),
          "a heavy engine left: its arena is free for another track");

    /* the two drum tracks keep their own voices and chokes */
    {
        uint32_t i, own0 = 0, own6 = 0;
        session();
        transport_req = 2;
        run(4);
        drum_on(&trk[0], 46, 100);                    /* track 1: an open hat */
        drum_on(&trk[6], 42, 100);                    /* track 7: a closed hat */
        for (i = 0; i < NDRUM; i++) {
            own0 += drums.v[i].active && drums.own[i] == 0u && drums.v[i].note == 46u;
            own6 += drums.v[i].active && drums.own[i] == 6u;
        }
        check(own0 == 1u && own6 >= 1u, "a closed hat on track 7 leaves track 1's open hat ringing");
        drum_on(&trk[0], 42, 100);
        for (i = 0, own0 = 0; i < NDRUM; i++)
            own0 += drums.v[i].active && drums.own[i] == 0u && drums.v[i].note == 46u;
        check(own0 == 0u, "a closed hat on track 1 chokes its own open hat");
    }

    /* mute / solo by bit, tracks 5..8 */
    session();
    transport_req = 1;
    run(2u * BAR_BLOCKS(124));
    song.solo = 1u << 7;                              /* solo track 8 */
    run(BAR_BLOCKS(124) / 2u);                         /* (the fades) */
    zero_stats();
    run(2u * BAR_BLOCKS(124));
    for (k = 0; k < 7u; k++)
        quiet &= peak[k] == 0u;
    check(quiet && peak[7] > 300u, "track 8 soloed: only track 8 is heard");
    song.solo = 0;
    trk[6].p[P_MUTE] = 1;                             /* mute drums 2 */
    run(BAR_BLOCKS(124) / 2u);
    zero_stats();
    run(2u * BAR_BLOCKS(124));
    check(peak[6] == 0u && peak[0] > 300u, "track 7 muted: silent, track 1 still heard");
    trk[6].p[P_MUTE] = 0;

    /* the budget under pressure: eight synth tracks each hold a protected mono lead (the whole budget), then one of
     * them, now POLY, plays a ninth note: another track's voice must give way (SLOOP's three parts never got here) */
    {
        uint32_t over = 0, b;
        session();
        transport_req = 2;
        run(4);
        for (k = 0; k < NTRK; k++) {
            trk_set_kind(&trk[k], TK_SYNTH);
            host_preset(&trk[k], 0, 7);
            trk[k].p[P_VOICE] = V_MONO;
            trk_note_on(&trk[k], 40u + k, 100);
        }
        run(2);
        printf("eight: voices held before the ninth note: %u\n", busy_now());
        trk[2].p[P_VOICE] = V_POLY;
        trk_note_on(&trk[2], 70, 100);
        for (b = 0; b < 200u; b++) {
            run(1);
            over += busy_now() > NVOICE;
        }
        check(!over, "eight held leads and a ninth note: the shared budget still holds");
        trk_set_kind(&trk[0], TK_DRUM);              /* back: voices sounding on a track that changes kind stop */
        trk_set_kind(&trk[6], TK_DRUM);
        check(!trk[0].v[0].active && !trk[6].v[0].active && is_drum(&trk[0]),
              "a track that changes kind stops its voices (none left hanging)");
        for (k = 0; k < NTRK; k++)
            trk_all_off(&trk[k]);
    }

    /* stop: everything ends */
    transport_req = 2;
    run(2u * FS / CTL);
    check(busy_now() == 0u && drum_voices() == 0u, "STOP: no synth or drum voice left after 2 s");

    /* cost: eight tracks against the first four (host only) */
    session();
    for (k = 4; k < NTRK; k++)
        steps_clear(&trk[k]);
    zero_stats();
    transport_req = 1;
    t0 = now_ns();
    run(bars8);
    t4 = now_ns() - t0;
    printf("eight: host time per block: 8 tracks %.2f us, 4 tracks %.2f us (x%.2f)\n", t8 / 1000.0 / bars8,
           t4 / 1000.0 / bars8, (double)t8 / (double)(t4 ? t4 : 1));
    printf(fails ? "eight: %d FAILED\n" : "eight: all checks ok\n", fails);
    return fails;
}
