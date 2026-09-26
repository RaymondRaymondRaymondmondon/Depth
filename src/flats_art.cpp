// Pixel-art portraits for the Flats creature cards: one small sprite per card name, drawn as crisp squares with an ink outline.
#include "raylib.h"
#include <algorithm>
#include <cmath>`n#include <cmath>
#include <cstring>
#include <map>`n#include <map>
#include <string>
#include <vector>

namespace {
struct Sprite { const char* name; std::vector<const char*> rows; };

Color Pal(char c) {
    switch (c) {
        case 'w': return {238, 236, 226, 255};
        case 'l': return {192, 198, 206, 255};
        case 'e': return {124, 132, 146, 255};
        case 'a': return {58, 64, 84, 255};
        case 'r': return {206, 62, 52, 255};
        case 'o': return {234, 132, 42, 255};
        case 'y': return {242, 208, 72, 255};
        case 'g': return {84, 172, 92, 255};
        case 'G': return {40, 112, 72, 255};
        case 'b': return {72, 134, 212, 255};
        case 'B': return {40, 72, 142, 255};
        case 'c': return {120, 214, 226, 255};
        case 'p': return {146, 92, 184, 255};
        case 'm': return {236, 142, 164, 255};
        case 'n': return {142, 92, 56, 255};
        case 'N': return {92, 56, 36, 255};
        case 's': return {220, 192, 142, 255};
        case 'k': return {22, 20, 30, 255};
        default: return {0, 0, 0, 0};
    }
}

const std::vector<Sprite>& Sprites() {
    static const std::vector<Sprite> s = {
        {"Minnow", {"....eeee........", "..eelllllle..ee.", ".elllkllllleeee.", ".ellllllllle.ee.", "..eelllllee.....", "....eeee........"}},
        {"Hermit Crab", {".......y........", "......yny.......", ".....nnyyn......", "....nnyynnn...oo", "...nnyyyynnn.oo.", "..nnynnnnynn.o..", "..nnyyyyyynnooo.", "..nnnnnnnnnnoo..", "...nnnnnnnn.oo.o", "....o.o.o.o.o.o."}},
        {"Flying Fish", {"...cc...........", "..cwwc..cc......", ".cwwwwccwwc.....", "cwwwwwwwwwwc....", ".ccwbbbbbbbbb...", "...bbbkbbbbbbbb.", "....lllllllbbbbb", ".....bbbbbb..bb."}},
        {"Anglerfish", {".........y......", "........y.......", ".......y........", "....aaaaaaa.....", "..aaaaaaaaaaa...", ".aaaaywaaaaaaa..", ".aaaaaaaaaaaaaa.", ".aawawawawaaaaa.", "..aaaaaaaaaaaaa.", "....aaaa..aaa..."}},
        {"Pufferfish", {"..o.o..o..o.....", "...oyyyyyyo.....", ".ooyyyyyyyyoo...", "..yyykyyyyyyy...", ".oyyyyyyyyyyyo..", "..yyswwsyyyyyy..", ".oyyyyyyyyyyo.oo", "...oyyyyyyyo.ooo", "..o.o..o..o....."}},
        {"Sea Urchin", {".p...p..p...p...", "..p..p..p..p....", "...pppppppp.....", "pppppkppppppppp.", "...pppppppp.....", "pppppppppppp.pp.", "..p..p..p..p....", ".p...p..p...p..."}},
        {"Clownfish", {"......oo........", "....ooooooo.....", "..ooowwooowwoo..", ".oookwwooowwooo.", ".oooowwooowwoo.o", "..ooowwooowwoooo", "....ooooooo..oo."}},
        {"Fry", {".cccc.b.........", "ckcccbb.........", ".cccc.b.........", "........cccc.b..", "........kcccbb..", "........cccc.b..", "...cccc.b.......", "...kcccbb.......", "...cccc.b......."}},
        {"Ship's Cat", {"..o.o...........", "..ooo...........", ".oooo.....oo....", ".okoko...oooo...", ".ooooo..ooo.....", ".oonoo.oooo.....", "..ooooooooo.....", "..oooooooooo....", "..oo.oo..oo....."}},
        {"Ballast Cask", {"....nnnnnnnn....", "...nnnnnnnnnn...", "..eeeeeeeeeeee..", "..nnnnnnnnnnnn..", "..nnnnnNNnnnnn..", "..nnnnnNNnnnnn..", "..eeeeeeeeeeee..", "...nnnnnnnnnn...", "....nnnnnnnn...."}},
        {"Salvage Diver", {".....yyyy.......", "....yyyyyy......", "...yycccyyy.....", "...ycccccyy.....", "...yycccyyy.....", "....yyyyyy.bbb..", "..bbbbbbbbbbb...", ".bbbbbbbbbbbb...", ".bb.bbbbb.bb....", "..gg.....gg....."}},
        {"Mudskipper", {".....k.k........", "....gggggg......", "..ggggggggggg...", ".ggggnggggggggg.", ".gggggggggggg.gg", "..ggnnggggnnggg.", ".nn.nn.nn.nn..g."}},
        {"Sailfish", {".....bb.........", "....bBBb........", "...bBBBBb.......", ".bbbbbbbbbbb....", "lllllkbbbbbbbb..", "....llllllbbbbbb", ".....bbbbb.bb..."}},
        {"Skeleton Sailor", {"...kkkkkkkk.....", "..kkkkyykkkk....", ".kkkkkkkkkkkk...", "...wwwwwwww.....", "...wkkwwkkw.....", "...wkkwwkkw.....", "....wwkkww......", "..awawawawawa...", ".awawawawawawa..", "..wawawawawaw...", "..a.wa..aw.aw...", "...c.c..c..c...."}},
        {"Stingray", {"......bbbb......", "...bbbbbbbbbb...", ".bbbbkbbbbkbbbb.", "bbbbbbbbbbbbbbbb", ".bbbbbbbbbbbbeee", "...bbbbbbbbbe...", ".....bbbbb.e...."}},
        {"Manta Ray", {".....b....b.....", ".bb.bbb..bbb.bb.", "bbbbbbbbbbbbbbbb", "bbbbbkbbbbkbbbbb", ".bbbbbwwwwbbbbb.", "..bbbbwwwwbbbb..", "....bbbwwbbb....", "......eee.......", ".......e........"}},
        {"Sea Turtle", {"....gggggggg....", "..gGGgGGgGGgg...", ".gGgGGgGGgGGgg..", ".ggGGgGGgGGgggg.", "gggggggggggggg..", "sskgggggggggg.s.", "ss.ss......ss..."}},
        {"Coral Queen", {"..r.r.r.rr.r.r..", "..r.rrrrrr.r.r..", "...rryyyyrrr....", "....mmmmmm......", "...mkmmmmkm.....", "....mmrrmm......", "...rrrrrrrr.....", "..rrrrrrrrrr....", ".rrr.rrrr.rrr..."}},
        {"Crab Sentinel", {".rr..........rr.", "rrrr...ee...rrrr", "rr.r..eeee..r.rr", ".rr.rrrrrrrr.rr.", "...rrkrrrrkrr...", "...rrrrrrrrrr...", "..rrrelllerrr...", ".r.rr.rrrr.rr.r.", "r..r..r..r..r..r"}},
        {"Ghost Crab", {".ww..........ww.", "wwww...cc...wwww", "ww.w..c..c..w.ww", ".ww.wwwwwwww.ww.", "...wwcwwwwcww...", "...wwwwwwwwww...", "..wwwwwwwwwww...", ".w.ww.wwww.ww.w.", "c..c..c..c..c..c"}},
        {"Sea Anemone", {".m.m.mm.m.m.m...", ".m.mm.mm.mm.m...", "..mmmmmmmmmmm...", "...mmmmmmmmm....", "....pppppppp....", "...ppppppppp....", "..eeeeeeeeeeee.."}},
        {"Hammerhead", {"..e.............", "..ee....ee......", "..eee.eeeeee....", "..eekeeeeeeeee..", "..eeeeeeeeeeeeee", "..eeewwwwwweee..", "..ee.eewwweee...", "..e....e....e..."}},
        {"Barracuda", {"............ee..", ".lllllllllllllee", "lllkllllllllllee", "lwwwwllleeeeeee.", ".llllllllll.ee.."}},
        {"Moray Eel", {"...nnnnnnnn.....", "..nnnnnnnnnn....", ".nnnggggggnnn...", ".nngggggggggg...", ".nnnkggggwwwgg..", ".nnnnggggggggg..", "..nnnnnngggg....", "...nnnnnnnn....."}},
        {"Nautilus", {"....ssss........", "...soooosss.....", "..soossooos.....", ".soossoossos....", ".soosoooosos....", ".sooossssoos....", "..soooooooss....", "...ssssss.mm.mm.", ".......mmmmmmmm."}},
        {"Great White", {".......ee.......", "......eeee......", ".....eeeeee.....", ".eeeeeeeeeeeeeee", "eeeekeeeeeeeeeee", "eewwwwwlllllleee", ".lwwwwwllllllle.", "..lllllllllll.ee", ".......ll......."}},
        {"Sperm Whale", {"..aaaaa......a.a", ".aaaaaaaaa..aaaa", "aaaaaaaaaaaaaaa.", "aakaaaaaaaaaaa..", "aaaaaaaaaaaaa...", "aeeeeeeeeeeaa...", "wwwwwwweeee.....", ".eeeeeeeee......"}},
        {"Kraken Spawn", {"....pppppp......", "...pppppppp.....", "..ppwkppwkpp....", "..pppppppppp....", "..pp.pppp.pp....", ".pp.pp..pp.pp...", "pp..p....p..pp..", ".p.pp....pp..p.."}},
        {"Sea Serpent", {".........gggg...", "........gggggg..", ".......ggkggrr..", "....gg..gggggg..", "...gggg..gggg...", "..gg.gg...gg....", ".gg...gg.gg.....", "gg.....ggg......"}},
        {"Drowned King", {"..y.y.yy.y.y....", "..yyyyyyyyyy....", "...wwwwwwww.....", "...wkkwwkkw.....", "...wwwwwwww.....", "....wwkkww......", ".gg.wwwwww.gg...", "..gggwkwkwggg...", "...gg.gg.gg....."}},
        {"Chambered Titan", {"..w..w..w..w....", "..sssssssss.....", ".sooosssoooss...", "soossoooossoos..", "sooosooossooos..", "soosoooooooos...", ".soooosssooss...", "..ssssssss.mmm..", ".......mmmmmmm.."}},
        {"Bilge Rat", {".ee.............", "eeee.....eee....", "eekee...eeeeee..", "eeeeeeeeeeeeeem.", ".eeeeeeeeeeeee.m", "..e.e....e.e..m."}},
        {"Deckhand", {"....wwwww.......", "...wwwwwww......", "...ssssss.......", "...sksskss......", "....ssss........", "..bbwbbwbbw.....", "..bwbbwbbwb.....", "..bbwbbwbbw.....", "...aa...aa......"}},
        {"Rusted Anchor", {".......ee.......", "......e..e......", ".......ee.......", "..eeeeeeeeee....", ".......ne.......", ".......en.......", ".e.....ne....e..", ".ee....ee...ee..", "..eee..en.eee...", "....eeeeeee....."}},
        {"Barnacle Husk", {".....eeeeee.....", "...eeeeeeeeee...", "..elelleeleelee.", ".eeeeeeeeeeeeee.", ".eelweeelweeeee.", ".eeeeeeeeeeeeee.", "..eeeeeeeeeeee.."}},
        {"The Croupier", {"....aaaaaa......", "....aaaaaa......", "..aaaaaaaaaaaa..", "....ssssss......", "...sksssksss....", "...aaaaaaaa.....", "....sssssss.....", ".....rrrrr......", "..aaaaraaaaaa...", "..aaaaaaaaaaaa.."}},
        {"Boulder", {"....eeeeeee.....", "..eeeleeeeeee...", ".eeleeeeeeeeeee.", ".eeeeeeeeaeeeee.", ".eeeeeeeeeeeeee.", "..eeeeeaeeeeee..", "....eeeeeeee...."}},
        {"Swordfish", {"....bb..........", "llllllkbbbbbbb..", "llllllbbbbbbbbbb", "....lllllbbbb.bb", ".....bbbbb..bb.."}},
        {"Squid", {"......pppp......", ".....pppppp.....", "....ppkppkpp....", "....pppppppp....", ".....pppppp.....", "....p.p.p.p.....", "...p..p.p..p....", "...p.p...p.p...."}},
        {"Kraken", {"....pppppppp....", "...pppppppppp...", "..pppwkppwkppp..", "..pppppppppppp..", "..pprrrrrrrrpp..", ".pp.pppppppp.pp.", "pp..pp.pp.pp..pp", "p..pp..pp..pp..p", ".pp.p...pp..p.pp"}},
        {"Atlantean Hoplite", {"....kyyk........", "...kyyyyk...w...", "...kssssk...w...", "...ksssk....w...", "..rrrrrr....w...", ".eeeeeeee...w...", ".eeeyyeee...w...", ".eeeeeeee...w...", "..ee..ee........"}},
        {"Sunken Oracle", {"....bbbbbb......", "...bbbbbbbb.....", "..bbbwwwwbb.....", "..bbwcwwcwb.....", "..bbbwwwwbb.....", "...bbbbbbbb.....", "..bbbbbbbbbb.cc.", ".bbbbbbbbbbb.cc.", "bbbbbbbbbbbbb..."}},
        {"Coral Golem", {"..r.r..r........", ".rrrrrrrrr......", "..eeeeeeee......", ".eeceeeceee.....", ".eeeeeeeeee.....", "eeeeeeeeeeeee...", "eeeeeeeeeeeee...", "ee.ee..ee.ee...."}},
        {"Selenis, the Moon God", {"....wwwwwwww....", "..wwwwwwwwwwww..", ".wwwlwwwwwwlwww.", ".wwwwkwwwwkwwww.", "wwwlwwwwwwwwwwww", "wwwwwwwlwwwwlwww", ".wwwwwwkkwwwwww.", ".wwwlwwwwwwwwww.", "..wwwwwwwwwwww..", "....wwwwwwww...."}},
        {"Black Goat",{".w..........w...", ".ww........ww...", "..ww.aaaa.ww....", "...waaaaaaw.....", "...arraarra.....", "...aaaaaaaa.....", "....aaaaaa......", ".....awwa.......", ".....aaaa......."}},
    };
    return s;
}
}  // namespace

