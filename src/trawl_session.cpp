// The Trawl's session loop (see trawl_session.h): deadlines, the quota, the night's clock, the Owners' telegraph,
// the Fish Market, the Chandler and the Slipway, customs; and --trawl-session-test (a bot plays a solo deadline).
#include "trawl_session.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace tw {

// ---------------------------------------------------------------- the numbers (design doc, "The session loop", "Economy")
namespace {
const float QUOTA_BASE = 400;            // shillings for six hands
const float QUOTA_GROWTH = 1.4f, QUOTA_ADD = 60;   // after each met deadline: +40% plus 60 (times the crew scale)
const float START_MONEY = 60;            // (the doc leaves it open; enough for bait and ice on the first night)
const float COAL_TO_REACH_LAGOON = 10;   // kg of coal off the bunker to reach the ground and back
const float CUSTOMS_FINE = 0.10f;        // of money, and the hold seized
const float FIRST_CATCH_BONUS = 1.5f;
const float GLUT_PER_10KG = 0.03f;
const float OVERNIGHT_ICED = 0.75f;      // fish kept overnight in ice lose 25%; un-iced fish rot
const int TOKENS_PER_DEADLINE = 10;
const float NIGHT_END = 540;             // 05:00
}

const char* PhaseName(Phase p) { static const char* N[] = {"dock", "sailing out", "night", "the count", "repossessed"}; return N[(int)p]; }

const std::vector<ShopItem>& ChandlerItems() {   // design doc, "The Chandler" (and the Tackle table's rod prices)
    static const std::vector<ShopItem> I = {
        {"ice", "Ice, 20 kg", 5, "Keeps gutted fish fresh"},
        {"coal", "Coal, a sack", 10, "18 kg: about 3 minutes at half"},
        {"shrimp", "Shrimp, a tin", 4, "10 baits: the light rod's bait"},
        {"squid", "Squid strips, a tin", 4, "10 baits: the heavier rods'"},
        {"chum", "Chum bucket", 15, "40 blood over 60 s: brings fish fast, and what eats them"},
        {"patch", "Patch kit", 25, "Stops one leak"},
        {"watch", "Pocket watch", 15, "Shows the time anywhere"},
        {"medium", "Medium rod", 120, "15 kg line, 25 m cast: most fish to 25 kg"},
        {"heavy", "Heavy boat rod", 300, "40 kg line, 2-speed: tuna, sharks, groupers"},
        {"deepdrop", "Deep-drop reel", 450, "30 kg braid, 200 m straight down"},
    };
    return I;
}
const std::vector<ShopItem>& SlipwayItems() {    // design doc, "The Slipway"
    static const std::vector<ShopItem> I = {
        {"plates", "Hull plates (a section)", 150, "The weakest section's integrity 100 -> 150"},
        {"icehold", "Bigger ice hold", 400, "Hold 600 -> 1,000 kg of iced fish"},
        {"pump", "Second pump", 300, "Bilge rate doubled"},
        {"engine", "Compound engine", 800, "+30% speed, -25% screw noise"},
        {"mast", "Hooded lantern mast", 500, "Adds the searchlight"},
        {"chair", "Big-game chair", 900, "The 120 kg rod station"},
    };
    return I;
}
const std::vector<DockStation>& DockStations() {
    static const std::vector<DockStation> S = {
        {DockKind::Chalkboard, "Chalkboard", {-8.5f, -5.2f}, "The quota, the nights left, the money"},
        {DockKind::Chandler, "The Chandler", {-3.5f, -8.2f}, "Bait, ice, coal, rods"},
        {DockKind::Market, "Fish Market scales", {3.0f, -8.2f}, "Sell the catch"},
        {DockKind::Office, "The Owners' office", {8.0f, -8.2f}, "Salvage (none yet)"},
        {DockKind::Slipway, "The Slipway", {12.0f, -5.2f}, "Refit the Gannet"},
    };
    return S;
}
int NearestDock(Vector2 at, float r) {
    int best = -1; float bd = r * r;
    const auto& S = DockStations();
    for (int i = 0; i < (int)S.size(); i++) { float d = Vector2DistanceSqr(at, S[i].at); if (d < bd) { bd = d; best = i; } }
    return best;
}

