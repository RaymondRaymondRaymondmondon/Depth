// The parkour creatures redrawn as shaded pixel sprites (pixelart.h): each is modelled from volumes - balls, tapered
// limbs, chains, facets - in world units, lit and inked by px::Render. The draw loop grows a creature by its species
// size with a matrix; PxSetScale makes the art pixel shrink by the same factor, so after the transform every art
// pixel is still two world pixels (a big shark just gets more of them), never a blown-up blocky pixel.
#include "beastart.h"
#include "pixelart.h"
#include "game.h"
#include "beasts.h"
#include "ik.h"
#include <algorithm>
#include <cmath>

namespace {
px::Canvas gCv;
float gPxCell = 2; // world units per art pixel before the size transform
float gPxAlpha = 1; // a fade on everything drawn (distant creatures in the background web)
constexpr float PIf = 3.14159265f;
Vector2 Add(Vector2 a, Vector2 b) { return {a.x + b.x, a.y + b.y}; }
Vector2 Sub(Vector2 a, Vector2 b) { return {a.x - b.x, a.y - b.y}; }
Vector2 Mul(Vector2 a, float k) { return {a.x * k, a.y * k}; }
float Len(Vector2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
Vector2 Norm(Vector2 a) { float l = Len(a); return l > 1e-4f ? Vector2{a.x / l, a.y / l} : Vector2{1, 0}; }
Vector2 Lerp(Vector2 a, Vector2 b, float u) { return {a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u}; }
Vector2 Bez(Vector2 a, Vector2 b, Vector2 c, Vector2 d, float u) { float v = 1 - u; return Add(Add(Mul(a, v * v * v), Mul(b, 3 * v * v * u)), Add(Mul(c, 3 * v * u * u), Mul(d, u * u * u))); }
Color Dim(Color c, bool dead) { return dead ? Color{(unsigned char)((c.r + 90) / 2), (unsigned char)((c.g + 86) / 2), (unsigned char)((c.b + 96) / 2), 255} : c; }
Color Shade(Color c, float k) { return {(unsigned char)std::clamp(c.r * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.g * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.b * k, 0.0f, 255.0f), 255}; }
px::Mat R(Color c, int pat = px::PAT_NONE, float s = 1) { return px::Ramp(c, pat, s); }

// a canvas over a world rectangle, with every primitive taking world units
struct Pen {
    Vector2 origin; int w, h;
    Pen(float x0, float y0, float x1, float y1) {
        float c = gPxCell;
        origin = {floorf(x0 / c) * c, floorf(y0 / c) * c};
        w = std::clamp((int)ceilf((x1 - origin.x) / c) + 1, 1, 520); h = std::clamp((int)ceilf((y1 - origin.y) / c) + 1, 1, 400);
        gCv.Begin(w, h);
    }
    Vector2 C(Vector2 p) const { return {(p.x - origin.x) / gPxCell, (p.y - origin.y) / gPxCell}; }
    float Rr(float r) const { return r / gPxCell; }
    void Ball(Vector2 p, float rx, float ry, int m, float z = 0, float clipY = -1e9f) { Vector2 c = C(p); gCv.Ball(c.x, c.y, Rr(rx), Rr(ry), m, Rr(z), -1, clipY > -1e8f ? C({0, clipY}).y : -1); }
    void Limb(Vector2 a, Vector2 b, float ra, float rb, int m, float z = 0) { gCv.Limb(C(a), C(b), Rr(ra), Rr(rb), m, Rr(z)); }
    void Chain(const Vector2* p, int n, const float* r, int m, float z = 0) { Vector2 cp[64]; float cr[64]; n = std::min(n, 64); for (int k = 0; k < n; k++) { cp[k] = C(p[k]); cr[k] = Rr(r[k]); } gCv.Chain(cp, n, cr, m, Rr(z)); }
    void Tri(Vector2 a, Vector2 b, Vector2 c, int m, float z = 0, float nx = 0, float ny = -0.4f) { gCv.Tri(C(a), C(b), C(c), m, Rr(z), nx, ny); }
    void Dot(Vector2 p, int m, float z = 500) { Vector2 c = C(p); gCv.Dot((int)floorf(c.x), (int)floorf(c.y), m, Rr(z)); }
    void Draw(const px::Mat* M, int n, float alpha = 1) { px::Render(gCv, M, n, origin, gPxCell, false, alpha * gPxAlpha); }
};
} // namespace

void PxSetScale(float sz) { gPxCell = 2.0f / std::clamp(sz, 0.1f, 8.0f); }
void PxSetAlpha(float a) { gPxAlpha = a; }

// ---------------------------------------------------------------- the Timber-Shell Tortoise (the Pirate Ship), and the Cave's Crystal-Shelled one
void PxTortoise(Vector2 c, float facing, float phase, bool moving, bool dead, float t, bool crystal) {
    enum { SHELL, RIM, SKIN, BEAK, EYE, IRON, WOOD, BARN, NAIL, GLOW };
    px::Mat M[10] = {
        crystal ? R(Dim({70, 130, 150, 255}, dead), px::PAT_SCUTES, 1.0f) : R(Dim({108, 90, 62, 255}, dead), px::PAT_SCUTES, 1.1f),
        R(Dim(crystal ? Color{60, 80, 96, 255} : Color{80, 62, 44, 255}, dead)),
        R(Dim(crystal ? Color{150, 150, 160, 255} : Color{130, 136, 100, 255}, dead), px::PAT_SCALES, 0.7f),
        R(Dim({66, 58, 50, 255}, dead)), R({18, 14, 12, 255}), R(Dim({74, 76, 84, 255}, dead)), R(Dim({142, 102, 62, 255}, dead), px::PAT_BANDS, 0.6f),
        R(Dim({208, 202, 182, 255}, dead), px::PAT_SPECKLE), R({222, 212, 180, 255}), px::Glow({150, 240, 255, 255})};
    const float cx = 36, cy = crystal ? 18.0f : 21.0f, legLen = crystal ? 20.0f : 10.0f;
    auto A = [&](float ax, float ay) { return Vector2{c.x + (ax - cx) * 2 * facing, c.y + (ay - cy) * 2}; }; // its design grid, in art pixels
    Pen P(c.x - 90, c.y - 60, c.x + 90, c.y + (cy + 3 + legLen) * 2 + 4);
    float st = moving ? sinf(phase * 2) : 0, fy = cy + 1 + legLen;
    auto leg = [&](float x, float swing, float lift, float r, float z) {
        Vector2 hip = A(x, cy + 1), foot = A(x + swing * 1.6f, fy - std::max(0.0f, lift) * 1.8f);
        P.Limb(hip, foot, r * 2, r * 1.8f, SKIN, z * 2);
        P.Ball(Add(foot, {facing, 0}), (r + 0.6f) * 2, 3.8f, SKIN, z * 2 + 2);
        for (int k = 0; k < 3; k++) P.Dot(Add(foot, {(r - 1) * 2 * facing, (k - 1) * 2.0f}), NAIL, z * 2 + 6);
    };
    leg(47, st, -st, 3.2f, 1); leg(24, -st, st, 3.2f, 1);
    P.Ball(A(cx, cy + 2), 44, 34, SHELL, 12, A(0, cy + 1.5f).y);
    P.Limb(A(14, cy + 1.5f), A(58, cy + 1.5f), 5, 5, RIM, 18);
    float bob = sinf(t * 1.3f + phase) * 0.6f;
    P.Limb(A(15, cy + 2), A(9, cy + 4), 3.6f, 1.2f, SKIN, 8);
    P.Limb(A(56, cy - 1), A(63, cy - 4 + bob), 7.2f, 5.8f, SKIN, 20);
    P.Ball(A(66, cy - 5 + bob), 10.4f, 8, SKIN, 24);
    P.Ball(A(70, cy - 4 + bob), 5.6f, 4.8f, SKIN, 26);
    P.Tri(A(69, cy - 3 + bob), A(74, cy - 3 + bob), A(70, cy - 0.5f + bob), BEAK, 30, 0.2f * facing, 0.3f);
    if (!(fmodf(t * 0.4f + phase, 4.0f) < 0.12f) && !dead) { P.Dot(A(67, cy - 7 + bob), EYE, 80); P.Dot(A(68, cy - 7 + bob), EYE, 80); }
    leg(49, -st, st, 3.8f, 16); leg(22, st, -st, 3.8f, 16);
    if (crystal) for (int k = 0; k < 5; k++) { float x = 24 + k * 6.0f, top = cy - 13 + fabsf(k - 2.0f) * 2.5f; P.Tri(A(x - 2.2f, top + 6), A(x + 2.2f, top + 6), A(x, top - 4 - (k % 2) * 3), GLOW, 60); }
    else {
        P.Limb(A(24, cy - 11), A(41, cy - 14), 2.6f, 2.6f, WOOD, 48); P.Limb(A(30, cy - 8), A(48, cy - 9), 2.4f, 2.4f, WOOD, 46);
        P.Ball(A(28, cy - 12), 5.6f, 5.6f, IRON, 54); P.Ball(A(36, cy - 15), 5.2f, 5.2f, IRON, 56); P.Ball(A(45, cy - 11), 4.8f, 4.8f, IRON, 52);
        for (int k = 0; k < 5; k++) P.Ball(A(17 + k * 9.0f, cy - 1 - (k % 2) * 3), 3.2f, 2.6f, BARN, 40);
    }
    P.Draw(M, 10);
}

// ---------------------------------------------------------------- the Totem-Centipede (the Island)
void PxCentipede(Vector2 a, Vector2 h, float t, float facing) {
    enum { cRED, cOCH, cDARK, cLEG, cBONE, cVEN, cEYE, cDIRT };
    px::Mat M[8] = {R({150, 54, 38, 255}), R({196, 140, 58, 255}), R({52, 32, 30, 255}), R({196, 96, 40, 255}), R({230, 220, 190, 255}), R({150, 40, 150, 255}), px::Glow({255, 220, 90, 255}), R({110, 84, 56, 255}, px::PAT_SPECKLE)};
    Pen P(std::min(a.x, h.x) - 60, std::min(a.y, h.y) - 60, std::max(a.x, h.x) + 60, std::max(a.y, h.y) + 40);
    Vector2 d = Sub(h, a), perp = Norm(Vector2{-d.y, d.x});
    if (perp.x * facing > 0) perp = Mul(perp, -1);
    const int N = 16;
    Vector2 pts[N + 1];
    for (int k = 0; k <= N; k++) { float u = k / (float)N; pts[k] = Add(Add(Lerp(a, h, u), Mul(perp, sinf(u * PIf) * Len(d) * 0.22f)), Mul(perp, sinf(u * 9 - t * 7) * 8 * (1 - u))); }
    for (int k = 0; k <= N; k++) {
        float u = k / (float)N, r = 13 - 4 * u;
        Vector2 q = pts[k], dir = Norm(Sub(pts[std::min(N, k + 1)], pts[std::max(0, k - 1)])), side{-dir.y, dir.x};
        for (int s = -1; s <= 1; s += 2) {
            float wave = sinf(t * 16 - k * 0.9f + (s > 0 ? PIf : 0));
            Vector2 root = Add(q, Mul(side, s * r * 0.5f)), knee = Add(root, Add(Mul(side, s * 9.0f), Mul(dir, wave * 4.4f))), tip = Add(knee, Add(Mul(side, s * 7.0f), Mul(dir, -3 + wave * 3)));
            float z = s > 0 ? 2 + u * 8 : 40 + u * 8;
            P.Limb(root, knee, 2.2f, 1.8f, cLEG, z); P.Limb(knee, tip, 1.8f, 0.8f, cLEG, z);
        }
        int m = (k / 2) % 3 == 0 ? cRED : (k / 2) % 3 == 1 ? cOCH : cDARK;
        P.Ball(q, r * 1.15f, r, m, 12 + u * 12);
        if (k % 3 == 1) { Vector2 up = Mul(side, -1); P.Dot(Add(q, Add(Mul(dir, 3), Mul(up, 3))), cBONE); P.Dot(Add(q, Add(Mul(dir, -3), Mul(up, 3))), cBONE); P.Dot(Add(q, Mul(up, -2.4f)), cBONE); }
    }
    Vector2 hd = pts[N], hdir = Norm(Sub(pts[N], pts[N - 2])), hs{-hdir.y, hdir.x};
    P.Ball(Add(hd, Mul(hdir, 6)), 15, 12, cRED, 40);
    for (int s = -1; s <= 1; s += 2) {
        Vector2 j0 = Add(Add(hd, Mul(hdir, 14)), Mul(hs, s * 6.0f)), j1 = Add(Add(j0, Mul(hdir, 10)), Mul(hs, -s * 7.0f));
        P.Limb(j0, j1, 3.2f, 1.4f, cDARK, 52); P.Dot(j1, cVEN);
        float sw = sinf(t * 5 + s) * 0.4f;
        Vector2 an0 = Add(Add(hd, Mul(hdir, 10)), Mul(hs, s * 8.0f)), an1 = Add(an0, Add(Mul(hdir, 18), Mul(hs, s * (14 + sw * 12)))), an2 = Add(an1, Add(Mul(hdir, 16), Mul(hs, s * (10 - sw * 8))));
        P.Limb(an0, an1, 1.6f, 1.2f, cOCH, 48); P.Limb(an1, an2, 1.2f, 0.8f, cOCH, 48);
        P.Dot(Add(hd, Add(Mul(hdir, 12), Mul(hs, s * 5.0f))), cEYE);
    }
    P.Limb(Add(hd, Mul(hs, 6)), Add(hd, Mul(hs, -6)), 1.4f, 1.4f, cBONE, 60);
    for (int k = 0; k < 7; k++) P.Ball({a.x - 24 + k * 8.0f, a.y + 2 - (k % 2) * 3.0f}, 4.8f, 3.6f, cDIRT, 80);
    P.Draw(M, 8);
}

// ---------------------------------------------------------------- the Arch-Serpent and its lair (the Island)
void PxSerpentLair(Vector2 at, float t, bool home) {
    enum { ROCK, ROCK2, MOSS, MOUTH, BONE, SKIN, VINE, EYE };
    px::Mat M[8] = {R({104, 96, 84, 255}, px::PAT_SPECKLE), R({86, 80, 74, 255}, px::PAT_SPECKLE), R({70, 120, 56, 255}, px::PAT_SPECKLE), R({14, 10, 12, 255}), R({226, 218, 196, 255}), R({196, 196, 150, 255}, px::PAT_SCALES, 0.8f), R({58, 104, 44, 255}), px::Glow({255, 210, 70, 255})};
    Pen P(at.x - 110, at.y - 110, at.x + 110, at.y + 4);
    const float Rk[9][4] = {{-38, -8, 16, 11}, {36, -7, 17, 12}, {-24, -26, 15, 12}, {22, -28, 16, 13}, {0, -40, 18, 12}, {-44, -2, 10, 7}, {46, -2, 11, 8}, {-12, -46, 10, 7}, {14, -48, 9, 6}};
    for (int k = 0; k < 9; k++) P.Ball({at.x + Rk[k][0] * 2, at.y + Rk[k][1] * 2}, Rk[k][2] * 2, Rk[k][3] * 2, k % 2 ? ROCK2 : ROCK, (4 + (k % 3)) * 2);
    P.Ball({at.x, at.y - 24}, 34, 30, MOUTH, 28, at.y + 2);
    P.Limb({at.x - 36, at.y - 48}, {at.x + 36, at.y - 52}, 10, 10, ROCK, 36);
    for (int k = 0; k < 6; k++) P.Ball({at.x - 60 + k * 24.0f, at.y - 100 + (k % 2) * 8 - fabsf(k - 2.5f) * 6}, 14, 7, MOSS, 44);
    for (int k = 0; k < 5; k++) { float x = at.x - 32 + k * 16.0f, len = 16 + (k * 14) % 22, sw = sinf(t * 0.9f + k) * 3; P.Limb({x, at.y - 54}, {x + sw, at.y - 54 + len}, 1.8f, 1.2f, VINE, 48); }
    P.Limb({at.x - 24, at.y - 2}, {at.x - 4, at.y - 4}, 2.2f, 2.2f, BONE, 60); P.Ball({at.x - 26, at.y - 2}, 4, 3.2f, BONE, 62);
    P.Ball({at.x + 16, at.y - 6}, 6, 4.8f, BONE, 60); P.Dot({at.x + 14, at.y - 8}, MOUTH); P.Dot({at.x + 18, at.y - 8}, MOUTH);
    Vector2 sk[6]; float sr[6];
    for (int k = 0; k < 6; k++) { sk[k] = {at.x + 44 + k * 14.0f, at.y - 3 - sinf(k * 1.3f) * 2.4f}; sr[k] = 4.4f - k * 0.5f; }
    P.Chain(sk, 6, sr, SKIN, 52);
    if (home && fmodf(t * 0.35f, 5.0f) < 3.2f) for (int e = -1; e <= 1; e += 2) { P.Dot({at.x + e * 7.0f, at.y - 28}, EYE); P.Dot({at.x + e * 7.0f + 2, at.y - 28}, EYE); }
    P.Draw(M, 8);
}
void PxSerpent(Vector2 lair, Vector2 head, Vector2 aim, float t, bool striking, bool rearing, float alpha) {
    enum { SCALE, BELLY, SPINE, HOOD, HORN, EYE, MOUTH, FANG, TONGUE };
    px::Mat M[9] = {R({46, 116, 92, 255}, px::PAT_SCALES, 1.2f), R({216, 204, 150, 255}, px::PAT_BANDS, 0.7f), R({30, 70, 60, 255}), R({60, 150, 120, 255}, px::PAT_SPOTS, 1.0f), R({232, 220, 186, 255}),
                    px::Glow({255, 214, 60, 255}), R({140, 30, 44, 255}), R({245, 240, 225, 255}), R({200, 40, 60, 255})};
    M[3].accent = {30, 60, 40, 255};
    float side = aim.x >= head.x ? 1.0f : -1.0f;
    Pen P(std::min(lair.x, head.x) - 90, std::min(lair.y, head.y) - 70, std::max(lair.x, head.x) + 90, lair.y + 10);
    Vector2 p0 = {lair.x, lair.y - 22}, p1 = {lair.x + side * 34, lair.y - 18}, p2 = {head.x - side * 26, head.y + 90}, p3 = head;
    const int N = 26;
    Vector2 pts[N + 1]; float rad[N + 1];
    for (int k = 0; k <= N; k++) { float u = k / (float)N; Vector2 q = Bez(p0, p1, p2, p3, u); q.x += sinf(u * 7 - t * 1.6f) * 7 * (1 - u) * (rearing ? 1 : 0.5f); pts[k] = q; rad[k] = 23 - 10 * u; }
    P.Chain(pts, N + 1, rad, SCALE, 12);
    for (int k = 0; k <= N; k++) {
        Vector2 dir = Norm(Sub(pts[std::min(N, k + 1)], pts[std::max(0, k - 1)])), n{-dir.y, dir.x}; if (n.x * side < 0) n = Mul(n, -1);
        P.Ball(Add(pts[k], Mul(n, rad[k] * 0.5f)), rad[k] * 0.55f, rad[k] * 0.55f, BELLY, 12 + rad[k] * 0.6f);
        if (k % 2 == 0 && k > 1 && k < N - 1) { Vector2 bk = Add(pts[k], Mul(n, -rad[k] * 0.85f)); P.Tri(Add(bk, Mul(dir, -4)), Add(bk, Mul(dir, 4)), Add(bk, Mul(n, -9)), SPINE, 14 + rad[k], -n.x * 0.5f, -0.5f); }
    }
    Vector2 h = pts[N], fwd = Norm(Vector2{side, rearing && !striking ? 0.25f : 0.6f});
    if (striking) fwd = Norm(Sub(aim, h));
    Vector2 up = Norm(Vector2{-fwd.y * side, fwd.x * side}); if (up.y > 0) up = Mul(up, -1);
    for (int k = -3; k <= 3; k++) { Vector2 tip = Add(h, Add(Mul(up, 24 + (3 - abs(k)) * 4.0f), Mul(fwd, -12 + k * 7.0f))); P.Tri(Add(h, Mul(fwd, -6)), Add(h, Mul(fwd, 8)), tip, HOOD, 36, -fwd.x * 0.3f, -0.3f); }
    P.Ball(h, 21, 17, SCALE, 52);
    Vector2 sn = Add(h, Mul(fwd, 20));
    P.Ball(sn, 15, 11, SCALE, 56);
    for (int s = 0; s < 2; s++) { Vector2 hb = Add(h, Add(Mul(up, 10), Mul(fwd, s ? -4.0f : 4.0f))), hm = Add(hb, Add(Mul(up, 14), Mul(fwd, -10.0f))), ht = Add(hm, Add(Mul(up, 6), Mul(fwd, -16.0f))); P.Limb(hb, hm, 4.4f, 3, HORN, 60 - s * 16); P.Limb(hm, ht, 3, 0.8f, HORN, 60 - s * 16); }
    float gape = striking ? 1.0f : rearing ? 0.35f : 0.0f;
    if (gape > 0) {
        Vector2 jaw = Add(sn, Add(Mul(fwd, 6), Mul(up, -6 - gape * 14)));
        P.Tri(Add(h, Mul(up, -4)), Add(sn, Mul(fwd, 10)), jaw, MOUTH, 54, 0, 0.3f);
        P.Limb(Add(h, Mul(up, -6)), jaw, 4.8f, 3.2f, SCALE, 58);
        P.Dot(Add(sn, Add(Mul(fwd, 6), {0, 4})), FANG); P.Dot(Add(sn, Add(Mul(fwd, 2), {0, 4})), FANG);
    } else if (fmodf(t * 0.8f, 3.0f) < 0.4f) {
        Vector2 t0 = Add(sn, Mul(fwd, 10)), t1 = Add(t0, Mul(fwd, 12));
        P.Limb(t0, t1, 1.2f, 1.0f, TONGUE, 40); P.Limb(t1, Add(t1, Add(Mul(fwd, 4), Mul(up, 4))), 0.8f, 0.6f, TONGUE, 40); P.Limb(t1, Add(t1, Add(Mul(fwd, 4), Mul(up, -4))), 0.8f, 0.6f, TONGUE, 40);
    }
    Vector2 eye = Add(h, Add(Mul(fwd, 6), Mul(up, 5)));
    P.Dot(eye, EYE); P.Dot(Add(eye, {2, 0}), EYE);
    P.Limb(Add(eye, Add(Mul(up, 3.6f), Mul(fwd, -4))), Add(eye, Add(Mul(up, 2.4f), Mul(fwd, 5))), 1.6f, 1.6f, SPINE, 90);
    P.Draw(M, 9, alpha);
}

// ================================================================ the creature kit
namespace {
struct LegSet { Vector2 hip[4], knee[4], foot[4]; };
LegSet Legs(const Beast& b, const SpeciesDef& S, float frontBend, float backBend) {
    LegSet L;
    float legLen = std::min(12.0f, S.radius * b.scale * 0.6f) + S.radius * b.scale * 0.2f;
    for (int k = 0; k < LEGS; k++) {
        Vector2 hip = BeastHip(b, S, k), foot = b.legs[k].init ? b.legs[k].foot : Vector2{hip.x, hip.y + legLen};
        if (k == 2 && b.health < 0.6f && b.life == BeastLife::Alive) foot = {hip.x - b.facing * legLen * 0.4f, hip.y + legLen * 0.62f};
        L.hip[k] = hip; L.foot[k] = foot; L.knee[k] = ik::Knee(hip, foot, legLen * 0.55f, legLen * 0.6f, (k < 2 ? frontBend : backBend) * b.facing);
    }
    return L;
}
void DrawLegs(Pen& P, const LegSet& L, bool nearSide, float r, int m, int claw, float z) {
    for (int k = 0; k < LEGS; k++) {
        if ((k % 2 == 0) != nearSide) continue;
        P.Limb(L.hip[k], L.knee[k], r, r * 0.85f, m, z); P.Limb(L.knee[k], L.foot[k], r * 0.85f, r * 0.7f, m, z);
        P.Ball(L.foot[k], r * 0.95f, r * 0.6f, claw, z + 1);
    }
}
void Body(Pen& P, const Vector2* pts, int n, float r0, float r1, int back, int belly, float z, float bellyK = 0.55f) {
    float rr[64];
    n = std::min(n, 64);
    for (int k = 0; k < n; k++) rr[k] = r0 + (r1 - r0) * k / std::max(1.0f, n - 1.0f);
    P.Chain(pts, n, rr, back, z);
    if (belly >= 0) for (int k = 0; k < n; k++) {
        Vector2 dir = Norm(Sub(pts[std::min(n - 1, k + 1)], pts[std::max(0, k - 1)])), nn{-dir.y, dir.x}; if (nn.y < 0) nn = Mul(nn, -1);
        P.Ball(Add(pts[k], Mul(nn, rr[k] * 0.45f)), rr[k] * bellyK, rr[k] * bellyK * 0.8f, belly, z + rr[k] * 0.55f);
    }
}
Vector2 BackDir(const Beast& b) { Vector2 d{b.spine[2].x - b.pos.x, b.spine[2].y - b.pos.y}; float l = Len(d); return l > 2 ? Vector2{d.x / l, d.y / l} : Vector2{-b.facing, 0}; }

// ---- a fish: a lens-shaped body swept along its heading, forked tail, fins, eye and mouth
struct Fish { Color back, belly, fin; int pat = px::PAT_NONE; float patS = 1; Color accent{0, 0, 0, 0}; float len = 20, girth = 4, tail = 5; int kind = 0; Color eye{20, 16, 16, 255}; bool eyeGlow = false; };
enum FishKind { F_PLAIN, F_SHARK, F_BARRACUDA, F_LOACH, F_ANGLER, F_MANATEE, F_WHALE };
void PxFish(const Beast& b, const Fish& F, float t, float gape, float alpha, float wave = 1) {
    enum { BK, BL, FN, EY, MO, TE, AC, GL };
    px::Mat M[8] = {R(F.back, F.pat, F.patS), R(F.belly), R(F.fin), F.eyeGlow ? px::Glow(F.eye) : R(F.eye), R({60, 14, 26, 255}), R({240, 236, 225, 255}), R(F.accent.a ? F.accent : F.fin), px::Glow({180, 250, 255, 255})};
    if (F.accent.a && F.pat != px::PAT_NONE) M[0].accent = F.accent;
    Vector2 bk = BackDir(b), n{-bk.y, bk.x}; if (n.y > 0) n = Mul(n, -1);
    float L = F.len, G = F.girth;
    Pen P(b.pos.x - L - F.tail - 30, b.pos.y - L - 30, b.pos.x + L + F.tail + 30, b.pos.y + L + 30);
    const int N = 7;
    Vector2 pts[N]; float rr[N];
    for (int k = 0; k < N; k++) {
        float u = k / (N - 1.0f);
        float sway = sinf(b.phase * 7 - k * 0.9f) * G * 0.35f * u * u * wave;
        pts[k] = Add(Add(b.pos, Mul(bk, -L * 0.5f + L * u)), Mul(n, sway));
        float prof = F.kind == F_ANGLER ? sinf(PIf * (0.15f + 0.8f * u)) : (F.kind == F_MANATEE || F.kind == F_WHALE) ? powf(sinf(PIf * (0.12f + 0.85f * u)), 0.5f) : powf(sinf(PIf * (0.08f + 0.9f * u)), 0.75f);
        rr[k] = std::max(0.8f, G * prof);
    }
    Vector2 head = pts[0], root = pts[N - 1], hdir = Mul(bk, -1);
    float beat = sinf(b.phase * 7 - 5) * F.tail * 0.45f * wave;
    if (F.kind == F_MANATEE) P.Ball(Add(root, Add(Mul(bk, F.tail * 0.6f), Mul(n, beat * 0.3f))), F.tail * 0.8f, F.tail * 0.45f, BK, 4);
    else if (F.kind == F_WHALE) { Vector2 tr = Add(root, Mul(bk, F.tail * 0.5f)); P.Tri(tr, Add(tr, Add(Mul(bk, F.tail), Mul(n, F.tail * 0.9f + beat))), Add(tr, Mul(bk, F.tail * 0.4f)), FN, 4); P.Tri(tr, Add(tr, Add(Mul(bk, F.tail), Mul(n, -F.tail * 0.9f + beat))), Add(tr, Mul(bk, F.tail * 0.4f)), FN, 3); }
    else {
        Vector2 t1 = Add(root, Add(Mul(bk, F.tail), Mul(n, F.tail * 0.85f + beat))), t2 = Add(root, Add(Mul(bk, F.tail), Mul(n, -F.tail * 0.85f + beat))), notch = Add(root, Mul(bk, F.tail * 0.45f));
        P.Tri(Add(root, Mul(bk, -2)), t1, notch, FN, 2, 0, -0.3f); P.Tri(Add(root, Mul(bk, -2)), t2, notch, FN, 2, 0, 0.3f);
    }
    Vector2 pf = Add(Add(b.pos, Mul(hdir, L * 0.2f)), Mul(n, -G * 0.4f));
    P.Tri(pf, Add(pf, Add(Mul(bk, G * 1.2f), Mul(n, -G * 0.9f))), Add(pf, Mul(bk, G * 0.5f)), FN, 1, 0, 0.4f);
    for (int k = 0; k + 1 < N; k++) P.Limb(pts[k], pts[k + 1], rr[k], rr[k + 1], BK, 8);
    for (int k = 1; k < N - 1; k++) P.Ball(Add(pts[k], Mul(n, -rr[k] * 0.45f)), rr[k] * 0.9f, rr[k] * 0.5f, BL, 8 + rr[k] * 0.7f);
    if (F.kind != F_MANATEE) {
        Vector2 d0 = Add(Add(b.pos, Mul(hdir, L * 0.05f)), Mul(n, G * 0.8f)), d1 = Add(Add(b.pos, Mul(bk, L * 0.3f)), Mul(n, G * 0.7f));
        float hgt = F.kind == F_SHARK ? G * 1.05f : F.kind == F_WHALE ? G * 0.4f : G * 0.8f;
        P.Tri(d0, d1, Add(Add(d0, Mul(bk, F.kind == F_SHARK ? L * 0.14f : L * 0.2f)), Mul(n, hgt)), FN, 6, 0, -0.5f);
    }
    Vector2 nf = Add(Add(b.pos, Mul(hdir, L * 0.18f)), Mul(n, -G * 0.55f));
    bool mammal = F.kind == F_MANATEE || F.kind == F_WHALE;
    P.Tri(nf, Add(nf, Add(Mul(bk, G * (mammal ? 1.6f : 1.1f)), Mul(n, -G * (mammal ? 1.1f : 0.8f)))), Add(nf, Mul(bk, G * 0.6f)), FN, 40, 0, 0.4f);
    Vector2 mouth = Add(head, Mul(hdir, rr[0] * 0.6f));
    if (gape > 0.05f) {
        P.Tri(Add(mouth, Mul(bk, G * 0.9f)), Add(mouth, Add(Mul(hdir, G * 0.5f), Mul(n, G * 0.35f * gape))), Add(mouth, Add(Mul(hdir, G * 0.3f), Mul(n, -G * 0.7f * gape))), MO, 30);
        if (F.kind == F_SHARK || F.kind == F_BARRACUDA || F.kind == F_ANGLER) for (int k = 0; k < 3; k++) P.Dot(Add(mouth, Add(Mul(bk, k * 2.5f), Mul(n, -G * 0.2f))), TE);
    }
    if (F.kind == F_SHARK) for (int k = 0; k < 3; k++) P.Limb(Add(Add(head, Mul(bk, G * 1.1f + k * 3)), Mul(n, G * 0.3f)), Add(Add(head, Mul(bk, G * 1.1f + k * 3)), Mul(n, -G * 0.2f)), 0.6f, 0.6f, AC, 40);
    if (F.kind == F_LOACH) for (int s = -1; s <= 1; s += 2) P.Limb(mouth, Add(mouth, Add(Mul(hdir, G * 1.2f), Mul(n, s * G * 0.5f - G * 0.3f))), 0.5f, 0.3f, AC, 40);
    if (F.kind == F_ANGLER) {
        Vector2 r0 = Add(head, Mul(n, G * 0.8f)), r1 = Add(Add(r0, Mul(n, G * 0.8f)), Mul(hdir, G * 0.6f)), lure = Add(Add(r1, Mul(hdir, G * 0.6f)), {sinf(t * 1.3f) * 2, cosf(t * 1.7f) * 2});
        P.Limb(r0, r1, 0.8f, 0.7f, FN, 30); P.Limb(r1, lure, 0.7f, 0.5f, FN, 30); P.Ball(lure, 2.6f, 2.6f, GL, 60);
    }
    P.Dot(Add(Add(head, Mul(bk, rr[0] * 0.4f + G * 0.15f)), Mul(n, G * 0.25f)), EY);
    P.Draw(M, 8, alpha);
}

// ---- an eel, a snake: a body along points
struct Eel { Color back, belly; int pat = px::PAT_NONE; float patS = 1; Color accent{0, 0, 0, 0}; float r0 = 3, r1 = 1; Color eye{20, 16, 16, 255}; bool eyeGlow = false; bool ribbon = false; int markings = 0; Color mark{0, 0, 0, 0}; };
void PxEel(const Vector2* pts, int n, const Eel& E, float gape, float t, float alpha, float facing) {
    enum { BK, BL, EY, MO, RB, MK };
    px::Mat M[6] = {R(E.back, E.pat, E.patS), R(E.belly), E.eyeGlow ? px::Glow(E.eye) : R(E.eye), R({70, 16, 30, 255}), R(Shade(E.back, 1.25f)), px::Glow(E.mark.a ? E.mark : Color{140, 240, 250, 255})};
    if (E.accent.a) M[0].accent = E.accent;
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int k = 0; k < n; k++) { x0 = std::min(x0, pts[k].x); x1 = std::max(x1, pts[k].x); y0 = std::min(y0, pts[k].y); y1 = std::max(y1, pts[k].y); }
    float m = E.r0 * 3 + 16;
    Pen P(x0 - m, y0 - m, x1 + m, y1 + m);
    if (E.ribbon) for (int k = 1; k + 1 < n; k++) { Vector2 dir = Norm(Sub(pts[k + 1], pts[k - 1])), nn{-dir.y, dir.x}; if (nn.y > 0) nn = Mul(nn, -1); float r = E.r0 + (E.r1 - E.r0) * k / (n - 1.0f); P.Tri(Add(pts[k], Mul(dir, -4)), Add(pts[k], Mul(dir, 4)), Add(pts[k], Mul(nn, r + 3 + sinf(t * 8 + k) * 1.5f)), RB, 2, 0, -0.5f); }
    Body(P, pts, n, E.r0, E.r1, BK, BL, 6, 0.5f);
    if (E.markings) for (int k = 1; k < n; k += 2) P.Dot(pts[k], MK);
    Vector2 hd = pts[0], dir = n > 1 ? Norm(Sub(pts[0], pts[1])) : Vector2{facing, 0}, nn{-dir.y, dir.x}; if (nn.y > 0) nn = Mul(nn, -1);
    P.Ball(Add(hd, Mul(dir, E.r0 * 0.4f)), E.r0 * 1.25f, E.r0 * 1.05f, BK, 20);
    if (gape > 0.05f) { Vector2 j = Add(hd, Mul(dir, E.r0 * 0.8f)); P.Tri(j, Add(j, Add(Mul(dir, E.r0 * 1.6f), Mul(nn, E.r0 * gape))), Add(j, Add(Mul(dir, E.r0 * 1.6f), Mul(nn, -E.r0 * gape * 1.2f))), MO, 22); }
    P.Dot(Add(Add(hd, Mul(dir, E.r0 * 0.7f)), Mul(nn, E.r0 * 0.5f)), EY);
    P.Draw(M, 6, alpha);
}

enum CrabKind { C_CRAB, C_COCONUT, C_STONE };
void PxCrab(Vector2 c, float f, float size, Color shell, float phase, bool moving, float open, int kind, float alpha, float t) {
    enum { SH, DK, LG, EY, GL, TIP };
    px::Mat M[6] = {R(shell, kind == C_STONE ? px::PAT_SCUTES : px::PAT_SPECKLE, kind == C_STONE ? 1.5f : 1), R(Shade(shell, 0.55f)), R(kind == C_COCONUT ? Color{190, 90, 60, 255} : shell), R({20, 16, 16, 255}), px::Glow({140, 240, 250, 255}), R({240, 230, 210, 255})};
    float S = size;
    (void)t;
    Pen P(c.x - S * 3.2f, c.y - S * 3.2f, c.x + S * 3.2f, c.y + S * 1.6f);
    float legLen = S * 0.9f;
    for (int side = 0; side < 2; side++) for (int k = 0; k < 3; k++) {
        float sx = side ? 1.0f : -1.0f, ph = phase * 8 + k * 2.1f + side * 1.3f, lift = moving ? std::max(0.0f, sinf(ph)) * S * 0.25f : 0;
        Vector2 hip{c.x + sx * S * (0.35f + k * 0.2f), c.y - S * 0.15f}, foot{c.x + sx * S * (1.1f + k * 0.28f), c.y + S * 0.55f - lift};
        Vector2 kn = ik::Knee(hip, foot, legLen * 0.62f, legLen * 0.62f, sx);
        float z = (k % 2) ? 2.0f : 30.0f;
        P.Limb(hip, kn, S * 0.14f, S * 0.12f, LG, z); P.Limb(kn, foot, S * 0.12f, S * 0.05f, LG, z);
    }
    P.Ball({c.x, c.y - S * 0.3f}, S * 1.0f, S * 0.62f, SH, 14);
    if (kind == C_STONE) { P.Dot({c.x - S * 0.3f, c.y - S * 0.55f}, GL); P.Dot({c.x + S * 0.2f, c.y - S * 0.4f}, GL); }
    for (int s = -1; s <= 1; s += 2) {
        Vector2 e0{c.x + f * S * 0.45f + s * S * 0.12f, c.y - S * 0.75f}, e1 = Add(e0, {f * S * 0.05f, -S * 0.3f});
        P.Limb(e0, e1, S * 0.06f, S * 0.05f, DK, 20); P.Dot(e1, kind == C_STONE ? GL : EY);
    }
    for (int s = -1; s <= 1; s += 2) {
        Vector2 sh{c.x + f * S * 0.7f, c.y - S * 0.2f + s * S * 0.12f}, el{c.x + f * S * 1.2f, c.y - S * (0.2f + 0.3f * open) + s * S * 0.18f};
        Vector2 cl = Add(el, {f * S * 0.45f, -S * 0.35f * open});
        float z = s > 0 ? 34.0f : 4.0f;
        P.Limb(sh, el, S * 0.16f, S * 0.16f, SH, z); P.Ball(cl, S * 0.34f, S * 0.24f, SH, z + 2);
        P.Tri(Add(cl, {f * S * 0.2f, -S * 0.08f}), Add(cl, {f * S * 0.6f, -S * (0.18f + 0.2f * open)}), Add(cl, {f * S * 0.25f, S * 0.05f}), TIP, z + 4, 0, -0.3f);
    }
    P.Draw(M, 6, alpha);
}

void PxShrimp(Vector2 c, float f, float len, Color col, bool glow, float t, float alpha, bool isopod) {
    enum { BD, DK, GL };
    px::Mat M[3] = {glow ? px::Glow(col) : R(col, px::PAT_RINGS, 0.8f), R(Shade(col, 0.6f)), px::Glow({255, 255, 255, 255})};
    Pen P(c.x - len * 2, c.y - len * 1.5f, c.x + len * 2, c.y + len * 1.2f);
    int N = isopod ? 7 : 6;
    for (int k = 0; k < N; k++) { float u = k / (N - 1.0f), ph = sinf(t * 16 + k * 1.1f); Vector2 root{c.x + f * len * (0.4f - u * 0.8f), c.y + len * 0.1f}; P.Limb(root, Add(root, {f * ph * len * 0.08f, len * 0.3f}), len * 0.04f + 0.4f, len * 0.03f + 0.3f, DK, 1); }
    for (int k = 0; k < N; k++) {
        float u = k / (N - 1.0f);
        Vector2 q = isopod ? Vector2{c.x + f * len * (0.5f - u), c.y - sinf(u * PIf) * len * 0.08f} : Vector2{c.x + f * len * (0.45f - u * 0.9f), c.y - sinf(u * PIf * 0.9f) * len * 0.22f + u * u * len * 0.25f};
        float r = isopod ? len * (0.2f - fabsf(u - 0.4f) * 0.12f) : len * (0.18f - u * 0.11f);
        P.Ball(q, r * 1.1f, r, BD, 6 + (isopod ? 0 : (1 - u)) * 4);
    }
    Vector2 tail{c.x - f * len * (isopod ? 0.55f : 0.5f), c.y + (isopod ? 0 : len * 0.25f)};
    P.Tri(tail, Add(tail, {-f * len * 0.25f, -len * 0.15f}), Add(tail, {-f * len * 0.25f, len * 0.15f}), BD, 3);
    Vector2 head{c.x + f * len * 0.5f, c.y - len * 0.05f};
    for (int s = 0; s < 2; s++) P.Limb(head, Add(head, {f * len * (0.8f + s * 0.3f), -len * (0.35f - s * 0.15f) + sinf(t * 4 + s) * len * 0.06f}), 0.6f, 0.4f, DK, 20);
    P.Draw(M, 3, alpha);
}

void PxJelly(Vector2 c, float size, Color col, float glow, float t, float phase, Vector2 vel) {
    enum { BELL, IN, THR };
    px::Mat M[3] = {px::Ramp(col), px::Glow(Shade(col, 1.3f)), px::Glow(col)};
    Pen P(c.x - size * 2, c.y - size * 1.6f, c.x + size * 2, c.y + size * 3.5f);
    float pulse = 1 + 0.1f * sinf(t * 3 + phase);
    for (int k = 0; k < 5; k++) {
        Vector2 prev{c.x - size * 0.7f + k * size * 0.35f, c.y + size * 0.3f};
        for (int j = 1; j <= 4; j++) { Vector2 q = Add(prev, {sinf(t * 2.2f + k + j * 0.8f + phase) * size * 0.18f - vel.x * 0.02f, size * 0.55f}); P.Limb(prev, q, size * 0.07f, size * 0.05f, THR, 1); prev = q; }
    }
    P.Ball(c, size * pulse, size * 0.75f / pulse, BELL, 10, c.y + size * 0.3f);
    P.Ball({c.x, c.y - size * 0.1f}, size * 0.45f, size * 0.3f, IN, 20);
    P.Draw(M, 3, 0.55f + 0.4f * glow);
}

void PxSpider(Vector2 c, float size, Color body, Color eye, float t, bool splay) {
    enum { BD, LG, EY };
    px::Mat M[3] = {R(body, px::PAT_SPECKLE), R(Shade(body, 0.8f), px::PAT_RINGS, 0.6f), px::Glow(eye)};
    float S = size;
    Pen P(c.x - S * 4, c.y - S * 2.5f, c.x + S * 4, c.y + S * 2.5f);
    for (int k = 0; k < 8; k++) {
        float side = k < 4 ? -1.0f : 1.0f, i = (float)(k % 4), an = side * (0.4f + i * 0.35f) + sinf(t * 1.5f + k) * 0.05f;
        Vector2 knee = splay ? Vector2{c.x + side * S * (1.4f + i * 0.2f), c.y - S * (0.9f - i * 0.3f)} : Vector2{c.x + sinf(an) * S * 1.5f, c.y - cosf(an) * S * 0.9f};
        Vector2 foot = splay ? Vector2{c.x + side * S * (2.6f + i * 0.35f), c.y + S * (0.2f + i * 0.2f)} : Vector2{c.x + sinf(an) * S * 2.4f, c.y + S * 0.8f};
        float z = (k % 2) ? 2.0f : 30.0f;
        P.Limb(c, knee, S * 0.13f + 0.4f, S * 0.1f + 0.3f, LG, z); P.Limb(knee, foot, S * 0.1f + 0.3f, S * 0.05f + 0.3f, LG, z);
    }
    P.Ball({c.x, c.y + S * 0.2f}, S * 0.95f, S * 0.8f, BD, 12);
    P.Ball({c.x, c.y - S * 0.55f}, S * 0.55f, S * 0.45f, BD, 16);
    for (int k = 0; k < 6; k++) P.Dot({c.x - S * 0.3f + (k % 3) * S * 0.3f, c.y - S * (0.7f + (k / 3) * 0.15f)}, EY);
    P.Draw(M, 3);
}
} // namespace

