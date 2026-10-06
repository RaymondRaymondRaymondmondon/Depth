// Fathoms' headless core (see fathoms.h): setup, commands, and the 20-tick step. The doc's sections map here:
// "Resources and economy" (gathering, drop-offs, population, upkeep, the Exchange), "Buildings" (placement, workers
// building together, garrisons, Lighthouses and territory), "Eras and technology", "Shared units and combat rules"
// (the damage formula, terrain, landings, morale and supply, coaling range, status effects), relics, score and the
// victory conditions. Neutral powers are fathoms_neutral.cpp; faction mechanics fathoms_faction.cpp; the AI fathoms_ai.cpp.
#include "fathoms.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <queue>
#include <unordered_map>

namespace fa {

float World::Rand() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 16777216.0f; }
Unit* World::U(int id) { for (auto& u : units) if (u.id == id && !u.dead) return &u; return nullptr; }
const Unit* World::U(int id) const { for (const auto& u : units) if (u.id == id && !u.dead) return &u; return nullptr; }
Building* World::Bd(int id) { for (auto& b : buildings) if (b.id == id && !b.dead) return &b; return nullptr; }
const Building* World::Bd(int id) const { for (const auto& b : buildings) if (b.id == id && !b.dead) return &b; return nullptr; }

bool World::Enemies(int a, int b) const {
    if (a == b) return false;
    if (!IsPlayer(a) && !IsPlayer(b)) return false;   // (the neutrals never fight each other)
    if (!IsPlayer(a) || !IsPlayer(b)) return true;
    if (a >= (int)players.size() || b >= (int)players.size()) return false;
    if (set.peace > 0 && t < set.peace * 60) return false;   // (the peace timer)
    return players[a].stance[b] == DP_WAR || players[b].stance[a] == DP_WAR;
}
bool World::Allied(int a, int b) const { return a == b || (IsPlayer(a) && IsPlayer(b) && a < (int)players.size() && b < (int)players.size() && players[a].stance[b] == DP_ALLIED && players[b].stance[a] == DP_ALLIED); }
bool World::Sees(int player, Vector2 p) const {
    if (!IsPlayer(player) || player >= (int)players.size() || set.reveal == 2) return true;
    int x = (int)p.x, y = (int)p.y; if (!In(x, y)) return false;
    return players[player].seen.empty() || players[player].seen[Idx(x, y)];
}
bool World::Passable(int x, int y, Move m, int faction) const {
    if (!In(x, y)) return false;
    int t = tile[Idx(x, y)];
    if (m == MV_AIR) return true;
    if (t == T_MOUNTAIN) return false;
    if (t == T_LAVA) return false;
    bool water = Water(t);
    if (m == MV_SEA) return water;
    int o = occ[Idx(x, y)];
    if (o >= 0) { const Building* b = Bd(o); if (b && !BD(*b).wall && BD(*b).key == "farm") {} else if (b) return false; }
    if (m == MV_AMPHIB) return true;
    // land: land tiles and shallows (wading); merfolk swim anywhere; Crustaceans walk the shallows
    if (water) return (t == T_SHALLOW && faction == 2) || faction == 3;   // (only the Brood walk the shallows and the Merfolk swim: everyone else needs a boat)
    return true;
}
int World::PopCap(int player) const {
    const Player& p = players[player]; int cap = 0;
    for (const auto& b : buildings) if (!b.dead && b.owner == player && b.progress >= 1) cap += BD(b).pop;
    int n = (int)players.size(); int hard = n <= 2 ? B().cap2 : n <= 4 ? B().cap4 : B().cap6; (void)p;
    return std::min(cap, hard);
}
bool World::HasTech(int player, const std::string& key) const { int i = B().Tech(key); return i >= 0 && IsPlayer(player) && player < (int)players.size() && i < (int)players[player].tech.size() && players[player].tech[i]; }
float World::TechSum(int player, const std::string& fx) const {
    if (!IsPlayer(player) || player >= (int)players.size()) return 0; float s = 0; const auto& T = B().techs;
    for (size_t i = 0; i < T.size() && i < players[player].tech.size(); i++) if (players[player].tech[i]) for (const auto& f : T[i].fx) if (f.first == fx) s += f.second;
    return s;
}
float World::GatherRate(int player, const Node& n) const {
    const Balance& Bl = B(); float r = Bl.nodes[n.kind].rate; if (!IsPlayer(player)) return r;
    const Player& p = players[player]; const FactionDef& f = Bl.factions[p.faction];
    if (n.kind == N_FISH || n.kind == N_KELP || n.kind == N_PEARL) r *= 1 + TechSum(player, "gather_fish");
    if (n.kind == N_FARM || n.kind == N_GROVE) r *= 1 + TechSum(player, "gather_farm");
    if (n.kind == N_BRASS || n.kind == N_COAL) r *= 1 + TechSum(player, "gather_mine");
    if (n.kind == N_VENT) { r *= 1 + TechSum(player, "gather_ichor") + (HasCard(*this, player, 3) ? 0.3f : 0); if (n.cap >= Bl.volcanoVent - 1) r *= Bl.volcanoRate; }
    r *= f.gatherMult[n.kind] * f.eraGather[std::clamp(p.era, 0, 2)] * p.handicap;
    if (p.ai && p.difficulty >= 0 && p.difficulty < (int)Bl.ai.size()) r *= Bl.ai[p.difficulty].gatherMult;
    return r;
}
Cost World::PriceOf(int player, const UnitDef& d) const {
    Cost c = d.cost; if (!IsPlayer(player)) return c; const FactionDef& f = B().factions[players[player].faction];
    for (auto& x : c) x *= f.costMult;
    if (f.foodToCoal > 0 && !(d.tags & TG_WORKER) && !(d.tags & TG_SHIP && d.attack <= 0)) { c[R_COAL] += c[R_FOOD] * f.foodToCoal; c[R_FOOD] = 0; }
    return c;
}
static bool Afford(const Player& p, const Cost& c) { for (int r = 0; r < R_COUNT; r++) if (p.res[r] + 1e-3f < c[r]) return false; return true; }
static void Pay(Player& p, const Cost& c) { for (int r = 0; r < R_COUNT; r++) p.res[r] -= c[r]; }
static void Refund(Player& p, const Cost& c, float k = 1) { for (int r = 0; r < R_COUNT; r++) p.res[r] += c[r] * k; }

// ---------------------------------------------------------------- setup
int World::SpawnUnit(int player, int def, Vector2 p) {
    const UnitDef& d = B().units[def]; Unit u; u.id = nextUnit++; u.owner = player; u.def = def; u.p = p; u.hp = d.hp; u.spawnT = t;
    if (IsPlayer(player) && player < (int)players.size()) {
        if (d.tags & TG_HERO) u.hp = B().factions[players[player].faction].heroHp;
        if ((d.tags & TG_SHIP) && (d.tags & TG_WORKER)) u.hp *= 1 + TechSum(player, "boat_hp");
        if (d.tags & TG_SHIP) u.hp *= 1 + TechSum(player, "ship_hp");
        if (HasTech(player, "elite") && !(d.tags & (TG_WORKER | TG_HERO | TG_ULTIMATE))) u.hp *= 1.2f;
        if (players[player].faction == 1 && d.key == "ironclad") u.hp *= 0.85f;   // (the Islanders' weakness)
        if (d.special == "dive" && HasCard(*this, player, 2)) u.hp *= 1.25f;   // (Pressure Hulls)
    }
    u.life = d.life; units.push_back(u); return u.id;
}
float MaxHp(const World& w, const Unit& u) {
    const UnitDef& d = w.UD(u); float hp = d.hp;
    if (IsPlayer(u.owner) && u.owner < (int)w.players.size()) {
        if (d.tags & TG_HERO) hp = B().factions[w.players[u.owner].faction].heroHp;
        if (d.tags & TG_SHIP) hp *= 1 + w.TechSum(u.owner, "ship_hp");
        if (w.HasTech(u.owner, "elite") && !(d.tags & (TG_WORKER | TG_HERO | TG_ULTIMATE))) hp *= 1.2f;
        if (w.players[u.owner].faction == 1 && d.key == "ironclad") hp *= 0.85f;
        if (d.special == "dive" && HasCard(w, u.owner, 2)) hp *= 1.25f;
    }
    hp *= 1 + 0.25f * u.molts;
    return hp;
}
bool World::CanPlace(int player, int def, int x, int y, std::string* why) const {
    const BuildingDef& d = B().buildings[def]; int s = d.size; bool coast = false;
    if (x < 0 || y < 0 || x + s > W || y + s > H) { if (why) *why = "off the map"; return false; }
    int isl = -1;
    for (int yy = y; yy < y + s; yy++) for (int xx = x; xx < x + s; xx++) {
        int k = Idx(xx, yy), t = tile[k];
        bool merfolk = IsPlayer(player) && player < (int)players.size() && players[player].faction == 3;
        if (!(Land(t) || (merfolk && (t == T_SHALLOW || t == T_KELP)))) { if (why) *why = "needs land"; return false; }
        if (occ[k] >= 0) { if (why) *why = "something is built there"; return false; }
        if (isl < 0) isl = isle[k];
    }
    for (int yy = y - 1; yy <= y + s; yy++) for (int xx = x - 1; xx <= x + s; xx++) if (In(xx, yy) && Water(tile[Idx(xx, yy)])) coast = true;
    if (d.coast && !coast) { if (why) *why = "must touch the sea"; return false; }
    for (const auto& n : nodes) if (n.amount > 0 && n.kind != N_FARM && n.p.x >= x && n.p.x < x + s && n.p.y >= y && n.p.y < y + s) { if (why) *why = "a resource is there"; return false; }
    if (d.faction >= 0 && (!IsPlayer(player) || player >= (int)players.size() || players[player].faction != d.faction)) { if (why) *why = "another faction's building"; return false; }
    if (d.key == "farm" && IsPlayer(player) && player < (int)players.size() && players[player].faction == 2) { if (why) *why = "the Brood uses Brood Pools"; return false; }
    if (IsPlayer(player)) {
        if (d.key == "lighthouse" || d.key == "totem") { if (isl >= 0) for (const auto& b : buildings) if (!b.dead && b.def == def && b.island == isl && (d.key == "lighthouse" || b.owner == player)) { if (why) *why = "one an island"; return false; } }
        if (!d.wall && d.key != "lighthouse") {
            // the rest only on your own ground (your territory), or on an island a Lighthouse of yours claims
            bool mine = false; for (int yy = y; yy < y + s && !mine; yy++) for (int xx = x; xx < x + s && !mine; xx++) if (owner[Idx(xx, yy)] == player || (owner[Idx(xx, yy)] >= 0 && Allied(player, owner[Idx(xx, yy)]) && false)) mine = true;
            if (!mine) { if (why) *why = "outside your territory"; return false; }
        }
        if (d.max > 0) { int have = 0; for (const auto& b : buildings) if (!b.dead && b.owner == player && b.def == def) have++; if (have >= d.max) { if (why) *why = "you have the most of these"; return false; } }
        if (d.era > players[player].era) { if (why) *why = "a later era"; return false; }
    }
    return true;
}
int World::PlaceBuilding(int player, int def, int x, int y, bool built) {
    const BuildingDef& d = B().buildings[def]; Building b; b.id = nextBuilding++; b.owner = player; b.def = def; b.x = x; b.y = y; b.size = d.size; b.progress = built ? 1 : 0;
    b.hp = built ? d.hp : d.hp * 0.1f; b.island = isle[Idx(x + d.size / 2, y + d.size / 2)];
    for (int yy = y; yy < y + d.size; yy++) for (int xx = x; xx < x + d.size; xx++) occ[Idx(xx, yy)] = (int16_t)b.id;
    if (d.key == "farm") { Node n; n.kind = N_FARM; n.p = b.Centre(); n.amount = n.cap = 1e9f; n.farm = b.id; nodes.push_back(n); }
    buildings.push_back(b);
    if (built) UpdateTerritory();
    return b.id;
}
void World::Init(const Settings& s) {
    set = s; set.players = std::clamp(set.players, 2, MAX_PLAYERS); rng = set.seed * 2654435761u + 13; for (int i = 0; i < 4; i++) Rand();
    t = 0; nextUnit = nextBuilding = 1; units.clear(); buildings.clear(); events.clear(); shots.clear(); pending.clear(); contracts.clear(); winner = -2; winners.clear(); over = false; evCount = 0;
    GenerateMap(*this);
    const Balance& Bl = B(); int n = set.players;
    players.assign(n, Player{});
    for (int i = 0; i < n; i++) {
        Player& p = players[i]; p.id = i; p.faction = std::clamp(set.faction[i], 0, (int)Bl.factions.size() - 1); p.team = set.teams == 0 ? i : set.team[i]; p.color = set.color[i]; p.ai = set.ai[i]; p.difficulty = set.aiLevelOf[i];
        p.name = set.names[i].empty() ? (p.ai ? Bl.ai[std::clamp(p.difficulty, 0, (int)Bl.ai.size() - 1)].name + " " + std::to_string(i + 1) : "Player " + std::to_string(i + 1)) : set.names[i];
        p.handicap = set.handicap[i]; p.res = Bl.start[std::clamp(set.start, 0, 2)]; p.tech.assign(Bl.techs.size(), 0); p.era = std::clamp(set.startEra, 0, 1);
        p.seen.assign(W * H, 0); p.explored.assign(W * H, set.reveal >= 1 ? 1 : 0);
        for (int j = 0; j < n; j++) p.stance[j] = (j == i || (set.teams != 0 && set.team[j] == p.team)) ? DP_ALLIED : DP_WAR;
    }
    // each home: the Harbor on its coast spot, the workers, a fishing boat and a scout skiff (doc "Starting position")
    int hi = 0, hb = Bl.Building("harbor");
    for (auto& is : islands) if (is.kind == I_HOME && hi < n) {
        int pl = is.home >= 0 ? is.home : hi; hi++;
        int sx = is.harborSpot % W, sy = is.harborSpot / W, sz = Bl.buildings[hb].size; int bx = -1, by = -1; float bd = 1e9f;
        for (int dy = -sz - 3; dy <= 3; dy++) for (int dx = -sz - 3; dx <= 3; dx++) { int x = sx + dx, y = sy + dy; std::string why; Player& pp = players[pl]; (void)pp;
            bool ok = true; for (int yy = y; yy < y + sz && ok; yy++) for (int xx = x; xx < x + sz && ok; xx++) if (!In(xx, yy) || !Land(tile[Idx(xx, yy)]) || occ[Idx(xx, yy)] >= 0) ok = false;
            for (const auto& nd : nodes) if (ok && nd.p.x >= x - 1 && nd.p.x < x + sz + 1 && nd.p.y >= y - 1 && nd.p.y < y + sz + 1) ok = false;   // (never over a resource)
            if (!ok) continue; bool coast = false; for (int yy = y - 1; yy <= y + sz; yy++) for (int xx = x - 1; xx <= x + sz; xx++) if (In(xx, yy) && Water(tile[Idx(xx, yy)])) coast = true; if (!coast) continue;
            float d = Vector2Distance({x + sz * 0.5f, y + sz * 0.5f}, {sx + 0.5f, sy + 0.5f}); if (d < bd) { bd = d; bx = x; by = y; } }
        if (bx < 0) { bx = sx - sz / 2; by = sy - sz / 2; }
        int id = PlaceBuilding(pl, hb, bx, by, true); is.owner = pl;
        Building* b = Bd(id); Vector2 c = b->Centre(); Vector2 toIn = Vector2Normalize(Vector2Subtract(is.c, c));
        for (int k = 0; k < Bl.startWorkers; k++) SpawnUnit(pl, Bl.Unit("worker"), Vector2Add(c, Vector2Add(Vector2Scale(toIn, 3.2f), {(k % 2) * 1.0f - 0.5f, (k / 2) * 1.0f - 0.5f})));
        Vector2 sea = c; for (int r = 2; r < 10; r++) { Vector2 q = Vector2Subtract(c, Vector2Scale(toIn, (float)r)); int tt = TileAt(q); if (Water(tt) && tt != T_SHALLOW) { sea = q; break; } if (Water(tt)) sea = q; }
        for (int k = 0; k < Bl.startBoats; k++) SpawnUnit(pl, Bl.Unit("fishing_boat"), Vector2Add(sea, {(float)k, 0.5f}));
        for (int k = 0; k < Bl.startSkiffs; k++) SpawnUnit(pl, Bl.Unit("scout_skiff"), Vector2Add(sea, {-1.0f, -0.5f}));
    }
    UpdateTerritory();
    if (set.startEra >= 1) for (auto& p : players) { p.era = 1; p.eraAt[1] = 0; }
    for (auto& p : players) FactionStart(p);
    InitSites();
    UpdateFog();
}

