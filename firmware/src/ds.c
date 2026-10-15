/* SPDX-License-Identifier: GPL-3.0-only */
/* FM1 Move design system: colour tokens, Inter type scale, anti-aliased primitives, geometric icons and the
 * band renderer (docs/UI_DESIGN_SYSTEM.md is the specification; this file implements it).
 *
 * Drawing model. Every drawing call takes absolute screen coordinates (0..239). A frame is rendered band by
 * band into the strip canvas of gfx.c (cv_px): the band renderer clears the band, calls the screen's scene
 * function, which draws everything (primitives clip to the band), hashes each row and sends only the rows that
 * changed since the last frame (ds_rowh). So a screen never tracks what it drew; an unchanged frame costs a
 * render into RAM and no SPI traffic, a blinking dot costs its own rows. A scene signature (a hash of the view
 * model) lets a frame with nothing new skip even the render.
 *
 * Runs in the main loop only (never the audio ISR). No allocation, no flash access, bounded loops. */

typedef struct {               /* a glyph: RLE data offset, box size, offset from the pen / line top, advance */
    uint16_t off;
    uint8_t w, h;
    int8_t x, y;
    uint8_t adv;
} ds_glyph_t;
typedef struct {
    uint8_t h, base, cap;      /* line height, baseline from the line top, cap height (px) */
    const uint8_t *map;        /* ASCII 32..127 -> glyph, 0xFF none */
    const ds_glyph_t *g;
    const uint8_t *d;          /* (run - 1) << 4 | alpha, tools/gen_dsfont.py */
} ds_font_t;
#include "ds_fonts.h"

/* ------------------------------------------------------------ colour tokens --- */
#define DS_RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
/* neutrals: a warm black-to-white ramp (docs §2.1) */
#define DS_BG DS_RGB(0, 0, 0)                 /* the screen */
#define DS_SURF1 DS_RGB(20, 20, 22)           /* cells, cards */
#define DS_SURF2 DS_RGB(34, 34, 37)           /* raised: empty steps, tracks of gauges */
#define DS_LINE DS_RGB(48, 48, 52)            /* rules */
#define DS_TEXT3 DS_RGB(112, 111, 106)        /* tertiary: hints, units, inactive */
#define DS_TEXT2 DS_RGB(166, 165, 158)        /* secondary: labels */
#define DS_TEXT1 DS_RGB(244, 243, 238)        /* primary: values, names; also the selection fill */
/* semantic */
#define DS_REC DS_RGB(255, 64, 56)            /* recording, destructive */
#define DS_PLAY DS_RGB(92, 220, 140)          /* transport running */
#define DS_WARN DS_RGB(255, 184, 64)          /* count-in, overload */
/* the eight track colours: equal steps of hue, equal lightness, moderated chroma (docs §2.2) */
static const uint16_t DS_TRK[8] = {
    DS_RGB(255, 128, 82), DS_RGB(250, 196, 84), DS_RGB(182, 220, 98), DS_RGB(92, 214, 166),
    DS_RGB(92, 196, 236), DS_RGB(132, 152, 255), DS_RGB(190, 146, 250), DS_RGB(246, 132, 186),
};

/* ------------------------------------------------------------- the canvas --- */
#define DS_W 240
#define DS_H 240
#ifndef DS_BAND
#define DS_BAND 60                            /* rows per band (<= the canvas, gfx.c CV_MAX: 240 x 60) */
#endif
static int32_t ds_y0, ds_y1;                  /* the band being drawn: rows ds_y0 .. ds_y1 - 1 */
static uint32_t ds_rowh[DS_H];                /* hash of each row as last sent */
static uint32_t ds_sig_last;
static uint32_t ds_rows_sent, ds_frames;      /* statistics (console, tests) */

static inline uint16_t ds_sw(uint32_t c) { return (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8)); }

/* c blended over the pixel at (x, y) with alpha a (0..16) */
static inline void ds_px(int32_t x, int32_t y, uint16_t c, uint32_t a)
{
    uint16_t *p;
    uint32_t d, r, g, b;
    if ((uint32_t)x >= DS_W || y < ds_y0 || y >= ds_y1 || !a)
        return;
    p = &cv_px[(uint32_t)(y - ds_y0) * DS_W + (uint32_t)x];
    if (a >= 16u) {
        *p = ds_sw(c);
        return;
    }
    d = ds_sw(*p);
    r = (((d >> 11) * (16u - a)) + ((uint32_t)(c >> 11) * a)) >> 4;
    g = ((((d >> 5) & 63u) * (16u - a)) + ((uint32_t)((c >> 5) & 63u) * a)) >> 4;
    b = (((d & 31u) * (16u - a)) + ((uint32_t)(c & 31u) * a)) >> 4;
    *p = ds_sw((r << 11) | (g << 5) | b);
}

