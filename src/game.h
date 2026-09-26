// ============================================================================
//  DEPTH - shared header
//  Everything the different parts of the game need to know about each other.
// ============================================================================
#pragma once
#include "raylib.h"
#include <array>
#include <functional>
#include <string>
#include <vector>

constexpr int SCREEN_W   = 1280;
constexpr int SCREEN_H   = 720;
constexpr int PARTY_SIZE = 4;
constexpr int LOADOUT_SIZE = 4; // abilities a hero brings on an expedition (out of 8)
constexpr int RECRUIT_BATCH = 4; // exactly four new recruits show up after each mission

// ---------- Palette: a brighter take on Darkest Dungeon ----------
namespace Pal {
const Color Sea     = {18, 70, 92, 255};
const Color SeaDeep = {10, 38, 58, 255};
const Color Brass   = {214, 166, 82, 255};
const Color BrassDk = {135, 96, 40, 255};
const Color Copper  = {184, 98, 58, 255};
const Color Paper   = {240, 228, 200, 255};
const Color Ink     = {40, 30, 24, 255};
const Color Teal    = {64, 196, 190, 255};
const Color Coral   = {240, 110, 90, 255};
const Color Good    = {110, 210, 120, 255};
const Color Bad     = {225, 70, 70, 255};
const Color Stress  = {170, 120, 230, 255};
}  // namespace Pal

enum class Scene { Hub, Helm, Crew, Radar, Ward, SickLeave, Bookshelf, Periscope, Workshop, Dungeon, Platformer, Cards };

// Workshop upgrades. Each has levels 0..UPGRADE_MAX.
enum Upgrade { UP_REFLECTOR, UP_BUNKS, UP_SONAR, UP_INFIRMARY, UP_COUNT };
constexpr int UPGRADE_MAX = 3;

// Rank masks. Bit 0 = rank 1 (front line), bit 3 = rank 4 (back line).
constexpr int RANK_1 = 1, RANK_2 = 2, RANK_3 = 4, RANK_4 = 8;
constexpr int MELEE_FROM = RANK_1 | RANK_2;           // melee: must stand in the front two ranks
constexpr int MELEE_HITS = RANK_1 | RANK_2;           // ...and can only reach the front two enemies
constexpr int RANGED_FROM = RANK_2 | RANK_3 | RANK_4; // ranged: anywhere except the very front
constexpr int ANY_RANK = RANK_1 | RANK_2 | RANK_3 | RANK_4;

enum class HeroClass { Nurse, Diver, Captain, Mechanic, Whaler, Stowaway, Merman, Queen, Robot, Octopus, Siren, Wisp, COUNT };
enum class Target { Enemy, Ally, Self, AllAllies };

struct Ability {
    std::string name, desc;
    int usableFrom = ANY_RANK;
    int hits = ANY_RANK;
    Target target = Target::Enemy;
    float dmgMult = 0.0f;        // 0 = deals no damage
    int accBonus = 0;
    int hitsCount = 1;           // strikes per use
    bool aoe = false;            // hits every rank in `hits`
    int heal = 0;
    bool cure = false;           // removes bleed and poison
    int stressHeal = 0;
    int stunChance = 0;          // percent
    int bleed = 0;               // damage per turn, 3 turns
    int poison = 0;              // damage per turn, 3 turns
    int buffDmg = 0;             // +% damage for a few turns (negative on an enemy: weakens its attack)
    int buffDodge = 0;           // +dodge for a few turns (negative on an enemy: strips its evasion)
    int buffProt = 0;            // +protection for a few turns (negative on an enemy: exposes it)
    int buffAcc = 0;             // +accuracy for a few turns (negative on an enemy: blinds it)
    int guardTurns = 0;          // taunt + extra protection
    bool mark = false;           // marked enemies take +25% damage for 3 turns
    int moveTarget = 0;          // + pushes an enemy back, - pulls it forward
    bool swapWithTarget = false; // "command team": trade places with an ally
    bool ranged = false;         // thrown or fired (animates with a projectile) rather than a close-in strike
    int unlockLevel = 0;         // hero level needed before it can be slotted
};