// ---------------------------------------------------------------- territory, fog, score
void World::UpdateTerritory() {
    std::fill(owner.begin(), owner.end(), (int16_t)-1);
    for (auto& is : islands) if (is.kind != I_HOME) is.owner = -1;
    for (const auto& b : buildings) {
        if (b.dead || b.progress < 1 || !IsPlayer(b.owner)) continue; const BuildingDef& d = BD(b);
        float r = d.claim > 0 ? d.claim + TechSum(b.owner, "claim") : (d.key == "harbor" || d.key == "colony_hall") ? 12.0f : d.wall ? 0 : 4.0f;   // (any other building holds the ground round it)
        if (r <= 0) continue; Vector2 c = b.Centre();
        for (int y = std::max(0, (int)(c.y - r)); y <= std::min(H - 1, (int)(c.y + r)); y++) for (int x = std::max(0, (int)(c.x - r)); x <= std::min(W - 1, (int)(c.x + r)); x++)
            if (Vector2Distance({x + 0.5f, y + 0.5f}, c) <= r && (owner[Idx(x, y)] < 0 || (d.key != "lighthouse" && r >= 12))) owner[Idx(x, y)] = (int16_t)b.owner;
        if (b.island >= 0 && b.island < (int)islands.size()) islands[b.island].owner = b.owner;
    }
    // allied tribal towns count as the Islanders' territory
    for (const auto& s : sites) if (s.kind == S_TRIBE) for (int pl = 0; pl < (int)players.size(); pl++) if (s.peace[pl] == 2) {
        for (int y = std::max(0, (int)(s.p.y - 10)); y <= std::min(H - 1, (int)(s.p.y + 10)); y++) for (int x = std::max(0, (int)(s.p.x - 10)); x <= std::min(W - 1, (int)(s.p.x + 10)); x++) if (owner[Idx(x, y)] < 0) owner[Idx(x, y)] = (int16_t)pl;
    }
}
void World::UpdateFog() {
    for (auto& p : players) {
        if (set.reveal == 2) { std::fill(p.seen.begin(), p.seen.end(), 1); continue; }
        std::fill(p.seen.begin(), p.seen.end(), 0);
        auto mark = [&](Vector2 c, float r) {
            for (int y = std::max(0, (int)(c.y - r)); y <= std::min(H - 1, (int)(c.y + r)); y++) for (int x = std::max(0, (int)(c.x - r)); x <= std::min(W - 1, (int)(c.x + r)); x++)
                if ((x + 0.5f - c.x) * (x + 0.5f - c.x) + (y + 0.5f - c.y) * (y + 0.5f - c.y) <= r * r) { p.seen[Idx(x, y)] = 1; p.explored[Idx(x, y)] = 1; }
        };
        float fogK = 1;
        for (const auto& u : units) if (!u.dead && u.inside < 0 && u.garrisoned < 0 && Allied(p.id, u.owner)) {
            float r = UD(u).vision; if (weather == 1 && Vector2Distance(u.p, weatherAt) < weatherR) r *= 0.5f; mark(u.p, r * fogK); }
        for (const auto& b : buildings) if (!b.dead && Allied(p.id, b.owner)) { const BuildingDef& d = BD(b); float r = d.vision + (d.key == "lighthouse" ? TechSum(b.owner, "lighthouse_vision") : 0); mark(b.Centre(), b.progress >= 1 ? r : 3); }
    }
}
void World::Score() {
    const Balance& Bl = B();
    for (auto& p : players) {
        float s = 0; int isl = 0; for (const auto& is : islands) if (is.owner == p.id) isl++;
        s += isl * Bl.scIsland;
        int relics = 0; for (const auto& b : buildings) if (!b.dead && b.owner == p.id) relics += b.relics;
        s += relics * Bl.scRelic;
        float g = 0; for (int r = 0; r < R_COUNT; r++) g += p.gathered[r]; s += g / Bl.scGather;
        s += Bl.scEra[std::clamp(p.era, 0, 2)];
        int techs = 0; for (char c : p.tech) techs += c; s += techs * Bl.scTech;
        for (const auto& b : buildings) if (!b.dead && b.owner == p.id && BD(b).key == "wonder" && b.progress >= 1) s += Bl.scWonder;
        s += p.killScore + p.razeScore + p.bossScore;
        p.score = s;
    }
}

// ---------------------------------------------------------------- paths (A* on the tile grid, 8 ways, no cut corners)
bool World::FindPath(const Unit& u, Vector2 to, std::vector<int>& out, float near) const {
    out.clear(); const UnitDef& d = UD(u); Move m = d.move; int fac = IsPlayer(u.owner) && u.owner < (int)players.size() ? players[u.owner].faction : -1;
    if (fac == 3 && m == MV_LAND && !(d.tags & TG_SIEGE)) m = MV_AMPHIB;   // (Merfolk swim)
    int sx = (int)u.p.x, sy = (int)u.p.y, gx = std::clamp((int)to.x, 0, W - 1), gy = std::clamp((int)to.y, 0, H - 1);
    if (!In(sx, sy)) return false;
    if (m == MV_AIR) { out.push_back(Idx(gx, gy)); return true; }
    // the goal may be blocked (a building, a node on land for a boat): accept any tile within `near` of it
    auto goalOk = [&](int x, int y) { return fabsf(x + 0.5f - to.x) <= near + 0.5f && fabsf(y + 0.5f - to.y) <= near + 0.5f; };
    // flat scratch arrays stamped per search (no hashing: the search is the hottest code in the step)
    if ((int)asG.size() != W * H) { asG.assign(W * H, 0); asFrom.assign(W * H, -1); asStamp.assign(W * H, 0); asGen = 0; }
    if (++asGen == 0) { std::fill(asStamp.begin(), asStamp.end(), 0u); asGen = 1; }
    auto H8 = [&](int x, int y) { float dx = fabsf(x + 0.5f - to.x), dy = fabsf(y + 0.5f - to.y); return (dx + dy + (1.414f - 2) * std::min(dx, dy)) * 1.15f; };
    using QE = std::pair<float, int>; std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
    int s = Idx(sx, sy); asStamp[s] = asGen; asG[s] = 0; asFrom[s] = -1; open.push({0, s}); int found = -1, iters = 0, best = s; float bestH = 1e9f;
    while (!open.empty() && iters++ < 6000) {
        float fq = open.top().first; int c = open.top().second; open.pop(); int cx = c % W, cy = c / W;
        float gc = asG[c]; if (fq > gc + H8(cx, cy) + 1e-3f) continue;   // (a stale entry)
        float hC = H8(cx, cy); if (hC < bestH) { bestH = hC; best = c; }
        if (goalOk(cx, cy)) { found = c; break; }
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dy) continue; int nx = cx + dx, ny = cy + dy;
            if (!Passable(nx, ny, m, fac) && !goalOk(nx, ny)) continue;
            if (dx && dy && (!Passable(cx + dx, cy, m, fac) || !Passable(cx, cy + dy, m, fac))) continue;
            int nk = Idx(nx, ny); float step = (dx && dy) ? 1.414f : 1.0f;
            int tt = tile[nk]; if (m == MV_LAND && tt == T_SHALLOW && fac != 2 && fac != 3) step *= 2;   // (wading is slow)
            float ng = gc + step; if (asStamp[nk] == asGen && asG[nk] <= ng) continue;
            asStamp[nk] = asGen; asG[nk] = ng; asFrom[nk] = c; open.push({ng + H8(nx, ny), nk});
        }
    }
    bool reached = found >= 0;
    if (found < 0) found = best;   // (as close as it can get)
    for (int c = found; c != s && c >= 0 && (int)out.size() < W * H; ) { out.push_back(c); c = asStamp[c] == asGen ? asFrom[c] : -1; }
    std::reverse(out.begin(), out.end());
    return reached || found == s;
}

