// Shaded pixel sprites (see pixelart.h).
#include "pixelart.h"
#include <algorithm>
#include <cmath>

namespace px {

static unsigned char C8(float v) { return (unsigned char)std::clamp(v, 0.0f, 255.0f); }
static Color Mix(Color a, Color b, float t) { return {C8(a.r + (b.r - a.r) * t), C8(a.g + (b.g - a.g) * t), C8(a.b + (b.b - a.b) * t), 255}; }
static Color Mul(Color a, float k) { return {C8(a.r * k), C8(a.g * k), C8(a.b * k), 255}; }

Mat Ramp(Color base, int pattern, float patScale) {
    Mat m;
    m.ramp[0] = Mix(Mul(base, 0.36f), Color{34, 20, 58, 255}, 0.35f); // shadows lean cool and purple
    m.ramp[1] = Mix(Mul(base, 0.66f), Color{50, 34, 70, 255}, 0.12f);
    m.ramp[2] = base;
    m.ramp[3] = Mix(Mul(base, 1.22f), Color{255, 240, 196, 255}, 0.22f); // lights lean warm
    m.pattern = pattern; m.patScale = patScale;
    return m;
}
Mat Glow(Color c) { Mat m = Ramp(c); m.emissive = true; return m; }

void Canvas::Begin(int W, int H) {
    w = W; h = H;
    mat.assign(w * h, -1); z.assign(w * h, -1e9f); nx.assign(w * h, 0); ny.assign(w * h, 0); u.assign(w * h, 0); v.assign(w * h, 0);
}
void Canvas::Put(int x, int y, int m, float zz, float nxv, float nyv, float uu, float vv) {
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    int i = y * w + x;
    if (zz <= z[i]) return;
    z[i] = zz; mat[i] = (signed char)m; nx[i] = nxv; ny[i] = nyv; u[i] = uu; v[i] = vv;
}
void Canvas::Ball(float cx, float cy, float rx, float ry, int m, float z0, float rz, float yClip) {
    if (rx <= 0 || ry <= 0) return;
    if (rz < 0) rz = std::min(rx, ry);
    int x0 = (int)floorf(cx - rx), x1 = (int)ceilf(cx + rx), y0 = (int)floorf(cy - ry), y1 = (int)ceilf(cy + ry);
    for (int y = y0; y <= y1; y++) {
        if (yClip > 0 && y > yClip) break;
        for (int x = x0; x <= x1; x++) {
            float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry, d2 = dx * dx + dy * dy;
            if (d2 > 1) continue;
            float nz = sqrtf(1 - d2);
            Put(x, y, m, z0 + nz * rz, dx * 0.95f, dy * 0.95f, x + 0.5f - cx, y + 0.5f - cy);
        }
    }
}
void Canvas::Limb(Vector2 a, Vector2 b, float ra, float rb, int m, float z0, float uOffset) {
    float dx = b.x - a.x, dy = b.y - a.y, len2 = dx * dx + dy * dy, len = sqrtf(std::max(1e-4f, len2));
    float rmax = std::max(ra, rb);
    int x0 = (int)floorf(std::min(a.x, b.x) - rmax), x1 = (int)ceilf(std::max(a.x, b.x) + rmax);
    int y0 = (int)floorf(std::min(a.y, b.y) - rmax), y1 = (int)ceilf(std::max(a.y, b.y) + rmax);
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        float px = x + 0.5f - a.x, py = y + 0.5f - a.y;
        float t = std::clamp((px * dx + py * dy) / std::max(1e-4f, len2), 0.0f, 1.0f);
        float qx = px - dx * t, qy = py - dy * t, d = sqrtf(qx * qx + qy * qy), r = ra + (rb - ra) * t;
        if (d > r || r <= 0) continue;
        float nxv = qx / r, nyv = qy / r, nz = sqrtf(std::max(0.0f, 1 - (d / r) * (d / r)));
        float side = (qx * -dy + qy * dx) >= 0 ? 1.0f : -1.0f; // which side of the axis: the pattern's across-coordinate
        Put(x, y, m, z0 + nz * r, nxv * 0.95f, nyv * 0.95f, uOffset + t * len, side * d);
    }
}
void Canvas::Chain(const Vector2* pts, int n, const float* radii, int m, float z0) {
    float along = 0;
    for (int k = 0; k + 1 < n; k++) {
        Limb(pts[k], pts[k + 1], radii[k], radii[k + 1], m, z0, along);
        along += sqrtf((pts[k + 1].x - pts[k].x) * (pts[k + 1].x - pts[k].x) + (pts[k + 1].y - pts[k].y) * (pts[k + 1].y - pts[k].y));
    }
}
void Canvas::Tri(Vector2 a, Vector2 b, Vector2 c, int m, float z0, float nxv, float nyv) {
    int x0 = (int)floorf(std::min({a.x, b.x, c.x})), x1 = (int)ceilf(std::max({a.x, b.x, c.x}));
    int y0 = (int)floorf(std::min({a.y, b.y, c.y})), y1 = (int)ceilf(std::max({a.y, b.y, c.y}));
    auto edge = [](Vector2 p, Vector2 q, float x, float y) { return (q.x - p.x) * (y - p.y) - (q.y - p.y) * (x - p.x); };
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        float fx = x + 0.5f, fy = y + 0.5f;
        float e0 = edge(a, b, fx, fy), e1 = edge(b, c, fx, fy), e2 = edge(c, a, fx, fy);
        if ((e0 >= 0 && e1 >= 0 && e2 >= 0) || (e0 <= 0 && e1 <= 0 && e2 <= 0)) Put(x, y, m, z0, nxv, nyv, fx, fy);
    }
}
void Canvas::Dot(int x, int y, int m, float z0) { Put(x, y, m, z0, 0, 0); }