struct Status {
    int bleedDmg = 0, bleedTurns = 0;
    int poisonDmg = 0, poisonTurns = 0; // poison stacks, and halves healing received
    int stunned = 0;
    int buffDmg = 0, buffTurns = 0;       // negative = weakened attack (an enemy effect)
    int dodgeBuff = 0, dodgeTurns = 0;    // negative = stripped evasion (an enemy effect)
    int protBuff = 0, protTurns = 0;      // negative = exposed (an enemy effect)
    int accBuff = 0, accTurns = 0;        // negative = blinded (an enemy effect)
    int spdBuff = 0, spdTurns = 0;        // negative = slowed
    // region debuffs, each from one location's creatures: Totemic Burn (Island), Silt Blindness (Cave),
    // Drowning Entanglement (Weeds), Eldritch Madness (Atlantis). Turns remaining.
    int burnTurns = 0, siltTurns = 0, drownTurns = 0, madTurns = 0;
    int guardTurns = 0;
    int marked = 0;
};

// ---------- relics ----------
// Crew carry at most two relics, and at most one of them a "hard weapon" (a blade, a firearm, a staff).
// Each relic has stat changes, a few combat and platformer effects, an on-hit hook, and a small hand-inked
// SVG icon that relics.cpp draws into a texture. See relics.h.
enum class RelicCategory { OFFENSE, ENGINEERING, SUPPORT, UTILITY, OCCULT };
struct CombatState;      // what an on-hit effect can see and change (relics.h)
struct PlatformState;
struct RelicFx {         // numeric effects, summed over a hero's relics (and their synergies) by RelicBundle()
    int critPct = 0;         // added to the hero's crit chance
    int armorPen = 0;        // percent of an enemy's protection ignored
    int vsArmored = 0;       // percent extra damage against enemies with 10+ protection (shells, plate)
    int vsConstruct = 0;     // percent extra damage against constructs (bosses of stone, brass and bronze)
    int healBonus = 0;       // flat added to every heal the hero casts
    int healParty = 0;       // a single-target heal also heals the rest of the party by this much
    int stressGainPct = 0;   // percent less stress gained
    int extraSlots = 0;      // more inventory slots on an expedition
    int pickupPct = 0;       // platformer: bigger coin pickup radius
    int speedPct = 0;        // platformer: run speed
    int jumpPct = 0;         // platformer: jump strength
    int lampPct = 0;         // platformer: helmet lamp radius in the dark levels
    bool chainOnCrit = false;// a critical hit always arcs
    int dmg = 0;             // synergy-only: added damage
};
struct RelicDef {
    std::string name, desc;
    int hp = 0, dmg = 0, speed = 0, acc = 0, dodge = 0, prot = 0, stressResist = 0;
    int price = 80;
    RelicCategory category = RelicCategory::UTILITY;
    bool isHardWeapon = false;
    RelicFx fx;
    std::string dungeonText, platformerText;                    // the two halves of the description
    std::function<void(CombatState&)> combatEffect;             // on-hit effect in expeditions
    std::function<void(PlatformState&)> platformerEffect;       // effect in the platform levels
    std::string svgSpriteData;                                  // the icon, as SVG markup
};

struct Stats { int maxHp, dmgMin, dmgMax, speed, acc, dodge, prot, stressResist; };

struct Hero {
    int id = 0;
    std::string name;
    HeroClass cls = HeroClass::Nurse;
    int level = 0, xp = 0;
    int hp = 1;
    int stress = 0;          // "Nerves", 0-100
    bool rattled = false;    // hit 100 stress; cured at Sick Leave
    bool deathsDoor = false; // 0 HP: the next hit might be fatal
    bool dead = false;
    int onLeave = 0;         // expeditions left to sit out
    int relics[2] = {-1, -1};
    int loadout[LOADOUT_SIZE] = {0, 1, 2, 3}; // indices into ClassAbilities, -1 = empty slot
    int outfit = -1;         // >= 0 for the Nautilus's own hands (see NpcOutfit): a uniform instead of class gear
    Status st;
};

