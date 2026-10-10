/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 Move clips: each track keeps NCLIP patterns (its clips); the one it plays is its pattern (track_t step[],
 * micro[], roff[], fill[], lock[], with LENGTH and DIV). Everything that writes a pattern (SEQ + keys, REC, EDIT, the
 * groove, the editor) writes the clip that plays: the main loop keeps the bank in step with it (clip_tick).
 *
 *   bank     the clips as the project format writes a pattern (projfmt.c fmp_put_pattern, only the steps that hold
 *            anything) after 'K', LENGTH, DIV: one buffer in .pool, packed (a store moves the clips after it down);
 *            a 16-step pattern takes ~100..300 bytes, the bank CLIP_MEM. Full: the store is refused (clip_full, the
 *            panel says so) and the clip keeps its last stored version; the pattern playing is never lost (the
 *            project then saves it in its track, as version 1 did)
 *   launch   clip_launch / clip_scene ask for a clip; playing, it starts on the next bar (seq.c calls clip_bar from
 *            events_block, in the audio interrupt, before the steps of that block: its step 0 is on time); stopped,
 *            at once. A switch first stores what the outgoing clip got since the last store (nothing is lost), then
 *            decodes the new one into the track (bounded: one pattern) and releases the old notes
 *   project  version 2 of the FM1M stream carries the bank after the tracks (fmp_put_clips / fmp_get_clips); a
 *            version 1 project's patterns become clip 1 of their track (clip_from_tracks)
 *
 * Built on the host too (tests/projfmt_test.c, tests/fm1move_ui_test.c, the wasm simulator): included by projfmt.c. */

#define CLIP_MEM 8192u                           /* the bank: with the tracks a project stays within FMP_MAX */
#define CLIP_NONE 0xFFu

static uint8_t clip_mem[CLIP_MEM] __attribute__((section(".pool")));
static uint16_t clip_off[NTRK][NCLIP] __attribute__((section(".pool")));
static uint16_t clip_len[NTRK][NCLIP] __attribute__((section(".pool")));   /* 0: an empty clip */
static uint32_t clip_used;                       /* bytes of the bank in use */
static volatile uint8_t clip_cur[NTRK];          /* the clip the track's pattern is */
static volatile uint8_t clip_next[NTRK];         /* the clip asked for (the next bar), CLIP_NONE: none */
static uint32_t clip_hash[NTRK];                 /* the pattern's hash when its clip was last stored (0: unknown) */
static uint8_t clip_out[NTRK];                   /* the pattern playing did not fit the bank: the track keeps it */
static uint8_t clip_full;                        /* a store found no room (the UI says CLIP MEMORY FULL, clears it) */

/* the clip encoding of track t's pattern through w: 'K', LENGTH, DIV, the pattern */
static void clip_put(fmp_w_t *w, const track_t *t)
{
    fmp_put(w, 'K');
    fmp_put(w, (uint32_t)t->p[P_SLEN]);
    fmp_put(w, (uint32_t)t->p[P_SDIV]);
    fmp_put_pattern(w, t, 0);
}
/* its length and hash */
static uint32_t clip_measure(const track_t *t, uint32_t *hash)
{
    fmp_w_t w = {0, 0, 0, 0, 2166136261u};
    clip_put(&w, t);
    *hash = w.hash | 1u;                         /* (never 0: 0 is "unknown") */
    return w.pos;
}
/* does track t hold an empty pattern (nothing a clip would keep)? */
static int clip_pattern_empty(const track_t *t)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        if (fmp_step_used(t, i))
            return 0;
    for (i = 0; i < NLOCK; i++)
        if (t->lock[i].step != LOCK_FREE)
            return 0;
    return 1;
}

/* bytes within the bank, low to high: safe for the bank's moves, which always copy downwards or past the end
 * (libc.c has no memmove) */
static void clip_move(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    while (n--)
        *dst++ = *src++;
}

