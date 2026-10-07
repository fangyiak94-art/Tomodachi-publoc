# Architecture and hardware migration

## The rule that makes migration work

The engine (`engine/`) never includes a platform header. It sees the world
only through the interfaces in `engine/include/deskpet/hal.h`. Each target
implements those interfaces, and nothing else in the engine changes between
targets.

```
            ┌─────────────────────────── engine/ (portable C++17) ───────────────────────────┐
            │ Pet (stats, mood) · World (goals, autonomy) · AlertCenter (priority rules)     │
            │ Pet packs + room themes (validated) · Ambience (time/weather/lights)           │
            │ UI screens · Portal page + rules · Gesture recognizer · Strip renderer         │
            └───────────────▲──────────────── hal.h interfaces ───────────────▲──────────────┘
                            │                                                 │
   platform/pc (+sim)       │                     platform/esp32              │   future: esp32s3, wasm
   FramebufferDisplay ──────┤ Display             LcdDisplay (LovyanGFX GC9A01)
   SDL mouse / script ──────┤ Input               TouchInput (CST816) + BOOT pin
   SDL_GetTicks/time() ─────┤ Clock               millis() + NTP / Chronos time
   DirStorage (./fs) ───────┤ Storage             LittleFsStorage
   SDL square wave ─────────┤ Buzzer              PiezoBuzzer (tone())
   FileCalendar (mock) ─────┤ CalendarSource      WifiCalendar (Apps Script, radio on only to poll)
   key "n" queue ───────────┤ NotificationSource  Chronos BLE / serial console
   key "w" / mock file ─────┤ WeatherSource       "weather" in the Apps Script feed
   HttpPortal (localhost) ──┤ PackUploader        WifiPortal (hotspot, WebServer)
```

