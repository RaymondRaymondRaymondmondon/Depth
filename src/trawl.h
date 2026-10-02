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
struct Wreck;   // (trawl_wreck.h)

// ---------------------------------------------------------------- the numbers (trawl_data.cpp)
struct TrawlData {
    // the Gannet: a 22 m steam trawler
    float length = 22, beam = 6, freeboard = 1.2f;       // metres; freeboard is the deck's height over still water, unladen
    float dryMass = 40000;                                // kg (hull, engine, gear)
    float waterplane = 96;                                // m^2: 1 m of sinkage takes waterplane * 1025 kg
    float gm = 0.5f;                                      // metacentric height (m): how stiff she is in roll
    float rollPeriod = 6, pitchPeriod = 4.5f, rollDamp = 0.35f, pitchDamp = 0.2f;
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
    // threats on the boat (design doc, "Threat stats"; the Lagoon's lethal paths, 2026-09-30)
    float ramDamage = 15, ramEvery = 20;                  // a reef shark's ram takes this off a section (the Great White about 25), at most every ramEvery s
    float ramHeel = 0.35f;                                // rad/s of roll the blow puts into her (enough to slide an unbraced hand)
    float netYield = 0.2f;                                // the share of a swept school the mouth really takes (balance: the Lagoon's quota should be met about 85% of the time by six hands)
    float railDrag = 0.5f;                                // per second at the worst: a running fish past 45% of the line's rating while she rolls past braceRoll toward it
    float junkPerCast = 0.10f;                            // a cast reeled in on the Lagoon brings up junk (design doc v2: Weeds 12%, Grotto 18%, Atlantis 15%)
    float chumBlood = 40, chumSeconds = 60;              // a chum bucket: 40 blood over 60 s at the rail (design doc, "The Chandler")
    // the skiff (design doc v2, "The skiff": the boat's table)
    float skiffRowOne = 1.5f, skiffRowTwo = 2.2f;         // m/s on the oars, one rower and two
    float skiffIntegrity = 40, skiffCapsize = 25, skiffLoad = 150, skiffLantern = 6;   // one section; degrees of roll; kg of catch; metres of light
    float skiffLower = 8, skiffRecover = 10;              // s at the davit (recovery: alongside the stern, the Gannet stopped)
    float skiffStroke = 0.55f, skiffCrab = 0.32f;         // a good stroke's rhythm; a stroke sooner than this after the last "catches a crab"
    float skiffRight = 4;                                 // s for a swimmer to right a capsized skiff (my call: the doc doesn't say)
};
const TrawlData& D();

// ---------------------------------------------------------------- the sea
enum class Weather { Calm, Fog, Rain, Squall, Storm, Glass, COUNT };
const char* WeatherName(Weather w);
float WeatherSwell(Weather w);                            // metres of swell (design doc, "Weather")
float WeatherRoll(Weather w);                             // degrees: the most the waves alone roll her (Squall 20, Storm 30)
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
    float graceMul = 1, speedMul = 1;     // the crew's upgrades, set each step: Stoker (2x the overpressure grace), Full Steam (+20%)
    int telegraph = 0;                                    // 0 stop, 1 slow, 2 half, 3 full; -1 slow astern
    int lantern = 2;                                      // 0 hooded (4 m), 1 low (8 m), 2 full (14 m), 3 searchlight (a 30 m cone)
    float searchAim = 0;                                  // the searchlight's bearing off the bow (radians)
    bool aground = false;
    float thrustMult = 1, noiseMult = 1;                  // the Slipway's compound engine: +30% speed, -25% screw noise
    float rudder = 0, shaft = 0;                          // -1..1 helm; 0..1 the screw
    bool sunk = false;
    float noise = 0;                                      // what the screw writes into the water this second
    float greenWater = 0;                                 // kg shipped over the rail so far (diagnostics)
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
    float noSnapUntil = 0;       // (the old hooks charm: the line never breaks on the first run)
    uint32_t holderUps = 0;      // the angler's role upgrades (Light Touch, Heavy Hand, Strong Line, Reader)
    FightEnd end = FightEnd::None;
    uint32_t rng = 1;
    float Rand();
    float Strength() const;      // the rating, less chafe and wraps
    float Pull() const;          // what the fish pulls with now (kgf)
    void HookFish(const FishSpec& f, Vector3 at, uint32_t seed);
    void Step(float dt);         // physics, the fish's mind, stamina, snaps
    void PredictReel(float dt);  // a guest's mirror: its own reeling, ahead of the host (the line shortens, the tension follows)
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
enum class StationKind { Helm, Boiler, Pumps, PortRod, StarRod, SternRodP, SternRodS, NetWinch, Lantern, Sonar, Harpoon, Gutting, AirPump, Bell, Printer, Locker, Davit, Magazine, Cot, COUNT };
struct StationDef { StationKind kind; const char* name; Vector2 at; int deck; const char* does; };   // deck 0 main deck, 1 engine room
const std::vector<StationDef>& Stations();
float LanternRadius(int level);                           // 4, 8, 14, 30 m
int NearestStation(Vector2 at, int deck, float r);

