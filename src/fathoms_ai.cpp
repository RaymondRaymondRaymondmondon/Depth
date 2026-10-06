// Fathoms' AI (doc pp. 35-36): a few managers that each run a few times a second and issue the same commands a player
// would (Apply validates them). Economy (workers to the doc's targets, gatherers balanced, Cottages before the cap,
// drop sites, farms), expansion (a Transport carries two workers to the best unclaimed island to raise a Lighthouse),
// military (a counter to what it has seen; waves by sea and by Transport landings; defence), neutral (tribes: tribute,
// Kinship or ignore; pirates: hire against the leader, counter-bid), and faction rules (Islanders' Kinship, Atlanteans'
// relics and conversions, Nautilus cards, heroes and ultimates). Difficulty: reaction delay, era timing, attack timing,
// and the Admiral's 10% gather bonus (GatherRate), from the data file.
#include "fathoms.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace fa {

namespace {
struct Mem {
    float thinkT = 0, lastT = -1; uint32_t seed = 0;
    // expansion: a transport carrying workers to an island
    int expTransport = -1, expIsland = -1, expStage = 0; float expT = 0; std::vector<int> expWorkers; bool resettle = false;
    // the army: an invasion transport, the last wave
    int invTransport = -1, invStage = 0; float invT = 0, lastWave = -1e9f; int waves = 0, waveEra = -1; std::vector<int> invUnits;
    int seen[6] = {};   // enemy units seen by class: 0 ranged, 1 melee infantry, 2 mounted, 3 siege, 4 light ships, 5 heavy ships
};
std::map<std::pair<const World*, int>, Mem> gMem;

int Count(const World& w, int p, const char* key) { int d = B().Unit(key), n = 0; for (const auto& u : w.units) if (!u.dead && u.owner == p && u.def == d) n++; for (const auto& b : w.buildings) if (!b.dead && b.owner == p) for (const auto& q : b.queue) if (q.kind == 0 && q.def == d) n++; return n; }
int CountB(const World& w, int p, const char* key, bool done = false) { int d = B().Building(key), n = 0; for (const auto& b : w.buildings) if (!b.dead && b.owner == p && b.def == d && (!done || b.progress >= 1)) n++; return n; }
const Building* FindB(const World& w, int p, const char* key, bool done = true) { int d = B().Building(key); for (const auto& b : w.buildings) if (!b.dead && b.owner == p && b.def == d && (!done || b.progress >= 1)) return &b; return nullptr; }
bool Afford(const Player& P, const Cost& c, float keep = 0) { for (int r = 0; r < R_COUNT; r++) if (P.res[r] < c[r] + (c[r] > 0 ? keep : 0)) return false; return true; }
int IsleOf(const World& w, Vector2 p) { int x = (int)p.x, y = (int)p.y; return w.In(x, y) ? w.isle[w.Idx(x, y)] : -1; }
bool Ship(const World& w, const Unit& u) { return w.UD(u).tags & TG_SHIP; }
bool Worker(const World& w, const Unit& u) { return (w.UD(u).tags & TG_WORKER) && !(w.UD(u).tags & TG_SHIP); }
bool Military(const World& w, const Unit& u) { const UnitDef& d = w.UD(u); return d.attack > 0 && !(d.tags & TG_WORKER) && !(d.tags & TG_ULTIMATE) && d.key != "tribal_warrior"; }
Vector2 HomeC(const World& w, int p) { const Building* h = FindB(w, p, "harbor", false); if (h) return h->Centre(); for (const auto& b : w.buildings) if (!b.dead && b.owner == p) return b.Centre(); for (const auto& u : w.units) if (!u.dead && u.owner == p) return u.p; return {w.W * 0.5f, w.H * 0.5f}; }

// a place for a building near a point, on one island, leaving a tile of room round it (walls excepted)
bool Spot(const World& w, int p, int def, Vector2 near, int island, int& ox, int& oy, float maxR = 16, uint32_t salt = 0) {
    const BuildingDef& d = B().buildings[def]; int s = d.size;
    for (int r = 0; r <= (int)maxR; r++) {
        int n = std::max(1, r * 8);
        for (int i = 0; i < n; i++) {
            int k = (i + (int)(salt % 7)) % n; float an = k * 6.2832f / n; int x = (int)(near.x + cosf(an) * r) - s / 2, y = (int)(near.y + sinf(an) * r) - s / 2;
            if (island >= 0) { int cx = x + s / 2, cy = y + s / 2; if (!w.In(cx, cy) || w.isle[w.Idx(cx, cy)] != island) continue; }
            if (!w.CanPlace(p, def, x, y)) continue;
            bool room = true;
            if (!d.wall) for (int yy = y - 1; yy <= y + s && room; yy++) for (int xx = x - 1; xx <= x + s && room; xx++) if (w.In(xx, yy) && w.occ[w.Idx(xx, yy)] >= 0) room = false;
            if (!room) continue;
            ox = x; oy = y; return true;
        }
    }
    return false;
}
void Build(World& w, int p, const char* key, Vector2 near, int island, const std::vector<int>& who, std::vector<Command>& out, float maxR = 16) {
    int def = B().Building(key); if (def < 0 || who.empty()) return; int x, y;
    if (!Spot(w, p, def, near, island, x, y, maxR, (uint32_t)(w.t * 3))) return;
    Command c; c.kind = C_BUILD; c.player = p; c.def = def; c.x = x; c.y = y; c.units = who; out.push_back(c);
}
void Train(World& w, int p, const Building* b, const char* key, std::vector<Command>& out) {
    if (!b) return; int def = B().Unit(key); if (def < 0) return; Command c; c.kind = C_TRAIN; c.player = p; c.target = b->id; c.def = def; out.push_back(c);
}
int Power(const World& w, int p, bool ships) { float s = 0; for (const auto& u : w.units) if (!u.dead && u.owner == p && Military(w, u) && Ship(w, u) == ships) s += w.UD(u).hp * w.UD(u).attack / std::max(0.5f, w.UD(u).reload) * 0.05f; return (int)s; }
}

