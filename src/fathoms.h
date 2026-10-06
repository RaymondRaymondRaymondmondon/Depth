#pragma once
// Fathoms (the Deep Arcade's strategy game): a 2-6 player steampunk island RTS. The design is "Fathoms - RTS Design
// Document.pdf" (Reference_For_Future_MP_Games; OCR in docs/fathoms_pdf_pages; build log docs/FATHOMS_PROGRESS.md).
// This header is the headless core: the balance (data/fathoms/fathoms_balance.json), the tile map and its islands,
// players, units, buildings, resource nodes, neutral sites, and the world stepped at a fixed 20 ticks a second from
// commands only (so the network and the AI drive it exactly as a player's clicks do).
// Frame: tile (x, y) covers [x, x+1) x [y, y+1); positions are in tiles; the 3D scene puts x east, y south.
#include "raylib.h"
#include "raymath.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace fa {

constexpr float STEP = 0.05f;          // 20 ticks a second
constexpr int MAX_PLAYERS = 6;
enum Res { R_FOOD, R_BRASS, R_COAL, R_ICHOR, R_DOUB, R_COUNT };
using Cost = std::array<float, R_COUNT>;
const char* ResName(int r);

// ---------------------------------------------------------------- the balance (every number from the data file)
enum DmgType : uint8_t { D_NONE, D_MELEE, D_PIERCE, D_BLAST, D_HEAL };
enum Tag : uint32_t { TG_INFANTRY = 1, TG_RANGED = 2, TG_LARGE = 4, TG_MOUNTED = 8, TG_SIEGE = 16, TG_SHIP = 32, TG_LIGHT = 64, TG_HEAVY = 128,
                      TG_AIR = 256, TG_WORKER = 512, TG_HERO = 1024, TG_ULTIMATE = 2048, TG_TORPEDO = 4096, TG_SUPPORT = 8192, TG_BUILDING = 16384 };
