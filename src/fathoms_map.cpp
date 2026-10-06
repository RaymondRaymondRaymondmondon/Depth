// Fathoms' archipelago generator (the doc's "Map and archipelago generation"). Maps come from a seed with rotational
// symmetry: one wedge of islands is laid out and copied round the centre for every player, so each home island is the
// same and so are the distances to its Coal, Mining and Fertile isles; tribes and coves sit in each wedge or on the
// boundaries between two (shared, equidistant); the volcano and the richest ruins sit in the centre. Island shapes are
// polar noise rotated with their wedge, so they rasterise the same. CheckFairness is the doc's rule list as a test.
#include "fathoms.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fa {

namespace {
struct IsleSpec { int kind; float ang, rad, r; uint32_t shape; int wedge; bool shallowPath; };
float Hash(uint32_t s, int i) { uint32_t h = s * 2654435761u ^ (uint32_t)i * 0x9E3779B9u; h ^= h >> 15; h *= 0x2C1B3C6Du; h ^= h >> 12; return (h & 0xFFFFFF) / 16777216.0f; }
float ShapeR(uint32_t shape, float phi) {   // a blob: a few smooth harmonics of radius
    float r = 1;
    for (int k = 2; k <= 5; k++) r += (Hash(shape, k) - 0.5f) * (0.36f / k) * cosf(k * phi + Hash(shape, k + 10) * 6.283f);
    return r;
}
}

static float IsleSize(int kind, uint32_t s) {   // diameter in tiles (the island types table)
    switch (kind) {
        case I_HOME: return 30; case I_FERTILE: return 15 + Hash(s, 1) * 3; case I_MINING: return 13 + Hash(s, 1) * 3; case I_COAL: return 10;
        case I_REEF: return 8; case I_RUIN: return 10; case I_TRIBAL: return 17; case I_COVE: return 13; case I_VOLCANO: return 22; default: return 10;
    }
}

