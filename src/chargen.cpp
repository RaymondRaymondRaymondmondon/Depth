// ============================================================================
//  DEPTH - a one-off developer tool: paints real PNG body parts for a skeletal character (currently just the
//  Siren, as a pilot) using the same technique as the Flats card creatures (flats_art.cpp's Paint): a small
//  hand-authored concept grid, EPX-upscaled, lit from the upper-left, ink-outlined and dithered into a proper
//  texture - except here the result is exported to disk as a PNG instead of uploaded straight to the GPU, so
//  the game's existing skeletal renderer (sprite_renderer.h) can load it like any other painted asset.
//
//  This exists because there is no actual digital-painting capability available to author these assets by hand;
//  it is the closest approximation to "painted parts on a skeleton" that can be produced procedurally. Run with
//  `depth.exe --gen-siren-art` (no window needed) to (re)generate assets/characters/siren/*.png and skeleton.txt.
// ============================================================================
#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace {
using FVec = std::vector<float>;

void BoxBlur(FVec& v, int W, int H, int r, int passes) {
    FVec tmp(v.size());
    for (int p = 0; p < passes; p++) {
        for (int y = 0; y < H; y++) {
            float sum = 0;
            for (int x = -r; x <= r; x++) if (x >= 0 && x < W) sum += v[y * W + x];
            for (int x = 0; x < W; x++) {
                tmp[y * W + x] = sum / (2 * r + 1);
                int add = x + r + 1, rem = x - r;
                if (add < W) sum += v[y * W + add];
                if (rem >= 0) sum -= v[y * W + rem];
            }
        }
        for (int x = 0; x < W; x++) {
            float sum = 0;
            for (int y = -r; y <= r; y++) if (y >= 0 && y < H) sum += tmp[y * W + x];
            for (int y = 0; y < H; y++) {
                v[y * W + x] = sum / (2 * r + 1);
                int add = y + r + 1, rem = y - r;
                if (add < H) sum += tmp[add * W + x];
                if (rem >= 0) sum -= tmp[rem * W + x];
            }
        }
    }
}
float SStep(float a, float b, float x) { float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3 - 2 * t); }
float Hash2(int x, int y) { unsigned v = (unsigned)(x * 73856093 ^ y * 19349663); v ^= v >> 13; v *= 1274126177u; v ^= v >> 16; return (v & 1023) / 1023.0f; }

// The Siren's palette: skin, dark hair, a coral-pink bodice, resonant-tube brass, pearl, tail scale teal, ink.
// The painting pipeline is value-driven (it dithers by luminance, not hue), so materials need real brightness
// separation to read as distinct parts, not just different colors at the same lightness: hair goes near-black,
// skin stays bright, the bodice sits at a clearly darker mid-tone between them.
Color SirenPal(char c) {
    switch (c) {
        case 's': return {230, 192, 160, 255};
        case 'S': return {184, 144, 112, 255};
        case 'h': return {24, 19, 23, 255};
        case 'p': return {176, 62, 80, 255};
        case 'P': return {130, 42, 58, 255};
        case 'c': return {216, 174, 96, 255};
        case 'w': return {242, 238, 230, 255};
        case 'k': return {22, 18, 16, 255};
        case 't': return {68, 134, 138, 255};
        case 'T': return {34, 86, 90, 255};
        case 'g': return {156, 156, 164, 255};
        case 'G': return {90, 90, 100, 255};
        default: return {0, 0, 0, 0};
    }
}

constexpr int SC = 3, PADC = 4;