// ---------------------------------------------------------------- commands
static int NearestFree(const World& w, Vector2 near, Move m, int fac) {   // the nearest passable tile
    int bx = (int)near.x, by = (int)near.y;
    for (int r = 0; r < 12; r++) for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) { if (std::max(abs(dx), abs(dy)) != r) continue; if (w.Passable(bx + dx, by + dy, m, fac)) return w.Idx(bx + dx, by + dy); }
    return -1;
}
static void SetOrder(World& w, Unit& u, int o, int target = -1, Vector2 goal = {}, int kind = 0) { u.order = (fa::Order)o; u.target = target; u.goal = goal; u.targetKind = kind; u.path.clear(); u.pathAt = 0; u.repathT = 0; u.pathFailed = false; (void)w; }
bool World::Apply(const Command& c) {
    if (!IsPlayer(c.player) || c.player >= (int)players.size()) return false;
    Player& P = players[c.player]; const Balance& Bl = B(); cmd = c;
    if (!P.alive && c.kind != C_DIPLO) return false;
    if (Paused() && c.kind != C_PAUSE && c.kind != C_DIPLO) return false;   // (nothing moves while the game is paused)
    auto mine = [&](int id) -> Unit* { Unit* u = U(id); return u && u->owner == c.player && u->inside < 0 ? u : nullptr; };
    switch (c.kind) {
        case C_MOVE: case C_ATTACK_MOVE: case C_PATROL: {
            int n = 0; for (int id : c.units) if (Unit* u = mine(id)) {
                if (u->garrisoned >= 0) continue;
                // a loose formation round the click (a square of spacing 1)
                int k = n++; int side = (int)ceilf(sqrtf((float)c.units.size())); Vector2 off{(k % side - side * 0.5f + 0.5f) * 1.1f, (k / side - side * 0.5f + 0.5f) * 1.1f};
                SetOrder(*this, *u, c.kind == C_MOVE ? O_MOVE : c.kind == C_PATROL ? O_PATROL : O_ATTACK_MOVE, -1, Vector2Add(c.at, off));
                if (c.kind == C_PATROL) { u->patrolA = u->p; u->patrolB = Vector2Add(c.at, off); }
            }
            return n > 0; }
        case C_ATTACK: {
            int n = 0; for (int id : c.units) if (Unit* u = mine(id)) { SetOrder(*this, *u, O_ATTACK, c.target, c.at, c.a); n++; } return n > 0; }
        case C_GATHER: {
            if (c.target < 0 || c.target >= (int)nodes.size()) return false; int n = 0;
            for (int id : c.units) if (Unit* u = mine(id)) { if (!(UD(*u).tags & TG_WORKER)) continue; SetOrder(*this, *u, O_GATHER, c.target, nodes[c.target].p, 2); u->lastNode = c.target; n++; } return n > 0; }
        case C_BUILD: {
            if (c.def < 0 || c.def >= (int)Bl.buildings.size()) return false; const BuildingDef& d = Bl.buildings[c.def];
            if (d.key == "harbor" || (d.key == "wonder" && P.wonder >= 0)) return false;
            std::string why; if (!CanPlace(c.player, c.def, c.x, c.y, &why)) return false;
            Cost cost = d.cost; if (P.faction == 0 && d.key == "lighthouse" && std::find(P.cards.begin(), P.cards.end(), 6) != P.cards.end()) for (auto& x : cost) x *= 0.7f;
            if (!Afford(P, cost)) return false;
            Pay(P, cost); int id = PlaceBuilding(c.player, c.def, c.x, c.y, false);
            if (d.key == "wonder") P.wonder = id;
            for (int uid : c.units) if (Unit* u = mine(uid)) if (UD(*u).tags & TG_WORKER && !(UD(*u).tags & TG_SHIP)) SetOrder(*this, *u, O_BUILD, id, Bd(id)->Centre(), 1);
            return true; }
        case C_REPAIR: { Building* b = Bd(c.target); if (!b || b->owner != c.player) return false; int n = 0; for (int id : c.units) if (Unit* u = mine(id)) if ((UD(*u).tags & TG_WORKER) && !(UD(*u).tags & TG_SHIP)) { SetOrder(*this, *u, O_BUILD, b->id, b->Centre(), 1); n++; } return n > 0; }
        case C_TRAIN: {
            Building* b = Bd(c.target); if (!b || b->owner != c.player || b->progress < 1 || c.def < 0 || c.def >= (int)Bl.units.size()) return false;
            const UnitDef& d = Bl.units[c.def]; const BuildingDef& bd = BD(*b);
            bool can = std::find(bd.trains.begin(), bd.trains.end(), d.key) != bd.trains.end() || (d.faction == P.faction && d.from == bd.key);
            if (d.key == "hero" && bd.key != "harbor") can = false;
            if (!can || d.era > P.era) return false;
            if (d.faction >= 0 && d.faction != P.faction) return false;
            if (P.faction == 2 && (d.key == "torpedo_boat" || d.key == "ironclad")) return false;   // (the Crustaceans have none)
            if (P.faction == 5 && d.key == "medic") return false;
            if (d.key == "hero" && (P.heroUnit >= 0 && U(P.heroUnit))) return false;
            if (d.key == "hero") for (const auto& ob : buildings) if (!ob.dead && ob.owner == c.player) for (const auto& q : ob.queue) if (q.kind == 0 && q.def == c.def) return false;   // (only one, queued or alive)
            if (d.key == "hero" && P.heroDeadT >= 0 && t - P.heroDeadT < 60) return false;
            if ((int)b->queue.size() >= 8) return false;
            Cost cost = PriceOf(c.player, d); if (d.key == "hero" && P.heroDeadT >= 0) for (auto& x : cost) x *= 0.5f;
            if (!Afford(P, cost)) return false;
            Pay(P, cost); b->queue.push_back({0, c.def}); return true; }
        case C_RESEARCH: {
            Building* b = Bd(c.target); if (!b || b->owner != c.player || b->progress < 1 || c.def < 0 || c.def >= (int)Bl.techs.size()) return false;
            const TechDef& td = Bl.techs[c.def]; if (P.tech[c.def] || td.era > P.era || (td.faction >= 0 && td.faction != P.faction)) return false;
            if (td.at != BD(*b).key && !(td.faction >= 0 && BD(*b).key == "harbor")) return false;
            if (td.forgeLine >= 0 && P.forge[td.forgeLine] != td.forgeLevel - 1) return false;
            for (const auto& q : b->queue) if (q.kind == 1 && q.def == c.def) return false;
            for (const auto& ob : buildings) if (!ob.dead && ob.owner == c.player) for (const auto& q : ob.queue) if (q.kind == 1 && q.def == c.def) return false;
            if (!Afford(P, td.cost)) return false;
            Pay(P, td.cost); b->queue.push_back({1, c.def}); return true; }
        case C_ERA: {
            Building* b = Bd(c.target); if (!b || b->owner != c.player || BD(*b).key != "harbor" || b->progress < 1 || P.era >= 2 || P.eraBuilding >= 0) return false;
            int e = P.era + 1; int have = 0;
            for (const auto& need : Bl.eraNeeds[e]) {
                if (need == "lighthouse2") { int isl = 0; for (const auto& ob : buildings) if (!ob.dead && ob.owner == c.player && BD(ob).key == "lighthouse" && ob.progress >= 1) isl++; if (isl >= 1) have++; continue; }
                for (const auto& ob : buildings) if (!ob.dead && ob.owner == c.player && BD(ob).key == need && ob.progress >= 1) { have++; break; }
            }
            if (have < 2 || !Afford(P, Bl.eraCost[e])) return false;
            Pay(P, Bl.eraCost[e]); b->queue.insert(b->queue.begin(), {2, e}); P.eraBuilding = b->id; return true; }
        case C_CANCEL: {
            Building* b = Bd(c.target); if (!b || b->owner != c.player) return false;
            if (b->progress < 1 && c.a < 0) { Refund(P, BD(*b).cost, 0.8f); Raze(*b, -1); return true; }
            if (b->queue.empty()) return false; int i = std::clamp(c.a, 0, (int)b->queue.size() - 1); QueueItem q = b->queue[i];
            if (q.kind == 0) Refund(P, PriceOf(c.player, Bl.units[q.def])); else if (q.kind == 1) Refund(P, Bl.techs[q.def].cost); else { Refund(P, Bl.eraCost[q.def]); P.eraBuilding = -1; }
            b->queue.erase(b->queue.begin() + i); if (i == 0) b->qT = 0; return true; }
        case C_GARRISON: {
            Building* b = Bd(c.target); if (!b || !Allied(b->owner, c.player) || BD(*b).garrison <= 0) return false; int n = 0;
            for (int id : c.units) if (Unit* u = mine(id)) if (UD(*u).tags & (TG_INFANTRY | TG_WORKER) && !(UD(*u).tags & TG_SHIP)) { SetOrder(*this, *u, O_GARRISON, b->id, b->Centre(), 1); n++; } return n > 0; }
        case C_UNGARRISON: {
            Building* b = Bd(c.target); if (!b || b->owner != c.player) return false; Vector2 ce = b->Centre();
            for (int id : b->garrison) if (Unit* u = U(id)) { u->garrisoned = -1; int k = NearestFree(*this, Vector2Add(ce, {b->size * 0.5f + 1, 0}), MV_LAND, P.faction); if (k >= 0) u->p = {k % W + 0.5f, k / W + 0.5f}; SetOrder(*this, *u, O_IDLE); }
            b->garrison.clear(); return true; }
        case C_BOARD: {
            Unit* tr = U(c.target); if (!tr || tr->owner != c.player) return false; const UnitDef& td = UD(*tr); if (td.carry <= 0) return false; int n = 0;
            for (int id : c.units) if (Unit* u = mine(id)) if (u->id != tr->id && !(UD(*u).tags & (TG_SHIP | TG_AIR))) { SetOrder(*this, *u, O_BOARD, tr->id, tr->p, 0); n++; } return n > 0; }
        case C_UNLOAD: {
            int n = 0; for (int id : c.units) if (Unit* u = mine(id)) if (UD(*u).carry > 0 || UD(*u).carryWorkers > 0) { SetOrder(*this, *u, O_UNLOAD, -1, c.at); n++; } return n > 0; }
        case C_STOP: { for (int id : c.units) if (Unit* u = mine(id)) SetOrder(*this, *u, O_IDLE); return true; }
        case C_STANCE: { for (int id : c.units) if (Unit* u = mine(id)) u->stance = (Stance)std::clamp(c.a, 0, 3); return true; }
        case C_RALLY: { Building* b = Bd(c.target); if (!b || b->owner != c.player) return false; b->rally = c.at; b->hasRally = true; return true; }
        case C_EXCHANGE: {
            // a = what you give (0 Food, 1 Brass, 2 Coal), b = what you get (0..2, or 4 Doubloons); 100 at a time
            bool have = false; for (const auto& ob : buildings) if (!ob.dead && ob.owner == c.player && BD(ob).key == "exchange" && ob.progress >= 1) have = true;
            if (!have || c.a < 0 || c.a > 2 || c.a == c.b || !(c.b >= 0 && (c.b <= 2 || c.b == 4))) return false;
            if (P.res[c.a] < Bl.exPer) return false;
            float step = Bl.exStep * (1 + TechSum(c.player, "exchange_penalty"));
            if (c.b == 4) { P.res[c.a] -= Bl.exPer; P.res[R_DOUB] += Bl.exSell; return true; }
            float got = Bl.exBaseOut * P.exchange[c.b] / std::max(0.3f, P.exchange[c.a]) * 1.0f;
            got = Bl.exBaseOut / P.exchange[c.b] * P.exchange[c.a];
            P.res[c.a] -= Bl.exPer; P.res[c.b] += got; P.exchange[c.a] *= 1 - step; P.exchange[c.b] *= 1 + step; return true; }
        case C_DIPLO: {
            // a = the other player, b = the stance offered; better stances need the other's acceptance (the AI decides at once)
            int o = c.a; if (o < 0 || o >= (int)players.size() || o == c.player || set.diplomacy == 0) return false;
            Diplo want = (Diplo)std::clamp(c.b, 0, 4), now = P.stance[o];
            if (want < now) {   // a downgrade: one-sided, announced; breaking Allied or Peace into War costs morale and trade
                P.stance[o] = want; players[o].stance[c.player] = want;
                if (want == DP_WAR && (now == DP_ALLIED || now == DP_PEACE)) { for (auto& u : units) if (u.owner == c.player) u.morale = std::max(0.0f, u.morale - Bl.betrayMorale); P.tradePauseT = Bl.betrayPause; }
                Emit(EV_DIPLO, {0, 0}, c.player, o, want); return true;
            }
            if (players[o].ai) { bool yes = players[o].score < P.score * 1.4f || want <= DP_CEASEFIRE; if (!yes) return false; }
            else if (P.offerFrom[o] < (int)want) {   // a human must accept: the offer waits on their diplomacy page
                players[o].offerFrom[c.player] = want; Emit(EV_OFFER, {0, 0}, c.player, o, want); return true; }
            P.offerFrom[o] = -1; players[o].offerFrom[c.player] = -1;
            P.stance[o] = players[o].stance[c.player] = want; if (want == DP_ALLIED) P.allyT[o] = players[o].allyT[c.player] = t; if (want == DP_CEASEFIRE) P.allyT[o] = players[o].allyT[c.player] = t;
            Emit(EV_DIPLO, {0, 0}, c.player, o, want); return true; }
        case C_TRIBUTE: {
            int o = c.a, r = c.b; float amt = c.amount; if (o < 0 || o >= (int)players.size() || o == c.player || r < 0 || r >= R_COUNT || amt <= 0 || P.res[r] < amt) return false;
            if (P.stance[o] < DP_PEACE) return false; float fee = (set.teams != 0 && Allied(c.player, o)) ? 0 : Bl.tributeFee;
            P.res[r] -= amt; players[o].res[r] += amt * (1 - fee); return true; }
        case C_TRADE: {   // a Trade Ship's route: to another player's Exchange (target building) or a pirate cove (a = site + 1000)
            int n = 0; for (int id : c.units) if (Unit* u = mine(id)) if (UD(*u).key == "trade_ship") { SetOrder(*this, *u, O_TRADE, c.target, c.at, c.a >= 1000 ? 3 : 1); u->home = -1; n++; } return n > 0; }
        case C_DELETE: { for (int id : c.units) if (Unit* u = mine(id)) Kill(*u, -1); if (c.target >= 0) if (Building* b = Bd(c.target)) if (b->owner == c.player) Raze(*b, -1); return true; }
        case C_PAUSE: {   // a pause of up to 60 s (3 a player); the one who paused, or anyone once 10 s have passed, resumes it
            if (Paused()) { if (pausedBy == c.player || pauseLeft < 50) { pausedBy = -1; Emit(EV_DIPLO, {0, 0}, c.player, -1, 20); return true; } return false; }
            if (pausesLeft[c.player] <= 0) return false; pausesLeft[c.player]--; pausedBy = c.player; pauseLeft = 60; Emit(EV_DIPLO, {0, 0}, c.player, -1, 21); return true; }
        case C_PING: { Emit(EV_PING, c.at, c.player, c.a); return true; }   // (attack here, defend here, danger: drawn for the sender's allies)
        case C_RESIGN: { P.alive = false; P.eliminated = true; for (auto& u : units) if (u.owner == c.player) u.dead = true; for (auto& b : buildings) if (b.owner == c.player) Raze(b, -1); Emit(EV_ELIM, {0, 0}, c.player); return true; }
        case C_ULTIMATE: case C_ABILITY: case C_CARD: case C_CONVERT: case C_OFFER: return FactionAbility(c.player, c.units.empty() ? -1 : c.units[0], c.kind * 100 + c.a);
        case C_HIRE: case C_BID: case C_TRIBE: {   // the neutral powers (fathoms_neutral.cpp reads these through FactionAbility's sibling)
            extern bool NeutralCommand(World& w, const Command& c); return NeutralCommand(*this, c); }
        default: return false;
    }
}