/* clip k of track ti out of the bank (the clips after it move down); the caller has the interrupts off */
static void clip_remove(uint32_t ti, uint32_t k)
{
    uint32_t off = clip_off[ti][k], len = clip_len[ti][k], i, j;
    if (!len)
        return;
    clip_move(clip_mem + off, clip_mem + off + len, clip_used - off - len);
    clip_used -= len;
    clip_len[ti][k] = 0;
    clip_off[ti][k] = 0;
    for (i = 0; i < NTRK; i++)
        for (j = 0; j < NCLIP; j++)
            if (clip_len[i][j] && clip_off[i][j] > off)
                clip_off[i][j] = (uint16_t)(clip_off[i][j] - len);
}

/* store track ti's pattern as its clip k (interrupts off: called from the main loop with them off, or from
 * clip_bar in the audio interrupt). 0 = stored (or nothing to store), -1 = no room: the bank keeps the old clip */
static int clip_store_irq(uint32_t ti, uint32_t k)
{
    const track_t *t = &trk[ti];
    uint32_t h, len = clip_measure(t, &h);
    if (h == clip_hash[ti] && clip_cur[ti] == k && !clip_out[ti])
        return 0;                                /* (unchanged since its last store) */
    if (clip_pattern_empty(t)) {                 /* an empty pattern: an empty clip */
        clip_remove(ti, k);
    } else {
        fmp_w_t w;
        if (clip_used - clip_len[ti][k] + len > CLIP_MEM || len > 0xFFFFu) {
            clip_full = 1;
            clip_out[ti] = 1;                    /* (the project keeps the pattern in its track) */
            return -1;
        }
        clip_remove(ti, k);
        w.pos = 0;
        w.lo = 0;
        w.hi = len;
        w.buf = clip_mem + clip_used;
        w.hash = 2166136261u;
        clip_put(&w, t);
        clip_off[ti][k] = (uint16_t)clip_used;
        clip_len[ti][k] = (uint16_t)len;
        clip_used += len;
    }
    clip_hash[ti] = h;
    clip_out[ti] = 0;
    return 0;
}

/* clip k of track ti into the track (its pattern, LENGTH, DIV); an empty clip: an empty pattern. Interrupts off */
static void clip_load_irq(uint32_t ti, uint32_t k)
{
    track_t *t = &trk[ti];
    if (clip_len[ti][k]) {
        fmp_r_t r;
        uint32_t len, div;
        fmp_open_ram(&r, clip_mem + clip_off[ti][k], clip_len[ti][k]);
        fmp_get(&r);                             /* 'K' (checked when it was stored or loaded) */
        len = fmp_get(&r);
        div = fmp_get(&r);
        fmp_get_pattern(&r, t, trk_kind(t));
        t->p[P_SLEN] = (int16_t)clamp((int32_t)len, 1, NSTEP);
        t->p[P_SDIV] = (int16_t)clamp((int32_t)div, TP[P_SDIV].min, TP[P_SDIV].max);
    } else {
        steps_clear(t);
        memset(t->micro, 0, sizeof t->micro);
    }
    t->seq_active = 1;
    clip_cur[ti] = (uint8_t)k;
    clip_hash[ti] = 0;                           /* (the main loop measures it again) */
    clip_out[ti] = 0;
}

/* the switch of track ti to clip k: what the outgoing clip got is stored first, its notes released */
static void clip_switch_irq(uint32_t ti, uint32_t k)
{
    track_t *t = &trk[ti];
    if (clip_cur[ti] < NCLIP)
        (void)clip_store_irq(ti, clip_cur[ti]);
    trk_all_off(t);
    t->seq_hold = 0;
    clip_load_irq(ti, k);
    if (song.playing) {                          /* on the bar: its step 0 now, on its own grid */
        uint32_t into, slen;
        t->seq_org = trk_grid(t, &into, &slen);
        t->seq_abs = SEQ_NONE;
        t->seq_den = (uint8_t)trk_div(t);
    } else {
        t->seq_org = 0;
    }
}

/* seq.c, in the audio interrupt at the start of a bar (before that block's steps): the clips asked for start */
static void clip_bar(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        uint32_t k = clip_next[i];
        if (k == CLIP_NONE)
            continue;
        clip_next[i] = CLIP_NONE;
        if (k < NCLIP)
            clip_switch_irq(i, k);
    }
}

