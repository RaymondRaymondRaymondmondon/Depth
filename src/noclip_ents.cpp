// NOCLIP's entities (design doc pp. 26-28, 22-23): rare, readable and different from each other. Each has a sense it
// hunts by, a tell and a counter; the behaviour classes are in the data (noclip_entities.json). Spawning follows each
// level's hourly budget, which rises after 18:00 and triples in Overtime; the forecast's active levels get more.
#include "noclip.h"
#include <algorithm>
#include <cmath>

namespace nc {

static float AngleTo(const Player& p, Vector3 at) {   // how far off the player's look a point is (radians, horizontal)
    float a = atan2f(at.z - p.p.z, at.x - p.p.x), d = a - p.yaw; while (d > PI) d -= 2 * PI; while (d < -PI) d += 2 * PI; return fabsf(d);
}
static bool Looking(const World& w, const Player& p, const Entity& e, float cone = 0.6f, float range = 30) {
    if (!p.Alive() || p.level != e.level) return false;
    if (Vector3Distance(p.p, e.p) > range) return false;
    return AngleTo(p, e.p) < cone && w.LineOfSight(e.level, p.Eye(), Vector3Add(e.p, {0, 1.2f, 0}));
}
static bool Lone(const World& w, const Player& p) { for (const auto& o : w.crew) if (o.id != p.id && o.level == p.level && o.Alive() && Vector3Distance(o.p, p.p) < 15) return false; return true; }

void World::SpawnEntities(float dt) {
    float gameRate = (D().dayEnd - D().dayStart) / dayLen();
    std::vector<int> occupied; for (const auto& p : crew) if (p.Alive() && std::find(occupied.begin(), occupied.end(), p.level) == occupied.end()) occupied.push_back(p.level);
    for (int lvId : occupied) {
        const LevelDef& def = D().levels[lvId]; Level& lv = L(lvId);
        float budget = def.budget * (clock >= D().evening ? 2.0f : 1.0f) * (overtime ? 3.0f : 1.0f) * (std::find(forecast.begin(), forecast.end(), lvId) != forecast.end() ? 1.5f : 1.0f);
        if (mode == 5) budget *= 0.6f;   // (Lonely: fewer entities)
        int count = 0; for (const auto& e : ents) if (e.level == lvId && e.st != ES_GONE && !e.hallucination) count++;
        // the singletons a level always has: the Leviathan, the Warden, the Innkeeper, the house's eyes, the poles' scarecrows
        auto ensure = [&](const char* id, int n) { int k = EntityIndex(id); if (k < 0) return; int have = 0; for (const auto& e : ents) if (e.level == lvId && e.def == k) have++; for (int i = have; i < n; i++) { Entity e; e.def = k; e.uid = nextUid++; e.level = lvId; e.st = ES_IDLE; for (int t = 0; t < 300; t++) { int x = RandI(lv.w), z = RandI(lv.h); if (lv.Walkable(x, z) && lv.At(x, z) != T_LABFLOOR && lv.At(x, z) != T_PIT) { e.p = lv.Center(x, z); break; } } e.goal = e.p; ents.push_back(e); } };
        if (lvId == 7) ensure("leviathan", 1);
        if (lvId == 16) ensure("warden", 1);
        if (lvId == 19) ensure("innkeeper", 1);
        if (lvId == 12) ensure("seer", 10);
        if (lvId == 10) ensure("scarecrow", clock >= D().evening ? 2 : 1);
        if (lvId == 9) ensure("neighbor", 3);
        if (count >= 3 + (int)budget) continue;
        float chance = budget / 60.0f * gameRate * dt;
        if (Rand() > chance) continue;
        // which: weighted by the level's list (the singletons above aren't spawned here)
        float tot = 0; for (const auto& e : def.entities) tot += e.second; float r = Rand() * tot; std::string pick = def.entities.empty() ? "" : def.entities[0].first;
        for (const auto& e : def.entities) { r -= e.second; if (r <= 0) { pick = e.first; break; } }
        int k = EntityIndex(pick); if (k < 0) continue; const EntityDef& ed = D().entities[k];
        if (ed.id == "leviathan" || ed.id == "warden" || ed.id == "innkeeper" || ed.id == "seer" || ed.id == "scarecrow" || ed.id == "neighbor") continue;
        if (ed.id == "smiler" && clock < D().evening && lvId == 0 && !overtime) continue;   // (the Lobby's Smilers come after 18:00)
        // somewhere out of everyone's sight, at least 18 m off
        Vector3 at{}; bool ok = false;
        for (int t = 0; t < 120 && !ok; t++) {
            int x = RandI(lv.w), z = RandI(lv.h); if (!lv.Walkable(x, z) || lv.At(x, z) == T_LABFLOOR || lv.At(x, z) == T_PIT || lv.At(x, z) == T_DEEP) continue;
            at = lv.Center(x, z); ok = true;
            if (ed.id == "smiler" && lv.light[z * lv.w + x] > 0 && !(lv.Flags(x, z) & CF_DEADLIGHT)) ok = false;   // (dark places)
            if (ed.id == "clump" && !(lv.Flags(x + 1, z) & CF_PIPES) && !(lv.Flags(x - 1, z) & CF_PIPES)) ok = ok && Rand() < 0.2f;
            if (ed.id == "spider" && !(lv.Flags(x, z) & CF_WEB)) ok = ok && Rand() < 0.3f;
            for (const auto& p : crew) if (p.Alive() && p.level == lvId && (Vector3Distance(p.p, at) < 18 || (Vector3Distance(p.p, at) < 40 && LineOfSight(lvId, p.Eye(), Vector3Add(at, {0, 1, 0}))))) ok = false;
        }
        if (!ok) continue;
        for (int n = 0; n < std::max(1, ed.pack); n++) { Entity e; e.def = k; e.uid = nextUid++; e.level = lvId; e.p = Vector3Add(at, {n * 0.8f, 0, 0}); e.goal = at; e.st = ES_WANDER; if (ed.id == "faceling") e.mimicOf = Rand() < 0.3f ? -2 : -1; ents.push_back(e); }   // (mimicOf -2 on a Faceling: a trader)
    }
}

void World::StepEntity(Entity& e) {
    float dt = STEP; const EntityDef& ed = D().entities[e.def]; const std::string& b = ed.behaviour; Level& lv = L(e.level);
    e.t += dt; e.cool = std::max(0.0f, e.cool - dt); e.stunT = std::max(0.0f, e.stunT - dt);
    if (e.stunT > 0) return;
    // the nearest living crew member on this level
    int tgt = -1; float td = 1e9f; for (const auto& p : crew) if (p.level == e.level && p.st == PS_ALIVE) { float d = Vector3Distance(p.p, e.p); if (d < td) { td = d; tgt = p.id; } }
    auto moveTo = [&](Vector3 goal, float speed) {
        if (e.path.empty() || e.memT <= 0 || Vector3Distance(goal, e.goal) > 2) { e.goal = goal; e.path = Path(e.level, e.p, goal, 2500); e.memT = 1.0f; if (!e.path.empty()) e.path.erase(e.path.begin()); }
        e.memT -= dt;
        Vector3 next = goal; if (!e.path.empty()) { int c = e.path.front(); next = lv.Center(c % lv.w, c / lv.w); if (Vector2Distance({next.x, next.z}, {e.p.x, e.p.z}) < 0.5f) e.path.erase(e.path.begin()); }
        Vector3 d = Vector3Subtract(next, e.p); d.y = 0; float L2 = Vector3Length(d); if (L2 < 0.01f) return;
        Vector3 step = Vector3Scale(d, std::min(speed * dt, L2) / L2);
        // the Smiler can't enter a lit cell; nothing walks into a solid one
        Vector3 np = Vector3Add(e.p, step); int cx = lv.CellX(np.x), cz = lv.CellZ(np.z);
        if (lv.Solid(cx, cz)) return;
        if (b == "light" && LightAt(e.level, np) > 0.75f && lv.light[cz * lv.w + cx] > 0) return;
        if (b == "light") { LabState* lab = LabAt(e.level, np); if (lab && ((lab->upgrades >> 0) & 1)) return; }   // (floodlights: no Smilers inside)
        // a Lab's locked or shut blast door holds it (reinforced: twice as long)
        if (lv.At(cx, cz) == T_BLAST) { for (int k = 0; k < (int)lv.labs.size(); k++) if (lv.labs[k].doorX == cx && lv.labs[k].doorZ == cz) { LabState* lab = Lab(e.level, k); if (lab && (!lab->doorOpen || lab->locked)) return; } }
        e.yaw = atan2f(d.z, d.x); e.p = np; e.v = Vector3Scale(step, 1 / dt);
    };
    auto wander = [&](float speed) { if (Vector3Distance(e.goal, e.p) < 1 || e.t > 20) { e.t = 0; for (int t = 0; t < 20; t++) { int x = lv.CellX(e.p.x) + RandI(17) - 8, z = lv.CellZ(e.p.z) + RandI(17) - 8; if (lv.Walkable(x, z) && lv.At(x, z) != T_PIT && lv.At(x, z) != T_LABFLOOR) { e.goal = lv.Center(x, z); break; } } e.path.clear(); } moveTo(e.goal, speed); };
    auto attack = [&](Player& p, float dmg, int injury, const char* cause) { if (e.cool > 0) return; e.cool = 1.6f; Hurt(p, dmg, cause, injury); };
    // the Siren: everything on the level comes to the Lab
    for (const auto& l : labs) if (l.level == e.level && l.sirenT > 0 && b != "seer" && b != "leviathan") { const LabPlan& lp = lv.labs[l.idx]; moveTo(lv.Center(lp.doorX, lp.doorZ), ed.chase); return; }
    Player* P = tgt >= 0 ? &crew[tgt] : nullptr;
    if (!P) { if (b != "seer" && b != "leviathan") wander(ed.walk); return; }
    // seeing an entity costs a little sanity (once a minute per entity)
    if (Looking(*this, *P, e, 0.7f, 20) && e.watchT <= 0) { P->sanity -= 4; e.watchT = 60; Emit(E_ENTITY_SEEN, P->id, e.def, e.level, e.p); }
    e.watchT = std::max(0.0f, e.watchT - dt);
    bool flareNear = false; for (const auto& w : items) if (w.level == e.level && w.loot.def < 0 && w.noiseT > 0 && D().items[-1 - w.loot.def].use == "flare" && Vector3Distance(w.p, e.p) < 12) flareNear = true;
    if (b == "light") {   // the Smiler: a grin in the dark; comes to light pointed at it, charges a runner
        bool tooth = false; for (const Loot& l : {P->pocket[0], P->pocket[1]}) if (l.def >= 0 && D().loot[l.def].effect == "tooth") tooth = true;
        if (flareNear || tooth || e.st == ES_FLEE) { if (e.st != ES_FLEE) { e.st = ES_FLEE; e.t = 0; } if (e.t > 30) e.st = ES_WANDER; Vector3 away = Vector3Add(e.p, Vector3Scale(Vector3Normalize(Vector3Subtract(e.p, P->p)), 6)); moveTo(away, ed.walk * 2); return; }
        if (td < 10) P->lampFlicker = 0.5f;
        bool pointed = P->lamp && P->battery > 0 && AngleTo(*P, e.p) < 0.45f && td < 12 && LineOfSight(e.level, P->Eye(), e.p);
        bool running = Vector2Length({P->vel.x, P->vel.z}) > 4 && td < 14;
        if (pointed || running || e.st == ES_CHASE) { e.st = ES_CHASE; moveTo(P->p, pointed || running ? ed.chase : ed.walk * 2); if (td < 1.2f) { attack(*P, (float)ed.damage, 0, "a Smiler"); e.st = ES_WANDER; } if (td > 18) e.st = ES_WANDER; }
        else if (P->lamp && td < 7) moveTo(P->p, ed.walk);
        else wander(ed.walk * 0.5f);
        if (Rand() < dt * 0.05f && td < 15) Emit(E_TELL, P->id, e.def, e.level, e.p, 0, "a giggle");
        return;
    }
    if (b == "hound") {   // sound and blood; a pack; a still player holding its eye makes it hesitate
        float heard = 0; int loud = -1; for (const auto& p : crew) if (p.level == e.level && p.st == PS_ALIVE) { float d = Vector3Distance(p.p, e.p); float n = p.noise * 30 / std::max(1.0f, d) + ((p.injuries & IN_BLEED) && d < 25 ? 2.0f : 0); if (n > heard) { heard = n; loud = p.id; } }
        for (const auto& w : items) if (w.level == e.level && w.loot.def < 0 && w.noiseT > 0 && D().items[-1 - w.loot.def].use == "noisemaker") { float d = Vector3Distance(w.p, e.p); if (d < 30) { moveTo(w.p, ed.chase); return; } }
        if (heard > 1.2f && loud >= 0) { if (e.st != ES_CHASE) Emit(E_HOWL, loud, e.def, e.level, e.p); e.st = ES_CHASE; e.target = loud; }
        if (e.st == ES_CHASE && e.target >= 0) {
            Player& T = crew[e.target]; float d = Vector3Distance(T.p, e.p);
            bool still = Vector2Length({T.vel.x, T.vel.z}) < 0.3f && AngleTo(T, e.p) < 0.4f && d < 8;
            if (still) { e.st = ES_STALK; e.t = 0; return; }
            moveTo(T.p, ed.chase); if (d < 1.1f) attack(T, (float)ed.damage, Rand() < 0.4f ? IN_BLEED : 0, "a Hound"); if (d > 35 || T.st != PS_ALIVE) e.st = ES_WANDER;
        } else if (e.st == ES_STALK) { if (e.t > 2.5f) e.st = ES_CHASE; }
        else wander(ed.walk);
        if (Rand() < dt * 0.1f && td < 20) Emit(E_TELL, P->id, e.def, e.level, e.p, 0, "panting");
        return;
    }
    if (b == "faceling") {   // wanders; stops when looked at; a trader now and then; aggressive if touched
        if (Looking(*this, *P, e, 0.5f, 20)) { e.v = {}; return; }
        if (td < 0.6f && e.mimicOf != -2) attack(*P, (float)ed.damage, 0, "a Faceling");
        wander(ed.walk); return;
    }
    if (b == "duller") {   // swarms anyone who stops moving in the dark; light freezes them
        bool lit = LightAt(e.level, e.p) > 0.5f || (P->lamp && AngleTo(*P, e.p) < 0.5f && td < 10);
        if (lit) return;
        bool stopped = Vector2Length({P->vel.x, P->vel.z}) < 0.3f;
        if (stopped && td < 12) { moveTo(P->p, ed.chase); if (td < 1.0f) attack(*P, (float)ed.damage, 0, "the Dullers"); } else wander(ed.walk);
        return;
    }
    if (b == "wretch") {   // the lowest sanity on the level; a group with high sanity is invisible to them
        int low = -1; float ls = 101; for (const auto& p : crew) if (p.level == e.level && p.st == PS_ALIVE && p.sanity < ls) { ls = p.sanity; low = p.id; }
        if (low >= 0 && ls < 60) { Player& T = crew[low]; moveTo(T.p, ed.chase); if (Vector3Distance(T.p, e.p) < 1.1f) attack(T, (float)ed.damage, 0, "a Wretch"); }
        else wander(ed.walk);
        if (Rand() < dt * 0.08f && td < 20) Emit(E_TELL, P->id, e.def, e.level, e.p, 0, "muttering in the pipes");
        return;
    }
    if (b == "clump") {   // waits overhead at the pipes and drops on whoever walks beneath
        if (flareNear) { wander(ed.walk * 3); return; }
        if (td < 1.6f && e.st != ES_ATTACK) { e.st = ES_ATTACK; e.t = 0; attack(*P, (float)ed.damage, 0, "a Clump"); }
        else if (e.st == ES_ATTACK) { if (e.t > 3) e.st = ES_IDLE; moveTo(P->p, ed.chase); }
        if (Rand() < dt * 0.1f && td < 12) Emit(E_TELL, P->id, e.def, e.level, e.p, 0, "pipes clank overhead");
        return;
    }
    if (b == "mimic") {   // the Skin-Stealer: a lone player, a teammate's voice from the wrong place; stays off groups
        if (e.mimicOf >= 0 && e.st != ES_CHASE) { wander(ed.walk); }
        // offered almond water, or seen by a scanner, or a group: it backs off
        int groups = 0; for (const auto& p : crew) if (p.level == e.level && p.st == PS_ALIVE && Vector3Distance(p.p, e.p) < 12) groups++;
        if (groups >= 2 && e.st != ES_CHASE) { wander(ed.walk); return; }
        if (Lone(*this, *P) && td < 25 && LineOfSight(e.level, e.p, P->Eye())) {
            e.st = ES_CHASE; moveTo(P->p, td < 6 ? ed.chase : ed.walk * 1.4f);
            if (Rand() < dt * 0.25f && crew.size() > 1) { int who = (P->id + 1 + RandI((int)crew.size() - 1)) % (int)crew.size(); Emit(E_TELL, P->id, e.def, e.level, e.p, 5, crew[who].name); }   // (a teammate's voice, from here)
            if (td < 1.0f && e.cool <= 0) {
                if (ed.id == "mirrorthing") { attack(*P, (float)ed.damage, IN_BLEED, "a Mirror Thing"); }
                else { P->st = PS_TAKEN; P->health = 0; P->deaths++; P->lastCause = "taken by a " + ed.name; Emit(E_TAKEN, P->id, e.def, e.level, P->p, 0, ed.name); e.mimicOf = P->id; e.st = ES_WANDER; e.cool = 30; }
            }
        } else { if (e.st == ES_CHASE && td > 30) e.st = ES_WANDER; wander(ed.walk); }
        return;
    }
    if (b == "party") {   // wants you at the party; harmless until you accept
        if (td < 3 && e.cool <= 0) { e.cool = 25; Emit(E_INVITE, P->id, e.def, e.level, e.p); }
        if (td > 2.5f) moveTo(P->p, ed.walk); return;
    }
    if (b == "moth") {   // swarms lights; the dust poisons
        if (flareNear) { wander(ed.chase); return; }
        if (P->lamp && P->battery > 0 && td < 15) { moveTo(Vector3Add(P->p, {0, 0, 0}), ed.chase); if (td < 1.5f) { bool hazmat = P->suits & 1; if (!hazmat) { P->sanity -= 3 * dt; P->health -= 1.5f * dt; } } }
        else wander(ed.walk); return;
    }
    if (b == "watch") {   // the Neighbors, the Scarecrows, the Patients: they move only while nobody looks
        bool watched = false; for (const auto& p : crew) if (Looking(*this, p, e, 0.55f, 60)) watched = true;
        if (watched) { e.v = {}; return; }
        if (ed.id == "scarecrow" && clock < D().evening && !overtime) { if (Rand() < dt * 0.05f) { for (int t = 0; t < 40; t++) { int x = RandI(lv.w), z = RandI(lv.h); if (lv.At(x, z) == T_LOW) { e.p = lv.Center(x, z); break; } } } return; }   // (on a different pole each time you look)
        moveTo(P->p, ed.chase); if (td < 1.1f) attack(*P, (float)ed.damage, 0, ed.name.c_str());
        return;
    }
    if (b == "spider") {   // vibration; webs hold you for a bite; crystal light keeps them off
        bool crystal = false; for (const Loot& l : {P->hands, P->pocket[0], P->pocket[1]}) if (l.def >= 0 && D().loot[l.def].props.find("glowing") != std::string::npos) crystal = true;
        if (crystal && td < 8) { wander(ed.walk * 2); return; }
        int cx = lv.CellX(P->p.x), cz = lv.CellZ(P->p.z); if ((lv.Flags(cx, cz) & CF_WEB) && P->stunT <= 0 && td < 20) { P->stunT = 2; Emit(E_TELL, P->id, e.def, e.level, P->p, 0, "web strands tremble"); }
        if (P->noise > 0.4f && td < 20) { moveTo(P->p, ed.chase); if (td < 1.1f) attack(*P, (float)ed.damage, IN_BLEED, "a Cave Spider"); } else wander(ed.walk);
        return;
    }
    if (b == "leviathan") {   // anything in the deep water too long is taken (to Level 8, with nothing)
        for (auto& p : crew) if (p.level == e.level && p.st == PS_ALIVE) {
            float limit = overtime ? 40.0f : 60.0f;
            if (p.swimT > limit - 15 && p.swimT < limit - 14.9f) Emit(E_TELL, p.id, e.def, e.level, p.p, 0, "the water goes still; a low groan");
            if (p.swimT > limit) { p.swimT = 0; for (auto& t : p.tools) t = Tool{}; p.hands = Loot{}; p.pocket[0] = p.pocket[1] = Loot{}; p.sanity = 10; Emit(E_TAKEN, p.id, e.def, e.level, p.p, 1, "the Leviathan"); Transit(p, 8, true, "pulled down"); }
        }
        return;
    }
    if (b == "seer") {   // eyes in the walls; three at once and a room seals on you
        if (td < 10 && LineOfSight(e.level, e.p, P->Eye())) e.watchT += 0;
        int watching = 0; for (const auto& o : ents) if (o.level == e.level && D().entities[o.def].behaviour == "seer" && Vector3Distance(o.p, P->p) < 10 && LineOfSight(e.level, o.p, P->Eye())) watching++;
        if (watching >= 3 && e.cool <= 0 && P->stunT <= 0) { e.cool = 20; P->stunT = 3; P->sanity -= 10; Emit(E_TELL, P->id, e.def, e.level, P->p, 6, "the room seals"); }
        return;
    }
    if (b == "orderly") {   // a low-health player is paged; it wheels them to surgery
        int paged = -1; for (const auto& p : crew) if (p.level == e.level && p.st == PS_ALIVE && p.health < 50) paged = p.id;
        if (paged < 0) { wander(ed.walk); return; }
        Player& T = crew[paged]; if (e.target != paged) { e.target = paged; Emit(E_PAGE, paged, e.def, e.level, T.p, 0, T.name); }
        moveTo(T.p, ed.chase);
        if (Vector3Distance(T.p, e.p) < 1.2f) { for (int t = 0; t < 100; t++) { int x = RandI(lv.w), z = RandI(lv.h); if (lv.Walkable(x, z) && (lv.Flags(x, z) & CF_ROOM)) { T.p = lv.Center(x, z); break; } } T.health = std::max(T.health, 50.0f); for (auto& tl : T.tools) if (tl.item >= 0 && Rand() < 0.5f) { tl = Tool{}; break; } T.stunT = 4; Emit(E_TAKEN, T.id, e.def, e.level, T.p, 2, "surgery"); e.target = -1; e.st = ES_WANDER; }
        return;
    }
    if (b == "sentry") {   // patrols; tases whatever moves in its sight
        wander(ed.walk);
        if (td < 10 && Vector2Length({P->vel.x, P->vel.z}) > 0.5f && LineOfSight(e.level, e.p, P->Eye()) && e.cool <= 0) { e.cool = 3; Hurt(*P, (float)ed.damage, "a Sentry's taser"); P->stunT = 2; }
        return;
    }
    if (b == "warden") {   // during a lockdown it walks the corridors; anyone in one goes in a cell
        if (lockdownT <= 0) { wander(ed.walk * 0.5f); return; }
        int cx = lv.CellX(P->p.x), cz = lv.CellZ(P->p.z); bool inCell = lv.Flags(cx, cz) & CF_CELL;
        if (!inCell && td < 20) { moveTo(P->p, ed.chase); if (td < 1.2f) { for (int t = 0; t < 100; t++) { int x = RandI(lv.w), z = RandI(lv.h); if (lv.Flags(x, z) & CF_CELL) { P->p = lv.Center(x, z); break; } } P->stunT = 8; Emit(E_TAKEN, P->id, e.def, e.level, P->p, 3, "a cell"); } }
        else wander(ed.walk);
        if (Rand() < dt * 0.2f && td < 25) Emit(E_TELL, P->id, e.def, e.level, e.p, 0, "keys jingling");
        return;
    }
    if (b == "friend") {   // takes what you put down to their clubhouse
        int near = -1; float nd = 10; for (int i = 0; i < (int)items.size(); i++) if (items[i].level == e.level && items[i].loot.def >= 0 && Vector3Distance(items[i].p, e.p) < nd) { nd = Vector3Distance(items[i].p, e.p); near = i; }
        if (near >= 0) { moveTo(items[near].p, ed.chase); if (nd < 0.8f) { items[near].p = lv.start; Emit(E_TELL, -1, e.def, e.level, e.p, 0, "giggles"); } }
        else if (td > 3) moveTo(P->p, ed.walk); return;
    }
    if (b == "innkeeper") {   // polite until you take from the bar
        if (e.mimicOf == -1) { e.mimicOf = -3; e.goal = e.p; }   // (home)
        bool theft = false; for (const auto& p : crew) if (p.level == e.level && p.st == PS_ALIVE) for (const Loot& l : {p.hands, p.pocket[0], p.pocket[1]}) if (l.def >= 0 && Vector3Distance(p.p, e.goal) < 9 && e.st != ES_CHASE && l.uid % 3 == 0) theft = true;
        if (theft) { e.st = ES_CHASE; Emit(E_TELL, P->id, e.def, e.level, e.p, 0, "a bell rings"); }
        if (e.st == ES_CHASE) { moveTo(P->p, ed.chase); if (td < 1.2f) attack(*P, (float)ed.damage, 0, "the Innkeeper"); if (td > 40) e.st = ES_IDLE; }
        return;
    }
    wander(ed.walk);
}

void World::StepEntities() {
    SpawnEntities(STEP);
    std::vector<int> occupied; for (const auto& p : crew) if (p.Alive()) occupied.push_back(p.level);
    for (auto& e : ents) if (!e.hallucination && std::find(occupied.begin(), occupied.end(), e.level) != occupied.end()) StepEntity(e);
    // entities drift away from levels nobody's on (they don't wait)
    ents.erase(std::remove_if(ents.begin(), ents.end(), [&](const Entity& e) { return e.st == ES_GONE || std::find(occupied.begin(), occupied.end(), e.level) == occupied.end(); }), ents.end());
    // a breach: an entity in a Lab's drop-off while nobody's there tips the crate
    for (auto& e : ents) {
        if (D().entities[e.def].behaviour == "seer" || D().entities[e.def].behaviour == "leviathan") continue;
        LabState* lab = LabAt(e.level, e.p); if (!lab || lab->crate.empty()) continue;
        bool manned = false; for (const auto& p : crew) if (p.Alive() && LabAt(p.level, p.p) == lab) manned = true; if (manned) continue;
        int n = std::max(1, (int)lab->crate.size() / 3); Level& lv = L(e.level); const LabPlan& lp = lv.labs[lab->idx];
        for (int k = 0; k < n && !lab->crate.empty(); k++) { WorldItem wi; wi.loot = lab->crate.back(); lab->crate.pop_back(); wi.level = e.level; wi.p = lv.Center(lp.x0 + RandI(lp.x1 - lp.x0 + 1), lp.z0 + RandI(lp.z1 - lp.z0 + 1)); items.push_back(wi); }
        Emit(E_BREACH, -1, e.def, e.level, e.p, (float)n);
        e.st = ES_GONE;
    }
}

void World::ToolOnEntities(Player& p, const std::string& use) {
    Vector3 eye = p.Eye();
    if (use == "shotgun") p.noise = 4;
    for (auto& e : ents) {
        if (e.level != p.level || e.hallucination) continue; const EntityDef& ed = D().entities[e.def];
        float d = Vector3Distance(e.p, p.p), a = AngleTo(p, e.p);
        bool los = LineOfSight(e.level, eye, Vector3Add(e.p, {0, 1, 0}));
        if (use == "crowbar" && d < 2.2f && a < 0.6f) { e.stunT = ed.id == "hound" ? 4 : 2; if (ed.id == "mirrorthing" || ed.id == "partygoer") e.st = ES_GONE; Emit(E_TELL, p.id, e.def, p.level, e.p, 7, "a solid hit"); }
        if (use == "shotgun" && d < 15 && a < 0.35f && los) { e.stunT = 5; if (ed.id == "mirrorthing" || ed.id == "partygoer") e.st = ES_GONE; }
        if (use == "camera" && d < 15 && a < 0.4f && los) {
            if (ed.id == "smiler") { e.st = ES_FLEE; e.t = 0; }
            bool lit = LightAt(e.level, e.p) > 0.3f || true;   // (the flash lights it)
            if (lit) { bool first = !((dossier >> e.def) & 1); dossier |= 1u << e.def; p.photos++; Emit(E_PHOTO, p.id, e.def, e.level, e.p, first ? (float)ed.bounty : 0); if (first) credit += ed.bounty; if (contract >= 0 && D().contracts[contract].id == "documentation") { int n = 0; for (int k = 0; k < (int)D().entities.size(); k++) n += (dossier >> k) & 1; if (n >= 3) contractDone = true; } }
        }
    }
}

}  // namespace nc
