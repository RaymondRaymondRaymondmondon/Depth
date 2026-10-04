// Scuffle stage 5: the stages of the other five worlds, the generator, and every world's finales (doc pp. 9-11).
// Headless. Each world's forty: its three signature stages (built by hand, below) and a mirrored variant of each, and
// seventeen stages from the generator (seeded per world, so the pack is the same every time it's written) and their
// mirrors. Each world also has three finales: twice the size, the wall at 30 s, and its set piece (the propeller starts,
// the Lobster rises, the kraken reaches up from the drop-off, the Wyrm surfaces in the plaza, the leviathan's lure
// appears, closing time with the whole bar thrown in). Every stage passes the reachability check before it's written.
//
// The generator (doc p. 11): it picks the world's ground (the Nautilus's decks and pits, the Cave's floor and ceiling,
// the Reef's islands over the sharks' water, Atlantis's terraces, the Void's floor with the abyss on one side, the
// Salon's closed room), lays 3-7 platforms on a grid in the world's materials, adds 1-3 of the world's hazards and 1-2
// movers, places eight spawns as far apart as it can, and runs the reachability check with the real movement code; a
// layout that fails is redrawn.
#include "scuffle.h"
#include "redtide.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>

namespace sf {

namespace {
struct G {
    Stage s; uint32_t r; int W, H; bool big;
    std::vector<Rectangle> keep;   // (cells kept clear of spawns: hazards)
    G(int world, uint32_t seed, bool finale, const char* name) : r(seed * 2654435761u + 977), big(finale) {
        W = finale ? 64 : 32; H = finale ? 36 : 18;
        s.w = W; s.h = H; s.t.assign(W * H, T_EMPTY); s.world = world; s.name = name; s.author = "Depth"; s.finale = finale;
        for (int i = 0; i < 3; i++) R();
    }
    float R() { r = r * 1664525u + 1013904223u; return ((r >> 8) & 0xffffff) / 16777216.0f; }
    int Ri(int a, int b) { return a + (int)(R() * (b - a + 1)) % std::max(1, b - a + 1); }
    bool In(int x, int y) const { return x >= 0 && y >= 0 && x < W && y < H; }
    void Fill(int x0, int y0, int x1, int y1, uint8_t k) { for (int y = std::min(y0, y1); y <= std::max(y0, y1); y++) for (int x = std::min(x0, x1); x <= std::max(x0, x1); x++) s.Set(x, y, k); }
    bool Clear(int x0, int y0, int x1, int y1) const { for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) if (!In(x, y) || s.At(x, y) != T_EMPTY) return false; return true; }
    Piece& P(int kind, int x, int y, int w, int h) {
        Piece p; p.kind = (uint8_t)kind; PieceDefaults(p); p.x = x; p.y = y; p.w = w; p.h = h;
        uint8_t k = kind == PK_VENT || kind == PK_COLUMN || kind == PK_EEL || kind == PK_GRATE || kind == PK_DRIP || kind == PK_DART || kind == PK_REACHER ? T_STONE : kind == PK_WINDOW ? T_GLASS : kind == PK_SHARK || kind == PK_TOAD ? T_WATER : kind == PK_POOL ? T_WOOD : T_EMPTY;
        if (k != T_EMPTY) Fill(x, y, x + w - 1, y + h - 1, k);
        s.pieces.push_back(p);
        keep.push_back({(float)x, (float)y, (float)w, (float)h});
        return s.pieces.back();
    }
    // the top of the ground at a column (the first empty row above solid ground from the bottom), or -1 if bottomless
    int Top(int x) const { for (int y = 0; y < H; y++) if (s.Solid(x, y) && !s.Solid(x, y + 1)) { bool open = true; for (int k = 1; k <= 3; k++) open &= !s.Solid(x, y + k); if (open) return y + 1; } return -1; }
    int FloorTop(int x) const { for (int y = 0; y < H / 2; y++) if (!s.Solid(x, y)) return y > 0 && s.Solid(x, y - 1) ? y : -1; return -1; }
    // the underside of the ceiling over a column (the first solid going up from row y), or -1
    int Ceil(int x, int y) const { for (int yy = y; yy < H; yy++) if (s.Solid(x, yy)) return yy; return -1; }
};
uint8_t Material(G& g) {
    switch (g.s.world) {
        case WD_CAVE: return g.R() < 0.2f ? T_CRUMBLE : T_STONE;
        case WD_ATLANTIS: return g.R() < 0.2f ? T_ICE : g.R() < 0.15f ? T_CRUMBLE : T_STONE;
        case WD_VOID: return g.R() < 0.25f ? T_CRUMBLE : T_STONE;
        case WD_SALON: return T_WOOD;
        default: return g.R() < 0.15f ? T_ROPE : T_STONE;
    }
}
// ---- the ground of each world
void Ground(G& g) {
    int W = g.W, H = g.H, wd = g.s.world;
    switch (wd) {
    case WD_NAUTILUS: {
        g.Fill(0, 0, W - 1, 1, T_STONE);
        int pits = g.big ? g.Ri(2, 3) : g.Ri(1, 2);
        for (int i = 0; i < pits; i++) { int pw = g.Ri(3, 5), px = g.Ri(5, W - 6 - pw); g.Fill(px, 0, px + pw - 1, 1, T_EMPTY); }
        if (g.R() < 0.5f) { int wh = g.Ri(2, 5); g.Fill(0, 2, 0, wh, T_STONE); g.Fill(W - 1, 2, W - 1, wh, T_STONE); }
        break;
    }
    case WD_CAVE: {
        g.Fill(0, 0, W - 1, 1, T_STONE); g.Fill(0, H - 2, W - 1, H - 1, T_STONE);
        for (int x = 0; x < W; x += g.Ri(3, 6)) { int d = g.Ri(0, 2), w = g.Ri(2, 4); if (d) g.Fill(x, H - 2 - d, std::min(W - 1, x + w - 1), H - 3, T_STONE); }   // (the ceiling's bumps)
        if (g.R() < 0.5f) { g.Fill(0, 2, 0, H - 3, T_STONE); g.Fill(W - 1, 2, W - 1, H - 3, T_STONE); }
        break;
    }
    case WD_REEF: {
        g.Fill(0, 0, W - 1, 1, T_WATER); g.P(PK_SHARK, 0, 0, W, 2);
        int x = g.Ri(0, 2);
        while (x < W - 3) { int w = g.Ri(5, g.big ? 12 : 9), top = g.Ri(3, 4); g.Fill(x, 2, std::min(W - 1, x + w - 1), top - 1, T_STONE); x += w + g.Ri(3, 5); }
        break;
    }
    case WD_ATLANTIS: {
        int x = 0;
        while (x < W) { int w = g.Ri(6, g.big ? 14 : 10), top = g.Ri(2, 4); g.Fill(x, 0, std::min(W - 1, x + w - 1), top - 1, T_STONE); x += w; }
        break;
    }
    case WD_VOID: {
        bool left = g.R() < 0.5f;   // (the abyss's side)
        int len = (int)(W * (0.55f + g.R() * 0.2f));
        if (left) g.Fill(W - len, 0, W - 1, 2, T_STONE); else g.Fill(0, 0, len - 1, 2, T_STONE);
        break;
    }
    case WD_SALON: {
        g.Fill(0, 0, W - 1, 1, T_WOOD); g.Fill(0, 2, 0, H - 1, T_STONE); g.Fill(W - 1, 2, W - 1, H - 1, T_STONE);
        break;
    }
    }
}
// ---- platforms on a grid of tiers
void Platforms(G& g, int n) {
    int tierGap = 4;
    for (int i = 0, tries = 0; i < n && tries < 400; tries++) {
        int tier = g.Ri(1, (g.H - (g.s.world == WD_CAVE ? 5 : 4)) / tierGap);
        int y = tier * tierGap + g.Ri(0, 1) + 1;
        int len = g.Ri(4, g.big ? 11 : 9), x0 = g.Ri(1, g.W - 1 - len);
        if (y >= g.H - 3) continue;
        if (!g.Clear(x0 - 1, y - 2, x0 + len, y + 3)) continue;
        uint8_t m = Material(g);
        g.Fill(x0, y, x0 + len - 1, y, m);
        if (g.s.world == WD_CAVE && g.R() < 0.3f) g.s.Set(g.R() < 0.5f ? x0 : x0 + len - 1, y + 1, T_CRYSTAL);
        if (g.s.world == WD_REEF && g.R() < 0.3f) g.s.Set(g.R() < 0.5f ? x0 : x0 + len - 1, y, T_URCHIN);
        i++;
    }
}
// ---- hazards: each world's kit (the placement tries a few spots; a hazard that won't fit is skipped)
bool Hazard(G& g, int kind) {
    int W = g.W, H = g.H;
    for (int tries = 0; tries < 30; tries++) {
        int x = g.Ri(3, W - 5);
        switch (kind) {
        case PK_STALACTITE: { int c = g.Ceil(x, H / 2); if (c < 0 || c < H - 6 || !g.Clear(x, c - 6, x, c - 1)) continue; g.P(PK_STALACTITE, x, c - 2, 1, 2); return true; }
        case PK_DRIP: { int c = g.Ceil(x, 3); if (c < 0 || c < 6 || !g.Clear(x, c - 4, x, c - 1)) continue; g.P(PK_DRIP, x, c, 1, 1).period = 1.8f + g.R(); return true; }
        case PK_TOAD: { int t = g.FloorTop(x); if (t != 2 || !g.Clear(x, 2, x + 3, 5) || !g.s.Solid(x - 1, 1) || !g.s.Solid(x + 4, 1)) continue; Piece& p = g.P(PK_TOAD, x, 1, 4, 1); p.dx = x < W / 2 ? 1 : -1; p.travel = 5; return true; }
        case PK_STREAM: { int y = g.Ri(2, H / 2); int w = g.Ri(10, g.big ? 24 : 16), x0 = g.Ri(1, W - 1 - w); Piece& p = g.P(PK_STREAM, x0, y, w, 4); p.dx = g.R() < 0.5f ? 1 : -1; p.power = 4 + g.R() * 2; if (g.s.world == WD_REEF) { p.period = 10; p.on = 2; p.power = 7; } return true; }
        case PK_REACHER: { int t = g.Top(x); if (t < 2 || !g.Clear(x, t, x, t + 3) || !g.s.Solid(x - 1, t - 1) || !g.s.Solid(x + 1, t - 1)) continue; g.P(PK_REACHER, x, t, 1, 2); return true; }
        case PK_EEL: { int t = g.FloorTop(x); if (t < 2 || !g.Clear(x - 2, t, x + 3, t + 4)) continue; g.Fill(x, t, x + 1, t + 2, T_STONE); Piece& p = g.P(PK_EEL, g.R() < 0.5f ? x : x + 1, t, 1, 2); p.dx = p.x == x ? -1 : 1; return true; }
        case PK_GRATE: { int t = g.FloorTop(x); if (t < 1 || !g.s.Solid(x + 1, t - 1) || g.FloorTop(x + 1) != t || !g.Clear(x, t, x + 1, t + 4)) continue; g.P(PK_GRATE, x, t - 1, 2, 1).phase = g.R(); return true; }
        case PK_TUNA: { int y = g.Ri(2, 4); if (!g.Clear(0, y + 1, W - 1, y + 2) && g.R() < 0.7f) continue; Piece& p = g.P(PK_TUNA, 0, y, W, 3); p.dx = g.R() < 0.5f ? 1 : -1; p.period = 6 + g.R() * 3; return true; }
        case PK_SLUICE: { int t = g.FloorTop(x); if (t < 1 || x + 7 >= W) continue; g.P(PK_SLUICE, x, t, 7, 4).phase = g.R(); return true; }
        case PK_COLUMN: { int t = g.FloorTop(x); if (t < 1 || !g.Clear(x - 4, t, x + 4, t + 5) || g.FloorTop(x + 3) != t || g.FloorTop(x - 3) != t) continue; g.P(PK_COLUMN, x, t, 1, 5).dx = x < W / 2 ? 1 : -1; return true; }
        case PK_LURE: {
            int ax = g.Top(2) < 0 ? 3 : g.Top(W - 3) < 0 ? W - 4 : -1; if (ax < 0) return false;
            Piece& p = g.P(PK_LURE, ax, g.Ri(5, H / 2), 1, 1); p.travel = 8; p.power = 3.5f; return true;
        }
        case PK_LOWG: { int w = g.Ri(5, 8), h = g.Ri(6, 9), x0 = g.Ri(2, W - 2 - w), y0 = g.Ri(2, H - 2 - h); g.P(PK_LOWG, x0, y0, w, h); return true; }
        case PK_WORM: { int t = g.FloorTop(x); int len = g.Ri(8, 12); if (t < 1 || x + len >= W) continue; bool flat = true; for (int i = 0; i < len; i++) flat &= g.FloorTop(x + i) == t; if (!flat) continue; g.P(PK_WORM, x, t, len, 1).phase = g.R(); return true; }
        case PK_CROWD: { Piece& p = g.P(PK_CROWD, 1, H - 3, W - 2, 2); p.period = 3 + g.R() * 2; return true; }
        case PK_DART: { int side = g.R() < 0.5f ? 0 : W - 1; if (!g.s.Solid(side, 4)) return false; Piece& p = g.P(PK_DART, side, g.Ri(3, 5), 1, 1); p.dx = side == 0 ? 1 : -1; return true; }
        case PK_POOL: { int t = g.FloorTop(x); if (t != 2 || x + 8 >= W - 1 || !g.Clear(x, 2, x + 7, 5)) continue; g.P(PK_POOL, x, 2, 8, 1).dx = g.R() < 0.5f ? 1 : -1; return true; }
        case PK_BOUNCER: { int t = g.FloorTop(x); if (t != 2 || x + 8 >= W - 2 || !g.Clear(x, 2, x + 8, 6)) continue; g.Fill(x, 2, x + 6, 3, T_BAR); g.P(PK_BOUNCER, x + 7, 2, 1, 2); return true; }
        case PK_DOG: { int t = g.FloorTop(x); if (t < 1) continue; int x0 = x, x1 = x; while (x0 > 1 && g.FloorTop(x0 - 1) == t && g.Clear(x0 - 1, t, x0 - 1, t + 1)) x0--; while (x1 < W - 2 && g.FloorTop(x1 + 1) == t && g.Clear(x1 + 1, t, x1 + 1, t + 1)) x1++; if (x1 - x0 < 8) continue; g.P(PK_DOG, x0, t, x1 - x0 + 1, 1); return true; }
        case PK_VENT: { int t = g.FloorTop(x); if (t < 1 || !g.Clear(x, t, x, t + 6)) continue; Piece& p = g.P(PK_VENT, x, t - 1, 1, 1); p.travel = 6; p.phase = g.R(); return true; }
        case PK_PISTON: { int c = g.Ceil(x, g.H / 2); if (c < 0 || !g.Clear(x, c - 7, x + 1, c - 1)) continue; Piece& p = g.P(PK_PISTON, x, c - 2, 2, 2); p.dy = -1; p.travel = 4; p.phase = g.R(); return true; }
        case PK_ELEVATOR: { int t = g.Top(x); if (t < 1 || !g.Clear(x, t, x + 2, t + 7)) continue; Piece& p = g.P(PK_ELEVATOR, x, t, 3, 1); p.travel = (float)g.Ri(4, 6); p.phase = g.R(); return true; }
        case -1: {   // (urchins on the floor)
            int t = g.FloorTop(x); if (t < 1) continue; for (int i = 0; i < 2; i++) if (g.s.Solid(x + i, t - 1)) g.s.Set(x + i, t - 1, T_URCHIN); return true;
        }
        case -2: {   // (a brine pool in the Void's floor)
            int t = g.FloorTop(x); if (t != 3 || g.FloorTop(x + 3) != 3 || !g.s.Solid(x - 1, 2) || !g.s.Solid(x + 4, 2)) continue; g.Fill(x, 1, x + 3, 2, T_BRINE); return true;
        }
        case -3: {   // (a conveyor along a floor stretch)
            int t = g.FloorTop(x); if (t < 1) continue; uint8_t k = g.R() < 0.5f ? T_CONV_L : T_CONV_R; for (int i = 0; i < 6; i++) if (g.FloorTop(x + i) == t) g.s.Set(x + i, t - 1, k); return true;
        }
        case -4: {   // (an electrified rail across a floor stretch)
            int t = g.FloorTop(x); if (t < 1) continue; for (int i = 0; i < 3; i++) if (g.FloorTop(x + i) == t) g.s.Set(x + i, t - 1, T_RAIL); return true;
        }
        case -5: {   // (crystal pillars)
            int t = g.Top(x); if (t < 1 || !g.Clear(x, t, x, t + 3)) continue; g.s.Set(x, t, T_CRYSTAL); g.s.Set(x, t + 1, T_CRYSTAL); return true;
        }
        default: return false;
        }
    }
    return false;
}
void Spawns(G& g, int want) {
    std::vector<Vector2> cand;
    for (int y = 1; y < g.H - 3; y++) for (int x = 1; x < g.W - 1; x++) {
        if (!(g.s.Solid(x, y - 1) || g.s.OneWay(x, y - 1)) || g.s.At(x, y - 1) == T_URCHIN || g.s.At(x, y - 1) == T_BAR || g.s.At(x, y - 1) == T_RAIL || g.s.At(x, y - 1) == T_CRUMBLE) continue;
        if (g.s.At(x, y) != T_EMPTY || g.s.At(x, y + 1) != T_EMPTY || g.s.At(x, y + 2) != T_EMPTY) continue;
        bool bad = false; for (const auto& k : g.keep) if (x >= k.x - 1 && x < k.x + k.width + 1 && y >= k.y - 1 && y < k.y + k.height + 6) bad = true;
        if (!bad) cand.push_back({(float)x, (float)y});
    }
    if (cand.empty()) return;
    std::vector<Vector2> pick{cand[g.Ri(0, (int)cand.size() - 1)]};
    while ((int)pick.size() < want && pick.size() < cand.size()) {
        float best = -1; Vector2 bp{};
        for (const auto& c : cand) { float d = 1e9f; for (const auto& p : pick) d = std::min(d, Vector2Distance(c, p) + (c.y != p.y ? 0 : 0.0f)); if (d > best) { best = d; bp = c; } }
        if (best < 2) break;
        pick.push_back(bp);
    }
    for (const auto& p : pick) g.s.spawns.push_back({(p.x + 0.5f) * TILE, p.y * TILE});
}
std::vector<int> Kit(int world) {
    switch (world) {
        case WD_CAVE: return {PK_STALACTITE, PK_DRIP, PK_TOAD, PK_STREAM, -5};
        case WD_REEF: return {PK_REACHER, PK_EEL, PK_STREAM, -1};
        case WD_ATLANTIS: return {PK_GRATE, PK_TUNA, PK_SLUICE, PK_COLUMN};
        case WD_VOID: return {PK_LURE, PK_LOWG, PK_WORM, -2};
        case WD_SALON: return {PK_CROWD, PK_DART, PK_POOL, PK_BOUNCER, PK_DOG};
        default: return {PK_VENT, PK_PISTON, -4, -3};
    }
}
std::vector<int> Movers(int world) {
    switch (world) {
        case WD_NAUTILUS: return {PK_ELEVATOR, PK_PISTON, -3};
        case WD_CAVE: case WD_REEF: return {PK_ELEVATOR, PK_STREAM};
        case WD_SALON: return {PK_ELEVATOR, -3};
        default: return {PK_ELEVATOR};
    }
}
// the finale's set piece, asleep until `at` seconds
void SetPiece(G& g, float at) {
    int W = g.W, H = g.H;
    switch (g.s.world) {
    case WD_NAUTILUS: { int x = W / 2 - 1; g.Fill(x - 3, 0, x + 4, 1, T_EMPTY); Piece& p = g.P(PK_PROPELLER, x, 0, 2, 3); p.start = at; p.power = 1.3f; break; }   // (the propeller starts in the pit at the middle)
    case WD_CAVE: { int y = g.FloorTop(W / 2); Piece& p = g.P(PK_CLAW, 2, std::max(2, y), W - 4, 4); p.start = at; p.period = 8; p.on = 1.1f; p.dx = 1; break; }   // (the Lobster rises)
    case WD_REEF: { Piece& p = g.P(PK_KRAKEN, 2, 0, W - 4, 4); p.start = at; p.period = 4.5f; break; }   // (the kraken reaches up from the drop-off)
    case WD_ATLANTIS: { int made = 0; for (int i = 0; i < 40 && made < 4; i++) { size_t n = g.s.pieces.size(); if (Hazard(g, PK_GRATE)) { made++; for (size_t k = n; k < g.s.pieces.size(); k++) { g.s.pieces[k].start = at; g.s.pieces[k].travel = 9; g.s.pieces[k].period = 3.5f; } } } break; }   // (the Wyrm surfaces in the plaza)
    case WD_VOID: { int ax = g.Top(2) < 0 ? 4 : W - 5; Piece& p = g.P(PK_LURE, ax, H / 2 - 2, 1, 1); p.start = at; p.travel = 16; p.power = 6; break; }   // (the leviathan's lure appears)
    case WD_SALON: { Piece& p = g.P(PK_CROWD, 1, H - 3, W - 2, 2); p.start = at; p.period = 0.5f; p.power = 12; break; }   // (closing time: the whole bar thrown in)
    }
}
}  // namespace

