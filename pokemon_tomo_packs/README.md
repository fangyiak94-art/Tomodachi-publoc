# pokemon_tomo_packs

Pokémon pets for your own Desk Pet. The repo only holds the **list**
(`pokemon.json`) and the **builder** (`make_packs.py`). The sprites are
downloaded and converted on your machine into `packs/`, which git ignores.
The characters belong to Nintendo / Game Freak, so they never go on GitHub
or into a shared pack store (see "Security and IP" in the requirements doc).

```bash
pip install pillow
python3 pokemon_tomo_packs/make_packs.py            # gastly-line + Gastly, Haunter, Gengar, Shiny Gengar
python3 pokemon_tomo_packs/make_packs.py --list
```

## Evolution: gastly-line

`gastly-line` is one pet that grows: **Gastly → Haunter (Lv 5) → Gengar
(Lv 10)**. Level up with Rare Candy:
- Press `c` in the simulator (or type `candy` in the board's serial
  console) to get one. You also earn them from the first feed of each day
  and every 2nd meeting you acknowledge.
- Swipe ← to Stats and tap the pink **USE** button. 4 candies take Gastly
  from Lv 1 to Lv 5.
- Watch "What? Gastly is evolving!". Press BOOT (Esc) during it to stop it.

Record it without a window:

```bash
./build/deskpet_headless --packs pokemon_tomo_packs/packs --preview gastly-line --candy 9 --room haunted
# shots/evo_000.ppm ... one frame per tick of both evolutions
```

Make your own family by adding `"evolves"` to an entry in `pokemon.json`:

```json
{ "id": "charmander-line", "name": "Charmander", "dex": 4,
  "evolves": [ { "level": 6, "name": "Charmeleon", "dex": 5 },
               { "level": 12, "name": "Charizard", "dex": 6 } ] }
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
