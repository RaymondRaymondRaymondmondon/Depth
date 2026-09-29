// ============================================================================
//  DEPTH - the expedition chart (Master Reference, "Expeditions: the sonar chart"). An expedition is a generated
//  map of rooms joined by corridors, read off the Nautilus's sonar scope. The short way to the boss runs through
//  fights; the loops carry treasure, curios and rest, and cost light and nerves to walk.
// ============================================================================
#pragma once
#include <string>
#include <vector>

enum class RoomType;

enum class CorridorEvent { None, HallFight, Trap, Loot, Curio, Blocked };

struct ChartRoom {
    RoomType type;
    int gx = 0, gy = 0;                 // its cell on the loose grid the chart is laid on
    bool known = false;                 // its position shows on the scope (a "?" echo until scouted)
    bool scouted = false;               // what it holds shows on the scope
    bool visited = false, cleared = false;
};
struct ChartEdge {
    int a = 0, b = 0;
    std::vector<CorridorEvent> segs;    // one event per corridor segment (resolved ones become None)
    int walked = 0;                     // how many times the party has gone along it
    bool loop = false;                  // not part of the spanning tree: an alternative route
};
struct Chart {
    std::vector<ChartRoom> rooms;
    std::vector<ChartEdge> edges;
    int entrance = 0, boss = 0;
    int minFights = 0;                  // the fewest fights on any route to the boss (checked by the generator)
    int Other(int e, int r) const { return edges[e].a == r ? edges[e].b : edges[e].a; }
    int EdgeBetween(int r1, int r2) const;
    std::vector<int> Neighbours(int r) const;
};

// per-tier numbers (data.cpp)
struct ChartParams { int rooms, segMin, segMax, fights, loops, curios, rests; };
const ChartParams& ChartParamsFor(int tier);
extern const float CHART_HALLFIGHT, CHART_TRAP, CHART_LOOT; // chances per corridor stretch (data.cpp)
extern const float CHART_STRETCH_DRAIN;
extern const int MAX_MINIS_PER_RUN;                   // the light a stretch drains, as a share of the Reflector's per-room drain

// Generate a chart for a tier from a seed, following the rules; reports why any attempt was rejected when `why`
// is given. Always returns a usable chart (the generator redraws until the rules hold).
Chart GenerateChart(int tier, unsigned seed, std::string* why = nullptr);
bool CheckChart(const Chart& c, int tier, std::string& why);   // the rules, as a test
std::string ChartAscii(const Chart& c);                         // --gen-chart
int ChartFightsTo(const Chart& c, int from, int to);            // the fewest fights (rooms and hallways) from one room to another
std::vector<int> ChartPath(const Chart& c, int from, int to, bool avoidBlocked = false); // the route with the fewest fights (rooms, in order); optionally going round blocked passages