// The Lagoon's harbour: the atoll island's quay, half way down its shore. She lies bow north, port side to the quay,
// in water deep enough at low tide.
Vector2 LagoonHarbour(const Eco& e, float* heading, Vector2* moor) {
    float y = e.n * e.cell * 0.5f, x = 0;
    for (x = 0; x < 250; x += 1) if (e.depth[e.CellIdx({x, y})] >= 5.2f) break;
    Vector2 m{x + 3.6f, y};
    if (heading) *heading = -PI / 2;
    if (moor) *moor = m;
    return {m.x + 25, m.y};
}

float Session::QuotaScale() const {
    static const float S[7] = {0.5f, 0.5f, 0.7f, 0.78f, 0.85f, 0.92f, 1.0f};   // solo, two, four and six from the doc; between them interpolated
    return S[std::clamp(players, 1, 6)];
}
void Session::Tape(const std::string& s) {
    tape.push_back(s);
    if (tape.size() > 40) tape.erase(tape.begin());
    if (G) G->Say("The telegraph clatters");
}

void Session::Begin(Gannet& g, Eco& e, int pl, uint32_t sd) {
    *this = Session{};
    G = &g; E = &e; players = std::max(1, pl); seed = sd;
    g.Init(players, seed, Weather::Calm);
    e.Init(ground, seed);
    // the starting gear: two handlines, one light rod, two gaffs, a fish priest, a life ring, a patch kit, 20 kg of ice
    g.ice = 20; g.baitShrimp = 0; g.baitSquid = 0; g.chum = 0;
    for (int t = 0; t < (int)Tackle::COUNT; t++) g.owned[t] = t <= (int)Tackle::Light;
    for (auto& r : g.rods) {
        r.tackle = Stations()[r.station].kind == StationKind::PortRod ? Tackle::Light : Tackle::Handline;
        r.line = LineType::Mono; r.hook = Hook::Small; r.fight.drag = 0.33f * TackleOf(r.tackle).strength;
    }
    if (!g.crew.empty()) g.crew[0].patchKits = 1;
    money = START_MONEY;
    quota = QUOTA_BASE * QuotaScale();
    harbour = LagoonHarbour(e, &g.moorHeading, &g.moorPos);
    Moor();
    g.crew[0].p = {0, -6.5f};   // on the quay, by the gangplank
    BeginDeadline();
}
void Session::BeginDeadline() {
    night = 0; sold = 0; glutKg.clear(); phase = Phase::Dock;
    Tape(TextFormat("QUOTA %.0f SHILLINGS STOP THREE NIGHTS STOP THE OWNERS ARE CONFIDENT STOP", quota));
}
void Session::Moor() {
    G->moored = true;
    G->boat.pos = G->moorPos; G->boat.heading = G->moorHeading; G->boat.vel = {0, 0}; G->boat.yawRate = 0;
    G->boat.telegraph = 0; G->boat.rudder = 0; G->boat.aground = false;
    for (auto& r : G->rods) if (r.state != RodState::Idle) { r.state = RodState::Idle; r.bite = Bite{}; }   // lines in
}
bool Session::InHarbour() const { return Vector2Distance(G->boat.pos, harbour) < harbourR; }
std::string Session::ClockText() const {
    int m = (int)clock, h = (20 + m / 60) % 24;
    return TextFormat("%02d:%02d", h, m % 60);
}