/* ------------------------------------------------------------------ the UI's calls (main loop) --- */
/* ask for clip k on track ti: playing, it starts on the next bar; stopped, now */
static void clip_launch(uint32_t ti, uint32_t k)
{
    if (ti >= NTRK || k >= NCLIP)
        return;
    if (song.playing || transport_req) {
        clip_next[ti] = (uint8_t)k;
        return;
    }
    fm1_irq_off();
    clip_switch_irq(ti, k);
    clip_next[ti] = CLIP_NONE;
    fm1_irq_on();
}
/* scene k: clip k on every track (an empty clip there: that track goes quiet), on the next bar */
static void clip_scene(uint32_t k)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++)
        clip_launch(i, k);
}
/* copy track ti's clip `from` into slot `to` (the clip playing is stored first); 0 = done, -1 = no room */
static int clip_copy(uint32_t ti, uint32_t from, uint32_t to)
{
    uint32_t len;
    int rc = 0;
    if (ti >= NTRK || from >= NCLIP || to >= NCLIP || from == to)
        return 0;
    fm1_irq_off();
    if (from == clip_cur[ti])
        (void)clip_store_irq(ti, from);
    len = clip_len[ti][from];
    if (to == clip_cur[ti]) {                    /* onto the clip playing: it becomes the copy */
        clip_load_irq(ti, from);
        clip_cur[ti] = (uint8_t)to;
        rc = clip_store_irq(ti, to);
    } else if (!len) {
        clip_remove(ti, to);
    } else if (clip_used - clip_len[ti][to] + len > CLIP_MEM) {
        clip_full = 1;
        rc = -1;
    } else {
        clip_remove(ti, to);                     /* (may move `from` down: its offset is read after) */
        clip_move(clip_mem + clip_used, clip_mem + clip_off[ti][from], len);
        clip_off[ti][to] = (uint16_t)clip_used;
        clip_len[ti][to] = (uint16_t)len;
        clip_used += len;
    }
    fm1_irq_on();
    return rc;
}
/* empty track ti's clip k (the clip playing: its pattern too) */
static void clip_erase(uint32_t ti, uint32_t k)
{
    if (ti >= NTRK || k >= NCLIP)
        return;
    fm1_irq_off();
    clip_remove(ti, k);
    if (k == clip_cur[ti]) {
        trk_all_off(&trk[ti]);
        steps_clear(&trk[ti]);
        memset(trk[ti].micro, 0, sizeof trk[ti].micro);
        clip_hash[ti] = 0;
        clip_out[ti] = 0;
    }
    if (clip_next[ti] == k)
        clip_next[ti] = CLIP_NONE;
    fm1_irq_on();
}
/* every clip of track ti empty, clip 1 playing (a new kind: synth steps and drum lanes do not convert) */
static void clip_reset_track(uint32_t ti)
{
    uint32_t k;
    if (ti >= NTRK)
        return;
    fm1_irq_off();
    for (k = 0; k < NCLIP; k++)
        clip_remove(ti, k);
    clip_cur[ti] = 0;
    clip_next[ti] = CLIP_NONE;
    clip_hash[ti] = 0;
    clip_out[ti] = 0;
    fm1_irq_on();
}
/* an empty bank, clip 1 playing on every track (power-on, a new project) */
static void clip_init(void)
{
    uint32_t i;
    memset(clip_len, 0, sizeof clip_len);
    memset(clip_off, 0, sizeof clip_off);
    clip_used = 0;
    for (i = 0; i < NTRK; i++) {
        clip_cur[i] = 0;
        clip_next[i] = CLIP_NONE;
        clip_hash[i] = 0;
        clip_out[i] = 0;
    }
    clip_full = 0;
    clip_bar_fn = clip_bar;                      /* (seq.c calls it on every bar) */
}
/* the main loop: the clips playing follow their patterns (only a changed one is stored) */
static void clip_sync_all(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {
        uint32_t h;
        if (clip_cur[i] >= NCLIP)
            continue;
        clip_measure(&trk[i], &h);               /* (cheap; the store re-measures with the interrupts off) */
        if (h == clip_hash[i] && !clip_out[i])
            continue;
        fm1_irq_off();
        (void)clip_store_irq(i, clip_cur[i]);
        fm1_irq_on();
    }
}
/* the main loop, every frame: stopped, a clip still asked for starts now (STOP came before its bar); then the
 * clips playing follow their patterns */