/* a colour between a (t = 0) and b (t = 16) */
static uint16_t ds_mix(uint16_t a, uint16_t b, uint32_t t)
{
    uint32_t r = (((uint32_t)(a >> 11) * (16u - t)) + ((uint32_t)(b >> 11) * t)) >> 4;
    uint32_t g = (((uint32_t)((a >> 5) & 63u) * (16u - t)) + ((uint32_t)((b >> 5) & 63u) * t)) >> 4;
    uint32_t bl = (((uint32_t)(a & 31u) * (16u - t)) + ((uint32_t)(b & 31u) * t)) >> 4;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}
#define DS_DIM(c) ds_mix(DS_BG, (c), 5)       /* a track colour as a quiet fill */
#define DS_MID(c) ds_mix(DS_BG, (c), 9)

static void ds_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t x1 = x + w, y1 = y + h, i, j;
    uint16_t s = ds_sw(c);
    if (x < 0) x = 0;
    if (y < ds_y0) y = ds_y0;
    if (x1 > DS_W) x1 = DS_W;
    if (y1 > ds_y1) y1 = ds_y1;
    for (j = y; j < y1; j++) {
        uint16_t *p = &cv_px[(uint32_t)(j - ds_y0) * DS_W];
        for (i = x; i < x1; i++)
            p[i] = s;
    }
}

static void ds_hline(int32_t x, int32_t y, int32_t w, uint16_t c) { ds_rect(x, y, w, 1, c); }
static void ds_vline(int32_t x, int32_t y, int32_t h, uint16_t c) { ds_rect(x, y, 1, h, c); }

/* integer square root (16.16-free): floor(sqrt(v)) */
static uint32_t ds_isqrt(uint32_t v)
{
    uint32_t r = 0, b = 1u << 30;
    while (b > v)
        b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

/* coverage (0..16) of the pixel whose centre is (px2, py2) (doubled coordinates) by the disc of centre
 * (cx2, cy2) and radius r2 (doubled) */
static uint32_t ds_cov(int32_t px2, int32_t py2, int32_t cx2, int32_t cy2, int32_t r2)
{
    int32_t dx = px2 - cx2, dy = py2 - cy2, e;
    uint32_t d16 = ds_isqrt((uint32_t)(dx * dx + dy * dy) << 8);   /* distance, doubled, x 16 */
    e = (r2 * 16 - (int32_t)d16) / 2 + 8;                          /* (r - d + 0.5) x 16, in pixels */
    return e <= 0 ? 0u : e >= 16 ? 16u : (uint32_t)e;
}

/* a rectangle with anti-aliased corners of radius r */
static void ds_rrect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c)
{
    int32_t i, j;
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;
    if (r <= 0 || y + h <= ds_y0 || y >= ds_y1) {
        ds_rect(x, y, w, h, c);
        return;
    }
    ds_rect(x, y + r, w, h - 2 * r, c);
    ds_rect(x + r, y, w - 2 * r, r, c);
    ds_rect(x + r, y + h - r, w - 2 * r, r, c);
    for (j = 0; j < r; j++)
        for (i = 0; i < r; i++) {
            uint32_t a = ds_cov(2 * i + 1, 2 * j + 1, 2 * r, 2 * r, 2 * r);
            ds_px(x + i, y + j, c, a);
            ds_px(x + w - 1 - i, y + j, c, a);
            ds_px(x + i, y + h - 1 - j, c, a);
            ds_px(x + w - 1 - i, y + h - 1 - j, c, a);
        }
}

/* an outline of thickness t (rounded): the outer shape in c, the inside put back to bg */
static void ds_rframe(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, int32_t t, uint16_t c, uint16_t bg)
{
    ds_rrect(x, y, w, h, r, c);
    ds_rrect(x + t, y + t, w - 2 * t, h - 2 * t, r > t ? r - t : 0, bg);
}