enum Move : uint8_t { MV_LAND, MV_SEA, MV_AMPHIB, MV_AIR };
struct UnitDef {
    std::string key, name, from; int era = 0, faction = -1;   // faction -1: shared
    Cost cost{}; int pop = 1; float hp = 50, attack = 0; DmgType type = D_NONE; float reload = 2, range = 0.5f, minRange = 0, speed = 1, waterSpeed = 0;
    float armor[2] = {0, 0}; uint32_t tags = 0; std::vector<std::pair<uint32_t, float>> bonus; float train = 20, vision = 6, upkeep = 0, heal = 0;
    int carry = 0, carryWorkers = 0; float shore = 0, life = 0, aura = 0; Move move = MV_LAND; std::string special;
};
struct BuildingDef {
    std::string key; int era = 0; Cost cost{}; float hp = 1000, time = 30; int size = 2, pop = 0, garrison = 0, max = 0;
    std::string drop; float vision = 5, attack = 0, range = 0, claim = 0, coalRange = 0, bonusShips = 0, score = 0; bool coast = false, wall = false, relics = false, antiAir = false; int faction = -1; float foodPerSec = 0, auraR = 0;
    std::vector<std::string> trains;
};
struct TechDef { std::string key, name, at; int era = 0; Cost cost{}; float time = 30; std::vector<std::pair<std::string, float>> fx; int faction = -1; int forgeLine = -1, forgeLevel = 0; };
struct FactionDef {
    std::string key, name, style, mechanic, weakness, ultimate, wonder, heroName, heroActive, heroAura; float heroAuraAmount = 0, heroCooldown = 60, heroHp = 500;
    float costMult = 1, landSpeedMult = 1, foodToCoal = 0; float gatherMult[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1}; float eraGather[3] = {1, 1, 1};
    std::vector<int> uniques; std::vector<int> techs; std::vector<std::string> cards;
};
enum NodeKind : uint8_t { N_FISH, N_GROVE, N_KELP, N_BRASS, N_WRECK, N_COAL, N_VENT, N_PEARL, N_FARM, N_COUNT };
struct NodeDef { float amount = 400, regrowEvery = 0, rate = 0.4f; int res = R_FOOD; bool water = false; };
struct Balance {
    float weights[R_COUNT] = {1, 1, 1.5f, 4, 1};
    NodeDef nodes[N_COUNT]; float carry[R_COUNT] = {10, 10, 8, 5, 5}; float homeCoal = 800, volcanoVent = 3000, volcanoRate = 1.6f, wreckDoub = 30;
    Cost start[3]{}; int startWorkers = 4, startBoats = 1, startSkiffs = 1;
    int popHarbor = 10, popCottage = 5, popColony = 8, cap2 = 100, cap4 = 80, cap6 = 60;
    float emptySpeed = 0.6f, emptyDamage = 0.75f;
    float exBaseOut = 70, exPer = 100, exStep = 0.03f, exDrift = 0.01f, exDriftEvery = 20, exSell = 15;
    float buildMinFrac = 0.3333f;
    Cost eraCost[3]{}; float eraTime[3] = {0, 90, 120}; std::vector<std::string> eraNeeds[3];
    std::vector<UnitDef> units; std::vector<BuildingDef> buildings; std::vector<TechDef> techs; std::vector<FactionDef> factions;
    // combat and morale
    float splashR = 1.5f, splashFrac = 0.5f, landingWindow = 10, landingMult = 1.25f, wadingMult = 1.2f, shallowLand = 0.5f, shallowShip = 0.7f;
    float jungleRanged = 0.75f, jungleHidden = 4, highDeal = 1.25f, highTake = 0.85f, hillTowerRange = 2, lavaDps = 40, currentShip = 1.5f;
    float moraleHome = 5, moraleAway = 2, moraleEnemy = 2, moraleEvery = 10, low1 = 50, low1Dmg = 0.9f, low2 = 25, low2Dmg = 0.75f, low2Speed = 0.9f, coalRange = 30, noCoalSpeed = 0.75f;
    // the map
    struct Size { std::string name; int players = 2, grid = 128, islands = 12, volcanoes = 1, coves = 1, tribes = 2, ruins = 3, coal = 4; };
    std::vector<Size> sizes; float weatherEvery = 180; float weatherP[4] = {0.6f, 0.2f, 0.15f, 0.05f}; float stormDps = 2, stormSpeed = 0.6f, whirlTime = 45;
    // neutral powers
    float neutralStrength[3] = {0.7f, 1, 1.3f};
    float coveHp = 4000, coveCannon = 14, coveRange = 8, contractEvery = 90, raidWarning = 20, pirateRaidEvery = 240, blackbeardHp = 1500; int covePirates = 8, coveCannons = 4;
    struct Deal { std::string key; int era = 0; float price = 150, time = 120; int sloops = 0, gunboats = 0, ironclads = 0, pirates = 0; };
    std::vector<Deal> deals; float bmSell = 20, bmBuy = 60, bmIchor = 100;
    float tribeHp = 3000, tribeWake = 360, tribeRaidEvery = 300, tribeGrowth = 60, tribeTributeEvery = 300, relicChance = 0.25f, ritualTime = 45, allyEvery = 60;
    int tribeWarriors = 8, tribeThrowers = 4, tribeCap = 16, allyCap = 6, raidSize[3] = {4, 6, 8}; Cost tribute{}, tribeLoot{}, kinship{};
    float volcanoWake = 600, eruptEvery = 240, eruptWarning = 30, lavaTime = 40, ash = 60, sunGodHp = 4000, sunGodBeam = 60, sunGodReform = 480, altarHold = 45, altarIchor = 1; int imps = 6;
    float krakenWake = 1200, krakenHp = 6000, krakenDps = 30; Cost krakenReward{}; float ghostWake = 900, ghostHp = 1200, ghostReward = 400, wreckTrap = 0.2f; int ruinSentinels = 4;
    float ceasefire = 180, tributeFee = 0.1f, betrayMorale = 15, betrayPause = 60, allianceAge = 300, tradePerTile = 0.15f;
    float conquestGrace = 90, relicCountdown = 180, volcanoCountdown = 240;
    float scIsland = 20, scRelic = 40, scKill = 10, scRaze = 20, scGather = 100, scEra[3] = {0, 50, 100}, scTech = 5, scWonder = 150, scBoss[4] = {50, 60, 80, 25};
    struct Ai { std::string name; float reaction = 1, steamAt = 600, attackAfter = 0, gatherMult = 1; };
    std::vector<Ai> ai;
    std::vector<std::string> resNames;
    int Unit(const std::string& key) const;          // index of a unit def by key (-1: none)
    int Building(const std::string& key) const;
    int Tech(const std::string& key) const;
};
const Balance& B();
Balance& BMut();
uint32_t BalanceHash();

