#pragma once
// The Trawl's session loop (design doc, "The session loop", "Economy and progression"): a run is a string of
// deadlines, each three nights at sea. Dock (untimed: sell, buy, refit), choose a ground, sail out through the
// harbour line, the night (20:00 to 05:00 on the wheelhouse clock, nine real minutes), run for harbour before 05:00
// or the customs cutter seizes the hold, sell at the Fish Market; after the third night the Owners count the quota.
//
// Headless (the scene in trawl.cpp drives it and draws the dock). Numbers are in trawl_data.cpp's spirit: here, as
// named constants at the top of trawl_session.cpp.
#include "trawl.h"
#include "trawl_eco.h"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace tw {

enum class Phase { Dock, SailOut, Night, Result, Over };
const char* PhaseName(Phase p);

struct ShopItem { const char* id; const char* name; int price; const char* note; };
const std::vector<ShopItem>& ChandlerItems();
const std::vector<ShopItem>& SlipwayItems();

// The quay's stations, in the boat's frame while she's moored (her port side along the quay)
enum class DockKind { Chalkboard, Chandler, Market, Office, Slipway, Scales, Gunsmith, COUNT };   // (Scales: the Owners' quota scales)
struct DockStation { DockKind kind; const char* name; Vector2 at; const char* does; };
const std::vector<DockStation>& DockStations();
int NearestDock(Vector2 at, float r);

struct SaleLine { std::string name; float kg, price, grade, fresh, glut, bonus, value; int src = 0; };
// Three ways to use a fish (design doc, "Economy and progression"): delivered at the Owners' quota scales it counts
// toward the quota at its full value (no glut; under 70% freshness it is rejected) and pays nothing; sold at the Fish
// Market it pays shillings into the ship's purse (with glut); kept aboard it can be bartered with a trader (120% of
// its market value in goods, 150% for a species the trader wants). Only delivered fish count toward the quota.
const float QUOTA_MIN_FRESH = 0.70f, CREDIT_CARRY = 0.5f;

// Nightly variants (design doc, "Nightly variants"): at most one a night, about 40% of nights none. The Lagoon's own
// three and the two food-web ones that need no salvage; the carcass, the derelict and the storm wreck wait for diving.
enum class Variant { None, BaitRun, RedTide, KingTide, TurtleNesting, CanoeNight, COUNT };
const char* VariantName(Variant v);
const char* VariantNote(Variant v);           // what the crew sees changed
// Canoe night: a war canoe comes alongside once in the night and waits a minute for an answer
enum class CanoeState { None, Coming, Alongside, Gone };
enum CanoeChoice { CANOE_TRADE, CANOE_TRIBUTE, CANOE_REFUSE };

struct Session {
    Phase phase = Phase::Dock;
    int deadline = 1, night = 0;                // night: nights finished this deadline (0..3)
    int players = 1;
    float quota = 0, money = 0, sold = 0;       // sold: the quota credit delivered at the Owners' scales this deadline (fish only)
    float carried = 0;                          // credit past the last quota, carried into this deadline at half value
    float clock = 0;                            // minutes since 20:00
    bool clockOn = false;
    std::string ground = "lagoon";
    Weather weather = Weather::Calm;
    float moon = 0.5f;
    float wxAt = -1; Weather wxTo = Weather::Calm;   // a change of weather in the night (design doc: "the sea starts calm and wakes"): the minute it comes, and what comes
    Variant variant = Variant::None;            // tonight's variant (rolled at cast off; the rumour points at it 70% of the time)
    bool plainNights = false;                   // tests: no variants and no weather changes (the shakedown night sets it too)
    // The shakedown (design doc, "First night"): a short night on the Lagoon with a fixed seed, no quota, deaths that
    // don't count, and Kess, an old deckhand aboard for this night only, chalking short lines on the deck
    struct Shakedown {
        bool on = false, done = false;
        int step = 0;                           // 0 cast off, 1 the handline, 2 the rod, 3 the table, 4 the sonar, 5 the net, 6 blood, 7 overboard, 8 home, 9 sell
        float stepT = 0, speedT = 0;
        std::string line;                       // Kess's chalk line
        std::string aside; float asideT = 0;    // a shorter line about what is happening right now
        int kess = 1;                           // Kess's hand
        bool sharkCalled = false, sharkSeen = false;
    } shake;
    static const int SHAKE_STEPS = 10;
    void BeginShakedown(Gannet& g, Eco& e);     // a new shakedown: the Gannet at the quay, Kess aboard, the stores for one night
    void ShakeStep(float dt);                   // the steps' conditions and Kess's lines (called from Step)
    void SkipShakedown();
    CanoeState canoe = CanoeState::None; float canoeAt = -1, canoeT = 0;   // Canoe night: when it comes, how long it has waited alongside
    std::string canoeWord;                      // what came of it (for the tape and the panel)
    bool Canoe(int choice);                     // the crew's answer while it's alongside (CanoeChoice); false if there's no canoe to answer
    std::map<std::string, float> glutKg;        // kg of each species sold this deadline
    std::set<std::string> catchLog;             // species ever landed this run (first catch pays a 50% bonus)
    std::vector<std::string> tape;              // the Owners' telegraph, newest last
    std::vector<SaleLine> lastSale;
    float lastSaleTotal = 0;
    int tokens = 0;
    bool met = false;                           // (Result) the last count
    bool slip[16] = {};                         // Slipway upgrades bought (by index in SlipwayItems)
    Vector2 harbour{};                          // the harbour mouth: inside this ring she is in harbour
    float harbourR = 70;
    bool cues[4] = {};                          // midnight, 04:00, ...
    uint32_t seed = 1;
    Gannet* G = nullptr; Eco* E = nullptr;

