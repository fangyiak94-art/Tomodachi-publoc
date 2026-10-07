// Unit tests for the portable engine. No framework: each TEST registers a
// function; failures print file:line and the process exits non-zero.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "deskpet/alerts.h"
#include "deskpet/engine.h"
#include "deskpet/gfx.h"
#include "deskpet/input.h"
#include "deskpet/pack.h"
#include "deskpet/pet.h"
#include "deskpet/world.h"
#include "deskpet/portal.h"
#include "deskpet/room.h"
#include "http_portal.h"
#include "pc_platform.h"

using namespace dp;

namespace {
struct TestCase {
  const char* name;
  std::function<void()> fn;
};
std::vector<TestCase>& registry() {
  static std::vector<TestCase> r;
  return r;
}
int g_failures = 0;
struct Reg {
  Reg(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};
}  // namespace

#define TEST(name)                       \
  static void name();                    \
  static Reg reg_##name(#name, name);    \
  static void name()
#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::printf("  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
      ++g_failures;                                                       \
    }                                                                     \
  } while (0)
#define CHECK_NEAR(a, b) CHECK(std::fabs((a) - (b)) < 0.01f)

// ------------------------------------------------------------------ pet

TEST(mood_follows_spec_table_first_match_wins) {
  Pet p;
  PetStats& s = p.stats();
  s = {80, 80, 80, 0, false};
  CHECK(p.mood() == Mood::Happy);
  s.fun = 50;
  CHECK(p.mood() == Mood::Neutral);
  s.fun = 20;
  CHECK(p.mood() == Mood::Sad);
  s.food = 10;  // hungry beats sad
  CHECK(p.mood() == Mood::Hungry);
  s.asleep = true;  // sleepy beats everything
  CHECK(p.mood() == Mood::Sleepy);
  s = {40, 90, 80, 0, false};  // fun high but food < 50: not happy
  CHECK(p.mood() == Mood::Neutral);
}

TEST(decay_rates_awake_and_asleep) {
  Pet p;
  p.stats() = {50, 50, 50, 0, false};
  p.elapse(1.0f);
  CHECK_NEAR(p.stats().food, 40);
  CHECK_NEAR(p.stats().fun, 45);
  CHECK_NEAR(p.stats().energy, 42);
  p.sleep();
  p.elapse(1.0f);
  CHECK_NEAR(p.stats().food, 35);
  CHECK_NEAR(p.stats().fun, 45);
  CHECK_NEAR(p.stats().energy, 62);
  p.elapse(100.0f);
  CHECK(p.stats().food == 0 && p.stats().energy == 100);  // clamped
}

TEST(actions_and_level) {
  Pet p;
  p.stats() = {10, 10, 50, 0, false};
  p.feed();
  CHECK_NEAR(p.stats().food, 35);
  p.played();
  CHECK_NEAR(p.stats().fun, 30);
  CHECK_NEAR(p.stats().energy, 35);
  CHECK(p.stats().xp == 5);
  p.meetingAck();
  CHECK(p.stats().xp == 15);
  CHECK_NEAR(p.stats().fun, 35);
  p.meetingIgnored();
  CHECK_NEAR(p.stats().fun, 20);
  CHECK(p.level() == 1);
  p.stats().xp = 250;
  CHECK(p.level() == 3);
}

TEST(save_roundtrip_and_rejects_garbage) {
  Pet a;
  a.stats() = {12, 34, 56, 789, true};
  std::string j = a.toJson(1700000000);
  Pet b;
  int64_t at = 0;
  CHECK(b.fromJson(j, at));
  CHECK(at == 1700000000);
  CHECK_NEAR(b.stats().fun, 34);
  CHECK(b.stats().xp == 789 && b.stats().asleep);
  CHECK(!b.fromJson("{\"v\":2}", at));
  CHECK(!b.fromJson("not json", at));
  CHECK(b.fromJson("{\"v\":1,\"food\":1e9,\"fun\":-5}", at));
  CHECK(b.stats().food == 100 && b.stats().fun == 0);
}

// ---------------------------------------------------------------- world

static void runTicks(World& w, Pet& p, int n) {
  for (int i = 0; i < n; ++i) w.tick(p);
}

TEST(feed_goal_walks_to_bowl_and_eats) {
  PackConfig pack = defaultPack();
  RoomConfig room = defaultRoom();
  World w;
  w.reset(room, 3);
  Pet p;
  p.stats().food = 20;
  w.requestFeed(p);
  CHECK(w.view().bowlFull);
  int i = 0;
  for (; i < 100 && p.stats().food < 40; ++i) w.tick(p);
  CHECK(p.stats().food > 40);
  CHECK(!w.view().bowlFull);
  CHECK(std::abs(w.view().x - room.scene(SceneId::House).spot("bowl")->x) <= 1);
}

