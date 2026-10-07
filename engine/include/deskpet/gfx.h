// Tiny software rasteriser. A Canvas wraps a strip buffer that covers part
// of the screen; every primitive takes screen coordinates and is clipped to
// the strip, so a full scene can be drawn into any dirty rectangle without a
// full-screen framebuffer (115 KB) in RAM.
#pragma once
#include <cstdint>

#include "deskpet/types.h"

namespace dp {

// Indexed 4-bit image (sprites, scene backgrounds). Index 0 is transparent
// when drawn with `transparent0`.
struct Image4 {
  uint16_t w = 0, h = 0, frames = 0;
  Color palette[16] = {};
  const uint8_t* data = nullptr;  // frames * frameBytes(), 2 pixels per byte
  uint32_t frameBytes() const { return (static_cast<uint32_t>(w) * h + 1) / 2; }
};

class Canvas {
 public:
  Canvas(Color* buffer, const Rect& area);

  const Rect& area() const { return area_; }
  const Rect& clip() const { return clip_; }
  void setClip(const Rect& r) { clip_ = Rect::intersect(r, area_); }
  void resetClip() { clip_ = area_; }

  void fill(Color c);
  void pixel(int x, int y, Color c);
  void fillRect(int x, int y, int w, int h, Color c);
  void strokeRect(int x, int y, int w, int h, int t, Color c);
  void fillRoundRect(int x, int y, int w, int h, int r, Color c);
  void fillCircle(int cx, int cy, int r, Color c);
  void fillEllipse(int cx, int cy, int rx, int ry, Color c);
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, Color c);
  void line(int x0, int y0, int x1, int y1, int thickness, Color c);
  void heart(int cx, int cy, int size, Color c);

  // 5x7 font, cell is 6*scale x 8*scale. Returns advance width.
  int text(int x, int y, const char* s, int scale, Color c);
  void textCentered(int cx, int y, const char* s, int scale, Color c);
  static int textWidth(const char* s, int scale);

  void image(const Image4& img, int frame, int x, int y, int scale, bool flipX,
             bool transparent0);

 private:
  void span(int x0, int x1, int y, Color c);  // inclusive x0..x1
  Color* buf_;
  Rect area_;
  Rect clip_;
};

}  // namespace dp
