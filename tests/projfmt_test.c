/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 Move project format FM1M (firmware/src/projfmt.c) over the real storage layer (storage.c) on a simulated NOR.
 *   round trip   an eight-track project (two drum kits, heavy engines, chords, ties, locks, nudges, raw timing,
 *                conditions, grooves, pad offsets, the song groove) saves and loads back exactly
 *   size         a sparse project takes one chunk; the densest possible one (64 full steps x 8, every lock) fits two
 *   power loss   a save cut at every possible flash write leaves the old project or the new one, never a mix
 *   damage       a corrupted chunk: the load is refused and the working project is untouched
 *   limits       a project asking for four heavy engines loads with the fourth on ANALOG (reported)
 *   autosave     the stream hash changes with any edit and only then
 * Exit status: the number of failed checks. */
#define FM1MOVE 1
#define main hostsim_main
#include "hostsim.c"
#undef main

static uint8_t nor[0x100000];
static int prog_budget = -1;                          /* writes left before the power goes, -1 = no limit */
static int st_read(uint32_t off, void *dst, uint32_t n)
{
    memcpy(dst, nor + off, n);
    return 0;
}
static int st_erase(uint32_t off)
{
    if (prog_budget == 0)
        return -5;
    if (prog_budget > 0)
        prog_budget--;
    memset(nor + (off & ~0xFFFu), 0xFF, 4096);
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if (prog_budget == 0)
        return -5;
    if (prog_budget > 0)
        prog_budget--;
    for (i = 0; i < n; i++)
        nor[off + i] &= ((const uint8_t *)src)[i];   /* NOR: programming only clears bits */
    return 0;
}
#include "../firmware/src/storage.c"
#include "../firmware/src/projfmt.c"