// ---------------------------------------------------------------- the map
enum TileKind : uint8_t { T_DEEP, T_SHALLOW, T_BEACH, T_GRASS, T_JUNGLE, T_HILL, T_MOUNTAIN, T_LAVA, T_KELP, T_CURRENT, T_COUNT };
enum IsleKind : uint8_t { I_HOME, I_FERTILE, I_MINING, I_COAL, I_REEF, I_RUIN, I_TRIBAL, I_COVE, I_VOLCANO, I_PLAIN };
struct Island { int kind = I_PLAIN; Vector2 c{}; float r = 5; int home = -1; int owner = -1; int lighthouse = -1; std::vector<int> tiles; int harborSpot = -1; };
struct Node { int kind = N_FISH; Vector2 p{}; float amount = 0, cap = 0, regrowT = 0; int island = -1; bool trapped = false; int farm = -1; };
enum MapType { MT_ARCHIPELAGO, MT_TWIN, MT_RING, MT_SCATTERED };

// ---------------------------------------------------------------- units and buildings
enum Order : uint8_t { O_IDLE, O_MOVE, O_ATTACK_MOVE, O_ATTACK, O_GATHER, O_RETURN, O_BUILD, O_GARRISON, O_BOARD, O_UNLOAD, O_PATROL, O_HEAL, O_CONVERT, O_RITUAL, O_TRADE, O_FISH_FARM };
enum Stance : uint8_t { ST_AGGRESSIVE, ST_DEFENSIVE, ST_GROUND, ST_PASSIVE };
struct Unit {
    int id = 0, owner = 0, def = 0; Vector2 p{}, vel{}; float facing = 0, hp = 1, morale = 100, cool = 0, xp = 0; int molts = 0;
    Order order = O_IDLE; int target = -1; Vector2 goal{}, patrolA{}, patrolB{}; int targetKind = 0;   // targetKind: 0 unit, 1 building, 2 node, 3 site
    std::vector<int> path; int pathAt = 0; float repathT = 0; bool pathFailed = false;
    int carryRes = -1; float carry = 0; int lastNode = -1;
    float landedT = -100, stunT = 0, bleedT = 0, poisonT = 0, weakT = 0, strongT = 0, cocoonT = 0, life = 0, abilityT = 0, hiddenT = 0;
    int inside = -1;          // a transport this unit rides in (-1: none)
    int garrisoned = -1;      // a building it hides in
    std::vector<int> cargo;   // what a transport carries
    Stance stance = ST_AGGRESSIVE; bool dead = false, converted = false; int hitCount = 0; float spawnT = 0;
    int hire = -1;            // a pirate on contract: the player it attacks (-1: its own business)
    int home = -1;            // a neutral's site (it returns there), a trade ship's far end
    int relic = 0;            // a relic carried
    int builds = -1;          // (a building this worker is adding to this tick)
    float buildDef = -1;      // (a colony ship's: unused)
    float dmgDealt = 0;       // (for the Kraken's reward and stats)
    float auraAtk = 0, auraArmor = 0, auraSpeed = 0, auraHeal = 0;   // (summed each tick from heroes, totems, songs, gazes)
    float fastT = 0, slowT = 0, hasteT = 0, chan = 0, scanT = 0;               // (Full Ahead, Riptide, Overclock; a channel: conversion, ritual)
};
struct QueueItem { int kind = 0; int def = 0; };   // kind 0 a unit, 1 a tech, 2 the next era
struct Building {
    int id = 0, owner = 0, def = 0, x = 0, y = 0, size = 2; float hp = 1, progress = 0;   // progress 0..1 (1: built)
    std::vector<QueueItem> queue; float qT = 0; Vector2 rally{}; bool hasRally = false;
    std::vector<int> garrison; float cool = 0; int relics = 0; bool dead = false; int island = -1; int buildersNow = 0; float attackedT = -100;
    Vector2 Centre() const { return {x + size * 0.5f, y + size * 0.5f}; }
};
// a neutral site: a pirate cove, a tribal town, a volcano altar, a sunken ruin, the Kraken, the Ghost Ship
enum SiteKind : uint8_t { S_COVE, S_TRIBE, S_ALTAR, S_RUIN, S_KRAKEN, S_GHOST, S_WRECK };
struct Site {
    int kind = 0, island = -1, owner = -1; Vector2 p{}; float hp = 0, maxHp = 0, t = 0, t2 = 0; int count = 0, state = 0;   // state: per kind
    int relic = 0; float holdT = 0; int holder = -1; std::vector<int> guards; int large = 0;
    float rep[MAX_PLAYERS] = {}; int peace[MAX_PLAYERS] = {};   // pirate reputation; tribe: 0 raids, 1 truce (tribute), 2 ally (kinship)
    float tributeT[MAX_PLAYERS] = {}; int lastAttacker = -1; std::vector<Vector2> lava;   // (a volcano's lava paths, as tiles)
};
struct Contract { int cove = -1, hirer = -1, target = -1, deal = 0; float price = 0, warnT = 0, liveT = 0; int bids = 0; bool live = false; std::vector<int> units; };