// ---------------------------------------------------------------- damage
void World::Damage(int attacker, int own, int target, int kind, float dmg, DmgType type, uint32_t bonusTags) {
    (void)bonusTags;
    if (kind == 0) {
        Unit* v = U(target); if (!v || v->garrisoned >= 0 || v->inside >= 0) return;
        if (type == D_HEAL) { v->hp = std::min(MaxHp(*this, *v), v->hp + dmg); return; }
        const UnitDef& vd = UD(*v);
        float mult = 1;
        if (t - v->landedT < B().landingWindow && !(IsPlayer(v->owner) && v->owner < (int)players.size() && HasTech(v->owner, "amphibious_drills"))) mult *= B().landingMult;
        if (TileAt(v->p) == T_SHALLOW && !(vd.tags & TG_SHIP) && vd.move != MV_AMPHIB && !(IsPlayer(v->owner) && v->owner < (int)players.size() && (players[v->owner].faction == 2 || players[v->owner].faction == 3))) mult *= B().wadingMult;
        if (v->cocoonT > 0) mult *= 1.5f;
        float d = dmg * mult;
        v->hp -= d; v->dmgDealt += 0;
        if (IsPlayer(v->owner)) { v->morale = std::max(0.0f, v->morale - d * 0.02f); }
        if (!IsPlayer(v->owner)) NeutralDamaged(*this, *v, own, d);
        if (Unit* a = U(attacker)) { a->dmgDealt += d; FactionOnHit(*a, *v, d); }
        Emit(EV_HIT, v->p, v->id, attacker, (int)d);
        if (v->hp <= 0) Kill(*v, own);
    } else if (kind == 1) {
        Building* b = Bd(target); if (!b) return;
        b->hp -= dmg; b->attackedT = t; Emit(EV_HIT, b->Centre(), -b->id, attacker, (int)dmg);
        if (b->hp <= 0) Raze(*b, own);
    } else if (kind == 3) {
        if (target < 0 || target >= (int)sites.size()) return; Site& s = sites[target]; s.hp -= dmg; s.lastAttacker = own;
        if (IsPlayer(own) && s.kind == S_COVE) s.rep[own] = std::max(-100.0f, s.rep[own] - 100);   // (attacking a cove: reputation -100, once it's war)
        Emit(EV_HIT, s.p, -1000 - target, attacker, (int)dmg);
    }
}
void World::Kill(Unit& u, int by) {
    if (u.dead) return;
    u.dead = true; const UnitDef& d = UD(u);
    if (!IsPlayer(u.owner) || u.home >= 0) NeutralDied(*this, u, by);
    for (int id : u.cargo) if (Unit* c = U(id)) { c->dead = true; }   // (a sunk transport takes its cargo down)
    if (u.relic) { Site s; s.kind = S_RUIN; s.p = u.p; s.relic = u.relic; s.state = 2; s.island = -1; sites.push_back(s); }   // (a dropped relic)
    if (IsPlayer(u.owner) && u.owner < (int)players.size()) { players[u.owner].losses++; if (players[u.owner].heroUnit == u.id) { players[u.owner].heroUnit = -1; players[u.owner].heroDeadT = t; } }
    if (IsPlayer(by) && by < (int)players.size() && by != u.owner) {
        float v = 0; for (int r = 0; r < R_COUNT; r++) v += d.cost[r] * B().weights[r];
        players[by].killScore += v / B().scKill; players[by].kills++;
    }
    // the Clockwork's salvage: any unit dying near a Clockwork unit or building pays 12% of its cost in Brass
    for (auto& p : players) if (p.faction == 5 && p.alive) {
        bool near = false; for (const auto& o : units) if (!o.dead && o.owner == p.id && Vector2Distance(o.p, u.p) < 8) { near = true; break; }
        if (!near) for (const auto& b : buildings) if (!b.dead && b.owner == p.id && Vector2Distance(b.Centre(), u.p) < 8) { near = true; break; }
        if (near) { float c = 0; for (int r = 0; r < R_COUNT; r++) c += d.cost[r]; p.res[R_BRASS] += c * 0.12f; }
    }
    Emit(EV_DIE, u.p, u.id, by, u.def);
}
void World::Raze(Building& b, int by) {
    if (b.dead) return; b.dead = true; const BuildingDef& d = BD(b);
    for (int yy = b.y; yy < b.y + b.size; yy++) for (int xx = b.x; xx < b.x + b.size; xx++) if (In(xx, yy) && occ[Idx(xx, yy)] == b.id) occ[Idx(xx, yy)] = -1;
    for (int id : b.garrison) if (Unit* u = U(id)) { u->garrisoned = -1; u->p = Vector2Add(b.Centre(), {(Rand() - 0.5f) * b.size, b.size * 0.6f}); }
    if (b.relics > 0) { Site s; s.kind = S_RUIN; s.p = b.Centre(); s.relic = b.relics; s.state = 2; sites.push_back(s); }
    for (auto& n : nodes) if (n.farm == b.id) n.amount = 0;
    if (IsPlayer(b.owner) && b.owner < (int)players.size()) { if (players[b.owner].eraBuilding == b.id) players[b.owner].eraBuilding = -1; if (players[b.owner].wonder == b.id) players[b.owner].wonder = -1; }
    if (IsPlayer(by) && by < (int)players.size() && by != b.owner) { float v = 0; for (int r = 0; r < R_COUNT; r++) v += d.cost[r] * B().weights[r]; players[by].razeScore += v / B().scRaze; }
    Emit(EV_RAZED, b.Centre(), b.id, by, b.def);
    UpdateTerritory();
}

