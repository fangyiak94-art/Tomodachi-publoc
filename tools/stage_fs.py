#!/usr/bin/env python3
"""Assemble the LittleFS image folder for one device: fs/ plus the pets you
choose from other folders, optionally preselecting the starting pet/room.

    python3 tools/stage_fs.py --packs pokemon_tomo_packs/packs --only gengar,haunter \\
        --pack gengar --room haunted
    PLATFORMIO_DATA_DIR=build/fs pio run -e esp32c3 -t uploadfs

Warning: preselecting writes settings.json, which replaces the device's
saved settings (brightness, DND) when you upload. The pet's progress
(save.json) is never included.
"""
import argparse
import json
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIMIT = 0x260000  # LittleFS partition in partitions.csv


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--packs", action="append", default=[], help="extra folder of pets (repeatable)")
    ap.add_argument("--only", help="comma-separated pet ids to take from --packs (default: all)")
    ap.add_argument("--pack", help="pet to start with")
    ap.add_argument("--room", help="room to start with")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "fs"))
    a = ap.parse_args()

    if os.path.exists(a.out):
        shutil.rmtree(a.out)
    shutil.copytree(os.path.join(ROOT, "fs"), a.out,
                    ignore=shutil.ignore_patterns("settings.json", "save.json"))

    only = set(a.only.split(",")) if a.only else None
    added = []
    for folder in a.packs:
        for pid in sorted(os.listdir(folder)):
            src = os.path.join(folder, pid)
            if not os.path.isfile(os.path.join(src, "manifest.json")):
                continue
            if only is not None and pid not in only:
                continue
            dst = os.path.join(a.out, "packs", pid)
            if os.path.exists(dst):
                shutil.rmtree(dst)
            shutil.copytree(src, dst)
            added.append(pid)
    if only:
        missing = only - set(added)
        if missing:
            sys.exit(f"not found in --packs: {', '.join(sorted(missing))}")

    if a.pack or a.room:
        packs = os.listdir(os.path.join(a.out, "packs"))
        rooms = os.listdir(os.path.join(a.out, "rooms")) + ["cozy"]
        if a.pack and a.pack not in packs:
            sys.exit(f"--pack {a.pack}: not in the image")
        if a.room and a.room not in rooms:
            sys.exit(f"--room {a.room}: not in the image")
        settings = {"pack": a.pack or "blobby", "room": a.room or "cozy", "brightness": 200, "dnd": False}
        with open(os.path.join(a.out, "settings.json"), "w") as f:
            json.dump(settings, f)

    size = sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(a.out) for f in fs)
    print(f"{a.out}: {size // 1024} KB of {LIMIT // 1024} KB" + (f", added {', '.join(added)}" if added else ""))
    if size > LIMIT * 0.9:
        sys.exit("too big for the LittleFS partition (keep under ~90%)")
    print("Upload: PLATFORMIO_DATA_DIR=" + os.path.relpath(a.out, ROOT) + " pio run -e esp32c3 -t uploadfs")


if __name__ == "__main__":
    main()
