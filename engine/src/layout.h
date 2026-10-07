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

}  // namespace layout
}  // namespace dp
