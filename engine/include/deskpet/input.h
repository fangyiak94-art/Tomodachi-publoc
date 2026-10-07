// Turns raw touch points and the BOOT button into gestures. Shared by every
// target so the feel is identical on the PC and the board.
#pragma once
#include <cstdint>

#include "deskpet/hal.h"

namespace dp {

class GestureRecognizer {
 public:
  static constexpr uint32_t kLongPressMs = 600;
  static constexpr uint32_t kDoubleTapMs = 280;
  static constexpr int kSwipeMinPx = 40;
  static constexpr int kTapSlopPx = 14;

  // Feed the current raw state; returns at most one gesture per call.
  bool update(bool down, int16_t x, int16_t y, bool back, uint32_t ms, InputEvent& out);

 private:
  bool down_ = false;
  bool back_ = false;
  bool longFired_ = false;
  bool pendingTap_ = false;
  int16_t sx_ = 0, sy_ = 0, lx_ = 0, ly_ = 0;
  int16_t tapX_ = 0, tapY_ = 0;
  uint32_t downMs_ = 0, tapMs_ = 0;
};

}  // namespace dp
