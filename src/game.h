// ============================================================================
//  DEPTH - shared header
//  Everything the different parts of the game need to know about each other.
// ============================================================================
#pragma once
#include "raylib.h"
#include "beasts.h"
#include "levelgen.h"
#include <array>
#include <functional>
#include <string>
#include <vector>
#include <map>
#include "chart.h"

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

enum class Scene { Hub, Helm, Crew, Radar, Ward, SickLeave, Bookshelf, Periscope, Workshop, Dungeon, Platformer, Cards, Abyss, Study, Arcade, RedTide, Trawl, Flight, Mouthful, NightOff, Scuffle };

// Workshop upgrades. Each has levels 0..UPGRADE_MAX.
enum Upgrade { UP_REFLECTOR, UP_BUNKS, UP_SONAR, UP_INFIRMARY, UP_CARGO, UP_COUNT };
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
    int riposte = 0;             // turns the caster counters melee hits (Stage 7)
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
    int riposteTurns = 0;                 // counters melee hits
    int marked = 0;
};

// ---------- relics ----------
// Crew carry at most two relics, and at most one of them a "hard weapon" (a blade, a firearm, a staff).
// Each relic has stat changes, a few combat and platformer effects, an on-hit hook, and a small hand-inked
// SVG icon that relics.cpp draws into a texture. See relics.h.
enum class RelicCategory { OFFENSE, ENGINEERING, SUPPORT, UTILITY, OCCULT };
struct CombatState;      // what an on-hit effect can see and change (relics.h)
struct PlatformState;
struct AbyssState;
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
    // Individual variance, rolled once at recruitment: -2..+2 on four axes, so two recruits of the same class
    // are never identical. The player is meant to read the numbers and judge which recruit suits which role.
    int vigor = 0, might = 0, quickness = 0, fortitude = 0;
    bool steeled = false;    // Stage 7 resolve: 100 nerves sometimes steels a hero instead of rattling them (for the rest of the expedition)
    unsigned habits = 0, habitLocked = 0;   // Stage 7 habits (quirks): bit = HabitId; locked ones can't be replaced
    unsigned ailments = 0;                  // Stage 7 ailments: bit = Ailment; persistent until cured
    int drill[8] = {};                      // Stage 7 Drill Deck: training level (0-DRILL_MAX) per class ability
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
    BarnacleCrab, LanternAngler, FireDancer, IdolBearer, MantisShrimp, KelpWraith, DrownedOracle, StarSpawn, // Stage 7: two more per location (appended: saves index by type)
    Leviathan, AbyssalEye,                                                       // Stage 7: the Trench's and the Hadal's bosses
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
    bool elite = false;                // Stage 7: an elite variant (from cave level 3): +30% HP, one extra ability, a brass name plate
    bool alive = true;
    std::vector<EnemyAbility> abilities;
    Status st;
};

// Stage 7: supplies bought from the Quartermaster at the Helm (lost on return), and camp skills (two per class)
enum Supply { SUP_BANDAGE, SUP_ANTIVENOM, SUP_GROG, SUP_CROWBAR, SUP_SALT, SUP_COUNT };
const char* SupplyName(int s);
const char* SupplyDesc(int s);
int SupplyPrice(int s);
extern const int BATTERY_PRICE, CAMP_POINTS, STEELED_CHANCE, SLEEP_HEAL_PCT, BANDAGE_HEAL;
struct CampSkill { HeroClass cls; const char* name; const char* desc; int cost; int healAll, healOne, nerveAll, nerveOne; bool cure, self; int ambushPct, blessFights, light, gold; bool lullaby; };
const std::vector<CampSkill>& CampSkills();

// EnemyBrain (dungeon.cpp): the weights of its features per tier (data.cpp BrainFor), and each enemy family's personality
struct BrainWeights { float expDmg, kill, focus, threat, healer, rank, nerve, status, setup, selfPres, turnOrder; int lookahead, samples; float temp; };
const BrainWeights& BrainFor(int tier);   // tier index 0-4 (cave levels 0, 1, 3, 5, 6)
enum class Personality { None, Swarm, Cunning, Brute, Cowardly, Guardian, Boss };
Personality EnemyPersonalityOf(EnemyType t);
extern const float BRAIN_KILL_VALUE, BRAIN_LOOKAHEAD_WEIGHT;
extern const int BRAIN_LOOKAHEAD_OPTIONS;
struct BrainPick { int ability = -1, target = -1; };
extern int gBrainMode;                    // 0 the brain at the dungeon's tier; -1 the old random choice (--brain-test's baseline); t+1 forces tier t
void BrainTest(int tier, int runs);       // --brain-test <tier|-1> <runs>

struct FloatText { Vector2 pos; std::string text; Color color; float life; };
struct TurnEntry { bool hero; int id; int init; };

enum class RoomType { Fight, Treasure, Boss, Entrance, Curio, Rest, Shrine, Empty }; // Empty: a quiet junction on the way

// Things the crew can pick up and carry through an expedition: a battery to burn for light, a bandage
// to patch someone up on the spot, a key that opens a locked chest, or a relic found still loose (not
// yet fitted to anyone). Carried in a short list with limited room, so keeping one sometimes means
// leaving another behind.
enum class ItemKind { Battery, Bandage, Key, Relic };
struct InvItem { ItemKind kind; int relicId = -1; };
constexpr int INV_SLOTS = 5;
enum class DPhase { Corridor, Walking, Combat, RoomClear, Treasure, Victory, Retreat, Defeat, Event };
// what an expedition sets out to do, chosen at the Helm (Master Reference: one objective per expedition, with a bonus)
enum class Objective { Slay, Chart, Salvage, Cleanse, COUNT };
const char* ObjectiveName(Objective o);
const char* ObjectiveText(Objective o);
// an event on the way or in a room that needs the player: a trap, a curio, a camp, a shrine, a blocked passage
enum class EventKind { None, Trap, Curio, Rest, Shrine, Blocked, Loot };

// Dungeon difficulty levels. Clearing one unlocks the next; earlier ones stay available.
constexpr int CAVE_TIERS = 6;   // tier indices 0-4 are the Shallows' ladder, 3-5 the deep locations' (levels 5, 6, 7)
inline constexpr int CAVE_TIER_LEVEL[CAVE_TIERS] = {0, 1, 3, 5, 6, 7};
inline const char* const CAVE_TIER_NAME[CAVE_TIERS] = {"Shallows", "Tidal Caves", "The Deep", "The Abyss", "The Drop", "The Bottom"};

