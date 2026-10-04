#pragma once
// ============================================================================
//  MOUTHFUL - arcade game 8 of the Deep Arcade (Reference_For_Future_MP_Games/Mouthful — Arcade Game 8 Design
//  Document.pdf; its OCR is in docs/mouthful_pdf_pages/). A 3D eat-and-grow arena: up to 12 mouths (players and bots)
//  share one reef shelf that drops into a trench, start as fry, eat their way up eight tiers, pick a path at each fork,
//  and the first to tier 8 wears the crown. The prey and the NPC predators are Red Tide's ecosystem engine
//  (rt::Ecosystem) on data/mouthful/sea/reef/ (map key "mouthful_reef"); the mouths are Diver agents in its web.
//
//  This header is headless (no drawing): the data (data/mouthful/*.json), the arena's shape, and the World that steps
//  a round. mouthful_game.cpp is the scene; mouthful_net.cpp the arcade's GameHost.
//  World units are metres: x runs west (the shallows) to east (the trench), y is up (the surface is 0), z is north.
// ============================================================================
#include "raylib.h"
#include "redtide.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace mf {

// ---------------------------------------------------------------- the data
enum Band { B_SHALLOWS, B_REEF, B_WALL, B_BLUE, B_TRENCH, B_COUNT };
const char* BandName(int b);
enum PathId { P_EEL, P_CEPH, P_SHARK, P_CRUST, P_PUFFER, P_BLOB, P_COUNT };
const char* PathName(int p);
enum Ability : uint8_t {
    AB_NONE, AB_DASH, AB_LUNGE, AB_DEATH_ROLL, AB_RIBBON, AB_INK, AB_DAZZLE, AB_INK_WALL, AB_HOOK, AB_FRENZY, AB_RAM,
    AB_PIN, AB_BREACH, AB_PUNCH, AB_TAIL_FLIP, AB_JUMP, AB_CLAW_SLAM, AB_ROLL, AB_INFLATE, AB_TOXIN, AB_DETONATE, AB_AMBUSH, AB_COUNT
};
enum Passive : uint8_t {
    PS_NONE, PS_HOLE, PS_VENOM, PS_DRAG, PS_ARMOR_BACK, PS_OMEN, PS_JET, PS_CAMO, PS_HOVER, PS_EIGHT_ARMS, PS_TRENCH_BORN,
    PS_BLOOD_SENSE, PS_THICK_SKIN, PS_ELECTRO, PS_APEX, PS_IRON_STOMACH, PS_BURROW, PS_SPINES_BACK, PS_CRUSHER, PS_FORTRESS,
    PS_SCAVENGER, PS_SPINES, PS_BARBED, PS_PLATES, PS_UNTOUCHABLE, PS_VENOM_SPINES, PS_COUNT
};
struct TierDef { float mass = 0, length = 0.1f, speed = 4, biteCd = 0.8f, turn = 300; };   // (mass: where the tier starts)
struct FormDef {
    std::string key, name, art, abilityText, passiveText, tell;
    int path = -1, tier = 1;
    float speed = 1, hp = 1, bite = 1, reach = 0;      // multipliers on the tier's base; reach: a crustacean's claw (m at tier 1 scale)
    Ability ab = AB_NONE; Passive ps = PS_NONE;
    float cd = 0, p1 = 0, p2 = 0;                      // the ability's cooldown and its two numbers (distance/time, bonus/time, radius/time)
    bool walker = false;                               // the crustaceans: they walk the floor and jump
    Color base{200, 200, 200, 255}, belly{240, 240, 240, 255}, accent{220, 180, 80, 255};
};
struct PreyDef { std::string species; float mass = 3; bool armored = false; bool npc = false; int eatsUpTo = 0; float bite = 1; };
struct Data {
    TierDef tiers[9];                                  // 1..8 (0 unused)
    std::vector<FormDef> forms;                        // forms[0] is the fry
    std::vector<PreyDef> prey;                         // by name (mouthful_sea.json)
    // mouthful_scoring.json
    float scoreMassPer = 10, scoreKill = 50, scoreKillPerTier = 25, scoreCrownPerS = 10, scoreKingKilled = 300, scoreTierPast4 = 50;
    float scoreApex = 200, scoreLeviathan = 1000, scoreOrca = 100, scoreWin = 200, scoreBlobKing = 500;
    float swallowBelow = 0.6f, fightPct = 0.10f, decayPerMin = 0.01f, respawnS = 5, immuneS = 10, startMass = 15, swallowS = 1;
    float boostMul = 1.6f, staminaS = 4, staminaRefillS = 6, kingBonus = 0.10f, streakBonus = 0.05f, armoredCost = 5;
    float planktonPerS = 1;
    // mouthful_bots.json (per level 1..3)
    float botSense[4] = {0, 18, 24, 30}, botAbilityUse[4] = {0, 0.2f, 0.7f, 1}, botHuntMouths[4] = {0, 0, 0.6f, 1};
    std::vector<std::string> botNames;
};
const Data& D();
int FormIndex(const std::string& key);
const PreyDef* PreyOf(const std::string& species);
// the fork's choices: the six paths' first forms at tier 2, a path's two second forms at 4, its finals at 6
std::vector<int> ForkChoices(int path, int tier, int fromForm);

