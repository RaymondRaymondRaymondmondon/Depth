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
// Quick to reach full speed and quick to stop, so the diver goes exactly where the keys say.
constexpr float RUN = 340, ACCEL_GROUND = 6500, DECEL_GROUND = 7500, ACCEL_AIR = 4200, DECEL_AIR = 2600;
constexpr float JUMP_V = 720, GRAV_UP = 2100, GRAV_UP_RELEASED = 5400, GRAV_DOWN = 3000, MAX_FALL = 980;
constexpr float WALL_SLIDE = 150, WALLJUMP_VX = 360, WALLJUMP_VY = 690, WALL_LOCK = 0.13f;
constexpr float COYOTE = 0.1f, JUMP_BUFFER = 0.14f, STEP = 1.0f / 240; // physics runs at a fixed 240 Hz
// The world is drawn at exactly 1 canvas pixel per 2 world pixels, and the canvas is scaled up by exactly 2 (see
// PIXEL_W): so one world pixel is one screen pixel, every sprite is authored on a 2-world-pixel art grid, and
// nothing is ever stretched, filtered or drawn at a fractional offset.
constexpr float ZOOM = 0.5f, HUD_PX = 28; // canvas pixels per world pixel; HUD height in canvas pixels
constexpr float ART = 2;                  // one art pixel, in world pixels

#define E "........................"
#define W "########################"
// Where sections join: '<' in the first column marks the row you enter on, '>' in the last column the
// row you leave on (row 13 if unmarked). Each section is raised or lowered so its '<' meets the
// previous section's '>', so a level can climb or plunge as it goes.
const char* START[CH_H] = {
    W, E, E, E, E, E, E, E, E, E, E, E, E,
    ".S......................",
    W,
    W,
};

// ---------------------------------------------------------------- the Pipes: inside the ship's ducts
// Everything around these sections is solid, so you're always crawling through the Nautilus's plumbing.
// '=' and '|' are pipes you can stand on and jump off.
const char* START_PIPES[] = {W, W, W, W, W, W, W, W, W, W, W, E, E, ".S.....................>", W, W, nullptr};
const char* END_PIPES[] = {W, W, W, W, W, W, W, W, W, W, W, E, E, "<..........E............", W, W, nullptr};
const char* PIPE_RISER[] = { // up a narrow riser: wall-jump the whole way
    W,
    "########................",
    "########................",
    "########...............>",
    "########...#############",
    "########...#############",
    "########...#############",
    "########...#############",
    "########...#############",
    "########...#############",
    "########...#############",
    "########...#############",
    "########...#############",
    "########...#############",
    "########...#############",
    "...........#############",
    "...........#############",
    "<.......xxx#############",
    W, W, nullptr};
const char* PIPE_DROP[] = { // down through a steam chamber, pipe to pipe, past a gear
    W,
    "..................######",
    "..................######",
    "<.................######",
    "######............######",
    "######............######",
    "######............######",
    "######..==........######",
    "######.....g......######",
    "######............######",
    "######......==....######",
    "######..................",
    "######..................",
    "######xxxxxxxxxxxx.....>",
    W, W, nullptr};
const char* PIPE_JETS[] = { // a low duct: time your runs between the jets, hop the gaps
    W, W, W, W, W, W, W, W, W, W, W,
    E,
    "........................",
    "<......................>",
    "####t###..#t##..##t#####",
    "########..####..########",
    nullptr};
const char* PIPE_SHAFT[] = { // a long plunge down a shaft, landing on pipes as you go
    W,
    "................########",
    "................########",
    "<...............########",
    "########........########",
    "########........########",
    "########....====########",
    "########........########",
    "########...g....########",
    "########........########",
    "########====....########",
    "########........########",
    "########........########",
    "########........########",
    "########....====########",
    "########................",
    "########....g...........",
    "########................",
    "########====............",
    "########................",
    "########................",
    "########xxxxxxxx.......>",
    W, W, nullptr};
const char* PIPE_CRAWL[] = { // a crawlspace two tiles high, with steam holes to hop
    W, W, W, W, W, W, W, W, W, W, W, W,
    "........................",
    "<......................>",
    "####x###x####xx####x####",
    W, nullptr};
const char* PIPE_BOILER[] = { // up the boiler room on staggered pipes, then one long leap under a gear
    W,
    "###..................###",
    "###..................###",
    "###.....................",
    "###...............g.....",
    "###....................>",
    "###.........===......###",
    "###..................###",
    "###..................###",
    "###....===...........###",
    "###........g.........###",
    "###..................###",
    "###.........===......###",
    "###..................###",
    "###..................###",
    ".......===...........###",
    ".....................###",
    "<....xxxxxxxxxxxxxxxx###",
    W, W, nullptr};
const char* PIPE_TWINS[] = { // climb between two pipes, drop behind a third, dodge the steam below
    W,
    "......|.....|...........",
    "......|.....|...........",
    "......|.....|...........",
    "......|.....|...........",
    "......|..|..|...........",
    "......|..|..|...........",
    "......|..|..|...........",
    "......|..|..|...........",
    "......|..|..|...........",
    ".........|..............",
    ".........|..............",
    ".........|..............",
    "<........|xx...........>",
    W, W, nullptr};

