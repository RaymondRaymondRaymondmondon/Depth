// ============================================================================
//  DEPTH - richly drawn enemies. Each creature is built, back to front, from the same lit parts the crew use
//  (ShadeLimb / ShadeBall / ShadeQuad, drawn between BeginFigure and EndFigure so the figure shader adds the ink,
//  the rim light and the block shadows), plus a shared kit of anatomy: two-segment limbs, armour plates, barnacle
//  colonies, ragged hems, rivets, scars and cross-hatching. Everything faces LEFT, toward the party.
//  Coordinates are "reference units" with the feet at (0, 0): x grows to the right, y grows DOWN, so a body sits at
//  negative y. Ctx maps them onto the enemy's rectangle, so a boss drawn across two or three ranks is the same
//  creature at a bigger scale, with extra layers.
//  Done so far: the Cave's crustaceans and the Island's tribe.
// ============================================================================
#include "game.h"
#include <algorithm>
#include <cmath>

namespace {
const Color INK{6, 8, 12, 255};

// a point that accepts int or float coordinates in braces without narrowing complaints
struct V {
    float x, y;
    template <class A, class B> V(A a, B b) : x((float)a), y((float)b) {}
    V(Vector2 v) : x(v.x), y(v.y) {}
};

struct Ctx {
    float cx, by, k, t;
    int u;
    Vector2 P(float x, float y) const { return {cx + x * k, by + y * k}; }
};

void Ball(const Ctx& c, float x, float y, float r, Color col) { ShadeBall(c.P(x, y), r * c.k, col); }
void Limb(const Ctx& c, V a, V b, float w0, float w1, Color col) { ShadeLimb(c.P(a.x, a.y), c.P(b.x, b.y), w0 * c.k, w1 * c.k, col); }
void Limb2(const Ctx& c, V a, V b, V d, float w0, float w1, float w2, Color col) { Limb(c, a, b, w0, w1, col); Limb(c, b, d, w1, w2, col); }
void Line(const Ctx& c, V a, V b, float w, Color col) { DrawLineEx(c.P(a.x, a.y), c.P(b.x, b.y), std::max(1.0f, w * c.k), col); }
void Tri(const Ctx& c, V a, V b, V d, Color col) { DrawTri(c.P(a.x, a.y), c.P(b.x, b.y), c.P(d.x, d.y), col); }
void Quad(const Ctx& c, V a, V b, V d, V e, Color col) { ShadeQuad(c.P(a.x, a.y), c.P(b.x, b.y), c.P(d.x, d.y), c.P(e.x, e.y), col); }
void Dot(const Ctx& c, float x, float y, float r, Color col) { DrawCircleV(c.P(x, y), std::max(1.0f, r * c.k), col); }
void Bar(const Ctx& c, float x, float y, float w, float h, Color col) { Vector2 p = c.P(x, y); DrawRectangle((int)p.x, (int)p.y, std::max(1, (int)(w * c.k)), std::max(1, (int)(h * c.k)), col); }

// a hard crescent of black on the lower-right rim of a round mass, away from the upper-left light
void Crescent(const Ctx& c, float x, float y, float r) { DrawRing(c.P(x, y), r * c.k * 0.7f, r * c.k, -25, 105, 14, INK); }

// a ragged hem: triangles hanging from the line a-b
void Jag(const Ctx& c, V a, V b, int n, float depth, Color col, int seed) {
    for (int i = 0; i < n; i++) {
        Vector2 p0{a.x + (b.x - a.x) * i / n, a.y + (b.y - a.y) * i / n}, p1{a.x + (b.x - a.x) * (i + 1) / n, a.y + (b.y - a.y) * (i + 1) / n};
        Tri(c, p0, p1, {(p0.x + p1.x) / 2, (p0.y + p1.y) / 2 + depth * (0.55f + 0.45f * (float)((i * 7 + seed) % 3) / 2.0f)}, col);
    }
}

void Barnacles(const Ctx& c, float x, float y, float spread, int n, int seed) {
    for (int i = 0; i < n; i++) {
        float bx = x + (float)((seed + i * 7) % 9 - 4) * spread / 4.0f, by2 = y + (float)((seed + i * 5) % 5 - 2) * 2.0f, r = 2.4f + (i % 3) * 0.8f;
        Tri(c, {bx - r - 0.8f, by2 + r + 0.8f}, {bx + r + 0.8f, by2 + r + 0.8f}, {bx, by2 - r * 1.3f - 0.8f}, INK);
        Tri(c, {bx - r, by2 + r}, {bx + r, by2 + r}, {bx, by2 - r * 1.2f}, Color{218, 206, 182, 255});
        Dot(c, bx, by2 + r * 0.2f, r * 0.35f, Color{60, 40, 40, 255});
    }
}

void Hatch(const Ctx& c, float x, float y, float w, float h, int n, Color col) {
    for (int i = 0; i < n; i++) Line(c, {x + w * i / n, y}, {x + w * i / n + w * 0.18f, y + h}, 0.7f, col);
}

void Rivet(const Ctx& c, float x, float y, float r, Color col) { Ball(c, x, y, r, col); Dot(c, x, y, r * 0.4f, Tone(col, -0.6f)); }

// a black eye-socket with a pinprick of light: the eyes are never lit, only found
void Eye(const Ctx& c, float x, float y, float w, Color glint) {
    Bar(c, x - w / 2, y - 1.6f, w, 3.2f, INK);
    Dot(c, x + w * 0.15f, y, 0.9f, glint);
}

// a crab-style claw: a heavy palm and two curved fingers, the lower one opening and shutting on toothed edges
void Claw(const Ctx& c, float x, float y, float size, float open, Color shell, Color lt, Color teeth, bool crusher) {
    Vector2 hinge{x - size * 0.55f, y};
    Vector2 tipA{hinge.x - size * 1.3f, hinge.y - size * 0.32f}, tipB{hinge.x - size * 1.25f * cosf(open), hinge.y + size * 0.22f + size * 1.15f * sinf(open)};
    Limb(c, {hinge.x + size * 0.2f, hinge.y + size * 0.15f}, tipB, size * 0.62f, size * 0.14f, Tone(shell, -0.2f));   // the moving finger, in shadow
    Ball(c, x, y, size, shell);                                                                                          // the palm
    Ball(c, x - size * 0.25f, y - size * 0.3f, size * 0.55f, lt);
    Crescent(c, x, y, size);
    Limb(c, {hinge.x + size * 0.2f, hinge.y - size * 0.15f}, tipA, size * 0.62f, size * 0.12f, Tone(shell, 0.05f));   // the fixed finger
    int n = crusher ? 5 : 3;
    for (int i = 0; i < n; i++) { // teeth on both inner edges
        float u = (i + 0.8f) / (n + 0.4f);
        Vector2 pa{hinge.x + (tipA.x - hinge.x) * u, hinge.y + (tipA.y - hinge.y) * u + size * 0.12f}, pb{hinge.x + (tipB.x - hinge.x) * u, hinge.y + (tipB.y - hinge.y) * u - size * 0.1f};
        Tri(c, {pa.x - 1.6f, pa.y}, {pa.x + 1.6f, pa.y}, {pa.x, pa.y + size * 0.3f}, teeth);
        Tri(c, {pb.x - 1.6f, pb.y}, {pb.x + 1.6f, pb.y}, {pb.x, pb.y - size * 0.3f}, teeth);
    }
    Rivet(c, x + size * 0.1f, y - size * 0.1f, size * 0.09f, Tone(shell, -0.4f));
}
// ============================================================ THE CAVE: crustaceans
void SeaLouse(const Ctx& c) {
    const float t = c.t;
    const Color shell{150, 130, 168, 255}, seam{88, 74, 108, 255}, pale{214, 206, 222, 255}, leg{104, 90, 124, 255}, bone{224, 214, 196, 255};
    for (int pass = 0; pass < 2; pass++) // seven pairs of two-segment legs, the far pair in shadow
        for (int i = 0; i < 7; i++) {
            float x = -34 + i * 10.5f + pass * 3, sw = sinf(t * 7 + i * 0.9f + pass * 2 + c.u) * 3;
            Limb2(c, {x, -18}, {x - 7, -8 - std::max(0.0f, sw) * 0.5f}, {x - 12 + sw, 0}, 2.8f, 2.3f, 1.5f, pass ? leg : Tone(leg, -0.35f));
        }
    for (int i = -1; i <= 1; i++) Tri(c, {44, -22}, {62 + i * 3, -30 + i * 10}, {60, -16 + i * 8}, i ? Tone(seam, 0.1f) : seam); // the tail fan
    for (int i = 0; i < 8; i++) { // plates from tail to head, each overlapping the next, edged with a lit rim
        float x = 40 - i * 10.5f, rad = 11 + sinf((i + 0.5f) / 8 * PI) * 7, y = -24 - rad * 0.15f;
        Ball(c, x, y, rad, i % 2 ? shell : Tone(shell, -0.08f));
        DrawRing(c.P(x + 1, y), (rad - 1.6f) * c.k, rad * c.k, 200, 340, 10, seam);
        Crescent(c, x, y, rad);
        if (i % 2 == 0) Tri(c, {x - 3, y - rad * 0.85f}, {x + 3, y - rad * 0.85f}, {x, y - rad - 8}, bone);          // a dorsal spine
        if (i == 2 || i == 4 || i == 6) Barnacles(c, x, y - rad * 0.55f, 5, 3, c.u + i);
        Hatch(c, x - rad * 0.5f, y - rad * 0.2f, rad, rad * 0.7f, 3, Fade(INK, 0.55f));
    }
    Limb(c, {-40, -13}, {38, -13}, 5, 4, pale);                                                                   // the pale underside
    Ball(c, -47, -25, 12, Tone(shell, 0.1f));                                                                     // the head
    Crescent(c, -47, -25, 12);
    Eye(c, -53, -29, 8, Color{220, 214, 240, 255});
    for (int s = -1; s <= 1; s += 2) Tri(c, {-56, -19}, {-64 + s * 2, -10}, {-51, -14 + s * 2}, bone);            // mandibles
    for (int a = 0; a < 2; a++) { // whip antennae, swaying
        Vector2 prev{-52.0f, -34.0f + a * 4};
        for (int i = 1; i <= 6; i++) {
            Vector2 q{-52.0f - i * 4.5f - a * i, -34.0f - i * (a ? 3.0f : 5.0f) + sinf(t * 2.4f + i * 0.7f + a) * i * 0.8f + a * 4};
            Limb(c, prev, q, 1.7f - i * 0.15f, 1.4f - i * 0.15f, seam);
            prev = q;
        }
    }
    Line(c, {10, -40}, {22, -30}, 0.9f, Fade(INK, 0.8f)); Line(c, {22, -30}, {20, -22}, 0.9f, Fade(INK, 0.8f)); // an old crack
}

void PistolShrimp(const Ctx& c) {
    const float t = c.t;
    const Color shell{150, 84, 74, 255}, dk{92, 48, 48, 255}, lt{192, 124, 100, 255}, pale{232, 200, 168, 255}, glow{170, 230, 255, 255};
    for (int i = 0; i < 4; i++) { // walking legs
        float x = -14 + i * 9.0f, sw = sinf(t * 6 + i + c.u) * 2.5f;
        Limb2(c, {x, -26}, {x - 5, -12}, {x - 9 + sw, 0}, 3, 2.4f, 1.6f, Tone(dk, i % 2 ? 0.0f : -0.25f));
    }
    for (int i = 0; i < 6; i++) { // the abdomen curls up and over, ringed in bands
        float x = 8 + i * 8.0f, y = -36 - sinf(i / 5.0f * PI) * 12 + i * 3.4f, r = 11 - i * 1.15f;
        Ball(c, x, y, r, i % 2 ? shell : Tone(shell, -0.1f));
        DrawRing(c.P(x, y), (r - 1.5f) * c.k, r * c.k, 220, 320, 8, dk);
        Crescent(c, x, y, r);
        Tri(c, {x - 2, y + r * 0.7f}, {x + 2, y + r * 0.7f}, {x, y + r + 4}, Tone(lt, -0.2f));                      // swimmerets
    }
    for (int i = -1; i <= 1; i++) Tri(c, {54, -16}, {68 + i * 2, -24 + i * 9}, {64, -12 + i * 7}, i ? dk : Tone(dk, 0.1f)); // the tail fan
    Ball(c, -6, -38, 18, shell); Ball(c, -4, -43, 12, lt); Crescent(c, -6, -38, 18);                               // the carapace
    Hatch(c, -14, -46, 20, 16, 5, Fade(INK, 0.5f));
    for (int b = 0; b < 3; b++) DrawRing(c.P(-6, -38), (12.0f + b * 2) * c.k, (13.2f + b * 2) * c.k, 200 + b * 10.0f, 300 - b * 10.0f, 8, Tone(dk, -0.1f));
    Tri(c, {-20, -46}, {-20, -38}, {-48, -52}, pale);                                                                // the rostrum
    for (int s = 0; s < 2; s++) { // eyestalks
        Vector2 b{-16.0f + s * 8, -52.0f}, tip{-22.0f + s * 10, -63.0f - s * 3};
        Limb(c, b, tip, 2.4f, 2.0f, dk);
        Ball(c, tip.x, tip.y, 3.6f, INK); Dot(c, tip.x - 0.8f, tip.y - 0.8f, 0.8f, Color{240, 230, 210, 255});
    }
    for (int a = 0; a < 2; a++) { // long antennae
        Vector2 prev{-20.0f, -46.0f + a * 6};
        for (int i = 1; i <= 7; i++) {
            Vector2 q{-20.0f - i * 6, -46.0f + a * 6 - i * (a ? 1.5f : 3.6f) + sinf(t * 2.2f + i * 0.6f + a * 2) * i * 0.7f};
            Limb(c, prev, q, 1.5f - i * 0.1f, 1.3f - i * 0.1f, dk);
            prev = q;
        }
    }
    Limb2(c, {-14, -30}, {-28, -22}, {-36, -25}, 3, 2.6f, 2.2f, shell); Ball(c, -40, -25, 4.6f, lt);                 // the small claw
    Limb2(c, {-18, -36}, {-40, -30}, {-54, -40}, 7, 6, 5, shell);                                                  // the great snapping arm...
    Rivet(c, -40, -33, 1.3f, dk);
    float open = 0.25f + 0.22f * (0.5f + 0.5f * sinf(t * 3.1f + c.u));
    Claw(c, -62, -42, 15, open, shell, lt, pale, false);                                                             // ...and its oversized claw
    float pulse = 0.5f + 0.5f * sinf(t * 6 + c.u); // the cavitation bubble at the snapping tip
    DrawRing(c.P(-84, -38), (3 + pulse * 5) * c.k, (4.5f + pulse * 5) * c.k, 0, 360, 16, Fade(glow, 0.5f));
    Glow(c.P(-84, -38), 26 * c.k, Fade(glow, 0.12f + 0.1f * pulse));
}

void DysCrustacean(const Ctx& c) {
    const float t = c.t;
    const Color shell{104, 58, 56, 255}, lt{150, 92, 80, 255}, flesh{176, 92, 86, 255}, coral{196, 122, 110, 255}, pale{216, 202, 178, 255}, dk{66, 36, 36, 255};
    // mismatched legs: three on the near side, two on the far, one bent short
    const float lx[5] = {-30, -16, -2, 14, 26}, len[5] = {24, 22, 26, 19, 14};
    for (int i = 0; i < 5; i++) {
        float sw = sinf(t * 5 + i * 1.3f + c.u) * 3;
        Limb2(c, {lx[i], -30}, {lx[i] - 9 - (i == 3 ? 6 : 0), -30 + len[i] * 0.35f - 6}, {lx[i] - 15 + sw, 0}, 3.4f, 2.8f, 1.6f, Tone(dk, i % 2 ? 0.05f : -0.2f));
    }
    Ball(c, -6, -38, 28, shell); Ball(c, 12, -44, 21, Tone(lt, -0.05f)); Crescent(c, -6, -38, 30);                    // a bloated, lopsided carapace
    for (int i = 0; i < 6; i++) Tri(c, {-22.0f + i * 8, -60 + std::abs(i - 2.5f) * 1.5f}, {-16.0f + i * 8, -60 + std::abs(i - 2.5f) * 1.5f}, {-19.0f + i * 8, -72 - (i % 2) * 5}, pale); // spines along the ridge
    Hatch(c, -26, -56, 40, 24, 9, Fade(INK, 0.5f));
    Line(c, {-12, -58}, {-4, -46}, 1.1f, INK); Line(c, {-4, -46}, {-10, -34}, 1.1f, INK); Line(c, {-4, -46}, {6, -42}, 1.1f, INK); // cracks in the shell
    Ball(c, -16, -30, 7.5f, Tone(dk, -0.4f)); Ball(c, -16, -30, 5.6f, flesh); Dot(c, -18, -32, 1.6f, Tone(flesh, 0.4f));  // an open wound
    Ball(c, 22, -60, 9, flesh); Ball(c, 20, -62, 3.6f, pale); Crescent(c, 22, -60, 9);                                 // a tumour
    for (int i = 0; i < 4; i++) Tri(c, {8.0f + i * 5, -60 - (i % 2) * 3}, {13.0f + i * 5, -60 - (i % 2) * 3}, {10.0f + i * 5 + (i % 2 ? 3 : -3), -76 - (i % 3) * 6}, coral); // coral growing from it
    Barnacles(c, -14, -64, 14, 5, c.u); Barnacles(c, 26, -46, 6, 3, c.u + 3);
    // the great claw, raised, and a withered one
    Limb2(c, {-24, -42}, {-46, -60}, {-60, -68}, 8, 7, 6, shell);
    Rivet(c, -46, -60, 1.6f, dk);
    float open = 0.3f + 0.2f * sinf(t * 2 + c.u);
    Claw(c, -70, -70, 16, open, shell, lt, pale, true);
    Limb2(c, {26, -40}, {40, -30}, {46, -20}, 3.4f, 2.8f, 2, Tone(shell, -0.15f)); Ball(c, 48, -17, 4.6f, Tone(shell, -0.1f));
    for (int i = 0; i < 3; i++) { // three eyestalks, one of them bent
        Vector2 b{-10.0f + i * 10, -62.0f}, tip{-14.0f + i * 11 + (i == 2 ? 8 : 0), -76.0f - (i == 1 ? 6 : 0) + (i == 2 ? 6 : 0)};
        Limb(c, b, tip, 2.4f, 1.8f, dk);
        Ball(c, tip.x, tip.y, 3.8f, INK); Dot(c, tip.x - 0.9f, tip.y - 0.9f, 0.8f, Color{230, 226, 190, 255});
    }
    for (int i = 0; i < 4; i++) Line(c, {-32.0f + i * 2, -22}, {-36.0f + i * 2 + sinf(t * 3 + i) * 1.5f, -14}, 0.9f, Tone(dk, -0.2f)); // mouthparts
}

void Lobster(const Ctx& c) { // the mini-boss: armoured, spiked, scarred, with a harpoon still in its back
    const float t = c.t;
    const Color shell{104, 48, 46, 255}, lt{150, 78, 62, 255}, dk{60, 28, 28, 255}, bone{218, 204, 178, 255}, rust{146, 84, 46, 255}, amber{255, 190, 80, 255};
    for (int i = 0; i < 4; i++) { // eight walking legs
        for (int pass = 0; pass < 2; pass++) {
            float x = -18 + i * 13.0f + pass * 4, sw = sinf(t * 4 + i * 1.1f + pass + c.u) * 2.5f;
            Limb2(c, {x, -36}, {x - 9, -20 - (sw > 0 ? sw : 0)}, {x - 16 + sw, 0}, 4.4f, 3.4f, 2.0f, pass ? Tone(shell, -0.12f) : Tone(dk, -0.1f));
            Tri(c, {x - 17 + sw, -1}, {x - 12 + sw, -1}, {x - 16 + sw, 4}, bone);
        }
    }
    for (int i = 0; i < 5; i++) { // the abdomen: banded plates curling down to a tail fan
        float x = 32 + i * 14.0f, y = -46 + i * 5.5f, r = 17 - i * 1.6f;
        Ball(c, x, y, r, i % 2 ? shell : Tone(shell, 0.08f));
        DrawRing(c.P(x, y), (r - 2) * c.k, r * c.k, 200, 340, 12, dk);
        Crescent(c, x, y, r);
        Tri(c, {x - 3, y - r * 0.9f}, {x + 3, y - r * 0.9f}, {x, y - r - 9}, bone);
        Hatch(c, x - r * 0.5f, y - r * 0.2f, r, r * 0.8f, 3, Fade(INK, 0.5f));
    }
    for (int i = -2; i <= 2; i++) Tri(c, {96, -20}, {114 + std::abs(i) * -2.0f, -32 + i * 9}, {110, -14 + i * 7}, i % 2 ? dk : Tone(shell, -0.05f)); // the tail fan
    Ball(c, -4, -56, 31, shell); Ball(c, -22, -54, 22, lt); Crescent(c, -4, -56, 32);                                    // the cephalothorax
    for (int i = 0; i < 6; i++) Tri(c, {-22.0f + i * 7, -80 + std::abs(i - 2.5f) * 2}, {-16.0f + i * 7, -80 + std::abs(i - 2.5f) * 2}, {-19.0f + i * 7, -94 - (i % 2) * 6}, bone); // ridge spikes
    Hatch(c, -24, -74, 44, 30, 10, Fade(INK, 0.5f));
    Line(c, {-16, -74}, {-6, -60}, 1.3f, Color{214, 196, 170, 255}); Line(c, {-10, -72}, {-2, -62}, 1.3f, Color{214, 196, 170, 255}); // scars
    Barnacles(c, 0, -84, 18, 6, c.u); Barnacles(c, 58, -50, 8, 3, c.u + 2); Barnacles(c, -30, -46, 8, 3, c.u + 5);
    // the harpoon still in its back, and the rope trailing from it
    Line(c, {12, -88}, {34, -130}, 2.4f, Color{92, 64, 40, 255});
    Tri(c, {8, -84}, {16, -90}, {12, -76}, rust);
    for (int i = 0; i < 6; i++) Line(c, {12.0f + i * 1.0f, -84.0f + i * 7}, {13.0f + i * 1.0f, -78.0f + i * 7}, 1.0f, Fade(bone, 0.7f));
    Line(c, {34, -130}, {40, -136}, 1.6f, Color{200, 180, 140, 255});
    Tri(c, {-40, -64}, {-40, -52}, {-74, -72}, bone);                                                                    // the rostrum
    for (int s = 0; s < 2; s++) { // eyestalks
        Vector2 b{-34.0f - s * 6, -70.0f}, tip{-38.0f - s * 8, -84.0f + s * 3};
        Limb(c, b, tip, 3, 2.2f, dk); Ball(c, tip.x, tip.y, 4.2f, INK); Dot(c, tip.x - 1, tip.y - 1, 0.9f, amber);
    }
    for (int a = 0; a < 2; a++) { // two long whip antennae
        Vector2 prev{-40.0f, -64.0f + a * 4};
        for (int i = 1; i <= 9; i++) {
            Vector2 q{-40.0f - i * 10.0f, -64.0f + a * 4 - i * (a ? 1.5f : 3.6f) + sinf(t * 2 + i * 0.5f + a * 2) * i * 0.9f};
            Limb(c, prev, q, 2.0f - i * 0.15f, 1.6f - i * 0.12f, dk);
            prev = q;
        }
    }
    Limb2(c, {-24, -44}, {-56, -42}, {-84, -34}, 8, 7, 6, Tone(shell, -0.05f));                                          // the pincer arm
    Claw(c, -96, -34, 14, 0.2f + 0.2f * sinf(t * 2.6f + c.u), shell, lt, bone, false);
    Limb2(c, {-26, -62}, {-60, -90}, {-92, -92}, 12, 10, 8, shell);                                                      // the crusher arm
    for (int i = 0; i < 3; i++) Rivet(c, -50.0f - i * 14, -84.0f - i * 2 + i * 0.5f, 1.8f, dk);
    Barnacles(c, -70, -92, 8, 3, c.u + 7);
    Claw(c, -108, -86, 24, 0.12f + 0.12f * (0.5f + 0.5f * sinf(t * 1.6f + c.u)), shell, lt, bone, true);
    Line(c, {-96, -108}, {-118, -96}, 1.2f, Color{214, 196, 170, 255});                                                  // a scar across the palm
    Glow(c.P(-38, -84), 16 * c.k, Fade(amber, 0.12f));
}

void CrabPup(const Ctx& c, float x, float y, float s, const Color& shell) { // a small crab riding the queen
    Ctx d = c;
    d.cx = c.P(x, y).x; d.by = c.P(x, y).y; d.k = c.k * s;
    for (int i = 0; i < 3; i++) Limb2(d, {-8.0f + i * 8, -6}, {-12.0f + i * 8, -2}, {-16.0f + i * 8, 0}, 1.6f, 1.3f, 0.9f, Tone(shell, -0.3f));
    Ball(d, 0, -10, 11, shell); Ball(d, -3, -13, 6, Tone(shell, 0.12f));
    Limb2(d, {-8, -12}, {-16, -18}, {-20, -20}, 2.4f, 2, 1.6f, shell); Ball(d, -23, -21, 4.4f, Tone(shell, 0.05f));
    for (int i = 0; i < 2; i++) { Limb(d, {-3.0f + i * 6, -19}, {-4.0f + i * 6, -26}, 1.2f, 1, INK); Dot(d, -4.0f + i * 6, -27, 1.6f, INK); }
}

void CrustaceanQueen(const Ctx& c) { // the Cave's level boss: a court on legs, a crowned carapace, an egg sac, two great claws
    const float t = c.t;
    const Color shell{76, 50, 70, 255}, lt{124, 84, 100, 255}, dk{44, 28, 42, 255}, cream{214, 196, 164, 255}, glow{200, 240, 140, 255}, kelp{58, 110, 72, 255}, pearl{240, 236, 226, 255}, gem{130, 230, 200, 255};
    float pulse = 0.5f + 0.5f * sinf(t * 1.6f);
    for (int side = -1; side <= 1; side += 2) // four long legs on each side, two segments each, ending in points
        for (int i = 0; i < 4; i++) {
            float sw = sinf(t * 3 + i * 1.2f + side + c.u) * 4, hx = side * (38 + i * 15.0f), reach = 62 + i * 12.0f;
            Limb2(c, {hx * 0.6f, -76}, {side * (hx * 0.6f + reach * 0.55f), -104 + i * 6}, {side * (hx * 0.6f + reach) + sw, 0},
                  9, 7, 3.4f, Tone(dk, side < 0 ? 0.08f : -0.15f));
            Tri(c, {side * (hx * 0.6f + reach) + sw - 3, -6}, {side * (hx * 0.6f + reach) + sw + 3, -6}, {side * (hx * 0.6f + reach) + sw, 4}, cream);
        }
    for (int i = 0; i < 16; i++) { // the egg sac at her back, glowing
        float a = i * 2.39996f, r2 = 5 + (i % 4) * 2.2f, x = 78 + cosf(a) * (6 + i * 1.6f), y = -66 + sinf(a) * (6 + i * 1.2f) - i * 0.6f;
        Ball(c, x, y, r2, Fade(Tone(glow, -0.25f), 0.88f)); Dot(c, x - r2 * 0.3f, y - r2 * 0.3f, r2 * 0.3f, Fade(WHITE, 0.7f));
    }
    Glow(c.P(80, -68), 60 * c.k, Fade(glow, 0.12f + 0.08f * pulse));
    Ball(c, 0, -108, 88, shell); Ball(c, -14, -122, 62, lt); Ball(c, -26, -130, 30, Tone(lt, 0.12f)); Crescent(c, 0, -108, 90); // the carapace: a hill of armour
    for (int i = 0; i < 12; i++) { // a ring of spines around its rim
        float a = PI * (1.08f + 0.84f * i / 11.0f), x = cosf(a) * 84, y = -108 + sinf(a) * 78;
        Tri(c, {x - 5, y}, {x + 5, y}, {x + cosf(a) * 15, y + sinf(a) * 20}, cream);
    }
    for (int i = 0; i < 6; i++) DrawRing(c.P(0, -108), (26.0f + i * 10) * c.k, (28.4f + i * 10) * c.k, 205, 335, 40, Fade(INK, 0.55f));   // growth rings across the carapace
    for (int i = 0; i < 7; i++) Rivet(c, -54.0f + i * 18, -168.0f + std::abs(i - 3) * 6.0f, 2.4f, Tone(shell, -0.4f));
    for (int i = 0; i < 4; i++) { // cracks that glow with something inside
        float x = -40 + i * 26.0f;
        Line(c, {x, -150}, {x + 8, -128}, 1.6f, INK); Line(c, {x + 8, -128}, {x - 2, -108}, 1.6f, INK);
        Line(c, {x, -150}, {x + 8, -128}, 0.7f, Fade(glow, 0.5f + 0.4f * pulse)); Line(c, {x + 8, -128}, {x - 2, -108}, 0.7f, Fade(glow, 0.5f + 0.4f * pulse));
    }
    Barnacles(c, -50, -60, 20, 7, c.u); Barnacles(c, 44, -158, 24, 7, c.u + 4); Barnacles(c, 62, -100, 10, 4, c.u + 8);
    for (int s = -1; s <= 1; s += 2) for (int i = 0; i < 6; i++) { // the kelp cloak: torn green drapes from the shoulders
        float x = s * (58 + i * 4.0f), sw = sinf(t * 1.4f + i + s) * 5;
        Tri(c, {x - 6, -124}, {x + 6, -124}, {x + sw + s * 6, -50 - i * 6}, i % 2 ? kelp : Tone(kelp, -0.25f));
    }
    for (int i = 0; i < 9; i++) { // a pearl necklace across the breast
        float u = i / 8.0f, x = -52 + u * 104, y = -92 + sinf(u * PI) * 16;
        Ball(c, x, y, 5.0f, pearl);
    }
    Ball(c, 0, -74, 8, Tone(gem, -0.2f)); Dot(c, -2, -76, 3, Tone(gem, 0.5f)); Glow(c.P(0, -74), 28 * c.k, Fade(gem, 0.18f + 0.1f * pulse)); // the pendant
    for (int i = 0; i < 6; i++) { // parasitic worms writhing from beneath, glowing
        float x = -34 + i * 14.0f;
        Vector2 prev{x, -60};
        for (int sg = 1; sg <= 5; sg++) { Vector2 q{x + sinf(t * 3 + i + sg) * 4.0f * sg * 0.5f, -60 + sg * 9.0f}; Limb(c, prev, q, 3.2f - sg * 0.4f, 3.0f - sg * 0.4f, Tone(glow, -0.35f)); prev = q; }
    }
    Ball(c, 0, -76, 26, dk); Ball(c, 0, -70, 20, Tone(dk, -0.4f));                                                        // the maw beneath, in shadow
    for (int i = -3; i <= 3; i++) Tri(c, {i * 6.0f - 2.5f, -84}, {i * 6.0f + 2.5f, -84}, {i * 6.0f, -72 + std::abs(i) * -0.8f}, cream);
    // the court riding on her shoulders
    CrabPup(c, -54, -168, 0.85f, Tone(shell, 0.1f)); CrabPup(c, 40, -178, 0.7f, Tone(shell, 0.2f));
    // eyestalks and the crown
    for (int s = -1; s <= 1; s += 2) {
        Vector2 b{s * 22.0f, -172.0f}, tip{s * 30.0f, -206.0f + sinf(t * 1.3f + s) * 2};
        Limb(c, b, tip, 8, 6, dk); Ball(c, tip.x, tip.y, 8.5f, INK); Dot(c, tip.x - 2, tip.y - 2, 1.6f, Color{255, 240, 200, 255});
        Glow(c.P(tip.x, tip.y), 24 * c.k, Fade(glow, 0.14f));
    }
    for (int i = -3; i <= 3; i++) { // the crown of bone
        float h = 30 + (3 - std::abs(i)) * 8;
        Tri(c, {i * 8.0f - 4, -178}, {i * 8.0f + 4, -178}, {i * 8.0f, -178 - h}, cream);
        Tri(c, {i * 8.0f - 4, -178}, {i * 8.0f, -178}, {i * 8.0f, -178 - h}, Tone(cream, -0.25f));
    }
    Line(c, {-28, -178}, {28, -178}, 5, Tone(cream, -0.3f));
    Ball(c, 0, -184, 6.5f, Tone(gem, -0.15f)); Dot(c, -1.6f, -185.6f, 2, Tone(gem, 0.6f)); Glow(c.P(0, -184), 30 * c.k, Fade(gem, 0.2f + 0.1f * pulse));
    // the great claws: left, raised at the party, and right
    Limb2(c, {-74, -120}, {-122, -168}, {-152, -176}, 17, 15, 11, shell);
    for (int i = 0; i < 4; i++) Rivet(c, -100.0f - i * 14, -146.0f - i * 6, 2.6f, dk);
    Barnacles(c, -128, -170, 12, 4, c.u + 1);
    Claw(c, -170, -172, 36, 0.16f + 0.26f * (0.5f + 0.5f * sinf(t * 1.5f + c.u)), shell, lt, cream, true);
    Limb2(c, {74, -120}, {112, -150}, {136, -142}, 14, 12, 9, Tone(shell, -0.08f));
    Ball(c, 150, -138, 26, Tone(shell, -0.05f)); Ball(c, 146, -144, 15, Tone(lt, -0.1f)); Crescent(c, 150, -138, 26);
    for (int i = 0; i < 3; i++) Tri(c, {140.0f + i * 8, -158}, {146.0f + i * 8, -158}, {143.0f + i * 8, -170}, cream);
}

// ============================================================ THE ISLAND: the tribe
void TribalSpearman(const Ctx& c) {
    const float t = c.t;
    const Color skin{150, 104, 72, 255}, skinDk{112, 76, 52, 255}, cloth{96, 82, 64, 255}, red{170, 60, 44, 255}, bone{214, 204, 180, 255}, teal{60, 110, 100, 255}, leather{92, 62, 40, 255}, wood{120, 82, 48, 255};
    float breathe = sinf(t * 1.8f + c.u) * 1.2f;
    Limb2(c, {6, -54}, {2, -30}, {-4, -4}, 10, 8, 6, skinDk);                                                             // the rear leg
    Limb2(c, {-2, -54}, {-10, -30}, {-16, -3}, 11, 9, 6.5f, skin);                                                        // the front leg
    for (int i = 0; i < 3; i++) Line(c, {-11.0f + i * 0.6f - 2, -28.0f + i * 7}, {-8.0f + i * 0.6f + 2, -26.0f + i * 7}, 2.2f, cloth);  // shin wraps
    DrawRing(c.P(-15, -8), 5.2f * c.k, 7 * c.k, 0, 360, 10, bone);                                                          // an anklet of bone
    Bar(c, -26, -5, 20, 4, INK); Bar(c, -8, -5, 16, 4, INK);                                                                // sandals
    Quad(c, {-9, -58}, {9, -58}, {12, -28}, {-6, -30}, red); Jag(c, {-6, -30}, {12, -28}, 4, 6, Tone(red, -0.2f), c.u); // a loincloth, ragged
    Quad(c, {-4, -58}, {6, -58}, {8, -36}, {-2, -38}, Tone(cloth, -0.1f));
    Limb(c, {-14, -57}, {14, -57}, 5, 5, leather); Ball(c, -2, -56, 4.4f, bone); Dot(c, -3.4f, -57, 1, INK); Dot(c, -0.6f, -57, 1, INK); // a skull belt buckle
    Limb(c, {0, -58}, {-2 + breathe * 0.2f, -98 + breathe}, 17, 21, skin);                                                  // a muscled torso
    Crescent(c, 0, -78, 17);
    for (int i = 0; i < 4; i++) Limb(c, {-12, -92.0f + i * 7 + breathe * 0.5f}, {7, -90.0f + i * 7 + breathe * 0.5f}, 3.2f, 3.2f, i % 2 ? Tone(bone, -0.1f) : bone); // ribs of bone plate armour
    Limb(c, {-14, -98}, {8, -60}, 3, 3, leather); Ball(c, 4, -66, 4.2f, leather); Rivet(c, 4, -66, 1.2f, bone);          // a harness and a fetish pouch
    for (int i = 0; i < 5; i++) Tri(c, {-7.0f + i * 3.2f, -98}, {-4.0f + i * 3.2f, -98}, {-5.5f + i * 3.2f, -92 - (i % 2) * 2}, bone); // a tooth necklace
    Ball(c, 12, -100, 8, Tone(skinDk, -0.1f)); Ball(c, -14, -100, 10, bone); Rivet(c, -14, -100, 1.6f, Tone(bone, -0.4f)); // shoulder pads
    for (int i = 0; i < 3; i++) Tri(c, {-19.0f + i * 4, -104}, {-15.0f + i * 4, -104}, {-17.0f + i * 4, -118 - (i % 2) * 4}, i % 2 ? red : teal); // feathers on the pad
    Limb2(c, {-14, -96}, {-26, -82}, {-30, -70}, 8, 7, 6, skin);                                                            // the shield arm
    Ball(c, -34, -72, 22, leather); Ball(c, -36, -74, 17, Tone(wood, 0.1f));                                               // a shield of woven reed
    DrawRing(c.P(-34, -72), 17 * c.k, 22 * c.k, 0, 360, 24, red);
    for (int i = 0; i < 4; i++) Line(c, {-34.0f - 15 * cosf(i * PI / 4), -72.0f - 15 * sinf(i * PI / 4)}, {-34.0f + 15 * cosf(i * PI / 4), -72.0f + 15 * sinf(i * PI / 4)}, 1.0f, Tone(wood, -0.4f));
    Ball(c, -34, -72, 6, bone); Dot(c, -36, -73, 1.5f, INK); Dot(c, -32, -73, 1.5f, INK);                                    // a skull boss on the shield
    Limb2(c, {8, -96}, {-4, -94}, {-20, -102}, 7, 6.5f, 6, skinDk);                                                         // the spear arm, cocked
    float thrust = sinf(t * 1.3f + c.u) * 2;
    Line(c, {16, -90}, {-84 - thrust, -112}, 2.6f, wood);                                                                   // the spear
    Tri(c, {-84 - thrust, -114}, {-84 - thrust, -110}, {-104 - thrust, -113}, Color{34, 34, 40, 255});                     // an obsidian head
    Line(c, {-82 - thrust, -112}, {-90 - thrust, -112}, 3.2f, red);
    for (int i = 0; i < 3; i++) Line(c, {-70.0f - thrust - i * 2, -111.0f}, {-72.0f - thrust - i * 2, -100.0f - i * 3}, 0.9f, Tone(bone, -0.1f)); // trophy tassels
    Ball(c, -8, -112, 12, skin); Crescent(c, -8, -112, 12);                                                                 // the head...
    Ball(c, -11, -111, 12.5f, bone);                                                                                        // ...under a skull mask
    Bar(c, -18, -114, 6, 5, INK); Bar(c, -10, -114, 6, 5, INK);                                                             // black eye holes
    Dot(c, -15, -112, 0.9f, Color{255, 190, 90, 255}); Dot(c, -7, -112, 0.9f, Color{255, 190, 90, 255});
    for (int i = 0; i < 4; i++) Bar(c, -18.0f + i * 3, -104, 2, 3.4f, INK);                                                 // its grin of teeth
    Line(c, {-19, -108}, {-14, -100}, 1.2f, red); Line(c, {-11, -108}, {-6, -100}, 1.2f, red);                             // warpaint
    for (int i = -2; i <= 2; i++) Tri(c, {-8.0f + i * 4, -122}, {-4.0f + i * 4, -122}, {-6.0f + i * 4 + i * 2, -138 - (i % 2 ? 0 : 6)}, i % 2 ? red : teal); // a crest of feathers
}

void WarDog(const Ctx& c) {
    const float t = c.t;
    const Color fur{120, 108, 96, 255}, dk{84, 74, 66, 255}, lt{160, 146, 130, 255}, bone{214, 204, 190, 255}, red{255, 90, 60, 255}, iron{74, 76, 82, 255};
    float run = sinf(t * 5 + c.u) * 2.4f, breathe = sinf(t * 2.4f + c.u) * 0.8f;
    Limb2(c, {24, -34}, {30, -18}, {26 - run, 0}, 8, 5, 3, dk); Limb2(c, {-18, -34}, {-24, -18}, {-30 + run, 0}, 8, 5, 3, dk);   // far legs, in shadow
    Limb(c, {-16, -44}, {26, -44}, 18, 16, fur);                                                                                    // an emaciated, muscled body
    Crescent(c, 4, -44, 17);
    for (int i = 0; i < 5; i++) Line(c, {-12.0f + i * 7, -50 + breathe}, {-10.0f + i * 7, -34}, 1.6f, Tone(bone, -0.1f));           // exposed ribs
    for (int i = 0; i < 3; i++) { Quad(c, {-8.0f + i * 14, -56}, {2.0f + i * 14, -56}, {0.0f + i * 14, -50}, {-6.0f + i * 14, -50}, bone); Rivet(c, -3.0f + i * 14, -54, 1.1f, iron); } // bone armour plates
    Hatch(c, -14, -46, 36, 14, 8, Fade(INK, 0.5f));
    Limb2(c, {20, -34}, {28, -16}, {22 - run, 0}, 9, 6, 3.4f, fur); Limb2(c, {-14, -34}, {-20, -16}, {-24 + run, 0}, 9, 6, 3.4f, lt); // near legs
    for (int i = 0; i < 3; i++) { Tri(c, {-27.0f + run + i * 2, -1}, {-23.0f + run + i * 2, -1}, {-26.0f + run + i * 2, 3}, bone); Tri(c, {19.0f - run + i * 2, -1}, {23.0f - run + i * 2, -1}, {20.0f - run + i * 2, 3}, bone); }
    Limb2(c, {28, -50}, {42, -58}, {46, -74}, 5, 3.6f, 2, dk);                                                                      // a bristled tail
    for (int i = 0; i < 4; i++) Tri(c, {36.0f + i * 3, -58 - i * 3}, {38.0f + i * 3, -57 - i * 3}, {40.0f + i * 4, -66 - i * 4}, fur);
    Ball(c, -22, -50, 16, fur);                                                                                                     // the chest and shoulder
    for (int i = 0; i < 3; i++) Line(c, {-24.0f + i * 5, -62}, {-16.0f + i * 5, -46}, 2.2f, Fade(bone, 0.85f));                     // ash warpaint
    for (int i = 0; i < 4; i++) Tri(c, {-28.0f + i * 5, -62 - (i % 2)}, {-24.0f + i * 5, -62}, {-26.0f + i * 5, -70 - (i % 2) * 2}, dk); // a ridge of mane
    Ball(c, -46, -52, 13, fur); Crescent(c, -46, -52, 13);                                                                          // the head
    Limb(c, {-54, -50}, {-70, -47}, 9, 6, fur);                                                                                     // the snout
    float jaw = 5 + sinf(t * 3.4f + c.u) * 2;
    Limb(c, {-52, -44}, {-68, -38 + jaw * 0.3f}, 5, 3.4f, dk);                                                                      // the open lower jaw
    for (int i = 0; i < 4; i++) { Tri(c, {-60.0f - i * 3, -46}, {-58.0f - i * 3, -46}, {-59.0f - i * 3, -41}, bone); Tri(c, {-59.0f - i * 3, -39 + jaw * 0.3f}, {-57.0f - i * 3, -39 + jaw * 0.3f}, {-58.0f - i * 3, -44 + jaw * 0.3f}, bone); } // teeth
    Limb(c, {-58, -40 + jaw * 0.25f}, {-66, -36 + jaw * 0.3f}, 2.6f, 2, Color{170, 50, 50, 255});                                    // a lolling tongue
    Ball(c, -71, -47, 3, INK);                                                                                                       // the nose
    Tri(c, {-42, -62}, {-36, -62}, {-40, -76}, fur); Tri(c, {-50, -62}, {-46, -62}, {-52, -72}, dk); Tri(c, {-49, -64}, {-47, -62}, {-50, -66}, INK); // one ear erect, one torn
    Bar(c, -52, -58, 14, 4, INK); Dot(c, -49, -56, 1.2f, red); Dot(c, -44, -56, 1.2f, red);                                        // eyes in a black brow, red glints
    Line(c, {-49, -60}, {-44, -50}, 1.0f, Color{200, 190, 176, 255});                                                                // an old scar
    DrawRing(c.P(-34, -50), 8 * c.k, 11 * c.k, 20, 160, 10, iron);                                                                   // a spiked iron collar
    for (int i = 0; i < 5; i++) Tri(c, {-42.0f + i * 4.6f, -58}, {-39.0f + i * 4.6f, -58}, {-40.5f + i * 4.6f, -64}, iron);
    Limb(c, {-34, -42}, {-28, -30}, 1.6f, 1.6f, iron); for (int i = 0; i < 4; i++) DrawRing(c.P(-28.0f + i * 3, -28.0f + i * 6), 1.5f * c.k, 2.4f * c.k, 0, 360, 6, iron); // a broken chain
    Line(c, {-66, -38}, {-64, -28 + sinf(t * 2 + c.u) * 1.5f}, 0.7f, Fade(WHITE, 0.6f));                                             // drool
}

void TribalShaman(const Ctx& c) {
    const float t = c.t;
    const Color skin{140, 100, 70, 255}, robe{88, 70, 56, 255}, teal{60, 96, 90, 255}, red{170, 60, 44, 255}, glow{150, 230, 120, 255}, bone{214, 204, 180, 255}, wood{110, 84, 50, 255};
    float sway = sinf(t * 1.2f + c.u) * 1.4f;
    Limb2(c, {4, -46}, {0, -24}, {-6, -3}, 6.4f, 5, 4, Tone(skin, -0.25f)); Limb2(c, {-2, -46}, {-8, -24}, {-14, -2}, 7, 5.6f, 4.2f, skin); // thin, bare legs
    for (int i = 0; i < 2; i++) DrawRing(c.P(-13.0f + i * 7, -7), 3.6f * c.k, 5.2f * c.k, 0, 360, 8, bone);                              // bone anklets
    Quad(c, {-16, -88}, {14, -88}, {20, -14}, {-22, -14}, robe);                                                                        // a long robe
    Quad(c, {-16, -88}, {-4, -88}, {-2, -14}, {-22, -14}, Tone(robe, 0.1f));
    Quad(c, {-4, -60}, {14, -60}, {20, -14}, {2, -14}, Tone(robe, -0.2f));                                                              // a second, darker layer
    Jag(c, {-22, -14}, {20, -14}, 8, 8, Tone(robe, -0.3f), c.u);
    for (int i = 0; i < 4; i++) { Line(c, {-10.0f + i * 5, -70.0f + i * 6}, {-7.0f + i * 5, -62.0f + i * 6}, 1.1f, Fade(glow, 0.6f + 0.3f * sinf(t * 2 + i))); Dot(c, -7.0f + i * 5, -62.0f + i * 6, 1.0f, Fade(glow, 0.8f)); } // glowing runes
    Limb(c, {-18, -52}, {16, -52}, 4.4f, 4.4f, Color{92, 62, 40, 255});                                                                 // a belt hung with totems
    Line(c, {-12, -52}, {-13, -34 + sway * 0.4f}, 1.1f, bone); Ball(c, -13, -32 + sway * 0.4f, 4.4f, bone); Dot(c, -14.4f, -33, 1, INK); Dot(c, -11.6f, -33, 1, INK);
    Line(c, {-2, -52}, {-2, -36}, 1.1f, bone); Ball(c, -2, -33, 5.4f, Color{136, 112, 58, 255}); Ball(c, -2, -38, 2, Color{92, 62, 40, 255});
    Line(c, {9, -52}, {10, -38}, 1.1f, bone); Tri(c, {6, -38}, {14, -38}, {10, -26}, Color{110, 130, 130, 255}); Dot(c, 10, -34, 0.9f, INK);
    for (int row = 0; row < 3; row++) // a cloak of feathers over the hunched shoulders, layered like scales
        for (int i = 0; i < 6; i++) {
            float x = -22.0f + i * 7.5f + row * 2, y = -92.0f + row * 9;
            Tri(c, {x - 4, y}, {x + 4, y}, {x + sinf(t * 1.6f + i + row) * 1.6f, y + 15}, (i + row) % 3 == 0 ? red : (i + row) % 3 == 1 ? teal : Tone(teal, -0.4f));
        }
    Ball(c, 14, -90, 9, Tone(skin, -0.2f)); Ball(c, -14, -92, 9, skin);                                                                  // hunched shoulders
    Limb2(c, {-14, -88}, {-26, -78}, {-32, -68}, 6, 5, 4.6f, skin);                                                                     // the staff arm
    Limb2(c, {12, -86}, {14, -74}, {8, -64}, 5.4f, 4.6f, 4, Tone(skin, -0.2f));                                                         // the rattle arm
    Ball(c, 6, -62, 5.6f, Color{136, 112, 58, 255}); for (int i = 0; i < 3; i++) Dot(c, 3.0f + i * 3, -63, 0.7f, Tone(wood, -0.5f)); // a gourd rattle
    // the staff: bone and feathers, topped by a glowing skull and orbiting motes
    Line(c, {-32, -2}, {-38, -122}, 2.8f, wood);
    for (int i = 0; i < 4; i++) Line(c, {-33.0f - i * 1.5f, -30.0f - i * 22}, {-36.0f - i * 1.5f, -26.0f - i * 22}, 4.0f, Tone(wood, -0.3f)); // lashings
    Ball(c, -38, -128, 8, bone); Dot(c, -40.5f, -129, 1.7f, INK); Dot(c, -35.5f, -129, 1.7f, INK); Bar(c, -41, -123, 6, 2, INK);
    float pulse = 0.5f + 0.5f * sinf(t * 2.6f + c.u);
    Ball(c, -38, -142, 6, Tone(glow, -0.15f)); Glow(c.P(-38, -142), 46 * c.k, Fade(glow, 0.2f + 0.16f * pulse));
    for (int i = 0; i < 4; i++) { float a = t * 1.8f + i * 1.57f; Dot(c, -38 + cosf(a) * 14, -142 + sinf(a) * 5, 1.4f, Fade(glow, 0.9f)); }
    for (int i = 0; i < 2; i++) Tri(c, {-39.0f + i * 2, -122}, {-37.0f + i * 2, -122}, {-42.0f + i * 5 + sinf(t * 2 + i) * 2, -104}, i ? red : teal); // ribbons
    Ball(c, -18, -100, 11, skin); Crescent(c, -18, -100, 11);                                                                            // a bowed head...
    Tri(c, {-24, -104}, {-14, -104}, {-30, -88}, bone); Tri(c, {-22, -103}, {-15, -103}, {-28, -90}, Tone(bone, -0.2f));               // ...in a beaked bird mask
    Bar(c, -24, -106, 6, 4, INK); Bar(c, -16, -106, 6, 4, INK); Dot(c, -21, -104, 1, glow); Dot(c, -13, -104, 1, glow);
    for (int i = -3; i <= 3; i++) { // a fan of feathers behind the head
        float a = -PI / 2 + i * 0.28f, len = 30 - std::abs(i) * 3;
        Tri(c, {-16 + i * 2.0f - 3, -108}, {-16 + i * 2.0f + 3, -108}, {-16 + cosf(a) * len + 8, -108 + sinf(a) * len}, i % 2 ? red : teal);
    }
}

void TribalDemigod(const Ctx& c) { // the Island's mini-boss: a mountain of a man in a wooden idol mask
    const float t = c.t;
    const Color skin{110, 84, 60, 255}, skinDk{80, 60, 42, 255}, wood{150, 96, 50, 255}, gold{255, 180, 60, 255}, bone{214, 204, 180, 255}, obsidian{28, 26, 32, 255}, red{170, 60, 44, 255}, teal{60, 110, 100, 255}, hide{104, 76, 52, 255};
    float breathe = sinf(t * 1.4f + c.u) * 1.6f, pulse = 0.5f + 0.5f * sinf(t * 2.2f + c.u);
    Limb2(c, {14, -80}, {20, -44}, {16, -6}, 22, 18, 13, skinDk);                                                                        // the rear leg
    Limb2(c, {-8, -80}, {-16, -44}, {-22, -4}, 25, 20, 14, skin);                                                                        // the front leg
    for (int i = 0; i < 3; i++) { Quad(c, {-32.0f, -36.0f + i * 9}, {-12.0f, -36.0f + i * 9}, {-12.0f, -30.0f + i * 9}, {-32.0f, -30.0f + i * 9}, bone); Tri(c, {-32.0f, -36.0f + i * 9}, {-32.0f, -32.0f + i * 9}, {-38.0f, -34.0f + i * 9}, bone); } // bone greaves with spikes
    Bar(c, -36, -6, 40, 6, INK); Bar(c, 0, -6, 30, 6, INK);
    Quad(c, {-22, -84}, {22, -84}, {26, -34}, {-20, -30}, hide); Jag(c, {-20, -30}, {26, -34}, 7, 10, Tone(hide, -0.25f), c.u);          // a hide loincloth
    Limb(c, {-24, -84}, {26, -84}, 8, 8, Color{60, 42, 28, 255});
    for (int i = 0; i < 6; i++) { Ball(c, -18.0f + i * 7.2f, -84, 3.0f, bone); Dot(c, -19.0f + i * 7.2f, -84.5f, 0.7f, INK); Dot(c, -17.2f + i * 7.2f, -84.5f, 0.7f, INK); } // a belt of little skulls
    Ball(c, 0, -80, 8.5f, bone); Dot(c, -2.6f, -81, 1.6f, INK); Dot(c, 2.6f, -81, 1.6f, INK);                                            // a great skull buckle
    Limb(c, {0, -84}, {-4 + breathe * 0.2f, -142 + breathe}, 34, 44, skin);                                                                // an enormous chest
    Crescent(c, -2, -112, 36);
    Hatch(c, -22, -128, 36, 34, 9, Fade(INK, 0.4f));
    for (int i = 0; i < 4; i++) { // war paint runes that glow amber
        float y = -132.0f + i * 10;
        Line(c, {-16, y}, {12, y + 3}, 1.6f, Fade(gold, 0.55f + 0.35f * pulse));
        Line(c, {-4, y - 3}, {-2, y + 6}, 1.6f, Fade(gold, 0.5f + 0.3f * pulse));
    }
    Glow(c.P(-2, -118), 60 * c.k, Fade(gold, 0.08f + 0.06f * pulse));
    for (int i = 0; i < 7; i++) { float u = i / 6.0f; Ball(c, -24 + u * 44, -142 + sinf(u * PI) * 12, 5.2f, bone); Dot(c, -25.5f + u * 44, -143.0f + sinf(u * PI) * 12, 1.2f, INK); Dot(c, -22.5f + u * 44, -143.0f + sinf(u * PI) * 12, 1.2f, INK); } // a necklace of skulls
    for (int i = -4; i <= 4; i++) { // the feather mantle behind the shoulders
        float x = i * 8.0f;
        Tri(c, {x - 5, -150}, {x + 5, -150}, {x * 1.4f + sinf(t * 1.1f + i) * 2, -196 + std::abs(i) * 3}, i % 2 ? red : teal);
    }
    Ball(c, -34, -148, 17, wood); Rivet(c, -34, -148, 2, Tone(wood, -0.4f));                                                             // carved pauldrons with tusks
    Ball(c, 30, -148, 15, Tone(wood, -0.1f)); Rivet(c, 30, -148, 2, Tone(wood, -0.4f));
    Tri(c, {-44, -156}, {-36, -160}, {-50, -182}, bone); Tri(c, {36, -154}, {28, -158}, {44, -178}, Tone(bone, -0.15f));
    Limb2(c, {-34, -144}, {-52, -114}, {-58, -92}, 16, 14, 12, skin);                                                                    // the club arm
    Limb2(c, {-52, -114}, {-58, -92}, {-58, -88}, 13, 12, 11, Tone(bone, -0.05f));                                                       // a spiked bracer
    Tri(c, {-62, -108}, {-58, -114}, {-68, -112}, bone);
    Limb(c, {-60, -84}, {-98, -164}, 9, 18, wood);                                                                                       // a greatclub...
    for (int i = 0; i < 5; i++) { float u = 0.4f + i * 0.12f; Tri(c, {-60 - 38 * u - 4, -84 - 80 * u}, {-60 - 38 * u + 4, -84 - 80 * u - 5}, {-60 - 38 * u - 12, -84 - 80 * u - 6}, obsidian); } // ...set with obsidian teeth
    Ball(c, -100, -168, 17, wood); Crescent(c, -100, -168, 17);
    for (int i = 0; i < 3; i++) Line(c, {-70.0f - i * 8, -100.0f - i * 16}, {-64.0f - i * 8, -98.0f - i * 16}, 3.4f, red);            // bindings
    Limb2(c, {32, -144}, {50, -124}, {56, -146}, 15, 13, 11, Tone(skin, -0.12f));                                                        // the raised arm, cradling an ember
    Ball(c, 58, -156, 11, Tone(gold, -0.2f)); Glow(c.P(58, -158), 60 * c.k, Fade(gold, 0.22f + 0.14f * pulse));
    for (int i = 0; i < 5; i++) Tri(c, {52.0f + i * 3, -162}, {56.0f + i * 3, -162}, {54.0f + i * 3 + sinf(t * 9 + i) * 2, -178 - (i % 3) * 6 - pulse * 4}, i % 2 ? gold : Color{255, 120, 40, 255}); // its flames
    Ball(c, -6, -158, 17, skinDk);                                                                                                       // the head, behind the mask
    Quad(c, {-24, -176}, {14, -176}, {12, -140}, {-22, -140}, wood);                                                                     // the carved idol mask
    Line(c, {-24, -176}, {-22, -140}, 1.6f, Tone(wood, 0.3f));
    Quad(c, {-16, -168}, {6, -168}, {6, -164}, {-16, -164}, INK);                                                                       // the brow: eyes hidden
    Bar(c, -14, -167, 6, 2.4f, INK); Bar(c, -2, -167, 6, 2.4f, INK); Dot(c, -11, -166, 0.8f, gold); Dot(c, 1, -166, 0.8f, gold);
    Tri(c, {-6, -164}, {-2, -164}, {-4, -152}, Tone(wood, -0.3f));                                                                       // a nose ridge
    for (int i = 0; i < 7; i++) Bar(c, -17.0f + i * 4.4f, -148, 3, 6, i % 2 ? bone : Tone(bone, -0.1f));                                  // a wide grin of teeth
    Line(c, {-18, -148}, {8, -148}, 1.2f, INK);
    Ball(c, -4, -181, 4, gold); Glow(c.P(-4, -181), 14 * c.k, Fade(gold, 0.25f));                                                        // a gold brow ornament
    for (int i = -4; i <= 4; i++) { // a tall crown of feathers
        float h = 30 + (4 - std::abs(i)) * 5;
        Tri(c, {-4.0f + i * 5 - 3.5f, -178}, {-4.0f + i * 5 + 3.5f, -178}, {-4.0f + i * 6.4f + sinf(t * 1.3f + i) * 1.5f, -178 - h}, i % 2 ? red : teal);
    }
}
// ============================================================ THE WEEDS: the merfolk
// a fish tail from the hips down: scaled segments swaying, ending in a fluke. `tip` is where the last segment lands.
void FishTail(const Ctx& c, float hx, float hy, float w, float len, float sway, Color scale, Color fin, Color belly, float finSize, bool ribs) {
    Vector2 prev{hx, hy};
    const int N = 7;
    for (int i = 1; i <= N; i++) {
        float u = (float)i / N;
        Vector2 q{hx + sinf(c.t * 2.0f + i * 0.7f + c.u) * sway * u + u * len * 0.12f, hy + u * (0 - hy)};
        float w0 = w * (1.0f - (i - 1) * 0.11f), w1 = w * (1.0f - i * 0.11f);
        Limb(c, prev, q, w0, w1, i % 2 ? scale : Tone(scale, -0.1f));
        DrawRing(c.P(q.x, q.y), std::max(1.0f, (w1 * 0.5f - 1.2f)) * c.k, (w1 * 0.5f + 0.6f) * c.k, 210, 330, 8, Tone(scale, -0.4f));   // a scale ring
        if (i < N - 1) Tri(c, {q.x + w1 * 0.4f, q.y - 3}, {q.x + w1 * 0.4f, q.y + 3}, {q.x + w1 * 0.4f + finSize * (1.0f - u * 0.5f), q.y}, fin);       // dorsal fin spines
        prev = q;
    }
    Limb(c, {hx - 1, hy + 3}, {prev.x - 1, prev.y - 4}, w * 0.5f, w * 0.2f, belly);                                                                 // the pale belly stripe
    for (int s = -1; s <= 1; s += 2) { // the fluke, two great lobes with rays
        Vector2 tipA{prev.x + s * finSize * 1.7f, prev.y + 2 + sinf(c.t * 2.4f + s + c.u) * 3};
        Tri(c, {prev.x, prev.y - 8}, tipA, {prev.x + s * 4.0f, prev.y + 5}, s < 0 ? Tone(fin, -0.25f) : fin);
        if (ribs) for (int r = 1; r <= 3; r++) Line(c, {prev.x, prev.y - 4}, {prev.x + (tipA.x - prev.x) * r / 3.2f, prev.y + (tipA.y - prev.y) * r / 3.2f}, 0.8f, Fade(INK, 0.55f));
    }
}

void FeralMerman(const Ctx& c) {
    const float t = c.t;
    const Color skin{104, 138, 128, 255}, scale{70, 100, 104, 255}, fin{124, 168, 166, 255}, belly{196, 210, 196, 255}, glow{150, 230, 220, 255}, bone{224, 214, 190, 255}, kelp{64, 124, 84, 255};
    float hover = sinf(t * 1.6f + c.u) * 2, pulse = 0.5f + 0.5f * sinf(t * 3 + c.u);
    FishTail(c, 2, -56 + hover, 15, 60, 8, scale, fin, belly, 12, true);
    for (int i = 0; i < 6; i++) Dot(c, 4.0f + i * 1.5f, -46.0f + i * 7 + hover, 1.2f, Fade(glow, 0.4f + 0.5f * sinf(t * 2 + i + c.u)));      // bioluminescent spots down the tail
    Limb(c, {2, -58 + hover}, {-8, -98 + hover}, 21, 26, skin);                                                                                    // a hunched, muscled torso
    Crescent(c, -2, -80 + hover, 22);
    for (int i = 0; i < 4; i++) Limb(c, {-10, -92.0f + i * 7 + hover}, {8, -90.0f + i * 7 + hover}, 3, 3, Tone(belly, -0.15f));                    // pale belly plates
    for (int r = 0; r < 4; r++) for (int i = 0; i < 4; i++) DrawRing(c.P(-10.0f + i * 6 + (r % 2) * 3, -90.0f + r * 6 + hover), 2.2f * c.k, 3.2f * c.k, 200, 340, 6, Fade(INK, 0.5f)); // scales
    for (int i = 0; i < 3; i++) Line(c, {-16, -102.0f + i * 3 + hover}, {-12, -100.0f + i * 3 + hover}, 1.0f, INK);                                // gill slits
    for (int i = 0; i < 5; i++) Tri(c, {6.0f + i * 1.0f, -98.0f + i * 7 + hover}, {6.0f + i * 1.0f, -92.0f + i * 7 + hover}, {16.0f + i * 1.0f, -96.0f + i * 7 + hover}, fin); // spines down the back
    Ball(c, 4, -98 + hover, 10, Tone(skin, -0.15f)); Ball(c, -18, -98 + hover, 11, Tone(fin, -0.1f));                                               // shoulders, one finned
    for (int i = 0; i < 3; i++) Tri(c, {-24.0f + i * 4, -104 + hover}, {-20.0f + i * 4, -104 + hover}, {-22.0f + i * 4, -116 - (i % 2) * 3 + hover}, fin);
    Barnacles(c, 6, -104 + hover, 8, 4, c.u);
    Limb2(c, {4, -96 + hover}, {-6, -76 + hover}, {-14, -60 + hover}, 8, 6.4f, 5, Tone(skin, -0.2f));                                              // the rear arm
    for (int i = 0; i < 3; i++) Tri(c, {-14.0f - i * 2, -60 + hover}, {-11.0f - i * 2, -58 + hover}, {-18.0f - i * 3, -50 + i * 2 + hover}, bone);  // claws
    Limb2(c, {-16, -96 + hover}, {-36, -84 + hover}, {-50, -70 + sinf(t * 2 + c.u) * 2 + hover}, 9, 7, 5.6f, skin);                                 // the reaching arm
    for (int i = 0; i < 4; i++) Tri(c, {-32.0f - i * 5, -88 + i * 1.5f + hover}, {-32.0f - i * 5, -81 + i * 1.5f + hover}, {-36.0f - i * 5, -85 + i * 2.2f + hover}, Tone(fin, 0.1f)); // fins along the forearm
    for (int i = 0; i < 4; i++) { Tri(c, {-50.0f - i * 1.5f, -72 + i * 2 + hover}, {-48.0f - i * 1.5f, -68 + i * 2 + hover}, {-62.0f - i * 2, -68 + i * 3.4f + hover}, bone); } // long claws
    Line(c, {-30, -90 + hover}, {-8, -70 + hover}, 1.2f, Fade(Color{190, 176, 140, 255}, 0.85f)); Line(c, {-34, -80 + hover}, {-6, -84 + hover}, 1.2f, Fade(Color{190, 176, 140, 255}, 0.85f)); // torn net wound round the arm
    for (int i = 0; i < 3; i++) Line(c, {-24.0f - i * 6, -80 + hover}, {-26.0f - i * 6 + sinf(t * 2 + i) * 3, -64 + i * 3 + hover}, 1.6f, kelp);  // weed trailing from it
    Line(c, {2, -102 + hover}, {16, -122 + hover}, 2.0f, Color{92, 64, 40, 255}); Tri(c, {-2, -100 + hover}, {6, -104 + hover}, {2, -94 + hover}, Color{130, 84, 46, 255}); // a harpoon head stuck in the shoulder
    Ball(c, -22, -110 + hover, 12, skin); Crescent(c, -22, -110 + hover, 12);                                                                       // the head, hung forward
    for (int i = 0; i < 5; i++) Tri(c, {-26.0f + i * 3.4f, -118 + hover}, {-23.0f + i * 3.4f, -118 + hover}, {-25.0f + i * 3.4f, -132 - (i % 2) * 5 + hover}, i % 2 ? fin : Tone(fin, -0.2f)); // a crest of fins
    Bar(c, -34, -114 + hover, 16, 5, INK); Dot(c, -30, -111.6f + hover, 1.2f, glow); Dot(c, -24, -111.6f + hover, 1.2f, glow);                        // eyes in a black brow
    float jaw = 4 + sinf(t * 3 + c.u) * 2;
    Limb(c, {-30, -102 + hover}, {-44, -98 + jaw + hover}, 6, 4, Tone(skin, -0.2f));                                                                // the lower jaw
    for (int i = 0; i < 5; i++) { Tri(c, {-32.0f - i * 2.6f, -103 + hover}, {-30.0f - i * 2.6f, -103 + hover}, {-31.0f - i * 2.6f, -97 + hover}, bone); Tri(c, {-33.0f - i * 2.4f, -98 + jaw * 0.8f + hover}, {-31.0f - i * 2.4f, -98 + jaw * 0.8f + hover}, {-32.0f - i * 2.4f, -103 + jaw * 0.8f + hover}, bone); } // needle teeth
    Glow(c.P(-27, -112 + hover), 20 * c.k, Fade(glow, 0.08f + 0.06f * pulse));
}

void Siren(const Ctx& c) { // a drowned singer: pale, bandaged, her song made visible
    const float t = c.t;
    const Color skin{198, 208, 198, 255}, scale{84, 120, 120, 255}, fin{148, 178, 170, 255}, glow{200, 226, 210, 255}, bone{224, 214, 192, 255}, hair{46, 92, 70, 255}, pearl{244, 240, 232, 255};
    float hover = sinf(t * 1.2f + c.u) * 2.4f, ph = fmodf(t * 0.8f + c.u * 0.13f, 1.0f);
    for (int s = -1; s <= 1; s += 2) // trailing veil-fins from the hips
        for (int i = 0; i < 3; i++) {
            float sw = sinf(t * 1.6f + i + s) * 5;
            Tri(c, {s * 6.0f, -60 + hover}, {s * (20.0f + i * 6) + sw, -28 + i * 4 + hover}, {s * 4.0f, -24 + i * 6 + hover}, Fade(fin, 0.55f));
        }
    FishTail(c, 0, -60 + hover, 14, 40, 11, scale, fin, Tone(skin, -0.1f), 13, true);
    for (int i = 0; i < 8; i++) { float a = i * 2.4f; Dot(c, -6 + cosf(a) * 8, -48 + i * 5 + hover, 1.0f, Fade(glow, 0.45f)); }                     // pale freckles of light down the tail
    Limb(c, {0, -60 + hover}, {-3, -104 + hover}, 14, 17, skin);                                                                                   // a slender torso
    Crescent(c, -1, -84 + hover, 15);
    for (int i = 0; i < 4; i++) Line(c, {-9, -98.0f + i * 4 + hover}, {6, -96.0f + i * 4 + hover}, 0.9f, Fade(INK, 0.45f));                        // showing ribs
    Quad(c, {-14, -106 + hover}, {12, -106 + hover}, {16, -64 + hover}, {-16, -62 + hover}, Fade(Color{60, 84, 78, 255}, 0.92f));               // a shroud of net and weed
    Jag(c, {-16, -62 + hover}, {16, -64 + hover}, 6, 9, Fade(Color{60, 84, 78, 255}, 0.9f), c.u);
    for (int i = 0; i < 5; i++) { Line(c, {-12.0f + i * 6, -104 + hover}, {-14.0f + i * 6, -66 + hover}, 0.6f, Fade(bone, 0.5f)); Line(c, {-14, -100.0f + i * 8 + hover}, {14, -96.0f + i * 8 + hover}, 0.6f, Fade(bone, 0.5f)); } // its mesh
    for (int s = -1; s <= 1; s += 2) { // a bodice of two shells
        for (int i = -2; i <= 2; i++) Tri(c, {s * 5.0f - 2 + i, -100 + hover}, {s * 5.0f + 2 + i, -100 + hover}, {s * 5.0f + i * 2.4f, -108 + std::abs(i) + hover}, Tone(Color{232, 200, 190, 255}, -0.1f * std::abs(i)));
        Ball(c, s * 5.0f, -98 + hover, 4.2f, Color{236, 206, 196, 255});
    }
    for (int i = 0; i < 9; i++) { float u = i / 8.0f; Ball(c, -12 + u * 24, -108 + sinf(u * PI) * 7 + hover, 2.2f, pearl); }                          // a pearl necklace
    Limb2(c, {-8, -102 + hover}, {-28, -102 + hover}, {-44, -118 + sinf(t * 1.4f) * 3 + hover}, 7, 5.6f, 4, skin);                                 // the arms rise, beckoning
    Limb2(c, {6, -102 + hover}, {20, -110 + hover}, {30, -128 + sinf(t * 1.4f + 1) * 3 + hover}, 6.4f, 5.2f, 3.6f, Tone(skin, -0.18f));
    for (int i = 0; i < 5; i++) { Line(c, {-44, -118 + hover}, {-58.0f - i * 1.5f, -128.0f + i * 5 + hover}, 1.5f, skin); Line(c, {-46.0f, -118 + hover}, {-58.0f - i * 1.5f, -128.0f + i * 5 + hover}, 0.6f, Fade(fin, 0.7f)); } // webbed fingers
    for (int i = 0; i < 4; i++) Line(c, {30, -128 + hover}, {38.0f + i * 1.4f, -138.0f + i * 5 + hover}, 1.4f, Tone(skin, -0.18f));
    for (int i = 0; i < 12; i++) { // long hair of kelp, drifting
        float x0 = -4.0f + (i - 6) * 1.6f;
        Vector2 prev{x0, -124 + hover};
        for (int sg = 1; sg <= 6; sg++) { Vector2 q{x0 + 6 + sinf(t * 1.3f + i * 0.5f + sg * 0.7f) * sg * 1.6f + sg * 1.5f, -124 + sg * 11.0f + hover}; Limb(c, prev, q, 2.6f - sg * 0.25f, 2.2f - sg * 0.25f, i % 3 ? hair : Tone(hair, 0.15f)); prev = q; }
    }
    Ball(c, -6, -118 + hover, 11, skin); Crescent(c, -6, -118 + hover, 11);                                                                         // the head
    for (int s = -1; s <= 1; s += 2) Tri(c, {-6 + s * 9.0f, -122 + hover}, {-6 + s * 9.0f, -114 + hover}, {-6 + s * 20.0f, -120 + hover}, Fade(fin, 0.8f)); // fin ears
    for (int i = -2; i <= 2; i++) Tri(c, {-6.0f + i * 4 - 3, -127 + hover}, {-6.0f + i * 4 + 3, -127 + hover}, {-6.0f + i * 4, -138 + std::abs(i) * 2 + hover}, i % 2 ? Color{206, 132, 126, 255} : bone); // a crown of coral and bone
    Bar(c, -18, -122 + hover, 24, 6, bone);                                                                                                         // a bandage over the eyes
    for (int i = 0; i < 4; i++) Line(c, {-16.0f + i * 6, -122 + hover}, {-13.0f + i * 6, -116 + hover}, 0.9f, Fade(INK, 0.5f));
    Ball(c, -12, -110 + hover, 2.6f, Tone(skin, -0.5f)); Bar(c, -13, -111.5f + hover, 4, 4.4f, INK);                                                // an open mouth, mid-song
    for (int i = 0; i < 3; i++) { // the song, drawn as rings of sound leaving her mouth
        float p2 = fmodf(ph + i / 3.0f, 1.0f);
        DrawRing(c.P(-12, -110 + hover), (8 + p2 * 44) * c.k, (9.4f + p2 * 44) * c.k, 150, 210, 18, Fade(glow, 0.5f * (1 - p2)));
    }
    Glow(c.P(-12, -110 + hover), 30 * c.k, Fade(glow, 0.08f));
}

void Neptune(const Ctx& c) { // the Weeds' level boss: a king of the drowned with a coiled tail and a trident like a mast
    const float t = c.t;
    const Color skin{96, 140, 120, 255}, scale{62, 98, 84, 255}, fin{140, 184, 156, 255}, coral{190, 104, 90, 255}, belly{170, 194, 172, 255}, gold{178, 140, 70, 255}, bone{214, 204, 180, 255}, glow{150, 230, 200, 255}, kelp{52, 108, 72, 255}, pearl{244, 240, 232, 255};
    float pulse = 0.5f + 0.5f * sinf(t * 2.0f + c.u), hover = sinf(t * 1.1f + c.u) * 2.5f;
    // the tail: a great coil behind and to the right, ending in a fluke that flicks
    Vector2 tp[10] = {{6, -92}, {40, -84}, {78, -64}, {112, -38}, {140, -20}, {166, -24}, {180, -46}, {170, -68}, {150, -74}, {136, -62}};
    for (int i = 1; i < 10; i++) {
        float w0 = 32 - (i - 1) * 2.6f, w1 = 32 - i * 2.6f;
        Limb(c, tp[i - 1], tp[i], w0, w1, i % 2 ? scale : Tone(scale, -0.12f));
        DrawRing(c.P(tp[i].x, tp[i].y), std::max(1.0f, w1 * 0.45f) * c.k, (w1 * 0.5f + 1) * c.k, 190, 350, 10, Tone(scale, -0.45f));
        Tri(c, {tp[i].x, tp[i].y - w1 * 0.5f}, {tp[i].x + 10, tp[i].y - w1 * 0.5f - 2}, {tp[i].x + 4, tp[i].y - w1 * 0.5f - 22 + (i % 2) * 4}, fin);   // dorsal spines
    }
    Limb(c, {6, -84}, {138, -14}, 8, 3, Tone(belly, -0.1f));
    for (int s = -1; s <= 1; s += 2) Tri(c, {136, -62}, {122 + s * 28.0f, -78 + sinf(t * 2.2f + s) * 6}, {138 + s * 6.0f, -50}, s < 0 ? Tone(fin, -0.25f) : fin);    // the fluke
    for (int i = 0; i < 12; i++) Dot(c, 24.0f + i * 12, -78.0f + i * 5 + sinf(i) * 6, 1.6f, Fade(glow, 0.35f + 0.5f * sinf(t * 2 + i)));                              // glow-spots
    // the kelp cloak behind him
    for (int i = 0; i < 9; i++) { float x = 14.0f + i * 7, sw = sinf(t * 1.3f + i) * 6; Tri(c, {x - 6, -178 + hover}, {x + 6, -178 + hover}, {x + 14 + sw, -60 - i * 4.0f}, i % 2 ? kelp : Tone(kelp, -0.3f)); }
    Limb(c, {2, -90 + hover}, {-4, -170 + hover}, 60, 78, skin);                                                                                     // a colossal torso
    Crescent(c, -2, -130 + hover, 62);
    for (int r = 0; r < 4; r++) for (int i = 0; i < 5 - (r % 2); i++) { // a breastplate of overlapping coral scales
        float x = -36.0f + i * 15 + (r % 2) * 7.5f, y = -160.0f + r * 15 + hover;
        Ball(c, x, y, 9, r % 2 ? coral : Tone(coral, 0.1f)); DrawRing(c.P(x, y), 6 * c.k, 9 * c.k, 200, 340, 8, Tone(coral, -0.45f));
    }
    for (int i = 0; i < 4; i++) { Line(c, {-30, -168.0f + i * 14 + hover}, {24, -166.0f + i * 14 + hover}, 1.4f, Fade(glow, 0.45f + 0.35f * pulse)); } // glowing seams between the scales
    Glow(c.P(-2, -140 + hover), 70 * c.k, Fade(glow, 0.06f + 0.05f * pulse));
    Limb(c, {-30, -92 + hover}, {28, -92 + hover}, 9, 9, gold); for (int i = 0; i < 5; i++) Rivet(c, -22.0f + i * 11, -92 + hover, 2.6f, Tone(gold, 0.2f));      // a belt of gold
    Ball(c, 0, -92 + hover, 9, gold); Ball(c, 0, -92 + hover, 5, Tone(glow, -0.2f));
    for (int i = 0; i < 8; i++) { float u = i / 7.0f; Ball(c, -30 + u * 58, -186 + sinf(u * PI) * 12 + hover, 5.4f, pearl); }                          // a pearl collar
    Ball(c, -58, -184 + hover, 27, coral); Crescent(c, -58, -184 + hover, 27); Barnacles(c, -60, -196 + hover, 22, 7, c.u);                          // barnacled pauldrons
    for (int i = 0; i < 4; i++) Tri(c, {-72.0f + i * 10, -204 + hover}, {-64.0f + i * 10, -204 + hover}, {-68.0f + i * 10, -228 + std::abs(i - 1.5f) * 5 + hover}, bone);
    Ball(c, 50, -184 + hover, 23, Tone(coral, -0.1f)); Barnacles(c, 50, -194 + hover, 16, 5, c.u + 3);
    for (int i = 0; i < 3; i++) Tri(c, {40.0f + i * 9, -202 + hover}, {48.0f + i * 9, -202 + hover}, {44.0f + i * 9, -220 + i * 2 + hover}, Tone(bone, -0.1f));
    // the trident, held upright in front
    Limb2(c, {-54, -178 + hover}, {-84, -150 + hover}, {-92, -122 + hover}, 20, 17, 14, skin);                                                       // the trident arm
    Limb2(c, {-84, -150 + hover}, {-92, -122 + hover}, {-92, -116 + hover}, 15, 14, 13, Tone(gold, -0.1f));                                          // a gold bracer
    Line(c, {-94, -6}, {-94, -232 + hover}, 6, Color{112, 92, 70, 255});
    for (int i = 0; i < 6; i++) Line(c, {-97, -20.0f - i * 34}, {-91, -14.0f - i * 34}, 3.4f, Tone(gold, -0.3f));                                    // its bindings
    Line(c, {-116, -236 + hover}, {-72, -236 + hover}, 6, gold);
    for (int i = -1; i <= 1; i++) { // the three tines, barbed
        float x = -94 + i * 22.0f;
        Line(c, {x, -236 + hover}, {x, -264 + hover + std::abs(i) * 4.0f}, 5, Tone(bone, -0.05f));
        Tri(c, {x - 6, -258 + hover + std::abs(i) * 4.0f}, {x + 6, -258 + hover + std::abs(i) * 4.0f}, {x, -280 + hover + std::abs(i) * 4.0f}, glow);
        Tri(c, {x, -244 + hover}, {x + 9, -236 + hover}, {x, -238 + hover}, bone);
    }
    Glow(c.P(-94, -262 + hover), 50 * c.k, Fade(glow, 0.14f + 0.1f * pulse));
    Limb2(c, {50, -178 + hover}, {76, -170 + hover}, {84, -198 + hover}, 18, 15, 12, Tone(skin, -0.14f));                                            // the raised arm, cradling a storm
    Ball(c, 88, -212 + hover, 16, Tone(glow, -0.25f)); Glow(c.P(88, -212 + hover), 70 * c.k, Fade(glow, 0.22f + 0.14f * pulse));
    for (int i = 0; i < 4; i++) { float a = t * 2.4f + i * 1.57f; Line(c, {88, -212 + hover}, {88 + cosf(a) * 26, -212 + sinf(a) * 26 + hover}, 1.2f, Fade(WHITE, 0.6f)); } // lightning about the orb
    // the head: a heavy brow, a beard of writhing weed, a crown of shells
    Ball(c, -6, -206 + hover, 24, skin); Crescent(c, -6, -206 + hover, 24);
    for (int s = -1; s <= 1; s += 2) for (int i = 0; i < 3; i++) Tri(c, {-6 + s * 22.0f, -212.0f + i * 5 + hover}, {-6 + s * 22.0f, -206.0f + i * 5 + hover}, {-6 + s * (36.0f - i * 4), -210.0f + i * 7 + hover}, Fade(fin, 0.85f)); // fin ears
    for (int i = 0; i < 9; i++) { // the beard
        float x0 = -22.0f + i * 5.5f;
        Vector2 prev{x0, -192 + hover};
        for (int sg = 1; sg <= 5; sg++) { Vector2 q{x0 - 3 + sinf(t * 1.6f + i + sg * 0.8f) * sg * 1.8f, -192 + sg * 9.0f + hover}; Limb(c, prev, q, 5.0f - sg * 0.7f, 4.4f - sg * 0.7f, i % 2 ? kelp : Tone(kelp, 0.15f)); prev = q; }
    }
    Ball(c, -12, -198 + hover, 7, Tone(skin, -0.25f));                                                                                              // the nose
    Bar(c, -32, -216 + hover, 52, 11, INK); Bar(c, -28, -213.5f + hover, 10, 3, INK); Dot(c, -22, -211.6f + hover, 1.6f, glow); Dot(c, 8, -211.6f + hover, 1.6f, glow); // eyes under a black brow
    for (int i = 0; i < 4; i++) Line(c, {-22.0f + i * 3, -226 + hover}, {-20.0f + i * 3, -220 + hover}, 1.2f, Fade(INK, 0.6f));                     // brow scars
    for (int i = -3; i <= 3; i++) { // a crown of shells and bone
        float h = 26 + (3 - std::abs(i)) * 7;
        Tri(c, {-6.0f + i * 8 - 4.5f, -222 + hover}, {-6.0f + i * 8 + 4.5f, -222 + hover}, {-6.0f + i * 9.4f, -222 - h + hover}, i % 2 ? Color{232, 214, 196, 255} : bone);
        if (i % 2 == 0) Ball(c, -6.0f + i * 9.4f, -222 - h + hover, 3.2f, pearl);
    }
    Line(c, {-30, -222 + hover}, {18, -222 + hover}, 5, gold);
    Ball(c, -6, -226 + hover, 6, Tone(glow, -0.2f)); Glow(c.P(-6, -226 + hover), 30 * c.k, Fade(glow, 0.18f + 0.1f * pulse));
}
// ============================================================ THE WEEDS: the beasts
void GiantOctopus(const Ctx& c) {
    const float t = c.t;
    const Color skin{108, 74, 132, 255}, lt{156, 124, 178, 255}, dk{62, 44, 82, 255}, pale{218, 200, 214, 255}, amber{240, 200, 90, 255}, glow{230, 120, 220, 255}, rust{130, 84, 50, 255};
    float breathe = sinf(t * 1.8f + c.u) * 1.6f, pulse = 0.5f + 0.5f * sinf(t * 2.4f + c.u);
    // eight arms, each a sinuous, tapering curve from under the head to the ground or up to reach for the party
    struct Arm { float bx, ex, ey, arch; };
    const Arm arms[8] = {{-20, -92, -34, 46}, {-16, -84, 0, 14}, {-10, -62, 0, 10}, {-4, -34, 0, 8}, {4, -4, 0, 10}, {12, 24, 0, 8}, {18, 52, -4, 16}, {22, 76, -26, 34}};
    for (int pass = 0; pass < 2; pass++)
        for (int i = pass; i < 8; i += 2) {
            const Arm& a = arms[i];
            Vector2 prev{a.bx, -42.0f};
            for (int sg = 1; sg <= 9; sg++) {
                float u = sg / 9.0f, wob = sinf(t * 1.8f + i * 1.3f + sg * 0.6f + c.u) * 4 * u;
                Vector2 q{a.bx + (a.ex - a.bx) * u + wob, -42.0f + (a.ey + 42.0f) * u - sinf(u * PI) * a.arch};
                float w0 = 9.5f * (1 - (sg - 1) / 10.0f), w1 = 9.5f * (1 - sg / 10.0f);
                Limb(c, prev, q, w0, w1, pass ? skin : Tone(skin, -0.28f));
                if (sg % 2 == 0) { Dot(c, q.x + 1, q.y + w1 * 0.35f, std::max(1.0f, w1 * 0.32f), pale); Dot(c, q.x + 1, q.y + w1 * 0.35f, std::max(0.6f, w1 * 0.14f), dk); }  // suckers
                if (sg == 9 && i == 0) { Tri(c, {q.x, q.y - 2}, {q.x, q.y + 2}, {q.x - 8, q.y + sinf(t * 3) * 3}, Tone(skin, 0.1f)); }
                prev = q;
            }
        }
    // an old anchor chain wound round the first arm, and a rusted hook
    for (int i = 0; i < 6; i++) DrawRing(c.P(-30.0f - i * 8, -46.0f + i * 1.5f), 2.6f * c.k, 3.8f * c.k, 0, 360, 8, Color{78, 78, 74, 255});
    Tri(c, {-78, -34}, {-72, -30}, {-76, -22}, rust);
    Ball(c, 26, -80 + breathe, 34, skin); Ball(c, 14, -92 + breathe, 22, lt); Crescent(c, 26, -80 + breathe, 34);                                    // the great mantle
    for (int i = 0; i < 9; i++) { float a = i * 2.2f; Dot(c, 26 + cosf(a) * (8 + i * 2.2f), -80 + sinf(a) * (8 + i * 2.0f) + breathe, 1.8f + (i % 3) * 0.6f, Fade(glow, 0.3f + 0.55f * (0.5f + 0.5f * sinf(t * 2 + i)))); } // chromatophores flickering
    for (int i = 0; i < 4; i++) Barnacles(c, 40.0f + i * 2, -98.0f + i * 10 + breathe, 4, 2, c.u + i);
    Line(c, {36, -100 + breathe}, {50, -82 + breathe}, 1.4f, Fade(INK, 0.6f));                                                                        // old scars
    Ball(c, -6, -56, 25, skin); Crescent(c, -6, -56, 25);                                                                                            // the head
    for (int s = -1; s <= 1; s += 2) { // eyes with a horizontal pupil, under a heavy brow
        Vector2 e{-6.0f + s * 12.0f, -62.0f};
        Ball(c, e.x, e.y, 6.6f, Tone(amber, -0.15f)); Bar(c, e.x - 4.2f, e.y - 1.0f, 8.4f, 2.4f, INK); Dot(c, e.x - 2.0f, e.y - 2.4f, 1.0f, WHITE);
        Tri(c, {e.x - 8, e.y - 5}, {e.x + 8, e.y - 5}, {e.x + s * 2.0f, e.y - 11}, Tone(skin, -0.35f));
    }
    Tri(c, {-12, -40}, {0, -40}, {-6, -30}, Color{200, 184, 150, 255});                                                                              // a parrot's beak between the arms
    Glow(c.P(-6, -62), 44 * c.k, Fade(amber, 0.05f + 0.03f * pulse));
    for (int i = 0; i < 3; i++) { float ph = fmodf(t * 0.5f + i / 3.0f + c.u * 0.1f, 1.0f); DrawCircleV(c.P(52 + sinf(ph * 6) * 6, -70 - ph * 40), (2.4f + ph * 5) * c.k, Fade(Color{40, 26, 60, 255}, 0.4f * (1 - ph))); } // wisps of ink
}

void ElectricEel(const Ctx& c) { // the Weeds' mini-boss: a long, rearing serpent crackling with current
    const float t = c.t;
    const Color skin{72, 100, 92, 255}, dk{40, 60, 58, 255}, belly{158, 178, 158, 255}, elec{120, 204, 255, 255}, bone{224, 214, 190, 255}, amber{255, 210, 100, 255};
    float pulse = 0.5f + 0.5f * sinf(t * 5 + c.u);
    const int N = 22;
    Vector2 pts[N + 1];
    for (int i = 0; i <= N; i++) { // from the coiled tail on the ground, up in an S, to the head held aloft toward the party
        float u = (float)i / N;
        float rise = std::clamp((u - 0.5f) / 0.5f, 0.0f, 1.0f), coil = 1.0f - std::clamp((u - 0.5f) / 0.25f, 0.0f, 1.0f);
        pts[i] = {100.0f - u * 178 - rise * rise * 8 + sinf(t * 1.5f + u * 5 + c.u) * 4 * rise, -(26.0f * powf(sinf(u * PI * 2.3f), 2.0f) * coil + 104.0f * powf(rise, 1.35f)) - 5};
    }
    for (int i = N; i >= 1; i--) {
        float u = (float)i / N, u1 = (float)(i - 1) / N;
        auto wf = [](float q) { return 7.0f + 15.0f * sinf(PI * std::clamp(q * 0.9f + 0.05f, 0.0f, 1.0f)); };
        float w0 = wf(u), w1 = wf(u1);
        Limb(c, pts[i], pts[i - 1], w0 * 0.85f, w1 * 0.85f, i % 2 ? skin : Tone(skin, -0.1f));
        Limb(c, {pts[i].x, pts[i].y + w0 * 0.22f}, {pts[i - 1].x, pts[i - 1].y + w1 * 0.22f}, w0 * 0.34f, w1 * 0.34f, belly);                              // a pale belly
        Tri(c, {pts[i].x, pts[i].y - w0 * 0.44f}, {pts[i - 1].x, pts[i - 1].y - w1 * 0.44f}, {(pts[i].x + pts[i - 1].x) / 2, (pts[i].y + pts[i - 1].y) / 2 - w0 * 0.44f - 7}, i % 2 ? dk : Tone(dk, 0.12f)); // the dorsal frill
        if (i % 3 == 0) DrawRing(c.P(pts[i].x, pts[i].y), w0 * 0.3f * c.k, w0 * 0.45f * c.k, 200, 340, 8, Fade(INK, 0.45f));
    }
    for (int i = 2; i < N - 1; i += 2) { // veins and spines that crackle
        float ph = 0.5f + 0.5f * sinf(t * 7 + i * 1.7f + c.u);
        Dot(c, pts[i].x, pts[i].y - 4, 1.6f, Fade(elec, 0.4f + 0.55f * ph));
        if (ph > 0.75f) { Vector2 a = pts[i], b{a.x + sinf(i * 3.1f + t * 9) * 16, a.y - 16 - ph * 10};
            Line(c, {a.x, a.y - 4}, {(a.x + b.x) / 2 + 4, (a.y + b.y) / 2}, 1.3f, Fade(elec, 0.9f)); Line(c, {(a.x + b.x) / 2 + 4, (a.y + b.y) / 2}, b, 1.3f, Fade(WHITE, 0.9f)); }
    }
    for (int i = 6; i < 16; i += 3) { Line(c, {pts[i].x, pts[i].y + 6}, {pts[i + 1].x - 2, pts[i + 1].y + 4}, 0.9f, Fade(INK, 0.6f)); }                   // scars
    Vector2 h = pts[0];
    for (int i = 0; i < 5; i++) Tri(c, {pts[2].x + i * 2.0f - 4, pts[2].y - 10 + i * 5}, {pts[2].x + i * 2.0f - 4, pts[2].y - 6 + i * 5}, {pts[2].x - 14 + i * 2.0f, pts[2].y - 8 + i * 5.6f}, Tone(skin, 0.12f)); // gill plates
    Ball(c, h.x - 2, h.y, 19, Tone(skin, 0.05f)); Ball(c, h.x - 6, h.y - 5, 11, Tone(skin, 0.2f)); Crescent(c, h.x - 2, h.y, 19);
    Limb(c, {h.x - 8, h.y + 2}, {h.x - 40, h.y + 8}, 15, 7, skin);                                                                                    // the snout...
    float jaw = 7 + sinf(t * 3 + c.u) * 3;
    Limb(c, {h.x - 6, h.y + 9}, {h.x - 38, h.y + 9 + jaw}, 9, 4, Tone(skin, -0.2f));                                                               // ...and the open lower jaw
    for (int i = 0; i < 6; i++) { Tri(c, {h.x - 14.0f - i * 3.6f, h.y + 6}, {h.x - 11.0f - i * 3.6f, h.y + 6}, {h.x - 12.5f - i * 3.6f, h.y + 12}, bone); Tri(c, {h.x - 13.0f - i * 3.4f, h.y + 10 + jaw * 0.8f}, {h.x - 10.0f - i * 3.4f, h.y + 10 + jaw * 0.8f}, {h.x - 11.5f - i * 3.4f, h.y + 4 + jaw * 0.8f}, bone); } // needle teeth
    Bar(c, h.x - 12, h.y - 6, 16, 5, INK); Dot(c, h.x - 8, h.y - 3.5f, 1.3f, amber); Dot(c, h.x - 1, h.y - 3.5f, 1.3f, amber);                          // eyes buried under the brow
    for (int i = 0; i < 2; i++) Line(c, {h.x - 30, h.y + 12 + i * 4}, {h.x - 44 + sinf(t * 2 + i) * 4, h.y + 22 + i * 8}, 1.0f, Tone(skin, -0.1f));  // barbels
    Glow(c.P(h.x - 6, h.y), 60 * c.k, Fade(elec, 0.10f + 0.10f * pulse));
    for (int i = 0; i < 3; i++) { Vector2 a{h.x - 34.0f + i * 6, h.y - 4}; Line(c, a, {a.x - 10 + sinf(t * 11 + i) * 6, a.y - 12}, 1.3f, Fade(elec, 0.85f)); Line(c, {a.x - 10 + sinf(t * 11 + i) * 6, a.y - 12}, {a.x - 4 + cosf(t * 9 + i) * 8, a.y - 22}, 1.3f, Fade(WHITE, 0.85f)); }
}

void GreatWhite(const Ctx& c) { // the Weeds' other mini-boss: a shark hauled up out of the water and furious about it
    const float t = c.t;
    const Color top{104, 122, 130, 255}, mid{132, 150, 156, 255}, belly{214, 214, 204, 255}, dk{62, 76, 84, 255}, bone{236, 230, 212, 255}, blood{150, 40, 40, 255}, rust{140, 84, 48, 255};
    float breathe = sinf(t * 2.0f + c.u) * 1.4f;
    // tail, rising behind
    Limb2(c, {80, -46}, {112, -58}, {132, -86}, 30, 18, 8, top);
    Tri(c, {126, -78}, {150, -118 + sinf(t * 2 + c.u) * 4}, {132, -88}, Tone(top, 0.05f)); Tri(c, {130, -84}, {156, -74}, {134, -60}, Tone(top, -0.18f)); // the caudal fin
    Ball(c, 108, -60, 4, INK);
    // pectoral fins as forelimbs, propping the body on the ground
    Limb2(c, {-10, -32}, {-24, -20}, {-38, -4}, 10, 8, 5, Tone(top, -0.2f));
    Limb2(c, {20, -34}, {32, -22}, {44, -4}, 12, 9, 5, Tone(top, -0.32f));
    // the body: a torpedo along a curved spine, countershaded dark above and pale beneath
    const int SN = 10;
    Vector2 sp[SN + 1];
    for (int i = 0; i <= SN; i++) { float u = (float)i / SN; sp[i] = {-30 + u * 128, -50 + breathe * (1 - u) - u * u * 20}; }
    for (int i = 1; i <= SN; i++) {
        float u0 = (float)(i - 1) / SN, u1 = (float)i / SN, w0 = 46.0f * (1 - u0 * 0.8f), w1 = 46.0f * (1 - u1 * 0.8f);
        Limb(c, sp[i - 1], sp[i], w0, w1, i % 2 ? top : Tone(top, -0.06f));
        if (u1 < 0.9f) Limb(c, {sp[i - 1].x, sp[i - 1].y + w0 * 0.3f}, {sp[i].x, sp[i].y + w1 * 0.3f}, w0 * 0.4f, w1 * 0.4f, belly);
        Limb(c, {sp[i - 1].x, sp[i - 1].y - w0 * 0.3f}, {sp[i].x, sp[i].y - w1 * 0.3f}, w0 * 0.3f, w1 * 0.3f, mid);
    }
    Crescent(c, 10, -46, 26);    for (int i = 0; i < 4; i++) Line(c, {-6.0f + i * 5, -34 + breathe}, {-8.0f + i * 5, -52 + breathe}, 1.4f, Fade(INK, 0.6f));                             // gill slits
    for (int i = 0; i < 6; i++) Dot(c, -30.0f + i * 5, -46.0f + breathe + (i % 2) * 2, 0.9f, Fade(INK, 0.5f));                                          // pores
    Tri(c, {14, -76}, {56, -76}, {36, -128 + breathe}, Tone(top, 0.02f)); Tri(c, {14, -76}, {36, -76}, {36, -128 + breathe}, Tone(top, -0.2f));         // the dorsal fin, torn at the edge
    Tri(c, {50, -110}, {56, -106}, {52, -96}, INK);
    Line(c, {60, -68}, {88, -54}, 1.6f, Fade(INK, 0.65f)); Line(c, {70, -72}, {94, -58}, 1.2f, Fade(INK, 0.5f)); Line(c, {-6, -66}, {10, -56}, 1.4f, Color{200, 190, 176, 255}); // scars
    Barnacles(c, 30, -74, 12, 4, c.u); Barnacles(c, 64, -62, 8, 3, c.u + 4);
    Line(c, {40, -74}, {62, -110}, 2.4f, Color{100, 72, 44, 255}); Tri(c, {36, -72}, {44, -78}, {40, -66}, rust);                                        // a rusted harpoon still in its back
    for (int i = 0; i < 5; i++) Line(c, {42.0f + i * 4, -86.0f - i * 5}, {44.0f + i * 4, -80.0f - i * 5}, 1.0f, Fade(bone, 0.7f));
    Line(c, {-30, -26}, {-14, -22}, 1.0f, Fade(Color{190, 176, 140, 255}, 0.8f)); Line(c, {-24, -34}, {-8, -14}, 1.0f, Fade(Color{190, 176, 140, 255}, 0.8f)); // torn net
    // the head: a blunt snout and a jaw that gapes and snaps
    Ball(c, -44, -52, 24, top); Ball(c, -50, -58, 14, mid); Crescent(c, -44, -52, 24);
    Limb(c, {-56, -50}, {-90, -46}, 22, 8, top);                                                                                                        // the snout
    float open = 0.24f + 0.2f * (0.5f + 0.5f * sinf(t * 2.6f + c.u));
    Vector2 hinge{-40, -38};
    Vector2 jawTip{hinge.x - 52.0f * cosf(open), hinge.y + 52.0f * sinf(open) + 4};
    Tri(c, {hinge.x - 4, hinge.y - 4}, {-88, -42}, {jawTip.x + 6, jawTip.y - 2}, Fade(blood, 0.95f));                                                   // the raw red of the throat
    Limb(c, hinge, jawTip, 14, 5, belly);                                                                                                               // the lower jaw
    for (int i = 0; i < 9; i++) { // rows of serrated teeth, top and bottom
        float u = (i + 0.6f) / 9.4f;
        Tri(c, {-52.0f - u * 34 - 2.6f, -43}, {-52.0f - u * 34 + 2.6f, -43}, {-52.0f - u * 34, -34 + u * 2}, bone);
        Vector2 jp{hinge.x + (jawTip.x - hinge.x) * u, hinge.y + (jawTip.y - hinge.y) * u - 4};
        Tri(c, {jp.x - 2.6f, jp.y}, {jp.x + 2.6f, jp.y}, {jp.x, jp.y - 8}, bone);
    }    Ball(c, -54, -60, 4.2f, INK); Dot(c, -55.4f, -61.4f, 1.0f, Color{230, 230, 220, 255});                                                            // a black, unblinking eye
    Line(c, {-62, -62}, {-50, -56}, 1.4f, Fade(INK, 0.65f));
    DrawLineEx(c.P(-84, -34), c.P(-82, -22 + sinf(t * 2 + c.u) * 2), std::max(1.0f, 0.9f * c.k), Fade(Color{170, 170, 176, 255}, 0.9f));                  // a fishhook and line in the jaw
    DrawCircleLines((int)c.P(-82, -20 + sinf(t * 2 + c.u) * 2).x, (int)c.P(-82, -20 + sinf(t * 2 + c.u) * 2).y, 4 * c.k, Color{170, 170, 176, 255});
    Limb(c, {-30, -12}, {-40, -2}, 2.6f, 1.6f, Fade(blood, 0.8f));                                                                                    // blood dripping
}
}  // namespace

