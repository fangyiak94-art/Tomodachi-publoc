#include "deskpet/gfx.h"

#include <cstdlib>
#include <utility>

#include "font5x7.h"

namespace dp {

namespace {
int isqrt(int64_t v) {
  if (v <= 0) return 0;
  int64_t r = 0;
  int64_t bit = int64_t(1) << 40;
  while (bit > v) bit >>= 2;
  while (bit != 0) {
    if (v >= r + bit) {
      v -= r + bit;
      r = (r >> 1) + bit;
    } else {
      r >>= 1;
    }
    bit >>= 2;
  }
  return static_cast<int>(r);
}
}  // namespace

Canvas::Canvas(Color* buffer, const Rect& area)
    : buf_(buffer), area_(area), clip_(area) {}

void Canvas::span(int x0, int x1, int y, Color c) {
  if (y < clip_.y || y >= clip_.bottom()) return;
  if (x0 < clip_.x) x0 = clip_.x;
  if (x1 >= clip_.right()) x1 = clip_.right() - 1;
  if (x1 < x0) return;
  Color* p = buf_ + (y - area_.y) * area_.w + (x0 - area_.x);
  for (int x = x0; x <= x1; ++x) *p++ = c;
}

void Canvas::fill(Color c) {
  for (int y = clip_.y; y < clip_.bottom(); ++y) span(clip_.x, clip_.right() - 1, y, c);
}

void Canvas::pixel(int x, int y, Color c) {
  if (!clip_.contains(x, y)) return;
  buf_[(y - area_.y) * area_.w + (x - area_.x)] = c;
}

void Canvas::fillRect(int x, int y, int w, int h, Color c) {
  Rect r = Rect::intersect(Rect(x, y, w, h), clip_);
  for (int yy = r.y; yy < r.bottom(); ++yy) span(r.x, r.right() - 1, yy, c);
}

void Canvas::strokeRect(int x, int y, int w, int h, int t, Color c) {
  fillRect(x, y, w, t, c);
  fillRect(x, y + h - t, w, t, c);
  fillRect(x, y, t, h, c);
  fillRect(x + w - t, y, t, h, c);
}

void Canvas::fillRoundRect(int x, int y, int w, int h, int r, Color c) {
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  int y0 = y > clip_.y ? y : clip_.y;
  int y1 = (y + h) < clip_.bottom() ? (y + h) : clip_.bottom();
  for (int yy = y0; yy < y1; ++yy) {
    int inset = 0;
    int dy = -1;
    if (yy < y + r) dy = y + r - yy;
    else if (yy >= y + h - r) dy = yy - (y + h - r - 1);
    if (dy >= 0) inset = r - isqrt(int64_t(r) * r - int64_t(dy) * dy);
    span(x + inset, x + w - 1 - inset, yy, c);
  }
}

void Canvas::fillCircle(int cx, int cy, int r, Color c) { fillEllipse(cx, cy, r, r, c); }

void Canvas::fillEllipse(int cx, int cy, int rx, int ry, Color c) {
  if (rx <= 0 || ry <= 0) return;
  int y0 = cy - ry, y1 = cy + ry;
  if (y0 < clip_.y) y0 = clip_.y;
  if (y1 >= clip_.bottom()) y1 = clip_.bottom() - 1;
  const int64_t ry2 = int64_t(ry) * ry;
  for (int y = y0; y <= y1; ++y) {
    int64_t dy = y - cy;
    int dx = isqrt((ry2 - dy * dy) * rx * rx / ry2);
    span(cx - dx, cx + dx, y, c);
  }
}

void Canvas::fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, Color c) {
  // Sort by y.
  if (y1 < y0) { std::swap(y0, y1); std::swap(x0, x1); }
  if (y2 < y0) { std::swap(y0, y2); std::swap(x0, x2); }
  if (y2 < y1) { std::swap(y1, y2); std::swap(x1, x2); }
  if (y2 == y0) return;
  int ys = y0 > clip_.y ? y0 : clip_.y;
  int ye = y2 < clip_.bottom() - 1 ? y2 : clip_.bottom() - 1;
  for (int y = ys; y <= ye; ++y) {
    int xa = x0 + (x2 - x0) * (y - y0) / (y2 - y0);
    int xb;
    if (y < y1) xb = (y1 == y0) ? x1 : x0 + (x1 - x0) * (y - y0) / (y1 - y0);
    else xb = (y2 == y1) ? x1 : x1 + (x2 - x1) * (y - y1) / (y2 - y1);
    if (xa > xb) std::swap(xa, xb);
    span(xa, xb, y, c);
  }
}

void Canvas::line(int x0, int y0, int x1, int y1, int thickness, Color c) {
  int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  int half = thickness / 2;
  for (;;) {
    if (thickness <= 1) pixel(x0, y0, c);
    else fillRect(x0 - half, y0 - half, thickness, thickness, c);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void Canvas::heart(int cx, int cy, int size, Color c) {
  int r = size / 4 + 1;
  fillCircle(cx - r, cy - r / 2, r, c);
  fillCircle(cx + r, cy - r / 2, r, c);
  fillTriangle(cx - 2 * r, cy, cx + 2 * r, cy, cx, cy + 2 * r, c);
}

int Canvas::textWidth(const char* s, int scale) {
  int n = 0;
  for (; *s; ++s) ++n;
  return n == 0 ? 0 : n * 6 * scale - scale;
}

int Canvas::text(int x, int y, const char* s, int scale, Color c) {
  int start = x;
  // Skip whole string quickly when it cannot touch this strip.
  if (y >= clip_.bottom() || y + 8 * scale <= clip_.y) {
    return textWidth(s, scale);
  }
  for (; *s; ++s) {
    uint8_t ch = static_cast<uint8_t>(*s);
    if (ch < font::kFirst || ch > font::kLast) ch = '?';
    const uint8_t* g = font::kGlyphs[ch - font::kFirst];
    for (int col = 0; col < 5; ++col) {
      uint8_t bits = g[col];
      for (int row = 0; row < 7; ++row) {
        if (bits & (1u << row)) {
          fillRect(x + col * scale, y + row * scale, scale, scale, c);
        }
      }
    }
    x += 6 * scale;
  }
  return x - start - scale;
}

void Canvas::textCentered(int cx, int y, const char* s, int scale, Color c) {
  text(cx - textWidth(s, scale) / 2, y, s, scale, c);
}

void Canvas::image(const Image4& img, int frame, int x, int y, int scale, bool flipX,
                   bool transparent0) {
  if (!img.data || frame < 0 || frame >= img.frames || scale < 1) return;
  Rect dst = Rect::intersect(Rect(x, y, img.w * scale, img.h * scale), clip_);
  if (dst.empty()) return;
  const uint8_t* base = img.data + img.frameBytes() * static_cast<uint32_t>(frame);
  for (int yy = dst.y; yy < dst.bottom(); ++yy) {
    int sy = (yy - y) / scale;
    Color* row = buf_ + (yy - area_.y) * area_.w;
    for (int xx = dst.x; xx < dst.right(); ++xx) {
      int sx = (xx - x) / scale;
      if (flipX) sx = img.w - 1 - sx;
      uint32_t i = static_cast<uint32_t>(sy) * img.w + sx;
      uint8_t b = base[i >> 1];
      uint8_t idx = (i & 1) ? (b & 0x0F) : (b >> 4);
      if (transparent0 && idx == 0) continue;
      row[xx - area_.x] = img.palette[idx];
    }
  }
}

}  // namespace dp
