#include "deskpet/input.h"

#include <cstdlib>

namespace dp {

const char* gestureName(Gesture g) {
  switch (g) {
    case Gesture::None: return "none";
    case Gesture::Tap: return "tap";
    case Gesture::DoubleTap: return "double-tap";
    case Gesture::LongPress: return "long-press";
    case Gesture::SwipeLeft: return "swipe-left";
    case Gesture::SwipeRight: return "swipe-right";
    case Gesture::SwipeUp: return "swipe-up";
    case Gesture::SwipeDown: return "swipe-down";
    case Gesture::Back: return "back";
  }
  return "?";
}

bool GestureRecognizer::update(bool down, int16_t x, int16_t y, bool back, uint32_t ms,
                               InputEvent& out) {
  // BOOT button: fire on press.
  if (back && !back_) {
    back_ = true;
    out = {Gesture::Back, kScreenW / 2, kScreenH / 2};
    return true;
  }
  back_ = back;

  if (down && !down_) {
    down_ = true;
    longFired_ = false;
    sx_ = lx_ = x;
    sy_ = ly_ = y;
    downMs_ = ms;
  } else if (down) {
    lx_ = x;
    ly_ = y;
    bool still = std::abs(lx_ - sx_) <= kTapSlopPx && std::abs(ly_ - sy_) <= kTapSlopPx;
    if (!longFired_ && still && ms - downMs_ >= kLongPressMs) {
      longFired_ = true;
      pendingTap_ = false;
      out = {Gesture::LongPress, sx_, sy_};
      return true;
    }
  } else if (down_) {
    down_ = false;
    if (longFired_) return false;
    int dx = lx_ - sx_, dy = ly_ - sy_;
    if (std::abs(dx) >= kSwipeMinPx || std::abs(dy) >= kSwipeMinPx) {
      pendingTap_ = false;
      Gesture g;
      if (std::abs(dx) >= std::abs(dy)) g = dx < 0 ? Gesture::SwipeLeft : Gesture::SwipeRight;
      else g = dy < 0 ? Gesture::SwipeUp : Gesture::SwipeDown;
      out = {g, sx_, sy_};
      return true;
    }
    if (pendingTap_ && ms - tapMs_ <= kDoubleTapMs) {
      pendingTap_ = false;
      out = {Gesture::DoubleTap, tapX_, tapY_};
      return true;
    }
    pendingTap_ = true;
    tapMs_ = ms;
    tapX_ = sx_;
    tapY_ = sy_;
    return false;
  }

  if (pendingTap_ && !down_ && ms - tapMs_ > kDoubleTapMs) {
    pendingTap_ = false;
    out = {Gesture::Tap, tapX_, tapY_};
    return true;
  }
  return false;
}

}  // namespace dp
