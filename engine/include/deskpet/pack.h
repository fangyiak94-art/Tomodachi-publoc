// Content packs: a manifest plus optional sprite sheet, scene backgrounds and
// buzzer tunes. Packs are untrusted input: data only, every field is bounded
// and validated, and a broken pack falls back to the built-in default.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "deskpet/gfx.h"
#include "deskpet/hal.h"
#include "deskpet/pet.h"

namespace dp {

enum class SceneId : uint8_t { House = 0, Yard = 1 };
constexpr int kSceneCount = 2;

struct Spot {
  std::string name;
  int16_t x = 0;  // where the pet stands (bottom-centre)
  int16_t y = 0;
};

struct SceneConfig {
  std::string background;  // optional .dpi file in the pack
  std::vector<Spot> spots;
  int16_t groundY = 182;
  int16_t minX = 56, maxX = 184;
  const Spot* spot(const char* name) const;
};

struct Anim {
  std::string name;
  std::vector<uint8_t> frames;
};

struct EvolutionStage {
  uint32_t level = 1;
  std::string name;
  Color body = 0;
  bool hasBody = false;
};

struct PackConfig {
  std::string id = "builtin";
  std::string name = "Blobby";
  std::string version = "0";
  Tuning tuning;
  Color body = rgb(126, 214, 165);
  Color outline = rgb(46, 107, 79);
  Color cheek = rgb(245, 154, 168);
  std::string sprites;  // optional .dps sprite sheet; empty = procedural
  uint8_t spriteScale = 3;
  std::vector<Anim> anims;
  SceneConfig scenes[kSceneCount];
  std::vector<std::pair<std::string, std::string>> sounds;  // name -> RTTTL
  std::vector<EvolutionStage> evolution;

  const SceneConfig& scene(SceneId id) const { return scenes[static_cast<int>(id)]; }
  const Anim* anim(const std::string& name) const;
  const char* sound(const char* name) const;
  // Highest stage whose level <= `level` (nullptr if none).
  const EvolutionStage* stageFor(uint32_t level) const;
};

PackConfig defaultPack();

// Limits (also documented in docs/PACK_FORMAT.md).
constexpr size_t kMaxManifestBytes = 16 * 1024;
constexpr size_t kMaxSpriteBytes = 64 * 1024;
constexpr size_t kMaxBackgroundBytes = 40 * 1024;
constexpr size_t kImageHeaderBytes = 44;  // "DPS1" + 4*u16 + 16*u16 palette

bool isSafeFileName(const std::string& name);
bool parseManifest(const char* data, size_t len, PackConfig& out, std::string& err);

// "DPS1" indexed image. `out.data` points into `data`, which must outlive it.
bool parseImage4(const uint8_t* data, size_t len, Image4& out, std::string& err);

struct LoadedPack {
  PackConfig cfg;
  std::string spriteBlob;
  Image4 sprite;
  bool hasSprite = false;
  std::string bgBlob[kSceneCount];
  Image4 bg[kSceneCount];
  bool hasBg[kSceneCount] = {false, false};

  // Image4::data points into the blobs. Moving a std::string may move its
  // buffer (small-string optimisation), so call this after moving a pack.
  void rebind();
};

// Loads /packs/<id>/... . On failure `out` holds the built-in default pack.
bool loadPack(Storage& storage, const std::string& id, LoadedPack& out, std::string& err);

}  // namespace dp
