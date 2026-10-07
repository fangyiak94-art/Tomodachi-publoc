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
#include "deskpet/room.h"
#include "deskpet/world.h"

namespace dp {

enum class Screen : uint8_t { Home, Stats, Agenda, Actions, Settings, Upload, Evolve };
const char* screenName(Screen s);

// The evolution scene: "What? X is evolving!" -> silhouettes flashing
// between old and new form -> white flash -> "X evolved into Y!".
// BOOT during the first part stops it (it tries again at the next level).
enum class EvoPhase : uint8_t { None, Intro, Morph, Flash, Done, Cancelled };

struct RenderStats {
  uint32_t frames = 0;         // render passes that pushed anything
  uint32_t pixelsLastFrame = 0;
  uint64_t pixelsTotal = 0;
  uint32_t fullRedraws = 0;
};

class Engine : public PortalHost {
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
  std::vector<std::string> listRooms();
  void setWeather(Weather w);  // also fed by Platform::weather
  void openPortal() { startPortal(); invalidateAll(); }  // room editor without the Upload screen

  // Rare Candy: +1 level now (and maybe an evolution). Used from Stats.
  bool useRareCandy();
  void giveCandy(int n, const char* why);
  // Evolve screen: evolve when the level allows it, or go back a form.
  bool canEvolve() const;
  bool evolveNow();
  bool devolveNow();
  EvoPhase evoPhase() const { return evo_.phase; }
  const std::string& toast() const { return toast_; }

  // PortalHost (room editor and theme switching from the phone/PC).
  std::string roomJson() override;
  std::vector<std::string> roomIds() override { return listRooms(); }
  std::string activeRoomId() override { return room_.cfg.id; }
  bool applyRoomJson(const std::string& json, std::string& err) override;
  bool selectRoom(const std::string& id) override;
  void save();

  Pet& pet() { return pet_; }
  const World& world() const { return world_; }
  World& world() { return world_; }
  AlertCenter& alerts() { return alerts_; }
  const LoadedPack& pack() const { return pack_; }
  const LoadedRoom& room() const { return room_; }
  const Ambience& ambience() const { return amb_; }
  bool portalOpen() const { return portalOpen_; }
  Screen screen() const { return screen_; }
  bool dnd() const { return dnd_; }
  const RenderStats& renderStats() const { return rstats_; }
  const std::string& lastPackError() const { return packError_; }
  const std::string& lastRoomError() const { return roomError_; }

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
  Ambience computeAmbience();
  void updateAmbience();
  void startPortal();
  void stopPortal();
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
  void onStats(const InputEvent& e);
  void onEvolveScreen(const InputEvent& e);
  void drawEvolveScreen(Canvas& c);
  bool startEvolution(int to, bool manual);

  // Levels, candy and evolution.
  void onLevelUp();
  void checkEvolution();
  void stepEvolution();
  void onEvolutionGesture(const InputEvent& e);
  void drawEvolution(Canvas& c);
  void drawToast(Canvas& c);
  void showToast(const std::string& text);
  bool loadPackStage(const std::string& id, int stage, LoadedPack& out);
  int64_t localDay();

  Platform p_;
  Pet pet_;
  World world_;
  AlertCenter alerts_;
  TunePlayer tune_;
  GestureRecognizer gestures_;
  LoadedPack pack_;
  std::string packError_;
  LoadedRoom room_;
  std::string roomError_;
  Weather weather_ = Weather::Unknown;
  struct Evolution {
    EvoPhase phase = EvoPhase::None;
    int to = 0;
    bool devolve = false;  // going back a form
    bool manual = false;   // chosen on the Evolve screen (no chaining)
    uint16_t ticks = 0;
    std::string fromName, toName;
    LoadedPack next;  // the new form's sprite sheet, loaded for the scene
  } evo_;
  uint32_t refusedLevel_ = 0;  // evolution stopped at this level
  std::string toast_;
  uint16_t toastTicks_ = 0;
  Ambience amb_;
  bool portalOpen_ = false;

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
