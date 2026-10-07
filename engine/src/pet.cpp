#include "deskpet/pet.h"

#include <ArduinoJson.h>

#include <cstring>

namespace dp {

const char* moodName(Mood m) {
  switch (m) {
    case Mood::Sleepy: return "sleepy";
    case Mood::Hungry: return "hungry";
    case Mood::Sad: return "sad";
    case Mood::Happy: return "happy";
    case Mood::Neutral: return "neutral";
  }
  return "neutral";
}

bool moodFromName(const char* s, Mood& out) {
  if (!s) return false;
  static const Mood all[] = {Mood::Sleepy, Mood::Hungry, Mood::Sad, Mood::Happy,
                             Mood::Neutral};
  for (Mood m : all) {
    if (std::strcmp(s, moodName(m)) == 0) {
      out = m;
      return true;
    }
  }
  return false;
}

void Pet::elapse(float hours) {
  if (hours <= 0) return;
  const Tuning& t = tuning_;
  if (s_.asleep) {
    s_.food = clamp(s_.food - t.foodDecayAsleep * hours);
    s_.fun = clamp(s_.fun - t.funDecayAsleep * hours);
    s_.energy = clamp(s_.energy + t.energyGainAsleep * hours);
  } else {
    s_.food = clamp(s_.food - t.foodDecayAwake * hours);
    s_.fun = clamp(s_.fun - t.funDecayAwake * hours);
    s_.energy = clamp(s_.energy - t.energyDecayAwake * hours);
  }
}

float Pet::statValue(Stat s) const {
  switch (s) {
    case Stat::Food: return s_.food;
    case Stat::Fun: return s_.fun;
    case Stat::Energy: return s_.energy;
  }
  return 0;
}

Mood Pet::mood() const {
  // First match wins.
  if (s_.asleep) return Mood::Sleepy;
  for (const MoodRule& r : tuning_.rules) {
    float v = statValue(r.stat);
    if (r.below ? v < r.value : v > r.value) return r.mood;
  }
  if (s_.food < tuning_.hungryBelow) return Mood::Hungry;
  if (s_.fun < tuning_.sadBelow) return Mood::Sad;
  if (s_.fun >= tuning_.happyFunMin && s_.food >= tuning_.happyFoodMin) return Mood::Happy;
  return Mood::Neutral;
}

void Pet::feed() { s_.food = clamp(s_.food + tuning_.feedFood); }
void Pet::petted() { s_.fun = clamp(s_.fun + tuning_.petFun); }
void Pet::played() {
  s_.fun = clamp(s_.fun + tuning_.playFun);
  s_.energy = clamp(s_.energy - tuning_.playEnergy);
  s_.xp += tuning_.playXp;
}
void Pet::walked() { s_.xp += tuning_.walkXp; }
void Pet::meetingAck() {
  s_.fun = clamp(s_.fun + tuning_.meetingAckFun);
  s_.xp += tuning_.meetingAckXp;
}
void Pet::meetingIgnored() { s_.fun = clamp(s_.fun - tuning_.meetingIgnoredFun); }

std::string Pet::toJson(int64_t savedAt) const {
  JsonDocument doc;
  doc["v"] = 1;
  doc["food"] = s_.food;
  doc["fun"] = s_.fun;
  doc["energy"] = s_.energy;
  doc["xp"] = s_.xp;
  doc["asleep"] = s_.asleep;
  doc["savedAt"] = savedAt;
  std::string out;
  serializeJson(doc, out);
  return out;
}

bool Pet::fromJson(const std::string& json, int64_t& savedAt) {
  JsonDocument doc;
  if (deserializeJson(doc, json)) return false;
  if (doc["v"].as<int>() != 1) return false;
  PetStats s;
  s.food = clamp(doc["food"] | s.food);
  s.fun = clamp(doc["fun"] | s.fun);
  s.energy = clamp(doc["energy"] | s.energy);
  s.xp = doc["xp"] | 0u;
  s.asleep = doc["asleep"] | false;
  savedAt = doc["savedAt"] | int64_t(0);
  s_ = s;
  return true;
}

}  // namespace dp