// ================================================================ the biomes
static float Gape(const Beast& b) { return b.act == BeastAct::Strike ? 1.0f : b.act == BeastAct::Coil ? 0.7f : b.act == BeastAct::Eat ? 0.5f * fabsf(sinf(b.phase * 9)) : 0.1f; }

bool PxDrawIslandBeast(const PlatformState& p, const Beast& b, float t) {
    (void)p;
    const SpeciesDef& S = BeastSpecies(PL_ISLAND, b.species);
    float x = b.pos.x, y = b.pos.y, f = b.facing;
    bool coil = b.act == BeastAct::Coil, strike = b.act == BeastAct::Strike;
    switch (b.species) {
    case IS_BOAR: {
        enum { BO, BE, HO, EY, TU, SN };
        px::Mat M[6] = {R({104, 76, 56, 255}, px::PAT_SPECKLE), R({140, 110, 86, 255}), R({44, 32, 28, 255}), coil || strike ? px::Glow({255, 90, 50, 255}) : R({20, 14, 12, 255}), R({236, 228, 208, 255}), R({176, 124, 112, 255})};
        LegSet L = Legs(b, S, -0.3f, 0.7f);
        Pen P(x - 40, y - 34, x + 40, y + 24);
        DrawLegs(P, L, false, 3.2f, BO, HO, 0);
        float low = coil ? 4.0f : 0.0f;
        P.Limb({x - f * 16, y - 2}, {x - f * 22, y - 6 + sinf(t * 9) * 1.5f}, 1.4f, 0.8f, BO, 4);
        P.Ball({x, y - 3}, 16, 10.5f, BO, 10); P.Ball({x, y + 3}, 12, 5, BE, 14);
        for (int k = 0; k < 8; k++) P.Tri({x - f * (10 - k * 3.0f), y - 11}, {x - f * (8 - k * 3.0f), y - 11}, {x - f * (9 - k * 3.0f), y - 16 - (coil || strike ? 3 : 0) - (k % 2)}, HO, 20);
        Vector2 head{x + f * 15, y - 3 + low};
        P.Ball(head, 8, 7, BO, 24);
        P.Limb(head, {head.x + f * 9, head.y + 3}, 4, 3.4f, BO, 26);
        P.Ball({head.x + f * 10, head.y + 3}, 2.4f, 3, SN, 28);
        P.Tri({head.x + f * 5, head.y + 5}, {head.x + f * 11, head.y - 2}, {head.x + f * 6, head.y + 2}, TU, 32, 0, -0.3f);
        P.Tri({head.x - f * 3, head.y - 5}, {head.x - f * 1, head.y - 12}, {head.x + f * 1, head.y - 5}, HO, 30);
        P.Dot({head.x + f * 2, head.y - 3}, EY);
        DrawLegs(P, L, true, 3.2f, BO, HO, 36);
        P.Draw(M, 6);
        return true; }
    case IS_LIZARD: case IS_MSTALKER: {
        bool croc = b.species == IS_MSTALKER;
        float camo = croc && b.act == BeastAct::Ambush ? 0.7f : 0.0f;
        enum { BO, BE, DK, EY, MO, TE };
        px::Mat M[6] = {R(croc ? Color{86, 96, 60, 255} : Color{104, 110, 72, 255}, px::PAT_SCUTES, croc ? 0.9f : 0.6f), R(croc ? Color{150, 150, 100, 255} : Color{160, 160, 110, 255}), R({50, 56, 36, 255}),
                        croc ? px::Glow({255, 200, 60, 255}) : R({20, 16, 12, 255}), R({150, 40, 50, 255}), R({240, 236, 220, 255})};
        Vector2 a = croc ? b.anchor : b.pos, jaw = croc ? b.territory : Vector2{x + f * 16, y - 1};
        float fc = croc ? b.facing : f;
        Pen P(a.x - 80, a.y - 40, a.x + 80, a.y + 30);
        if (!croc) { LegSet L = Legs(b, S, 0.9f, 0.9f); DrawLegs(P, L, false, 2.4f, BO, DK, 0); }
        else for (int k = 0; k < 2; k++) { Vector2 hp{a.x + fc * (k ? 10.0f : -8.0f), a.y + 2}; P.Limb(hp, {hp.x + fc * 4, hp.y + 6}, 2.4f, 2, BO, 1); }
        Vector2 tp[6]; for (int k = 0; k < 6; k++) tp[k] = {a.x - fc * (10 + k * 6.0f), a.y + 1 + sinf(t * 2 + k * 0.8f + b.phase) * (croc ? 0.5f : 1.5f) * k * 0.4f};
        Body(P, tp, 6, croc ? 4.5f : 3.4f, 1.0f, BO, BE, 6, 0.5f);
        P.Ball(a, croc ? 18.0f : 12.0f, croc ? 6.0f : 5.0f, BO, 10);
        Vector2 hdir = Norm(Sub(jaw, a)); if (Len(Sub(jaw, a)) < 2) hdir = {fc, 0};
        Vector2 skull = Add(a, Mul(hdir, croc ? 17.0f : 12.0f));
        P.Ball(skull, croc ? 7.0f : 5.0f, croc ? 4.5f : 3.4f, BO, 18);
        bool open = strike || coil;
        Vector2 tip = Add(skull, Mul(hdir, croc ? 14.0f : 7.0f));
        P.Limb(skull, tip, croc ? 4.0f : 3.0f, croc ? 2.4f : 1.6f, BO, 20);
        if (open) { Vector2 lo = Add(tip, {0, croc ? 7.0f : 4.0f}); P.Tri(skull, tip, lo, MO, 19); P.Limb(skull, lo, croc ? 3.0f : 2.0f, croc ? 2.0f : 1.4f, BO, 21); for (int k = 0; k < 3; k++) P.Dot(Lerp(skull, tip, 0.35f + k * 0.2f), TE); }
        P.Dot(Add(skull, {fc, -3}), EY);
        if (!croc) { LegSet L = Legs(b, S, 0.9f, 0.9f); DrawLegs(P, L, true, 2.4f, BO, DK, 30); }
        else for (int k = 0; k < 2; k++) { Vector2 hp{a.x + fc * (k ? 12.0f : -6.0f), a.y + 3}; P.Limb(hp, {hp.x + fc * 5, hp.y + 6}, 2.6f, 2.2f, BO, 30); }
        P.Draw(M, 6, 1 - camo * 0.6f);
        return true; }
    case IS_SNAKE: {
        float camo = std::clamp(b.special, 0.0f, 1.0f);
        Vector2 pts[SPINE];
        for (int k = 0; k < SPINE; k++) pts[k] = b.spine[k];
        if (coil) for (int k = 1; k < SPINE; k++) { pts[k].x -= f * sinf(k * 1.4f) * 3; pts[k].y += cosf(k * 1.4f) * 4; }
        Eel E; E.back = {80, 132, 58, 255}; E.belly = {190, 196, 110, 255}; E.pat = px::PAT_BANDS; E.patS = 0.8f; E.accent = {40, 34, 20, 255}; E.r0 = 3.4f; E.r1 = 1.2f; E.eye = {240, 200, 40, 255}; E.eyeGlow = true;
        PxEel(pts, SPINE, E, strike || coil ? 1.0f : 0.0f, t, 1 - 0.7f * camo, f);
        return true; }
    case IS_CRAB: PxCrab({x, y - 3}, f, 9, {206, 96, 52, 255}, b.phase, fabsf(b.vel.x) > 5, b.special > 12 ? 1.0f : 0.3f, C_COCONUT, 1, t); return true;
    case IS_GBEETLE: {
        enum { SH, DK, LG, ST, ID, EY };
        px::Mat M[6] = {R({70, 60, 50, 255}, px::PAT_SPECKLE), R({34, 28, 26, 255}), R({50, 42, 38, 255}), R({130, 124, 110, 255}, px::PAT_SCUTES, 1.4f), R({160, 116, 72, 255}), px::Glow({255, 200, 90, 255})};
        float fy = b.anchor.y;
        Pen P(x - 70, fy - 80, x + 80, fy + 6);
        for (int k = 0; k < 6; k++) { float lx = x - 26 + k * 10.4f, lift = fabsf(b.vel.x) > 2 ? std::max(0.0f, sinf(b.phase * 3 + k * 2.1f)) * 3 : 0; Vector2 hip{lx, fy - 18}, foot{lx + f * 5, fy - lift}; Vector2 kn = ik::Knee(hip, foot, 11, 11, -f); float z = k % 2 ? 2.0f : 50.0f; P.Limb(hip, kn, 2.6f, 2.2f, LG, z); P.Limb(kn, foot, 2.2f, 1.2f, LG, z); }
        P.Ball({x, fy - 26}, 40, 17, SH, 20);
        P.Ball({x, fy - 38}, 36, 6, ST, 26, fy - 36);
        for (int k = 0; k < 3; k++) { float ix = x - 20 + k * 20; P.Limb({ix, fy - 42}, {ix, fy - 54}, 3.2f, 3, ID, 40); P.Ball({ix, fy - 56}, 4, 3.4f, ID, 42); P.Dot({ix - 1, fy - 57}, DK); P.Dot({ix + 2, fy - 57}, DK); }
        P.Ball({x + f * 40, fy - 22}, 9, 7, SH, 30);
        P.Limb({x + f * 44, fy - 26}, {x + f * 56, fy - 38}, 3.2f, 1.2f, DK, 34);
        P.Dot({x + f * 45, fy - 23}, EY);
        P.Draw(M, 6);
        return true; }
    case IS_FROG: {
        Color cols[3] = {{40, 160, 220, 255}, {240, 200, 40, 255}, {230, 70, 50, 255}};
        enum { BO, SP, EY };
        px::Mat M[3] = {R(cols[b.id % 3], px::PAT_SPOTS, 0.7f), R({20, 18, 22, 255}), R({16, 12, 12, 255})};
        M[0].accent = {20, 18, 22, 255};
        bool air = !b.grounded;
        Pen P(x - 16, y - 14, x + 16, y + 10);
        if (air) P.Limb({x - f * 3, y + 2}, {x - f * 10, y + 5}, 1.6f, 1.0f, BO, 2); else P.Limb({x - f * 3, y + 2}, {x - f * 6, y + 4}, 2.0f, 1.6f, BO, 2);
        P.Ball({x, y}, air ? 7.0f : 5.5f, air ? 3.6f : 4.4f, BO, 6);
        P.Ball({x + f * 3, y - 3}, 2, 2, BO, 9); P.Dot({x + f * 3, y - 4}, EY);
        P.Limb({x + f * 2, y + 2}, {x + f * 4, y + 4}, 1.5f, 1.2f, BO, 12);
        P.Draw(M, 3);
        return true; }
    case IS_DOG: {
        Color coats[3] = {{176, 132, 84, 255}, {120, 90, 60, 255}, {210, 196, 170, 255}};
        enum { BO, BE, DK, EY, MO };
        px::Mat M[5] = {R(coats[b.id % 3]), R(Shade(coats[b.id % 3], 1.25f)), R({50, 36, 28, 255}), R({16, 12, 10, 255}), R({150, 40, 50, 255})};
        LegSet L = Legs(b, S, -0.3f, 0.7f);
        Pen P(x - 34, y - 30, x + 34, y + 22);
        DrawLegs(P, L, false, 2.2f, BO, DK, 0);
        float wag = sinf(t * 12 + b.phase) * 3;
        P.Limb({x - f * 10, y - 6}, {x - f * 16, y - 12 + wag}, 1.8f, 1.0f, BO, 4);
        P.Ball({x, y - 4}, 11, 6.5f, BO, 10); P.Ball({x, y - 1}, 8, 3.5f, BE, 12);
        Vector2 head{x + f * 11, y - 10};
        P.Limb({x + f * 7, y - 6}, head, 3.4f, 3.4f, BO, 14);
        P.Ball(head, 5, 4.4f, BO, 18); P.Limb(head, {head.x + f * 6, head.y + 2}, 2.6f, 2, BO, 20);
        if (strike || coil) P.Tri({head.x + f * 3, head.y + 3}, {head.x + f * 8, head.y + 3}, {head.x + f * 5, head.y + 6}, MO, 21);
        P.Tri({head.x - f * 2, head.y - 3}, {head.x, head.y - 9}, {head.x + f * 2, head.y - 3}, DK, 22);
        P.Dot({head.x + f * 2, head.y - 2}, EY);
        DrawLegs(P, L, true, 2.2f, BO, DK, 30);
        P.Draw(M, 5);
        return true; }
    case IS_BAT: { // a fruit bat: a furry body, big ears, leathery wings on long fingers
        enum { FUR, WING, EY };
        px::Mat M[3] = {R({150, 100, 70, 255}, px::PAT_SPECKLE), R({66, 48, 50, 255}), R({16, 12, 10, 255})};
        bool hanging = (b.act == BeastAct::Rest || b.act == BeastAct::Idle) && Len(b.vel) < 15;
        Pen P(x - 24, y - 16, x + 24, y + 16);
        if (hanging) { P.Ball({x, y}, 4.5f, 7, WING, 6); P.Ball({x, y + 5}, 3, 2.6f, FUR, 8); P.Draw(M, 3); return true; }
        float flap = sinf(t * 18 + b.phase * 3) * 5;
        for (int side = -1; side <= 1; side += 2) {
            Vector2 root{x, y - 1}, el{x + side * 7.0f, y - 4 - flap}, tip{x + side * 15.0f, y - flap * 0.5f};
            P.Tri(root, el, {x + side * 5.0f, y + 3}, WING, side > 0 ? 2.0f : 12.0f); P.Tri(el, tip, {x + side * 9.0f, y + 3}, WING, side > 0 ? 2.0f : 12.0f);
            P.Limb(root, el, 0.8f, 0.6f, FUR, side > 0 ? 3.0f : 13.0f); P.Limb(el, tip, 0.6f, 0.4f, FUR, side > 0 ? 3.0f : 13.0f);
        }
        P.Ball({x, y}, 4, 3.4f, FUR, 8); P.Ball({x + f * 3, y - 2}, 2.6f, 2.4f, FUR, 10);
        P.Tri({x + f * 2, y - 4}, {x + f * 3, y - 8}, {x + f * 4, y - 4}, FUR, 11);
        P.Dot({x + f * 4, y - 2}, EY);
        P.Draw(M, 3);
        return true; }
    default: return false;
    }
}