// ---------------------------------------------------------------- hands' gear (trawl_gear.cpp)
// Four slots a hand (design doc, "Inventory"); the rest lives in the deck locker.
enum class Item { None, Gaff, Priest, Knife, Speargun, Flare, Rifle, Shotgun, Charge, Ring, Bandage, Longline, Pot, Weapon, COUNT };
struct ItemDef { const char* name; int price; int ammoPer; int ammoPrice; float noise; const char* use; };
const ItemDef& ItemOf(Item i);
// a hand's slot. Item::Weapon is a row of the Gunsmith's catalogue (trawl_weapons.h): its damage upgrades, up to three
// attachments, the loaded magazine (ammo) and the one spare reload a hand carries (spare); the rest of the ammunition
// is the ship's (Gannet::ammo), restocked at the locker
struct Slot { Item it = Item::None; int ammo = 0; int wpn = -1, lvl = 0, spare = 0; int8_t att[3] = {-1, -1, -1}; };
const char* SlotName(const Slot& s);                      // the item's name, or the catalogue weapon's
Item DrawItemOf(const Slot& s);                           // what the screens draw in hand: a catalogue weapon as its nearest kind (blade, gaff, rifle, shotgun, speargun, flare pistol)
enum Injury { INJ_HOOKED_HAND = 1, INJ_BROKEN_ARM = 2, INJ_BURN = 4, INJ_BITE = 8 };
// Role upgrades (design doc, "The crew of six": a role ranks up after the 1st, 2nd and 4th met deadlines; at each rank
// the hand picks one of two on the dock chalkboard). In rank order per role: rank 2 pair, rank 3 pair, rank 4 pair.
enum RoleUp {
    UP_SHIPWRIGHT, UP_STOKER, UP_OLDSALT, UP_DECKBOSS, UP_IRONHULL, UP_FULLSTEAM,               // Bosun
    UP_LIGHTTOUCH, UP_HEAVYHAND, UP_READER, UP_STRONGLINE, UP_TROPHY, UP_BAITMASTER,           // Angler
    UP_DEEPLUNGS, UP_GLINTEYE, UP_PRESSURE, UP_WRECKRAT, UP_STRONGBACK, UP_OLDHAND,            // Diver
    UP_FIELDSURGEON, UP_BLANKETS, UP_SECONDWIND, UP_STEADY, UP_MIRACLE, UP_GHOSTSPEAKER,       // Medic
    UP_COUNT };
const char* RoleUpName(int u);
const char* RoleUpNote(int u);
int RoleUpOf(int role, int rank, int choice);   // the upgrade for (Role, rank 2..4, choice 0/1)
const char* InjuryName(int bit);

struct CatchRec {
    std::string name; float kg = 0; float price = 0;       // shillings per kg at the market, before the rest
    int sp = -1;                                          // the ground's species index (-1: a stand-in fish)
    float grade = 1;                                      // how it was taken: hook 100%, less 10% a bite
    float fresh = 1;                                      // 1% a real minute on deck, 0.2% gutted and iced
    bool gutted = false, iced = false;
    bool first = false;                                   // the run's first of its kind: the Owners pay 50% more
    bool trophy = false;                                  // landed by a Trophy Hunter: a first catch pays 50% more again
    bool bycatch = false, protectedSp = false; float aboardT = 0;   // worthless or protected: back over the side (a turtle within 60 s)
    int src = 0;                                          // how it came aboard: CatchSource (the sim's money by source)
    // on the deck (the playtest, 2026-10-01): a landed fish lies where it came aboard and flops for the rail until
    // it is clubbed (the priest, a gaff, a knife), shot, or gutted; one that reaches the rail goes back over the side
    bool dead = false; float flopT = 0; Vector2 deckAt{-8, 0};
    // the kill (design doc v2, "The kill"): anything of 1 kg or more comes aboard alive with hit points (8 + 6 x kg^0.75),
    // fights by its deck behaviour, and the finishing blow sets the Killscore (bonuses multiplied, up to 4x)
    float hp = -1, hpMax = 0;                             // (-1: not yet set up; StepDeckFish does it the moment it's aboard)
    float heading = 0;                                    // which way it lies on the deck (its head is forward of deckAt)
    int deckKind = 0;                                     // DeckBehaviour
    float actT = 0, airT = 0;                             // its next act; in the air on a flop (Airborne)
    float killScore = 1; std::string killHow; float killT = -1;   // the finishing blow's multiplier and why; seconds since (the popup)
    int grabbed = -1;                                     // (a Grabber) the hand it has hold of
    bool crated = false;                                  // in one of the six lidded catch crates on the aft deck: safe from birds (still to be gutted)
    bool junk = false;                                    // junk from the sea (design doc v2, "Junk from the sea"): price is its flat value; stowed, never quota
    // cooked ashore (design doc v2, "Cooking"): 1.0x to 1.5x over 10 s + 1 s a kg, held 5 s, then burning to 0.3x over 5 s;
    // a cooked fish no longer spoils
    float cookT = -1, cook = 1; bool cooked = false;
    bool glimmer = false;                                 // a rare shimmering variant: 3x its value
    int boss = -1;                                        // a mini-boss (MiniBosses index): price is its flat value spread over its weight
    bool cursed = false;                                  // (salvage) an Atlantean idol: aboard, it raises the Wake by 10 and the Eye blinks
};
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
    float z = 0, vz = 0;                                  // a jump (the playtest, 2026-10-01): height over the deck and the climb; a careless leap clears the rail
    float inkT = 0;                                       // blinded by a landed octopus's ink (seconds left)
    float oarT = 9, rightT = 0;                           // since this hand's last stroke at the oars; righting a capsized skiff
    bool skiffLine = false;                               // in the skiff: working her line instead of the oars (T)
    int charm = 0;                                        // the charm on a cord round this hand's neck (Charm); lost with a body lost at sea
    bool carrying = false; CatchRec carry;                // ashore: one thing in the arms (a fish, a chest, a crab)
    std::string skin, costume;                            // the player's Wardrobe (skins.h ids, empty as issued): sent by CMD_WARDROBE, drawn for all
    int workOn = -1; float workT = 0;                     // ashore: digging a cache (its index) or relighting the fire (100)
    float tangleT = 0;                                    // (the Weeds) seconds a Kelp Wraith has had this hand by the ankle at the rail (0: free)
    float heldT = 0;                                      // (the Grotto) lured by an Angler's light or in a Drowned sailor's grip: moved by it, not by their own feet
    // the hand's slots, injuries, and life (design doc, "Death, injury, and ghosts")
    Slot slots[4]; int sel = 0;
    float cool = 0, reloadT = 0;
    int injuries = 0, serious = 0;                        // bits of Injury; serious injuries this night (a second is death)
    bool dead = false, bodyLost = false;                  // dead for the night: a ghost on deck, or lost to the sea
    Vector2 swim{0, 0};                                   // overboard: where in the sea (world x/y)
    float drownT = 0, bleedT = 0;
    float cprT = 0;   // drowned with a Medic aboard: seconds left to haul them in for CPR (design doc: 15 s)
    uint32_t ups = 0; // role upgrades held (bit = RoleUp), set by the session from the hand's choices
    bool Up(int u) const { return (ups >> u) & 1u; }
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
    bool readWarn = false;       // (Reader) the fish is about to run
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

