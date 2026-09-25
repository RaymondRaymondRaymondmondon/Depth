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
//    c  crab   e  leaping eel   P  pirate   p  parakeet
//    K  the Kraken   B  Blackbeard
//  Every section has floor in its two leftmost and rightmost columns, so any
//  section can follow any other, and each is a checkpoint.
// ============================================================================
#include "game.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <queue>
#include <unordered_set>

namespace {
constexpr int T = 32, CH_W = 24, CH_H = 16;
constexpr float PW = 20, PH = 26;
constexpr float RUN = 340, ACCEL_GROUND = 3400, DECEL_GROUND = 3800, ACCEL_AIR = 2500, DECEL_AIR = 1400;
constexpr float JUMP_V = 720, GRAV_UP = 2100, GRAV_UP_RELEASED = 5400, GRAV_DOWN = 3000, MAX_FALL = 980;
constexpr float WALL_SLIDE = 150, WALLJUMP_VX = 360, WALLJUMP_VY = 690, WALL_LOCK = 0.13f;
constexpr float COYOTE = 0.09f, JUMP_BUFFER = 0.12f, STEP = 1.0f / 240; // physics runs at a fixed 240 Hz
constexpr float ZOOM = 0.625f, HUD_PX = 28; // canvas pixels per world pixel; HUD height in canvas pixels

#define E "........................"
const char* START[CH_H] = {
    "########################", E, E, E, E, E, E, E, E, E, E, E, E,
    ".S......................",
    "########################",
    "########################",
};
const char* END_PIPES[CH_H] = {
    "########################", E, E, E, E, E, E, E, E, E, E, E, E,
    "...........E............",
    "########################",
    "########################",
};

// ---------------------------------------------------------------- the Pipes: pure platforming
const char* PIPES[][CH_H] = {
    {   // A: stepping stones - land on single posts across a pit
        "########################", E, E, E, E, E, E, E, E, E,
        "..........o.............",
        "......o...#......o......",
        "......#..........#......",
        E,
        "##....................##",
        "##....................##",
    },
    {   // B: low gears - a full jump hits the gear, so hop just high enough
        "########################", E, E, E, E, E, E, E, E,
        "......g.....g.....g.....",
        E, E,
        ".....o.....o.....o......",
        E,
        "####xxxx##xxxx##xxxx####",
        "########################",
    },
    {   // C: chimney - wall-jump up between the two walls
        "########################",
        E,
        "......#.................",
        "......#...o.............",
        "......#...##............",
        "......#...##............",
        "......#...##............",
        "......#...##............",
        "......#...##............",
        "......#...##............",
        "..........##............",
        "..........##............",
        "..........##.....o......",
        "..........##............",
        "############xxx#########",
        "########################",
    },
    {   // D: timed jets under a low ceiling
        "########################", E, E, E, E, E, E, E, E, E,
        "...####################.",
        E,
        "........o.......o.......",
        E,
        "####t###..#t##..##t#####",
        "########..####..########",
    },
    {   // E: long leaps upward
        "########################", E, E, E, E,
        ".................o......",
        "................###.....",
        E,
        "..........o.............",
        ".........###............",
        E, E,
        "...###..................",
        E,
        "##....................##",
        "##....................##",
    },
    {   // F: gear corridor - a low jump hits the gear, so jump high
        "########################", E, E, E, E, E, E, E,
        "........g......g........",
        E,
        "....o......o......o.....",
        "....##.....##.....##....",
        E, E,
        "##xxxxxxxxxxxxxxxxxxxx##",
        "########################",
    },
    {   // G: pillars over steam, with gears between
        "########################", E, E, E, E, E, E, E, E,
        "..........g.....g.......",
        "......o.....o.....o.....",
        "......##....##....##....",
        "......##....##....##....",
        "......##....##....##....",
        "###xxx##xxxx##xxxx##xx##",
        "########################",
    },
};

// ---------------------------------------------------------------- the Hull: crabs, eels, urchins, mines
const char* HULL[][CH_H] = {
    {   // A: crab walk
        "########################", E, E, E, E, E, E, E, E, E,
        "..........o.o...........",
        ".........####...........",
        E,
        ".....c.......c..........",
        "########xx######xx######",
        "########################",
    },
    {   // B: eel pits
        "########################", E, E, E, E, E, E, E, E, E,
        ".....o.......o.......o..",
        E, E,
        "......e.......e.........",
        "####....####....####..##",
        "####....####....####..##",
    },
    {   // C: mine climb
        "########################", E, E, E, E,
        "..................o.....",
        ".................###....",
        "............##..........",
        ".........g..............",
        ".......###..............",
        E, E,
        "...###..................",
        ".....................c..",
        "##..................####",
        "##..................####",
    },
    {   // D: barnacle chimney
        "########################",
        E,
        "......#.................",
        "......#...o.............",
        "......#...##............",
        "......#...##............",
        "......#...##............",
        "......#...##............",
        "......#...##............",
        "......#...##............",
        "..........##............",
        "..........##............",
        "..........##.....o......",
        "..........##.......c....",
        "#######xxx##xxx#########",
        "########################",
    },
    {   // E: eel bridge with a crab on the middle span
        "########################", E, E, E, E, E, E, E, E, E,
        ".....o.....c.....o......",
        "....###...###...###.....",
        E,
        "........e.....e.........",
        "##....................##",
        "##....................##",
    },
};
const char* HULL_ARENA[CH_H] = {
    "########################", E, E, E, E, E, E, E, E, E, E,
    "......###...##....###...",
    E,
    "...............K.....E..",
    "####..............######",
    "####..............######",
};

// ---------------------------------------------------------------- the Pirate Ship: pirates, parakeets, fire
const char* PIRATE[][CH_H] = {
    {   // A: deck planks with patrolling pirates
        "########################", E, E, E, E, E, E,
        "..........p.............",
        E, E,
        ".....P.......P..........",
        "....####....####...##...",
        E, E,
        "##xxxxxxxxxxxxxxxxxxxx##",
        "########################",
    },
    {   // B: rigging climb - a narrow chimney with a parakeet at the top
        "########################",
        E,
        "......#.................",
        "......#.....p...........",
        "......#..###............",
        "......#..###............",
        "......#..###............",
        "......#..###............",
        "......#..###............",
        "......#..###............",
        ".........###............",
        ".........###............",
        ".........###......o.....",
        ".........###............",
        "######xxx###xxxx########",
        "########################",
    },
    {   // C: cannon deck - fire vents in a low corridor, and a pirate
        "########################", E, E, E, E, E, E, E, E, E,
        "...####################.",
        E,
        "......o.......o.........",
        "............P...........",
        "####t####t####t####t####",
        "########################",
    },
    {   // D: big leaps between single posts
        "########################", E, E, E, E, E, E, E,
        "...........p............",
        E,
        ".......o......o.........",
        E,
        ".......#......#.........",
        E,
        "##....................##",
        "##....................##",
    },
    {   // E: spiked balls over pirate-guarded decks
        "########################", E, E, E, E, E, E, E, E,
        ".......g........g.......",
        E,
        ".........o.......o......",
        E,
        ".........P.......P......",
        "###xxx#####xxx#####xx###",
        "########################",
    },
};
const char* PIRATE_ARENA[CH_H] = {
    "########################", E, E, E, E, E, E, E, E, E,
    ".....###........###.....",
    E, E,
    "..........B..........E..",
    "########################",
    "########################",
};
#undef E

struct LevelDef {
    const char* name;
    const char* const* chunks; // count * CH_H rows
    int count, perRun;
    const char* const* last;   // the final section (exit or boss arena)
    int coinValue, bonus;
};
const LevelDef LEVELS[PL_COUNT] = {
    {"The Pipes", &PIPES[0][0], (int)(sizeof(PIPES) / sizeof(PIPES[0])), 6, END_PIPES, 2, 30},
    {"The Hull", &HULL[0][0], (int)(sizeof(HULL) / sizeof(HULL[0])), 5, HULL_ARENA, 3, 60},
    {"The Pirate Ship", &PIRATE[0][0], (int)(sizeof(PIRATE) / sizeof(PIRATE[0])), 5, PIRATE_ARENA, 4, 100},
};

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
    return c == '#' || c == 't';
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

// ---------------------------------------------------------------- enemies and bosses
Rectangle EnemyBox(const PlatEnemy& e) {
    switch (e.type) {
        case 'c': return {e.pos.x + 3, e.pos.y + 4, 20, 12};
        case 'P': return {e.pos.x + 3, e.pos.y + 2, 16, 28};
        case 'p': return {e.pos.x - 9, e.pos.y - 6, 18, 12};
        default:  return {e.pos.x - 8, e.pos.y - 18, 16, 36}; // eel
    }
}

void UpdateEnemies(PlatformState& p, float dt) {
    for (auto& e : p.enemies) {
        e.t += dt;
        if (e.type == 'c' || e.type == 'P') { // walk, turning at walls and ledges
            float w = e.type == 'c' ? 26.0f : 22.0f, h = e.type == 'c' ? 16.0f : 30.0f, speed = e.type == 'c' ? 70.0f : 105.0f;
            float nx = e.pos.x + e.dir * speed * dt;
            int ftx = (int)floorf((e.dir > 0 ? nx + w : nx) / T), fty = (int)floorf((e.pos.y + h - 1) / T);
            if (Solid(p, ftx, fty) || !Solid(p, ftx, fty + 1)) e.dir = -e.dir;
            else e.pos.x = nx;
        } else if (e.type == 'p') { // fly back and forth, bobbing
            float nx = e.pos.x + e.dir * 120 * dt;
            if (fabsf(nx - e.home.x) > 5 * T || Solid(p, (int)floorf((nx + e.dir * 10) / T), (int)floorf(e.pos.y / T))) e.dir = -e.dir;
            else e.pos.x = nx;
            e.pos.y = e.home.y + sinf(e.t * 3) * 14;
        } else { // eel: leaps out of the depths, then dives back
            float cyc = fmodf(e.t, 2.6f), bottom = p.h * T + 40.0f, apex = e.home.y - 3.0f * T;
            e.pos.y = cyc < 1.2f ? bottom - (bottom - apex) * sinf(PI * cyc / 1.2f) : bottom + 200;
        }
    }
}

// The Kraken: an ancient horror rising from the abyss beneath the arena. Its tentacles strike up from
// the depths and slam down from above where you stand; then its great head surfaces between the
// platforms. Stomp its head three times.
constexpr float KRAKEN_SURFACED_TOP = 11 * T - 10;
constexpr float TENT_IDLE = -100;
constexpr float BB_W = 36, BB_H = 72; // Blackbeard is a head taller than anyone
float KrakenTop(const PlatformState& p) {
    const PlatBoss& b = p.boss;
    float under = p.h * T + 30.0f;
    switch (b.state) {
        case 1: return under + (KRAKEN_SURFACED_TOP - under) * std::min(1.0f, b.timer / 0.5f);
        case 2: return KRAKEN_SURFACED_TOP + sinf(p.time * 3) * 3;
        case 3: case 4: return KRAKEN_SURFACED_TOP + (under - KRAKEN_SURFACED_TOP) * std::min(1.0f, b.timer / 0.5f);
        default: return under;
    }
}
Rectangle KrakenHead(const PlatformState& p) { float top = KrakenTop(p); return {p.boss.home.x - 48, top, 96, 70}; }
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
        float top = (float)T, bottom = top + (14.0f * T - top) * r;
        return {p.boss.tentX[i] - 15, top, 30, bottom - top};
    }
    float bottom = p.h * T + 10.0f, top = bottom + (7.0f * T - bottom) * r;
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
}