// ---------------------------------------------------------------- the arena (doc pp. 2-4)
constexpr float X0 = -300, X1 = 300, Z0 = -150, Z1 = 150;   // ~600 m across
float FloorY(float x, float z);                     // the seabed (deterministic; the scene builds its mesh from this)
int BandAt(Vector3 p);
float LightAt(Vector3 p, float dusk);               // 1 lit .. 0 dark (sight scales with it)
Vector3 CurrentAt(Vector3 p);                       // the shallows' surface current, the upwelling, the trench's downdraft
struct Hole { Vector3 pos; float r = 0.9f; int maxTier = 2; };   // an eel hole (tiers 1-2) or a wall cave (up to tier 5)
struct Coral { Vector3 pos; float r = 1.5f, h = 4; };           // a coral head: a column the big can't squeeze past
const std::vector<Hole>& Holes();
const std::vector<Coral>& Corals();
constexpr Vector3 CLEANING{-95, -22, 30};           // the cleaning station (a 10 m truce)
constexpr Vector3 HOLLOW{240, -292, 0};             // the leviathan's hollow
constexpr Vector3 BRINE{210, -296, -40};            // the brine pool (10 a second; crustaceans float)
constexpr float BRINE_R = 14;

// ---------------------------------------------------------------- the round
struct Input {
    float yaw = 0, pitch = 0;                        // where to swim (the camera's aim for a person)
    bool swim = false, boost = false, bite = false, ability = false, brake = false;
    int fork = -1;                                   // a fork's pick (index into the offered choices)
};
struct Mouth {
    int id = 0; std::string name;
    bool bot = true; int botLevel = 1; int seat = -1;  // seat: the arcade seat a person plays from (-1 a bot)
    bool alive = false; float respawnT = 0;
    Vector3 pos{}, vel{}; float yaw = 0, pitch = 0, bank = 0;
    float mass = 15; int tier = 1; int form = 0; int path = -1;
    float stamina = 1; bool boosting = false;
    float biteCd = 0, abCd = 0, abT = 0;              // abT: the ability's active seconds left
    float immuneT = 0, swallowT = 0, stunT = 0, blindT = 0, reverseT = 0, poisonT = 0, bleedT = 0, holdT = 0, jetT = 0, frenzyT = 0, frenzyK = 0;
    float stillT = 0, buriedT = 0, morphT = 0, biteAnim = 0, hurtT = 0, tellT = 0;
    float markT = 0; int lastHurtBy = -1;             // markT: marked for the NPC sharks (bit a glowing fry, or bit inside the truce)
    int holdOf = -1; bool holdAgent = false;          // what we hold (a mouth id or an agent index)
    float dashT = 0; Vector3 dashV{}; int dashLeft = 0; int dashKind = 0;   // (1 lunge: bites at the end, 2 death roll: grabs, 3 ram: knocks back)
    bool ambush = false, hidden = false, airborne = false;
    int pendingFork = 0; float forkT = 0; std::vector<int> forkOpts;
    int lastPath = -1, streak = 0;
    int agent = -1;                                   // its Diver agent in the web
    // the round's score
    float score = 0, massEaten = 0, crownT = 0; int kills = 0, deaths = 0, bestTier = 1; bool king = false; bool blobKing = false;
    int apexKills = 0; bool leviathanKill = false;
    std::string lastCause;
    Input in;
    // the bot's mind
    float thinkT = 0; int tgtMouth = -1, tgtAgent = -1; Vector3 goal{}; bool fleeing = false; Vector3 fleeFrom{};
    int chaseId = -1; float chaseBest = 1e9f, chaseT = 0; int banId = -1; float banT = 0;   // (a chase that isn't closing is given up)
    float bodyR() const;
};
struct Cloud { Vector3 pos; float r = 5, t = 3; int owner = -1; int kind = 0; };   // 0 ink, 1 toxin
struct Plankton { Vector3 pos; float r = 5; Vector3 drift{}; };
struct FeedLine { std::string text; float t = 0; Color c{255, 255, 255, 255}; };
struct Opts { int humans = 1; int bots = 11; float minutes = 15; int mode = 0; int botLevel = 0; uint32_t seed = 1; };   // botLevel 0: a mix

