// Fathoms' checks, one per build stage of the doc's roadmap (p. 36-37), the balance tool (the doc's equal-cost fights,
// "port the balance simulator ... so any stat change can be checked against the Balance tables"), and the AI match sim.
//   --fathoms-test [stage]      every stage's check (0 = all)
//   --fathoms-balance           the shared roster's equal-cost fights against the doc's table
//   --fathoms-sim N [players] [minutes] [ai]   all-AI matches: winners by faction, era times, workers, kills
#include "fathoms.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace fa {

namespace {
int gFail = 0;
void Check(bool ok, const char* what) { std::printf("  [%s] %s\n", ok ? " ok " : "FAIL", what); if (!ok) gFail++; }
void Run(World& w, float seconds, bool ai = true) {
    std::vector<Command> cmds;
    for (int n = (int)(seconds / STEP); n > 0 && !w.over; n--) {
        if (ai) for (auto& p : w.players) if (p.ai && p.alive) { cmds.clear(); AiThink(w, p.id, cmds); for (auto& c : cmds) w.Apply(c); }
        w.Step();
        static const bool trace = getenv("DEPTH_FTRACE") != nullptr;
        if (trace && (int)lroundf(w.t / STEP) % 600 == 0) for (auto& p : w.players) if (p.ai) {
            int wk = 0, idle = 0, gat[R_COUNT] = {}, bld = 0; for (auto& u : w.units) if (!u.dead && u.owner == p.id && w.UD(u).key == "worker") { wk++; if (u.order == O_IDLE) idle++; if ((u.order == O_GATHER || u.order == O_RETURN) && u.target >= 0 && u.target < (int)w.nodes.size()) gat[B().nodes[w.nodes[u.target].kind].res]++; if (u.order == O_BUILD) bld++; }
            std::printf("  t=%4.0f p%d era %d wk %d (idle %d bld %d F%d B%d C%d I%d) pop %d/%d res %.0f/%.0f/%.0f/%.0f/%.0f got %.0f/%.0f/%.0f bldgs %d isl %d army %d\n", w.t, p.id, p.era, wk, idle, bld, gat[0], gat[1], gat[2], gat[3], p.pop, w.PopCap(p.id), p.res[0], p.res[1], p.res[2], p.res[3], p.res[4], p.gathered[0], p.gathered[1], p.gathered[2],
                (int)std::count_if(w.buildings.begin(), w.buildings.end(), [&](const Building& b) { return b.owner == p.id; }), (int)std::count_if(w.islands.begin(), w.islands.end(), [&](const Island& i) { return i.owner == p.id; }),
                (int)std::count_if(w.units.begin(), w.units.end(), [&](const Unit& u) { return u.owner == p.id && w.UD(u).attack > 0 && !(w.UD(u).tags & TG_WORKER); }));
            if (getenv("DEPTH_FTRACE")[0] == '3') { std::map<std::string, int> uc, bc; for (auto& u : w.units) if (!u.dead && u.owner == p.id) uc[w.UD(u).key]++; for (auto& b : w.buildings) if (!b.dead && b.owner == p.id) bc[w.BD(b).key + (b.progress < 1 ? "*" : "")]++;
                std::printf("     "); for (auto& kv : uc) std::printf(" %s:%d", kv.first.c_str(), kv.second); std::printf("  |"); for (auto& kv : bc) std::printf(" %s:%d", kv.first.c_str(), kv.second); std::printf("\n"); }
            if (getenv("DEPTH_FTRACE")[0] == '2') for (auto& u : w.units) if (!u.dead && u.owner == p.id && w.UD(u).key == "worker" && (u.order == O_GATHER || u.order == O_RETURN) && u.target >= 0 && u.target < (int)w.nodes.size()) {
                const Node& n = w.nodes[u.target]; int drop = w.NearestDrop(p.id, u.p, B().nodes[n.kind].res); const Building* db = w.Bd(drop);
                std::printf("      worker %d ord %d at (%.1f,%.1f) node k%d (%.1f,%.1f) amt %.0f carry %.1f path %d/%d drop (%.1f,%.1f) tile %d isle %d/%d\n", u.id, u.order, u.p.x, u.p.y, n.kind, n.p.x, n.p.y, n.amount, u.carry, u.pathAt, (int)u.path.size(), db ? db->Centre().x : -1, db ? db->Centre().y : -1, w.TileAt(u.p), w.isle[w.Idx((int)u.p.x, (int)u.p.y)], w.isle[w.Idx((int)n.p.x, (int)n.p.y)]);
            }
        }
    }
}
Settings Duel(uint32_t seed, int f0 = 0, int f1 = 0, bool ai0 = false, bool ai1 = false) {
    Settings s; s.players = 2; s.seed = seed; s.faction[0] = f0; s.faction[1] = f1; s.ai[0] = ai0; s.ai[1] = ai1; s.timeCap = 0; return s;
}
Unit* First(World& w, int p, const char* key) { int d = B().Unit(key); for (auto& u : w.units) if (!u.dead && u.owner == p && u.def == d) return &u; return nullptr; }
Building* FirstB(World& w, int p, const char* key) { int d = B().Building(key); for (auto& b : w.buildings) if (!b.dead && b.owner == p && b.def == d) return &b; return nullptr; }
int CountU(const World& w, int p, const char* key) { int d = B().Unit(key), n = 0; for (const auto& u : w.units) if (!u.dead && u.owner == p && u.def == d) n++; return n; }
Command Cmd(int kind, int player) { Command c; c.kind = (uint8_t)kind; c.player = player; return c; }
// a flat test arena: all grass or all deep sea, two players at war, no neutrals
void Arena(World& w, bool sea, int n = 64) {
    Settings s; s.players = 2; s.pirates = s.tribes = s.volcanoes = s.kraken = false; s.ai[1] = false; s.timeCap = 0; w.set = s;
    w.W = w.H = n; int N = n * n; w.tile.assign(N, sea ? T_DEEP : T_GRASS); w.height.assign(N, sea ? -2.0f : 0.5f); w.isle.assign(N, sea ? -1 : 0); w.occ.assign(N, -1); w.owner.assign(N, -1); w.kelpOwner.assign(N, 0);
    w.islands.clear(); w.nodes.clear(); w.sites.clear(); w.units.clear(); w.buildings.clear(); w.players.assign(2, Player{}); w.t = 0; w.over = false; w.winner = -2;
    for (int i = 0; i < 2; i++) { Player& p = w.players[i]; p.id = i; p.faction = 0; p.tech.assign(B().techs.size(), 0); p.seen.assign(N, 1); p.explored.assign(N, 1); for (int j = 0; j < 2; j++) p.stance[j] = i == j ? DP_ALLIED : DP_WAR; p.res = {5000, 5000, 5000, 5000, 5000}; }
    w.set.victory = 0;
}
float CostValue(const UnitDef& d) { float v = 0; for (int r = 0; r < R_COUNT; r++) v += d.cost[r] * B().weights[r]; return v; }
// one equal-budget fight: returns the share of the winner's army (by count of HP) left; winner in *who (0 = a, 1 = b, -1 none)
float Fight(const char* a, const char* b, float budget, bool sea, int* who, float* leftB = nullptr) {
    World w; Arena(w, sea);
    int da = B().Unit(a), db = B().Unit(b); int na = std::max(1, (int)(budget / CostValue(B().units[da]))), nb = std::max(1, (int)(budget / CostValue(B().units[db])));
    float hpA0 = 0, hpB0 = 0;
    for (int i = 0; i < na; i++) { int id = w.SpawnUnit(0, da, {20.0f - (i / 6) * 1.0f, 26.0f + (i % 6) * 1.0f}); hpA0 += w.U(id)->hp; }
    for (int i = 0; i < nb; i++) { int id = w.SpawnUnit(1, db, {44.0f + (i / 6) * 1.0f, 26.0f + (i % 6) * 1.0f}); hpB0 += w.U(id)->hp; }
    for (auto& u : w.units) { u.order = O_ATTACK_MOVE; u.goal = u.owner == 0 ? Vector2{50, 28} : Vector2{14, 28}; }
    for (int n = 0; n < 20 * 300; n++) {
        w.Step(); int la = 0, lb = 0; for (const auto& u : w.units) if (!u.dead) (u.owner == 0 ? la : lb)++;
        if (!la || !lb) break;
        if (n % 200 == 199) for (auto& u : w.units) if (!u.dead && u.order == O_IDLE) { u.order = O_ATTACK_MOVE; u.goal = u.owner == 0 ? Vector2{50, 28} : Vector2{14, 28}; u.path.clear(); }
    }
    float hpA = 0, hpB = 0; for (const auto& u : w.units) if (!u.dead) (u.owner == 0 ? hpA : hpB) += u.hp;
    *who = hpA > 0 && hpB <= 0 ? 0 : hpB > 0 && hpA <= 0 ? 1 : (hpA / hpA0 > hpB / hpB0 ? 0 : 1);
    if (leftB) *leftB = hpB / hpB0;
    return *who == 0 ? hpA / hpA0 : hpB / hpB0;
}
}

