# Hardware bring-up (DIYMORE ESP32-C3 round board)

## Before buying or plugging in
- The module is an ESP32-C3-MINI-1U, which has **no built-in antenna**.
  Attach the u.FL 2.4 GHz antenna before using Wi-Fi or BLE.
- LiPo: JST 1.25 mm 2-pin, with a protection circuit. Check polarity (red
  to +) before plugging in. Ask the seller whether USB-C charges it.
- Buzzer (optional): wire a piezo to a free GPIO on the SH1.0-4P port. Then
  build with `-DDP_PIN_BUZZER=<pin>`, added to `build_flags` in
  `platformio.ini`.

## First flash
```bash
pip install platformio
pio run -e esp32c3 -t upload       # if it doesn't connect: hold BOOT, tap RESET, release BOOT
pio run -e esp32c3 -t uploadfs     # writes fs/ (packs, secrets.json) to LittleFS
pio device monitor
```

Pins live in `platform/esp32/board_pins.h`. They come from the requirements
doc (SCLK 6, MOSI 7, CS 10, DC 2, BL 3, touch SDA 4, SCL 5) and must be
checked against the seller's demo. The display reset (-1) and touch INT/RST
(0/1) pins are assumptions about this clone.

### If the screen is wrong
| Symptom | Fix in `lgfx_board.h` |
|---|---|
| black screen, backlight on | pins or SPI mode; try `freq_write = 40000000` |
| colours inverted | `cfg.invert = false` |
| red and blue swapped | `cfg.rgb_order = true` |
| image rotated or mirrored | `lcd.setRotation(n)` in `main.cpp`, `offset_rotation` |
| touch axes wrong | touch `offset_rotation`, or `x_min`/`x_max` swapped |

## Serial console (no phone or calendar needed)
```
time 1791363600            set the clock (unix seconds)
meeting 11 Standup         a meeting in 11 minutes, so the alert shows in 1
notify WhatsApp|Ali|Lunch? a phone notification
weather rain               change the room's weather (clear|cloudy|rain|snow)
edit                       open the room editor hotspot
speed 600                  make pet time run 600x
stats                      pet JSON + free heap
```

## Calendar
1. Deploy `integrations/apps-script/Code.gs` (instructions are in the file).
2. `cp integrations/secrets.example.json fs/secrets.json` and fill it in.
   The file is gitignored.
3. `pio run -e esp32c3 -t uploadfs`.

The device joins Wi-Fi every 5 minutes, syncs NTP on the first join, fetches
the feed, and turns Wi-Fi off again.

## Phone notifications
Build `pio run -e esp32c3-chronos -t upload`, install Chronos on Android,
and pair with "DeskPet".

## Room editor and uploads over Wi-Fi
On the device, open Settings › UPLOAD. Join the `DeskPet-XXXX` hotspot with
the password shown, then open http://192.168.4.1. Edit the room live, or
upload pet packs and room themes. Press BOOT to finish (the hotspot turns
off), then pick new packs and rooms in Settings. See [ROOMS.md](ROOMS.md).
