#pragma once
// ============================================================================
//  THE FLIGHT - arcade game 7 of the Deep Arcade (Reference_For_Future_MP_Games/"The Flight - Arcade Game 7 Design
//  Document"; progress in docs/FLIGHT_PROGRESS.md). A 2-6 player 3D RTS in which each player is a bird: the Founder,
//  flown in third person, fishing the living sea (Red Tide's ecosystem engine) to feed a colony.
//
//  This header is the headless core (no drawing): the founders' data, the island, the wind, the Founder's flight,
//  the strike, carrying, the nest's cache, hunger, death and the chick-leader. Stage 1 of the design's build order:
//  "Flight: the Founder's body, wind, stamina, altitude, the dive and strike, carrying; one tropical island and a
//  lagoon of fish on Red Tide's engine".
//
//  World units are metres; y is up and the sea's surface is y = 0; the island stands at the origin.
// ============================================================================
#include "raylib.h"
#include "redtide.h"
#include <string>
#include <vector>

namespace fl {

// ---------------------------------------------------------------- the twelve founders (data/flight/flight_founders.json)
struct FounderDef {
    std::string key, name, bird, boost, unique, weakness, playstyle;
    float cruise = 12, sprint = 20, stamina = 8, talon = 1, attack = 20, hp = 100;
    int carry = 3;                          // the largest fish size class it lifts
    float span = 1.5f, beak = 0.1f, tail = 0.5f, crest = 0;   // the look: wingspan (m), beak, tail, crest
    Color back{120, 120, 120, 255}, belly{230, 230, 230, 255}, accent{40, 40, 40, 255};
};
const std::vector<FounderDef>& Founders();
int FounderIndex(const std::string& key);   // -1 if unknown
std::string FlightDataDir();                  // data/flight (found like Red Tide's)

// ---------------------------------------------------------------- the island (stage 1: the tropical island)
struct Island {
    int n = 0; float cell = 2, x0 = 0, z0 = 0;   // the heightmap's grid
    std::vector<float> h;                       // metres above the sea (negative: the sea floor)
    std::vector<Vector3> palms;                 // trunk feet
    Vector3 hill{}, nest{};                     // the hill's top; the Founder's first nest (in a palm by the hill)
    uint32_t seed = 1;
    void Generate(uint32_t seed);
    float Height(float x, float z) const;       // bilinear; the open sea beyond the grid is -12
    Vector3 Normal(float x, float z) const;
    bool Land(float x, float z) const { return Height(x, z) > 0.15f; }
    Vector3 Ground(float x, float z) const { return {x, std::max(0.0f, Height(x, z)), z}; }   // where a bird stands (the surface over water)
};

// ---------------------------------------------------------------- the weather
struct Wind {
    Vector2 dir{1, 0}; float speed = 6;         // where it blows to, m/s
    Vector2 nextDir{1, 0}; float nextSpeed = 6; float shiftT = 0, shiftLen = 180;
    Vector2 At(float t) const;                  // the wind now (with a little gust)
};

// ---------------------------------------------------------------- the Founder
enum class FState : uint8_t { Fly, Strike, Struggle, Under, Perched, Floating, Fainted, Dead };
const char* FStateName(FState s);
struct CachedFish { int sp = -1; int size = 1; float age = 0; };
struct Founder {
    int def = 0;
    FState st = FState::Perched;
    Vector3 pos{}, vel{};
    float yaw = 0, pitch = 0, bank = 0;         // facing (yaw about y, 0 = +x), nose up (+) / down (-), roll
    float airspeed = 0;
    float stamina = 8, hunger = 1, hp = 100;    // stamina in seconds of sprint; hunger 1 full .. 0 starving
    bool flapping = false, sprinting = false, gliding = true, exhausted = false;
    // carried: one fish (species, size)
    int carrySp = -1, carrySize = 0;
    // the strike: a slow-motion half second steering the talons onto a fish
    float strikeT = 0, strikeLen = 0.5f; Vector3 strikeAt{}, strikeAim{}; float strikeSpeed = 0;
    float struggleT = 0; int struggleSp = -1, struggleAgent = -1;
    float underT = 0, faintT = 0, respawnT = 0;
    bool chick = false; int chickFish = 0;      // the chick-leader after a death: half speed and carry until 3 fish
    float adultT = 0;                           // seconds at full stats after growing up (the colony takes orders again at 10)
    int deaths = 0;
    int agent = -1;                             // the body in the sea's web (a "Diver" record) while low over water
    std::string lastCause;
    // what this founder is now (species stats, chick-leader, starvation and three deaths applied)
    float Cruise(const FounderDef& d) const;
    float Sprint(const FounderDef& d) const;
    int Carry(const FounderDef& d) const;
    float StatMul() const;                      // starving -30%, three deaths -10%, a chick half
};
struct FounderInput {
    float yaw = 0, pitch = 0;                   // where the player is steering (absolute, radians)
    bool flap = false, sprint = false, brake = false;
    bool interact = false;                      // E: drop the fish in the cache, land, pick up
    bool eat = false;                           // F: eat a fish from the cache
    bool takeoff = false;                       // Space from a perch or the water
    Vector2 steer{0, 0};                        // during a strike: the talons' aim, -1..1 on each axis
};

// ---------------------------------------------------------------- the world (one Founder, one island: stage 1)
struct World {
    std::string seaKey = "flight_tropical";
    Island island;
    rt::Ecosystem eco;
    Wind wind;
    Founder me;
    std::vector<CachedFish> cache; int cacheCap = 20;
    float time = 0;                             // seconds of game time
    float timeScale = 1;                        // the strike's slow motion
    uint32_t rng = 7;
    std::vector<std::string> log;
    int fishCaught = 0, fishMissed = 0, fishLost = 0, fishEaten = 0;
    bool forceHit = false;                      // (tests: a strike on a fish always lands)
    static constexpr float DAY = 240;           // a game day is four real minutes
    float DayPhase() const;                     // 0 midnight .. 0.25 dawn .. 0.5 noon .. 0.75 dusk
    const FounderDef& Def() const { return Founders()[me.def]; }
    void Init(const std::string& founderKey, uint32_t seed);
    void Step(float dt, const FounderInput& in); // dt in real seconds (the slow motion scales the world inside)
    void Say(const std::string& s);
    float Rand();
    // the sea
    Vector2 WindAt() const { return wind.At(time); }
    float Thermal(Vector3 p) const;             // updraft m/s (the hill in the afternoon)
    int FishNear(Vector3 p, float r, float maxDepth, int* count = nullptr) const;   // nearest catchable fish agent
    float FeedValue(int sp) const;              // a fish's size class
  private:
    void StepFounder(float dt, const FounderInput& in);
    void StartStrike();
    void ResolveStrike();
    void Kill(const std::string& cause);
    void Respawn();
    void SyncBody();
};

int RunFlightTest();                            // depth.exe --flight-test

}  // namespace fl