float CookMultiplier(float kg, float t);                  // the curve above, t seconds on the fire

// Mini-bosses, charms (design doc v2, "Mini-bosses, boss lures, and harbour requests", "Charms"; trawl_quest.cpp)
struct MiniBossDef { const char* name; float kg, value; int deck; Pattern a, b; const char* drop; float lure; const char* mark; };   // mark: its boss water (a skiff mark's name)
const std::vector<MiniBossDef>& MiniBosses();            // the Lagoon's two: Old Snapjaw, the Crest Grouper (boss water: the Crest Pass)
int MiniBossOf(const std::string& name);                  // index, or -1
enum Charm { CH_NONE, CH_LUCKY_COIN, CH_SHARK_TOOTH, CH_ANKLET, CH_OLD_HOOKS, CH_BRASS_LURE, CH_KELP_CROWN, CH_GOLDEN_SCALE, CH_WHITE_SKULL, CH_GLASS_LANTERN, CH_INSCRIBED_BILL, CH_BEAK, CH_COUNT };
const char* CharmName(int c);
const char* CharmEffect(int c);
int CharmOfDrop(const std::string& drop);
struct Gannet;
void BossCast(Gannet& g);                                 // the skiff's line cast with a boss lure armed                 // a mini-boss drop worn as a charm (CH_NONE if it isn't one)
const float GLIMMER_CHANCE = 0.02f;                       // a landed fish is a Glimmer variant (worth 3x; the collector wants them); a lucky coin doubles it

// A landing (design doc v2, "Islands: fires, traders, and treasure"; trawl_landing.cpp): a small island reached by
// skiff and walked on foot (DECK_SHORE: a hand's p is in the landing's frame, metres from its centre, world-aligned).
// The Atoll: a palm islet with a fire pit (open: rain puts it out), the tribe's elder (fish only, at 150%; closed
// to crews who fought the canoes), a beached smuggler sloop with a locked strongbox, one more cache (a chest under
// the palms, or one buried where a bottle's map or three chart pieces mark it), crabs on the beach and a moray in
// its little lagoon.
struct Cache { Vector2 p{}; int kind = 0; float value = 0, kg = 0; bool open = false, found = true; std::string what; };   // kind 0 plain, 1 locked, 2 buried
// kind: 0 the Atoll (the Lagoon), 1 Seal Rock and 2 the Cannery Pier (the Weeds). The same few parts serve each:
// `fire` is the Atoll's fire pit, the sealers' hut stove or the cannery boiler (both sheltered: rain doesn't reach them);
// `elder` is the elder, Old Hoskins or the last foreman; `sloop` is the beached sloop, the sealers' hut or the cannery
// shed; `pond` is the Atoll's lagoon or Seal Rock's haul-out (the bull seal stands in for the moray); the Cannery Pier
// is a stage on pilings with no pond, and Kelp Wraiths in the pilings take a hand at its edge.
enum LandingKind { LK_ATOLL, LK_SEALROCK, LK_CANNERY, LK_SHELF, LK_BONEBEACH, LK_STAIR, LK_TOWER, LK_CULT, LK_LIGHTHOUSE, LK_SANDBAR };   // (8, 9: the Lagoon's Old Lighthouse rock and the Sandbar)   // (3, 4: the Grotto's Smugglers' Shelf and Bone Beach; 5-7: Atlantis's Drowned Stair, Watchtower stump, Cult Landing)
struct Landing {
    int kind = LK_ATOLL;
    std::string name; Vector2 at{}; float r = 13;         // world centre; the shore's radius
    Vector2 pond{2.5f, 3.0f}; float pondR = 3.2f;          // the little lagoon (wading: half speed; the moray)
    Vector2 fire{-3.0f, 1.5f}; bool fireLit = true;
    Vector2 elder{4.8f, -5.0f};
    Vector2 sloop{-7.2f, -5.0f}; float sloopHead = 0.6f;   // the beached smuggler sloop (a 5 x 2 m wreck)
    std::vector<Vector2> palms;
    std::vector<Cache> caches;
    std::vector<CatchRec> onFire, onBeach;                // deckAt: where it lies (landing frame)
    std::vector<Vector2> crabs;
    Vector2 moray{}; float morayT = 0;                    // where it lurks in the pond; its next bite
    float elderCredit = 0;                                // fish given to the elder, at 150% of their value, to spend on his goods
    bool flooded = false;                                 // the Sandbar: under water from 02:00 (what was left on it is gone)
    Vector2 ToWorld(Vector2 l) const { return {at.x + l.x, at.y + l.y}; }
};
// The junk table (design doc v2, pages 25-27): what a cast or a net haul brings up besides fish
enum JunkUse { JU_SELL, JU_MAP, JU_KEY, JU_CHART, JU_TRAP, JU_BOOT };
struct JunkDef { const char* name; float lo, hi, kg; int use; float weight; unsigned grounds; };   // grounds: bit 0 Lagoon, 1 Weeds, 2 Grotto, 3 Atlantis
const std::vector<JunkDef>& JunkTable();
// What a landed fish does on the deck (design doc v2, "Deck behaviours")
enum DeckBehaviour { DB_FLOPPER, DB_THRASHER, DB_BITER, DB_SPEARER, DB_GRABBER, DB_PINCHER, DB_STINGER, DB_COUNT };
const char* DeckBehaviourName(int b);
int DeckBehaviourOf(const std::string& name, float kg);
float DeckFishHP(float kg);                               // 8 + 6 x kg^0.75: a 4 kg snapper 25, a 40 kg yellowfin 103
const float KILLSCORE_MAX = 4;
enum BirdKind { BIRD_GULL, BIRD_PELICAN, BIRD_FRIGATE, BIRD_COUNT };
struct BirdDef { const char* name; float lifts, value; };
const BirdDef& BirdOf(int kind);
int BirdKindOf(const std::string& species);   // "gull flock" / "brown pelican" / "frigatebird", or -1
enum KillHow { KH_MELEE, KH_BULLET, KH_PELLET, KH_SPEAR, KH_EXPLOSIVE };
enum CatchSource { CS_HOOK, CS_NET, CS_GUN, CS_SET, CS_DIVE, CS_COUNT };
const char* CatchSourceName(int s);

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
    float kelpKg = 0;                                 // (the Weeds) kelp in the cod end: weight, no value
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

