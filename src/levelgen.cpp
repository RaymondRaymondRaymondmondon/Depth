#include "levelgen.h"
#include <algorithm>
#include <cmath>
#include <random>

using namespace kin;

// ---------------------------------------------------------------------------- the arc
// A full jump leaves the ground at JUMP_V and rises under GRAV_UP to an apex of v^2/2g (about 3.85 tiles), then
// falls under GRAV_DOWN. To land on a surface `rise` higher, the fall only covers (apex - rise), so:
//   reach = RUN * (JUMP_V / GRAV_UP  +  sqrt(2 * (apex - rise) / GRAV_DOWN))
JumpArc CalculateValidJumpArc(float risePx, int n) {
    JumpArc a;
    a.apex = JUMP_V * JUMP_V / (2 * GRAV_UP);
    a.timeUp = JUMP_V / GRAV_UP;
    if (risePx > a.apex - 6) return a; // the surface is above the top of the jump
    float tDown = std::sqrt(2 * (a.apex - risePx) / GRAV_DOWN);
    a.maxReach = RUN * (a.timeUp + tDown);
    a.reachable = true;
    for (int i = 0; i <= n; i++) {
        float t = (a.timeUp + tDown) * i / std::max(1, n);
        float h = t <= a.timeUp ? JUMP_V * t - 0.5f * GRAV_UP * t * t : a.apex - 0.5f * GRAV_DOWN * (t - a.timeUp) * (t - a.timeUp);
        a.samples.push_back({RUN * t, h});
    }
    return a;
}

namespace {
struct Rng {
    std::mt19937 g;
    explicit Rng(unsigned s) : g(s) {}
    int I(int lo, int hi) { if (hi < lo) hi = lo; return std::uniform_int_distribution<int>(lo, hi)(g); }
    float F(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(g); }
    bool C(float p) { return F(0, 1) < p; }
};

struct Grid {
    int w, h;
    std::vector<std::string> r;
    Grid(int w_, int h_, char f) : w(w_), h(h_), r(h_, std::string(w_, f)) {}
    bool in(int x, int y) const { return x >= 0 && x < w && y >= 0 && y < h; }
    char get(int x, int y) const { return in(x, y) ? r[y][x] : '#'; }
    void set(int x, int y, char c) { if (in(x, y)) r[y][x] = c; }
    void rect(int x0, int y0, int x1, int y1, char c) { for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) set(x, y, c); }
};

enum Conn { C_START, C_JUMP, C_SHAFT_UP, C_SHAFT_DOWN, C_CRUMBLE, C_STEAM };
struct Plat {
    int x0, x1, y;          // top surface row y, tiles x0..x1
    int conn;
    char ch;                // '#' ledge, '=' pipe or beam, 'f' crumbling scaffold
    SetPiece tag;
    int gap;                // tiles of air between this and the previous platform (jump connections)
    int wx;                 // the tile the critical path stands on
    bool deck = false;      // a big solid deck, drawn thick
};

struct Params {
    float safety;
    int length, H, wMin, wMax, maxRise, maxDrop, gapMin;
    bool enclosed;
    float shaftChance;
    int shaftMin, shaftMax;
    float setChance;
};

Params ParamsFor(int level) {
    switch (level) {
        case 0: return {0.62f, 230, 40, 4, 9, 2, 3, 1, true, 0.0f, 0, 0, 0.38f};     // the Pipes: wide, forgiving, no wall jumps
        case 1: return {0.78f, 270, 64, 2, 5, 3, 4, 2, false, 0.40f, 9, 14, 0.20f};   // the Hull: verticality, shafts, footholds
        default: return {0.93f, 310, 64, 1, 3, 3, 4, 3, false, 0.36f, 10, 14, 0.20f}; // the Pirate Ship: tiny footholds at the arc's edge
    }
}
}  // namespace