bool PxDrawCaveBeast(const PlatformState& p, const Beast& b, float t) {
    (void)p;
    const SpeciesDef& S = BeastSpecies(PL_CAVE, b.species);
    float x = b.pos.x, y = b.pos.y, f = b.facing;
    switch (b.species) {
    case CS_CUSK: {
        bool resting = (b.act == BeastAct::Rest || b.act == BeastAct::Idle) && Len(b.vel) < 15;
        Vector2 pts[6];
        for (int k = 0; k < 6; k++) pts[k] = resting ? Vector2{x + sinf(t * 1.5f + k) * 1.0f, y - 6 + k * 3.0f} : b.spine[std::min(k, SPINE - 1)];
        Eel E; E.back = {206, 196, 200, 255}; E.belly = {230, 222, 226, 255}; E.r0 = 3.0f; E.r1 = 1.0f; E.eye = {140, 120, 130, 255}; E.ribbon = true; E.markings = 1; E.mark = {150, 200, 230, 255};
        PxEel(pts, 6, E, Gape(b) * 0.6f, t, 1, f);
        return true; }
    case CS_OLM: case CS_STALKER: {
        bool stalker = b.species == CS_STALKER;
        enum { BO, BE, DK, GI, MO, EYE };
        px::Mat M[6] = {R(stalker ? Color{176, 170, 180, 255} : Color{226, 214, 206, 255}, px::PAT_SPECKLE), R(stalker ? Color{200, 196, 206, 255} : Color{240, 232, 226, 255}), R({120, 110, 120, 255}), R(stalker ? Color{200, 190, 220, 255} : Color{210, 120, 130, 255}), R({90, 20, 30, 255}), R({150, 140, 150, 255})};
        LegSet L = Legs(b, S, 0.9f, 0.9f);
        float L0 = stalker ? 12.0f : 9.0f;
        Pen P(x - L0 * 4, y - 30, x + L0 * 4, y + 20);
        DrawLegs(P, L, false, stalker ? 2.4f : 1.8f, BO, DK, 0);
        Vector2 tp[6]; for (int k = 0; k < 6; k++) tp[k] = {x - f * (L0 * 0.8f + k * L0 * 0.45f), y - 1 + sinf(t * (stalker ? 3.0f : 2.0f) + k * 0.9f + b.phase) * k * 0.7f};
        Body(P, tp, 6, stalker ? 3.8f : 2.8f, 0.8f, BO, BE, 4);
        P.Ball({x, y - 2}, L0, L0 * 0.42f, BO, 8);
        Vector2 head{x + f * L0, y - (stalker ? 4.0f : 2.0f)};
        P.Ball(head, stalker ? 6.0f : 4.6f, stalker ? 4.0f : 3.0f, BO, 14);
        for (int k = 0; k < 3; k++) { Vector2 g0{head.x - f * 3, head.y - 1}; P.Limb(g0, {head.x - f * (7 + k * 1.5f), head.y - (stalker ? 9 : 6) + k * 3 + sinf(t * 3 + k) * 0.8f}, 1.0f, 0.5f, GI, 16); }
        if (b.act == BeastAct::Coil || b.act == BeastAct::Strike) P.Tri({head.x + f * 3, head.y}, {head.x + f * (stalker ? 11 : 9), head.y - 3}, {head.x + f * (stalker ? 11 : 9), head.y + 3}, MO, 18);
        if (!stalker) P.Dot({head.x + f * 2, head.y - 1}, EYE);
        DrawLegs(P, L, true, stalker ? 2.4f : 1.8f, BO, DK, 30);
        P.Draw(M, 6);
        return true; }
    case CS_ISOPOD: PxShrimp({x, y - 1}, f, 13, {150, 140, 128, 255}, false, t, 1, true); return true;
    case CS_GSHRIMP: PxShrimp({x, y}, f, 8, {120, 240, 210, 255}, true, t, 1, false); return true;
    case CS_LOACH: { Fish F; F.back = {190, 176, 160, 255}; F.belly = {220, 210, 196, 255}; F.fin = {170, 156, 140, 255}; F.pat = px::PAT_SPECKLE; F.patS = 0.6f; F.accent = {140, 120, 110, 255}; F.len = 13; F.girth = 2.8f; F.tail = 4.5f; F.kind = F_LOACH; PxFish(b, F, t, 0, 1, b.act == BeastAct::Flee ? 2.0f : 0.7f); return true; }
    case CS_CTORTOISE: PxTortoise({x, b.anchor.y - 42}, f, b.phase, fabsf(b.vel.x) > 2, false, t, true); return true;
    case CS_TREMOR: { // the Tremor Worm bursting up through the floor: a ringed, muscular body and a round maw of teeth
        if (b.act != BeastAct::Strike && b.act != BeastAct::Wander) return false; // (underground: the old drawer shows the floor shuddering)
        enum { BO, LIP, MAW, TE, DIRT };
        px::Mat M[5] = {R({160, 112, 118, 255}, px::PAT_RINGS, 1.4f), R({200, 150, 150, 255}), R({70, 14, 24, 255}), R({240, 236, 220, 255}), R({110, 100, 96, 255}, px::PAT_SPECKLE)};
        Vector2 a = b.anchor, h = b.territory;
        Pen P(std::min(a.x, h.x) - 50, std::min(a.y, h.y) - 50, std::max(a.x, h.x) + 50, a.y + 30);
        Vector2 pts[8]; float rr[8];
        for (int k = 0; k < 8; k++) { float u = k / 7.0f; pts[k] = Add(Lerp({a.x, a.y + 24}, h, u), {sinf(u * 5 + t * 6) * 3 * (1 - u), 0}); rr[k] = 15 - u * 2; }
        P.Chain(pts, 8, rr, BO, 6);
        Vector2 dir = Norm(Sub(h, a));
        P.Ball(h, 15, 15, LIP, 30);
        P.Ball(Add(h, Mul(dir, 2)), 11, 11, MAW, 40);
        for (int k = 0; k < 12; k++) { float an = k * PIf / 6 + t * 0.5f; Vector2 o{h.x + cosf(an) * 11, h.y + sinf(an) * 11}, i{h.x + cosf(an) * 5, h.y + sinf(an) * 5}; P.Tri(Add(o, {cosf(an + 1.57f) * 2, sinf(an + 1.57f) * 2}), Add(o, {cosf(an - 1.57f) * 2, sinf(an - 1.57f) * 2}), i, TE, 50); }
        for (int k = 0; k < 7; k++) P.Ball({a.x - 24 + k * 8.0f, a.y + 2 - (k % 2) * 3.0f}, 4.8f, 3.6f, DIRT, 60); // the floor it burst
        P.Draw(M, 5);
        return true; }
    case CS_ARACHNID: PxSpider({x, y}, 16, {34, 30, 44, 255}, {255, 80, 70, 255}, t, true); return true;
    default: return false;
    }
}

