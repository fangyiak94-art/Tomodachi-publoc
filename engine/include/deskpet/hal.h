// Hardware abstraction layer. The engine talks to the outside world only
// through these interfaces; each target (PC simulator, headless tests,
// ESP32-C3 board, later ESP32-S3 / WebAssembly) provides implementations.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "deskpet/types.h"

namespace dp {

// A 240x240 RGB565 panel. The engine only ever pushes small rectangles.
class Display {
 public:
  virtual ~Display() = default;
  virtual void pushPixels(const Rect& area, const Color* pixels) = 0;
  // 0..255. Used to dim the screen while the pet sleeps.
  virtual void setBrightness(uint8_t level) = 0;
};

enum class Gesture : uint8_t {
  None,
  Tap,
  DoubleTap,
  LongPress,
  SwipeLeft,   // finger moves right-to-left
  SwipeRight,  // finger moves left-to-right
  SwipeUp,
  SwipeDown,
  Back,  // BOOT button
};

const char* gestureName(Gesture g);

struct InputEvent {
  Gesture gesture = Gesture::None;
  int16_t x = kScreenW / 2;
  int16_t y = kScreenH / 2;
};

// Raw input. The engine runs its own gesture recognizer on top of raw touch
// points so behaviour is identical on every target.
class Input {
 public:
  virtual ~Input() = default;
  // Returns true while a finger is down, with screen coordinates.
  virtual bool readTouch(int16_t& x, int16_t& y) = 0;
  // Returns true while the BOOT (back) button is held.
  virtual bool readBack() = 0;
  // Optional: already-recognised gestures (keyboard shortcuts in the
  // simulator, hardware gestures from the touch chip, scripted tests).
  virtual bool pollGesture(InputEvent& out) {
    (void)out;
    return false;
  }
};

class Clock {
 public:
  virtual ~Clock() = default;
  virtual uint32_t millis() = 0;
  // Unix seconds, or 0 when the time is not known yet (no NTP / phone sync).
  virtual int64_t epoch() = 0;
  // Minutes east of UTC for showing local times.
  virtual int32_t utcOffsetMinutes() { return 0; }
};

// Flat file access rooted at the pack filesystem (LittleFS on the board,
// a folder on the PC). Paths are absolute, e.g. "/packs/blobby/manifest.json".
class Storage {
 public:
  virtual ~Storage() = default;
  virtual bool read(const char* path, std::string& out, size_t maxBytes) = 0;
  virtual bool write(const char* path, const std::string& data) = 0;
  virtual bool listDirs(const char* path, std::vector<std::string>& out) = 0;
};

// One square-wave voice (piezo buzzer on v1). freqHz == 0 means silence.
class Buzzer {
 public:
  virtual ~Buzzer() = default;
  virtual void tone(uint16_t freqHz) = 0;
};

struct CalendarEvent {
  std::string id;
  std::string title;
  int64_t start = 0;  // unix seconds
  int64_t end = 0;
};

class CalendarSource {
 public:
  virtual ~CalendarSource() = default;
  // Called every loop. The source decides when to actually fetch (and when
  // to power the radio). Returns true when `out` was refreshed.
  virtual bool poll(std::vector<CalendarEvent>& out) = 0;
};

struct Notification {
  std::string app;
  std::string title;
  std::string body;
};

class NotificationSource {
 public:
  virtual ~NotificationSource() = default;
  virtual bool poll(Notification& out) = 0;
};

// Optional: a target that can receive packs (Wi-Fi portal, Web Serial...).
class PackUploader {
 public:
  virtual ~PackUploader() = default;
  virtual void start() = 0;
  virtual void stop() = 0;
  virtual void loop() {}
  // Up to 3 short lines shown on the upload screen.
  virtual std::vector<std::string> statusLines() = 0;
};

struct ConnectivityStatus {
  const char* wifi = "off";
  const char* phone = "--";
};

// Everything a target hands to the engine. Optional parts may be null.
struct Platform {
  Display* display = nullptr;
  Input* input = nullptr;
  Clock* clock = nullptr;
  Storage* storage = nullptr;
  Buzzer* buzzer = nullptr;                    // optional
  CalendarSource* calendar = nullptr;          // optional
  NotificationSource* notifications = nullptr; // optional
  PackUploader* uploader = nullptr;            // optional
  void (*log)(const char* line) = nullptr;     // optional
  ConnectivityStatus (*connectivity)() = nullptr;  // optional
  uint32_t seed = 1;
};

}  // namespace dp