void UpdateBoss(PlatformState& p, float dt) {
    PlatBoss& b = p.boss;
    if (!b.type) return;
    b.timer += dt;
    b.invuln = std::max(0.0f, b.invuln - dt);
    if (b.type == 'K') {
        float arenaX = (p.w - CH_W) * (float)T, pitL = arenaX + 4 * T + 16, pitR = arenaX + 18 * T - 16;
        bool playerInArena = p.pos.x > arenaX - 2 * T;
        // tentT: TENT_IDLE when idle, negative while waiting to start, then seconds since its warning began
        for (int i = 0; i < 2; i++)
            if (b.tentT[i] > TENT_IDLE && (b.tentT[i] += dt) > 1.8f) b.tentT[i] = TENT_IDLE;
        switch (b.state) {
            case 0: // submerged: one tentacle rises from the abyss, another slams down from above
                if (!b.defeated && playerInArena && b.timer > 0.3f && b.timer < 1.0f && b.tentT[0] == TENT_IDLE && b.tentT[1] == TENT_IDLE) {
                    float px = p.pos.x + PW / 2;
                    bool slamFirst = GetRandomValue(0, 1) == 1;
                    b.tentTop[0] = slamFirst;
                    b.tentX[0] = slamFirst ? std::clamp(px, arenaX + 2.0f * T, arenaX + 21.0f * T) : std::clamp(px, pitL, pitR);
                    b.tentTop[1] = !slamFirst;
                    b.tentX[1] = std::clamp(px + (GetRandomValue(0, 1) ? 120.0f : -120.0f), pitL, pitR);
                    if (b.tentTop[1]) b.tentX[1] = std::clamp(px, arenaX + 2.0f * T, arenaX + 21.0f * T);
                    b.tentT[0] = 0;
                    b.tentT[1] = -0.5f; // the second strike follows a moment later, where you've moved to
                }
                if (b.defeated) break;
                if (b.timer > 2.9f && playerInArena) { b.state = 1; b.timer = 0; }
                break;
            case 1: if (b.timer > 0.5f) { b.state = 2; b.timer = 0; } break;
            case 2: if (b.timer > 2.2f) { b.state = 3; b.timer = 0; } break;
            case 3: if (b.timer > 0.5f) { b.state = 0; b.timer = 0; } break;
            case 4: break; // sinking away for good
        }
    } else if (b.type == 'B') {
        if (b.defeated) { b.vel.x = 0; }
        else {
            float px = p.pos.x + PW / 2, bx = b.pos.x + BB_W / 2;
            float speedUp = (3 - b.hp) * 25.0f;
            switch (b.state) {
                case 0: // stalk toward the player
                    b.dir = px < bx ? -1.0f : 1.0f;
                    b.vel.x = b.dir * (70 + speedUp);
                    if (b.timer > 2.0f) { b.state = 1; b.timer = 0; b.vel.x = 0; }
                    if (p.pos.y + PH < b.pos.y - 20 && b.vel.y == 0 && GetRandomValue(0, 90) == 0) b.vel.y = -760;
                    break;
                case 1: b.vel.x = 0; if (b.timer > 0.45f) { b.state = 2; b.timer = 0; } break; // wind up
                case 2: b.vel.x = b.dir * (330 + speedUp); if (b.timer > 0.8f) { b.state = 3; b.timer = 0; } break; // charge!
                default: b.vel.x = 0; if (b.timer > 0.6f) { b.state = 0; b.timer = 0; } break; // catch breath
            }
        }
        b.vel.y = std::min(b.vel.y + GRAV_DOWN * dt, MAX_FALL);
        bool grounded, hitWall;
        MoveAndCollide(p, b.pos, b.vel, BB_W, BB_H, dt, grounded, hitWall);
        if (hitWall && b.state == 2) { b.state = 3; b.timer = 0; }
    }
}