// Builds the lit, inked, dithered CPU-side Image for one concept grid. Adapted from flats_art.cpp's Paint(),
// stopping short of the GPU upload so the pixels can be exported straight to disk.
Image PaintImage(const std::vector<std::string>& rowsIn, const std::function<Color(char)>& pal) {
    std::vector<std::string> g0;
    size_t w0max = 4; for (auto& row : rowsIn) w0max = std::max(w0max, row.size());
    for (auto& row : rowsIn) { std::string s = row; s.resize(w0max, '.'); g0.push_back(s); }
    for (int pass = 0; pass < 2; pass++) {
        int h0 = (int)g0.size(), w0 = (int)g0[0].size();
        std::vector<std::string> g1(h0 * 2, std::string(w0 * 2, '.'));
        auto at = [&](int x, int y) { return (x < 0 || y < 0 || x >= w0 || y >= h0) ? '.' : g0[y][x]; };
        for (int y = 0; y < h0; y++) for (int x = 0; x < w0; x++) {
            char P = g0[y][x], A = at(x, y - 1), B = at(x + 1, y), C = at(x - 1, y), D = at(x, y + 1);
            char p1 = P, p2 = P, p3 = P, p4 = P;
            if (C == A && C != D && A != B) p1 = A;
            if (A == B && A != C && B != D) p2 = B;
            if (D == C && D != B && C != A) p3 = C;
            if (B == D && B != A && D != C) p4 = D;
            g1[y * 2][x * 2] = p1; g1[y * 2][x * 2 + 1] = p2; g1[y * 2 + 1][x * 2] = p3; g1[y * 2 + 1][x * 2 + 1] = p4;
        }
        g0 = g1;
    }
    int rows = (int)g0.size(), cols = (int)g0[0].size(), gw = cols + 2 * PADC, gh = rows + 2 * PADC, W = gw * SC, H = gh * SC;
    std::vector<float> m(gw * gh, 0.0f), cr(gw * gh, 0.0f), cg(gw * gh, 0.0f), cb(gw * gh, 0.0f);
    for (int y = 0; y < rows; y++) for (int x = 0; x < cols; x++) {
        char ch = g0[y][x];
        if (ch == '.') continue;
        Color c = pal(ch);
        int i = (y + PADC) * gw + x + PADC;
        m[i] = 1; cr[i] = c.r; cg[i] = c.g; cb[i] = c.b;
    }
    FVec a1(W * H), R(W * H), G(W * H), B(W * H);
    for (int py = 0; py < H; py++) for (int px = 0; px < W; px++) {
        float fx = (px + 0.5f) / SC - 0.5f, fy = (py + 0.5f) / SC - 0.5f;
        int ix = (int)floorf(fx), iy = (int)floorf(fy);
        float tx = fx - ix, ty = fy - iy, asum = 0, rs = 0, gs = 0, bs = 0;
        for (int dy = 0; dy < 2; dy++) for (int dx = 0; dx < 2; dx++) {
            int cx = std::clamp(ix + dx, 0, gw - 1), cy = std::clamp(iy + dy, 0, gh - 1);
            float wgt = (dx ? tx : 1 - tx) * (dy ? ty : 1 - ty);
            int i = cy * gw + cx;
            asum += wgt * m[i]; rs += wgt * cr[i]; gs += wgt * cg[i]; bs += wgt * cb[i];
        }
        int o = py * W + px;
        a1[o] = asum;
        if (asum > 1e-4f) { R[o] = rs / asum; G[o] = gs / asum; B[o] = bs / asum; }
    }
    FVec Rb = R, Gb = G, Bb = B, wgtv = a1;
    for (size_t i = 0; i < Rb.size(); i++) { Rb[i] *= a1[i]; Gb[i] *= a1[i]; Bb[i] *= a1[i]; }
    BoxBlur(Rb, W, H, 1, 1); BoxBlur(Gb, W, H, 1, 1); BoxBlur(Bb, W, H, 1, 1); BoxBlur(wgtv, W, H, 1, 1);
    FVec a2 = a1, hgt = a1;
    BoxBlur(a2, W, H, 3, 2);
    BoxBlur(hgt, W, H, 11, 3);
    std::vector<unsigned char> px(W * H * 4, 0);
    const float lx = -0.52f, ly = -0.62f, lz = 0.59f, ll = sqrtf(lx * lx + ly * ly + lz * lz);
    const float Lx = lx / ll, Ly = ly / ll, Lz = lz / ll;
    float hx = Lx, hy = Ly, hz = Lz + 1, hl = sqrtf(hx * hx + hy * hy + hz * hz); hx /= hl; hy /= hl; hz /= hl;
    for (int y = 1; y < H - 1; y++) for (int x = 1; x < W - 1; x++) {
        int o = y * W + x;
        float a = a2[o];
        if (a < 0.10f) continue;
        unsigned char* q = &px[o * 4];
        auto ink = [&](int alpha) { q[0] = 24; q[1] = 17; q[2] = 14; q[3] = (unsigned char)alpha; };
        float wob = (Hash2(x / 2, y / 2) - 0.5f) * 0.10f;
        if (a < 0.47f + wob) { ink((int)(255 * SStep(0.10f, 0.20f, a))); continue; }
        float wv = std::max(wgtv[o], 1e-3f);
        float r = Rb[o] / wv, g = Gb[o] / wv, b = Bb[o] / wv;
        float dhx = hgt[o + 1] - hgt[o - 1], dhy = hgt[o + W] - hgt[o - W], dax = a2[o + 1] - a2[o - 1], day = a2[o + W] - a2[o - W];
        float nx = -(dhx * 30 + dax * 2.6f), ny = -(dhy * 30 + day * 2.6f), nz = 1;
        float nl = sqrtf(nx * nx + ny * ny + nz * nz); nx /= nl; ny /= nl; nz /= nl;
        float diff = std::max(0.0f, nx * Lx + ny * Ly + nz * Lz), spec = powf(std::max(0.0f, nx * hx + ny * hy + nz * hz), 28.0f) * 0.32f;
        float shade = 0.40f + 0.82f * diff;
        shade *= 0.72f + 0.28f * SStep(0.47f, 0.62f, a);
        float lumBase = (0.3f * r + 0.59f * g + 0.11f * b) / 255.0f;
        float v = std::pow(lumBase, 0.55f) * std::clamp(shade, 0.3f, 1.2f);
        {
            auto lum = [&](int i) { return 0.3f * R[i] + 0.59f * G[i] + 0.11f * B[i]; };
            float grad = fabsf(lum(o + 1) - lum(o - 1)) + fabsf(lum(o + W) - lum(o - W));
            if (a1[o] > 0.9f && a1[o + 1] > 0.9f && a1[o - 1] > 0.9f && a1[o + W] > 0.9f && a1[o - W] > 0.9f && grad > 36.0f) { ink(255); continue; }
        }
        static const int BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
        float thr = (BAYER[(y / 3) & 3][(x / 3) & 3] + 0.5f) / 16.0f;
        float d = std::clamp((0.80f - v) / 0.62f, 0.0f, 1.0f);
        bool hatch = v < 0.36f && fmodf((x + y) / 7.0f, 1.0f) < 0.2f;
        if (spec > 0.12f) continue;
        if (hatch || thr < d * 0.55f) ink(255);
        else if (thr < d * 1.1f) { q[0] = 122; q[1] = 88; q[2] = 62; q[3] = 205; }
    }
    Image img{};
    img.width = W; img.height = H; img.mipmaps = 1; img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    img.data = RL_MALLOC((size_t)W * H * 4);
    memcpy(img.data, px.data(), px.size());
    return img;
}

// A soft, un-dithered glow layer for classes whose identity depends on visible colour (e.g. a bioluminescent
// jellyfish): PaintImage above always resolves down to black ink and a fixed brown stipple, by design, to match
// the game's ink-illustration style - which means a bright, saturated palette colour never actually survives it.
// This skips that pipeline entirely: blur the same concept grid's silhouette, tint it the real glow colour, and
// let the renderer's existing Additive blend (BlendKind::Additive, documented for "ethereal glows") composite it
// behind the inked shape, instead of trying to make the dither itself carry colour.
Image PaintGlow(const std::vector<std::string>& rowsIn, Color glowColor) {
    // Match PaintImage's grid dimensions exactly (its two EPX passes double the grid twice, i.e. 4x total) so
    // glow.png lands pixel-for-pixel under body.png at the same attach pivot/scale, instead of drifting off to
    // one side at the wrong size. The smoothing EPX does for edges doesn't matter here - everything gets blurred
    // heavily anyway - so a plain 4x nearest-neighbour blow-up of the mask is enough.
    size_t w0max = 4; for (auto& row : rowsIn) w0max = std::max(w0max, row.size());
    int rows4 = (int)rowsIn.size() * 4, cols4 = (int)w0max * 4;
    int gw = cols4 + 2 * PADC, gh = rows4 + 2 * PADC, W = gw * SC, H = gh * SC;
    std::vector<float> mask(gw * gh, 0.0f);
    for (int y = 0; y < (int)rowsIn.size(); y++)
        for (int x = 0; x < (int)rowsIn[y].size(); x++)
            if (rowsIn[y][x] != '.')
                for (int dy = 0; dy < 4; dy++) for (int dx = 0; dx < 4; dx++)
                    mask[(y * 4 + dy + PADC) * gw + (x * 4 + dx + PADC)] = 1.0f;
    std::vector<float> hi(W * H, 0.0f);
    for (int py = 0; py < H; py++) for (int px = 0; px < W; px++) {
        int gx = std::clamp(px / SC, 0, gw - 1), gy = std::clamp(py / SC, 0, gh - 1);
        hi[py * W + px] = mask[gy * gw + gx];
    }
    // A tight halo, not a fog: earlier this used a much wider blur (radius 6) that bridged the gaps between
    // separate silhouette parts (e.g. neighbouring tentacles) into one glowing blob, erasing exactly the shape
    // definition the ink linework was drawing. Keeping the blur small lets the glow brighten the true silhouette
    // without smearing across the dark gaps that separate its parts.
    std::vector<float> soft = hi, core = hi;
    BoxBlur(soft, W, H, 2, 2);
    BoxBlur(core, W, H, 1, 1);
    std::vector<unsigned char> px(W * H * 4, 0);
    for (int i = 0; i < W * H; i++) {
        float a = std::clamp(soft[i] * 0.45f + core[i] * 0.65f, 0.0f, 1.0f);
        if (a < 0.02f) continue;
        px[i * 4 + 0] = glowColor.r; px[i * 4 + 1] = glowColor.g; px[i * 4 + 2] = glowColor.b;
        px[i * 4 + 3] = (unsigned char)(a * 255);
    }
    Image img{};
    img.width = W; img.height = H; img.mipmaps = 1; img.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    img.data = RL_MALLOC((size_t)W * H * 4);
    memcpy(img.data, px.data(), px.size());
    return img;
}

