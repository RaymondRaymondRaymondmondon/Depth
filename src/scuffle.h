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
// the pieces a stage is built from (doc pp. 10-11, 17): platforms of stone (the Nautilus's steel), wood, ice (slippery),
// glass (breaks under weight), rope (one-way: you land on it from above, drop through holding down), electrified rails
// (live on a rhythm), conveyors; and the moving and hazardous pieces below
// (stage 5, the other worlds: crumbling floors, the Cave's crystal, the Reef's urchins, water and the Void's brine (not
// solid: you swim, and drown), and the Salon's bar counter)
enum Tile : uint8_t { T_EMPTY, T_STONE, T_WOOD, T_ICE, T_GLASS, T_ROPE, T_RAIL, T_CONV_L, T_CONV_R,
                      T_CRUMBLE, T_CRYSTAL, T_URCHIN, T_WATER, T_BRINE, T_BAR, T_COUNT };
// the pieces of all six worlds (doc pp. 9-11); `start` holds a piece asleep until that second (the finales' set pieces)
enum PieceKind : uint8_t { PK_PISTON, PK_ELEVATOR, PK_VENT, PK_PROPELLER, PK_TUBE, PK_WINDOW,
                           PK_STALACTITE, PK_DRIP, PK_STREAM, PK_TOAD, PK_CLAW,                    // the Cave (the stream is also the Reef's surge)
                           PK_REACHER, PK_EEL, PK_SHARK, PK_KRAKEN,                              // the Reef
                           PK_GRATE, PK_TUNA, PK_SLUICE, PK_COLUMN,                              // Atlantis
                           PK_LURE, PK_LOWG, PK_WORM,                                            // the Void
                           PK_CROWD, PK_DART, PK_POOL, PK_BOUNCER, PK_DOG,                       // the Salon
                           PK_COUNT };
const char* PieceName(int kind);
struct Piece;
void PieceDefaults(Piece& p);
const char* TileName(int t);
struct Piece {
    uint8_t kind = PK_PISTON; int x = 0, y = 0, w = 1, h = 1, dx = 0, dy = 1;   // tiles; (dx, dy): which way it moves or faces
    float travel = 3, period = 3, phase = 0, on = 0.8f, power = 1;               // tiles, seconds, 0..1, seconds, a multiplier
    Vector2 off{}, prevOff{};                                                     // (runtime: a mover's offset in metres)
    float start = 0;                                                              // (asleep until this second)
    float cool = 0; bool broken = false;
    float prog = 0; int hold = -1;                                                // (runtime: a fall, a lunge, a strike's progress; a stick held)
};
enum World6 { WD_NAUTILUS, WD_CAVE, WD_REEF, WD_ATLANTIS, WD_VOID, WD_SALON, WD_COUNT };
const char* WorldName(int w);
struct Stage {
    int w = 32, h = 18; std::vector<uint8_t> t;           // row-major, row 0 at the bottom
    std::vector<Vector2> spawns;                           // in metres (feet)
    std::vector<Piece> pieces; std::vector<int> crateCols; // (crate zones: columns; empty = anywhere with a floor)
    std::string name = "Stone", author = "Depth"; int world = WD_NAUTILUS; bool wrap = false, finale = false;
    uint8_t At(int x, int y) const { if (wrap) x = ((x % w) + w) % w; return x < 0 || y < 0 || x >= w || y >= h ? T_EMPTY : t[y * w + x]; }
    bool Solid(int x, int y) const { uint8_t k = At(x, y); return k != T_EMPTY && k != T_ROPE && k != T_WATER && k != T_BRINE; }   // (rope is one-way: see OneWay)
    bool Liquid(int x, int y) const { uint8_t k = At(x, y); return k == T_WATER || k == T_BRINE; }
    bool OneWay(int x, int y) const { return At(x, y) == T_ROPE; }
    void Set(int x, int y, uint8_t k) { if (x >= 0 && y >= 0 && x < w && y < h) t[y * w + x] = k; }
    float Width() const { return w * TILE; }
    float Height() const { return h * TILE; }
};
// the editor's text form (one char a tile; pieces by letter; a parameter line per piece is optional) and its share codes
std::string StageToCode(const Stage& s);                   // "SCF1-..." (compressed, base64: fits a chat line)
bool StageFromCode(const std::string& code, Stage& out, std::string* err = nullptr);
std::string PackToCode(const std::vector<Stage>& pack);    // up to 20 stages: "SCP1-..."
bool PackFromCode(const std::string& code, std::vector<Stage>& out, std::string* err = nullptr);
std::vector<std::string> StageToText(const Stage& s);      // the editor's text form (rows, top first, then piece lines)
// the check (doc p. 17): every spawn can reach every other with the real movement code
struct Reach { bool ok = false; int spawns = 0, pairsFailed = 0, nodes = 0; std::string why; };
Reach CheckReachable(const Stage& s);
std::vector<Stage> LoadWorldPack(int world);               // the built-in stages (data/scuffle/stages/<world>.txt)
std::vector<Stage> BuildNautilus();                        // (the builder behind nautilus.txt; --scuffle-build-packs writes it)
std::vector<Stage> BuildWorld(int world);                  // a world's forty and its three finales (scuffle_build.cpp)
Stage GenerateStage(int world, uint32_t seed, bool finale = false, int* tries = nullptr);   // the generator (doc p. 11): checked with the real movement code
int RunScuffleBuildPacks();
int RunScuffleVerify(const std::string& codeOrAll);       // --scuffle-verify <code> | --scuffle-verify-all
Stage StoneStage();                                        // stage 1's one stone stage (the doc's gate)
Stage StageFromText(const std::vector<std::string>& rows, const char* name);   // '#' stone, 'S' a spawn (top row first); see scuffle_stage.cpp for every letter

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
struct ArmsTuning { float crateFirst = 3, crateEvery = 5, chuteFall = 2, crush = 30, crateThrown = 20, emptyThrow = 10, blockWindow = 0.15f, wallStart = 45, wallCenter = 60, wallAll = 70, finaleWall = 30, gearChance = 0.1f, trapChance = 0.033f; };
const ArmsTuning& Arms();

