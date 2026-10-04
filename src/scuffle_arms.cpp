// Scuffle stage 2: the arms (doc pp. 4-9). Headless. Crates fall from the sky on parachutes (the first at 3 s, then one
// every 5 s); a crate opens on touch into a weapon; a stick holds one weapon (picking up a second drops the first, a
// duck over a loose one swaps); guns fire bullets with real knockback and recoil (headshots, pellets, pierce, bounces,
// a harpoon that pins, grenades and rockets that explode); melee weapons swing; an empty weapon is thrown (10). The
// block: a punch (or a cutlass swing) thrown in the 0.15 s before a bullet lands deflects it along the punch; the frying
// pan blocks everything that hits it. The wall: at 45 s the stage starts to kill (here, the bulkheads flood from the
// bottom: half the stage by 60 s, all of it by 70 s).
#include "scuffle.h"
#include "json.h"
#include "redtide.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace sf {

constexpr int CURRENT_STAGE = 6;   // (weapons whose "stage" is later stay out of the crates until then: all 48 are in from stage 6)
struct ArmsData { std::vector<WeaponDef> w; ArmsTuning t; float kindW[AR_COUNT][3] = {}; std::vector<std::string> only[AR_COUNT]; };
static const ArmsData& AD() {
    static ArmsData d = [] {
        ArmsData d; Json j = LoadJsonFile(rt::DataDir() + "/../scuffle/scuffle_weapons.json");
        for (const Json& e : j["weapons"].a) {
            WeaponDef x; x.key = e["key"].Str0(); x.name = e["name"].Str0(x.key); x.kind = e["kind"].Str0("gun"); x.special = e["special"].Str0(); x.wrong = e["wrong"].Str0();
            x.stage = e["stage"].I(2); x.ammo = e["ammo"].I(0); x.pellets = e["pellets"].I(1); x.pierce = e["pierce"].I(1); x.bounce = e["bounce"].I(0); x.count = e["count"].I(0);
            x.dmg = e["dmg"].F(0); x.rate = e["rate"].F(1); x.knock = e["knock"].F(0); x.recoil = e["recoil"].F(0); x.speed = e["speed"].F(0); x.spread = e["spread"].F(0); x.head = e["head"].F(1);
            x.gravity = e["gravity"].F(0); x.area = e["area"].F(0); x.areaDmg = e["area_dmg"].F(0); x.fuse = e["fuse"].F(0); x.swing = e["swing"].F(0.3f); x.reach = e["reach"].F(0.8f);
            x.throwDmg = e["throw"].F(0); x.range = e["range"].F(0); x.spinup = e["spinup"].F(0); x.scope = e["scope"].F(0); x.support = e["support"].Bool0(false);
            x.hold = e["hold"].Bool0(false); x.twin = e["twin"].Bool0(false); x.pin = e["pin"].Bool0(false); x.deflect = e["deflect"].Bool0(false); x.blocks = e["blocks"].Bool0(false);
            d.w.push_back(x);
        }
        static const char* AR[AR_COUNT] = {"classic", "melee", "chaos", "snakes", "random"}; static const char* K[3] = {"gun", "melee", "thrown"};
        for (int a = 0; a < AR_COUNT; a++) { for (int k = 0; k < 3; k++) d.kindW[a][k] = j["arsenals"][AR[a]][K[k]].F(a == AR_RANDOM ? 1 : 0); for (const Json& o : j["arsenals"][AR[a]]["only"].a) d.only[a].push_back(o.Str0()); }
        const Json& c = j["crates"]; d.t.crateFirst = c["first"].F(3); d.t.crateEvery = c["every"].F(5); d.t.chuteFall = c["parachute_fall"].F(2); d.t.crush = c["crush"].F(30); d.t.crateThrown = c["thrown_dmg"].F(20); d.t.gearChance = c["gear_chance"].F(0.1f); d.t.trapChance = c["trap_chance"].F(0.033f);
        d.t.emptyThrow = j["empty_throw_dmg"].F(10); d.t.blockWindow = j["block_window"].F(0.15f);
        const Json& w = j["wall"]; d.t.wallStart = w["start"].F(45); d.t.wallCenter = w["center"].F(60); d.t.wallAll = w["all"].F(70); d.t.finaleWall = w["finale_start"].F(30);
        return d;
    }();
    return d;
}
const std::vector<WeaponDef>& Weapons() { return AD().w; }
const ArmsTuning& Arms() { return AD().t; }
int WeaponIndex(const std::string& key) { const auto& w = Weapons(); for (int i = 0; i < (int)w.size(); i++) if (w[i].key == key) return i; return -1; }
const char* ArsenalName(int a) { static const char* N[AR_COUNT] = {"Classic", "Melee only", "Chaos", "Snakes", "Random"}; return N[std::clamp(a, 0, AR_COUNT - 1)]; }
static const WeaponDef& Def(int i) { static WeaponDef none; return i >= 0 && i < (int)Weapons().size() ? Weapons()[i] : none; }
static int KindIdx(const std::string& k) { return k == "gun" ? 0 : k == "melee" ? 1 : 2; }