// ---------------------------------------------------------------- units
static float Terrain(const World& w, Vector2 p) { return w.height[w.Idx(std::clamp((int)p.x, 0, w.W - 1), std::clamp((int)p.y, 0, w.H - 1))]; }
void World::StepMove(Unit& u, float speedMult) {
    const UnitDef& d = UD(u);
    if (u.path.empty() || u.pathAt >= (int)u.path.size()) { u.vel = {}; return; }
    int k = u.path[u.pathAt]; Vector2 wp{k % W + 0.5f, k / W + 0.5f};
    // look ahead: skip a waypoint when the next is in a straight, passable line
    if (u.pathAt + 1 < (int)u.path.size()) { int k2 = u.path[u.pathAt + 1]; Vector2 w2{k2 % W + 0.5f, k2 / W + 0.5f}; if (Vector2Distance(u.p, wp) < 0.6f) { u.pathAt++; wp = w2; } }
    else if (u.order == O_GATHER || u.order == O_RETURN || u.order == O_BUILD || u.order == O_ATTACK || u.order == O_GARRISON || u.order == O_BOARD) {}
    Vector2 to = Vector2Subtract(wp, u.p); float L = Vector2Length(to);
    float sp = d.speed * speedMult;
    int fac = IsPlayer(u.owner) && u.owner < (int)players.size() ? players[u.owner].faction : -1;
    int tt = TileAt(u.p);
    if (d.move == MV_LAND && Water(tt) && fac != 2 && fac != 3) sp *= B().shallowLand;
    if (fac == 3 && Water(tt) && !(d.tags & TG_SHIP)) sp = (d.waterSpeed > 0 ? d.waterSpeed : 1.2f) * speedMult;
    if (fac == 3 && !Water(tt) && !(d.tags & TG_SHIP) && !(d.tags & TG_WORKER)) sp *= 0.8f;
    if (d.move == MV_SEA && tt == T_SHALLOW) sp *= B().shallowShip;
    if (d.move == MV_SEA && tt == T_CURRENT) sp *= B().currentShip;
    if (d.move == MV_SEA) sp *= 1 + TechSum(u.owner, "ship_speed");
    if (d.tags & TG_WORKER && !(d.tags & TG_SHIP)) sp *= 1 + TechSum(u.owner, "worker_speed");
    if (fac >= 0 && d.move == MV_LAND && !(d.tags & TG_WORKER)) sp *= B().factions[fac].landSpeedMult;
    if (d.move == MV_SEA && weather == 2 && Vector2Distance(u.p, weatherAt) < weatherR) sp *= B().stormSpeed;
    // kelp: Merfolk faster, enemy ships slower
    int ki = Idx(std::clamp((int)u.p.x, 0, W - 1), std::clamp((int)u.p.y, 0, H - 1));
    if (kelpOwner[ki]) { int ko = kelpOwner[ki] - 1; if (ko == u.owner && fac == 3) sp *= 1.2f; else if (d.move == MV_SEA && Enemies(ko, u.owner)) sp *= 0.8f; }
    if (L < 1e-3f) { u.pathAt++; return; }
    Vector2 v = Vector2Scale(to, std::min(sp, L / STEP) / L);
    u.vel = v; u.p = Vector2Add(u.p, Vector2Scale(v, STEP)); u.facing = atan2f(v.y, v.x);
    if (L < 0.35f) u.pathAt++;
}
// where a unit can stand next to a target (a node, a building's edge, another unit)
static Vector2 Approach(const World& w, const Unit& u, Vector2 c, float reach) {
    Vector2 d = Vector2Subtract(u.p, c); float L = Vector2Length(d); if (L < 1e-3f) return c;
    return Vector2Add(c, Vector2Scale(d, std::min(L, reach) / L)); (void)w;
}
static void Seek(World& w, Unit& u, Vector2 at, float near = 0.6f) {
    u.repathT -= STEP;
    bool moved = Vector2Distance(u.goal, at) > 1.5f;
    if (moved || ((u.path.empty() || u.pathAt >= (int)u.path.size()) && u.repathT <= 0)) {
        u.goal = at; bool ok = w.FindPath(u, at, u.path, near); u.pathAt = 0;
        u.repathT = ok ? 0.5f + (u.id % 5) * 0.1f : 6.0f + (u.id % 7) * 0.3f;   // (no way there: don't search again for a while)
        u.pathFailed = !ok;
    }
    // the last step: the path stops on the tile next to the goal; walk on to the goal's own tile when it can be stood on
    if (u.pathAt >= (int)u.path.size() && Vector2Distance(u.p, at) < 3) {
        int x = (int)at.x, y = (int)at.y; const UnitDef& d = w.UD(u); int f = IsPlayer(u.owner) && u.owner < (int)w.players.size() ? w.players[u.owner].faction : -1;
        Move m = d.move; if (f == 3 && m == MV_LAND) m = MV_AMPHIB;
        if (w.Passable(x, y, m, f) && (int)u.p.x + (int)u.p.y * w.W != w.Idx(x, y)) { u.path.assign(1, w.Idx(x, y)); u.pathAt = 0; }
    }
}
int World::NearestDrop(int player, Vector2 p, int res) const {
    int best = -1; float bd = 1e9f;
    for (const auto& b : buildings) {
        if (b.dead || b.progress < 1 || !Allied(player, b.owner) || b.owner != player) continue; const std::string& dr = BD(b).drop;
        bool ok = dr == "all" || (dr == "food" && res == R_FOOD) || (dr == "mine" && (res == R_BRASS || res == R_COAL || res == R_ICHOR)) || (dr == "foodbrass" && (res == R_FOOD || res == R_BRASS || res == R_DOUB)) || (dr == "coal" && res == R_COAL);
        if (!ok) continue; float d = Vector2Distance(p, b.Centre()); if (d < bd) { bd = d; best = b.id; }
    }
    return best;
}
void World::StepGather(Unit& u) {
    const UnitDef& d = UD(u); bool boat = d.tags & TG_SHIP; Player& P = players[u.owner];
    if (u.order == O_GATHER) {
        if (u.target < 0 || u.target >= (int)nodes.size() || nodes[u.target].amount <= 0) {
            // find the nearest node of the same kind (or any the unit can work) near the old one
            int kind = u.target >= 0 && u.target < (int)nodes.size() ? nodes[u.target].kind : -1; int best = -1; float bd = 18;
            for (size_t i = 0; i < nodes.size(); i++) { const Node& n = nodes[i]; if (n.amount <= 0 || (kind >= 0 && n.kind != kind) || B().nodes[n.kind].water != boat) continue; if (n.farm >= 0 && n.kind == N_FARM) { bool used = false; for (const auto& o : units) if (&o != &u && !o.dead && o.order == O_GATHER && o.target == (int)i) used = true; if (used) continue; } float dd = Vector2Distance(n.p, u.p); if (dd < bd) { bd = dd; best = (int)i; } }
            if (best < 0) { u.order = O_IDLE; if (u.carry > 0) { u.order = O_RETURN; } return; }
            u.target = best; u.path.clear();
        }
        Node& n = nodes[u.target];
        if (u.carry > 0 && u.carryRes != B().nodes[n.kind].res) { u.order = O_RETURN; u.path.clear(); return; }
        float reach = boat ? 1.6f : 1.1f;
        if (Vector2Distance(u.p, n.p) > reach) {
            Vector2 before = u.p; Seek(*this, u, n.p, boat ? 1.2f : 0.8f); StepMove(u, 1);
            // stuck (it can't get there): after 8 s, try the next nearest node of the kind
            if (Vector2Distance(before, u.p) < 0.01f) u.chan += STEP; else u.chan = 0;
            if (u.chan > 8) { u.chan = 0; int old = u.target, best = -1; float bd = 30; for (size_t i = 0; i < nodes.size(); i++) { if ((int)i == old || nodes[i].amount <= 0 || nodes[i].kind != n.kind) continue; float dd = Vector2Distance(nodes[i].p, u.p); if (dd < bd) { bd = dd; best = (int)i; } } u.target = best; u.lastNode = best; u.path.clear(); if (best < 0) u.order = O_IDLE; }
            return; }
        u.chan = 0;
        u.vel = {};
        if (n.trapped) { n.trapped = false; extern void SpringTrap(World& w, Node& n); SpringTrap(*this, n); }
        int res = B().nodes[n.kind].res; float cap = B().carry[res] + TechSum(u.owner, "carry");
        float g = GatherRate(u.owner, n) * STEP; g = std::min(g, n.amount); if (n.kind != N_FARM) n.amount -= g;
        u.carryRes = res; u.carry += g;
        if (n.kind == N_WRECK && n.amount <= 0) P.res[R_DOUB] += B().wreckDoub;
        if (u.carry >= cap - 1e-4f || n.amount <= 0) { u.order = O_RETURN; u.path.clear(); }
        return;
    }
    if (u.order == O_RETURN) {
        int dropId = NearestDrop(u.owner, u.p, u.carryRes); Building* b = Bd(dropId);
        if (!b) { u.order = O_IDLE; return; }
        Vector2 c = b->Centre(); float reach = b->size * 0.5f + (boat ? 2.4f : 1.2f);
        if (std::max(fabsf(u.p.x - c.x), fabsf(u.p.y - c.y)) > reach) { Seek(*this, u, c, b->size * 0.5f + (boat ? 1.5f : 0.6f)); StepMove(u, 1); if (u.pathFailed) u.order = O_IDLE; return; }
        P.res[u.carryRes] += u.carry; P.gathered[u.carryRes] += u.carry; u.carry = 0;
        Emit(EV_GATHER, u.p, u.id, u.carryRes);
        u.order = O_GATHER; u.target = u.lastNode; u.path.clear();
        if (u.target < 0) u.order = O_IDLE;
    }
}
void World::StepCombat(Unit& u) {
    const UnitDef& d = UD(u); if (d.attack <= 0 && d.type != D_HEAL) return;
    if (u.stunT > 0 || u.cocoonT > 0 || u.garrisoned >= 0 || u.inside >= 0) return;
    u.cool = std::max(0.0f, u.cool - STEP);
    // pick a target: the ordered one, else (not passive) the nearest enemy in sight
    int tk = -1, tid = -1; Vector2 tp{}; float tdist = 1e9f; uint32_t ttags = 0; float tarmor[2] = {0, 0}; bool tAir = false;
    auto consider = [&](int kind, int id, Vector2 p, float dist, uint32_t tags, float am, float ap, bool air) { if (dist < tdist) { tdist = dist; tk = kind; tid = id; tp = p; ttags = tags; tarmor[0] = am; tarmor[1] = ap; tAir = air; } };
    float rangeV = d.range + (d.tags & TG_RANGED ? TechSum(u.owner, "ranged_range_l3") : 0) + (d.tags & TG_SHIP ? TechSum(u.owner, "ship_range_l3") : 0);
    if (d.type == D_HEAL) {
        for (auto& o : units) if (!o.dead && Allied(o.owner, u.owner) && o.id != u.id && o.garrisoned < 0 && o.inside < 0 && (UD(o).tags & TG_INFANTRY) && o.hp < MaxHp(*this, o)) { float dd = Vector2Distance(o.p, u.p); if (dd < 6) consider(0, o.id, o.p, dd, 0, 0, 0, false); }
        if (tid < 0) return;
        if (tdist > rangeV + 0.4f) { if (u.order == O_IDLE) { Seek(*this, u, tp, rangeV * 0.8f); StepMove(u, 1); } return; }
        if (u.cool <= 0) { float h = d.heal * (1 + TechSum(u.owner, "heal")); Damage(u.id, u.owner, tid, 0, h, D_HEAL); u.cool = d.reload; if (Unit* o = U(tid)) { o->bleedT = 0; o->poisonT = 0; } }
        return;
    }
    if (u.order == O_ATTACK && u.target >= 0) {
        if (u.targetKind == 0) { if (const Unit* o = U(u.target)) if (o->garrisoned < 0 && o->inside < 0) consider(0, o->id, o->p, Vector2Distance(o->p, u.p), UD(*o).tags, UD(*o).armor[0], UD(*o).armor[1], UD(*o).move == MV_AIR); }
        else if (u.targetKind == 1) { if (const Building* b = Bd(u.target)) { Vector2 c = b->Centre(); float dd = std::max(0.0f, Vector2Distance(c, u.p) - b->size * 0.5f); consider(1, b->id, c, dd, TG_BUILDING, 0, 3, false); } }
        else if (u.targetKind == 3 && u.target < (int)sites.size()) { const Site& s = sites[u.target]; if (s.hp > 0) consider(3, u.target, s.p, std::max(0.0f, Vector2Distance(s.p, u.p) - 2.0f), TG_BUILDING, 0, 2, false); }
        if (tid < 0) { u.order = O_IDLE; u.target = -1; }
    }
    bool autoAcq = tid < 0 && u.stance != ST_PASSIVE && (u.order == O_IDLE || u.order == O_ATTACK_MOVE || u.order == O_PATROL) && u.scanT <= 0;
    if (autoAcq) {
        u.scanT = 0.3f + (u.id % 4) * 0.05f;   // (a look round a few times a second, staggered)
        float look = std::max(rangeV, d.vision);
        if (u.stance == ST_GROUND) look = rangeV;
        ForNear(u.p, look, [&](const Unit& o) {
            if (o.dead || o.garrisoned >= 0 || o.inside >= 0 || !Enemies(u.owner, o.owner)) return;
            float dd = Vector2Distance(o.p, u.p); if (dd > look) return;
            if (u.hire >= 0 && o.owner != u.hire) return;   // (a pirate on contract attacks only its target)
            if (!NeutralHostile(*this, u, o.owner) || !NeutralHostile(*this, o, u.owner)) return;
            const UnitDef& od = UD(o);
            if (d.special == "burrow" && !(od.tags & TG_SHIP) && !Water(TileAt(o.p))) return;   // (the Ghost Worm: ships and swimmers only)
            if (od.move == MV_AIR && !(d.tags & (TG_RANGED)) && d.key != "gunboat" && !(d.tags & TG_AIR)) return;   // (only some things hit airships)
            if (d.move == MV_AIR && od.move == MV_AIR) return;
            if (o.hiddenT > 0 && dd > 3) return;
            if (IsPlayer(u.owner) && u.owner < (int)players.size() && !Sees(u.owner, o.p)) return;
            consider(0, o.id, o.p, dd + ((od.tags & TG_WORKER) ? 2.0f : 0.0f), od.tags, od.armor[0], od.armor[1], od.move == MV_AIR);
        });
        if (tid < 0 && (u.order == O_ATTACK_MOVE || u.stance == ST_AGGRESSIVE)) for (const auto& b : buildings) {
            if (b.dead || !Enemies(u.owner, b.owner)) continue; if (u.hire >= 0 && b.owner != u.hire) continue; float dd = std::max(0.0f, Vector2Distance(b.Centre(), u.p) - b.size * 0.5f); if (dd > look) continue;
            consider(1, b.id, b.Centre(), dd + 3, TG_BUILDING, 0, 3, false);
        }
        if (tid >= 0) u.scanT = 0;   // (in a fight: look again the moment this target falls)
    }
    if (tid < 0) return;
    // in range? (melee: touching; ranged: range; ships can shell the shore within their range)
    float need = rangeV + (tk == 1 ? 0.2f : 0.25f) + (d.range <= 0.6f ? 0.3f : 0);
    if (tdist > need) {
        if (u.stance == ST_GROUND || u.order == O_MOVE) return;
        if (u.stance == ST_DEFENSIVE && Vector2Distance(u.p, u.patrolA) > 8 && u.order != O_ATTACK) return;
        if (u.order != O_ATTACK) { u.order = O_ATTACK; u.target = tid; u.targetKind = tk; }
        Seek(*this, u, tp, std::max(0.3f, rangeV * 0.85f) + (tk == 1 ? 1.0f : 0)); StepMove(u, 1);
        return;
    }
    u.vel = {};
    if (d.minRange > 0 && tdist < d.minRange) return;
    if (u.cool > 0) return;
    // the damage formula: attack + bonus vs tags - armor of the type (blast ignores armor), minimum 1
    float atk = d.attack, bonus = 0;
    for (const auto& bb : d.bonus) if (ttags & bb.first) bonus += bb.second;
    if (IsPlayer(u.owner) && u.owner < (int)players.size()) {
        if ((d.tags & TG_INFANTRY) && d.type == D_MELEE) atk += TechSum(u.owner, "melee_attack");
        if (d.tags & TG_RANGED) atk += TechSum(u.owner, "ranged_attack");
        if (d.tags & TG_SHIP) atk += TechSum(u.owner, "ship_attack");
        if (HasTech(u.owner, "elite") && !(d.tags & (TG_WORKER | TG_HERO | TG_ULTIMATE))) atk *= 1.15f;
    }
    atk *= 1 + 0.15f * u.molts;
    float arm = d.type == D_MELEE ? tarmor[0] : d.type == D_PIERCE ? tarmor[1] : 0;
    if (tk == 0) if (const Unit* o = U(tid)) arm += d.type != D_BLAST ? o->auraArmor : 0;
    if (tk == 0) if (const Unit* o = U(tid)) if (IsPlayer(o->owner) && o->owner < (int)players.size()) { const UnitDef& od = UD(*o); if (!(od.tags & TG_SHIP)) { arm += d.type == D_MELEE ? TechSum(o->owner, "land_melee_armor") : d.type == D_PIERCE ? TechSum(o->owner, "land_pierce_armor") : 0; } else if (d.type == D_PIERCE) arm += TechSum(o->owner, "ship_pierce_armor"); }
    float dmg = std::max(1.0f, atk + bonus - arm);
    // modifiers: morale, the coal shortage, high ground, the jungle, airships hit by the few things that can
    float mult = 1;
    if (IsPlayer(u.owner) && u.owner < (int)players.size()) {
        if (players[u.owner].faction != 5) { if (u.morale < B().low2) mult *= B().low2Dmg; else if (u.morale < B().low1) mult *= B().low1Dmg; }
        if (players[u.owner].coalShort && (d.upkeep > 0 || (players[u.owner].faction == 5 && !(d.tags & TG_WORKER)))) mult *= HasTech(u.owner, "pressure_regulators") ? 1 - (1 - B().emptyDamage) * 0.5f : B().emptyDamage;
    }
    float hA = Terrain(*this, u.p), hT = Terrain(*this, tp);
    if (hA > hT + 0.8f && d.range > 1) mult *= B().highDeal; else if (hT > hA + 0.8f && d.range > 1) mult *= B().highTake;
    if (d.range > 1 && TileAt(tp) == T_JUNGLE) mult *= B().jungleRanged;
    if (tAir && (d.tags & (TG_RANGED)) ) mult *= 0.5f;
    if (tAir && d.key == "gunboat") mult *= 0.5f;
    if (u.weakT > 0) mult *= 0.8f; if (u.strongT > 0) mult *= 1.2f; mult *= std::max(0.2f, 1 + u.auraAtk);
    if (!IsPlayer(u.owner)) mult *= B().neutralStrength[std::clamp(set.neutral, 0, 2)];
    dmg *= mult;
    u.cool = d.reload / (u.hasteT > 0 ? 1.3f : 1.0f);
    u.facing = atan2f(tp.y - u.p.y, tp.x - u.p.x);
    if (d.range > 1.2f) {
        float fly = std::clamp(tdist / 18.0f, 0.12f, 1.2f);
        pending.push_back({u.id, u.owner, tid, tk, dmg, d.type, fly, tp, d.type == D_BLAST ? B().splashR : 0.0f, ttags});
        shots.push_back({u.p, tp, 0, fly, d.type == D_BLAST ? 1 : (d.tags & TG_SHIP) ? 2 : 0, d.type == D_BLAST ? 2.5f : 0.6f});
        Emit(EV_SHOT, u.p, u.id, d.type);
    } else {
        Damage(u.id, u.owner, tid, tk, dmg, d.type, ttags);
        if (d.type == D_BLAST) for (auto& o : units) if (!o.dead && o.id != tid && Enemies(u.owner, o.owner) && Vector2Distance(o.p, tp) < B().splashR) Damage(u.id, u.owner, o.id, 0, dmg * B().splashFrac, d.type);
        Emit(EV_SHOT, u.p, u.id, d.type);
    }
}
void World::StepUnit(Unit& u) {
    const UnitDef& d = UD(u); const Balance& Bl = B();
    // status effects (the roguelike's): stun, bleed, poison, weaken, strengthen; a molt's cocoon
    u.scanT -= STEP; u.stunT = std::max(0.0f, u.stunT - STEP); u.fastT = std::max(0.0f, u.fastT - STEP); u.slowT = std::max(0.0f, u.slowT - STEP); u.hasteT = std::max(0.0f, u.hasteT - STEP);
    if (u.auraHeal > 0) u.hp = std::min(MaxHp(*this, u), u.hp + u.auraHeal * STEP); u.weakT = std::max(0.0f, u.weakT - STEP); u.strongT = std::max(0.0f, u.strongT - STEP); u.cocoonT = std::max(0.0f, u.cocoonT - STEP); u.hiddenT = std::max(0.0f, u.hiddenT - STEP); u.abilityT = std::max(0.0f, u.abilityT - STEP);
    if (u.bleedT > 0) { u.bleedT -= STEP; u.hp -= 2 * STEP; }
    if (u.poisonT > 0) { u.poisonT -= STEP; u.hp -= 1 * STEP; }
    if (u.hp <= 0) { Kill(u, -1); return; }
    if (u.life > 0) { u.life -= STEP; if (u.life <= 0) { u.dead = true; return; } }
    if (u.garrisoned >= 0) { u.hp = std::min(MaxHp(*this, u), u.hp + 1 * STEP); return; }
    if (u.inside >= 0) return;
    // lava, storms
    int tt = TileAt(u.p);
    if (tt == T_LAVA && d.move != MV_AIR) { u.hp -= Bl.lavaDps * STEP; if (u.hp <= 0) { Kill(u, -1); return; } }
    if (weather == 2 && (d.tags & TG_SHIP) && Vector2Distance(u.p, weatherAt) < weatherR) { float dps = Bl.stormDps * ((IsPlayer(u.owner) && u.owner < (int)players.size() && players[u.owner].faction == 5 && !HasTech(u.owner, "grounded_casings")) ? 2.0f : 1.0f); u.hp -= dps * STEP; if (u.hp <= 0) { Kill(u, -1); return; } }
    if (weather == 3 && (d.tags & TG_SHIP) && Vector2Distance(u.p, weatherAt) < weatherR) { Vector2 pull = Vector2Normalize(Vector2Subtract(weatherAt, u.p)); u.p = Vector2Add(u.p, Vector2Scale(pull, 0.6f * STEP)); }
    // regeneration (Field Medicine, the Legionnaire)
    if ((d.tags & TG_INFANTRY) && IsPlayer(u.owner) && u.owner < (int)players.size()) { float reg = TechSum(u.owner, "regen"); if (d.special == "regen") reg += 0.6f; if (reg > 0) u.hp = std::min(MaxHp(*this, u), u.hp + reg * STEP); }
    FactionUnitStep(u);
    if (u.stunT > 0 || u.cocoonT > 0) return;
    float sm = 1;
    if (IsPlayer(u.owner) && u.owner < (int)players.size()) {
        const Player& P = players[u.owner];
        if (P.faction != 5 && u.morale < Bl.low2) sm *= Bl.low2Speed;
        if (P.coalShort && (d.upkeep > 0 || (P.faction == 5 && !(d.tags & TG_WORKER)))) sm *= HasTech(u.owner, "pressure_regulators") ? 1 - (1 - Bl.emptySpeed) * 0.5f : Bl.emptySpeed;
        // ships beyond coaling range go 25% slower
        if (d.move == MV_SEA && !(d.tags & TG_WORKER)) { bool inRange = false; for (const auto& b : buildings) if (!b.dead && Allied(b.owner, u.owner) && (BD(b).key == "harbor" || BD(b).key == "dock" || BD(b).key == "coaling_station") && Vector2Distance(b.Centre(), u.p) < Bl.coalRange) { inRange = true; break; } if (!inRange) sm *= Bl.noCoalSpeed; }
        if (u.weakT > 0) sm *= 0.9f; if (u.strongT > 0) sm *= 1.1f;
    }
    sm *= 1 + u.auraSpeed; if (u.fastT > 0) sm *= 1.4f; if (u.slowT > 0) sm *= 0.5f;
    // orders
    switch (u.order) {
        case O_IDLE: StepCombat(u); break;
        case O_MOVE: Seek(*this, u, u.goal, 0.5f); StepMove(u, sm); if (u.pathAt >= (int)u.path.size() && Vector2Distance(u.p, u.goal) < 1.2f) u.order = O_IDLE; else if (u.pathAt >= (int)u.path.size()) { u.repathT = 0; if (Vector2Distance(u.p, u.goal) < 3) u.order = O_IDLE; } break;
        case O_ATTACK_MOVE: case O_PATROL: {
            Unit before = u; StepCombat(u);
            if (u.order == O_ATTACK) break;   // (it found something)
            if (before.order == O_ATTACK_MOVE || before.order == O_PATROL) { u.order = before.order; Seek(*this, u, u.goal, 0.8f); StepMove(u, sm); if (Vector2Distance(u.p, u.goal) < 1.2f) { if (u.order == O_PATROL) { Vector2 tmp = u.patrolA; u.patrolA = u.patrolB; u.patrolB = tmp; u.goal = u.patrolB; u.path.clear(); } else u.order = O_IDLE; } }
            break; }
        case O_ATTACK: StepCombat(u); break;
        case O_GATHER: case O_RETURN: StepGather(u); break;
        case O_BUILD: {
            Building* b = Bd(u.target); if (!b || (b->progress >= 1 && b->hp >= BD(*b).hp * (1 + TechSum(b->owner, "building_hp")) - 0.5f)) {
                // finished: carry on gathering what's near (a drop-off), or go idle
                u.order = O_IDLE; if (b && BD(*b).drop != "") { int best = -1; float bd = 10; for (size_t i = 0; i < nodes.size(); i++) if (nodes[i].amount > 0 && !B().nodes[nodes[i].kind].water && Vector2Distance(nodes[i].p, b->Centre()) < bd) { bd = Vector2Distance(nodes[i].p, b->Centre()); best = (int)i; } if (best >= 0) { u.order = O_GATHER; u.target = best; u.lastNode = best; u.path.clear(); } }
                if (b && BD(*b).key == "farm") for (size_t i = 0; i < nodes.size(); i++) if (nodes[i].farm == b->id) { u.order = O_GATHER; u.target = (int)i; u.lastNode = (int)i; u.path.clear(); }
                break; }
            Vector2 c = b->Centre(); float reach = b->size * 0.5f + 1.0f;
            if (fabsf(u.p.x - c.x) > reach || fabsf(u.p.y - c.y) > reach) { Seek(*this, u, c, b->size * 0.5f + 0.6f); StepMove(u, sm); break; }
            u.vel = {}; b->buildersNow++;
            break; }
        case O_GARRISON: {
            Building* b = Bd(u.target); if (!b) { u.order = O_IDLE; break; }
            Vector2 c = b->Centre(); if (Vector2Distance(u.p, c) > b->size * 0.5f + 1.2f) { Seek(*this, u, c, b->size * 0.5f + 0.6f); StepMove(u, sm); break; }
            if ((int)b->garrison.size() >= BD(*b).garrison) { u.order = O_IDLE; break; }
            b->garrison.push_back(u.id); u.garrisoned = b->id; u.order = O_IDLE;
            if (u.relic && (BD(*b).relics || BD(*b).key == "harbor")) { b->relics += u.relic; u.relic = 0; Emit(EV_RELIC, c, u.owner, b->id); }
            break; }
        case O_BOARD: {
            Unit* tr = U(u.target); if (!tr) { u.order = O_IDLE; break; }
            int cap = UD(*tr).carry; int used = 0; for (int id : tr->cargo) if (const Unit* c = U(id)) used += UD(*c).pop;
            if (used + d.pop > cap) { u.order = O_IDLE; break; }
            if (Vector2Distance(u.p, tr->p) > 2.2f) { Seek(*this, u, tr->p, 1.4f); StepMove(u, sm); break; }
            tr->cargo.push_back(u.id); u.inside = tr->id; u.order = O_IDLE; u.path.clear(); break; }
        case O_UNLOAD: {
            // sail to the shore nearest the spot, then put everyone ashore one a second
            bool atShore = false; int landK = -1;
            // the island the goal is on (or nearest to it): only its shore will do
            int gIsle = -1; for (int r = 0; r < 8 && gIsle < 0; r++) for (int dy = -r; dy <= r && gIsle < 0; dy++) for (int dx = -r; dx <= r && gIsle < 0; dx++) { int x = (int)u.goal.x + dx, y = (int)u.goal.y + dy; if (In(x, y) && Land(tile[Idx(x, y)])) gIsle = isle[Idx(x, y)]; }
            for (int dy = -2; dy <= 2 && landK < 0; dy++) for (int dx = -2; dx <= 2 && landK < 0; dx++) { int x = (int)u.p.x + dx, y = (int)u.p.y + dy; if (In(x, y) && Land(tile[Idx(x, y)]) && occ[Idx(x, y)] < 0 && (gIsle < 0 || isle[Idx(x, y)] == gIsle)) landK = Idx(x, y); }
            atShore = landK >= 0 && (Vector2Distance(u.p, u.goal) < 9 || (!u.path.empty() && u.pathAt >= (int)u.path.size() && Vector2Distance(u.p, u.goal) < 30));   // (as near as the sea goes)
            if (!atShore) { Seek(*this, u, u.goal, 2.5f); StepMove(u, sm); if (u.pathAt >= (int)u.path.size() && u.repathT < 2.0f) { u.repathT = 0; } break; }
            u.vel = {}; u.cool -= STEP * (1 + TechSum(u.owner, "unload_speed"));
            if (d.carryWorkers > 0) {   // a Colony Ship: three workers and a Colony Hall site
                for (int k = 0; k < d.carryWorkers; k++) SpawnUnit(u.owner, B().Unit("worker"), {landK % W + 0.5f + k * 0.3f, landK / W + 0.5f});
                int ch = B().Building("colony_hall"); int bx = -1, by = -1; float bd = 1e9f;
                for (int dy = -8; dy <= 8; dy++) for (int dx = -8; dx <= 8; dx++) { int x = landK % W + dx, y = landK / W + dy; bool ok = true; int s = B().buildings[ch].size;
                    for (int yy = y; yy < y + s && ok; yy++) for (int xx = x; xx < x + s && ok; xx++) if (!In(xx, yy) || !Land(tile[Idx(xx, yy)]) || occ[Idx(xx, yy)] >= 0) ok = false;
                    if (ok) { float dd = (float)(dx * dx + dy * dy); if (dd < bd) { bd = dd; bx = x; by = y; } } }
                if (bx >= 0) { int id = PlaceBuilding(u.owner, ch, bx, by, false); for (auto& o : units) if (!o.dead && o.owner == u.owner && (UD(o).tags & TG_WORKER) && !(UD(o).tags & TG_SHIP) && Vector2Distance(o.p, u.p) < 4 && o.order == O_IDLE) SetOrder(*this, o, O_BUILD, id, Bd(id)->Centre(), 1); }
                Kill(u, -1); break;
            }
            if (u.cargo.empty()) { u.order = O_IDLE; break; }
            if (u.cool <= 0) {
                int id = u.cargo.back(); u.cargo.pop_back(); if (Unit* c = U(id)) { c->inside = -1; c->p = {landK % W + 0.5f, landK / W + 0.5f}; c->landedT = t; c->order = O_IDLE; if (!IsPlayer(c->owner)) { c->order = O_ATTACK_MOVE; c->goal = u.goal; c->stance = ST_AGGRESSIVE; } Emit(EV_LAND, c->p, c->id, c->owner); }
                u.cool = 1.0f;
            }
            break; }
        case O_TRADE: {
            // a round trip between your Exchange and theirs (or a cove): each arrival pays both ends 0.15 x the distance
            const Building* mineEx = nullptr; for (const auto& b : buildings) if (!b.dead && b.owner == u.owner && BD(b).key == "exchange") { mineEx = &b; break; }
            Vector2 far = u.goal; if (u.targetKind == 1) { if (const Building* b = Bd(u.target)) far = b->Centre(); else { u.order = O_IDLE; break; } }
            if (!mineEx) { u.order = O_IDLE; break; }
            Vector2 dest = u.home == 1 ? mineEx->Centre() : far;
            if (Vector2Distance(u.p, dest) > 5) { Seek(*this, u, dest, 4); StepMove(u, sm); break; }
            if (IsPlayer(u.owner) && u.owner < (int)players.size() && players[u.owner].tradePauseT <= 0) {
                float pay = Bl.tradePerTile * Vector2Distance(mineEx->Centre(), far) * (1 + TechSum(u.owner, "trade_income"));
                players[u.owner].res[R_DOUB] += pay; if (u.targetKind == 1) if (const Building* b = Bd(u.target)) if (IsPlayer(b->owner)) players[b->owner].res[R_DOUB] += pay;
            }
            u.home = u.home == 1 ? 0 : 1; u.path.clear(); break; }
        default: StepCombat(u); break;
    }
    // separation: units on the same footing push apart a little
    (void)0;
}
void World::StepBuilding(Building& b) {
    const BuildingDef& d = BD(b); const Balance& Bl = B();
    if (b.progress < 1) {
        if (b.buildersNow > 0) {
            float eff = d.time * std::max(Bl.buildMinFrac, 1.0f / (float)(1 << std::min(4, b.buildersNow - 1)));
            float add = STEP / std::max(1.0f, eff);
            b.progress = std::min(1.0f, b.progress + add); b.hp = std::min(d.hp, b.hp + d.hp * add * 0.9f);
            if (b.progress >= 1) { b.hp = std::max(b.hp, d.hp * 0.9f); Emit(EV_BUILT, b.Centre(), b.id, b.owner, b.def); if (IsPlayer(b.owner)) players[b.owner].built++; UpdateTerritory(); if (d.key == "lighthouse") Emit(EV_CLAIM, b.Centre(), b.owner, b.island); }
        }
        b.buildersNow = 0; return;
    }
    // repair: workers at a damaged building
    if (b.buildersNow > 0) { float mx = d.hp * (1 + TechSum(b.owner, "building_hp")); b.hp = std::min(mx, b.hp + mx / std::max(1.0f, d.time) * 0.5f * STEP * b.buildersNow); }
    b.buildersNow = 0;
    // towers and town centres fire (one shot per garrisoned unit, plus their own)
    if (d.attack > 0 && IsPlayer(b.owner)) {
        b.cool = std::max(0.0f, b.cool - STEP);
        if (b.cool <= 0) {
            float range = d.range + TechSum(b.owner, "tower_range") + (TileAt(b.Centre()) == T_HILL ? Bl.hillTowerRange : 0);
            int best = -1; float bd = range + 0.01f; Vector2 c = b.Centre();
            for (const auto& o : units) {
                if (o.dead || o.garrisoned >= 0 || o.inside >= 0 || !Enemies(b.owner, o.owner) || !NeutralHostile(*this, o, b.owner)) continue; const UnitDef& od = UD(o);
                if (o.hiddenT > 0 && Vector2Distance(o.p, b.Centre()) > 4) continue;
                if (od.move == MV_AIR && !d.antiAir && d.key != "coastal_battery") continue; if (d.antiAir && od.move != MV_AIR) continue;
                float dd = Vector2Distance(o.p, c) - b.size * 0.5f; if (dd < bd) { bd = dd; best = o.id; }
            }
            if (best >= 0) {
                const Unit* o = U(best); const UnitDef& od = UD(*o);
                float dmg = d.attack + (int)b.garrison.size() * 2; if (d.bonusShips > 0 && (od.tags & TG_SHIP)) dmg *= 1 + d.bonusShips; if (d.antiAir) dmg *= 4;
                dmg = std::max(1.0f, dmg - od.armor[1]);
                float fly = std::clamp(bd / 18.0f, 0.1f, 1.0f);
                pending.push_back({-b.id, b.owner, best, 0, dmg, D_PIERCE, fly, o->p, 0, od.tags}); shots.push_back({c, o->p, 0, fly, 0, 1.6f});
                b.cool = 2.0f; Emit(EV_SHOT, c, -b.id, D_PIERCE);
            }
        }
    }
    // production: units, techs, the next era (one at a time)
    if (b.queue.empty()) return;
    QueueItem& q = b.queue.front(); float need = q.kind == 0 ? Bl.units[q.def].train : q.kind == 1 ? Bl.techs[q.def].time : Bl.eraTime[q.def];
    if (q.kind == 0) { const UnitDef& ud = Bl.units[q.def]; if (players[b.owner].pop + ud.pop > PopCap(b.owner) && ud.pop > 0) return; }   // (waits for room)
    b.qT += STEP;
    if (b.qT < need) return;
    b.qT = 0; Player& P = players[b.owner];
    if (q.kind == 0) {
        const UnitDef& ud = Bl.units[q.def]; Move m = ud.move; int fac = P.faction; if (fac == 3 && m == MV_LAND) m = MV_AMPHIB;
        Vector2 c = b.Centre(); int k = -1;
        // ships out of the water side, land units out of the land side (toward the rally)
        Vector2 inland = b.island >= 0 && b.island < (int)islands.size() ? islands[b.island].c : Vector2Add(c, {0, (float)b.size});
        Vector2 toward = b.hasRally ? b.rally : (ud.move == MV_SEA ? Vector2Subtract(c, Vector2Subtract(inland, c)) : inland); Vector2 dir = Vector2Normalize(Vector2Subtract(toward, c)); if (Vector2Length(dir) < 0.1f) dir = {0, 1};
        for (int r = b.size / 2 + 1; r < b.size + 8 && k < 0; r++) for (int a = 0; a < 16 && k < 0; a++) { float an = atan2f(dir.y, dir.x) + (a % 2 ? 1 : -1) * (a / 2) * 0.4f; int x = (int)(c.x + cosf(an) * r), y = (int)(c.y + sinf(an) * r); if (Passable(x, y, ud.move == MV_SEA ? MV_SEA : m, fac) && (ud.move != MV_LAND || !Water(tile[Idx(x, y)]) || fac == 3) && (ud.move == MV_SEA || b.island < 0 || isle[Idx(x, y)] == b.island)) k = Idx(x, y); }
        if (k < 0) { b.qT = need; return; }
        int id = SpawnUnit(b.owner, q.def, {k % W + 0.5f, k / W + 0.5f}); P.trained++;
        if (ud.tags & TG_HERO) P.heroUnit = id;
        if (b.hasRally) if (Unit* u = U(id)) { u->order = O_MOVE; u->goal = b.rally; }
        // a worker or a boat sent to a node's rally gathers it
        if (Unit* u = U(id)) if ((ud.tags & TG_WORKER) && b.hasRally) { int best = -1; float bd = 2.5f; for (size_t i = 0; i < nodes.size(); i++) if (nodes[i].amount > 0 && B().nodes[nodes[i].kind].water == ((ud.tags & TG_SHIP) != 0) && Vector2Distance(nodes[i].p, b.rally) < bd) { bd = Vector2Distance(nodes[i].p, b.rally); best = (int)i; } if (best >= 0) { u->order = O_GATHER; u->target = best; u->lastNode = best; } }
        Emit(EV_TRAINED, c, id, b.owner, q.def);
    } else if (q.kind == 1) {
        const TechDef& td = Bl.techs[q.def]; P.tech[q.def] = 1; if (td.forgeLine >= 0) P.forge[td.forgeLine] = td.forgeLevel;
        if (td.key == "deep_bore_rigs") for (auto& n : nodes) if (n.kind == N_BRASS || n.kind == N_COAL) { n.amount *= 1.25f; n.cap *= 1.25f; }
        if (td.key == "fresnel_lens") UpdateTerritory();
        Emit(EV_RESEARCHED, b.Centre(), b.owner, q.def);
    } else {
        P.era = q.def; P.eraBuilding = -1; P.eraAt[q.def] = t; Emit(EV_ERA, b.Centre(), b.owner, q.def);
        FactionStart(P);   // (Nemo's Logbook draws again on each era)
    }
    b.queue.erase(b.queue.begin());
}
void World::StepPlayer(Player& p) {
    const Balance& Bl = B(); if (!p.alive) return;
    // population
    int pop = 0; for (const auto& u : units) if (!u.dead && u.owner == p.id) pop += UD(u).pop; p.pop = pop; p.popCap = PopCap(p.id);
    // upkeep: Coal every minute for every steam unit (Coal Efficiency, cards and the Great Furnace cut it)
    float up = 0; for (const auto& u : units) if (!u.dead && u.owner == p.id) up += UD(u).upkeep;
    float cut = 1 + TechSum(p.id, "upkeep"); if (p.faction == 0 && std::find(p.cards.begin(), p.cards.end(), 7) != p.cards.end()) cut *= 0.75f;
    for (const auto& b : buildings) if (!b.dead && b.owner == p.id && BD(b).key == "wonder" && b.progress >= 1 && p.faction == 5) cut *= 0.5f;
    p.res[R_COAL] -= up * std::max(0.0f, cut) / 60.0f * STEP; if (p.res[R_COAL] < 0) p.res[R_COAL] = 0;
    float shortK = p.faction == 5 && HasTech(p.id, "pressure_regulators") ? 0.5f : 1.0f; (void)shortK;
    p.coalShort = up > 0 && p.res[R_COAL] <= 0.5f;
    // Brood Pools and the like make Food with no worker
    for (const auto& b : buildings) if (!b.dead && b.owner == p.id && b.progress >= 1 && BD(b).foodPerSec > 0) { p.res[R_FOOD] += BD(b).foodPerSec * STEP; p.gathered[R_FOOD] += BD(b).foodPerSec * STEP; }
    // relics trickle Ichor
    int relics = 0; for (const auto& b : buildings) if (!b.dead && b.owner == p.id) relics += b.relics; p.res[R_ICHOR] += relics * B().nodes[N_VENT].rate * 2 * STEP;
    // the Exchange's prices drift back
    for (auto& m : p.exchange) m += (1 - m) * (Bl.exDrift / Bl.exDriftEvery) * STEP;
    p.tradePauseT = std::max(0.0f, p.tradePauseT - STEP);
    p.ultimateCD = std::max(0.0f, p.ultimateCD - STEP);
    FactionStep(p);
    // conquest: no town centre left -> 90 s to start another with a Colony Ship, or out
    bool town = false; for (const auto& b : buildings) if (!b.dead && b.owner == p.id && (BD(b).key == "harbor" || BD(b).key == "colony_hall")) town = true;
    if (town) p.noTownT = -1; else { if (p.noTownT < 0) p.noTownT = t; bool ship = false; for (const auto& u : units) if (!u.dead && u.owner == p.id && UD(u).key == "colony_ship") ship = true;
        if (t - p.noTownT > Bl.conquestGrace && !ship && (set.victory & 1)) { p.alive = false; p.eliminated = true; Emit(EV_ELIM, {0, 0}, p.id); for (auto& u : units) if (u.owner == p.id) u.dead = true; } }
}
void World::Victory() {
    const Balance& Bl = B(); if (over) return;
    // a side: the living players allied to each other (an Open alliance counts only once it's 5 minutes old)
    auto sideOf = [&](int p) { std::vector<int> s; for (const auto& q : players) if (q.alive && Allied(p, q.id) && (set.teams != 0 || q.id == p || t - players[p].allyT[q.id] >= Bl.allianceAge)) s.push_back(q.id); return s; };
    auto win = [&](int p, const char* why) { winners = sideOf(p); winner = p; over = true; Emit(EV_VICTORY, {0, 0}, p, (int)winners.size(), why[0]); };
    // conquest: one side left
    if (set.victory & 1) { int alive = 0, any = -1; for (const auto& p : players) if (p.alive) { alive++; any = p.id; } if (alive == 0) { over = true; return; }
        std::vector<int> s = sideOf(any); int others = 0; for (const auto& p : players) if (p.alive && std::find(s.begin(), s.end(), p.id) == s.end()) others++;
        if (others == 0 && (int)players.size() > 1) { win(any, "conquest"); return; } }
    // relics: one side holds every relic for the countdown
    if ((set.victory & 2) && relicsTotal > 0) {
        int leader = -1; for (const auto& p : players) { int r = 0; for (int q : sideOf(p.id)) for (const auto& b : buildings) if (!b.dead && b.owner == q) r += b.relics; if (r >= relicsTotal) leader = p.id; }
        if (leader >= 0) { if (relicLeader != leader) { relicLeader = leader; relicCountT = t; } if (t - relicCountT >= Bl.relicCountdown) { win(leader, "relics"); return; } } else { relicLeader = -1; relicCountT = -1; }
    }
    // volcano ascension: hold the altar(s) through a full eruption cycle (fathoms_neutral.cpp keeps the altar's holder)
    if (set.victory & 4) {
        int need = 0, holder = -2; for (const auto& s : sites) if (s.kind == S_ALTAR) { need++; if (holder == -2) holder = s.holder; else if (s.holder != holder) holder = -1; }
        if (need > 0 && holder >= 0) { if (volcanoLeader != holder) { volcanoLeader = holder; volcanoCountT = t; } if (t - volcanoCountT >= Bl.volcanoCountdown) { win(holder, "volcano"); return; } } else { volcanoLeader = -1; volcanoCountT = -1; }
    }
    // the time cap: the highest score (ties: most islands, then most relics)
    if (set.timeCap > 0 && t >= set.timeCap) {
        int best = -1; for (const auto& p : players) if (p.alive && (best < 0 || p.score > players[best].score)) best = p.id;
        if (best >= 0) win(best, "score"); else over = true;
    }
}

