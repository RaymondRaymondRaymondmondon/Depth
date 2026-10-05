// Fowl Play: the bots (shoot, shop, gamble a little, grief a little), the acceptance tests (--fowl-test), the match sim
// (--fowl-sim) and the house-edge check (--fowl-gamble-sim).
#include "fowl.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <string>

namespace fp {

static uint32_t Hash(uint32_t a) { a ^= a >> 16; a *= 0x7feb352d; a ^= a >> 15; a *= 0x846ca68b; a ^= a >> 16; return a; }
static float H01(uint32_t a) { return (Hash(a) & 0xFFFF) / 65535.0f; }
static float AngTo(float from, float to) { float d = to - from; while (d > PI) d -= 2 * PI; while (d < -PI) d += 2 * PI; return d; }

// the shopping list: what a bot would rather hold, best first (fun guns now and then, for the show)
static const char* WANT[] = {"sniper", "battle", "tommy", "lever", "autoshot", "carbine", "pump", "lightning", "long", "twin", "double", "pocket"};
static void PlanIntermission(World& w, Player& p, uint32_t& rng, int skill) {
    p.botPlan.clear(); p.botPlanAt.clear(); p.botPlanned = true;
    auto R = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 16777216.0f; };
    int money = p.money; const ModeDef& md = w.M();
    auto add = [&](int station, uint8_t kind, int a, int b = 0) { fp::Command c; c.kind = kind; c.a = a; c.b = b; p.botPlan.push_back(c); p.botPlanAt.push_back(station); };
    // counters for what's coming (everyone can see it on the board)
    if (skill >= 1) for (const auto& s : p.incoming) { const SlopItem& it = D().sabotage[s.item]; if (!s.countered && it.counterPrice > 0 && money >= it.counterPrice + 40) { add(4, CMD_COUNTER, s.item); money -= it.counterPrice; } }
    // a better gun
    auto owns = [&](int g) { return p.guns[0].def == g || p.guns[1].def == g || p.hook.def == g; };
    int bestHeld = 0; for (const auto& g : p.guns) if (g.Has()) bestHeld = std::max(bestHeld, D().guns[g.def].price);
    if (!md.zapperOnly && !md.mystery) {
        int pick = -1;
        if (R() < (skill == 2 ? 0.12f : 0.25f)) { std::vector<int> fun; for (int i = 0; i < (int)D().guns.size(); i++) if (D().guns[i].Fun() && D().guns[i].price <= money * 0.8f && D().guns[i].price > bestHeld * 0.6f && !owns(i)) { static const char* KILLERS[] = {"crab", "ray", "net", "boomerang", "banana", "lightning", "firework", "gatling", "mega"}; for (const char* k : KILLERS) if (D().guns[i].id == k) fun.push_back(i); } if (!fun.empty()) pick = fun[(int)(R() * fun.size()) % fun.size()]; }
        if (pick < 0) for (const char* id : WANT) { int g = GunIndex(id); if (g >= 0 && !owns(g) && D().guns[g].price <= money && D().guns[g].price > bestHeld) { pick = g; break; } }
        if (pick >= 0) { add(0, CMD_BUY_GUN, pick); money -= D().guns[pick].price; bestHeld = D().guns[pick].price; }
    }
    if (md.mystery && p.mysteryFree) add(1, CMD_MYSTERY, 0);
    // an attachment or two
    const char* atts[] = {"reddot", "extmag", "speedloader", "longbarrel", "hair"};
    for (const char* a : atts) { int ai = AttIndex(a); if (ai < 0) continue; if (money > D().atts[ai].price + 120 && R() < 0.4f + 0.15f * skill) { add(0, CMD_BUY_ATT, ai, p.guns[1].Has() ? 1 : 0); money -= D().atts[ai].price; } }
    // a flutter on the slots (the green bots love them)
    float gamble = skill == 0 ? 0.6f : skill == 1 ? 0.25f : 0.08f;
    if (R() < gamble && money >= 20) { int pulls = 1 + (int)(R() * 3); for (int i = 0; i < pulls; i++) add(2, CMD_SLOT, 10); money -= 10 * pulls; }
    if (R() < gamble * 0.5f && money >= 20) { add(3, CMD_SCRATCH, 1); }
    // a little grief, on the leader, when rich
    int lead = w.Leader();
    if (!md.practice && lead >= 0 && lead != p.id && money > 350 && R() < 0.3f) {
        std::vector<int> c; for (int i = 0; i < (int)D().sabotage.size(); i++) if (w.SabPrice(i, lead) <= money - 150 && D().sabotage[i].id != "glitter") c.push_back(i);
        if (!c.empty() && !(md.teams && w.players[lead].team == p.team)) { int it = c[(int)(R() * c.size()) % c.size()]; add(4, CMD_SABOTAGE, it, lead); }
    }
}

