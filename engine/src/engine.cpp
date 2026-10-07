#include "deskpet/engine.h"

#include "layout.h"

#include <ArduinoJson.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <ctime>

namespace dp {

const char* screenName(Screen s) {
  switch (s) {
    case Screen::Home: return "home";
    case Screen::Stats: return "stats";
    case Screen::Agenda: return "agenda";
    case Screen::Actions: return "actions";
    case Screen::Settings: return "settings";
    case Screen::Upload: return "upload";
  }
  return "?";
}

Engine::Engine(const Platform& platform) : p_(platform) {
  pack_.cfg = defaultPack();
  room_.cfg = defaultRoom();
  hudRect_ = Rect(70, 8, 100, 50);
}

void Engine::log(const std::string& line) {
  if (p_.log) p_.log(line.c_str());
}

int64_t Engine::now() { return p_.clock ? p_.clock->epoch() : 0; }

Ambience Engine::computeAmbience() {
  Ambience a;
  int64_t t = now();
  if (t > 0) {
    int64_t local = t + int64_t(p_.clock->utcOffsetMinutes()) * 60;
    int hour = static_cast<int>(((local / 3600) % 24 + 24) % 24);
    a.phase = dayPhaseForHour(hour);
  }
  a.weather = weather_;
  a.lightsOff = pet_.asleep();
  a.tick = world_.view().tick;
  return a;
}

// The room changes by itself: time of day, weather, lights off at bedtime.
void Engine::updateAmbience() {
  Ambience a = computeAmbience();
  bool changed = a != amb_;
  amb_ = a;
  world_.setBadWeather(a.precipitation());
  if (screen_ != Screen::Home) return;
  if (changed) {
    invalidateAll();
  } else if (a.precipitation()) {
    if (world_.view().scene == SceneId::Yard) invalidateAll();  // falling rain/snow
    else invalidate(windowBounds(room_.cfg));
  }
}

void Engine::setWeather(Weather w) {
  if (w == weather_) return;
  weather_ = w;
  log(std::string("deskpet: weather ") + weatherName(w));
  updateAmbience();
}

PetLook Engine::look() const {
  PetLook l;
  l.mood = pet_.mood();
  l.body = pack_.cfg.body;
  l.light = sceneTint(amb_, world_.view().scene);
  l.blanket = room_.cfg.colors.blanket;
  if (const EvolutionStage* st = pack_.cfg.stage(pack_.stage))
    if (st->hasBody) l.body = st->body;
  return l;
}

// ------------------------------------------------------------------ setup

void Engine::begin() {
  loadSettings();
  world_.reset(room_.cfg, p_.seed);
  restorePet();
  // Show the form the pet had evolved into (saved), not just stage 0.
  {
    int want = std::min<int>(pet_.stats().stage, static_cast<int>(pack_.cfg.evolution.size()) - 1);
    LoadedPack staged;
    if (want > 0 && want != pack_.stage && loadPackStage(pack_.cfg.id, want, staged)) {
      pack_ = std::move(staged);
      pack_.rebind();
    }
    pet_.stats().stage = static_cast<uint8_t>(std::max(0, pack_.stage));
  }
  amb_ = computeAmbience();
  lastLevel_ = pet_.level();
  lastTickMs_ = lastSaveMs_ = p_.clock->millis();
  applyBrightness();
  invalidateAll();
  started_ = true;
  log("deskpet: started with pack '" + pack_.cfg.id + "'");
  render();
}

std::vector<std::string> Engine::listPacks() {
  std::vector<std::string> ids;
  if (p_.storage) p_.storage->listDirs("/packs", ids);
  ids.erase(std::remove_if(ids.begin(), ids.end(),
                           [](const std::string& s) { return !isSafeFileName(s); }),
            ids.end());
  std::sort(ids.begin(), ids.end());
  return ids;
}

bool Engine::loadPackStage(const std::string& id, int stage, LoadedPack& out) {
  std::string err;
  if (!p_.storage || !loadPack(*p_.storage, id, out, err, stage)) {
    packError_ = err;
    log("deskpet: pack '" + id + "' rejected: " + err);
    return false;
  }
  return true;
}

bool Engine::selectPack(const std::string& id) {
  if (!p_.storage) return false;
  // Same pet: keep its form. Another pet: take the form its level allows.
  int stage = 0;
  if (started_) {
    if (id == pack_.cfg.id) stage = pet_.stats().stage;
    else stage = -1;  // decided after the manifest is known
  }
  LoadedPack next;
  if (!loadPackStage(id, stage < 0 ? 0 : stage, next)) return false;
  if (stage < 0) {
    int target = next.cfg.stageIndexFor(pet_.level());
    if (target != next.stage && !loadPackStage(id, target, next)) return false;
  }
  packError_.clear();
  pack_ = std::move(next);
  pack_.rebind();
  if (started_) pet_.stats().stage = static_cast<uint8_t>(pack_.stage);
  pet_.setTuning(pack_.cfg.tuning);
  log("deskpet: loaded pack '" + pack_.cfg.id + "' v" + pack_.cfg.version);
  if (started_) {
    saveSettings();
    invalidateAll();
  }
  return true;
}

std::vector<std::string> Engine::listRooms() {
  std::vector<std::string> ids;
  if (p_.storage) p_.storage->listDirs("/rooms", ids);
  ids.push_back("cozy");  // built in, even without a folder
  ids.erase(std::remove_if(ids.begin(), ids.end(),
                           [](const std::string& s) { return !isSafeFileName(s); }),
            ids.end());
  std::sort(ids.begin(), ids.end());
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  return ids;
}

bool Engine::selectRoom(const std::string& id) {
  if (!p_.storage) return false;
  LoadedRoom next;
  std::string err;
  if (!loadRoom(*p_.storage, id, next, err)) {
    roomError_ = err;
    log("deskpet: room '" + id + "' rejected: " + err);
    return false;
  }
  roomError_.clear();
  room_ = std::move(next);
  room_.rebind();
  world_.setRoom(room_.cfg);
  log("deskpet: room '" + room_.cfg.id + "'");
  if (started_) {
    saveSettings();
    invalidateAll();
  }
  return true;
}

std::string Engine::roomJson() { return roomToJson(room_.cfg); }

bool Engine::applyRoomJson(const std::string& json, std::string& err) {
  RoomConfig cfg;
  if (!parseRoom(json.data(), json.size(), cfg, err)) return false;
  // Edits always land in the "custom" room so shipped themes stay intact.
  if (cfg.backgroundsFrom.empty() && cfg.id != "custom") {
    bool anyBg = !cfg.scenes[0].background.empty() || !cfg.scenes[1].background.empty();
    if (anyBg) cfg.backgroundsFrom = cfg.id;
  }
  cfg.id = "custom";
  if (!p_.storage || !p_.storage->write("/rooms/custom/room.json", roomToJson(cfg))) {
    err = "cannot save room";
    return false;
  }
  if (!selectRoom("custom")) {
    err = roomError_;
    return false;
  }
  // Show the result live while the editor stays open.
  if (screen_ != Screen::Home) goTo(Screen::Home);
  return true;
}

void Engine::startPortal() {
  if (portalOpen_ || !p_.uploader) return;
  p_.uploader->start(*this);
  portalOpen_ = true;
}

void Engine::stopPortal() {
  if (!portalOpen_) return;
  p_.uploader->stop();
  portalOpen_ = false;
  // New files may have arrived: reload what is in use.
  selectPack(pack_.cfg.id);
  selectRoom(room_.cfg.id);
  invalidateAll();
}

void Engine::loadSettings() {
  std::string json, packId = "blobby", roomId = "cozy";
  if (p_.storage && p_.storage->read("/settings.json", json, 2048)) {
    JsonDocument doc;
    if (!deserializeJson(doc, json)) {
      packId = doc["pack"] | "blobby";
      roomId = doc["room"] | "cozy";
      brightness_ = std::min(255, std::max(20, doc["brightness"] | 200));
      dnd_ = doc["dnd"] | false;
    }
  }
  if (!selectPack(packId)) {
    std::vector<std::string> ids = listPacks();
    bool ok = false;
    for (const std::string& id : ids)
      if (id != packId && selectPack(id)) { ok = true; break; }
    if (!ok) {
      pack_ = LoadedPack();
      pack_.cfg = defaultPack();
      pet_.setTuning(pack_.cfg.tuning);
      log("deskpet: using built-in pack");
    }
  }
  if (!selectRoom(roomId)) selectRoom("cozy");
}

void Engine::saveSettings() {
  if (!p_.storage) return;
  JsonDocument doc;
  doc["pack"] = pack_.cfg.id;
  doc["room"] = room_.cfg.id;
  doc["brightness"] = brightness_;
  doc["dnd"] = dnd_;
  std::string out;
  serializeJson(doc, out);
  p_.storage->write("/settings.json", out);
}

void Engine::save() {
  if (p_.storage) p_.storage->write("/save.json", pet_.toJson(now()));
  lastSaveMs_ = p_.clock->millis();
}

void Engine::restorePet() {
  std::string json;
  int64_t savedAt = 0;
  if (!p_.storage || !p_.storage->read("/save.json", json, 1024) || !pet_.fromJson(json, savedAt))
    return;
  int64_t t = now();
  if (savedAt > 0 && t > savedAt) {
    // Catch up on time spent powered off (capped at three days).
    float hours = static_cast<float>(std::min<int64_t>(t - savedAt, 72 * 3600)) / 3600.0f;
    pet_.elapse(hours);
  }
  if (pet_.asleep()) world_.placeInBed();
}

// ------------------------------------------------------------------- loop

void Engine::loop() {
  const uint32_t ms = p_.clock->millis();

  // Input.
  InputEvent ev;
  int16_t tx = 0, ty = 0;
  bool down = p_.input && p_.input->readTouch(tx, ty);
  bool back = p_.input && p_.input->readBack();
  if (gestures_.update(down, tx, ty, back, ms, ev)) handleGesture(ev);
  while (p_.input && p_.input->pollGesture(ev)) handleGesture(ev);

  pollSources(ms);
  applyAlertSignals(alerts_.update(now(), ms, pet_.asleep()));

  // Fixed-step behaviour. If we fell far behind (debugger, blocking fetch),
  // resync instead of fast-forwarding the animation.
  uint32_t behind = ms - lastTickMs_;
  if (behind > kTickMs * 8) lastTickMs_ = ms - kTickMs;
  while (ms - lastTickMs_ >= kTickMs) {
    lastTickMs_ += kTickMs;
    tick();
  }

  tune_.update(p_.buzzer, ms);
  if (portalOpen_) p_.uploader->loop();
  if (ms - lastSaveMs_ >= kSaveEveryMs) save();
  render();
}

void Engine::pollSources(uint32_t ms) {
  (void)ms;
  if (p_.calendar) {
    std::vector<CalendarEvent> evs;
    if (p_.calendar->poll(evs)) {
      alerts_.setEvents(evs);
      log("deskpet: calendar has " + std::to_string(evs.size()) + " events");
    }
  }
  if (p_.weather) {
    Weather w;
    if (p_.weather->poll(w)) setWeather(w);
  }
  if (p_.notifications) {
    Notification n;
    while (p_.notifications->poll(n)) {
      if (!alerts_.pushNotification(n, dnd_)) log("deskpet: notification muted (DND)");
    }
  }
}

void Engine::applyAlertSignals(const AlertSignals& s) {
  if (s.meetingRaised) {
    if (pet_.asleep()) world_.wake(pet_);
    if (const char* t = pack_.cfg.sound("meeting")) tune_.play(t, 0);
    log("deskpet: meeting alert '" + alerts_.meeting().title + "'");
  }
  if (s.meetingIgnored) {
    pet_.meetingIgnored();
    log("deskpet: meeting ignored");
  }
  if (s.notificationShown) {
    world_.notified();
    if (!dnd_)
      if (const char* t = pack_.cfg.sound("notify")) tune_.play(t, 0);
  }
  if (s.changed) invalidateAll();
}

void Engine::tick() {
  const Rect before = petBounds(pack_, world_.view());
  const bool bowlBefore = world_.view().bowlFull;
  const bool asleepBefore = pet_.asleep();

  pet_.elapse(static_cast<float>(kTickMs) * timeScale_ / 3600000.0f);
  world_.tick(pet_);

  if (pet_.level() > lastLevel_) onLevelUp();
  if (evo_.phase != EvoPhase::None) stepEvolution();
  if (toastTicks_ > 0 && --toastTicks_ == 0) {
    toast_.clear();
    invalidate(layout::kToast);
  }
  if (asleepBefore != pet_.asleep()) {
    applyBrightness();
    save();
  }

  updateAmbience();
  if (world_.sceneChangedSinceLastCheck()) {
    if (screen_ == Screen::Home) invalidateAll();
  } else if (screen_ == Screen::Home) {
    invalidate(Rect::unite(before, petBounds(pack_, world_.view())));
    if (bowlBefore != world_.view().bowlFull) invalidate(bowlBounds(room_.cfg));
  }

  // HUD and info screens only redraw when what they show changed.
  uint32_t h = contentHash();
  if (h != shownHash_) {
    shownHash_ = h;
    if (screen_ == Screen::Home) invalidate(hudRect_);
    else if (screen_ != Screen::Actions) invalidateAll();
  }
}

uint32_t Engine::contentHash() {
  uint32_t h = 2166136261u;
  auto mix = [&h](uint32_t v) { h = (h ^ v) * 16777619u; };
  int64_t t = now();
  mix(static_cast<uint32_t>(t / 60));
  mix(dnd_);
  mix(static_cast<uint32_t>(brightness_));
  mix(portalOpen_);
  if (screen_ == Screen::Home) {
    const PetStats& s = pet_.stats();
    mix(pet_.isLow(s.food) || pet_.isLow(s.fun) || pet_.isLow(s.energy));
  } else if (screen_ == Screen::Stats) {
    const PetStats& s = pet_.stats();
    mix(static_cast<uint32_t>(s.food));
    mix(static_cast<uint32_t>(s.fun));
    mix(static_cast<uint32_t>(s.energy));
    mix(s.xp);
    mix(s.candies);
    mix(static_cast<uint32_t>(pet_.mood()));
  } else if (screen_ == Screen::Agenda) {
    for (const CalendarEvent& e : alerts_.events()) mix(static_cast<uint32_t>(e.start));
  } else if (screen_ == Screen::Upload && portalOpen_) {
    for (const std::string& l : p_.uploader->statusLines())
      for (char ch : l) mix(static_cast<uint8_t>(ch));
  }
  if (p_.connectivity && screen_ == Screen::Upload) {
    ConnectivityStatus cs = p_.connectivity();
    for (const char* q = cs.wifi; *q; ++q) mix(static_cast<uint8_t>(*q));
    for (const char* q = cs.phone; *q; ++q) mix(static_cast<uint8_t>(*q));
  }
  return h;
}

// ----------------------------------------------------------------- input

void Engine::goTo(Screen s) {
  screen_ = s;
  if (s == Screen::Upload) startPortal();
  shownHash_ = contentHash();
  invalidateAll();
}

void Engine::toggleDnd() {
  dnd_ = !dnd_;
  saveSettings();
  invalidateAll();
}

void Engine::setBrightness(int level) {
  brightness_ = std::min(255, std::max(20, level));
  applyBrightness();
  saveSettings();
}

void Engine::applyBrightness() {
  int level = pet_.asleep() ? std::min(brightness_, 24) : brightness_;
  if (level != appliedBrightness_ && p_.display) {
    p_.display->setBrightness(static_cast<uint8_t>(level));
    appliedBrightness_ = level;
  }
}

void Engine::handleGesture(const InputEvent& e) {
  if (e.gesture == Gesture::None) return;
  if (alerts_.active() != AlertKind::None) {
    onAlert(e);
    return;
  }
  if (evo_.phase != EvoPhase::None) {
    onEvolutionGesture(e);
    return;
  }
  switch (screen_) {
    case Screen::Home: onHome(e); break;
    case Screen::Stats: onStats(e); break;
    case Screen::Agenda:
      if (e.gesture == Gesture::SwipeLeft || e.gesture == Gesture::Back) goTo(Screen::Home);
      break;
    case Screen::Actions: onActions(e); break;
    case Screen::Settings: onSettings(e); break;
    case Screen::Upload:
      if (e.gesture == Gesture::Back) {
        stopPortal();
        goTo(Screen::Settings);
      }
      break;
  }
}

void Engine::onAlert(const InputEvent& e) {
  if (alerts_.active() == AlertKind::Meeting) {
    if (e.gesture == Gesture::Tap) {
      pet_.meetingAck();
      world_.tapped(pet_);
      if (pet_.stats().meetingAcks % 2 == 0) giveCandy(1, "meeting streak");
      alerts_.ack();
      tune_.stop(p_.buzzer);
    } else if (e.gesture == Gesture::LongPress) {
      alerts_.snooze(now());
      tune_.stop(p_.buzzer);
    } else if (e.gesture == Gesture::Back) {
      alerts_.dismiss();
      tune_.stop(p_.buzzer);
    }
  } else if (e.gesture == Gesture::Tap || e.gesture == Gesture::Back) {
    alerts_.dismiss();
  }
  applyAlertSignals(alerts_.update(now(), p_.clock->millis(), pet_.asleep()));
}

void Engine::onHome(const InputEvent& e) {
  switch (e.gesture) {
    case Gesture::Tap: world_.tapped(pet_); break;
    case Gesture::DoubleTap:
      if (pet_.asleep()) world_.wake(pet_);
      else world_.requestSleep(pet_);
      applyBrightness();
      break;
    case Gesture::LongPress: toggleDnd(); break;
    case Gesture::SwipeLeft: goTo(Screen::Stats); break;
    case Gesture::SwipeRight: goTo(Screen::Agenda); break;
    case Gesture::SwipeUp: goTo(Screen::Actions); break;
    case Gesture::SwipeDown: goTo(Screen::Settings); break;
    case Gesture::Back:
      if (portalOpen_) stopPortal();  // leave live room editing
      break;
    default: break;
  }
}

void Engine::onActions(const InputEvent& e) {
  if (e.gesture == Gesture::SwipeDown || e.gesture == Gesture::Back) {
    goTo(Screen::Home);
    return;
  }
  if (e.gesture != Gesture::Tap) return;
  int hit = -1;
  for (int i = 0; i < 4; ++i)
    if (layout::kActionTiles[i].contains(e.x, e.y)) hit = i;
  if (hit < 0) return;
  switch (hit) {
    case 0: {
      world_.requestFeed(pet_);
      int64_t day = localDay();  // first meal of the day comes with a treat
      if (day >= 0 && day != pet_.stats().treatDay) {
        pet_.stats().treatDay = static_cast<int32_t>(day);
        giveCandy(1, "daily treat");
      }
      break;
    }
    case 1: world_.requestPlay(pet_); break;
    case 2:
      if (pet_.asleep()) world_.wake(pet_);
      else world_.requestSleep(pet_);
      break;
    case 3:
      if (world_.view().scene == SceneId::Yard) world_.requestHome(pet_);
      else world_.requestWalk(pet_);
      break;
  }
  applyBrightness();
  goTo(Screen::Home);
}

void Engine::onSettings(const InputEvent& e) {
  if (e.gesture == Gesture::SwipeUp || e.gesture == Gesture::Back) {
    goTo(Screen::Home);
    return;
  }
  if (e.gesture == Gesture::LongPress) {
    toggleDnd();
    return;
  }
  if (e.gesture != Gesture::Tap) return;
  using namespace layout;
  if (e.y >= kRowBrightness && e.y < kRowBrightness + kRowH) {
    setBrightness(brightness_ + (e.x < kScreenW / 2 ? -40 : 40));
  } else if (e.y >= kRowDnd && e.y < kRowDnd + kRowH) {
    toggleDnd();
  } else if (e.y >= kRowPack && e.y < kRowPack + kRowH) {
    std::vector<std::string> ids = listPacks();
    if (!ids.empty()) {
      auto it = std::find(ids.begin(), ids.end(), pack_.cfg.id);
      size_t start = it == ids.end() ? 0 : static_cast<size_t>(it - ids.begin()) + 1;
      for (size_t i = 0; i < ids.size(); ++i)
        if (selectPack(ids[(start + i) % ids.size()])) break;
    }
  } else if (e.y >= kRowRoom && e.y < kRowRoom + kRowH) {
    std::vector<std::string> ids = listRooms();
    auto it = std::find(ids.begin(), ids.end(), room_.cfg.id);
    size_t start = it == ids.end() ? 0 : static_cast<size_t>(it - ids.begin()) + 1;
    for (size_t i = 0; i < ids.size(); ++i)
      if (selectRoom(ids[(start + i) % ids.size()])) break;
  } else if (e.y >= kRowUpload) {
    goTo(Screen::Upload);
    return;
  }
  invalidateAll();
}

// ------------------------------------------------------------- rendering

void Engine::invalidate(const Rect& r0) {
  Rect r = Rect::intersect(r0, Rect(0, 0, kScreenW, kScreenH));
  if (r.empty()) return;
  for (int i = 0; i < dirtyCount_; ++i) {
    if (dirty_[i].intersects(r)) {
      dirty_[i] = Rect::unite(dirty_[i], r);
      return;
    }
  }
  if (dirtyCount_ < kMaxDirty) {
    dirty_[dirtyCount_++] = r;
  } else {
    dirty_[0] = Rect::unite(dirty_[0], r);
  }
}

void Engine::render() {
  if (!p_.display || dirtyCount_ == 0) return;
  uint32_t pixels = 0;
  for (int i = 0; i < dirtyCount_; ++i) {
    const Rect r = dirty_[i];
    if (r.w == kScreenW && r.h == kScreenH) ++rstats_.fullRedraws;
    for (int y = r.y; y < r.bottom(); y += kStripRows) {
      Rect strip(r.x, y, r.w, std::min<int>(kStripRows, r.bottom() - y));
      Canvas c(strip_, strip);
      drawAll(c);
      p_.display->pushPixels(strip, strip_);
      pixels += static_cast<uint32_t>(strip.w) * strip.h;
    }
  }
  dirtyCount_ = 0;
  ++rstats_.frames;
  rstats_.pixelsLastFrame = pixels;
  rstats_.pixelsTotal += pixels;
}

void Engine::drawAll(Canvas& c) {
  switch (screen_) {
    case Screen::Home: drawHome(c); break;
    case Screen::Stats: drawStats(c); break;
    case Screen::Agenda: drawAgenda(c); break;
    case Screen::Actions: drawActions(c); break;
    case Screen::Settings: drawSettings(c); break;
    case Screen::Upload: drawUpload(c); break;
  }
  if (evo_.phase != EvoPhase::None) drawEvolution(c);
  if (alerts_.active() == AlertKind::Meeting) drawMeeting(c);
  else if (alerts_.active() == AlertKind::Notification) drawNotification(c);
}

std::string Engine::clockText(int64_t t) {
  if (t <= 0) return "--:--";
  int64_t local = t + int64_t(p_.clock->utcOffsetMinutes()) * 60;
  int mins = static_cast<int>((local / 60) % (24 * 60));
  if (mins < 0) mins += 24 * 60;
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02d:%02d", mins / 60, mins % 60);
  return buf;
}

}  // namespace dp