// ---------------------------------------------------------------- the step
static float Rad(const UnitDef& d) { return (d.tags & TG_SHIP) ? 0.55f : (d.tags & (TG_LARGE | TG_ULTIMATE)) ? 0.4f : 0.26f; }   // (half the spacing units keep)
void World::Step() {
    if (over) return;
    t += STEP; if (units.capacity() < units.size() + 48) units.reserve(units.size() * 2 + 64); if (sites.capacity() < sites.size() + 16) sites.reserve(sites.size() * 2 + 32);
    for (auto& p : players) StepPlayer(p);
    FactionAuras(*this);
    // the grid of units by 8-tile cell (target scans look only at nearby cells)
    gridW = (W + 7) / 8; int gh = (H + 7) / 8; gridHead.assign(gridW * gh, -1); gridNext.assign(units.size(), -1);
    for (int i = 0; i < (int)units.size(); i++) { const Unit& u = units[i]; if (u.dead) continue; int c = std::clamp((int)(u.p.y / 8), 0, gh - 1) * gridW + std::clamp((int)(u.p.x / 8), 0, gridW - 1); gridNext[i] = gridHead[c]; gridHead[c] = i; }
    for (size_t i = 0; i < units.size(); i++) { Unit& u = units[i]; if (!u.dead) StepUnit(units[i]); }
    // separation (cheap: units overlapping on the same tile push apart)
    { std::unordered_map<int, std::vector<int>> cell; cell.reserve(units.size());
      for (int i = 0; i < (int)units.size(); i++) { const Unit& u = units[i]; if (u.dead || u.inside >= 0 || u.garrisoned >= 0) continue; cell[((int)(u.p.y * 0.5f)) * W + (int)(u.p.x * 0.5f)].push_back(i); }
      for (auto& kv : cell) { int cx = kv.first % W, cy = kv.first / W; std::vector<int> v = kv.second;
        for (int dy = 0; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) { if (dy == 0 && dx <= 0) continue; auto it = cell.find((cy + dy) * W + cx + dx); if (it != cell.end()) v.insert(v.end(), it->second.begin(), it->second.end()); }
        size_t own = kv.second.size(); if (v.size() < 2) continue; for (size_t a = 0; a < own; a++) for (size_t b = a + 1; b < v.size(); b++) { Unit& A = units[v[a]]; Unit& Bu = units[v[b]]; if ((UD(A).move == MV_AIR) != (UD(Bu).move == MV_AIR)) continue; Vector2 d = Vector2Subtract(A.p, Bu.p); float L = Vector2Length(d); float R = (Rad(UD(A)) + Rad(UD(Bu))); if (L > R) continue; if (L < 1e-3f) d = {Rand() - 0.5f, Rand() - 0.5f}; d = Vector2Scale(Vector2Normalize(d), (R - L) * 0.25f);
          Vector2 na = Vector2Add(A.p, d), nb = Vector2Subtract(Bu.p, d); Move ma = UD(A).move, mb = UD(Bu).move; int f1 = IsPlayer(A.owner) && A.owner < (int)players.size() ? players[A.owner].faction : -1, f2 = IsPlayer(Bu.owner) && Bu.owner < (int)players.size() ? players[Bu.owner].faction : -1;
          if (Passable((int)na.x, (int)na.y, ma, f1)) A.p = na; if (Passable((int)nb.x, (int)nb.y, mb, f2)) Bu.p = nb; } } }
    for (auto& b : buildings) if (!b.dead) StepBuilding(b);
    // damage in flight lands
    for (size_t i = 0; i < pending.size(); i++) {
        Pending& q = pending[i]; q.t -= STEP; if (q.t > 0) continue;
        if (q.targetKind == 0) { const Unit* o = U(q.target); bool hit = o && Vector2Distance(o->p, q.at) < 2.5f; if (hit) Damage(q.attacker, q.owner, q.target, 0, q.dmg, q.type, q.bonusVs); }
        else Damage(q.attacker, q.owner, q.target, q.targetKind, q.dmg, q.type, q.bonusVs);
        if (q.splash > 0) for (auto& o : units) if (!o.dead && o.id != q.target && Enemies(q.owner, o.owner) && Vector2Distance(o.p, q.at) < q.splash) Damage(q.attacker, q.owner, o.id, 0, q.dmg * B().splashFrac, q.type);
        pending.erase(pending.begin() + i); i--;
    }
    for (auto& s : shots) s.t += STEP;
    shots.erase(std::remove_if(shots.begin(), shots.end(), [](const Projectile& p) { return p.t >= p.T; }), shots.end());
    // nodes regrow (fish, kelp, pearls) while some is left
    for (auto& n : nodes) { float every = B().nodes[n.kind].regrowEvery; if (every > 0 && n.amount > 0 && n.amount < n.cap) { n.regrowT += STEP; if (n.regrowT >= every) { n.regrowT = 0; n.amount = std::min(n.cap, n.amount + 1); } } }
    // morale every 10 s: home ground heals it, away drains it (twice in enemy territory)
    if (++moraleTick >= (int)(B().moraleEvery / STEP)) {
        moraleTick = 0;
        for (auto& u : units) {
            if (u.dead || !IsPlayer(u.owner) || u.owner >= (int)players.size() || (UD(u).tags & TG_WORKER)) continue;
            int o = owner[Idx(std::clamp((int)u.p.x, 0, W - 1), std::clamp((int)u.p.y, 0, H - 1))];
            float loss = B().moraleAway * (1 + TechSum(u.owner, "morale_loss"));
            if (o == u.owner || (o >= 0 && Allied(o, u.owner))) u.morale = std::min(100.0f, u.morale + B().moraleHome);
            else u.morale = std::max(0.0f, u.morale - loss * (o >= 0 && Enemies(o, u.owner) ? B().moraleEnemy : 1));
        }
    }
    units.erase(std::remove_if(units.begin(), units.end(), [](const Unit& u) { return u.dead; }), units.end());
    buildings.erase(std::remove_if(buildings.begin(), buildings.end(), [](const Building& b) { return b.dead; }), buildings.end());
    StepSites();
    StepWeather();
    if (++fogTick >= 10) { fogTick = 0; UpdateFog(); }
    if (++scoreTick >= 20) { scoreTick = 0; Score(); Victory(); }
}

}  // namespace fa