// ---------------------------------------------------------------- the Hull: crabs, eels, urchins, mines
// Sections run in three phases: two laps along the outer hull plating close to the sub (near enough
// that its ribs and portholes still show behind you), then two stretches of open water further out,
// then always the drop-off and the trench mouth, descending toward the abyss where the Kraken lairs.
const char* HULL[][CH_H] = {
    {   // A (near): crab walk along the plating
        "########################", E, E, E, E, E, E, E, E, E,
        "........................",
        ".........####...........",
        E,
        ".....c.......c..........",
        "########xx######xx######",
        "########################",
    },
    {   // D (near): barnacle chimney, wall-jumping up past the hull's ribs
        "########################",
        E,
        "......#.................",
        "......#.................",
        "......#...##............",
        "......#...##............",
        "......#...##............",
        "......#...##............",
        "......#...##............",
        "......#...##............",
        "..........##............",
        "..........##............",
        "..........##............",
        "..........##.......c....",
        "#######xxx##xxx#########",
        "########################",
    },
    {   // H (near): the service duct -- a plain crawl along the plating, with a sneaky pipe
        // branching up off the main floor into a dead-end nook: a staircase of pipe segments,
        // each one jump above the last, hiding a stash of coins nobody's going to stumble on.
        "########################",
        E, E, E,
        "..............oo........",
        "..............==........",
        E, E,
        "...........==...........",
        E, E,
        "........==..............",
        E,
        ".........p..............",
        "<..........c.......c...>",
        "########xx######xx######",
    },
    {   // B (open water): eel pits
        "########################", E, E, E, E, E, E, E, E, E,
        "........................",
        E, E,
        "......e.......e.........",
        "####....####....####..##",
        "####....####....####..##",
    },
    {   // C (open water): mine climb
        "########################", E, E, E, E,
        "........................",
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
    {   // E (open water): eel bridge with a crab on the middle span
        "########################", E, E, E, E, E, E, E, E, E,
        "...........c............",
        "....###...###...###.....",
        E,
        "........e.....e.........",
        "##....................##",
        "##....................##",
    },
    {   // J (open water): the kelp drift -- floating wreckage and a pair of drifting mines
        "########################", E, E, E, E, E, E, E, E, E,
        E,
        "..g.................g...",
        E,
        "...e.............e......",
        "##....####....####....##",
        "##....####....####....##",
    },
    {   // F (trench approach): the drop-off, leaving the plating behind for open water
        "########################",
        "........................",
        "........................",
        "........................",
        "........................",
        "...###..................",
        "........................",
        ".........g..............",
        "........###.............",
        "........................",
        "........................",
        "................###.....",
        "........................",
        "......................c.",
        "########xx######xx######",
        "########################",
    },
    {   // G (trench approach): the trench mouth, right at the edge of the abyss
        "########################",
        "........................",
        "........................",
        "........................",
        "........................",
        "...........g............",
        "........................",
        "........................",
        "....c..............c....",
        "########xx######xx######",
        "........................",
        "......e..........e......",
        "####....####....####..##",
        "####....####....####..##",
        "########################",
        "########################",
    },
};
enum { HULL_NEAR0 = 0, HULL_NEAR_N = 3, HULL_OPEN0 = 3, HULL_OPEN_N = 4, HULL_TRENCH0 = 7, HULL_TRENCH_N = 2 }; // indexes into HULL[]
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

// ---------------------------------------------------------------- the Pirate Ship: deck, hold, cabin
// A run boards over the rail, crosses the open deck, drops through a hatch into the hold, climbs the
// companionway to the quarterdeck, and ends in Blackbeard's cabin. 'k' is a crate or barrel (solid).
// Pirates don't patrol: 'P' waits behind a door and bursts out to stab whoever passes, and 'G' shoots
// from behind a barrel. Parakeets ('p') still flap about above the deck.
const char* START_DECK[] = {E, E, E, E, E, E, E, E, E, E, E, E, E, ".S.....................>", W, W, nullptr};
const char* DECK_WAIST[] = { // crates, an open hatch full of spikes, and a gunner at the far end
    E, E, E, E, E, E, E,
    "..............p.........",
    E,
    "........................",
    "........................",
    "................kk......",
    "...kkk..........kk......",
    "<..kkk.....P....kk.kG..>",
    "######xxxxx###xx########",
    W, nullptr};
const char* DECK_RIGGING[] = { // up the yards and onto the forecastle
    E, E, E, E, E,
    "........................",
    ".............==.........",
    "..........p.............",
    E,
    "..........===...........",
    "...................kG..>",
    ".....===..........######",
    "..................######",
    "<.................######",
    "####xxxxxxxxxxxxxx######",
    W, nullptr};
const char* DECK_BARRELS[] = { // barrel stacks between spike-filled gratings
    E, E, E, E, E, E,
    "..........p.............",
    E,
    "........................",
    E, E,
    "........k...............",
    "........k........k......",
    "<.......k......P.k...G.>",
    "####xxx###xxx#####xx####",
    W, nullptr};
const char* DECK_MAST[] = { // a flat crossing, with a mast worth climbing far past where it's useful
    E, E, E,
    "..............oo........",
    "..............==........",
    E, E,
    "...........==...........",
    E, E,
    "........==..............",
    "....k...................",
    ".........p..............",
    "<...k......P.......kG..>",
    "####xxx#########xx######",
    W, nullptr};
const char* DECK_HATCH[] = { // down through the main hatch into the hold
    E, E, E, E, E,
    ".........p..............",
    E,
    "........................",
    E,
    "<.......................",
    "#######...##############",
    "#######...##############",
    "####...............#####",
    "####...............#####",
    "####...............#####",
    "####...............#####",
    "####......===......#####",
    "####...............#####",
    "####...............#####",
    "####...............#####",
    "####....................",
    "####....................",
    "####....................",
    "####xxxx.......kG......>",
    W, W, nullptr};
const char* HOLD_CARGO[] = { // stacks of cargo, a pirate behind the bulkhead door
    W, W, W, W, E, E, E,
    "........................",
    E,
    "..........kk............",
    "..........kk............",
    "......kk..kk.....kk.....",
    "......kk..kk.....kk.....",
    "<.....kk..kk...P.kk..G.>",
    "####xx######xxx#########",
    W, nullptr};
const char* HOLD_GUNDECK[] = { // the gun deck: a low passage with powder flares in the floor
    W, W, W, W, W, W, W, W, W,
    E,
    "........................",
    E, E,
    "<..........P.......kG..>",
    "####t###t####t###t######",
    W, nullptr};
const char* HOLD_BILGE[] = { // beams over the flooded bilge, and a swinging ball and chain
    W, W, W, W, E, E, E, E,
    "........................",
    "..............g.........",
    E,
    "......==.........==.....",
    E,
    "<..........==..........>",
    "##xxxxxxxxxxxxxxxxxxx###",
    W, nullptr};
const char* HOLD_MAGAZINE[] = { // the powder magazine: a swinging shot-chain over a low crossing
    W, W, W, W, W, W, W, W, W,
    E,
    E,
    "..................g.....",
    E,
    "<....P.............kG..>",
    "####t###t####t###t######",
    W, nullptr};
const char* COMPANIONWAY[] = { // up the companionway to the quarterdeck
    E,
    "...............p........",
    E,
    ".......................>",
    "#######...##############",
    "#######...##############",
    "#######...##############",
    "#######...##############",
    "#######...##############",
    "#######...##############",
    "#######...##############",
    "#######...##############",
    "#######...##############",
    "####......##############",
    "####......##############",
    "####......##############",
    "####...kkk##############",
    "####......##############",
    ".....kk...##############",
    ".....kk...##############",
    "<....kk...##############",
    W, W, nullptr};
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
enum { PS_DECK0 = 0, PS_DECKS = 4, PS_HATCH = 4, PS_HOLD0 = 5, PS_HOLDS = 4, PS_STAIRS = 9 }; // indexes into the Pirate sections#undef E
#undef W

// interior: the row where a section's below-decks interior starts (what's drawn behind it); kind 1 = hold, 2 = cabin
struct Part { const char* const* rows; int h; int interior = 999, kind = 0; };
Part P(const char* const* rows, int interior = 999, int kind = 0) { int h = 0; while (rows[h]) h++; return {rows, h, interior, kind}; } // null-terminated
Part P16(const char* const* rows) { return {rows, CH_H}; }
int EntryRow(const Part& s) { for (int r = 0; r < s.h; r++) if (s.rows[r][0] == '<') return r; return 13; }
int ExitRow(const Part& s) { for (int r = 0; r < s.h; r++) if (s.rows[r][CH_W - 1] == '>') return r; return 13; }

struct LevelDef {
    const char* name;
    std::vector<Part> sections;
    Part first, last;     // the opening section, and the final one (exit or boss arena)
    Part lastNoBoss;      // used instead of `last` when the boss fight is switched off
    int perRun, coinValue, bonus;
    char fill;            // what's below and around the sections: solid for the Pipes and the ship, open water otherwise
    bool dark;            // lit only by the diver's helmet lamp
    char fillAbove;       // what's above them: open sky over the pirate ship's deck
};
const LevelDef& Lv(int level) {
    static const std::vector<LevelDef> defs = [] {
        std::vector<LevelDef> d(PL_COUNT);
        Part pipesEnd = P(END_PIPES);
        d[PL_PIPES] = {"The Pipes", {P(PIPE_RISER), P(PIPE_DROP), P(PIPE_JETS), P(PIPE_SHAFT), P(PIPE_CRAWL), P(PIPE_BOILER), P(PIPE_TWINS)},
                       P(START_PIPES), pipesEnd, pipesEnd, 6, 4, 65, '#', true, '#'};
        std::vector<Part> hull;
        for (auto& c : HULL) hull.push_back(P16(c));
        d[PL_HULL] = {"The Hull", hull, P16(START), P16(HULL_ARENA), P16(HULL_ARENA_NOBOSS), 8, 5, 165, '.', false, '.'};
        std::vector<Part> pirate = {P(DECK_WAIST), P(DECK_RIGGING), P(DECK_BARRELS), P(DECK_MAST), P(DECK_HATCH, 12, 1),
                                    P(HOLD_CARGO, 4, 1), P(HOLD_GUNDECK, 9, 1), P(HOLD_BILGE, 4, 1), P(HOLD_MAGAZINE, 9, 1), P(COMPANIONWAY, 4, 1)};
        d[PL_PIRATE] = {"The Pirate Ship", pirate, P(START_DECK), P(CABIN_ARENA, 0, 2), P(CABIN_ARENA_NOBOSS, 0, 2), 8, 7, 230, '#', false, '.'};
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
    return c == '#' || c == 't' || c == '=' || c == '|' || c == 'k';
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
            int ftx = (int)floorf((e.dir > 0 ? nx + 26 : nx) / T), fty = (int)floorf((e.pos.y + 15) / T);
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
            if (Solid(p, (int)floorf(s.pos.x / T), (int)floorf(s.pos.y / T))) {
                s.life = 0;
                for (int k = 0; k < 4; k++) p.particles.push_back({s.pos, {Rnd(-80, 80), Rnd(-120, -20)}, 0.25f, 0.25f, 2, Color{200, 190, 170, 255}});
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
            }
        }
    }
    p.shots.erase(std::remove_if(p.shots.begin(), p.shots.end(), [](const PlatShot& s) { return s.life <= 0; }), p.shots.end());
}