// The wheelhouse sonar (design doc, "The sonar"; trawl_sonar.cpp): the crew's only view below the surface outside the
// lantern. Returns live 6 s after a ping (passive returns 2 s); a mark shows every hand a bearing arrow for 10 s.
enum class SonarKind { School, Fish, Threat, Gear, Wreck };   // (a Wreck return's count is its index in the ground's wrecks)
struct SonarReturn { Vector3 p{}; int sp = -1; SonarKind kind = SonarKind::Fish; float size = 1, t = 0; int count = 1; bool passive = false; };
struct SonarMark { Vector2 p{}; float t = 0; std::string what; int by = -1; };
struct SonarState {
    float cool = 0;                                       // until the next ping is ready (3 s)
    float sinceP = 99;                                    // since the last ping (the sweep, the seabed's fade)
    int band = 0;                                         // the depth dial: 0 all, 1 surface (0-5 m), 2 middle (5-20), 3 deep (20+)
    std::vector<SonarReturn> ret;
    std::vector<SonarMark> marks;
    float passiveT = 0, botT = 0;
    unsigned wrecksMarked = 0;                            // a bot on the sonar calls each wreck once a night (bit = index)
};
const char* SonarBandName(int band);
bool SonarBandHas(int band, float depth);
const float SONAR_RANGE = 150, SONAR_COOL = 3, SONAR_LIFE = 6, SONAR_MARK_LIFE = 10;

struct Eco;                                               // the food web (trawl_eco.h)