| HAL interface | Contract |
|---|---|
| `Display::pushPixels(rect, rgb565)` | Blit a small rectangle. Never asked for more than a 240×16 strip at a time. |
| `Display::setBrightness(0..255)` | Backlight. The engine dims while the pet sleeps. |
| `Input::readTouch / readBack` | Raw finger position and BOOT level. The engine recognizes gestures, so they feel the same everywhere. |
| `Input::pollGesture` | Optional ready-made gestures (keyboard, tests, or the touch chip's hardware gestures). |
| `Clock::millis / epoch / utcOffsetMinutes` | `epoch()` returns 0 until time is known. Meeting alerts wait for a valid clock. |
| `Storage::read / write / listDirs` | Absolute paths such as `/packs/blobby/manifest.json`. Reads take a size cap. |
| `Buzzer::tone(hz)` | One square-wave voice. The engine plays RTTTL itself. |
| `CalendarSource::poll` | Called every loop. The source decides when to fetch and powers the radio. |
| `NotificationSource::poll` | Non-blocking. Called every loop. |
| `WeatherSource::poll` | Optional. Rain and snow change the room and keep the pet inside. |
| `PackUploader` | HTTP transport for the portal. The engine passes itself as `PortalHost` (room JSON, theme list, live edit). The page and validation live in the engine (`portal.cpp`). |

## Engine loop

`Engine::loop()` is called as often as the target likes (every 5 ms on the
board, every frame in the simulator):

1. Read input and run the gesture recognizer, then dispatch to the overlay
   or the current screen.
2. Poll the calendar and notification sources, then let `AlertCenter`
   decide what is on top.
3. Run fixed 280 ms behaviour ticks: stat decay (scaled by `timeScale`),
   then `World::tick`.
4. Advance the RTTTL player.
5. Render only the dirty rectangles. Each rectangle is drawn in 16-row
   strips: every layer (scene, pet, HUD, overlay) draws into a 7.7 KB strip
   buffer clipped to that strip, then the strip goes to `pushPixels`.

The pet's rectangle (old ∪ new position, including hops, the heart and Zzz)
is invalidated every tick. The HUD and info screens are invalidated only
when a hash of what they show changes. A scene change or overlay change
redraws the full screen once.

## Memory budget on the ESP32-C3 (400 KB SRAM, no PSRAM)

| Item | Size |
|---|---|
| `Engine` object (mostly the strip buffer) | ~9 KB |
| Pack config + strings | ~1–2 KB |
| Sprite sheet (24×24, 9 frames, 4-bit) | ~2.6 KB; limit 64 KB |
| Room backgrounds, if a room theme ships them | ~29 KB each at 240×240 4-bit; limit 40 KB |
| ArduinoJson documents | transient, a few KB while parsing |
| Wi-Fi + TLS (calendar fetch) | ~50–70 KB, only while polling |
| BLE (Chronos) | ~40–60 KB, always on in the `esp32c3-chronos` build |

On the board, the serial `stats` command prints free heap and the low-water
mark. Check it after the first flash, with and without Chronos.

## Decisions that differ from the v0 requirements doc

1. **No LVGL (for now).** The engine uses its own small strip rasterizer
   (`gfx.cpp`, about 300 lines): rectangles, ellipses, triangles, lines, a
   5×7 font scaled ×2 or more, and 4-bit indexed images. Reasons: LVGL's
   draw buffers and heap compete with Wi-Fi + BLE on the C3 (the doc already
   flags this as tight), the UI is a handful of fixed screens, and a
   dependency-free renderer makes the PC, headless test and future
   WebAssembly builds trivial. LovyanGFX is used on the board only as the
   panel and touch driver. LVGL can be revisited for the v3 S3 with 8 MB
   PSRAM, behind the same `Display` interface.
2. **Arduino-ESP32 via PlatformIO** (open question in the doc). It has the
   fastest bring-up, LovyanGFX/ChronosESP32/LittleFS all target it, and the
   engine doesn't care. Moving to ESP-IDF later means rewriting
   `platform/esp32` only.
3. **Packs are uploaded as files, not a zip.** The portal takes
   `manifest.json` (pets) or `room.json` (rooms) plus `.dps` files for an id. Unzipping on the C3 is
   possible (the ROM has an inflate routine) but not needed yet. The PC
   Pack Studio can still produce zips and unpack them in the browser before
   uploading.
4. **Pack signing (Ed25519) is not implemented yet.** Packs are data only,
   and every parser is bounded and fuzz-smoke-tested, but there is no
   signature check. Do this before any public pack sharing.
5. **Calendar TLS uses `setInsecure()`** in this POC. Pin the Google root CA
   before release. The calendar token lives in `/secrets.json` on LittleFS,
   which is readable by anyone with USB until flash encryption is enabled.

## Migrating to hardware, stage by stage

### Stage 2: virtual board (Wokwi)
- Add `diagram.json` + `wokwi.toml` pointing at `.pio/build/esp32c3/firmware.bin`.
  Wokwi has no GC9A01 part. Either use its ILI9341 with a 240×240 window
  (add `Panel_ILI9341` behind a `DP_WOKWI` flag in `lgfx_board.h`) or
  validate boot, partitions and logic over serial only.
- Wokwi has no capacitive touch. Map push buttons to gestures via
  `Input::pollGesture`. The engine is already built for injected gestures.
- Gate: boots, renders, `partitions.csv` fits, LittleFS mounts.

### Stage 3: v0 on the real board
1. Flash the seller's demo first to confirm pins, then compare with
   `platform/esp32/board_pins.h`.
2. `pio run -e esp32c3 -t upload`, then `-t uploadfs`.
3. Check, in order: backlight (pin 3), colours (if they look inverted,
   toggle `cfg.invert`, and swap `rgb_order` if red/blue are swapped),
   orientation (`setRotation`), and touch axes (`offset_rotation` in the
   touch config).
4. Smooth animation gate: watch the walk cycle. If it tears or stutters,
   raise SPI to 80 MHz (already the default) or enable DMA pushes.
5. Run the serial `stats` command and record free heap.

### Stage 4: v1
- Calendar: copy `integrations/secrets.example.json` to `fs/secrets.json`,
  deploy `integrations/apps-script/Code.gs`, then `uploadfs`.
- Notifications: build `esp32c3-chronos` and pair from the Chronos app.
- Packs: Settings › Upload, join the hotspot shown, upload, press BOOT, then
  pick the pack in Settings.
- Add pack signing (see decision 4) and a real libFuzzer target for
  `parseManifest`, `parseImage4` and `parseCalendarJson`.

### v3: ESP32-S3 (Waveshare 1.75" AMOLED, 466×466)
- New `platform/esp32s3` with a CO5300 driver and CST9217 touch.
- The engine has a fixed 240×240 logical screen (`kScreenW/H`). For 466×466,
  either render at 240 and upscale ×2 in `pushPixels` (fast, chunky), or
  make the screen size a runtime value. Layout constants live in
  `engine/src/layout.h`, `render.cpp` and `ui.cpp`.
- With PSRAM, a full framebuffer becomes affordable. The strip renderer
  still works unchanged.
- Audio files and the mic go behind new HAL interfaces. Packs already
  carry named sounds.

### WebAssembly (stage 5)
`platform/sim/main_sdl.cpp` only needs `emscripten_set_main_loop` instead of
the `while` loop, and Emscripten's SDL2 port (`-sUSE_SDL=2`). The engine
itself has no OS dependencies. This is not built yet.
