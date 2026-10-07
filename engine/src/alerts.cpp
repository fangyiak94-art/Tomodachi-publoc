#include "deskpet/alerts.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace dp {

bool parseCalendarJson(const char* data, size_t len, std::vector<CalendarEvent>& out) {
  if (!data || len == 0 || len > 16 * 1024) return false;
  JsonDocument doc;
  if (deserializeJson(doc, data, len, DeserializationOption::NestingLimit(4))) return false;
  JsonArrayConst arr = doc["events"];
  if (arr.isNull()) return false;
  std::vector<CalendarEvent> evs;
  for (JsonObjectConst o : arr) {
    if (evs.size() >= 16) break;
    CalendarEvent e;
    e.title = o["title"] | "Meeting";
    if (e.title.size() > 64) e.title.resize(64);
    e.start = o["start"] | int64_t(0);
    e.end = o["end"] | (e.start + 30 * 60);
    e.id = o["id"] | "";
    if (e.id.size() > 64) e.id.resize(64);
    if (e.start <= 0) continue;
    if (e.end < e.start) e.end = e.start;
    evs.push_back(e);
  }
  std::sort(evs.begin(), evs.end(),
            [](const CalendarEvent& a, const CalendarEvent& b) { return a.start < b.start; });
  out = evs;
  return true;
}

static std::string keyOf(const CalendarEvent& e) {
  return e.id + "@" + std::to_string(static_cast<long long>(e.start));
}

AlertCenter::Tracked* AlertCenter::track(const CalendarEvent& e) {
  std::string k = keyOf(e);
  for (Tracked& t : tracked_)
    if (t.key == k) return &t;
  tracked_.push_back({k, e.start - kLeadSeconds});
  return &tracked_.back();
}

void AlertCenter::setEvents(const std::vector<CalendarEvent>& events) {
  events_ = events;
  // Forget tracking for events that disappeared (keeps memory bounded).
  std::vector<Tracked> keep;
  for (const CalendarEvent& e : events_) keep.push_back(*track(e));
  tracked_ = keep;
}

std::vector<CalendarEvent> AlertCenter::upcoming(int64_t now, size_t n) const {
  std::vector<CalendarEvent> out;
  for (const CalendarEvent& e : events_) {
    if (e.end > now) out.push_back(e);
    if (out.size() >= n) break;
  }
  return out;
}

bool AlertCenter::pushNotification(const Notification& n, bool dnd) {
  if (dnd) {
    ++missed_;
    return false;
  }
  if (queue_.size() >= kMaxQueue) queue_.pop_front();
  queue_.push_back(n);
  return true;
}

AlertSignals AlertCenter::update(int64_t now, uint32_t ms, bool petAsleep) {
  AlertSignals sig;
  sig.changed = pendingChange_;
  pendingChange_ = false;

  // 1. Meetings (need a valid clock).
  if (now > 0 && active_ != AlertKind::Meeting) {
    for (const CalendarEvent& e : events_) {
      Tracked* t = track(e);
      if (t->remindAt == 0 || now < t->remindAt || now >= e.end) continue;
      if (active_ == AlertKind::Notification) queue_.push_front(notif_);  // hold it
      active_ = AlertKind::Meeting;
      meeting_ = e;
      meetingShownAt_ = now;
      t->remindAt = 0;
      sig.changed = sig.meetingRaised = true;
      break;
    }
  }
  if (active_ == AlertKind::Meeting) {
    int64_t deadline = std::max(meeting_.start, meetingShownAt_ + kIgnoreAfterSeconds);
    if (now >= deadline) {
      active_ = AlertKind::None;
      sig.changed = sig.meetingIgnored = true;
    }
    return sig;
  }

  // 2. Notifications.
  if (active_ == AlertKind::Notification && ms - notifShownMs_ >= kNotificationMs) {
    active_ = AlertKind::None;
    sig.changed = true;
  }
  if (active_ == AlertKind::None && !petAsleep && !queue_.empty()) {
    notif_ = queue_.front();
    queue_.pop_front();
    active_ = AlertKind::Notification;
    notifShownMs_ = ms;
    sig.changed = sig.notificationShown = true;
  }
  return sig;
}