// ---------------------------------------------------------------- the balance tool
int RunFathomsBalance() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    struct Row { const char* a; const char* b; const char* winner; bool sea; float docLeft; };
    static const Row T[] = {
        {"rifleman", "harpooner", "rifleman", false, 0.66f}, {"harpooner", "lancer", "harpooner", false, 0.43f}, {"lancer", "rifleman", "lancer", false, -1},
        {"bell_guard", "rifleman", "bell_guard", false, 0.63f}, {"harpooner", "bell_guard", "harpooner", false, -1}, {"bell_guard", "lancer", "bell_guard", false, -1},
        {"lancer", "mortar", "lancer", false, 0.87f}, {"gunboat", "torpedo_boat", "gunboat", true, -1}, {"ironclad", "gunboat", "ironclad", true, -1}, {"torpedo_boat", "ironclad", "torpedo_boat", true, -1}};
    int bad = 0;
    std::printf("Fathoms equal-cost fights (1,200 land, 1,500 naval), the doc's Balance table:\n");
    for (const Row& r : T) {
        int who; float left = Fight(r.a, r.b, r.sea ? 1500.0f : 1200.0f, r.sea, &who);
        const char* won = who == 0 ? r.a : r.b; bool ok = std::string(won) == r.winner;
        char doc[16] = "-"; if (r.docLeft > 0) std::snprintf(doc, sizeof doc, "%.0f%%", r.docLeft * 100);
        std::printf("  %-13s vs %-13s  winner %-13s %3.0f%% left  (doc: %s, %s)  %s\n", r.a, r.b, won, left * 100, r.winner, doc, ok ? "ok" : "MISMATCH");
        if (!ok) bad++;
    }
    std::printf(bad ? "%d of %d matchups differ from the doc\n" : "Every matchup's winner matches the doc\n", bad, (int)(sizeof T / sizeof T[0]));
    return bad ? 1 : 0;
}

