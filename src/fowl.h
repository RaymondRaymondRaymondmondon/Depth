#pragma once
// Fowl Play (the Deep Arcade's eleventh game, Slop): six stalls on a gun-club porch, one marsh sky, toy guns, money,
// gambling and sabotage. The spec is "Fowl Play — Arcade Game 11 Design Document.pdf" (Reference_For_Future_MP_Games;
// OCR in docs/fowl_pdf_pages). This header is the headless core, stepped at a fixed 60 Hz: the data (data/fowl/), the
// birds and their flight, shooting and kill credit, the fun guns' projectiles, the dog, rounds, the room (the shop,
// gambling, the Slop Shop), sabotage and its counters, modes and bots. All play goes through Input and Command.
// Frame: x runs along the porch (stall 0 at the left), y is up, +z looks out over the marsh; the room is behind (z < 0).
#include "raylib.h"
#include "raymath.h"
#include <cstdint>
#include <string>
#include <vector>

namespace fp {

constexpr float STEP = 1.0f / 60;
constexpr int MAX_PLAYERS = 6;
constexpr float STALL_W = 3.0f;
constexpr int HIST = 10;                 // bird positions kept for rewinding a shot (150 ms at 60 Hz)
enum Slot { SL_SIGHT, SL_MAG, SL_BARREL, SL_TRIGGER, SL_UNDER, SL_COUNT };

// ---------------------------------------------------------------- the data
struct GunDef { std::string id, name, kind, type, note, special; int price = 0, damage = 1, pellets = 1, mag = 6, reserve = 0, ammoPrice = 0; float interval = 0.2f, reload = 1, spread = 0.5f, range = 60, kick = 1, speed = 40; bool autoFire = false; bool Fun() const { return kind == "fun"; } };
struct AttDef { std::string id, name, slot, note; int price = 0, slotIdx = -1; float spread = 1, zoom = 1, mag = 1, reload = 1, shotSpread = 1, range = 1, seek = 0, money = 1, rate = 1, steady = 1; };
struct BirdDef { std::string id, name, pattern; int from = 1, hp = 1, counts = 1, pays = 10, flock = 0, convoy = 0, split = 0, perRound = 0; float speed = 7, r = 0.4f, life = 8, rare = 1; bool armor = false, metal = false, decoy = false, angry = false, boo = false, clay = false; Color body{}, wing{}, head{}; };
struct Bracket { int from = 1, perWave = 2, waves = 6; float speed = 1, erratic = 0; std::vector<int> birds; };
struct Scratch { std::string id, name; int price = 5, odds = 3; std::vector<std::pair<std::string, float>> prizes; };
struct SlopItem { std::string id, name, kind, effect, counter, counterItem; int price = 0, counterPrice = 0; bool sabotage = false; };
struct ModeDef { std::string id, name, rule; int rounds = 15; float intermission = 45; int startMoney = -1; bool zapperOnly = false, moneyScore = false, teams = false, night = false, mystery = false, practice = false; };
struct Data {
    std::vector<GunDef> guns; std::vector<AttDef> atts; std::vector<BirdDef> birds; std::vector<Bracket> brackets;
    float hunt = 60, tally = 5, intermission = 45, bell = 10, patience = 0.05f; int rounds = 15, startMoney = 60, perfectBonus = 30, bonusEvery = 3, bonusClays = 10, goldenHour = 15; float bonusLength = 10;
    std::vector<int> slotBets; std::vector<int> slotWeights; float slotPull = 2; int threeBell = 12, threeZappa = 30;
    std::vector<Scratch> scratch; float scratchTime = 3; int mysteryPrice = 100;
    std::vector<SlopItem> cosmetics, sabotage; int perTarget = 2; float leaderMarkup = 0.5f;
    std::vector<ModeDef> modes; int tokMatch = 10, tokPerBirds = 5, tokWin = 25, tokUfo = 50, tokPerfect = 20;
};
const Data& D();
int GunIndex(const std::string& id);
int AttIndex(const std::string& id);
int BirdIndex(const std::string& id);
int SabIndex(const std::string& id);
int ModeIndex(const std::string& id);

// ---------------------------------------------------------------- guns in hand
struct Gun {
    int def = -1; int att[SL_COUNT] = {-1, -1, -1, -1, -1}; int ammo = 0, reserve = 0; float cool = 0, reloadT = 0, spin = 0; int shotN = 0; int paint = 0;
    bool Has() const { return def >= 0; }
    int MagSize() const;
    float Spread() const;       // degrees, with the attachments
    float Range() const;
    float Interval() const;
    float ReloadTime() const;
    float MoneyMul() const;
    float Seek() const;
};

Gun MakeGun(int def);   // a fresh gun, loaded

// ---------------------------------------------------------------- input and commands
struct Input {
    float yaw = 0, pitch = 0;           // where you look (absolute)
    float moveX = 0, moveZ = 0;         // the wish: x along the porch / room, z toward the marsh (world axes)
    bool fire = false, alt = false;     // the trigger held; the second action held (scope, both barrels, the duck call)
    bool reload = false, swap = false, use = false, hook = false, wipe = false, crouch = false;   // presses (wipe: held)
    float lean = 0;                     // -1 .. 1
    int lagSteps = 0;                   // how far behind the host this client sees the birds (the rewind)
};
enum CommandKind : uint8_t { CMD_NONE, CMD_BUY_GUN, CMD_BUY_ATT, CMD_BUY_AMMO, CMD_MYSTERY, CMD_SLOT, CMD_SCRATCH, CMD_SCRATCH_OPEN, CMD_COSMETIC, CMD_SABOTAGE, CMD_COUNTER, CMD_TAKE_FLOOR, CMD_HOOK_SWAP, CMD_DEAL, CMD_NAME };
struct Command { uint8_t kind = CMD_NONE; int a = 0, b = 0; std::string s; };

// ---------------------------------------------------------------- the world
enum Phase : uint8_t { PH_LOBBY, PH_HUNT, PH_BONUS, PH_TALLY, PH_INTER, PH_OVER };
enum BirdState : uint8_t { BI_FLY, BI_HIT, BI_FALL, BI_GONE };
struct Bird {
    int def = 0, id = 0, wave = 0; BirdState st = BI_FLY; Vector3 p{}, v{}; float t = 0, life = 8, hp = 1, freezeT = 0, seed = 0;
    int killer = -1, gunOf = -1; bool escaped = false, retrieved = false, golden = false;
    // effects
    float breadT = 0, bubbleT = 0, clampT = 0, honkT = 0, abductT = 0, angryT = 0, blowT = 0; int breadBy = -1, bubbleBy = -1, clampBy = -1, honkTo = -1, plungers = 0, plungeBy = -1, leader = -1, flock = -1, angryAt = -1;
    bool decoyRain = false; int sideOf = -1;       // (a sabotage decoy over one player's half)
    Vector3 hist[HIST]; int histN = 0;
    Vector3 PosAgo(int steps) const;
};
enum ProjKind : uint8_t { PJ_CRAB, PJ_BEAM, PJ_NET, PJ_BREAD, PJ_BOOMERANG, PJ_BUBBLE, PJ_FIREWORK, PJ_PLUNGER, PJ_BANANA, PJ_BANANA_BIT, PJ_FLARE };
struct Proj { uint8_t kind = 0; int owner = -1; Vector3 p{}, v{}; float t = 0; int state = 0; bool alive = true; std::vector<int> hit; };
struct Sabotage { int item = -1, by = -1; bool countered = false; };
struct Player {
    int id = 0, stall = 0, team = 0; bool present = true, bot = false; std::string name;
    Vector3 pos{}; float yaw = 0, pitch = 0, crouchK = 0, lean = 0; int room = 0;   // room: 0 in the stall, 1 walking the room
    Gun guns[2]; int hand = 0; Gun hook; bool lockbox = false;
    int money = 0, birds = 0, shots = 0, hits = 0; int roundBirds = 0, roundMoney = 0, roundSpent = 0; float roundPay = 0;
    int grudgeMoney = 0; std::vector<int> best;   // (a grudged hunt's money, halved off at the end)
    // cosmetics (match-only) and counters owned
    int hat = -1, paint = 0, dance = -1, flag = -1, dogCoat = -1, killSound = -1, tracer = -1; bool hatOff = false; float hatOffT = 0;
    uint32_t counters = 0;                          // bits: a counter item bought for the next hunt (by sabotage index)
    std::vector<Sabotage> incoming;                 // sabotage waiting for the next hunt
    std::vector<Sabotage> active;                   // this hunt's
    float beesT = 0, smudgeT = 0, wipeT = 0, reverseT = 0, pepperT = 0, repelT = 0, cardboardT = 0, gunDropT = 0, jamT = 0; int cardboardHP = 0, dropsLeft = 0, rubberLeft = 0; float nextDropT = 0;
    bool bagpipe = false, grudge = false, glitter = false; int taxBy = -1, swapWith = -1; bool menace = false; uint32_t sabotaged = 0;
    bool dogSausage = false; int dogFirst = 0; bool dogShot = false;   // (the Dog's Bone: retrieves first and sniffs out a golden duck; shooting the dog: retrieved last)
    int scratchPocket[8] = {}; int scratchOpen = -1; float scratchT = 0;   // tickets in the pocket, the one being scratched
    float slotT = 0; int slotBet = 0; int reels[3] = {0, 0, 0}; int slotWin = 0;   // the pull in progress / the last result
    int deal = -1; bool dealIsAtt = false; bool boughtThisInter = false, mysteryFree = false; int pendingCapsule = -1;
    int perfect = 0, ufoKills = 0; int mostExpensive = -1, mostExpensiveBirds = 0; int gambleWon = 0, gambleLost = 0, sabGiven = 0, sabTaken = 0;
    Gun saved[2]; int savedHand = 0; bool inBonus = false, lastFire = false;
    int botTarget = -1; float botReact = 0, botErrT = 0; Vector2 botErr{}; std::vector<fp::Command> botPlan; std::vector<int> botPlanAt; bool botPlanned = false; int botPlanRound = -1;   // (a bot's mind; host only)   // (the bonus wave lends everyone the grey pistol)
    Input in; std::vector<Command> cmds;
    Gun& G() { return guns[hand]; }
    const Gun& G() const { return guns[hand]; }
    Vector3 Eye() const { return {pos.x + lean * 0.35f, pos.y + 1.62f - crouchK * 0.5f, pos.z}; }
    Vector3 Look() const { return {cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw)}; }   // (yaw 0 looks +z, over the marsh)
};
struct Dog { int state = 0; Vector3 p{0, 0, 9}; float t = 0; int holding = -1, laughs = 0; std::vector<int> queue; };   // 0 hidden, 1 flush, 2 retrieve, 3 laugh, 4 hold
struct FloorGun { Gun g; Vector3 p{}; };
struct Event { int kind; Vector3 at; int who, by; float a; };
enum EventKind { EV_SHOT = 1, EV_HIT, EV_KILL, EV_ARMOR, EV_ESCAPE, EV_WAVE, EV_DOG_LAUGH, EV_DOG_SHOT, EV_TALLY, EV_PERFECT, EV_BELL, EV_JACKPOT, EV_SLOT, EV_SCRATCH, EV_MYSTERY, EV_BUY, EV_SABOTAGE, EV_COUNTER, EV_JAM, EV_HATOFF, EV_BOO, EV_DROP, EV_RELOAD, EV_FLASH, EV_HONK, EV_FLARE, EV_ROUND, EV_BONUS, EV_UFO, EV_FREE, EV_DECOY, EV_CHAIN };
struct Stats { int match = 0; };

