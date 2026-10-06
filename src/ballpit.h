#pragma once
// Ball Pit Brawl (the Deep Arcade's thirteenth game, Fighting group): a first-person foam-weapon shooter inside a giant
// four-level indoor play centre. The spec is "Ball Pit Brawl - Game Design Spec.pdf" (Reference_For_Future_MP_Games;
// OCR in docs/ballpit_pdf_pages). Like Warp Dodgeball it is built inside Depth, not Godot (log: docs/BALLPIT_PROGRESS.md).
// This header is the headless core, stepped at a fixed 60 Hz: the arena built from a modular kit, the traversal table
// (decks, crouch, jump, ladders and cargo nets, crawl tunnels, slides, rope bridges, ball pits), foam darts, the knife,
// balls (deadly until their first bounce), the one shared ball supply (pits, gutters, the conveyor, the lift, the cannon
// hoppers), the six cannons, score and the store, the vacuum, streak rewards, joke items, and the four modes.
// Every tuning number is in data/ballpit/ballpit_config.json.
// Frame: x runs the hall's length (team 0's tower at x < 0, team 1's at x > 0), y is up, z across. Levels: ground 0,
// level 2 at 3 m, level 3 at 6 m, the roof decks at 9 m.
#include "raylib.h"
#include "raymath.h"
#include <cstdint>
#include <string>
#include <vector>