struct World {
    std::unique_ptr<rt::MapData> sea; rt::Ecosystem eco;
    std::vector<Mouth> mouths;
    std::vector<Plankton> plankton;
    std::vector<Cloud> clouds;
    std::vector<FeedLine> feed;
    std::vector<float> preyMass;                      // by species index (the web's mass of a whole one)
    std::vector<int> npcEats;                         // by species index: the highest mouth tier an NPC predator hunts (0 none)
    std::vector<float> reviveS, deadT;                // the World's refill: seconds before a dead fish comes back (by species), and per agent
    Opts opts;
    float time = 0, roundLen = 900; bool over = false; int winner = -1;
    int king = -1;                                    // the mouth wearing the crown
    int leviathan = -1; float levAwakeT = 0, levNoise = 0; bool levAte = false;
    uint32_t rng = 1;
    bool mirror = false;                              // a guest's copy of the host's world (stepped only by snapshots)
    bool duel = false;                                // --mouthful-duel: the bots fight each other and nothing else
    // the round's report (--mouthful-round)
    float firstKingT = -1; int crownsChanged = 0;
    int deathsBy[4] = {};                             // 0 players, 1 NPC predators, 2 the boat and hazards, 3 the leviathan
    int bestTierByPath[P_COUNT] = {};

    void Init(const Opts& o);
    void Step(float dt);
    int AddMouth(const std::string& name, bool bot, int level, int seat);
    void Respawn(Mouth& m, bool first);
    float Rand();
    float Rand(float a, float b) { return a + (b - a) * Rand(); }
    void Say(const std::string& s, Color c = Color{255, 255, 255, 255});
    const FormDef& FormOf(const Mouth& m) const;
    float Speed(const Mouth& m) const;               // m/s (the tier's base x the form x the crown x the streak)
    float BiteMul(const Mouth& m) const;
    float HpMul(const Mouth& m) const;
    float Length(const Mouth& m) const;              // body length, metres
    float Reach(const Mouth& m) const;
    int TierOfMass(float mass) const;
    bool Visible(const Mouth& viewer, const Mouth& m) const;   // a bot's sight (ink, camouflage, burrow, ambush, dark)
    float MassOfAgent(int ai) const;
    int MouthOfAgent(int ai) const;
    void Bite(Mouth& m, bool free = false);
    void UseAbility(Mouth& m);
    void Feed(Mouth& m, float mass, bool kill);       // mass eaten (scored)
    void Hurt(Mouth& m, float mass, int byMouth, int byAgent, const char* cause);
    void KillMouth(Mouth& m, int byMouth, int byAgent, const char* cause);
    void GrowCheck(Mouth& m);
    void PickFork(Mouth& m, int choice);
    void StepMouth(Mouth& m, float dt);
    void StepBot(Mouth& m, float dt);
    void StepNpc(float dt);
    bool DecideHook(rt::Agent& a, int idx);
    void OnDiverHit(int diverAgent, int attacker, float dmg);
    int Leader() const;                              // the mouth with the highest score
    std::vector<int> Board() const;                  // mouths by score
};

float ScoreOf(const World& w, const Mouth& m);     // the round score so far (mass eaten, kills, the crown, tiers)
bool SwallowOk(const World& w, const Mouth& by, const Mouth& t);

// tools (main.cpp)
int RunMouthfulTest();                               // --mouthful-test
int RunMouthfulRound(int bots, float minutes, uint32_t seed, int runs);   // --mouthful-round
int RunMouthfulDuel(const std::string& a, const std::string& b, float mass, int runs);   // --mouthful-duel

} // namespace mf
