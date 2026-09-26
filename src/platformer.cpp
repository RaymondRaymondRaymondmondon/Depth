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
        d[PL_PIPES] = {"The Pipes", Part{}, Part{}, 2, 65, '#', true, '#'};
        d[PL_HULL] = {"The Hull", P16(HULL_ARENA), P16(HULL_ARENA_NOBOSS), 2, 165, '.', false, '.'};
        d[PL_PIRATE] = {"The Pirate Ship", P(CABIN_ARENA, 0, 2), P(CABIN_ARENA_NOBOSS, 0, 2), 5, 230, '#', false, '.'};
        return d;
    }();
    return defs[level];
}
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
    return c == '#' || c == 't' || c == '=' || c == '|' || c == 'k' || c == 'f' || c == 'v' || c == 'b' || c == 'r';
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
bool JetOn(const PlatformState& p, int tx) { return JetCycle(p, tx) < 1.1f; }

Rectangle PlayerBox(const PlatformState& p) { return {p.pos.x + 3, p.pos.y + 3, PW - 6, PH - 5}; }

bool TouchesHazard(const PlatformState& p) {
    Rectangle pr = PlayerBox(p);
    int x0 = (int)floorf(pr.x / T), x1 = (int)floorf((pr.x + pr.width) / T);
    int y0 = (int)floorf(pr.y / T), y1 = (int)floorf((pr.y + pr.height) / T);
    for (int ty = y0; ty <= y1 + 3; ty++)
        for (int tx = x0; tx <= x1; tx++) {
            char c = At(p, tx, ty);
            if (c == 'x' && ty <= y1 && CheckCollisionRecs(pr, {tx * (float)T + 3, ty * (float)T + 12, T - 6.0f, T - 12.0f})) return true;
            if (c == 'g' && ty <= y1 && CheckCollisionCircleRec({tx * (float)T + 16, ty * (float)T + 16}, 13, pr)) return true;
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
        if (e.state == 2 && e.timer > 0.08f) {
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
        if (e.type == 'c') { // crabs walk, turning at walls and ledges
            float nx = e.pos.x + e.dir * 70 * dt;
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
                case 2: if (e.timer > AMB_STAB) { e.state = 3; e.timer = 0; } break;
                default: if (e.timer > AMB_BACK) { e.state = 0; e.timer = -1.1f; } break; // a pause before he'll come out again
            }
            e.pos = {e.home.x + 5 + e.dir * Lunge(e), e.home.y + T - 30};
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
            float cyc = fmodf(e.t, 2.6f), bottom = e.home.y + 3.0f * T + 40, apex = e.home.y - 3.0f * T;
            e.pos.y = cyc < 1.2f ? bottom - (bottom - apex) * sinf(PI * cyc / 1.2f) : bottom + 200;
        }
    }
}

