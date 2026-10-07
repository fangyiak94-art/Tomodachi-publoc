#!/usr/bin/env python3
"""Turn any single image (PNG/GIF/WebP, ideally with a transparent background)
into a sprite pack the engine can load.

    python3 tools/image2pack.py my_creature.png --name "Ghosty"
    python3 tools/image2pack.py art.gif --name Ghosty --id private-ghosty --size 48 --scale 2

The idle, walk, sleep, eat and happy frames are made from the one image
(bob, squash, hop, dim). An animated GIF's own frames become the idle loop.

By default the pack goes to fs/packs/private-<name>/, which is gitignored.
Use that for art you don't own (fan art, franchise characters): it stays on
your machine and your own device and is never committed or shared.
"""
import argparse
import json
import os
import re
import sys

from PIL import Image, ImageEnhance, ImageSequence

sys.path.insert(0, os.path.dirname(__file__))
from png2dps import convert  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def remove_flat_background(im, tolerance=24):
    """If the image has no transparency, treat the corner colour as background."""
    im = im.convert("RGBA")
    if im.getextrema()[3][0] < 250:
        return im  # already has transparency
    px = im.load()
    w, h = im.size
    bg = px[0, 0]
    stack, seen = [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)], set()
    while stack:
        x, y = stack.pop()
        if (x, y) in seen or not (0 <= x < w and 0 <= y < h):
            continue
        seen.add((x, y))
        r, g, b, a = px[x, y]
        if abs(r - bg[0]) + abs(g - bg[1]) + abs(b - bg[2]) > tolerance:
            continue
        px[x, y] = (0, 0, 0, 0)
        stack += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return im


def fit(im, size):
    im = remove_flat_background(im)
    box = im.getbbox()
    if box:
        im = im.crop(box)
    w, h = im.size
    k = (size - 2) / max(w, h)
    nw, nh = max(1, round(w * k)), max(1, round(h * k))
    small_art = max(w, h) <= size * 2
    im = im.resize((nw, nh), Image.NEAREST if small_art else Image.LANCZOS)
    # Hard alpha edge: the format has on/off transparency only.
    a = im.split()[3].point(lambda v: 255 if v >= 128 else 0)
    im.putalpha(a)
    canvas = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    canvas.paste(im, ((size - nw) // 2, size - nh), im)  # feet on the ground
    return canvas


def shifted(im, dy):
    out = Image.new("RGBA", im.size, (0, 0, 0, 0))
    out.paste(im, (0, -dy), im)
    return out


def squashed(im, factor):
    w, h = im.size
    box = im.getbbox() or (0, 0, w, h)
    body = im.crop(box)
    bw, bh = body.size
    body = body.resize((min(w, round(bw * (2 - factor))), max(1, round(bh * factor))), Image.NEAREST)
    out = Image.new("RGBA", im.size, (0, 0, 0, 0))
    out.paste(body, ((w - body.size[0]) // 2, h - body.size[1]), body)
    return out


def dimmed(im, k=0.55):
    rgb = ImageEnhance.Brightness(im.convert("RGB")).enhance(k).convert("RGBA")
    rgb.putalpha(im.split()[3])
    return rgb


def dominant_hex(im):
    small = im.resize((16, 16))
    counts = {}
    px = small.load()
    for r, g, b, a in (px[x, y] for y in range(16) for x in range(16)):
        if a:
            key = (r // 16 * 16, g // 16 * 16, b // 16 * 16)
            counts[key] = counts.get(key, 0) + 1
    if not counts:
        return "#9C8CF0"
    r, g, b = max(counts, key=counts.get)
    return "#%02X%02X%02X" % (r, g, b)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image")
    ap.add_argument("--name", required=True, help="display name, max 24 chars")
    ap.add_argument("--id", help="pack id (default: private-<name>)")
    ap.add_argument("--size", type=int, default=48, help="frame size in pixels (default 48)")
    ap.add_argument("--scale", type=int, default=2, help="on-screen scale 1-6 (default 2)")
    ap.add_argument("--faces-left", action="store_true", help="the art looks left (engine expects right)")
    ap.add_argument("--out", default=os.path.join(ROOT, "fs", "packs"))
    a = ap.parse_args()

    pid = a.id or "private-" + re.sub(r"[^a-z0-9]+", "-", a.name.lower()).strip("-")
    if not re.fullmatch(r"[a-z0-9][a-z0-9_.-]{0,23}", pid) or ".." in pid:
        sys.exit(f"bad pack id {pid!r}: use 1-24 chars of a-z 0-9 - _ .")
    if a.size * a.scale > 160:
        sys.exit("size * scale must be <= 160 px so the pet fits the scenes")

    src = Image.open(a.image)
    idle = [fit(f.copy(), a.size) for f in ImageSequence.Iterator(src)][:8]
    if a.faces_left:
        idle = [f.transpose(Image.FLIP_LEFT_RIGHT) for f in idle]
    base = idle[0]

    frames = list(idle)
    def add(im):
        frames.append(im)
        return len(frames) - 1
    if len(idle) == 1:
        idle_anim = [0, add(shifted(base, 1))]
    else:
        idle_anim = list(range(len(idle)))
    walk = [add(squashed(base, 0.94)), add(shifted(base, 2))]
    sleep = [add(dimmed(squashed(base, 0.7)))]
    eat = [add(squashed(base, 0.88)), 0]
    happy = [add(shifted(base, 3)), 0]
    sad = [add(dimmed(base, 0.8))]

    sheet = Image.new("RGBA", (a.size * len(frames), a.size), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        sheet.paste(f, (i * a.size, 0))
    data, _ = convert(sheet, a.size, background=False)

    out = os.path.join(a.out, pid)
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "sprites.dps"), "wb") as f:
        f.write(data)
    body = dominant_hex(base)
    manifest = {
        "format": 1,
        "id": pid,
        "name": a.name[:24],
        "version": "1.0.0",
        "creature": {
            "sprites": "sprites.dps",
            "scale": a.scale,
            "body": body,
            "outline": body,
            "animations": {
                "idle": idle_anim,
                "walk": walk,
                "sleep": sleep,
                "eat": eat,
                "play": happy,
                "idle_happy": happy,
                "idle_sad": sad,
                "idle_hungry": sad,
            },
        },
    }
    with open(os.path.join(out, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print(f"wrote {out} ({len(frames)} frames, {len(data)} bytes)")
    if pid.startswith("private-"):
        print("private-* packs are gitignored: they stay on this machine and your device.")
    print("preview: ./build/deskpet_headless --preview", pid)


if __name__ == "__main__":
    main()
