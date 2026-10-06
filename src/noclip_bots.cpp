// NOCLIP: the bot salvagers (solo crews and the sim), the acceptance tests (--noclip-test), the generator check
// (--noclip-gen) and the balance sim (--noclip-sim).
#include "noclip.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>

namespace nc {

static float Ang(float a) { while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }
// steer along a cell path toward a goal; true while there's somewhere to go
static bool Follow(World& w, Player& p, Input& o, Vector3 goal, bool sprint) {
    Level& lv = w.L(p.level);
    if (p.botPath.empty() || p.botThink <= 0) { p.botPath = w.Path(p.level, p.p, goal, 6000); p.botThink = 1.2f; if (!p.botPath.empty()) p.botPath.erase(p.botPath.begin()); }
    Vector3 next = goal; while (!p.botPath.empty()) { int c = p.botPath.front(); next = lv.Center(c % lv.w, c / lv.w); if (Vector2Distance({next.x, next.z}, {p.p.x, p.p.z}) < 0.6f) p.botPath.erase(p.botPath.begin()); else break; }
    if (p.botPath.empty()) next = goal;
    Vector3 d = Vector3Subtract(next, p.p); float L = Vector2Length({d.x, d.z}); if (L < 0.3f) return false;
    float want = atan2f(d.z, d.x); o.yaw = p.yaw + std::clamp(Ang(want - p.yaw), -0.25f, 0.25f); o.pitch = 0;
    o.moveX = fabsf(Ang(want - p.yaw)) < 1.0f ? 1.0f : 0.2f; o.sprint = sprint && p.stamina > 30;
    return true;
}
static int Online(const World& w, int level) { for (int i = 0; i < (int)w.labs.size(); i++) if (w.labs[i].level == level && w.labs[i].online) return i; return -1; }

void BotInput(World& w, int me, Input& in, std::vector<Command>& cmds, uint32_t& rng) {
    Player& p = w.crew[me]; Input o; o.yaw = p.yaw; o.pitch = p.pitch; p.botThink -= STEP;
    auto R = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 16777216.0f; };
    auto tool = [&](const char* id) { int it = ItemIndex(id); for (int k = 0; k < p.toolSlots; k++) if (p.tools[k].item == it && it >= 0) return k; return -1; };
    // the Surface: sell (the quota first, the rest to the Fence), restock, go
    if (!w.inDay) {
        if (w.failed) { in = o; return; }
        if (!w.bay.empty()) { int need = w.quota - w.credit; Command c; c.kind = C_SELL_ALL; c.a = need > 0 ? 0 : 1; cmds.push_back(c); in = o; return; }
        bool slot = false; for (int k = 0; k < p.toolSlots; k++) if (p.tools[k].item < 0) slot = true;
        if (slot) for (const char* id : {"battery", "almond", "medkit", "fuel", "bandages"}) if (tool(id) < 0) { int it = ItemIndex(id); if (it >= 0 && w.cash > D().items[it].price + 60) { Command c; c.kind = C_BUY; c.a = it; cmds.push_back(c); in = o; return; } }
        bool humans = false; for (const auto& q : w.crew) if (!q.bot && q.present) humans = true;
        if (!humans && me == 0) { Command c; c.kind = C_START_DAY; cmds.push_back(c); }
        in = o; return;
    }
    if (p.st == PS_DEAD || p.st == PS_SURFACE || p.st == PS_TAKEN) { in = o; return; }
    if (p.st == PS_DOWNED) { in = o; return; }
    // look after yourself
    if (p.sanity < 40) { int k = tool("almond"); if (k >= 0) { o.slot = k; p.sel = k; o.primary = true; in = o; return; } }
    if (p.health < 40) { int k = tool("medkit"); if (k >= 0) { o.slot = k; p.sel = k; o.primary = true; in = o; return; } }
    if (p.injuries & IN_BLEED) { int k = tool("bandages"); if (k >= 0) { o.slot = k; p.sel = k; o.primary = true; in = o; return; } }
    if (p.battery <= 0) { int k = tool("battery"); if (k >= 0) { o.slot = k; p.sel = k; o.primary = true; in = o; return; } }
    // a downed teammate nearby: revive them
    for (const auto& q : w.crew) if (q.id != me && q.level == p.level && q.st == PS_DOWNED && Vector3Distance(q.p, p.p) < 25) { if (Vector3Distance(q.p, p.p) < 1.5f) { o.use = true; in = o; return; } Follow(w, p, o, q.p, true); in = o; return; }
    // a sealed hatch on this level: someone may be trapped; turn the nearest valve
    { bool hatch = false; for (const auto& s : w.seals) if (s.level == p.level && s.kind == 0) hatch = true;
      if (hatch) { const World::Lever* best = nullptr; float bd = 1e9f; for (const auto& l : w.levers) if (l.level == p.level && l.kind == 0) { float d = Vector3Distance(l.at, p.p); if (d < bd && !w.Path(p.level, p.p, l.at, 4000).empty()) { bd = d; best = &l; } }
        if (best) { if (bd < 1.4f) o.use = true; else Follow(w, p, o, best->at, true); in = o; return; } } }
    // a lost survivor nearby (the Rescue contract): bring them along
    for (const auto& e : w.ents) if (e.level == p.level && D().entities[e.def].id == "survivor" && e.target == me && Vector3Distance(e.p, p.p) > 7) { o.yaw = atan2f(e.p.z - p.p.z, e.p.x - p.p.x); in = o; return; }   // (wait for them)
    for (const auto& e : w.ents) if (e.level == p.level && D().entities[e.def].id == "survivor" && e.target < 0 && (Vector3Distance(e.p, p.p) < 30 || (w.contract >= 0 && D().contracts[w.contract].id == "rescue"))) { if (Vector3Distance(e.p, p.p) < 1.8f) { o.use = true; o.yaw = atan2f(e.p.z - p.p.z, e.p.x - p.p.x); } else Follow(w, p, o, e.p, false); in = o; return; }
    int labIdx = Online(w, p.level); LabState* lab = labIdx >= 0 ? &w.labs[labIdx] : nullptr; Level& lv = w.L(p.level);
    // going home: late, or the crew's crate is enough, or the pockets are full
    bool full = p.hands.def >= 0 || (p.pocket[0].def >= 0 && p.pocket[1].def >= 0);
    bool late = w.mode == 1 || w.clock > 1260 || (w.CrateTotal() + 50 >= w.quota - w.credit && w.clock > 900 && w.day == D().daysPerWeek);
    p.botLevelT += STEP;
    if (!lab) {
        // a dormant Lab here and fuel in hand: restart it (Lost mode's way out, and a new home for the crew)
        if (tool("fuel") >= 0) for (int i = 0; i < (int)w.labs.size(); i++) { const LabState& l = w.labs[i]; if (l.level != p.level || l.online || l.idx >= (int)lv.labs.size()) continue;
            const LabPlan& dp = lv.labs[l.idx];
            if (!l.doorOpen) { Vector3 b = lv.Center(dp.breakerX, dp.breakerZ); if (Vector3Distance(b, p.p) < 1.5f) o.use = true; else Follow(w, p, o, b, true); in = o; return; }   // the breaker first
            for (const auto& sp : dp.spots) if (sp.part == LP_GEN) { if (Vector2Distance({sp.at.x, sp.at.z}, {p.p.x, p.p.z}) < 1.4f) { o.use = true; o.yaw = atan2f(sp.at.z - p.p.z, sp.at.x - p.p.x); } else Follow(w, p, o, sp.at, true); in = o; return; } }
        // Lost: stay with whoever carries the fuel
        if (w.mode == 1) { for (const auto& q : w.crew) if (q.id != me && q.level == p.level && q.Alive()) { bool has = false; for (int k = 0; k < q.toolSlots; k++) if (q.tools[k].item == ItemIndex("fuel")) has = true; if (has) { if (Vector3Distance(q.p, p.p) > 2.5f) Follow(w, p, o, q.p, true); in = o; return; } } }
        // otherwise back the way we came (the door to where we came from, else the nearest exit; a noclip if it comes to it)
        float bd = 1e9f; Vector3 tgt = p.p; for (const auto& e : lv.exits) { float d = Vector3Distance(lv.Center(e.cx, e.cz), p.p); if (p.botFrom >= 0 && e.to == p.botFrom && !e.noclip) d -= 500; if (d < bd) { bd = d; tgt = lv.Center(e.cx, e.cz); } } if (bd < -400) bd += 500;
        // ...unless we came here to scout: loot what's near first, for a while
        if (p.botFrom >= 0 && p.botLevelT < 150 && !(p.hands.def >= 0 || (p.pocket[0].def >= 0 && p.pocket[1].def >= 0)) && w.clock < 1100) {
            for (const auto& it : w.items) if (it.level == p.level && it.loot.def >= 0 && D().loot[it.loot.def].size != "h" && Vector3Distance(it.p, p.p) < 30) { if (Vector3Distance(it.p, p.p) < 1.4f) { o.use = true; o.yaw = atan2f(it.p.z - p.p.z, it.p.x - p.p.x); } else Follow(w, p, o, it.p, false); in = o; return; }
        }
        if (!Follow(w, p, o, tgt, true) || bd < 1.2f) { o.use = true; o.sprint = true; o.moveX = 1; }
        in = o; return;
    }
    const LabPlan& lp = lv.labs[lab->idx];
    Vector3 crate = lp.spots[LP_CRATE].at, desk = lp.spots[LP_DESK].at, ring = lp.spots[LP_RING].at;
    if (full || late) {
        if (full && Vector2Distance({crate.x, crate.z}, {p.p.x, p.p.z}) > 1.2f) { Follow(w, p, o, crate, false); in = o; return; }
        if (full) { o.use = true; in = o; return; }
        // late: to the desk, signal, wait in the hall
        if (!lab->charging && lab->openT <= 0) { if (Vector2Distance({desk.x, desk.z}, {p.p.x, p.p.z}) > 1.3f) { Follow(w, p, o, desk, false); in = o; return; } Command c; c.kind = C_PORTAL; cmds.push_back(c); in = o; return; }
        Follow(w, p, o, ring, false); in = o; return;
    }
    // looting: the nearest worthwhile thing we can carry
    if (p.botGoal < 0 || p.botThink <= 0) {
        int best = -1; float bs = -1;
        for (int i = 0; i < (int)w.items.size(); i++) {
            const WorldItem& it = w.items[i]; if (it.level != p.level || it.loot.def < 0) continue; const LootDef& ld = D().loot[it.loot.def];
            if (ld.size == "h") continue;
            float d = Vector3Distance(it.p, p.p); if (d > 60) continue;
            float s = (it.loot.value + 20) / (5 + d) * (R() * 0.2f + 0.9f);
            if (s > bs) { bs = s; best = i; }
        }
        p.botGoal = best;
        if (best >= 0) p.botTarget = w.items[best].p;
    }
    if (p.botGoal >= 0 && p.botGoal < (int)w.items.size() && Vector3Distance(w.items[p.botGoal].p, p.botTarget) < 0.1f) {
        if (Vector3Distance(p.botTarget, p.p) < 1.4f) { o.use = true; o.yaw = atan2f(p.botTarget.z - p.p.z, p.botTarget.x - p.p.x); p.botGoal = -1; }
        else Follow(w, p, o, p.botTarget, false);
        in = o; return;
    }
    p.botGoal = -1;
    // nothing near and the day is young: one bot in three goes deeper through a real door, remembering the way back
    if (me % 3 == 1 && w.clock < 800 && p.botLevelT > 120) {
        const Exit* best = nullptr; for (const auto& e : lv.exits) if (!e.noclip && e.to > p.level && (!best || e.to < best->to)) best = &e;
        if (best) { Vector3 at = lv.Center(best->cx, best->cz); int from = p.level; if (Vector3Distance(at, p.p) < 1.2f) { o.use = true; o.moveX = 1; p.botFrom = from; p.botLevelT = 0; } else Follow(w, p, o, at, false); in = o; return; }
    }
    // nothing near: wander outward toward the unseen
    if (Vector3Distance(p.botTarget, p.p) < 2 || p.botThink <= 0) { for (int t = 0; t < 30; t++) { int x = (int)(R() * lv.w), z = (int)(R() * lv.h); if (lv.Walkable(x, z) && lv.At(x, z) != T_PIT && lv.At(x, z) != T_DEEP) { p.botTarget = lv.Center(x, z); break; } } }
    Follow(w, p, o, p.botTarget, false);
    in = o;
}

