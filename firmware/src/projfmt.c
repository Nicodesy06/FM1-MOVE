/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 Move project format "FM1M" (docs/PROJECT_FORMAT.md): the whole project as one sparse byte stream, written
 * and read straight through the storage staging buffer (st_buf), split over at most FMP_NCHUNK flash objects.
 *
 *   stream   header (magic, version, tracks, globals, the song groove), then per track: kind, engine, preset,
 *            parameters (common and engine, counted: newer and older firmware read each other's), groove, pad
 *            offsets, a bitmap of the steps that hold anything and only those steps (10 bytes + nudge + raw timing
 *            + condition), the locks in use; an end mark
 *   chunks   each flash object = chunk header (magic, generation, index, count, stream length) + up to FMP_DATA
 *            bytes; the storage layer adds its CRC-32 and A/B copies. A save writes the chunks after the first, then
 *            chunk 0 (the commit): a torn save leaves the previous generation complete
 *   memory   no project-sized buffer: the writer runs the stream once per chunk with a window onto st_buf (and once
 *            with no window to size and hash it); the reader validates the whole stream first (pass 1, nothing
 *            written), then applies it (pass 2) with the interrupts off
 *
 * Built on the host too (tests/projfmt_test.c). Needs core.h, params.c, engines.c, drums.c, storage.c and seq.c's
 * steps_clear / trk_set_kind / locks; the caller provides fmp_obj(slot, chunk) (where the chunks live). */

#define FMP_MAGIC 0x4D314D46u                  /* "FM1M" */
#define FMP_VERSION 1u
#define FMP_CMAGIC 0x4B504D46u                 /* "FMPK": a chunk */
#define FMP_CHDR 16u                           /* chunk header bytes */
#define FMP_DATA (ST_PAYLOAD_MAX - FMP_CHDR)   /* stream bytes per chunk */
#define FMP_NCHUNK 3u
#define FMP_MAX (FMP_DATA * FMP_NCHUNK)        /* the largest stream (11 472 B: eight tracks and their clips) */
static uint32_t fmp_obj(uint32_t slot, uint32_t chunk);   /* the storage object of a slot's chunk (caller) */

/* ---------------------------------------------------------------- writer --- */
typedef struct {
    uint32_t pos;                              /* stream bytes so far */
    uint32_t lo, hi;                           /* the window copied into buf: stream bytes lo .. hi - 1 */
    uint8_t *buf;
    uint32_t hash;                             /* FNV-1a of the whole stream (autosave: changed?) */
} fmp_w_t;
static void fmp_put(fmp_w_t *w, uint32_t b)
{
    if (w->pos >= w->lo && w->pos < w->hi)
        w->buf[w->pos - w->lo] = (uint8_t)b;
    w->hash = (w->hash ^ (b & 0xFFu)) * 16777619u;
    w->pos++;
}
static void fmp_put16(fmp_w_t *w, uint32_t v)
{
    fmp_put(w, v & 0xFFu);
    fmp_put(w, (v >> 8) & 0xFFu);
}
static void fmp_put32(fmp_w_t *w, uint32_t v)
{
    fmp_put16(w, v & 0xFFFFu);
    fmp_put16(w, v >> 16);
}
static void fmp_putn(fmp_w_t *w, const void *p, uint32_t n)
{
    const uint8_t *b = p;
    while (n--)
        fmp_put(w, *b++);
}

/* a step holds something worth keeping (notes, a tie, lanes, a nudge, a raw deviation, a condition) */
static int fmp_step_used(const track_t *t, uint32_t i)
{
    static const uint8_t ZERO[10];
    step_t rest;
    if (t->micro[i] || t->roff[i] || ((t->fill[i / 4u] >> (2u * (i % 4u))) & 3u))
        return 1;
    if (is_drum(t))
        return memcmp(&t->dstep[i], ZERO, sizeof ZERO) != 0;
    memset(&rest, 0, sizeof rest);
    rest.time = ST_REST;
    return memcmp(&t->step[i], &rest, sizeof rest) != 0;
}

static void fmp_put_groove(fmp_w_t *w, const groove_t *g)
{
    fmp_put(w, g->qloose);
    fmp_put(w, g->tmpl);
    fmp_put(w, g->amt);
    fmp_put(w, g->vamt);
    fmp_put(w, g->hum_t);
    fmp_put(w, g->hum_v);
    fmp_put(w, g->own);
    fmp_put16(w, g->seed);
    fmp_putn(w, g->user_off, 16);
    fmp_putn(w, g->user_vel, 16);
}

/* a parameter's default on track t (what a stream leaves out): the engine's for P_E0..P_E7, else TP's */
static int16_t fmp_pdef(uint32_t kind, uint32_t e, uint32_t id);
/* parameters first..first+n-1 as a presence bitmap (those that differ from their default) and their values */
static void fmp_put_params(fmp_w_t *w, const track_t *t, uint32_t first, uint32_t n)
{
    uint32_t i, b;
    fmp_put(w, n);
    for (b = 0; b < n; b += 8u) {
        uint32_t m = 0;
        for (i = b; i < b + 8u && i < n; i++)
            if (t->p[first + i] != fmp_pdef(trk_kind(t), t->eng_req, first + i))
                m |= 1u << (i - b);
        fmp_put(w, m);
    }
    for (i = 0; i < n; i++)
        if (t->p[first + i] != fmp_pdef(trk_kind(t), t->eng_req, first + i))
            fmp_put16(w, (uint16_t)t->p[first + i]);
}

static void fmp_put_track(fmp_w_t *w, const track_t *t)
{
    uint32_t i, nl = 0;
    uint8_t bits[NSTEP / 8] = {0};
    fmp_put(w, 'T');
    fmp_put(w, trk_kind(t));
    fmp_put(w, t->eng_req);
    fmp_put(w, t->preset);
    fmp_put_params(w, t, 0, P_E0);                    /* the common parameters, then the engine's eight */
    fmp_put_params(w, t, P_E0, 8);
    fmp_put_groove(w, &t->gv);
    fmp_putn(w, t->pad_off, DRUM_LANES);
    for (i = 0; i < NSTEP; i++)
        if (fmp_step_used(t, i))
            bits[i / 8u] |= (uint8_t)(1u << (i % 8u));
    fmp_putn(w, bits, sizeof bits);
    for (i = 0; i < NSTEP; i++)
        if ((bits[i / 8u] >> (i % 8u)) & 1u) {
            uint32_t k;
            if (is_drum(t)) {                         /* lanes on, then a byte per lane on: level | ratchet << 2 */
                const dstep_t *d = &t->dstep[i];
                uint32_t m = dstep_mask(d);
                fmp_put16(w, m);
                for (k = 0; k < DRUM_LANES; k++)
                    if ((m >> k) & 1u)
                        fmp_put(w, dstep_lvl(d, k) | dstep_rat(d, k) << 2);
            } else {                                  /* n | time << 3, flags, vel, lvl, rat, then the n notes */
                const step_t *st = &t->step[i];
                uint32_t n = st->n > 4u ? 4u : st->n;
                fmp_put(w, n | (uint32_t)(st->time & 3u) << 3);
                fmp_put(w, st->flags);
                fmp_put(w, st->vel);
                fmp_put(w, st->lvl);
                fmp_put(w, st->rat);
                for (k = 0; k < n; k++)
                    fmp_put(w, st->note[k]);
            }
            fmp_put(w, ((uint32_t)t->micro[i] & 63u) | ((t->fill[i / 4u] >> (2u * (i % 4u))) & 3u) << 6);
            fmp_put(w, (uint32_t)t->roff[i] & 63u);
        }
    for (i = 0; i < NLOCK; i++)
        nl += t->lock[i].step != LOCK_FREE;
    fmp_put(w, nl);
    for (i = 0; i < NLOCK; i++)
        if (t->lock[i].step != LOCK_FREE) {
            fmp_put(w, t->lock[i].step);
            fmp_put(w, t->lock[i].param);
            fmp_put16(w, (uint16_t)t->lock[i].val);
        }
}

/* the project as it is now, as a stream (through the window of w) */
static void fmp_stream(fmp_w_t *w)
{
    uint32_t i;
    fmp_put32(w, FMP_MAGIC);
    fmp_put(w, FMP_VERSION);
    fmp_put(w, NTRK);
    fmp_put(w, song.sel);
    fmp_put(w, G_COUNT);
    for (i = 0; i < G_COUNT; i++)
        fmp_put16(w, (uint16_t)song.g[i]);
    fmp_put_groove(w, &song_groove);
    for (i = 0; i < NTRK; i++)
        fmp_put_track(w, &trk[i]);
    fmp_put(w, 'E');
}

/* the stream's length and hash, nothing written (autosave: did the project change?) */
static uint32_t fmp_measure(uint32_t *hash)
{
    fmp_w_t w = {0, 0, 0, 0, 2166136261u};
    fmp_stream(&w);
    if (hash)
        *hash = w.hash;
    return w.pos;
}

/* ---------------------------------------------------------------- reader --- */
typedef struct {
    uint32_t slot, gen, n, len;                /* the slot, its generation, chunks, stream length */
    uint32_t pos;                              /* stream bytes read */
    uint32_t chunk;                            /* the chunk in st_buf, 0xFF = none */
    int err;
    const uint8_t *ram0;                       /* chunk 0's data in RAM (a restore before its commit, a clip): the later
                                                * chunks come from the flash (slot, gen) */
} fmp_r_t;

/* the chunk header fields of what st_buf holds */
static uint32_t fmp_le32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
#define FMP_ANY 0xFFFFFFFFu
/* a copy of chunk idx of generation gen (FMP_ANY: the newest copy that is a valid chunk idx) into st_buf; 0 = found.
 * Both copies are tried, the newer (higher storage seq) first */
static int fmp_chunk_load(uint32_t obj, uint32_t gen, uint32_t idx)
{
    st_hdr_t h[2];
    int v[2], k;
    v[0] = !st_head(obj, 0, &h[0]);
    v[1] = !st_head(obj, 1, &h[1]);
    for (k = 0; k < 2; k++) {
        uint32_t c = (v[1] && (!v[0] || h[1].seq > h[0].seq)) ? (uint32_t)(k ^ 1) : (uint32_t)k;
        if (!v[c] || h[c].len < FMP_CHDR || h[c].len - FMP_CHDR > FMP_DATA || st_body(obj, c, &h[c]))
            continue;
        if (fmp_le32(st_buf) != FMP_CMAGIC || st_buf[8] != idx || (gen != FMP_ANY && fmp_le32(st_buf + 4) != gen))
            continue;
        return 0;
    }
    return -1;
}

static uint32_t fmp_get(fmp_r_t *r)
{
    uint32_t k, b;
    if (r->err)
        return 0;
    if (r->pos >= r->len) {
        r->err = 1;                                   /* past the end: a malformed stream */
        return 0;
    }
    k = r->pos / FMP_DATA;
    if (k == 0u && r->ram0)                           /* (chunk 0 in RAM) */
        return r->ram0[r->pos++];
    if (k != r->chunk) {
        if (k >= r->n || fmp_chunk_load(fmp_obj(r->slot, k), r->gen, k)) {
            r->err = 2;                               /* a chunk of this generation is missing or damaged */
            return 0;
        }
        r->chunk = k;
    }
    b = st_buf[FMP_CHDR + r->pos % FMP_DATA];
    r->pos++;
    return b;
}
static uint32_t fmp_get16(fmp_r_t *r)
{
    uint32_t a = fmp_get(r);
    return a | fmp_get(r) << 8;
}
static uint32_t fmp_get32(fmp_r_t *r)
{
    uint32_t a = fmp_get16(r);
    return a | fmp_get16(r) << 16;
}

/* open a slot: chunk 0's newest copy gives the generation, the chunk count, the length */
static int fmp_open(fmp_r_t *r, uint32_t slot)
{
    memset(r, 0, sizeof *r);
    r->slot = slot;
    if (fmp_chunk_load(fmp_obj(slot, 0), FMP_ANY, 0))
        return -1;
    r->gen = fmp_le32(st_buf + 4);
    r->n = st_buf[9];
    r->len = (uint32_t)st_buf[10] | (uint32_t)st_buf[11] << 8;
    r->chunk = 0;
    if (!r->n || r->n > FMP_NCHUNK || !r->len || r->len > r->n * FMP_DATA)
        return -1;
    return 0;
}

/* a stream of len bytes (at most one chunk) held in RAM: a clip */
static int fmp_open_ram(fmp_r_t *r, const uint8_t *buf, uint32_t len)
{
    memset(r, 0, sizeof *r);
    r->ram0 = buf;
    r->len = len;
    r->n = 1;
    r->chunk = 0xFFu;
    return !buf || !len || len > FMP_DATA ? -1 : 0;
}
/* a restore being staged into slot: chunk 0's data in RAM (c0), chunks 1.. already written as generation gen
 * (fmp_put_chunk as they came); parsed whole before chunk 0, the commit, is written (fmp_restore_commit) */
static int fmp_open_staged(fmp_r_t *r, uint32_t slot, uint32_t gen, const uint8_t *c0, uint32_t len)
{
    memset(r, 0, sizeof *r);
    r->slot = slot;
    r->gen = gen;
    r->ram0 = c0;
    r->len = len;
    r->n = (len + FMP_DATA - 1u) / FMP_DATA;
    r->chunk = 0xFFu;
    return !c0 || !len || len > FMP_MAX ? -1 : 0;
}

static void fmp_get_groove(fmp_r_t *r, groove_t *g)
{
    uint32_t i;
    g->qloose = (uint8_t)fmp_get(r);
    g->tmpl = (uint8_t)fmp_get(r);
    g->amt = (uint8_t)fmp_get(r);
    g->vamt = (uint8_t)fmp_get(r);
    g->hum_t = (uint8_t)fmp_get(r);
    g->hum_v = (uint8_t)fmp_get(r);
    g->own = (uint8_t)fmp_get(r);
    g->seed = (uint16_t)fmp_get16(r);
    for (i = 0; i < 16u; i++)
        g->user_off[i] = (int8_t)fmp_get(r);
    for (i = 0; i < 16u; i++)
        g->user_vel[i] = (int8_t)fmp_get(r);
    groove_sane(g);
}

static int is_drum_kind(uint32_t kind) { return kind == TK_DRUM; }
static int16_t fmp_pdef(uint32_t kind, uint32_t e, uint32_t id)
{
    if (id >= P_E0 && id <= P_E7)
        return kind == TK_DRUM && id == P_E0 ? DRUM_KIT_DESC.def : ENGINES[e % NENGINES]->edit[id - P_E0].def;
    return TP[id].def;
}
/* a parameter's descriptor on track t playing engine e (the drum kit on a drum track) */
static const param_desc_t *fmp_desc(const track_t *t, uint32_t e, uint32_t id)
{
    if (is_drum(t) && id == P_E0)
        return &DRUM_KIT_DESC;
    return id >= P_E0 && id <= P_E7 ? &ENGINES[e % NENGINES]->edit[id - P_E0] : &TP[id];
}

/* one track from the stream into t (apply), or only checked (t == 0); returns 0 when it is well formed */
static int fmp_get_track(fmp_r_t *r, track_t *t, uint32_t ti, uint32_t *heavy_refused)
{
    uint32_t kind, e, pre, nc, ne, i, nl;
    int16_t p[P_COUNT];
    groove_t g;
    int8_t pad[DRUM_LANES];
    uint8_t bits[NSTEP / 8];
    if (fmp_get(r) != 'T')
        return -1;
    kind = fmp_get(r);
    e = fmp_get(r);
    pre = fmp_get(r);
    if (kind >= TK_COUNT || e >= NENGINES)
        return -1;
    for (i = 0; i < P_COUNT; i++)
        p[i] = fmp_pdef(kind, e, i);                  /* (left out: the default; an older stream: added since) */
    for (ne = 0; ne < 2u; ne++) {                     /* the common parameters, then the engine's */
        uint32_t first = ne ? P_E0 : 0u, lim = ne ? 8u : P_E0, b, m[8] = {0};
        nc = fmp_get(r);
        if (nc > 64u)
            return -1;
        for (b = 0; b < (nc + 7u) / 8u; b++)
            m[b] = fmp_get(r);
        for (i = 0; i < nc; i++)
            if ((m[i / 8u] >> (i % 8u)) & 1u) {
                uint32_t v = fmp_get16(r);
                if (i < lim)
                    p[first + i] = (int16_t)v;
            }
    }
    fmp_get_groove(r, &g);
    for (i = 0; i < DRUM_LANES; i++)
        pad[i] = (int8_t)fmp_get(r);
    for (i = 0; i < sizeof bits; i++)
        bits[i] = (uint8_t)fmp_get(r);
    if (r->err)
        return -1;
    if (t) {                                          /* apply: silence it, its kind, then the rest */
        uint32_t k;
        trk_silence(t);
        t->kind = (uint8_t)(kind + 1u);
        if (kind == TK_SYNTH && eng_heavy(e) && !eng_arena_claim(t)) {
            e = 0;                                    /* every heavy-engine arena is taken: a light engine */
            for (k = 0; k < 8u; k++)
                p[P_E0 + k] = ENGINES[0]->edit[k].def;
            if (heavy_refused)
                *heavy_refused |= 1u << ti;
        }
        t->eng_req = t->engine = (uint8_t)(kind == TK_SYNTH ? e : 0u);
        t->preset = (uint8_t)(ENGINES[t->engine]->npresets ? pre % ENGINES[t->engine]->npresets : 0u);
        t->user = 0;
        t->lk_n = 0;
        for (i = 0; i < P_COUNT; i++) {
            const param_desc_t *d = fmp_desc(t, t->engine, i);
            t->p[i] = (int16_t)clamp(p[i], d->min, d->max);
        }
        t->gv = g;
        for (i = 0; i < DRUM_LANES; i++)
            t->pad_off[i] = (int8_t)clamp(pad[i], MICRO_MIN, MICRO_MAX);
        steps_clear(t);
        memset(t->micro, 0, sizeof t->micro);
    }
    for (i = 0; i < NSTEP; i++)
        if ((bits[i / 8u] >> (i % 8u)) & 1u) {
            uint32_t k, b, fc;
            int32_t mi, ro;
            if (is_drum_kind(kind)) {
                uint32_t m = fmp_get16(r);
                dstep_t d;
                memset(&d, 0, sizeof d);
                for (k = 0; k < DRUM_LANES; k++)
                    if ((m >> k) & 1u) {
                        b = fmp_get(r);
                        dstep_set(&d, k, b & 3u, (b >> 2) & 3u);
                    }
                if (t)
                    t->dstep[i] = d;
            } else {
                step_t st;
                b = fmp_get(r);
                memset(&st, 0, sizeof st);
                st.n = (uint8_t)(b & 7u);
                st.time = (uint8_t)((b >> 3) & 3u);
                st.flags = (uint8_t)fmp_get(r);
                st.vel = (uint8_t)fmp_get(r);
                st.lvl = (uint8_t)fmp_get(r);
                st.rat = (uint8_t)fmp_get(r);
                if (st.n > 4u)
                    return -1;
                for (k = 0; k < st.n; k++)
                    st.note[k] = (uint8_t)(fmp_get(r) & 127u);
                if (st.time > ST_REST)
                    st.time = ST_REST;
                if (t)
                    t->step[i] = st;
            }
            b = fmp_get(r);
            mi = (int32_t)(b & 63u) - ((b & 32u) ? 64 : 0);   /* 6-bit two's complement */
            fc = (b >> 6) & 3u;
            b = fmp_get(r);
            ro = (int32_t)(b & 63u) - ((b & 32u) ? 64 : 0);
            if (!t)
                continue;
            t->micro[i] = (int8_t)clamp(mi, MICRO_MIN, MICRO_MAX);
            t->roff[i] = (int8_t)clamp(ro, MICRO_MIN, MICRO_MAX);
            if (fc != 3u)
                t->fill[i / 4u] = (uint8_t)((t->fill[i / 4u] & ~(3u << (2u * (i % 4u)))) | fc << (2u * (i % 4u)));
        }
    nl = fmp_get(r);
    if (nl > NLOCK)
        return -1;
    for (i = 0; i < nl; i++) {
        uint32_t st = fmp_get(r), id = fmp_get(r);
        int32_t v = (int16_t)fmp_get16(r);
        if (t && st < NSTEP && id < P_COUNT && p_lockable(id)) {
            const param_desc_t *d = fmp_desc(t, t->engine, id);
            t->lock[i].step = (uint8_t)st;
            t->lock[i].param = (uint8_t)id;
            t->lock[i].val = (int16_t)clamp(v, d->min, d->max);
        }
    }
    if (t)
        t->seq_active = 1;
    return r->err ? -1 : 0;
}

/* read slot: pass 1 checks every byte (nothing changes), pass 2 applies. Returns 0, or -1 (empty / damaged: the
 * working project is untouched). *refused: bit per track whose heavy engine found no arena (it got ANALOG). The
 * caller stops the transport and calls this with the interrupts off for pass 2 (fmp_load does both). */
static int fmp_parse_r(fmp_r_t *rp, int apply, uint32_t *refused)
{
#define r (*rp)
    uint32_t ntr, ng, i, sel;
    if (fmp_get32(&r) != FMP_MAGIC || fmp_get(&r) != FMP_VERSION)
        return -1;
    ntr = fmp_get(&r);
    sel = fmp_get(&r);
    ng = fmp_get(&r);
    if (r.err || !ntr || ntr > 16u)
        return -1;
    for (i = 0; i < ng; i++) {
        uint32_t v = fmp_get16(&r);
        if (apply && i < G_COUNT && i != G_SLOT && i != G_LOAD && i != G_SAVE && i != G_SYNC && i != G_MIDI &&
            i != G_ROUTE)
            song.g[i] = (int16_t)clamp((int16_t)v, GP[i].min, GP[i].max);
    }
    {
        groove_t g;
        fmp_get_groove(&r, &g);
        if (apply)
            song_groove = g;
    }
    if (apply) {                                      /* every arena back: the project claims what it needs */
        for (i = 0; i < N_ARENA; i++)
            eng_arena_trk[i] = 0;
        if (refused)
            *refused = 0;
    }
    for (i = 0; i < ntr; i++) {
        track_t *t = apply && i < NTRK ? &trk[i] : 0;
        if (fmp_get_track(&r, t, i, refused))
            return -1;
    }
    for (i = ntr; apply && i < NTRK; i++) {           /* a project of fewer tracks: the others empty synths */
        trk_set_kind(&trk[i], TK_SYNTH);
        steps_clear(&trk[i]);
    }
    if (fmp_get(&r) != 'E' || r.err || r.pos != r.len)
        return -1;
    if (apply)
        song.sel = (uint8_t)(sel < NTRK ? sel : 0u);
    return 0;
#undef r
}
static int fmp_parse(uint32_t slot, int apply, uint32_t *refused)
{
    fmp_r_t r;
    return fmp_open(&r, slot) ? -1 : fmp_parse_r(&r, apply, refused);
}

static int fmp_load(uint32_t slot, uint32_t *refused)
{
    if (fmp_parse(slot, 0, 0))
        return -1;                                    /* (pass 1 failed: nothing changed) */
    fm1_irq_off();
    fmp_parse(slot, 1, refused);
    fm1_irq_on();
    return 0;
}

/* ---------------------------------------------------------------- saving --- */
/* the next generation of a slot: one more than any chunk copy holds, so every save attempt has its own (two torn
 * saves in a row can never leave two different streams under one generation) */
static uint32_t fmp_next_gen(uint32_t slot)
{
    uint32_t k, c, g = 0;
    for (k = 0; k < FMP_NCHUNK; k++)
        for (c = 0; c < 2u; c++) {
            st_hdr_t h;
            uint32_t obj = fmp_obj(slot, k);
            if (st_head(obj, c, &h) || h.len < FMP_CHDR || st_body(obj, c, &h) || fmp_le32(st_buf) != FMP_CMAGIC)
                continue;
            if (fmp_le32(st_buf + 4) > g)
                g = fmp_le32(st_buf + 4);
        }
    return g + 1u;
}

/* chunk k of n (generation gen, stream length len) whose data (nb bytes) is already at st_buf + FMP_CHDR: its header,
 * then the storage write */
static int fmp_put_chunk(uint32_t slot, uint32_t k, uint32_t n, uint32_t gen, uint32_t len, uint32_t nb)
{
    st_buf[0] = (uint8_t)FMP_CMAGIC;
    st_buf[1] = (uint8_t)(FMP_CMAGIC >> 8);
    st_buf[2] = (uint8_t)(FMP_CMAGIC >> 16);
    st_buf[3] = (uint8_t)(FMP_CMAGIC >> 24);
    st_buf[4] = (uint8_t)gen;
    st_buf[5] = (uint8_t)(gen >> 8);
    st_buf[6] = (uint8_t)(gen >> 16);
    st_buf[7] = (uint8_t)(gen >> 24);
    st_buf[8] = (uint8_t)k;
    st_buf[9] = (uint8_t)n;
    st_buf[10] = (uint8_t)len;
    st_buf[11] = (uint8_t)(len >> 8);
    st_buf[12] = st_buf[13] = st_buf[14] = st_buf[15] = 0;
    return st_save_staged(fmp_obj(slot, k), FMP_CHDR + nb);
}

/* write the project now into slot: the later chunks, then chunk 0. 0 = saved; -9 = too big; else storage's code */
static int fmp_save(uint32_t slot)
{
    uint32_t len = fmp_measure(0), n = (len + FMP_DATA - 1u) / FMP_DATA, gen = fmp_next_gen(slot), k;
    if (len > FMP_MAX)
        return -9;
    for (k = n; k-- > 0;) {
        fmp_w_t w;
        uint32_t lo = k * FMP_DATA, hi = lo + FMP_DATA < len ? lo + FMP_DATA : len;
        int rc;
        w.pos = 0;
        w.lo = lo;
        w.hi = hi;
        w.buf = st_buf + FMP_CHDR;
        w.hash = 2166136261u;
        fmp_stream(&w);
        if ((rc = fmp_put_chunk(slot, k, n, gen, len, hi - lo)) != 0)
            return rc;
    }
    return 0;
}

/* a staged restore's commit (editor.c): chunk 0 (its data at c0), written last as fmp_save writes it; the caller wrote
 * chunks 1.. (fmp_put_chunk) and parsed the whole stream (fmp_open_staged + fmp_parse_r) first */
static int fmp_restore_commit(uint32_t slot, uint32_t gen, const uint8_t *c0, uint32_t len)
{
    uint32_t n = (len + FMP_DATA - 1u) / FMP_DATA;
    if (!len || len > FMP_MAX)
        return -9;
    memcpy(st_buf + FMP_CHDR, c0, n > 1u ? FMP_DATA : len);
    return fmp_put_chunk(slot, 0, n, gen, len, n > 1u ? FMP_DATA : len);
}

/* the stream bytes off .. off + cnt - 1 of what slot holds, into dst (backup); 0 = done */
static int fmp_read(uint32_t slot, uint32_t off, uint8_t *dst, uint32_t cnt)
{
    fmp_r_t r;
    uint32_t i;
    if (fmp_open(&r, slot) || off > r.len || cnt > r.len - off)
        return -1;
    r.pos = off;
    r.chunk = 0xFFu;                                  /* (st_buf holds chunk 0: re-read whichever is needed) */
    for (i = 0; i < cnt; i++)
        dst[i] = (uint8_t)fmp_get(&r);
    return r.err ? -1 : 0;
}

/* the length and CRC-32 of what slot holds (0 length: empty or damaged) */
static uint32_t fmp_stored(uint32_t slot, uint32_t *crc)
{
    fmp_r_t r;
    uint32_t k, c = 0;
    *crc = 0;
    if (fmp_open(&r, slot))
        return 0;
    for (k = 0; k < r.n; k++) {
        uint32_t nb = k + 1u < r.n ? FMP_DATA : r.len - k * FMP_DATA;
        if (fmp_chunk_load(fmp_obj(slot, k), r.gen, k))
            return 0;
        c = st_crc32_more(c, st_buf + FMP_CHDR, nb);
    }
    *crc = c;
    return r.len;
}

/* the working project's stream bytes off .. off + cnt - 1 into dst (a window, nothing else written); returns the
 * stream's hash, so a reader can tell the project changed between two calls */
static uint32_t fmp_window(uint32_t off, uint8_t *dst, uint32_t cnt)
{
    fmp_w_t w = {0, off, off + cnt, dst, 2166136261u};
    fmp_stream(&w);
    return w.hash;
}
