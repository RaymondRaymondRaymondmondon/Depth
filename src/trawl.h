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
#include <vector>

namespace tw {

// ---------------------------------------------------------------- the numbers (trawl_data.cpp)
struct TrawlData {
    // the Gannet: a 22 m steam trawler
    float length = 22, beam = 6, freeboard = 1.2f;       // metres; freeboard is the deck's height over still water, unladen
    float dryMass = 40000;                                // kg (hull, engine, gear)
    float waterplane = 96;                                // m^2: 1 m of sinkage takes waterplane * 1025 kg
    float gm = 0.5f;                                      // metacentric height (m): how stiff she is in roll
    float rollPeriod = 6, pitchPeriod = 4.5f, rollDamp = 0.12f, pitchDamp = 0.2f;
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
};
const TrawlData& D();

// ---------------------------------------------------------------- the sea
enum class Weather { Calm, Fog, Rain, Squall, Storm, Glass, COUNT };
const char* WeatherName(Weather w);
float WeatherSwell(Weather w);                            // metres of swell (design doc, "Weather")
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
    int telegraph = 0;                                    // 0 stop, 1 slow, 2 half, 3 full; -1 slow astern
    float rudder = 0, shaft = 0;                          // -1..1 helm; 0..1 the screw
    bool sunk = false;
    float noise = 0;                                      // what the screw writes into the water this second
    std::vector<Load> loads;                              // everything aboard that isn't her own dry mass, set each step
    float extraHeelTorque = 0;                            // kg*m from lines and hands pulling at the rail (set each step)
    Boat();
    float TotalMass() const;
    float Freeboard() const;                              // the deck's height over the water amidships, now
    float RollDeg() const { return roll * 57.2958f; }
    Vector2 Forward() const { return {cosf(heading), sinf(heading)}; }
    Vector2 ToWorld(Vector2 deck) const;                  // deck point -> sea x/z
    float Speed() const;                                  // along the heading
    void Hit(int section, float dmg);                     // a ram, a bite, a rock
    void Step(float dt, const Sea& sea);
    void Shovel(float kg);                                // coal from the bunker into the firebox
    void Bleed(float dt);
    void Pump(float kg);
};

// ---------------------------------------------------------------- stations and the crew
enum class StationKind { Helm, Boiler, Pumps, PortRod, StarRod, SternRodP, SternRodS, NetWinch, Lantern, Sonar, Harpoon, Gutting, AirPump, Bell, Printer, COUNT };
struct StationDef { StationKind kind; const char* name; Vector2 at; int deck; const char* does; };   // deck 0 main deck, 1 engine room
const std::vector<StationDef>& Stations();
int NearestStation(Vector2 at, int deck, float r);

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
};

// The boat and her crew as one step (the host's 60 Hz tick): the hands' weights into the boat, the boat's roll into
// the hands.
struct Gannet {
    Sea sea;
    Boat boat;
    std::vector<Crew> crew;
    float time = 0;
    std::vector<std::string> log;                         // what just happened (a sunk section, a blown valve, a fall)
    void Init(int crewCount, uint32_t seed, Weather w = Weather::Calm);
    void Step(float dt);
    // a hand's controls (the scene feeds its human; bots will call these too)
    void Move(int c, Vector2 wish, bool brace, float dt);
    bool TakeStation(int c);                              // E near a station
    void LeaveStation(int c);                             // X
    void Primary(int c, bool held, float dt);             // left mouse at a station (shovel, pump, ...)
    void Secondary(int c, bool held, float dt);           // right mouse (bleed at the boiler)
    void Scroll(int c, float amount);                     // telegraph, lantern level
    void Steer(int c, float amount, float dt);            // A/D at the helm
    void Say(const std::string& s);
};

int RunTrawlBoatTest();                                   // depth.exe --trawl-boat-test

} // namespace tw
