#pragma once
// ============================================================================
//  SCUFFLE - arcade game 9 of the Deep Arcade (Reference_For_Future_MP_Games/Scuffle - Arcade Game 9 Design Document
//  (a Stick Fight game).pdf; its OCR is in docs/scuffle_pdf_pages/). A 2D physics brawler for 2-8 players: stick figures
//  fight on small, dangerous one-screen stages with whatever falls from the sky; the last one standing wins the round.
//
//  Headless (no drawing; scuffle_game.cpp is the scene). Units are metres and seconds, y up, the stage's bottom-left
//  corner at the origin. The physics is ours (the doc allows "written in the codebase"): a position-based particle
//  engine at a fixed 120 Hz, float math in a fixed order, so the same inputs give the same world (--scuffle-determinism).
//
//  A stick is an active ragdoll in two halves: a movement controller (a box that runs, jumps, climbs and collides with
//  the tiles like a platformer body: it's what makes the stick controllable) and a stick-figure ragdoll of eleven
//  particles held together by bones and pulled toward an animated pose around the controller (it's what makes the stick
//  funny). Hits push both; a big hit, a stun or death lets go of the pose and the ragdoll flies on its own; the
//  controller then follows the ragdoll's pelvis until the stick gets up.
// ============================================================================
#include "raylib.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace sf {

constexpr float TILE = 0.6f;                 // a tile is about a stick's torso (doc p. 17)
constexpr float STEP = 1.0f / 120;           // the physics' fixed step (doc p. 21)
constexpr int MAX_STICKS = 8;

// ---------------------------------------------------------------- the stage
enum Tile : uint8_t { T_EMPTY, T_STONE, T_COUNT };
struct Stage {
    int w = 32, h = 18; std::vector<uint8_t> t;           // row-major, row 0 at the bottom
    std::vector<Vector2> spawns;                           // in metres (feet)
    std::string name = "Stone";
    uint8_t At(int x, int y) const { return x < 0 || y < 0 || x >= w || y >= h ? T_EMPTY : t[y * w + x]; }
    bool Solid(int x, int y) const { return At(x, y) != T_EMPTY; }
    float Width() const { return w * TILE; }
    float Height() const { return h * TILE; }
};
Stage StoneStage();                                        // stage 1's one stone stage (the doc's gate)
Stage StageFromText(const std::vector<std::string>& rows, const char* name);   // '#' stone, 'S' a spawn (top row first)

// ---------------------------------------------------------------- the particles
struct Particle { Vector2 p{}, q{}; float r = 0.1f, invMass = 1; };   // position, last position (velocity is p - q)
struct Bone { int a = 0, b = 0; float len = 0, stiff = 1; };

// ---------------------------------------------------------------- the arsenal (data/scuffle/scuffle_weapons.json; doc pp. 5-9)
struct WeaponDef {
    std::string key, name, kind, special, wrong;      // kind: gun, melee, thrown
    int stage = 2, ammo = 0, pellets = 1, pierce = 1, bounce = 0, count = 0;
    float dmg = 0, rate = 1, knock = 0, recoil = 0, speed = 0, spread = 0, head = 1, gravity = 0, area = 0, areaDmg = 0, fuse = 0;
    float swing = 0.3f, reach = 0.8f, throwDmg = 0, range = 0, spinup = 0;
    bool hold = false, twin = false, pin = false, deflect = false, blocks = false;
};
const std::vector<WeaponDef>& Weapons();
int WeaponIndex(const std::string& key);
enum Arsenal { AR_CLASSIC, AR_MELEE, AR_CHAOS, AR_SNAKES, AR_RANDOM, AR_COUNT };
const char* ArsenalName(int a);
struct ArmsTuning { float crateFirst = 3, crateEvery = 5, chuteFall = 2, crush = 30, crateThrown = 20, emptyThrow = 10, blockWindow = 0.15f, wallStart = 45, wallCenter = 60, wallAll = 70, finaleWall = 30; };
const ArmsTuning& Arms();