// The four Shallows expeditions, all open from the start. They share the same room-and-combat engine
// and the same tier ladder above -- what differs is the scenery, the names, and who's waiting at the end.
enum class Location { Cave, Island, Weeds, Atlantis, Trench, Hadal, COUNT };   // the last two: the deep tiers (Stage 7), unlocked by clearing all four Shallows
// ---------- Stage 7: habits (quirks), ailments and crew bonds (numbers in data.cpp) ----------
enum HabitId { HB_STEADY_HANDS, HB_DECK_LEGS, HB_NIGHT_EYES, HB_IRON_GUT, HB_QUICK_STEP, HB_BRAWLER, HB_CALM_HEART, HB_TOUGH_HIDE,
               HB_BOTTLE_FIEND, HB_CLAUSTROPHOBE, HB_SUPERSTITIOUS, HB_SHAKY_HANDS, HB_SLOW_STARTER, HB_FRAIL_FRAME, HB_JUMPY, HB_SEASICK, HB_COUNT };
struct HabitDef { const char* name; const char* desc; bool good; int acc, dodge, speed, prot, dmg, maxHp, stressPct; int loc; bool lowLight; };
const HabitDef& Habit(int id);
extern const int HABIT_MAX_GOOD, HABIT_MAX_BAD, HABIT_GAIN_PCT, HABIT_GOOD_WIN_PCT, HABIT_GOOD_LOSS_PCT, HABIT_LOCK_PRICE, HABIT_REMOVE_PRICE, CURIO_HABIT_PCT;
enum Ailment { AIL_SALT_ROT, AIL_REEF_FEVER, AIL_BARNACLE_LUNG, AIL_BENDS, AIL_COUNT };
const char* AilmentName(int a);
const char* AilmentDesc(int a);
int AilmentCurePrice(int a);
int LocationAilment(Location l);              // what each location's bleeding and poisoning creatures pass on
extern const int AILMENT_HIT_PCT, CURIO_AILMENT_PCT, BONESAW_SELF_HP;
extern const int BOND_MAX, BOND_PERK, BOND_DMG_PCT, BOND_DEATH_NERVES, BOND_BARK_PCT, BOND_BARK_CALM, HERO_CRIT_CALM, ENEMY_CRIT_NERVES;
extern const int ENEMY_FLEE_PCT, RIPOSTE_DMG_PCT, DRILL_MAX, DRILL_STEP_PCT, DRILL_STUN_STEP;
extern const int ELITE_PCT_BY_TIER[CAVE_TIERS], ELITE_HP_PCT, SIM_DRILLS_BY_TIER[CAVE_TIERS];
extern const int REGION_BURN_DMG, MADNESS_SLIP_PCT;   // the Island's burn per turn; the chance Eldritch Madness costs a turn
extern const int TRENCH_PRESSURE_MAX, TRENCH_PRESSURE_SPEED, TRENCH_VENT_LIGHT, CREW_MAX_LEVEL;
int DrillPrice(int toLevel, int unlockLevel);   // the Drill Deck's price for the next level of an ability
void ApplyDrill(Ability& a, int level);         // +10% damage or effect per level
extern int gStatLocation;                     // the location of the expedition in progress (-1 aboard): location habits read it
int HabitStressPct(const Hero& h, bool lowLight);
bool GainHabit(Hero& h, bool good);           // adds a random habit of that kind (replacing an unlocked one at the cap); false if none could be added
std::string HabitList(const Hero& h);         // "Steady Hands, Jumpy; Salt Rot" for panels
void SuggestedKit(Location loc, int out[SUP_COUNT]);   // the Quartermaster's suggested kit (data.cpp)
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
    std::vector<RoomType> rooms;   // (the old linear run: unused since the sonar chart, kept for the boss simulator's one-room fights)
    int roomIndex = -1;
    // ---------- the sonar chart (chart.h)
    Chart chart;
    int curRoom = 0;               // where the party stands
    int walkEdge = -1, walkDest = -1, walkSeg = 0, walkSegs = 0; // the corridor being walked, and how far along it
    bool walkForward = true, walkRevisit = false;
    bool inHall = false;           // the fight under way is a hallway fight (in a corridor, not a room)
    int ambushSeg = -1;            // a revisited corridor's ambush: which segment it springs on (-1: none)
    Objective objective = Objective::Slay;
    bool objectiveDone = false;
    int fightsWon = 0;
    int minisMet = 0;              // mini-bosses met this expedition (at most MAX_MINIS_PER_RUN)
    std::map<int, int> dmgDealt;   // this fight: damage each hero has dealt (EnemyBrain's threat)
    std::map<int, int> lastAbility; // this fight: each enemy's last ability (boss scripts)
    int supply[8] = {};              // supplies carried (SUP_*)
    int fled = 0;                          // Stage 7: cowards who fled this fight (they take their share of the spoils)
    int pressure = 0;                      // Stage 7: the Trench's pressure this fight (-speed per point; a battery burn vents it)
    int campPoints = 0, campAmbush = 0;   // an open camp: points left to spend on camp skills, and the night-ambush chance
    std::vector<int> campUsed;       // camp skills used at this camp (indices into CampSkills())
    bool lullaby = false, lullabyActive = false; // a Siren's Lullaby: no nerves gained in the next fight
    EventKind event = EventKind::None;
    std::string eventTitle, eventBody;
    int eventStage = 0;            // 0 the choice, 1 the outcome shown
    int eventArg = 0;              // which curio (or shrine)
    bool eventAmbush = false;      // the outcome ends in a fight (a curio's ambush, a night attack at camp)
    int blessFights = 0;           // a shrine's blessing: the next few fights hit harder
    bool scopeOpen = false;        // the sonar scope pulled up full size while walking or fighting (Tab)
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
enum PlatLevel { PL_PIPES, PL_HULL, PL_PIRATE, PL_ISLAND, PL_CAVE, PL_WEEDS, PL_ATLANTIS, PL_COUNT };

// PersonalityProfile (rolled per creature from the level's seed) lives in beasts.h, with the living-AI engine.

struct PlatEnemy {
    char type; Vector2 pos, home; float dir, t;
    int state = 0;    // pirates: 0 hidden or idle, then emerging / stabbing / retreating, or aiming
    float timer = 0;  // time in the current state (or cooldown while hidden)
    Vector2 aim{0, 0}; // where a gunner is aiming
    // Ecosystem-framework retrofit (crabs and eels so far - see UpdateEnemies): rolled once at spawn from
    // the tile's own position, so a level's danger varies without changing what a creature fundamentally
    // is. Defaults to neutral for the pirate/gunner/parakeet types, which don't read it (their existing
    // proximity-gated state machines already give them real perception).
    PersonalityProfile personality;
};