void BotInput(World& w, int me, Input& in, std::vector<Command>& cmds, uint32_t& rng, int skill) {
    Player& p = w.players[me]; Input o; o.yaw = p.yaw; o.pitch = p.pitch;
    auto R = [&]() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 16777216.0f; };
    if (w.phase == PH_INTER) {
        if (!p.botPlanned) PlanIntermission(w, p, rng, skill);
        if (!p.botPlan.empty()) {
            Vector3 s = World::Station(p.botPlanAt[0]); Vector3 at = s; at.z += p.botPlanAt[0] == 2 ? 1.2f : (p.botPlanAt[0] == 0 || p.botPlanAt[0] == 4 ? 0 : 1.5f); if (p.botPlanAt[0] == 0) at.x += 1.6f; if (p.botPlanAt[0] == 4) at.x -= 1.6f;
            if (p.botPlanAt[0] == 2) at.x += (p.id - 2.5f) * 1.2f;   // (a slot each)
            // through the stall gate first
            Vector3 gate = w.StallPos(p.stall); gate.z = -2;
            Vector3 goal = (p.pos.z > -1.8f && fabsf(p.pos.x - gate.x) > 0.3f) ? Vector3{gate.x, 0, -0.8f} : (p.pos.z > -2.4f ? Vector3{gate.x, 0, -3.0f} : at);
            Vector2 d{goal.x - p.pos.x, goal.z - p.pos.z}; float L = Vector2Length(d);
            if (w.NearStation(p) == p.botPlanAt[0] && p.pos.z < -2.4f) { if (p.slotT <= 0) { cmds.push_back(p.botPlan[0]); p.botPlan.erase(p.botPlan.begin()); p.botPlanAt.erase(p.botPlanAt.begin()); } }
            else if (L > 0.1f) { o.moveX = d.x / std::max(1.0f, L); o.moveZ = d.y / std::max(1.0f, L); }
            o.yaw = atan2f(d.x, d.y);
        }
        else if (p.scratchPocket[1] > 0 && p.scratchOpen < 0) { fp::Command c; c.kind = CMD_SCRATCH_OPEN; c.a = 1; cmds.push_back(c); }
        in = o; return;
    }
    if (w.phase != PH_HUNT && w.phase != PH_BONUS) { in = o; return; }
    // the hunt: the best bird by value and how far the gun must turn
    Gun& g = p.G(); if (!g.Has()) { in = o; return; }
    const GunDef& G = D().guns[g.def];
    if (p.G().def == GunIndex("zapper") && p.guns[1 - p.hand].Has() && !p.inBonus) { o.swap = true; in = o; return; }
    if (p.hook.Has() && !p.inBonus && D().guns[p.hook.def].price > D().guns[p.G().def].price && D().guns[p.G().def].id != "zapper") { cmds.push_back(fp::Command{CMD_HOOK_SWAP}); }
    if (p.smudgeT > 0 && skill >= 1) { o.wipe = true; in = o; return; }
    Vector3 eye = p.Eye(); float speed = G.special.empty() || G.special == "akimbo" || G.special == "both" || G.special == "scope" || G.special == "pinned" || G.special == "bipod" || G.special == "chain" || G.special == "cone" || G.special == "spinup" || G.special == "magnet" || G.special == "blower" || G.special == "honker" ? 0 : G.speed;
    int best = -1; float bs = -1e9f;
    for (int i = 0; i < (int)w.birds.size(); i++) {
        const Bird& b = w.birds[i]; if (b.st != BI_FLY) continue; const BirdDef& B = D().birds[b.def];
        if (B.decoy && H01(b.id * 7 + me) > (skill == 0 ? 0.4f : skill == 1 ? 0.15f : 0.0f)) continue;
        if (B.boo && skill >= 1) continue;
        if (G.special == "magnet" && !B.metal) continue;
        Vector3 v = Vector3Subtract(b.p, eye); float dist = Vector3Length(v);
        float ang = fabsf(AngTo(p.yaw, atan2f(v.x, v.z))) + fabsf(asinf(v.y / dist) - p.pitch);
        float val = (B.pays + 4.0f) * (dist > g.Range() ? g.Range() / dist : 1) / (1 + ang * 3) * (b.id == p.botTarget ? 1.6f : 1);
        if (val > bs) { bs = val; best = i; }
    }
    if (G.special == "honker") { bool geese = false; for (const auto& b : w.birds) if (b.st == BI_FLY && D().birds[b.def].id == "goose") geese = true; o.fire = geese && R() < 0.05f; in = o; return; }
    if (best < 0) { o.pitch = p.pitch + (0.25f - p.pitch) * 0.05f; if (g.ammo < g.MagSize() / 2 && g.reloadT <= 0) o.reload = true; in = o; return; }
    Bird& b = w.birds[best];
    if (b.id != p.botTarget) { p.botTarget = b.id; p.botReact = skill == 0 ? 0.42f : skill == 1 ? 0.3f : 0.2f; }
    p.botReact -= STEP;
    // the error drifts slowly (a hand, not a dice roll)
    p.botErrT -= STEP; if (p.botErrT <= 0) { float e = (skill == 0 ? 2.2f : skill == 1 ? 1.2f : 0.6f) * DEG2RAD; p.botErr = {(R() - 0.5f) * 2 * e, (R() - 0.5f) * 2 * e}; p.botErrT = 0.3f + R() * 0.4f; }
    float dist = Vector3Distance(b.p, eye), tf = speed > 0 ? dist / speed : 0.05f;
    Vector3 aim = Vector3Add(b.p, Vector3Scale(b.v, tf * (skill == 0 ? 0.6f : 1.0f)));
    if (speed > 0) aim.y += 0.5f * 3.0f * tf * tf;   // (lobbed projectiles drop)
    if (D().birds[b.def].armor) aim.y -= D().birds[b.def].r * 0.6f;   // (the belly)
    Vector3 v = Vector3Subtract(aim, eye);
    float wy = atan2f(v.x, v.z) + p.botErr.x, wp = asinf(std::clamp(v.y / Vector3Length(v), -1.0f, 1.0f)) + p.botErr.y;
    float turn = (skill == 0 ? 3.0f : skill == 1 ? 4.5f : 7.0f) * STEP * (G.special == "spinup" ? 0.4f : 1.0f);
    float dy = AngTo(p.yaw, wy), dp = wp - p.pitch;
    o.yaw = p.yaw + std::clamp(dy, -turn, turn); o.pitch = p.pitch + std::clamp(dp, -turn, turn);
    float tol = std::max(g.Spread() * DEG2RAD * 0.8f, D().birds[b.def].r / std::max(1.0f, dist) * 0.9f) + (G.pellets > 1 ? 0.02f : 0);
    bool onTarget = fabsf(dy) < tol + 0.01f && fabsf(dp) < tol + 0.01f;
    o.fire = (onTarget && p.botReact <= 0) || (G.special == "spinup" && (onTarget || g.spin > 0.5f) ) || (G.autoFire && onTarget);
    if (!G.autoFire && p.lastFire) o.fire = false;   // (let go between semi-auto shots)
    in = o;
}