// ---------------------------------------------------------------- input (all play, bots too, goes through this)
struct Input {
    float moveX = 0, moveY = 0;          // the stick (-1..1); moveY < 0 is down (duck, dive)
    Vector2 aim{1, 0};                   // where the free arm points (a unit vector)
    bool jump = false, fire = false;     // held
    bool taunt = false;
};

// ---------------------------------------------------------------- a stick
enum StickState : uint8_t { S_STAND, S_AIR, S_WALL, S_DUCK, S_DIVE, S_PRONE, S_RAGDOLL, S_DEAD };
enum Joint : uint8_t { J_HEAD, J_NECK, J_PELVIS, J_ELBOW_L, J_HAND_L, J_ELBOW_R, J_HAND_R, J_KNEE_L, J_FOOT_L, J_KNEE_R, J_FOOT_R, J_COUNT };
struct Stick {
    int id = 0; bool alive = true, present = true; float hp = 100;
    // the controller: feet position, velocity, the box's half width and height
    Vector2 pos{}, vel{}; float halfW = 0.24f, height = 1.7f;
    StickState st = S_AIR; int face = 1;                   // 1 right, -1 left
    bool grounded = false, wallLeft = false, wallRight = false; int wallSide = 0;
    float climbT = 0, coyoteT = 0, jumpHeldT = 0, stunT = 0, ragT = 0, getUpT = 0, fallTop = 0, proneT = 0, hitT = 0, knockT = 0, grabWait = 0;
    bool jumpWas = false, fireWas = false;
    // the ragdoll
    std::array<Particle, J_COUNT> pt{};
    float stiff = 1;                                       // the pose's pull (0 limp .. 1 in control)
    // fists (doc p. 3): the jab (and the haymaker every third), the block window, the grab and the throw, the kick
    float punchT = 0, punchCool = 0, comboT = 0; int combo = 0; bool punchHay = false; int punchArm = 0; std::vector<int> punchHit;
    int grabbing = -1, grabbedBy = -1; float holdT = 0, thrownT = 0; int thrownBy = -1;
    float kickT = 0; float tauntT = 0;
    // arms: the weapon in hand (an index into World::items, -1 fists), the trigger's cooldown, the minigun's spin, a melee swing
    int weapon = -1; float fireCool = 0, spin = 0, swingT = 0; bool swingHay = false; std::vector<int> swingHit; float blockT = 0;
    int killsBy[4] = {};                                   // (unused yet: kill kinds for the scoring)
    float walkPh = 0, breathe = 0;
    // the round's story
    int kills = 0; int lastHitBy = -1; float lastHitT = -10; std::string cause;
    Input in;
};

// ---------------------------------------------------------------- the world (one round on one stage)
struct Event { int kind = 0; Vector2 at{}; float a = 0; int who = -1, by = -1; };   // (the scene's sounds and splashes)
enum EventKind { EV_PUNCH = 1, EV_HIT, EV_HAYMAKER, EV_KICK, EV_JUMP, EV_LAND, EV_DIE, EV_THROW, EV_GRAB, EV_BONK, EV_FALL_OUT,
                 EV_SHOT, EV_EXPLODE, EV_BLOCK, EV_CRATE_OPEN, EV_PICKUP, EV_EMPTY, EV_CHUTE, EV_SWING, EV_CRUSH, EV_WALL };
