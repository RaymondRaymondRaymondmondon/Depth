#pragma once
// Warp Dodgeball (the Deep Arcade's ninth game, Slop group): team dodgeball where every player carries a portal pair.
// The spec is "Warp Dodgeball - Game Design Spec.pdf" (Reference_For_Future_MP_Games; OCR in docs/warp_pdf_pages).
// The spec targets Godot; the game is built inside Depth like every other arcade game (the user's call is logged in
// docs/WARP_PROGRESS.md). This header is the headless core: the arena, players, balls, throwing, curve, catching,
// portals, rules, rounds and bots, stepped at a fixed 120 Hz. Every tuning number is in data/warp/warp_config.json.
// Frame: x runs the court's length (team 0 on x < 0, team 1 on x > 0; the centre line is x = 0), y is up, z across.
#include "raylib.h"
#include "raymath.h"
#include <cstdint>
#include <string>
#include <vector>

namespace wd {

constexpr float STEP = 1.0f / 120;
constexpr int MAX_PLAYERS = 12;

struct Config {
    // the player (spec "Player movement and controls")
    float height = 1.8f, radius = 0.25f, walk = 4.5f, sprint = 6.5f, crouchSpeed = 2.0f, ladderSpeed = 2.0f, jumpApex = 1.2f, airControl = 0.3f, gravity = 9.81f;
    float crouchH = 1.1f, squatH = 0.9f, squatTime = 0.4f, squatCool = 0.6f, diveDist = 3.0f, diveTime = 0.3f, proneH = 0.5f, diveRecover = 0.8f, chargeSlow = 0.7f, eyeFromTop = 0.12f;
    // throwing (spec "Throwing")
    float chargeTime = 1.2f, speedMin = 8, speedAdd = 18, loftZero = 10, loftFull = 2, overAfter = 2.0f, overRate = 2, overMax = 6, releaseTime = 0.15f, selfImmune = 0.3f;
    float spinMax = 30, magnusK = 0.0035f, spinDecay = 0.15f, spinBounce = 0.5f;
    // the ball
    float ballR = 0.105f, ballMass = 0.25f, dragCd = 0.47f, airRho = 1.2f, bounceFloor = 0.6f, bounceWall = 0.7f, friction = 0.5f, restSpeed = 0.25f;
    // catching
    float lookahead = 0.6f, coneDeg = 100, slowSpeed = 12, fastSpeed = 20, winSlow = 0.35f, winMid = 0.25f, winFast = 0.15f;
    // portals
    float portalW = 1.0f, portalH = 1.6f, portalCool = 1.0f, exitIgnore = 0.1f;
    // the match
    float roundTime = 180; int roundsToWin = 3, teamSize = 4; float attackLine = 3, rushWhistle = 2, roundEndPause = 3; bool friendlyFire = false, headshots = true;
    // the arenas
    float cLength = 18, cWidth = 9, cRunoff = 2, cWall = 6, cCeil = 8; int cBalls = 6;
    float xLength = 30, xWidth = 16, xWall = 8, xCeil = 10; int xBalls = 8;
};
const Config& Cfg();
Config& CfgMutable();   // (tests tune a number, then put it back)

// ---------------------------------------------------------------- the arena
// A panel is a flat rectangle that takes portals (the light grey surfaces); a box is solid (cover, pillars, nest decks)
struct Panel { Vector3 c, n, u; float hw, hh; };            // centre, outward normal, up along the panel, half extents
struct Box { Vector3 lo, hi; uint8_t portalFaces = 0; int kind = 0; };   // portalFaces: bit per face (0 -x, 1 +x, 2 -y, 3 +y, 4 -z, 5 +z)
struct Ladder { Vector3 base; float top; Vector3 into; };   // a ladder up a nest's side (into: the way you face climbing)
enum ArenaKind { AR_CLASSIC, AR_EXTREME };
struct Arena {
    int kind = AR_CLASSIC;
    float halfL = 9, halfW = 4.5f, runoff = 2, wall = 6, ceil = 8;   // the court, the run-off, the walls' height, the ceiling
    std::vector<Box> boxes; std::vector<Panel> panels; std::vector<Ladder> ladders;
    float OuterX() const { return halfL + runoff; }
    float OuterZ() const { return halfW + runoff; }
};
Arena MakeArena(int kind);

// ---------------------------------------------------------------- players and balls
enum Posture : uint8_t { PO_STAND, PO_CROUCH, PO_SQUAT, PO_DIVE, PO_PRONE, PO_LADDER };
struct Input {
    float moveX = 0, moveZ = 0;          // the wish, in the player's own frame (x forward, z right), length <= 1
    float yaw = 0, pitch = 0;            // where the player looks (absolute: the camera's)
    bool sprint = false, jump = false, crouch = false, squat = false, dive = false;   // squat and dive: presses
    bool throwHeld = false, cancel = false; float curve = 0;   // curve: -1 left .. 1 right, read at release
    bool portalA = false, portalB = false, catchP = false;     // presses
};
struct Portal { bool on = false; Vector3 c{}, n{}, u{}; float cool = 0; };
struct Player {
    int id = 0, team = 0; bool alive = true, present = true; std::string name;
    Vector3 pos{}, vel{}; float yaw = 0, pitch = 0; bool grounded = true;
    Posture po = PO_STAND; float poT = 0, squatCool = 0; Vector3 diveDir{};
    int ladder = -1;                     // the ladder being climbed
    int ball = -1;                       // the ball in hand (-1: empty)
    float charge = 0, heldT = 0, overT = 0, releaseT = 0; bool charging = false; float relCurve = 0;   // the throw in progress
    Portal portal[2];
    float catchPressT = -10;             // when the catch button was last pressed (the world's clock)
    int pendingHit = -1; float pendingT = 0;   // a ball that touched you, waiting the back half of the window for a late press
    int outs = 0, catches = 0, sidelineOrder = -1;
    std::string outCause;
    Input in;
    float Height() const;
    Vector3 Eye() const;
    Vector3 Look() const { return {cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw)}; }
};
enum BallState : uint8_t { BS_REST, BS_HELD, BS_LIVE, BS_DEAD };
struct Ball {
    Vector3 p{}, v{}, spin{}; BallState st = BS_REST;
    int holder = -1, thrower = -1, team = -1;
    bool viaPortal = false, viaWall = false, mustCarry = false;   // (rebound-out needs one of the first two; a rush ball must be carried behind the attack line)
    float age = 0, ignoreT = 0;
};