// ---------------------------------------------------------------- building a level
void BuildFromParts(PlatformState& p, const std::vector<const char* const*>& parts) {
    p.tiles.assign(CH_H, "");
    for (auto part : parts)
        for (int r = 0; r < CH_H; r++) {
            std::string row = part[r];
            if ((int)row.size() != CH_W) {
                TraceLog(LOG_WARNING, "PLATFORM: section row '%s' is %d wide, expected %d", part[r], (int)row.size(), CH_W);
                row.resize(CH_W, '.');
            }
            p.tiles[r] += row;
        }
    p.w = (int)p.tiles[0].size();
    p.h = CH_H;
    p.enemies.clear();
    p.boss = PlatBoss{};
    for (int r = 0; r < p.h; r++)
        for (int c = 0; c < p.w; c++) {
            char& ch = p.tiles[r][c];
            float x = c * (float)T, y = r * (float)T;
            switch (ch) {
                case 'S': p.startPos = {x + 6, y + T - PH}; break;
                case 'c': p.enemies.push_back({'c', {x + 3, y + T - 16}, {x, y}, -1, 0}); break;
                case 'P': p.enemies.push_back({'P', {x + 5, y + T - 30}, {x, y}, -1, 0}); break;
                case 'p': p.enemies.push_back({'p', {x + 16, y + 16}, {x + 16, y + 16}, 1, 0}); break;
                case 'e': p.enemies.push_back({'e', {x + 16, p.h * (float)T + 200}, {x + 16, y}, 1, c * 0.37f}); break;
                case 'K': p.boss.type = 'K'; p.boss.home = {x + 16, y + T}; p.boss.tentT[0] = p.boss.tentT[1] = TENT_IDLE; break;
                case 'B': p.boss.type = 'B'; p.boss.home = p.boss.pos = {x, y + T - BB_H}; break;
                default: continue;
            }
            ch = '.';
        }
    p.exitOpen = p.boss.type != 'B'; // Blackbeard guards the treasure
    p.pos = p.startPos;
    p.camX = p.pos.x;
}

void BuildLevel(PlatformState& p, const std::vector<int>& layout) {
    const LevelDef& L = LEVELS[p.level];
    std::vector<const char* const*> parts;
    parts.push_back(START);
    for (int c : layout) parts.push_back(L.chunks + c * CH_H);
    parts.push_back(L.last);
    BuildFromParts(p, parts);
}

Vector2 SpawnPoint(const PlatformState& p) {
    if (p.checkpointChunk == 0) return p.startPos;
    return {p.checkpointChunk * CH_W * (float)T + 6, 14.0f * T - PH};
}

void Die(PlatformState& p) {
    if (p.deathTimer > 0 || p.finished) return;
    p.deathTimer = 0.45f;
    p.deaths++;
    Vector2 c{p.pos.x + PW / 2, p.pos.y + PH / 2};
    Burst(p, c, 18, Pal::Teal, 260, 0.6f, 3);
    Burst(p, c, 10, Pal::Brass, 200, 0.5f, 3);
}

void Respawn(PlatformState& p) {
    p.pos = SpawnPoint(p);
    p.vel = {0, 0};
    p.scale = {1, 1};
    p.wallLock = 0;
    p.jumpBuffer = 0;
    ResetBoss(p);
}

// ---------------------------------------------------------------- the player
void StepPlayer(PlatformState& p, float dir, bool jumpHeld) {
    if (p.wallLock > 0) {
        p.wallLock -= STEP;
        if (dir == (float)p.lockSide) dir = 0; // right after a wall jump, pushing back into the wall is ignored
    }
    float target = dir * RUN, accel;
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
            p.vel.y = -JUMP_V;
            p.coyote = p.jumpBuffer = 0;
            p.onGround = false;
            p.scale = {0.72f, 1.32f};
            Dust(p, {p.pos.x + PW / 2, p.pos.y + PH}, 5, -p.vel.x / RUN);
        } else if (p.wallCoyote > 0) {
            p.vel.x = -p.lockSide * WALLJUMP_VX;
            p.vel.y = -WALLJUMP_VY;
            p.wallLock = WALL_LOCK;
            p.wallCoyote = p.jumpBuffer = 0;
            p.facingRight = p.lockSide < 0;
            p.scale = {0.75f, 1.28f};
            Dust(p, {p.lockSide > 0 ? p.pos.x + PW : p.pos.x, p.pos.y + PH / 2}, 5, (float)-p.lockSide);
        }
    }

    // gravity is stronger once the jump button is released (short hops) and when falling (snappy arcs)
    p.vel.y += (p.vel.y < 0 ? (jumpHeld ? GRAV_UP : GRAV_UP_RELEASED) : GRAV_DOWN) * STEP;
    if (p.wallSide && p.vel.y > WALL_SLIDE) p.vel.y = std::max(WALL_SLIDE, p.vel.y - 6000 * STEP);
    p.vel.y = std::min(p.vel.y, MAX_FALL);

    bool was = p.onGround, wall;
    float fallSpeed = p.vel.y;
    MoveAndCollide(p, p.pos, p.vel, PW, PH, STEP, p.onGround, wall);
    if (p.onGround && !was) {
        p.scale = {1.0f + std::min(0.35f, fallSpeed / 2400), 1.0f - std::min(0.3f, fallSpeed / 2800)};
        if (fallSpeed > 400) Dust(p, {p.pos.x + PW / 2, p.pos.y + PH}, 6, 0);
    }
    p.scale.x += (1 - p.scale.x) * std::min(1.0f, STEP * 14);
    p.scale.y += (1 - p.scale.y) * std::min(1.0f, STEP * 14);
    if (p.onGround) p.runAnim += fabsf(p.vel.x) * STEP * 0.055f;
    if (p.wallSide && p.vel.y > 0 && GetRandomValue(0, 30) == 0)
        p.particles.push_back({{p.wallSide > 0 ? p.pos.x + PW : p.pos.x, p.pos.y + PH - 4}, {-p.wallSide * 20.0f, -20}, 0.3f, 0.3f, 2, Color{220, 214, 200, 255}});

    // coins, the exit, hazards and falling out of the level
    Rectangle pr = PlayerBox(p);
    for (int ty = (int)floorf(pr.y / T); ty <= (int)floorf((pr.y + pr.height) / T); ty++)
        for (int tx = (int)floorf(pr.x / T); tx <= (int)floorf((pr.x + pr.width) / T); tx++) {
            char c = At(p, tx, ty);
            if (c == 'o') {
                p.tiles[ty][tx] = '.';
                p.coins++;
                Burst(p, {tx * (float)T + 16, ty * (float)T + 16}, 8, Color{255, 220, 90, 255}, 120, 0.35f, 2);
            } else if (c == 'E' && p.exitOpen) {
                p.finished = true;
            }
        }
    if (TouchesHazard(p) || p.pos.y > p.h * T + 40) Die(p);
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