void AlertCenter::ack() {
  if (active_ == AlertKind::None) return;
  active_ = AlertKind::None;
  pendingChange_ = true;
}

void AlertCenter::snooze(int64_t now) {
  if (active_ != AlertKind::Meeting) return;
  track(meeting_)->remindAt = now + kSnoozeSeconds;
  active_ = AlertKind::None;
  pendingChange_ = true;
}

void AlertCenter::dismiss() { ack(); }

// ---------------------------------------------------------------- RTTTL

bool TunePlayer::parse(const char* s, std::vector<Note>& out) {
  out.clear();
  if (!s) return false;
  const char* p = std::strchr(s, ':');
  if (!p) return false;
  ++p;
  int defDur = 4, defOct = 6, bpm = 63;
  // Defaults section "d=4,o=5,b=120".
  while (*p && *p != ':') {
    char key = static_cast<char>(std::tolower(*p));
    if (p[1] == '=') {
      int v = std::atoi(p + 2);
      if (key == 'd' && v > 0 && v <= 32) defDur = v;
      if (key == 'o' && v >= 3 && v <= 8) defOct = v;
      if (key == 'b' && v >= 20 && v <= 900) bpm = v;
    }
    while (*p && *p != ',' && *p != ':') ++p;
    if (*p == ',') ++p;
  }
  if (*p != ':') return false;
  ++p;
  const uint32_t wholeMs = 60000u * 4 / bpm;
  static const int kSemis[] = {9, 11, 0, 2, 4, 5, 7};  // a b c d e f g
  while (*p && out.size() < 64) {
    while (*p == ' ' || *p == ',') ++p;
    if (!*p) break;
    int dur = 0;
    while (std::isdigit(static_cast<unsigned char>(*p))) dur = dur * 10 + (*p++ - '0');
    if (dur <= 0 || dur > 32) dur = defDur;
    char n = static_cast<char>(std::tolower(*p));
    if (!n) break;
    ++p;
    int semi = -1;
    if (n >= 'a' && n <= 'g') semi = kSemis[n - 'a'];
    else if (n != 'p') return false;
    if (*p == '#') { ++semi; ++p; }
    uint32_t ms = wholeMs / dur;
    if (*p == '.') { ms += ms / 2; ++p; }
    int oct = defOct;
    if (std::isdigit(static_cast<unsigned char>(*p))) oct = *p++ - '0';
    if (*p == '.') { ms += ms / 2; ++p; }
    uint16_t freq = 0;
    if (semi >= 0) {
      // A4 = 440 Hz; midi = 12 * (oct + 1) + semi.
      int midi = 12 * (oct + 1) + semi;
      double f = 440.0;
      int d = midi - 69;
      static const double kSemi = 1.0594630943592953;
      for (; d > 0; --d) f *= kSemi;
      for (; d < 0; ++d) f /= kSemi;
      freq = static_cast<uint16_t>(f + 0.5);
    }
    out.push_back({freq, static_cast<uint16_t>(ms > 4000 ? 4000 : ms)});
    while (*p && *p != ',') ++p;
  }
  return !out.empty();
}

void TunePlayer::play(const char* rtttl, uint32_t nowMs) {
  (void)nowMs;
  if (!parse(rtttl, notes_)) notes_.clear();
  pos_ = 0;
  started_ = false;
}

void TunePlayer::stop(Buzzer* b) {
  notes_.clear();
  pos_ = 0;
  if (b) b->tone(0);
}

void TunePlayer::update(Buzzer* b, uint32_t nowMs) {
  if (pos_ >= notes_.size()) return;
  if (!started_) {
    started_ = true;
    noteEnd_ = nowMs + notes_[pos_].ms;
    if (b) b->tone(notes_[pos_].freq);
    return;
  }
  if (static_cast<int32_t>(nowMs - noteEnd_) < 0) return;
  ++pos_;
  if (pos_ >= notes_.size()) {
    if (b) b->tone(0);
    return;
  }
  noteEnd_ = nowMs + notes_[pos_].ms;
  if (b) b->tone(notes_[pos_].freq);
}

}  // namespace dp