// Ambient duct life for the Pipes (the level with no enemies - see CLAUDE.md - so this is life, not a
// hazard): little vermin that scurry along the floor, reacting to the diver with the same Flee/Investigate
// shape as the Abyss's ecosystem, but never touching or harming - see PopulateCritters/UpdateCritters.
enum class CritterState { Idle, Fleeing, Investigating };
struct PlatCritter {
    Vector2 pos{0, 0}, home{0, 0};
    PersonalityProfile personality;
    float dir = 1;                        // -1 left, 1 right: which way it's currently facing/walking
    CritterState state = CritterState::Idle;
    float stateTimer = 0, phase = 0;      // phase: per-critter offset so a cluster doesn't move in lockstep
};
// (Every level's creatures run on the living-AI engine - see beasts.h / PlatformState::fauna.)
struct PlatShot { Vector2 pos, vel; float life; int kind; }; // 0 musket ball, 1 lit bomb, 2 explosion, 3 falling ink, 4 torpedo, 5 cannonball, 6 rolling barrel
struct PlatLauncher { int tx, ty; char type; float t; }; // a torpedo tube (T), a deck cannon (N) or a barrel chute (y): fires on a timer, with a warning before
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
enum class KrakenMove { TentacleSlam = 0, InkFlood = 1, Lunge = 2, TentacleReach = 3, InkRain = 4, BeakCharge = 5, Sweep = 6 };

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
    float sweepT = -1, sweepY = 0, sweepDir = 1;  // Kraken sweep: an arm swung flat across the arena at head height (slide under it); < 0 = idle
    bool defeated = false;
};

struct PlatformState;
void PlatStepPlayer(PlatformState& p, float dir, bool jumpHeld);   // (platformer.cpp's StepPlayer, for the Trawl's dive scene)
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
    std::vector<PlatCritter> critters; // ambient duct life (Pipes only) - see PlatCritter; never a hazard
    BeastWorld fauna;                       // the living-AI creatures (beasts.h), every level
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
    std::vector<PlatShot> shots;     // musket balls, bombs, torpedoes, cannonballs and barrels in flight
    std::vector<PlatLauncher> launchers;
    int pickupPct = 0, speedPct = 0, jumpPct = 0, lampPct = 0; // from the lead hero's relics (see RelicFx)
    bool hard = false;               // Hard keeps the gears and jets; Normal leaves them out
    bool checkpoints = false;        // respawn at the last section reached, but forfeit the relic
    bool bossEnabled = true;         // Hull/Pirate: whether the boss arena has its boss in it
    float waterY = 0;                // the sea's surface in world pixels (the Pirate Ship); 0 when there is none
    bool onWeed = false;             // the diver is among seaweed: it slows a fall and can be climbed
    int climbDir = 0;                // -1 up, 1 down: held keys, read only while on weed (never set by the path search)
    // The diver's extra moves (ParkourReference1.2, "Traversal States"). All are driven by inputs the path search never
    // presses, so --verify sees exactly the old movement; see StepPlayer.
    bool inDown = false, upHeld = false, shiftHeld = false; // held this frame (player input only)
    int dashReq = 0;                 // a double-tap asked for a dash this frame: -1 left, 1 right
    int pose = 0;                    // 0 normal, 1 slide, 2 roll, 3 stunned, 4 brake, 5 dash, 6 balance on a pole tip, 7 hydro-glide, 8 backflip, 9 ledge hang
    float moveT = 0;                 // time left (or spent) in the current pose
    float fallTop = 0;               // the highest point of the current fall (a long drop stuns; a short one never does)
    unsigned long long seenAtStart = 0; // the dossier's entries for this level when the dive began: a species new to it gets a first-meeting hint
    std::vector<std::pair<int, float>> hinted; // (species, when it was first met this dive)
    struct BgHunt { int pred = -1, prey = -1; float start = 0, dur = 7, x0 = 0, y0 = 0, worldX = 0, depth = 0.4f; int dir = 1; bool caught = true; };
    BgHunt bgHunt; float bgHuntNext = 10; // the food web at work in the middle distance (DrawBackgroundWeb): a real pairing from the level's web, now and then
    float boostT = 0;                // a boosted move's extra speed isn't clawed back in the air until this runs out
    bool dashReady = true;           // one dash per jump: back on landing, on a wall, a pole or a ledge
    Vector2 dashDir{0, 0};
    float downBuf = 0;               // Down was pressed moments ago: the window for an impact roll
    int ledgeTx = 0, ledgeTy = 0;    // the ledge being hung from
    // ParkourReference1.3: the creatures and flora push back on the diver (all set by beasts.cpp, never while verifying)
    float slickT = 0;                // crushed barnacle-mite slime on the suit: less drag, a higher top speed for a moment
    struct Mover { Rectangle r; Vector2 vel; };
    std::vector<Mover> movers;       // moving solid tops the diver can land on and ride (a grazing whale's back)
    int onMover = -1;                // the mover being ridden
    bool anchored = false;           // holding hull-kelp or a pole, hanging on a ledge, or under a low ceiling: turbulence can't tear you loose
    std::vector<GenSnap> snaps;      // Pirate Ship: the proven places the Grand Kraken may snap a ship (layout[3] is the proof mask)
    std::vector<uint8_t> snapped;    // and which of them it already has
    std::string deathCause, deathTip; float causeT = 0;
    float dayOffset = 0;             // where this dive starts in the 4-minute day/night cycle of the backgrounds (random each dive) // what killed the diver last, and one way to beat it (shown a few seconds)
    bool ghost = false;              // the rare Ghost Ship: undead crew, fog, and everything 1.6x faster
    struct Crumble { int tx, ty; float t; };
    std::vector<Crumble> crumbles;   // fragile scaffolding that has been stepped on and is shaking
    std::vector<std::pair<int, int>> crumbled; // and what has already fallen, so a checkpoint respawn can put it back
    int genTop = 0;                  // generator row 0 sits at this row offset (negative when the arena rises above it)
    bool verifying = false;          // the path search drives the real movement: nothing may permanently change the tiles
};