struct EnemyAbility {
    std::string name;
    int hits = ANY_RANK;               // which HERO ranks it can reach (melee: front two)
    int fromRanks = ANY_RANK;          // which of the ENEMY's own ranks may use it (melee: 1-2; long range: 2-4)
    bool aoe = false;
    float dmgMult = 1.0f;
    int stress = 0, stunChance = 0, bleed = 0, poison = 0;
    int weakAtk = 0, weakAcc = 0, weakDef = 0, weakSpd = 0; // percent, for 3 turns
    int region = 0;                    // 1 Totemic Burn, 2 Silt Blindness, 3 Drowning Entanglement, 4 Eldritch Madness
    int targetsN = 0;                  // >0: reaches this many random heroes instead of one
    int pull = 0;                      // command: 1 drag a back-rank hero to the front, 2 shuffle the party, 3 swap two heroes
    int selfMove = 0;                  // command on itself: +1 falls back a rank, -99 steps to the front
    int healSelf = 0, healAllies = 0, healLowest = 0;   // heals (flat)
    int buffAllyAtk = 0, buffAllyDef = 0, buffSelfAtk = 0, buffSelfDef = 0; // percent
    bool cleanse = false, drain = false;
    int summon = -1;                   // an EnemyType to call to the fight
};

// Region bestiaries. Standards and supports fill the ordinary fights; minis turn up with a probability that
// climbs with depth; each location ends in its own level boss.
enum class EnemyType {
    SeaLouse, CaveShrimp, BrineWorm, Lobster,
    DysCrustacean, GhostWorm, LostDiver, CrustaceanQueen,                       // the Cave
    TribalSpearman, WarDog, TribalShaman, TribalDemigod, CoconutQueen, SunGod,   // the Island
    FeralMerman, Siren, GiantOctopus, ElectricEel, GreatWhite, Neptune,          // the Weeds
    LostInfantry, LostCultist, ArmorLostOne, AlienHorror, Cthulhu,               // Atlantis
    COUNT
};

struct Enemy {
    int uid = 0;
    EnemyType type = EnemyType::SeaLouse;
    std::string name;
    int maxHp = 1, hp = 1, dmgMin = 1, dmgMax = 1, speed = 0, acc = 80, dodge = 0, prot = 0;
    bool boss = false;                 // drawn large; minis and level bosses both
    int extraAct = 0;                  // percent chance (bosses only) to act a second time after each of its turns, tuned per boss
    int span = 1;                      // how many of the four enemy ranks it fills: a level boss 3, a mini-boss 2. Still one enemy, hittable in any of its ranks
    int tier = 0;                      // 0 standard, 1 mini-boss, 2 level boss
    bool alive = true;
    std::vector<EnemyAbility> abilities;
    Status st;
};

struct FloatText { Vector2 pos; std::string text; Color color; float life; };
struct TurnEntry { bool hero; int id; int init; };

enum class RoomType { Fight, Treasure, Boss };

// Things the crew can pick up and carry through an expedition: a battery to burn for light, a bandage
// to patch someone up on the spot, a key that opens a locked chest, or a relic found still loose (not
// yet fitted to anyone). Carried in a short list with limited room, so keeping one sometimes means
// leaving another behind.
enum class ItemKind { Battery, Bandage, Key, Relic };
struct InvItem { ItemKind kind; int relicId = -1; };
constexpr int INV_SLOTS = 5;
enum class DPhase { Corridor, Walking, Combat, RoomClear, Treasure, Victory, Retreat, Defeat };

// Dungeon difficulty levels. Clearing one unlocks the next; earlier ones stay available.
constexpr int CAVE_TIERS = 5;
inline constexpr int CAVE_TIER_LEVEL[CAVE_TIERS] = {0, 1, 3, 5, 6};
inline const char* const CAVE_TIER_NAME[CAVE_TIERS] = {"Shallows", "Tidal Caves", "The Deep", "The Abyss", "The Trench"};

// The four Shallows expeditions, all open from the start. They share the same room-and-combat engine
// and the same tier ladder above -- what differs is the scenery, the names, and who's waiting at the end.
enum class Location { Cave, Island, Weeds, Atlantis, COUNT };
constexpr int LOCATION_COUNT = (int)Location::COUNT;
const char* LocationName(Location loc);
const char* AtmosphereName(Location loc, int variant);
const char* LocationBossName(Location loc); // the mini-boss at the end of a run
const char* LocationDesc(Location loc);