static float Hash(int a, int b) { unsigned h = (unsigned)(a * 73856093) ^ (unsigned)(b * 19349663); h ^= h >> 13; h *= 0x5bd1e995; h ^= h >> 15; return (h & 0xFFFF) / 65535.0f; }
static float Frac(float x) { return x - floorf(x); }

// 1 where the pattern darkens (or takes the accent colour), 0 elsewhere
static int PatternAt(const Mat& m, float uu, float vv, int x, int y) {
    float c = std::max(1.0f, m.patScale);
    switch (m.pattern) {
    case PAT_SCALES: { float s = 3 * c; int iu = (int)floorf(uu / s); float fu = Frac(uu / s), fv = Frac(vv / s + (iu & 1) * 0.5f); return (fu < 0.28f || fv < 0.22f) ? 1 : 0; } // staggered plates: at the art grid's size they read as scales
    case PAT_BANDS: return Frac(uu / (4 * c)) < 0.34f ? 1 : 0;
    case PAT_RINGS: return Frac(uu / (3 * c)) < 0.22f ? 1 : 0;
    case PAT_SPOTS: { float s = 4 * c; int iu = (int)floorf(uu / s), iv = (int)floorf(vv / s); if (Hash(iu, iv) > 0.45f) return 0; float fu = Frac(uu / s) - 0.5f, fv = Frac(vv / s) - 0.5f; return fu * fu + fv * fv < 0.1f ? 1 : 0; }
    case PAT_SCUTES: { float s = 6 * c; int iu = (int)floorf(uu / s); float fu = Frac(uu / s), fv = Frac(vv / s + (iu & 1) * 0.5f); return (fu < 0.12f || fu > 0.94f || fv < 0.1f) ? 1 : 0; }
    case PAT_SPECKLE: return Hash(x * 3 + 1, y * 7 + 2) < 0.13f ? 1 : 0;
    default: return 0;
    }
}

void Render(const Canvas& cv, const Mat* mats, int nMats, Vector2 world, float cell, bool flipX, float alpha, Color ink) {
    static const float BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
    const float Lx = -0.55f, Ly = -0.62f, Lz = 0.56f; // light from the upper left, a little in front
    float ox = world.x, oy = world.y, ic = cell; // (the caller lines the origin up on the art grid; cell may be fractional under a size transform)
    unsigned char A = (unsigned char)(255 * std::clamp(alpha, 0.0f, 1.0f));
    auto filled = [&](int x, int y) { return x >= 0 && y >= 0 && x < cv.w && y < cv.h && cv.mat[y * cv.w + x] >= 0; };
    for (int y = 0; y < cv.h; y++) for (int x = 0; x < cv.w; x++) {
        int i = y * cv.w + x;
        float sx = ox + (flipX ? (cv.w - 1 - x) : x) * ic, sy = oy + y * ic;
        int m = cv.mat[i];
        if (m < 0) { // the ink outline round the silhouette
            bool edge = false;
            for (int d = 0; d < 4 && !edge; d++) { int qx = x + (d == 0) - (d == 1), qy = y + (d == 2) - (d == 3); if (filled(qx, qy) && cv.mat[qy * cv.w + qx] < nMats && !mats[cv.mat[qy * cv.w + qx]].emissive) edge = true; }
            if (edge) DrawRectangleRec({sx, sy, ic, ic}, Color{ink.r, ink.g, ink.b, A});
            continue;
        }
        if (m >= nMats) continue;
        const Mat& M = mats[m];
        int band;
        if (M.emissive) band = 3;
        else {
            float nxv = flipX ? -cv.nx[i] : cv.nx[i], nyv = cv.ny[i], nz = sqrtf(std::max(0.0f, 1 - nxv * nxv - nyv * nyv));
            float l = nxv * Lx + nyv * Ly + nz * Lz;          // -1 .. 1
            float s = 0.5f + 0.62f * l;                        // with a little ambient
            s += (BAYER[y & 3][x & 3] / 16.0f - 0.47f) * 0.28f; // dither the band edges
            band = std::clamp((int)floorf(s * 4), 0, 3);
            // a part passing behind a nearer one gets a dark contour along the join
            for (int d = 0; d < 4; d++) { int qx = x + (d == 0) - (d == 1), qy = y + (d == 2) - (d == 3); if (filled(qx, qy) && cv.z[qy * cv.w + qx] > cv.z[i] + 2.2f) { band = std::max(0, band - 2); break; } }
            if (M.pattern != PAT_NONE && PatternAt(M, cv.u[i], cv.v[i], x, y)) { if (M.accent.a == 0) band = std::max(0, band - 1); }
        }
        Color c = M.ramp[band];
        if (!M.emissive && M.pattern != PAT_NONE && M.accent.a > 0 && PatternAt(M, cv.u[i], cv.v[i], x, y)) c = Mul(M.accent, 0.62f + 0.14f * band); // the accent is shaded too
        c.a = A;
        DrawRectangleRec({sx, sy, ic, ic}, c);
    }
}

} // namespace px
