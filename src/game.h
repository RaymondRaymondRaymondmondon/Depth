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
constexpr int PARTY_SIZE = 4;
constexpr int LOADOUT_SIZE = 4; // abilities a hero brings on an expedition (out of 8)

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

enum class Scene { Hub, Helm, Crew, Radar, Ward, SickLeave, Bookshelf, Periscope, Workshop, Dungeon, Platformer };

// Workshop upgrades. Each has levels 0..UPGRADE_MAX.
enum Upgrade { UP_REFLECTOR, UP_BUNKS, UP_SONAR, UP_INFIRMARY, UP_COUNT };
constexpr int UPGRADE_MAX = 3;

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
    int buffDodge = 0;           // +dodge for a few turns
    int buffProt = 0;            // +protection for a few turns
    int guardTurns = 0;          // taunt + extra protection
    bool mark = false;           // marked enemies take +25% damage for 3 turns
    int moveTarget = 0;          // + pushes an enemy back, - pulls it forward
    bool swapWithTarget = false; // "command team": trade places with an ally
    bool ranged = false;         // thrown or fired (animates with a projectile) rather than a close-in strike
    int unlockLevel = 0;         // hero level needed before it can be slotted
};

struct Status {
    int bleedDmg = 0, bleedTurns = 0;
    int poisonDmg = 0, poisonTurns = 0; // poison stacks, and halves healing received
    int stunned = 0;
    int buffDmg = 0, buffTurns = 0;
    int dodgeBuff = 0, dodgeTurns = 0;
    int protBuff = 0, protTurns = 0;
    int guardTurns = 0;
    int marked = 0;
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
    int loadout[LOADOUT_SIZE] = {0, 1, 2, 3}; // indices into ClassAbilities, -1 = empty slot
    int outfit = -1;         // >= 0 for the Nautilus's own hands (see NpcOutfit): a uniform instead of class gear
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
enum class DPhase { Corridor, Walking, Combat, RoomClear, Treasure, Victory, Retreat, Defeat };

// Dungeon difficulty levels. Clearing one unlocks the next; earlier ones stay available.
constexpr int CAVE_TIERS = 5;
inline constexpr int CAVE_TIER_LEVEL[CAVE_TIERS] = {0, 1, 3, 5, 6};
inline const char* const CAVE_TIER_NAME[CAVE_TIERS] = {"Shallows", "Tidal Caves", "The Deep", "The Abyss", "The Trench"};

// ---------- combat animation ----------
enum class Anim { None, Melee, Ranged, Heal, Buff, Hurt, Dodge, Stress };
struct UnitAnim { bool hero; int id; Anim kind; float t, dur; };
struct Projectile { Vector2 from, to; float t, dur; int kind; bool hero; };
struct Spark { Vector2 p, v; float life, max, size; Color c; };
// An action in progress: the actor winds up, its effect lands at `impact`, and the turn ends at `end`.
struct PendingAction {
    bool active = false, hero = false, applied = false, fired = false;
    int id = -1, ability = -1, target = -1;
    Anim kind = Anim::None;
    float t = 0, fire = 0, impact = 0, end = 0;
};

// How a crew member is posed this frame (all zero = standing ready). Set by the combat animations.
// Uniforms worn by the Nautilus's own crew, who work the salon but never join expeditions.
enum NpcOutfit { OUT_HELMSMAN, OUT_RADIO, OUT_ENGINEER, OUT_PROFESSOR, OUT_STEWARD, OUT_ORDERLY, OUT_COUNT };

struct Pose {
    float lean = 0;        // + leans forward, - rears back
    float crouch = 0;      // 0..1 bends the knees
    float reach = 0;       // 0..1 thrusts the weapon arm forward
    float raise = 0;       // 0..1 lifts the weapon arm overhead
    float backRaise = 0;   // 0..1 lifts the other arm
    float weaponTilt = 0;  // extra weapon rotation in degrees (+ swings down/forward)
    float stride = 0;      // 0..1 steps the front foot forward (lunges, staggers)
    float tremble = 0;     // 0..1 shaking hands and head (fear, strain)
    float headDown = 0;    // + bows the head (dread, pain), - snaps it back
};

struct DungeonState {
    int tier = 0;                  // index into CAVE_TIER_LEVEL
    float scroll = 0, walkT = 0;   // how far the party has walked (drives the parallax), and the walk timer
    std::vector<UnitAnim> anims;
    std::vector<Projectile> shots;
    std::vector<Spark> sparks;
    PendingAction pending;
    float shake = 0;
    float corridorT = 0;           // time since the "advance or swap a battery?" choice came up (it fades in)
    float batteryT = 0;            // counts down while a battery is being swapped in
    float lightShown = 100;        // the flashlight as drawn: eases toward `light`
    std::string levelUps;
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

// ---------- platformer ----------
enum PlatLevel { PL_PIPES, PL_HULL, PL_PIRATE, PL_COUNT };

struct PlatEnemy {
    char type; Vector2 pos, home; float dir, t;
    int state = 0;    // pirates: 0 hidden or idle, then emerging / stabbing / retreating, or aiming
    float timer = 0;  // time in the current state (or cooldown while hidden)
    Vector2 aim{0, 0}; // where a gunner is aiming
};
struct PlatShot { Vector2 pos, vel; float life; int kind; }; // 0 musket ball, 1 lit bomb, 2 explosion
struct PlatParticle { Vector2 p, v; float life, max, size; Color c; };
struct PlatBoss {
    char type = 0;               // 'K' Kraken, 'B' Blackbeard, 0 = none
    Vector2 home{0, 0}, pos{0, 0}, vel{0, 0};
    int hp = 3, state = 0;
    float timer = 0, invuln = 0, dir = -1;
    int volley = 0;                               // Blackbeard: alternates pistol shots and charges
    float tentX[2] = {0, 0}, tentT[2] = {-1, -1}; // Kraken tentacle strikes (x, time since warning; <0 = idle)
    bool tentTop[2] = {false, false};             // true = slams down from above, false = rises from the abyss
    bool defeated = false;
};

struct PlatformState {
    int level = PL_PIPES;
    std::string layoutCode;
    std::vector<std::string> tiles;
    int w = 0, h = 0;
    Vector2 pos{0, 0}, vel{0, 0}, scale{1, 1}, startPos{0, 0};
    bool onGround = false, facingRight = true;
    int wallSide = 0, lockSide = 0;   // wall being slid on / wall just jumped from (-1 left, 1 right)
    float coyote = 0, wallCoyote = 0, jumpBuffer = 0, wallLock = 0, runAnim = 0, accumulator = 0;
    int checkpointChunk = 0;
    std::vector<PlatEnemy> enemies;
    PlatBoss boss;
    std::vector<PlatParticle> particles;
    int coins = 0, deaths = 0, reward = 0, relic = -1;
    float time = 0, deathTimer = 0, camX = 0, camY = 0;
    bool finished = false, exitOpen = true;
    // Sections are stitched left to right, each raised or lowered so its entrance meets the last exit.
    std::vector<int> partX;          // first tile column of each section
    std::vector<Vector2> spawns;     // where you respawn in each section (its checkpoint)
    std::vector<float> deathY;       // per tile column: fall below this and you're lost
    std::vector<int> partInterior;   // per section: the tile row where its below-decks interior starts (or a huge number)
    std::vector<int> partKind;       // per section: 0 open air, 1 the ship's hold, 2 the captain's cabin
    std::vector<int> layout;         // the sections in this run, so a death can rebuild the level from scratch
    std::vector<PlatShot> shots;     // musket balls and bombs in flight
    bool hard = false;               // Hard keeps the gears and jets; Normal leaves them out
    bool checkpoints = false;        // respawn at the last section reached, but forfeit the relic
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
    int upgrades[UP_COUNT] = {0, 0, 0, 0};
    std::vector<int> platLayouts[PL_COUNT]; // which chunks make up each platform level's current layout
    bool platCleared[PL_COUNT] = {false, false, false};
    float platBest[PL_COUNT] = {0, 0, 0};    // best clear time in seconds (0 = never cleared)
    bool platHard = false;                   // Periscope option: the full-strength layouts
    bool platCheckpoints = false;            // Periscope option: checkpoints, at the cost of the relic
    int caveTierCleared = -1;                // highest cave level beaten (-1 = none)
    int caveTier = 0;                        // the cave level chosen at the Helm
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
int MaxRoster(const Game& g);
int RecruitsPerScan(const Game& g);
int ScanCost(const Game& g);
int WardCostPerHp(const Game& g);
int LightDrainPerRoom(const Game& g);
const char* UpgradeName(int u);
const char* UpgradeDesc(int u, int level); // what the given level does
int UpgradePrice(int level);                // price to buy the given level
int LoadoutCount(const Hero& h);
void ScaleEnemyForTier(Enemy& e, int tier);

// ---------- save.cpp ----------
bool SaveGame(const Game& g);
bool LoadGame(Game& g);
void DeleteSave();

// ---------- render.cpp: lighting, textures, post-processing, figures ----------
void InitArt();
void UnloadArt();
const Font& BodyFont();
const Font& BoldFont();
void BeginFrame();                 // everything is drawn into an offscreen scene...
void EndFrame(float time);         // ...then presented through the post-process shader
void SetPost(float vignette, float grain, float bloom);
bool SaveFrameShot(const char* path); // debug: writes the last presented frame to a PNG
void BeginLayer(RenderTexture2D& rt); // draw into another texture for a while...
void EndLayer();                      // ...then return to the scene
void DrawTri(Vector2 a, Vector2 b, Vector2 c, Color col); // a triangle in any vertex order
void LightsBegin(Color ambient);   // start a lightmap: ambient is how dark unlit areas get
void AddLight(Vector2 pos, float radius, Color c, float intensity = 1.0f);
void AddCone(Vector2 origin, float angle, float spread, float length, Color c);
void LightsEnd();                  // multiply the lightmap onto the scene
void Glow(Vector2 pos, float radius, Color c); // additive bloom sprite drawn straight onto the scene
enum class Tex { Metal, Wood, Paper, Rock };
Texture2D GetTex(Tex t);
void DrawTiled(Tex t, Rectangle dst, float scale, Color tint, Vector2 offset = {0, 0});
void DrawTexturedCircle(Texture2D tex, Vector2 c, float r, bool flipY);
RenderTexture2D& OceanRT();
constexpr int PIXEL_W = SCREEN_W / 2, PIXEL_H = SCREEN_H / 2;
RenderTexture2D& PixelRT(); // low-resolution canvas for the pixel-art platformer
void DrawVGradient(Rectangle r, Color top, Color bottom);
void DrawPipeH(float x1, float x2, float y, float radius, Color base);
void DrawPipeV(float x, float y1, float y2, float radius, Color base);
void DrawFlange(Vector2 c, float radius, bool vertical, Color base);
void DrawGauge(Vector2 c, float r, float needle01, Color face);
void DrawGear(Vector2 c, float r, int teeth, float rot, Color col);
void DrawBrassPlate(Rectangle r, const char* text, int size);
void DrawCrewFigure(const Hero& h, Vector2 feet, float scale, bool faceRight, float walk, float t, const Pose& pose = Pose{});
void DrawCrewFigureInked(const Hero& h, Vector2 feet, float scale, bool faceRight, float walk, float t,
                         const Pose& pose = Pose{}, Color tint = WHITE); // with ink and volume
void DrawShadowBlob(Vector2 feet, float w);
// Characters: draw between BeginFigure/EndFigure with their feet at FigureFeet(); EndFigure inks them,
// adds volume, and places them with their feet at `feet` on screen. `tint` flashes them (e.g. red when hit).
void BeginFigure();
void EndFigure(Vector2 feet, Color tint = WHITE);
Vector2 FigureFeet();
RenderTexture2D& ArtRT();              // 512 x 768 scratch canvas for flat art mapped onto walls
void BeginCanvas(RenderTexture2D& rt); // like BeginFigure, but just paints into `rt`
void EndCanvas();
Color Tone(Color c, float k); // k < 0 darkens toward shadow, k > 0 lightens toward a warm highlight
void ShadeBall(Vector2 c, float r, Color col);                         // a lit sphere
void ShadeLimb(Vector2 a, Vector2 b, float wa, float wb, Color c);     // a lit tapered cylinder
void ShadeQuad(Vector2 tl, Vector2 tr, Vector2 br, Vector2 bl, Color c); // a lit panel
void BeginBackdrop();          // draw distant scenery softly out of focus...
void EndBackdrop(float blur);  // ...and lay it into the scene
void InkPass(float ink, float hatch); // Darkest Dungeon-style inking and crosshatching over the world drawn so far

// ---------- ui.cpp ----------
int MeasureTxt(const std::string& s, int size, bool bold = false);
void Txt(const std::string& s, float x, float y, int size, Color c);
void TxtBold(const std::string& s, float x, float y, int size, Color c);
void TxtShadow(const std::string& s, float x, float y, int size, Color c, bool bold = false);
void DrawTextCenteredBold(const std::string& s, float cx, float y, int size, Color c);
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
// sprite sheet pages (depth.exe --sprites)
void DrawSalonSpritePage(float t);
void DrawCrewSpritePage(float t);
void DrawCaveSpritePage(float t);
void DrawPlatformSpritePage(int page, float t);
Image GrabFrame();
void DebugPetCat();
void SceneHub(Game& g);
void SceneHelm(Game& g);
void SceneCrew(Game& g);
void SceneRadar(Game& g);
void SceneWard(Game& g);
void SceneSickLeave(Game& g);
void SceneBookshelf(Game& g);
void ScenePeriscope(Game& g);
void SceneWorkshop(Game& g);

// ---------- dungeon.cpp ----------
void StartDungeon(Game& g);
void SceneDungeon(Game& g);
void DebugEnterCombat(Game& g);    // debug: jump straight into the first fight
void SimulateExpeditions(int runs, int level, bool randomPlayer, int tier = 0); // debug: auto-play expeditions and print the results

// ---------- platformer.cpp ----------
void GeneratePlatLayout(Game& g, int level);
bool PlatLayoutValid(const Game& g, int level);
std::string PlatLayoutCode(const Game& g, int level);
const char* PlatLevelName(int level);
void StartPlatform(Game& g, int level);
void ScenePlatformer(Game& g);
int VerifyPlatformLevels(); // debug: proves every section can be crossed; returns the number that can't