namespace bp {

constexpr float STEP = 1.0f / 60;
constexpr int MAX_PLAYERS = 12;
constexpr float L2 = 3, L3 = 6, ROOF = 9;

// ---------------------------------------------------------------- the numbers
enum GunId { G_STARTER, G_PISTOL, G_REVOLVER, G_DUAL, G_PUMP, G_BURST, G_FLYWHEEL, G_CROSSBOW, G_BELT, G_LONG, G_ROCKET, G_COUNT };
struct GunDef {
    std::string key, name, role; int cost = 0; float dmg = 35; int pellets = 1; int mag = 1; float rate = 1, reload = 1, speed = 20, grav = 0.5f, spread = 1;
    int burst = 1; float spinUp = 0, slow = 1, knock = 0, splash = 0; int dartCost = 1; bool ads = true;
};
struct RewardDef { std::string key, name, text, counter; int streak = 300; float time = 0; };
struct JokeDef { std::string key, name, text; int cost = 5; };
struct ModeDef { std::string key, name, goal; int minP = 2, maxP = 12; float respawn = 5; int toWin = 25; float timeLimit = 600; bool teams = true; };
struct Config {
    // the player and the traversal table (spec "Movement and traversal")
    float height = 1.75f, crouchH = 1.1f, subH = 0.55f, radius = 0.3f, eyeFromTop = 0.12f, gravity = 9.81f;
    float run = 5.5f, crouch = 2.5f, jumpH = 1.0f, climb = 1.5f, tunnel = 2.0f, slide = 9.0f, bridge = 4.0f, wade = 2.0f, submerged = 1.0f, airControl = 0.35f, stepUp = 0.55f;
    float safeFall = 3.0f, fallDmgPerM = 30, bridgeSpread = 1.3f, flagSpeed = 0.9f, vacuumSpeed = 0.7f;
    // health
    float health = 100, regenAfter = 4, regenRate = 20, respawn = 5;
    // the knife
    float knifeDmg = 50, knifeCool = 0.6f, knifeReach = 2.0f;
    // balls
    float ballR = 0.075f, throwSpeed = 25, throwWindup = 0.3f, throwLoft = 3, grabTime = 0.4f, cannonSpeed = 40, ballGrav = 0.6f;
    int ballsTotal = 2600, hopperMax = 40; float conveyorTime = 8, cannonRate = 6, cannonYaw = 60, cannonPitch = 20, cannonMount = 0.5f, cannonHeat = 3, cannonCool = 2;
    // darts
    float dartFloorLife = 60; int dartCap = 400, dartStart = 12, dartMax = 60; float vacuumRange = 4, vacuumCone = 35, vacuumRate = 10;
    // score and store
    int scoreKO = 100, scoreCannonKO = 50, scoreAssist = 50, scoreCapture = 300, scoreReturn = 100, scorePlant = 200, scoreDefuse = 300;
    int dartPack = 12, dartPackCost = 50, disarmKit = 200; float storeSafe = 3, assistWindow = 5, jokeCool = 10;
    // the objectives
    float flagReturn = 20, plantTime = 4, fuse = 40, defuseTime = 6, defuseKit = 3, roundTime = 120; int bombRounds = 7, bombSwap = 6;
    std::vector<GunDef> guns; std::vector<RewardDef> rewards; std::vector<JokeDef> jokes; std::vector<ModeDef> modes;
    std::vector<std::string> dadJokes, prizes;
};
const Config& Cfg();
Config& CfgMutable();   // (tests tune a number, then put it back)

// ---------------------------------------------------------------- the arena (the modular kit, spec "Setting and arena")
// A solid is an axis-aligned box. What it stops is its material (the spec's cover table):
enum Mat : uint8_t { M_PANEL, M_NET, M_FRAME, M_PAD, M_DECK, M_SHELL, M_RAIL, M_WALL };
bool BlocksMove(int m);
bool BlocksShot(int m);     // darts and balls
bool BlocksSight(int m);
struct Solid { Vector3 lo, hi; uint8_t mat = M_PANEL; uint8_t colour = 0; };
struct Climb { Vector3 lo, hi; Vector3 into; bool net = true; };   // a ladder or cargo net: stand in front (lo..hi in x/z), push "into" to climb to hi.y
struct Slide { std::vector<Vector3> pts; bool spiral = false; uint8_t colour = 0; float len = 0; };   // feet path, top to exit
struct Tunnel { Vector3 lo, hi; };                                  // a crawl tunnel's inside (knife only in here)
struct Bridge { Vector3 lo, hi; bool netted = false; };             // a rope bridge or a net tunnel (its walking surface)
struct Pit { Vector3 lo, hi; int balls = 0, cap = 0; };            // floor at lo.y, the balls' surface at hi.y when full
struct CannonDef { Vector3 pivot; float yaw0; int team = -1; };     // team -1: the neutral pair on the central roof
struct Spot { Vector3 p; int team = -1; };
struct Arena {
    float halfX = 30, halfZ = 15, ceil = 14;
    std::vector<Solid> solids; std::vector<Climb> climbs; std::vector<Slide> slides; std::vector<Tunnel> tunnels; std::vector<Bridge> bridges;
    std::vector<Pit> pits; std::vector<CannonDef> cannons; std::vector<Spot> spawns, stores, flags, bombSites, gutters;
    Vector3 hub[2];                     // where the belts meet the lift pipes (one each long wall)
};
Arena MakeArena();

// ---------------------------------------------------------------- players
enum Posture : uint8_t { PO_STAND, PO_CROUCH, PO_CLIMB, PO_SLIDE, PO_CANNON, PO_DRIVE, PO_DEAD };
struct Input {
    float moveX = 0, moveZ = 0;          // the wish in the player's own frame (x forward, z right), length <= 1
    float yaw = 0, pitch = 0;            // the look (absolute)
    bool jump = false, crouch = false;   // jump: a press
    bool fire = false, aim = false;      // fire held; aim (right mouse)
    bool knife = false, reload = false, use = false, vacuum = false, grab = false;   // knife/reload/use/grab: presses; vacuum held
    int reward = -1;                     // use a ready streak reward this tick (its index 0..5), -1 none
    int slot = -1;                       // a weapon key this tick (0 starter, 1 bought gun), -1 none
    int buy = -1;                        // a store purchase this tick: 0..G_COUNT-1 guns, 100 darts, 101 the disarm kit, 200+ joke items
    int joke = -1;                       // use a joke item this tick (its index)
    int pickRewards = -1;                // the three chosen rewards as bits (the loadout screen), -1 none
};
struct Player {
    int id = 0, team = 0; bool present = true, bot = false; std::string name;
    Vector3 pos{}, vel{}; float yaw = 0, pitch = 0; bool grounded = true; float fallTop = 0;
    Posture po = PO_STAND; float poT = 0;
    int climb = -1, slide = -1; float slideS = 0;   // the ladder / slide in use, how far down the slide
    bool inPit = false, submerged = false, inTunnel = false, onBridge = false;
    float hp = 100, sinceHurt = 10, respawnT = 0; bool alive = true;
    // weapons: 0 the starter, 1 the bought gun (G_COUNT: none); the knife is always carried
    int gun = G_COUNT, wield = 0; int mag[2] = {1, 0}; int reserve = 12; float cool = 0, reloadT = 0, spin = 0, knifeT = 0, knifeSwing = 0; int burstLeft = 0; float burstT = 0;
    int dualSide = 0;
    float vacuumT = 0; bool vacuuming = false;
    int ball = -1; float grabT = 0, throwT = 0;      // the ball in hand; reaching into a pit; the throw's windup
    int cannon = -1; float mountT = 0;
    int carry = -1;                                  // a flag carried (its team), -1 none
    bool bomb = false, kit = false; float plantT = 0, defuseT = 0;
    // score: the leaderboard total, the wallet (spending never lowers the total), the streak since the last knockout
    int score = 0, cash = 0, streak = 0, kos = 0, deaths = 0, caps = 0;
    int rewardPick = 0b000111;                       // three of the six (bits)
    uint8_t rewardReady = 0, rewardUsed = 0;         // unlocked this life / spent this life (bits)
    float storeT = 0; bool inStore = false;          // seconds since arriving at a store (safe for the first storeSafe)
    float jokeCool = 0, fingerT = 0; bool crown = false, beanie = false; int prizes = 0; uint8_t jokes[16] = {};   // joke items in your pocket
    float swapT = 0;                                 // switching weapons
    int lastHitBy[2] = {-1, -1}; float lastHitT[2] = {-100, -100};   // for assists: the last two others who hurt you
    int drive = -1;                                  // the entity you're driving (RC car / tank)
    Input in;
    float Height() const;
    Vector3 Eye() const;
    Vector3 Look() const { return {cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw)}; }
};

