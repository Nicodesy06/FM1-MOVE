/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 Move clips (firmware/src/clip.c) on the host, with the real sequencer, mixer, project format and storage:
 *   bank        a pattern stored as its track's clip, launched back exactly (steps, LENGTH, DIV); empty slots
 *   launch      stopped: at once; playing: on the next bar, in the block that starts it (its step 0 on time)
 *   no loss     what the outgoing clip got just before its bar (no main loop pass since) is in it after the switch
 *   scenes      clip k on every track, every track in the same block
 *   tools       copy, erase, a new kind empties the track's clips, STOP before the bar starts a queued clip at once
 *   project     version 2 saves the clips and loads them back byte for byte
 *   full        a full bank: the store is refused, the pattern playing is still saved with the project and loads
 * Exit status: the number of failed checks. */
#define FM1MOVE 1
#define main hostsim_main
#include "hostsim.c"
#undef main

static uint8_t nor[0x100000];
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

static int fails;
static void check(int ok, const char *what)
{
    printf("clip: %-76s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static void note_at(track_t *t, uint32_t i, uint8_t note)
{
    put_step(t, i, 1, &note, ST_NOTE, 0);
    t->seq_active = 1;
}
static uint32_t pat_hash(const track_t *t)               /* the pattern and its LENGTH / DIV, as one number */
{
    uint32_t h = 2166136261u, i;
    const uint8_t *b = (const uint8_t *)t->step;
    for (i = 0; i < sizeof t->step; i++)
        h = (h ^ b[i]) * 16777619u;
    for (i = 0; i < NSTEP; i++)
        h = (h ^ (uint8_t)t->micro[i]) * 16777619u;
    for (i = 0; i < NLOCK; i++)
        if (t->lock[i].step != LOCK_FREE)
            h = (h ^ (t->lock[i].step | (uint32_t)t->lock[i].param << 8 | (uint32_t)(uint16_t)t->lock[i].val << 16)) * 16777619u;
    return (h ^ (uint32_t)t->p[P_SLEN] * 31u ^ (uint32_t)t->p[P_SDIV] * 131u);
}
static void run(uint32_t blocks)
{
    int32_t out[2 * CTL];
    while (blocks--)
        mix_block(out, CTL);
}
static void play(void)
{
    transport_req = 1;
    run(2);
}
static void stop(void)
{
    transport_req = 2;
    run(2);
}
/* render up to the end of the bar playing: the bar's last block is rendered, the clock now stands at the next bar
 * (clk_beat a multiple of 4), whose first block (where events_block starts the clips asked for) is not rendered yet */
static uint32_t to_bar_end(void)
{
    uint32_t n = 0, adv = CTL * (uint32_t)song.g[G_BPM];
    while (!((clk_beat & 3u) == 3u && clk_pos + adv >= BEAT_U) && n < 100000u) {
        run(1);
        n++;
    }
    run(1);
    return n + 1u;
}

int main(void)
{
    track_t *t = &trk[1];
    uint32_t hA, hB, i, k;
    memset(nor, 0xFF, sizeof nor);
    memset(trk, 0, sizeof trk);
    host_tracks_init();
    song.g[G_BPM] = 120;
    clip_init();

    /* ---- the bank, stopped: pattern A is clip 1; clip 2 starts empty, gets pattern B; back and forth */
    note_at(t, 0, 60);
    note_at(t, 4, 63);
    t->p[P_SLEN] = 16;
    clip_sync_all();
    hA = pat_hash(t);
    check(clip_state(1, 0) == 2 && clip_len[1][0] > 0 && clip_state(1, 1) == 0, "a pattern is stored as the clip playing (clip 1)");
    clip_launch(1, 1);
    check(clip_state(1, 1) == 2 && clip_state(1, 0) == 1 && !t->step[0].n, "stopped: clip 2 starts at once, empty; clip 1 keeps pattern A");
    note_at(t, 0, 67);
    note_at(t, 3, 70);
    t->p[P_SLEN] = 8;
    t->p[P_SDIV] = 3;
    clip_sync_all();
    hB = pat_hash(t);
    clip_launch(1, 0);
    check(pat_hash(t) == hA && t->p[P_SLEN] == 16, "clip 1 back: pattern A exactly, its LENGTH 16");
    clip_launch(1, 1);
    check(pat_hash(t) == hB && t->p[P_SLEN] == 8 && t->p[P_SDIV] == 3, "clip 2 back: pattern B exactly, its LENGTH 8 and DIV");

    /* ---- playing: a launch waits for the next bar and happens in the block that starts it */
    clip_launch(1, 0);
    play();
    run(3000);                                            /* (somewhere inside a bar) */
    clip_launch(1, 1);
    check(clip_state(1, 1) == 3 && pat_hash(t) == hA, "playing: clip 2 is asked for, clip 1 keeps playing until the bar");
    to_bar_end();
    check(pat_hash(t) == hA && (clk_beat & 3u) == 0u, "... still clip 1 through the last block of the bar");
    run(1);
    check(pat_hash(t) == hB && (clk_beat & 3u) == 0u && clk_pos <= CTL * (uint32_t)song.g[G_BPM] * 2u,
          "... clip 2 from the first block of the next bar");
    run(4);                                               /* (the voice's attack) */
    {
        uint32_t v, on = 0;
        for (v = 0; v < NVOICE; v++)
            on |= t->v[v].active && t->v[v].note == 67;
        check(on, "... and its step 0 (note 67) sounds on that bar");
    }

    /* ---- an odd LENGTH launched after some bars still starts on its step 0 on the bar */
    stop();
    clip_launch(1, 5);
    note_at(t, 0, 74);
    t->p[P_SLEN] = 5;
    t->p[P_SDIV] = 0;
    clip_sync_all();
    clip_launch(1, 1);
    play();
    run(5000);
    clip_launch(1, 5);
    to_bar_end();
    run(1);
    check(t->p[P_SLEN] == 5 && t->seq_idx == 0, "a LENGTH 5 clip launched after some bars: on the bar it plays its step 0");
    run(4);
    {
        uint32_t v, on = 0;
        for (v = 0; v < NVOICE; v++)
            on |= t->v[v].active && t->v[v].note == 74;
        check(on, "... and that step sounds");
    }
    clip_launch(1, 1);
    to_bar_end();
    run(1);
    clip_erase(1, 5);

    /* ---- no loss: a step written into the outgoing clip just before its bar (no main loop pass) is kept */
    clip_launch(1, 0);
    to_bar_end();
    note_at(t, 6, 72);                                    /* (as a live recording would, in the last block) */
    run(1);
    check(pat_hash(t) == hA, "the switch to clip 1 on the bar");
    clip_launch(1, 1);
    to_bar_end();
    run(1);
    check(t->step[6].n == 1 && t->step[6].note[0] == 72, "the step written into clip 2 in its last block was kept");
    note_at(t, 6, 0);                                     /* (B again, for what follows) */
    t->step[6].n = 0;
    t->step[6].time = ST_REST;
    clip_sync_all();

    /* ---- scenes: clip k on every track, together */
    for (k = 0; k < NTRK; k++)
        if (!is_drum(&trk[k]) && k != 1) {
            clip_launch(k, 2);                            /* (queued: playing) */
        }
    to_bar_end();
    run(1);
    for (k = 0; k < NTRK; k++)
        if (!is_drum(&trk[k]) && k != 1) {
            note_at(&trk[k], 0, (uint8_t)(48 + k));       /* clip 3 of each synth track gets a note */
        }
    clip_sync_all();
    clip_scene(0);
    to_bar_end();
    run(1);
    {
        int all = 1;
        for (k = 0; k < NTRK; k++)
            all &= clip_cur[k] == 0;
        check(all && pat_hash(t) == hA, "scene 1: every track on clip 1 in the same block");
    }
    clip_scene(2);
    to_bar_end();
    run(1);
    {
        int all = 1;
        for (k = 0; k < NTRK; k++)
            if (!is_drum(&trk[k]) && k != 1)
                all &= trk[k].step[0].n == 1 && trk[k].step[0].note[0] == 48 + k;
        check(all && clip_cur[0] == 2 && !t->step[0].n, "scene 3: each track's clip 3 (empty ones go quiet)");
    }

    /* ---- STOP before the bar: the clip asked for starts at once */
    clip_launch(1, 0);
    stop();
    clip_tick();
    check(clip_cur[1] == 0 && pat_hash(t) == hA, "STOP before the bar: the clip asked for starts when stopped");

    /* ---- copy, erase, a new kind */
    check(clip_copy(1, 0, 4) == 0 && clip_state(1, 4) == 1, "copy clip 1 into slot 5");
    clip_launch(1, 4);
    check(pat_hash(t) == hA, "... slot 5 plays pattern A");
    clip_erase(1, 4);
    check(clip_state(1, 4) == 2 && !t->step[0].n && clip_len[1][4] == 0, "erase the clip playing: it and its pattern are empty");
    clip_launch(1, 0);
    trk_set_kind(&trk[2], TK_DRUM);
    clip_reset_track(2);
    check(clip_len[2][0] == 0 && clip_len[2][2] == 0 && clip_cur[2] == 0, "a new kind: the track's clips are emptied");
    trk_set_kind(&trk[2], TK_SYNTH);
    clip_reset_track(2);

    /* ---- the project: version 2 carries the clips */
    {
        static uint8_t mem0[CLIP_MEM];
        static uint16_t len0[NTRK][NCLIP];
        uint8_t cur0[NTRK];
        uint32_t ok = 1, j;
        for (k = 0; k < NCLIP; k++)
            if (k != 1u) {
                clip_launch(3, k);
                for (i = 0; i < 8u; i++)
                    note_at(&trk[3], i * 2u, (uint8_t)(60 + k + i));
                trk[3].p[P_SLEN] = (int16_t)(8 + k);
            }
        clip_launch(3, 3);
        clip_sync_all();
        for (k = 0; k < NTRK; k++) {
            cur0[k] = clip_cur[k];
            for (j = 0; j < NCLIP; j++) {
                len0[k][j] = clip_len[k][j];
                memcpy(mem0 + (k * NCLIP + j) * 0u, mem0, 0);
            }
        }
        hA = pat_hash(&trk[3]);
        {
            static uint8_t snap[NTRK][NCLIP][1500];
            for (k = 0; k < NTRK; k++)
                for (j = 0; j < NCLIP; j++)
                    memcpy(snap[k][j], clip_mem + clip_off[k][j], clip_len[k][j]);
            check(fmp_save(0) == 0, "a project with clips saves");
            clip_init();
            for (k = 0; k < NTRK; k++)
                steps_clear(&trk[k]);
            check(fmp_load(0, 0) == 0, "... and loads");
            for (k = 0; k < NTRK; k++) {
                ok &= clip_cur[k] == cur0[k];
                for (j = 0; j < NCLIP; j++)
                    ok &= clip_len[k][j] == len0[k][j] && !memcmp(snap[k][j], clip_mem + clip_off[k][j], clip_len[k][j]);
            }
            check(ok && pat_hash(&trk[3]) == hA, "... every clip byte for byte, each track on its clip, the pattern playing");
        }
    }

    /* ---- a full bank: the store is refused, the pattern playing still saves and loads */
    {
        uint32_t tries = 0, h;
        clip_full = 0;
        for (k = 0; k < NTRK && !clip_full; k++) {
            if (is_drum(&trk[k]))
                continue;
            for (i = 0; i < NCLIP && !clip_full; i++) {
                uint32_t s;
                clip_launch(k, i);
                for (s = 0; s < NSTEP; s++) {
                    uint8_t ch[4] = {(uint8_t)(40 + s % 30), (uint8_t)(50 + s % 20), (uint8_t)(60 + s % 10), (uint8_t)(70 + k)};
                    put_step(&trk[k], s, 4, ch, ST_NOTE, s & 3u);
                    trk[k].micro[s] = (int8_t)((s % 7u) - 3);
                }
                trk[k].p[P_SLEN] = NSTEP;
                clip_sync_all();
                tries++;
            }
        }
        check(clip_full && clip_out[clip_cur[k - 1] < NCLIP ? k - 1 : 0], "dense clips fill the bank: the store is refused (CLIP MEMORY FULL)");
        h = pat_hash(&trk[k - 1]);
        check(fmp_save(1) == 0, "... the project still saves (the pattern playing in its track)");
        steps_clear(&trk[k - 1]);
        check(fmp_load(1, 0) == 0 && pat_hash(&trk[k - 1]) == h, "... and the pattern that did not fit the bank loads back exactly");
        printf("clip: the bank held %u bytes of %u after %u dense clips\n", (unsigned)clip_used, CLIP_MEM, (unsigned)tries);
    }

    printf(fails ? "clip: %d FAILED\n" : "clip: all checks ok\n", fails);
    return fails;
}