struct Part { const char* file; std::vector<std::string> rows; };

// The three parts, drawn facing right: a torso/head/arm piece holding her shell-tipped rod, and two tail
// segments that flare into the fluke. Coordinates are deliberately simple first-pass shapes - the pipeline
// is the point of this pilot, not final art polish.
const std::vector<Part>& Parts() {
    static const std::vector<Part> p = {
        {"body.png", {
            "......hhhhhh.......",
            ".....hhhhhhhh......",
            "....hhhhhhhhhh.....",
            "....hssssssssh.....",
            "....hsssssssSh.....",
            "....hssskssSSh.....",
            "....hSssssssSh.....",
            ".....hSssssSh......",
            ".......hSSSh.......",
            "........Sss........",
            "........Sss........",
            ".......ccccccc.....",
            "......pppppppp.....",
            ".....pppppppppp....",
            "....ppppppppppppS..",
            "...ppppppppppppSSg.",
            "...ppppppppppppSSGg",
            "..SppppppppppppS.Gg",
            "..SsppppppppppSs.Gg",
            "..SsppppppppppSs.G.",
            "...SppppppppppS....",
            "....PppppppppP.....",
            "....PPpppppppP.....",
            ".....PPppppppP.....",
            "......PPPPPPP......",
            ".......PPPPP.......",
        }},
        {"tail_upper.png", {
            "....tttttttttt....",
            "...ttttttttttttt...",
            "..tttttttttttttttt..",
            "..TtttktttktttT.....",
            "..TtttttttttttT.....",
            "...TtttttttttT......",
            "...TtttttttttT......",
            "....TttktttT........",
            "....TtttttttT.......",
            ".....TtttttT........",
            ".....TtttttT........",
            "......TtttT.........",
            "......TtttT.........",
            ".......TtT..........",
            ".......TtT..........",
        }},
        {"tail_lower.png", {
            "......TtT..........",
            "......TtT..........",
            ".......tt...........",
            ".......tt...........",
            ".......tt...........",
            "......TttT..........",
            "......TttT..........",
            ".....TtttttT........",
            "....TttttttttT......",
            "...Tttttttttttt.....",
            "kkTtttttttttttTkk...",
            ".kTttttttttttttTk...",
            "..TttttttttttT......",
            "...TtttttttT........",
            "....TtttttT.........",
            ".....TttT...........",
        }},
    };
    return p;
}

// ============================================================================
//  The general-purpose rig (depth.exe --gen-crew-art): the same technique as GenerateSirenArt above, extended
//  from "one hand-authored pilot" to a small silhouette-composition toolkit so the remaining eleven classes can
//  each get their own painted, skeletal parts without hand-typing a full ASCII grid per body. A `Grid` is a
//  canvas of palette letters built from simple primitives (ellipses, tapered "capsule" limbs, filled polygons);
//  it is converted to rows of characters and pushed through the same PaintImage() lighting/ink/dither pass as
//  the Siren's hand-drawn grids. Letters are roles, not fixed colours: every class supplies its own palette map,
//  so the same silhouette code reads as a different uniform for each one (matching each class's existing
//  procedural colours in render.cpp's DrawCrewFigure, so the painted pilot and the procedural fallback agree).
// ============================================================================
struct Grid {
    int w, h;
    std::vector<std::string> rows;
    Grid(int w, int h) : w(w), h(h), rows(h, std::string(w, '.')) {}
    void Set(int x, int y, char c) { if (x >= 0 && x < w && y >= 0 && y < h) rows[y][x] = c; }
    void Ellipse(float cx, float cy, float rx, float ry, char c) {
        rx = std::max(rx, 0.35f); ry = std::max(ry, 0.35f);
        int x0 = (int)floorf(cx - rx - 1), x1 = (int)ceilf(cx + rx + 1), y0 = (int)floorf(cy - ry - 1), y1 = (int)ceilf(cy + ry + 1);
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
            float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry;
            if (dx * dx + dy * dy <= 1.0f) Set(x, y, c);
        }
    }
    void Rect(float x0, float y0, float x1, float y1, char c) {
        for (int y = (int)floorf(y0); y < (int)ceilf(y1); y++) for (int x = (int)floorf(x0); x < (int)ceilf(x1); x++) Set(x, y, c);
    }
    // a tapered limb from (x0,y0) radius r0 to (x1,y1) radius r1: stamped ellipses walking the segment
    void Capsule(float x0, float y0, float x1, float y1, float r0, float r1, char c) {
        float len = std::max(1.0f, (float)std::hypot((double)(x1 - x0), (double)(y1 - y0)));
        int n = (int)(len * 2) + 4;
        for (int i = 0; i <= n; i++) { float t = (float)i / n; Ellipse(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, r0 + (r1 - r0) * t, r0 + (r1 - r0) * t, c); }
    }
    // even-odd fill, one sample per cell centre - good enough for the simple silhouettes here
    void Poly(const std::vector<std::pair<float, float>>& pts, char c) {
        float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
        for (auto& p : pts) { x0 = std::min(x0, p.first); x1 = std::max(x1, p.first); y0 = std::min(y0, p.second); y1 = std::max(y1, p.second); }
        for (int y = (int)floorf(y0); y <= (int)ceilf(y1); y++) for (int x = (int)floorf(x0); x <= (int)ceilf(x1); x++) {
            float px = x + 0.5f, py = y + 0.5f; bool in = false;
            for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
                float xi = pts[i].first, yi = pts[i].second, xj = pts[j].first, yj = pts[j].second;
                if (((yi > py) != (yj > py)) && (px < (xj - xi) * (py - yi) / (yj - yi) + xi)) in = !in;
            }
            if (in) Set(x, y, c);
        }
    }
};

using HeadFn = std::function<void(Grid&, float)>;   // (grid, torso centre x) - drawn after the head, for hats/goggles/crowns
using TorsoFn = std::function<void(Grid&, float)>;  // drawn after the torso, for aprons/straps/collars