// a crate on its parachute, or a weapon lying loose (a weapon in a hand is drawn from the hand: Stick::weapon)
struct Item {
    bool alive = true, crate = false, chute = false, shot = false; int weapon = -1, ammo = 0, holder = -1, count = 0;
    Particle a{}, b{};                                    // crate: a is the centre; weapon: a the grip, b the muzzle
    float age = 0, thrownT = 0; int thrownBy = -1;
};
struct Bullet {
    Vector2 p{}, v{}; int owner = -1, weapon = -1, pierce = 1, bounces = 0; float dmg = 0, knock = 0, life = 3, grav = 0, area = 0, areaDmg = 0, fuse = 0, age = 0;
    bool explode = false, alive = true, deflected = false; std::vector<int> hit;
};
struct World {
    Stage stage; std::vector<Stick> sticks; uint32_t rng = 1; uint32_t frame = 0; float t = 0;
    std::vector<Event> events; uint32_t eventBase = 0;     // (a numbered log the scene reads by cursor)
    float gravity = 30;
    // arms (scuffle_arms.cpp): crates, loose weapons, bullets; the round's clock and the wall
    std::vector<Item> items; std::vector<Bullet> bullets; float nextCrate = 3, wallY = -10; int arsenal = AR_CLASSIC; bool finale = false, wallOn = true;
    int crates = 0;
    void StepArms();
    void StepCrates();
    void StepItems();
    void StepBullets();
    void StepWall();
    void Fire(Stick& k);
    void Explode(Vector2 at, float radius, float dmg, float knock, int owner, int weapon);
    int DropCrate(float x);
    int SpawnWeapon(int weapon, Vector2 at, Vector2 vel);
    void Pickup(Stick& k, int item);
    void DropWeapon(Stick& k, Vector2 vel, bool thrown);
    int RollWeapon();
    bool LineOfSight(Vector2 a, Vector2 b) const;
    void Init(const Stage& s, int players, uint32_t seed);
    void Step();                                           // one fixed step (STEP seconds)
    float Rand();
    int Living() const;
    uint64_t Hash() const;                                 // every body's state (--scuffle-determinism)
    // pieces
    void SpawnStick(Stick& k, Vector2 feet, int face);
    void StepController(Stick& k);
    void StepPose(Stick& k);
    void StepParticles();
    void StepFists(Stick& k);
    void Hit(Stick& target, int by, float dmg, Vector2 dir, float knock, bool ragdoll, const char* cause);
    void Kill(Stick& k, int by, const char* cause);
    void Emit(int kind, Vector2 at, int who = -1, int by = -1, float a = 0);
    bool BoxHits(float x0, float y0, float x1, float y1) const;   // the box overlaps a solid tile
};
Vector2 PoseOffset(const Stick& k, int joint, float t);    // where a joint wants to be, relative to the feet

// a fists-only bot (stage 1): the nearest living stick, closing, punching, jumping gaps and walls, not walking off edges
void BotInput(const World& w, int me, Input& in, uint32_t& rng, int skill);

// ---------------------------------------------------------------- the match (doc p. 4): rounds on a stage rotation, first to N
struct Match {
    World w; int players = 4, toWin = 10, round = 0, arsenal = AR_CLASSIC; uint32_t seed = 1;
    std::vector<int> wins, score, roundKills; std::vector<Stage> playlist; int stageIdx = 0;
    enum Phase { P_COUNT, P_FIGHT, P_WIN, P_OVER } phase = P_COUNT; float phaseT = 1; int roundWinner = -1, champion = -1, draws = 0;
    std::vector<std::string> log;                          // (the round's story lines)
    uint32_t evSeen = 0;
    void Start(int nPlayers, int roundsToWin, uint32_t seed, int arsenal = AR_CLASSIC);
    void NewRound();
    void Step();                                           // one fixed step: the phases, and the world while fighting
    bool Over() const { return phase == P_OVER; }
};
std::vector<Stage> StagePlaylist();                        // stage 2: the stone stages

int RunScuffleTest();                                      // --scuffle-test
int RunScuffleDeterminism(uint32_t seed);                  // --scuffle-determinism <seed>
int RunScuffleSim(int players, int rounds, int arsenal = AR_CLASSIC);   // --scuffle-sim <players> <rounds> [arsenal]: bots play a match

} // namespace sf
