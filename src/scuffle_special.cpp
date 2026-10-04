// Scuffle stage 6: the full arsenal's strange half and gear (doc pp. 5-8, 14). Headless.
//   Statuses on a stick (StepStatus): burning (8 a second; water puts it out; wood catches), frozen (a statue for 2 s,
//   then a ragdoll; a hit shatters it), bubbled (carried upward 3 s; any hit pops it), netted (held 3 s), flipped (the
//   gravity gun: up is down while the beam is on it), trapped (a bear trap holds until you're hit free).
//   Things (StepThings): bees (follow the nearest stick, which is sometimes you), snakes (slither to the nearest stick and
//   bite), fish (come to chum, in the Reef and the Void), a black hole (pulls everything within 6 m for 4 s; the middle is
//   gone), chum, portals (two per owner: sticks and bullets go through), a bear trap, a turret (shoots the nearest stick for
//   10 s), a mine, a banana peel, a decoy (bees, snakes and turrets prefer it), a spring, a stuck charge, and beams.
//   Beams (SpecialFire): the tesla gun (chains to two more sticks, and through water), the gravity gun, the laser (reflects
//   off ice and glass, cuts ropes). Thrown things are bullets with gravity that become things where they land.
//   Gear (StepGear, the gear button): grappling hook, shield, jetpack, decoy, parachute, spring, rope.
#include "scuffle.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>