// ---------------------------------------------------------------------------- shafts
// A shaft is two walls to wall-jump between. Up: a doorway at the bottom of the left wall, a right wall whose top is the
// way out. Down: drop in from the entry platform, leave through a doorway at the bottom of the right wall.
static Plat CarveShaft(Grid& g, const Plat& c, bool up, int iw, int Hs, bool barnacle, int ext) {
    int yb = c.y, sx = c.x1 + 1, lw0 = sx, lw1 = sx + 1, i0 = sx + 2, rw0 = sx + 2 + iw, rw1 = sx + 3 + iw;
    Plat n{rw0, rw1 + ext, up ? yb - Hs : yb + Hs, up ? C_SHAFT_UP : C_SHAFT_DOWN, '#', up ? SetPiece::ShaftUp : SetPiece::ShaftDown, 0, rw0 + 1};
    if (barnacle) n.tag = SetPiece::BarnacleShaft;
    if (up) {
        g.rect(sx, yb, rw1, yb + 1, '#');                     // the shaft floor
        g.rect(lw0, yb - Hs - 1, lw1, yb - 4, '#');           // left wall, a tile taller, with a doorway at the bottom
        g.rect(rw0, yb - Hs, rw1, yb - 1, '#');               // right wall, whose top is the way out
        if (barnacle) { g.rect(lw1, yb - Hs - 1, lw1, yb - 4, 'b'); g.rect(rw0, yb - Hs + 1, rw0, yb - 1, 'b'); }
    } else {
        g.rect(lw0, yb, lw1, yb + Hs + 1, '#');               // left wall, continuing the platform down
        g.rect(rw0, yb - 8, rw1, yb + Hs - 4, '#');           // right wall, with a doorway at the bottom
        g.rect(i0, yb + Hs, rw0 - 1, yb + Hs + 1, '#');       // the shaft floor
        if (barnacle) g.rect(lw1, yb + 1, lw1, yb + Hs - 1, 'b');
    }
    for (int x = n.x0; x <= n.x1; x++) for (int t = 0; t < 2; t++) g.set(x, n.y + t, '#');
    return n;
}

// The tallest up-shaft the diver can climb, per interior width (3 or 4 tiles), proven by `depth.exe --verify`
// (which searches every template with the real movement code). Down shafts are always passable.
static int MaxUpShaft(int iw, bool barnacle) {
    static const int plain[2] = {UP_MAX_3, UP_MAX_4}, barn[2] = {UP_MAX_3B, UP_MAX_4B};
    return (barnacle ? barn : plain)[iw == 3 ? 0 : 1];
}

GenLevel ShaftTemplate(int iw, int Hs, bool up, bool barnacle) {
    Grid g(60, 60, '.');
    Plat first{2, 9, 30, C_START, '#', SetPiece::None, 0, 3};
    for (int x = first.x0; x <= first.x1; x++) for (int t = 0; t < 2; t++) g.set(x, first.y + t, '#');
    g.set(first.wx, first.y - 1, 'S');
    Plat n = CarveShaft(g, first, up, iw, Hs, barnacle, 2);
    GenLevel out;
    out.w = n.x1 + 1;
    out.h = 60;
    out.rows = g.r;
    for (auto& r : out.rows) r.resize(out.w);
    out.exitRow = n.y - 1;
    out.path = {{first.wx, first.y - 1, SetPiece::None}, {n.wx, n.y - 1, n.tag}};
    return out;
}
// ---------------------------------------------------------------------------- macro-structures
// The Hull is a trench: tall coral columns rooted in the seabed and reaching the ceiling, each with a tunnel at its base,
// alternating with short columns whose tops are plateaus. You come through a tunnel, climb the shaft between a tall column
// and a short one (wall-jumping), cross the plateau, drop into the shaft on its far side, and leave through the next tall
// column's tunnel. Nothing floats: every tile of the level is part of a column or the seabed.
static void BuildTrench(Grid& g, std::vector<Plat>& pl, Rng& rng, GenLevel& out, const Params& P, int& wOut) {
    const int H = g.h, F = H - 6;
    g.rect(0, F, g.w - 1, H - 1, '#');       // the seabed
    g.rect(0, 0, g.w - 1, 1, '#');           // the ceiling of the trench
    auto column = [&](int x0, int width, int top, bool tunnel) {
        g.rect(x0, top, x0 + width - 1, H - 1, '#');
        if (tunnel) g.rect(x0, F - 3, x0 + width - 1, F - 1, '.');
    };
    Plat start{2, 9, F, C_START, '#', SetPiece::None, 0, 3};
    pl.push_back(start);
    int x = 10;
    column(x, 3, 0, true);
    int guard = 0;
    while (x < P.length && guard++ < 40) {
        int iw1 = rng.I(3, 4), iw2 = rng.I(3, 4), sw = rng.I(5, 7);
        bool barnacle = rng.C(0.35f);
        int Hs = std::min(rng.I(P.shaftMin, P.shaftMax + 1), MaxUpShaft(iw1, barnacle));
        int upX0 = x + 3, sX0 = upX0 + iw1, sTop = F - Hs, t2X0 = sX0 + sw + iw2;
        column(sX0, sw, sTop, false);                                       // the short column, its top a plateau
        column(t2X0, 3, 0, true);                                           // the next tall column, with its tunnel
        if (barnacle) { g.rect(x + 2, sTop - 1, x + 2, F - 4, 'b'); g.rect(sX0, sTop + 1, sX0, F - 1, 'b'); }
        // an overhang shelf off the tall column high above the plateau, with weed hanging from it
        int shelfY = sTop - 12;
        if (shelfY > 3) {
            g.rect(upX0, shelfY, upX0 + 2, shelfY, '#');
            for (int k = 0; k < 3; k += 2) g.rect(upX0 + k, shelfY + 1, upX0 + k, shelfY + 4, 'w');
        }
        // the down shaft's floor: urchins across its left, the tunnel side left clear
        for (int xx = sX0 + sw; xx <= t2X0 - 3; xx++) g.set(xx, F, 'x');
        // coins up the middle of the climb, crossing the plateau
        for (int k = 0; k < 3; k++) { int cy = F - 3 - (Hs - 4) * (k + 1) / 4; if (cy < F && g.get(upX0 + iw1 / 2, cy) == '.') g.set(upX0 + iw1 / 2, cy, 'o'); }
        for (int xx = sX0 + 1; xx < sX0 + sw - 1; xx += 2) if (g.get(xx, sTop - 1) == '.') g.set(xx, sTop - 1, 'o');
        if (sw >= 6 && rng.C(0.5f)) { // an urchin bed in the plateau, with a mine hung just over it (Hard only): a short, careful hop
            g.set(sX0 + 3, sTop, 'x');
            if (rng.C(0.6f) && g.get(sX0 + 3, sTop - 4) == '.') g.set(sX0 + 3, sTop - 4, 'g');
        } else if (sw >= 5 && rng.C(0.5f) && g.get(sX0 + sw - 2, sTop - 1) == '.') g.set(sX0 + sw - 2, sTop - 1, 'c');
        if (rng.C(0.4f) && g.get(sX0 + 1, sTop - 8) == '.') g.set(sX0 + 1, sTop - 8, 'p');
        // the critical path: in through the tunnel, up to the plateau, down to the next tunnel
        Plat entry{upX0, upX0 + iw1 - 1, F, C_JUMP, '#', SetPiece::None, 0, upX0};
        Plat plateau{sX0, sX0 + sw - 1, sTop, C_SHAFT_UP, '#', barnacle ? SetPiece::BarnacleShaft : SetPiece::ShaftUp, 0, sX0 + 1};
        Plat bottom{t2X0 - 2, t2X0 - 1, F, C_SHAFT_DOWN, '#', SetPiece::ShaftDown, 0, t2X0 - 1};
        pl.push_back(entry); pl.push_back(plateau); pl.push_back(bottom);
        out.setPieces[(int)plateau.tag]++; out.setPieces[(int)SetPiece::ShaftDown]++;
        x = t2X0;
    }
    int fx = x + 3;
    Plat fin{fx, fx + 6, F, C_JUMP, '#', SetPiece::None, 0, fx + 1};
    pl.push_back(fin);
    wOut = fx + 7;
    out.exitRow = F - 1;
}