// The shared humanoid torso+head+tucked-arms silhouette every biped class is built from. `dome` swaps the
// bare head for a round helmet/dome (Diver, Robot, Octopus) with no face painted (eyes are added by headAcc).
Grid BodyGrid(float headR, float shoulderW, float hipW, float torsoTop, float torsoBot, bool dome, const TorsoFn& torsoAcc, const HeadFn& headAcc) {
    Grid g(24, 30);
    const float cx = 12;
    g.Poly({{cx - shoulderW, torsoTop}, {cx + shoulderW, torsoTop}, {cx + hipW, torsoBot}, {cx - hipW, torsoBot}}, 't');
    g.Poly({{cx + shoulderW * 0.1f, torsoTop}, {cx + shoulderW, torsoTop}, {cx + hipW, torsoBot}, {cx + hipW * 0.15f, torsoBot}}, 'T');
    g.Capsule(cx - shoulderW * 0.82f, torsoTop + 1, cx - shoulderW * 0.62f, torsoBot - 3, 1.7f, 1.3f, 't');
    g.Capsule(cx + shoulderW * 0.82f, torsoTop + 1, cx + shoulderW * 0.62f, torsoBot - 3, 1.7f, 1.3f, 'T');
    g.Ellipse(cx - shoulderW * 0.6f, torsoBot - 2.5f, 1.5f, 1.5f, 'g');
    g.Ellipse(cx + shoulderW * 0.6f, torsoBot - 2.5f, 1.5f, 1.5f, 'G');
    torsoAcc(g, cx);
    float headCy = torsoTop - headR * 0.95f;
    if (dome) {
        g.Ellipse(cx, headCy, headR, headR, 'g');
        g.Ellipse(cx + headR * 0.15f, headCy + headR * 0.08f, headR * 0.6f, headR * 0.6f, 'e');
    } else {
        g.Ellipse(cx, headCy, headR, headR * 1.05f, 's');
        g.Ellipse(cx + headR * 0.32f, headCy + headR * 0.05f, headR * 0.82f, headR * 0.98f, 'S');
        g.Rect(cx - headR, headCy - headR * 0.1f, cx + headR, headCy + headR * 0.2f, 'k'); // eyes lost in the brow's shadow
    }
    headAcc(g, cx);
    return g;
}
Grid LegGrid(float len, float rTop, float rBot) {
    Grid g(10, (int)len + 6);
    g.Capsule(5, 1, 5, len, rTop, rBot, 'l');
    g.Capsule(5.7f, 1, 5.7f, len, rTop * 0.5f, rBot * 0.5f, 'L');
    g.Ellipse(5, len, rBot * 1.15f, rBot * 0.8f, 'o');
    return g;
}
// one segment of a fish tail or a flared skirt; `fluke` adds the finned tip (siren/merman only)
Grid TailSegGrid(float len, float rTop, float rBot, bool fluke) {
    Grid g(20, (int)len + 8);
    g.Capsule(10, 1, 10, len, rTop, rBot, 'l');
    g.Capsule(10.8f, 1, 10.8f, len, rTop * 0.5f, rBot * 0.5f, 'L');
    if (fluke) {
        g.Poly({{10, len - 2}, {1, len + 9}, {9, len + 3}}, 'l');
        g.Poly({{10, len - 2}, {19, len + 9}, {11, len + 3}}, 'L');
    }
    return g;
}

struct ClassSpec {
    std::string className;                 // folder = lowercase(className), spaces kept (matches ClassName()/render.cpp exactly)
    std::map<char, Color> pal;
    std::vector<std::string> body, leg, tailUpper, tailLower;
    enum class Rig { Biped, Tail, Float } rig = Rig::Biped;
    float legAmpIdle = 3.0f, legAmpWalk = 18.0f;
    float tailAmpIdleUp = 7.0f, tailAmpIdleLow = 5.0f, tailAmpWalkUp = 14.0f, tailAmpWalkLow = 11.0f;
    float floatAmp = 4.0f;
    bool hasGlow = false;      // an extra un-dithered glow.png, additive-blended behind body.png (see PaintGlow)
    Color glowColor{0, 0, 0, 0};
    float bodyScale = 0.6f;    // body.png's (and glow.png's) attach scale; classes needing extra screen presence can raise it
};