Stage GenerateStage(int world, uint32_t seed, bool finale, int* triesOut) {
    world = std::clamp(world, 0, WD_COUNT - 1);
    static const char* WORDS[WD_COUNT][2][10] = {
        {{"Boiler", "Bilge", "Ballast", "Pump", "Hatch", "Gantry", "Valve", "Chain", "Rivet", "Coal"}, {"Room", "Deck", "Run", "Locker", "Well", "Bay", "Walk", "Trunk", "Gallery", "Shaft"}},
        {{"Drip", "Glow", "Echo", "Lantern", "Crystal", "Mould", "Toad", "Silt", "Flint", "Bat"}, {"Gallery", "Hollow", "Grotto", "Pool", "Chamber", "Throat", "Shelf", "Passage", "Den", "Vault"}},
        {{"Coral", "Kelp", "Surge", "Sandbar", "Urchin", "Moray", "Turtle", "Lagoon", "Brain", "Fan"}, {"Garden", "Shoals", "Channel", "Ledge", "Bed", "Wall", "Flats", "Bank", "Head", "Gap"}},
        {{"Temple", "Forum", "Aqueduct", "Pillar", "Marble", "Oracle", "Trident", "Mosaic", "Bath", "Senate"}, {"Steps", "Court", "Terrace", "Arcade", "Stair", "Hall", "Walk", "Fountain", "Gate", "Ruin"}},
        {{"Black", "Hollow", "Brine", "Vent", "Ash", "Silent", "Lantern", "Pale", "Dead", "Deep"}, {"Shelf", "Edge", "Pocket", "Field", "Spur", "Ledges", "Flats", "Sill", "Ridge", "Mouth"}},
        {{"Back", "Card", "Snug", "Cellar", "Jukebox", "Dartboard", "Barrel", "Dance", "Cloak", "Piano"}, {"Room", "Corner", "Bar", "Floor", "Stair", "Nook", "Stage", "Booth", "Alley", "Yard"}}};
    for (int attempt = 0; attempt < 60; attempt++) {
        uint32_t sd = seed + attempt * 7919u;
        char name[64];
        uint32_t h = sd * 2246822519u;
        snprintf(name, sizeof name, "The %s %s", WORDS[world][0][(h >> 4) % 10], WORDS[world][1][(h >> 12) % 10]);
        G g(world, sd, finale, finale ? (std::string(name) + " (Finale)").c_str() : name);
        Ground(g);
        Platforms(g, finale ? g.Ri(8, 14) : g.Ri(3, 7));
        std::vector<int> kit = Kit(world), mv = Movers(world);
        int nh = finale ? g.Ri(2, 4) : g.Ri(1, 3);
        for (int i = 0; i < nh; i++) Hazard(g, kit[g.Ri(0, (int)kit.size() - 1)]);
        int nm = finale ? g.Ri(2, 3) : g.Ri(1, 2);
        for (int i = 0; i < nm; i++) Hazard(g, mv[g.Ri(0, (int)mv.size() - 1)]);
        if (world == WD_REEF && g.R() < 0.3f) Hazard(g, PK_STREAM);
        if (finale) SetPiece(g, 12);
        Spawns(g, 8);
        if (g.s.spawns.size() < 4) continue;
        if (!CheckReachable(g.s).ok) continue;
        if (triesOut) *triesOut = attempt + 1;
        return g.s;
    }
    if (triesOut) *triesOut = -1;
    Stage s = StoneStage(); s.world = world; s.name = "The Stone Yard"; return s;
}

