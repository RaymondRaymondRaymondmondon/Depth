// Fathoms' neutral powers (doc pp. 23-26): pirate coves (black market, raid contracts with counter-bids, reputation,
// trade raids, storming the cove and Blackbeard), tribal towns (raids, growth, tribute, Kinship, conquest), volcano
// islands (the Sun God and his imps, eruptions on lava paths, ash, the altar), sunken ruins and their relics, wrecks
// (traps), the Ghost Pirate Ship, the Kraken, and the weather. Every timer is the doc's (data file), scaled by the lobby's
// Neutral Power setting (HP here, damage in StepCombat).
#include "fathoms.h"
#include <algorithm>
#include <cmath>

namespace fa {

static float Str(const World& w) { return B().neutralStrength[std::clamp(w.set.neutral, 0, 2)]; }
static int SpawnNeutral(World& w, int owner, const char* key, Vector2 p, int site) {
    int def = B().Unit(key); if (def < 0) return -1;
    int id = w.SpawnUnit(owner, def, p); Unit* u = w.U(id); if (!u) return -1;
    u->hp *= Str(w); u->home = site; u->stance = ST_DEFENSIVE; u->patrolA = p; return id;
}
// a free tile of the right kind near a point (land for walkers, water for ships)
static Vector2 FreeNear(const World& w, Vector2 at, bool water, float spread = 3) {
    for (int r = 0; r < 14; r++) for (int a = 0; a < 16; a++) {
        float an = a * 0.3927f + r * 0.7f, rr = r * 0.8f + spread * 0.3f; int x = (int)(at.x + cosf(an) * rr), y = (int)(at.y + sinf(an) * rr);
        if (!w.In(x, y)) continue; int t = w.tile[w.Idx(x, y)];
        if (water ? w.Water(t) : (w.Land(t) && w.occ[w.Idx(x, y)] < 0)) return {x + 0.5f, y + 0.5f};
    }
    return at;
}
static int Living(const World& w, int site, const char* key = nullptr) {
    int n = 0, def = key ? B().Unit(key) : -1;
    for (const auto& u : w.units) if (!u.dead && u.home == site && !IsPlayer(u.owner) && (def < 0 || u.def == def)) n++;
    return n;
}
static int MaxEra(const World& w) { int e = 0; for (const auto& p : w.players) if (p.alive) e = std::max(e, p.era); return e; }
static float RepMult(const Site& s, int p) { return 1 - 0.05f * floorf(s.rep[p] / 25.0f); }   // (every 25 reputation shifts prices 5%)

// ---------------------------------------------------------------- set-up
void World::InitSites() {
    const Balance& Bl = B(); sites.clear(); relicsTotal = 0;
    for (size_t i = 0; i < islands.size(); i++) {
        Island& is = islands[i]; Site s; s.island = (int)i; s.p = is.c; int si = (int)sites.size();
        if (is.kind == I_COVE && set.pirates) {
            s.kind = S_COVE; s.hp = s.maxHp = Bl.coveHp * Str(*this); sites.push_back(s);
            for (int k = 0; k < Bl.covePirates; k++) SpawnNeutral(*this, OWN_PIRATE, "pirate", FreeNear(*this, is.c, false, 2 + k * 0.3f), si);
            for (int k = 0; k < 2; k++) SpawnNeutral(*this, OWN_PIRATE, "sloop", FreeNear(*this, Vector2Add(is.c, {is.r + 3.0f * (k ? 1 : -1), is.r}), true), si);
        } else if (is.kind == I_TRIBAL && set.tribes) {
            s.kind = S_TRIBE; s.hp = s.maxHp = Bl.tribeHp * Str(*this); s.large = (int)(i % 2); sites.push_back(s);
            for (int k = 0; k < Bl.tribeWarriors; k++) SpawnNeutral(*this, OWN_TRIBE, "tribal_warrior", FreeNear(*this, is.c, false, 2), si);
            for (int k = 0; k < Bl.tribeThrowers; k++) SpawnNeutral(*this, OWN_TRIBE, "tribal_thrower", FreeNear(*this, is.c, false, 2), si);
            if (s.large) SpawnNeutral(*this, OWN_TRIBE, i % 4 == 1 ? "chieftain" : "witch_doctor", FreeNear(*this, is.c, false, 1), si);
        } else if (is.kind == I_VOLCANO && set.volcanoes) {
            s.kind = S_ALTAR; s.hp = s.maxHp = 1; s.state = 0; s.t = Bl.volcanoWake;   // t: the next eruption
            // three lava paths from the summit to the sea, the vents beside them
            float a0 = Rand() * 6.283f;
            for (int k = 0; k < 3; k++) { float an = a0 + k * 2.094f;
                for (float r = 2.5f; r < is.r * 1.3f; r += 0.5f) { int x = (int)(is.c.x + cosf(an) * r), y = (int)(is.c.y + sinf(an) * r); if (!In(x, y)) continue; int t = tile[Idx(x, y)]; if (Water(t)) break; if (t == T_MOUNTAIN) continue;
                    Vector2 c{(float)x, (float)y}; bool dup = false; for (auto& q : s.lava) if (q.x == c.x && q.y == c.y) dup = true; if (!dup) s.lava.push_back(c); } }
            sites.push_back(s);
            SpawnNeutral(*this, OWN_WILD, "sun_god", FreeNear(*this, is.c, false, 1), si);
            for (int k = 0; k < Bl.imps; k++) SpawnNeutral(*this, OWN_WILD, "fire_imp", FreeNear(*this, is.c, false, 3), si);
        } else if (is.kind == I_RUIN) {
            s.kind = S_RUIN; s.relic = 1; s.hp = s.maxHp = 1; sites.push_back(s); relicsTotal++;
            for (int k = 0; k < Bl.ruinSentinels; k++) SpawnNeutral(*this, OWN_WILD, "sentinel", FreeNear(*this, is.c, false, 2), si);
        }
    }
    // the Kraken sleeps in the central sea; the Ghost Ship comes later (Medium and Large maps)
    if (set.kraken) { Site k; k.kind = S_KRAKEN; k.p = FreeNear(*this, {W * 0.5f + W * 0.18f, H * 0.5f}, true); k.state = 0; sites.push_back(k); }
    if (set.players > 2) { Site g; g.kind = S_GHOST; g.p = FreeNear(*this, {W * 0.15f, H * 0.5f}, true); g.state = 0; sites.push_back(g); relicsTotal++; }
}

// ---------------------------------------------------------------- hostility (truces, contracts, the cove's grudges)
bool NeutralHostile(const World& w, const Unit& u, int other) {
    if (IsPlayer(u.owner) || !IsPlayer(other)) return true;
    if (u.hire >= 0) return other == u.hire;
    if (u.home < 0 || u.home >= (int)w.sites.size()) return true;
    const Site& s = w.sites[u.home];
    if (u.owner == OWN_TRIBE) return other >= (int)w.players.size() || s.peace[other] == 0;
    if (u.owner == OWN_PIRATE) return other < (int)w.players.size() && (s.rep[other] < -50 || s.owner >= 0 && s.owner != other && false);
    if (s.kind == S_ALTAR && other < (int)w.players.size() && w.players[other].faction == 1 && HasWonder(w, other)) return false;   // (Temple of the Sun)
    return true;
}
void NeutralDamaged(World& w, Unit& v, int by, float d) {
    if (v.home < 0 || v.home >= (int)w.sites.size() || !IsPlayer(by)) return;
    Site& s = w.sites[v.home];
    if (s.kind == S_KRAKEN && by < MAX_PLAYERS) s.rep[by] += d;   // (the Kraken's tally: who dealt the most)
    if (s.kind == S_TRIBE && s.peace[by] == 0) s.lastAttacker = by;
    if (s.kind == S_TRIBE && s.peace[by] >= 1) { s.peace[by] = 0; w.Emit(EV_DIPLO, s.p, by, -1, 0); }   // (striking the town ends the truce)
}
void NeutralDied(World& w, Unit& u, int by) {
    const Balance& Bl = B(); const UnitDef& d = w.UD(u);
    if (u.home < 0 || u.home >= (int)w.sites.size()) return;
    Site& s = w.sites[u.home];
    if (IsPlayer(u.owner)) return;
    if (u.owner == OWN_PIRATE && IsPlayer(by) && by < (int)w.players.size() && (d.tags & TG_SHIP) && s.kind == S_COVE) s.rep[by] = std::max(-100.0f, s.rep[by] - 30);
    if (d.key == "blackbeard" && IsPlayer(by)) { s.owner = by; s.state = 3; w.players[by].bossScore += Bl.scBoss[0]; w.Emit(EV_PIRATE_ARRIVE, s.p, by, 1); }
    if (d.key == "sun_god") { s.state = 2; s.t2 = w.t; if (IsPlayer(by)) w.players[by].bossScore += Bl.scBoss[1]; w.Emit(EV_ERUPT, s.p, by, 2); }
    if (d.key == "kraken") {
        int best = -1; float bd = 0; for (int p = 0; p < (int)w.players.size(); p++) if (s.rep[p] > bd) { bd = s.rep[p]; best = p; }
        if (best >= 0) { Player& P = w.players[best]; for (int r = 0; r < R_COUNT; r++) P.res[r] += Bl.krakenReward[r]; P.bossScore += Bl.scBoss[2]; P.krakenInk = true;
            for (auto& o : w.units) if (!o.dead && o.owner == best && (w.UD(o).tags & TG_SHIP)) o.hiddenT = 60; }   // (Kraken Ink: the fleet unseen for 60 s)
        s.state = 3; w.Emit(EV_KRAKEN, u.p, best, 2);
    }
    if (d.key == "ghost_ship") {
        if (IsPlayer(by) && by < (int)w.players.size()) { w.players[by].res[R_DOUB] += Bl.ghostReward; }
        Site r; r.kind = S_RUIN; r.p = FreeNear(w, u.p, true); r.relic = 1; r.state = 2; w.sites.push_back(r);   // (its relic floats where it sank)
        w.sites[u.home].state = 3; w.Emit(EV_GHOST, u.p, by, 2);
    }
}
// a trapped wreck: three drowned sailors, or a Ghost Worm that was burrowed under it
void SpringTrap(World& w, Node& n) {
    if (w.Rand() < 0.5f) for (int k = 0; k < 3; k++) { int id = w.SpawnUnit(OWN_WILD, B().Unit("drowned_sailor"), Vector2Add(n.p, {k * 0.6f - 0.6f, 0.4f})); if (Unit* u = w.U(id)) u->hp *= Str(w); }
    else { int id = w.SpawnUnit(OWN_WILD, B().Unit("ghost_worm"), n.p); if (Unit* u = w.U(id)) u->hp *= Str(w); }
    w.Emit(EV_ATTACKED, n.p, -1, 0, 1);
}

// ---------------------------------------------------------------- commands: the pirates' deals, the black market, tribes
static int CoveIndex(const World& w, int i) { return i >= 0 && i < (int)w.sites.size() && w.sites[i].kind == S_COVE ? i : -1; }
static bool PayDoub(Player& P, float price) { if (P.res[R_DOUB] + 1e-3f < price) return false; P.res[R_DOUB] -= price; return true; }
static void SpawnRaid(World& w, Contract& k) {
    const Balance& Bl = B(); const Balance::Deal& D = Bl.deals[std::clamp(k.deal, 0, (int)Bl.deals.size() - 1)]; Site& s = w.sites[k.cove];
    // the target: the victim's Harbor (or any building)
    Vector2 tgt = s.p; for (const auto& b : w.buildings) if (!b.dead && b.owner == k.target) { tgt = b.Centre(); if (w.BD(b).key == "harbor") break; }
    auto ship = [&](const char* key) { int id = SpawnNeutral(w, OWN_PIRATE, key, FreeNear(w, s.p, true, 5), k.cove); if (Unit* u = w.U(id)) { u->hire = k.target; u->life = D.time; u->order = O_ATTACK_MOVE; u->goal = tgt; u->stance = ST_AGGRESSIVE; k.units.push_back(id); } };
    for (int i = 0; i < D.sloops; i++) ship("sloop");
    for (int i = 0; i < D.gunboats; i++) ship("gunboat");
    for (int i = 0; i < D.ironclads; i++) ship("ironclad");
    // the landing party comes ashore near the target (the Transport is drawn going in)
    Vector2 shore = FreeNear(w, tgt, false, 6);
    for (int i = 0; i < D.pirates; i++) { int id = SpawnNeutral(w, OWN_PIRATE, "pirate", FreeNear(w, shore, false, 2), k.cove); if (Unit* u = w.U(id)) { u->hire = k.target; u->life = D.time; u->order = O_ATTACK_MOVE; u->goal = tgt; u->stance = ST_AGGRESSIVE; u->landedT = w.t; k.units.push_back(id); } }
    k.live = true; k.liveT = w.t; w.Emit(EV_PIRATE_ARRIVE, tgt, k.target, k.hirer);
}
bool NeutralCommand(World& w, const Command& c) {
    const Balance& Bl = B(); Player& P = w.players[c.player];
    if (c.kind == C_HIRE) {
        int ci = CoveIndex(w, c.a); if (ci < 0) return false; Site& s = w.sites[ci];
        if (s.hp <= 0 && s.state != 3) return false;
        if (s.rep[c.player] < -50 && s.owner != c.player) return false;   // (the cove refuses you)
        float fee = s.owner >= 0 && s.owner != c.player ? 1.25f : 1.0f, own = s.owner == c.player ? 0.5f : 1.0f;
        float mult = RepMult(s, c.player) * fee * own;
        auto payOwner = [&](float price) { if (s.owner >= 0 && s.owner != c.player && s.owner < (int)w.players.size()) w.players[s.owner].res[R_DOUB] += price * 0.25f / 1.25f; };
        int r = std::clamp(c.x, 0, 3);
        switch (c.b) {
            case 0: case 1: case 2: {   // a raid contract on a player
                const Balance::Deal& D = Bl.deals[c.b]; if (D.era > P.era) return false;
                int tg = c.target; if (tg < 0 || tg >= (int)w.players.size() || tg == c.player || w.Allied(tg, c.player) || !w.players[tg].alive) return false;
                for (const auto& k : w.contracts) if (k.target == tg && (k.warnT > w.t || k.live)) return false;   // (one per target at a time)
                if (w.t - s.t2 < Bl.contractEvery && s.t2 > 0) return false;                                   // (each cove: one every 90 s)
                float price = D.price * mult * (1 + w.TechSum(c.player, "pirate_price"));
                if (!PayDoub(P, price)) return false; payOwner(price);
                Contract k; k.cove = ci; k.hirer = c.player; k.target = tg; k.deal = c.b; k.price = price; k.warnT = w.t + Bl.raidWarning; w.contracts.push_back(k);
                s.t2 = w.t; s.rep[c.player] = std::min(100.0f, s.rep[c.player] + 5);
                w.Emit(EV_PIRATE_WARN, s.p, tg, (int)w.contracts.size() - 1, (int)Bl.raidWarning);
                return true; }
            case 3: {   // intel: a player's base revealed 30 s (100), or a region (60)
                int tg = c.target; float price = (tg >= 0 ? 100 : 60) * mult; if (!PayDoub(P, price)) return false; payOwner(price);
                if (tg >= 0 && tg < (int)w.players.size()) P.intelUntil[tg] = w.t + 30; else P.intelUntil[c.player] = w.t + 30;
                return true; }
            case 4: {   // black market: sell 100 of Food/Brass/Coal for 20 Doubloons
                if (r > 2 || P.res[r] < 100) return false; P.res[r] -= 100; P.res[R_DOUB] += Bl.bmSell / fee; s.rep[c.player] = std::min(100.0f, s.rep[c.player] + 1); return true; }
            case 5: {   // black market: buy 100 Food/Brass/Coal for 60 Doubloons, or 40 Ichor for 100 (the only way to buy Ichor)
                float price = (r == R_ICHOR ? Bl.bmIchor : Bl.bmBuy) * mult; if (!PayDoub(P, price)) return false; payOwner(price);
                P.res[r] += r == R_ICHOR ? 40 : 100; s.rep[c.player] = std::min(100.0f, s.rep[c.player] + 1); return true; }
        }
        return false;
    }
    if (c.kind == C_BID) {   // counter-bids on a contract during its warning: 0 call off (125%), 1 send back (150%), 2 the hirer tops (+25%)
        if (c.a < 0 || c.a >= (int)w.contracts.size()) return false; Contract& k = w.contracts[c.a];
        if (k.live || k.warnT <= w.t || k.bids >= 2) return false;
        // (k.deal: the deal index; -1000 + it while called off; 100 + it while turned back on the hirer, who is then k.target)
        bool off = k.deal < 0, back = k.deal >= 100; int orig = back ? k.target : k.hirer;
        if (c.b == 2) { if (c.player != orig || (!off && !back)) return false; float p = k.price * 1.25f; if (!PayDoub(P, p)) return false; k.price = p; k.bids++;
            if (off) k.deal += 1000; else { k.deal -= 100; std::swap(k.hirer, k.target); } return true; }
        if (off || back) return false;
        if (c.player != k.target) return false;
        float p = k.price * (c.b == 0 ? 1.25f : 1.5f); if (!PayDoub(P, p)) return false;
        k.price = p; k.bids++;
        if (c.b == 0) k.deal -= 1000;                              // (called off: marked, can be reinstated by the hirer's top)
        else { k.deal += 100; std::swap(k.hirer, k.target); }   // (sent back at whoever hired them)
        w.Emit(EV_DIPLO, w.sites[k.cove].p, c.player, k.hirer, 9);
        return true;
    }
    if (c.kind == C_TRIBE) {
        if (c.a < 0 || c.a >= (int)w.sites.size() || w.sites[c.a].kind != S_TRIBE || w.sites[c.a].hp <= 0) return false; Site& s = w.sites[c.a];
        switch (c.b) {
            case 0: {   // tribute: a truce, paid now and every 5 minutes
                if (s.peace[c.player] >= 1) return false; for (int r = 0; r < R_COUNT; r++) if (P.res[r] < Bl.tribute[r]) return false;
                for (int r = 0; r < R_COUNT; r++) P.res[r] -= Bl.tribute[r]; s.peace[c.player] = 1; s.tributeT[c.player] = w.t + Bl.tribeTributeEvery; return true; }
            case 1: {   // buy a Tribal Warrior at truce (50 Food, at most 6 alive)
                if (s.peace[c.player] < 1 || P.res[R_FOOD] < 50) return false; int alive = 0; for (const auto& u : w.units) if (!u.dead && u.owner == c.player && u.home == c.a) alive++;
                if (alive >= Bl.allyCap) return false; P.res[R_FOOD] -= 50;
                int id = w.SpawnUnit(c.player, Bl.Unit("tribal_warrior"), FreeNear(w, s.p, false, 3)); if (Unit* u = w.U(id)) { u->home = c.a; if (w.HasTech(c.player, "blood_oath")) u->hp *= 1.25f; } return true; }
            case 2: {   // Kinship: a Shaman's 45 s ritual with a gift of 150 Food and 100 Doubloons (Islanders)
                if (P.faction != 1 || s.peace[c.player] == 2 || c.units.empty()) return false; Unit* sh = w.U(c.units[0]);
                if (!sh || sh->owner != c.player || w.UD(*sh).special != "kinship") return false;
                for (int r = 0; r < R_COUNT; r++) if (P.res[r] < Bl.kinship[r]) return false;
                for (int r = 0; r < R_COUNT; r++) P.res[r] -= Bl.kinship[r];
                s.peace[c.player] = std::max(s.peace[c.player], 1); s.tributeT[c.player] = w.t + Bl.tribeTributeEvery;   // (a truce while the ritual lasts)
                sh->order = O_RITUAL; sh->target = c.a; sh->targetKind = 3; sh->chan = 0; sh->path.clear(); return true; }
            case 3: { if (s.peace[c.player] != 1) return false; s.peace[c.player] = 0; return true; }   // (stop paying)
        }
    }
    return false;
}

// ---------------------------------------------------------------- the step
static void StepCove(World& w, Site& s, int si) {
    const Balance& Bl = B();
    if (s.hp > 0) {
        // shore cannons: four, at whoever the cove is hostile to
        s.t -= STEP;
        if (s.t <= 0) { s.t = 2.5f; int shots = 0;
            for (auto& u : w.units) { if (shots >= Bl.coveCannons) break; if (u.dead || !IsPlayer(u.owner) || u.inside >= 0 || u.garrisoned >= 0) continue; if (Vector2Distance(u.p, s.p) > Bl.coveRange + 2) continue;
                if (u.owner >= (int)w.players.size() || s.rep[u.owner] >= -50) continue;
                float dmg = std::max(1.0f, Bl.coveCannon * Str(w) - w.UD(u).armor[1]); w.pending.push_back({-1, OWN_PIRATE, u.id, 0, dmg, D_PIERCE, 0.5f, u.p, 0, 0}); w.shots.push_back({s.p, u.p, 0, 0.5f, 1, 2.0f}); shots++; } }
        // the pirates come back to the cove when idle and far away
        return;
    }
    // stormed: Blackbeard comes out (once), calls 4 pirates every 30 s until he falls
    if (s.state == 0) { s.state = 1; int id = SpawnNeutral(w, OWN_PIRATE, "blackbeard", FreeNear(w, s.p, false, 1), si); if (Unit* u = w.U(id)) { u->stance = ST_AGGRESSIVE; } s.t = 30; for (int p = 0; p < (int)w.players.size(); p++) s.rep[p] = std::min(s.rep[p], -60.0f); }
    if (s.state == 1) { s.t -= STEP; if (s.t <= 0) { s.t = 30; for (int k = 0; k < 4; k++) SpawnNeutral(w, OWN_PIRATE, "pirate", FreeNear(w, s.p, false, 2), si); } }
}
static void StepTribe(World& w, Site& s, int si) {
    const Balance& Bl = B();
    if (s.state == 3) return;   // (conquered)
    if (s.hp <= 0) {
        s.state = 3; int by = s.lastAttacker;
        for (auto& u : w.units) if (!u.dead && u.home == si && u.owner == OWN_TRIBE) w.Kill(u, by);
        if (IsPlayer(by) && by < (int)w.players.size()) { Player& P = w.players[by]; for (int r = 0; r < R_COUNT; r++) P.res[r] += Bl.tribeLoot[r]; P.bossScore += Bl.scBoss[3];
            if (w.Rand() < Bl.relicChance) { Site r; r.kind = S_RUIN; r.p = FreeNear(w, s.p, false, 2); r.relic = 1; r.state = 2; w.sites.push_back(r); w.relicsTotal++; } }
        w.Emit(EV_RAZED, s.p, -1000 - si, by, 0); return;
    }
    // tribute falls due every 5 minutes; a truce ends if it can't be paid
    for (int p = 0; p < (int)w.players.size(); p++) if (s.peace[p] == 1 && w.t >= s.tributeT[p]) {
        Player& P = w.players[p]; bool ok = true; for (int r = 0; r < R_COUNT; r++) if (P.res[r] < Bl.tribute[r]) ok = false;
        if (ok) { for (int r = 0; r < R_COUNT; r++) P.res[r] -= Bl.tribute[r]; s.tributeT[p] = w.t + Bl.tribeTributeEvery; } else s.peace[p] = 0;
    }
    // an ally's town makes a free Tribal Warrior every 60 s (30 with the Temple of the Sun), up to 6
    for (int p = 0; p < (int)w.players.size(); p++) if (s.peace[p] == 2) {
        s.tributeT[p] -= STEP * (HasWonder(w, p) && w.players[p].faction == 1 ? 2.0f : 1.0f);
        if (s.tributeT[p] <= 0) { s.tributeT[p] = Bl.allyEvery; int alive = 0; for (const auto& u : w.units) if (!u.dead && u.owner == p && u.home == si) alive++;
            if (alive < Bl.allyCap) { int id = w.SpawnUnit(p, Bl.Unit("tribal_warrior"), FreeNear(w, s.p, false, 3)); if (Unit* u = w.U(id)) { u->home = si; if (w.HasTech(p, "blood_oath")) u->hp *= 1.25f; } } }
    }
    // growth: a warrior a minute while ignored (cap 16)
    s.t += STEP;
    if (s.t >= Bl.tribeGrowth) { s.t = 0; if (Living(w, si) < Bl.tribeCap) SpawnNeutral(w, OWN_TRIBE, w.Rand() < 0.7f ? "tribal_warrior" : "tribal_thrower", FreeNear(w, s.p, false, 2), si); }
    // raids: from minute 6, every 5 minutes, at whoever last attacked it or the nearest player not at peace
    if (w.t >= Bl.tribeWake) {
        s.t2 -= STEP;
        if (s.t2 <= 0) {
            s.t2 = Bl.tribeRaidEvery; int target = -1;
            if (s.lastAttacker >= 0 && s.lastAttacker < (int)w.players.size() && w.players[s.lastAttacker].alive && s.peace[s.lastAttacker] == 0) target = s.lastAttacker;
            Vector2 at{}; float bd = 1e9f;
            for (const auto& b : w.buildings) { if (b.dead || !IsPlayer(b.owner) || b.owner >= (int)w.players.size() || s.peace[b.owner] != 0) continue; if (target >= 0 && b.owner != target) continue; float d = Vector2Distance(b.Centre(), s.p); if (d < bd) { bd = d; at = b.Centre(); if (target < 0) target = b.owner; } }
            if (target >= 0 && bd < 1e8f) {
                int n = Bl.raidSize[std::clamp(MaxEra(w), 0, 2)];
                // the war canoes land them near the target (drawn going in)
                Vector2 shore = FreeNear(w, at, false, 7);
                for (int k = 0; k < n; k++) { int id = SpawnNeutral(w, OWN_TRIBE, k == 0 && MaxEra(w) >= 2 ? "chieftain" : "tribal_warrior", FreeNear(w, shore, false, 2), si); if (Unit* u = w.U(id)) { u->hire = target; u->order = O_ATTACK_MOVE; u->goal = at; u->stance = ST_AGGRESSIVE; u->landedT = w.t; u->life = 150; } }
                w.Emit(EV_TRIBE_RAID, at, target, si, n);
            }
        }
    }
}
static void StepAltar(World& w, Site& s, int si) {
    const Balance& Bl = B();
    // eruptions: from minute 10, every 4 minutes; a 30 s tremor, 40 s of lava on the three paths, then 60 s of ash
    if (w.t >= s.t - Bl.eruptWarning && s.state < 10 && w.t < s.t) { if (s.count != 1) { s.count = 1; w.Emit(EV_TREMOR, s.p, si, (int)Bl.eruptWarning); } }
    if (w.t >= s.t && s.count == 1) {
        s.count = 2; w.Emit(EV_ERUPT, s.p, si, 1);
        for (auto& q : s.lava) { int k = w.Idx((int)q.x, (int)q.y); w.tile[k] = T_LAVA; int o = w.occ[k]; if (o >= 0) if (Building* b = w.Bd(o)) w.Raze(*b, -1); }
    }
    if (s.count == 2 && w.t >= s.t + Bl.lavaTime) {
        s.count = 3; for (auto& q : s.lava) { int k = w.Idx((int)q.x, (int)q.y); w.tile[k] = w.height[k] > 1.4f ? T_HILL : T_GRASS; }
    }
    if (s.count == 3 && w.t >= s.t + Bl.lavaTime + Bl.ash) { s.count = 0; s.t += Bl.eruptEvery; }
    // the Sun God: a fire-beam in a line every 6 s at the nearest enemy, Strengthen on his imps
    for (auto& u : w.units) if (!u.dead && u.home == si && w.UD(u).special == "beam") {
        for (auto& o : w.units) if (!o.dead && o.home == si && o.id != u.id && Vector2Distance(o.p, u.p) < 8) o.strongT = std::max(o.strongT, 0.2f);
        if (u.abilityT > 0) continue;
        const Unit* tg = nullptr; float bd = 8;
        for (const auto& o : w.units) if (!o.dead && IsPlayer(o.owner) && o.inside < 0 && o.garrisoned < 0 && NeutralHostile(w, u, o.owner)) { float d = Vector2Distance(o.p, u.p); if (d < bd) { bd = d; tg = &o; } }
        if (!tg) continue;
        Vector2 a = u.p, dir = Vector2Normalize(Vector2Subtract(tg->p, a)), b = Vector2Add(a, Vector2Scale(dir, 9));
        for (auto& o : w.units) { if (o.dead || !IsPlayer(o.owner) || o.inside >= 0) continue; Vector2 ap = Vector2Subtract(o.p, a); float along = Vector2DotProduct(ap, dir); if (along < 0 || along > 9) continue; float off = fabsf(ap.x * dir.y - ap.y * dir.x); if (off < 1.0f) w.Damage(u.id, OWN_WILD, o.id, 0, Bl.sunGodBeam * Str(w), D_BLAST); }
        w.shots.push_back({a, b, 0, 0.6f, 3, 1.5f}); u.abilityT = 6; w.Emit(EV_SHOT, a, u.id, D_BLAST);
    }
    // the altar: held by standing on it 45 s uncontested once the Sun God is dead; +1 Ichor/s to its holder
    bool god = Living(w, si, "sun_god") > 0;
    if (!god) {
        int who = -1; bool contested = false;
        for (const auto& u : w.units) if (!u.dead && u.inside < 0 && Vector2Distance(u.p, s.p) < 2.5f) { if (!IsPlayer(u.owner)) { contested = true; continue; } if (who < 0) who = u.owner; else if (!w.Allied(who, u.owner)) contested = true; }
        if (who >= 0 && !contested && who != s.holder) { if (s.owner != who) { s.owner = who; s.holdT = 0; } s.holdT += STEP; if (s.holdT >= Bl.altarHold) { s.holder = who; w.Emit(EV_CLAIM, s.p, who, -1, 1); } }
        else if (who < 0) { s.holdT = 0; s.owner = -1; }
        // he reforms 8 minutes after his defeat if nobody holds the altar
        if (s.holder < 0 && s.state == 2 && w.t - s.t2 > Bl.sunGodReform) { s.state = 0; SpawnNeutral(w, OWN_WILD, "sun_god", FreeNear(w, s.p, false, 1), si); for (int k = 0; k < Bl.imps; k++) SpawnNeutral(w, OWN_WILD, "fire_imp", FreeNear(w, s.p, false, 3), si); }
    }
    if (s.holder >= 0 && s.holder < (int)w.players.size()) { w.players[s.holder].res[R_ICHOR] += Bl.altarIchor * STEP; w.players[s.holder].altarT += STEP; }
}
static void StepRuin(World& w, Site& s, int si) {
    if (s.relic <= 0) return;
    if (s.state != 2 && Living(w, si, "sentinel") > 0) return;
    // a land unit (or a ship, for a relic floating at sea) standing on it picks the relic up
    for (auto& u : w.units) if (!u.dead && IsPlayer(u.owner) && u.inside < 0 && u.garrisoned < 0 && !(w.UD(u).tags & TG_WORKER) && Vector2Distance(u.p, s.p) < 2.0f) {
        u.relic += s.relic; s.relic = 0; w.Emit(EV_RELIC, s.p, u.owner, u.id, 0); break;
    }
}
static void StepKraken(World& w, Site& s, int si) {
    const Balance& Bl = B();
    if (s.state == 0 && w.t >= Bl.krakenWake) { s.state = 1; int id = SpawnNeutral(w, OWN_WILD, "kraken", s.p, si); s.count = id; w.Emit(EV_KRAKEN, s.p, -1, 1); }
    if (s.state != 1) return;
    Unit* k = w.U(s.count); if (!k) return;
    k->order = O_IDLE; k->path.clear();
    // it grabs one ship at a time (30 dps) and sinks transports outright
    Unit* held = w.U((int)s.t2);
    if (!held || Vector2Distance(held->p, k->p) > 7) {
        held = nullptr; float bd = 6;
        for (auto& o : w.units) if (!o.dead && IsPlayer(o.owner) && (w.UD(o).tags & TG_SHIP) && o.inside < 0) { float d = Vector2Distance(o.p, k->p); if (d < bd) { bd = d; held = &o; } }
        s.t2 = held ? (float)held->id : -1;
    }
    if (held) {
        held->stunT = std::max(held->stunT, 0.2f);
        if (w.UD(*held).carry > 0 && w.UD(*held).attack <= 0) { w.Kill(*held, OWN_WILD); s.t2 = -1; }
        else w.Damage(k->id, OWN_WILD, held->id, 0, Bl.krakenDps * Str(w) * STEP, D_MELEE);
    }
}
static void StepGhost(World& w, Site& s, int si) {
    const Balance& Bl = B();
    if (s.state == 0 && w.t >= Bl.ghostWake) { s.state = 1; int id = SpawnNeutral(w, OWN_WILD, "ghost_ship", s.p, si); s.count = id; if (Unit* g = w.U(id)) g->stance = ST_AGGRESSIVE; w.Emit(EV_GHOST, s.p, -1, 1); }
    if (s.state != 1) return;
    Unit* g = w.U(s.count); if (!g) return;
    // it roams between the players' waters
    if (g->order == O_IDLE) { int p = (int)(w.Rand() * w.players.size()); for (const auto& b : w.buildings) if (!b.dead && b.owner == p) { g->order = O_ATTACK_MOVE; g->goal = FreeNear(w, b.Centre(), true, 8); g->path.clear(); break; } }
}
void World::StepSites() {
    const Balance& Bl = B(); if (units.capacity() < units.size() + 48) units.reserve(units.size() * 2 + 64); if (sites.capacity() < sites.size() + 16) sites.reserve(sites.size() * 2 + 32);
    for (int i = 0; i < (int)sites.size(); i++) {
        Site& s = sites[i];
        switch (s.kind) { case S_COVE: StepCove(*this, s, i); break; case S_TRIBE: StepTribe(*this, s, i); break; case S_ALTAR: StepAltar(*this, s, i); break; case S_RUIN: StepRuin(*this, s, i); break; case S_KRAKEN: StepKraken(*this, s, i); break; case S_GHOST: StepGhost(*this, s, i); break; }
    }
    // guards with no raid drift home (a leash of 12 tiles)
    for (auto& u : units) if (!u.dead && !IsPlayer(u.owner) && u.hire < 0 && u.home >= 0 && u.home < (int)sites.size() && sites[u.home].kind != S_GHOST && sites[u.home].kind != S_KRAKEN) {
        Vector2 h = sites[u.home].p; if (Vector2Distance(u.p, h) > 12 && u.order != O_MOVE) { u.order = O_MOVE; u.goal = Vector2Add(h, {(Rand() - 0.5f) * 4, (Rand() - 0.5f) * 4}); u.path.clear(); }
    }
    // raid contracts: the warning runs out, the raiders arrive, the contract ends
    for (auto& k : contracts) {
        if (!k.live && k.warnT > 0 && t >= k.warnT) { if (k.deal >= 100) k.deal -= 100; if (k.cove >= 0 && k.cove < (int)sites.size() && k.deal >= 0) SpawnRaid(*this, k); k.warnT = 0; }
        if (k.live && t - k.liveT > Bl.deals[std::clamp(k.deal, 0, (int)Bl.deals.size() - 1)].time + 1) k.live = false;
    }
    contracts.erase(std::remove_if(contracts.begin(), contracts.end(), [&](const Contract& k) { return !k.live && k.warnT == 0; }), contracts.end());
    // unhired pirates raid the most valuable trade route every 4 minutes (a 20 s warning to its owner), or a player the cove hates
    static const float NEVER = -1;
    for (int i = 0; i < (int)sites.size(); i++) {
        Site& s = sites[i]; if (s.kind != S_COVE || s.hp <= 0) continue;
        s.holdT += STEP; if (s.holdT < Bl.pirateRaidEvery) continue; s.holdT = 0;
        const Unit* best = nullptr; float bv = 0;
        for (const auto& u : units) if (!u.dead && IsPlayer(u.owner) && UD(u).key == "trade_ship") { float v = Vector2Distance(u.p, s.p) < 1e9f ? 1 + (float)(u.id % 7) : 0; if (v > bv) { bv = v; best = &u; } }
        int target = best ? best->owner : -1;
        if (target < 0) for (int p = 0; p < (int)players.size(); p++) if (s.rep[p] < -50 && players[p].alive) target = p;
        if (target < 0) continue;
        Contract k; k.cove = i; k.hirer = -1; k.target = target; k.deal = 0; k.warnT = t + 20; contracts.push_back(k);
        Emit(EV_PIRATE_WARN, best ? best->p : s.p, target, (int)contracts.size() - 1, 20);
    }
    (void)NEVER;
}
void World::StepWeather() {
    const Balance& Bl = B();
    weatherT += STEP;
    if (weather != 0) { weatherLeft -= STEP; if (weatherLeft <= 0) { weather = 0; Emit(EV_WEATHER, weatherAt, 0); } }
    if (weatherT < Bl.weatherEvery) return;
    weatherT = 0; float r = Rand(), acc = 0; int w = 0;
    for (int k = 0; k < 4; k++) { acc += Bl.weatherP[k]; if (r < acc) { w = k; break; } }
    weather = w; if (w == 0) return;
    // somewhere at sea, not on a home
    for (int tries = 0; tries < 30; tries++) { int x = (int)(Rand() * W), y = (int)(Rand() * H); if (tile[Idx(x, y)] == T_DEEP) { weatherAt = {x + 0.5f, y + 0.5f}; break; } }
    weatherR = w == 3 ? 6.0f : 18.0f; weatherLeft = w == 3 ? Bl.whirlTime : 60.0f;
    Emit(EV_WEATHER, weatherAt, w);
}

}  // namespace fa