// ---------- The Open Abyss: a fully-3D vertical descent biome, separate from the 2D platformer above ----------
constexpr float ABYSS_DEPTH_SPAN = 900.0f; // how far down this vertical slice's trench is generated, in metres
constexpr int ABYSS_PAYOUT = 400;          // gold at the bottom - the deepest dive, so it pays the most

enum class AbyssCreatureState { Cling, Idle, Dislodged, Hunting, Fleeing, Investigating, Shattered, Lunging, Passing };
enum class AbyssCreatureKind {
    GlassSponge, GiantIsopod, GulperEel, BioPlankton, VampireSquid, Siphonophore,
    Leviathan,   // a colossal background presence; mostly a suggested silhouette, rarely a close, dangerous pass
    TrenchWorm,  // lunges out of the wall at anything that lingers nearby
    Hatchetfish, // harmless ambient schooling prey - just life in the water, no threat, no reward
    BrineSlug,   // slow, heavy, crushing - the hazard that makes the bowling-lane set-piece work
    RockLedge,   // a plain, permanent stone shelf: unlike a Glass Sponge it never shatters, so a diver can
                 // always find somewhere to wait out whatever's patrolling below
    // ParkourReference1.3, "7. The Abyss"
    WhaleFall,        // the Whale-Fall Scavenger: a gargantuan armoured isopod on a whale skeleton; its flat back is cover from suction
    AnglerCephalopod, // hides in void-moss, dangling a lure that looks like ghost-kelp; comes too close and its beak has you
    PressureGhost,    // a vast translucent jellyfish that swells, then inhales: violent suction toward it
    TrenchMaw,        // the apex: a colossal gulper rising up the shaft, jaw wider than the trench; a pressure-bulb's blast drives it back
    TrenchKrill,      // swarms that only light up in your wake, tracing the currents
    SlimeHagfish,     // drawn by blood; their mucus makes the water around them slick - you fall faster there
    VentWorms, VoidMoss, PressureBulb, AbyssalCoral, GhostKelp, // the flora
};

// A set-piece band of depth, generated once from the level seed (see PopulateEcosystem): the trench's own
// geometry (TrenchRadius) is left untouched by these - they're layered on as physics/behaviour effects so the
// proven, verified wall collision never has to change shape underneath them.
enum class AbyssZoneKind { Vent, SiphonophoreMaze, BrinePool, BowlingLane, Cavern };
struct AbyssZone {
    AbyssZoneKind kind = AbyssZoneKind::Vent;
    float depth = 0, span = 40;   // the band this zone covers: [depth, depth + span)
    float angle = 0;              // Vent: the up-draft's centre angle around the shaft
    float strength = 1.0f;        // Vent: updraft force scale; BrinePool: how much gravity is cut
};

struct AbyssCreature {
    AbyssCreatureKind kind = AbyssCreatureKind::GlassSponge;
    PersonalityProfile personality;
    AbyssCreatureState state = AbyssCreatureState::Idle;
    float wallAngle = 0;      // position around the shaft's circumference, radians
    float depth = 0;          // how far down the shaft (world -Y)
    float radiusOffset = 0;   // how far the creature sits from the shaft wall (ledges reach inward)
    Vector3 pos{0, 0, 0};
    Vector3 vel{0, 0, 0};
    Vector3 home{0, 0, 0};    // TrenchWorm/BrineSlug: the resting spot a lunge/crawl returns to
    float health = 1.0f;      // sponges: shatter (state -> Shattered) at 0; others: simple hit points
    float stateTimer = 0;
    float phase = 0;          // per-entity animation/wobble offset so a school doesn't move in lockstep
    bool alive = true;
};

struct AbyssPlanktonPuff { Vector3 pos{0, 0, 0}; float life = 0, maxLife = 1.3f, radius = 40; };
// A single mote of marine snow: ambient drift that streaks (and telegraphs a coming downdraft) rather than
// just sitting there looking dusty.
struct AbyssSnowMote { Vector3 pos{0, 0, 0}; float speed = 1; float phase = 0; };

struct AbyssState {
    unsigned seed = 1;
    Vector3 playerPos{0, 0, 0};
    Vector3 playerVel{0, 0, 0};
    float yaw = 0;                    // facing direction around the shaft, for dash/glide aim
    float stamina = 100.0f;
    bool isDashing = false;
    float dashTimer = 0, dashCooldown = 0;
    float tapTime[4] = {-10, -10, -10, -10}; // last press time of right/left/up/down, for double-tap-to-dash
    float camYaw = 0, camPitch = 0;   // free-look orbit around the player, mouse-controlled, clamped to the trench
    bool isGliding = false, glidingUp = false, glidingDown = false;
    float depth = 0;                  // world -Y of the player: how far the descent has gone
    float bestDepth = 0;
    bool downdraftActive = false;
    float downdraftTimer = 0;
    float hazardIFrame = 0;           // brief immunity after any hazard hit, so one crush doesn't chain into ten
    float invisT = 0;                 // dashed through void-moss: visual hunters can't see you
    float lastHitT = 0;               // when the last hazard hit (the director waits for calm)
    float mawT = 0; int mawVisits = 0;  // the Trench-Maw director
    float genDepth = 0;               // deepest point the generator has populated so far (streams downward)
    std::vector<AbyssCreature> creatures;
    std::vector<AbyssPlanktonPuff> puffs;
    std::vector<AbyssZone> zones;
    std::vector<AbyssSnowMote> snow;
    float time = 0;
    bool dead = false;
    bool won = false;                 // reached the bottom of this vertical slice's trench
    bool awarded = false;             // the won/dead payout has already been applied to Game (don't double-pay)
    bool verifying = false;           // headless self-test: no window/audio/frame timing assumptions
};

