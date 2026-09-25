// ============================================================================
//  DEPTH - shared header
//  Everything the different parts of the game need to know about each other.
// ============================================================================
#pragma once
#include "raylib.h"
#include <array>
#include <string>
#include <vector>

constexpr int SCREEN_W   = 1280;
constexpr int SCREEN_H   = 720;
constexpr int MAX_ROSTER = 8;
constexpr int PARTY_SIZE = 4;

// ---------- Palette: a brighter take on Darkest Dungeon ----------
namespace Pal {
const Color Sea     = {18, 70, 92, 255};
const Color SeaDeep = {10, 38, 58, 255};
const Color Brass   = {214, 166, 82, 255};
const Color BrassDk = {135, 96, 40, 255};
const Color Copper  = {184, 98, 58, 255};
const Color Paper   = {240, 228, 200, 255};
const Color Ink     = {40, 30, 24, 255};
const Color Teal    = {64, 196, 190, 255};
const Color Coral   = {240, 110, 90, 255};
const Color Good    = {110, 210, 120, 255};
const Color Bad     = {225, 70, 70, 255};
const Color Stress  = {170, 120, 230, 255};
}  // namespace Pal

enum class Scene { Hub, Helm, Crew, Radar, Ward, SickLeave, Bookshelf, Periscope, Dungeon, Platformer };

// Rank masks. Bit 0 = rank 1 (front line), bit 3 = rank 4 (back line).
constexpr int RANK_1 = 1, RANK_2 = 2, RANK_3 = 4, RANK_4 = 8;
constexpr int MELEE_FROM = RANK_1 | RANK_2;           // melee: must stand in the front two ranks
constexpr int MELEE_HITS = RANK_1 | RANK_2;           // ...and can only reach the front two enemies
constexpr int RANGED_FROM = RANK_2 | RANK_3 | RANK_4; // ranged: anywhere except the very front
constexpr int ANY_RANK = RANK_1 | RANK_2 | RANK_3 | RANK_4;

enum class HeroClass { Nurse, Diver, Captain, Mechanic, COUNT };
enum class Target { Enemy, Ally, Self, AllAllies };

struct Ability {
    std::string name, desc;
    int usableFrom = ANY_RANK;
    int hits = ANY_RANK;
    Target target = Target::Enemy;
    float dmgMult = 0.0f;        // 0 = deals no damage
    int accBonus = 0;
    int hitsCount = 1;           // strikes per use
    bool aoe = false;            // hits every rank in `hits`
    int heal = 0;
    bool cure = false;           // removes bleed and poison
    int stressHeal = 0;
    int stunChance = 0;          // percent
    int bleed = 0;               // damage per turn, 3 turns
    int poison = 0;              // damage per turn, 3 turns
    int buffDmg = 0;             // +% damage for a few turns
    int guardTurns = 0;          // taunt + extra protection
    int moveTarget = 0;          // + pushes an enemy back, - pulls it forward
    bool swapWithTarget = false; // "command team": trade places with an ally
};

struct Status {
    int bleedDmg = 0, bleedTurns = 0;
    int poisonDmg = 0, poisonTurns = 0;
    int stunned = 0;
    int buffDmg = 0, buffTurns = 0;
    int guardTurns = 0;
};

struct RelicDef {
    std::string name, desc;
    int hp = 0, dmg = 0, speed = 0, acc = 0, dodge = 0, prot = 0, stressResist = 0;
    int price = 80;
};

struct Stats { int maxHp, dmgMin, dmgMax, speed, acc, dodge, prot, stressResist; };

struct Hero {
    int id = 0;
    std::string name;
    HeroClass cls = HeroClass::Nurse;
    int level = 0, xp = 0;
    int hp = 1;
    int stress = 0;          // "Nerves", 0-100
    bool rattled = false;    // hit 100 stress; cured at Sick Leave
    bool deathsDoor = false; // 0 HP: the next hit might be fatal
    bool dead = false;
    int onLeave = 0;         // expeditions left to sit out
    int relics[2] = {-1, -1};
    Status st;
};

struct EnemyAbility {
    std::string name;
    int hits = ANY_RANK;
    bool aoe = false;
    float dmgMult = 1.0f;
    int stress = 0, stunChance = 0, bleed = 0, poison = 0;
};

enum class EnemyType { SeaLouse, CaveShrimp, BrineWorm, Lobster };

struct Enemy {
    int uid = 0;
    EnemyType type = EnemyType::SeaLouse;
    std::string name;
    int maxHp = 1, hp = 1, dmgMin = 1, dmgMax = 1, speed = 0, acc = 80, dodge = 0, prot = 0;
    bool boss = false;
    bool alive = true;
    std::vector<EnemyAbility> abilities;
    Status st;
};

