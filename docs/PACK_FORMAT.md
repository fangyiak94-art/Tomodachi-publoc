# Pet pack format (format 1)

A pet pack is a folder `/packs/<id>/` on LittleFS (`fs/packs/<id>/` in this
repo). It holds data only. No scripts. The house and yard are **room
themes**, separate from the pet; see [ROOMS.md](ROOMS.md).

```
packs/sprout/
  manifest.json     required, ≤ 16 KB
  sprout.dps        optional sprite sheet, ≤ 64 KB
```

## manifest.json

Only `format`, `id` and `name` are required. Everything else falls back to
the v0 spec defaults. Numbers are clamped to sane ranges. Unknown keys are
ignored.

```jsonc
{
  "format": 1,
  "id": "blobby",                 // a-z 0-9 - _ . ; max 24; must match the folder name
  "name": "Blobby",               // max 24 chars
  "version": "1.0.0",
  "creature": {
    "body": "#7ED6A5", "outline": "#2E6B4F", "cheek": "#F59AA8",   // procedural creature colours
    "sprites": "sprout.dps",      // optional; omit for the built-in procedural blob
    "scale": 3,                   // 1-6, on-screen pixel size of a sprite pixel
    "animations": {               // frame indexes into the sprite sheet, max 16 per animation
      "idle": [0], "walk": [1, 2], "sleep": [3], "eat": [4, 5], "play": [6, 0],
      "idle_happy": [6]           // "<anim>_<mood>" overrides "<anim>"
    }
  },
  "decay":   { "awake":  { "food": 10, "fun": 5, "energy": 8 },        // per hour
               "asleep": { "food": 5, "fun": 0, "energyGain": 20 } },
  "actions": { "feed": { "food": 25 }, "pet": { "fun": 5 },
               "play": { "fun": 20, "energyCost": 15, "xp": 5 }, "walk": { "xp": 5 },
               "meetingAck": { "fun": 5, "xp": 10 }, "meetingIgnored": { "funCost": 15 } },
  "mood":    { "hungryBelow": 30, "sadBelow": 35, "happyFunMin": 70, "happyFoodMin": 50, "warnBelow": 30 },
  "behavior":{ "outingEnergyMin": 40, "tiredBelow": 30, "outingMaxTicks": 45,
               "outingChancePct": 2, "autoSleepBelow": 10 },
  "rules": [ { "stat": "food", "below": 15, "mood": "sad" } ],        // checked before the mood table, max 16
  "evolution": [ { "level": 1, "name": "Blobby" },
                 { "level": 5, "name": "Big Blobby", "body": "#5CC48C" },     // procedural: new colour
                 { "level": 10, "name": "Gengar", "sprites": "stage2.dps",    // sprite pets: new sheet
                   "animations": { "idle": [0, 1] } } ],                     // optional, else the pack's; max 8
  "sounds": { "meeting": "meeting:d=8,o=6,b=180:c,e,g", "notify": "...", "happy": "..." }  // RTTTL
}
```

Mood names: `hungry`, `sad`, `happy`, `neutral` (`sleepy` is reserved for
sleep).

## Levels, Rare Candy and evolution

Level = XP ÷ 100 + 1. XP comes from meetings, play and walks. A **Rare
Candy** jumps straight to the next level. Candies are earned with:
- a **daily treat**: the first Feed of each day (needs the clock)
- a **meeting streak**: every 2nd meeting you acknowledge
- `c` in the simulator, or `candy` on the board's serial console

Use them on the Stats screen (pink **xN USE** button).

When the level reaches the next stage's `level`, the evolution scene plays:
"What? X is evolving!", then the old and new forms flash as white
silhouettes, faster and faster, then a white flash and "X evolved into Y!".
The pet's sprite sheet and animations switch to that stage's `sprites`
and `animations`. Press **BOOT** during the first part to stop it, like
pressing B. It tries again at the next level up. The form the pet reached is
saved and survives reboots. Only the current stage's sheet is in RAM
(about 30 KB). Both sheets are loaded briefly during the scene.

Make a family from pictures with
`tools/image2pack.py first.gif --name A --evolve 5 B second.gif --evolve 10 C third.gif`.

## DPS1 image format (.dps)

Little endian:

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `"DPS1"` |
| 4 | 2 | frame width (1-240) |
| 6 | 2 | height (1-240) |
| 8 | 2 | frame count (1-64) |
| 10 | 2 | reserved (0) |
| 12 | 32 | 16 × RGB565 palette |
| 44 | … | frames × ceil(w·h/2) bytes, 2 pixels per byte, high nibble first |

For sprites, palette index 0 is transparent. The file size must match the
header exactly. Room backgrounds use the same format: 240×240 is 28,844
bytes, and 120×120 is drawn at 2× (7,244 bytes).

Convert art with:

```bash
python3 tools/png2dps.py sheet.png out.dps --frame-width 24   # sprite strip
python3 tools/png2dps.py house.png house.dps --background     # opaque scene
```

### From a single picture

```bash
python3 tools/image2pack.py my_creature.png --name "Ghosty"   # -> fs/packs/private-ghosty/
./build/deskpet_headless --preview private-ghosty              # screenshots in shots/
```

`image2pack.py` crops the image, and if there is no transparency it removes
a flat background colour. It fits the image into a 48×48 frame and makes
walk, sleep, eat and happy frames by bobbing, squashing and dimming it. An
animated GIF's frames become the idle loop. Packs whose id starts with
`private-` are gitignored. Use that for art you don't own (fan art,
franchise characters) so it stays on your own machine and device and is
never committed or shared. The shipped **Wisp** pack was made from
`tools/art/wisp.png` with this tool.

`tools/make_sprout_pack.py` is a complete example. It draws frames with
Pillow, converts them, and writes the manifest.

## Validation

The loader rejects the pack, and the engine keeps the previous one, when any
of these hold:
- bad JSON, JSON nested deeper than 6, or a manifest over 16 KB
- `format` is not 1, or `id`/`name` is missing or invalid
- file names are not `[a-z0-9._-]`, start with `.`, or contain `..`
- an image header does not match its size, or exceeds its limits
- an animation references a frame the sheet doesn't have
- there are too many rules, stages, animations or sounds
