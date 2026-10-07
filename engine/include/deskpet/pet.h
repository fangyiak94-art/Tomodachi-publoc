// Pet stats, decay and mood. Pure logic: no drawing, no time source.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dp {

enum class Mood : uint8_t { Sleepy, Hungry, Sad, Happy, Neutral };
const char* moodName(Mood m);
bool moodFromName(const char* s, Mood& out);

enum class Stat : uint8_t { Food, Fun, Energy };

// Optional pack rule evaluated before the default mood table:
// "when <stat> below/above <value> -> mood".
struct MoodRule {
  Stat stat = Stat::Food;
  bool below = true;
  float value = 0;
  Mood mood = Mood::Neutral;
};

// All numbers come from the pack manifest; defaults match the v0 spec.
struct Tuning {
  // Per hour.
  float foodDecayAwake = 10, foodDecayAsleep = 5;
  float funDecayAwake = 5, funDecayAsleep = 0;
  float energyDecayAwake = 8, energyGainAsleep = 20;
  // Actions.
  float feedFood = 25, petFun = 5, playFun = 20, playEnergy = 15;
  float meetingAckFun = 5, meetingIgnoredFun = 15;
  uint32_t meetingAckXp = 10, playXp = 5, walkXp = 5;
  // Mood thresholds.
  float hungryBelow = 30, sadBelow = 35, happyFunMin = 70, happyFoodMin = 50;
  float warnBelow = 30;  // stat bar turns amber
  // Behaviour.
  float outingEnergyMin = 40;   // happy pet with more energy may go out
  float tiredBelow = 30;        // comes home when energy drops below
  uint16_t outingMaxTicks = 45;
  uint8_t outingChancePct = 2;  // per idle tick
  float autoSleepBelow = 10;
  std::vector<MoodRule> rules;
};

struct PetStats {
  float food = 80, fun = 70, energy = 90;
  uint32_t xp = 0;
  bool asleep = false;
  uint16_t candies = 0;   // Rare Candies in the bag
  uint8_t stage = 0;      // evolution stage (index into the pack's stages)
  uint16_t meetingAcks = 0;
  int32_t treatDay = -1;  // local day number of the last daily candy
  bool holdForm = false;  // devolved on purpose: no automatic evolution
};

class Pet {
 public:
  Pet() = default;
  explicit Pet(const Tuning& t) : tuning_(t) {}

  void setTuning(const Tuning& t) { tuning_ = t; }
  const Tuning& tuning() const { return tuning_; }

  PetStats& stats() { return s_; }
  const PetStats& stats() const { return s_; }

  // Advance time. Hours may be fractional.
  void elapse(float hours);

  Mood mood() const;
  uint32_t level() const { return s_.xp / 100 + 1; }
  bool asleep() const { return s_.asleep; }
  bool isLow(float v) const { return v < tuning_.warnBelow; }

  void feed();
  void petted();
  void played();
  void walked();
  void meetingAck();
  // Rare Candy: straight to the start of the next level. False if none left.
  bool useRareCandy();
  void meetingIgnored();
  void sleep() { s_.asleep = true; }
  void wake() { s_.asleep = false; }

  // Save transfer (JSON). `savedAt` is unix seconds (0 if unknown).
  std::string toJson(int64_t savedAt) const;
  bool fromJson(const std::string& json, int64_t& savedAt);

 private:
  static float clamp(float v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }
  float statValue(Stat s) const;
  Tuning tuning_;
  PetStats s_;
};

}  // namespace dp