/* a disc inscribed in the box (x, y, d, d) */
static void ds_disc(int32_t x, int32_t y, int32_t d, uint16_t c)
{
    int32_t i, j;
    if (y + d <= ds_y0 || y >= ds_y1)
        return;
    for (j = 0; j < d; j++)
        for (i = 0; i < d; i++)
            ds_px(x + i, y + j, c, ds_cov(2 * i + 1, 2 * j + 1, d, d, d));
}

/* a ring (thickness t) inscribed in the box (x, y, d, d) */
static void ds_ring(int32_t x, int32_t y, int32_t d, int32_t t, uint16_t c)
{
    int32_t i, j;
    if (y + d <= ds_y0 || y >= ds_y1)
        return;
    for (j = 0; j < d; j++)
        for (i = 0; i < d; i++) {
            uint32_t o = ds_cov(2 * i + 1, 2 * j + 1, d, d, d), n = ds_cov(2 * i + 1, 2 * j + 1, d, d, d - 2 * t);
            ds_px(x + i, y + j, c, o > n ? o - n : 0u);
        }
}

/* a filled triangle, 4 x 4 supersampled (icons only: a few hundred pixels) */
static void ds_tri(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t c)
{
    int32_t minx = x0, maxx = x0, miny = y0, maxy = y0, x, y, sx, sy;
    int32_t area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (x1 < minx) minx = x1;
    if (x2 < minx) minx = x2;
    if (x1 > maxx) maxx = x1;
    if (x2 > maxx) maxx = x2;
    if (y1 < miny) miny = y1;
    if (y2 < miny) miny = y2;
    if (y1 > maxy) maxy = y1;
    if (y2 > maxy) maxy = y2;
    if (!area || maxy < ds_y0 || miny >= ds_y1)
        return;
    for (y = miny; y <= maxy; y++)
        for (x = minx; x <= maxx; x++) {
            uint32_t n = 0;
            for (sy = 0; sy < 4; sy++)
                for (sx = 0; sx < 4; sx++) {
                    int32_t px = x * 8 + sx * 2 + 1, py = y * 8 + sy * 2 + 1;   /* x 8 sub-pixel units */
                    int32_t e0 = (x1 * 8 - x0 * 8) * (py - y0 * 8) - (y1 * 8 - y0 * 8) * (px - x0 * 8);
                    int32_t e1 = (x2 * 8 - x1 * 8) * (py - y1 * 8) - (y2 * 8 - y1 * 8) * (px - x1 * 8);
                    int32_t e2 = (x0 * 8 - x2 * 8) * (py - y2 * 8) - (y0 * 8 - y2 * 8) * (px - x2 * 8);
                    if (area > 0 ? (e0 >= 0 && e1 >= 0 && e2 >= 0) : (e0 <= 0 && e1 <= 0 && e2 <= 0))
                        n++;
                }
            ds_px(x, y, c, n);
        }
}

/* the angle of (dx, dy) in 1/10 degree, clockwise from 12 o'clock (screen y grows down): 0..3599 */
static int32_t ds_angle(int32_t dx, int32_t dy)
{
    static const uint16_t AT[65] = {0, 9, 18, 27, 36, 45, 54, 62, 71, 80, 89, 98, 106, 115, 123, 132, 140, 149, 157,
                                    165, 174, 182, 190, 198, 206, 213, 221, 229, 236, 244, 251, 258, 266, 273, 280,
                                    287, 294, 300, 307, 314, 320, 326, 333, 339, 345, 351, 357, 363, 369, 374, 380,
                                    386, 391, 396, 402, 407, 412, 417, 422, 427, 432, 436, 441, 445, 450};
    int32_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, a;
    if (!ax && !ay)
        return 0;
    a = ax <= ay ? AT[ax * 64 / ay] : 900 - AT[ay * 64 / ax];   /* from the vertical, 0..900 */
    if (dx >= 0)
        return dy <= 0 ? a : 1800 - a;
    return dy <= 0 ? 3600 - a : 1800 + a;
}

/* an arc of a ring (thickness t, box x, y, d) from angle a0 to a1 (1/10 degree, clockwise from the top,
 * a1 >= a0, may pass 3600) */
