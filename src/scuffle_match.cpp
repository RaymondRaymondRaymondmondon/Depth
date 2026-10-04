// Scuffle stage 2: rounds and matches (doc pp. 4-5). Headless. A match draws stages from a playlist (no stage twice until
// it runs out); a round is a 1 s countdown, the fight, and the winner's pose; the round's last stick standing (a
// ragdolled living stick counts) scores the round; first to N wins the match. Match point is played on a finale (the
// wall at 30 s). Score: round won 100, kill 20 (+10 knocked into a hazard, +20 by a block-deflect), survived to the
// wall 10, match won 300. Also --scuffle-sim and the stage-2 checks.
#include "scuffle.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>

namespace sf {

static std::vector<Stage> Pack(int world) {
    static std::vector<Stage> cache[WD_COUNT]; static bool loaded[WD_COUNT] = {};
    if (!loaded[world]) { cache[world] = LoadWorldPack(world); loaded[world] = true; }
    return cache[world];
}
std::vector<Stage> FinalePlaylist(int world) {
    std::vector<Stage> v;
    for (int w = 0; w < WD_COUNT; w++) if (world < 0 || world >= WD_COUNT || w == world) for (const auto& s : Pack(w)) if (s.finale) v.push_back(s);
    return v;
}
std::vector<Stage> StagePlaylist(int world) {
    std::vector<Stage> v;
    for (int w = 0; w < WD_COUNT; w++) if (world < 0 || world >= WD_COUNT || w == world) for (const auto& s : Pack(w)) if (!s.finale) v.push_back(s);
    if (!v.empty()) return v;
    v.push_back(StoneStage());
    v.push_back(StageFromText({
        "................................",
        "................................",
        "................................",
        "..............####..............",
        "................................",
        "................................",
        "......####............####......",
        "................................",
        "................................",
        "..S......####......####......S..",
        "..##...........S..........##....",
        "............######..............",
        "....S.....................S.....",
        "...####...................####..",
        "................................",
        "#.......####..........####.....#",
        "#..............................#",
        "#####....##################..###",
    }, "The Steps"));
    v.push_back(StageFromText({
        "................................",
        "................................",
        "................................",
        "................................",
        "...####..................####...",
        "................................",
        "................................",
        "...S........................S...",
        "..####.....S........S.....####..",
        "..#..#...############.....#..#..",
        "..#..#....................#..#..",
        "..#..#....S..........S....#..#..",
        "..#..######################..#..",
        "..#..#....................#..#..",
        "..#..#....................#..#..",
        "###..#....................#..###",
        "###..#....................#..###",
        "###..#....................#..###",
    }, "The Bridge"));
    return v;
}

void Match::Start(int nPlayers, int roundsToWin, uint32_t s, int ars) {
    players = std::clamp(nPlayers, 1, MAX_STICKS); toWin = std::max(1, roundsToWin); seed = s ? s : 1; arsenal = ars;
    wins.assign(players, 0); score.assign(players, 0); roundKills.assign(players, 0);
    playlist = custom.empty() ? StagePlaylist(world) : custom; round = 0; draws = 0; champion = -1; log.clear();
    if (mode == MD_CHAOS) { randomMutator = true; arsenal = AR_RANDOM; }
    if (mode == MD_KING) target = 60; if (mode == MD_EGG) target = 30;
    if (mode == MD_KING || mode == MD_EGG || mode == MD_GAUNTLET) toWin = 1;
    if (mode == MD_DUEL) toWin = 4;   // (best of 7)
    if (mode == MD_BOSS) toWin = world >= 0 && world < WD_COUNT ? 1 : BK_COUNT;   // (one boss, or all six in turn)
    gStage = 0; gDeaths = 0; gTime = 0; gFailed = false;
    // trinkets: each player's pick, or the game picks (it's seeded)
    trinkets.resize(players, -1);
    { uint32_t r = seed * 2246822519u + 99; for (auto& tk : trinkets) { r = r * 1664525u + 1013904223u; if (tk < 0 || tk >= TK_COUNT) tk = (int)((r >> 8) % TK_COUNT); } }
    finales = custom.empty() ? FinalePlaylist(world) : std::vector<Stage>{};
    if (mode == MD_DUEL && !finales.empty()) playlist = finales;   // (the Duel: best of 7 on finale stages)
    // the rotation: shuffled by the seed (no stage twice until the list runs out)
    uint32_t r = seed;
    for (int i = (int)playlist.size() - 1; i > 0; i--) { r = r * 1664525u + 1013904223u; int j = (int)((r >> 8) % (uint32_t)(i + 1)); std::swap(playlist[i], playlist[j]); }
    NewRound();
}
void Match::NewRound() {
    round++;
    bool matchPoint = false; for (int x : wins) matchPoint |= x >= toWin - 1;
    stageIdx = (round - 1) % std::max(1, (int)playlist.size());
    bool fin = (matchPoint && toWin > 1 && mode != MD_KING && mode != MD_EGG) || mode == MD_DUEL;
    uint32_t rs = seed * 2654435761u + round * 7919u;
    roundMut = mutators; if (randomMutator) roundMut |= 1u << ((rs >> 12) % MU_COUNT);   // (the lobby's stack and Random's pick)
    bool mirror = (roundMut >> MU_MIRROR) & 1u;
    static const int BOSS_ORDER[BK_COUNT] = {BK_LOBSTER, BK_KRAKEN, BK_WYRM, BK_SUN_GOD, BK_GOLIATH, BK_BOUNCER};   // (the doc's order)
    int bossKind = world >= 0 && world < WD_COUNT ? BossOfWorld(world) : BOSS_ORDER[gStage % BK_COUNT];
    if (mode == MD_BOSS) { fin = false; w.Init(BossArena(bossKind), players, rs); }
    else if (mode == MD_GAUNTLET) w.Init(GauntletStage(gWorld, gStage, seed), players, rs);   // (the Gauntlet: its next stage)
    else if (world == WD_COUNT && custom.empty()) {   // (endless: a fresh stage from the generator each round, any world)
        Stage g = GenerateStage((int)(rs % WD_COUNT), rs, fin);
        w.Init(g, players, rs);
    }
    else if (fin && !finales.empty()) { const Stage& f = finales[(rs >> 8) % finales.size()]; w.Init(mirror ? MirrorStage(f) : f, players, rs); }   // (match point: a finale stage, the wall at 30 s)
    else w.Init(mirror ? MirrorStage(playlist[stageIdx]) : playlist[stageIdx], players, rs);
    w.arsenal = arsenal; w.finale = fin;
    // the round's rules; one round in four an event, at 10-30 s; the trinkets
    w.mut = roundMut;
    w.event = ((rs >> 4) % 4 == 0) ? (int)((rs >> 9) % RE_COUNT) : -1; w.eventAt = 10 + (float)((rs >> 16) % 2000) / 100.0f;
    w.leader = -1; { int best = 0; for (int i = 0; i < (int)wins.size(); i++) if (wins[i] > best) { best = wins[i]; w.leader = i; } }
    for (int i = 0; i < (int)w.sticks.size() && i < (int)trinkets.size(); i++) w.sticks[i].trinket = trinkets[i];
    w.ApplyRules();
    phase = P_COUNT; phaseT = 1.0f; roundWinner = -1;
    // the mode (scuffle_modes.cpp)
    w.mode = mode; w.friendlyFire = friendlyFire || mode == MD_GAUNTLET ? (mode != MD_GAUNTLET) : false;
    w.wallOn = wallOn && mode != MD_KING && mode != MD_EGG && mode != MD_GAUNTLET;
    for (int i = 0; i < (int)w.sticks.size(); i++) { w.sticks[i].team = TeamOf(i); w.sticks[i].persona = (int)((i * 3 + seed) % PE_COUNT); }
    if (mode == MD_CHAOS) { w.event = (int)((rs >> 9) % RE_COUNT); w.arsenal = AR_RANDOM; }
    if (mode == MD_KING) w.MovePlank();
    if (mode == MD_BOSS) { w.boss = MakeBoss(bossKind, players, w.stage); w.event = -1; w.friendlyFire = false; }
    if (mode == MD_HUNT && players > 1) {   // (the Shark: a different stick each round)
        sf::Stick& s = w.sticks[(round - 1) % players]; s.shark = true; s.hp = 200;
        int it = w.SpawnWeapon(WeaponIndex("harpoon"), s.pt[J_HAND_R].p, {0, 0}); w.Pickup(s, it); w.items[it].ammo = 99;
    }
    if (mode == MD_DUEL) {   // (three weapons offered, the same to both; the countdown is the time to choose)
        phaseT = 4.0f; uint32_t r = rs;
        for (int k = 0; k < 3; k++) { int wi; int tries = 0; do { r = r * 1664525u + 1013904223u; wi = (int)((r >> 8) % Weapons().size()); } while ((Weapons()[wi].support || Weapons()[wi].kind == "thrown" || (k > 0 && wi == duelOffer[0]) || (k > 1 && wi == duelOffer[1])) && ++tries < 50); duelOffer[k] = wi; }
        for (int& p : duelPick) p = 0;
    }
    if (mode == MD_GAUNTLET) {   // (no crates; everyone at the start; the exit at the spawn farthest from it)
        w.nextCrate = 1e9f; w.start = 0; float far = -1;
        for (const auto& sp : w.stage.spawns) { float d = Vector2Distance(sp, w.stage.spawns[0]); if (d > far) { far = d; w.goal = sp; } }
        for (auto& k : w.sticks) { k.finished = -1; w.Respawn(k); k.pos.x += (k.id - (players - 1) * 0.5f) * 0.4f; }
    }
    std::fill(roundKills.begin(), roundKills.end(), 0);
    evSeen = w.eventBase;
}
void Match::Step() {
    if (phase == P_OVER) return;
    if (phase == P_COUNT) {
        if (mode == MD_DUEL) for (int i = 0; i < players && i < MAX_STICKS; i++) { int p = w.sticks[i].in.pick; if (p >= 1 && p <= 3) duelPick[i] = p; }
        for (auto& k : w.sticks) k.in = Input{};
    }
    w.Step();
    if (phase == P_FIGHT && w.Mut(MU_FAST_FORWARD) && (w.frame & 1)) w.Step();   // (Fast Forward: half again as fast)
    // the round's scoring from the world's log: kills (and their bonuses), the wall
    const auto& E = w.events; uint32_t base = w.eventBase; if (evSeen < base) evSeen = base;
    for (uint32_t i = evSeen; i < base + E.size(); i++) {
        const Event& e = E[i - base];
        if (e.kind == EV_DIE && e.by >= 0 && e.by < players && e.by != e.who && phase == P_FIGHT) { score[e.by] += 20 + (e.a == 1 ? 10 : e.a == 2 ? 20 : 0); roundKills[e.by]++; }
        if (e.kind == EV_DIE && (mode == MD_GAUNTLET || mode == MD_BOSS) && phase == P_FIGHT) gDeaths++;
        if (e.kind == EV_WALL) for (const auto& k : w.sticks) if (k.alive && k.present && k.id < players) score[k.id] += 10;
    }
    evSeen = base + (uint32_t)E.size();
    int rw = -1;
    if (phase == P_COUNT && (phaseT -= STEP) <= 0) {
        phase = P_FIGHT;
        if (mode == MD_DUEL) for (int i = 0; i < players && i < (int)w.sticks.size(); i++) {   // (each stick's pick, or the first if it chose none)
            int p = duelPick[i] >= 1 ? duelPick[i] : 1 + (int)((seed + round * 31 + i * 7) % 3);
            Stick& k = w.sticks[i]; if (k.weapon >= 0) w.DropWeapon(k, {0, 0}, false); int it = w.SpawnWeapon(duelOffer[p - 1], k.pt[J_HAND_R].p, {0, 0}); w.Pickup(k, it);
        }
    }
    else if (phase == P_FIGHT && RoundOver(&rw)) {
        phase = P_WIN; phaseT = 1.5f; roundWinner = rw;
        if (champion < 0) for (int i = 0; i < players; i++) if (wins[i] >= toWin) { champion = i; score[i] += 300; break; }
        if (mode == MD_GAUNTLET) { if (champion >= 0) phase = P_WIN; }
    }
    else if (phase == P_WIN && (phaseT -= STEP) <= 0) { if (champion >= 0) phase = P_OVER; else NewRound(); }
}

// ---------------------------------------------------------------- --scuffle-sim <players> <rounds> [arsenal]
int RunScuffleSim(int players, int rounds, int arsenal) {
    players = std::clamp(players, 2, MAX_STICKS); rounds = std::max(1, rounds);
    Match m; m.Start(players, 100000, 4242, arsenal);
    std::vector<uint32_t> rr(players); for (int i = 0; i < players; i++) rr[i] = 77 + i * 7919;
    std::map<std::string, int> causes; double total = 0; int done = 0, wallDeaths = 0, deaths = 0, draws = 0;
    int lastRound = m.round;
    while (done < rounds) {
        for (int i = 0; i < players; i++) BotInput(m.w, i, m.w.sticks[i].in, rr[i], 2 - i % 3 / 2);
        bool fighting = m.phase == Match::P_FIGHT;
        float tRound = m.w.t;
        m.Step();
        if (fighting && m.phase == Match::P_WIN) {
            done++; total += tRound; draws += m.roundWinner < 0;
            for (const auto& k : m.w.sticks) if (!k.alive) { causes[k.cause]++; deaths++; wallDeaths += k.cause == "the wall"; }
        }
        if (m.round != lastRound) lastRound = m.round;
    }
    printf("scuffle-sim: %d rounds, %d sticks, %s arsenal: %.1f s a round, %.1f%% draws, the wall %.1f%% of deaths\n", rounds, players, ArsenalName(arsenal), total / done, 100.0 * draws / done, 100.0 * wallDeaths / std::max(1, deaths));
    std::vector<std::pair<int, std::string>> v; for (const auto& c : causes) v.push_back({c.second, c.first}); std::sort(v.rbegin(), v.rend());
    printf("  deaths by cause:"); for (const auto& c : v) printf(" %s %.0f%%;", c.second.c_str(), 100.0 * c.first / std::max(1, deaths)); printf("\n");
    for (int i = 0; i < players; i++) printf("  stick %d: %d rounds, %d points\n", i, m.wins[i], m.score[i]);
    return 0;
}

// ---------------------------------------------------------------- the stage-2 checks (in --scuffle-test)
static int AF = 0;
static void AC(bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) AF++; }
static Stage FlatArena() { std::vector<std::string> rows(18, std::string(40, '.')); rows[17] = std::string(40, '#'); rows[16] = std::string(40, '#'); return StageFromText(rows, "Flat"); }
static void Settle(World& w, float s) { int n = (int)(s / STEP); for (int i = 0; i < n; i++) w.Step(); }
static int Give(World& w, Stick& k, const char* key) { int it = w.SpawnWeapon(WeaponIndex(key), k.pt[J_HAND_R].p, {0, 0}); w.Pickup(k, it); k.fireCool = 0; return it; }
int ScuffleArmsChecks() {
    AF = 0;
    printf("Scuffle: stage 2 (crates, the first twelve weapons, the wall, matches)\n");
    int on = 0; for (const auto& d : Weapons()) on += d.stage <= 2;
    AC(Weapons().size() == 48 && on == 12, TextFormat("48 weapons in scuffle_weapons.json, %d in the crates at stage 2", on));
    // a crate on its parachute: slow, then it opens on touch into a weapon in the hand
    { World w; w.Init(FlatArena(), 1, 3); w.nextCrate = 1e9f; Stick& k = w.sticks[0]; k.pos = {6, 1.2f}; Settle(w, 0.5f);
      int c = w.DropCrate(10.0f); float y0 = w.items[c].a.p.y; Settle(w, 1.0f); float fell = y0 - w.items[c].a.p.y;
      AC(fell > 1.5f && fell < 2.6f, TextFormat("a crate on its parachute falls about 2 m a second (%.1f)", fell));
      for (int i = 0; i < 1200 && k.weapon < 0; i++) { k.in.moveX = w.items[c].a.p.x > k.pos.x ? 1.0f : -1.0f; w.Step(); }
      AC(k.weapon >= 0 && w.items[k.weapon].weapon >= 0 && Weapons()[w.items[k.weapon].weapon].stage <= 6, TextFormat("it opens on touch: %s, in hand", k.weapon >= 0 ? Weapons()[w.items[k.weapon].weapon].name.c_str() : "nothing")); }
    // guns: damage, the headshot, knockback, recoil, pierce, explosions
    auto duel = [&](const char* key, float gap, float bh, std::function<void(World&, Stick&, Stick&)> after) {
        World w; w.Init(FlatArena(), 2, 9); w.nextCrate = 1e9f; Stick& a = w.sticks[0]; Stick& b = w.sticks[1]; a.pos = {5, 1.2f}; b.pos = {5 + gap, 1.2f}; Settle(w, 0.6f);
        Give(w, a, key); a.in.aim = Vector2Normalize(Vector2Subtract({b.pt[J_NECK].p.x, b.pt[J_NECK].p.y + bh}, a.pt[J_NECK].p)); a.in.fire = true; w.Step(); a.in.fire = false;
        Settle(w, 1.2f); after(w, a, b);
    };
    duel("gaspistol", 4, 0, [&](World&, Stick&, Stick& b) { AC(b.hp == 75, TextFormat("a gas pistol: 25 (hp %.0f)", b.hp)); });
    duel("carbine", 6, 0.22f, [&](World&, Stick&, Stick& b) { AC(b.hp == 20, TextFormat("a carbine to the head: 80 (hp %.0f)", b.hp)); });
    { World w; w.Init(FlatArena(), 2, 9); w.nextCrate = 1e9f; Stick& a = w.sticks[0]; Stick& b = w.sticks[1]; a.pos = {5, 1.2f}; b.pos = {7, 1.2f}; Settle(w, 0.6f); float bx = b.pos.x;
      Give(w, a, "scatter"); a.in.aim = {1, 0.05f}; a.in.fire = true; w.Step(); a.in.fire = false; float far = bx; for (int i = 0; i < 300; i++) { w.Step(); far = std::max(far, b.pt[J_PELVIS].p.x); }
      AC(far - bx > 4 && b.hp < 100, TextFormat("the scatter gun: a blast and a long flight (%.1f m)", far - bx)); }
    { World w; w.Init(FlatArena(), 4, 9); w.nextCrate = 1e9f; for (int i = 0; i < 4; i++) w.sticks[i].pos = {4.0f + i * 2.2f, 1.2f}; Settle(w, 0.6f);
      Stick& a = w.sticks[0]; Give(w, a, "sniper"); a.in.aim = Vector2Normalize(Vector2Subtract(w.sticks[1].pt[J_NECK].p, a.pt[J_NECK].p)); a.in.fire = true; w.Step(); a.in.fire = false; Settle(w, 0.9f);   // (the scope: the shot leaves 0.5 s after the press)
      int hit = 0; for (int i = 1; i < 4; i++) hit += w.sticks[i].hp < 100; AC(hit == 3, TextFormat("the sniper: through three sticks in a line (%d)", hit)); }
    { World w; w.Init(FlatArena(), 3, 9); w.nextCrate = 1e9f; Stick& a = w.sticks[0]; a.pos = {4, 1.2f}; w.sticks[1].pos = {12, 1.2f}; w.sticks[2].pos = {13, 1.2f}; Settle(w, 0.6f);
      Give(w, a, "rocket"); a.in.aim = Vector2Normalize(Vector2Subtract(w.sticks[1].pt[J_PELVIS].p, a.pt[J_NECK].p)); a.in.fire = true; w.Step(); a.in.fire = false; Settle(w, 1.0f);
      AC(w.sticks[1].hp < 50 && w.sticks[2].hp < 100, TextFormat("a rocket: 90 in an area (%.0f, %.0f)", w.sticks[1].hp, w.sticks[2].hp)); }
    { World w; w.Init(FlatArena(), 2, 9); w.nextCrate = 1e9f; Stick& a = w.sticks[0]; Stick& b = w.sticks[1]; a.pos = {4, 1.2f}; b.pos = {9, 1.2f}; Settle(w, 0.6f);
      Give(w, a, "grenadelauncher"); a.in.aim = Vector2Normalize({1, 0.6f}); a.in.fire = true; w.Step(); a.in.fire = false;
      bool boom = false; for (int i = 0; i < 400 && !boom; i++) { w.Step(); for (const auto& e : w.events) boom |= e.kind == EV_EXPLODE; }
      AC(boom, "a grenade: bounces, fizzes, goes off"); }
    { World w; w.Init(FlatArena(), 1, 9); w.nextCrate = 1e9f; Stick& a = w.sticks[0]; a.pos = {12, 1.2f}; Settle(w, 0.6f); float x0 = a.pos.x;
      Give(w, a, "minigun"); for (int i = 0; i < 240; i++) { a.in.aim = {1, 0}; a.in.fire = true; w.Step(); }
      AC(a.pos.x < x0 - 0.8f, TextFormat("the minigun pushes you backward (%.1f m)", x0 - a.pos.x)); }
    // the block: a punch in time sends the bullet back; the frying pan stops everything
    { World w; w.Init(FlatArena(), 2, 9); w.nextCrate = 1e9f; Stick& a = w.sticks[0]; Stick& b = w.sticks[1]; a.pos = {4, 1.2f}; b.pos = {10, 1.2f}; Settle(w, 0.6f);
      Give(w, a, "gaspistol"); a.in.aim = Vector2Normalize(Vector2Subtract(b.pt[J_NECK].p, a.pt[J_NECK].p)); a.in.fire = true; w.Step(); a.in.fire = false;
      bool blocked = false;
      for (int i = 0; i < 120 && !blocked; i++) {
          for (const auto& bl : w.bullets) if (Vector2Distance(bl.p, b.pt[J_HAND_R].p) < 0.9f && !b.fireWas) { b.in.aim = Vector2Normalize(Vector2Subtract(a.pt[J_NECK].p, b.pt[J_NECK].p)); b.in.fire = true; }
          w.Step(); b.in.fire = false;
          for (const auto& e : w.events) blocked |= e.kind == EV_BLOCK;
      }
      Settle(w, 0.6f);
      AC(blocked && b.hp == 100 && a.hp < 100, TextFormat("a punch in the last 0.15 s deflects the bullet back at the shooter (%.0f, %.0f)", a.hp, b.hp)); }
    { World w; w.Init(FlatArena(), 2, 9); w.nextCrate = 1e9f; Stick& a = w.sticks[0]; Stick& b = w.sticks[1]; a.pos = {4, 1.2f}; b.pos = {10, 1.2f}; b.face = -1; Settle(w, 0.6f);
      Give(w, b, "fryingpan"); b.in.aim = {-1, 0.25f};
      Give(w, a, "gaspistol"); a.in.aim = Vector2Normalize(Vector2Subtract(b.pt[J_HAND_R].p, a.pt[J_NECK].p)); a.in.fire = true; w.Step(); a.in.fire = false;
      for (int i = 0; i < 120; i++) { b.in.aim = {-1, 0.25f}; w.Step(); }
      AC(b.hp == 100, TextFormat("the frying pan blocks the shot (hp %.0f)", b.hp)); }
    { World w; w.Init(FlatArena(), 2, 9); w.nextCrate = 1e9f; Stick& a = w.sticks[0]; Stick& b = w.sticks[1]; a.pos = {4, 1.2f}; b.pos = {7, 1.2f}; Settle(w, 0.6f);
      int it = Give(w, a, "gaspistol"); w.items[it].ammo = 0; a.in.aim = Vector2Normalize(Vector2Subtract(b.pt[J_NECK].p, a.pt[J_NECK].p)); a.in.fire = true; w.Step(); a.in.fire = false; Settle(w, 1.0f);
      AC(a.weapon < 0 && b.hp == 90, TextFormat("an empty gun is thrown (10; hp %.0f)", b.hp)); }
    { World w; w.Init(FlatArena(), 2, 9); w.nextCrate = 1e9f; Stick& a = w.sticks[0]; Stick& b = w.sticks[1]; a.pos = {4, 1.2f}; b.pos = {4.9f, 1.2f}; Settle(w, 0.6f);
      Give(w, a, "cutlass"); a.in.aim = {1, 0.1f}; a.in.fire = true; w.Step(); a.in.fire = false; Settle(w, 0.6f);
      AC(b.hp == 65, TextFormat("a cutlass: 35 (hp %.0f)", b.hp)); }
    // the wall: the bulkheads flood from the bottom at 45 s
    { World w; w.Init(FlatArena(), 2, 9); w.nextCrate = 1e9f; w.sticks[0].pos = {4, 1.2f}; w.sticks[1].pos = {12, 1.2f};
      for (int i = 0; i < (int)(44 / STEP); i++) w.Step(); bool before = w.sticks[0].alive && w.wallY < 0;
      for (int i = 0; i < (int)(9 / STEP); i++) w.Step();
      AC(before && !w.sticks[0].alive && w.sticks[0].cause == "the wall", TextFormat("at 45 s the wall comes in: the bulkheads flood (%.1f m by 53 s)", w.wallY)); }
    // the gate: four bots finish a ten-round match
    { Match m; m.Start(4, 10, 777, AR_CLASSIC); std::vector<uint32_t> rr = {11, 22, 33, 44}; int steps = 0;
      while (!m.Over() && steps < (int)(3600 / STEP)) { for (int i = 0; i < 4; i++) BotInput(m.w, i, m.w.sticks[i].in, rr[i], 2); m.Step(); steps++; }
      int best = 0; for (int i = 1; i < 4; i++) if (m.wins[i] > m.wins[best]) best = i;
      AC(m.Over() && m.champion == best && m.wins[best] == 10, TextFormat("four bots finish a 10-round match: %d rounds, %d draws, %.0f s a round, the winner %d points", m.round, m.draws, steps * STEP / std::max(1, m.round), m.score[m.champion >= 0 ? m.champion : 0])); }
    return AF;
}

} // namespace sf