// ---------------------------------------------------------------- the world and the match
struct Event { int kind; Vector3 at; int who, by; float a; };
enum EventKind { EV_THROW = 1, EV_BOUNCE, EV_HIT, EV_OUT, EV_CATCH, EV_FUMBLE, EV_BLOCK, EV_PORTAL_PLACE, EV_PORTAL_FAIL, EV_TRANSIT, EV_PICKUP, EV_RETURN, EV_WHISTLE, EV_ROUND, EV_LINE };
enum Phase : uint8_t { PH_WARMUP, PH_RUSH, PH_LIVE, PH_ROUND_END, PH_MATCH_END };
struct World {
    Arena arena; std::vector<Player> players; std::vector<Ball> balls; std::vector<Event> events;
    float t = 0; uint32_t rng = 1; Phase phase = PH_WARMUP; float phaseT = 0;
    int round = 0, wins[2] = {0, 0}, roundWinner = -1, champion = -1; bool suddenDeath = false;
    std::vector<int> sideline[2];        // the eliminated, in catch order
    bool practice = false;               // practice: the arc preview (never in matches)
    void Init(int arenaKind, int perTeam, uint32_t seed);
    void NewRound();
    void Step();                         // one fixed step (every player's Input set first)
    float Rand();
    int Alive(int team) const;
    // pieces
    void StepPlayer(Player& p);
    void StepThrow(Player& p);
    void StepBall(Ball& b);
    void StepCatch(Player& p);
    void PlacePortal(Player& p, int which);
    bool RayPanel(Vector3 o, Vector3 d, float maxT, Vector3* hit, int* panel) const;
    bool RaySolid(Vector3 o, Vector3 d, float maxT, float* tHit) const;
    float GroundAt(Vector3 p, float r) const;   // the highest walkable top under a circle at p (0: the floor)
    void Out(Player& p, int by, const char* cause);
    void Return(int team);
    void Emit(int kind, Vector3 at, int who = -1, int by = -1, float a = 0) { events.push_back({kind, at, who, by, a}); if (events.size() > 400) events.erase(events.begin(), events.begin() + 200); }
    bool Threat(const Player& p, int* ballOut, float* tContact) const;   // the most urgent incoming enemy ball (for the catch prompt)
    float CatchWindow(float speed) const;
};

void BotInput(const World& w, int me, Input& in, uint32_t& rng, int skill);
int RunWarpTest();                         // depth.exe --warp-test (the milestones' acceptance tests)
int RunWarpSim(int matches, int perTeam);  // depth.exe --warp-sim [matches] [perTeam]

}  // namespace wd