struct World {
    std::vector<Player> players; std::vector<Bird> birds; std::vector<Proj> projs; std::vector<FloorGun> floor; Dog dog; std::vector<Event> events; uint32_t evCount = 0;
    Phase phase = PH_LOBBY; float phaseT = 0, t = 0; int round = 0, wave = 0, wavesThisRound = 0; float nextWaveT = 0; int nextBirdId = 1; uint32_t rng = 1;
    int mode = 0; int roundsTotal = 15; float interLen = 45; bool ufoDone = false, goldenDone = false; float flareT = 0; int nightRound = 0;
    std::vector<int> waveKills; std::vector<int> waveBirds; std::vector<int> waveCredit;   // per wave: birds, kills, -2 none yet / -1 shared / player
    int champion = -1;
    // setup and the clock
    void Init(int humans, int bots, int modeIdx, uint32_t seed);
    void Step();
    void Emit(int kind, Vector3 at, int who = -1, int by = -1, float a = 0) { evCount++; events.push_back({kind, at, who, by, a}); if (events.size() > 400) events.erase(events.begin(), events.begin() + 200); }
    float Rand();
    int RandI(int n) { return std::min(n - 1, (int)(Rand() * n)); }
    const ModeDef& M() const;
    const Bracket& Br() const;
    // phases
    void BeginHunt(); void BeginBonus(); void BeginTally(); void BeginInter(); void EndMatch();
    void SpawnWave(); Bird& SpawnBird(int def, Vector3 at, int wave);
    void StepBirds(); void StepBird(Bird& b); void KillBird(Bird& b, int by, int gun, bool freeze = true);
    void StepDog(); void StepProjs(); void StepPlayer(Player& p); void StepGun(Player& p);
    void Fire(Player& p, bool both);
    bool RayBird(Vector3 o, Vector3 d, float maxT, int rewind, int* bird, float* tHit, Vector3* at) const;
    void DamageBird(Bird& b, int dmg, int by, int gun, Vector3 at);
    void ApplySabotage(Player& p); void StepSabotage(Player& p);
    void Command(Player& p, const fp::Command& c);
    void GiveGun(Player& p, const Gun& g);
    bool CanBuy(const Player& p, int station) const;   // (in the intermission, standing at it)
    int SabPrice(int item, int target) const;
    int Leader() const;
    int Score(const Player& p) const;   // birds (money in High Roller)
    // the room's stations (where to stand to use them)
    static Vector3 Station(int k);      // 0 gun counter, 1 mystery machine, 2 slots, 3 scratchers, 4 Slop Shop, 5 trophy wall, 6 bar
    int NearStation(const Player& p) const;
    Vector3 StallPos(int stall) const { return {(stall - 2.5f) * STALL_W, 0, 0}; }
};

// gambling (headless, for the house-edge check)
void SlotSpin(uint32_t& rng, int out[3]);
int SlotPayout(const int reels[3], int bet, int* gunOut, uint32_t& rng);   // cash back (gunOut: a gun won, or -1)
float SlotEV(int bet, int pulls, uint32_t seed);

void BotInput(World& w, int me, Input& in, std::vector<Command>& cmds, uint32_t& rng, int skill);
int RunFowlTest();
int RunFowlSim(int matches, int players, int skill);
int RunFowlGambleSim(int pulls);

}  // namespace fp