bool GenerateClassArt(const ClassSpec& spec) {
    std::string folder = spec.className;
    for (auto& ch : folder) ch = (char)tolower((unsigned char)ch);
    std::string dir = "assets/characters/" + folder;
    MakeDirectory("assets"); MakeDirectory("assets/characters"); MakeDirectory(dir.c_str());
    std::map<char, Color> pal = spec.pal;
    std::function<Color(char)> palFn = [pal](char c) { auto it = pal.find(c); return it != pal.end() ? it->second : Color{0, 0, 0, 0}; };
    int bodyH = 0, legH = 0;
    auto write = [&](const char* file, const std::vector<std::string>& rows, int* outH = nullptr) -> bool {
        if (rows.empty()) return true;
        Image img = PaintImage(rows, palFn);
        std::string path = dir + "/" + file;
        bool ok = ExportImage(img, path.c_str());
        printf("chargen: %s %s (%dx%d)\n", path.c_str(), ok ? "written" : "FAILED", img.width, img.height);
        if (outH) *outH = img.height;
        UnloadImage(img);
        return ok;
    };
    bool ok = write("body.png", spec.body, &bodyH);
    if (spec.rig == ClassSpec::Rig::Biped) ok = write("leg.png", spec.leg, &legH) && ok;
    else if (spec.rig == ClassSpec::Rig::Tail) { ok = write("tail_upper.png", spec.tailUpper) && ok; ok = write("tail_lower.png", spec.tailLower) && ok; }
    if (spec.hasGlow) {
        Image img = PaintGlow(spec.body, spec.glowColor);
        std::string path = dir + "/glow.png";
        bool gok = ExportImage(img, path.c_str());
        printf("chargen: %s %s (%dx%d)\n", path.c_str(), gok ? "written" : "FAILED", img.width, img.height);
        UnloadImage(img);
        ok = gok && ok;
    }
    if (!ok) return false;
    // body.png at a fixed 0.6 attach scale is the one piece confirmed to look right (checked visually). The leg
    // grids, though, come out roughly 2.5-3x taller in raw pixels than the body grids for no proportional reason
    // (nothing in how they're authored ties their height to the body's), so the same fixed scale over-sizes them
    // by that same factor. Scale the leg relative to the body's own proven-good pixel-to-unit ratio instead of
    // trusting a second fixed constant. The tail rig (Siren, Merman, Queen, Nurse) already matches the exact
    // bone/scale values verified for the Siren pilot, so it's left as-is.
    float legScale = (legH > 0 && bodyH > 0) ? std::clamp(0.6f * (float)bodyH / legH, 0.05f, 0.9f) : 0.6f;

    std::ofstream f(dir + "/skeleton.txt");
    f << "# generated by chargen.cpp (depth.exe --gen-crew-art) - painted skeletal parts for " << spec.className << "\n";
    f << "bone hip - 0 -84 0 1 1 0\n";
    const float TAU = 6.28318530f;
    if (spec.rig == ClassSpec::Rig::Biped) {
        f << "bone legBack hip -7 -70 0 1 1 78\n";
        f << "bone legFront hip 7 -70 0 1 1 78\n";
        f << "bone body hip 0 0 0 1 1 90\n";
        f << "slot legBack legBack\nslot body hip\nslot legFront legFront\n";
        f << "attach legBack piece characters/" << folder << "/leg.png 0.5 0.04 " << legScale << "\n";
        f << "attach legFront piece characters/" << folder << "/leg.png 0.5 0.04 " << legScale << "\n";
        f << "attach body piece characters/" << folder << "/body.png 0.5 0.92 0.6\n";
        auto anim = [&](const char* name, float dur, float amp) {
            f << "anim " << name << " " << dur << "\n";
            const int N = 8;
            for (int k = 0; k <= N; k++) { float t = dur * k / N; f << "key legBack rot " << t << " " << (amp * sinf(TAU * t / dur)) << "\n"; }
            for (int k = 0; k <= N; k++) { float t = dur * k / N; f << "key legFront rot " << t << " " << (-amp * sinf(TAU * t / dur)) << "\n"; }
        };
        anim("idle", 2.4f, spec.legAmpIdle);
        anim("walk", 0.9f, spec.legAmpWalk);
    } else if (spec.rig == ClassSpec::Rig::Tail) {
        f << "bone tailUpper hip 0 -10 0 1 1 30\n";
        f << "bone tailLower tailUpper 0 32 0 1 1 28\n";
        f << "slot tail_lower tailLower\nslot tail_upper tailUpper\nslot body hip\n";
        f << "attach tail_lower piece characters/" << folder << "/tail_lower.png 0.5 0.03 0.6\n";
        f << "attach tail_upper piece characters/" << folder << "/tail_upper.png 0.5 0.02 0.6\n";
        f << "attach body piece characters/" << folder << "/body.png 0.5 0.94 0.6\n";
        auto anim = [&](const char* name, float dur, float upAmp, float lowAmp) {
            f << "anim " << name << " " << dur << "\n";
            const int N = 8;
            for (int k = 0; k <= N; k++) { float t = dur * k / N; f << "key tailUpper rot " << t << " " << (upAmp * sinf(TAU * t / dur)) << "\n"; }
            for (int k = 0; k <= N; k++) { float t = dur * k / N; f << "key tailLower rot " << t << " " << (lowAmp * sinf(TAU * (t - dur * 0.22f) / dur)) << "\n"; }
        };
        anim("idle", 2.4f, spec.tailAmpIdleUp, spec.tailAmpIdleLow);
        anim("walk", 1.2f, spec.tailAmpWalkUp, spec.tailAmpWalkLow);
    } else { // Float: no legs at all - the whole body drifts, a slow bob and a lazy tilt
        f << "bone body hip 0 0 0 1 1 90\n";
        if (spec.hasGlow) {
            f << "slot glow hip add\n";
            f << "attach glow piece characters/" << folder << "/glow.png 0.5 0.92 " << spec.bodyScale << "\n";
        }
        f << "slot body hip\n";
        f << "attach body piece characters/" << folder << "/body.png 0.5 0.92 " << spec.bodyScale << "\n";
        auto anim = [&](const char* name, float dur, float amp) {
            f << "anim " << name << " " << dur << "\n";
            const int N = 8;
            for (int k = 0; k <= N; k++) { float t = dur * k / N; f << "key hip y " << t << " " << (-84.0f + amp * sinf(TAU * t / dur)) << "\n"; }
            for (int k = 0; k <= N; k++) { float t = dur * k / N; f << "key hip rot " << t << " " << (amp * 0.5f * sinf(TAU * t / dur + 1.0f)) << "\n"; }
        };
        anim("idle", 3.0f, spec.floatAmp);
        anim("walk", 2.0f, spec.floatAmp * 1.4f);
    }
    printf("chargen: %s/skeleton.txt written\n", dir.c_str());
    return true;
}

// One representative skin/hair tone stands in for render.cpp's per-hero random picks (skins[0]/a mid-brown
// hair), matching how SirenPal above hardcodes a single skin tone for her too.
constexpr Color SKIN{226, 186, 152, 255}, SKIN_DK{184, 148, 118, 255}, HAIR{58, 45, 36, 255};