void DrawBackground(const PlatformState& p, float t) {
    float cw = PIXEL_W + 2.0f, ch = PIXEL_H + 2.0f, cx = p.camX * ZOOM;
    switch (p.level) {
        case PL_PIPES: {
            DrawRectangleGradientV(0, 0, (int)cw, (int)ch, Color{62, 50, 42, 255}, Color{22, 26, 32, 255});
            Layer(cx, 0.08f, 60, cw, [&](float x, float wx) { // a far lattice of pipes
                DrawRectangle((int)x, 30, 3, (int)ch, Color{54, 44, 38, 255});
                DrawRectangle(0, (int)(60 + Hs(wx) * 240), (int)cw, 2, Color{54, 44, 38, 255});
            });
            DrawRectangle(0, 0, (int)cw, (int)ch, Color{40, 34, 30, 60});
            Layer(cx, 0.2f, 170, cw, [&](float x, float wx) { // big boilers
                float r = 40 + Hs(wx) * 30, y = 170 + Hs(wx + 1) * 110;
                DrawCircleV({x, y}, r, Color{66, 52, 42, 255});
                DrawCircleV({x - r * 0.3f, y - r * 0.3f}, r * 0.35f, Color{78, 62, 50, 255});
                for (int k = 0; k < 10; k++) DrawCircleV({x + cosf(k * 0.63f) * r * 0.85f, y + sinf(k * 0.63f) * r * 0.85f}, 1.2f, Color{96, 78, 60, 255});
            });
            Layer(cx, 0.35f, 162, cw, [&](float x, float wx) { // columns and cross-pipes with valves and gauges
                DrawRectangle((int)x, 30, 22, (int)ch, Color{88, 68, 52, 255});
                DrawRectangle((int)x, 30, 3, (int)ch, Color{112, 88, 66, 255});
                float y = 100 + Hs(wx) * 190;
                DrawRectangle((int)x - 60, (int)y, 140, 9, Color{102, 78, 58, 255});
                DrawCircleV({x + 11, y + 30}, 9, Color{150, 120, 70, 255});
                DrawCircleV({x + 11, y + 30}, 7, Color{220, 210, 180, 255});
                DrawLineEx({x + 11, y + 30}, {x + 11 + cosf(t * 2 + wx) * 5, y + 30 + sinf(t * 2 + wx) * 5}, 1, Color{160, 40, 30, 255});
            });
            Layer(cx, 0.6f, 230, cw, [&](float x, float wx) { // chains and steam close by
                for (int k = 0; k < 14; k++) DrawRectangle((int)(x + sinf(t + wx) * k * 0.3f), 30 + k * 9, 3, 6, Color{40, 32, 28, 255});
                for (int k = 0; k < 3; k++) {
                    float ph = fmodf(t * 0.4f + k * 0.33f + Hs(wx), 1.0f);
                    DrawCircleV({x + 40 + sinf(ph * 5) * 6, 330 - ph * 200}, 6 + ph * 12, Fade(Color{220, 220, 214, 255}, 0.18f * (1 - ph)));
                }
            });
        } break;
        case PL_HULL: {
            DrawRectangleGradientV(0, 0, (int)cw, (int)ch, Color{18, 64, 94, 255}, Color{4, 14, 30, 255});
            for (int x = 0; x < (int)cw; x += 3) // the shimmering surface, far above
                DrawRectangle(x, 29, 3, (int)(4 + sinf(x * 0.08f + t * 1.5f) * 2 + 2), Color{120, 190, 210, 90});
            BeginBlendMode(BLEND_ADDITIVE);
            Layer(cx, 0.06f, 140, cw, [&](float x, float wx) {
                float w = 18 + Hs(wx) * 16;
                DrawTri({x, 30}, {x + w, 30}, {x - 70, ch}, Color{60, 130, 160, 30});
                DrawTri({x + w, 30}, {x - 70 + w * 1.6f, ch}, {x - 70, ch}, Color{60, 130, 160, 30});
            });
            EndBlendMode();
            { // the Nautilus itself, looming overhead with its portholes lit
                float hx = 700 - cx * 0.12f;
                DrawRectangleRounded({hx - 520, 40, 1040, 70}, 1.0f, 24, Color{14, 34, 46, 255});
                DrawRectangleRounded({hx - 80, 22, 160, 30}, 0.6f, 8, Color{14, 34, 46, 255});
                for (int k = 0; k < 16; k++) DrawCircleV({hx - 450 + k * 60.0f, 78}, 3, Color{255, 210, 130, 255});
            }
            Layer(cx, 0.22f, 110, cw, [&](float x, float wx) { // far rock spires
                float h = 70 + Hs(wx) * 110;
                DrawTri({x - 26, ch}, {x + 26, ch}, {x + Hs(wx + 1) * 10, ch - h}, Color{14, 40, 54, 255});
            });
            DrawRectangle(0, 0, (int)cw, (int)ch, Color{20, 60, 80, 40});
            Layer(cx, 0.42f, 70, cw, [&](float x, float wx) { // a kelp forest
                Vector2 prev{x, ch};
                int n = 10 + (int)(Hs(wx) * 8);
                for (int s = 1; s <= n; s++) {
                    Vector2 q{x + sinf(t * 0.9f + wx + s * 0.45f) * s * 1.5f, ch - s * 14.0f};
                    DrawLineEx(prev, q, 3.5f - s * 0.12f, Color{22, 76, 60, 255});
                    prev = q;
                }
            });
            Layer(cx, 0.7f, 90, cw, [&](float x, float wx) { // coral and bubbles close by
                DrawCircleV({x, ch + 6}, 14 + Hs(wx) * 10, Color{120, 60, 80, 255});
                DrawCircleV({x + 12, ch}, 8, Color{180, 90, 90, 255});
                float by = ch - fmodf(t * (20 + Hs(wx) * 20) + wx, ch);
                DrawCircleLines((int)(x + sinf(t * 2 + wx) * 3), (int)by, 2, Color{180, 230, 250, 150});
            });
        } break;
        default: {
            DrawRectangleGradientV(0, 0, (int)cw, (int)ch, Color{12, 14, 38, 255}, Color{52, 34, 62, 255});
            Layer(cx, 0.02f, 23, cw, [&](float x, float wx) {
                float sy = 34 + Hs(wx) * 200;
                if (Hs(wx + 2) > 0.45f && sinf(t * 2 + wx) > -0.6f) DrawPixel((int)x, (int)sy, Color{230, 230, 255, 255});
            });
            Vector2 moon{cw - 110 - cx * 0.03f, 80};
            DrawCircleV(moon, 26, Color{250, 244, 220, 60});
            DrawCircleV(moon, 20, Color{246, 240, 214, 255});
            DrawCircleV({moon.x - 6, moon.y - 4}, 4, Color{220, 214, 190, 255});
            Layer(cx + t * 6, 0.08f, 260, cw, [&](float x, float wx) { // drifting clouds
                float y = 60 + Hs(wx) * 70;
                for (int k = 0; k < 4; k++) DrawEllipse((int)(x + k * 22), (int)(y + (k % 2) * 4), 26, 8, Color{44, 40, 70, 200});
            });
            Layer(cx, 0.2f, 260, cw, [&](float x, float wx) { // a ghostly fleet on the horizon
                float y = 240 + Hs(wx) * 16;
                DrawRectangle((int)x, (int)y, 90, 16, Color{28, 24, 44, 255});
                DrawRectangle((int)x + 40, (int)y - 80, 3, 80, Color{28, 24, 44, 255});
                DrawTri({x + 44, y - 72}, {x + 44, y - 14}, {x + 80, y - 14}, Color{36, 32, 56, 255});
            });
            for (int x = 0; x < (int)cw; x += 4) { // the far sea
                float y = 262 + sinf((x + cx * 0.3f) * 0.05f + t * 1.2f) * 2;
                DrawRectangle(x, (int)y, 4, (int)ch - (int)y, Color{22, 28, 56, 255});
                if (((x / 4) % 9) == 0) DrawRectangle(x, (int)y, 3, 1, Color{120, 130, 180, 255});
            }
            Layer(cx, 0.5f, 300, cw, [&](float x, float wx) { // our own masts and rigging, nearer
                DrawRectangle((int)x, 30, 6, (int)ch, Color{24, 18, 22, 255});
                DrawRectangle((int)x - 50, (int)(90 + Hs(wx) * 30), 106, 4, Color{24, 18, 22, 255});
                DrawTri({x + 8, 100}, {x + 8, 220}, {x + 70, 200}, Color{58, 50, 64, 255});
                for (int k = 0; k < 5; k++) DrawLineEx({x + 3, 40.0f + k * 4}, {x + 150, 330}, 1, Color{30, 24, 26, 255});
                Vector2 lamp{x + 20, 150};
                DrawCircleV(lamp, 7, Color{255, 190, 90, 70});
                DrawCircleV(lamp, 3, Color{255, 210, 120, 255});
            });
            for (int x = 0; x < (int)cw; x += 4) { // the near sea, rolling
                float y = 300 + sinf((x + cx * 0.6f) * 0.04f + t * 1.6f) * 4;
                DrawRectangle(x, (int)y, 4, (int)ch - (int)y, Color{18, 24, 50, 255});
                if (((x / 4) % 6) == 0) DrawRectangle(x, (int)y, 3, 1, Color{150, 160, 200, 255});
            }
        } break;
    }
}

