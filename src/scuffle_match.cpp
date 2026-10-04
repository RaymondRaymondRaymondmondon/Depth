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
    finales = custom.empty() ? FinalePlaylist(world) : std::vector<Stage>{};
    // the rotation: shuffled by the seed (no stage twice until the list runs out)
    uint32_t r = seed;
    for (int i = (int)playlist.size() - 1; i > 0; i--) { r = r * 1664525u + 1013904223u; int j = (int)((r >> 8) % (uint32_t)(i + 1)); std::swap(playlist[i], playlist[j]); }
    NewRound();
}
void Match::NewRound() {
    round++;
    bool matchPoint = false; for (int x : wins) matchPoint |= x >= toWin - 1;
    stageIdx = (round - 1) % std::max(1, (int)playlist.size());
    bool fin = matchPoint && toWin > 1;
    uint32_t rs = seed * 2654435761u + round * 7919u;
    if (world == WD_COUNT && custom.empty()) {   // (endless: a fresh stage from the generator each round, any world)
        Stage g = GenerateStage((int)(rs % WD_COUNT), rs, fin);
        w.Init(g, players, rs);
    }
    else if (fin && !finales.empty()) w.Init(finales[(rs >> 8) % finales.size()], players, rs);   // (match point: a finale stage, the wall at 30 s)
    else w.Init(playlist[stageIdx], players, rs);
    w.arsenal = arsenal; w.finale = fin;
    phase = P_COUNT; phaseT = 1.0f; roundWinner = -1;
    std::fill(roundKills.begin(), roundKills.end(), 0);
    evSeen = w.eventBase;
}
void Match::Step() {
    if (phase == P_OVER) return;
    if (phase == P_COUNT) { for (auto& k : w.sticks) k.in = Input{}; }
    w.Step();
    // the round's scoring from the world's log: kills (and their bonuses), the wall
    const auto& E = w.events; uint32_t base = w.eventBase; if (evSeen < base) evSeen = base;
    for (uint32_t i = evSeen; i < base + E.size(); i++) {
        const Event& e = E[i - base];
        if (e.kind == EV_DIE && e.by >= 0 && e.by < players && e.by != e.who && phase == P_FIGHT) { score[e.by] += 20 + (e.a == 1 ? 10 : e.a == 2 ? 20 : 0); roundKills[e.by]++; }
        if (e.kind == EV_WALL) for (const auto& k : w.sticks) if (k.alive && k.present && k.id < players) score[k.id] += 10;
    }
    evSeen = base + (uint32_t)E.size();
    if (phase == P_COUNT && (phaseT -= STEP) <= 0) phase = P_FIGHT;
    else if (phase == P_FIGHT && (w.Living() <= 1 || w.t > 95)) {
        phase = P_WIN; phaseT = 1.5f; roundWinner = -1;
        if (w.Living() == 1) for (const auto& k : w.sticks) if (k.alive && k.present) roundWinner = k.id;
        if (roundWinner >= 0) { wins[roundWinner]++; score[roundWinner] += 100; log.push_back(TextFormat("Round %d to stick %d (%.0f s)", round, roundWinner, w.t)); }
        else { draws++; log.push_back(TextFormat("Round %d: a draw (%.0f s)", round, w.t)); }
        for (int i = 0; i < players; i++) if (wins[i] >= toWin) { champion = i; score[i] += 300; }
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
      Stick& a = w.sticks[0]; Give(w, a, "sniper"); a.in.aim = Vector2Normalize(Vector2Subtract(w.sticks[1].pt[J_NECK].p, a.pt[J_NECK].p)); a.in.fire = true; w.Step(); a.in.fire = false; Settle(w, 0.5f);
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
