// NOCLIP's level generators (design doc pp. 9-21 and p. 36, "Generation"): one kit per level style, seeded per day per
// level, with fixed rules for the landmarks (Labs, exits, a few set pieces) so a crew learns a level without
// memorizing it. Every level is joined up afterwards (no floor region is left cut off), then checked: every Lab
// reaches every exit (CheckReachable, --noclip-gen).
#include "noclip.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <queue>
#include <string>

namespace nc {
namespace {

struct Gen {
    Level& L; uint32_t r;
    Gen(Level& l, uint32_t seed) : L(l), r(seed * 2654435761u + 0x9E3779B9u) { for (int i = 0; i < 4; i++) Rand(); }
    float Rand() { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return (r & 0xFFFFFF) / 16777216.0f; }
    int RI(int a, int b) { return a + std::min(b - a, (int)(Rand() * (b - a + 1))); }   // [a, b]
    bool In(int x, int z) const { return x >= 1 && z >= 1 && x < L.w - 1 && z < L.h - 1; }
    uint8_t& T(int x, int z) { return L.tile[z * L.w + x]; }
    uint16_t& F(int x, int z) { return L.flags[z * L.w + x]; }
    void Fill(uint8_t t) { std::fill(L.tile.begin(), L.tile.end(), t); }
    void Rect(int x0, int z0, int x1, int z1, uint8_t t) { for (int z = std::max(1, z0); z <= std::min(L.h - 2, z1); z++) for (int x = std::max(1, x0); x <= std::min(L.w - 2, x1); x++) T(x, z) = t; }
    void RectFlag(int x0, int z0, int x1, int z1, uint16_t f) { for (int z = std::max(1, z0); z <= std::min(L.h - 2, z1); z++) for (int x = std::max(1, x0); x <= std::min(L.w - 2, x1); x++) F(x, z) |= f; }
    // an L-shaped corridor of the given width
    void Corridor(int x0, int z0, int x1, int z1, int wdt = 1, uint8_t t = T_FLOOR) {
        bool xFirst = Rand() < 0.5f; int cx = x0, cz = z0;
        auto carve = [&](int x, int z) { for (int a = 0; a < wdt; a++) for (int b = 0; b < wdt; b++) if (In(x + a, z + b) && Solidish(T(x + a, z + b))) T(x + a, z + b) = t; };
        auto Hm = [&]() { while (cx != x1) { carve(cx, cz); cx += x1 > cx ? 1 : -1; } };
        auto Vm = [&]() { while (cz != z1) { carve(cx, cz); cz += z1 > cz ? 1 : -1; } };
        if (xFirst) { Hm(); Vm(); } else { Vm(); Hm(); } carve(cx, cz);
    }
    static bool Solidish(uint8_t t) { return t == T_WALL || t == T_VOID || t == T_LOW || t == T_WHEAT; }
    bool Floorish(int x, int z) { uint8_t t = L.At(x, z); return L.Walkable(x, z) && t != T_PIT && t != T_DEEP; }
    // rooms on a loose scatter, joined by a spanning tree plus a few loops
    struct Room { int x0, z0, x1, z1; int cx() const { return (x0 + x1) / 2; } int cz() const { return (z0 + z1) / 2; } };
    std::vector<Room> Rooms(int n, int minS, int maxS, uint8_t floorT = T_FLOOR, int corrW = 1, float loops = 0.2f) {
        std::vector<Room> rooms;
        for (int tries = 0; tries < n * 30 && (int)rooms.size() < n; tries++) {
            int w = RI(minS, maxS), h = RI(minS, maxS), x = RI(2, L.w - w - 3), z = RI(2, L.h - h - 3);
            bool ok = true; for (const auto& o : rooms) if (x <= o.x1 + 2 && x + w >= o.x0 - 2 && z <= o.z1 + 2 && z + h >= o.z0 - 2) ok = false;
            if (!ok) continue;
            rooms.push_back({x, z, x + w - 1, z + h - 1}); Rect(x, z, x + w - 1, z + h - 1, floorT);
        }
        // join: each room to its nearest earlier one, then some loops
        for (size_t i = 1; i < rooms.size(); i++) { size_t best = 0; int bd = 1 << 30; for (size_t j = 0; j < i; j++) { int d = abs(rooms[i].cx() - rooms[j].cx()) + abs(rooms[i].cz() - rooms[j].cz()); if (d < bd) { bd = d; best = j; } } Corridor(rooms[i].cx(), rooms[i].cz(), rooms[best].cx(), rooms[best].cz(), corrW); }
        for (int k = 0; k < (int)(rooms.size() * loops); k++) { const Room& a = rooms[RI(0, (int)rooms.size() - 1)]; const Room& b = rooms[RI(0, (int)rooms.size() - 1)]; Corridor(a.cx(), a.cz(), b.cx(), b.cz(), corrW); }
        return rooms;
    }
    // join every walkable region to the biggest one (a short tunnel to the nearest reached cell)
    void JoinAll(uint8_t tunnel = T_FLOOR) {
        for (int pass = 0; pass < 60; pass++) {
            std::vector<int> comp(L.w * L.h, -1); std::vector<int> sizes; int nc = 0;
            for (int i = 0; i < L.w * L.h; i++) if (comp[i] < 0 && Floorish(i % L.w, i / L.w)) {
                int sz = 0; std::vector<int> st{i}; comp[i] = nc;
                while (!st.empty()) { int c = st.back(); st.pop_back(); sz++; int x = c % L.w, z = c / L.w; const int D4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}}; for (auto& d : D4) { int nx = x + d[0], nz = z + d[1]; if (nx < 0 || nz < 0 || nx >= L.w || nz >= L.h) continue; int ni = nz * L.w + nx; if (comp[ni] < 0 && Floorish(nx, nz)) { comp[ni] = nc; st.push_back(ni); } } }
                sizes.push_back(sz); nc++;
            }
            if (nc <= 1) return;
            int main = (int)(std::max_element(sizes.begin(), sizes.end()) - sizes.begin());
            // the smallest other region: tunnel from it to the nearest main cell
            int other = -1; for (int k = 0; k < nc; k++) if (k != main && (other < 0 || sizes[k] < sizes[other])) other = k;
            int bx = 0, bz = 0, ax = 0, az = 0, bd = 1 << 30;
            std::vector<int> oc, mc; for (int i = 0; i < L.w * L.h; i++) { if (comp[i] == other) oc.push_back(i); else if (comp[i] == main) mc.push_back(i); }
            for (int k = 0; k < (int)oc.size(); k += std::max(1, (int)oc.size() / 40)) for (int m = 0; m < (int)mc.size(); m += std::max(1, (int)mc.size() / 400)) { int d = abs(oc[k] % L.w - mc[m] % L.w) + abs(oc[k] / L.w - mc[m] / L.w); if (d < bd) { bd = d; ax = oc[k] % L.w; az = oc[k] / L.w; bx = mc[m] % L.w; bz = mc[m] / L.w; } }
            Corridor(ax, az, bx, bz, 1, tunnel);
        }
    }
    // the farthest walkable cell from a set (BFS distance), excluding Labs
    std::vector<int> Dist(int sx, int sz) {
        std::vector<int> d(L.w * L.h, -1); std::queue<int> q; if (!Floorish(sx, sz)) return d; d[sz * L.w + sx] = 0; q.push(sz * L.w + sx);
        while (!q.empty()) { int c = q.front(); q.pop(); int x = c % L.w, z = c / L.w; const int D4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}}; for (auto& dd : D4) { int nx = x + dd[0], nz = z + dd[1]; if (nx < 0 || nz < 0 || nx >= L.w || nz >= L.h) continue; int ni = nz * L.w + nx; if (d[ni] < 0 && (Floorish(nx, nz) || L.At(nx, nz) == T_BLAST)) { d[ni] = d[c] + 1; q.push(ni); } } }
        return d;
    }
    // a Lab: a 7 x 7 room of concrete and steel with a blast door, the ring in the middle, the rooms' fittings round it
    void PlaceLab(const std::string& name, int x0, int z0) {
        x0 = std::clamp(x0, 2, L.w - 10); z0 = std::clamp(z0, 2, L.h - 10);
        Rect(x0, z0, x0 + 8, z0 + 8, T_LABWALL); Rect(x0 + 1, z0 + 1, x0 + 7, z0 + 7, T_LABFLOOR);
        for (int z = z0; z <= z0 + 8; z++) for (int x = x0; x <= x0 + 8; x++) F(x, z) = 0;
        // the blast door on the side facing the level's middle, and a cleared approach
        int dx = x0 + 4, dz = z0 + 8, ox = 0, oz = 1;
        if (z0 > L.h / 2) { dz = z0; oz = -1; }
        T(dx, dz) = T_BLAST; for (int k = 1; k <= 3; k++) if (In(dx, dz + oz * k)) { if (T(dx, dz + oz * k) == T_LABWALL) break; if (Solidish(T(dx, dz + oz * k)) || T(dx, dz + oz * k) == T_PIT || T(dx, dz + oz * k) == T_DEEP) T(dx, dz + oz * k) = T_FLOOR; }
        (void)ox;
        LabPlan lp; lp.name = name; lp.x0 = x0 + 1; lp.z0 = z0 + 1; lp.x1 = x0 + 7; lp.z1 = z0 + 7; lp.doorX = dx; lp.doorZ = dz;
        auto at = [&](float cx, float cz) { return Vector3{(x0 + cx) * CELL, 0, (z0 + cz) * CELL}; };
        lp.spots = {{LP_RING, at(4.5f, 4.5f)}, {LP_DESK, at(4.5f, oz > 0 ? 2.0f : 7.0f)}, {LP_CRATE, at(1.6f, 2.0f)}, {LP_SHOP, at(7.4f, 2.0f)}, {LP_BUNK, at(1.6f, 7.0f)}, {LP_GEN, at(7.4f, 7.0f)}, {LP_MONITORS, at(1.6f, 4.5f)}, {LP_ARCHIVE, at(7.4f, 4.5f)}};
        L.labs.push_back(lp);
    }
    int FreeLabSpot(int& ox, int& oz, int prefX, int prefZ) {   // a 9 x 9 block that's mostly solid or floor away from other labs
        for (int tries = 0; tries < 400; tries++) {
            int x = std::clamp(prefX + RI(-12, 12), 2, L.w - 11), z = std::clamp(prefZ + RI(-12, 12), 2, L.h - 11);
            bool ok = true; for (const auto& l : L.labs) if (abs(l.x0 - x) < 14 && abs(l.z0 - z) < 14) ok = false;
            if (ok) { ox = x; oz = z; return 1; }
        }
        ox = std::clamp(prefX, 2, L.w - 11); oz = std::clamp(prefZ, 2, L.h - 11); return 0;
    }
    // the exits: far from the Labs and from each other
    void PlaceExits(const LevelDef& def) {
        std::vector<int> dist(L.w * L.h, 1 << 20);
        auto mergeFrom = [&](int x, int z) { auto d = Dist(x, z); for (size_t i = 0; i < d.size(); i++) if (d[i] >= 0) dist[i] = std::min(dist[i], d[i]); };
        Vector3 s0 = L.labs.empty() ? L.start : L.labs[0].spots[0].at; int sx = (int)(s0.x / CELL), sz = (int)(s0.z / CELL);
        if (!L.labs.empty()) { sx = L.labs[0].doorX; sz = L.labs[0].doorZ; }
        mergeFrom(sx, sz);
        std::vector<int> reach = dist;
        for (const auto& e : def.exits) {
            if (e.chance < 1 && Rand() > e.chance) continue;   // ("sometimes there")
            int best = -1, bs = -1;
            for (int k = 0; k < 600; k++) {
                int x = RI(1, L.w - 2), z = RI(1, L.h - 2), i = z * L.w + x;
                if (!Floorish(x, z) || reach[i] >= (1 << 20) || T(x, z) == T_LABFLOOR) continue;
                int score = std::min(dist[i], 60) * 3 + std::min(reach[i], 80) + RI(0, 10);
                if (e.kind == "pit" || e.kind == "abyss") score += T(x, z) == T_EDGE ? 200 : 0;
                if (score > bs) { bs = score; best = i; }
            }
            if (best < 0) continue;
            Exit ex; ex.cx = best % L.w; ex.cz = best / L.w; ex.to = e.to; ex.kind = e.kind; ex.label = e.label; ex.noclip = e.kind == "noclip" || e.kind == "pit" || e.kind == "abyss";
            L.exits.push_back(ex);
            mergeFrom(ex.cx, ex.cz);
            if (e.kind == "noclip") { const int D4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}}; for (auto& d : D4) if (In(ex.cx + d[0], ex.cz + d[1])) F(ex.cx + d[0], ex.cz + d[1]) |= CF_MANILA; F(ex.cx, ex.cz) |= CF_MANILA; }
            if (e.kind == "pit") F(ex.cx, ex.cz) |= CF_STAIN;
        }
    }
    void Lights(const LevelDef& def, int spacing = 2) {
        for (int z = 0; z < L.h; z++) for (int x = 0; x < L.w; x++) {
            int i = z * L.w + x; uint8_t t = L.tile[i];
            if (!L.Walkable(x, z) && t != T_LOW) { L.light[i] = 0; continue; }
            bool fixture = (x % spacing == 0 && z % spacing == 0) || spacing <= 1;
            if (!fixture) { L.light[i] = 0; continue; }
            if (Rand() > def.lit) { L.light[i] = 0; continue; }
            L.light[i] = Rand() < def.flicker ? 128 + RI(0, 100) : 255;   // (128..228: flickering at a phase)
        }
        // dead-light rooms (the Lobby's quiet rooms, where the Smilers are)
        int dead = (L.w * L.h) / 700;
        for (int k = 0; k < dead; k++) { int x = RI(4, L.w - 5), z = RI(4, L.h - 5); for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) { int i = (z + dz) * L.w + (x + dx); L.light[i] = 0; L.flags[i] |= CF_DEADLIGHT; } }
        for (const auto& lp : L.labs) for (int z = lp.z0; z <= lp.z1; z++) for (int x = lp.x0; x <= lp.x1; x++) { L.light[z * L.w + x] = 0; L.flags[z * L.w + x] &= ~CF_DEADLIGHT; }   // (a Lab's lights are its generator's)
    }
    void Loot(const LevelDef& def, int count) {
        (void)def;
        for (int k = 0; k < count * 8 && (int)L.loot.size() < count; k++) {
            int x = RI(1, L.w - 2), z = RI(1, L.h - 2); if (!Floorish(x, z) || T(x, z) == T_LABFLOOR) continue;
            bool near = false; for (const auto& o : L.loot) if (fabsf(o.at.x - (x + 0.5f) * CELL) < CELL * 2 && fabsf(o.at.z - (z + 0.5f) * CELL) < CELL * 2) near = true; if (near) continue;
            L.loot.push_back({{(x + 0.2f + Rand() * 0.6f) * CELL, 0, (z + 0.2f + Rand() * 0.6f) * CELL}, -1});
        }
        for (int k = 0; k < count / 3 + 4; k++) { int x = RI(1, L.w - 2), z = RI(1, L.h - 2); if (Floorish(x, z) && T(x, z) != T_LABFLOOR) L.spawns.push_back(L.Center(x, z)); }
    }
};