// ---------- combat animation ----------
enum class Anim { None, Melee, Ranged, Heal, Buff, Hurt, Dodge, Stress };
struct UnitAnim { bool hero; int id; Anim kind; float t, dur; };
struct Projectile { Vector2 from, to; float t, dur; int kind; bool hero; };
struct Spark { Vector2 p, v; float life, max, size; Color c; };
// An action in progress: the actor winds up, its effect lands at `impact`, and the turn ends at `end`.
struct PendingAction {
    bool active = false, hero = false, applied = false, fired = false;
    int id = -1, ability = -1, target = -1;
    Anim kind = Anim::None;
    float t = 0, fire = 0, impact = 0, end = 0;
};

// How a crew member is posed this frame (all zero = standing ready). Set by the combat animations.
// Uniforms worn by the Nautilus's own crew, who work the salon but never join expeditions.
enum NpcOutfit { OUT_HELMSMAN, OUT_RADIO, OUT_ENGINEER, OUT_PROFESSOR, OUT_STEWARD, OUT_ORDERLY, OUT_COUNT };

struct Pose {
    float lean = 0;        // + leans forward, - rears back
    float crouch = 0;      // 0..1 bends the knees
    float reach = 0;       // 0..1 thrusts the weapon arm forward
    float raise = 0;       // 0..1 lifts the weapon arm overhead
    float backRaise = 0;   // 0..1 lifts the other arm
    float weaponTilt = 0;  // extra weapon rotation in degrees (+ swings down/forward)
    float stride = 0;      // 0..1 steps the front foot forward (lunges, staggers)
    float tremble = 0;     // 0..1 shaking hands and head (fear, strain)
    float headDown = 0;    // + bows the head (dread, pain), - snaps it back
};

struct DungeonState {
    Location loc = Location::Cave; // which of the four Shallows expeditions this is
    unsigned visSeed = 0;          // the run's visual seed: skyline silhouettes, prop layout
    int atmos = 0;                 // the run's atmospheric state, 0-2 (see AtmosphereName)
    bool miniFight = false;        // the current room holds a mini-boss
    int tier = 0;                  // index into CAVE_TIER_LEVEL
    float scroll = 0, walkT = 0;   // how far the party has walked (drives the parallax), and the walk timer
    std::vector<UnitAnim> anims;
    std::vector<Projectile> shots;
    std::vector<Spark> sparks;
    PendingAction pending;
    float shake = 0;
    float corridorT = 0;           // time since the "advance or swap a battery?" choice came up (it fades in)
    float batteryT = 0;            // counts down while a battery is being swapped in
    float lightShown = 100;        // the flashlight as drawn: eases toward `light`
    std::string levelUps;
    std::vector<RoomType> rooms;
    int roomIndex = -1;
    float light = 100;
    int lootGold = 0;
    std::vector<int> lootRelics;
    int rewardRelic = -1;
    int roomGold = 0, roomRelic = -1;
    DPhase phase = DPhase::Corridor;
    bool resultsApplied = false;
    std::vector<Enemy> enemies;
    int nextUid = 1;
    std::vector<TurnEntry> order;
    int turnIdx = 0, round = 0;
    bool turnStarted = false, pendingSkip = false;
    float actTimer = 0;
    int selectedAbility = -1;
    std::vector<std::string> log;
    std::vector<FloatText> floats;

    // ---------- carried items ----------
    std::vector<InvItem> inventory;
    bool pendingItem = false;      // a find is waiting to be taken or left in the current room
    InvItem pendingItemVal{};
    bool roomIsChest = false;      // this Treasure room is a locked chest: needs a Key to open
    bool chestOpened = false;
    int invSelected = -1;          // an inventory slot awaiting a target (a hero to heal or fit a relic to)
};

// ---------- platformer ----------
enum PlatLevel { PL_PIPES, PL_HULL, PL_PIRATE, PL_COUNT };

struct PlatEnemy {
    char type; Vector2 pos, home; float dir, t;
    int state = 0;    // pirates: 0 hidden or idle, then emerging / stabbing / retreating, or aiming
    float timer = 0;  // time in the current state (or cooldown while hidden)
    Vector2 aim{0, 0}; // where a gunner is aiming
};
struct PlatShot { Vector2 pos, vel; float life; int kind; }; // 0 musket ball, 1 lit bomb, 2 explosion, 3 falling ink
struct PlatParticle { Vector2 p, v; float life, max, size; Color c; };
// ---- animation states for the articulated platform characters
enum class BBAnim { Idle, Walk, Windup, Charge, AimPistol, Dazed, Recover };   // Blackbeard: each drives his limbs
enum class CrabAnim { Idle, Scuttle, ClawSnap };                                // the crab's legs and claws