// Builds the eleven remaining classes' specs from render.cpp's DrawCrewFigure colours (top/legs/boots/sleeve/
// glove/trim) and proportions (cw/chh/bulk), so the painted pilot and the procedural fallback agree.
std::vector<ClassSpec> CrewSpecs() {
    std::vector<ClassSpec> out;
    auto base = [](Color top, Color legs, Color boots, Color trim, Color glove) {
        std::map<char, Color> p;
        p['s'] = SKIN; p['S'] = SKIN_DK; p['h'] = HAIR; p['k'] = Color{20, 16, 14, 255}; p['w'] = Color{238, 232, 218, 255};
        p['t'] = top; p['T'] = Color{(unsigned char)(top.r * 0.72f), (unsigned char)(top.g * 0.72f), (unsigned char)(top.b * 0.72f), 255};
        p['g'] = glove; p['G'] = Color{(unsigned char)(glove.r * 0.75f), (unsigned char)(glove.g * 0.75f), (unsigned char)(glove.b * 0.75f), 255};
        p['a'] = trim; p['A'] = Color{(unsigned char)(trim.r * 0.7f), (unsigned char)(trim.g * 0.7f), (unsigned char)(trim.b * 0.7f), 255};
        p['l'] = legs; p['L'] = Color{(unsigned char)(legs.r * 0.72f), (unsigned char)(legs.g * 0.72f), (unsigned char)(legs.b * 0.72f), 255};
        p['o'] = boots; p['e'] = Color{160, 224, 236, 255}; p['r'] = Color{176, 48, 46, 255}; p['R'] = Color{120, 30, 30, 255};
        return p;
    };
    auto noop2 = [](Grid&, float) {};

    { // Nurse: a pale cowl, a long habit-like dress in place of legs, a small red cross
        ClassSpec c; c.className = "Nurse"; c.rig = ClassSpec::Rig::Tail;
        c.pal = base({72, 94, 124, 255}, {230, 226, 214, 255}, {44, 32, 26, 255}, {230, 226, 214, 255}, SKIN);
        TorsoFn cross = [](Grid& g, float cx) { g.Rect(cx - 3.5f, 12.5f, cx + 3.5f, 15.5f, 'r'); g.Rect(cx - 1.5f, 10.5f, cx + 1.5f, 17.5f, 'r'); };
        HeadFn cowl = [](Grid& g, float cx) { g.Poly({{cx - 5.6f, 3}, {cx + 5.6f, 3}, {cx + 4.4f, 10}, {cx - 4.4f, 10}}, 'a'); g.Poly({{cx - 3.4f, 4.5f}, {cx + 3.4f, 4.5f}, {cx + 2.4f, 8}, {cx - 2.4f, 8}}, 's'); };
        c.body = BodyGrid(3.6f, 6.2f, 5.0f, 9, 20, false, cross, cowl).rows;
        c.tailUpper = TailSegGrid(20, 8.5f, 9.5f, false).rows; c.tailLower = TailSegGrid(19, 9.5f, 3.5f, false).rows;
        c.tailAmpIdleUp = 3; c.tailAmpIdleLow = 2; c.tailAmpWalkUp = 6; c.tailAmpWalkLow = 5;
        out.push_back(c);
    }
    { // Diver: a full brass dome helmet, twin tanks hinted at the shoulders
        ClassSpec c; c.className = "Diver"; c.rig = ClassSpec::Rig::Biped;
        c.pal = base({140, 114, 74, 255}, {140, 114, 74, 255}, {96, 90, 84, 255}, Color{176, 140, 60, 255}, {74, 58, 44, 255});
        TorsoFn tanks = [](Grid& g, float cx) { g.Rect(cx - 8.5f, 11, cx - 6, 20, 'a'); g.Rect(cx + 6, 11, cx + 8.5f, 20, 'a'); };
        HeadFn port = [](Grid& g, float cx) { g.Ellipse(cx, 5.6f, 1.7f, 2.1f, 'e'); g.Poly({{cx - 4, 8.6f}, {cx + 4, 8.6f}, {cx + 3.2f, 10.4f}, {cx - 3.2f, 10.4f}}, 'a'); };
        c.body = BodyGrid(4.4f, 7.6f, 5.6f, 9, 23, true, tanks, port).rows;
        c.leg = LegGrid(78, 2.3f, 3.4f).rows; c.legAmpWalk = 16;
        out.push_back(c);
    }
    { // Captain: a peaked cap, epaulettes, a clockwork off-hand hinted in brass
        ClassSpec c; c.className = "Captain"; c.rig = ClassSpec::Rig::Biped;
        c.pal = base({40, 50, 82, 255}, {36, 34, 42, 255}, {44, 32, 26, 255}, Color{190, 150, 60, 255}, {220, 214, 200, 255});
        TorsoFn coat = [](Grid& g, float cx) { for (int i = 0; i < 3; i++) g.Ellipse(cx, 12.5f + i * 3.2f, 0.7f, 0.7f, 'a'); g.Rect(cx - 6.4f, 10, cx - 4, 11.6f, 'a'); g.Rect(cx + 4, 10, cx + 6.4f, 11.6f, 'a'); };
        HeadFn cap = [](Grid& g, float cx) { g.Poly({{cx - 4.6f, 2.4f}, {cx + 4.6f, 2.4f}, {cx + 4, 6.4f}, {cx - 4, 6.4f}}, 'k'); g.Rect(cx - 4.4f, 5.2f, cx + 4.4f, 6.4f, 'a'); g.Poly({{cx - 1, 6.2f}, {cx + 5.6f, 6.6f}, {cx - 1, 7.6f}}, 'k'); };
        c.body = BodyGrid(3.6f, 6.6f, 5.2f, 9, 23, false, coat, cap).rows;
        c.leg = LegGrid(78, 2.2f, 3.1f).rows; c.legAmpWalk = 15;
        out.push_back(c);
    }
    { // Mechanic: a broad welding apron, iron gauntlets, goggles shoved up on the forehead
        ClassSpec c; c.className = "Mechanic"; c.rig = ClassSpec::Rig::Biped;
        c.pal = base({226, 112, 30, 255}, {206, 96, 26, 255}, {60, 56, 52, 255}, Color{200, 198, 192, 255}, {66, 60, 56, 255});
        TorsoFn apron = [](Grid& g, float cx) { g.Poly({{cx - 5.2f, 11}, {cx + 5.2f, 11}, {cx + 4.6f, 22}, {cx - 4.6f, 22}}, 'a'); g.Rect(cx - 0.6f, 11, cx + 0.6f, 21, 'k'); };
        HeadFn goggles = [](Grid& g, float cx) { g.Ellipse(cx - 2.3f, 3, 1.7f, 1.7f, 'e'); g.Ellipse(cx + 2.3f, 3, 1.7f, 1.7f, 'e'); g.Rect(cx - 4.2f, 2.2f, cx + 4.2f, 3.8f, 'k'); };
        c.body = BodyGrid(3.9f, 7.6f, 6.0f, 9, 23, false, apron, goggles).rows;
        c.leg = LegGrid(76, 2.6f, 3.6f).rows; c.legAmpWalk = 14; c.legAmpIdle = 2;
        out.push_back(c);
    }
    { // Whaler: an oilskin collar and a harpoon-gun strap across the chest
        ClassSpec c; c.className = "Whaler"; c.rig = ClassSpec::Rig::Biped;
        c.pal = base({84, 108, 138, 255}, {64, 70, 84, 255}, {92, 62, 36, 255}, Color{126, 88, 50, 255}, {110, 76, 44, 255});
        TorsoFn strap = [](Grid& g, float cx) { g.Poly({{cx - 5.4f, 10}, {cx - 3.4f, 10}, {cx + 4, 21}, {cx + 2, 21}}, 'a'); };
        HeadFn collar = [](Grid& g, float cx) { g.Poly({{cx - 5, 8}, {cx - 2, 6.4f}, {cx - 2, 9.6f}}, 'a'); g.Poly({{cx + 5, 8}, {cx + 2, 6.4f}, {cx + 2, 9.6f}}, 'a'); };
        c.body = BodyGrid(3.9f, 7.2f, 5.6f, 9, 23, false, strap, collar).rows;
        c.leg = LegGrid(77, 2.5f, 3.5f).rows; c.legAmpWalk = 16;
        out.push_back(c);
    }
    { // Stowaway: a slouched hood, a rum bottle silhouette at the hip
        ClassSpec c; c.className = "Stowaway"; c.rig = ClassSpec::Rig::Biped;
        c.pal = base({124, 94, 62, 255}, {84, 72, 52, 255}, {58, 48, 38, 255}, Color{176, 48, 46, 255}, SKIN);
        TorsoFn bottle = [](Grid& g, float cx) { g.Rect(cx + 4.6f, 17, cx + 6.4f, 21.5f, 'r'); g.Rect(cx + 5, 15.5f, cx + 6, 17, 'r'); };
        HeadFn hood = [](Grid& g, float cx) { g.Poly({{cx - 4.8f, 2.6f}, {cx + 3.6f, 2}, {cx + 4.6f, 8}, {cx - 4, 9}}, 'h'); };
        c.body = BodyGrid(3.5f, 6.0f, 5.0f, 10, 22.5f, false, bottle, hood).rows;
        c.leg = LegGrid(74, 2.1f, 3.0f).rows; c.legAmpWalk = 13; c.legAmpIdle = 4;
        out.push_back(c);
    }
    { // Merman: a real fish tail, bare chest, a bioluminescent lure hinted at the temple
        ClassSpec c; c.className = "Merman"; c.rig = ClassSpec::Rig::Tail;
        c.pal = base({34, 168, 176, 255}, {26, 132, 146, 255}, {20, 104, 120, 255}, Color{216, 174, 96, 255}, SKIN);
        c.pal['s'] = Color{58, 150, 150, 255}; c.pal['S'] = Color{40, 116, 118, 255}; // scaled skin, not human
        TorsoFn fins = [](Grid& g, float cx) { g.Poly({{cx - 6.4f, 11}, {cx - 9, 9}, {cx - 7.6f, 13.5f}}, 'l'); g.Poly({{cx + 6.4f, 11}, {cx + 9, 9}, {cx + 7.6f, 13.5f}}, 'l'); };
        HeadFn lure = [](Grid& g, float cx) { g.Ellipse(cx + 4.6f, -0.5f, 1.0f, 1.0f, 'e'); g.Rect(cx + 4.2f, 0.4f, cx + 5.0f, 3.4f, 'k'); };
        c.body = BodyGrid(3.6f, 6.4f, 5.2f, 9, 20, false, fins, lure).rows;
        c.tailUpper = TailSegGrid(20, 8.0f, 6.8f, false).rows; c.tailLower = TailSegGrid(18, 6.8f, 3.6f, true).rows;
        out.push_back(c);
    }
    { // Dethroned Island Queen: a cracked shell crown, a long faded-silk skirt, a tribal staff hinted by a strap
        ClassSpec c; c.className = "Dethroned Island Queen"; c.rig = ClassSpec::Rig::Tail;
        c.pal = base({150, 108, 170, 255}, {92, 70, 112, 255}, {70, 55, 62, 255}, Color{202, 172, 92, 255}, {200, 190, 210, 255});
        c.pal['s'] = Color{150, 106, 76, 255}; c.pal['S'] = Color{116, 80, 56, 255}; // a warmer, sun-weathered skin
        TorsoFn necklace = [](Grid& g, float cx) { for (int i = -2; i <= 2; i++) g.Ellipse(cx + i * 1.6f, 10.2f + fabsf(i) * 0.5f, 0.6f, 0.6f, 'a'); };
        HeadFn crown = [](Grid& g, float cx) { g.Poly({{cx - 4.4f, 4.2f}, {cx - 2.6f, 1}, {cx - 0.8f, 3.6f}, {cx + 0.8f, 0.4f}, {cx + 2.6f, 3.6f}, {cx + 4.4f, 1.2f}, {cx + 4, 5}, {cx - 4, 5}}, 'a'); };
        c.body = BodyGrid(3.7f, 6.4f, 5.4f, 9, 21, false, necklace, crown).rows;
        c.tailUpper = TailSegGrid(21, 9.0f, 10.5f, false).rows; c.tailLower = TailSegGrid(19, 10.5f, 4.5f, false).rows;
        c.tailAmpIdleUp = 4; c.tailAmpIdleLow = 3; c.tailAmpWalkUp = 8; c.tailAmpWalkLow = 6;
        out.push_back(c);
    }
    { // Robot: a boxy iron chassis and a furnace-glow porthole where a chest would be
        ClassSpec c; c.className = "Robot"; c.rig = ClassSpec::Rig::Biped;
        c.pal = base({198, 196, 190, 255}, {150, 146, 140, 255}, {90, 85, 80, 255}, Color{190, 150, 60, 255}, {142, 110, 60, 255});
        TorsoFn furnace = [](Grid& g, float cx) { g.Ellipse(cx, 15, 2.6f, 2.6f, 'e'); g.Ellipse(cx, 15, 1.6f, 1.6f, 'w'); for (int i = -1; i <= 1; i += 2) g.Rect(cx + i * 6.4f, 10, cx + i * 7.4f, 22, 'a'); };
        HeadFn rivets = [](Grid& g, float cx) { g.Rect(cx - 3.4f, 1.6f, cx + 3.4f, 3.0f, 'k'); for (int i = -2; i <= 2; i++) g.Ellipse(cx + i * 1.4f, 7.6f, 0.4f, 0.4f, 'k'); };
        c.body = BodyGrid(4.4f, 7.8f, 6.2f, 9, 23, true, furnace, rivets).rows;
        c.leg = LegGrid(76, 2.9f, 3.9f).rows; c.legAmpWalk = 12; c.legAmpIdle = 2;
        out.push_back(c);
    }
    { // Octopus: a cracked dome, a rubbery mantle, one tentacle reused on both sides for a many-limbed silhouette
        ClassSpec c; c.className = "Octopus"; c.rig = ClassSpec::Rig::Biped;
        c.pal = base({176, 60, 150, 255}, {146, 50, 128, 255}, {120, 40, 108, 255}, Color{232, 214, 226, 255}, {190, 80, 164, 255});
        TorsoFn suckers = [](Grid& g, float cx) { for (int i = 0; i < 3; i++) { g.Ellipse(cx - 5.4f, 13 + i * 3, 0.8f, 0.6f, 'a'); g.Ellipse(cx + 5.4f, 13 + i * 3, 0.8f, 0.6f, 'a'); } };
        HeadFn crack = [](Grid& g, float cx) { g.Poly({{cx - 1, 1}, {cx + 0.6f, 3.4f}, {cx - 0.6f, 4.6f}, {cx + 1.2f, 6.6f}}, 'k'); };
        c.body = BodyGrid(4.6f, 7.4f, 6.6f, 10, 22, true, suckers, crack).rows;
        c.leg = TailSegGrid(70, 3.0f, 1.0f, false).rows; c.legAmpWalk = 22; c.legAmpIdle = 6;
        out.push_back(c);
    }
    { // Wisp: from the reference sheet - a glistening translucent jellyfish bell (no limbs, no face), a small
        // brass steampunk lantern with cogs floating inside it on a chain, and trailing spectral tentacles with
        // bioluminescent command/communication patches. No humanoid silhouette at all.
        ClassSpec c; c.className = "Wisp of the Sea"; c.rig = ClassSpec::Rig::Float; c.floatAmp = 5.0f;
        c.hasGlow = true; c.glowColor = {120, 235, 228, 255};   // the "Radiant Blue and Teal Energy Source" the ink pass can't carry
        c.bodyScale = 0.85f;   // noticeably bigger on screen than the other classes' shared 0.6 - a large, imposing jellyfish
        c.pal['t'] = {110, 205, 208, 255};   // bell: translucent teal-cyan
        c.pal['T'] = {62, 140, 152, 255};    // bell underside shadow
        c.pal['w'] = {225, 255, 250, 255};   // bioluminescent glow (bright)
        c.pal['k'] = {35, 72, 78, 255};      // faint dark patterning under the bell's glow (not a face)
        c.pal['l'] = {198, 158, 78, 255};    // lantern brass
        c.pal['L'] = {142, 106, 48, 255};    // lantern brass, shadowed / cogs
        c.pal['c'] = {58, 52, 45, 255};      // lantern chain and rings

        // Drawn much bolder than the first pass: at the figure's actual on-screen size (tens of pixels, not a
        // zoomed-in debug crop) six thin, closely-spaced tentacles and a fine 3-unit-wide lantern simply vanished
        // into a single glowing smudge - "a mono-colour floating ball dripping something". Fewer, thicker,
        // further-apart parts, a wider flatter dome, and ink slits pre-separating the tentacle roots all read at
        // a glance; fine detail that only shows up zoomed in doesn't count for anything here. Scaled up again (a
        // bigger grid, six tentacles instead of four, plus a larger bodyScale above) for more screen presence.
        const float cx = 22;
        Grid g(44, 50);
        // the bell: wide and flat like a real jellyfish cap, not a round ball - shaded darker underneath so it
        // reads as a hollow glowing form, relit across the crown so the apex stays bright
        g.Ellipse(cx, 13, 17.0f, 11.0f, 't');
        g.Ellipse(cx, 17.0f, 15.5f, 8.0f, 'T');
        g.Ellipse(cx, 9.0f, 15.0f, 8.0f, 't');
        // faint darker patterning inside the glow (echoes the reference's subtle mask-like shading, not real eyes)
        g.Ellipse(cx - 5.5f, 10.0f, 2.2f, 2.8f, 'k');
        g.Ellipse(cx + 5.5f, 10.0f, 2.2f, 2.8f, 'k');
        // ink slits between where each tentacle will start, so the hem reads as gathered/split rather than solid
        for (float gap : {cx - 11.2f, cx - 5.6f, cx, cx + 5.6f, cx + 11.2f}) g.Capsule(gap, 16.5f, gap, 21.5f, 0.75f, 0.4f, 'k');

        // the spectral lantern: much bigger than the first pass so it actually reads as an object, not a speck
        g.Rect(cx - 0.5f, 11.5f, cx + 0.5f, 13.0f, 'c');
        g.Ellipse(cx, 11.8f, 0.85f, 0.85f, 'c');
        g.Rect(cx - 3.6f, 13.0f, cx + 3.6f, 14.8f, 'L');
        g.Rect(cx - 2.8f, 14.8f, cx + 2.8f, 21.5f, 'l');
        g.Ellipse(cx, 18.0f, 1.4f, 2.2f, 'w');
        g.Ellipse(cx - 4.5f, 17.6f, 1.25f, 1.25f, 'L'); g.Ellipse(cx - 4.5f, 17.6f, 0.42f, 0.42f, 'c');
        g.Ellipse(cx + 4.5f, 16.2f, 1.05f, 1.05f, 'L'); g.Ellipse(cx + 4.5f, 16.2f, 0.35f, 0.35f, 'c');

        // six wavy tentacles - thick at the root, real gaps between them, tapering to a point - with a bright
        // bioluminescent patch partway down each one
        struct Tendril { float baseX, amp, phase, rTop, rBot; };
        const Tendril tendrils[] = {
            {cx - 14.0f, 1.9f, 0.0f, 3.2f, 0.8f}, {cx - 8.4f, 2.3f, 1.1f, 2.8f, 0.7f}, {cx - 2.8f, 2.6f, 2.2f, 2.6f, 0.65f},
            {cx + 2.8f, 2.6f, 0.6f, 2.6f, 0.65f}, {cx + 8.4f, 2.3f, 1.7f, 2.8f, 0.7f}, {cx + 14.0f, 1.9f, 2.9f, 3.2f, 0.8f},
        };
        const float y0 = 21, y1 = 48;
        for (const Tendril& td : tendrils) {
            for (int yy = (int)y0; yy <= (int)y1; yy++) {
                float t = (yy - y0) / (y1 - y0);
                float x = td.baseX + td.amp * sinf(t * 4.0f + td.phase);
                float r = td.rTop + (td.rBot - td.rTop) * t;
                g.Ellipse(x, (float)yy, r, r, 't');
                g.Ellipse(x + r * 0.4f, (float)yy, r * 0.5f, r * 0.5f, 'T');
            }
            float t = 0.45f;
            float yy = y0 + t * (y1 - y0);
            float x = td.baseX + td.amp * sinf(t * 4.0f + td.phase);
            g.Ellipse(x, yy, 0.85f, 0.85f, 'w');
        }
        c.body = g.rows;
        out.push_back(c);
    }
    (void)noop2;
    return out;
}

}  // namespace

