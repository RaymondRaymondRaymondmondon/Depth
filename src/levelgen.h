// ============================================================================
//  DEPTH - the kinematic level generator. Platform levels are no longer stitched from hand-made chunks: they are
//  built platform by platform from the diver's own movement numbers. Every gap and step is sized from the arc
//  the diver can actually jump (CalculateValidJumpArc), scaled by a per-level safety factor (the Pipes stay far
//  inside the arc, the Pirate Ship lives at its very edge). Pass 1 lays the critical path and its set-pieces;
//  pass 2 fills the surfaces with hazards, enemies and coins. platformer.cpp then proves every hop with the
//  real movement code and regenerates the level if any hop can't be made.
// ============================================================================
#pragma once
#include <string>
#include <vector>

// The movement constants, shared with platformer.cpp so the generator and the game can never disagree.
namespace kin {
constexpr float RUN = 340, JUMP_V = 720, GRAV_UP = 2100, GRAV_UP_RELEASED = 5400, GRAV_DOWN = 3000, MAX_FALL = 980;
constexpr float WALL_SLIDE = 150, WALLJUMP_VX = 360, WALLJUMP_VY = 690;
constexpr float TILE = 32, PLAYER_W = 20, PLAYER_H = 26;
}  // namespace kin

struct ArcPoint { float x, y; };   // px from the launch edge, and px above the launch surface

struct JumpArc {
    bool reachable = false;        // false when the target is higher than the jump can rise
    float apex = 0;                // highest point of a full jump, px above the launch surface
    float timeUp = 0;              // seconds to the apex
    float maxReach = 0;            // farthest horizontal distance (px) at which a surface `rise` px higher can be landed on
    std::vector<ArcPoint> samples; // the full-jump arc, from launch to landing
};
// The reachable arc from the edge of one platform to a surface `risePx` above it (negative = below).
JumpArc CalculateValidJumpArc(float risePx, int samples = 0);

// Set-pieces are injected while the critical path is plotted, and the terrain adapts its spacing to them.
enum class SetPiece { None, SteamBoost, CrumbleRun, GearGauntlet, BarnacleShaft, ShipGap, ShaftUp, ShaftDown };

struct GenWaypoint { int tx, ty; SetPiece tag; };   // a standing tile on the critical path (ty = the row you stand in)

struct GenLevel {
    int w = 0, h = 0;
    std::vector<std::string> rows;      // the finished terrain, hazards and enemies, in the game's tile characters
    std::vector<GenWaypoint> path;      // consecutive standing points, each reachable from the one before
    int exitRow = 0;                    // the standing row of the last platform, where a boss arena would be joined
    float safety = 0;
    bool enclosed = false;              // solid all around (the Pipes) rather than open water or sky
    std::vector<int> setPieces;         // count per SetPiece, for the developer report
};

// The tallest up-shaft (rows) that --verify has proven climbable, by interior width and barnacle lining.
constexpr int UP_MAX_3 = 6, UP_MAX_4 = 6, UP_MAX_3B = 6, UP_MAX_4B = 6;
// A lone shaft between two platforms, for the developer verification of the shaft dimensions.
GenLevel ShaftTemplate(int iw, int Hs, bool up, bool barnacle);

// level: 0 Pipes, 1 Hull, 2 Pirate Ship. Deterministic for a given seed. safetyScale < 1 makes it more forgiving.
GenLevel GenerateLevel(int level, unsigned seed, float safetyScale = 1.0f);
