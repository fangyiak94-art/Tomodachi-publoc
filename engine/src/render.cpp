#include "deskpet/render.h"

#include <algorithm>

namespace dp {

using namespace palette;

namespace {

constexpr int kBowlOffset = -34;  // bowl sits left of the "bowl" spot

Color darker(Color c, int k = 200) {
  Tint t;
  t.r = t.g = t.b = static_cast<uint16_t>(k);
  return t.apply(c);
}

Color mix(Color a, Color b, int k, int n) {
  int r = ((a >> 11) * (n - k) + (b >> 11) * k) / n;
  int g = (((a >> 5) & 63) * (n - k) + ((b >> 5) & 63) * k) / n;
  int bl = ((a & 31) * (n - k) + (b & 31) * k) / n;
  return static_cast<Color>((r << 11) | (g << 5) | bl);
}

struct Sky {
  Color top, low;
  bool stars, sun, moon;
};

Sky skyFor(const Ambience& a) {
  const bool grey = a.weather == Weather::Cloudy || a.precipitation();
  const Color g = rgb(150, 150, 166);
  switch (a.phase) {
    case DayPhase::Dawn:
      if (grey) return {mix(rgb(120, 140, 210), g, 2, 3), mix(rgb(255, 196, 170), g, 2, 3), false, false, false};
      return {rgb(120, 140, 210), rgb(255, 196, 170), false, true, false};
    case DayPhase::Dusk:
      if (grey) return {mix(rgb(90, 80, 160), g, 2, 3), mix(rgb(255, 150, 100), g, 2, 3), false, false, false};
      return {rgb(90, 80, 160), rgb(255, 150, 100), false, true, false};
    case DayPhase::Night: return {rgb(10, 16, 50), rgb(40, 50, 110), !grey, false, !grey};
    case DayPhase::Day: break;
  }
  if (grey) return {rgb(140, 150, 170), rgb(196, 204, 216), false, false, false};
  return {rgb(110, 190, 250), rgb(200, 235, 255), false, true, false};
}

void drawSkyBands(Canvas& c, const Rect& r, const Sky& sky, int bands) {
  for (int i = 0; i < bands; ++i) {
    int y0 = r.y + r.h * i / bands, y1 = r.y + r.h * (i + 1) / bands;
    c.fillRect(r.x, y0, r.w, y1 - y0, mix(sky.top, sky.low, i, bands - 1));
  }
}

// Rain streaks / snow flakes inside `r`, animated by the tick.
void drawPrecipitation(Canvas& c, const Rect& r, const Ambience& a, int density) {
  if (!a.precipitation()) return;
  const bool snow = a.weather == Weather::Snow;
  uint32_t seed = 2166136261u;
  for (int i = 0; i < density; ++i) {
    seed = (seed ^ static_cast<uint32_t>(i * 7919)) * 16777619u;
    int x = r.x + static_cast<int>(seed % static_cast<uint32_t>(r.w));
    int speed = snow ? 3 : 14;
    int y = r.y + static_cast<int>((seed / 7 + a.tick * speed) % static_cast<uint32_t>(r.h));
    if (snow) {
      x += static_cast<int>((a.tick + i) % 4) - 2;
      c.fillRect(x, y, 2, 2, palette::kWhite);
    } else {
      c.line(x, y, x - 2, y + 6, 1, rgb(200, 220, 255));
    }
  }
}

void drawBowl(Canvas& c, const RoomColors& col, int cx, int gy, bool full) {
  if (full) c.fillEllipse(cx, gy - 13, 12, 6, rgb(170, 110, 50));
  c.fillRect(cx - 16, gy - 14, 32, 4, darker(col.bowl, 220));
  c.fillRoundRect(cx - 14, gy - 12, 28, 12, 5, col.bowl);
}

void drawWindow(Canvas& c, const RoomConfig& r, const Ambience& a) {
  const int wx = r.windowX, wy = r.windowY;
  const RoomColors& col = r.colors;
  c.fillRect(wx - 5, wy - 5, 74, 62, col.frame);
  // The window shows the real sky; outdoor light, not the room's.
  Tint room = c.tint();
  c.setTint(Tint());
  Sky sky = skyFor(a);
  drawSkyBands(c, Rect(wx, wy, 64, 52), sky, 4);
  if (sky.moon) {
    c.fillCircle(wx + 46, wy + 16, 8, rgb(250, 240, 190));
    c.fillCircle(wx + 50, wy + 13, 7, sky.top);
  } else if (sky.sun) {
    c.fillCircle(wx + 48, wy + 14, 7, rgb(255, 214, 80));
  }
  if (sky.stars) {
    c.fillRect(wx + 10, wy + 10, 2, 2, palette::kWhite);
    c.fillRect(wx + 24, wy + 34, 2, 2, palette::kWhite);
  }
  if (a.phase != DayPhase::Night || a.weather == Weather::Cloudy || a.precipitation()) {
    Color cloud = a.precipitation() ? rgb(170, 176, 190) : palette::kWhite;
    c.fillCircle(wx + 16, wy + 18, 9, cloud);
    c.fillCircle(wx + 26, wy + 16, 11, cloud);
  }
  drawPrecipitation(c, Rect(wx, wy, 64, 52), a, 10);
  c.setTint(room);
  c.fillRect(wx + 30, wy, 4, 52, col.frame);
  c.fillRect(wx, wy + 24, 64, 4, col.frame);
}

void drawHouse(Canvas& c, const RoomConfig& r, int gy, const Ambience& a) {
  const RoomColors& col = r.colors;
  c.fillRect(0, 0, kScreenW, gy - 34, col.wall);
  c.fillRect(0, gy - 34, kScreenW, 34, col.wainscot);
  c.fillRect(0, gy - 36, kScreenW, 3, col.trim);
  c.fillRect(0, gy, kScreenW, kScreenH - gy, col.floor);
  for (int y = gy + 14; y < kScreenH; y += 16) c.fillRect(0, y, kScreenW, 2, col.plank);

  drawWindow(c, r, a);

  int dx = r.spotX(SceneId::House, "door", 184);
  c.fillRoundRect(dx - 20, gy - 78, 40, 78, 6, darker(col.door, 205));
  c.fillRoundRect(dx - 16, gy - 74, 32, 74, 4, col.door);
  c.fillCircle(dx - 10, gy - 38, 3, rgb(240, 200, 80));

  int bx = r.spotX(SceneId::House, "bed", 62);
  Color frameDark = darker(col.bed, 220);
  c.fillRect(bx - 40, gy - 34, 8, 34, frameDark);          // headboard
  c.fillRect(bx - 34, gy - 18, 76, 12, col.bed);           // frame
  c.fillRect(bx - 34, gy - 6, 6, 6, frameDark);            // legs
  c.fillRect(bx + 36, gy - 6, 6, 6, frameDark);
  c.fillRoundRect(bx - 34, gy - 26, 76, 10, 4, col.blanket);
  c.fillEllipse(bx - 22, gy - 28, 11, 6, col.pillow);
}

void drawYard(Canvas& c, const RoomConfig& r, int gy, const Ambience& a) {
  const RoomColors& col = r.colors;
  Sky sky = skyFor(a);
  Tint ground = c.tint();
  c.setTint(Tint());  // the sky has its own colours per phase
  drawSkyBands(c, Rect(0, 0, kScreenW, gy), sky, 6);
  if (sky.stars) {
    static const uint8_t stars[][2] = {{60, 40}, {100, 24}, {140, 60}, {180, 36}, {120, 90}, {40, 80}};
    for (auto& s : stars) c.fillRect(s[0], s[1], 2, 2, palette::kWhite);
  }
  if (sky.moon) c.fillCircle(150, 42, 12, rgb(250, 240, 190));
  if (sky.sun) c.fillCircle(150, a.phase == DayPhase::Day ? 40 : 76, 14, rgb(255, 214, 80));
  if (a.weather == Weather::Cloudy || a.precipitation() || a.phase == DayPhase::Day) {
    Color cloud = a.precipitation() ? rgb(150, 156, 172) : palette::kWhite;
    c.fillCircle(96, 52, 10, cloud);
    c.fillCircle(110, 46, 13, cloud);
    c.fillCircle(124, 54, 9, cloud);
    if (a.weather != Weather::Clear && a.weather != Weather::Unknown) {
      c.fillCircle(160, 40, 12, cloud);
      c.fillCircle(176, 46, 10, cloud);
      c.fillCircle(56, 70, 9, cloud);
    }
  }
  c.setTint(ground);

  c.fillRect(0, gy, kScreenW, kScreenH - gy, a.weather == Weather::Snow ? rgb(236, 240, 248) : col.grass);
  c.fillRect(0, gy, kScreenW, 3, darker(col.grass, 210));
  if (a.weather != Weather::Snow) {
    static const int16_t flowers[][3] = {{40, 200, 0}, {84, 214, 1}, {130, 204, 2}, {170, 220, 0}, {104, 228, 2}, {196, 200, 1}};
    static const Color petals[] = {rgb(255, 120, 150), rgb(255, 230, 90), rgb(190, 140, 255)};
    for (auto& f : flowers) {
      c.fillRect(f[0], f[1], 2, 7, rgb(60, 130, 60));
      c.fillCircle(f[0] + 1, f[1], 3, petals[f[2]]);
      c.fillRect(f[0], f[1] - 1, 2, 2, rgb(255, 250, 220));
    }
  }

  int tx = r.spotX(SceneId::Yard, "tree", 70) - 10;
  c.fillRect(tx - 6, gy - 62, 12, 62, col.trunk);
  c.fillCircle(tx, gy - 80, 28, col.leaves);
  Color leaves2 = mix(col.leaves, palette::kWhite, 1, 8);
  c.fillCircle(tx - 20, gy - 64, 16, leaves2);
  c.fillCircle(tx + 20, gy - 64, 16, leaves2);

  int dx = r.spotX(SceneId::Yard, "door", 176);
  c.fillRect(dx - 30, gy - 72, 82, 72, col.houseWall);
  c.fillTriangle(dx - 40, gy - 72, dx + 62, gy - 72, dx + 11, gy - 112, col.roof);
  c.fillRoundRect(dx - 15, gy - 52, 30, 52, 5, col.door);
  c.fillCircle(dx - 8, gy - 26, 2, rgb(240, 200, 80));
  // Lit window at night or when the weather is grim.
  bool lit = a.phase == DayPhase::Night || a.phase == DayPhase::Dusk || a.precipitation();
  c.setTint(Tint());
  c.fillRect(dx + 22, gy - 56, 20, 18, lit ? rgb(255, 220, 120) : rgb(150, 210, 250));
  c.setTint(ground);
}

}  // namespace

Rect bowlBounds(const RoomConfig& r) {
  int cx = r.spotX(SceneId::House, "bowl", 150) + kBowlOffset;
  return Rect(cx - 18, r.scene(SceneId::House).groundY - 22, 36, 24);
}

Rect windowBounds(const RoomConfig& r) { return Rect(r.windowX - 5, r.windowY - 5, 74, 62); }

void drawScene(Canvas& c, const LoadedRoom& room, const ActorView& a, const Ambience& amb) {
  const int idx = static_cast<int>(a.scene);
  const RoomConfig& r = room.cfg;
  const int gy = r.scenes[idx].groundY;
  c.setTint(sceneTint(amb, a.scene));
  if (room.hasBg[idx]) {
    const Image4& bg = room.bg[idx];
    int scale = std::max(1, kScreenW / bg.w);
    c.image(bg, 0, 0, 0, scale, false, false);
    if (a.scene == SceneId::Yard) drawPrecipitation(c, Rect(0, 0, kScreenW, gy), amb, 40);
  } else if (a.scene == SceneId::House) {
    drawHouse(c, r, gy, amb);
  } else {
    drawYard(c, r, gy, amb);
    drawPrecipitation(c, Rect(0, 0, kScreenW, kScreenH), amb, 48);
  }
  if (a.scene == SceneId::House) {
    Rect b = bowlBounds(r);
    if (!room.hasBg[idx] || a.bowlFull) drawBowl(c, r.colors, b.x + b.w / 2, gy, a.bowlFull);
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
    c.fillRoundRect(cx - 32, cy + 4, 70, 14, 5, look.blanket);  // blanket over
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
  // Pets catch half of the room's light so they stay readable in the dark.
  c.setTint(Tint().blend(look.light, 128));
  struct Reset {
    Canvas& c;
    ~Reset() { c.setTint(Tint()); }
  } reset{c};
  if (!pack.hasSprite) {
    drawProcedural(c, pack.cfg, a, look);
    return;
  }
  const Anim* an = pickAnim(pack.cfg, a, look.mood);
  if (!an) return;
  const int s = pack.cfg.spriteScale;
  const int w = pack.sprite.w * s, h = pack.sprite.h * s;
  // Classic two-frame walks follow the walk cycle; longer animations (e.g.
  // from an animated GIF) play one frame per tick in every state.
  const bool twoStep = a.activity == Activity::Walking && an->frames.size() == 2;
  const int frame = an->frames[(twoStep ? a.walkFrame : a.tick) % an->frames.size()];
  const int top = a.groundY - a.hop - h;
  c.image(pack.sprite, frame, a.x - w / 2, top, s, a.facing < 0, true);
  drawEffects(c, a, a.x, top);
}

}  // namespace dp