// ---------------------------------------------------------------- input (all play, bots too, goes through this)
struct Input {
    float moveX = 0, moveY = 0;          // the stick (-1..1); moveY < 0 is down (duck, dive)
    Vector2 aim{1, 0};                   // where the free arm points (a unit vector)
    bool jump = false, fire = false;     // held
    bool taunt = false;
    bool gear = false;                   // (stage 6: the gear button, held)
};
// gear (doc p. 14): one crate in ten; a second slot used with the gear button, kept for the round
enum Gear { GR_HOOK, GR_SHIELD, GR_JETPACK, GR_DECOY, GR_PARACHUTE, GR_SPRING, GR_ROPE, GR_COUNT };
const char* GearName(int g);

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
    float swimT = 0, hazT = 0, sharkT = 0; bool wet = false;          // (stage 5: seconds with the head under water; a hazard's cooldown on this stick; swimming)
    // stage 6: what the strange weapons do to a stick (seconds left), the gear slot and its state
    float burnT = 0, frozenT = 0, bubbleT = 0, netT = 0, gravT = 0, trapT = 0;
    int gear = -1; float gearFuel = 3, gearCool = 0; Vector2 hook{}; bool hookOn = false, gearWas = false;
    // the round's story
    int kills = 0; int lastHitBy = -1; float lastHitT = -10; std::string cause;
    Input in;
};

// ---------------------------------------------------------------- the world (one round on one stage)
struct Event { int kind = 0; Vector2 at{}; float a = 0; int who = -1, by = -1; };   // (the scene's sounds and splashes)
enum EventKind { EV_PUNCH = 1, EV_HIT, EV_HAYMAKER, EV_KICK, EV_JUMP, EV_LAND, EV_DIE, EV_THROW, EV_GRAB, EV_BONK, EV_FALL_OUT,
                 EV_SHOT, EV_EXPLODE, EV_BLOCK, EV_CRATE_OPEN, EV_PICKUP, EV_EMPTY, EV_CHUTE, EV_SWING, EV_CRUSH, EV_WALL,
                 EV_ZAP, EV_FREEZE, EV_BURN, EV_BUBBLE, EV_PORTAL, EV_SNAKE, EV_BEES, EV_GEAR, EV_INK, EV_FLASH, EV_TRAP, EV_EVENT };
