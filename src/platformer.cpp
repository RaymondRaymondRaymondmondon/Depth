// ============================================================================
//  DEPTH - the platform levels: the Pipes, the Hull and the Pirate Ship.
//
//  In the spirit of Super Meat Boy, the challenge is the jumping itself: quick
//  acceleration, a jump whose height depends on how long you hold the button,
//  wall slides and wall jumps. Deaths are instant and so are restarts.
//  The Pipes have no enemies. The Hull and the Pirate Ship add enemies as an
//  extra hazard: touching one is deadly, and only the bosses can be stomped.
//
//  Levels are stitched from hand-built sections (24 x 16 tiles). Legend:
//    #  wall / floor        x  floor hazard (steam, urchins, spikes)
//    g  spinning hazard     t  timed jet (solid; fires upward 3 tiles)
//    o  coin                S  start          E  exit
//    k  crate or barrel (solid)   =  |  pipes, or yards and masts on the ship
//    c  crab   e  leaping eel   p  parakeet
//    P  pirate who bursts out of a door to stab   G  pirate who shoots from cover
//    K  the Kraken   B  Blackbeard
//  A death sends you back to the very start, unless checkpoints are switched
//  on (at the cost of the relic). Normal difficulty leaves out the gears,
//  mines, spiked balls and jets; Hard keeps them all.
// ============================================================================
#include "game.h"
#include "relics.h"
#include "levelgen.h"
#include "ik.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <chrono>
#include <cstdio>
#include <queue>
#include <unordered_set>

namespace {
constexpr int T = 32, CH_W = 24, CH_H = 16;
constexpr float PW = 20, PH = 26;
// Quick to reach full speed and quick to stop, so the diver goes exactly where the keys say.
constexpr float RUN = kin::RUN, ACCEL_GROUND = 6500, DECEL_GROUND = 7500, ACCEL_AIR = 4200, DECEL_AIR = 2600;
constexpr float JUMP_V = kin::JUMP_V, GRAV_UP = kin::GRAV_UP, GRAV_UP_RELEASED = kin::GRAV_UP_RELEASED, GRAV_DOWN = kin::GRAV_DOWN, MAX_FALL = kin::MAX_FALL;
constexpr float WALL_SLIDE = kin::WALL_SLIDE, WALLJUMP_VX = kin::WALLJUMP_VX, WALLJUMP_VY = kin::WALLJUMP_VY, WALL_LOCK = 0.13f;
constexpr float COYOTE = 0.1f, JUMP_BUFFER = 0.14f, STEP = 1.0f / 240; // physics runs at a fixed 240 Hz
// The world is drawn at exactly 1 canvas pixel per 2 world pixels, and the canvas is scaled up by exactly 2 (see
// PIXEL_W): so one world pixel is one screen pixel, every sprite is authored on a 2-world-pixel art grid, and
// nothing is ever stretched, filtered or drawn at a fractional offset.
constexpr float ZOOM = 0.5f, HUD_PX = 28; // canvas pixels per world pixel; HUD height in canvas pixels
bool gGhost = false; // the ghost-ship variant is being drawn: skeleton crew, teal fog
constexpr float ART = 2;                  // one art pixel, in world pixels

#define E "........................"
#define W "########################"
const char* HULL_ARENA[CH_H] = {
    "########################", E, E, E, E, E, E, E, E, E, E,
    "......###...##....###...",
    E,
    "...............K.....E..",
    "####..............######",
    "####..............######",
};
const char* HULL_ARENA_NOBOSS[CH_H] = { // the Kraken switched off: a quiet stretch of open water to the exit
    "########################", E, E, E, E, E, E, E, E, E, E,
    "......###...##....###...",
    E,
    ".......................E",
    "####..............######",
    "####..............######",
};

const char* CABIN_ARENA[] = { // Blackbeard's great cabin: charge him into a wall, then stomp him while he's dazed
    W, W, W,
    "##....................##",
    "##....................##",
    "##....................##",
    "##....................##",
    "##....................##",
    "##....................##",
    "##....................##",
    "##....................##",
    "##..===..........===..##",
    "......................##",
    "<.........B.........E.##",
    W, W, nullptr};
const char* CABIN_ARENA_NOBOSS[] = { // Blackbeard switched off: the cabin sits empty, treasure for the taking
    W, W, W,
    "##....................##",
    "##....................##",
    "##....................##",
    "##....................##",
    "##....................##",
    "##....................##",
    "##....................##",
    "##....................##",
    "##..===..........===..##",
    "......................##",
    "<...................E.##",
    W, W, nullptr};
#undef E
#undef W

// interior: the row where a section's below-decks interior starts (what's drawn behind it); kind 1 = hold, 2 = cabin
struct Part { const char* const* rows; int h; int interior = 999, kind = 0; };
Part P(const char* const* rows, int interior = 999, int kind = 0) { int h = 0; while (rows[h]) h++; return {rows, h, interior, kind}; } // null-terminated
Part P16(const char* const* rows) { return {rows, CH_H}; }
int EntryRow(const Part& s) { for (int r = 0; r < s.h; r++) if (s.rows[r][0] == '<') return r; return 13; }
int ExitRow(const Part& s) { for (int r = 0; r < s.h; r++) if (s.rows[r][CH_W - 1] == '>') return r; return 13; }

struct LevelDef {
    const char* name;
    Part last;            // the boss arena (Hull, Pirate Ship); unused by the Pipes, which end on an exit pipe
    Part lastNoBoss;      // used instead of `last` when the boss fight is switched off
    int coinValue, bonus;
    char fill;            // what's around the generated level: open water for the Hull, solid for the ship's hull below the arena
    bool dark;            // lit only by the diver's helmet lamp
    char fillAbove;       // what's above it: open sky over the pirate ship's deck
};
const LevelDef& Lv(int level) {
    static const std::vector<LevelDef> defs = [] {
        std::vector<LevelDef> d(PL_COUNT);
        d[PL_PIPES] = {"The Pipes", Part{}, Part{}, 0, 60, '#', true, '#'};
        d[PL_HULL] = {"The Hull", P16(HULL_ARENA), P16(HULL_ARENA_NOBOSS), 0, 120, '.', false, '.'};
        d[PL_PIRATE] = {"The Pirate Ship", P(CABIN_ARENA, 0, 2), P(CABIN_ARENA_NOBOSS, 0, 2), 0, 200, '#', false, '.'};
        d[PL_ISLAND] = {"The Island", Part{}, Part{}, 0, 240, '#', false, '.'}; // no boss arena yet - the crossing itself ends the level, like the Pipes
        d[PL_CAVE] = {"The Cave", Part{}, Part{}, 0, 280, '#', true, '#'}; // no boss arena yet; dark like the Pipes - lamp-lit only
        d[PL_WEEDS] = {"The Weeds", Part{}, Part{}, 0, 320, '#', false, '.'}; // a sunlit kelp forest; no boss arena
        d[PL_ATLANTIS] = {"Atlantis", Part{}, Part{}, 0, 360, '#', true, '.'}; // the drowned city: deep, lamp-lit, glyph-lit; no boss arena yet
        return d;
    }();
    return defs[level];
}
bool PlatHasArena(int level) { return level == PL_HULL || level == PL_PIRATE; } // only these two end in a boss arena; the rest end in an exit
float Rnd(float lo, float hi) { return lo + (hi - lo) * GetRandomValue(0, 10000) / 10000.0f; }

// ---------------------------------------------------------------- tiles and collision
char At(const PlatformState& p, int tx, int ty) {
    if (tx < 0 || tx >= p.w || ty < 0 || ty >= p.h) return '.';
    return p.tiles[ty][tx];
}

bool Solid(const PlatformState& p, int tx, int ty) {
    if (tx < 0 || tx >= p.w) return true; // level edges act as walls
    if (ty < 0 || ty >= p.h) return false;
    char c = p.tiles[ty][tx];
    return c == '#' || c == 't' || c == '=' || c == '|' || c == 'k' || c == 'f' || c == 'v' || c == 'b' || c == 's' || c == 'r' || c == 'R' || c == 'T' || c == 'N' || c == 'y' || c == 'D';
}

// Moves a box one axis at a time and pushes it out of solid tiles.
void MoveAndCollide(const PlatformState& p, Vector2& pos, Vector2& vel, float w, float h, float dt, bool& grounded, bool& hitWall) {
    grounded = false;
    hitWall = false;
    pos.x += vel.x * dt;
    int y0 = (int)floorf(pos.y / T), y1 = (int)floorf((pos.y + h - 0.01f) / T);
    if (vel.x > 0) {
        int tx = (int)floorf((pos.x + w - 0.01f) / T);
        for (int ty = y0; ty <= y1; ty++) if (Solid(p, tx, ty)) { pos.x = tx * (float)T - w; vel.x = 0; hitWall = true; break; }
    } else if (vel.x < 0) {
        int tx = (int)floorf(pos.x / T);
        for (int ty = y0; ty <= y1; ty++) if (Solid(p, tx, ty)) { pos.x = (tx + 1) * (float)T; vel.x = 0; hitWall = true; break; }
    }
    pos.y += vel.y * dt;
    int x0 = (int)floorf(pos.x / T), x1 = (int)floorf((pos.x + w - 0.01f) / T);
    if (vel.y > 0) {
        int ty = (int)floorf((pos.y + h - 0.01f) / T);
        for (int tx = x0; tx <= x1; tx++) if (Solid(p, tx, ty)) { pos.y = ty * (float)T - h; vel.y = 0; grounded = true; break; }
    } else if (vel.y < 0) {
        int ty = (int)floorf(pos.y / T);
        for (int tx = x0; tx <= x1; tx++) if (Solid(p, tx, ty)) { pos.y = (ty + 1) * (float)T; vel.y = 0; break; }
    }
}

bool TouchWall(const PlatformState& p, int side) {
    float x = side > 0 ? p.pos.x + PW + 0.5f : p.pos.x - 0.5f;
    int tx = (int)floorf(x / T);
    int y0 = (int)floorf((p.pos.y + 4) / T), y1 = (int)floorf((p.pos.y + PH - 4) / T);
    for (int ty = y0; ty <= y1; ty++) if (Solid(p, tx, ty)) return true;
    return false;
}

// Timed jets fire for 1.1 s out of every 2.4 s, staggered along the level.
float JetCycle(const PlatformState& p, int tx) { return fmodf(p.time + (tx % 5) * 0.5f, 2.4f); }
bool JetOn(const PlatformState& p, int tx) { return JetCycle(p, tx) < (p.hard ? 1.1f : 0.7f); }   // Normal: a shorter, gentler window, but the jet still fires

Rectangle PlayerBox(const PlatformState& p) { // sliding or rolling (poses 1 and 2), the diver is 12 px shorter, feet where they were
    float dh = (p.pose == 1 || p.pose == 2) ? 12.0f : 0.0f;
    return {p.pos.x + 3, p.pos.y + 3 + dh, PW - 6, PH - 5 - dh};
}

bool TouchesHazard(const PlatformState& p) {
    Rectangle pr = PlayerBox(p);
    int x0 = (int)floorf(pr.x / T), x1 = (int)floorf((pr.x + pr.width) / T);
    int y0 = (int)floorf(pr.y / T), y1 = (int)floorf((pr.y + pr.height) / T);
    for (int ty = y0; ty <= y1 + 3; ty++)
        for (int tx = x0; tx <= x1; tx++) {
            char c = At(p, tx, ty);
            if (c == 'x' && ty <= y1 && CheckCollisionRecs(pr, {tx * (float)T + 3, ty * (float)T + 12, T - 6.0f, T - 12.0f})) return true;
            if (c == 'g' && ty <= y1 && CheckCollisionCircleRec({tx * (float)T + 16, ty * (float)T + 16}, 13, pr)) return true;
            if (c == 'h' && ty <= y1 && CheckCollisionRecs(pr, {tx * (float)T, ty * (float)T, (float)T, 16.0f})) return true; // a low beam: slide under it
            if (c == 't' && JetOn(p, tx) && CheckCollisionRecs(pr, {tx * (float)T + 7, (ty - 3) * (float)T, T - 14.0f, 3.0f * T})) return true;
        }
    return false;
}

// ---------------------------------------------------------------- particles
void Burst(PlatformState& p, Vector2 at, int n, Color c, float speed, float life, float size) {
    for (int i = 0; i < n; i++) {
        float a = Rnd(0, 2 * PI), v = Rnd(speed * 0.3f, speed);
        p.particles.push_back({at, {cosf(a) * v, sinf(a) * v}, life, life, size, c});
    }
}

void Dust(PlatformState& p, Vector2 at, int n, float dirX) {
    for (int i = 0; i < n; i++)
        p.particles.push_back({{at.x + Rnd(-6, 6), at.y}, {dirX * Rnd(20, 90) + Rnd(-40, 40), Rnd(-70, -10)}, 0.35f, 0.35f, Rnd(2, 4), Color{220, 214, 200, 255}});
}

// Rising air: a negative size marks a bubble.
void Bubbles(PlatformState& p, Vector2 at, int n, float spread = 8) {
    if (p.verifying) return;
    for (int i = 0; i < n; i++)
        p.particles.push_back({{at.x + Rnd(-spread, spread), at.y + Rnd(-3, 3)}, {Rnd(-18, 18), Rnd(-70, -25)}, Rnd(0.6f, 1.3f), 1.3f, -Rnd(2, 4), Color{196, 236, 250, 255}});
}
// Picking up a coin: a spray of sparks, a rising ring of gold and a small glint.
void CoinPop(PlatformState& p, Vector2 at) {
    if (p.verifying) return;
    Burst(p, at, 8, Color{255, 220, 90, 255}, 120, 0.35f, 2);
    Burst(p, at, 4, Color{255, 250, 210, 255}, 60, 0.5f, 3);
    p.particles.push_back({at, {0, -40}, 0.5f, 0.5f, -7.0f, Color{255, 226, 110, 255}});
}
// The diver arriving, or leaving: a ring of teal light that flares out and a plume of air.
void Flare(PlatformState& p, Vector2 at, Color c, int n) {
    if (p.verifying) return;
    for (int i = 0; i < n; i++) {
        float a = i * 2 * PI / n;
        p.particles.push_back({at, {cosf(a) * 150, sinf(a) * 150}, 0.45f, 0.45f, 2, c});
    }
    Bubbles(p, at, 6, 10);
}

// ---------------------------------------------------------------- enemies and bosses
Rectangle EnemyBox(const PlatEnemy& e) {
    switch (e.type) {
        case 'c': return {e.pos.x + 3, e.pos.y + 4, 28, 14}; // a wide, low crustacean hull
        case 'P': case 'G': return {e.pos.x + 3, e.pos.y + 3, 16, 27};
        case 'p': return {e.pos.x - 9, e.pos.y - 6, 18, 12};
        default:  return {e.pos.x - 8, e.pos.y - 18, 16, 36}; // eel
    }
}

// Ambusher timings: bursting out of the door, the stab, and ducking back in.
constexpr float AMB_OUT = 0.28f, AMB_STAB = 0.42f, AMB_BACK = 0.32f;
// Gunner timings: the pause between shots, and the aim (the telegraph) before each one.
constexpr float GUN_REST = 1.5f, GUN_AIM = 0.7f, BALL_SPEED = 330;

float Lunge(const PlatEnemy& e) { // how far an ambusher has stepped out of his doorway
    switch (e.state) {
        case 1: return 10 * e.timer / AMB_OUT;
        case 2: return 10 + 12 * sinf(PI * std::min(1.0f, e.timer / AMB_STAB));
        case 3: return 10 * (1 - e.timer / AMB_BACK);
        default: return 0;
    }
}

// Is this enemy touching the player's box? A hidden ambusher can't be touched; a stabbing one reaches.
bool EnemyHits(const PlatEnemy& e, Rectangle pr) {
    if (e.type == 'P') {
        if (e.state == 0) return false;
        if ((e.state == 2 || e.state == 5) && e.timer > 0.08f) {
            Rectangle blade{e.dir > 0 ? e.pos.x + 18 : e.pos.x - 18, e.pos.y + 10, 22, 8};
            if (CheckCollisionRecs(pr, blade)) return true;
        }
    }
    return CheckCollisionRecs(pr, EnemyBox(e));
}

void UpdateEnemies(PlatformState& p, float dt) {
    Vector2 pc{p.pos.x + PW / 2, p.pos.y + PH / 2};
    for (auto& e : p.enemies) {
        e.t += dt;
        if (e.type == 'c') { // crabs walk, turning at walls and ledges; some are aggressive enough to charge
            float speed = 55 + e.personality.energy * 40; // 55-95: livelier crabs scuttle faster
            // an aggressive crab that notices the diver at its own height turns to charge instead of patrolling -
            // but only if that direction doesn't walk it straight off the edge of its own platform: check the
            // ledge the same way the patrol turn below does, so an aggro override can never be silently undone
            // by that same check a moment later, and a crab never suicides off a cliff just to give chase
            if (e.personality.aggression > 0.6f && fabsf(pc.y - e.pos.y) < 1.3f * T && fabsf(pc.x - e.pos.x) < 5.0f * T) {
                float chargeDir = pc.x < e.pos.x ? -1.0f : 1.0f;
                int cftx = (int)floorf((chargeDir > 0 ? e.pos.x + 34 : e.pos.x) / T), cfty = (int)floorf((e.pos.y + 17) / T);
                if (!Solid(p, cftx, cfty) && Solid(p, cftx, cfty + 1)) e.dir = chargeDir;
            }
            float nx = e.pos.x + e.dir * speed * dt;
            int ftx = (int)floorf((e.dir > 0 ? nx + 34 : nx) / T), fty = (int)floorf((e.pos.y + 17) / T);
            if (Solid(p, ftx, fty) || !Solid(p, ftx, fty + 1)) e.dir = -e.dir;
            else e.pos.x = nx;
        } else if (e.type == 'P') { // waits behind his door; bursts out when you come near, stabs, ducks back
            float dx = pc.x - (e.home.x + 16), dy = pc.y - (e.home.y + 16);
            e.timer += dt;
            switch (e.state) {
                case 0:
                    if (e.timer > 0 && fabsf(dx) < 4.2f * T && fabsf(dy) < 1.6f * T) { e.state = 1; e.timer = 0; e.dir = dx < 0 ? -1.0f : 1.0f; }
                    else if (e.timer > 0) e.timer = 0;
                    break;
                case 1: if (e.timer > AMB_OUT) { e.state = 2; e.timer = 0; } break;
                case 2:
                    if (e.timer > AMB_STAB) {
                        // some come back in at once; the bolder ones step out onto the deck and stay a while
                        bool stays = p.level != PL_CAVE && Rnd(0, 1) < 0.2f + 0.55f * e.personality.aggression;
                        e.state = stays ? 4 : 3; e.timer = 0;
                        if (stays) { e.aim = {Rnd(3.0f, 7.0f), 0}; e.pos.x = e.home.x + 5 + e.dir * Lunge(e); }
                    }
                    break;
                case 3: if (e.timer > AMB_BACK) { e.state = 0; e.timer = -1.1f; } break; // a pause before he'll come out again
                case 4: { // out on deck: paces by his door, closes in on the diver if he sees them, stabs, then heads back in
                    e.aim.x -= dt;
                    float doorX = e.home.x + 5, px = pc.x - 11;
                    bool sees = fabsf(dy) < 1.5f * T && fabsf(dx) < 6.0f * T;
                    bool home = e.aim.x <= 0 || fabsf(dx) > 10.0f * T;
                    float want = home ? doorX : sees ? px : doorX + sinf(e.t * 0.8f) * 2.5f * T;
                    float mv = want < e.pos.x - 2 ? -1.0f : want > e.pos.x + 2 ? 1.0f : 0.0f;
                    if (mv != 0) {
                        float nx = e.pos.x + mv * (sees && !home ? 70.0f : 50.0f) * dt;
                        int ftx = (int)floorf((mv > 0 ? nx + 18 : nx - 2) / T), fty = (int)floorf((e.pos.y + 15) / T);
                        if (!Solid(p, ftx, fty) && Solid(p, ftx, fty + 1)) e.pos.x = nx; // never walks off his deck
                        e.dir = mv;
                    }
                    if (sees && !home) e.dir = dx < 0 ? -1.0f : 1.0f;
                    if (home && fabsf(e.pos.x - doorX) < 3) { e.pos.x = doorX; e.state = 3; e.timer = 0; }
                    else if (!home && sees && fabsf(px - e.pos.x) < 1.4f * T) { e.state = 5; e.timer = 0; }
                    break;
                }
                default: if (e.timer > AMB_STAB) { e.state = 4; e.timer = 0; } break; // 5: a stab out on deck
            }
            if (e.state <= 3) e.pos = {e.home.x + 5 + e.dir * Lunge(e), e.home.y + T - 30};
        } else if (e.type == 'G') { // behind his barrel: aims at you, fires, reloads
            float dx = pc.x - (e.pos.x + 11), dy = pc.y - (e.pos.y + 10);
            bool inRange = fabsf(dx) < 15.0f * T && fabsf(dy) < 7.0f * T;
            e.dir = dx < 0 ? -1.0f : 1.0f;
            e.timer += dt;
            if (e.state == 0) {
                if (e.timer > GUN_REST && inRange) { e.state = 1; e.timer = 0; }
            } else {
                e.aim = pc; // tracks you while aiming, then fires where you were at the last moment
                if (e.timer > GUN_AIM) {
                    Vector2 muzzle{e.pos.x + 11 + e.dir * 14, e.pos.y + 11};
                    float ax = e.aim.x - muzzle.x, ay = e.aim.y - muzzle.y, len = std::max(1.0f, sqrtf(ax * ax + ay * ay));
                    p.shots.push_back({muzzle, {ax / len * BALL_SPEED, ay / len * BALL_SPEED}, 4, 0});
                    for (int k = 0; k < 6; k++)
                        p.particles.push_back({muzzle, {e.dir * Rnd(40, 160), Rnd(-60, 20)}, 0.3f, 0.3f, 2, Color{255, 200, 90, 255}});
                    e.state = 0;
                    e.timer = Rnd(-0.3f, 0.3f);
                }
            }
        } else if (e.type == 'p') { // fly back and forth, bobbing
            float nx = e.pos.x + e.dir * 120 * dt;
            if (fabsf(nx - e.home.x) > 5 * T || Solid(p, (int)floorf((nx + e.dir * 10) / T), (int)floorf(e.pos.y / T))) e.dir = -e.dir;
            else e.pos.x = nx;
            e.pos.y = e.home.y + sinf(e.t * 3) * 14;
        } else { // eel: leaps out of the depths, then dives back
            float period = 2.6f - e.personality.aggression * 0.9f; // 1.7-2.6s: aggressive eels leap more often
            // a curious eel notices the diver lingering right over its hole and leaps early rather than
            // finishing out a long dormant phase - the same "investigate a presence" shape as the Abyss's
            // Disturb(), just triggered by proximity instead of an acoustic disturbance (the 2D platformer
            // has nothing analogous to a dash's sound to react to)
            float cyc = fmodf(e.t, period);
            if (e.personality.curiosity > 0.6f && cyc > period * 0.55f && fabsf(pc.x - e.home.x) < 2.0f * T) { e.t = ceilf(e.t / period) * period; cyc = 0; }
            float riseFrac = 0.46f, bottom = e.home.y + 3.0f * T + 40, apex = e.home.y - (3.0f + e.personality.energy) * T;
            e.pos.y = cyc < period * riseFrac ? bottom - (bottom - apex) * sinf(PI * cyc / (period * riseFrac)) : bottom + 200;
        }
    }
}

// Torpedo tubes, deck cannons and barrel chutes fire on a timer, with a warning glow before each shot. They only wake when the
// diver is within range, so a level is never firing behind you.
constexpr float TUBE_PERIOD = 2.8f, TUBE_WARN = 0.9f, CANNON_PERIOD = 2.6f, CANNON_WARN = 0.9f, CHUTE_PERIOD = 2.9f, CHUTE_WARN = 0.75f;
float LauncherPeriod(char t) { return t == 'T' ? TUBE_PERIOD : t == 'N' ? CANNON_PERIOD : CHUTE_PERIOD; }
float LauncherWarn(char t) { return t == 'T' ? TUBE_WARN : t == 'N' ? CANNON_WARN : CHUTE_WARN; }
float LauncherAlert(const PlatformState& p, int tx, int ty) { // 0..1 how close it is to firing (for the warning glow)
    for (const auto& l : p.launchers) if (l.tx == tx && l.ty == ty) { float rem = LauncherPeriod(l.type) - l.t, w = LauncherWarn(l.type); return rem < w ? 1.0f - rem / w : 0.0f; }
    return 0;
}
void UpdateLaunchers(PlatformState& p, float dt) {
    for (auto& l : p.launchers) {
        if (fabsf((l.tx + 0.5f) * T - (p.pos.x + PW / 2)) > 30.0f * T || fabsf((l.ty + 0.5f) * T - (p.pos.y + PH / 2)) > 16.0f * T) continue;
        l.t += dt;
        if (l.t < LauncherPeriod(l.type)) continue;
        l.t -= LauncherPeriod(l.type);
        float lx = l.tx * (float)T, ly = l.ty * (float)T;
        BeastsNoise(p, {lx, ly + 16}, 0.9f); // a torpedo tube or a cannon going off startles everything for a long way
        if (l.type == 'T') {
            p.shots.push_back({{lx - 4, ly + 16}, {-310, 0}, 5.0f, 4});
            for (int k = 0; k < 8; k++) p.particles.push_back({{lx - 6, ly + 16}, {Rnd(-120, -20), Rnd(-40, 40)}, 0.5f, 0.5f, -Rnd(2, 4), Color{196, 236, 250, 255}});
        } else if (l.type == 'N') {
            p.shots.push_back({{lx - 6, ly + 14}, {-300, 0}, 5.0f, 5});
            for (int k = 0; k < 10; k++) p.particles.push_back({{lx - 6, ly + 14}, {Rnd(-160, -30), Rnd(-50, 30)}, 0.5f, 0.5f, Rnd(2, 5), k % 2 ? Color{230, 230, 220, 255} : Color{255, 190, 80, 255}});
        } else {
            p.shots.push_back({{lx - 14, ly + 6}, {-115, 0}, 9.0f, 6});
            for (int k = 0; k < 5; k++) p.particles.push_back({{lx - 4, ly + 16}, {Rnd(-70, -10), Rnd(-60, -10)}, 0.35f, 0.35f, 2, Color{170, 130, 80, 255}});
        }
    }
}

// Musket balls fly straight until they hit something; bombs arc, bounce, and go off.
constexpr float BOMB_FUSE = 1.3f, BLAST_R = 46;
void UpdateShots(PlatformState& p, float dt) {
    for (auto& s : p.shots) {
        s.life -= dt;
        if (s.kind == 4 || s.kind == 5) { // a torpedo or a cannonball: straight and fast, until it meets something solid
            s.pos.x += s.vel.x * dt;
            if (Solid(p, (int)floorf(s.pos.x / T), (int)floorf(s.pos.y / T))) {
                s.life = 0;
                Burst(p, s.pos, s.kind == 4 ? 16 : 10, s.kind == 4 ? Color{255, 170, 70, 255} : Color{210, 200, 180, 255}, 220, 0.4f, 3);
                if (s.kind == 4) Bubbles(p, s.pos, 6, 8);
                BeastsNoise(p, s.pos, 1.1f);
            } else if (GetRandomValue(0, 2) == 0) {
                if (s.kind == 4) p.particles.push_back({{s.pos.x + 14, s.pos.y + Rnd(-3, 3)}, {Rnd(0, 40), Rnd(-20, 20)}, 0.5f, 0.5f, -Rnd(2, 4), Color{196, 236, 250, 255}});
                else p.particles.push_back({{s.pos.x + 6, s.pos.y}, {Rnd(0, 30), Rnd(-30, 10)}, 0.4f, 0.4f, Rnd(2, 4), Color{170, 170, 170, 255}});
            }
        } else if (s.kind == 6) { // a barrel rolling along the deck, falling into gaps, smashing on the first wall it meets
            s.vel.y = std::min(s.vel.y + 1400 * dt, 700.0f);
            float nx = s.pos.x + s.vel.x * dt, ny = s.pos.y + s.vel.y * dt;
            if (Solid(p, (int)floorf((nx - 13) / T), (int)floorf(s.pos.y / T)) || Solid(p, (int)floorf((nx - 13) / T), (int)floorf((s.pos.y - 10) / T))) {
                s.life = 0;
                for (int k = 0; k < 10; k++) p.particles.push_back({s.pos, {Rnd(-120, 60), Rnd(-160, -20)}, 0.5f, 0.5f, Rnd(2, 4), Color{140, 96, 56, 255}});
            } else s.pos.x = nx;
            if (Solid(p, (int)floorf(s.pos.x / T), (int)floorf((ny + 13) / T))) { s.vel.y = 0; s.pos.y = floorf((ny + 13) / T) * (float)T - 13; }
            else s.pos.y = ny;
            if (s.pos.y > p.h * (float)T + 40 || (p.waterY > 0 && s.pos.y > p.waterY)) s.life = 0;
        } else if (s.kind == 0) {
            s.pos.x += s.vel.x * dt;
            s.pos.y += s.vel.y * dt;
            for (size_t i = 0; i < p.enemies.size() && s.life > 0; i++) { // friendly fire: a musket ball kills whoever it hits, pirate or parakeet
                const PlatEnemy& e = p.enemies[i];
                if (e.type == 'G' || e.type == 'c' || e.type == 'e' || (e.type == 'P' && e.state == 0)) continue;
                if (CheckCollisionRecs(EnemyBox(e), {s.pos.x - 3, s.pos.y - 3, 6, 6})) {
                    Burst(p, {e.pos.x + 11, e.pos.y + 12}, 14, e.type == 'p' ? Color{120, 180, 100, 255} : Color{220, 210, 190, 255}, 200, 0.5f, 3);
                    p.enemies.erase(p.enemies.begin() + i);
                    s.life = 0;
                }
            }
            if (Solid(p, (int)floorf(s.pos.x / T), (int)floorf(s.pos.y / T))) {
                s.life = 0;
                for (int k = 0; k < 4; k++) p.particles.push_back({s.pos, {Rnd(-80, 80), Rnd(-120, -20)}, 0.25f, 0.25f, 2, Color{200, 190, 170, 255}});
            }
        } else if (s.kind == 3) { // a drop of Kraken ink, falling from above
            s.vel.y = std::min(s.vel.y + 700 * dt, 620.0f);
            s.pos.y += s.vel.y * dt;
            if (Solid(p, (int)floorf(s.pos.x / T), (int)floorf((s.pos.y + 8) / T))) {
                s.life = 0;
                for (int k = 0; k < 5; k++) p.particles.push_back({s.pos, {Rnd(-90, 90), Rnd(-140, -30)}, 0.35f, 0.35f, 3, Color{40, 24, 50, 255}});
            }
        } else if (s.kind == 1) {
            s.vel.y += 1500 * dt;
            Vector2 np{s.pos.x + s.vel.x * dt, s.pos.y + s.vel.y * dt};
            if (Solid(p, (int)floorf(np.x / T), (int)floorf(s.pos.y / T))) { s.vel.x *= -0.5f; np.x = s.pos.x; }
            if (Solid(p, (int)floorf(np.x / T), (int)floorf((np.y + 6) / T))) { s.vel.y *= -0.35f; s.vel.x *= 0.7f; np.y = s.pos.y; }
            s.pos = np;
            if (s.life <= 0) { // boom
                s.kind = 2;
                s.life = 0.3f;
                Burst(p, s.pos, 26, Color{255, 170, 60, 255}, 300, 0.5f, 3);
                Burst(p, s.pos, 12, Color{90, 84, 80, 255}, 140, 0.8f, 4);
                for (size_t i = 0; i < p.enemies.size();) { // the blast kills any other pirates or parakeets in reach
                    const PlatEnemy& e = p.enemies[i];
                    if ((e.type == 'P' || e.type == 'p' || e.type == 'G') && CheckCollisionCircleRec(s.pos, BLAST_R, EnemyBox(e))) p.enemies.erase(p.enemies.begin() + i);
                    else i++;
                }
            }
        }
    }
    p.shots.erase(std::remove_if(p.shots.begin(), p.shots.end(), [](const PlatShot& s) { return s.life <= 0; }), p.shots.end());
}

bool ShotHits(const PlatShot& s, Rectangle pr) {
    if (s.kind == 4) return CheckCollisionRecs(pr, {s.pos.x - 16, s.pos.y - 6, 32, 12});
    if (s.kind == 5) return CheckCollisionCircleRec(s.pos, 8, pr);
    if (s.kind == 6) return CheckCollisionRecs(pr, {s.pos.x - 12, s.pos.y - 12, 24, 24});
    if (s.kind == 0) return CheckCollisionRecs(pr, {s.pos.x - 3, s.pos.y - 3, 6, 6});
    if (s.kind == 2) return CheckCollisionCircleRec(s.pos, BLAST_R, pr);
    if (s.kind == 3) return CheckCollisionRecs(pr, {s.pos.x - 6, s.pos.y - 10, 12, 20}); // ink
    return false; // a bomb only hurts when it goes off
}
// The Kraken: an ancient horror rising from the abyss beneath the arena. Its tentacles strike up from
// the depths and slam down from above where you stand; then its great head surfaces between the
// platforms. Stomp its head three times.
// The arena's row 0 sits at KrakenOrigin(p): everything below is measured from it.
float KrakenOrigin(const PlatformState& p) { return p.boss.home.y - 14.0f * T; }
constexpr float TENT_IDLE = -100;
constexpr float BB_W = 28, BB_H = 78; // Blackbeard is a head taller than anyone: a tall, narrow humanoid frame, not a wide block
constexpr float KRAKEN_SCALE = 1.8f;  // the Kraken's mantle is drawn at life size: this is the beast you fight, not a stand-in
float KrakenTop(const PlatformState& p) {
    const PlatBoss& b = p.boss;
    float under = KrakenOrigin(p) + 17.0f * T, KRAKEN_SURFACED_TOP = KrakenOrigin(p) + 11.0f * T - 10;
    switch (b.state) {
        case 1: return under + (KRAKEN_SURFACED_TOP - under) * std::min(1.0f, b.timer / 0.5f);
        case 2: return KRAKEN_SURFACED_TOP + sinf(p.time * 3) * 3;
        case 3: case 4: return KRAKEN_SURFACED_TOP + (under - KRAKEN_SURFACED_TOP) * std::min(1.0f, b.timer / 0.5f);
        default: return under;
    }
}
Rectangle KrakenHead(const PlatformState& p) {
    float top = KrakenTop(p), w = 96 * KRAKEN_SCALE, h = 70 * KRAKEN_SCALE;
    return {p.boss.home.x - w / 2, top, w, h};
}
float TentacleReach(float tt) { // 0..1 extent of a strike, tt = time since its warning began
    if (tt < 0.75f) return 0;
    if (tt < 0.9f) return (tt - 0.75f) / 0.15f;
    if (tt < 1.5f) return 1;
    if (tt < 1.8f) return 1 - (tt - 1.5f) / 0.3f;
    return 0;
}
Rectangle TentacleBox(const PlatformState& p, int i) {
    float r = TentacleReach(p.boss.tentT[i]);
    if (p.boss.tentTop[i]) { // slamming down from the dark above
        float top = KrakenOrigin(p) + T, bottom = top + (KrakenOrigin(p) + 14.0f * T - top) * r;
        return {p.boss.tentX[i] - 15, top, 30, bottom - top};
    }
    float bottom = KrakenOrigin(p) + 16.0f * T + 10, top = bottom + (KrakenOrigin(p) + 7.0f * T - bottom) * r;
    return {p.boss.tentX[i] - 15, top, 30, bottom - top};
}

void ResetBoss(PlatformState& p) {
    PlatBoss& b = p.boss;
    if (!b.type || b.defeated) return;
    b.hp = 3;
    b.state = 0;
    b.timer = 0;
    b.invuln = 0;
    b.pos = b.home;
    b.vel = {0, 0};
    b.tentT[0] = b.tentT[1] = TENT_IDLE;
    b.tentFake[0] = b.tentFake[1] = false;
    b.moveKind = -1;
    b.inkT = -1;
    b.lungeT = -1;
    b.reach = TentacleReachState{};
    b.rain = InkRainState{};
    b.beak = BeakChargeState{};
}

// Does a segment (a tentacle from base to tip, with a radius) touch a box? Sampled along its length.
bool SegmentTouches(Vector2 a, Vector2 b, float radius, Rectangle r) {
    float len = sqrtf((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
    int n = std::max(2, (int)(len / 12));
    for (int i = 0; i <= n; i++) {
        float u = (float)i / n;
        if (CheckCollisionCircleRec({a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u}, radius * (1 - u * 0.5f), r)) return true;
    }
    return false;
}

// The Kraken's head, in its beaked charge, occupies a body and a narrower beak out front.
Rectangle BeakBody(const BeakChargeState& s) { return {s.x - 55, s.y - 34, 110, 68}; }
Rectangle BeakTip(const BeakChargeState& s) { return {s.dir > 0 ? s.x + 40 : s.x - 40 - 62, s.y - 14, 62, 28}; }
bool BeakDeadly(const BeakChargeState& s) { return s.active && s.t >= 0.9f && s.t < 2.05f; }
bool ReachDeadly(const TentacleReachState& s) { return s.active && s.t >= 0.8f && s.t < 3.0f; }

// Advance the three new Kraken attacks.
void UpdateKrakenAttacks(PlatformState& p, float dt, float arenaX) {
    PlatBoss& b = p.boss;
    Vector2 pc{p.pos.x + PW / 2, p.pos.y + PH / 2};
    if (b.reach.active) {
        TentacleReachState& r = b.reach;
        r.t += dt;
        if (r.t < 2.8f) {
            r.target = pc;
            for (int i = 0; i < 2; i++) {
                Vector2 to = r.t < 0.8f ? Vector2{r.base[i].x, r.base[i].y - 50} : r.target;
                Vector2 d{to.x - r.tip[i].x, to.y - r.tip[i].y};
                float len = std::max(1.0f, sqrtf(d.x * d.x + d.y * d.y)), step = std::min(len, (r.t < 0.8f ? 90.0f : 300.0f) * dt);
                r.tip[i].x += d.x / len * step;
                r.tip[i].y += d.y / len * step;
                float bl = sqrtf((r.tip[i].x - r.base[i].x) * (r.tip[i].x - r.base[i].x) + (r.tip[i].y - r.base[i].y) * (r.tip[i].y - r.base[i].y));
                if (bl > 13.0f * T) { r.tip[i].x = r.base[i].x + (r.tip[i].x - r.base[i].x) * 13.0f * T / bl; r.tip[i].y = r.base[i].y + (r.tip[i].y - r.base[i].y) * 13.0f * T / bl; }
            }
        } else {
            for (int i = 0; i < 2; i++) { r.tip[i].x += (r.base[i].x - r.tip[i].x) * std::min(1.0f, dt * 6); r.tip[i].y += (r.base[i].y - r.tip[i].y) * std::min(1.0f, dt * 6); }
        }
        if (r.t > 3.4f) r.active = false;
    }
    if (b.rain.active) {
        InkRainState& s = b.rain;
        s.t += dt;
        if (s.t > 0.4f && s.t < 3.0f) {
            s.spawnT -= dt;
            while (s.spawnT <= 0) {
                s.spawnT += 0.14f;
                float x = arenaX + Rnd(2.0f * T, 22.0f * T);
                p.shots.push_back({{x, p.camY - 340}, {0, 260}, 5, 3}); // dark projectiles, from the top of the screen
            }
        }
        if (s.t > 3.4f) s.active = false;
    }
    if (b.beak.active) {
        BeakChargeState& s = b.beak;
        s.t += dt;
        if (s.t < 0.9f) s.y = std::clamp(pc.y, KrakenOrigin(p) + 3.0f * T, KrakenOrigin(p) + 13.0f * T); // locks onto your height
        else if (s.t < 2.0f) s.x = s.fromX + (s.toX - s.fromX) * std::min(1.0f, (s.t - 0.9f) / 1.0f);
        if (s.t > 2.4f) s.active = false;
    }
}

void UpdateBoss(PlatformState& p, float dt) {
    PlatBoss& b = p.boss;
    if (!b.type) return;
    b.timer += dt;
    b.invuln = std::max(0.0f, b.invuln - dt);
    if (b.type == 'K') {
        float arenaX = (p.w - CH_W) * (float)T, pitL = arenaX + 4 * T + 16, pitR = arenaX + 18 * T - 16;
        float arenaMid = arenaX + 11.5f * T;
        bool playerInArena = p.pos.x > arenaX - 2 * T;
        // tentT: TENT_IDLE when idle, negative while waiting to start, then seconds since its warning began.
        // A "fake" strike warns exactly like a real one, then never extends -- just to bait an early dodge.
        for (int i = 0; i < 2; i++)
            if (b.tentT[i] > TENT_IDLE) {
                b.tentT[i] += dt;
                if (b.tentFake[i] && b.tentT[i] >= 0.75f) { b.tentT[i] = TENT_IDLE; b.tentFake[i] = false; }
                else if (b.tentT[i] > 1.8f) b.tentT[i] = TENT_IDLE;
            }
        UpdateKrakenAttacks(p, dt, arenaX);
        if (b.inkT >= 0 && (b.inkT += dt) > 2.0f) b.inkT = -1;
        if (b.lungeT >= 0 && (b.lungeT += dt) > 1.3f) b.lungeT = -1;
        switch (b.state) {
            case 0: // submerged: picks one move for this cycle -- a tentacle strike (maybe a bluff), a
                     // spray of ink that floods half the arena, or a fast lunge sweeping across it
                if (!b.defeated && playerInArena && b.timer > 0.3f && b.timer < 1.0f && b.moveKind == -1) {
                    b.moveKind = GetRandomValue(0, 5);
                    float px = p.pos.x + PW / 2;
                    if (b.moveKind == (int)KrakenMove::TentacleReach) {
                        b.reach = TentacleReachState{};
                        b.reach.active = true;
                        b.reach.base[0] = {pitL, KrakenOrigin(p) + 16.0f * T};
                        b.reach.base[1] = {pitR, KrakenOrigin(p) + 16.0f * T};
                        b.reach.tip[0] = b.reach.base[0]; b.reach.tip[1] = b.reach.base[1];
                        b.reach.target = {px, p.pos.y};
                    } else if (b.moveKind == (int)KrakenMove::InkRain) {
                        b.rain = InkRainState{};
                        b.rain.active = true;
                    } else if (b.moveKind == (int)KrakenMove::BeakCharge) {
                        b.beak = BeakChargeState{};
                        b.beak.active = true;
                        bool fromLeft = px > arenaMid; // it charges from the side you are NOT near, toward you
                        b.beak.dir = fromLeft ? 1.0f : -1.0f;
                        b.beak.fromX = fromLeft ? arenaX - 2.0f * T : arenaX + 26.0f * T;
                        b.beak.toX = fromLeft ? arenaX + 26.0f * T : arenaX - 2.0f * T;
                        b.beak.x = b.beak.fromX;
                        b.beak.y = p.pos.y;
                    } else
                    if (b.moveKind == 0) {
                        bool slamFirst = GetRandomValue(0, 1) == 1;
                        b.tentTop[0] = slamFirst;
                        b.tentX[0] = slamFirst ? std::clamp(px, arenaX + 2.0f * T, arenaX + 21.0f * T) : std::clamp(px, pitL, pitR);
                        b.tentTop[1] = !slamFirst;
                        b.tentX[1] = std::clamp(px + (GetRandomValue(0, 1) ? 120.0f : -120.0f), pitL, pitR);
                        if (b.tentTop[1]) b.tentX[1] = std::clamp(px, arenaX + 2.0f * T, arenaX + 21.0f * T);
                        b.tentFake[0] = GetRandomValue(0, 99) < 25;
                        b.tentFake[1] = GetRandomValue(0, 99) < 25;
                        b.tentT[0] = 0;
                        b.tentT[1] = -0.5f; // the second strike follows a moment later, where you've moved to
                    } else if (b.moveKind == 1) {
                        b.inkSafeRight = px < arenaMid; // the spray floods away from where you're standing now
                        b.inkT = 0;
                    } else {
                        bool fromLeft = px > arenaMid;
                        b.lungeFromX = fromLeft ? arenaX + 20.0f * T : arenaX + 3.0f * T;
                        b.lungeToX = fromLeft ? arenaX + 3.0f * T : arenaX + 20.0f * T;
                        b.lungeT = 0;
                    }
                }
                if (b.defeated) break;
                if (b.timer > 2.9f && playerInArena && !b.reach.active && !b.rain.active && !b.beak.active) { b.state = 1; b.timer = 0; }
                break;
            case 1: if (b.timer > 0.5f) { b.state = 2; b.timer = 0; } break;
            case 2: if (b.timer > 2.2f) { b.state = 3; b.timer = 0; } break;
            case 3: if (b.timer > 0.5f) { b.state = 0; b.timer = 0; b.moveKind = -1; } break;
            case 4: break; // sinking away for good
        }
    } else if (b.type == 'B') {
        // Blackbeard can't be stomped while he's on his guard: touching him is deadly. He stalks you,
        // fires his pistols, and charges. Dodge a charge so he crashes into a wall, and he's dazed for a
        // moment: that's when to stomp him. Each hit makes him faster, and after the first he throws bombs.
        float px = p.pos.x + PW / 2, bx = b.pos.x + BB_W / 2;
        int hits = 3 - b.hp;
        float charge = (p.hard ? 460.0f : 380.0f) + hits * (p.hard ? 65.0f : 45.0f);
        if (b.defeated) b.vel.x = 0;
        else switch (b.state) {
            case 0: // stalk toward you
                b.dir = px < bx ? -1.0f : 1.0f;
                b.vel.x = b.dir * (70 + hits * 30.0f);
                if (b.timer > (hits ? 0.65f : 1.0f)) { // reads faster each hit: less time to predict what's next
                    b.vel.x = 0;
                    b.timer = 0;
                    b.state = (b.volley++ % 2 == 0 && fabsf(px - bx) > 2.5f * T) || p.pos.y + PH < b.pos.y ? 4 : 1; // a shot, or a charge
                }
                break;
            case 1: // wind-up: he lowers his head and paws the boards
                b.vel.x = 0;
                b.dir = px < bx ? -1.0f : 1.0f;
                if (b.timer > (p.hard ? 0.3f : 0.4f)) { b.state = 2; b.timer = 0; b.chargeStartX = b.pos.x; }
                break;
            case 2: // charge! He only stops at a wall -- and it only counts as a daze if the charge carried
                     // real distance first, so tapping a wall he started right next to doesn't count
                b.vel.x = b.dir * charge;
                if (b.timer > 3.0f) { b.state = 3; b.timer = 0; }
                break;
            case 4: // aiming a pistol; it fires at the end
                b.vel.x = 0;
                b.dir = px < bx ? -1.0f : 1.0f;
                if (b.timer > (p.hard ? 0.38f : 0.5f)) {
                    Vector2 muzzle{bx + b.dir * 26, b.pos.y + 30}, to{px, p.pos.y + PH / 2};
                    float ax = to.x - muzzle.x, ay = to.y - muzzle.y, len = std::max(1.0f, sqrtf(ax * ax + ay * ay));
                    p.shots.push_back({muzzle, {ax / len * 430, ay / len * 430}, 4, 0});
                    Burst(p, muzzle, 8, Color{255, 210, 110, 255}, 150, 0.25f, 2);
                    b.state = b.volley % 2 == 0 ? 4 : 0; // fires twice in a row from the second hit on
                    b.volley += b.state == 4;
                    b.timer = b.state == 4 ? 0.08f : 0;
                }
                break;
            case 5: // dazed after hitting a wall: now he can be stomped, but not for long
                b.vel.x = 0;
                if (b.timer > (p.hard ? 1.0f : 1.5f)) { b.state = 3; b.timer = 0; }
                break;
            default: // 3: back on his feet (and furious)
                b.vel.x = 0;
                if (b.timer > 0.4f) { b.state = 0; b.timer = 0; }
                break;
        }
        b.vel.y = std::min(b.vel.y + GRAV_DOWN * dt, MAX_FALL);
        bool grounded, hitWall;
        MoveAndCollide(p, b.pos, b.vel, BB_W, BB_H, dt, grounded, hitWall);
        if (hitWall && b.state == 2) {
            float traveled = fabsf(b.pos.x - b.chargeStartX);
            if (traveled >= 5.0f * T) { // a real charge, carried at speed into the wall: he's genuinely dazed
                b.state = 5;
                b.timer = 0;
                Burst(p, {b.dir > 0 ? b.pos.x + BB_W : b.pos.x, b.pos.y + 30}, 16, Color{190, 150, 100, 255}, 220, 0.5f, 3); // splinters
            } else { // he was already right on top of the wall: barely a bump, and he shrugs it off
                b.state = 0;
                b.timer = 0;
                b.vel.x = 0;
                Burst(p, {b.dir > 0 ? b.pos.x + BB_W : b.pos.x, b.pos.y + 30}, 5, Color{190, 150, 100, 255}, 90, 0.25f, 2);
            }
        }
    }
}

// Blackbeard lobs a bomb at you as he gets up.
void ThrowBomb(PlatformState& p) {
    const PlatBoss& b = p.boss;
    Vector2 from{b.pos.x + BB_W / 2, b.pos.y + 16};
    float dx = std::clamp((p.pos.x + PW / 2 - from.x) * 1.25f, -420.0f, 420.0f);
    p.shots.push_back({from, {dx, -560}, BOMB_FUSE, 1});
}
// A tiny, self-contained hash (no dependency on Hs(), which isn't defined until much later in this file):
// rolls a crab's or an eel's personality from its own tile position, so it's stable across a checkpoint
// respawn's rebuild without needing to plumb the level seed all the way down to here.
float EnemyTraitHash(float a, float b, float salt) { float s = sinf(a * 12.9898f + b * 78.233f + salt * 37.719f) * 43758.5453f; return s - floorf(s); }
PersonalityProfile RollEnemyTraits(float x, float y) { return {EnemyTraitHash(x, y, 1), EnemyTraitHash(x, y, 2), EnemyTraitHash(x, y, 3), EnemyTraitHash(x, y, 4)}; }

// ---------------------------------------------------------------- building a level
// The generator (levelgen.cpp) supplies the terrain; a boss arena, when there is one, is joined onto its last
// platform. Whatever the level doesn't cover is filled with `fill` (below) or `fillAbove` (above).
void ScanTiles(PlatformState& p) {
    p.enemies.clear();
    p.launchers.clear();
    p.boss = PlatBoss{};
    for (int r = 0; r < p.h; r++)
        for (int c = 0; c < p.w; c++) {
            char& ch = p.tiles[r][c];
            float x = c * (float)T, y = r * (float)T;
            switch (ch) {
                case 'S': p.startPos = {x + 6, y + T - PH}; break;
                case 'c': { PlatEnemy pe{'c', {x - 1, y + T - 18}, {x, y}, -1, 0}; pe.personality = RollEnemyTraits(x, y); p.enemies.push_back(pe); break; }
                case 'P': {
                    if (p.level == PL_CAVE) { // a stalactite spider needs a ceiling to hang from - none within reach, no spider
                        int roof = -1;
                        for (int yy = r - 2; yy >= std::max(0, r - 12) && roof < 0; yy--) if (Solid(p, c, yy)) roof = yy;
                        if (roof < 0) { ch = '.'; break; }
                        PlatEnemy pe{'P', {x + 5, y + T - 30}, {x, y}, -1, 0}; pe.personality = RollEnemyTraits(x, y); pe.aim = {0, (roof + 1) * (float)T}; p.enemies.push_back(pe); break;
                    }
                    PlatEnemy pe{'P', {x + 5, y + T - 30}, {x, y}, -1, 0}; pe.personality = RollEnemyTraits(x, y); p.enemies.push_back(pe); break;
                }
                case 'G': { PlatEnemy pe{'G', {x + 6, y + T - 30}, {x, y}, -1, 0, 0, Rnd(0, 1.2f)}; pe.personality = RollEnemyTraits(x, y); p.enemies.push_back(pe); break; }
                case 'p': { PlatEnemy pe{'p', {x + 16, y + 16}, {x + 16, y + 16}, 1, 0}; pe.personality = RollEnemyTraits(x, y); p.enemies.push_back(pe); break; }
                case 'e': { PlatEnemy pe{'e', {x + 16, y + 400}, {x + 16, y}, 1, c * 0.37f}; pe.personality = RollEnemyTraits(x, y); p.enemies.push_back(pe); break; }
                case 'o': if ((r * 7 + c * 13) % 5 != 0) ch = '.'; continue;   // generator "coins" survive only as a few faint pools of light: environmental cues, never markers or pickups
                case 'T': case 'N': case 'y': p.launchers.push_back({c, r, ch, Rnd(0.0f, 2.0f)}); continue; // solid fixtures that fire on a timer: the tile stays
                case 'K': p.boss.type = 'K'; p.boss.home = {x + 16, y + T}; p.boss.tentT[0] = p.boss.tentT[1] = TENT_IDLE; break;
                case 'B': p.boss.type = 'B'; p.boss.home = p.boss.pos = {x, y + T - BB_H}; break;
                default: continue;
            }
            ch = '.';
        }
    p.spawns[0] = p.startPos;
    p.exitOpen = p.boss.type != 'B'; // Blackbeard guards the treasure
    p.pos = p.startPos;
    p.camX = p.pos.x;
    p.camY = p.pos.y;
}

void BuildFromGrid(PlatformState& p, const GenLevel& gl, const Part* arena, char fill, char fillAbove) {
    int ay = arena ? gl.exitRow - EntryRow(*arena) : 0; // the arena's first row, in generator rows
    int top = arena ? std::min(0, ay) : 0, bottom = arena ? std::max(gl.h, ay + arena->h) : gl.h;
    p.genTop = top;
    p.w = gl.w + (arena ? CH_W : 0);
    p.h = bottom - top;
    p.tiles.assign(p.h, std::string(p.w, fill));
    for (int r = 0; r < p.h; r++) {
        int gr = r + top;
        for (int c = 0; c < gl.w; c++) p.tiles[r][c] = gr < 0 ? fillAbove : gr >= gl.h ? fill : gl.rows[gr][c];
        if (arena && r < ay - top) for (int c = gl.w; c < p.w; c++) p.tiles[r][c] = fillAbove;
    }
    p.partX.clear(); p.spawns.clear(); p.partInterior.clear(); p.partKind.clear();
    p.deathY.assign(p.w, p.h * (float)T + 40);
    p.waterY = 0;
    if (p.level == PL_PIRATE) { // the sea: falling between the ships means the water, not a strip of spikes
        p.waterY = (gl.h - 20 + 3 - top) * (float)T + 16;
        for (int c = 0; c < gl.w; c++) p.deathY[c] = p.waterY - PH + 12;
    }
    for (int x = 0; x < gl.w; x += CH_W) { // checkpoint chunks: each respawns at the first critical-path platform inside it
        const GenWaypoint* wp = &gl.path.back();
        for (const GenWaypoint& w : gl.path) if (w.tx >= x) { wp = &w; break; }
        p.partX.push_back(x);
        p.partInterior.push_back(1 << 20);
        p.partKind.push_back(0);
        p.spawns.push_back({wp->tx * (float)T + 6, (wp->ty - top + 1) * (float)T - PH});
    }
    if (arena) {
        for (int r = 0; r < arena->h; r++) {
            std::string row = arena->rows[r];
            row.resize(CH_W, '.');
            for (int c = 0; c < CH_W; c++) p.tiles[r + ay - top][gl.w + c] = (row[c] == '<' || row[c] == '>') ? '.' : row[c];
        }
        p.partX.push_back(gl.w);
        p.partInterior.push_back(arena->interior >= 999 ? 1 << 20 : ay - top + arena->interior);
        p.partKind.push_back(arena->kind);
        p.spawns.push_back({gl.w * (float)T + 6, (ay - top + EntryRow(*arena) + 1) * (float)T - PH});
    }
    ScanTiles(p);
}

// ---- the Grand Kraken's snaps (ParkourReference1.3): a ship torn in half at a proven point (see GenSnap)
int SnapSinkA(const GenSnap& s) { return s.sink < 0 ? s.x0 : s.col + 2; }
int SnapSinkB(const GenSnap& s) { return s.sink < 0 ? s.col - 2 : s.x1; }
void ApplySnapTiles(PlatformState& p, const GenSnap& s) {
    int top = std::max(0, s.top - p.genTop), bot = std::min(p.h - 2, s.bottom - p.genTop);
    for (int x = std::max(0, s.col - 1); x <= std::min(p.w - 1, s.col + 1); x++)
        for (int y = top; y <= bot; y++) p.tiles[y][x] = '.';
    if (s.sink != 0)
        for (int x = std::max(0, SnapSinkA(s)); x <= std::min(p.w - 1, SnapSinkB(s)); x++) {
            for (int y = bot; y >= top; y--) p.tiles[y + 1][x] = p.tiles[y][x];
            p.tiles[top][x] = '.';
        }
}
void SnapShip(PlatformState& p, int k) {
    if (k < 0 || k >= (int)p.snaps.size() || p.snapped[k]) return;
    const GenSnap s = p.snaps[k];
    p.snapped[k] = 1;
    ApplySnapTiles(p, s);
    float t0 = (s.col - 1) * (float)T, t1 = (s.col + 2) * (float)T;
    float a = SnapSinkA(s) * (float)T, b = (SnapSinkB(s) + 1) * (float)T;
    for (size_t e = 0; e < p.enemies.size();) { // whoever stood in the tear goes into the sea; whoever is on the sinking half goes down with it
        PlatEnemy& en = p.enemies[e];
        float cx = en.home.x + 16;
        if (cx >= t0 && cx < t1) { Burst(p, {cx, en.pos.y + 16}, 10, Color{200, 225, 250, 255}, 160, 0.6f, 3); p.enemies.erase(p.enemies.begin() + e); continue; }
        if (s.sink && cx >= a && cx < b) { en.pos.y += T; en.home.y += T; }
        e++;
    }
    for (size_t l = 0; l < p.launchers.size();) {
        PlatLauncher& L = p.launchers[l];
        if (L.tx >= s.col - 1 && L.tx <= s.col + 1) { p.launchers.erase(p.launchers.begin() + l); continue; }
        if (s.sink && L.tx >= SnapSinkA(s) && L.tx <= SnapSinkB(s)) L.ty++;
        l++;
    }
    if (s.sink) {
        for (auto& c : p.crumbles) if (c.tx >= SnapSinkA(s) && c.tx <= SnapSinkB(s)) c.ty++;
        for (auto& c : p.crumbled) if (c.first >= SnapSinkA(s) && c.first <= SnapSinkB(s)) c.second++;
        float cx = p.pos.x + PW / 2;
        if (cx >= a && cx < b && (p.onGround || p.pose == 6)) p.pos.y += T; // riding the half down
    }
    for (int y = std::max(0, s.top - p.genTop); y <= std::min(p.h - 1, s.bottom - p.genTop + 1); y++) // the timbers splinter
        Burst(p, {s.col * (float)T + 16, y * (float)T + 16}, 3, Color{150, 108, 66, 255}, 260, 0.9f, 3);
    Bubbles(p, {s.col * (float)T + 16, p.waterY}, 14, 40);
    BeastsNoise(p, {s.col * (float)T + 16, (s.bottom - p.genTop) * (float)T}, 1.6f);
    BeastsTerrainChanged(p);
}

// Builds (or rebuilds, after a death) the whole level from its seed: coins, enemies and the boss all come back.
static void PopulateCritters(PlatformState& p, unsigned seed); // defined below Hs(), which it needs

void BuildLevel(PlatformState& p) {
    const LevelDef& L = Lv(p.level);
    unsigned seed = p.layout.empty() ? 1u : (unsigned)p.layout[0];
    float scale = p.layout.size() > 1 ? p.layout[1] / 100.0f : 1.0f;
    GenLevel gl = GenerateLevel(p.level, seed, scale);
    const Part* arena = !PlatHasArena(p.level) ? nullptr : &(p.bossEnabled ? L.last : L.lastNoBoss);
    BuildFromGrid(p, gl, arena, L.fill, L.fillAbove);
    p.snaps.clear(); p.snapped.clear(); // only the snap points the validator proved (layout[3]) - an older layout has none
    unsigned proven = p.layout.size() >= 4 ? (unsigned)p.layout[3] : 0u;
    for (size_t k = 0; k < gl.snaps.size() && k < 31; k++) if (proven >> k & 1) p.snaps.push_back(gl.snaps[k]);
    p.snapped.assign(p.snaps.size(), 0);
    if (!p.hard) // Normal: mines and spiked balls become a plain spiked bed underfoot instead of vanishing outright, and the jets fire on a shorter, gentler window (see JetOn) rather than going cold
        for (auto& row : p.tiles)
            for (char& c : row) if (c == 'g') c = 'x';
    p.coins = 0;
    p.relic = -1;
    p.shots.clear();
    p.checkpointChunk = 0;
    p.critters.clear();
    // The Pipes have no enemies (CLAUDE.md) - this is ambient duct life, not a hazard: no collision or
    // death check anywhere touches p.critters. Skipped headlessly: the path-search rebuilds many
    // PlatformState instances rapidly and never renders, so there is nothing for this to add there.
    if (p.level == PL_PIPES && !p.verifying) PopulateCritters(p, seed);
    // The living-AI creatures (beasts.h): every level's food web, dens and all. In the Hull the engine also takes
    // the generator's crabs and eels out of p.enemies and runs them as hunters with dens of their own.
    BeastsBuild(p, seed);
}
int PartAt(const PlatformState& p, float x) {
    int k = 0;
    for (int i = 0; i < (int)p.partX.size(); i++) if (x >= p.partX[i] * (float)T) k = i;
    return k;
}

float DeathY(const PlatformState& p) {
    int c = std::clamp((int)((p.pos.x + PW / 2) / T), 0, p.w - 1);
    return p.deathY.empty() ? p.h * (float)T + 40 : p.deathY[c];
}

Vector2 SpawnPoint(const PlatformState& p) { return p.spawns.empty() ? p.startPos : p.spawns[std::min(p.checkpointChunk, (int)p.spawns.size() - 1)]; }

void Die(PlatformState& p) {
    if (p.deathTimer > 0 || p.finished) return;
    p.deathTimer = 0.45f;
    p.deaths++;
    Vector2 c{p.pos.x + PW / 2, p.pos.y + PH / 2};
    if (p.waterY > 0 && p.pos.y + PH > p.waterY - 6) { // into the sea: a spout of spray
        for (int k = 0; k < 14; k++) p.particles.push_back({{c.x + Rnd(-8, 8), p.waterY}, {Rnd(-70, 70), Rnd(-260, -120)}, 0.7f, 0.7f, Rnd(2, 4), Color{200, 225, 250, 255}});
        Bubbles(p, {c.x, p.waterY + 6}, 8, 12);
    }
    Burst(p, c, 18, Pal::Teal, 260, 0.6f, 3);
    Burst(p, c, 10, Pal::Brass, 200, 0.5f, 3);
    if (!p.verifying) {
        Burst(p, c, 8, Color{250, 250, 250, 255}, 320, 0.25f, 2); // the flash
        Bubbles(p, c, 10, 10);
        Flare(p, c, Color{120, 240, 240, 255}, 14);
    }
}

void Respawn(PlatformState& p) {
    for (auto& q : p.crumbled) if (At(p, q.first, q.second) == '.') p.tiles[q.second][q.first] = 'f'; // the scaffolding is put back
    p.crumbled.clear();
    p.crumbles.clear();
    if (!p.checkpoints) BuildLevel(p); // back to the very beginning, as it was
    p.pos = SpawnPoint(p);
    BeastsDiverRespawned(p, {p.pos.x + PW / 2, p.pos.y + PH / 2}); // whatever killed you doesn't get to wait on the checkpoint
    Flare(p, {p.pos.x + PW / 2, p.pos.y + PH / 2}, Color{140, 250, 255, 255}, 16);
    p.shots.clear();
    for (auto& e : p.enemies) if (e.type == 'P' || e.type == 'G') { e.state = 0; e.timer = 0.6f; }
    p.vel = {0, 0};
    p.scale = {1, 1};
    p.wallLock = 0;
    p.jumpBuffer = 0;
    ResetBoss(p);
}

// ---------------------------------------------------------------- the player
// Steam vents ('v', set into the floor) blow a column of steam 5 tiles high for 1.6 s of every 2.6 s, lifting the diver.
bool VentOn(const PlatformState& p, int tx) { return fmodf(p.time + (tx % 7) * 0.37f, 2.6f) < 1.6f; }
bool WallIsSlime(const PlatformState& p, int side) { // the wall at the diver's side is slimed over
    float x = side > 0 ? p.pos.x + PW + 0.5f : p.pos.x - 0.5f;
    int tx = (int)floorf(x / T);
    for (int ty = (int)floorf((p.pos.y + 4) / T); ty <= (int)floorf((p.pos.y + PH - 4) / T); ty++) if (At(p, tx, ty) == 's') return true;
    return false;
}
bool WallIsBarnacle(const PlatformState& p, int side) { // the wall you are wall-jumping off is lined with barnacles: springy
    float x = side > 0 ? p.pos.x + PW + 0.5f : p.pos.x - 0.5f;
    int tx = (int)floorf(x / T);
    for (int ty = (int)floorf((p.pos.y + 4) / T); ty <= (int)floorf((p.pos.y + PH - 4) / T); ty++) if (At(p, tx, ty) == 'b') return true;
    return false;
}
constexpr float GHOST_SPEED = 1.6f;
// ---- the diver's extra moves (ParkourReference1.2, "Traversal States & Physics Constants")
constexpr float LOW_DH = 12;             // sliding or rolling, the diver is this much shorter
constexpr float STUN_V = 860, STUN_T = 0.7f; // a landing faster than this (a drop taller than any jump) stuns - unless you roll
constexpr float ROLL_T = 0.38f, ROLL_MIN_V = 480;
constexpr float SLIDE_MIN_V = RUN * 0.55f, SLIDE_FRICTION = 380;
constexpr float DASH_V = 640, DASH_T = 0.14f, WATER_DASH_V = 600, WATER_DASH_T = 0.18f;
constexpr float BRAKE_FALL = 170, GLIDE_FALL = 190;
bool WaterLevel(const PlatformState& p) { return p.level == PL_HULL || p.level == PL_WEEDS || p.level == PL_ATLANTIS || p.level == PL_CAVE; } // the Cave is a flooded cave (the user's call)
bool LowPose(const PlatformState& p) { return p.pose == 1 || p.pose == 2; }
bool Climbable(char c) { return c == 'w' || c == 'l'; }
bool HeadroomToStand(const PlatformState& p) { // room to stand back up out of a slide
    int x0 = (int)floorf((p.pos.x + 1) / T), x1 = (int)floorf((p.pos.x + PW - 1) / T), ty = (int)floorf((p.pos.y + 1) / T);
    for (int tx = x0; tx <= x1; tx++) if (Solid(p, tx, ty)) return false;
    return true;
}
void StepPlayer(PlatformState& p, float dir, bool jumpHeld) {
    const bool water = WaterLevel(p);
    p.downBuf = p.inDown ? 0.15f : p.downBuf - STEP;
    if (p.wallLock > 0) {
        p.wallLock -= STEP;
        if (dir == (float)p.lockSide) dir = 0; // right after a wall jump, pushing back into the wall is ignored
    }
    // ---- poses that take control away for a moment
    if (p.pose == 3) { p.moveT -= STEP; dir = 0; p.jumpBuffer = 0; if (p.moveT <= 0) p.pose = 0; } // stunned by a hard landing
    if (p.pose == 2) { p.moveT -= STEP; dir = p.vel.x > 0 ? 1.0f : -1.0f; if (p.moveT <= 0 && HeadroomToStand(p)) p.pose = 0; } // the roll carries you
    if (p.pose == 8) { p.moveT -= STEP; if (p.moveT <= 0 || p.onGround) p.pose = 0; }
    // ---- the ledge hang: hands on the lip, feet against the wall
    if (p.pose == 9) {
        p.vel = {0, 0};
        p.dashReady = true;
        if (p.jumpBuffer > 0 || p.upHeld) { // haul up and over
            float top = p.ledgeTy * (float)T;
            p.pos = {p.ledgeTx * (float)T + (p.lockSide > 0 ? 2.0f : T - PW - 2.0f), top - PH};
            p.pose = 0; p.jumpBuffer = 0; p.onGround = true; p.scale = {1.2f, 0.85f};
            Dust(p, {p.pos.x + PW / 2, p.pos.y + PH}, 4, 0);
            return;
        }
        if (p.inDown || dir == -(float)p.lockSide) { p.pose = 0; p.pos.x -= p.lockSide * 2.0f; } // let go
        else return;
    }
    // ---- a dash: double-tap a direction. On land, a flat burst that holds your height; underwater, any of eight ways
    if (p.dashReq != 0 && p.dashReady && p.pose != 3 && p.pose != 2) {
        Vector2 d{(float)p.dashReq, 0};
        if (water) { if (p.upHeld) d.y = -1; else if (p.inDown) d.y = 1; float l = sqrtf(d.x * d.x + d.y * d.y); d = {d.x / l, d.y / l}; }
        p.dashDir = d; p.pose = 5; p.moveT = water ? WATER_DASH_T : DASH_T; p.dashReady = false;
        p.facingRight = d.x > 0;
        p.scale = {1.3f, 0.8f};
        BeastsNoise(p, {p.pos.x + PW / 2, p.pos.y + PH / 2}, 0.5f); // the reference: a dash is heard
        if (water) Bubbles(p, {p.pos.x + PW / 2, p.pos.y + PH / 2}, 8, 10);
    }
    p.dashReq = 0;
    bool dashing = p.pose == 5;
    if (dashing) {
        p.moveT -= STEP;
        float v = water ? WATER_DASH_V : DASH_V;
        p.vel = {p.dashDir.x * v, p.dashDir.y * v};
        if (!p.verifying && GetRandomValue(0, 2) == 0) p.particles.push_back({{p.pos.x + PW / 2, p.pos.y + PH / 2 + Rnd(-8, 8)}, {-p.dashDir.x * 60, 0}, 0.25f, 0.25f, 3, water ? Color{200, 236, 250, 200} : Color{240, 240, 230, 200}}); // the streak behind
        if (p.moveT <= 0) {
            p.pose = 0;
            if (water) { float keep = p.slickT > 0 ? 0.7f : 0.3f; p.vel = {p.vel.x * keep, p.vel.y * keep}; } // the water takes it straight back: heavy drag after the burst (less when slick)
            else p.boostT = 0.4f;
        }
    }

    // ---- running (not while sliding, rolling or dashing)
    if (!dashing && p.pose != 1 && p.pose != 2) {
        float target = dir * RUN * (1 + p.speedPct / 100.0f), accel; // the lead hero's relics (a syringe) quicken the run
        if (p.pose == 4) target *= 0.6f;                                // spread out against the water, drifting
        if (p.slickT > 0) target *= 1.3f;                               // barnacle-mite slime on the suit: the water lets go of you
        if (At(p, (int)floorf((p.pos.x + PW / 2) / T), (int)floorf((p.pos.y + PH - 2) / T)) == '~') target *= 0.6f; // wading through a pool (the Island)
        if (p.onGround) accel = dir == 0 ? DECEL_GROUND : p.vel.x * dir < 0 ? DECEL_GROUND + ACCEL_GROUND : ACCEL_GROUND;
        else accel = dir == 0 ? DECEL_AIR : ACCEL_AIR;
        if (p.slickT > 0 && dir == 0) accel *= 0.3f;
        // a boosted move (slide-jump, roll, pole hop, backflip, dash) keeps its extra speed through the air a while
        if (!p.onGround && (p.boostT > 0 || p.slickT > 0) && dir != 0 && p.vel.x * dir > fabsf(target)) accel = 260;
        if (p.vel.x < target) p.vel.x = std::min(target, p.vel.x + accel * STEP);
        else if (p.vel.x > target) p.vel.x = std::max(target, p.vel.x - accel * STEP);
    }
    p.boostT -= STEP;
    p.slickT -= STEP;
    if (p.pose == 1) { // the ground slide: nearly frictionless, it carries on under its own momentum
        float s = p.vel.x > 0 ? 1.0f : -1.0f;
        p.vel.x -= s * SLIDE_FRICTION * (p.slickT > 0 ? 0.3f : 1.0f) * STEP; // on glassy slime a slide barely slows
        if (p.vel.x * s < 0) p.vel.x = 0;
        bool end = !p.onGround || fabsf(p.vel.x) < 90 || !p.inDown;
        if (end && HeadroomToStand(p)) p.pose = 0;
        if (!p.verifying && GetRandomValue(0, 3) == 0) Dust(p, {p.pos.x + PW / 2, p.pos.y + PH}, 1, -s);
    }
    if (p.pose == 2) { float s = p.vel.x > 0 ? 1.0f : -1.0f; p.vel.x -= s * 300 * STEP; }
    if (dir != 0 && p.wallLock <= 0 && p.pose != 1 && p.pose != 2 && !dashing) p.facingRight = dir > 0;
    // start a slide: Down while running on the ground
    if (p.pose == 0 && p.onGround && p.inDown && fabsf(p.vel.x) > SLIDE_MIN_V) {
        p.pose = 1; p.vel.x *= 1.12f; p.scale = {1.25f, 0.8f};
        BeastsNoise(p, {p.pos.x + PW / 2, p.pos.y + PH}, 0.3f);
    }

    // wall slide: in the air, pressing into a wall
    p.wallSide = 0;
    if (!p.onGround && dir != 0 && !dashing && TouchWall(p, (int)dir)) p.wallSide = (int)dir;
    if (p.wallSide != 0 && WallIsSlime(p, p.wallSide)) { p.wallSide = 0; p.vel.y = std::max(p.vel.y, 320.0f); } // slime: no grip, no wall jump - straight down
    if (p.wallSide) { p.wallCoyote = 0.08f; p.lockSide = p.wallSide; p.dashReady = true; if (p.pose == 4 || p.pose == 7) p.pose = 0; }
    else p.wallCoyote -= STEP;

    // the ledge grab: falling past the lip of a wall you're pushing into, with open space above it
    if (!p.onGround && !dashing && p.vel.y > 0 && p.wallSide != 0 && p.pose != 9 && p.lockSide != 0) {
        int tx = (int)floorf((p.wallSide > 0 ? p.pos.x + PW + 1 : p.pos.x - 1) / T);
        int handY = (int)floorf((p.pos.y + 2) / T);
        if (Solid(p, tx, handY) && !Solid(p, tx, handY - 1) && !Solid(p, tx, handY - 2) && At(p, tx, handY) != 'f' && At(p, tx, handY) != 'x' &&
            p.pos.y + 2 - handY * (float)T < 14) { // (the path search can grab ledges too: movement pass 2)
            p.pose = 9; p.ledgeTx = tx; p.ledgeTy = handY; p.vel = {0, 0};
            p.pos.y = handY * (float)T - 2;
            p.scale = {0.9f, 1.1f};
            return;
        }
    }

    p.coyote = p.onGround || p.pose == 6 ? COYOTE : p.coyote - STEP;
    p.jumpBuffer -= STEP;
    if (p.jumpBuffer > 0) {
        bool fromPole = p.onWeed || p.pose == 6;
        if (fromPole && p.shiftHeld && dir != 0) { // the pole backflip: kicked off the pole, higher and faster than a plain jump
            p.vel.y = -JUMP_V * 1.18f * (1 + p.jumpPct / 100.0f);
            p.vel.x = dir * RUN * 1.2f;
            p.pose = 8; p.moveT = 0.45f; p.coyote = p.jumpBuffer = 0; p.onGround = false; p.dashReady = true; p.boostT = 0.8f;
            p.scale = {0.7f, 1.35f};
            BeastsNoise(p, {p.pos.x + PW / 2, p.pos.y + PH}, 0.25f);
        } else if (p.coyote > 0) {
            // a pole hop: jumping forward off a horizontal pole (a pipe, a spar, a kelp float) is a flatter, faster leap
            bool onBar = false;
            if (p.onGround) for (int tx = (int)floorf((p.pos.x + 3) / T); tx <= (int)floorf((p.pos.x + PW - 3) / T); tx++) if (At(p, tx, (int)floorf((p.pos.y + PH + 1) / T)) == '=') onBar = true;
            bool hop = onBar && dir != 0 && !p.verifying && p.shiftHeld == false && p.inDown == false && p.pose == 0 && fabsf(p.vel.x) > RUN * 0.5f;
            bool slideJump = p.pose == 1;
            p.vel.y = -JUMP_V * (1 + p.jumpPct / 100.0f) * (hop ? 0.9f : 1.0f);
            if (hop) p.vel.x = dir * std::max(fabsf(p.vel.x), RUN * 1.25f);
            if (slideJump) { float s = p.vel.x > 0 ? 1.0f : -1.0f; p.vel.x = s * std::min(fabsf(p.vel.x) * 1.25f + 60, RUN * 1.7f); p.pose = 0; p.boostT = 0.8f; } // the slide-jump boost
            if (hop) p.boostT = 0.6f;
            if (p.pose == 6) p.pose = 0;
            p.coyote = p.jumpBuffer = 0;
            p.onGround = false;
            p.scale = {0.72f, 1.32f};
            Dust(p, {p.pos.x + PW / 2, p.pos.y + PH}, 5, -p.vel.x / RUN);
            if (water) Bubbles(p, {p.pos.x + PW / 2, p.pos.y + PH - 4}, 5);
            BeastsNoise(p, {p.pos.x + PW / 2, p.pos.y + PH}, 0.2f); // a kick off the deck is heard by anything close
        } else if (p.wallCoyote > 0) {
            float spring = WallIsBarnacle(p, p.lockSide) ? 1.5f : 1.0f; // barnacle springboards fling you 1.5x
            p.vel.x = -p.lockSide * WALLJUMP_VX * spring;
            p.vel.y = -WALLJUMP_VY * spring;
            p.wallLock = WALL_LOCK;
            p.wallCoyote = p.jumpBuffer = 0;
            p.facingRight = p.lockSide < 0;
            p.scale = {0.75f, 1.28f};
            Dust(p, {p.lockSide > 0 ? p.pos.x + PW : p.pos.x, p.pos.y + PH / 2}, 5, (float)-p.lockSide);
        } else if (water && !p.verifying && p.vel.y > -120 && !dashing && p.pose == 0) { // underwater, jump again on the way down: the hydro-glide
            p.pose = 7; p.jumpBuffer = 0; p.scale = {1.2f, 0.9f};
            Bubbles(p, {p.pos.x + PW / 2, p.pos.y + PH / 2}, 4);
        }
    }

    // seaweed, kelp, ratlines - climbable poles: they slow a fall, you can climb up them or slide down them fast
    {
        p.onWeed = false;
        for (int ty = (int)floorf((p.pos.y + 2) / T); ty <= (int)floorf((p.pos.y + PH - 2) / T) && !p.onWeed; ty++)
            for (int tx = (int)floorf((p.pos.x + 3) / T); tx <= (int)floorf((p.pos.x + PW - 3) / T); tx++) if (Climbable(At(p, tx, ty))) { p.onWeed = true; break; }
        if (p.onWeed) { p.coyote = COYOTE; p.dashReady = true; }
    }
    // balancing on a pole's tip: climbing up off the top of a pole leaves you standing on it
    if (p.onWeed && p.climbDir < 0 && !p.verifying) {
        int cx = (int)floorf((p.pos.x + PW / 2) / T), feet = (int)floorf((p.pos.y + PH - 2) / T);
        if (Climbable(At(p, cx, feet)) && !Climbable(At(p, cx, feet - 1)) && !Solid(p, cx, feet - 1) && p.pos.y + PH <= feet * (float)T + 6) {
            p.pose = 6; p.pos.y = feet * (float)T - PH; p.vel.y = 0;
        }
    }
    if (p.pose == 6) {
        int cx = (int)floorf((p.pos.x + PW / 2) / T), below = (int)floorf((p.pos.y + PH + 2) / T);
        if (!Climbable(At(p, cx, below)) || dir != 0 || p.inDown) p.pose = 0; // stepped off it, or slid back down
        else { p.vel = {0, 0}; p.coyote = COYOTE; p.dashReady = true; }
    }
    if (!p.verifying && water && GetRandomValue(0, 160) == 0) // the diver's exhaled air rises from the helmet
        p.particles.push_back({{p.pos.x + PW / 2 + (p.facingRight ? 5.0f : -5.0f), p.pos.y + 3}, {(float)GetRandomValue(-10, 10), -36}, 1.8f, 1.8f, -(float)GetRandomValue(2, 4), Color{196, 236, 250, 255}});
    // a steam vent's column (or a warm current, or an old fountain) lifts whoever is in it - a glider more
    {
        int vx = (int)floorf((p.pos.x + PW / 2) / T), vy = (int)floorf((p.pos.y + PH - 1) / T);
        for (int k = 0; k <= 5; k++)
            if (At(p, vx, vy + k) == 'v') {
                if (VentOn(p, vx)) p.vel.y = std::max(p.vel.y - (p.pose == 7 ? 13000 : 9000) * STEP, -540.0f);
                break;
            }
    }
    // the parachute brake (hold Down while falling) and the hydro-glide (hold jump while falling underwater)
    if (!p.onGround && !dashing && p.pose != 8 && p.pose != 6 && !p.onWeed && !p.wallSide && p.vel.y > 0) {
        if (p.inDown && dir == 0 && (p.pose == 0 || p.pose == 4 || p.pose == 7)) p.pose = 4; // (Down with a direction is the roll's stance instead)
        else if (p.pose == 7 && !jumpHeld) p.pose = 0;      // the glide lasts while jump is held
        else if (p.pose == 4 && (!p.inDown || dir != 0)) p.pose = 0;
    } else if ((p.pose == 4 || p.pose == 7) && (p.onGround || p.onWeed || p.wallSide)) p.pose = 0;

    // gravity is stronger once the jump button is released (short hops) and when falling (snappy arcs)
    if (!dashing && p.pose != 6) {
        float g = p.vel.y < 0 ? (jumpHeld ? GRAV_UP : GRAV_UP_RELEASED) : GRAV_DOWN;
        if (p.pose == 4) g *= 0.2f;   // spread flat against the water: a fraction of the pull
        if (p.pose == 7) g *= 0.35f;  // the glide: gravity cut by 65%
        p.vel.y += g * STEP;
    }
    if (p.pose == 4 && p.vel.y > BRAKE_FALL) p.vel.y -= (p.vel.y - BRAKE_FALL) * std::min(1.0f, 10 * STEP);
    if (p.pose == 7 && p.vel.y > GLIDE_FALL) p.vel.y -= (p.vel.y - GLIDE_FALL) * std::min(1.0f, 8 * STEP);
    if (p.wallSide && p.vel.y > WALL_SLIDE) p.vel.y = std::max(WALL_SLIDE, p.vel.y - 6000 * STEP);
    if (p.onWeed && p.pose != 8) { // hanging on the pole
        if (p.climbDir < 0) p.vel.y = -150.0f;
        else if (p.climbDir > 0) p.vel.y = 380.0f; // sliding down it, fast
        else p.vel.y = std::min(p.vel.y, 60.0f);
        p.vel.x = std::clamp(p.vel.x, -140.0f, 140.0f);
    }
    p.vel.y = std::min(p.vel.y, MAX_FALL);

    bool was = p.onGround, wall;
    float fallSpeed = p.vel.y;
    if (LowPose(p)) { // sliding or rolling, the hitbox is short: the top comes down, the feet stay put
        Vector2 lp{p.pos.x, p.pos.y + LOW_DH};
        MoveAndCollide(p, lp, p.vel, PW, PH - LOW_DH, STEP, p.onGround, wall);
        p.pos = {lp.x, lp.y - LOW_DH};
    } else MoveAndCollide(p, p.pos, p.vel, PW, PH, STEP, p.onGround, wall);
    // movers (a grazing whale's back): land on the top from above, and ride it while you stand there
    if (!p.verifying && !p.movers.empty()) {
        int on = -1;
        float feet = p.pos.y + PH, prevFeet = feet - p.vel.y * STEP;
        for (int k = 0; k < (int)p.movers.size() && p.vel.y >= 0; k++) {
            const auto& m = p.movers[k];
            if (p.pos.x + PW <= m.r.x + 2 || p.pos.x >= m.r.x + m.r.width - 2) continue;
            if (prevFeet <= m.r.y + 6 && feet >= m.r.y - 1) { on = k; break; }
        }
        if (on >= 0) {
            const auto& m = p.movers[on];
            p.pos.y = m.r.y - PH;
            p.vel.y = 0;
            p.onGround = true;
            float nx = p.pos.x + m.vel.x * STEP; // carried along - unless that would push you into a wall
            if (!Solid(p, (int)floorf((m.vel.x > 0 ? nx + PW : nx) / T), (int)floorf((p.pos.y + PH / 2) / T))) p.pos.x = nx;
        }
        p.onMover = on;
    }
    if (p.onGround) p.dashReady = true;
    if (p.onGround && !was) {
        p.scale = {1.0f + std::min(0.35f, fallSpeed / 2400), 1.0f - std::min(0.3f, fallSpeed / 2800)};
        if (fallSpeed > 400) Dust(p, {p.pos.x + PW / 2, p.pos.y + PH}, 6, 0);
        if (fallSpeed > 300 && water) Bubbles(p, {p.pos.x + PW / 2, p.pos.y + PH - 3}, 6, 12);
        bool soft = !p.verifying && BeastsSoftLanding(p); // cannon-moss soaks up the landing: no sound, no stun
        float launch = p.verifying ? 0.0f : BeastsLandingLaunch(p, fallSpeed); // a root-sponge or a drum-fungus throws you back up
        if (launch > 0) { p.vel.y = -launch; p.onGround = false; p.dashReady = true; soft = true; p.scale = {0.8f, 1.25f}; }
        if (launch < 0) soft = true;
        if (fallSpeed > 250 && !soft) BeastsNoise(p, {p.pos.x + PW / 2, p.pos.y + PH}, std::min(1.4f, fallSpeed / 600.0f)); // a hard landing carries a long way
        if (p.pose == 4 || p.pose == 7 || p.pose == 8) p.pose = 0;
        // the impact roll: Down and a direction as you land turn the fall into forward speed ...
        if (launch != 0) {} // (already airborne again, or turned sideways by prism-moss)
        else if (fallSpeed > ROLL_MIN_V && p.downBuf > 0 && dir != 0 && !p.verifying) {
            p.pose = 2; p.moveT = ROLL_T; p.boostT = 0.8f;
            p.vel.x = std::clamp(p.vel.x + dir * 0.7f * fallSpeed, -RUN * 1.8f, RUN * 1.8f);
            p.facingRight = dir > 0;
            Dust(p, {p.pos.x + PW / 2, p.pos.y + PH}, 6, dir);
        } else if (fallSpeed > STUN_V && !p.verifying && !soft) { // ... and without one, a drop taller than any jump leaves you reeling
            bool crumbly = false;
            for (int tx = (int)floorf((p.pos.x + 3) / T); tx <= (int)floorf((p.pos.x + PW - 3) / T); tx++) if (At(p, tx, (int)floorf((p.pos.y + PH + 1) / T)) == 'f') crumbly = true;
            if (!crumbly) { p.pose = 3; p.moveT = STUN_T; p.vel.x = 0; p.scale = {1.35f, 0.7f}; } // (never on scaffolding that's about to give way)
        }
    }
    if (!p.verifying) { // fragile scaffolding: shakes when stood on, and is gone half a second later
        if (p.onGround)
            for (int tx = (int)floorf((p.pos.x + 3) / T); tx <= (int)floorf((p.pos.x + PW - 3) / T); tx++) {
                int ty = (int)floorf((p.pos.y + PH + 1) / T);
                if (At(p, tx, ty) != 'f') continue;
                bool has = false;
                for (auto& c : p.crumbles) has |= c.tx == tx && c.ty == ty;
                if (!has) p.crumbles.push_back({tx, ty, 0});
            }
        for (size_t i = 0; i < p.crumbles.size();) {
            auto& c = p.crumbles[i];
            c.t += STEP;
            if (c.t >= 0.5f) {
                p.tiles[c.ty][c.tx] = '.';
                p.crumbled.push_back({c.tx, c.ty});
                Burst(p, {c.tx * (float)T + 16, c.ty * (float)T + 10}, 8, Color{150, 110, 70, 255}, 140, 0.5f, 3);
                p.crumbles.erase(p.crumbles.begin() + i);
            } else i++;
        }
    }
    p.scale.x += (1 - p.scale.x) * std::min(1.0f, STEP * 14);
    p.scale.y += (1 - p.scale.y) * std::min(1.0f, STEP * 14);
    if (p.onGround) p.runAnim += fabsf(p.vel.x) * STEP * 0.055f;
    if (p.wallSide && p.vel.y > 0 && GetRandomValue(0, 5) == 0 && !p.verifying) { // scraping down the wall: grit and a short streak of sparks
        float wx = p.wallSide > 0 ? p.pos.x + PW : p.pos.x;
        p.particles.push_back({{wx, p.pos.y + PH - 4}, {-p.wallSide * 24.0f, -24}, 0.3f, 0.3f, 2, Color{220, 214, 200, 255}});
        p.particles.push_back({{wx, p.pos.y + PH - 8}, {-p.wallSide * 40.0f, 30}, 0.22f, 0.22f, 2, Color{255, 210, 120, 255}});
    }

    // the exit, hazards and falling out of the level
    Rectangle pr = PlayerBox(p);
    for (int ty = (int)floorf(pr.y / T); ty <= (int)floorf((pr.y + pr.height) / T); ty++)
        for (int tx = (int)floorf(pr.x / T); tx <= (int)floorf((pr.x + pr.width) / T); tx++) {
            char c = At(p, tx, ty);
            if (c == 'E' && p.exitOpen) {
                p.finished = true;
            }
        }
    if (TouchesHazard(p) || p.pos.y > DeathY(p)) Die(p);
}
// ---------------------------------------------------------------- drawing: backgrounds
// Every level's scenery is several layers deep; each layer scrolls at its own speed, so the far
// ones barely move and the near ones sweep past.
float Hs(float x) { float s = sinf(x * 12.9898f + 3.1f) * 43758.5453f; return s - floorf(s); }
float Hs2(float x, float y) { return Hs(x * 7.13f + y * 91.7f); }
constexpr float WATER_LEVEL_Y = 262; // the pirate biome's horizon: the far sea's surface, where background ships and masts stand

// ---------------------------------------------------------------- ambient duct life (the Pipes)
// The Pipes are the one level with no enemies (CLAUDE.md's own call), so this retrofits the ecosystem
// framework's shape - a rolled PersonalityProfile driving Flee/Investigate/Idle - onto life that never
// threatens the diver: little vermin skittering along the duct floor, startled by a close pass, occasionally
// bold enough to creep toward a diver standing still. Same walk-and-turn-at-ledges movement as the crab
// enemy ('c' in UpdateEnemies), just never able to hurt anything.
constexpr float CRITTER_FLEE_R = 68, CRITTER_CURIOUS_R = 150;

static void PopulateCritters(PlatformState& p, unsigned seed) {
    int idx = 0;
    for (int x = 2; x < p.w - 2; x++) {
        int fy = -1;
        for (int y = 1; y < p.h - 1; y++) if (!Solid(p, x, y) && Solid(p, x, y + 1)) { fy = y; break; } // topmost open tile with solid ground beneath it
        if (fy < 0) continue;
        // sparse and seed-varied: not every eligible column gets one, so the ducts don't feel wall-to-wall
        float roll = Hs2((float)x, (float)seed * 3.7f + 1);
        if (roll > 0.14f) continue;
        PlatCritter c;
        c.home = c.pos = {x * (float)T + T / 2.0f, fy * (float)T + T - 3}; // feet near the bottom of the open tile, just above the floor
        c.personality = {Hs2(idx * 3.0f + 1, (float)seed), Hs2(idx * 3.0f + 2, (float)seed), Hs2(idx * 3.0f + 3, (float)seed), Hs2(idx * 3.0f + 4, (float)seed)};
        c.dir = Hs2(idx * 5.0f, (float)seed) > 0.5f ? 1.0f : -1.0f;
        c.phase = Hs2(idx * 9.0f, (float)seed) * 6.28f;
        idx++;
        p.critters.push_back(c);
        if (idx > 40) break; // a generous cap; PopulateEcosystem-style density, not a swarm
    }
}

void UpdateCritters(PlatformState& p, float dt) {
    if (p.critters.empty()) return;
    Vector2 pc{p.pos.x + PW / 2, p.pos.y + PH / 2};
    bool diverStill = fabsf(p.vel.x) < 6 && fabsf(p.vel.y) < 6;
    for (auto& c : p.critters) {
        c.phase += dt;
        float dx = pc.x - c.pos.x, dy = pc.y - c.pos.y, dist = sqrtf(dx * dx + dy * dy);
        CritterState want = CritterState::Idle;
        // braver/less curious vermin tolerate a closer approach before bolting, mirroring the Abyss's
        // personality-driven curiosity/bravery thresholds rather than one fixed radius for every one of them
        float fleeR = CRITTER_FLEE_R * (0.6f + c.personality.bravery * 0.8f);
        if (dist < fleeR) want = CritterState::Fleeing;
        else if (diverStill && dist < CRITTER_CURIOUS_R && c.personality.curiosity > 0.55f) want = CritterState::Investigating;
        if (want != c.state) { c.state = want; c.stateTimer = 0; }
        c.stateTimer += dt;

        float speedMag = 0; // set the facing direction first, then walk in it - turning back at a wall or a
                             // ledge exactly like the crab enemy does, so a fleeing critter can't run off a ledge
        if (c.state == CritterState::Fleeing) { speedMag = 50 + c.personality.energy * 70; c.dir = dx < 0 ? 1.0f : -1.0f; }
        else if (c.state == CritterState::Investigating) { speedMag = 22 + c.personality.energy * 18; c.dir = dx < 0 ? -1.0f : 1.0f; }
        else if (fmodf(c.phase, 3.0f) > 2.2f) speedMag = 14; // idle: a short skitter, then a long pause
        if (speedMag > 0) {
            float nx = c.pos.x + c.dir * speedMag * dt;
            int ftx = (int)floorf((c.dir > 0 ? nx + 6 : nx - 6) / T), fty = (int)floorf((c.pos.y - 2) / T);
            if (Solid(p, ftx, fty) || !Solid(p, ftx, fty + 1)) c.dir = -c.dir;
            else c.pos.x = nx;
            // never stray far from home when idly skittering - it's dressing, not a migration
            if (c.state == CritterState::Idle && fabsf(c.pos.x - c.home.x) > 40) c.dir = c.home.x < c.pos.x ? -1.0f : 1.0f;
        }
    }
}

// Small, flat, unrotated (the Pipes' 2px pixel-art grid has no rotation on sprites) - a scurrying silhouette
// with a wobbling pair of legs/antennae, just enough to read as alive under the helmet lamp.
void DrawCritter(const PlatCritter& c, float t) {
    float bob = sinf(t * 14 + c.phase) * (c.state == CritterState::Idle ? 0.4f : 1.2f);
    int x = (int)(c.pos.x - c.dir * 3), y = (int)(c.pos.y + bob);
    Color body = c.state == CritterState::Fleeing ? Color{168, 96, 70, 255} : Color{120, 92, 74, 255};
    DrawRectangle(x - 4, y - 3, 8, 4, body);
    DrawRectangle(x + (c.dir > 0 ? 3 : -5), y - 4, 2, 2, body); // a small raised head at the leading edge
    float legPh = t * (c.state == CritterState::Idle ? 4.0f : 12.0f) + c.phase;
    for (int k = -1; k <= 1; k += 2) {
        int ly = y + 1 + (sinf(legPh + k) > 0 ? 0 : 1);
        DrawRectangle(x - 2, ly, 1, 2, Fade(BLACK, 0.6f));
        DrawRectangle(x + 2, ly, 1, 2, Fade(BLACK, 0.6f));
    }
}

// Simple walk-and-turn-at-ledges patrol, shared by every wandering species here (Shrimp/Puffer/Hermit, and
// the Pirate Ship's Rat/Cat/Monkey/GuardDog below) - the same shape as the crab enemy and the Pipes'
// critters, just parameterised by speed and a home leash. Templated since PlatEcoLife and PlatPirateLife
// are separate structs that both happen to carry pos/dir/home.
template <class Ent>
static void EcoWander(PlatformState& p, Ent& e, float speed, float leash) {
    if (speed <= 0) return;
    float nx = e.pos.x + e.dir * speed;
    int ftx = (int)floorf((e.dir > 0 ? nx + 6 : nx - 6) / T), fty = (int)floorf((e.pos.y - 2) / T);
    if (Solid(p, ftx, fty) || !Solid(p, ftx, fty + 1)) e.dir = -e.dir;
    else e.pos.x = nx;
    if (fabsf(e.pos.x - e.home.x) > leash) e.dir = e.home.x < e.pos.x ? -1.0f : 1.0f;
}

template <typename F>
void Layer(float cx, float depth, float gap, float cw, F fn) {
    float off = cx * depth;
    float first = floorf(off / gap) * gap;
    for (float wx = first - gap; wx < off + cw + gap; wx += gap) fn(wx - off, wx);
}

// ---------------------------------------------------------------- the background, in three parallax tiers
// Each zone has a FAR layer (the deep pipe matrix / the dark trench and its light / the night sky), a MID layer (out-of-focus
// mains and plating / faded coral and kelp / distant ships and the sea), and the playable layer at 1.0x scroll. A FORE layer
// of near silhouettes drawn in front of the diver adds the last of the depth.
struct ParallaxLayer {
    float scrollSpeedX = 0.2f, scrollSpeedY = 0.2f;   // how far it moves for each pixel the camera does
    std::function<void(const PlatformState&, float t, float ox, float oy)> draw;
};
class BackgroundSystem {
public:
    ParallaxLayer farLayer, midLayer, foreLayer;
    int level = -1;
    void Setup(int lv);
    void RenderBack(const PlatformState& p, float t) {
        if (level != p.level) Setup(p.level);
        float cx = p.camX * ZOOM, cy = p.camY * ZOOM;
        farLayer.draw(p, t, cx * farLayer.scrollSpeedX, cy * farLayer.scrollSpeedY);
        midLayer.draw(p, t, cx * midLayer.scrollSpeedX, cy * midLayer.scrollSpeedY);
    }
    void RenderFore(const PlatformState& p, float t) {
        if (level != p.level) Setup(p.level);
        float cx = p.camX * ZOOM, cy = p.camY * ZOOM;
        foreLayer.draw(p, t, cx * foreLayer.scrollSpeedX, cy * foreLayer.scrollSpeedY);
    }
};

void BackgroundSystem::Setup(int lv) {
    level = lv;
    const float cw = PIXEL_W + 2.0f, ch = PIXEL_H + 2.0f;
    if (lv == PL_PIPES) {
        // Three tiers: far steel plating (0.2x scroll, 35% bright), mid pipework (0.5x, 55%), then the playable tiles at 1.0x.
        // Background colours are pulled halfway to grey and dimmed, so nothing behind the diver competes with what he can land on.
        auto dim = [](Color c, float k) {
            float l = c.r * 0.3f + c.g * 0.59f + c.b * 0.11f;
            return Color{(unsigned char)((c.r * 0.5f + l * 0.5f) * k), (unsigned char)((c.g * 0.5f + l * 0.5f) * k), (unsigned char)((c.b * 0.5f + l * 0.5f) * k), 255};
        };
        farLayer = {0.2f, 0.2f, [cw, ch, dim](const PlatformState& p, float t, float ox, float oy) {   // riveted steel plating, lost in the gloom
            (void)p; (void)t;
            const float B = 0.46f;
            DrawRectangle(0, 0, (int)cw, (int)ch, dim(Color{60, 52, 46, 255}, B));
            float px0 = fmodf(ox, 64), py0 = fmodf(oy + 64000, 64);
            for (float y = -py0 - 64; y < ch; y += 64)
                for (float x = -px0 - 64; x < cw; x += 64) {
                    int gx = (int)floorf((x + ox) / 64 + 0.5f), gy = (int)floorf((y + oy) / 64 + 0.5f);
                    float h = Hs(gx * 1.7f + gy * 5.3f);
                    DrawRectangle((int)x + 1, (int)y + 1, 62, 62, dim(Color{(unsigned char)(80 + h * 26), (unsigned char)(70 + h * 22), (unsigned char)(62 + h * 18), 255}, B));
                    DrawRectangle((int)x + 1, (int)y + 1, 62, 2, dim(Color{118, 104, 92, 255}, B));
                    DrawRectangle((int)x + 1, (int)y + 61, 62, 2, dim(Color{40, 34, 30, 255}, B));
                    for (int k = 0; k < 4; k++) DrawRectangle((int)x + 6 + (k % 2) * 50, (int)y + 6 + (k / 2) * 50, 3, 3, dim(Color{140, 122, 104, 255}, B));
                    if (h > 0.78f) DrawRectangle((int)x + 14, (int)y + 24, 36, 5, dim(Color{20, 18, 16, 255}, B));   // a vent slot
                    else if (h < 0.16f) { DrawRectangle((int)x + 10, (int)y + 20, 44, 22, dim(Color{50, 44, 40, 255}, B)); DrawRectangle((int)x + 14, (int)y + 24, 36, 14, dim(Color{90, 78, 66, 255}, B)); }   // a bolted access panel
                }
        }};
        midLayer = {0.5f, 0.5f, [cw, ch, dim](const PlatformState& p, float t, float ox, float oy) {   // pipework: mains, T-joints, valves and trusses
            (void)p;
            const float B = 0.72f;
            Color pipe = dim(Color{176, 104, 62, 255}, B), pipeHi = dim(Color{222, 148, 96, 255}, B), pipeLo = dim(Color{92, 52, 32, 255}, B), brass = dim(Color{184, 140, 60, 255}, B), iron = dim(Color{70, 60, 54, 255}, B);
            // horizontal runs, with trusses between the upper and lower ones
            const float rowGap = 210;
            for (int k = -1; k < (int)(ch / rowGap) + 2; k++) {
                float y = fmodf(k * rowGap - oy + 42000, rowGap * 4) - rowGap;
                DrawRectangle(0, (int)y, (int)cw, 14, pipe);
                DrawRectangle(0, (int)y + 2, (int)cw, 3, pipeHi);
                DrawRectangle(0, (int)y + 11, (int)cw, 3, pipeLo);
            }
            Layer(ox, 1.0f, 120, cw, [&](float x, float wx) {   // truss braces hung between two horizontal runs
                if (Hs(wx * 0.31f + 4) < 0.55f) return;
                float y0 = fmodf(-oy + 42000, rowGap * 4) - rowGap;
                for (int r = 0; r < 4; r++) {
                    float ya = y0 + r * rowGap + 14, yb = ya + rowGap - 14;
                    if (ya > ch || yb < 0) continue;
                    DrawLineEx({x, ya}, {x + 120, yb}, 4, iron); DrawLineEx({x + 120, ya}, {x, yb}, 4, iron);
                    DrawRectangle((int)x - 3, (int)ya, 6, (int)(yb - ya), iron); DrawRectangle((int)x + 117, (int)ya, 6, (int)(yb - ya), iron);
                }
            });
            Layer(ox, 1.0f, 240, cw, [&](float x, float wx) {   // vertical mains dropping through the runs: flanged T-joints, valve wheels
                if (Hs(wx * 0.17f + 9) < 0.35f) return;
                DrawRectangle((int)x, 0, 24, (int)ch, pipe);
                DrawRectangle((int)x + 3, 0, 4, (int)ch, pipeHi);
                DrawRectangle((int)x + 19, 0, 3, (int)ch, pipeLo);
                float y0 = fmodf(-oy + 42000, rowGap * 4) - rowGap;
                for (int r = 0; r < 4; r++) {
                    float y = y0 + r * rowGap;
                    DrawRectangle((int)x - 5, (int)y - 4, 34, 6, brass); DrawRectangle((int)x - 5, (int)y + 12, 34, 6, brass);   // T-joint flanges
                }
                float vy = fmodf(-oy * 0.9f + Hs(wx) * 700 + 42000, 460) - 30;
                DrawRectangle((int)x + 10, (int)vy - 8, 4, 16, iron);
                DrawCircle((int)x + 12, (int)vy, 11, iron); DrawCircle((int)x + 12, (int)vy, 8, dim(Color{150, 50, 40, 255}, B)); DrawCircle((int)x + 12, (int)vy, 3, iron);
            });
            Layer(ox, 1.0f, 520, cw, [&](float x, float wx) {   // slow machinery behind the pipes: great gears, and dials glowing amber
                float gy = 90 + Hs(wx * 0.23f) * (ch - 260);
                float dir = Hs(wx * 0.7f) > 0.5f ? 1.0f : -1.0f;
                DrawGear({x, gy}, 46, 14, t * 0.5f * dir + wx, dim(Color{150, 116, 60, 255}, B));
                DrawGear({x + 66, gy + 20}, 26, 9, -t * 0.9f * dir + wx, dim(Color{176, 104, 62, 255}, B));
                DrawCircle((int)x, (int)gy, 9, dim(Color{40, 34, 30, 255}, B));
                if (Hs(wx * 1.9f) > 0.45f) { // a pressure dial with a trembling needle
                    Vector2 dc{x + 150, gy - 40};
                    DrawCircleV(dc, 20, dim(Color{40, 34, 30, 255}, B)); DrawCircleV(dc, 16, dim(Color{176, 158, 120, 255}, B));
                    float na = -2.3f + 1.6f * (0.5f + 0.5f * sinf(t * 0.7f + wx)) + sinf(t * 23) * 0.03f;
                    DrawLineEx(dc, {dc.x + cosf(na) * 13, dc.y + sinf(na) * 13}, 2, Color{170, 30, 24, 255});
                    DrawCircleV(dc, 2.5f, Color{20, 16, 14, 255});
                }
            });
            Layer(ox, 1.0f, 190, cw, [&](float x, float wx) {   // chains and cables hanging from the ceiling
                if (Hs(wx * 0.9f + 2) < 0.5f) return;
                float len = 60 + Hs(wx) * 200, sway = sinf(t * 0.8f + wx) * 3;
                for (float yy = 0; yy < len; yy += 9) DrawRectangle((int)(x + sway * yy / len), (int)yy, 4, 6, dim(Color{90, 80, 70, 255}, B + 0.15f));
            });            Layer(ox, 1.0f, 340, cw, [&](float x, float wx) {   // something watches from a gap, then isn't there
                float cyc = fmodf(t * 0.11f + Hs(wx + 20) * 9, 9.0f);
                if (cyc > 1.6f) return;
                float a = std::min(1.0f, cyc * 4) * std::min(1.0f, (1.6f - cyc) * 4);
                float y = 140 + Hs(wx + 21) * (ch - 280);
                for (int s = -1; s <= 1; s += 2) DrawCircleV({x + s * 4.0f, y}, 1.4f, Fade(Color{220, 60, 50, 255}, a * 0.6f));
            });
            if (fmodf(t * 0.6f + oy * 0.002f, 5.0f) < 0.12f) DrawRectangle(0, 0, (int)cw, (int)ch, Fade(BLACK, 0.25f));   // the lamp flicker
        }};
        foreLayer = {1.3f, 1.3f, [cw, ch](const PlatformState& p, float t, float ox, float oy) { // brackets, valves and steam very near the lens
            (void)p;
            Layer(ox / 1.3f, 1.3f, 330, cw, [&](float x, float wx) {
                float len = 40 + Hs(wx) * 50;
                DrawRectangle((int)x, 30, 12, (int)len, Color{10, 9, 9, 255}); DrawRectangle((int)x - 5, 30 + (int)len - 8, 22, 12, Color{10, 9, 9, 255});  // a hanging pipe with a flange
                DrawCircle((int)x + 6, 30 + (int)len + 12, 8, Color{10, 9, 9, 255}); DrawRectangle((int)x + 4, 30 + (int)len + 4, 4, 16, Color{10, 9, 9, 255});
            });
            for (int k = 0; k < 6; k++) { // steam wisps drifting through the frame
                float ph = fmodf(t * 0.08f + k * 0.21f, 1.0f), wx0 = fmodf(k * 231.0f - ox * 0.6f + 8000, cw + 200) - 100;
                DrawEllipse((int)wx0, (int)(ch - ph * ch), 60 + ph * 40, 12 + ph * 16, Fade(Color{200, 200, 196, 255}, 0.05f * (1 - ph)));
            }
            (void)oy;
        }};
    } else if (lv == PL_HULL) {
        farLayer = {0.1f, 0.1f, [cw, ch](const PlatformState& p, float t, float ox, float oy) { // the dark trench and the light that reaches it
            (void)p; (void)oy;
            DrawRectangleGradientV(0, 0, (int)cw, (int)ch, Color{16, 60, 88, 255}, Color{3, 12, 26, 255});
            for (int x = 0; x < (int)cw; x += 3) DrawRectangle(x, 29, 3, (int)(4 + sinf(x * 0.08f + t * 1.5f) * 2 + 2), Color{120, 190, 210, 80});   // the surface, far above
            BeginBlendMode(BLEND_ADDITIVE);
            Layer(ox / 0.1f, 0.1f, 140, cw, [&](float x, float wx) {
                float w = 18 + Hs(wx) * 16;
                DrawTri({x, 30}, {x + w, 30}, {x - 70, ch}, Color{60, 130, 160, 26});
                DrawTri({x + w, 30}, {x - 70 + w * 1.6f, ch}, {x - 70, ch}, Color{60, 130, 160, 26});
            });
            EndBlendMode();
            float wx0 = fmodf(900 - t * 14 - ox * 0.6f + 4000, cw + 700) - 350;   // a great whale far off, crossing slowly through the blue
            DrawEllipse((int)wx0, 150, 150, 26, Color{8, 26, 40, 255});
            DrawTri({wx0 + 120, 150}, {wx0 + 210, 118}, {wx0 + 200, 176}, Color{8, 26, 40, 255});
            DrawTri({wx0 - 40, 168}, {wx0 + 30, 168}, {wx0 - 10, 204}, Color{8, 26, 40, 255});            Layer(ox / 0.1f, 0.1f, 300, cw, [&](float x, float wx) { // the ribs of a great sunken hull, far off
                for (int k = 0; k < 5; k++) DrawRing({x + k * 30.0f, ch + 20}, 80 + Hs(wx) * 30, 84 + Hs(wx) * 30, 200, 340, 18, Color{10, 30, 42, 255});
            });
        }};
        midLayer = {0.35f, 0.35f, [cw, ch](const PlatformState& p, float t, float ox, float oy) { // faded coral columns, kelp and marine snow
            (void)p; (void)oy;
            for (int tier = 0; tier < 2; tier++) { // two ranks of far coral pillars, the near rank a little clearer
                float sp = 0.6f + tier * 0.5f, gap = 150 - tier * 30;
                Layer(ox / 0.35f, 0.35f * sp, gap, cw, [&](float x, float wx) {
                    float h = 140 + Hs(wx * 1.3f + tier) * 220, w = 30 + Hs(wx + 4) * 30;
                    Color c = tier ? Color{18, 44, 60, 255} : Color{12, 34, 48, 255};
                    DrawRectangle((int)x, (int)(ch - h), (int)w, (int)h + 2, c);                                      // a pillar of coral
                    for (int k = 0; k < 4; k++) DrawCircle((int)(x + k * w / 3), (int)(ch - h) + (k % 2) * 6, 8 + Hs(wx + k) * 6, c);   // its lumpy crown
                    DrawRectangle((int)x + 3, (int)(ch - h) + 6, 3, (int)h - 6, tier ? Color{34, 74, 90, 255} : Color{24, 56, 70, 255});  // a faded lit edge
                    for (int k = 0; k < 3; k++) DrawCircle((int)(x + 4 + Hs(wx * 2 + k) * (w - 8)), (int)(ch - h * (0.2f + 0.25f * k)), 2.5f, Fade(Color{190, 96, 120, 255}, tier ? 0.55f : 0.35f)); // polyps
                });
            }
            Layer(ox / 0.35f, 0.5f, 70, cw, [&](float x, float wx) { // a kelp forest
                Vector2 prev{x, ch};
                int n = 10 + (int)(Hs(wx) * 8);
                for (int s = 1; s <= n; s++) {
                    Vector2 q{x + sinf(t * 0.9f + wx + s * 0.45f) * s * 1.5f, ch - s * 14.0f};
                    DrawLineEx(prev, q, 3.5f - s * 0.12f, Color{16, 64, 52, 255});
                    prev = q;
                }
            });
            for (int k = 0; k < 40; k++) { // marine snow, falling slowly
                float sx2 = fmodf(k * 97.0f - ox * 0.9f + 9000, cw), sy2 = fmodf(k * 53.0f + t * (8 + k % 5 * 3), ch);
                DrawRectangle((int)sx2, (int)sy2, 1, 1, Fade(Color{190, 220, 230, 255}, 0.45f));
            }
        }};
        foreLayer = {1.35f, 1.2f, [cw, ch](const PlatformState& p, float t, float ox, float oy) { // near kelp and coral, dark against the lens
            (void)p; (void)oy;
            Layer(ox / 1.35f, 1.35f, 520, cw, [&](float x, float wx) {
                // only at the edges of the lens (kelp across the middle hides the platforms), fading rather than popping
                float edge = std::clamp((fabsf(x - cw * 0.5f) - cw * 0.3f) / (cw * 0.13f), 0.0f, 1.0f);
                if (edge <= 0.01f) return;
                Vector2 prev{x, ch + 4};
                for (int s = 1; s <= 8; s++) {
                    Vector2 q{x + sinf(t * 0.8f + wx + s * 0.5f) * s * 2.2f, ch - s * 20.0f * (0.6f + Hs(wx) * 0.5f)};
                    DrawLineEx(prev, q, 8.0f - s * 0.85f, Fade(Color{4, 12, 14, 255}, 0.88f * edge));
                    prev = q;
                }
            });
            for (int k = 0; k < 8; k++) { float bx = fmodf(k * 173.0f - ox * 0.5f + 9000, cw + 100), by = ch - fmodf(t * (16 + k * 4) + k * 60, ch); DrawCircleLines((int)bx, (int)by, 3 + k % 3, Fade(Color{190, 230, 250, 255}, 0.4f)); }
        }};
    } else if (lv == PL_ISLAND) {
        farLayer = {0.12f, 0.12f, [cw, ch](const PlatformState& p, float t, float ox, float oy) { // a hazy sky, and a ridge of carved idols far off - the skyline reads as a shrine ground, not a forest
            (void)p; (void)t; (void)oy;
            DrawRectangleGradientV(0, 0, (int)cw, (int)ch, Color{150, 190, 150, 255}, Color{86, 130, 96, 255});
            DrawCircle((int)(cw * 0.7f), 70, 34, Color{230, 220, 170, 220}); // a hazy sun
            Layer(ox / 0.12f, 0.12f, 260, cw, [&](float x, float wx) { // a low canopy hummock behind every other idol, so the ridge doesn't read as bare rock
                float h = 30 + Hs(wx) * 30;
                DrawTri({x - 50, ch}, {x + 50, ch}, {x, ch - h}, Color{70, 108, 78, 255});
            });
            Layer(ox / 0.12f + 90, 0.12f, 260, cw, [&](float x, float wx) { // a monolithic idol head - the dominant silhouette on the skyline
                if (Hs(wx) < 0.4f) return;
                float h = 90 + Hs(wx + 5) * 70, w = 26 + Hs(wx + 9) * 12;
                Color stone{62, 58, 62, 255};
                DrawRectangle((int)(x - w / 2), (int)(ch - h), (int)w, (int)h, stone);              // the monolith's shaft
                DrawEllipse((int)x, (int)(ch - h), w * 0.7f, w * 0.55f, stone);                       // a carved head atop it
                DrawTri({x - w * 0.5f, ch - h - w * 0.3f}, {x + w * 0.5f, ch - h - w * 0.3f}, {(float)x, ch - h - w * 0.9f}, stone); // a peaked headdress
                if (Hs(wx + 2) > 0.5f) { DrawCircle((int)x - 4, (int)(ch - h), 2, Color{40, 36, 40, 200}); DrawCircle((int)x + 4, (int)(ch - h), 2, Color{40, 36, 40, 200}); } // deep-set eye hollows, only sometimes visible at this distance
            });
        }};
        midLayer = {0.4f, 0.4f, [cw, ch](const PlatformState& p, float t, float ox, float oy) { // standing stone idols among the trees - carved worship sites, not bare jungle
            (void)p; (void)oy;
            Layer(ox / 0.4f, 0.4f, 150, cw, [&](float x, float wx) {
                bool idol = Hs(wx + 11) > 0.45f; // roughly half the mid-ground silhouettes are carved stone, not trees
                float h = ch * (0.45f + Hs(wx) * 0.4f), w = idol ? 20 + Hs(wx + 2) * 8 : 14 + Hs(wx + 2) * 10;
                if (idol) {
                    Color stone{72, 66, 60, 255}, stoneDk{48, 44, 40, 255};
                    DrawRectangle((int)x, (int)(ch - h), (int)w, (int)h + 4, stone);
                    DrawRectangle((int)x + 2, (int)(ch - h), 3, (int)h, stoneDk);                      // a shadowed carved seam
                    for (int k = 0; k < 3; k++) DrawRectangle((int)x + 3, (int)(ch - h) + 14 + k * 26, (int)w - 6, 3, stoneDk); // banded glyph rings, stacked like a totem
                    DrawEllipse((int)(x + w / 2), (int)(ch - h) - 8, w * 0.55f, 9, stone);             // a broad carved brow atop the pillar
                    DrawCircle((int)(x + w / 2 - w * 0.2f), (int)(ch - h) - 6, 2.2f, stoneDk); DrawCircle((int)(x + w / 2 + w * 0.2f), (int)(ch - h) - 6, 2.2f, stoneDk); // eye hollows
                } else {
                    DrawRectangle((int)x, (int)(ch - h), (int)w, (int)h + 4, Color{54, 44, 30, 255});
                    DrawRectangle((int)x + 3, (int)(ch - h), 3, (int)h, Color{74, 60, 40, 255});
                    float sway = sinf(t * 0.7f + wx) * 8;
                    DrawCircle((int)(x + w / 2 + sway), (int)(ch - h) - 26, 34 + Hs(wx + 3) * 14, Color{58, 96, 50, 255}); // a canopy crown, swaying
                    DrawCircle((int)(x + w / 2 + sway * 1.3f), (int)(ch - h) - 30, 20, Color{74, 118, 62, 255});
                }
            });
            for (int k = 0; k < 26; k++) { // drifting pollen/spores, or incense smoke curling off an unseen offering
                float sx2 = fmodf(k * 83.0f - ox * 0.8f + 9000, cw), sy2 = fmodf(k * 61.0f + t * (6 + k % 4 * 2), ch);
                DrawRectangle((int)sx2, (int)sy2, 1, 1, Fade(Color{230, 220, 160, 255}, 0.4f));
            }
        }};
        foreLayer = {1.3f, 1.15f, [cw, ch](const PlatformState& p, float t, float ox, float oy) { // hanging vines and near leaves, dark against the lens
            (void)p; (void)oy;
            Layer(ox / 1.3f, 1.3f, 260, cw, [&](float x, float wx) {
                // they frame the edges of the lens and fade out toward the middle - smoothly, so nothing pops in or out as you walk
                float edge = std::clamp((fabsf(x - cw * 0.5f) - cw * 0.28f) / (cw * 0.14f), 0.0f, 1.0f);
                if (edge <= 0.01f) return;
                float len = 60 + Hs(wx) * 120, sway = sinf(t * 0.6f + wx) * 10;
                for (float yy = 0; yy < len; yy += 10) DrawRectangle((int)(x + sway * yy / len), (int)yy, 4, 8, Fade(Color{20, 30, 14, 255}, edge));
                DrawEllipse((int)(x + sway), (int)len, 14, 8, Fade(Color{16, 26, 12, 255}, edge));
            });
        }};
    } else if (lv == PL_WEEDS) {
        farLayer = {0.1f, 0.1f, [cw, ch](const PlatformState& p, float t, float ox, float oy) { // sunlit green water: shafts of light from the surface, the forest fading into haze
            (void)p; (void)oy;
            DrawRectangleGradientV(0, 0, (int)cw, (int)ch, Color{96, 170, 150, 255}, Color{22, 70, 70, 255});
            for (int k = 0; k < 7; k++) { // god-rays, slanting and slowly shifting
                float x0 = fmodf(k * 211.0f - ox * 0.05f + t * 6 + 9000, cw + 300) - 150, w = 40 + (k % 3) * 25;
                DrawTri({x0, 0}, {x0 + w, 0}, {x0 + w * 0.5f + 160, ch}, Fade(Color{220, 250, 210, 255}, 0.07f + 0.03f * sinf(t * 0.5f + k)));
            }
            Layer(ox / 0.1f, 0.1f, 60, cw, [&](float x, float wx) { // the far forest, a wall of pale stalks
                float h = ch * (0.5f + Hs(wx) * 0.45f);
                DrawRectangle((int)x, (int)(ch - h), 5, (int)h, Color{44, 104, 90, 255});
            });
        }};
        midLayer = {0.4f, 0.4f, [cw, ch](const PlatformState& p, float t, float ox, float oy) { // tall kelp with its floats, a school of fish drifting through, rock stacks
            (void)p; (void)oy;
            Layer(ox / 0.4f, 0.4f, 180, cw, [&](float x, float wx) { // sea-stacks of reef rock
                if (Hs(wx + 7) < 0.55f) return;
                float h = ch * (0.25f + Hs(wx) * 0.35f), w = 50 + Hs(wx + 1) * 40;
                DrawRectangle((int)x, (int)(ch - h), (int)w, (int)h, Color{30, 64, 62, 255});
                DrawRectangle((int)x, (int)(ch - h), (int)w, 6, Color{110, 120, 90, 255});
            });
            Layer(ox / 0.4f, 0.4f, 120, cw, [&](float x, float wx) {
                Vector2 prev{x, ch};
                int n = 18 + (int)(Hs(wx) * 14);
                for (int s = 1; s <= n; s++) {
                    Vector2 q{x + sinf(t * 0.8f + wx + s * 0.3f) * s * 0.9f, ch - s * 18.0f};
                    DrawLineEx(prev, q, 4.0f, Fade(Color{36, 96, 60, 255}, 0.55f));
                    if (s % 4 == 0) { DrawEllipse((int)q.x + 5, (int)q.y, 4, 3, Color{120, 110, 50, 255}); DrawTri(q, {q.x + 14, q.y - 6}, {q.x + 4, q.y + 4}, Color{50, 120, 70, 255}); }
                    prev = q;
                }
            });
            for (int k = 0; k < 12; k++) { // a school of small fish wheeling far off
                float sx2 = fmodf(k * 23.0f - ox * 0.3f + t * 30 + 9000, cw + 200) - 100, sy2 = ch * 0.3f + sinf(t * 0.7f + k * 0.4f) * 30 + (k % 4) * 8;
                DrawEllipse((int)sx2, (int)sy2, 4, 1.5f, Fade(Color{200, 230, 220, 255}, 0.5f));
            }
        }};
        foreLayer = {1.3f, 1.15f, [cw, ch](const PlatformState& p, float t, float ox, float oy) { // near kelp fronds at the edges of the lens
            (void)p; (void)oy;
            Layer(ox / 1.3f, 1.3f, 300, cw, [&](float x, float wx) {
                float edge = std::clamp((fabsf(x - cw * 0.5f) - cw * 0.3f) / (cw * 0.13f), 0.0f, 1.0f);
                if (edge <= 0.01f) return;
                Vector2 prev{x, ch + 4};
                for (int s = 1; s <= 12; s++) {
                    Vector2 q{x + sinf(t * 0.7f + wx + s * 0.4f) * s * 2.0f, ch - s * 26.0f * (0.6f + Hs(wx) * 0.5f)};
                    DrawLineEx(prev, q, 10.0f - s * 0.5f, Fade(Color{8, 30, 20, 255}, 0.85f * edge));
                    prev = q;
                }
            });
        }};
    } else if (lv == PL_ATLANTIS) {
        farLayer = {0.06f, 0.06f, [cw, ch](const PlatformState& p, float t, float ox, float oy) { // the deep: a pale eye of light far above, and the skyline of the drowned city
            (void)p; (void)oy;
            DrawRectangleGradientV(0, 0, (int)cw, (int)ch, Color{20, 34, 58, 255}, Color{4, 8, 16, 255});
            DrawCircle((int)(cw * 0.62f), 70, 60, Fade(Color{170, 210, 230, 255}, 0.10f)); DrawCircle((int)(cw * 0.62f), 70, 26, Fade(Color{200, 230, 240, 255}, 0.16f)); // the pale eye
            Layer(ox / 0.06f, 0.06f, 140, cw, [&](float x, float wx) { // domes, towers and stepped temples
                float h = ch * (0.25f + Hs(wx) * 0.35f), w = 40 + Hs(wx + 1) * 60;
                Color c{14, 24, 40, 255};
                int kind = (int)(Hs(wx + 5) * 3);
                DrawRectangle((int)x, (int)(ch - h), (int)w, (int)h, c);
                if (kind == 0) DrawCircle((int)(x + w / 2), (int)(ch - h), w * 0.45f, c);                                         // a dome
                else if (kind == 1) { DrawRectangle((int)(x + w * 0.35f), (int)(ch - h - 60), (int)(w * 0.3f), 60, c); DrawTri({x + w * 0.3f, ch - h - 60}, {x + w * 0.7f, ch - h - 60}, {x + w * 0.5f, ch - h - 90}, c); } // a spire
                else for (int k = 1; k <= 3; k++) DrawRectangle((int)(x + k * 6), (int)(ch - h - k * 14), (int)(w - k * 12), 14, c); // a stepped temple
                if (Hs(wx + 9) > 0.6f) DrawRectangle((int)(x + w / 2 - 2), (int)(ch - h * 0.6f), 4, 6, Fade(Color{120, 220, 240, 255}, 0.35f + 0.15f * sinf(t + wx))); // a window still glowing
            });
        }};
        midLayer = {0.35f, 0.35f, [cw, ch](const PlatformState& p, float t, float ox, float oy) { // nearer ruins: colonnades, broken arches and fallen statues
            (void)p; (void)oy;
            Layer(ox / 0.35f, 0.35f, 120, cw, [&](float x, float wx) {
                Color c{24, 36, 54, 255}, lt{40, 56, 78, 255};
                int kind = (int)(Hs(wx) * 3);
                if (kind == 0) { // a column, whole or broken
                    float h = ch * (0.3f + Hs(wx + 2) * 0.4f);
                    DrawRectangle((int)x, (int)(ch - h), 22, (int)h, c); DrawRectangle((int)x + 3, (int)(ch - h), 3, (int)h, lt);
                    DrawRectangle((int)x - 4, (int)(ch - h), 30, 8, c);
                } else if (kind == 1) { // an arch
                    float h = ch * 0.45f;
                    DrawRectangle((int)x, (int)(ch - h), 16, (int)h, c); DrawRectangle((int)x + 70, (int)(ch - h), 16, (int)h, c);
                    DrawRing({x + 43, ch - h}, 27, 43, 180, 360, 16, c);
                } else { // a fallen statue''s head, half buried
                    DrawCircle((int)x + 30, (int)(ch - 20), 34, c);
                    DrawRectangle((int)x + 14, (int)(ch - 30), 10, 4, lt); DrawRectangle((int)x + 36, (int)(ch - 30), 10, 4, lt);
                }
            });
            for (int k = 0; k < 30; k++) { // slow motes of silt glinting in the glyph-light
                float sx2 = fmodf(k * 89.0f - ox * 0.6f + 9000, cw), sy2 = fmodf(k * 47.0f + t * (4 + k % 3 * 2), ch);
                DrawRectangle((int)sx2, (int)sy2, 1, 1, Fade(Color{160, 220, 240, 255}, 0.35f));
            }
        }};
        foreLayer = {1.3f, 1.15f, [cw, ch](const PlatformState& p, float t, float ox, float oy) { // broken column shafts framing the lens
            (void)p; (void)t; (void)oy;
            Layer(ox / 1.3f, 1.3f, 340, cw, [&](float x, float wx) {
                float edge = std::clamp((fabsf(x - cw * 0.5f) - cw * 0.3f) / (cw * 0.13f), 0.0f, 1.0f);
                if (edge <= 0.01f) return;
                float h = (120 + Hs(wx) * 200) * edge;
                DrawRectangle((int)x - 20, (int)(ch - h), 40, (int)h, Color{4, 6, 10, 255});
                DrawTri({x - 20, ch - h}, {x + 20, ch - h}, {x + 6, ch - h - 18 * edge}, Color{4, 6, 10, 255});
            });
        }};
    } else if (lv == PL_CAVE) {        farLayer = {0.1f, 0.1f, [cw, ch](const PlatformState& p, float t, float ox, float oy) { // near-black rock strata, lost past the lamp's reach
            (void)p; (void)t; (void)oy;
            DrawRectangleGradientV(0, 0, (int)cw, (int)ch, Color{8, 18, 24, 255}, Color{2, 5, 8, 255}); // black water, the faintest teal where light once reached
            Layer(ox / 0.1f, 0.1f, 160, cw, [&](float x, float wx) {
                float h = 60 + Hs(wx) * 160;
                DrawTri({x - 50, 0}, {x + 50, 0}, {x, h}, Color{12, 20, 24, 255}); // a drowned stalactite hanging from far overhead
            });
        }};
        midLayer = {0.32f, 0.32f, [cw, ch](const PlatformState& p, float t, float ox, float oy) { // dripping rock walls veined with faint glowing fungus
            (void)p; (void)oy;
            Layer(ox / 0.32f, 0.32f, 110, cw, [&](float x, float wx) {
                float h = ch * (0.4f + Hs(wx) * 0.5f), w = 26 + Hs(wx + 2) * 20;
                DrawRectangle((int)x, (int)(ch - h), (int)w, (int)h + 4, Color{22, 18, 20, 255});
                DrawRectangle((int)x + 3, (int)(ch - h), 3, (int)h, Color{30, 25, 28, 255});
                if (Hs(wx + 6) > 0.6f) for (int k = 0; k < 3; k++) DrawCircle((int)(x + w / 2 + k * 6 - 6), (int)(ch - h * 0.3f - k * 18), 2.5f, Fade(Color{110, 220, 190, 255}, 0.5f)); // faint fungal glow
            });
            for (int k = 0; k < 14; k++) { // bubbles seeping up out of the rock
                float dx = fmodf(k * 91.0f - ox * 0.7f + 9000, cw) + sinf(t * 2 + k) * 2, dy = ch - fmodf(t * (18 + k * 3) + k * 70, ch);
                DrawCircleLines((int)dx, (int)dy, 1.5f + (k % 3) * 0.5f, Fade(Color{150, 210, 225, 255}, 0.35f));
            }
            for (int k = 0; k < 40; k++) { // silt hanging in the still water
                float sx2 = fmodf(k * 73.0f - ox * 0.5f + 9000, cw), sy2 = fmodf(k * 131.0f + sinf(t * 0.3f + k) * 6 + t * 1.5f, ch);
                DrawRectangle((int)sx2, (int)sy2, 1, 1, Fade(Color{120, 160, 150, 255}, 0.3f));
            }
        }};
        foreLayer = {1.3f, 1.15f, [cw, ch](const PlatformState& p, float t, float ox, float oy) { // jagged rock framing the lens, dark against the lamp
            (void)p; (void)t; (void)oy;
            Layer(ox / 1.3f, 1.3f, 300, cw, [&](float x, float wx) {
                float edge = std::clamp((fabsf(x - cw * 0.5f) - cw * 0.3f) / (cw * 0.13f), 0.0f, 1.0f); // edges of the lens, fading in and out
                if (edge <= 0.01f) return;
                float h = (40 + Hs(wx) * 90) * edge;
                DrawTri({x - 30, 0}, {x + 30, 0}, {x, h}, Color{3, 2, 3, 255});
                DrawTri({x - 26, ch}, {x + 26, ch}, {x, ch - h * 0.7f}, Color{3, 2, 3, 255});
            });
        }};
    } else {
        farLayer = {0.05f, 0.05f, [cw, ch](const PlatformState& p, float t, float ox, float oy) { // the night sky, a full moon, drifting cloud
            (void)p; (void)oy;
            DrawRectangleGradientV(0, 0, (int)cw, (int)ch, Color{10, 12, 34, 255}, Color{56, 36, 66, 255});
            Layer(ox / 0.05f, 0.02f, 23, cw, [&](float x, float wx) {
                float sy = 34 + Hs(wx) * 200;
                if (Hs(wx + 2) > 0.45f && sinf(t * 2 + wx) > -0.6f) DrawPixel((int)x, (int)sy, Color{230, 230, 255, 255});
            });
            Vector2 moon{cw - 110 - ox * 0.6f, 80};
            DrawCircleV(moon, 26, Color{250, 244, 220, 60}); DrawCircleV(moon, 20, Color{246, 240, 214, 255}); DrawCircleV({moon.x - 6, moon.y - 4}, 4, Color{220, 214, 190, 255});
            Layer(ox / 0.05f + t * 6, 0.08f, 260, cw, [&](float x, float wx) {
                float y = 60 + Hs(wx) * 70;
                for (int k = 0; k < 4; k++) DrawEllipse((int)(x + k * 22), (int)(y + (k % 2) * 4), 26, 8, Color{44, 40, 70, 200});
            });
            Layer(ox / 0.05f, 0.2f, 260, cw, [&](float x, float wx) { // a ghostly fleet on the horizon
                float y = WATER_LEVEL_Y - 12; // every background hull sits on the horizon line
                DrawRectangle((int)x, (int)y, 90, 16, Color{28, 24, 44, 255});
                DrawRectangle((int)x + 40, (int)y - 80, 3, 80, Color{28, 24, 44, 255});
                DrawTri({x + 44, y - 72}, {x + 44, y - 14}, {x + 80, y - 14}, Color{36, 32, 56, 255});
            });
        }};
        midLayer = {0.35f, 0.35f, [cw, ch](const PlatformState& p, float t, float ox, float oy) { // the far sea, a rival galleon close by, the near sea
            (void)p; (void)oy;
            for (int x = 0; x < (int)cw; x += 4) {
                float y = 262 + sinf((x + ox * 0.9f) * 0.05f + t * 1.2f) * 2;
                DrawRectangle(x, (int)y, 4, (int)ch - (int)y, Color{22, 28, 56, 255});
                if (((x / 4) % 9) == 0) DrawRectangle(x, (int)y, 3, 1, Color{120, 130, 180, 255});
            }
            Layer(ox / 0.35f, 0.35f, 520, cw, [&](float x, float wx) {
                float y = WATER_LEVEL_Y - 20 + sinf(t * 0.8f + wx) * 2; // its keel rides at the waterline
                Color hullC{30, 22, 30, 255};
                DrawRectangle((int)x, (int)y, 170, 26, hullC);
                DrawTri({x + 170, y}, {x + 200, y - 10}, {x + 170, y + 26}, hullC);
                DrawRectangle((int)x - 10, (int)y - 14, 40, 16, hullC);
                for (int k = 0; k < 6; k++) DrawRectangle((int)x + 20 + k * 24, (int)y + 10, 5, 4, Color{255, 190, 90, 255});
                for (int m = 0; m < 3; m++) {
                    float mx = x + 40 + m * 55;
                    DrawRectangle((int)mx, (int)y - 110 + m * 10, 3, 110 - m * 10, hullC);
                    DrawTri({mx + 3, y - 100 + m * 10}, {mx + 3, y - 30}, {mx + 38, y - 40}, Color{60, 52, 66, 255});
                }
            });
            for (int x = 0; x < (int)cw; x += 4) {
                float y = 300 + sinf((x + ox * 1.7f) * 0.04f + t * 1.6f) * 4;
                DrawRectangle(x, (int)y, 4, (int)ch - (int)y, Color{18, 24, 50, 255});
                if (((x / 4) % 6) == 0) DrawRectangle(x, (int)y, 3, 1, Color{150, 160, 200, 255});
            }
        }};
        foreLayer = {1.3f, 1.1f, [cw, ch](const PlatformState& p, float t, float ox, float oy) { // spray and slack rigging, near the lens
            (void)p; (void)oy;
            for (int k = 0; k < 14; k++) {
                float bx = fmodf(k * 131.0f - ox * 0.7f + 9000, cw + 120) - 40, ph = fmodf(t * 0.5f + k * 0.13f, 1.0f);
                DrawRectangle((int)bx, (int)(ch - 12 - sinf(ph * PI) * 26), 2, 2, Fade(Color{220, 230, 250, 255}, 0.6f * (1 - ph)));
            }
        }};
    }
}

BackgroundSystem gBackground;
void DrawBackground(const PlatformState& p, float t) { gBackground.RenderBack(p, t); }
void DrawForeground(const PlatformState& p, float t) { gBackground.RenderFore(p, t); }
// ---------------------------------------------------------------- drawing: the ship around you
// On the pirate ship, the world itself is dressed as a ship: masts and sails rise from the open deck,
// a rail runs along it, and below decks each section has its own interior behind it: the hold with
// its beams, lanterns and gunports, or the captain's cabin with its great stern windows.
void DrawHoldWall(const PlatformState& p, float x0, float y0, float x1, float y1, float t, bool gundeck) {
    DrawRectangle((int)x0, (int)y0, (int)(x1 - x0), (int)(y1 - y0), Color{40, 26, 18, 255});
    for (float x = x0; x < x1; x += 12) DrawRectangle((int)x, (int)y0, 1, (int)(y1 - y0), Color{30, 18, 12, 255}); // planking
    for (float x = x0 + 64; x < x1; x += 128) { // ribs, with knees under the deck beams
        DrawRectangle((int)x - 7, (int)y0, 14, (int)(y1 - y0), Color{62, 40, 24, 255});
        DrawRectangle((int)x - 7, (int)y0, 3, (int)(y1 - y0), Color{80, 54, 32, 255});
        DrawTri({x + 7, y0}, {x + 30, y0}, {x + 7, y0 + 24}, Color{62, 40, 24, 255});
        DrawTri({x - 7, y0}, {x - 30, y0}, {x - 7, y0 + 24}, Color{62, 40, 24, 255});
    }
    for (float x = x0 + 128; x < x1; x += 256) { // gunports (lashed cannons on the gun deck), with the moonlit sea outside
        int col = (int)(x / T), row = (int)(y0 / T); // sit the port a little above the floor below it
        while (row < p.h - 1 && !Solid(p, col, row)) row++;
        float gy = row * (float)T - 44;
        float floorY = row * (float)T;
        DrawRectangle((int)x - 12, (int)gy - 12, 24, 22, Color{20, 22, 44, 255});
        DrawRectangle((int)x - 12, (int)gy + 2, 24, 8, Color{30, 40, 70, 255});
        DrawRectangle((int)x - 7, (int)gy + 2 + (int)(sinf(t * 1.5f + x) * 1.5f), 8, 1, Color{150, 160, 200, 255});
        DrawRectangleLines((int)x - 13, (int)gy - 13, 26, 24, Color{24, 14, 10, 255});
        if (gundeck) {
            DrawRectangle((int)x - 26, (int)gy + 6, 34, 10, Color{34, 34, 38, 255});   // the cannon
            DrawCircle((int)x - 26, (int)gy + 11, 6, Color{34, 34, 38, 255});
            DrawRectangle((int)x - 28, (int)floorY - 14, 30, 14, Color{80, 52, 30, 255}); // its carriage
        }
    }
    for (float x = x0 + 192; x < x1; x += 256) { // hanging lanterns and a slung hammock
        float sway = sinf(t * 1.1f + x) * 3;
        DrawLineEx({x, y0}, {x + sway, y0 + 30}, 1, Color{30, 26, 22, 255});
        DrawRectangle((int)(x + sway) - 4, (int)y0 + 30, 8, 10, Color{255, 200, 110, 255});
        DrawRectangleLines((int)(x + sway) - 4, (int)y0 + 30, 8, 10, Color{60, 50, 30, 255});
        float hy = y0 + 58;
        for (int k = 0; k < 10; k++) DrawRectangle((int)(x - 60 + k * 8), (int)(hy + sinf(k / 9.0f * PI) * 12 + sinf(t * 0.9f) * 2), 8, 3, Color{150, 132, 100, 255});
    }
    // A row of shut cabin doors lines the passage at floor level, so the one or two that burst open read
    // as part of a real corridor of quarters, not as a random ambush spot in open air. Skip anywhere an
    // ambusher's own (interactive) door will be drawn, so the two don't overlap.
    for (float x = x0 + 44; x < x1 - 8; x += 52) {
        bool live = false;
        for (auto& e : p.enemies) if (e.type == 'P' && fabsf(e.home.x - x) < 40) live = true;
        if (live) continue;
        int col = (int)(x / T), row = (int)(y0 / T);
        while (row < p.h - 1 && !Solid(p, col, row)) row++;
        float dy = row * (float)T - 2 * T;
        DrawRectangle((int)x, (int)dy + 1, 30, 2 * T - 1, Color{56, 34, 20, 255});
        DrawRectangle((int)x + 3, (int)dy + 4, 24, 2 * T - 7, Color{74, 46, 26, 255});
        DrawRectangle((int)x + 3, (int)dy + 12, 24, 3, Color{48, 30, 18, 255});
        DrawRectangle((int)x + 3, (int)dy + 2 * T - 16, 24, 3, Color{48, 30, 18, 255});
        DrawCircle((int)x + 22, (int)dy + T, 2, Pal::Brass);
    }
}

void DrawCabinWall(float x0, float y0, float x1, float y1, float t) {
    DrawRectangle((int)x0, (int)y0, (int)(x1 - x0), (int)(y1 - y0), Color{70, 38, 26, 255});
    for (float x = x0; x < x1; x += 48) { // panelling
        DrawRectangleLines((int)x + 6, (int)y1 - 120, 36, 100, Color{50, 26, 16, 255});
        DrawRectangle((int)x + 7, (int)y1 - 119, 34, 2, Color{110, 64, 40, 255});
    }
    float mx = (x0 + x1) / 2; // the great stern windows, with the moon on the sea behind
    for (int k = -2; k <= 2; k++) {
        float wx = mx + k * 64 - 26, wy = y0 + 110;
        DrawRectangle((int)wx - 4, (int)wy - 4, 60, 128, Color{130, 90, 40, 255});
        DrawRectangleGradientV((int)wx, (int)wy, 52, 120, Color{24, 26, 60, 255}, Color{50, 40, 80, 255});
        DrawRectangle((int)wx, (int)wy + 84, 52, 36, Color{20, 24, 50, 255});
        DrawRectangle((int)wx + 4, (int)wy + 86 + (int)(sinf(t + k) * 1.5f), 20, 1, Color{150, 160, 200, 255});
        DrawRectangle((int)wx + 25, (int)wy, 2, 120, Color{130, 90, 40, 255});
        DrawRectangle((int)wx, (int)wy + 58, 52, 2, Color{130, 90, 40, 255});
    }
    DrawCircle((int)mx + 60, (int)y0 + 150, 12, Color{246, 240, 214, 255});
    // a chart pinned to the wall, and a portrait of the captain himself
    DrawRectangle((int)x0 + 90, (int)y0 + 130, 70, 50, Color{220, 200, 150, 255});
    DrawLineEx({x0 + 100, y0 + 160}, {x0 + 150, y0 + 140}, 1, Color{150, 40, 30, 255});
    DrawRectangle((int)x1 - 150, (int)y0 + 120, 56, 70, Color{170, 128, 60, 255});
    DrawRectangle((int)x1 - 146, (int)y0 + 124, 48, 62, Color{40, 30, 26, 255});
    DrawCircle((int)x1 - 122, (int)y0 + 146, 9, Color{210, 170, 130, 255});
    DrawCircle((int)x1 - 122, (int)y0 + 158, 10, Color{24, 22, 22, 255});
    DrawTri({x1 - 138, y0 + 140}, {x1 - 106, y0 + 140}, {x1 - 122, y0 + 128}, Color{24, 22, 30, 255});
    for (int k = 0; k < 2; k++) { // candle lamps
        float lx = x0 + 200 + k * (x1 - x0 - 400);
        DrawRectangle((int)lx - 3, (int)y0 + 200, 6, 14, Color{236, 228, 200, 255});
        DrawCircle((int)lx, (int)y0 + 196, 3 + sinf(t * 12 + k), Color{255, 210, 120, 255});
    }
}

void DrawShipScenery(const PlatformState& p, int c0, int c1, float t) {
    for (int i = 0; i < (int)p.partX.size(); i++) {
        int x0 = p.partX[i], x1 = x0 + CH_W;
        if (x1 < c0 || x0 > c1) continue;
        float wx0 = x0 * (float)T, wx1 = x1 * (float)T;
        if (p.partInterior[i] < p.h) { // below decks
            float y0 = p.partInterior[i] * (float)T, y1 = p.h * (float)T;
            if (p.partKind[i] == 2) DrawCabinWall(wx0, y0, wx1, y1, t);
            else DrawHoldWall(p, wx0, y0, wx1, y1, t, false);
        }
        if (p.partKind[i] != 0 || i == (int)p.partX.size() - 1) continue;
        // an open deck: a mast with its sails and rigging
        int mc = x0 + 12, base = 0;
        while (base < p.h - 1 && p.tiles[base][mc] != '#') base++;
        float mx = mc * (float)T + 16, by = base * (float)T, top = by - 24.0f * T;
        for (int s = -1; s <= 1; s += 2) // shrouds, down to the rail
            for (int k = 0; k < 4; k++) DrawLineEx({mx, top + 40}, {mx + s * (4 + k * 1.5f) * T, by - 10}, 1, Color{40, 30, 26, 255});
        DrawRectangle((int)mx - 7, (int)top, 14, (int)(by - top), Color{92, 60, 34, 255});
        DrawRectangle((int)mx - 7, (int)top, 4, (int)(by - top), Color{130, 90, 54, 255});
        for (int y = 0; y < 3; y++) { // yards, and the sails bellying out in the wind
            float yy = top + 60 + y * 7.0f * T, half = (5.5f - y * 0.8f) * T, h = 5.2f * T, belly = 18 + sinf(t * 0.9f + y) * 4;
            DrawRectangle((int)(mx - half), (int)yy, (int)(half * 2), 8, Color{80, 52, 30, 255});
            Color sail{214, 200, 170, 255}, sailDk{176, 160, 132, 255};
            if (gGhost) { // shredded: hanging strips of grey-green canvas, each torn off at its own length
                float sw = (half * 2 - 12) / 6.0f;
                for (int k = 0; k < 6; k++) {
                    float sx0 = mx - half + 6 + k * sw, len = (h - 8) * (0.3f + Hs(k * 3.1f + y * 7.7f + mx * 0.01f) * 0.65f), sway = sinf(t * 1.3f + k + y) * 5;
                    Color sc = k % 2 ? Color{112, 142, 132, 235} : Color{86, 114, 106, 235};
                    DrawTri({sx0, yy + 8}, {sx0 + sw - 3, yy + 8}, {sx0 + sw - 3 + sway, yy + 8 + len}, sc);
                    DrawTri({sx0, yy + 8}, {sx0 + sw - 3 + sway, yy + 8 + len}, {sx0 + sway * 0.6f, yy + 8 + len * 0.8f}, Tone(sc, -0.2f));
                }
            } else {
                DrawTri({mx - half + 6, yy + 8}, {mx + half - 6, yy + 8}, {mx + half - 10 + belly, yy + h}, sail);
                DrawTri({mx - half + 6, yy + 8}, {mx + half - 10 + belly, yy + h}, {mx - half + 10 + belly, yy + h}, sailDk);
                for (int k = 1; k < 4; k++) DrawLineEx({mx - half + k * half / 2, yy + 8}, {mx - half + k * half / 2 + belly, yy + h}, 1, Color{160, 144, 118, 255});
            }
        }
        { // a lantern hung from the lowest yard: fixed to the mast in the world, so it never moves with the diver
            float yy = top + 60 + 2 * 7.0f * T, half = 3.9f * T, lx = mx - half + 16, sway = sinf(t * 1.1f + mx * 0.01f) * 2;
            DrawLineEx({lx, yy + 8}, {lx + sway, yy + 34}, 1, Color{20, 16, 12, 255});
            DrawRectangle((int)(lx + sway) - 5, (int)yy + 34, 10, 13, Color{8, 8, 12, 255});
            DrawRectangle((int)(lx + sway) - 3, (int)yy + 36, 6, 9, gGhost ? Color{120, 255, 210, 255} : Color{255, 206, 110, 255});
            BeginBlendMode(BLEND_ADDITIVE);
            for (int k = 0; k < 3; k++) DrawCircleV({lx + sway, yy + 41}, 14.0f + k * 12, gGhost ? Color{80, 240, 190, (unsigned char)(26 - k * 7)} : Color{255, 180, 80, (unsigned char)(28 - k * 8)});
            EndBlendMode();
        }
        DrawRectangle((int)mx - 18, (int)top - 20, 36, 14, Color{70, 46, 26, 255}); // the crow's nest
        DrawRectangle((int)mx - 2, (int)top - 60, 3, 40, Color{80, 52, 30, 255});
        float wave = sinf(t * 3) * 6; // the Jolly Roger
        DrawTri({mx + 1, top - 60}, {mx + 44, top - 54 + wave}, {mx + 1, top - 30}, Color{20, 18, 22, 255});
        DrawTri({mx + 44, top - 54 + wave}, {mx + 44, top - 26 + wave}, {mx + 1, top - 30}, Color{20, 18, 22, 255});
        DrawCircle((int)mx + 22, (int)(top - 44 + wave * 0.5f), 5, Color{230, 226, 210, 255});
    }
    // the rail along the open deck (and along any raised deck), behind you
    int r0 = 0;
    for (int x = c0; x <= c1; x++) {
        int part = PartAt(p, x * (float)T);
        for (int y = r0 + 1; y < p.h; y++) {
            if (!Solid(p, x, y) || Solid(p, x, y - 1) || p.tiles[y][x] != '#' || y >= p.partInterior[part]) continue;
            float px = x * (float)T, py = y * (float)T;
            for (int k = 0; k < 2; k++) DrawRectangle((int)px + 4 + k * 16, (int)py - 18, 4, 18, Color{84, 54, 32, 255});
            DrawRectangle((int)px, (int)py - 22, T, 5, Color{120, 80, 46, 255});
            float hh = Hs(x * 7.13f + 2);   // deck furniture behind the rail, dimmer than the deck you walk on: barrels, cannons, coiled rope
            if (!gGhost || hh > 0.5f) {
                if (hh > 0.90f && hh < 0.945f) {
                    DrawRectangle((int)px + 6, (int)py - 16, 18, 16, Color{58, 38, 22, 255});
                    DrawRectangle((int)px + 6, (int)py - 13, 18, 2, Color{28, 22, 18, 255}); DrawRectangle((int)px + 6, (int)py - 6, 18, 2, Color{28, 22, 18, 255});
                } else if (hh >= 0.945f && hh < 0.975f) {
                    DrawRectangle((int)px + 4, (int)py - 12, 22, 6, Color{26, 26, 30, 255}); DrawRectangle((int)px + 20, (int)py - 14, 8, 9, Color{36, 36, 40, 255});
                    DrawCircle((int)px + 9, (int)py - 4, 4, Color{54, 36, 22, 255}); DrawCircle((int)px + 22, (int)py - 4, 4, Color{54, 36, 22, 255});
                } else if (hh >= 0.975f) {
                    for (int k = 0; k < 3; k++) DrawEllipse((int)px + 15, (int)py - 3 - k * 3, 10 - k, 3, Color{92, 76, 52, 255});
                }
            }
            break; // just the top surface in each column
        }
    }
}

// ---------------------------------------------------------------- drawing: tiles
// Auto-tiling: a bitmask of which of a tile's four neighbours match (1 = north, 2 = east, 4 = south, 8 = west), so pipes,
// coral, timbers and ropes can draw the right junction, corner, cap or open face instead of a disjointed block.
uint8_t SolidMask(const PlatformState& p, int x, int y) {
    auto S = [&](int tx, int ty) { return Solid(p, tx, ty) || At(p, tx, ty) == 'i'; }; // a cabin behind the timbers is part of the hull
    return (S(x, y - 1) ? 1 : 0) | (S(x + 1, y) ? 2 : 0) | (S(x, y + 1) ? 4 : 0) | (S(x - 1, y) ? 8 : 0);
}
template <typename Pred>
uint8_t TileMask(const PlatformState& p, int x, int y, Pred match) {
    return (match(At(p, x, y - 1)) ? 1 : 0) | (match(At(p, x + 1, y)) ? 2 : 0) | (match(At(p, x, y + 1)) ? 4 : 0) | (match(At(p, x - 1, y)) ? 8 : 0);
}
void Barnacles(float bx, float by) {
    for (int k = 0; k < 3; k++) {
        float ox = bx + k * 5 - 5, oy = by + (k % 2) * 3;
        DrawTri({ox - 3, oy + 3}, {ox + 3, oy + 3}, {ox, oy - 3}, Color{8, 10, 14, 255});
        DrawTri({ox - 2, oy + 2}, {ox + 2, oy + 2}, {ox, oy - 2}, Color{214, 202, 178, 255});
    }
}
// The Hull level is the top of a submarine: riveted steel plating, welded seams, hatches, grilles and hazard stripes. Coral
// belongs on the rocks ('R') the hull lies among, not on the hull itself, so this is drawn clean and industrial.
void DrawHullSteel(const PlatformState& p, int x, int y) {
    int px = x * T, py = y * T;
    uint8_t m = SolidMask(p, x, y);
    float h1 = Hs(x * 3.7f + y * 11.1f), h2 = Hs(x * 6.1f + y * 2.3f + 5), h3 = Hs(x * 1.9f + y * 7.7f + 9);
    Color ink{8, 10, 14, 255};
    if (y <= 1) { // the surface of the sea, seen from below: pale light and rolling waves
        Color a = y == 0 ? Color{198, 238, 248, 255} : Color{104, 176, 204, 255}, b = y == 0 ? Color{132, 204, 226, 255} : Color{58, 128, 168, 255};
        DrawRectangleGradientV(px, py, T, T, a, b);
        for (int k = 0; k < 4; k++) DrawRectangle(px + ((k * 9 + (int)(sinf(p.time * 1.2f + x + k) * 4)) & 31), py + 6 + k * 7, 8, 1, Fade(WHITE, 0.55f));
        return;
    }
    bool inner = m == 15;
    Color steel = inner ? Color{54, 64, 76, 255} : Color{86, 100, 114, 255}, dk = Color{34, 42, 52, 255}, lt = Color{140, 158, 172, 255};
    DrawRectangle(px, py, T, T, steel);
    DrawRectangle(px, py, T, 1, dk); DrawRectangle(px, py, 1, T, dk);                       // the seams between plates
    DrawRectangleLines(px + 3, py + 3, T - 6, T - 6, Fade(dk, 0.7f));                       // an inset panel
    for (int k = 0; k < 4; k++) { // rivets at the corners of every panel
        int rx = px + 5 + (k % 2) * (T - 12), ry = py + 5 + (k / 2) * (T - 12);
        DrawRectangle(rx + 1, ry + 1, 2, 2, dk); DrawRectangle(rx, ry, 2, 2, lt);
    }
    if (x % 4 == 0) { DrawRectangle(px, py, 3, T, Color{30, 38, 46, 255}); for (int k = 0; k < 4; k++) DrawRectangle(px + 5, py + 4 + k * 8, 1, 3, lt); } // a welded frame rib
    if (!(m & 1)) { // the deck: bright edge and non-slip diamonds
        DrawRectangle(px, py, T, 3, lt);
        DrawRectangle(px, py + 3, T, 1, dk);
        for (int k = 0; k < 5; k++) { DrawRectangle(px + 3 + k * 6, py + 8, 2, 1, Color{112, 128, 142, 255}); DrawRectangle(px + 6 + k * 6, py + 10, 2, 1, Color{112, 128, 142, 255}); }
    }
    if (!(m & 2)) DrawRectangle(px + T - 3, py, 3, T, dk);
    if (!(m & 4)) { DrawRectangle(px, py + T - 4, T, 4, dk); DrawRectangle(px, py + T - 4, T, 1, lt); }
    if (!inner || h1 > 0.5f) {
        if (h1 > 0.93f) { // a round pressure hatch with a wheel
            DrawCircle(px + 16, py + 16, 11, ink); DrawCircle(px + 16, py + 16, 9, Color{102, 116, 128, 255}); DrawRing({px + 16.0f, py + 16.0f}, 3, 5, 0, 360, 10, dk);
            for (int k = 0; k < 4; k++) { float a = k * PI / 2 + 0.4f; DrawLineEx({px + 16.0f, py + 16.0f}, {px + 16 + cosf(a) * 8, py + 16 + sinf(a) * 8}, 2, Color{190, 150, 60, 255}); }
        } else if (h1 > 0.86f) { // a vent grille
            DrawRectangle(px + 5, py + 8, T - 10, 16, ink);
            for (int k = 0; k < 5; k++) DrawRectangle(px + 7, py + 10 + k * 3, T - 14, 1, Color{92, 106, 118, 255});
        } else if (h1 > 0.8f) { // a stencilled white patch: a hull marking
            DrawRectangle(px + 6, py + 10, 20, 12, Fade(Color{226, 232, 236, 255}, 0.85f)); DrawRectangle(px + 8, py + 12, 16, 8, steel); DrawRectangle(px + 11, py + 14, 3, 4, Fade(WHITE, 0.85f)); DrawRectangle(px + 17, py + 14, 4, 4, Fade(WHITE, 0.85f));
        }
    }
    if (!(m & 1) && h2 > 0.88f) for (int k = 0; k < 8; k++) DrawRectangle(px + k * 4, py + 4, 4, 3, k % 2 ? Color{232, 196, 52, 255} : Color{22, 20, 18, 255}); // hazard stripes along a deck edge
    if (h3 > 0.9f && (m & 1)) { DrawRectangle(px + 8, py + 2, 3, 12, Color{110, 60, 36, 200}); DrawRectangle(px + 9, py + 2, 1, 12, Fade(BLACK, 0.3f)); } // a rust weep from a bolt
    if (inner) DrawRectangle(px, py, T, T, Fade(BLACK, 0.12f));
}
void DrawSolid(const PlatformState& p, int x, int y) {
    float px = x * (float)T, py = y * (float)T;
    bool topEdge = !Solid(p, x, y - 1);
    if (p.level == PL_HULL && p.tiles[y][x] != 'R') { DrawHullSteel(p, x, y); return; }
    switch (p.level) {
        case PL_PIPES: {
            // the duct walls: dark iron. Deep inside the mass it's plain; at the edges, riveted and rusting.
            bool inner = Solid(p, x, y - 1) && Solid(p, x, y + 1) && Solid(p, x - 1, y) && Solid(p, x + 1, y);
            if (inner) {
                DrawRectangle((int)px, (int)py, T, T, Color{36, 32, 30, 255});
                if ((x + y) % 3 == 0) DrawRectangle((int)px + 6, (int)py + 6, 2, 2, Color{46, 40, 36, 255});
                break;
            }
            DrawRectangle((int)px, (int)py, T, T, Color{78, 66, 58, 255});
            DrawRectangleLines((int)px, (int)py, T, T, Color{44, 38, 34, 255});
            DrawRectangle((int)px + 2, (int)py + 2, T - 4, 3, Color{96, 82, 70, 255});
            DrawCircle((int)px + 6, (int)py + 8, 2, Color{130, 112, 92, 255});
            DrawCircle((int)px + T - 6, (int)py + 8, 2, Color{130, 112, 92, 255});
            if ((x * 5 + y * 3) % 4 == 0) DrawRectangle((int)px + 10 + (x % 3) * 4, (int)py + 10, 2, T - 12, Color{120, 64, 34, 255}); // rust
            if (topEdge) DrawRectangle((int)px, (int)py, T, 3, Color{140, 120, 100, 255});
        } break;
        case PL_HULL: {
            // a coral mass grown over a wreck's iron: dark rock and plating inside, and wherever a face is open to the water
            // (found from the N/E/S/W neighbour bitmask) coral crowns, lumps and hanging tendrils
            uint8_t m = SolidMask(p, x, y); // 1 N, 2 E, 4 S, 8 W: which neighbours are solid
            bool inner = m == 15;
            Color rock{38, 52, 58, 255}, rockLt{56, 74, 78, 255}, ink{8, 10, 14, 255};
            DrawRectangle((int)px, (int)py, T, T, inner ? Color{30, 42, 48, 255} : rock);
            float h1 = Hs(x * 3.7f + y * 11.1f), h2 = Hs(x * 6.1f + y * 2.3f + 5);
            if (inner) { // deep in the mass: plating seams and polyp pits
                if (x % 2 == 0) DrawRectangle((int)px, (int)py, 2, T, Color{22, 32, 38, 255});
                if (y % 2 == 0) DrawRectangle((int)px, (int)py, T, 2, Color{22, 32, 38, 255});
                for (int k = 0; k < 3; k++) DrawCircle((int)(px + 5 + Hs(x * 1.3f + k) * 22), (int)(py + 5 + Hs(y * 1.7f + k) * 22), 1.5f, Color{60, 48, 66, 255});
                break;
            }
            DrawRectangle((int)px + 2, (int)py + 2, T - 4, 2, rockLt);
            Color pal[4] = {{204, 100, 112, 255}, {216, 134, 76, 255}, {142, 92, 158, 255}, {196, 170, 120, 255}};
            Color cA = pal[(x * 7 + y * 3) & 3], cB = pal[(x * 5 + y * 11 + 1) & 3];
            if (!(m & 1)) { // an open top: a crown of coral branches and a few polyps
                for (int k = 0; k < 4; k++) {
                    float bx = px + 3 + k * 8 + h1 * 3, bh = 6 + Hs(x * 2.9f + k) * 12;
                    DrawLineEx({bx, py + 2}, {bx + (k - 1.5f) * 2, py - bh}, 5, ink);
                    DrawLineEx({bx, py + 2}, {bx + (k - 1.5f) * 2, py - bh}, 3, k % 2 ? cA : cB);
                    DrawCircleV({bx + (k - 1.5f) * 2, py - bh}, 3, ink); DrawCircleV({bx + (k - 1.5f) * 2, py - bh}, 2, Tone(cA, 0.25f));
                }
                DrawRectangle((int)px, (int)py, T, 3, Color{88, 108, 100, 255});
            }
            if (!(m & 2) && h1 > 0.86f && y % 2 == 0) { float cy = py + 6 + h2 * 16, r = 4 + h1 * 4; DrawCircleV({px + T - 1, cy}, r + 1.5f, ink); DrawCircleV({px + T - 2, cy}, r, Tone(cA, -0.2f)); DrawCircleV({px + T - 3, cy - 1}, r * 0.4f, Tone(cA, 0.2f)); }   // a coral lump on the right face
            if (!(m & 8) && h2 > 0.86f && y % 2 == 1) { float cy = py + 6 + h1 * 16, r = 4 + h2 * 4; DrawCircleV({px + 1, cy}, r + 1.5f, ink); DrawCircleV({px + 2, cy}, r, Tone(cB, -0.2f)); DrawCircleV({px + 3, cy - 1}, r * 0.4f, Tone(cB, 0.2f)); }    // and the left
            if (!(m & 4)) for (int k = 0; k < 3; k++) { // tendrils hanging from the underside, swaying
                float bx = px + 6 + k * 10, sw = sinf(p.time * 1.4f + x + k * 2) * 2.5f;
                DrawLineEx({bx, py + T - 2}, {bx + sw, py + T + 6}, 4, ink); DrawLineEx({bx, py + T - 2}, {bx + sw, py + T + 6}, 2, Color{70, 120, 96, 255});
            }
            if (h1 > 0.82f) Barnacles(px + 6 + h2 * 16, py + 8 + h1 * 12);
            if (h2 > 0.7f) DrawLineEx({px + 8, py + 12}, {px + 14, py + 24}, 1.5f, ink);       // a crack
        } break;
        case PL_ISLAND: {
            // worked stone terraces, earth-filled: this ground was cut and stepped by hand, not left wild - moss and
            // hanging roots have crept back over it, but the carving underneath still shows
            uint8_t m = SolidMask(p, x, y);
            bool inner = m == 15;
            Color soil{62, 54, 44, 255}, soilLt{78, 68, 54, 255}, ink{10, 8, 6, 255};
            DrawRectangle((int)px, (int)py, T, T, inner ? Color{42, 36, 30, 255} : soil);
            float h1 = Hs(x * 3.3f + y * 9.7f), h2 = Hs(x * 5.9f + y * 2.9f + 5);
            if (inner) {
                if (h1 > 0.85f) { DrawLineEx({px + 6, py + 8}, {px + 14, py + 8}, 1.4f, Color{58, 50, 40, 255}); DrawLineEx({px + 10, py + 6}, {px + 10, py + 22}, 1.4f, Color{58, 50, 40, 255}); } // a shallow carved glyph, buried in the mass
                else for (int k = 0; k < 3; k++) DrawCircle((int)(px + 5 + Hs(x * 1.1f + k) * 22), (int)(py + 5 + Hs(y * 1.9f + k) * 22), 1.5f, Color{54, 46, 36, 255});
                break;
            }
            DrawRectangle((int)px + 2, (int)py + 2, T - 4, 2, soilLt);
            if (!(m & 1)) { // open top: a mat of grass and low fronds
                DrawRectangle((int)px, (int)py, T, 4, Color{78, 118, 52, 255});
                for (int k = 0; k < 5; k++) { float bx = px + 2 + k * 6 + h1 * 2, bh = 4 + Hs(x * 2.1f + k) * 6; DrawLineEx({bx, py + 1}, {bx + (k % 2 ? 2.0f : -2.0f), py - bh}, 1.6f, Color{62, 100, 40, 255}); }
            }
            if (!(m & 2) && h1 > 0.82f && y % 2 == 0) { float cy = py + 8 + h2 * 14; DrawLineEx({px + T - 1, cy}, {px + T + 5, cy + 4}, 2.2f, Color{86, 66, 40, 255}); } // a root snaking out the right face
            if (!(m & 8) && h2 > 0.82f && y % 2 == 1) { float cy = py + 8 + h1 * 14; DrawLineEx({px + 1, cy}, {px - 5, cy + 4}, 2.2f, Color{86, 66, 40, 255}); }
            if (!(m & 4)) for (int k = 0; k < 3; k++) { // hanging vines, swaying
                float bx = px + 6 + k * 10, sw = sinf(p.time * 1.2f + x + k * 2) * 2.5f;
                DrawLineEx({bx, py + T - 2}, {bx + sw, py + T + 8}, 3, ink); DrawLineEx({bx, py + T - 2}, {bx + sw, py + T + 8}, 1.6f, Color{74, 112, 48, 255});
            }
            if (h2 > 0.75f) DrawLineEx({px + 8, py + 10}, {px + 15, py + 22}, 1.4f, ink); // a crack in the stone underneath
        } break;
        case PL_WEEDS: {
            // the seabed of a sunlit kelp forest: pale rippled sand over dark reef rock, seagrass and shells on the open tops
            uint8_t m = SolidMask(p, x, y);
            bool inner = m == 15;
            float h1 = Hs(x * 3.9f + y * 7.3f), h2 = Hs(x * 5.7f + y * 3.1f + 3);
            Color rock{46, 62, 60, 255}, ink{8, 12, 12, 255};
            DrawRectangle((int)px, (int)py, T, T, inner ? Color{36, 50, 50, 255} : rock);
            if (inner) { if (h1 > 0.8f) DrawCircle((int)(px + 8 + h2 * 16), (int)(py + 8 + h1 * 14), 2, Color{56, 74, 70, 255}); break; }
            if (!(m & 1)) { // open top: sand
                DrawRectangle((int)px, (int)py, T, 7, Color{196, 180, 132, 255});
                DrawRectangle((int)px, (int)py + 7, T, 2, Color{150, 136, 96, 255});
                for (int k = 0; k < 3; k++) DrawRectangle((int)px + 2 + k * 11 + (int)(h1 * 4), (int)py + 3 + (k % 2), 6, 1, Color{224, 212, 170, 255}); // ripples
                for (int k = 0; k < 3; k++) { float bx = px + 4 + k * 10 + h2 * 3, sw = sinf(p.time * 1.6f + x + k) * 2.5f, bh = 5 + Hs(x * 2.3f + k) * 7; DrawLineEx({bx, py + 1}, {bx + sw, py - bh}, 1.6f, Color{70, 140, 90, 255}); } // seagrass
                if (h2 > 0.85f) { DrawCircle((int)(px + 20), (int)py + 3, 2.5f, Color{236, 206, 196, 255}); DrawCircle((int)(px + 20), (int)py + 3, 1, Color{190, 140, 130, 255}); } // a shell
            }
            if (!(m & 4)) for (int k = 0; k < 2; k++) { float bx = px + 8 + k * 14, sw = sinf(p.time * 1.3f + x + k) * 2; DrawLineEx({bx, py + T - 2}, {bx + sw, py + T + 6}, 3, ink); DrawLineEx({bx, py + T - 2}, {bx + sw, py + T + 6}, 1.6f, Color{60, 120, 86, 255}); }
            if (h1 > 0.84f) Barnacles(px + 6 + h2 * 16, py + 12 + h1 * 10);
        } break;
        case PL_ATLANTIS: {
            // the drowned city's masonry: pale dressed stone in courses, a carved meander along every open top, glyphs that still glow
            uint8_t m = SolidMask(p, x, y);
            bool inner = m == 15;
            float h1 = Hs(x * 4.3f + y * 6.1f), h2 = Hs(x * 2.7f + y * 8.9f + 2);
            Color stone = inner ? Color{64, 70, 82, 255} : Color{112, 118, 126, 255}, joint{48, 52, 64, 255}, lt{156, 162, 166, 255};
            DrawRectangle((int)px, (int)py, T, T, stone);
            int off = (y % 2) * 16; // courses of ashlar, the joints staggered row to row
            DrawRectangle((int)px, (int)py + 15, T, 2, joint);
            DrawRectangle((int)px + ((off + 0) % T), (int)py, 2, 15, joint);
            DrawRectangle((int)px + ((off + 16) % T), (int)py + 17, 2, 15, joint);
            if (inner) { if (h1 > 0.9f) DrawRectangle((int)px + 10, (int)py + 6, 12, 4, Fade(Color{120, 220, 230, 255}, 0.25f + 0.1f * sinf(p.time * 1.5f + x))); break; }
            DrawRectangle((int)px + 1, (int)py + 1, T - 2, 2, lt);
            if (!(m & 1)) { // a carved meander band along the top
                DrawRectangle((int)px, (int)py, T, 8, Color{140, 146, 150, 255});
                for (int k = 0; k < 4; k++) { int kx = (int)px + k * 8; DrawRectangle(kx + 1, (int)py + 2, 6, 1, joint); DrawRectangle(kx + 6, (int)py + 2, 1, 4, joint); DrawRectangle(kx + 3, (int)py + 5, 4, 1, joint); DrawRectangle(kx + 3, (int)py + 3, 1, 3, joint); }
                if (h2 > 0.7f) DrawRectangle((int)px + 4 + (int)(h1 * 16), (int)py - 2, 6, 2, Color{70, 120, 90, 255}); // weed on the ledge
            }
            if (!(m & 2) && h1 > 0.75f) DrawTri({px + T, py + 6}, {px + T, py + 18}, {px + T - 5, py + 12}, joint); // a chipped corner
            if (h2 > 0.88f) { float gl = 0.35f + 0.25f * sinf(p.time * 2 + x * 0.7f); DrawRectangle((int)px + 12, (int)py + 18, 2, 8, Fade(Color{140, 230, 240, 255}, gl)); DrawRectangle((int)px + 9, (int)py + 21, 8, 2, Fade(Color{140, 230, 240, 255}, gl)); } // a glyph, still faintly lit
            if (h1 > 0.8f) DrawLineEx({px + 6, py + 20}, {px + 13, py + 30}, 1.2f, Color{30, 34, 44, 255}); // a crack
        } break;
        case PL_CAVE: {
            // bare stratified rock: no moss, no coral, no timber - jagged mineral seams and a rare crystal glint, lit only by the lamp
            uint8_t m = SolidMask(p, x, y);
            bool inner = m == 15;
            Color rock{40, 36, 38, 255}, rockDk{26, 23, 25, 255}, ink{6, 5, 6, 255};
            DrawRectangle((int)px, (int)py, T, T, inner ? Color{24, 21, 23, 255} : rock);
            float h1 = Hs(x * 4.1f + y * 8.3f), h2 = Hs(x * 6.7f + y * 3.1f + 7);
            if (y % 3 == 0) DrawRectangle((int)px, (int)py, T, 2, rockDk); // horizontal strata banding
            if (inner) {
                if (h1 > 0.88f) DrawCircle((int)(px + 8 + h2 * 16), (int)(py + 8 + h1 * 16), 1.4f, Color{140, 200, 230, 200}); // a buried crystal glint
                break;
            }
            if (!(m & 1)) for (int k = 0; k < 3; k++) { float bx = px + 4 + k * 10 + h1 * 4, bh = 3 + Hs(x * 2.3f + k) * 7; DrawTri({bx - 2, py + 1}, {bx + 2, py + 1}, {bx, py - bh}, rockDk); } // jagged broken lip along an open top
            if (!(m & 2) && h1 > 0.8f) { DrawTri({px + T - 6, py + 4}, {px + T + 2, py + 10 + h2 * 12}, {px + T - 6, py + 18}, rockDk); } // a jutting shard on the right face
            if (!(m & 8) && h2 > 0.8f) { DrawTri({px + 6, py + 4}, {px - 2, py + 10 + h1 * 12}, {px + 6, py + 18}, rockDk); }
            if (h2 > 0.9f) DrawCircle((int)(px + 10 + h1 * 12), (int)(py + 10 + h2 * 10), 1.6f, Color{130, 190, 220, 180}); // a small crystal vein
            if (h1 > 0.72f) DrawLineEx({px + 6, py + 8}, {px + 20, py + 24}, 1.2f, ink); // a hairline crack
        } break;
        default: {
            // the pirate ship: deck planks where they're open to the sky, heavy dark timbers inside the hull
            bool inner = Solid(p, x, y - 1) && Solid(p, x, y + 1) && Solid(p, x - 1, y) && Solid(p, x + 1, y);
            if (inner) { // the ship's hull timbers: long strakes, each a slightly different shade
                for (int k = 0; k < 4; k++) {
                    int strake = y * 4 + k;
                    float sh = Hs(strake * 1.7f + (x / 5) * 0.31f);
                    DrawRectangle((int)px, (int)py + k * 8, T, 8, Color{(unsigned char)(56 + sh * 14), (unsigned char)(34 + sh * 9), (unsigned char)(20 + sh * 6), 255});
                    DrawRectangle((int)px, (int)py + k * 8 + 7, T, 1, Color{36, 22, 12, 255});
                    if ((x + strake * 3) % 7 == 0) DrawRectangle((int)px + 12, (int)py + k * 8, 1, 7, Color{36, 22, 12, 255});
                }
                if ((x + y * 3) % 5 == 0) DrawRectangle((int)px + 10, (int)py + 5, 2, 2, Color{80, 72, 66, 255}); // treenails
                break;
            }
            DrawRectangle((int)px, (int)py, T, T, Color{118, 76, 44, 255});
            for (int k = 1; k < 4; k++) DrawRectangle((int)px, (int)py + k * 8, T, 1, Color{84, 52, 30, 255}); // planks
            DrawRectangle((int)px + ((x + y) % 3) * 9 + 4, (int)py, 1, T, Color{84, 52, 30, 255});          // butt joints
            DrawCircle((int)px + 3, (int)py + 4, 1, Color{60, 56, 60, 255});                                  // nails
            DrawCircle((int)px + T - 4, (int)py + 20, 1, Color{60, 56, 60, 255});
            if (topEdge) {
                DrawRectangle((int)px, (int)py, T, 4, Color{176, 128, 80, 255}); // worn, lighter deck boards
                DrawRectangle((int)px, (int)py + 4, T, 1, Color{70, 44, 26, 255});
            }
            uint8_t sm = SolidMask(p, x, y);                                     // the ship's outline: keel below, carved trim on the ends
            if (!(sm & 4)) { DrawRectangle((int)px, (int)py + T - 7, T, 7, Color{34, 22, 14, 255}); Barnacles(px + 10, py + T - 4); DrawRectangle((int)px, (int)py + T - 8, T, 1, Color{176, 150, 70, 255}); }
            if (!(sm & 2)) { DrawRectangle((int)px + T - 5, (int)py, 5, T, Color{60, 38, 22, 255}); DrawRectangle((int)px + T - 3, (int)py + 2, 1, T - 4, Color{176, 150, 70, 255}); }
            if (!(sm & 8)) { DrawRectangle((int)px, (int)py, 5, T, Color{60, 38, 22, 255}); DrawRectangle((int)px + 2, (int)py + 2, 1, T - 4, Color{176, 150, 70, 255}); }
        } break;
    }
}
// The environment has depth: every block with open space above or to its right shows a top and a side
// receding into the scene, and pipes cast a shadow on the wall behind them. Drawn before the faces.
constexpr float DEPTH = 7;
void DrawDepth(const PlatformState& p, int x, int y) {
    char c = p.tiles[y][x];
    float px = x * (float)T, py = y * (float)T;
    if (c == '=' || c == '|') {
        if (p.level == PL_WEEDS) return; // a kelp float casts no box of shadow
        DrawRectangle((int)px + 5, (int)py + 5, T, T, Color{0, 0, 0, 80});
        return;
    }
    if (c != '#' && c != 't' && c != 'k' && c != 'R' && c != 'T' && c != 'N' && c != 'y' && c != 'D') return;
    Color topC, sideC;
    switch (p.level) {
        case PL_PIPES: topC = {112, 96, 82, 255}; sideC = {40, 34, 30, 255}; break;
        case PL_HULL: topC = {124, 142, 154, 255}; sideC = {30, 38, 48, 255}; break;
        case PL_ISLAND: topC = {96, 132, 66, 255}; sideC = {46, 36, 24, 255}; break;
        case PL_CAVE: topC = {56, 50, 54, 255}; sideC = {18, 15, 17, 255}; break;
        case PL_WEEDS: topC = {176, 160, 116, 255}; sideC = {44, 56, 52, 255}; break;
        case PL_ATLANTIS: topC = {150, 160, 168, 255}; sideC = {38, 44, 58, 255}; break;
        default: topC = {150, 104, 64, 255}; sideC = {64, 40, 24, 255}; break;
    }
    if (!Solid(p, x, y - 1)) {
        DrawTri({px, py}, {px + T, py}, {px + T + DEPTH, py - DEPTH}, topC);
        DrawTri({px, py}, {px + T + DEPTH, py - DEPTH}, {px + DEPTH, py - DEPTH}, topC);
    }
    if (!Solid(p, x + 1, y)) {
        DrawTri({px + T, py}, {px + T + DEPTH, py - DEPTH}, {px + T + DEPTH, py + T - DEPTH}, sideC);
        DrawTri({px + T, py}, {px + T + DEPTH, py + T - DEPTH}, {px + T, py + T}, sideC);
    }
}

// A free-standing column (one or two tiles wide) is capped top and bottom, like a real pipe or beam:
// a flange plate in brass on the Pipes, a rusted collar on the Hull, an iron-banded beam-end on the ship.
void DrawPillarCaps(const PlatformState& p, int x, int y) {
    bool L1 = Solid(p, x - 1, y), R1 = Solid(p, x + 1, y);
    bool narrow = (!L1 && !R1) || (!L1 && R1 && !Solid(p, x + 2, y)) || (L1 && !R1 && !Solid(p, x - 2, y));
    if (!narrow) return;
    bool top = !Solid(p, x, y - 1) && Solid(p, x, y + 1), bottom = !Solid(p, x, y + 1) && Solid(p, x, y - 1);
    if (!top && !bottom) return;
    float px = x * (float)T, py = y * (float)T;
    float capY = top ? py : py + T - 8;
    Color plate = p.level == PL_PIPES ? Color{184, 140, 60, 255} : p.level == PL_HULL ? Color{128, 78, 46, 255} : p.level == PL_ISLAND ? Color{110, 84, 50, 255} : p.level == PL_CAVE ? Color{90, 84, 88, 255} : p.level == PL_WEEDS ? Color{120, 110, 80, 255} : p.level == PL_ATLANTIS ? Color{176, 180, 170, 255} : Color{92, 70, 60, 255};
    Color dark = ColorBrightness(plate, -0.5f);
    float x0 = (!L1 ? px : px - 0) - (!L1 ? 4 : 0), x1 = (!R1 ? px + T + 4 : px + T);
    DrawRectangle((int)x0, (int)capY, (int)(x1 - x0), 8, dark);
    DrawRectangle((int)x0 + 2, (int)capY + 2, (int)(x1 - x0) - 4, 4, plate);
    DrawRectangle((int)x0 + 3, (int)capY + 3, 2, 2, Color{240, 220, 160, 255});
    DrawRectangle((int)x1 - 5, (int)capY + 3, 2, 2, Color{240, 220, 160, 255});
}

// Foreground dressing that wraps the hazards' edges and ties the depth layers together: fronds curling over
// the Hull's urchins, steam-pipe condensation on the Pipes, rope lashings on the ship's spike frames.
void DrawHazardOverlay(const PlatformState& p, int c0, int c1, int r0, int r1, float t) {
    for (int y = r0; y <= r1; y++)
        for (int x = c0; x <= c1; x++) {
            char hc = p.tiles[y][x];
            if (hc == 'x' || hc == 'g' || hc == 't') { // one warning colour for every hazard: pulsing corner brackets
                int bx = x * T, by = y * T;
                unsigned char a = (unsigned char)(150 + 90 * sinf(t * 5 + x));
                Color w{255, 96, 52, a};
                for (int sx = 0; sx < 2; sx++)
                    for (int sy = 0; sy < 2; sy++) {
                        int cx = bx + sx * (T - 2), cy = by + sy * (T - 2);
                        DrawRectangle(sx ? cx - 4 : cx, cy, 6, 2, w);
                        DrawRectangle(cx, sy ? cy - 4 : cy, 2, 6, w);
                    }
            }
            if (hc != 'x') continue;
            float px = x * (float)T, py = y * (float)T;
            if (p.level == PL_HULL) {
                for (int k = 0; k < 2; k++) {
                    float sway = sinf(t * 2 + x + k * 2) * 3, bx = px + (k ? T - 6 : 4);
                    DrawLineEx({bx, py + T}, {bx + sway, py + 12}, 3, Color{28, 96, 70, 255});
                    DrawLineEx({bx + sway, py + 12}, {bx + sway * 1.6f + (k ? -4 : 4), py + 6}, 2, Color{40, 128, 90, 255});
                }
            } else if (p.level == PL_PIRATE) {
                DrawRectangle((int)px + 3, (int)py + 14, 2, 10, Color{200, 180, 130, 255});
                DrawRectangle((int)px + T - 5, (int)py + 14, 2, 10, Color{200, 180, 130, 255});
            } else if (p.level == PL_WEEDS) { // kelp fronds leaning over the urchins
                for (int k = 0; k < 2; k++) { float sway = sinf(t * 1.6f + x + k * 2) * 3, bx = px + (k ? T - 6 : 4); DrawLineEx({bx, py + T}, {bx + sway, py + 8}, 3, Color{60, 110, 60, 255}); }
            } else if (p.level == PL_ISLAND) { // broad leaves curling over the thorn bed
                for (int k = 0; k < 2; k++) { float bx = px + (k ? T - 8 : 6); DrawEllipse((int)bx, (int)py + 10, 6, 3, Color{70, 112, 46, 220}); }
            } else {
                DrawRectangle((int)px + 10, (int)py + 10, 2, 6, Color{120, 150, 160, 170});
            }
        }
}

// Weathering laid over every solid tile: rust running down from the rivets, salt crust along the tops, and
// hairline cracks, placed by a hash of the tile so it never crawls.
void DrawTileGrit(const PlatformState& p, int x, int y) {
    float px = x * (float)T, py = y * (float)T;
    float h1 = Hs(x * 3.1f + y * 7.7f), h2 = Hs(x * 5.3f + y * 2.9f + 4), h3 = Hs(x * 1.7f + y * 9.1f + 9);
    if (h1 > 0.55f) { // a rust streak
        Color rust = p.level == PL_PIRATE ? Color{70, 44, 26, 150} : p.level == PL_ISLAND ? Color{56, 82, 40, 150} : p.level == PL_CAVE ? Color{70, 100, 110, 130} : p.level == PL_WEEDS ? Color{60, 96, 70, 140} : p.level == PL_ATLANTIS ? Color{60, 110, 120, 130} : Color{120, 62, 34, 150};
        DrawRectangle((int)px + 6 + (int)(h2 * 16), (int)py + 4, 3, 8 + (int)(h3 * 20), rust);
        DrawRectangle((int)px + 7 + (int)(h2 * 16), (int)py + 4, 1, 8 + (int)(h3 * 20), Fade(BLACK, 0.4f));
    }
    if (h2 > 0.82f) { // a crack
        Color ink{10, 10, 14, 210};
        Vector2 a{px + 8 + h1 * 12, py + 4}, b{a.x + 5, a.y + 9}, c{b.x - 4, b.y + 9}, d{c.x + 6, c.y + 8};
        DrawLineEx(a, b, 1, ink); DrawLineEx(b, c, 1, ink); DrawLineEx(c, d, 1, ink);
    }
    if (!Solid(p, x, y - 1) && h3 > 0.5f) // salt crust along the top edge
        for (int k = 0; k < 3; k++) DrawRectangle((int)px + 3 + k * 10 + (int)(h1 * 5), (int)py, 4, 2, Color{214, 210, 196, 200});
}

// Weathering over a pipe run: an ink edge on the shadow side, rust blooms, dents, weld seams and stencilled hatching.
void DrawPipeGrit(float px, float py, int len, bool horiz, int seed) {
    float h = Hs(seed * 4.3f + px * 0.07f + py * 0.05f);
    Color ink{8, 8, 12, 255}, rust{132, 68, 36, 210}, seam{30, 26, 24, 255};
    if (horiz) {
        DrawRectangle((int)px, (int)py + 13, len, 2, Fade(ink, 0.7f));                                     // shadow-side ink
        DrawRectangle((int)px + 12, (int)py, 2, 14, seam);                                                 // weld seam
        DrawRectangle((int)px + 11, (int)py + 1, 1, 2, Color{200, 190, 160, 255});                         // seam sparkle
        DrawRectangle((int)px + 4 + (int)(h * 12), (int)py + 3, 7, 4, rust);                               // rust bloom
        DrawRectangle((int)px + 6 + (int)(h * 12), (int)py + 7, 1, 5, rust);                               // running down
        for (int k = 0; k < 3; k++) DrawRectangle((int)px + 18 + k * 3, (int)py + 4 + k * 2, 2, 1, ink);   // hatching
        if (h > 0.6f) DrawEllipse((int)px + 22, (int)py + 7, 4, 3, Fade(ink, 0.55f));                     // a dent
    } else {
        DrawRectangle((int)px + 13, (int)py, 2, len, Fade(ink, 0.7f));
        DrawRectangle((int)px, (int)py + 12, 14, 2, seam);
        DrawRectangle((int)px + 3, (int)py + 4 + (int)(h * 12), 4, 7, rust);
        DrawRectangle((int)px + 4, (int)py + 11 + (int)(h * 12), 1, 5, rust);
        for (int k = 0; k < 3; k++) DrawRectangle((int)px + 4 + k * 2, (int)py + 20 + k * 3, 1, 2, ink);
        if (h > 0.6f) DrawEllipse((int)px + 7, (int)py + 24, 3, 4, Fade(ink, 0.55f));
    }
}
// Things that give light: an emergency lamp hung from the duct ceiling, a lantern on a post along the ship's deck.
bool CeilingLamp(const PlatformState& p, int x, int y) { return p.level == PL_PIPES && At(p, x, y) == '#' && !Solid(p, x, y + 1) && !Solid(p, x, y + 2) && Hs(x * 4.1f + y * 9.3f) > 0.88f; }
bool DeckLantern(const PlatformState& p, int x, int y) { return p.level == PL_PIRATE && At(p, x, y) == '#' && !Solid(p, x, y - 1) && !Solid(p, x, y - 2) && !Solid(p, x, y - 3) && (x * 7 + y) % 9 == 0; }
bool IslandTotem(const PlatformState& p, int x, int y) { return p.level == PL_ISLAND && At(p, x, y) == '#' && !Solid(p, x, y - 1) && !Solid(p, x, y - 2) && !Solid(p, x, y - 3) && !Solid(p, x, y - 4) && Hs(x * 5.3f + y * 2.1f) > 0.82f; }
constexpr Color WARN{255, 96, 52, 255}; // every hazard on every level carries this colour

// Wear and variety laid over the exposed faces of solid tiles, chosen by tile hashes so it never crawls: access plates,
// hazard tape and drips in the duct; shells, algae and barnacle clusters on the coral; warped and mismatched boards,
// water stains, moss and rope coils on the ship.
void DrawTileDetail(const PlatformState& p, int x, int y, float t) {
    uint8_t m = SolidMask(p, x, y);
    if (m == 15) return; // the inside of a mass stays calm
    int px = x * T, py = y * T;
    float h1 = Hs(x * 2.3f + y * 5.9f + 1), h2 = Hs(x * 7.7f + y * 1.3f + 2), h3 = Hs(x * 4.1f + y * 3.7f + 3);
    Color ink{8, 8, 12, 255};
    if (p.level == PL_PIPES) {
        if (h1 > 0.86f) { // a riveted access plate with a brass tag
            DrawRectangle(px + 4, py + 6, 24, 18, ink);
            DrawRectangle(px + 5, py + 7, 22, 16, Color{70, 62, 56, 255});
            DrawRectangle(px + 5, py + 7, 22, 2, Color{104, 92, 80, 255});
            for (int k = 0; k < 4; k++) DrawRectangle(px + 7 + (k % 2) * 17, py + 9 + (k / 2) * 11, 2, 2, Color{150, 132, 106, 255});
            DrawRectangle(px + 12, py + 13, 8, 4, Color{184, 140, 60, 255});
        } else if (h2 > 0.9f && !(m & 4)) { // hazard tape along the underside
            for (int k = 0; k < 8; k++) DrawRectangle(px + k * 4, py + T - 6, 4, 4, k % 2 ? Color{232, 196, 52, 255} : Color{22, 20, 18, 255});
        }
        if (!(m & 4) && h3 > 0.45f) { // condensation gathers and drips
            float ph = fmodf(t * 0.8f + h1 * 7, 2.0f);
            int dx = px + 6 + (int)(h2 * 18);
            DrawRectangle(dx, py + T - 2, 2, 3, Color{120, 170, 190, 255});
            if (ph < 1.0f) DrawRectangle(dx, py + T + (int)(ph * ph * 40), 2, 3, Fade(Color{150, 200, 220, 255}, 1 - ph));
        }
        if (!(m & 1) && h3 > 0.7f) DrawRectangle(px + 4, py + 3, 12, 2, Color{170, 150, 120, 255}); // a scuffed, polished top edge
    } else if (p.level == PL_HULL) {
        if (p.tiles[y][x] != 'R') return; // steel plating stays clean; the marine growth lives on the rocks
        if (!(m & 1)) { // algae fuzz on top
            for (int k = 0; k < 7; k++) {
                float sw = sinf(t * 1.6f + x + k) * 1.2f;
                DrawRectangle(px + 2 + k * 4 + (int)sw, py - 2 - (int)(Hs(x * 1.9f + k) * 3), 2, 3, Fade(Color{60, 150, 96, 255}, 0.85f));
            }
            if (h2 > 0.72f) { // a scallop shell wedged in the crust
                float cx = px + 8 + h1 * 14, cy = py + 6;
                DrawCircleSector({cx, cy + 1}, 6, 180, 360, 8, ink);
                DrawCircleSector({cx, cy + 1}, 5, 180, 360, 8, Color{226, 190, 160, 255});
                for (int k = -2; k <= 2; k++) DrawLineEx({cx, cy + 1}, {cx + k * 2.2f, cy - 4}, 1, Color{150, 108, 90, 255});
            }
        }
        if (!(m & 2) && h3 > 0.76f) Barnacles(px + T - 6.0f, py + 10 + h1 * 12); // a cluster on the right face
        if (!(m & 4) && h1 > 0.6f) for (int k = 0; k < 3; k++) DrawCircle(px + 7 + k * 9, py + T - 3, 2, Color{224, 212, 186, 255}); // a row of barnacles under an overhang
    } else if (gGhost) { // the drowned ship: rot-grey timber, splintered edges, holes that show the dark inside
        DrawRectangle(px, py, T, T, Fade(Color{40, 74, 68, 255}, 0.42f));
        if (!(m & 1)) for (int k = 0; k < 4; k++) DrawRectangle(px + 2 + k * 8, py - 1 - (int)(Hs(x * 2.7f + k) * 3), 3, 3, Color{60, 84, 78, 255}); // splintered top edge
        if (h1 > 0.62f && (m & 1)) { // a rotted-through hole in the plank
            int hx = px + 5 + (int)(h2 * 12), hw = 8 + (int)(h3 * 8);
            DrawRectangle(hx, py + 8, hw, 12, Color{6, 10, 12, 255});
            DrawRectangle(hx - 2, py + 10, 2, 8, Color{30, 44, 42, 255}); DrawRectangle(hx + hw, py + 10, 2, 8, Color{30, 44, 42, 255});
            DrawRectangle(hx + 2, py + 12, 2, 2, Fade(Color{110, 250, 200, 255}, 0.7f)); // a spark of ghostfire within
        }
        if (h2 > 0.7f) DrawRectangle(px + 6, py + 4, 2, T - 8, Color{6, 10, 12, 200});   // a wide crack
        if (h3 > 0.8f && !(m & 4)) DrawRectangle(px + 2, py + T - 6, 12, 5, Color{70, 120, 92, 255});  // green slime
    } else {
        if (!(m & 1)) { // deck boards: mismatched, warped, one sprung
            int row = (x + y) % 3;
            DrawRectangle(px, py + 5 + row * 8, T, 7, Fade(row == 1 ? Color{146, 98, 60, 255} : Color{92, 60, 36, 255}, 0.55f));
            if (h2 > 0.8f) { DrawRectangle(px + 4, py + 4, 22, 2, Color{40, 24, 14, 255}); DrawRectangle(px + 4, py + 2, 22, 2, Color{190, 142, 90, 255}); } // a warped board lifting
            if (h3 > 0.88f) { // a coil of rope
                DrawEllipse(px + 16, py - 2, 8, 3, ink);
                DrawEllipse(px + 16, py - 3, 7, 2, Color{190, 168, 116, 255});
                DrawEllipse(px + 16, py - 3, 3, 1, Color{100, 84, 56, 255});
            }
        }
        if (h1 > 0.6f) DrawRectangle(px + 3 + (int)(h2 * 20), py + 6, 4, 6 + (int)(h3 * 16), Fade(Color{20, 14, 10, 255}, 0.35f)); // a water stain running down
        if (h3 > 0.8f && !(m & 4)) { DrawRectangle(px + 3, py + T - 7, 10, 4, Color{56, 98, 60, 255}); DrawRectangle(px + 5, py + T - 8, 5, 2, Color{86, 132, 78, 255}); } // moss under the hull
        if (h2 < 0.08f) { DrawCircle(px + 16, py + 16, 3, ink); DrawCircle(px + 16, py + 16, 2, Color{74, 46, 26, 255}); } // a knot in the wood
    }
}

void DrawTile(const PlatformState& p, char c, int x, int y, float t) {
    float px = x * (float)T, py = y * (float)T;
    switch (c) {
        case 'D': // a creature den: solid underfoot like any floor; the breach itself is drawn with the creatures (DrawFauna)
            DrawSolid(p, x, y); DrawTileGrit(p, x, y);
            break;
        case '#':
            DrawSolid(p, x, y); DrawPillarCaps(p, x, y); DrawTileGrit(p, x, y); DrawTileDetail(p, x, y, t);
            if (CeilingLamp(p, x, y)) { // a caged emergency lamp, flickering red
                float fl = 0.6f + 0.4f * sinf(t * 7 + x * 1.7f) * sinf(t * 3.1f + y);
                DrawRectangle((int)px + 14, (int)py + T, 4, 5, Color{40, 36, 34, 255});
                DrawRectangle((int)px + 8, (int)py + T + 5, 16, 10, Color{8, 8, 12, 255});
                DrawRectangle((int)px + 10, (int)py + T + 7, 12, 6, Color{(unsigned char)(120 + 100 * fl), 24, 20, 255});
                for (int k = 0; k < 3; k++) DrawRectangle((int)px + 11 + k * 4, (int)py + T + 6, 1, 8, Color{20, 12, 12, 255});
            }
            if (DeckLantern(p, x, y)) { // a lantern on a post
                float fl = 0.85f + 0.15f * sinf(t * 9 + x);
                DrawRectangle((int)px + 14, (int)py - 26, 3, 26, Color{60, 40, 26, 255});
                DrawRectangle((int)px + 8, (int)py - 38, 15, 13, Color{8, 8, 12, 255});
                if (gGhost) { // ghostfire in the glass, licking upward
                    DrawRectangle((int)px + 10, (int)py - 36, 11, 9, Color{(unsigned char)(70 + 40 * fl), (unsigned char)(200 + 50 * fl), (unsigned char)(170 + 40 * fl), 255});
                    for (int k = 0; k < 3; k++) { float ph = fmodf(t * 0.9f + k * 0.33f, 1.0f); DrawRectangle((int)px + 13 + (int)(sinf(t * 3 + k * 2) * 4), (int)(py - 40 - ph * 22), 2, 3, Fade(Color{120, 255, 210, 255}, 1 - ph)); }
                } else
                DrawRectangle((int)px + 10, (int)py - 36, 11, 9, Color{(unsigned char)(200 + 55 * fl), (unsigned char)(150 + 60 * fl), 60, 255});
                DrawRectangle((int)px + 7, (int)py - 40, 17, 3, Color{60, 58, 62, 255});
            }
            if (IslandTotem(p, x, y)) { // a totem pole planted in the ground - carved rings, a skull lashed to the top, a feather trailing
                float sway = sinf(t * 0.8f + x) * 1.5f;
                DrawRectangle((int)px + 13, (int)py - 46, 5, 46, Color{74, 56, 34, 255});
                for (int k = 0; k < 3; k++) DrawRectangle((int)px + 11, (int)py - 12 - k * 12, 9, 3, Color{50, 36, 20, 255}); // carved rings
                DrawCircle((int)px + 15, (int)py - 46, 6, Color{224, 216, 196, 255});                 // the skull
                DrawCircle((int)(px + 13), (int)py - 46, 1.4f, Color{20, 18, 16, 255}); DrawCircle((int)(px + 17), (int)py - 46, 1.4f, Color{20, 18, 16, 255});
                DrawLineEx({px + 15.0f + sway, py - 52.0f}, {px + 19.0f + sway * 1.6f, py - 58.0f}, 1.4f, Color{200, 60, 50, 255}); // a trailing dyed feather
            }
            break;
        case 'l': { // ratlines: a rope ladder, two shrouds with rungs between, swaying a little
            float sw = sinf(t * 1.2f + y * 0.7f) * 1.2f;
            Color rope{176, 150, 100, 255}, ropeDk{96, 76, 48, 255};
            DrawRectangle((int)(px + 7 + sw), (int)py, 2, T, ropeDk); DrawRectangle((int)(px + 8 + sw), (int)py, 1, T, rope);
            DrawRectangle((int)(px + 22 + sw), (int)py, 2, T, ropeDk); DrawRectangle((int)(px + 23 + sw), (int)py, 1, T, rope);
            for (int k = 0; k < 4; k++) { DrawRectangle((int)(px + 7 + sw), (int)py + 3 + k * 8, 18, 3, Color{8, 8, 12, 255}); DrawRectangle((int)(px + 8 + sw), (int)py + 4 + k * 8, 16, 1, Color{150, 108, 64, 255}); }
        } break;
        case 'i': { // below decks, behind the timbers: planking, ribs, a lamp, and a cabin door now and then
            DrawRectangle((int)px, (int)py, T, T, gGhost ? Color{16, 30, 30, 255} : Color{34, 22, 16, 255});
            DrawRectangle((int)px + 11, (int)py, 1, T, Color{24, 14, 10, 255});
            DrawRectangle((int)px, (int)py, T, 2, Color{54, 34, 22, 255});
            if (x % 6 == 0 && At(p, x, y + 1) != 'i') { // a door in the bulkhead, on the room's floor
                DrawRectangle((int)px + 4, (int)py + 4, 24, T - 4, Color{58, 36, 22, 255});
                DrawRectangle((int)px + 6, (int)py + 6, 20, T - 8, Color{78, 50, 30, 255});
                DrawCircle((int)px + 22, (int)py + 18, 2, Pal::Brass);
            }
            if (x % 6 == 3) { // a bulkhead lamp
                float fl = 0.8f + 0.2f * sinf(t * 8 + x);
                DrawRectangle((int)px + 13, (int)py + 6, 6, 8, Color{8, 8, 12, 255});
                DrawRectangle((int)px + 14, (int)py + 7, 4, 6, gGhost ? Color{110, 250, 210, 255} : Color{(unsigned char)(220 * fl + 30), (unsigned char)(170 * fl + 30), 80, 255});
            }
        } break;
        case 'R': DrawSolid(p, x, y); DrawTileGrit(p, x, y); DrawTileDetail(p, x, y, t); break;
        case 'T': { // a torpedo tube set into a steel housing: a dark bore facing left, hazard bands, a red lamp that warns before it fires
            DrawHullSteel(p, x, y);
            float al = LauncherAlert(p, x, y);
            DrawRectangle((int)px, (int)py + 4, T, 24, Color{8, 10, 14, 255});
            DrawRectangle((int)px + 2, (int)py + 6, T - 4, 20, Color{58, 66, 76, 255});
            DrawCircle((int)px + 6, (int)py + 16, 11, Color{8, 10, 14, 255});
            DrawCircle((int)px + 6, (int)py + 16, 9, al > 0 ? Color{(unsigned char)(80 + 160 * al), 40, 30, 255} : Color{22, 26, 32, 255});
            DrawRing({px + 6, py + 16}, 9, 11, 0, 360, 14, Color{150, 164, 176, 255});
            for (int k = 0; k < 4; k++) DrawRectangle((int)px + 14 + k * 4, (int)py + 4, 2, 24, k % 2 ? Color{232, 196, 52, 255} : Color{22, 20, 18, 255});
            DrawCircle((int)px + T - 6, (int)py + 8, 3, al > 0 && fmodf(t * 10, 1.0f) < 0.5f ? Color{255, 60, 40, 255} : Color{90, 30, 26, 255});
            if (al > 0) { BeginBlendMode(BLEND_ADDITIVE); DrawCircleV({px + 4, py + 16}, 20 + 10 * al, Color{255, 90, 50, (unsigned char)(50 * al)}); EndBlendMode(); }
        } break;
        case 'N': { // a deck cannon: an iron barrel on a wooden carriage, muzzle to the left, a smoking fuse before it fires
            float al = LauncherAlert(p, x, y);
            DrawRectangle((int)px - 8, (int)py + 8, 30, 12, Color{8, 8, 12, 255});
            DrawRectangle((int)px - 6, (int)py + 10, 26, 8, Color{58, 60, 66, 255});
            DrawRectangle((int)px - 6, (int)py + 10, 26, 3, Color{110, 114, 122, 255});
            DrawRectangle((int)px - 9, (int)py + 7, 5, 14, Color{40, 42, 48, 255});
            DrawRectangle((int)px + 2, (int)py + 18, 26, 12, Color{112, 74, 42, 255});
            DrawRectangle((int)px + 2, (int)py + 18, 26, 3, Color{164, 116, 72, 255});
            DrawCircle((int)px + 8, (int)py + 28, 6, Color{8, 8, 12, 255}); DrawCircle((int)px + 8, (int)py + 28, 4, Color{92, 60, 34, 255});
            DrawCircle((int)px + 22, (int)py + 28, 6, Color{8, 8, 12, 255}); DrawCircle((int)px + 22, (int)py + 28, 4, Color{92, 60, 34, 255});
            if (al > 0) { DrawRectangle((int)px + 20, (int)py + 4, 2, 6, Color{60, 50, 40, 255}); DrawCircle((int)px + 21, (int)py + 3, 2 + 2 * al, Fade(Color{255, 180, 60, 255}, 0.9f)); BeginBlendMode(BLEND_ADDITIVE); DrawCircleV({px - 8, py + 14}, 12 + 12 * al, Color{255, 140, 50, (unsigned char)(70 * al)}); EndBlendMode(); }
        } break;
        case 'y': { // a barrel chute: a hatch of planks with a sluice of barrels stacked behind it, its bolt drawn back to warn
            float al = LauncherAlert(p, x, y);
            DrawRectangle((int)px, (int)py, T, T, Color{8, 8, 12, 255});
            DrawRectangle((int)px + 2, (int)py + 2, T - 4, T - 4, Color{120, 80, 46, 255});
            for (int k = 1; k < 4; k++) DrawRectangle((int)px + 2, (int)py + k * 8, T - 4, 1, Color{74, 46, 26, 255});
            DrawRectangle((int)px + 2, (int)py + 4 + (int)(al * 6), 5, 10, Color{54, 52, 56, 255});                    // the iron bolt
            DrawCircle((int)px + 14, (int)py + 22, 6, Color{150, 100, 56, 255}); DrawCircleLines((int)px + 14, (int)py + 22, 6, Color{60, 40, 22, 255});
            DrawRectangle((int)px + 4, (int)py + 26, T - 8, 2, Color{190, 150, 60, 255});
            if (al > 0.3f) DrawRectangle((int)px - 3, (int)py + 4, 3, T - 8, Fade(Color{255, 190, 60, 255}, al));
        } break;
        case 'k': { // a crate or a barrel
            bool barrel = Hs(x * 7.1f + y * 3.3f) > 0.5f;
            if (barrel) {
                DrawRectangleRounded({px + 2, py, T - 4.0f, (float)T}, 0.35f, 4, Color{120, 76, 40, 255});
                DrawRectangle((int)px + 6, (int)py + 1, 4, T - 2, Color{150, 100, 58, 255});
                DrawRectangle((int)px + 2, (int)py + 5, T - 4, 3, Color{60, 58, 62, 255});  // iron hoops
                DrawRectangle((int)px + 2, (int)py + T - 8, T - 4, 3, Color{60, 58, 62, 255});
                DrawRectangle((int)px + 22, (int)py + 2, 2, T - 4, Color{90, 56, 28, 255});
                DrawRectangleRoundedLinesEx({px + 2, py, T - 4.0f, (float)T}, 0.35f, 4, 2, Color{8, 8, 12, 255});
                for (int k = 0; k < 4; k++) DrawRectangle((int)px + 3 + k * 7, (int)py + 5, 1, 3, Color{190, 110, 60, 255}); // rust bleeding from the hoop
                for (int k = 0; k < 3; k++) DrawRectangle((int)px + 4 + k * 8, (int)py + 10 + k * 5, 2, 1, Color{40, 26, 14, 255}); // hatching
                DrawRectangle((int)px + 5, (int)py + 6, 2, 2, Color{20, 20, 24, 255}); DrawRectangle((int)px + T - 8, (int)py + 6, 2, 2, Color{20, 20, 24, 255}); // hoop rivets
            } else {
                DrawRectangle((int)px, (int)py, T, T, Color{150, 108, 62, 255});
                DrawRectangleLines((int)px, (int)py, T, T, Color{80, 54, 30, 255});
                DrawRectangleLines((int)px + 3, (int)py + 3, T - 6, T - 6, Color{96, 66, 36, 255});
                DrawLineEx({px + 4, py + 4}, {px + T - 4, py + T - 4}, 3, Color{110, 78, 42, 255}); // cross-brace
                DrawRectangle((int)px + 2, (int)py + 2, T - 4, 2, Color{186, 140, 88, 255});
                DrawRectangleLines((int)px - 1, (int)py - 1, T + 2, T + 2, Color{8, 8, 12, 255});                       // ink outline
                DrawRectangle((int)px + T - 6, (int)py + 3, 4, T - 6, Fade(BLACK, 0.28f));                            // shadow plank
                for (int k = 0; k < 4; k++) DrawRectangle((int)px + 5 + k * 5, (int)py + 20 + k % 2 * 3, 3, 1, Color{50, 32, 18, 255}); // hatching
                for (int q = 0; q < 4; q++) DrawRectangle((int)px + 4 + (q % 2) * (T - 10), (int)py + 4 + (q / 2) * (T - 10), 2, 2, Color{60, 58, 62, 255}); // iron nails
                DrawRectangle((int)px + 9, (int)py + 22, 1, 6, Color{120, 64, 36, 200});                                // rust drip
                DrawLineEx({px + 14, py + 3}, {px + 17, py + 9}, 1, Color{20, 12, 8, 255});                             // a split in the wood
            }
        } break;
        case '=': {
            if (p.level == PL_PIRATE) { // a yardarm: a spar crossing a mast, iron-capped at its ends, lashed where it meets the mast
                uint8_t m = TileMask(p, x, y, [](char c) { return c == '=' || c == '|' || c == 'r'; });
                bool cross = (m & 1) || (m & 4);
                DrawRectangle((int)px, (int)py + 5, T, 16, Color{8, 8, 12, 255});
                DrawRectangle((int)px, (int)py + 7, T, 12, Color{112, 76, 44, 255});
                DrawRectangle((int)px, (int)py + 7, T, 3, Color{164, 120, 72, 255});
                DrawRectangle((int)px, (int)py + 16, T, 3, Color{70, 46, 26, 255});
                for (int k = 0; k < 3; k++) DrawRectangle((int)px + 4 + k * 9, (int)py + 11 + (k % 2) * 3, 5, 1, Color{86, 56, 32, 255});   // grain
                if (!(m & 8)) { DrawRectangle((int)px, (int)py + 5, 5, 16, Color{56, 54, 58, 255}); DrawRectangle((int)px + 1, (int)py + 8, 2, 2, Color{176, 176, 182, 255}); }   // iron end-caps
                if (!(m & 2)) { DrawRectangle((int)px + T - 5, (int)py + 5, 5, 16, Color{56, 54, 58, 255}); DrawRectangle((int)px + T - 3, (int)py + 8, 2, 2, Color{176, 176, 182, 255}); }
                if (cross) for (int k = 0; k < 3; k++) DrawRectangle((int)px + 8, (int)py + 5 + k * 5, 16, 2, Color{204, 184, 134, 255});      // rope lashings at the mast
                else if (x % 4 == 1) { float sw = sinf(p.time * 1.5f + x) * 1.5f; DrawTri({px + 4, py + 19}, {px + 28, py + 19}, {px + 16 + sw, py + 34}, Color{8, 8, 12, 255}); DrawTri({px + 6, py + 19}, {px + 26, py + 19}, {px + 16 + sw, py + 31}, Color{184, 176, 152, 255}); }   // a furled sail
                break;
            }
            if (p.level == PL_WEEDS) { // a kelp float: a raft of gas bladders on its own stalk, bobbing
                float bob = sinf(p.time * 1.4f + x * 0.9f) * 1.5f;
                for (int k = 0; k < 3; k++) {
                    float bx = px + 6 + k * 10, by = py + 8 + bob + (k % 2);
                    DrawEllipse((int)bx, (int)by, 7, 6, Color{8, 12, 10, 255});
                    DrawEllipse((int)bx, (int)by, 5.5f, 4.5f, Color{150, 132, 60, 255});
                    DrawEllipse((int)bx - 1, (int)by - 2, 2.5f, 1.5f, Color{210, 196, 120, 255});
                }
                DrawLineEx({px, py + 12 + bob}, {px + T, py + 12 + bob}, 3, Color{92, 110, 50, 255}); // the frond they grow from
                for (int k = 0; k < 2; k++) DrawTri({px + 4 + k * 16, py + 13 + bob}, {px + 12 + k * 16, py + 13 + bob}, {px + 10 + k * 16, py + 22 + bob}, Color{80, 130, 70, 255});
                break;
            }
            if (p.level == PL_ATLANTIS) { // an aqueduct span: a stone channel on its arches, water still spilling over the lip
                uint8_t am = TileMask(p, x, y, [](char c) { return c == '=' || c == '#'; });
                DrawRectangle((int)px, (int)py, T, 14, Color{16, 18, 26, 255});
                DrawRectangle((int)px, (int)py + 1, T, 12, Color{124, 130, 136, 255});
                DrawRectangle((int)px, (int)py + 1, T, 3, Color{164, 170, 172, 255});
                for (int k = 0; k < 2; k++) DrawRectangle((int)px + 4 + k * 16, (int)py + 5, 1, 8, Color{64, 70, 82, 255});
                if (!(am & 4)) DrawRing({px + 16, py + 14}, 12, 15, 0, 180, 12, Color{96, 102, 112, 255}); // the arch under it
                if (x % 3 == 0) for (int k = 0; k < 4; k++) { float ph = fmodf(p.time * 1.5f + k * 0.25f + x, 1.0f); DrawRectangle((int)px + 20, (int)(py + 12 + ph * 16), 2, 3, Fade(Color{180, 220, 240, 255}, 0.5f * (1 - ph))); } // a trickle over the lip
                break;
            }
            Color pipe = p.level == PL_PIPES ? Color{176, 104, 62, 255} : Color{120, 124, 118, 255};
            uint8_t m = TileMask(p, x, y, [](char c) { return c == '=' || c == '|'; });
            bool hasW = m & 8, hasE = m & 2, up = m & 1, down = m & 4;
            float hx0 = hasW ? px : px + 6, hx1 = hasE ? px + T : px + T - 6;
            bool lone = !hasW && !hasE && !up && !down;
            if (lone) { hx0 = px; hx1 = px + T; }
            if (!hasW && !hasE && (up || down)) hx1 = hx0;                                      // a pure T stem: no horizontal body
            if (hx1 > hx0 + 1) DrawPipeH(hx0, hx1, py + 16, 14, pipe);
            if (lone || hasW || hasE) {
                if (!hasW) DrawFlange({px + 3, py + 16}, 12, false, Pal::BrassDk);
                if (!hasE) DrawFlange({px + T - 3, py + 16}, 12, false, Pal::BrassDk);
            }
            if (up) DrawPipeV(px + 16, py, py + 16, 14, pipe);
            if (down) DrawPipeV(px + 16, py + 16, py + T, 14, pipe);
            if (up || down) { // a joint where the network branches: an elbow, a tee or a cross, with a brass collar
                DrawCircleV({px + 16, py + 16}, 15, Color{8, 8, 12, 255}); DrawCircleV({px + 16, py + 16}, 13, Pal::BrassDk); DrawCircleV({px + 14, py + 14}, 5, Color{232, 196, 110, 255});
                for (int k = 0; k < 4; k++) DrawCircleV({px + 16 + cosf(k * PI / 2 + PI / 4) * 10, py + 16 + sinf(k * PI / 2 + PI / 4) * 10}, 1.4f, Color{40, 32, 24, 255});
            }
            if (hx1 > hx0 + 8) DrawPipeGrit(px, py + 9, T, true, x);
            if (x % 4 == 0 && hasE) DrawCircleV({px + T, py + 4}, 3, Color{220, 60, 40, 255}); // a valve wheel
        } break;
        case '|': {
            if (p.level == PL_PIRATE) { // a mast: a pole of pale timber, banded in iron and lashed with rope; two tiles make one thick mast
                uint8_t m = TileMask(p, x, y, [](char c) { return c == '|' || c == '='; });
                float x0 = (m & 8) ? px : px + 4, x1 = (m & 2) ? px + T : px + T - 4;
                bool topEnd = !(m & 1), footEnd = !(m & 4);
                DrawRectangle((int)x0 - 1, (int)py, (int)(x1 - x0) + 2, T, Color{8, 8, 12, 255});
                DrawRectangle((int)x0, (int)py, (int)(x1 - x0), T, Color{148, 108, 66, 255});
                DrawRectangle((int)x0, (int)py, (m & 8) ? 0 : 4, T, Color{196, 156, 104, 255});                                    // the lit edge
                DrawRectangle((int)x1 - ((m & 2) ? 0 : 4), (int)py, (m & 2) ? 0 : 4, T, Color{92, 62, 36, 255});                    // the shaded edge
                for (int k = 0; k < 3; k++) DrawRectangle((int)x0 + 3 + (k * 7 + y * 3) % 12, (int)py + 4 + k * 10, 1, 8, Color{104, 74, 44, 255});   // grain
                if (y % 3 == 0) { DrawRectangle((int)x0, (int)py + 12, (int)(x1 - x0), 5, Color{56, 54, 58, 255}); DrawRectangle((int)x0 + 3, (int)py + 13, 2, 2, Color{176, 176, 182, 255}); }   // an iron band
                else if (y % 3 == 1) for (int k = 0; k < 4; k++) DrawLineEx({x0, py + 4.0f + k * 6}, {x1, py + 8.0f + k * 6}, 2, Color{200, 180, 130, 255});   // rope lashing
                if (footEnd) { DrawRectangle((int)x0 - 2, (int)py + T - 7, (int)(x1 - x0) + 4, 7, Color{40, 38, 42, 255}); }        // the mast-step collar
                if (topEnd && !(m & 8)) { // the masthead: a truck, and a pennant
                    DrawRectangle((int)x0 - 3, (int)py - 3, (int)(x1 - x0) + 12, 5, Color{56, 54, 58, 255});
                    DrawLineEx({x1 - 2, py - 3}, {x1 - 2, py - 22}, 2, Color{60, 44, 30, 255});
                    float fl = sinf(p.time * 5 + x) * 3;
                    DrawTri({x1, py - 22}, {x1 + 20 + fl, py - 18 + fl * 0.5f}, {x1, py - 12}, Color{20, 18, 24, 255});
                    DrawRectangle((int)x1 + 6, (int)py - 20, 3, 3, Color{214, 210, 196, 255});
                }
                break;
            }
            Color pipe = Color{150, 154, 146, 255};
            uint8_t m = TileMask(p, x, y, [](char c) { return c == '=' || c == '|'; });
            bool left = m & 8, right = m & 2;
            DrawPipeV(px + 16, py, py + T, 14, pipe);
            if (!(m & 1)) DrawFlange({px + 16, py + 3}, 12, true, Pal::BrassDk);
            if (!(m & 4)) DrawFlange({px + 16, py + T - 3}, 12, true, Pal::BrassDk);
            Color hpipe = p.level == PL_PIPES ? Color{176, 104, 62, 255} : Color{120, 124, 118, 255};
            if (right) DrawPipeH(px + 16, px + T, py + 16, 14, hpipe);
            if (left) DrawPipeH(px, px + 16, py + 16, 14, hpipe);
            if (left || right) { DrawCircleV({px + 16, py + 16}, 15, Color{8, 8, 12, 255}); DrawCircleV({px + 16, py + 16}, 13, Pal::BrassDk); DrawCircleV({px + 14, py + 14}, 5, Color{232, 196, 110, 255}); }
            DrawPipeGrit(px + 9, py, T, false, y);
        } break;
        case 'r': { // rigging: a rope sagging between two masts, twisted, with an iron thimble at each end
            int a = x, b = x;
            while (At(p, a - 1, y) == 'r') a--;
            while (At(p, b + 1, y) == 'r') b++;
            float span = (float)(b - a + 1), u0 = (x - a) / span, u1 = (x - a + 1) / span, sag = std::min(9.0f, 3.0f + span * 0.25f);
            float sw = sinf(p.time * 1.3f + x * 0.3f) * 0.8f;
            Vector2 s0{px, py + 9 + sinf(u0 * PI) * sag + sw}, s1{px + T, py + 9 + sinf(u1 * PI) * sag + sw};
            DrawLineEx({s0.x, s0.y + 1}, {s1.x, s1.y + 1}, 5, Color{8, 8, 12, 255});
            DrawLineEx(s0, s1, 3, Color{176, 148, 98, 255});
            for (int k = 0; k < 4; k++) DrawRectangle((int)(px + 3 + k * 8), (int)(s0.y + (s1.y - s0.y) * (3 + k * 8) / T) - 1, 2, 3, Color{110, 88, 56, 255});   // twist
            if (x == a) { DrawRing({px + 2, s0.y}, 2, 4, 0, 360, 8, Color{60, 58, 62, 255}); }
            if (x == b) { DrawRing({px + T - 2, s1.y}, 2, 4, 0, 360, 8, Color{60, 58, 62, 255}); }
        } break;
        case 'w': {
            float wy = py, sway0 = sinf(p.time * 1.5f + x * 0.7f + wy * 0.05f) * 4, sway1 = sinf(p.time * 1.5f + x * 0.7f + (wy + T) * 0.05f) * 4;
            Vector2 a{px + 16 + sway0, py}, b{px + 16 + sway1, py + T};
            if (p.level == PL_ISLAND) { // a hanging liana, knotted and leafy, climbable the same way as seaweed
                DrawLineEx({a.x + 1, a.y}, {b.x + 1, b.y}, 5, Color{10, 8, 6, 255});
                DrawLineEx(a, b, 2.6f, Color{74, 58, 34, 255});
                DrawLineEx({a.x - 1, a.y}, {b.x - 1, b.y}, 1, Color{112, 90, 54, 255});
                for (int k = 0; k < 3; k++) { float ly = py + 4 + k * 10, lx = px + 16 + (sway0 + (sway1 - sway0) * (ly - py) / T); DrawCircleV({lx, ly}, 2, Color{58, 44, 26, 255}); } // knots along the vine
                for (int k = 0; k < 2; k++) { float ly = py + 8 + k * 14, lx = px + 16 + (sway0 + (sway1 - sway0) * (ly - py) / T); int side = (k + x) % 2 ? 1 : -1; DrawEllipse((int)(lx + side * 9), (int)ly, 8, 4, Color{70, 116, 48, 255}); } // broad leaves
                break;
            }
            // seaweed: a swaying ribbon of blades, climbable (hold up or down while it is around you)
            DrawLineEx({a.x + 1, a.y}, {b.x + 1, b.y}, 7, Color{8, 10, 14, 255});
            DrawLineEx(a, b, 4, Color{48, 130, 88, 255});
            DrawLineEx({a.x - 1, a.y}, {b.x - 1, b.y}, 1, Color{110, 190, 130, 255});
            for (int k = 0; k < 2; k++) { float ly = py + 6 + k * 14, lx = px + 16 + (sway0 + (sway1 - sway0) * (ly - py) / T); int side = (k + x) % 2 ? 1 : -1; DrawTri({lx, ly}, {lx + side * 12.0f, ly - 6}, {lx + side * 4.0f, ly + 4}, Color{58, 150, 100, 255}); }
        } break;        case 'f': { // fragile scaffolding: a rusted plank on brackets, shaking when it is about to go
            float shake = 0;
            for (auto& c : p.crumbles) if (c.tx == x && c.ty == y) shake = (fmodf(c.t * 40, 2) < 1 ? -2.0f : 2.0f) * std::min(1.0f, c.t * 4);
            float ox = px + shake;
            const Color ink{8, 8, 12, 255};
            if (p.level == PL_ATLANTIS) { // a loose, cracked stone of the span: grit sifting out of it as it shifts
                DrawRectangle((int)ox, (int)py, T, 14, ink);
                DrawRectangle((int)ox + 1, (int)py + 1, T - 2, 12, Color{118, 110, 102, 255});
                DrawRectangle((int)ox + 1, (int)py + 1, T - 2, 3, Color{150, 142, 132, 255});
                DrawLineEx({ox + 10, py + 1}, {ox + 14, py + 8}, 1.2f, ink); DrawLineEx({ox + 14, py + 8}, {ox + 11, py + 13}, 1.2f, ink); DrawLineEx({ox + 20, py + 2}, {ox + 24, py + 12}, 1.0f, ink);
                if (shake != 0) for (int k = 0; k < 3; k++) DrawRectangle((int)ox + 6 + k * 9, (int)(py + 15 + fmodf(p.time * 40 + k * 5, 10)), 2, 2, Color{150, 142, 132, 200});
                break;
            }
            DrawRectangle((int)ox, (int)py, T, 13, ink);
            DrawRectangle((int)ox + 2, (int)py + 2, T - 4, 9, Color{124, 84, 48, 255});
            DrawRectangle((int)ox + 2, (int)py + 2, T - 4, 2, Color{178, 134, 82, 255});
            DrawRectangle((int)ox + 2, (int)py + 9, T - 4, 2, Color{70, 44, 26, 255});
            for (int k = 0; k < 3; k++) DrawRectangle((int)ox + 5 + k * 10, (int)py + 5, 2, 2, Color{60, 58, 62, 255});    // iron nails
            DrawLineEx({ox + 12, py + 3}, {ox + 15, py + 10}, 1, ink);                                                    // a split
            for (int k = 0; k < 3; k++) DrawRectangle((int)ox + 18 + k * 3, (int)py + 4 + k, 2, 1, Color{50, 32, 18, 255}); // hatching
            DrawRectangle((int)ox + 3, (int)py + 13, 3, 6, Color{110, 60, 34, 255});                                      // rusted brackets
            DrawRectangle((int)ox + T - 6, (int)py + 13, 3, 6, Color{110, 60, 34, 255});
        } break;
        case 'v': { // a steam vent set into the floor: a brass grate, and a plume whenever it erupts
            DrawSolid(p, x, y);
            if (p.level == PL_WEEDS || p.level == PL_ATLANTIS) { // a warm seep in the rock (the Weeds), an old fountain's mouth (Atlantis): a column of rising water full of bubbles
                bool at = p.level == PL_ATLANTIS;
                if (at) { DrawRectangle((int)px - 2, (int)py - 6, T + 4, 8, Color{16, 18, 26, 255}); DrawRectangle((int)px, (int)py - 5, T, 6, Color{140, 146, 150, 255}); DrawCircle((int)px + 16, (int)py - 2, 5, Color{30, 60, 80, 255}); }
                else { DrawEllipse((int)px + 16, (int)py + 2, 13, 5, Color{30, 24, 22, 255}); DrawEllipse((int)px + 16, (int)py + 1, 8, 3, Color{220, 120, 60, 200}); }
                bool on = VentOn(p, x);
                for (int k = 0; k < (on ? 14 : 3); k++) {
                    float ph = fmodf(p.time * (on ? 1.1f : 0.5f) + k * 0.09f + x * 0.13f, 1.0f);
                    float bx = px + 16 + sinf(ph * 9 + k) * (4 + ph * 6), by = py - ph * (on ? 5.4f : 1.2f) * T;
                    DrawCircleLines((int)bx, (int)by, 2 + (k % 3), Fade(Color{210, 240, 250, 255}, (on ? 0.7f : 0.4f) * (1 - ph)));
                }
                if (on) DrawRectangle((int)px + 6, (int)(py - 5.4f * T), T - 12, (int)(5.4f * T), Fade(Color{200, 236, 250, 255}, 0.06f)); // the shimmer of the current
                break;
            }
            DrawRectangle((int)px + 3, (int)py, T - 6, 5, Color{20, 16, 14, 255});
            for (int k = 0; k < 4; k++) DrawRectangle((int)px + 5 + k * 6, (int)py, 2, 5, Color{176, 132, 56, 255});
            DrawRectangle((int)px + 2, (int)py - 1, T - 4, 2, Color{184, 140, 60, 255});
            if (VentOn(p, x))
                for (int k = 0; k < 7; k++) {
                    float ph = fmodf(p.time * 1.6f + k * 0.16f + x * 0.17f, 1.0f);
                    DrawRectangle((int)(px + 7 + sinf(ph * 6 + k) * 5), (int)(py - ph * 5.4f * T), 8 + (int)(ph * 12), 8, Fade(Color{240, 245, 250, 255}, 0.6f * (1 - ph)));
                }
            else DrawRectangle((int)px + 12, (int)(py - 6 - fmodf(p.time * 12, 8.0f)), 6, 4, Fade(Color{240, 245, 250, 255}, 0.25f));
        } break;
        case 'h': { // a low beam across the way (movement pass 2): a standing diver hits it, a sliding one passes under
            Color beam = p.level == PL_PIPES ? Color{150, 110, 60, 255} : p.level == PL_PIRATE ? Color{110, 76, 44, 255} : p.level == PL_ATLANTIS ? Color{170, 170, 160, 255} : Color{96, 90, 84, 255};
            DrawRectangle((int)px - 1, (int)py - 1, T + 2, 17, Color{8, 8, 10, 255});
            DrawRectangle((int)px, (int)py, T, 15, beam);
            DrawRectangle((int)px, (int)py, T, 2, Tone(beam, 0.3f));
            for (int k = 0; k < 4; k++) DrawTri({px + 3 + k * 8.0f, py + 15}, {px + 7 + k * 8.0f, py + 15}, {px + 5 + k * 8.0f, py + 21}, Color{200, 190, 170, 255}); // teeth on its underside
        } break;
        case '~': { // a shallow pool (the Island): clear water over the floor, ripples, a glint
            DrawRectangle((int)px, (int)py + 10, T, T - 10, Color{70, 150, 170, 150});
            DrawRectangle((int)px, (int)py + 10, T, 2, Color{190, 235, 240, 200});
            for (int k = 0; k < 2; k++) { float rx = px + fmodf(t * 8 + k * 16 + x * 7, (float)T); DrawRectangle((int)rx, (int)py + 14 + k * 6, 6, 1, Fade(WHITE, 0.4f)); }
        } break;
        case 's': { // a slimy wall (ParkourReference1.2): a glistening coat - you slide straight down it and can't jump off it
            DrawSolid(p, x, y);
            Color slime = p.level == PL_CAVE ? Color{60, 110, 90, 200} : p.level == PL_ATLANTIS ? Color{70, 130, 120, 200} : Color{78, 135, 82, 200};
            DrawRectangle((int)px, (int)py, T, T, Fade(slime, 0.55f));
            for (int k = 0; k < 3; k++) { float dx = px + 5 + k * 10 + Hs(x * 2.1f + k) * 4, drip = fmodf(t * (8 + k * 3) + Hs(y * 3.3f + k) * 30, 30); DrawRectangle((int)dx, (int)(py + drip), 2, 5, Fade(Color{150, 220, 160, 255}, 0.7f)); } // runs of slime
            DrawRectangle((int)px + 3, (int)py + 3, 3, T - 6, Fade(WHITE, 0.18f + 0.1f * sinf(t * 2 + x))); // the wet sheen
        } break;
        case 'b': {
            DrawSolid(p, x, y);
            if (p.level == PL_ISLAND) { // a wall of gnarled bark, studded with thorns: it springs a wall jump 1.5x the same as barnacles do
                for (int k = 0; k < 6; k++) {
                    float bx = px + 6 + (k % 2) * 16 + Hs(x * 3.3f + k) * 5, by = py + 4 + (k / 2) * 10 + Hs(y * 5.1f + k) * 3;
                    DrawTri({bx - 5, by + 7}, {bx + 5, by + 7}, {bx, by - 6}, Color{8, 8, 6, 255});
                    DrawTri({bx - 3.5f, by + 6}, {bx + 3.5f, by + 6}, {bx, by - 4}, Color{86, 70, 44, 255});
                }
                break;
            }
            // a wall crusted with barnacles: it springs a wall jump 1.5x
            for (int k = 0; k < 6; k++) {
                float bx = px + 6 + (k % 2) * 16 + Hs(x * 3.3f + k) * 5, by = py + 4 + (k / 2) * 10 + Hs(y * 5.1f + k) * 3;
                DrawTri({bx - 6, by + 8}, {bx + 6, by + 8}, {bx, by - 3}, Color{8, 8, 12, 255});
                DrawTri({bx - 4, by + 7}, {bx + 4, by + 7}, {bx, by - 1}, Color{214, 196, 170, 255});
                DrawRectangle((int)bx - 1, (int)by, 2, 2, Color{60, 30, 40, 255});
            }
        } break;        case 'x': {
            // Every hazard is built INTO the level, never set on top of it: a recessed housing is cut into
            // the floor, framed in the zone's own materials, and the hazard rises out of it.
            if (p.level == PL_PIPES) { // a recessed brass grate over a steam exhaust port
                DrawRectangle((int)px, (int)py + 14, T, T - 14, Color{22, 18, 16, 255});                 // the cut-out
                DrawRectangle((int)px, (int)py + 14, T, 3, Color{184, 140, 60, 255});                    // brass lip
                DrawRectangle((int)px, (int)py + 14, 3, T - 14, Color{138, 100, 40, 255});               // side flanges
                DrawRectangle((int)px + T - 3, (int)py + 14, 3, T - 14, Color{138, 100, 40, 255});
                for (int k = 0; k < 4; k++) DrawRectangle((int)px + 5 + k * 6, (int)py + 19, 3, 11, Color{70, 56, 40, 255}); // the grate bars
                DrawRectangle((int)px + 2, (int)py + 16, 2, 2, Color{230, 200, 120, 255});               // rivets
                DrawRectangle((int)px + T - 4, (int)py + 16, 2, 2, Color{230, 200, 120, 255});
                for (int k = 0; k < 3; k++) {
                    float ph = fmodf(t * 1.6f + k * 0.33f + x * 0.17f, 1.0f);
                    DrawRectangle((int)(px + T / 2 + sinf(ph * 6 + k) * 6) - 3, (int)(py + 14 - ph * 30), 6 + (int)(ph * 6), 4, Fade(Color{240, 245, 250, 255}, 0.75f * (1 - ph)));
                }
            } else if (p.level == PL_ATLANTIS) { // a broken floor: shards of dark glassy stone standing up out of the cracked paving
                DrawRectangle((int)px, (int)py + 16, T, T - 16, Color{16, 18, 26, 255});
                DrawRectangle((int)px, (int)py + 16, T, 2, Color{96, 102, 112, 255});
                for (int k = 0; k < 4; k++) { float bx = px + 2 + k * 8.0f, hh = 10 + Hs(x * 3.1f + k) * 8; DrawTri({bx, py + 18}, {bx + 7, py + 18}, {bx + 2 + (k % 2) * 3, py + 18 - hh}, Color{36, 44, 64, 255}); DrawLineEx({bx + 2, py + 17}, {bx + 2 + (k % 2) * 3, py + 19 - hh}, 1, Color{120, 170, 200, 200}); }
            } else if (p.level == PL_HULL || p.level == PL_WEEDS) { // an urchin nested in a barnacle-crusted seam
                DrawRectangle((int)px, (int)py + 16, T, T - 16, Color{30, 22, 26, 255});                 // dark rust seam
                DrawRectangle((int)px, (int)py + 16, T, 2, Color{120, 64, 34, 255});                     // rust line
                for (int k = 0; k < 4; k++) DrawRectangle((int)px + 4 + k * 8, (int)py + 14 - (k % 2) * 2, 6, 4, Color{204, 198, 176, 255}); // barnacles
                Vector2 c0{px + 16, py + T - 8};
                for (int k = 0; k < 7; k++) {
                    float a = PI + k * PI / 6;
                    DrawLineEx(c0, {c0.x + cosf(a) * 14, c0.y + sinf(a) * 14}, 2, Color{70, 40, 90, 255});
                }
                DrawRectangle((int)c0.x - 8, (int)c0.y - 8, 16, 12, Color{90, 50, 110, 255});
                DrawRectangle((int)c0.x - 6, (int)c0.y - 6, 4, 4, Color{150, 100, 170, 255});
            } else if (p.level == PL_ISLAND) { // a bed of thorns grown up through split jungle earth
                DrawRectangle((int)px, (int)py + 18, T, T - 18, Color{46, 34, 22, 255});                  // the split soil
                DrawRectangle((int)px, (int)py + 18, T, 2, Color{78, 118, 52, 255});                      // a moss lip
                for (int k = 0; k < 4; k++) DrawTri({px + k * 8.0f + 1, py + 20}, {px + k * 8.0f + 9, py + 20}, {px + k * 8.0f + 5, py + 4}, Color{88, 128, 58, 255});
                for (int k = 0; k < 4; k++) DrawLineEx({px + k * 8.0f + 5, py + 20}, {px + k * 8.0f + 5, py + 4}, 1, Color{56, 92, 36, 255});
            } else if (p.level == PL_CAVE) { // rock-boring urchin spines packed into a crevice of broken shell
                DrawRectangle((int)px, (int)py + 16, T, T - 16, Color{20, 18, 20, 255});                   // the crevice
                DrawRectangle((int)px, (int)py + 16, T, 2, Color{50, 46, 50, 255});
                for (int k = 0; k < 5; k++) DrawLineEx({px + k * 6.0f + 3, py + 30}, {px + k * 6.0f + 3, py + 30 - (8 + (k % 2) * 6)}, 1.6f, Color{60, 30, 40, 255}); // dark spines
                for (int k = 0; k < 3; k++) DrawTri({px + k * 10.0f + 2, py + T - 2}, {px + k * 10.0f + 10, py + T - 2}, {px + k * 10.0f + 6, py + 20}, Color{200, 196, 190, 255}); // broken shell fragments
            } else { // iron spikes mounted in a plank frame with rusty brackets
                DrawRectangle((int)px, (int)py + 20, T, T - 20, Color{84, 52, 30, 255});                 // the mounting plank
                DrawRectangle((int)px, (int)py + 20, T, 2, Color{150, 108, 64, 255});
                for (int k = 0; k < 4; k++) DrawTri({px + k * 8.0f, py + 20}, {px + k * 8.0f + 8, py + 20}, {px + k * 8.0f + 4, py + 6}, Color{150, 150, 160, 255});
                DrawRectangle((int)px, (int)py + 18, 4, T - 18, Color{92, 70, 60, 255});                  // iron brackets
                DrawRectangle((int)px + T - 4, (int)py + 18, 4, T - 18, Color{92, 70, 60, 255});
                DrawRectangle((int)px + 1, (int)py + 22, 2, 2, Color{170, 96, 60, 255});
                DrawRectangle((int)px + T - 3, (int)py + 22, 2, 2, Color{170, 96, 60, 255});
            }
        } break;        case 'g': {
            Vector2 c0{px + 16, py + 16};
            if (p.level == PL_PIPES) DrawGear(c0, 13, 8, t * 3 + x, Color{190, 150, 80, 255});
            else if (p.level == PL_HULL) { // naval mine
                for (int k = 0; k < 6; k++) {
                    float a = k * PI / 3;
                    DrawLineEx(c0, {c0.x + cosf(a) * 16, c0.y + sinf(a) * 16}, 3, Color{40, 44, 50, 255});
                }
                DrawCircleV(c0, 12, Color{50, 56, 64, 255});
                DrawCircleV({c0.x - 4, c0.y - 4}, 4, Color{100, 110, 120, 255});
                if (fmodf(t + x * 0.3f, 1.0f) < 0.5f) DrawCircleV(c0, 3, Color{255, 60, 50, 255});
            } else if (p.level == PL_ISLAND) { // a spider's egg sack, cut loose by a climbing Coconut Crab
                DrawLineEx({c0.x, py}, c0, 2, Color{70, 60, 50, 255});
                DrawCircleV(c0, 10, Color{224, 214, 190, 255});
                for (int k = 0; k < 5; k++) DrawCircleV({c0.x + cosf(k * 1.26f) * 5, c0.y + sinf(k * 1.26f) * 5}, 2.2f, Color{40, 34, 28, 255});
                DrawCircleLines((int)c0.x, (int)c0.y, 10, Color{40, 34, 28, 180});
            } else { // spiked ball
                for (int k = 0; k < 8; k++) {
                    float a = t * 2 + k * PI / 4;
                    DrawTri({c0.x + cosf(a - 0.25f) * 10, c0.y + sinf(a - 0.25f) * 10}, {c0.x + cosf(a) * 17, c0.y + sinf(a) * 17},
                            {c0.x + cosf(a + 0.25f) * 10, c0.y + sinf(a + 0.25f) * 10}, Color{150, 150, 160, 255});
                }
                DrawCircleV(c0, 11, Color{50, 50, 56, 255});
                DrawCircleV({c0.x - 3, c0.y - 3}, 3, Color{110, 110, 120, 255});
            }
        } break;
        case 't': {
            DrawSolid(p, x, y);
            float cyc = JetCycle(p, x);
            if (p.level == PL_WEEDS) { // a ray buried in the sand: only its eyes and the outline of its disc show - until it shocks
                DrawEllipse((int)px + 16, (int)py + 3, 15, 4, Color{176, 160, 116, 255});
                DrawEllipse((int)px + 16, (int)py + 2, 12, 2.5f, Color{150, 136, 96, 255});
                DrawCircle((int)px + 11, (int)py + 1, 1.5f, Color{20, 20, 20, 255}); DrawCircle((int)px + 21, (int)py + 1, 1.5f, Color{20, 20, 20, 255});
                if (cyc < 1.1f) for (int k = 0; k < 5; k++) { // the shock: forked arcs crackling up out of the sand
                    float a0 = -PI / 2 + (k - 2) * 0.35f, len = 20 + Hs(t * 17 + k) * 30;
                    Vector2 s0{px + 16, py}, m1{s0.x + cosf(a0) * len * 0.5f + Hs(t * 31 + k) * 8 - 4, s0.y + sinf(a0) * len * 0.5f}, e1{s0.x + cosf(a0) * len, s0.y + sinf(a0) * len};
                    DrawLineEx(s0, m1, 2, Color{200, 240, 255, 230}); DrawLineEx(m1, e1, 1.5f, Color{150, 220, 255, 200});
                } else if (cyc > 2.1f) DrawCircle((int)px + 16, (int)py, 3, Fade(Color{170, 230, 255, 255}, 0.5f + 0.5f * sinf(t * 30))); // a flicker: it's charging
                break;
            }
            if (p.level == PL_ATLANTIS) { // a rune plate set into the paving: its glyph flares and anything standing on it burns
                DrawRectangle((int)px + 3, (int)py, T - 6, 5, Color{30, 36, 48, 255});
                float on = cyc < 1.1f ? 1.0f : cyc > 2.1f ? 0.4f + 0.3f * sinf(t * 25) : 0.15f;
                DrawRectangle((int)px + 13, (int)py, 6, 5, Fade(Color{140, 240, 250, 255}, on)); DrawRectangle((int)px + 7, (int)py + 2, 18, 1, Fade(Color{140, 240, 250, 255}, on));
                if (cyc < 1.1f) for (int k = 0; k < 7; k++) { float ph = fmodf(t * 3 + k * 0.14f, 1.0f); DrawRectangle((int)(px + 6 + k * 3), (int)(py - ph * 2.5f * T), 2, 6, Fade(Color{140, 240, 250, 255}, 0.8f * (1 - ph))); }
                break;
            }
            if (p.level == PL_ISLAND) { // a poison-frog mud wallow: no grate, no tall jet - just a rim of churned mud that bubbles low when it's "on"
                DrawEllipse((int)px + 16, (int)py + 2, 15, 5, Color{58, 44, 26, 255});
                DrawEllipse((int)px + 16, (int)py + 1, 12, 3.5f, Color{74, 96, 40, 200});
                if (cyc < 1.1f) for (int k = 0; k < 6; k++) {
                    float ph = fmodf(t * 3 + k * 0.2f, 1.0f);
                    float bx = px + 8 + (k * 3) % 16;
                    DrawCircle((int)bx, (int)(py - ph * 10), 2.5f - ph * 1.5f, Fade(Color{110, 140, 60, 255}, 0.8f - ph * 0.6f));
                } else if (cyc > 2.1f) DrawCircle((int)px + 16, (int)py - 1, 2, Fade(Color{110, 140, 60, 255}, 0.6f)); // a single warning bubble
                break;
            }
            DrawRectangle((int)px + 6, (int)py, T - 12, 5, Color{30, 30, 34, 255});
            Color jet = p.level == PL_PIPES ? Color{240, 245, 250, 255} : p.level == PL_HULL ? Color{170, 230, 250, 255} : Color{255, 170, 60, 255};
            if (cyc < 1.1f) {
                for (int k = 0; k < 9; k++) {
                    float ph = fmodf(t * 5 + k * 0.11f, 1.0f);
                    float wob = sinf(t * 20 + k) * 3;
                    Color cc = p.level == PL_PIRATE && k % 2 ? Color{255, 230, 120, 255} : jet;
                    DrawCircle((int)(px + 16 + wob), (int)(py - ph * 3 * T), 7 + ph * 3, Fade(cc, 0.85f - ph * 0.4f));
                }
            } else if (cyc > 2.1f) { // sputtering: about to fire
                DrawCircle((int)px + 16 + GetRandomValue(-3, 3), (int)py - 4, 4, Fade(jet, 0.8f));
            }
        } break;
        case 'o': {
            // No marker: just a faint pool of ambient light, the kind of cue the diver has to read (light ahead, a lit passage), never an arrow.
            float fl = 0.8f + 0.2f * sinf(t * 2.3f + x * 1.7f);
            Color lc = p.level == PL_HULL ? Color{60, 170, 170, 255} : p.level == PL_CAVE ? Color{110, 220, 190, 255} : p.level == PL_WEEDS ? Color{230, 240, 170, 255} : p.level == PL_ATLANTIS ? Color{140, 230, 250, 255} : gGhost ? Color{90, 220, 180, 255} : Color{255, 170, 80, 255};
            DrawCircle((int)px + T / 2, (int)py + T / 2, 30, Fade(lc, 0.05f * fl));
            DrawCircle((int)px + T / 2, (int)py + T / 2, 16, Fade(lc, 0.06f * fl));
        } break;
        case 'E':
            if (!p.exitOpen) break;
            if (p.level == PL_PIPES) {
                Vector2 c0{px + T / 2.0f, py - 10};
                DrawRectangle((int)px + 10, (int)py - 10, 12, T + 10, Pal::BrassDk);
                for (int k = 0; k < 4; k++) {
                    float ang = t * 1.5f + k * PI / 2;
                    DrawLineEx(c0, {c0.x + cosf(ang) * 26, c0.y + sinf(ang) * 26}, 4, Pal::Coral);
                }
                DrawRing(c0, 22, 28, 0, 360, 36, Pal::Bad);
                DrawCircleV(c0, 7, Pal::Brass);
            } else if (p.level == PL_HULL) { // an airlock hatch
                DrawCircle((int)px + 16, (int)py, 30, Color{90, 110, 116, 255});
                DrawCircle((int)px + 16, (int)py, 24, Color{50, 64, 70, 255});
                DrawRing({px + 16, py}, 10, 14, 0, 360, 24, Pal::Bad);
            } else if (p.level == PL_ISLAND) { // a moss-grown stone idol, its mouth the way out
                DrawRectangle((int)px - 6, (int)py - 24, 44, 50, Color{92, 88, 76, 255});
                DrawRectangle((int)px - 6, (int)py - 24, 44, 6, Color{112, 108, 92, 255});
                DrawCircle((int)px + 10, (int)py - 8, 5, Color{40, 36, 30, 255}); DrawCircle((int)px + 22, (int)py - 8, 5, Color{40, 36, 30, 255});
                float glow = 0.5f + 0.5f * sinf(t * 4);
                DrawEllipse((int)px + 16, (int)py + 6, 9, 7 + glow * 2, Fade(Color{255, 220, 100, 255}, 0.5f));
                for (int k = 0; k < 3; k++) DrawEllipse((int)px - 2 + k * 6, (int)py - 24, 5, 3, Color{78, 118, 52, 255}); // moss along the crown
            } else if (p.level == PL_WEEDS) { // a giant clam gaping open at the forest's edge, a pearl of light inside: the way on
                float open = 0.7f + 0.15f * sinf(t * 1.5f);
                DrawEllipse((int)px + 16, (int)py + 20, 30, 12, Color{8, 12, 12, 255});
                DrawEllipse((int)px + 16, (int)py + 18, 28, 10, Color{150, 150, 176, 255});                    // the lower shell
                for (int k = -3; k <= 3; k++) DrawLineEx({px + 16, py + 26}, {px + 16 + k * 8.0f, py + 12}, 1.5f, Color{110, 110, 140, 255});
                DrawEllipse((int)px + 16, (int)(py + 6 - open * 18), 28, 9, Color{170, 170, 196, 255});         // the upper shell, raised
                DrawEllipse((int)px + 16, (int)py + 8, 22, 8 * open, Color{220, 150, 170, 255});                // the soft mantle
                float glow = 0.6f + 0.4f * sinf(t * 3);
                DrawCircle((int)px + 16, (int)py + 6, 7, Color{250, 248, 236, 255}); DrawCircle((int)px + 16, (int)py + 6, 16, Fade(Color{255, 250, 220, 255}, 0.25f * glow));
            } else if (p.level == PL_ATLANTIS) { // the city gate: two great doors under a lintel carved with an eye, standing open on light
                DrawRectangle((int)px - 20, (int)py - 50, 72, 82, Color{16, 18, 26, 255});
                DrawRectangle((int)px - 18, (int)py - 48, 68, 10, Color{150, 156, 160, 255});                  // the lintel
                DrawEllipse((int)px + 16, (int)py - 43, 10, 4, Color{30, 36, 48, 255}); DrawCircle((int)px + 16, (int)py - 43, 2.5f, Color{140, 240, 250, 255}); // the carved eye
                DrawRectangle((int)px - 18, (int)py - 38, 8, 70, Color{120, 126, 134, 255}); DrawRectangle((int)px + 42, (int)py - 38, 8, 70, Color{120, 126, 134, 255}); // jambs
                float glow = 0.6f + 0.4f * sinf(t * 2);
                DrawRectangle((int)px - 10, (int)py - 38, 52, 70, Fade(Color{150, 230, 250, 255}, 0.35f * glow));
                DrawRectangle((int)px - 10, (int)py - 38, 10, 70, Color{76, 82, 96, 255}); DrawRectangle((int)px + 32, (int)py - 38, 10, 70, Color{76, 82, 96, 255}); // the doors, swung in
            } else if (p.level == PL_CAVE) { // a fissure in the rock, packed with glowing crystal - the way up and out
                DrawRectangle((int)px - 4, (int)py - 30, 40, 56, Color{20, 18, 20, 255});
                DrawRectangle((int)px - 2, (int)py - 28, 36, 52, Color{10, 9, 10, 255});
                float glow = 0.6f + 0.4f * sinf(t * 3);
                for (int k = 0; k < 5; k++) { float cy2 = py - 24 + k * 10.0f; DrawTri({px + 4.0f, cy2}, {px + 4.0f + 6, cy2 - 3}, {px + 4.0f + 3, cy2 + 8}, Fade(Color{120, 220, 210, 255}, glow)); }
                DrawEllipse((int)px + 14, (int)py + 2, 12, 16, Fade(Color{140, 230, 220, 255}, 0.35f * glow));
            } else { // the treasure chest
                DrawRectangle((int)px - 4, (int)py + 6, 40, 26, Color{120, 72, 36, 255});
                DrawRectangle((int)px - 4, (int)py - 6, 40, 14, Color{140, 86, 44, 255});
                DrawRectangle((int)px - 4, (int)py + 4, 40, 4, Pal::Brass);
                DrawRectangle((int)px + 12, (int)py + 2, 8, 10, Pal::Brass);
                float glow = 0.5f + 0.5f * sinf(t * 4);
                DrawCircle((int)px + 16, (int)py - 6, 10 + glow * 4, Fade(Color{255, 220, 100, 255}, 0.25f));
            }
            break;
        default: break;
    }
}

// ---------------------------------------------------------------- drawing: characters
// The diver, on the same 2-world-pixel art grid as every tile: an oversized copper dome helmet with a cyan
// glowing visor, twin air tanks on the back joined to the helmet by a hose, a saturated ocean-canvas suit
// and heavy lead boots, all under a one-art-pixel ink outline (the silhouette stamped in four directions
// first). Everything here is in art pixels (U = 2 world px), 13 tall, so it lines up with the tiles exactly.
namespace DiverPalette {
constexpr Color OUTLINE{14, 20, 27, 255}, HELMET_DARK{138, 82, 20, 255}, HELMET_BASE{217, 130, 43, 255}, HELMET_LIGHT{252, 224, 104, 255};
constexpr Color VISOR_GLOW{0, 255, 255, 255}, VISOR_INNER{224, 255, 255, 255}, SUIT_MAIN{31, 111, 120, 255}, SUIT_SHADOW{13, 60, 66, 255};
constexpr Color TANKS_METAL{136, 153, 166, 255}, TANKS_DARK{74, 88, 100, 255}, BOOTS_LEAD{44, 62, 80, 255}, BOOTS_HI{90, 112, 134, 255};
}  // namespace DiverPalette

void DrawDiverShape(const PlatformState& p, bool outline) {
    using namespace DiverPalette;
    auto R = [&](int x, int y, int w, int h, Color c) { // a rectangle of art pixels; the outline pass paints it all ink
        DrawRectangle((int)(x * ART), (int)(y * ART), (int)(w * ART), (int)(h * ART), outline ? OUTLINE : c);
    };
    // ---- the extra moves' poses, drawn as their own shapes (pixel art: whole art pixels, never rotated)
    auto helmet = [&](int hx, int hy) { // the dome with its visor, top-left at (hx, hy), 6 x 5
        R(hx + 1, hy, 4, 1, HELMET_BASE); R(hx, hy + 1, 6, 4, HELMET_BASE); R(hx, hy + 1, 2, 1, HELMET_LIGHT);
        R(hx, hy + 3, 6, 1, HELMET_DARK); R(hx + 3, hy + 1, 3, 2, VISOR_GLOW); if (!outline) R(hx + 4, hy + 1, 1, 1, VISOR_INNER);
    };
    if (p.pose == 1) { // the slide: laid back, boots first, skimming the floor
        R(-8, -4, 2, 3, TANKS_METAL);                                   // tanks, dragging behind
        R(-6, -5, 6, 3, SUIT_MAIN); R(-6, -3, 6, 1, SUIT_SHADOW);       // the torso, lying back
        R(0, -3, 3, 2, SUIT_MAIN); R(3, -3, 3, 2, BOOTS_LEAD); R(3, -3, 3, 1, BOOTS_HI); // legs and boots out front
        R(-4, -6, 1, 1, SUIT_MAIN); R(-3, -2, 3, 1, HELMET_DARK);       // a trailing hand
        helmet(-11, -8);
        return;
    }
    if (p.pose == 2 || p.pose == 8) { // the roll and the backflip: a tucked ball turning over, four frames
        float t = p.pose == 2 ? (ROLL_T - p.moveT) / ROLL_T : (0.45f - p.moveT) / 0.45f;
        int fr = ((int)(t * 8)) % 4;
        if (p.pose == 8) fr = 3 - fr; // a flip turns backward
        int by = p.pose == 2 ? -8 : -12;
        R(-4, by, 8, 8, SUIT_MAIN); R(-4, by + 6, 8, 2, SUIT_SHADOW);
        const int hx[4] = {-3, 1, -3, -7}, hy[4] = {-5, -1, 3, -1};   // the helmet goes round: top, front, bottom, back
        helmet(hx[fr], by + 4 + hy[fr] - 2);
        const int bx[4] = {-3, -6, -1, 3}, byo[4] = {5, 0, -3, 1};
        R(bx[fr], by + 3 + byo[fr], 3, 2, BOOTS_LEAD);
        return;
    }
    if (p.pose == 9) { // hanging from a ledge: arms straight up to the lip, boots against the wall
        R(1, -18, 1, 5, SUIT_MAIN); R(-2, -18, 1, 5, SUIT_SHADOW); R(1, -19, 1, 1, HELMET_BASE); R(-2, -19, 1, 1, HELMET_BASE);
        helmet(-3, -14);
        R(-3, -9, 6, 5, SUIT_MAIN); R(-3, -9, 1, 5, SUIT_SHADOW); R(-3, -5, 6, 1, HELMET_DARK);
        R(-6, -9, 2, 5, TANKS_METAL);
        R(-2, -4, 2, 2, SUIT_SHADOW); R(1, -4, 2, 2, SUIT_MAIN); R(-2, -2, 3, 2, BOOTS_LEAD); R(1, -2, 3, 2, BOOTS_LEAD);
        return;
    }
    bool spread = p.pose == 4 || p.pose == 7 || p.pose == 6; // braking, gliding, balancing: arms and legs flung wide
    // squash and stretch, in whole art pixels only: the body sinks a pixel on landing and rises one when launched
    int sqy = p.scale.y < 0.88f ? 1 : p.scale.y > 1.16f ? -1 : 0;
    auto B = [&](int x, int y, int w, int h, Color c) { R(x, y + sqy, w, h, c); };
    bool running = p.onGround && fabsf(p.vel.x) > 30, sliding = p.wallSide != 0;
    int step = running ? (int)roundf(sinf(p.runAnim) * 1.4f) : 0;             // legs swing by whole art pixels
    int lean = running ? (fabsf(p.vel.x) > RUN * 0.6f ? 1 : 0) : 0;            // the head and shoulders lean into the run
    int bob = running && (int)(p.runAnim / PI) % 2 == 0 ? 0 : 0;
    (void)bob;
    // twin air tanks on the back, and the hose up to the helmet
    B(-6, -9, 2, 6, TANKS_METAL); B(-6, -9, 2, 1, TANKS_DARK); B(-6, -5, 2, 1, TANKS_DARK);
    B(-5, -8, 2, 6, TANKS_DARK); B(-5, -8, 1, 5, TANKS_METAL);
    B(-5, -10, 1, 1, HELMET_DARK);
    // legs and lead boots
    if (!p.onGround) {
        int tr = p.vel.y > 0 ? 1 : 0;                                          // trailing when falling, tucked when rising
        R(-3, -4 + tr, 2, 2, SUIT_SHADOW); R(1, -5 + tr, 2, 2, SUIT_MAIN);
        R(-4, -2 + tr, 3, 2, BOOTS_LEAD); R(1, -3 + tr, 3, 2, BOOTS_LEAD); R(-4, -2 + tr, 3, 1, BOOTS_HI);
    } else {
        R(-3 + step, -4, 2, 2, SUIT_SHADOW); R(1 - step, -4, 2, 2, SUIT_MAIN);
        R(-4 + step, -2, 3, 2, BOOTS_LEAD); R(1 - step, -2, 3, 2, BOOTS_LEAD);
        R(-4 + step, -2, 3, 1, BOOTS_HI); R(1 - step, -2, 3, 1, BOOTS_HI);
    }
    // the far arm, then the torso with its brass collar and weight belt
    if (spread) { B(-8 + lean, -9, 5, 1, SUIT_SHADOW); B(-9 + lean, -9, 1, 1, HELMET_BASE); }
    else if (!sliding) B(-4 + lean, -8, 1, 4, SUIT_SHADOW);
    B(-3 + lean, -9, 6, 5, SUIT_MAIN);
    B(-3 + lean, -9, 1, 5, SUIT_SHADOW);
    B(-3 + lean, -5, 6, 1, HELMET_DARK); B(0 + lean, -5, 1, 1, HELMET_LIGHT);
    B(-3 + lean, -9, 6, 1, HELMET_BASE);
    // the near arm: reaching for the wall when sliding, swinging with the stride otherwise
    if (spread) { B(3 + lean, -9, 5, 1, SUIT_MAIN); B(8 + lean, -9, 1, 1, HELMET_BASE); }
    else if (sliding) { B(2 + lean, -12, 2, 5, SUIT_MAIN); B(2 + lean, -13, 2, 1, HELMET_BASE); }
    else { B(3 + lean, -8, 1, 3, SUIT_MAIN); B(3 + lean, -5, 1, 1, HELMET_BASE); }
    // the helmet: a copper dome with a gold highlight and a shadowed underside
    B(-2 + lean, -14, 4, 1, HELMET_BASE);
    B(-3 + lean, -13, 6, 4, HELMET_BASE);
    B(-3 + lean, -13, 2, 1, HELMET_LIGHT); B(-2 + lean, -14, 2, 1, HELMET_LIGHT);
    B(-3 + lean, -11, 1, 2, HELMET_DARK); B(2 + lean, -10, 1, 1, HELMET_DARK);
    B(-3 + lean, -10, 6, 1, HELMET_DARK);
    // the cyan visor, glowing, with a hot centre
    B(0 + lean, -13, 3, 2, VISOR_GLOW);
    if (!outline) { B(1 + lean, -13, 1, 1, VISOR_INNER); }
    if (sqy < 0) R(-2, -5, 4, 1, SUIT_SHADOW); // stretched: the suit fills the gap above the boots
}

void DrawDiver(const PlatformState& p) {
    int wall = TouchWall(p, 1) ? 1 : TouchWall(p, -1) ? -1 : 0;
    float feetX = p.pos.x + PW / 2 - wall * 2;
    feetX = roundf(feetX * ZOOM) / ZOOM;                              // both snapped to the 2-world-pixel grid
    float feetY = roundf((p.pos.y + PH) * ZOOM) / ZOOM;
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();                                        // the mirrored transform flips winding
    const float o = ART;                                              // the outline is one art pixel thick
    const Vector2 offs[5] = {{-o, 0}, {o, 0}, {0, -o}, {0, o}, {0, 0}};
    for (int k = 0; k < 5; k++) {
        rlPushMatrix();
        rlTranslatef(feetX + offs[k].x, feetY + offs[k].y, 0);
        rlScalef(p.facingRight ? 1.0f : -1.0f, 1, 1);                 // a mirror only: never a stretch
        DrawDiverShape(p, k < 4);
        rlPopMatrix();
    }
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    if (p.pose == 3) for (int k = 0; k < 3; k++) { // reeling from a hard landing: stars wheel round the helmet
        float a = p.time * 7 + k * 2.09f;
        DrawRectangle((int)(feetX + cosf(a) * 12) - 1, (int)(feetY - 32 + sinf(a) * 4) - 1, 3, 3, Color{255, 230, 120, 255});
    }
    BeginBlendMode(BLEND_ADDITIVE);                                    // the visor's glow
    DrawCircleV({feetX + (p.facingRight ? 3.0f : -3.0f) * ART, feetY - 12.0f * ART}, 10, Color{0, 200, 220, 40});
    EndBlendMode();
}
// In the dark ducts, some things still shine: live steam jets, vents, the exit valve, coins catching the lamp.
void DrawGlowingBits(const PlatformState& p, int c0, int c1, int r0, int r1, float t) {
    BeginBlendMode(BLEND_ADDITIVE);
    for (int y = r0; y <= r1; y++)
        for (int x = c0; x <= c1; x++) {
            char c = p.tiles[y][x];
            Vector2 m{x * (float)T + 16, y * (float)T + 16};
            if (c == 't' && JetOn(p, x)) for (int k = 1; k <= 3; k++) DrawCircleV({m.x, m.y - k * T}, 18, Color{120, 110, 90, 60});
            else if (c == 'x') DrawCircleV({m.x, m.y + 8}, 16, Color{140, 70, 30, (unsigned char)(50 + 30 * sinf(t * 3 + x))});
            else if (c == 'E') DrawCircleV({m.x, m.y - 10}, 34, Color{255, 120, 80, 90});
            
            else if (c == 'g') DrawCircleV(m, 16, Color{90, 70, 30, 60});
            if (c == 'g') { float pl = 0.5f + 0.5f * sinf(t * 5 + x * 0.9f); DrawCircleV(m, 26, Color{200, 40, 30, (unsigned char)(20 + 40 * pl)}); }   // a mine's red heart pulses
            if (c == 'v' && VentOn(p, x)) DrawCircleV({m.x, m.y - T}, 26, Color{90, 100, 110, 30});
            if (CeilingLamp(p, x, y)) { // a pool of red light spilling down the duct
                float fl = 0.6f + 0.4f * sinf(t * 7 + x * 1.7f) * sinf(t * 3.1f + y);
                for (int k = 0; k < 4; k++) DrawCircleV({m.x, m.y + T + 8}, 30.0f + k * 22, Color{200, 40, 30, (unsigned char)((26 - k * 5) * fl)});
            }
            if (DeckLantern(p, x, y)) {
                float fl = 0.85f + 0.15f * sinf(t * 9 + x);
                for (int k = 0; k < 4; k++) DrawCircleV({m.x, m.y - 30}, 18.0f + k * 16, gGhost ? Color{80, 240, 190, (unsigned char)((34 - k * 7) * fl)} : Color{255, 180, 80, (unsigned char)((30 - k * 6) * fl)});
            }
        }
    EndBlendMode();
}

// Life in the world, behind the tiles: drifting plankton and jellyfish in the trench, sparking pipes in the duct, gulls and
// swaying rigging above the ships. Placed by hashes of world cells so it is the same wherever you look.
void DrawAmbientLife(const PlatformState& p, float t, float viewW, float viewH) {
    float x0 = p.camX - viewW / 2 - 120, x1 = p.camX + viewW / 2 + 120, y0 = p.camY - viewH / 2 - 120, y1 = p.camY + viewH / 2 + 120;
    if (p.level == PL_HULL) {
        BeginBlendMode(BLEND_ADDITIVE);
        for (int k = 0; k < 5; k++) { // god-rays slanting down from the surface, drifting slowly across the view
            float bx = p.camX - viewW / 2 - 100 + fmodf(k * viewW / 4.2f + t * 5 - p.camX * 0.12f + 90000, viewW + 200);
            float top = p.camY - viewH / 2 - 20, len = viewH + 40, w0 = 26 + k * 8, w1 = w0 + 60, slant = 130;
            for (int s = 0; s < 7; s++) {
                float u0 = s / 7.0f, u1 = (s + 1) / 7.0f;
                Color rc = Fade(Color{150, 235, 230, 255}, 0.055f * (1 - u0));
                Vector2 a{bx + slant * u0, top + len * u0}, b{bx + slant * u1, top + len * u1};
                float wa = w0 + (w1 - w0) * u0, wb = w0 + (w1 - w0) * u1;
                DrawTri({a.x, a.y}, {a.x + wa, a.y}, {b.x + wb, b.y}, rc);
                DrawTri({a.x, a.y}, {b.x + wb, b.y}, {b.x, b.y}, rc);
            }
        }
        for (int r = 0; r < 10; r++) { // caustics: a shifting net of light thrown down through the waves
            float by = y0 + r * (viewH + 240) / 10.0f;
            Vector2 prev{x0, by};
            for (float cx = x0 + 16; cx <= x1; cx += 16) {
                Vector2 q{cx, by + sinf(cx * 0.05f + t * 0.9f + r * 1.7f) * 6 + sinf(cx * 0.11f - t * 1.3f + r) * 3};
                DrawLineEx(prev, q, 1.5f, Fade(Color{170, 250, 240, 255}, 0.07f));
                prev = q;
            }
        }
        EndBlendMode();
        for (int k = 0; k < 45; k++) { // silt in the slow current
            float fx = Hs(k * 5.7f) * (viewW + 240), fy = Hs(k * 2.9f) * (viewH + 240);
            float wx = x0 + fmodf(fx + t * (4 + k % 5) + 60000 - p.camX * 0.06f, viewW + 240), wy = y0 + fmodf(fy + sinf(t * 0.5f + k) * 10 + t * 2 + 60000, viewH + 240);
            DrawRectangle((int)wx, (int)wy, 2, 2, Fade(Color{120, 150, 150, 255}, 0.35f));
        }
        BeginBlendMode(BLEND_ADDITIVE);
        for (int k = 0; k < 140; k++) { // plankton, twinkling
            float fx = Hs(k * 7.1f) * (viewW + 240), fy = Hs(k * 3.3f) * (viewH + 240);
            float wx = x0 + fmodf(fx + t * (3 + k % 6) - 0.0f + 60000, viewW + 240), wy = y0 + fmodf(fy + sinf(t * 0.7f + k) * 16 + 60000, viewH + 240);
            float tw = 0.5f + 0.5f * sinf(t * 2 + k);
            DrawCircleV({wx, wy}, 1.5f + (k % 3) * 0.6f, Fade(Color{120, 240, 220, 255}, 0.22f + 0.4f * tw));
        }
        EndBlendMode();
        for (int cx = (int)floorf(x0 / 420); cx <= (int)floorf(x1 / 420); cx++)
            for (int cy = (int)floorf(y0 / 360); cy <= (int)floorf(y1 / 360); cy++) {
                float h = Hs(cx * 5.1f + cy * 9.7f);
                if (h < 0.5f) continue;
                float jx = (cx + 0.5f) * 420 + sinf(t * 0.35f + cx * 2 + cy) * 50, jy = (cy + 0.5f) * 360 - fmodf(t * 6 + h * 300, 60.0f) + sinf(t * 0.8f + h * 6) * 12;
                float pulse = 0.5f + 0.5f * sinf(t * 2.2f + h * 9), bh = 11 + pulse * 4;
                Color bell = h > 0.75f ? Color{210, 110, 190, 255} : Color{110, 190, 230, 255};
                for (int k = 0; k < 6; k++) { // trailing tentacles
                    float tx = jx - 12 + k * 4.8f; Vector2 prev{tx, jy + 6};
                    for (int sg = 1; sg <= 5; sg++) { Vector2 q{tx + sinf(t * 1.6f + k + sg * 0.8f) * sg * 1.6f, jy + 6 + sg * 7.0f}; DrawLineEx(prev, q, 1.4f, Fade(bell, 0.5f - sg * 0.07f)); prev = q; }
                }
                DrawEllipse((int)jx, (int)jy, 17, bh, Fade(bell, 0.45f)); DrawEllipse((int)jx, (int)jy - 2, 11, bh * 0.6f, Fade(WHITE, 0.25f));
                BeginBlendMode(BLEND_ADDITIVE); DrawCircleV({jx, jy}, 36, Fade(bell, 0.07f + 0.05f * pulse)); EndBlendMode();
            }
        for (int k = 0; k < 3; k++) { // a school of small fish crossing, far off
            float fx = fmodf(t * (24 + k * 5) + k * 700 + p.camX * 0.5f, viewW + 400) + x0 - 100, fy = p.camY - 60 + k * 90 + sinf(t * 0.6f + k) * 20;
            for (int q = 0; q < 9; q++) { float ox = -q * 11 + (q % 3) * 4, oy = sinf(q * 1.7f) * 12 + (q % 2) * 6; DrawTri({fx + ox, fy + oy}, {fx + ox - 8, fy + oy - 3}, {fx + ox - 8, fy + oy + 3}, Color{16, 44, 58, 255}); }
        }
    } else if (p.level == PL_PIPES) {
        int c0 = std::max(0, (int)(x0 / T)), c1 = std::min(p.w - 1, (int)(x1 / T)), r0 = std::max(0, (int)(y0 / T)), r1 = std::min(p.h - 1, (int)(y1 / T));
        BeginBlendMode(BLEND_ADDITIVE);
        for (int y = r0; y <= r1; y++)
            for (int x = c0; x <= c1; x++) {
                char c = p.tiles[y][x];
                if ((c == '=' || c == '|') && Hs(x * 6.7f + y * 2.1f) > 0.86f) { // a fractured pipe throwing sparks in bursts
                    float cyc = fmodf(t + Hs(x * 1.3f + y) * 5, 4.0f);
                    if (cyc < 0.45f)
                        for (int k = 0; k < 8; k++) { float a = -PI / 2 + (k - 3.5f) * 0.3f, d = cyc * (60 + k * 8); DrawRectangle((int)(x * T + 16 + cosf(a) * d), (int)(y * T + 8 + sinf(a) * d + cyc * cyc * 90), 2, 2, Fade(Color{255, 200, 90, 255}, 1 - cyc / 0.45f)); }
                }
                if (c == '#' && !Solid(p, x, y - 1) && Hs(x * 3.9f + y * 8.1f) > 0.9f) { // a seam venting steam
                    float ph = fmodf(t * 0.7f + Hs(x + y) * 3, 1.0f);
                    DrawEllipse((int)(x * T + 16 + sinf(ph * 5) * 4), (int)(y * T - ph * 48), 8 + ph * 12, 5 + ph * 8, Fade(Color{190, 190, 186, 255}, 0.16f * (1 - ph)));
                }
            }
        EndBlendMode();
    } else {
        if (gGhost) // a low bank of fog crawling along the deck around the diver
            for (int k = 0; k < 12; k++) {
                float fx = x0 + fmodf(k * 89.0f + t * (6 + k % 4 * 3) + 60000, viewW + 240), fy = p.pos.y + PH - 4 - (k % 3) * 5 + sinf(t * 0.6f + k) * 3;
                DrawEllipse((int)fx, (int)fy, 46 + (k % 4) * 10, 7, Color{150, 235, 214, 26});
            }
        if (p.waterY > 0) {
            for (int k = 0; k < 4; k++) { // gulls riding the storm wind, high over the fleet
                float gx = x0 + fmodf(k * 311.0f + t * (26 + k * 7) + p.camX * 0.3f + 60000, viewW + 240), gy = p.waterY - 150 - k * 34 + sinf(t * 0.9f + k * 2) * 12;
                float flap = sinf(t * 7 + k * 1.3f) * 4;
                Color gc = gGhost ? Color{120, 230, 200, 220} : Color{20, 18, 26, 230};
                DrawLineEx({gx - 8, gy + flap}, {gx, gy}, 2, gc); DrawLineEx({gx, gy}, {gx + 8, gy + flap}, 2, gc);
            }
            for (int k = 0; k < 16; k++) { // rain and spray dimpling the sea, and a low mist rolling over it
                float sx = x0 + fmodf(Hs(k * 4.7f) * (viewW + 240) + t * 14 * (1 + k % 3), viewW + 240);
                if (fmodf(t * 3 + k * 0.37f, 1.0f) < 0.3f) DrawRectangle((int)sx, (int)(p.waterY + 6 + (k % 5) * 3), 5, 1, Fade(WHITE, 0.45f));
            }
            for (int k = 0; k < 6; k++) {
                float mx = x0 + fmodf(k * 173.0f + t * (5 + k) + 60000, viewW + 320) - 80, my = p.waterY - 5 - (k % 3) * 4 + sinf(t * 0.5f + k) * 2;
                DrawEllipse((int)mx, (int)my, 70 + (k % 3) * 14, 8, Color{170, 190, 220, 24});
            }
        }
        for (int k = 0; k < 3; k++) { // gulls, high over the fleet
            float gx = fmodf(t * (30 + k * 8) + k * 500 + p.camX * 0.3f, viewW + 500) + x0 - 200, gy = p.camY - 150 - k * 34 + sinf(t * 0.9f + k) * 14, fl = sinf(t * 7 + k * 2) * 5;
            Color gc{10, 10, 16, 255};
            DrawLineEx({gx - 9, gy + fl}, {gx, gy - 2}, 2, gc); DrawLineEx({gx, gy - 2}, {gx + 9, gy + fl}, 2, gc);
        }
    }
    (void)y1;
}
// The Ghost Ship's crew: bone, tatters and a pale green light in the sockets, in place of the living pirates.
// The sea under the fleet: a wavy body of water in front of the hulls' lower planking, foam on its crest. Falling into it is the
// hazard (see DeathY), not a strip of spikes.
void DrawSea(const PlatformState& p, float t, float viewW, float viewH) {
    if (p.waterY <= 0) return;
    float x0 = floorf(p.camX - viewW / 2) - 4, x1 = p.camX + viewW / 2 + 4, yb = p.camY + viewH / 2 + 8;
    if (yb < p.waterY - 8) return;
    Color body = gGhost ? Color{16, 70, 74, 168} : Color{16, 46, 92, 172}, crest = gGhost ? Color{120, 226, 204, 255} : Color{140, 196, 240, 255};
    for (float x = x0; x < x1; x += 2) {
        float wy = p.waterY + sinf(x * 0.045f + t * 1.5f) * 2.5f + sinf(x * 0.12f - t * 2.2f) * 1.5f;
        DrawRectangle((int)x, (int)wy, 2, (int)(yb - wy), body);
        DrawRectangle((int)x, (int)wy, 2, 2, Fade(crest, 0.9f));
        if (((int)(x * 0.5f + t * 9)) % 19 == 0) DrawRectangle((int)x, (int)wy - 2, 4, 2, Fade(WHITE, 0.85f)); // foam
    }
    for (int k = 0; k < 6; k++) { // darker swells rolling below the surface
        float sy = p.waterY + 14 + k * 12;
        for (float x = x0; x < x1; x += 4) DrawRectangle((int)x, (int)(sy + sinf(x * 0.03f + t * 0.8f + k * 2) * 3), 4, 2, Fade(BLACK, 0.16f));
    }
}

void DrawSkeletonBody(float x, float y, float f, float t, float lean) {
    const Color bone{224, 220, 196, 255}, ink{8, 8, 12, 255}, glow{120, 255, 190, 255};
    float cx = x + 11 + lean;
    DrawEllipse((int)cx, (int)y + 14, 14, 18, Fade(glow, 0.10f + 0.04f * sinf(t * 4)));
    for (int s = -1; s <= 1; s += 2) {
        DrawLineEx({cx + s * 3, y + 21}, {cx + s * 4, y + 30}, 4, ink);
        DrawLineEx({cx + s * 3, y + 21}, {cx + s * 4, y + 30}, 2, bone);
        DrawRectangle((int)(cx + s * 4 + (s > 0 ? -1 : -3)), (int)y + 29, 4, 2, ink);
    }
    DrawRectangle((int)cx - 6, (int)y + 18, 12, 4, ink); DrawRectangle((int)cx - 5, (int)y + 19, 10, 2, bone);
    DrawLineEx({cx, y + 9}, {cx, y + 19}, 3, ink); DrawLineEx({cx, y + 9}, {cx, y + 19}, 1.5f, bone);
    for (int k = 0; k < 4; k++) { DrawRectangle((int)cx - 8, (int)y + 10 + k * 3, 16, 3, ink); DrawRectangle((int)cx - 7, (int)y + 11 + k * 3, 14, 1, bone); }
    DrawTri({cx - 9, y + 9}, {cx - 2, y + 9}, {cx - 6 + sinf(t * 3) * 2, y + 22}, Color{34, 58, 66, 210});   // a tattered vest
    DrawTri({cx + 2, y + 9}, {cx + 9, y + 9}, {cx + 7 - sinf(t * 3 + 1) * 2, y + 20}, Color{34, 58, 66, 210});
    DrawCircle((int)cx, (int)y + 4, 7, ink); DrawCircle((int)cx, (int)y + 4, 5.5f, bone);                      // the skull
    DrawRectangle((int)cx - 3, (int)y + 8, 6, 4, ink); DrawRectangle((int)cx - 2, (int)y + 8, 4, 2, bone);     // the jaw
    DrawRectangle((int)cx - 4, (int)y + 2, 3, 3, ink); DrawRectangle((int)cx + 1, (int)y + 2, 3, 3, ink);
    DrawRectangle((int)(cx - 3 + f * 0.5f), (int)y + 3, 1, 1, glow); DrawRectangle((int)(cx + 2 + f * 0.5f), (int)y + 3, 1, 1, glow);
    DrawTri({cx - 11, y}, {cx + 11, y}, {cx, y - 9}, Color{22, 34, 40, 255});                                   // a torn tricorn
    DrawRectangle((int)cx - 10, (int)y - 1, 20, 3, Color{22, 34, 40, 255});
    DrawTri({cx + 5, y - 4}, {cx + 9, y}, {cx + 9 + sinf(t * 5) * 2, y - 9}, Fade(glow, 0.6f));                  // a green ghostfire wisp
}
void DrawEnemy(const PlatEnemy& e, float t, int level = -1) {
    float x = e.pos.x, y = e.pos.y, f = e.dir;
    bool island = level == PL_ISLAND, cave = level == PL_CAVE;
    switch (e.type) {
        case 'c': {
            if (island) { // a Monitor Lizard: a low sprawling body on four splayed legs, a long tapering tail, a forked tongue - nothing like a crab
                const Color INKC{8, 8, 12, 255};
                Color c{92, 118, 62, 255}, dk{54, 74, 38, 255}, belly{168, 176, 140, 255};
                float walk = e.t * 10;
                Vector2 body{x + 17.0f, y + 11.0f};
                for (int s = -1; s <= 1; s += 2) for (int k = 0; k < 2; k++) { // four short splayed legs, opposite corners stepping together
                    float ph = walk + (s * k > 0 ? 0 : PI);
                    float lift = std::max(0.0f, sinf(ph)) * 2.0f;
                    Vector2 hip{body.x + s * 7 + (k ? -4.0f : 4.0f), body.y + 3};
                    Vector2 foot{hip.x + s * 5 + cosf(ph) * 2, body.y + 9 - lift};
                    DrawLineEx(hip, foot, 3.2f, INKC); DrawLineEx(hip, foot, 1.8f, dk);
                }
                DrawEllipse((int)body.x, (int)body.y, 15, 6, INKC);       // the ink base
                DrawEllipse((int)body.x, (int)body.y, 13.5f, 5, c);       // the sprawling body
                DrawEllipse((int)body.x, (int)body.y + 2, 9, 2.4f, belly); // pale underbelly
                for (int k = -2; k <= 2; k++) DrawCircleV({body.x + k * 4.0f, body.y - 2 + fabsf((float)k) * 0.4f}, 1.0f, dk); // scale mottling
                Vector2 tail{body.x - f * 13, body.y}, tailTip{body.x - f * 26, body.y + sinf(e.t * 6) * 3};
                DrawLineEx(tail, tailTip, 4.5f, INKC); DrawLineEx(tail, tailTip, 2.6f, c); // a long tapering tail
                DrawCircle((int)(body.x + f * 12), (int)(body.y - 1), 4, c); // a blunt low head
                DrawCircleLines((int)(body.x + f * 12), (int)(body.y - 1), 4, INKC);
                DrawCircle((int)(body.x + f * 13.5f), (int)(body.y - 2.5f), 0.9f, INKC); // a small dark eye, no stalk
                float tongue = fabsf(sinf(e.t * 4));
                if (tongue > 0.6f) DrawLineEx({body.x + f * 16, body.y - 1}, {body.x + f * (16 + 5 * tongue), body.y - 1}, 1, Color{200, 60, 50, 255}); // a flicking tongue
                break;
            }
            // crab (or, in the Cave, a Mutated Crustacean sharing its shape but not its palette): a domed
            // carapace, two-segment legs, eyestalks and claws that open and snap (CrabAnim)
            const Color INKC{8, 8, 12, 255};
            Color c = cave ? Color{92, 70, 98, 255} : Color{170, 84, 56, 255};
            Color dk = cave ? Color{54, 40, 60, 255} : Color{104, 50, 38, 255};
            Color lt = cave ? Color{150, 120, 160, 255} : Color{232, 140, 100, 255};
            CrabAnim an = fmodf(e.t + x * 0.013f, 2.6f) < 0.4f ? CrabAnim::ClawSnap : CrabAnim::Scuttle;
            float cx = x + 17, cy = y + 10, w = e.t * 16;
            for (int s = -1; s <= 1; s += 2) for (int k = 0; k < 3; k++) { // three jointed legs per side
                float ph = w + k * 2.1f + (s > 0 ? PI : 0), lift = std::max(0.0f, sinf(ph)) * 2.5f;
                Vector2 hip{cx + s * (5 + k * 3.0f), cy + 3}, knee{cx + s * (11 + k * 3.5f), cy - 1 - lift}, foot{cx + s * (14 + k * 4.5f) + cosf(ph) * 2, y + 17 - lift * 0.5f};
                DrawLineEx(hip, knee, 4.2f, INKC); DrawLineEx(knee, foot, 3.6f, INKC);
                DrawLineEx(hip, knee, 2, dk); DrawLineEx(knee, foot, 1.6f, c);
            }
            for (int s = -1; s <= 1; s += 2) { // claws on jointed arms, the pincer snapping open and shut
                float open = an == CrabAnim::ClawSnap ? fabsf(sinf(e.t * 22)) : 0.35f + 0.15f * sinf(e.t * 3 + s);
                Vector2 sh{cx + s * 9, cy - 1}, el{cx + s * 15, cy - 6}, wr{cx + s * 17, cy - 11};
                DrawLineEx(sh, el, 5, INKC); DrawLineEx(el, wr, 5, INKC);
                DrawLineEx(sh, el, 2.6f, dk); DrawLineEx(el, wr, 2.6f, c);
                DrawCircleV(wr, 5.4f, INKC); DrawCircleV(wr, 3.8f, c);
                for (int j = -1; j <= 1; j += 2) { // the two pincer fingers
                    Vector2 tip{wr.x + s * 3 * (1 - open * 0.3f) + j * open * 4, wr.y - 5 - (1 - j * 0.4f) * 3};
                    DrawLineEx(wr, tip, 4.4f, INKC); DrawLineEx(wr, tip, 2.4f, lt);
                }
            }
            DrawEllipse((int)cx, (int)cy, 13, 8.5f, INKC);                     // the domed carapace
            DrawEllipse((int)cx, (int)cy, 11.5f, 7, c);
            DrawEllipse((int)cx - 2, (int)cy - 3, 7, 2.4f, lt);
            DrawEllipse((int)cx + 3, (int)cy + 3, 9, 3, dk);                  // shadow side
            for (int k = -1; k <= 1; k++) DrawCircleV({cx + k * 5.0f, cy + 1 + fabsf((float)k)}, 1.2f, dk); // mottling
            for (int s = -1; s <= 1; s += 2) { // eyestalks: no shine, just a black bead
                DrawLineEx({cx + s * 3, cy - 6}, {cx + s * 4.5f, cy - 11}, 2.6f, INKC);
                DrawCircleV({cx + s * 4.5f, cy - 12}, 2.6f, INKC);
                DrawRectangle((int)(cx + s * 4.5f + f * 0.5f), (int)cy - 13, 1, 1, Color{230, 226, 210, 255});
            }
            if (cave) { // the mutation: one claw grown oversized, and a row of sickly glowing spots down the shell
                DrawCircleV({cx - 17.0f, cy - 11.0f}, 5.0f, INKC); DrawCircleV({cx - 17.0f, cy - 11.0f}, 3.6f, c);
                for (int k = -1; k <= 1; k++) DrawCircle((int)(cx + k * 4.0f), (int)cy - 2, 0.9f, Fade(Color{140, 230, 90, 255}, 0.7f + 0.3f * sinf(e.t * 3 + k)));
            }
        } break;        case 'P': {
            float open = e.state == 1 ? e.timer / AMB_OUT : e.state == 2 || e.state >= 4 ? 1 : e.state == 3 ? 1 - e.timer / AMB_BACK : 0;
            float dx = e.home.x, dy = e.home.y - T; // the doorway fills this tile and the one above
            if (cave) { // a Stalactite Spider: no door at all - a crack overhead, a silk thread, and a many-legged drop
                // it hangs from the real ceiling above its spot, and drops the whole way down on its thread to strike
                float ceilY = e.aim.y > 0 ? e.aim.y : dy - 2, low = dy + 2 * T - 10; // (the ceiling row is found at spawn)
                Vector2 crack{dx + T / 2.0f, ceilY}, body{crack.x, ceilY + 10 + open * std::max(0.0f, low - ceilY - 10)};
                DrawTri({crack.x - 8, crack.y}, {crack.x + 8, crack.y}, {crack.x, crack.y - 10}, Color{16, 14, 16, 255}); // the crack it hangs from
                if (open > 0.02f) DrawLineEx(crack, body, 1, Fade(Color{220, 220, 220, 255}, 0.7f)); // the silk thread
                Color c{30, 26, 30, 255}, lt{70, 60, 68, 255};
                for (int s = -1; s <= 1; s += 2) for (int k = 0; k < 4; k++) { // eight jointed legs, splayed wide
                    float ph = e.timer * 8 + k * 1.4f;
                    Vector2 hip{body.x + s * 2, body.y - 1}, knee{body.x + s * (7 + k * 2.0f), body.y - 3 + k}, foot{body.x + s * (12 + k * 3.0f), body.y + 4 + k * 1.5f + sinf(ph) * (e.state == 2 ? 1.5f : 0)};
                    DrawLineEx(hip, knee, 1.8f, c); DrawLineEx(knee, foot, 1.6f, c);
                }
                DrawCircleV(body, 5, c); DrawCircleV({body.x, body.y - 4}, 3, c); // the abdomen and the smaller head
                DrawCircle((int)(body.x - 1.5f), (int)(body.y - 5), 0.8f, Color{200, 40, 40, 255}); DrawCircle((int)(body.x + 1.5f), (int)(body.y - 5), 0.8f, Color{200, 40, 40, 255}); // a cluster of red eye-glints
                for (int k = -2; k <= 2; k++) DrawCircleV({body.x + k * 2.0f, body.y + 2 + fabsf((float)k) * 0.6f}, 0.8f, lt); // mottling
                break;
            }
            { // a deckhouse: the door is set into the wall of a little timber cabin standing on the deck, never in open air
                Color wood{78, 50, 30, 255}, woodDk{46, 28, 18, 255};
                if (gGhost) { wood = Color{56, 82, 76, 255}; woodDk = Color{28, 46, 44, 255}; }
                if (island) { wood = Color{96, 78, 44, 255}; woodDk = Color{56, 46, 24, 255}; } // a thatched hut wall, bamboo and dried palm instead of ship timber
                DrawRectangle((int)dx - T + 2, (int)dy - 10, 3 * T - 4, 2 * T + 10, woodDk);
                DrawRectangle((int)dx - T + 4, (int)dy - 8, 3 * T - 8, 2 * T + 8, wood);
                for (int k = 0; k < 12; k++) DrawRectangle((int)dx - T + 4 + k * 8, (int)dy - 8, 1, 2 * T + 8, woodDk);
                DrawRectangle((int)dx - T - 2, (int)dy - 18, 3 * T + 4, 8, Color{40, 26, 16, 255});   // the roof's eave
                DrawRectangle((int)dx - T - 2, (int)dy - 18, 3 * T + 4, 2, Color{110, 76, 48, 255});
                DrawRectangle((int)dx - T + 9, (int)dy + 4, 12, 12, Color{10, 14, 26, 255});          // a little window
                DrawRectangle((int)dx - T + 9, (int)dy + 9, 12, 1, woodDk);
            }
            DrawRectangle((int)dx + 1, (int)dy + 1, T - 2, 2 * T - 1, Color{58, 36, 22, 255});   // frame
            DrawRectangle((int)dx + 4, (int)dy + 4, T - 8, 2 * T - 4, Color{16, 10, 8, 255});    // the dark inside
            if (e.state == 0 && fmodf(t * 0.7f + e.home.x * 0.01f, 3.0f) < 2.4f) { // watching through the crack
                DrawRectangle((int)dx + 21, (int)dy + 16, 2, 2, Color{255, 230, 150, 255});
                DrawRectangle((int)dx + 25, (int)dy + 16, 2, 2, Color{255, 230, 150, 255});
            }
            float leaf = (T - 8) * (1 - open * 0.8f);  // the door leaf, swinging open toward us
            float hinge = e.dir > 0 ? dx + 4 : dx + T - 4 - leaf;
            if (e.state == 0) hinge = dx + 4;
            DrawRectangle((int)hinge, (int)dy + 4, (int)leaf, 2 * T - 4, Color{110, 70, 40, 255});
            DrawRectangle((int)hinge, (int)dy + 12, (int)leaf, 3, Color{60, 58, 60, 255});         // iron bands
            DrawRectangle((int)hinge, (int)dy + 2 * T - 16, (int)leaf, 3, Color{60, 58, 60, 255});
            if (e.state == 0) {
                DrawRectangle((int)hinge + (int)leaf - 7, (int)dy + T, 3, 3, Pal::Brass); // handle
                break;
            }
            float cx = x + 11, lean = e.state == 2 || e.state == 5 ? f * 3 : 0;
            float stride = e.state == 4 ? sinf(t * 11 + e.home.x) * 2.0f : 0.0f; // walking the deck
            if (gGhost) { // the skeleton pirate: same lunge, bare bone
                DrawSkeletonBody(x, y, f, t, lean);
                float ang = e.state == 1 ? -1.2f : e.state == 2 || e.state == 5 ? -0.05f : e.state == 4 ? -0.6f : 0.7f;
                Vector2 hand{cx + f * 8 + lean, y + 14}, tip{hand.x + f * cosf(ang) * 18, hand.y + sinf(ang) * 18};
                DrawLineEx(hand, tip, 3.5f, Color{8, 8, 12, 255}); DrawLineEx(hand, tip, 2, Color{170, 210, 200, 255});
                if (e.state == 2 && e.timer < 0.15f) DrawLineEx({tip.x - f * 10, tip.y - 3}, {tip.x + f * 4, tip.y}, 1, Fade(Color{150, 255, 210, 255}, 0.8f));
                break;
            }
            Color skin = island ? Color{150, 96, 62, 255} : Color{214, 164, 124, 255};
            Color shirt = island ? Color{74, 58, 40, 255} : Color{232, 228, 216, 255}; // bare chest with a woven sash, not a shirt, on the Island
            Color stripe = island ? Color{200, 170, 60, 255} : Color{150, 36, 34, 255};
            Color trousers = island ? Color{86, 62, 34, 255} : Color{56, 46, 72, 255}; // a grass/bark skirt
            DrawRectangleRec({cx - 6 + stride, y + 21, 5, 9}, trousers);
            DrawRectangleRec({cx + 1 + (e.state == 2 || e.state == 5 ? f * 3 : 0) - stride, y + 21, 5, 9}, trousers);
            DrawRectangleRec({cx - 7, y + 28, 6, 2}, Color{30, 24, 22, 255});
            DrawRectangleRec({cx - 8 + lean, y + 9, 16, 13}, shirt);                               // striped shirt, or the Island's bare chest
            for (int k = 0; k < 3; k++) DrawRectangleRec({cx - 8 + lean, y + 11 + k * 4.0f, 16, 2}, stripe);
            DrawRectangleRec({cx - 8 + lean, y + 19, 16, 2}, Color{40, 30, 24, 255});             // belt
            DrawCircle((int)(cx + lean), (int)y + 5, 6, skin);                                      // head
            DrawRectangleRec({cx - 6 + lean, y + 7, 12, 4}, island ? Color{30, 24, 20, 255} : Color{50, 36, 30, 255}); // stubble, or dark hair
            DrawRectangleRec({cx - 7 + lean, y - 2, 14, 5}, island ? Color{200, 170, 60, 255} : Color{196, 40, 36, 255}); // bandana, or a bone-and-feather band
            DrawRectangleRec({cx - f * 9 + lean, y - 1, 3, 6}, island ? Color{220, 210, 200, 255} : Color{196, 40, 36, 255}); // its tails, or a trailing feather
            DrawRectangleRec({cx + f * 1 + lean, y + 2, 4, 2}, Color{20, 16, 16, 255});             // eye patch strap
            DrawCircle((int)(cx + f * 3 + lean), (int)y + 4, 1.5f, Color{255, 220, 150, 255});     // a mean eye
            // the cutlass (or the Island's spear): raised on the way out, thrust at the stab, trailing on the way back
            float ang = e.state == 1 ? -1.2f : e.state == 2 || e.state == 5 ? -0.05f : e.state == 4 ? -0.6f : 0.7f; // on deck: held ready
            Vector2 hand{cx + f * 8 + lean, y + 14};
            Vector2 tip{hand.x + f * cosf(ang) * 18, hand.y + sinf(ang) * 18};
            DrawLineEx(hand, tip, 2.5f, island ? Color{90, 66, 40, 255} : Color{214, 220, 226, 255});
            if (island) DrawTri({tip.x, tip.y}, {tip.x - f * 5 - 2, tip.y - 4}, {tip.x - f * 5 - 2, tip.y + 4}, Color{200, 196, 186, 255}); // a stone spearhead
            DrawLineEx(hand, {hand.x + f * cosf(ang) * 6, hand.y + sinf(ang) * 6}, 3, Color{70, 50, 30, 255});
            DrawCircleV(hand, 2.5f, skin);
            if (e.state == 2 && e.timer < 0.15f) DrawLineEx({tip.x - f * 10, tip.y - 3}, {tip.x + f * 4, tip.y}, 1, Fade(WHITE, 0.8f)); // swish
        } break;
        case 'G': { // a musketeer crouched behind a barrel
            float cx = x + 11;
            bool aiming = e.state == 1;
            Color coat = island ? Color{70, 54, 36, 255} : Color{40, 60, 110, 255};
            Color coatDk = island ? Color{44, 34, 22, 255} : Color{26, 40, 78, 255};
            Color skin = island ? Color{150, 96, 62, 255} : Color{206, 156, 118, 255};
            if (gGhost) DrawSkeletonBody(x, y, f, t, 0); else {
            DrawRectangleRec({cx - 6, y + 20, 5, 10}, Color{50, 40, 36, 255});
            DrawRectangleRec({cx + 1, y + 20, 5, 10}, Color{50, 40, 36, 255});
            DrawRectangleRec({cx - 8, y + 8, 16, 14}, coat);
            DrawRectangleRec({cx - 8, y + 8, 4, 14}, coatDk);
            DrawLineEx({cx - 8, y + 9}, {cx + 8, y + 20}, 2, island ? Color{200, 170, 60, 255} : Color{220, 210, 190, 255}); // bandolier, or a woven shoulder strap
            DrawCircle((int)cx, (int)y + 4, 6, skin);
            DrawRectangleRec({cx - 5, y + 6, 10, 4}, island ? Color{30, 24, 20, 255} : Color{120, 80, 50, 255}); // a ginger beard, or dark hair
            if (island) DrawTri({cx - 9, y - 6}, {cx + 9, y - 6}, {cx, y - 13}, Color{200, 170, 60, 255}); // a feather headdress instead of a tricorn
            else { DrawTri({cx - 11, y}, {cx + 11, y}, {cx, y - 9}, Color{30, 26, 34, 255}); DrawRectangleRec({cx - 10, y - 1, 20, 3}, Color{30, 26, 34, 255}); }
            DrawCircle((int)(cx + f * 3), (int)y + 3, 1.5f, Pal::Ink);
            }
            // the musket (or the Island's blowgun): resting on his shoulder, or levelled at you
            Vector2 shoulder{cx + f * 2, y + 11};
            float ang = aiming ? atan2f(e.aim.y - shoulder.y, e.aim.x - shoulder.x) : (f > 0 ? -1.0f : PI + 1.0f);
            if (!aiming) ang = f > 0 ? -1.1f : PI + 1.1f;
            Vector2 muzzle{shoulder.x + cosf(ang) * 22, shoulder.y + sinf(ang) * 22};
            DrawLineEx({shoulder.x - cosf(ang) * 6, shoulder.y - sinf(ang) * 6}, {shoulder.x + cosf(ang) * 6, shoulder.y + sinf(ang) * 6}, 4, island ? Color{90, 66, 40, 255} : Color{110, 70, 40, 255});
            DrawLineEx(shoulder, muzzle, 2.5f, island ? Color{100, 82, 56, 255} : Color{70, 72, 80, 255});
            if (aiming) { // a fair warning: the glint at the muzzle, and where he's pointing
                float u = e.timer / GUN_AIM;
                for (float d = 30; d < 30 + 90 * u; d += 10) {
                    Vector2 q{shoulder.x + cosf(ang) * d, shoulder.y + sinf(ang) * d};
                    DrawRectangle((int)q.x, (int)q.y, 2, 2, Fade(Color{255, 90, 60, 255}, 0.35f + 0.3f * u));
                }
                if (fmodf(t * 12, 1.0f) < 0.5f) DrawCircleV(muzzle, 2.5f, Color{255, 230, 150, 255});
            }
        } break;        case 'p': {
            float flap = sinf(t * 18) * 5;
            if (island) { // a fruit bat: leathery wings instead of a parakeet's feathers, no beak
                Color wing{58, 50, 54, 220}, body{74, 62, 56, 255};
                DrawTri({x - f * 2, y - 2}, {x - f * 16, y - 8 - flap}, {x - f * 12, y + 4}, wing);
                DrawTri({x + f * 2, y - 2}, {x + f * 16, y - 8 - flap}, {x + f * 12, y + 4}, wing);
                DrawEllipse((int)x, (int)y, 6, 5, body);
                DrawCircle((int)(x + f * 5), (int)y - 2, 3.5f, body);
                DrawTri({x + f * 6, y - 5}, {x + f * 8, y - 8}, {x + f * 4, y - 6}, body); // an ear
                DrawCircle((int)(x + f * 6), (int)y - 2, 1, Color{200, 40, 30, 255});
            } else { // parakeet
                DrawEllipse((int)x, (int)y, 9, 6, Color{88, 126, 82, 255});
                DrawTri({x - 2, y - 2}, {x + 6, y - 2}, {x + 1, y - 8 - flap}, Color{140, 60, 52, 255});
                DrawCircle((int)(x + f * 8), (int)y - 3, 4, Color{104, 138, 90, 255});
                DrawTri({x + f * 11, y - 4}, {x + f * 16, y - 2}, {x + f * 11, y}, Color{250, 200, 60, 255});
                DrawCircle((int)(x + f * 9), (int)y - 4, 1.2f, Pal::Ink);
                DrawTri({x - f * 8, y}, {x - f * 16, y + 4}, {x - f * 8, y + 3}, Color{40, 120, 200, 255});
            }
        } break;
        default: {
            if (island) { // a coiled viper, lunging up out of the canyon mud on a zigzag S-curve body, not a fish's tapering one
                Color body{84, 128, 54, 255}, band{50, 38, 24, 255};
                Vector2 prev{x, y};
                for (int k = 6; k >= 1; k--) {
                    float sy = y - k * 5.0f, sx = x + sinf(k * 1.1f + t * 2.0f) * 7.0f;
                    Vector2 q{sx, sy};
                    DrawLineEx(prev, q, 7.0f, Color{20, 16, 10, 255});
                    DrawLineEx(prev, q, 5.0f, body);
                    if (k % 2 == 0) DrawCircleV(q, 2.6f, band); // dorsal banding
                    prev = q;
                }
                DrawEllipse((int)prev.x, (int)prev.y - 2, 5.5f, 4.5f, body); // a wide triangular pit-viper head, flatter than the eel's
                DrawEllipse((int)prev.x, (int)prev.y - 2, 5.5f, 4.5f, Fade(Color{20, 16, 10, 255}, 0.25f));
                DrawCircle((int)prev.x - 2, (int)prev.y - 3, 1, Pal::Ink); DrawCircle((int)prev.x + 2, (int)prev.y - 3, 1, Pal::Ink);
                float flick = fabsf(sinf(t * 8));
                if (flick > 0.5f) { Vector2 tt{prev.x + sinf(t * 20) * 3, prev.y - 7 - flick * 3}; DrawLineEx({prev.x, prev.y - 3}, tt, 1, Color{200, 60, 50, 255}); } // a flicking forked tongue
            } else { // eel
                for (int k = 5; k >= 0; k--) {
                    float sy = y - 16 + k * 7, sx = x + sinf(t * 10 + k) * 3;
                    DrawCircle((int)sx, (int)sy, 7 - k * 0.6f, k % 2 ? Color{90, 130, 70, 255} : Color{110, 150, 80, 255});
                }
                DrawCircle((int)x - 3, (int)y - 19, 2, Color{250, 230, 60, 255});
                DrawTri({x - 5, y - 12}, {x + 5, y - 12}, {x, y - 7}, Color{240, 230, 220, 255});
            }
        } break;
    }
}

// ---------------------------------------------------------------- the living creatures (beasts.h), drawn
// Every pose comes from the simulation: heading and speed drive the fins and tails, the eel and the leech are
// drawn along their own trailing spine, and each state reads at a glance - an eel coiling to strike opens its
// jaws, a fleeing fish beats its tail twice as fast, a hunting octopus loses its camouflage and flushes red,
// an eater chews, the dead lie pale and shrink as they're picked over.
void DrawTentacle(Vector2 base, Vector2 tip, float w0, float t, float seed, Color c);
namespace {
const Color FAUNA_INK{10, 10, 16, 255};

// A thick, tapering body laid along a chain of points, outlined first so the joints never show.
void DrawSpineBody(const Vector2* pts, int n, float r0, float r1, float wave, float phase, Color c, Color belly) {
    Vector2 q[SPINE];
    for (int k = 0; k < n; k++) {
        Vector2 a = pts[std::max(0, k - 1)], b = pts[std::min(n - 1, k + 1)];
        Vector2 d{b.x - a.x, b.y - a.y};
        float l = sqrtf(d.x * d.x + d.y * d.y);
        Vector2 perp = l > 0.01f ? Vector2{-d.y / l, d.x / l} : Vector2{0, 1};
        float s = sinf(phase * 6 - k * 0.9f) * wave * k / (float)n;
        q[k] = {pts[k].x + perp.x * s, pts[k].y + perp.y * s};
    }
    for (int k = 0; k + 1 < n; k++) {
        float r = r0 + (r1 - r0) * k / (float)(n - 1);
        DrawLineEx(q[k], q[k + 1], r * 2 + 3, FAUNA_INK);
    }
    for (int k = 0; k + 1 < n; k++) {
        float r = r0 + (r1 - r0) * k / (float)(n - 1);
        DrawLineEx(q[k], q[k + 1], r * 2, c);
        DrawCircleV(q[k], r, c);
        DrawLineEx({q[k].x, q[k].y + r * 0.45f}, {q[k + 1].x, q[k + 1].y + r * 0.45f}, std::max(1.0f, r * 0.6f), belly);
    }
}

void DrawHullBeast(const PlatformState& p, const Beast& b, float t) {
    const BeastWorld& W = p.fauna;
    float x = b.pos.x, y = b.pos.y, f = b.facing, s = b.scale;
    float speed = sqrtf(b.vel.x * b.vel.x + b.vel.y * b.vel.y);
    bool dead = b.life == BeastLife::Corpse;
    bool fleeing = b.act == BeastAct::Flee || b.act == BeastAct::Hide;
    bool rabid = b.pers.abnormal == Abnormal::RabidEnraged, glow = b.pers.abnormal == Abnormal::SymbioticCompanion;
    if (glow && !dead) { DrawCircle((int)x, (int)y, 26, Fade(Color{150, 240, 220, 255}, 0.10f + 0.05f * sinf(t * 3))); DrawCircle((int)x, (int)y, 14, Fade(Color{190, 255, 235, 255}, 0.12f)); }
    Color deadTint{90, 86, 96, 255};
    auto body = [&](Color c) { return dead ? Color{(unsigned char)((c.r + deadTint.r) / 2), (unsigned char)((c.g + deadTint.g) / 2), (unsigned char)((c.b + deadTint.b) / 2), 255} : c; };
    float beat = sinf(b.phase * (fleeing ? 14.0f : 7.0f)); // tail beat, faster in flight
    switch (b.species) {
    case HS_PILOT: {
        float len = 9 * s * (dead ? b.meat * 0.5f + 0.5f : 1.0f);
        Color back = body(Color{70, 110, 150, 255}), side = body(Color{200, 214, 222, 255});
        Vector2 tail{x - f * len, y + (dead ? 0 : beat * 2.5f)};
        DrawTri({x - f * (len - 2), y}, {tail.x - f * 5, tail.y - 4}, {tail.x - f * 5, tail.y + 4}, FAUNA_INK);
        DrawTri({x - f * (len - 2), y}, {tail.x - f * 4, tail.y - 3}, {tail.x - f * 4, tail.y + 3}, back);
        DrawEllipse((int)x, (int)y, len + 1, 4.5f * s, FAUNA_INK);
        DrawEllipse((int)x, (int)y, len, 3.5f * s, side);
        DrawEllipse((int)x, (int)(y - 1.5f * s), len - 1, 1.6f * s, back);
        if (!dead && fmodf(t * 2 + b.phase, 3.0f) < 0.12f) DrawEllipse((int)x, (int)y, len * 0.6f, 1.5f, Color{255, 255, 255, 200}); // the flash of a turning flank
        DrawCircle((int)(x + f * (len - 3)), (int)(y - 1), 1.1f, dead ? deadTint : FAUNA_INK);
        if (b.straggler && fleeing && !dead) DrawCircle((int)(x + f * (len - 3)), (int)(y - 1), 1.6f, Color{240, 240, 255, 120}); // a wide, panicked eye
        break;
    }
    case HS_SHRIMP: {
        Color c = body(Color{200, 70, 60, 230}), stripe = body(Color{240, 236, 228, 255});
        float bob = dead ? 0 : sinf(t * 4 + b.phase) * 1.2f;
        for (int k = 0; k < 6; k++) { // a curled, segmented body - red with the white cleaner's stripe down its back
            float u = k / 5.0f, bx = x - f * (k * 2.6f - 5), by = y + bob + u * u * 5;
            DrawCircle((int)bx, (int)by, 3.4f - u * 1.2f + 0.8f, FAUNA_INK);
            DrawCircle((int)bx, (int)by, 3.4f - u * 1.2f, c);
            DrawCircle((int)bx, (int)(by - 1.5f), 1.0f, stripe);
        }
        DrawTri({x - f * 10, y + bob + 5}, {x - f * 15, y + bob + 1}, {x - f * 15, y + bob + 9}, c); // the tail fan
        if (!dead) for (int k = 0; k < 2; k++) { // long white antennae, sweeping - the "grooming" signal
            float sw = sinf(t * 5 + k * 1.7f + b.phase) * 4;
            DrawLineEx({x + f * 4, y + bob - 2}, {x + f * (16 + k * 3), y + bob - 12 + sw}, 1.0f, Color{245, 240, 230, 220});
        }
        for (int k = 0; k < 4 && !dead; k++) DrawLineEx({x - f * (k * 2.5f - 3), y + bob + 3}, {x - f * (k * 2.5f - 3) + sinf(t * 16 + k) * 1.5f, y + bob + 7}, 1.0f, c);
        break;
    }
    case HS_OCTOPUS: {
        float camo = dead ? 0 : std::clamp(b.special, 0.0f, 1.0f);
        bool hunting = b.act == BeastAct::Hunt || b.act == BeastAct::Coil || b.act == BeastAct::Strike || b.act == BeastAct::Eat;
        Color skin = hunting ? Color{200, 70, 70, 255} : fleeing ? Color{235, 225, 215, 255} : Color{150, 90, 130, 255}; // flushes red to hunt, blanches to flee
        skin = body(skin);
        float alpha = 1.0f - 0.82f * camo;
        Vector2 back = speed > 20 ? Vector2{-b.vel.x / speed, -b.vel.y / speed} : Vector2{0, 1};
        // what it's reaching for: its prey (or the diver), if it's within an arm's length
        bool reachFor = false; Vector2 prey{0, 0};
        if (!dead && (b.act == BeastAct::Coil || b.act == BeastAct::Strike || b.act == BeastAct::Hunt || b.act == BeastAct::Eat)) {
            if (b.target >= 0 && b.target < (int)W.beasts.size() && W.beasts[b.target].id == b.targetId) { prey = W.beasts[b.target].pos; reachFor = true; }
            else if (b.target == BEAST_DIVER) { Rectangle d = PlatDiverBox(p); prey = {d.x + d.width / 2, d.y + d.height / 2}; reachFor = true; }
            if (reachFor && sqrtf((prey.x - x) * (prey.x - x) + (prey.y - y) * (prey.y - y)) > 34 * s) reachFor = false;
        }
        for (int k = 0; k < 8; k++) { // eight arms, each a FABRIK chain: trailing and curling as it moves, the front pair reaching for prey
            float a0 = (k - 3.5f) * 0.32f, sw = sinf(t * 4 + k * 0.8f + b.phase) * 0.35f;
            float ca = cosf(a0 + sw), sa = sinf(a0 + sw);
            Vector2 dir{back.x * ca - back.y * sa, back.x * sa + back.y * ca};
            float len = (13 + (k % 3) * 2) * s * (speed > 60 ? 1.3f : 1.0f);
            Vector2 root{x + (k - 3.5f) * 1.2f, y + 3};
            Vector2 goal{x + dir.x * len + sinf(t * 6 + k) * 2, y + 4 + dir.y * len};
            if (reachFor && (k == 3 || k == 4 || (b.act == BeastAct::Eat && (k == 2 || k == 5))))
                goal = {prey.x + sinf(t * 9 + k) * 2, prey.y + cosf(t * 7 + k) * 2}; // wrapped round it
            Vector2 ch[5] = {root, root, root, root, root};
            float seg = len * 0.27f, lens[4] = {seg, seg, seg * 0.9f, seg * 0.8f};
            for (int j = 1; j < 5; j++) ch[j] = {root.x + dir.x * seg * j, root.y + dir.y * seg * j}; // start straight, then solve
            ik::Fabrik(ch, 5, lens, goal, 6);
            for (int j = 0; j < 4; j++) DrawLineEx(ch[j], ch[j + 1], 3.6f - j * 0.5f, Fade(FAUNA_INK, alpha));
            for (int j = 0; j < 4; j++) DrawLineEx(ch[j], ch[j + 1], 2.6f - j * 0.45f, Fade(skin, alpha));
            if (k % 2 == 0) for (int j = 1; j < 4; j++) DrawCircleV(ch[j], 0.7f, Fade(Tone(skin, 0.4f), alpha));
        }
        DrawEllipse((int)x, (int)(y - 3), 9.5f * s, 8.5f * s, Fade(FAUNA_INK, alpha));
        DrawEllipse((int)x, (int)(y - 3), 8.5f * s, 7.5f * s, Fade(skin, alpha));
        DrawEllipse((int)(x - f * 2), (int)(y - 6), 4 * s, 2.5f * s, Fade(Tone(skin, 0.25f), alpha));
        if (camo > 0.5f) DrawCircleLines((int)x, (int)(y - 3), 9 * s, Fade(Color{190, 200, 210, 255}, 0.25f * sinf(t * 2) * sinf(t * 2))); // the faint shimmer that gives it away
        DrawCircle((int)(x + f * 3), (int)(y - 2), 2.0f, Fade(Color{230, 220, 150, 255}, std::max(alpha, 0.5f))); // its eyes never quite disappear
        DrawRectangle((int)(x + f * 3) - 1, (int)y - 2, 2, 1, FAUNA_INK);
        break;
    }
    case HS_PUFFER: {
        bool puffed = b.act == BeastAct::Puffed;
        float r = (puffed ? 15.0f + sinf(t * 10) * 1.0f : 8.0f) * s;
        Color c = body(puffed ? Color{235, 170, 60, 255} : Color{205, 195, 95, 255});
        if (!puffed) { DrawTri({x - f * (r - 1), y}, {x - f * (r + 7), y - 5 + beat * 2}, {x - f * (r + 7), y + 5 + beat * 2}, FAUNA_INK); DrawTri({x - f * (r - 1), y}, {x - f * (r + 6), y - 4 + beat * 2}, {x - f * (r + 6), y + 4 + beat * 2}, c); }
        DrawCircle((int)x, (int)y, r + 1.5f, FAUNA_INK);
        DrawCircle((int)x, (int)y, r, c);
        DrawCircle((int)(x - f * 2), (int)(y + r * 0.35f), r * 0.6f, Tone(c, 0.3f)); // pale belly
        for (int k = 0; k < 4; k++) DrawCircle((int)(x - f * (r * 0.2f - k * 2.5f)), (int)(y - r * 0.4f), 1.2f, Tone(c, -0.35f)); // spots
        if (puffed) for (int k = 0; k < 14; k++) { float a = k * 2 * PI / 14 + t * 0.4f; DrawLineEx({x + cosf(a) * r, y + sinf(a) * r}, {x + cosf(a) * (r + 6), y + sinf(a) * (r + 6)}, 2.0f, Color{190, 70, 40, 255}); }
        else DrawTri({x, y - r * 0.2f}, {x - f * 2, y - r - 3}, {x - f * 6, y - r * 0.6f}, Tone(c, -0.2f)); // a little dorsal fin
        DrawCircle((int)(x + f * r * 0.45f), (int)(y - r * 0.3f), puffed ? 2.6f : 2.0f, FAUNA_INK);
        DrawCircle((int)(x + f * r * 0.5f), (int)(y - r * 0.35f), 0.8f, WHITE);
        if (!puffed) DrawRectangle((int)(x + f * (r - 1)) - 1, (int)y + 1, 2, 2, FAUNA_INK); // the beak
        break;
    }
    case HS_LEECH: {
        Color c = body(Color{120, 40, 55, 255});
        Vector2 pts[5];
        for (int k = 0; k < 5; k++) pts[k] = b.spine[std::min(k, SPINE - 1)];
        if (b.act == BeastAct::Latched) for (int k = 0; k < 5; k++) pts[k] = {x - f * k * 2.5f, y + sinf(t * 3 + k) * 0.8f};
        DrawSpineBody(pts, 5, 2.6f, 1.8f, b.act == BeastAct::Drift ? 3.0f : 1.2f, b.phase, c, Tone(c, 0.3f));
        DrawCircle((int)pts[0].x, (int)pts[0].y, 2.0f, Tone(c, -0.3f)); // the sucker
        break;
    }
    case HS_ANEMONE: {
        bool fed = b.act == BeastAct::Eat;
        Color c = fed ? Color{240, 120, 160, 255} : Color{195, 85, 125, 255};
        float by = y + 4;
        DrawRectangle((int)x - 5, (int)by - 6, 10, 7, FAUNA_INK);
        DrawRectangle((int)x - 4, (int)by - 6, 8, 6, Tone(c, -0.3f)); // the column it's rooted by
        for (int k = -4; k <= 4; k++) { // tentacles: open and sweeping when hungry, closed in a fist while it feeds
            float a = k * (fed ? 0.12f : 0.28f) + sinf(t * 2.4f + b.phase + k * 0.5f) * 0.14f;
            float len = fed ? 9.0f : 15.0f + fabsf((float)k) * -0.8f;
            Vector2 tip{x + sinf(a) * len, by - 6 - cosf(a) * len};
            DrawLineEx({x + k * 0.8f, by - 6}, tip, 3.2f, FAUNA_INK);
            DrawLineEx({x + k * 0.8f, by - 6}, tip, 2.0f, c);
            DrawCircleV(tip, 1.4f, Tone(c, 0.35f));
        }
        if (fed) DrawCircle((int)x, (int)(by - 12), 5, Fade(Color{255, 200, 220, 255}, 0.25f));
        break;
    }
    case HS_HERMIT: {
        Color shell = body(Color{186, 150, 98, 255}), crab = body(Color{214, 120, 80, 255});
        bool walking = speed > 10 && !dead;
        for (int k = 0; k < 3; k++) { // legs out the front of the shell, stepping
            float ph = b.phase * 9 + k * 2.1f, lift = walking ? std::max(0.0f, sinf(ph)) * 2 : 0;
            Vector2 hip{x + f * (4 + k * 1.5f), y + 1}, foot{x + f * (7 + k * 2.5f) + (walking ? cosf(ph) * 1.5f : 0), y + 6 - lift};
            DrawLineEx(hip, foot, 2.6f, FAUNA_INK); DrawLineEx(hip, foot, 1.4f, crab);
        }
        DrawCircle((int)(x - f * 1), (int)(y - 2), 7.5f * s, FAUNA_INK);
        DrawCircle((int)(x - f * 1), (int)(y - 2), 6.5f * s, shell);
        DrawRing({x - f * 1, y - 2}, 2.5f, 4.0f, 0, 300, 10, Tone(shell, -0.3f)); // the whorl
        DrawCircle((int)(x + f * 6), (int)(y - 1), 3.4f, FAUNA_INK);
        DrawCircle((int)(x + f * 6), (int)(y - 1), 2.6f, crab);
        float snap = b.act == BeastAct::Mob ? fabsf(sinf(t * 14)) * 2 : 0; // nipping in a mob
        DrawLineEx({x + f * 7, y - 1}, {x + f * (11 + snap), y - 4}, 2.2f, crab);
        DrawCircle((int)(x + f * 7), (int)(y - 5), 1.0f, FAUNA_INK);
        break;
    }
    case HS_BRITTLE: {
        bool broken = b.act == BeastAct::Drift;
        Color c{165, 152, 140, 230};
        float grow = broken ? std::min(1.0f, b.actT / 20.0f) * 0.7f + 0.2f : 1.0f; // regrowing arms after it broke
        for (int k = 0; k < 5; k++) {
            float a = k * 2 * PI / 5 + b.phase * 0.2f;
            Vector2 prev{x, y};
            for (int seg = 1; seg <= 4; seg++) { // each arm wriggles a little, segment by segment
                float u = seg / 4.0f, wig = sinf(t * 2 + k + seg) * 0.2f;
                Vector2 q{x + cosf(a + wig * u) * 17 * u * grow, y + sinf(a + wig * u) * 6 * u * grow};
                DrawLineEx(prev, q, 3.0f - u * 1.5f, FAUNA_INK);
                DrawLineEx(prev, q, 2.0f - u * 1.0f, c);
                prev = q;
            }
        }
        DrawCircle((int)x, (int)y, 3.5f, FAUNA_INK);
        DrawCircle((int)x, (int)y, 2.6f, Tone(c, -0.15f));
        break;
    }
    case HS_CRAB: {
        PlatEnemy e{'c', {x - 17, y - 10}, {x, y}, f, b.phase};
        e.state = 0;
        DrawEnemy(e, t, PL_HULL);
        if (b.act == BeastAct::Coil) { DrawRectangle((int)x - 1, (int)y - 26, 3, 7, Color{255, 90, 60, 255}); DrawRectangle((int)x - 1, (int)y - 17, 3, 3, Color{255, 90, 60, 255}); } // winding up: the "!" tell
        if (dead) DrawEllipse((int)x, (int)y, 14, 7, Fade(deadTint, 0.6f));
        break;
    }
    case HS_EEL: {
        Color c = body(Color{96, 110, 60, 255}), belly = body(Color{176, 170, 110, 255});
        bool coil = b.act == BeastAct::Coil, strike = b.act == BeastAct::Strike, eating = b.act == BeastAct::Eat;
        Vector2 pts[SPINE];
        for (int k = 0; k < SPINE; k++) pts[k] = b.spine[k];
        if (coil) for (int k = 1; k < SPINE; k++) { float kk = (float)k; pts[k].y += sinf(kk * 1.3f) * 5; } // drawing itself up into an S
        DrawSpineBody(pts, SPINE, 6.5f * s, 2.2f * s, coil ? 1.5f : strike ? 0.5f : 3.5f, b.phase, c, belly);
        for (int k = 1; k < SPINE; k += 2) DrawCircle((int)pts[k].x, (int)pts[k].y - 2, 1.5f, Tone(c, -0.35f)); // mottling
        Vector2 hd{pts[0].x - pts[1].x, pts[0].y - pts[1].y};
        float hl = sqrtf(hd.x * hd.x + hd.y * hd.y);
        hd = hl > 0.01f ? Vector2{hd.x / hl, hd.y / hl} : Vector2{f, 0};
        float open = coil ? 0.6f : strike ? 0.45f : eating ? 0.25f + 0.25f * fabsf(sinf(t * 12)) : 0.12f + 0.08f * sinf(t * 2 + b.phase); // morays breathe with their mouths open
        Vector2 jaw0{pts[0].x + hd.x * 4, pts[0].y + hd.y * 4};
        auto rot = [](Vector2 v, float a) { return Vector2{v.x * cosf(a) - v.y * sinf(a), v.x * sinf(a) + v.y * cosf(a)}; };
        float sideSign = hd.x >= 0 ? 1.0f : -1.0f;
        Vector2 up = rot(hd, -open * sideSign), dn = rot(hd, open * sideSign);
        DrawTri(jaw0, {jaw0.x + up.x * 11, jaw0.y + up.y * 11}, {jaw0.x + dn.x * 11, jaw0.y + dn.y * 11}, Color{70, 20, 30, 255}); // the gape
        DrawLineEx(jaw0, {jaw0.x + up.x * 11, jaw0.y + up.y * 11}, 4.0f, c);
        DrawLineEx(jaw0, {jaw0.x + dn.x * 10, jaw0.y + dn.y * 10}, 3.4f, belly);
        for (int k = 1; k <= 3; k++) DrawRectangle((int)(jaw0.x + up.x * k * 3), (int)(jaw0.y + up.y * k * 3) + 1, 1, 2, WHITE); // needle teeth
        DrawCircle((int)(pts[0].x + hd.x * 2), (int)(pts[0].y - 3), 1.8f, rabid ? Color{255, 60, 40, 255} : Color{230, 210, 80, 255});
        DrawCircle((int)(pts[0].x + hd.x * 2), (int)(pts[0].y - 3), 0.8f, FAUNA_INK);
        break;
    }
    // ---- ParkourReference1.3: the giants, the fodder and the flora
    case HS_WHALE: { // the Hull-Grazer: a flat, armoured back (the platform), a pale grooved belly, slow flukes
        Color hide = body(Color{72, 88, 106, 255}), belly = body(Color{168, 176, 172, 255}), plate = body(Color{104, 112, 118, 255});
        float bob = sinf(b.phase * 0.5f) * 1.0f;
        auto top = [](float u) { return u < 0.12f ? 12 + 15 * (u / 0.12f) : u > 0.88f ? 27 - (u - 0.88f) / 0.12f * 16 : 27.0f; };
        auto bot = [](float u) { return 4 + 20 * sinf(PI * (0.1f + 0.8f * u)); };
        const int N = 14;
        float fl = sinf(t * 1.2f + b.phase) * 8;
        Vector2 ts{x - f * 94, y + bob + 2};
        for (int pass = 0; pass < 2; pass++) { // the flukes, then the body over them; ink first
            float g = pass == 0 ? 2.0f : 0.0f;
            Color c = pass == 0 ? FAUNA_INK : hide;
            DrawTri({ts.x + f * g, ts.y}, {ts.x - f * (28 + g), ts.y - 16 + fl - g}, {ts.x - f * (16 + g), ts.y + fl * 0.4f}, c);
            DrawTri({ts.x + f * g, ts.y}, {ts.x - f * (28 + g), ts.y + 14 + fl * 0.6f + g}, {ts.x - f * (16 + g), ts.y + fl * 0.4f}, c);
            for (int k = 0; k < N; k++) {
                float u0 = k / (float)N, u1 = (k + 1) / (float)N;
                float x0 = x + f * (95 - u0 * 190), x1 = x + f * (95 - u1 * 190);
                Vector2 a{x0, y + bob - top(u0) - g}, bb{x1, y + bob - top(u1) - g}, cc{x1, y + bob + bot(u1) + g}, d{x0, y + bob + bot(u0) + g};
                DrawTri(a, bb, cc, c); DrawTri(a, cc, d, c);
                if (pass == 1) { // the pale belly with its grooves
                    Vector2 m0{x0, y + bob + bot(u0) * 0.25f}, m1{x1, y + bob + bot(u1) * 0.25f};
                    DrawTri(m0, m1, cc, belly); DrawTri(m0, cc, d, belly);
                    if (k % 2 == 0 && k < N - 2) DrawLineEx({x0, y + bob + bot(u0) * 0.45f}, {x1, y + bob + bot(u1) * 0.8f}, 1, Tone(belly, -0.25f));
                }
            }
        }
        for (float px = -72; px < 60; px += 16) { // armour plates along the back - flat on top, crusted with barnacles and rust
            float ax = x + px * f;
            DrawRectangle((int)std::min(ax, ax + 14 * f), (int)(y + bob - 27), 14, 7, plate);
            DrawRectangle((int)std::min(ax, ax + 14 * f), (int)(y + bob - 27), 14, 1, Tone(plate, 0.3f));
            DrawRectangle((int)std::min(ax, ax + 14 * f) + 13, (int)(y + bob - 27), 1, 7, FAUNA_INK);
            if ((int)px % 32 == 0) DrawCircle((int)(ax + 5 * f), (int)(y + bob - 22), 1.4f, Color{226, 216, 196, 255});
            else DrawRectangle((int)(ax + 4 * f), (int)(y + bob - 20), 2, 4, Color{130, 70, 40, 200});
        }
        float fin = sinf(t * 1.5f + b.phase) * 5; // the long pectoral fin
        DrawTri({x + f * 40, y + bob + 12}, {x + f * 12, y + bob + 34 + fin}, {x + f * 26, y + bob + 14}, Tone(hide, -0.2f));
        DrawCircle((int)(x + f * 72), (int)(y + bob + 2), 2.4f, FAUNA_INK);
        DrawCircle((int)(x + f * 72.5f), (int)(y + bob + 1.5f), 0.8f, Color{220, 230, 240, 255});
        DrawLineEx({x + f * 96, y + bob + 8}, {x + f * 46, y + bob + 12}, 1.4f, FAUNA_INK); // the mouth line, and the baleen fringe as it grazes
        for (int k = 0; k < 9; k++) DrawLineEx({x + f * (92 - k * 5), y + bob + 9}, {x + f * (92 - k * 5), y + bob + 12 + (k % 2)}, 1, Color{210, 196, 160, 255});
        break;
    }
    case HS_MEGALODON: { // the Steel-Biter: vast, dark above and pale below, scarred, its jaws full of plate
        Color back = body(Color{58, 66, 80, 255}), bel = body(Color{196, 200, 198, 255});
        bool coil = b.act == BeastAct::Coil, strike = b.act == BeastAct::Strike;
        // its shadow falls across everything under it
        for (int k = 0; k < 6; k++) DrawEllipse((int)x, (int)(y + 70 + k * 36), 230 - k * 12, 34, Fade(BLACK, 0.06f));
        auto top = [](float u) { float s = sinf(PI * (0.06f + 0.94f * u)); return 5 + 27 * powf(std::max(0.0f, s), 0.7f); };
        auto bot = [](float u) { return 3 + 21 * sinf(PI * (0.06f + 0.9f * u)); };
        auto sway = [&](float u) { return sinf(b.phase * 1.2f - u * 3) * u * (strike ? 2.0f : coil ? 9.0f : 5.0f); };
        const int N = 16;
        Vector2 tail{x - f * 96, y + sway(1)};
        float tb = sinf(b.phase * 1.2f - 3) * (coil ? 10.0f : 6.0f);
        for (int pass = 0; pass < 2; pass++) {
            float g = pass == 0 ? 2.0f : 0.0f;
            Color c = pass == 0 ? FAUNA_INK : back;
            DrawTri({tail.x + f * 6, tail.y - 4}, {tail.x - f * (26 + g), tail.y - 48 + tb - g}, {tail.x - f * (8 - g), tail.y}, c); // the crescent tail
            DrawTri({tail.x + f * 6, tail.y + 2}, {tail.x - f * (20 + g), tail.y + 30 + tb + g}, {tail.x - f * (8 - g), tail.y}, c);
            DrawTri({x - f * 10, y - top(0.45f) + 2}, {x - f * (36 + g), y - top(0.45f) - 36 - g}, {x - f * 44, y - top(0.55f) + 4}, c); // the dorsal fin
            for (int k = 0; k < N; k++) {
                float u0 = k / (float)N, u1 = (k + 1) / (float)N;
                float x0 = x + f * (100 - u0 * 196), x1 = x + f * (100 - u1 * 196);
                Vector2 a{x0, y + sway(u0) - top(u0) - g}, bb{x1, y + sway(u1) - top(u1) - g}, cc{x1, y + sway(u1) + bot(u1) + g}, d{x0, y + sway(u0) + bot(u0) + g};
                DrawTri(a, bb, cc, c); DrawTri(a, cc, d, c);
                if (pass == 1) { Vector2 m0{x0, y + sway(u0) + bot(u0) * 0.1f}, m1{x1, y + sway(u1) + bot(u1) * 0.1f}; DrawTri(m0, m1, cc, bel); DrawTri(m0, cc, d, bel); }
            }
            DrawTri({x + f * 40, y + 10}, {x + f * (2 - g), y + 44 + g}, {x + f * 22, y + 16}, c); // the pectoral fin
        }
        for (int k = 0; k < 5; k++) DrawLineEx({x + f * (52 - k * 5), y - 10}, {x + f * (48 - k * 5), y + 8}, 1.2f, Tone(back, -0.35f)); // gill slits
        DrawLineEx({x + f * 10, y - 14}, {x - f * 30, y - 4}, 1, Tone(back, 0.35f)); // old scars
        DrawLineEx({x - f * 20, y - 18}, {x - f * 44, y - 12}, 1, Tone(back, 0.35f));
        DrawCircle((int)(x + f * 74), (int)(y - 7), 3.0f, FAUNA_INK);
        DrawCircle((int)(x + f * 75), (int)(y - 8), 0.9f, Color{220, 220, 230, 255});
        float open = strike ? 1.0f : coil ? std::min(1.0f, b.actT / 0.6f) : 0.08f;
        Vector2 hinge{x + f * 58, y + 8}, up{x + f * 100, y - 2 - open * 10}, lo{x + f * 94, y + 10 + open * 22};
        if (open > 0.1f) {
            DrawTri(hinge, up, lo, Color{80, 16, 26, 255});
            for (int k = 1; k <= 6; k++) { // rows of teeth, and a shard of hull plate caught among them
                float u = k / 7.0f;
                Vector2 tu{hinge.x + (up.x - hinge.x) * u, hinge.y + (up.y - hinge.y) * u}, tl{hinge.x + (lo.x - hinge.x) * u, hinge.y + (lo.y - hinge.y) * u};
                DrawTri(tu, {tu.x + f * 3, tu.y}, {tu.x + f * 1.5f, tu.y + 5}, WHITE);
                DrawTri(tl, {tl.x + f * 3, tl.y}, {tl.x + f * 1.5f, tl.y - 5}, WHITE);
            }
            DrawRectangle((int)(hinge.x + f * 20), (int)(hinge.y + 2), 4, 3, Color{130, 136, 140, 255});
        } else DrawLineEx(hinge, {x + f * 96, y + 4}, 1.5f, FAUNA_INK);
        if (coil) DrawCircleLines((int)(x + f * 90), (int)y, 30 + 10 * sinf(t * 20), Fade(WHITE, 0.3f)); // the tell: it shudders before it comes
        break;
    }
    case HS_MITES: { // a carpet of barnacle-mites, seething; crushed, a slick smear
        if (b.act == BeastAct::Drift) { DrawEllipse((int)x, (int)y - 1, 34, 3, Fade(Color{196, 176, 132, 255}, std::max(0.0f, 0.8f - b.actT / 10))); break; }
        for (int k = 0; k < 26; k++) {
            float hx = Hs(b.id * 0.37f + k * 1.13f) * 72 - 36 + sinf(t * 3 + k) * 1.5f, hy = -1 - Hs(b.id * 0.71f + k * 2.3f) * 4;
            DrawRectangle((int)(x + hx), (int)(y + hy), 2, 2, k % 3 ? Color{150, 88, 70, 255} : Color{196, 150, 110, 255});
        }
        break;
    }
    case HS_RUST: { // brittle metallic fronds; broken, stubs regrowing
        Color rc{176, 92, 40, 255}, hi{226, 150, 80, 255};
        if (b.act == BeastAct::Drift) { float g = std::min(1.0f, b.actT / 18.0f); for (int k = 0; k < 4; k++) DrawRectangle((int)(x - 7 + k * 4), (int)(y - 2 - g * 6), 2, (int)(2 + g * 6), rc); break; }
        for (int k = 0; k < 6; k++) {
            float bx = x - 9 + k * 3.6f, h = 10 + (k * 7 % 5) * 2.0f, sw = sinf(t * 1.3f + k) * 1.5f;
            Vector2 a{bx, y}, m{bx + sw + (k % 2 ? 2.0f : -2.0f), y - h * 0.55f}, e{bx + sw * 1.6f, y - h};
            DrawLineEx(a, m, 3.0f, FAUNA_INK); DrawLineEx(m, e, 3.0f, FAUNA_INK);
            DrawLineEx(a, m, 2.0f, rc); DrawLineEx(m, e, 2.0f, rc);
            DrawRectangle((int)e.x, (int)e.y, 1, 1, hi);
        }
        break;
    }
    case HS_HYDROID: { // feathery stalks on a rivet; charged, a faint blue glow; discharging, arcs
        bool charged = b.special2 <= 0, firing = b.act == BeastAct::Eat;
        if (charged) DrawCircle((int)x, (int)(y - 8), 11 + sinf(t * 4) * 1.5f, Fade(Color{120, 200, 255, 255}, 0.12f));
        DrawCircle((int)x, (int)y - 1, 3.5f, FAUNA_INK); DrawCircle((int)x, (int)y - 1, 2.5f, Color{140, 144, 150, 255});
        for (int k = 0; k < 5; k++) {
            float a = (k - 2) * 0.3f + sinf(t * 2 + k) * 0.12f, h = 10 + (k % 3) * 3.0f;
            Vector2 tip{x + sinf(a) * h, y - 2 - cosf(a) * h};
            DrawLineEx({x, y - 2}, tip, 1.0f, Color{200, 214, 220, 255});
            DrawCircleV(tip, 1.6f, charged ? Color{190, 236, 255, 255} : Color{150, 160, 170, 255});
        }
        if (firing) for (int k = 0; k < 6; k++) { // the discharge: forked arcs
            float a = k * PI / 3 + t * 7;
            Vector2 prev{x, y - 8};
            for (int s = 1; s <= 4; s++) { Vector2 q{x + cosf(a) * s * 12 + sinf(t * 50 + s * k) * 4, y - 8 + sinf(a) * s * 12 + cosf(t * 43 + s) * 4}; DrawLineEx(prev, q, 1.5f, Color{190, 240, 255, 255}); prev = q; }
        }
        break;
    }
    case HS_PANEMONE: { // an elastic column; squashed flat for a moment when it fires
        bool fired = b.act == BeastAct::Eat;
        float h = fired ? 8.0f : 18.0f + sinf(t * 2 + b.phase) * 1.0f, w = fired ? 24.0f : 15.0f;
        Color c{70, 150, 160, 255}, disc{206, 112, 164, 255};
        DrawRectangleRounded({x - w / 2 - 1, y - h - 1, w + 2, h + 2}, 0.5f, 6, FAUNA_INK);
        DrawRectangleRounded({x - w / 2, y - h, w, h}, 0.5f, 6, c);
        for (int k = 1; k < 4; k++) DrawLineEx({x - w / 2 + 1, y - h * k / 4}, {x + w / 2 - 1, y - h * k / 4}, 1, Tone(c, -0.25f));
        DrawEllipse((int)x, (int)(y - h), w / 2 + 2, 3.5f, disc);
        for (int k = -3; k <= 3; k++) DrawLineEx({x + k * 2.5f, y - h}, {x + k * 3.2f + sinf(t * 3 + k) * 1.5f, y - h - 5}, 1.2f, Tone(disc, 0.25f));
        if (fired) for (int k = 0; k < 5; k++) DrawCircle((int)(x - b.facing * (14 + k * 8)), (int)(y - 10 + sinf(k * 2.1f) * 4), 2.5f - k * 0.3f, Fade(Color{220, 240, 250, 255}, 0.6f - k * 0.1f)); // the jet
        break;
    }
    case HS_MOSS: { // acid moss round a corroded seam; it fizzes when something walks through it
        DrawEllipse((int)x, (int)y, 20, 2.5f, Color{120, 70, 36, 200}); // the eaten metal under it
        for (int k = 0; k < 18; k++) {
            float hx = Hs(b.id * 0.37f + k * 1.13f) * 36 - 18, hy = -Hs(b.id * 0.53f + k * 1.9f) * 4;
            DrawCircle((int)(x + hx), (int)(y + hy), 2.0f, k % 2 ? Color{150, 176, 60, 255} : Color{96, 120, 40, 255});
        }
        if (b.flashT > 0) for (int k = 0; k < 5; k++) DrawCircle((int)(x - 12 + k * 6 + sinf(t * 9 + k) * 2), (int)(y - 6 - fmodf(t * 30 + k * 7, 14)), 1.2f, Color{210, 240, 120, 220});
        break;
    }
    case HS_KELP: { // hull-kelp: a thick strand from the hydroplane, its tip riding the current (matches OnKelp in beasts_hull.cpp)
        float wt = W.time;
        Vector2 root{x, y}, tip{x + sinf(wt * 1.1f + b.phase) * 8, y - 72};
        if (b.act == BeastAct::Eat) tip.x += b.facing * 18 * (1 - std::min(1.0f, b.actT / 0.5f)); // bent by a swing
        Vector2 prev = root;
        for (int k = 1; k <= 8; k++) {
            float u = k / 8.0f, bow = sinf(u * PI) * 6 * sinf(wt * 0.8f + b.phase);
            Vector2 q{root.x + (tip.x - root.x) * u + bow, root.y + (tip.y - root.y) * u};
            DrawLineEx(prev, q, 8.5f - u * 3, FAUNA_INK);
            DrawLineEx(prev, q, 6.5f - u * 3, Color{70, 92, 40, 255});
            DrawLineEx({prev.x - 1, prev.y}, {q.x - 1, q.y}, 1, Color{116, 136, 60, 255});
            if (k % 2 == 0) { float s = k % 4 ? 1.0f : -1.0f; DrawTri(q, {q.x + s * 12, q.y + 2 + sinf(wt * 2 + k) * 2}, {q.x + s * 3, q.y + 6}, Color{86, 110, 46, 255}); } // blades
            prev = q;
        }
        DrawEllipse((int)tip.x, (int)tip.y + 4, 3, 5, Color{150, 156, 70, 255}); // the gas bladder
        DrawCircle((int)x, (int)y - 1, 3, Color{90, 96, 100, 255});           // the holdfast on the plating
        break;
    }
    default: DrawCircle((int)x, (int)y, 6, Color{200, 200, 200, 255}); break;
    }
    if (b.species == HS_PILOT && !dead) for (int k = 0; k < 3; k++) DrawRectangle((int)(x + f * (3 - k * 4)) - 1, (int)y - 3, 1, 6, Color{40, 50, 70, 200}); // the pilot-fish's bands
    if (b.stunT > 0 && !dead) for (int k = 0; k < 3; k++) { float a = t * 6 + k * 2.1f; DrawCircle((int)(x + cosf(a) * 9), (int)(y - 12 + sinf(a) * 3), 1.3f, Color{190, 240, 255, 255}); } // stunned: sparks
    if (rabid && !dead && b.species != HS_EEL) DrawCircle((int)(x + f * 4), (int)(y - 4), 1.6f, Color{255, 40, 30, 255}); // a rabid animal's eye
    if (dead && b.meat < 0.7f) for (int k = 0; k < 3; k++) DrawRectangle((int)(x - 6 + k * 5), (int)(y + 1), 2, 3, Color{220, 210, 190, 255}); // picked to the bone
}

// The Siphon Octopus in its exhaust tube (it's always "hidden": nothing sees it until it pulls). Tube, eyes in the
// dark, a few arms along the plating; the windup flexes the rim and stirs the water, the pull draws streaks inward.
void DrawSiphon(const PlatformState& p, const Beast& b, float t) {
    float x = b.anchor.x, y = b.anchor.y, f = b.facing;
    bool coil = b.act == BeastAct::Coil, pull = b.act == BeastAct::Strike, stunned = b.stunT > 0;
    Color steel{104, 112, 118, 255};
    float ex = x + f * 16, flex = coil ? 1.5f + sinf(t * 30) * 1.5f : 0;
    float lx = std::min(x, ex);
    DrawRectangle((int)lx - 1, (int)(y - 17 - flex), 18, (int)(34 + flex * 2), FAUNA_INK); // the pipe stub out of the plating
    DrawRectangle((int)lx, (int)(y - 16 - flex), 16, (int)(32 + flex * 2), steel);
    DrawRectangle((int)lx, (int)(y - 16 - flex), 16, 3, Tone(steel, 0.35f));
    DrawRectangle((int)lx, (int)(y + 12 + flex), 16, 4, Tone(steel, -0.35f));
    for (int k = 0; k < 3; k++) DrawRectangle((int)lx + 3 + k * 5, (int)(y - 16 - flex), 1, (int)(32 + flex * 2), Tone(steel, -0.2f));
    DrawRectangle((int)lx + 2, (int)(y - 6), 3, 12, Color{128, 66, 36, 220}); // rust weeping from the seam
    DrawEllipse((int)ex, (int)y, 7 + flex * 0.5f, 19 + flex, FAUNA_INK); // the flange, bolted
    DrawEllipse((int)ex, (int)y, 6 + flex * 0.5f, 18 + flex, Tone(steel, 0.15f));
    DrawEllipse((int)ex, (int)y, 4 + flex * 0.5f, 14 + flex, Color{14, 10, 18, 255});
    for (int k = 0; k < 4; k++) DrawRectangle((int)ex - 1, (int)(y - 16 + k * 10.5f), 2, 2, Color{190, 190, 180, 255});
    if (coil || pull) DrawEllipseLines((int)ex, (int)y, 8 + flex, 20 + flex, Color{200, 230, 240, 220});
    if (!stunned && fmodf(t * 0.6f + b.phase, 5.0f) > 0.2f) { // eyes in the dark of the tube
        DrawRectangle((int)(ex - f * 2) - 1, (int)y - 6, 3, 1, Color{240, 210, 90, 255});
        DrawRectangle((int)(ex - f * 2) - 1, (int)y - 1, 3, 1, Color{240, 210, 90, 255});
    }
    Color arm = stunned ? Color{150, 110, 110, 255} : Color{176, 84, 72, 255};
    Rectangle d = PlatDiverBox(p);
    Vector2 diver{d.x + d.width / 2, d.y + d.height / 2};
    for (int k = 0; k < 4; k++) { // arms: curled on the plating, spread wide in the windup, reaching in the pull, limp when stunned
        Vector2 root{ex, y + 6 + k * 2.0f};
        float reach = 18 + k * 6;
        Vector2 goal = stunned ? Vector2{ex + f * reach * 0.5f, y + 16}
                     : coil ? Vector2{ex + f * reach, y - 18 + k * 12 + sinf(t * 12 + k) * 3}
                     : pull && k < 2 && fabsf(diver.x - ex) < 90 ? diver
                     : Vector2{ex + f * reach + sinf(t * 2 + k * 1.3f) * 3, y + 14 + sinf(t * 3 + k) * 2};
        Vector2 ch[5];
        float seg = reach * 0.3f, lens[4] = {seg, seg, seg * 0.9f, seg * 0.8f};
        for (int j = 0; j < 5; j++) ch[j] = {root.x + f * seg * j, root.y};
        ik::Fabrik(ch, 5, lens, goal, 6);
        for (int j = 0; j < 4; j++) DrawLineEx(ch[j], ch[j + 1], 5.5f - j * 0.8f, FAUNA_INK);
        for (int j = 0; j < 4; j++) DrawLineEx(ch[j], ch[j + 1], 4.0f - j * 0.7f, arm);
        for (int j = 1; j < 4; j++) DrawCircleV(ch[j], 0.8f, Color{226, 190, 170, 255}); // suckers
    }
    if (coil) for (int k = 0; k < 6; k++) { float a = k * 1.05f + t * 3; DrawCircle((int)(ex + f * (20 + 10 * sinf(a))), (int)(y + cosf(a) * 14), 1.2f, Color{200, 236, 250, 180}); } // the water stirring
    if (pull) for (int k = 0; k < 14; k++) { // streaks rushing into the mouth
        float a = (k / 13.0f - 0.5f) * 2.2f, r = 200 * (1 - fmodf(t * 1.8f + k * 0.137f, 1.0f));
        Vector2 q{ex + f * cosf(a) * r, y + sinf(a) * r};
        Vector2 q2{ex + f * cosf(a) * (r - 14), y + sinf(a) * (r - 14)};
        DrawLineEx(q, q2, 2, Fade(Color{210, 240, 255, 255}, 0.75f));
    }
    if (stunned) for (int k = 0; k < 4; k++) { float a = t * 7 + k * 1.6f; DrawCircle((int)(ex + cosf(a) * 10), (int)(y - 20 + sinf(a) * 3), 1.3f, Color{190, 240, 255, 255}); }
}

// A den: a breach torn in the hull plating, crusted with barnacles. Something inside watches out of it.
void DrawHullDen(const PlatformState& p, const Den& d, float t) {
    const BeastWorld& W = p.fauna;
    float x = d.pos.x, y = d.pos.y;
    DrawEllipse((int)x, (int)y + 3, 12, 6, FAUNA_INK);
    DrawEllipse((int)x, (int)y + 3, 10, 4.5f, Color{24, 16, 20, 255});
    for (int k = 0; k < 5; k++) DrawTri({x - 12 + k * 5.5f, y}, {x - 9 + k * 5.5f, y}, {x - 10.5f + k * 5.5f, y + 4 + (k % 2) * 2}, Color{120, 124, 118, 255}); // torn plate edge
    DrawRing({x, y + 3}, 11, 13, 190, 350, 12, Color{120, 64, 36, 200}); // rust weeping from the tear
    for (int k = 0; k < 3; k++) { float bx = x - 14 + k * 13; DrawCircle((int)bx, (int)y + 1, 2.2f, Color{214, 196, 170, 255}); DrawCircle((int)bx, (int)y + 1, 0.9f, Color{60, 30, 40, 255}); }
    int inside = 0;
    for (const auto& b : W.beasts) if (b.life == BeastLife::Alive && b.hidden && fabsf(b.pos.x - x) < 4 && !(b.species == HS_EEL && b.act == BeastAct::Ambush)) inside++;
    if (inside && fmodf(t * 0.7f + x * 0.013f, 4.0f) < 3.0f) { DrawCircle((int)x - 3, (int)y + 3, 1.1f, Color{240, 220, 140, 255}); DrawCircle((int)x + 3, (int)y + 3, 1.1f, Color{240, 220, 140, 255}); }
}

// An eel waiting in its breach: only the head, swaying, jaws working.
void DrawEelPeek(const Beast& b, float t) {
    float x = b.pos.x, y = b.pos.y - 8;
    float sway = sinf(t * 1.6f + b.phase) * 3;
    Vector2 pts[4] = {{x + sway, y - 14}, {x + sway * 0.6f, y - 8}, {x + sway * 0.3f, y - 3}, {x, y + 2}};
    DrawSpineBody(pts, 4, 6.0f, 5.0f, 0, b.phase, Color{96, 110, 60, 255}, Color{176, 170, 110, 255});
    float open = 0.15f + 0.12f * sinf(t * 2.2f + b.phase);
    Vector2 j{x + sway, y - 17};
    DrawTri(j, {j.x - 5, j.y - 8 - open * 6}, {j.x + 5, j.y - 8 - open * 6}, Color{70, 20, 30, 255});
    DrawLineEx(j, {j.x - 5, j.y - 9 - open * 6}, 3, Color{96, 110, 60, 255});
    DrawLineEx(j, {j.x + 5, j.y - 9 - open * 6}, 3, Color{176, 170, 110, 255});
    DrawCircle((int)(x + sway - 3), (int)(y - 15), 1.6f, Color{230, 210, 80, 255});
    DrawCircle((int)(x + sway + 3), (int)(y - 15), 1.6f, Color{230, 210, 80, 255});
}
// ---------------------------------------------------------------- legged beasts: IK legs over planted feet
// Far legs are drawn before the body, near legs after, each a two-bone limb solved by ik::Knee from the hip to
// the foot the gait engine planted (beasts.cpp, UpdateGait).
void DrawBeastLegs(const Beast& b, const SpeciesDef& S, bool nearSide, float thick, Color c, float frontBend, float backBend) {
    float legLen = std::min(12.0f, S.radius * b.scale * 0.6f) + S.radius * b.scale * 0.2f;
    for (int k = 0; k < LEGS; k++) {
        if ((k % 2 == 0) != nearSide) continue;
        Vector2 hip = BeastHip(b, S, k), foot = b.legs[k].init ? b.legs[k].foot : Vector2{hip.x, hip.y + legLen};
        bool lame = k == 2 && b.health < 0.6f && b.life == BeastLife::Alive; // a persistent injury: the hind leg held up, dragged
        if (lame) foot = {hip.x - b.facing * legLen * 0.4f, hip.y + legLen * 0.62f + fabsf(sinf(b.phase * 6)) * 1.5f};
        float bend = (k < 2 ? frontBend : backBend) * b.facing;
        float l1 = legLen * 0.55f, l2 = legLen * 0.6f;
        Vector2 knee = ik::Knee(hip, foot, l1, l2, bend);
        Color col = nearSide ? c : Tone(c, -0.35f);
        DrawLineEx(hip, knee, thick + 2, FAUNA_INK); DrawLineEx(knee, foot, thick + 1.5f, FAUNA_INK);
        DrawLineEx(hip, knee, thick, col); DrawLineEx(knee, foot, thick * 0.8f, col);
        DrawCircleV(foot, thick * 0.6f, Tone(col, -0.2f));
        if (lame) DrawCircleV(knee, 1.2f, Color{150, 30, 40, 255});
    }
}
// A tail of fixed length, bent segment by segment: lifted by mood (lift 0 droops, 1 stands up), lashing when it's
// agitated, and swept back by the animal's own speed. (It used to follow the body's movement trail, which made
// tails stretch out behind a running animal like rope.)
void DrawBeastTail(const Beast& b, Vector2 root, float lift, float lash, float w0, float w1, Color c, int segs = 5) {
    float f = b.facing, speed = fabsf(b.vel.x);
    float len = 1.6f + w0 * 0.8f;                             // segment length scales with the tail's thickness
    float base = (f > 0 ? PI : 0.0f) + f * (lift * 1.1f - 0.25f); // straight back (y is down: a larger angle lifts it when facing right)
    base -= f * std::min(0.4f, speed / 500.0f);                  // swept back flatter as it runs
    Vector2 pts[9];
    pts[0] = root;
    int n = std::min(segs, 8);
    for (int k = 1; k <= n; k++) {
        float u = k / (float)n;
        float a = base + f * (lift * 0.35f * u) + sinf(b.phase * 3 - k * 0.8f) * lash * 0.12f * u;
        pts[k] = {pts[k - 1].x + cosf(a) * len, pts[k - 1].y + sinf(a) * len};
    }
    for (int k = 0; k < n; k++) { float u = k / (float)n; DrawLineEx(pts[k], pts[k + 1], w0 + (w1 - w0) * u + 2, FAUNA_INK); }
    for (int k = 0; k < n; k++) { float u = k / (float)n; DrawLineEx(pts[k], pts[k + 1], w0 + (w1 - w0) * u, c); }
}// A bird: body, head and beak, and two wings that flap (or hold a glide, or fold back in a dive).
void DrawBird(const Beast& b, float t, float span, float bodyR, Color body, Color wing, Color beak, float flapRate, bool diving) {
    float x = b.pos.x, y = b.pos.y, f = b.facing;
    float speed = sqrtf(b.vel.x * b.vel.x + b.vel.y * b.vel.y);
    bool gliding = speed > 60 && fmodf(b.phase * 0.35f, 3.0f) > 1.6f;
    float flap = diving ? -0.9f : gliding ? 0.15f : sinf(t * flapRate + b.phase * 2);
    for (int side = 0; side < 2; side++) { // far wing first
        float dep = side == 0 ? -1.5f : 1.5f;
        Vector2 root{x - f * bodyR * 0.2f, y - bodyR * 0.3f + dep};
        Vector2 elbow{root.x - f * span * 0.2f, root.y - flap * span * 0.45f};
        Vector2 tip{root.x - f * span * (diving ? 0.9f : 0.55f), root.y - flap * span * 0.75f + (diving ? span * 0.2f : 0)};
        Color wc = side == 0 ? Tone(wing, -0.3f) : wing;
        DrawTri(root, elbow, {root.x - f * span * 0.35f, root.y + 2}, FAUNA_INK);
        DrawLineEx(root, elbow, 4.5f, FAUNA_INK); DrawLineEx(elbow, tip, 3.5f, FAUNA_INK);
        DrawLineEx(root, elbow, 3.0f, wc); DrawLineEx(elbow, tip, 2.0f, wc);
        DrawTri(root, elbow, {root.x - f * span * 0.3f, root.y + 1.5f}, wc);
        if (side == 0) { DrawEllipse((int)x, (int)y, bodyR * 1.35f + 1, bodyR * 0.8f + 1, FAUNA_INK); DrawEllipse((int)x, (int)y, bodyR * 1.35f, bodyR * 0.8f, body); }
    }
    DrawTri({x - f * bodyR * 1.2f, y}, {x - f * (bodyR * 2.2f), y - 3}, {x - f * (bodyR * 2.2f), y + 3}, Tone(wing, -0.1f)); // tail
    DrawCircle((int)(x + f * bodyR * 1.1f), (int)(y - bodyR * 0.5f), bodyR * 0.6f + 1, FAUNA_INK);
    DrawCircle((int)(x + f * bodyR * 1.1f), (int)(y - bodyR * 0.5f), bodyR * 0.6f, body);
    DrawTri({x + f * bodyR * 1.5f, y - bodyR * 0.6f}, {x + f * bodyR * 2.3f, y - bodyR * 0.4f}, {x + f * bodyR * 1.5f, y - bodyR * 0.25f}, beak);
    DrawCircle((int)(x + f * bodyR * 1.3f), (int)(y - bodyR * 0.65f), 1.0f, FAUNA_INK);
}

// A dog (the Pirate Ship's guard dogs, the Island's hunting dogs): wags when content, hackles and foam when berserk.
void DrawDog(const Beast& b, const SpeciesDef& S, float t, Color c, bool berserk, bool lean) {
    float x = b.pos.x, y = b.pos.y, f = b.facing, s = b.scale;
    Color muzzle = Tone(c, 0.35f);
    x += berserk ? sinf(t * 40) * 1.2f : 0;
    float sniff = (b.act == BeastAct::Hunt && !berserk && lean) ? 3.0f : 0.0f; // nose down on a trail
    DrawBeastLegs(b, S, false, lean ? 2.4f : 3.0f, c, -0.5f, 0.9f);
    DrawBeastTail(b, {x - f * 11, y - 4}, berserk ? 0.2f : 0.9f, berserk ? 0.5f : fmodf(t, 4) < 1 ? 3.0f : 0.8f, 3.0f, 1.8f, c, 4);
    DrawEllipse((int)x, (int)y - 2, (lean ? 12.0f : 13.0f) * s, (lean ? 6.0f : 7.5f) * s, FAUNA_INK);
    DrawEllipse((int)x, (int)y - 2, (lean ? 11.0f : 12.0f) * s, (lean ? 5.0f : 6.5f) * s, c);
    if (berserk) for (int k = 0; k < 5; k++) DrawTri({x - 6.0f + k * 3, y - 7}, {x - 5.0f + k * 3, y - 12 - (k % 2) * 2}, {x - 4.0f + k * 3, y - 7}, Tone(c, -0.3f));
    Vector2 head{x + f * 12, y - 8 + sniff};
    DrawCircleV(head, 6.0f * s, FAUNA_INK); DrawCircleV(head, 5.0f * s, c);
    DrawEllipse((int)(head.x + f * 5), (int)head.y + 2, 4.5f, 3.2f, FAUNA_INK); DrawEllipse((int)(head.x + f * 5), (int)head.y + 2, 3.8f, 2.5f, muzzle);
    if (lean) DrawTri({head.x - f * 1, head.y - 4}, {head.x - f * 2, head.y - 10}, {head.x + f * 2, head.y - 4}, Tone(c, -0.2f)); // pricked ears
    else DrawTri({head.x - f * 2, head.y - 4}, {head.x - f * 5, head.y + 3}, {head.x - f * 1, head.y + 1}, Tone(c, -0.3f));
    DrawCircle((int)(head.x + f * 2), (int)head.y - 2, berserk ? 1.5f : 1.0f, berserk ? Color{255, 60, 40, 255} : FAUNA_INK);
    DrawCircle((int)(head.x + f * 8.5f), (int)head.y + 1, 1.2f, FAUNA_INK);
    if (berserk || b.act == BeastAct::Coil) { DrawRectangle((int)(head.x + f * 4), (int)head.y + 4, 4, 1, WHITE); if (berserk) DrawCircle((int)(head.x + f * 6), (int)head.y + 6, 1.3f, Fade(WHITE, 0.8f)); }
    DrawBeastLegs(b, S, true, lean ? 2.4f : 3.0f, c, -0.5f, 0.9f);
}

void DrawIslandBeast(const PlatformState& p, const Beast& b, float t) {
    const SpeciesDef& S = BeastSpecies(PL_ISLAND, b.species);
    float x = b.pos.x, y = b.pos.y, f = b.facing, s = b.scale;
    bool dead = b.life == BeastLife::Corpse;
    bool fleeing = b.act == BeastAct::Flee || b.act == BeastAct::Hide;
    bool coil = b.act == BeastAct::Coil, strike = b.act == BeastAct::Strike;
    if (b.species == IS_COCONUT) { // a fallen coconut, cracked open as it's eaten
        DrawCircle((int)x, (int)y, 5.5f, FAUNA_INK); DrawCircle((int)x, (int)y, 4.5f, Color{120, 80, 44, 255});
        if (b.meat < 0.95f) { DrawCircle((int)x, (int)y - 1, 3.2f, Color{244, 240, 226, 255}); DrawCircle((int)x, (int)y - 1, 1.6f, Color{200, 190, 170, 255}); }
        DrawCircle((int)x - 2, (int)y - 2, 0.8f, Color{60, 40, 24, 255}); DrawCircle((int)x + 1, (int)y - 3, 0.8f, Color{60, 40, 24, 255});
        return;
    }
    if (dead) {
        float r = S.radius * s;
        DrawEllipse((int)x, (int)y + 1, r * 1.1f + 1, r * 0.55f + 1, FAUNA_INK);
        DrawEllipse((int)x, (int)y + 1, r * 1.1f, r * 0.55f, Color{112, 100, 92, 255});
        if (b.meat < 0.7f) for (int k = 0; k < 3; k++) DrawRectangle((int)(x - 5 + k * 4), (int)y, 2, 2, Color{220, 210, 190, 255});
        return;
    }
    switch (b.species) {
    case IS_BOAR: {
        Color c{96, 70, 52, 255}, bristle{60, 44, 34, 255};
        float low = coil ? 3.0f : 0.0f; // head down, pawing, before it goes
        DrawBeastLegs(b, S, false, 3.4f, c, -0.3f, 0.7f);
        DrawEllipse((int)x, (int)y - 3, 15.0f * s, 9.5f * s, FAUNA_INK);
        DrawEllipse((int)x, (int)y - 3, 14.0f * s, 8.5f * s, c);
        for (int k = 0; k < 7; k++) DrawTri({x - 9.0f + k * 3, y - 10}, {x - 8.0f + k * 3, y - 15 - (coil || strike ? 3 : 0)}, {x - 7.0f + k * 3, y - 10}, bristle); // the ridge of bristles
        Vector2 head{x + f * 14, y - 3 + low};
        DrawCircleV(head, 7.5f * s, FAUNA_INK); DrawCircleV(head, 6.5f * s, c);
        DrawEllipse((int)(head.x + f * 6), (int)head.y + 2, 3.5f, 3.2f, FAUNA_INK); DrawEllipse((int)(head.x + f * 6), (int)head.y + 2, 2.8f, 2.6f, Color{170, 120, 110, 255}); // snout
        DrawTri({head.x + f * 3, head.y + 4}, {head.x + f * 9, head.y - 2}, {head.x + f * 4, head.y + 1}, Color{236, 228, 208, 255}); // a tusk
        DrawCircle((int)(head.x + f * 1), (int)head.y - 3, 1.1f, coil || strike ? Color{255, 90, 50, 255} : FAUNA_INK);
        DrawTri({head.x - f * 3, head.y - 5}, {head.x - f * 1, head.y - 11}, {head.x + f * 1, head.y - 5}, bristle);
        if (strike) for (int k = 0; k < 3; k++) DrawCircle((int)(x - f * (12 + k * 6)), (int)y + 6, 2.0f - k * 0.4f, Fade(Color{200, 186, 150, 255}, 0.6f)); // dust kicked up
        DrawBeastLegs(b, S, true, 3.4f, c, -0.3f, 0.7f);
        break;
    }
    case IS_SNAKE: { // along its own spine; fades into the branch when camouflaged, jaws wide when it strikes
        float camo = std::clamp(b.special, 0.0f, 1.0f), alpha = 1.0f - 0.7f * camo;
        Color c = Fade(Color{80, 132, 58, 255}, alpha), belly = Fade(Color{190, 196, 110, 255}, alpha);
        Vector2 pts[SPINE];
        for (int k = 0; k < SPINE; k++) pts[k] = b.spine[k];
        if (coil) for (int k = 1; k < SPINE; k++) { pts[k].x -= f * sinf(k * 1.4f) * 3; pts[k].y += cosf(k * 1.4f) * 4; } // drawn back into an S
        DrawSpineBody(pts, SPINE, 3.2f * s, 1.2f * s, coil ? 0.8f : 2.2f, b.phase, c, belly);
        for (int k = 1; k < SPINE; k += 2) DrawCircle((int)pts[k].x, (int)pts[k].y - 1, 1.0f, Fade(Color{40, 34, 20, 255}, alpha)); // bands
        Vector2 hd{pts[0].x - pts[1].x, pts[0].y - pts[1].y}; float hl = sqrtf(hd.x * hd.x + hd.y * hd.y); hd = hl > 0.01f ? Vector2{hd.x / hl, hd.y / hl} : Vector2{f, 0};
        if (strike || coil) DrawTri(pts[0], {pts[0].x + hd.x * 6 - hd.y * 3, pts[0].y + hd.y * 6 + hd.x * 3}, {pts[0].x + hd.x * 6 + hd.y * 3, pts[0].y + hd.y * 6 - hd.x * 3}, Color{190, 60, 80, 255});
        else if (fmodf(t * 1.5f + b.phase, 2.0f) < 0.25f) DrawLineEx(pts[0], {pts[0].x + hd.x * 7, pts[0].y + hd.y * 7}, 1.0f, Color{200, 40, 60, 255}); // the tongue, tasting
        DrawCircle((int)(pts[0].x + hd.x), (int)(pts[0].y - 2), 0.9f, Fade(Color{240, 200, 40, 255}, std::max(alpha, 0.6f)));
        break;
    }
    case IS_LIZARD: {
        Color c{104, 110, 72, 255}, spots{70, 76, 48, 255};
        bool basking = b.act == BeastAct::Rest || (sqrtf(b.vel.x * b.vel.x) < 5 && b.act == BeastAct::Wander);
        DrawBeastLegs(b, S, false, 2.4f, c, 0.9f, 0.9f); // sprawling legs, elbows out
        DrawBeastTail(b, {x - f * 10, y + 1}, -0.1f, 1.2f, 3.4f, 1.0f, c, 7);
        DrawEllipse((int)x, (int)y, 12.0f * s, 5.0f * s, FAUNA_INK);
        DrawEllipse((int)x, (int)y, 11.0f * s, 4.0f * s, c);
        for (int k = 0; k < 4; k++) DrawCircle((int)(x - 7 + k * 4), (int)y - 2, 1.1f, spots);
        Vector2 head{x + f * 13, y - (basking ? 3.0f : 1.0f)};
        DrawEllipse((int)head.x, (int)head.y, 5.5f, 3.4f, FAUNA_INK); DrawEllipse((int)head.x, (int)head.y, 4.6f, 2.6f, c);
        DrawCircle((int)(head.x + f * 1), (int)head.y - 1, 0.9f, FAUNA_INK);
        if (fmodf(t * 0.8f + b.phase, 3.0f) < 0.3f) DrawLineEx({head.x + f * 4, head.y}, {head.x + f * 10, head.y - 1}, 1.2f, Color{230, 200, 200, 255}); // forked tongue
        DrawBeastLegs(b, S, true, 2.4f, c, 0.9f, 0.9f);
        break;
    }
    case IS_BAT: {
        bool hanging = b.act == BeastAct::Rest || b.act == BeastAct::Idle;
        if (hanging && sqrtf(b.vel.x * b.vel.x + b.vel.y * b.vel.y) < 15) { // wrapped in its wings, upside down
            DrawEllipse((int)x, (int)y, 4.5f, 7, FAUNA_INK); DrawEllipse((int)x, (int)y, 3.6f, 6, Color{70, 52, 48, 255});
            DrawCircle((int)x, (int)y + 5, 2.6f, Color{150, 100, 70, 255});
            break;
        }
        float flap = sinf(t * 18 + b.phase * 3) * 5;
        Color wing{66, 48, 50, 235}, fur{150, 100, 70, 255};
        for (int side = -1; side <= 1; side += 2) {
            Vector2 root{x, y - 1}, el{x + side * 7.0f, y - 4 - flap}, tip{x + side * 14.0f, y - flap * 0.5f};
            DrawTri(root, el, {x + side * 5.0f, y + 3}, wing); DrawTri(el, tip, {x + side * 9.0f, y + 3}, wing);
            DrawLineEx(root, el, 1.2f, FAUNA_INK); DrawLineEx(el, tip, 1.0f, FAUNA_INK);
        }
        DrawEllipse((int)x, (int)y, 3.8f, 3.2f, fur);
        DrawCircle((int)(x + f * 2.5f), (int)y - 2, 2.2f, fur);
        DrawCircle((int)(x + f * 3.2f), (int)y - 2.5f, 0.6f, FAUNA_INK);
        break;
    }
    case IS_SPIDER: { // the orb web and its owner at the hub; a fed spider wraps its catch
        float wr = 16;
        for (int k = 0; k < 8; k++) { float a = k * PI / 4; DrawLineEx({x, y}, {x + cosf(a) * wr, y + sinf(a) * wr}, 0.6f, Fade(Color{235, 235, 235, 255}, 0.45f)); }
        for (int r = 4; r <= 16; r += 4) DrawPolyLines({x, y}, 8, (float)r, 22.5f, Fade(Color{235, 235, 235, 255}, 0.35f));
        if (b.act == BeastAct::Eat) DrawEllipse((int)x + 5, (int)y + 5, 3, 4, Color{230, 226, 210, 255}); // a wrapped bundle
        for (int k = 0; k < 4; k++) for (int side = -1; side <= 1; side += 2) DrawLineEx({x, y}, {x + side * (4 + k), y - 3 + k * 2.2f + sinf(t * 2 + k) * 0.5f}, 0.9f, FAUNA_INK);
        DrawCircle((int)x, (int)y, 3.0f, Color{230, 190, 40, 255}); DrawCircle((int)x, (int)y + 3, 2.2f, FAUNA_INK);
        break;
    }
    case IS_CRAB: {
        Color c{206, 96, 52, 255}, dark = Tone(c, -0.3f);
        bool cutting = b.special > 12;
        for (int side = 0; side < 2; side++) for (int k = 0; k < 3; k++) { // eight jointed legs, stepping
            float ph = b.phase * 8 + k * 2.1f + side * 1.3f, lift = sqrtf(b.vel.x * b.vel.x) > 5 ? std::max(0.0f, sinf(ph)) * 2 : 0;
            float sx = (side ? 1 : -1);
            Vector2 hip{x + sx * (3 + k * 2.0f), y - 2}, foot{x + sx * (10 + k * 3.0f), y + 6 - lift};
            Vector2 knee = ik::Knee(hip, foot, 6, 6, sx);
            DrawLineEx(hip, knee, 2.4f, FAUNA_INK); DrawLineEx(knee, foot, 2.0f, FAUNA_INK);
            DrawLineEx(hip, knee, 1.4f, side ? c : dark); DrawLineEx(knee, foot, 1.1f, side ? c : dark);
        }
        DrawEllipse((int)x, (int)y - 3, 10 * s, 6.5f * s, FAUNA_INK); DrawEllipse((int)x, (int)y - 3, 9 * s, 5.5f * s, c);
        float snip = cutting ? sinf(t * 16) * 2 : 0;
        for (int side = -1; side <= 1; side += 2) { // the great claws, raised to cut
            Vector2 sh{x + f * 6 + side * 2, y - 5}, cl{x + f * 13 + side * 3, y - (cutting ? 18 : 9) + snip * side};
            DrawLineEx(sh, cl, 3.6f, FAUNA_INK); DrawLineEx(sh, cl, 2.4f, c);
            DrawCircleV(cl, 3.2f, FAUNA_INK); DrawCircleV(cl, 2.4f, Tone(c, 0.2f));
        }
        DrawCircle((int)(x + f * 4), (int)y - 8, 1.0f, FAUNA_INK); DrawCircle((int)(x + f * 7), (int)y - 8, 1.0f, FAUNA_INK);
        break;
    }
    case IS_FROG: {
        Color cols[3] = {{40, 160, 220, 255}, {240, 200, 40, 255}, {230, 70, 50, 255}};
        Color c = cols[b.id % 3];
        bool air = !b.grounded;
        float stretch = air ? 1.4f : 1.0f;
        DrawEllipse((int)x, (int)y, 5.5f * s * stretch, 4.2f * s / stretch, FAUNA_INK);
        DrawEllipse((int)x, (int)y, 4.6f * s * stretch, 3.4f * s / stretch, c);
        for (int k = 0; k < 3; k++) DrawCircle((int)(x - 2 + k * 2), (int)y - 1 + (k % 2), 0.8f, FAUNA_INK); // the warning spots
        if (air) DrawLineEx({x - f * 3, y + 2}, {x - f * 9, y + 5}, 1.6f, c); // legs flung back mid-hop
        else { DrawLineEx({x - f * 3, y + 2}, {x - f * 5, y + 4}, 1.8f, c); DrawLineEx({x + f * 2, y + 2}, {x + f * 3, y + 4}, 1.4f, c); }
        DrawCircle((int)(x + f * 2.5f), (int)y - 3, 1.4f, FAUNA_INK); DrawCircle((int)(x + f * 2.8f), (int)y - 3.3f, 0.5f, WHITE);
        break;
    }
    case IS_GULL:
        DrawBird(b, t, 18 * s, 5 * s, Color{240, 238, 232, 255}, Color{156, 160, 168, 255}, Color{236, 196, 60, 255}, 11, strike);
        break;
    case IS_DOG: {
        Color coats[3] = {{176, 132, 84, 255}, {120, 90, 60, 255}, {210, 196, 170, 255}};
        DrawDog(b, S, t, coats[b.id % 3], false, true);
        break;
    }
    case IS_GBEETLE: { // the Goliath beetle: a flat stone shell carved with the tribe's idols, six slow legs
        float fy = b.anchor.y, top = fy - 44;
        Color stone{120, 116, 104, 255}, dark{70, 66, 60, 255};
        for (int k = 0; k < 6; k++) { float lx = x - 26 + k * 10.4f, lift = fabsf(b.vel.x) > 2 ? std::max(0.0f, sinf(b.phase * 3 + k * 2.1f)) * 3 : 0; DrawLineEx({lx, fy - 16}, {lx + f * 4, fy - lift}, 3, FAUNA_INK); DrawLineEx({lx, fy - 16}, {lx + f * 4, fy - lift}, 2, dark); }
        DrawEllipse((int)x, (int)(fy - 26), 40, 16, FAUNA_INK); DrawEllipse((int)x, (int)(fy - 26), 38, 14, stone);
        DrawRectangle((int)x - 36, (int)top, 72, 8, stone); DrawRectangle((int)x - 36, (int)top, 72, 2, Tone(stone, 0.3f)); // the flat top
        for (int k = 0; k < 3; k++) { float ix = x - 20 + k * 20; DrawRectangle((int)ix - 3, (int)top - 12, 6, 12, Color{150, 110, 70, 255}); DrawRectangle((int)ix - 2, (int)top - 10, 1, 1, FAUNA_INK); DrawRectangle((int)ix + 1, (int)top - 10, 1, 1, FAUNA_INK); } // little carved idols
        for (int k = 0; k < 4; k++) DrawLineEx({x - 30 + k * 18, fy - 34}, {x - 24 + k * 18, fy - 20}, 1, dark); // glyphs
        DrawEllipse((int)(x + f * 40), (int)(fy - 20), 8, 6, dark); DrawLineEx({x + f * 46, fy - 22}, {x + f * 54, fy - 30}, 2, dark); // the horn
        break;
    }
    case IS_MSTALKER: { // the Mangrove Stalker: a gnarled, root-coloured reptile - nearly invisible until it moves
        float camo = b.act == BeastAct::Ambush ? 0.75f : 0.0f;
        Color c = Fade(Color{86, 96, 60, 255}, 1 - camo * 0.6f);
        Vector2 a = b.anchor, jaw = b.territory;
        DrawEllipse((int)a.x, (int)a.y + 2, 18, 6, Fade(FAUNA_INK, 1 - camo * 0.6f)); DrawEllipse((int)a.x, (int)a.y + 2, 17, 5, c);
        for (int k = 0; k < 5; k++) DrawRectangle((int)(a.x - 14 + k * 6), (int)a.y - 3, 3, 2, Fade(Color{60, 70, 40, 255}, 1 - camo * 0.6f)); // scutes
        bool open = b.act == BeastAct::Coil || b.act == BeastAct::Strike;
        DrawLineEx(a, jaw, 7, Fade(FAUNA_INK, 1 - camo * 0.6f)); DrawLineEx(a, jaw, 5, c);
        if (open) { DrawTri(jaw, {jaw.x + b.facing * 12, jaw.y - 7}, {jaw.x + b.facing * 12, jaw.y + 5}, Color{150, 40, 50, 255}); for (int k = 0; k < 3; k++) DrawRectangle((int)(jaw.x + b.facing * (3 + k * 3)), (int)jaw.y - 4, 1, 2, WHITE); }
        DrawCircle((int)(a.x + b.facing * 8), (int)a.y - 3, 1.5f, open ? Color{255, 200, 60, 255} : Fade(Color{220, 200, 80, 255}, 0.5f)); // its eye, the giveaway
        if (b.stunT > 0) for (int k = 0; k < 3; k++) DrawCircle((int)(jaw.x + cosf(t * 5 + k * 2) * 8), (int)(jaw.y - 8), 1.2f, Color{170, 240, 120, 255}); // paralysed
        break;
    }
    case IS_CENTIPEDE: { // the Totem-Centipede: segments carved like a totem pole; while it wakes, the tribe's drums
        if (b.act == BeastAct::Coil) {
            for (int k = 0; k < 5; k++) DrawRectangle((int)(b.anchor.x - 24 + k * 12 + sinf(t * 60 + k) * 2), (int)(b.anchor.y - 3), 6, 3, Color{180, 150, 100, 255});
            for (int d = -1; d <= 1; d += 2) { float beat = fmodf(t * 3, 1.0f); DrawCircleLines((int)(b.anchor.x + d * 7 * (float)T), (int)(b.anchor.y - 5 * (float)T), 10 + beat * 30, Fade(Color{255, 200, 120, 255}, 1 - beat)); } // drums from the villages
            break;
        }
        if (b.act != BeastAct::Strike && b.act != BeastAct::Wander) break;
        Vector2 a = b.anchor, h = b.territory;
        for (int k = 0; k < 7; k++) {
            Vector2 q{a.x + (h.x - a.x) * k / 6.0f + sinf(t * 20 + k) * 2, a.y + (h.y - a.y) * k / 6.0f};
            DrawRectangle((int)q.x - 14, (int)q.y - 8, 28, 16, FAUNA_INK); DrawRectangle((int)q.x - 13, (int)q.y - 7, 26, 14, k % 2 ? Color{140, 70, 50, 255} : Color{180, 120, 60, 255});
            DrawRectangle((int)q.x - 6, (int)q.y - 3, 3, 3, FAUNA_INK); DrawRectangle((int)q.x + 3, (int)q.y - 3, 3, 3, FAUNA_INK); // a carved face on each segment
            DrawLineEx({q.x - 13, q.y}, {q.x - 22, q.y + 6}, 2, FAUNA_INK); DrawLineEx({q.x + 13, q.y}, {q.x + 22, q.y + 6}, 2, FAUNA_INK); // legs
        }
        DrawTri({h.x - 10, h.y - 6}, {h.x + 10, h.y - 6}, {h.x, h.y - 22}, Color{200, 60, 50, 255}); // mandibles
        break;
    }
    case IS_SERPENT: { // the Arch-Serpent: a colossal neck rising out of the ravine, horned, sea-green
        Vector2 a = b.anchor, h = b.territory;
        Color scale{50, 110, 100, 255}, belly{200, 190, 140, 255};
        Vector2 prev = {a.x, a.y + 40};
        for (int k = 1; k <= 12; k++) {
            float u = k / 12.0f;
            Vector2 q{a.x + (h.x - a.x) * u + sinf(u * PI) * 40 * (h.x > a.x ? -1 : 1), a.y + 40 + (h.y - a.y - 40) * u};
            float w = 30 - u * 12;
            DrawLineEx(prev, q, w + 4, FAUNA_INK); DrawLineEx(prev, q, w, scale); DrawLineEx({prev.x + 4, prev.y}, {q.x + 4, q.y}, w * 0.35f, belly);
            prev = q;
        }
        DrawEllipse((int)h.x, (int)h.y, 26, 16, FAUNA_INK); DrawEllipse((int)h.x, (int)h.y, 24, 14, scale);
        DrawTri({h.x - 14, h.y - 10}, {h.x - 8, h.y - 12}, {h.x - 20, h.y - 34}, Color{230, 220, 190, 255}); DrawTri({h.x + 14, h.y - 10}, {h.x + 8, h.y - 12}, {h.x + 20, h.y - 34}, Color{230, 220, 190, 255}); // horns
        bool strike = b.act == BeastAct::Strike || b.act == BeastAct::Coil;
        for (int e = -1; e <= 1; e += 2) DrawCircle((int)(h.x + e * 10), (int)h.y - 3, 3, strike ? Color{255, 220, 80, 255} : Color{200, 180, 90, 255});
        if (strike) DrawTri({h.x - 12, h.y + 6}, {h.x + 12, h.y + 6}, {h.x, h.y + 22}, Color{110, 20, 30, 255});
        break;
    }
    case IS_SKIPPER: { // a mud-skipper: bug-eyed, finned, skipping
        Color c{150, 130, 90, 255};
        DrawEllipse((int)x, (int)y, 5, 2.5f, c); DrawTri({x - f * 5, y}, {x - f * 9, y - 3}, {x - f * 9, y + 2}, c);
        DrawCircle((int)(x + f * 3), (int)y - 3, 1.4f, Color{240, 240, 220, 255}); DrawRectangle((int)(x + f * 3), (int)y - 3, 1, 1, FAUNA_INK);
        break;
    }
    case IS_FIREFLY: { float gl = 0.5f + 0.5f * sinf(t * 6 + b.phase * 3); DrawCircle((int)x, (int)y, 4, Fade(Color{255, 240, 120, 255}, 0.25f * gl)); DrawRectangle((int)x, (int)y, 2, 2, Fade(Color{255, 250, 170, 255}, 0.5f + 0.5f * gl)); break; }
    case IS_DRUM: { // a tribal drum-fungus: a taut cap, painted by the tribe
        float sq = b.flashT > 0 ? 4.0f : 0.0f;
        DrawRectangle((int)x - 3, (int)(y - 10 + sq), 6, (int)(10 - sq), Color{210, 200, 170, 255});
        DrawEllipse((int)x, (int)(y - 11 + sq), 13, 5 - sq * 0.5f, FAUNA_INK); DrawEllipse((int)x, (int)(y - 11 + sq), 12, 4 - sq * 0.5f, Color{190, 90, 60, 255});
        for (int k = -1; k <= 1; k++) DrawRectangle((int)(x + k * 6) - 1, (int)(y - 12 + sq), 2, 2, Color{240, 220, 160, 255});
        break;
    }
    case IS_DARTVINE: { // a poison-dart vine hanging from a bough, beaded with neurotoxin
        float len = b.special2;
        Vector2 prev{x, y};
        for (int k = 1; k <= 6; k++) { Vector2 q{x + sinf(t * 1.3f + k * 0.8f) * 2 * k / 6.0f, y + len * k / 6.0f}; DrawLineEx(prev, q, 3, FAUNA_INK); DrawLineEx(prev, q, 2, Color{60, 120, 50, 255}); if (k % 2 == 0) DrawCircle((int)q.x + 2, (int)q.y, 1.4f, Color{200, 80, 220, 255}); prev = q; }
        if (fmodf(t + b.phase, 2.0f) < 0.3f) DrawCircle((int)x, (int)(y + len + fmodf(t * 40, 20)), 1, Color{220, 120, 240, 255}); // a drip
        break;
    }
    case IS_RAZOR: { for (int k = 0; k < 6; k++) { float rx = x - 14 + k * 5.5f, h = 6 + (k * 7 % 4) * 2; DrawTri({rx - 2, y}, {rx + 2, y}, {rx + (k % 2 ? 3 : -3), y - h}, Color{110, 80, 50, 255}); DrawLineEx({rx, y - 1}, {rx + (k % 2 ? 3 : -3), y - h}, 1, Color{220, 200, 170, 255}); } break; }
    case IS_BLOOM: { // Idol's bloom: a rare pale flower, misting when brushed
        DrawLineEx({x, y}, {x, y - 10}, 1.5f, Color{60, 120, 60, 255});
        for (int k = 0; k < 5; k++) { float an = k * 1.256f + t * 0.3f; DrawEllipse((int)(x + cosf(an) * 4), (int)(y - 12 + sinf(an) * 3), 3, 2, Color{250, 220, 240, 255}); }
        DrawCircle((int)x, (int)y - 12, 2, Color{255, 230, 120, 255});
        if (b.flashT > 0) DrawCircle((int)x, (int)y - 12, 16 * (1 - b.flashT), Fade(Color{255, 230, 250, 255}, b.flashT));
        break;
    }
    case IS_SPONGE: { // a mangrove root-sponge: a springy pad in a cradle of roots
        float sq = b.flashT > 0 ? 5.0f : 0.0f;
        for (int k = -2; k <= 2; k++) DrawLineEx({x + k * 5, y}, {x + k * 7, y - 6}, 2, Color{100, 70, 40, 255});
        DrawEllipse((int)x, (int)(y - 8 + sq), 14 + sq, 5 - sq * 0.6f, FAUNA_INK); DrawEllipse((int)x, (int)(y - 8 + sq), 13 + sq, 4 - sq * 0.6f, Color{200, 170, 90, 255});
        for (int k = 0; k < 5; k++) DrawRectangle((int)(x - 9 + k * 4), (int)(y - 9 + sq), 1, 1, Color{140, 110, 60, 255}); // pores
        break;
    }
    default: DrawCircle((int)x, (int)y, 6, Color{200, 200, 200, 255}); break;
    }
    if (b.pers.abnormal == Abnormal::RabidEnraged) DrawCircle((int)(x + f * 6), (int)(y - 6), 1.4f, Color{255, 40, 30, 255});
    if (b.pers.abnormal == Abnormal::SymbioticCompanion) DrawCircle((int)x, (int)y, 18, Fade(Color{255, 240, 170, 255}, 0.10f + 0.05f * sinf(t * 3)));
    (void)p; (void)fleeing;
}
// A burrow under the roots of the jungle floor, fringed with fern.
void DrawIslandDen(const PlatformState& p, const Den& d, float t) {
    const BeastWorld& W = p.fauna;
    float x = d.pos.x, y = d.pos.y;
    DrawEllipse((int)x, (int)y + 2, 11, 6, FAUNA_INK);
    DrawEllipse((int)x, (int)y + 2, 9.5f, 4.6f, Color{26, 18, 12, 255});
    for (int k = 0; k < 3; k++) DrawLineEx({x - 10 + k * 8.0f, y - 1}, {x - 8 + k * 8.0f + sinf(k * 2.0f) * 3, y + 6}, 1.6f, Color{96, 70, 46, 255}); // roots
    for (int k = -1; k <= 1; k += 2) for (int j = 0; j < 4; j++) { float a = -PI / 2 + k * (0.5f + j * 0.28f) + sinf(t * 1.2f + j) * 0.05f; DrawLineEx({x + k * 9.0f, y}, {x + k * 9.0f + cosf(a) * 9, y + sinf(a) * 9}, 2.0f, Color{60, 130, 60, 255}); } // ferns
    int inside = 0;
    for (const auto& b : W.beasts) if (b.life == BeastLife::Alive && b.hidden && fabsf(b.pos.x - x) < 4) inside++;
    if (inside && fmodf(t * 0.8f + x * 0.013f, 3.5f) < 2.6f) { DrawCircle((int)x - 2, (int)y + 2, 0.9f, Color{250, 220, 120, 255}); DrawCircle((int)x + 2, (int)y + 2, 0.9f, Color{250, 220, 120, 255}); }
}
// A many-legged crawler (centipede) laid along its spine, legs rippling down the body in a wave.
void DrawCrawler(const Beast& b, float t, int segs, float r, Color c, Color leg) {
    Vector2 pts[SPINE];
    int n = std::min(segs, SPINE);
    for (int k = 0; k < n; k++) pts[k] = b.spine[k];
    for (int k = 0; k < n; k++) { // legs first, each pair a beat behind the one before
        Vector2 a = pts[std::max(0, k - 1)], z = pts[std::min(n - 1, k + 1)];
        Vector2 d{z.x - a.x, z.y - a.y}; float l = sqrtf(d.x * d.x + d.y * d.y); Vector2 nrm = l > 0.01f ? Vector2{-d.y / l, d.x / l} : Vector2{0, 1};
        float wave = sinf(b.phase * 10 - k * 1.1f) * 2.5f;
        for (int side = -1; side <= 1; side += 2) DrawLineEx(pts[k], {pts[k].x + nrm.x * side * (r + 3) + wave * d.x / std::max(1.0f, l), pts[k].y + nrm.y * side * (r + 3) + wave * d.y / std::max(1.0f, l)}, 1.0f, leg);
    }
    DrawSpineBody(pts, n, r, r * 0.7f, 0.6f, b.phase, c, Tone(c, -0.25f));
    for (int k = 1; k < n; k++) DrawCircleV(pts[k], 0.8f, Tone(c, -0.45f)); // segment joints
    DrawCircleV(pts[0], r * 0.5f, Tone(c, -0.35f));
}

void DrawCaveBeast(const PlatformState& p, const Beast& b, float t) {
    const SpeciesDef& S = BeastSpecies(PL_CAVE, b.species);
    float x = b.pos.x, y = b.pos.y, f = b.facing, s = b.scale;
    bool dead = b.life == BeastLife::Corpse;
    if (dead) {
        float r = S.radius * s;
        DrawEllipse((int)x, (int)y + 1, r * 1.1f + 1, r * 0.5f + 1, FAUNA_INK);
        DrawEllipse((int)x, (int)y + 1, r * 1.1f, r * 0.5f, Color{120, 116, 120, 255});
        return;
    }
    switch (b.species) {
    case CS_CUSK: { // a blind cave cusk-eel: pale, eel-long, a ribbon fin; resting, tucked head-up in a ceiling crevice
        Color c{206, 196, 200, 255}, fin{230, 220, 226, 200};
        bool resting = (b.act == BeastAct::Rest || b.act == BeastAct::Idle) && sqrtf(b.vel.x * b.vel.x + b.vel.y * b.vel.y) < 15;
        Vector2 pts[6];
        for (int k = 0; k < 6; k++) pts[k] = resting ? Vector2{x + sinf(t * 1.5f + k) * 1.0f, y - 6 + k * 3.0f} : b.spine[std::min(k, SPINE - 1)];
        DrawSpineBody(pts, 6, 2.6f, 1.0f, resting ? 0.5f : 2.5f, b.phase, c, fin);
        Vector2 hd = pts[0];
        DrawCircle((int)(hd.x + (resting ? 0 : f * 2)), (int)hd.y - 1, 0.7f, Color{140, 120, 130, 255}); // vestigial eye
        for (int k = 1; k < 5; k += 2) DrawRectangle((int)pts[k].x, (int)pts[k].y - 1, 1, 1, Color{150, 200, 230, 255}); // the lateral line, faintly lit
        break;
    }
    case CS_JELLY: {
        float glow = b.flashT > 0 ? 1.0f : 0.35f + 0.1f * sinf(t * 2 + b.phase);
        Color c{120, 230, 220, 255};
        if (b.flashT > 0) { DrawCircle((int)x, (int)y, 34, Fade(c, 0.12f)); DrawCircle((int)x, (int)y, 18, Fade(c, 0.2f)); }
        DrawCircle((int)x, (int)y, 9, Fade(c, 0.12f * glow + 0.05f));
        for (int k = 0; k < 5; k++) { // trailing tentacles, streaming behind as it drifts
            float a = sinf(t * 2.2f + k + b.phase) * 3;
            DrawLineEx({x - 5.0f + k * 2.5f, y + 3}, {x - 5.0f + k * 2.5f + a - b.vel.x * 0.05f, y + 13 + (k % 2) * 3}, 1.0f, Fade(c, 0.35f + 0.4f * glow));
        }
        float pulse = 1 + 0.08f * sinf(t * 3 + b.phase);
        DrawEllipse((int)x, (int)y, 7.0f * pulse, 5.0f / pulse, Fade(c, 0.35f + 0.45f * glow));
        DrawEllipse((int)x, (int)y - 1, 4.0f * pulse, 2.4f / pulse, Fade(WHITE, 0.3f + 0.5f * glow));
        break;
    }
    case CS_OLM: {
        Color c{226, 214, 206, 255}, gill{210, 120, 130, 255};
        bool strike = b.act == BeastAct::Strike;
        DrawBeastLegs(b, S, false, 1.8f, c, 0.9f, 0.9f);
        DrawBeastTail(b, {x - f * 8, y}, -0.05f, 2.0f, 3.0f, 0.8f, c, 7);
        DrawEllipse((int)x, (int)y - 1, 10 * s, 4.5f * s, FAUNA_INK); DrawEllipse((int)x, (int)y - 1, 9 * s, 3.6f * s, c);
        Vector2 head{x + f * 10, y - 2};
        DrawEllipse((int)head.x, (int)head.y, 5, 3.6f, FAUNA_INK); DrawEllipse((int)head.x, (int)head.y, 4.2f, 2.8f, c);
        for (int k = 0; k < 3; k++) DrawLineEx({head.x - f * 3, head.y - 1}, {head.x - f * (6 + k), head.y - 5 + k * 2 + sinf(t * 3 + k) * 0.8f}, 1.3f, gill); // feathery gills
        DrawCircle((int)(head.x + f * 2), (int)head.y - 1, 0.6f, Color{150, 140, 150, 255}); // vestigial eyes
        if (strike || b.act == BeastAct::Coil) DrawLineEx({head.x + f * 4, head.y + 1}, {head.x + f * (strike ? 18 : 6), head.y + 2}, 1.4f, Color{220, 110, 120, 255}); // the tongue, shot out
        DrawBeastLegs(b, S, true, 1.8f, c, 0.9f, 0.9f);
        break;
    }
    case CS_ISOPOD: { // a silt isopod: banded plates, feathery legs; cornered, it kicks up a blinding cloud of silt
        Color shell{150, 140, 128, 255};
        for (int k = 0; k < 4; k++) for (int side = 0; side < 2; side++) {
            float ph = b.phase * 12 + k * 2 + side * 3.1f, lift = fabsf(b.vel.x) > 4 ? std::max(0.0f, sinf(ph)) * 1.5f : 0;
            DrawLineEx({x - 4 + k * 2.6f, y}, {x - 6 + k * 3.4f + (side ? 1 : -1), y + 5 - lift}, 1.0f, FAUNA_INK);
        }
        DrawEllipse((int)x, (int)y - 1, 7, 4.5f, FAUNA_INK); DrawEllipse((int)x, (int)y - 1, 6, 3.6f, shell);
        for (int k = 0; k < 4; k++) DrawLineEx({x - 5 + k * 3.0f, y - 4}, {x - 5 + k * 3.0f, y + 1}, 1, Tone(shell, -0.3f)); // the plates
        DrawLineEx({x + f * 6, y - 2}, {x + f * 11, y - 7 + sinf(t * 4) * 1.5f}, 1, shell); // antennae
        if (b.cooldown > 6) for (int k = 0; k < 6; k++) DrawCircle((int)(x + sinf(t * 3 + k) * 8), (int)(y - 8 - k * 2), 1.4f, Fade(Color{150, 140, 120, 255}, 0.6f));
        break;
    }
    case CS_LEECH: {
        Color c{110, 36, 50, 255};
        bool hanging = b.special == 0, dropping = b.special == 1;
        float len = hanging ? 7 + sinf(t * 2 + b.phase) * 2 : 5;
        if (hanging) { // dangling, questing for warmth below
            DrawLineEx({x, y - 6}, {x + sinf(t * 1.5f + b.phase) * 2, y - 6 + len}, 5, FAUNA_INK);
            DrawLineEx({x, y - 6}, {x + sinf(t * 1.5f + b.phase) * 2, y - 6 + len}, 3.4f, c);
            DrawCircle((int)(x + sinf(t * 1.5f + b.phase) * 2), (int)(y - 6 + len), 2.4f, Tone(c, 0.2f));
        } else {
            DrawEllipse((int)x, (int)y, dropping ? 3.5f : 6, dropping ? 6 : 3, FAUNA_INK);
            DrawEllipse((int)x, (int)y, dropping ? 2.6f : 5, dropping ? 5 : 2.2f, c);
            if (dropping) DrawCircle((int)x, (int)y + 5, 2.0f, Color{200, 80, 90, 255}); // the sucker, first
        }
        break;
    }
    case CS_WORM: {
        bool in = b.act == BeastAct::Hide;
        float out = in ? std::min(1.0f, b.actT / 1.2f) * 0.2f : std::min(1.0f, b.actT * 0.8f + 0.2f);
        DrawRectangle((int)x - 4, (int)y - 16, 8, 16, FAUNA_INK);
        DrawRectangle((int)x - 3, (int)y - 16, 6, 16, Color{220, 214, 196, 255}); // the chalky tube
        if (out > 0.1f) {
            Color plume{220, 50, 60, 255};
            for (int k = -3; k <= 3; k++) {
                float a = k * 0.22f + sinf(t * 1.6f + k) * 0.08f, len = 14 * out;
                DrawLineEx({x, y - 16}, {x + sinf(a) * len, y - 16 - cosf(a) * len}, 2.2f, plume);
            }
            DrawCircle((int)x, (int)y - 16, 3 * out + 1, Tone(plume, -0.2f));
        }
        break;
    }
    case CS_GSHRIMP: { // glow-shrimp: a speck of blue-green light with legs, never still
        Color c{120, 240, 210, 255};
        DrawCircle((int)x, (int)y, 5, Fade(c, 0.18f));
        float kick = sinf(t * 18 + b.phase * 4) * 1.2f;
        DrawEllipse((int)x, (int)y, 3.2f, 1.6f, c);
        DrawLineEx({x - f * 3, y}, {x - f * 5, y + 2 + kick}, 1, c); // the tail fan
        DrawLineEx({x + f * 2, y - 1}, {x + f * 6, y - 3 - kick}, 1, Fade(c, 0.7f)); // feelers
        break;
    }
    case CS_LOACH: { // a blind loach: pale, whiskered, sluggish - until the floor shudders
        Color c{190, 176, 160, 255};
        float wig = sinf(t * (b.act == BeastAct::Flee ? 16.0f : 5.0f) + b.phase) * 1.5f;
        DrawEllipse((int)x, (int)y, 6, 2.4f, FAUNA_INK); DrawEllipse((int)x, (int)y, 5, 1.8f, c);
        DrawTri({x - f * 5, y}, {x - f * 9, y - 2 + wig}, {x - f * 9, y + 2 + wig}, c);
        for (int k = -1; k <= 1; k += 2) DrawLineEx({x + f * 5, y}, {x + f * 8, y + k * 2 + 1}, 1, Tone(c, -0.2f)); // barbels
        break;
    }
    case CS_CTORTOISE: { // the Crystal-Shelled Tortoise: a glowing crystal dome on tall legs - there's room to walk under it
        float floorY = b.anchor.y, top = floorY - 72, under = floorY - 34;
        Color skin{110, 120, 110, 255}, crystal{150, 220, 240, 255};
        float step = sinf(b.phase * 2) * (fabsf(b.vel.x) > 2 ? 3.0f : 0.0f);
        for (int k = 0; k < 4; k++) { // four pillar legs
            float lx = x + (k < 2 ? 30 : -30) + (k % 2 ? 5 : -5), lift = k % 2 == 0 ? step : -step;
            DrawRectangle((int)lx - 5, (int)(under - 2), 10, (int)(floorY - under + 2 - std::max(0.0f, lift)), FAUNA_INK);
            DrawRectangle((int)lx - 4, (int)(under - 2), 8, (int)(floorY - under + 1 - std::max(0.0f, lift)), skin);
        }
        DrawEllipse((int)(x + f * 48), (int)(under - 4), 10, 7, FAUNA_INK); DrawEllipse((int)(x + f * 48), (int)(under - 4), 9, 6, skin); // the head
        DrawCircle((int)(x + f * 52), (int)(under - 6), 1.4f, FAUNA_INK);
        DrawEllipse((int)x, (int)((top + under) / 2), 44, (under - top) / 2 + 2, FAUNA_INK);
        DrawEllipse((int)x, (int)((top + under) / 2), 42, (under - top) / 2, Color{40, 60, 70, 255});
        for (int k = 0; k < 7; k++) { // the crystals, faceted, pulsing faintly
            float cx = x - 30 + k * 10, ch = 10 + (k * 7 % 5) * 3, gl = 0.6f + 0.4f * sinf(t * 1.5f + k);
            DrawTri({cx - 5, top + 14}, {cx + 5, top + 14}, {cx, top + 14 - ch}, Fade(crystal, gl));
            DrawTri({cx - 2, top + 14}, {cx + 2, top + 14}, {cx, top + 16 - ch}, Fade(WHITE, 0.5f * gl));
        }
        DrawRectangle((int)x - 42, (int)(under - 5), 84, 5, Tone(skin, -0.3f)); // the shell's underside rim
        break;
    }
    case CS_STALKER: { // the Echo-Stalker: a long, pale amphibian with no eyes and a head full of ears; its tail flicks stones
        Color c{176, 170, 180, 255};
        bool hunting = b.act == BeastAct::Hunt || b.act == BeastAct::Coil || b.act == BeastAct::Strike;
        DrawBeastLegs(b, S, false, 2.4f, c, 0.9f, 0.9f);
        DrawBeastTail(b, {x - f * 10, y - 2}, 0.3f, hunting ? 1.5f : 0.5f, 3.4f, 1.0f, c, 7);
        DrawEllipse((int)x, (int)y - 2, 12 * s, 5 * s, FAUNA_INK); DrawEllipse((int)x, (int)y - 2, 11 * s, 4 * s, c);
        Vector2 head{x + f * 12, y - 4};
        DrawEllipse((int)head.x, (int)head.y, 6, 4, FAUNA_INK); DrawEllipse((int)head.x, (int)head.y, 5, 3.2f, c);
        for (int k = 0; k < 3; k++) DrawCircleLines((int)head.x, (int)head.y, 6 + k * 3 + fmodf(t * 8, 3), Fade(Color{200, 220, 240, 255}, b.special2 > 0 ? 0.0f : 0.12f)); // its sonar clicks
        if (b.act == BeastAct::Coil || b.act == BeastAct::Strike) DrawTri({head.x + f * 3, head.y}, {head.x + f * 10, head.y - 3}, {head.x + f * 10, head.y + 3}, Color{90, 20, 30, 255});
        if (b.special2 > 0) for (int k = 0; k < 3; k++) DrawCircle((int)(head.x + cosf(t * 5 + k * 2) * 7), (int)(head.y - 8 + sinf(t * 5 + k * 2) * 2), 1, Color{220, 220, 255, 255}); // deafened, dazed
        DrawBeastLegs(b, S, true, 2.4f, c, 0.9f, 0.9f);
        break;
    }
    case CS_TREMOR: { // the Tremor Worm: only ever seen bursting up out of the floor
        if (b.act == BeastAct::Coil) { for (int k = 0; k < 5; k++) DrawRectangle((int)(b.anchor.x - 24 + k * 12 + sinf(t * 60 + k) * 2), (int)(b.anchor.y - 3), 6, 3, Color{110, 100, 96, 255}); break; } // the floor shudders
        if (b.act != BeastAct::Strike && b.act != BeastAct::Wander) break;
        Vector2 a = b.anchor, h = b.territory;
        DrawLineEx(a, h, 30, FAUNA_INK);
        DrawLineEx(a, h, 26, Color{150, 110, 120, 255});
        for (int k = 1; k < 6; k++) { Vector2 q{a.x + (h.x - a.x) * k / 6.0f, a.y + (h.y - a.y) * k / 6.0f}; DrawLineEx({q.x - 13, q.y}, {q.x + 13, q.y}, 2, Color{110, 76, 86, 255}); } // segments
        DrawCircle((int)h.x, (int)h.y, 14, FAUNA_INK);
        DrawCircle((int)h.x, (int)h.y, 11, Color{80, 20, 30, 255}); // the round, toothed maw
        for (int k = 0; k < 10; k++) { float an = k * 0.628f; DrawTri({h.x + cosf(an) * 11, h.y + sinf(an) * 11}, {h.x + cosf(an + 0.2f) * 11, h.y + sinf(an + 0.2f) * 11}, {h.x + cosf(an + 0.1f) * 6, h.y + sinf(an + 0.1f) * 6}, WHITE); }
        break;
    }
    case CS_ARACHNID: { // the Abyssal Arachnid: a vast shape clinging to the roof - you see its eyes before anything else
        float r = 20;
        for (int k = 0; k < 8; k++) { // legs splayed across the rock
            float an = (k < 4 ? -0.3f - k * 0.35f : PI + 0.3f + (k - 4) * 0.35f) + sinf(t * 1.5f + k) * 0.05f;
            Vector2 knee{x + cosf(an) * r * 1.8f, y - 14 - fabsf(sinf(an)) * 6}, foot{x + cosf(an) * r * 3.0f, y - 8};
            DrawLineEx({x, y}, knee, 3, FAUNA_INK); DrawLineEx(knee, foot, 2.4f, FAUNA_INK);
            DrawLineEx({x, y}, knee, 1.8f, Color{40, 36, 50, 255}); DrawLineEx(knee, foot, 1.4f, Color{40, 36, 50, 255});
        }
        DrawEllipse((int)x, (int)y, r + 1, r * 0.7f + 1, FAUNA_INK); DrawEllipse((int)x, (int)y, r, r * 0.7f, Color{30, 26, 40, 255});
        for (int k = 0; k < 6; k++) DrawCircle((int)(x - 8 + k * 3.2f), (int)(y + 4 + (k % 2) * 2), 1.4f, Color{255, 80, 70, 255}); // a cluster of red eyes
        break;
    }
    case CS_LSHROOM: { // a lantern-shroom: the only steady light down here
        float gl = 0.8f + 0.2f * sinf(t * 1.2f + b.phase);
        DrawCircle((int)x, (int)y - 10, 26, Fade(Color{150, 255, 200, 255}, 0.08f * gl));
        DrawRectangle((int)x - 1, (int)y - 9, 3, 9, Color{200, 220, 200, 255});
        DrawEllipse((int)x, (int)y - 10, 7, 4, FAUNA_INK); DrawEllipse((int)x, (int)y - 10, 6, 3, Fade(Color{150, 255, 200, 255}, gl));
        break;
    }
    case CS_LICHEN: { // acid-lichen crusting the wall: a sickly, bright green - never touch it
        for (int k = 0; k < 9; k++) { float hx = sinf(k * 2.3f) * 5, hy = -12 + k * 3.0f; DrawCircle((int)(x + hx), (int)(y + hy), 2.6f, k % 2 ? Color{190, 230, 60, 255} : Color{140, 190, 40, 255}); }
        if (fmodf(t * 2 + b.phase, 3) < 0.5f) DrawCircle((int)x, (int)(y - 14 - fmodf(t * 20, 10)), 1, Color{220, 255, 120, 255}); // a fizzing bead
        break;
    }
    case CS_VSPORE: { // a drum of a fungus, taut-skinned
        if (b.act == BeastAct::Drift) { DrawEllipse((int)x, (int)y - 2, 8, 2, Color{120, 100, 130, 255}); break; }
        DrawEllipse((int)x, (int)y - 7, 9, 7, FAUNA_INK); DrawEllipse((int)x, (int)y - 7, 8, 6, Color{150, 120, 170, 255});
        DrawEllipse((int)x, (int)y - 11, 6, 2, Color{200, 180, 220, 255});
        break;
    }
    case CS_CABBAGE: { // dense cave-cabbage: layered leaves low on the floor
        for (int k = 0; k < 7; k++) { float hx = -15 + k * 5, hh = 9 + (k % 3) * 3 + (b.flashT > 0 ? sinf(t * 30 + k) * 2 : 0); DrawEllipse((int)(x + hx), (int)(y - hh / 2), 5, hh / 2 + 1, FAUNA_INK); DrawEllipse((int)(x + hx), (int)(y - hh / 2), 4, hh / 2, k % 2 ? Color{70, 110, 90, 255} : Color{56, 92, 76, 255}); }
        break;
    }
    case CS_NROOT: { // nerve-root: a knot of barbed filaments
        for (int k = 0; k < 6; k++) { float an = -PI / 2 + (k - 2.5f) * 0.35f + sinf(t * 2 + k) * 0.1f; Vector2 tip{x + cosf(an) * 12, y + sinf(an) * 12}; DrawLineEx({x, y}, tip, 1.4f, Color{200, 120, 160, 255}); DrawCircleV(tip, 1.2f, Color{255, 170, 210, 255}); }
        break;
    }
    default: DrawCircle((int)x, (int)y, 5, Color{200, 200, 200, 255}); break;
    }
    if (b.pers.abnormal == Abnormal::SymbioticCompanion) DrawCircle((int)x, (int)y, 18, Fade(Color{190, 255, 230, 255}, 0.10f + 0.05f * sinf(t * 3)));
    (void)p;
}

void DrawPipesBeast(const PlatformState& p, const Beast& b, float t) {
    const SpeciesDef& S = BeastSpecies(PL_PIPES, b.species);
    float x = b.pos.x, y = b.pos.y, f = b.facing, s = b.scale;
    if (b.life == BeastLife::Corpse) { DrawEllipse((int)x, (int)y + 1, S.radius + 1, S.radius * 0.5f + 1, FAUNA_INK); DrawEllipse((int)x, (int)y + 1, S.radius, S.radius * 0.5f, Color{110, 104, 98, 255}); return; }
    switch (b.species) {
    case PP_MOTH: {
        Color c{214, 206, 184, 230};
        float w = sinf(t * 20 + b.phase * 4) * 3;
        DrawTri({x, y}, {x - 5, y - 2 - w}, {x - 3, y + 2}, c); DrawTri({x, y}, {x + 5, y - 2 - w}, {x + 3, y + 2}, c);
        DrawRectangle((int)x - 1, (int)y - 2, 2, 5, Color{120, 110, 90, 255});
        break;
    }
    case PP_SPIDER: {
        for (int r = 5; r <= 13; r += 4) DrawCircleLines((int)x, (int)y, (float)r, Fade(Color{200, 214, 230, 255}, 0.3f));
        for (int k = 0; k < 6; k++) { float a = k * PI / 3; DrawLineEx({x, y}, {x + cosf(a) * 13, y + sinf(a) * 13}, 0.6f, Fade(Color{200, 214, 230, 255}, 0.4f)); }
        for (int k = 0; k < 4; k++) for (int side = -1; side <= 1; side += 2) { Vector2 hip{x, y}, foot{x + side * (5 + k), y - 2 + k * 2.0f}; Vector2 kn = ik::Knee(hip, foot, 4, 4, -side); DrawLineEx(hip, kn, 1.0f, FAUNA_INK); DrawLineEx(kn, foot, 1.0f, FAUNA_INK); }
        DrawCircle((int)x, (int)y, 3, Color{60, 70, 90, 255}); DrawCircle((int)x, (int)y + 3, 2, Color{40, 46, 60, 255});
        if (b.act == BeastAct::Eat) DrawEllipse((int)x + 4, (int)y + 4, 2, 3, Color{220, 216, 200, 255});
        break;
    }
    case PP_CENTIPEDE: DrawCrawler(b, t, 7, 2.6f, Color{170, 96, 50, 255}, Color{110, 60, 30, 255}); break;
    case PP_RAT: {
        Color c{120, 110, 104, 255}, pink{210, 150, 150, 255};
        DrawBeastLegs(b, S, false, 1.6f, c, -0.6f, 0.8f);
        DrawBeastTail(b, {x - f * 6, y + 1}, 0.1f, 1.5f, 1.8f, 0.8f, pink, 6);
        DrawEllipse((int)x, (int)y, 8.5f * s, 5.2f * s, FAUNA_INK); DrawEllipse((int)x, (int)y, 7.5f * s, 4.3f * s, c);
        DrawTri({x + f * 5, y - 3}, {x + f * 5, y + 3}, {x + f * 12, y + 1}, c);
        DrawCircleV({x + f * 12, y + 1}, 1.0f, pink);
        DrawCircle((int)(x + f * 5), (int)y - 4, 2.4f, pink);
        DrawCircle((int)(x + f * 7), (int)y - 1, 0.9f, Color{200, 200, 210, 255}); // milky, blind eye
        for (int k = 0; k < 3; k++) DrawLineEx({x + f * 11, y}, {x + f * 16, y - 2 + k * 2.0f}, 0.6f, Color{220, 220, 220, 200}); // whiskers: how it sees
        DrawBeastLegs(b, S, true, 1.6f, c, -0.6f, 0.8f);
        break;
    }
    case PP_MITE: DrawCircle((int)x, (int)y, 2.2f, FAUNA_INK); DrawCircle((int)x, (int)y, 1.6f, Color{190, 90, 50, 255}); break;
    case PP_PILLBUG: {
        Color c{110, 116, 126, 255};
        if (b.act == BeastAct::Puffed) { // a rolling armoured ball
            DrawCircle((int)x, (int)y + 1, 5.2f, FAUNA_INK); DrawCircle((int)x, (int)y + 1, 4.4f, c);
            float r = b.pos.x * 0.2f;
            for (int k = 0; k < 3; k++) DrawLineEx({x + cosf(r + k) * 4, y + 1 + sinf(r + k) * 4}, {x - cosf(r + k) * 4, y + 1 - sinf(r + k) * 4}, 0.8f, Tone(c, -0.35f));
            break;
        }
        DrawEllipse((int)x, (int)y, 6, 3.6f, FAUNA_INK); DrawEllipse((int)x, (int)y, 5, 2.8f, c);
        for (int k = 0; k < 4; k++) DrawLine((int)(x - 4 + k * 2.5f), (int)y - 2, (int)(x - 4 + k * 2.5f), (int)y + 2, Tone(c, -0.35f));
        break;
    }
    case PP_MOUSE: {
        Color c{150, 124, 96, 255};
        DrawBeastLegs(b, S, false, 1.2f, c, -0.6f, 0.8f);
        DrawBeastTail(b, {x - f * 4, y}, 0.3f, 1.2f, 1.2f, 0.6f, Color{200, 160, 150, 255}, 5);
        DrawEllipse((int)x, (int)y, 5.5f, 3.8f, FAUNA_INK); DrawEllipse((int)x, (int)y, 4.6f, 3.0f, c);
        DrawCircle((int)(x + f * 4), (int)y - 1, 2.8f, c);
        DrawCircle((int)(x + f * 3), (int)y - 4, 2.0f, Color{210, 170, 160, 255}); // round ear
        DrawCircle((int)(x + f * 5), (int)y - 1, 0.8f, FAUNA_INK);
        DrawBeastLegs(b, S, true, 1.2f, c, -0.6f, 0.8f);
        break;
    }
    case PP_ROACH: {
        Color c{110, 64, 36, 255};
        for (int k = 0; k < 3; k++) { float ph = b.phase * 14 + k * 2; DrawLineEx({x - 2 + k * 2.0f, y}, {x - 4 + k * 3.0f + cosf(ph), y + 4}, 0.9f, FAUNA_INK); }
        DrawEllipse((int)x, (int)y - 1, 6, 3.2f, FAUNA_INK); DrawEllipse((int)x, (int)y - 1, 5, 2.5f, c);
        DrawLine((int)x, (int)y - 3, (int)x, (int)y + 1, Tone(c, -0.3f));
        for (int k = 0; k < 2; k++) DrawLineEx({x + f * 5, y - 2}, {x + f * (11 + k), y - 6 + k * 2 + sinf(t * 7 + k) * 1.5f}, 0.6f, c); // antennae, feeling
        break;
    }
    case PP_GLOW: {
        Color g{190, 250, 140, 255};
        if (b.flashT > 0) { DrawCircle((int)x, (int)y, 30, Fade(g, 0.16f)); DrawCircle((int)x, (int)y, 14, Fade(g, 0.3f)); }
        DrawEllipse((int)x, (int)y, 5, 3.4f, FAUNA_INK); DrawEllipse((int)x, (int)y, 4, 2.6f, Color{50, 60, 40, 255});
        DrawCircle((int)(x - f * 2), (int)y, 2.0f, Fade(g, b.flashT > 0 ? 1.0f : 0.4f + 0.2f * sinf(t * 2 + b.phase)));
        break;
    }
    case PP_CRICKET: {
        Color c{150, 130, 100, 255};
        bool air = !b.grounded;
        DrawEllipse((int)x, (int)y - 1, 5.5f, 3.2f, FAUNA_INK); DrawEllipse((int)x, (int)y - 1, 4.6f, 2.5f, c);
        Vector2 hip{x - f * 2, y - 1}, foot = air ? Vector2{x - f * 9, y + 4} : Vector2{x - f * 4, y + 4};
        Vector2 kn = ik::Knee(hip, foot, 6, 6, f);
        DrawLineEx(hip, kn, 1.6f, Tone(c, -0.2f)); DrawLineEx(kn, foot, 1.2f, Tone(c, -0.2f)); // the great jumping leg
        for (int k = 0; k < 2; k++) DrawLineEx({x + f * 4, y - 2}, {x + f * (12 + k * 3), y - 8 + k * 3}, 0.6f, c);
        break;
    }
    default: DrawCircle((int)x, (int)y, 4, Color{200, 200, 200, 255}); break;
    }
    (void)p;
}
// Cave dens: a crack in the rock, glistening; Pipes dens: a rust-hole gnawed through a duct plate.
void DrawCaveDen(const PlatformState& p, const Den& d, float t) {
    float x = d.pos.x, y = d.pos.y;
    DrawEllipse((int)x, (int)y + 2, 10, 5, FAUNA_INK); DrawEllipse((int)x, (int)y + 2, 8.5f, 3.8f, Color{8, 10, 14, 255});
    for (int k = 0; k < 3; k++) DrawCircle((int)(x - 7 + k * 7), (int)y - 1, 1.0f, Fade(Color{150, 190, 200, 255}, 0.3f + 0.2f * sinf(t * 2 + k)));
    int inside = 0;
    for (const auto& b : p.fauna.beasts) if (b.life == BeastLife::Alive && b.hidden && fabsf(b.pos.x - x) < 4) inside++;
    if (inside && fmodf(t * 0.8f + x * 0.013f, 3.5f) < 2.6f) { DrawCircle((int)x - 2, (int)y + 2, 0.9f, Color{200, 230, 255, 255}); DrawCircle((int)x + 2, (int)y + 2, 0.9f, Color{200, 230, 255, 255}); }
}
void DrawPipesDen(const PlatformState& p, const Den& d, float t) {
    float x = d.pos.x, y = d.pos.y;
    DrawEllipse((int)x, (int)y + 2, 8, 4.5f, FAUNA_INK); DrawEllipse((int)x, (int)y + 2, 6.5f, 3.2f, Color{14, 10, 8, 255});
    DrawRing({x, y + 2}, 7, 9, 180, 360, 10, Color{150, 70, 36, 220}); // rust flaking round the hole
    int inside = 0;
    for (const auto& b : p.fauna.beasts) if (b.life == BeastLife::Alive && b.hidden && fabsf(b.pos.x - x) < 4) inside++;
    if (inside && fmodf(t * 0.8f + x * 0.013f, 3.5f) < 2.6f) { DrawCircle((int)x - 2, (int)y + 2, 0.8f, Color{250, 220, 140, 255}); DrawCircle((int)x + 2, (int)y + 2, 0.8f, Color{250, 220, 140, 255}); }
}
// ---------------------------------------------------------------- the Weeds and Atlantis
// Drawn at base size; DrawFauna scales each species up the food chain (the sharks and mermen three to four times
// the diver). Swimmers are laid out along a heading taken from their own trailing spine, so they turn and bank.
Vector2 SwimBack(const Beast& b) { // unit vector from the head toward the tail
    Vector2 d{b.spine[2].x - b.pos.x, b.spine[2].y - b.pos.y};
    float l = sqrtf(d.x * d.x + d.y * d.y);
    return l > 2 ? Vector2{d.x / l, d.y / l} : Vector2{-b.facing, 0};
}
// A fish body along its heading: a lens of two triangles fans with an ink rim, a forked tail that beats.
void DrawFishBody(const Beast& b, float len, float girth, Color back, Color belly, float tailBeat, float tailSize) {
    Vector2 bk = SwimBack(b), n{-bk.y, bk.x};
    if (n.y > 0) n = {-n.x, -n.y}; // n points up (the back)
    Vector2 head{b.pos.x - bk.x * len * 0.5f, b.pos.y - bk.y * len * 0.5f}, tailRoot{b.pos.x + bk.x * len * 0.45f, b.pos.y + bk.y * len * 0.45f};
    Vector2 top{b.pos.x + n.x * girth, b.pos.y + n.y * girth}, bot{b.pos.x - n.x * girth * 0.9f, b.pos.y - n.y * girth * 0.9f};
    Vector2 sw{n.x * tailBeat, n.y * tailBeat};
    Vector2 t1{tailRoot.x + bk.x * tailSize + n.x * tailSize * 0.8f + sw.x, tailRoot.y + bk.y * tailSize + n.y * tailSize * 0.8f + sw.y};
    Vector2 t2{tailRoot.x + bk.x * tailSize - n.x * tailSize * 0.8f + sw.x, tailRoot.y + bk.y * tailSize - n.y * tailSize * 0.8f + sw.y};
    DrawTri(tailRoot, t1, t2, FAUNA_INK);
    DrawTri({tailRoot.x - bk.x * 2, tailRoot.y - bk.y * 2}, {t1.x - bk.x * 1.5f, t1.y - bk.y * 1.5f}, {t2.x - bk.x * 1.5f, t2.y - bk.y * 1.5f}, back);
    auto inflate = [&](Vector2 v, float k) { return Vector2{b.pos.x + (v.x - b.pos.x) * k, b.pos.y + (v.y - b.pos.y) * k}; };
    DrawTri(inflate(head, 1.08f), inflate(top, 1.15f), inflate(tailRoot, 1.08f), FAUNA_INK); DrawTri(inflate(head, 1.08f), inflate(bot, 1.15f), inflate(tailRoot, 1.08f), FAUNA_INK);
    DrawTri(head, top, tailRoot, back); DrawTri(head, bot, tailRoot, belly);
}

void DrawWeedsBeast(const PlatformState& p, const Beast& b, float t) {
    const SpeciesDef& S = BeastSpecies(PL_WEEDS, b.species);
    float x = b.pos.x, y = b.pos.y, f = b.facing;
    bool dead = b.life == BeastLife::Corpse, strike = b.act == BeastAct::Strike, coil = b.act == BeastAct::Coil, fleeing = b.act == BeastAct::Flee;
    float beat = sinf(b.phase * (fleeing ? 14.0f : 7.0f));
    if (dead && b.species != WS_FUNGUS) {
        float r = S.radius;
        DrawEllipse((int)x, (int)y + 1, r * 1.2f + 1, r * 0.5f + 1, FAUNA_INK);
        DrawEllipse((int)x, (int)y + 1, r * 1.2f, r * 0.5f, Color{130, 136, 140, 255});
        if (b.special2 > 0) for (int k = 0; k < 3; k++) { float a = t * 7 + k * 2.1f; DrawLineEx({x + cosf(a) * 4, y + sinf(a) * 2}, {x + cosf(a) * 11, y + sinf(a) * 5 - 3}, 1.2f, Fade(Color{180, 230, 255, 255}, 0.5f + 0.5f * sinf(t * 23 + k))); } // still crackling
        return;
    }
    switch (b.species) {
    case WS_PLANKTON: // a drifting cloud of green motes and spores
        for (int k = 0; k < 7; k++) { float a = k * 0.9f + t * 0.6f + b.phase, r = 3 + (k % 3) * 2.5f; DrawCircle((int)(x + cosf(a) * r), (int)(y + sinf(a * 1.3f) * r * 0.7f), 1.2f, Fade(Color{150, 220, 120, 255}, 0.55f + 0.3f * sinf(t * 3 + k))); }
        break;
    case WS_SEAHORSE: { // upright, curled tail, a long snout; the dorsal fin flutters; it fades into the kelp when still
        float a = 1.0f - 0.75f * std::clamp(b.special, 0.0f, 1.0f);
        Color c = Fade(Color{214, 170, 70, 255}, a), dk = Fade(Color{150, 100, 40, 255}, a);
        float bob = sinf(t * 2 + b.phase) * 1.2f;
        for (int k = 0; k < 5; k++) { float u = k / 4.0f; DrawCircle((int)(x + sinf(u * 2.6f) * 3 * f), (int)(y - 6 + k * 3 + bob), 3.2f - u * 1.6f, Fade(FAUNA_INK, a)); DrawCircle((int)(x + sinf(u * 2.6f) * 3 * f), (int)(y - 6 + k * 3 + bob), 2.4f - u * 1.4f, c); }
        DrawCircleLines((int)(x - f * 2), (int)(y + 9 + bob), 2.5f, dk);                                      // the curled tail
        DrawCircle((int)x, (int)(y - 8 + bob), 3.2f, c); DrawLineEx({x + f * 2, y - 8 + bob}, {x + f * 7, y - 7 + bob}, 1.6f, c); // head and snout
        DrawTri({x - f * 2, y - 3 + bob}, {x - f * (6 + sinf(t * 20) * 1.5f), y - 1 + bob}, {x - f * 2, y + 2 + bob}, Fade(Color{240, 210, 120, 255}, a * 0.8f)); // the fin
        DrawCircle((int)(x + f * 1), (int)(y - 9 + bob), 0.7f, Fade(FAUNA_INK, a));
        break;
    }
    case WS_BARRACUDA: {
        DrawFishBody(b, 22, 3.6f, Color{120, 140, 150, 255}, Color{210, 220, 222, 255}, beat * 2, 5);
        Vector2 bk = SwimBack(b), head{x - bk.x * 11, y - bk.y * 11};
        for (int k = 0; k < 4; k++) DrawRectangle((int)(x - bk.x * (k * 4 - 6)), (int)(y - bk.y * (k * 4 - 6)) - 2, 2, 3, Color{60, 70, 80, 255}); // bars along the flank
        if (strike || coil) DrawTri(head, {head.x - bk.x * 5, head.y - bk.y * 5 - 2}, {head.x - bk.x * 5, head.y - bk.y * 5 + 3}, Color{70, 20, 30, 255}); // the underslung jaw, gaping
        DrawCircle((int)(head.x + bk.x * 3), (int)(head.y + bk.y * 3 - 1), 1.1f, FAUNA_INK);
        break;
    }
    case WS_SHARK: { // a tiger shark: blunt head, stripes, a tall dorsal fin; jaws open when it comes in
        Vector2 bk = SwimBack(b), n{-bk.y, bk.x};
        if (n.y > 0) n = {-n.x, -n.y};
        DrawFishBody(b, 34, 6.5f, Color{110, 116, 108, 255}, Color{214, 210, 196, 255}, beat * 3, 9);
        Vector2 fin{x + n.x * 6, y + n.y * 6};
        DrawTri({fin.x - bk.x * 4, fin.y - bk.y * 4}, {fin.x + bk.x * 6, fin.y + bk.y * 6}, {fin.x + n.x * 8 + bk.x * 4, fin.y + n.y * 8 + bk.y * 4}, Color{90, 96, 88, 255}); // dorsal
        for (int k = 0; k < 5; k++) { Vector2 q{x + bk.x * (k * 4 - 8) + n.x * 3, y + bk.y * (k * 4 - 8) + n.y * 3}; DrawLineEx(q, {q.x + n.x * 2.5f + bk.x * 1.5f, q.y + n.y * 2.5f + bk.y * 1.5f}, 1.4f, Color{60, 62, 56, 255}); } // the tiger stripes
        Vector2 head{x - bk.x * 16, y - bk.y * 16};
        float gape = coil ? 0.7f : strike ? 1.0f : b.act == BeastAct::Eat ? 0.5f * fabsf(sinf(t * 10)) : 0.1f;
        DrawTri({head.x + bk.x * 5, head.y + bk.y * 5 - n.y * 0.5f}, {head.x - bk.x * 1 - n.x * 3 * gape, head.y - bk.y * 1 - n.y * 3 * gape + 1}, {head.x + bk.x * 2 - n.x * 5 * gape, head.y + bk.y * 2 - n.y * 5 * gape}, Color{90, 24, 34, 255});
        if (gape > 0.4f) for (int k = 0; k < 3; k++) DrawRectangle((int)(head.x + bk.x * (1 + k * 1.5f)), (int)(head.y + bk.y * (1 + k * 1.5f)), 1, 1, WHITE);
        DrawCircle((int)(head.x + bk.x * 4 + n.x * 1.5f), (int)(head.y + bk.y * 4 + n.y * 1.5f), 0.9f, FAUNA_INK);
        for (int k = 0; k < 3; k++) DrawLineEx({head.x + bk.x * (7 + k * 1.5f), head.y + bk.y * (7 + k * 1.5f) - 1.5f}, {head.x + bk.x * (7 + k * 1.5f), head.y + bk.y * (7 + k * 1.5f) + 1.5f}, 0.8f, Color{60, 62, 56, 255}); // gill slits
        break;
    }
    case WS_MERMAN: { // a man to the waist, a great fish tail below; a trident held in a two-bone arm
        Vector2 bk = SwimBack(b);
        Color skin{150, 176, 150, 255}, scale{56, 110, 110, 255}, hair{30, 50, 44, 255};
        // the tail, trailing and beating
        Vector2 prev{x + bk.x * 3, y + bk.y * 3 + 3};
        for (int k = 1; k <= 5; k++) {
            float u = k / 5.0f, w = sinf(b.phase * 6 - k * 0.9f) * 2.5f * u;
            Vector2 q{x + bk.x * (3 + k * 3.2f) - bk.y * w, y + bk.y * (3 + k * 3.2f) + 3 + bk.x * w};
            DrawLineEx(prev, q, 7.0f - u * 4 + 2, FAUNA_INK); DrawLineEx(prev, q, 7.0f - u * 4, scale);
            prev = q;
        }
        DrawTri(prev, {prev.x + bk.x * 5 - 4, prev.y - 5}, {prev.x + bk.x * 5 + 4, prev.y + 5}, Color{70, 140, 140, 255}); // the flukes
        // the torso, leaning into the swim
        Vector2 chest{x - bk.x * 3, y - bk.y * 3 - 2}, head{x - bk.x * 7, y - bk.y * 7 - 5};
        DrawLineEx({x + bk.x * 2, y + bk.y * 2 + 2}, chest, 8, FAUNA_INK); DrawLineEx({x + bk.x * 2, y + bk.y * 2 + 2}, chest, 6, skin);
        DrawCircleV(head, 4.2f, FAUNA_INK); DrawCircleV(head, 3.4f, skin);
        for (int k = 0; k < 4; k++) DrawLineEx(head, {head.x + bk.x * (5 + k) + sinf(t * 2 + k) * 1.5f, head.y + bk.y * (5 + k) - 2 + k}, 1.4f, hair); // streaming hair
        DrawCircle((int)(head.x - bk.x * 1.6f), (int)head.y - 1, 0.8f, Color{230, 240, 120, 255});
        // the trident arm: shoulder to hand by IK, levelled at the target when it strikes
        Vector2 sh{chest.x, chest.y - 1}, hand{chest.x - bk.x * (strike ? 10 : coil ? 3 : 6), chest.y - bk.y * 6 + (coil ? -5 : 2)};
        Vector2 el = ik::Knee(sh, hand, 5, 5, f);
        DrawLineEx(sh, el, 3, FAUNA_INK); DrawLineEx(el, hand, 3, FAUNA_INK); DrawLineEx(sh, el, 2, skin); DrawLineEx(el, hand, 2, skin);
        Vector2 tip{hand.x - bk.x * 12, hand.y - bk.y * 12}, butt{hand.x + bk.x * 6, hand.y + bk.y * 6};
        DrawLineEx(butt, tip, 1.4f, Color{200, 170, 90, 255});
        for (int k = -1; k <= 1; k++) DrawLineEx(tip, {tip.x - bk.x * 3 + bk.y * k * 2, tip.y - bk.y * 3 - bk.x * k * 2}, 1, Color{220, 200, 120, 255});
        break;
    }
    case WS_RAY: { // a flat disc undulating along its edges, a whip tail; blue-white arcs when it discharges
        float wave = sinf(b.phase * 4) * 2;
        Color c{120, 110, 96, 255};
        DrawTri({x - 13, y + wave * 0.5f}, {x + 13, y - wave * 0.5f}, {x, y - 7}, FAUNA_INK); DrawTri({x - 13, y + wave * 0.5f}, {x + 13, y - wave * 0.5f}, {x, y + 6}, FAUNA_INK);
        DrawTri({x - 12, y + wave * 0.5f}, {x + 12, y - wave * 0.5f}, {x, y - 6}, c); DrawTri({x - 12, y + wave * 0.5f}, {x + 12, y - wave * 0.5f}, {x, y + 5}, Tone(c, 0.2f));
        DrawLineEx({x - f * 5, y + 1}, {x - f * 16, y + 2 + sinf(t * 3) * 2}, 1.2f, c);
        DrawCircle((int)(x + f * 3) - 2, (int)y - 2, 0.9f, FAUNA_INK); DrawCircle((int)(x + f * 3) + 2, (int)y - 2, 0.9f, FAUNA_INK);
        for (int k = 0; k < 3; k++) DrawCircle((int)(x - 4 + k * 4), (int)y + 1, 1.1f, Color{200, 220, 240, 150});
        if (b.special > 0) for (int k = 0; k < 8; k++) { // the discharge
            float a = k * PI / 4 + t * 9, r0 = 8, r1 = 30 + Hs(t * 13 + k) * 30;
            Vector2 m1{x + cosf(a) * (r0 + r1) * 0.5f + Hs(t * 7 + k) * 6 - 3, y + sinf(a) * (r0 + r1) * 0.5f};
            DrawLineEx({x + cosf(a) * r0, y + sinf(a) * r0}, m1, 2, Color{210, 240, 255, 230}); DrawLineEx(m1, {x + cosf(a) * r1, y + sinf(a) * r1}, 1.4f, Color{150, 210, 255, 200});
        }
        break;
    }
    case WS_CRAB: {
        Color c{190, 110, 70, 255};
        for (int side = -1; side <= 1; side += 2) for (int k = 0; k < 3; k++) { float ph = b.phase * 10 + k * 2 + side, lift = fabsf(b.vel.x) > 4 ? std::max(0.0f, sinf(ph)) * 1.5f : 0; Vector2 hip{x + side * 2.0f, y}, foot{x + side * (6 + k * 2.0f), y + 4 - lift}; Vector2 kn = ik::Knee(hip, foot, 4, 4, (float)side); DrawLineEx(hip, kn, 1.2f, FAUNA_INK); DrawLineEx(kn, foot, 1.0f, FAUNA_INK); }
        DrawEllipse((int)x, (int)y - 1, 6, 4, FAUNA_INK); DrawEllipse((int)x, (int)y - 1, 5, 3.2f, c);
        float snip = b.act == BeastAct::Eat ? sinf(t * 16) * 1.5f : 0;
        for (int side = -1; side <= 1; side += 2) DrawCircle((int)(x + f * 6 + side * 2), (int)(y - 4 + snip * side), 1.8f, Tone(c, 0.15f));
        break;
    }
    case WS_FUNGUS: { // a cluster of pale, glowing caps on thin stalks
        float glow = 0.5f + 0.3f * sinf(t * 1.5f + b.phase);
        for (int k = 0; k < 4; k++) { float bx = x - 5 + k * 3.5f, h = 5 + (k % 2) * 4 + Hs(b.id * 1.3f + k) * 3; DrawLineEx({bx, y + 3}, {bx, y + 3 - h}, 1.2f, Color{200, 196, 170, 255}); DrawEllipse((int)bx, (int)(y + 3 - h), 2.8f, 1.8f, Color{180, 230, 200, 255}); DrawEllipse((int)bx, (int)(y + 3 - h), 5, 3, Fade(Color{160, 255, 210, 255}, 0.15f * glow)); }
        break;
    }
    case WS_MANATEE: { // the Goliath Manatee: vast, wrinkled, slow; a paddle tail; its broad back a platform
        Color c{120, 116, 110, 255}, belly{160, 154, 146, 255};
        float bob = sinf(t * 0.8f + b.phase) * 1.5f;
        DrawEllipse((int)x, (int)(y + bob), 56, 22, FAUNA_INK); DrawEllipse((int)x, (int)(y + bob), 54, 20, c);
        DrawEllipse((int)x, (int)(y + bob + 8), 46, 10, belly);
        float tl = sinf(t * 1.2f + b.phase) * 6;
        DrawEllipse((int)(x - f * 58), (int)(y + bob + tl * 0.3f), 14, 9 + tl * 0.2f, FAUNA_INK); DrawEllipse((int)(x - f * 58), (int)(y + bob + tl * 0.3f), 13, 8 + tl * 0.2f, c); // the paddle
        DrawEllipse((int)(x + f * 52), (int)(y + bob + 4), 14, 11, c); // the snout, browsing
        DrawCircle((int)(x + f * 58), (int)(y + bob - 2), 1.6f, FAUNA_INK);
        for (int k = 0; k < 4; k++) DrawLineEx({x - 30 + k * 16, y + bob - 16}, {x - 26 + k * 16, y + bob - 6}, 1, Tone(c, -0.25f)); // wrinkles and old scars
        DrawTri({x + f * 20, y + bob + 12}, {x + f * 4, y + bob + 26}, {x + f * 14, y + bob + 12}, Tone(c, -0.15f)); // a flipper
        break;
    }
    case WS_OCTOSTALKER: { // the Mimic Octopus-Stalker: when still, one more stripe of kelp on the stalk
        bool shown = b.act != BeastAct::Ambush || b.special < 0.5f;
        Color kelp{70, 110, 50, 255}, skin{190, 150, 90, 255};
        Color c = shown ? skin : kelp;
        DrawEllipse((int)b.anchor.x, (int)b.anchor.y, 7, 10, Fade(FAUNA_INK, shown ? 1.0f : 0.4f)); DrawEllipse((int)b.anchor.x, (int)b.anchor.y, 6, 9, c);
        if (shown) { DrawCircle((int)b.anchor.x - 2, (int)b.anchor.y - 3, 1.4f, Color{250, 220, 90, 255}); for (int k = 0; k < 3; k++) DrawLineEx({b.anchor.x - 5, b.anchor.y - 4 + k * 4}, {b.anchor.x + 5, b.anchor.y - 3 + k * 4}, 1, Color{90, 60, 40, 255}); } // banded, and its eye
        Vector2 tip = b.territory;
        for (int k = 0; k < 3; k++) { Vector2 end = (b.act == BeastAct::Strike || b.act == BeastAct::Wander) && k == 0 ? tip : Vector2{b.anchor.x + (k - 1) * 3.0f + sinf(t * 2 + k) * 2, b.anchor.y + 14 + k * 3.0f}; DrawLineEx(b.anchor, end, 3, Fade(FAUNA_INK, shown ? 1.0f : 0.4f)); DrawLineEx(b.anchor, end, 2, c); }
        break;
    }
    case WS_HMANTIS: { // the Harpoon Mantis in its burrow: stalked eyes, a cocked club; the strike, a white streak
        float px = b.anchor.x, py = b.anchor.y;
        bool cock = b.act == BeastAct::Coil;
        DrawEllipse((int)px, (int)py, 9, 7, FAUNA_INK); DrawEllipse((int)px, (int)py, 8, 6, Color{20, 30, 30, 255}); // the burrow
        DrawRectangle((int)(px + f * 1) - 3, (int)py - 3, 7, 6, Color{80, 180, 150, 255});
        for (int e = -1; e <= 1; e += 2) DrawCircle((int)(px + f * 3 + e * 2), (int)py - 7, 1.8f, b.stunT > 0 ? Color{120, 120, 120, 255} : cock ? Color{255, 80, 60, 255} : Color{250, 210, 80, 255});
        if (cock) DrawCircleLines((int)px, (int)py, 10 + sinf(t * 40) * 2, Color{255, 120, 90, 255}); // the tell
        if (b.act == BeastAct::Strike) { DrawLineEx(b.anchor, b.territory, 2, Color{240, 250, 255, 255}); DrawCircleV(b.territory, 4, Color{255, 255, 255, 255}); }
        if (b.stunT > 0) for (int k = 0; k < 3; k++) DrawCircle((int)(px + cosf(t * 5 + k * 2) * 8), (int)(py - 10), 1.2f, Color{200, 240, 255, 255});
        break;
    }
    case WS_SARDINE: { // silver-fin sardines: a glint of silver, flickering as the school turns
        bool glint = fmodf(t * 3 + b.phase, 2.0f) < 0.2f;
        DrawEllipse((int)x, (int)y, 4, 1.6f, glint ? Color{250, 250, 255, 255} : Color{170, 190, 205, 255});
        DrawTri({x - f * 4, y}, {x - f * 7, y - 2}, {x - f * 7, y + 2}, Color{140, 160, 180, 255});
        break;
    }
    case WS_BLOODKELP: { // blood-kelp: dark red fronds, bleeding when grazed
        bool bleeding = b.flashT > 0;
        for (int k = 0; k < 3; k++) { float sw = sinf(t * 1.2f + k + b.phase) * 4; Vector2 prev{x + (k - 1) * 3.0f, y}; for (int j = 1; j <= 5; j++) { Vector2 q{x + (k - 1) * 3.0f + sw * j / 5.0f, y - j * 8.0f}; DrawLineEx(prev, q, 3.5f - j * 0.3f, Color{120, 30, 40, 255}); prev = q; } }
        if (bleeding) for (int k = 0; k < 4; k++) DrawCircle((int)(x + sinf(t * 2 + k) * 6), (int)(y - 20 - fmodf(t * 12 + k * 9, 30)), 2.5f, Fade(Color{200, 40, 50, 255}, 0.4f));
        break;
    }
    case WS_LANEMONE: { // a luminescent anemone: the one safe light in the murk
        float gl = 0.75f + 0.25f * sinf(t * 1.5f + b.phase);
        DrawCircle((int)x, (int)y - 10, 30, Fade(Color{140, 220, 255, 255}, 0.07f * gl));
        DrawRectangle((int)x - 4, (int)y - 8, 8, 8, Color{90, 70, 110, 255});
        for (int k = -4; k <= 4; k++) { float an = k * 0.25f + sinf(t * 2 + k) * 0.1f; Vector2 tip{x + sinf(an) * 12, y - 8 - cosf(an) * 12}; DrawLineEx({x, y - 8}, tip, 1.6f, Fade(Color{160, 230, 255, 255}, gl)); DrawCircleV(tip, 1.4f, Fade(WHITE, gl)); }
        break;
    }
    case WS_AIRWEED: { // air-weed: bulbs of trapped gas on a stalk
        if (b.act == BeastAct::Drift) { DrawLineEx({x, y}, {x, y - 6}, 1.5f, Color{80, 120, 70, 255}); break; }
        DrawLineEx({x, y}, {x + sinf(t) * 2, y - 14}, 1.5f, Color{80, 120, 70, 255});
        for (int k = 0; k < 3; k++) { float bx = x + (k - 1) * 5 + sinf(t * 1.5f + k) * 1.5f, by = y - 14 - (k % 2) * 5; DrawCircle((int)bx, (int)by, 4.5f, FAUNA_INK); DrawCircle((int)bx, (int)by, 3.8f, Color{170, 220, 200, 200}); DrawCircle((int)bx - 1, (int)by - 1, 1.2f, WHITE); }
        break;
    }
    case WS_TANGLE: { // tangle-vine: a knotted wall of tough kelp, three tiles high
        for (int k = 0; k < 7; k++) { float sx = x - 14 + k * 4.5f; Vector2 prev{sx, y}; for (int j = 1; j <= 6; j++) { Vector2 q{sx + sinf(j * 1.3f + k + t * 0.6f) * 6, y - j * 16.0f}; DrawLineEx(prev, q, 3, Color{40, 70, 36, 255}); prev = q; } }
        for (int j = 0; j < 5; j++) DrawLineEx({x - 16, y - 12 - j * 18.0f}, {x + 16, y - 20 - j * 18.0f}, 2, Color{56, 90, 44, 255}); // woven across
        break;
    }
    case WS_SPOREPOD: { // a spore-pod, swollen and ripe
        if (b.act == BeastAct::Drift) { DrawEllipse((int)x, (int)y - 3, 6, 3, Color{110, 100, 80, 255}); break; }
        DrawEllipse((int)x, (int)y - 8, 8, 9, FAUNA_INK); DrawEllipse((int)x, (int)y - 8, 7, 8, Color{180, 160, 110, 255});
        for (int k = 0; k < 5; k++) DrawCircle((int)(x - 4 + k * 2), (int)(y - 10 + (k % 2) * 4), 1, Color{120, 100, 60, 255});
        break;
    }
    default: DrawCircle((int)x, (int)y, 5, Color{200, 200, 200, 255}); break;
    }
    (void)p;
}

void DrawAtlantisBeast(const PlatformState& p, const Beast& b, float t) {
    const SpeciesDef& S = BeastSpecies(PL_ATLANTIS, b.species);
    float x = b.pos.x, y = b.pos.y, f = b.facing;
    bool dead = b.life == BeastLife::Corpse, strike = b.act == BeastAct::Strike, coil = b.act == BeastAct::Coil;
    float beat = sinf(b.phase * 7);
    if (dead) {
        float r = S.radius;
        DrawEllipse((int)x, (int)y + 1, r * 1.2f + 1, r * 0.5f + 1, FAUNA_INK);
        DrawEllipse((int)x, (int)y + 1, r * 1.2f, r * 0.5f, Color{90, 100, 116, 255});
        return;
    }
    switch (b.species) {
    case AS_WISP: {
        float g = 0.6f + 0.4f * sinf(t * 5 + b.phase);
        DrawCircle((int)x, (int)y, 9, Fade(Color{140, 240, 250, 255}, 0.12f * g)); DrawCircle((int)x, (int)y, 3, Fade(Color{200, 255, 255, 255}, 0.8f * g));
        DrawLineEx({x, y}, {x - b.vel.x * 0.06f, y - b.vel.y * 0.06f}, 1.2f, Fade(Color{140, 240, 250, 255}, 0.4f));
        break;
    }
    case AS_SHRIMP: { // pale, eyeless, translucent
        Color c = Fade(Color{220, 214, 230, 255}, 0.8f);
        for (int k = 0; k < 5; k++) { float u = k / 4.0f; DrawCircle((int)(x - f * (k * 2.4f - 4)), (int)(y + u * u * 4), 2.8f - u, c); }
        for (int k = 0; k < 2; k++) DrawLineEx({x + f * 4, y - 1}, {x + f * (14 + k * 3), y - 7 + k * 2 + sinf(t * 4 + k) * 2}, 0.8f, Fade(WHITE, 0.6f));
        break;
    }
    case AS_ANGLER: { // a black bulb of a body, a gape of needle teeth, and the lure - the only light it shows
        float camo = std::clamp(b.special, 0.0f, 1.0f), a = 1.0f - 0.6f * camo;
        Color c = Fade(Color{40, 36, 44, 255}, a);
        DrawCircle((int)x, (int)y, 11, Fade(FAUNA_INK, a)); DrawCircle((int)x, (int)y, 10, c);
        DrawTri({x - f * 9, y}, {x - f * 17, y - 5 + beat * 2}, {x - f * 17, y + 5 + beat * 2}, c);
        float gape = coil ? 0.9f : strike ? 1.0f : b.act == BeastAct::Eat ? 0.6f * fabsf(sinf(t * 10)) : 0.3f;
        DrawTri({x + f * 4, y + 1}, {x + f * 12, y - 2 - gape * 4}, {x + f * 12, y + 4 + gape * 5}, Color{20, 6, 12, 255});
        for (int k = 0; k < 4; k++) { DrawLineEx({x + f * (6 + k * 1.8f), y - 1 - gape * 2}, {x + f * (6 + k * 1.8f), y + 1 - gape * 1}, 0.8f, Color{230, 230, 220, 255}); DrawLineEx({x + f * (6 + k * 1.8f), y + 3 + gape * 3}, {x + f * (6 + k * 1.8f), y + 1 + gape * 2}, 0.8f, Color{230, 230, 220, 255}); }
        Vector2 stalkTip{x + f * 14 + sinf(t * 1.3f) * 2, y - 16 + cosf(t * 1.7f) * 2};
        DrawLineEx({x + f * 2, y - 9}, {x + f * 8, y - 18}, 1.2f, Fade(c, 1)); DrawLineEx({x + f * 8, y - 18}, stalkTip, 1.2f, Fade(c, 1));
        float g = 0.7f + 0.3f * sinf(t * 3);
        DrawCircleV(stalkTip, 12, Fade(Color{150, 250, 240, 255}, 0.12f * g)); DrawCircleV(stalkTip, 2.6f, Color{210, 255, 250, 255}); // the lure
        DrawCircle((int)(x + f * 4), (int)y - 5, 1.2f, Fade(Color{220, 230, 140, 255}, a));
        break;
    }
    case AS_LOSTONE: { // a drowned citizen: hunched, robes shredding in the current, eyes gone to glyph-light
        Color robe{70, 78, 96, 255}, flesh{120, 140, 140, 255};
        DrawBeastLegs(b, S, false, 2.4f, Tone(robe, -0.2f), 0.6f, 0.6f);
        float sway = sinf(t * 1.2f + b.phase) * 1.5f;
        DrawTri({x - 7, y + 4}, {x + 7, y + 4}, {x + sway, y - 12}, FAUNA_INK); DrawTri({x - 6, y + 3}, {x + 6, y + 3}, {x + sway, y - 11}, robe);
        for (int k = 0; k < 3; k++) DrawLineEx({x - 5 + k * 5.0f, y + 3}, {x - 5 + k * 5.0f + sinf(t * 2 + k) * 3, y + 9}, 1.4f, robe); // tatters
        Vector2 head{x + f * 3 + sway, y - 13};
        DrawCircleV(head, 4, FAUNA_INK); DrawCircleV(head, 3.2f, flesh);
        DrawCircle((int)(head.x + f * 1.5f), (int)head.y - 1, 1.1f, Color{140, 240, 250, 255}); // the glyph-lit eye
        Vector2 sh{x + f * 2 + sway, y - 7}, hand{x + f * 10, y - 4 + sinf(t * 1.5f) * 2}; // reaching ahead as it shuffles
        Vector2 el = ik::Knee(sh, hand, 5, 5, -f);
        DrawLineEx(sh, el, 2, flesh); DrawLineEx(el, hand, 1.8f, flesh);
        DrawBeastLegs(b, S, true, 2.4f, robe, 0.6f, 0.6f);
        break;
    }
    case AS_GUARDIAN: { // a crab of carved stone: fully camouflaged it is a heap of masonry; moving, legs and claws unfold from it
        float camo = std::clamp(b.special, 0.0f, 1.0f);
        Color stone{116, 122, 130, 255}, dk{60, 64, 76, 255};
        if (camo < 0.7f) for (int side = 0; side < 2; side++) for (int k = 0; k < 3; k++) {
            float ph = b.phase * 7 + k * 2 + side * 3, lift = fabsf(b.vel.x) > 5 ? std::max(0.0f, sinf(ph)) * 2 : 0, sx = side ? 1.0f : -1.0f;
            Vector2 hip{x + sx * (4 + k * 2), y - 2}, foot{x + sx * (11 + k * 3), y + 7 - lift}; Vector2 kn = ik::Knee(hip, foot, 7, 7, sx);
            DrawLineEx(hip, kn, 3, FAUNA_INK); DrawLineEx(kn, foot, 2.4f, FAUNA_INK); DrawLineEx(hip, kn, 2, stone); DrawLineEx(kn, foot, 1.6f, stone);
        }
        DrawRectangle((int)x - 12, (int)y - 9, 24, 14, FAUNA_INK); DrawRectangle((int)x - 11, (int)y - 8, 22, 12, stone);   // the carved block of its shell
        DrawRectangle((int)x - 11, (int)y - 3, 22, 1, dk); DrawRectangle((int)x - 3, (int)y - 8, 1, 5, dk);
        if (camo < 0.7f) { // claws out, and eyes
            float open = coil || strike ? 1.0f : 0.3f;
            for (int side = -1; side <= 1; side += 2) { Vector2 cl{x + f * 14 + side * 3, y - 6 - open * 4}; DrawRectangle((int)cl.x - 3, (int)cl.y - 2, 6, 5, stone); DrawRectangle((int)cl.x - 3, (int)cl.y + 1, 6, 1, dk); }
            DrawCircle((int)(x + f * 5), (int)y - 10, 1.2f, Color{140, 240, 250, 255}); DrawCircle((int)(x + f * 8), (int)y - 10, 1.2f, Color{140, 240, 250, 255});
        }
        break;
    }
    case AS_EEL: {
        Color c{50, 60, 90, 255}, belly{120, 130, 150, 255};
        Vector2 pts[SPINE];
        for (int k = 0; k < SPINE; k++) pts[k] = b.spine[k];
        if (coil) for (int k = 1; k < SPINE; k++) pts[k].y += sinf(k * 1.3f) * 5;
        DrawSpineBody(pts, SPINE, 5.5f, 2.0f, coil ? 1.5f : 3.0f, b.phase, c, belly);
        for (int k = 1; k < SPINE; k += 2) DrawCircle((int)pts[k].x, (int)pts[k].y - 1, 1.2f, Fade(Color{140, 240, 250, 255}, 0.6f + 0.3f * sinf(t * 3 + k))); // glowing glyph marks
        Vector2 hd{pts[0].x - pts[1].x, pts[0].y - pts[1].y}; float hl = sqrtf(hd.x * hd.x + hd.y * hd.y); hd = hl > 0.01f ? Vector2{hd.x / hl, hd.y / hl} : Vector2{f, 0};
        float open = coil ? 0.6f : strike ? 0.45f : 0.15f;
        Vector2 j{pts[0].x + hd.x * 4, pts[0].y + hd.y * 4};
        DrawTri(j, {j.x + hd.x * 9 - hd.y * 9 * open, j.y + hd.y * 9 + hd.x * 9 * open}, {j.x + hd.x * 9 + hd.y * 9 * open, j.y + hd.y * 9 - hd.x * 9 * open}, Color{40, 14, 24, 255});
        DrawCircle((int)(pts[0].x + hd.x * 2), (int)(pts[0].y - 2), 1.4f, Color{140, 240, 250, 255});
        break;
    }
    case AS_OLEVIATHAN: { // the Orichalcum Leviathan: a gentle whale crusted in glowing Atlantean crystal
        Color hide{60, 70, 96, 255}, crystal{250, 200, 90, 255};
        float bob = sinf(t * 0.6f + b.phase) * 2;
        DrawEllipse((int)x, (int)(y + bob), 68, 22, FAUNA_INK); DrawEllipse((int)x, (int)(y + bob), 66, 20, hide);
        DrawEllipse((int)x, (int)(y + bob + 9), 56, 9, Color{120, 128, 150, 255});
        DrawTri({x - f * 64, y + bob}, {x - f * 90, y + bob - 16}, {x - f * 84, y + bob + 2}, hide); DrawTri({x - f * 64, y + bob}, {x - f * 90, y + bob + 16}, {x - f * 84, y + bob + 2}, hide);
        for (int k = 0; k < 8; k++) { float cx = x - 48 + k * 13, ch = 8 + (k * 5 % 4) * 3, gl = 0.6f + 0.4f * sinf(t * 2 + k); DrawTri({cx - 4, y + bob - 18}, {cx + 4, y + bob - 18}, {cx, y + bob - 18 - ch}, Fade(crystal, gl)); }
        DrawRectangle((int)x - 60, (int)(y + bob - 24), 120, 3, Fade(crystal, 0.5f)); // the ridge you ride on
        DrawCircle((int)(x + f * 52), (int)(y + bob - 2), 2.2f, Color{250, 230, 160, 255});
        break;
    }
    case AS_SCOURGE: { // Poseidon's Scourge: a colossal golden serpent, its body trailing back out of the dark
        Color gold{210, 170, 60, 255}, dark{120, 90, 30, 255};
        Vector2 prev = b.pos;
        for (int k = 1; k <= 14; k++) { Vector2 q{b.pos.x - b.facing * k * 22 + sinf(t * 3 - k * 0.6f) * 3, b.pos.y + sinf(t * 2.5f - k * 0.5f) * 18}; float w = 30 - k * 1.2f; DrawLineEx(prev, q, w + 4, FAUNA_INK); DrawLineEx(prev, q, w, k % 2 ? gold : Tone(gold, -0.15f)); DrawLineEx({prev.x, prev.y + w * 0.3f}, {q.x, q.y + w * 0.3f}, w * 0.3f, dark); prev = q; }
        Vector2 h = b.pos;
        DrawEllipse((int)h.x, (int)h.y, 30, 20, FAUNA_INK); DrawEllipse((int)h.x, (int)h.y, 28, 18, gold);
        for (int k = 0; k < 5; k++) DrawTri({h.x - 14 + k * 7, h.y - 16}, {h.x - 10 + k * 7, h.y - 16}, {h.x - 12 + k * 7, h.y - 32 - (k % 2) * 6}, Color{250, 230, 150, 255}); // the crown of spines
        DrawTri({h.x + b.facing * 20, h.y - 4}, {h.x + b.facing * 44, h.y - 14}, {h.x + b.facing * 44, h.y + 16}, Color{90, 20, 30, 255}); // the gape
        for (int k = 0; k < 4; k++) DrawTri({h.x + b.facing * (24 + k * 5), h.y - 6}, {h.x + b.facing * (27 + k * 5), h.y - 6}, {h.x + b.facing * (25 + k * 5), h.y}, WHITE);
        DrawCircle((int)(h.x + b.facing * 10), (int)h.y - 6, 3.5f, Color{255, 90, 40, 255});
        break;
    }
    case AS_CMINNOW: { // crystal-minnows: a sliver of stored light; startled, a blinding flash
        bool flash = b.flashT > 0;
        if (flash) DrawCircle((int)x, (int)y, 28, Fade(Color{255, 250, 220, 255}, 0.35f * std::min(1.0f, b.flashT)));
        DrawEllipse((int)x, (int)y, 3.5f, 1.4f, flash ? WHITE : Color{180, 230, 240, 255});
        DrawTri({x - f * 3, y}, {x - f * 6, y - 2}, {x - f * 6, y + 2}, Color{150, 210, 230, 255});
        break;
    }
    case AS_SNAIL: { // a mosaic-snail: a shell of tiny coloured tiles
        DrawEllipse((int)x - (int)f * 2, (int)y + 2, 6, 2, Color{170, 200, 190, 255});
        DrawCircle((int)x, (int)y - 2, 4.5f, FAUNA_INK);
        for (int k = 0; k < 6; k++) DrawRectangle((int)(x - 3 + (k % 3) * 2), (int)(y - 5 + (k / 3) * 3), 2, 2, k % 3 == 0 ? Color{200, 80, 60, 255} : k % 3 == 1 ? Color{60, 120, 200, 255} : Color{230, 200, 90, 255});
        break;
    }
    case AS_SUNKELP: { // sun-crystal kelp: fronds holding a blazing crystal
        for (int k = 0; k < 3; k++) { float sw = sinf(t + k) * 3; DrawLineEx({x + (k - 1) * 3.0f, y}, {x + (k - 1) * 3.0f + sw, y - 24}, 2, Color{70, 120, 80, 255}); }
        DrawCircle((int)x, (int)y - 26, 22, Fade(Color{255, 240, 170, 255}, 0.18f));
        DrawTri({x - 5, y - 24}, {x + 5, y - 24}, {x, y - 36}, Color{255, 240, 170, 255});
        break;
    }
    case AS_PRISM: { // prism-moss: faceted, rainbow-glinting
        for (int k = 0; k < 8; k++) { float hx = x - 14 + k * 4, hh = 3 + (k % 3) * 2; Color c = k % 3 == 0 ? Color{240, 120, 200, 255} : k % 3 == 1 ? Color{120, 220, 240, 255} : Color{240, 230, 120, 255}; if (b.flashT > 0) c = WHITE; DrawTri({hx - 2, y}, {hx + 2, y}, {hx, y - hh}, c); }
        break;
    }
    case AS_AVINE: { // an aqueduct-vine hanging from the stonework
        float len = b.special2, sw = sinf(t * 0.9f + b.phase) * 6 + (b.act == BeastAct::Eat ? b.facing * 14 * (1 - std::min(1.0f, b.actT / 0.5f)) : 0);
        Vector2 prev{x, y};
        for (int k = 1; k <= 8; k++) { Vector2 q{x + sw * k / 8.0f, y + len * k / 8.0f}; DrawLineEx(prev, q, 4, FAUNA_INK); DrawLineEx(prev, q, 2.6f, Color{90, 130, 70, 255}); if (k % 2) DrawEllipse((int)q.x + 3, (int)q.y, 3, 1.5f, Color{110, 160, 80, 255}); prev = q; }
        break;
    }
    case AS_LILY: { // a stasis-lily: a pale bud full of gel
        if (b.act == BeastAct::Drift) { DrawEllipse((int)x, (int)y - 3, 6, 2, Color{150, 200, 190, 255}); break; }
        DrawLineEx({x, y}, {x, y - 8}, 1.5f, Color{80, 130, 90, 255});
        for (int k = 0; k < 5; k++) { float an = -PI / 2 + (k - 2) * 0.5f; DrawEllipse((int)(x + cosf(an) * 5), (int)(y - 10 + sinf(an) * 4), 3, 5, Color{210, 250, 240, 255}); }
        break;
    }
    case AS_RUINSPORE: { // ruin-spore: a crust of grey fungus in a fractured block
        if (b.act == BeastAct::Drift) break;
        for (int k = 0; k < 6; k++) DrawCircle((int)(x - 8 + k * 3), (int)(y - 3 - (k % 2) * 2), 2.2f, Color{150, 150, 140, 255});
        DrawLineEx({x - 10, y - 8}, {x + 2, y - 14}, 1, FAUNA_INK); DrawLineEx({x + 2, y - 14}, {x + 10, y - 9}, 1, FAUNA_INK); // the crack it has opened
        break;
    }
    default: DrawCircle((int)x, (int)y, 5, Color{200, 200, 200, 255}); break;
    }
    if (b.species == AS_GUARDIAN && b.life == BeastLife::Alive) { // the Phalanx: its charge, and its hard-light beam
        if (b.anchor.x > 0) DrawCircle((int)x, (int)y - 6, 10 + b.anchor.x * 20, Fade(Color{160, 240, 255, 255}, 0.3f));
        if (b.special2 > 0) { float x0 = std::min(x, b.goal.x), x1 = std::max(x, b.goal.x); DrawRectangle((int)x0, (int)y - 16, (int)(x1 - x0), 32, Fade(Color{190, 250, 255, 255}, 0.6f)); DrawRectangle((int)x0, (int)y - 4, (int)(x1 - x0), 8, WHITE); }
    }
    (void)p;
}
// A kelp holdfast hollow (the Weeds); a niche in broken masonry (Atlantis).
void DrawWeedsDen(const PlatformState& p, const Den& d, float t) {
    float x = d.pos.x, y = d.pos.y;
    DrawEllipse((int)x, (int)y + 2, 11, 5, FAUNA_INK); DrawEllipse((int)x, (int)y + 2, 9, 3.8f, Color{16, 24, 20, 255});
    for (int k = 0; k < 5; k++) { float a = PI + k * PI / 4; DrawLineEx({x + cosf(a) * 9, y + 2 + sinf(a) * 4}, {x + cosf(a) * 14, y + 3}, 2, Color{90, 80, 40, 255}); } // the holdfast's roots
    int inside = 0; for (const auto& b : p.fauna.beasts) if (b.life == BeastLife::Alive && b.hidden && fabsf(b.pos.x - x) < 4) inside++;
    if (inside && fmodf(t * 0.8f + x * 0.013f, 3.5f) < 2.6f) { DrawCircle((int)x - 2, (int)y + 2, 0.9f, Color{230, 240, 170, 255}); DrawCircle((int)x + 2, (int)y + 2, 0.9f, Color{230, 240, 170, 255}); }
}
void DrawAtlantisDen(const PlatformState& p, const Den& d, float t) {
    float x = d.pos.x, y = d.pos.y;
    DrawRectangle((int)x - 9, (int)y - 2, 18, 9, FAUNA_INK); DrawRectangle((int)x - 8, (int)y - 1, 16, 7, Color{8, 10, 18, 255});
    DrawRectangle((int)x - 10, (int)y - 4, 20, 3, Color{140, 146, 150, 255}); // a broken lintel over the hole
    int inside = 0; for (const auto& b : p.fauna.beasts) if (b.life == BeastLife::Alive && b.hidden && fabsf(b.pos.x - x) < 4 && !(b.species == AS_EEL && b.act == BeastAct::Ambush)) inside++;
    if (inside && fmodf(t * 0.8f + x * 0.013f, 3.5f) < 2.6f) { DrawCircle((int)x - 2, (int)y + 2, 0.9f, Color{140, 240, 250, 255}); DrawCircle((int)x + 2, (int)y + 2, 0.9f, Color{140, 240, 250, 255}); }
}
void DrawPirateBeast(const PlatformState& p, const Beast& b, float t) {
    const SpeciesDef& S = BeastSpecies(PL_PIRATE, b.species);
    float x = b.pos.x, y = b.pos.y, f = b.facing, s = b.scale;
    bool dead = b.life == BeastLife::Corpse;
    bool hunting = b.act == BeastAct::Hunt || b.act == BeastAct::Coil || b.act == BeastAct::Strike;
    bool fleeing = b.act == BeastAct::Flee || b.act == BeastAct::Hide;
    Color deadTint{90, 86, 96, 255};
    auto body = [&](Color c) { return dead ? Color{(unsigned char)((c.r + deadTint.r) / 2), (unsigned char)((c.g + deadTint.g) / 2), (unsigned char)((c.b + deadTint.b) / 2), 255} : c; };
    unsigned coat = (unsigned)b.id * 2654435761u;
    if (dead) { // lying on its side, legs stiff, picked over
        float r = S.radius * s;
        if (S.move == MoveMode::Fly) { DrawEllipse((int)x, (int)y + 2, r * 1.3f + 1, r * 0.5f + 1, FAUNA_INK); DrawEllipse((int)x, (int)y + 2, r * 1.3f, r * 0.5f, body(Color{200, 196, 186, 255})); }
        else { DrawEllipse((int)x, (int)y + 1, r * 1.1f + 1, r * 0.55f + 1, FAUNA_INK); DrawEllipse((int)x, (int)y + 1, r * 1.1f, r * 0.55f, body(Color{120, 100, 84, 255}));
               for (int k = 0; k < 2; k++) DrawLineEx({x - r * 0.4f + k * r * 0.8f, y - 1}, {x - r * 0.5f + k * r * 0.8f, y - r * 0.9f}, 1.5f, body(Color{90, 76, 64, 255})); }
        if (b.meat < 0.7f) for (int k = 0; k < 3; k++) DrawRectangle((int)(x - 5 + k * 4), (int)y, 2, 2, Color{220, 210, 190, 255});
        return;
    }
    switch (b.species) {
    case PS_RAT: {
        Color c = body(Color{104, 92, 84, 255}), belly = Tone(c, 0.25f), pink{214, 150, 150, 255};
        float crouch = fleeing ? 0 : b.act == BeastAct::Scavenge || b.act == BeastAct::Eat ? 1.5f : 0;
        DrawBeastLegs(b, S, false, 1.6f, c, -0.6f, 0.8f);
        DrawBeastTail(b, {x - f * 5, y + 1}, 0.2f, 1.5f, 1.8f, 0.8f, pink, 6);
        DrawEllipse((int)x, (int)(y + crouch), 7.5f * s, 4.8f * s, FAUNA_INK);
        DrawEllipse((int)x, (int)(y + crouch), 6.5f * s, 3.9f * s, c);
        DrawEllipse((int)x, (int)(y + crouch + 1.5f), 5.0f * s, 2.0f * s, belly);
        float nib = b.act == BeastAct::Eat ? sinf(t * 18) * 1.2f : 0;
        Vector2 nose{x + f * (10 + nib), y + crouch + 1};
        DrawTri({x + f * 4, y + crouch - 3.5f}, {x + f * 4, y + crouch + 3}, nose, FAUNA_INK);
        DrawTri({x + f * 4.5f, y + crouch - 2.5f}, {x + f * 4.5f, y + crouch + 2}, {nose.x - f, nose.y}, c);
        DrawCircleV(nose, 1.0f, pink);
        DrawCircle((int)(x + f * 4), (int)(y + crouch - 4), 2.2f, pink); // the ear
        DrawCircle((int)(x + f * 6.5f), (int)(y + crouch - 1.5f), 0.9f, FAUNA_INK);
        DrawBeastLegs(b, S, true, 1.6f, c, -0.6f, 0.8f);
        break;
    }
    case PS_CAT: {
        Color coats[3] = {{40, 38, 42, 255}, {196, 120, 60, 255}, {128, 116, 100, 255}};
        Color c = body(coats[coat % 3]), light = Tone(c, 0.3f);
        bool coil = b.act == BeastAct::Coil, strike = b.act == BeastAct::Strike;
        float low = coil ? 4.0f : hunting ? 2.5f : 0.0f; // it slinks, then crouches flat to pounce
        float stretch = strike ? 1.3f : 1.0f;
        DrawBeastLegs(b, S, false, 2.0f, c, -0.5f, 0.9f);
        DrawBeastTail(b, {x - f * 8, y - 2 + low}, hunting ? 0.1f : 1.3f, coil ? 3.0f : hunting ? 1.5f : 0.8f, 2.6f, 1.8f, c, 6); // up and curious, low and lashing when it hunts
        DrawEllipse((int)x, (int)(y - 1 + low), 10.0f * s * stretch, 5.8f * s, FAUNA_INK);
        DrawEllipse((int)x, (int)(y - 1 + low), 9.0f * s * stretch, 4.8f * s, c);
        if (coat % 3 == 2) for (int k = 0; k < 3; k++) DrawLineEx({x - 5.0f + k * 4, y - 5 + low}, {x - 6.0f + k * 4, y - 1 + low}, 1.2f, Tone(c, -0.4f)); // tabby stripes
        Vector2 head{x + f * 9 * stretch, y - 5 + low * 1.3f};
        DrawCircleV(head, 4.6f * s, FAUNA_INK); DrawCircleV(head, 3.8f * s, c);
        for (int e = -1; e <= 1; e += 2) DrawTri({head.x + e * 1.2f - f * 1, head.y - 2.5f}, {head.x + e * 3.2f, head.y - 7.5f}, {head.x + e * 3.8f, head.y - 1.5f}, c);
        DrawCircle((int)(head.x + f * 1.8f), (int)head.y - 1, hunting ? 1.3f : 1.0f, hunting ? Color{240, 220, 80, 255} : FAUNA_INK);
        DrawCircle((int)(head.x + f * 3.2f), (int)head.y + 1, 0.7f, Color{220, 140, 140, 255});
        DrawEllipse((int)(x + f * 3), (int)(y + 1 + low), 4, 2, light);
        DrawBeastLegs(b, S, true, 2.0f, c, -0.5f, 0.9f);
        break;
    }
    case PS_MONKEY: {
        Color c = body(Color{128, 88, 56, 255}), face = body(Color{214, 176, 136, 255});
        bool keg = b.cooldown <= 0; // still carrying its powder keg
        DrawBeastLegs(b, S, false, 2.0f, c, 0.8f, 0.8f);
        DrawBeastTail(b, {x - f * 6, y}, 1.6f, 1.0f, 2.0f, 1.4f, c, 6);
        if (keg) { DrawRectangle((int)(x - f * 7) - 4, (int)y - 14, 8, 9, FAUNA_INK); DrawRectangle((int)(x - f * 7) - 3, (int)y - 13, 6, 7, Color{110, 76, 44, 255}); DrawLine((int)(x - f * 7) - 3, (int)y - 10, (int)(x - f * 7) + 3, (int)y - 10, Color{60, 60, 60, 255}); }
        DrawEllipse((int)x, (int)y - 4, 6.5f * s, 7.5f * s, FAUNA_INK);
        DrawEllipse((int)x, (int)y - 4, 5.5f * s, 6.5f * s, c);
        float armUp = fleeing ? -10 : 0; // screaming, arms flung up
        for (int a = 0; a < 2; a++) {
            Vector2 sh{x + f * (a ? 3 : -1), y - 8}, hand{x + f * (a ? 9 : 5), y + 3 + armUp + sinf(b.phase * 4 + a) * 2};
            Vector2 el = ik::Knee(sh, hand, 6, 7, -f);
            DrawLineEx(sh, el, 3.4f, FAUNA_INK); DrawLineEx(el, hand, 3.0f, FAUNA_INK);
            DrawLineEx(sh, el, 2.0f, c); DrawLineEx(el, hand, 1.8f, c);
        }
        Vector2 head{x + f * 3, y - 13};
        DrawCircleV(head, 5.2f, FAUNA_INK); DrawCircleV(head, 4.4f, c);
        DrawEllipse((int)(head.x + f * 1.6f), (int)head.y + 1, 3, 2.6f, face);
        DrawCircle((int)(head.x + f * 2.2f), (int)head.y, 0.8f, FAUNA_INK);
        if (fleeing) DrawCircle((int)(head.x + f * 3), (int)head.y + 2, 1.2f, FAUNA_INK); // mouth open
        DrawBeastLegs(b, S, true, 2.0f, c, 0.8f, 0.8f);
        break;
    }
    case PS_DOG:
        DrawDog(b, S, t, body(b.special > 0 ? Color{150, 92, 70, 255} : ((coat % 2) ? Color{150, 112, 70, 255} : Color{70, 62, 58, 255})), b.special > 0, false);
        break;
    case PS_FLEA: {
        Color c{40, 30, 24, 230};
        int n = b.act == BeastAct::Latched ? 5 : 9;
        float r = b.act == BeastAct::Latched ? 5.0f : 9.0f;
        for (int k = 0; k < n; k++) {
            float a = k * 2.399f + t * (b.act == BeastAct::Latched ? 3.0f : 9.0f), hop = fabsf(sinf(t * 11 + k * 1.3f)) * 3;
            DrawRectangle((int)(x + cosf(a) * r * (0.4f + 0.6f * ((k * 37) % 10) / 10.0f)), (int)(y + sinf(a) * r * 0.6f - hop), 2, 2, c);
        }
        break;
    }
    case PS_OWL: {
        bool dive = b.act == BeastAct::Strike || b.act == BeastAct::Coil;
        bool perched = b.grounded || (sqrtf(b.vel.x * b.vel.x + b.vel.y * b.vel.y) < 12 && (b.act == BeastAct::Rest || b.act == BeastAct::Idle));
        Color c = body(Color{196, 160, 110, 255}), face{240, 232, 214, 255};
        if (perched) {
            DrawEllipse((int)x, (int)y, 7 * s, 9 * s, FAUNA_INK); DrawEllipse((int)x, (int)y, 6 * s, 8 * s, c);
            DrawEllipse((int)x, (int)y + 2, 4 * s, 5 * s, Tone(c, 0.35f));
        } else DrawBird(b, t, 22 * s, 6 * s, c, Tone(c, -0.1f), Color{200, 180, 150, 255}, 9, dive);
        Vector2 fc{x + (perched ? 0 : f * 7), y - (perched ? 8 : 4)};
        DrawCircleV(fc, 4.6f, FAUNA_INK); DrawCircleV(fc, 3.8f, face); // the heart-shaped face
        DrawCircle((int)fc.x - 1, (int)fc.y, 0.9f, FAUNA_INK); DrawCircle((int)fc.x + 2, (int)fc.y, 0.9f, FAUNA_INK);
        if (dive) for (int k = -1; k <= 1; k += 2) DrawLineEx({x + k * 2.0f, y + 4}, {x + f * 6 + k * 2, y + 10}, 1.5f, Color{120, 110, 90, 255}); // talons out
        break;
    }
    case PS_GULL:
        DrawBird(b, t, 18 * s, 5 * s, body(Color{236, 234, 228, 255}), body(Color{150, 156, 164, 255}), Color{230, 190, 60, 255}, 11, b.act == BeastAct::Strike);
        break;
    case PS_ALBATROSS:
        DrawBird(b, t, 40 * s, 7 * s, body(Color{240, 238, 232, 255}), body(Color{70, 68, 72, 255}), Color{226, 190, 150, 255}, 3.5f, false);
        break;
    case PS_TORTOISE: { // the Timber-Shell Tortoise: a dome of shell heaped with sunken cannonballs and broken planks
        Color shell = body(Color{96, 84, 60, 255}), skin = body(Color{120, 128, 96, 255});
        float step = sinf(b.phase * 2) * (fabsf(b.vel.x) > 2 ? 2.0f : 0.0f);
        for (int k = 0; k < 4; k++) { // four stumpy legs, stepping in turn
            float lx = x + (k < 2 ? 22 : -22) * f + (k % 2 ? 6 : -6), lift = (k % 2 == 0 ? step : -step);
            DrawRectangle((int)lx - 6, (int)(y + 4 - std::max(0.0f, lift)), 12, (int)(16 + std::min(0.0f, lift)), FAUNA_INK);
            DrawRectangle((int)lx - 5, (int)(y + 5 - std::max(0.0f, lift)), 10, (int)(14 + std::min(0.0f, lift)), skin);
        }
        DrawEllipse((int)(x + f * 44), (int)(y + 2), 11, 8, FAUNA_INK); DrawEllipse((int)(x + f * 44), (int)(y + 2), 10, 7, skin); // the head
        DrawCircle((int)(x + f * 48), (int)y, 1.6f, FAUNA_INK);
        DrawEllipse((int)x, (int)y, 42, 26, FAUNA_INK);
        DrawEllipse((int)x, (int)y, 40, 24, shell);
        DrawRectangle((int)x - 40, (int)y, 80, 10, Tone(shell, -0.35f)); // the shell's rim
        for (int k = 0; k < 5; k++) DrawRectangle((int)(x - 30 + k * 13), (int)(y - 18 + (k % 2) * 5), 12, 3, Color{130, 96, 60, 255}); // planks
        for (int k = 0; k < 4; k++) { float cx2 = x - 24 + k * 16; DrawCircle((int)cx2, (int)(y - 12 - (k % 2) * 6), 4.5f, FAUNA_INK); DrawCircle((int)cx2, (int)(y - 12 - (k % 2) * 6), 3.6f, Color{60, 62, 66, 255}); } // cannonballs
        break;
    }
    case PS_CUTTLE: { // the Rigging-Mimic: a fraying rope - until it isn't
        Vector2 top = b.anchor, tip = b.territory;
        bool shown = b.act != BeastAct::Ambush;
        Color rope{150, 128, 90, 255}, flesh = body(Color{170, 90, 110, 255});
        Color c = shown ? flesh : rope;
        Vector2 prev = top;
        for (int k = 1; k <= 8; k++) {
            float u = k / 8.0f;
            Vector2 q{top.x + (tip.x - top.x) * u + sinf(t * 2 + u * 5 + b.phase) * 2 * u, top.y + (tip.y - top.y) * u};
            DrawLineEx(prev, q, 4, FAUNA_INK); DrawLineEx(prev, q, 2.5f, c);
            if (!shown && k % 2 == 0) DrawLineEx({q.x - 2, q.y - 2}, {q.x + 2, q.y + 1}, 1, Tone(rope, -0.3f)); // the lay of the rope
            if (shown && k % 2 == 0) DrawCircle((int)q.x, (int)q.y, 1, Color{230, 200, 210, 255});   // suckers
            prev = q;
        }
        if (!shown) { DrawLineEx(tip, {tip.x - 3, tip.y + 5}, 1, rope); DrawLineEx(tip, {tip.x + 3, tip.y + 4}, 1, rope); } // the frayed end
        if (b.act == BeastAct::Flee || b.act == BeastAct::Coil || b.act == BeastAct::Strike) { // its body, up on the yard
            DrawEllipse((int)top.x, (int)top.y - 4, 12, 7, FAUNA_INK); DrawEllipse((int)top.x, (int)top.y - 4, 11, 6, flesh);
            DrawEllipse((int)(top.x + 4), (int)top.y - 5, 3, 2, Color{240, 220, 120, 255});
            DrawRectangle((int)(top.x + 3), (int)top.y - 5, 3, 1, FAUNA_INK); // the W-shaped pupil, near enough
        }
        break;
    }
    case PS_MANTIS: { // in a crate's porthole: eyes on stalks and folded clubs; the strike, a streak of boiling water
        float px = b.anchor.x, py = b.anchor.y;
        bool cock = b.act == BeastAct::Coil;
        DrawCircle((int)px, (int)py - 2, 7, FAUNA_INK); DrawCircle((int)px, (int)py - 2, 6, Color{30, 20, 18, 255}); // the porthole
        Color shell{60, 160, 120, 255};
        DrawRectangle((int)(px - 3 + f * 2), (int)py - 5, 6, 6, shell);
        for (int e = -1; e <= 1; e += 2) { DrawLineEx({px + f * 3, py - 5}, {px + f * 5 + e * 2, py - 11}, 1, shell); DrawCircle((int)(px + f * 5 + e * 2), (int)py - 12, 1.8f, cock ? Color{255, 90, 60, 255} : Color{250, 200, 80, 255}); }
        DrawLineEx({px + f * 3, py}, {px + f * (cock ? 1.0f : 8.0f), py + 2}, 2, Color{230, 90, 70, 255}); // the club
        if (b.act == BeastAct::Strike) {
            DrawLineEx(b.anchor, b.goal, 3, Fade(Color{220, 240, 255, 255}, 0.7f));
            DrawCircleV(b.goal, 9 + sinf(t * 60) * 2, Fade(Color{240, 250, 255, 255}, 0.85f));
            DrawCircleLines((int)b.goal.x, (int)b.goal.y, 14, Color{190, 230, 255, 255});
        }
        break;
    }
    case PS_BORER: { // worm-riddled planks: holes, sawdust, a worm's head now and then
        int x0 = (int)b.anchor.x, row = (int)b.anchor.y, n = (int)b.special2;
        for (int k = 0; k < n * 3; k++) {
            float hx = x0 * (float)T + fmodf(k * 37.0f + b.id * 11.0f, n * (float)T), hy = row * (float)T + 3 + fmodf(k * 13.0f, 10);
            DrawRectangle((int)hx, (int)hy, 2, 2, Color{40, 26, 18, 255});
            if (b.act != BeastAct::Drift && k % 5 == 0 && fmodf(t * 0.8f + k, 3.0f) < 0.4f) DrawRectangle((int)hx, (int)hy - 2, 2, 2, Color{230, 200, 170, 255});
        }
        break;
    }
    case PS_MOTH: { // copper-scale moths: a glint of metal wings, flaring bright when they're scared
        float flap = sinf(t * 30 + b.phase * 5) * 3;
        Color wing = b.flashT > 0 ? Color{255, 230, 150, 255} : Color{200, 130, 70, 255};
        DrawTri({x, y}, {x - 5, y - 2 - flap}, {x - 2, y + 2}, wing); DrawTri({x, y}, {x + 5, y - 2 - flap}, {x + 2, y + 2}, wing);
        DrawRectangle((int)x - 1, (int)y - 1, 2, 3, Color{60, 40, 30, 255});
        if (b.flashT > 0) DrawCircle((int)x, (int)y, 7, Fade(Color{255, 220, 140, 255}, 0.35f));
        break;
    }
    case PS_CMOSS: { // spongy cannon-moss heaped round a sunken cannonball
        DrawCircle((int)x, (int)y - 5, 6, Color{60, 62, 66, 255});
        for (int k = 0; k < 14; k++) { float hx = fmodf(k * 7.3f + b.id, 32) - 16, hy = -fmodf(k * 3.1f, 7); DrawCircle((int)(x + hx), (int)(y + hy), 3, k % 2 ? Color{110, 140, 70, 255} : Color{80, 110, 56, 255}); }
        break;
    }
    case PS_ROT: { // ship-rot: pale shelf fungus on the timbers; burst, a stain
        if (b.act == BeastAct::Drift) { DrawEllipse((int)x, (int)y - 2, 10, 3, Color{90, 90, 60, 200}); break; }
        for (int k = 0; k < 4; k++) { float cy = y - 3 - k * 4; DrawEllipse((int)(x + (k % 2 ? 3 : -3)), (int)cy, 8 - k, 3, FAUNA_INK); DrawEllipse((int)(x + (k % 2 ? 3 : -3)), (int)cy, 7 - k, 2, Color{210, 200, 150, 255}); }
        break;
    }
    case PS_MKELP: { // mast-kelp lashed round a broken spar; snapped, the spar swings
        bool swing = b.act == BeastAct::Eat;
        float ang = swing ? b.facing * (1.4f - b.actT) * 1.2f : 0.0f;
        Vector2 pivot{x, y - 44}, end{x + sinf(ang) * 44, y - 44 + cosf(ang) * 44};
        DrawLineEx(pivot, end, 7, FAUNA_INK); DrawLineEx(pivot, end, 5, Color{120, 86, 52, 255}); // the spar
        if (b.act != BeastAct::Drift && !swing) for (int k = 0; k < 6; k++) DrawLineEx({x - 5, y - 6 - k * 7.0f}, {x + 5, y - 10 - k * 7.0f}, 2.5f, Color{70, 100, 50, 255}); // the kelp wrapped round it
        else DrawLineEx({x, y - 44}, {x + 6, y - 30}, 2, Color{70, 100, 50, 255}); // parted
        break;
    }
    case PS_BARNACLE: { // a razor-edged clump of barnacles
        for (int k = 0; k < 7; k++) {
            float hx = x - 10 + k * 3.3f, hh = 5 + (k * 5 % 4) * 2;
            DrawTri({hx - 3, y}, {hx + 3, y}, {hx, y - hh}, FAUNA_INK);
            DrawTri({hx - 2, y}, {hx + 2, y}, {hx, y - hh + 1}, Color{214, 206, 190, 255});
            DrawRectangle((int)hx, (int)(y - hh + 2), 1, 1, Color{60, 40, 50, 255});
        }
        break;
    }
    case PS_LANTERN: { // Siren's lantern weed: pulsing bulbs on a trailing stem
        float pulse = 0.6f + 0.4f * sinf(t * 2.5f + b.phase);
        DrawCircle((int)x, (int)y - 14, 16 * pulse + 6, Fade(Color{120, 255, 210, 255}, 0.12f));
        DrawLineEx({x, y}, {x + sinf(t) * 3, y - 16}, 2, Color{60, 120, 90, 255});
        for (int k = 0; k < 3; k++) DrawCircle((int)(x + sinf(t + k) * 3 + (k - 1) * 4), (int)(y - 12 - k * 3), 2.5f, Color{(unsigned char)(150 + 100 * pulse), 255, 220, 255});
        break;
    }
    case PS_KRAKEN: { // the Grand Kraken: a vast shape under the fleet, and its arms out of the sea
        float sea = p.waterY;
        float rise = b.act == BeastAct::Explore ? std::clamp(b.actT / 3, 0.0f, 1.0f) : b.act == BeastAct::Flee ? 1 - std::clamp(b.actT / 3, 0.0f, 1.0f) : 1.0f;
        float by = sea + 170 - rise * 70 + sinf(t * 0.5f) * 6;
        DrawEllipse((int)x, (int)by, 300, 130, Fade(Color{26, 14, 30, 255}, 0.55f * rise));
        DrawEllipse((int)x, (int)(by - 40), 200, 70, Fade(Color{44, 20, 40, 255}, 0.45f * rise));
        for (int e = -1; e <= 1; e += 2) { // two pale eyes, slitted, turning to the diver
            Rectangle d = PlatDiverBox(p);
            float look = std::clamp((d.x - x) / 400.0f, -1.0f, 1.0f) * 6;
            DrawEllipse((int)(x + e * 70), (int)(by - 50), 18, 11, Fade(Color{226, 196, 90, 255}, 0.8f * rise));
            DrawRectangle((int)(x + e * 70 + look) - 2, (int)(by - 60), 4, 20, Fade(Color{20, 10, 16, 255}, 0.9f * rise));
        }
        Color skin{124, 46, 60, 255};
        bool slam = b.target == 1 && (b.act == BeastAct::Coil || b.act == BeastAct::Strike || b.act == BeastAct::Eat || b.act == BeastAct::Wander);
        if (slam) {
            if (b.act == BeastAct::Coil) { // the tell: its shadow grows over where it will land, and the water boils where it rises
                float u = std::clamp(b.actT / 1.1f, 0.0f, 1.0f);
                DrawEllipse((int)b.goal.x, (int)b.goal.y + 4, 16 + 34 * u, 5, Fade(BLACK, 0.2f + 0.35f * u));
                for (int k = 0; k < 5; k++) DrawCircle((int)(b.anchor.x + sinf(t * 9 + k) * 14), (int)(b.anchor.y - fmodf(t * 40 + k * 9, 20)), 2, Color{220, 236, 250, 200});
            }
            DrawTentacle(b.anchor, b.territory, 19, t, (float)b.id, skin);
        }
        if (b.target == 2 && b.carry >= 0 && b.carry < (int)p.snaps.size() && (b.act == BeastAct::Coil || b.act == BeastAct::Eat)) { // two arms wrapped round the hull at the snap point
            const GenSnap& sn = p.snaps[b.carry];
            float cx = sn.col * (float)T + 16, deckY = (sn.bottom - 4 - p.genTop) * (float)T;
            float u = b.act == BeastAct::Coil ? std::clamp(b.actT / 2.2f, 0.0f, 1.0f) : 1.0f;
            for (int side = -1; side <= 1; side += 2) {
                Vector2 base{cx + side * 4.5f * T, sea + 10};
                Vector2 tip{cx + side * (1.4f - 1.2f * u) * T, deckY - 60 * (1 - u) - 12 + (b.act == BeastAct::Eat ? 40 * std::min(1.0f, b.actT / 0.4f) : 0)};
                DrawTentacle(base, tip, 17, t, side * 3.0f, skin);
            }
            if (b.act == BeastAct::Coil && u > 0.4f) // the timbers groan: splinters and a shudder along the tear line
                for (int k = 0; k < 4; k++) DrawRectangle((int)(cx - 20 + fmodf(t * 97 + k * 13, 40)), (int)(deckY - 4 - fmodf(t * 53 + k * 7, 12)), 2, 2, Color{190, 150, 100, 255});
            if (b.act == BeastAct::Coil) DrawLineEx({cx, deckY - 8}, {cx, deckY + 5 * (float)T}, 2, Fade(Color{255, 220, 150, 255}, 0.25f + 0.25f * sinf(t * 18) * u)); // the crack opening
        }
        break;
    }
    default: DrawCircle((int)x, (int)y, 6, Color{200, 200, 200, 255}); break;
    }
    if (b.pers.abnormal == Abnormal::RabidEnraged) DrawCircle((int)(x + f * 6), (int)(y - 6), 1.4f, Color{255, 40, 30, 255});
    if (b.pers.abnormal == Abnormal::SymbioticCompanion) DrawCircle((int)x, (int)y, 18, Fade(Color{255, 230, 150, 255}, 0.10f + 0.05f * sinf(t * 3)));
    (void)p;
}
// A rat-hole gnawed through the deck planking, or a kennel hatch: something's eyes in the dark when it's home.
void DrawPirateDen(const PlatformState& p, const Den& d, float t) {
    const BeastWorld& W = p.fauna;
    float x = d.pos.x, y = d.pos.y;
    DrawEllipse((int)x, (int)y + 2, 9, 5, FAUNA_INK);
    DrawEllipse((int)x, (int)y + 2, 7.5f, 3.8f, Color{22, 14, 10, 255});
    for (int k = 0; k < 4; k++) DrawTri({x - 9 + k * 5.0f, y - 1}, {x - 6 + k * 5.0f, y - 1}, {x - 7.5f + k * 5.0f, y + 2 + (k % 2)}, Color{120, 86, 54, 255}); // gnawed splinters
    int inside = 0;
    for (const auto& b : W.beasts) if (b.life == BeastLife::Alive && b.hidden && fabsf(b.pos.x - x) < 4) inside++;
    if (inside && fmodf(t * 0.8f + x * 0.011f, 3.5f) < 2.6f) { DrawCircle((int)x - 2, (int)y + 2, 0.9f, Color{250, 210, 120, 255}); DrawCircle((int)x + 2, (int)y + 2, 0.9f, Color{250, 210, 120, 255}); }
}
void DrawBeastProps(const PlatformState& p, float t) {
    for (const auto& k : p.fauna.props) { // a lit powder keg, its fuse spitting faster as it burns down
        float x = k.pos.x, y = k.pos.y;
        if (p.fauna.biome == PL_CAVE) { DrawCircle((int)x, (int)y, 3.5f, FAUNA_INK); DrawCircle((int)x, (int)y, 2.6f, Color{110, 104, 100, 255}); continue; } // an Echo-Stalker's thrown stone
        if (p.fauna.biome == PL_ATLANTIS) { DrawEllipse((int)x, (int)y - 1, 9, 2, Fade(Color{200, 240, 255, 255}, 0.45f * (1 - k.t / 25))); continue; } // a mosaic-snail's glassy slime
        DrawRectangle((int)x - 6, (int)y - 7, 12, 13, FAUNA_INK);
        DrawRectangle((int)x - 5, (int)y - 6, 10, 11, Color{120, 80, 44, 255});
        DrawRectangle((int)x - 5, (int)y - 3, 10, 1, Color{70, 70, 76, 255}); DrawRectangle((int)x - 5, (int)y + 2, 10, 1, Color{70, 70, 76, 255});
        float blink = fmodf(t * (4 + 12 / std::max(0.2f, k.t)), 1.0f);
        DrawLineEx({x + 2, y - 7}, {x + 4, y - 11}, 1.2f, Color{60, 50, 40, 255});
        DrawCircle((int)x + 4, (int)y - 11, blink < 0.5f ? 2.2f : 1.4f, Color{255, 200, 80, 255});
        if (!p.verifying && blink < 0.1f) DrawCircle((int)x + 4, (int)y - 11, 5, Fade(Color{255, 160, 40, 255}, 0.4f));
    }
}}  // namespace

void DrawFauna(const PlatformState& p, float t, int c0, int c1) {
    const BeastWorld& W = p.fauna;
    if (!W.active) return;
    float x0 = (c0 - 8) * (float)T, x1 = (c1 + 9) * (float)T; // wide: a giant's body reaches well past its centre
    for (const auto& k : W.ink) { // an octopus's ink: a dark bloom spreading and thinning
        float u = std::clamp(k.life / std::max(0.1f, k.max), 0.0f, 1.0f), r = k.r * (1.2f - 0.5f * u);
        if (k.kind == 4) { // a shockwave: a ring racing outward
            float rr = k.r * (1 - u);
            for (int i = 0; i < 3; i++) DrawCircleLines((int)k.pos.x, (int)k.pos.y, rr - i * 5, Fade(Color{190, 236, 255, 255}, u * (0.8f - i * 0.25f)));
            continue;
        }
        Color cc = k.kind == 1 ? Color{150, 190, 90, 255} : k.kind == 2 ? Color{120, 116, 110, 255} : k.kind == 3 ? Color{160, 84, 40, 255} : Color{24, 16, 32, 255}; // ink, spores, powder smoke, rust
        for (int i = 0; i < 4; i++) DrawCircle((int)(k.pos.x + sinf(t + i * 1.7f) * r * 0.3f), (int)(k.pos.y + cosf(t * 0.8f + i) * r * 0.2f - (k.kind == 2 ? (1 - u) * 20 : 0)), r * (0.5f + 0.15f * i), Fade(cc, (k.kind == 0 ? 0.16f : 0.22f) * u));
    }
    for (size_t k = 0; k < p.snaps.size() && k < p.snapped.size(); k++) { // a snapped ship: splintered timber ends either side of the tear
        if (!p.snapped[k]) continue;
        const GenSnap& sn = p.snaps[k];
        for (int side = -1; side <= 1; side += 2) {
            int col = sn.col + side * 2;
            float ex = side < 0 ? (col + 1) * (float)T : col * (float)T;
            for (int y = std::max(0, sn.top - p.genTop); y <= std::min(p.h - 1, sn.bottom - p.genTop + 1); y++) {
                if (!Solid(p, col, y)) continue;
                for (int j = 0; j < 3; j++) {
                    float jy = y * (float)T + 4 + j * 10, len = 5 + fmodf((float)(col * 7 + y * 13 + j * 5), 9.0f);
                    DrawTri({ex, jy}, {ex, jy + 8}, {ex - side * len, jy + 3}, Color{120, 84, 50, 255});
                    DrawTri({ex, jy + 1}, {ex, jy + 6}, {ex - side * (len - 2), jy + 3}, Color{196, 150, 96, 255});
                }
            }
        }
    }
    { // the Abyssal Arachnid's webs: all but invisible - a glint in the lamp when you're right on one, bright once shown up
        Rectangle d = PlatDiverBox(p);
        if (W.glowSuitT > 0) DrawCircle((int)(d.x + d.width / 2), (int)(d.y + d.height / 2), 18, Fade(Color{120, 240, 210, 255}, 0.12f * std::min(1.0f, W.glowSuitT / 3))); // glow-shrimp fluid on the suit
        for (const auto& w : W.webs) {
            float near = fabsf((d.x + d.width / 2) - w.top.x);
            float a = w.revealed ? 0.8f : near < 1.5f * (float)T ? 0.25f * (1 - near / (1.5f * T)) : 0.0f;
            if (a > 0) for (int k = 0; k < 5; k++) {
                float ox = (k - 2) * 5.0f;
                DrawLineEx({w.top.x + ox, w.top.y}, {w.top.x + ox * 0.4f, w.bottom.y}, 1, Fade(Color{220, 240, 255, 255}, a));
                if (k < 4) for (int j = 1; j < 5; j++) { float yy = w.top.y + (w.bottom.y - w.top.y) * j / 5.0f; DrawLineEx({w.top.x + ox, yy}, {w.top.x + ox + 5, yy + 2}, 1, Fade(Color{220, 240, 255, 255}, a * 0.7f)); }
            }
            Vector2 husk{w.top.x + 3, (w.top.y + w.bottom.y) / 2}; // a drained husk hanging in it: the warning you always see
            DrawEllipse((int)husk.x, (int)husk.y, 5, 8, FAUNA_INK); DrawEllipse((int)husk.x, (int)husk.y, 4, 7, Color{150, 140, 130, 255});
            DrawLineEx({husk.x - 3, husk.y + 2}, {husk.x - 7, husk.y + 8}, 1, Color{150, 140, 130, 255}); DrawLineEx({husk.x + 3, husk.y + 2}, {husk.x + 6, husk.y + 9}, 1, Color{150, 140, 130, 255});
            DrawLineEx({w.top.x, w.top.y}, {husk.x, husk.y - 8}, 1, Fade(Color{220, 240, 255, 255}, 0.35f)); // the line it hangs by
        }
    }
    for (const auto& sc : W.scars) { // the megalodon's bites: a torn, jagged dent in the plating, raw metal at the edges
        if (sc.x < x0 || sc.x > x1) continue;
        DrawEllipse((int)sc.x, (int)sc.y, 11, 13, FAUNA_INK);
        DrawEllipse((int)sc.x, (int)sc.y, 9, 11, Color{30, 32, 38, 255});
        for (int k = 0; k < 7; k++) { float a = k * 0.9f; DrawTri({sc.x + cosf(a) * 10, sc.y + sinf(a) * 12}, {sc.x + cosf(a + 0.3f) * 6, sc.y + sinf(a + 0.3f) * 7}, {sc.x + cosf(a - 0.3f) * 6, sc.y + sinf(a - 0.3f) * 7}, Color{176, 180, 184, 255}); }
    }
    for (const auto& d : W.dens) {
        if (d.pos.x < x0 || d.pos.x > x1) continue;
        if (W.biome == PL_HULL) DrawHullDen(p, d, t);
        else if (W.biome == PL_PIRATE) DrawPirateDen(p, d, t);
        else if (W.biome == PL_ISLAND) DrawIslandDen(p, d, t);
        else if (W.biome == PL_CAVE) DrawCaveDen(p, d, t);
        else if (W.biome == PL_PIPES) DrawPipesDen(p, d, t);
        else if (W.biome == PL_WEEDS) DrawWeedsDen(p, d, t);
        else if (W.biome == PL_ATLANTIS) DrawAtlantisDen(p, d, t);
    }
    DrawBeastProps(p, t);
    for (int pass = 0; pass < 2; pass++) // corpses first, the living over them
        for (const auto& b : W.beasts) {
            if (b.life == BeastLife::Gone || b.pos.x < x0 || b.pos.x > x1) continue;
            if ((pass == 0) != (b.life == BeastLife::Corpse)) continue;
            // drawn at its size - bigger the higher up the food chain - grown about its feet so it stays on the ground
            const SpeciesDef& SD = BeastSpecies(W.biome, b.species);
            float sz = BeastSize(W.biome, b.species);
            bool ground = SD.move == MoveMode::Walk || SD.move == MoveMode::Climb;
            Vector2 piv{b.pos.x, b.pos.y + (ground ? std::min(12.0f, SD.radius * b.scale * 0.6f) : 0.0f)};
            if (b.hidden) piv = {b.pos.x, b.pos.y - 8};
            rlPushMatrix(); rlTranslatef(piv.x, piv.y, 0); rlScalef(sz, sz, 1); rlTranslatef(-piv.x, -piv.y, 0);
            if (b.hidden && W.biome == PL_HULL && b.species == HS_SIPHON) { rlPopMatrix(); DrawSiphon(p, b, t); continue; }
            if (b.hidden) { if ((W.biome == PL_HULL && b.species == HS_EEL || W.biome == PL_ATLANTIS && b.species == AS_EEL) && b.act == BeastAct::Ambush) DrawEelPeek(b, t); rlPopMatrix(); continue; }
            // a body: death throes for the first moment, then it jerks with each bite and shrinks as it's eaten
            bool body = b.life == BeastLife::Corpse, xf = false;
            if (body) {
                float throes = std::max(0.0f, 1 - b.corpseT / 0.9f);
                bool bitten = false;
                for (const auto& o : W.beasts) if (o.life == BeastLife::Alive && o.act == BeastAct::Eat && o.target >= 0 && o.target < (int)W.beasts.size() && &W.beasts[o.target] == &b) { bitten = true; break; }
                float jx = throes * sinf(t * 47 + b.id) * 2.5f + (bitten ? sinf(t * 23) * 1.2f : 0), jy = bitten ? fabsf(sinf(t * 17)) * -1.0f : 0;
                float k = 0.55f + 0.45f * std::clamp(b.meat, 0.0f, 1.0f);
                rlPushMatrix(); xf = true;
                rlTranslatef(b.pos.x + jx, b.pos.y + jy, 0);
                rlRotatef(throes * sinf(t * 31 + b.id) * 18.0f, 0, 0, 1);
                rlScalef(k, k, 1);
                rlTranslatef(-b.pos.x, -b.pos.y, 0);
                if (throes > 0.6f && !p.verifying) DrawCircleLines((int)b.pos.x, (int)b.pos.y, 10 + (1 - throes) * 30, Fade(WHITE, (throes - 0.6f) * 1.2f)); // the moment it dies
            }
            if (W.biome == PL_HULL) DrawHullBeast(p, b, t);
            else if (W.biome == PL_PIRATE) DrawPirateBeast(p, b, t);
            else if (W.biome == PL_ISLAND) DrawIslandBeast(p, b, t);
            else if (W.biome == PL_CAVE) DrawCaveBeast(p, b, t);
            else if (W.biome == PL_PIPES) DrawPipesBeast(p, b, t);
            else if (W.biome == PL_WEEDS) DrawWeedsBeast(p, b, t);
            else if (W.biome == PL_ATLANTIS) DrawAtlantisBeast(p, b, t);
            if (xf) rlPopMatrix();
            rlPopMatrix();
        }
}

// A tapering, swaying tentacle from base to tip, with a row of pale suckers and a curled tip.
void DrawTentacle(Vector2 base, Vector2 tip, float w0, float t, float seed, Color c) {
    const int N = 18;
    Color hi = ColorBrightness(c, 0.25f), sucker{176, 146, 140, 255};
    Vector2 prev = base;
    float len = sqrtf((tip.x - base.x) * (tip.x - base.x) + (tip.y - base.y) * (tip.y - base.y));
    Vector2 dir{(tip.x - base.x) / std::max(1.0f, len), (tip.y - base.y) / std::max(1.0f, len)}, n{-dir.y, dir.x};
    for (int i = 1; i <= N; i++) {
        float u = (float)i / N, sway = sinf(t * 3 + seed + u * 5) * 14 * u;
        Vector2 q{base.x + (tip.x - base.x) * u + n.x * sway, base.y + (tip.y - base.y) * u + n.y * sway};
        float w = w0 * (1 - u * 0.75f);
        DrawLineEx(prev, q, w * 2, c);
        DrawLineEx({prev.x - n.x * w * 0.4f, prev.y - n.y * w * 0.4f}, {q.x - n.x * w * 0.4f, q.y - n.y * w * 0.4f}, std::max(1.0f, w * 0.5f), hi);
        if (i % 2 == 0) DrawCircleV({q.x + n.x * w * 0.7f, q.y + n.y * w * 0.7f}, std::max(1.2f, w * 0.3f), sucker);
        prev = q;
    }
    DrawRing(prev, 3, 6, 0, 270, 8, c); // the curled tip
}

// The Kraken's bulk, looming in the abyss behind the arena: drawn before the tiles.
// Only the abyss it's rising from is drawn here now: the great arms churning the dark water below the
// platforms, foreshadowing what's about to surface. The creature itself -- the thing you actually fight
// -- is drawn life-sized by DrawBoss below; there is no separate, smaller stand-in.
void DrawBossBack(const PlatformState& p, float t) {
    const PlatBoss& b = p.boss;
    if (b.type != 'K') return;
    float arenaX = (p.w - CH_W) * (float)T;
    float sink = b.defeated && b.state == 4 ? std::min(1.0f, b.timer / 3.0f) * 300 : 0;
    Vector2 body{arenaX + 13 * T, KrakenOrigin(p) + 17.5f * T + sink};
    Color deeper{30, 13, 40, 255};
    for (int k = 0; k < 7; k++) { // great arms churning in the abyss beneath everything
        float bx = body.x - 300 + k * 100, sw = sinf(t * 0.6f + k) * 60;
        DrawTentacle({bx, body.y}, {bx + sw, KrakenOrigin(p) + 3.0f * T + (k % 3) * 40 + sink}, 20, t * 0.4f, k * 1.7f, deeper);
    }
}

// The Kraken's head, drawn as a textured horror rather than a flat oval: a layered mantle with cross-hatched
// shadow and mottled skin, translucent glowing organs showing through, barnacles and suckers, a heavy brow
// shadow over great slit eyes that follow you, and a dark beak. `beakDir` (+1/-1) draws it charging sideways.
void DrawKrakenHeadArt(float cx, float top, float S, float t, bool blink, float look, float beakDir = 0) {
    Color skin = blink ? WHITE : Color{92, 62, 86, 255}, mid{70, 46, 66, 255}, dk{46, 30, 46, 255}, ink{12, 8, 14, 255};
    float pulse = 0.5f + 0.5f * sinf(t * 3);
    float f = beakDir != 0 ? beakDir : 0;
    for (int k = -3; k <= 3; k++) // a crown of arms writhing behind and below the head
        DrawTentacle({cx + k * 18 * S - f * 30 * S, top + 60 * S}, {cx + k * 46 * S - f * 90 * S, top + (110 + fabsf((float)k) * 8) * S}, 9 * S, t * 3, k * 2.0f, dk);
    DrawEllipse((int)cx, (int)(top + 28 * S), 57 * S, 43 * S, ink);                      // heavy ink outline
    DrawEllipse((int)cx, (int)(top + 28 * S), 54 * S, 40 * S, skin);                      // the great mantle
    DrawEllipse((int)(cx + 12 * S), (int)(top + 36 * S), 42 * S, 30 * S, mid);             // its shadowed underside
    DrawEllipse((int)(cx + 20 * S), (int)(top + 46 * S), 30 * S, 18 * S, dk);
    for (int k = 0; k < 9; k++) DrawLineEx({cx - 44 * S + k * 10 * S, top + 8 * S}, {cx - 30 * S + k * 10 * S, top + 44 * S}, 1.4f * S, Fade(ink, 0.55f)); // cross-hatching
    for (int k = 0; k < 7; k++) DrawLineEx({cx - 40 * S + k * 12 * S, top + 46 * S}, {cx - 52 * S + k * 12 * S, top + 12 * S}, 1.2f * S, Fade(ink, 0.35f));
    for (int k = 0; k < 6; k++) DrawCircle((int)(cx - 36 * S + k * 14 * S), (int)(top + (20 + (k % 2) * 10) * S), 3 * S, dk);            // mottling
    for (int k = 0; k < 3; k++) { // translucent glowing organs beneath the skin
        Vector2 o{cx - 20 * S + k * 20 * S, top + 20 * S + (k & 1) * 8 * S};
        DrawEllipse((int)o.x, (int)o.y, 8 * S, 5 * S, Fade(Color{90, 190, 170, 255}, 0.28f + 0.2f * pulse));
        DrawEllipse((int)o.x, (int)o.y, 4 * S, 2.6f * S, Fade(Color{170, 240, 210, 255}, 0.3f + 0.2f * pulse));
    }
    for (int k = 0; k < 5; k++) { // barnacles encrusting the crown
        float bx = cx - 28 * S + k * 14 * S, by = top + (2 + (k * 7) % 6) * S;
        DrawTri({bx - 4 * S, by + 6 * S}, {bx + 4 * S, by + 6 * S}, {bx, by - 2 * S}, Color{184, 176, 152, 255});
        DrawTri({bx, by - 2 * S}, {bx + 4 * S, by + 6 * S}, {bx + 1 * S, by + 6 * S}, ink);
    }
    for (int s = -1; s <= 1; s += 2) { // vast eyes under a heavy black brow
        float ex = cx + s * 24 * S, ey = top + 42 * S;
        DrawEllipse((int)ex, (int)ey, 14 * S, 11 * S, ink);
        DrawEllipse((int)ex, (int)ey, 12 * S, 9 * S, Color{206, 176, 70, 255});
        DrawRectangle((int)(ex - 2 * S + look * S), (int)(ey - 8 * S), (int)(4 * S), (int)(16 * S), ink);
        DrawTri({ex - 14 * S, ey - 4 * S}, {ex + 14 * S, ey - 4 * S}, {ex + s * 4 * S, ey - 16 * S}, ink);                                  // the brow
    }
    for (int k = -2; k <= 2; k++) DrawCircle((int)(cx + k * 12 * S), (int)(top + 62 * S), 2.6f * S, Color{150, 120, 116, 255});             // suckers
    Vector2 bt{cx, top + 70 * S};
    if (beakDir == 0) DrawTri({cx - 9 * S, top + 58 * S}, {cx + 9 * S, top + 58 * S}, bt, Color{34, 26, 26, 255});                          // the beak, pointing down
    else {                                                                                                                                 // ...or forward, when it charges
        DrawTri({cx + f * 40 * S, top + 30 * S}, {cx + f * 40 * S, top + 54 * S}, {cx + f * 100 * S, top + 44 * S}, ink);
        DrawTri({cx + f * 40 * S, top + 32 * S}, {cx + f * 40 * S, top + 46 * S}, {cx + f * 94 * S, top + 42 * S}, Color{62, 46, 40, 255});
    }
}

// Blackbeard, articulated: a skeleton of hip, chest, head and jointed arms and legs, animated per BBAnim, dressed
// in a long red coat, a waist sash, tall boots, a feathered hat, a fusing beard, a cutlass and a brace of pistols.
BBAnim BBAnimOf(const PlatBoss& b) {
    switch (b.state) {
        case 0: return fabsf(b.vel.x) > 1 ? BBAnim::Walk : BBAnim::Idle;
        case 1: return BBAnim::Windup;
        case 2: return BBAnim::Charge;
        case 4: return BBAnim::AimPistol;
        case 5: return BBAnim::Dazed;
        default: return BBAnim::Recover;
    }
}

void DrawBlackbeard(const PlatformState& p, const PlatBoss& b, float t) {
    const Color INKC{8, 8, 12, 255};
    BBAnim an = BBAnimOf(b);
    float f = b.dir, cx = b.pos.x + BB_W / 2, fy = b.pos.y + BB_H;
    auto P = [&](float x, float y) { return Vector2{cx + f * x, fy + y}; };
    auto limb = [&](Vector2 a, Vector2 c, float w, Color col) { DrawLineEx(a, c, w + 3.2f, INKC); DrawLineEx(a, c, w, col); DrawCircleV(c, w * 0.5f, col); };
    float w = t * (an == BBAnim::Charge ? 17.0f : 9.0f);
    float lean = an == BBAnim::Charge ? 9 : an == BBAnim::Windup ? -5 : an == BBAnim::Dazed ? sinf(t * 3) * 5 : an == BBAnim::AimPistol ? 3 : 0;
    float crouch = an == BBAnim::Windup ? 8.0f + sinf(t * 30) : an == BBAnim::Dazed ? 4.0f : 0.0f;
    float bob = (an == BBAnim::Walk || an == BBAnim::Charge) ? fabsf(sinf(w)) * 2.4f : sinf(t * 2) * 0.8f;
    Vector2 hip = P(0, -35 + crouch - bob), chest = P(lean * 0.6f, -55 + crouch * 0.6f - bob), head = P(lean, -66 + crouch * 0.5f - bob);
    float stride = an == BBAnim::Charge ? 13.0f : an == BBAnim::Walk ? 8.0f : an == BBAnim::Windup ? 6.0f : an == BBAnim::Dazed ? 7.0f : 0.0f;
    Color breeches{56, 46, 54, 255}, boot{22, 18, 18, 255}, coat{128, 30, 34, 255}, coatDk{78, 16, 20, 255}, skin{206, 160, 122, 255};
    if (gGhost) { // Ghost Blackbeard: bone, a drowned teal coat, and a beard of green ghostfire
        breeches = {36, 50, 58, 255}; coat = {44, 84, 92, 255}; coatDk = {22, 46, 54, 255}; skin = {224, 220, 196, 255};
        DrawEllipse((int)cx, (int)(fy - 38), 26, 46, Fade(Color{110, 255, 190, 255}, 0.10f + 0.04f * sinf(t * 4)));
    }
    // ---- far arm and far leg
    auto leg = [&](float phase, Color pants) {
        float sw = sinf(w + phase) * stride, lift = std::max(0.0f, cosf(w + phase)) * (stride > 0 ? 6.0f : 0.0f);
        if (an == BBAnim::Dazed) sw = (phase > 0 ? 9.0f : -9.0f);
        Vector2 knee = P(sw * 0.45f + (crouch > 4 ? 5.0f : 0.0f), -18 + crouch * 0.4f - lift * 0.5f), foot = P(sw, -3 - lift);
        limb(hip, knee, 8, pants);
        limb(knee, foot, 7, boot);
        DrawRectangle((int)(foot.x - 5 + (f > 0 ? 0 : -3)), (int)foot.y - 2, 12, 6, INKC); // the boot's toe
    };
    Vector2 shBack = P(lean * 0.6f - 5, -58 + crouch * 0.6f - bob), shFront = P(lean * 0.6f + 5, -58 + crouch * 0.6f - bob);
    Vector2 handB{}, handF{};
    switch (an) {
        case BBAnim::Windup: handF = P(-12, -78); handB = P(-8, -44); break;                        // cutlass raised behind
        case BBAnim::Charge: handF = P(22, -50 - sinf(w) * 2); handB = P(-10, -46); break;           // lunging forward
        case BBAnim::AimPistol: { Vector2 aim{p.pos.x + PW / 2, p.pos.y + PH / 2}; float ang = atan2f(aim.y - shFront.y, aim.x - shFront.x); handF = {shFront.x + cosf(ang) * 26, shFront.y + sinf(ang) * 26}; handB = P(-6, -44); break; }
        case BBAnim::Dazed: handF = P(10, -30); handB = P(-6, -30); break;
        case BBAnim::Walk: handF = P(12 + sinf(w) * 5, -46); handB = P(-8 - sinf(w) * 5, -44); break;
        default: handF = P(14, -48); handB = P(-8, -44); break;
    }
    Vector2 elbowB = P((handB.x - cx) / f * 0.5f - 6, -50 + crouch * 0.4f), elbowF = {(shFront.x + handF.x) / 2, (shFront.y + handF.y) / 2 + 6};
    limb(shBack, elbowB, 7, coatDk); limb(elbowB, handB, 6, coatDk);
    DrawCircleV(handB, 4, skin);
    if (an != BBAnim::AimPistol) { // a pistol holstered in the sash
        DrawRectangle((int)(hip.x - f * 9) - 2, (int)hip.y - 8, 5, 12, INKC);
    }
    leg(PI, breeches);
    // ---- the long coat: tails that sway and flare with the charge
    float flare = an == BBAnim::Charge ? 14.0f : 4.0f, sway = sinf(t * 4) * 2 + (an == BBAnim::Walk ? sinf(w) * 3 : 0);
    Vector2 c1 = P(-13 - flare, -6 + sway), c2 = P(13 + flare * 0.3f, -8 - sway), c3 = P(11, -34), c4 = P(-11, -34);
    DrawTri(c4, c3, c2, INKC); DrawTri(c4, c2, c1, INKC);
    Vector2 d1 = P(-11 - flare * 0.8f, -9 + sway), d2 = P(11, -11 - sway), d3 = P(9.5f, -33), d4 = P(-9.5f, -33);
    DrawTri(d4, d3, d2, coat); DrawTri(d4, d2, d1, coat);
    DrawTri(P(0, -33), P(-9.5f, -33), P(-6 - flare * 0.4f, -10), coatDk);
    // ---- torso, sash, buttons, gold trim
    limb(hip, chest, 17, coat);
    if (gGhost) for (int k = 0; k < 4; k++) DrawLineEx(P(-5, -54 + k * 5.0f), P(6, -54 + k * 5.0f), 1.6f, Color{224, 220, 196, 255}); // ribs through the torn coat
    DrawLineEx(P(4, -50), P(3, -36), 2.4f, coatDk);
    DrawRectangle((int)std::min(hip.x - 9, hip.x + 9), (int)hip.y - 6, 18, 7, Color{176, 132, 52, 255});               // the sash
    DrawRectangle((int)std::min(hip.x - 9, hip.x + 9), (int)hip.y - 6, 18, 2, Color{110, 26, 30, 255});
    DrawTri(P(6, -30), P(12, -30), P(9 + sinf(t * 3) * 2, -20), Color{176, 132, 52, 255});                            // its loose end
    for (int k = 0; k < 3; k++) DrawCircleV(P(6, -52 + k * 6.0f), 1.6f, Color{200, 170, 90, 255});
    DrawTri(P(-9, -60), P(9, -60), P(0, -55), Color{176, 132, 52, 255});                                            // epaulettes
    leg(0, breeches);
    // ---- near arm with the cutlass or the pistol
    limb(shFront, elbowF, 7.5f, coat); limb(elbowF, handF, 6.5f, coat);
    DrawCircleV(handF, 4.5f, skin);
    if (an == BBAnim::AimPistol) { // a pistol, levelled
        float ang = atan2f(handF.y - shFront.y, handF.x - shFront.x);
        Vector2 muzzle{handF.x + cosf(ang) * 14, handF.y + sinf(ang) * 14};
        DrawLineEx(handF, muzzle, 5.5f, INKC); DrawLineEx(handF, muzzle, 3.2f, Color{90, 90, 100, 255});
        float u = std::min(1.0f, b.timer / 0.5f);
        for (float dd = 18; dd < 18 + 80 * u; dd += 10) DrawRectangle((int)(handF.x + cosf(ang) * dd), (int)(handF.y + sinf(ang) * dd), 2, 2, Fade(Color{255, 90, 60, 255}, 0.5f));
        if (fmodf(t * 12, 1.0f) < 0.5f) DrawCircleV(muzzle, 2.6f, Color{255, 230, 150, 255});
    } else { // the cutlass: a curved blade
        float ang = an == BBAnim::Windup ? -2.2f : an == BBAnim::Charge ? 0.05f : an == BBAnim::Dazed ? 1.4f : -0.6f;
        Vector2 dir{f * cosf(ang), sinf(ang)}, tip{handF.x + dir.x * 34, handF.y + dir.y * 34}, mid{handF.x + dir.x * 18 - dir.y * 3, handF.y + dir.y * 18 + dir.x * 3};
        DrawLineEx(handF, mid, 6, INKC); DrawLineEx(mid, tip, 5, INKC);
        DrawLineEx(handF, mid, 3, Color{190, 196, 202, 255}); DrawLineEx(mid, tip, 2.4f, Color{214, 218, 224, 255});
        DrawLineEx({handF.x - dir.y * 6, handF.y + dir.x * 6}, {handF.x + dir.y * 6, handF.y - dir.x * 6}, 3.4f, Color{176, 132, 52, 255});   // the guard
    }
    // ---- head: hat, feather, face in shadow, beard with fuses
    DrawCircleV(head, 9.5f, INKC);
    DrawCircleV(head, 8, skin);
    DrawEllipse((int)head.x, (int)(head.y + 8), 11, 10, INKC);                                 // the famous beard...
    DrawEllipse((int)head.x, (int)(head.y + 7), 9, 8, gGhost ? Color{40, 130, 96, 255} : Color{22, 20, 22, 255});
    for (int k = 0; k < 3; k++) {                                                              // ...with fuses smoking in it
        Vector2 fz{head.x - 6 + k * 6.0f, head.y + 12};
        DrawCircleV(fz, 1.5f, Color{255, 140, 40, 255});
        float ph = fmodf(t * 0.8f + k * 0.3f, 1.0f);
        DrawCircleV({fz.x + sinf(ph * 6 + k) * 3, fz.y - 5 - ph * 18}, 2 + ph * 3, Fade(Color{150, 150, 150, 255}, 0.55f * (1 - ph)));
    }
    DrawRectangle((int)head.x - 8, (int)head.y - 4, 16, 5, INKC);                              // the brow shadow: no eyes, just a glint
    DrawRectangle((int)(head.x + f * 3), (int)head.y - 3, 2, 2, an == BBAnim::Windup || an == BBAnim::Charge ? Color{255, 70, 50, 255} : gGhost ? Color{120, 255, 190, 255} : Color{214, 210, 190, 255});
    DrawTri({head.x - 15, head.y - 5}, {head.x + 15, head.y - 5}, {head.x, head.y - 20}, INKC);        // the tricorn
    DrawTri({head.x - 13, head.y - 6}, {head.x + 13, head.y - 6}, {head.x, head.y - 17}, Color{34, 30, 40, 255});
    DrawRectangle((int)head.x - 15, (int)head.y - 7, 30, 4, INKC);
    Vector2 fe0{head.x - f * 12, head.y - 15};
    for (int k = 0; k < 4; k++) { // the feather plume, curving back
        float a = k * 0.28f;
        DrawLineEx({head.x - f * 2, head.y - 17}, {fe0.x - f * (4 + k * 3), fe0.y - 8 + k * 4 + sinf(t * 3 + k) * 1.5f}, 3.2f - k * 0.5f, k == 0 ? INKC : Color{224, 220, 206, 255});
        (void)a;
    }
    DrawCircleV(P(lean, -71 + crouch * 0.5f - bob), 2.2f, Color{230, 226, 210, 255});         // a skull badge
    if (an == BBAnim::Dazed) { // stars circle his head
        for (int k = 0; k < 4; k++) {
            float a = t * 5 + k * PI / 2;
            Vector2 st{head.x + cosf(a) * 20, head.y - 20 + sinf(a) * 5};
            DrawRectangle((int)st.x - 1, (int)st.y - 3, 2, 6, Color{255, 230, 90, 255});
            DrawRectangle((int)st.x - 3, (int)st.y - 1, 6, 2, Color{255, 230, 90, 255});
        }
    }
    if (an == BBAnim::Windup) for (int k = 0; k < 2; k++) DrawRectangle((int)(cx - f * 14 + GetRandomValue(-6, 6)), (int)(fy - 3), 3, 3, Color{190, 170, 140, 255});
}
void DrawBoss(const PlatformState& p, float t) {
    const PlatBoss& b = p.boss;
    if (b.type == 'K') {
        Color arm{86, 58, 80, 255};
        for (int i = 0; i < 2; i++) {
            if (b.tentT[i] < 0) continue;
            if (b.tentT[i] < 0.75f) { // warning: churning water below, or a shadow falling from above
                if (b.tentTop[i]) {
                    float a = 0.3f + 0.5f * b.tentT[i] / 0.75f;
                    DrawRectangle((int)(b.tentX[i] - 16), (int)KrakenOrigin(p) + T, 32, 13 * T, Fade(Color{20, 0, 30, 255}, a * 0.35f));
                    for (int k = 0; k < 3; k++) DrawCircle((int)(b.tentX[i] + Rnd(-14, 14)), (int)(KrakenOrigin(p) + T + Rnd(0, 40)), 3, Fade(Color{200, 120, 220, 255}, a));
                } else {
                    float surface = KrakenOrigin(p) + 16.0f * T - 6;
                    for (int k = 0; k < 5; k++)
                        DrawCircle((int)(b.tentX[i] + Rnd(-14, 14)), (int)(surface - Rnd(0, 30) * b.tentT[i]), 3, Fade(Color{200, 120, 220, 255}, 0.8f));
                }
                continue;
            }
            Rectangle r = TentacleBox(p, i);
            Vector2 base = b.tentTop[i] ? Vector2{b.tentX[i], KrakenOrigin(p)} : Vector2{b.tentX[i], KrakenOrigin(p) + 16.0f * T + 30};
            Vector2 tip = b.tentTop[i] ? Vector2{b.tentX[i], r.y + r.height} : Vector2{b.tentX[i], r.y};
            DrawTentacle(base, tip, 16, t * 2, i * 3.0f, arm);
        }
        if (b.inkT >= 0) { // a spreading cloud of ink, flooding everything but the safe half
            float arenaX = (p.w - CH_W) * (float)T, arenaMid = arenaX + 11.5f * T;
            float x0 = b.inkSafeRight ? arenaX : arenaMid, x1 = b.inkSafeRight ? arenaMid : arenaX + 22.0f * T;
            float grow = std::clamp(b.inkT / 0.5f, 0.0f, 1.0f), fade = 1 - std::clamp((b.inkT - 1.7f) / 0.3f, 0.0f, 1.0f);
            float top = KrakenOrigin(p) + (17.0f - 8.0f * grow) * T;
            DrawRectangle((int)x0, (int)top, (int)(x1 - x0), (int)(KrakenOrigin(p) + 17.0f * T - top), Fade(Color{18, 8, 26, 255}, 0.88f * fade));
            for (int k = 0; k < 10; k++) DrawCircle((int)Rnd(x0, x1), (int)Rnd(top, KrakenOrigin(p) + 17.0f * T), Rnd(3, 8), Fade(Color{60, 20, 80, 255}, 0.5f * fade));
        }
        if (b.lungeT >= 0) { // the head skims just under the surface, sweeping across
            float u = std::clamp((b.lungeT - 0.15f) / 1.0f, 0.0f, 1.0f), lx = b.lungeFromX + (b.lungeToX - b.lungeFromX) * u;
            float warn = 1 - std::clamp(b.lungeT / 0.15f, 0.0f, 1.0f);
            Color wake{90, 40, 110, 255};
            if (warn > 0) DrawCircle((int)b.lungeFromX, (int)(KrakenOrigin(p) + 10.5f * T), 30 + 20 * (1 - warn), Fade(wake, 0.5f * warn));
            else {
                DrawEllipse((int)lx, (int)(KrakenOrigin(p) + 10.5f * T), 44, 26, wake);
                DrawEllipse((int)lx, (int)(KrakenOrigin(p) + 10.5f * T), 30, 16, Fade(Color{150, 90, 170, 255}, 0.8f));
                for (int k = 0; k < 4; k++) DrawCircle((int)(lx - (b.lungeToX > b.lungeFromX ? 1 : -1) * k * 14), (int)(KrakenOrigin(p) + 10.5f * T + Rnd(-8, 8)), 6, Fade(WHITE, 0.4f));
            }
        }
        if (b.reach.active) { // the tracking tentacles, and the marks they are homing on
            const TentacleReachState& r = b.reach;
            if (r.t < 0.8f) {
                float a = 0.25f + 0.5f * (r.t / 0.8f);
                DrawRing(r.target, 16, 22, 0, 360, 18, Fade(Color{190, 150, 120, 255}, a));
                DrawRing(r.target, 4, 8, 0, 360, 12, Fade(Color{190, 150, 120, 255}, a));
            }
            for (int i = 0; i < 2; i++) DrawTentacle(r.base[i], r.tip[i], 15, t * 2, i * 2.0f, arm);
        }
        if (b.beak.active) { // a warning bar at the height it has locked, then the beaked head itself
            const BeakChargeState& s = b.beak;
            float arenaX2 = (p.w - CH_W) * (float)T;
            if (s.t < 0.9f) {
                float a = 0.18f + 0.28f * fabsf(sinf(t * 14));
                DrawRectangle((int)arenaX2, (int)(s.y - 30), 24 * T, 60, Fade(Color{150, 40, 40, 255}, a));
                for (int k = 0; k < 8; k++) { float x = s.dir > 0 ? arenaX2 + 24 + k * 40.0f : arenaX2 + 24 * T - 24 - k * 40.0f; DrawTri({x, s.y - 14}, {x, s.y + 14}, {x + s.dir * 22, s.y}, Fade(Color{220, 80, 70, 255}, a + 0.3f)); }
            } else if (s.t < 2.05f) {
                DrawKrakenHeadArt(s.x, s.y - 34, 1.0f, t, false, 0, s.dir);
            }
        }
        if (b.state >= 1) {
            Rectangle h = KrakenHead(p);
            bool blink = b.invuln > 0 && fmodf(t, 0.15f) < 0.075f;
            DrawKrakenHeadArt(h.x + h.width / 2, h.y, KRAKEN_SCALE, t, blink, std::clamp((p.pos.x - (h.x + h.width / 2)) * 0.015f, -6.0f, 6.0f));
            DrawTri({h.x + h.width / 2 - 12 * KRAKEN_SCALE, h.y - 6 * KRAKEN_SCALE}, {h.x + h.width / 2, h.y + 6 * KRAKEN_SCALE}, {h.x + h.width / 2 + 12 * KRAKEN_SCALE, h.y - 6 * KRAKEN_SCALE}, Color{190, 156, 70, 200}); // stomp here
        }
    } else if (b.type == 'B' && !b.defeated) {
        bool blink = b.invuln > 0 && fmodf(t, 0.15f) < 0.075f;
        if (!blink) DrawBlackbeard(p, b, t);
    }
}
// Musket balls, bombs and their blasts.
void DrawShots(const PlatformState& p, float t) {
    for (const auto& s : p.shots) {
        if (s.kind == 4) { // a torpedo: a steel cylinder with a nose cone and a churning propeller
            int px = (int)s.pos.x, py = (int)s.pos.y;
            DrawRectangle(px - 16, py - 6, 30, 12, Color{8, 8, 12, 255});
            DrawRectangle(px - 14, py - 4, 26, 8, Color{120, 132, 140, 255});
            DrawRectangle(px - 14, py - 4, 26, 3, Color{176, 190, 198, 255});
            DrawTri({px - 16.0f, py - 6.0f}, {px - 16.0f, py + 6.0f}, {px - 24.0f, (float)py}, Color{8, 8, 12, 255});
            DrawTri({px - 15.0f, py - 4.0f}, {px - 15.0f, py + 4.0f}, {px - 22.0f, (float)py}, Color{220, 84, 60, 255});
            DrawRectangle(px + 6, py - 4, 3, 8, Color{220, 190, 60, 255});
            DrawRectangle(px + 14, py - 5 + (int)(sinf(t * 40) * 2), 2, 10, Color{60, 66, 72, 255});
            BeginBlendMode(BLEND_ADDITIVE); DrawCircleV({s.pos.x + 18, s.pos.y}, 12, Color{255, 140, 60, 80}); EndBlendMode();
        } else if (s.kind == 5) { // an iron cannonball
            DrawCircleV(s.pos, 9, Color{8, 8, 12, 255});
            DrawCircleV(s.pos, 7, Color{54, 56, 62, 255});
            DrawCircleV({s.pos.x - 2, s.pos.y - 3}, 2.5f, Color{170, 176, 184, 255});
        } else if (s.kind == 6) { // a barrel, its staves turning as it rolls
            float rot = s.pos.x * 0.07f;
            DrawCircleV(s.pos, 14, Color{8, 8, 12, 255});
            DrawCircleV(s.pos, 12, Color{132, 86, 46, 255});
            for (int k = 0; k < 4; k++) { float a = rot + k * PI / 2; DrawLineEx({s.pos.x + cosf(a) * 12, s.pos.y + sinf(a) * 12}, {s.pos.x - cosf(a) * 12, s.pos.y - sinf(a) * 12}, 1.5f, Color{74, 46, 24, 255}); }
            DrawCircleLines((int)s.pos.x, (int)s.pos.y, 7, Color{60, 58, 62, 255});
            DrawCircleV({s.pos.x + cosf(rot) * 8, s.pos.y + sinf(rot) * 8}, 2, Color{190, 60, 50, 255});
        } else if (s.kind == 0) {
            Vector2 back{s.pos.x - s.vel.x * 0.03f, s.pos.y - s.vel.y * 0.03f};
            DrawLineEx(back, s.pos, 2, Fade(Color{230, 230, 220, 255}, 0.5f));
            DrawCircleV(s.pos, 3, Color{40, 40, 44, 255});
            DrawCircleV({s.pos.x - 1, s.pos.y - 1}, 1, Color{200, 200, 210, 255});
        } else if (s.kind == 3) { // ink: a dark teardrop with a purple sheen and a streak behind it
            DrawLineEx({s.pos.x, s.pos.y - 26}, {s.pos.x, s.pos.y - 8}, 3, Fade(Color{40, 24, 50, 255}, 0.55f));
            DrawEllipse((int)s.pos.x, (int)s.pos.y, 7, 10, Color{10, 6, 14, 255});
            DrawTri({s.pos.x - 5, s.pos.y - 6}, {s.pos.x + 5, s.pos.y - 6}, {s.pos.x, s.pos.y - 18}, Color{10, 6, 14, 255});
            DrawEllipse((int)s.pos.x - 2, (int)s.pos.y - 1, 2, 4, Color{110, 70, 130, 255});
        } else if (s.kind == 1) {
            bool flash =fmodf(t * (4 + (BOMB_FUSE - s.life) * 10), 1.0f) < 0.5f;
            DrawCircleV(s.pos, 7, flash && s.life < 0.5f ? Color{200, 60, 50, 255} : Color{30, 30, 34, 255});
            DrawCircleV({s.pos.x - 2, s.pos.y - 2}, 2, Color{120, 120, 130, 255});
            DrawLineEx({s.pos.x + 3, s.pos.y - 6}, {s.pos.x + 6, s.pos.y - 10}, 2, Color{120, 90, 60, 255});
            DrawCircleV({s.pos.x + 6 + GetRandomValue(-1, 1), s.pos.y - 11 + GetRandomValue(-1, 1)}, 2.5f, Color{255, 200, 80, 255});
        } else {
            float u = 1 - s.life / 0.3f;
            DrawCircleV(s.pos, BLAST_R * (0.5f + u * 0.5f), Fade(Color{255, 150, 50, 255}, 0.8f * (1 - u)));
            DrawCircleV(s.pos, BLAST_R * 0.5f * (1 - u), Fade(Color{255, 240, 180, 255}, 0.9f));
        }
    }
}}  // namespace

// ============================================================ public
const char* PlatLevelName(int level) { return Lv(level).name; }
// What the creature engine (beasts.cpp) borrows from the platformer.
bool PlatSolid(const PlatformState& p, int tx, int ty) { return Solid(p, tx, ty); }
char PlatTileAt(const PlatformState& p, int tx, int ty) { return At(p, tx, ty); }
Rectangle PlatDiverBox(const PlatformState& p) { return PlayerBox(p); }
void PlatBurst(PlatformState& p, Vector2 at, int n, Color c, float speed, float life, float size) { Burst(p, at, n, c, speed, life, size); }
void PlatBubbles(PlatformState& p, Vector2 at, int n) { Bubbles(p, at, n); }
void PlatBuildLevel(PlatformState& p) { BuildLevel(p); }
void PlatSnapShip(PlatformState& p, int k) { SnapShip(p, k); }

namespace { bool ValidateGenerated(int level, const GenLevel& gl, int* failedHop, float* solveTime = nullptr, int* hopsOut = nullptr, unsigned* snapMask = nullptr); }

// A layout is a generator seed and a difficulty scale (percent). Layouts are validated when they are made: every hop
// on the critical path is searched with the real movement code, and a level that fails is thrown away.
bool PlatLayoutValid(const Game& g, int level) {
    const std::vector<int>& l = g.platLayouts[level];
    return l.size() >= 2 && l.size() <= 4 && l[0] > 0 && l[1] >= 40 && l[1] <= 100; // {seed, scale%[, ghost[, proven snaps]]}
}

void GeneratePlatLayout(Game& g, int level) {
    const double start = GetTime();
    for (int attempt = 0; attempt < 150; attempt++) {
        unsigned seed = (unsigned)GetRandomValue(1, 999999);
        int scale = 100 - (attempt / 20) * 6; // a level that keeps failing is eased off
        if (GetTime() - start > 5.0) scale = 60; // never leave the window frozen for long: ease off hard once the budget is spent
        if (GetTime() - start > 8.0) break;
        GenLevel gl = GenerateLevel(level, seed, scale / 100.0f);
        unsigned snaps = 0;
        if (ValidateGenerated(level, gl, nullptr, nullptr, nullptr, level == PL_PIRATE ? &snaps : nullptr)) {
            g.platLayouts[level] = {(int)seed, scale};
            if (level == PL_PIRATE) g.platLayouts[level].push_back(GetRandomValue(1, 100) <= 12 ? 1 : 0); // rarely, the ship is a ghost ship
            if (level == PL_PIRATE) g.platLayouts[level].push_back((int)snaps);                            // where the Grand Kraken may snap a ship
            return;
        }
    }
    g.platLayouts[level] = {1, 60};
}

std::string PlatLayoutCode(const Game& g, int level) {
    const std::vector<int>& l = g.platLayouts[level];
    if (l.size() < 2) return "(new)";
    return l.size() >= 3 && l[2] ? TextFormat("#%06d GHOST", l[0]) : TextFormat("#%06d", l[0]);
}
void StartPlatform(Game& g, int level, bool freshLayout) {
    if (freshLayout || !PlatLayoutValid(g, level)) GeneratePlatLayout(g, level); // every dive is a new random level (the user); deaths replay it
    g.plat = PlatformState{};
    g.plat.level = level;
    g.plat.layoutCode = PlatLayoutCode(g, level);
    g.plat.layout = g.platLayouts[level];
    g.plat.ghost = level == PL_PIRATE && g.plat.layout.size() >= 3 && g.plat.layout[2] != 0;
    g.plat.hard = g.platHard;
    g.plat.checkpoints = g.platCheckpoints;
    g.plat.bossEnabled = level == PL_HULL ? g.platHullBoss : level == PL_PIRATE ? g.platPirateBoss : true;
    if (Hero* lead = FindHero(g, g.party[0])) { // the rank-1 hero's relics shape the run
        RelicFx fx = RelicBundle(*lead);
        g.plat.pickupPct = fx.pickupPct; g.plat.speedPct = fx.speedPct; g.plat.jumpPct = fx.jumpPct; g.plat.lampPct = fx.lampPct;
    }
    BuildLevel(g.plat);
    g.scene = Scene::Platformer;
}

// depth.exe --verify-critters: a headless smoke test for the Pipes' ambient duct life (PopulateCritters/
// UpdateCritters), in the same spirit as --verify-abyss - no window/GL context needed, since nothing here
// draws. Proves critters spawn, that a close approach makes at least one flee, and that none of them ever
// produce a NaN/runaway position over a few seconds of simulated time.
bool VerifyCritters() {
    PlatformState p;
    p.level = PL_PIPES;
    p.layout = {303, 100};
    BuildLevel(p);
    if (p.critters.empty()) { TraceLog(LOG_WARNING, "verify-critters: FAILED - nothing spawned"); return false; }
    size_t n = p.critters.size();
    p.pos = p.critters[n / 2].home; // stand right on top of one: it must react
    bool anyFled = false;
    for (int f = 0; f < 300; f++) {
        UpdateCritters(p, 1 / 60.0f);
        for (const auto& c : p.critters) {
            if (std::isnan(c.pos.x) || std::isnan(c.pos.y) || fabsf(c.pos.x) > 1e6f || fabsf(c.pos.y) > 1e6f) {
                TraceLog(LOG_WARNING, "verify-critters: FAILED - a critter's position blew up");
                return false;
            }
            if (c.state == CritterState::Fleeing) anyFled = true;
        }
    }
    if (!anyFled) { TraceLog(LOG_WARNING, "verify-critters: FAILED - none fled a diver standing on top of one"); return false; }
    TraceLog(LOG_WARNING, "verify-critters: OK - %zu spawned, at least one fled on approach", n);
    return true;
}

// The darkness of the ducts, drawn in bands (it suits the pixel art) around the diver's helmet lamp.
static void DrawLampDarkness(Vector2 c, float r, float maxA) {
    const int B = 6;
    for (int i = 0; i < B; i++) {
        float r0 = r * (0.4f + 0.6f * i / B), r1 = r * (0.4f + 0.6f * (i + 1) / B);
        DrawRing(c, r0, r1, 0, 360, 48, Fade(BLACK, maxA * (i + 1) / (B + 1)));
    }
    DrawRing(c, r, 1600, 0, 360, 64, Fade(BLACK, maxA));
}

// depth.exe --verify-moves: the diver's extra moves (ParkourReference1.2), driven through the real StepPlayer with
// scripted inputs on small synthetic stages - and a check that none of it fires for the path search.
bool VerifyMoves() {
    bool ok = true;
    auto fail = [&](const char* what) { TraceLog(LOG_WARNING, "verify-moves: FAILED - %s", what); ok = false; };
    auto stage = [](PlatformState& p, int level, int w, int h, int floorY) {
        p = PlatformState{};
        p.level = level; p.w = w; p.h = h;
        p.tiles.assign(h, std::string(w, '.'));
        for (int x = 0; x < w; x++) for (int y = floorY; y < h; y++) p.tiles[y][x] = '#';
        p.pos = {3 * (float)T, (floorY) * (float)T - PH}; p.onGround = true;
    };
    auto run = [](PlatformState& p, int frames, float dir, bool jump, bool down, bool up = false, bool shift = false) {
        for (int k = 0; k < frames; k++) { p.inDown = down; p.upHeld = up; p.shiftHeld = shift; StepPlayer(p, dir, jump); if (p.deathTimer > 0) break; }
    };
    // 1) a running slide: Down at speed ducks the hitbox and carries you, then a slide-jump flies further than a plain jump
    {
        PlatformState p; stage(p, PL_ISLAND, 80, 20, 15);
        run(p, 60, 1, false, false);
        run(p, 3, 1, false, true);
        if (p.pose != 1) fail("Down while running didn't start a slide");
        else if (PlayerBox(p).height > PH - 10) fail("the slide didn't lower the hitbox");
        float slideStart = p.pos.x;
        run(p, 30, 0, false, true);
        if (p.pos.x - slideStart < 40) fail("the slide didn't carry the diver on under its own momentum");
        PlatformState a; stage(a, PL_ISLAND, 80, 20, 15); run(a, 60, 1, false, false); a.jumpBuffer = 0.1f; float ax0 = a.pos.x; run(a, 1, 1, true, false); int fa = 0; while (!a.onGround && fa < 400) { run(a, 1, 1, true, false); fa++; }
        PlatformState b; stage(b, PL_ISLAND, 80, 20, 15); run(b, 60, 1, false, false); run(b, 3, 1, false, true); b.jumpBuffer = 0.1f; float bx0 = b.pos.x; run(b, 1, 1, true, true); int fb = 0; while (!b.onGround && fb < 400) { run(b, 1, 1, true, false); fb++; }
        TraceLog(LOG_WARNING, "verify-moves: plain running jump %.0f px, slide-jump %.0f px", a.pos.x - ax0, b.pos.x - bx0);
        if (b.pos.x - bx0 <= a.pos.x - ax0) fail("a slide-jump didn't carry further than a plain running jump");
    }
    // 2) a hard landing stuns; the same landing with Down and a direction rolls instead, turning the fall into speed
    {
        PlatformState p; stage(p, PL_ISLAND, 60, 40, 35); p.pos.y = 5 * (float)T; p.onGround = false;
        for (int k = 0; k < 900 && !p.onGround; k++) run(p, 1, 0, false, false);
        if (p.pose != 3) fail("a drop taller than any jump didn't stun the diver on landing");
        PlatformState r; stage(r, PL_ISLAND, 60, 40, 35); r.pos.y = 5 * (float)T; r.onGround = false;
        bool rolled = false;
        for (int k = 0; k < 600 && !rolled; k++) { run(r, 1, 1, false, true); rolled = r.pose == 2; }
        if (!rolled) fail("Down and a direction on a hard landing didn't roll");
        else if (fabsf(r.vel.x) < RUN) fail("the roll didn't convert the fall into forward speed");
    }
    // 3) the parachute brake falls far slower than a plain fall
    {
        PlatformState p; stage(p, PL_PIRATE, 40, 60, 55); p.pos.y = 4 * (float)T; p.onGround = false;
        PlatformState q = p;
        run(p, 90, 0, false, false); run(q, 90, 0, false, true);
        TraceLog(LOG_WARNING, "verify-moves: in 1.5 s a plain fall drops %.0f px, the brake %.0f px", p.pos.y - 4 * T, q.pos.y - 4 * T);
        if (q.pos.y - 4 * T > (p.pos.y - 4 * T) * 0.45f) fail("the parachute brake didn't slow the fall enough");
    }
    // 4) dash: on land a flat burst that holds height; underwater, straight up if Up is held; only one per jump
    {
        PlatformState p; stage(p, PL_ISLAND, 80, 30, 25); p.pos = {10 * (float)T, 10 * (float)T}; p.onGround = false; p.vel = {0, 0};
        p.dashReq = 1; float x0 = p.pos.x, y0 = p.pos.y;
        run(p, 34, 0, false, false);
        if (p.pos.x - x0 < 70) fail("a land dash didn't burst forward");
        if (p.pos.y - y0 > 40) fail("a land dash didn't hold the diver's height");
        p.dashReq = 1; run(p, 1, 0, false, false);
        if (p.pose == 5) fail("a second dash worked in the same jump");
        PlatformState w; stage(w, PL_WEEDS, 80, 30, 25); w.pos = {10 * (float)T, 15 * (float)T}; w.onGround = false; w.vel = {0, 0};
        w.dashReq = 1; w.upHeld = true; float wy = w.pos.y;
        w.inDown = false; StepPlayer(w, 0, false); run(w, 42, 0, false, false, true);
        TraceLog(LOG_WARNING, "verify-moves: water dash with Up rose %.0f px (pose %d dir %.2f,%.2f)", wy - w.pos.y, w.pose, w.dashDir.x, w.dashDir.y);
        if (wy - w.pos.y < 40) fail("an underwater dash with Up held didn't rise");
    }
    // 5) hydro-glide: underwater, a second jump press on the way down cuts the fall
    {
        PlatformState p; stage(p, PL_WEEDS, 40, 60, 55); p.pos.y = 4 * (float)T; p.onGround = false; p.vel.y = 100;
        PlatformState q = p;
        q.jumpBuffer = 0.1f; StepPlayer(q, 0, true);
        if (q.pose != 7) fail("pressing jump while falling underwater didn't start a hydro-glide");
        run(p, 90, 0, false, false); run(q, 90, 0, true, false);
        if (q.pos.y - p.pos.y > -60) fail("the glide didn't fall slower than a plain fall");
    }
    // 6) poles: climbing off the top leaves the diver balanced on the tip; a Shift backflip from a pole outjumps a plain jump
    {
        PlatformState p; stage(p, PL_WEEDS, 40, 30, 25);
        for (int y = 18; y < 25; y++) p.tiles[y][10] = 'w';
        p.pos = {10 * (float)T + 6, 23 * (float)T - PH + 10}; p.onGround = false;
        bool tip = false;
        for (int k = 0; k < 400 && !tip; k++) { p.climbDir = -1; run(p, 1, 0, false, false, true); tip = p.pose == 6; }
        if (!tip) fail("climbing off the top of a pole didn't balance the diver on its tip");
        PlatformState a = p, b = p; a.climbDir = b.climbDir = 0; a.pose = b.pose = 6;
        a.jumpBuffer = b.jumpBuffer = 0.1f;
        run(a, 1, 1, true, false); run(b, 1, 1, true, false, false, true);
        float ay = a.pos.y, by = b.pos.y;
        for (int k = 0; k < 40; k++) { run(a, 1, 1, true, false); run(b, 1, 1, true, false); ay = std::min(ay, a.pos.y); by = std::min(by, b.pos.y); }
        if (by >= ay) fail("a pole backflip didn't go higher than a plain jump off the pole");
    }
    // 7) a ledge grab: falling past a lip you're pushing into catches you; Up hauls you over
    {
        PlatformState p; stage(p, PL_ATLANTIS, 40, 30, 25);
        for (int y = 15; y < 25; y++) for (int x = 12; x < 16; x++) p.tiles[y][x] = '#';
        p.pos = {12 * (float)T - PW - 1, 15 * (float)T - PH + 6}; p.onGround = false; p.vel = {0, 0}; // a jump that fell just short: body below the lip
        bool hung = false;
        for (int k = 0; k < 200 && !hung; k++) { run(p, 1, 1, false, false); hung = p.pose == 9; }
        if (!hung) fail("falling past a ledge while pushing into it didn't catch hold");
        else { run(p, 2, 0, false, false, true); if (p.pos.y + PH > 15 * T + 2) fail("Up didn't haul the diver up onto the ledge"); }
    }
    // 8) the path search never presses any of it: with no extra input the movement is exactly the old one
    {
        PlatformState p; stage(p, PL_WEEDS, 80, 40, 35); p.verifying = true; p.pos.y = 5 * (float)T; p.onGround = false;
        run(p, 400, 1, true, false);
        if (p.pose != 0) fail("a pose changed during the path search's movement");
    }
    // 9) a mover (a grazing whale's back): land on it from above and it carries you; barnacle-mite slime makes you faster
    {
        PlatformState p; stage(p, PL_HULL, 80, 40, 35);
        p.movers.push_back({{10 * (float)T, 20 * (float)T, 140, 8}, {40, 0}});
        p.pos = {10 * (float)T + 40, 16 * (float)T}; p.onGround = false; p.vel = {0, 0};
        for (int k = 0; k < 240; k++) { p.movers[0].r.x += 40 * STEP; run(p, 1, 0, false, false); }
        float x0 = p.pos.x;
        for (int k = 0; k < 240; k++) { p.movers[0].r.x += 40 * STEP; run(p, 1, 0, false, false); }
        if (fabsf(p.pos.y + PH - p.movers[0].r.y) > 1.5f || !p.onGround) fail("the diver didn't land on the mover's top");
        else if (fabsf(p.pos.x - x0 - 40) > 6) fail("standing on a mover didn't carry the diver along with it");
        PlatformState q; stage(q, PL_HULL, 200, 40, 35);
        PlatformState r = q; r.slickT = 5;
        run(q, 120, 1, false, false); run(r, 120, 1, false, false);
        if (r.pos.x - q.pos.x < 30) fail("slime (slickT) didn't raise the diver's top speed");
    }
    // 10) a slimy wall (1.2): pressing into it gives no grip - you slide straight down and can't wall-jump off it
    {
        PlatformState p; stage(p, PL_CAVE, 40, 40, 35);
        for (int y = 5; y < 35; y++) p.tiles[y][12] = 's';
        p.pos = {12 * (float)T - PW - 0.5f, 10 * (float)T}; p.onGround = false; p.vel = {0, 0};
        run(p, 60, 1, false, false);
        float vy = p.vel.y, x0 = p.pos.x;
        run(p, 2, 1, true, false); run(p, 20, 1, false, false);
        if (vy < 300) fail("a slimy wall still gave grip");
        if (p.pos.x < x0 - 30) fail("a slimy wall still allowed a wall jump");
    }
    if (ok) TraceLog(LOG_WARNING, "verify-moves: OK - slide and slide-jump, stun and impact roll, parachute brake, dash (land and water), hydro-glide, pole tip and backflip, ledge grab, movers, slime, slimy walls");
    return ok;
}
void ScenePlatformer(Game& g) {
    auto& p = g.plat;
    gGhost = p.ghost;
    float dt = std::min(GetFrameTime(), 0.05f);

    // ---------------- update
    if (!p.finished) {
        if (IsKeyPressed(KEY_ESCAPE)) {
            g.scene = Scene::Periscope;
            Toast(g, "Run abandoned.");
            return;
        }
        float dir = 0;
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) dir += 1;
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) dir -= 1;
        bool weed = p.onWeed; // in seaweed, up and down climb instead of jumping (Space still jumps off it)
        bool jumpPressed = IsKeyPressed(KEY_SPACE) || (!weed && (IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP)));
        p.climbDir = weed ? ((IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) ? -1 : (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) ? 1 : 0) : 0;
        bool jumpHeld = IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_W) || IsKeyDown(KEY_UP);
        if (IsGamepadAvailable(0)) {
            float ax = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
            if (ax > 0.4f || IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) dir = 1;
            if (ax < -0.4f || IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) dir = -1;
            jumpPressed |= IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
            jumpHeld |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
        }
        if (jumpPressed) p.jumpBuffer = JUMP_BUFFER;
        // the extra moves: Down (slide, roll, brake, pole slide), a double-tap of a direction (dash), Shift (pole backflip)
        {
            static float lastTap[2] = {-9, -9};
            p.inDown = IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN);
            p.upHeld = IsKeyDown(KEY_W) || IsKeyDown(KEY_UP);
            p.shiftHeld = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            if (IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT)) { if (p.time - lastTap[1] < 0.25f) p.dashReq = 1; lastTap[1] = p.time; }
            if (IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT)) { if (p.time - lastTap[0] < 0.25f) p.dashReq = -1; lastTap[0] = p.time; }
            if (IsGamepadAvailable(0)) {
                float ay = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
                p.inDown |= ay > 0.5f || IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN);
                p.upHeld |= ay < -0.5f || IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP);
                p.shiftHeld |= IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1);
                if (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT)) p.dashReq = dir != 0 ? (int)dir : (p.facingRight ? 1 : -1);
            }
        }
        p.time += dt;

        if (p.deathTimer > 0) {
            p.deathTimer -= dt;
            if (p.deathTimer <= 0) Respawn(p);
        } else {
            p.accumulator += dt;
            while (p.accumulator >= STEP && p.deathTimer <= 0 && !p.finished) {
                p.accumulator -= STEP;
                StepPlayer(p, dir, jumpHeld);
            }
            int part = PartAt(p, p.pos.x + PW / 2);
            if (p.onGround && part > p.checkpointChunk) {
                p.checkpointChunk = part; // with checkpoints off this just counts progress
                if (p.checkpoints) Burst(p, {p.pos.x + PW / 2, p.pos.y}, 12, Pal::Good, 140, 0.5f, 2);
            }
        }
        float ed = p.ghost ? dt * GHOST_SPEED : dt; // ghosts move, aim, fire and charge 1.6x faster
        UpdateEnemies(p, ed);
        UpdateCritters(p, dt); // ambient duct life - never touched by ghost speed, it isn't part of the challenge
        BeastsUpdate(p, dt);   // the living-AI creatures - real time, never ghost-sped
        UpdateBoss(p, ed);
        UpdateLaunchers(p, ed);
        UpdateShots(p, ed);

        // touching an enemy is deadly; only a boss can be stomped
        if (p.deathTimer <= 0 && !p.finished) {
            Rectangle pr = PlayerBox(p);
            for (auto& e : p.enemies) if (EnemyHits(e, pr)) Die(p);
            for (auto& s : p.shots) if (ShotHits(s, pr)) Die(p);
            if (BeastsTouchDiver(p, pr)) { // a crab, an eel, a puffed pufferfish - see BeastLethalNow
                const char* tip = nullptr; const char* who = BeastsKiller(p, pr, &tip);
                if (who && p.deathTimer <= 0) { p.deathCause = TextFormat("Taken by the %s", who); p.deathTip = tip ? tip : ""; p.causeT = 5.0f; }
                Die(p);
            }
            PlatBoss& b = p.boss;
            bool falling = p.vel.y > 0;
            if (b.type == 'K' && !b.defeated) {
                for (int i = 0; i < 2; i++) if (b.tentT[i] >= 0.75f && CheckCollisionRecs(pr, TentacleBox(p, i))) Die(p);
                if (ReachDeadly(b.reach)) // the tracking tentacles: the whole length is deadly
                    for (int i = 0; i < 2; i++) if (SegmentTouches(b.reach.base[i], b.reach.tip[i], 14, pr)) Die(p);
                if (BeakDeadly(b.beak) && (CheckCollisionRecs(pr, BeakBody(b.beak)) || CheckCollisionRecs(pr, BeakTip(b.beak)))) Die(p);
                if (b.inkT >= 0.5f && b.inkT < 1.7f) { // the cloud floods everything but one half of the arena
                    float arenaX = (p.w - CH_W) * (float)T, arenaMid = arenaX + 11.5f * T;
                    bool inCloud = b.inkSafeRight ? p.pos.x + PW < arenaMid : p.pos.x > arenaMid;
                    if (inCloud) Die(p);
                }
                if (b.lungeT >= 0.15f && b.lungeT < 1.15f) { // the head skims the surface, sweeping across
                    float u = (b.lungeT - 0.15f) / 1.0f, lx = b.lungeFromX + (b.lungeToX - b.lungeFromX) * u;
                    if (CheckCollisionRecs(pr, {lx - 40, KrakenOrigin(p) + 9.0f * T, 80, 3.0f * T})) Die(p);
                }
                if ((b.state == 1 || b.state == 2) && CheckCollisionRecs(pr, KrakenHead(p))) {
                    Rectangle h = KrakenHead(p);
                    if (falling && p.pos.y + PH - p.vel.y * dt <= h.y + 14 * KRAKEN_SCALE && b.invuln <= 0) {
                        b.hp--;
                        b.invuln = 0.6f;
                        p.vel.y = IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_W) || IsKeyDown(KEY_UP) ? -820.0f : -600.0f;
                        p.scale = {0.75f, 1.3f};
                        Burst(p, {h.x + h.width / 2, h.y}, 16, Color{230, 170, 220, 255}, 200, 0.5f, 3);
                        b.state = b.hp <= 0 ? 4 : 3;
                        b.timer = 0;
                        if (b.hp <= 0) {
                            b.defeated = true;
                            b.tentT[0] = b.tentT[1] = TENT_IDLE;
                            if (!p.checkpoints) p.relic = GetRandomValue(0, (int)Relics().size() - 1);
                            Toast(g, p.checkpoints ? "The Kraken sinks into the abyss!" : "The Kraken sinks into the abyss! It left something behind...");
                        }
                    } else if (b.invuln <= 0) {
                        Die(p);
                    }
                }
            } else if (b.type == 'B' && !b.defeated && CheckCollisionRecs(pr, b.state == 5 ? Rectangle{b.pos.x - 10, b.pos.y - 8, BB_W + 20, BB_H + 8} : Rectangle{b.pos.x + 5, b.pos.y, BB_W - 10, BB_H})) {
                if (b.state == 5) { // dazed: stomp him (brushing against him now is safe). A wider, more forgiving box than his
                                     // usual deadly one: he's tall and narrow, and pixel-perfect landings weren't fun to chase
                    if (falling && p.pos.y + PH - p.vel.y * dt <= b.pos.y + 28 && b.invuln <= 0) {
                        b.hp--;
                        b.invuln = 1.0f;
                        b.state = 3;
                        b.timer = 0;
                        Burst(p, {b.pos.x + BB_W / 2, b.pos.y}, 14, Color{240, 240, 230, 255}, 200, 0.5f, 3);
                        p.vel.y = IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_W) || IsKeyDown(KEY_UP) ? -820.0f : -600.0f;
                        p.scale = {0.75f, 1.3f};
                        if (b.hp <= 0) {
                            b.defeated = true;
                            p.exitOpen = true;
                            p.shots.clear();
                            Burst(p, {b.pos.x + BB_W / 2, b.pos.y + 30}, 30, Pal::Brass, 260, 0.8f, 3);
                            Toast(g, "Blackbeard is beaten! The treasure is yours.");
                        } else if (p.hard || b.hp == 1) {
                            ThrowBomb(p);
                        }
                    }
                } else if (b.invuln <= 0) {
                    Die(p); // on his guard, he's deadly from every side, even from above
                }
            }
        }

        if (p.finished) {
            const LevelDef& L = Lv(p.level);
            // one payout at the end: a flat sum per level (the Pipes add a speed bonus; the Ghost Ship pays double)
            int base = L.bonus + (p.level == PL_PIPES ? std::clamp((int)(120 - p.time), 0, 60) : 0);
            if (p.level == PL_PIRATE && p.ghost) base *= 2;
            p.reward = p.hard ? base * 3 / 2 : base;
            // the Kraken rolls a relic (above); Blackbeard always leaves one, and the Ghost Ship's captain two
            if (p.level == PL_PIRATE && p.boss.type == 'B' && p.boss.defeated && !p.checkpoints) {
                p.relic = GetRandomValue(0, (int)Relics().size() - 1);
                if (p.ghost) p.relic2 = GetRandomValue(0, (int)Relics().size() - 1);
            }
            g.gold += p.reward;
            if (p.relic >= 0) g.relicStorage.push_back(p.relic);
            if (p.relic2 >= 0) g.relicStorage.push_back(p.relic2);
            g.platCleared[p.level] = true;
            if (g.platBest[p.level] <= 0 || p.time < g.platBest[p.level]) g.platBest[p.level] = p.time;
            GeneratePlatLayout(g, p.level); // beaten: a fresh layout next time
        }
    }
    for (auto& pt : p.particles) {
        pt.p.x += pt.v.x * dt;
        pt.p.y += pt.v.y * dt;
        pt.v.y += (pt.size < 0 ? -30 : 400) * dt;   // a negative size marks a bubble: it rises
        pt.life -= dt;
    }
    p.particles.erase(std::remove_if(p.particles.begin(), p.particles.end(), [](const PlatParticle& q) { return q.life <= 0; }), p.particles.end());

    // camera: locked to the diver on both axes (no easing), stopping only at the level's edges
    const float viewW = PIXEL_W / ZOOM, viewH = (PIXEL_H - HUD_PX) / ZOOM;
    float levelW = (float)p.w * T, levelH = (float)p.h * T;
    p.camX = std::clamp(p.pos.x + PW / 2, viewW / 2, std::max(viewW / 2, levelW - viewW / 2));
    p.camY = levelH <= viewH ? levelH / 2 : std::clamp(p.pos.y + PH / 2, viewH / 2, levelH - viewH / 2);

    // ---------------- draw
    // The platform levels are deliberately retro: the world is drawn at half resolution onto a small
    // canvas, then scaled up without smoothing. The camera is snapped to whole canvas pixels (as the
    // diver is), and the leftover fraction is applied when scaling up, so scrolling stays smooth.
    float t = g.time;
    SetPost(0.2f, 0.0f, 0.15f);
    const float PX = (float)SCREEN_W / PIXEL_W;
    float cx = p.camX * ZOOM, cy = p.camY * ZOOM, sx = floorf(cx), sy = floorf(cy);
    BeginLayer(PixelRT());
    DrawBackground(p, t);
    Camera2D cam{};
    cam.zoom = ZOOM;
    cam.offset = {(PIXEL_W + 2) / 2.0f, 1 + HUD_PX + (PIXEL_H - HUD_PX) / 2.0f};
    cam.target = {sx / ZOOM, sy / ZOOM};
    BeginMode2D(cam);
    DrawBossBack(p, t);
    int c0 = std::max(0, (int)((p.camX - viewW / 2) / T) - 2), c1 = std::min(p.w - 1, (int)((p.camX + viewW / 2) / T) + 2);
    if (p.level == PL_PIRATE) DrawShipScenery(p, c0, c1, t);
    DrawAmbientLife(p, t, viewW, viewH);
    int r0 = std::max(0, (int)((p.camY - viewH / 2) / T) - 2), r1 = std::min(p.h - 1, (int)((p.camY + viewH / 2) / T) + 2);
    for (int y = r0; y <= r1; y++) // first the sides and tops of blocks, which recede into the scene...
        for (int x = c0; x <= c1; x++) DrawDepth(p, x, y);
    for (int y = r0; y <= r1; y++) // ...then their faces and everything else
        for (int x = c0; x <= c1; x++) DrawTile(p, p.tiles[y][x], x, y, t);
    DrawHazardOverlay(p, c0, c1, r0, r1, t);
    DrawBoss(p, t);
    for (auto& e : p.enemies) DrawEnemy(e, t, p.level);
    for (auto& c : p.critters) DrawCritter(c, t);
    DrawFauna(p, t, c0, c1);
    DrawShots(p, t);
    DrawSea(p, t, viewW, viewH);
    for (auto& pt : p.particles) {
        if (pt.size < 0) DrawRing({pt.p.x, pt.p.y}, -pt.size * 0.6f, -pt.size, 0, 360, 10, Fade(pt.c, std::min(1.0f, pt.life / pt.max * 1.5f) * 0.8f));
        else DrawRectangle((int)pt.p.x, (int)pt.p.y, (int)pt.size, (int)pt.size, Fade(pt.c, std::min(1.0f, pt.life / pt.max * 1.5f)));
    }
    if (p.deathTimer <= 0) {
        DrawDiver(p);
    }
    if (!Lv(p.level).dark) DrawGlowingBits(p, c0, c1, r0, r1, t);
    EndMode2D();
    DrawForeground(p, t); // the near silhouettes, in front of the diver
    if (p.level == PL_PIRATE) { // a storm over the fleet: driving rain, low fog, and lightning every so often
        for (int k = 0; k < 110; k++) {
            float rx = fmodf(k * 97.3f + t * 60, PIXEL_W + 60.0f) - 30, ry = fmodf(k * 53.7f + t * 330 + Hs(k * 1.3f) * 400, PIXEL_H + 40.0f) - 20;
            DrawLine((int)rx, (int)ry, (int)rx - 3, (int)ry + 8, Fade(Color{170, 190, 230, 255}, 0.26f));
        }
        for (int k = 0; k < 4; k++) DrawEllipse((int)(fmodf(k * 211.0f + t * 7 * (1 + k % 2), PIXEL_W + 300.0f) - 150), PIXEL_H - 30 - k * 12, 150, 12, Color{140, 150, 190, 22});
        float ph = fmodf(t + 3.0f, 11.0f);
        float flash = ph < 0.08f ? 1.0f : (ph > 0.2f && ph < 0.3f) ? 0.6f : 0.0f;
        if (flash > 0) {
            DrawRectangle(0, 0, PIXEL_W + 2, PIXEL_H + 2, Fade(Color{200, 210, 255, 255}, 0.22f * flash));
            float bx = 90 + Hs(floorf((t + 3.0f) / 11.0f)) * (PIXEL_W - 180);
            for (int s = 0; s < 9; s++) DrawLineEx({bx + sinf(s * 2.1f) * 14, HUD_PX + s * 30.0f}, {bx + sinf((s + 1) * 2.1f) * 14, HUD_PX + (s + 1) * 30.0f}, 2, Fade(WHITE, flash));
        }
    }
    if (p.ghost) { // a cold teal grade and drifting fog
        DrawRectangle(0, 0, PIXEL_W + 2, PIXEL_H + 2, Color{26, 78, 96, 72});
        for (int k = 0; k < 6; k++) DrawEllipse((int)(fmodf(k * 137.0f + t * 9 * (1 + k % 3), PIXEL_W + 240.0f) - 120), (int)(HUD_PX + 50 + k * 46), 130, 14, Color{170, 235, 230, 26});
    }
    if (Lv(p.level).dark) {
        Vector2 lamp = GetWorldToScreen2D({p.pos.x + PW / 2 + (p.facingRight ? 14.0f : -14.0f), p.pos.y + 6}, cam);
        DrawLampDarkness(lamp, (p.hard ? 132.0f : 160.0f) * (1 + p.lampPct / 100.0f), p.hard ? 0.72f : 0.62f); // Normal lights more of the duct
        BeginMode2D(cam); // things that glow in the dark: steam jets, gears' rims, the valve, warning lamps
        DrawGlowingBits(p, c0, c1, r0, r1, t);
        EndMode2D();
    }
    EndLayer();
    DrawTexturePro(PixelRT().texture, {0, 0, PIXEL_W + 2.0f, -(PIXEL_H + 2.0f)},
                   {-PX, -PX, (PIXEL_W + 2) * PX, (PIXEL_H + 2) * PX}, {0, 0}, 0, WHITE); // whole-pixel placement only
    if (p.deathTimer > 0) DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(Pal::Bad, p.deathTimer * 0.5f));

    // ---------------- HUD
    DrawRectangle(0, 0, SCREEN_W, 56, Color{16, 30, 40, 235});
    DrawRectangle(0, 56, SCREEN_W, 3, Pal::BrassDk);
    const char* title = TextFormat("%s   %s", Lv(p.level).name, p.layoutCode.c_str());
    TxtShadow(title, 20, 15, 22, Pal::Brass, true);
    TxtBold(p.hard ? "HARD" : "NORMAL", 20 + MeasureTxt(title, 22, true) + 14, 20, 14, p.hard ? Pal::Bad : Color{160, 200, 190, 255});
    int sections = (int)p.partX.size();
    Txt(TextFormat(p.checkpoints ? "Checkpoint %d/%d" : "Section %d/%d", std::min(p.checkpointChunk + 1, sections), sections), 530, 18, 19, Pal::Paper);

    Txt(TextFormat("Deaths %d", p.deaths), 720, 18, 19, Pal::Paper);
    if (p.causeT > 0 && !p.deathCause.empty()) { // what killed you, and one way to beat it
        float a = std::min(1.0f, p.causeT);
        DrawTextCenteredBold(p.deathCause.c_str(), SCREEN_W / 2.0f, 86, 22, Fade(Color{255, 190, 150, 255}, a));
        DrawTextCenteredBold(p.deathTip.c_str(), SCREEN_W / 2.0f, 112, 15, Fade(Pal::Paper, a));
        p.causeT -= GetFrameTime();
    }
    Txt(TextFormat("%.1fs", p.time), 840, 18, 19, Pal::Paper);
    if (p.boss.type && !p.boss.defeated && p.pos.x > (p.w - CH_W - 2) * (float)T) {
        const char* name = p.boss.type == 'K' ? "KRAKEN" : p.ghost ? "GHOST BLACKBEARD" : "BLACKBEARD";
        TxtBold(name, 930, 18, 19, Pal::Bad);
        for (int k = 0; k < 3; k++) DrawCircle(1060 + MeasureTxt(name, 19, true) - 110 + k * 18, 28, 6, k < p.boss.hp ? Pal::Bad : Color{60, 50, 50, 255});
    }
    Txt("Esc: give up", 1150, 20, 16, Color{190, 200, 200, 255});

    if (p.finished) {
        Rectangle panel{380, 190, 520, 300};
        Panel(panel);
        const LevelDef& L = Lv(p.level);
        DrawTextCenteredBold(p.level == PL_PIPES ? "Valve reached!" : p.level == PL_HULL ? "Back inside!" : p.level == PL_ISLAND ? "Idol reached!" : p.level == PL_CAVE ? "Clear water ahead!" : p.level == PL_WEEDS ? "Through the forest!" : p.level == PL_ATLANTIS ? "The gate opens!" : "Treasure claimed!", panel.x + panel.width / 2, panel.y + 24, 34, Pal::Good);
        DrawTextCentered(TextFormat(p.level == PL_PIPES ? "Payout (with speed bonus): %d gold" : p.ghost && p.level == PL_PIRATE ? "Ghost Ship payout, doubled: %d gold" : "Payout: %d gold", p.reward), panel.x + panel.width / 2, panel.y + 88, 21, Pal::Ink);
        DrawTextCentered(TextFormat("Time %.1fs  (best %.1fs)    Deaths %d", p.time, g.platBest[p.level], p.deaths), panel.x + panel.width / 2, panel.y + 124, 19, Pal::BrassDk);
        if (p.relic >= 0 && p.relic2 >= 0)
            DrawTextCenteredBold(TextFormat("Relics found: %s, %s", Relics()[p.relic].name.c_str(), Relics()[p.relic2].name.c_str()),
                                 panel.x + panel.width / 2, panel.y + 160, 20, Pal::Copper);
        else if (p.relic >= 0)
            DrawTextCenteredBold(TextFormat("Relic found: %s", Relics()[p.relic].name.c_str()), panel.x + panel.width / 2, panel.y + 160, 20, Pal::Copper);
        DrawTextCentered("A new layout will be waiting next time.", panel.x + panel.width / 2, panel.y + 194, 17, Pal::BrassDk);
        if (Button({panel.x + 110, panel.y + 228, 300, 48}, "Back to the periscope")) g.scene = Scene::Periscope;
    }
}

// ============================================================ sprite sheet pages (developer tool)
// Draws the platform levels' sprites on the pixel canvas, at twice the size they appear in the game.
// page 0: the diver and the enemies; 1: the bosses; 2: each level's tiles.
void DrawPlatformSpritePage(int page, float t) {
    const float PX = (float)SCREEN_W / PIXEL_W; // screen pixels per canvas pixel
    struct Label { const char* text; Vector2 at; };
    std::vector<Label> labels;
    BeginLayer(PixelRT());
    ClearBackground(Color{30, 34, 44, 255});
    for (int x = 0; x < PIXEL_W + 2; x += 16) DrawRectangle(x, 0, 1, PIXEL_H + 2, Color{36, 40, 52, 255});
    PlatformState p;
    p.time = t;
    p.tiles.assign(1, std::string(40, '.'));
    p.w = 40;
    p.h = 1;
    if (page == 0) {
        struct DiverPose { const char* name; bool ground; Vector2 vel; float anim; int wall; };
        const DiverPose dp[6] = {{"Idle", true, {0, 0}, 0, 0}, {"Run", true, {RUN, 0}, 1.2f, 0}, {"Run", true, {RUN, 0}, 2.9f, 0},
                                 {"Jump", false, {200, -500}, 0, 0}, {"Fall", false, {100, 500}, 0, 0}, {"Wall slide", false, {0, 150}, 0, 1}};
        for (int k = 0; k < 6; k++) {
            p.onGround = dp[k].ground; p.vel = dp[k].vel; p.runAnim = dp[k].anim; p.wallSide = dp[k].wall; p.facingRight = true;
            p.pos = {40.0f + k * 56, 80 - PH};
            DrawDiver(p);
            labels.push_back({dp[k].name, {p.pos.x + PW / 2, 90}});
        }
        PlatEnemy crab{'c', {380, 64}, {380, 64}, 1, 0}, eel{'e', {450, 64}, {450, 64}, 1, 0}, bird{'p', {520, 60}, {520, 60}, 1, 0};
        DrawEnemy(crab, t); DrawEnemy(eel, t); DrawEnemy(bird, t);
        labels.push_back({"Crab", {393, 90}}); labels.push_back({"Eel", {450, 90}}); labels.push_back({"Parakeet", {520, 90}});
        const char* ambName[4] = {"Behind his door", "Bursting out", "Stab!", "Ducking back"};
        for (int k = 0; k < 4; k++) {
            PlatEnemy a{'P', {0, 0}, {40.0f + k * 80, 150}, 1, 0, k, k == 1 ? AMB_OUT * 0.6f : k == 2 ? AMB_STAB * 0.5f : k == 3 ? AMB_BACK * 0.3f : 0};
            a.pos = {a.home.x + 5 + Lunge(a), a.home.y + T - 30};
            DrawEnemy(a, t);
            labels.push_back({ambName[k], {a.home.x + 20, 192}});
        }
        for (int k = 0; k < 2; k++) {
            PlatEnemy gn{'G', {380.0f + k * 90, 162}, {380.0f + k * 90, 160}, 1, 0, k, k ? GUN_AIM * 0.8f : 0};
            gn.aim = {gn.pos.x + 120, gn.pos.y - 10};
            DrawTile(p, 'k', (int)((gn.pos.x - 34) / T), 5, t); // his barrel
            DrawEnemy(gn, t);
            labels.push_back({k ? "Gunner, aiming" : "Gunner", {gn.pos.x + 11, 200}});
        }
        p.shots = {{{80, 260}, {-330, 0}, 1, 0}, {{160, 256}, {0, 0}, 0.4f, 1}, {{250, 256}, {0, 0}, 0.15f, 2}};
        DrawShots(p, t);
        labels.push_back({"Musket ball", {80, 300}}); labels.push_back({"Lit bomb", {160, 300}}); labels.push_back({"Blast", {250, 300}});
    } else if (page == 1) {
        const char* bbName[5] = {"Blackbeard stalks", "winds up", "charges", "aims a pistol", "dazed: stomp now!"};
        const int bbState[5] = {0, 1, 2, 4, 5};
        p.pos = {600, 200};
        for (int k = 0; k < 5; k++) {
            p.boss = PlatBoss{};
            p.boss.type = 'B'; p.boss.state = bbState[k]; p.boss.dir = 1; p.boss.timer = 0.3f;
            p.boss.pos = {30.0f + k * 78, 110 - BB_H};
            if (k == 2) p.boss.vel.x = 300;
            DrawBoss(p, t);
            labels.push_back({bbName[k], {p.boss.pos.x + BB_W / 2, 124}});
        }
        p.boss = PlatBoss{};
        p.boss.type = 'K'; p.boss.state = 2; p.boss.home = {500, 20 + 3.0f * T + 10};
        p.boss.tentT[0] = p.boss.tentT[1] = TENT_IDLE;
        DrawBoss(p, t);
        labels.push_back({"The Kraken's head (stomp it)", {500, 150}});
        DrawTentacle({120, 340}, {130, 200}, 16, t * 2, 1, Color{86, 58, 80, 255});
        DrawTentacle({220, 190}, {230, 330}, 16, t * 2, 2, Color{86, 58, 80, 255});
        labels.push_back({"Tentacles rise and slam", {175, 350}});
    } else {
        struct Strip { int level; const char* name; const char* rows[3]; };
        const Strip strips[5] = {{PL_PIPES, "The Pipes", {".o..g..E", ".==.|...", "##tx#x##"}},
                                 {PL_HULL, "The Hull", {".o..g..E", "........", "##x#####"}},
                                 {PL_PIRATE, "The Pirate Ship", {".o..g..E", ".==.k...", "##tx#k##"}},
                                 {PL_ISLAND, "The Island", {".o..g..E", "........", "##x#####"}},
                                 {PL_CAVE, "The Cave", {".o..g..E", "........", "##x#####"}}};
        for (int i = 0; i < 5; i++) {
            PlatformState q;
            q.level = strips[i].level;
            q.time = t;
            q.w = 8; q.h = 3;
            for (auto r : strips[i].rows) q.tiles.push_back(r);
            float ox = 16 + (i % 2) * 320.0f, oy = 30 + (i / 2) * 170.0f;
            rlPushMatrix();
            rlTranslatef(ox, oy, 0);
            for (int y = 0; y < 3; y++) for (int x = 0; x < 8; x++) DrawDepth(q, x, y);
            for (int y = 0; y < 3; y++) for (int x = 0; x < 8; x++) DrawTile(q, q.tiles[y][x], x, y, t);
            rlPopMatrix();
            labels.push_back({strips[i].name, {ox + 128, oy + 104}});
        }
        labels.push_back({"coin, hazard (gear / mine / spiked ball), exit; pipes, jets, crates", {480, 330}});
    }
    EndLayer();
    DrawTexturePro(PixelRT().texture, {0, 0, PIXEL_W + 2.0f, -(PIXEL_H + 2.0f)}, {-PX, -PX, (PIXEL_W + 2) * PX, (PIXEL_H + 2) * PX}, {0, 0}, 0, WHITE);
    const char* titles[3] = {"Platform levels: the diver and the enemies (shown at 1.6x their in-game size)", "Platform levels: the bosses",
                             "Platform levels: tiles for each level"};
    TxtBold(titles[page], 30, 12, 22, Pal::Brass);
    for (auto& l : labels) {
        int w = MeasureTxt(l.text, 14);
        Txt(l.text, l.at.x * PX - w / 2.0f, l.at.y * PX, 14, Pal::Paper);
    }
}
// ============================================================ verification (developer tool)
// Proves every section can be crossed: an A* search over button inputs (left / right / neither, jump
// held or not, re-decided every 1/40 s) driving the real movement code, from the section's left edge
// until the player stands in the section after it. Hazards and timed jets count; enemies and bosses
// are left out, since they're timing obstacles rather than walls. Run with:  depth.exe --verify
namespace {
struct SimNode {
    Vector2 pos, vel;
    float coyote, wallCoyote, jumpBuffer, wallLock, time;
    int wallSide, lockSide;
    bool onGround, held, facing;
    // movement pass 2: the search can dash and grab ledges too, so it carries that state
    int pose = 0, ledgeTx = 0, ledgeTy = 0;
    float moveT = 0, boostT = 0;
    bool dashReady = true;
    Vector2 dashDir{0, 0};
};

SimNode Snap(const PlatformState& p, bool held) {
    return {p.pos, p.vel, p.coyote, p.wallCoyote, p.jumpBuffer, p.wallLock, p.time, p.wallSide, p.lockSide, p.onGround, held, p.facingRight,
            p.pose, p.ledgeTx, p.ledgeTy, p.moveT, p.boostT, p.dashReady, p.dashDir};
}

void Restore(PlatformState& p, const SimNode& n) {
    p.pos = n.pos; p.vel = n.vel; p.coyote = n.coyote; p.wallCoyote = n.wallCoyote; p.jumpBuffer = n.jumpBuffer;
    p.wallLock = n.wallLock; p.time = n.time; p.wallSide = n.wallSide; p.lockSide = n.lockSide;
    p.onGround = n.onGround; p.facingRight = n.facing; p.deathTimer = 0; p.finished = false;
    p.pose = n.pose; p.ledgeTx = n.ledgeTx; p.ledgeTy = n.ledgeTy; p.moveT = n.moveT; p.boostT = n.boostT; p.dashReady = n.dashReady; p.dashDir = n.dashDir;
    p.inDown = false; p.upHeld = false; p.shiftHeld = false; p.dashReq = 0;
}

// States that land in the same bucket count as already visited. Sections with timed jets also
// track where in the jet cycle we are, so they use coarser buckets to keep the search small.
uint64_t Key(const SimNode& n, bool jets) {
    float pq = jets ? 6.0f : 4.0f, vxq = jets ? 85.0f : 60.0f, vyq = jets ? 120.0f : 80.0f;
    uint64_t k = (uint64_t)std::clamp((int)(n.pos.x / pq), 0, 65535);
    k = k * 1024 + (uint64_t)std::clamp((int)(n.pos.y / pq) + 100, 0, 1023);
    k = k * 32 + (uint64_t)std::clamp((int)lroundf(n.vel.x / vxq) + 16, 0, 31);
    k = k * 64 + (uint64_t)std::clamp((int)lroundf(n.vel.y / vyq) + 32, 0, 63);
    k = k * 2 + n.onGround;
    k = k * 2 + n.held;
    k = k * 3 + (uint64_t)(n.wallSide + 1);
    k = k * 2 + (n.wallLock > 0);
    k = k * 2 + (n.coyote > 0);
    k = k * 2 + (n.wallCoyote > 0);
    k = k * 16 + (uint64_t)(n.pose & 15);
    k = k * 2 + n.dashReady;
    if (jets) k = k * 16 + (uint64_t)((int)(fmodf(n.time, 2.4f) / 0.15f) % 16);
    return k;
}

bool Crossable(PlatformState& p, float goalX, bool jets, long& expanded, float goalY = 0, float tol = 0, long cap = 3000000, float* outTime = nullptr) {
    const float startTime = p.time;
    struct Item { float f; int idx; bool operator<(const Item& o) const { return f > o.f; } };
    std::vector<SimNode> nodes;
    std::priority_queue<Item> open;
    std::unordered_set<uint64_t> seen;
    p.vel = {0, 0};
    nodes.push_back(Snap(p, false));
    open.push({0, 0});
    seen.insert(Key(nodes[0], jets));
    expanded = 0;
    const int SUB = 6;
    while (!open.empty() && expanded < cap) {
        SimNode cur = nodes[open.top().idx];
        open.pop();
        expanded++;
        for (int act = 0; act < 10; act++) { // run left/none/right, jump held or not - and (movement pass 2) a dash or a slide either way
                int dir = act < 6 ? act / 2 - 1 : ((act - 6) % 2 == 0 ? -1 : 1), held = act < 6 ? act % 2 : (cur.held ? 1 : 0);
                bool dashAct = act == 6 || act == 7, slideAct = act >= 8;
                if (dashAct && (!cur.dashReady || cur.pose == 5)) continue;
                if (slideAct && !cur.onGround && cur.pose != 1) continue; // a slide starts on the ground (or carries on)
                Restore(p, cur);
                if (dashAct) p.dashReq = dir;
                if (slideAct) p.inDown = true;
                if (held && !cur.held) p.jumpBuffer = JUMP_BUFFER;
                bool dead = false;
                for (int k = 0; k < SUB && !dead; k++) {
                    StepPlayer(p, (float)dir, held != 0);
                    p.time += STEP;
                    dead = p.deathTimer > 0;
                }
                p.particles.clear();
                if (dead) continue;
                if (tol > 0 ? (p.onGround && fabsf(p.pos.x + PW / 2 - goalX) < tol && fabsf(p.pos.y - goalY) < 6) : (p.pos.x >= goalX && p.onGround)) { if (outTime) *outTime = p.time - startTime; return true; }
                SimNode n = Snap(p, held != 0);
                if (!seen.insert(Key(n, jets)).second) continue;
                nodes.push_back(n);
                // hop searches (tol > 0) only need *a* path, so they use a weighted heuristic: horizontal and vertical distance to go
                float toGo = fabsf(goalX - n.pos.x) / RUN + (tol > 0 ? fabsf(goalY - n.pos.y) / 900 : 0);
                open.push({n.time + (tol > 0 ? 3.0f : 1.0f) * toGo, (int)nodes.size() - 1});
            }
    }
    return false;
}
}  // namespace

namespace {
// Searches every hop of the critical path, from one platform's standing tile to the next's, with the real movement.
double gMs[PL_COUNT] = {}; int gDraws[PL_COUNT] = {};
bool gValidateShafts = false; // DEPTH_FULL=1: also search the shaft climbs of every generated level, not just the proven templates
long gHopExpanded[PL_COUNT][3] = {}; // per level: total expansions, hops searched, largest hop
bool ValidateGenerated(int level, const GenLevel& gl, int* failedHop, float* solveTime, int* hopsOut, unsigned* snapMask) {
    const LevelDef& L = Lv(level);
    PlatformState p;
    p.level = level;
    p.hard = true;
    p.verifying = true;
    BuildFromGrid(p, gl, !PlatHasArena(level) ? nullptr : &L.last, L.fill, L.fillAbove);
    bool jets = false;
    for (auto& row : p.tiles) jets |= row.find_first_of("tv") != std::string::npos;
    p.exitOpen = false;
    for (size_t i = 0; i + 1 < gl.path.size(); i++) {
        const GenWaypoint &a = gl.path[i], &b = gl.path[i + 1];
        if (!gValidateShafts && (b.tag == SetPiece::ShaftUp || b.tag == SetPiece::ShaftDown || b.tag == SetPiece::BarnacleShaft)) continue; // shafts are proven as templates (see VerifyPlatformLevels)
        p.pos = {a.tx * (float)T + 6, (a.ty - p.genTop + 1) * (float)T - PH};
        p.vel = {0, 0};
        p.coyote = p.wallCoyote = p.jumpBuffer = p.wallLock = p.time = 0;
        p.onGround = false;
        long n = 0;
        float hopTime = 0;
        bool hopOk = Crossable(p, b.tx * (float)T + 16, jets, n, (b.ty - p.genTop + 1) * (float)T - PH, 22, gValidateShafts ? 2500000 : 60000, &hopTime);
        if (hopOk && solveTime) *solveTime += hopTime;
        if (hopsOut) (*hopsOut)++;
        gHopExpanded[level][0] += n; gHopExpanded[level][1]++; gHopExpanded[level][2] = std::max(gHopExpanded[level][2], n);
        if (!hopOk) {
            if (failedHop) *failedHop = (int)i;
            return false;
        }
    }
    // the Grand Kraken's snap points: each is proven on a snapped copy - every hop from the ship before to the ship
    // after, with the torn-out waypoints dropped and the sunk half's moved down a row. Unproven ones are never used.
    if (snapMask) {
        *snapMask = 0;
        for (size_t k = 0; k < gl.snaps.size() && k < 31; k++) {
            const GenSnap& s = gl.snaps[k];
            PlatformState q = p;
            ApplySnapTiles(q, s);
            std::vector<GenWaypoint> wps;
            for (const GenWaypoint& w0 : gl.path) {
                GenWaypoint w = w0;
                if (w.tx < s.x0 - 14 || w.tx > s.x1 + 18 || (w.tx >= s.col - 1 && w.tx <= s.col + 1)) continue;
                if (s.sink && w.tx >= SnapSinkA(s) && w.tx <= SnapSinkB(s)) w.ty += 1;
                wps.push_back(w);
            }
            bool ok = wps.size() >= 2;
            for (size_t i = 0; ok && i + 1 < wps.size(); i++) {
                const GenWaypoint &a = wps[i], &b = wps[i + 1];
                if (!gValidateShafts && (b.tag == SetPiece::ShaftUp || b.tag == SetPiece::ShaftDown || b.tag == SetPiece::BarnacleShaft)) continue;
                q.pos = {a.tx * (float)T + 6, (a.ty - q.genTop + 1) * (float)T - PH};
                q.vel = {0, 0};
                q.coyote = q.wallCoyote = q.jumpBuffer = q.wallLock = q.time = 0;
                q.onGround = false;
                long n = 0;
                ok = Crossable(q, b.tx * (float)T + 16, jets, n, (b.ty - q.genTop + 1) * (float)T - PH, 22, 60000);
            }
            if (ok) *snapMask |= 1u << k;
        }
    }
    return true;
}
}  // namespace

// Proves the shaft templates the generator is allowed to use, and reports the tallest climbable up-shaft per width.
static int VerifyShafts() {
    int bad = 0;
    for (int barnacle = 0; barnacle < 2; barnacle++)
        for (int iw = 3; iw <= 4; iw++) {
            int maxUp = 0, maxDown = 0;
            for (int up = 0; up < 2; up++)
                for (int Hs = 5; Hs <= 14; Hs++) {
                    GenLevel gl = ShaftTemplate(iw, Hs, up != 0, barnacle != 0);
                    PlatformState p;
                    p.level = PL_HULL; p.hard = true; p.verifying = true;
                    BuildFromGrid(p, gl, nullptr, '.', '.');
                    p.exitOpen = false;
                    p.pos = {gl.path[0].tx * (float)T + 6, (gl.path[0].ty - p.genTop + 1) * (float)T - PH};
                    long n = 0;
                    bool ok = Crossable(p, gl.path[1].tx * (float)T + 16, false, n, (gl.path[1].ty - p.genTop + 1) * (float)T - PH, 22, 1500000);
                    if (ok) (up ? maxUp : maxDown) = Hs; else if (!up || Hs <= 6) bad += !up;
                    if (!ok && up) break;
                }
            printf("shaft, interior %d wide%s: climbable up to %d rows, drops up to %d rows\n", iw, barnacle ? ", barnacles" : "", maxUp, maxDown);
            fflush(stdout);
        }
    return bad;
}

// DEPTH_METRICS=1 depth.exe --verify : how hard is each generated level? The optimal solve time (the path search's best run,
// shaft climbs included), how wide it is, and how many hazards and enemies it holds, averaged over several seeds.
static void PrintLevelMetrics() {
    gValidateShafts = true;
    const char* names[PL_COUNT] = {"Pipes", "Hull", "Pirate Ship", "Island", "Cave", "Weeds", "Atlantis"};
    for (int lv = 0; lv < PL_COUNT; lv++) {
        double time = 0, wide = 0, hops = 0, hazards = 0, foes = 0, coins = 0;
        int n = 0;
        for (int seed = 1; seed <= 6; seed++) {
            GenLevel gl;
            bool ok = false;
            float tsolve = 0; int hp = 0;
            for (int k = 0; k < 40 && !ok; k++) { gl = GenerateLevel(lv, seed * 1000 + k, 1.0f); tsolve = 0; hp = 0; ok = ValidateGenerated(lv, gl, nullptr, &tsolve, &hp); }
            if (!ok) continue;
            int hz = 0, en = 0, co = 0;
            for (auto& row : gl.rows) for (char c : row) { if (c == 'g' || c == 'x' || c == 't') hz++; if (c == 'c' || c == 'P' || c == 'G' || c == 'p' || c == 'e') en++; if (c == 'o') co++; }
            time += tsolve; wide += gl.w; hops += hp; hazards += hz; foes += en; coins += co; n++;
        }
        if (n) printf("%-12s optimal run %5.1f s | %5.0f wide | %4.1f hops checked | %4.1f hazards | %4.1f enemies | %4.1f coins\n", names[lv], time / n, wide / n, hops / n, hazards / n, foes / n, coins / n);
        fflush(stdout);
    }
}

int VerifyPlatformLevels() {
    if (const char* chk = getenv("DEPTH_CHECK")) { // DEPTH_CHECK=<level>:<seed>[:<scale%>]: validate one layout and report each hop
        int lv = 0, seed = 0, sc = 100; sscanf(chk, "%d:%d:%d", &lv, &seed, &sc);
        GenLevel gl = GenerateLevel(lv, (unsigned)seed, sc / 100.0f);
        gValidateShafts = true;
        int hop = -1;
        bool ok = ValidateGenerated(lv, gl, &hop);
        printf("level %d seed %d scale %d: %s", lv, seed, sc, ok ? "crossable\n" : "FAILED");
        if (!ok) printf(" at hop %d -> waypoint (%d,%d)\n", hop, gl.path[hop + 1].tx, gl.path[hop + 1].ty);
        return ok ? 0 : 1;
    }
    if (getenv("DEPTH_METRICS")) { PrintLevelMetrics(); return 0; }
    gValidateShafts = getenv("DEPTH_FULL") != nullptr;
    int failures = getenv("DEPTH_SHAFTS") ? VerifyShafts() : 0; // slow: set DEPTH_SHAFTS=1 to re-prove the shaft dimensions
    for (int lv = 0; lv < PL_COUNT; lv++) {
        const LevelDef& L = Lv(lv);
        const int SEEDS = gValidateShafts ? 2 : 4;
        int firstTry = 0, totalAttempts = 0, worstAttempts = 0, hopsFailed = 0, snapsTried = 0, snapsProven = 0;
        for (int seed = 1; seed <= SEEDS; seed++) {
            int attempts = 0;
            bool ok = false;
            for (int k = 0; k < 150 && !ok; k++) {
                attempts++;
                GenLevel gl = GenerateLevel(lv, seed * 1000 + k, 1.0f - (k / 20) * 0.06f);
                int hop = -1;
                auto t0 = std::chrono::steady_clock::now();
                unsigned mask = 0;
                ok = ValidateGenerated(lv, gl, &hop, nullptr, nullptr, lv == PL_PIRATE ? &mask : nullptr);
                if (ok && lv == PL_PIRATE) { snapsTried += (int)std::min<size_t>(31, gl.snaps.size()); for (unsigned m = mask; m; m &= m - 1) snapsProven++; }
                if (!ok && hop >= 0 && k < 6) printf("    draw %d failed at hop %d -> waypoint (%d,%d) tag %d\n", k, hop, gl.path[hop + 1].tx, gl.path[hop + 1].ty, (int)gl.path[hop + 1].tag);
                gMs[lv] += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(); gDraws[lv]++;
                if (!ok && k == 0) hopsFailed++;
                if (ok && k == 0) firstTry++;
            }
            failures += !ok;
            totalAttempts += attempts;
            worstAttempts = std::max(worstAttempts, attempts);
        }
        GenLevel sample = GenerateLevel(lv, 4242, 1.0f);
        int sp = 0;
        for (int c : sample.setPieces) sp += c;
        printf("%-16s %d seeds: %d valid at the first draw, %.1f draws on average (worst %d)  |  sample: %d wide, %d hops, %d set-pieces\n",
               L.name, SEEDS, firstTry, totalAttempts / (float)SEEDS, worstAttempts, sample.w, (int)sample.path.size() - 1, sp);
        printf("    validation: %.0f ms per draw on average\n", gMs[lv] / std::max(1, gDraws[lv]));
        if (lv == PL_PIRATE) printf("    Grand Kraken snap points: %d proven crossable of %d proposed\n", snapsProven, snapsTried);
        printf("    hops searched: %ld, %.0f states each on average, largest %ld\n", gHopExpanded[lv][1], gHopExpanded[lv][0] / (double)std::max(1L, gHopExpanded[lv][1]), gHopExpanded[lv][2]);
        fflush(stdout);
        if (PlatHasArena(lv)) { // the arenas: from the landing to the exit, with the boss switched on and off
            for (int nb = 0; nb < 2; nb++) {
                PlatformState p;
                p.level = lv; p.hard = true; p.verifying = true;
                GenLevel gl = GenerateLevel(lv, 4242, 0.8f);
                BuildFromGrid(p, gl, nb ? &L.lastNoBoss : &L.last, L.fill, L.fillAbove);
                p.exitOpen = false;
                const GenWaypoint& a = gl.path.back();
                p.pos = {a.tx * (float)T + 6, (a.ty - p.genTop + 1) * (float)T - PH};
                float goal = 0;
                for (int r = 0; r < p.h; r++) for (int x = gl.w; x < p.w; x++) if (p.tiles[r][x] == 'E') goal = x * (float)T;
                long n = 0;
                bool ok = Crossable(p, goal, false, n);
                failures += !ok;
                printf("%-16s %s arena: %s  (%ld states searched)\n", L.name, nb ? "no-boss" : "boss", ok ? "crossable" : "NOT CROSSABLE", n);
                fflush(stdout);
            }
        }
    }
    printf(failures ? "%d level(s) failed.\n" : "All generated levels can be crossed.\n", failures);
    return failures;
}
