#!/usr/bin/env python3
"""Turn pictures into a room theme (house and/or yard background).

    python3 tools/image2room.py --name "My Den" --house den.png --yard garden.jpg
    python3 tools/image2room.py --name Castle --house castle.png --full --bed 70 --door 180

Each picture is centre-cropped to a square and reduced to 16 colours. By
default it is stored at 120x120 and drawn at 2x (7 KB). --full stores it at
240x240 (29 KB) for more detail. The pet walks along --ground (default 182),
so pick or crop art whose floor line sits about three quarters down.

Spots (where the pet stands) are x positions: --bed, --bowl, --door (house)
and --yard-door, --tree (yard). Fine-tune them later in the room editor
(Settings > Upload on the device, then open the page shown).

Rooms go to fs/rooms/private-<name>/ by default (gitignored).
"""
import argparse
import json
import os
import re
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
from png2dps import convert  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def square(path, size):
    im = Image.open(path).convert("RGBA")
    w, h = im.size
    s = min(w, h)
    im = im.crop(((w - s) // 2, (h - s) // 2, (w - s) // 2 + s, (h - s) // 2 + s))
    return im.resize((size, size), Image.LANCZOS)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", required=True)
    ap.add_argument("--id")
    ap.add_argument("--house", help="picture for inside the house")
    ap.add_argument("--yard", help="picture for the yard")
    ap.add_argument("--full", action="store_true", help="240x240 instead of 120x120 at 2x")
    ap.add_argument("--ground", type=int, default=182)
    ap.add_argument("--bed", type=int, default=62)
    ap.add_argument("--bowl", type=int, default=150)
    ap.add_argument("--door", type=int, default=184)
    ap.add_argument("--yard-door", type=int, default=176)
    ap.add_argument("--tree", type=int, default=70)
    ap.add_argument("--out", default=os.path.join(ROOT, "fs", "rooms"))
    a = ap.parse_args()
    if not (a.house or a.yard):
        sys.exit("give --house and/or --yard")

    rid = a.id or "private-" + re.sub(r"[^a-z0-9]+", "-", a.name.lower()).strip("-")
    if not re.fullmatch(r"[a-z0-9][a-z0-9_.-]{0,23}", rid) or ".." in rid:
        sys.exit(f"bad room id {rid!r}")
    out = os.path.join(a.out, rid)
    os.makedirs(out, exist_ok=True)
    size = 240 if a.full else 120

    backgrounds = {}
    for scene, path in (("house", a.house), ("yard", a.yard)):
        if not path:
            continue
        data, _ = convert(square(path, size), None, background=True)
        with open(os.path.join(out, scene + ".dps"), "wb") as f:
            f.write(data)
        backgrounds[scene] = scene + ".dps"
        print(f"{scene}.dps: {len(data)} bytes")

    room = {
        "format": 1,
        "id": rid,
        "name": a.name[:24],
        "layout": {"groundY": a.ground, "bed": a.bed, "bowl": a.bowl, "door": a.door,
                   "yardDoor": a.yard_door, "tree": a.tree},
        "backgrounds": backgrounds,
    }
    with open(os.path.join(out, "room.json"), "w") as f:
        json.dump(room, f, indent=2)
        f.write("\n")
    print("wrote", out)
    print("preview: ./build/deskpet_headless --preview blobby --room", rid)


if __name__ == "__main__":
    main()