static void clip_tick(void)
{
    uint32_t i;
    if (!song.playing && !transport_req)
        for (i = 0; i < NTRK; i++)
            if (clip_next[i] != CLIP_NONE)
                clip_launch(i, clip_next[i]);
    clip_sync_all();
}
/* for the UI: 0 empty, 1 holds a pattern, 2 playing, 3 asked for (the next bar) */
static uint32_t clip_state(uint32_t ti, uint32_t k)
{
    if (ti >= NTRK || k >= NCLIP)
        return 0;
    if (clip_next[ti] == k)
        return 3;
    if (clip_cur[ti] == k)
        return 2;
    return clip_len[ti][k] ? 1u : 0u;
}

/* ------------------------------------------------------------------ the project stream --- */
/* the track's pattern is in the bank (its record in the stream then leaves it out) */
static int clip_in_bank(uint32_t ti) { return ti < NTRK && clip_cur[ti] < NCLIP && !clip_out[ti]; }

/* 'C', NCLIP, then per track: the clip playing, and per clip its length (16 bits) and bytes */
static void fmp_put_clips(fmp_w_t *w)
{
    uint32_t i, k;
    fmp_put(w, 'C');
    fmp_put(w, NCLIP);
    for (i = 0; i < NTRK; i++) {
        fmp_put(w, clip_cur[i] < NCLIP ? clip_cur[i] : 0u);
        for (k = 0; k < NCLIP; k++) {
            fmp_put16(w, clip_len[i][k]);
            fmp_putn(w, clip_mem + clip_off[i][k], clip_len[i][k]);
        }
    }
}

/* the clip section: pass 1 (apply 0) checks every clip as a pattern of its track's kind and that they fit the bank;
 * pass 2 copies them into the bank and loads each track's clip (a track whose own pattern came in its record and
 * whose clip is empty keeps that pattern: it becomes the clip) */
static int fmp_get_clips(fmp_r_t *r, int apply, const uint8_t *kinds, uint32_t ntr)
{
    uint32_t nc, i, k, total = 0;
    if (fmp_get(r) != 'C')
        return -1;
    nc = fmp_get(r);
    if (r->err || nc > 16u)
        return -1;
    if (apply)
        clip_init();
    for (i = 0; i < ntr; i++) {
        uint32_t cur = fmp_get(r);
        for (k = 0; k < nc; k++) {
            uint32_t len = fmp_get16(r), j, start = r->pos;
            if (r->err)
                return -1;
            if (!len)
                continue;
            if (!apply) {                        /* a pattern of this kind, exactly len bytes long */
                if (fmp_get(r) != 'K')
                    return -1;
                fmp_get(r);
                fmp_get(r);
                if (fmp_get_pattern(r, 0, kinds[i % NTRK]) || r->pos != start + len)
                    return -1;
                total += len;
                if (total > CLIP_MEM)
                    return -1;
                continue;
            }
            if (i < NTRK && k < NCLIP) {
                clip_off[i][k] = (uint16_t)clip_used;
                clip_len[i][k] = (uint16_t)len;
                for (j = 0; j < len; j++)
                    clip_mem[clip_used + j] = (uint8_t)fmp_get(r);
                clip_used += len;
            } else {
                for (j = 0; j < len; j++)
                    fmp_get(r);
            }
        }
        if (apply && i < NTRK) {
            clip_cur[i] = (uint8_t)(cur < NCLIP ? cur : 0u);
            if (clip_pattern_empty(&trk[i]))
                clip_load_irq(i, clip_cur[i]);   /* (the caller has the interrupts off in pass 2) */
            else
                clip_hash[i] = 0;                /* (its record carried the pattern: the bank had no room for it then;
                                                  * it is what played, so it wins over the clip's older version) */
        }
    }
    return r->err ? -1 : 0;
}

/* a version 1 project (no clips): each track's pattern becomes its clip 1 */
static void clip_from_tracks(void)
{
    uint32_t i;
    clip_init();
    for (i = 0; i < NTRK; i++)
        (void)clip_store_irq(i, 0);              /* (pass 2: the interrupts are off) */
}