TEST(feed_from_outside_comes_home_first) {
  PackConfig pack = defaultPack();
  RoomConfig room = defaultRoom();
  World w;
  w.reset(room, 3);
  Pet p;
  w.requestWalk(p);
  runTicks(w, p, 60);
  CHECK(w.view().scene == SceneId::Yard);
  w.requestFeed(p);
  bool ate = false;
  for (int i = 0; i < 150 && !ate; ++i) {
    float before = p.stats().food;
    w.tick(p);
    ate = p.stats().food > before;
  }
  CHECK(ate);
  CHECK(w.view().scene == SceneId::House);
}

TEST(sleep_goal_goes_to_bed_and_wakes_rested) {
  PackConfig pack = defaultPack();
  RoomConfig room = defaultRoom();
  World w;
  w.reset(room, 5);
  Pet p;
  w.requestSleep(p);
  runTicks(w, p, 80);
  CHECK(p.asleep());
  CHECK(w.view().x == room.scene(SceneId::House).spot("bed")->x);
  CHECK(w.view().activity == Activity::Sleeping);
  p.stats().energy = 100;
  w.tick(p);
  CHECK(!p.asleep());  // wakes itself when fully rested
}

TEST(happy_pet_goes_out_alone_and_returns_after_max_ticks) {
  PackConfig pack = defaultPack();
  pack.tuning.outingChancePct = 100;
  RoomConfig room = defaultRoom();
  World w;
  w.reset(room, 9);
  Pet p(pack.tuning);
  p.stats() = {90, 90, 90, 0, false};
  bool wentOut = false;
  for (int i = 0; i < 100 && !wentOut; ++i) {
    w.tick(p);
    wentOut = w.view().scene == SceneId::Yard;
  }
  CHECK(wentOut);
  CHECK(p.stats().xp == pack.tuning.walkXp);
  pack.tuning.outingChancePct = 0;  // don't immediately go out again
  p.setTuning(pack.tuning);
  bool back = false;
  for (int i = 0; i < 200 && !back; ++i) {
    w.tick(p);
    back = w.view().scene == SceneId::House;
  }
  CHECK(back);
}

TEST(neutral_pet_stays_inside) {
  PackConfig pack = defaultPack();
  pack.tuning.outingChancePct = 100;
  RoomConfig room = defaultRoom();
  World w;
  w.reset(room, 11);
  Pet p(pack.tuning);
  p.stats() = {60, 50, 90, 0, false};
  p.setTuning(pack.tuning);
  for (int i = 0; i < 300; ++i) {
    p.stats().fun = 50;  // keep neutral
    w.tick(p);
    CHECK(w.view().scene == SceneId::House);
    if (w.view().scene != SceneId::House) break;
  }
}

TEST(world_stays_inside_scene_bounds) {
  PackConfig pack = defaultPack();
  RoomConfig room = defaultRoom();
  World w;
  w.reset(room, 1);
  Pet p;
  p.stats() = {90, 90, 90, 0, false};
  for (int i = 0; i < 2000; ++i) {
    w.tick(p);
    p.stats().energy = 90;
    const SceneConfig& sc = room.scene(w.view().scene);
    int lo = sc.minX, hi = sc.maxX;
    for (const Spot& s : sc.spots) { lo = std::min<int>(lo, s.x); hi = std::max<int>(hi, s.x); }
    CHECK(w.view().x >= lo && w.view().x <= hi);
    if (w.view().x < lo || w.view().x > hi) break;
  }
}

// --------------------------------------------------------------- alerts

TEST(meeting_alert_timing_ack_snooze_ignore) {
  AlertCenter a;
  const int64_t t0 = 1'000'000;
  a.setEvents({{"m1", "Standup", t0 + 900, t0 + 1800}});
  CHECK(!a.update(t0, 0, false).meetingRaised);
  CHECK(a.active() == AlertKind::None);
  AlertSignals s = a.update(t0 + 300, 0, false);  // 10 min before
  CHECK(s.meetingRaised && a.active() == AlertKind::Meeting);
  a.snooze(t0 + 310);
  CHECK(a.active() == AlertKind::None);
  CHECK(!a.update(t0 + 500, 0, false).meetingRaised);
  CHECK(a.update(t0 + 610, 0, false).meetingRaised);  // 5 min later
  a.ack();
  a.update(t0 + 620, 0, false);
  CHECK(a.active() == AlertKind::None);
  CHECK(!a.update(t0 + 700, 0, false).meetingRaised);  // not again

  AlertCenter b;
  b.setEvents({{"m2", "Review", t0 + 600, t0 + 1200}});
  CHECK(b.update(t0, 0, false).meetingRaised);
  CHECK(!b.update(t0 + 300, 0, false).meetingIgnored);
  CHECK(b.update(t0 + 600, 0, false).meetingIgnored);  // unanswered at start
}

