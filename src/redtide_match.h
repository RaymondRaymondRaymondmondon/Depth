#pragma once
// ============================================================================
//  Red Tide: the match rules (design doc, "Core loop and rules", "Weapons", "Tonics...", "Enemy factions", "Boss
//  fights"). Headless: the scene draws a Match and feeds it one diver's input; --redtide-sim drives every diver with
//  a bot. Everything numeric comes from data/redtide (the workbooks, extra.json, weapons.json).
// ============================================================================
#include "redtide.h"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace rt {

// ---------------------------------------------------------------- weapons (data/redtide/engine/weapons.json)
struct WeaponClass { float spreadHip = 2, spreadAds = 1, adsS = 0.2f, swimAim = 0.9f, headshot = 2, fullTo = 15, halfAt = 25, speed = 28, recoil = 1; };
struct WeaponDef {
    std::string id, name, cls, source, forged, forgedTwist;
    float damage = 30, rpm = 300, noise = 2, reload = 1.4f, chum = 0, arc = 0, splash = 0, reach = 0, spinup = 0, cone = 0, coneDeg = 30;
    int pellets = 1, mag = 8, reserve = 32, price = 0, burst = 0, chain = 0, explodesOver = 0;
    bool perRound = false, pins = false, net = false, melee = false, polyp = false, wave = false;
    WeaponClass handling;
};
struct TonicDef { std::string id, name, effect; int price = 2000, priceSolo = 0; };
struct WeaponsData {
    std::vector<WeaponDef> weapons;
    std::vector<TonicDef> tonics;
    std::map<std::string, std::string> rackFor, tonicPoi;
    float forgePrice = 5000, forgeDmg = 2.5f, forgeAmmo = 1.5f, reroll = 2500, rearm = 4500;
    std::vector<std::string> forgeAmmoTypes;
    int lockerPull = 950, fireSalePull = 10, lockerMoveMin = 8, lockerMoveMax = 12;
    std::string wonder;
    float knifeDamage = 100, knifeReach = 1.5f, knifeRate = 1.2f;
    int startScrip = 500, startLimpets = 2;
    std::string sidearm = "cormorant";
    int Index(const std::string& id) const;
    const TonicDef* Tonic(const std::string& id) const;
};
const WeaponsData& Weapons();

// ---------------------------------------------------------------- the level (swimmable volumes built from the blockout)
struct Volume { Vector3 lo, hi; int zone = -1; int link = -1; int window = -1; bool diverOk = true; };   // a room (zone), a passage (link) or a porthole (window)
struct Door { int link = -1; bool open = false; int cost = 0; Vector3 pos{}; std::string name; };
enum class StationType { Rack, Tonic, Locker, Forge, Power, Workbench, Trap, Quest, Cleaning, Feature, Hazard, Entry, Boss, QuestStep, Cache };
struct Station {
    StationType type = StationType::Feature;
    std::string name;
    Vector3 pos{};
    int zone = -1;
    int weapon = -1;               // racks: the weapon
    std::string tonic;             // tonic machines: the tonic id
    int lockerSpot = -1;           // Locker spots: 0, 1, ...
    bool needsPower = false;       // tonic machines beyond the first two, the second Locker spot, the Forge, traps
    int step = 0;                  // quest steps: 1, 2, 3...
};
// Scenery from a map's "dressing" (extra.json): placed once from a fixed seed; solid pieces (columns, coral walls, brain
// coral, a coral head, ledges) block divers, darts and sight.
enum class PropKind { Column, Stalactite, Stalagmite, Crystal, Root, Ledge, Pool, Silt, Machine, CoralWall, Table, Brain, Staghorn, Seagrass, Mangrove, Mound, Building, Terrace, Fan, Amphora, Grate, Crenel, COUNT };
struct Prop { PropKind kind = PropKind::Column; Vector3 pos{}, half{}; int zone = -1; bool solid = false; uint32_t seed = 0; };
struct Level {
    std::vector<Volume> vols;
    std::vector<Prop> props;
    std::vector<Door> doors;       // one per link (cost 0: always open)
    std::vector<Station> stations;
    int startZone = 0;
    Vector3 start{};
    // inside some room, or a passage whose door is open, with a margin r (linkOpen: per link, 1 = open); divers keep
    // to the volumes they may use, darts (darts = true) also fly through portholes into the open water outside
    bool Inside(Vector3 p, float r, const std::vector<char>& linkOpen, bool darts = false) const;
    // slides along walls: tries the whole move, then each axis alone
    Vector3 Move(Vector3 from, Vector3 to, float r, const std::vector<char>& linkOpen) const;
    bool Sight(Vector3 a, Vector3 b, const std::vector<char>& linkOpen, bool darts = false) const;   // a clear line through open water
};
void BuildLevel(const MapData& m, Level& L);