bool GenerateSirenArt();   // defined below; forward-declared so GenerateAllCrewArt can call it in file order

// depth.exe --gen-crew-art: regenerates the Siren (the pilot) plus the eleven remaining classes.
bool GenerateAllCrewArt() {
    bool ok = GenerateSirenArt();
    for (const ClassSpec& c : CrewSpecs()) ok = GenerateClassArt(c) && ok;
    return ok;
}

bool GenerateSirenArt() {
    const char* dir = "assets/characters/siren";
    // MakeDirectory is recursive-safe in raylib; ignore the result if it already exists.
    MakeDirectory("assets");
    MakeDirectory("assets/characters");
    MakeDirectory(dir);
    for (const Part& part : Parts()) {
        Image img = PaintImage(part.rows, SirenPal);
        std::string path = std::string(dir) + "/" + part.file;
        bool ok = ExportImage(img, path.c_str());
        printf("chargen: %s %s (%dx%d)\n", path.c_str(), ok ? "written" : "FAILED", img.width, img.height);
        UnloadImage(img);
        if (!ok) return false;
    }
    // The rig: a single stable "hip" bone (where the bodice meets the tail, matching where other classes'
    // hips sit above their feet) with the tail hanging below it toward the feet anchor, and the body above it.
    // Two tail bones animate; the body stays still, matching a mermaid's tail-driven swim rather than a stride.
    std::ofstream f(std::string(dir) + "/skeleton.txt");
    f << "# generated by chargen.cpp (depth.exe --gen-siren-art) - a pilot for painted skeletal parts\n";
    f << "bone hip - 0 -84 0 1 1 0\n";
    f << "bone tailUpper hip 0 -10 0 1 1 30\n";
    f << "bone tailLower tailUpper 0 32 0 1 1 28\n";
    f << "bone tip tailLower 0 32 0 1 1 20\n";
    f << "slot tail_lower tailLower\n";
    f << "slot tail_upper tailUpper\n";
    f << "slot body hip\n";
    f << "attach tail_lower piece characters/siren/tail_lower.png 0.5 0.03 0.62\n";
    f << "attach tail_upper piece characters/siren/tail_upper.png 0.5 0.02 0.62\n";
    f << "attach body piece characters/siren/body.png 0.5 0.94 0.62\n";
    auto anim = [&](const char* name, float dur, float upAmp, float lowAmp, float lowLagFrac) {
        f << "anim " << name << " " << dur << "\n";
        const int N = 8;
        for (int k = 0; k <= N; k++) {
            float t = dur * k / N;
            float up = upAmp * sinf(2.0f * 3.14159265f * t / dur);
            f << "key tailUpper rot " << t << " " << up << "\n";
        }
        for (int k = 0; k <= N; k++) {
            float t = dur * k / N;
            float low = lowAmp * sinf(2.0f * 3.14159265f * (t - dur * lowLagFrac) / dur);
            f << "key tailLower rot " << t << " " << low << "\n";
        }
    };
    anim("idle", 2.4f, 10.0f, 8.0f, 0.22f);
    anim("walk", 1.2f, 16.0f, 13.0f, 0.22f);
    f.close();
    printf("chargen: %s/skeleton.txt written\n", dir);
    return true;
}
