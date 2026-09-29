// The parkour creatures redrawn as shaded pixel sprites (pixelart.h): each is modelled from volumes on a canvas in
// art pixels (two world pixels each), lit and inked by px::Render. Called from the per-biome draw switches in
// platformer.cpp. Coordinates in here are world pixels unless they're named as canvas ones.
#include "beastart.h"
#include "pixelart.h"
#include <algorithm>
#include <cmath>

namespace {
px::Canvas gCv;
constexpr float CELL = 2;       // world pixels per art pixel
constexpr float PIf = 3.14159265f;
Vector2 V(float x, float y) { return {x, y}; }
Vector2 Add(Vector2 a, Vector2 b) { return {a.x + b.x, a.y + b.y}; }
Vector2 Sub(Vector2 a, Vector2 b) { return {a.x - b.x, a.y - b.y}; }
Vector2 Mul(Vector2 a, float k) { return {a.x * k, a.y * k}; }
float Len(Vector2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
Vector2 Norm(Vector2 a) { float l = Len(a); return l > 1e-4f ? Vector2{a.x / l, a.y / l} : Vector2{1, 0}; }
Vector2 Lerp(Vector2 a, Vector2 b, float u) { return {a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u}; }
Vector2 Bez(Vector2 a, Vector2 b, Vector2 c, Vector2 d, float u) { float v = 1 - u; return Add(Add(Mul(a, v * v * v), Mul(b, 3 * v * v * u)), Add(Mul(c, 3 * v * u * u), Mul(d, u * u * u))); }
Color Dim(Color c, bool dead) { return dead ? Color{(unsigned char)((c.r + 90) / 2), (unsigned char)((c.g + 86) / 2), (unsigned char)((c.b + 96) / 2), 255} : c; }

// a canvas covering a world rectangle; C() turns world coordinates into canvas ones
struct WorldCanvas {
    Vector2 origin; int w, h;
    WorldCanvas(float x0, float y0, float x1, float y1) {
        origin = {floorf(x0 / CELL) * CELL, floorf(y0 / CELL) * CELL};
        w = std::clamp((int)ceilf((x1 - origin.x) / CELL) + 1, 1, 400); h = std::clamp((int)ceilf((y1 - origin.y) / CELL) + 1, 1, 300);
        gCv.Begin(w, h);
    }
    Vector2 C(Vector2 p) const { return {(p.x - origin.x) / CELL, (p.y - origin.y) / CELL}; }
    void Draw(const px::Mat* m, int n, float alpha = 1) const { px::Render(gCv, m, n, origin, CELL, false, alpha); }
};
} // namespace

// ---------------------------------------------------------------- the Timber-Shell Tortoise (the Pirate Ship)
// A proper tortoise: a high domed shell of plates, thick elephant legs stepping in diagonal pairs, a leathery neck and
// a hooked beak - with a sunken ship's cargo of planks and cannonballs heaped on its back and barnacles on the rim.
void PxTortoise(Vector2 c, float facing, float phase, bool moving, bool dead, float t, bool crystal) {
    enum { SHELL, RIM, SKIN, BEAK, EYE, IRON, WOOD, BARN, NAIL, GLOW };
    px::Mat M[10] = {
        crystal ? px::Ramp(Dim({90, 170, 190, 255}, dead), px::PAT_SCUTES, 1.0f) : px::Ramp(Dim({108, 90, 62, 255}, dead), px::PAT_SCUTES, 1.1f),
        px::Ramp(Dim(crystal ? Color{60, 80, 96, 255} : Color{80, 62, 44, 255}, dead)),
        px::Ramp(Dim(crystal ? Color{150, 150, 160, 255} : Color{130, 136, 100, 255}, dead), px::PAT_SCALES, 0.7f),
        px::Ramp(Dim({66, 58, 50, 255}, dead)), px::Ramp({18, 14, 12, 255}),
        px::Ramp(Dim({74, 76, 84, 255}, dead)), px::Ramp(Dim({142, 102, 62, 255}, dead), px::PAT_BANDS, 0.6f),
        px::Ramp(Dim({208, 202, 182, 255}, dead), px::PAT_SPECKLE), px::Ramp({222, 212, 180, 255}),
        px::Glow({150, 240, 255, 255})};
    const int W = 78, H = crystal ? 52 : 38;
    gCv.Begin(W, H);
    float legLen = crystal ? 20.0f : 10.0f; // the Cave's tortoise walks tall enough to pass under
    float cy = crystal ? 18.0f : 21.0f, cx = 36;
    float st = moving ? sinf(phase * 2) : 0;
    float fy = cy + 1 + legLen; // feet
    auto leg = [&](float x, float swing, float lift, float r, float z) {
        Vector2 hip{x, cy + 1}, foot{x + swing * 1.6f, fy - std::max(0.0f, lift) * 1.8f};
        gCv.Limb(hip, foot, r, r * 0.9f, SKIN, z);
        gCv.Ball(foot.x + 0.5f, foot.y, r + 0.6f, 1.9f, SKIN, z + 1);
        for (int k = 0; k < 3; k++) gCv.Dot((int)(foot.x + r - 1), (int)(foot.y - 1 + k), NAIL, z + 3); // blunt nails
    };
    leg(47, st, -st, 3.2f, 1); leg(24, -st, st, 3.2f, 1);     // the far pair
    gCv.Ball(cx, cy + 2, 22, 17, SHELL, 6, 16, cy + 1.5f);      // the dome, high and round
    gCv.Limb({14, cy + 1.5f}, {58, cy + 1.5f}, 2.5f, 2.5f, RIM, 9); // its rim
    float bob = sinf(t * 1.3f + phase) * 0.6f;
    gCv.Limb({15, cy + 2}, {9, cy + 4}, 1.8f, 0.6f, SKIN, 4);   // the tail
    gCv.Limb({56, cy - 1}, {63, cy - 4 + bob}, 3.6f, 2.9f, SKIN, 10); // the neck
    gCv.Ball(66, cy - 5 + bob, 5.2f, 4.0f, SKIN, 12);           // the head
    gCv.Ball(70, cy - 4 + bob, 2.8f, 2.4f, SKIN, 13);
    gCv.Tri({69, cy - 3 + bob}, {74, cy - 3 + bob}, {70, cy - 0.5f + bob}, BEAK, 15, 0.2f, 0.3f); // the hooked beak
    bool blink = fmodf(t * 0.4f + phase, 4.0f) < 0.12f;
    if (!blink && !dead) { gCv.Dot(67, (int)(cy - 7 + bob), EYE, 40); gCv.Dot(68, (int)(cy - 7 + bob), EYE, 40); }
    leg(49, -st, st, 3.8f, 16); leg(22, st, -st, 3.8f, 16);     // the near pair
    if (crystal) { // crystals grown out of the plates, lit from inside
        for (int k = 0; k < 5; k++) { float x = 24 + k * 6.0f, top = cy - 13 + fabsf(k - 2.0f) * 2.5f; gCv.Tri({x - 2.2f, top + 6}, {x + 2.2f, top + 6}, {x, top - 4 - (k % 2) * 3}, GLOW, 30); }
    } else { // the cargo of a sunken ship: planks and cannonballs
        gCv.Limb({24, cy - 11}, {41, cy - 14}, 1.3f, 1.3f, WOOD, 24);
        gCv.Limb({30, cy - 8}, {48, cy - 9}, 1.2f, 1.2f, WOOD, 23);
        gCv.Ball(28, cy - 12, 2.8f, 2.8f, IRON, 27); gCv.Ball(36, cy - 15, 2.6f, 2.6f, IRON, 28); gCv.Ball(45, cy - 11, 2.4f, 2.4f, IRON, 26);
        for (int k = 0; k < 5; k++) gCv.Ball(17 + k * 9.0f, cy - 1 - (k % 2) * 3, 1.6f, 1.3f, BARN, 20);
    }
    bool right = facing >= 0;
    Vector2 origin{c.x - (right ? cx : (W - 1 - cx)) * CELL, c.y - cy * CELL};
    px::Render(gCv, M, 10, origin, CELL, !right);
}

// ---------------------------------------------------------------- the Totem-Centipede (the Island)
// A giant centipede bursting out of the ground: a long, jointed body of armoured segments, a pair of legs on each
// rippling in a wave, long antennae, venom-tipped forcipules - and on its plates the tribe's totem paint, faces
// and bands in ochre, red and bone, the reason they call it what they do.
void PxCentipede(Vector2 a, Vector2 h, float t, float facing) {
    enum { cRED, cOCHRE, cDARK, cLEG, cBONE, cVENOM, cEYE, cDIRT };
    px::Mat M[8] = {px::Ramp({150, 54, 38, 255}), px::Ramp({196, 140, 58, 255}), px::Ramp({52, 32, 30, 255}), px::Ramp({196, 96, 40, 255}),
                    px::Ramp({230, 220, 190, 255}), px::Ramp({150, 40, 150, 255}), px::Glow({255, 220, 90, 255}), px::Ramp({110, 84, 56, 255}, px::PAT_SPECKLE)};
    WorldCanvas wc(std::min(a.x, h.x) - 60, std::min(a.y, h.y) - 60, std::max(a.x, h.x) + 60, std::max(a.y, h.y) + 40);
    Vector2 d = Sub(h, a), perp = Norm(Vector2{-d.y, d.x});
    if (perp.x * facing > 0) perp = Mul(perp, -1); // it bows away from the way it's striking
    const int N = 16;
    Vector2 pts[N + 1];
    for (int k = 0; k <= N; k++) {
        float u = k / (float)N;
        Vector2 q = Add(Lerp(a, h, u), Mul(perp, sinf(u * PIf) * Len(d) * 0.22f));
        q = Add(q, Mul(perp, sinf(u * 9 - t * 7) * 4 * (1 - u))); // a ripple running up it
        pts[k] = wc.C(q);
    }
    for (int k = 0; k <= N; k++) { // segments, tail first, each with its pair of legs
        float u = k / (float)N, r = 6.5f - 2.0f * u;
        Vector2 q = pts[k], dir = Norm(Sub(pts[std::min(N, k + 1)], pts[std::max(0, k - 1)])), side{-dir.y, dir.x};
        for (int s = -1; s <= 1; s += 2) {
            float wave = sinf(t * 16 - k * 0.9f + (s > 0 ? PIf : 0));
            Vector2 root = Add(q, Mul(side, s * r * 0.5f)), knee = Add(root, Add(Mul(side, s * 4.5f), Mul(dir, wave * 2.2f))), tip = Add(knee, Add(Mul(side, s * 3.5f), Mul(dir, -1.5f + wave * 1.5f)));
            float z = s > 0 ? 1.0f + u * 4 : 20 + u * 4;
            gCv.Limb(root, knee, 1.1f, 0.9f, cLEG, z); gCv.Limb(knee, tip, 0.9f, 0.4f, cLEG, z);
        }
        int m = (k / 2) % 3 == 0 ? cRED : (k / 2) % 3 == 1 ? cOCHRE : cDARK;
        gCv.Ball(q.x, q.y, r * 1.15f, r, m, 6 + u * 6, r);
        gCv.Limb(Add(q, Mul(dir, -r * 0.9f)), Add(q, Mul(dir, -r * 0.9f)), r * 0.35f, r * 0.35f, cDARK, 6 + u * 6 + r * 0.5f); // the joint
        if (k % 3 == 1) { // a carved totem face on the plate: two eyes, a mouth
            Vector2 up = Mul(side, -1);
            gCv.Dot((int)(q.x + dir.x * 1.5f + up.x * 1.5f), (int)(q.y + dir.y * 1.5f + up.y * 1.5f), cBONE, 60);
            gCv.Dot((int)(q.x - dir.x * 1.5f + up.x * 1.5f), (int)(q.y - dir.y * 1.5f + up.y * 1.5f), cBONE, 60);
            gCv.Dot((int)(q.x - up.x * 1.2f), (int)(q.y - up.y * 1.2f), cBONE, 60);
        }
    }
    Vector2 hd = pts[N], hdir = Norm(Sub(pts[N], pts[N - 2])), hs{-hdir.y, hdir.x};
    gCv.Ball(hd.x + hdir.x * 3, hd.y + hdir.y * 3, 7.5f, 6, cRED, 20, 7);                     // the head
    for (int s = -1; s <= 1; s += 2) { // the forcipules, and the antennae sweeping
        Vector2 j0 = Add(Add(hd, Mul(hdir, 7)), Mul(hs, s * 3.0f)), j1 = Add(Add(j0, Mul(hdir, 5)), Mul(hs, -s * 3.5f));
        gCv.Limb(j0, j1, 1.6f, 0.7f, cDARK, 26); gCv.Dot((int)j1.x, (int)j1.y, cVENOM, 30);
        float sw = sinf(t * 5 + s) * 0.4f;
        Vector2 an0 = Add(Add(hd, Mul(hdir, 5)), Mul(hs, s * 4.0f));
        Vector2 an1 = Add(an0, Add(Mul(hdir, 9), Mul(hs, s * (7 + sw * 6)))), an2 = Add(an1, Add(Mul(hdir, 8), Mul(hs, s * (5 - sw * 4))));
        gCv.Limb(an0, an1, 0.8f, 0.6f, cOCHRE, 24); gCv.Limb(an1, an2, 0.6f, 0.4f, cOCHRE, 24);
    }
    for (int s = -1; s <= 1; s += 2) gCv.Dot((int)(hd.x + hdir.x * 6 + hs.x * s * 2.5f), (int)(hd.y + hdir.y * 6 + hs.y * s * 2.5f), cEYE, 40); // a cluster of eyes, glowing
    gCv.Limb(Add(hd, Mul(hs, 3)), Add(hd, Mul(hs, -3)), 0.7f, 0.7f, cBONE, 30); // a painted band across the head
    Vector2 ga = wc.C(a); // the ground it tore open
    for (int k = 0; k < 7; k++) gCv.Ball(ga.x - 12 + k * 4.0f, ga.y + 1 - (k % 2) * 1.5f, 2.4f, 1.8f, cDIRT, 40);
    wc.Draw(M, 8);
}

// ---------------------------------------------------------------- the Arch-Serpent and its lair (the Island)
// Its lair: a mound of boulders and roots with a dark mouth, bones and shed skin at the entrance. While it's at home
// its eyes show in the dark now and then.
void PxSerpentLair(Vector2 at, float t, bool home) {
    enum { ROCK, ROCK2, MOSS, MOUTH, BONE, SKIN, VINE, EYE };
    px::Mat M[8] = {px::Ramp({104, 96, 84, 255}, px::PAT_SPECKLE), px::Ramp({86, 80, 74, 255}, px::PAT_SPECKLE), px::Ramp({70, 120, 56, 255}, px::PAT_SPECKLE),
                    px::Ramp({14, 10, 12, 255}), px::Ramp({226, 218, 196, 255}), px::Ramp({196, 196, 150, 255}, px::PAT_SCALES, 0.8f), px::Ramp({58, 104, 44, 255}), px::Glow({255, 210, 70, 255})};
    WorldCanvas wc(at.x - 110, at.y - 110, at.x + 110, at.y + 4);
    Vector2 g = wc.C(at);
    // the boulders, heaped into a mound around the mouth
    const float R[9][4] = {{-38, -8, 16, 11}, {36, -7, 17, 12}, {-24, -26, 15, 12}, {22, -28, 16, 13}, {0, -40, 18, 12}, {-44, -2, 10, 7}, {46, -2, 11, 8}, {-12, -46, 10, 7}, {14, -48, 9, 6}};
    for (int k = 0; k < 9; k++) gCv.Ball(g.x + R[k][0], g.y + R[k][1], R[k][2], R[k][3], k % 2 ? ROCK2 : ROCK, 4 + (k % 3), R[k][3]);
    gCv.Ball(g.x, g.y - 12, 17, 15, MOUTH, 14, 1, g.y + 1);                          // the dark mouth
    gCv.Limb({g.x - 18, g.y - 24}, {g.x + 18, g.y - 26}, 5, 5, ROCK, 18);             // the lintel stone over it
    for (int k = 0; k < 6; k++) gCv.Ball(g.x - 30 + k * 12.0f, g.y - 50 + (k % 2) * 4 - fabsf(k - 2.5f) * 3, 7, 3.5f, MOSS, 22); // moss and ferns on top
    for (int k = 0; k < 5; k++) { // roots and vines hanging over the mouth, stirring
        float x = g.x - 16 + k * 8.0f, len = 8 + (k * 7) % 11, sw = sinf(t * 0.9f + k) * 1.5f;
        gCv.Limb({x, g.y - 27}, {x + sw, g.y - 27 + len}, 0.9f, 0.6f, VINE, 24);
    }
    // bones and a shed skin at the entrance
    gCv.Limb({g.x - 12, g.y - 1}, {g.x - 2, g.y - 2}, 1.1f, 1.1f, BONE, 30); gCv.Ball(g.x - 13, g.y - 1, 2, 1.6f, BONE, 31);
    gCv.Ball(g.x + 8, g.y - 3, 3, 2.4f, BONE, 30); gCv.Dot((int)g.x + 7, (int)g.y - 4, MOUTH, 40); gCv.Dot((int)g.x + 9, (int)g.y - 4, MOUTH, 40); // a skull
    Vector2 sk[6]; float sr[6];
    for (int k = 0; k < 6; k++) { sk[k] = {g.x + 22 + k * 7.0f, g.y - 1.5f - sinf(k * 1.3f) * 1.2f}; sr[k] = 2.2f - k * 0.25f; }
    gCv.Chain(sk, 6, sr, SKIN, 26);
    if (home && fmodf(t * 0.35f, 5.0f) < 3.2f) { gCv.Dot((int)g.x - 4, (int)g.y - 14, EYE, 50); gCv.Dot((int)g.x - 3, (int)g.y - 14, EYE, 50); gCv.Dot((int)g.x + 3, (int)g.y - 14, EYE, 50); gCv.Dot((int)g.x + 4, (int)g.y - 14, EYE, 50); }
    wc.Draw(M, 8);
}

// The serpent itself: a thick scaled body pouring out of the lair's mouth and rearing up, a cream belly, a ridge of
// spines, a fringed hood, horns, burning eyes, and a jaw that gapes on the strike.
void PxSerpent(Vector2 lair, Vector2 head, Vector2 aim, float t, bool striking, bool rearing, float alpha) {
    enum { SCALE, BELLY, SPINE, HOOD, HORN, EYE, MOUTH, FANG, TONGUE };
    px::Mat M[9] = {px::Ramp({46, 116, 92, 255}, px::PAT_SCALES, 1.2f), px::Ramp({216, 204, 150, 255}, px::PAT_BANDS, 0.7f), px::Ramp({30, 70, 60, 255}),
                    px::Ramp({60, 150, 120, 255}, px::PAT_SPOTS, 1.0f), px::Ramp({232, 220, 186, 255}), px::Glow({255, 214, 60, 255}), px::Ramp({140, 30, 44, 255}),
                    px::Ramp({245, 240, 225, 255}), px::Ramp({200, 40, 60, 255})};
    M[3].accent = {30, 60, 40, 255};
    float side = aim.x >= head.x ? 1.0f : -1.0f;
    WorldCanvas wc(std::min(lair.x, head.x) - 90, std::min(lair.y, head.y) - 70, std::max(lair.x, head.x) + 90, lair.y + 10);
    // the path: out of the mouth along the ground, then up in an S to the head
    Vector2 p0 = {lair.x, lair.y - 22}, p1 = {lair.x + side * 34, lair.y - 18}, p2 = {head.x - side * 26, head.y + 90}, p3 = head; // (close to the straight line its strike is judged along)
    const int N = 26;
    Vector2 pts[N + 1]; float rad[N + 1];
    for (int k = 0; k <= N; k++) {
        float u = k / (float)N;
        Vector2 q = Bez(p0, p1, p2, p3, u);
        q.x += sinf(u * 7 - t * 1.6f) * 7 * (1 - u) * (rearing ? 1 : 0.5f); // it sways, coiled with menace
        pts[k] = wc.C(q); rad[k] = 11.5f - 5.0f * u;
    }
    gCv.Chain(pts, N + 1, rad, SCALE, 6);
    for (int k = 0; k <= N; k++) { // the belly, on the side it faces
        Vector2 dir = Norm(Sub(pts[std::min(N, k + 1)], pts[std::max(0, k - 1)]));
        Vector2 n{-dir.y, dir.x}; if (n.x * side < 0) n = Mul(n, -1);
        Vector2 b = Add(pts[k], Mul(n, rad[k] * 0.5f));
        gCv.Ball(b.x, b.y, rad[k] * 0.55f, rad[k] * 0.55f, BELLY, 6 + rad[k] * 0.6f, rad[k] * 0.5f);
        if (k % 2 == 0 && k > 1 && k < N - 1) { Vector2 bk = Add(pts[k], Mul(n, -rad[k] * 0.85f)); gCv.Tri(Add(bk, Mul(dir, -2)), Add(bk, Mul(dir, 2)), Add(bk, Mul(n, -4.5f)), SPINE, 7 + rad[k], -n.x * 0.5f, -0.5f); } // the ridge of spines
    }
    Vector2 h = pts[N], fwd = Norm(Vector2{side, rearing && !striking ? 0.25f : 0.6f});
    if (striking) fwd = Norm(Sub(wc.C(aim), h));
    Vector2 up{fwd.y * -side, -fabsf(fwd.x)}; up = Norm(Vector2{-fwd.y * side, fwd.x * side}); if (up.y > 0) up = Mul(up, -1);
    // the hood: a fringe of membrane fanning out behind the head
    for (int k = -3; k <= 3; k++) { Vector2 tip = Add(h, Add(Mul(up, 12 + (3 - abs(k)) * 2.0f), Mul(fwd, -6 + k * 3.5f))); gCv.Tri(Add(h, Mul(fwd, -3)), Add(h, Mul(fwd, 4)), tip, HOOD, 18, -fwd.x * 0.3f, -0.3f); }
    gCv.Ball(h.x, h.y, 10.5f, 8.5f, SCALE, 26, 8);                                 // the skull, broad and heavy
    Vector2 sn = Add(h, Mul(fwd, 10));
    gCv.Ball(sn.x, sn.y, 7.5f, 5.5f, SCALE, 28, 6);                                // the snout
    for (int s = 0; s < 2; s++) { // two horns sweeping back
        Vector2 hb = Add(h, Add(Mul(up, 5), Mul(fwd, s ? -2.0f : 2.0f))), hm = Add(hb, Add(Mul(up, 7), Mul(fwd, -5.0f))), ht = Add(hm, Add(Mul(up, 3), Mul(fwd, -8.0f)));
        gCv.Limb(hb, hm, 2.2f, 1.5f, HORN, 30 - s * 8); gCv.Limb(hm, ht, 1.5f, 0.4f, HORN, 30 - s * 8);
    }
    float gape = striking ? 1.0f : rearing ? 0.35f : 0.0f;
    if (gape > 0) { // the jaw drops: a red mouth and fangs
        Vector2 jaw = Add(sn, Add(Mul(fwd, 3), Mul(up, -3 - gape * 7)));
        gCv.Tri(Add(h, Mul(up, -2)), Add(sn, Mul(fwd, 5)), jaw, MOUTH, 27, 0, 0.3f);
        gCv.Limb(Add(h, Mul(up, -3)), jaw, 2.4f, 1.6f, SCALE, 29);
        gCv.Dot((int)(sn.x + fwd.x * 3), (int)(sn.y + fwd.y * 3 + 2), FANG, 40); gCv.Dot((int)(sn.x + fwd.x * 1), (int)(sn.y + fwd.y * 1 + 2), FANG, 40);
    } else if (fmodf(t * 0.8f, 3.0f) < 0.4f) { // the tongue flickering out
        Vector2 t0 = Add(sn, Mul(fwd, 5)), t1 = Add(t0, Mul(fwd, 6));
        gCv.Limb(t0, t1, 0.6f, 0.5f, TONGUE, 20); gCv.Limb(t1, Add(t1, Add(Mul(fwd, 2), Mul(up, 2))), 0.4f, 0.3f, TONGUE, 20); gCv.Limb(t1, Add(t1, Add(Mul(fwd, 2), Mul(up, -2))), 0.4f, 0.3f, TONGUE, 20);
    }
    Vector2 eye = Add(h, Add(Mul(fwd, 3), Mul(up, 2.5f)));
    gCv.Dot((int)eye.x, (int)eye.y, EYE, 50); gCv.Dot((int)eye.x + 1, (int)eye.y, EYE, 50);
    gCv.Limb(Add(eye, Add(Mul(up, 1.8f), Mul(fwd, -2))), Add(eye, Add(Mul(up, 1.2f), Mul(fwd, 2.5f))), 0.8f, 0.8f, SPINE, 45); // the brow ridge
    wc.Draw(M, 9, alpha);
}
