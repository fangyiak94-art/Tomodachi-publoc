// Creature packs: a manifest plus optional sprite sheet and buzzer tunes.
// (The house and yard live in room themes, see room.h.) Packs are untrusted input: data only, every field is bounded
// and validated, and a broken pack falls back to the built-in default.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "deskpet/gfx.h"
#include "deskpet/hal.h"
#include "deskpet/pet.h"
#include "deskpet/room.h"

namespace dp {

struct Anim {
  std::string name;
  std::vector<uint8_t> frames;
};

// One form of the creature. Stages are sorted by level; the pet evolves
// into the next one when it reaches that level (see Engine evolution).
struct EvolutionStage {
  uint32_t level = 1;
  std::string name;
  Color body = 0;
  bool hasBody = false;
  std::string sprites;     // own sprite sheet; empty = the pack's
  std::vector<Anim> anims; // own animations; empty = the pack's
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
  std::vector<std::pair<std::string, std::string>> sounds;  // name -> RTTTL
  std::vector<EvolutionStage> evolution;

  const Anim* anim(const std::string& name) const;
  const char* sound(const char* name) const;
  // Highest stage whose level <= `level` (nullptr if none).
  const EvolutionStage* stageFor(uint32_t level) const;
  // Index of that stage (0 when there are no stages).
  int stageIndexFor(uint32_t level) const;
  const EvolutionStage* stage(int index) const;
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
  int stage = 0;           // evolution stage this sprite sheet belongs to
  std::vector<Anim> anims; // animations for that stage
  std::string spriteBlob;
  Image4 sprite;
  bool hasSprite = false;

  const Anim* anim(const std::string& name) const;

  // Image4::data points into the blob. Moving a std::string may move its
  // buffer (small-string optimisation), so call this after moving a pack.
  void rebind();
};

// Loads /packs/<id>/... with the sprite sheet of evolution stage `stage`
// (clamped). On failure `out` holds the built-in default pack.
bool loadPack(Storage& storage, const std::string& id, LoadedPack& out, std::string& err,
              int stage = 0);

}  // namespace dp