struct FloatText { Vector2 pos; std::string text; Color color; float life; };
struct TurnEntry { bool hero; int id; int init; };

enum class RoomType { Fight, Treasure, Boss };
enum class DPhase { Corridor, Combat, RoomClear, Treasure, Victory, Retreat, Defeat };

struct DungeonState {
    std::vector<RoomType> rooms;
    int roomIndex = -1;
    float light = 100;
    int lootGold = 0;
    std::vector<int> lootRelics;
    int rewardRelic = -1;
    int roomGold = 0, roomRelic = -1;
    DPhase phase = DPhase::Corridor;
    bool resultsApplied = false;
    std::vector<Enemy> enemies;
    int nextUid = 1;
    std::vector<TurnEntry> order;
    int turnIdx = 0, round = 0;
    bool turnStarted = false, pendingSkip = false;
    float actTimer = 0;
    int selectedAbility = -1;
    std::vector<std::string> log;
    std::vector<FloatText> floats;
};

struct Rat { Vector2 pos; float dir; float vy; bool alive; };

struct PlatformState {
    std::vector<int> pipesLayout; // which chunks make up the current Pipes layout
    std::string layoutCode;
    std::vector<std::string> tiles;
    int w = 0, h = 0;
    Vector2 pos{0, 0}, vel{0, 0};
    bool onGround = false, facingRight = true;
    float coyote = 0, jumpBuffer = 0;
    int checkpointChunk = 0;
    std::vector<Rat> rats;
    int coins = 0, deaths = 0, reward = 0;
    float time = 0, deathFlash = 0;
    bool finished = false;
};

struct Game {
    Scene scene = Scene::Hub;
    int gold = 300;
    int batteries = 2;
    std::vector<Hero> roster;
    std::array<int, PARTY_SIZE> party{{-1, -1, -1, -1}}; // hero ids, rank 1 first
    std::vector<int> relicStorage;
    std::vector<Hero> recruits;
    std::vector<int> shopRelics;
    int nextHeroId = 1;
    int selectedHero = -1;
    int dismissArmed = -1;
    int bookTab = 0;
    int relicScroll = 0;
    std::string toast;
    float toastTimer = 0;
    float time = 0;
    DungeonState dungeon;
    PlatformState plat;
};

// ---------- data.cpp ----------
void InitGame(Game& g);
const std::vector<Ability>& ClassAbilities(HeroClass c);
const char* ClassName(HeroClass c);
const char* ClassBlurb(HeroClass c);
Color ClassColor(HeroClass c);
Hero MakeHero(Game& g, HeroClass c);
Hero MakeRandomHero(Game& g);
const std::vector<RelicDef>& Relics();
Stats GetStats(const Hero& h);
Enemy MakeEnemy(EnemyType t, int uid);
void GiveXP(Game& g, Hero& h, int amount);
int XpForNextLevel(const Hero& h);
Hero* FindHero(Game& g, int id);
bool InParty(const Game& g, int id);
void CompactParty(Game& g);
void RefreshRadar(Game& g);

// ---------- ui.cpp ----------
void Txt(const std::string& s, float x, float y, int size, Color c);
void DrawTextCentered(const std::string& s, float cx, float y, int size, Color c);
bool Button(Rectangle r, const char* text, bool enabled = true, int fontSize = 20);
void Panel(Rectangle r, Color fill = Pal::Paper);
void DrawWrapped(const std::string& text, Rectangle r, int size, Color c);
void DrawBar(Rectangle r, float frac, Color fill);
void Toast(Game& g, const std::string& msg);
void DrawToast(Game& g);
bool BackButton(Game& g);
void DrawSceneTitle(const char* title, const char* subtitle);
void DrawGoldBadge(const Game& g);
void DrawCabinBackground();
std::string RankString(int mask);

// ---------- hub.cpp ----------
void SceneHub(Game& g);
void SceneHelm(Game& g);
void SceneCrew(Game& g);
void SceneRadar(Game& g);
void SceneWard(Game& g);
void SceneSickLeave(Game& g);
void SceneBookshelf(Game& g);
void ScenePeriscope(Game& g);

// ---------- dungeon.cpp ----------
void StartDungeon(Game& g);
void SceneDungeon(Game& g);

// ---------- platformer.cpp ----------
void GeneratePipesLayout(Game& g);
std::string PipesLayoutCode(const Game& g);
void StartPipes(Game& g);
void ScenePlatformer(Game& g);
