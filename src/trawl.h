#pragma once
// ============================================================================
//  THE TRAWL - arcade game 2 of the Deep Arcade (The_Trawl_Reference/, "The Trawl - Arcade Game 2 Design Document").
//  A 1-6 player co-op fishing horror game: a crew works the steam trawler Gannet through three nights per deadline
//  to meet the Owners' quota, while a living food web under the hull decides what bites and what comes up the rail.
//
//  This header is the headless core (the host's simulation; nothing here draws): the sea, the boat, the crew and
//  its stations. trawl_art.cpp draws it as top-down pixel art; trawl.cpp is the scene and the self-tests.
//
//  Units: metres, kilograms, seconds. World x/z is the sea's plane (the ground is about 600 m across). The boat
//  frame has x toward the bow and y toward starboard; deck positions are in the boat frame.
// ============================================================================
#include "raylib.h"
#include <cmath>
#include <cstdint>
#include <string>
#include <cstdio>
#include <vector>

namespace tw {

// ---------------------------------------------------------------- the numbers (trawl_data.cpp)
struct TrawlData {
    // the Gannet: a 22 m steam trawler
    float length = 22, beam = 6, freeboard = 1.2f;       // metres; freeboard is the deck's height over still water, unladen
    float dryMass = 40000;                                // kg (hull, engine, gear)
    float waterplane = 96;                                // m^2: 1 m of sinkage takes waterplane * 1025 kg
    float gm = 0.5f;                                      // metacentric height (m): how stiff she is in roll
    float rollPeriod = 6, pitchPeriod = 4.5f, rollDamp = 0.12f, pitchDamp = 0.2f;
    float rollGain = 2.0f, pitchGain = 1.2f;              // how hard the wave slope throws her (a squall rolls her to about 20 deg)
    float maxThrust = 26000, dragFwd = 1100, dragSide = 9000, rudderYaw = 0.10f, yawDamp = 1.2f;
    // the engine (design doc, "The engine")
    float sackKg = 18, shovelKg = 1.2f;                   // a sack runs her about 3 minutes at half; one shovelful
    float burnBase = 0.02f, burnPerDraw = 0.057f;         // kg/s of coal the firebox eats, idling and per telegraph step
    float heatPerKg = 0.12f, pressureLoss = 0.004f, drawPerStep = 0.0105f;   // pressure per kg burnt; lost per second; drawn per second at full
    float greenLo = 0.45f, greenHi = 0.85f, redAt = 1.0f, redGrace = 5, valveStop = 20;
    float bleedRate = 0.25f;                              // pressure bled per second at the boiler (right mouse)
    int noiseByTelegraph[4] = {0, 2, 5, 9};               // stop, slow, half, full: into the water's sound layer
    float telegraphDraw[4] = {0, 0.35f, 0.65f, 1.0f};
    // the hull's six sections, leaks and pumps
    float sectionMax = 100, leakBelow = 40, floodBelow = 10;
    float leakRate = 60, floodRate = 140;                 // kg of water per second from a leaking / a flooding section
    float pumpKgPerStroke = 22, strokeTime = 0.45f;       // one hand on the bilge pump
    float patchTime = 6;                                  // a hand with a patch kit stops a leak (Bosun: 3 s)
    float overRailKgPerS = 900;                           // green water over the rail when she's on her beam
    // the crew on deck
    float walk = 3, crewMass = 80, braceRoll = 12, fallRoll = 25, beamEnds = 35;   // m/s, kg, degrees
    float slideAccel = 6, fallTime = 1.5f;
};
const TrawlData& D();

// ---------------------------------------------------------------- the sea
enum class Weather { Calm, Fog, Rain, Squall, Storm, Glass, COUNT };
const char* WeatherName(Weather w);
float WeatherSwell(Weather w);                            // metres of swell (design doc, "Weather")
struct Sea {
    Weather weather = Weather::Calm;
    float swell = 0.2f, t = 0;
    Vector2 wind{2, 0}, current{0.2f, 0.1f};              // m/s over the surface
    uint32_t seed = 1;
    // four directional waves (a cheap Gerstner set) scaled by the swell
    float Height(float x, float z) const;
    void Set(Weather w, uint32_t s);
};

// ---------------------------------------------------------------- the boat
enum Section { SEC_BOW_P, SEC_BOW_S, SEC_MID_P, SEC_MID_S, SEC_STERN_P, SEC_STERN_S, SEC_COUNT };
const char* SectionName(int s);
int SectionAt(Vector2 deck);                              // which section a deck point sits over
Vector2 SectionSpot(int s);                               // an open place to stand in a section (to patch it)
inline std::string KgText(float kg) { char b[32]; snprintf(b, sizeof b, kg < 1 ? "%.0f g" : "%.1f kg", kg < 1 ? kg * 1000 : kg); return b; }   // "80 g", "2.4 kg"
struct Load { Vector2 at{}; float kg = 0; };              // a weight aboard at a deck position (crew, fish, the hold, the net)
struct Boat {
    Vector2 pos{0, 0}, vel{0, 0};                         // world x/z, m/s
    float heading = 0, yawRate = 0;                       // radians; 0 is +x
    float roll = 0, rollVel = 0, pitch = 0, pitchVel = 0, heave = 0, heaveVel = 0;   // radians (+roll: starboard down), m
    float integrity[SEC_COUNT], integrityMax[SEC_COUNT];
    bool patched[SEC_COUNT];                              // a patch holds the leak until the section is hit again
    float bilge = 0;                                      // kg of water in her
    // the engine
    float bunker = 5 * 18, firebox = 4, pressure = 0.6f, redT = 0, valveT = 0, fireT = 0;
    int telegraph = 0;                                    // 0 stop, 1 slow, 2 half, 3 full; -1 slow astern
    int lantern = 2;                                      // 0 hooded (4 m), 1 low (8 m), 2 full (14 m), 3 searchlight (a 30 m cone)
    float searchAim = 0;                                  // the searchlight's bearing off the bow (radians)
    bool aground = false;
    float thrustMult = 1, noiseMult = 1;                  // the Slipway's compound engine: +30% speed, -25% screw noise
    float rudder = 0, shaft = 0;                          // -1..1 helm; 0..1 the screw
    bool sunk = false;
    float noise = 0;                                      // what the screw writes into the water this second
    std::vector<Load> loads;                              // everything aboard that isn't her own dry mass, set each step
    float extraHeelTorque = 0;                            // kg*m from lines and hands pulling at the rail (set each step)
    Vector2 extraForce{0, 0};                             // N on her from lines (a hooked marlin tows her), set each step
    Boat();
    float TotalMass() const;
    float Freeboard() const;                              // the deck's height over the water amidships, now
    float RollDeg() const { return roll * 57.2958f; }
    Vector2 Forward() const { return {cosf(heading), sinf(heading)}; }
    Vector2 ToWorld(Vector2 deck) const;                  // deck point -> sea x/z
    Vector2 ToDeck(Vector2 world) const;                  // and back
    float Speed() const;                                  // along the heading
    void Hit(int section, float dmg);                     // a ram, a bite, a rock
    void Step(float dt, const Sea& sea);
    void Shovel(float kg);                                // coal from the bunker into the firebox
    void Bleed(float dt);
    void Pump(float kg);
};

// ---------------------------------------------------------------- lines and fishing (trawl_fish.cpp)
// Design doc, "Fishing": tackle, line types and hooks; the bite (inspect, nibble, take); the fight (reel, rod angle,
// drag against a fish with strength, stamina and a fight pattern); landing. Positions in the fight are world x/y (the
// sea's plane, as Boat::pos) and z the depth below the surface (the rod tip sits at a negative z).
enum class Tackle { Handline, Light, Medium, Heavy, DeepDrop, Chair, COUNT };
struct TackleDef {
    const char* name;
    float strength;              // kgf the line is rated to
    float cast;                  // metres (the deep-drop drops straight down instead)
    float reel, reelLow;         // m/s of line gained; a 2-speed reel drops to reelLow against heavy tension
    float spool;                 // metres of line on the reel
    float rodSoft;               // metres the rod bends per kgf (a soft rod cushions the line)
    int price; bool dropDown;
};
const TackleDef& TackleOf(Tackle t);
enum class LineType { Mono, Braid, Wire, Glow, COUNT };
struct LineDef { const char* name; float stretch; float shock; bool biteProof; float wary; };   // strain at the rating, snap window (s), spook factor
const LineDef& LineOf(LineType l);
enum class Hook { Small, Circle, Treble, COUNT };
const char* HookName(Hook h);
enum class Pattern { None, Run, Dive, Jump, Circle, Cover, Roll, COUNT };
const char* PatternName(Pattern p);
float PatternPull(Pattern p);    // the doc's pull ratio

// What a hooked fish is (stage 3's species records fill these; stage 2 has a dummy table)
struct FishSpec {
    const char* name; float kg; Pattern a, b; bool wary, teeth;
    float depth, floor;          // where it takes, and the bottom under it
    float price;                 // shillings per kg
    float pullK = 1, staminaK = 1;   // a sluggish or tireless species against the pattern's ratio and the stamina rule
    float softMouth = 1;             // how easily a hook tears out
};
const std::vector<FishSpec>& DummyFish();
const FishSpec* FindDummyFish(const std::string& name);

enum class FightEnd { None, Landed, Snapped, ThrownHook, PulledHook, SlackHook, Spooled, Spooked, Taken };
const char* FightEndName(FightEnd e);

// A line in the water and whatever is on it. One per rod.
struct Fight {
    // the gear
    Tackle tackle = Tackle::Light; LineType line = LineType::Mono; Hook hook = Hook::Small;
    // the line (Verlet nodes for its drawn shape; the tension comes from the stretch between tip and fish)
    Vector3 tip{0, 0, -2};       // the rod tip
    float L = 0, drag = 3, tension = 0, overT = 0, slackT = 0, chafe = 0; int wraps = 0;
    float slipT = 0, stillT = 0, stillT0 = 0;
    std::vector<Vector3> node, prev;
    bool reeling = false, pumping = false;
    float rodLean = 0;           // -1..1: the rod swung left/right of the line (side pressure)
    bool bowed = false;          // the rod lowered (on a jump)
    Vector2 outboard{1, 0};      // away from the hull at this rod: a fish crossing under her passes the keel
    float keelClear = 0;         // chance the angler walks the line clear of the keel on a pass (bots; players by rod lean)
    float botBowAt = -1;         // (the bot's plan for the current jump)
    // the fish
    FishSpec spec{}; bool on = false;
    Vector3 p{}, h{1, 0, 0};     // position and the way it is swimming
    float S = 0, S0 = 0, P0 = 0, effort = 0.5f, speedK = 2;
    Pattern cur = Pattern::None;
    float modeT = 0, nextBurst = 0, burstT = 0, turn = 0;
    float jumpT = -1, nextJump = 0, bowT = -1;   // a jump in the air; the angler's bow timing
    float rollNext = 0, keelSide = 0, circleDir = 1, bottomRough = 0;
    Vector3 cover{}; bool hasCover = false, inCover = false; float coverPullT = 0;
    bool alongside = false, gaffPending = false;
    bool lightHook = false;      // lip-hooked, barely: head-shakes shed it
    float t = 0;                 // seconds on
    FightEnd end = FightEnd::None;
    uint32_t rng = 1;
    float Rand();
    float Strength() const;      // the rating, less chafe and wraps
    float Pull() const;          // what the fish pulls with now (kgf)
    void HookFish(const FishSpec& f, Vector3 at, uint32_t seed);
    void Step(float dt);         // physics, the fish's mind, stamina, snaps
    bool Land(float skill);      // gaff/lift/tail rope at alongside; false on a miss (the fish runs again)
    Vector2 PullOnBoat() const;  // kgf on the rod tip, horizontal, world frame
};

// The bite: inspect (the tip ticks), nibbles (1-4 taps), take (the rod loads; set the hook in the window)
enum class BiteStage { None, Inspect, Nibble, Take, Gone };
struct Bite {
    BiteStage stage = BiteStage::None; float t = 0, window = 0.25f; int nibbles = 0;
    const FishSpec* fish = nullptr; uint32_t rng = 7;
    void Start(const FishSpec* f, bool wary, bool angler, uint32_t seed);
    // advances; returns 1 hooked, -1 lost (spooked by a strike on a nibble, or missed), 0 still biting
    int Step(float dt, bool strike, bool reelingCircle);
    float Tick() const;          // how hard the rod tip moves right now (drawing and sound)
};

// The sensible bot angler (design doc "Bot skill": Green, Able, Old Hand)
enum class Skill { Green, Able, OldHand, COUNT };
struct SkillDef { const char* name; float reaction, hookSet, gaff, bow, keel; };
const SkillDef& SkillOf(Skill s);
void BotFight(Fight& f, Skill s, float dt, uint32_t& rng);    // sets drag, reel, lean, bow, pump from what it sees

int RunTrawlFight(int argc, char** argv);   // depth.exe --trawl-fight <species|all> [tackle] [N]

// ---------------------------------------------------------------- stations and the crew
enum class StationKind { Helm, Boiler, Pumps, PortRod, StarRod, SternRodP, SternRodS, NetWinch, Lantern, Sonar, Harpoon, Gutting, AirPump, Bell, Printer, Locker, COUNT };
struct StationDef { StationKind kind; const char* name; Vector2 at; int deck; const char* does; };   // deck 0 main deck, 1 engine room
const std::vector<StationDef>& Stations();
float LanternRadius(int level);                           // 4, 8, 14, 30 m
int NearestStation(Vector2 at, int deck, float r);

// ---------------------------------------------------------------- hands' gear (trawl_gear.cpp)
// Four slots a hand (design doc, "Inventory"); the rest lives in the deck locker.
enum class Item { None, Gaff, Priest, Knife, Speargun, Flare, Rifle, Shotgun, Charge, Ring, Bandage, Longline, Pot, COUNT };
struct ItemDef { const char* name; int price; int ammoPer; int ammoPrice; float noise; const char* use; };
const ItemDef& ItemOf(Item i);
struct Slot { Item it = Item::None; int ammo = 0; };
enum Injury { INJ_HOOKED_HAND = 1, INJ_BROKEN_ARM = 2, INJ_BURN = 4, INJ_BITE = 8 };
const char* InjuryName(int bit);

enum class Role { Bosun, Angler, Diver, Medic, COUNT };
const char* RoleName(Role r);
struct Crew {
    int slot = 0; bool bot = false; Role role = Role::Bosun;
    Vector2 p{0, 0}, v{0, 0};                             // deck position and velocity (boat frame)
    int deck = 0;                                         // 0 main deck, 1 engine room
    int station = -1;                                     // manned station index, -1 none
    bool braced = false, fallen = false, overboard = false;
    float fallT = 0, strokeT = 0, patchT = 0; int patchSec = -1;
    int patchKits = 0;
    float carryKg = 0;
    Vector2 facing{1, 0};
    // the hand's slots, injuries, and life (design doc, "Death, injury, and ghosts")
    Slot slots[4]; int sel = 0;
    float cool = 0, reloadT = 0;
    int injuries = 0, serious = 0;                        // bits of Injury; serious injuries this night (a second is death)
    bool dead = false, bodyLost = false;                  // dead for the night: a ghost on deck, or lost to the sea
    Vector2 swim{0, 0};                                   // overboard: where in the sea (world x/y)
    float drownT = 0, bleedT = 0;
    std::string cause;                                    // what killed them
    bool Has(int inj) const { return (injuries & inj) != 0; }
};

// A rod at one of the four rod stations: the cast, the lure in the water, the bite and the fight (trawl_fish.cpp).
enum class RodState { Idle, Charging, Out, Fighting };
struct Rod {
    int station = -1;
    Tackle tackle = Tackle::Light; LineType line = LineType::Mono; Hook hook = Hook::Small;
    RodState state = RodState::Idle;
    float charge = 0;                                     // 0..1 while the cast is held
    Vector2 aim{0, 1};                                    // deck-frame direction the cast goes
    Vector3 lure{};                                       // world x/y, depth z
    float lureDepth = 8;                                  // where it settles (the line counter)
    float settleT = 0, biteClock = 0;
    Bite bite; Fight fight;
    float lastTick = 0;
    float lineOut = 0;                                    // metres off the reel while the lure is out
    bool castHeld = false, reel = false, strikeQ = false, bow = false, gaffQ = false;   // this step's hands on it
    float lean = 0;
    bool botAngler = false; float alongT = 0;             // a bot fights it (tests; bot crew later)
    std::string lastCatch;
    std::string bait;                                     // what's on the hook (stage 4's Chandler sells the rest)
    FishSpec biteSpec{}; int fishSp = -1; bool headOnly = false;   // the web's fish on the line
    uint32_t rng = 1;
    Vector2 TipDeck() const;                              // the rod tip, in the boat frame, over the rail
};
// A fish landed aboard (design doc, "Economy": value = base price x weight x grade x freshness x glut)
struct CatchRec {
    std::string name; float kg = 0; float price = 0;       // shillings per kg at the market, before the rest
    int sp = -1;                                          // the ground's species index (-1: a stand-in fish)
    float grade = 1;                                      // how it was taken: hook 100%, less 10% a bite
    float fresh = 1;                                      // 1% a real minute on deck, 0.2% gutted and iced
    bool gutted = false, iced = false;
    bool first = false;                                   // the run's first of its kind: the Owners pay 50% more
    bool bycatch = false, protectedSp = false; float aboardT = 0;   // worthless or protected: back over the side (a turtle within 60 s)
};

// Things in the water that belong to the crew: projectiles, shot fish afloat, the trawl, set gear, the life ring.
enum class Shot { Bullet, Pellet, Spear, Harpoon, Explosive, Flare, Charge };
struct Projectile {
    Shot kind; Vector3 p, v;                              // world x/y; z below the surface (negative is in the air)
    int owner = -1; float life = 3, dmg = 0; bool tether = false, inWater = false; float travel = 0;
};
struct Floater { std::string name; int sp = -1; float kg = 0, price = 0, grade = 1; Vector2 p; float life = 90; bool tethered = false; };
struct FlareLight { Vector2 p; float t; };
enum class NetState { Stowed, Shooting, Down, Snagged, Hauling, Lost };
struct Trawl {
    NetState state = NetState::Stowed;
    float t = 0;                                          // shooting / hauling progress (s)
    float load = 0, depth = 0, backT = 0;                 // kg in the cod end; how deep the mouth is running; backing off a snag
    std::vector<std::pair<int, float>> catchKg;           // species -> kg in the net
    Vector3 node[8 * 6], prev[8 * 6];                     // the mesh (world x/y, depth)
    bool meshInit = false;
    float Width(bool bigger) const { return bigger ? 18.0f : 12.0f; }
};
struct SetHook { int sp = -1; bool head = false; float kg = 0; };
struct Longline { Vector2 a, b; std::vector<SetHook> hooks; float age = 0; };
struct Pot { Vector2 p; float age = 0; std::vector<std::pair<int, float>> catchKg; int n = 0; };
struct LifeRing { int state = 0; Vector2 p{}, v{}; int thrower = -1, holder = -1; float haulT = 0; };   // 0 aboard, 1 flying, 2 in the water

struct Eco;                                               // the food web (trawl_eco.h)

// The boat and her crew as one step (the host's 60 Hz tick): the hands' weights into the boat, the boat's roll into
// the hands.
struct Gannet {
    Sea sea;
    Boat boat;
    std::vector<Crew> crew;
    float time = 0;
    std::vector<std::string> log;                         // what just happened (a sunk section, a blown valve, a fall)
    std::vector<Rod> rods;                                // one per rod station
    std::vector<CatchRec> hold;                           // landed fish (stage 4 grades, ices and sells them)
    Eco* eco = nullptr;                                   // the ground's food web, when she's on one: bites, thieves, light and noise into the water
    // stores and gear (stage 4: bought at the Chandler, kept for the run)
    bool moored = false; Vector2 moorPos{0, 0}; float moorHeading = 0;   // alongside the quay: the crew can walk ashore
    float ice = 20, iceCap = 600;                         // kg of ice aboard; the hold's capacity for iced fish
    int baitShrimp = 10, baitSquid = 0, chum = 0;         // baits (a tin is ten), chum buckets
    bool owned[(int)Tackle::COUNT] = {true, true, false, false, false, false};   // two handlines and a light rod to start
    bool watch = false, searchlight = false, secondPump = false;
    float gutT = 0;
    // shooting, the net, set gear, the locker (trawl_gear.cpp)
    std::vector<Projectile> shots;
    std::vector<Floater> floaters;
    std::vector<FlareLight> flares;
    Trawl net; bool biggerNet = false;
    std::vector<Longline> longlines;
    std::vector<Pot> pots;
    std::vector<LifeRing> rings;
    std::vector<Slot> locker;
    bool harpoonCannon = false; int harpoons = 0, explosives = 0; bool explosiveLoaded = false; float harpoonReload = 0;
    Rod harpoon;                                          // the cannon's tethered fight (a Fight on 120 kg steel), when one is on
    int harpoonSp = -1;
    float gullT = 0, fines = 0;                           // fines: the Owners take these at the dock
    int chargesUsed = 0;
    // bot crew (trawl_bots.cpp; design doc "Bot crew"): every hand after the first is a bot when botsOn
    bool botsOn = false; Skill botSkill = Skill::Able;
    struct Brain {
        int order = -1;                                   // a station the skipper ordered it to (-1: its watch)
        int goal = -1, goalDeck = 0; float think = 0;     // where it is going, and when it next reconsiders
        float castT = 0; bool struck = false; RodState lastRod = RodState::Idle;
        Vector2 lastP{}; float stuckT = 0, sideT = 0; int side = 1;
        std::string bark; float barkT = 0;                // a short line over its head ("Fish on, port!")
        int task = 0;                                     // 0 a station, 1 the life ring for a hand overboard, 2 a leak, 3 following the skipper
        int target = -1; float taskT = 0;                 // who or what the task is for; how long it has been at it
        int follow = -1;                                  // ordered to follow this hand (F), -1 not
        uint32_t rng = 1;
    };
    std::vector<Brain> brains;
    void StepBots(float dt);
    int OrderBot(int station);                            // the nearest bot takes it (-1: every bot back to its watch); returns the bot
    int OrderFollow(int leader);                          // the nearest free bot follows that hand about (again: stops); returns the bot
    std::string BotDoing(int c) const;                    // what it's about, for the crew list
    int DeckFish() const;                                 // landed and not yet gutted (they draw gulls, they spoil)
    int RodAt(int station) const;                         // index into rods, or -1
    void StepRods(float dt);                              // lures, bites (a dummy bite table until the web comes), fights, the pull on her
    // a hand at a rod: the cast, the reel, the strike, the rod's lean, the bow, the gaff
    void RodInput(int c, bool castHeld, Vector2 aimDeck, bool reel, bool strike, float lean, bool bow, bool gaff, float dragScroll);
    void CycleTackle(int c);
    void GutsOverboard(float kg);
    // gear (trawl_gear.cpp)
    void GiveStartingKit();
    bool AddItem(Item it, int ammo);                      // into the first free slot of hand 0, else the locker
    void UseItem(int c, Vector2 aimDeck, bool pressed, bool held, bool sight, float dt);   // left mouse off-station
    void Reload(int c);
    void StepGear(float dt);
    void HitShot(Projectile& p, int agent);               // a projectile in a fish or a gull
    void NetInput(int c, bool held, bool cut, float dt);
    void HarpoonInput(int c, Vector2 aimDeck, bool fire, bool held, bool release, float dt);
    bool GaffFloater(int c);                              // E at the rail beside a shot fish afloat
    bool HaulSetGear(int c);                              // E at the rail beside a longline buoy or a pot float
    void Injure(int c, int injury, const std::string& cause);
    void Kill(int c, const std::string& cause, bool bodyLost);
    void GoOverboard(int c, const std::string& why);
    bool AllDead() const;
    Vector2 RailWorld(int c) const;                         // blood into the water by the gutting table's rail
    void Init(int crewCount, uint32_t seed, Weather w = Weather::Calm);
    void Step(float dt);
    // a hand's controls (the scene feeds its human; bots will call these too)
    void Move(int c, Vector2 wish, bool brace, float dt);
    bool TakeStation(int c);                              // E near a station
    void LeaveStation(int c);                             // X
    bool StartPatch(int c);                               // E in a leaking section, with a patch kit (or the ship's)
    int PatchKits() const;
    void Primary(int c, bool held, float dt);             // left mouse at a station (shovel, pump, ...)
    void Secondary(int c, bool held, float dt);           // right mouse (bleed at the boiler)
    void Scroll(int c, float amount);                     // telegraph, lantern level
    void Steer(int c, float amount, float dt);            // A/D at the helm
    void Say(const std::string& s);
};

void EcoTick(Eco& e, Gannet& g, float dt);                // what the Gannet puts into the water, then the web's step (trawl_eco.cpp)

bool QuayWalkable(Vector2 p);                             // the quay beside her port side (boat frame) when she's moored

int RunTrawlGearTest();                                   // depth.exe --trawl-gear-test
int RunTrawlBotTest();                                    // depth.exe --trawl-bot-test
int RunTrawlBoatTest();                                   // depth.exe --trawl-boat-test
int RunTrawlRodTest();                                    // (part of --trawl-boat-test) a rod on the Gannet, cast to landing

} // namespace tw
