#!/usr/bin/env python3
"""Turn any single image (PNG/GIF/WebP, ideally with a transparent background)
into a sprite pack the engine can load.

    python3 tools/image2pack.py my_creature.png --name "Ghosty"
    python3 tools/image2pack.py art.gif --name Ghosty --id private-ghosty --size 48 --scale 2

An evolution family in one pack (each stage has its own picture):

    python3 tools/image2pack.py egg.png --name Eggy --evolve 5 Chick chick.gif --evolve 10 Hen hen.gif

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


def fit(im, size, box=None):
    im = remove_flat_background(im)
    box = box or im.getbbox()
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


def build_sheet(path, size, faces_left):
    """One image (or animated GIF) -> (DPS1 bytes, frame count, animations)."""
    src = Image.open(path)
    raw = [f.convert("RGBA") for f in ImageSequence.Iterator(src)]
    if len(raw) > 8:  # sample the whole loop evenly, max 8 frames
        raw = [raw[i * len(raw) // 8] for i in range(8)]
    # One crop box for all frames so the animation doesn't jitter.
    boxes = [b for b in (remove_flat_background(f).getbbox() for f in raw) if b]
    box = (min(b[0] for b in boxes), min(b[1] for b in boxes),
           max(b[2] for b in boxes), max(b[3] for b in boxes)) if boxes else None
    idle = [fit(f, size, box) for f in raw]
    if faces_left:
        idle = [f.transpose(Image.FLIP_LEFT_RIGHT) for f in idle]
    base = idle[0]

    frames = list(idle)
    def add(im):
        frames.append(im)
        return len(frames) - 1

    animations = {}
    if len(idle) == 1:
        # One still picture: make movement by bobbing, squashing and dimming it.
        animations["idle"] = [0, add(shifted(base, 1))]
        animations["walk"] = [add(squashed(base, 0.94)), add(shifted(base, 2))]
        happy = [add(shifted(base, 3)), 0]
        animations["idle_happy"] = happy
        animations["play"] = happy
        sad = [add(dimmed(base, 0.8))]
    else:
        # Animated GIF: keep its own motion in every state.
        animations["idle"] = list(range(len(idle)))
        animations["walk"] = [add(shifted(f, 2 if i % 2 else 0)) for i, f in enumerate(idle)]
        animations["play"] = [add(shifted(f, 4 if i % 2 else 1)) for i, f in enumerate(idle[::2])]
        sad = [add(dimmed(f, 0.8)) for f in idle[::2]]
    animations["sleep"] = [add(dimmed(squashed(base, 0.7)))]
    animations["eat"] = [add(squashed(base, 0.88)), 0]
    animations["idle_sad"] = sad
    animations["idle_hungry"] = sad

    sheet = Image.new("RGBA", (size * len(frames), size), (0, 0, 0, 0))
    for i, f in enumerate(frames):
        sheet.paste(f, (i * size, 0))
    data, _ = convert(sheet, size, background=False)
    return data, len(frames), animations, dominant_hex(base)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("image")
    ap.add_argument("--name", required=True, help="display name, max 24 chars")
    ap.add_argument("--id", help="pack id (default: private-<name>)")
    ap.add_argument("--size", type=int, default=48, help="frame size in pixels (default 48)")
    ap.add_argument("--scale", type=int, default=2, help="on-screen scale 1-6 (default 2)")
    ap.add_argument("--faces-left", action="store_true", help="the art looks left (engine expects right)")
    ap.add_argument("--evolve", nargs=3, action="append", default=[], metavar=("LEVEL", "NAME", "IMAGE"),
                    help="evolve into NAME (picture IMAGE) at LEVEL; repeat for more stages")
    ap.add_argument("--out", default=os.path.join(ROOT, "fs", "packs"))
    a = ap.parse_args()

    pid = a.id or "private-" + re.sub(r"[^a-z0-9]+", "-", a.name.lower()).strip("-")
    if not re.fullmatch(r"[a-z0-9][a-z0-9_.-]{0,23}", pid) or ".." in pid:
        sys.exit(f"bad pack id {pid!r}: use 1-24 chars of a-z 0-9 - _ .")
    if a.size * a.scale > 160:
        sys.exit("size * scale must be <= 160 px so the pet fits the scenes")
    stages = [(1, a.name, a.image)]
    for level, name, image in a.evolve:
        if not level.isdigit() or int(level) < 2:
            sys.exit(f"--evolve level must be a number >= 2, got {level!r}")
        stages.append((int(level), name, image))
    stages.sort(key=lambda s: s[0])
    if len(stages) > 8:
        sys.exit("at most 8 stages")

    out = os.path.join(a.out, pid)
    os.makedirs(out, exist_ok=True)
    manifest = {
        "format": 1,
        "id": pid,
        "name": a.name[:24],
        "version": "1.0.0",
        "creature": {"sprites": "sprites.dps", "scale": a.scale},
        "evolution": [],
    }
    total = 0
    for i, (level, name, image) in enumerate(stages):
        data, count, animations, body = build_sheet(image, a.size, a.faces_left)
        fname = "sprites.dps" if i == 0 else f"stage{i}.dps"
        with open(os.path.join(out, fname), "wb") as f:
            f.write(data)
        total += len(data)
        stage = {"level": level, "name": name[:24]}
        if i == 0:
            manifest["creature"].update({"animations": animations, "body": body, "outline": body})
        else:
            stage.update({"sprites": fname, "animations": animations})
        manifest["evolution"].append(stage)
        print(f"  stage {i}: {name} (Lv {level}) {count} frames, {len(data)} bytes")
    with open(os.path.join(out, "manifest.json"), "w") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print(f"wrote {out} ({total} bytes)")
    if pid.startswith("private-"):
        print("private-* packs are gitignored: they stay on this machine and your device.")
    print("preview: ./build/deskpet_headless --preview", pid)


if __name__ == "__main__":
    main()
