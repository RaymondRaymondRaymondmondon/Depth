#pragma once
// The Trawl's food web (design doc, "The food web"): one ecosystem per fishing ground, run by the host.
//
// Two layers. The **population layer** holds every species' biomass for the whole ground (and five resources:
// plankton, benthos, algae, seagrass, carrion) and moves it with a consumer-resource model calibrated so the start
// numbers are its equilibrium: a ground with no crew stays put, and anything the crew takes, spills or shines moves
// it (netting forage leaves predators hungry, netting parrotfish lets algae smother the coral). The **agent layer**
// materialises groups (a school, a pack, one grouper) within 150 m of the boat from the population's density map,
// and they swim by the web's senses: light (the new layer), blood and scent, sound, vibration (the new layer), prey.
// Wake (the third new layer) sums what the crew puts into the water.
//
// Time: one real second is one minute on the night's clock (20:00-05:00 is nine real minutes). Population rates are
// per game hour; blood, vibration and Wake run in real seconds (the doc's "3% per second", "1 point a minute").
#include "trawl.h"
#include <map>
#include <string>
#include <vector>

namespace tw {

enum Band { BAND_AIR = -1, BAND_SURFACE = 0, BAND_UPPER, BAND_MID, BAND_DEEP, BAND_ABYSS, BAND_FLOOR, BAND_COUNT };
enum Habitat { H_LAND, H_OPEN, H_SEAGRASS, H_REEF, H_CREST, H_SEA, H_HOLES, H_SARGASSUM, H_KELP, H_BARREN, H_WALL, H_WRECK, H_COUNT };   // kelp canopy and urchin barrens: the Weeds; mould-lit walls and smugglers' wrecks: the Grotto
enum LightResp { LR_DRAWN, LR_NEUTRAL, LR_SHY };
enum Res { R_PLANKTON, R_BENTHOS, R_ALGAE, R_SEAGRASS, R_CARRION, R_COUNT };
const char* ResName(int r);
const char* HabitatName(int h);

// A species record (data/trawl/trawl_species.json)
struct SpeciesRec {
    std::string name, cls;
    int size = 1, tier = 1;
    float kgLo = 1, kgHi = 1;
    int band = BAND_UPPER, nightBand = BAND_UPPER;
    float habitat[H_COUNT] = {};
    std::vector<std::pair<int, float>> eats;   // node: a species index, or -1 - resource
    float start = 0; int schoolLo = 1, schoolHi = 1;
    float sight = 10, scent = 5, hearing = 5, lateral = 3;
    int light = LR_NEUTRAL; float fear = 0.5f, aggression = 0, curiosity = 0, speed = 1;
    std::vector<std::string> baits, tackle; bool nightBite = false, needsWire = false;
    Pattern fa = Pattern::None, fb = Pattern::None;
    float price = 0;
    bool bait = false, netOnly = false, pots = false, reef = false, grazesCoral = false, teeth = false, inks = false;
    bool threat = false, protectedSp = false, bycatchOnly = false, stings = false, isStatic = false, stealsDeck = false, ramsHull = false;
    float lifts = 3;   // a deck thief's heaviest fish (design doc v2: gull 3, brown pelican 5, frigatebird 2 kg)
    int thief = 0;                              // 1 strikes a hooked fish to the head, 2 takes it whole
    float bloodThreshold = 60;
    float pullK = 1, staminaK = 1, softMouth = 1;
    // calibrated by the ground (consumption per game hour, conversion, losses)
    float q = 0, e = 0.3f, m = 0, c = 0;
    float MeanKg() const { return 0.5f * (kgLo + kgHi); }
    float MeanSchool() const { return 0.5f * (schoolLo + schoolHi); }
    bool Takes(Tackle t) const;
};
struct ResourceRec { float turnover = 12, start = 0, r = 0, K = 0; };
struct GroundDef {
    std::string key, name;
    float size = 600, cell = 4, depthMin = 3, depthMax = 40, bloodDecay = 0.03f;
    Vector2 current{0.08f, 0.03f};
    float stirSafe = 120, stirCurve = 1.5f;     // the Stir clock (design doc, "Night pacing"): minutes of near-safety, and how steeply the threat curve rises after
    float stirFloor = 0.1f;                     // what the baseline leaves of the threats' presence at the start (a lone shark can still be about)
    ResourceRec res[R_COUNT];
    std::vector<int> species;                   // indices into SpeciesDB::sp
    std::vector<std::string> flora;
};
struct SpeciesDB {
    std::vector<SpeciesRec> sp;
    std::map<std::string, GroundDef> grounds;
    bool ok = false; std::string why;
    int Find(const std::string& name) const;
};
const SpeciesDB& Species();

// A field over the ground: 4 m cells, five depth bands (surface, upper, mid, deep, abyss)
struct Grid {
    int n = 0, nz = 5; float cell = 4;
    std::vector<float> v, tmp;
    void Init(int cells, float cellM) { n = cells; cell = cellM; v.assign((size_t)n * n * nz, 0); tmp = v; }
    static int BandOf(float depth);
    int Idx(int x, int y, int b) const { return (b * n + y) * n + x; }
    bool Cell(Vector3 p, int& x, int& y, int& b) const;
    void Add(Vector3 p, float amount);
    float At(Vector3 p) const;
    float Near(Vector3 p, int r = 1) const;     // the sum over the (2r+1)^2 cells around p in its band
    float Total() const;
    void Step(float dt, float decay, float diffuse, Vector2 current);
};

struct EcoAgent {
    int sp = 0, count = 1;
    Vector3 p{}, v{};                           // world x/y (metres), depth z
    Vector2 wander{1, 0};
    float hunger = 0.5f, fedT = 0, t = 0, chaseT = 0;
    float hurt = 0;                             // damage on its lead member (a shot, a spear)
    int target = -1;
    bool alive = true;
    float flash = 0;                            // a strike or a flee just happened (drawing)
    Vector3 goal{}; float goalT = 0;            // a hooked fish it is going for
};
struct Raft { Vector2 p; float r; };
struct EcoArrival { std::string species; float t; };

struct Eco {
    const GroundDef* g = nullptr;
    std::string ground;
    uint32_t initSeed = 0;                      // the seed the chart was built from (a network mirror rebuilds the same chart)
    int n = 0; float cell = 4;
    std::vector<float> depth;                   // the chart: metres of water at high tide (0 = land)
    std::vector<uint8_t> hab, holes;
    std::vector<std::vector<float>> suit;       // per species, per cell: where it lives
    std::vector<float> B, B0;                   // biomass (kg), and its start
    float R[R_COUNT] = {}, R0[R_COUNT] = {};
    float coral = 1;                            // reef health (1 = the start); algae over the coral smothers it
    float time = 0, clock = 0;                  // real seconds this night; minutes since 20:00
    int night = 0;
    float tide = 0;                             // metres below high water (the Lagoon's tide falls through the night)
    Grid blood, sound, vib;
    float bloodAmbient = 0;
    float wake = 0;
    std::vector<EcoAgent> agents;
    std::vector<Raft> rafts;
    std::vector<Vector2> landingAt;             // the landings' centres (the Lagoon: the Atoll), islets the skiff can beach on
    std::vector<int> landingKind;               // LandingKind per landing (the Weeds: Seal Rock, the Cannery Pier)
    // the skiff-only fishing marks (design doc v2, "Skiff destinations": about twice the bite rate, the ground's rarer
    // fish): the Lagoon's Crest Pass (a gap in the coral too shallow for the Gannet) and the Sargassum Line (a weed bank
    // that fouls her screw)
    struct SkiffMark { std::string name; Vector2 at; float r; int kind; };   // kind 0 shallow pass, 1 weed bank
    std::vector<SkiffMark> marks;
    int MarkAt(Vector2 p) const;
    void BuildWeedsChart(uint32_t seed);        // the Weeds' chart (BuildChart dispatches by ground)
    void BuildGrottoChart(uint32_t seed);       // the Grotto's: open water, a headland pierced by the sea arch, the cave
    float archY = 0, archX0 = 0, archX1 = 0, archHalf = 0; bool archOpen = true;   // the Grotto's sea arch (closed: no water under her keel)
    bool InArch(Vector2 p) const;
    void BuildAtlantisChart(uint32_t seed);     // Atlantis Waters: the terraces, the slope, the Trench's edge, the Pale Eye
    Vector2 eyeP{};                             // (Atlantis) where the Pale Eye lies, far below
    float wakeMul = 1, wakeDrift = 0;           // (Atlantis) the Wake's gain while someone looks at the Eye; a steady rise a minute (the Eye wide open)
    std::vector<Vector2> rockfalls;             // (the Grotto) stalactites brought down by loud noise, landing this tick (EcoTick)
    float rockfallMul = 1;                      // (the Rockfall variant: half the usual noise brings them down)                // the skiff mark a point lies in, or -1
    bool skiffOn = false; Vector2 skiffPos{};    // the skiff out on its own (60 m+ from her): the web keeps a second bubble of life round it
    bool birdDrawOn = false; Vector2 birdDraw{};  // set by EcoTick: cooking smoke or fish ashore, or a laden skiff away from her, draws the birds             // the landings' centres (the Lagoon: the Atoll), islets the skiff can beach on
    std::vector<EcoArrival> arrivals;
    std::vector<std::string> log;
    // what the boat is doing (set each step by the Gannet, or by the test patterns)
    struct Lamp { Vector3 p; float r, k; };
    std::vector<Lamp> lamps;
    bool boat = false; Vector2 boatPos{300, 300}; float screwNoise = 0; int deckFish = 0;
    Vector2 observer{300, 300};
    bool agentsOn = true;
    int agentBudget = 260;
    // tonight's variant, as the web feels it (set by the session at cast off; design doc, "Nightly variants")
    float forageMul = 1;                        // bait species about the boat (Bait run x3, Red tide x0.3)
    float threatHungerMul = 1;                  // how fast the threats grow bold (Bait run and Red tide 1.5)
    float seaMul = 1;                           // open-sea species inside the lagoon (King tide x2)
    float turtleMul = 1, sharkMul = 1;          // Turtle nesting: turtles x4, the sharks that follow them x2
    bool tideHeld = false;                      // King tide: the crest stays passable all night
    bool redTide = false;                       // dead forage floating: blood everywhere, fish sell at half
    std::map<std::string, float> speciesMul;    // tonight's variant on named species (the Weeds' tuna run, the kelp storm's yellowtail)
    float foulMul = 1;                          // how often the canopy and the drift mats foul her screw (the kelp storm: 2)
    float biteMul = 1;                          // every bite's chance (the Grotto's mould bloom: everything bites)
    void AddDriftMats(int n);                   // (the kelp storm) more drift kelp mats over the forest and the edge
    int extraRafts = 0;                         // how many of the rafts are the storm's (gone again the next night)
    float stirOverride = -1;                    // the shakedown's shark lesson: Stir() returns this when it is 0 or more
    uint32_t rng = 1;
    float popAcc = 0, fieldAcc = 0, spawnAcc = 0, agentAcc = 0, gullT = -1;
    std::vector<float> fed;                     // per species: its intake as a fraction of the start's (the population's hunger)

