#pragma once
// NOCLIP (the Deep Arcade's tenth game; on the Action reel by the user's call): first-person co-op scavenging in the
// Backrooms. The spec is "NOCLIP — Arcade Game 10 Design Document.pdf" (Reference_For_Future_MP_Games; OCR in
// docs/noclip_pdf_pages). This header is the headless core: the data (data/noclip/), the twenty levels' generators,
// the crew (health, sanity, stamina, injuries, carrying), loot, Threshold Labs and their network, the portal, the
// clock and Overtime, entities, the Surface (the Bureau, the Fence, the quota, contracts) and the bots.
// All play goes through Input and Command. Levels are grids of 2.5 m cells; x and z are metres, y is up.
#include "raylib.h"
#include "raymath.h"
#include <cstdint>
#include <string>
#include <vector>
#include <map>

namespace nc {

constexpr float STEP = 1.0f / 30;          // the simulation's fixed step
constexpr float CELL = 2.5f;               // metres a cell
constexpr int MAX_CREW = 6;
constexpr int LEVELS = 20;

// ---------------------------------------------------------------- data
struct ExitDef { int to = 0; std::string kind, label; float chance = 1; };
struct LevelDef {
    int id = 0; std::string name, kit, mood; int w = 60, h = 60; float ceil = 2.6f;
    Color wall{}, floor{}, top{}, light{}; float lit = 0.9f, flicker = 0.05f; std::string tex[3];
    std::vector<std::pair<std::string, float>> entities; float budget = 2;
    int lootBand = 0; std::vector<std::string> labs; bool startOnline = false, compass = false;
    std::vector<ExitDef> exits;
};
struct LootDef { std::string name, size, props, note; int min = 5, max = 40; int level = 0; bool anomalous = false; std::string effect; };
struct ItemDef { std::string id, name, use, note; int price = 10, charges = 1; };
struct SuitDef { std::string id, name, note; int price = 100; };
struct EntityDef { std::string id, name, hunts, behaviour, tell, counter; float walk = 1, chase = 4; int damage = 20, pack = 1, bounty = 50; };
struct ContractDef { std::string id, name, task; int creditMin = 0, creditMax = 0, cash = 0; };
struct CosDef { std::string id, name, kind; int tier = 0; bool glow = false; };
struct Data {
    std::vector<LevelDef> levels; std::vector<LootDef> loot; std::vector<ItemDef> items; std::vector<SuitDef> suits, labUps;
    std::vector<EntityDef> entities; std::vector<ContractDef> contracts; std::vector<CosDef> cosmetics;
    std::vector<float> labPriceMul; float anomalousChance = 0.02f;
    int week1 = 600, lateFrom = 5, daysPerWeek = 3, startCash = 120; float growth = 0.4f, growthLate = 0.35f, bureauCash = 0.2f, deathFee = 0.15f;
    float dayStart = 360, dayEnd = 1440, dayReal = 900, evening = 1080;
    std::vector<std::pair<std::string, int>> ranks; int tokDay = 10, tokQuota = 50, tokEntity = 25; int cosPrice[4] = {15, 30, 60, 0}; int cratePrice = 1; int crateW[4] = {60, 28, 10, 2};
};
const Data& D();
int ItemIndex(const std::string& id);
int EntityIndex(const std::string& id);
int ContractIndex(const std::string& id);

// ---------------------------------------------------------------- a level
enum Tile : uint8_t { T_VOID, T_FLOOR, T_WALL, T_LOW, T_GLASS, T_WATER, T_DEEP, T_PIT, T_DOOR, T_LABWALL, T_LABFLOOR, T_BLAST, T_WHEAT, T_EDGE, T_COUNT };
// a cell's extra marks (bits)
enum CellFlag : uint16_t { CF_STAIN = 1, CF_WINDOW = 2, CF_PIPES = 4, CF_STEAM = 8, CF_LIVE = 16, CF_WEB = 32, CF_DEADLIGHT = 64, CF_MANILA = 128, CF_HOT = 256, CF_COLD = 512, CF_STREETLIGHT = 1024, CF_CELL = 2048, CF_MIRROR = 4096, CF_ROOM = 8192 };
struct Exit { int cx = 0, cz = 0, to = 0; std::string kind, label; bool noclip = false; };
enum LabPart { LP_RING, LP_DESK, LP_CRATE, LP_SHOP, LP_BUNK, LP_GEN, LP_MONITORS, LP_ARCHIVE, LP_COUNT };
struct LabSpot { int part; Vector3 at; };
struct LabPlan { std::string name; int x0 = 0, z0 = 0, x1 = 0, z1 = 0; int doorX = 0, doorZ = 0; std::vector<LabSpot> spots; int breakerX = 0, breakerZ = 0; };
struct LootSpot { Vector3 at; int def; };
struct Level {
    int id = 0; uint32_t seed = 0; int w = 0, h = 0;
    std::vector<uint8_t> tile; std::vector<uint16_t> flags; std::vector<uint8_t> light;   // light: 0 dead, 1..255 brightness (flicker in the high bit range: see LightOf)
    std::vector<Exit> exits; std::vector<LabPlan> labs; std::vector<LootSpot> loot; std::vector<Vector3> spawns;
    Vector3 start{};                     // where a crew lands if there's no Lab (a noclip in)
    uint8_t At(int x, int z) const { return x < 0 || z < 0 || x >= w || z >= h ? T_VOID : tile[z * w + x]; }
    uint16_t Flags(int x, int z) const { return x < 0 || z < 0 || x >= w || z >= h ? 0 : flags[z * w + x]; }
    bool Solid(int x, int z) const { uint8_t t = At(x, z); return t == T_VOID || t == T_WALL || t == T_GLASS || t == T_LABWALL || t == T_LOW; }
    bool SeeThrough(int x, int z) const { uint8_t t = At(x, z); return !(t == T_VOID || t == T_WALL || t == T_LABWALL); }   // (glass and low partitions don't block sight)
    bool Walkable(int x, int z) const { uint8_t t = At(x, z); return t == T_FLOOR || t == T_WATER || t == T_DEEP || t == T_DOOR || t == T_LABFLOOR || t == T_BLAST || t == T_WHEAT || t == T_EDGE || t == T_PIT; }
    int CellX(float x) const { return (int)floorf(x / CELL); }
    int CellZ(float z) const { return (int)floorf(z / CELL); }
    Vector3 Center(int x, int z) const { return {(x + 0.5f) * CELL, 0, (z + 0.5f) * CELL}; }
};
Level Generate(int level, uint32_t daySeed);           // the level's own generator (noclip_gen.cpp)
bool CheckReachable(const Level& L, std::string* why); // every Lab reaches every exit (doors and blast doors pass)
std::string DescribeLevel(const Level& L);             // ASCII (--noclip-gen)

// ---------------------------------------------------------------- the crew and their things
enum Size : uint8_t { SZ_S, SZ_M, SZ_L, SZ_H };
struct Loot { int def = -1; int value = 0; bool damaged = false; int foundOn = 0; uint32_t uid = 0; };
struct WorldItem { Loot loot; int level = 0; Vector3 p{}; float noiseT = 0; };   // loot lying in a level
struct Tool { int item = -1; int charges = 0; float fuel = 0; };                  // fuel: seconds of light (flashlight, glow stick) or of use
enum Injury : uint8_t { IN_SPRAIN = 1, IN_BLEED = 2, IN_BREAK = 4 };
enum PState : uint8_t { PS_SURFACE, PS_ALIVE, PS_DOWNED, PS_DEAD, PS_TAKEN };
struct Input {
    float yaw = 0, pitch = 0, moveX = 0, moveZ = 0;    // the wish in the player's frame (x forward, z right)
    bool sprint = false, crouch = false, use = false, useHeld = false, drop = false, throwIt = false, lamp = false, primary = false, primaryHeld = false;
    int slot = -1;                                     // select a tool slot (presses)
};
enum CmdKind : uint8_t { C_NONE, C_BUY, C_BUY_SUIT, C_BUY_UPGRADE, C_SELL_BUREAU, C_SELL_FENCE, C_SELL_ALL, C_CONTRACT, C_INSERT, C_START_DAY, C_PORTAL, C_PORTAL_PAUSE, C_JUMP, C_LAB_BUY, C_SLEEP, C_LOCK, C_SUMMON, C_TRADE, C_ACCEPT, C_DECLINE, C_NAME, C_READY, C_GRAB };
struct Command { uint8_t kind = C_NONE; int a = 0, b = 0; std::string s; };
struct Player {
    int id = 0; bool present = true, bot = false; std::string name;
    PState st = PS_SURFACE; int level = 0; Vector3 p{}; float yaw = 0, pitch = 0; Vector3 vel{};
    float health = 100, sanity = 100, stamina = 100; uint8_t injuries = 0; float bleedT = 0, downT = 0, stunT = 0, reviveT = 0; int revivedBy = -1;
    Tool tools[5]; int toolSlots = 4, sel = 0; Loot pocket[2]; Loot hands; int carryWith = -1;   // carryWith: a partner on a huge item
    bool lamp = true; float battery = 600; float lampFlicker = 0;
    float aloneT = 0, voiceT = 0, labLightT = 0, windowT = 0, edgeT = 0, swimT = 0, breath = 0, tankAir = 0, flaskT = 0;
    float lostT = 0; int lostTarget = -1; float blackoutT = 0, jumpCool = 0, sleptDay = -1; int noclipCount = 0;
    bool crouched = false; float noise = 0;            // the noise this player is making (entities hear it)
    int deaths = 0; std::string lastCause; int broughtValue = 0; int photos = 0;
    uint32_t suits = 0;                                // bits: suits owned (by index)
    int hat = -1, vest = -1, lamp_c = -1, costume = -1, suitCos = -1;   // cosmetics
    Input in; std::vector<Command> cmds;
    // bots
    int botGoal = -1; Vector3 botTarget{}; float botThink = 0; std::vector<int> botPath; int botMode = 0;
    bool Alive() const { return st == PS_ALIVE || st == PS_DOWNED; }
    float Height() const { return crouched ? 1.0f : 1.65f; }
    Vector3 Eye() const { return {p.x, p.y + Height(), p.z}; }
    Vector3 Look() const { return {cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw)}; }
    float SpeedMul() const;
};
// a Lab's state (permanent for the campaign, per level and index)
struct LabState {
    int level = 0, idx = 0; bool online = false, doorOpen = false, locked = false; float fuel = 0;   // fuel: in-game days left
    std::vector<Loot> crate; float charge = 0; bool charging = false; float openT = 0, cooldown = 0; int jumpFor = -1; float jumpT = 0; int jumpTo = -1;
    uint32_t upgrades = 0;
};
// an entity
enum EState : uint8_t { ES_IDLE, ES_WANDER, ES_STALK, ES_CHASE, ES_ATTACK, ES_FLEE, ES_STUN, ES_HIDDEN, ES_TAKE, ES_GONE };
struct Entity {
    int def = 0; uint32_t uid = 0; int level = 0; Vector3 p{}, v{}; float yaw = 0; EState st = ES_WANDER; float t = 0, stunT = 0, cool = 0, memT = 0;
    int target = -1; Vector3 goal{}; Vector3 lastHeard{}; bool heard = false; int mimicOf = -1; float watchT = 0; int carrying = -1; std::vector<int> path;
    bool hallucination = false;                       // (client-side only)
};
struct Event { int kind; int who, by; int level; Vector3 at; float a; std::string s; };
enum EventKind { E_PICKUP = 1, E_DROP, E_HURT, E_DOWNED, E_DIED, E_REVIVED, E_TAKEN, E_NOCLIP, E_EXIT, E_LAB_ONLINE, E_PORTAL_START, E_PORTAL_OPEN, E_EXTRACT, E_JUMP, E_ENTITY_SEEN, E_PHOTO, E_TELL, E_HOWL, E_SHOT, E_SANITY, E_LOST, E_OVERTIME, E_DAY_END, E_QUOTA, E_SALE, E_BREACH, E_CRATE, E_TRADE, E_INVITE, E_PAGE, E_LOCKDOWN, E_STAY, E_MEMO, E_WAKE, E_SHUTDOWN };