void GenerateMap(World& w) {
    const Balance& Bal = B(); const Settings& S = w.set;
    int n = std::clamp(S.players, 2, MAX_PLAYERS);
    int si = S.size >= 0 ? S.size : (n <= 2 ? 0 : n <= 4 ? 1 : 2); si = std::clamp(si, 0, (int)Bal.sizes.size() - 1);
    const Balance::Size& Z = Bal.sizes[si];
    w.W = w.H = Z.grid; int N = w.W * w.H;
    w.tile.assign(N, T_DEEP); w.height.assign(N, -2.0f); w.isle.assign(N, -1); w.occ.assign(N, -1); w.owner.assign(N, -1); w.kelpOwner.assign(N, 0);
    w.islands.clear(); w.nodes.clear(); w.sites.clear();
    Vector2 C{w.W * 0.5f, w.H * 0.5f}; float R = w.W * 0.5f;
    uint32_t seed = S.seed;
    // ---- the wedge: every player's own set, at fixed offsets from their home (the fairness rules 1 and 2)
    float wedge = 2 * PI / n, a0 = Hash(seed, 99) * 2 * PI;
    float homeR = R * (S.mapType == MT_TWIN ? 0.62f : 0.66f);
    std::vector<IsleSpec> specs;
    int extraPerWedge = std::max(0, (Z.islands - 1 - Z.volcanoes - n * 4) / n);   // (plain and reef isles to reach the size's count)
    for (int p = 0; p < n; p++) {
        float base = a0 + p * wedge;
        auto add = [&](int kind, float dAng, float rad, uint32_t shp, bool path = false) { specs.push_back({kind, base + dAng, rad, IsleSize(kind, shp), shp, p, path}); };
        add(I_HOME, 0, homeR, 0x1000);
        add(I_FERTILE, wedge * 0.18f, homeR * 0.72f, 0x2000, true);   // (the Crustaceans' shallow path: a shallow bridge to it)
        add(I_MINING, -wedge * 0.2f, homeR * 0.74f, 0x3000);
        add(I_COAL, wedge * 0.0f, homeR * 0.48f, 0x4000);
        if (n * 2 <= Z.coal) add(I_COAL, wedge * 0.36f, homeR * 0.9f, 0x4100);
        add(I_RUIN, wedge * 0.5f, homeR * 0.55f, 0x5000);   // (between two players: equidistant from both)
        for (int e = 0; e < extraPerWedge; e++) add(e % 2 ? I_REEF : I_PLAIN, wedge * (e % 2 ? -0.42f : 0.42f), homeR * (0.95f - 0.12f * e), 0x6000 + e);
    }
    // tribes and coves: one each per wedge if the size has enough, else on the boundaries (shared by two neighbours)
    for (int p = 0; p < n; p++) {
        float base = a0 + p * wedge;
        if (Z.tribes >= n) specs.push_back({I_TRIBAL, base - wedge * 0.38f, homeR * 0.92f, IsleSize(I_TRIBAL, 0x7000), 0x7000, p, false});
        else if (p % 2 == 0 && p / 2 < Z.tribes) specs.push_back({I_TRIBAL, base + wedge * 0.5f, homeR * 0.95f, IsleSize(I_TRIBAL, 0x7000), 0x7000, p, false});
        if (Z.coves >= n) specs.push_back({I_COVE, base + wedge * 0.5f, homeR * 0.98f, IsleSize(I_COVE, 0x8000), 0x8000, p, false});
        // fewer coves than players: on the boundaries, shared by two neighbours (every other one for an even count, every one for odd)
        else if (n == 2 ? p == 0 : (n % 2 == 1 || p % 2 == 1)) specs.push_back({I_COVE, base + wedge * 0.5f, homeR * (n == 2 ? 0.9f : 0.98f), IsleSize(I_COVE, 0x8000), 0x8000, p, false});
    }
    // the centre: the volcano(es) and the richest ruins
    if (S.mapType == MT_RING || Z.volcanoes >= 1) {
        if (Z.volcanoes >= 2) { specs.push_back({I_VOLCANO, a0, R * 0.12f, IsleSize(I_VOLCANO, 0x9000), 0x9000, -1, false}); specs.push_back({I_VOLCANO, a0 + PI, R * 0.12f, IsleSize(I_VOLCANO, 0x9000), 0x9000, -1, false}); }
        else specs.push_back({I_VOLCANO, 0, 0, IsleSize(I_VOLCANO, 0x9000), 0x9000, -1, false});
    }
    // ---- rasterise: each island a polar blob (its shape turns with its wedge), a shallow ring round it
    w.islands.resize(specs.size());
    for (size_t i = 0; i < specs.size(); i++) {
        const IsleSpec& s = specs[i]; Island& is = w.islands[i];
        is.kind = s.kind; is.c = {C.x + cosf(s.ang) * s.rad, C.y + sinf(s.ang) * s.rad}; is.r = s.r * 0.5f; is.home = s.kind == I_HOME ? s.wedge : -1;
        float rot = s.ang;   // (the shape is turned by the island's own angle: the same island in every wedge)
        float shallowW = s.kind == I_REEF ? 4.0f : s.kind == I_HOME ? 3.0f : 2.0f;
        int x0 = std::max(0, (int)(is.c.x - is.r * 1.5f - shallowW - 2)), x1 = std::min(w.W - 1, (int)(is.c.x + is.r * 1.5f + shallowW + 2));
        int y0 = std::max(0, (int)(is.c.y - is.r * 1.5f - shallowW - 2)), y1 = std::min(w.H - 1, (int)(is.c.y + is.r * 1.5f + shallowW + 2));
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
            float dx = x + 0.5f - is.c.x, dy = y + 0.5f - is.c.y, d = sqrtf(dx * dx + dy * dy), phi = atan2f(dy, dx) - rot;
            float rr = is.r * ShapeR(s.shape, phi);
            int k = w.Idx(x, y);
            float landR = s.kind == I_REEF ? rr * 0.45f : rr;
            if (d < landR) {
                float inner = (landR - d) / std::max(1.0f, landR);
                uint8_t t = d > landR - 1.2f ? T_BEACH : T_GRASS;
                float jn = Hash(s.shape, (int)(phi * 7 + 50) + (int)(d * 0.7f) * 31);
                if (t == T_GRASS && (s.kind == I_TRIBAL || s.kind == I_FERTILE) && jn > 0.55f && inner > 0.15f) t = T_JUNGLE;
                if (t == T_GRASS && (s.kind == I_MINING || s.kind == I_VOLCANO || s.kind == I_RUIN) && inner > 0.35f) t = T_HILL;
                if (s.kind == I_VOLCANO && t != T_BEACH) t = T_HILL;   // (the volcano is one walkable cone: its altar on the summit)
                if (s.kind == I_HOME && t == T_GRASS && inner > 0.25f && jn > 0.82f) t = T_HILL;
                w.tile[k] = t; w.isle[k] = (int16_t)i; is.tiles.push_back(k);
                w.height[k] = 0.3f + inner * (t == T_MOUNTAIN ? 6.0f : t == T_HILL ? 2.4f : 0.9f) + (t == T_BEACH ? -0.15f : 0);
                if (s.kind == I_VOLCANO) w.height[k] = 0.3f + powf(inner, 1.35f) * 7.0f;
            } else if (d < landR + shallowW + (s.shallowPath ? 0 : 0)) {
                if (w.tile[k] == T_DEEP) { w.tile[k] = T_SHALLOW; w.height[k] = -0.5f; }
            }
        }
    }
    // the Crustaceans' shallow path: a shallow bridge from each home to its Fertile isle (the same for every player)
    for (size_t i = 0; i < specs.size(); i++) if (specs[i].shallowPath) {
        int home = -1; for (size_t j = 0; j < specs.size(); j++) if (specs[j].kind == I_HOME && specs[j].wedge == specs[i].wedge) home = (int)j;
        if (home < 0) continue; Vector2 a = w.islands[home].c, b = w.islands[i].c;
        for (float u = 0; u <= 1; u += 0.01f) { Vector2 p = Vector2Lerp(a, b, u); for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) { int x = (int)p.x + dx, y = (int)p.y + dy; if (w.In(x, y) && w.tile[w.Idx(x, y)] == T_DEEP) { w.tile[w.Idx(x, y)] = T_SHALLOW; w.height[w.Idx(x, y)] = -0.5f; } } }
    }
    // the sea: kelp forests in each wedge, a one-way current round the centre (the fixed lanes between regions)
    for (int p = 0; p < n; p++) for (int k = 0; k < 2; k++) {
        float ang = a0 + p * wedge + wedge * (k ? 0.3f : -0.3f), rad = homeR * (k ? 0.36f : 0.82f);
        Vector2 c{C.x + cosf(ang) * rad, C.y + sinf(ang) * rad};
        for (int dy = -4; dy <= 4; dy++) for (int dx = -4; dx <= 4; dx++) { int x = (int)c.x + dx, y = (int)c.y + dy; if (!w.In(x, y) || dx * dx + dy * dy > 14) continue; int id = w.Idx(x, y); if (w.tile[id] == T_DEEP) w.tile[id] = T_KELP; }
    }
    { float cr = homeR * 0.3f; for (int a = 0; a < 720; a++) { float ang = a * PI / 360; int x = (int)(C.x + cosf(ang) * cr), y = (int)(C.y + sinf(ang) * cr); if (w.In(x, y) && w.tile[w.Idx(x, y)] == T_DEEP) w.tile[w.Idx(x, y)] = T_CURRENT; } }
    for (int k = 0; k < N; k++) if (w.tile[k] == T_DEEP) { int x = k % w.W, y = k / w.W; float d = Vector2Distance({x + 0.5f, y + 0.5f}, C) / R; w.height[k] = -2 - 2 * d; }
    // ---- what each island holds (the island types table), nodes placed by the wedge's own angle so they match
    auto node = [&](int kind, Vector2 p, int island, float amount = -1) {
        Node nd; nd.kind = kind; nd.p = p; nd.island = island; nd.amount = nd.cap = amount > 0 ? amount : Bal.nodes[kind].amount; w.nodes.push_back(nd); };
    auto polar = [&](const Island& is, float rot, float ang, float frac) { return Vector2{is.c.x + cosf(rot + ang) * is.r * frac, is.c.y + sinf(rot + ang) * is.r * frac}; };
    auto snap = [&](Vector2 p, bool water, int island) {   // the nearest tile of the right kind
        int bx = (int)p.x, by = (int)p.y; float bd = 1e9f; Vector2 best = p;
        for (int dy = -6; dy <= 6; dy++) for (int dx = -6; dx <= 6; dx++) { int x = bx + dx, y = by + dy; if (!w.In(x, y)) continue; int t = w.tile[w.Idx(x, y)];
            bool ok = water ? w.Water(t) : (t == T_GRASS || t == T_HILL || t == T_JUNGLE) && (island < 0 || w.isle[w.Idx(x, y)] == island); if (!ok) continue; float d = (float)(dx * dx + dy * dy); if (d < bd) { bd = d; best = {x + 0.5f, y + 0.5f}; } }
        return best; };
    for (size_t i = 0; i < specs.size(); i++) {
        Island& is = w.islands[i]; float rot = specs[i].ang; int ii = (int)i;
        switch (is.kind) {
            case I_HOME:
                for (int k = 0; k < 2; k++) node(N_BRASS, snap(polar(is, rot, 2.2f + k * 0.5f, 0.6f), false, ii), ii);
                node(N_COAL, snap(polar(is, rot, -2.4f, 0.6f), false, ii), ii, Bal.homeCoal);
                for (int k = 0; k < 4; k++) node(N_GROVE, snap(polar(is, rot, 1.2f + k * 0.35f, 0.55f), false, ii), ii);
                // (the sea side faces the centre, where the Harbor stands: angle pi from the island's own bearing)
                node(N_KELP, snap(polar(is, rot, PI - 0.95f, 1.25f), true, -1), -1);
                for (int k = 0; k < 6; k++) node(N_FISH, snap(polar(is, rot, PI - 1.35f + k * 0.54f, 1.18f), true, -1), -1);
                break;
            case I_FERTILE: for (int k = 0; k < 5; k++) node(N_GROVE, snap(polar(is, rot, k * 1.25f, 0.5f), false, ii), ii); for (int k = 0; k < 3; k++) node(N_FISH, snap(polar(is, rot, k * 2.1f + 0.5f, 1.3f), true, -1), -1); break;
            case I_MINING: for (int k = 0; k < 3; k++) node(N_BRASS, snap(polar(is, rot, k * 2.1f, 0.45f), false, ii), ii); break;
            case I_COAL: for (int k = 0; k < 2; k++) node(N_COAL, snap(polar(is, rot, k * 3.1f, 0.4f), false, ii), ii); break;
            case I_REEF: for (int k = 0; k < 3; k++) node(N_KELP, snap(polar(is, rot, k * 2.1f, 1.4f), true, -1), -1); for (int k = 0; k < 2; k++) node(N_PEARL, snap(polar(is, rot, k * 3.1f + 1, 1.0f), true, -1), -1); break;
            case I_RUIN: node(N_VENT, snap(is.c, false, ii), ii); break;
            case I_VOLCANO: for (int k = 0; k < 2; k++) { Node v; v.kind = N_VENT; v.p = snap(polar(is, rot, k * PI + 0.6f, 0.62f), false, ii); v.island = ii; v.amount = v.cap = Bal.volcanoVent; w.nodes.push_back(v); } break;
            case I_PLAIN: for (int k = 0; k < 3; k++) node(N_GROVE, snap(polar(is, rot, k * 2.1f, 0.4f), false, ii), ii); node(N_BRASS, snap(polar(is, rot, 1.0f, 0.3f), false, ii), ii); break;
            default: break;
        }
    }
    // wrecks in each wedge's sea (salvage; 20% are trapped)
    for (int p = 0; p < n; p++) for (int k = 0; k < 2; k++) {
        float ang = a0 + p * wedge + wedge * (k ? 0.25f : -0.28f), rad = homeR * (k ? 0.62f : 0.4f);
        Node nd; nd.kind = N_WRECK; nd.p = snap({C.x + cosf(ang) * rad, C.y + sinf(ang) * rad}, true, -1); nd.amount = nd.cap = Bal.nodes[N_WRECK].amount; nd.trapped = Hash(seed, p * 7 + k) < Bal.wreckTrap; w.nodes.push_back(nd);
    }
    // the harbour spot on each home island: its coast facing the centre
    for (auto& is : w.islands) if (is.kind == I_HOME) {
        Vector2 toC = Vector2Normalize(Vector2Subtract(C, is.c)); int best = -1; float bd = 1e9f;
        for (int k : is.tiles) { int x = k % w.W, y = k / w.W; bool coast = false; for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (w.In(x + dx, y + dy) && w.Water(w.tile[w.Idx(x + dx, y + dy)])) coast = true; if (!coast) continue;
            Vector2 p{x + 0.5f, y + 0.5f}; float d = -Vector2DotProduct(Vector2Subtract(p, is.c), toC); if (d < bd) { bd = d; best = k; } }
        is.harborSpot = best;
    }
}

