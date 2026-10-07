#!/usr/bin/env python3
"""Draws the sample 'Sprout' creature and writes the sprite-based pack to
fs/packs/sprout. Shows the sprite pipeline end to end: PNG -> png2dps -> pack.

    python3 tools/make_sprout_pack.py
"""
import json
import os
import sys

from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(__file__))
from png2dps import convert  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "fs", "packs", "sprout")
S = 24  # frame size; drawn at 3x on screen

BODY = (250, 196, 90, 255)
DARK = (120, 80, 30, 255)
LEAF = (90, 180, 80, 255)
INK = (40, 30, 40, 255)
CHEEK = (240, 120, 120, 255)


def frame(eyes="dot", mouth="smile", step=0, squash=0, leaf_tilt=0):
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    top = 7 + squash
    # leaf sprout
    d.line([(12, top), (12 + leaf_tilt, top - 4)], fill=DARK)
    d.ellipse([7 + leaf_tilt, top - 7, 12 + leaf_tilt, top - 3], fill=LEAF)
    d.ellipse([12 + leaf_tilt, top - 6, 17 + leaf_tilt, top - 2], fill=LEAF)
    # body
    d.ellipse([3, top, 20, 21], fill=DARK)
    d.ellipse([4, top + 1, 19, 20], fill=BODY)
    # feet
    d.rectangle([6, 21 - (1 if step == 1 else 0), 9, 22 - (1 if step == 1 else 0)], fill=DARK)
    d.rectangle([14, 21 - (1 if step == 2 else 0), 17, 22 - (1 if step == 2 else 0)], fill=DARK)
    ey = top + 6
    if eyes == "dot":
        d.rectangle([8, ey, 9, ey + 1], fill=INK)
        d.rectangle([14, ey, 15, ey + 1], fill=INK)
    elif eyes == "closed":
        d.line([(7, ey + 1), (10, ey + 1)], fill=INK)
        d.line([(13, ey + 1), (16, ey + 1)], fill=INK)
    elif eyes == "happy":
        d.line([(7, ey + 1), (8, ey), (9, ey), (10, ey + 1)], fill=INK)
        d.line([(13, ey + 1), (14, ey), (15, ey), (16, ey + 1)], fill=INK)
    my = ey + 4
    if mouth == "smile":
        d.line([(10, my), (11, my + 1), (12, my + 1), (13, my)], fill=INK)
    elif mouth == "frown":
        d.line([(10, my + 1), (11, my), (12, my), (13, my + 1)], fill=INK)
    elif mouth == "open":
        d.rectangle([10, my, 13, my + 2], fill=INK)
    elif mouth == "flat":
        d.line([(10, my), (13, my)], fill=INK)
    d.point([(6, ey + 3), (17, ey + 3)], fill=CHEEK)
    return im


FRAMES = [
    frame(),                                      # 0 idle
    frame(step=1, leaf_tilt=-1),                  # 1 walk a
    frame(step=2, leaf_tilt=1),                   # 2 walk b
    frame(eyes="closed", mouth="flat", squash=4), # 3 sleep
    frame(mouth="open"),                          # 4 eat open
    frame(mouth="flat"),                          # 5 eat closed
    frame(eyes="happy", mouth="smile"),           # 6 happy
    frame(eyes="dot", mouth="frown", leaf_tilt=-2),  # 7 sad
    frame(eyes="dot", mouth="open", leaf_tilt=2),    # 8 hungry
]


def main():
    os.makedirs(OUT, exist_ok=True)
    sheet = Image.new("RGBA", (S * len(FRAMES), S), (0, 0, 0, 0))
    for i, f in enumerate(FRAMES):
        sheet.paste(f, (i * S, 0))
    sheet.save(os.path.join(ROOT, "tools", "art", "sprout_sheet.png"))
    data, _ = convert(sheet, S, background=False)
    with open(os.path.join(OUT, "sprout.dps"), "wb") as f:
        f.write(data)

    manifest = {
        "format": 1,
        "id": "sprout",
        "name": "Sprout",
        "version": "0.1.0",
        "creature": {
            "sprites": "sprout.dps",
            "scale": 3,
            "body": "#FAC45A",
            "outline": "#78501E",
            "animations": {
                "idle": [0],
                "idle_happy": [6],
                "idle_sad": [7],
                "idle_hungry": [8],
                "walk": [1, 2],
                "sleep": [3],
                "eat": [4, 5],
                "play": [6, 0],
            },
        },
        "decay": {"awake": {"food": 12, "fun": 6, "energy": 8}, "asleep": {"food": 5, "fun": 0, "energyGain": 25}},
        "mood": {"hungryBelow": 35, "sadBelow": 30},
        "behavior": {"outingChancePct": 4, "outingMaxTicks": 60},
        "rules": [{"stat": "energy", "below": 20, "mood": "sad"}],
        "evolution": [{"level": 1, "name": "Sprout"}, {"level": 4, "name": "Sapling"}],
        "sounds": {"happy": "happy:d=16,o=6,b=220:g,c7,e7"},
    }
    with open(os.path.join(OUT, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print("wrote", OUT)


if __name__ == "__main__":
    main()