// ---------------------------------------------------------------- darts, balls, the supply, cannons
struct Dart { Vector3 p{}, v{}; int owner = -1, team = -1; float dmg = 0, age = 0, grav = 0.5f, knock = 0, splash = 0; uint8_t kind = 0; bool live = true; };   // kind 0 dart, 1 mega dart, 2 rocket, 3 a turret's
enum BallState : uint8_t { BS_LIVE, BS_LOOSE, BS_HELD };
struct Ball { Vector3 p{}, v{}; BallState st = BS_LOOSE; int holder = -1, thrower = -1, team = -1; bool fromCannon = false; float age = 0; };
struct Cannon { int op = -1; int hopper = 40; float heat = 0, coolT = 0, shotT = 0, yaw = 0, pitch = 0; bool overheated = false; };
struct Conveyor { std::vector<float> arrive; };   // each ball on the belts, by when it reaches the lift

// ---------------------------------------------------------------- reward entities and joke props
enum EntKind : uint8_t { E_HEALTHBOX, E_KID, E_RCCAR, E_DRONE, E_TANK, E_CHICKEN, E_CUSHION, E_CONFETTI };
struct Ent { uint8_t kind = 0; int owner = -1, team = -1; Vector3 p{}, v{}; float yaw = 0, hp = 0, life = 0, cool = 0, t = 0; int uses = 0; Vector3 goal{}; float armor = 0; bool dead = false; };
struct Flag { Vector3 home{}, p{}; int carrier = -1; bool home_ = true; float dropT = 0; };
struct Bomb { Vector3 p{}; int carrier = -1; bool planted = false, done = false; int site = -1; float fuseT = 0; };

// ---------------------------------------------------------------- the world and the match
struct Event { int kind; Vector3 at; int who, by; float a; };
enum EventKind { EV_SHOT = 1, EV_HIT, EV_KO, EV_THROW, EV_BOUNCE, EV_GRAB, EV_KNIFE, EV_CANNON, EV_OVERHEAT, EV_BUY, EV_VACUUM, EV_RESPAWN,
                 EV_STREAK, EV_REWARD, EV_JOKE, EV_FLAG_TAKE, EV_FLAG_DROP, EV_FLAG_RETURN, EV_CAPTURE, EV_PLANT, EV_DEFUSE, EV_BOOM,
                 EV_ROUND, EV_SLIDE, EV_SPLASH, EV_ROCKET, EV_DRY, EV_RELOAD, EV_LAND, EV_ASSIST, EV_HOPPER, EV_CLIMB };