void AiThink(World& w, int p, std::vector<Command>& out) {
    const Balance& Bl = B(); Player& P = w.players[p]; if (!P.alive) return;
    static const bool aiTrace = getenv("DEPTH_AITRACE") != nullptr;
    Mem& M = gMem[{&w, p}];
    if (M.lastT > w.t + 1 || M.seed != w.set.seed) { M = Mem(); M.seed = w.set.seed; }
    M.lastT = w.t;
    const Balance::Ai& A = Bl.ai[std::clamp(P.difficulty, 0, (int)Bl.ai.size() - 1)];
    if (w.t < M.thinkT) return;
    M.thinkT = w.t + std::max(0.5f, A.reaction);
    Vector2 home = HomeC(w, p); int homeIsle = IsleOf(w, home);
    const Building* harbor = FindB(w, p, "harbor"); const Building* dock = FindB(w, p, "dock"); if (!dock) dock = harbor;
    const Building* barracks = FindB(w, p, "barracks"); const Building* stable = FindB(w, p, "stable");
    const FactionDef& F = Bl.factions[P.faction]; int era = P.era;
    float t = w.t;
    // saving for the next era: from 80% of its time, only Cottages and workers until it's paid for
    float eraWhen = era == 0 ? A.steamAt : A.steamAt * 1.9f;
    Cost eraFB = Bl.eraCost[std::min(era + 1, 2)]; eraFB[R_ICHOR] = 0;   // (Ichor comes by its own road: the black market, ruins, vents)
    bool saving = era < 2 && P.eraBuilding < 0 && t >= eraWhen * 0.8f && !Afford(P, eraFB);
    // the next building the era needs comes first: nothing else spends Brass until it's placed
    const char* keyB = nullptr; if (era == 0) { if (t >= 20 && !FindB(w, p, "farmstead", false)) keyB = "farmstead"; else if (t >= 40 && !FindB(w, p, "mine_shed", false)) keyB = "mine_shed"; else if (t >= 110 && !FindB(w, p, "barracks", false)) keyB = "barracks"; else if (t >= 150 && !FindB(w, p, "dock", false)) keyB = "dock"; }
    else if (era == 1) { if (!FindB(w, p, "workshop", false)) keyB = "workshop"; else if (!FindB(w, p, "exchange", false)) keyB = "exchange"; }
    bool hold = keyB && !Afford(P, Bl.buildings[Bl.Building(keyB)].cost);

    // ---- Nemo's Logbook: take a card (ships if the enemy fields ships, else the economy)
    if (!P.cardOffer.empty()) { int best = 0, bs = -1; for (int i = 0; i < (int)P.cardOffer.size(); i++) { int c = P.cardOffer[i]; int s = c == 7 ? 6 : c == 3 ? 5 : c == 0 ? 4 + M.seen[4] + M.seen[5] : c == 4 ? 4 : c == 6 ? 3 : 2; if (s > bs) { bs = s; best = i; } }
        Command c; c.kind = C_CARD; c.player = p; c.a = best; out.push_back(c); }

    // ---- what the enemy has been seen to field (the counter table's input)
    for (const auto& u : w.units) if (!u.dead && IsPlayer(u.owner) && w.Enemies(p, u.owner) && w.Sees(p, u.p)) {
        const UnitDef& d = w.UD(u); int k = (d.tags & TG_SHIP) ? ((d.tags & TG_HEAVY) ? 5 : 4) : (d.tags & TG_SIEGE) ? 3 : (d.tags & TG_MOUNTED) ? 2 : (d.tags & TG_RANGED) ? 0 : 1;
        if (d.attack > 0 && !(d.tags & TG_WORKER)) M.seen[k] = std::min(60, M.seen[k] + 1);
    }

    // ---- eras
    if (harbor && P.eraBuilding < 0 && era < 2) {
        if (t >= eraWhen * 0.8f && Afford(P, Bl.eraCost[era + 1])) { Command c; c.kind = C_ERA; c.player = p; c.target = harbor->id; out.push_back(c); }
    }
    // ---- economy: workers and boats
    std::vector<Unit*> workers, idleW, boats, idleB, army, armyShips;
    for (auto& u : w.units) if (!u.dead && u.owner == p && u.inside < 0 && u.garrisoned < 0) {
        if (Worker(w, u)) { workers.push_back(&u); if (u.order == O_IDLE) idleW.push_back(&u); }
        else if (w.UD(u).tags & TG_WORKER) { boats.push_back(&u); if (u.order == O_IDLE) idleB.push_back(&u); }
        else if (Military(w, u)) { if (Ship(w, u)) armyShips.push_back(&u); else army.push_back(&u); }
    }
    int workerTarget = t < 360 ? 8 + (int)(t / 360 * 12) : t < 720 ? 20 + (int)((t - 360) / 360 * 12) : std::min(45, 32 + (int)((t - 720) / 360 * 13));
    int nW = Count(w, p, "worker");
    bool popRoom = P.pop + 2 <= w.PopCap(p);
    bool foodShort = saving && P.res[R_FOOD] < eraFB[R_FOOD] && nW >= 18;   // (the era first: workers wait a moment)
    if (harbor && nW < workerTarget && popRoom && harbor->queue.size() < 2 && P.res[R_FOOD] >= 50 && !foodShort) Train(w, p, harbor, "worker", out);
    if (dock && !hold && !saving && Count(w, p, "fishing_boat") < (t < 240 ? 3 : 6) && popRoom && dock->queue.size() < 2 && P.res[R_BRASS] >= 90) Train(w, p, dock, "fishing_boat", out);
    // gatherers balanced against a split that shifts as the game goes on
    float want[R_COUNT] = {0.42f, 0.44f, 0.14f, 0, 0}; if (saving) { want[0] = 0.55f; want[1] = 0.35f; want[2] = 0.1f; } if (era >= 1) { want[0] = 0.42f; want[1] = 0.3f; want[2] = 0.18f; want[3] = 0.1f; }
    if (P.faction == 5) { want[0] -= 0.1f; want[2] += 0.1f; }
    int on[R_COUNT] = {};
    for (Unit* u : workers) if (u->order == O_GATHER || u->order == O_RETURN) { int r = u->target >= 0 && u->target < (int)w.nodes.size() ? Bl.nodes[w.nodes[u->target].kind].res : u->carryRes; if (r >= 0) on[r]++; }
    for (Unit* u : idleW) {
        if (M.expStage > 0 && std::find(M.expWorkers.begin(), M.expWorkers.end(), u->id) != M.expWorkers.end()) continue;
        int isl = IsleOf(w, u->p); int need = 0; float gap = -1e9f;
        for (int r = 0; r < 4; r++) { float g = want[r] * (int)workers.size() - on[r] - P.res[r] / (r == R_FOOD ? 120.0f : 200.0f); if (r == R_ICHOR && era < 1) continue; if (g > gap) { gap = g; need = r; } }
        int best = -1; float bd = 1e9f;
        for (int pass = 0; pass < 2 && best < 0; pass++)
            for (size_t i = 0; i < w.nodes.size(); i++) { const Node& n = w.nodes[i]; if (n.amount <= 0 || Bl.nodes[n.kind].water) continue; if (pass == 0 && Bl.nodes[n.kind].res != need) continue; if (IsleOf(w, n.p) != isl) continue;
                if (n.kind == N_FARM) { const Building* fb = w.Bd(n.farm); if (!fb || fb->owner != p || fb->progress < 1) continue; bool used = false; for (Unit* o : workers) if (o->target == (int)i && (o->order == O_GATHER || o->order == O_RETURN)) used = true; if (used) continue; }
                float d = Vector2Distance(n.p, u->p); if (d < bd) { bd = d; best = (int)i; } }
        if (best >= 0) { Command c; c.kind = C_GATHER; c.player = p; c.units = {u->id}; c.target = best; out.push_back(c); on[Bl.nodes[w.nodes[best].kind].res]++; }
    }
    for (Unit* u : idleB) {
        int best = -1; float bd = 1e9f;
        for (size_t i = 0; i < w.nodes.size(); i++) { const Node& n = w.nodes[i]; if (n.amount <= 0 || !Bl.nodes[n.kind].water) continue; float d = Vector2Distance(n.p, home) + Vector2Distance(n.p, u->p) * 0.3f; if (n.kind == N_WRECK && n.trapped) d += 30; if (d < bd) { bd = d; best = (int)i; } }
        if (best >= 0) { Command c; c.kind = C_GATHER; c.player = p; c.units = {u->id}; c.target = best; out.push_back(c); }
    }
    // builders: whoever is nearest and not on the expedition
    auto builders = [&](Vector2 at, int n, int isl) { std::vector<std::pair<float, int>> c; for (Unit* u : workers) { if (std::find(M.expWorkers.begin(), M.expWorkers.end(), u->id) != M.expWorkers.end()) continue; if (u->order == O_BUILD) continue; if (isl >= 0 && IsleOf(w, u->p) != isl) continue; c.push_back({Vector2Distance(u->p, at), u->id}); }
        std::sort(c.begin(), c.end()); std::vector<int> r; for (int i = 0; i < n && i < (int)c.size(); i++) r.push_back(c[i].second); return r; };
    int building = 0; for (const auto& b : w.buildings) if (!b.dead && b.owner == p && b.progress < 1) building++;
    // finish any unbuilt site nobody is working on
    for (const auto& b : w.buildings) if (!b.dead && b.owner == p && b.progress < 1) { bool someone = false; for (Unit* u : workers) if (u->order == O_BUILD && u->target == b.id) someone = true;
        if (!someone) { auto who = builders(b.Centre(), 2, b.island); if (!who.empty()) { Command c; c.kind = C_REPAIR; c.player = p; c.target = b.id; c.units = who; out.push_back(c); } } }
    if (building < 2 && harbor && (!saving || P.pop + 5 >= w.PopCap(p))) {
        int cap = w.PopCap(p);
        auto want1 = [&](const char* key, float after, int n = 1) { return t >= after && CountB(w, p, key) < n && Afford(P, Bl.buildings[Bl.Building(key)].cost); };
        bool barracksDue = !saving && t >= 70 && CountB(w, p, "barracks") == 0, dockDue = !saving && t >= 100 && !FindB(w, p, "dock", false);
        if (P.pop + ((barracksDue || dockDue) && P.pop + 2 < cap ? 2 : 5) >= cap && cap < (int)(w.set.players <= 2 ? Bl.cap2 : w.set.players <= 4 ? Bl.cap4 : Bl.cap6) && Afford(P, Bl.buildings[Bl.Building("cottage")].cost)) Build(w, p, "cottage", home, homeIsle, builders(home, 1, homeIsle), out, 24);
        else if (saving) {}
        else if (keyB && std::string(keyB) != "farmstead" && std::string(keyB) != "mine_shed") { if (!hold) Build(w, p, keyB, home, homeIsle, builders(home, 2, homeIsle), out, 18); }
        else if (want1("barracks", 70)) Build(w, p, "barracks", home, homeIsle, builders(home, 2, homeIsle), out);
        else if (want1("dock", 100) && !FindB(w, p, "dock", false)) Build(w, p, "dock", home, homeIsle, builders(home, 2, homeIsle), out, 18);
        else if (want1("farmstead", 20)) { // near the groves
            Vector2 g = home; float bd = 1e9f; for (const auto& n : w.nodes) if (n.kind == N_GROVE && n.amount > 0 && IsleOf(w, n.p) == homeIsle) { float d = Vector2Distance(n.p, home); if (d < bd && d > 5) { bd = d; g = n.p; } }
            Build(w, p, "farmstead", g, homeIsle, builders(g, 1, homeIsle), out, 8); }
        else if (want1("mine_shed", 40)) {
            Vector2 g = home; float bd = 1e9f; for (const auto& n : w.nodes) if ((n.kind == N_BRASS || n.kind == N_COAL) && n.amount > 0 && IsleOf(w, n.p) == homeIsle) { float d = Vector2Distance(n.p, home); if (d < bd && d > 5) { bd = d; g = n.p; } }
            Build(w, p, "mine_shed", g, homeIsle, builders(g, 1, homeIsle), out, 8); }
        else if ((era >= 1 || P.res[R_BRASS] > 300) && t > 200 && homeIsle >= 0 && std::none_of(w.buildings.begin(), w.buildings.end(), [&](const Building& b) { return !b.dead && b.island == homeIsle && w.BD(b).key == "lighthouse"; }) && Afford(P, Bl.buildings[Bl.Building("lighthouse")].cost, 30)) {   // (a Lighthouse at home widens the ground to build on)
            Vector2 far = Vector2Add(w.islands[homeIsle].c, Vector2Scale(Vector2Subtract(w.islands[homeIsle].c, home), 0.6f)); Build(w, p, "lighthouse", far, homeIsle, builders(far, 1, homeIsle), out, 10); }
        else if (P.faction == 1 && want1("totem", 240)) Build(w, p, "totem", home, homeIsle, builders(home, 1, homeIsle), out);
        else if (P.faction == 2 && t > 200 && CountB(w, p, "brood_pool") < std::min(10, 2 + (int)(t / 120)) && Afford(P, Bl.buildings[Bl.Building("brood_pool")].cost, 60)) Build(w, p, "brood_pool", home, homeIsle, builders(home, 1, homeIsle), out);
        else if (want1("stable", 420)) Build(w, p, "stable", home, homeIsle, builders(home, 2, homeIsle), out);
        else if (era >= 1 && want1("workshop", 0)) Build(w, p, "workshop", home, homeIsle, builders(home, 2, homeIsle), out);
        else if (era >= 1 && want1("siege_works", t > A.steamAt + 120 ? 0 : 1e9f)) Build(w, p, "siege_works", home, homeIsle, builders(home, 2, homeIsle), out);
        else if (era >= 1 && want1("exchange", A.steamAt + 60)) Build(w, p, "exchange", home, homeIsle, builders(home, 2, homeIsle), out);
        else if (era >= 1 && (P.faction == 4 || P.difficulty >= 2) && want1("chapel", A.steamAt + 30)) Build(w, p, "chapel", home, homeIsle, builders(home, 2, homeIsle), out);
        else if (era >= 1 && want1("coastal_battery", A.steamAt + 200)) Build(w, p, "coastal_battery", Vector2Lerp(home, {w.W * 0.5f, w.H * 0.5f}, 0.08f), homeIsle, builders(home, 2, homeIsle), out, 14);
        else if (era >= 2 && want1("airship_hangar", 0)) Build(w, p, "airship_hangar", home, homeIsle, builders(home, 2, homeIsle), out);
        else if (era >= 2 && P.difficulty >= 2 && want1("wonder", 0) && P.res[R_BRASS] > 1400) Build(w, p, "wonder", home, homeIsle, builders(home, 4, homeIsle), out, 20);
        else if (t > 300 && P.faction != 2) {   // farms once the groves thin out
            int groves = 0; for (const auto& n : w.nodes) if (n.kind == N_GROVE && n.amount > 50 && IsleOf(w, n.p) == homeIsle) groves++;
            int farms = CountB(w, p, "farm"); if (groves < 3 && farms < 4 + era * 3 && Afford(P, Bl.buildings[Bl.Building("farm")].cost, 40)) { const Building* fs = FindB(w, p, "farmstead"); Vector2 at = fs ? fs->Centre() : home; Build(w, p, "farm", at, homeIsle, builders(at, 1, homeIsle), out, 10); }
        }
    }
    // ---- the late economy: farms when the groves thin, idle hands moved to islands with work, surplus traded
    {
        int groves = 0; for (const auto& n : w.nodes) if (n.kind == N_GROVE && n.amount > 40 && IsleOf(w, n.p) == homeIsle) groves++;
        int farms = CountB(w, p, P.faction == 2 ? "brood_pool" : "farm"), unbuilt = 0; for (const auto& b : w.buildings) if (!b.dead && b.owner == p && b.progress < 1 && (w.BD(b).key == "farm" || w.BD(b).key == "brood_pool")) unbuilt++;
        int wantFarms = groves >= 4 ? 0 : groves >= 2 ? 3 : 6 + era * 3; if (P.faction == 2) wantFarms = std::min(10, wantFarms + 2);
        if (t > 240 && farms < wantFarms && unbuilt == 0 && !hold && FindB(w, p, "farmstead")) {
            const char* fk = P.faction == 2 ? "brood_pool" : "farm"; const Building* fs = FindB(w, p, "farmstead");
            if (Afford(P, Bl.buildings[Bl.Building(fk)].cost)) { std::vector<int> who; for (Unit* u : idleW) if ((int)who.size() < 1 && IsleOf(w, u->p) == IsleOf(w, fs->Centre())) who.push_back(u->id); if (who.empty()) for (Unit* u : workers) if (who.empty() && u->order == O_GATHER && IsleOf(w, u->p) == IsleOf(w, fs->Centre())) who.push_back(u->id); Build(w, p, fk, fs->Centre(), IsleOf(w, fs->Centre()), who, out, 12); }
        }
        // idle workers with nothing to do on their island go where the work is (walking over shallows, or by Transport)
        int idleHome = 0; for (Unit* u : idleW) if (IsleOf(w, u->p) == homeIsle) idleHome++;
        if (idleHome >= 3 && M.expStage == 0) {
            int best = -1; float bv = 0;
            for (size_t i = 0; i < w.islands.size(); i++) { if (w.islands[i].owner != p || (int)i == homeIsle) continue; float v = 0; for (const auto& n : w.nodes) if (n.amount > 0 && !Bl.nodes[n.kind].water && IsleOf(w, n.p) == (int)i) v += n.amount; if (v > bv) { bv = v; best = (int)i; } }
            if (best >= 0 && bv > 300) {
                std::vector<int> go; for (Unit* u : idleW) if (IsleOf(w, u->p) == homeIsle && go.size() < 8) go.push_back(u->id);
                std::vector<int> path; const Unit* u0 = w.U(go[0]); w.FindPath(*u0, w.islands[best].c, path, 2.0f);
                if (!path.empty() && w.isle[path.back()] == best) { Command c; c.kind = C_MOVE; c.player = p; c.units = go; c.at = w.islands[best].c; out.push_back(c); }
                else { M.expIsland = best; M.expStage = 1; M.expT = t; M.expWorkers = go; M.expTransport = -1; for (auto& q : w.units) if (!q.dead && q.owner == p && w.UD(q).key == "transport" && q.cargo.empty() && q.id != M.invTransport) M.expTransport = q.id; if (M.expTransport < 0 && dock) Train(w, p, dock, "transport", out); M.resettle = true; }
            }
        }
        // a drop site on any owned island with work and none of its own
        for (size_t i = 0; i < w.islands.size(); i++) {
            if (w.islands[i].owner != p || (int)i == homeIsle) continue; bool drop = false; for (const auto& b : w.buildings) if (!b.dead && b.owner == p && b.island == (int)i && w.BD(b).drop != "") drop = true; if (drop) continue;
            std::vector<int> there; for (Unit* u : workers) if (IsleOf(w, u->p) == (int)i && u->order != O_BUILD) there.push_back(u->id); if (there.empty()) continue;
            Vector2 g = w.islands[i].c; bool mine = false; for (const auto& n : w.nodes) if (IsleOf(w, n.p) == (int)i && n.amount > 0 && !Bl.nodes[n.kind].water) { g = n.p; mine = n.kind == N_BRASS || n.kind == N_COAL || n.kind == N_VENT; break; }
            const char* dk = mine ? "mine_shed" : "farmstead"; if (Afford(P, Bl.buildings[Bl.Building(dk)].cost)) { there.resize(std::min<size_t>(there.size(), 2)); Build(w, p, dk, g, (int)i, there, out, 8); } break;
        }
        // the Exchange: sell what piles up for what runs short
        if (FindB(w, p, "exchange")) { int hi = -1, lo = -1; float hv = 700, lv = 1e9f; for (int r = 0; r < 3; r++) { float v = P.res[r] * (r == R_COAL ? 0.8f : 1.0f); if (v > hv) { hv = v; hi = r; } if (P.res[r] < lv) { lv = P.res[r]; lo = r; } } if (hi >= 0 && lo >= 0 && hi != lo && lv < 250) { Command c; c.kind = C_EXCHANGE; c.player = p; c.a = hi; c.b = lo; out.push_back(c); } }
    }
    // ---- research: one affordable tech at a time per building, keeping a reserve
    if (t > 240 && !saving && !hold && (era >= 1 || P.res[R_BRASS] > 260)) for (size_t i = 0; i < Bl.techs.size(); i++) {
        const TechDef& td = Bl.techs[i]; if (P.tech[i] || td.era > era || (td.faction >= 0 && td.faction != P.faction)) continue;
        if (td.forgeLine >= 0 && P.forge[td.forgeLine] != td.forgeLevel - 1) continue;
        if (!Afford(P, td.cost, 150)) continue; const Building* at = FindB(w, p, td.faction >= 0 ? "harbor" : td.at.c_str()); if (!at || !at->queue.empty()) continue;
        Command c; c.kind = C_RESEARCH; c.player = p; c.target = at->id; c.def = (int)i; out.push_back(c); break;
    }
    // ---- expansion: a Transport carries two workers to the best unclaimed island; they raise a Lighthouse and a drop site
    if (M.expStage == 0 && t > 150 && dock && (!hold || P.res[R_BRASS] > 140) && !saving) {
        int owned = 0; for (const auto& is : w.islands) if (is.owner == p) owned++;
        if (owned < 2 + era * 2) {
            int best = -1; float bs = -1e9f;
            for (size_t i = 0; i < w.islands.size(); i++) { const Island& is = w.islands[i]; if (is.owner >= 0 || is.kind == I_HOME || is.kind == I_TRIBAL || is.kind == I_COVE || is.kind == I_REEF || (int)is.tiles.size() < 20) continue;
                if (is.kind == I_VOLCANO && era < 2) continue; if (is.kind == I_RUIN && era < 1) continue;
                float brassLeft = 0; for (const auto& n : w.nodes) if (n.kind == N_BRASS && IsleOf(w, n.p) == homeIsle) brassLeft += n.amount;
                int coalOwned = 0; for (const auto& o : w.islands) if (o.owner == p && o.kind == I_COAL) coalOwned++;
                float val = is.kind == I_COAL ? (coalOwned ? 20 : 60) : is.kind == I_MINING ? (brassLeft < 2500 ? 90 : 55) : is.kind == I_FERTILE ? 45 : is.kind == I_RUIN ? 40 : 25;
                float d = Vector2Distance(is.c, home); float danger = 0; for (const auto& u : w.units) if (!u.dead && w.Enemies(p, u.owner) && Vector2Distance(u.p, is.c) < is.r + 4 && w.UD(u).attack > 0) danger += 10;
                float s = val - d * 0.6f - danger; if (s > bs) { bs = s; best = (int)i; } }
            if (best >= 0) {
                M.expIsland = best; M.expStage = 1; M.expT = t; M.expWorkers.clear();
                // joined by shallows? two workers simply walk over
                { std::vector<int> walkers; for (Unit* u : workers) { if ((int)walkers.size() >= 2) break; if (IsleOf(w, u->p) == homeIsle && u->order != O_BUILD) walkers.push_back(u->id); }
                  if (!walkers.empty()) { std::vector<int> path; const Unit* u0 = w.U(walkers[0]); w.FindPath(*u0, w.islands[best].c, path, 2.0f);
                    if (!path.empty() && w.isle[path.back()] == best) { M.expWorkers = walkers; M.expStage = 3; Command c; c.kind = C_MOVE; c.player = p; c.units = walkers; c.at = w.islands[best].c; out.push_back(c); } } }
                if (M.expStage == 1) {
                Unit* tr = nullptr; for (auto& u : w.units) if (!u.dead && u.owner == p && w.UD(u).key == "transport" && u.cargo.empty() && u.id != M.invTransport) tr = &u;
                if (tr) M.expTransport = tr->id; else { Train(w, p, dock, "transport", out); M.expTransport = -1; }
                }
            }
        }
    }
    static int lastStage[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    if (aiTrace && lastStage[p] != M.expStage) { lastStage[p] = M.expStage; std::printf("    [ai %d t %.0f] expansion stage %d island %d transport %d workers %d\n", p, t, M.expStage, M.expIsland, M.expTransport, (int)M.expWorkers.size()); }
    if (M.expStage > 0) {
        Unit* tr = w.U(M.expTransport);
        if (!tr && M.expStage == 1 && dock && dock->queue.empty() && Count(w, p, "transport") == 0) Train(w, p, dock, "transport", out);
        if (!tr && M.expStage >= 1) { for (auto& u : w.units) if (!u.dead && u.owner == p && w.UD(u).key == "transport" && u.cargo.empty() && u.id != M.invTransport) { tr = &u; M.expTransport = u.id; break; } }
        if (M.resettle && M.expStage == 3) { bool ashore = true; for (int id : M.expWorkers) if (const Unit* u = w.U(id)) if (u->inside >= 0) ashore = false; if (ashore) { M.expStage = 0; M.resettle = false; M.expWorkers.clear(); M.expTransport = -1; } }
        if (t - M.expT > 240 || M.expIsland < 0 || (w.islands[M.expIsland].owner >= 0 && !M.resettle)) { M.resettle = false; M.expStage = 0; M.expWorkers.clear(); M.expTransport = -1; }   // (gave up, or done)
        else if (M.expStage == 1 && tr) {
            // two workers board
            if (M.expWorkers.size() < 2 && !M.resettle) for (Unit* u : workers) { if ((int)M.expWorkers.size() >= 2) break; if (IsleOf(w, u->p) == homeIsle && u->order != O_BUILD) M.expWorkers.push_back(u->id); }
            Command c; c.kind = C_BOARD; c.player = p; c.target = tr->id; c.units = M.expWorkers; out.push_back(c); M.expStage = 2;
        } else if (M.expStage == 2 && tr) {
            int aboard = (int)tr->cargo.size();
            if (aboard >= (int)M.expWorkers.size() && aboard > 0) { Command c; c.kind = C_UNLOAD; c.player = p; c.units = {tr->id}; c.at = w.islands[M.expIsland].c; out.push_back(c); M.expStage = 3; }
            else { Command c; c.kind = C_BOARD; c.player = p; c.target = tr->id; for (int id : M.expWorkers) if (const Unit* u = w.U(id)) if (u->inside < 0 && u->order != O_BOARD) c.units.push_back(id); if (!c.units.empty()) out.push_back(c); }
        } else if (M.expStage == 3) {
            std::vector<int> ashore; for (int id : M.expWorkers) if (const Unit* u = w.U(id)) if (u->inside < 0 && IsleOf(w, u->p) == M.expIsland) ashore.push_back(id);
            if (aiTrace && (int)t % 10 == 0) { std::printf("      stage 3: ashore %d afford %d", (int)ashore.size(), (int)Afford(P, Bl.buildings[Bl.Building("lighthouse")].cost)); for (int id : M.expWorkers) if (const Unit* u = w.U(id)) std::printf(" [w%d in %d at %.0f,%.0f isle %d]", id, u->inside, u->p.x, u->p.y, IsleOf(w, u->p)); if (tr) std::printf(" tr at %.0f,%.0f ord %d cargo %d", tr->p.x, tr->p.y, tr->order, (int)tr->cargo.size()); std::printf("\n"); }
            if (!ashore.empty() && Afford(P, Bl.buildings[Bl.Building("lighthouse")].cost)) {
                const Island& is = w.islands[M.expIsland]; Build(w, p, "lighthouse", is.c, M.expIsland, ashore, out, is.r + 2);
                if (!out.empty() && out.back().kind == C_BUILD) M.expStage = 4;
            }
        } else if (M.expStage == 4) {
            // once it stands, a drop site by the island's nodes; then the workers gather there
            const Island& is = w.islands[M.expIsland]; (void)is;
            std::vector<int> ashore; for (int id : M.expWorkers) if (const Unit* u = w.U(id)) if (u->inside < 0 && IsleOf(w, u->p) == M.expIsland && u->order == O_IDLE) ashore.push_back(id);
            if (w.islands[M.expIsland].owner == p && !ashore.empty()) {
                bool mine = false; Vector2 g = w.islands[M.expIsland].c; for (const auto& n : w.nodes) if (IsleOf(w, n.p) == M.expIsland && n.amount > 0) { mine = n.kind == N_BRASS || n.kind == N_COAL || n.kind == N_VENT; g = n.p; break; }
                Build(w, p, mine ? "mine_shed" : "farmstead", g, M.expIsland, ashore, out, 8);
                M.expStage = 0; M.expWorkers.clear(); M.expTransport = -1;
            }
        }
    }
    // ---- relics and Devotion (everyone picks up relics; the Atlanteans prize them)
    for (auto& u : w.units) if (!u.dead && u.owner == p && u.relic > 0 && u.order != O_GARRISON) {
        const Building* to = FindB(w, p, "chapel"); if (!to || IsleOf(w, to->Centre()) != IsleOf(w, u.p)) to = harbor;
        if (to) { Command c; c.kind = C_GARRISON; c.player = p; c.target = to->id; c.units = {u.id}; out.push_back(c); }
    }
    // ---- military: train what counters what we've seen
    int armyTarget = (int)(4 + t / 60 * (1.2f + 0.3f * P.difficulty)); if (t < A.attackAfter) armyTarget = std::min(armyTarget, 14);
    bool roomArmy = P.pop + 3 <= w.PopCap(p) && ((nW >= std::min(workerTarget, 12) && (!saving || t > eraWhen * 1.3f) && !hold) || ((int)army.size() < 4 && t > 220));
    if (roomArmy && (int)army.size() < armyTarget && barracks && barracks->queue.size() < 2) {
        const char* pick = "rifleman";
        if (M.seen[2] > M.seen[0] + 2) pick = "harpooner"; else if (M.seen[1] > M.seen[0] + 3 && era >= 1) pick = "bell_guard";
        else if (((int)army.size() % 3) == 1) pick = "harpooner";
        for (int ui : F.uniques) { const UnitDef& ud = Bl.units[ui]; if (ud.from == "barracks" && ud.era <= era && ud.attack > 0 && (int)army.size() % 2 == 0) pick = ud.key.c_str(); }
        if (P.faction == 5 && era >= 1 && (int)army.size() % 3 == 0) pick = "rivet_golem";
        if (P.faction == 5 && pick == std::string("medic")) pick = "rifleman";
        if (era >= 1 && (int)army.size() % 6 == 5 && P.faction != 5) pick = "medic";
        if (P.faction == 1 && Count(w, p, "shaman") < 1) pick = "shaman";
        if (P.faction == 4 && era >= 1 && Count(w, p, "deep_priest") < 2) { const Building* ch = FindB(w, p, "chapel"); if (ch) Train(w, p, ch, "deep_priest", out); }
        Train(w, p, barracks, pick, out);
    }
    if (roomArmy && stable && era >= 1 && stable->queue.empty() && (M.seen[0] + M.seen[3] > M.seen[1] || (int)army.size() % 4 == 0)) Train(w, p, stable, P.faction == 3 ? "eel_rider" : "lancer", out);
    if (roomArmy && era >= 1 && (int)army.size() > 8) { const Building* sw = FindB(w, p, "siege_works"); if (sw && sw->queue.empty() && Count(w, p, "mortar") + Count(w, p, "tesla_walker") < 2 + era) Train(w, p, sw, P.faction == 5 ? "tesla_walker" : "mortar", out); }
    if (roomArmy && era >= 2) { const Building* hg = FindB(w, p, "airship_hangar"); if (hg && hg->queue.empty() && Count(w, p, "airship") < 3) Train(w, p, hg, "airship", out); }
    const Building* realDock = FindB(w, p, "dock");
    int navyTarget = era == 0 ? 2 : era == 1 ? 5 : 7;
    if (roomArmy && realDock && (int)armyShips.size() < navyTarget && realDock->queue.size() < 2) {
        const char* s = "sloop";
        if (era >= 1) { s = M.seen[5] > M.seen[4] ? "torpedo_boat" : ((int)armyShips.size() % 3 == 2 ? "ironclad" : "gunboat"); if (P.faction == 2 && (std::string(s) == "torpedo_boat" || std::string(s) == "ironclad")) s = "ghost_worm"; }
        if (P.faction == 1 && era == 0) s = "war_canoe";
        if (P.faction == 0 && era >= 1 && (int)armyShips.size() % 2 == 1) s = "submarine";
        Train(w, p, realDock, s, out);
    }
    // the hero
    if (era >= 1 && harbor && P.heroUnit < 0 && Afford(P, Bl.units[Bl.Unit("hero")].cost, 50) && harbor->queue.size() < 2 && (P.heroDeadT < 0 || t - P.heroDeadT > 60)) Train(w, p, harbor, "hero", out);
    if (Unit* h = w.U(P.heroUnit)) if (h->abilityT <= 0) {
        // use the active when enemies are close: the densest spot within 8
        Vector2 at{}; int bestN = 0, tgt = -1;
        for (const auto& o : w.units) if (!o.dead && w.Enemies(p, o.owner) && o.inside < 0 && Vector2Distance(o.p, h->p) < 8) { int n = 0; for (const auto& q : w.units) if (!q.dead && w.Enemies(p, q.owner) && Vector2Distance(q.p, o.p) < 3) n++; if (n > bestN) { bestN = n; at = o.p; tgt = o.id; } }
        if (bestN >= 2 || (P.faction == 2 && tgt >= 0)) { Command c; c.kind = C_ABILITY; c.player = p; c.units = {h->id}; c.at = at; c.target = tgt; if (P.faction == 4) c.at = Vector2Lerp(h->p, at, 1.5f); out.push_back(c); }
    }
    // the ultimate, when the fighting is on
    if (era >= 2 && P.ultimateCD <= 0 && P.ultimateUnit < 0 && P.res[R_ICHOR] >= (P.faction == 4 ? 150 : 300) && (P.faction != 4 || P.devotion >= 600) && t - M.lastWave < 60) { Command c; c.kind = C_ULTIMATE; c.player = p; out.push_back(c); }
    // Deep Priests convert what comes close
    for (auto& u : w.units) if (!u.dead && u.owner == p && w.UD(u).special == "convert" && u.order == O_IDLE && u.abilityT <= 0 && P.devotion >= 30) {
        for (const auto& o : w.units) if (!o.dead && w.Enemies(p, o.owner) && IsPlayer(o.owner) && Vector2Distance(o.p, u.p) < 9 && !(w.UD(o).tags & (TG_HERO | TG_ULTIMATE | TG_SHIP))) { Command c; c.kind = C_CONVERT; c.player = p; c.units = {u.id}; c.target = o.id; out.push_back(c); break; }
    }
    // ---- defence: anything attacking our buildings draws the idle army (on its island) and the fleet
    const Building* hit = nullptr; for (const auto& b : w.buildings) if (!b.dead && b.owner == p && t - b.attackedT < 8) { hit = &b; break; }
    if (hit && harbor && (int)army.size() < 3) {   // (no army: the workers near it hide in the Harbor and shoot from it)
        Command g; g.kind = C_GARRISON; g.player = p; g.target = harbor->id; int room = Bl.buildings[harbor->def].garrison - (int)harbor->garrison.size();
        for (Unit* u : workers) { if ((int)g.units.size() >= room) break; if (Vector2Distance(u->p, hit->Centre()) < 16 && u->order != O_GARRISON) g.units.push_back(u->id); }
        if (!g.units.empty()) out.push_back(g);
    }
    if (!hit && harbor && !harbor->garrison.empty()) { bool quiet = true; for (const auto& b : w.buildings) if (!b.dead && b.owner == p && t - b.attackedT < 20) quiet = false; if (quiet) { Command u; u.kind = C_UNGARRISON; u.player = p; u.target = harbor->id; out.push_back(u); } }
    for (const auto& b : w.buildings) if (!b.dead && b.owner == p && b.progress >= 1 && b.hp < Bl.buildings[b.def].hp * 0.7f && t - b.attackedT > 10) {
        bool someone = false; for (Unit* u : workers) if (u->order == O_BUILD && u->target == b.id) someone = true;
        if (!someone) { auto who = builders(b.Centre(), 2, b.island); if (!who.empty()) { Command c; c.kind = C_REPAIR; c.player = p; c.target = b.id; c.units = who; out.push_back(c); } } break; }
    if (hit) {
        Command land; land.kind = C_ATTACK_MOVE; land.player = p; land.at = hit->Centre(); Command sea = land;
        for (Unit* u : army) if ((u->order == O_IDLE || u->order == O_MOVE) && IsleOf(w, u->p) == hit->island) land.units.push_back(u->id);
        for (Unit* u : armyShips) if (u->order == O_IDLE) sea.units.push_back(u->id);
        int k = -1; for (int dy = -6; dy <= 6 && k < 0; dy++) for (int dx = -6; dx <= 6 && k < 0; dx++) { int x = (int)hit->Centre().x + dx, y = (int)hit->Centre().y + dy; if (w.In(x, y) && w.Water(w.tile[w.Idx(x, y)])) k = w.Idx(x, y); }
        if (k >= 0) sea.at = {k % w.W + 0.5f, k / w.W + 0.5f};
        if (!land.units.empty()) out.push_back(land); if (!sea.units.empty() && k >= 0) out.push_back(sea);
    }
    // ---- attacks: Mate one wave per era, Captain and Admiral whenever the army is ready
    int enemy = -1; { float bs = -1; for (const auto& q : w.players) if (q.alive && w.Enemies(p, q.id)) { float s = q.score + 1; if (s > bs) { bs = s; enemy = q.id; } } }
    bool waveOk = t >= std::max(A.attackAfter, 420.0f) && (P.difficulty >= 2 || M.waveEra < era) && t - M.lastWave > 150;
    int roomFor = std::max(6, (int)((w.PopCap(p) - nW) * 0.45f));
    if (enemy >= 0 && waveOk && (int)army.size() + (int)armyShips.size() >= std::min(armyTarget * 0.8f, (float)roomFor)) {
        Vector2 eh = HomeC(w, enemy);
        // the fleet sails at their harbour
        Command sea; sea.kind = C_ATTACK_MOVE; sea.player = p; for (Unit* u : armyShips) sea.units.push_back(u->id);
        int k = -1; for (int r = 2; r < 10 && k < 0; r++) for (int a = 0; a < 16 && k < 0; a++) { int x = (int)(eh.x + cosf(a * 0.39f) * r), y = (int)(eh.y + sinf(a * 0.39f) * r); if (w.In(x, y) && w.Water(w.tile[w.Idx(x, y)])) k = w.Idx(x, y); }
        if (k >= 0 && !sea.units.empty() && t - M.invT > 45) { sea.at = {k % w.W + 0.5f, k / w.W + 0.5f}; out.push_back(sea); M.invT = t; if ((int)armyShips.size() >= 3) { M.lastWave = t; M.waveEra = era; } }
        // the land army boards for a landing
        if (M.invStage == 0 && !army.empty()) {
            Unit* tr = nullptr; for (auto& u : w.units) if (!u.dead && u.owner == p && w.UD(u).key == "transport" && u.cargo.empty() && u.id != M.expTransport) tr = &u;
            bool merfolk = P.faction == 3;
            if (merfolk) { Command c; c.kind = C_ATTACK_MOVE; c.player = p; for (Unit* u : army) c.units.push_back(u->id); c.at = eh; out.push_back(c); M.lastWave = t; M.waves++; M.waveEra = era; }
            else if (tr) { if (aiTrace) std::printf("    [ai %d t %.0f] wave %d: army %d ships %d target %d\n", p, t, M.waves + 1, (int)army.size(), (int)armyShips.size(), enemy); M.invTransport = tr->id; M.invUnits.clear(); for (Unit* u : army) if ((int)M.invUnits.size() < 10 && IsleOf(w, u->p) == homeIsle && u->order == O_IDLE) M.invUnits.push_back(u->id);
                Command c; c.kind = C_BOARD; c.player = p; c.target = tr->id; c.units = M.invUnits; out.push_back(c); M.invStage = 1; M.invT = t; }
            else if (realDock && Count(w, p, "transport") < 2 && realDock->queue.size() < 2) Train(w, p, realDock, "transport", out);
        }
    }
    if (M.invStage > 0) {
        Unit* tr = w.U(M.invTransport);
        if (!tr || t - M.invT > 200) { M.invStage = 0; }
        else if (M.invStage == 1) {
            int aboard = (int)tr->cargo.size(), alive = 0; for (int id : M.invUnits) if (w.U(id)) alive++;
            if (aboard >= alive || (t - M.invT > 40 && aboard > 0)) { Command c; c.kind = C_UNLOAD; c.player = p; c.units = {tr->id}; c.at = HomeC(w, enemy >= 0 ? enemy : p); out.push_back(c); M.invStage = 2; M.lastWave = t; M.waves++; M.waveEra = era; }
            else { Command c; c.kind = C_BOARD; c.player = p; c.target = tr->id; for (int id : M.invUnits) if (const Unit* u = w.U(id)) if (u->inside < 0 && u->order != O_BOARD) c.units.push_back(id); if (!c.units.empty()) out.push_back(c); }
        } else if (M.invStage == 2) {
            if (tr->cargo.empty()) {
                Command c; c.kind = C_ATTACK_MOVE; c.player = p; c.at = HomeC(w, enemy >= 0 ? enemy : p); for (int id : M.invUnits) if (const Unit* u = w.U(id)) if (u->inside < 0) c.units.push_back(id);
                if (!c.units.empty()) out.push_back(c);
                Command back; back.kind = C_MOVE; back.player = p; back.units = {tr->id}; back.at = home; out.push_back(back); M.invStage = 0;
            }
        }
    }
    // ---- neutral powers
    for (int si = 0; si < (int)w.sites.size(); si++) {
        const Site& s = w.sites[si];
        if (s.kind == S_TRIBE && s.hp > 0 && s.state != 3) {
            float d = Vector2Distance(s.p, home);
            if (P.faction == 1 && s.peace[p] != 2 && d < w.W * 0.6f && Afford(P, Bl.kinship, 50)) {   // Kinship
                for (auto& u : w.units) if (!u.dead && u.owner == p && w.UD(u).special == "kinship" && u.order == O_IDLE) {
                    if (IsleOf(w, u.p) != IsleOf(w, s.p)) { // a transport carries the Shaman over when free
                        Unit* tr = w.U(M.expTransport); if (!tr && M.expStage == 0) { for (auto& q : w.units) if (!q.dead && q.owner == p && w.UD(q).key == "transport" && q.cargo.empty() && q.id != M.invTransport) { Command c; c.kind = C_BOARD; c.player = p; c.target = q.id; c.units = {u.id}; out.push_back(c); Command c2; c2.kind = C_UNLOAD; c2.player = p; c2.units = {q.id}; c2.at = s.p; out.push_back(c2); break; } }
                        continue; }
                    Command c; c.kind = C_TRIBE; c.player = p; c.a = si; c.b = 2; c.units = {u.id}; out.push_back(c); break; }
            } else if (s.peace[p] == 0 && P.difficulty >= 1 && t > Bl.tribeWake - 30 && d < w.W * 0.45f && Afford(P, Bl.tribute, 300) && P.faction != 1) {
                Command c; c.kind = C_TRIBE; c.player = p; c.a = si; c.b = 0; out.push_back(c);
            }
        }
        if (s.kind == S_COVE && s.hp > 0 && P.difficulty >= 2 && enemy >= 0 && t > 480) {   // hire a raid on the leader now and then
            int deal = std::min(era, 2); float price = Bl.deals[deal].price * (1 - 0.05f * floorf(s.rep[p] / 25));
            if (P.res[R_DOUB] > price * 1.4f && w.t - s.t2 > Bl.contractEvery) { Command c; c.kind = C_HIRE; c.player = p; c.a = si; c.b = deal; c.target = enemy; out.push_back(c); }
            if (P.res[R_DOUB] < 120 && P.res[R_FOOD] > 900) { Command c; c.kind = C_HIRE; c.player = p; c.a = si; c.b = 4; c.x = R_FOOD; out.push_back(c); }
        }
    }
    // Ichor for the Leviathan Era (and the ultimate): the black market, paid for by selling surplus at the Exchange
    if (era >= 1) {
        float needI = era == 1 ? Bl.eraCost[2][R_ICHOR] : 300; int cove = -1; for (int si = 0; si < (int)w.sites.size(); si++) if (w.sites[si].kind == S_COVE && (w.sites[si].hp > 0 || w.sites[si].owner == p) && w.sites[si].rep[p] >= -50) cove = si;
        if (P.res[R_ICHOR] < needI && cove >= 0) {
            float price = Bl.bmIchor * (1 - 0.05f * floorf(w.sites[cove].rep[p] / 25));
            if (P.res[R_DOUB] >= price) { Command c; c.kind = C_HIRE; c.player = p; c.a = cove; c.b = 5; c.x = R_ICHOR; out.push_back(c); }
            else if (FindB(w, p, "exchange")) { int sell = P.res[R_FOOD] > 500 ? R_FOOD : P.res[R_COAL] > 500 ? R_COAL : P.res[R_BRASS] > 700 ? R_BRASS : -1; if (sell >= 0) { Command c; c.kind = C_EXCHANGE; c.player = p; c.a = sell; c.b = 4; out.push_back(c); } }
        }
    }
    // counter-bids: the Admiral calls off a raid on itself when it can
    for (int i = 0; i < (int)w.contracts.size(); i++) { const Contract& k = w.contracts[i]; if (k.target == p && !k.live && k.warnT > w.t && k.deal >= 0 && k.deal < 100 && P.difficulty >= 3 && P.res[R_DOUB] >= k.price * 1.5f) { Command c; c.kind = C_BID; c.player = p; c.a = i; c.b = P.res[R_DOUB] >= k.price * 2.5f ? 1 : 0; out.push_back(c); } }
    // relics: an army unit walks to a free relic on our island
    for (const auto& s : w.sites) if (s.kind == S_RUIN && s.relic > 0 && IsleOf(w, s.p) >= 0) {
        bool guarded = false; for (const auto& u : w.units) if (!u.dead && u.home >= 0 && &w.sites[u.home] == &s && w.UD(u).key == "sentinel") guarded = true;
        for (Unit* u : army) if (IsleOf(w, u->p) == IsleOf(w, s.p) && u->order == O_IDLE) { Command c; c.kind = guarded ? C_ATTACK_MOVE : C_MOVE; c.player = p; c.units = {u->id}; c.at = s.p; out.push_back(c); break; }
    }
    // rally the idle army at home
    if (harbor) { Command c; c.kind = C_MOVE; c.player = p; c.at = Vector2Lerp(home, {w.W * 0.5f, w.H * 0.5f}, 0.05f); for (Unit* u : army) if (u->order == O_IDLE && Vector2Distance(u->p, home) > 14 && IsleOf(w, u->p) == homeIsle && std::find(M.invUnits.begin(), M.invUnits.end(), u->id) == M.invUnits.end()) c.units.push_back(u->id); if (!c.units.empty()) out.push_back(c); }
}

}  // namespace fa