bool PxDrawWeedsBeast(const PlatformState& p, const Beast& b, float t) {
    (void)p;
    float x = b.pos.x, y = b.pos.y, f = b.facing;
    bool strike = b.act == BeastAct::Strike, coil = b.act == BeastAct::Coil, fleeing = b.act == BeastAct::Flee;
    switch (b.species) {
    case WS_BARRACUDA: { Fish F; F.back = {120, 140, 150, 255}; F.belly = {214, 222, 224, 255}; F.fin = {100, 120, 130, 255}; F.pat = px::PAT_BANDS; F.patS = 1.2f; F.accent = {70, 84, 96, 255}; F.len = 24; F.girth = 3.6f; F.tail = 6; F.kind = F_BARRACUDA; PxFish(b, F, t, strike || coil ? 1.0f : 0.1f, 1, fleeing ? 2.0f : 1.0f); return true; }
    case WS_SHARK: { Fish F; F.back = {110, 116, 108, 255}; F.belly = {220, 216, 202, 255}; F.fin = {96, 102, 94, 255}; F.pat = px::PAT_BANDS; F.patS = 1.6f; F.accent = {70, 72, 66, 255}; F.len = 38; F.girth = 7; F.tail = 10; F.kind = F_SHARK; PxFish(b, F, t, Gape(b), 1); return true; }
    case WS_SARDINE: { Fish F; F.back = {150, 176, 196, 255}; F.belly = {235, 240, 248, 255}; F.fin = {140, 160, 180, 255}; F.len = 8; F.girth = 1.8f; F.tail = 3; PxFish(b, F, t, 0, 1, 1.5f); return true; }
    case WS_MANATEE: { Fish F; F.back = {120, 116, 110, 255}; F.belly = {164, 158, 150, 255}; F.fin = {104, 100, 96, 255}; F.pat = px::PAT_SPECKLE; F.patS = 1.4f; F.len = 110; F.girth = 22; F.tail = 16; F.kind = F_MANATEE; PxFish(b, F, t, 0, 1, 0.3f); return true; }
    case WS_SEAHORSE: {
        float a = 1.0f - 0.75f * std::clamp(b.special, 0.0f, 1.0f), bob = sinf(t * 2 + b.phase) * 1.2f;
        enum { BO, DK, FN, EY };
        px::Mat M[4] = {R({214, 170, 70, 255}, px::PAT_RINGS, 0.6f), R({150, 100, 40, 255}), R({240, 210, 120, 255}), R({20, 16, 12, 255})};
        Pen P(x - 16, y - 18, x + 16, y + 18);
        Vector2 pts[7]; for (int k = 0; k < 7; k++) { float u = k / 6.0f; pts[k] = {x + sinf(u * 3.4f) * 3.5f * f - (u > 0.7f ? (u - 0.7f) * 12 * f : 0), y - 6 + k * 3.2f + bob}; }
        float rr[7] = {3.6f, 3.4f, 3.0f, 2.5f, 1.9f, 1.4f, 1.0f};
        P.Tri({x - f * 2, y - 3 + bob}, {x - f * (6 + sinf(t * 20) * 1.5f), y - 1 + bob}, {x - f * 2, y + 2 + bob}, FN, 2);
        P.Chain(pts, 7, rr, BO, 6);
        P.Ball({x, y - 9 + bob}, 3.4f, 3, BO, 10); P.Limb({x + f * 2, y - 9 + bob}, {x + f * 8, y - 8 + bob}, 1.4f, 1.1f, BO, 11);
        P.Dot({x + f * 1, y - 10 + bob}, EY);
        P.Draw(M, 4, a);
        return true; }
    case WS_CRAB: PxCrab({x, y - 1}, f, 6, {190, 110, 70, 255}, b.phase, fabsf(b.vel.x) > 4, b.act == BeastAct::Eat ? 0.8f : 0.3f, C_CRAB, 1, t); return true;
    case WS_RAY: { // an electric ray: a spotted disc, its wings rippling, a whip tail
        enum { BO, BE, EY };
        px::Mat M[3] = {R({120, 110, 96, 255}, px::PAT_SPOTS, 0.9f), R({200, 196, 180, 255}), R({20, 16, 12, 255})};
        M[0].accent = {84, 76, 66, 255};
        float wave = sinf(b.phase * 4) * 3;
        Pen P(x - 40, y - 18, x + 40, y + 18);
        P.Limb({x - f * 8, y + 1}, {x - f * 26, y + 3 + sinf(t * 3) * 3}, 1.6f, 0.4f, BO, 1);
        P.Ball({x - f * 18, y + 2}, 3, 1.6f, BO, 2); // the little dorsal fins on the tail
        P.Ball({x, y + 1}, 15, 5.5f, BE, 4);
        P.Ball({x, y - 1}, 16, 6, BO, 8);
        P.Tri({x - 16, y + wave}, {x - 5, y - 4}, {x - 5, y + 3}, BO, 9); P.Tri({x + 16, y - wave}, {x + 5, y - 4}, {x + 5, y + 3}, BO, 9);
        P.Dot({x + f * 4 - 2, y - 5}, EY); P.Dot({x + f * 4 + 2, y - 5}, EY);
        P.Draw(M, 3);
        return true; }
    case WS_HMANTIS: { // the Harpoon Mantis in its burrow: a gaudy shrimp, stalked eyes, the club cocked
        bool cock = coil;
        enum { HOLE, BO, BO2, EYE, CLUB };
        px::Mat M[5] = {R({18, 26, 26, 255}), R({70, 170, 140, 255}, px::PAT_RINGS, 0.7f), R({240, 120, 60, 255}), b.stunT > 0 ? R({120, 120, 120, 255}) : px::Glow(cock ? Color{255, 80, 60, 255} : Color{250, 210, 80, 255}), R({230, 90, 110, 255})};
        Vector2 a = b.anchor;
        Pen P(a.x - 30, a.y - 30, a.x + 30, a.y + 16);
        P.Ball(a, 11, 8, HOLE, 2);
        P.Ball({a.x + f * 1, a.y - 1}, 6, 4.5f, BO, 10); P.Ball({a.x + f * 4, a.y - 3}, 4, 3.4f, BO2, 12);
        for (int e = -1; e <= 1; e += 2) { Vector2 s0{a.x + f * 4 + e * 2.0f, a.y - 5}, s1{s0.x + e * 1.5f, s0.y - 5}; P.Limb(s0, s1, 0.8f, 0.8f, BO, 14); P.Ball(s1, 1.8f, 1.8f, EYE, 16); }
        Vector2 cl0{a.x + f * 5, a.y}, cl1 = cock ? Vector2{a.x + f * 2, a.y - 8} : Vector2{a.x + f * 10, a.y + 1};
        P.Limb(cl0, cl1, 1.6f, 1.6f, CLUB, 18); P.Ball(cl1, 2.4f, 2.4f, CLUB, 20);
        P.Draw(M, 5);
        return false; } // (the old drawer adds the tell ring and the harpoon's streak)
    case WS_MERMAN: {
        Vector2 bk = BackDir(b);
        enum { SK, SC, HA, EY, GO, FL };
        px::Mat M[6] = {R({150, 176, 150, 255}), R({56, 110, 110, 255}, px::PAT_SCALES, 0.8f), R({30, 50, 44, 255}), px::Glow({230, 240, 120, 255}), R({210, 176, 80, 255}), R({70, 140, 140, 255})};
        Pen P(x - 50, y - 50, x + 50, y + 50);
        Vector2 tp[6]; for (int k = 0; k < 6; k++) { float u = k / 5.0f, w = sinf(b.phase * 6 - k * 0.9f) * 3.0f * u; tp[k] = {x + bk.x * (3 + k * 4.0f) - bk.y * w, y + bk.y * (3 + k * 4.0f) + 3 + bk.x * w}; }
        float rr[6] = {6.5f, 6, 5, 4, 3, 2};
        P.Chain(tp, 6, rr, SC, 6);
        Vector2 end = tp[5]; P.Tri(end, Add(end, {bk.x * 7 - 5, -7}), Add(end, {bk.x * 7 + 5, 7}), FL, 4);
        Vector2 chest{x - bk.x * 3, y - bk.y * 3 - 2}, head{x - bk.x * 7, y - bk.y * 7 - 6};
        P.Limb({x + bk.x * 2, y + bk.y * 2 + 2}, chest, 5.5f, 6.5f, SK, 10);
        P.Ball(head, 4.4f, 4.6f, SK, 16);
        for (int k = 0; k < 4; k++) P.Limb(head, {head.x + bk.x * (6 + k) + sinf(t * 2 + k) * 1.5f, head.y + bk.y * (6 + k) - 2 + k}, 1.4f, 0.6f, HA, 14);
        P.Dot({head.x - bk.x * 1.8f, head.y - 1}, EY);
        Vector2 sh{chest.x, chest.y - 2}, hand{chest.x - bk.x * (strike ? 11 : coil ? 3 : 7), chest.y - bk.y * 6 + (coil ? -5 : 2)};
        Vector2 el = ik::Knee(sh, hand, 6, 6, f);
        P.Limb(sh, el, 2.2f, 2, SK, 20); P.Limb(el, hand, 2, 1.8f, SK, 20);
        Vector2 tip{hand.x - bk.x * 14, hand.y - bk.y * 14}, butt{hand.x + bk.x * 7, hand.y + bk.y * 7};
        P.Limb(butt, tip, 1.0f, 1.0f, GO, 24);
        for (int k = -1; k <= 1; k++) P.Limb(tip, {tip.x - bk.x * 4 + bk.y * k * 2.5f, tip.y - bk.y * 4 - bk.x * k * 2.5f}, 0.8f, 0.5f, GO, 25);
        P.Draw(M, 6);
        return true; }
    case WS_OCTOSTALKER: {
        bool shown = b.act != BeastAct::Ambush || b.special < 0.5f;
        enum { BO, EY };
        px::Mat M[2] = {shown ? R({190, 150, 90, 255}, px::PAT_BANDS, 1.2f) : R({70, 110, 50, 255}), px::Glow({250, 220, 90, 255})};
        M[0].accent = {110, 76, 50, 255};
        Vector2 a = b.anchor;
        Pen P(a.x - 60, a.y - 30, a.x + 60, a.y + 70);
        for (int k = 0; k < 4; k++) {
            Vector2 end = (strike || b.act == BeastAct::Wander) && k == 0 ? b.territory : Vector2{a.x + (k - 1.5f) * 4.0f + sinf(t * 2 + k) * 3, a.y + 16 + k * 3.0f};
            Vector2 mid = Add(Lerp(a, end, 0.5f), {sinf(t * 3 + k) * 4, 0});
            P.Limb(a, mid, 2.4f, 1.8f, BO, 2.0f + k); P.Limb(mid, end, 1.8f, 0.6f, BO, 2.0f + k);
        }
        P.Ball(a, 7, 10, BO, 10);
        if (shown) { P.Dot({a.x - 2, a.y - 3}, EY); P.Dot({a.x + 2, a.y - 3}, EY); }
        P.Draw(M, 2, shown ? 1.0f : 0.55f);
        return true; }
    default: return false;
    }
}

