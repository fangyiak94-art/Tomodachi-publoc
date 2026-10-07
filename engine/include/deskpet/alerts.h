// Meeting alerts and phone notifications, with the v0 priority rules:
//   1. Meeting alert: bypasses DND, wakes the pet; ignoring it costs fun.
//   2. Notification: muted by DND, queued while the pet sleeps, held behind
//      a meeting alert.
//   3. Pet idle.
#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "deskpet/hal.h"

namespace dp {

// Parses the Apps Script feed: {"events":[{"id","title","start","end"}]}
// with start/end in unix seconds. Bounded: at most 16 events kept.
bool parseCalendarJson(const char* data, size_t len, std::vector<CalendarEvent>& out);
// The same feed may carry "weather": "clear|cloudy|rain|snow".
bool parseWeatherJson(const char* data, size_t len, Weather& out);

enum class AlertKind : uint8_t { None, Meeting, Notification };

struct AlertSignals {
  bool changed = false;          // overlay appeared / disappeared
  bool meetingRaised = false;    // play sound, wake pet
  bool meetingIgnored = false;   // pet loses fun
  bool notificationShown = false;
};

class AlertCenter {
 public:
  static constexpr int64_t kLeadSeconds = 10 * 60;
  static constexpr int64_t kSnoozeSeconds = 5 * 60;
  static constexpr uint32_t kNotificationMs = 5000;
  static constexpr int64_t kIgnoreAfterSeconds = 120;
  static constexpr size_t kMaxQueue = 8;

  void setEvents(const std::vector<CalendarEvent>& events);
  const std::vector<CalendarEvent>& events() const { return events_; }
  // Next `n` events that have not ended yet.
  std::vector<CalendarEvent> upcoming(int64_t now, size_t n) const;

  // Returns false when muted by DND (dropped, counted in missed()).
  bool pushNotification(const Notification& n, bool dnd);

  AlertSignals update(int64_t now, uint32_t ms, bool petAsleep);

  void ack();      // tap: got it
  void snooze(int64_t now);
  void dismiss();  // BOOT

  AlertKind active() const { return active_; }
  const CalendarEvent& meeting() const { return meeting_; }
  const Notification& notification() const { return notif_; }
  uint32_t missed() const { return missed_; }
  size_t queued() const { return queue_.size(); }

 private:
  struct Tracked {
    std::string key;
    int64_t remindAt;  // 0 = done
  };
  Tracked* track(const CalendarEvent& e);

  std::vector<CalendarEvent> events_;
  std::vector<Tracked> tracked_;
  std::deque<Notification> queue_;
  AlertKind active_ = AlertKind::None;
  CalendarEvent meeting_;
  int64_t meetingShownAt_ = 0;
  Notification notif_;
  uint32_t notifShownMs_ = 0;
  uint32_t missed_ = 0;
  bool pendingChange_ = false;
};

// Plays RTTTL ("name:d=4,o=5,b=120:c,e,g") on a Buzzer, one note per update.
class TunePlayer {
 public:
  void play(const char* rtttl, uint32_t nowMs);
  void stop(Buzzer* b);
  void update(Buzzer* b, uint32_t nowMs);
  bool playing() const { return pos_ < notes_.size(); }

  struct Note {
    uint16_t freq;
    uint16_t ms;
  };
  static bool parse(const char* rtttl, std::vector<Note>& out);

 private:
  std::vector<Note> notes_;
  size_t pos_ = 0;
  uint32_t noteEnd_ = 0;
  bool started_ = false;
};

}  // namespace dp
