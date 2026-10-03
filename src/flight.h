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
enum class IsleType : uint8_t { Tropical, Stack, Town, Atoll, Islet, KrakenCove, Skull, Volcano, ReefGarden, Wreck, COUNT };
const char* IsleTypeName(IsleType t);
inline bool IsStartType(IsleType t) { return t <= IsleType::Atoll; }
inline bool IsDangerous(IsleType t) { return t >= IsleType::KrakenCove; }
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
    uint32_t seed = 1;
    void Generate(uint32_t seed);               // (the stage-1 tropical island at the origin)
    void Generate(IsleType type, uint32_t seed, Vector3 centre);
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
};
struct IsleSpec { IsleType type = IsleType::Islet; Vector3 c{}; int start = -1; std::string name; };
std::vector<IsleSpec> LayoutMap(const MapOpts& o);   // slot 0 at the origin; rotational fairness
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
enum class Role : uint8_t { None, Fisher, Feeder, Builder, Scout, Skirmisher, Tank, Striker, Watcher, Screamer, Flockmaster, COUNT };
inline bool IsWarrior(Role r) { return r >= Role::Skirmisher; }
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
};
struct Site { Vector3 pos{}; int palm = -1; int nest = -1; };
struct Nest {
    int site = -1; Vector3 pos{}; float twigs = 0; bool built = false, founders = false;
    int mate = -1; int bowl = 0, bowlNeed = 3; float mateT = -1;   // mateT: counting down to the mate's arrival
    float larder = 0;                         // feed laid in the nest (chicks and the mate eat from it)
    int shells = 0;                           // lining
};
struct Cache { Vector3 pos{}; std::vector<CachedFish> fish; bool built = true; float twigs = 0; };
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
    Formation form = Formation::Chevron; Alt alt = Alt::Mid; Stance stance = Stance::Raid;
    Target target = Target::Home; int tSide = -1, tIsle = -1, tZone = -1, tFlock = -1; Vector3 tAt{};
    Vector3 pos{}, vel{};                     // its middle, how it's moving
    float morale = 50, wins = 0, engagedT = 0, overWaterT = 0;
    int startSize = 0, lost = 0;
    bool retreating = false, scattered = false, leaderDead = false;
    std::string name;
};
// what a colony has raised to defend its nests (doc p24): hedges round nest sites, towers for Watchers
int StructureTwigs(int kind); int StructureShells(int kind);   // (flight_war.json: a hedge 10 twigs, a tower 20 and 5 shells)
struct Structure { int kind = 0; Vector3 pos{}; float twigs = 0; int shells = 0; bool built = false; int site = -1; };   // kind 0 hedge, 1 tower
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
struct ScoreCard { int birds = 0, nests = 0, isles = 0, cache = 0, kills = 0, founder = 0, total = 0; };

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
    Vector2 WindAt() const { return wind.At(time); }
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
int RunFlightNetTest();                         // depth.exe --flight-net-test: inputs, orders, snapshots, mirrors, the score (flight_net.cpp)
int RunFlightNetLoop(bool forceMemory);         // depth.exe --net-loop flight [mem]: the stage-5 gate (six players finish a 30-minute match)

}  // namespace fl