// ---------------------------------------------------------------- players
enum Diplo : uint8_t { DP_WAR, DP_CEASEFIRE, DP_PEACE, DP_OPEN, DP_ALLIED };
struct Player {
    int id = 0, team = 0, faction = 0, color = 0; bool alive = true, ai = false, present = true; std::string name; int difficulty = 1;
    Cost res{}; float gathered[R_COUNT] = {}; int pop = 0, popCap = 0; int era = 0; float eraT = 0; int eraBuilding = -1;
    std::vector<char> tech; int forge[6] = {};
    float score = 0, killScore = 0, razeScore = 0, bossScore = 0; float handicap = 1; float devotion = 0;
    int heroUnit = -1; float heroDeadT = -1, ultimateCD = 0; int ultimateUnit = -1;
    std::vector<int> cards; std::vector<int> cardOffer;   // Nemo's Logbook: kept, on offer
    Diplo stance[MAX_PLAYERS] = {}; float allyT[MAX_PLAYERS] = {}; float noTownT = -1; bool eliminated = false;
    std::vector<uint8_t> seen, explored;   // the fog: what this player sees now, and has ever seen
    float exchange[3] = {1, 1, 1};         // the Exchange's price multipliers (Food, Brass, Coal)
    int wonder = -1; float relicHeldT = 0, altarT = 0; bool coalShort = false; float upkeepAcc = 0, tradePauseT = 0; int kills = 0, losses = 0, built = 0, trained = 0; float eraAt[3] = {0, -1, -1};
    float intelUntil[MAX_PLAYERS] = {}; bool krakenInk = false; std::vector<int> cardsSeen;
};

// ---------------------------------------------------------------- commands (every action is one)
enum CmdKind : uint8_t { C_MOVE, C_ATTACK_MOVE, C_ATTACK, C_GATHER, C_BUILD, C_REPAIR, C_TRAIN, C_RESEARCH, C_CANCEL, C_GARRISON, C_UNGARRISON, C_BOARD, C_UNLOAD,
                         C_STOP, C_STANCE, C_PATROL, C_ERA, C_EXCHANGE, C_RALLY, C_DIPLO, C_HIRE, C_BID, C_TRIBUTE, C_TRIBE, C_CARD, C_ABILITY, C_ULTIMATE, C_RESIGN, C_DELETE, C_TRADE, C_CONVERT, C_OFFER };
struct Command {
    uint8_t kind = C_MOVE; int player = 0; std::vector<int> units; int target = -1, def = -1, x = 0, y = 0, a = 0, b = 0; Vector2 at{}; float amount = 0;
};

// ---------------------------------------------------------------- the world
struct Event { int kind; Vector2 at; int a, b, c; };
enum EventKind { EV_HIT = 1, EV_DIE, EV_BUILT, EV_TRAINED, EV_RESEARCHED, EV_ERA, EV_SHOT, EV_RAZED, EV_CLAIM, EV_ATTACKED, EV_IDLE, EV_ERUPT, EV_TREMOR, EV_WEATHER, EV_PIRATE_WARN, EV_PIRATE_ARRIVE,
                 EV_TRIBE_RAID, EV_TRIBE_ALLY, EV_RELIC, EV_KRAKEN, EV_GHOST, EV_VICTORY, EV_ELIM, EV_DIPLO, EV_CHAT, EV_MOLT, EV_CONVERT, EV_ULTIMATE, EV_HERO, EV_CARD, EV_LAND, EV_GATHER };
