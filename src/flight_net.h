#pragma once
// The Flight over the Deep Arcade's network session (design doc p32, "Simulation and networking"; the session layer is
// docs/design/5_Depth_Arcade_Networking.md). Host-authoritative: the host runs the one real World (a FlightHost, the
// arcade's GameHost for G_FLIGHT): the ecosystem, every colony's birds, the economy and the war. Each person's Founder
// is flown by the input they send (FA_INPUT); everything else they do is an order (the colony panel, the flocks page,
// the chart), applied by ApplyOrder - solo play goes through the same orders. The host writes each player their own
// snapshot 20 times a second: the world from their side (their colony in the World's fields, their fog, their news),
// the other colonies' birds only near them (interest management by distance), the sea's fish only round their bird.
// A guest keeps a mirror World built from the same seed and options (the islands, the sea and the stocks are already
// there) and flies its own Founder ahead of the host between snapshots (World::PredictFounder).
// A dropped player's colony runs on its last orders; after the session's two minutes a cautious AI takes it (it fishes
// and defends, it doesn't raid), and gives it back if they rejoin. No raylib drawing here.
#include "flight.h"
#include "arcade_game.h"
#include "net_msg.h"
#include <memory>
#include <string>

namespace fl {

// ---------------------------------------------------------------- what a player sends
enum : uint8_t {
    FA_INPUT = 0,          // the Founder's input (every frame)
    FA_HELLO,              // name, founder key (the founder only counts in the first 30 s)
    FA_PLAN, FA_NESTS, FA_GROUND, FA_REST, FA_RETRAIN, FA_SCOUT,
    FA_FLOCK_MAKE, FA_FLOCK_SET, FA_FLOCK_ORDER, FA_FLOCK_HOME, FA_FLOCK_DISBAND, FA_LEAD, FA_BUILD,
    FA_RESEARCH, FA_TRADEFOR, FA_BOOM, FA_CORNER, FA_PELICAN, FA_BARTER, FA_ANSWER,   // (stage 6)
    FA_FOUND, FA_DOSE, FA_BREW,                                                        // (stage 7)
    FA_DECREE, FA_PERK, FA_WANT_TRAIT, FA_HIRE, FA_TRIBUTE, FA_PACT, FA_LOAN, FA_BOUNTY, FA_BREAK, FA_TECH, FA_NEST_STYLE,                                                                         // (the long match: today's decree, 0-2 of the three offered)
    FA_AUTOPILOT,          // (tests only: a host configured with "test" lets a seat's colony and Founder run themselves)
    FA_COUNT
};
enum : uint8_t { FI_FLAP = 1, FI_SPRINT = 2, FI_BRAKE = 4, FI_INTERACT = 8, FI_EAT = 16, FI_TAKEOFF = 32, FI_PRESSES = FI_INTERACT | FI_EAT | FI_TAKEOFF };
void WriteInput(const FounderInput& in, Writer& w);          // (with its FA_INPUT byte)
bool ReadInput(Reader& r, FounderInput& in);                 // (after the FA_INPUT byte)
void MergeInput(FounderInput& into, const FounderInput& n);  // the newest steering and held keys; presses add up until a step uses them
void ClearPresses(FounderInput& in);

// the orders (each writes one action)
void OrderHello(Writer& w, const std::string& name, const std::string& founderKey, const std::string& look = "");   // look: "costume;colour;hat"
void OrderPlan(Writer& w, Role r, float share);
void OrderNests(Writer& w, int n);
void OrderGround(Writer& w, int zone);
void OrderRest(Writer& w, float below);
void OrderRetrain(Writer& w, Role to, Role from = Role::None);
void OrderScout(Writer& w, int isle, int zone, Vector3 at, Alt alt);
void OrderFlockMake(Writer& w, const std::vector<int>& ids, Formation f, Alt a, Stance s);
void OrderFlockSet(Writer& w, int flock, Formation f, Alt a, Stance s);
void OrderFlockTarget(Writer& w, int flock, Target t, int tSide, int tIsle, int tZone, int tFlock, Vector3 at);
void OrderFlockHome(Writer& w, int flock);
void OrderFlockDisband(Writer& w, int flock);
void OrderLead(Writer& w);
void OrderBuild(Writer& w, int kind);
void OrderResearch(Writer& w, Tree t);
void OrderTradeFor(Writer& w, int good);
void OrderBoom(Writer& w);
void OrderCorner(Writer& w, int town);
void OrderPelican(Writer& w, int town, int isle);
void OrderBarter(Writer& w, int to, const int give[G_COUNT], const int get[G_COUNT], float truceDays);
void OrderAnswer(Writer& w, int offer, bool accept);
void OrderFound(Writer& w, int isle);          // a Pathfinder founds an outpost there (Trade 4)
void OrderDose(Writer& w, int flock, int stim);
void OrderBrew(Writer& w, int stim);
void OrderDecree(Writer& w, int k);
void OrderPerk(Writer& w, int k);
void OrderWantTrait(Writer& w, int trait);
void OrderHire(Writer& w, int target);         // the Frigate Pirates against a colony, for a day
void OrderTribute(Writer& w);
void OrderPact(Writer& w, int to);
void OrderLoan(Writer& w, int to, int flock, int fish);
void OrderBounty(Writer& w, int target, int fish);
void OrderBreak(Writer& w, int with);
void OrderTech(Writer& w, int tech);
void OrderNestStyle(Writer& w, int style);                  // the Grey Wings, for a day's peace     // the trait the courtship bowls ask for (-1 any)
bool FormationUnlocked(const Colony& c, Formation f);   // (War 1: formations beyond the chevron and the scatter)
// a side's order on the world (anything but FA_INPUT, FA_AUTOPILOT); false if it was refused or changed nothing
bool ApplyOrder(World& w, int side, Reader& r);
bool ApplyOrder(World& w, int side, const Writer& order);   // (solo: the scene's own orders)
std::string TargetText(World& w, const Flock& f);           // "raiding Rival 2's caches", for the panel and the news

// ---------------------------------------------------------------- the snapshot
// (the world as `viewer` may see it; `full` adds the slow parts: the knowledge, the news, the scores, the stocks)
void WriteWorld(World& w, int viewer, Writer& out, bool full);
void PackWorld(World& w, int viewer, Writer& out, bool full);   // the wire form (deflated)
constexpr int SNAP_FULL_EVERY = 4;
// into a mirror: (re)built from the seed and options when they change; `keepOwn` keeps the guest's own Founder's flight
// (its prediction) and eases it toward the host's
bool ReadWorld(Reader& r, World& w, bool keepOwn = false);

// ---------------------------------------------------------------- the host
// Configure: "<arrangement 0-3>:<home type 0-3>:<minutes>[:test]"
std::unique_ptr<arcade::GameHost> MakeFlightHost();
World* FlightHostWorld(arcade::GameHost* h);               // the host's real world (its own screen draws it)
uint32_t FlightDataHash();
std::string FlightHostOpts(int arrangement, int homeType, int minutes);
const std::vector<int>& MatchLengths();                     // the lobby's choices (20, 30, 45 minutes)

}  // namespace fl
