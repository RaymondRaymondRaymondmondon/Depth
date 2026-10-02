#pragma once
// ============================================================================
//  RED TIDE - arcade game 5 of the Deep Arcade (Red_Tide_Reference/, "Red Tide - Arcade Game 5 Design Document").
//  A four-diver survival shooter in living ecosystems. This header is the data model (loaded from
//  data/redtide/*.json, exported from the reference workbooks by tools/export_redtide.py) and the Ecosystem:
//  the one simulation every map runs, in which beasts, flora, enemy factions and divers share a food web,
//  a 3D scent grid, a sound field and a regional alarm (design doc, "The ecosystem engine").
//
//  World units are metres: x runs bow to stern along the blockout's x, z is the blockout's y, y is up.
// ============================================================================
#include "json.h"
#include "raylib.h"
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace rt {

// ---------------------------------------------------------------- data (one record per workbook row)
struct Species {
    int id = 0;
    std::string name, cls, archetype, social, homeZone, homeFlora, weakPoint, special;
    std::vector<std::string> tags;
    int size = 1, tier = 1, groupSize = 1;
    float defendR = 0, bloodThreshold = 0, aggression = 0, fear = 0, curiosity = 0;
    float sight = 10, scent = 10, hearing = 10, electro = 0, armorFront = 0;
    float hpBase = 20, bountyBase = 40, bloodDeath = 20, bloodPerS = 2, speed = 2, turnDeg = 360, dropPct = 1;
    bool isEnemy = false;              // a faction unit, a member of the same web
    bool isDiver = false;              // the players' record (divers are agents in the web too)
    bool bloodless = false;            // leaves no blood (the Cave's Drowned: "they do not bleed")
    std::string art;                   // draw (and size) as this art row (a species added in extra.json)
    float artScale = 1, attackScale = 1;
    std::string attacksAs;             // use this species' attacks
    bool Has(const char* tag) const;
    bool Scavenger() const { return Has("scavenger"); }
    bool Cleaner() const { return Has("cleaner"); }
    bool Parasite() const { return Has("parasite"); }
    bool Sessile() const { return Has("sessile"); }
};
struct Attack {
    std::string beast, name, tell, effect;
    float damage = 0, windup = 0, cooldown = 0, range = 1;
};
struct Flora {
    std::string name, type, effect;
    std::vector<std::string> zones, grazedBy;
    float contactDamage = 0, regrowPerS = 0, regrowDelay = 0;
};
struct SpawnRow { std::string zone, species; int count = 0; float respawnS = 0, capMult = 1.03f; };
struct TideRow {
    int tide = 1, quota4p = 12, tideBonus = 100;
    float hpMult = 1, bountyMult = 1, dmgMult = 1, alarmThreshold = 90, enemySpawnChance = 0, bloodDecay = 0.02f;
    std::string hunt, apexWander, bossMayAppear;
};
struct Zone {
    std::string name, deck, notes;
    Rectangle plan{};                  // metres, x/z
    float y0 = 0, y1 = 8;              // floor and ceiling
    Vector3 flow{0, 0, 0};             // current, m/s
    int doorCost = 0;
    int alarmRegion = 0;
    bool diverOk = true;               // false: open water the divers only see and shoot into (a confined map's outside)
    bool air = false;                  // an air chamber: divers walk (the Cave's dry chambers)
    Vector3 life{}; float lifeR = 0;   // where a vast zone's life keeps (the Void's rim: near the hatch); 0 = anywhere
    bool radial = false;               // Atlantis districts: an annulus sector (plan is its bounding box)
    float rMin = 0, rMax = 0, a0 = 0, a1 = 0; // metres and degrees
    // A zone built from several boxes (extra.json "zone_parts": the Atlantis ring wall). Boxes that only touch would
    // be walls, so each seam between two touching boxes gets a hidden connector box straddling it (inside both, so it
    // never reaches outside the zone); the renderer draws no faces for connectors or between the zone's own boxes.
    // plan is their bounding box.
    struct Part { Rectangle r{}; bool hidden = false; };
    std::vector<Part> parts;
    std::vector<std::vector<int>> partAdj;   // parts that overlap (the steering graph)
    Vector3 Center() const;
    bool Contains(Vector3 p, float pad = 0) const;
    Vector3 Clamp(Vector3 p, float pad = 0.5f) const;
    // the next point to steer for inside this zone on the way from `from` to `to` (`to` itself unless the zone is
    // made of parts and the straight line would leave it: then the seam into the next part along the part graph)
    Vector3 Waypoint(Vector3 from, Vector3 to) const;
    void BuildPartGraph();
};
struct Link {
    int from = 0, to = 0; int cost = 0; std::string passage; bool oneWay = false; float flow = 0; Vector3 a{}, b{};
    bool diverOk = true;               // false: beasts (and darts) only
    int beastRule = 0;                 // 0 a door (closed to beasts until bought), 1 never open to beasts, 2 a breach (opens at openTide or when the faction first comes)
    int openTide = 99;
    int opensWith = -1;                // another link whose door opens this one too (Atlantis's parade stair)
    bool slip = false; float slipSpeed = 8;  // a slipstream: a one-way current that carries divers and beasts (not a swimmable passage)
};
// A porthole: a dart-only opening from a room the divers use into open water they don't (extra.json "windows").
struct Window { Vector3 lo{}, hi{}; int zone = -1, outside = -1; int axis = 2; float g0 = 0, g1 = 0; bool outHigh = false; };   // g0..g1: the wall it pierces; outHigh: the water is on the high side
struct Poi { std::string name, type, zoneName; int zone = -1; Vector3 pos{}; int step = 0; };
struct AlarmRegion { std::string name; float mult = 1; std::vector<int> zones; std::string entry; };
struct FactionUnit {
    std::string unit, weapon, role, tell, drops;
    float hp = 100, damage = 20, interval = 2, range = 2, speed = 2; int loot = 200; bool huntOnly = false, bloodless = false;
};
struct Faction {
    std::string name, speciesName, entryPoi, huntStaging;
    std::vector<std::string> patrol, composition;
    std::vector<FactionUnit> units;
    int fleeFromSize = 4, ignoreBelowSize = 3;
    bool bleeds = true, slipstreams = true, ignoreBeasts = false, huntsBeasts = false, shootsAllies = false;
    std::map<std::string, std::vector<std::string>> barks;
};
struct DietRow { std::vector<std::pair<int, float>> prey; float corpse = 0, plankton = 0, parasites = 0, flora = 0, enemy = 0; std::vector<std::pair<std::string, float>> floraItems; };

struct MapData {
    std::string key, title;
    std::vector<Species> species;
    std::vector<Attack> attacks;
    std::vector<DietRow> diet;         // by species index
    std::vector<std::string> foodNames;
    std::vector<Flora> flora;
    std::vector<SpawnRow> spawns;
    std::map<std::string, double> tunables;
    std::vector<TideRow> tides;
    std::vector<Zone> zones;
    std::vector<Link> links;
    std::vector<Poi> pois;
    std::vector<AlarmRegion> alarmRegions;
    std::vector<Window> windows;
    std::vector<std::string> notes, boss, readme;
    Json extra;                        // the map's extra.json (map mechanics read their own sections)
    Faction faction;
    int enemySpecies = -1;             // index of the faction's species record (appended at load)
    Vector3 boundsMin{}, boundsMax{};
    int SpeciesIndex(const std::string& name) const;
    int ZoneIndex(const std::string& name) const;   // accepts the workbook's short names ("Salon")
    const TideRow& Tide(int tide) const;
    std::map<std::string, std::string> zoneAlias;
};

// Engine-wide numbers (RedTide_Engine_and_Systems.xlsx): Constants, Movement, Drops.
struct EngineData {
    Json constants, movement, drops, progression, barks, bodyplans, palettes;
    float C(const char* name, float def = 0) const;                  // a scalar constant
    float CBy(const char* name, int index, float def = 0) const;     // an element of a list constant ([..] by size)
    float M(const char* name, float def = 0) const;                  // a movement value
};

// Finds data/redtide (next to the exe, in the working directory, or up to three levels above the exe).
std::string DataDir();
const EngineData& Engine();
const MapData& Map(const std::string& key);      // loads on first use; "ship", "cave", "reef", "atlantis", "void"
bool DataOk(std::string* why = nullptr);

// ---------------------------------------------------------------- the simulation
enum class State : uint8_t { Graze, Rest, Investigate, Hunt, Feed, Flee, Defend, Return, Attached, Dead };
const char* StateName(State s);

struct Agent {
    int sp = 0;                        // species index
    Vector3 pos{}, vel{}, home{};
    int zone = 0, homeZone = 0;
    State st = State::Graze;
    float hp = 1, hpMax = 1, hunger = 0.3f, stateT = 0, fedT = 0, eatT = 0, cooldown = 0, decideT = 0;
    int target = -1;                   // agent index being hunted/defended against, or corpse index while feeding (see targetCorpse)
    bool targetCorpse = false;
    Vector3 goal{};                    // where Investigate/Return/Graze is heading
    bool alive = true;
    bool fromRespawn = false;
    int group = -1;                    // school or pack id
    int host = -1;                     // parasites: who it's on
    int squad = -1;                    // enemies: which squad
    int unit = -1;                     // enemies: which FactionUnit
    int diver = -1;                    // divers: which player slot (0-3); -1 for everything else
    float wound = 0;                   // 0..1, drives the bleeding rate
    float fleeBleedT = 0;              // seconds since reaching home while fleeing
    float aggrMod = 0;                 // symbiosis: cleaners calm (negative), losses enrage (positive)
    float stun = 0, held = 0;          // stunned seconds; a hold on this agent (net, grab)
    int lostPrey = -1; float lostPreyT = 0; // an escaped prey it won't chase again for a while (lost it in the ink)
    bool downed = false;               // divers: downed (a blood source; beasts stop hunting it)
    bool weakHit = false;              // the last hit that landed was on the weak point (for the bounty)
    bool oil = false;                  // a clockwork (the Void's Sentinels): it leaks oil, not blood
    uint32_t rng = 1;
    std::vector<int> path;             // zone path when crossing links
    int pathStep = 0;
};
struct Corpse {
    int sp = 0; Vector3 pos{}; int zone = 0;
    float bloodLeft = 0, life = 0, age = 0; bool active = true; int feeders = 0; bool byPlayer = false;
};
struct FloraPatch { int flora = 0, zone = 0; Vector3 pos{}; float units = 100, regrowT = 0; };
struct Squad { int region = 0; std::vector<int> members; Vector3 goal{}; float lostT = 0; bool alive = true; bool hunt = false; };

// A 3D grid over the map's bounding box (2 m cells). Cells outside every zone are rock and hold nothing.
struct Field {
    int nx = 0, ny = 0, nz = 0; float cell = 2; Vector3 origin{};
    std::vector<float> v, tmp;
    std::vector<int16_t> zone;         // -1 rock
    std::vector<double> sat, satX, satY, satZ; // summed-volume tables, rebuilt on each update (O(1) box sums)
    int Idx(int x, int y, int z) const { return (z * ny + y) * nx + x; }
    bool Cell(Vector3 p, int& x, int& y, int& z) const;
    float At(Vector3 p) const;
    void Add(Vector3 p, float amount);
    void BuildSat();
    // Sum of values in a cube of half-size r around p, and the value-weighted centroid of that cube.
    float BoxSum(Vector3 p, float r, Vector3* centroid = nullptr) const;
    // Sum over an axis-aligned box in world metres; adds value*position into *weighted (for centroids).
    double BoxSumAABB(Vector3 lo, Vector3 hi, Vector3* weighted = nullptr) const;
    float Total() const;
};

struct EcoEvent { float t; std::string text; };
struct Arrival { std::string species; float investigateT = -1, arriveT = -1; };

struct Ecosystem {
    const MapData* map = nullptr;
    const EngineData* eng = nullptr;
    std::vector<Agent> agents;
    std::vector<Corpse> corpses;
    std::vector<FloraPatch> flora;
    std::vector<Squad> squads;
    std::vector<float> alarm;          // per alarm region
    std::vector<float> spawnT;         // per spawn row: time until the next replacement
    std::vector<int> spawnAlive;       // per spawn row: living agents that belong to it
    Field scent, sound;
    float time = 0, scentT = 0, decideAcc = 0, popT = 0, alarmRollT = 0;
    int tide = 1, players = 4;
    float cleanerRage = 0;             // seconds left of the map-wide +20% aggression after the cleaners were wiped
    float cleanerRageRegionMask = 0;
    bool log = false;
    uint32_t rng = 12345;
    std::vector<EcoEvent> events;
    std::vector<int> killsBySpecies, deathsBySpecies; // deaths: eaten or killed by anything
    std::map<std::pair<int, int>, int> eatenBy;        // (killer species, victim species) -> count: 'beasts eaten by beasts'
    int squadsSpawned = 0;
    std::function<void(int agent, int killer)> onDeath;  // the game layer hooks scrip and drops here
    // The match layer: a beast's strike on a diver goes to the game (which picks the attack and its effect) instead of
    // the agent's hp; a decision hook may take over an agent's choice for this tick (return true); closed doors
    // (per link) keep beasts out of rooms the divers haven't opened; suppressBlood makes kills clean (Purge).
    std::function<void(int diverAgent, int attacker, float dmg)> onDiverHit;
    std::function<bool(Agent& a, int idx)> decideHook;
    std::vector<char> linkClosed;
    bool suppressBlood = false;
    float bloodMult = 1;               // Blood Frenzy: the water fills with blood at 5x
    float alarmMult = 1, decayMult = 1, killBloodMult = 1;   // the modes: Quiet Water halves the alarm and the decay; Feeding Frenzy stops it and bleeds kills 3x
    bool LinkOpen(int li) const { return li < 0 || li >= (int)linkClosed.size() || !linkClosed[li]; }
    void SpawnSquad(int region, bool hunt, int count = -1, bool leader = false);

    void Init(const MapData& m, uint32_t seed, int tideNum = 1, int playerCount = 4);
    void Step(float dt);                                   // advance the whole web
    void SetTide(int t);
    int Spawn(int sp, Vector3 pos, int zone);
    void Damage(int agent, float dmg, int attacker, bool melee = false, bool weakPoint = false);
    void Kill(int agent, int killer, bool melee = false);
    void AddBlood(Vector3 pos, float amount);
    void AddNoise(Vector3 pos, float noise, bool explosion = false);   // weapon noise 1-10 (x10 into the field) plus the alarm
    void AddChum(Vector3 pos, float amount) { AddBlood(pos, amount); }
    int ZoneAt(Vector3 p) const;
    int Count(int sp) const;
    int CountInZone(int sp, int zone) const;
    float BloodNear(Vector3 p, float r) const { return scent.BoxSum(p, r); }
    // What a beast in zone `zone` smells within r of p: blood in its own zone and the zones one passage away only,
    // so blood has to travel through the doorways (with the current) before a beast in another room notices it.
    float Smell(Vector3 p, int zone, float r, Vector3* centroid = nullptr) const;
    float SoundAt(Vector3 p) const { return sound.At(p); }
    float DecayRate() const;
    float Rand();                                          // 0..1
    float Rand(float a, float b) { return a + (b - a) * Rand(); }
    void Event(const std::string& s);
    std::vector<int> ZonePath(int from, int to, bool enemy = false, int size = 0) const;
    float flowSign = 1;                // the Reef's tide: -1 while the flood runs the set backward
    std::vector<float> narrowInit; float erosionT = 0, soundAcc = 0;
    // ink clouds (an ink bomb, an ink cap): nothing sees or smells through them for their seconds
    struct Ink { Vector3 pos{}; float r = 5, t = 8; };
    std::vector<Ink> inks;
    bool InInk(Vector3 p) const { for (const auto& k : inks) { float dx = p.x - k.pos.x, dy = p.y - k.pos.y, dz = p.z - k.pos.z; if (dx * dx + dy * dy + dz * dz < k.r * k.r) return true; } return false; }
    void AddInk(Vector3 p, float r, float t) { inks.push_back({p, r, t}); }
    // bubble walls (a salvage build): a vertical curtain from a to b (x/z) that beasts up to maxSize won't cross
    struct Curtain { Vector3 a{}, b{}; float y0 = -1e9f, y1 = 1e9f, t = 60; int maxSize = 2; };
    std::vector<Curtain> curtains;
    bool CrossesCurtain(Vector3 from, Vector3 to, int size) const;
    std::string entryOverride;         // a zone the faction enters by instead (the Cave, once the Lantern Cache is opened)
    std::vector<int> zoneMaxSize;      // per zone: the largest size that fits its corridors (99: anything), relaxed as they erode
    // The divers are agents too (species index = diverSpecies); the game layer moves them.
    int diverSpecies = -1;
    int AddDiver(int slot, Vector3 pos);

  private:
    void UpdateFields(float dt);
    void Decide(Agent& a, int idx);
    void Move(Agent& a, int idx, float dt);
    void Population(float dt);
    void AlarmUpdate(float dt);
    int FindPrey(const Agent& a, int idx, float range) const;
    int FindThreat(const Agent& a, int idx) const;
    int FindCorpse(const Agent& a, float range) const;
    bool Eats(int predSp, int preySp) const;
    float Aggression(const Agent& a) const;
    void SteerTo(Agent& a, Vector3 goal, float speed, float dt);
    void Schooling(Agent& a, int idx, float dt);
  public:
    void SteerToPublic(Agent& a, Vector3 goal, float speed, float dt) { SteerTo(a, goal, speed, dt); }   // (the tests)
};

// Body sizes for hits (length along the spine, capsule radius), from the art workbook's rows: the numbers the
// CreatureBuilder builds with (redtide_render.cpp; needs no window).
struct Body { float length = 0.5f, radius = 0.1f; };
Body BodyOf(const std::string& artKey, const std::string& speciesName);

// Headless tools (depth.exe --eco-sim, --web-check).
int RunEcoSim(const std::string& mapKey, float minutes, const std::string& pattern);
int RunWebCheck(const std::string& mapKey);
int RunEcoTest(const std::string& mapKey);

} // namespace rt
