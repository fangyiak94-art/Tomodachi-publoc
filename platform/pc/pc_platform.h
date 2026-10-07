// HAL implementations shared by the PC targets (SDL simulator, headless
// runner, unit tests). The board implements the same interfaces in
// platform/esp32.
#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "deskpet/hal.h"

namespace dp {
namespace pc {

// Keeps a full 240x240 framebuffer (fine on a PC) and counts pushed pixels.
class FramebufferDisplay : public Display {
 public:
  FramebufferDisplay();
  void pushPixels(const Rect& area, const Color* pixels) override;
  void setBrightness(uint8_t level) override { brightness = level; }
  bool savePpm(const std::string& path, bool roundMask = true) const;

  std::vector<Color> fb;
  uint8_t brightness = 255;
  uint64_t pushedPixels = 0;
  uint32_t pushes = 0;
  bool dirty = false;
};

// Files under a host directory, e.g. ./fs (the same tree PlatformIO uploads
// to LittleFS).
class DirStorage : public Storage {
 public:
  explicit DirStorage(std::string root) : root_(std::move(root)) {}
  bool read(const char* path, std::string& out, size_t maxBytes) override;
  bool write(const char* path, const std::string& data) override;
  bool listDirs(const char* path, std::vector<std::string>& out) override;
  // Writes go here instead of the pack tree when set (keeps fs/ clean).
  void setWriteRoot(std::string r) { writeRoot_ = std::move(r); }

 private:
  std::string resolve(const char* path, bool forWrite) const;
  std::string root_;
  std::string writeRoot_;
};

class ManualClock : public Clock {
 public:
  uint32_t millis() override { return ms; }
  int64_t epoch() override { return epochBase > 0 ? epochBase + ms / 1000 : 0; }
  int32_t utcOffsetMinutes() override { return offsetMin; }
  uint32_t ms = 0;
  int64_t epochBase = 0;
  int32_t offsetMin = 0;
};

class ScriptInput : public Input {
 public:
  bool readTouch(int16_t& x, int16_t& y) override {
    x = tx;
    y = ty;
    return down;
  }
  bool readBack() override { return back; }
  bool pollGesture(InputEvent& out) override {
    if (queue.empty()) return false;
    out = queue.front();
    queue.pop_front();
    return true;
  }
  void push(Gesture g, int16_t x = kScreenW / 2, int16_t y = kScreenH / 2) {
    queue.push_back({g, x, y});
  }
  std::deque<InputEvent> queue;
  bool down = false, back = false;
  int16_t tx = 0, ty = 0;
};

class MockWeather;

// Reads the same JSON the Apps Script returns from a file, re-reading it
// every `intervalMs` so it can be edited while the simulator runs. Events
// may use "startIn"/"endIn" (seconds from now) instead of absolute times.
class FileCalendar : public CalendarSource {
 public:
  FileCalendar(std::string path, Clock* clock, uint32_t intervalMs = 60000)
      : path_(std::move(path)), clock_(clock), interval_(intervalMs) {}
  bool poll(std::vector<CalendarEvent>& out) override;
  void addEvent(const CalendarEvent& e) { extra_.push_back(e); force_ = true; }
  // The mock file may carry {"weather": "rain"} like the real feed.
  void setWeatherSink(MockWeather* w) { weather_ = w; }

 private:
  std::string path_;
  Clock* clock_;
  uint32_t interval_;
  uint32_t last_ = 0;
  bool first_ = true, force_ = false;
  int64_t base_ = 0;
  std::vector<CalendarEvent> extra_;
  MockWeather* weather_ = nullptr;
  std::string lastWeather_;
};

class MockWeather : public WeatherSource {
 public:
  bool poll(Weather& out) override {
    if (!changed) return false;
    changed = false;
    out = value;
    return true;
  }
  void set(Weather w) {
    value = w;
    changed = true;
  }
  Weather value = Weather::Unknown;
  bool changed = false;
};

class QueueNotifications : public NotificationSource {
 public:
  bool poll(Notification& out) override {
    if (queue.empty()) return false;
    out = queue.front();
    queue.pop_front();
    return true;
  }
  std::deque<Notification> queue;
};

class SilentBuzzer : public Buzzer {
 public:
  void tone(uint16_t f) override {
    freq = f;
    if (f) ++notes;
  }
  uint16_t freq = 0;
  uint32_t notes = 0;
};

}  // namespace pc
}  // namespace dp