// ---- the Kraken's three newest attacks, each a small state machine with its own timers
// TentacleReachState: two tentacles rise from the abyss and TRACK the player, reaching for wherever they stand.
struct TentacleReachState {
    bool active = false;
    float t = 0;                     // 0-0.8 telegraph, 0.8-2.8 reaching (deadly), 2.8-3.4 retracting
    Vector2 base[2], tip[2];         // where each tentacle starts, and where its tip is now
    Vector2 target{0, 0};            // the last known player position, updated every frame while reaching
};
// InkRainState: ink falls from the top of the screen at random x, as dark projectiles with hitboxes.
struct InkRainState {
    bool active = false;
    float t = 0, spawnT = 0;         // spawns drops between 0.4 s and 3.0 s
};
// BeakChargeState: the head locks onto the player's height, telegraphs, then dashes across the arena. Its front
// (the beak) has its own, longer and narrower hitbox than its body.
struct BeakChargeState {
    bool active = false;
    float t = 0;                     // 0-0.9 telegraph (tracks Y), 0.9-2.0 dash, then done
    float y = 0;                     // the height it locked onto (centre)
    float fromX = 0, toX = 0, x = 0; // dash endpoints and current position
    float dir = 1;
};
enum class KrakenMove { TentacleSlam = 0, InkFlood = 1, Lunge = 2, TentacleReach = 3, InkRain = 4, BeakCharge = 5 };

struct PlatBoss {
    char type = 0;               // 'K' Kraken, 'B' Blackbeard, 0 = none
    TentacleReachState reach;    // Kraken
    InkRainState rain;
    BeakChargeState beak;
    Vector2 home{0, 0}, pos{0, 0}, vel{0, 0};
    int hp = 3, state = 0;
    float timer = 0, invuln = 0, dir = -1;
    int volley = 0;                               // Blackbeard: alternates pistol shots and charges
    float chargeStartX = 0;                        // Blackbeard: where a charge began, so a lucky tap on a nearby wall doesn't count
    float tentX[2] = {0, 0}, tentT[2] = {-1, -1}; // Kraken tentacle strikes (x, time since warning; <0 = idle)
    bool tentTop[2] = {false, false};             // true = slams down from above, false = rises from the abyss
    bool tentFake[2] = {false, false};            // a bluff: warns like a real strike, then never extends
    int moveKind = -1;                            // Kraken, chosen once per submerged cycle: -1 not yet, 0 tentacles, 1 ink, 2 lunge
    float inkT = -1;                              // Kraken ink spray: seconds since it began, < 0 = idle
    bool inkSafeRight = false;                    // which half of the arena the ink cloud leaves clear
    float lungeT = -1;                            // Kraken lunge: seconds since it began, < 0 = idle
    float lungeFromX = 0, lungeToX = 0;           // sweep endpoints
    bool defeated = false;
};