// ---------------------------------------------------------------- from concept grid to a painted, lit illustration
// The hand-drawn grid is only the concept. Each creature is rebuilt once as a high-resolution image: the blocky silhouette is smoothed
// into an organic outline, the height of the body is inferred so it can be lit from the upper left (diffuse, a glossy highlight, ambient
// occlusion and a dark rim), then skin detail is added (scale pattern, fine grain, crosshatching in shadow) and a thin ink line drawn
// round it. The result is drawn scaled down with mipmaps, so it reads as a real, inked creature rather than big squares.
namespace {
using FVec = std::vector<float>;

void BoxBlur(FVec& v, int W, int H, int r, int passes) {
    FVec tmp(v.size());
    for (int p = 0; p < passes; p++) {
        for (int y = 0; y < H; y++) { // horizontal running sum
            float sum = 0; int cnt = 0;
            for (int x = -r; x <= r; x++) if (x >= 0 && x < W) { sum += v[y * W + x]; cnt++; }
            for (int x = 0; x < W; x++) {
                tmp[y * W + x] = sum / (2 * r + 1);
                int add = x + r + 1, rem = x - r;
                if (add < W) sum += v[y * W + add];
                if (rem >= 0) sum -= v[y * W + rem];
            }
            (void)cnt;
        }
        for (int x = 0; x < W; x++) { // vertical
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

constexpr int SC = 3, PADC = 4, UP = 4, OC = SC * UP;   // pixels per (4x upscaled) cell, the margin, the upscale factor, and pixels per ORIGINAL cell

struct Painted { Texture2D tex{}; int gw = 0, gh = 0; bool ok = false; };

Painted Paint(const Sprite& sp) {
    Painted out;
    // first the concept grid is upscaled 4x with the edge-aware EPX rule (twice), which rounds diagonals and curves without smearing detail
    std::vector<std::string> g0;
    for (const char* row : sp.rows) { std::string s = row; s.resize(16, '.'); g0.push_back(s); }
    for (int pass = 0; pass < 2; pass++) {
        int h0 = (int)g0.size(), w0 = (int)g0[0].size();
        std::vector<std::string> g1(h0 * 2, std::string(w0 * 2, '.'));
        auto at = [&](int x, int y, char self) { return (x < 0 || y < 0 || x >= w0 || y >= h0) ? '.' : g0[y][x]; (void)self; };
        for (int y = 0; y < h0; y++) for (int x = 0; x < w0; x++) {
            char P = g0[y][x], A = at(x, y - 1, P), B = at(x + 1, y, P), C = at(x - 1, y, P), D = at(x, y + 1, P);
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
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < cols; x++) {
            char ch = g0[y][x];
            if (ch == '.') continue;
            Color c = Pal(ch);
            int i = (y + PADC) * gw + x + PADC;
            m[i] = 1; cr[i] = c.r; cg[i] = c.g; cb[i] = c.b;
        }
    FVec a1(W * H), R(W * H), G(W * H), B(W * H);
    for (int py = 0; py < H; py++)
        for (int px = 0; px < W; px++) {
            float fx = (px + 0.5f) / SC - 0.5f, fy = (py + 0.5f) / SC - 0.5f;
            int ix = (int)floorf(fx), iy = (int)floorf(fy);
            float tx = fx - ix, ty = fy - iy, asum = 0, rs = 0, gs = 0, bs = 0;
            for (int dy = 0; dy < 2; dy++)
                for (int dx = 0; dx < 2; dx++) {
                    int cx = std::clamp(ix + dx, 0, gw - 1), cy = std::clamp(iy + dy, 0, gh - 1);
                    float wgt = (dx ? tx : 1 - tx) * (dy ? ty : 1 - ty);
                    int i = cy * gw + cx;
                    asum += wgt * m[i]; rs += wgt * cr[i]; gs += wgt * cg[i]; bs += wgt * cb[i];
                }
            int o = py * W + px;
            a1[o] = asum;
            if (asum > 1e-4f) { R[o] = rs / asum; G[o] = gs / asum; B[o] = bs / asum; }
        }
    // colours bleed a little into each other: a painted look, not flat fills
    FVec Rb = R, Gb = G, Bb = B, wgtv = a1;
    for (size_t i = 0; i < Rb.size(); i++) { Rb[i] *= a1[i]; Gb[i] *= a1[i]; Bb[i] *= a1[i]; }
    BoxBlur(Rb, W, H, 1, 1); BoxBlur(Gb, W, H, 1, 1); BoxBlur(Bb, W, H, 1, 1); BoxBlur(wgtv, W, H, 1, 1);
    FVec a2 = a1, hgt = a1;
    BoxBlur(a2, W, H, 3, 2);          // the organic silhouette (small radius, so fins and tentacles survive)
    BoxBlur(hgt, W, H, 11, 3);        // the body's swell: what the light bends round
    std::vector<unsigned char> px(W * H * 4, 0);
    const float lx = -0.52f, ly = -0.62f, lz = 0.59f, ll = sqrtf(lx * lx + ly * ly + lz * lz);
    const float Lx = lx / ll, Ly = ly / ll, Lz = lz / ll;
    float hx = Lx, hy = Ly, hz = Lz + 1, hl = sqrtf(hx * hx + hy * hy + hz * hz); hx /= hl; hy /= hl; hz /= hl;
    for (int y = 1; y < H - 1; y++)
        for (int x = 1; x < W - 1; x++) {
            int o = y * W + x;
            float a = a2[o];
            if (a < 0.10f) continue;
            unsigned char* q = &px[o * 4];
            auto ink = [&](int alpha) { q[0] = 24; q[1] = 17; q[2] = 14; q[3] = (unsigned char)alpha; };
            float wob = (Hash2(x / 2, y / 2) - 0.5f) * 0.10f;               // a ragged, hand-inked contour, like fur and scratchy pen work
            if (a < 0.47f + wob) { ink((int)(255 * SStep(0.10f, 0.20f, a))); continue; }
            float wv = std::max(wgtv[o], 1e-3f);
            float r = Rb[o] / wv, g = Gb[o] / wv, b = Bb[o] / wv;
            float dhx = hgt[o + 1] - hgt[o - 1], dhy = hgt[o + W] - hgt[o - W], dax = a2[o + 1] - a2[o - 1], day = a2[o + W] - a2[o - W];
            float nx = -(dhx * 30 + dax * 2.6f), ny = -(dhy * 30 + day * 2.6f), nz = 1;
            float nl = sqrtf(nx * nx + ny * ny + nz * nz); nx /= nl; ny /= nl; nz /= nl;
            float diff = std::max(0.0f, nx * Lx + ny * Ly + nz * Lz), spec = powf(std::max(0.0f, nx * hx + ny * hy + nz * hz), 28.0f) * 0.32f;
            float shade = 0.40f + 0.82f * diff;
            shade *= 0.72f + 0.28f * SStep(0.47f, 0.62f, a);
            // value: how dark this part of the creature is in the drawing (its own colour, times the light on it)
            float lumBase = (0.3f * r + 0.59f * g + 0.11f * b) / 255.0f;
            float v = std::pow(lumBase, 0.55f) * std::clamp(shade, 0.3f, 1.2f);
            {   // inked linework wherever one colour region meets another: fins, belly, eyes, hat brims
                auto lum = [&](int i) { return 0.3f * R[i] + 0.59f * G[i] + 0.11f * B[i]; };
                float grad = fabsf(lum(o + 1) - lum(o - 1)) + fabsf(lum(o + W) - lum(o - W));
                if (a1[o] > 0.9f && a1[o + 1] > 0.9f && a1[o - 1] > 0.9f && a1[o + W] > 0.9f && a1[o - W] > 0.9f && grad > 36.0f) { ink(255); continue; }
            }
            // ordered dither in 3-pixel blocks: dense black in shadow, brown stipple in the mid-tones, bare paper in the light
            static const int BAYER[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
            float thr = (BAYER[(y / 3) & 3][(x / 3) & 3] + 0.5f) / 16.0f;
            float d = std::clamp((0.80f - v) / 0.62f, 0.0f, 1.0f);
            bool hatch = v < 0.36f && fmodf((x + y) / 7.0f, 1.0f) < 0.2f;                      // diagonal hatching in the shadows
            float su = x / 8.5f, sv = y / 7.0f + ((int)floorf(su) % 2 ? 0.5f : 0.0f);
            float fu = su - floorf(su) - 0.5f, fv = sv - floorf(sv) - 0.5f;
            bool scaleLine = SStep(0.30f, 0.52f, sqrtf(fu * fu * 0.9f + fv * fv * 1.2f)) > 0.9f && v < 0.6f && Hash2(x / 3, y / 3) > 0.55f;   // scale arcs
            if (spec > 0.12f) continue;                                                        // a glint: the paper shows through
            if (hatch || scaleLine || thr < d * 0.55f) ink(255);
            else if (thr < d * 1.1f) { q[0] = 122; q[1] = 88; q[2] = 62; q[3] = 205; }
        }
    Image img{px.data(), W, H, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    out.tex = LoadTextureFromImage(img);
    GenTextureMipmaps(&out.tex);
    SetTextureFilter(out.tex, TEXTURE_FILTER_TRILINEAR);
    out.gw = gw; out.gh = gh; out.ok = out.tex.id != 0;
    return out;
}
std::map<std::string, Painted>& PaintCache() { static std::map<std::string, Painted> c; return c; }
}  // namespace

// Draws the named creature filling `box`. Returns false if the card has no sprite (the caller draws its suit icon).
bool DrawCreaturePixels(const std::string& name, Rectangle box, float dim, int seed) {
    (void)seed;
    const Sprite* sp = nullptr;
    for (const Sprite& s : Sprites()) if (name == s.name) { sp = &s; break; }
    if (!sp) return false;
    auto& cache = PaintCache();
    auto it = cache.find(name);
    if (it == cache.end()) it = cache.emplace(name, Paint(*sp)).first;
    const Painted& p = it->second;
    if (!p.ok) return false;
    int rows = (int)sp->rows.size();
    float scale = std::min(box.width / (16.0f * OC), box.height / (std::max(rows, 9) * (float)OC));
    float dw = p.tex.width * scale, dh = p.tex.height * scale;
    float dx = box.x + (box.width - dw) / 2, dy = box.y + (box.height - dh) / 2 + scale * OC * 0.3f;
    DrawEllipse((int)(dx + dw / 2), (int)(dy + dh - scale * OC * (1 + 0.3f)), dw * 0.36f, scale * OC * 0.8f, Fade(Color{20, 14, 12, 255}, 0.30f * dim));
    DrawTexturePro(p.tex, {0, 0, (float)p.tex.width, (float)p.tex.height}, {dx - dw * 0.06f, dy - dh * 0.10f, dw * 1.12f, dh * 1.12f}, {0, 0}, 0, Fade(Color{130, 100, 78, 255}, 0.16f * dim));   // the creature's faint shadow-self looming behind it
    DrawTexturePro(p.tex, {0, 0, (float)p.tex.width, (float)p.tex.height}, {dx, dy, dw, dh}, {0, 0}, 0, Fade(WHITE, dim));
    return true;
}