struct MemorialEntry { std::string name; int cls = 0, level = 0; std::string cause; };
enum VoyageEvent { VE_SALVAGER, VE_STORM, VE_HABIT_FLARE, VE_CARD_SHARP, VE_COUNT };
struct Game {
    Scene scene = Scene::Hub;
    int gold = 60; // kept deliberately scarce: parkour runs and Flats are meant to make up the difference
    int batteries = 2;
    int provision[8] = {};               // supplies bought at the Helm for the next expedition (SUP_*); refunded if put back
    std::map<std::pair<int, int>, int> bonds;   // Stage 7 crew bonds, 0-BOND_MAX, keyed by (lower hero id, higher hero id)
    std::vector<MemorialEntry> memorial;       // Stage 7: the dead, on the Library's memorial wall
    std::vector<std::string> seaLog;           // Stage 7: boss kills, curio outcomes, first meetings (newest last)
    unsigned long long enemiesMet = 0;         // bit = EnemyType, for the Sea Log's first meetings
    int voyageEvent = -1;                      // Stage 7: the event waiting in the salon on return (VoyageEvent), -1 none (not saved)
    int stormUpgrade = -1;                     // an upgrade knocked offline by a storm for the next expedition
    std::vector<int> salvagerStock;            // the passing salvager's relics (not saved)
    int flareHero = -1;                        // the hero whose habit flared (not saved)
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
    int upgrades[UP_COUNT] = {0, 0, 0, 0, 0};
    std::vector<int> platLayouts[PL_COUNT]; // which chunks make up each platform level's current layout
    bool platCleared[PL_COUNT] = {};
    float platBest[PL_COUNT] = {};    // best clear time in seconds (0 = never cleared)
    bool platHard = false;                   // Periscope option: the full-strength layouts
    bool platCheckpoints = false;            // Periscope option: checkpoints, at the cost of the relic
    bool platHullBoss = true;                // Periscope option: fight the Kraken (only chance of a relic)
    int periscopeSel = 0;                    // which dive the Periscope's chart has open (not saved)
    bool mourning = false;                   // a crew member died on the last expedition: the salon's music is the organ alone until the next one (not saved)
    bool platPirateBoss = true;              // Periscope option: fight Blackbeard (guarantees relic(s))
    bool abyssCleared = false;               // reached the bottom of the Open Abyss's trench at least once
    float abyssBest = 0;                     // best depth ever reached there (metres), 0 = never dived
    unsigned long long platSeen[PL_COUNT + 1] = {}; // the Periscope dossier: which species (bit = species index; [PL_COUNT] = the Abyss's kinds) you've met
    int dossier = -1;                        // the dossier open on the Periscope (a level index, PL_COUNT = the Abyss), -1 = closed
    int dossierPick = -1;                    // the entry selected in it
    int tierCleared[LOCATION_COUNT] = {-1, -1, -1, -1, -1, -1}; // highest tier index beaten, per location (-1 = none)
    int tierSel[LOCATION_COUNT] = {0, 0, 0, 0, 3, 3};   // the tier chosen at the Helm, per location (the deep ones start at tier 3)
    Objective objectiveSel = Objective::Slay;             // the objective chosen at the Helm for the next expedition (not saved)
    std::string toast;
    float toastTimer = 0;
    float time = 0;
    DungeonState dungeon;
    PlatformState plat;
    AbyssState abyss;
};

// ---------- data.cpp ----------
void InitGame(Game& g);
int BondOf(const Game& g, int a, int b);
int Stage7Test();
// the deep tiers (Stage 7): tier ranges, names, unlocking, and which Shallows location each borrows its scenery from
bool DeepLocation(Location l);
int TierFirst(Location l);
int TierLast(Location l);
int TierUnlocked(const Game& g, Location l);    // the deepest tier open at the Helm
bool DeepUnlocked(const Game& g);
const char* TierName(Location l, int tier);
Location VisLoc(Location l);
extern int gSimLocation;                       // --sim's location (dungeon.cpp)
void SeaLog(Game& g, const std::string& s);
int Upg(const Game& g, int u);                 // an upgrade's working level (a storm can knock one offline)
void RollVoyageEvent(Game& g);                 // on return to the salon (not in --sim)
const char* EnemyHint(int enemyType);          // the Sea Log's first-meeting note
extern const int VOYAGE_EVENT_PCT, SALVAGER_PRICE_PCT, SHARP_PRIZE, HABIT_FLARE_NERVES, SEA_LOG_MAX;
void FlatsCardSharp(bool on);                  // flats.cpp: the card sharp's free battle is offered at the table
void DrawVoyageEvent(Game& g);                 // hub.cpp                              // --stage7-test (dungeon.cpp)
void AddBond(Game& g, int a, int b, int n);
const std::vector<Ability>& ClassAbilities(HeroClass c);
const char* ClassName(HeroClass c);
const char* ClassBlurb(HeroClass c);
Color ClassColor(HeroClass c);
Hero MakeHero(Game& g, HeroClass c);
std::string HeroBuildTag(const Hero& h);   // "Vigorous", "Frail, Quick", ... a read on which stat axes stand out
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
bool DrawRichEnemy(const Enemy& e, Rectangle r, float t);
// ---------- rigfigs.cpp: figures rebuilt on the shared rig (rig.h) ----------
void DrawRigCaptain(const Hero& h, Vector2 feet, float s, bool faceRight, float walk, float t, const Pose& pose);
void DrawRigNurse(const Hero& h, Vector2 feet, float s, bool faceRight, float walk, float t, const Pose& pose);
void DrawRigDiver(const Hero& h, Vector2 feet, float s, bool faceRight, float walk, float t, const Pose& pose);
void DrawRigMechanic(const Hero& h, Vector2 feet, float s, bool faceRight, float walk, float t, const Pose& pose);
void DrawRigWhaler(const Hero& h, Vector2 feet, float s, bool faceRight, float walk, float t, const Pose& pose);
void DrawRigStowaway(const Hero& h, Vector2 feet, float s, bool faceRight, float walk, float t, const Pose& pose);
void DrawRigMerman(const Hero& h, Vector2 feet, float s, bool faceRight, float walk, float t, const Pose& pose);
void DrawRigOctopus(const Hero& h, Vector2 feet, float s, bool faceRight, float walk, float t, const Pose& pose);
void DrawRigQueen(const Hero& h, Vector2 feet, float s, bool faceRight, float walk, float t, const Pose& pose);
void DrawRigRobot(const Hero& h, Vector2 feet, float s, bool faceRight, float walk, float t, const Pose& pose);
void DrawRigSiren(const Hero& h, Vector2 feet, float s, bool faceRight, float walk, float t, const Pose& pose);
void DrawRigWisp(const Hero& h, Vector2 feet, float s, bool faceRight, float walk, float t, const Pose& pose);
void DrawRigCultist(const Enemy& e, Rectangle r, float t);
void DrawRigLostDiver(const Enemy& e, Rectangle r, float t);
void RigSetActing(int clip, float t);
void RigGetActing(int* clip, float* t);
void RigAfterInk(std::function<void()> fn);   // rigfigs.cpp: light a figure paints over itself after the ink pass
void RigRunAfterInk(Vector2 canvasToScreen);
void DrawFigureSheet(bool heroSheet, int index, float t); // dungeon.cpp: one figure in idle, walk, windup, strike, hit, death   // the combat clip (rig::ClipId, -1 none) and its time for the next enemy drawn      // the richly drawn creatures (enemyart.cpp); false = not one of them yet
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
int CargoBonusSlots(const Game& g);
const char* UpgradeName(int u);
const char* UpgradeDesc(int u, int level); // what the given level does
int UpgradePrice(int level);                // price to buy the given level
int LoadoutCount(const Hero& h);
void ScaleEnemyForTier(Enemy& e, int tier);
int ChartTrapSpotChance();
int ChartRevisitAmbush();
int ChartNightAmbush();