struct PlatformState {
    int level = PL_PIPES;
    std::string layoutCode;
    std::vector<std::string> tiles;
    int w = 0, h = 0;
    Vector2 pos{0, 0}, vel{0, 0}, scale{1, 1}, startPos{0, 0};
    bool onGround = false, facingRight = true;
    int wallSide = 0, lockSide = 0;   // wall being slid on / wall just jumped from (-1 left, 1 right)
    float coyote = 0, wallCoyote = 0, jumpBuffer = 0, wallLock = 0, runAnim = 0, accumulator = 0;
    int checkpointChunk = 0;
    std::vector<PlatEnemy> enemies;
    PlatBoss boss;
    std::vector<PlatParticle> particles;
    int coins = 0, deaths = 0, reward = 0, relic = -1, relic2 = -1; // relic2: Blackbeard sometimes leaves a second
    float time = 0, deathTimer = 0, camX = 0, camY = 0;
    bool finished = false, exitOpen = true;
    // Sections are stitched left to right, each raised or lowered so its entrance meets the last exit.
    std::vector<int> partX;          // first tile column of each section
    std::vector<Vector2> spawns;     // where you respawn in each section (its checkpoint)
    std::vector<float> deathY;       // per tile column: fall below this and you're lost
    std::vector<int> partInterior;   // per section: the tile row where its below-decks interior starts (or a huge number)
    std::vector<int> partKind;       // per section: 0 open air, 1 the ship's hold, 2 the captain's cabin
    std::vector<int> layout;         // the sections in this run, so a death can rebuild the level from scratch
    std::vector<PlatShot> shots;     // musket balls and bombs in flight
    int pickupPct = 0, speedPct = 0, jumpPct = 0, lampPct = 0; // from the lead hero's relics (see RelicFx)
    bool hard = false;               // Hard keeps the gears and jets; Normal leaves them out
    bool checkpoints = false;        // respawn at the last section reached, but forfeit the relic
    bool bossEnabled = true;         // Hull/Pirate: whether the boss arena has its boss in it
    bool onWeed = false;             // the diver is among seaweed: it slows a fall and can be climbed
    int climbDir = 0;                // -1 up, 1 down: held keys, read only while on weed (never set by the path search)
    bool ghost = false;              // the rare Ghost Ship: undead crew, fog, and everything 1.6x faster
    struct Crumble { int tx, ty; float t; };
    std::vector<Crumble> crumbles;   // fragile scaffolding that has been stepped on and is shaking
    std::vector<std::pair<int, int>> crumbled; // and what has already fallen, so a checkpoint respawn can put it back
    int genTop = 0;                  // generator row 0 sits at this row offset (negative when the arena rises above it)
    bool verifying = false;          // the path search drives the real movement: nothing may permanently change the tiles
};

struct Game {
    Scene scene = Scene::Hub;
    int gold = 60; // kept deliberately scarce: parkour runs and Flats are meant to make up the difference
    int batteries = 2;
    std::vector<Hero> roster;
    std::array<int, PARTY_SIZE> party{{-1, -1, -1, -1}}; // hero ids, rank 1 first
    std::vector<int> relicStorage;
    std::vector<Hero> recruits;
    int radarRefreshes = 0; // manual re-scans left this expedition cycle, from the Sonar Array upgrade
    std::vector<int> shopRelics;
    int nextHeroId = 1;
    int selectedHero = -1;
    int dismissArmed = -1;
    int bookTab = 0;
    int bookScroll = 0;
    int relicScroll = 0;
    int upgrades[UP_COUNT] = {0, 0, 0, 0};
    std::vector<int> platLayouts[PL_COUNT]; // which chunks make up each platform level's current layout
    bool platCleared[PL_COUNT] = {false, false, false};
    float platBest[PL_COUNT] = {0, 0, 0};    // best clear time in seconds (0 = never cleared)
    bool platHard = false;                   // Periscope option: the full-strength layouts
    bool platCheckpoints = false;            // Periscope option: checkpoints, at the cost of the relic
    bool platHullBoss = true;                // Periscope option: fight the Kraken (only chance of a relic)
    bool platPirateBoss = true;              // Periscope option: fight Blackbeard (guarantees relic(s))
    int tierCleared[LOCATION_COUNT] = {-1, -1, -1, -1}; // highest level beaten, per location (-1 = none)
    int tierSel[LOCATION_COUNT] = {0, 0, 0, 0};         // the level chosen at the Helm, per location
    std::string toast;
    float toastTimer = 0;
    float time = 0;
    DungeonState dungeon;
    PlatformState plat;
};

