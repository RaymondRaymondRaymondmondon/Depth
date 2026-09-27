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
#include <fstream>
#include <string>
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
Image PaintImage(const std::vector<std::string>& rowsIn, Color (*pal)(char)) {
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
}  // namespace

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