int World::RollWeapon() {
    const auto& W = Weapons(); const ArmsData& d = AD();
    float tot = 0; std::vector<float> wt(W.size(), 0);
    for (int i = 0; i < (int)W.size(); i++) {
        if (W[i].stage > CURRENT_STAGE) continue;
        if (Mut(MU_PACIFIST) && W[i].kind == "gun") continue;   // (Pacifist: guns don't drop)
        float x = d.kindW[std::clamp(arsenal, 0, AR_COUNT - 1)][KindIdx(W[i].kind)];
        if (!d.only[arsenal].empty()) x = std::find(d.only[arsenal].begin(), d.only[arsenal].end(), W[i].key) != d.only[arsenal].end() ? 1.0f : 0.0f;
        wt[i] = x; tot += x;
    }
    if (tot <= 0) { for (int i = 0; i < (int)W.size(); i++) if (W[i].stage <= CURRENT_STAGE && !(Mut(MU_PACIFIST) && W[i].kind == "gun")) { wt[i] = 1; tot += 1; } }
    float u = Rand() * tot;
    for (int i = 0; i < (int)W.size(); i++) { u -= wt[i]; if (u <= 0 && wt[i] > 0) return i; }
    return 0;
}

// ---------------------------------------------------------------- crates and loose weapons
int World::DropCrate(float x) {
    Item c; c.crate = true; c.chute = true; c.a.p = c.a.q = {x, stage.Height() + 0.8f}; c.a.r = 0.28f;
    items.push_back(c); crates++;
    Emit(EV_CHUTE, c.a.p);
    return (int)items.size() - 1;
}
int World::SpawnWeapon(int weapon, Vector2 at, Vector2 vel) {
    Item it; it.weapon = weapon; it.ammo = Def(weapon).ammo; it.count = Def(weapon).count;
    it.a.p = at; it.b.p = Vector2Add(at, {0.55f, 0}); it.a.r = it.b.r = 0.07f;
    it.a.q = Vector2Subtract(it.a.p, Vector2Scale(vel, STEP)); it.b.q = Vector2Subtract(it.b.p, Vector2Scale(vel, STEP));
    items.push_back(it);
    return (int)items.size() - 1;
}
void World::Pickup(Stick& k, int i) {
    if (k.trinket == TK_PACK_RAT && k.weapon >= 0 && k.carry < 0 && k.weapon != i) { items[i].holder = k.id; items[i].thrownT = 0; k.carry = i; Emit(EV_PICKUP, k.pt[J_HAND_R].p, k.id, -1, (float)items[i].weapon); return; }   // (Pack Rat: the second weapon on the back)
    if (k.weapon >= 0) DropWeapon(k, {0, 2}, false);
    const WeaponDef& wd = Def(items[i].weapon);
    if (k.trinket == TK_QUICK_DRAW && wd.kind == "gun" && items[i].ammo == wd.ammo && items[i].count == 0) { items[i].ammo = std::max(1, (int)ceilf(wd.ammo * 0.8f)); items[i].count = -1; }   // (Quick Draw: ammo -20%; count -1 marks the first shot)
    items[i].holder = k.id; items[i].thrownT = 0; k.weapon = i; k.fireCool = k.trinket == TK_QUICK_DRAW ? 0.0f : 0.15f; k.spin = 0;
    Emit(EV_PICKUP, k.pt[J_HAND_R].p, k.id, -1, (float)items[i].weapon);
}
void World::DropWeapon(Stick& k, Vector2 vel, bool thrown) {
    if (k.weapon < 0 || k.weapon >= (int)items.size()) { k.weapon = -1; return; }
    Item& it = items[k.weapon];
    it.holder = -1;
    it.a.q = Vector2Subtract(it.a.p, Vector2Scale(vel, STEP)); it.b.q = Vector2Subtract(it.b.p, Vector2Scale(vel, STEP));
    if (thrown) { it.thrownT = 1.2f; it.thrownBy = k.id; }
    k.weapon = -1;
}
void World::StepCrates() {
    if (t < nextCrate) return;
    nextCrate += Arms().crateEvery;
    // a random column with something to land on (the stage's crate zones, until the editor marks them)
    if (luckyFor >= 0 && crates == 0 && luckyFor < (int)sticks.size() && sticks[luckyFor].alive) {   // (Lucky Crate: the first one comes straight down on you)
        int c = DropCrate(sticks[luckyFor].pos.x); items[c].chute = false; items[c].a.p.y = items[c].a.q.y = std::min(stage.Height() + 0.8f, sticks[luckyFor].pos.y + 7); return;
    }
    for (int tries = 0; tries < 20; tries++) {
        int x = 1 + (int)(Rand() * (stage.w - 2));
        bool floor = false; for (int y = 0; y < stage.h; y++) floor |= stage.Solid(x, y);
        if (floor && !stage.Solid(x, stage.h - 1)) { DropCrate((x + 0.5f) * TILE); return; }
    }
}
static bool Touching(const Stick& k, Vector2 p, float r) { return p.x > k.pos.x - k.halfW - r && p.x < k.pos.x + k.halfW + r && p.y > k.pos.y - r && p.y < k.pos.y + k.height + r; }
static void CollideItemTiles(const World& w, Particle& a) {
    int x0 = (int)floorf((a.p.x - a.r) / TILE), x1 = (int)floorf((a.p.x + a.r) / TILE), y0 = (int)floorf((a.p.y - a.r) / TILE), y1 = (int)floorf((a.p.y + a.r) / TILE);
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        if (!w.stage.Solid(x, y)) continue;
        Vector2 c{std::clamp(a.p.x, x * TILE, (x + 1) * TILE), std::clamp(a.p.y, y * TILE, (y + 1) * TILE)};
        Vector2 d = Vector2Subtract(a.p, c); float L = Vector2Length(d);
        if (L >= a.r) continue;
        Vector2 n = L > 1e-5f ? Vector2Scale(d, 1 / L) : Vector2{0, 1};
        a.p = Vector2Add(a.p, Vector2Scale(n, a.r - L));
        Vector2 v = Vector2Subtract(a.p, a.q); float vn = Vector2DotProduct(v, n); Vector2 vt = Vector2Subtract(v, Vector2Scale(n, vn));
        a.q = Vector2Add(a.q, Vector2Scale(vt, 0.35f)); if (vn < 0) a.q = Vector2Add(a.q, Vector2Scale(n, vn * 1.3f));
    }
}
void World::StepItems() {
    items.reserve(items.size() + 64);   // (a crate opening adds a weapon mid-loop: no reallocation under the references below)
    for (int i = 0, n0 = (int)items.size(); i < n0; i++) {
        Item& it = items[i];
        if (!it.alive) continue;
        it.age += STEP; it.thrownT = std::max(0.0f, it.thrownT - STEP);
        if (it.holder >= 0) {   // (in a hand: the grip at the hand, the muzzle along the aim)
            const Stick& k = sticks[it.holder];
            Vector2 aim = Vector2Length(k.in.aim) > 0.1f ? Vector2Normalize(k.in.aim) : Vector2{(float)k.face, 0};
            it.a.p = it.a.q = k.pt[J_HAND_R].p; it.b.p = it.b.q = Vector2Add(it.a.p, Vector2Scale(aim, 0.55f));
            continue;
        }
        // the physics: Verlet, a parachute's slow fall, the tiles; a weapon is two particles on a bone
        for (Particle* p : {&it.a, &it.b}) {
            if (it.crate && p == &it.b) continue;
            Vector2 v = Vector2Scale(Vector2Subtract(p->p, p->q), 0.995f);
            if (it.crate && it.chute) v.y = std::max(v.y, -Arms().chuteFall * STEP);
            p->q = p->p; p->p = Vector2Add(p->p, Vector2Add(v, {0, -gravity * STEP * STEP}));
        }
        for (int iter = 0; iter < 2; iter++) {
            if (!it.crate) { Vector2 d = Vector2Subtract(it.b.p, it.a.p); float L = Vector2Length(d); if (L > 1e-5f) { Vector2 c = Vector2Scale(d, (L - 0.55f) / L * 0.5f); it.a.p = Vector2Add(it.a.p, c); it.b.p = Vector2Subtract(it.b.p, c); } }
            CollideItemTiles(*this, it.a); if (!it.crate) CollideItemTiles(*this, it.b);
        }
        Vector2 vel = Vector2Scale(Vector2Subtract(it.a.p, it.a.q), 1 / STEP);
        if (it.crate) {
            bool resting = fabsf(vel.y) < 0.3f && it.age > 0.3f;
            if (resting && it.chute) { it.chute = false; }
            for (auto& k : sticks) {
                if (!k.present || !k.alive) continue;
                if (k.shark || EggHolder(k.id)) continue;   // (the Shark takes no crates; the egg's holder only punches)
                // falling hard (no parachute): it crushes the stick it lands on
                if (!it.chute && vel.y < -5 && Vector2Distance(it.a.p, k.pt[J_HEAD].p) < 0.45f) { Hit(k, -1, Arms().crush, {0, -1}, 3, true, "a crate"); Emit(EV_CRUSH, it.a.p, k.id); }
                // a crate opens on touch: the weapon goes into an empty hand, or pops out
                if (it.alive && Touching(k, it.a.p, it.a.r)) {
                    it.alive = false;
                    float roll = Rand();
                    if (roll < Arms().gearChance) {   // (one crate in ten is gear: it goes to the gear slot)
                        k.gear = std::min(GR_COUNT - 1, (int)(Rand() * GR_COUNT)); k.gearFuel = 3; k.gearCool = 0.3f;
                        Emit(EV_GEAR, it.a.p, k.id, -1, (float)k.gear);
                        break;
                    }
                    if (roll < Arms().gearChance + Arms().trapChance) {   // (one in thirty is a trap: a snake, bees, a flashbang)
                        int which = std::min(2, (int)(Rand() * 3));
                        if (which == 0) Snakes(it.a.p, 1, -1); else if (which == 1) { AddThing(TH_SWARM, it.a.p, {}, 6, -1); Emit(EV_BEES, it.a.p); } else { flashT = 1.5f; Emit(EV_FLASH, it.a.p); }
                        Emit(EV_CRATE_OPEN, it.a.p, k.id, -1, -1);
                        break;
                    }
                    int w = SpawnWeapon(RollWeapon(), it.a.p, {0, 3});
                    Emit(EV_CRATE_OPEN, it.a.p, k.id, -1, (float)items[w].weapon);
                    if (k.weapon < 0 && k.grabbing < 0) Pickup(k, w);
                    break;
                }
            }
            if (it.alive && it.shot && resting) { it.alive = false; int w = SpawnWeapon(RollWeapon(), it.a.p, {0, 3}); Emit(EV_CRATE_OPEN, it.a.p, -1, -1, (float)items[w].weapon); }   // (shot: it opens where it lands)
            if (it.a.p.y < -6) it.alive = false;
            continue;
        }
        // a thrown weapon is a projectile (10; the axe spins for 60)
        if (it.thrownT > 0 && Vector2Length(vel) > 5) {
            for (auto& k : sticks) {
                if (!k.present || k.id == it.thrownBy || !Touching(k, it.b.p, 0.1f)) continue;
                const WeaponDef& d = Def(it.weapon);
                Hit(k, it.thrownBy, d.throwDmg > 0 ? d.throwDmg : Arms().emptyThrow, Vector2Normalize(vel), 5, d.throwDmg > 0, d.throwDmg > 0 ? "a thrown axe" : "a thrown gun");
                it.thrownT = 0; it.a.q = it.a.p; it.b.q = it.b.p; break;
            }
        }
        // a loose weapon is picked up by an empty hand (or swapped for by a duck)
        if (it.thrownT <= 0 && (it.ammo > 0 || Def(it.weapon).kind != "gun")) for (auto& k : sticks) {
            if (!k.present || !k.alive || k.st == S_RAGDOLL || k.grabbing >= 0 || k.shark || EggHolder(k.id) || !Touching(k, it.a.p, 0.05f)) continue;
            if (k.weapon < 0 || (k.st == S_DUCK && k.in.moveY < -0.5f && k.fireCool <= 0 && k.weapon != i)) { Pickup(k, i); break; }
        }
        if (it.a.p.y < -6) it.alive = false;
    }
}