namespace {
// the hand-built signature stages (doc pp. 9-10), three a world, in the same little builder the Nautilus uses
struct B {
    Stage s;
    B(int world, const char* name, int w = 32, int h = 18) { s.name = name; s.w = w; s.h = h; s.t.assign(w * h, T_EMPTY); s.world = world; s.author = "Depth"; }
    B& fill(int x0, int y0, int x1, int y1, uint8_t k = T_STONE) { for (int y = std::min(y0, y1); y <= std::max(y0, y1); y++) for (int x = std::min(x0, x1); x <= std::max(x0, x1); x++) s.Set(x, y, k); return *this; }
    B& spawn(int x, int y) { s.spawns.push_back({(x + 0.5f) * TILE, y * TILE}); return *this; }
    Piece& piece(int kind, int x, int y, int w, int h) {
        Piece p; p.kind = (uint8_t)kind; PieceDefaults(p); p.x = x; p.y = y; p.w = w; p.h = h;
        uint8_t k = kind == PK_VENT || kind == PK_COLUMN || kind == PK_EEL || kind == PK_GRATE || kind == PK_DRIP || kind == PK_DART || kind == PK_REACHER ? T_STONE : kind == PK_SHARK || kind == PK_TOAD ? T_WATER : kind == PK_POOL ? T_WOOD : T_EMPTY;
        if (k != T_EMPTY) fill(x, y, x + w - 1, y + h - 1, k);
        s.pieces.push_back(p); return s.pieces.back();
    }
};
std::vector<Stage> Signatures(int world) {
    std::vector<Stage> v;
    switch (world) {
    case WD_CAVE: {
        // The Chimney: vertical; ledges up both walls, the drips over the middle
        { B b(WD_CAVE, "The Chimney"); b.fill(0, 0, 31, 1).fill(0, 17, 31, 17).fill(0, 2, 1, 16).fill(30, 2, 31, 16)
            .fill(2, 5, 9, 5).fill(22, 9, 29, 9).fill(2, 13, 9, 13).fill(13, 7, 18, 7).fill(13, 11, 18, 11, T_CRUMBLE).fill(11, 9, 12, 9, T_CRYSTAL);
          b.piece(PK_DRIP, 15, 17, 1, 1).period = 2.0f; b.piece(PK_DRIP, 25, 17, 1, 1).period = 2.6f; b.piece(PK_STALACTITE, 25, 15, 1, 2);
          b.spawn(4, 2).spawn(27, 2).spawn(16, 2).spawn(5, 6).spawn(25, 10).spawn(5, 14).spawn(14, 8).spawn(17, 8); v.push_back(b.s); }
        // The Slipstream: everyone's moving (a current along the bottom, another back along the top; the stage wraps)
        { B b(WD_CAVE, "The Slipstream"); b.s.wrap = true; b.fill(0, 0, 31, 1).fill(0, 16, 31, 17).fill(4, 8, 12, 8).fill(19, 8, 27, 8).fill(10, 12, 21, 12).fill(14, 4, 17, 4, T_CRYSTAL);
          Piece& a = b.piece(PK_STREAM, 0, 2, 32, 4); a.dx = 1; a.power = 4.5f; Piece& c = b.piece(PK_STREAM, 0, 9, 32, 3); c.dx = -1; c.power = 4.5f;
          b.piece(PK_STALACTITE, 8, 14, 1, 2); b.piece(PK_STALACTITE, 24, 14, 1, 2);
          b.spawn(3, 2).spawn(28, 2).spawn(10, 2).spawn(21, 2).spawn(6, 9).spawn(25, 9).spawn(13, 13).spawn(18, 13); v.push_back(b.s); }
        // The Lobster: a giant claw sweeps the floor on a timer; get up on the ledges
        { B b(WD_CAVE, "The Lobster"); b.fill(0, 0, 31, 1).fill(0, 16, 31, 17).fill(3, 6, 9, 6).fill(22, 6, 28, 6).fill(12, 10, 19, 10).fill(3, 7, 3, 8, T_CRYSTAL).fill(28, 7, 28, 8, T_CRYSTAL);
          Piece& k = b.piece(PK_CLAW, 2, 2, 28, 3); k.period = 7; k.on = 1.0f; k.dx = 1;
          b.piece(PK_STALACTITE, 15, 13, 1, 2);
          b.spawn(2, 2).spawn(29, 2).spawn(14, 2).spawn(17, 2).spawn(5, 7).spawn(25, 7).spawn(14, 11).spawn(17, 11); v.push_back(b.s); }
        break;
    }
    case WD_REEF: {
        // The Bommie: a pinnacle in the middle of the sharks' water
        { B b(WD_REEF, "The Bommie"); b.fill(0, 0, 31, 1, T_WATER); b.piece(PK_SHARK, 0, 0, 32, 2);
          b.fill(12, 2, 19, 4).fill(13, 5, 18, 7).fill(14, 8, 17, 10).fill(2, 2, 7, 3).fill(24, 2, 29, 3).fill(3, 8, 7, 8).fill(24, 8, 28, 8).fill(8, 11, 12, 11, T_ROPE).fill(19, 11, 23, 11, T_ROPE);
          Piece& e = b.piece(PK_EEL, 13, 6, 1, 2); e.dx = -1; e.travel = 2;
          b.piece(PK_REACHER, 18, 8, 1, 2);
          b.spawn(3, 4).spawn(28, 4).spawn(12, 5).spawn(19, 5).spawn(5, 9).spawn(26, 9).spawn(15, 11).spawn(16, 11); v.push_back(b.s); }
        // The Flats: open, no cover, the sharks between two long sand flats; the surge runs across the gap on a count
        { B b(WD_REEF, "The Flats"); b.fill(0, 0, 31, 1, T_WATER); b.piece(PK_SHARK, 0, 0, 32, 2);
          b.fill(1, 2, 12, 2).fill(19, 2, 30, 2).fill(5, 3, 6, 3, T_URCHIN).fill(25, 3, 26, 3, T_URCHIN);
          Piece& s = b.piece(PK_STREAM, 10, 3, 12, 4); s.dx = 1; s.period = 10; s.on = 2; s.power = 7;
          b.spawn(2, 3).spawn(29, 3).spawn(9, 3).spawn(22, 3).spawn(4, 3).spawn(27, 3).spawn(11, 3).spawn(20, 3); v.push_back(b.s); }
        // The Drop-off: a cliff into blue water; small heads of coral out over it
        { B b(WD_REEF, "The Drop-off"); b.fill(16, 0, 31, 1, T_WATER); b.piece(PK_SHARK, 16, 0, 16, 2);
          b.fill(0, 0, 15, 7).fill(19, 4, 23, 4).fill(25, 7, 29, 7).fill(20, 10, 25, 10).fill(4, 11, 10, 11, T_ROPE);
          b.piece(PK_REACHER, 15, 8, 1, 2);
          Piece& e = b.piece(PK_EEL, 15, 3, 1, 2); e.dx = 1; e.travel = 3;
          b.spawn(2, 8).spawn(7, 8).spawn(12, 8).spawn(21, 5).spawn(27, 8).spawn(22, 11).spawn(6, 12).spawn(9, 12); v.push_back(b.s); }
        break;
    }
    case WD_ATLANTIS: {
        // The Plaza: the tuna lane runs across the square; plinths to stand clear of it
        { B b(WD_ATLANTIS, "The Plaza"); b.fill(0, 0, 31, 1).fill(3, 2, 7, 4).fill(24, 2, 28, 4).fill(10, 8, 21, 8).fill(1, 11, 6, 11).fill(25, 11, 30, 11);
          Piece& t = b.piece(PK_TUNA, 0, 2, 32, 3); t.dx = 1; t.period = 6;
          b.piece(PK_COLUMN, 9, 2, 1, 5).dx = -1; b.piece(PK_COLUMN, 22, 2, 1, 5).dx = 1;
          b.spawn(4, 5).spawn(26, 5).spawn(5, 5).spawn(27, 5).spawn(12, 9).spawn(19, 9).spawn(3, 12).spawn(28, 12); v.push_back(b.s); }
        // The Baths: slippery floors, the hypocaust boils through vents, the sluice floods the middle
        { B b(WD_ATLANTIS, "The Baths"); b.fill(0, 0, 31, 0).fill(0, 1, 31, 1, T_ICE).fill(3, 6, 11, 6, T_ICE).fill(20, 6, 28, 6, T_ICE).fill(12, 10, 19, 10);
          Piece& v1 = b.piece(PK_VENT, 8, 1, 1, 1); v1.travel = 6; v1.period = 3.5f; Piece& v2 = b.piece(PK_VENT, 23, 1, 1, 1); v2.travel = 6; v2.period = 3.5f; v2.phase = 0.5f;
          b.piece(PK_SLUICE, 12, 2, 8, 4);
          b.spawn(2, 2).spawn(29, 2).spawn(5, 7).spawn(26, 7).spawn(9, 7).spawn(22, 7).spawn(14, 11).spawn(17, 11); v.push_back(b.s); }
        // The Cisterns: grates everywhere (the Wyrm under all of them)
        { B b(WD_ATLANTIS, "The Cisterns"); b.fill(0, 0, 31, 1).fill(2, 6, 8, 6).fill(13, 6, 18, 6).fill(23, 6, 29, 6).fill(7, 10, 12, 10).fill(19, 10, 24, 10);
          for (int i = 0; i < 5; i++) { Piece& g = b.piece(PK_GRATE, 4 + i * 6, 1, 2, 1); g.phase = i * 0.2f; g.period = 5; }
          b.spawn(1, 2).spawn(30, 2).spawn(4, 7).spawn(27, 7).spawn(15, 7).spawn(16, 7).spawn(9, 11).spawn(22, 11); v.push_back(b.s); }
        break;
    }
    case WD_VOID: {
        // The Rim: the worm takes the edges of a floor over nothing
        { B b(WD_VOID, "The Rim"); b.fill(4, 0, 27, 2).fill(8, 6, 14, 6).fill(17, 6, 23, 6).fill(12, 10, 19, 10);
          Piece& a = b.piece(PK_WORM, 4, 3, 7, 1); a.period = 6; Piece& c = b.piece(PK_WORM, 21, 3, 7, 1); c.period = 6; c.phase = 0.5f;
          b.spawn(13, 3).spawn(18, 3).spawn(10, 7).spawn(21, 7).spawn(14, 11).spawn(17, 11).spawn(15, 3).spawn(16, 3); v.push_back(b.s); }
        // The Overlook: a ledge over nothing; a low gravity pocket out over the abyss, and the lure beyond it
        { B b(WD_VOID, "The Overlook"); b.fill(0, 0, 11, 2).fill(12, 7, 24, 7).fill(20, 11, 26, 11).fill(3, 8, 8, 8);
          b.piece(PK_LOWG, 13, 2, 12, 4).power = 0.4f;
          Piece& l = b.piece(PK_LURE, 29, 6, 1, 1); l.travel = 9; l.power = 3.5f;
          b.spawn(2, 3).spawn(9, 3).spawn(5, 3).spawn(5, 9).spawn(14, 8).spawn(22, 8).spawn(18, 8).spawn(23, 12); v.push_back(b.s); }
        // The Reactor: vents on a timer, brine in the floor
        { B b(WD_VOID, "The Reactor"); b.fill(0, 0, 31, 2).fill(6, 1, 9, 2, T_BRINE).fill(22, 1, 25, 2, T_BRINE).fill(5, 8, 11, 8).fill(20, 8, 26, 8).fill(12, 12, 19, 12);
          float ph[4] = {0, 0.25f, 0.5f, 0.75f}; int vx[4] = {3, 15, 16, 28};
          for (int i = 0; i < 4; i++) { Piece& p = b.piece(PK_VENT, vx[i], 2, 1, 1); p.travel = 7; p.period = 4; p.phase = ph[i]; p.power = 1.4f; }
          b.piece(PK_LOWG, 12, 13, 8, 4);
          b.spawn(1, 3).spawn(30, 3).spawn(11, 3).spawn(20, 3).spawn(7, 9).spawn(24, 9).spawn(13, 13).spawn(18, 13); v.push_back(b.s); }
        break;
    }
    case WD_SALON: {
        // The Bar: the counter is the stage (here it's yours: the bouncer only comes at closing time)
        { B b(WD_SALON, "The Bar"); b.fill(0, 0, 31, 1, T_WOOD).fill(0, 2, 0, 17).fill(31, 2, 31, 17).fill(6, 2, 25, 4, T_WOOD).fill(3, 9, 10, 9, T_WOOD).fill(21, 9, 28, 9, T_WOOD).fill(12, 12, 19, 12, T_CRUMBLE);
          b.piece(PK_CROWD, 1, 15, 30, 2).period = 4; b.piece(PK_DART, 0, 6, 1, 1).dx = 1;
          b.spawn(3, 2).spawn(28, 2).spawn(8, 5).spawn(23, 5).spawn(5, 10).spawn(26, 10).spawn(14, 5).spawn(17, 5); v.push_back(b.s); }
        // The Pool Table: the balls are hazards
        { B b(WD_SALON, "The Pool Table"); b.fill(0, 0, 31, 1, T_WOOD).fill(0, 2, 0, 17).fill(31, 2, 31, 17).fill(2, 7, 8, 7, T_WOOD).fill(23, 7, 29, 7, T_WOOD).fill(11, 11, 20, 11, T_WOOD);
          b.piece(PK_POOL, 6, 2, 20, 1).period = 3.5f; b.piece(PK_CROWD, 1, 15, 30, 2).period = 5; b.piece(PK_BOUNCER, 30, 2, 1, 2);
          b.fill(2, 2, 4, 3, T_BAR);
          b.spawn(1, 2).spawn(30, 2).spawn(10, 3).spawn(21, 3).spawn(4, 8).spawn(26, 8).spawn(13, 12).spawn(18, 12); v.push_back(b.s); }
        // The Yard: mini golf windmills (their sails sweep), the dog
        { B b(WD_SALON, "The Yard"); b.fill(0, 0, 31, 1, T_WOOD).fill(4, 7, 11, 7, T_WOOD).fill(20, 7, 27, 7, T_WOOD).fill(13, 11, 18, 11, T_WOOD).fill(7, 2, 7, 3).fill(24, 2, 24, 3);
          Piece& a = b.piece(PK_PISTON, 8, 3, 1, 1); a.dx = 1; a.dy = 0; a.travel = 3; a.period = 2.2f; Piece& c = b.piece(PK_PISTON, 23, 3, 1, 1); c.dx = -1; c.dy = 0; c.travel = 3; c.period = 2.2f; c.phase = 0.5f;
          b.piece(PK_DOG, 1, 2, 30, 1);
          b.spawn(2, 2).spawn(29, 2).spawn(15, 2).spawn(16, 2).spawn(6, 8).spawn(25, 8).spawn(14, 12).spawn(17, 12); v.push_back(b.s); }
        break;
    }
    }
    return v;
}
Stage MirrorOf(const Stage& in, const std::string& name) {
    Stage s = in; s.name = name;
    for (int y = 0; y < s.h; y++) for (int x = 0; x < s.w; x++) { uint8_t k = in.At(s.w - 1 - x, y); if (k == T_CONV_L) k = T_CONV_R; else if (k == T_CONV_R) k = T_CONV_L; s.t[y * s.w + x] = k; }
    for (auto& sp : s.spawns) sp.x = s.Width() - sp.x;
    for (auto& p : s.pieces) { p.x = s.w - p.x - p.w; p.dx = -p.dx; if (p.kind != PK_WINDOW && p.kind != PK_STALACTITE && p.kind != PK_COLUMN && p.period > 0) p.phase = p.phase + 0.5f >= 1 ? p.phase - 0.5f : p.phase + 0.5f; }
    for (auto& c : s.crateCols) c = s.w - 1 - c;
    return s;
}
}  // namespace