TEST(meeting_needs_valid_clock) {
  AlertCenter a;
  a.setEvents({{"m1", "Standup", 100, 200}});
  CHECK(!a.update(0, 0, false).meetingRaised);
}

TEST(notification_priority_rules) {
  AlertCenter a;
  // Muted by DND.
  CHECK(!a.pushNotification({"app", "t", "b"}, true));
  CHECK(a.missed() == 1 && a.queued() == 0);
  // Queued while asleep.
  CHECK(a.pushNotification({"app", "t", "b"}, false));
  a.update(0, 0, true);
  CHECK(a.active() == AlertKind::None && a.queued() == 1);
  CHECK(a.update(0, 10, false).notificationShown);
  // Auto-dismiss after 5 s.
  a.update(0, 4000, false);
  CHECK(a.active() == AlertKind::Notification);
  a.update(0, 5011, false);
  CHECK(a.active() == AlertKind::None);
  // Held behind a meeting, shown after it.
  const int64_t t0 = 5000;
  a.setEvents({{"m", "M", t0 + 60, t0 + 600}});
  a.pushNotification({"app", "x", "y"}, false);
  a.update(t0, 6000, false);
  CHECK(a.active() == AlertKind::Meeting);
  a.ack();
  CHECK(a.update(t0 + 1, 6100, false).notificationShown);
}

TEST(meeting_preempts_visible_notification) {
  AlertCenter a;
  const int64_t t0 = 10000;
  a.pushNotification({"app", "n1", ""}, false);
  a.update(t0, 0, false);
  CHECK(a.active() == AlertKind::Notification);
  a.setEvents({{"m", "M", t0 + 60, t0 + 600}});
  CHECK(a.update(t0 + 1, 100, false).meetingRaised);
  CHECK(a.queued() == 1);  // the notification was put back
}

TEST(calendar_json_parser_bounds) {
  std::vector<CalendarEvent> ev;
  const char* ok = "{\"events\":[{\"id\":\"b\",\"title\":\"B\",\"start\":200,\"end\":300},"
                   "{\"id\":\"a\",\"title\":\"A\",\"start\":100}]}";
  CHECK(parseCalendarJson(ok, std::strlen(ok), ev));
  CHECK(ev.size() == 2 && ev[0].id == "a" && ev[0].end == 100 + 1800);
  CHECK(!parseCalendarJson("{}", 2, ev));
  CHECK(!parseCalendarJson("[", 1, ev));
  std::string big = "{\"events\":[";
  for (int i = 0; i < 40; ++i) big += std::string(i ? "," : "") + "{\"start\":" + std::to_string(1000 + i) + "}";
  big += "]}";
  CHECK(parseCalendarJson(big.data(), big.size(), ev));
  CHECK(ev.size() == 16);
}

TEST(rtttl_parser) {
  std::vector<TunePlayer::Note> n;
  CHECK(TunePlayer::parse("x:d=4,o=5,b=120:a,p,8c6,c#.", n));
  CHECK(n.size() == 4);
  CHECK(n[0].freq == 880 && n[0].ms == 500);  // o=5 a = A5, as in the Arduino RTTTL players
  CHECK(n[1].freq == 0);
  CHECK(n[2].freq == 1047 && n[2].ms == 250);
  CHECK(n[3].freq == 554 && n[3].ms == 750);
  CHECK(!TunePlayer::parse("garbage", n));
  CHECK(!TunePlayer::parse("x:d=4:zz", n));
}

// ---------------------------------------------------------------- input

TEST(gesture_recognizer) {
  GestureRecognizer g;
  InputEvent e;
  uint32_t t = 0;
  auto step = [&](bool down, int x, int y) {
    t += 20;
    return g.update(down, static_cast<int16_t>(x), static_cast<int16_t>(y), false, t, e);
  };
  // Tap fires after the double-tap window.
  step(true, 100, 100);
  step(false, 100, 100);
  bool got = false;
  for (int i = 0; i < 20 && !got; ++i) got = step(false, 0, 0);
  CHECK(got && e.gesture == Gesture::Tap && e.x == 100);
  // Double tap.
  step(true, 50, 50);
  step(false, 50, 50);
  step(true, 52, 50);
  CHECK(step(false, 52, 50) && e.gesture == Gesture::DoubleTap);
  // Swipe left.
  step(true, 200, 120);
  step(true, 150, 122);
  step(true, 100, 125);
  CHECK(step(false, 100, 125) && e.gesture == Gesture::SwipeLeft);
  // Long press.
  step(true, 120, 120);
  got = false;
  for (int i = 0; i < 40 && !got; ++i) got = step(true, 121, 120);
  CHECK(got && e.gesture == Gesture::LongPress);
  CHECK(!step(false, 121, 120));
  // BOOT.
  t += 20;
  CHECK(g.update(false, 0, 0, true, t, e) && e.gesture == Gesture::Back);
  t += 20;
  CHECK(!g.update(false, 0, 0, true, t, e));  // held: no repeat
}

