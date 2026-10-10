#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# FM1 Move: WCAG 2.x contrast of the design-system colour pairs, read from firmware/src/ds.c (after the RGB565
# quantisation the panel really shows). Fails when a pair is under its role's minimum (docs/UI_DESIGN_SYSTEM.md §2.4).
import os
import re
import sys

SRC = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "firmware", "src", "ds.c")).read()


def q565(r, g, b):
    r5, g6, b5 = r >> 3, g >> 2, b >> 3
    return (r5 * 255 // 31, g6 * 255 // 63, b5 * 255 // 31)


def lum(c):
    def ch(v):
        v /= 255
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = (ch(x) for x in c)
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def ratio(a, b):
    la, lb = sorted((lum(a), lum(b)), reverse=True)
    return (la + 0.05) / (lb + 0.05)


tok = {m[0]: q565(*map(int, m[1:])) for m in re.findall(r"#define (DS_\w+) DS_RGB\((\d+), (\d+), (\d+)\)", SRC)}
trk = [q565(*map(int, m)) for m in re.findall(r"DS_RGB\((\d+), (\d+), (\d+)\)", SRC[SRC.index("DS_TRK[8]"):SRC.index("};", SRC.index("DS_TRK[8]"))])]
pairs = [  # foreground, background, minimum, role
    ("DS_TEXT1", "DS_BG", 7.0, "primary text"),
    ("DS_TEXT1", "DS_SURF2", 7.0, "primary text on a raised surface"),
    ("DS_TEXT2", "DS_BG", 4.5, "secondary text (labels)"),
    ("DS_TEXT2", "DS_SURF1", 4.5, "labels on a card"),
    ("DS_TEXT3", "DS_BG", 3.0, "tertiary text (hints, units)"),
    ("DS_TEXT3", "DS_SURF1", 3.0, "tertiary on a card"),
    ("DS_BG", "DS_TEXT1", 7.0, "inverted (selection)"),
    ("DS_REC", "DS_BG", 3.0, "record indicator (icon, with a shape)"),
    ("DS_PLAY", "DS_BG", 3.0, "transport indicator"),
    ("DS_BG", "DS_REC", 4.5, "text on the destructive button"),
]
fail = 0
for fg, bg, mn, role in pairs:
    r = ratio(tok[fg], tok[bg])
    ok = r >= mn
    fail += not ok
    print(f"contrast {fg:9s} on {bg:9s} {r:5.2f}:1 (min {mn}) {role:38s} {'ok' if ok else 'FAIL'}")
for i, c in enumerate(trk):
    for bg, mn, role in (("DS_BG", 4.5, "track colour as text"), ("DS_SURF1", 4.5, "track colour on a card")):
        r = ratio(c, tok[bg])
        ok = r >= mn
        fail += not ok
        print(f"contrast TRACK {i + 1}  on {bg:9s} {r:5.2f}:1 (min {mn}) {role:38s} {'ok' if ok else 'FAIL'}")
    r = ratio(tok["DS_BG"], c)
    ok = r >= 4.5
    fail += not ok
    print(f"contrast DS_BG     on TRACK {i + 1}  {r:5.2f}:1 (min 4.5) number on a track chip                 {'ok' if ok else 'FAIL'}")
print("contrast: all pairs ok" if not fail else f"contrast: {fail} pair(s) under the minimum")
sys.exit(1 if fail else 0)