// ---------------------------------------------------------------- divers
struct Held { int def = -1; int mag = 0, reserve = 0; bool forged = false; int altAmmo = -1; };
struct DiverState {
    int slot = 0; bool bot = false; bool invulnerable = false;   // (--shots only)
    Vector3 pos{}, vel{};
    float yaw = 0, pitch = 0;
    int zone = 0;
    float hp = 100, hpMax = 100, regenT = 99;
    bool downed = false, dead = false; float downT = 0, reviveT = 0, reviveTouchT = 0, selfReviveT = 0; int selfRevives = 0, quickBought = 0;
    std::vector<Held> weapons; int cur = 0; int slots = 2;
    Held downHeld;                     // downed: the Cormorant only
    std::vector<Held> savedWeapons; int savedCur = 0; bool harpoonHour = false;
    float fireT = 0, reloadT = 0; bool reloading = false; int burstLeft = 0; float spin = 0; float meleeT = 0; bool ads = false;
    float recoil = 0;
    int hitCount = 0;
    int limpets = 2;
    std::set<std::string> tonics;
    int scrip = 500, scripEarned = 0;
    float stamina = 1;
    float heldT = 0; int holder = -1; float holdDmg = 0, holdPending = 0, holdHp0 = 0; bool holdLethal = false; int struggle = 0; float holdDragT = 0;
    float stunT = 0, aimSway = 0, poisonT = 0, poisonDps = 0, bleedT = 0, slowT = 0, slowMult = 1, flinchT = 0;
    int agent = -1;                    // the diver's body in the Ecosystem
    int slipLink = -1; float slipT = 0, driftT = 0;   // riding a slipstream (link), how far along; the drift after it
    int drumUses = 0;                  // the Reef Shaman's drum, taken
    bool spark = false, ichorJar = false;   // Atlantis: the treasury crystal's spark in a jar; a Lost One's ichor
    float cutT = 0;                    // being cut free of Reacher coral by a teammate
    int kills = 0, headshots = 0, downs = 0, revives = 0;
    float hitMarker = 0; bool hitWeak = false;
    float hurtT = 0; Vector3 hurtFrom{};
    std::string lastHitBy, lastKill; float lastKillT = 0;
    // bot memory
    int botTarget = -1, botFlee = -1; float botThinkT = 0; Vector3 botGoal{}; std::string botPlan; float botStuckT = 0; Vector3 botLastPos{};
};