static void ds_arc(int32_t x, int32_t y, int32_t d, int32_t t, int32_t a0, int32_t a1, uint16_t c)
{
    int32_t i, j;
    if (y + d <= ds_y0 || y >= ds_y1 || a1 <= a0)
        return;
    for (j = 0; j < d; j++)
        for (i = 0; i < d; i++) {
            uint32_t o = ds_cov(2 * i + 1, 2 * j + 1, d, d, d), n, a;
            int32_t ang;
            if (!o)
                continue;
            n = ds_cov(2 * i + 1, 2 * j + 1, d, d, d - 2 * t);
            a = o > n ? o - n : 0u;
            if (!a)
                continue;
            ang = ds_angle(2 * i + 1 - d, 2 * j + 1 - d);
            if ((ang >= a0 && ang <= a1) || (ang + 3600 >= a0 && ang + 3600 <= a1))
                ds_px(x + i, y + j, c, a);
        }
}

/* a line w px thick (Bresenham with a square pen: graphs, envelopes) */
static void ds_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w, uint16_t c)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy, guard = 1000;
    while (guard--) {
        int32_t e2 = 2 * err;
        ds_rect(x0 - w / 2, y0 - w / 2, w, w, c);
        if (x0 == x1 && y0 == y1)
            break;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* ------------------------------------------------------------------- text --- */
static const ds_glyph_t *ds_glyph(const ds_font_t *f, uint32_t ch)
{
    uint32_t i;
    if (ch >= 'a' && ch <= 'z')
        ch -= 32u;                            /* the fonts are capitals: lower case folds */
    if (ch < 32u || ch > 127u)
        ch = '?';
    i = f->map[ch - 32u];
    if (i == 0xFFu)
        i = f->map['?' - 32u];
    return i == 0xFFu ? 0 : &f->g[i];
}

static int32_t ds_track;                       /* px added after every glyph but the last (labels) */
static int32_t ds_cx1 = DS_W;                 /* glyphs that would end past this x are not drawn (clipping) */

static int32_t ds_text_w(const ds_font_t *f, const char *s)
{
    int32_t w = 0;
    if (!s)
        return 0;
    for (; *s; s++) {
        const ds_glyph_t *g = ds_glyph(f, (uint8_t)*s);
        if (g)
            w += g->adv + (s[1] ? ds_track : 0);
    }
    return w;
}

/* text with its baseline at y, starting at x; returns the pen's end */
static int32_t ds_text(const ds_font_t *f, int32_t x, int32_t y, const char *s, uint16_t c)
{
    int32_t top = y - f->base;
    if (!s)
        return x;
    for (; *s; s++) {
        const ds_glyph_t *g = ds_glyph(f, (uint8_t)*s);
        const uint8_t *d;
        uint32_t k = 0, n;
        int32_t gx, gy;
        if (!g)
            continue;
        if (x + g->adv > ds_cx1)
            break;
        gx = x + g->x;
        gy = top + g->y;
        if (g->w && gy < ds_y1 && gy + g->h > ds_y0 && gx < DS_W && gx + g->w > 0) {
            n = (uint32_t)g->w * g->h;
            d = f->d + g->off;
            while (k < n) {
                uint32_t b = *d++, run = (b >> 4) + 1u, a = b & 15u;
                if (a) {
                    uint32_t e = k + run, q;
                    for (q = k; q < e && q < n; q++)
                        ds_px(gx + (int32_t)(q % g->w), gy + (int32_t)(q / g->w), c, a + (a >> 3));   /* 0..15 -> 0..16 */
                }
                k += run;
            }
        }
        x += g->adv + ds_track;
    }
    return x - (*s ? 0 : ds_track);
}

/* text clipped to the width w (no glyph drawn past x + w) */
static int32_t ds_text_clip(const ds_font_t *f, int32_t x, int32_t y, int32_t w, const char *s, uint16_t c)
{
    int32_t e;
    ds_cx1 = x + w;
    e = ds_text(f, x, y, s, c);
    ds_cx1 = DS_W;
    return e;
}

/* a label (the 12 px capitals): tracked +1 px when it fits w, else as set, else tightened, else clipped */
static uint32_t ds_clipped;                   /* labels that did not fit (the host tests fail on any) */
static int32_t ds_label(int32_t x, int32_t y, int32_t w, const char *s, uint16_t c)
{
    int32_t t, e;
    for (t = 1; t > -2; t--) {
        ds_track = t;
        if (ds_text_w(&DS_F12, s) <= w)
            break;
    }
    if (t == -2) {
        ds_track = -1;
        ds_clipped++;
#ifdef DS_CLIP_LOG
        DS_CLIP_LOG(s, w);                    /* (host tests: which label, how wide the room was) */
#endif
    }
    e = ds_text_clip(&DS_F12, x, y, w, s, c);
    ds_track = 0;
    return e;
}

enum { DS_L, DS_C, DS_R };
/* text in a box x .. x + w: left, centred or right */
static int32_t ds_text_in(const ds_font_t *f, int32_t x, int32_t y, int32_t w, int32_t al, const char *s, uint16_t c)
{
    int32_t tw = ds_text_w(f, s);
    if (al == DS_C)
        x += (w - tw) / 2;
    else if (al == DS_R)
        x += w - tw;
    return ds_text(f, x, y, s, c);
}

/* the largest of the fonts given that fits w (for values that can be long: preset names, "OFF", "1/16T") */
static const ds_font_t *ds_fit(const char *s, int32_t w, const ds_font_t *big, const ds_font_t *small)
{
    if (ds_text_w(big, s) <= w)
        return big;
    return ds_text_w(small, s) <= w || small == &DS_F12 ? small : &DS_F12;
}

/* --------------------------------------------------------------- formatting --- */
static char *ds_itoa(char *b, int32_t v)       /* b gets v in decimal; returns the end */
{
    char t[12];
    uint32_t n = 0, u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    if (v < 0)
        *b++ = '-';
    do {
        t[n++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u && n < sizeof t);
    while (n)
        *b++ = t[--n];
    *b = 0;
    return b;
}
static char *ds_cat(char *b, const char *s)
{
    while (*s)
        *b++ = *s++;
    *b = 0;
    return b;
}

/* ------------------------------------------------------------------ icons --- */
/* geometric icons on a 12 px box (docs §6): drawn with the primitives, no bitmaps */
enum { IC_PLAY, IC_STOP, IC_REC, IC_ARM, IC_SYNTH, IC_DRUM, IC_MIDI, IC_LOCK, IC_LEFT, IC_RIGHT, IC_LOOP, IC_NOTE };
static void ds_icon(uint32_t ic, int32_t x, int32_t y, uint16_t c, uint16_t bg)
{
    switch (ic) {
    case IC_PLAY: ds_tri(x + 2, y, x + 2, y + 12, x + 12, y + 6, c); break;
    case IC_STOP: ds_rrect(x + 1, y + 1, 10, 10, 1, c); break;
    case IC_REC: ds_disc(x, y, 12, c); break;
    case IC_ARM: ds_ring(x, y, 12, 2, c); break;
    case IC_SYNTH:                                     /* a saw tooth */
        ds_tri(x, y + 11, x + 6, y + 1, x + 6, y + 11, c);
        ds_tri(x + 6, y + 11, x + 12, y + 1, x + 12, y + 11, c);
        break;
    case IC_DRUM:                                      /* four pads */
        ds_rrect(x, y, 5, 5, 1, c);
        ds_rrect(x + 7, y, 5, 5, 1, c);
        ds_rrect(x, y + 7, 5, 5, 1, c);
        ds_rrect(x + 7, y + 7, 5, 5, 1, c);
        break;
    case IC_MIDI:                                      /* a socket: ring and three pins */
        ds_ring(x, y, 12, 2, c);
        ds_rect(x + 3, y + 5, 2, 2, c);
        ds_rect(x + 7, y + 5, 2, 2, c);
        ds_rect(x + 5, y + 3, 2, 2, c);
        break;
    case IC_LOCK:
        ds_rframe(x + 2, y, 8, 8, 3, 2, c, bg);
        ds_rrect(x + 1, y + 5, 10, 7, 1, c);
        break;
    case IC_LEFT: ds_tri(x + 8, y + 1, x + 8, y + 11, x + 3, y + 6, c); break;
    case IC_RIGHT: ds_tri(x + 4, y + 1, x + 4, y + 11, x + 9, y + 6, c); break;
    case IC_LOOP:
        ds_rframe(x, y + 2, 12, 9, 3, 2, c, bg);
        ds_tri(x + 7, y, x + 7, y + 6, x + 11, y + 3, c);
        break;
    case IC_NOTE:
        ds_disc(x, y + 7, 6, c);
        ds_rect(x + 4, y, 2, 10, c);
        ds_rect(x + 4, y, 6, 2, c);
        break;
    }
}

/* ------------------------------------------------------------- components --- */
/* a rounded chip with centred text (the font's caps centred vertically) */
static void ds_chip(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t bg, uint16_t fg, const ds_font_t *f,
                    const char *s)
{
    ds_rrect(x, y, w, h, h >= 16 ? 4 : 3, bg);
    ds_text_in(f, x, y + (h + f->cap) / 2, w, DS_C, s, fg);
}
/* the same as an outline (an inactive or optional state: shape, not only colour) */
static void ds_chip_line(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c, uint16_t bg, const ds_font_t *f,
                         const char *s)
{
    ds_rframe(x, y, w, h, h >= 16 ? 4 : 3, 1, c, bg);
    ds_text_in(f, x, y + (h + f->cap) / 2, w, DS_C, s, c);
}

/* a horizontal gauge: ratio 0..1000 (-1: none), bipolar from the centre */
static void ds_gauge(int32_t x, int32_t y, int32_t w, int32_t h, int32_t ratio, int bipolar, uint16_t fg, uint16_t bg)
{
    int32_t f;
    ds_rect(x, y, w, h, bg);
    if (ratio < 0)
        return;
    if (ratio > 1000)
        ratio = 1000;
    f = ratio * w / 1000;
    if (bipolar) {
        int32_t m = w / 2;
        if (f >= m)
            ds_rect(x + m, y, f - m + 1, h, fg);
        else
            ds_rect(x + f, y, m - f + 1, h, fg);
        ds_rect(x + m, y - 2, 1, h + 4, DS_TEXT2);   /* the centre mark */
    } else {
        ds_rect(x, y, f, h, fg);
    }
}

/* the band renderer: draw `scene` band by band and send the rows that changed. sig: a hash of everything the
 * scene shows (0: always render); force: send every row (after something else drew on the screen) */
static void ds_paint(void (*scene)(void), uint32_t sig, int force)
{
    int32_t y, r;
    if (!force && sig && sig == ds_sig_last)
        return;
    ds_sig_last = sig;
    ds_frames++;
    for (y = 0; y < DS_H; y += DS_BAND) {
        int32_t bh = DS_H - y < DS_BAND ? DS_H - y : DS_BAND, run = -1;
        cv_begin(DS_W, (uint32_t)bh, DS_BG);
        ds_y0 = y;
        ds_y1 = y + bh;
        scene();
        for (r = 0; r <= bh; r++) {
            int dirty = 0;
            if (r < bh) {
                const uint16_t *p = &cv_px[(uint32_t)r * DS_W];
                uint32_t h = 2166136261u, i;
                for (i = 0; i < DS_W; i++)
                    h = (h ^ p[i]) * 16777619u;
                dirty = force || h != ds_rowh[y + r];
                ds_rowh[y + r] = h;
            }
            if (dirty && run < 0)
                run = r;
            if (!dirty && run >= 0) {            /* rows run .. r - 1 changed: one window, one DMA */
                lcd_blit(0, (uint32_t)(y + run), DS_W, (uint32_t)(r - run), cv_px + (uint32_t)run * DS_W);
                ds_rows_sent += (uint32_t)(r - run);
                run = -1;
            }
        }
    }
}

#if FM1MOVE
/* one-shot: text in a box, cleared to black, sent (the system screens: crash, update, UBOOT; SLOOP's signature) */
static void draw_text_box(uint32_t x, uint32_t y, uint32_t w, const felucca_font_t *f, const char *s, uint16_t c,
                          int align)
{
    (void)f;
    (void)w;
    cv_begin(DS_W, 20, DS_BG);   /* (the primitives index rows of DS_W pixels: a full-width band) */
    ds_y0 = 0;
    ds_y1 = 20;
    ds_text_in(&DS_F16, (int32_t)x, 15, (int32_t)w, align == 1 ? DS_C : align == 2 ? DS_R : DS_L, s, c);
    lcd_blit(0, y, DS_W, 20, cv_px);
    lcd_sync();
}
#endif

/* FNV-1a over a block (the scene signatures) */
static uint32_t ds_hash(uint32_t h, const void *p, uint32_t n)
{
    const uint8_t *b = p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}
