#pragma once
// Red Tide's Visual Overhaul: the divers on the shared figure, and the studio harness (see redtide_vis.cpp).
#include "raylib.h"
#include "redtide_render.h"
#include "figure3d.h"
#include <vector>

namespace rt {

const Model* DiverModel(int voice);   // 0 the Diver, 1 the Whaler, 2 the Stowaway, 3 the Mechanic (Match::VoiceOf)
bool DiversReady();
// draws a diver at frame (feet at the origin, x forward), posed; returns the skinning matrices (for grips)
std::vector<Matrix> DrawDiverFigure(int voice, Matrix frame, fig::Pose P, float t, Color tint);
void DrawRedTideStudio(int which, float t);

} // namespace rt