struct Dart {
    Vector3 pos, vel, start; float life = 2.5f; float damage = 30; int weapon = 0; int owner = 0; bool forged = false; bool alive = true;
    int pierce = 0; int alt = -1;
    int enemy = -1;                    // fired by this enemy agent (hits divers); -1 a diver's dart
    int kind = 0;                      // 0 dart, 1 chum, 2 net, 3 explosive, 4 limpet charge (thrown), 5 enemy net, 6 enemy chum
    float fuse = 0; int stuck = -1; Vector3 stuckOff{};
};
enum class DropType { Resupply, BloodFrenzy, DoubleScrip, Purge, Shipwright, FireSale, HarpoonHour, COUNT };
const char* DropName(DropType d);
struct FloorDrop { DropType type = DropType::Resupply; Vector3 pos{}; float t = 30; bool alive = true; int weapon = -1; };  // weapon >= 0: a dropped gun
struct Caption { std::string who, text; float t = 4; };
struct FxEvent { int kind = 0; Vector3 pos{}; Vector3 dir{}; };   // 0 blood hit, 1 wall hit, 2 muzzle, 3 explosion, 4 pickup, 5 boom, 6 arc, 7 crate, 8 melee
struct Crate { Vector3 pos{}; float t = 1.5f; bool fallen = false; int kind = 0; float dmg = 60, radius = 1.6f; int owner = -1; float top = 0; };   // kind 0 loose cargo, 1 a stalactite

enum class TidePhase { Calm, Tide, Hunt, Over };

struct Match {
    std::string mapKey, artKey;
    const MapData* map = nullptr;
    Ecosystem eco;
    Level level;
    std::vector<char> linkOpen;       // live: per link
    std::vector<DiverState> divers;
    std::vector<Dart> darts;
    std::vector<FloorDrop> drops;
    std::vector<Caption> captions;
    std::vector<FxEvent> fx;          // the scene drains these for particles
    std::vector<Crate> crates;
    int tide = 1, quota = 12, tideKills = 0, players = 1;
    TidePhase phase = TidePhase::Calm;
    float phaseT = 0, time = 0;
    bool power = false;
    int lockerSpot = 0, lockerSpots = 1, lockerPulls = 0, lockerMoveAt = 10, teamPulls = 0; bool wonderHeld = false; float lockerMovedT = 0;
    float fireSaleT = 0, doubleScripT = 0, frenzyT = 0, harpoonT = 0, lastDropT = 0; int lastFireSaleTide = -99;
    std::vector<DropType> recentDrops;
    bool predatorHunt = false; int huntApexLeft = 0;
    bool bossActive = false; int bossAgent = -1; int bossPhase = 1; float bossLingerT = 0, bossIdleT = 0, bossCd[4] = {0, 0, 0, 0};
    int bossWind = -1; float bossWindT = 0; int bossTarget = -1;
    float bossGillsT = 0, bossInhaleT = -1; int bossInhaleDiver = -1; float bossGillDmg = 0; bool bossStunUsed = false, bossCalled = false, bossProvoked = false;
    float trapT = 0;
    std::set<std::string> keys; bool safeOpen = false;
    std::map<int, float> enemyFireT, enemyTellT, contactT, patchT;
    std::map<int, int> enemyPatrol;
    std::set<int> squadSpotted;
    std::vector<std::vector<int>> attacksBySp;
    std::vector<Body> bodies;                   // per species: hit capsules
    std::vector<float> bodyScale;               // per species: the drawing scale that matches them   // indices into map->attacks per species
    std::string botStyle = "careful";
    int scripAt10 = -1;
    int questStep = 0, whistlePulls = 0; float whistleT = 0; bool supperCall = false;   // the Supper Call easter egg
    bool breachOpen = false;
    // map mechanics (from extra.json): per-zone flags, the boss's kind and its extra state
    std::vector<char> noFireZone, rockZone; std::vector<float> crawlZone;
    std::map<int, float> floraBleed;        // a cut Bloodvine patch: seconds it keeps smelling of blood
    int bossKind = 0;                       // 0 the Goliath, 1 the Lobster, 9 any other (attacks straight from its sheet)
    float bossRecentDmg = 0; bool bossRearReq = false, bossMoved = false, cacheOpen = false;
    std::string WonderId() const;
    // the Reef
    bool alliesHostile = false; int drumBeats = 0; float tideTurnT = 0, reefScanT = 0;
    std::map<int, float> reacherHP;          // Reacher coral patches: their HP (a held diver is freed at 0)
    std::map<int, float> reacherPrey;        // beasts the coral holds: seconds until it has fed on them
    struct Polyp { Vector3 pos; float t = 30, cd = 0; int owner = -1; bool forged = false; };
    std::vector<Polyp> polyps;               // the Anemone Gun's rooted polyps
    std::vector<int> pod; bool podSpawned = false; float breathT = 0;   // the Matriarch's pod and her breath cycle
    void BeatDrum(int d);
    // Atlantis
    struct Ichor { Vector3 pos; float t = 60; };
    std::vector<Ichor> ichor;                // dead Lost Ones' black ichor: repels the apex beasts, draws scavengers
    std::map<int, float> shieldHP;           // Legionnaires' tower shields
    int wyrmState = 0, wyrmGrate = -1;       // the Cistern Wyrm: 0 below, 1 rising (the grate rattles), 2 surfaced, 3 hunting the streets
    float wyrmT = 3, wyrmUp = 0, wyrmStrandT = 0;
    int wyrmDragDiver = -1; float wyrmDragT = 0, wyrmDragDmg = 0; bool wyrmStruck = false, wyrmCongers = false, wyrmFlooded = false;
    float wyrmReformT = -1, floodT = 0;
    float priestT = 0; int horror = -1; float horrorT = 0, horrorTeleT = 0; bool horrorCalled = false;
    std::vector<int> questAt;                // per quest chain (extra.json "quests"): the step it waits on (last + 1: done)
    float holdT = 0;
    bool revealAll = false;                  // the lighthouse: the whole city on the sonar
    int QuestChainOf(int step) const;
    std::vector<int> WyrmGrates(int ph, int zone) const;
    std::string ArtName(int sp) const;
    uint32_t rng = 99;
    bool over = false;
    std::string overReason;
    int pendingKiller = -1; bool pendingMelee = false;
    // statistics for the results panel and --redtide-sim
    int deathsByBeast = 0, deathsByEnemy = 0, deathsByHazard = 0, huntsSeen = 0, bossInhales = 0, crocHolds = 0, doorsOpened = 0;
    std::map<std::string, int> downsBy, killsBy;
    float timeToTide10 = -1;
    int maxTide = 1;

