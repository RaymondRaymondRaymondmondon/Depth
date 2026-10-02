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
std::vector<Matrix> DrawDiverFigure(int voice, Matrix frame, fig::Pose P, float t, Color tint, const std::string& suit = "", const std::string& helmet = "");
bool DrawFirstPersonArms(int voice, const Camera3D& cam, Vector3 gripR, Vector3 gripL, bool leftOn, float t, const std::string& suit = "", const std::string& helmet = "");
void DrawHelmetPort(float wet, float t);   // the helmet's port rim round the first-person view (2D, before the HUD)
void DiverSkinColours(const std::string& suit, const std::string& helmet, std::vector<Recolor>& out);   // the Locker's suit/helmet ids
void DrawRedTideStudio(int which, float t);

// the water's particles (phase 2), drawn after the ink pass as soft sprites: bubbles (rising, wobbling, growing
// toward the surface, popping there or after their life), and the soft dot the snow and the blood are drawn with
const Texture2D& SoftDot();
void FxBubbles(Vector3 at, int n, float spread, float size = 0.03f);
void FxStep(float dt, float surfaceY);
void FxDrawBubbles(const Camera3D& cam);
void FxClear();

} // namespace rt
