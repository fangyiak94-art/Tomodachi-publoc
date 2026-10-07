// The Desk Pet engine: owns the pet, world, alerts, UI screens and the
// dirty-rectangle renderer. Targets construct it with a Platform and call
// begin() once and loop() as often as they like.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "deskpet/alerts.h"
#include "deskpet/hal.h"
#include "deskpet/input.h"
#include "deskpet/pack.h"
#include "deskpet/pet.h"
#include "deskpet/render.h"
#include "deskpet/world.h"

namespace dp {

enum class Screen : uint8_t { Home, Stats, Agenda, Actions, Settings, Upload };
const char* screenName(Screen s);

struct RenderStats {
  uint32_t frames = 0;         // render passes that pushed anything
  uint32_t pixelsLastFrame = 0;
  uint64_t pixelsTotal = 0;
  uint32_t fullRedraws = 0;
};

class Engine {
 public:
  static constexpr uint32_t kTickMs = 280;
  static constexpr int kStripRows = 16;
  static constexpr uint32_t kSaveEveryMs = 5u * 60u * 1000u;

  explicit Engine(const Platform& platform);

  void begin();
  void loop();

  // Used by the targets' debug keys and by tests.
  void handleGesture(const InputEvent& e);
  void tick();  // one behaviour step, as if kTickMs passed
  void setTimeScale(float s) { timeScale_ = s < 0 ? 0 : s; }
  float timeScale() const { return timeScale_; }
  bool selectPack(const std::string& id);
  std::vector<std::string> listPacks();
  void save();

  Pet& pet() { return pet_; }
  const World& world() const { return world_; }
  World& world() { return world_; }
  AlertCenter& alerts() { return alerts_; }
  const LoadedPack& pack() const { return pack_; }
  Screen screen() const { return screen_; }
  bool dnd() const { return dnd_; }
  const RenderStats& renderStats() const { return rstats_; }
  const std::string& lastPackError() const { return packError_; }

 private:
  // Engine flow.
  void pollSources(uint32_t ms);
  void applyAlertSignals(const AlertSignals& s);
  void goTo(Screen s);
  void toggleDnd();
  void setBrightness(int level);
  void applyBrightness();
  void loadSettings();
  void saveSettings();
  void restorePet();
  void log(const std::string& line);
  int64_t now();
  bool night();
  PetLook look() const;

  // Rendering.
  void invalidate(const Rect& r);
  void invalidateAll() { invalidate(Rect(0, 0, kScreenW, kScreenH)); }
  void render();
  void drawAll(Canvas& c);
  void drawHome(Canvas& c);
  void drawHud(Canvas& c);
  void drawStats(Canvas& c);
  void drawAgenda(Canvas& c);
  void drawActions(Canvas& c);
  void drawSettings(Canvas& c);
  void drawUpload(Canvas& c);
  void drawMeeting(Canvas& c);
  void drawNotification(Canvas& c);
  std::string clockText(int64_t t);
  uint32_t contentHash();

  // Screen input.
  void onHome(const InputEvent& e);
  void onActions(const InputEvent& e);
  void onSettings(const InputEvent& e);
  void onAlert(const InputEvent& e);

  Platform p_;
  Pet pet_;
  World world_;
  AlertCenter alerts_;
  TunePlayer tune_;
  GestureRecognizer gestures_;
  LoadedPack pack_;
  std::string packError_;

  Screen screen_ = Screen::Home;
  bool dnd_ = false;
  int brightness_ = 200;
  int appliedBrightness_ = -1;
  float timeScale_ = 1.0f;
  uint32_t lastTickMs_ = 0;
  uint32_t lastSaveMs_ = 0;
  uint32_t lastLevel_ = 1;
  uint32_t shownHash_ = 0;
  Rect hudRect_;
  bool hudLowWarn_ = false;
  int hudMinute_ = -1;
  bool started_ = false;

  static constexpr int kMaxDirty = 6;
  Rect dirty_[kMaxDirty];
  int dirtyCount_ = 0;
  RenderStats rstats_;
  Color strip_[kScreenW * kStripRows];
};

}  // namespace dp