bool PxDrawAtlantisBeast(const PlatformState& p, const Beast& b, float t) {
    (void)p;
    const SpeciesDef& S = BeastSpecies(PL_ATLANTIS, b.species);
    float x = b.pos.x, y = b.pos.y, f = b.facing;
    bool strike = b.act == BeastAct::Strike, coil = b.act == BeastAct::Coil;
    switch (b.species) {
    case AS_SHRIMP: PxShrimp({x, y}, f, 11, {220, 214, 230, 255}, false, t, 0.8f, false); return true;
    case AS_ANGLER: {
        float camo = std::clamp(b.special, 0.0f, 1.0f);
        Fish F; F.back = {42, 38, 48, 255}; F.belly = {60, 54, 64, 255}; F.fin = {34, 30, 40, 255}; F.pat = px::PAT_SPECKLE; F.patS = 0.8f; F.len = 22; F.girth = 11; F.tail = 8; F.kind = F_ANGLER; F.eye = {220, 230, 140, 255}; F.eyeGlow = true;
        PxFish(b, F, t, Gape(b) * 1.2f, 1 - 0.6f * camo, 0.5f);
        return true; }
    case AS_EEL: {
        Vector2 pts[SPINE];
        for (int k = 0; k < SPINE; k++) pts[k] = b.spine[k];
        if (coil) for (int k = 1; k < SPINE; k++) pts[k].y += sinf(k * 1.3f) * 5;
        Eel E; E.back = {58, 66, 90, 255}; E.belly = {120, 130, 150, 255}; E.pat = px::PAT_SCALES; E.r0 = 6; E.r1 = 2.2f; E.eye = {140, 240, 250, 255}; E.eyeGlow = true; E.ribbon = true; E.markings = 1; E.mark = {140, 240, 250, 255};
        PxEel(pts, SPINE, E, coil ? 0.6f : strike ? 0.5f : 0.15f, t, 1, f);
        return true; }
    case AS_GUARDIAN: PxCrab({x, y - 2}, f, 11, {116, 122, 130, 255}, b.phase, fabsf(b.vel.x) > 5, coil || strike ? 1.0f : 0.3f, C_STONE, 1, t); return true; // (platformer.cpp adds its charge glow and beam)
    case AS_OLEVIATHAN: {
        Fish F; F.back = {62, 72, 100, 255}; F.belly = {126, 134, 156, 255}; F.fin = {54, 62, 88, 255}; F.pat = px::PAT_SPECKLE; F.patS = 2.0f; F.len = 130; F.girth = 22; F.tail = 26; F.kind = F_WHALE; F.eye = {250, 230, 160, 255}; F.eyeGlow = true;
        PxFish(b, F, t, 0, 1, 0.25f);
        { // its crust of glowing Atlantean crystal
            enum { CRY }; px::Mat M[1] = {px::Glow({250, 200, 90, 255})};
            float bob = sinf(t * 0.6f + b.phase) * 2;
            Pen P(x - 80, y - 60, x + 80, y);
            for (int k = 0; k < 8; k++) { float cx = x - 48 + k * 13, ch = 8 + (k * 5 % 4) * 3; P.Tri({cx - 4, y + bob - 18}, {cx + 4, y + bob - 18}, {cx, y + bob - 18 - ch}, CRY, 10); }
            P.Draw(M, 1, 0.6f + 0.4f * sinf(t * 2));
        }
        return true; }
    case AS_LOSTONE: {
        enum { RO, FL, EY, DK };
        px::Mat M[4] = {R({70, 78, 96, 255}, px::PAT_BANDS, 1.5f), R({120, 140, 140, 255}), px::Glow({140, 240, 250, 255}), R({46, 50, 64, 255})};
        M[0].accent = {56, 62, 78, 255};
        LegSet L = Legs(b, S, 0.6f, 0.6f);
        Pen P(x - 30, y - 34, x + 30, y + 20);
        DrawLegs(P, L, false, 2.2f, DK, DK, 0);
        float sway = sinf(t * 1.2f + b.phase) * 1.5f;
        P.Tri({x - 8, y + 5}, {x + 8, y + 5}, {x + sway, y - 13}, RO, 8, 0, -0.2f);
        P.Ball({x + sway * 0.5f, y - 6}, 6, 8, RO, 10);
        for (int k = 0; k < 3; k++) P.Limb({x - 6 + k * 6.0f, y + 4}, {x - 6 + k * 6.0f + sinf(t * 2 + k) * 3, y + 10}, 1.4f, 0.6f, RO, 12);
        Vector2 head{x + f * 3 + sway, y - 14};
        P.Ball({head.x - f * 1, head.y - 1}, 5.2f, 5, RO, 14);
        P.Ball(head, 3.6f, 3.8f, FL, 16);
        P.Dot({head.x + f * 1.5f, head.y - 1}, EY);
        Vector2 sh{x + f * 2 + sway, y - 8}, hand{x + f * 11, y - 4 + sinf(t * 1.5f) * 2};
        Vector2 el = ik::Knee(sh, hand, 6, 6, -f);
        P.Limb(sh, el, 1.8f, 1.6f, RO, 20); P.Limb(el, hand, 1.4f, 1.2f, FL, 20);
        DrawLegs(P, L, true, 2.2f, DK, DK, 24);
        P.Draw(M, 4);
        return true; }
    case AS_SCOURGE: {
        Vector2 pts[15];
        for (int k = 0; k < 15; k++) pts[k] = {b.pos.x - b.facing * k * 22 + sinf(t * 3 - k * 0.6f) * 3, b.pos.y + sinf(t * 2.5f - k * 0.5f) * 18};
        enum { GO, BE, CR, EY, MO, TE };
        px::Mat M[6] = {R({210, 170, 60, 255}, px::PAT_SCALES, 1.6f), R({250, 226, 140, 255}), R({250, 230, 150, 255}), px::Glow({255, 90, 40, 255}), R({90, 20, 30, 255}), R({245, 240, 225, 255})};
        Pen P(b.pos.x - 360, b.pos.y - 70, b.pos.x + 360, b.pos.y + 70);
        Body(P, pts, 15, 18, 6, GO, BE, 8);
        Vector2 h = b.pos;
        P.Ball(h, 28, 19, GO, 30);
        for (int k = 0; k < 5; k++) P.Tri({h.x - 14 + k * 7, h.y - 15}, {h.x - 10 + k * 7, h.y - 15}, {h.x - 12 + k * 7, h.y - 32 - (k % 2) * 6}, CR, 34);
        P.Tri({h.x + b.facing * 18, h.y - 4}, {h.x + b.facing * 44, h.y - 14}, {h.x + b.facing * 44, h.y + 16}, MO, 32);
        for (int k = 0; k < 4; k++) P.Tri({h.x + b.facing * (24 + k * 5), h.y - 6}, {h.x + b.facing * (27 + k * 5), h.y - 6}, {h.x + b.facing * (25 + k * 5), h.y}, TE, 36);
        P.Dot({h.x + b.facing * 10, h.y - 6}, EY); P.Dot({h.x + b.facing * 12, h.y - 6}, EY);
        P.Draw(M, 6);
        return true; }
    default: return false;
    }
}