// ---------- save.cpp ----------
bool SaveGame(const Game& g);
std::string SavePath();
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
RenderTexture2D& Mode3DRT(); // a real depth-buffered target for BeginMode3D scenes (the Abyss), composited in like any other texture
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
void BeginUiLayer();            // draw a UI into its own layer...
void EndUiLayer(float alpha);   // ...and lay it over the scene at this opacity (the Study's focus mode)
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

// ---------- the painterly light rig (Master Reference, "Shared visual direction") ----------
// One strong key light per scene (the flashlight, the salon's lamps), a cool fill from the water, a rim on the side
// away from the key, and a fog colour for the veils between parallax layers. The figure shader and the rig's
// materials read it; scenes set it each frame (BeginFrame resets it to a neutral default).
struct SceneLight {
    Vector2 keyDir{-0.55f, -0.83f};      // screen-space direction TOWARD the key light
    Color key{255, 232, 196, 255};       // warm lamp light on the lit side
    Color fill{110, 150, 176, 255};      // cool water light filling the shadow side
    Color rim{150, 214, 230, 255};       // the edge light away from the key
    Color fog{24, 52, 62, 255};          // the scene's fog, between layers
    float keyAmt = 1, fillAmt = 1, rimAmt = 1;
};
void SetSceneLight(const SceneLight& l);
const SceneLight& CurSceneLight();
void SetFigureFacing(float facing); // while a figure is drawn: its key light comes from the side it faces (0 = off)
// A location's muted paper palette: five base tones (umbers, slates, sea-greens) and two hot accents. InkPass pulls
// low-saturation colour toward the palette's hues (keeping each pixel's value), so every scene sits in its family.
struct Palette { Color base[5]; Color accent[2]; };
const Palette& LocationPalette(Location loc);   // data.cpp
const Palette& SalonPalette();
SceneLight LocationLight(Location loc, float light01); // the flashlight is the key; low light drops the fill, raises the rim, closes the fog
SceneLight SalonLight();
void SetInkLook(const Palette* pal, float palAmt, unsigned seed); // the next InkPass: palette pull, and ink flecks fixed per seed
void FogVeil(float amount);    // a veil of the scene's fog colour over everything drawn so far (between parallax layers)
extern bool gSilhouette;       // --silhouette: figures render solid black, to check they read from their shape alone

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
void DrawCabinBackground();   // station screens: the salon behind (dimmed), and the panel slides in from the station's side
void EndStationPanel();       // RunScene: end the slide-in offset DrawCabinBackground set (safe to call always)
void SetSceneSlide(Vector2 d); // render.cpp: offset everything drawn into the scene from now on (a panel sliding in)
void DrawSalonBackdrop(Game& g, Scene station); // salon.cpp: the room as a backdrop behind a station's panel (no hover, no HUD)
void SceneStudy(Game& g);     // the Study below the hatch (under refit for now)
void SceneArcade(Game& g);    // the Deep Arcade cabinet (arcade.cpp): the reels, Host / Join / Browse, the lobby, Scuttle; solo launches for the Trawl and Red Tide
void DebugArcadeShot(int which);   // shots: 0 the lobby, 1 the Scuttle table (AI crabs, no network)
void DebugArcadeReel(int reel);    // --shots: turn the arcade drum to a reel (0 Flats Duel ... 4 Red Tide)
int RunScuttleSim(int matches);    // --scuttle-sim (net_test.cpp)
int RunNetLoop(int lagMs, bool forceMemory);   // --net-loop
int RunBetTest();                                // --bet-test
int RunStudyAudioTest(const char* wavPath, float seconds);   // --study-audio-test (study_test.cpp)
void SceneTrawl(Game& g);     // The Trawl, the Deep Arcade's co-op fishing horror game (trawl.cpp)
void StartTrawl(Game& g, bool firstPerson = false, int crew = 1, int botSkill = 1);
void StartTrawlShakedown(Game& g, bool firstPerson = false);   // the tutorial night with Kess (design doc, "First night")
namespace arcade { class Session; }
void StartTrawlNet(Game& g, arcade::Session* net, bool firstPerson = false);   // a Deep Arcade match of the Trawl (host or guest)
void LeaveTrawlMatch(Game& g);                                                 // back to the arcade (the host takes the table back to the lobby)
void TrawlMenuTick(float dt);                                                  // (the game menu is open) a networked Trawl keeps talking   // crew 1-6 (the rest are bots), botSkill 0 Green, 1 Able, 2 Old Hand   // firstPerson: the 3D version (trawl_view3d.cpp); V switches in game
void DebugTrawlShot(Game& g, int which);   // --shots: 0 the deck, 1 the engine room, 2 the wheelhouse, 3 a squall
void SceneFlight(Game& g);    // The Flight, arcade game 7: the Founder flown over the tropical island (flight_game.cpp)
void SceneScuffle(Game& g);    // Scuffle, arcade game 9: the stick fight (scuffle_game.cpp)
void StartScuffle(Game& g, int bots = 3, int skill = 2, int toWin = 5, int world = -1);   // (world: -1 all six, 0-5 one, 6 endless from the generator)
void LeaveScuffle(Game& g);
void StartScuffleNet(Game& g, arcade::Session* net, const char* name);   // a networked match (host or guest; scuffle_net.cpp)
void ScuffleMenuTick(float dt);                                          // (the game menu is open: the fight goes on)
std::string ScuffleOpts(int toWin, int arsenal, int skill, int world = -1, uint32_t mutators = 0, bool randomMutator = false);
const char* ScuffleWorldChoice(int world);
extern int gScuffleTrinket, gScuffleRules, gScuffleMode;   // (gScuffleMode: sf::Mode, the solo panel's and the lobby's pick)   // (the arcade's picks: your trinket, -1 the game picks; the rules: 0 none, 1 Random, 2+ one mutator)
const char* ScuffleTrinketChoice(int t);
const char* ScuffleRulesChoice(int r);
uint32_t ScuffleRulesMask(int r);
void StartScuffleEditor(Game& g);   // the level editor (doc p. 17): paint, check, share by code, play now
void DebugScuffleEditorShot(Game& g, int which);
void DebugScuffleShot(Game& g, int which);
void SceneNightOff(Game& g);  // A Night Off, arcade game 6: one night ashore at the Sodden Gull (nightoff_game.cpp)
void StartNightOff(Game& g, int crew = 0, int mode = 0, int crowd = 1, int bar = 0, int season = 0);
namespace arcade { class Session; }
void StartNightOffNet(Game& g, arcade::Session* net, const char* name, int crew);   // a networked night (host or guest)
std::string NightOffOpts(int mode, int crowd, bool pvp, int bar = 0, int season = 0);
bool NightCloakroomPage();                           // A Night Off's cloakroom over the arcade: the store, the brass wheel, the wardrobe (true: Back)
void DebugNightCloakroom(int which);                 // (--shots: 0-5, a demo skin)
extern bool gNoCloakShot;                            // (--shots: open the cloakroom over the arcade)                           // the host's options for the session
void LeaveNightOff(Game& g);
void NightOffMenuTick(float dt);
void DebugNightOffShot(Game& g, int which);
bool NightOffOwnsEsc();        // a conversation, a game or the menu at the bar takes Esc before the game menu does
void SceneMouthful(Game& g);  // Mouthful, arcade game 8: eat and grow on the reef shelf (mouthful_game.cpp)
void StartMouthful(Game& g, int bots = 11, float minutes = 15, int botLevel = 0);   // solo: you and up to 11 bots (botLevel 0 a mix of Minnow/Hunter/Shark)
void LeaveMouthful(Game& g);
void StartMouthfulNet(Game& g, arcade::Session* net, const char* name);
bool MouthfulWardrobePage();                      // the shop, the crate, a skin per path (true: Back)
void DebugMouthfulWardrobe();
void StartMouthfulMode(Game& g, int bots, float minutes, int botLevel, int mode, int path);   // a Deep Arcade round of Mouthful (host or guest)
std::string MouthfulOpts(int minutes, int botLevel, int fill, int mode = 0, int path = 2);
void MouthfulMenuTick(float dt);
void DebugMouthfulShot(Game& g, int which);
void StartFlight(Game& g, const char* founder = "taloned", int isleType = 0, int arrangement = 0, int players = 4, int seasons = 0);   // seasons: the long match (2-4), 0 standard   // a whole map: your island type (0 tropical, 1 stack, 2 town, 3 atoll), the arrangement, 2-6 starting islands
const char* FlightIsleTypeName(int t);
bool ResumeFlight(Game& g);                      // (the Long Flight: the save made at the last season's break)
bool FlightResumable();
const char* FlightArrangementName(int a);
void LeaveFlight(Game& g);
void StartFlightNet(Game& g, arcade::Session* net, const char* founderKey, const char* name);   // a Deep Arcade match of the Flight (host or guest)
void FlightMenuTick(float dt);                                                                 // (the game menu is open) a networked Flight keeps talking
void DebugFlightShot(Game& g, int which);   // --shots: 0 dawn over the lagoon, 1 the strike, 2 the nest, 3 high over the island
int FlightFounderCount();
bool FlightWardrobePage(int founder);            // the Roost wardrobe over the arcade (true: Back)
void DebugFlightWardrobe(int tab, const char* pick);   // (--shots)
void DrawFlightCostumeGallery(int page);         // (--shots: every costume, page by page)
void DebugArcadeFlightWardrobe(Game& g, int tab, const char* pick, int galleryPage);
const char* FlightFounderName(int i);
const char* FlightFounderKey(int i);
const char* FlightFounderLine(int i);
void SceneRedTide(Game& g);   // Red Tide, the Deep Arcade's survival shooter (redtide_game.cpp)
void StartRedTide(Game& g, const char* map = "ship");
void StartRedTideNet(Game& g, arcade::Session* net);   // a Deep Arcade match of Red Tide (host or guest)
void SetRedTideMode(int mode);                         // the modes (design doc "Modes"): the next solo dive's
int RedTideModeCount();
const char* RedTideModeName(int mode);
const char* RedTideModeRules(int mode);
const char* RedTideModeKey(int mode);                  // a host's option: "<map>:<mode key>:<season>"
bool RedTideLongNightSaved(const char* map, int* tide = nullptr, float* time = nullptr);   // a Long Night kept on this machine (the host's)
void RedTideClearLongNight(const char* map);
// voice chat's glue (arcade.cpp): every frame from the main loop; the "who's talking" strip; the settings page's bits
void ArcadeVoiceFrame(float dt);
// the pre-match bets: each game says, once its match is over, which lobby seats won (false while it isn't over)
bool RedTideBetWinners(std::vector<int>& seats);
bool TrawlBetWinners(std::vector<int>& seats);
void DrawVoiceHud();
bool& VoiceMicTest();
float VoiceMicLevel();
bool VoiceTalking();
const std::string& VoiceMicError();
int VoiceMySeat();
bool VoiceSpeaking(int seat);               // (me: my own push-to-talk or gate)
const char* VoiceSeatName(int seat);
void SetRedTideResume(bool resume);
void SetRedTideCustom(const std::string& rules);         // Custom mode's rules (solo dives and the lobby's option)
std::string RedTideCustomRules();
bool RedTideCustomPanel();                               // the rules panel over the arcade; true when closed                      // the next solo Long Night resumes the saved one
void SetRedTideSeason(int season);                     // a species season (design doc "Species seasons"; 0 none)
int RedTideSeasonCount();
std::string RedTideSeasonName(int season);
void LeaveRedTideMatch(Game& g);                       // back to the arcade (a host takes the table back to the lobby; a guest's diver goes on as a bot)
void RedTideMenuTick(float dt);                        // (the game menu is open) a networked match keeps talking
bool RedTideAudioActive();     // a Red Tide match is playing (its own music, not the salon's)
void OpenRedTidePage(Game& g, int page);    // the arcade's Red Tide pages: 1 dossier, 2 records, 3 how to play, 4 charms, 5 locker (redtide_menu.cpp)
namespace rt { void DebugRedTidePage(int page, int map, int sel); }
void DebugRedTideShot(Game& g, int which); // --shots: 0 the tank, 1 its silhouettes, 2+ the species lineup pages
int RunRedTideTest();                       // depth.exe --redtide-test (headless)
void SetFigureClip(const Rectangle* r); // clip the next EndFigure composites to r (portraits); nullptr turns it off
void SetFigureMood(float desat, float door);
void SnapshotFrame();                 // render.cpp: hold the last finished frame (the game menu's pause)
void DrawSnapshot();
void SetPostBypass(bool on);         // this frame skips the post pass (it's showing an already finished frame)
bool GameMenuActive();               // menu.cpp: the game menu (Esc)
void GameMenuOpen();
void GameMenuFrame(Game& g);
bool GameMenuWantsQuit();
void DebugMenuPage(int page);          // --shots: 0 main, 1 settings, 2 controls
void ResetSalonLife();               // salon.cpp: the ship's hands start over (a new game) // the next figures: greyed (0..1) and a red rim (Death's Door); set back to 0, 0 after
void SceneCamera(Vector2 focus, float zoom, Vector2 offset); // re-lay the stage drawn so far: the combat camera
void DrawPortrait(const Hero& h, Rectangle r, float t); // head and shoulders of a crew member, clipped to r
void DebugSalonHover(int station); // --shots: hold a salon station hovered (-1 off)
extern Game* gCurrentGame;    // the game RunScene is drawing (for backdrops that need it)
std::string RankString(int mask);
extern bool gDiveGear;   // true only on an expedition: the crew wear masks and helmets there and take them off aboard the Nautilus

