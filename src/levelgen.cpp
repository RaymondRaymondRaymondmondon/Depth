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
        case 0: return {0.70f, 230, 40, 4, 9, 2, 3, 1, true, 0.0f, 0, 0, 0.48f};     // the Pipes: still forgiving, no wall jumps, but tighter gaps and denser set-pieces than before
        case 1: return {0.84f, 300, 64, 2, 5, 3, 4, 2, false, 0.42f, 9, 14, 0.27f};   // the Hull: verticality, shafts, footholds
        case 2: return {0.95f, 310, 64, 1, 3, 3, 4, 3, false, 0.38f, 10, 14, 0.24f}; // the Pirate Ship: tiny footholds at the arc's edge
        case 3: return {0.91f, 340, 64, 1, 3, 3, 4, 3, false, 0.38f, 10, 15, 0.30f}; // the Island: harder than the Pirate Ship - narrower canopy footholds
        case 4: return {0.93f, 350, 64, 1, 3, 3, 5, 3, false, 0.44f, 10, 15, 0.34f}; // the Cave: harder than the Island - a denser run of shaft climbs, in the dark
        case 5: return {0.95f, 360, 64, 1, 3, 3, 5, 3, false, 0.40f, 10, 15, 0.36f}; // the Weeds: harder again - most of the way is kelp floats at the arc's edge
        default: return {0.97f, 380, 64, 1, 3, 3, 5, 3, false, 0.44f, 10, 15, 0.38f}; // Atlantis: the hardest - tight masonry hops, in and out of the drowned buildings
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
// The Hull is the top of a submarine, and you cross it. The deck is a long run of steel plating with the superstructure built
// on it, and between the features (which are chosen at random, never the same twice in a row) there is plain deck to catch your
// breath. The features are what make each crossing different:
//   conning tower  the old climb: through a hatch at the foot of a tower, up a shaft between it and a lower housing, over its
//                  roof and down the far side
//   torpedo gap    a breach in the hull too wide to step over, with a torpedo tube on a gun tower across it firing down the
//                  lane you must jump through
//   live plating   a stretch of deck with electrified plates that spark on a timer: cross them between surges
//   limpet mines   spikes on the deck under mines hung at the height of a big jump: a low, careful hop
//   rotten grating a breach bridged by corroded grating that gives way half a second after you land: keep moving
//   rock reef      the hull lies among rocks: a breach crossed by stepping stones of rock (this is where the coral grows), with
//                  urchins, crabs and an eel leaping from the water below
// Everything is joined by plain deck, so nothing floats: every tile belongs to the hull, the rocks or a fixture on them.
static void BuildTrench(Grid& g, std::vector<Plat>& pl, Rng& rng, GenLevel& out, const Params& P, int& wOut) {
    const int H = g.h, F = H - 6;
    g.rect(0, F, g.w - 1, H - 1, '#');       // the hull's deck and everything under it
    g.rect(0, 0, g.w - 1, 1, '#');           // the sea's surface overhead
    auto column = [&](int x0, int width, int top, bool tunnel) {
        g.rect(x0, top, x0 + width - 1, H - 1, '#');
        if (tunnel) g.rect(x0, F - 3, x0 + width - 1, F - 1, '.');
    };
    Plat start{2, 9, F, C_START, '#', SetPiece::None, 0, 3};
    pl.push_back(start);
    JumpArc a0 = CalculateValidJumpArc(0);
    int gm = std::max(3, (int)std::floor((a0.maxReach * P.safety - 12) / kin::TILE));   // the widest breach a plain jump clears
    int x = 12, lastKind = -1, towers = 0, guard = 0;
    while (x < P.length && guard++ < 60) {
        // ---- choose a feature: weighted, and never the one before; towers no more than one in three
        int kind;
        for (int tries = 0;; tries++) {
            int r = rng.I(0, 99);
            kind = r < 13 ? 0 : r < 28 ? 1 : r < 41 ? 2 : r < 53 ? 3 : r < 64 ? 4 : r < 76 ? 5 : r < 88 ? 6 : 7;
            if (kind != lastKind && !(kind == 0 && towers > 0 && guard % 3 != 0) && (kind != 0 || x > 40)) break;
            if (tries > 12) { kind = (lastKind + 1) % 8; break; }
        }
        if (kind == 0) towers = 3; else if (towers > 0) towers--;
        lastKind = kind;
        int coinRow = F - 2;
        switch (kind) {
            case 0: { // the conning tower climb
                int iw1 = rng.I(3, 4), iw2 = rng.I(3, 4), sw = rng.I(5, 7);
                bool barnacle = rng.C(0.35f);
                int Hs = std::min(rng.I(P.shaftMin, P.shaftMax + 1), MaxUpShaft(iw1, barnacle));
                column(x, 3, 0, true);                                              // the tower itself, with a hatch through its foot
                int upX0 = x + 3, sX0 = upX0 + iw1, sTop = F - Hs, t2X0 = sX0 + sw + iw2;
                column(sX0, sw, sTop, false);                                       // a lower housing beside it: its roof is the way over
                column(t2X0, 3, 0, true);                                           // the next bulkhead, with its own hatch
                if (barnacle) { g.rect(x + 2, sTop - 1, x + 2, F - 4, 'b'); g.rect(sX0, sTop + 1, sX0, F - 1, 'b'); }
                int shelfY = sTop - 12;
                if (shelfY > 3) { g.rect(upX0, shelfY, upX0 + 2, shelfY, '#'); for (int k = 0; k < 3; k += 2) g.rect(upX0 + k, shelfY + 1, upX0 + k, shelfY + 4, 'l'); } // a gantry with a chain ladder hanging from it
                for (int xx = sX0 + sw; xx <= t2X0 - 3; xx++) g.set(xx, F, 'x');
                for (int k = 0; k < 3; k++) { int cy = F - 3 - (Hs - 4) * (k + 1) / 4; if (cy < F && g.get(upX0 + iw1 / 2, cy) == '.') g.set(upX0 + iw1 / 2, cy, 'o'); }
                for (int xx = sX0 + 1; xx < sX0 + sw - 1; xx += 2) if (g.get(xx, sTop - 1) == '.') g.set(xx, sTop - 1, 'o');
                if (sw >= 6 && rng.C(0.5f)) { g.set(sX0 + 3, sTop, 'x'); if (rng.C(0.6f) && g.get(sX0 + 3, sTop - 4) == '.') g.set(sX0 + 3, sTop - 4, 'g'); }
                else if (sw >= 5 && rng.C(0.5f) && g.get(sX0 + sw - 2, sTop - 1) == '.') g.set(sX0 + sw - 2, sTop - 1, 'c');
                Plat entry{upX0, upX0 + iw1 - 1, F, C_JUMP, '#', SetPiece::None, 0, upX0};
                Plat plateau{sX0, sX0 + sw - 1, sTop, C_SHAFT_UP, '#', barnacle ? SetPiece::BarnacleShaft : SetPiece::ShaftUp, 0, sX0 + 1};
                Plat bottom{t2X0 - 2, t2X0 - 1, F, C_SHAFT_DOWN, '#', SetPiece::ShaftDown, 0, t2X0 - 1};
                pl.push_back(entry); pl.push_back(plateau); pl.push_back(bottom);
                out.setPieces[(int)plateau.tag]++; out.setPieces[(int)SetPiece::ShaftDown]++;
                x = t2X0 + 3;
            } break;
            case 1: { // torpedo gap
                int gw = std::clamp(gm - 1, 4, 6);
                for (int xx = x; xx < x + gw; xx++) g.set(xx, F, 'x');              // the hull stays whole: a bed of limpets and urchins on the deck to hop
                if (gw >= 4 && rng.C(0.3f)) g.set(x + gw / 2, F, 'e');              // sometimes a leaping eel lurks in the bed instead of another urchin
                int tx = x + gw + 6;
                g.rect(tx, F - 2, tx + 1, F - 1, '#');
                g.set(tx, F - 1, 'T');                                              // the tube, low on the tower, firing along the deck's lane
                Plat before{x - 3, x - 1, F, C_JUMP, '#', SetPiece::None, 0, x - 1};
                Plat after{x + gw, x + gw + 3, F, C_JUMP, '#', SetPiece::None, 0, x + gw};
                pl.push_back(before); pl.push_back(after);
                for (int k = 1; k <= 2; k++) g.set(x + gw * k / 3, F - 4, 'o');
                out.setPieces[(int)SetPiece::ShipGap]++;
                x = tx + 3;
            } break;
            case 2: { // live plating
                int len = 26;
                for (int i = 4; i < len - 2; i += 6) { g.set(x + i, F, 't'); g.set(x + i + 1, F, 't'); g.set(x + i, F - 4, 'o'); }
                Plat run{x, x + len - 1, F, C_JUMP, '#', SetPiece::None, 0, x + 1};
                pl.push_back(run);
                x += len;
            } break;
            case 3: { // limpet mines over spikes
                int len = 26;
                for (int i = 5; i < len - 3; i += 5) { g.set(x + i, F, 'x'); g.set(x + i, F - 5, 'g'); g.set(x + i + 2, coinRow - 1, 'o'); }
                Plat run{x, x + len - 1, F, C_JUMP, '#', SetPiece::None, 0, x + 1};
                pl.push_back(run);
                out.setPieces[(int)SetPiece::GearGauntlet]++;
                x += len;
            } break;
            case 4: { // rotten grating
                int len = 14;
                for (int xx = x; xx < x + len; xx++) g.set(xx, F, 'x');            // a spiked bed on the deck, with corroded grating laid over it
                for (int px2 : {x + 1, x + 5, x + 9}) {
                    g.rect(px2, F - 1, px2 + 1, F - 1, 'f');
                    Plat plate{px2, px2 + 1, F - 1, C_JUMP, 'f', SetPiece::CrumbleRun, 0, px2};
                    pl.push_back(plate);
                }
                Plat after{x + len, x + len + 2, F, C_JUMP, '#', SetPiece::None, 0, x + len};
                pl.push_back(after);
                out.setPieces[(int)SetPiece::CrumbleRun]++;
                x += len;
            } break;
            case 6: { // cavern detour: a reef wall stands on the deck and runs to the surface. The way on is in through a low doorway,
                      // up a wall-jump chimney to a tunnel high in the rock, along it, and down a drop shaft to a doorway on the far side.
                      // Nothing goes below the deck: the hull is never breached.
                int Hs = 6, tl = rng.I(12, 18);
                int dx = x + 8 + tl, bx1 = dx + 5;                                   // the drop shaft, and the reef's far face
                Plat before{x, x + 2, F, C_JUMP, '#', SetPiece::None, 0, x + 1};
                g.rect(x + 3, 2, bx1, F - 1, 'R');                                   // the reef, where the coral grows
                g.rect(x + 3, F - 3, x + 4, F - 1, '.');                             // the entry doorway
                g.rect(x + 5, F - Hs, x + 7, F - 1, '.');                            // the chimney
                g.rect(x + 5, F - 10, dx - 1, F - 7, '.');                           // the tunnel, four tiles high
                g.rect(dx, F - 10, dx + 2, F - 1, '.');                              // the drop shaft
                g.rect(dx + 3, F - 3, bx1, F - 1, '.');                              // the exit doorway
                g.set(x + 8 + rng.I(4, tl - 5), F - Hs, 'x');                        // an urchin bed on the tunnel floor: hop it
                Plat tunnel{x, x + 2, F, C_JUMP, '#', SetPiece::None, 0, x + 1};
                Plat plateau = CarveShaft(g, tunnel, true, 3, Hs, false, dx - 1 - (x + 9));
                Plat after{bx1 + 1, bx1 + 3, F, C_SHAFT_DOWN, '#', SetPiece::ShaftDown, 0, bx1 + 1};
                pl.push_back(before); pl.push_back(plateau); pl.push_back(after);
                out.setPieces[(int)SetPiece::ShaftUp]++; out.setPieces[(int)SetPiece::ShaftDown]++;
                x = bx1 + 4;
            } break;            case 7: { // ballast vent: a steam vent set into the deck lifts you onto a gantry over a bed of urchins; walk off the far end
                int run = 6;
                Plat before{x, x + run - 1, F, C_JUMP, '#', SetPiece::None, 0, x + 1};
                g.set(x + run - 1, F, 'v');
                Plat gantry{x + run, x + run + 4, F - 5, C_STEAM, '#', SetPiece::SteamBoost, 0, x + run + 1};
                for (int xx = gantry.x0; xx <= gantry.x1; xx++) { g.set(xx, F - 5, '#'); g.set(xx, F - 4, '#'); g.set(xx, F, 'x'); }   // the gantry, over a bed of urchins on the deck
                for (int yy = F - 3; yy < F; yy++) { g.set(gantry.x0 + 1, yy, '#'); g.set(gantry.x1 - 1, yy, '#'); }   // its legs
                for (int yy = F - 12; yy < F; yy++) if (g.get(x + run - 1, yy) != '.') g.set(x + run - 1, yy, '.');
                Plat after{x + run + 5, x + run + 8, F, C_JUMP, '#', SetPiece::None, 0, x + run + 5};
                pl.push_back(before); pl.push_back(gantry); pl.push_back(after);
                out.setPieces[(int)SetPiece::SteamBoost]++;
                x += run + 9;
            } break;            default: { // rock reef
                int n = 4, w0 = 3;
                int total = w0 + n * 6;
                int prevTop = F;
                for (int k = 0; k < n; k++) {
                    int sx = x + w0 + k * 6;
                    int top = std::clamp(prevTop + rng.I(-2, 2), F - 3, F - 1);
                    g.rect(sx, top, sx + 2, F - 1, 'R');                             // boulders standing on the deck; urchins fill the gaps between
                    if (k > 0) { for (int xx = sx - 3; xx < sx; xx++) g.set(xx, F, 'x'); if (k == 1 && rng.C(0.3f)) g.set(sx - 2, F, 'e'); } // an eel sometimes lurks between the first two boulders
                    Plat stone{sx, sx + 2, top, C_JUMP, 'R', SetPiece::None, 0, sx + 1};
                    pl.push_back(stone);
                    if (k == 2 && g.get(sx + 2, top - 1) == '.') g.set(sx + 2, top - 1, 'c');
                    prevTop = top;
                }
                Plat after{x + total, x + total + 2, F, C_JUMP, '#', SetPiece::None, 0, x + total};
                pl.push_back(after);
                x += total;
            } break;
        }
        // ---- plain deck to breathe on, with a little to pick up
        int breath = rng.I(5, 8);
        if (rng.C(0.55f)) { // a dense kelp bed growing from the deck: passable, but it swallows the view and drags at a fall
            int kx = x + rng.I(0, std::max(0, breath - 3)), kh = rng.I(3, 5);
            for (int i = 0; i < 3; i++) for (int j = 1; j <= kh - (i == 1 ? 0 : 1); j++) if (g.get(kx + i, F - j) == '.') g.set(kx + i, F - j, 'w');
        }
        x += breath;
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
    auto rig = [&](int mx, int deckRow, int top) { g.rect(mx, top, mx, deckRow - 1, 'l'); }; // ratlines: a rope ladder you climb or walk through, not a wall
    auto yard = [&](int cx, int row, int half) { for (int xx = cx + 1 - half; xx <= cx + half; xx++) if (xx != cx) g.set(xx, row, '='); }; // a spar either side of the ladder, with a gap to climb through
    int x = 0, ship = 0, guard = 0, prevD = D0, prevBowX = -1, prevBowD = D0;
    bool bridgePending = false; int pendingMastX = 0, pendingRow = 0, pendingHalf = 2;
    while (x < P.length && guard++ < 12) {
        int len = rng.I(36, 46);
        int sx = x, ex = sx + len - 1;
        int Ds = ship == 0 ? D0 : bridgePending ? std::clamp(prevD + rng.I(-1, 1), D0 - 2, D0 + 2)
                                                : std::clamp(prevD + rng.I(0, 1), D0 - 2, D0 + 2); // a jumped gap never climbs: the stern castle is as high as the bow, or lower
        bool isLast = x + len >= P.length - 8;
        g.rect(sx, Ds, ex, Ds + 4, '#');                                       // the hull: deck planking over timber
        bool quarter = ship > 0 && !bridgePending; // the ship a rope bridge lands on has a plain deck at that end
        if (quarter) { g.rect(sx, Ds - 2, sx + 8, Ds - 1, '#'); g.rect(sx + 9, Ds - 1, sx + 10, Ds - 1, '#'); } // the quarter deck, raised two tiles, and a step down to the main deck
        if (!isLast) { g.rect(ex - 2, Ds - 1, ex, Ds - 1, '#'); g.rect(ex - 1, Ds - 2, ex, Ds - 2, '#'); g.set(ex, Ds - 3, '#'); } // the bow, stepping up
        int cx = sx + (ship == 0 ? 9 : quarter ? 14 : 6);
        // ---- if a rope bridge arrives here, drop its far mast onto this ship
        if (bridgePending) {
            int mB = sx + 7;
            rig(mB, Ds, pendingRow);
            int i = 0;                                                            // the ladder runs down from the rope's own row, so no yard is stacked on the landing yard
            for (int r = pendingRow + 3; r <= Ds - 3; r += 3) yard(mB, r, std::max(2, 4 - i++));
            yard(mB, pendingRow, pendingHalf);
            for (int xx = pendingMastX + pendingHalf + 1; xx < mB + 1 - pendingHalf; xx++) g.set(xx, pendingRow, 'r'); // the rigging rope
            for (int xx = pendingMastX + pendingHalf + 6; xx < mB - pendingHalf - 4; xx += 9) if (g.get(xx, pendingRow - 5) == '.') g.set(xx, pendingRow - 5, 'p');   // parakeets over the rope
            for (int xx = pendingMastX + pendingHalf + 2; xx < mB - pendingHalf - 1; xx += 3) if (g.get(xx, pendingRow - 1) == '.') g.set(xx, pendingRow - 1, 'o');   // coins strung along the rope
            Plat ropeEnd{mB - pendingHalf - 1, mB - pendingHalf - 1, pendingRow, C_JUMP, 'r', SetPiece::None, 0, mB - pendingHalf - 1};
            pl.push_back(ropeEnd);
            Plat land{mB + pendingHalf + 2, mB + pendingHalf + 4, Ds, C_JUMP, '#', SetPiece::None, 0, mB + pendingHalf + 2};
            pl.push_back(land);
            bridgePending = false;
            cx = mB + 15;                                                          // keep the next ship's masts and yardarms clear of the descent
        } else if (ship > 0) { // arrived by a jump: a waypoint on the stern castle
            Plat cap{sx, sx + 3, Ds - 2, C_JUMP, '#', SetPiece::None, 0, sx + 1};
            pl.push_back(cap);
        }
        if (ship == 0) { Plat d0{x + 4, x + 8, Ds, C_JUMP, '#', SetPiece::None, 0, x + 5}; (void)d0; }
        // ---- the deck's obstacles
        int last = ex - 12;
        int bridgeAt = (rng.C(0.55f) && x + len < P.length - 40) ? 1 : 0;
        if (bridgeAt) last = ex - 16;
        while (cx < last) {
            // 0 hatch, 1 barricade shaft, 2 cargo, 3 dressing mast, 4 gun battery, 5 barrel run, 6 rotten planking, 7 gun crossfire
            // weighted toward the obstacles that demand a real jump or a dodge; cargo and dressing masts (a walk-past)
            // are kept rare so the deck isn't mostly flat ground between ships
            int roll = rng.I(0, 99);
            int kind = roll < 14 ? 0 : roll < 34 ? 1 : roll < 38 ? 2 : roll < 41 ? 3 : roll < 58 ? 4 : roll < 74 ? 5 : roll < 88 ? 6 : 7;
            const int need[8] = {12, 20, 9, 10, 16, 18, 14, 18};              // the widest each segment can grow, with its run-off
            if (cx + need[kind] > last) kind = 2;                              // not enough deck left: something small
            if (cx + need[kind] > last) break;
            if (kind == 0) { // an open hatch, spikes in the hold
                int gw = P.safety > 0.85f ? rng.I(3, 4) : 3;
                g.rect(cx, Ds, cx + gw - 1, Ds, '.'); g.rect(cx, Ds + 1, cx + gw - 1, Ds + 1, 'x'); // a shallow hatch: spikes on the grating below, the hull unbroken beneath
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
            } else if (kind == 4) { // a gun battery: a cannon firing along the deck at hopping height, with crates to shelter behind
                g.set(cx + 3, Ds - 1, 'k');
                g.set(cx + 4, Ds - 1, 'k'); g.set(cx + 4, Ds - 2, 'k');
                g.set(cx + 11, Ds - 1, 'N');
                if (rng.C(0.5f)) { g.set(cx + 8, Ds - 1, 'k'); }
                for (int q = 5; q < 11; q += 2) if (g.get(cx + q, Ds - 3) == '.') g.set(cx + q, Ds - 4, 'o');
                cx += 15;
            } else if (kind == 5) { // a barrel run: a chute ahead lets barrels roll down the deck at you: hop each one
                g.set(cx + 13, Ds - 1, 'y');
                if (g.get(cx + 12, Ds - 1) == '.') g.set(cx + 12, Ds - 1, 'k');
                for (int q = 3; q < 12; q += 3) g.set(cx + q, Ds - 3, 'o');
                cx += 17;
            } else if (kind == 6) { // rotten planking: deck boards over the open hold that give way soon after you step on them
                int len = rng.I(6, 8);
                g.rect(cx, Ds, cx + len - 1, Ds, 'f');
                g.rect(cx, Ds + 1, cx + len - 1, Ds + 1, 'x');
                for (int q = 1; q < len; q += 3) g.set(cx + q, Ds - 3, 'o');
                cx += len + 5;
            } else if (kind == 7) { // gun crossfire: two cannons face each other across the lane, firing on staggered timers so the safe beat keeps changing
                int gw = rng.I(7, 8);
                g.set(cx + 1, Ds - 1, 'N');
                g.set(cx + gw, Ds - 1, 'N');
                for (int q = 3; q < gw - 1; q += 3) if (g.get(cx + q, Ds - 4) == '.') g.set(cx + q, Ds - 4, 'o');
                Plat mid{cx + 2, cx + gw - 1, Ds, C_JUMP, '#', SetPiece::GunCrossfire, 0, cx + 2 + gw / 2};
                pl.push_back(mid);
                out.setPieces[(int)SetPiece::GunCrossfire]++;
                cx += gw + 6;
            } else { // a standing mast with yardarms out of reach: dressing, a tunnel at its foot
                rig(cx, Ds, Ds - 22);
                yard(cx, Ds - 14, 6); yard(cx, Ds - 20, 4);
                cx += 7;
            }
            for (int off : {2, 4}) if (rng.C(0.55f) && g.get(cx - off, Ds) == '#' && g.get(cx - off, Ds - 1) == '.' && g.get(cx - off, Ds - 2) == '.') g.set(cx - off, Ds - 1, 'P');
            if (rng.C(0.25f) && g.get(cx - 6, Ds) == '#' && g.get(cx - 6, Ds - 1) == '.') g.set(cx - 6, Ds - 1, 'c');
            if (rng.C(0.6f) && g.get(cx - 1, Ds - 1) == '.') g.set(cx - 1, Ds - 1, 'o');
        }
        // ---- below decks: cabins under the quarter deck and a corridor beneath the main deck (behind the timbers, not reachable)
        auto room = [&](int a, int b, int r0, int r1) { for (int xx = a; xx <= b; xx++) for (int rr = r0; rr <= r1; rr++) if (g.get(xx, rr) == '#') g.set(xx, rr, 'i'); };
        if (quarter) room(sx + 2, sx + 8, Ds + 1, Ds + 3);
        if (len >= 40) room(sx + 14, ex - 8, Ds + 2, Ds + 3);
        // ---- a snap point for the Grand Kraken: plain deck clear overhead, away from the ends; it may sink a half
        //      unless a rope bridge is tied to that half (a sunk mast would leave the rope hanging a row off)
        if (ship > 0 && !isLast && len >= 38) {
            std::vector<int> cols;
            for (int c = sx + 14; c <= ex - 14; c++) {
                bool ok = true;
                for (int d = -1; d <= 1 && ok; d++) {
                    ok = g.get(c + d, Ds) == '#';
                    for (int r = std::max(0, Ds - 26); r < Ds && ok; r++) ok = g.get(c + d, r) == '.' || g.get(c + d, r) == 'o'; // nothing overhead: no mast, yard, shaft wall or rope
                }
                if (ok) cols.push_back(c);
            }
            if (!cols.empty()) {
                GenSnap sn;
                sn.col = cols[rng.I(0, (int)cols.size() - 1)];
                sn.x0 = sx; sn.x1 = bridgeAt ? ex : ex + 3; sn.top = std::max(0, Ds - 26); sn.bottom = Ds + 4;
                int roll = rng.I(0, 2);
                sn.sink = roll == 0 ? 0 : roll == 1 ? -1 : 1;
                if (sn.sink < 0 && !quarter) sn.sink = 0; // the stern carries the far mast of a rope bridge
                if (sn.sink > 0 && bridgeAt) sn.sink = 0;  // the bow carries the near mast of one
                out.snaps.push_back(sn);
            }
        }
        // ---- leaving the ship
        prevBowX = ex; prevBowD = Ds; prevD = Ds;
        if (isLast) { x = ex + 1; ship++; break; }
        if (bridgeAt) { // a mast ladder up to a rope that runs across a gap too wide to jump
            int mA = ex - 10, n = rng.I(3, 4);
            int gap = rng.I(9, 11);
            rig(mA, Ds, Ds - 3 * n);
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
            g.rect(ex + 1, Ds - 2, ex + 3, Ds - 2, '='); // the bowsprit: a spar jutting over the water, the place to leap from
            Plat bow{ex - 1, ex, Ds - 2, C_JUMP, '#', SetPiece::None, 0, ex - 1};
            Plat tip{ex + 2, ex + 3, Ds - 2, C_JUMP, '=', SetPiece::None, 0, ex + 2};
            pl.push_back(bow); pl.push_back(tip);
            out.setPieces[(int)SetPiece::ShipGap]++;
            x = ex + 4 + gap;
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

// The Island is a tropical shrine ground, not a forest: carved idols and untouched worship sites over a
// solid canopy floor, open sky above - a coast-to-coast crossing that randomizes its theme per seed (variant
// 0 stepped temple, 1 stilt village strung with totems, 2 a ridge of standing statues):
//   idol climb       stepped stone plinths, each carved with a crude glyph, rising to a totem head with
//                     glowing eyes at the top - the level's visual centrepiece, and its most common feature
//   canyon crossing  a mud-and-thorn ravine too wide to step over, with a coiled viper sometimes waiting below
//   plank bridge     a rope-and-plank bridge over a ravine that gives way soon after you step on it
//   vine chimney     a wall-jump shaft between two rock faces laced with thorny vines (climbs like a barnacle shaft)
//   village stand    stepped huts strung with totems and skulls, warriors behind cover, a monitor lizard
//                    patrolling the boards - the second most common feature
//   processional way a straight ceremonial path lined with totem poles, not open hillside - poison-frog mud
//                    patches pulse on a timer between them
// Reuses the Hull/Pirate Ship's own hazard and enemy tiles (x/t/g/e/p/P/G/k/c/b/f/w), redrawn with jungle art
// per-level in platformer.cpp - see CLAUDE.md's rendering notes; no new tile semantics or physics needed.
static void BuildIsland(Grid& g, std::vector<Plat>& pl, Rng& rng, GenLevel& out, const Params& P, int& wOut, int variant) {
    const int H = g.h, F = H - 6;
    g.rect(0, F, g.w - 1, H - 1, '#');       // the jungle floor and everything under it
    auto plinth = [&](int x0, int width, int top) { g.rect(x0, top, x0 + width - 1, H - 1, '#'); };
    Plat start{2, 9, F, C_START, '#', SetPiece::None, 0, 3};
    pl.push_back(start);
    JumpArc a0 = CalculateValidJumpArc(0);
    int gm = std::max(3, (int)std::floor((a0.maxReach * P.safety - 12) / kin::TILE));
    auto gapFor = [&](int riseTiles) { // the widest safe gap for a hop that also climbs riseTiles, same margin as gm above
        JumpArc a = CalculateValidJumpArc(riseTiles * kin::TILE);
        if (!a.reachable) return 1;
        return std::max(1, (int)std::floor((a.maxReach * P.safety - 12) / kin::TILE) - 1);
    };
    int x = 12, lastKind = -1, guard = 0;
    while (x < P.length && guard++ < 60) {
        int kind;
        for (int tries = 0;; tries++) {
            int r = rng.I(0, 99);
            // idol climbs and village stands - the two worship-site features - dominate; the seed's variant
            // leans further into whichever of the two (or the standing-statue ridge, favouring idols) it rolled
            int ruinBias = variant == 0 || variant == 2 ? 14 : 0, villageBias = variant == 1 ? 14 : 0;
            kind = r < 30 + ruinBias ? 0 : r < 45 ? 1 : r < 57 ? 2 : r < 70 ? 3 : r < 92 + villageBias ? 4 : 5;
            if (kind != lastKind || tries > 6) break;
        }
        lastKind = kind;
        switch (kind) {
            case 0: { // ruin climb: stepped stone plinths rising to a high ledge, no shaft - just tight vertical hops
                int steps = rng.I(2, 3), stepW = rng.I(2, 3);
                int px = x, py = F;
                for (int k = 0; k < steps; k++) {
                    int rise = 2, gap = std::max(1, gapFor(rise) - rng.I(0, 1));
                    py -= rise;
                    plinth(px, stepW, py);
                    Plat step{px, px + stepW - 1, py, C_JUMP, '#', SetPiece::None, 0, px + 1};
                    pl.push_back(step);
                    if (k == steps - 2 && rng.C(0.5f) && g.get(px + stepW, py - 1) == '.') g.set(px + stepW, py - 1, 'o');
                    px += stepW + gap;
                }
                if (rng.C(0.4f) && g.get(px - 2, py - 1) == '.') g.set(px - 2, py - 1, 'p'); // a fruit bat roosting over the ruin
                x = px + 2;
            } break;
            case 1: { // canyon crossing: a mud-and-thorn ravine, sometimes a coiled viper waiting below
                int gw = std::clamp(gm - 1, 4, 6);
                for (int xx = x; xx < x + gw; xx++) g.set(xx, F, 'x');
                if (gw >= 4 && rng.C(0.3f)) g.set(x + gw / 2, F, 'e'); // a viper coiled in the undergrowth
                Plat before{x - 3, x - 1, F, C_JUMP, '#', SetPiece::None, 0, x - 1};
                Plat after{x + gw, x + gw + 3, F, C_JUMP, '#', SetPiece::None, 0, x + gw};
                pl.push_back(before); pl.push_back(after);
                for (int k = 1; k <= 2; k++) g.set(x + gw * k / 3, F - 4, 'o');
                out.setPieces[(int)SetPiece::ShipGap]++;
                x = x + gw + 4;
            } break;
            case 2: { // plank bridge: rope-and-plank over a ravine, gives way soon after you step on it
                int px2[3] = {x + 1, x + 5, x + 9};
                for (int p2 : px2) { g.rect(p2, F - 1, p2 + 1, F - 1, 'f'); Plat plate{p2, p2 + 1, F - 1, C_JUMP, 'f', SetPiece::CrumbleRun, 0, p2}; pl.push_back(plate); }
                for (int xx = x; xx < x + 12; xx++) if (g.get(xx, F) == '#') g.set(xx, F, 'x'); // the ravine floor, thorns below the boards
                Plat after{x + 13, x + 15, F, C_JUMP, '#', SetPiece::None, 0, x + 13};
                pl.push_back(after);
                out.setPieces[(int)SetPiece::CrumbleRun]++;
                x += 16;
            } break;
            case 3: { // vine chimney: a wall-jump shaft between two rock faces, laced with thorny vines
                int iw = rng.I(3, 4);
                bool thorny = rng.C(0.5f);
                int Hs = std::min(rng.I(P.shaftMin, P.shaftMax + 1), MaxUpShaft(iw, thorny));
                Plat tunnel{x, x + 2, F, C_JUMP, '#', SetPiece::None, 0, x + 1};
                pl.push_back(tunnel);
                Plat top = CarveShaft(g, tunnel, true, iw, Hs, thorny, rng.I(0, 2));
                pl.push_back(top);
                out.setPieces[(int)top.tag]++;
                x = top.x1 + 4;
            } break;
            case 4: { // village stand: stepped huts on stilts, a warrior behind cover, a monitor lizard patrolling
                int n = 3, w0 = 3;
                int prevTop = F, sx = x + w0, prevEnd = x + w0 - 1;
                for (int k = 0; k < n; k++) {
                    int top = std::clamp(prevTop + rng.I(-2, 1), F - 3, F - 1);
                    int gap = std::max(1, gapFor(prevTop - top) - 1);
                    if (k > 0) sx = prevEnd + 1 + gap;
                    g.rect(sx, top, sx + 2, F - 1, '#');
                    if (k > 0) for (int xx = sx - gap; xx < sx; xx++) g.set(xx, F, 'x');
                    Plat hut{sx, sx + 2, top, C_JUMP, '#', SetPiece::None, 0, sx + 1};
                    pl.push_back(hut);
                    if (k == 1 && rng.C(0.6f) && g.get(sx + 2, top - 1) == '.' && g.get(sx + 1, top - 1) == '.') { g.set(sx + 2, top - 1, 'P'); g.set(sx + 1, top - 1, 'k'); }
                    else if (k == 2 && g.get(sx + 2, top - 1) == '.') g.set(sx + 2, top - 1, 'c'); // a monitor lizard defending the last hut
                    prevTop = top; prevEnd = sx + 2;
                }
                int gapEnd = std::max(1, gapFor(prevTop - F) - 1);
                Plat after{prevEnd + 1 + gapEnd, prevEnd + 3 + gapEnd, F, C_JUMP, '#', SetPiece::None, 0, prevEnd + 1 + gapEnd};
                pl.push_back(after);
                x = after.x1 + 1;
            } break;
            default: { // processional way: a straight ceremonial path (totem poles are added by the tile art, see IslandTotem in platformer.cpp), poison-frog mud patches pulsing underfoot
                int len = 22;
                for (int i = 4; i < len - 2; i += 6) { g.set(x + i, F, 't'); g.set(x + i, F - 4, 'o'); }
                Plat run{x, x + len - 1, F, C_JUMP, '#', SetPiece::None, 0, x + 1};
                pl.push_back(run);
                x += len;
            } break;
        }
        int breath = rng.I(5, 8);
        if (rng.C(0.5f)) { // hanging vines: passable, but they slow a fall
            int kx = x + rng.I(0, std::max(0, breath - 3)), kh = rng.I(3, 5);
            for (int i = 0; i < 3; i++) for (int j = 1; j <= kh - (i == 1 ? 0 : 1); j++) if (g.get(kx + i, F - j) == '.') g.set(kx + i, F - j, 'w');
        }
        x += breath;
    }
    int fx = x + 3;
    Plat fin{fx, fx + 6, F, C_JUMP, '#', SetPiece::None, 0, fx + 1};
    pl.push_back(fin);
    g.set(fx + 3, F - 1, 'E'); // no boss arena for this first pass - the crossing itself ends the level, like the Pipes
    wOut = fx + 7;
    out.exitRow = F - 1;
}

// The Cave is a claustrophobic tunnel, lit only by the diver's lamp (LevelDef::dark) - harder than the Island,
// with a shape unlike any of the other three: where the Hull is a long horizontal deck broken up by the
// occasional shaft, and the Island is a floor-level crossing of set-pieces, the Cave's dominant motif is the
// climb itself - a winding chain of wall-jump shafts through solid rock, rising and dropping repeatedly
// rather than holding to one floor level, with only short connecting squeezes between them:
//   chained shaft    a wall-jump shaft ridged with calcified tube worms, climbing or dropping - the level's
//                     standing height (curF) actually changes here, unlike Island's fixed floor row
//   crevice squeeze  a short, low-ceilinged connector between shafts, urchin spines underfoot and sometimes
//                     a Mutated Crustacean lurking in them
//   rockfall ledge   loose scree between shafts, gives way soon after you land on it
//   stalactite drop  a Stalactite Spider waits in a crack overhead and drops to ambush (the 'P' tile, reused
//                    with its own cave art - see DrawEnemy)
//   flooded pocket   a still, bioluminescent-lit landing at the foot of a shaft - a breather, not a hazard
static void BuildCave(Grid& g, std::vector<Plat>& pl, Rng& rng, GenLevel& out, const Params& P, int& wOut) {
    const int H = g.h, F = H - 6, yLo = 8, yHi = H - 10;
    g.rect(0, F, g.w - 1, H - 1, '#');       // only the entry floor - everywhere else, height comes from the shaft chain itself
    auto overhang = [&](int x0, int x1, int depth) { g.rect(x0, 0, x1, depth, '#'); }; // a hanging mass of rock, purely decorative - never low enough to touch a jump arc
    Plat start{2, 9, F, C_START, '#', SetPiece::None, 0, 3};
    pl.push_back(start);
    int x = 12, curF = F, lastKind = -1, guard = 0, shaftsInRow = 0;
    while (x < P.length && guard++ < 70) {
        int kind;
        for (int tries = 0;; tries++) {
            int r = rng.I(0, 99);
            // shafts dominate; the other four are connectors that only ever show up between climbs
            kind = r < 48 ? 0 : r < 63 ? 1 : r < 78 ? 2 : r < 90 ? 3 : 4;
            if (kind == 0 && shaftsInRow >= 3) kind = 1 + rng.I(0, 3); // never more than three shafts in a row, or it stops reading as a path
            if (kind != lastKind || tries > 6) break;
        }
        lastKind = kind;
        if (kind == 0) shaftsInRow++; else shaftsInRow = 0;
        g.rect(x - 3, curF, x + 3, H - 1, '#'); // a guaranteed local floor bridge at the entry, however the previous feature left the grid - curF keeps moving, so nothing here can be assumed already solid
        switch (kind) {
            case 0: { // chained shaft: climbs or drops, so the path actually winds through the rock instead of holding one level
                int iw = rng.I(3, 4);
                bool up = shaftsInRow <= 1 ? rng.C(0.55f) : rng.C(0.3f); // after climbing once, more likely to level out or drop, so it winds rather than only rising
                int Hs = rng.I(P.shaftMin, P.shaftMax);
                if (up) Hs = std::min(Hs, MaxUpShaft(iw, true));
                if (up && curF - Hs < yLo) up = false;
                if (!up && (curF + Hs > yHi || curF - 8 < 2)) { up = true; Hs = std::min(Hs, MaxUpShaft(iw, true)); if (curF - Hs < yLo) { x += 8; break; } }
                Plat tunnel{x, x + 2, curF, C_JUMP, '#', SetPiece::None, 0, x + 1};
                pl.push_back(tunnel);
                Plat top = CarveShaft(g, tunnel, up, iw, Hs, true, rng.I(0, 2));
                pl.push_back(top);
                out.setPieces[(int)top.tag]++;
                x = top.x1 + 4;
                curF = top.y + 1;
                g.rect(x - 2, curF, x + 6, H - 1, '#'); // a short landing floor at the shaft's new height
            } break;
            case 1: { // crevice squeeze: a bed of urchin spines jumped over, never landed on - the standing platforms flank it, exactly like a canyon crossing
                int len = rng.I(4, 6);
                g.rect(x - 2, curF, x + len + 2, H - 1, '#');
                overhang(x - 2, x + len + 2, curF - 9); // rock hanging low overhead - decorative only, well clear of the standing headroom
                for (int xx = x; xx < x + len; xx++) g.set(xx, curF, 'x');
                if (len >= 5 && rng.C(0.35f) && g.get(x + len / 2, curF - 5) == '.') g.set(x + len / 2, curF - 5, 'c'); // a Mutated Crustacean clinging over the spines
                Plat before{x - 3, x - 1, curF, C_JUMP, '#', SetPiece::None, 0, x - 1};
                Plat after{x + len, x + len + 2, curF, C_JUMP, '#', SetPiece::None, 0, x + len};
                pl.push_back(before); pl.push_back(after);
                x += len + 3;
            } break;
            case 2: { // rockfall ledge: loose scree between shafts, gives way soon after landing
                g.rect(x - 2, curF, x + 15, H - 1, '#');
                int px2[3] = {x + 1, x + 5, x + 9};
                for (int p2 : px2) { g.rect(p2, curF - 1, p2 + 1, curF - 1, 'f'); Plat plate{p2, p2 + 1, curF - 1, C_JUMP, 'f', SetPiece::CrumbleRun, 0, p2}; pl.push_back(plate); }
                for (int xx = x; xx < x + 12; xx++) if (g.get(xx, curF) == '#') g.set(xx, curF, 'x'); // sharp broken rock below the scree
                Plat after{x + 13, x + 15, curF, C_JUMP, '#', SetPiece::None, 0, x + 13};
                pl.push_back(after);
                out.setPieces[(int)SetPiece::CrumbleRun]++;
                x += 16;
            } break;
            case 3: { // stalactite drop: a Stalactite Spider waits in a crack overhead, right at the mouth of the next shaft
                int len = 9;
                g.rect(x - 2, curF, x + len + 1, H - 1, '#');
                Plat run{x, x + len - 1, curF, C_JUMP, '#', SetPiece::None, 0, x + 1};
                pl.push_back(run);
                overhang(x + 2, x + 6, curF - 12);
                if (g.get(x + 4, curF - 1) == '.') g.set(x + 4, curF - 1, 'P');
                x += len;
            } break;
            default: { // flooded pocket: a still, bioluminescent landing at the foot of a shaft - a breather, not a hazard
                int len = 12;
                g.rect(x - 2, curF, x + len + 1, H - 1, '#');
                for (int i = 3; i < len - 2; i += 5) if (g.get(x + i, curF - 4) == '.') g.set(x + i, curF - 4, 'o');
                Plat run{x, x + len - 1, curF, C_JUMP, '#', SetPiece::None, 0, x + 1};
                pl.push_back(run);
                x += len;
            } break;
        }
    }
    int fx = x + 3;
    g.rect(fx - 2, curF, fx + 8, H - 1, '#');
    Plat fin{fx, fx + 6, curF, C_JUMP, '#', SetPiece::None, 0, fx + 1};
    pl.push_back(fin);
    g.set(fx + 3, curF - 1, 'E');
    wOut = fx + 7;
    out.exitRow = curF - 1;
}
// The Weeds: a kelp forest in open, sunlit water - and unlike every other level, the ground is the danger. Long
// stretches of seabed are carpeted with urchins or lie over electric sand where rays have buried themselves, so the way
// across is up in the forest: kelp floats strung along the canopy (the stalks under them are climbable and break a
// fall), rock stacks to scale, a sea-stack chimney, and warm currents that lift you out of the gullies.
//   kelp canopy      a run of small floats at the arc's edge, bobbing at different heights, over an urchin carpet
//   stack climb      rock stacks stepping up out of the kelp, then a leap back down
//   ray flats        open sand where rays lie buried and shock on a timer
//   urchin gully     a gully too wide to step over, floored with urchins
//   current          a warm vent that lifts you onto a rock stack too tall to jump
//   stack chimney    a wall-jump chimney between two sea-stacks crusted with barnacles
static void BuildWeeds(Grid& g, std::vector<Plat>& pl, Rng& rng, GenLevel& out, const Params& P, int& wOut) {
    const int H = g.h, F = H - 6;
    g.rect(0, F, g.w - 1, H - 1, '#');       // the seabed and the rock under it
    auto stack = [&](int x0, int width, int top) { g.rect(x0, top, x0 + width - 1, H - 1, '#'); };
    auto stalk = [&](int x0, int top, int bottom) { for (int y = top; y < bottom; y++) if (g.get(x0, y) == '.') g.set(x0, y, 'w'); };
    Plat start{2, 9, F, C_START, '#', SetPiece::None, 0, 3};
    pl.push_back(start);
    JumpArc a0 = CalculateValidJumpArc(0);
    int gm = std::max(3, (int)std::floor((a0.maxReach * P.safety - 12) / kin::TILE));
    auto gapFor = [&](int riseTiles) {
        JumpArc a = CalculateValidJumpArc(riseTiles * kin::TILE);
        if (!a.reachable) return 1;
        return std::max(1, (int)std::floor((a.maxReach * P.safety - 12) / kin::TILE) - 1);
    };
    auto landOnSeabed = [&](int fromX, int fromY) { // leap from a height back down to a patch of clean seabed
        int gap = std::max(1, gapFor(fromY - F) - rng.I(0, 1));
        Plat land{fromX + gap, fromX + gap + 3, F, C_JUMP, '#', SetPiece::None, gap, fromX + gap};
        for (int xx = land.x0 - 1; xx <= land.x1 + 1; xx++) if (g.get(xx, F) != '#') g.set(xx, F, '#');
        pl.push_back(land);
        return land.x1 + 1;
    };
    int x = 12, lastKind = -1, guard = 0;
    while (x < P.length && guard++ < 70) {
        int kind;
        for (int tries = 0;; tries++) {
            int r = rng.I(0, 99);
            kind = r < 32 ? 0 : r < 48 ? 1 : r < 62 ? 2 : r < 74 ? 3 : r < 86 ? 4 : 5;
            if (kind != lastKind || tries > 6) break;
        }
        lastKind = kind;
        switch (kind) {
            case 0: { // kelp canopy
                Plat before{x - 3, x - 1, F, C_JUMP, '#', SetPiece::None, 0, x - 1};
                pl.push_back(before);
                int n = rng.I(4, 6), px = x, py = F;
                for (int k = 0; k < n; k++) {
                    int ny = std::clamp(py - (k == 0 ? 3 : rng.I(-1, 1)), F - 7, F - 3);
                    int gap = std::max(1, gapFor(py - ny) - rng.I(0, 1)), wdt = rng.I(1, 2);
                    int fx = px + gap;
                    g.rect(fx, ny, fx + wdt - 1, ny, '=');
                    stalk(fx, ny + 1, F);                                   // the float's own stalk down to the seabed
                    Plat fl{fx, fx + wdt - 1, ny, C_JUMP, '=', SetPiece::None, gap, fx};
                    pl.push_back(fl);
                    px = fx + wdt; py = ny;
                }
                for (int xx = x; xx < px; xx++) if (g.get(xx, F) == '#') g.set(xx, F, 'x'); // don't touch the bottom
                if (rng.C(0.5f)) g.set(x + (px - x) / 2, F - 9 >= 2 ? F - 9 : 2, 'o');         // sunlight slanting through the canopy
                x = landOnSeabed(px, py);
                out.setPieces[(int)SetPiece::CrumbleRun]++;
            } break;
            case 1: { // stack climb
                Plat before{x - 3, x - 1, F, C_JUMP, '#', SetPiece::None, 0, x - 1};
                pl.push_back(before);
                int steps = rng.I(2, 3), px = x, py = F;
                for (int k = 0; k < steps; k++) {
                    int rise = 2, gap = std::max(1, gapFor(rise) - rng.I(0, 1)), sw = rng.I(2, 3);
                    stalk(px, F - rng.I(3, 5), F);                          // kelp in the gap between the stacks
                    px += gap; py -= rise;
                    stack(px, sw, py);
                    Plat st{px, px + sw - 1, py, C_JUMP, '#', SetPiece::None, gap, px + 1};
                    pl.push_back(st);
                    px += sw;
                }
                x = landOnSeabed(px, py);
            } break;
            case 2: { // ray flats
                int len = 20;
                for (int i = 3; i < len - 2; i += 5) g.set(x + i, F, 't');
                Plat run{x, x + len - 1, F, C_JUMP, '#', SetPiece::None, 0, x + 1};
                pl.push_back(run);
                x += len;
            } break;
            case 3: { // urchin gully
                int gw = std::clamp(gm - 1, 4, 6);
                for (int xx = x; xx < x + gw; xx++) g.set(xx, F, 'x');
                Plat before{x - 3, x - 1, F, C_JUMP, '#', SetPiece::None, 0, x - 1};
                Plat after{x + gw, x + gw + 3, F, C_JUMP, '#', SetPiece::None, gw, x + gw};
                pl.push_back(before); pl.push_back(after);
                stalk(x + gw / 2, F - 4, F);
                out.setPieces[(int)SetPiece::ShipGap]++;
                x += gw + 4;
            } break;
            case 4: { // current: a warm vent lifts you onto a stack too tall to jump
                Plat before{x - 3, x, F, C_JUMP, '#', SetPiece::None, 0, x - 1};
                pl.push_back(before);
                g.set(x, F, 'v');
                stack(x + 1, 5, F - 5);
                g.rect(x, F - 11, x, F - 1, '.');                         // the column of rising water above the vent
                Plat top{x + 1, x + 5, F - 5, C_STEAM, '#', SetPiece::SteamBoost, 0, x + 2};
                pl.push_back(top);
                out.setPieces[(int)SetPiece::SteamBoost]++;
                x = landOnSeabed(x + 6, F - 5);
            } break;
            default: { // stack chimney
                int iw = rng.I(3, 4);
                int Hs = std::min(rng.I(P.shaftMin, P.shaftMax), MaxUpShaft(iw, true));
                Plat tunnel{x, x + 2, F, C_JUMP, '#', SetPiece::None, 0, x + 1};
                pl.push_back(tunnel);
                Plat top = CarveShaft(g, tunnel, true, iw, Hs, true, rng.I(1, 2));
                pl.push_back(top);
                out.setPieces[(int)top.tag]++;
                x = landOnSeabed(top.x1 + 1, top.y);
            } break;
        }
        int breath = rng.I(4, 7);
        if (rng.C(0.6f)) for (int i = 0; i < 2; i++) stalk(x + 1 + i * 2, F - rng.I(4, 7), F); // a clump of kelp on the clean seabed
        x += breath;
    }
    int fx = x + 3;
    Plat fin{fx, fx + 6, F, C_JUMP, '#', SetPiece::None, 0, fx + 1};
    pl.push_back(fin);
    g.set(fx + 3, F - 1, 'E');
    wOut = fx + 7;
    out.exitRow = F - 1;
}

// Atlantis: a drowned city, crossed street by street - and the only level you spend half your time inside. Its shape is
// architecture: buildings you run through, stairs up to their roofs, and the broken works between them.
//   temple hall     through a great building: a low-ceilinged hall with a shattered floor to leap and rune plates that flare
//   grand stair     a stairway up the face of a building, a run across its roof, and a drop back to the street
//   aqueduct        a broken aqueduct on piers over a flooded street full of shards: missing spans and loose stones
//   colonnade       the stumps of a fallen colonnade, each a different height, over shards
//   tower           a wall-jump climb inside a ruined tower, out over its broken top
//   fountain plaza  a plaza whose old fountain still breathes a current strong enough to lift you onto a balcony
static void BuildAtlantis(Grid& g, std::vector<Plat>& pl, Rng& rng, GenLevel& out, const Params& P, int& wOut) {
    const int H = g.h, F = H - 6;
    g.rect(0, F, g.w - 1, H - 1, '#');       // the paving of the drowned streets
    auto block = [&](int x0, int x1, int top) { g.rect(x0, top, x1, H - 1, '#'); };
    Plat start{2, 9, F, C_START, '#', SetPiece::None, 0, 3};
    pl.push_back(start);
    JumpArc a0 = CalculateValidJumpArc(0);
    int gm = std::max(3, (int)std::floor((a0.maxReach * P.safety - 12) / kin::TILE));
    auto gapFor = [&](int riseTiles) {
        JumpArc a = CalculateValidJumpArc(riseTiles * kin::TILE);
        if (!a.reachable) return 1;
        return std::max(1, (int)std::floor((a.maxReach * P.safety - 12) / kin::TILE) - 1);
    };
    auto dropToStreet = [&](int fromX, int fromY) {
        int gap = std::max(1, gapFor(fromY - F) - rng.I(0, 1));
        Plat land{fromX + gap, fromX + gap + 3, F, C_JUMP, '#', SetPiece::None, gap, fromX + gap};
        for (int xx = land.x0 - 1; xx <= land.x1 + 1; xx++) if (g.get(xx, F) != '#') g.set(xx, F, '#');
        pl.push_back(land);
        return land.x1 + 1;
    };
    int x = 12, lastKind = -1, guard = 0;
    while (x < P.length && guard++ < 70) {
        int kind;
        for (int tries = 0;; tries++) {
            int r = rng.I(0, 99);
            kind = r < 24 ? 0 : r < 42 ? 1 : r < 58 ? 2 : r < 74 ? 3 : r < 88 ? 4 : 5;
            if (kind != lastKind || tries > 6) break;
        }
        lastKind = kind;
        switch (kind) {
            case 0: { // temple hall
                int L = rng.I(15, 19), top = F - 9, pit = std::clamp(gm - 2, 2, 3);
                block(x, x + L - 1, top);
                g.rect(x, F - 6, x + L - 1, F - 1, '.');                  // the hall itself, open at both ends
                int mid = x + L / 2 - pit / 2;
                for (int xx = mid; xx < mid + pit; xx++) g.set(xx, F, 'x'); // a shattered floor
                g.set(x + 3, F, 't');                                      // a rune plate before it
                if (L >= 17) g.set(x + L - 4, F, 't');
                for (int xx = x + 2; xx < x + L - 2; xx += 5) g.set(xx, F - 5, 'o'); // lamps of pale glyph-light along the ceiling
                Plat before{x, mid - 1, F, C_JUMP, '#', SetPiece::None, 0, mid - 2};
                Plat after{mid + pit, x + L - 1, F, C_JUMP, '#', SetPiece::None, pit, mid + pit};
                pl.push_back(before); pl.push_back(after);
                x += L;
            } break;
            case 1: { // grand stair
                int steps = rng.I(3, 5), sx = x, sy = F;
                for (int k = 0; k < steps; k++) {
                    sy -= 1;
                    block(sx, sx + 1, sy);
                    Plat st{sx, sx + 1, sy, C_JUMP, '#', SetPiece::None, 0, sx};
                    pl.push_back(st);
                    sx += 2;
                }
                int roof = rng.I(5, 8);
                block(sx, sx + roof - 1, sy);
                Plat r{sx, sx + roof - 1, sy, C_JUMP, '#', SetPiece::None, 0, sx + 1};
                pl.push_back(r);
                if (rng.C(0.5f)) g.set(sx + roof - 2, sy, 't');
                x = dropToStreet(sx + roof, sy);
            } break;
            case 2: { // aqueduct
                Plat before{x - 3, x - 1, F, C_JUMP, '#', SetPiece::None, 0, x - 1};
                pl.push_back(before);
                int y = F - 3, gap0 = std::max(1, gapFor(3) - 1);
                int px = x + gap0, spans = rng.I(3, 4), loose = rng.I(1, spans - 1), lastGap = 0;
                int x0 = x;
                for (int k = 0; k < spans; k++) {
                    int len = rng.I(3, 4);
                    char ch = k == loose ? 'f' : '=';
                    g.rect(px, y, px + len - 1, y, ch);
                    if (ch == '=') g.rect(px + len / 2, y + 1, px + len / 2, F - 1, '#'); // a pier under the span
                    Plat sp{px, px + len - 1, y, C_JUMP, ch, ch == 'f' ? SetPiece::CrumbleRun : SetPiece::None, k == 0 ? gap0 : 0, px};
                    pl.push_back(sp);
                    lastGap = std::max(1, gapFor(0) - rng.I(1, 2));
                    px += len + lastGap;
                }
                px -= lastGap; // (the gap after the last span isn't used)
                for (int xx = x0; xx < px; xx++) if (g.get(xx, F) == '#' && g.get(xx, F - 1) == '.') g.set(xx, F, 'x'); // shards in the flooded street
                out.setPieces[(int)SetPiece::CrumbleRun]++;
                x = dropToStreet(px, y);
            } break;
            case 3: { // colonnade
                Plat before{x - 3, x - 1, F, C_JUMP, '#', SetPiece::None, 0, x - 1};
                pl.push_back(before);
                int n = rng.I(4, 5), px = x - 1, py = F;
                for (int k = 0; k < n; k++) {
                    int ny = std::clamp(py - rng.I(-1, 2), F - 5, F - 2), gap = std::max(1, gapFor(py - ny) - rng.I(0, 1));
                    int cx = px + gap, cw = rng.I(1, 2);
                    block(cx, cx + cw - 1, ny);
                    Plat col{cx, cx + cw - 1, ny, C_JUMP, '#', SetPiece::None, gap, cx};
                    pl.push_back(col);
                    px = cx + cw - 1; py = ny;
                }
                for (int xx = x; xx <= px; xx++) if (g.get(xx, F) == '#' && g.get(xx, F - 1) == '.') g.set(xx, F, 'x');
                x = dropToStreet(px + 1, py);
            } break;
            case 4: { // tower
                int iw = rng.I(3, 4);
                int Hs = std::min(rng.I(P.shaftMin, P.shaftMax), MaxUpShaft(iw, false));
                Plat tunnel{x, x + 2, F, C_JUMP, '#', SetPiece::None, 0, x + 1};
                pl.push_back(tunnel);
                Plat top = CarveShaft(g, tunnel, true, iw, Hs, false, rng.I(1, 2));
                pl.push_back(top);
                out.setPieces[(int)top.tag]++;
                g.set(top.x0 + 1, top.y - 3 >= 2 ? top.y - 3 : 2, 'o'); // a glyph burning at the tower's top
                x = dropToStreet(top.x1 + 1, top.y);
            } break;
            default: { // fountain plaza
                Plat before{x - 3, x, F, C_JUMP, '#', SetPiece::None, 0, x - 1};
                pl.push_back(before);
                g.set(x, F, 'v');
                block(x + 1, x + 6, F - 5);
                g.rect(x, F - 11, x, F - 1, '.');
                Plat balcony{x + 1, x + 6, F - 5, C_STEAM, '#', SetPiece::SteamBoost, 0, x + 2};
                pl.push_back(balcony);
                g.set(x + 4, F - 8, 'o');
                out.setPieces[(int)SetPiece::SteamBoost]++;
                x = dropToStreet(x + 7, F - 5);
            } break;
        }
        x += rng.I(4, 7);
    }
    int fx = x + 3;
    Plat fin{fx, fx + 6, F, C_JUMP, '#', SetPiece::None, 0, fx + 1};
    pl.push_back(fin);
    g.set(fx + 3, F - 1, 'E');
    wOut = fx + 7;
    out.exitRow = F - 1;
}
// ---------------------------------------------------------------------------- creature dens
// ParkourReference1.2.pdf's "Poisson-disc den spawner": the burrows, breaches and nests where a level's creatures
// hide, rest, and come back out of after something's been eaten (see beasts.h). A den is a floor tile ('#' -> 'D')
// with open water or air above it - exactly as solid as the floor it replaces, so nothing the diver needs to
// cross changes and --verify sees the same level. Every open-topped floor tile is a candidate - ledges and tower
// tops as well as the main floor, so some dens end up well off the diver's route - and Poisson-disc sampling
// (shuffle, then keep each candidate only if it's far enough from every den already placed) spreads them out.
static void PlaceDens(Grid& g, int w, unsigned seed, int level) {
    Rng r(seed * 747796405u + (unsigned)level * 2891336453u + 17u);
    std::vector<std::pair<int, int>> cand;
    for (int x = 13; x < w - 10; x++)
        for (int y = 2; y < g.h - 1; y++)
            if (g.get(x, y) == '#' && g.get(x, y - 1) == '.' && g.get(x, y - 2) == '.' && g.get(x - 1, y) != 'x' && g.get(x + 1, y) != 'x')
                cand.push_back({x, y});
    for (int i = (int)cand.size() - 1; i > 0; i--) std::swap(cand[i], cand[r.I(0, i)]);
    const float minD = level == 0 ? 16.0f : 13.0f; // tiles
    const int target = std::max(4, w / 16);
    std::vector<std::pair<int, int>> dens;
    for (const auto& c : cand) {
        if ((int)dens.size() >= target) break;
        bool ok = true;
        for (const auto& d : dens) {
            float dx = (float)(c.first - d.first), dy = (float)(c.second - d.second) * 1.5f;
            if (dx * dx + dy * dy < minD * minD) { ok = false; break; }
        }
        if (ok) dens.push_back(c);
    }
    for (const auto& d : dens) g.set(d.first, d.second, 'D');
}

// ---------------------------------------------------------------------------- the generator
GenLevel GenerateLevel(int level, unsigned seed, float scale) {
    Params P = ParamsFor(level);
    P.safety = std::min(0.97f, P.safety * scale);
    int islandVariant = (int)(seed % 3); // 0 ruined temple, 1 stilt village, 2 hillside/palm traverse - read back by platformer.cpp's Island art pass from p.seed the same way
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
    auto pistonCorridor = [&]() { // two crumbling plates back to back, landing right on a vent that launches you clear: no time to plant your feet
        Plat c = pl.back();
        if (c.ch == 'f' || c.conn == C_STEAM) return false;
        Plat cur = c;
        for (int k = 0; k < 2; k++) {
            Plat f{cur.x1 + 3, cur.x1 + 3, cur.y, C_CRUMBLE, 'f', k == 0 ? SetPiece::PistonCorridor : SetPiece::None, 2, 0};
            f.wx = f.x0;
            push(f, &cur);
            cur = f;
        }
        int ny = cur.y - 5;
        if (ny >= yLo) {
            g.set(cur.x1, cur.y, 'v');
            Plat n{cur.x1 + 1, cur.x1 + 5, ny, C_STEAM, '#', SetPiece::None, 0, cur.x1 + 2};
            push(n, nullptr);
            g.rect(cur.x1, ny - 6, cur.x1, cur.y - 1, '.');
        } else {
            addJump(3, 4);
        }
        out.setPieces[(int)SetPiece::PistonCorridor]++;
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
        if (level == 1) BuildTrench(g, pl, rng, out, P, w);
        else if (level == 2) BuildFleet(g, pl, rng, out, P, w);
        else if (level == 3) BuildIsland(g, pl, rng, out, P, w, islandVariant);
        else if (level == 4) BuildCave(g, pl, rng, out, P, w);
        else if (level == 5) BuildWeeds(g, pl, rng, out, P, w);
        else BuildAtlantis(g, pl, rng, out, P, w);
    } else {
    // ---- pass 1: the critical path
    int guard = 0;
    while (pl.back().x1 < P.length && guard++ < 400) {
        bool did = false;
        if (rng.C(P.setChance)) {
            if (level == 0) { int k = rng.I(0, 4); did = k == 0 ? steamBoost() : k == 1 ? crumbleRun() : k == 2 ? gearGauntlet() : k == 3 ? pipeDrop() : pistonCorridor(); }
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
                float hazardChance = level == 0 ? 0.55f : level == 1 ? 0.40f : 0.55f;
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
            if (wdt >= 3 && p.ch == '#' && rng.C(0.4f) && air(p.x1 - 1, p.y - 1) && air(p.x1 - 1, p.y - 2)) g.set(p.x1 - 1, p.y - 1, 'P'); // only on a deck, never on a spar in mid-air
            if (wdt >= 5 && rng.C(0.3f) && air(p.x0 + 2, p.y - 1)) g.set(p.x0 + 2, p.y - 1, 'k');
            if (wdt >= 4 && rng.C(0.4f) && air(p.x0 + 1, p.y - 1) && g.get(p.x0 + 1, p.y - 1) == '.') g.set(p.x0 + 1, p.y - 1, 'c');
        }
    }

    if (P.enclosed) g.set(last.x0 + 3, last.y - 1, 'E');
    }
    PlaceDens(g, w, seed, level);
    // ---- the start, the exit and the finished grid
    g.set(pl[0].wx, pl[0].y - 1, 'S');
    for (auto& r : g.r) r.resize(w);
    out.w = w;
    out.h = P.H;
    out.rows = g.r;
    for (const Plat& p : pl) out.path.push_back({p.wx, p.y - 1, p.tag});
    // pass-2 dressing (cover crates, warriors, dens, gunners) must never sit in a standing cell of the critical path
    // (it made most Island draws unwinnable: a crate on the spot you had to stand)
    for (const GenWaypoint& w : out.path)
        for (int dy = 0; dy <= 1; dy++) {
            int y = w.ty - dy;
            if (y < 0 || y >= (int)out.rows.size() || w.tx < 0 || w.tx >= (int)out.rows[y].size()) continue;
            char& c = out.rows[y][w.tx];
            if (c == 'k' || c == 'P' || c == 'G' || c == 'c' || c == 'e' || c == 'p' || c == 'g' || c == 'N' || c == 'y') c = '.';
        }
    return out;
}
