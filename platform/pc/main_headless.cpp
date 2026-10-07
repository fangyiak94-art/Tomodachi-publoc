// Headless runner: drives the real engine through the MVP gate scenario
// (walks, eats, sleeps, goes outside) with a fake clock, prints a report and
// writes screenshots of every screen. Exit code != 0 if the gate fails.
//
//   deskpet_headless [--fs fs] [--out shots] [--seed 7]
//   deskpet_headless --preview <pack-id> [--packs DIR] [--room id] [--weather rain] [--hour 21]
//                    [--candy N]   feed N Rare Candies and record evolutions as evo_NNN shots
//                                          screenshots of a pack/room (plus 16
//                                          consecutive anim_NN frames), no gate
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include "deskpet/engine.h"
#include "pc_platform.h"

using namespace dp;

namespace {

struct Harness {
  pc::FramebufferDisplay display;
  pc::ScriptInput input;
  pc::ManualClock clock;
  pc::DirStorage storage;
  pc::SilentBuzzer buzzer;
  pc::QueueNotifications notifications;
  pc::FileCalendar calendar;
  pc::MockWeather weather;
  Engine engine;

  Harness(const std::string& fsRoot, const std::string& writeRoot, uint32_t seed)
      : storage(fsRoot),
        calendar(fsRoot + "/../mock/calendar.json", &clock),
        engine(makePlatform(seed)) {
    storage.setWriteRoot(writeRoot);
  }

  Platform makePlatform(uint32_t seed) {
    clock.epochBase = 1791363600;  // 2026-10-07 09:00 UTC
    clock.offsetMin = 8 * 60;      // Malaysia
    Platform p;
    p.display = &display;
    p.input = &input;
    p.clock = &clock;
    p.storage = &storage;
    p.buzzer = &buzzer;
    p.calendar = &calendar;
    p.notifications = &notifications;
    p.weather = &weather;
    p.log = [](const char* l) { std::printf("  [log] %s\n", l); };
    p.connectivity = []() { return ConnectivityStatus{"mock", "mock"}; };
    p.seed = seed;
    return p;
  }

  // Advance simulated time in 20 ms loop steps.
  void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 20) {
      clock.ms += 20;
      engine.loop();
    }
  }
  void ticks(int n) { run(n * Engine::kTickMs); }
  bool until(const std::function<bool()>& cond, int maxTicks) {
    for (int i = 0; i < maxTicks; ++i) {
      if (cond()) return true;
      ticks(1);
    }
    return cond();
  }
  void gesture(Gesture g, int x = 120, int y = 120) {
    input.push(g, static_cast<int16_t>(x), static_cast<int16_t>(y));
    run(40);
  }
};

int failures = 0;
void check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
}

}  // namespace

