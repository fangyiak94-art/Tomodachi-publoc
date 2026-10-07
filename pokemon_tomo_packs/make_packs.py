#!/usr/bin/env python3
"""Build Pokemon pet packs for your own Desk Pet.

Downloads each Pokemon's animated sprite (PokeAPI sprite collection on
GitHub) and converts it with tools/image2pack.py into packs/<id>/.
Only the list and this script are in git. The images and generated packs
stay on your machine (see .gitignore), because the characters are
Nintendo/Game Freak's.

    python3 pokemon_tomo_packs/make_packs.py              # everything in pokemon.json
    python3 pokemon_tomo_packs/make_packs.py gengar       # just some ids
    python3 pokemon_tomo_packs/make_packs.py --list

Then:
    ./build/deskpet_sim --packs pokemon_tomo_packs/packs --pack gengar --room haunted
    python3 tools/stage_fs.py --packs pokemon_tomo_packs/packs --only gengar   # for the board

Add more Pokemon by adding lines to pokemon.json.
"""
import argparse
import json
import os
import subprocess
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SPRITES = "https://raw.githubusercontent.com/PokeAPI/sprites/master/sprites/pokemon"


def urls(dex, shiny):
    s = "shiny/" if shiny else ""
    return [
        f"{SPRITES}/versions/generation-v/black-white/animated/{s}{dex}.gif",  # animated, dex 1-649
        f"{SPRITES}/{s}{dex}.png",                                             # still, any dex
    ]


def download(entry):
    cache = os.path.join(HERE, "cache")
    os.makedirs(cache, exist_ok=True)
    for url in urls(entry["dex"], entry.get("shiny", False)):
        ext = url.rsplit(".", 1)[1]
        path = os.path.join(cache, f"{entry['id']}.{ext}")
        if os.path.exists(path):
            return path
        try:
            with urllib.request.urlopen(url, timeout=20) as r:
                data = r.read()
        except Exception as e:  # try the next source
            print(f"  {url}: {e}")
            continue
        with open(path, "wb") as f:
            f.write(data)
        return path
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("ids", nargs="*", help="ids from pokemon.json (default: all)")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--size", default="48", help="frame size passed to image2pack (default 48)")
    a = ap.parse_args()

    entries = json.load(open(os.path.join(HERE, "pokemon.json")))["pokemon"]
    if a.list:
        for e in entries:
            print(f"{e['id']:14} {e['name']:14} #{e['dex']}  room: {e.get('room', '-')}")
        return
    wanted = set(a.ids) if a.ids else {e["id"] for e in entries}
    unknown = wanted - {e["id"] for e in entries}
    if unknown:
        sys.exit(f"not in pokemon.json: {', '.join(sorted(unknown))}")

    out = os.path.join(HERE, "packs")
    failed = []
    for e in entries:
        if e["id"] not in wanted:
            continue
        print(f"{e['name']}:")
        src = download(e)
        if not src:
            failed.append(e["id"])
            continue
        r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "image2pack.py"), src,
                            "--name", e["name"], "--id", e["id"], "--size", a.size, "--out", out],
                           capture_output=True, text=True)
        print("  " + (r.stdout.strip().splitlines()[0] if r.returncode == 0 else r.stderr.strip()))
        if r.returncode != 0:
            failed.append(e["id"])
    if failed:
        sys.exit(f"failed: {', '.join(failed)}")
    print(f"\nPacks are in {out}")
    print("Run: ./build/deskpet_sim --packs pokemon_tomo_packs/packs --pack gengar --room haunted")


if __name__ == "__main__":
    main()
