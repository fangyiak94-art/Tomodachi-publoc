#include "deskpet/world.h"

#include <cstdlib>

namespace dp {

namespace {
constexpr int8_t kJumpCurve[] = {6, 12, 14, 9, 3};
constexpr uint8_t kJumpLen = sizeof(kJumpCurve);
constexpr uint8_t kHeartTicks = 7;
constexpr uint16_t kEatTicks = 6;
constexpr uint16_t kPlayTicks = 10;
}  // namespace

void World::setRoom(const RoomConfig& room) {
  room_ = &room;
  const SceneConfig& sc = room.scene(v_.scene);
  v_.groundY = sc.groundY;
  if (v_.activity == Activity::Sleeping) {
    v_.x = spotX(SceneId::House, "bed", 60);
  } else {
    if (v_.x < sc.minX) v_.x = sc.minX;
    if (v_.x > sc.maxX) v_.x = sc.maxX;
  }
  plan_.clear();
  wanderTarget_ = -1;
  sceneChanged_ = true;
}

void World::reset(const RoomConfig& room, uint32_t seed) {
  room_ = &room;
  rng_ = Rng(seed);
  v_ = ActorView();
  v_.groundY = room.scene(SceneId::House).groundY;
  v_.x = (room.scene(SceneId::House).minX + room.scene(SceneId::House).maxX) / 2;
  plan_.clear();
  stepTicks_ = pauseTicks_ = outingTicks_ = 0;
  jumpTicks_ = lookTicks_ = 0;
  wanderTarget_ = -1;
  sceneChanged_ = true;
}

bool World::sceneChangedSinceLastCheck() {
  bool c = sceneChanged_;
  sceneChanged_ = false;
  return c;
}

int16_t World::spotX(SceneId scene, const char* name, int16_t def) const {
  const Spot* s = room_->scene(scene).spot(name);
  return s ? s->x : def;
}

void World::placeInBed() {
  plan_.clear();
  v_.scene = SceneId::House;
  v_.groundY = room_->scene(SceneId::House).groundY;
  v_.x = spotX(SceneId::House, "bed", 60);
  v_.activity = Activity::Sleeping;
  sceneChanged_ = true;
}

int World::speedFor(Mood m) const {
  switch (m) {
    case Mood::Happy: return 7;
    case Mood::Hungry:
    case Mood::Sad: return 3;
    default: return 5;
  }
}

bool World::walkToward(int16_t target, int speed) {
  int dx = target - v_.x;
  if (dx == 0) return true;
  v_.facing = dx > 0 ? 1 : -1;
  if (std::abs(dx) <= speed) {
    v_.x = target;
  } else {
    v_.x = static_cast<int16_t>(v_.x + v_.facing * speed);
  }
  v_.activity = Activity::Walking;
  v_.walkFrame ^= 1;
  return v_.x == target;
}

void World::goHome() {
  if (v_.scene == SceneId::Yard) {
    plan_.push_back({StepKind::WalkTo, spotX(SceneId::Yard, "door", 176)});
    plan_.push_back({StepKind::Enter, static_cast<int16_t>(SceneId::House)});
  }
}

void World::requestFeed(Pet& pet) {
  if (pet.asleep()) wake(pet);
  plan_.clear();
  v_.bowlFull = true;
  goHome();
  plan_.push_back({StepKind::WalkTo, spotX(SceneId::House, "bowl", 150)});
  plan_.push_back({StepKind::Eat, kEatTicks});
}

void World::requestPlay(Pet& pet) {
  if (pet.asleep()) wake(pet);
  plan_.clear();
  plan_.push_back({StepKind::Play, kPlayTicks});
}

void World::requestSleep(Pet& pet) {
  if (pet.asleep()) return;
  plan_.clear();
  goHome();
  plan_.push_back({StepKind::WalkTo, spotX(SceneId::House, "bed", 60)});
  plan_.push_back({StepKind::Sleep, 0});
}

void World::requestWalk(Pet& pet) {
  if (pet.asleep()) wake(pet);
  if (v_.scene == SceneId::Yard) {
    tapped(pet);  // already outside: a happy hop
    return;
  }
  plan_.clear();
  plan_.push_back({StepKind::WalkTo, spotX(SceneId::House, "door", 184)});
  plan_.push_back({StepKind::Enter, static_cast<int16_t>(SceneId::Yard)});
  const SceneConfig& yard = room_->scene(SceneId::Yard);
  plan_.push_back({StepKind::WalkTo, static_cast<int16_t>(rng_.range(yard.minX, yard.maxX - 30))});
}

void World::requestHome(Pet& pet) {
  (void)pet;
  if (v_.scene != SceneId::Yard) return;
  plan_.clear();
  goHome();
}

void World::wake(Pet& pet) {
  if (!pet.asleep()) return;
  pet.wake();
  plan_.clear();
  v_.activity = Activity::Idle;
  jumpTicks_ = kJumpLen;  // hops out of bed
  pauseTicks_ = 3;
}

void World::tapped(Pet& pet) {
  if (pet.asleep()) return;
  pet.petted();
  jumpTicks_ = kJumpLen;
  v_.heart = true;
  v_.heartAge = 0;
}

void World::notified() {
  if (v_.activity == Activity::Sleeping) return;
  jumpTicks_ = kJumpLen;
  lookTicks_ = 8;
}

void World::startOuting(Pet& pet) {
  requestWalk(pet);
}

void World::idleBehaviour(Pet& pet) {
  const Tuning& t = pet.tuning();
  const Mood mood = pet.mood();
  const SceneConfig& sc = room_->scene(v_.scene);

  // Autonomy first.
  if (pet.stats().energy < t.autoSleepBelow) {
    requestSleep(pet);
    return;
  }
  if (v_.scene == SceneId::House) {
    if (!badWeather_ && mood == Mood::Happy && pet.stats().energy > t.outingEnergyMin &&
        rng_.chance(t.outingChancePct)) {
      startOuting(pet);
      return;
    }
  } else {
    ++outingTicks_;
    if (badWeather_ || pet.stats().energy < t.tiredBelow || pet.stats().fun < t.sadBelow ||
        outingTicks_ >= t.outingMaxTicks) {
      requestHome(pet);
      return;
    }
  }

  if (mood == Mood::Happy && jumpTicks_ == 0 && rng_.chance(6)) jumpTicks_ = kJumpLen;

  // Wander: walk somewhere, pause, turn around, repeat.
  if (pauseTicks_ > 0) {
    --pauseTicks_;
    v_.activity = Activity::Idle;
    if (pauseTicks_ == 1 && rng_.chance(40)) v_.facing = static_cast<int8_t>(-v_.facing);
    return;
  }
  if (wanderTarget_ < 0) {
    wanderTarget_ = static_cast<int16_t>(rng_.range(sc.minX, sc.maxX));
  }
  if (walkToward(wanderTarget_, speedFor(mood))) {
    wanderTarget_ = -1;
    pauseTicks_ = static_cast<uint16_t>(rng_.range(3, 12));
    v_.activity = Activity::Idle;
  }
}

void World::tick(Pet& pet) {
  ++v_.tick;

  // Short-lived effects.
  v_.hop = jumpTicks_ > 0 ? kJumpCurve[kJumpLen - jumpTicks_] : 0;
  if (jumpTicks_ > 0) --jumpTicks_;
  if (v_.heart && ++v_.heartAge >= kHeartTicks) v_.heart = false;
  v_.lookingUp = lookTicks_ > 0;
  if (lookTicks_ > 0) --lookTicks_;
  v_.blinking = !pet.asleep() && rng_.chance(8);

  if (pet.asleep()) {
    v_.activity = Activity::Sleeping;
    if (pet.stats().energy >= 100) wake(pet);  // slept enough
    return;
  }

  if (plan_.empty()) {
    if (v_.scene == SceneId::House) outingTicks_ = 0;
    idleBehaviour(pet);
    return;
  }

  Step& s = plan_.front();
  const Mood mood = pet.mood();
  switch (s.kind) {
    case StepKind::WalkTo:
      if (walkToward(s.arg, speedFor(mood) + 1)) {
        v_.activity = Activity::Idle;
        plan_.erase(plan_.begin());
      }
      break;
    case StepKind::Enter: {
      SceneId target = static_cast<SceneId>(s.arg);
      v_.scene = target;
      v_.groundY = room_->scene(target).groundY;
      v_.x = spotX(target, "door", 180);
      v_.facing = -1;
      sceneChanged_ = true;
      if (target == SceneId::Yard) {
        outingTicks_ = 0;
        pet.walked();
      }
      plan_.erase(plan_.begin());
      break;
    }
    case StepKind::Eat:
      v_.activity = Activity::Eating;
      v_.facing = -1;  // bowl sits to the left of its spot
      if (++stepTicks_ >= static_cast<uint16_t>(s.arg)) {
        stepTicks_ = 0;
        pet.feed();
        v_.bowlFull = false;
        v_.activity = Activity::Idle;
        jumpTicks_ = kJumpLen;
        plan_.erase(plan_.begin());
      }
      break;
    case StepKind::Sleep:
      pet.sleep();
      v_.activity = Activity::Sleeping;
      plan_.clear();
      break;
    case StepKind::Play:
      v_.activity = Activity::Playing;
      if (stepTicks_ % 3 == 0) {
        jumpTicks_ = kJumpLen;
        v_.facing = static_cast<int8_t>(-v_.facing);
      }
      if (++stepTicks_ >= static_cast<uint16_t>(s.arg)) {
        stepTicks_ = 0;
        pet.played();
        v_.heart = true;
        v_.heartAge = 0;
        v_.activity = Activity::Idle;
        plan_.erase(plan_.begin());
      }
      break;
  }
}

}  // namespace dp