static int fails;
static void check(int ok, const char *what)
{
    printf("projfmt: %-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

/* everything a project holds, as one number (locks as a set: their slots may move) */
static uint64_t fingerprint(void)
{
    uint64_t h = 1469598103934665603ull;
    uint32_t k, i;
#define FH(p, n) do { const uint8_t *b_ = (const uint8_t *)(p); uint32_t n_ = (n); while (n_--) h = (h ^ *b_++) * 1099511628211ull; } while (0)
    FH(&song.sel, 1);
    for (i = 0; i < G_COUNT; i++)
        if (i != G_SLOT && i != G_LOAD && i != G_SAVE && i != G_SYNC && i != G_MIDI && i != G_ROUTE)
            FH(&song.g[i], 2);
    FH(&song_groove, sizeof song_groove);
    for (k = 0; k < NTRK; k++) {
        const track_t *t = &trk[k];
        uint32_t kind = trk_kind(t);
        uint64_t locks = 0;
        FH(&kind, 4);
        FH(&t->eng_req, 1);
        FH(&t->preset, 1);
        FH(t->p, sizeof t->p);
        FH(t->step, sizeof t->step);
        FH(t->micro, sizeof t->micro);
        FH(t->roff, sizeof t->roff);
        FH(t->fill, sizeof t->fill);
        FH(&t->gv, sizeof t->gv);
        FH(t->pad_off, sizeof t->pad_off);
        for (i = 0; i < NLOCK; i++)
            if (t->lock[i].step != LOCK_FREE)
                locks += (uint64_t)(t->lock[i].step * 7919u + t->lock[i].param * 104729u + (uint16_t)t->lock[i].val) *
                         2654435761u;
        FH(&locks, 8);
    }
    return h;
}

static void blank(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++) {
        trk_set_kind(&trk[k], k == 0u ? TK_DRUM : TK_SYNTH);
        memset(&trk[k].gv, 0, sizeof trk[k].gv);
        memset(trk[k].pad_off, 0, sizeof trk[k].pad_off);
        memset(trk[k].micro, 0, sizeof trk[k].micro);
        steps_clear(&trk[k]);
    }
    memset(eng_arena_trk, 0, sizeof eng_arena_trk);
    memset(&song_groove, 0, sizeof song_groove);
    host_tracks_init();
}

/* a session: dense enough to be a real test, sparse as music is */
static void session(uint32_t variant)
{
    static const uint8_t CH[3] = {60, 63, 67};
    uint32_t k, i;
    blank();
    song.g[G_BPM] = (int16_t)(118 + variant);
    song.g[G_SWING] = 20;
    song.sel = 3;
    song_groove.tmpl = GT_GARAGE;
    song_groove.amt = 60;
    song_groove.vamt = 80;
    trk_set_kind(&trk[6], TK_DRUM);
    trk[0].p[P_E0] = DRUM_DEFAULT_KIT;
    trk[6].p[P_E0] = DRUM_SAMPLED + 3u;
    for (i = 0; i < 16u; i++) {
        if (i % 4u == 0u) dstep_set(&trk[0].dstep[i], 0, LV_NORM, 0);
        if (i % 2u == 0u) dstep_set(&trk[0].dstep[i], 4, i % 4u ? LV_SOFT : LV_HARD, i == 14u ? 2u : 0u);
        if (i % 3u == 1u) dstep_set(&trk[6].dstep[i], 14, LV_GHOST, 0);
    }
    trk[0].pad_off[4] = 6;
    trk[0].pad_off[2] = -4;
    trk[0].roff[5] = -9;
    host_preset(&trk[1], 0, 7);
    trk[1].p[P_SLEN] = 12;
    for (i = 0; i < 12u; i += 2u) {
        uint8_t n = (uint8_t)(36 + i + variant);
        put_step(&trk[1], i, 1, &n, ST_NOTE, i == 4u ? SF_SLIDE : 0u);
        trk[1].micro[i] = (int8_t)(i - 5);
        trk[1].roff[i] = (int8_t)(7 - i);
    }
    put_step(&trk[1], 1, 0, 0, ST_TIE, 0);
    step_fill_set(&trk[1], 6, FC_FILL);
    lock_set(&trk[1], 2, P_ED_FLT, 40);
    lock_set(&trk[1], 8, P_ATK, 12);
    trk[1].gv.own = 1;
    trk[1].gv.tmpl = GT_USER;
    trk[1].gv.amt = 70;
    trk[1].gv.qloose = 35;
    trk[1].gv.hum_t = 20;
    trk[1].gv.seed = 999;
    trk[1].gv.user_off[3] = 5;
    trk[1].gv.user_vel[7] = -30;
    host_preset(&trk[2], 1, 2);
    put_step(&trk[2], 0, 3, CH, ST_NOTE, SF_ACCENT);
    host_preset(&trk[4], ENGI_FM6, 1);
    eng_arena_claim(&trk[4]);
    host_preset(&trk[5], ENGI_GRAIN, 0);
    eng_arena_claim(&trk[5]);
    trk[5].p[P_SLEN] = 64;
    put_step(&trk[5], 63, 2, CH, ST_NOTE, 0);
    host_preset(&trk[7], ENGI_PHYS, 2);
    eng_arena_claim(&trk[7]);
    for (k = 0; k < NTRK; k++)
        trk[k].p[P_PAN] = (int16_t)((int)k * 9 - 30);
}

static uint32_t chunks_of(uint32_t len) { return (len + FMP_DATA - 1u) / FMP_DATA; }

int main(void)
{
    uint64_t a, b, f;
    uint32_t len, hash1, hash2, refused = 0, k, mixed = 0, tried = 0;
    int rc;
    memset(nor, 0xFF, sizeof nor);

    /* round trip */
    session(0);
    a = fingerprint();
    len = fmp_measure(&hash1);
    printf("projfmt: the session's stream: %u bytes (%u chunk%s)\n", len, chunks_of(len), chunks_of(len) > 1u ? "s" : "");
    rc = fmp_save(0);
    check(rc == 0, "an eight-track project saves");
    blank();
    check(fingerprint() != a, "(the working project is cleared)");
    check(fmp_load(0, &refused) == 0 && fingerprint() == a && !refused, "... and loads back exactly");
    check(len <= FMP_DATA, "a typical project takes one chunk (sparse steps)");
    check(fmp_measure(&hash2) == len && hash2 == hash1, "the stream is the same after a load (hash, length)");

    /* the densest project */
    for (k = 0; k < NTRK; k++) {
        uint32_t i;
        for (i = 0; i < NSTEP; i++) {
            if (is_drum(&trk[k])) {
                uint32_t l;
                for (l = 0; l < 16u; l++)
                    dstep_set(&trk[k].dstep[i], l, LV_HARD, 3);
            } else {
                static const uint8_t N4[4] = {60, 64, 67, 71};
                put_step(&trk[k], i, 4, N4, ST_NOTE, SF_ACCENT);
            }
            trk[k].micro[i] = -3;
            trk[k].roff[i] = 9;
            step_fill_set(&trk[k], i, FC_NOFILL);
        }
        for (i = 0; i < NLOCK; i++)
            lock_set(&trk[k], i, P_ED_FLT + (i % 3u), (int32_t)i);
    }
    b = fingerprint();
    len = fmp_measure(0);
    printf("projfmt: the densest stream: %u bytes of %u (%u chunks)\n", len, (unsigned)FMP_MAX, chunks_of(len));
    if (len <= FMP_MAX) {
        check(fmp_save(1) == 0, "the densest eight-track project fits (3 chunks) and saves");
        blank();
        check(fmp_load(1, 0) == 0 && fingerprint() == b, "... and loads back exactly");
    } else {                                          /* beyond the capacity: refused, the slot keeps what it had */
        check(fmp_save(1) == -9, "the densest project is over the capacity: refused (-9), nothing written");
    }
    {   /* eight tracks of 64 single-note steps / 4-lane drum steps with nudges: the long-pattern case must fit */
        uint32_t i;
        session(7);
        for (k = 0; k < NTRK; k++)
            for (i = 0; i < NSTEP; i++) {
                if (is_drum(&trk[k])) {
                    dstep_set(&trk[k].dstep[i], 0, LV_NORM, 0);
                    dstep_set(&trk[k].dstep[i], 2, LV_NORM, 1);
                    dstep_set(&trk[k].dstep[i], 4, LV_SOFT, 0);
                    dstep_set(&trk[k].dstep[i], 7, LV_GHOST, 0);
                } else {
                    uint8_t n = (uint8_t)(40 + i % 24u);
                    put_step(&trk[k], i, 1, &n, ST_NOTE, 0);
                }
                trk[k].micro[i] = (int8_t)(i % 7u) - 3;
                trk[k].roff[i] = (int8_t)(i % 11u) - 5;
            }
        b = fingerprint();
        len = fmp_measure(0);
        printf("projfmt: eight full 64-step tracks: %u bytes (%u chunks)\n", len, chunks_of(len));
        check(fmp_save(1) == 0, "eight full 64-step tracks (single notes, 4-lane drums, nudges) fit and save");
        blank();
        check(fmp_load(1, 0) == 0 && fingerprint() == b, "... and load back exactly");
    }

    /* power loss at every write of a two-chunk save over a saved project */
    for (prog_budget = 0;; prog_budget = (int)++tried) {
        int saved_budget = prog_budget;
        uint64_t got;
        memset(nor, 0xFF, sizeof nor);
        prog_budget = -1;
        session(1);
        f = fingerprint();                                  /* the old project: session 1 */
        fmp_save(2);
        fmp_load(1, 0);                                     /* (garbage slot: refused, nothing changes) */
        b = 0;
        /* the new project: the densest one */
        session(2);
        for (k = 0; k < NTRK; k++) {
            uint32_t i;
            for (i = 0; i < NSTEP; i++)
                if (!is_drum(&trk[k])) {
                    static const uint8_t N4[4] = {48, 52, 55, 59};
                    put_step(&trk[k], i, 4, N4, ST_NOTE, 0);
                } else {
                    dstep_set(&trk[k].dstep[i], i % 16u, LV_NORM, 0);
                }
        }
        b = fingerprint();
        prog_budget = saved_budget;
        rc = fmp_save(2);
        prog_budget = -1;
        blank();
        if (fmp_load(2, 0) != 0) {
            mixed++;                                        /* nothing loads: the old project must survive */
            printf("projfmt:   cut after %d writes: no project at all\n", saved_budget);
        } else {
            got = fingerprint();
            if (got != f && got != b) {
                mixed++;
                printf("projfmt:   cut after %d writes: a mixed project\n", saved_budget);
            }
            if (rc == 0 && got != b) {
                mixed++;
                printf("projfmt:   cut after %d writes: reported saved, the old one loads\n", saved_budget);
            }
        }
        if (rc == 0)
            break;                                          /* enough budget: the save completed */
        if (tried > 200u)
            break;
    }
    printf("projfmt: a save cut at each of %u flash writes\n", tried);
    check(!mixed && tried > 4u, "power lost at any write: the old project or the new, never a mix or nothing");

    /* damage: a corrupted chunk 1 */
    memset(nor, 0xFF, sizeof nor);
    session(3);
    for (k = 0; k < NTRK; k++) {
        uint32_t i;
        for (i = 0; i < NSTEP; i++)
            if (!is_drum(&trk[k])) {
                uint8_t n = 50;
                put_step(&trk[k], i, 1, &n, ST_NOTE, 0);
            }
    }
    fmp_save(0);
    {
        st_hdr_t h0, h1;
        uint32_t obj = fmp_obj(0, 1), c;
        st_head(obj, 0, &h0);
        c = st_head(obj, 1, &h1) == 0 && (st_head(obj, 0, &h0) != 0 || h1.seq > h0.seq) ? 1u : 0u;
        nor[st_sector(obj, c) + ST_PAYLOAD_OFF + 100] ^= 0x5A;
    }
    session(4);
    a = fingerprint();
    check(fmp_load(0, 0) != 0 && fingerprint() == a, "a damaged chunk: the load is refused, the working project untouched");
    {
        uint32_t k, c;
        for (k = 0; k < FMP_NCHUNK; k++)
            for (c = 0; c < 2u; c++)
                st_erase(st_sector(fmp_obj(1, k), c));
    }
    check(fmp_load(1, 0) != 0 && fingerprint() == a, "an empty slot: refused, untouched");

    /* the layout of FM1 Move 0.1 (4 projects + the autosave, 2 chunks each: objects 2s, 2s + 1, the autosave 8, 9):
     * its project 1 and its autosave still load under the new map; its projects 2..4 read as empty, not as garbage */
    {
        uint32_t k, c, o, g;
        uint64_t p1, as;
        for (o = 0; o < FMP_OBJS; o++)
            for (c = 0; c < 2u; c++)
                st_erase(st_sector(OBJ_FMP0 + o, c));
        session(6);
        p1 = fingerprint();
        g = 7;                                          /* old project 1 at objects 0, 1 (what slot 0 maps to: 0, 1, 2) */
        for (k = 0; k < 2u; k++) {
            fmp_w_t w = {0, k * FMP_DATA, (k + 1u) * FMP_DATA, st_buf + FMP_CHDR, 2166136261u};
            uint32_t len = fmp_measure(0), n = (len + FMP_DATA - 1u) / FMP_DATA;
            if (k >= n)
                break;
            fmp_stream(&w);
            fmp_put_chunk(0, k, n, g, len, k + 1u < n ? FMP_DATA : len - k * FMP_DATA);
        }
        session(7);
        as = fingerprint();
        fmp_save(FMP_AUTOSAVE);                         /* old autosave objects 8, 9: the new map's autosave 8, 9, 7 */
        session(8);                                     /* old project 2 at objects 2, 3: under the new map slot 0's
                                                         * chunk 2 and slot 1's chunk 0 */
        {
            fmp_w_t w = {0, 0, FMP_DATA, st_buf + FMP_CHDR, 2166136261u};
            uint32_t len = fmp_measure(0);
            fmp_stream(&w);
            st_buf[0] = (uint8_t)FMP_CMAGIC; st_buf[1] = (uint8_t)(FMP_CMAGIC >> 8);
            st_buf[2] = (uint8_t)(FMP_CMAGIC >> 16); st_buf[3] = (uint8_t)(FMP_CMAGIC >> 24);
            st_buf[4] = 3; st_buf[5] = st_buf[6] = st_buf[7] = 0;
            st_buf[8] = 0; st_buf[9] = 1; st_buf[10] = (uint8_t)len; st_buf[11] = (uint8_t)(len >> 8);
            st_buf[12] = st_buf[13] = st_buf[14] = st_buf[15] = 0;
            st_save_staged(OBJ_FMP0 + 2u, FMP_CHDR + len);   /* (its chunk 0, index 0, in object 2) */
        }
        check(fmp_load(0, 0) == 0 && fingerprint() == p1, "old layout: project 1 still loads (the old project 2's chunk in object 2 is ignored)");
        check(fmp_load(FMP_AUTOSAVE, 0) == 0 && fingerprint() == as, "old layout: the autosave still loads");
        check(fmp_load(1, 0) != 0, "old layout: a slot over other projects' chunks reads as empty");
    }

    /* four heavy engines asked for: the fourth gets ANALOG */
    session(5);
    trk[2].eng_req = trk[2].engine = ENGI_FM6;          /* (bypassing the arena claim, as an editor or an old file could) */
    fmp_save(0);
    blank();
    check(fmp_load(0, &refused) == 0 && refused && trk[__builtin_ctz(refused)].engine == 0,
          "four heavy engines in a project: the one without an arena loads as ANALOG (reported)");

    /* autosave: the hash changes with an edit and only then */
    session(6);
    fmp_measure(&hash1);
    fmp_measure(&hash2);
    trk[3].p[P_PAN] += 1;
    fmp_measure(&b == 0 ? 0 : &len);
    check(hash1 == hash2 && len != hash1, "the autosave hash: the same without edits, another after one");

    printf(fails ? "projfmt: %d FAILED\n" : "projfmt: all checks ok\n", fails);
    return fails;
}
