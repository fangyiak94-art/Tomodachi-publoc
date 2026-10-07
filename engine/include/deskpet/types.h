// Basic value types shared by the whole engine. No platform headers here.
#pragma once
#include <cstdint>

namespace dp {

constexpr int16_t kScreenW = 240;
constexpr int16_t kScreenH = 240;

using Color = uint16_t;  // RGB565, native endianness

constexpr Color rgb(uint8_t r, uint8_t g, uint8_t b) {
  return static_cast<Color>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

struct Rect {
  int16_t x = 0, y = 0, w = 0, h = 0;

  constexpr Rect() = default;
  constexpr Rect(int x_, int y_, int w_, int h_)
      : x(static_cast<int16_t>(x_)), y(static_cast<int16_t>(y_)),
        w(static_cast<int16_t>(w_)), h(static_cast<int16_t>(h_)) {}

  bool empty() const { return w <= 0 || h <= 0; }
  int right() const { return x + w; }
  int bottom() const { return y + h; }
  bool contains(int px, int py) const {
    return px >= x && py >= y && px < right() && py < bottom();
  }
  bool intersects(const Rect& o) const { return !intersect(*this, o).empty(); }

  static Rect intersect(const Rect& a, const Rect& b) {
    int x0 = a.x > b.x ? a.x : b.x;
    int y0 = a.y > b.y ? a.y : b.y;
    int x1 = a.right() < b.right() ? a.right() : b.right();
    int y1 = a.bottom() < b.bottom() ? a.bottom() : b.bottom();
    if (x1 <= x0 || y1 <= y0) return Rect();
    return Rect(x0, y0, x1 - x0, y1 - y0);
  }
  static Rect unite(const Rect& a, const Rect& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    int x0 = a.x < b.x ? a.x : b.x;
    int y0 = a.y < b.y ? a.y : b.y;
    int x1 = a.right() > b.right() ? a.right() : b.right();
    int y1 = a.bottom() > b.bottom() ? a.bottom() : b.bottom();
    return Rect(x0, y0, x1 - x0, y1 - y0);
  }
  bool operator==(const Rect& o) const {
    return x == o.x && y == o.y && w == o.w && h == o.h;
  }
};

// Small deterministic PRNG so behaviour is reproducible in tests and the
// simulator, and identical on every target.
class Rng {
 public:
  explicit Rng(uint32_t seed = 0x1234567u) : s_(seed ? seed : 1u) {}
  uint32_t next() {
    s_ ^= s_ << 13;
    s_ ^= s_ >> 17;
    s_ ^= s_ << 5;
    return s_;
  }
  // Uniform integer in [lo, hi].
  int range(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + static_cast<int>(next() % static_cast<uint32_t>(hi - lo + 1));
  }
  // True with probability percent/100.
  bool chance(int percent) { return range(0, 99) < percent; }

 private:
  uint32_t s_;
};

}  // namespace dp