namespace sf {

const char* GearName(int g) { static const char* N[GR_COUNT] = {"Grappling hook", "Shield", "Jetpack", "Decoy", "Parachute", "Spring", "Rope", "Mirror", "Balloon", "Fishbowl helmet"}; return g >= 0 && g < GR_COUNT ? N[g] : "?"; }
static const WeaponDef& WDef(int i) { static WeaponDef none; return i >= 0 && i < (int)Weapons().size() ? Weapons()[i] : none; }
static int Blame(const World& w, const Stick& k) { return k.lastHitBy >= 0 && w.t - k.lastHitT < 4 ? k.lastHitBy : -1; }

int World::AddThing(int kind, Vector2 p, Vector2 v, float life, int owner) {
    Thing th; th.kind = (uint8_t)kind; th.p = p; th.v = v; th.life = life; th.owner = owner;
    things.push_back(th);
    return (int)things.size() - 1;
}
void World::Snakes(Vector2 at, int n, int owner) {
    for (int i = 0; i < n; i++) { int s = AddThing(TH_SNAKE, Vector2Add(at, {(i - (n - 1) * 0.5f) * 0.3f, 0.2f}), {(Rand() - 0.5f) * 4, 2}, 14, owner); things[s].cool = 0.4f; }
    Emit(EV_SNAKE, at, -1, owner, (float)n);
}
void World::Burn(Stick& k, float s, int by) {
    if (!k.alive || k.wet || (k.trinket == TK_THICK_COAT && !(k.windUsed && k.hp <= 1))) return;   // (Thick Coat: fire doesn't stick; the Second Wind's fire still does)
    if (k.burnT <= 0) Emit(EV_BURN, k.pt[J_PELVIS].p, k.id, by);
    k.burnT = std::max(k.burnT, s);
    if (by >= 0) { k.lastHitBy = by; k.lastHitT = t; }
    if (k.gear == GR_SHIELD) k.gear = -1;   // (the shield melts)
}
void World::Freeze(Stick& k, float s, int by) {
    if (!k.alive || k.trinket == TK_THICK_COAT) return;
    k.frozenT = s; k.burnT = 0; k.vel = {0, 0};
    if (by >= 0) { k.lastHitBy = by; k.lastHitT = t; }
    Emit(EV_FREEZE, k.pt[J_PELVIS].p, k.id, by);
}
// the tesla: the first stick, then up to two more within 3 m of the last; anyone wet near a wet target too
void World::Zap(Vector2 from, Stick& first, float dmg, int by, int weapon) {
    std::vector<int> hit{first.id};
    Vector2 at = first.pt[J_NECK].p;
    int b = AddThing(TH_BEAM, from, {}, 0.18f, by); things[b].q = at; things[b].a = 1;
    if (dmg > 0) Hit(first, by, dmg, Vector2Normalize(Vector2Subtract(at, from)), 3, false, WDef(weapon).name.c_str());
    for (int n = 0; n < 2; n++) {
        int best = -1; float bd = 3;
        for (const auto& o : sticks) if (o.present && o.alive && std::find(hit.begin(), hit.end(), o.id) == hit.end() && Vector2Distance(o.pt[J_NECK].p, at) < bd) { bd = Vector2Distance(o.pt[J_NECK].p, at); best = o.id; }
        if (best < 0) break;
        Stick& o = sticks[best]; hit.push_back(best);
        int bb = AddThing(TH_BEAM, at, {}, 0.18f, by); things[bb].q = o.pt[J_NECK].p; things[bb].a = 1;
        Hit(o, by, (dmg > 0 ? dmg : WDef(weapon).dmg) * 0.7f, Vector2Normalize(Vector2Subtract(o.pt[J_NECK].p, at)), 2, false, WDef(weapon).name.c_str());
        at = o.pt[J_NECK].p;
    }
    if (first.wet) for (auto& o : sticks) if (o.present && o.alive && o.wet && std::find(hit.begin(), hit.end(), o.id) == hit.end() && Vector2Distance(o.pt[J_PELVIS].p, first.pt[J_PELVIS].p) < 10) {
        int bb = AddThing(TH_BEAM, first.pt[J_PELVIS].p, {}, 0.18f, by); things[bb].q = o.pt[J_PELVIS].p; things[bb].a = 1;
        Hit(o, by, std::max(dmg, WDef(weapon).dmg), {0, 1}, 2, false, WDef(weapon).name.c_str());
    }
    Emit(EV_ZAP, at, first.id, by);
}

// ---------------------------------------------------------------- statuses
void World::StepStatus(Stick& k) {
    if (!k.present) return;
    const float dt = STEP;
    if (k.burnT > 0) {
        k.burnT -= dt;
        if (k.wet) k.burnT = 0;
        if (k.alive) { k.hp -= 12 * dt; if (k.hp <= 0) Kill(k, Blame(*this, k), "fire"); }
    }
    if (!k.alive) { k.frozenT = k.bubbleT = k.netT = k.gravT = k.trapT = 0; k.hookOn = false; return; }
    if (k.frozenT > 0) {
        k.frozenT -= dt; k.in = Input{}; k.vel.x = 0;
        if (k.frozenT <= 0) { k.st = S_RAGDOLL; k.ragT = 0.6f; k.stiff = 0; Emit(EV_HIT, k.pt[J_PELVIS].p, k.id, -1, 0); }
    }
    if (k.netT > 0) { k.netT -= dt; Input keep = k.in; k.in = Input{}; k.in.aim = keep.aim; k.vel.x *= 0.5f; }
    if (k.trapT > 0) { k.trapT -= dt; k.in.moveX = 0; k.in.jump = false; k.vel.x = 0; }
    if (k.bubbleT > 0) {
        k.bubbleT -= dt; k.vel.y = 2.4f; k.in.moveX = 0; k.in.jump = false; k.in.fire = false; k.st = S_AIR;   // (inside a bubble you can't do anything)
        for (auto& a : k.pt) a.q.y -= gravity * dt * dt;   // (the body floats too)
    }
    if (k.gravT > 0) k.gravT -= dt;
    if (k.balloonT > 0) { k.balloonT -= dt; k.vel.y = std::max(k.vel.y, 1.6f); k.st = S_AIR; k.fallTop = k.pos.y; }   // (the balloon: up, steering as you like)
    k.gearCool = std::max(0.0f, k.gearCool - dt);
}

// ---------------------------------------------------------------- the beams and the thrown things (from Fire)
static bool RayStick(const World& w, Vector2 from, Vector2 dir, float range, int skip, int* who, Vector2* end) {
    for (float s = 0.15f; s < range; s += 0.12f) {
        Vector2 p = Vector2Add(from, Vector2Scale(dir, s));
        if (w.stage.Solid((int)floorf(p.x / TILE), (int)floorf(p.y / TILE))) { *end = p; *who = -1; return false; }
        for (const auto& o : w.sticks) {
            if (o.id == skip || !o.present || !o.alive) continue;
            for (int j : {J_HEAD, J_NECK, J_PELVIS, J_KNEE_L, J_KNEE_R}) if (Vector2Distance(o.pt[j].p, p) < o.pt[j].r + 0.12f) { *who = o.id; *end = p; return true; }
        }
    }
    *end = Vector2Add(from, Vector2Scale(dir, range)); *who = -1; return false;
}
void World::SpecialFire(Stick& k, const WeaponDef& d, Item& it, Vector2 aim) {
    Vector2 from = it.b.p;
    if (d.special == "chain") {   // the tesla gun: an instant arc
        int who = -1; Vector2 end;
        if (RayStick(*this, from, aim, 12, k.id, &who, &end)) Zap(from, sticks[who], d.dmg, k.id, it.weapon);
        else { int b = AddThing(TH_BEAM, from, {}, 0.12f, k.id); things[b].q = end; things[b].a = 1; }
        return;
    }
    if (d.special == "gravity") {   // the gravity gun: whoever the beam is on falls up
        int who = -1; Vector2 end;
        if (RayStick(*this, from, aim, 8, k.id, &who, &end) && sticks[who].trinket != TK_DEADWEIGHT) { Stick& o = sticks[who]; if (o.gravT <= 0) Emit(EV_HIT, o.pt[J_PELVIS].p, o.id, k.id, 0); o.gravT = 0.25f; o.lastHitBy = k.id; o.lastHitT = t; }
        int b = AddThing(TH_BEAM, from, {}, STEP * 3, k.id); things[b].q = end; things[b].a = 2;
        for (auto& itm : items) if (itm.alive && itm.holder < 0 && fabsf(Vector2DotProduct(Vector2Subtract(itm.a.p, from), {-aim.y, aim.x})) < 0.4f && Vector2DotProduct(Vector2Subtract(itm.a.p, from), aim) > 0 && Vector2Distance(itm.a.p, from) < 8) { itm.a.q.y -= gravity * 2 * STEP * STEP; itm.b.q.y -= gravity * 2 * STEP * STEP; }
        return;
    }
    if (d.special == "laser") {   // the laser: a beam that reflects off ice and glass and cuts ropes; it stops in the first stick
        Vector2 p = from, dir = aim; int bounces = 0;
        for (float s = 0; s < 22; s += 0.08f) {
            Vector2 n = Vector2Add(p, Vector2Scale(dir, 0.08f));
            int tx = (int)floorf(n.x / TILE), ty = (int)floorf(n.y / TILE); uint8_t tile = stage.At(tx, ty);
            if (tile == T_ROPE) { stage.Set(tx, ty, T_EMPTY); Emit(EV_HIT, n, -1, k.id, 0); }
            if ((tile == T_ICE || tile == T_GLASS) && bounces < 3) {
                bool fx = stage.Solid((int)floorf(n.x / TILE), (int)floorf(p.y / TILE)), fy = stage.Solid((int)floorf(p.x / TILE), (int)floorf(n.y / TILE));
                int b = AddThing(TH_BEAM, from, {}, STEP * 3, k.id); things[b].q = p; things[b].a = 3; from = p;
                if (fx || !fy) dir.x = -dir.x; if (fy || !fx) dir.y = -dir.y; bounces++; continue;
            }
            if (stage.Solid(tx, ty)) break;
            p = n;
            bool hit = false;
            for (auto& o : sticks) {
                if (o.id == k.id || !o.present || !o.alive) continue;
                for (int j : {J_HEAD, J_NECK, J_PELVIS}) if (Vector2Distance(o.pt[j].p, p) < o.pt[j].r + 0.08f) { Hit(o, k.id, d.dmg / std::max(1.0f, d.rate), dir, 0.5f, false, d.name.c_str()); hit = true; break; }
                if (hit) break;
            }
            if (hit) break;
        }
        int b = AddThing(TH_BEAM, from, {}, STEP * 3, k.id); things[b].q = p; things[b].a = 3;
        return;
    }
}

// ---------------------------------------------------------------- a special bullet's impact
bool World::SpecialHit(Bullet& b, Vector2 at, Stick* k) {
    const WeaponDef& d = WDef(b.weapon);
    const std::string& sp = d.special;
    if (b.hazard != -1 || (sp.empty() && d.kind != "thrown")) return false;
    int by = b.owner;
    auto floorAt = [&](Vector2 p) { for (int i = 0; i < 40; i++) { if (stage.Solid((int)floorf(p.x / TILE), (int)floorf((p.y - 0.02f) / TILE))) return p; p.y -= 0.05f; } return p; };
    if (sp == "fire") { if (k) { if (d.dmg > 0) Hit(*k, by, d.dmg, Vector2Normalize(b.v), d.knock, false, d.name.c_str()); Burn(*k, d.key == "flare" ? 3.0f : 1.2f, by); return true; }
                        int tx = (int)floorf(at.x / TILE), ty = (int)floorf(at.y / TILE);
                        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) { uint8_t q = stage.At(tx + dx, ty + dy); int i = (ty + dy) * stage.w + tx + dx;
                            if (q == T_WOOD && i >= 0 && i < (int)fireT.size() && fireT[i] <= 0) fireT[i] = STEP; else if (q == T_ICE) stage.Set(tx + dx, ty + dy, T_EMPTY); }
                        return true; }
    if (sp == "freeze") { if (k) { if (d.dmg > 0) Hit(*k, by, d.dmg, Vector2Normalize(b.v), 0, false, d.name.c_str()); Freeze(*k, 1.4f, by); return true; }   // (a shot at a frozen stick shatters it: Hit adds 20)
                          int tx = (int)floorf(at.x / TILE), ty = (int)floorf(at.y / TILE);
                          for (int dy = -1; dy <= 1; dy++) for (int dx = -2; dx <= 2; dx++) if (stage.Liquid(tx + dx, ty + dy)) stage.Set(tx + dx, ty + dy, T_ICE);
                          return true; }
    if (sp == "snake") { if (k) Hit(*k, by, d.dmg, Vector2Normalize(b.v), 1, false, "a snake"); Snakes(k ? k->pt[J_PELVIS].p : at, 1, by); return true; }
    if (sp == "bees") { int s = AddThing(TH_SWARM, at, {}, d.kind == "thrown" ? 8.0f : 7.0f, by); things[s].weapon = b.weapon; Emit(EV_BEES, at, -1, by); return true; }
    if (sp == "blackhole") { AddThing(TH_HOLE, at, {}, 4, by); Emit(EV_EXPLODE, at, -1, by, 0.2f); return true; }
    if (sp == "bubble") { if (k && k->trinket == TK_DEADWEIGHT) return true; if (k && d.dmg > 0) Hit(*k, by, d.dmg, {0, 1}, 0, false, d.name.c_str()); if (k && k->alive) { k->bubbleT = 3; k->vel.x = Vector2Normalize(b.v).x * 2.2f;   /* (carried along the shot as it rises) */ k->lastHitBy = by; k->lastHitT = t; Emit(EV_BUBBLE, k->pt[J_PELVIS].p, k->id, by); } return true; }
    if (sp == "net") { if (k) { k->netT = 3; Hit(*k, by, d.dmg, Vector2Normalize(b.v), 0, false, d.name.c_str()); Emit(EV_TRAP, k->pt[J_PELVIS].p, k->id, by); } return true; }
    if (sp == "chum") { if (k) Hit(*k, by, d.dmg, Vector2Normalize(b.v), d.knock, false, d.name.c_str()); AddThing(TH_CHUM, k ? k->pt[J_PELVIS].p : at, {}, 8, by); return true; }
    if (sp == "portal") {
        if (k) return false;   // (a portal opens on a surface; a stick in the way just takes the shot)
        int mine = 0, oldest = -1; float oldT = -1;
        for (int i = 0; i < (int)things.size(); i++) if (things[i].alive && things[i].kind == TH_PORTAL && things[i].owner == by) { mine++; if (things[i].t > oldT) { oldT = things[i].t; oldest = i; } }
        if (mine >= 2 && oldest >= 0) things[oldest].alive = false;
        Vector2 n = Vector2Normalize(Vector2Negate(b.v));
        int p = AddThing(TH_PORTAL, Vector2Add(at, Vector2Scale(n, 0.05f)), {}, 1e9f, by); things[p].q = n;
        int idx = 0; for (const auto& th : things) if (th.alive && th.kind == TH_PORTAL && th.owner == by && &th != &things[p]) idx = 1 - (int)th.a;
        things[p].a = (float)idx;
        Emit(EV_PORTAL, at, -1, by);
        return true;
    }
    if (d.kind == "thrown") {
        Vector2 down = floorAt(at);
        if (sp == "stick") { int s = AddThing(TH_STUCK, at, {}, d.fuse, by); things[s].on = k ? k->id : -1; if (k) things[s].q = Vector2Subtract(at, k->pt[J_PELVIS].p); things[s].weapon = b.weapon; return true; }
        if (d.key == "sticky") { int s = AddThing(TH_STUCK, at, {}, d.fuse, by); things[s].on = k ? k->id : -1; if (k) things[s].q = Vector2Subtract(at, k->pt[J_PELVIS].p); things[s].weapon = b.weapon; return true; }
        if (sp == "ink") { inkT = 3; Emit(EV_INK, at, -1, by); return true; }
        if (sp == "flash") {   // (the white-out, and a concussion: anyone within 3 m is knocked down)
            flashT = 1.5f; Emit(EV_FLASH, at, -1, by);
            for (auto& o : sticks) { if (!o.present || !o.alive || o.id == by) continue; Vector2 dd = Vector2Subtract(o.pt[J_PELVIS].p, at); float L = Vector2Length(dd); if (L < 3) Hit(o, by, 14, Vector2Normalize(Vector2Add(dd, {0, 0.6f})), 11 * (1 - L / 3) + 4, true, d.name.c_str()); }
            return true;
        }
        if (sp == "trap") { if (k) { Hit(*k, by, 15, {0, -1}, 0, false, d.name.c_str()); k->trapT = 4; Emit(EV_TRAP, k->pt[J_FOOT_L].p, k->id, by); return true; } AddThing(TH_TRAP, down, {}, 60, by); return true; }
        if (sp == "turret") { int s = AddThing(TH_TURRET, down, {}, 10, by); things[s].cool = 0.6f; return true; }
        if (sp == "mine") { int s = AddThing(TH_MINE, down, {}, 60, by); things[s].cool = 0.3f; things[s].weapon = b.weapon; return true; }
        if (sp == "slip") { if (k) { k->st = S_RAGDOLL; k->ragT = 0.8f; k->stiff = 0; k->lastHitBy = by; k->lastHitT = t; for (auto& a : k->pt) a.q = Vector2Subtract(a.p, Vector2Scale({Vector2Normalize(b.v).x * 6, 3}, STEP)); return true; } AddThing(TH_PEEL, down, {}, 60, by); return true; }
        if (sp == "snakes") { Snakes(down, 4, by); return true; }
        if (sp == "bottle") { if (k) Hit(*k, by, d.dmg, Vector2Normalize(b.v), 7, false, "a bottle"); Emit(EV_HIT, at, -1, by, 0); return true; }
    }
    return false;
}