struct Projectile { Vector2 a, b; float t = 0, T = 0.3f; int kind = 0; float h = 1; };   // (only to draw: damage is applied when it lands)
struct Pending { int attacker, owner, target, targetKind; float dmg; DmgType type; float t; Vector2 at; float splash; uint32_t bonusVs; };
struct Settings {
    int players = 2, size = -1, mapType = MT_ARCHIPELAGO, victory = 0b0111, timeCap = 1800, start = 0, startEra = 0, neutral = 1, peace = 0, diplomacy = 0, reveal = 0, aiLevel = 1;
    bool pirates = true, tribes = true, volcanoes = true, kraken = true; uint32_t seed = 1; int teams = 0;   // teams: 0 free-for-all, 1 2v2, 2 3v3, 3 2v2v2, 4 4v2, 5 custom
    int faction[MAX_PLAYERS] = {0, 1, 2, 3, 4, 5}, team[MAX_PLAYERS] = {0, 1, 2, 3, 4, 5}, color[MAX_PLAYERS] = {0, 1, 2, 3, 4, 5}; bool ai[MAX_PLAYERS] = {false, true, true, true, true, true};
    float handicap[MAX_PLAYERS] = {1, 1, 1, 1, 1, 1}; int aiLevelOf[MAX_PLAYERS] = {1, 1, 1, 1, 1, 1}; std::string names[MAX_PLAYERS];
};
// the neutral owners (beyond the players' slots)
constexpr int OWN_PIRATE = MAX_PLAYERS, OWN_TRIBE = MAX_PLAYERS + 1, OWN_WILD = MAX_PLAYERS + 2;
inline bool IsPlayer(int owner) { return owner >= 0 && owner < MAX_PLAYERS; }
struct World {
    Settings set; int W = 128, H = 128;
    std::vector<uint8_t> tile; std::vector<float> height; std::vector<int16_t> isle; std::vector<int16_t> occ;   // occ: a building's id on the tile (-1 free)
    std::vector<int16_t> owner;        // the territory: a player's claim on each tile (-1 none)
    std::vector<uint8_t> kelpOwner;    // (Merfolk kelp spread: owner + 1, 0 none)
    std::vector<Island> islands; std::vector<Node> nodes; std::vector<Unit> units; std::vector<Building> buildings; std::vector<Player> players;
    std::vector<Site> sites; std::vector<Contract> contracts; std::vector<Projectile> shots; std::vector<Pending> pending; std::vector<Event> events;
    int nextUnit = 1, nextBuilding = 1; float t = 0; uint32_t rng = 1; uint32_t evCount = 0; int winner = -2; std::vector<int> winners; bool over = false;
    int relicsTotal = 0; float weatherT = 0; int weather = 0; Vector2 weatherAt{}; float weatherR = 18, weatherLeft = 0;
    float relicCountT = -1, volcanoCountT = -1; int relicLeader = -1, volcanoLeader = -1;
    Command cmd;                       // (the command being applied: abilities read its point and target)
    mutable std::vector<float> asG; mutable std::vector<int> asFrom; mutable std::vector<uint32_t> asStamp; mutable uint32_t asGen = 0;   // (path search scratch)
    std::vector<int> gridHead, gridNext; int gridW = 0;   // (units by 8-tile cell, rebuilt each tick)
    template <class F> void ForNear(Vector2 p, float r, F f) {
        if (gridW <= 0) { for (int i = 0; i < (int)units.size(); i++) f(units[i]); return; }
        int gh = (int)gridHead.size() / gridW, x0 = std::max(0, (int)((p.x - r) / 8)), x1 = std::min(gridW - 1, (int)((p.x + r) / 8)), y0 = std::max(0, (int)((p.y - r) / 8)), y1 = std::min(gh - 1, (int)((p.y + r) / 8));
        for (int cy = y0; cy <= y1; cy++) for (int cx = x0; cx <= x1; cx++) for (int i = gridHead[cy * gridW + cx]; i >= 0; i = gridNext[i]) if (i < (int)units.size()) f(units[i]);
    }
    int moraleTick = 0, fogTick = 0, scoreTick = 0;
    // setup and the step
    void Init(const Settings& s);
    void Step();
    bool Apply(const Command& c);      // validates and carries out a command (false: refused)
    float Rand();
    void Emit(int kind, Vector2 at, int a = -1, int b = -1, int c = -1) { evCount++; events.push_back({kind, at, a, b, c}); if (events.size() > 600) events.erase(events.begin(), events.begin() + 300); }
    // queries
    bool In(int x, int y) const { return x >= 0 && y >= 0 && x < W && y < H; }
    int Idx(int x, int y) const { return y * W + x; }
    int TileAt(Vector2 p) const { int x = (int)floorf(p.x), y = (int)floorf(p.y); return In(x, y) ? tile[Idx(x, y)] : T_DEEP; }
    bool Land(int t) const { return t == T_BEACH || t == T_GRASS || t == T_JUNGLE || t == T_HILL; }
    bool Water(int t) const { return t == T_DEEP || t == T_SHALLOW || t == T_KELP || t == T_CURRENT; }
    bool Passable(int x, int y, Move m, int faction) const;
    Unit* U(int id); const Unit* U(int id) const;
    Building* Bd(int id); const Building* Bd(int id) const;
    const UnitDef& UD(const Unit& u) const { return B().units[u.def]; }
    const BuildingDef& BD(const Building& b) const { return B().buildings[b.def]; }
    bool Enemies(int a, int b) const;   // players at war
    bool Allied(int a, int b) const;
    bool Sees(int player, Vector2 p) const;
    int PopCap(int player) const;
    float GatherRate(int player, const Node& n) const;
    Cost PriceOf(int player, const UnitDef& d) const;
    bool HasTech(int player, const std::string& key) const;
    float TechSum(int player, const std::string& fx) const;   // the sum of an effect over the player's techs
    int SpawnUnit(int player, int def, Vector2 p);
    int PlaceBuilding(int player, int def, int x, int y, bool built);
    bool CanPlace(int player, int def, int x, int y, std::string* why = nullptr) const;
    int NearestDrop(int player, Vector2 p, int res) const;
    bool FindPath(const Unit& u, Vector2 to, std::vector<int>& out, float near = 0.5f) const;
    void Damage(int attacker, int owner, int target, int kind, float dmg, DmgType type, uint32_t bonusTags = 0);
    void Kill(Unit& u, int by);
    void Raze(Building& b, int by);
    void UpdateFog();
    void UpdateTerritory();
    void Score();
    // pieces (fathoms.cpp)
    void StepUnit(Unit& u);
    void StepBuilding(Building& b);
    void StepPlayer(Player& p);
    void StepCombat(Unit& u);
    void StepMove(Unit& u, float speedMult);
    void StepGather(Unit& u);
    void Victory();
    // neutral powers (fathoms_neutral.cpp)
    void InitSites();
    void StepSites();
    void StepWeather();
    // factions (fathoms_faction.cpp)
    void FactionStart(Player& p);
    void FactionStep(Player& p);
    void FactionUnitStep(Unit& u);
    void FactionOnHit(Unit& attacker, Unit& victim, float dmg);
    bool FactionAbility(int player, int unit, int ability);
};

// neutral powers (fathoms_neutral.cpp) and factions (fathoms_faction.cpp): hooks the core calls
bool NeutralHostile(const World& w, const Unit& u, int other);   // would this unit fight units of that owner?
void NeutralDied(World& w, Unit& u, int by);
void NeutralDamaged(World& w, Unit& v, int by, float d);
bool NeutralCommand(World& w, const Command& c);
void SpringTrap(World& w, Node& n);
void FactionAuras(World& w);
bool HasCard(const World& w, int player, int card);
bool HasWonder(const World& w, int player);
float MaxHp(const World& w, const Unit& u);

// the map generator (fathoms_map.cpp)
void GenerateMap(World& w);
bool CheckFairness(const World& w, std::string* why);
// the AI (fathoms_ai.cpp): a player's commands for this moment
void AiThink(World& w, int player, std::vector<Command>& out);
// tests and tools (fathoms_test.cpp)
int RunFathomsTest(int stage);                 // --fathoms-test [stage]
int RunFathomsBalance();                       // --fathoms-balance: the doc's equal-cost fights
int RunFathomsSim(int matches, int players, int minutes, int ai);   // --fathoms-sim
const char* FactionName(int f);

}  // namespace fa
