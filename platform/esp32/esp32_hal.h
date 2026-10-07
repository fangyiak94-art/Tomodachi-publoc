// HAL implementations for the ESP32-C3 round board (Arduino-ESP32).
#pragma once
#include <Arduino.h>

#include <deque>
#include <string>
#include <vector>

#include "deskpet/hal.h"

class LGFX;

namespace board {

class LcdDisplay : public dp::Display {
 public:
  explicit LcdDisplay(LGFX& lcd) : lcd_(lcd) {}
  void pushPixels(const dp::Rect& area, const dp::Color* pixels) override;
  void setBrightness(uint8_t level) override;

 private:
  LGFX& lcd_;
};

class TouchInput : public dp::Input {
 public:
  explicit TouchInput(LGFX& lcd) : lcd_(lcd) {}
  bool readTouch(int16_t& x, int16_t& y) override;
  bool readBack() override;

 private:
  LGFX& lcd_;
};

class SystemClock : public dp::Clock {
 public:
  uint32_t millis() override { return ::millis(); }
  int64_t epoch() override;
  int32_t utcOffsetMinutes() override { return offsetMin; }
  int32_t offsetMin = 8 * 60;
};

class LittleFsStorage : public dp::Storage {
 public:
  bool read(const char* path, std::string& out, size_t maxBytes) override;
  bool write(const char* path, const std::string& data) override;
  bool listDirs(const char* path, std::vector<std::string>& out) override;
};

class PiezoBuzzer : public dp::Buzzer {
 public:
  explicit PiezoBuzzer(int pin) : pin_(pin) {}
  void tone(uint16_t freqHz) override;

 private:
  int pin_;
};

struct Secrets {
  String wifiSsid, wifiPass, calendarUrl;
  int32_t utcOffsetMinutes = 8 * 60;
  bool load();  // from /secrets.json on LittleFS
};

// Polls the Apps Script feed every few minutes. Wi-Fi is only switched on for
// the fetch (and NTP on first connect), then turned off again.
class WifiCalendar : public dp::CalendarSource {
 public:
  explicit WifiCalendar(const Secrets& s) : s_(s) {}
  bool poll(std::vector<dp::CalendarEvent>& out) override;
  void addLocal(const dp::CalendarEvent& e) {
    local_.push_back(e);
    dirty_ = true;
  }
  const char* status() const;
  void setPaused(bool p) { paused_ = p; }
  // Weather that came with the last feed ({"weather": "rain"}).
  bool takeWeather(dp::Weather& out);
  void setWeather(dp::Weather w) {  // serial console
    weather_ = w;
    weatherNew_ = true;
  }

  static constexpr uint32_t kIntervalMs = 5u * 60u * 1000u;
  static constexpr uint32_t kConnectTimeoutMs = 15000;

 private:
  enum class State { Idle, Connecting, Fetching };
  bool fetch(std::vector<dp::CalendarEvent>& out);
  void radioOff();
  const Secrets& s_;
  State state_ = State::Idle;
  uint32_t lastFetch_ = 0, stateSince_ = 0;
  bool first_ = true, dirty_ = false, ntpStarted_ = false, paused_ = false;
  std::vector<dp::CalendarEvent> remote_, local_;
  dp::Weather weather_ = dp::Weather::Unknown;
  bool weatherNew_ = false;
};

class FeedWeather : public dp::WeatherSource {
 public:
  explicit FeedWeather(WifiCalendar& c) : c_(c) {}
  bool poll(dp::Weather& out) override { return c_.takeWeather(out); }

 private:
  WifiCalendar& c_;
};

class NotificationQueue : public dp::NotificationSource {
 public:
  bool poll(dp::Notification& out) override;
  void push(const dp::Notification& n);

 private:
  std::deque<dp::Notification> q_;
  portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};

// Wi-Fi hotspot "DeskPet-XXXX" serving the room editor and uploads at
// 192.168.4.1. Runs only while the portal is open (Upload screen / EDIT).
class WifiPortal : public dp::PackUploader {
 public:
  explicit WifiPortal(WifiCalendar& cal) : cal_(cal) {}
  void start(dp::PortalHost& host) override;
  void stop() override;
  void loop() override;
  std::vector<std::string> statusLines() override;

 private:
  WifiCalendar& cal_;
  bool running_ = false;
  String ssid_, pass_;
};

}  // namespace board
