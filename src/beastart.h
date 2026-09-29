#pragma once
// Parkour creatures drawn as shaded pixel sprites (beastart.cpp, on pixelart.h). World coordinates throughout.
#include "raylib.h"

void PxTortoise(Vector2 shellCentre, float facing, float phase, bool moving, bool dead, float t, bool crystal = false);
void PxCentipede(Vector2 anchor, Vector2 head, float t, float facing);
void PxSerpentLair(Vector2 groundAt, float t, bool serpentHome);
void PxSerpent(Vector2 lair, Vector2 head, Vector2 aim, float t, bool striking, bool rearing, float alpha = 1);
