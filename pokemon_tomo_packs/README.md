# pokemon_tomo_packs

Pokémon pets for your own Desk Pet. The repo only holds the **list**
(`pokemon.json`) and the **builder** (`make_packs.py`). The sprites are
downloaded and converted on your machine into `packs/`, which git ignores.
The characters belong to Nintendo / Game Freak, so they never go on GitHub
or into a shared pack store (see "Security and IP" in the requirements doc).

```bash
pip install pillow
python3 pokemon_tomo_packs/make_packs.py            # Gastly, Haunter, Gengar, Shiny Gengar
python3 pokemon_tomo_packs/make_packs.py --list
```

## Run a specific one in the simulator

```bash
./build/deskpet_sim --packs pokemon_tomo_packs/packs --pack gengar --room haunted
./build/deskpet_sim --packs pokemon_tomo_packs/packs --pack haunter
```

`--packs` adds a folder of pets (you can repeat it for several folders).
`--pack` picks the one to start with. All of them also show up in Settings ›
Pet, so you can switch while it runs.

Preview without a window (writes screenshots, including 16 animation frames):

```bash
./build/deskpet_headless --packs pokemon_tomo_packs/packs --preview gengar --room haunted
```

## Put one on the board

```bash
python3 tools/stage_fs.py --packs pokemon_tomo_packs/packs --only gengar --pack gengar --room haunted
PLATFORMIO_DATA_DIR=build/fs pio run -e esp32c3 -t uploadfs
```

`stage_fs.py` copies `fs/` plus only the pets you list into `build/fs`, and
can preselect the starting pet and room. That way you can flash a different
set of Pokémon to different devices. Flash is 2.4 MB for packs, and each
Pokémon here is about 30 KB.

Or upload over Wi-Fi: Settings › UPLOAD on the device, then choose "Pet
pack" with id `gengar`, and select `manifest.json` and `sprites.dps` from
`packs/gengar/`.

## Add more

Add a line to `pokemon.json` with an `id` (a-z, 0-9, -), a `name` and the
National Dex number `dex`. Add `"shiny": true` for the shiny sprite. Animated
sprites exist for #1–649. Newer Pokémon get a still picture with
bob/squash animation.
