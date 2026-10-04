// Scuffle stage 6: the checks of the full arsenal and the gear (ScuffleArsenalChecks, in --scuffle-test), and
// --scuffle-arsenal (every weapon against every other on three stage shapes: every weapon must win somewhere).
#include "scuffle.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>

namespace sf {

static int AF6 = 0;
static void C6(bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) AF6++; }
static float F(float tiles) { return tiles * TILE; }
static Stage Arena(int world = WD_NAUTILUS, int w = 40, int h = 18) {
    Stage s; s.w = w; s.h = h; s.t.assign(w * h, T_EMPTY); s.world = world; s.name = "arena";
    for (int x = 0; x < w; x++) { s.Set(x, 0, T_STONE); s.Set(x, 1, T_STONE); }
    s.spawns = {{3 * TILE, 2 * TILE}, {(w - 3) * TILE, 2 * TILE}};
    return s;
}
static World Place(const Stage& s, std::vector<float> xs) {
    World w; w.Init(s, (int)xs.size(), 5); w.nextCrate = 1e9f; w.wallOn = false;
    for (size_t i = 0; i < xs.size(); i++) w.SpawnStick(w.sticks[i], {xs[i], 2 * TILE}, 1);
    for (int i = 0; i < 60; i++) w.Step();
    return w;
}
static void Run(World& w, float s, std::function<void(World&)> fn = nullptr) { int n = (int)lroundf(s / STEP); for (int i = 0; i < n; i++) { if (fn) fn(w); w.Step(); } }
static int Arm(World& w, int who, const char* key) { Stick& k = w.sticks[who]; if (k.weapon >= 0) w.DropWeapon(k, {0, 0}, false); int it = w.SpawnWeapon(WeaponIndex(key), k.pt[J_HAND_R].p, {0, 0}); w.Pickup(k, it); k.fireCool = 0; return it; }
// a press of the trigger (or a hold for `hold` seconds), aimed
static void Shoot(World& w, int who, Vector2 aim, float hold = 0) {
    Stick& k = w.sticks[who]; k.in.aim = Vector2Normalize(aim); k.in.fire = false; w.Step();
    int n = std::max(1, (int)(hold / STEP)); for (int i = 0; i < n; i++) { w.sticks[who].in.aim = Vector2Normalize(aim); w.sticks[who].in.fire = true; w.Step(); }
    w.sticks[who].in.fire = false;
}
static int Count(const World& w, int kind) { int n = 0; for (const auto& th : w.things) n += th.alive && th.kind == kind; return n; }