// ---------------------------------------------------------------- drawing: tiles
void DrawSolid(const PlatformState& p, int x, int y) {
    float px = x * (float)T, py = y * (float)T;
    bool topEdge = !Solid(p, x, y - 1);
    switch (p.level) {
        case PL_PIPES:
            DrawRectangle((int)px, (int)py, T, T, Color{150, 106, 52, 255});
            DrawRectangleLines((int)px, (int)py, T, T, Color{104, 72, 36, 255});
            DrawCircle((int)px + 6, (int)py + 6, 2, Color{226, 186, 116, 255});
            DrawCircle((int)px + T - 6, (int)py + 6, 2, Color{226, 186, 116, 255});
            if (topEdge) DrawRectangle((int)px, (int)py, T, 4, Color{208, 160, 92, 255});
            break;
        case PL_HULL:
            DrawRectangle((int)px, (int)py, T, T, Color{62, 88, 104, 255});
            DrawRectangleLines((int)px, (int)py, T, T, Color{36, 52, 64, 255});
            DrawCircle((int)px + 5, (int)py + T - 6, 2, Color{120, 150, 164, 255});
            if (topEdge) {
                DrawRectangle((int)px, (int)py, T, 4, Color{110, 140, 150, 255});
                for (int k = 0; k < 3; k++) DrawCircle((int)px + 6 + k * 10, (int)py + 3, 2 + (x + k) % 2, Color{200, 196, 180, 255}); // barnacles
                if ((x * 7) % 3 == 0) {
                    float sway = sinf(p.time * 2 + x) * 3;
                    DrawLineEx({px + 20, py}, {px + 18 + sway, py - 12}, 2, Color{60, 140, 80, 255});
                }
            }
            break;
        default:
            DrawRectangle((int)px, (int)py, T, T, Color{112, 72, 42, 255});
            DrawRectangle((int)px, (int)py + 10, T, 1, Color{80, 50, 28, 255});
            DrawRectangle((int)px, (int)py + 21, T, 1, Color{80, 50, 28, 255});
            DrawRectangle((int)px + (y % 2 ? 10 : 22), (int)py, 1, T, Color{80, 50, 28, 255});
            DrawCircle((int)px + 4, (int)py + 5, 1.5f, Color{60, 56, 60, 255});
            if (topEdge) DrawRectangle((int)px, (int)py, T, 4, Color{168, 116, 70, 255});
            break;
    }
}