// ---------- hub.cpp ----------
// sprite sheet pages (depth.exe --sprites)
void DrawSalonSpritePage(float t);
void DrawCrewSpritePage(float t);
void DrawCrewGalleryPage(HeroClass cls, float t);   // developer tool: one class filling the whole page
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
void DebugFlatsCombat();            // debug: the first strike of a combat
void DebugFlatsWon();               // debug: the battle-won panel
void DebugFlatsBoon();              // debug: the momentum choice
void DebugFlatsMap();               // debug: the run map
void DebugFlatsCampfire();          // debug: the campfire screen
void DebugFlatsVents();             // debug: the new map events and the boss, and the showcase pages of every card and component
void DebugFlatsSplicers();
void DebugFlatsScrimshaw();
void DebugFlatsBarnacle();
void DebugFlatsMaelstrom();
void DebugFlatsBoss(int phase);
void DebugFlatsShowcase(int page);
void DebugFlatsAutoplay(int battles);   // developer self-test (depth.exe --flats-ui-test N)
bool FlatsAutoplayActive();
int FlatsCatalogSize();
const char* FlatsCardName(int i);
void DebugFlatsShop();              // debug: the dealer's stall
void DebugFlatsDeck();            // debug: the deck viewer
void DebugFlatsDeal();            // debug: skip the menu and deal a mid-round hand (for screenshots)
void DrawItemSpritePage(float t);   // the sprite sheet page of carried items and relic icons
bool GenerateSirenArt();            // developer tool (depth.exe --gen-siren-art): paints assets/characters/siren/*
bool GenerateAllCrewArt();          // developer tool (depth.exe --gen-crew-art): the Siren plus all 11 other classes
void SceneHelm(Game& g);
void SceneCrew(Game& g);
void SceneRadar(Game& g);
void SceneWard(Game& g);
void SceneSickLeave(Game& g);
void SceneBookshelf(Game& g);
void ScenePeriscope(Game& g);
bool DossierButton(Rectangle r);   // dossier.cpp: the file-folder button on the Periscope
bool DrawDossier(Game& g);          // ...and the open dossier
const char* BeastHint(const char* name); // dossier.cpp: what a plant or creature is for, in a line (first-meeting hints)
void ParkourAudio(const PlatformState& p, float dt); // sound_parkour.cpp: the platform levels' sound cues
void AbyssAudio(const AbyssState& a, float dt);
void DrawVolumeSliders(Rectangle r);  // master / music / effects / ambience, on the Periscope
void SceneWorkshop(Game& g);
void DebugWorkshopTab(int t);   // --shots
void DebugHelmDeep();           // --shots