// ---------------------------------------------------------------- tests
static int gFails = 0;
static void Check(bool ok, const char* what, const std::string& d = "") { std::printf("  [%s] %s%s%s\n", ok ? "ok" : "FAIL", what, d.empty() ? "" : ": ", d.c_str()); if (!ok) gFails++; }
static std::string Fm(const char* f, double a, double b = 0, double c = 0) { char s[200]; std::snprintf(s, sizeof s, f, a, b, c); return s; }
static void Run(World& w, float secs, std::function<void()> each = nullptr) { for (int i = 0; i < (int)(secs / STEP); i++) { if (each) each(); w.Step(); } }

int RunNoclipGen(int level, uint32_t seed) {
    Level L = Generate(level, seed); std::printf("%s", DescribeLevel(L).c_str());
    std::string why; bool ok = CheckReachable(L, &why);
    std::printf("Level %d (%s) seed %u: %dx%d, %d Labs, %d exits, %d loot spots: %s\n", level, D().levels[level].name.c_str(), seed, L.w, L.h, (int)L.labs.size(), (int)L.exits.size(), (int)L.loot.size(), ok ? "every Lab reaches every exit" : why.c_str());
    return ok ? 0 : 1;
}

int RunNoclipTest() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // (unbuffered: a crash still shows how far the checks got)
    gFails = 0; std::printf("NOCLIP tests\n"); const Data& d = D();
    Check(d.levels.size() == 20 && d.entities.size() >= 20 && d.items.size() >= 22 && d.suits.size() == 6 && d.labUps.size() == 7 && d.contracts.size() == 8 && d.cosmetics.size() == 41, "the data: 20 levels, the entities, 22 items, 6 suits, 7 Lab upgrades, 8 contracts, 41 cosmetics (40 and the Orange)", Fm("%.0f levels, %.0f entities, %.0f items", d.levels.size(), d.entities.size(), d.items.size()));
    // every level generates and every Lab reaches every exit, over several days' seeds
    { int bad = 0; std::string first; int labs = 0, exits = 0; for (int lv = 0; lv < 20; lv++) for (uint32_t s = 1; s <= 4; s++) { Level L = Generate(lv, s * 977); std::string why; if (!CheckReachable(L, &why)) { bad++; if (first.empty()) first = "level " + std::to_string(lv) + ": " + why; } labs += (int)L.labs.size(); exits += (int)L.exits.size(); }
      Check(bad == 0, "all twenty levels generate with every Lab reaching every exit (4 seeds each)", bad ? first : Fm("%.0f Labs, %.0f exits", labs, exits)); }
    { World w; Check(w.QuotaFor(1) == 600 && w.QuotaFor(2) == 840 && w.QuotaFor(3) == 1180 && w.QuotaFor(4) == 1650 && w.QuotaFor(5) == 2300, "the quota: 600, 840, 1180, 1650, 2300", Fm("%.0f %.0f %.0f", w.QuotaFor(3), w.QuotaFor(4), w.QuotaFor(5))); }
    // a day: land at Lab Alpha, pick up loot, drop it in the crate, signal the portal, extract, sell
    {
        World w; w.Init(1, 0, 0, 7); Command go; go.kind = C_START_DAY; w.crew[0].cmds.push_back(go); w.Step();
        Player& p = w.crew[0];
        Check(w.inDay && p.level == 0 && p.st == PS_ALIVE && w.LabAt(0, p.p) != nullptr, "the first day starts in Lab Alpha on Level 0");
        // walls hold
        Level& lv = w.L(0); Vector3 start = p.p; for (int i = 0; i < 300; i++) { p.in = Input{}; p.in.moveX = 1; p.in.yaw = 0; w.Step(); }
        int cx = lv.CellX(p.p.x), cz = lv.CellZ(p.p.z); Check(!lv.Solid(cx, cz), "walking into a wall stops at it");
        p.p = start;
        // a loot item put at the player's feet, then into the crate
        WorldItem wi; wi.level = 0; wi.p = Vector3Add(p.p, {0.8f, 0, 0}); wi.loot.def = 0; wi.loot.value = 30; w.items.push_back(wi);
        p.yaw = 0; p.in = Input{}; p.in.yaw = 0; p.in.use = true; w.Step(); p.in.use = false;
        bool got = p.pocket[0].def >= 0 || p.hands.def >= 0;
        LabState* lab = w.Lab(0, 0); const LabPlan& lp = lv.labs[0]; p.p = lp.spots[LP_CRATE].at; p.in = Input{}; p.in.use = true; w.Step();
        Check(got && lab->crate.size() == 1, "loot picked up, then dropped in the Lab's crate");
        p.p = lp.spots[LP_DESK].at; Command sig; sig.kind = C_PORTAL; p.cmds.push_back(sig); w.Step();
        Check(lab->charging, "the portal signal starts a 30 s charge");
        p.p = lp.spots[LP_RING].at; Run(w, 32, [&] { p.in = Input{}; });
        Check(!w.inDay && w.bay.size() == 1 && p.st == PS_SURFACE, "the portal opens and the crew goes up with the crate", Fm("bay %.0f", w.bay.size()));
        Command sell; sell.kind = C_SELL_ALL; sell.a = 0; p.cmds.push_back(sell); w.Step();
        Check(w.credit == 30 && w.cash >= D().startCash + 6, "selling to the Bureau: quota credit, and 20% as cash", Fm("credit %.0f", w.credit));
    }
    // sanity: drains alone in the dark; at zero the player is Lost and walks off to a noclip
    {
        World w; w.Init(1, 0, 0, 11); w.crew[0].cmds.push_back(Command{C_START_DAY}); w.Step();
        Player& p = w.crew[0]; Level& lv = w.L(0); const Exit* nc = nullptr; for (const auto& e : lv.exits) if (e.noclip && e.kind == "noclip") nc = &e;
        p.p = nc ? lv.Center(nc->cx, nc->cz + 0) : p.p; if (nc) { for (int dz = -3; dz <= 3; dz++) for (int dx = -3; dx <= 3; dx++) if (lv.Walkable(nc->cx + dx, nc->cz + dz) && (dx || dz) && abs(dx) + abs(dz) >= 2) { p.p = lv.Center(nc->cx + dx, nc->cz + dz); dz = 99; break; } }
        p.sanity = 0; int before = p.noclipCount; int fr = 0; Run(w, 12, [&] { p.in = Input{}; if (getenv("DEPTH_NCDBG") && fr++ % 30 == 0 && nc) std::printf("    lost %.1f san %.1f at (%.1f,%.1f) exit (%.1f,%.1f) lvl %d\n", p.lostT, p.sanity, p.p.x, p.p.z, (nc->cx + 0.5f) * CELL, (nc->cz + 0.5f) * CELL, p.level); });
        Check(nc && p.noclipCount > before, "a player at zero sanity is Lost and wanders into a noclip", Fm("level %.0f", p.level));
    }
    // a dormant Lab: the breaker opens the blast door; fuel at the generator brings it online; then a jump
    {
        World w; w.Init(1, 0, 0, 13); w.crew[0].cmds.push_back(Command{C_START_DAY}); w.Step();
        Player& p = w.crew[0]; Level& l1 = w.L(1); LabState* beta = w.Lab(1, 0);
        p.level = 1; const LabPlan& lp = l1.labs[0]; p.p = l1.Center(lp.breakerX, lp.breakerZ); p.in = Input{}; p.in.use = true; w.Step();
        bool door = beta->doorOpen;
        p.tools[3] = {ItemIndex("fuel"), 1, 0}; p.p = lp.spots[LP_GEN].at; p.in = Input{}; p.in.use = true; w.Step();
        Check(door && beta->online, "a dormant Lab: the breaker opens its door, a fuel canister brings it online");
        int betaIdx = -1; for (int i = 0; i < (int)w.labs.size(); i++) if (&w.labs[i] == beta) betaIdx = i;
        p.level = 0; Level& l0 = w.L(0); p.p = l0.labs[0].spots[LP_RING].at; Command j; j.kind = C_JUMP; j.a = betaIdx; p.cmds.push_back(j); Run(w, 11, [&] { p.in = Input{}; });
        Check(p.level == 1 && w.LabAt(1, p.p) == beta, "a jump through the network to the restarted Lab");
    }
    // entities: a Hound comes for a sprinting player; the Leviathan takes a long swimmer to Level 8; a Skin-Stealer takes a lone one
    {
        World w; w.Init(1, 0, 0, 17); w.crew[0].cmds.push_back(Command{C_START_DAY}); w.Step(); Player& p = w.crew[0]; Level& lv = w.L(0);
        Entity h; h.def = EntityIndex("hound"); h.level = 0; h.uid = 99; Vector3 hp = p.p; for (int dz = -8; dz <= 8; dz++) for (int dx = -8; dx <= 8; dx++) { int x = lv.CellX(p.p.x) + dx, z = lv.CellZ(p.p.z) + dz; if (lv.Walkable(x, z) && abs(dx) + abs(dz) > 5 && lv.At(x, z) != T_LABFLOOR) { hp = lv.Center(x, z); dz = 99; break; } }
        p.level = 0; p.p = hp; Vector3 pp = p.p; h.p = Vector3Add(pp, {6, 0, 0}); for (int t = 0; t < 40; t++) { int x = lv.CellX(pp.x) + (t % 9) - 4, z = lv.CellZ(pp.z) + (t / 9) - 2; if (lv.Walkable(x, z) && Vector3Distance(lv.Center(x, z), pp) > 4) { h.p = lv.Center(x, z); break; } }
        w.ents.push_back(h); float hurt = p.health;
        p.injuries |= IN_BLEED;   // (blood: a Hound comes to it)
        Run(w, 8, [&] { p.in = Input{}; p.stamina = 100; });
        Check(p.health < hurt || p.st != PS_ALIVE, "a Hound hears a sprinting player and bites", Fm("health %.0f", p.health));
    }
    {
        World w; w.Init(1, 0, 0, 19); w.crew[0].cmds.push_back(Command{C_START_DAY}); w.Step(); Player& p = w.crew[0];
        w.Transit(p, 7, false, "test"); Level& lv = w.L(7); for (int i = 0; i < lv.w * lv.h; i++) if (lv.tile[i] == T_DEEP) { p.p = lv.Center(i % lv.w, i / lv.w); break; }
        p.tankAir = 999; Run(w, 70, [&] { p.in = Input{}; p.stamina = 100; p.health = 100; });
        Check(p.level == 8 && p.sanity <= 12, "the Leviathan takes a swimmer after 60 s to Level 8, with nothing", Fm("level %.0f", p.level));
    }
    {
        World w; w.Init(2, 0, 0, 23); w.crew[0].cmds.push_back(Command{C_START_DAY}); w.Step(); Player& p = w.crew[0]; Player& q = w.crew[1]; Level& lv = w.L(0);
        q.p = lv.labs[0].spots[LP_RING].at;   // (the other one stays in the Lab)
        Vector3 far = p.p; for (int i = 0; i < 4000; i++) { int x = (i * 37) % lv.w, z = (i * 53) % lv.h; if (lv.Walkable(x, z) && lv.At(x, z) == T_FLOOR && Vector3Distance(lv.Center(x, z), q.p) > 40) { far = lv.Center(x, z); break; } }
        p.p = far; Entity s; s.def = EntityIndex("skinstealer"); s.level = 0; s.uid = 77; s.p = far;
        for (int t = 0; t < 60; t++) { int x = lv.CellX(far.x) + (t % 7) - 3, z = lv.CellZ(far.z) + (t / 7) - 3; if (lv.Walkable(x, z) && Vector3Distance(lv.Center(x, z), far) > 3 && w.LineOfSight(0, Vector3Add(lv.Center(x, z), {0, 1, 0}), Vector3Add(far, {0, 1.6f, 0}))) { s.p = lv.Center(x, z); break; } }
        w.ents.push_back(s);
        Run(w, 10, [&] { p.in = Input{}; q.in = Input{}; });
        bool impostor = false; for (const auto& e : w.ents) if (e.uid == 77 && e.mimicOf == p.id) impostor = true;
        Check(p.st == PS_TAKEN && impostor, "a Skin-Stealer takes a lone salvager, and wears them");
    }
    // Overtime: past 24:00 the network goes down; a night in a lit Lab is pulled out in the morning
    {
        World w; w.Init(1, 0, 0, 29); w.crew[0].cmds.push_back(Command{C_START_DAY}); w.Step(); Player& p = w.crew[0];
        p.p = w.L(0).labs[0].spots[LP_RING].at; WorldItem wi; wi.level = 0; wi.p = p.p; wi.loot.def = 0; wi.loot.value = 40; w.items.push_back(wi);
        p.in = Input{}; p.in.use = true; w.Step(); p.p = w.L(0).labs[0].spots[LP_CRATE].at; p.in = Input{}; p.in.use = true; w.Step(); p.p = w.L(0).labs[0].spots[LP_RING].at;
        w.clock = 1439; Run(w, 2, [&] { p.in = Input{}; }); bool ot = w.overtime;
        w.clock = D().dayEnd + D().dayStart - 1; Run(w, 2, [&] { p.in = Input{}; p.health = 100; p.sanity = 100; });
        Check(ot && !w.inDay && !w.bay.empty(), "Overtime: the night in a lit Lab, and the morning's extraction");
    }
    // a bot crew works a day and gets home with something
    {
        World w; w.Init(0, 3, 0, 31); uint32_t r = 5; int steps = 0; int day0 = w.day;
        while (w.day == day0 && steps < 60 * 30 * 40) { for (auto& p : w.crew) BotInput(w, p.id, p.in, p.cmds, r); w.Step(); steps++; }
        Check(w.day == day0 + 1, "a day ends exactly once (the last extraction doesn't skip a day)", Fm("day %.0f", w.day));
        Check(w.day > day0 && !w.bay.empty(), "a bot crew of three works a day and extracts with loot", Fm("bay %.0f items, %.1f min", w.bay.size(), steps * STEP / 60));
    }
    // the modes
    {   // Lost: a bot crew finds the breaker, restarts Lab Alpha, and gets out
        World w; w.Init(0, 2, 1, 37); uint32_t r = 9; int steps = 0;
        while (!w.failed && steps < 30 * 60 * 12) { for (auto& p : w.crew) BotInput(w, p.id, p.in, p.cmds, r); w.Step(); steps++; }
        Check(w.failed && w.won, "Lost: the bots work the breaker, restart Lab Alpha with the one canister and escape", Fm("%.1f min", steps * STEP / 60));
    }
    {   // Expedition: one day, then a score
        World w; w.Init(0, 3, 4, 41); uint32_t r = 11; int steps = 0;
        while (!w.failed && steps < 30 * 60 * 70) { for (auto& p : w.crew) BotInput(w, p.id, p.in, p.cmds, r); w.Step(); steps++; }
        Check(w.failed && w.won && w.score > 0, "Expedition: one long day, scored on the haul", Fm("score %.0f in %.1f min", w.score, steps * STEP / 60));
    }
    {   // Roulette: a door is a noclip to a random level
        World w; w.Init(1, 0, 2, 43); w.crew[0].cmds.push_back(Command{C_START_DAY}); w.Step(); Player& p = w.crew[0]; int n0 = p.noclipCount;
        w.Transit(p, 1, false, "door"); Check(p.noclipCount == n0 + 1 && p.level >= 0 && p.level <= 9, "Noclip Roulette: every exit noclips to a random level (0-9)", Fm("level %.0f", p.level));
    }
    {   // Skin-Stealer: with three players one is the impostor, and it can't be hurt
        World w; w.Init(3, 0, 6, 47); int imp = -1; for (auto& p : w.crew) if (p.impostor) imp = p.id;
        bool ok = imp >= 0; if (ok) { w.crew[0].cmds.push_back(Command{C_START_DAY}); w.Step(); Player& q = w.crew[imp]; float h = q.health; w.Hurt(q, 50, "test"); ok = q.health == h; }
        Check(ok, "Skin-Stealer: one of three players is the impostor and can't be hurt", Fm("impostor %.0f", imp));
    }
    {   // the Siren: an upgraded Lab's Siren wails for a while
        World w; w.Init(1, 0, 0, 53); w.labs[0].upgrades |= 1 << 6; w.crew[0].cmds.push_back(Command{C_START_DAY}); w.Step();
        w.crew[0].cmds.push_back(Command{C_SIREN}); w.Step(); Check(w.labs[0].sirenT > 0, "the Siren upgrade sounds from the desk", Fm("%.0f s", w.labs[0].sirenT));
    }
    // the levels' own rules and the last two contracts
    {   // Level 2: a dead end's hatch seals; a valve opens it
        World w; w.Init(1, 0, 0, 59); w.crew[0].cmds.push_back(Command{C_START_DAY}); w.Step(); Player& p = w.crew[0]; Level& lv = w.L(2); p.level = 2; w.Rand();
        int dx = -1, dz = -1; for (int z = 1; z < lv.h - 1 && dx < 0; z++) for (int x = 1; x < lv.w - 1; x++) { if (lv.At(x, z) != T_FLOOR) continue; int n = 0; const int D4[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}}; for (auto& d : D4) if (lv.Walkable(x + d[0], z + d[1])) n++; if (n == 1) { dx = x; dz = z; break; } }
        bool sealedOnce = false; for (int k = 0; k < 40 && !sealedOnce && dx >= 0; k++) { w.trapRooms.clear(); w.seals.clear(); p.p = lv.Center(dx, dz); w.StepMeters(p); sealedOnce = !w.seals.empty(); }
        bool opened = false; if (sealedOnce && !w.levers.empty()) { p.p = w.levers[0].at; p.yaw = 0; w.Interact(p); opened = w.seals.empty(); }
        Check(sealedOnce && opened, "Level 2: a dead end's hatch slams behind you, and a valve opens it", Fm("%.0f valves", w.levers.size()));
    }
    {   // Level 3: the breaker turns the power off (dark, quiet) and back on
        World w; w.Init(1, 0, 0, 61); w.crew[0].cmds.push_back(Command{C_START_DAY}); w.Step(); Player& p = w.crew[0]; w.L(3); p.level = 3;
        bool ok = false; if (!w.levers.empty()) { p.p = w.levers[0].at; w.Interact(p); bool off = !w.power; w.Interact(p); ok = off && w.power; }
        Check(ok, "Level 3: a breaker cuts the power and restores it", Fm("%.0f breakers", w.levers.size()));
    }
    {   // Level 5: a room ending in 3 is a trap
        World w; w.Init(1, 0, 0, 67); w.crew[0].cmds.push_back(Command{C_START_DAY}); w.Step(); Player& p = w.crew[0]; Level& lv = w.L(5); p.level = 5;
        int tx = -1, tz = -1; for (int z = 0; z < lv.h && tx < 0; z++) for (int x = 0; x < lv.w; x++) if (lv.At(x, z) == T_DOOR && World::RoomNumber(x, z) % 10 == 3) { tx = x; tz = z; break; }
        size_t before = w.ents.size(); float h = p.health; if (tx >= 0) { p.p = lv.Center(tx, tz); w.StepMeters(p); }
        Check(tx >= 0 && p.health < h && w.ents.size() > before, "Level 5: a room ending in 3 is a trap (a hurt and a Hound)", Fm("room %.0f", tx >= 0 ? World::RoomNumber(tx, tz) : 0));
    }
    {   // Level 17: the lower decks flood through the day
        World w; w.Init(1, 0, 0, 71); w.crew[0].cmds.push_back(Command{C_START_DAY}); w.Step(); Level& lv = w.L(17); int r0 = w.FloodRow(lv); w.clock = D().dayEnd - 30; int r1 = w.FloodRow(lv);
        Check(r0 >= lv.h - 1 && r1 < lv.h - lv.h / 3, "Level 17: the lower decks flood as the day goes on", Fm("row %.0f of %.0f by evening", r1, lv.h));
    }
    {   // Rescue: the survivor follows the bot crew home
        World w; w.Init(0, 1, 0, 73); w.contract = ContractIndex("rescue"); w.contractLevel = 0; w.crew[0].cmds.push_back(Command{C_START_DAY}); w.Step();
        int si = -1; for (int i = 0; i < (int)w.ents.size(); i++) if (D().entities[w.ents[i].def].id == "survivor") si = i;
        uint32_t r = 3; int steps = 0; int day0 = w.day; while (w.day == day0 && steps < 30 * 60 * 30) { for (auto& p : w.crew) BotInput(w, p.id, p.in, p.cmds, r); w.Step(); steps++;
            if (getenv("DEPTH_RESCUETRACE") && steps % 900 == 0) { const Entity* s = nullptr; for (const auto& e : w.ents) if (D().entities[e.def].id == "survivor") s = &e; const Player& b = w.crew[0]; std::printf("    t %.0f bot L%d (%.0f,%.0f) st %d | survivor %s tgt %d (%.0f,%.0f) d %.1f | clock %.0f\n", steps * STEP, b.level, b.p.x, b.p.z, (int)b.st, s ? "yes" : "no", s ? s->target : -9, s ? s->p.x : 0, s ? s->p.z : 0, s ? Vector3Distance(s->p, b.p) : 0, w.clock); } }
        si = -1; for (int i = 0; i < (int)w.ents.size(); i++) if (D().entities[w.ents[i].def].id == "survivor") si = i;
        bool paid = w.credit >= 400; std::string why = si < 0 ? Fm("credit %.0f", w.credit) : Fm("credit %.0f, survivor following %.0f, on level %.0f", w.credit, si >= 0 ? w.ents[si].target : -9, si >= 0 ? w.ents[si].level : -9);
        Check(si >= 0 && paid, "Rescue: a bot finds the survivor, who follows it to the portal (the contract pays)", why);
    }
    {   // the Party: accept, and a teammate fetches you back
        World w; w.Init(2, 0, 0, 79); w.crew[0].cmds.push_back(Command{C_START_DAY}); w.Step(); Player& p = w.crew[0]; Player& q = w.crew[1];
        Entity e; e.def = EntityIndex("partygoer"); e.level = p.level; e.p = Vector3Add(p.p, {1, 0, 0}); e.uid = 999; w.ents.push_back(e);
        p.cmds.push_back(Command{C_ACCEPT}); w.Step(); bool there = p.atParty && p.level == 5;
        q.level = 5; q.p = Vector3Add(p.p, {0.8f, 0, 0}); w.Interact(q);
        Check(there && !p.atParty && p.partied, "the Party: an invitation sends you to the ballroom, and a teammate pulls you out");
    }
    std::printf(gFails ? "NOCLIP: %d check(s) FAILED\n" : "NOCLIP: all checks passed\n", gFails);
    return gFails ? 1 : 0;
}