// ---------------------------------------------------------------- firing (and swinging)
void World::Fire(Stick& k) {
    Item& it = items[k.weapon]; const WeaponDef& d = Def(it.weapon);
    Input& in = k.in;
    bool press = in.fire && !k.fireWas;
    Vector2 aim = Vector2Length(in.aim) > 0.1f ? Vector2Normalize(in.aim) : Vector2{(float)k.face, 0};
    if (fabsf(aim.x) > 0.2f) k.face = aim.x > 0 ? 1 : -1;
    k.fireCool = std::max(0.0f, k.fireCool - STEP);
    // duck and click: throw whatever you hold (the axe spins for 60; anything else is 10)
    if (press && k.st == S_DUCK && d.kind != "thrown") { Emit(EV_THROW, it.b.p, k.id); DropWeapon(k, Vector2Add(Vector2Scale(aim, 15), {0, 3}), true); k.fireWas = in.fire; return; }
    if (d.kind == "thrown") {   // (charges, bombs, traps, peels, hives, jars, bottles: one a click; the last one leaves the hand)
        if (press && it.count > 0 && k.fireCool <= 0) {
            Bullet b; b.p = it.b.p; b.v = Vector2Add(Vector2Scale(aim, 13), {0, 3}); b.owner = k.id; b.weapon = it.weapon; b.dmg = d.dmg; b.knock = 3; b.grav = gravity;
            b.bounces = d.bounce; b.life = 4; b.pierce = 1;
            bullets.push_back(b); it.count--; k.fireCool = 0.35f;
            Emit(EV_THROW, it.b.p, k.id, -1, (float)it.weapon);
            if (it.count <= 0) { it.alive = false; k.weapon = -1; }
        }
        k.fireWas = in.fire; return;
    }
    if (d.kind == "melee") {
        k.swingT = std::max(0.0f, k.swingT - STEP);
        if (d.blocks) k.blockT = 0.05f;                                   // (the frying pan: a shield while held)
        if (press && k.swingT <= 0) {
            k.swingT = d.swing; k.swingHit.clear(); if (d.deflect) k.blockT = std::max(k.blockT, Arms().blockWindow); Emit(EV_SWING, it.b.p, k.id, -1, (float)it.weapon);
            if (k.grounded && fabsf(aim.y) < 0.7f) k.vel.x += (aim.x >= 0 ? 1.0f : -1.0f) * 5;   // (a swing steps into it)
            if (d.key == "trident" && k.grounded && aim.y < -0.6f) { k.vel.y = 15; k.vel.x += k.face * 3; k.st = S_AIR; k.grounded = false; Emit(EV_JUMP, k.pos, k.id); }   // (a pole vault)
        }
        float u = k.swingT > 0 ? 1 - k.swingT / d.swing : 2;
        float reach = d.reach * (d.key == "poolcue" && it.count > 0 ? 0.55f : 1.0f);   // (a broken cue is half as long)
        if (d.key == "sledge" && u > 0.5f && u < 0.5f + STEP / std::max(0.05f, d.swing) * 1.5f) {   // (the sledgehammer breaks any tile it comes down on)
            Vector2 hp = Vector2Add(k.pt[J_NECK].p, Vector2Scale(aim, 0.3f + reach));
            int tx = (int)floorf(hp.x / TILE), ty = (int)floorf(hp.y / TILE);
            for (int dy = 0; dy >= -1; dy--) if (stage.Solid(tx, ty + dy) && ty + dy > 0) { stage.Set(tx, ty + dy, T_EMPTY); Emit(EV_HIT, {(tx + 0.5f) * TILE, (ty + dy + 0.5f) * TILE}, -1, k.id, 0); break; }
        }
        if (u > 0.3f && u < 0.8f) {
            for (auto& o : sticks) {
                if (o.id == k.id || !o.present || std::find(k.swingHit.begin(), k.swingHit.end(), o.id) != k.swingHit.end()) continue;
                bool hit = false;
                for (int s = 1; s <= 3 && !hit; s++) { Vector2 p = Vector2Add(k.pt[J_NECK].p, Vector2Scale(aim, 0.3f + reach * s / 3)); for (int j : {J_HEAD, J_NECK, J_PELVIS}) hit |= Vector2Distance(o.pt[j].p, p) < o.pt[j].r + 0.14f; }
                if (!hit) continue;
                k.swingHit.push_back(o.id);
                Vector2 dir = Vector2Normalize(Vector2Add(aim, {0, 0.3f}));
                if (d.key == "whip") dir = Vector2Normalize(Vector2Add(Vector2Subtract(k.pt[J_PELVIS].p, o.pt[J_PELVIS].p), {0, 0.6f}));   // (the whip pulls them in)
                Hit(o, k.id, d.dmg, dir, d.key == "whip" ? 8.0f : d.knock, d.knock >= 8 || d.key == "whip", d.name.c_str());
                o.fireCool = std::max(o.fireCool, 0.35f); o.punchCool = std::max(o.punchCool, 0.35f); o.aimT = 0;   // (hitstun: a blow up close puts their next shot or swing back)
                if (d.special == "chain") { Zap(it.b.p, o, 0, k.id, it.weapon); o.fireCool = std::max(o.fireCool, 0.8f); o.punchCool = std::max(o.punchCool, 0.8f); o.knockT = std::max(o.knockT, 0.4f); }   // (the tesla gaff sparks on, and the shock holds them a moment)
                if (d.key == "poolcue" && it.count == 0) { it.count = 1; int h = SpawnWeapon(it.weapon, it.b.p, {aim.x * 3, 4}); items[h].count = 1; Emit(EV_HIT, it.b.p, -1, k.id, 0); }   // (the cue breaks in two: two weapons)
            }
        }
        k.fireWas = in.fire; return;
    }
    // guns: an empty gun is thrown
    if (it.ammo <= 0) { if (press) { Emit(EV_EMPTY, it.b.p, k.id); DropWeapon(k, Vector2Add(Vector2Scale(aim, 14), {0, 2}), true); } k.fireWas = in.fire; return; }
    bool trigger = d.hold ? in.fire : press;
    // underwater only the harpoon and the tesla gaff work (doc p. 11)
    if ((InLiquid(it.b.p) || InLiquid(k.pt[J_HEAD].p)) && d.key != "harpoon" && d.key != "speargun" && d.key.find("tesla") == std::string::npos && k.trinket != TK_BIG_LUNGS && k.gear != GR_FISHBOWL) trigger = false;
    if (d.spinup > 0) { k.spin = in.fire ? std::min(d.spinup, k.spin + STEP) : std::max(0.0f, k.spin - STEP * 2); if (k.spin < d.spinup) trigger = false; }
    if (d.scope > 0) {   // (the sniper: the press starts the aim, the shot leaves a moment later where you aim then)
        if (k.aimT > 0) { k.aimT -= STEP; trigger = k.aimT <= 0; if (trigger) k.aimT = 0; }
        else if (trigger && k.fireCool <= 0) { k.aimT = d.scope; trigger = false; }
        else trigger = false;
    }
    if (trigger && k.fireCool <= 0) {
        k.fireCool = 1 / std::max(0.1f, d.rate); if (!Mut(MU_INFINITE_AMMO)) it.ammo--;
        bool steady = it.count == -1; if (steady) it.count = 0;   // (Quick Draw: the first shot after a pick-up has no spread)
        if (d.speed <= 0 && !d.special.empty()) SpecialFire(k, d, it, aim);   // (the beams: the tesla, the gravity gun, the laser)
        else for (int n = 0; n < std::max(1, d.pellets); n++) {
            float ang = atan2f(aim.y, aim.x) + (steady ? 0.0f : (Rand() - 0.5f) * d.spread * DEG2RAD);
            Bullet b; b.p = it.b.p; if (d.twin && (it.ammo % 2)) b.p = Vector2Add(b.p, {0, -0.12f});   // (twin pistols: one barrel, then the other)
            b.v = {cosf(ang) * d.speed, sinf(ang) * d.speed}; b.owner = k.id; b.weapon = it.weapon; b.dmg = d.dmg; b.knock = d.knock; b.grav = d.gravity * gravity;
            b.pierce = std::max(1, d.pierce); b.bounces = d.bounce; b.explode = d.area > 0; b.area = d.area; b.areaDmg = d.areaDmg; b.fuse = d.fuse;
            b.life = d.range > 0 ? d.range / std::max(1.0f, d.speed) : 3.0f;
            if (d.special == "blackhole") b.life = 0.9f;   // (it opens where it is after 0.9 s, or on what it hits)
            if (d.special == "bees") b.life = 0.7f;
            if (d.special == "boomerang") { b.life = 2.5f; b.pierce = 3; }
            if (Mut(MU_MOON_SHOT)) { b.v = Vector2Scale(b.v, 0.35f); b.life /= 0.35f; }   // (slow and visible)
            if (Mut(MU_RICOCHET)) b.bounces = std::max(b.bounces, 1);
            bullets.push_back(b);
        }
        // the recoil moves the body (a minigun pushes you back; aim it down and it's a jetpack)
        k.vel = Vector2Subtract(k.vel, Vector2Scale(aim, d.recoil * (d.hold ? 0.35f : 1.0f))); if (d.recoil >= 2) k.knockT = std::max(k.knockT, 0.08f);   // (the feet skid under recoil)
        if (d.recoil >= 6) for (auto& a : k.pt) a.q = Vector2Add(a.q, Vector2Scale(aim, d.recoil * STEP * 0.4f));
        Emit(EV_SHOT, it.b.p, k.id, -1, (float)it.weapon);
    }
    k.fireWas = in.fire;
}

