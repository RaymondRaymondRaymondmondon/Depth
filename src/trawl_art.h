#pragma once
// The Trawl's pixel art (trawl_art.cpp): the sea, the Gannet and her crew, drawn into the pixel canvas in the boat's
// frame. Everything is lit by the view's lights (the lantern mast, the wheelhouse, hand lamps); the rest is night.
#include "game.h"
#include "trawl.h"
#include <vector>

namespace tw {

struct View {
    float ppm = 12;                       // canvas pixels per metre (twice the parkour's pixel scale on screen)
    Vector2 center{};                     // where the boat's origin lands on the canvas
    int viewerDeck = 0;                   // 0 the main deck; 1 the engine room (the deck goes dark above)
    bool inWheelhouse = false;            // the roof comes off
    float moon = 0.5f;                    // 0 new .. 1 full: glints on the crests out in the dark
    struct Light { Vector2 at; float r, k; Vector2 dir{0, 0}; float half = 0; };   // half > 0: a cone (radians) along dir
    std::vector<Light> lights;            // deck-frame lights
    bool ghost = false; Vector2 ghostAt{}; float ghostSee = 0;   // a ghost sees threats within 10 m as pale outlines for a moment after it moves
    Vector2 ToCanvas(Vector2 deck) const;
    Vector2 DeckOfCanvas(Vector2 c) const;
    float LightAt(Vector2 deck) const;
};
void DrawSea(const Gannet& g, const View& v);
void DrawBoat(const Gannet& g, const View& v);
void DrawLines(const Gannet& g, const View& v);
void DrawLife(const Gannet& g, const View& v, bool air);   // the web's fish in the light (air: the gulls, over everything)          // rods, lines, lures and what's on them
void DrawGear(const Gannet& g, const View& v);            // shots, shot fish afloat, the net, set gear, life rings, hands overboard
void DrawQuay(const Gannet& g, const View& v);            // the harbour quay beside her port side while she's moored
void DrawCrewMember(const Crew& c, const View& v, float t, bool you);
void DrawSkiff(const Gannet& g, const View& v);
void DrawLanding(const Gannet& g, const View& v);         // the landings (the Atoll) on foot           // the skiff (on the davit, afloat, keel up)
// the dive scene (trawl_divescene.cpp): the local diver's body in a side view of the wreck, on the parkour movement code
int DiveSceneStep(const Gannet& g, int you, float dt, float dir, bool jumpHeld, bool up, bool down);   // the room to ask the host to move the diver into, or -1
void DiveSceneDraw(const Gannet& g, int you);
bool DiveSceneActive();
Color RoleColor(Role r);

extern int gSprayWarm;   // frames of first-person spray to run before the next draw (shots)
void DrawTrawlStudio(int which, float t);   // the visual overhaul's turnaround stage (shots tvis_6/7/8, the test assets)
} // namespace tw