// ---------- data.cpp ----------
void InitGame(Game& g);
const std::vector<Ability>& ClassAbilities(HeroClass c);
const char* ClassName(HeroClass c);
const char* ClassBlurb(HeroClass c);
Color ClassColor(HeroClass c);
Hero MakeHero(Game& g, HeroClass c);
Hero MakeRandomHero(Game& g);
const std::vector<RelicDef>& Relics();
Stats GetStats(const Hero& h);
Enemy MakeEnemy(EnemyType t, int uid);
std::vector<EnemyType> LocationStandards(Location loc);  // ordinary melee and ranged enemies
std::vector<EnemyType> LocationSupports(Location loc);   // healers, controllers: at most one per fight, in the back
std::vector<EnemyType> LocationMinis(Location loc);
EnemyType LocationLevelBoss(Location loc);
int MiniBossChance(int levelValue);                       // percent chance of a mini-boss encounter at a depth level
const char* RegionDebuffName(Location loc);
void DrawBestiaryFigure(const Enemy& e, Rectangle r, float t); // the new creatures (render.cpp)
bool DrawRichEnemy(const Enemy& e, Rectangle r, float t);      // the richly drawn creatures (enemyart.cpp); false = not one of them yet
void DebugSetEnemies(Game& g, Location loc, const std::vector<EnemyType>& types); // debug: a hand-picked enemy line-up in the first fight
void GiveXP(Game& g, Hero& h, int amount);
int XpForNextLevel(const Hero& h);
Hero* FindHero(Game& g, int id);
bool InParty(const Game& g, int id);
void CompactParty(Game& g);
void RefreshRadar(Game& g);
int MaxRoster(const Game& g);
int SonarRefreshCount(const Game& g);
int ScanCost(const Game& g);
int WardCostPerHp(const Game& g);
int LightDrainPerRoom(const Game& g);
const char* UpgradeName(int u);
const char* UpgradeDesc(int u, int level); // what the given level does
int UpgradePrice(int level);                // price to buy the given level
int LoadoutCount(const Hero& h);
void ScaleEnemyForTier(Enemy& e, int tier);

// ---------- save.cpp ----------
bool SaveGame(const Game& g);
bool LoadGame(Game& g);
void DeleteSave();

// ---------- render.cpp: lighting, textures, post-processing, figures ----------
void InitArt();
void UnloadArt();
const Font& BodyFont();
const Font& BoldFont();
void BeginFrame();                 // everything is drawn into an offscreen scene...
void EndFrame(float time);         // ...then presented through the post-process shader
void SetPost(float vignette, float grain, float bloom);
bool SaveFrameShot(const char* path); // debug: writes the last presented frame to a PNG
void BeginLayer(RenderTexture2D& rt); // draw into another texture for a while...
void EndLayer();                      // ...then return to the scene
void DrawTri(Vector2 a, Vector2 b, Vector2 c, Color col); // a triangle in any vertex order
void LightsBegin(Color ambient);   // start a lightmap: ambient is how dark unlit areas get
void AddLight(Vector2 pos, float radius, Color c, float intensity = 1.0f);
void AddCone(Vector2 origin, float angle, float spread, float length, Color c);
void LightsEnd();                  // multiply the lightmap onto the scene
void Glow(Vector2 pos, float radius, Color c); // additive bloom sprite drawn straight onto the scene
enum class Tex { Metal, Wood, Paper, Rock };
Texture2D GetTex(Tex t);
void DrawTiled(Tex t, Rectangle dst, float scale, Color tint, Vector2 offset = {0, 0});
void DrawTexturedCircle(Texture2D tex, Vector2 c, float r, bool flipY);
RenderTexture2D& OceanRT();
constexpr int PIXEL_W = SCREEN_W / 2, PIXEL_H = SCREEN_H / 2;
RenderTexture2D& PixelRT(); // low-resolution canvas for the pixel-art platformer
void DrawVGradient(Rectangle r, Color top, Color bottom);
void DrawPipeH(float x1, float x2, float y, float radius, Color base);
void DrawPipeV(float x, float y1, float y2, float radius, Color base);
void DrawFlange(Vector2 c, float radius, bool vertical, Color base);
void DrawGauge(Vector2 c, float r, float needle01, Color face);
void DrawGear(Vector2 c, float r, int teeth, float rot, Color col);
void DrawBrassPlate(Rectangle r, const char* text, int size);
void DrawCrewFigure(const Hero& h, Vector2 feet, float scale, bool faceRight, float walk, float t, const Pose& pose = Pose{});
void DrawCrewFigureInked(const Hero& h, Vector2 feet, float scale, bool faceRight, float walk, float t,
                         const Pose& pose = Pose{}, Color tint = WHITE); // with ink and volume