    void Init(const std::string& mapKey, int playerCount, uint32_t seed, bool bots = false);
    void InitMap(const MapData& m, const std::string& art, int playerCount, uint32_t seed, bool bots);
    void Step(float dt);
    // a diver's actions (the scene feeds its human; bots call these too)
    void SteerDiver(int d, Vector3 wish, float vert, bool sprint, bool ads, float dt);
    void Fire(int d, bool held, float dt);
    void Reload(int d);
    void Melee(int d);
    void SwapWeapon(int d, int slot);
    bool Interact(int d, bool hold, float dt);   // buys, doors, revives, struggling free; true if something happened
    std::string PromptFor(int d, int* cost = nullptr) const;
    void ThrowLimpet(int d);
    // helpers
    const WeaponDef& W(const Held& h) const;
    Held& Cur(DiverState& d);
    const Held& Cur(const DiverState& d) const;
    float Rand();
    float Rand(float a, float b) { return a + (b - a) * Rand(); }
    void Say(const std::string& who, const std::string& text, float t = 4);
    int NearestStation(Vector3 p, float r) const;
    int NearestDoor(Vector3 p, float r) const;
    int QuotaFor(int t) const;
    bool LockerLiveAt(const Station& s) const;
    Vector3 Eye(const DiverState& d) const { return {d.pos.x, d.pos.y + 0.1f, d.pos.z}; }
    Vector3 Forward(const DiverState& d) const;
    int Living() const;               // divers up (not downed, not dead)
    bool IsBoss(int agent) const { return agent >= 0 && agent == bossAgent; }
    bool DiverLink(int li) const { return linkOpen[li] && map->links[li].diverOk; }
    // for the tests
    void HitDiverPublic(DiverState& d, float dmg, const std::string& by, const std::string& effect, Vector3 from, int attacker) { HitDiver(d, dmg, by, effect, from, attacker); }
    void ApplyDropPublic(DropType t, Vector3 at) { ApplyDrop(t, at); }
    void BeginTidePublic(int t) { BeginTide(t); }
    void DropRocksPublic(Vector3 at, int n, float spread, float dmg, float radius, float delay, int owner) { DropRocks(at, n, spread, dmg, radius, delay, owner); }
    void FloraToolPublic(int patch, Vector3 at) { FloraTool(patch, at, nullptr); }
    void HitAgentPublic(int d, int agent, float dmg, bool blast = false) { Dart t; t.weapon = -1; HitAgent(d >= 0 ? &divers[d] : nullptr, agent, dmg, false, false, {1, 0, 0}, blast ? nullptr : &t); }
    void FloraHazardsPublic(float dt) { FloraHazards(dt); }
    int WonderIdx() const { return Weapons().Index(WonderId()); }
    static Held NewHeldPublic(int def) { Held h; h.def = def; if (def >= 0) { h.mag = Weapons().weapons[def].mag; h.reserve = Weapons().weapons[def].reserve; } return h; }

