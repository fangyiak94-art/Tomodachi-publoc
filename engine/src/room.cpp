#include "deskpet/room.h"

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>

#include "deskpet/pack.h"  // isSafeFileName, parseImage4, kMaxBackgroundBytes

namespace dp {

const Spot* SceneConfig::spot(const char* name) const {
  for (const Spot& s : spots)
    if (s.name == name) return &s;
  return nullptr;
}

int16_t RoomConfig::spotX(SceneId s, const char* name, int16_t def) const {
  const Spot* sp = scene(s).spot(name);
  return sp ? sp->x : def;
}

const std::vector<ColorField>& roomColorFields() {
  static const std::vector<ColorField> fields = {
      {"wall", &RoomColors::wall},       {"wainscot", &RoomColors::wainscot},
      {"trim", &RoomColors::trim},       {"floor", &RoomColors::floor},
      {"plank", &RoomColors::plank},     {"frame", &RoomColors::frame},
      {"door", &RoomColors::door},       {"bed", &RoomColors::bed},
      {"blanket", &RoomColors::blanket}, {"pillow", &RoomColors::pillow},
      {"bowl", &RoomColors::bowl},       {"grass", &RoomColors::grass},
      {"houseWall", &RoomColors::houseWall}, {"roof", &RoomColors::roof},
      {"leaves", &RoomColors::leaves},   {"trunk", &RoomColors::trunk},
  };
  return fields;
}

namespace {

void setLayout(RoomConfig& r, int groundY, int bed, int bowl, int door, int yardDoor, int tree) {
  for (SceneConfig& sc : r.scenes) {
    sc.groundY = static_cast<int16_t>(groundY);
    sc.spots.clear();
  }
  auto g = static_cast<int16_t>(groundY);
  r.scene(SceneId::House).spots = {{"bed", static_cast<int16_t>(bed), g},
                                   {"bowl", static_cast<int16_t>(bowl), g},
                                   {"door", static_cast<int16_t>(door), g}};
  r.scene(SceneId::Yard).spots = {{"door", static_cast<int16_t>(yardDoor), g},
                                  {"tree", static_cast<int16_t>(tree), g}};
}

int clampInt(JsonVariantConst v, int def, int lo, int hi) {
  if (!v.is<int>() && !v.is<float>()) return def;
  float f = v.as<float>();
  if (!(f == f)) return def;
  int i = static_cast<int>(f);
  return i < lo ? lo : (i > hi ? hi : i);
}

bool parseHex(const char* s, Color& out) {
  if (!s || std::strlen(s) != 7 || s[0] != '#') return false;
  uint32_t v = 0;
  for (int i = 1; i < 7; ++i) {
    char c = s[i];
    uint32_t d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else return false;
    v = v * 16 + d;
  }
  out = rgb((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF);
  return true;
}

std::string toHex(Color c) {
  unsigned r5 = (c >> 11) & 31, g6 = (c >> 5) & 63, b5 = c & 31;
  char buf[8];
  std::snprintf(buf, sizeof(buf), "#%02X%02X%02X", (r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4),
                (b5 << 3) | (b5 >> 2));
  return buf;
}

bool safeName(JsonVariantConst v, size_t maxLen, std::string& out) {
  const char* s = v.as<const char*>();
  if (!s) return false;
  size_t n = std::strlen(s);
  if (n == 0 || n > maxLen) return false;
  for (size_t i = 0; i < n; ++i)
    if (static_cast<unsigned char>(s[i]) < 0x20 || s[i] == '"' || s[i] == '\\') return false;
  out.assign(s, n);
  return true;
}

}  // namespace

RoomConfig defaultRoom() {
  RoomConfig r;
  setLayout(r, 182, 62, 150, 184, 176, 70);
  return r;
}

bool parseRoom(const char* data, size_t len, RoomConfig& out, std::string& err) {
  if (!data || len == 0 || len > kMaxRoomBytes) {
    err = "room.json missing or too large";
    return false;
  }
  JsonDocument doc;
  if (DeserializationError e = deserializeJson(doc, data, len, DeserializationOption::NestingLimit(4))) {
    err = std::string("room JSON: ") + e.c_str();
    return false;
  }
  if (!doc.is<JsonObject>() || doc["format"].as<int>() != 1) {
    err = "room needs \"format\": 1";
    return false;
  }
  RoomConfig r = defaultRoom();
  std::string id;
  if (!safeName(doc["id"], 24, id) || !isSafeFileName(id)) {
    err = "room id must be 1-24 chars of a-z 0-9 _ - .";
    return false;
  }
  r.id = id;
  if (!safeName(doc["name"], 24, r.name)) {
    err = "room name missing";
    return false;
  }

  JsonObjectConst colors = doc["colors"];
  for (const ColorField& f : roomColorFields()) {
    JsonVariantConst v = colors[f.name];
    if (v.isNull()) continue;
    if (!parseHex(v.as<const char*>(), r.colors.*f.member)) {
      err = std::string("bad colour for ") + f.name;
      return false;
    }
  }

  JsonObjectConst lay = doc["layout"];
  int ground = clampInt(lay["groundY"], 182, 150, 200);
  int bed = clampInt(lay["bed"], 62, 50, 120);
  int bowl = clampInt(lay["bowl"], 150, 90, 190);
  int door = clampInt(lay["door"], 184, 120, 196);
  int yardDoor = clampInt(lay["yardDoor"], 176, 120, 190);
  int tree = clampInt(lay["tree"], 70, 40, 120);
  setLayout(r, ground, bed, bowl, door, yardDoor, tree);
  JsonArrayConst win = lay["window"];
  if (win.size() == 2) {
    r.windowX = static_cast<int16_t>(clampInt(win[0], 84, 30, 150));
    r.windowY = static_cast<int16_t>(clampInt(win[1], 46, 20, ground - 100));
  }

  JsonObjectConst bgs = doc["backgrounds"];
  const char* names[kSceneCount] = {"house", "yard"};
  for (int i = 0; i < kSceneCount; ++i) {
    JsonVariantConst v = bgs[names[i]];
    if (v.isNull()) continue;
    std::string file;
    if (!safeName(v, 32, file) || !isSafeFileName(file)) {
      err = std::string("bad background file for ") + names[i];
      return false;
    }
    r.scenes[i].background = file;
  }
  if (!doc["backgroundsFrom"].isNull()) {
    std::string from;
    if (!safeName(doc["backgroundsFrom"], 24, from) || !isSafeFileName(from)) {
      err = "bad backgroundsFrom";
      return false;
    }
    r.backgroundsFrom = from;
  }
  out = r;
  return true;
}

std::string roomToJson(const RoomConfig& r) {
  JsonDocument doc;
  doc["format"] = 1;
  doc["id"] = r.id;
  doc["name"] = r.name;
  JsonObject colors = doc["colors"].to<JsonObject>();
  for (const ColorField& f : roomColorFields()) colors[f.name] = toHex(r.colors.*f.member);
  JsonObject lay = doc["layout"].to<JsonObject>();
  lay["groundY"] = r.scene(SceneId::House).groundY;
  JsonArray win = lay["window"].to<JsonArray>();
  win.add(r.windowX);
  win.add(r.windowY);
  lay["bed"] = r.spotX(SceneId::House, "bed", 62);
  lay["bowl"] = r.spotX(SceneId::House, "bowl", 150);
  lay["door"] = r.spotX(SceneId::House, "door", 184);
  lay["yardDoor"] = r.spotX(SceneId::Yard, "door", 176);
  lay["tree"] = r.spotX(SceneId::Yard, "tree", 70);
  const char* names[kSceneCount] = {"house", "yard"};
  bool anyBg = false;
  for (int i = 0; i < kSceneCount; ++i) {
    if (r.scenes[i].background.empty()) continue;
    doc["backgrounds"][names[i]] = r.scenes[i].background;
    anyBg = true;
  }
  if (anyBg) doc["backgroundsFrom"] = r.backgroundsFrom.empty() ? r.id : r.backgroundsFrom;
  std::string out;
  serializeJson(doc, out);
  return out;
}

void LoadedRoom::rebind() {
  for (int i = 0; i < kSceneCount; ++i)
    if (hasBg[i]) bg[i].data = reinterpret_cast<const uint8_t*>(bgBlob[i].data()) + kImageHeaderBytes;
}

bool loadRoom(Storage& storage, const std::string& id, LoadedRoom& out, std::string& err) {
  out = LoadedRoom();
  out.cfg = defaultRoom();
  if (!isSafeFileName(id)) {
    err = "bad room id";
    return false;
  }
  std::string json;
  if (!storage.read(("/rooms/" + id + "/room.json").c_str(), json, kMaxRoomBytes)) {
    if (id == "cozy") return true;  // built-in
    err = "cannot read /rooms/" + id + "/room.json";
    return false;
  }
  LoadedRoom r;
  if (!parseRoom(json.data(), json.size(), r.cfg, err)) return false;
  const std::string dir =
      "/rooms/" + (r.cfg.backgroundsFrom.empty() ? id : r.cfg.backgroundsFrom) + "/";
  for (int i = 0; i < kSceneCount; ++i) {
    const std::string& file = r.cfg.scenes[i].background;
    if (file.empty()) continue;
    std::string path = dir + file;
    if (!storage.read(path.c_str(), r.bgBlob[i], kMaxBackgroundBytes)) {
      err = "cannot read " + path;
      return false;
    }
    std::string e;
    if (!parseImage4(reinterpret_cast<const uint8_t*>(r.bgBlob[i].data()), r.bgBlob[i].size(),
                     r.bg[i], e)) {
      err = path + ": " + e;
      return false;
    }
    r.hasBg[i] = true;
  }
  out = std::move(r);
  out.rebind();
  return true;
}

// -------------------------------------------------------------- ambience

const char* weatherName(Weather w) {
  switch (w) {
    case Weather::Unknown: return "unknown";
    case Weather::Clear: return "clear";
    case Weather::Cloudy: return "cloudy";
    case Weather::Rain: return "rain";
    case Weather::Snow: return "snow";
  }
  return "unknown";
}

bool weatherFromName(const char* s, Weather& out) {
  if (!s) return false;
  static const Weather all[] = {Weather::Unknown, Weather::Clear, Weather::Cloudy, Weather::Rain,
                                Weather::Snow};
  for (Weather w : all)
    if (!std::strcmp(s, weatherName(w))) {
      out = w;
      return true;
    }
  return false;
}

DayPhase dayPhaseForHour(int hour) {
  if (hour >= 5 && hour < 7) return DayPhase::Dawn;
  if (hour >= 7 && hour < 17) return DayPhase::Day;
  if (hour >= 17 && hour < 19) return DayPhase::Dusk;
  return DayPhase::Night;
}

Tint sceneTint(const Ambience& a, SceneId scene) {
  auto t = [](int r, int g, int b) {
    Tint x;
    x.r = static_cast<uint16_t>(r);
    x.g = static_cast<uint16_t>(g);
    x.b = static_cast<uint16_t>(b);
    return x;
  };
  if (scene == SceneId::House) {
    if (a.lightsOff) return t(92, 104, 176);
    switch (a.phase) {
      case DayPhase::Dawn: return t(256, 240, 226);
      case DayPhase::Day: return a.precipitation() ? t(226, 230, 240) : Tint();
      case DayPhase::Dusk: return t(256, 226, 200);
      case DayPhase::Night: return t(236, 214, 176);  // warm lamp light
    }
    return Tint();
  }
  Tint base;
  switch (a.phase) {
    case DayPhase::Dawn: base = t(250, 214, 200); break;
    case DayPhase::Day: base = Tint(); break;
    case DayPhase::Dusk: base = t(256, 186, 156); break;
    case DayPhase::Night: base = t(104, 116, 184); break;
  }
  if (a.weather == Weather::Cloudy) base = base.blend(t(200, 206, 222), 160);
  if (a.precipitation()) base = base.blend(t(176, 186, 210), 200);
  return base;
}

}  // namespace dp