void DrawShadowBlob(Vector2 feet, float w);
// Characters: draw between BeginFigure/EndFigure with their feet at FigureFeet(); EndFigure inks them,
// adds volume, and places them with their feet at `feet` on screen. `tint` flashes them (e.g. red when hit).
void BeginFigure();
void EndFigure(Vector2 feet, Color tint = WHITE, float sx = 1, float sy = 1);
Vector2 FigureFeet();
RenderTexture2D& ArtRT();              // 512 x 768 scratch canvas for flat art mapped onto walls
void BeginCanvas(RenderTexture2D& rt); // like BeginFigure, but just paints into `rt`
void EndCanvas();
Color Tone(Color c, float k); // k < 0 darkens toward shadow, k > 0 lightens toward a warm highlight
void ShadeBall(Vector2 c, float r, Color col);                         // a lit sphere
void ShadeLimb(Vector2 a, Vector2 b, float wa, float wb, Color c);     // a lit tapered cylinder
void ShadeQuad(Vector2 tl, Vector2 tr, Vector2 br, Vector2 bl, Color c); // a lit panel
void BeginBackdrop();          // draw distant scenery softly out of focus...
void EndBackdrop(float blur);  // ...and lay it into the scene
void DrawItemIcon(ItemKind kind, int relicId, Vector2 c, float s); // a carried item, drawn at radius ~s
void InkPass(float ink, float hatch); // Darkest Dungeon-style inking and crosshatching over the world drawn so far

// ---------- ui.cpp ----------
int MeasureTxt(const std::string& s, int size, bool bold = false);
void Txt(const std::string& s, float x, float y, int size, Color c);
void TxtBold(const std::string& s, float x, float y, int size, Color c);
void TxtShadow(const std::string& s, float x, float y, int size, Color c, bool bold = false);
void DrawTextCenteredBold(const std::string& s, float cx, float y, int size, Color c);
void DrawTextCentered(const std::string& s, float cx, float y, int size, Color c);
bool Button(Rectangle r, const char* text, bool enabled = true, int fontSize = 20);
void Panel(Rectangle r, Color fill = Pal::Paper);
void DrawWrapped(const std::string& text, Rectangle r, int size, Color c);
void DrawBar(Rectangle r, float frac, Color fill);
void Toast(Game& g, const std::string& msg);
void DrawToast(Game& g);
bool BackButton(Game& g);
void DrawSceneTitle(const char* title, const char* subtitle);
void DrawGoldBadge(const Game& g);
void DrawCabinBackground();
std::string RankString(int mask);

// ---------- hub.cpp ----------
// sprite sheet pages (depth.exe --sprites)
void DrawSalonSpritePage(float t);
void DrawCrewSpritePage(float t);
void DrawCaveSpritePage(float t);
void DrawBestiarySpritePage(int page, float t);
void DrawPlatformSpritePage(int page, float t);
Image GrabFrame();
void DebugPetCat();
void SceneHub(Game& g);
void SceneCards(Game& g);            // Flats, the card table
void FlatsSpritePage(float t);      // the sprite sheet page of Flats cards, the dealer and the bell
void FlatsSim(int runs, bool sensible);                // headless: play Flats runs and print how they go
void DebugFlatsReward();            // debug: the reward pick screen
void DebugFlatsShop();              // debug: the dealer's stall
void DebugFlatsDeck();            // debug: the deck viewer
void DebugFlatsDeal();            // debug: skip the menu and deal a mid-round hand (for screenshots)
void DrawItemSpritePage(float t);   // the sprite sheet page of carried items and relic icons
void SceneHelm(Game& g);
void SceneCrew(Game& g);
void SceneRadar(Game& g);
void SceneWard(Game& g);
void SceneSickLeave(Game& g);
void SceneBookshelf(Game& g);
void ScenePeriscope(Game& g);
void SceneWorkshop(Game& g);

// ---------- dungeon.cpp ----------
void StartDungeon(Game& g, Location loc);
void SceneDungeon(Game& g);
void DebugEnterCombat(Game& g, Location loc = Location::Cave); // debug: jump straight into the first fight
void SimulateBossFight(int runs, int level, int tier, int enemyType, bool randomPlayer); // debug: one boss, many fights
void SimulateExpeditions(int runs, int level, bool randomPlayer, int tier = 0); // debug: auto-play expeditions and print the results

// ---------- platformer.cpp ----------
void GeneratePlatLayout(Game& g, int level);
bool PlatLayoutValid(const Game& g, int level);
std::string PlatLayoutCode(const Game& g, int level);
const char* PlatLevelName(int level);
void StartPlatform(Game& g, int level);
void ScenePlatformer(Game& g);
int VerifyPlatformLevels(); // debug: proves every section can be crossed; returns the number that can't