// ---------------------------------------------------------------- the dock
float Session::Value(const CatchRec& c, float* glut, float* bonus) const {
    std::string base = c.name.substr(0, c.name.find(" ("));
    auto it = glutKg.find(base);
    float g = std::max(0.2f, 1 - GLUT_PER_10KG * (it == glutKg.end() ? 0 : it->second) / 10);
    float b = c.first ? FIRST_CATCH_BONUS : 1;
    if (glut) *glut = g;
    if (bonus) *bonus = b;
    return c.price * c.kg * c.grade * c.fresh * g * b;
}
float Session::Sell() {
    lastSale.clear(); lastSaleTotal = 0;
    for (const auto& c : G->hold) {
        float g, b, v = Value(c, &g, &b);
        lastSale.push_back({c.name, c.kg, c.price, c.grade, c.fresh, g, b, v});
        glutKg[c.name.substr(0, c.name.find(" ("))] += c.kg;   // (the glut counts after each fish: a big haul drives its own price down)
        lastSaleTotal += v;
    }
    G->hold.clear();
    money += lastSaleTotal; sold += lastSaleTotal;
    if (!lastSale.empty()) Tape(TextFormat("FISH MARKET PAID %.0f SHILLINGS STOP", lastSaleTotal));
    return lastSaleTotal;
}
bool Session::Buy(const std::string& id, std::string* why) {
    const ShopItem* it = nullptr;
    for (const auto& s : ChandlerItems()) if (id == s.id) it = &s;
    if (!it) { if (why) *why = "not sold here"; return false; }
    auto owns = [&](Tackle t) { return G->owned[(int)t]; };
    if ((id == "medium" && owns(Tackle::Medium)) || (id == "heavy" && owns(Tackle::Heavy)) || (id == "deepdrop" && owns(Tackle::DeepDrop)) || (id == "watch" && G->watch)) { if (why) *why = "already aboard"; return false; }
    if (id == "ice" && G->ice + 20 > G->iceCap) { if (why) *why = "the ice hold is full"; return false; }
    if (money < it->price) { if (why) *why = "not enough money"; return false; }
    money -= it->price;
    if (id == "ice") G->ice += 20;
    else if (id == "coal") G->boat.bunker += D().sackKg;
    else if (id == "shrimp") G->baitShrimp += 10;
    else if (id == "squid") G->baitSquid += 10;
    else if (id == "chum") G->chum++;
    else if (id == "patch") G->crew[0].patchKits++;
    else if (id == "watch") G->watch = true;
    else if (id == "medium") G->owned[(int)Tackle::Medium] = true;
    else if (id == "heavy") G->owned[(int)Tackle::Heavy] = true;
    else if (id == "deepdrop") G->owned[(int)Tackle::DeepDrop] = true;
    return true;
}
bool Session::BuySlip(int idx, std::string* why) {
    const auto& I = SlipwayItems();
    if (idx < 0 || idx >= (int)I.size()) return false;
    std::string id = I[idx].id;
    if (id != "plates" && slip[idx]) { if (why) *why = "already fitted"; return false; }
    Boat& b = G->boat;
    int weakest = 0; for (int s = 1; s < SEC_COUNT; s++) if (b.integrityMax[s] < b.integrityMax[weakest]) weakest = s;
    if (id == "plates" && b.integrityMax[weakest] >= 150) { if (why) *why = "every section is plated"; return false; }
    if (money < I[idx].price) { if (why) *why = "not enough money"; return false; }
    money -= I[idx].price; slip[idx] = true;
    if (id == "plates") { b.integrityMax[weakest] = 150; b.integrity[weakest] = 150; }
    else if (id == "icehold") G->iceCap = 1000;
    else if (id == "pump") G->secondPump = true;
    else if (id == "engine") { b.thrustMult = 1.3f; b.noiseMult = 0.75f; }
    else if (id == "mast") G->searchlight = true;
    else if (id == "chair") G->owned[(int)Tackle::Chair] = true;
    return true;
}
bool Session::CanCastOff(std::string* why) const {
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    if (phase != Phase::Dock) return no("she's not at the quay");
    if (night >= 3) return no("three nights done: hand in to the Owners at the chalkboard");
    if (G->boat.bunker < COAL_TO_REACH_LAGOON) return no("not enough coal in the bunker to reach the ground");
    for (const auto& c : G->crew) if (!c.overboard && c.deck == 0 && c.p.y < -3.0f) return no("all hands aboard first");
    return true;
}
bool Session::CastOff(std::string* why) {
    if (!CanCastOff(why)) return false;
    G->boat.bunker -= COAL_TO_REACH_LAGOON;
    // fish kept from an earlier night: iced lose a quarter, un-iced have rotted
    for (auto& c : G->hold) c.fresh = c.iced ? c.fresh * OVERNIGHT_ICED : 0;
    // the night's conditions (rolled per night and ground) and a rumour on the tape (right 70% of the time)
    uint32_t h = seed * 2654435761u + (uint32_t)(deadline * 31 + night * 7);
    auto R = [&]() { h = h * 1664525u + 1013904223u; return (h >> 8) * (1.0f / 16777216.0f); };
    float w = R();
    weather = w < 0.5f ? Weather::Calm : w < 0.7f ? Weather::Fog : w < 0.9f ? Weather::Rain : Weather::Squall;
    moon = fmodf(0.5f + (deadline * 3 + night) * 0.14f, 1.0f);
    G->sea.Set(weather, h);
    static const char* WX[] = {"CALM", "FOG", "RAIN", "SQUALL", "STORM", "GLASS"};
    const char* rumours[] = {"WHALERS REPORT BARRACUDA THICK ON THE CREST", "SARDINE BALLS SEEN OFF THE FLATS", "REEF SHARKS QUIET THIS WEEK",
                             "MAHI UNDER THE WEED RAFTS", "SNAPPER BITING ON SHRIMP"};
    Tape(TextFormat("LAGOON %s STOP MOON %s STOP %s STOP", WX[(int)weather], moon < 0.25f ? "NEW" : moon < 0.5f ? "WAXING" : moon < 0.75f ? "FULL" : "WANING",
                    rumours[(int)(R() * 5) % 5]));
    // the ground remembers across a deadline's nights; a new deadline starts it afresh
    if (night == 0) E->Init(ground, seed + deadline * 97);
    else E->Day(15);
    E->StartNight();
    G->eco = nullptr;                    // (the night and the web start at the harbour line)
    G->moored = false;
    G->boat.lantern = std::min(G->boat.lantern, G->searchlight ? 3 : 2);
    phase = Phase::SailOut; clock = 0; clockOn = false;
    for (bool& c : cues) c = false;
    return true;
}
void Session::Count() {
    if (phase != Phase::Dock || night < 3) return;
    met = sold >= quota;
    if (met) {
        tokens += TOKENS_PER_DEADLINE;
        float next = quota * QUOTA_GROWTH + QUOTA_ADD * QuotaScale();
        Tape(TextFormat("QUOTA MET STOP NEW QUOTA %.0f STOP THE OWNERS EXPECTED NOTHING LESS STOP", next));
        phase = Phase::Result;
    } else {
        Tape("QUOTA NOT MET STOP GANNET REPOSSESSED STOP CREW RELEASED WITHOUT REFERENCE STOP");
        phase = Phase::Over;
    }
}
void Session::Continue() {
    if (phase != Phase::Result) return;
    deadline++;
    quota = quota * QUOTA_GROWTH + QUOTA_ADD * QuotaScale();
    BeginDeadline();
}