bool ShotHits(const PlatShot& s, Rectangle pr) {
    if (s.kind == 0) return CheckCollisionRecs(pr, {s.pos.x - 3, s.pos.y - 3, 6, 6});
    if (s.kind == 2) return CheckCollisionCircleRec(s.pos, BLAST_R, pr);
    return false; // a bomb only hurts when it goes off
}
// The Kraken: an ancient horror rising from the abyss beneath the arena. Its tentacles strike up from
// the depths and slam down from above where you stand; then its great head surfaces between the
// platforms. Stomp its head three times.
// The arena's row 0 sits at KrakenOrigin(p): everything below is measured from it.
float KrakenOrigin(const PlatformState& p) { return p.boss.home.y - 14.0f * T; }
constexpr float TENT_IDLE = -100;
constexpr float BB_W = 36, BB_H = 72; // Blackbeard is a head taller than anyone
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
        if (b.inkT >= 0 && (b.inkT += dt) > 2.0f) b.inkT = -1;
        if (b.lungeT >= 0 && (b.lungeT += dt) > 1.3f) b.lungeT = -1;
        switch (b.state) {
            case 0: // submerged: picks one move for this cycle -- a tentacle strike (maybe a bluff), a
                     // spray of ink that floods half the arena, or a fast lunge sweeping across it
                if (!b.defeated && playerInArena && b.timer > 0.3f && b.timer < 1.0f && b.moveKind == -1) {
                    b.moveKind = GetRandomValue(0, 2);
                    float px = p.pos.x + PW / 2;
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
                if (b.timer > 2.9f && playerInArena) { b.state = 1; b.timer = 0; }
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
// Sections go left to right. Each is raised or lowered so the row you enter on lines up with the row
// you left the previous one on; whatever the sections don't cover is filled with `fill`.
void BuildFromParts(PlatformState& p, const std::vector<Part>& parts, char fill, char fillAbove) {
    int n = (int)parts.size();
    std::vector<int> yoff(n, 0);
    for (int i = 1; i < n; i++) yoff[i] = yoff[i - 1] + ExitRow(parts[i - 1]) - EntryRow(parts[i]);
    int top = 0, bottom = 0;
    for (int i = 0; i < n; i++) { top = std::min(top, yoff[i]); bottom = std::max(bottom, yoff[i] + parts[i].h); }
    p.w = n * CH_W;
    p.h = bottom - top;
    p.tiles.assign(p.h, std::string(p.w, fill));
    p.partX.clear();
    p.spawns.clear();
    p.partInterior.clear();
    p.partKind.clear();
    p.deathY.assign(p.w, 0);
    for (int i = 0; i < n; i++) {
        int ox = i * CH_W, oy = yoff[i] - top;
        for (int r = 0; r < oy; r++) for (int c = 0; c < CH_W; c++) p.tiles[r][ox + c] = fillAbove;
        p.partInterior.push_back(parts[i].interior >= 999 ? 1 << 20 : oy + parts[i].interior);
        p.partKind.push_back(parts[i].kind);
        for (int r = 0; r < parts[i].h; r++) {
            std::string row = parts[i].rows[r];
            if ((int)row.size() != CH_W) {
                TraceLog(LOG_WARNING, "PLATFORM: section row '%s' is %d wide, expected %d", parts[i].rows[r], (int)row.size(), CH_W);
                row.resize(CH_W, '.');
            }
            for (int c = 0; c < CH_W; c++) p.tiles[oy + r][ox + c] = (row[c] == '<' || row[c] == '>') ? '.' : row[c];
        }
        p.partX.push_back(ox);
        p.spawns.push_back({ox * (float)T + 6, (oy + EntryRow(parts[i]) + 1) * (float)T - PH});
        for (int c = 0; c < CH_W; c++) p.deathY[ox + c] = (oy + parts[i].h) * (float)T + 40;
    }
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

// Builds (or rebuilds, after a death) the whole level: coins, enemies and the boss all come back.
void BuildLevel(PlatformState& p) {
    const LevelDef& L = Lv(p.level);
    std::vector<Part> parts{L.first};
    for (int c : p.layout) parts.push_back(L.sections[c]);
    parts.push_back(p.bossEnabled ? L.last : L.lastNoBoss);
    BuildFromParts(p, parts, L.fill, L.fillAbove);
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
}

void Respawn(PlatformState& p) {
    if (!p.checkpoints) BuildLevel(p); // back to the very beginning, as it was
    p.pos = SpawnPoint(p);
    p.shots.clear();
    for (auto& e : p.enemies) if (e.type == 'P' || e.type == 'G') { e.state = 0; e.timer = 0.6f; }
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

void DrawBackground(const PlatformState& p, float t) {
    float cw = PIXEL_W + 2.0f, ch = PIXEL_H + 2.0f, cx = p.camX * ZOOM;
    switch (p.level) {
        case PL_PIPES: { // the back wall of a cramped duct, close behind you
            float cyv = p.camY * ZOOM;
            DrawRectangle(0, 0, (int)cw, (int)ch, Color{22, 20, 20, 255});
            Layer(cx, 0.5f, 150, cw, [&](float x, float wx) { // big mains running behind the plating
                DrawRectangle((int)x, 0, 26, (int)ch, Color{48, 40, 34, 255});
                DrawRectangle((int)x + 4, 0, 5, (int)ch, Color{64, 54, 46, 255});
                float fy = fmodf(wx * 7 - cyv * 0.5f + 40000, 170.0f);
                DrawRectangle((int)x - 4, (int)fy, 34, 8, Color{70, 58, 46, 255});
            });
            float ox = fmodf(cx * 0.85f, 40), oy = fmodf(cyv * 0.85f + 40000, 40);
            for (float y = -oy - 40; y < ch + 40; y += 40)      // riveted plates
                for (float x = -ox - 40; x < cw + 40; x += 40) {
                    DrawRectangle((int)x + 1, (int)y + 1, 38, 38, Color{40, 35, 32, 235});
                    DrawRectangle((int)x + 1, (int)y + 1, 38, 2, Color{54, 47, 42, 255});
                    for (int k = 0; k < 4; k++) DrawPixel((int)x + 4 + (k % 2) * 31, (int)y + 4 + (k / 2) * 31, Color{80, 68, 58, 255});
                }
            for (int k = 0; k < 3; k++) { // pipes across the plating
                float y = fmodf(k * 130.0f - cyv * 0.85f + 40000, 390.0f) - 20;
                DrawRectangle(0, (int)y, (int)cw, 9, Color{70, 50, 36, 255});
                DrawRectangle(0, (int)y + 2, (int)cw, 2, Color{110, 80, 54, 255});
            }
            Layer(cx, 0.85f, 70, cw, [&](float x, float wx) { // water dripping from the seams -- rust-dark, more often than not
                float ph = fmodf(t * 0.7f + Hs(wx) * 5, 1.0f);
                bool dark = Hs(wx + 3) > 0.4f;
                DrawRectangle((int)x, (int)(ph * ch), 1, 3, dark ? Color{80, 30, 26, 180} : Color{120, 150, 160, 160});
                if (dark && ph > 0.85f) DrawCircle((int)x, (int)ch - 4, 2, Color{60, 22, 20, 140}); // a stain pooling on the plating below
            });
            Layer(cx, 0.5f, 260, cw, [&](float x, float wx) { // claw marks raked across a plate, as if something came this way
                if (Hs(wx + 11) < 0.62f) return;
                float y = 60 + Hs(wx + 12) * (ch - 160);
                for (int k = 0; k < 4; k++) DrawLineEx({x - 14.0f + k * 9, y}, {x + 8.0f + k * 9, y + 46}, 2, Color{18, 14, 12, 200});
            });
            Layer(cx, 0.5f, 340, cw, [&](float x, float wx) { // something watches from a gap in the plating, then isn't there
                float cyc = fmodf(t * 0.11f + Hs(wx + 20) * 9, 9.0f);
                if (cyc > 1.6f) return;
                float a = std::min(1.0f, cyc * 4) * std::min(1.0f, (1.6f - cyc) * 4);
                float y = 140 + Hs(wx + 21) * (ch - 280);
                for (int s = -1; s <= 1; s += 2) DrawCircleV({x + s * 4.0f, y}, 1.4f, Fade(Color{220, 60, 50, 255}, a));
            });
            if (fmodf(t * 0.6f + cyv * 0.002f, 5.0f) < 0.12f) // the lamp flicker, and a bang echoing from deeper in
                DrawRectangle(0, 0, (int)cw, (int)ch, Fade(BLACK, 0.35f));
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
            { // the Nautilus's own hull, curving away beneath you: riveted plates, lit portholes, a fin
                float par = 0.32f, hx = cx * par, hy = 150 - (p.camY - 420) * ZOOM * par;
                DrawRectangleGradientV(0, (int)hy, (int)cw, (int)(ch - hy) + 2, Color{44, 70, 76, 255}, Color{14, 26, 32, 255});
                DrawRectangle(0, (int)hy - 3, (int)cw, 4, Color{120, 108, 70, 255});
                for (float y = hy + 22; y < ch; y += 26) DrawRectangle(0, (int)y, (int)cw, 1, Color{18, 32, 38, 255});
                float ox = fmodf(hx, 52);
                for (float x = -ox; x < cw; x += 52)
                    for (float y = hy; y < ch; y += 26) {
                        float sx = x + (((int)((y - hy) / 26)) % 2) * 26;
                        DrawRectangle((int)sx, (int)y, 1, 22, Color{18, 32, 38, 255});
                        DrawPixel((int)sx + 3, (int)y + 4, Color{90, 116, 112, 255});
                        DrawPixel((int)sx + 3, (int)y + 17, Color{90, 116, 112, 255});
                    }
                Layer(cx, par, 104, cw, [&](float x, float wx) { // two rows of portholes, glowing warm
                    for (int r = 0; r < 2; r++) {
                        float py = hy + 48 + r * 78;
                        if (Hs(wx + r * 7) < 0.25f) continue;
                        DrawCircle((int)x, (int)py, 8, Color{120, 100, 60, 255});
                        DrawCircle((int)x, (int)py, 5, Hs(wx + r) > 0.4f ? Color{255, 200, 120, 255} : Color{40, 60, 70, 255});
                    }
                });
                Layer(cx, par, 760, cw, [&](float x, float wx) { // a steering fin, and rungs up the plating
                    (void)wx;
                    DrawTri({x, hy}, {x + 90, hy}, {x + 20, hy - 60}, Color{36, 58, 64, 255});
                    DrawLineEx({x + 20, hy - 60}, {x + 90, hy}, 2, Color{120, 108, 70, 255});
                    for (int k = 0; k < 6; k++) DrawRectangle((int)x + 150, (int)(hy + 10 + k * 14), 14, 2, Color{120, 108, 70, 255});
                });
            }
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
            Layer(cx, 0.35f, 520, cw, [&](float x, float wx) { // a rival ship close by, its gunports lit
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
            for (int x = 0; x < (int)cw; x += 4) { // the near sea, rolling
                float y = 300 + sinf((x + cx * 0.6f) * 0.04f + t * 1.6f) * 4;
                DrawRectangle(x, (int)y, 4, (int)ch - (int)y, Color{18, 24, 50, 255});
                if (((x / 4) % 6) == 0) DrawRectangle(x, (int)y, 3, 1, Color{150, 160, 200, 255});
            }
        } break;
    }
}

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
            else DrawHoldWall(p, wx0, y0, wx1, y1, t, p.layout.size() > 0 && i > 0 && i - 1 < (int)p.layout.size() && p.layout[i - 1] == 5);
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
            // the Nautilus's outer plating: riveted plates two tiles square, a porthole here and there,
            // and a handrail along the walkways
            Color plate{66, 92, 94, 255}, seam{34, 50, 54, 255}, rivet{140, 164, 150, 255};
            bool inner = Solid(p, x, y - 1) && Solid(p, x, y + 1);
            DrawRectangle((int)px, (int)py, T, T, inner ? Color{54, 76, 80, 255} : plate);
            DrawRectangle((int)px + 2, (int)py + 2, T - 4, 2, Color{92, 120, 118, 255}); // light along the plate's upper edge
            if (x % 2 == 0) { DrawRectangle((int)px, (int)py, 2, T, seam); for (int k = 0; k < 3; k++) DrawCircle((int)px + 5, (int)py + 6 + k * 10, 1.5f, rivet); }
            if (y % 2 == 0) { DrawRectangle((int)px, (int)py, T, 2, seam); for (int k = 0; k < 3; k++) DrawCircle((int)px + 6 + k * 10, (int)py + 5, 1.5f, rivet); }
            if (inner && Hs(x * 3.7f + y * 11.1f) > 0.86f) { // a porthole, lit from inside the sub
                DrawCircle((int)px + 16, (int)py + 16, 9, Color{150, 120, 60, 255});
                DrawCircle((int)px + 16, (int)py + 16, 6, Color{255, 206, 130, 255});
                DrawCircle((int)px + 14, (int)py + 14, 2, Color{255, 246, 210, 255});
            }
            if (topEdge) {
                DrawRectangle((int)px, (int)py, T, 4, Color{150, 128, 70, 255});           // brass trim
                for (int k = 0; k < 3; k++) DrawCircle((int)px + 6 + k * 10, (int)py + 5, 2 + (x + k) % 2, Color{196, 192, 176, 255}); // barnacles
                if (x % 2 == 0) DrawRectangle((int)px + 14, (int)py - 14, 3, 14, Color{120, 110, 80, 255}); // handrail stanchion
                DrawRectangle((int)px, (int)py - 13, T, 2, Color{110, 100, 72, 255});      // the rail
                if ((x * 7) % 5 == 0) {
                    float sway = sinf(p.time * 2 + x) * 3;
                    DrawLineEx({px + 24, py}, {px + 22 + sway, py - 12}, 2, Color{60, 140, 80, 255}); // weed
                }
            }
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
            if (p.tiles[y][x] != 'x') continue;
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

void DrawTile(const PlatformState& p, char c, int x, int y, float t) {
    float px = x * (float)T, py = y * (float)T;
    switch (c) {
        case '#': DrawSolid(p, x, y); DrawPillarCaps(p, x, y); break;
        case 'k': { // a crate or a barrel
            bool barrel = Hs(x * 7.1f + y * 3.3f) > 0.5f;
            if (barrel) {
                DrawRectangleRounded({px + 2, py, T - 4.0f, (float)T}, 0.35f, 4, Color{120, 76, 40, 255});
                DrawRectangle((int)px + 6, (int)py + 1, 4, T - 2, Color{150, 100, 58, 255});
                DrawRectangle((int)px + 2, (int)py + 5, T - 4, 3, Color{60, 58, 62, 255});  // iron hoops
                DrawRectangle((int)px + 2, (int)py + T - 8, T - 4, 3, Color{60, 58, 62, 255});
                DrawRectangle((int)px + 22, (int)py + 2, 2, T - 4, Color{90, 56, 28, 255});
            } else {
                DrawRectangle((int)px, (int)py, T, T, Color{150, 108, 62, 255});
                DrawRectangleLines((int)px, (int)py, T, T, Color{80, 54, 30, 255});
                DrawRectangleLines((int)px + 3, (int)py + 3, T - 6, T - 6, Color{96, 66, 36, 255});
                DrawLineEx({px + 4, py + 4}, {px + T - 4, py + T - 4}, 3, Color{110, 78, 42, 255}); // cross-brace
                DrawRectangle((int)px + 2, (int)py + 2, T - 4, 2, Color{186, 140, 88, 255});
            }
        } break;
        case '=': if (p.level == PL_PIRATE) { // a yard or a beam, lashed with rope
            DrawRectangle((int)px, (int)py + 6, T, 14, Color{104, 70, 40, 255});
            DrawRectangle((int)px, (int)py + 6, T, 3, Color{150, 108, 64, 255});
            DrawRectangle((int)px, (int)py + 17, T, 3, Color{70, 46, 26, 255});
            if (At(p, x - 1, y) != '=' || x % 3 == 0) for (int k = 0; k < 3; k++) DrawRectangle((int)px + 4 + k * 3, (int)py + 5, 2, 16, Color{200, 180, 130, 255});
            break;
        } else { // a horizontal pipe you can stand on
            Color pipe = p.level == PL_PIPES ? Color{176, 104, 62, 255} : Color{120, 124, 118, 255};
            DrawPipeH(px, px + T, py + 16, 14, pipe);
            if (At(p, x - 1, y) != '=') DrawFlange({px + 3, py + 16}, 12, false, Pal::BrassDk);
            if (At(p, x + 1, y) != '=') DrawFlange({px + T - 3, py + 16}, 12, false, Pal::BrassDk);
            if (x % 4 == 0 && At(p, x + 1, y) == '=') DrawCircleV({px + T, py + 4}, 3, Color{220, 60, 40, 255}); // a valve wheel
        } break;
        case '|': { // a vertical pipe you can jump off
            Color pipe = Color{150, 154, 146, 255};
            DrawPipeV(px + 16, py, py + T, 14, pipe);
            if (At(p, x, y - 1) != '|') DrawFlange({px + 16, py + 3}, 12, true, Pal::BrassDk);
            if (At(p, x, y + 1) != '|') DrawFlange({px + 16, py + T - 3}, 12, true, Pal::BrassDk);
        } break;
        case 'x': {
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
    bool running = p.onGround && fabsf(p.vel.x) > 30, sliding = p.wallSide != 0;
    int step = running ? (int)roundf(sinf(p.runAnim) * 1.4f) : 0;             // legs swing by whole art pixels
    int lean = running ? (fabsf(p.vel.x) > RUN * 0.6f ? 1 : 0) : 0;            // the head and shoulders lean into the run
    int bob = running && (int)(p.runAnim / PI) % 2 == 0 ? 0 : 0;
    (void)bob;
    // twin air tanks on the back, and the hose up to the helmet
    R(-6, -9, 2, 6, TANKS_METAL); R(-6, -9, 2, 1, TANKS_DARK); R(-6, -5, 2, 1, TANKS_DARK);
    R(-5, -8, 2, 6, TANKS_DARK); R(-5, -8, 1, 5, TANKS_METAL);
    R(-5, -10, 1, 1, HELMET_DARK);
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
    if (!sliding) R(-4 + lean, -8, 1, 4, SUIT_SHADOW);
    R(-3 + lean, -9, 6, 5, SUIT_MAIN);
    R(-3 + lean, -9, 1, 5, SUIT_SHADOW);
    R(-3 + lean, -5, 6, 1, HELMET_DARK); R(0 + lean, -5, 1, 1, HELMET_LIGHT);
    R(-3 + lean, -9, 6, 1, HELMET_BASE);
    // the near arm: reaching for the wall when sliding, swinging with the stride otherwise
    if (sliding) { R(2 + lean, -12, 2, 5, SUIT_MAIN); R(2 + lean, -13, 2, 1, HELMET_BASE); }
    else { R(3 + lean, -8, 1, 3, SUIT_MAIN); R(3 + lean, -5, 1, 1, HELMET_BASE); }
    // the helmet: a copper dome with a gold highlight and a shadowed underside
    R(-2 + lean, -14, 4, 1, HELMET_BASE);
    R(-3 + lean, -13, 6, 4, HELMET_BASE);
    R(-3 + lean, -13, 2, 1, HELMET_LIGHT); R(-2 + lean, -14, 2, 1, HELMET_LIGHT);
    R(-3 + lean, -11, 1, 2, HELMET_DARK); R(2 + lean, -10, 1, 1, HELMET_DARK);
    R(-3 + lean, -10, 6, 1, HELMET_DARK);
    // the cyan visor, glowing, with a hot centre
    R(0 + lean, -13, 3, 2, VISOR_GLOW);
    if (!outline) { R(1 + lean, -13, 1, 1, VISOR_INNER); }
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
        }
    EndBlendMode();
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
        case 'P': { // a cutthroat behind a door: you see his eyes through the gap, then the door bangs open
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

void DrawBoss(const PlatformState& p, float t) {
    const PlatBoss& b = p.boss;
    if (b.type == 'K') {
        Color arm{130, 60, 140, 255};
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
        if (b.state >= 1) {
            Rectangle h = KrakenHead(p);
            const float S = KRAKEN_SCALE;
            bool blink = b.invuln > 0 && fmodf(t, 0.15f) < 0.075f;
            Color skin = blink ? WHITE : Color{130, 64, 150, 255}, dk{96, 44, 112, 255};
            float cx = h.x + h.width / 2;
            for (int k = -3; k <= 3; k++) // a crown of arms writhing around the huge head
                DrawTentacle({cx + k * 18 * S, h.y + 60 * S}, {cx + k * 46 * S, h.y + (110 + fabsf((float)k) * 8) * S}, 9 * S, t * 3, k * 2.0f, dk);
            DrawEllipse((int)cx, (int)(h.y + 28 * S), 54 * S, 40 * S, skin);             // the great mantle
            DrawEllipse((int)(cx - 14 * S), (int)(h.y + 12 * S), 22 * S, 12 * S, Fade(WHITE, 0.18f));
            for (int k = 0; k < 6; k++) DrawCircle((int)(cx - 36 * S + k * 14 * S), (int)(h.y + (20 + (k % 2) * 10) * S), 3 * S, dk); // mottling
            for (int s = -1; s <= 1; s += 2) { // vast eyes that follow you
                float look = std::clamp((p.pos.x - (cx + s * 24 * S)) * 0.015f, -6.0f, 6.0f) * S;
                DrawEllipse((int)(cx + s * 24 * S), (int)(h.y + 42 * S), 12 * S, 9 * S, Color{250, 220, 80, 255});
                DrawRectangle((int)(cx + s * 24 * S - 2 * S + look), (int)(h.y + 35 * S), 4 * S, 14 * S, Pal::Ink);
            }
            DrawTri({cx - 8 * S, h.y + 58 * S}, {cx + 8 * S, h.y + 58 * S}, {cx, h.y + 70 * S}, Color{40, 30, 30, 255}); // the beak
            DrawTri({cx - 12 * S, h.y - 6 * S}, {cx, h.y + 6 * S}, {cx + 12 * S, h.y - 6 * S}, Color{250, 220, 80, 200}); // stomp here
        }
    } else if (b.type == 'B' && !b.defeated) {
        float x = b.pos.x, y = b.pos.y, f = b.dir, cx = x + BB_W / 2;
        bool blink = b.invuln > 0 && fmodf(t, 0.15f) < 0.075f;
        if (blink) return;
        bool dazed = b.state == 5, windup = b.state == 1, charging = b.state == 2, aiming = b.state == 4;
        float step = b.vel.x != 0 ? sinf(t * (charging ? 22 : 12)) * 5 : 0;
        float lean = charging ? f * 8 : windup ? -f * 3 : dazed ? sinf(t * 3) * 4 : 0; // head and shoulders
        float dip = windup ? 5 + sinf(t * 30) : 0;                                   // crouched to charge
        Color coat{140, 26, 30, 255}, coatDk{96, 16, 20, 255}, boot{24, 20, 18, 255};
        DrawRectangleRec({cx - 12 + step, y + 50, 10, 22}, boot);                          // tall boots
        DrawRectangleRec({cx + 2 - step, y + 50, 10, 22}, boot);
        DrawRectangleRec({cx - 14 + step, y + 48, 13, 5}, Color{60, 40, 24, 255});
        DrawRectangleRec({cx + 1 - step, y + 48, 13, 5}, Color{60, 40, 24, 255});
        float ty = y + dip;
        DrawTri({cx - 18 + lean * 0.5f, ty + 22}, {cx + 18 + lean * 0.5f, ty + 22}, {cx + 20, y + 56}, coat); // a long coat
        DrawTri({cx - 18 + lean * 0.5f, ty + 22}, {cx + 20, y + 56}, {cx - 22, y + 56}, coatDk);
        DrawRectangleRounded({cx - 17 + lean * 0.5f, ty + 18, 34, 28}, 0.3f, 4, coat);
        DrawRectangleRec({cx - 17 + lean * 0.5f, ty + 38, 34, 5}, Color{60, 40, 24, 255}); // belt
        DrawRectangleRec({cx - 3 + lean * 0.5f, ty + 37, 6, 7}, Pal::Brass);
        DrawRectangleRec({cx - 17 + lean * 0.5f, ty + 18, 34, 3}, Pal::Brass);
        float hx = cx + lean, hy = ty;
        DrawCircle((int)hx, (int)hy + 12, 9, Color{220, 170, 130, 255});                  // face
        DrawCircle((int)hx, (int)hy + 20, 11, Color{24, 22, 22, 255});                    // the famous beard...
        DrawRectangleRec({hx - 10, hy + 20, 20, 12}, Color{24, 22, 22, 255});
        for (int k = 0; k < 3; k++) {                                                   // ...with smoking fuses in it
            Vector2 fz{hx - 8 + k * 8.0f, hy + 28};
            DrawCircleV(fz, 1.5f, Color{255, 140, 40, 255});
            float ph = fmodf(t * 0.8f + k * 0.3f, 1.0f);
            DrawCircleV({fz.x + sinf(ph * 6 + k) * 3, fz.y - 6 - ph * 22}, 2 + ph * 3, Fade(Color{150, 150, 150, 255}, 0.6f * (1 - ph)));
        }
        if (dazed) { // cross-eyed, with stars going round
            DrawLineEx({hx + f * 2, hy + 8}, {hx + f * 6, hy + 12}, 1.5f, Pal::Ink);
            DrawLineEx({hx + f * 6, hy + 8}, {hx + f * 2, hy + 12}, 1.5f, Pal::Ink);
            for (int k = 0; k < 4; k++) {
                float a = t * 5 + k * PI / 2;
                Vector2 st{hx + cosf(a) * 20, hy - 12 + sinf(a) * 5};
                DrawRectangle((int)st.x - 1, (int)st.y - 3, 2, 6, Color{255, 230, 90, 255});
                DrawRectangle((int)st.x - 3, (int)st.y - 1, 6, 2, Color{255, 230, 90, 255});
            }
        } else {
            DrawCircle((int)(hx + f * 4), (int)hy + 10, 2, windup || charging ? Color{255, 60, 40, 255} : Pal::Ink);
        }
        DrawTri({hx - 22, hy + 4}, {hx + 22, hy + 4}, {hx, hy - 14}, Color{24, 22, 30, 255}); // tricorn
        DrawRectangleRec({hx - 22, hy + 2, 44, 4}, Color{24, 22, 30, 255});
        DrawCircle((int)hx, (int)hy - 3, 3, Color{230, 230, 220, 255});                    // skull badge
        Vector2 hand{cx + f * 18 + lean * 0.5f, ty + 30};
        if (aiming) { // a pistol, levelled at you
            float u = std::min(1.0f, b.timer / 0.5f);
            Vector2 aimTo{p.pos.x + PW / 2, p.pos.y + PH / 2};
            float ang = atan2f(aimTo.y - hand.y, aimTo.x - hand.x);
            Vector2 muzzle{hand.x + cosf(ang) * 12, hand.y + sinf(ang) * 12};
            DrawLineEx(hand, muzzle, 4, Color{70, 70, 76, 255});
            DrawCircleV(hand, 3, Pal::Brass);
            for (float d = 18; d < 18 + 80 * u; d += 10)
                DrawRectangle((int)(hand.x + cosf(ang) * d), (int)(hand.y + sinf(ang) * d), 2, 2, Fade(Color{255, 90, 60, 255}, 0.5f));
            if (fmodf(t * 12, 1.0f) < 0.5f) DrawCircleV(muzzle, 2.5f, Color{255, 230, 150, 255});
        } else {
            float swordA = windup ? -1.3f : charging ? 0.1f : dazed ? 1.2f : -0.5f;
            DrawLineEx(hand, {hand.x + f * cosf(swordA) * 34, hand.y + sinf(swordA) * 34}, 3, Color{210, 214, 220, 255});
            DrawCircleV(hand, 3, Pal::Brass);
        }
        if (windup) // he paws the boards
            for (int k = 0; k < 2; k++) DrawRectangle((int)(cx - f * 14 + GetRandomValue(-6, 6)), (int)(y + BB_H - 3), 3, 3, Color{190, 170, 140, 255});
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
        } else if (s.kind == 1) {
            bool flash = fmodf(t * (4 + (BOMB_FUSE - s.life) * 10), 1.0f) < 0.5f;
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

// The pirate ship always runs deck, deck, hatch, hold, hold, companionway (in a random mix of each).
static std::vector<int> Shuffled(int first, int count, int keep) {
    std::vector<int> idx;
    for (int i = 0; i < count; i++) idx.push_back(first + i);
    for (int i = count - 1; i > 0; i--) std::swap(idx[i], idx[GetRandomValue(0, i)]);
    idx.resize(keep);
    return idx;
}

bool PlatLayoutValid(const Game& g, int level) {
    const std::vector<int>& l = g.platLayouts[level];
    if (l.empty()) return false;
    for (int c : l) if (c < 0 || c >= (int)Lv(level).sections.size()) return false;
    if (level == PL_PIRATE) {
        if (l.size() != 8 || l[3] != PS_HATCH || l[7] != PS_STAIRS) return false;
        for (int i : {0, 1, 2}) if (l[i] < PS_DECK0 || l[i] >= PS_DECK0 + PS_DECKS) return false;
        for (int i : {4, 5, 6}) if (l[i] < PS_HOLD0 || l[i] >= PS_HOLD0 + PS_HOLDS) return false;
    } else if (level == PL_HULL) {
        // near the hull, then open water, then always the drop-off and the trench mouth, in that order
        if (l.size() != 8) return false;
        for (int i : {0, 1, 2}) if (l[i] < HULL_NEAR0 || l[i] >= HULL_NEAR0 + HULL_NEAR_N) return false;
        for (int i : {3, 4, 5}) if (l[i] < HULL_OPEN0 || l[i] >= HULL_OPEN0 + HULL_OPEN_N) return false;
        if (l[6] != HULL_TRENCH0 || l[7] != HULL_TRENCH0 + 1) return false;
    }
    return true;
}

void GeneratePlatLayout(Game& g, int level) {
    if (level == PL_PIRATE) {
        std::vector<int> deck = Shuffled(PS_DECK0, PS_DECKS, 3), hold = Shuffled(PS_HOLD0, PS_HOLDS, 3);
        g.platLayouts[level] = {deck[0], deck[1], deck[2], PS_HATCH, hold[0], hold[1], hold[2], PS_STAIRS};
        return;
    }
    if (level == PL_HULL) {
        std::vector<int> near = Shuffled(HULL_NEAR0, HULL_NEAR_N, 3), open = Shuffled(HULL_OPEN0, HULL_OPEN_N, 3);
        g.platLayouts[level] = {near[0], near[1], near[2], open[0], open[1], open[2], HULL_TRENCH0, HULL_TRENCH0 + 1};
        return;
    }
    const LevelDef& L = Lv(level);
    int count = (int)L.sections.size();
    std::vector<int> idx;
    for (int i = 0; i < count; i++) idx.push_back(i);
    for (int i = (int)idx.size() - 1; i > 0; i--) std::swap(idx[i], idx[GetRandomValue(0, i)]);
    idx.resize(std::min(L.perRun, count));
    g.platLayouts[level] = idx;
}

std::string PlatLayoutCode(const Game& g, int level) {
    std::string s;
    for (int c : g.platLayouts[level]) { if (!s.empty()) s += "-"; s += (char)('A' + c); }
    return s.empty() ? "(new)" : s;
}

void StartPlatform(Game& g, int level) {
    if (!PlatLayoutValid(g, level)) GeneratePlatLayout(g, level); // e.g. an old save
    g.plat = PlatformState{};
    g.plat.level = level;
    g.plat.layoutCode = PlatLayoutCode(g, level);
    g.plat.layout = g.platLayouts[level];
    g.plat.hard = g.platHard;
    g.plat.checkpoints = g.platCheckpoints;
    g.plat.bossEnabled = level == PL_HULL ? g.platHullBoss : level == PL_PIRATE ? g.platPirateBoss : true;
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
            int part = PartAt(p, p.pos.x + PW / 2);
            if (p.onGround && part > p.checkpointChunk) {
                p.checkpointChunk = part; // with checkpoints off this just counts progress
                if (p.checkpoints) Burst(p, {p.pos.x + PW / 2, p.pos.y}, 12, Pal::Good, 140, 0.5f, 2);
            }
        }
        UpdateEnemies(p, dt);
        UpdateBoss(p, dt);
        UpdateShots(p, dt);

        // touching an enemy is deadly; only a boss can be stomped
        if (p.deathTimer <= 0 && !p.finished) {
            Rectangle pr = PlayerBox(p);
            for (auto& e : p.enemies) if (EnemyHits(e, pr)) Die(p);
            for (auto& s : p.shots) if (ShotHits(s, pr)) Die(p);
            PlatBoss& b = p.boss;
            bool falling = p.vel.y > 0;
            if (b.type == 'K' && !b.defeated) {
                for (int i = 0; i < 2; i++) if (b.tentT[i] >= 0.75f && CheckCollisionRecs(pr, TentacleBox(p, i))) Die(p);
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
        pt.v.y += 400 * dt;
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
    int r0 = std::max(0, (int)((p.camY - viewH / 2) / T) - 2), r1 = std::min(p.h - 1, (int)((p.camY + viewH / 2) / T) + 2);
    for (int y = r0; y <= r1; y++) // first the sides and tops of blocks, which recede into the scene...
        for (int x = c0; x <= c1; x++) DrawDepth(p, x, y);
    for (int y = r0; y <= r1; y++) // ...then their faces and everything else
        for (int x = c0; x <= c1; x++) DrawTile(p, p.tiles[y][x], x, y, t);
    DrawHazardOverlay(p, c0, c1, r0, r1, t);
    DrawBoss(p, t);
    for (auto& e : p.enemies) DrawEnemy(e, t);
    DrawShots(p, t);
    for (auto& pt : p.particles) DrawRectangle((int)pt.p.x, (int)pt.p.y, (int)pt.size, (int)pt.size, Fade(pt.c, std::min(1.0f, pt.life / pt.max * 1.5f)));
    if (p.deathTimer <= 0) {
        DrawDiver(p);
    }
    EndMode2D();
    if (Lv(p.level).dark) {
        Vector2 lamp = GetWorldToScreen2D({p.pos.x + PW / 2 + (p.facingRight ? 14.0f : -14.0f), p.pos.y + 6}, cam);
        DrawLampDarkness(lamp, p.hard ? 120.0f : 148.0f, p.hard ? 0.82f : 0.74f); // Normal lights more of the duct
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
        const char* name = p.boss.type == 'K' ? "KRAKEN" : "BLACKBEARD";
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
        DrawTentacle({120, 340}, {130, 200}, 16, t * 2, 1, Color{130, 60, 140, 255});
        DrawTentacle({220, 190}, {230, 330}, 16, t * 2, 2, Color{130, 60, 140, 255});
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
        const LevelDef& L = Lv(lv);
        int count = (int)L.sections.size();
        for (int c = 0; c <= count; c++) {
            bool last = c == count;
            if (last && lv == PL_PIPES) continue; // the Pipes end on a flat section
            PlatformState p;
            p.level = lv;
            BuildFromParts(p, {L.first, last ? L.last : L.sections[c], P(END_PIPES)}, L.fill, L.fillAbove);
            bool jets = false;
            for (auto& row : p.tiles) jets |= row.find('t') != std::string::npos;
            p.exitOpen = false;
            long n = 0;
            float goal = p.partX[2] * (float)T + 8; // into the next section, or onto the exit if this one ends the level
            for (int r = 0; r < p.h; r++)
                for (int x = p.partX[1]; x < p.partX[2]; x++) if (p.tiles[r][x] == 'E') goal = x * (float)T;
            bool ok = Crossable(p, goal, jets, n);
            failures += !ok;
            printf("%-16s section %c: %s  (%ld states searched)\n", L.name, last ? '*' : 'A' + c, ok ? "crossable" : "NOT CROSSABLE", n);
            fflush(stdout);
        }
        if (lv == PL_HULL || lv == PL_PIRATE) { // also check the arena used when the boss fight is switched off
            PlatformState p;
            p.level = lv;
            BuildFromParts(p, {L.first, L.lastNoBoss, P(END_PIPES)}, L.fill, L.fillAbove);
            bool jets = false;
            for (auto& row : p.tiles) jets |= row.find('t') != std::string::npos;
            p.exitOpen = false;
            long n = 0;
            float goal = p.partX[2] * (float)T + 8;
            for (int r = 0; r < p.h; r++)
                for (int x = p.partX[1]; x < p.partX[2]; x++) if (p.tiles[r][x] == 'E') goal = x * (float)T;
            bool ok = Crossable(p, goal, jets, n);
            failures += !ok;
            printf("%-16s no-boss arena: %s  (%ld states searched)\n", L.name, ok ? "crossable" : "NOT CROSSABLE", n);
            fflush(stdout);
        }
    }
    printf(failures ? "%d section(s) failed.\n" : "All sections can be crossed.\n", failures);
    return failures;
}