// The Pirate Ship is a fleet: ships on the sea, each a run of deck with a raised stern castle and a stepped bow, and open
// water between them. On a deck you meet open hatches (spikes below), cargo, and masts. A mast with a barricade beside it
// is a shaft to climb; a mast with yardarms of shrinking length is a ladder, and between the ships a rigging rope runs from
// one masthead to the next. Yardarms cross a mast, ropes join masts, and every mast has a tunnel at its foot.
static void BuildFleet(Grid& g, std::vector<Plat>& pl, Rng& rng, GenLevel& out, const Params& P, int& wOut) {
    const int H = g.h;
    const int D0 = H - 20;
    Plat start{2, 8, D0, C_START, '#', SetPiece::None, 0, 3};
    pl.push_back(start);
    auto mast = [&](int mx, int deckRow, int top) { // two planks wide, with a tunnel at its foot
        g.rect(mx, top, mx + 1, deckRow - 1, '|');
        g.rect(mx, deckRow - 3, mx + 1, deckRow - 1, '.');
    };
    auto yard = [&](int cx, int row, int half) { g.rect(cx + 1 - half, row, cx + half, row, '='); };
    int x = 0, ship = 0, guard = 0, prevD = D0, prevBowX = -1, prevBowD = D0;
    bool bridgePending = false; int pendingMastX = 0, pendingRow = 0, pendingHalf = 2;
    while (x < P.length && guard++ < 12) {
        int len = rng.I(36, 46);
        int sx = x, ex = sx + len - 1;
        int Ds = ship == 0 ? D0 : std::clamp(prevD + rng.I(-1, 1), D0 - 2, D0 + 2);
        bool isLast = x + len >= P.length - 8;
        g.rect(sx, Ds, ex, Ds + 4, '#');                                       // the hull: deck planking over timber
        if (ship > 0) g.rect(sx, Ds - 3, sx + 3, Ds - 1, '#');                  // the stern castle, raised
        if (!isLast) { g.rect(ex - 2, Ds - 1, ex, Ds - 1, '#'); g.rect(ex - 1, Ds - 2, ex, Ds - 2, '#'); g.set(ex, Ds - 3, '#'); } // the bow, stepping up
        int cx = sx + (ship == 0 ? 9 : 6);
        // ---- if a rope bridge arrives here, drop its far mast onto this ship
        if (bridgePending) {
            int mB = sx + 7;
            mast(mB, Ds, pendingRow);
            int nB = 4;
            for (int i = 1; i <= nB; i++) yard(mB, Ds - 3 * i, std::max(2, 5 - i));
            yard(mB, pendingRow, pendingHalf);
            for (int xx = pendingMastX + pendingHalf + 1; xx < mB + 1 - pendingHalf; xx++) g.set(xx, pendingRow, 'r'); // the rigging rope
            for (int xx = pendingMastX + pendingHalf + 6; xx < mB - pendingHalf - 4; xx += 9) if (g.get(xx, pendingRow - 5) == '.') g.set(xx, pendingRow - 5, 'p');   // parakeets over the rope
            for (int xx = pendingMastX + pendingHalf + 2; xx < mB - pendingHalf - 1; xx += 3) if (g.get(xx, pendingRow - 1) == '.') g.set(xx, pendingRow - 1, 'o');   // coins strung along the rope
            Plat ropeEnd{mB - pendingHalf - 1, mB - pendingHalf - 1, pendingRow, C_JUMP, 'r', SetPiece::None, 0, mB - pendingHalf - 1};
            pl.push_back(ropeEnd);
            Plat land{mB + pendingHalf + 2, mB + pendingHalf + 4, Ds, C_JUMP, '#', SetPiece::None, 0, mB + pendingHalf + 2};
            pl.push_back(land);
            bridgePending = false;
            cx = mB + 8;
        } else if (ship > 0) { // arrived by a jump: a waypoint on the stern castle
            Plat cap{sx, sx + 3, Ds - 3, C_JUMP, '#', SetPiece::None, 0, sx + 1};
            pl.push_back(cap);
        }
        if (ship == 0) { Plat d0{x + 4, x + 8, Ds, C_JUMP, '#', SetPiece::None, 0, x + 5}; (void)d0; }
        // ---- the deck's obstacles
        int last = ex - 12;
        int bridgeAt = (rng.C(0.55f) && x + len < P.length - 40) ? 1 : 0;
        if (bridgeAt) last = ex - 16;
        while (cx < last) {
            int kind = rng.C(0.34f) ? 0 : rng.C(0.5f) ? 1 : rng.I(2, 3);        // hatches and barricades dominate: crossing a ship is a fight
            const int need[4] = {12, 20, 9, 10};                              // the widest each segment can grow, with its run-off
            if (cx + need[kind] > last) kind = 2;                              // not enough deck left: something small
            if (cx + need[kind] > last) break;
            if (kind == 0) { // an open hatch, spikes in the hold
                int gw = P.safety > 0.85f ? rng.I(3, 4) : 3;
                g.rect(cx, Ds, cx + gw - 1, Ds + 3, '.'); g.rect(cx, Ds + 4, cx + gw - 1, Ds + 4, 'x');
                if (rng.C(0.5f)) g.set(cx + gw / 2, Ds - 4, 'g');                      // a spiked ball hung over the hatch (Hard): a low or a high arc
                for (int q = 0; q < gw; q++) if (g.get(cx + q, Ds - 3 - (q == gw / 2 ? 2 : 0)) == '.' && rng.C(0.5f)) g.set(cx + q, Ds - 3 - (q == gw / 2 ? 2 : 0), 'o');
                Plat after{cx + gw, cx + gw + 2, Ds, C_JUMP, '#', SetPiece::None, 0, cx + gw};
                Plat before{cx - 3, cx - 1, Ds, C_JUMP, '#', SetPiece::None, 0, cx - 1};
                pl.push_back(before); pl.push_back(after);
                cx += gw + 5;
            } else if (kind == 1) { // a mast and a barricade: a shaft to climb
                int iw = rng.I(3, 4), Hs = std::min(rng.I(P.shaftMin, P.shaftMax), MaxUpShaft(iw, false)), bw = rng.I(3, 5);
                int mx = cx, ix = mx + 2, bx = ix + iw;
                if (Ds - Hs < 6) { cx += 6; continue; }
                mast(mx, Ds, Ds - Hs - 9);                                       // the mast, the left wall of the shaft
                g.rect(bx, Ds - Hs, bx + bw - 1, Ds - 1, '#');                   // the barricade of cargo, the right wall
                yard(mx, Ds - Hs - 9, 5);
                Plat inside{ix, ix + iw - 1, Ds, C_JUMP, '#', SetPiece::None, 0, ix};
                Plat top{bx, bx + bw - 1, Ds - Hs, C_SHAFT_UP, '#', SetPiece::ShaftUp, 0, bx + 1};
                Plat down{bx + bw, bx + bw + 2, Ds, C_JUMP, '#', SetPiece::None, 0, bx + bw + 1};
                pl.push_back(inside); pl.push_back(top); pl.push_back(down);
                out.setPieces[(int)SetPiece::ShaftUp]++;
                if (bw >= 4 && g.get(bx + bw - 1, Ds - Hs - 1) == '.' && g.get(bx + bw - 2, Ds - Hs - 1) == '.') { g.set(bx + bw - 1, Ds - Hs - 1, 'G'); g.set(bx + bw - 2, Ds - Hs - 1, 'k'); }   // a gunner on the barricade, behind a barrel, covering the deck beyond
                else if (rng.C(0.5f) && g.get(bx + bw - 2, Ds - Hs - 1) == '.' && bw >= 4) g.set(bx + bw - 2, Ds - Hs - 1, 'c');
                cx = bx + bw + 5;
            } else if (kind == 2) { // cargo: a crate or two to hop
                int h = rng.I(1, 2);
                for (int k = 1; k <= h; k++) g.set(cx, Ds - k, 'k');
                if (rng.C(0.6f)) { g.set(cx + 1, Ds - 1, 'k'); }
                cx += 6;
            } else { // a standing mast with yardarms out of reach: dressing, a tunnel at its foot
                mast(cx, Ds, Ds - 22);
                yard(cx, Ds - 14, 6); yard(cx, Ds - 20, 4);
                cx += 7;
            }
            for (int off : {2, 4}) if (rng.C(0.55f) && g.get(cx - off, Ds) == '#' && g.get(cx - off, Ds - 1) == '.' && g.get(cx - off, Ds - 2) == '.') g.set(cx - off, Ds - 1, 'P');
            if (rng.C(0.25f) && g.get(cx - 6, Ds) == '#' && g.get(cx - 6, Ds - 1) == '.') g.set(cx - 6, Ds - 1, 'c');
            if (rng.C(0.6f) && g.get(cx - 1, Ds - 1) == '.') g.set(cx - 1, Ds - 1, 'o');
        }
        // ---- leaving the ship
        prevBowX = ex; prevBowD = Ds; prevD = Ds;
        if (isLast) { x = ex + 1; ship++; break; }
        if (bridgeAt) { // a mast ladder up to a rope that runs across a gap too wide to jump
            int mA = ex - 10, n = rng.I(3, 4);
            int gap = rng.I(9, 11);
            mast(mA, Ds, Ds - 3 * n);
            std::vector<Plat> ladder;
            for (int i = 1; i <= n; i++) {
                int half = std::max(2, 5 - i), row = Ds - 3 * i;
                yard(mA, row, half);
                if (g.get(mA + half, row - 1) == '.') g.set(mA + half, row - 1, 'o');
                ladder.push_back(Plat{mA + 1 - half, mA + half, row, C_JUMP, '=', i == n ? SetPiece::MastLadder : SetPiece::None, 0, mA + 1 - half});
            }
            Plat at{mA - 4, mA - 2, Ds, C_JUMP, '#', SetPiece::None, 0, mA - 3};
            pl.push_back(at);
            for (auto& l : ladder) pl.push_back(l);
            bridgePending = true; pendingMastX = mA; pendingRow = Ds - 3 * n; pendingHalf = std::max(2, 5 - n);
            out.setPieces[(int)SetPiece::MastLadder]++; out.setPieces[(int)SetPiece::ShipGap]++;
            x = ex + 1 + gap;
        } else { // a gap a good jump across, between a bow and the next stern castle
            int rise = 0;
            JumpArc a = CalculateValidJumpArc(rise * kin::TILE);
            int gm = std::max(2, (int)std::floor((a.maxReach * P.safety - 12) / kin::TILE));
            int gap = rng.I(std::max(3, gm - 1), gm);
            Plat bow{ex - 1, ex, Ds - 2, C_JUMP, '#', SetPiece::None, 0, ex - 1};
            pl.push_back(bow);
            out.setPieces[(int)SetPiece::ShipGap]++;
            x = ex + 1 + gap;
        }
        ship++;
    }
    (void)prevBowX; (void)prevBowD;
    // the last ship: a long flat deck to meet the arena
    Plat fin{x - 8, x - 2, prevD, C_JUMP, '#', SetPiece::None, 0, x - 6};
    if (ship > 0) { fin.x0 = std::max(fin.x0, 1); }
    pl.push_back(fin);
    wOut = x;
    out.exitRow = prevD - 1;
}
// ---------------------------------------------------------------------------- the generator
GenLevel GenerateLevel(int level, unsigned seed, float scale) {
    Params P = ParamsFor(level);
    P.safety = std::min(0.97f, P.safety * scale);
    Rng rng(seed * 2654435761u + (unsigned)level * 97u + 12345u);
    const int W = P.length + 48;
    Grid g(W, P.H, P.enclosed ? '#' : '.');
    std::vector<Plat> pl;
    GenLevel out;
    out.setPieces.assign(12, 0);
    out.safety = P.safety;
    out.enclosed = P.enclosed;
    const int yLo = P.enclosed ? 10 : 9, yHi = P.H - 10;

    // ---- painting a platform into the grid
    auto paint = [&](const Plat& p) {
        if (P.enclosed) {
            g.rect(p.x0, p.y - 6, p.x1, p.y - 1, '.');                      // headroom for a full jump
            if (p.ch != '#') {                                               // a pipe or scaffold hangs over a pit
                g.rect(p.x0, p.y + 1, p.x1, p.y + 2, '.');
                g.rect(p.x0, p.y + 3, p.x1, p.y + 3, 'x');
                g.rect(p.x0, p.y, p.x1, p.y, p.ch);
                if (p.ch == '=' && p.x1 - p.x0 >= 2) { int sx = p.x0 + (p.x1 - p.x0) / 2; g.rect(sx, p.y + 1, sx, p.y + 3, '|'); } // a riser down to the duct floor: the run branches off the network
            }
        } else {
            int thick = p.ch != '#' ? 1 : (p.deck ? 3 : 2);
            for (int x = p.x0; x <= p.x1; x++) for (int t = 0; t < thick; t++) g.set(x, p.y + t, t == 0 ? p.ch : '#');
        }
    };
    // ---- the air between two platforms (only matters when everything else is solid)
    auto carveGap = [&](const Plat& a, const Plat& b) {
        if (!P.enclosed) return;
        int lo = std::min(a.y, b.y), hi = std::max(a.y, b.y);
        g.rect(a.x1 + 1, lo - 6, b.x0 - 1, hi + 2, '.');
        g.rect(a.x1 + 1, hi + 3, b.x0 - 1, hi + 3, 'x');
    };
    auto push = [&](Plat p, const Plat* from) { if (from) carveGap(*from, p); paint(p); pl.push_back(p); };

    Plat first{2, 9, P.H / 2, C_START, '#', SetPiece::None, 0, 3};
    paint(first);
    pl.push_back(first);

    // ---- an ordinary hop, sized from the arc
    auto gmaxFor = [&](int dy) {
        JumpArc a = CalculateValidJumpArc(dy * kin::TILE);
        return a.reachable ? (int)std::floor((a.maxReach * P.safety - 12) / kin::TILE) : -1;
    };
    auto addJump = [&](int wMin, int wMax, bool deck = false) {
        Plat c = pl.back();
        std::vector<int> pool;
        for (int dy = -P.maxDrop; dy <= P.maxRise; dy++) {
            int ny = c.y - dy;
            if (ny < yLo || ny > yHi || gmaxFor(dy) < P.gapMin) continue;
            int wgt = dy == 0 ? 4 : std::abs(dy) == 1 ? 3 : 2;
            if (level == 0 && dy < 0) wgt = 1;
            for (int k = 0; k < wgt; k++) pool.push_back(dy);
        }
        int dy = pool.empty() ? 0 : pool[rng.I(0, (int)pool.size() - 1)];
        int gm = std::max(gmaxFor(dy), P.gapMin);
        int gap = rng.I(P.safety > 0.9f ? std::max(P.gapMin, gm - 1) : P.gapMin, gm);
        int w = rng.I(wMin, wMax);
        bool small = w <= 3 && !deck;
        Plat n{c.x1 + 1 + gap, c.x1 + gap + w, c.y - dy, C_JUMP, '#', SetPiece::None, gap, 0};
        n.deck = deck;
        if (P.enclosed) {
            n.ch = (w >= 2 && rng.C(0.45f)) ? '=' : '#';
            if (n.ch == '=' && c.ch == '=' && dy == 0 && rng.C(0.4f)) { n.x0 = c.x1 + 1; n.x1 = n.x0 + w - 1; n.gap = 0; }   // the run simply continues, joined at a flange
        }
        else if (small) n.ch = '=';
        n.wx = n.x0;
        push(n, &c);
    };

    // ---- set-pieces: the terrain adapts its spacing to the mechanic
    auto steamBoost = [&]() {
        Plat c = pl.back();
        int ny = c.y - 5;
        if (c.x1 - c.x0 < 2 || ny < yLo || c.conn == C_CRUMBLE || c.ch != '#') return false;
        g.set(c.x1, c.y, 'v'); // the vent, set into the floor at the platform's end
        Plat n{c.x1 + 1, c.x1 + 5, ny, C_STEAM, '#', SetPiece::SteamBoost, 0, c.x1 + 2};
        push(n, nullptr);
        g.set(c.x1, c.y, 'v');
        // the shaft above the vent, open up to the ledge
        g.rect(c.x1, ny - 6, c.x1, c.y - 1, '.');
        out.setPieces[(int)SetPiece::SteamBoost]++;
        return true;
    };
    auto crumbleRun = [&]() {
        int n = rng.I(3, 4);
        for (int k = 0; k < n; k++) {
            Plat c = pl.back();
            Plat f{c.x1 + 3, c.x1 + 3, c.y, C_CRUMBLE, 'f', k == 0 ? SetPiece::CrumbleRun : SetPiece::None, 2, 0};
            f.wx = f.x0;
            push(f, &c);
        }
        out.setPieces[(int)SetPiece::CrumbleRun]++;
        return true;
    };
    auto gearGauntlet = [&]() {
        Plat c = pl.back();
        int dy = 0, gm = gmaxFor(dy);
        if (gm < 3 || c.ch == 'f') return false;
        Plat n{c.x1 + 1 + gm - 1, c.x1 + gm + 5, c.y, C_JUMP, '#', SetPiece::GearGauntlet, gm - 1, 0};
        n.wx = n.x0;
        push(n, &c);
        out.setPieces[(int)SetPiece::GearGauntlet]++;
        return true;
    };
    auto pipeDrop = [&]() { // a vertical pipe carries the run down to a lower one: a drop with an elbow at the bottom
        Plat c = pl.back();
        int D = rng.I(5, 9), iw = 3;
        if (c.ch == 'f' || c.y + D > yHi || c.conn == C_STEAM) return false;
        g.rect(c.x1 + 1, c.y - 1, c.x1 + iw, c.y + D - 1, '.');                                   // the chamber
        Plat n{c.x1 + 1, c.x1 + iw + rng.I(3, 5), c.y + D, C_JUMP, '=', SetPiece::PipeDrop, 0, c.x1 + 2};
        push(n, nullptr);
        g.rect(c.x1, c.y + 1, c.x1, c.y + D - 1, '|');                                            // the riser: it meets the lower run in an elbow
        out.setPieces[(int)SetPiece::PipeDrop]++;
        return true;
    };
    auto shipGap = [&]() {
        // two big decks facing each other across a gap at the very edge of the arc: a ship-to-ship leap
        addJump(6, 7, true);
        Plat c = pl.back();
        int gm = gmaxFor(0);
        if (c.y + 4 > yHi || gm < 4) return true;
        Plat n{c.x1 + 1 + gm, c.x1 + gm + 6, c.y, C_JUMP, '#', SetPiece::ShipGap, gm, 0};
        n.deck = true;
        n.wx = n.x0;
        push(n, &c);
        out.setPieces[(int)SetPiece::ShipGap]++;
        return true;
    };

    // ---- a vertical shaft: two walls to wall-jump between (Hull and Pirate Ship only)
    auto shaft = [&](bool barnacle) {
        Plat c = pl.back();
        if (c.ch == 'f') return false;
        int iw = level == 2 ? rng.I(3, 4) : 3, Hs = rng.I(P.shaftMin, P.shaftMax), yb = c.y;
        bool up = rng.C(0.6f);
        if (up) Hs = std::min(Hs, MaxUpShaft(iw, barnacle));
        if (up && yb - Hs < yLo) up = false;
        if (!up && (yb + Hs > yHi || yb - 8 < 2)) { Hs = std::min(Hs, MaxUpShaft(iw, barnacle)); if (yb - Hs >= yLo) up = true; else return false; }
        Plat n = CarveShaft(g, c, up, iw, Hs, barnacle, rng.I(0, 2));
        pl.push_back(n);
        out.setPieces[(int)n.tag]++;
        return true;
    };
    int w = 0;
    if (level != 0) {
        pl.clear();
        g = Grid(W, P.H, '.');
        if (level == 1) BuildTrench(g, pl, rng, out, P, w); else BuildFleet(g, pl, rng, out, P, w);
    } else {
    // ---- pass 1: the critical path
    int guard = 0;
    while (pl.back().x1 < P.length && guard++ < 400) {
        bool did = false;
        if (rng.C(P.setChance)) {
            if (level == 0) { int k = rng.I(0, 3); did = k == 0 ? steamBoost() : k == 1 ? crumbleRun() : k == 2 ? gearGauntlet() : pipeDrop(); }
            else if (level == 1) did = shaft(true);
            else did = shipGap();
        }
        if (!did && P.shaftChance > 0 && rng.C(P.shaftChance)) did = shaft(false);
        if (!did) addJump(P.wMin, P.wMax);
    }
    addJump(7, 7, true); // the landing flush against the arena or the exit
    Plat& last = pl.back();
    last.deck = true;
    for (int x = last.x0; x <= last.x1; x++) for (int t = 0; t < 3; t++) g.set(x, last.y + t, '#');
    if (P.enclosed) g.rect(last.x0, last.y - 6, last.x1, last.y - 1, '.');
    out.exitRow = last.y - 1;
    w = last.x1 + 1;

    // ---- pass 2: hazards, enemies and coins on the surfaces that exist
    auto air = [&](int x, int y) { return g.get(x, y) == '.'; };
    auto arcHeightAt = [&](const JumpArc& a, float dist) {
        for (size_t i = 1; i < a.samples.size(); i++)
            if (a.samples[i].x >= dist) {
                float t = (dist - a.samples[i - 1].x) / std::max(0.01f, a.samples[i].x - a.samples[i - 1].x);
                return a.samples[i - 1].y + (a.samples[i].y - a.samples[i - 1].y) * t;
            }
        return 0.0f;
    };
    for (size_t i = 1; i + 1 < pl.size(); i++) {
        const Plat& p = pl[i];
        const Plat& prev = pl[i - 1];
        int wdt = p.x1 - p.x0 + 1;
        if (p.conn == C_JUMP && p.gap >= 2) {
            int rise = prev.y - p.y;
            JumpArc a = CalculateValidJumpArc(rise * kin::TILE, 32);
            if (a.reachable) {
                int gc = prev.x1 + 1 + p.gap / 2; // the gap's middle column
                auto rowFor = [&](float frac) {
                    float dist = (p.gap * kin::TILE) * frac + 6;
                    float hgt = arcHeightAt(a, dist);
                    return (int)std::lround((prev.y * kin::TILE - 16 - hgt - 13) / kin::TILE);
                };
                for (float fr : {0.3f, 0.55f, 0.8f}) { // coins along the arc
                    int cx = prev.x1 + 1 + (int)(p.gap * fr), cy = rowFor(fr);
                    if (air(cx, cy)) g.set(cx, cy, 'o');
                }
                float hazardChance = level == 0 ? 0.45f : level == 1 ? 0.30f : 0.45f;
                if (p.tag == SetPiece::GearGauntlet) hazardChance = 1.0f;
                if (p.gap >= 3 && p.gap <= gmaxFor(rise) - 1 && rng.C(hazardChance)) { // a hazard hung in the arc's middle
                    int hy = rowFor(0.5f);
                    if (air(gc, hy) && (!P.enclosed || hy >= std::min(prev.y, p.y) - 5)) g.set(gc, hy, 'g');
                }
                if (level >= 1 && p.gap >= 3 && rng.C(level == 1 ? 0.28f : 0.22f)) g.set(gc, std::max(prev.y, p.y), 'e');       // a leaping eel below
                if (level >= 1 && p.gap >= 3 && rng.C(level == 1 ? 0.12f : 0.25f) && air(gc, std::min(prev.y, p.y) - 5)) g.set(gc, std::min(prev.y, p.y) - 5, 'p'); // a parakeet over the arc's top
                if (level == 2 && p.gap >= 3 && i >= 2 && rng.C(0.35f)) { // a gunner's perch high above the arc
                    int py = std::min(prev.y, p.y) - 8;
                    if (py > 3 && air(gc - 1, py) && air(gc, py) && air(gc + 1, py) && air(gc - 1, py - 1) && air(gc, py - 1)) {
                        g.rect(gc - 1, py, gc + 1, py, '=');
                        g.set(gc - 1, py - 1, 'k');
                        g.set(gc, py - 1, 'G');
                    }
                }
            }
        }
        if (p.ch == 'f' || p.conn == C_STEAM) continue;
        // on the surface itself
        int mid = p.x0 + wdt / 2;
        if (wdt >= 3 && air(mid, p.y - 1) && rng.C(0.5f)) g.set(mid, p.y - 1, 'o');
        if (level == 0) {
            if (wdt >= 6 && rng.C(0.4f)) g.set(p.x0 + wdt / 2, p.y, 't');                // a timed jet in the floor
            else if (wdt >= 7 && rng.C(0.3f) && p.ch == '#') g.set(p.x0 + 2, p.y, 'x'); // a steam grate to hop over
        } else if (level == 1) {
            if (wdt >= 4 && rng.C(0.45f) && air(p.x0 + 1, p.y - 1)) g.set(p.x0 + 1, p.y - 1, 'c');
            else if (wdt >= 5 && rng.C(0.3f) && p.ch == '#') g.set(p.x0 + wdt / 2, p.y, 'x');
        } else {
            if (wdt >= 3 && rng.C(0.4f) && air(p.x1 - 1, p.y - 1) && air(p.x1 - 1, p.y - 2)) g.set(p.x1 - 1, p.y - 1, 'P');
            if (wdt >= 5 && rng.C(0.3f) && air(p.x0 + 2, p.y - 1)) g.set(p.x0 + 2, p.y - 1, 'k');
            if (wdt >= 4 && rng.C(0.4f) && air(p.x0 + 1, p.y - 1) && g.get(p.x0 + 1, p.y - 1) == '.') g.set(p.x0 + 1, p.y - 1, 'c');
        }
    }

    if (P.enclosed) g.set(last.x0 + 3, last.y - 1, 'E');
    }
    // ---- the start, the exit and the finished grid
    g.set(pl[0].wx, pl[0].y - 1, 'S');
    for (auto& r : g.r) r.resize(w);
    out.w = w;
    out.h = P.H;
    out.rows = g.r;
    for (const Plat& p : pl) out.path.push_back({p.wx, p.y - 1, p.tag});
    return out;
}