void DrawTile(const PlatformState& p, char c, int x, int y, float t) {
    float px = x * (float)T, py = y * (float)T;
    switch (c) {
        case '#': DrawSolid(p, x, y); break;
        case 'x':
            if (p.level == PL_PIPES) {
                DrawRectangle((int)px, (int)py + T - 12, T, 12, Color{64, 64, 70, 255});
                for (int k = 0; k < 4; k++) DrawRectangle((int)px + 3 + k * 8, (int)py + T - 10, 4, 8, Color{30, 30, 34, 255});
                for (int k = 0; k < 3; k++) {
                    float ph = fmodf(t * 1.6f + k * 0.33f + x * 0.17f, 1.0f);
                    DrawCircle((int)(px + T / 2 + sinf(ph * 6 + k) * 6), (int)(py + T - 8 - ph * 30), 5 + ph * 7, Fade(Color{240, 245, 250, 255}, 0.7f * (1 - ph)));
                }
            } else if (p.level == PL_HULL) { // sea urchins
                Vector2 c0{px + 16, py + T - 10};
                for (int k = 0; k < 10; k++) {
                    float a = PI + k * PI / 9;
                    DrawLineEx(c0, {c0.x + cosf(a) * 15, c0.y + sinf(a) * 15}, 2, Color{70, 40, 90, 255});
                }
                DrawCircleV(c0, 10, Color{90, 50, 110, 255});
                DrawCircleV({c0.x - 3, c0.y - 3}, 3, Color{150, 100, 170, 255});
            } else { // iron spikes
                for (int k = 0; k < 4; k++) DrawTri({px + k * 8.0f, py + T}, {px + k * 8.0f + 8, py + T}, {px + k * 8.0f + 4, py + 12}, Color{150, 150, 160, 255});
                DrawRectangle((int)px, (int)py + T - 3, T, 3, Color{70, 70, 76, 255});
            }
            break;
        case 'g': {
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
// The diver is drawn a little narrower than its collision box, squash-and-stretch never widens it into
// a wall it's touching, and its position is snapped to the canvas's pixel grid (as the tiles are), so
// it can never appear to sink into a wall.
void DrawDiver(const PlatformState& p) {
    Color suit{64, 196, 190, 255}, suitDk{40, 140, 136, 255}, boot{60, 56, 60, 255};
    int wall = TouchWall(p, 1) ? 1 : TouchWall(p, -1) ? -1 : 0;
    float sx = p.scale.x, feetX = p.pos.x + PW / 2 - wall * 1.5f;
    if (wall) sx = std::min(sx, 0.92f);
    feetX = roundf(feetX * ZOOM) / ZOOM;
    float feetY = roundf((p.pos.y + PH) * ZOOM) / ZOOM;
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling(); // the mirrored transform flips triangle winding
    rlPushMatrix();
    rlTranslatef(feetX, feetY, 0);
    rlScalef((p.facingRight ? 1.0f : -1.0f) * sx, p.scale.y, 1);
    bool running = p.onGround && fabsf(p.vel.x) > 30, sliding = p.wallSide != 0;
    float ph = p.runAnim, l1 = running ? sinf(ph) * 4.5f : 0;
    if (!p.onGround) { // legs tucked in the air
        DrawRectangleRec({-7.5f, -11, 6, 7}, suitDk);
        DrawRectangleRec({2, -12, 6, 7}, suit);
        DrawRectangleRec({-8.5f, -6, 7, 3}, boot);
        DrawRectangleRec({1.5f, -7, 7, 3}, boot);
    } else {
        DrawRectangleRec({-7 + l1 * 0.6f, -9, 6, 9 - fmaxf(0, l1 * 0.4f)}, suitDk);
        DrawRectangleRec({1.5f - l1 * 0.6f, -9, 6, 9 - fmaxf(0, -l1 * 0.4f)}, suit);
        DrawRectangleRec({-8.5f + l1 * 0.7f, -3, 8, 3}, boot);
        DrawRectangleRec({0.5f - l1 * 0.7f, -3, 8, 3}, boot);
    }
    DrawRectangleRounded({-7.5f, -21, 15, 13}, 0.4f, 4, suit);
    DrawRectangleRec({-7.5f, -11, 15, 2}, Color{80, 60, 40, 255});
    if (sliding) DrawRectangleRec({5, -27, 3.5f, 9}, suitDk); // hand pressed to the wall
    else DrawRectangleRec({3.5f - l1 * 0.3f, -19, 4, 8}, suitDk);
    DrawCircleV({0.5f, -26}, 8.5f, Pal::Brass);
    DrawCircleV({-0.5f, -28}, 3, Color{250, 220, 150, 255});
    DrawCircleV({3.5f, -26}, 4.6f, Color{30, 70, 90, 255});
    DrawCircleV({2.5f, -28}, 1.8f, Color{190, 235, 245, 255});
    rlPopMatrix();
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
}

void DrawEnemy(const PlatEnemy& e, float t) {
    float x = e.pos.x, y = e.pos.y, f = e.dir;
    switch (e.type) {
        case 'c': { // crab
            Color c{220, 90, 50, 255}, dk{160, 60, 34, 255};
            float cx = x + 13, cy = y + 10;
            for (int k = 0; k < 3; k++) {
                float lx = cx - 8 + k * 8, sw = sinf(t * 14 + k) * 2;
                DrawLineEx({lx, cy}, {lx - 4 + sw, y + 16}, 2, dk);
            }
            DrawEllipse((int)cx, (int)cy, 12, 7, c);
            DrawEllipse((int)cx - 2, (int)cy - 3, 6, 2, Color{250, 150, 110, 255});
            for (int s = -1; s <= 1; s += 2) {
                DrawCircle((int)(cx + s * 14), (int)(cy - 5 + sinf(t * 6 + s) * 2), 5, c);
                DrawLineEx({cx + s * 4, cy - 6}, {cx + s * 4, cy - 12}, 1.5f, dk);
                DrawCircle((int)(cx + s * 4), (int)cy - 13, 2, Pal::Ink);
            }
        } break;
        case 'P': { // pirate
            float cx = x + 11, step = sinf(t * 12) * 3;
            DrawRectangleRec({cx - 6 + step, y + 22, 5, 8}, Color{50, 40, 36, 255});
            DrawRectangleRec({cx + 1 - step, y + 22, 5, 8}, Color{50, 40, 36, 255});
            DrawRectangleRec({cx - 8, y + 10, 16, 13}, Color{230, 226, 216, 255});
            for (int k = 0; k < 3; k++) DrawRectangleRec({cx - 8, y + 12 + k * 4.0f, 16, 2}, Color{40, 60, 120, 255});
            DrawCircle((int)cx, (int)y + 6, 6, Color{220, 170, 130, 255});
            DrawRectangleRec({cx - 7, y - 1, 14, 5}, Color{190, 40, 36, 255});
            DrawCircle((int)(cx + f * 3), (int)y + 6, 2, Pal::Ink);
            DrawLineEx({cx + f * 8, y + 16}, {cx + f * 20, y + 4}, 2, Color{200, 204, 210, 255});
        } break;
        case 'p': { // parakeet
            float flap = sinf(t * 18) * 5;
            DrawEllipse((int)x, (int)y, 9, 6, Color{60, 190, 80, 255});
            DrawTri({x - 2, y - 2}, {x + 6, y - 2}, {x + 1, y - 8 - flap}, Color{230, 60, 50, 255});
            DrawCircle((int)(x + f * 8), (int)y - 3, 4, Color{80, 210, 100, 255});
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
    Color hi = ColorBrightness(c, 0.25f), sucker{236, 176, 214, 255};
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
void DrawBossBack(const PlatformState& p, float t) {
    const PlatBoss& b = p.boss;
    if (b.type != 'K') return;
    float arenaX = (p.w - CH_W) * (float)T;
    float sink = b.defeated && b.state == 4 ? std::min(1.0f, b.timer / 3.0f) * 300 : 0;
    Vector2 body{arenaX + 13 * T, 17.5f * T + sink};
    Color deep{52, 22, 64, 255}, deeper{36, 16, 46, 255};
    for (int k = 0; k < 7; k++) { // great arms curling up behind the platforms
        float bx = body.x - 300 + k * 100, sw = sinf(t * 0.6f + k) * 60;
        DrawTentacle({bx, body.y}, {bx + sw, 3.0f * T + (k % 3) * 40 + sink}, 22, t * 0.4f, k * 1.7f, deeper);
    }
    DrawEllipse((int)body.x, (int)body.y, 300, 170, deep); // the mantle, rising from the dark
    DrawEllipse((int)(body.x - 80), (int)(body.y - 90), 90, 40, Color{70, 32, 84, 255});
    for (int s = -1; s <= 1; s += 2) { // vast eyes that follow you
        Vector2 e{body.x + s * 110, body.y - 60};
        float look = std::clamp((p.pos.x - e.x) * 0.02f, -6.0f, 6.0f);
        DrawEllipse((int)e.x, (int)e.y, 34, 24, Color{30, 10, 20, 255});
        DrawEllipse((int)e.x, (int)e.y, 28, 18, Color{230, 190, 60, 255});
        DrawRectangle((int)(e.x + look - 3), (int)e.y - 16, 6, 32, Color{20, 6, 10, 255});
    }
}

void DrawBoss(const PlatformState& p, float t) {
    const PlatBoss& b = p.boss;
    if (b.type == 'K') {
        Color arm{130, 60, 140, 255};
        for (int i = 0; i < 2; i++) {
            if (b.tentT[i] < 0) continue;
            if (b.tentT[i] < 0.75f) { // warning: churning water below, or a shadow falling from above
                if (b.tentTop[i]) {
                    float a = 0.3f + 0.5f * b.tentT[i] / 0.75f;
                    DrawRectangle((int)(b.tentX[i] - 16), T, 32, 13 * T, Fade(Color{20, 0, 30, 255}, a * 0.35f));
                    for (int k = 0; k < 3; k++) DrawCircle((int)(b.tentX[i] + Rnd(-14, 14)), (int)(T + Rnd(0, 40)), 3, Fade(Color{200, 120, 220, 255}, a));
                } else {
                    float surface = p.h * T - 6.0f;
                    for (int k = 0; k < 5; k++)
                        DrawCircle((int)(b.tentX[i] + Rnd(-14, 14)), (int)(surface - Rnd(0, 30) * b.tentT[i]), 3, Fade(Color{200, 120, 220, 255}, 0.8f));
                }
                continue;
            }
            Rectangle r = TentacleBox(p, i);
            Vector2 base = b.tentTop[i] ? Vector2{b.tentX[i], 0} : Vector2{b.tentX[i], (float)p.h * T + 30};
            Vector2 tip = b.tentTop[i] ? Vector2{b.tentX[i], r.y + r.height} : Vector2{b.tentX[i], r.y};
            DrawTentacle(base, tip, 16, t * 2, i * 3.0f, arm);
        }
        if (b.state >= 1) {
            Rectangle h = KrakenHead(p);
            bool blink = b.invuln > 0 && fmodf(t, 0.15f) < 0.075f;
            Color skin = blink ? WHITE : Color{130, 64, 150, 255}, dk{96, 44, 112, 255};
            float cx = h.x + h.width / 2;
            for (int k = -2; k <= 2; k++) // short arms writhing around the head
                DrawTentacle({cx + k * 18, h.y + 60}, {cx + k * 46, h.y + 110 + fabsf((float)k) * 8}, 9, t * 3, k * 2.0f, dk);
            DrawEllipse((int)cx, (int)(h.y + 28), 54, 40, skin);             // the great mantle
            DrawEllipse((int)(cx - 14), (int)(h.y + 12), 22, 12, Fade(WHITE, 0.18f));
            for (int k = 0; k < 6; k++) DrawCircle((int)(cx - 36 + k * 14), (int)(h.y + 20 + (k % 2) * 10), 3, dk); // mottling
            for (int s = -1; s <= 1; s += 2) {
                DrawEllipse((int)(cx + s * 24), (int)(h.y + 42), 12, 9, Color{250, 220, 80, 255});
                DrawRectangle((int)(cx + s * 24 - 2), (int)(h.y + 35), 4, 14, Pal::Ink);
            }
            DrawTri({cx - 8, h.y + 58}, {cx + 8, h.y + 58}, {cx, h.y + 70}, Color{40, 30, 30, 255}); // the beak
            DrawTri({cx - 12, h.y - 6}, {cx, h.y + 6}, {cx + 12, h.y - 6}, Color{250, 220, 80, 200});    // stomp here
        }
    } else if (b.type == 'B' && !b.defeated) {
        float x = b.pos.x, y = b.pos.y, f = b.dir, cx = x + BB_W / 2;
        bool blink = b.invuln > 0 && fmodf(t, 0.15f) < 0.075f;
        if (blink) return;
        float step = b.vel.x != 0 ? sinf(t * 14) * 5 : 0;
        Color coat{140, 26, 30, 255}, coatDk{96, 16, 20, 255}, boot{24, 20, 18, 255};
        DrawRectangleRec({cx - 12 + step, y + 50, 10, 22}, boot);                          // tall boots
        DrawRectangleRec({cx + 2 - step, y + 50, 10, 22}, boot);
        DrawRectangleRec({cx - 14 + step, y + 48, 13, 5}, Color{60, 40, 24, 255});
        DrawRectangleRec({cx + 1 - step, y + 48, 13, 5}, Color{60, 40, 24, 255});
        DrawTri({cx - 18, y + 22}, {cx + 18, y + 22}, {cx + 20, y + 56}, coat);             // a long coat
        DrawTri({cx - 18, y + 22}, {cx + 20, y + 56}, {cx - 22, y + 56}, coatDk);
        DrawRectangleRounded({cx - 17, y + 18, 34, 28}, 0.3f, 4, coat);
        DrawRectangleRec({cx - 17, y + 38, 34, 5}, Color{60, 40, 24, 255});                 // belt
        DrawRectangleRec({cx - 3, y + 37, 6, 7}, Pal::Brass);
        DrawRectangleRec({cx - 14, y + 30, 5, 10}, Color{70, 70, 76, 255});                 // a brace of pistols
        DrawRectangleRec({cx + 9, y + 30, 5, 10}, Color{70, 70, 76, 255});
        DrawRectangleRec({cx - 17, y + 18, 34, 3}, Pal::Brass);
        DrawCircle((int)cx, (int)y + 12, 9, Color{220, 170, 130, 255});                   // face
        DrawCircle((int)cx, (int)y + 20, 11, Color{24, 22, 22, 255});                     // the famous beard...
        DrawRectangleRec({cx - 10, y + 20, 20, 12}, Color{24, 22, 22, 255});
        for (int k = 0; k < 3; k++) {                                                    // ...with smoking fuses in it
            Vector2 fz{cx - 8 + k * 8.0f, y + 28};
            DrawCircleV(fz, 1.5f, Color{255, 140, 40, 255});
            float ph = fmodf(t * 0.8f + k * 0.3f, 1.0f);
            DrawCircleV({fz.x + sinf(ph * 6 + k) * 3, fz.y - 6 - ph * 22}, 2 + ph * 3, Fade(Color{150, 150, 150, 255}, 0.6f * (1 - ph)));
        }
        DrawCircle((int)(cx + f * 4), (int)y + 10, 2, b.state == 1 ? Color{255, 60, 40, 255} : Pal::Ink);
        DrawTri({cx - 22, y + 4}, {cx + 22, y + 4}, {cx, y - 14}, Color{24, 22, 30, 255}); // tricorn
        DrawRectangleRec({cx - 22, y + 2, 44, 4}, Color{24, 22, 30, 255});
        DrawCircle((int)cx, (int)y - 3, 3, Color{230, 230, 220, 255});                  // skull badge
        float swordA = b.state == 1 ? -1.3f : b.state == 2 ? 0.1f : -0.5f;
        Vector2 hand{cx + f * 18, y + 30};
        DrawLineEx(hand, {hand.x + f * cosf(swordA) * 34, hand.y + sinf(swordA) * 34}, 3, Color{210, 214, 220, 255});
        DrawCircleV(hand, 3, Pal::Brass);
    }
}
}  // namespace

// ============================================================ public
const char* PlatLevelName(int level) { return LEVELS[level].name; }

void GeneratePlatLayout(Game& g, int level) {
    const LevelDef& L = LEVELS[level];
    std::vector<int> idx;
    for (int i = 0; i < L.count; i++) idx.push_back(i);
    for (int i = (int)idx.size() - 1; i > 0; i--) std::swap(idx[i], idx[GetRandomValue(0, i)]);
    idx.resize(std::min(L.perRun, L.count));
    g.platLayouts[level] = idx;
}

std::string PlatLayoutCode(const Game& g, int level) {
    std::string s;
    for (int c : g.platLayouts[level]) { if (!s.empty()) s += "-"; s += (char)('A' + c); }
    return s.empty() ? "(new)" : s;
}

void StartPlatform(Game& g, int level) {
    bool bad = g.platLayouts[level].empty();
    for (int c : g.platLayouts[level]) bad |= c < 0 || c >= LEVELS[level].count; // e.g. an old save
    if (bad) GeneratePlatLayout(g, level);
    g.plat = PlatformState{};
    g.plat.level = level;
    g.plat.layoutCode = PlatLayoutCode(g, level);
    BuildLevel(g.plat, g.platLayouts[level]);
    g.scene = Scene::Platformer;
}

void ScenePlatformer(Game& g) {
    auto& p = g.plat;
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
        bool jumpPressed = IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP);
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
            int chunk = (int)((p.pos.x + PW / 2) / (CH_W * T));
            if (p.onGround && chunk > p.checkpointChunk) {
                p.checkpointChunk = chunk;
                Burst(p, {p.pos.x + PW / 2, p.pos.y}, 12, Pal::Good, 140, 0.5f, 2);
            }
        }
        UpdateEnemies(p, dt);
        Vector2 prevBossPos = p.boss.pos;
        UpdateBoss(p, dt);
        (void)prevBossPos;

        // touching an enemy is deadly; only a boss can be stomped
        if (p.deathTimer <= 0 && !p.finished) {
            Rectangle pr = PlayerBox(p);
            for (auto& e : p.enemies) if (CheckCollisionRecs(pr, EnemyBox(e))) Die(p);
            PlatBoss& b = p.boss;
            bool falling = p.vel.y > 0;
            if (b.type == 'K' && !b.defeated) {
                for (int i = 0; i < 2; i++) if (b.tentT[i] >= 0.75f && CheckCollisionRecs(pr, TentacleBox(p, i))) Die(p);
                if ((b.state == 1 || b.state == 2) && CheckCollisionRecs(pr, KrakenHead(p))) {
                    Rectangle h = KrakenHead(p);
                    if (falling && p.pos.y + PH - p.vel.y * dt <= h.y + 14 && b.invuln <= 0) {
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
                            p.relic = GetRandomValue(0, (int)Relics().size() - 1);
                            Toast(g, "The Kraken sinks into the abyss! It left something behind...");
                        }
                    } else if (b.invuln <= 0) {
                        Die(p);
                    }
                }
            } else if (b.type == 'B' && !b.defeated && CheckCollisionRecs(pr, {b.pos.x + 5, b.pos.y, BB_W - 10, BB_H})) {
                if (falling && p.pos.y + PH - p.vel.y * dt <= b.pos.y + 12) {
                    if (b.invuln <= 0) {
                        b.hp--;
                        b.invuln = 1.0f;
                        b.state = 3;
                        b.timer = 0;
                        Burst(p, {b.pos.x + BB_W / 2, b.pos.y}, 14, Color{240, 240, 230, 255}, 200, 0.5f, 3);
                        if (b.hp <= 0) {
                            b.defeated = true;
                            p.exitOpen = true;
                            Burst(p, {b.pos.x + BB_W / 2, b.pos.y + 30}, 30, Pal::Brass, 260, 0.8f, 3);
                            Toast(g, "Blackbeard is beaten! The treasure is yours.");
                        }
                    }
                    p.vel.y = IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_W) || IsKeyDown(KEY_UP) ? -820.0f : -600.0f;
                    p.scale = {0.75f, 1.3f};
                } else if (b.invuln <= 0) {
                    Die(p);
                }
            }
        }

        if (p.finished) {
            const LevelDef& L = LEVELS[p.level];
            p.reward = p.coins * L.coinValue + L.bonus;
            if (p.level == PL_PIRATE) p.relic = GetRandomValue(0, (int)Relics().size() - 1);
            g.gold += p.reward;
            if (p.relic >= 0) g.relicStorage.push_back(p.relic);
            g.platCleared[p.level] = true;
            if (g.platBest[p.level] <= 0 || p.time < g.platBest[p.level]) g.platBest[p.level] = p.time;
            GeneratePlatLayout(g, p.level); // beaten: a fresh layout next time
        }
    }
    for (auto& pt : p.particles) {
        pt.p.x += pt.v.x * dt;
        pt.p.y += pt.v.y * dt;
        pt.v.y += 400 * dt;
        pt.life -= dt;
    }
    p.particles.erase(std::remove_if(p.particles.begin(), p.particles.end(), [](const PlatParticle& q) { return q.life <= 0; }), p.particles.end());

    // camera: follows smoothly, leading in the direction you're moving
    float levelW = (float)p.w * T, halfView = PIXEL_W / 2.0f / ZOOM;
    float lead = (p.facingRight ? 60.0f : -60.0f) + p.vel.x * 0.12f;
    p.camX += (p.pos.x + PW / 2 + lead - p.camX) * std::min(1.0f, dt * 5);
    p.camX = std::clamp(p.camX, halfView, levelW - halfView);

    // ---------------- draw
    // The platform levels are deliberately retro: the world is drawn at half resolution onto a small
    // canvas, then scaled up without smoothing. The canvas has a pixel of margin, and the fraction of a
    // pixel the camera has moved is applied when scaling up, so scrolling stays smooth.
    float t = g.time;
    SetPost(0.2f, 0.0f, 0.15f);
    const float PX = (float)SCREEN_W / PIXEL_W;
    float camC = p.camX * ZOOM, snapped = floorf(camC), frac = camC - snapped;
    BeginLayer(PixelRT());
    DrawBackground(p, t);
    Camera2D cam{};
    cam.zoom = ZOOM;
    cam.offset = {(PIXEL_W + 2) / 2.0f, 1 + HUD_PX};
    cam.target = {snapped / ZOOM, 0};
    BeginMode2D(cam);
    DrawBossBack(p, t);
    int c0 = std::max(0, (int)((p.camX - halfView) / T) - 2), c1 = std::min(p.w - 1, (int)((p.camX + halfView) / T) + 2);
    for (int y = 0; y < p.h; y++)
        for (int x = c0; x <= c1; x++) DrawTile(p, p.tiles[y][x], x, y, t);
    DrawBoss(p, t);
    for (auto& e : p.enemies) DrawEnemy(e, t);
    for (auto& pt : p.particles) DrawRectangle((int)pt.p.x, (int)pt.p.y, (int)pt.size, (int)pt.size, Fade(pt.c, std::min(1.0f, pt.life / pt.max * 1.5f)));
    if (p.deathTimer <= 0) DrawDiver(p);
    EndMode2D();
    EndLayer();
    DrawTexturePro(PixelRT().texture, {0, 0, PIXEL_W + 2.0f, -(PIXEL_H + 2.0f)},
                   {-PX - frac * PX, -PX, (PIXEL_W + 2) * PX, (PIXEL_H + 2) * PX}, {0, 0}, 0, WHITE);
    if (p.deathTimer > 0) DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(Pal::Bad, p.deathTimer * 0.5f));

    // ---------------- HUD
    DrawRectangle(0, 0, SCREEN_W, 56, Color{16, 30, 40, 235});
    DrawRectangle(0, 56, SCREEN_W, 3, Pal::BrassDk);
    TxtShadow(TextFormat("%s   %s", LEVELS[p.level].name, p.layoutCode.c_str()), 20, 15, 22, Pal::Brass, true);
    DrawCircle(440, 28, 10, Color{250, 210, 70, 255});
    Txt(TextFormat("x %d", p.coins), 458, 16, 22, Pal::Paper);
    int sections = (int)g.platLayouts[p.level].size() + 2;
    Txt(TextFormat("Checkpoint %d/%d", std::min(p.checkpointChunk + 1, sections), sections), 530, 18, 19, Pal::Paper);
    Txt(TextFormat("Deaths %d", p.deaths), 720, 18, 19, Pal::Paper);
    Txt(TextFormat("%.1fs", p.time), 840, 18, 19, Pal::Paper);
    if (p.boss.type && !p.boss.defeated && p.pos.x > (p.w - CH_W - 2) * (float)T) {
        const char* name = p.boss.type == 'K' ? "KRAKEN" : "BLACKBEARD";
        TxtBold(name, 930, 18, 19, Pal::Bad);
        for (int k = 0; k < 3; k++) DrawCircle(1060 + MeasureTxt(name, 19, true) - 110 + k * 18, 28, 6, k < p.boss.hp ? Pal::Bad : Color{60, 50, 50, 255});
    }
    Txt("Esc: give up", 1150, 20, 16, Color{190, 200, 200, 255});

    if (p.finished) {
        Rectangle panel{380, 190, 520, 300};
        Panel(panel);
        const LevelDef& L = LEVELS[p.level];
        DrawTextCenteredBold(p.level == PL_PIPES ? "Valve reached!" : p.level == PL_HULL ? "Back inside!" : "Treasure claimed!", panel.x + panel.width / 2, panel.y + 24, 34, Pal::Good);
        DrawTextCentered(TextFormat("%d coins x %d  +  %d bonus  =  %d gold", p.coins, L.coinValue, L.bonus, p.reward), panel.x + panel.width / 2, panel.y + 88, 21, Pal::Ink);
        DrawTextCentered(TextFormat("Time %.1fs  (best %.1fs)    Deaths %d", p.time, g.platBest[p.level], p.deaths), panel.x + panel.width / 2, panel.y + 124, 19, Pal::BrassDk);
        if (p.relic >= 0) DrawTextCenteredBold(TextFormat("Relic found: %s", Relics()[p.relic].name.c_str()), panel.x + panel.width / 2, panel.y + 160, 20, Pal::Copper);
        DrawTextCentered("A new layout will be waiting next time.", panel.x + panel.width / 2, panel.y + 194, 17, Pal::BrassDk);
        if (Button({panel.x + 110, panel.y + 228, 300, 48}, "Back to the periscope")) g.scene = Scene::Periscope;
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

bool Crossable(PlatformState& p, float goalX, bool jets, long& expanded) {
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
    while (!open.empty() && expanded < 3000000) {
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
                if (p.pos.x >= goalX && p.onGround) return true;
                SimNode n = Snap(p, held != 0);
                if (!seen.insert(Key(n, jets)).second) continue;
                nodes.push_back(n);
                open.push({n.time + (goalX - n.pos.x) / RUN, (int)nodes.size() - 1});
            }
    }
    return false;
}
}  // namespace

int VerifyPlatformLevels() {
    int failures = 0;
    for (int lv = 0; lv < PL_COUNT; lv++) {
        const LevelDef& L = LEVELS[lv];
        for (int c = 0; c <= L.count; c++) {
            bool last = c == L.count;
            if (last && lv == PL_PIPES) continue; // the Pipes end on a flat section
            PlatformState p;
            p.level = lv;
            BuildFromParts(p, {START, last ? L.last : L.chunks + c * CH_H, END_PIPES});
            bool jets = false;
            for (auto& row : p.tiles) jets |= row.find('t') != std::string::npos;
            p.exitOpen = false;
            long n = 0;
            bool ok = Crossable(p, 2.0f * CH_W * T + 8, jets, n);
            failures += !ok;
            printf("%-16s section %c: %s  (%ld states searched)\n", L.name, last ? '*' : 'A' + c, ok ? "crossable" : "NOT CROSSABLE", n);
            fflush(stdout);
        }
    }
    printf(failures ? "%d section(s) failed.\n" : "All sections can be crossed.\n", failures);
    return failures;
}
