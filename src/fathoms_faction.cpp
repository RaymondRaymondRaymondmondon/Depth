// Fathoms' six factions (doc pp. 17-23): each one's signature mechanic, unique units' specials, hero aura and active,
// faction techs, ultimate and wonder. Nautilus Crew: Nemo's Logbook (draw 3, keep 1, each era). Islanders: Kinship
// (fathoms_neutral.cpp runs the towns), Totems, Bleed. Crustacean Brood: molting, seafloor walking, Brood Pools, Poison.
// Merfolk: swimming (FindPath), Kelp Spread, hiding on kelp, the Siren's song. Atlanteans: Devotion, conversion,
// offerings. Clockwork Foundry: Coal for Food, no morale, salvage (World::Kill), chain lightning.
#include "fathoms.h"
#include <algorithm>
#include <cmath>

namespace fa {

bool HasCard(const World& w, int p, int card) { return IsPlayer(p) && p < (int)w.players.size() && std::find(w.players[p].cards.begin(), w.players[p].cards.end(), card) != w.players[p].cards.end(); }
bool HasWonder(const World& w, int p) { for (const auto& b : w.buildings) if (!b.dead && b.owner == p && b.progress >= 1 && w.BD(b).key == "wonder") return true; return false; }
static int Fac(const World& w, int owner) { return IsPlayer(owner) && owner < (int)w.players.size() ? w.players[owner].faction : -1; }
static bool NearWater(const World& w, Vector2 p, float r) {
    for (int a = 0; a < 8; a++) for (float d = 1; d <= r; d += 1.5f) { int x = (int)(p.x + cosf(a * 0.785f) * d), y = (int)(p.y + sinf(a * 0.785f) * d); if (w.In(x, y) && w.Water(w.tile[w.Idx(x, y)])) return true; }
    return false;
}
static void Goto(World& w, Unit& u, Vector2 at, float near) {
    if (Vector2Distance(u.p, at) <= near) { u.vel = {}; return; }
    u.repathT -= STEP; if (u.path.empty() || u.pathAt >= (int)u.path.size() || u.repathT <= 0) { w.FindPath(u, at, u.path, near * 0.8f); u.pathAt = 0; u.repathT = 2.5f; }
    w.StepMove(u, 1);
}

// ---------------------------------------------------------------- per player
void World::FactionStart(Player& p) {
    if (p.faction != 0) return;
    // Nemo's Logbook: on each era (the start too), draw three cards never drawn before and keep one
    if (!p.cardOffer.empty()) return;
    std::vector<int> pool; for (int c = 0; c < (int)B().factions[0].cards.size(); c++) if (std::find(p.cardsSeen.begin(), p.cardsSeen.end(), c) == p.cardsSeen.end()) pool.push_back(c);
    for (int k = 0; k < 3 && !pool.empty(); k++) { int i = (int)(Rand() * pool.size()) % (int)pool.size(); p.cardOffer.push_back(pool[i]); p.cardsSeen.push_back(pool[i]); pool.erase(pool.begin() + i); }
    if (!p.cardOffer.empty()) Emit(EV_CARD, {0, 0}, p.id, (int)p.cardOffer.size());
}
void World::FactionStep(Player& p) {
    const Balance& Bl = B();
    // an ultimate's cooldown starts when it leaves
    if (p.ultimateUnit >= 0 && !U(p.ultimateUnit)) { p.ultimateUnit = -1; p.ultimateCD = 360; }
    if (p.faction == 4) {   // Devotion: +1/s a relic held, +0.5/s a ruin isle owned (doubled by the Sunken Spire)
        int relics = 0; for (const auto& b : buildings) if (!b.dead && b.owner == p.id) relics += b.relics;
        int ruins = 0; for (const auto& is : islands) if (is.kind == I_RUIN && is.owner == p.id) ruins++;
        float g = relics + 0.5f * ruins; if (HasWonder(*this, p.id)) g *= 2;
        p.devotion = std::min(1000.0f, p.devotion + g * STEP);
    }
    if (p.faction == 3) {   // Kelp Spread: owned kelp grows a tile every 15 s, up to 10 out (Tidecallers: 7.5 s, 14)
        bool tide = HasTech(p.id, "tidecallers"); p.upkeepAcc += STEP;
        if (p.upkeepAcc >= (tide ? 7.5f : 15.0f)) {
            p.upkeepAcc = 0; float reach = tide ? 14.0f : 10.0f; uint8_t me = (uint8_t)(p.id + 1);
            std::vector<Vector2> src; for (const auto& n : nodes) if (n.kind == N_KELP && n.amount > 0) { int x = (int)n.p.x, y = (int)n.p.y; bool mine = In(x, y) && (owner[Idx(x, y)] == p.id || kelpOwner[Idx(x, y)] == me); if (!mine) for (const auto& b : buildings) if (!b.dead && b.owner == p.id && b.progress >= 1 && Vector2Distance(b.Centre(), n.p) < 28) { mine = true; break; } if (mine) src.push_back(n.p); }
            for (int k = 0; k < W * H; k++) if (tile[k] == T_KELP && owner[k] == p.id && !kelpOwner[k]) kelpOwner[k] = me;
            if (!src.empty()) {
                std::vector<int> cand;
                for (int k = 0; k < W * H; k++) { if (kelpOwner[k] || !Water(tile[k])) continue; int x = k % W, y = k / W; bool adj = false;
                    for (int dy = -1; dy <= 1 && !adj; dy++) for (int dx = -1; dx <= 1 && !adj; dx++) if (In(x + dx, y + dy) && kelpOwner[Idx(x + dx, y + dy)] == me) adj = true;
                    if (!adj) continue; bool nearSrc = false; for (auto& s : src) if (Vector2Distance(s, {x + 0.5f, y + 0.5f}) <= reach) nearSrc = true; if (nearSrc) cand.push_back(k); }
                if (cand.empty()) for (auto& s : src) { int k = Idx((int)s.x, (int)s.y); if (!kelpOwner[k]) cand.push_back(k); }
                for (int g = 0; g < 3 && !cand.empty(); g++) { int i = (int)(Rand() * cand.size()) % (int)cand.size(); kelpOwner[cand[i]] = me; cand.erase(cand.begin() + i); }
            }
        }
    }
    (void)Bl;
}

// ---------------------------------------------------------------- auras (summed each tick, before units step)
void FactionAuras(World& w) {
    for (auto& u : w.units) { u.auraAtk = u.auraArmor = u.auraSpeed = u.auraHeal = 0; }
    std::vector<char> sung(w.units.size(), 0);
    for (size_t i = 0; i < w.units.size(); i++) {
        Unit& u = w.units[i]; if (u.dead || u.inside >= 0) continue; const UnitDef& d = w.UD(u); int f = Fac(w, u.owner);
        bool ship = d.tags & TG_SHIP; int tt = w.TileAt(u.p); int k = w.Idx(std::clamp((int)u.p.x, 0, w.W - 1), std::clamp((int)u.p.y, 0, w.H - 1));
        if (f == 0) { if (ship && HasCard(w, u.owner, 0)) u.auraAtk += 0.1f; if (ship && HasCard(w, u.owner, 4)) u.auraSpeed += 0.15f; if (d.key == "diving_marine" && HasCard(w, u.owner, 5)) u.auraArmor += 2;
            if (ship && HasWonder(w, u.owner) && w.owner[k] == u.owner) u.auraHeal += 2; }   // (the Nautilus Drydock)
        if (f == 2 && w.HasTech(u.owner, "chitin_plating")) u.auraArmor += 1;
        if (f == 3 && !ship) {
            if (!w.Water(tt) && !(d.tags & TG_WORKER) && !NearWater(w, u.p, 6)) u.auraAtk -= 0.1f;   // (weak far inland)
            if (w.kelpOwner[k] == u.owner + 1) { u.auraSpeed += 0.2f; u.hiddenT = std::max(u.hiddenT, w.HasTech(u.owner, "octopus_ink") ? 5.0f : 0.2f); if (HasWonder(w, u.owner)) u.auraHeal += 2; }
        }
        if ((d.tags & TG_ULTIMATE) && (f == 3 || f == 4)) u.auraAtk -= 0.25f;   // (the crowd-control ultimates deal 25% less)
        if ((d.tags & TG_ULTIMATE) && f == 4) for (auto& o : w.units) if (!o.dead && w.Enemies(u.owner, o.owner) && Vector2Distance(o.p, u.p) < 20) o.weakT = std::max(o.weakT, 0.2f);   // (Cthulhu's gaze)
        // heroes
        if ((d.tags & TG_HERO) && IsPlayer(u.owner) && f >= 0) {
            const FactionDef& F = B().factions[f]; float r = d.aura > 0 ? d.aura : 6;
            for (auto& o : w.units) { if (o.dead || !w.Allied(o.owner, u.owner) || Vector2Distance(o.p, u.p) > r) continue;
                if (F.heroAura == "attack") o.auraAtk += F.heroAuraAmount; else if (F.heroAura == "heal" || F.heroAura == "repair") o.auraHeal += F.heroAuraAmount;
                else if (F.heroAura == "armor") o.auraArmor += F.heroAuraAmount; else if (F.heroAura == "swim_speed" && w.Water(w.TileAt(o.p))) o.auraSpeed += F.heroAuraAmount; }
            if (F.heroAura == "reveal") for (auto& o : w.units) if (!o.dead && w.Enemies(u.owner, o.owner) && Vector2Distance(o.p, u.p) < F.heroAuraAmount) o.hiddenT = 0;
        }
        // the neutral chiefs (war cry +20%, the witch doctor heals tribesmen)
        if (d.special == "war_cry" || d.special == "witch") for (auto& o : w.units) if (!o.dead && o.owner == u.owner && o.id != u.id && Vector2Distance(o.p, u.p) < 6) { if (d.special == "war_cry") o.auraAtk += 0.2f; else o.auraHeal += 2; }
        // the Siren's song: enemies within 5 deal 20% less (it does not stack)
        if (d.special == "song") for (size_t j = 0; j < w.units.size(); j++) { Unit& o = w.units[j]; if (!sung[j] && !o.dead && w.Enemies(u.owner, o.owner) && Vector2Distance(o.p, u.p) < 5) { sung[j] = 1; o.auraAtk -= 0.2f; } }
    }
    // Totems: friendly units within 8 (12 with Sun Drums) +15% attack (+1 armor with Sun Drums)
    for (const auto& b : w.buildings) if (!b.dead && b.progress >= 1 && w.BD(b).key == "totem") {
        bool drums = w.HasTech(b.owner, "sun_drums"); float r = w.BD(b).auraR + (drums ? 4 : 0); Vector2 c = b.Centre();
        for (auto& o : w.units) if (!o.dead && w.Allied(o.owner, b.owner) && Vector2Distance(o.p, c) < r) { o.auraAtk += 0.15f; if (drums) o.auraArmor += 1; }
    }
}

// ---------------------------------------------------------------- per unit
void World::FactionUnitStep(Unit& u) {
    const UnitDef& d = UD(u); int f = Fac(*this, u.owner);
    // molting: experience = damage dealt; at 60 (40 with Rapid Molting) cocooned 5 s, then +25% HP, +15% attack; Lobster Knights twice
    if (f == 2 && !(d.tags & (TG_WORKER | TG_HERO | TG_ULTIMATE))) {
        int maxM = d.key == "lobster_knight" ? 2 : 1;
        if (u.molts == 0 && u.xp == 0 && t - u.spawnT < STEP * 1.5f && HasWonder(*this, u.owner)) { u.molts = 1; u.hp *= 1.25f; }   // (the Great Brood Mound)
        float need = HasTech(u.owner, "rapid_molting") ? 40.0f : 60.0f;
        if (u.molts < maxM && u.xp >= need * (u.molts + 1)) { u.molts++; u.cocoonT = 5; u.hp *= 1.25f; Emit(EV_MOLT, u.p, u.id, u.owner); }
    }
    // the Submarine dives: hidden 3 s after it last fired (Ballast Tanks: a free shot every 20 s)
    if (d.special == "dive") { bool quiet = u.cool <= d.reload - 3; bool ballast = HasTech(u.owner, "ballast_tanks") && u.abilityT > 14; if (quiet || ballast) u.hiddenT = std::max(u.hiddenT, 0.3f); }
    if (d.special == "burrow" && u.order == O_IDLE && u.cool <= d.reload - 2) u.hiddenT = std::max(u.hiddenT, 0.3f);
    // the Deep Priest converts one enemy unit over 6 s (4 with Whispers Below) for 30 Devotion; 20 s cooldown
    if (u.order == O_CONVERT) {
        Unit* tg = U(u.target); Player* P = IsPlayer(u.owner) ? &players[u.owner] : nullptr;
        if (!tg || !P || !Enemies(u.owner, tg->owner) || (UD(*tg).tags & (TG_HERO | TG_ULTIMATE)) || ((UD(*tg).tags & TG_SHIP) && UD(*tg).pop > 3)) { u.order = O_IDLE; u.chan = 0; return; }
        if (Vector2Distance(u.p, tg->p) > d.range) { Goto(*this, u, tg->p, d.range * 0.8f); u.chan = 0; return; }
        u.vel = {}; u.chan += STEP;
        if (u.chan >= (HasTech(u.owner, "whispers_below") ? 4.0f : 6.0f)) {
            u.chan = 0; u.order = O_IDLE;
            if (P->devotion >= 30) { P->devotion -= 30; tg->owner = u.owner; tg->converted = true; tg->order = O_IDLE; tg->target = -1; tg->hire = -1; tg->home = -1; tg->path.clear(); if (HasTech(u.owner, "drowned_legion")) tg->hp *= 1.2f; u.abilityT = 20; Emit(EV_CONVERT, tg->p, tg->id, u.owner); }
        }
        return;
    }
    // the Shaman's 45 s Kinship ritual at a tribal town
    if (u.order == O_RITUAL) {
        if (u.target < 0 || u.target >= (int)sites.size() || sites[u.target].kind != S_TRIBE || sites[u.target].hp <= 0) { u.order = O_IDLE; return; }
        Site& s = sites[u.target]; if (Vector2Distance(u.p, s.p) > 5) { Goto(*this, u, s.p, 4); return; }
        u.vel = {}; u.chan += STEP;
        if (u.chan >= B().ritualTime) { u.chan = 0; u.order = O_IDLE; s.peace[u.owner] = 2; s.tributeT[u.owner] = 0; for (auto& o : units) if (!o.dead && o.home == u.target && o.owner == OWN_TRIBE && o.hire == u.owner) o.hire = -1; Emit(EV_TRIBE_ALLY, s.p, u.owner, u.target); UpdateTerritory(); }
        return;
    }
    // ultimates
    if ((d.tags & TG_ULTIMATE) && f >= 0) {
        if (f == 0 && u.hp < d.hp * 0.5f && u.chan == 0) { u.chan = 1; u.hiddenT = 10; }   // (the Nautilus submerges once, 10 s)
        if (u.abilityT > 0) return;
        if (f == 1) {   // the Demigod leaps between targets
            const Unit* tg = nullptr; float bd = 10; for (const auto& o : units) if (!o.dead && o.inside < 0 && Enemies(u.owner, o.owner)) { float dd = Vector2Distance(o.p, u.p); if (dd > 3 && dd < bd) { bd = dd; tg = &o; } }
            if (tg) { Vector2 to = Vector2Add(tg->p, Vector2Scale(Vector2Normalize(Vector2Subtract(u.p, tg->p)), 1.2f)); if (Passable((int)to.x, (int)to.y, MV_AMPHIB, f)) { u.p = to; u.path.clear(); Emit(EV_ULTIMATE, to, u.id, 1); } }
            u.abilityT = 6;
        } else if (f == 2) {   // the Crustacean Queen spawns 2 Spitter Shrimp every 10 s
            int sp = B().Unit("spitter_shrimp"); for (int k = 0; k < 2; k++) { int id = SpawnUnit(u.owner, sp, Vector2Add(u.p, {k ? 1.0f : -1.0f, 1})); if (Unit* s = U(id)) { s->life = 60; s->order = O_ATTACK_MOVE; s->goal = u.p; } }
            u.abilityT = 10;
        } else if (f == 3) {   // Neptune's trident: a wave that stuns every ship in a line
            const Unit* tg = nullptr; float bd = 14; for (const auto& o : units) if (!o.dead && (UD(o).tags & TG_SHIP) && Enemies(u.owner, o.owner)) { float dd = Vector2Distance(o.p, u.p); if (dd < bd) { bd = dd; tg = &o; } }
            if (tg) { Vector2 dir = Vector2Normalize(Vector2Subtract(tg->p, u.p));
                for (auto& o : units) { if (o.dead || !(UD(o).tags & TG_SHIP) || !Enemies(u.owner, o.owner)) continue; Vector2 ap = Vector2Subtract(o.p, u.p); float al = Vector2DotProduct(ap, dir); if (al < 0 || al > 14) continue; if (fabsf(ap.x * dir.y - ap.y * dir.x) < 1.5f) { o.stunT = std::max(o.stunT, 3.0f); Damage(-1, u.owner, o.id, 0, 20, D_BLAST); } }
                shots.push_back({u.p, Vector2Add(u.p, Vector2Scale(dir, 14)), 0, 0.8f, 4, 1}); Emit(EV_ULTIMATE, u.p, u.id, 3); }
            u.abilityT = 12;
        } else if (f == 5) {   // the Steam Titan fires its cannons in every direction
            int n = 0; for (auto& o : units) if (!o.dead && o.inside < 0 && Enemies(u.owner, o.owner) && Vector2Distance(o.p, u.p) < 6 && n < 3) { pending.push_back({u.id, u.owner, o.id, 0, 30, D_BLAST, 0.4f, o.p, 1.0f, 0}); shots.push_back({u.p, o.p, 0, 0.4f, 1, 2}); n++; }
            u.abilityT = 2;
        }
    }
}
void World::FactionOnHit(Unit& a, Unit& v, float dmg) {
    const UnitDef& d = UD(a); int f = Fac(*this, a.owner);
    if (f == 2) a.xp += dmg;
    if (d.special == "bleed") v.bleedT = 4;
    if (d.special == "poison") v.poisonT = 4;
    if (d.special == "stun3" && ++a.hitCount % 3 == 0) v.stunT = std::max(v.stunT, 1.0f);
    if (d.key == "harpooner" && HasTech(a.owner, "electric_harpoons") && a.abilityT <= 0) { v.stunT = std::max(v.stunT, 1.0f); a.abilityT = 8; }
    if (d.special == "dive" && HasTech(a.owner, "ballast_tanks") && a.abilityT <= 0) a.abilityT = 20;   // (this shot didn't surface it)
    if (d.special == "chain") {   // chain lightning: 2 more targets within 3 tiles at 40%
        int n = 0; for (auto& o : units) { if (n >= 2) break; if (o.dead || o.id == v.id || !Enemies(a.owner, o.owner) || Vector2Distance(o.p, v.p) > 3) continue;
            float k = Fac(*this, o.owner) == 5 && HasTech(o.owner, "grounded_casings") ? 0.75f : 1.0f; Damage(-1, a.owner, o.id, 0, dmg * 0.4f * k, D_BLAST); shots.push_back({v.p, o.p, 0, 0.25f, 5, 0.6f}); n++; }
    }
    if ((d.tags & TG_ULTIMATE) && f == 1) { Vector2 push = Vector2Scale(Vector2Normalize(Vector2Subtract(v.p, a.p)), 1.5f); Vector2 to = Vector2Add(v.p, push); if (Passable((int)to.x, (int)to.y, UD(v).move, Fac(*this, v.owner))) v.p = to; v.stunT = std::max(v.stunT, 0.5f); }
}

// ---------------------------------------------------------------- abilities, ultimates, cards, conversions, offerings
bool World::FactionAbility(int player, int unit, int ability) {
    const Balance& Bl = B(); Player& P = players[player]; const FactionDef& F = Bl.factions[P.faction];
    int kind = ability / 100, a = ability % 100; const Command& c = cmd;
    if (kind == C_CARD) {
        if (P.faction != 0 || a < 0 || a >= (int)P.cardOffer.size()) return false;
        int card = P.cardOffer[a]; P.cards.push_back(card); P.cardOffer.clear();
        if (card == 1) {   // Deep Survey: reveal every ruin and wreck
            auto reveal = [&](Vector2 p) { for (int dy = -4; dy <= 4; dy++) for (int dx = -4; dx <= 4; dx++) if (In((int)p.x + dx, (int)p.y + dy)) P.explored[Idx((int)p.x + dx, (int)p.y + dy)] = 1; };
            for (const auto& s : sites) if (s.kind == S_RUIN) reveal(s.p); for (const auto& n : nodes) if (n.kind == N_WRECK) reveal(n.p);
        }
        if (card == 2) for (auto& u : units) if (u.owner == player && UD(u).special == "dive") u.hp *= 1.25f;
        Emit(EV_CARD, {0, 0}, player, -1, card); return true;
    }
    if (kind == C_ABILITY) {
        Unit* h = U(unit); if (!h || h->owner != player || !(UD(*h).tags & TG_HERO) || h->abilityT > 0) return false;
        Vector2 at = c.at; float reach = 9;
        switch (P.faction) {
            case 0: for (auto& o : units) if (!o.dead && o.owner == player && (UD(o).tags & TG_SHIP) && Vector2Distance(o.p, h->p) < 8) o.fastT = 10; break;   // Full Ahead
            case 1: { if (Vector2Distance(at, h->p) > reach) return false;   // Coconut Barrage
                for (auto& o : units) if (!o.dead && o.inside < 0 && Enemies(player, o.owner) && Vector2Distance(o.p, at) < 3) { Damage(h->id, player, o.id, 0, 40, D_BLAST); o.stunT = std::max(o.stunT, 2.0f); }
                shots.push_back({h->p, at, 0, 0.6f, 1, 3}); break; }
            case 2: { Unit* tg = U(c.target); if (!tg || !Enemies(player, tg->owner) || Vector2Distance(tg->p, h->p) > 2.5f) return false; Damage(h->id, player, tg->id, 0, 80, D_MELEE); if (Unit* t2 = U(c.target)) t2->stunT = 3; break; }   // Crushing Claw
            case 3: { if (Vector2Distance(at, h->p) > reach) return false;   // Riptide
                for (auto& o : units) if (!o.dead && (UD(o).tags & TG_SHIP) && Enemies(player, o.owner) && Vector2Distance(o.p, at) < 5) { Vector2 to = Vector2Lerp(o.p, at, 0.5f); if (Passable((int)to.x, (int)to.y, MV_SEA, -1)) o.p = to; o.slowT = 5; o.path.clear(); }
                break; }
            case 4: {   // Phase Step: the hero and up to 6 nearby units 12 tiles toward the point
                Vector2 dir = Vector2Subtract(at, h->p); float L = Vector2Length(dir); if (L < 0.5f) return false; dir = Vector2Scale(dir, std::min(12.0f, L) / L);
                std::vector<Unit*> g{h}; for (auto& o : units) if ((int)g.size() < 7 && !o.dead && o.owner == player && o.id != h->id && o.inside < 0 && o.garrisoned < 0 && !(UD(o).tags & TG_SHIP) && Vector2Distance(o.p, h->p) < 5) g.push_back(&o);
                for (Unit* o : g) { Vector2 to = Vector2Add(o->p, dir); if (Passable((int)to.x, (int)to.y, MV_LAND, P.faction)) { o->p = to; o->path.clear(); o->order = O_IDLE; } }
                break; }
            case 5: for (auto& o : units) if (!o.dead && o.owner == player && Vector2Distance(o.p, h->p) < 8) o.hasteT = 10; break;   // Overclock
        }
        h->abilityT = F.heroCooldown; Emit(EV_HERO, h->p, h->id, player, P.faction); return true;
    }
    if (kind == C_ULTIMATE) {
        if (P.era < 2 || P.ultimateCD > 0 || (P.ultimateUnit >= 0 && U(P.ultimateUnit))) return false;
        float ich = P.faction == 4 ? 150 : 300; if (P.res[R_ICHOR] < ich || (P.faction == 4 && P.devotion < 600)) return false;
        Vector2 at{-1, -1}; if (Unit* h = U(P.heroUnit)) at = h->p;
        if (at.x < 0) for (const auto& b : buildings) if (!b.dead && b.owner == player && BD(b).key == "harbor") { at = Vector2Add(b.Centre(), {(float)b.size, 0}); break; }
        if (at.x < 0) return false;
        P.res[R_ICHOR] -= ich; if (P.faction == 4) P.devotion -= 600;
        int id = SpawnUnit(player, Bl.Unit("ultimate"), at); P.ultimateUnit = id; Emit(EV_ULTIMATE, at, id, player, P.faction); return true;
    }
    if (kind == C_CONVERT) {
        Unit* pr = U(unit); if (!pr || pr->owner != player || UD(*pr).special != "convert" || pr->abilityT > 0 || P.devotion < 30) return false;
        Unit* tg = U(c.target); if (!tg || !Enemies(player, tg->owner)) return false;
        pr->order = O_CONVERT; pr->target = tg->id; pr->chan = 0; pr->path.clear(); return true;
    }
    if (kind == C_OFFER) {   // offer units at a Chapel: half their cost in Devotion
        if (P.faction != 4) return false; int n = 0;
        for (int id : c.units) { Unit* u = U(id); if (!u || u->owner != player || u->inside >= 0) continue; bool near = false;
            for (const auto& b : buildings) if (!b.dead && b.owner == player && BD(b).key == "chapel" && b.progress >= 1 && Vector2Distance(b.Centre(), u->p) < 4) near = true;
            if (!near) continue; float v = 0; for (int r = 0; r < R_COUNT; r++) v += UD(*u).cost[r]; P.devotion = std::min(1000.0f, P.devotion + v * 0.5f); u->dead = true; n++; }
        return n > 0;
    }
    return false;
}

}  // namespace fa