// ---------------------------------------------------------------- at sea
void Session::Step(float dt) {
    // a first of its kind is noted by the Owners the moment it's aboard
    for (auto& c : G->hold) {
        std::string base = c.name.substr(0, c.name.find(" ("));
        if (!c.first && !catchLog.count(base) && c.sp >= 0) { c.first = true; catchLog.insert(base); Tape("SPECIMEN NOTED STOP BONUS AUTHORISED STOP DO NOT BRUISE STOP"); }
    }
    switch (phase) {
        case Phase::SailOut:
            if (!InHarbour()) {
                // past the harbour line: the night begins
                phase = Phase::Night; clockOn = true; clock = 0;
                G->eco = E;
            }
            break;
        case Phase::Night: {
            clock += dt;
            if (clock >= 240 && !cues[0]) { cues[0] = true; Tape("MIDNIGHT STOP HOLD INSPECTED AT HARBOUR LINE 0500 STOP"); }
            if (clock >= 480 && !cues[1]) { cues[1] = true; Tape("ONE HOUR STOP CUSTOMS CUTTER ON STATION STOP"); }
            bool damaged = false; for (int s = 0; s < SEC_COUNT; s++) if (G->boat.integrity[s] < D().leakBelow) damaged = true;
            if (damaged && !cues[2]) { cues[2] = true; Tape("KINDLY REFRAIN FROM DAMAGING OWNERS PROPERTY STOP"); }
            if (clock > 1 && InHarbour()) {
                Tape(TextFormat("GANNET IN HARBOUR %s STOP", ClockText().c_str()));
                night++; clockOn = false; G->eco = nullptr; Moor(); phase = Phase::Dock;
            } else if (clock >= NIGHT_END) {
                // the customs cutter: the hold seized, a tenth of the money fined, the Gannet towed in
                float fine = money * CUSTOMS_FINE;
                money -= fine;
                int n = (int)G->hold.size(); G->hold.clear();
                Tape(TextFormat("0500 STOP GANNET OUTSIDE HARBOUR LINE STOP HOLD SEIZED %d FISH STOP FINE %.0f SHILLINGS STOP", n, fine));
                night++; clockOn = false; G->eco = nullptr; Moor(); phase = Phase::Dock;
                for (auto& c : G->crew) if (!c.overboard) { c.deck = 0; c.station = -1; c.p = {-1.0f, 0.8f}; }
            }
            break;
        }
        default: break;
    }
}

