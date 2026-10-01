#pragma once
// The Trawl in first person (trawl_view3d.cpp): the same Gannet, sea, web and session as the top-down game, drawn in
// 3D through Red Tide's inked low-poly renderer (redtide_render.h) from the eyes of the hand you play. Only the view
// and the aiming differ; the simulation, the stations, the HUD and the panels are shared with trawl.cpp.
//
// Frames: the sea's plane is world x/z (Boat::pos.x -> x, Boat::pos.y -> z), up is +y, depth z -> -y. The boat's
// local frame is x toward the bow, y up, z toward starboard (the deck frame's x/y with height between them); the
// main deck is at local y = DECK_Y and the engine room's floor at ENGINE_Y.
#include "trawl.h"
#include "trawl_eco.h"
#include "trawl_session.h"

namespace tw {

const float DECK_Y = 1.2f, ENGINE_Y = -1.3f, EYE_H = 1.62f;

struct Eye3D { float yaw = 0, pitch = -0.12f; };        // boat-relative look: yaw from the bow toward starboard

Matrix BoatMatrix(const Boat& b);                       // the boat's local frame -> the world
Vector3 BoatPoint(const Boat& b, Vector3 local);
Camera3D EyeCamera(const Gannet& g, int you, const Eye3D& e);
// Where the crosshair (or any screen point, in game pixels) meets the water: the deck-frame point, and false when
// the look is at the sky (the point is then 40 m out along the look).
bool AimAtWater(const Gannet& g, const Camera3D& cam, Vector2 screen, Vector2* deckOut);
bool AimAtDeck(const Gannet& g, const Camera3D& cam, Vector2 screen, int deck, Vector2* deckOut);   // the same onto the deck's planks
bool CrewHeadOnScreen(const Gannet& g, int c, const Camera3D& cam, Vector2* out);                // over a hand's head (barks)
Vector2 LookDeckDir(const Eye3D& e);                    // the look's direction along the deck (deck frame, unit)

void DrawTrawl3D(const Gannet& g, const Eco* eco, const Session& sess, int you, const Camera3D& cam, float ghostSee);
void UnloadTrawl3D();

} // namespace tw