// Musket balls fly straight until they hit something; bombs arc, bounce, and go off.
constexpr float BOMB_FUSE = 1.3f, BLAST_R = 46;
void UpdateShots(PlatformState& p, float dt) {
    for (auto& s : p.shots) {
        s.life -= dt;
        if (s.kind == 0) {
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
// ---------------------------------------------------------------- building a level
// The generator (levelgen.cpp) supplies the terrain; a boss arena, when there is one, is joined onto its last
// platform. Whatever the level doesn't cover is filled with `fill` (below) or `fillAbove` (above).
void ScanTiles(PlatformState& p) {
    p.enemies.clear();
    p.boss = PlatBoss{};
    for (int r = 0; r < p.h; r++)
        for (int c = 0; c < p.w; c++) {
            char& ch = p.tiles[r][c];
            float x = c * (float)T, y = r * (float)T;
            switch (ch) {
                case 'S': p.startPos = {x + 6, y + T - PH}; break;
                case 'c': p.enemies.push_back({'c', {x - 1, y + T - 18}, {x, y}, -1, 0}); break;
                case 'P': p.enemies.push_back({'P', {x + 5, y + T - 30}, {x, y}, -1, 0}); break;
                case 'G': p.enemies.push_back({'G', {x + 6, y + T - 30}, {x, y}, -1, 0, 0, Rnd(0, 1.2f)}); break;
                case 'p': p.enemies.push_back({'p', {x + 16, y + 16}, {x + 16, y + 16}, 1, 0}); break;
                case 'e': p.enemies.push_back({'e', {x + 16, y + 400}, {x + 16, y}, 1, c * 0.37f}); break;
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

// Builds (or rebuilds, after a death) the whole level from its seed: coins, enemies and the boss all come back.
void BuildLevel(PlatformState& p) {
    const LevelDef& L = Lv(p.level);
    unsigned seed = p.layout.empty() ? 1u : (unsigned)p.layout[0];
    float scale = p.layout.size() > 1 ? p.layout[1] / 100.0f : 1.0f;
    GenLevel gl = GenerateLevel(p.level, seed, scale);
    const Part* arena = p.level == PL_PIPES ? nullptr : &(p.bossEnabled ? L.last : L.lastNoBoss);
    BuildFromGrid(p, gl, arena, L.fill, L.fillAbove);
    if (!p.hard) // Normal: no spinning hazards, and the jets are left cold (plain floor)
        for (auto& row : p.tiles)
            for (char& c : row) c = c == 'g' ? '.' : c == 't' ? '#' : c;
    p.coins = 0;
    p.relic = -1;
    p.shots.clear();
    p.checkpointChunk = 0;
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
bool WallIsBarnacle(const PlatformState& p, int side) { // the wall you are wall-jumping off is lined with barnacles: springy
    float x = side > 0 ? p.pos.x + PW + 0.5f : p.pos.x - 0.5f;
    int tx = (int)floorf(x / T);
    for (int ty = (int)floorf((p.pos.y + 4) / T); ty <= (int)floorf((p.pos.y + PH - 4) / T); ty++) if (At(p, tx, ty) == 'b') return true;
    return false;
}
constexpr float GHOST_SPEED = 1.6f;
void StepPlayer(PlatformState& p, float dir, bool jumpHeld) {
    if (p.wallLock > 0) {
        p.wallLock -= STEP;
        if (dir == (float)p.lockSide) dir = 0; // right after a wall jump, pushing back into the wall is ignored
    }
    float target = dir * RUN * (1 + p.speedPct / 100.0f), accel; // the lead hero's relics (a syringe) quicken the run
    if (p.onGround) accel = dir == 0 ? DECEL_GROUND : p.vel.x * dir < 0 ? DECEL_GROUND + ACCEL_GROUND : ACCEL_GROUND;
    else accel = dir == 0 ? DECEL_AIR : ACCEL_AIR;
    if (p.vel.x < target) p.vel.x = std::min(target, p.vel.x + accel * STEP);
    else if (p.vel.x > target) p.vel.x = std::max(target, p.vel.x - accel * STEP);
    if (dir != 0 && p.wallLock <= 0) p.facingRight = dir > 0;

    // wall slide: in the air, pressing into a wall
    p.wallSide = 0;
    if (!p.onGround && dir != 0 && TouchWall(p, (int)dir)) p.wallSide = (int)dir;
    if (p.wallSide) { p.wallCoyote = 0.08f; p.lockSide = p.wallSide; }
    else p.wallCoyote -= STEP;

    p.coyote = p.onGround ? COYOTE : p.coyote - STEP;
    p.jumpBuffer -= STEP;
    if (p.jumpBuffer > 0) {
        if (p.coyote > 0) {
            p.vel.y = -JUMP_V * (1 + p.jumpPct / 100.0f);
            p.coyote = p.jumpBuffer = 0;
            p.onGround = false;
            p.scale = {0.72f, 1.32f};
            Dust(p, {p.pos.x + PW / 2, p.pos.y + PH}, 5, -p.vel.x / RUN);
            if (p.level == PL_HULL) Bubbles(p, {p.pos.x + PW / 2, p.pos.y + PH - 4}, 5);
        } else if (p.wallCoyote > 0) {
            float spring = WallIsBarnacle(p, p.lockSide) ? 1.5f : 1.0f; // barnacle springboards fling you 1.5x
            p.vel.x = -p.lockSide * WALLJUMP_VX * spring;
            p.vel.y = -WALLJUMP_VY * spring;
            p.wallLock = WALL_LOCK;
            p.wallCoyote = p.jumpBuffer = 0;
            p.facingRight = p.lockSide < 0;
            p.scale = {0.75f, 1.28f};
            Dust(p, {p.lockSide > 0 ? p.pos.x + PW : p.pos.x, p.pos.y + PH / 2}, 5, (float)-p.lockSide);
        }
    }

    // seaweed: it slows a fall, lets the diver hang on, and can be climbed up or down
    {
        p.onWeed = false;
        for (int ty = (int)floorf((p.pos.y + 2) / T); ty <= (int)floorf((p.pos.y + PH - 2) / T) && !p.onWeed; ty++)
            for (int tx = (int)floorf((p.pos.x + 3) / T); tx <= (int)floorf((p.pos.x + PW - 3) / T); tx++) if (At(p, tx, ty) == 'w') { p.onWeed = true; break; }
        if (p.onWeed) p.coyote = COYOTE; // you can always jump off it
    }
    if (!p.verifying && p.level == PL_HULL && GetRandomValue(0, 160) == 0) // the diver's exhaled air rises from the helmet
        p.particles.push_back({{p.pos.x + PW / 2 + (p.facingRight ? 5.0f : -5.0f), p.pos.y + 3}, {(float)GetRandomValue(-10, 10), -36}, 1.8f, 1.8f, -(float)GetRandomValue(2, 4), Color{196, 236, 250, 255}});
    // a steam vent's column lifts whoever is in it
    {
        int vx = (int)floorf((p.pos.x + PW / 2) / T), vy = (int)floorf((p.pos.y + PH - 1) / T);
        for (int k = 0; k <= 5; k++)
            if (At(p, vx, vy + k) == 'v') {
                if (VentOn(p, vx)) p.vel.y = std::max(p.vel.y - 9000 * STEP, -540.0f);
                break;
            }
    }
    // gravity is stronger once the jump button is released (short hops) and when falling (snappy arcs)
    p.vel.y += (p.vel.y < 0 ? (jumpHeld ? GRAV_UP : GRAV_UP_RELEASED) : GRAV_DOWN) * STEP;
    if (p.wallSide && p.vel.y > WALL_SLIDE) p.vel.y = std::max(WALL_SLIDE, p.vel.y - 6000 * STEP);
    if (p.onWeed) { // hanging in the weed
        if (p.climbDir != 0) p.vel.y = p.climbDir * 150.0f;
        else p.vel.y = std::min(p.vel.y, 60.0f);
        p.vel.x = std::clamp(p.vel.x, -140.0f, 140.0f);
    }
    p.vel.y = std::min(p.vel.y, MAX_FALL);

    bool was = p.onGround, wall;
    float fallSpeed = p.vel.y;
    MoveAndCollide(p, p.pos, p.vel, PW, PH, STEP, p.onGround, wall);
    if (p.onGround && !was) {
        p.scale = {1.0f + std::min(0.35f, fallSpeed / 2400), 1.0f - std::min(0.3f, fallSpeed / 2800)};
        if (fallSpeed > 400) Dust(p, {p.pos.x + PW / 2, p.pos.y + PH}, 6, 0);
        if (fallSpeed > 300 && p.level == PL_HULL) Bubbles(p, {p.pos.x + PW / 2, p.pos.y + PH - 3}, 6, 12);
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

    // a backpack's wide reach: coins within a bigger radius are gathered too
    if (p.pickupPct > 0) {
        float ex = 32.0f * p.pickupPct / 100.0f;
        Rectangle wide = PlayerBox(p);
        wide = {wide.x - ex, wide.y - ex, wide.width + 2 * ex, wide.height + 2 * ex};
        for (int ty = (int)floorf(wide.y / T); ty <= (int)floorf((wide.y + wide.height) / T); ty++)
            for (int tx = (int)floorf(wide.x / T); tx <= (int)floorf((wide.x + wide.width) / T); tx++)
                if (At(p, tx, ty) == 'o') {
                    p.tiles[ty][tx] = '.';
                    p.coins++;
                    CoinPop(p, {tx * (float)T + 16, ty * (float)T + 16});
                }
    }
    // coins, the exit, hazards and falling out of the level
    Rectangle pr = PlayerBox(p);
    for (int ty = (int)floorf(pr.y / T); ty <= (int)floorf((pr.y + pr.height) / T); ty++)
        for (int tx = (int)floorf(pr.x / T); tx <= (int)floorf((pr.x + pr.width) / T); tx++) {
            char c = At(p, tx, ty);
            if (c == 'o') {
                p.tiles[ty][tx] = '.';
                p.coins++;
                CoinPop(p, {tx * (float)T + 16, ty * (float)T + 16});
            } else if (c == 'E' && p.exitOpen) {
                p.finished = true;
            }
        }
    if (TouchesHazard(p) || p.pos.y > DeathY(p)) Die(p);
}

// ---------------------------------------------------------------- drawing: backgrounds
// Every level's scenery is several layers deep; each layer scrolls at its own speed, so the far
// ones barely move and the near ones sweep past.
float Hs(float x) { float s = sinf(x * 12.9898f + 3.1f) * 43758.5453f; return s - floorf(s); }

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
        farLayer = {0.3f, 0.3f, [cw, ch](const PlatformState& p, float t, float ox, float oy) { // the deep pipe matrix, lost in fog
            (void)p;
            DrawRectangle(0, 0, (int)cw, (int)ch, Color{16, 14, 14, 255});
            float px0 = fmodf(ox, 90), py0 = fmodf(oy + 40000, 90);
            for (float x = -px0 - 90; x < cw + 90; x += 90) { // a lattice of thin pipes with junction flanges and tiny lamps
                DrawRectangle((int)x, 0, 7, (int)ch, Color{30, 26, 24, 255}); DrawRectangle((int)x + 1, 0, 2, (int)ch, Color{42, 36, 32, 255});
            }
            for (float y = -py0 - 90; y < ch + 90; y += 90) { DrawRectangle(0, (int)y, (int)cw, 7, Color{30, 26, 24, 255}); DrawRectangle(0, (int)y + 1, (int)cw, 2, Color{42, 36, 32, 255}); }
            for (float x = -px0 - 90; x < cw + 90; x += 90)
                for (float y = -py0 - 90; y < ch + 90; y += 90) {
                    DrawRectangle((int)x - 3, (int)y - 3, 13, 13, Color{38, 32, 28, 255}); DrawRectangle((int)x - 1, (int)y - 1, 9, 9, Color{54, 46, 38, 255});
                    if (Hs(x * 0.13f + y * 0.07f + ox * 0.001f) > 0.72f) { float fl = 0.6f + 0.4f * sinf(t * 3 + x); DrawRectangle((int)x + 1, (int)y + 1, 5, 5, Fade(Color{255, 170, 70, 255}, 0.55f * fl)); }
                }
            DrawRectangle(0, 0, (int)cw, (int)ch, Fade(Color{10, 8, 8, 255}, 0.35f)); // fog
        }};
        midLayer = {0.6f, 0.6f, [cw, ch](const PlatformState& p, float t, float cx, float cyv) {
            Layer(cx / 0.6f, 0.5f, 150, cw, [&](float x, float wx) { // big mains running behind the plating
                DrawRectangle((int)x, 0, 26, (int)ch, Color{48, 40, 34, 255});
                DrawRectangle((int)x + 4, 0, 5, (int)ch, Color{64, 54, 46, 255});
                float fy = fmodf(wx * 7 - cyv * 0.5f + 40000, 170.0f);
                DrawRectangle((int)x - 4, (int)fy, 34, 8, Color{70, 58, 46, 255});
                DrawCircle((int)x + 13, (int)fy + 4, 9, Color{60, 50, 40, 255}); DrawCircle((int)x + 13, (int)fy + 4, 5, Color{150, 50, 40, 255});   // a valve wheel
            });
            (void)p;
            float ox = fmodf(cx * 1.4f, 40), oy = fmodf(cyv * 1.4f + 40000, 40);
            for (float y = -oy - 40; y < ch + 40; y += 40)      // riveted plates
                for (float x = -ox - 40; x < cw + 40; x += 40) {
                    DrawRectangle((int)x + 1, (int)y + 1, 38, 38, Color{40, 35, 32, 200});
                    DrawRectangle((int)x + 1, (int)y + 1, 38, 2, Color{54, 47, 42, 255});
                    for (int k = 0; k < 4; k++) DrawPixel((int)x + 4 + (k % 2) * 31, (int)y + 4 + (k / 2) * 31, Color{80, 68, 58, 255});
                }
            for (int k = 0; k < 3; k++) { // pipes across the plating
                float y = fmodf(k * 130.0f - cyv * 1.4f + 40000, 390.0f) - 20;
                DrawRectangle(0, (int)y, (int)cw, 9, Color{70, 50, 36, 255});
                DrawRectangle(0, (int)y + 2, (int)cw, 2, Color{110, 80, 54, 255});
            }
            Layer(cx * 1.4f, 0.85f, 70, cw, [&](float x, float wx) { // water dripping from the seams
                float ph = fmodf(t * 0.7f + Hs(wx) * 5, 1.0f);
                bool dark = Hs(wx + 3) > 0.4f;
                DrawRectangle((int)x, (int)(ph * ch), 1, 3, dark ? Color{80, 30, 26, 180} : Color{120, 150, 160, 160});
                if (dark && ph > 0.85f) DrawCircle((int)x, (int)ch - 4, 2, Color{60, 22, 20, 140});
            });
            Layer(cx, 0.5f, 260, cw, [&](float x, float wx) { // claw marks raked across a plate
                if (Hs(wx + 11) < 0.62f) return;
                float y = 60 + Hs(wx + 12) * (ch - 160);
                for (int k = 0; k < 4; k++) DrawLineEx({x - 14.0f + k * 9, y}, {x + 8.0f + k * 9, y + 46}, 2, Color{18, 14, 12, 200});
            });
            Layer(cx, 0.5f, 340, cw, [&](float x, float wx) { // something watches from a gap, then isn't there
                float cyc = fmodf(t * 0.11f + Hs(wx + 20) * 9, 9.0f);
                if (cyc > 1.6f) return;
                float a = std::min(1.0f, cyc * 4) * std::min(1.0f, (1.6f - cyc) * 4);
                float y = 140 + Hs(wx + 21) * (ch - 280);
                for (int s = -1; s <= 1; s += 2) DrawCircleV({x + s * 4.0f, y}, 1.4f, Fade(Color{220, 60, 50, 255}, a));
            });
            if (fmodf(t * 0.6f + cyv * 0.002f, 5.0f) < 0.12f) DrawRectangle(0, 0, (int)cw, (int)ch, Fade(BLACK, 0.35f)); // the lamp flicker
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
            float hx = 700 - ox * 1.2f; // the Nautilus, looming overhead, portholes lit
            DrawRectangleRounded({hx - 520, 40, 1040, 70}, 1.0f, 24, Color{10, 26, 38, 255});
            DrawRectangleRounded({hx - 80, 22, 160, 30}, 0.6f, 8, Color{10, 26, 38, 255});
            for (int k = 0; k < 16; k++) DrawCircleV({hx - 450 + k * 60.0f, 78}, 3, Color{255, 210, 130, 255});
            Layer(ox / 0.1f, 0.1f, 300, cw, [&](float x, float wx) { // the ribs of a great sunken hull, far off
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
                if (x > 110 && x < cw - 110) return; // only at the edges of the lens: kelp across the middle of the frame hides the platforms
                Vector2 prev{x, ch + 4};
                for (int s = 1; s <= 8; s++) {
                    Vector2 q{x + sinf(t * 0.8f + wx + s * 0.5f) * s * 2.2f, ch - s * 20.0f * (0.6f + Hs(wx) * 0.5f)};
                    DrawLineEx(prev, q, 8.0f - s * 0.85f, Fade(Color{4, 12, 14, 255}, 0.88f));
                    prev = q;
                }
            });
            for (int k = 0; k < 8; k++) { float bx = fmodf(k * 173.0f - ox * 0.5f + 9000, cw + 100), by = ch - fmodf(t * (16 + k * 4) + k * 60, ch); DrawCircleLines((int)bx, (int)by, 3 + k % 3, Fade(Color{190, 230, 250, 255}, 0.4f)); }
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
                float y = 240 + Hs(wx) * 16;
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
                float y = 250 + Hs(wx) * 10 + sinf(t * 0.8f + wx) * 2;
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
            Layer(ox / 1.3f, 1.3f, 520, cw, [&](float x, float wx) { // a lantern swinging from a spar, black against the sky
                float sw = sinf(t * 1.1f + wx) * 5;
                DrawLineEx({x, 30}, {x + sw, 78}, 2, Color{6, 6, 10, 255}); DrawRectangle((int)(x + sw) - 5, 78, 10, 14, Color{6, 6, 10, 255});
                DrawRectangle((int)(x + sw) - 2, 82, 4, 6, Color{255, 200, 100, 255});
            });
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
            DrawTri({mx - half + 6, yy + 8}, {mx + half - 6, yy + 8}, {mx + half - 10 + belly, yy + h}, sail);
            DrawTri({mx - half + 6, yy + 8}, {mx + half - 10 + belly, yy + h}, {mx - half + 10 + belly, yy + h}, sailDk);
            for (int k = 1; k < 4; k++) DrawLineEx({mx - half + k * half / 2, yy + 8}, {mx - half + k * half / 2 + belly, yy + h}, 1, Color{160, 144, 118, 255});
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
            break; // just the top surface in each column
        }
    }
}

// ---------------------------------------------------------------- drawing: tiles
// Auto-tiling: a bitmask of which of a tile's four neighbours match (1 = north, 2 = east, 4 = south, 8 = west), so pipes,
// coral, timbers and ropes can draw the right junction, corner, cap or open face instead of a disjointed block.
uint8_t SolidMask(const PlatformState& p, int x, int y) {
    return (Solid(p, x, y - 1) ? 1 : 0) | (Solid(p, x + 1, y) ? 2 : 0) | (Solid(p, x, y + 1) ? 4 : 0) | (Solid(p, x - 1, y) ? 8 : 0);
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
void DrawSolid(const PlatformState& p, int x, int y) {
    float px = x * (float)T, py = y * (float)T;
    bool topEdge = !Solid(p, x, y - 1);
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
        } break;        default: {
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
        DrawRectangle((int)px + 5, (int)py + 5, T, T, Color{0, 0, 0, 80});
        return;
    }
    if (c != '#' && c != 't' && c != 'k') return;
    Color topC, sideC;
    switch (p.level) {
        case PL_PIPES: topC = {112, 96, 82, 255}; sideC = {40, 34, 30, 255}; break;
        case PL_HULL: topC = {96, 126, 138, 255}; sideC = {34, 50, 60, 255}; break;
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
    Color plate = p.level == PL_PIPES ? Color{184, 140, 60, 255} : p.level == PL_HULL ? Color{128, 78, 46, 255} : Color{92, 70, 60, 255};
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
        Color rust = p.level == PL_PIRATE ? Color{70, 44, 26, 150} : Color{120, 62, 34, 150};
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
                DrawRectangle((int)px + 10, (int)py - 36, 11, 9, Color{(unsigned char)(200 + 55 * fl), (unsigned char)(150 + 60 * fl), 60, 255});
                DrawRectangle((int)px + 7, (int)py - 40, 17, 3, Color{60, 58, 62, 255});
            }
            break;
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
        case 'w': { // seaweed: a swaying ribbon of blades, climbable (hold up or down while it is around you)
            float wy = py, sway0 = sinf(p.time * 1.5f + x * 0.7f + wy * 0.05f) * 4, sway1 = sinf(p.time * 1.5f + x * 0.7f + (wy + T) * 0.05f) * 4;
            Vector2 a{px + 16 + sway0, py}, b{px + 16 + sway1, py + T};
            DrawLineEx({a.x + 1, a.y}, {b.x + 1, b.y}, 7, Color{8, 10, 14, 255});
            DrawLineEx(a, b, 4, Color{48, 130, 88, 255});
            DrawLineEx({a.x - 1, a.y}, {b.x - 1, b.y}, 1, Color{110, 190, 130, 255});
            for (int k = 0; k < 2; k++) { float ly = py + 6 + k * 14, lx = px + 16 + (sway0 + (sway1 - sway0) * (ly - py) / T); int side = (k + x) % 2 ? 1 : -1; DrawTri({lx, ly}, {lx + side * 12.0f, ly - 6}, {lx + side * 4.0f, ly + 4}, Color{58, 150, 100, 255}); }
        } break;        case 'f': { // fragile scaffolding: a rusted plank on brackets, shaking when it is about to go
            float shake = 0;
            for (auto& c : p.crumbles) if (c.tx == x && c.ty == y) shake = (fmodf(c.t * 40, 2) < 1 ? -2.0f : 2.0f) * std::min(1.0f, c.t * 4);
            float ox = px + shake;
            const Color ink{8, 8, 12, 255};
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
        case 'b': { // a wall crusted with barnacles: it springs a wall jump 1.5x
            DrawSolid(p, x, y);
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
            } else if (p.level == PL_HULL) { // an urchin nested in a barnacle-crusted seam
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
            DrawRectangle((int)px + 6, (int)py, T - 12, 5, Color{30, 30, 34, 255});
            float cyc = JetCycle(p, x);
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
            float bob = sinf(t * 4 + x) * 3, squeeze = fabsf(cosf(t * 3 + x));
            DrawEllipse((int)px + T / 2, (int)(py + T / 2 + bob), 9 * squeeze + 1, 9, Color{250, 210, 70, 255});
            DrawEllipse((int)px + T / 2 - 2, (int)(py + T / 2 - 3 + bob), 3 * squeeze, 3, Color{255, 246, 196, 255});
            if (fmodf(t * 1.1f + x * 0.9f, 3.0f) < 0.16f) { float gx = px + T / 2 + 6, gy = py + T / 2 - 8 + bob; DrawRectangle((int)gx - 4, (int)gy, 9, 1, WHITE); DrawRectangle((int)gx, (int)gy - 4, 1, 9, WHITE); }   // a glint
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
    if (!sliding) B(-4 + lean, -8, 1, 4, SUIT_SHADOW);
    B(-3 + lean, -9, 6, 5, SUIT_MAIN);
    B(-3 + lean, -9, 1, 5, SUIT_SHADOW);
    B(-3 + lean, -5, 6, 1, HELMET_DARK); B(0 + lean, -5, 1, 1, HELMET_LIGHT);
    B(-3 + lean, -9, 6, 1, HELMET_BASE);
    // the near arm: reaching for the wall when sliding, swinging with the stride otherwise
    if (sliding) { B(2 + lean, -12, 2, 5, SUIT_MAIN); B(2 + lean, -13, 2, 1, HELMET_BASE); }
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
            else if (c == 'o') DrawCircleV(m, 10, Color{120, 100, 30, 110});
            else if (c == 'g') DrawCircleV(m, 16, Color{90, 70, 30, 60});
            if (c == 'g') { float pl = 0.5f + 0.5f * sinf(t * 5 + x * 0.9f); DrawCircleV(m, 26, Color{200, 40, 30, (unsigned char)(20 + 40 * pl)}); }   // a mine's red heart pulses
            if (c == 'v' && VentOn(p, x)) DrawCircleV({m.x, m.y - T}, 26, Color{90, 100, 110, 30});
            if (CeilingLamp(p, x, y)) { // a pool of red light spilling down the duct
                float fl = 0.6f + 0.4f * sinf(t * 7 + x * 1.7f) * sinf(t * 3.1f + y);
                for (int k = 0; k < 4; k++) DrawCircleV({m.x, m.y + T + 8}, 30.0f + k * 22, Color{200, 40, 30, (unsigned char)((26 - k * 5) * fl)});
            }
            if (DeckLantern(p, x, y)) {
                float fl = 0.85f + 0.15f * sinf(t * 9 + x);
                for (int k = 0; k < 4; k++) DrawCircleV({m.x, m.y - 30}, 18.0f + k * 16, Color{255, 180, 80, (unsigned char)((30 - k * 6) * fl)});
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
        for (int k = 0; k < 3; k++) { // gulls, high over the fleet
            float gx = fmodf(t * (30 + k * 8) + k * 500 + p.camX * 0.3f, viewW + 500) + x0 - 200, gy = p.camY - 150 - k * 34 + sinf(t * 0.9f + k) * 14, fl = sinf(t * 7 + k * 2) * 5;
            Color gc{10, 10, 16, 255};
            DrawLineEx({gx - 9, gy + fl}, {gx, gy - 2}, 2, gc); DrawLineEx({gx, gy - 2}, {gx + 9, gy + fl}, 2, gc);
        }
    }
    (void)y1;
}
// The Ghost Ship's crew: bone, tatters and a pale green light in the sockets, in place of the living pirates.
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
void DrawEnemy(const PlatEnemy& e, float t) {
    float x = e.pos.x, y = e.pos.y, f = e.dir;
    switch (e.type) {
        case 'c': { // crab: a domed carapace, two-segment legs, eyestalks and claws that open and snap (CrabAnim)
            const Color INKC{8, 8, 12, 255};
            Color c{170, 84, 56, 255}, dk{104, 50, 38, 255}, lt{232, 140, 100, 255};
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
        } break;        case 'P': { // a cutthroat behind a door: you see his eyes through the gap, then the door bangs open
            float open = e.state == 1 ? e.timer / AMB_OUT : e.state == 2 ? 1 : e.state == 3 ? 1 - e.timer / AMB_BACK : 0;
            float dx = e.home.x, dy = e.home.y - T; // the doorway fills this tile and the one above
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
            float cx = x + 11, lean = e.state == 2 ? f * 3 : 0;
            if (gGhost) { // the skeleton pirate: same lunge, bare bone
                DrawSkeletonBody(x, y, f, t, lean);
                float ang = e.state == 1 ? -1.2f : e.state == 2 ? -0.05f : 0.7f;
                Vector2 hand{cx + f * 8 + lean, y + 14}, tip{hand.x + f * cosf(ang) * 18, hand.y + sinf(ang) * 18};
                DrawLineEx(hand, tip, 3.5f, Color{8, 8, 12, 255}); DrawLineEx(hand, tip, 2, Color{170, 210, 200, 255});
                if (e.state == 2 && e.timer < 0.15f) DrawLineEx({tip.x - f * 10, tip.y - 3}, {tip.x + f * 4, tip.y}, 1, Fade(Color{150, 255, 210, 255}, 0.8f));
                break;
            }
            Color skin{214, 164, 124, 255}, shirt{232, 228, 216, 255}, stripe{150, 36, 34, 255}, trousers{56, 46, 72, 255};
            DrawRectangleRec({cx - 6, y + 21, 5, 9}, trousers);
            DrawRectangleRec({cx + 1 + (e.state == 2 ? f * 3 : 0), y + 21, 5, 9}, trousers);
            DrawRectangleRec({cx - 7, y + 28, 6, 2}, Color{30, 24, 22, 255});
            DrawRectangleRec({cx - 8 + lean, y + 9, 16, 13}, shirt);                               // striped shirt
            for (int k = 0; k < 3; k++) DrawRectangleRec({cx - 8 + lean, y + 11 + k * 4.0f, 16, 2}, stripe);
            DrawRectangleRec({cx - 8 + lean, y + 19, 16, 2}, Color{40, 30, 24, 255});             // belt
            DrawCircle((int)(cx + lean), (int)y + 5, 6, skin);                                      // head
            DrawRectangleRec({cx - 6 + lean, y + 7, 12, 4}, Color{50, 36, 30, 255});                // stubble
            DrawRectangleRec({cx - 7 + lean, y - 2, 14, 5}, Color{196, 40, 36, 255});               // bandana
            DrawRectangleRec({cx - f * 9 + lean, y - 1, 3, 6}, Color{196, 40, 36, 255});            // its tails
            DrawRectangleRec({cx + f * 1 + lean, y + 2, 4, 2}, Color{20, 16, 16, 255});             // eye patch strap
            DrawCircle((int)(cx + f * 3 + lean), (int)y + 4, 1.5f, Color{255, 220, 150, 255});     // a mean eye
            // the cutlass: raised on the way out, thrust at the stab, trailing on the way back
            float ang = e.state == 1 ? -1.2f : e.state == 2 ? -0.05f : 0.7f;
            Vector2 hand{cx + f * 8 + lean, y + 14};
            Vector2 tip{hand.x + f * cosf(ang) * 18, hand.y + sinf(ang) * 18};
            DrawLineEx(hand, tip, 2.5f, Color{214, 220, 226, 255});
            DrawLineEx(hand, {hand.x + f * cosf(ang) * 6, hand.y + sinf(ang) * 6}, 3, Color{70, 50, 30, 255});
            DrawCircleV(hand, 2.5f, skin);
            if (e.state == 2 && e.timer < 0.15f) DrawLineEx({tip.x - f * 10, tip.y - 3}, {tip.x + f * 4, tip.y}, 1, Fade(WHITE, 0.8f)); // swish
        } break;
        case 'G': { // a musketeer crouched behind a barrel
            float cx = x + 11;
            bool aiming = e.state == 1;
            Color coat{40, 60, 110, 255}, coatDk{26, 40, 78, 255}, skin{206, 156, 118, 255};
            if (gGhost) DrawSkeletonBody(x, y, f, t, 0); else {
            DrawRectangleRec({cx - 6, y + 20, 5, 10}, Color{50, 40, 36, 255});
            DrawRectangleRec({cx + 1, y + 20, 5, 10}, Color{50, 40, 36, 255});
            DrawRectangleRec({cx - 8, y + 8, 16, 14}, coat);
            DrawRectangleRec({cx - 8, y + 8, 4, 14}, coatDk);
            DrawLineEx({cx - 8, y + 9}, {cx + 8, y + 20}, 2, Color{220, 210, 190, 255}); // bandolier
            DrawCircle((int)cx, (int)y + 4, 6, skin);
            DrawRectangleRec({cx - 5, y + 6, 10, 4}, Color{120, 80, 50, 255});          // a ginger beard
            DrawTri({cx - 11, y}, {cx + 11, y}, {cx, y - 9}, Color{30, 26, 34, 255});   // tricorn
            DrawRectangleRec({cx - 10, y - 1, 20, 3}, Color{30, 26, 34, 255});
            DrawCircle((int)(cx + f * 3), (int)y + 3, 1.5f, Pal::Ink);
            }
            // the musket: resting on his shoulder, or levelled at you
            Vector2 shoulder{cx + f * 2, y + 11};
            float ang = aiming ? atan2f(e.aim.y - shoulder.y, e.aim.x - shoulder.x) : (f > 0 ? -1.0f : PI + 1.0f);
            if (!aiming) ang = f > 0 ? -1.1f : PI + 1.1f;
            Vector2 muzzle{shoulder.x + cosf(ang) * 22, shoulder.y + sinf(ang) * 22};
            DrawLineEx({shoulder.x - cosf(ang) * 6, shoulder.y - sinf(ang) * 6}, {shoulder.x + cosf(ang) * 6, shoulder.y + sinf(ang) * 6}, 4, Color{110, 70, 40, 255});
            DrawLineEx(shoulder, muzzle, 2.5f, Color{70, 72, 80, 255});
            if (aiming) { // a fair warning: the glint at the muzzle, and where he's pointing
                float u = e.timer / GUN_AIM;
                for (float d = 30; d < 30 + 90 * u; d += 10) {
                    Vector2 q{shoulder.x + cosf(ang) * d, shoulder.y + sinf(ang) * d};
                    DrawRectangle((int)q.x, (int)q.y, 2, 2, Fade(Color{255, 90, 60, 255}, 0.35f + 0.3f * u));
                }
                if (fmodf(t * 12, 1.0f) < 0.5f) DrawCircleV(muzzle, 2.5f, Color{255, 230, 150, 255});
            }
        } break;        case 'p': { // parakeet
            float flap = sinf(t * 18) * 5;
            DrawEllipse((int)x, (int)y, 9, 6, Color{88, 126, 82, 255});
            DrawTri({x - 2, y - 2}, {x + 6, y - 2}, {x + 1, y - 8 - flap}, Color{140, 60, 52, 255});
            DrawCircle((int)(x + f * 8), (int)y - 3, 4, Color{104, 138, 90, 255});
            DrawTri({x + f * 11, y - 4}, {x + f * 16, y - 2}, {x + f * 11, y}, Color{250, 200, 60, 255});
            DrawCircle((int)(x + f * 9), (int)y - 4, 1.2f, Pal::Ink);
            DrawTri({x - f * 8, y}, {x - f * 16, y + 4}, {x - f * 8, y + 3}, Color{40, 120, 200, 255});
        } break;
        default: { // eel
            for (int k = 5; k >= 0; k--) {
                float sy = y - 16 + k * 7, sx = x + sinf(t * 10 + k) * 3;
                DrawCircle((int)sx, (int)sy, 7 - k * 0.6f, k % 2 ? Color{90, 130, 70, 255} : Color{110, 150, 80, 255});
            }
            DrawCircle((int)x - 3, (int)y - 19, 2, Color{250, 230, 60, 255});
            DrawTri({x - 5, y - 12}, {x + 5, y - 12}, {x, y - 7}, Color{240, 230, 220, 255});
        } break;
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
        if (s.kind == 0) {
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

namespace { bool ValidateGenerated(int level, const GenLevel& gl, int* failedHop, float* solveTime = nullptr, int* hopsOut = nullptr); }

// A layout is a generator seed and a difficulty scale (percent). Layouts are validated when they are made: every hop
// on the critical path is searched with the real movement code, and a level that fails is thrown away.
bool PlatLayoutValid(const Game& g, int level) {
    const std::vector<int>& l = g.platLayouts[level];
    return (l.size() == 2 || l.size() == 3) && l[0] > 0 && l[1] >= 40 && l[1] <= 100;
}

void GeneratePlatLayout(Game& g, int level) {
    const double start = GetTime();
    for (int attempt = 0; attempt < 150; attempt++) {
        unsigned seed = (unsigned)GetRandomValue(1, 999999);
        int scale = 100 - (attempt / 20) * 6; // a level that keeps failing is eased off
        if (GetTime() - start > 5.0) scale = 60; // never leave the window frozen for long: ease off hard once the budget is spent
        if (GetTime() - start > 8.0) break;
        GenLevel gl = GenerateLevel(level, seed, scale / 100.0f);
        if (ValidateGenerated(level, gl, nullptr)) {
            g.platLayouts[level] = {(int)seed, scale};
            if (level == PL_PIRATE && GetRandomValue(1, 100) <= 12) g.platLayouts[level].push_back(1); // rarely, the ship is a ghost ship
            return;
        }
    }
    g.platLayouts[level] = {1, 60};
}

std::string PlatLayoutCode(const Game& g, int level) {
    const std::vector<int>& l = g.platLayouts[level];
    if (l.size() < 2) return "(new)";
    return l.size() == 3 && l[2] ? TextFormat("#%06d GHOST", l[0]) : TextFormat("#%06d", l[0]);
}
void StartPlatform(Game& g, int level) {
    if (!PlatLayoutValid(g, level)) GeneratePlatLayout(g, level); // e.g. a save from before the generator
    g.plat = PlatformState{};
    g.plat.level = level;
    g.plat.layoutCode = PlatLayoutCode(g, level);
    g.plat.layout = g.platLayouts[level];
    g.plat.ghost = level == PL_PIRATE && g.plat.layout.size() == 3 && g.plat.layout[2] != 0;
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

// The darkness of the ducts, drawn in bands (it suits the pixel art) around the diver's helmet lamp.
static void DrawLampDarkness(Vector2 c, float r, float maxA) {
    const int B = 6;
    for (int i = 0; i < B; i++) {
        float r0 = r * (0.4f + 0.6f * i / B), r1 = r * (0.4f + 0.6f * (i + 1) / B);
        DrawRing(c, r0, r1, 0, 360, 48, Fade(BLACK, maxA * (i + 1) / (B + 1)));
    }
    DrawRing(c, r, 1600, 0, 360, 64, Fade(BLACK, maxA));
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
        UpdateBoss(p, ed);
        UpdateShots(p, ed);

        // touching an enemy is deadly; only a boss can be stomped
        if (p.deathTimer <= 0 && !p.finished) {
            Rectangle pr = PlayerBox(p);
            for (auto& e : p.enemies) if (EnemyHits(e, pr)) Die(p);
            for (auto& s : p.shots) if (ShotHits(s, pr)) Die(p);
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
            } else if (b.type == 'B' && !b.defeated && CheckCollisionRecs(pr, {b.pos.x + 5, b.pos.y, BB_W - 10, BB_H})) {
                if (b.state == 5) { // dazed: stomp him (brushing against him now is safe)
                    if (falling && p.pos.y + PH - p.vel.y * dt <= b.pos.y + 14 && b.invuln <= 0) {
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
            p.reward = p.coins * L.coinValue + (p.hard ? L.bonus * 3 / 2 : L.bonus);
            // Blackbeard is the reason the Pirate Ship pays out relics: beating him guarantees one, often two.
            if (p.level == PL_PIRATE && p.boss.type == 'B' && p.boss.defeated && !p.checkpoints) {
                p.relic = GetRandomValue(0, (int)Relics().size() - 1);
                if (GetRandomValue(1, 100) <= 45) p.relic2 = GetRandomValue(0, (int)Relics().size() - 1);
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
    for (auto& e : p.enemies) DrawEnemy(e, t);
    DrawShots(p, t);
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
    DrawCircle(440, 28, 10, Color{250, 210, 70, 255});
    Txt(TextFormat("x %d", p.coins), 458, 16, 22, Pal::Paper);
    int sections = (int)p.partX.size();
    Txt(TextFormat(p.checkpoints ? "Checkpoint %d/%d" : "Section %d/%d", std::min(p.checkpointChunk + 1, sections), sections), 530, 18, 19, Pal::Paper);

    Txt(TextFormat("Deaths %d", p.deaths), 720, 18, 19, Pal::Paper);
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
        DrawTextCenteredBold(p.level == PL_PIPES ? "Valve reached!" : p.level == PL_HULL ? "Back inside!" : "Treasure claimed!", panel.x + panel.width / 2, panel.y + 24, 34, Pal::Good);
        DrawTextCentered(TextFormat("%d coins x %d  +  %d bonus  =  %d gold", p.coins, L.coinValue, L.bonus, p.reward), panel.x + panel.width / 2, panel.y + 88, 21, Pal::Ink);
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
        const Strip strips[3] = {{PL_PIPES, "The Pipes", {".o..g..E", ".==.|...", "##tx#x##"}},
                                 {PL_HULL, "The Hull", {".o..g..E", "........", "##x#####"}},
                                 {PL_PIRATE, "The Pirate Ship", {".o..g..E", ".==.k...", "##tx#k##"}}};
        for (int i = 0; i < 3; i++) {
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
};

SimNode Snap(const PlatformState& p, bool held) {
    return {p.pos, p.vel, p.coyote, p.wallCoyote, p.jumpBuffer, p.wallLock, p.time, p.wallSide, p.lockSide, p.onGround, held, p.facingRight};
}

void Restore(PlatformState& p, const SimNode& n) {
    p.pos = n.pos; p.vel = n.vel; p.coyote = n.coyote; p.wallCoyote = n.wallCoyote; p.jumpBuffer = n.jumpBuffer;
    p.wallLock = n.wallLock; p.time = n.time; p.wallSide = n.wallSide; p.lockSide = n.lockSide;
    p.onGround = n.onGround; p.facingRight = n.facing; p.deathTimer = 0; p.finished = false;
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
        for (int dir = -1; dir <= 1; dir++)
            for (int held = 0; held <= 1; held++) {
                Restore(p, cur);
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
double gMs[3] = {}; int gDraws[3] = {};
bool gValidateShafts = false; // DEPTH_FULL=1: also search the shaft climbs of every generated level, not just the proven templates
long gHopExpanded[3][3] = {}; // per level: total expansions, hops searched, largest hop
bool ValidateGenerated(int level, const GenLevel& gl, int* failedHop, float* solveTime, int* hopsOut) {
    const LevelDef& L = Lv(level);
    PlatformState p;
    p.level = level;
    p.hard = true;
    p.verifying = true;
    BuildFromGrid(p, gl, level == PL_PIPES ? nullptr : &L.last, L.fill, L.fillAbove);
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
    const char* names[3] = {"Pipes", "Hull", "Pirate Ship"};
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
        int firstTry = 0, totalAttempts = 0, worstAttempts = 0, hopsFailed = 0;
        for (int seed = 1; seed <= SEEDS; seed++) {
            int attempts = 0;
            bool ok = false;
            for (int k = 0; k < 150 && !ok; k++) {
                attempts++;
                GenLevel gl = GenerateLevel(lv, seed * 1000 + k, 1.0f - (k / 20) * 0.06f);
                int hop = -1;
                auto t0 = std::chrono::steady_clock::now();
                ok = ValidateGenerated(lv, gl, &hop);
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
        printf("    hops searched: %ld, %.0f states each on average, largest %ld\n", gHopExpanded[lv][1], gHopExpanded[lv][0] / (double)std::max(1L, gHopExpanded[lv][1]), gHopExpanded[lv][2]);
        fflush(stdout);
        if (lv != PL_PIPES) { // the arenas: from the landing to the exit, with the boss switched on and off
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
