// Scuffle stage 6b: trinkets, mutators and mid-round events (doc pp. 11-14). Headless.
// Most trinkets and mutators are one line where the thing they change is computed (the controller, Hit, Fire, the
// bullets, the statuses); this file names them, applies the round's rules after a world is built (ApplyRules), and runs
// the events, the hot potato and Ragdoll Royale (StepRules).
#include "scuffle.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

namespace sf {

const char* TrinketName(int t) {
    static const char* N[TK_COUNT] = {"None", "Spring Heels", "Thick Skull", "Lucky Crate", "Long Arms", "Magnet Palms", "Cat Legs", "Big Lungs", "Quick Draw", "Pack Rat", "Thick Coat", "Loud Mouth", "Second Wind", "Snake Charmer", "Deadweight"};
    return t >= 0 && t < TK_COUNT ? N[t] : "?";
}
const char* TrinketText(int t) {
    static const char* N[TK_COUNT] = {
        "A clean stick: round wins pay +10.",
        "Double jump. The cost: a shorter first jump.",
        "Headshots do normal damage. The cost: a slower get-up.",
        "The first crate of each round lands on you. The cost: it lands on you (30) if you don't move.",
        "Punch reach +50%. The cost: punches are 0.1 s slower.",
        "Wall climbs never slide. The cost: you can't dive.",
        "No fall stun. The cost: lighter, knockback +20%.",
        "Drowning takes twice as long; you can shoot underwater. The cost: a slower swim.",
        "Pick-ups are instant and the first shot has no spread. The cost: ammo -20%.",
        "Carry two weapons (the gear button swaps them). The cost: run speed -10%.",
        "Fire and ice don't stick. The cost: you can't climb.",
        "Your taunts push sticks within 1 m. The cost: in Blackout everyone can see you.",
        "Once a round you survive a killing hit at 1 HP. The cost: you're on fire for 2 s after.",
        "Snakes don't bite you. The cost: everyone else's snakes come to you.",
        "Knockback -50%; nothing bubbles or lifts you. The cost: you fall straight through bubbles."};
    return t >= 0 && t < TK_COUNT ? N[t] : "?";
}
const char* MutatorName(int m) {
    static const char* N[MU_COUNT] = {"Low Gravity", "Moon Shot", "Ricochet", "Big Heads", "Infinite Ammo", "One Hit", "Ragdoll Royale", "Snakes", "Hot Potato", "Blackout", "Giants", "Tiny", "Mirror", "Vampire", "Sudden Wall", "Pacifist", "Fast Forward"};
    return m >= 0 && m < MU_COUNT ? N[m] : "?";
}
const char* MutatorText(int m) {
    static const char* N[MU_COUNT] = {"Jumps are twice as high; everything floats", "Bullets are slow and visible", "Every bullet bounces once", "Headshots are easy; heads are balloons",
                                      "Nothing runs dry", "Everything kills, fists included", "Everyone is always ragdolled", "Every crate is snakes", "A bomb passes by touch; it goes off at 10 s",
                                      "Lights off; muzzle flashes and hazards glow", "Every stick is twice the size", "Every stick is half the size", "The stage is mirrored each round; wrap is on",
                                      "Kills heal 50", "The wall starts at 15 s", "Guns don't drop: melee, gear and hazards", "Everything at speed"};
    return m >= 0 && m < MU_COUNT ? N[m] : "?";
}
const char* EventName(int e) {
    static const char* N[RE_COUNT] = {"The Flood", "The Reach", "Lights Out", "Crate Rain", "Earthquake", "Swap", "The Dog", "Gravity Flip", "The Bouncer", "Fish Storm"};
    return e >= 0 && e < RE_COUNT ? N[e] : "?";
}

void World::Resize(Stick& k, float size) {
    static const float R[J_COUNT] = {0.16f, 0.07f, 0.09f, 0.06f, 0.07f, 0.06f, 0.07f, 0.06f, 0.07f, 0.06f, 0.07f};
    k.size = size; k.halfW = 0.24f * size; k.height = 1.7f * size;
    for (int j = 0; j < J_COUNT; j++) { k.pt[j].r = R[j] * size; Vector2 p = Vector2Add(k.pos, PoseOffset(k, j, t)); k.pt[j].p = k.pt[j].q = p; }
    if (Mut(MU_BIG_HEADS)) k.pt[J_HEAD].r *= 2.2f;
}
void World::ApplyRules() {
    if (Mut(MU_LOW_GRAVITY)) gravity = 15;
    if (Mut(MU_SNAKES)) arsenal = AR_SNAKES;
    float size = Mut(MU_GIANTS) ? 1.6f : Mut(MU_TINY) ? 0.55f : 1.0f;   // (a true double would wedge sticks in the stages' gaps: 1.6 reads as giant)
    for (auto& k : sticks) Resize(k, size);
    if (Mut(MU_MIRROR)) stage.wrap = true;
    for (auto& k : sticks) if (k.trinket == TK_LUCKY_CRATE && luckyFor < 0) luckyFor = k.id;
}

// ---------------------------------------------------------------- the round's event, the hot potato, ragdoll royale
void World::StepRules() {
    const float dt = STEP;
    lightsT = std::max(0.0f, lightsT - dt);
    // Ragdoll Royale: everyone is always a ragdoll; the stick flails toward where you push
    if (Mut(MU_RAGDOLL_ROYALE)) for (auto& k : sticks) {
        if (!k.alive || !k.present) continue;
        k.st = S_RAGDOLL; k.ragT = 1; k.stiff = 0;
        float push = k.in.moveX * 30 * dt * dt;
        for (auto& a : k.pt) a.q.x -= push;
        bool down = false; for (const auto& a : k.pt) down |= stage.Solid((int)floorf(a.p.x / TILE), (int)floorf((a.p.y - a.r - 0.05f) / TILE));
        k.walkPh += dt;
        if (down && fabsf(k.in.moveX) > 0.2f && k.walkPh > 0.45f) { k.walkPh = 0; for (auto& a : k.pt) { a.q.y -= 3.5f * dt; a.q.x -= k.in.moveX * 2.5f * dt; } }   // (a flop along the floor)
        if (k.in.jump && !k.jumpWas && down) for (auto& a : k.pt) a.q.y -= 9 * dt;   // (a flop upward)
        k.jumpWas = k.in.jump;
    }
    // the hot potato: from 3 s someone holds it; it passes by touch; at 10 s it goes off
    if (Mut(MU_HOT_POTATO) || mode == MD_POTATO) {
        int pot = -1; for (int i = 0; i < (int)things.size(); i++) if (things[i].alive && things[i].kind == TH_POTATO) pot = i;
        if (pot < 0 && ((potatoNext < 0 && t > 3) || (potatoNext > 0 && t >= potatoNext))) {
            potatoNext = 0;
            std::vector<int> live; for (const auto& k : sticks) if (k.alive && k.present) live.push_back(k.id);
            if (!live.empty()) { int p = AddThing(TH_POTATO, {}, {}, 10, -1); things[p].on = live[(int)(Rand() * live.size()) % live.size()]; Emit(EV_EVENT, sticks[things[p].on].pt[J_NECK].p, things[p].on, -1, 100); }
        }
        else if (pot >= 0) {
            Thing& th = things[pot];
            if (th.on >= 0 && th.on < (int)sticks.size()) {
                Stick& h = sticks[th.on];
                th.p = Vector2Add(h.pt[J_HAND_R].p, {0, 0.1f});
                if (th.cool <= 0) for (const auto& o : sticks) if (o.id != h.id && o.alive && o.present && Vector2Distance(o.pt[J_PELVIS].p, h.pt[J_PELVIS].p) < 0.75f) { th.owner = h.id; th.on = o.id; th.cool = 0.5f; Emit(EV_GRAB, o.pt[J_NECK].p, o.id, h.id); break; }
                if (!h.alive) { std::vector<int> live; for (const auto& k : sticks) if (k.alive && k.present) live.push_back(k.id); if (!live.empty()) th.on = live[(int)(Rand() * live.size()) % live.size()]; }
            }
            if (th.life <= dt) { Explode(th.p, 2.6f, 140, 16, th.owner, -1); th.alive = false; potatoNext = mode == MD_POTATO ? t + 1.5f : 1e9f; }   // (the Hot Potato mode: another comes)
        }
    }
    // the event: a one-second tell, then it happens
    if (event < 0) return;
    if (t >= eventAt - 1 && t < eventAt - 1 + dt * 1.5f) Emit(EV_EVENT, {stage.Width() / 2, stage.Height()}, -1, -1, (float)event);
    if (t < eventAt) return;
    float e = t - eventAt;
    bool first = e < dt * 1.5f;
    switch (event) {
    case RE_FLOOD: floodY = e < 10 ? stage.Height() * 0.5f * std::min(1.0f, e / 1.5f) : -10; break;   // (InLiquid reads it: guns stop, the tesla is king)
    case RE_REACH: {
        // the kraken's arm comes over the edge and sweeps the lowest platform (the lowest standing row)
        if (first) {
            reachRow = -1;
            for (int y = 0; y < stage.h - 3 && reachRow < 0; y++) for (int x = 0; x < stage.w; x++) if (stage.Solid(x, y) && !stage.Solid(x, y + 1) && !stage.Solid(x, y + 2)) { reachRow = y + 1; break; }
        }
        if (reachRow < 0 || e > 2.5f) { reachX = -10; break; }
        reachX = e < 0.5f ? -1 : (e - 0.5f) / 2.0f * (stage.Width() + 2) - 1;
        if (reachX >= 0) for (auto& k : sticks) if (k.alive && k.present && fabsf(k.pos.x - reachX) < 0.8f && k.pos.y < (reachRow + 2.5f) * TILE && k.pos.y > (reachRow - 0.5f) * TILE) Hit(k, k.lastHitBy >= 0 && t - k.lastHitT < 4 ? k.lastHitBy : -1, 100, {1, 0.5f}, 16, true, "the kraken's arm");
        break;
    }
    case RE_LIGHTS_OUT: if (first) lightsT = 5; break;
    case RE_CRATE_RAIN: if (first) for (int i = 0; i < 20; i++) { int c = DropCrate((0.5f + Rand() * (stage.w - 1)) * TILE); items[c].a.p.y += Rand() * 3; items[c].a.q = items[c].a.p; } break;
    case RE_EARTHQUAKE:
        if (first) {
            for (auto& tl : stage.t) if (tl == T_GLASS || tl == T_CRUMBLE || tl == T_CRYSTAL) tl = T_EMPTY;
            for (auto& p : stage.pieces) if (p.kind == PK_WINDOW) p.broken = true;
            moversStopped = true;
            Emit(EV_EXPLODE, {stage.Width() / 2, stage.Height() / 2}, -1, -1, 1);
        }
        break;
    case RE_SWAP:
        if (first) {
            std::vector<int> live; for (const auto& k : sticks) if (k.alive && k.present && k.st != S_RAGDOLL) live.push_back(k.id);
            if (live.size() >= 2) {
                std::vector<Vector2> at; for (int i : live) at.push_back(sticks[i].pos);
                for (size_t i = 0; i < live.size(); i++) { Stick& k = sticks[live[i]]; Vector2 to = at[(i + 1) % at.size()], d = Vector2Subtract(to, k.pos); k.pos = to; for (auto& a : k.pt) { a.p = Vector2Add(a.p, d); a.q = Vector2Add(a.q, d); } Emit(EV_PORTAL, to, k.id); }
            }
        }
        break;
    case RE_DOG:
        if (first) { int d = AddThing(TH_DOG, {-0.5f, 0}, {7, 0}, 7, -1); things[d].cool = 0; for (int y = 0; y < stage.h; y++) if (stage.Solid(0, y) && !stage.Solid(0, y + 1)) things[d].p.y = (y + 1) * TILE; }
        break;
    case RE_GRAVITY_FLIP: if (first) for (auto& k : sticks) if (k.alive && k.trinket != TK_DEADWEIGHT) k.gravT = 4; break;
    case RE_BOUNCER:
        if (first && leader >= 0 && leader < (int)sticks.size() && sticks[leader].alive) {   // (the leader is thrown toward the nearest edge, into whatever's there)
            Stick& k = sticks[leader]; float side = k.pos.x < stage.Width() / 2 ? -1.0f : 1.0f;
            Hit(k, -1, 20, Vector2Normalize({side, 0.5f}), 26, true, "the bouncer"); k.thrownT = 1;
            Emit(EV_THROW, k.pt[J_NECK].p, k.id, -1, 1);
        }
        break;
    case RE_FISH_STORM:
        if (first) { int fish = WeaponIndex("fish"); for (int i = 0; i < 12; i++) SpawnWeapon(fish, {(0.5f + Rand() * (stage.w - 1)) * TILE, stage.Height() + 1 + Rand() * 4}, {(Rand() - 0.5f) * 2, -2}); }
        break;
    }
}

// ---------------------------------------------------------------- the stage-6b checks (in --scuffle-test)
static int RF = 0;
static void RC(bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) RF++; }
static float F(float tiles) { return tiles * TILE; }
static Stage Field(int world = WD_NAUTILUS) {
    Stage s; s.w = 40; s.h = 18; s.t.assign(40 * 18, T_EMPTY); s.world = world; s.name = "field";
    for (int x = 0; x < 40; x++) { s.Set(x, 0, T_STONE); s.Set(x, 1, T_STONE); }
    s.spawns = {{F(3), F(2)}, {F(37), F(2)}};
    return s;
}
static World Make(const Stage& s, std::vector<float> xs, std::vector<int> trinkets = {}, uint32_t mut = 0) {
    World w; w.Init(s, (int)xs.size(), 5); w.nextCrate = 1e9f; w.wallOn = false; w.mut = mut;
    for (size_t i = 0; i < xs.size(); i++) { w.SpawnStick(w.sticks[i], {xs[i], F(2)}, 1); if (i < trinkets.size()) w.sticks[i].trinket = trinkets[i]; }
    w.ApplyRules();
    for (int i = 0; i < 60; i++) w.Step();
    return w;
}
static void Go(World& w, float s, std::function<void(World&)> fn = nullptr) { int n = (int)lroundf(s / STEP); for (int i = 0; i < n; i++) { if (fn) fn(w); w.Step(); } }
static int Hold(World& w, int who, const char* key) { Stick& k = w.sticks[who]; int it = w.SpawnWeapon(WeaponIndex(key), k.pt[J_HAND_R].p, {0, 0}); w.Pickup(k, it); k.fireCool = 0; return it; }
static void Pull(World& w, int who, Vector2 aim) { w.sticks[who].in.aim = Vector2Normalize(aim); w.sticks[who].in.fire = false; w.Step(); w.sticks[who].in.aim = Vector2Normalize(aim); w.sticks[who].in.fire = true; w.Step(); w.sticks[who].in.fire = false; }
static float JumpTop(World w, bool twice, int hold = 30) {
    float top = 0;
    for (int i = 0; i < 360; i++) { Stick& k = w.sticks[0]; k.in = Input{}; k.in.jump = i < hold || (twice && i >= 50 && i < 80); w.Step(); top = std::max(top, w.sticks[0].pos.y); }
    return top - F(2);
}
int ScuffleRulesChecks() {
    RF = 0;
    printf("Scuffle: stage 6b (trinkets, mutators, mid-round events)\n");
    // ---- trinkets
    { World a = Make(Field(), {F(10)}), b = Make(Field(), {F(10)}, {TK_SPRING_HEELS});
      float one = JumpTop(a, false), first = JumpTop(b, false), dbl = JumpTop(b, true);
      RC(first < one && dbl > one, TextFormat("Spring Heels: a shorter first jump (%.1f m against %.1f) and a double jump (%.1f m)", first, one, dbl)); }
    { float hp[2];
      for (int s = 0; s < 2; s++) { World a = Make(Field(), {F(8), F(16)}, {TK_NONE, s ? TK_THICK_SKULL : TK_NONE}); int it = Hold(a, 0, "carbine"); a.Step();
        Pull(a, 0, Vector2Subtract(a.sticks[1].pt[J_HEAD].p, a.items[it].b.p)); Go(a, 0.4f); hp[s] = a.sticks[1].hp; }
      RC(hp[1] > hp[0], TextFormat("Thick Skull: a carbine headshot leaves %.0f HP (%.0f without)", hp[1], hp[0])); }
    { World w = Make(Field(), {F(12), F(30)}, {TK_LUCKY_CRATE}); w.nextCrate = w.t + 0.1f; Go(w, 0.3f);
      float cx = -1; for (const auto& it : w.items) if (it.crate) cx = it.a.p.x;
      RC(fabsf(cx - w.sticks[0].pos.x) < 0.3f, "Lucky Crate: the round's first crate comes straight down on you"); }
    { float hp[2];
      for (int s = 0; s < 2; s++) { World w = Make(Field(), {F(8), F(9.9f)}, {s ? TK_LONG_ARMS : TK_NONE}); w.sticks[1].pos.x = w.sticks[0].pos.x + 1.05f; Go(w, 0.3f);
        Go(w, 0.6f, [](World& ww) { ww.sticks[0].in.aim = {1, 0.1f}; ww.sticks[0].in.fire = ww.frame % 70 < 3; }); hp[s] = w.sticks[1].hp; }
      RC(hp[1] < hp[0], TextFormat("Long Arms: a jab lands at 1 m (%.0f HP, %.0f with short arms)", hp[1], hp[0])); }
    { Stage s = Field(); for (int y = 2; y < 16; y++) s.Set(25, y, T_STONE);
      World w = Make(s, {F(23)}, {TK_MAGNET_PALMS}); w.sticks[0].in.jump = true; w.Step();
      float top = 0; Go(w, 3, [&](World& ww) { ww.sticks[0].in.moveX = 1; ww.sticks[0].in.jump = false; top = std::max(top, ww.sticks[0].pos.y); });
      RC(top > F(2) + 5, TextFormat("Magnet Palms: a wall climb never slides (%.1f m up in 3 s)", top - F(2))); }
    { Stage s = Field();
      World w = Make(s, {F(10)}, {TK_CAT_LEGS}); w.SpawnStick(w.sticks[0], {F(10), F(2) + 12}, 1); w.sticks[0].trinket = TK_CAT_LEGS; Go(w, 2);
      RC(w.sticks[0].st != S_RAGDOLL, "Cat Legs: no stun after a 12 m fall"); }
    { Stage s = Field(WD_REEF); for (int x = 0; x < 40; x++) for (int y = 2; y < 9; y++) s.Set(x, y, T_WATER);
      World w = Make(s, {F(10)}, {TK_BIG_LUNGS}); Go(w, 11);
      RC(w.sticks[0].alive, "Big Lungs: still breathing after 11 s under water"); }
    { World w = Make(Field(), {F(10)}, {TK_QUICK_DRAW}); int it = Hold(w, 0, "carbine");
      RC(w.items[it].ammo == 8 && w.sticks[0].fireCool == 0, TextFormat("Quick Draw: an instant pick-up, ammo -20%% (%d of 10)", w.items[it].ammo)); }
    { World w = Make(Field(), {F(10)}, {TK_PACK_RAT}); int a = Hold(w, 0, "carbine"); int b = Hold(w, 0, "sniper");
      bool two = w.sticks[0].weapon == a && w.sticks[0].carry == b;
      w.sticks[0].in.gear = true; w.Step(); w.sticks[0].in.gear = false; w.Step();
      RC(two && w.sticks[0].weapon == b, "Pack Rat: two weapons; the gear button swaps them"); }
    { World w = Make(Field(), {F(8), F(12)}, {TK_NONE, TK_THICK_COAT}); Hold(w, 0, "flamethrower"); Go(w, 0.5f, [](World& ww) { ww.sticks[0].in.aim = {1, 0}; ww.sticks[0].in.fire = true; });
      RC(w.sticks[1].burnT <= 0, "Thick Coat: fire doesn't stick"); }
    { World w = Make(Field(), {F(10), F(11.4f)}, {TK_LOUD_MOUTH}); float x0 = w.sticks[1].pos.x; w.sticks[0].in.taunt = true; w.Step(); w.sticks[0].in.taunt = false; Go(w, 0.5f);
      RC(w.sticks[1].pos.x > x0 + 0.5f, TextFormat("Loud Mouth: a taunt shoves (%.1f m)", w.sticks[1].pos.x - x0)); }
    { World w = Make(Field(), {F(10)}, {TK_SECOND_WIND}); w.Hit(w.sticks[0], -1, 150, {0, 1}, 0, false, "a test"); bool once = w.sticks[0].alive && w.sticks[0].hp <= 1 && w.sticks[0].burnT > 0;
      w.Hit(w.sticks[0], -1, 150, {0, 1}, 0, false, "a test");
      RC(once && !w.sticks[0].alive, "Second Wind: once a round a killing hit leaves 1 HP (and you burn)"); }
    { World w = Make(Field(), {F(10)}, {TK_SNAKE_CHARMER}); w.Snakes(w.sticks[0].pos, 3, -1); Go(w, 3);
      RC(w.sticks[0].hp == 100, "Snake Charmer: snakes don't bite you"); }
    { float run[2];
      for (int s = 0; s < 2; s++) { World w = Make(Field(), {F(10)}, {s ? TK_DEADWEIGHT : TK_NONE}); float x0 = w.sticks[0].pt[J_PELVIS].p.x; w.Hit(w.sticks[0], -1, 0, {1, 0.3f}, 12, true, "a test"); float far = x0; Go(w, 1.5f, [&](World& ww) { far = std::max(far, ww.sticks[0].pt[J_PELVIS].p.x); }); run[s] = far - x0; }
      RC(run[1] < run[0] * 0.75f, TextFormat("Deadweight: knockback halved (%.1f m against %.1f)", run[1], run[0])); }
    // ---- mutators
    { World w = Make(Field(), {F(10)}, {}, 1u << MU_LOW_GRAVITY); float hi = JumpTop(w, false, 200); World n = Make(Field(), {F(10)}); float lo = JumpTop(n, false, 200);
      RC(hi > lo * 1.7f, TextFormat("Low Gravity: jumps twice as high (%.1f m against %.1f)", hi, lo)); }
    { World w = Make(Field(), {F(10)}, {}, 1u << MU_MOON_SHOT); int it = Hold(w, 0, "carbine"); Pull(w, 0, {1, 0});
      RC(!w.bullets.empty() && Vector2Length(w.bullets.back().v) < 30, TextFormat("Moon Shot: slow bullets (%.0f m/s)", w.bullets.empty() ? 0.0f : Vector2Length(w.bullets.back().v))); (void)it; }
    { World w = Make(Field(), {F(10)}, {}, 1u << MU_RICOCHET); Hold(w, 0, "carbine"); Pull(w, 0, {1, 0});
      RC(!w.bullets.empty() && w.bullets.back().bounces >= 1, "Ricochet: every bullet bounces once"); }
    { World w = Make(Field(), {F(10)}, {}, 1u << MU_BIG_HEADS); RC(w.sticks[0].pt[J_HEAD].r > 0.3f, TextFormat("Big Heads: heads are balloons (%.2f m)", w.sticks[0].pt[J_HEAD].r)); }
    { World w = Make(Field(), {F(10)}, {}, 1u << MU_INFINITE_AMMO); int it = Hold(w, 0, "carbine"); for (int i = 0; i < 5; i++) { Pull(w, 0, {1, 0}); Go(w, 0.6f); }
      RC(w.items[it].ammo == 10, "Infinite Ammo: nothing runs dry"); }
    { World w = Make(Field(), {F(10), F(11.3f)}, {}, 1u << MU_ONE_HIT); Go(w, 0.5f, [](World& ww) { ww.sticks[0].in.aim = {1, 0.1f}; ww.sticks[0].in.fire = ww.frame % 60 < 3; });
      RC(!w.sticks[1].alive, "One Hit: a jab kills"); }
    { World w = Make(Field(), {F(10)}, {}, 1u << MU_RAGDOLL_ROYALE); float x0 = w.sticks[0].pt[J_PELVIS].p.x; Go(w, 3, [](World& ww) { ww.sticks[0].in.moveX = 1; });
      RC(w.sticks[0].st == S_RAGDOLL && w.sticks[0].pt[J_PELVIS].p.x > x0 + 0.5f, TextFormat("Ragdoll Royale: always a ragdoll, flailing along (%.1f m)", w.sticks[0].pt[J_PELVIS].p.x - x0)); }
    { Match m; m.mutators = 1u << MU_SNAKES; m.Start(2, 5, 7); int snakes = 0; for (int i = 0; i < 20; i++) { int wi = m.w.RollWeapon(); snakes += Weapons()[wi].key == "snakegun" || Weapons()[wi].key == "snakejar"; }
      RC(snakes == 20, "Snakes: every crate is snakes"); }
    { World w = Make(Field(), {F(8), F(9.2f)}, {}, 1u << MU_HOT_POTATO); bool held = false, passed = false; int first = -1;
      Go(w, 12, [&](World& ww) { for (const auto& th : ww.things) if (th.kind == TH_POTATO) { if (first < 0) first = th.on; held = true; if (th.on != first) passed = true; } if (ww.t > 4 && ww.t < 8) { int h = -1; for (const auto& th : ww.things) if (th.kind == TH_POTATO) h = th.on; if (h >= 0) { int o = 1 - h; ww.sticks[o].in.moveX = ww.sticks[h].pos.x > ww.sticks[o].pos.x ? 1.0f : -1.0f; } } });
      bool boom = w.sticks[0].hp < 100 || w.sticks[1].hp < 100;
      RC(held && passed && boom, "Hot Potato: a bomb passes by touch and goes off at 10 s"); }
    { World w = Make(Field(), {F(10)}, {}, 1u << MU_GIANTS); World t = Make(Field(), {F(10)}, {}, 1u << MU_TINY);
      RC(w.sticks[0].height > 2.5f && t.sticks[0].height < 1.0f, TextFormat("Giants and Tiny (%.1f m and %.1f m tall)", w.sticks[0].height, t.sticks[0].height)); }
    { Match m; m.mutators = 1u << MU_MIRROR; m.custom = {Field()}; m.custom[0].Set(2, 5, T_STONE); m.Start(2, 5, 3);
      RC(m.w.stage.At(37, 5) == T_STONE && m.w.stage.wrap, "Mirror: the stage is mirrored and wraps"); }
    { World w = Make(Field(), {F(10), F(11.3f)}, {}, (1u << MU_VAMPIRE) | (1u << MU_ONE_HIT)); w.sticks[0].hp = 30; Go(w, 0.5f, [](World& ww) { ww.sticks[0].in.aim = {1, 0.1f}; ww.sticks[0].in.fire = ww.frame % 60 < 3; });
      RC(!w.sticks[1].alive && w.sticks[0].hp >= 80, TextFormat("Vampire: a kill heals 50 (%.0f HP)", w.sticks[0].hp)); }
    { World w = Make(Field(), {F(10)}, {}, 1u << MU_SUDDEN_WALL); w.wallOn = true; Go(w, 25);
      RC(!w.sticks[0].alive, "Sudden Wall: the wall starts at 15 s"); }
    { Match m; m.mutators = 1u << MU_PACIFIST; m.Start(2, 5, 9); int guns = 0; for (int i = 0; i < 60; i++) guns += Weapons()[m.w.RollWeapon()].kind == "gun";
      RC(guns == 0, "Pacifist: guns don't drop"); }
    { Match a, b; a.custom = b.custom = {Field()}; b.mutators = 1u << MU_FAST_FORWARD; a.Start(2, 5, 11); b.Start(2, 5, 11);
      for (int i = 0; i < 600; i++) { a.Step(); b.Step(); }
      RC(b.w.t > a.w.t * 1.3f, TextFormat("Fast Forward: everything at speed (%.1f s of fight against %.1f)", b.w.t, a.w.t)); }
    { Match m; m.randomMutator = true; m.Start(2, 50, 5); uint32_t seen = 0; for (int r = 0; r < 40; r++) { seen |= m.roundMut; m.NewRound(); }
      int kinds = 0; for (int i = 0; i < MU_COUNT; i++) kinds += (seen >> i) & 1u;
      RC(kinds >= 8, TextFormat("Random picks a mutator each round (%d different in 40 rounds)", kinds)); }
    // ---- events
    auto ev = [&](int kind, std::function<void(World&)> setup, std::function<bool(World&)> ok, const char* what) {
        Stage s = Field(); World w = Make(s, {F(10), F(20)}); if (setup) setup(w); w.event = kind; w.eventAt = w.t + 1.5f; bool told = false; uint32_t base = w.eventBase + (uint32_t)w.events.size();
        Go(w, 4.0f, [&](World& ww) { for (uint32_t i = std::max(base, ww.eventBase); i < ww.eventBase + ww.events.size(); i++) told |= ww.events[i - ww.eventBase].kind == EV_EVENT; base = ww.eventBase + (uint32_t)ww.events.size(); });
        RC(told && ok(w), std::string(EventName(kind)) + ": " + what);
    };
    ev(RE_FLOOD, nullptr, [](World& w) { return w.InLiquid({F(10), F(3)}); }, "water rises to half the stage");
    ev(RE_REACH, nullptr, [](World& w) { return !w.sticks[0].alive && w.sticks[0].cause == "the kraken's arm"; }, "the kraken's arm sweeps the lowest platform");
    ev(RE_LIGHTS_OUT, nullptr, [](World& w) { return w.lightsT > 0; }, "five seconds of dark");
    ev(RE_CRATE_RAIN, nullptr, [](World& w) { return w.crates >= 20; }, "twenty crates at once");
    ev(RE_EARTHQUAKE, [](World& w) { w.stage.Set(15, 6, T_GLASS); }, [](World& w) { return w.stage.At(15, 6) == T_EMPTY && w.moversStopped; }, "every breakable breaks, every mover stops");
    { Stage s = Field(); World w = Make(s, {F(10), F(20)}); float a0 = w.sticks[0].pos.x; w.event = RE_SWAP; w.eventAt = w.t + 1.5f; Go(w, 2.0f);
      RC(fabsf(w.sticks[0].pos.x - F(20)) < 0.5f && fabsf(w.sticks[1].pos.x - a0) < 0.5f, "Swap: every stick swaps places with another"); }
    ev(RE_DOG, nullptr, [](World& w) { return w.sticks[0].hp < 100; }, "the alley dog runs through and bites");
    ev(RE_GRAVITY_FLIP, [](World& w) { for (int x = 0; x < 40; x++) w.stage.Set(x, 17, T_STONE); }, [](World& w) { return w.sticks[0].pos.y > F(8); }, "up is down");
    ev(RE_BOUNCER, [](World& w) { w.leader = 1; }, [](World& w) { return w.sticks[1].hp <= 80; }, "the bouncer throws the leader");
    ev(RE_FISH_STORM, nullptr, [](World& w) { int fish = 0; for (const auto& it : w.items) fish += it.alive && it.weapon == WeaponIndex("fish"); return fish >= 12; }, "fish fall from the sky");
    { int with = 0; for (int i = 0; i < 400; i++) { Match m; m.custom = {Field()}; m.Start(2, 5, 1000 + i); with += m.w.event >= 0; }
      RC(with > 70 && with < 130, TextFormat("one round in four has an event (%d of 400)", with)); }
    printf(RF ? "  stage 6b: %d FAILED\n" : "  stage 6b: all checks passed\n", RF);
    return RF;
}

} // namespace sf