// ---------------------------------------------------------------- the tests
static int gFails = 0;
static void Check(bool ok, const char* what, const std::string& d = "") { std::printf("  [%s] %s%s%s\n", ok ? "ok" : "FAIL", what, d.empty() ? "" : ": ", d.c_str()); if (!ok) gFails++; }
static std::string Fm(const char* f, double a, double b = 0, double c = 0) { char s[200]; std::snprintf(s, sizeof s, f, a, b, c); return s; }
// a world in the middle of a hunt with no waves coming (birds placed by hand)
static World Quiet(int players) { World w; w.Init(players, 0, 0, 5); w.BeginHunt(); w.nextWaveT = 1e9f; w.birds.clear(); w.dog.state = 0; return w; }
static Bird& Place(World& w, const char* id, Vector3 at) { Bird& b = w.SpawnBird(BirdIndex(id), at, -1); b.v = {}; b.life = 999; return b; }
static void AimAt(Player& p, Vector3 at) { Vector3 v = Vector3Subtract(at, p.Eye()); p.in.yaw = atan2f(v.x, v.z); p.in.pitch = asinf(v.y / Vector3Length(v)); p.yaw = p.in.yaw; p.pitch = p.in.pitch; }
static void Shoot(World& w, int who, Vector3 at, int frames = 2) { Player& p = w.players[who]; AimAt(p, at); p.in.fire = true; p.lastFire = false; for (int i = 0; i < frames; i++) { w.Step(); for (auto& b : w.birds) if (b.st == BI_FLY) b.v = {}; } p.in.fire = false; w.Step(); }
static void Hold(World& w, Vector3 at, int who, float secs) { for (int i = 0; i < (int)(secs / STEP); i++) { AimAt(w.players[who], at); w.players[who].in.fire = true; w.Step(); } w.players[who].in.fire = false; }

