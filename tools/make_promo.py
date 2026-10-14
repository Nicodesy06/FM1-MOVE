#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The README's screen sheet and the promo images that show screens, from the firmware's own renders.

  python3 tools/ui_mockups.py      first: docs/ui/live/*.png (the virtual FM1 running the firmware's controls)
  python3 tools/make_promo.py      -> assets/readme/screens.png, promo/2-screens.png, promo/7-clips-step.png,
                                      promo/8-sloop26-tempo.png

Every screen here is drawn by the firmware's UI code in host simulation (tests/fm1move_ui_test.c), so the images
follow the firmware: run this again whenever the screens change. Not photos of an FM-1.
"""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
LIVE = ROOT / "docs/ui/live"
FONT = ROOT / "assets/fonts/inter"
BG = (10, 10, 11)
TEXT1, TEXT2, TEXT3 = (244, 241, 234), (170, 168, 162), (110, 108, 104)
AMBER = (255, 200, 87)


def shot(name, scale):
    im = Image.open(LIVE / f"{name}.png").convert("RGB")
    return im.resize((im.width * scale // 4, im.height * scale // 4), Image.NEAREST if scale % 4 == 0 else Image.LANCZOS)


def card(base, im, x, y, radius=14):
    """a screen with rounded corners and a thin frame, as on the panel"""
    d = ImageDraw.Draw(base)
    d.rounded_rectangle((x - 4, y - 4, x + im.width + 3, y + im.height + 3), radius + 4, fill=(36, 36, 40))
    mask = Image.new("L", im.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, im.width - 1, im.height - 1), radius, fill=255)
    base.paste(im, (x, y), mask)


def font(name, size):
    return ImageFont.truetype(str(FONT / name), size)


def readme_sheet():
    names = ["01-play", "03-sequencer", "03b-sequencer-step", "16-steps-held",
             "07c-clips-queued", "05-groove", "17-key-held", "08-mixer"]
    s, gap = 480, 24
    out = Image.new("RGB", (4 * s + 5 * gap, 2 * s + 3 * gap), BG)
    for i, n in enumerate(names):
        out.paste(shot(n, 8), (gap + (i % 4) * (s + gap), gap + (i // 4) * (s + gap)))
    out.save(ROOT / "assets/readme/screens.png", optimize=True)


def promo_screens():
    out = Image.new("RGB", (1920, 1080), BG)
    d = ImageDraw.Draw(out)
    d.text((110, 78), "Every function, one press away", font=font("InterDisplay-Bold.otf", 72), fill=TEXT1)
    d.text((112, 172), "The FM-1's 240 × 240 display — rendered here by the firmware's own code",
           font=font("Inter-SemiBold.otf", 32), fill=TEXT2)
    items = [("01-play", "PLAY"), ("03-sequencer", "SEQUENCER"), ("03b-sequencer-step", "STEP EDIT"),
             ("07c-clips-queued", "CLIPS"), ("05-groove", "GROOVE"),
             ("02-tracks", "TRACKS"), ("08-mixer", "MIXER"), ("09-sound", "SOUND"),
             ("12-project-live", "PROJECTS"), ("13-settings-live", "SETTINGS")]
    s, gx, x0, y0, rowh = 300, 47, 110, 255, 375
    lab = font("Inter-Bold.otf", 24)
    for i, (n, t) in enumerate(items):
        x, y = x0 + (i % 5) * (s + gx), y0 + (i // 5) * rowh
        card(out, shot(n, 5), x, y)
        d.text((x + 2, y + s + 18), t, font=lab, fill=TEXT2)
    out.save(ROOT / "promo/2-screens.png", optimize=True)


def promo_clips():
    out = Image.new("RGB", (1920, 1080), BG)
    d = ImageDraw.Draw(out)
    d.text((110, 70), "NEW IN 0.2", font=font("Inter-Bold.otf", 28), fill=AMBER)
    d.text((108, 112), "Clips, scenes and step editing", font=font("InterDisplay-Bold.otf", 72), fill=TEXT1)
    d.text((112, 206), "8 tracks × 6 clips launched on the next bar · SLOOP's step editor and layer pages are back",
           font=font("Inter-SemiBold.otf", 32), fill=TEXT2)
    s, gx = 300, 47
    cols = [("07c-clips-queued", "CLIPS held + keys", "launch clips · scenes"),
            ("07b-clips-live", "CLIPS screen", "copy · clear · launch"),
            ("03b-sequencer-step", "STEP page", "chords · ties · accent · slide"),
            ("16-steps-held", "SEQ held: STEPS", "the steps, from any screen"),
            ("17-key-held", "SEL held: KEY", "the chord on every key")]
    for i, (n, a, b) in enumerate(cols):
        x = 110 + i * (s + gx)
        card(out, shot(n, 5), x, 330)
        d.text((x, 660), a, font=font("Inter-Bold.otf", 26), fill=TEXT1)
        d.text((x, 698), b, font=font("Inter-SemiBold.otf", 23), fill=TEXT3)
    d.text((110, 970), "Experimental · simulated on a computer, not yet tested on an FM-1 · nicodesy06.github.io/FM1-MOVE",
           font=font("Inter-SemiBold.otf", 26), fill=TEXT3)
    out.save(ROOT / "promo/7-clips-step.png", optimize=True)


def promo_v03():
    out = Image.new("RGB", (1920, 1080), BG)
    d = ImageDraw.Draw(out)
    d.text((110, 70), "NEW IN 0.3", font=font("Inter-Bold.otf", 28), fill=AMBER)
    d.text((108, 112), "SLOOP 2.6 inside, tempo in one hand", font=font("InterDisplay-Bold.otf", 72), fill=TEXT1)
    d.text((112, 206), "27 punch-ins · INSERT on every track · TEXTURE · MIDI LEARN · ALGORITHM is the tempo",
           font=font("Inter-SemiBold.otf", 32), fill=TEXT2)
    s, gx = 360, 64
    cols = [("18-tempo", "Turn ALGORITHM", "tap OCT− + OCT+ in time"),
            ("11d-perform-black", "FX + black keys", "11 more punch-ins"),
            ("11c-texture", "TEXTURE", "a bed under the mix"),
            ("09b-sound-insert", "INSERT", "drive, fuzz, crush, phaser…")]
    for i, (n, a, b) in enumerate(cols):
        x = 110 + i * (s + gx)
        card(out, shot(n, 6), x, 320)
        d.text((x, 718), a, font=font("Inter-Bold.otf", 28), fill=TEXT1)
        d.text((x, 758), b, font=font("Inter-SemiBold.otf", 24), fill=TEXT3)
    d.text((110, 970), "Experimental · simulated on a computer, not yet tested on an FM-1 · nicodesy06.github.io/FM1-MOVE",
           font=font("Inter-SemiBold.otf", 26), fill=TEXT3)
    out.save(ROOT / "promo/8-sloop26-tempo.png", optimize=True)


if __name__ == "__main__":
    readme_sheet()
    promo_screens()
    promo_clips()
    promo_v03()
    print("make_promo: assets/readme/screens.png, promo/2-screens.png, promo/7-clips-step.png, promo/8-sloop26-tempo.png")