// ---------------------------------------------------------------- the kits
void KitMaze(Gen& g, bool glass) {   // Level 0: a maze of rooms, with walls knocked out so it's never quite a maze; Level 13's glass lattice
    Level& L = g.L; g.Fill(T_WALL);
    for (int z = 1; z < L.h - 1; z += 2) for (int x = 1; x < L.w - 1; x += 2) g.T(x, z) = T_FLOOR;
    std::vector<uint8_t> seen(L.w * L.h, 0); std::vector<std::pair<int, int>> st{{1, 1}}; seen[L.w + 1] = 1;
    while (!st.empty()) {
        auto [x, z] = st.back(); int dirs[4] = {0, 1, 2, 3}; for (int i = 3; i > 0; i--) std::swap(dirs[i], dirs[g.RI(0, i)]);
        bool moved = false;
        for (int d : dirs) { int dx = d == 0 ? 2 : d == 1 ? -2 : 0, dz = d == 2 ? 2 : d == 3 ? -2 : 0, nx = x + dx, nz = z + dz; if (nx < 1 || nz < 1 || nx >= L.w - 1 || nz >= L.h - 1 || seen[nz * L.w + nx]) continue; g.T(x + dx / 2, z + dz / 2) = T_FLOOR; seen[nz * L.w + nx] = 1; st.push_back({nx, nz}); moved = true; break; }
        if (!moved) st.pop_back();
    }
    for (int z = 1; z < L.h - 1; z++) for (int x = 1; x < L.w - 1; x++) if (g.T(x, z) == T_WALL && g.Rand() < 0.42f) { bool h = g.T(x - 1, z) == T_FLOOR && g.T(x + 1, z) == T_FLOOR, v = g.T(x, z - 1) == T_FLOOR && g.T(x, z + 1) == T_FLOOR; if (h || v) g.T(x, z) = T_FLOOR; }
    for (int k = 0; k < (L.w * L.h) / 260; k++) { int x = g.RI(3, L.w - 9), z = g.RI(3, L.h - 9); g.Rect(x, z, x + g.RI(3, 6), z + g.RI(3, 6), T_FLOOR); if (g.Rand() < 0.5f) g.T(x + 2, z + 2) = T_WALL; }   // bigger rooms, a pillar
    for (int k = 0; k < (L.w * L.h) / 400; k++) { int x = g.RI(2, L.w - 3), z = g.RI(2, L.h - 3); if (g.T(x, z) == T_FLOOR) g.F(x, z) |= CF_STAIN; }   // damp carpet stains
    if (glass) {   // glass walls you can see through, mirrors, frosted panes
        for (int z = 1; z < L.h - 1; z++) for (int x = 1; x < L.w - 1; x++) if (g.T(x, z) == T_WALL) { float r = g.Rand(); if (r < 0.55f) g.T(x, z) = T_GLASS; else if (r < 0.7f) g.F(x, z) |= CF_MIRROR; }
    }
}
void KitHalls(Gen& g) {   // Level 1: big concrete halls with racks of crates, joined by corridors and ramps; fog in low rooms
    Level& L = g.L; g.Fill(T_WALL);
    auto rooms = g.Rooms(11, 7, 13, T_FLOOR, 2, 0.35f);
    for (const auto& r : rooms) {
        for (int z = r.z0 + 2; z <= r.z1 - 2; z += 3) for (int x = r.x0 + 2; x <= r.x1 - 2; x++) if (g.Rand() < 0.75f && (x - r.x0) % 5 != 0) g.T(x, z) = T_LOW;   // racks
        if (g.Rand() < 0.25f) g.RectFlag(r.x0, r.z0, r.x1, r.z1, CF_COLD);   // (a fog room)
    }
}
void KitCorridors(Gen& g, uint16_t wallFlag, int rooms, int roomMin, int roomMax) {   // Pipe Dreams, the hospital, the asylum: corridors that branch, side rooms
    Level& L = g.L; g.Fill(T_WALL);
    // a corridor graph: random walkers that branch
    struct W { int x, z, dx, dz, life; }; std::vector<W> walkers{{L.w / 2, L.h / 2, 1, 0, 400}};
    int carved = 0;
    while (!walkers.empty() && carved < L.w * L.h / 3) {
        W w = walkers.back(); walkers.pop_back();
        for (int s = 0; s < w.life; s++) {
            if (!g.In(w.x, w.z) || w.x < 2 || w.z < 2 || w.x > L.w - 3 || w.z > L.h - 3) { w.dx = -w.dx; w.dz = -w.dz; w.x += w.dx * 2; w.z += w.dz * 2; continue; }
            if (g.T(w.x, w.z) != T_FLOOR) { g.T(w.x, w.z) = T_FLOOR; carved++; }
            if (g.Rand() < 0.08f) { int t = w.dx; w.dx = g.Rand() < 0.5f ? w.dz : -w.dz; w.dz = t ? (g.Rand() < 0.5f ? 0 : 0) : (g.Rand() < 0.5f ? 1 : -1); if (!w.dx && !w.dz) w.dx = 1; }
            if (g.Rand() < 0.03f && walkers.size() < 12) walkers.push_back({w.x, w.z, w.dz, w.dx, 60 + g.RI(0, 120)});
            if (g.Rand() < 0.015f) break;   // (a dead end)
            w.x += w.dx; w.z += w.dz;
        }
    }
    // side rooms off the corridors, through doors
    for (int k = 0, made = 0; k < rooms * 20 && made < rooms; k++) {
        int x = g.RI(3, L.w - 8), z = g.RI(3, L.h - 8), w = g.RI(roomMin, roomMax), h = g.RI(roomMin, roomMax);
        bool clear = true; for (int zz = z - 1; zz <= z + h; zz++) for (int xx = x - 1; xx <= x + w; xx++) if (g.T(xx, zz) != T_WALL) clear = false;
        if (!clear) continue;
        // a door to the nearest corridor cell on any side
        int doorX = -1, doorZ = -1; for (int d = 2; d < 6 && doorX < 0; d++) { if (g.T(x + w / 2, z - d) == T_FLOOR) { doorX = x + w / 2; doorZ = z - 1; for (int q = z - d + 1; q < z; q++) g.T(doorX, q) = T_FLOOR; } else if (g.T(x + w / 2, z + h - 1 + d) == T_FLOOR) { doorX = x + w / 2; doorZ = z + h; for (int q = z + h + 1; q < z + h - 1 + d; q++) g.T(doorX, q) = T_FLOOR; } }
        if (doorX < 0) continue;
        g.Rect(x, z, x + w - 1, z + h - 1, T_FLOOR); g.RectFlag(x, z, x + w - 1, z + h - 1, CF_ROOM); g.T(doorX, doorZ) = T_DOOR; made++;
    }
    for (int z = 0; z < L.h; z++) for (int x = 0; x < L.w; x++) if (g.T(x, z) == T_WALL && g.Rand() < 0.7f) g.F(x, z) |= wallFlag;
}
void KitMachines(Gen& g, bool future) {   // the Electrical Station's machine halls in a grid, catwalk-free in 2D; the future's clean halls
    Level& L = g.L; g.Fill(T_WALL);
    int blk = 9;
    for (int bz = 0; bz * blk + blk < L.h; bz++) for (int bx = 0; bx * blk + blk < L.w; bx++) {
        int x0 = 2 + bx * blk, z0 = 2 + bz * blk; g.Rect(x0, z0, x0 + blk - 3, z0 + blk - 3, T_FLOOR);
        if (!future) { for (int k = 0; k < 3; k++) { int mx = x0 + g.RI(1, blk - 6), mz = z0 + g.RI(1, blk - 6); g.Rect(mx, mz, mx + 1, mz + g.RI(0, 2), T_LOW); } if (g.Rand() < 0.25f) g.RectFlag(x0, z0, x0 + blk - 3, z0 + blk - 3, CF_LIVE); g.RectFlag(x0, z0, x0 + blk - 3, z0 + blk - 3, CF_HOT); }
        else if (g.Rand() < 0.5f) { int mx = x0 + 2, mz = z0 + 2; g.Rect(mx, mz, mx + 2, mz, T_LOW); }
        // doors between neighbouring halls
        if (x0 + blk + blk - 3 < L.w) { int dz = z0 + g.RI(1, blk - 5); g.Rect(x0 + blk - 2, dz, x0 + blk - 1, dz + (future ? 0 : 1), future ? T_DOOR : T_FLOOR); }
        if (z0 + blk + blk - 3 < L.h) { int dx = x0 + g.RI(1, blk - 5); g.Rect(dx, z0 + blk - 2, dx + (future ? 0 : 1), z0 + blk - 1, future ? T_DOOR : T_FLOOR); }
    }
}
void KitOffice(Gen& g) {   // an endless office floor: cubicles of low partitions, meeting rooms, windows that show only dark
    Level& L = g.L; g.Fill(T_WALL); g.Rect(2, 2, L.w - 3, L.h - 3, T_FLOOR);
    for (int z = 4; z < L.h - 5; z += 4) for (int x = 4; x < L.w - 5; x += 4) {
        if (g.Rand() < 0.15f) continue;
        g.Rect(x, z, x + 2, z, T_LOW); g.Rect(x, z, x, z + 2, T_LOW); if (g.Rand() < 0.5f) g.Rect(x + 2, z, x + 2, z + 1, T_LOW);   // a cubicle, open on one side
    }
    for (int k = 0; k < 8; k++) { int x = g.RI(3, L.w - 12), z = g.RI(3, L.h - 10), w = g.RI(5, 8), h = g.RI(4, 6); g.Rect(x, z, x + w, z + h, T_WALL); g.Rect(x + 1, z + 1, x + w - 1, z + h - 1, T_FLOOR); g.RectFlag(x + 1, z + 1, x + w - 1, z + h - 1, CF_ROOM); g.T(x + w / 2, z) = T_DOOR; }
    for (int x = 2; x < L.w - 2; x++) { g.F(x, 1) |= CF_WINDOW; g.F(x, L.h - 2) |= CF_WINDOW; } for (int z = 2; z < L.h - 2; z++) { g.F(1, z) |= CF_WINDOW; g.F(L.w - 2, z) |= CF_WINDOW; }
}
void KitHotel(Gen& g) {   // corridors with numbered rooms behind doors; a ballroom, a kitchen, the boiler room
    Level& L = g.L; g.Fill(T_WALL);
    for (int z = 4; z < L.h - 4; z += 9) g.Rect(2, z, L.w - 3, z + 1, T_FLOOR);           // the corridors
    for (int x = 4; x < L.w - 4; x += 22) g.Rect(x, 2, x + 1, L.h - 3, T_FLOOR);           // the cross halls
    for (int z = 4; z < L.h - 4; z += 9) for (int x = 3; x < L.w - 6; x += 4) {
        for (int side = -1; side <= 1; side += 2) {
            int rz0 = side < 0 ? z - 3 : z + 3, rz1 = side < 0 ? z - 1 : z + 5; if (rz0 < 2 || rz1 > L.h - 3) continue;
            bool clear = true; for (int zz = std::min(rz0, rz1); zz <= std::max(rz0, rz1); zz++) for (int xx = x; xx <= x + 2; xx++) if (g.T(xx, zz) != T_WALL) clear = false; if (!clear) continue;
            g.Rect(x, std::min(rz0, rz1), x + 2, std::max(rz0, rz1), T_FLOOR); g.RectFlag(x, std::min(rz0, rz1), x + 2, std::max(rz0, rz1), CF_ROOM);
            g.T(x + 1, side < 0 ? z - 1 : z + 2) = T_DOOR; if (side > 0) g.T(x + 1, z + 2) = T_DOOR;
        }
    }
    int bx = L.w / 2 - 6, bz = L.h / 2 - 4; g.Rect(bx, bz, bx + 12, bz + 8, T_FLOOR);   // the ballroom
}
void KitLong(Gen& g) {   // Lights Out: one long winding corridor with occasional rooms; walk far enough and it ends
    Level& L = g.L; g.Fill(T_WALL);
    int x = 3, z = 3, dir = 0; int len = 0;
    while (len < 1400) {
        g.T(x, z) = T_FLOOR; len++;
        if (g.Rand() < 0.1f) dir = (dir + (g.Rand() < 0.5f ? 1 : 3)) % 4;
        int dx = dir == 0 ? 1 : dir == 2 ? -1 : 0, dz = dir == 1 ? 1 : dir == 3 ? -1 : 0;
        if (x + dx < 3 || x + dx > L.w - 4 || z + dz < 3 || z + dz > L.h - 4) { dir = (dir + 1) % 4; continue; }
        x += dx; z += dz;
        if (g.Rand() < 0.012f) { g.Rect(x - 2, z - 2, x + 2, z + 2, T_FLOOR); g.RectFlag(x - 2, z - 2, x + 2, z + 2, CF_ROOM); }
        if (g.Rand() < 0.01f) { int pw = g.RI(1, 2); g.Rect(x, z, x + pw, z + pw, T_PIT); }   // a pit in the dark
    }
    L.start = L.Center(x, z);   // (the far end: the walk back to the Lobby)
}
void KitWater(Gen& g) {   // Thalassophobia: a house's few rooms above an endless dark sea; the basement flooded
    Level& L = g.L; g.Fill(T_DEEP);
    for (int z = 0; z < L.h; z++) for (int x = 0; x < L.w; x++) if (x == 0 || z == 0 || x == L.w - 1 || z == L.h - 1) g.T(x, z) = T_VOID;
    int hx = 4, hz = 4; g.Rect(hx, hz, hx + 14, hz + 12, T_WALL);
    g.Rect(hx + 1, hz + 1, hx + 6, hz + 5, T_FLOOR); g.Rect(hx + 8, hz + 1, hx + 13, hz + 5, T_FLOOR); g.Rect(hx + 1, hz + 7, hx + 13, hz + 11, T_WATER);   // rooms, the flooded basement
    g.T(hx + 7, hz + 3) = T_DOOR; g.T(hx + 4, hz + 6) = T_DOOR; g.T(hx + 10, hz + 6) = T_DOOR; g.T(hx + 14, hz + 3) = T_DOOR; g.Rect(hx + 15, hz + 2, hx + 18, hz + 4, T_WATER);   // the front door onto the water
    g.RectFlag(hx + 1, hz + 1, hx + 13, hz + 11, CF_ROOM | CF_COLD);
    for (int k = 0; k < 18; k++) { int x = g.RI(22, L.w - 4), z = g.RI(4, L.h - 4); g.Rect(x, z, x + g.RI(0, 1), z + g.RI(0, 1), T_WATER); }   // shallows (a breath)
    L.start = L.Center(hx + 3, hz + 3);
}
void KitCaves(Gen& g) {   // caves from noise: cellular automata, chasms with bridges, webs, an underground river
    Level& L = g.L;
    for (int z = 0; z < L.h; z++) for (int x = 0; x < L.w; x++) g.T(x, z) = (x < 2 || z < 2 || x > L.w - 3 || z > L.h - 3 || g.Rand() < 0.45f) ? T_WALL : T_FLOOR;
    for (int it = 0; it < 5; it++) { std::vector<uint8_t> nt = L.tile; for (int z = 1; z < L.h - 1; z++) for (int x = 1; x < L.w - 1; x++) { int n = 0; for (int dz = -1; dz <= 1; dz++) for (int dx = -1; dx <= 1; dx++) n += g.T(x + dx, z + dz) == T_WALL; nt[z * L.w + x] = n >= 5 ? T_WALL : T_FLOOR; } L.tile = nt; }
    for (int k = 0; k < 6; k++) { int x = g.RI(6, L.w - 10), z = g.RI(6, L.h - 10); for (int dz = 0; dz < 3; dz++) for (int dx = 0; dx < g.RI(3, 6); dx++) if (g.T(x + dx, z + dz) == T_FLOOR) g.T(x + dx, z + dz) = dz == 1 && dx == 2 ? T_FLOOR : T_PIT; }   // chasms, a bridge across
    for (int z = 1; z < L.h - 1; z++) for (int x = 1; x < L.w - 1; x++) if (g.T(x, z) == T_FLOOR && g.Rand() < 0.03f) g.F(x, z) |= CF_WEB;
    int rz = g.RI(L.h / 3, 2 * L.h / 3); for (int x = 2; x < L.w - 2; x++) { int zz = rz + (int)(sinf(x * 0.2f) * 3); if (g.T(x, zz) == T_FLOOR) g.T(x, zz) = T_WATER; }
}
void KitStreets(Gen& g, bool city) {   // the suburbs: a street grid, houses (small dungeons) in yards, streetlights; the city: tall blocks
    Level& L = g.L; g.Fill(T_FLOOR);
    for (int z = 0; z < L.h; z++) for (int x = 0; x < L.w; x++) if (x == 0 || z == 0 || x == L.w - 1 || z == L.h - 1) g.T(x, z) = T_WALL;
    int blk = city ? 12 : 10;
    for (int bz = 0; bz * blk + 4 < L.h - 2; bz++) for (int bx = 0; bx * blk + 4 < L.w - 2; bx++) {
        int x0 = 3 + bx * blk, z0 = 3 + bz * blk, x1 = std::min(L.w - 3, x0 + blk - 4), z1 = std::min(L.h - 3, z0 + blk - 4);
        g.F(x0 - 1, z0 - 1) |= CF_STREETLIGHT;
        if (city) {   // a tower: walls, a lobby open from the street, sometimes the whole ground floor
            g.Rect(x0, z0, x1, z1, T_WALL); if (g.Rand() < 0.55f) { g.Rect(x0 + 1, z0 + 1, x1 - 1, z1 - 1, T_FLOOR); g.RectFlag(x0 + 1, z0 + 1, x1 - 1, z1 - 1, CF_ROOM); g.T((x0 + x1) / 2, z1) = T_DOOR; }
            for (int x = x0; x <= x1; x++) { g.F(x, z0) |= CF_WINDOW; g.F(x, z1) |= CF_WINDOW; }
        } else {   // two houses a block, each a few rooms with a front door
            for (int hsx = 0; hsx < 2; hsx++) {
                int hx0 = x0 + hsx * (blk / 2 - 1), hx1 = std::min(x1, hx0 + blk / 2 - 3), hz0 = z0 + 1, hz1 = z1 - 1;
                if (hx1 - hx0 < 3) continue;
                g.Rect(hx0, hz0, hx1, hz1, T_WALL); g.Rect(hx0 + 1, hz0 + 1, hx1 - 1, hz1 - 1, T_FLOOR); g.RectFlag(hx0 + 1, hz0 + 1, hx1 - 1, hz1 - 1, CF_ROOM);
                if (hz1 - hz0 > 4) g.Rect(hx0 + 1, (hz0 + hz1) / 2, hx1 - 1, (hz0 + hz1) / 2, T_WALL), g.T((hx0 + hx1) / 2, (hz0 + hz1) / 2) = T_DOOR;
                g.T((hx0 + hx1) / 2, hz1) = T_DOOR; g.F(hx0, hz0) |= CF_WINDOW; g.F(hx1, hz0) |= CF_WINDOW;
            }
        }
    }
}
void KitWheat(Gen& g) {   // an endless wheat field; a dirt road; a barn, a silo, scarecrows on poles
    Level& L = g.L; g.Fill(T_WHEAT);
    for (int z = 0; z < L.h; z++) for (int x = 0; x < L.w; x++) if (x == 0 || z == 0 || x == L.w - 1 || z == L.h - 1) g.T(x, z) = T_WALL;
    int rz = L.h / 2; for (int x = 1; x < L.w - 1; x++) { int zz = rz + (int)(sinf(x * 0.08f) * 6); g.Rect(x, zz, x, zz + 1, T_FLOOR); }
    int rx = L.w / 3; for (int z = 1; z < L.h - 1; z++) g.Rect(rx, z, rx + 1, z, T_FLOOR);
    for (int k = 0; k < 4; k++) { int x = g.RI(6, L.w - 14), z = g.RI(6, L.h - 12); g.Rect(x, z, x + 7, z + 5, T_WALL); g.Rect(x + 1, z + 1, x + 6, z + 4, T_FLOOR); g.RectFlag(x + 1, z + 1, x + 6, z + 4, CF_ROOM); g.T(x + 3, z + 5) = T_DOOR; g.Rect(x + 2, z + 6, x + 4, z + 7, T_FLOOR); }   // barns
    for (int k = 0; k < 14; k++) { int x = g.RI(3, L.w - 4), z = g.RI(3, L.h - 4); if (g.T(x, z) == T_WHEAT) g.T(x, z) = T_LOW; }   // poles
}
void KitRoomsKit(Gen& g, int n, uint16_t extra) {   // Aedificium's living house; Nostalgic Memories' stitched rooms: rooms joined by doors
    Level& L = g.L; g.Fill(T_WALL);
    auto rooms = g.Rooms(n, 3, 7, T_FLOOR, 1, 0.5f);
    for (const auto& r : rooms) { g.RectFlag(r.x0, r.z0, r.x1, r.z1, CF_ROOM | extra); }
    // doors where corridors meet room walls
    for (int z = 1; z < L.h - 1; z++) for (int x = 1; x < L.w - 1; x++) if (g.T(x, z) == T_FLOOR && !(g.F(x, z) & CF_ROOM) && g.Rand() < 0.1f) { bool between = (g.T(x - 1, z) == T_WALL && g.T(x + 1, z) == T_WALL) || (g.T(x, z - 1) == T_WALL && g.T(x, z + 1) == T_WALL); if (between) g.T(x, z) = T_DOOR; }
}
void KitCarrier(Gen& g) {   // the carrier: a long hull of steel corridors; the flight deck open along one side
    Level& L = g.L; g.Fill(T_WALL);
    for (int z = 0; z < L.h; z++) for (int x = 0; x < L.w; x++) if (z < 2 || z > L.h - 3 || x < 2 || x > L.w - 3) g.T(x, z) = T_VOID;
    g.Rect(3, 3, L.w - 4, 12, T_FLOOR); g.RectFlag(3, 3, L.w - 4, 12, CF_COLD);   // the flight deck
    for (int k = 0; k < 9; k++) { int x = g.RI(6, L.w - 12); g.Rect(x, g.RI(5, 8), x + 3, g.RI(9, 10), T_LOW); }   // derelict aircraft
    for (int z = 16; z < L.h - 4; z += 5) g.Rect(3, z, L.w - 4, z, T_FLOOR);       // corridors below
    for (int x = 4; x < L.w - 4; x += 7) g.Rect(x, 13, x, L.h - 4, T_FLOOR);        // ladders and passages down
    for (int k = 0; k < 24; k++) { int x = g.RI(4, L.w - 8), z = g.RI(17, L.h - 7); g.Rect(x, z, x + 2, z + 2, T_FLOOR); g.RectFlag(x, z, x + 2, z + 2, CF_ROOM); }
    for (int z = 13; z < L.h - 3; z++) for (int x = 3; x < L.w - 3; x++) if (g.T(x, z) == T_FLOOR && g.Rand() < 0.04f && g.T(x - 1, z) == T_WALL && g.T(x + 1, z) == T_WALL) g.T(x, z) = T_DOOR;   // bulkheads
}
void KitInn(Gen& g) {   // the inn along a cliff: rooms above, a balcony and rooms on stilts over the edge, then nothing
    Level& L = g.L; g.Fill(T_WALL);
    int cliff = L.h - 14;
    g.Rect(2, 2, L.w - 3, cliff - 1, T_WALL);
    auto rooms = g.Rooms(14, 3, 6, T_FLOOR, 1, 0.3f);
    for (const auto& r : rooms) g.RectFlag(r.x0, r.z0, r.x1, r.z1, CF_ROOM);
    for (int z = 0; z < L.h; z++) for (int x = 0; x < L.w; x++) if (z >= cliff) g.T(x, z) = z < cliff + 2 ? T_EDGE : T_PIT;
    g.Rect(4, cliff - 3, L.w - 5, cliff - 1, T_FLOOR);   // the long balcony corridor along the edge
    for (int k = 0; k < 5; k++) { int x = g.RI(6, L.w - 10); g.Rect(x, cliff, x + 3, cliff + 4, T_FLOOR); g.RectFlag(x, cliff, x + 3, cliff + 4, CF_ROOM); }   // rooms on stilts
    for (int x = 0; x < L.w; x++) { g.T(x, L.h - 1) = T_VOID; }
}

}  // namespace