int RunFowlTest() {
    gFails = 0; std::printf("Fowl Play tests\n");
    const Data& d = D();
    int real = 0, fun = 0; for (const auto& g : d.guns) (g.Fun() ? fun : real)++;
    Check(real == 15 && fun == 15, "the arsenal: 15 real types and 15 fun guns", Fm("%.0f + %.0f", real, fun));
    Check(d.birds.size() >= 15 && d.brackets.size() == 5 && d.sabotage.size() == 14 && d.modes.size() == 8 && d.atts.size() >= 14, "birds, the five round brackets, 14 sabotage items, 8 modes, the pegboard");
    // the Zapper: a mallard dies to one shot; credit and pay
    { World w = Quiet(2); Bird& b = Place(w, "mallard", {0, 8, 30}); int id = b.id; Shoot(w, 0, b.p);
      bool dead = false; for (auto& x : w.birds) if (x.id == id) dead = x.st != BI_FLY && x.killer == 0;
      Check(dead && w.players[0].birds == 1 && w.players[0].roundPay == 10, "one Zapper shot drops a mallard; the shooter gets the bird and $10 at the tally"); }
    // kill credit: the killing shot gets the goose
    { World w = Quiet(2); Bird& b = Place(w, "goose", {3, 10, 25}); Vector3 at = b.p; int id = b.id;
      Shoot(w, 0, at); Shoot(w, 0, at); int lg = GunIndex("long"); w.players[1].guns[0] = MakeGun(lg); w.players[1].G().ammo = 6; Shoot(w, 1, at);
      int k = -2; for (auto& x : w.birds) if (x.id == id) k = x.killer;
      Check(k == 1 && w.players[1].birds == 3 && w.players[0].birds == 0, "a goose softened by one player is stolen by another (the killing shot)", Fm("killer %.0f", k)); }
    // the armored duck: shots to the helmet ping off; the belly kills
    { World w = Quiet(1); Bird& b = Place(w, "armored", {0, 9, 20}); Vector3 c = b.p; float r = D().birds[b.def].r; int id = b.id;
      w.players[0].guns[0] = MakeGun(GunIndex("sniper")); Shoot(w, 0, Vector3Add(c, {0, r * 0.7f, 0}));
      bool alive1 = false; for (auto& x : w.birds) if (x.id == id) alive1 = x.st == BI_FLY;
      w.players[0].G().cool = 0; Shoot(w, 0, Vector3Add(c, {0, -r * 0.8f, 0}));
      bool dead = false; for (auto& x : w.birds) if (x.id == id) dead = x.st != BI_FLY;
      Check(alive1 && dead, "the armored duck: the helmet pings, the belly kills"); }
    // decoys cost $10; the dog costs a bird
    { World w = Quiet(1); Bird& b = Place(w, "decoy", {0, 8, 20}); Shoot(w, 0, b.p); Check(w.players[0].roundPay == -10 && w.players[0].birds == 0, "a decoy costs $10 and counts for nothing", Fm("pay %.0f", w.players[0].roundPay));
      w.players[0].birds = 3; w.dog.state = 3; w.dog.p = {0, 1, 9}; w.players[0].G().cool = 0; w.players[0].G().ammo = 6; Shoot(w, 0, {0, 1.5f, 9}); Check(w.players[0].birds == 2 && w.players[0].dogShot, "shooting the dog costs a bird, and it remembers"); }
    // the fun guns, one by one
    auto funTest = [&](const char* gun, const char* bird, Vector3 at, float secs, std::function<bool(World&, int)> ok, const char* what) {
        World w = Quiet(2); Bird& b = Place(w, bird, at); int id = b.id; w.players[0].guns[0] = MakeGun(GunIndex(gun));
        Player& p = w.players[0]; AimAt(p, at); p.in.fire = true; p.lastFire = false;
        for (int i = 0; i < (int)(secs / STEP); i++) { AimAt(p, at); p.in.fire = i < 3 || D().guns[p.G().def].autoFire; w.Step(); for (auto& x : w.birds) if (x.st == BI_FLY && x.clampT <= 0 && x.bubbleT <= 0 && x.breadT <= 0) x.v = {}; }
        Check(ok(w, id), what);
    };
    auto deadBy = [](World& w, int id, int by) { for (auto& x : w.birds) if (x.id == id) return x.st != BI_FLY && x.killer == by; return false; };
    funTest("crab", "goose", {0, 8, 18}, 2.5f, [&](World& w, int id) { return deadBy(w, id, 0); }, "Crab Cannon: the crab clamps on and drags the goose down (its kill)");
    funTest("ray", "swan", {2, 9, 20}, 2.0f, [&](World& w, int id) { return deadBy(w, id, 0); }, "Ray Gun: the beam turns a swan into a roast chicken");
    { World w = Quiet(1); for (int i = 0; i < 4; i++) Place(w, "mallard", {i * 1.2f, 8, 20}); w.players[0].guns[0] = MakeGun(GunIndex("net")); Shoot(w, 0, {1.5f, 8, 20}, 60); int dead = 0; for (auto& b : w.birds) if (b.st != BI_FLY) dead++; Check(dead == 3, "Net Launcher: catches three of four birds together", Fm("%.0f", dead)); }
    funTest("bread", "mallard", {0, 8, 15}, 1.5f, [&](World& w, int id) { for (auto& x : w.birds) if (x.id == id) return x.breadT > 0 && x.breadBy == 0; return false; }, "Bread Gun: the bird eats it and heads for your stall");
    { World w = Quiet(2); Bird& b = Place(w, "goose", {0, 8, 14}); int id = b.id; w.players[0].guns[0] = MakeGun(GunIndex("bubble")); Shoot(w, 0, b.p, 40);
      Vector3 at{}; bool bub = false; for (auto& x : w.birds) if (x.id == id) { bub = x.bubbleT > 0; at = x.p; }
      Shoot(w, 1, at); Check(bub && deadBy(w, id, 1), "Bubble Cannon: the bubble floats a goose up; another player pops it for the kill"); }
    { World w = Quiet(2); Bird& b = Place(w, "goose", {0, 8, 14}); int id = b.id; w.players[0].guns[0] = MakeGun(GunIndex("plunger")); Shoot(w, 0, b.p, 30); bool slow = false; Vector3 at{}; for (auto& x : w.birds) if (x.id == id) { slow = x.plungers == 1 && x.st == BI_FLY; at = x.p; }
      w.players[0].G().cool = 0; Shoot(w, 0, at, 30); Check(slow && deadBy(w, id, 0), "Plunger Rifle: one plunger slows it, the second kills"); }
    { World w = Quiet(1); for (int i = 0; i < 5; i++) Place(w, "teal", {-2.0f + i, 10, 30}); w.players[0].guns[0] = MakeGun(GunIndex("banana")); Shoot(w, 0, {0, 10.5f, 30}, 90); int dead = 0; for (auto& b : w.birds) if (b.st != BI_FLY) dead++; Check(dead >= 2, "Banana Bazooka: the bunch splits; bananas drop small birds", Fm("%.0f teal", dead)); }
    { World w = Quiet(1); Bird& g = Place(w, "goose", {20, 15, 40}); int id = g.id; w.players[0].guns[0] = MakeGun(GunIndex("honker")); Shoot(w, 0, {0, 10, 20}); bool turned = false; for (auto& x : w.birds) if (x.id == id) turned = x.honkT > 0 && x.honkTo == 0; Check(turned, "The Honker: every goose turns toward you"); }
    { World w = Quiet(1); for (int i = 0; i < 5; i++) Place(w, "goose", {i * 6.0f, 10, 30}); w.players[0].guns[0] = MakeGun(GunIndex("lightning")); Shoot(w, 0, {0, 10, 30}); int hurt = 0; for (auto& b : w.birds) if (b.hp < 3) hurt++; Check(hurt == 5, "Lightning Rod: the bolt chains through five geese", Fm("%.0f", hurt)); }
    { World w = Quiet(1); for (int i = 0; i < 6; i++) Place(w, "swan", {(i - 2.5f) * 5, 14, 40}); Place(w, "mallard", {60, 10, 10}); w.players[0].guns[0] = MakeGun(GunIndex("mega")); Shoot(w, 0, {0, 14, 40}); int dead = 0; for (auto& b : w.birds) if (b.st != BI_FLY) dead++; Check(dead == 6, "The Mega Zapper: everything in the cone, nothing outside it", Fm("%.0f", dead)); }
    { World w = Quiet(1); Bird& b = Place(w, "armored", {0, 10, 25}); int id = b.id; w.players[0].guns[0] = MakeGun(GunIndex("magnet")); Hold(w, {0, 10, 25}, 0, 4); Check(deadBy(w, id, 0), "Magnet Gun: an armored duck yanked to the rail"); }
    { World w = Quiet(1); Bird& b = Place(w, "mallard", {0, 8, 20}); b.v = {0, 0, 4}; int id = b.id; w.players[0].guns[0] = MakeGun(GunIndex("blower")); float z0 = b.p.z; for (int i = 0; i < 60; i++) { AimAt(w.players[0], {0, 8, 20}); w.players[0].in.fire = true; w.Step(); } float z1 = 0; for (auto& x : w.birds) if (x.id == id) z1 = x.p.z; Check(z1 < z0, "Leaf Blower: blows birds back toward the porch", Fm("%.1f -> %.1f m", z0, z1)); }
    { World w = Quiet(1); Vector3 e = w.players[0].Eye(), tg{0, 8, 18}; Place(w, "mallard", Vector3Lerp(e, tg, 0.5f)); Place(w, "teal", tg); w.players[0].guns[0] = MakeGun(GunIndex("boomerang")); Shoot(w, 0, tg, 200); int dead = 0; for (auto& b : w.birds) if (b.st != BI_FLY) dead++; Check(dead == 2 && w.players[0].G().ammo == 1, "Boomerang Blaster: out and back through both birds, and caught", Fm("%.0f", dead)); }
    { World w = Quiet(1); for (int i = 0; i < 6; i++) Place(w, "mallard", {(i - 2.5f) * 6, 20, 30}); w.players[0].guns[0] = MakeGun(GunIndex("firework")); Shoot(w, 0, {0, 26, 30}, 100); int dead = 0; for (auto& b : w.birds) if (b.st != BI_FLY) dead++; Check(dead >= 1 && dead <= 3, "Firework Launcher: a few random kills and a show", Fm("%.0f", dead)); }
    { World w = Quiet(1); w.players[0].guns[0] = MakeGun(GunIndex("flare")); Shoot(w, 0, {0, 20, 30}, 80); Check(w.flareT > 0, "Flare Pistol: the sky is lit for everyone"); }
    // the slots hold the house's edge over 10,000 pulls a bet
    { float e10 = SlotEV(10, 10000, 3), e25 = SlotEV(25, 10000, 4), e50 = SlotEV(50, 10000, 5), avg = (e10 + e25 + e50) / 3;
      Check(avg > 0.89f && avg < 0.95f && e10 < 1 && e25 < 1 && e50 < 1, "the slots keep about an 8% edge at every bet (10,000 pulls each)", Fm("returns %.3f / %.3f / %.3f", e10, e25, e50)); }
    // sabotage: each lands and each counter stops it; at most two a hunt; the leader costs half again
    { World w; w.Init(3, 0, 0, 9); w.players[1].birds = 10; w.players[0].money = 5000;
      for (auto& p : w.players) p.pos = World::Station(4);
      int bees = SabIndex("bees"), tax = SabIndex("tax"), rubber = SabIndex("rubber");
      int before = w.players[0].money; fp::Command c; c.kind = CMD_SABOTAGE; c.a = bees; c.b = 1; w.Command(w.players[0], c);
      Check(before - w.players[0].money == (int)roundf(D().sabotage[bees].price * 1.5f), "sabotage on the leader costs +50%");
      c.a = rubber; w.Command(w.players[0], c); int third = w.players[0].money; c.a = tax; w.Command(w.players[0], c);
      Check(w.players[1].incoming.size() == 2 && w.players[0].money == third, "at most two sabotage items per target per hunt (sold out)");
      w.players[2].money = 500; fp::Command cc; cc.kind = CMD_COUNTER; cc.a = bees; w.Command(w.players[1], cc);   // (no money: nothing)
      w.players[1].money = 500; w.Command(w.players[1], cc);
      w.phase = PH_INTER; w.phaseT = w.interLen - STEP; w.Step();
      Check(w.players[1].beesT <= 0 && w.players[1].rubberLeft > 0, "a counter bought in time stops its sabotage; the other lands");
      Shoot(w, 1, {0, 10, 30}); w.players[1].in.reload = true; w.Step(); w.players[1].in.reload = false; for (int i = 0; i < 200; i++) w.Step();
      Check(w.players[1].rubberLeft == 0, "Rubber Ducky Rounds: reloading early throws the ducks out"); }
    { World w; w.Init(2, 0, 0, 13); w.players[0].money = 2000; for (auto& p : w.players) p.pos = World::Station(4);
      int tax = SabIndex("tax"); fp::Command c; c.kind = CMD_SABOTAGE; c.a = tax; c.b = 1; w.Command(w.players[0], c);
      w.players[1].money = 500; fp::Command cc; cc.kind = CMD_COUNTER; cc.a = tax; w.Command(w.players[1], cc);
      w.phase = PH_INTER; w.phaseT = w.interLen - STEP; w.Step();
      Check(w.players[0].taxBy == 1 && w.players[1].taxBy < 0, "the lawyer returns the Taxman to sender"); }
    // the round structure: a whole match of bots runs its fifteen rounds
    { World w; w.Init(0, 6, 0, 21); uint32_t r = 3; int steps = 0;
      while (w.phase != PH_OVER && steps < 60 * 60 * 40) { for (auto& p : w.players) BotInput(w, p.id, p.in, p.cmds, r, p.id % 3); w.Step(); steps++; }
      int bought = 0; for (auto& p : w.players) bought += p.guns[1].Has();
      Check(w.phase == PH_OVER && w.round == 15 && w.champion >= 0, "six bots play a whole 15-round match", Fm("%.1f min", steps * STEP / 60));
      Check(bought >= 4, "the bots spend their money in the room", Fm("%.0f of 6 hold a second gun", bought)); }
    std::printf(gFails ? "Fowl Play: %d check(s) FAILED\n" : "Fowl Play: all checks passed\n", gFails);
    return gFails ? 1 : 0;
}