// ================================================================ the Island's scenery, baked once into textures
// Palms (three sway frames each), jungle trees, stilt huts, stone idols and a totem, the stepped temple, the volcano,
// the altar, fern clumps, a fishing canoe and villagers - modelled like the creatures, rendered once at one texel
// per canvas pixel, then laid out in the parallax layers and tinted through the day (DrawIslandArt).
#include <functional>
#include <vector>
namespace {
struct Baked { RenderTexture2D rt{}; int w = 0, h = 0; };
std::vector<Baked> gIslandArt; // indexed by IslandArt*frame
Baked Bake(int w, int h, const std::function<void(Pen&)>& build, const px::Mat* M, int n) {
    Baked b; b.w = w; b.h = h; b.rt = LoadRenderTexture(w, h);
    SetTextureFilter(b.rt.texture, TEXTURE_FILTER_POINT);
    BeginLayer(b.rt); ClearBackground(BLANK);
    float oldCell = gPxCell, oldA = gPxAlpha; gPxCell = 1; gPxAlpha = 1;
    { Pen P(0, 0, (float)w - 1, (float)h - 1); build(P); P.Draw(M, n); }
    gPxCell = oldCell; gPxAlpha = oldA;
    EndLayer();
    return b;
}
} // namespace

void IslandArtPrepare() {
    if (!gIslandArt.empty()) return;
    // ---- palms: a ringed, curving trunk; seven drooping fronds (three frames of the breeze); coconuts
    for (int v = 0; v < 3; v++) for (int fr = 0; fr < 3; fr++) {
        enum { TRUNK, LEAF, LEAF2, NUT };
        px::Mat M[4] = {R({120, 96, 66, 255}, px::PAT_RINGS, 0.8f), R({64, 128, 58, 255}), R({48, 100, 46, 255}), R({110, 76, 40, 255})};
        int W = 76, H = 100;
        gIslandArt.push_back(Bake(W, H, [&](Pen& P) {
            float lean = (v - 1) * 10.0f, sway = (fr - 1) * 1.6f, hh = 70 + v * 8.0f;
            Vector2 tr[6]; float rr[6];
            for (int k = 0; k < 6; k++) { float u = k / 5.0f; tr[k] = {W / 2.0f + lean * u * u, H - 2 - hh * u}; rr[k] = 3.4f - u * 1.4f; }
            P.Chain(tr, 6, rr, TRUNK, 4);
            Vector2 crown = tr[5];
            for (int k = 0; k < 7; k++) {
                float a = -PIf * 0.95f + k * PIf * 0.95f / 6 + sway * 0.05f, len = 22 + (k % 3) * 4.0f;
                Vector2 pts[5]; float fr2[5];
                for (int j = 0; j < 5; j++) { float u = j / 4.0f; pts[j] = {crown.x + cosf(a) * len * u + sway * u * u, crown.y + sinf(a) * len * u * 0.55f + u * u * 12}; fr2[j] = 2.2f - u * 1.6f; }
                P.Chain(pts, 5, fr2, k % 2 ? LEAF : LEAF2, 10 + (k % 3));
                for (int j = 1; j < 5; j++) P.Limb(pts[j], {pts[j].x + (j % 2 ? 2.0f : -2.0f), pts[j].y + 3}, 0.6f, 0.3f, LEAF2, 9); // the leaflets
            }
            for (int k = 0; k < 3; k++) P.Ball({crown.x - 2 + k * 2.0f, crown.y + 3 + (k % 2)}, 1.8f, 1.8f, NUT, 14);
        }, M, 4));
    }
    // ---- jungle trees: a buttressed trunk and a heaped canopy of leaf masses
    for (int v = 0; v < 2; v++) {
        enum { BARK, LEAF, LEAF2, LEAF3 };
        px::Mat M[4] = {R({92, 72, 52, 255}, px::PAT_BANDS, 0.6f), R(v ? Color{58, 110, 56, 255} : Color{70, 124, 52, 255}, px::PAT_SPECKLE), R(v ? Color{44, 88, 48, 255} : Color{52, 100, 44, 255}, px::PAT_SPECKLE), R({90, 150, 70, 255}, px::PAT_SPECKLE)};
        int W = 110, H = 130;
        gIslandArt.push_back(Bake(W, H, [&](Pen& P) {
            P.Limb({W / 2.0f, H - 2.0f}, {W / 2.0f + 3, H - 70.0f}, 6, 4, BARK, 4);
            P.Limb({W / 2.0f - 12, H - 2.0f}, {W / 2.0f - 2, H - 22.0f}, 3, 1, BARK, 3); P.Limb({W / 2.0f + 13, H - 2.0f}, {W / 2.0f + 3, H - 20.0f}, 3, 1, BARK, 3); // buttress roots
            const float C[9][4] = {{0, -78, 26, 18}, {-26, -70, 20, 15}, {26, -72, 21, 16}, {-14, -94, 20, 15}, {16, -96, 19, 14}, {0, -108, 16, 12}, {-34, -86, 12, 10}, {34, -88, 12, 10}, {0, -64, 22, 10}};
            for (int k = 0; k < 9; k++) P.Ball({W / 2.0f + C[k][0], H + C[k][1]}, C[k][2], C[k][3], k == 8 ? LEAF2 : k == 5 ? LEAF3 : k % 2 ? LEAF : LEAF2, 10 + k);
        }, M, 4));
    }
    // ---- stilt huts: bamboo walls, a layered thatch roof, a doorway, a ladder
    for (int v = 0; v < 2; v++) {
        enum { POST, WALL, THATCH, THATCH2, DARK, LADDER };
        px::Mat M[6] = {R({96, 70, 44, 255}), R({176, 140, 86, 255}, px::PAT_BANDS, 0.5f), R({206, 176, 104, 255}), R({170, 140, 80, 255}), R({30, 22, 18, 255}), R({130, 98, 60, 255})};
        int W = 64, H = 70;
        gIslandArt.push_back(Bake(W, H, [&](Pen& P) {
            float floorY = H - 16.0f, wall = 18 + v * 4.0f;
            for (int k = 0; k < 4; k++) P.Limb({8 + k * 16.0f, H - 2.0f}, {8 + k * 16.0f, floorY}, 1.6f, 1.6f, POST, 2 + (k % 2));
            P.Limb({4, floorY}, {W - 4.0f, floorY}, 2.2f, 2.2f, POST, 6);
            P.Tri({8, floorY - 1}, {W - 8.0f, floorY - 1}, {W - 8.0f, floorY - wall}, WALL, 8, 0, 0); P.Tri({8, floorY - 1}, {8, floorY - wall}, {W - 8.0f, floorY - wall}, WALL, 8, 0, 0);
            P.Tri({W / 2.0f - 5, floorY - 1}, {W / 2.0f + 5, floorY - 1}, {W / 2.0f + 5, floorY - 12}, DARK, 9, 0, 0); P.Tri({W / 2.0f - 5, floorY - 1}, {W / 2.0f - 5, floorY - 12}, {W / 2.0f + 5, floorY - 12}, DARK, 9, 0, 0);
            for (int k = 0; k < 5; k++) { float y = floorY - wall - 2 - k * 5.0f, half = W / 2.0f + 4 - k * 6.0f; P.Limb({W / 2.0f - half, y + 3}, {W / 2.0f + half, y + 3}, 3, 3, k % 2 ? THATCH : THATCH2, 12 + k); } // thatch in layers
            for (int k = 0; k < 4; k++) P.Limb({W / 2.0f - 3, floorY + 2 + k * 4.0f}, {W / 2.0f + 3, floorY + 2 + k * 4.0f}, 0.7f, 0.7f, LADDER, 20);
            P.Limb({W / 2.0f - 3, floorY}, {W / 2.0f - 3, H - 2.0f}, 0.8f, 0.8f, LADDER, 19); P.Limb({W / 2.0f + 3, floorY}, {W / 2.0f + 3, H - 2.0f}, 0.8f, 0.8f, LADDER, 19);
        }, M, 6));
    }
    // ---- a stone idol head on its body, and a carved totem
    for (int v = 0; v < 2; v++) {
        enum { STONE, DARK, MOSS, PAINT };
        px::Mat M[4] = {R({126, 118, 108, 255}, px::PAT_SPECKLE), R({58, 54, 52, 255}), R({78, 118, 60, 255}, px::PAT_SPECKLE), R({170, 60, 40, 255})};
        int W = 40, H = 84;
        gIslandArt.push_back(Bake(W, H, [&](Pen& P) {
            if (v == 0) {
                P.Ball({W / 2.0f, H - 18.0f}, 13, 17, STONE, 4, H - 2.0f);                          // the body, sunk in the ground
                P.Ball({W / 2.0f, H - 48.0f}, 11, 20, STONE, 10);                                   // the long head
                P.Limb({W / 2.0f - 9, H - 58.0f}, {W / 2.0f + 9, H - 58.0f}, 3, 3, STONE, 14);       // the heavy brow
                P.Limb({W / 2.0f + 2, H - 56.0f}, {W / 2.0f + 5, H - 42.0f}, 2.6f, 3.2f, STONE, 16); // the nose
                P.Limb({W / 2.0f - 5, H - 36.0f}, {W / 2.0f + 6, H - 36.0f}, 1.4f, 1.4f, DARK, 15);  // the lips
                P.Ball({W / 2.0f - 4, H - 52.0f}, 2.2f, 1.6f, DARK, 15); P.Ball({W / 2.0f + 5, H - 52.0f}, 2.2f, 1.6f, DARK, 15);
                P.Ball({W / 2.0f, H - 68.0f}, 10, 4, MOSS, 12);                                      // moss on its crown
            } else {
                for (int k = 0; k < 3; k++) { // a carved pole: three stacked faces, square-cut, painted
                    float y1 = H - 2 - k * 22.0f, y0 = y1 - 21, x0 = W / 2.0f - 9, x1 = W / 2.0f + 9;
                    P.Tri({x0, y1}, {x1, y1}, {x1, y0}, STONE, 6 + k, 0, -0.1f); P.Tri({x0, y1}, {x0, y0}, {x1, y0}, STONE, 6 + k, 0, -0.1f);
                    P.Limb({x0, y0 + 1}, {x1, y0 + 1}, 1.2f, 1.2f, DARK, 8 + k);                                            // the cut between the faces
                    P.Ball({W / 2.0f - 4, y0 + 7}, 2.4f, 1.8f, DARK, 12); P.Ball({W / 2.0f + 4, y0 + 7}, 2.4f, 1.8f, DARK, 12); // eyes
                    P.Limb({W / 2.0f, y0 + 8}, {W / 2.0f, y0 + 13}, 1.4f, 2, STONE, 13);                                     // a nose
                    P.Limb({W / 2.0f - 5, y0 + 16}, {W / 2.0f + 5, y0 + 16}, 1.2f, 1.2f, PAINT, 12);                         // a painted mouth
                }
                P.Tri({W / 2.0f - 9, H - 60.0f}, {1, H - 70.0f}, {W / 2.0f - 9, H - 52.0f}, PAINT, 10); P.Tri({W / 2.0f + 9, H - 60.0f}, {W - 1.0f, H - 70.0f}, {W / 2.0f + 9, H - 52.0f}, PAINT, 10); // the carved wings
            }
        }, M, 4));
    }
    // ---- the stepped temple: blocks of stone, a central stair, a shrine on top
    {
        enum { STONE, STONE2, STAIR, DARK, MOSS };
        px::Mat M[5] = {R({138, 130, 112, 255}, px::PAT_SCUTES, 1.2f), R({116, 110, 96, 255}, px::PAT_SCUTES, 1.2f), R({168, 160, 138, 255}, px::PAT_BANDS, 0.4f), R({26, 22, 22, 255}), R({80, 120, 62, 255}, px::PAT_SPECKLE)};
        int W = 180, H = 120;
        gIslandArt.push_back(Bake(W, H, [&](Pen& P) {
            for (int k = 0; k < 5; k++) {
                float half = 84 - k * 14.0f, y1 = H - 2 - k * 18.0f, y0 = y1 - 18;
                P.Tri({W / 2.0f - half, y1}, {W / 2.0f + half, y1}, {W / 2.0f + half, y0}, k % 2 ? STONE : STONE2, 4 + k, 0, -0.1f);
                P.Tri({W / 2.0f - half, y1}, {W / 2.0f - half, y0}, {W / 2.0f + half, y0}, k % 2 ? STONE : STONE2, 4 + k, 0, -0.1f);
                P.Limb({W / 2.0f - half, y0}, {W / 2.0f + half, y0}, 1.2f, 1.2f, STAIR, 5 + k); // the lit lip of the tier
                if (k % 2 == 0) P.Ball({W / 2.0f - half + 8, y0 + 3}, 6, 2.4f, MOSS, 6 + k);
            }
            P.Tri({W / 2.0f - 8, H - 2.0f}, {W / 2.0f + 8, H - 2.0f}, {W / 2.0f + 5, H - 92.0f}, STAIR, 20, 0, -0.2f); P.Tri({W / 2.0f - 8, H - 2.0f}, {W / 2.0f - 5, H - 92.0f}, {W / 2.0f + 5, H - 92.0f}, STAIR, 20, 0, -0.2f);
            P.Tri({W / 2.0f - 12, H - 92.0f}, {W / 2.0f + 12, H - 92.0f}, {W / 2.0f + 12, H - 112.0f}, STONE, 22, 0, 0); P.Tri({W / 2.0f - 12, H - 92.0f}, {W / 2.0f - 12, H - 112.0f}, {W / 2.0f + 12, H - 112.0f}, STONE, 22, 0, 0); // the shrine
            P.Tri({W / 2.0f - 5, H - 92.0f}, {W / 2.0f + 5, H - 92.0f}, {W / 2.0f + 5, H - 104.0f}, DARK, 24, 0, 0); P.Tri({W / 2.0f - 5, H - 92.0f}, {W / 2.0f - 5, H - 104.0f}, {W / 2.0f + 5, H - 104.0f}, DARK, 24, 0, 0);
            P.Tri({W / 2.0f - 15, H - 112.0f}, {W / 2.0f + 15, H - 112.0f}, {W / 2.0f, H - 119.0f}, STONE2, 23, 0, -0.6f);
        }, M, 5));
    }
    // ---- the volcano: two faces of a cone catching the light differently, ridged, with a broken crater rim
    {
        enum { LIT, SHADE, RIDGE, RIM, ASH };
        px::Mat M[5] = {R({112, 92, 84, 255}, px::PAT_SPECKLE), R({70, 58, 60, 255}, px::PAT_SPECKLE), R({52, 42, 44, 255}), R({90, 70, 64, 255}), R({140, 132, 124, 255}, px::PAT_SPECKLE)};
        int W = 340, H = 170;
        gIslandArt.push_back(Bake(W, H, [&](Pen& P) {
            Vector2 peakL{W / 2.0f - 22, 12}, peakR{W / 2.0f + 22, 12};
            P.Tri({2, H - 2.0f}, {W / 2.0f, H - 2.0f}, peakL, LIT, 4, -0.55f, -0.45f); P.Tri({W / 2.0f, H - 2.0f}, peakR, peakL, LIT, 4, -0.2f, -0.5f);
            P.Tri({W / 2.0f, H - 2.0f}, {W - 3.0f, H - 2.0f}, peakR, SHADE, 4, 0.6f, -0.3f);
            for (int k = 0; k < 7; k++) { float x0 = W / 2.0f - 20 + k * 7.0f, x1 = W / 2.0f - 120 + k * 42.0f; P.Limb({x0, 16}, {x1, H - 4.0f}, 1.2f, 2.4f, RIDGE, 6); } // gullies down its flanks
            P.Limb(peakL, peakR, 3, 3, RIM, 8);

        }, M, 5));
    }
    // ---- the altar: a slab on two stones, a skull and offering bowls
    {
        enum { STONE, DARK, BONE, BOWL };
        px::Mat M[4] = {R({130, 124, 112, 255}, px::PAT_SPECKLE), R({40, 34, 32, 255}), R({226, 218, 196, 255}), R({150, 90, 50, 255})};
        gIslandArt.push_back(Bake(56, 30, [&](Pen& P) {
            P.Ball({14, 22}, 7, 7, STONE, 4, 28); P.Ball({42, 22}, 7, 7, STONE, 4, 28);
            P.Limb({6, 15}, {50, 15}, 4, 4, STONE, 8);
            P.Ball({28, 9}, 4, 3.4f, BONE, 12); P.Dot({27, 9}, DARK); P.Dot({29, 9}, DARK);
            P.Ball({16, 10}, 3, 1.6f, BOWL, 11); P.Ball({40, 10}, 3, 1.6f, BOWL, 11);
        }, M, 4));
    }
    // ---- fern clumps and bushes for the jungle floor
    for (int v = 0; v < 2; v++) {
        enum { LEAF, LEAF2, LEAF3 };
        px::Mat M[3] = {R({58, 118, 52, 255}), R({44, 92, 44, 255}), R({86, 148, 64, 255})};
        gIslandArt.push_back(Bake(60, 34, [&](Pen& P) {
            if (v == 0) for (int k = 0; k < 9; k++) { float a = -PIf + k * PIf / 8; Vector2 b0{30, 32}, tip{30 + cosf(a) * 26, 32 + sinf(a) * 22}, mid = Lerp(b0, tip, 0.5f); mid.y -= 5; P.Limb(b0, mid, 2.2f, 1.8f, k % 2 ? LEAF : LEAF2, 4 + k % 3); P.Limb(mid, tip, 1.8f, 0.5f, k % 3 == 0 ? LEAF3 : LEAF, 5 + k % 3); }
            else for (int k = 0; k < 6; k++) P.Ball({10 + k * 8.0f, 26 - (k % 2) * 5.0f}, 9, 8, k % 3 == 0 ? LEAF3 : k % 2 ? LEAF : LEAF2, 4 + k);
        }, M, 3));
    }
    // ---- a fishing canoe with its fisher, and villagers (two poses each)
    {
        enum { WOOD, SKIN, CLOTH, DARK };
        px::Mat M[4] = {R({120, 84, 50, 255}, px::PAT_BANDS, 0.5f), R({150, 100, 70, 255}), R({190, 70, 50, 255}), R({40, 30, 24, 255})};
        gIslandArt.push_back(Bake(44, 22, [&](Pen& P) {
            P.Limb({4, 16}, {40, 16}, 3, 3, WOOD, 4); P.Tri({1, 13}, {6, 13}, {4, 19}, WOOD, 5); P.Tri({38, 13}, {43, 13}, {40, 19}, WOOD, 5);
            P.Limb({22, 13}, {23, 6}, 2.4f, 2, CLOTH, 8); P.Ball({23, 4}, 2.2f, 2.2f, SKIN, 10);
            P.Limb({18, 4}, {30, 18}, 0.6f, 0.6f, DARK, 12);
        }, M, 4));
        for (int v = 0; v < 4; v++) gIslandArt.push_back(Bake(14, 22, [&](Pen& P) {
            float st = (v % 2) ? 2.0f : -2.0f;
            P.Limb({7, 12}, {7 - st, 20}, 1.2f, 1, SKIN, 2); P.Limb({7, 12}, {7 + st, 20}, 1.2f, 1, SKIN, 6);
            P.Limb({7, 12}, {7, 6}, 2.4f, 2, v / 2 ? CLOTH : SKIN, 4); P.Ball({7, 11}, 2.6f, 2, CLOTH, 5);
            P.Ball({7, 4}, 2.2f, 2.2f, SKIN, 8); P.Limb({7, 7}, {10, 10 + st * 0.5f}, 0.9f, 0.8f, SKIN, 9);
        }, M, 4));
    }
}

void DrawIslandArt(int id, Vector2 bottomCentre, Color tint, bool flip) {
    if (id < 0 || id >= (int)gIslandArt.size()) return;
    const Baked& b = gIslandArt[id];
    Rectangle src{0, 0, (float)(flip ? -b.w : b.w), (float)-b.h};
    DrawTextureRec(b.rt.texture, src, {floorf(bottomCentre.x - b.w / 2.0f), floorf(bottomCentre.y - b.h)}, tint);
}