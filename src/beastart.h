#pragma once
// Parkour creatures drawn as shaded pixel sprites (beastart.cpp, on pixelart.h). World coordinates throughout.
#include "raylib.h"
struct PlatformState;
struct Beast;

void PxSetScale(float sz); // the size the draw loop grows the next creature by (so its art pixels stay two world pixels)
void PxTortoise(Vector2 shellCentre, float facing, float phase, bool moving, bool dead, float t, bool crystal = false);
void PxCentipede(Vector2 anchor, Vector2 head, float t, float facing);
void PxSerpentLair(Vector2 groundAt, float t, bool serpentHome);
void PxSerpent(Vector2 lair, Vector2 head, Vector2 aim, float t, bool striking, bool rearing, float alpha = 1);
// the biomes' living creatures; true when it was drawn completely (false: the old drawer still draws it, or adds its effects)
bool PxDrawIslandBeast(const PlatformState& p, const Beast& b, float t);
bool PxDrawCaveBeast(const PlatformState& p, const Beast& b, float t);
bool PxDrawWeedsBeast(const PlatformState& p, const Beast& b, float t);
bool PxDrawAtlantisBeast(const PlatformState& p, const Beast& b, float t);
