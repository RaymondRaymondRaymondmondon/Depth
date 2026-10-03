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

// ---------------------------------------------------------------- the island (stage 1: the tropical island)
struct Island {
    int n = 0; float cell = 2, x0 = 0, z0 = 0;   // the heightmap's grid
    std::vector<float> h;                       // metres above the sea (negative: the sea floor)
    std::vector<Vector3> palms;                 // trunk feet
    std::vector<float> palmH;                   // their heights (the crown is at the foot + 0.97 h)
    int nestPalm = -1;
    Vector3 hill{}, nest{};                     // the hill's top; the Founder's first nest (in a palm by the hill)
    uint32_t seed = 1;
    void Generate(uint32_t seed);
    float Height(float x, float z) const;       // bilinear; the open sea beyond the grid is -12
    Vector3 Normal(float x, float z) const;
    bool Land(float x, float z) const { return Height(x, z) > 0.15f; }
    Vector3 Ground(float x, float z) const { return {x, std::max(0.0f, Height(x, z)), z}; }   // where a bird stands (the surface over water)
};

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
enum class Role : uint8_t { None, Fisher, Feeder, Builder, COUNT };
struct RoleDef { std::string key, name, what; float hp = 60, speed = 12; int carry = 2; };
const RoleDef& RoleOf(Role r);
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
struct Stock { int row = 0, sp = 0, zone = 0; float K = 0, births = 0; };   // a fishing ground's species (one spawn row)
struct DayStats { int day = 0, birds = 0, eggs = 0, chicks = 0, mates = 0, nests = 0, caught = 0, deaths = 0; float feedCaught = 0, mouths = 0, cacheFeed = 0, lagoon = 0; };
struct Colony {
    std::vector<Bird> birds;
    std::vector<Nest> nests;
    std::vector<Cache> caches;
    std::vector<Site> sites;
    std::vector<TwigSource> twigSrc;
    int twigs = 0, shells = 0, wildMates = 30, nextId = 1;
    float plan[(int)Role::COUNT] = {0, 0.5f, 0.2f, 0.3f};   // the fledging plan: the share of each role
    int ground = -1;                          // fishers' ground: -1 the best, else a zone index
    float restBelow = 0;                      // fishers leave a ground resting while its stock is under this (0 never)
    int nestsWanted = 2;                      // builders raise nests (on free sites) until there are this many
    float feedToday = 0, feedYesterday = 0;   // feed caught by the colony's fishers
    int caughtToday = 0, deathsToday = 0;
    std::vector<std::pair<std::string, int>> deaths;   // by cause
    std::vector<DayStats> days;
    bool leaderless = false;
};

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
    static constexpr float DAY = 240;           // a game day is four real minutes
    float DayPhase() const;                     // 0 midnight .. 0.25 dawn .. 0.5 noon .. 0.75 dusk
    const FounderDef& Def() const { return Founders()[me.def]; }
    void Init(const std::string& founderKey, uint32_t seed);
    void Step(float dt, const FounderInput& in); // dt in real seconds (the slow motion scales the world inside)
    void Say(const std::string& s);
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
  private:
    void StepFounder(float dt, const FounderInput& in);
    void StartStrike();
    void ResolveStrike();
    void Kill(const std::string& cause);
    void Respawn();
    void SyncBody();
};

int RunFlightTest();                            // depth.exe --flight-test
int RunFlightColonyTest();                      // depth.exe --flight-colony-test
int RunFlightSim(int argc, char** argv);        // depth.exe --flight-sim <island> <days> [careful|lagoon] [founder] [seed]

}  // namespace fl