// ---------------------------------------------------------------- packs

static bool parse(const std::string& json, PackConfig& p, std::string& err) {
  return parseManifest(json.data(), json.size(), p, err);
}

TEST(manifest_accepts_shipped_blobby_pack) {
  pc::DirStorage st(std::string(DESKPET_SOURCE_DIR) + "/fs");
  LoadedPack lp;
  std::string err;
  CHECK(loadPack(st, "blobby", lp, err));
  if (!err.empty()) std::printf("  err: %s\n", err.c_str());
  CHECK(lp.cfg.name == "Blobby");
  CHECK(lp.cfg.stageFor(5) && lp.cfg.stageFor(5)->name == "Big Blobby");
  CHECK(lp.cfg.stageFor(4)->name == "Blobby");
}

TEST(all_shipped_packs_load) {
  pc::DirStorage st(std::string(DESKPET_SOURCE_DIR) + "/fs");
  std::vector<std::string> ids;
  CHECK(st.listDirs("/packs", ids));
  CHECK(!ids.empty());
  for (const std::string& id : ids) {
    LoadedPack lp;
    std::string err;
    bool ok = loadPack(st, id, lp, err);
    if (!ok) std::printf("  pack %s: %s\n", id.c_str(), err.c_str());
    CHECK(ok);
  }
}

TEST(extra_pack_folders_are_listed_and_readable) {
  namespace fsys = std::filesystem;
  fsys::path extra = fsys::temp_directory_path() / "deskpet_extra_packs";
  fsys::remove_all(extra);
  fsys::create_directories(extra / "ghosty");
  std::FILE* f = std::fopen((extra / "ghosty" / "manifest.json").string().c_str(), "w");
  std::fputs("{\"format\":1,\"id\":\"ghosty\",\"name\":\"Ghosty\"}", f);
  std::fclose(f);
  pc::DirStorage st(std::string(DESKPET_SOURCE_DIR) + "/fs");
  st.addPackDir(extra.string());
  std::vector<std::string> ids;
  CHECK(st.listDirs("/packs", ids));
  CHECK(std::find(ids.begin(), ids.end(), "ghosty") != ids.end());
  CHECK(std::find(ids.begin(), ids.end(), "blobby") != ids.end());
  LoadedPack lp;
  std::string err;
  CHECK(loadPack(st, "ghosty", lp, err) && lp.cfg.name == "Ghosty");
  CHECK(loadPack(st, "blobby", lp, err));
  std::string out;
  CHECK(!st.read("/packs/../../etc/passwd", out, 1000));
}

TEST(manifest_validation_rejects_bad_input) {
  PackConfig p;
  std::string err;
  const std::string base = "{\"format\":1,\"id\":\"x\",\"name\":\"X\"";
  CHECK(parse(base + "}", p, err));
  CHECK(!parse("{\"format\":2,\"id\":\"x\",\"name\":\"X\"}", p, err));
  CHECK(!parse("{\"format\":1,\"id\":\"../evil\",\"name\":\"X\"}", p, err));
  CHECK(!parse("{\"format\":1,\"id\":\"UPPER\",\"name\":\"X\"}", p, err));
  CHECK(!parse(base + ",\"creature\":{\"sprites\":\"../../secrets.json\"}}", p, err));
  CHECK(!parse(base + ",\"creature\":{\"sprites\":\"s.dps\"}}", p, err));  // no idle anim
  CHECK(!parse(base + ",\"rules\":[{\"stat\":\"mana\",\"below\":3,\"mood\":\"sad\"}]}", p, err));
  CHECK(!parse(base + ",\"rules\":[{\"stat\":\"food\",\"mood\":\"sad\"}]}", p, err));
  CHECK(!parse("[1,2,3]", p, err));
  CHECK(!parse("", p, err));
  std::string deep = base + ",\"x\":" + std::string(50, '[') + std::string(50, ']') + "}";
  CHECK(!parse(deep, p, err));
  std::string huge = base + ",\"pad\":\"" + std::string(kMaxManifestBytes, 'a') + "\"}";
  CHECK(!parse(huge, p, err));
}