// ---------------------------------------------------------------- things
void World::StepThings() {
    const float dt = STEP;
    inkT = std::max(0.0f, inkT - dt); flashT = std::max(0.0f, flashT - dt);
    // burning wood: it burns 2 s, catches its neighbours at 0.7 s, and sets whoever touches it alight
    if (fireT.size() != stage.t.size()) fireT.assign(stage.t.size(), 0);
    for (int i = 0; i < (int)fireT.size(); i++) {
        if (fireT[i] <= 0) continue;
        if (stage.t[i] != T_WOOD) { fireT[i] = 0; continue; }
        float before = fireT[i]; fireT[i] += dt;
        int x = i % stage.w, y = i / stage.w;
        if (before < 0.7f && fireT[i] >= 0.7f) for (int dd = 0; dd < 4; dd++) { int nx = x + (dd == 0) - (dd == 1), ny = y + (dd == 2) - (dd == 3); int j = ny * stage.w + nx; if (nx >= 0 && ny >= 0 && nx < stage.w && ny < stage.h && stage.t[j] == T_WOOD && fireT[j] <= 0) fireT[j] = dt; }
        if (fireT[i] > 2) { stage.t[i] = T_EMPTY; fireT[i] = 0; Emit(EV_HIT, {(x + 0.5f) * TILE, (y + 0.5f) * TILE}, -1, -1, 0); continue; }
        Rectangle r{x * TILE - 0.1f, y * TILE - 0.1f, TILE + 0.2f, TILE + 0.3f};
        for (auto& k : sticks) if (k.alive && k.present && k.pos.x + k.halfW > r.x && k.pos.x - k.halfW < r.x + r.width && k.pos.y < r.y + r.height && k.pos.y + k.height > r.y) Burn(k, 1.0f, -1);
    }
    auto target = [&](Vector2 from, int owner, bool avoidOwner, float range) -> Vector2* {   // (the nearest living stick, or a decoy if one is near)
        static Vector2 tp; float best = range; bool found = false;
        for (const auto& th : things) if (th.alive && th.kind == TH_DECOY && Vector2Distance(th.p, from) < range) { tp = Vector2Add(th.p, {0, 0.8f}); return &tp; }
        for (const auto& k : sticks) { if (!k.alive || !k.present || (avoidOwner && k.id == owner)) continue; float dd = Vector2Distance(k.pt[J_PELVIS].p, from); if (dd < best) { best = dd; tp = k.pt[J_PELVIS].p; found = true; } }
        return found ? &tp : nullptr;
    };
    size_t n0 = things.size();
    things.reserve(n0 + 16);
    for (size_t i = 0; i < n0; i++) {
        Thing& th = things[i];
        if (!th.alive) continue;
        th.t += dt; th.life -= dt; th.cool = std::max(0.0f, th.cool - dt);
        if (th.life <= 0 && th.kind != TH_STUCK) { th.alive = false; continue; }
        switch (th.kind) {
        case TH_SWARM: {
            // (bees go for the nearest stick but their owner's, unless the owner is much the nearer: it's sometimes you)
            bool skipOwner = true; if (th.owner >= 0 && th.owner < (int)sticks.size() && sticks[th.owner].alive) { float od = Vector2Distance(sticks[th.owner].pt[J_PELVIS].p, th.p), ed = 1e9f; for (const auto& o : sticks) if (o.alive && o.present && o.id != th.owner) ed = std::min(ed, Vector2Distance(o.pt[J_PELVIS].p, th.p)); skipOwner = th.t < 2.0f || od > ed * 0.4f; }
            Vector2* tg = target(th.p, th.owner, skipOwner, 30);
            if (tg) { Vector2 d = Vector2Subtract(*tg, th.p); float L = Vector2Length(d); if (L > 0.05f) th.v = Vector2Lerp(th.v, Vector2Scale(d, 9.0f / L), 0.08f); }
            th.v = Vector2Add(th.v, {(Rand() - 0.5f) * 0.6f, (Rand() - 0.5f) * 0.6f});
            th.p = Vector2Add(th.p, Vector2Scale(th.v, dt));
            for (auto& k : sticks) {
                if (!k.alive || !k.present || Vector2Distance(k.pt[J_PELVIS].p, th.p) > 0.8f) continue;
                if (k.weapon >= 0 && k.weapon < (int)items.size() && WDef(items[k.weapon].weapon).key == "fryingpan" && k.swingT > 0) { th.alive = false; Emit(EV_HIT, th.p, -1, k.id, 0); break; }   // (the frying pan cooks bees)
                if (th.cool <= 0) { Hit(k, th.owner == k.id ? -1 : th.owner, WDef(th.weapon).dmg > 0 ? WDef(th.weapon).dmg : 8, {0, 0.2f}, 0.5f, false, "bees"); th.cool = 0.25f; }
            }
            break;
        }
        case TH_SNAKE: case TH_FISH: {
            bool fish = th.kind == TH_FISH;
            Vector2 goal{};
            bool have = false;
            if (fish) for (const auto& c : things) if (c.alive && c.kind == TH_CHUM) { goal = c.p; have = true; break; }
            if (!have && !fish) for (const auto& c : sticks) if (c.alive && c.present && c.trinket == TK_SNAKE_CHARMER && c.id != th.owner) { goal = c.pt[J_PELVIS].p; have = true; break; }   // (everyone else's snakes go to the Snake Charmer)
            if (!have) { Vector2* tg = target(th.p, th.owner, th.t < 3.0f, 25); if (tg) { goal = *tg; have = true; } }
            if (fish) {   // (fish swim straight to it)
                if (have) { Vector2 d = Vector2Subtract(goal, th.p); float L = Vector2Length(d); if (L > 0.05f) th.v = Vector2Lerp(th.v, Vector2Scale(d, 5 / L), 0.08f); }
                th.p = Vector2Add(th.p, Vector2Scale(th.v, dt));
            } else {      // (a snake falls, then slithers along whatever it's on)
                th.v.y -= gravity * dt;
                Vector2 np = Vector2Add(th.p, Vector2Scale(th.v, dt));
                if (stage.Solid((int)floorf(np.x / TILE), (int)floorf((np.y - 0.02f) / TILE))) { np.y = (floorf((np.y - 0.02f) / TILE) + 1) * TILE; th.v.y = 0;
                    if (have) { th.v.x = (goal.x > th.p.x ? 1 : -1) * 3.2f; if (fabsf(goal.x - th.p.x) < 1.6f && fabsf(goal.y - th.p.y) < 1.5f && th.cool <= 0 && Rand() < 0.04f) th.v.y = 6; }   // (a lunge at whoever's close)
                    if (stage.Solid((int)floorf((np.x + (th.v.x > 0 ? 0.15f : -0.15f)) / TILE), (int)floorf((np.y + 0.1f) / TILE))) { th.v.x = -th.v.x; np.x = th.p.x; } }
                th.p = np;
                if (th.p.y < -4) th.alive = false;
            }
            for (auto& k : sticks) {
                if (!k.alive || !k.present || th.cool > 0 || (!fish && k.trinket == TK_SNAKE_CHARMER) || (k.id == th.owner && th.t < 3.0f)) continue;   // (snakes don't bite the charmer, nor their shooter at first)
                Vector2 c = fish ? k.pt[J_PELVIS].p : k.pos;
                if (Vector2Distance(c, th.p) > (fish ? 0.6f : 0.45f)) continue;
                Hit(k, th.owner == k.id ? -1 : th.owner, fish ? 10 : 25, {0, 0.3f}, 1, false, fish ? "fish" : "a snake");
                th.cool = fish ? 0.8f : 0.9f;
                Emit(EV_SNAKE, th.p, k.id, th.owner, 0);
            }
            // fists and blasts kill them
            for (const auto& k : sticks) if (k.alive && k.present && (k.punchT > 0 || k.kickT > 0) && (Vector2Distance(k.pt[J_HAND_R].p, th.p) < 0.35f || Vector2Distance(k.pt[J_FOOT_R].p, th.p) < 0.35f)) { th.alive = false; Emit(EV_HIT, th.p, -1, k.id, 0); }
            break;
        }
        case TH_HOLE: {
            // everything within 6 m is pulled in; the middle takes sticks, crates, weapons and bullets
            for (auto& k : sticks) {
                if (!k.present) continue;
                Vector2 d = Vector2Subtract(th.p, k.pt[J_PELVIS].p); float L = Vector2Length(d);
                if (L > 6 || L < 0.01f) continue;
                if (L < 0.55f) { if (k.alive) { Kill(k, th.owner == k.id ? Blame(*this, k) : th.owner, "the black hole"); } k.present = false; Emit(EV_EXPLODE, th.p, k.id, th.owner, 0.2f); continue; }
                float pull = 7 * (1 - L / 6) + 1;
                Nudge(k, Vector2Scale(d, pull * dt / L));
                for (auto& a : k.pt) a.q = Vector2Subtract(a.q, Vector2Scale(d, pull * 0.4f * dt * dt / L));
                if (k.alive && k.st != S_RAGDOLL && L < 2.5f) { k.st = S_RAGDOLL; k.ragT = 0.5f; k.stiff = 0; }
            }
            for (auto& it : items) { if (!it.alive || it.holder >= 0) continue; Vector2 d = Vector2Subtract(th.p, it.a.p); float L = Vector2Length(d); if (L > 6) continue; if (L < 0.5f) { it.alive = false; continue; } it.a.q = Vector2Subtract(it.a.q, Vector2Scale(d, 8 * dt * dt / L)); it.b.q = Vector2Subtract(it.b.q, Vector2Scale(d, 8 * dt * dt / L)); }
            for (auto& b : bullets) { Vector2 d = Vector2Subtract(th.p, b.p); float L = Vector2Length(d); if (L > 6) continue; if (L < 0.4f) { b.alive = false; continue; } b.v = Vector2Add(b.v, Vector2Scale(d, 40 * dt / L)); }
            break;
        }
        case TH_CHUM: {
            // the stage's fish come to it (in the Reef and the Void): one a second, from the nearest water or the stage's bottom
            if ((stage.world == WD_REEF || stage.world == WD_VOID) && th.cool <= 0) {
                th.cool = 1.0f;
                int fish = 0; for (const auto& f : things) fish += f.alive && f.kind == TH_FISH;
                if (fish < 6) { Vector2 from{th.p.x + (Rand() < 0.5f ? -8.0f : 8.0f), std::max(0.3f, th.p.y - 4)}; int f = AddThing(TH_FISH, from, {}, 9, th.owner); things[f].cool = 0.5f; }
            }
            break;
        }
        case TH_PORTAL: {
            int other = -1;
            for (size_t j = 0; j < things.size(); j++) if (j != i && things[j].alive && things[j].kind == TH_PORTAL && things[j].owner == th.owner) other = (int)j;
            if (other < 0 || th.cool > 0) break;
            Thing& o = things[other];
            for (auto& k : sticks) {
                Vector2 rel = Vector2Subtract(k.pt[J_PELVIS].p, th.p); float along = Vector2DotProduct(rel, th.q), side = fabsf(rel.x * th.q.y - rel.y * th.q.x);
                if (!k.present || along < -0.3f || along > 0.55f || side > 0.95f) continue;   // (a portal is a tall oval)
                Vector2 d = Vector2Subtract(Vector2Add(o.p, Vector2Scale(o.q, 0.8f)), k.pt[J_PELVIS].p);
                for (auto& a : k.pt) { a.p = Vector2Add(a.p, d); a.q = Vector2Add(a.q, d); }
                k.pos = Vector2Add(k.pos, d);
                th.cool = o.cool = 0.5f; Emit(EV_PORTAL, o.p, k.id, th.owner);
            }
            for (auto& b : bullets) if (b.alive && Vector2Distance(b.p, th.p) < 0.45f) { b.p = Vector2Add(o.p, Vector2Scale(o.q, 0.5f)); b.v = Vector2Scale(o.q, Vector2Length(b.v)); }
            break;
        }
        case TH_TRAP: {
            for (auto& k : sticks) if (k.alive && k.present && k.trapT <= 0 && fabsf(k.pos.x - th.p.x) < 0.35f && fabsf(k.pos.y - th.p.y) < 0.2f) {
                Hit(k, th.owner == k.id ? -1 : th.owner, 15, {0, -1}, 0, false, "a bear trap"); k.trapT = 4; th.alive = false; Emit(EV_TRAP, th.p, k.id, th.owner); break;
            }
            break;
        }
        case TH_TURRET: {
            if (th.cool > 0) break;
            Vector2 eye = Vector2Add(th.p, {0, 0.5f});
            Vector2* tg = target(eye, th.owner, true, 14);
            if (!tg || !LineOfSight(eye, *tg)) break;
            Vector2 d = Vector2Normalize(Vector2Subtract(*tg, eye));
            Bullet b; b.p = Vector2Add(eye, Vector2Scale(d, 0.4f)); b.v = Vector2Scale(d, 50); b.owner = th.owner; b.weapon = -1; b.hazard = -3; b.dmg = 12; b.knock = 3; b.life = 1;
            bullets.push_back(b); th.cool = 0.5f; th.a = atan2f(d.y, d.x);
            Emit(EV_SHOT, b.p, -1, th.owner, -1);
            break;
        }
        case TH_MINE: {
            if (th.cool > 0) break;
            for (const auto& k : sticks) if (k.alive && k.present && Vector2Distance(k.pos, th.p) < 0.5f) { const WeaponDef& d = WDef(th.weapon); Explode(Vector2Add(th.p, {0, 0.2f}), d.area > 0 ? d.area : 1.6f, d.areaDmg > 0 ? d.areaDmg : 70, 12, th.owner, th.weapon); th.alive = false; break; }
            break;
        }
        case TH_PEEL: {
            for (auto& k : sticks) if (k.alive && k.present && k.st != S_RAGDOLL && fabsf(k.vel.x) > 1 && Vector2Distance(k.pos, th.p) < 0.35f) {
                k.st = S_RAGDOLL; k.ragT = 1.7f; k.stiff = 0; for (auto& a : k.pt) a.q = Vector2Subtract(a.p, Vector2Scale({k.vel.x * 2.6f + (k.vel.x >= 0 ? 3.0f : -3.0f), 4}, dt));   // (feet out from under you: you slide on, faster)
                if (th.owner != k.id) { k.lastHitBy = th.owner; k.lastHitT = t; }
                th.alive = false; Emit(EV_HIT, th.p, k.id, th.owner, 0); break;
            }
            break;
        }
        case TH_SPRING: {
            for (auto& k : sticks) if (k.alive && k.present && k.vel.y < -1 && fabsf(k.pos.x - th.p.x) < 0.5f && k.pos.y - th.p.y < 0.35f && k.pos.y - th.p.y > -0.1f) { k.vel.y = 18; k.st = S_AIR; k.grounded = false; th.cool = 0.3f; Emit(EV_JUMP, th.p, k.id); }
            break;
        }
        case TH_STUCK: {
            if (th.on >= 0 && th.on < (int)sticks.size() && sticks[th.on].present) th.p = Vector2Add(sticks[th.on].pt[J_PELVIS].p, th.q);
            if (th.life <= 0) { const WeaponDef& d = WDef(th.weapon); Explode(th.p, d.area > 0 ? d.area : 2.2f, d.areaDmg > 0 ? d.areaDmg : 60, 12, th.owner, th.weapon); th.alive = false; }
            break;
        }
        case TH_DOG: {   // (the alley dog runs through, along the ground, and bites whoever's closest to it)
            th.p.x += th.v.x * dt;
            int col = (int)floorf(th.p.x / TILE); float top = -10; for (int y = stage.h - 1; y >= 0; y--) if (stage.Solid(col, y) && !stage.Solid(col, y + 1)) { if ((y + 1) * TILE <= th.p.y + 1.0f) { top = (y + 1) * TILE; break; } }
            th.p.y = top > -5 ? top : th.p.y - 6 * dt; th.a += dt;
            if (th.p.x > stage.Width() + 1) th.alive = false;
            for (auto& k : sticks) if (k.alive && k.present && k.hazT <= 0 && fabsf(k.pos.x - th.p.x) < 0.6f && fabsf(k.pos.y - th.p.y) < 1.2f) { k.hazT = 0.8f; Hit(k, -1, 15, Vector2Normalize({1, 0.5f}), 6, false, "the alley dog"); Emit(EV_PUNCH, k.pt[J_PELVIS].p, k.id); }
            break;
        }
        default: break;   // (decoys stand; beams fade; the potato rides a hand: StepRules)
        }
    }
    things.erase(std::remove_if(things.begin(), things.end(), [](const Thing& th) { return !th.alive; }), things.end());
}

