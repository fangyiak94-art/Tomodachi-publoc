// Drawing of scenes and the pet. Everything draws through a Canvas so it can
// be rendered strip by strip into any dirty rectangle.
#pragma once
#include "deskpet/gfx.h"
#include "deskpet/pack.h"
#include "deskpet/pet.h"
#include "deskpet/room.h"
#include "deskpet/world.h"

namespace dp {

struct PetLook {
  Mood mood = Mood::Neutral;
  Color body = 0;
  Tint light;  // the room's light; the pet gets half of it
  Color blanket = rgb(120, 150, 230);
};

void drawScene(Canvas& c, const LoadedRoom& room, const ActorView& a, const Ambience& amb);
void drawPet(Canvas& c, const LoadedPack& pack, const ActorView& a, const PetLook& look);

// Screen area the pet (with hops, heart and Zzz) may touch this tick.
Rect petBounds(const LoadedPack& pack, const ActorView& a);
Rect bowlBounds(const RoomConfig& room);
Rect windowBounds(const RoomConfig& room);

namespace palette {
constexpr Color kBlack = rgb(0, 0, 0);
constexpr Color kWhite = rgb(255, 255, 255);
constexpr Color kInk = rgb(40, 34, 48);
constexpr Color kAmber = rgb(255, 176, 32);
constexpr Color kGreen = rgb(96, 200, 120);
constexpr Color kRed = rgb(232, 72, 84);
constexpr Color kBlue = rgb(90, 160, 240);
constexpr Color kPanel = rgb(32, 30, 48);
constexpr Color kPanelHi = rgb(58, 54, 86);
constexpr Color kMuted = rgb(150, 146, 170);
constexpr Color kAccent = rgb(255, 200, 90);
}  // namespace palette

}  // namespace dp
