#include "deskpet/render.h"

#include <algorithm>

namespace dp {

using namespace palette;

namespace {

constexpr int kBowlOffset = -34;  // bowl sits left of the "bowl" spot

int16_t spotX(const PackConfig& p, SceneId s, const char* name, int16_t def) {
  const Spot* sp = p.scene(s).spot(name);
  return sp ? sp->x : def;
}

void drawBowl(Canvas& c, int cx, int gy, bool full) {
  if (full) c.fillEllipse(cx, gy - 13, 12, 6, rgb(170, 110, 50));
  c.fillRect(cx - 16, gy - 14, 32, 4, rgb(200, 60, 70));
  c.fillRoundRect(cx - 14, gy - 12, 28, 12, 5, rgb(232, 86, 92));
}

void drawHouse(Canvas& c, const PackConfig& p, int gy, bool night) {
  // Wall and wainscot.
  c.fillRect(0, 0, kScreenW, gy - 34, rgb(250, 234, 206));
  c.fillRect(0, gy - 34, kScreenW, 34, rgb(236, 206, 166));
  c.fillRect(0, gy - 36, kScreenW, 3, rgb(210, 170, 120));
  // Floor planks.
  c.fillRect(0, gy, kScreenW, kScreenH - gy, rgb(196, 140, 92));
  for (int y = gy + 14; y < kScreenH; y += 16) c.fillRect(0, y, kScreenW, 2, rgb(170, 116, 72));

  // Window.
  const int wx = 84, wy = 46;
  c.fillRect(wx - 5, wy - 5, 74, 62, rgb(150, 100, 60));
  c.fillRect(wx, wy, 64, 52, night ? rgb(30, 40, 90) : rgb(150, 210, 250));
  if (night) {
    c.fillCircle(wx + 46, wy + 16, 8, rgb(250, 240, 190));
    c.fillCircle(wx + 50, wy + 13, 7, rgb(30, 40, 90));
  } else {
    c.fillCircle(wx + 16, wy + 18, 9, rgb(255, 255, 255));
    c.fillCircle(wx + 26, wy + 16, 11, rgb(255, 255, 255));
  }
  c.fillRect(wx + 30, wy, 4, 52, rgb(150, 100, 60));
  c.fillRect(wx, wy + 24, 64, 4, rgb(150, 100, 60));

  // Door.
  int dx = spotX(p, SceneId::House, "door", 184);
  c.fillRoundRect(dx - 20, gy - 78, 40, 78, 6, rgb(130, 82, 46));
  c.fillRoundRect(dx - 16, gy - 74, 32, 74, 4, rgb(160, 104, 60));
  c.fillCircle(dx - 10, gy - 38, 3, rgb(240, 200, 80));

  // Bed.
  int bx = spotX(p, SceneId::House, "bed", 62);
  c.fillRect(bx - 40, gy - 34, 8, 34, rgb(120, 76, 44));          // headboard
  c.fillRect(bx - 34, gy - 18, 76, 12, rgb(140, 90, 52));         // frame
  c.fillRect(bx - 34, gy - 6, 6, 6, rgb(120, 76, 44));            // legs
  c.fillRect(bx + 36, gy - 6, 6, 6, rgb(120, 76, 44));
  c.fillRoundRect(bx - 34, gy - 26, 76, 10, 4, rgb(120, 150, 230));  // blanket
  c.fillEllipse(bx - 22, gy - 28, 11, 6, rgb(255, 255, 255));      // pillow
}

void drawYard(Canvas& c, const PackConfig& p, int gy, bool night, uint32_t tick) {
  (void)tick;
  // Sky in bands.
  const Color dayTop = rgb(110, 190, 250), dayLow = rgb(200, 235, 255);
  const Color nightTop = rgb(10, 16, 50), nightLow = rgb(40, 50, 110);
  Color top = night ? nightTop : dayTop, low = night ? nightLow : dayLow;
  const int bands = 6;
  for (int i = 0; i < bands; ++i) {
    int y0 = gy * i / bands, y1 = gy * (i + 1) / bands;
    int r = ((top >> 11) * (bands - 1 - i) + (low >> 11) * i) / (bands - 1);
    int g = (((top >> 5) & 63) * (bands - 1 - i) + ((low >> 5) & 63) * i) / (bands - 1);
    int b = ((top & 31) * (bands - 1 - i) + (low & 31) * i) / (bands - 1);
    c.fillRect(0, y0, kScreenW, y1 - y0, static_cast<Color>((r << 11) | (g << 5) | b));
  }
  if (night) {
    static const uint8_t stars[][2] = {{60, 40}, {100, 24}, {140, 60}, {180, 36}, {120, 90}, {40, 80}};
    for (auto& s : stars) c.fillRect(s[0], s[1], 2, 2, kWhite);
    c.fillCircle(150, 42, 12, rgb(250, 240, 190));
  } else {
    c.fillCircle(150, 40, 14, rgb(255, 214, 80));
    c.fillCircle(96, 52, 10, kWhite);
    c.fillCircle(110, 46, 13, kWhite);
    c.fillCircle(124, 54, 9, kWhite);
  }

  // Grass and flowers.
  c.fillRect(0, gy, kScreenW, kScreenH - gy, rgb(108, 186, 88));
  c.fillRect(0, gy, kScreenW, 3, rgb(84, 160, 70));
  static const int16_t flowers[][3] = {{40, 200, 0}, {84, 214, 1}, {130, 204, 2}, {170, 220, 0}, {104, 228, 2}, {196, 200, 1}};
  static const Color petals[] = {rgb(255, 120, 150), rgb(255, 230, 90), rgb(190, 140, 255)};
  for (auto& f : flowers) {
    c.fillRect(f[0], f[1], 2, 7, rgb(60, 130, 60));
    c.fillCircle(f[0] + 1, f[1], 3, petals[f[2]]);
    c.fillRect(f[0], f[1] - 1, 2, 2, rgb(255, 250, 220));
  }

  // Tree.
  int tx = spotX(p, SceneId::Yard, "tree", 70) - 10;
  c.fillRect(tx - 6, gy - 62, 12, 62, rgb(130, 86, 50));
  c.fillCircle(tx, gy - 80, 28, rgb(70, 150, 70));
  c.fillCircle(tx - 20, gy - 64, 16, rgb(80, 165, 76));
  c.fillCircle(tx + 20, gy - 64, 16, rgb(80, 165, 76));

  // House front with door.
  int dx = spotX(p, SceneId::Yard, "door", 176);
  c.fillRect(dx - 30, gy - 72, 82, 72, rgb(240, 204, 156));
  c.fillTriangle(dx - 40, gy - 72, dx + 62, gy - 72, dx + 11, gy - 112, rgb(200, 84, 72));
  c.fillRoundRect(dx - 15, gy - 52, 30, 52, 5, rgb(150, 96, 56));
  c.fillCircle(dx - 8, gy - 26, 2, rgb(240, 200, 80));
  c.fillRect(dx + 22, gy - 56, 20, 18, night ? rgb(255, 220, 120) : rgb(150, 210, 250));
}

}  // namespace

Rect bowlBounds(const PackConfig& p) {
  const SceneConfig& sc = p.scene(SceneId::House);
  int cx = spotX(p, SceneId::House, "bowl", 150) + kBowlOffset;
  return Rect(cx - 18, sc.groundY - 22, 36, 24);
}

void drawScene(Canvas& c, const LoadedPack& pack, const ActorView& a, bool night) {
  const int idx = static_cast<int>(a.scene);
  const PackConfig& p = pack.cfg;
  const int gy = p.scenes[idx].groundY;
  if (pack.hasBg[idx]) {
    const Image4& bg = pack.bg[idx];
    int scale = std::max(1, kScreenW / bg.w);
    c.image(bg, 0, 0, 0, scale, false, false);
  } else if (a.scene == SceneId::House) {
    drawHouse(c, p, gy, night);
  } else {
    drawYard(c, p, gy, night, a.tick);
  }
  if (a.scene == SceneId::House) {
    Rect b = bowlBounds(p);
    if (!pack.hasBg[idx] || a.bowlFull) drawBowl(c, b.x + b.w / 2, gy, a.bowlFull);
  }
}

Rect petBounds(const LoadedPack& pack, const ActorView& a) {
  int halfW = 44, height = 104;
  if (pack.hasSprite) {
    int s = pack.cfg.spriteScale;
    halfW = std::max(halfW, pack.sprite.w * s / 2 + 8);
    height = std::max(height, pack.sprite.h * s + 48);
  }
  return Rect(a.x - halfW, a.groundY - height, halfW * 2, height + 4);
}

namespace {

void drawEffects(Canvas& c, const ActorView& a, int cx, int top) {
  if (a.heart) c.heart(cx + 4 * a.facing, top - 10 - a.heartAge * 2, 14, kRed);
  if (a.activity == Activity::Sleeping) {
    int phase = static_cast<int>(a.tick % 4);
    c.text(cx + 14, top - 2 - phase * 4, "z", 2, kInk);
    if (phase >= 2) c.text(cx + 26, top - 16 - (phase - 2) * 4, "Z", 2, kInk);
  }
}

void drawProcedural(Canvas& c, const PackConfig& p, const ActorView& a, const PetLook& look) {
  const bool asleep = a.activity == Activity::Sleeping;
  const int gy = a.groundY - a.hop;
  const int f = a.facing;
  const Color body = look.body;

  if (asleep) {
    // Lying on the bed blanket.
    int cx = a.x + 4, cy = a.groundY - 36;
    c.fillEllipse(cx, cy, 30, 17, p.outline);
    c.fillEllipse(cx, cy, 28, 15, body);
    c.line(cx - 14, cy - 2, cx - 6, cy - 2, 2, kInk);
    c.line(cx + 4, cy - 2, cx + 12, cy - 2, 2, kInk);
    c.fillEllipse(cx - 2, cy + 6, 3, 2, kInk);
    c.fillRoundRect(cx - 32, cy + 4, 70, 14, 5, rgb(120, 150, 230));  // blanket over
    drawEffects(c, a, cx, cy - 18);
    return;
  }

  const int rx = 26, ry = 22;
  const bool walking = a.activity == Activity::Walking;
  const int bob = walking && a.walkFrame ? 1 : 0;
  const int cx = a.x, cy = gy - 6 - ry + bob;

  // Feet (two-frame walk cycle).
  int lf = walking && a.walkFrame == 0 ? 3 : 0;
  int rf = walking && a.walkFrame == 1 ? 3 : 0;
  c.fillEllipse(cx - 12, gy - 4 - lf, 8, 5, p.outline);
  c.fillEllipse(cx + 12, gy - 4 - rf, 8, 5, p.outline);

  // Body.
  c.fillEllipse(cx, cy, rx + 2, ry + 2, p.outline);
  c.fillEllipse(cx, cy, rx, ry, body);
  c.fillEllipse(cx - 9, cy - 11, 6, 3, rgb(255, 255, 255));  // shine

  // Face looks where the pet walks.
  const int fx = cx + f * 5;
  const int ey = cy - 4 - (a.lookingUp ? 4 : 0);
  const int exL = fx - 9, exR = fx + 9;
  const Mood m = look.mood;

  if (m == Mood::Happy || m == Mood::Neutral) {
    c.fillEllipse(fx - 15, cy + 6, 4, 3, p.cheek);
    c.fillEllipse(fx + 15, cy + 6, 4, 3, p.cheek);
  }

  // Eyes.
  if (a.blinking) {
    c.line(exL - 3, ey, exL + 3, ey, 2, kInk);
    c.line(exR - 3, ey, exR + 3, ey, 2, kInk);
  } else if (m == Mood::Happy) {
    c.line(exL - 4, ey + 2, exL, ey - 2, 2, kInk);
    c.line(exL, ey - 2, exL + 4, ey + 2, 2, kInk);
    c.line(exR - 4, ey + 2, exR, ey - 2, 2, kInk);
    c.line(exR, ey - 2, exR + 4, ey + 2, 2, kInk);
  } else {
    c.fillCircle(exL, ey, 3, kInk);
    c.fillCircle(exR, ey, 3, kInk);
    c.pixel(exL + 1, ey - 1, kWhite);
    c.pixel(exR + 1, ey - 1, kWhite);
    if (m == Mood::Sad) {
      c.line(exL - 5, ey - 5, exL + 3, ey - 9, 2, kInk);
      c.line(exR - 3, ey - 9, exR + 5, ey - 5, 2, kInk);
    }
  }

  // Mouth.
  const int mx = fx, my = cy + 8;
  if (a.activity == Activity::Eating) {
    if (a.tick % 2) c.fillEllipse(mx, my, 4, 4, kInk);
    else c.line(mx - 3, my, mx + 3, my, 2, kInk);
  } else if (m == Mood::Happy) {
    c.fillEllipse(mx, my, 6, 5, kInk);
    c.fillEllipse(mx, my - 3, 7, 5, body);
  } else if (m == Mood::Sad) {
    c.fillEllipse(mx, my + 3, 6, 5, kInk);
    c.fillEllipse(mx, my + 6, 7, 5, body);
  } else if (m == Mood::Hungry) {
    c.fillEllipse(mx, my, 4, 5, kInk);
    c.fillEllipse(mx + 5, my + 8, 2, 3, kBlue);  // drool
  } else {
    c.line(mx - 3, my, mx + 3, my, 2, kInk);
  }

  drawEffects(c, a, cx, cy - ry);
}

const Anim* pickAnim(const PackConfig& p, const ActorView& a, Mood mood) {
  const char* base = "idle";
  switch (a.activity) {
    case Activity::Walking: base = "walk"; break;
    case Activity::Eating: base = "eat"; break;
    case Activity::Sleeping: base = "sleep"; break;
    case Activity::Playing: base = "play"; break;
    case Activity::Idle: base = "idle"; break;
  }
  std::string b(base);
  if (const Anim* an = p.anim(b + "_" + moodName(mood))) return an;
  if (const Anim* an = p.anim(b)) return an;
  if (const Anim* an = p.anim(std::string("idle_") + moodName(mood))) return an;
  return p.anim("idle");
}

}  // namespace

void drawPet(Canvas& c, const LoadedPack& pack, const ActorView& a, const PetLook& look) {
  if (!pack.hasSprite) {
    drawProcedural(c, pack.cfg, a, look);
    return;
  }
  const Anim* an = pickAnim(pack.cfg, a, look.mood);
  if (!an) return;
  const int s = pack.cfg.spriteScale;
  const int w = pack.sprite.w * s, h = pack.sprite.h * s;
  const int frame = an->frames[(a.activity == Activity::Walking ? a.walkFrame : a.tick) % an->frames.size()];
  const int top = a.groundY - a.hop - h;
  c.image(pack.sprite, frame, a.x - w / 2, top, s, a.facing < 0, true);
  drawEffects(c, a, a.x, top);
}

}  // namespace dp
