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
    struct Light { Vector2 at; float r, k; };
    std::vector<Light> lights;            // deck-frame lights
    Vector2 ToCanvas(Vector2 deck) const;
    Vector2 DeckOfCanvas(Vector2 c) const;
    float LightAt(Vector2 deck) const;
};
void DrawSea(const Gannet& g, const View& v);
void DrawBoat(const Gannet& g, const View& v);
void DrawCrewMember(const Crew& c, const View& v, float t, bool you);
Color RoleColor(Role r);

} // namespace tw
