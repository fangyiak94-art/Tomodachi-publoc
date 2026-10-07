// The pet's body in the world: position, scene, goals and autonomous
// behaviour. Advanced in fixed ticks (~280 ms). Pure logic, no drawing.
#pragma once
#include <cstdint>
#include <vector>

#include "deskpet/pet.h"
#include "deskpet/room.h"
#include "deskpet/types.h"

namespace dp {

enum class Activity : uint8_t { Idle, Walking, Eating, Sleeping, Playing };

// Read-only snapshot the renderer uses.
struct ActorView {
  SceneId scene = SceneId::House;
  int16_t x = 120;
  int16_t groundY = 182;
  int8_t facing = 1;  // -1 left, +1 right
  Activity activity = Activity::Idle;
  uint8_t walkFrame = 0;  // two-frame walk cycle
  int8_t hop = 0;         // pixels above ground
  bool blinking = false;
  bool lookingUp = false;
  bool heart = false;
  uint8_t heartAge = 0;
  bool bowlFull = false;
  uint32_t tick = 0;
};

class World {
 public:
  void reset(const RoomConfig& room, uint32_t seed);
  // Swap the room theme live. Keeps the pet where it is (clamped to the
  // new room's walkable range).
  void setRoom(const RoomConfig& room);
  // Rain or snow: no outings, and a pet that is outside comes home.
  void setBadWeather(bool bad) { badWeather_ = bad; }

  // One behaviour step. May change pet stats (eating, playing...).
  void tick(Pet& pet);

  // Player / engine requests.
  void requestFeed(Pet& pet);
  void requestPlay(Pet& pet);
  void requestSleep(Pet& pet);
  void requestWalk(Pet& pet);  // go outside
  void requestHome(Pet& pet);
  void wake(Pet& pet);
  void tapped(Pet& pet);
  void notified();  // react to a phone notification: jump and look up

  const ActorView& view() const { return v_; }
  bool busy() const { return !plan_.empty(); }
  bool sceneChangedSinceLastCheck();
  uint16_t outingTicks() const { return outingTicks_; }
  // Where the pet was put to bed / restored to on boot.
  void placeInBed();

 private:
  enum class StepKind : uint8_t { WalkTo, Enter, Eat, Sleep, Play };
  struct Step {
    StepKind kind;
    int16_t arg;  // x target, scene id, or tick count
  };

  int speedFor(Mood m) const;
  void startOuting(Pet& pet);
  void goHome();  // queue steps to get back into the house
  int16_t spotX(SceneId scene, const char* name, int16_t def) const;
  void idleBehaviour(Pet& pet);
  bool walkToward(int16_t target, int speed);

  const RoomConfig* room_ = nullptr;
  bool badWeather_ = false;
  Rng rng_;
  ActorView v_;
  std::vector<Step> plan_;
  uint16_t stepTicks_ = 0;
  uint16_t pauseTicks_ = 0;
  int16_t wanderTarget_ = -1;
  uint16_t outingTicks_ = 0;
  uint8_t jumpTicks_ = 0;
  uint8_t lookTicks_ = 0;
  bool sceneChanged_ = false;
};

}  // namespace dp