TEST(manifest_clamps_numbers) {
  PackConfig p;
  std::string err;
  CHECK(parse("{\"format\":1,\"id\":\"x\",\"name\":\"X\","
              "\"decay\":{\"awake\":{\"food\":1e30,\"fun\":-4}}}",
              p, err));
  CHECK(p.tuning.foodDecayAwake == 100);
  CHECK(p.tuning.funDecayAwake == 0);
}

TEST(manifest_rules_override_mood) {
  PackConfig p;
  std::string err;
  CHECK(parse("{\"format\":1,\"id\":\"x\",\"name\":\"X\","
              "\"rules\":[{\"stat\":\"energy\",\"below\":50,\"mood\":\"sad\"}]}",
              p, err));
  Pet pet(p.tuning);
  pet.stats() = {90, 90, 40, 0, false};
  CHECK(pet.mood() == Mood::Sad);
  pet.stats().energy = 60;
  CHECK(pet.mood() == Mood::Happy);
}

static std::string makeImage(uint16_t w, uint16_t h, uint16_t frames) {
  std::string s = "DPS1";
  auto put16 = [&s](uint16_t v) { s += static_cast<char>(v & 0xFF); s += static_cast<char>(v >> 8); };
  put16(w); put16(h); put16(frames); put16(0);
  for (int i = 0; i < 16; ++i) put16(static_cast<uint16_t>(i * 0x1111));
  s += std::string(((w * h + 1) / 2) * frames, '\x12');
  return s;
}

TEST(image_parser_bounds) {
  Image4 img;
  std::string err;
  std::string ok = makeImage(3, 3, 2);
  CHECK(parseImage4(reinterpret_cast<const uint8_t*>(ok.data()), ok.size(), img, err));
  CHECK(img.w == 3 && img.frames == 2 && img.frameBytes() == 5);
  std::string trunc = ok.substr(0, ok.size() - 1);
  CHECK(!parseImage4(reinterpret_cast<const uint8_t*>(trunc.data()), trunc.size(), img, err));
  std::string big = makeImage(1000, 2, 1);
  CHECK(!parseImage4(reinterpret_cast<const uint8_t*>(big.data()), big.size(), img, err));
  std::string zero = makeImage(4, 4, 0);
  CHECK(!parseImage4(reinterpret_cast<const uint8_t*>(zero.data()), zero.size(), img, err));
  CHECK(!parseImage4(reinterpret_cast<const uint8_t*>("DPS1"), 4, img, err));
}

// Cheap deterministic fuzzing of the parsers (the real fuzz target would use
// libFuzzer; this keeps a smoke test in every build).
TEST(parsers_survive_random_mutation) {
  pc::DirStorage st(std::string(DESKPET_SOURCE_DIR) + "/fs");
  std::string seed;
  CHECK(st.read("/packs/blobby/manifest.json", seed, kMaxManifestBytes));
  Rng rng(42);
  for (int iter = 0; iter < 3000; ++iter) {
    std::string m = seed;
    int edits = rng.range(1, 8);
    for (int k = 0; k < edits; ++k) {
      size_t pos = static_cast<size_t>(rng.range(0, static_cast<int>(m.size()) - 1));
      switch (rng.range(0, 2)) {
        case 0: m[pos] = static_cast<char>(rng.range(0, 255)); break;
        case 1: m.erase(pos, static_cast<size_t>(rng.range(1, 20))); break;
        case 2: m.insert(pos, std::string(1, "{}[]\":,0-e"[rng.range(0, 9)])); break;
      }
    }
    PackConfig p;
    std::string err;
    parseManifest(m.data(), m.size(), p, err);
    std::vector<CalendarEvent> ev;
    parseCalendarJson(m.data(), m.size(), ev);
    Image4 img;
    std::string im = makeImage(4, 4, 1);
    im[static_cast<size_t>(rng.range(4, static_cast<int>(im.size()) - 1))] = static_cast<char>(rng.range(0, 255));
    parseImage4(reinterpret_cast<const uint8_t*>(im.data()), im.size(), img, err);
  }
  CHECK(true);
}


// ---------------------------------------------------------------- rooms

TEST(shipped_rooms_load) {
  pc::DirStorage st(std::string(DESKPET_SOURCE_DIR) + "/fs");
  std::vector<std::string> ids;
  CHECK(st.listDirs("/rooms", ids));
  CHECK(ids.size() >= 3);
  for (const std::string& id : ids) {
    LoadedRoom r;
    std::string err;
    bool ok = loadRoom(st, id, r, err);
    if (!ok) std::printf("  room %s: %s\n", id.c_str(), err.c_str());
    CHECK(ok);
  }
  LoadedRoom builtin;
  std::string err;
  pc::DirStorage empty("/nonexistent");
  CHECK(loadRoom(empty, "cozy", builtin, err));  // built in without files
  CHECK(!loadRoom(empty, "haunted", builtin, err));
}