    void Begin(Gannet& g, Eco& e, int players, uint32_t seed);   // a new run: the first deadline, starting gear
    void BeginDeadline();
    int NightsLeft() const { return 3 - night; }
    float QuotaScale() const;
    void Tape(const std::string& s);
    // harbour requests (design doc v2, "Harbour requests"): five people on the dock each chalk one request a deadline;
    // filling one pays and often unlocks something the Chandler or the Gunsmith doesn't sell
    enum ReqWho { REQ_COOK, REQ_NATURALIST, REQ_COLLECTOR, REQ_APPRENTICE, REQ_CAREY, REQ_COUNT };
    struct Request { int who = 0; std::string species; bool done = false; };
    std::vector<Request> requests;
    int freeAttach = 0;                         // the apprentice's reward: the Gunsmith fits one attachment free
    void RollRequests();                        // a fresh set for the deadline (the cook's and the collector's named species)
    std::string RequestText(int i) const;       // "The cannery cook wants a snapper cooked to 1.5x" ...
    bool RequestReady(int ci, int i, std::string* why = nullptr) const;
    bool FillRequest(int ci, int i, std::string* why = nullptr);   // at the chalkboard; hand ci's carried / the hold's fish, the deck's tally
    bool WearDrop(int ci, int dropIdx, std::string* why = nullptr);   // a mini-boss drop on a cord round hand ci's neck (instead of Mother Carey)
    // the dock
    bool Buy(const std::string& id, std::string* why = nullptr, int ci = 0);
    bool BuySlip(int idx, std::string* why = nullptr);
    float Sell(int idx = -1);                   // at the Fish Market: one fish (an index into the hold) or all (-1); fills lastSale; pays the purse
    float Deliver(int idx = -1, int* rejected = nullptr);   // at the Owners' scales: one fish or every fresh enough one (-1); quota credit; fills lastDelivery
    float QuotaValue(const CatchRec& c) const;  // what the scales credit for it (no glut), 0 if it's too far gone (under 70% fresh)
    // the Gunsmith (design doc v2, "Weapons"; trawl_weapons.h): guns and melee weapons into hand c's slots, damage
    // upgrades and attachments for the weapon in a slot, ammunition into the ship's magazine stock
    bool GunBuy(int c, const std::string& id, std::string* why = nullptr);
    // the Atoll's elder: fish in (150% credit), his goods out
    bool ElderNear(int c, std::string* why = nullptr) const;
    float ElderGive(int c, std::string* why = nullptr);
    bool ElderBuy(int c, const std::string& id, std::string* why = nullptr);
    bool GunUpgrade(int c, int slot, std::string* why = nullptr);
    bool GunAttach(int c, int slot, const std::string& att, std::string* why = nullptr);
    bool AmmoBuy(const std::string& kind, std::string* why = nullptr);
    std::vector<SaleLine> lastDelivery; float lastDeliveryTotal = 0; int lastRejected = 0;
    float Value(const CatchRec& c, float* glut = nullptr, float* bonus = nullptr) const;
    bool CanCastOff(std::string* why = nullptr) const;
    bool CastOff(std::string* why = nullptr);   // coal paid, overnight losses, the night's conditions on the tape
    void Count();                               // the Owners count the quota (after the third night's sale)
    void Continue();                            // (Result) on to the next deadline
    // at sea
    bool InHarbour() const;
    void Step(float dt);                        // the clock, the harbour line, the telegraph's cues, customs
    std::string ClockText() const;              // "23:41"
    void Moor();
};

Vector2 LagoonHarbour(const Eco& e, float* moorHeading, Vector2* moorPos);
int RunTrawlSessionTest();                     // depth.exe --trawl-session-test
int RunTrawlShakedownTest();                   // depth.exe --trawl-shakedown-test (a scripted hand plays the shakedown through)

std::vector<std::string> ElderStock();                    // the Atoll elder's goods: weapon ids, and "att:" attachments

} // namespace tw
