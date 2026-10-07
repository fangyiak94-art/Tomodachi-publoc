# Pack format (format 1)

A pack is a folder `/packs/<id>/` on LittleFS (`fs/packs/<id>/` in this repo).
It holds data only. No scripts.

```
packs/sprout/
  manifest.json     required, ≤ 16 KB
  sprout.dps        optional sprite sheet, ≤ 64 KB
  house.dps         optional scene backgrounds, ≤ 40 KB each
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
                 { "level": 5, "name": "Big Blobby", "body": "#5CC48C" } ],   // max 8
  "scenes": {
    "house": { "background": "house.dps", "groundY": 182, "minX": 56, "maxX": 184,
               "spots": { "bed": [62, 182], "bowl": [150, 182], "door": [184, 182] } },
    "yard":  { "spots": { "door": [176, 182] } }
  },
  "sounds": { "meeting": "meeting:d=8,o=6,b=180:c,e,g", "notify": "...", "happy": "..." }  // RTTTL
}
```

Required spots: `house.bed`, `house.bowl`, and `door` in both scenes. Spots
are where the pet stands (bottom centre). The bowl is drawn 34 px to the
left of its spot.

Mood names: `hungry`, `sad`, `happy`, `neutral` (`sleepy` is reserved for
sleep).

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
header exactly. A 240×240 background is 28,844 bytes. A 120×120 background
is drawn at 2× (7,244 bytes).

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
- required spots are missing, or there are too many spots, rules, stages,
  animations or sounds
