#pragma once
// ============================================================================
//  THE FLIGHT - arcade game 7 of the Deep Arcade (Reference_For_Future_MP_Games/"The Flight - Arcade Game 7 Design
//  Document"; progress in docs/FLIGHT_PROGRESS.md). A 2-6 player 3D RTS in which each player is a bird: the Founder,
//  flown in third person, fishing the living sea (Red Tide's ecosystem engine) to feed a colony.
//
//  This header is the headless core (no drawing): the founders' data, the island, the wind, the Founder's flight,
//  the strike, carrying, the nest's cache, hunger, death and the chick-leader. Stage 1 of the design's build order:
//  "Flight: the Founder's body, wind, stamina, altitude, the dive and strike, carrying; one tropical island and a
//  lagoon of fish on Red Tide's engine".
//
//  World units are metres; y is up and the sea's surface is y = 0; the island stands at the origin.
// ============================================================================
#include "raylib.h"
#include "redtide.h"
#include <memory>
#include <string>
#include <vector>

namespace fl {

// ---------------------------------------------------------------- the twelve founders (data/flight/flight_founders.json)
struct FounderDef {
    std::string key, name, bird, boost, unique, weakness, playstyle;
    float cruise = 12, sprint = 20, stamina = 8, talon = 1, attack = 20, hp = 100;
    int carry = 3;                          // the largest fish size class it lifts
    float span = 1.5f, beak = 0.1f, tail = 0.5f, crest = 0;   // the look: wingspan (m), beak, tail, crest
    Color back{120, 120, 120, 255}, belly{230, 230, 230, 255}, accent{40, 40, 40, 255};
};
const std::vector<FounderDef>& Founders();
int FounderIndex(const std::string& key);   // -1 if unknown
std::string FlightDataDir();                  // data/flight (found like Red Tide's)

// ---------------------------------------------------------------- the islands (stage 1: the tropical island; stage 3: every type)
// Design doc pp. 13-16: four starting types (one per player), neutral islets, and the dangerous islands of the map's
// middle (their shapes now; their dangers and prizes are stage 7).
enum class IsleType : uint8_t { Tropical, Stack, Town, Atoll, Islet, KrakenCove, Skull, Volcano, ReefGarden, Wreck,
                                Iceberg, Lighthouse, Shipwreck, Mangrove, KelpRaft, CliffTown,          // (the expansion's six more starting islands, doc p43)
                                IronIsland, Whale, SirenRocks, Maelstrom, GhostShip, BirdIsland, COUNT };   // (and six more dangerous ones, pp. 44-45)
const char* IsleTypeName(IsleType t);
inline bool IsStartType(IsleType t) { return t <= IsleType::Atoll || (t >= IsleType::Iceberg && t <= IsleType::CliffTown); }
inline bool IsDangerous(IsleType t) { return (t >= IsleType::KrakenCove && t <= IsleType::Wreck) || t >= IsleType::IronIsland; }
inline IsleType StartTypeOf(int i) { return i <= 0 ? IsleType::Tropical : i < 4 ? (IsleType)i : i < 10 ? (IsleType)((int)IsleType::Iceberg + i - 4) : IsleType::CliffTown; }   // (a lobby's choice 0-9: the four, then the expansion's six)
inline bool IsDrifting(IsleType t) { return t == IsleType::Wreck || t == IsleType::GhostShip; }   // (no terrain: drawn from its props, moved by the time)
struct Prop { Vector3 c{}, half{}; int kind = 0; float yaw = 0; };   // a box: 0 house, 1 roof, 2 tower, 3 dock, 4 boat, 5 woodpile, 6 hull, 7 mast
struct Island {
    IsleType type = IsleType::Tropical;
    std::string name;
    Vector3 c{};                                // its middle (y = 0)
    float radius = 90;                          // its land's reach
    int start = -1;                             // a player's starting island (slot), or -1
    int n = 0; float cell = 2, x0 = 0, z0 = 0;   // the heightmap's grid (world coordinates)
    std::vector<float> h;                       // metres above the sea (negative: the sea floor)
    std::vector<Vector3> palms;                 // trunk feet
    std::vector<float> palmH;                   // their heights (the crown is at the foot + 0.97 h)
    int nestPalm = -1;
    Vector3 hill{}, nest{};                     // the high point; the first nest (a palm crown, a ledge, a roof, the ring)
    std::vector<Vector3> sites;                 // nest sites (doc: 40 tropical, 20 stack, 25 town, 15 atoll)
    std::vector<std::pair<Vector3, float>> twigPts;   // where twigs come from, and how many a spot holds (palms, driftwood, the woodpile)
    std::vector<Vector3> shellPts;              // shells on the beaches
    std::vector<Prop> props;                    // the town's houses, tower, docks and boats; the wreck's hull
    std::vector<Vector2> outline;               // the coast, 72 points (the chart draws it)
    float floorDepth = -25;                     // the sea floor at the grid's edge
    float drop = 0;                             // (the wreck sinking: metres the whole island has gone down)
    uint32_t seed = 1;
    void Generate(uint32_t seed);               // (the stage-1 tropical island at the origin)
    void Generate(IsleType type, uint32_t seed, Vector3 centre);
    void GenerateMore(IsleType type, uint32_t seed);   // (the expansion's islands: flight_isles.cpp)
    void addSiteAt(Vector3 p);
    float Height(float x, float z) const;       // bilinear; outside the grid: the floor depth
    bool Covers(float x, float z) const { return n > 0 && x >= x0 && z >= z0 && x < x0 + (n - 1) * cell && z < z0 + (n - 1) * cell; }
    Vector3 Normal(float x, float z) const;
    bool Land(float x, float z) const { return Height(x, z) > 0.15f; }
    Vector3 Ground(float x, float z) const { return {x, std::max(0.0f, Height(x, z)), z}; }   // where a bird stands (the surface over water)
  private:
    void BuildOutline();
};

// ---------------------------------------------------------------- the map (stage 3: doc p14 "Arrangements")
enum class Arrangement : uint8_t { Archipelago, SafeDistance, Ring, Chain, COUNT };
const char* ArrangementName(Arrangement a);
struct MapOpts {
    int players = 4;                            // 2-6 starting islands (solo: you, and rivals that sit still until stage 4)
    Arrangement arr = Arrangement::Archipelago;
    IsleType home = IsleType::Tropical;         // your starting island's type
    uint32_t seed = 1;
    // a networked match (stage 5): which sides people play (a bit per side; side 0 is the host), the time limit
    uint32_t humanMask = 1;
    float minutes = 0;                          // 0: no limit (solo)
    bool multi = false;                         // no slow motion in a strike (the world can't crawl for one player)
    int seasons = 0;                            // (the expansion's long match) 0 standard; 2, 3 or 4 seasons (7, 11 or 16 game days)
};
// ---------------------------------------------------------------- the long match (the expansion, doc pp. 35-51: flight_long.cpp)
enum SeasonId { SEASON_SPRING = 0, SEASON_SUMMER, SEASON_AUTUMN, SEASON_WINTER, SEASON_COUNT };
enum SeasonEvent { EV_SPAWNING = 0, EV_TUNA_RUN, EV_MIGRATION, EV_LONG_NIGHT, EV_SEASON_COUNT };
struct SeasonFx {   // what a season does to the sea, the wind and the work (flight_long.json "seasons")
    std::string name, sea, wind, rewards, event, eventWhat;
    float regrowNear = 1, regrowFar = 1, mateTime = 1, windK = 1, stormK = 1, krakenWake = 1, stamina = 1, fishDepth = 0, bombCost = 1;
    bool thermalsAllDay = false, galeOneQuarter = false;
    int firstDay = 1, lastDay = 3;
};
const std::vector<SeasonFx>& Seasons();
int SeasonDays(int seasons);                    // a long match's length in game days (2: 7, 3: 11, 4: 16)
const char* SeasonEventName(int e);
// decrees (doc pp. 36-38): one a day, picked at dawn from three dealt from a deck of 24 (flight_long.json "decrees")
struct DecreeFx {
    float catchK = 1, splash = 1, grow = 1, spoil = 1, build = 1, trade = 1, flockSpeed = 1, chill = 1, guano = 1, mateTime = 1, morale = 0, grudge = 0, heal = 1, cacheCost = 0;
    int fervour = 0, tithe = 0, reputation = 0, wildJoin = 0;
    bool hatchAll = false, noClutch = false, callToArms = false, pearlDive = false, noConvert = false, scoutsExact = false, scoutsLate = false, noRaids = false, noFormation = false,
         dangersIgnore = false, extraEgg = false, noMates = false, visible = false, hidden = false, scoutsBlind = false, noDesert = false, thermalHome = false, salvage = false,
         silentRaids = false, tradersKnown = false, rest = false;
};
struct DecreeDef { std::string key, name, effect, tradeoff; DecreeFx fx; DecreeFx tomorrow; };
const std::vector<DecreeDef>& Decrees();
int DecreeIndex(const std::string& key);
enum VetTrait { VT_FEARLESS = 0, VT_LUCKY, VT_KEEN, VT_GREEDY, VT_LOYAL, VT_COUNT };
enum MateTrait { MT_BIG_EGGS = 0, MT_QUICK, MT_HARDY, MT_KEEN, MT_FIERCE, MT_FERTILE, MT_COUNT };
struct MateTraitDef { std::string key, name, effect, favorite; float hpAdd = 0, hpMul = 1, speed = 1, scout = 1, attack = 1; int clutch = 0; };
struct VetData { int fights = 3; float bonus = 0.15f, fearlessMorale = 15, loyalAttack = 1.2f; std::vector<std::string> names, traitNames, traitWhat; };
const VetData& Veterans();
const std::vector<MateTraitDef>& MateTraits();
enum RelicId { RL_BELL = 0, RL_LENS, RL_BEAK, RL_EGG, RL_FLAG, RL_FEATHER, RL_SHELL, RL_LOGBOOK, RL_COUNT };
enum LegendId { LG_ALBATROSS = 0, LG_FISHER_KING, LG_OLD_OWL, LG_PHOENIX_CHICK, LG_DODO, LG_COUNT };
enum GreatId { GE_RED_TIDE = 0, GE_KRAKEN_WALK, GE_GREAT_STORM, GE_ECLIPSE, GE_TREASURE, GE_PLAGUE, GE_CALM, GE_VISITOR, GE_COUNT };
struct RelicDef { std::string key, name, effect; float morale = 0, scout = 1, trade = 1, striker = 1, mateTime = 1; };
struct LegendDef { std::string key, name, effect; int score = 100; };
struct GreatDef { std::string key, name, what; float stock = 1, poison = 0, days = 1, loss = 0, catchK = 1; int pearls = 0, take = 0, crowd = 60; };
const std::vector<RelicDef>& Relics();
const std::vector<LegendDef>& Legends();
const std::vector<GreatDef>& GreatEvents();
int RelicsMax();
float RelicStealChance();
int PirateHireFish();
int TributeFish();
// nest styles (doc p49): chosen per nest, each a small bet
enum NestStyle { NS_CUP = 0, NS_PLATFORM, NS_BURROW, NS_HANGING, NS_MUD, NS_CLIFF, NS_FLOATING, NS_COUNT };
enum { NT_THEFT = 0, NT_TEAR, NT_LAND, NT_LAVA, NT_ASH };   // what threatens a nest: egg theft, a raid tearing it down, things on land (lizards, plants), lava, ash
struct NestStyleDef { std::string key, name, cost, effect; int twigs = 10, shells = 0, eggs = 0; };
const std::vector<NestStyleDef>& NestStyles();
struct Nest;
bool NestOpen(const Nest& n, int threat);       // (a nest's style shuts out some threats)
// fishing mastery (doc p48): techniques, each a different dive and a different risk
enum Tech { TK_PLUNGE = 0, TK_SKIM, TK_HOVER, TK_DRIVE, TK_DEEP, TK_NIGHT, TK_COUNT };
enum { TL_TRY = 0, TL_CATCH = 1, TL_LOSS = 2 };
struct TechDef {
    std::string key, name, how, best, risk, founder, safeFor;
    float catchK = 1, bigK = 0, smallK = 1, shoreK = 1, riskK = 1, splash = 1, pace = 1, steal = 0, fightK = 0, recover = 0, reach = 1, founderK = 1, safeK = 1;
    int yield = 1, minFishers = 0;
};
struct TechMod { float hit = 1, risk = 1, splash = 1, steal = 0, fight = 0, recover = 0, pace = 1, reach = 1; int yield = 1; };
const std::vector<TechDef>& Techniques();
// Founder perks (doc p38)
struct PerkDef {
    std::string key, name, effect;
    float stamina = 1, attack = 1, ledAttack = 1, chickGrow = 1, pearl = 0, barter = 1, sightM = 0; int carry = 0;
    bool sharpEyes = false, noRout = false, weatherSense = false, nineLives = false, thiefsEye = false, oldSalt = false;
};
const std::vector<PerkDef>& Perks();
int PerkIndex(const std::string& key);
const std::vector<int>& PerkDays();
PerkDef PerkSum(uint32_t bits);                 // every perk in the bits folded into one (multipliers multiplied, flags or-ed)
float WinterHoldings();
int RunFlightLongTest();                        // depth.exe --flight-long-test                         // (a four-season match: Winter's islands and nests count this many times)
struct IsleSpec { IsleType type = IsleType::Islet; Vector3 c{}; int start = -1; std::string name; };
std::vector<IsleSpec> LayoutMap(const MapOpts& o);   // slot 0 at the origin; rotational fairness
// the Long Flight (the two-hour expansion: flight_longflight.cpp)
struct ChronLine { int day = 0, season = 0, year = 1, kind = 0; std::string text; };   // a line of a colony's Chronicle
enum { CK_DEATH = 0, CK_SUCCESSION, CK_ELDER, CK_FOUNDING, CK_CLUTCH, CK_RAID, CK_WONDER, CK_WAR, CK_MARRIAGE, CK_OATH, CK_BEAST, CK_TITLE, CK_SPECIES, CK_OTHER, CK_COUNT };
int YearDays();
const std::vector<std::string>& DynastyNames();
const char* SuccessionName(int c); const char* SuccessionWhat(int c);
int RunFlightLongFlightTest();                  // depth.exe --flight-longflight-test
struct Rival { int isle = -1; std::vector<Vector3> nests; std::vector<Vector3> caches; int birds = 0; };   // (a colony that sits still until stage 4)

// What your birds have seen (doc pp. 18-19): the fog, islands by how well they're known, grounds, sightings, reports
struct Sighting { float t = -1; int nests = 0, caches = 0, birds = 0; int alt = 0; bool exact = false; int scouts = 0; };
struct GroundInfo { float t = -1; float stock = 0; float predators = 0; int fishers = 0; };
struct Report { float t = 0; int kind = 0; std::string text; Vector3 at{}; };   // kind 0 ground, 1 island, 2 flock, 3 danger, 4 opportunity
struct Knowledge {
    float x0 = 0, z0 = 0, cell = 25; int nx = 0, nz = 0;
    std::vector<float> seen;                    // last seen (game time), -1 never: the fog
    std::vector<uint8_t> isle;                  // per island: 0 unknown, 1 flown over (a silhouette), 2 landed on (in full)
    std::vector<Sighting> sight;                // per island: the latest report on it
    std::vector<GroundInfo> ground;             // per zone
    std::vector<Report> log;                    // newest last
    bool Seen(float x, float z) const;
    float SeenAt(float x, float z) const;
};
enum class Alt : uint8_t { Low, Mid, High };
const char* AltName(Alt a);
float AltHeight(Alt a);                         // 12, 40, 120 m
float AltSight(Alt a);                          // what a bird at that height sees round it (60, 120, 250 m)

// ---------------------------------------------------------------- the weather
struct Wind {
    Vector2 dir{1, 0}; float speed = 6;         // where it blows to, m/s
    Vector2 nextDir{1, 0}; float nextSpeed = 6; float shiftT = 0, shiftLen = 180;
    Vector2 At(float t) const;                  // the wind now (with a little gust)
};

// ---------------------------------------------------------------- the Founder
enum class FState : uint8_t { Fly, Strike, Struggle, Under, Perched, Floating, Fainted, Dead };
const char* FStateName(FState s);
struct CachedFish { int sp = -1; int size = 1; float age = 0; };
struct Founder {
    int def = 0;
    FState st = FState::Perched;
    Vector3 pos{}, vel{};
    float yaw = 0, pitch = 0, bank = 0;         // facing (yaw about y, 0 = +x), nose up (+) / down (-), roll
    float airspeed = 0;
    float stamina = 8, hunger = 1, hp = 100;    // stamina in seconds of sprint; hunger 1 full .. 0 starving
    bool flapping = false, sprinting = false, gliding = true, exhausted = false;
    // carried: one fish (species, size)
    int carrySp = -1, carrySize = 0, carryTwigs = 0;
    // the strike: a slow-motion half second steering the talons onto a fish
    float strikeT = 0, strikeLen = 0.5f; Vector3 strikeAt{}, strikeAim{}; float strikeSpeed = 0;
    float struggleT = 0; int struggleSp = -1, struggleAgent = -1;
    float underT = 0, faintT = 0, respawnT = 0;
    bool chick = false; int chickFish = 0;      // the chick-leader after a death: half speed and carry until 3 fish
    float adultT = 0;                           // seconds at full stats after growing up (the colony takes orders again at 10)
    int deaths = 0;
    int agent = -1;                             // the body in the sea's web (a "Diver" record) while low over water
    std::string lastCause;
    float starveT = 0, strikeCd = 0, airT = 0;   // (airT: seconds since it took off: it can't settle back on a site at once)            // (a bot Founder's time at 0 hunger; the cooldown on a flown Founder's strike at a bird)
    // what this founder is now (species stats, chick-leader, starvation and three deaths applied)
    float Cruise(const FounderDef& d) const;
    float Sprint(const FounderDef& d) const;
    int Carry(const FounderDef& d) const;
    // the long match: perks picked at days 3, 7 and 11 (a bit each, flight_long.json "perks"), and the three on offer
    float ageSpeed = 1, ageAttack = 1; bool old = false; int oldCarry = 0;   // (the Long Flight: its age: prime +10%, old a day slower at a time and no size-4 fish)
    uint32_t perks = 0; int perkOffer[3] = {-1, -1, -1}; int perkLevel = 0, saltDay = 0; bool nineUsed = false; float stormWarned = -1;
    float diveTop = 0; int rebornSeason = -9; float drownT = 0;   // (the Gannet: the dive's top; the Phoenix: the season it was last reborn; the Frigatebird: time on the water)
    float StatMul() const;                      // starving -30%, three deaths -10%, a chick half
};
struct FounderInput {
    float yaw = 0, pitch = 0;                   // where the player is steering (absolute, radians)
    bool flap = false, sprint = false, brake = false;
    bool interact = false;                      // E: drop the fish in the cache, land, pick up
    bool eat = false;                           // F: eat a fish from the cache
    bool takeoff = false;                       // Space from a perch or the water
    Vector2 steer{0, 0};                        // during a strike: the talons' aim, -1..1 on each axis
};

// ---------------------------------------------------------------- the colony (stage 2: design doc pp. 3-7, 11)
// Every number is data: data/flight/flight_economy.json (Economy) and flight_roles.json (RoleDef).
struct Economy {
    float daySeconds = 120, workPace = 1;     // a game day in real seconds; colony birds work this much faster (keeps the per-day economy when days are short)
    int startPearls = 2, startShells = 12;    // (a founder's dowry: the first research by day 2, as the doc's pacing has it)
    float pearlCatch = 0.02f;                 // (stage 8: the chance any delivered catch has an oyster with a pearl in it)
    float founderHungerS = 360;               // real seconds a full Founder lasts (the user: a day's worth emptied far too fast once days were 2 minutes)
    float feedAdult = 2.5f, feedChick = 1, feedFounder = 3;   // feed units a full hunger bar holds (one day's eating)
    float chickDrain = 2;                     // chicks empty twice as fast
    float starveDays = 0.5f;                  // at 0 hunger this long, a bird dies
    int cacheCap = 20; float spoilDays = 2;
    int courtFish = 3, courtStep = 1, courtMinSize = 2;   // the courtship bowl: 3 fish of feed 2+, one more for each mate after
    float mateMin = 0.3f, mateMax = 0.9f, tropicalMate = 0.8f; int wildMates = 30;
    int clutchMin = 2, clutchMax = 4; float clutchDays = 2; int clutches = 3, nestEggs = 4;
    float hatchDays = 1, chillDays = 2, chickDays = 2, overfeed = 0.25f, liningHatch = 0.25f; int liningShells = 3;
    int nestTwigs = 12, cacheTwigs = 6; float retrainDays = 1;
    float regrow[7] = {0, 3.0f, 1.6f, 0.9f, 0.5f, 0.3f, 0.2f};   // per day by size class: logistic r
    float immigration = 0.04f;                // per day, a fraction of K, even into an empty ground
    int sites = 24, palmTwigs = 6; float twigRegrowDays = 1;
    float selfFetchS = 20, fedS = 2;          // a meal at the cache: fetched yourself, or handed over by a feeder
    int feederCover = 8;                      // one feeder serves this many working birds
};
const Economy& Econ();
// working roles (doc p11) and warrior roles (p11-12; stage 4); the Bomber comes with the Works (stage 7)
// (stage 6 adds the Trader, Priest, Chemist and Pathfinder; stage 7 the Bomber and the frigatebird Pirate)
enum class Role : uint8_t { None, Fisher, Feeder, Builder, Scout, Trader, Priest, Chemist, Pathfinder, Skirmisher, Tank, Striker, Watcher, Screamer, Flockmaster, Bomber, Pirate,
                            // the expansion (doc pp. 40-42): eight warriors, then six workers (appended: saved indices stay put)
                            Plunger, Swallow, Mimic, Nurse, Ferrier, Lancer, Harrier, Drummer, Diver, Gardener, Keeper, Teacher, Herald, Augur, COUNT };
inline bool IsWarrior(Role r) { return (r >= Role::Skirmisher && r <= Role::Pirate) || (r >= Role::Plunger && r <= Role::Drummer); }
const char* RoleAbbrev(Role r);
struct RoleDef {
    std::string key, name, what; float hp = 60, speed = 12; int carry = 2;
    float attack = 3, cooldown = 1.2f; Alt pref = Alt::Mid; bool perched = false;   // (flight_war.json)
    uint32_t strong = 0, weak = 0;            // bits by Role: what it beats, what beats it
};
const RoleDef& RoleOf(Role r);
int SpawnFishSlot(rt::Ecosystem& eco, int sp, Vector3 pos, int zone);   // a fish into a dead slot (the web only appends)
const char* RoleName(Role r);
enum class BStage : uint8_t { Egg, Chick, Adult, Mate };
enum class Task : uint8_t { Idle, Fly, Search, Dive, Deliver, Eat, Gather, Build, Sit, Fetch, Feed };
struct Bird {
    int id = 0; BStage stage = BStage::Egg; Role role = Role::None, retrainTo = Role::None;
    bool alive = true; std::string cause;
    int nest = -1;                            // eggs, chicks and mates belong to a nest
    Vector3 pos{}, vel{}; float yaw = 0, flapPh = 0;
    float hunger = 1, starveT = 0, age = 0, chillT = 0, retrainT = 0;
    Task task = Task::Idle; Vector3 goal{}; float taskT = 0; int target = -1, fish = -1;
    int carrySp = -1, carrySize = 0, carryTwigs = 0, carryShells = 0;
    int clutches = 0; float clutchT = 0;      // mates
    int caught = 0;
    // a scout's order (doc p19): where, how high, what it saw, and the report it carries home
    int scoutIsle = -1, scoutZone = -1; Vector3 scoutAt{}; Alt alt = Alt::Mid; bool hasOrder = false, observed = false;
    Sighting obs; GroundInfo gobs; float obsT = 0;
    // war (stage 4): its health, its fighting breath, a strike's cooldown, a net holding it, its flock and its target
    float hp = 60, fight = 25, atkCd = 0, netT = 0, fleeT = 0, zoomT = 0;
    int flock = -1; int tgtSide = -1, tgtId = 0;   // tgtId 0 = the side's Founder
    bool struck = false;                      // (its first strike in this engagement is spent)
    Vector3 post{};                           // a Watcher's perch
    int carryGood = -1, carryN = 0;
    // the long match: veterans (doc p42) and mates' traits (p43)
    int fights = 0; int vet = -1, vetName = -1; bool luckyUsed = false; float foughtT = -100, countedT = -100;   // vet: VT_* trait, -1 none
    int trait = -1;                           // MT_*: a mate's trait, and its chicks' (inherited)
    bool taught = false;                      // (a Teacher saw it fledge)
    float songT = 0;                          // (the Siren Rocks: enthralled, sitting on the rocks)
    bool elder = false; float vetT = -1e9f; int kin = -1;   // (the Long Flight: a veteran a year on is an elder; a mate's founder species, passed to its chicks)
    int tk = -1, bonusFish = 0; float recoverT = 0;   // (fishing mastery: the technique of this trip; a second fish (night fishing); a missed plunge's recovery)           // (a Trader's goods coming home; carrySp -2 an egg being stolen, -3 a bomb)
};
struct Site { Vector3 pos{}; int palm = -1; int nest = -1; int isle = -1; };
struct Nest {
    int site = -1; Vector3 pos{}; float twigs = 0; bool built = false, founders = false;
    int mate = -1; int bowl = 0, bowlNeed = 3; float mateT = -1; int favFish = 0;   // favFish: courtship fish of the wanted trait's favourite   // mateT: counting down to the mate's arrival
    float larder = 0;                         // feed laid in the nest (chicks and the mate eat from it)
    int shells = 0;                           // lining
    int isle = -1;                            // (the island it stands on: an island is held by whoever has the most nests there)
    float tear = 0;                           // (an assault's damage to it)
    int style = 0; float rainT = 0, floodT = 0;   // (the long match: NS_* style; a mud nest's rain; a burrow's flooding, a floating nest's day)
};
struct Cache { Vector3 pos{}; std::vector<CachedFish> fish; bool built = true; float twigs = 0; int isle = -1; };
struct TwigSource { Vector3 pos{}; float twigs = 0, cap = 6; bool shells = false; };
struct Stock { int row = 0, sp = 0, zone = 0; float K = 0, births = 0, pop = 0; };   // a fishing ground's species (one spawn row); pop: its count while the zone sleeps
struct DayStats { int day = 0, birds = 0, eggs = 0, chicks = 0, mates = 0, nests = 0, caught = 0, deaths = 0; float feedCaught = 0, mouths = 0, cacheFeed = 0, lagoon = 0; };
// A flock (doc p13): 2-12 warriors with a leader, a formation, an altitude order, a stance and a target
enum class Formation : uint8_t { Chevron, Wall, Spiral, Scatter, Hammer, Cover, COUNT };
const char* FormationName(Formation f);
enum class Stance : uint8_t { Raid, Hold, Escort, RetreatHalf, COUNT };
const char* StanceName(Stance s);
enum class Target : uint8_t { Home, Cache, Nests, Ground, Flock, Point, COUNT };   // Home: guard your island
struct Flock {
    int id = 0; std::vector<int> members;     // bird ids
    int leader = -1;                          // -2 the Founder (when it flies with the flock), a Flockmaster's id, or -1
    int loanTo = -1; float loanUntil = 0; bool pactWarned = false;   // (diplomacy: lent to another colony for a day; its raid on a pact partner announced)
    Formation form = Formation::Chevron; Alt alt = Alt::Mid; Stance stance = Stance::Raid;
    Target target = Target::Home; int tSide = -1, tIsle = -1, tZone = -1, tFlock = -1; Vector3 tAt{};
    Vector3 pos{}, vel{};                     // its middle, how it's moving
    float morale = 50, wins = 0, engagedT = 0, overWaterT = 0;
    int startSize = 0, lost = 0;
    bool retreating = false, scattered = false, leaderDead = false;
    std::string name;
    int stim = 0; float stimT = 0, crashT = 0;   // (stage 7: a stimulant the flock was dosed with, and its crash)
};
// stage 7: stimulants (doc p25; the Chemistry tree)
enum Stim : uint8_t { STIM_NONE, STIM_HASTE, STIM_FURY, STIM_CLOT, STIM_DRAUGHT, STIM_COUNT };
const char* StimName(int s);
float StructureHp(int kind); float StimSpeed(int stim); float StimAttack(int stim); float StimBleed(int stim);   // (flight_danger.cpp)
float BellMorale(); float FogSight(); float TearPerStrike(); float SkullShrineFervour(); float DesertDays();
// what a colony has raised (doc p24, p20): hedges round nest sites, towers for Watchers, the Roost (research), the
// shrine (faith), the Works (bombs and stimulants: stage 7)
enum { ST_HEDGE = 0, ST_TOWER = 1, ST_ROOST = 2, ST_SHRINE = 3, ST_WORKS = 4, ST_PERCH, ST_SMOKEHOUSE, ST_LOOKOUT, ST_ROOKERY, ST_BEACON, ST_MONUMENT, ST_COUNT };   // (5 on: the long match's, doc p49)
int StructureTwigs(int kind); int StructureShells(int kind);   // (flight_war.json and flight_research.json)
const char* StructureName(int kind);
struct Structure { int kind = 0; Vector3 pos{}; float twigs = 0; int shells = 0; bool built = false; int site = -1; float hp = 100; int isle = -1; };
struct Colony;
const Structure* BuiltOf(const Colony& C, int kind);   // (the long match's structures, doc p49)
int MonumentsOf(const Colony& C);
float PerchSight(); float SmokehouseSpoil(); float LookoutReport();

// ---------------------------------------------------------------- stage 6: research, faith, trade, the founders' bends
// (flight_society.cpp; data/flight/flight_research.json, flight_bends.json, flight_towns.json)
enum class Tree : uint8_t { Nesting, Fishing, Flight, Caches, War, Bombing, Chemistry, Faith, Trade, COUNT };
const char* TreeName(Tree t);
struct ResearchTier { std::string name, what; };
const ResearchTier& ResearchOf(Tree t, int tier);           // tier 1-4
struct ResearchCost { int pearls = 2, shells = 10; float days = 0.5f; int birds = 0; };
ResearchCost ResearchCostOf(int tier);
// a founder species' bend on its whole colony (doc pp. 8-10): every number is data (flight_bends.json)
struct Bend {
    float fishHit = 1, speed = 1, attack = 1, mateTime = 1, fervourGain = 1, fervourCap = 100, trade = 1, traderCarry = 1;
    float convert = 1, rest = 1, guano = 1, research = 1, stamina = 1, wind = 1, scout = 1, reach = 1, nestTwigs = 1, predatorRange = 1;
    int carry = 0, earlyTrees = 0; float clutch = 0, fledgeDays = 0;   // (clutch: eggs, fractional = a chance of one more or one less)   // earlyTrees: bits by Tree that research faster and a day sooner
    bool nightFishing = false, eggTheft = false, noFaith = false, boom = false, goldenNest = false, talonLock = false, skim = false;
    bool tear = false, serenade = false, duskRaid = false, cornering = false, pilgrimage = false, brood = false, longReach = false;
    // the expansion's six (doc pp. 39-40)
    int feederCarry = 0; float cacheCap = 1, colonySpeed = 1, daylight = 1;
    bool plungeStrike = false, noLowStrike = false, pouch = false, steal = false, piracy = false, noWater = false, swim = false, noAirWar = false,
         nightDay = false, silentWings = false, phoenixChicks = false, rebirth = false, fervourDecay = false;
};
const Bend& BendOf(int founderDef);
const Bend& NoBend();                           // (a regent's colony: no founder bonus)
// a fishing town's dock market (doc p26): prices in feed (fish), moving with supply and the hour
enum Good : uint8_t { G_FISH, G_TWIGS, G_SHELLS, G_PEARLS, G_COUNT };
const char* GoodName(int g);
struct Town {
    int isle = -1; Vector3 dock{};
    float price[G_COUNT] = {1, 0.5f, 1, 4};   // feed per unit (fish: what the town pays per feed)
    float stock[G_COUNT] = {60, 80, 60, 10};  // what it has to sell (fish: what it has bought, in feed)
    std::vector<float> rep;                   // per side, -100..100
    float storm = 0;                          // (a storm coming: fish pays double)
};
struct Barter {   // an offer between two colonies (Trade tier 2): goods each way, a truce
    int id = 0, from = 0, to = 0; int give[G_COUNT] = {}, get[G_COUNT] = {}; float truceDays = 0, t = 0; int state = 0;   // 0 open, 1 accepted, 2 refused, 3 expired
    bool pact = false; int loanFlock = -1;      // (the long match's diplomacy: a feed pact; a flock lent for a day, for the fish asked)
};
struct Colony {
    int side = 0;                             // whose (0 you; 1.. the rivals)
    std::vector<Bird> birds;
    std::vector<Nest> nests;
    std::vector<Cache> caches;
    std::vector<Site> sites;
    std::vector<TwigSource> twigSrc;
    int twigs = 0, shells = 0, wildMates = 30, nextId = 1;
    float plan[(int)Role::COUNT] = {0, 0.5f, 0.2f, 0.3f, 0};   // the fledging plan: the share of each role
    int ground = -1;                          // fishers' ground: -1 the best, else a zone index
    float restBelow = 0;                      // fishers leave a ground resting while its stock is under this (0 never)
    int nestsWanted = 2;                      // builders raise nests (on free sites) until there are this many
    float feedToday = 0, feedYesterday = 0;   // feed caught by the colony's fishers
    int caughtToday = 0, deathsToday = 0;
    std::vector<std::pair<std::string, int>> deaths;   // by cause
    std::vector<DayStats> days;
    bool leaderless = false;
    std::vector<Flock> flocks; int nextFlock = 1;
    std::vector<Structure> builds;
    int stolen = 0, lostToRaids = 0, kills = 0, losses = 0;   // (war tallies)
    float warT = 0;                           // (a bot's next war decision)
    float downT = 0;                          // (how long its Founder has been down)
    // stage 6: the stores research and trade are made of; research; faith; trade; the Tycoon's boom
    int pearls = 0; float guano = 0, sulfur = 0;
    uint8_t tier[(int)Tree::COUNT] = {};      // research reached per tree (0-4)
    int resTree = -1; float resLeft = 0, resDays = 0;   // the research under way (days left of it), at the Roost
    float fervour = 20;                       // 0-100 (doc p26)
    float prayerT = 0; int prayedDay = -1;    // the Founder's prayer: +15 for a game hour, once a day
    int tradeFor = G_PEARLS;                  // what the Traders buy with the surplus fish
    int boomState = 0; float boomT = 0, boomCd = 0;   // the Tycoon: 0 none, 1 boom (2 days), 2 bust (1 day)
    bool cornered = false; float cornerT = -1; int cornerTown = -1; float cornerBuy = 0;   // the Sigma's market cornering
    int serenadeDay = -1;                     // the Lyrebird's once-a-day call
    std::vector<int> spies;                   // the Cuckoo's parasite chicks: islands they report from
    int eggsStolen = 0, nestsDestroyed = 0, converted = 0;   // (tallies for the score and the news)
    float goldenT = 0;                        // (the Tycoon's golden nest: a pearl a day)
    float convertAcc = 0;                     // (the priests' conversions under way)
    // stage 7: bombs and stimulants at the Works, the ship's bell, the kraken's offerings
    int bombs = 0, blockbusters = 0; float bombT = 0, blockT = 0;
    int stims[STIM_COUNT] = {}; int brewFor = STIM_HASTE; float brewT = 0;
    // the long match: today's decree, the three offered at dawn, the ones used (no repeats), yesterday's (its after-effects)
    int wantTrait = -1, nextVetName = 0;
    uint32_t relics = 0; int legend = -1; bool legendAlive = false, goldenEggUsed = false;
    float greyHit = -1000;
    int nestStyle = 0;                        // (the long match: the style new nests are laid in)
    // the Long Flight: generations (the heir, the succession choice, the perk it keeps), the dynasty, the Chronicle
    int gen = 0, heirId = -1, heirTrait = -1, succChoice = 0, keepPerk = -1, dynastyPick = 0, heirAnnounced = -1; bool regent = false;
    float genStart = 0, successionT = -1e9f; uint32_t relicsKept = 0; std::string dynasty; std::vector<ChronLine> chronicle;
    float beaconT = -1e9f, rookeryFledgeT = -1e9f; bool rookeryWarm = false;   // (the Beacon last lit; the Rookery's chicks fledging together; enough adults about it)
    int tech = -1; float techMastery[TK_COUNT] = {}; std::vector<int> techLog;   // (fishing mastery: the colony's technique, -1 auto; per ground x technique: tries, catches, losses)
    int pact = -1, bounty = 0, bountyBy = -1; float pactT = 0, truceBroken = -1000;   // (diplomacy: a feed-pact partner; fish posted on this colony's Founder)                      // (the last time the Grey Wings took one of its birds)   // (the long match: relics at the shrine, a legendary bird)        // (the long match) the trait the courtship bowls ask for (-1 any); the next veteran's name
    int decree = -1, yesterday = -1, offer[3] = {-1, -1, -1}, dealtDay = 0, lastRaider = -1; uint32_t decreesUsed = 0; float salvageT = 0, titheFish = 0;
    bool bell = false; float offeredKraken = -1e9f, apeFedT = -1e9f;
    int krakenKill = 0;                       // (the kraken killed: 150 to the score)
    int expandTo = -1;                        // (an island the colony's Founder or Pathfinder is off to found an outpost on)
    bool HasTier(Tree t, int n) const { return tier[(int)t] >= n; }
};
// A rival (and, while it steps, you): everything that is one player's and not the world's. The colony code works on the
// World's own fields; a rival steps by swapping its Side in (World::SwapSide), so one set of code runs every colony.
struct Side {
    Colony col; Island island; int home = 0; Founder me; Bird fb; bool founderBot = true;
    int lagoonZone = -1, inshoreZone = -1; float dayAcc = 0; int dayNum = 0; int caughtIn[16] = {};
    Knowledge know; std::vector<std::string> log; bool human = false;   // (a person's side: its own fog, reports and news)
    int slot = 1; std::string name; Color livery{200, 60, 60, 255};
};
// the score (design doc p27; data/flight/flight_scoring.json)
struct ScoreCard { int birds = 0, nests = 0, isles = 0, cache = 0, kills = 0, founder = 0, total = 0, research = 0, pearls = 0, faith = 0, thefts = 0, kraken = 0, legacy = 0; };   // (legacy: the long match's additions, doc p50)
// stage 7: the dangerous islands' monsters and moods, the weather (flight_danger.cpp; data/flight/flight_danger.json)
struct Kraken { int isle = -1; int mood = 0; float hp = 4000, hpMax = 4000, moodT = 0, calmT = 0, grabT = 0, armT = 0, armKill = 0; bool dead = false; int killedBy = -1; Vector3 arm{}; };   // mood 0 asleep, 1 awake, 2 surfaced
struct Ape { int isle = -1; float sleepT = 0, throwT = 0, rockT = 0; Vector3 pos{}, rockFrom{}, rockTo{}; float lizardT = 0, plantT = 0, plantsBurnt = 0; };
struct Volcano { int isle = -1; float next = 0, tremorT = 0, ashT = 0; int eruptions = 0; };
struct WreckState { int isle = -1; Vector3 c0{}; Vector2 vel{}; float sunk = 0; int hold = 30; bool bell = true; bool gone = false; float ratT = 0, ghostT = 0; };
// the expansion's islands' state (flight_isles.cpp)
struct IsleState { int ghost = -1, whale = -1, dives = 0; Vector3 ghostC0{}; float whaleNext = 0, whaleUnderT = 0, ironT = 0; bool ironStores = false, sirenGift = false, maelRelic = false; bool holdFlooded[6] = {}; std::vector<float> birdConv; std::vector<int> ghostSeason; };
float IcebergShells(); float SongRange();
struct Weather { int kind = 0; float t = 0, next = 0; float fogDawn = -1; };   // kind 0 fair, 1 storm, 2 fog

// ---------------------------------------------------------------- the world (one Founder, one island: stage 1)
struct World {
    std::string seaKey = "flight_tropical";
    Island island;
    rt::Ecosystem eco;
    Wind wind;
    Founder me;
    Colony col;
    std::vector<Stock> stocks;                  // the grounds' fish, regrown logistically (stage 2)
    bool founderBot = false;                    // --flight-sim: the Founder is flown by the colony's own logic
    Bird fb;                                    // (founderBot) the Founder's body as a colony bird
    int caughtIn[16] = {};                      // (the colony's catch by home ground)
    std::vector<Bird> born;                     // (birds laid this step, added after the colony's loop)
    int lagoonZone = -1;
    float time = 0;                             // seconds of game time
    float timeScale = 1;                        // the strike's slow motion
    uint32_t rng = 7;
    std::vector<std::string> log;
    int fishCaught = 0, fishMissed = 0, fishLost = 0, fishEaten = 0;
    bool forceHit = false;                      // (tests: a strike on a fish always lands)
    static inline float DAY = 120;              // seconds in a game day (flight_economy.json "day_seconds"; the user: shorter days than the doc's 4 minutes, so a match runs well past day 8)
    float DayPhase() const;                     // 0 midnight .. 0.25 dawn .. 0.5 noon .. 0.75 dusk
    const FounderDef& Def() const { return Founders()[me.def]; }
    void Init(const std::string& founderKey, uint32_t seed);                       // (stage 1: the tropical island alone)
    void Init(const std::string& founderKey, uint32_t seed, const MapOpts& o);     // a whole map (stage 3)
    // the map
    MapOpts opts; bool wholeMap = false;
    std::vector<Island> isles; int home = 0;    // every island (home is yours; island above is a copy of it)
    std::vector<Rival> rivals;                  // (stage 3's still colonies; stage 4 makes them sides)
    std::vector<Side> sides;                    // the rival colonies, run by bots (slot 1..)
    int cur = 0;                                // whose colony is in the fields now (0 you; i+1 sides[i] swapped in)
    bool quiet = false;                         // (a rival is stepping: its news isn't yours)
    void SwapSide(int i);                       // trade the World's colony fields with sides[i] (call twice to swap back)
    Colony& ColOf(int side);                    // a side's colony, wherever it is
    const Colony& ColOf(int side) const { return const_cast<World*>(this)->ColOf(side); }
    Founder& FounderOf(int side);
    const Founder& FounderOf(int side) const { return const_cast<World*>(this)->FounderOf(side); }
    int HomeOf(int side) const;                 // a side's home island
    int OwnerOf(int isle) const;                // the side whose home it is, or -1
    bool HumanOf(int side) const;               // a person plays it (their own news, fog and input)
    bool BotFlown(int side) const;              // its Founder is flown by the colony's logic
    template <class F> void WithSide(int side, F fn) {   // run fn with that side swapped into the fields (whatever is in now)
        if (side == cur) { fn(); return; }
        int was = cur;
        if (was != 0) SwapSide(was - 1);
        if (side != 0) SwapSide(side - 1);
        fn();
        if (side != 0) SwapSide(side - 1);
        if (was != 0) SwapSide(was - 1);
    }
    void SayTo(int side, const std::string& s); // a side's own news (nothing for a bot's)
    const std::string& SideName(int side) const;
    Color SideColor(int side) const;
    std::string name0 = "your colony";          // side 0's name (a networked match: the host's)
    // a networked match (stage 5; flight_net.cpp): each person's side is flown from its own input, one world
    bool human = true;                          // (side 0's, swapped like the rest: a person plays the side in the fields)
    std::vector<FounderInput> sideIn;           // each side's input this step (by side; side 0's is Step's argument)
    bool multi = false, mirror = false;         // a networked match; a guest's copy of the host's (it never steps)
    bool predicting = false;                    // (a guest flying its own Founder ahead of the host: no side effects)
    std::vector<float> mirrorStock;             // (a mirror: each zone's stock as the host has it)
    uint32_t cautiousMask = 0;                  // (people's sides the AI stands in for: it fishes and defends, it doesn't raid)
    float matchLen = 0;                         // seconds (0: no limit)
    bool over = false; int winner = -1; std::string overReason;
    std::vector<ScoreCard> scores; float scoreT = 0;   // (every side's, kept a second at a time; a mirror's come from the host)
    ScoreCard Score(int side) const;
    void CheckEnd();
    // stage 6 (flight_society.cpp): research, faith, trade, barter, the founders' bends
    std::vector<Town> towns;                    // every fishing town on the map (neutral and home)
    std::vector<float> townCredit;              // (town * 8 + side: feed sold there and not yet spent)
    Sighting TrueSighting(int isle);            // what's really on an island now (exact counts)
    std::vector<Barter> offers; int nextOffer = 1;
    std::vector<float> truceUntil;              // (side a * N + b) -> game time the truce lasts to
    const Bend& BendNow() const { return col.regent ? NoBend() : BendOf(me.def); }   // the bend of the colony in the fields (a regent's colony: none)
    const Bend& BendOfSide(int side) const { return ColOf(side).regent ? NoBend() : BendOf(FounderOf(side).def); }
    bool CanResearch(Tree t, std::string* why = nullptr) const;   // the colony in the fields
    bool StartResearch(Tree t);
    bool RoleUnlocked(Role r) const;             // (fledging and retraining into it)
    bool BuildUnlocked(int kind) const;
    const Structure* Built(int kind) const;      // the colony's built structure of that kind, or null
    void StepSociety(float dt);                  // research, fervour, priests, the boom, guano (the colony in the fields)
    void StepTowns(float dt);                    // the markets' prices recover and move through the day (once a step)
    float FervourBand() const;                   // what fervour does now: 0 low, 1 normal, 2 high, 3 very high, 4 zeal
    int FervourBandOf(int side) const;
    float FervourRout() const;                   // (a flock routed: the colony's fervour drops)
    float WindPenalty(int side) const;           // a headwind's cost (the Albatross and Wind reading halve it)
    float FightStamina(int side) const;          // a warrior's fighting breath (Long wings, the founder's bend)
    int CacheCap() const;                        // (Big caches)
    float SpoilDays() const;                     // (the Smokehouse)
    int NestEggs() const;                        // (Big nests)
    float MaxHp(Role r) const;                   // (Shell armor)
    int NestTwigs() const;                       // twigs a nest takes (the Albatross's are big)
    int ShellsWanted() const;                    // (the stock of shells the builders keep for research)
    float DeepDivePearl() const;                 // (Deep dive: the chance a delivered catch brings a pearl)
    float NightRest() const;                     // the share of the night the colony roosts (fervour)
    int NearestTown(Vector3 p, float r) const;
    float SellPrice(int town, int good) const;   // what the town wants (feed per unit) for one of its goods now
    float FishPrice(int town) const;             // what it pays per feed now
    bool TradeAt(int town, int feedIn, int good, int* got = nullptr);   // sell feed for a good (the colony in the fields)
    bool Pray();                                 // the Founder at the shrine
    bool Offer(int size);                        // a fish laid at the shrine
    bool StartBoom();                            // the Tycoon
    bool Corner(int town);                       // the Sigma
    bool Serenade(int nest);                     // the Lyrebird
    bool Pelican(int town, int isle);            // a pearl for one true thing about an island
    bool Truce(int a, int b) const;
    int MakeOffer(int from, int to, const int give[G_COUNT], const int get[G_COUNT], float truceDays);
    bool AnswerOffer(int side, int id, bool accept);
    bool PayGoods(int side, const int goods[G_COUNT], bool take);   // take from (or check) a colony's stores
    void GiveGoods(int side, const int goods[G_COUNT]);
    void BotSociety(float dt);                   // a bot's research, shrine, trade and boom (the colony in the fields)
    void TraderStep(Bird& b, float dt);
    void PriestStep(Bird& b, float dt);
    void InitTowns();
    // stage 7 (flight_danger.cpp): the dangerous islands, the weather, holding islands, sieges, bombs and stimulants
    Kraken kraken; Ape ape; Volcano volcano; WreckState wreck; Weather weather;
    // the long match (flight_long.cpp): seasons and their events
    int seasons = 0;                            // (MapOpts::seasons) 0: a standard match, no seasons
    int seasonEvent = -1; float eventUntil = 0; uint32_t eventsDone = 0; float eventDay[EV_SEASON_COUNT] = {-1, -1, -1, -1};
    std::vector<int> tuna;                      // (the Tuna Run's school: agent indices)
    struct RelicSpot { Vector3 pos{}; int relic = -1, isle = -1; bool taken = false; };
    // the neutral factions (doc p46)
    struct Pirates { Vector3 pos{}; float hp = 400, stealT = 0, scatterUntil = 0, hireUntil = 0; int target = -1, hiredBy = -1, loot = 0; bool on = false; };
    struct Boat { Vector3 pos{}, goal{}; float chumT = 0; };
    struct GreyWings { int isle = -1; Vector3 crag{}, hunter{}; float hp = 300, huntT = 0, hunterT = 0; bool dead = false; std::vector<float> peaceUntil; };
    Pirates pirates; std::vector<Boat> fleet; GreyWings grey;
    std::vector<RelicSpot> relicSpots;          // (relics lying on the dangerous islands)
    int greatEvent = -1, greatZone = -1, treasure = 0, legendFree = -1; float greatDay = -1, greatUntil = 0; bool greatAnnounced = false, greatDone = false;
    Vector3 greatPos{}, walkFrom{}, walkTo{}, legendPos{};
    Vector3 tunaFrom{}, tunaTo{};
    std::vector<uint8_t> zoneNear;              // (a ground near an island's shore: summer thins them)
    int GameDay() const { return (int)(time / DAY) + 1; }
    int Season() const;                         // SEASON_* or -1 in a standard match
    const SeasonFx& SeasonNow() const;          // (a standard match: no change)
    bool EventNow(int e) const { return seasonEvent == e && time < eventUntil; }
    void InitSeasons();
    void StepSeasons(float dt);
    float RegrowMul(int zone) const;
    void RespawnNow() { Respawn(); }            // (tests)
    void FledgeNow(Bird& b) { Fledge(b); }      // (tests)
    const DecreeFx& DecreeOf(int side) const;   // today's decree for any side (none: no change); yesterday's after-effects folded in
    const DecreeFx& DecreeNow() const { return DecreeOf(cur); }
    void StepDecrees(float dt);                 // dawn: deal three to every colony; bots (and anyone who hasn't picked by mid-morning) choose
    bool PickDecree(int k);                     // the colony in the fields takes offer k (0-2)
    int BotDecree() const;                      // (what a bot would pick from its offer)
    PerkDef PerksOf(int side) const { return PerkSum(FounderOf(side).perks); }
    void StepPerks(float dt);                   // the Founder levels at days 3, 7 and 11: three perks offered (a bot picks at once)
    bool PickPerk(int k);                       // the Founder in the fields takes offer k
    int BotPerk() const;
    void StepVeterans(float dt);
    bool HasRelic(int side, int relic) const;
    bool HasLegend(int side, int legend) const;
    int RelicCount(int side) const;
    void InitRelics();
    bool TakeRelic(int spot);                   // the colony in the fields picks up a relic (at most three)
    bool RecruitLegend();                       // the Founder gives the Visitor a fish of size 3+
    void GiveLegend(int side, int legend);
    bool GreatNow(int e) const;
    void StepGreat(float dt);
    void InitFactions();
    void StepFactions(float dt);
    void BotFactions();
    int LegacyScore(int side, int* part = nullptr) const;   // (the long match's additions to the score, doc p50: part[6] veterans, relics, legend, monuments, decrees, truces)
    IsleState isx;                              // (the expansion's islands, doc pp. 43-45)
    // the Long Flight (flight_longflight.cpp)
    bool LongFlight() const { return seasons >= 6; }
    int Year() const;                           // 1 or 2
    float FounderAge(int side) const;           // days into its generation
    int AgeStage(int side) const;               // 0 young, 1 prime, 2 old
    void Chronicle(int side, int kind, const std::string& text);
    std::string DynastyOf(int side) const;
    bool MarkHeir(int id); bool MarkNextHeir();
    void Succeed(int side);
    void StepGenerations(float dt);
    int Elders(int side, int trait = -1) const;
    float eventDay2[4] = {-1, -1, -1, -1};      // (the Long Flight: year two's season events)
    void InitIsles(); void StepIsles(float dt); void SetGhostPose();
    bool IsleShields(int isle, int threat) const;   // (the island keeps raiders off its nests: sheer ice, the roots, the Maelstrom's rocks, the beam at night)
    bool Calm() const;                          // (a dead-calm day: the Maelstrom's rocks can be reached)
    int IsleOfType(IsleType t) const;
    float IsleConv(int side) const;             // (Bird Island: how far a side's priests have converted it)
    bool InRookery(const Nest& n) const;        // (structures, doc p49)
    bool LightBeacon();
    void StepStructures(float dt);
    bool LayStructure(int kind);
    bool OnPerch(int side, Vector3 p) const { const Structure* s = BuiltOf(ColOf(side), ST_PERCH); return s && fabsf(s->pos.x - p.x) < 2 && fabsf(s->pos.z - p.z) < 2; }
    bool WaterNear(Vector3 p, float r, Vector3* at = nullptr) const;   // (nest styles, doc p49)
    bool NestStyleFits(int style, int site) const;
    void StyleNest(Nest& n, bool tell = false);
    int NestCostOf(int style) const;
    int NestCost(const Nest& n) const { return NestCostOf(n.style); }
    int NestEggsOf(const Nest& n) const;
    void StepNestStyles(float dt);
    bool TechUsable(int tk, int zone, const Bird* b = nullptr) const;   // (fishing mastery, doc p48)
    int BestTech(int zone, const Bird* b = nullptr) const;
    int PickTech(int zone, const Bird* b = nullptr);
    TechMod TechMods(int tk, int zone, int size) const;
    void TechLog(int zone, int tk, int what);
    float TechRate(int zone, int tk, int what, int* tries = nullptr) const;
    int OfferPact(int to);                      // (diplomacy, doc pp. 47-48: the colony in the fields)
    int OfferLoan(int to, int flock, int fish);
    bool PostBounty(int target, int fish);
    bool BreakTruce(int with);
    void CollectBounty(int victim, int killer);
    void StepDiplomacy(float dt);
    bool HirePirates(int target);               // the colony in the fields pays the Frigate Pirates to raid a colony for a day
    bool PayTribute();                          // ... and the Grey Wings for a day's peace                // warriors that have survived three fights become veterans (the colony in the fields)
    std::string VetLabel(const Bird& b) const;  // "Old Gray, Fearless" ("" for a bird that isn't one)
    int wantTraitOrder = 0;                     // (scratch)
    std::vector<std::string> lookOf;            // (stage 8) per absolute side: "costume;livery colour;livery hat" (cosmetic; from the hello)
    std::string& LookOf(int s) { if ((int)lookOf.size() <= s) lookOf.resize(s + 1); return lookOf[s]; }
    std::vector<int> outpostIsle;               // (scratch)
    int HolderOf(int isle) const;               // the side with the most built nests on an island (-1: nobody, or a tie)
    int NestsOn(int side, int isle) const;
    int OutpostSite() const;                    // (a site for the next nest on an outpost that has fewer than three)
    bool FoundOutpost(int isle, Vector3 near);  // the colony in the fields lays out a nest and a cache on another island
    void InitDanger();
    void StepDanger(float dt);                  // the monsters, the volcano, the wreck, the weather (once a step)
    void StepWorks(float dt);                   // bombs and stimulants at the colony's Works (the colony in the fields)
    void Blast(Vector3 at, int side, int kind); // a bomb (kind 0 plain, 1 incendiary, 2 blockbuster)
    bool Dose(int flock, int stim);             // a stimulant into a flock of the colony in the fields
    bool Grounded(Vector3 p) const;
    float ApeCeiling(Vector3 p, Vector3 goal) const;   // (a bird passing an awake ape climbs above its throws: the height to keep, or 0)             // (a storm, or the volcano's ash: birds stay down)
    bool Blockaded(int zone, int side) const;   // a hostile flock holds that ground
    bool Walled(int isle, int side) const;      // a hostile Wall holds the air over that island
    bool OfferKraken();                          // the Founder at the cove with a fish
    bool FeedApe();                              // the Founder at the ape with a big fish
    void SetWreckPose();                         // (the wreck where it has drifted to by now)
    bool StormNow() const { return weather.kind == 1; }
    bool FogNow() const { return weather.kind == 2; }
    void BotDanger(float dt);                   // a bot's expansion, offerings, sieges and bombs (the colony in the fields)
    void PredictFounder(float dt, const FounderInput& in);   // the guest's own Founder between snapshots
    void StepFog(float dt);                     // the fog round the side in the fields
    // war (flight_war.cpp)
    void StepWar(float dt);
    void BotWar(int side, float dt);            // a bot's decisions: its plan, its flocks' orders
    void BotGovern(float dt);                   // a bot's colony panel (nests, plan, retraining), on the colony swapped in
    int MakeFlock(int side, const std::vector<int>& ids, Formation f, Alt a, Stance s);
    void OrderFlock(int side, int flock, Target t, int tSide, int tIsle, int tZone, int tFlock, Vector3 at);
    Flock* FindFlock(int side, int id);
    Bird* FindBird(int side, int id);
    float Morale(int side, const Flock& f) const;
    std::vector<std::string> warLog;            // (fights, for the tests and the HUD)
    struct WarFx { Vector3 p{}; int kind = 0; int side = 0; Role role = Role::None; float yaw = 0; };   // 0 a hit, 1 a death (it falls), 2 a net
    std::vector<WarFx> warFx; size_t warFxBase = 0;   // (the scene reads them by cursor; trimmed now and then)
    Knowledge know;
    std::unique_ptr<rt::MapData> sea;           // the generated sea (a whole map)
    std::vector<uint8_t> liveZone; std::vector<float> zoneNearT;   // zones with fish in them now; when a bird was last near
    int inshoreZone = -1;
    std::vector<float> zoneDay;                 // each zone's daytime shoal depth (stage 4: the stack's upwelling)
    float HeightAt(float x, float z) const;     // every island's ground; the deep sea elsewhere
    bool LandAt(float x, float z) const { return HeightAt(x, z) > 0.15f; }
    Vector3 GroundAt(float x, float z) const { return {x, std::max(0.0f, HeightAt(x, z)), z}; }
    Vector3 NormalAt(float x, float z) const;
    int IsleAt(float x, float z, float pad = 0) const;   // the island whose land reach covers the point, or -1
    void Reveal(Vector3 p, float radius, int landedIsle = -1);
    bool SendScout(int isle, int zone, Vector3 at, Alt alt);   // an idle scout flies there; false if none
    void ScoutReport(Bird& b);
    void ScoutStep(Bird& b, float dt);
    void StepMap(float dt);                     // the fog, the live zones
    void Step(float dt, const FounderInput& in); // dt in real seconds (the slow motion scales the world inside)
    void Say(const std::string& s);              // (your log; silent while a rival steps)
    float Rand();
    // the sea
    Vector2 WindAt() const { Vector2 v = wind.At(time); float k = (weather.kind == 1 ? 2.5f : 1.0f) * SeasonNow().windK * (GreatNow(GE_CALM) ? 0.0f : 1.0f); return {v.x * k, v.y * k}; }   // (a storm: a gale)
    float Thermal(Vector3 p) const;             // updraft m/s (the hill in the afternoon)
    int FishNear(Vector3 p, float r, float maxDepth, int* count = nullptr) const;   // nearest catchable fish agent
    float FeedValue(int sp) const;              // a fish's size class
    // the colony (flight_colony.cpp)
    std::vector<CachedFish>& Cache0() { return col.caches[0].fish; }
    const std::vector<CachedFish>& Cache0() const { return col.caches[0].fish; }
    void InitColony();
    void StepColony(float dt);
    std::string InteractHint() const;           // what E does here (the HUD shows it)
    void Interact();                            // the Founder's E
    bool Eat();                                 // the Founder's F
    int NearestCache(Vector3 p, bool withFish, bool withRoom) const;
    int NearestNest(Vector3 p, float r) const;
    int NearestSite(Vector3 p, float r) const;
    int NearestTwigs(Vector3 p, float r) const;
    float FeedPerDayEstimate() const;           // what the fishers bring in a day
    float MouthsPerDay() const;                 // what the colony eats in a day
    float DaysOfFood() const;
    float CacheFeed() const;
    int Count(BStage s, Role r = Role::None) const;
    int Adults() const;                         // adults and mates, the Founder included
    int Alive() const;                          // every bird, eggs not counted
    float StockOf(int zone) const;              // standing fish in a zone (vs its capacity: 0..1)
    void ColonySay(const std::string& s) { Say(s); }
    int BotFounderStep(float dt);               // (founderBot) a turn of the Founder's colony job
    bool Retrain(Role to, Role from = Role::None);   // one adult retrains (a day); from None: the biggest other role
  private:
    void RegrowFish(float dt);
    void StepBird(Bird& b, float dt);
    void FisherStep(Bird& b, float dt);
    void FeederStep(Bird& b, float dt);
    void BuilderStep(Bird& b, float dt);
    void MateStep(Bird& b, float dt);
    void Fledge(Bird& b);
    void BirdDies(Bird& b, const std::string& cause);
    bool MoveTo(Bird& b, Vector3 goal, float speed, float dt, float arrive = 1.5f);
    bool BirdEatsAtCache(Bird& b, float dt);
    int ChooseGround(Vector3 from) const;
    void DayTick();
  public:
    float dayAcc = 0; int dayNum = 0;
    float fogT = 0, liveT = 0, regrowAcc = 0; bool fogNow = false;   // (the map's timers)
    void StepFounder(float dt, const FounderInput& in);
    void StrikeStep(float realDt, const FounderInput& in);   // the strike, on real time
    void Kill(const std::string& cause);
    void SyncBody();
    void HookDiverHits();                       // a shark's strike on a Founder's body in the web: whose it is, and its death
  private:
    void FlyMotion(float dt, const FounderInput& in);   // the Founder's flight (Fly): steering, speed, breath, the wind
    void StartStrike();
    void ResolveStrike();
    void Respawn();
};

int RunFlightTest();                            // depth.exe --flight-test
int RunFlightColonyTest();                      // depth.exe --flight-colony-test
int RunFlightSim(int argc, char** argv);        // depth.exe --flight-sim <island> <days> [careful|lagoon] [founder] [seed]
int RunFlightFairTest(int argc, char** argv);   // depth.exe --flight-fair [seed]: every arrangement and player count is fair
int RunFlightScoutTest();                       // depth.exe --flight-scout-test: the stage-3 gate (a scout's report from each altitude)
int RunFlightWar(int argc, char** argv);         // depth.exe --flight-war [scenario|all] [runs]: the five rules in scripted fights; the stage-4 gate
int RunFlightFounders(int days, int seeds);      // depth.exe --flight-sim founders [days] [seeds]: every founder's window (the stage-6 gate)
int RunFlightSocietyTest();
int RunFlightSiege(int argc, char** argv);      // depth.exe --flight-siege [runs]: the stage-7 gate (a bot takes the cove and holds it through a siege)
int RunFlightDangerTest();                      // depth.exe --flight-danger-test: the dangerous islands, bombs, stimulants, sieges                     // depth.exe --flight-society-test: research, faith, trade, the founders (stage 6)
int RunFlightNetTest();                         // depth.exe --flight-net-test: inputs, orders, snapshots, mirrors, the score (flight_net.cpp)
int RunFlightNetLoop(bool forceMemory);         // depth.exe --net-loop flight [mem]: the stage-5 gate (six players finish a 30-minute match)

}  // namespace fl