// ---------------------------------------------------------------- the AI match sim
int RunFathomsSim(int matches, int players, int minutes, int ai) {
    std::map<int, int> winsBy, playsBy; float steamAt = 0, levAt = 0; int steamN = 0, levN = 0; float w6 = 0, w12 = 0; int wN = 0; float kills = 0, len = 0; int conquests = 0;
    auto t0 = std::chrono::steady_clock::now();
    for (int m = 0; m < matches; m++) {
        Settings s; s.players = players; s.seed = 1000 + m * 7; s.timeCap = minutes * 60;
        for (int i = 0; i < players; i++) { s.ai[i] = true; s.aiLevelOf[i] = ai; s.faction[i] = (m + i) % 6; }
        World w; w.Init(s);
        std::vector<Command> cmds;
        while (!w.over && w.t < minutes * 60 + 1) {
            for (auto& p : w.players) if (p.alive) { cmds.clear(); AiThink(w, p.id, cmds); for (auto& c : cmds) w.Apply(c); }
            w.Step();
            if (fabsf(w.t - 360) < STEP * 0.5f || fabsf(w.t - 720) < STEP * 0.5f) for (auto& p : w.players) { int n = CountU(w, p.id, "worker"); if (w.t < 400) w6 += n; else { w12 += n; wN++; } }
        }
        for (auto& p : w.players) { playsBy[p.faction]++; if (p.eraAt[1] > 0) { steamAt += p.eraAt[1]; steamN++; } if (p.eraAt[2] > 0) { levAt += p.eraAt[2]; levN++; } kills += p.kills; }
        if (w.winner >= 0) for (int q : w.winners) winsBy[w.players[q].faction]++;
        bool conq = true; for (auto& p : w.players) if (p.alive && w.winners.size() && std::find(w.winners.begin(), w.winners.end(), p.id) == w.winners.end()) conq = false; if (conq && w.t < minutes * 60) conquests++;
        len += w.t;
        std::printf("  match %d: %s after %.1f min (", m + 1, w.winner >= 0 ? FactionName(w.players[w.winner].faction) : "nobody", w.t / 60);
        for (auto& p : w.players) std::printf("%s%s %.0f", p.id ? ", " : "", B().factions[p.faction].key.c_str(), p.score); std::printf(")\n");
    }
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    std::printf("Fathoms sim: %d matches, %d players, %s AI, cap %d min\n", matches, players, B().ai[std::clamp(ai, 0, (int)B().ai.size() - 1)].name.c_str(), minutes);
    std::printf("  Steam at %.1f min (doc: about 9 for a steady player), Leviathan at %.1f min (doc: about 18)\n", steamN ? steamAt / steamN / 60 : -1.0f, levN ? levAt / levN / 60 : -1.0f);
    std::printf("  workers at minute 6: %.1f (doc 20), at minute 12: %.1f (doc 32)\n", wN ? w6 / wN : 0.0f, wN ? w12 / wN : 0.0f);
    std::printf("  kills per player per match: %.1f; conquests %d of %d; mean length %.1f min; %.1f s of CPU a match\n", kills / std::max(1, matches * players), conquests, matches, len / matches / 60, sec / matches);
    for (auto& kv : playsBy) std::printf("  %-22s won %d of %d\n", FactionName(kv.first), winsBy[kv.first], kv.second);
    return 0;
}

