#pragma once
// NOCLIP's own look (design doc p. 36, "Look"): found-footage liminal. Low-poly first-person 3D with flat, overlit
// procedural textures (wallpaper, carpet, concrete...), fluorescent light from a per-cell light grid that flickers, the
// headlamp, and a VHS pass (scanlines, chroma bleed, grain, a timestamp). It deliberately doesn't use Depth's ink:
// this is plain raylib 3D into Mode3DRT with its own shaders. The Labs are the one designed place (brass and steel).
#include "noclip.h"

namespace ncr {
struct View {
    Camera3D cam{}; int level = 0; float t = 0; float sanity = 100; bool lamp = true, flashlight = false; float lampRange = 9;
    bool surface = false; float noise = 0; float blackout = 0; int me = -1; bool ghost = false; bool scanner = false;
    std::vector<nc::Entity> fakes;   // this player's hallucinations
    bool teammatesAsFacelings = false;
};
void Render(const nc::World& w, const View& v);                   // the 3D frame into Mode3DRT, then the VHS pass onto the screen
void RenderSurface(const nc::World& w, const View& v);            // the Bureau's warehouse at night (sodium light)
void Stamp(const std::string& left, const std::string& right, float t);   // the camcorder's overlay: REC, the date and the clock
void Unload();
}
