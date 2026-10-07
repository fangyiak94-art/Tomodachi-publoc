// Screen drawing. Text is at least scale 2 (14 px tall glyphs) so it stays
// readable on the 1.28 in panel; touch targets are at least 44 px.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "deskpet/engine.h"
#include "layout.h"

namespace dp {

using namespace palette;

namespace {

constexpr Color kBg = rgb(24, 22, 36);

std::string fit(const std::string& s, size_t maxChars) {
  if (s.size() <= maxChars) return s;
  if (maxChars < 3) return s.substr(0, maxChars);
  return s.substr(0, maxChars - 2) + "..";
}

std::vector<std::string> wrap(const std::string& s, size_t width, size_t maxLines) {
  std::vector<std::string> lines;
  std::string line, word;
  auto flushWord = [&]() {
    if (word.empty()) return;
    if (line.empty()) line = word;
    else if (line.size() + 1 + word.size() <= width) line += " " + word;
    else {
      lines.push_back(line);
      line = word;
    }
    while (line.size() > width) {
      lines.push_back(line.substr(0, width));
      line = line.substr(width);
    }
    word.clear();
  };
  for (char ch : s) {
    if (ch == ' ' || ch == '\n') flushWord();
    else word += ch;
  }
  flushWord();
  if (!line.empty()) lines.push_back(line);
  if (lines.size() > maxLines) {
    lines.resize(maxLines);
    lines.back() = fit(lines.back() + "...", width);
  }
  return lines;
}

void title(Canvas& c, const char* t) { c.textCentered(kScreenW / 2, 18, t, 2, kAccent); }

void bar(Canvas& c, int y, const char* label, float value, bool low) {
  c.text(36, y + 1, label, 2, kWhite);
  const int x = 100, w = 104, h = 16;
  c.fillRoundRect(x, y, w, h, 6, kPanelHi);
  int fillW = static_cast<int>(w * std::min(100.0f, std::max(0.0f, value)) / 100.0f);
  if (fillW > 0) c.fillRoundRect(x, y, std::max(fillW, 12), h, 6, low ? kAmber : kGreen);
}

}  // namespace

void Engine::drawHud(Canvas& c) {
  if (!c.area().intersects(hudRect_)) return;
  int64_t t = now();
  if (t > 0) {
    std::string clk = clockText(t);
    int w = Canvas::textWidth(clk.c_str(), 2);
    c.fillRoundRect(kScreenW / 2 - w / 2 - 6, 12, w + 12, 20, 8, rgb(0, 0, 0));
    c.textCentered(kScreenW / 2, 15, clk.c_str(), 2, kWhite);
  }
  int x = kScreenW / 2;
  const PetStats& s = pet_.stats();
  bool low = pet_.isLow(s.food) || pet_.isLow(s.fun) || pet_.isLow(s.energy);
  if (dnd_ && low) x -= 22;
  if (dnd_) {
    c.fillRoundRect(x - 22, 36, 44, 18, 8, kPanel);
    c.fillCircle(x - 10, 45, 5, kAccent);
    c.fillCircle(x - 8, 43, 5, kPanel);
    c.text(x - 2, 38, "z", 2, kWhite);
    x += 44;
  }
  if (low) {
    c.fillCircle(x, 45, 9, kAmber);
    c.text(x - 2, 38, "!", 2, kInk);
  }
  if (portalOpen_) {
    // Live room editing from the phone/PC; BOOT leaves it.
    c.fillRoundRect(kScreenW / 2 - 30, 58, 60, 20, 8, kBlue);
    c.textCentered(kScreenW / 2, 61, "EDIT", 2, kWhite);
  }
}

void Engine::drawHome(Canvas& c) {
  drawScene(c, room_, world_.view(), amb_);
  drawPet(c, pack_, world_.view(), look());
  drawHud(c);
  drawToast(c);
}

void Engine::drawToast(Canvas& c) {
  const Rect& r = layout::kToast;
  if (toast_.empty() || !c.area().intersects(r)) return;
  c.fillRoundRect(r.x, r.y, r.w, r.h, 10, kInk);
  c.textCentered(r.x + r.w / 2, r.y + 6, fit(toast_, 11).c_str(), 2, kAccent);
}

void Engine::drawStats(Canvas& c) {
  c.fill(kBg);
  const PetStats& s = pet_.stats();
  const EvolutionStage* st = pack_.cfg.stage(pack_.stage);
  std::string name = st ? st->name : pack_.cfg.name;
  title(c, fit(name, 12).c_str());
  char line[32];
  std::snprintf(line, sizeof(line), "Lv %u  %s", static_cast<unsigned>(pet_.level()),
                moodName(pet_.mood()));
  c.textCentered(kScreenW / 2, 44, line, 2, kMuted);
  bar(c, 76, "FOOD", s.food, pet_.isLow(s.food));
  bar(c, 106, "FUN", s.fun, pet_.isLow(s.fun));
  bar(c, 136, "NRG", s.energy, pet_.isLow(s.energy));
  bar(c, 166, "XP", static_cast<float>(s.xp % 100), false);
  // Opens the Evolve screen (Rare Candy, evolve, devolve).
  const Rect& b = layout::kCandyButton;
  c.fillRoundRect(b.x, b.y, b.w, b.h, 14, rgb(236, 96, 160));
  c.fillCircle(b.x + 16, b.y + b.h / 2, 6, kWhite);  // candy
  c.fillTriangle(b.x + 6, b.y + b.h / 2 - 5, b.x + 6, b.y + b.h / 2 + 5, b.x + 12, b.y + b.h / 2,
                 kWhite);
  c.text(b.x + 34, b.y + 13, "EVOLVE", 2, kWhite);
}

void Engine::drawEvolveScreen(Canvas& c) {
  c.fill(kBg);
  const PetStats& s = pet_.stats();
  // Each option: a title, plus a second line saying what it does.
  auto row = [&](const Rect& r, bool on, Color col, const std::string& title,
                 const std::string& sub) {
    c.fillRoundRect(r.x, r.y, r.w, r.h, 14, on ? col : kPanelHi);
    if (sub.empty()) {
      c.textCentered(r.x + r.w / 2, r.y + 15, title.c_str(), 2, on ? kWhite : kMuted);
      return;
    }
    c.textCentered(r.x + r.w / 2, r.y + 5, title.c_str(), 2, on ? kWhite : kMuted);
    c.textCentered(r.x + r.w / 2, r.y + 25, sub.c_str(), 2, on ? rgb(255, 230, 245) : kMuted);
  };
  char buf[24];
  std::snprintf(buf, sizeof(buf), "CANDY x%u", static_cast<unsigned>(s.candies));
  row(layout::kEvoCandy, s.candies > 0, rgb(236, 96, 160), buf, s.candies ? "+1 level" : "none yet");

  const EvolutionStage* next = pack_.cfg.stage(pack_.stage + 1);
  if (!next) {
    row(layout::kEvoEvolve, false, 0, "FINAL FORM", "");
  } else {
    std::snprintf(buf, sizeof(buf), " Lv%u", static_cast<unsigned>(next->level));
    bool ok = canEvolve();
    row(layout::kEvoEvolve, ok, rgb(130, 90, 220), "EVOLVE",
        ok ? "to " + fit(next->name, 11) : fit(next->name, 10) + buf);
  }
  const EvolutionStage* prev = pack_.cfg.stage(pack_.stage - 1);
  if (prev) row(layout::kEvoDevolve, true, rgb(70, 120, 200), "DEVOLVE", "to " + fit(prev->name, 11));
  else row(layout::kEvoDevolve, false, 0, "FIRST FORM", "");

  const EvolutionStage* cur = pack_.cfg.stage(pack_.stage);
  std::snprintf(buf, sizeof(buf), " Lv%u", static_cast<unsigned>(pet_.level()));
  c.textCentered(kScreenW / 2, 202, (fit(cur ? cur->name : pack_.cfg.name, 7) + buf).c_str(), 2,
                 kAccent);
}

void Engine::drawEvolution(Canvas& c) {
  if (evo_.phase == EvoPhase::Flash) {
    c.fill(kWhite);
    return;
  }
  c.fill(rgb(18, 14, 30));
  // Twinkling sparkles.
  static const uint8_t sparkles[][2] = {{60, 96}, {176, 84}, {44, 150}, {196, 146}, {84, 60}, {160, 190}};
  for (size_t i = 0; i < sizeof(sparkles) / sizeof(sparkles[0]); ++i)
    if ((evo_.ticks + i) % 3 == 0) {
      int x = sparkles[i][0], y = sparkles[i][1];
      c.fillRect(x - 1, y - 4, 2, 8, kAccent);
      c.fillRect(x - 4, y - 1, 8, 2, kAccent);
    }

  const std::string from = fit(evo_.fromName, 12), to = fit(evo_.toName, 12);
  switch (evo_.phase) {
    case EvoPhase::Intro:
    case EvoPhase::Morph:
      c.textCentered(kScreenW / 2, 30, "What?", 2, kWhite);
      c.textCentered(kScreenW / 2, 50, (from + " is").c_str(), 2, kWhite);
      c.textCentered(kScreenW / 2, 70, evo_.devolve ? "devolving!" : "evolving!", 2, kWhite);
      c.textCentered(kScreenW / 2, 206, "BOOT: stop", 2, kMuted);
      break;
    case EvoPhase::Done:
      c.textCentered(kScreenW / 2, 40, (from + (evo_.devolve ? " devolved" : " evolved")).c_str(), 2, kWhite);
      c.textCentered(kScreenW / 2, 60, ("into " + to + "!").c_str(), 2, kAccent);
      break;
    case EvoPhase::Cancelled:
      c.textCentered(kScreenW / 2, 40, ("Huh? " + from).c_str(), 2, kWhite);
      c.textCentered(kScreenW / 2, 60, evo_.devolve ? "stayed put" : "stopped evolving", 2, kWhite);
      break;
    default:
      break;
  }

  ActorView v;
  v.x = kScreenW / 2;
  v.groundY = 186;
  v.facing = 1;
  v.activity = Activity::Idle;
  v.tick = evo_.ticks;
  PetLook l = look();
  l.light = Tint();
  l.mood = Mood::Happy;
  if (evo_.phase == EvoPhase::Morph) {
    // Old and new forms swap as white shapes, faster and faster.
    int k = evo_.ticks - 7;
    int period = std::max(1, 6 - k / 4);
    bool showNew = (k / period) % 2 == 1;
    l.silhouette = true;
    drawPet(c, showNew ? evo_.next : pack_, v, l);
  } else {
    drawPet(c, pack_, v, l);
  }
}

void Engine::drawAgenda(Canvas& c) {
  c.fill(kBg);
  title(c, "NEXT UP");
  int64_t t = now();
  if (t <= 0) {
    c.textCentered(kScreenW / 2, 100, "No clock", 2, kMuted);
    c.textCentered(kScreenW / 2, 124, "yet", 2, kMuted);
    return;
  }
  std::vector<CalendarEvent> evs = alerts_.upcoming(t, 3);
  if (evs.empty()) {
    c.textCentered(kScreenW / 2, 110, "No meetings", 2, kMuted);
    return;
  }
  int y = 48;
  for (const CalendarEvent& e : evs) {
    bool soon = e.start - t <= AlertCenter::kLeadSeconds;
    std::string when = e.start <= t ? "now" : clockText(e.start);
    c.textCentered(kScreenW / 2, y, when.c_str(), 2, soon ? kAmber : kAccent);
    c.textCentered(kScreenW / 2, y + 20, fit(e.title, 14).c_str(), 2, kWhite);
    y += 54;
  }
}

void Engine::drawActions(Canvas& c) {
  c.fill(kBg);
  static const Color colors[4] = {rgb(232, 120, 80), rgb(90, 160, 240), rgb(130, 110, 220),
                                  rgb(90, 190, 110)};
  const char* labels[4] = {"FEED", "PLAY", pet_.asleep() ? "WAKE" : "SLEEP",
                           world_.view().scene == SceneId::Yard ? "HOME" : "WALK"};
  for (int i = 0; i < 4; ++i) {
    const Rect& r = layout::kActionTiles[i];
    c.fillRoundRect(r.x, r.y, r.w, r.h, 14, colors[i]);
    c.textCentered(r.x + r.w / 2, r.y + r.h / 2 - 7, labels[i], 2, kWhite);
  }
  c.textCentered(kScreenW / 2, 208, "v back", 2, kMuted);
}

void Engine::drawSettings(Canvas& c) {
  using namespace layout;
  c.fill(kBg);
  char buf[32];
  // Brightness: tap left half "-", right half "+".
  int y = kRowBrightness;
  c.fillRoundRect(48, y + 6, 44, 32, 10, kPanelHi);
  c.fillRoundRect(148, y + 6, 44, 32, 10, kPanelHi);
  c.textCentered(70, y + 15, "-", 2, kWhite);
  c.textCentered(170, y + 15, "+", 2, kWhite);
  std::snprintf(buf, sizeof(buf), "%d%%", brightness_ * 100 / 255);
  c.textCentered(120, y + 15, buf, 2, kWhite);

  y = kRowDnd;
  c.fillRoundRect(28, y + 4, 184, 36, 12, dnd_ ? kAccent : kPanelHi);
  c.textCentered(120, y + 15, dnd_ ? "DND: ON" : "DND: OFF", 2, dnd_ ? kInk : kWhite);

  y = kRowPack;
  c.fillRoundRect(16, y + 4, 208, 36, 12, kPanelHi);
  std::string pk = "Pet: " + fit(pack_.cfg.name, 9) + " >";
  c.textCentered(120, y + 15, pk.c_str(), 2, packError_.empty() ? kWhite : kAmber);

  y = kRowRoom;
  c.fillRoundRect(20, y + 4, 200, 36, 12, kPanelHi);
  std::string rm = "Room: " + fit(room_.cfg.name, 8) + " >";
  c.textCentered(120, y + 15, rm.c_str(), 2, roomError_.empty() ? kWhite : kAmber);

  y = kRowUpload;
  c.fillRoundRect(64, y + 4, 112, 34, 12, kBlue);
  c.textCentered(120, y + 14, "UPLOAD", 2, kWhite);
}

void Engine::drawUpload(Canvas& c) {
  c.fill(kBg);
  title(c, "EDIT & UPLOAD");
  ConnectivityStatus cs = p_.connectivity ? p_.connectivity() : ConnectivityStatus();
  char buf[40];
  std::snprintf(buf, sizeof(buf), "WiFi %s  BT %s", cs.wifi, cs.phone);
  c.textCentered(kScreenW / 2, 44, fit(buf, 16).c_str(), 2, kMuted);
  std::vector<std::string> lines;
  if (portalOpen_) lines = p_.uploader->statusLines();
  else lines = {"Not on this", "build yet"};
  int y = 76;
  for (size_t i = 0; i < lines.size() && i < 4; ++i, y += 24)
    c.textCentered(kScreenW / 2, y, fit(lines[i], 15).c_str(), 2, kWhite);
  if (!packError_.empty() || !roomError_.empty())
    c.textCentered(kScreenW / 2, 178, "Last upload bad", 2, kAmber);
  c.textCentered(kScreenW / 2, 206, "BOOT: exit", 2, kMuted);
}

void Engine::drawMeeting(Canvas& c) {
  c.fill(rgb(255, 140, 40));
  const CalendarEvent& e = alerts_.meeting();
  c.textCentered(kScreenW / 2, 40, "MEETING", 3, kWhite);
  int64_t t = now();
  char buf[24];
  int64_t mins = (e.start - t + 59) / 60;
  if (mins <= 0) std::snprintf(buf, sizeof(buf), "now");
  else std::snprintf(buf, sizeof(buf), "in %d min", static_cast<int>(mins));
  c.textCentered(kScreenW / 2, 76, buf, 3, kInk);
  int y = 116;
  for (const std::string& l : wrap(e.title, 14, 2)) {
    c.textCentered(kScreenW / 2, y, l.c_str(), 2, kWhite);
    y += 20;
  }
  c.textCentered(kScreenW / 2, 172, "TAP: OK", 2, kInk);
  c.textCentered(kScreenW / 2, 194, "HOLD: 5 MIN", 2, kInk);
}

void Engine::drawNotification(Canvas& c) {
  const Rect card(24, 62, 192, 116);
  if (!c.area().intersects(card)) return;
  const Notification& n = alerts_.notification();
  c.fillRoundRect(card.x, card.y, card.w, card.h, 16, kPanel);
  c.textCentered(kScreenW / 2, card.y + 12, fit(n.app, 14).c_str(), 2, kAccent);
  c.textCentered(kScreenW / 2, card.y + 36, fit(n.title, 14).c_str(), 2, kWhite);
  int y = card.y + 60;
  for (const std::string& l : wrap(n.body, 14, 2)) {
    c.textCentered(kScreenW / 2, y, l.c_str(), 2, kMuted);
    y += 20;
  }
}

}  // namespace dp