int ScuffleArsenalChecks() {
    AF6 = 0;
    printf("Scuffle: stage 6 (the full arsenal and gear)\n");
    int on = 0; for (const auto& d : Weapons()) on += d.stage <= 6;
    C6(on == 48, TextFormat("all 48 weapons are in the crates (%d)", on));
    // ---- the strange guns
    { World w = Place(Arena(), {F(8), F(16)}); Arm(w, 0, "bees"); Shoot(w, 0, {1, 0.1f}); Run(w, 3);
      C6(w.sticks[1].hp < 100, TextFormat("the Bazooka of Bees: a swarm that follows the nearest stick (%.0f HP left)", w.sticks[1].hp)); }
    { World w = Place(Arena(), {F(8), F(12)}); Arm(w, 0, "flamethrower"); Shoot(w, 0, {1, 0}, 0.4f); bool burning = w.sticks[1].burnT > 0; Run(w, 1);
      Stage s = Arena(); for (int x = 14; x <= 20; x++) s.Set(x, 4, T_WOOD); World ww = Place(s, {F(8)}); Arm(ww, 0, "flamethrower"); Shoot(ww, 0, Vector2Subtract({F(15.5f), F(4.5f)}, ww.sticks[0].pt[J_HAND_R].p), 0.5f); Run(ww, 4);
      int left = 0; for (int x = 14; x <= 20; x++) left += ww.stage.At(x, 4) == T_WOOD;
      C6(burning && w.sticks[1].hp < 95 && left < 7, TextFormat("the flamethrower sets a stick alight (%.0f HP) and the wood burns (%d of 7 boards left)", w.sticks[1].hp, left)); }
    { World w = Place(Arena(), {F(8), F(14)}); Arm(w, 0, "icegun"); Shoot(w, 0, {1, 0}); Run(w, 0.5f); bool frozen = w.sticks[1].frozenT > 0; Run(w, 2.0f);
      C6(frozen && w.sticks[1].frozenT <= 0, "the ice gun: a statue for 2 s, then a ragdoll"); }
    { World w = Place(Arena(), {F(6), F(10), F(12.5f), F(15)}); Arm(w, 0, "tesla"); Shoot(w, 0, {1, 0.05f}); Run(w, 0.1f);
      int hit = 0; for (int i = 1; i < 4; i++) hit += w.sticks[i].hp < 100;
      C6(hit == 3, TextFormat("the tesla gun chains through three sticks (%d hit)", hit)); }
    { Stage s = Arena(WD_REEF); for (int x = 0; x < 40; x++) for (int y = 2; y < 5; y++) s.Set(x, y, T_WATER);
      World w = Place(s, {F(6), F(10), F(24)}); Arm(w, 0, "tesla"); Shoot(w, 0, {1, 0}); Run(w, 0.1f);
      C6(w.sticks[2].hp < 100, TextFormat("and through the water to anyone standing in it (%.0f HP 8 m off)", w.sticks[2].hp)); }
    { World w = Place(Arena(), {F(8), F(13)}); Arm(w, 0, "snakegun"); Shoot(w, 0, {1, 0.2f}); Run(w, 3);
      C6(Count(w, TH_SNAKE) >= 1 && w.sticks[1].hp < 100, TextFormat("the snake gun: a snake that bites and slithers (%.0f HP left)", w.sticks[1].hp)); }
    { World w = Place(Arena(), {F(8), F(18)}); Arm(w, 0, "blackhole"); Shoot(w, 0, {1, 0.1f}); Run(w, 4);
      C6(!w.sticks[1].alive || w.sticks[1].cause == "the black hole", TextFormat("the black hole gun pulls everything in (the far stick: %s)", w.sticks[1].alive ? "alive" : w.sticks[1].cause.c_str())); }
    { World w = Place(Arena(), {F(8), F(12)}); Arm(w, 0, "bubble"); Shoot(w, 0, {1, 0.1f}); Run(w, 1.5f);
      C6(w.sticks[1].bubbleT > 0 && w.sticks[1].pos.y > F(2) + 1.5f, TextFormat("the bubble gun carries a stick upward (%.1f m up)", w.sticks[1].pos.y - F(2))); }
    { World w = Place(Arena(), {F(8), F(12)}); Arm(w, 0, "gravitygun"); Shoot(w, 0, {1, 0.15f}, 1.2f);
      C6(w.sticks[1].pos.y > F(2) + 2, TextFormat("the gravity gun: the stick falls up (%.1f m)", w.sticks[1].pos.y - F(2))); }
    { Stage s = Arena(); s.Set(20, 3, T_ROPE); World w = Place(s, {F(8), F(25)}); Arm(w, 0, "laser"); Shoot(w, 0, Vector2Subtract(w.sticks[1].pt[J_NECK].p, w.sticks[0].pt[J_HAND_R].p), 1.0f);
      C6(w.sticks[1].hp < 70, TextFormat("the laser burns through (%.0f HP after a second)", w.sticks[1].hp));
      World r = Place(s, {F(8)}); Arm(r, 0, "laser"); Shoot(r, 0, Vector2Subtract({F(20.5f), F(3.5f)}, r.sticks[0].pt[J_HAND_R].p), 0.3f);
      C6(r.stage.At(20, 3) == T_EMPTY, "and cuts ropes"); }
    { Stage s = Arena(WD_REEF); World w = Place(s, {F(8), F(16)}); Arm(w, 0, "chum"); Shoot(w, 0, {1, 0.3f}); Run(w, 4);
      C6(Count(w, TH_FISH) >= 2, TextFormat("the chum cannon: the Reef's fish come to it (%d)", Count(w, TH_FISH))); }
    { World w = Place(Arena(), {F(8), F(13)}); Arm(w, 0, "netgun"); Shoot(w, 0, {1, 0.15f}); Run(w, 0.5f); float x0 = w.sticks[1].pos.x;
      Run(w, 1.5f, [](World& ww) { ww.sticks[1].in.moveX = 1; });
      C6(w.sticks[1].netT > 0 && fabsf(w.sticks[1].pos.x - x0) < 0.3f, "the net gun holds a stick 3 s"); }
    { World w = Place(Arena(), {F(8)}); Arm(w, 0, "boomerang"); Shoot(w, 0, {1, 0}); Run(w, 2.5f);
      C6(w.sticks[0].hp < 100 && w.sticks[0].cause.empty(), TextFormat("the boomerang comes back and hits you if you miss (%.0f HP)", w.sticks[0].hp)); }
    { World w = Place(Arena(), {F(8), F(10)}); Arm(w, 0, "confetti"); Shoot(w, 0, {1, 0}); Run(w, 0.4f);
      C6(w.sticks[1].hp == 100 && w.sticks[1].pos.x > F(10) + 1.5f, TextFormat("the confetti cannon: pure knockback (%.1f m, no damage)", w.sticks[1].pos.x - F(10))); }
    { Stage s = Arena(); for (int y = 2; y < 10; y++) { s.Set(39, y, T_STONE); s.Set(0, y, T_STONE); }
      World w = Place(s, {F(20)}); Arm(w, 0, "portal"); Shoot(w, 0, {1, 0.05f}); Run(w, 0.5f); w.sticks[0].fireCool = 0; Shoot(w, 0, {-1, 0.05f}); Run(w, 0.5f);
      float x0 = w.sticks[0].pos.x; bool jumped = false;
      Run(w, 4, [&](World& ww) { ww.sticks[0].in.moveX = 1; if (fabsf(ww.sticks[0].pos.x - x0) > 6 && ww.sticks[0].pos.x < x0) jumped = true; });
      C6(Count(w, TH_PORTAL) == 2 && jumped, "the portal gun: two portals, and you walk through one and out of the other"); }
    { Stage s = Arena(WD_REEF); for (int x = 0; x < 40; x++) for (int y = 2; y < 8; y++) s.Set(x, y, T_WATER);
      World w = Place(s, {F(8), F(13)}); Arm(w, 0, "speargun"); size_t b0 = w.bullets.size(); Shoot(w, 0, {1, 0}); Run(w, 0.05f);
      C6(w.bullets.size() > b0 || w.sticks[1].hp < 100, "the speargun works underwater"); }
    // ---- melee
    { World w = Place(Arena(), {F(8), F(16)}); Arm(w, 0, "axe"); Run(w, 0.3f, [](World& ww) { ww.sticks[0].in.moveY = -1; }); Stick& k = w.sticks[0]; k.in.moveY = -1; Shoot(w, 0, {1, 0.12f}); w.sticks[0].in.moveY = 0; Run(w, 1);
      C6(w.sticks[0].weapon < 0 && w.sticks[1].hp <= 40, TextFormat("duck and click throws what you hold: the axe spins for 60 (%.0f HP left)", w.sticks[1].hp)); }
    { World w = Place(Arena(), {F(8)}); Arm(w, 0, "trident"); Shoot(w, 0, {0.2f, -1}); float top = 0; Run(w, 1, [&](World& ww) { top = std::max(top, ww.sticks[0].pos.y); });
      C6(top > F(2) + 2.5f, TextFormat("the trident vaults (%.1f m)", top - F(2))); }
    { World w = Place(Arena(), {F(8), F(9.6f)}); int c = Arm(w, 0, "poolcue"); Shoot(w, 0, {1, 0.2f}); Run(w, 0.5f);
      int cues = 0; for (const auto& it : w.items) cues += it.alive && it.weapon == w.items[c].weapon;
      C6(cues == 2 && w.items[c].count == 1, "the pool cue breaks in two on a hit: two weapons"); }
    { Stage s = Arena(); World w = Place(s, {F(8)}); Arm(w, 0, "sledge"); Shoot(w, 0, {0.5f, -1}); Run(w, 1.2f);
      int gone = 0; for (int x = 6; x <= 11; x++) gone += !w.stage.Solid(x, 1);
      C6(gone > 0, "the sledgehammer breaks the floor tile it comes down on"); }
    { World w = Place(Arena(), {F(8), F(11)}); Arm(w, 0, "whip"); float x0 = w.sticks[1].pt[J_PELVIS].p.x; Shoot(w, 0, {1, 0.2f}); Run(w, 0.4f);
      C6(w.sticks[1].hp < 100 && w.sticks[1].pt[J_PELVIS].p.x < x0, "the whip pulls a stick in"); }
    { Stage s = Arena(WD_REEF); for (int x = 0; x < 40; x++) for (int y = 2; y < 6; y++) s.Set(x, y, T_WATER);
      World a = Place(s, {F(8)}); World b = a; Arm(b, 0, "oar");
      Run(a, 1.5f, [](World& ww) { ww.sticks[0].in.moveX = 1; }); Run(b, 1.5f, [](World& ww) { ww.sticks[0].in.moveX = 1; });
      C6(b.sticks[0].pos.x - F(8) > 1.5f * (a.sticks[0].pos.x - F(8)), TextFormat("the oar rows twice as fast (%.1f m against %.1f)", b.sticks[0].pos.x - F(8), a.sticks[0].pos.x - F(8))); }
    { World w = Place(Arena(), {F(8), F(9.8f), F(11.5f)}); Arm(w, 0, "teslagaff"); Shoot(w, 0, {1, 0.2f}); Run(w, 0.5f);
      C6(w.sticks[2].hp < 100, "the tesla gaff sparks on to the next stick"); }
    { World w = Place(Arena(), {F(8)}); Arm(w, 0, "fryingpan"); w.AddThing(TH_SWARM, Vector2Add(w.sticks[0].pt[J_PELVIS].p, {0.6f, 0}), {}, 8, -1);
      Run(w, 0.6f, [](World& ww) { ww.sticks[0].in.fire = (ww.frame / 20) % 2 == 0; ww.sticks[0].in.aim = {1, 0}; });
      C6(Count(w, TH_SWARM) == 0, "the frying pan cooks bees"); }
    // ---- thrown and placed
    { World w = Place(Arena(), {F(8), F(13)}); Arm(w, 0, "limpet"); Shoot(w, 0, Vector2Subtract(w.sticks[1].pt[J_PELVIS].p, w.sticks[0].pt[J_HAND_R].p)); Run(w, 0.4f);
      bool stuck = false; for (const auto& th : w.things) stuck |= th.kind == TH_STUCK && th.on == 1;
      Run(w, 2.2f);
      C6(stuck && w.sticks[1].hp < 50, TextFormat("a limpet charge sticks to a stick and goes off (%.0f HP left)", w.sticks[1].hp)); }
    { World w = Place(Arena(), {F(8)}); Arm(w, 0, "inkbomb"); Shoot(w, 0, {1, 0.5f}); Run(w, 1.5f);
      World f = Place(Arena(), {F(8)}); Arm(f, 0, "flashbang"); Shoot(f, 0, {1, 0.5f}); Run(f, 1.0f);
      C6(w.inkT > 0 && f.flashT > 0, "the ink bomb blinds the stage; the flashbang whites it out"); }
    { World w = Place(Arena(), {F(8), F(16)}); Arm(w, 0, "beartrap"); Shoot(w, 0, {1, 0.3f}); Run(w, 1.5f);
      bool placed = Count(w, TH_TRAP) == 1; float tx = 0; for (const auto& th : w.things) if (th.kind == TH_TRAP) tx = th.p.x;
      bool caught = w.sticks[1].trapT > 0;
      Run(w, 3, [&](World& ww) { ww.sticks[1].in.moveX = tx < ww.sticks[1].pos.x ? -1.0f : 1.0f; caught |= ww.sticks[1].trapT > 0; });
      C6((placed || caught) && caught, "a bear trap holds whoever steps on it"); }
    { World w = Place(Arena(), {F(8), F(22)}); Arm(w, 0, "turret"); Shoot(w, 0, {1, 0.3f}); Run(w, 4);
      C6(w.sticks[1].hp < 100, TextFormat("a turret shoots the nearest stick (%.0f HP left)", w.sticks[1].hp)); }
    { World w = Place(Arena(), {F(8), F(16)}); Arm(w, 0, "mine"); Shoot(w, 0, {1, 0.3f}); Run(w, 1.5f); float mx = 0; for (const auto& th : w.things) if (th.kind == TH_MINE) mx = th.p.x;
      Run(w, 3, [&](World& ww) { ww.sticks[1].in.moveX = mx < ww.sticks[1].pos.x ? -1.0f : 1.0f; });
      C6(w.sticks[1].hp < 50, TextFormat("a mine goes off underfoot (%.0f HP left)", w.sticks[1].hp)); }
    { World w = Place(Arena(), {F(8), F(16)}); Arm(w, 0, "banana"); Shoot(w, 0, {1, 0.3f}); Run(w, 1.5f); float px = 0; for (const auto& th : w.things) if (th.kind == TH_PEEL) px = th.p.x;
      bool slipped = false; Run(w, 3, [&](World& ww) { ww.sticks[1].in.moveX = px < ww.sticks[1].pos.x ? -1.0f : 1.0f; slipped |= ww.sticks[1].st == S_RAGDOLL; });
      C6(slipped && w.sticks[1].hp == 100, "a banana: you slip (no damage)"); }
    { World w = Place(Arena(), {F(8), F(14)}); Arm(w, 0, "snakejar"); Shoot(w, 0, {1, 0.3f}); Run(w, 1.2f);
      World h = Place(Arena(), {F(8), F(14)}); Arm(h, 0, "beehive"); Shoot(h, 0, {1, 0.3f}); Run(h, 1.2f);
      C6(Count(w, TH_SNAKE) == 4 && Count(h, TH_SWARM) == 1, "a jar of snakes (four) and a beehive (bees for 8 s)"); }
    // ---- gear
    { Stage s = Arena(); for (int x = 15; x < 25; x++) s.Set(x, 12, T_STONE);
      World w = Place(s, {F(12)}); w.sticks[0].gear = GR_HOOK;
      Run(w, 1.2f, [](World& ww) { ww.sticks[0].in.aim = Vector2Normalize({0.6f, 1}); ww.sticks[0].in.gear = true; });
      C6(w.sticks[0].pos.y > F(2) + 3, TextFormat("the grappling hook reels you in (%.1f m up)", w.sticks[0].pos.y - F(2))); }
    { World w = Place(Arena(), {F(8)}); w.sticks[0].gear = GR_JETPACK; Run(w, 1, [](World& ww) { ww.sticks[0].in.gear = true; });
      C6(w.sticks[0].pos.y > F(2) + 3, TextFormat("the jetpack lifts you (%.1f m in a second)", w.sticks[0].pos.y - F(2))); }
    { Stage s = Arena(); for (int x = 0; x < 40; x++) s.Set(x, 14, T_STONE);
      World w = Place(s, {F(8)}); w.SpawnStick(w.sticks[0], {F(8), F(15)}, 1); w.sticks[0].gear = GR_PARACHUTE; for (int x = 6; x <= 10; x++) w.stage.Set(x, 14, T_EMPTY);
      float worst = 0; Run(w, 1.5f, [&](World& ww) { ww.sticks[0].in.gear = true; worst = std::min(worst, ww.sticks[0].vel.y); });
      C6(worst >= -3.2f, TextFormat("the parachute: a slow fall (%.1f m/s at the fastest)", worst)); }
    { World w = Place(Arena(), {F(8), F(9.4f)}); w.sticks[1].gear = GR_SHIELD; w.sticks[1].face = -1; Arm(w, 0, "carbine");
      Run(w, 0.2f, [](World& ww) { ww.sticks[1].in.gear = true; ww.sticks[1].in.aim = {-1, 0}; }); Shoot(w, 0, {1, 0.1f}); Run(w, 0.3f, [](World& ww) { ww.sticks[1].in.gear = true; ww.sticks[1].in.aim = {-1, 0}; });
      C6(w.sticks[1].hp == 100, TextFormat("the shield blocks bullets from the front (%.0f HP)", w.sticks[1].hp)); }
    { World w = Place(Arena(), {F(8)}); w.sticks[0].gear = GR_SPRING; Run(w, 0.1f, [](World& ww) { ww.sticks[0].in.gear = true; }); w.sticks[0].in.gear = false;
      Run(w, 0.3f); float top = 0; Run(w, 1.6f, [&](World& ww) { ww.sticks[0].in.jump = ww.t < 3.0f; top = std::max(top, ww.sticks[0].pos.y); });
      C6(Count(w, TH_SPRING) == 1 && top > F(2) + 4, TextFormat("a spring: a trampoline (%.1f m)", top - F(2))); }
    { Stage s = Arena(); for (int x = 10; x <= 16; x++) { s.Set(x, 0, T_EMPTY); s.Set(x, 1, T_EMPTY); }
      World w = Place(s, {F(9)}); w.sticks[0].face = 1; w.sticks[0].gear = GR_ROPE; Run(w, 0.1f, [](World& ww) { ww.sticks[0].in.gear = true; ww.sticks[0].in.aim = {1, 0}; });
      int rope = 0; for (int x = 10; x <= 16; x++) rope += w.stage.At(x, 1) == T_ROPE;
      C6(rope >= 5, TextFormat("a rope: a bridge over a gap (%d tiles)", rope)); }
    { World w = Place(Arena(), {F(8), F(30)}); w.sticks[0].gear = GR_DECOY; Run(w, 0.1f, [](World& ww) { ww.sticks[0].in.gear = true; }); w.sticks[0].in.gear = false;
      w.AddThing(TH_SWARM, {F(20), F(5)}, {}, 4, -1); Run(w, 3);
      C6(Count(w, TH_DECOY) == 1 && w.sticks[1].hp == 100, "a decoy: bees go for it first"); }
    // ---- crates: gear and traps
    { int gear = 0, trap = 0, n = 400;
      for (int i = 0; i < n; i++) { World w; w.Init(Arena(), 1, 1000 + i); w.nextCrate = 1e9f; w.wallOn = false; w.SpawnStick(w.sticks[0], {F(10), F(2)}, 1);
        int c = w.DropCrate(F(10.5f)); w.items[c].chute = false; w.items[c].a.p = w.items[c].a.q = {F(10.5f), F(3.5f)};
        for (int s = 0; s < 120 && w.items[c].alive; s++) w.Step();
        gear += w.sticks[0].gear >= 0; trap += w.sticks[0].weapon < 0 && w.sticks[0].gear < 0; }
      C6(gear > n * 0.05f && gear < n * 0.16f && trap > 2 && trap < n * 0.08f, TextFormat("one crate in ten is gear (%d of %d), one in thirty a trap (%d)", gear, n, trap)); }
    printf(AF6 ? "  stage 6: %d FAILED\n" : "  stage 6: all checks passed\n", AF6);
    return AF6;
}

} // namespace sf