  private:
    void BeginTide(int t);
    void EndTide();
    void UpdateDiver(DiverState& d, float dt);
    void UpdateDarts(float dt);
    void FireRound(DiverState& d);
    void HitAgent(DiverState* d, int agent, float dmg, bool weak, bool melee, Vector3 dir, const Dart* dart);
    void Explode(Vector3 p, float dmg, float radius, int owner);
    void Arc(DiverState& d, int first, float dmg, int chain, float radius, float stun);
    bool DecideHook(Agent& a, int idx);
    bool DecideEnemy(Agent& a, int idx);
    bool DecideBoss(Agent& a, int idx);
    void OnDiverHit(int diverAgent, int attacker, float dmg);
    void BeastsVsDivers(float dt);
    void EnemiesVsDivers(float dt);
    void FloraHazards(float dt);
    void UpdateBoss(float dt);
    void UpdateDrops(float dt);
    void OnDeath(int agent, int killer);
    void DownDiver(DiverState& d, const std::string& by);
    void Revive(DiverState& d, int by);
    void HitDiver(DiverState& d, float dmg, const std::string& by, const std::string& effect, Vector3 from, int attacker = -1);
    void MaybeDrop(Vector3 at);
    void DropRocks(Vector3 at, int n, float spread, float dmg, float radius, float delay, int owner);
    void FireCone(DiverState& d, const WeaponDef& w, float dmg);
    void UpdateBossLobster(float dt, int near, float dist);
    void UpdateBossGeneric(float dt, int near, float dist);
    void UpdateBossMatriarch(float dt, int near, float dist);
    void UpdateReef(float dt);
    void UpdateAtlantis(float dt);
    void UpdateBossWyrm(float dt);
    void QuestAdvance(int chain, DiverState* d);
    void EnemyBlast(Vector3 at, float dmg, float radius, int enemy);
    void FloraTool(int patch, Vector3 at, DiverState* d);
    void ApplyDrop(DropType t, Vector3 at);
    void GiveWeapon(DiverState& d, int def, bool forged = false);
    void GiveLockerWeapon(DiverState& d);
    void Pay(DiverState& d, float amount);
    void Bot(DiverState& d, float dt);
    int DiverOfAgent(int agent) const;
    int NearestDiver(Vector3 p, float r, bool needSight, bool upOnly = true) const;
    Vector3 NavStep(const DiverState& d, Vector3 goal) const;
    Vector3 Skirt(const DiverState& d, Vector3 to) const;
};

int RunRedTideSim(const std::string& mapKey, int tides, const std::string& style, int runs, int players);
int RunRedTideMatchTest();
int RunRedTideMapTest(const std::string& key);

} // namespace rt