// ---------------------------------------------------------------- gear
void World::StepGear(Stick& k) {
    const float dt = STEP;
    bool held = k.in.gear, press = held && !k.gearWas;
    k.gearWas = held;
    if (k.alive && k.gear < 0 && press && k.trinket == TK_PACK_RAT && k.carry >= 0 && k.carry < (int)items.size() && items[k.carry].alive) { int c = k.carry; k.carry = k.weapon; k.weapon = c; k.fireCool = 0.2f; Emit(EV_PICKUP, k.pt[J_HAND_R].p, k.id, -1, (float)items[c].weapon); }   // (Pack Rat: the gear button swaps)
    if (!k.alive || k.gear < 0) { k.hookOn = false; return; }
    if (k.grounded && k.gear == GR_JETPACK && !held) k.gearFuel = std::min(3.0f, k.gearFuel + dt * 1.5f);
    if (k.st == S_RAGDOLL || k.frozenT > 0 || k.netT > 0) { k.hookOn = false; return; }
    Vector2 aim = Vector2Length(k.in.aim) > 0.1f ? Vector2Normalize(k.in.aim) : Vector2{(float)k.face, 0};
    switch (k.gear) {
    case GR_HOOK:
        if (press) {   // (a line at anything within 10 m)
            Vector2 from = k.pt[J_HAND_R].p;
            for (float s = 0.2f; s < 10; s += 0.1f) { Vector2 p = Vector2Add(from, Vector2Scale(aim, s)); if (stage.Solid((int)floorf(p.x / TILE), (int)floorf(p.y / TILE))) { k.hook = p; k.hookOn = true; Emit(EV_GEAR, p, k.id, -1, GR_HOOK); break; } }
        }
        if (!held) k.hookOn = false;
        if (k.hookOn) {
            Vector2 d = Vector2Subtract(k.hook, Vector2Add(k.pos, {0, 1.2f})); float L = Vector2Length(d);
            if (L < 0.7f) k.vel = Vector2Scale(k.vel, 0.8f);
            else { k.vel = Vector2Lerp(k.vel, Vector2Scale(d, 14 / L), 0.2f); k.st = S_AIR; k.grounded = false; k.knockT = std::max(k.knockT, STEP * 2); }
        }
        break;
    case GR_SHIELD: if (held) k.blockT = std::max(k.blockT, STEP * 2); break;
    case GR_JETPACK:
        if (held && k.gearFuel > 0) {
            k.gearFuel -= dt; k.vel.y = std::min(k.vel.y + 62 * dt, 8.0f); k.st = S_AIR; k.grounded = false; k.fallTop = k.pos.y;
            for (auto& o : sticks) if (o.id != k.id && o.alive && o.present && fabsf(o.pos.x - k.pos.x) < 0.5f && o.pos.y + o.height < k.pos.y && k.pos.y - (o.pos.y + o.height) < 1.5f) Burn(o, 0.6f, k.id);
        }
        break;
    case GR_PARACHUTE: if (held && !k.grounded && k.vel.y < -3) { k.vel.y = -3; k.fallTop = k.pos.y; } break;
    case GR_DECOY: if (press && k.gearCool <= 0) { int d = AddThing(TH_DECOY, k.pos, {}, 15, k.id); things[d].a = (float)k.face; k.gear = -1; Emit(EV_GEAR, k.pos, k.id, -1, GR_DECOY); } break;
    case GR_SPRING: if (press && k.grounded) { AddThing(TH_SPRING, k.pos, {}, 30, k.id); k.gear = -1; Emit(EV_GEAR, k.pos, k.id, -1, GR_SPRING); } break;
    case GR_MIRROR:   // (a plate set down in front of you: it sends bullets and the laser back)
        if (press) { int m = AddThing(TH_MIRROR, Vector2Add(k.pos, {(float)k.face * 0.7f, 0}), {}, 25, k.id); things[m].q = {(float)-k.face, 0}; k.gear = -1; Emit(EV_GEAR, k.pos, k.id, -1, GR_MIRROR); }
        break;
    case GR_BALLOON:   // (tied to you: you float up until it's popped, or it runs out)
        if (press && k.balloonT <= 0) { k.balloonT = 6; k.gear = -1; Emit(EV_GEAR, k.pos, k.id, -1, GR_BALLOON); }
        break;
    case GR_FISHBOWL: break;   // (worn: you breathe and shoot underwater, and your head takes 10 less)
    case GR_ROPE:
        if (press) {   // (a rope at the feet, out in front: a bridge over a gap, a tripwire on a floor)
            int x = (int)floorf(k.pos.x / TILE) + k.face, y = (int)floorf((k.pos.y + 0.05f) / TILE) - (k.grounded ? 0 : 1);
            if (k.grounded && stage.At(x, y - 1) == T_EMPTY) y--;   // (over a gap: flush with the floor, a bridge)
            int laid = 0; for (int i = 0; i < 7; i++) { int xx = x + i * k.face; if (stage.At(xx, y) != T_EMPTY) break; stage.Set(xx, y, T_ROPE); laid++; }
            if (laid) { k.gear = -1; Emit(EV_GEAR, k.pos, k.id, -1, GR_ROPE); }
        }
        break;
    }
}

} // namespace sf