enum Phase : uint8_t { PH_WARMUP, PH_PLAY, PH_ROUND_END, PH_OVER };
enum ModeId { MD_FFA, MD_TDM, MD_CTF, MD_BOMB, MD_COUNT };
struct World {
    Arena arena; std::vector<Player> players; std::vector<Dart> darts; std::vector<Ball> balls; std::vector<Cannon> cannons;
    Conveyor belt; std::vector<Ent> ents; Flag flags[2]; Bomb bomb; std::vector<Event> events;
    int mode = MD_TDM; float t = 0, phaseT = 0; uint32_t rng = 1; Phase phase = PH_WARMUP;
    int teamKOs[2] = {0, 0}, caps[2] = {0, 0}, round = 0, roundWins[2] = {0, 0}, attackers = 0, winner = -1, roundWinner = -1;   // winner: a team, or a player id in free for all
    bool noRespawn = false; uint32_t evCount = 0; int jokeLine = -1;
    void Init(int mode, int players, uint32_t seed);
    void Step();                         // one fixed step (every player's Input set first)
    float Rand();
    // pieces (ballpit.cpp)
    void StepPlayer(Player& p);
    void StepWeapons(Player& p);
    void StepBallHands(Player& p);
    void StepDarts();
    void StepBalls();
    void StepSupply();
    void StepCannon(Player& p);
    void StepStore(Player& p);
    void StepRewards(Player& p);
    void StepEnts();
    void StepMode();
    void Hurt(Player& v, float dmg, int by, const char* how, bool cannon = false, bool ko = false);
    void KnockOut(Player& v, int by, const char* how, bool cannon);
    void Respawn(Player& p);
    void NewRound();
    void FireDart(Player& p, const GunDef& g, Vector3 from, Vector3 dir);
    void ThrowBall(Player& p);
    void FireCannon(int c);
    void ToBelt(int n);
    void Emit(int kind, Vector3 at, int who = -1, int by = -1, float a = 0) { evCount++; events.push_back({kind, at, who, by, a}); if (events.size() > 400) events.erase(events.begin(), events.begin() + 200); }
    // queries
    float GroundAt(Vector3 p, float r, float maxUp) const;     // the highest walkable top under a circle (0: the hall floor)
    bool Ray(Vector3 o, Vector3 d, float maxT, float* tHit, int what) const;   // what: 0 movement, 1 shots, 2 sight
    bool Sees(Vector3 a, Vector3 b) const { Vector3 d = Vector3Subtract(b, a); float L = Vector3Length(d); return L < 1e-3f || !Ray(a, Vector3Scale(d, 1 / L), L, nullptr, 2); }
    bool ShotClear(Vector3 a, Vector3 b) const { Vector3 d = Vector3Subtract(b, a); float L = Vector3Length(d); return L < 1e-3f || !Ray(a, Vector3Scale(d, 1 / L), L, nullptr, 1); }
    int PitAt(Vector3 p, float pad = 0) const;
    int StoreNear(Vector3 p) const;
    int CannonNear(Vector3 p) const;
    Vector3 CannonMuzzle(int c) const;
    Vector3 CannonSeat(int c) const;
    int BallsTotal() const;             // pits + loose/live/held + belt + hoppers (the shared-supply rule: constant)
    bool Enemies(const Player& a, const Player& b) const { return a.id != b.id && (mode == MD_FFA || a.team != b.team); }
    bool Hittable(const Player& p) const;   // not sliding, not submerged, alive, not in the store's safe seconds
    const GunDef& Gun(const Player& p) const;   // the gun in hand
    int Leader() const;                  // free for all: the top player
};

// the bots (ballpit_bots.cpp)
void BotInput(World& w, int me, Input& in, uint32_t& rng, int skill);
void BotGoto(World& w, int me, Vector3 goal, Input& in);   // walk a player to a spot by the graph (tests, the tutorial)
struct Nav;                              // the navigation graph (built once per arena)
const Nav& NavOf(const Arena& a);
int RunBallPitTest();                    // depth.exe --ballpit-test (the milestones' acceptance tests)
int RunBallPitSim(int matches, int mode, int players);   // depth.exe --ballpit-sim
const char* ModeName(int m);
const char* RewardName(int r);

}  // namespace bp
