#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# FM1 Move: build and run tests/ui_mockups.c (the firmware's own UI code on the host), convert its frames to PNG
# (native 240 x 240) and make a contact sheet at 2x for review.
#   tools/ui_mockups.py [OUTDIR]        (default docs/ui/mockups)
import glob
import os
import subprocess
import sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "docs", "ui", "mockups")
TMP = os.path.join(ROOT, "build", "host", "ui")
os.makedirs(OUT, exist_ok=True)
os.makedirs(TMP, exist_ok=True)
gen = os.path.join(ROOT, "build", "gen")
os.makedirs(gen, exist_ok=True)
subprocess.check_call([sys.executable, os.path.join(ROOT, "tools", "gen_dsfont.py"), os.path.join(gen, "ds_fonts.h")],
                      stdout=subprocess.DEVNULL)
exe = os.path.join(ROOT, "build", "host", "ui_mockups")
subprocess.check_call(["cc", "-O1", "-Wall", "-Wno-unused-function", "-I" + gen, "-I" + os.path.join(ROOT, "firmware", "src"),
                       "-o", exe, os.path.join(ROOT, "tests", "ui_mockups.c")])
rc = subprocess.call([exe, TMP])
frames = sorted(glob.glob(os.path.join(TMP, "*.ppm")))
for f in frames:
    Image.open(f).save(os.path.join(OUT, os.path.basename(f)[:-4] + ".png"))
# contact sheet: 2x, nearest neighbour (what the pixels are), 4 per row, captions
S, PAD, CAP = 2, 24, 22
cols = 4
rows = (len(frames) + cols - 1) // cols
sheet = Image.new("RGB", (cols * (240 * S + PAD) + PAD, rows * (240 * S + PAD + CAP) + PAD), (40, 40, 44))
d = ImageDraw.Draw(sheet)
try:
    font = ImageFont.truetype(os.path.join(ROOT, "assets", "fonts", "inter", "Inter-SemiBold.otf"), 16)
except OSError:
    font = None
for i, f in enumerate(frames):
    x = PAD + (i % cols) * (240 * S + PAD)
    y = PAD + (i // cols) * (240 * S + PAD + CAP)
    d.text((x, y), os.path.basename(f)[:-4].replace("_", " ").upper(), fill=(200, 200, 196), font=font)
    sheet.paste(Image.open(f).resize((240 * S, 240 * S), Image.NEAREST), (x, y + CAP))
sheet.save(os.path.join(OUT, "contact_sheet.png"))
# the live screens: the virtual FM1 (tests/fm1move_ui_test.c) running the firmware's controls on the eight-track engine
LIVE = os.path.join(os.path.dirname(OUT), "live")
os.makedirs(LIVE, exist_ok=True)
lexe = os.path.join(ROOT, "build", "host", "fm1move_ui_test")
ltmp = os.path.join(TMP, "live")
os.makedirs(ltmp, exist_ok=True)
if subprocess.call(["cc", "-O1", "-w", "-I" + gen, "-I" + os.path.join(ROOT, "firmware", "src"), "-I" + os.path.join(ROOT, "firmware", "hal"),
                    "-o", lexe, os.path.join(ROOT, "tests", "fm1move_ui_test.c"), "-lm"]) == 0:
    rc |= subprocess.call([lexe, ltmp], stdout=subprocess.DEVNULL)
    lf = sorted(glob.glob(os.path.join(ltmp, "live-*.ppm")))
    for f in lf:
        Image.open(f).save(os.path.join(LIVE, os.path.basename(f)[5:-4] + ".png"))
    rows = (len(lf) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * (240 * S + PAD) + PAD, rows * (240 * S + PAD + CAP) + PAD), (40, 40, 44))
    d = ImageDraw.Draw(sheet)
    for i, f in enumerate(lf):
        x = PAD + (i % cols) * (240 * S + PAD)
        y = PAD + (i // cols) * (240 * S + PAD + CAP)
        d.text((x, y), os.path.basename(f)[5:-4].replace("-", " ").upper(), fill=(200, 200, 196), font=font)
        sheet.paste(Image.open(f).resize((240 * S, 240 * S), Image.NEAREST), (x, y + CAP))
    sheet.save(os.path.join(LIVE, "contact_sheet.png"))
    print(f"ui live: {len(lf)} screens -> {LIVE}")
print(f"ui mockups: {len(frames)} screens -> {OUT}")
sys.exit(rc)