// ---------- dungeon.cpp ----------
void StartDungeon(Game& g, Location loc);
void SceneDungeon(Game& g);
void DebugEnterCombat(Game& g, Location loc = Location::Cave); // debug: jump straight into the first fight
void DebugChartWalk(Game& g, int dest);   // --shots: walking a corridor on the chart
void DebugChartEvent(Game& g, int kind);  // --shots: 1 a curio, 2 a camp
void SimulateBossFight(int runs, int level, int tier, int enemyType, bool randomPlayer); // debug: one boss, many fights
void SimulateExpeditions(int runs, int level, bool randomPlayer, int tier = 0); // debug: auto-play expeditions and print the results

// ---------- platformer.cpp ----------
void GeneratePlatLayout(Game& g, int level);
bool PlatLayoutValid(const Game& g, int level);
std::string PlatLayoutCode(const Game& g, int level);
const char* PlatLevelName(int level);
// Tile and effect helpers the creature engine (beasts.cpp) borrows from the platformer.
bool PlatSolid(const PlatformState& p, int tx, int ty);
char PlatTileAt(const PlatformState& p, int tx, int ty);
Rectangle PlatDiverBox(const PlatformState& p);
void PlatBurst(PlatformState& p, Vector2 at, int n, Color c, float speed, float life, float size);
void PlatBubbles(PlatformState& p, Vector2 at, int n);
void PlatBuildLevel(PlatformState& p);
void PlatSnapShip(PlatformState& p, int k); // the Grand Kraken breaks proven snap point k (tiles, enemies, launchers, the diver, the beasts' map)
void StartPlatform(Game& g, int level, bool freshLayout = false); // freshLayout: a brand-new random level (every dive from the Periscope)
void ScenePlatformer(Game& g);
int VerifyPlatformLevels(); // debug: proves every section can be crossed; returns the number that can't

// ---------- abyss.cpp ----------
void StartAbyss(Game& g);
void SceneAbyss(Game& g);
void UpdateAbyss(Game& g, float dt); // the fixed-step simulation, callable headlessly for --verify
bool VerifyAbyss();                  // debug: proves a run can descend past the first downdraft/sponge gauntlet
bool VerifyMoves();                  // debug (depth.exe --verify-moves): the diver's extra moves, through the real physics step
bool VerifyCritters();               // debug (depth.exe --verify-critters): proves the Pipes' ambient duct life spawns and reacts