// the doc's fairness rules as a test: homes on a circle evenly spaced; each player's nearest Coal, Mining and Fertile isle
// at the same distance (+-2 tiles); a tribe and a cove at the same distance or shared; the centre equidistant
bool CheckFairness(const World& w, std::string* why) {
    Vector2 C{w.W * 0.5f, w.H * 0.5f}; std::vector<int> homes; for (size_t i = 0; i < w.islands.size(); i++) if (w.islands[i].kind == I_HOME) homes.push_back((int)i);
    if ((int)homes.size() != w.set.players) { if (why) *why = "a home island per player"; return false; }
    float r0 = Vector2Distance(w.islands[homes[0]].c, C);
    for (int h : homes) if (fabsf(Vector2Distance(w.islands[h].c, C) - r0) > 1.5f) { if (why) *why = "the homes sit on one circle"; return false; }
    auto nearestOf = [&](int h, int kind) { float bd = 1e9f; for (const auto& is : w.islands) if (is.kind == kind) bd = std::min(bd, Vector2Distance(is.c, w.islands[h].c)); return bd; };
    int volcanoes = 0; for (const auto& is : w.islands) volcanoes += is.kind == I_VOLCANO;
    for (int kind : {I_COAL, I_MINING, I_FERTILE, I_TRIBAL, I_COVE, I_RUIN, I_VOLCANO}) {
        if (kind == I_VOLCANO && volcanoes > 1) continue;   // (two volcanoes sit either side of the centre; the homes are equidistant from the centre)
        float lo = 1e9f, hi = -1e9f; for (int h : homes) { float d = nearestOf(h, kind); lo = std::min(lo, d); hi = std::max(hi, d); }
        if (hi - lo > 2.0f && hi < 1e8f) { if (why) { char b[96]; std::snprintf(b, sizeof b, "the nearest isle of kind %d differs by %.1f tiles", kind, hi - lo); *why = b; } return false; }
    }
    for (int h : homes) if (w.islands[h].harborSpot < 0) { if (why) *why = "a harbour spot on every home"; return false; }
    return true;
}

}  // namespace fa