// The skiff (design doc v2, "The skiff"; trawl_skiff.cpp): a 4.5 m rowing boat hung on the stern davit. A hand at the
// davit lowers it (8 s) or recovers it (10 s, alongside the stern with the Gannet stopped). Hands aboard it are on
// deck DECK_SKIFF, their p in the skiff's own frame (x forward, y to starboard); ashore on a landing they are on
// DECK_SHORE, their p in the landing's frame. It rows on the two mouse buttons (left the port oar, right the
// starboard): a good rhythm makes 1.5 m/s with one rower and 2.2 with two; a stroke too soon after the last "catches a
// crab" and stops her a second; every stroke writes a little noise into the water. One section of 40; past 25 degrees
// of roll it capsizes and everyone aboard goes in.
const int DECK_SKIFF = 2, DECK_SHORE = 3, DECK_DIVE = 4;   // (DECK_DIVE: down on a wreck in the hardhat; Gannet::dive has where)
enum class SkiffState { Stowed, Lowering, Afloat, Recovering, Capsized, Beached, Lost };
// The skiff's refits (design doc v2, "Skiff upgrades", bought at the Slipway; bits of Gannet::skiffUps)
enum SkiffUp : uint32_t { SU_LANTERN = 1, SU_PLANKED = 2, SU_BIGGER = 4, SU_CRATE = 8, SU_TROLL = 16, SU_MUFFLED = 32, SU_MORTAR = 64, SU_LAUNCH = 128 };
struct Skiff {
    SkiffState state = SkiffState::Stowed; float t = 0;   // the davit's progress (lowering / recovering), or the righting
    Vector2 p{}, vel{}; float heading = 0, yawRate = 0;   // world
    float roll = 0, rollV = 0;                            // radians
    float integrity = 40;
    float crabT = 0;                                      // a caught crab: no way on for a second
    float noise = 0;                                      // the strokes' splash, decaying (into the water's sound)
    std::vector<CatchRec> load;                           // catch and salvage aboard (150 kg)
    int landing = -1;                                     // beached at this landing (-1 none)
    bool engine = false;                                  // the steam launch kit running (3 m/s with nobody rowing; noise 4)
    int mortar = 0;                                       // flare mortar shells left tonight (three a night)
    float trollT = 0;                                     // the trolling holders' rods: time since the last strike
    float LoadKg() const { float k = 0; for (const auto& c : load) k += c.kg; return k; }
    Vector2 Forward() const { return {cosf(heading), sinf(heading)}; }
    Vector2 ToWorld(Vector2 l) const { Vector2 f = Forward(); return {p.x + f.x * l.x - f.y * l.y, p.y + f.y * l.x + f.x * l.y}; }
    Vector2 ToLocal(Vector2 w) const { Vector2 f = Forward(), d{w.x - p.x, w.y - p.y}; return {d.x * f.x + d.y * f.y, -d.x * f.y + d.y * f.x}; }
    bool Up() const { return state == SkiffState::Afloat || state == SkiffState::Beached; }
};

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
    bool netLast = false;                                 // the net is in for the night: a bot at the winch hauls it but won't shoot it again
    std::vector<Longline> longlines;
    std::vector<Pot> pots;
    std::vector<LifeRing> rings;
    std::vector<Slot> locker;
    bool harpoonCannon = false; int harpoons = 0, explosives = 0; bool explosiveLoaded = false; float harpoonReload = 0;
    Rod harpoon;                                          // the cannon's tethered fight (a Fight on 120 kg steel), when one is on
    int harpoonSp = -1;
    float gullT = 0, awayGullT = 0, fines = 0;                          // fines: the Owners take these at the dock
    float ramT = 0;                                       // a shark's ram cooldown (design doc, "Threats": sharks ram the hull)
    float chumLeft = 0;                                   // blood still to run out of a thrown chum bucket (at the gutting rail)
    bool ThrowChum(int c);                                // a bucket over the side (needs one in the stores)
    int chargesUsed = 0;
    SonarState sonar;                                     // (trawl_sonar.cpp)
    bool SonarPing(int c);                                // an active ping (every 3 s; noise into the water)
    bool SonarMarkAt(int c, Vector2 aimDeck);             // mark the contact nearest the aim (deck frame); every hand sees its bearing
    void StepSonar(float dt);                             // passive returns, fading, a bot operator's pings and marks
    // bot crew (trawl_bots.cpp; design doc "Bot crew"): every hand after the first is a bot when botsOn
    bool botsOn = false; Skill botSkill = Skill::Able;
    struct Brain {
        int order = -1;                                   // a station the skipper ordered it to (-1: its watch)
        int goal = -1, goalDeck = 0; float think = 0;     // where it is going, and when it next reconsiders
        float castT = 0; bool struck = false; RodState lastRod = RodState::Idle;
        Vector2 lastP{}; float stuckT = 0, sideT = 0; int side = 1;
        std::string bark; float barkT = 0;                // a short line over its head ("Fish on, port!")
        int task = 0;                                     // 0 a station, 1 the life ring for a hand overboard, 2 a leak, 3 following the skipper, 4 cutting a hand free of the kelp
        int target = -1; float taskT = 0;                 // who or what the task is for; how long it has been at it
        int follow = -1;                                  // ordered to follow this hand (F), -1 not
        float defT = 0;                                   // clubbing a dangerous landed fish within reach (self-defence)
        float depthT = 0;                                 // an Able hand reads the sounder and sets its lure's depth every few seconds
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
    bool KillDeckFish(int c, float reach = 1.6f, float dmg = -1, bool head = false);
    bool CrateFish(int c, float reach = 1.6f);                              // E beside a dead fish on the deck: into a catch crate (birds can't have it)
    // a bird flying off with a stolen fish (design doc v2, "Birds and the catch crates"): shoot it down and the bird and
    // the fish drop where they fall (on the deck, or afloat to be gaffed); a bird is a catch too (gull 4, pelican 12,
    // frigatebird 15), and every bird kill is Airborne
    struct Thief { int kind = 0; CatchRec fish; Vector2 p{}; Vector2 v{}; float z = 4, t = 0; };
    std::vector<Thief> thieves;
    void Steal(int holdIdx, int kind);                    // a bird takes this fish off the deck
    void StepThieves(float dt);
    void DropThief(int idx, int by);                      // shot: the bird and its fish come down
    int landedSmall = 0, landedBig = 0;                   // hook fish landed this run under 0.4 kg / of 1 kg or more (the shakedown's lessons count these: birds may have the fish)
    int junkBottles = 0, junkKeys = 0, junkCharts = 0;    // junk kept for the landings (the skiff)
    void FindJunk(Vector2 deckAt, const char* how);
    void CastJunk(Vector2 deckAt);                        // an empty cast reeled home: junk on the hook now and then
    void DropFish(int idx);                               // harried (a frigatebird): only the fish comes down
    // the skiff (trawl_skiff.cpp)
    Skiff skiff;
    Vector2 SkiffBerth() const;                           // where she lies alongside the stern (world)
    bool SkiffAlongside(float r = 4) const;               // afloat within r m of the berth
    void DavitWork(int c, bool held, float dt);           // a hand at the davit: lower it, or recover it
    bool BoardSkiff(int c);                               // E: from the davit (or the water beside it) into the skiff
    bool LeaveSkiff(int c);                               // E in the skiff: up onto the Gannet's stern, or ashore when beached
    void Oar(int c, bool port, bool starboard);           // a stroke on either oar (the press)
    void StepSkiff(float dt);
    void SkiffCapsize(const std::string& why);
    void SkiffHit(float dmg, const std::string& why);
    void SkiffRock(float rad);                            // a shove to her roll (a ram, a thrashing fish, a wave's slap)
    bool SkiffLand(const CatchRec& r);                    // a fish or salvage into her (false: over 150 kg)
    int SkiffRowers() const;
    // fishing from the skiff (design doc v2, "Fishing from the skiff"): T at the oars takes up her line (a light rod in
    // the stern sheets) and back. The kill happens at the waterline: under 30 kg comes aboard her (a thrasher of 10 kg
    // or more rocks her hard); anything bigger is killed alongside and goes on the tow line, slowing her and bleeding
    // all the way home. A hooked fish tows her and heels her toward it. Inside a skiff mark the bites come twice as often.
    Rod skiffRod;
    // boss lures and what the harbour gives (trawl_quest.cpp)
    int bossLures = 0; bool bossArmed = false;            // R on the skiff's line arms a boss lure for the next cast
    float bossBiteT = -1; int bossBiteIdx = -1;           // a mini-boss coming to a boss lure over boss water
    int bossCaught = 0;                                   // bits: mini-bosses landed this deadline
    std::vector<std::string> drops;                       // mini-boss drops (not sold: harbour folk want them; one can be worn as a charm)
    bool tagGun = false; int tagged = 0;                  // turtles tagged and released alive (the naturalist)
    int highKills = 0;                                    // kills at Killscore 2.5x or better tonight (the gunsmith's apprentice)
    bool spiceRub = false;                                // the cook's spice rub: cooking climbs 25% faster this deadline
    int rareLures = 0; bool rareLureNight = false;        // the naturalist's lure: rare fish bite more, for a night (used at cast off)
    int legendLures = 0;                                  // Mother Carey's legend lures (the legendary fish are still to be designed)
    bool WearsCharm(int ci, int charm) const { return ci >= 0 && ci < (int)crew.size() && crew[ci].charm == charm; }
    bool AnyWears(int charm) const { for (const auto& c : crew) if (c.charm == charm && !c.dead) return true; return false; }
    bool ArmBossLure(int c);                              // R on the skiff's line
    void StepBoss(float dt);                              // a boss lure over boss water calls the mark's mini-boss to it
    void OnLanded(CatchRec& r, int holder);               // every hooked fish landed: a Glimmer roll (the lucky coin), the mini-boss's drop
    std::vector<CatchRec> towed;                          // on the tow line, alongside (into the hold when she's hauled up)
    void StepSkiffRod(float dt);
    Vector2 SkiffRodTip() const;                          // (world) the rod's tip over her starboard quarter
    Vector2 HandWorld(int c) const;                       // where a hand is on the sea, whichever deck it is on
    // below decks (design doc v2, "Below decks"; trawl_below.cpp): deck 1 is the engine room, the fish hold (through the
    // watertight door) and the fo'c'sle (its own hatch: the magazine locker, the Medic's cot); hatches open, shut or
    // battened; an oil lamp in each space (out past 20 deg of roll); bilge eels when she's half full; the fire spreads
    struct Hatch { Vector2 at; int state = 0; };          // 0 open, 1 shut (4 s to open), 2 battened (10 s; not from below)
    Hatch hatches[2];                                     // 0 the main hatch (the hold), 1 the fore hatch (the fo'c'sle)
    bool doorOpen = true;                                 // the watertight door between the hold and the engine room
    struct OilLamp { Vector2 at; bool lit = true; };
    OilLamp lamps[3];                                     // the engine room's, the hold's, the fo'c'sle's (deck 1)
    float fireSpread = 0, cotT = 0, hookT = 0;            // the fire's 30 s to the bunker; the Medic's 10 s at the cot; hooking the skiff on from the water
    void InitBelow();
    int SpaceAt(Vector2 p, int deck) const;               // 0 the deck, 1 the engine room, 2 the hold, 3 the fo'c'sle
    bool BelowUse(int c);                                 // E: a hatch (down/up, or open it), the door, a lamp, smothering the fire
    bool HatchCycle(int c);                               // R at a hatch on deck: open -> shut -> battened -> shut
    void StepBelow(float dt);
    void FinishRecovery();                                // the skiff comes up (the davit's falls, or hooked on from the water)
    // the landings (trawl_landing.cpp)
    std::vector<Landing> landings;
    bool foughtCanoes = false;                            // refused the canoes: the Atoll's elder won't trade
    void BuildLandings();                                 // from the ground's chart (Eco::landingAt)
    void FloodSandbar(int li);                            // 02:00: the Sandbar goes under (what's on it is lost; who's on it swims)
    uint32_t skiffUps = 0;                                // SkiffUp bits (the Slipway's skiff refits)
    bool SkiffUp(uint32_t u) const { return (skiffUps & u) != 0; }
    float SkiffHullMax() const;                           // 40, or 70 with planked-up sides
    float SkiffLoadMax() const;                           // 150 kg, or 250 in the bigger skiff
    int SkiffSeats() const { return SkiffUp(SU_BIGGER) ? 3 : 2; }
    bool SkiffEngine(int c);                              // X in the skiff with the steam launch kit: the engine on or off
    bool SkiffMortar(int c);                              // W in the skiff with the flare mortar: a flare 40 m round her for 20 s (everyone sees it)
    int LandingNear(Vector2 world, float extra) const;    // the landing whose shore is within extra m, or -1
    void StepLandings(float dt);
    void ShoreMove(int c, Vector2 wish, float dt);        // on foot ashore (wish in the Gannet's frame, as on screen)
    bool ShoreUse(int c);                                 // E ashore: the skiff's load, the fire, a cache, the beach, a crab
    bool EatCooked(int c);                                // R ashore with a cooked fish of 2 kg or more: mends one minor injury
    bool BeachSkiff(int c);                               // E in the skiff close to a shore: run her up and step ashore
    void StealFrom(std::vector<CatchRec>& v, int idx, Vector2 world, int kind);   // a bird takes a fish from the skiff or a beach
    bool SwimInSkiffFrame(int c) const;                   // a swimmer nearer the skiff than the Gannet (the screens follow her then)
    void SkiffSwim(int c, bool held, float dt);
    bool screwFouled = false; float cutT = 0;          // the Weeds' kelp round the screw (half her way)
    // the Weeds' threats (design doc v2, the Weeds; trawl_weeds.cpp). A Siren sings from the lanes and pulls the helm
    // toward her rocks (40 s; a flare near her or a shot at her drives her off; reaching her is a holed bow). A Kelp
    // Wraith takes a hand at the rail by the ankle while she lies in the canopy (8 s to drag them over; E beside them, or
    // their own knife, cuts them free; the kelp crown keeps them off). Feral Mermen gather at a net towed near the kelp
    // (splashing at the cod end for 12 s: a shot, a flare or the searchlight on them sends them off, else they cut it open).
    struct SirenState { bool on = false; Vector2 p{}; float t = 0; };
    SirenState siren; float sirenCool = 240;
    float wraithCool = 120;
    struct MermenState { bool on = false; Vector2 p{}; float t = 0; };
    MermenState mermen; float mermenCool = 150;
    bool marketNight = false;                             // the Mermen's market (a Weeds variant): they come to trade, and leave the nets alone
    bool cultRaid = false;                                // (Atlantis) the cult's hoard taken, or its bonfire used: the longboats come
    float bellT = 99; int bellRings = 0;                  // since the ship's bell last rang; rings tonight (a Ghost Ship answers a bell rung too often)
    // Atlantis Waters' threats (design doc v2, pages 38, 49-50; trawl_atlantis.cpp): the Pale Eye (Wake twice as fast
    // while a hand looks at it), the Deep Choir (a Siren chorus that sings the whole crew to the rails; the bell breaks
    // it, a shot at the singer ends it), a Cult longboat (circles and chums; outrun it or rifle the rowers), the Ghost
    // Ship (Wake 65 or the bell rung too often: alongside, boards with drowned crew, steals set gear; flee or fight for a
    // relic chest) and the Kraken (Wake 90: the Glass, then arms that take crew and gear and crush the hull; cut the net,
    // kill the lantern, run at full steam, harpoon an arm)
    bool eyeLooked = false; float eyeBlinkT = 0;
    struct ChoirState { bool on = false; float t = 0, calmT = 0; Vector2 singer{}; bool surfaced = false; };
    ChoirState choir; float choirCool = 200;
    struct LongboatState { bool on = false; Vector2 p{}; float ang = 0, t = 0, fleeT = 0; int hits = 0; };
    LongboatState longboat; float longboatCool = 150;
    struct GhostShipState { int state = 0; Vector2 p{}; float t = 0, fleeT = 0; };   // 0 none, 1 a bell answering (approach), 2 alongside, boarders aboard
    GhostShipState ghost; bool ghostDone = false;
    struct KrakenState { int state = 0; float t = 0, armT = 0, fleeT = 0; int letGo = 0; };   // 0 none, 1 the Glass (the tell), 2 the arms
    KrakenState kraken; bool krakenDone = false;
    bool Atlantis() const;
    void StepAtlantis(float dt);
    // diving (design doc v2, "Diving and salvage"; trawl_dive.cpp): a hand with the hardhat suit goes down off the stern
    // over a wreck (she lying still within 15 m of it); a hand at the air pump keeps the gauge in the green (without it
    // the helmet holds 30 s); drifting more than 15 m off fouls the hose; in the wreck the diver moves room to room
    // (Dive* below), takes salvage (a crowbar for a locked cabin), and sends it up in the basket from a breach; two tugs
    // (the recall) and the winch brings them up at 1 m/s (faster: the bends)
    struct DiveState {
        int diver = -1, wreck = -1, room = -1;            // who is down, in which of the ground's wrecks, in which room (-1: on the line)
        int diver2 = -1; bool bell = false;               // (the diving bell) a second diver alongside the first; the air is the bell's, not the pump's
        float depth = 0, air = 30, gauge = 1;             // m below the surface; helmet air (s); the pump's gauge 0..1 (green 0.4-0.9)
        bool recall = false; float ascentRate = 1;        // being winched up; m/s
        bool carrying = false; int item = -1;             // salvage in both hands (the wreck's item index)
        float pumpT = 0;                                  // since the last pump stroke
        float roomT = 0, holdT = 0, lampOutT = 0, siltT = 0, moveT = 9;   // in this room; held fast (a bite, a Drowned's grip); the lamp stolen; blinded by silt; since the last move
        bool hoseBitten = false;                          // a Ghost Worm hatchling bit through the hose: no fresh air until she's up
    };
    std::vector<CatchRec> basketLine;                     // salvage on its way up in the basket (cookT: seconds left on its line)
    DiveState dive;
    bool hardhat = false;                                 // the hardhat suit (the Chandler, 350)
    bool divingBell = false;                              // the diving bell (the Slipway, 1,200): two divers to 120 m, its own air
    std::vector<Wreck>* wrecks = nullptr;                 // the ground's wrecks this deadline (the session's)
    int WreckNear(float r) const;                         // a wreck within r m of her, or -1
    bool StartDive(int c);                                // a hand with the suit at the stern, she still over a wreck
    void DivePump(int c, bool stroke);                    // a hand at the air pump: a stroke
    bool DiveMove(int linkTo);                            // the diver: through a door, hatch or squeeze into another room
    bool DiveTake();                                      // the diver: lift an item in this room (a crowbar opens a locked cabin first)
    bool DiveBasket();                                    // the diver: at a breach, the carried item into the basket
    void DiveRecall();                                    // two sharp tugs: haul me up
    void StepDive(float dt);
    void StepDrowned(float dt);                           // the Drowned on the deck (the Grotto's, and the Ghost Ship's boarders)
    bool bloomNight = false, eelRun = false;              // the Grotto's mould bloom (anglers stay away, the Drowned see her from anywhere) and glass eel run (anglers follow the eels to the light)
    // the Grotto's threats (design doc v2, page 49-50; trawl_grotto.cpp): a Lantern Angler's second light lures the
    // nearest hand to the rail; a Ghost Worm woken by vibration (taut lines, the net, the screw) bites lines, snags the
    // net and coils the hull; an isopod swarm drawn by offal climbs the anchor chain, eats the catch, bites ankles; the
    // Drowned climb aboard by the wrecks, slow and relentless, and drag a hand to the rail
    struct AnglerState { bool on = false; Vector2 p{}; float t = 0; int lured = -1; };
    AnglerState angler; float anglerCool = 150;
    struct WormState { int state = 0; Vector2 p{}; float t = 0, ang = 0, actT = 0, quietT = 0, coilT = 0; int hits = 0; };   // 0 asleep, 1 circling (the tell), 2 hunting, 3 coiled round the hull
    WormState worm; float wormWake = 0;
    struct IsopodState { int state = 0; float t = 0, eatT = 0; int n = 0; float drawT = 0; };   // 0 none, 1 clicking on the anchor chain, 2 aboard
    IsopodState isopods;
    struct DrownedSailor { Vector2 p{}; float hp = 40; int grab = -1; float hitT = 0; };   // on the deck (deck frame)
    std::vector<DrownedSailor> drowned; float knockT = -1, drownedCool = 120;
    bool Grotto() const;
    void StepGrotto(float dt);
    bool HitDrowned(Vector2 deckP, float dmg, float reach);   // a melee blow on the deck: a Drowned sailor, or a stamp on the isopods
    bool Weeds() const;
    void StepWeeds(float dt);
    bool FreeTangled(int c);                              // E beside a tangled hand (or the hand itself, with a knife)
    void CutNet(const std::string& why);                  // the cod end slit: the catch goes, the net comes up empty
    void CutScrew(int c, bool held, float dt);           // a swimmer at the stern holding left mouse cuts it free (4 s)           // a swimmer beside a capsized skiff holding left mouse rights her
    bool HitDeckFish(int idx, float dmg, int by, int how, bool head, float range);   // a blow on a deck fish (KillHow); true if it died of it
    float deckBlood = 0;                                  // blood on the planking: drains through the scuppers into the sea at 20% a second
    // role upgrades' state: Full Steam (once a night, the session resets it), Iron Hull (applied once), Miracle Worker
    bool fullSteamUsed = false, ironHull = false, miracleUsed = false; float fullSteamT = 0, miracleT = 0;
    // the magazine stock (design doc v2, "Carrying and ammunition"): rounds, shells, spears, flares, pellets, rivets kept
    // in the locker; a hand restocks its spare reload there (until below decks has the fo'c'sle's magazine locker)
    int ammoRounds = 0, ammoShells = 0, ammoSpears = 0, ammoFlares = 0, ammoPellets = 0, ammoRivets = 0;
    int* AmmoStock(const std::string& kind);              // the stock of a kind, or null (melee, thrown)
    void RestockAtLocker(int c);                          // tops up every catalogue weapon's spare reload in the hand's slots
    void StepDeckFish(float dt);                          // the flopping (StepGear)
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
    bool Jump(int c);                                     // Space off a station: a hop; over the rail it's the sea
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

int RunTrawlSim(int argc, char** argv);                   // depth.exe --trawl-sim <ground> <nights> [crew] [pattern] [runs] [skill] (trawl_sim.cpp)
int RunTrawlGearTest();
int RunTrawlBelowTest();                                  // depth.exe --trawl-below-test
int RunTrawlWeedsTest();                                  // depth.exe --trawl-weeds-test
int RunTrawlGrottoTest();                                 // depth.exe --trawl-grotto-test
int RunTrawlAtlantisTest();                               // depth.exe --trawl-atlantis-test
int RunTrawlDiveTest();                                   // depth.exe --trawl-dive-test
int RunTrawlDiveSceneTest();                              // depth.exe --trawl-divescene-test
bool GrottoShake(Gannet& g, int c);                       // E beside a hand an Angler's light has lured: shake them out of it
int RunTrawlQuestTest();                                  // depth.exe --trawl-quest-test
int RunTrawlSkiffTest();                                  // depth.exe --trawl-skiff-test                                   // depth.exe --trawl-gear-test
int RunTrawlBotTest();                                    // depth.exe --trawl-bot-test
int RunTrawlSailDiag();                                   // depth.exe --trawl-sail-diag (water shipped under way, by weather)
int RunTrawlBoatTest();                                   // depth.exe --trawl-boat-test
int RunTrawlRodTest();                                    // (part of --trawl-boat-test) a rod on the Gannet, cast to landing

} // namespace tw