// stage 6: the things the strange weapons leave in the world (doc pp. 6-8, 14): a swarm of bees, a snake, fish come to
// chum, a black hole, chum in the water, a portal, a bear trap, a turret, a mine, a banana peel, a decoy, a spring, a
// stuck charge, and a beam (the laser's and the tesla's, for the drawing)
enum ThingKind : uint8_t { TH_SWARM, TH_SNAKE, TH_FISH, TH_HOLE, TH_CHUM, TH_PORTAL, TH_TRAP, TH_TURRET, TH_MINE, TH_PEEL, TH_DECOY, TH_SPRING, TH_STUCK, TH_BEAM, TH_COUNT };
struct Thing {
    uint8_t kind = TH_SWARM; bool alive = true;
    Vector2 p{}, v{}, q{};                                // (position, velocity; q: a beam's far end, a portal's facing)
    float life = 5, t = 0, a = 0, cool = 0;               // (seconds left; age; a kind's number: a portal's pair index, a charge's fuse)
    int owner = -1, on = -1, hold = -1, weapon = -1;      // (who made it; the stick it's on; the stick it holds; the weapon behind it)
};
// a crate on its parachute, or a weapon lying loose (a weapon in a hand is drawn from the hand: Stick::weapon)
struct Item {
    bool alive = true, crate = false, chute = false, shot = false; int weapon = -1, ammo = 0, holder = -1, count = 0;
    Particle a{}, b{};                                    // crate: a is the centre; weapon: a the grip, b the muzzle
    float age = 0, thrownT = 0; int thrownBy = -1;
};
struct Bullet {
    Vector2 p{}, v{}; int owner = -1, weapon = -1, pierce = 1, bounces = 0; float dmg = 0, knock = 0, life = 3, grav = 0, area = 0, areaDmg = 0, fuse = 0, age = 0;
    bool explode = false, alive = true, deflected = false; std::vector<int> hit;
    int hazard = -1;                                      // (a stage's hazard fired it: its piece kind; -2 crystal shrapnel)
};
struct World {
    Stage stage; std::vector<Stick> sticks; uint32_t rng = 1; uint32_t frame = 0; float t = 0;
    std::vector<Event> events; uint32_t eventBase = 0;     // (a numbered log the scene reads by cursor)
    float gravity = 30;
    // arms (scuffle_arms.cpp): crates, loose weapons, bullets; the round's clock and the wall
    std::vector<Item> items; std::vector<Bullet> bullets; float nextCrate = 3, wallY = -10; int arsenal = AR_CLASSIC; bool finale = false, wallOn = true;
    int crates = 0;
    float ceilY = 1e9f, sideX = -10; int wallSide = 1;     // (the other worlds' walls: the Cave's ceiling coming down; the Void's abyss and the Salon's bouncer from a side)
    // stage 6 (scuffle_special.cpp): the strange weapons' things, burning tiles, the screen's ink and flash
    std::vector<Thing> things; std::vector<float> fireT; float inkT = 0, flashT = 0;
    void StepThings();
    void StepStatus(Stick& k);                             // (burning, frozen, bubbled, netted, flipped, trapped)
    bool SpecialHit(Bullet& b, Vector2 at, Stick* k);      // (a special bullet's impact on a stick or a tile: true if it's spent)
    void SpecialFire(Stick& k, const WeaponDef& d, Item& it, Vector2 aim);   // (the beams and the thrown things)
    void StepGear(Stick& k);
    void Burn(Stick& k, float s, int by);
    void Freeze(Stick& k, float s, int by);
    void Zap(Vector2 from, Stick& first, float dmg, int by, int weapon);
    int AddThing(int kind, Vector2 p, Vector2 v, float life, int owner);
    void Snakes(Vector2 at, int n, int owner);
    bool InLiquid(Vector2 p) const;                        // (water, brine, the tide, a live sluice)
    bool BrineAt(Vector2 p) const;
    float GravityAt(Vector2 p) const;                      // (low gravity pockets)
    void StepHazard(Piece& p);                             // the other worlds' pieces (scuffle_hazards.cpp)
    void Nudge(Stick& k, Vector2 d);                       // (a push that respects the tiles: currents, the lure)
    bool ShotAt(Vector2 at, float radius);                 // (gunfire or a blast: stalactites fall, columns topple, crystal shatters; true if it struck one)
    int HazardBullet(int kind, Vector2 at, Vector2 v, float dmg, float knock, float grav);
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
    bool BoxHits(float x0, float y0, float x1, float y1) const;   // the box overlaps a solid tile or a mover
    bool RopeUnder(float x0, float x1, float yOld, float yNew) const;   // a one-way tile's top between the box's old and new bottom
    void StepPieces();                                     // movers, vents, rails, the propeller, tubes, glass (scuffle_pieces.cpp)
    bool RailLive(int x, int y) const;
    bool MoverHits(float x0, float y0, float x1, float y1, int* which = nullptr) const;
    std::vector<float> glassT;                             // (weight on each glass tile: it breaks at 0.6 s)
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
    std::vector<Stage> custom;                             // (a playlist of the group's own: the editor's "play now", a pasted pack)
    int world = -1;                                        // (the lobby's world: -1 all six; WD_COUNT the generator, endless)
    std::vector<Stage> finales;                            // (the match point's stages: the playlist's worlds' finales)
    void Start(int nPlayers, int roundsToWin, uint32_t seed, int arsenal = AR_CLASSIC);
    void NewRound();
    void Step();                                           // one fixed step: the phases, and the world while fighting
    bool Over() const { return phase == P_OVER; }
};
std::vector<Stage> StagePlaylist(int world = -1);           // a world's forty (-1: all six worlds); the stone stages if no pack is found
std::vector<Stage> FinalePlaylist(int world = -1);

int RunScuffleTest();                                      // --scuffle-test
int RunScuffleDeterminism(uint32_t seed);                  // --scuffle-determinism <seed>
int RunScuffleSim(int players, int rounds, int arsenal = AR_CLASSIC);   // --scuffle-sim <players> <rounds> [arsenal]: bots play a match

} // namespace sf