TEST(room_json_round_trip_and_clamping) {
  RoomConfig r = defaultRoom();
  r.name = "Den";
  r.colors.wall = rgb(10, 200, 30);
  r.windowX = 100;
  std::string j = roomToJson(r);
  RoomConfig back;
  std::string err;
  CHECK(parseRoom(j.data(), j.size(), back, err));
  CHECK(back.name == "Den" && back.colors.wall == r.colors.wall && back.windowX == 100);
  CHECK(back.spotX(SceneId::House, "bed", 0) == 62);
  CHECK(roomToJson(back) == j);
  const std::string wild = R"({"format":1,"id":"x","name":"X","layout":{"bed":-500,"door":9999,"groundY":1e9}})";
  CHECK(parseRoom(wild.data(), wild.size(), back, err));
  CHECK(back.spotX(SceneId::House, "bed", 0) == 50);
  CHECK(back.spotX(SceneId::House, "door", 0) == 196);
  CHECK(back.scene(SceneId::Yard).groundY == 200);
}

TEST(room_validation_rejects_bad_input) {
  RoomConfig r;
  std::string err;
  auto bad = [&](const std::string& j) { return !parseRoom(j.data(), j.size(), r, err); };
  CHECK(bad(R"({"format":2,"id":"x","name":"X"})"));
  CHECK(bad(R"({"format":1,"id":"../x","name":"X"})"));
  CHECK(bad(R"({"format":1,"id":"x","name":"<script>\"x"})"));
  CHECK(bad(R"({"format":1,"id":"x","name":"X","colors":{"wall":"red"}})"));
  CHECK(bad(R"({"format":1,"id":"x","name":"X","backgrounds":{"house":"../../secrets.json"}})"));
  CHECK(bad(R"({"format":1,"id":"x","name":"X","backgroundsFrom":"../packs"})"));
  CHECK(bad(std::string(kMaxRoomBytes + 1, ' ')));
  CHECK(!bad(R"({"format":1,"id":"x","name":"X","colors":{"wall":"#112233"},"unknown":1})"));
}

TEST(day_phases_and_tints) {
  CHECK(dayPhaseForHour(6) == DayPhase::Dawn);
  CHECK(dayPhaseForHour(12) == DayPhase::Day);
  CHECK(dayPhaseForHour(18) == DayPhase::Dusk);
  CHECK(dayPhaseForHour(23) == DayPhase::Night);
  CHECK(dayPhaseForHour(2) == DayPhase::Night);
  Ambience day;
  CHECK(sceneTint(day, SceneId::House).identity());
  Ambience dark;
  dark.lightsOff = true;
  Tint t = sceneTint(dark, SceneId::House);
  CHECK(t.r < 128 && t.g < 128);
  CHECK(t.apply(rgb(255, 255, 255)) != rgb(255, 255, 255));
  Ambience rain;
  rain.weather = Weather::Rain;
  CHECK(!sceneTint(rain, SceneId::Yard).identity());
  Weather w;
  const char* feed = R"({"events":[],"weather":"snow"})";
  CHECK(parseWeatherJson(feed, std::strlen(feed), w) && w == Weather::Snow);
  const char* feed2 = R"({"weather":{"code":"rain","tempC":27}})";
  CHECK(parseWeatherJson(feed2, std::strlen(feed2), w) && w == Weather::Rain);
  CHECK(!parseWeatherJson("{\"weather\":\"lava\"}", 18, w));
}

TEST(canvas_tint_applies_to_shapes_and_images) {
  std::vector<Color> buf(4, 0);
  Canvas c(buf.data(), Rect(0, 0, 4, 1));
  Tint half;
  half.r = half.g = half.b = 128;
  c.setTint(half);
  c.fillRect(0, 0, 2, 1, rgb(255, 255, 255));
  CHECK(buf[0] == half.apply(rgb(255, 255, 255)));
  CHECK(buf[0] != rgb(255, 255, 255));
}

TEST(bad_weather_keeps_pet_inside) {
  PackConfig pack = defaultPack();
  pack.tuning.outingChancePct = 100;
  RoomConfig room = defaultRoom();
  World w;
  w.reset(room, 4);
  Pet p(pack.tuning);
  p.stats() = {90, 90, 90, 0, false};
  w.setBadWeather(true);
  for (int i = 0; i < 300; ++i) {
    w.tick(p);
    p.stats().fun = 90;
    if (w.view().scene != SceneId::House) break;
  }
  CHECK(w.view().scene == SceneId::House);
}

TEST(world_set_room_keeps_pet_in_range) {
  RoomConfig a = defaultRoom();
  World w;
  w.reset(a, 1);
  Pet p;
  w.requestSleep(p);
  for (int i = 0; i < 80; ++i) w.tick(p);
  CHECK(p.asleep());
  RoomConfig b = defaultRoom();
  std::string err;
  const std::string j = R"({"format":1,"id":"b","name":"B","layout":{"bed":100}})";
  CHECK(parseRoom(j.data(), j.size(), b, err));
  w.setRoom(b);
  CHECK(w.view().x == 100);  // sleeping pet moves with its bed
}

// --------------------------------------------------------------- canvas

TEST(canvas_clips_to_strip) {
  std::vector<Color> buf(20 * 4, 0);
  Canvas c(buf.data(), Rect(10, 10, 20, 4));
  c.fillRect(0, 0, 240, 240, 0xFFFF);
  for (Color px : buf) CHECK(px == 0xFFFF);
  c.fill(0);
  c.fillCircle(20, 100, 50, 0x1234);  // far outside below
  c.text(0, 12, "Hello world, this is long", 3, 0x00FF);
  c.line(-100, -100, 400, 400, 5, 0x0F0F);
  c.fillTriangle(-50, 0, 300, 5, 10, 400, 0x7777);
  CHECK(buf.size() == 80);  // no overflow (ASan would catch writes)
}

TEST(text_width) {
  CHECK(Canvas::textWidth("", 2) == 0);
  CHECK(Canvas::textWidth("A", 1) == 5);
  CHECK(Canvas::textWidth("AB", 2) == 22);
}

// --------------------------------------------------------------- engine

struct EngineRig {
  pc::FramebufferDisplay display;
  pc::ScriptInput input;
  pc::ManualClock clock;
  pc::DirStorage storage{std::string(DESKPET_SOURCE_DIR) + "/fs"};
  pc::QueueNotifications notes;
  std::unique_ptr<Engine> engine;
  std::string state;

  explicit EngineRig(const char* name) {
    state = (std::filesystem::temp_directory_path() / ("deskpet_test_" + std::string(name))).string();
    std::filesystem::remove_all(state);
    std::filesystem::create_directories(state);
    storage.setWriteRoot(state);
    clock.epochBase = 1791363600;
    Platform p;
    p.display = &display;
    p.input = &input;
    p.clock = &clock;
    p.storage = &storage;
    p.notifications = &notes;
    p.seed = 99;
    engine.reset(new Engine(p));
    engine->begin();
  }
  void run(uint32_t ms) {
    for (uint32_t t = 0; t < ms; t += 20) {
      clock.ms += 20;
      engine->loop();
    }
  }
};

TEST(engine_first_frame_is_full_then_dirty_rects_only) {
  EngineRig r("dirty");
  CHECK(r.display.pushedPixels == static_cast<uint64_t>(kScreenW) * kScreenH);
  uint64_t before = r.display.pushedPixels;
  r.run(Engine::kTickMs * 40);
  uint64_t perTick = (r.display.pushedPixels - before) / 40;
  CHECK(perTick > 0);
  CHECK(perTick < static_cast<uint64_t>(kScreenW) * kScreenH / 4);
}

TEST(engine_navigation_and_back) {
  EngineRig r("nav");
  Engine& e = *r.engine;
  const struct { Gesture open; Screen s; } nav[] = {
      {Gesture::SwipeLeft, Screen::Stats}, {Gesture::SwipeRight, Screen::Agenda},
      {Gesture::SwipeUp, Screen::Actions}, {Gesture::SwipeDown, Screen::Settings}};
  for (auto& n : nav) {
    e.handleGesture({n.open, 120, 120});
    CHECK(e.screen() == n.s);
    e.handleGesture({Gesture::Back, 120, 120});
    CHECK(e.screen() == Screen::Home);
  }
  e.handleGesture({Gesture::LongPress, 120, 120});
  CHECK(e.dnd());
  r.notes.queue.push_back({"a", "b", "c"});
  r.run(100);
  CHECK(e.alerts().active() == AlertKind::None);
  CHECK(e.alerts().missed() == 1);
}

TEST(engine_persists_settings_and_pet) {
  {
    EngineRig r("persist");
    r.engine->pet().stats().xp = 321;
    r.engine->handleGesture({Gesture::LongPress, 120, 120});  // DND on
    r.engine->save();
  }
  // Second boot in the same state dir.
  pc::FramebufferDisplay d;
  pc::ScriptInput in;
  pc::ManualClock clk;
  clk.epochBase = 1791363600 + 3600;  // one hour later
  pc::DirStorage st(std::string(DESKPET_SOURCE_DIR) + "/fs");
  st.setWriteRoot((std::filesystem::temp_directory_path() / "deskpet_test_persist").string());
  Platform p;
  p.display = &d;
  p.input = &in;
  p.clock = &clk;
  p.storage = &st;
  Engine e(p);
  e.begin();
  CHECK(e.pet().stats().xp == 321);
  CHECK(e.dnd());
  CHECK(e.pet().stats().food < 80);  // caught up on the hour away
}

TEST(engine_rejects_bad_pack_and_keeps_running) {
  EngineRig r("badpack");
  std::string before = r.engine->pack().cfg.id;
  CHECK(!r.engine->selectPack("does-not-exist"));
  CHECK(!r.engine->selectPack("../../etc"));
  CHECK(r.engine->pack().cfg.id == before);
  CHECK(!r.engine->lastPackError().empty());
  r.run(1000);
}

TEST(portal_live_room_edit_and_theme_switch) {
  EngineRig r("portal");
  Engine& e = *r.engine;
  pc::HttpPortal portal(r.storage, 0);
  portal.start(e);  // socket may fail on port 0 binding; handle() works regardless
  auto req = [&](const std::string& method, const std::string& target, const std::string& body,
                 const std::string& extra = "") {
    return portal.handle(method + " " + target + " HTTP/1.1\r\nHost: x\r\n" + extra +
                         "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body);
  };
  CHECK(req("GET", "/", "").find("Edit the room") != std::string::npos);
  CHECK(req("GET", "/rooms", "").find("haunted") != std::string::npos);
  CHECK(req("POST", "/room/select?id=haunted", "").find("200 OK") != std::string::npos);
  CHECK(e.room().cfg.id == "haunted");
  CHECK(req("POST", "/room/select?id=..%2Fetc", "").find("400") != std::string::npos);

  // Live edit: move the bed, change the wall colour.
  std::string room = req("GET", "/room", "");
  room = room.substr(room.find("\r\n\r\n") + 4);
  RoomConfig cfg;
  std::string err;
  CHECK(parseRoom(room.data(), room.size(), cfg, err));
  cfg.scene(SceneId::House).spots[0].x = 90;  // bed
  cfg.colors.wall = rgb(255, 0, 0);
  e.handleGesture({Gesture::SwipeDown, 120, 120});  // somewhere else
  CHECK(req("POST", "/room", roomToJson(cfg)).find("200 OK") != std::string::npos);
  CHECK(e.room().cfg.id == "custom");
  CHECK(e.room().cfg.colors.wall == rgb(255, 0, 0));
  CHECK(e.room().cfg.spotX(SceneId::House, "bed", 0) == 90);
  CHECK(e.screen() == Screen::Home);  // shows the change live
  CHECK(req("POST", "/room", "{\"format\":1}").find("400") != std::string::npos);
  CHECK(e.room().cfg.colors.wall == rgb(255, 0, 0));  // bad edit ignored

  // Uploads land in the right folder; unsafe names are refused.
  std::string mp = "--XYZ\r\nContent-Disposition: form-data; name=\"file\"; filename=\"room.json\"\r\n"
                   "Content-Type: application/json\r\n\r\n"
                   "{\"format\":1,\"id\":\"den\",\"name\":\"Den\"}\r\n--XYZ--\r\n";
  std::string ct = "Content-Type: multipart/form-data; boundary=XYZ\r\n";
  CHECK(req("POST", "/upload?kind=room&id=den", mp, ct).find("200 OK") != std::string::npos);
  CHECK(e.selectRoom("den"));
  CHECK(req("POST", "/upload?kind=room&id=..", mp, ct).find("400") != std::string::npos);
  CHECK(req("POST", "/upload?kind=exe&id=den", mp, ct).find("400") != std::string::npos);
  std::string path;
  CHECK(!portalUploadPath("pack", "x", "run.sh", path, err));
  CHECK(portalUploadPath("pack", "x", "sprites.dps", path, err) && path == "/packs/x/sprites.dps");
  portal.stop();
}

TEST(engine_rain_redraws_window_only_in_house) {
  EngineRig r("rain");
  Engine& e = *r.engine;
  e.setWeather(Weather::Rain);
  r.run(Engine::kTickMs * 2);
  CHECK(e.ambience().weather == Weather::Rain);
  uint32_t full = e.renderStats().fullRedraws;
  r.run(Engine::kTickMs * 20);
  if (e.world().view().scene == SceneId::House) CHECK(e.renderStats().fullRedraws == full);
}

int main() {
  int failedTests = 0;
  for (const TestCase& t : registry()) {
    int before = g_failures;
    t.fn();
    bool ok = g_failures == before;
    if (!ok) ++failedTests;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", t.name);
  }
  std::printf("\n%zu tests, %d failed\n", registry().size(), failedTests);
  return failedTests ? 1 : 0;
}