Stage MirrorStage(const Stage& s) { return MirrorOf(s, s.name); }
std::vector<Stage> BuildWorld(int world) {
    std::vector<Stage> v;
    if (world == WD_NAUTILUS) v = BuildNautilus();
    else {
        std::vector<Stage> sig = Signatures(world);
        static const char* TAG[3] = {" (Night)", " (Turned)", " (Late)"};
        for (size_t i = 0; i < sig.size(); i++) v.push_back(sig[i]);
        for (size_t i = 0; i < sig.size(); i++) v.push_back(MirrorOf(sig[i], sig[i].name + TAG[i % 3]));
        uint32_t seed = 1000 + world * 100000;
        std::vector<std::string> names;
        for (auto& s : v) names.push_back(s.name);
        while ((int)v.size() < 40) {
            int tries = 0; Stage g = GenerateStage(world, seed, false, &tries); seed += 31;
            if (tries < 0 || std::find(names.begin(), names.end(), g.name) != names.end()) continue;   // (a name once only)
            Stage m = MirrorOf(g, g.name + " (Turned)");
            if (std::find(names.begin(), names.end(), m.name) != names.end()) continue;
            names.push_back(g.name); names.push_back(m.name);
            v.push_back(g); if ((int)v.size() < 40) v.push_back(m);
        }
    }
    // the three finales
    uint32_t fs = 77 + world * 1000;
    for (int i = 0; i < 3; i++) { Stage f = GenerateStage(world, fs, true); fs += 97; f.finale = true; v.push_back(f); }
    return v;
}

} // namespace sf