// ---------------------------------------------------------------- --trawl-session-test
namespace {
// the test's hands: an autopilot for the helm, a fireman who keeps steam up, an angler at one rod and a gutter;
// solo, the bot "teleports" between stations (a real hand would walk)
struct Bot {
    Session& S; Gannet& G;
    uint32_t rng = 99;
    explicit Bot(Session& s) : S(s), G(*s.G) {}
    void Steam() { if (G.boat.pressure < D().greenLo + 0.15f && G.boat.firebox < 5) G.boat.Shovel(D().shovelKg * 0.05f); }
    void SteerTo(Vector2 t, float slowWithin) {
        Vector2 d = Vector2Subtract(t, G.boat.pos);
        float want = atan2f(d.y, d.x), err = want - G.boat.heading;
        while (err > PI) err -= 2 * PI;
        while (err < -PI) err += 2 * PI;
        G.boat.rudder = std::clamp(err * 2.5f, -1.0f, 1.0f);
        float dist = Vector2Length(d);
        G.boat.telegraph = dist > slowWithin ? 2 : dist > 6 ? 1 : 0;
    }
    int StationOf(StationKind k) { for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == k) return i; return -1; }
};
struct NightReport { int landed = 0; float soldAt = 0; bool customs = false; };
NightReport PlayNight(Session& S, Bot& b, bool stayLate) {
    Gannet& G = *S.G;
    NightReport rep;
    const float dt = 1 / 60.0f;
    // the dock: bait and ice with what we have
    while (G.baitShrimp < 20 && S.Buy("shrimp")) {}
    while (G.ice < 60 && S.Buy("ice")) {}
    while (G.boat.bunker < 40 && S.Buy("coal")) {}
    G.crew[0].p = {-1, 0.8f}; G.crew[0].station = -1;
    std::string why;
    if (!S.CastOff(&why)) { printf("    cast off refused: %s\n", why.c_str()); return rep; }
    // out to the fishing spot: east over the basin, 120 m past the harbour mouth
    Vector2 spot = Vector2Add(S.harbour, {140, 20});
    int rodSt = b.StationOf(StationKind::PortRod), gutSt = b.StationOf(StationKind::Gutting);
    int ri = G.RodAt(rodSt);
    G.rods[ri].botAngler = true;
    float t = 0; size_t h0 = 0;
    bool homeward = false, onSpot = false;
    while (S.phase == Phase::SailOut || S.phase == Phase::Night) {
        b.Steam();
        float leave = stayLate ? 600.0f : 440.0f;   // (a careful skipper turns for home at 03:20)
        if (S.phase == Phase::Night && S.clock > leave) homeward = true;
        if (homeward) { b.SteerTo(G.moorPos, 25); G.crew[0].station = -1; G.rods[ri].state = RodState::Idle; }
        else if (!onSpot) {
            b.SteerTo(spot, 30);
            if (Vector2Distance(G.boat.pos, spot) <= 15) onSpot = true;   // stop engines and fish where she lies
        } else if (Vector2Distance(G.boat.pos, spot) > 45) onSpot = false;   // drifted off: steam back
        else {
            G.boat.telegraph = 0; G.boat.rudder = 0;
            Rod& r = G.rods[ri];
            // gut what's on deck between fights; otherwise fish the port rod
            if (G.DeckFish() > 0 && r.state == RodState::Idle) { G.crew[0].p = Stations()[gutSt].at; G.crew[0].station = gutSt; G.Primary(0, true, dt); }
            else {
                G.crew[0].p = Stations()[rodSt].at; G.crew[0].station = rodSt;
                bool cast = r.state == RodState::Idle && fmodf(t, 2.0f) < 0.9f;
                Vector2 aim = Vector2Add(r.TipDeck(), {1, -9});
                G.RodInput(0, cast, aim, false, r.bite.stage == BiteStage::Take, 0, false, false, 0);
            }
        }
        G.Step(dt); S.Step(dt); t += dt;
        if (getenv("DEPTH_TRACE") && fmodf(t, 20) < dt) printf("      t%4.0f %s clock %s pos (%.0f,%.0f) spot d %.0f v %.1f tel %d p %.2f bunker %.0f aground %d rod %d bait %d\n", t, PhaseName(S.phase), S.ClockText().c_str(), G.boat.pos.x, G.boat.pos.y, Vector2Distance(G.boat.pos, spot), Vector2Length(G.boat.vel), G.boat.telegraph, G.boat.pressure, G.boat.bunker, G.boat.aground, (int)G.rods[ri].state, G.baitShrimp);
        if (G.hold.size() > h0) { rep.landed += (int)(G.hold.size() - h0); }
        h0 = G.hold.size();
        if (t > 900) break;
    }
    rep.customs = G.hold.empty() && rep.landed > 0 && S.tape.back().find("SEIZED") != std::string::npos;
    // home: gut the rest, then the Fish Market
    for (int k = 0; k < 60 * 60 && G.DeckFish() > 0; k++) { G.crew[0].station = gutSt; G.Primary(0, true, dt); G.Step(dt); }
    G.crew[0].station = -1;
    rep.soldAt = S.Sell();
    return rep;
}
} // namespace

int RunTrawlSessionTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Trawl stage 4: the session loop\n");
    // the rules, one at a time
    {
        Gannet g; Eco e; Session s; s.Begin(g, e, 1, 5);
        check(fabsf(s.quota - 200) < 0.01f && s.phase == Phase::Dock && g.moored, TextFormat("a solo run starts at the quay with a quota of %.0f (400 x 0.5)", s.quota));
        check(!s.tape.empty() && s.tape.back().find("QUOTA 200 SHILLINGS") != std::string::npos, "the Owners' first tape: QUOTA 200 SHILLINGS STOP THREE NIGHTS STOP");
        Session s6; Gannet g6; Eco e6; s6.Begin(g6, e6, 6, 5);
        check(fabsf(s6.quota - 400) < 0.01f, "six hands: 400");
        CatchRec c; c.name = "snapper"; c.kg = 2; c.price = 3; c.grade = 1; c.fresh = 0.9f;
        check(fabsf(s.Value(c) - 5.4f) < 0.01f, TextFormat("a 2 kg snapper at 3 sh/kg and 90%% fresh is worth %.2f", s.Value(c)));
        c.first = true; check(fabsf(s.Value(c) - 8.1f) < 0.01f, "the run's first snapper pays 50% more");
        c.first = false; s.glutKg["snapper"] = 50;
        check(fabsf(s.Value(c) - 5.4f * 0.85f) < 0.01f, "after 50 kg of snapper sold this deadline its price is down 15%");
        s.glutKg.clear();
        CatchRec hd = c; hd.name = "snapper (head)"; hd.grade = 0.9f;
        check(fabsf(s.Value(hd) - 5.4f * 0.9f) < 0.01f, "a fish bitten on the line is graded 10% down");
        // buying
        float m0 = s.money;
        check(s.Buy("shrimp") && g.baitShrimp == 10 && fabsf(s.money - (m0 - 4)) < 0.01f, "a tin of shrimp: 10 baits for 4 shillings");
        std::string why;
        check(!s.Buy("heavy", &why) && why == "not enough money", "a heavy rod is out of reach with the starting money");
        s.money = 1000;
        check(s.Buy("heavy") && g.owned[(int)Tackle::Heavy], "bought, the heavy rod goes into the rack");
        check(s.BuySlip(2) && g.secondPump, "the Slipway fits a second pump");
        check(s.BuySlip(0) && g.boat.integrityMax[0] == 150, "hull plates raise a section to 150");
        // no casting off with a hand ashore
        g.crew[0].p = {0, -6};
        check(!s.CanCastOff(&why) && why == "all hands aboard first", "she won't cast off with a hand on the quay");
        g.crew[0].p = {-1, 0.8f};
        // overnight: iced fish lose a quarter, un-iced rot
        CatchRec a; a.name = "grunt"; a.kg = 1; a.price = 1.5f; a.iced = true; a.gutted = true; a.fresh = 0.96f;
        CatchRec r2 = a; r2.iced = false;
        g.hold = {a, r2};
        why.clear();
        check(s.CastOff(&why), "casting off");
        check(fabsf(g.hold[0].fresh - 0.72f) < 0.001f && g.hold[1].fresh == 0, "fish kept overnight: iced ones lose 25%, un-iced ones rot");
        check(s.phase == Phase::SailOut && !s.clockOn, "the clock waits at 20:00 until she clears the harbour line");
        // out past the line: the night starts
        g.boat.pos = Vector2Add(s.harbour, {s.harbourR + 5, 0}); g.hold.clear();
        g.Step(1 / 60.0f); s.Step(1 / 60.0f);
        check(s.phase == Phase::Night && s.clockOn && g.eco == &e, "past the harbour line the night begins and the web wakes");
        // 05:00 still outside: the customs cutter
        CatchRec f; f.name = "snapper"; f.kg = 3; f.price = 3; g.hold = {f};
        float money = s.money;
        s.clock = 539.9f; s.Step(0.2f);
        check(s.phase == Phase::Dock && g.hold.empty() && fabsf(s.money - money * 0.9f) < 0.01f && s.night == 1, "at 05:00 outside the line the cutter seizes the hold and fines 10%");
        // the count
        s.night = 3; s.sold = 250;
        s.Count();
        check(s.phase == Phase::Result && s.met && s.tokens == 10, "250 sold against 200: QUOTA MET, 10 arcade tokens");
        s.Continue();
        check(fabsf(s.quota - (200 * 1.4f + 30)) < 0.01f && s.deadline == 2 && s.night == 0, TextFormat("the next quota is %.0f (+40%% and 60 x 0.5)", s.quota));
        s.night = 3; s.sold = 10; s.Count();
        check(s.phase == Phase::Over, "short of it: GANNET REPOSSESSED, the run is over");
    }
    // a whole solo deadline played by a bot: bait, cast off, fish the port rod, gut and ice, home before 05:00, sell
    {
        Gannet g; Eco e; Session s; s.Begin(g, e, 1, 20261);
        Bot b(s);
        float total = 0; int fish = 0; bool customs = false, allHome = true;
        for (int n = 0; n < 3; n++) {
            NightReport r = PlayNight(s, b, false);
            printf("    night %d: %d fish landed, sold for %.0f; money %.0f; the tape: %s\n", n + 1, r.landed, r.soldAt, s.money, s.tape.back().c_str());
            total += r.soldAt; fish += r.landed; customs |= r.customs; allHome &= s.phase == Phase::Dock;
        }
        check(allHome && !customs && s.night == 3, "three nights out and back through the harbour line before 05:00");
        check(fish >= 6, TextFormat("one hand at one rod landed %d fish over the deadline", fish));
        s.Count();
        printf("    the count: %.0f sold against a quota of %.0f: %s\n", s.sold, s.quota, s.met ? "met" : "missed");
        check(s.phase == Phase::Result || s.phase == Phase::Over, "the Owners count the quota after the third night");
        // staying out past 05:00
        Gannet g2; Eco e2; Session s2; s2.Begin(g2, e2, 1, 7);
        Bot b2(s2);
        NightReport late = PlayNight(s2, b2, true);
        check(s2.tape.size() > 0 && (late.landed == 0 || late.customs), "a skipper who stays past 05:00 loses the hold to the cutter");
    }
    printf(fails ? "trawl-session-test: %d check(s) failed\n" : "trawl-session-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}

} // namespace tw