// ---------------------------------------------------------------- the generator for a level
Level Generate(int level, uint32_t daySeed) {
    const LevelDef& def = D().levels[std::clamp(level, 0, (int)D().levels.size() - 1)];
    Level L; L.id = def.id; L.seed = daySeed * 7919u + (uint32_t)level * 104729u + 17; L.w = def.w; L.h = def.h;
    L.tile.assign(L.w * L.h, T_WALL); L.flags.assign(L.w * L.h, 0); L.light.assign(L.w * L.h, 0);
    Gen g(L, L.seed);
    const std::string& k = def.kit;
    int spacing = 2;
    if (k == "maze") KitMaze(g, false);
    else if (k == "glass") KitMaze(g, true);
    else if (k == "halls") { KitHalls(g); spacing = 3; }
    else if (k == "corridors") KitCorridors(g, CF_PIPES, 24, 3, 5);
    else if (k == "hospital") KitCorridors(g, 0, 30, 3, 6);
    else if (k == "asylum") { KitCorridors(g, 0, 40, 2, 3); for (int z = 0; z < L.h; z++) for (int x = 0; x < L.w; x++) if (L.flags[z * L.w + x] & CF_ROOM) L.flags[z * L.w + x] |= CF_CELL; }
    else if (k == "machines") { KitMachines(g, false); spacing = 3; }
    else if (k == "future") KitMachines(g, true);
    else if (k == "office") KitOffice(g);
    else if (k == "hotel") KitHotel(g);
    else if (k == "long") KitLong(g);
    else if (k == "water") KitWater(g);
    else if (k == "caves") KitCaves(g);
    else if (k == "suburbs") { KitStreets(g, false); spacing = 1; }
    else if (k == "city") { KitStreets(g, true); spacing = 1; }
    else if (k == "wheat") { KitWheat(g); spacing = 1; }
    else if (k == "livinghouse") KitRoomsKit(g, 22, 0);
    else if (k == "memories") KitRoomsKit(g, 20, 0);
    else if (k == "carrier") KitCarrier(g);
    else if (k == "inn") KitInn(g);
    else KitMaze(g, false);
    // the Labs: the first near a third of the way in, the second across the level
    for (size_t i = 0; i < def.labs.size(); i++) { int ox, oz; g.FreeLabSpot(ox, oz, i == 0 ? L.w / 4 : 3 * L.w / 4 - 8, i == 0 ? L.h / 4 : 3 * L.h / 4 - 8); if (k == "inn") oz = std::min(oz, L.h - 26); g.PlaceLab(def.labs[i], ox, oz); }
    g.JoinAll(k == "water" ? T_WATER : T_FLOOR);
    if (L.labs.empty()) { /* (Level 7: no Lab; the house is the landing) */ }
    else { const LabPlan& lp = L.labs[0]; L.start = L.Center(lp.doorX, lp.doorZ + (lp.doorZ > lp.z1 ? 1 : -1)); }
    if (k == "water") L.start = L.Center(7, 7);
    // a breaker for each Lab, somewhere in the level (the blast door needs its power)
    for (auto& lp : L.labs) { for (int tries = 0; tries < 400; tries++) { int x = g.RI(2, L.w - 3), z = g.RI(2, L.h - 3); if (g.Floorish(x, z) && L.At(x, z) != T_LABFLOOR && abs(x - lp.doorX) + abs(z - lp.doorZ) > 14) { lp.breakerX = x; lp.breakerZ = z; break; } } }
    g.PlaceExits(def);
    g.Lights(def, spacing);
    if (k == "suburbs" || k == "city") for (int i = 0; i < L.w * L.h; i++) { if (L.flags[i] & CF_STREETLIGHT) { int x = i % L.w, z = i / L.w; for (int dz = -2; dz <= 2; dz++) for (int dx = -2; dx <= 2; dx++) { int xx = x + dx, zz = z + dz; if (xx < 0 || zz < 0 || xx >= L.w || zz >= L.h) continue; int v = 255 - (abs(dx) + abs(dz)) * 50; if (k == "suburbs" && g.Rand() < 0.3f) v = 0; L.light[zz * L.w + xx] = (uint8_t)std::max<int>(L.light[zz * L.w + xx], std::max(0, v)); } } else if (k == "suburbs" && !(L.flags[i] & CF_ROOM)) L.light[i] = std::min<uint8_t>(L.light[i], 0); }
    int lootCount = (L.w * L.h) / (level >= 10 ? 70 : 55);
    g.Loot(def, lootCount);
    return L;
}