namespace dp {

// --------------------------------------------------- candy and evolution

namespace {
constexpr uint16_t kIntroEnd = 7;    // "What? X is evolving!"
constexpr uint16_t kMorphEnd = 31;   // silhouettes flashing faster
constexpr uint16_t kFlashEnd = 35;   // white flash, then the new form
constexpr uint16_t kDoneEnd = 75;    // message stays ~11 s (or tap)
constexpr uint16_t kCancelEnd = 9;
}  // namespace

int64_t Engine::localDay() {
  int64_t t = now();
  if (t <= 0) return -1;
  return (t + int64_t(p_.clock->utcOffsetMinutes()) * 60) / 86400;
}

void Engine::showToast(const std::string& text) {
  toast_ = text;
  toastTicks_ = 12;  // ~3.4 s
  invalidate(layout::kToast);
}

void Engine::giveCandy(int n, const char* why) {
  PetStats& s = pet_.stats();
  s.candies = static_cast<uint16_t>(std::min(999, s.candies + n));
  showToast("+" + std::to_string(n) + " CANDY!");
  log(std::string("deskpet: +") + std::to_string(n) + " Rare Candy (" + (why ? why : "gift") + ")");
  save();
}

bool Engine::useRareCandy() {
  if (evo_.phase != EvoPhase::None || !pet_.useRareCandy()) return false;
  if (pet_.asleep()) world_.wake(pet_);
  goTo(Screen::Home);
  world_.celebrate();
  log("deskpet: Rare Candy used");
  onLevelUp();
  save();
  return true;
}

void Engine::onLevelUp() {
  lastLevel_ = pet_.level();
  showToast("LEVEL " + std::to_string(lastLevel_) + "!");
  if (const char* t = pack_.cfg.sound("happy")) tune_.play(t, 0);
  log("deskpet: level up to " + std::to_string(lastLevel_));
  checkEvolution();
}

void Engine::checkEvolution() {
  if (evo_.phase != EvoPhase::None) return;
  const int stage = pack_.stage;
  const int target = pack_.cfg.stageIndexFor(pet_.level());
  if (target <= stage || pet_.level() <= refusedLevel_) return;
  const int to = stage + 1;  // one form at a time
  LoadedPack next;
  if (!loadPackStage(pack_.cfg.id, to, next)) {
    refusedLevel_ = pet_.level();
    return;
  }
  evo_.phase = EvoPhase::Intro;
  evo_.to = to;
  evo_.ticks = 0;
  evo_.fromName = pack_.cfg.stage(stage) ? pack_.cfg.stage(stage)->name : pack_.cfg.name;
  evo_.toName = next.cfg.stage(to)->name;
  evo_.next = std::move(next);
  evo_.next.rebind();
  if (pet_.asleep()) world_.wake(pet_);
  if (screen_ != Screen::Home) goTo(Screen::Home);
  if (const char* t = pack_.cfg.sound("evolve")) tune_.play(t, 0);
  log("deskpet: " + evo_.fromName + " is evolving into " + evo_.toName);
  invalidateAll();
}

void Engine::stepEvolution() {
  ++evo_.ticks;
  invalidateAll();  // the scene animates every tick
  switch (evo_.phase) {
    case EvoPhase::Intro:
      if (evo_.ticks >= kIntroEnd) evo_.phase = EvoPhase::Morph;
      break;
    case EvoPhase::Morph:
      if (evo_.ticks >= kMorphEnd) evo_.phase = EvoPhase::Flash;
      break;
    case EvoPhase::Flash:
      if (evo_.ticks >= kFlashEnd) {
        pack_ = std::move(evo_.next);
        pack_.rebind();
        evo_.next = LoadedPack();
        pet_.stats().stage = static_cast<uint8_t>(pack_.stage);
        evo_.phase = EvoPhase::Done;
        if (const char* t = pack_.cfg.sound("happy")) tune_.play(t, 0);
        log("deskpet: evolved into " + evo_.toName);
        save();
      }
      break;
    case EvoPhase::Done:
      if (evo_.ticks >= kDoneEnd) {
        evo_.phase = EvoPhase::None;
        checkEvolution();  // a big candy streak may allow another step
      }
      break;
    case EvoPhase::Cancelled:
      if (evo_.ticks >= kCancelEnd) evo_.phase = EvoPhase::None;
      break;
    case EvoPhase::None:
      break;
  }
}

void Engine::onEvolutionGesture(const InputEvent& e) {
  if ((evo_.phase == EvoPhase::Intro || evo_.phase == EvoPhase::Morph) &&
      e.gesture == Gesture::Back) {
    evo_.phase = EvoPhase::Cancelled;
    evo_.ticks = 0;
    evo_.next = LoadedPack();
    refusedLevel_ = pet_.level();
    tune_.stop(p_.buzzer);
    log("deskpet: evolution stopped");
    invalidateAll();
  } else if ((evo_.phase == EvoPhase::Done && evo_.ticks > kFlashEnd + 4) ||
             evo_.phase == EvoPhase::Cancelled) {
    if (e.gesture == Gesture::Tap || e.gesture == Gesture::Back) {
      evo_.phase = EvoPhase::None;
      invalidateAll();
      checkEvolution();
    }
  }
}

void Engine::onStats(const InputEvent& e) {
  if (e.gesture == Gesture::SwipeRight || e.gesture == Gesture::Back) {
    goTo(Screen::Home);
  } else if (e.gesture == Gesture::Tap && layout::kCandyButton.contains(e.x, e.y)) {
    if (!useRareCandy()) invalidateAll();
  }
}

}  // namespace dp
