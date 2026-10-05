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
Stage GenerateStage(int world, uint32_t seed, bool finale = false, int* tries = nullptr, int boost = 0);   // (boost: extra hazards: the Gauntlet's stages get worse)
Stage MirrorStage(const Stage& s);                         // (left for right: the Mirror mutator, the packs' variants)   // the generator (doc p. 11): checked with the real movement code
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
    float swing = 0.3f, reach = 0.8f, throwDmg = 0, range = 0, spinup = 0, scope = 0;   // (scope: seconds between the trigger and the shot: the sniper's glint)
    bool hold = false, twin = false, pin = false, deflect = false, blocks = false, support = false;   // (support: a utility item that can't win a duel alone: the ink bomb, the portal gun)
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
    int pick = 0;                        // (stage 7: the Duel's weapon pick, 1-3, while the round counts down)
};
// gear (doc p. 14): one crate in ten; a second slot used with the gear button, kept for the round
enum Gear { GR_HOOK, GR_SHIELD, GR_JETPACK, GR_DECOY, GR_PARACHUTE, GR_SPRING, GR_ROPE, GR_MIRROR, GR_BALLOON, GR_FISHBOWL, GR_COUNT };
// the modes (doc pp. 15-16; Boss Arena is stage 8)
enum Mode { MD_CLASSIC, MD_TEAMS, MD_KING, MD_EGG, MD_POTATO, MD_HUNT, MD_DUEL, MD_CHAOS, MD_CUSTOM, MD_GAUNTLET, MD_BOSS, MD_COUNT };
// Boss Arena (stage 8, scuffle_boss.cpp): one of six Depth bosses on its own stage, three phases each, the stage changing with them
enum BossKind { BK_LOBSTER, BK_KRAKEN, BK_WYRM, BK_SUN_GOD, BK_GOLIATH, BK_BOUNCER, BK_COUNT };
const char* BossName(int b);
int BossOfWorld(int world);                                // (each world has its boss: the stage picker picks the boss)
struct BossPart { Vector2 p{}; float r = 0.3f, mul = 1, hurt = 0; };   // (a hit circle: mul is what a blow does to it (0.25 armour, 1 body, 2 the weak point); hurt is what it does to a stick it touches)
struct Boss {
    int kind = -1; float hp = 0, maxHp = 0; int phase = 0; bool dead = false;
    float t = 0, actT = 0, next = 2, hitT = 0, roarT = 0, deadT = 0;   // (the boss's clock; time in the current attack; until the next; a hit's flash; a phase change's roar)
    int act = 0, target = -1, face = -1, side = 1, count = 0;
    Vector2 pos{}, at{}, from{};                              // (where it stands; the attack's mark; where a move began)
    float s[8] = {};                                         // (each boss's own numbers: a claw's sweep, an eye's rise, the inhale)
    std::vector<BossPart> parts;                             // (rebuilt from the pose every step)
    std::vector<Vector2> chain;                              // (a tentacle's or the wyrm's body)
    std::vector<Vector3> marks;                              // (falling things' tells: x, y, seconds left)
    std::vector<Rectangle> danger;                           // (where it's about to hurt: the bots step out)
    float dmgBy[MAX_STICKS] = {};
};
const char* ModeName(int m);
const char* ModeRule(int m);
// bots' personalities (doc p. 16): a plain bot, one that always rushes, one that camps crates, one that only uses melee, one that taunts
enum Persona { PE_PLAIN, PE_RUSHER, PE_CAMPER, PE_MELEE, PE_TAUNTER, PE_COUNT };
const char* GearName(int g);
// trinkets (doc pp. 11-12): one per stick for the match, an edge with a cost, open for everyone to see
enum Trinket { TK_NONE, TK_SPRING_HEELS, TK_THICK_SKULL, TK_LUCKY_CRATE, TK_LONG_ARMS, TK_MAGNET_PALMS, TK_CAT_LEGS, TK_BIG_LUNGS, TK_QUICK_DRAW,
               TK_PACK_RAT, TK_THICK_COAT, TK_LOUD_MOUTH, TK_SECOND_WIND, TK_SNAKE_CHARMER, TK_DEADWEIGHT, TK_COUNT };
