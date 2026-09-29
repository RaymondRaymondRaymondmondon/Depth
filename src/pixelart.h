#pragma once
// Shaded pixel sprites for the parkour creatures (pixelart.cpp). A creature is modelled on a small canvas out of
// volumes - ellipsoids (Ball), tapered limbs (Limb/Chain) and flat facets (Tri) - each writing a depth and a surface
// normal per art pixel, so overlapping parts sort themselves like 3D and every pixel knows which way it faces. The
// renderer then lights it from the upper left in four hue-shifted bands per material, dithers the band edges,
// darkens contours where a part passes behind another, lays a pattern (scales, bands, spots, scutes, speckle) over
// the surface, and inks the silhouette - so the result reads as a lit, rounded, hand-shaded pixel sprite rather
// than flat shapes. Each art pixel is drawn as `cell` world pixels (2 on the platform levels' art grid).
#include "raylib.h"
#include <vector>

namespace px {

enum Pattern { PAT_NONE, PAT_SCALES, PAT_BANDS, PAT_SPOTS, PAT_SCUTES, PAT_SPECKLE, PAT_RINGS };
struct Mat {
    Color ramp[4];            // darkest .. lightest
    int pattern = PAT_NONE;
    float patScale = 1;       // pattern size (art pixels)
    bool emissive = false;    // glows: always the brightest band, and never outlined dark
    Color accent{0, 0, 0, 0}; // PAT_SPOTS / PAT_BANDS draw in this colour when it's set (alpha > 0), else one band darker
};
Mat Ramp(Color base, int pattern = PAT_NONE, float patScale = 1); // a four-band pixel-art ramp around a base colour
Mat Glow(Color c);

struct Canvas {
    int w = 0, h = 0;
    std::vector<signed char> mat;
    std::vector<float> z, nx, ny, u, v;
    void Begin(int W, int H);
    // an ellipsoid; yClip > 0 cuts it off below that canvas row (a shell's rim)
    void Ball(float cx, float cy, float rx, float ry, int m, float z0 = 0, float rz = -1, float yClip = -1);
    void Limb(Vector2 a, Vector2 b, float ra, float rb, int m, float z0 = 0, float uOffset = 0); // a tapered cylinder
    void Chain(const Vector2* pts, int n, const float* radii, int m, float z0 = 0);             // limbs joined end to end (a body, a neck, a tail)
    void Tri(Vector2 a, Vector2 b, Vector2 c, int m, float z0 = 0, float nxv = 0, float nyv = -0.4f); // a flat facet (a horn, a claw, a fin, a beak)
    void Dot(int x, int y, int m, float z0 = 200);
    void Put(int x, int y, int m, float zz, float nxv, float nyv, float uu = 0, float vv = 0);
};
// draw it: `world` is where canvas pixel (0,0) lands (before flipping), flipX mirrors it about its own width
void Render(const Canvas& cv, const Mat* mats, int nMats, Vector2 world, float cell, bool flipX, float alpha = 1, Color ink = {16, 10, 14, 255});

} // namespace px