int RunNoclipSim(int crewN, int days, int runs) {
    std::map<std::string, int> causes; double credit = 0; int quotaMet = 0, weeks = 0; std::map<int, int> levels; int deaths = 0; double extracted = 0; int dayCount = 0;
    for (int r = 0; r < runs; r++) {
        World w; w.Init(0, crewN, 0, 100 + r); uint32_t rng = 3 + r; int steps = 0;
        int targetDay = days + 1;
        while (!w.failed && (w.week - 1) * D().daysPerWeek + w.day < targetDay + D().daysPerWeek * 0 && steps < 60 * 30 * 60 * days) {
            for (auto& p : w.crew) BotInput(w, p.id, p.in, p.cmds, rng);
            size_t e0 = w.evCount; w.Step(); steps++;
            for (const auto& p : w.crew) if (p.Alive()) levels[p.level]++;
            if (getenv("DEPTH_SIMLOG") && steps % 9000 == 0) { std::printf("      step %d: day %d inDay %d clock %.0f ot %d |", steps, w.day, (int)w.inDay, w.clock, (int)w.overtime); for (const auto& p : w.crew) std::printf(" [L%d st%d %.0f,%.0f h%.0f]", p.level, (int)p.st, p.p.x, p.p.z, p.health); std::printf(" bay %d crate %d\n", (int)w.bay.size(), w.CrateTotal()); }
            uint32_t fresh = w.evCount - (uint32_t)e0; for (size_t k = w.events.size() - std::min<size_t>(fresh, w.events.size()); k < w.events.size(); k++) { const Event& e = w.events[k]; if (e.kind == E_DIED || e.kind == E_TAKEN) { deaths++; causes[e.s]++; } if (e.kind == E_DAY_END) dayCount++; if (e.kind == E_SALE && e.by == 0) credit += e.a; if (e.kind == E_QUOTA) quotaMet++; }
            if (w.week > 1 && weeks < w.week - 1) weeks = w.week - 1;
            if ((w.week - 1) * D().daysPerWeek + w.day > days) break;
        }
        extracted += w.credit;
        if (getenv("DEPTH_SIMLOG")) std::printf("    run %d: week %d day %d, credit %d of %d, cash %d, failed %d, bay %d\n", r, w.week, w.day, w.credit, w.quota, w.cash, (int)w.failed, (int)w.bay.size());
    }
    std::printf("NOCLIP sim: %d runs, a bot crew of %d, %d days\n", runs, crewN, days);
    std::printf("  quota credit per day: %.0f; weeks' quotas met: %d; deaths: %d\n", credit / std::max(1, dayCount), quotaMet, deaths);
    for (auto& c : causes) std::printf("    died by %-32s %d\n", c.first.c_str(), c.second);
    std::printf("  time spent by level:"); double tot = 0; for (auto& l : levels) tot += l.second; for (auto& l : levels) std::printf(" L%d %.0f%%", l.first, 100.0 * l.second / std::max(1.0, tot)); std::printf("\n");
    return 0;
}

}  // namespace nc
