// Scuffle stage 7: the modes (doc pp. 15-16). Headless.
//   Scuffle (classic): last stick standing. Teams: 2v2, 3v3, 4v4 or 2v2v2v2, friendly fire on (a lobby toggle); the last
//   team standing. King of the Plank: a marked platform scores a point a second to whoever stands on it alone; first to
//   60; the plank moves every 20 s; the dead come back after 3 s. The Egg: hold the egg (it breaks if it falls from
//   height) for 30 s in all; the holder can only punch. Hot Potato: a bomb passes by touch and goes off at 10 s, and
//   another comes; the last stick wins. Hunt: one stick is the Shark (200 HP, a harpoon, no crates); a shark's kill
//   makes a shark; the sharks win by eating everyone before the wall. Duel: two sticks, best of 7 on finale stages, each
//   picking a weapon from the same three before the round. Chaos: a random mutator, a random arsenal and an event every
//   round. Custom: whatever the lobby sets (mutators, arsenal, playlist, score, the wall). The Gauntlet (co-op): a run of
//   twenty generated stages with no other sticks to fight, each worse than the last, an exit to reach before the clock.
#include "scuffle.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>

namespace sf {

const char* ModeName(int m) {
    static const char* N[MD_COUNT] = {"Classic", "Teams", "King of the Plank", "The Egg", "Hot Potato", "Hunt", "Duel", "Chaos", "Custom", "The Gauntlet"};
    return m >= 0 && m < MD_COUNT ? N[m] : "?";
}
const char* ModeRule(int m) {
    static const char* N[MD_COUNT] = {"Last stick standing.", "The last team standing (friendly fire on, unless the lobby says no).",
                                      "Stand on the marked plank alone: a point a second; first to 60. The plank moves every 20 s.",
                                      "Hold the egg 30 s in all (it breaks if it falls from height); the holder can only punch.",
                                      "A bomb passes by touch and goes off at 10 s; then another. The last stick wins.",
                                      "One Shark (200 HP, a harpoon, no crates) eats everyone before the wall; a shark's kill makes a shark.",
                                      "Two sticks, best of 7 on finale stages; each picks a weapon from the same three.",
                                      "A random mutator, a random arsenal and an event every round.", "Whatever the lobby sets.",
                                      "Co-op: twenty generated stages, worse each time; reach the exit before the clock."};
    return m >= 0 && m < MD_COUNT ? N[m] : "?";
}

// ---------------------------------------------------------------- the world's side
void World::Respawn(Stick& k) {
    // back at a spawn (the gauntlet's start; otherwise the spawn farthest from the living), keeping who you are
    int id = k.id, team = k.team, tk = k.trinket, pe = k.persona; bool shark = k.shark; float size = k.size;
    Vector2 at = stage.spawns.empty() ? Vector2{stage.Width() / 2, stage.Height() / 2} : stage.spawns[0];
    if (mode == MD_GAUNTLET && start >= 0 && start < (int)stage.spawns.size()) at = stage.spawns[start];
    else if (!stage.spawns.empty()) {
        float best = -1;
        for (const auto& sp : stage.spawns) { float d = 1e9f; for (const auto& o : sticks) if (o.alive && o.present && o.id != id) d = std::min(d, Vector2Distance(o.pos, sp)); if (d > best) { best = d; at = sp; } }
    }
    SpawnStick(k, at, at.x < stage.Width() / 2 ? 1 : -1);
    k.id = id; k.team = team; k.trinket = tk; k.persona = pe; k.shark = shark;
    Resize(k, size);
    k.steadyT = 1.0f;
    if (k.shark) { k.hp = 200; int it = SpawnWeapon(WeaponIndex("harpoon"), k.pt[J_HAND_R].p, {0, 0}); Pickup(k, it); items[it].ammo = 99; }
    Emit(EV_PORTAL, at, id);
}
void World::MovePlank() {
    // a stretch of three or more tiles with room above, somewhere the sticks can stand; never the same place twice running
    std::vector<Rectangle> runs;
    for (int y = 1; y < stage.h - 3; y++) for (int x = 1; x < stage.w - 3; x++) {
        if (!stage.Solid(x, y - 1) || stage.Solid(x, y) || stage.Solid(x, y + 1) || stage.Solid(x, y + 2)) continue;
        int len = 0; while (x + len < stage.w - 1 && len < 5 && stage.Solid(x + len, y - 1) && !stage.Solid(x + len, y) && !stage.Solid(x + len, y + 1) && !stage.Solid(x + len, y + 2)) len++;
        if (len >= 3) { runs.push_back({(float)x, (float)y, (float)len, 1}); x += len; }
    }
    if (runs.empty()) { plank = {(float)stage.w / 2 - 2, 2, 4, 1}; return; }
    Rectangle pick = runs[(int)(Rand() * runs.size()) % runs.size()];
    for (int tries = 0; tries < 6 && runs.size() > 1 && pick.x == plank.x && pick.y == plank.y; tries++) pick = runs[(int)(Rand() * runs.size()) % runs.size()];
    plank = pick; plankT = 20;
    Emit(EV_EVENT, {(plank.x + plank.width / 2) * TILE, plank.y * TILE}, -1, -1, 200);
}
void World::StepMode() {
    const float dt = STEP;
    if ((int)pts.size() != (int)sticks.size()) pts.assign(sticks.size(), 0);
    bool respawns = mode == MD_KING || mode == MD_EGG || mode == MD_GAUNTLET || mode == MD_HUNT;
    for (auto& k : sticks) {
        if (k.alive || !respawns) continue;
        if (mode == MD_HUNT && !k.shark) {   // (eaten by a shark: you come back as one; killed otherwise, you're out)
            if (k.respawnT == 0 && k.lastHitBy >= 0 && k.lastHitBy < (int)sticks.size() && sticks[k.lastHitBy].shark) { k.respawnT = 1.5f; k.shark = true; }
        } else if (mode == MD_HUNT && k.respawnT <= 0) continue;   // (a dead shark stays dead; a new one is on its way back)
        else if (k.respawnT == 0) k.respawnT = mode == MD_GAUNTLET ? 1.5f : 3.0f;
        if (k.respawnT > 0) { k.respawnT -= dt; if (k.respawnT <= 0) { k.respawnT = 0; Respawn(k); } }
    }
    if (mode == MD_KING) {
        if ((plankT -= dt) <= 0) MovePlank();
        int on = -1, n = 0;
        for (const auto& k : sticks) if (k.alive && k.present && k.grounded && k.pos.x > plank.x * TILE && k.pos.x < (plank.x + plank.width) * TILE && fabsf(k.pos.y - plank.y * TILE) < 0.2f) { on = k.id; n++; }
        if (n == 1) pts[on] += dt;
    }
    if (mode == MD_EGG) {
        int egg = -1; for (int i = 0; i < (int)things.size(); i++) if (things[i].kind == TH_EGG) egg = i;
        // (the egg sits on the highest floor under the middle of the stage: it never starts with a fall)
        auto nest = [&]() { int cx = stage.w / 2; for (int dx = 0; dx < stage.w / 2; dx++) for (int s : {1, -1}) { int x = cx + dx * s; for (int y = stage.h - 2; y >= 1; y--) if (stage.Solid(x, y - 1) && !stage.Solid(x, y) && !stage.Solid(x, y + 1)) return Vector2{(x + 0.5f) * TILE, y * TILE + 0.16f}; } return Vector2{stage.Width() / 2, stage.Height() / 2}; };
        if (egg < 0) { egg = AddThing(TH_EGG, nest(), {}, 1e9f, -1); }
        Thing& e = things[egg];
        if (e.a > 0) { e.a -= dt; if (e.a <= 0) { e.p = nest(); e.v = {0, 0}; e.hold = -1; } }   // (broken: a new one after 2 s)
        else if (e.hold >= 0) {
            Stick& h = sticks[e.hold];
            if (!h.alive || !h.present || h.st == S_RAGDOLL) { e.v = h.vel; e.hold = -1; e.t = 0; }   // (dropped)
            else { e.p = Vector2Add(h.pt[J_HAND_L].p, {0, 0.12f}); pts[h.id] += dt; }
        } else {
            e.v.y -= gravity * dt; Vector2 np = Vector2Add(e.p, Vector2Scale(e.v, dt));
            if (stage.Solid((int)floorf(np.x / TILE), (int)floorf((np.y - 0.15f) / TILE))) {
                if (e.v.y < -9) { e.a = 2; e.p = {-100, -100}; Emit(EV_EXPLODE, np, -1, -1, 0.2f); }   // (it breaks)
                else { np.y = (floorf((np.y - 0.15f) / TILE) + 1) * TILE + 0.15f; e.v.y = 0; e.v.x *= 0.8f; }
            }
            if (stage.Solid((int)floorf((np.x + (e.v.x > 0 ? 0.15f : -0.15f)) / TILE), (int)floorf(np.y / TILE))) { e.v.x = -e.v.x * 0.5f; np.x = e.p.x; }
            if (e.a <= 0) e.p = np;
            if (e.p.y < -6) { e.a = 2; e.p = {-100, -100}; }
            e.t += dt;
            if (e.a <= 0 && e.t > 0.4f) for (auto& k : sticks) if (k.alive && k.present && k.st != S_RAGDOLL && fabsf(k.pt[J_PELVIS].p.x - e.p.x) < 0.5f && e.p.y > k.pos.y - 0.3f && e.p.y < k.pt[J_NECK].p.y + 0.2f) {   // (anywhere along the body: it sits at your feet)
                e.hold = k.id; if (k.weapon >= 0) DropWeapon(k, {0, 2}, false); Emit(EV_PICKUP, e.p, k.id); break;
            }
        }
    }
    if (mode == MD_GAUNTLET) {
        for (auto& k : sticks) if (k.alive && k.present && k.finished < 0 && Vector2Distance(k.pt[J_PELVIS].p, Vector2Add(goal, {0, 0.9f})) < 0.9f) { k.finished = t; Emit(EV_EVENT, goal, k.id, -1, 300); }
        for (auto& k : sticks) if (k.finished >= 0) { k.vel = {0, 0}; k.in = Input{}; }   // (through the exit: you wait for the others there)
    }
}

// ---------------------------------------------------------------- the match's side
int Match::TeamOf(int i) const { return mode == MD_TEAMS ? i / std::max(1, teamSize) : mode == MD_GAUNTLET ? 0 : -1; }
bool Match::RoundOver(int* winner) {
    *winner = -1;
    auto credit = [&](int i) { if (i >= 0 && i < players) { wins[i]++; score[i] += 100 + (i < (int)trinkets.size() && trinkets[i] == TK_NONE ? 10 : 0); } };
    switch (mode) {
    case MD_TEAMS: {
        int teamAlive = -1, teams = 0;
        { std::vector<int> seen; for (const auto& k : w.sticks) if (k.alive && k.present && std::find(seen.begin(), seen.end(), k.team) == seen.end()) seen.push_back(k.team); teams = (int)seen.size(); if (teams == 1) teamAlive = seen[0]; }
        if (teams > 1 && w.t <= 95) return false;
        if (teams == 1) { for (int i = 0; i < players; i++) if (TeamOf(i) == teamAlive) credit(i); *winner = -2; log.push_back(TextFormat("Round %d to team %d", round, teamAlive + 1)); }
        else { draws++; log.push_back(TextFormat("Round %d: a draw", round)); }
        return true;
    }
    case MD_KING: case MD_EGG: {
        for (int i = 0; i < players && i < (int)w.pts.size(); i++) if (w.pts[i] >= target) { *winner = i; wins[i] = toWin; score[i] += 300; log.push_back(TextFormat("%s to stick %d", ModeName(mode), i)); return true; }
        return false;
    }
    case MD_HUNT: {
        int survivors = 0, sharks = 0;
        for (const auto& k : w.sticks) { if (!k.present) continue; if (k.shark && (k.alive || k.respawnT > 0)) sharks++; else if (!k.shark && k.alive) survivors++; }
        bool wall = w.wallOn && w.t >= (w.finale ? Arms().finaleWall : Arms().wallStart);
        if (survivors > 0 && sharks > 0 && !wall && w.t <= 95) return false;
        if (survivors == 0) { for (int i = 0; i < players; i++) if (w.sticks[i].shark) credit(i); *winner = -2; log.push_back(TextFormat("Round %d to the sharks", round)); }
        else { for (int i = 0; i < players; i++) if (!w.sticks[i].shark && w.sticks[i].alive) credit(i); *winner = -2; log.push_back(TextFormat("Round %d to the survivors", round)); }
        return true;
    }
    case MD_GAUNTLET: {
        float limit = std::max(25.0f, 45.0f - gStage);
        int racing = 0, done = 0; for (const auto& k : w.sticks) if (k.present) { racing++; done += k.finished >= 0; }
        if (done < racing && w.t < limit) return false;
        if (done > 0) { gStage++; float last = 0; for (const auto& k : w.sticks) if (k.finished >= 0) last = std::max(last, k.finished); gTime += last; for (int i = 0; i < players; i++) if (w.sticks[i].finished >= 0) credit(i); *winner = -2; log.push_back(TextFormat("Gauntlet stage %d cleared in %.1f s", gStage, last)); }
        else { gFailed = true; log.push_back(TextFormat("The Gauntlet ends at stage %d (out of time)", gStage + 1)); }
        if (gFailed || gStage >= 20) { champion = 0; }
        return true;
    }
    default: {   // (classic, potato, duel, chaos, custom: the last stick standing)
        if (w.Living() > 1 && w.t <= 95) return false;
        if (w.Living() == 1) for (const auto& k : w.sticks) if (k.alive && k.present) *winner = k.id;
        if (*winner >= 0) { credit(*winner); log.push_back(TextFormat("Round %d to stick %d (%.0f s)", round, *winner, w.t)); }
        else { draws++; log.push_back(TextFormat("Round %d: a draw (%.0f s)", round, w.t)); }
        return true;
    }
    }
}

// the Gauntlet's stages: generated, an exit at the spawn farthest from the start, more hazards each time
Stage GauntletStage(int world, int n, uint32_t seed) {
    Stage s = GenerateStage(world, seed + n * 7919u, false, nullptr, 1 + n / 3);
    s.name = TextFormat("The Gauntlet %d: %s", n + 1, s.name.c_str());
    return s;
}

// ---------------------------------------------------------------- the stage-7 checks (in --scuffle-test): every mode finishes with bots
static int MF = 0;
static void MC(bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) MF++; }
int ScuffleModeChecks() {
    MF = 0;
    printf("Scuffle: stage 7 (the modes: each finishes with bots)\n");
    struct Run { int mode, players, toWin; };
    static const Run RUNS[] = {{MD_CLASSIC, 4, 3}, {MD_TEAMS, 4, 3}, {MD_KING, 4, 1}, {MD_EGG, 4, 1}, {MD_POTATO, 4, 2}, {MD_HUNT, 5, 2}, {MD_DUEL, 2, 4}, {MD_CHAOS, 4, 2}, {MD_CUSTOM, 4, 2}, {MD_GAUNTLET, 2, 1}};
    for (const auto& r : RUNS) {
        Match m; m.mode = r.mode; if (r.mode == MD_CUSTOM) { m.mutators = (1u << MU_LOW_GRAVITY) | (1u << MU_RICOCHET); m.wallOn = false; }
        if (r.mode == MD_GAUNTLET) m.gWorld = WD_CAVE;
        m.Start(r.players, r.toWin, 4242 + r.mode, r.mode == MD_CHAOS ? AR_RANDOM : AR_CLASSIC);
        std::vector<uint32_t> rr(r.players); for (int i = 0; i < r.players; i++) rr[i] = 77 + i * 7919 + r.mode;
        float simT = 0; int rounds = 0, lastRound = m.round; bool teamsOk = true, events = r.mode != MD_CHAOS;
        for (int f = 0; f < 120 * 60 * 12 && !m.Over(); f++) {
            for (int i = 0; i < r.players; i++) BotInput(m.w, i, m.w.sticks[i].in, rr[i], 2);
            m.Step(); simT += STEP;
            if (m.round != lastRound) { lastRound = m.round; rounds++; }
            if (r.mode == MD_TEAMS) for (const auto& k : m.w.sticks) teamsOk &= k.team == k.id / 2;
            if (r.mode == MD_CHAOS && m.w.event >= 0) events = true;
        }
        std::string extra;
        if (r.mode == MD_KING || r.mode == MD_EGG) { float best = 0; for (float p : m.w.pts) best = std::max(best, p); extra = TextFormat(", the winner on %.0f", best); }
        if (r.mode == MD_GAUNTLET) extra = TextFormat(", %d stages cleared in %.0f s", m.gStage, m.gTime);
        if (r.mode == MD_HUNT) { std::string l = m.log.empty() ? "" : m.log.back(); extra = ", " + l; }
        MC(m.Over() && teamsOk && events, TextFormat("%s finishes with bots (%d players, %.0f s of play, %d rounds%s)", ModeName(r.mode), r.players, simT, rounds + 1, extra.c_str()));
    }
    // the rules each mode adds
    { Match m; m.mode = MD_TEAMS; m.friendlyFire = false; m.Start(4, 3, 9); World& w = m.w; m.phase = Match::P_FIGHT; float hp = w.sticks[1].hp; w.Hit(w.sticks[1], 0, 30, {1, 0}, 3, false, "a test"); float enemy = w.sticks[2].hp; w.Hit(w.sticks[2], 0, 30, {1, 0}, 3, false, "a test");
      MC(w.sticks[1].hp == hp && w.sticks[2].hp < enemy, "Teams: friendly fire off spares a teammate, not an enemy"); }
    { Match m; m.mode = MD_KING; m.Start(2, 1, 11); World& w = m.w; m.phase = Match::P_FIGHT;
      Vector2 at{(w.plank.x + w.plank.width / 2) * TILE, w.plank.y * TILE}; w.SpawnStick(w.sticks[0], at, 1); w.SpawnStick(w.sticks[1], {at.x + 8, at.y + 4}, -1);
      for (int i = 0; i < 240; i++) { w.sticks[0].in = Input{}; w.sticks[1].in = Input{}; m.Step(); }
      MC(w.pts[0] > 1.5f && w.pts[1] == 0, TextFormat("King of the Plank: alone on the plank, a point a second (%.1f in 2 s)", w.pts[0])); }
    { Match m; m.mode = MD_HUNT; m.Start(3, 2, 13); World& w = m.w; int shark = -1; for (const auto& k : w.sticks) if (k.shark) shark = k.id;
      MC(shark >= 0 && w.sticks[shark].hp == 200 && w.sticks[shark].weapon >= 0, "Hunt: one Shark, 200 HP, a harpoon");
      int prey = (shark + 1) % 3; m.phase = Match::P_FIGHT; w.Hit(w.sticks[prey], shark, 200, {1, 0}, 3, false, "a test");
      for (int i = 0; i < 240; i++) m.Step();
      MC(w.sticks[prey].alive && w.sticks[prey].shark, "and a shark's kill makes a shark"); }
    { Match m; m.mode = MD_DUEL; m.Start(2, 4, 15);
      MC(m.duelOffer[0] >= 0 && m.duelOffer[1] >= 0 && m.duelOffer[2] >= 0 && m.w.stage.finale, "Duel: a finale stage and three weapons offered");
      m.w.sticks[0].in.pick = 2; for (int i = 0; i < 120 * 5 && m.phase == Match::P_COUNT; i++) { m.w.sticks[0].in.pick = 2; m.Step(); }
      MC(m.w.sticks[0].weapon >= 0 && m.w.items[m.w.sticks[0].weapon].weapon == m.duelOffer[1], TextFormat("and the round starts with each stick's pick (%s)", m.w.sticks[0].weapon >= 0 ? Weapons()[m.w.items[m.w.sticks[0].weapon].weapon].name.c_str() : "nothing")); }
    printf(MF ? "  stage 7: %d FAILED\n" : "  stage 7: all checks passed\n", MF);
    return MF;
}

} // namespace sf