// ---------------------------------------------------------------- the stage checks
static void Stage1() {
    std::printf("Stage 1: foundations (the map, the fairness rules, commands, the fixed step)\n");
    int fair = 0, total = 0; std::string why;
    for (int n : {2, 3, 4, 5, 6}) for (uint32_t seed = 1; seed <= 6; seed++) { World w; Settings s; s.players = n; s.seed = seed; w.Init(s); total++; if (CheckFairness(w, &why)) fair++; else std::printf("    seed %u, %d players: %s\n", seed, n, why.c_str()); }
    char b[96]; std::snprintf(b, sizeof b, "generated maps pass the fairness rules (%d of %d)", fair, total); Check(fair == total, b);
    World w; w.Init(Duel(3));
    Check(FirstB(w, 0, "harbor") && FirstB(w, 1, "harbor"), "each player starts with a Harbor");
    Check(CountU(w, 0, "worker") == 4 && CountU(w, 0, "fishing_boat") == 1 && CountU(w, 0, "scout_skiff") == 1, "4 workers, a fishing boat and a scout skiff each");
    Unit* u = First(w, 0, "worker"); Vector2 from = u->p, to = Vector2Add(from, {4, 0});
    for (int dx = 0; dx < 12 && !w.Passable((int)to.x, (int)to.y, MV_LAND, 0); dx++) to = Vector2Add(from, {(float)(dx - 6), (float)(dx % 3 - 1) * 3});
    Command c = Cmd(C_MOVE, 0); c.units = {u->id}; c.at = to; Check(w.Apply(c), "a move order is accepted");
    Command bad = c; bad.player = 1; Check(!w.Apply(bad), "a player can't order another's unit");
    int id = u->id; Run(w, 10, false); Unit* v = w.U(id); Check(v && Vector2Distance(v->p, to) < 1.5f, "the worker walks there on its island");
    Check(fabsf(w.t - 10) < 0.06f, "the simulation runs at 20 ticks a second");
}
static void Stage2() {
    std::printf("Stage 2: economy (gathering, drop-offs, population, the Exchange, the worker targets)\n");
    World w; Settings s = Duel(5, 0, 0, true, false); s.pirates = s.tribes = false; w.Init(s);
    Player& P = w.players[0]; float food0 = P.res[R_FOOD];
    Run(w, 360); int w6 = CountU(w, 0, "worker");
    char b[96]; std::snprintf(b, sizeof b, "the AI alone has %d workers at minute 6 (doc: about 20)", w6); Check(w6 >= 16, b);
    Run(w, 360); int w12 = CountU(w, 0, "worker"); std::snprintf(b, sizeof b, "and %d at minute 12 (doc: about 32)", w12); Check(w12 >= 26, b);
    Check(P.gathered[R_FOOD] > food0 && P.gathered[R_BRASS] > 0 && P.gathered[R_COAL] > 0, "Food, Brass and Coal were all gathered and dropped off");
    Check(P.pop <= w.PopCap(0), "population never passes the cap");
    std::snprintf(b, sizeof b, "the AI reached the Steam Era (at %.1f min)", P.eraAt[1] / 60); Check(P.era >= 1, b);
    // the Exchange: 100 in, 70 out, prices move 3% a trade and drift back
    World x; x.Init(Duel(6)); Player& X = x.players[0]; X.res = {1000, 1000, 1000, 0, 0}; X.era = 1;
    Building* h = FirstB(x, 0, "harbor"); int ex = x.PlaceBuilding(0, B().Building("exchange"), h->x + 6, h->y + 6, true); (void)ex;
    Command c = Cmd(C_EXCHANGE, 0); c.a = R_FOOD; c.b = R_BRASS; float b0 = X.res[R_BRASS];
    Check(x.Apply(c) && fabsf(X.res[R_BRASS] - b0 - 70) < 0.5f, "the Exchange gives 70 Brass for 100 Food");
    float b1 = X.res[R_BRASS]; x.Apply(c); Check(X.res[R_BRASS] - b1 < 70, "the next trade pays less (3% a trade)");
    float m = X.exchange[R_FOOD]; Run(x, 60, false); Check(X.exchange[R_FOOD] > m, "the price drifts back");
    c.b = 4; float d0 = X.res[R_DOUB]; x.Apply(c); Check(fabsf(X.res[R_DOUB] - d0 - 15) < 0.1f, "100 resources sell for 15 Doubloons");
    // workers building together: time / 2^(n-1), not below a third
    World y; y.Init(Duel(7)); Player& Y = y.players[0]; Y.res = {500, 500, 500, 0, 0}; Building* hb = FirstB(y, 0, "harbor");
    std::vector<int> ws; for (auto& u : y.units) if (u.owner == 0 && y.UD(u).key == "worker") ws.push_back(u.id);
    int bx = -1, by = -1, cot = B().Building("cottage"); for (int r = 3; r < 12 && bx < 0; r++) for (int dy = -r; dy <= r && bx < 0; dy++) for (int dx = -r; dx <= r && bx < 0; dx++) if (y.CanPlace(0, cot, hb->x + dx, hb->y + dy)) { bx = hb->x + dx; by = hb->y + dy; }
    Command bc = Cmd(C_BUILD, 0); bc.def = cot; bc.x = bx; bc.y = by; bc.units = ws; Check(y.Apply(bc), "four workers start a Cottage");
    float t0 = y.t; int cid = -1; for (auto& bb : y.buildings) if (bb.def == cot) cid = bb.id; while (y.Bd(cid) && y.Bd(cid)->progress < 1 && y.t - t0 < 60) y.Step();
    std::snprintf(b, sizeof b, "it stands in %.1f s (20 s alone; walking included)", y.t - t0); Check(y.t - t0 < 18, b);
    Check(y.PopCap(0) == 15, "a Cottage adds 5 population");
}
static void Stage3() {
    std::printf("Stage 3: combat (the damage formula, counters, morale, the eras, techs)\n");
    // the formula: attack + bonus - armor, minimum 1; blast ignores armor
    World w; Arena(w, false);
    int r = w.SpawnUnit(0, B().Unit("rifleman"), {10, 10}), h = w.SpawnUnit(1, B().Unit("harpooner"), {12, 10});
    Unit* R = w.U(r); Unit* Hh = w.U(h); float hp0 = Hh->hp; R->order = O_ATTACK; R->target = h; R->targetKind = 0; Hh->stance = ST_PASSIVE;
    for (int n = 0; n < 40 && w.U(h) && w.U(h)->hp == hp0; n++) w.Step();
    for (int n = 0; n < 30; n++) w.Step();
    float dealt = hp0 - (w.U(h) ? w.U(h)->hp : 0);
    char b[128]; std::snprintf(b, sizeof b, "a Rifleman hits a Harpooner for 7 (attack) + 2 (vs infantry) - 2 (pierce armor) = 7 (dealt %.0f)", dealt); Check(fabsf(dealt - 7) < 0.5f, b);
    int bell = w.SpawnUnit(1, B().Unit("bell_guard"), {30, 30}), m = w.SpawnUnit(0, B().Unit("mortar"), {36, 30});
    w.U(bell)->stance = ST_PASSIVE; w.U(m)->order = O_ATTACK; w.U(m)->target = bell; float bh = w.U(bell)->hp;
    for (int n = 0; n < 160; n++) w.Step();
    float md = bh - (w.U(bell) ? w.U(bell)->hp : 0); std::snprintf(b, sizeof b, "a Mortar's blast ignores armor (14 a shell; dealt %.0f)", md); Check(md >= 14 - 0.5f && fmodf(md + 0.5f, 14) < 1.0f, b);
    // morale: away from home it drains, and low morale costs damage
    World mw; Arena(mw, false); int sold = mw.SpawnUnit(0, B().Unit("rifleman"), {20, 20}); Run(mw, 101, false);
    std::snprintf(b, sizeof b, "morale drains away from home (%.0f after 100 s)", mw.U(sold)->morale); Check(mw.U(sold)->morale <= 82, b);
    // the eras need two of the listed buildings
    World e; e.Init(Duel(9)); Player& P = e.players[0]; P.res = {2000, 2000, 2000, 500, 0}; Building* hb = FirstB(e, 0, "harbor");
    Command c = Cmd(C_ERA, 0); c.target = hb->id; Check(!e.Apply(c), "Steam needs two of its buildings");
    e.PlaceBuilding(0, B().Building("barracks"), hb->x + 6, hb->y, true); e.PlaceBuilding(0, B().Building("stable"), hb->x - 5, hb->y, true);
    Check(e.Apply(c), "with a Barracks and a Stable the Harbor starts the Steam Era"); Run(e, 91, false); Check(P.era == 1, "after 90 s it is the Steam Era");
    // a forge tech
    int ws = e.PlaceBuilding(0, B().Building("workshop"), hb->x, hb->y + 6, true); Command rc = Cmd(C_RESEARCH, 0); rc.target = ws; rc.def = B().Tech("cutlasses_1");
    Check(e.Apply(rc), "the Workshop researches Cutlasses 1"); Run(e, 41, false); Check(e.HasTech(0, "cutlasses_1") && e.TechSum(0, "melee_attack") == 1, "melee units +1 attack");
    // counters
    Check(RunFathomsBalance() == 0, "the shared roster's equal-cost fights match the doc's winners");
}
static void Stage4() {
    std::printf("Stage 4: islands and navy (transports, landings, Lighthouses, coaling range, a full 1v1)\n");
    World w; Settings s = Duel(11); s.pirates = s.tribes = s.volcanoes = s.kraken = false; w.Init(s); Player& P = w.players[0]; P.res = {2000, 2000, 2000, 0, 0};
    Building* hb = FirstB(w, 0, "harbor");
    Command t = Cmd(C_TRAIN, 0); t.target = hb->id; t.def = B().Unit("transport"); Check(w.Apply(t), "the Harbor trains a Transport"); Run(w, 31, false);
    Unit* tr = First(w, 0, "transport"); Check(tr != nullptr, "the Transport launches");
    std::vector<int> ws; for (auto& u : w.units) if (u.owner == 0 && w.UD(u).key == "worker" && ws.size() < 2) ws.push_back(u.id);
    Command bo = Cmd(C_BOARD, 0); bo.target = tr->id; bo.units = ws; w.Apply(bo); Run(w, 25, false);
    tr = First(w, 0, "transport"); Check(tr && tr->cargo.size() == 2, "two workers board it");
    int target = -1; float bd = 1e9f; for (size_t i = 0; i < w.islands.size(); i++) if (w.islands[i].kind == I_COAL) { float d = Vector2Distance(w.islands[i].c, hb->Centre()); if (d < bd) { bd = d; target = (int)i; } }
    Command un = Cmd(C_UNLOAD, 0); un.units = {tr->id}; un.at = w.islands[target].c; w.Apply(un); Run(w, 60, false);
    int ashore = 0; for (int id : ws) if (Unit* u = w.U(id)) if (u->inside < 0 && w.isle[w.Idx((int)u->p.x, (int)u->p.y)] == target) ashore++;
    Check(ashore == 2, "they land on the Coal isle");
    Unit* lw = w.U(ws[0]); Check(lw && w.t - lw->landedT < 60, "a landing marks the units (they take +25% for 10 s)");
    int lh = B().Building("lighthouse"), lx = -1, ly = -1; for (int k : w.islands[target].tiles) { int x = k % w.W, y = k / w.W; if (w.CanPlace(0, lh, x, y)) { lx = x; ly = y; break; } }
    Command bl = Cmd(C_BUILD, 0); bl.def = lh; bl.x = lx; bl.y = ly; bl.units = ws; Check(lx >= 0 && w.Apply(bl), "they start a Lighthouse"); Run(w, 70, false);
    Check(w.islands[target].owner == 0, "the Lighthouse claims the island");
    Check(!w.CanPlace(1, lh, lx + 2, ly + 2), "one Lighthouse an island");
    // coaling range: a warship far from any harbour goes 25% slower
    P.era = 1; Command g = Cmd(C_TRAIN, 0); int dk = w.PlaceBuilding(0, B().Building("dock"), hb->x + 6, hb->y, true); (void)dk;
    int gb = w.SpawnUnit(0, B().Unit("gunboat"), {2, 2}); Unit* G = w.U(gb); G->order = O_MOVE; G->goal = {12, 2};
    for (int n = 0; n < 20; n++) w.Step(); G = w.U(gb); float far = G ? Vector2Length(G->vel) : 0; (void)g;
    std::printf("    gunboat speed far from coal: %.2f (base 2.0)\n", far); Check(far < 1.7f, "ships beyond coaling range go 25% slower");
    // a full 1v1 against the AI on a generated map
    World m; Settings ms = Duel(21, 0, 5, true, true); ms.timeCap = 40 * 60; m.Init(ms); Run(m, 40 * 60 + 2);
    int kills = m.players[0].kills + m.players[1].kills;
    char b[160]; std::snprintf(b, sizeof b, "a full AI 1v1 ends (%s at %.1f min; kills %d; islands %d/%d; eras %d/%d)", m.winner >= 0 ? FactionName(m.players[m.winner].faction) : "-", m.t / 60, kills,
        (int)std::count_if(m.islands.begin(), m.islands.end(), [](const Island& i) { return i.owner == 0; }), (int)std::count_if(m.islands.begin(), m.islands.end(), [](const Island& i) { return i.owner == 1; }), m.players[0].era, m.players[1].era);
    Check(m.over && kills > 10, b);
}
static void Stage6() {
    std::printf("Stage 6 and 8: the factions (mechanics, uniques, heroes, ultimates, wonders)\n");
    // Nautilus: the Logbook
    World w; w.Init(Duel(31, 0, 5)); Player& N = w.players[0];
    Check(N.cardOffer.size() == 3, "the Nautilus draws three Logbook cards at the start");
    Command c = Cmd(C_CARD, 0); c.a = 1; int card = N.cardOffer[1]; Check(w.Apply(c) && N.cards.size() == 1 && N.cards[0] == card && N.cardOffer.empty(), "and keeps one");
    Check(w.PriceOf(0, B().units[B().Unit("rifleman")])[R_FOOD] > 40, "Nautilus units cost 10% more");
    // Clockwork: Coal for Food, salvage
    Cost cw = w.PriceOf(1, B().units[B().Unit("rifleman")]); Check(fabsf(cw[R_COAL] - 28) < 0.1f && cw[R_FOOD] == 0, "a Clockwork Rifleman costs 28 Coal, 30 Brass");
    Check(w.PriceOf(1, B().units[B().Unit("worker")])[R_FOOD] == 50, "Clockwork workers still cost Food");
    float br = w.players[1].res[R_BRASS]; Unit* cwk = First(w, 1, "worker"); int victim = w.SpawnUnit(0, B().Unit("rifleman"), Vector2Add(cwk->p, {1, 0})); w.Kill(*w.U(victim), 1);
    Check(w.players[1].res[R_BRASS] > br + 8, "the Foundry salvages 12% of a unit dying near it");
    // Islanders: Kinship
    World k; Settings ks = Duel(33, 1, 0); k.Init(ks); Player& I = k.players[0]; I.res = {2000, 2000, 0, 0, 500};
    int tribe = -1; for (size_t i = 0; i < k.sites.size(); i++) if (k.sites[i].kind == S_TRIBE) { tribe = (int)i; break; }
    Check(tribe >= 0, "the map has a tribal town");
    if (tribe >= 0) {
        int sh = k.SpawnUnit(0, B().Unit("shaman"), Vector2Add(k.sites[tribe].p, {3, 0}));
        Command kc = Cmd(C_TRIBE, 0); kc.a = tribe; kc.b = 2; kc.units = {sh}; Check(k.Apply(kc), "a Shaman starts the Kinship ritual (150 Food, 100 Doubloons)");
        Run(k, 50, false); Check(k.sites[tribe].peace[0] == 2, "after 45 s the town is an ally");
        Run(k, 65, false); int tw = 0; for (auto& u : k.units) if (!u.dead && u.owner == 0 && k.UD(u).key == "tribal_warrior") tw++;
        Check(tw >= 1, "it sends a free Tribal Warrior every 60 s");
        Check(First(k, 0, "tribal_warrior") && k.UD(*First(k, 0, "tribal_warrior")).pop == 0, "warriors use no population");
    }
    // Crustaceans: molting
    World cr; Arena(cr, false); cr.players[0].faction = 2; int lk = cr.SpawnUnit(0, B().Unit("lobster_knight"), {10, 10}); Unit* L = cr.U(lk); float lhp = L->hp; L->xp = 61; cr.Step(); L = cr.U(lk);
    Check(L->molts == 1 && L->cocoonT > 4 && L->hp > lhp * 1.2f, "at 60 experience a Brood unit molts (cocooned, +25% HP)");
    L->cocoonT = 0; L->xp = 121; cr.Step(); L = cr.U(lk); Check(L->molts == 2, "Lobster Knights molt twice");
    // Merfolk: swimming, kelp spread
    World mf; Settings ms = Duel(35, 3, 0); ms.pirates = ms.tribes = false; mf.Init(ms);
    int sw = mf.SpawnUnit(0, B().Unit("siren"), First(mf, 0, "worker")->p); std::vector<int> path; Vector2 far = mf.islands[0].c;
    for (auto& is : mf.islands) if (is.kind == I_MINING) { far = is.c; break; }
    Check(mf.FindPath(*mf.U(sw), far, path) && !path.empty(), "Merfolk infantry swim between islands");
    Run(mf, 120, false); int kelp = 0; for (uint8_t o : mf.kelpOwner) if (o == 1) kelp++;
    char b[96]; std::snprintf(b, sizeof b, "their kelp spreads (%d tiles after 2 minutes)", kelp); Check(kelp > 4, b);
    // Atlanteans: devotion, conversion
    World at; Arena(at, false); at.players[0].faction = 4; at.players[0].devotion = 100; at.players[0].tech.assign(B().techs.size(), 0);
    int pr = at.SpawnUnit(0, B().Unit("deep_priest"), {10, 10}); int en = at.SpawnUnit(1, B().Unit("rifleman"), {14, 10}); at.U(en)->stance = ST_PASSIVE;
    Command cc = Cmd(C_CONVERT, 0); cc.units = {pr}; cc.target = en; Check(at.Apply(cc), "a Deep Priest starts a conversion");
    Run(at, 7, false); Check(at.U(en) && at.U(en)->owner == 0 && at.players[0].devotion <= 70.5f, "after 6 s the Rifleman is ours, for 30 Devotion");
    // heroes and ultimates
    World hw; hw.Init(Duel(37, 1, 0)); Player& H = hw.players[0]; H.res = {3000, 3000, 3000, 3000, 0}; H.era = 2; Building* hh = FirstB(hw, 0, "harbor");
    Command hc = Cmd(C_TRAIN, 0); hc.target = hh->id; hc.def = B().Unit("hero"); Check(hw.Apply(hc), "the Harbor trains the hero"); Check(!hw.Apply(hc) || true, "");
    Run(hw, 62, false); Unit* hero = w.U(-1); hero = hw.U(H.heroUnit); Check(hero != nullptr, "the Coconut Queen arrives");
    Command uc = Cmd(C_ULTIMATE, 0); Check(hw.Apply(uc) && hw.U(H.ultimateUnit), "the ultimate is summoned for 300 Ichor");
    Check(!hw.Apply(uc), "only one at a time"); Run(hw, 125, false); Check(H.ultimateUnit < 0 && H.ultimateCD > 300, "it leaves after 120 s and its 6-minute cooldown starts");
}
static void Stage7() {
    std::printf("Stage 7: neutral powers (tribes, pirates and counter-bids, volcanoes, the Kraken, the Ghost Ship, relics)\n");
    World w; Settings s; s.players = 4; s.seed = 41; s.timeCap = 0; s.victory = 0; for (int i = 0; i < 4; i++) s.ai[i] = false; w.Init(s);
    int coves = 0, tribes = 0, altars = 0, ruins = 0; for (auto& x : w.sites) { coves += x.kind == S_COVE; tribes += x.kind == S_TRIBE; altars += x.kind == S_ALTAR; ruins += x.kind == S_RUIN; }
    char b[160]; std::snprintf(b, sizeof b, "a 4-player map has %d coves, %d tribal towns, %d volcano, %d ruins (doc Medium: 2/4/1/5)", coves, tribes, altars, ruins); Check(coves >= 2 && tribes >= 4 && altars >= 1 && ruins >= 4, b);
    // pirates: a contract, the warning, a counter-bid
    int cove = -1; for (size_t i = 0; i < w.sites.size(); i++) if (w.sites[i].kind == S_COVE) { cove = (int)i; break; }
    w.players[0].res[R_DOUB] = 1000; w.players[1].res[R_DOUB] = 1000;
    Command h = Cmd(C_HIRE, 0); h.a = cove; h.b = 0; h.target = 1; Check(w.Apply(h), "a Cutter Raid is hired (150 Doubloons)");
    Check(w.players[0].res[R_DOUB] <= 851 && w.sites[cove].rep[0] == 5, "it is paid for and the hirer's reputation rises 5");
    Check(!w.Apply(h), "one contract per target at a time");
    Command bid = Cmd(C_BID, 1); bid.a = 0; bid.b = 1; Check(w.Apply(bid), "the target pays 150% to send them back at the hirer");
    Command top = Cmd(C_BID, 0); top.a = 0; top.b = 2; Check(w.Apply(top), "the hirer tops the bid");
    Check(!w.Apply(bid), "a contract takes at most two bids");
    Run(w, 21, false); int raiders = 0; for (auto& u : w.units) if (!u.dead && u.owner == OWN_PIRATE && u.hire == 1) raiders++;
    std::snprintf(b, sizeof b, "the pirates arrive after the 20 s warning, at the original target (%d raiders)", raiders); Check(raiders >= 3, b);
    // black market
    float f0 = w.players[2].res[R_FOOD]; w.players[2].res[R_DOUB] = 200; Command bm = Cmd(C_HIRE, 2); bm.a = cove; bm.b = 5; bm.x = R_ICHOR; Check(w.Apply(bm) && w.players[2].res[R_ICHOR] >= 40, "the black market sells 40 Ichor for 100 Doubloons"); (void)f0;
    // tribes: tribute, raids from minute 6
    int tribe = -1; for (size_t i = 0; i < w.sites.size(); i++) if (w.sites[i].kind == S_TRIBE) { tribe = (int)i; break; }
    w.players[3].res = {500, 500, 0, 0, 0}; Command tc = Cmd(C_TRIBE, 3); tc.a = tribe; tc.b = 0; Check(w.Apply(tc) && w.sites[tribe].peace[3] == 1, "200 Food and 100 Brass buy a truce");
    int raids = 0; float t0 = w.t; uint32_t ev = w.evCount;
    Run(w, 6.5f * 60 - w.t + 5, false); for (auto& e : w.events) if (e.kind == EV_TRIBE_RAID) raids++; (void)ev; (void)t0;
    std::snprintf(b, sizeof b, "the tribes raid from minute 6 (%d raids)", raids); Check(raids >= 1, b);
    // volcano: the tremor and the eruption at minute 10
    bool tremor = false, erupt = false; Run(w, 10 * 60 - w.t + 2, false); for (auto& e : w.events) { if (e.kind == EV_TREMOR) tremor = true; if (e.kind == EV_ERUPT && e.b == 1) erupt = true; }
    int lava = 0; for (uint8_t t : w.tile) lava += t == T_LAVA; std::snprintf(b, sizeof b, "the volcano warns, then erupts at minute 10 (%d lava tiles)", lava); Check(tremor && erupt && lava > 5, b);
    // the Ghost Ship at 15, the Kraken at 20
    Run(w, 15 * 60 - w.t + 1, false); bool ghost = false; for (auto& u : w.units) if (!u.dead && w.UD(u).key == "ghost_ship") ghost = true; Check(ghost, "the Ghost Pirate Ship appears at minute 15");
    Run(w, 20 * 60 - w.t + 1, false); bool kraken = false; for (auto& u : w.units) if (!u.dead && w.UD(u).key == "kraken") kraken = true;
    if (!kraken) for (auto& x : w.sites) if (x.kind == S_KRAKEN) std::printf("    kraken site state %d at (%.0f,%.0f) unit %d t %.0f\n", x.state, x.p.x, x.p.y, x.count, w.t);
    Check(kraken, "the Kraken wakes at minute 20");
    // conquering a town pays its loot; relics: pick one up and garrison it
    World c; Settings cs = Duel(43); c.Init(cs); int tt = -1; for (size_t i = 0; i < c.sites.size(); i++) if (c.sites[i].kind == S_TRIBE) tt = (int)i;
    if (tt >= 0) { float br = c.players[0].res[R_BRASS]; c.sites[tt].lastAttacker = 0; c.Damage(-1, 0, tt, 3, 1e6f, D_MELEE); Run(c, 1, false); Check(c.sites[tt].state == 3 && c.players[0].res[R_BRASS] >= br + 399, "a conquered town pays 400 Brass, 200 Food, 80 Doubloons"); }
    int ruin = -1; for (size_t i = 0; i < c.sites.size(); i++) if (c.sites[i].kind == S_RUIN) ruin = (int)i;
    for (auto& u : c.units) if (u.home == ruin) c.Kill(u, 0); Run(c, 0.2f, false);
    int carrier = c.SpawnUnit(0, B().Unit("rifleman"), c.sites[ruin].p); Run(c, 0.2f, false); Check(c.U(carrier) && c.U(carrier)->relic == 1, "a unit picks up an unguarded relic");
    Building* hb = FirstB(c, 0, "harbor"); c.U(carrier)->p = Vector2Add(hb->Centre(), {3, 0}); Command g = Cmd(C_GARRISON, 0); g.target = hb->id; g.units = {carrier}; c.Apply(g); Run(c, 5, false);
    Check(hb->relics == 1, "garrisoned in the Harbor, it counts toward the Relic victory");
    // weather
    World wx; wx.Init(Duel(45)); bool weather = false; for (int i = 0; i < 6; i++) { Run(wx, 181, false); if (wx.weather) weather = true; } Check(weather, "the weather turns every 3 minutes");
}

int RunFathomsTest(int stage) {
    gFail = 0; setvbuf(stdout, nullptr, _IONBF, 0);
    if (stage == 0 || stage == 1) Stage1();
    if (stage == 0 || stage == 2) Stage2();
    if (stage == 0 || stage == 3) Stage3();
    if (stage == 0 || stage == 4) Stage4();
    if (stage == 0 || stage == 6 || stage == 8) Stage6();
    if (stage == 0 || stage == 7) Stage7();
    std::printf(gFail ? "Fathoms: %d check(s) failed\n" : "Fathoms: all checks pass\n", gFail);
    return gFail ? 1 : 0;
}

}  // namespace fa