const char* TrinketName(int t);
const char* TrinketText(int t);                            // (the edge, and the cost)
// mutators (doc pp. 12-13): lobby rules that stack (a bit each); Random picks one a round
enum Mutator { MU_LOW_GRAVITY, MU_MOON_SHOT, MU_RICOCHET, MU_BIG_HEADS, MU_INFINITE_AMMO, MU_ONE_HIT, MU_RAGDOLL_ROYALE, MU_SNAKES, MU_HOT_POTATO,
               MU_BLACKOUT, MU_GIANTS, MU_TINY, MU_MIRROR, MU_VAMPIRE, MU_SUDDEN_WALL, MU_PACIFIST, MU_FAST_FORWARD, MU_COUNT };
const char* MutatorName(int m);
const char* MutatorText(int m);
// mid-round events (doc pp. 13-14): one round in four, at a random second from 10 to 30, after a one-second tell
enum RoundEvent { RE_FLOOD, RE_REACH, RE_LIGHTS_OUT, RE_CRATE_RAIN, RE_EARTHQUAKE, RE_SWAP, RE_DOG, RE_GRAVITY_FLIP, RE_BOUNCER, RE_FISH_STORM, RE_COUNT };
const char* EventName(int e);

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
    float aimT = 0;                                        // (a scoped shot on its way: it fires when this runs out)
    float steadyT = 0;                                     // (just back on your feet: no ragdoll for a moment, so nothing can keep you down)
    // stage 7: the modes
    int team = -1; bool shark = false; float respawnT = 0, finished = -1, balloonT = 0; int persona = PE_PLAIN;
    int skin = -1, hat = -1;                               // (stage 9: the cosmetics: indices into Skins() and Hats(), -1 none; nothing changes a stat)
    int trinket = TK_NONE; float size = 1; bool airJump = false, windUsed = false; int carry = -1;   // (stage 6b: the trinket; Giants and Tiny; the Spring Heels' second jump; the Second Wind; the Pack Rat's second weapon)
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
enum ThingKind : uint8_t { TH_SWARM, TH_SNAKE, TH_FISH, TH_HOLE, TH_CHUM, TH_PORTAL, TH_TRAP, TH_TURRET, TH_MINE, TH_PEEL, TH_DECOY, TH_SPRING, TH_STUCK, TH_BEAM, TH_DOG, TH_POTATO,
                          TH_EGG, TH_MIRROR, TH_HAT, TH_COUNT };   // (TH_HAT: a hat knocked off by a headshot; weapon = the hat)
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
    int crates = 0; bool training = false;   // (the Training room: everyone comes back, no wall, no round end)
    float ceilY = 1e9f, sideX = -10; int wallSide = 1;     // (the other worlds' walls: the Cave's ceiling coming down; the Void's abyss and the Salon's bouncer from a side)
    // stage 6 (scuffle_special.cpp): the strange weapons' things, burning tiles, the screen's ink and flash
    std::vector<Thing> things; std::vector<float> fireT; float inkT = 0, flashT = 0;
    // stage 6b (scuffle_rules.cpp): the match's mutators (a bit each), this round's event, the hot potato, the flood, the dark
    uint32_t mut = 0; int event = -1; float eventAt = 0; int leader = -1; float floodY = -10, lightsT = 0, reachX = -10; int reachRow = -1; bool moversStopped = false; int luckyFor = -1;
    bool Mut(int m) const { return (mut >> m) & 1u; }
    void ApplyRules();                                     // (after Init: gravity, sizes, the wall's start, the arsenal)
    void StepRules();                                      // (the event, the hot potato, ragdoll royale)
    void Resize(Stick& k, float size);
    // stage 7 (scuffle_modes.cpp): the mode's rules in the world: teams and friendly fire, respawns, the plank, the egg,
    // the hunt's sharks, the gauntlet's exit
    int mode = MD_CLASSIC; bool friendlyFire = true; std::vector<float> pts; Rectangle plank{}; float plankT = 0, potatoNext = -1; Vector2 goal{}; int start = 0;
    bool SameTeam(int a, int b) const { return a >= 0 && b >= 0 && a < (int)sticks.size() && b < (int)sticks.size() && sticks[a].team >= 0 && sticks[a].team == sticks[b].team; }
    void StepMode();
    void Respawn(Stick& k);
    void MovePlank();
    Boss boss;                                             // (stage 8: Boss Arena's boss; kind -1 when there is none)
    void StepBoss();
    void KnockHat(Stick& k, Vector2 v);                    // (stage 9: a headshot sends the hat flying)
    void StepHats();                                       // (loose hats fall, bounce, and are worn by the next bare head)
    bool BossStrike(Vector2 at, float r, float dmg, int by, bool splash = false);
    bool BossTouch(Vector2 at, float r) const;              // (would a shot here meet the boss)   // (a blow, a shot or a blast on the boss: true if it struck)
    bool EggHolder(int id) const { for (const auto& th : things) if (th.kind == TH_EGG && th.hold == id) return true; return false; }
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
    uint32_t mutators = 0; bool randomMutator = false;     // (the lobby's rules; Random: one picked each round)
    std::vector<int> trinkets;                             // (each player's trinket, -1: let the game pick)
    std::vector<int> skins, hats;                          // (each player's cosmetics: -1 none, -2 the game dresses them (bots))
    uint32_t roundMut = 0;                                 // (this round's rules: the stack plus Random's pick)
    // stage 7: the mode (scuffle_modes.cpp)
    int mode = MD_CLASSIC, teamSize = 2; bool friendlyFire = true, wallOn = true; float target = 0;   // (target: King's 60 points, the Egg's 30 s)
    int duelOffer[3] = {-1, -1, -1}, duelPick[MAX_STICKS] = {};   // (the Duel: three weapons offered before each round, and each stick's pick)
    int gStage = 0, gDeaths = 0, gWorld = 0; float gTime = 0; bool gFailed = false;   // (the Gauntlet's run)
    int TeamOf(int i) const;
    bool RoundOver(int* winner);                           // (the mode's end of a round: true, and the round's winning stick, -1 a draw, -2 a team's or the sharks' win)
    int world = -1;                                        // (the lobby's world: -1 all six; WD_COUNT the generator, endless)
    std::vector<Stage> finales;                            // (the match point's stages: the playlist's worlds' finales)
    void Start(int nPlayers, int roundsToWin, uint32_t seed, int arsenal = AR_CLASSIC);
    void NewRound();
    void Step();                                           // one fixed step: the phases, and the world while fighting
    bool Over() const { return phase == P_OVER; }
};
Stage GauntletStage(int world, int n, uint32_t seed);
// stage 9 (scuffle_cosmetics.cpp): skins and hats (doc pp. 18-20), the locker (tokens, the crate), the payout
struct Cosmetic { std::string id, name, look; int tier = 0, cost = 0; bool hat = false; };   // (tier 0 the shop, 1 common, 2 rare, 3 super rare, 4 special)
const std::vector<Cosmetic>& Skins();
const std::vector<Cosmetic>& Hats();
int SkinIndex(const std::string& id);
int HatIndex(const std::string& id);
const char* CosTierName(int tier);
struct Locker { int tokens = 0, crates = 0, bananas = 0, matches = 0, wins = 0; std::vector<std::string> owned; std::string skin, hat; bool Owns(const std::string& id) const; };
Locker& MyLocker();                                         // (scuffle_profile.txt next to the exe)
void SaveLocker();
extern bool gLockerNoSave;                                  // (tests: the player's file is never touched)
int CratePrice();
bool BuyCosmetic(const std::string& id, std::string* why = nullptr);
bool BuyCrate(std::string* why = nullptr);
struct CrateRoll { bool ok = false, banana = false; std::string id; int tier = -1; };
CrateRoll OpenCrate(uint32_t seed);                         // (a duplicate gives a banana)
bool WearCosmetic(const std::string& id);                   // (a skin or a hat; "skin:" or "hat:" alone takes it off)
int MatchTokens(const Match& m, int me);                    // (what a finished match pays player me)
Stage BossArena(int kind);                                  // (stage 8: each boss's stage)
Boss MakeBoss(int kind, int players, const Stage& s);       // (its health grows with the crew: half again for each stick past one)   // (the Gauntlet's nth stage: generated, worse each time, an exit)
std::vector<Stage> StagePlaylist(int world = -1);           // a world's forty (-1: all six worlds); the stone stages if no pack is found
std::vector<Stage> FinalePlaylist(int world = -1);

int RunScuffleTest();                                      // --scuffle-test
int RunScuffleArsenal(int reps);                           // --scuffle-arsenal [reps]: every weapon against every other on three shapes
int RunScuffleDeterminism(uint32_t seed);                  // --scuffle-determinism <seed>
int RunScuffleSim(int players, int rounds, int arsenal = AR_CLASSIC);   // --scuffle-sim <players> <rounds> [arsenal]: bots play a match

} // namespace sf