// ---------------------------------------------------------------- bullets
void World::Explode(Vector2 at, float radius, float dmg, float knock, int owner, int weapon) {
    Emit(EV_EXPLODE, at, -1, owner, radius);
    ShotAt(at, radius * 0.6f);
    for (auto& th : things) if (th.alive && (th.kind == TH_SNAKE || th.kind == TH_SWARM || th.kind == TH_FISH || th.kind == TH_PEEL || th.kind == TH_TRAP) && Vector2Distance(th.p, at) < radius) th.alive = false;   // (a blast clears snakes, bees, fish and what lies on the floor)
    for (auto& k : sticks) {
        if (!k.present) continue;
        float d = Vector2Distance(at, k.pt[J_PELVIS].p);
        if (d > radius) continue;
        float f = 1 - d / radius * 0.6f;
        Vector2 dir = Vector2Normalize(Vector2Add(Vector2Subtract(k.pt[J_PELVIS].p, at), {0, 0.4f}));
        Hit(k, owner, dmg * f * (k.id == owner ? 0.25f : 1.0f), dir, knock * f, true, weapon >= 0 ? Def(weapon).name.c_str() : "an explosion");   // (your own blast hurts you less)
    }
    for (auto& it : items) if (it.alive && it.holder < 0 && Vector2Distance(it.a.p, at) < radius) { Vector2 dir = Vector2Normalize(Vector2Subtract(it.a.p, at)); it.a.q = Vector2Subtract(it.a.q, Vector2Scale(dir, knock * STEP)); it.b.q = Vector2Subtract(it.b.q, Vector2Scale(dir, knock * STEP)); if (it.crate) it.chute = false; }
}
static bool Blocking(const Stick& k) { return k.alive && k.st != S_RAGDOLL && k.blockT > 0; }
void World::StepBullets() {
    bullets.reserve(bullets.size() + 256);   // (an impact can add bullets mid-loop: shrapnel; no reallocation under the reference)
    for (auto& b : bullets) {
        if (!b.alive) continue;
        b.age += STEP; b.life -= STEP;
        if (b.fuse > 0 && b.age >= b.fuse) { Explode(b.p, b.area, b.areaDmg, b.knock, b.owner, b.weapon); b.alive = false; continue; }
        if (b.life <= 0) { if (b.explode) Explode(b.p, b.area, b.areaDmg, b.knock, b.owner, b.weapon); else SpecialHit(b, b.p, nullptr); b.alive = false; continue; }
        b.v.y -= b.grav * STEP;
        if (b.hazard == -1 && b.weapon >= 0 && Def(b.weapon).special == "boomerang" && b.age > 0.45f && b.owner >= 0 && b.owner < (int)sticks.size()) {   // (it comes back; it hits you if you missed)
            Stick& o = sticks[b.owner]; Vector2 d = Vector2Subtract(o.pt[J_NECK].p, b.p); float L = Vector2Length(d);
            if (L < 0.45f) { if (b.hit.empty() && o.alive) Hit(o, b.owner, 6, Vector2Normalize(b.v), 3, false, "your own boomerang"); b.alive = false; continue; }
            if (b.age < 0.47f && !b.hit.empty()) b.hit.clear();   // (on the way back it can hit them again)
            b.v = Vector2Lerp(b.v, Vector2Scale(d, 18 / std::max(0.01f, L)), 0.12f);
        }
        float dist = Vector2Length(b.v) * STEP; int n = std::max(1, (int)ceilf(dist / 0.12f));
        Vector2 step = Vector2Scale(b.v, STEP / n);
        for (int s = 0; s < n && b.alive; s++) {
            Vector2 prev = b.p; b.p = Vector2Add(b.p, step);
            int tx = (int)floorf(b.p.x / TILE), ty = (int)floorf(b.p.y / TILE);
            if (stage.Solid(tx, ty)) {
                if (b.hazard == -1 && ShotAt(b.p, 0.05f)) { b.alive = false; break; }   // (a stalactite, a column, crystal)
                if (b.bounces > 0) {   // a bounce off the face it came through
                    b.bounces--;
                    bool fx = stage.Solid((int)floorf(b.p.x / TILE), (int)floorf(prev.y / TILE)), fy = stage.Solid((int)floorf(prev.x / TILE), (int)floorf(b.p.y / TILE));
                    if (fx || !fy) b.v.x = -b.v.x * 0.7f; if (fy || !fx) b.v.y = -b.v.y * 0.7f;
                    b.p = prev; step = Vector2Scale(b.v, STEP / n);
                    continue;
                }
                if (SpecialHit(b, prev, nullptr)) { b.alive = false; break; }   // (stage 6: a portal, fire on wood, a placed trap, a charge that sticks...)
                if (b.explode && b.fuse <= 0) Explode(prev, b.area, b.areaDmg, b.knock, b.owner, b.weapon);
                else if (b.fuse <= 0) Emit(EV_HIT, prev, -1, b.owner, 0);
                if (b.fuse > 0) { b.v = {0, 0}; b.p = prev; break; }   // (a grenade with no bounces left sits and fizzes)
                b.alive = false; break;
            }
            // crates in the air: a bullet pops the parachute, or marks the crate to open where it lands
            for (auto& it : items) {
                if (!it.alive || !it.crate) continue;
                if (it.chute && Vector2Distance(b.p, Vector2Add(it.a.p, {0, 0.75f})) < 0.4f) { it.chute = false; Emit(EV_CHUTE, it.a.p, -1, b.owner, 1); b.alive = !b.explode; break; }
                if (Vector2Distance(b.p, it.a.p) < it.a.r + 0.05f) { it.shot = true; it.a.q = Vector2Subtract(it.a.q, Vector2Scale(Vector2Normalize(b.v), 2 * STEP)); if (b.explode) Explode(b.p, b.area, b.areaDmg, b.knock, b.owner, b.weapon); b.alive = false; break; }
            }
            if (!b.alive) break;
            // a mirror sends it back
            for (auto& th : things) if (th.alive && th.kind == TH_MIRROR && fabsf(b.p.x - th.p.x) < 0.12f && b.p.y > th.p.y && b.p.y < th.p.y + 1.3f && b.v.x * th.q.x < 0) { b.v.x = -b.v.x; b.p.x = th.p.x + th.q.x * 0.13f; b.owner = th.owner; b.deflected = true; b.hit.clear(); step = Vector2Scale(b.v, STEP / n); Emit(EV_BLOCK, b.p, -1, th.owner); }
            // a stalactite hangs in open air: a shot anywhere on it brings it down
            if (b.hazard == -1) for (auto& pc : stage.pieces) if (pc.kind == PK_STALACTITE && pc.prog <= 0 && !pc.broken && t >= pc.start && b.p.x >= pc.x * TILE && b.p.x < (pc.x + pc.w) * TILE && b.p.y >= pc.y * TILE && b.p.y < (pc.y + pc.h) * TILE) { pc.prog = STEP; b.alive = false; Emit(EV_HIT, b.p, -1, b.owner, 0); break; }
            if (!b.alive) break;
            // sticks: a block deflects it (along the blocker's aim); otherwise it hits (the head is its own body: a headshot)
            for (auto& k : sticks) {
                if (!k.present) continue;
                if (k.id == b.owner && b.age < 0.25f) continue;   // (not the shooter, nor the stick that just sent it back)
                if (b.hazard == -2 && b.age < 0.32f) continue;     // (shrapnel: clear of the crystal first)
                if (std::find(b.hit.begin(), b.hit.end(), k.id) != b.hit.end()) continue;
                bool head = Vector2Distance(b.p, k.pt[J_HEAD].p) < k.pt[J_HEAD].r + 0.04f, body = head;
                if (!body) for (int j : {J_NECK, J_PELVIS, J_ELBOW_L, J_ELBOW_R, J_KNEE_L, J_KNEE_R}) body |= Vector2Distance(b.p, k.pt[j].p) < k.pt[j].r + 0.06f;
                if (!body) { Vector2 a = k.pt[J_NECK].p, c = k.pt[J_PELVIS].p, ac = Vector2Subtract(c, a); float L2 = Vector2LengthSqr(ac); float u = L2 > 0 ? std::clamp(Vector2DotProduct(Vector2Subtract(b.p, a), ac) / L2, 0.0f, 1.0f) : 0; body = Vector2Distance(Vector2Add(a, Vector2Scale(ac, u)), b.p) < 0.12f; }
                bool nearHand = Vector2Distance(b.p, k.pt[J_HAND_R].p) < 0.6f;
                bool frontal = Vector2DotProduct(b.v, k.in.aim) < 0;   // (the punch thrown at the bullet, not away from it)
                // (the frying pan held up stops a bullet dead; a timed swing or punch sends it back)
                bool pan = k.weapon >= 0 && k.weapon < (int)items.size() && Def(items[k.weapon].weapon).blocks && k.swingT <= 0;
                if ((body || nearHand) && Blocking(k) && frontal && k.id != b.owner && pan) { b.alive = false; Emit(EV_BLOCK, b.p, k.id); break; }
                if ((body || nearHand) && Blocking(k) && frontal && k.id != b.owner) {
                    Vector2 aim = Vector2Length(k.in.aim) > 0.1f ? Vector2Normalize(k.in.aim) : Vector2{(float)k.face, 0};
                    b.v = Vector2Scale(aim, Vector2Length(b.v) * 1.05f); b.owner = k.id; b.deflected = true; b.age = 0; b.hit.clear();
                    step = Vector2Scale(b.v, STEP / n);
                    Emit(EV_BLOCK, b.p, k.id); break;
                }
                if (!body) continue;
                if (Def(b.weapon).special == "boomerang" && k.id == b.owner) continue;   // (its owner is met by the return, above)
                if (SpecialHit(b, b.p, &k)) { b.hit.push_back(k.id); b.alive = false; break; }
                b.hit.push_back(k.id);
                if (b.explode) { Explode(b.p, b.area, b.areaDmg, b.knock, b.owner, b.weapon); b.alive = false; break; }
                const WeaponDef& d = Def(b.weapon);
                float dmg = b.dmg * (head && k.trinket != TK_THICK_SKULL ? d.head : 1.0f); if (head && k.gear == GR_FISHBOWL) dmg = std::max(0.0f, dmg - 10);
                std::string cause = (b.deflected ? std::string("a deflected ") : std::string()) + (b.hazard == -2 ? std::string("crystal shrapnel") : b.hazard == PK_CROWD ? std::string("a bottle from the crowd") : b.hazard == PK_DART ? std::string("a dart") : b.hazard == PK_POOL ? std::string("a pool ball") : b.hazard == PK_DRIP ? std::string("acid") : b.hazard == -3 ? std::string("a turret") : d.name);
                Hit(k, b.owner, dmg, Vector2Normalize(b.v), b.knock, b.knock >= 10 || d.pin, cause.c_str());
                if (d.pin && k.alive) k.ragT = std::max(k.ragT, 1.0f);   // (the harpoon pins: a second on the end of the line)
                if (--b.pierce <= 0) { b.alive = false; break; }
            }
        }
        if (b.p.x < -6 || b.p.x > stage.Width() + 6 || b.p.y < -6 || b.p.y > stage.Height() + 12) b.alive = false;
    }
    bullets.erase(std::remove_if(bullets.begin(), bullets.end(), [](const Bullet& b) { return !b.alive; }), bullets.end());
}
// ---------------------------------------------------------------- the wall (doc pp. 4, 9-10): each world closes in its own way
//   the Nautilus: the bulkheads flood from the bottom (it kills); the Cave: the ceiling comes down; the Reef: the tide comes
//   in and Atlantis sinks (water you swim in and drown in); the Void: the abyss widens from its side, taking the floor; the
//   Salon: closing time, the bouncer clears the room from the door
void World::StepWall() {
    const ArmsTuning& a = Arms();
    float start = Mut(MU_SUDDEN_WALL) ? 15 : finale ? a.finaleWall : a.wallStart, mid = start + (a.wallCenter - a.wallStart), all = start + (a.wallAll - a.wallStart);
    if (!wallOn || t < start) { wallY = -10; ceilY = 1e9f; sideX = -10; return; }
    if (wallY < -5 && ceilY > 1e8f && sideX < -5) Emit(EV_WALL, {stage.Width() / 2, 0});
    float H = stage.Height(), W = stage.Width();
    float u = t < mid ? 0.5f * (t - start) / (mid - start) : 0.5f + 0.5f * std::min(1.0f, (t - mid) / (all - mid));
    auto blame = [&](const Stick& k) { return k.lastHitBy >= 0 && t - k.lastHitT < 3 ? k.lastHitBy : -1; };
    switch (stage.world) {
    case WD_CAVE:
        wallY = -9; ceilY = Lerp(H + 1, -1, u);
        for (auto& k : sticks) if (k.alive && k.present && k.pt[J_HEAD].p.y + 0.15f > ceilY) Kill(k, blame(k), "the wall");
        break;
    case WD_REEF: case WD_ATLANTIS:
        wallY = Lerp(0, H + 2, u);   // (InLiquid: below it you swim, and drown in 8 s)
        break;
    case WD_VOID: case WD_SALON: {
        wallY = -9;
        float before = sideX; sideX = Lerp(0, W + 1, u);
        if (stage.world == WD_VOID) {   // (the floor goes with it, column by column)
            int c0 = (int)floorf(std::max(0.0f, before) / TILE), c1 = (int)floorf(sideX / TILE);
            for (int c = c0; c <= c1 && c < stage.w; c++) { int x = wallSide > 0 ? stage.w - 1 - c : c; for (int y = 0; y < stage.h; y++) if (stage.At(x, y) != T_EMPTY) { stage.Set(x, y, T_EMPTY); if (y % 4 == 0) Emit(EV_HIT, {(x + 0.5f) * TILE, (y + 0.5f) * TILE}, -1, -1, 0); } }
        }
        for (auto& k : sticks) {
            if (!k.alive || !k.present) continue;
            float from = wallSide > 0 ? W - k.pt[J_PELVIS].p.x : k.pt[J_PELVIS].p.x;
            if (from >= sideX) continue;
            if (stage.world == WD_SALON) { for (auto& q : k.pt) q.q = Vector2Subtract(q.q, Vector2Scale({(float)-wallSide, 0.6f}, 18 * STEP)); k.st = S_RAGDOLL; }
            Kill(k, blame(k), "the wall");
        }
        break;
    }
    default:
        ceilY = 1e9f;
        wallY = t < mid ? Lerp(0, H * 0.5f, (t - start) / (mid - start)) : Lerp(H * 0.5f, H + 2, std::min(1.0f, (t - mid) / (all - mid)));
        for (auto& k : sticks) if (k.alive && k.present && k.pt[J_PELVIS].p.y < wallY) Kill(k, blame(k), "the wall");
    }
}
bool World::LineOfSight(Vector2 a, Vector2 b) const {
    Vector2 d = Vector2Subtract(b, a); float L = Vector2Length(d); int n = (int)(L / 0.15f) + 1;
    for (int i = 1; i < n; i++) { Vector2 p = Vector2Add(a, Vector2Scale(d, i / (float)n)); if (stage.Solid((int)floorf(p.x / TILE), (int)floorf(p.y / TILE))) return false; }
    return true;
}
void World::StepArms() { StepCrates(); StepItems(); StepBullets(); StepWall(); }

} // namespace sf