// the Surface's ledger for a day
struct Sale { std::string what; int value = 0; bool fence = false; int level = 0; };
struct World {
    std::vector<Player> crew; std::map<int, Level> levels; std::vector<LabState> labs; std::vector<WorldItem> items; std::vector<Entity> ents;
    std::vector<Event> events; uint32_t evCount = 0; uint32_t rng = 1, nextUid = 1;
    // the campaign
    int mode = 0, week = 1, day = 1; int quota = 600, credit = 0, cash = 120; bool failed = false; int weeksSurvived = 0;
    int contract = -1; int contractTarget = 0, contractLevel = 0; bool contractDone = false; std::vector<int> forecast; std::vector<int> fenceRate;   // per category, percent
    std::vector<Sale> sold; std::vector<Loot> bay;      // the loading bay: today's extracted loot, waiting to be sold
    uint32_t dossier = 0;                               // bits: entities photographed (by index)
    std::vector<uint32_t> seen;                         // per level, a cell bitmap of what the crew has seen (the map), packed
    std::vector<std::string> learned;                   // exits learned, as "level:label"
    // the day
    bool inDay = false; float clock = 360; bool overtime = false; float hourT = 0; int insertion = 0; uint32_t daySeed = 1;
    float lightsOutT = 0; float lockdownT = 0, lockdownNext = 0;   // (Level 1's lights-out; Level 16's lockdowns)
    std::string memo;
    bool mirror = false;              // a guest's copy: levels are generated for drawing, but no loot is rolled (the host's comes in the snapshot)
    // setup and the clock
    void Init(int humans, int bots, int mode, uint32_t seed);
    void Step();
    float Rand();
    int RandI(int n) { return n <= 0 ? 0 : std::min(n - 1, (int)(Rand() * n)); }
    void Emit(int kind, int who = -1, int by = -1, int level = -1, Vector3 at = {}, float a = 0, const std::string& s = "") { evCount++; events.push_back({kind, who, by, level, at, a, s}); if (events.size() > 300) events.erase(events.begin(), events.begin() + 150); }
    // the day's shape
    void BeginDay();                 // insertion: the crew lands at the chosen Lab
    void EndDay(bool extracted);     // the Surface: the bay is ready to sell; the next day's forecast and contract
    void NextWeek();
    Level& L(int level);             // generated on first use each day
    LabState* Lab(int level, int idx);
    LabState* LabAt(int level, Vector3 p);            // the Lab whose rooms contain p
    bool InPortalHall(const Player& pl, const LabState& lab);
    // the crew
    void StepPlayer(Player& p); void StepMeters(Player& p); void Move(Player& p, Vector3 wish, float speed);
    void Hurt(Player& p, float dmg, const std::string& cause, int injury = 0);
    void Down(Player& p, const std::string& cause); void Kill(Player& p, const std::string& cause);
    void UseTool(Player& p); void Interact(Player& p); void Drop(Player& p, bool throwIt);
    bool Give(Player& p, const Loot& l);             // into the pocket or the hands; false if there's no room
    void Transit(Player& p, int to, bool noclip, const std::string& how);   // to another level
    void Command(Player& p, const nc::Command& c);
    int CrateTotal() const;
    int Nearest(int level, Vector3 at, float range, int except = -1) const;   // the nearest living crew member
    // entities
    void StepEntities(); void SpawnEntities(float dt); void StepEntity(Entity& e); void ToolOnEntities(Player& p, const std::string& use);
    void SeeMap(const Player& p); float SeenFraction(int level) const; std::vector<std::vector<uint8_t>> seenCells;   // (the shared field map)
    // labs
    void StepLabs(); void Extract(LabState& lab);
    float Noise(int level, Vector3 at) const;         // how loud it is here (players, the portal, items)
    // helpers
    bool LineOfSight(int level, Vector3 a, Vector3 b) const;
    float LightAt(int level, Vector3 at) const;       // 0 dark .. 1 lit (cells' lights, lamps, flares)
    std::vector<int> Path(int level, Vector3 from, Vector3 to, int maxNodes = 4000) const;
    std::string ClockText() const;
    int QuotaFor(int week) const;
};

void BotInput(World& w, int me, Input& in, std::vector<Command>& cmds, uint32_t& rng);
int RunNoclipTest();
int RunNoclipGen(int level, uint32_t seed);
int RunNoclipSim(int crew, int days, int runs);

}  // namespace nc
