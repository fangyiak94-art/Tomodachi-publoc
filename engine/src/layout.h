// Touch layout shared by drawing and hit-testing.
#pragma once
#include "deskpet/types.h"

namespace dp {
namespace layout {

constexpr Rect kActionTiles[4] = {Rect(36, 40, 80, 76), Rect(124, 40, 80, 76),
                                  Rect(36, 124, 80, 76), Rect(124, 124, 80, 76)};

// Settings rows (44 px tall touch targets).
constexpr int kRowBrightness = 20;
constexpr int kRowDnd = 64;
constexpr int kRowPack = 108;
constexpr int kRowRoom = 152;
constexpr int kRowUpload = 196;
constexpr int kRowH = 44;

// Evolve screen rows.
constexpr Rect kEvoCandy = Rect(32, 40, 176, 46);
constexpr Rect kEvoEvolve = Rect(20, 92, 200, 46);
constexpr Rect kEvoDevolve = Rect(24, 144, 192, 46);

// Stats screen "EVOLVE >" button and the home toast strip.
constexpr Rect kCandyButton = Rect(60, 188, 120, 40);
constexpr Rect kToast = Rect(46, 194, 148, 26);

}  // namespace layout
}  // namespace dp