// Returns true if the enemy has a rich drawing (the rest still use the older archetype drawers).
bool DrawRichEnemy(const Enemy& e, Rectangle r, float t) {
    float H = 0, W = 0;
    void (*fn)(const Ctx&) = nullptr;
    switch (e.type) {
        case EnemyType::SeaLouse: fn = SeaLouse; H = 46; W = 108; break;
        case EnemyType::CaveShrimp: fn = PistolShrimp; H = 80; W = 128; break;
        case EnemyType::DysCrustacean: fn = DysCrustacean; H = 82; W = 124; break;
        case EnemyType::Lobster: fn = Lobster; H = 132; W = 235; break;
        case EnemyType::CrustaceanQueen: fn = CrustaceanQueen; H = 224; W = 380; break;
        case EnemyType::TribalSpearman: fn = TribalSpearman; H = 132; W = 130; break;
        case EnemyType::WarDog: fn = WarDog; H = 78; W = 140; break;
        case EnemyType::TribalShaman: fn = TribalShaman; H = 138; W = 110; break;
        case EnemyType::TribalDemigod: fn = TribalDemigod; H = 205; W = 170; break;
        case EnemyType::FeralMerman: fn = FeralMerman; H = 116; W = 110; break;
        case EnemyType::GiantOctopus: fn = GiantOctopus; H = 108; W = 150; break;
        case EnemyType::ElectricEel: fn = ElectricEel; H = 132; W = 240; break;
        case EnemyType::GreatWhite: fn = GreatWhite; H = 128; W = 250; break;
        case EnemyType::Siren: fn = Siren; H = 120; W = 100; break;
        case EnemyType::Neptune: fn = Neptune; H = 284; W = 380; break;
        default: return false;
    }
    float k = std::min(r.height / H, 1.5f * r.width / W);
    Ctx c{r.x + r.width / 2, r.y + r.height, k, t, e.uid};
    fn(c);
    return true;
}