bool CheckReachable(const Level& L, std::string* why) {
    // from each Lab's door, every exit must be reachable by walking (doors, blast doors, water and wheat pass)
    for (const auto& lp : L.labs) {
        std::vector<int> d(L.w * L.h, -1); std::queue<int> q; int s = lp.doorZ * L.w + lp.doorX; d[s] = 0; q.push(s);
        while (!q.empty()) { int c = q.front(); q.pop(); int x = c % L.w, z = c / L.w; const int D4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}}; for (auto& dd : D4) { int nx = x + dd[0], nz = z + dd[1]; if (nx < 0 || nz < 0 || nx >= L.w || nz >= L.h) continue; int ni = nz * L.w + nx; uint8_t t = L.At(nx, nz); if (d[ni] < 0 && L.Walkable(nx, nz) && t != T_PIT) { d[ni] = d[c] + 1; q.push(ni); } } }
        for (const auto& e : L.exits) { int i = e.cz * L.w + e.cx; bool near = d[i] >= 0; if (!near) { const int D4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}}; for (auto& dd : D4) { int nx = e.cx + dd[0], nz = e.cz + dd[1]; if (nx >= 0 && nz >= 0 && nx < L.w && nz < L.h && d[nz * L.w + nx] >= 0) near = true; } } if (!near) { if (why) *why = "Lab " + lp.name + " can't reach the exit '" + e.label + "'"; return false; } }
        // the Lab's own ring from its door
        int ri = ((lp.z0 + lp.z1) / 2) * L.w + (lp.x0 + lp.x1) / 2; if (d[ri] < 0) { if (why) *why = "Lab " + lp.name + "'s ring can't be reached from its door"; return false; }
    }
    if (L.labs.empty() && L.exits.empty()) { if (why) *why = "no Lab and no exit"; return false; }
    return true;
}
std::string DescribeLevel(const Level& L) {
    std::string s; static const char C[T_COUNT] = {' ', '.', '#', '=', '%', '~', 'w', 'O', '+', 'L', ',', 'B', '"', '_'};
    for (int z = 0; z < L.h; z++) {
        for (int x = 0; x < L.w; x++) {
            char ch = C[L.tile[z * L.w + x]];
            for (const auto& e : L.exits) if (e.cx == x && e.cz == z) ch = e.noclip ? 'N' : 'E';
            for (const auto& lp : L.labs) if (x == (lp.x0 + lp.x1) / 2 && z == (lp.z0 + lp.z1) / 2) ch = '@';
            s += ch;
        }
        s += '\n';
    }
    return s;
}

}  // namespace nc
