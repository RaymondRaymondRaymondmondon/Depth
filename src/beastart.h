#pragma once
// Parkour creatures drawn as shaded pixel sprites (beastart.cpp, on pixelart.h). World coordinates throughout.
#include "raylib.h"
struct PlatformState;
struct Beast;

void PxSetScale(float sz); // the size the draw loop grows the next creature by (so its art pixels stay two world pixels)
void PxSetAlpha(float a);  // fade everything drawn after this (1 = solid)
void PxTortoise(Vector2 shellCentre, float facing, float phase, bool moving, bool dead, float t, bool crystal = false);
void PxCentipede(Vector2 anchor, Vector2 head, float t, float facing);
void PxSerpentLair(Vector2 groundAt, float t, bool serpentHome);
void PxSerpent(Vector2 lair, Vector2 head, Vector2 aim, float t, bool striking, bool rearing, float alpha = 1);
// the biomes' living creatures; true when it was drawn completely (false: the old drawer still draws it, or adds its effects)
bool PxDrawIslandBeast(const PlatformState& p, const Beast& b, float t);
bool PxDrawCaveBeast(const PlatformState& p, const Beast& b, float t);
bool PxDrawWeedsBeast(const PlatformState& p, const Beast& b, float t);
bool PxDrawAtlantisBeast(const PlatformState& p, const Beast& b, float t);
// the Island's scenery, baked once into textures (call IslandArtPrepare outside any other layer, e.g. before the frame's canvas)
enum IslandArtId { IA_PALM = 0 /* 3 variants x 3 sway frames: IA_PALM + v*3 + frame */, IA_TREE = 9, IA_HUT = 11, IA_IDOL = 13, IA_TOTEM = 14, IA_TEMPLE = 15, IA_VOLCANO = 16, IA_ALTAR = 17, IA_FERN = 18, IA_BUSH = 19, IA_CANOE = 20, IA_VILLAGER = 21 /* 4: two poses x two garbs */ };
void IslandArtPrepare();
void DrawIslandArt(int id, Vector2 bottomCentre, Color tint, bool flip = false);