# Rooms: themes, live changes, and the editor

The house and yard are a **room theme**, separate from the pet. Any pet can
live in any room: Gengar in Haunted, Sprout at the Beach.

## 1. Swappable themes

A theme is a folder `/rooms/<id>/` on LittleFS (`fs/rooms/<id>/` here):

```
rooms/haunted/
  room.json        required, ≤ 4 KB
  house.dps        optional background picture (DPS1, see PACK_FORMAT.md)
  yard.dps         optional
```

Shipped themes: **cozy** (built in, works without files), **haunted** and
**beach**. Switch with the **Room** row in Settings, or from the editor page.

```jsonc
{
  "format": 1,
  "id": "haunted",                     // a-z 0-9 - _ . ; max 24
  "name": "Haunted",
  "colors": {                          // any subset; "#RRGGBB"
    "wall": "#3E3358", "wainscot": "#2C2442", "trim": "#5A4A7C",
    "floor": "#3A2E2A", "plank": "#2A201C", "frame": "#1E1A26",
    "door": "#4A3A2E", "bed": "#2E2438", "blanket": "#6A3C8C", "pillow": "#C8C0D8",
    "bowl": "#7CC46A", "grass": "#4E6A48", "houseWall": "#5C5470",
    "roof": "#2E2638", "leaves": "#3E4E3A", "trunk": "#3A2E26"
  },
  "layout": {                          // x positions where the pet stands; clamped to safe ranges
    "groundY": 182, "window": [92, 40],
    "bed": 64, "bowl": 146, "door": 186, "yardDoor": 172, "tree": 64
  },
  "backgrounds": { "house": "house.dps", "yard": "yard.dps" }   // optional pictures
}
```

### A room from your own pictures

```bash
python3 tools/image2room.py --name "My Den" --house den.png --yard garden.jpg
./build/deskpet_headless --preview blobby --room private-my-den    # screenshots in shots/
```

Each picture is cropped square and reduced to 16 colours: 120×120 drawn at
2× (7 KB), or 240×240 with `--full` (29 KB). Rooms named `private-*` are
gitignored.

## 2. The room changes by itself

| Trigger | What changes |
|---|---|
| Real clock (local time) | Dawn 5–7, day 7–17, dusk 17–19, night. Sky colours, sun or moon and stars, outdoor light, warm lamp light indoors at night, lit house window |
| Weather from the calendar feed (`"weather": "rain"`) | Cloudy: grey sky and more clouds. Rain: falling rain in the yard and on the window, darker light, the pet stays inside and comes home if it is out. Snow: snowflakes, white ground, no flowers |
| Pet asleep | House lights off (dark blue). The pet is dimmed too, but less |

Weather source:
- **Board:** the Apps Script adds `weather` from Open-Meteo (set the `LAT`
  and `LON` script properties). For testing, use the serial command
  `weather rain`.
- **Simulator:** the `w` key, or `"weather"` in `mock/calendar.json`.

Lighting is a per-channel multiplier on the canvas. Scenes, background
pictures and themes need no extra art for day, night or weather.

Animated rain or snow redraws the window area each tick indoors, and the
whole screen in the yard (~12 ms of SPI at 80 MHz). In clear weather the
normal small dirty rectangles apply.

Preview any combination:

```bash
./build/deskpet_headless --preview wisp --room haunted --weather rain --hour 18
```

## 3. Edit the room from a phone or PC

1. On the pet, open Settings › **UPLOAD** (or type `edit` in the serial
   console). On the simulator, use Settings › UPLOAD or start it with
   `--edit`.
2. Connect:
   - **Board:** join the `DeskPet-XXXX` Wi-Fi with the password on screen,
     then open http://192.168.4.1.
   - **Simulator:** open http://localhost:8080.
3. Pick a theme, or drag the layout sliders and colour pickers. Each change
   is sent after a 250 ms pause and **shows on the pet right away**. The pet
   jumps to the home screen and shows a blue **EDIT** badge.
4. Press **BOOT** to finish. This turns the hotspot off.

Edits are saved as the room `custom` (`/rooms/custom/room.json`), so
shipped themes stay untouched. If the edited theme had background pictures,
the custom room keeps using them (`"backgroundsFrom"`).

The same page uploads files: choose "Pet pack" or "Room theme", type an id,
and select the files (`room.json` / `manifest.json` and `.dps`).

### Portal routes (simulator and board serve the same page)

| Route | |
|---|---|
| `GET /` | editor page |
| `GET /room` | active room JSON |
| `POST /room` | body = room JSON. Validated, saved as `custom`, shown live |
| `GET /rooms` | `{"rooms":[...],"active":"id"}` |
| `POST /room/select?id=x` | switch theme |
| `POST /upload?kind=pack\|room&id=x` | multipart file upload |

Everything is validated by the engine (`parseRoom`, `portalUploadPath`):
safe ids and file names only, size limits, and colours and positions
clamped. A bad edit is rejected and the current room stays.
