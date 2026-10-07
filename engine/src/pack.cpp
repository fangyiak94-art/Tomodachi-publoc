#include "deskpet/pack.h"

#include <ArduinoJson.h>

#include <cstring>

namespace dp {

const Spot* SceneConfig::spot(const char* name) const {
  for (const Spot& s : spots)
    if (s.name == name) return &s;
  return nullptr;
}

const Anim* PackConfig::anim(const std::string& n) const {
  for (const Anim& a : anims)
    if (a.name == n) return &a;
  return nullptr;
}

const char* PackConfig::sound(const char* n) const {
  for (const auto& s : sounds)
    if (s.first == n) return s.second.c_str();
  return nullptr;
}

const EvolutionStage* PackConfig::stageFor(uint32_t level) const {
  const EvolutionStage* best = nullptr;
  for (const EvolutionStage& s : evolution)
    if (s.level <= level && (!best || s.level >= best->level)) best = &s;
  return best;
}

PackConfig defaultPack() {
  PackConfig p;
  SceneConfig& house = p.scenes[static_cast<int>(SceneId::House)];
  house.spots = {{"bed", 62, 182}, {"bowl", 150, 182}, {"door", 184, 182}};
  SceneConfig& yard = p.scenes[static_cast<int>(SceneId::Yard)];
  yard.spots = {{"door", 176, 182}, {"tree", 70, 182}};
  p.sounds = {
      {"meeting", "meeting:d=8,o=6,b=180:c,e,g,p,c,e,g"},
      {"notify", "notify:d=16,o=6,b=200:e,g"},
      {"happy", "happy:d=16,o=6,b=200:c,e,g,c7"},
  };
  p.evolution = {{1, "Blobby", 0, false}};
  return p;
}

bool isSafeFileName(const std::string& name) {
  if (name.empty() || name.size() > 32) return false;
  if (name[0] == '.') return false;
  for (char c : name) {
    bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' ||
              c == '.';
    if (!ok) return false;
  }
  return name.find("..") == std::string::npos;
}

namespace {

float num(JsonVariantConst v, float def, float lo, float hi) {
  if (!v.is<float>() && !v.is<int>() && !v.is<long>()) return def;
  float f = v.as<float>();
  if (!(f == f)) return def;  // NaN
  return f < lo ? lo : (f > hi ? hi : f);
}

bool parseColor(JsonVariantConst v, Color& out) {
  const char* s = v.as<const char*>();
  if (!s || std::strlen(s) != 7 || s[0] != '#') return false;
  uint32_t value = 0;
  for (int i = 1; i < 7; ++i) {
    char c = s[i];
    uint32_t d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else return false;
    value = value * 16 + d;
  }
  out = rgb((value >> 16) & 0xFF, (value >> 8) & 0xFF, value & 0xFF);
  return true;
}

bool boundedString(JsonVariantConst v, size_t maxLen, std::string& out) {
  const char* s = v.as<const char*>();
  if (!s) return false;
  size_t n = std::strlen(s);
  if (n == 0 || n > maxLen) return false;
  for (size_t i = 0; i < n; ++i)
    if (static_cast<unsigned char>(s[i]) < 0x20) return false;
  out.assign(s, n);
  return true;
}

bool optionalFile(JsonVariantConst v, std::string& out, std::string& err, const char* what) {
  if (v.isNull()) return true;
  std::string name;
  if (!boundedString(v, 32, name) || !isSafeFileName(name)) {
    err = std::string("bad file name for ") + what;
    return false;
  }
  out = name;
  return true;
}

bool parseStat(const char* s, Stat& out) {
  if (!s) return false;
  if (!std::strcmp(s, "food")) out = Stat::Food;
  else if (!std::strcmp(s, "fun")) out = Stat::Fun;
  else if (!std::strcmp(s, "energy")) out = Stat::Energy;
  else return false;
  return true;
}

bool parseScene(JsonVariantConst v, SceneConfig& sc, std::string& err, const char* name) {
  if (v.isNull()) return true;
  if (!optionalFile(v["background"], sc.background, err, name)) return false;
  JsonObjectConst spots = v["spots"];
  if (!spots.isNull()) {
    std::vector<Spot> parsed;
    for (JsonPairConst kv : spots) {
      if (parsed.size() >= 16) { err = "too many spots"; return false; }
      JsonArrayConst xy = kv.value();
      if (xy.size() != 2) { err = "spot must be [x, y]"; return false; }
      Spot s;
      s.name = kv.key().c_str();
      if (s.name.empty() || s.name.size() > 16) { err = "bad spot name"; return false; }
      s.x = static_cast<int16_t>(num(xy[0], 120, 0, kScreenW - 1));
      s.y = static_cast<int16_t>(num(xy[1], 182, 0, kScreenH - 1));
      parsed.push_back(s);
    }
    // Spots named in the manifest replace the defaults with the same name.
    for (const Spot& s : parsed) {
      bool replaced = false;
      for (Spot& d : sc.spots)
        if (d.name == s.name) { d = s; replaced = true; }
      if (!replaced) sc.spots.push_back(s);
    }
  }
  sc.groundY = static_cast<int16_t>(num(v["groundY"], sc.groundY, 60, kScreenH - 20));
  sc.minX = static_cast<int16_t>(num(v["minX"], sc.minX, 20, 220));
  sc.maxX = static_cast<int16_t>(num(v["maxX"], sc.maxX, sc.minX, 220));
  return true;
}

}  // namespace

bool parseManifest(const char* data, size_t len, PackConfig& out, std::string& err) {
  if (!data || len == 0 || len > kMaxManifestBytes) {
    err = "manifest missing or too large";
    return false;
  }
  JsonDocument doc;
  DeserializationError e =
      deserializeJson(doc, data, len, DeserializationOption::NestingLimit(6));
  if (e) {
    err = std::string("manifest JSON: ") + e.c_str();
    return false;
  }
  if (!doc.is<JsonObject>()) { err = "manifest must be an object"; return false; }
  if (doc["format"].as<int>() != 1) { err = "unsupported format (want 1)"; return false; }

  PackConfig p = defaultPack();
  std::string id;
  if (!boundedString(doc["id"], 24, id) || !isSafeFileName(id)) {
    err = "id must be 1-24 chars of a-z 0-9 _ - .";
    return false;
  }
  p.id = id;
  if (!boundedString(doc["name"], 24, p.name)) { err = "name missing"; return false; }
  boundedString(doc["version"], 16, p.version);

  // Creature look.
  JsonObjectConst cr = doc["creature"];
  parseColor(cr["body"], p.body);
  parseColor(cr["outline"], p.outline);
  parseColor(cr["cheek"], p.cheek);
  if (!optionalFile(cr["sprites"], p.sprites, err, "sprites")) return false;
  p.spriteScale = static_cast<uint8_t>(num(cr["scale"], 3, 1, 6));
  JsonObjectConst anims = cr["animations"];
  for (JsonPairConst kv : anims) {
    if (p.anims.size() >= 32) { err = "too many animations"; return false; }
    Anim a;
    a.name = kv.key().c_str();
    if (a.name.empty() || a.name.size() > 24) { err = "bad animation name"; return false; }
    JsonArrayConst frames = kv.value();
    if (frames.size() == 0 || frames.size() > 16) { err = "animation needs 1-16 frames"; return false; }
    for (JsonVariantConst f : frames) a.frames.push_back(static_cast<uint8_t>(num(f, 0, 0, 63)));
    p.anims.push_back(a);
  }
  if (!p.sprites.empty() && !p.anim("idle")) { err = "sprite packs need an 'idle' animation"; return false; }

  // Stats and behaviour.
  Tuning& t = p.tuning;
  JsonObjectConst awake = doc["decay"]["awake"];
  JsonObjectConst asleep = doc["decay"]["asleep"];
  t.foodDecayAwake = num(awake["food"], t.foodDecayAwake, 0, 100);
  t.funDecayAwake = num(awake["fun"], t.funDecayAwake, 0, 100);
  t.energyDecayAwake = num(awake["energy"], t.energyDecayAwake, 0, 100);
  t.foodDecayAsleep = num(asleep["food"], t.foodDecayAsleep, 0, 100);
  t.funDecayAsleep = num(asleep["fun"], t.funDecayAsleep, 0, 100);
  t.energyGainAsleep = num(asleep["energyGain"], t.energyGainAsleep, 0, 100);

  JsonObjectConst act = doc["actions"];
  t.feedFood = num(act["feed"]["food"], t.feedFood, 0, 100);
  t.petFun = num(act["pet"]["fun"], t.petFun, 0, 100);
  t.playFun = num(act["play"]["fun"], t.playFun, 0, 100);
  t.playEnergy = num(act["play"]["energyCost"], t.playEnergy, 0, 100);
  t.playXp = static_cast<uint32_t>(num(act["play"]["xp"], t.playXp, 0, 1000));
  t.walkXp = static_cast<uint32_t>(num(act["walk"]["xp"], t.walkXp, 0, 1000));
  t.meetingAckFun = num(act["meetingAck"]["fun"], t.meetingAckFun, 0, 100);
  t.meetingAckXp = static_cast<uint32_t>(num(act["meetingAck"]["xp"], t.meetingAckXp, 0, 1000));
  t.meetingIgnoredFun = num(act["meetingIgnored"]["funCost"], t.meetingIgnoredFun, 0, 100);

  JsonObjectConst mood = doc["mood"];
  t.hungryBelow = num(mood["hungryBelow"], t.hungryBelow, 0, 100);
  t.sadBelow = num(mood["sadBelow"], t.sadBelow, 0, 100);
  t.happyFunMin = num(mood["happyFunMin"], t.happyFunMin, 0, 100);
  t.happyFoodMin = num(mood["happyFoodMin"], t.happyFoodMin, 0, 100);
  t.warnBelow = num(mood["warnBelow"], t.warnBelow, 0, 100);

  JsonObjectConst beh = doc["behavior"];
  t.outingEnergyMin = num(beh["outingEnergyMin"], t.outingEnergyMin, 0, 100);
  t.tiredBelow = num(beh["tiredBelow"], t.tiredBelow, 0, 100);
  t.outingMaxTicks = static_cast<uint16_t>(num(beh["outingMaxTicks"], t.outingMaxTicks, 1, 10000));
  t.outingChancePct = static_cast<uint8_t>(num(beh["outingChancePct"], t.outingChancePct, 0, 100));
  t.autoSleepBelow = num(beh["autoSleepBelow"], t.autoSleepBelow, 0, 100);

  JsonArrayConst rules = doc["rules"];
  if (rules.size() > 16) { err = "too many rules"; return false; }
  for (JsonObjectConst r : rules) {
    MoodRule mr;
    if (!parseStat(r["stat"].as<const char*>(), mr.stat)) { err = "rule: bad stat"; return false; }
    if (!moodFromName(r["mood"].as<const char*>(), mr.mood) || mr.mood == Mood::Sleepy) {
      err = "rule: bad mood";
      return false;
    }
    if (!r["below"].isNull()) { mr.below = true; mr.value = num(r["below"], 0, 0, 100); }
    else if (!r["above"].isNull()) { mr.below = false; mr.value = num(r["above"], 100, 0, 100); }
    else { err = "rule needs 'below' or 'above'"; return false; }
    t.rules.push_back(mr);
  }

  JsonArrayConst evo = doc["evolution"];
  if (!evo.isNull()) {
    if (evo.size() > 8) { err = "too many evolution stages"; return false; }
    p.evolution.clear();
    for (JsonObjectConst s : evo) {
      EvolutionStage st;
      st.level = static_cast<uint32_t>(num(s["level"], 1, 1, 1000));
      if (!boundedString(s["name"], 24, st.name)) { err = "evolution stage needs a name"; return false; }
      st.hasBody = parseColor(s["body"], st.body);
      p.evolution.push_back(st);
    }
  }

  JsonObjectConst scenes = doc["scenes"];
  if (!parseScene(scenes["house"], p.scenes[0], err, "house")) return false;
  if (!parseScene(scenes["yard"], p.scenes[1], err, "yard")) return false;
  for (const SceneConfig& sc : p.scenes)
    if (!sc.spot("door")) { err = "every scene needs a 'door' spot"; return false; }
  if (!p.scene(SceneId::House).spot("bed") || !p.scene(SceneId::House).spot("bowl")) {
    err = "house needs 'bed' and 'bowl' spots";
    return false;
  }

  JsonObjectConst sounds = doc["sounds"];
  for (JsonPairConst kv : sounds) {
    std::string tune;
    if (!boundedString(kv.value(), 256, tune)) { err = "bad sound"; return false; }
    bool replaced = false;
    for (auto& s : p.sounds)
      if (s.first == kv.key().c_str()) { s.second = tune; replaced = true; }
    if (!replaced) {
      if (p.sounds.size() >= 16) { err = "too many sounds"; return false; }
      p.sounds.emplace_back(kv.key().c_str(), tune);
    }
  }

  out = p;
  return true;
}

namespace {
uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
}  // namespace

bool parseImage4(const uint8_t* data, size_t len, Image4& out, std::string& err) {
  // Header: "DPS1" w:u16 h:u16 frames:u16 reserved:u16 palette:16*u16 (RGB565 LE)
  const size_t header = kImageHeaderBytes;
  if (!data || len < header || std::memcmp(data, "DPS1", 4) != 0) {
    err = "not a DPS1 image";
    return false;
  }
  Image4 img;
  img.w = rd16(data + 4);
  img.h = rd16(data + 6);
  img.frames = rd16(data + 8);
  if (img.w == 0 || img.h == 0 || img.w > kScreenW || img.h > kScreenH || img.frames == 0 ||
      img.frames > 64) {
    err = "image dimensions out of range";
    return false;
  }
  for (int i = 0; i < 16; ++i) img.palette[i] = rd16(data + 12 + i * 2);
  uint64_t need = header + static_cast<uint64_t>(img.frameBytes()) * img.frames;
  if (len != need) {
    err = "image size does not match header";
    return false;
  }
  img.data = data + header;
  out = img;
  return true;
}

namespace {
bool loadImage(Storage& st, const std::string& path, size_t maxBytes, std::string& blob,
               Image4& img, std::string& err) {
  if (!st.read(path.c_str(), blob, maxBytes)) {
    err = "cannot read " + path;
    return false;
  }
  if (!parseImage4(reinterpret_cast<const uint8_t*>(blob.data()), blob.size(), img, err)) {
    err = path + ": " + err;
    return false;
  }
  return true;
}
}  // namespace

void LoadedPack::rebind() {
  if (hasSprite) sprite.data = reinterpret_cast<const uint8_t*>(spriteBlob.data()) + kImageHeaderBytes;
  for (int i = 0; i < kSceneCount; ++i)
    if (hasBg[i]) bg[i].data = reinterpret_cast<const uint8_t*>(bgBlob[i].data()) + kImageHeaderBytes;
}

bool loadPack(Storage& storage, const std::string& id, LoadedPack& out, std::string& err) {
  out = LoadedPack();
  out.cfg = defaultPack();
  if (!isSafeFileName(id)) {
    err = "bad pack id";
    return false;
  }
  const std::string dir = "/packs/" + id + "/";
  std::string manifest;
  if (!storage.read((dir + "manifest.json").c_str(), manifest, kMaxManifestBytes)) {
    err = "cannot read " + dir + "manifest.json";
    return false;
  }
  LoadedPack p;
  if (!parseManifest(manifest.data(), manifest.size(), p.cfg, err)) return false;

  if (!p.cfg.sprites.empty()) {
    if (!loadImage(storage, dir + p.cfg.sprites, kMaxSpriteBytes, p.spriteBlob, p.sprite, err))
      return false;
    for (const Anim& a : p.cfg.anims)
      for (uint8_t f : a.frames)
        if (f >= p.sprite.frames) {
          err = "animation '" + a.name + "' uses a missing frame";
          return false;
        }
    p.hasSprite = true;
  }
  for (int i = 0; i < kSceneCount; ++i) {
    const std::string& bg = p.cfg.scenes[i].background;
    if (bg.empty()) continue;
    if (!loadImage(storage, dir + bg, kMaxBackgroundBytes, p.bgBlob[i], p.bg[i], err))
      return false;
    p.hasBg[i] = true;
  }
  out = std::move(p);
  out.rebind();
  return true;
}

}  // namespace dp
