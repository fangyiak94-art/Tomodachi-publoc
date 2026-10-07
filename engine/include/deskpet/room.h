// Room themes: the house and yard the pet lives in, separate from the
// creature pack. A theme is /rooms/<id>/room.json (colours, furniture
// layout, optional background images). Themes can be swapped in Settings and
// edited live from a phone or PC through the portal.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "deskpet/gfx.h"
#include "deskpet/hal.h"

namespace dp {

enum class SceneId : uint8_t { House = 0, Yard = 1 };
constexpr int kSceneCount = 2;

struct Spot {
  std::string name;
  int16_t x = 0;  // where the pet stands (bottom-centre)
  int16_t y = 0;
};

struct SceneConfig {
  std::string background;  // optional .dps file
  std::vector<Spot> spots;
  int16_t groundY = 182;
  int16_t minX = 56, maxX = 184;
  const Spot* spot(const char* name) const;
};

// Every colour the procedural house and yard use. Names match room.json.
struct RoomColors {
  Color wall = rgb(250, 234, 206);
  Color wainscot = rgb(236, 206, 166);
  Color trim = rgb(210, 170, 120);
  Color floor = rgb(196, 140, 92);
  Color plank = rgb(170, 116, 72);
  Color frame = rgb(150, 100, 60);
  Color door = rgb(160, 104, 60);
  Color bed = rgb(140, 90, 52);
  Color blanket = rgb(120, 150, 230);
  Color pillow = rgb(255, 255, 255);
  Color bowl = rgb(232, 86, 92);
  Color grass = rgb(108, 186, 88);
  Color houseWall = rgb(240, 204, 156);
  Color roof = rgb(200, 84, 72);
  Color leaves = rgb(70, 150, 70);
  Color trunk = rgb(130, 86, 50);
};

struct ColorField {
  const char* name;
  Color RoomColors::*member;
};
// Table of the colour fields, for parsing, saving and the editor.
const std::vector<ColorField>& roomColorFields();

struct RoomConfig {
  std::string id = "cozy";
  std::string name = "Cozy";
  RoomColors colors;
  int16_t windowX = 84, windowY = 46;  // house window, top-left of the glass
  SceneConfig scenes[kSceneCount];
  std::string backgroundsFrom;  // room id whose folder holds the background files

  const SceneConfig& scene(SceneId id) const { return scenes[static_cast<int>(id)]; }
  SceneConfig& scene(SceneId id) { return scenes[static_cast<int>(id)]; }
  int16_t spotX(SceneId s, const char* name, int16_t def) const;
};

RoomConfig defaultRoom();

constexpr size_t kMaxRoomBytes = 4 * 1024;

bool parseRoom(const char* data, size_t len, RoomConfig& out, std::string& err);
// Serialises everything the editor can change (round-trips through parseRoom).
std::string roomToJson(const RoomConfig& room);

struct LoadedRoom {
  RoomConfig cfg;
  std::string bgBlob[kSceneCount];
  Image4 bg[kSceneCount];
  bool hasBg[kSceneCount] = {false, false};
  void rebind();
};

// Loads /rooms/<id>/room.json. The built-in "cozy" room is used when the
// folder is missing. On failure `out` holds the default room.
bool loadRoom(Storage& storage, const std::string& id, LoadedRoom& out, std::string& err);

// ------------------------------------------------------------- ambience

const char* weatherName(Weather w);
bool weatherFromName(const char* s, Weather& out);

enum class DayPhase : uint8_t { Dawn, Day, Dusk, Night };
DayPhase dayPhaseForHour(int hour);  // hour 0..23 local

// Everything that makes the room change by itself.
struct Ambience {
  DayPhase phase = DayPhase::Day;
  Weather weather = Weather::Unknown;
  bool lightsOff = false;  // pet asleep: house lights off
  uint32_t tick = 0;       // drives rain and snow animation

  bool precipitation() const { return weather == Weather::Rain || weather == Weather::Snow; }
  bool operator==(const Ambience& o) const {
    return phase == o.phase && weather == o.weather && lightsOff == o.lightsOff;
  }
  bool operator!=(const Ambience& o) const { return !(*this == o); }
};

// Light multipliers (256 = unchanged) for a scene under this ambience.
Tint sceneTint(const Ambience& a, SceneId scene);

}  // namespace dp