int main(int argc, char** argv) {
  std::string fsRoot = "fs", out = "shots";
  uint32_t seed = 7;
  std::string preview, room, weatherArg;
  std::vector<std::string> packDirs;
  int hour = -1;
  int candy = 0;
  for (int i = 1; i + 1 < argc; i += 2) {
    if (!std::strcmp(argv[i], "--fs")) fsRoot = argv[i + 1];
    else if (!std::strcmp(argv[i], "--out")) out = argv[i + 1];
    else if (!std::strcmp(argv[i], "--seed")) seed = static_cast<uint32_t>(std::atoi(argv[i + 1]));
    else if (!std::strcmp(argv[i], "--preview")) preview = argv[i + 1];
    else if (!std::strcmp(argv[i], "--room")) room = argv[i + 1];
    else if (!std::strcmp(argv[i], "--packs")) packDirs.push_back(argv[i + 1]);
    else if (!std::strcmp(argv[i], "--weather")) weatherArg = argv[i + 1];
    else if (!std::strcmp(argv[i], "--hour")) hour = std::atoi(argv[i + 1]);
    else if (!std::strcmp(argv[i], "--candy")) candy = std::atoi(argv[i + 1]);
  }
  std::filesystem::create_directories(out);
  const std::string state = out + "/state";
  std::filesystem::remove_all(state);
  std::filesystem::create_directories(state);

  Harness h(fsRoot, state, seed);
  for (const std::string& d : packDirs) h.storage.addPackDir(d);
  Engine& e = h.engine;
  auto shot = [&](const char* name) {
    std::string p = out + "/" + name + ".ppm";
    h.display.savePpm(p);
    std::printf("  shot %s\n", p.c_str());
  };

  if (hour >= 0) h.clock.epochBase = 1791331200 + (hour - 8 + 24) % 24 * 3600;  // local = UTC+8
  e.begin();
  if (!preview.empty()) {
    if (!e.selectPack(preview)) {
      std::printf("pack '%s' rejected: %s\n", preview.c_str(), e.lastPackError().c_str());
      return 1;
    }
    if (!room.empty() && !e.selectRoom(room)) {
      std::printf("room '%s' rejected: %s\n", room.c_str(), e.lastRoomError().c_str());
      return 1;
    }
    Weather w;
    if (!weatherArg.empty()) {
      if (!weatherFromName(weatherArg.c_str(), w)) {
        std::printf("weather must be clear, cloudy, rain or snow\n");
        return 1;
      }
      h.weather.set(w);
    }
    h.ticks(30);
    shot("preview_house");
    // One shot per tick, to check the animation actually moves.
    for (int i = 0; i < 16; ++i) {
      h.ticks(1);
      char name[32];
      std::snprintf(name, sizeof(name), "anim_%02d", i);
      shot(name);
    }
    if (candy > 0) {
      // Use candies from the Stats screen one by one; film every evolution.
      int evoShot = 0;
      e.giveCandy(candy, "preview");
      for (int i = 0; i < candy; ++i) {
        h.gesture(Gesture::SwipeLeft);
        h.gesture(Gesture::Tap, 120, 206);  // candy button
        h.ticks(1);
        while (e.evoPhase() != EvoPhase::None) {
          char name[32];
          std::snprintf(name, sizeof(name), "evo_%03d", evoShot++);
          shot(name);
          h.ticks(1);
        }
      }
      std::printf("level %u, form '%s'\n", static_cast<unsigned>(e.pet().level()),
                  e.pack().cfg.stage(e.pack().stage) ? e.pack().cfg.stage(e.pack().stage)->name.c_str() : "?");
      h.ticks(4);
      shot("preview_after_candy");
    }
    h.gesture(Gesture::SwipeUp);
    h.gesture(Gesture::Tap, 164, 162);  // WALK
    h.until([&] { return e.world().view().scene == SceneId::Yard; }, 80);
    h.ticks(3);
    shot("preview_yard");
    h.gesture(Gesture::DoubleTap);
    h.until([&] { return e.pet().asleep(); }, 160);
    h.ticks(3);
    shot("preview_sleep");
    return 0;
  }
  check(e.pack().cfg.id == "blobby", "loads the blobby pack from storage");
  h.ticks(30);
  shot("01_home_house");

  // Walks.
  int16_t x0 = e.world().view().x;
  bool moved = h.until([&] { return e.world().view().x != x0; }, 60);
  check(moved, "pet wanders around on its own");

  // Eats: Actions > Feed.
  e.pet().stats().food = 40;
  h.gesture(Gesture::SwipeUp);
  shot("02_actions");
  h.gesture(Gesture::Tap, 76, 78);  // FEED tile
  bool ate = h.until([&] { return e.pet().stats().food > 60; }, 80);
  check(ate, "feed: walks to the bowl and eats (+25 food)");
  check(!e.world().view().bowlFull, "bowl is empty afterwards");

  // Goes outside: Actions > Walk.
  h.gesture(Gesture::SwipeUp);
  h.gesture(Gesture::Tap, 164, 162);  // WALK tile
  bool outside = h.until([&] { return e.world().view().scene == SceneId::Yard; }, 80);
  check(outside, "walk: goes out through the door into the yard");
  h.ticks(8);
  shot("03_home_yard");

  // Comes back when tired.
  e.pet().stats().energy = 20;
  bool home = h.until([&] { return e.world().view().scene == SceneId::House; }, 120);
  check(home, "tired pet comes back inside");

  // Sleeps: double tap on home.
  e.pet().stats().energy = 50;
  h.gesture(Gesture::DoubleTap);
  bool slept = h.until([&] { return e.pet().asleep(); }, 120);
  check(slept, "double tap: walks to bed and sleeps");
  check(h.display.brightness < 40, "screen dims while asleep");
  h.ticks(4);
  shot("04_home_sleeping");

  // Notification is queued while asleep, then shown after waking.
  h.notifications.queue.push_back({"Chronos", "Ali", "Lunch at 1? The usual place"});
  h.ticks(2);
  check(e.alerts().active() == AlertKind::None && e.alerts().queued() == 1,
        "notification is queued while the pet sleeps");
  h.gesture(Gesture::DoubleTap);
  h.ticks(2);
  check(e.alerts().active() == AlertKind::Notification, "notification shows once awake");
  shot("05_notification");
  h.run(5200);
  check(e.alerts().active() == AlertKind::None, "notification auto-dismisses after 5 s");

  // Meeting alert from the mock calendar (standup starts 11 min after boot).
  bool meeting = h.until([&] { return e.alerts().active() == AlertKind::Meeting; }, 4000);
  check(meeting, "calendar event raises the meeting alert 10 min before");
  shot("06_meeting");
  uint32_t xp = e.pet().stats().xp;
  h.gesture(Gesture::Tap);
  check(e.pet().stats().xp == xp + 10, "tap on meeting alert: +10 XP");

  // Other screens.
  h.gesture(Gesture::SwipeLeft);
  shot("07_stats");
  h.gesture(Gesture::Back);
  h.gesture(Gesture::SwipeRight);
  shot("08_agenda");
  h.gesture(Gesture::Back);
  h.gesture(Gesture::SwipeDown);
  shot("09_settings");
  h.gesture(Gesture::Back);
  check(e.screen() == Screen::Home, "BOOT always returns home");

  // Dirty-rect rendering: idle ticks must not redraw the whole screen.
  uint32_t full = e.renderStats().fullRedraws;
  uint64_t before = h.display.pushedPixels;
  h.ticks(50);
  uint64_t perTick = (h.display.pushedPixels - before) / 50;
  std::printf("  avg pixels pushed per tick: %llu of %d\n",
              static_cast<unsigned long long>(perTick), kScreenW * kScreenH);
  check(e.renderStats().fullRedraws == full || e.world().view().scene == SceneId::Yard,
        "no full-screen redraws while idling in the house");
  check(perTick < static_cast<uint64_t>(kScreenW * kScreenH / 3),
        "average redraw well under a full frame");

  // Rare Candy levels the pet up; Blobby evolves at level 5.
  e.giveCandy(4, "gate");
  for (int i = 0; i < 4 && e.evoPhase() == EvoPhase::None; ++i) {
    h.gesture(Gesture::SwipeLeft);
    h.gesture(Gesture::Tap, 120, 206);
  }
  check(e.pet().level() >= 5, "rare candies level the pet up");
  check(e.evoPhase() != EvoPhase::None, "reaching level 5 starts the evolution scene");
  h.ticks(12);
  shot("12_evolving");
  bool evolved = h.until([&] { return e.evoPhase() == EvoPhase::None; }, 120);
  check(evolved && e.pack().stage == 1 && e.pet().stats().stage == 1, "evolves into the next form");

  // Swap packs at runtime (Settings > Pack), no rebuild.
  h.gesture(Gesture::SwipeDown);
  h.gesture(Gesture::Tap, 120, 130);
  check(e.pack().cfg.id != "blobby" && e.pack().hasSprite, "settings: switch to the next (sprite) pack");
  std::string roomBefore = e.room().cfg.id;
  h.gesture(Gesture::Tap, 120, 174);
  check(e.room().cfg.id != roomBefore, "settings: switch room theme");
  shot("10_settings_rooms");
  h.gesture(Gesture::Back);
  h.ticks(20);
  shot("11_sprout_new_room");

  // The room changes by itself: rain keeps the pet inside.
  h.weather.set(Weather::Rain);
  h.ticks(2);
  check(e.ambience().weather == Weather::Rain, "weather feed reaches the room");
  h.gesture(Gesture::SwipeUp);
  h.gesture(Gesture::Tap, 164, 162);  // WALK
  h.until([&] { return e.world().view().scene == SceneId::Yard; }, 80);
  bool cameHome = h.until([&] { return e.world().view().scene == SceneId::House; }, 120);
  check(cameHome, "pet comes back in when it rains");

  std::printf("\n%s: %d failure(s)\n", failures ? "GATE FAILED" : "GATE PASSED", failures);
  return failures ? 1 : 0;
}