// ---------------------------------------------------------------- the sims
int RunFowlGambleSim(int pulls) {
    for (int bet : D().slotBets) std::printf("slots $%d: return %.3f over %d pulls\n", bet, SlotEV(bet, pulls, 77 + bet), pulls);
    return 0;
}
int RunFowlSim(int matches, int players, int skill) {
    double birdsR13 = 0, birdsR10 = 0; int n13 = 0, n10 = 0; std::map<std::string, int> killsByGun; int kills = 0; double gamble = 0; int sab = 0; double spent = 0;
    int gamblerWins = 0, gamblerMatches = 0;
    for (int m = 0; m < matches; m++) {
        World w; w.Init(0, players, 0, 1000 + m); uint32_t r = 7 + m; int steps = 0; size_t seen = 0; uint32_t seenCount = 0;
        std::vector<int> skills(players); for (int i = 0; i < players; i++) skills[i] = skill >= 0 ? skill : i % 3;
        int prevRound = 0; std::vector<int> prevBirds(players, 0);
        while (w.phase != PH_OVER && steps < 60 * 60 * 40) {
            for (auto& p : w.players) BotInput(w, p.id, p.in, p.cmds, r, skills[p.id]);
            w.Step(); steps++;
            uint32_t fresh = w.evCount - seenCount; seenCount = w.evCount;
            for (size_t k = w.events.size() - std::min<size_t>(fresh, w.events.size()); k < w.events.size(); k++) { const Event& e = w.events[k]; if (e.kind == EV_KILL && e.who >= 0) { kills++; killsByGun[e.by >= 0 ? D().guns[e.by].name : "?"]++; } }
            if (w.phase == PH_TALLY && w.phaseT < STEP * 0.5f) {
                if (getenv("DEPTH_FOWLLOG")) { std::printf("r%2d", w.round); for (auto& p : w.players) std::printf("  p%d %2d birds $%4d [%s / %s]", p.id, p.roundBirds, p.money, p.guns[0].Has() ? D().guns[p.guns[0].def].name.c_str() : "-", p.guns[1].Has() ? D().guns[p.guns[1].def].name.c_str() : "-"); std::printf("\n"); }
                for (auto& p : w.players) { if (w.round <= 3) { birdsR13 += p.roundBirds; n13++; } if (w.round >= 10 && w.round <= 12) { birdsR10 += p.roundBirds; n10++; } }
            }
            (void)seen; (void)prevRound;
        }
        for (auto& p : w.players) { gamble += p.gambleWon - p.gambleLost; sab += p.sabGiven; }
        // who leaned on gambling most, and did they win?
        int gb = -1, gl = 0; for (auto& p : w.players) if (p.gambleLost + p.gambleWon > gl) { gl = p.gambleLost + p.gambleWon; gb = p.id; }
        if (gb >= 0) { gamblerMatches++; if (w.champion == gb) gamblerWins++; }
    }
    std::printf("Fowl sim: %d matches, %d bots (skill %d)\n", matches, players, skill);
    std::printf("  birds a round per player: rounds 1-3 %.1f, rounds 10-12 %.1f\n", birdsR13 / std::max(1, n13), birdsR10 / std::max(1, n10));
    std::printf("  gambling net per player per match: %.0f; sabotage bought per match: %.1f; the heaviest gambler won %d of %d\n", gamble / (matches * players), sab / (double)matches, gamblerWins, gamblerMatches);
    std::printf("  kills by gun (share of %d):\n", kills);
    std::vector<std::pair<int, std::string>> v; for (auto& k : killsByGun) v.push_back({k.second, k.first}); std::sort(v.rbegin(), v.rend());
    for (auto& k : v) std::printf("    %-22s %5.1f%%\n", k.second.c_str(), 100.0 * k.first / std::max(1, kills));
    return 0;
}

}  // namespace fp