    bool Init(const std::string& groundKey, uint32_t seed);
    void Step(float dt);
    void StartNight();                          // 20:00 again (the populations carry over)
    void Day(float hours);                      // between nights: the populations alone
    // the chart
    bool InMap(Vector2 p) const { return p.x >= 0 && p.y >= 0 && p.x < n * cell && p.y < n * cell; }
    int CellIdx(Vector2 p) const;
    float DepthAt(Vector2 p) const;             // at the current tide
    int HabAt(Vector2 p) const;
    // the layers
    float LightAt(Vector3 p) const;
    void AddBlood(Vector3 p, float amount);
    void AddNoise(Vector3 p, float amount);
    void AddVibration(Vector3 p, float amount);
    void DepthCharge(Vector3 p, std::vector<std::pair<int, float>>* floated = nullptr);   // +15 Wake, a blast of noise; everything in 12 m stunned or killed, floating up (kg per species)
    // shooting and gear (stage 6)
    float SpeciesHP(int sp) const;              // a threat's HP from the doc's stat table; a fish's from its weight
    int HitAgent(Vector3 p, float r, bool air) const;   // the agent whose body a projectile at p touches, or -1
    bool DamageAgent(int idx, float dmg, bool head, Vector3 at);   // true: one of them is dead (off the population, bleeding)
    float Sweep(Vector3 mouth, Vector2 dir, float width, float speed, float dt, std::vector<std::pair<int, float>>& kgOut, float yield = 1);   // a trawl's mouth through the water (yield: the share of a swept school it keeps)
    float DensityAt(int sp, Vector2 p) const;   // individuals per 4 m cell (set gear fishes the population, not the agents)
    void Harvest(int sp, float kg, Vector3 at, bool bleed);   // the crew took it (landed, netted, shot)
    // populations
    float Pop(int sp) const { return B[sp] / Species().sp[sp].MeanKg(); }
    float PopFrac(int sp) const { return B0[sp] > 0 ? B[sp] / B0[sp] : 1; }
    float Hunger(int sp) const;                 // 0 fed .. 1 starving (0.5 at the start's balance)
    float Stir() const;                         // the night's threat pressure 0..1: the ground's baseline curve by the clock, plus the Wake the crew raised
    // fishing
    int TryBite(Vector3 lure, Tackle t, const std::string& bait, float dt, int* agentIdx);
    FishSpec SpecOf(int sp, float kgRoll) const;
    void TakeFromAgent(int agentIdx);
    void TakeNear(int sp, Vector3 at);          // the one that was hooked leaves its school
    int Depredate(Vector3 fishPos, float fishKg, float dt, int* kind);   // a thief takes a hooked fish; kind 1 head, 2 whole
    float Rand();
    int SpawnAgentPublic(int sp, Vector2 at) { return SpawnAgent(sp, at); }
private:
    void BuildChart(uint32_t seed);
    void Calibrate();
    void StepPop(float hours);
    void StepAgents(float dt);
    void Materialize();
    int SpawnAgent(int sp, Vector2 at);
};

std::string DefaultBait(Tackle t);
std::string TrawlDataPath();                   // where data/trawl is (beside the exe, or up from it)             // until the Chandler sells bait (stage 4)
int RunTrawlEco(int argc, char** argv);        // depth.exe --trawl-eco <ground> <minutes> [pattern]
int RunTrawlEcoTest();                         // depth.exe --trawl-eco-test

} // namespace tw
