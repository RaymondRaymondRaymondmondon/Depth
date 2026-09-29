// ============================================================================
//  DEPTH - the beast engine's internals, shared between the engine (beasts.cpp) and the biome
//  species tables / behaviours (beasts_biomes.cpp). Not for the host: platformer.cpp uses beasts.h.
// ============================================================================
#pragma once
#include "beasts.h"
#include "game.h"
#include <algorithm>
#include <cmath>

namespace bk {
constexpr float TILE = 32.0f;
constexpr float WALK_G = 1900.0f;
constexpr int SRC_ENEMY = -100; // memory sources at or below this are the level's own enemies (pirates, warriors, spiders)

inline Vector2 Add(Vector2 a, Vector2 b) { return {a.x + b.x, a.y + b.y}; }
inline Vector2 Sub(Vector2 a, Vector2 b) { return {a.x - b.x, a.y - b.y}; }
inline Vector2 Mul(Vector2 a, float s) { return {a.x * s, a.y * s}; }
inline float Len(Vector2 a) { return sqrtf(a.x * a.x + a.y * a.y); }
inline float Dist(Vector2 a, Vector2 b) { return Len(Sub(a, b)); }
inline Vector2 Norm(Vector2 a) { float l = Len(a); return l > 1e-4f ? Mul(a, 1 / l) : Vector2{0, 0}; }
inline float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
inline float Sig(float x, float k, float m) { return 1.0f / (1.0f + expf(-k * (x - m))); } // the reference's utility response curve

// Deterministic randomness: the beasts never touch raylib's global RNG, so a level seed replays exactly.
inline float Hash(unsigned a, unsigned b) {
    unsigned h = a * 374761393u + b * 668265263u + 0x9E3779B9u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xFFFFFF) / 16777216.0f;
}
inline float R(BeastWorld& W) { W.rng ^= W.rng << 13; W.rng ^= W.rng >> 17; W.rng ^= W.rng << 5; return (W.rng & 0xFFFFFF) / 16777216.0f; }
inline float R(BeastWorld& W, float lo, float hi) { return lo + (hi - lo) * R(W); }

struct Choice { BeastAct act = BeastAct::Wander; int target = -1, targetId = 0; Vector2 goal{0, 0}; float score = 0; };
struct Diver { Vector2 pos{0, 0}, vel{0, 0}; bool alive = false; };

// One biome's creatures: its species, who eats whom, and the few behaviours the shared engine can't express.
struct BiomeDef {
    int level = -1;
    const SpeciesDef* species = nullptr;
    int count = 0;
    const FoodEdge* web = nullptr;
    int webN = 0;
    bool water = false;       // swimmers and slow-sinking bodies; otherwise air (bodies drop)
    bool ignoreDiver = false; // the Pipes: nothing there notices the diver at all
    float clarity = 0.6f;     // how far sight carries (water haze, darkness)
    float daylight = 0.1f;    // ambient light where nothing else lights it
    bool arenaLimit = false;  // a boss arena is appended past the last part: keep out of it
    const float* sizes = nullptr; // how big each species is drawn (and bites), relative to its base art - higher up the food chain, bigger
    void (*spawn)(BeastWorld&, PlatformState&) = nullptr;
    void (*hooks)(BeastWorld&, PlatformState&, int, float) = nullptr;       // per-beast species behaviour (before thinking)
    void (*extras)(BeastWorld&, const PlatformState&, int, Choice&) = nullptr; // extra utility options
    void (*tick)(BeastWorld&, PlatformState&, float) = nullptr;               // per-world objects (kegs, coconuts)
    bool (*lethal)(const BeastWorld&, const Beast&) = nullptr;               // when touching is fatal (null = SpeciesDef::lethal)
    bool (*touch)(const BeastWorld&, const Beast&, Rectangle) = nullptr;     // extra lethal reach (an eel's head out of its hole)
};
const BiomeDef* Biome(int level);
const BiomeDef& HullBiome();
const BiomeDef& PirateBiome();
const BiomeDef& IslandBiome();
const BiomeDef& CaveBiome();
const BiomeDef& PipesBiome();
const BiomeDef& WeedsBiome();
const BiomeDef& AtlantisBiome();

const SpeciesDef& Sp(int biome, int s);
float Pref(int biome, int pred, int prey);
inline bool Has(const SpeciesDef& S, unsigned t) { return (S.traits & t) != 0; }
Diver SeeDiver(const PlatformState& p);
bool Alive(const Beast& b);
bool Valid(const BeastWorld& W, int i, int id);
Vector2 Home(const BeastWorld& W, const Beast& b);
Vector2 DenMouth(const BeastWorld& W, int d);
int NearestDen(const BeastWorld& W, Vector2 at, float maxD = 1e9f);
bool InCloud(const BeastWorld& W, Vector2 at);
float LightAt(const PlatformState& p, const BeastWorld& W, Vector2 at);
float Recall(const Beast& b, const BeastMemory& m, float now);
void Remember(Beast& b, uint8_t kind, int source, int sid, Vector2 pos, Vector2 vel, float strength, float now);
void Forget(Beast& b, int source, int sid);
int NewBeast(BeastWorld& W, int species, Vector2 at);
int PackCount(const BeastWorld& W, const Beast& b, float r);
void Consider(const Beast& b, Choice& best, BeastAct a, float u, int tgt, int tid, Vector2 g);
void Kill(BeastWorld& W, PlatformState& p, int v, int killer);
void Hurt(BeastWorld& W, PlatformState& p, int v, float dmg, int attackerSpecies, bool byDiver);
void EnterDen(BeastWorld& W, Beast& b, int d);
void LeaveDen(BeastWorld& W, Beast& b);
Vector2 FleeTarget(BeastWorld& W, int i, Vector2 threat);
Vector2 WanderTarget(BeastWorld& W, int i);
void Collide(const NavGrid& N, Vector2& pos, Vector2& vel, float hw, float hh, float dt, bool& grounded);
float HalfW(const SpeciesDef& S, const Beast& b);
float HalfH(const SpeciesDef& S, const Beast& b);
void AddCloud(BeastWorld& W, Vector2 at, float r, float life, int kind);
int SpawnCorpse(BeastWorld& W, int species, Vector2 at, float meat);

// Where a biome can put things: one floor spot per column (the first standable cell from the top of the
// open space), in pixels x and tile row y. Shared by every biome's spawner.
struct Spots {
    std::vector<Vector2> floor;
    void Build(const BeastWorld& W, const PlatformState& p, int x0, int x1);
    Vector2 At(float u) const { return floor[std::min((int)floor.size() - 1, std::max(0, (int)(u * floor.size())))]; }
    Vector2 Stand(Vector2 f, float radius) const { return {f.x, (f.y + 1) * TILE - radius * 0.6f - 1}; }
    Vector2 Above(const BeastWorld& W, Vector2 f, int band) const; // open cell `band` tiles over the floor
};
}  // namespace bk
