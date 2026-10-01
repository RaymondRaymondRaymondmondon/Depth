// The Trawl's session loop (see trawl_session.h): deadlines, the quota, the night's clock, the Owners' telegraph,
// the Fish Market, the Chandler and the Slipway, customs; and --trawl-session-test (a bot plays a solo deadline).
#include "trawl_session.h"
#include "trawl_weapons.h"
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
const char* VariantName(Variant v) { static const char* N[(int)Variant::COUNT] = {"an ordinary night", "Bait run", "Red tide", "King tide", "Turtle nesting", "Canoe night"}; return N[(int)v]; }
const char* VariantNote(Variant v) {
    static const char* N[(int)Variant::COUNT] = {
        "",
        "Forage fish three deep at the surface; every predator after them, and bolder by midnight",
        "The forage is dying and floating: blood all over the ground, hungry predators that take any bait, and fish from here sell at half",
        "The crest stays under all night: open-sea fish come into the lagoon",
        "Turtles everywhere (a turtle in the net is a fine), and the sharks that follow them",
        "Island war canoes are out: when one comes alongside, trade, pay tribute, or refuse them",
    };
    return N[(int)v];
}

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
        {"knife", "Knife", 10, "Cuts a snagged net loose"},
        {"bandage", "Bandages (3)", 10, "Stop a bite bleeding"},
        {"ring", "Extra life ring", 40, "Thrown on a rope"},
        {"longline", "Longline", 60, "20 hooks, two buoys: set it, fish elsewhere, haul it"},
        {"pot", "Crab pot", 25, "Reusable: crabs, lobster, octopus"},
        {"flare", "Flare pistol (3 flares)", 40, "A 40 m arc of light"},
        {"flares", "Flares (6)", 20, "A box of six"},
        {"speargun", "Speargun (3 spears)", 80, "8 m in water, 12 m in air, tethered"},
        {"spears", "Spears (10)", 12, "A quiver of ten"},
        {"rifle", "Rifle (10 rounds)", 140, "Fish breaking the surface, gulls, boarders"},
        {"rounds", "Rounds (30)", 15, "A box of thirty"},
        {"shotgun", "Shotgun (8 shells)", 120, "15 m in air: gull flocks"},
        {"shells", "Shells (24)", 18, "A box of two dozen"},
        {"charge", "Depth charge", 120, "12 m blast; the Owners fine 30 in the Lagoon"},
        {"explosive", "Explosive harpoon head", 80, "For the bow cannon: kills, but ruins the fish"},
        {"bosslure", "Boss lure (the Lagoon)", 60, "Calls a mini-boss over boss water (the Crest Pass); +10 Wake"},
        {"tag", "Tag gun", 30, "Tag a protected catch before it goes back: the naturalist wants them"},
        {"coin", "Lucky coin (a charm)", 50, "Worn on a cord: Glimmer variants twice as likely"},
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
        {"harpoon", "Harpoon cannon", 700, "Bow cannon and winch: 35 m, to 6 m deep, tethered"},
        {"bignet", "Bigger trawl", 600, "Mouth width +50%, weight +50%"},
        {"net", "A new trawl net", 150, "Replaces one cut away (the Owners' price)"},
    };
    return I;
}
const std::vector<DockStation>& DockStations() {
    static const std::vector<DockStation> S = {
        {DockKind::Chalkboard, "Chalkboard", {-8.5f, -5.2f}, "The quota, the nights left, the money"},
        {DockKind::Chandler, "The Chandler", {-3.5f, -8.2f}, "Bait, ice, coal, rods"},
        {DockKind::Market, "The Fish Market", {1.0f, -8.2f}, "Sell fish for the ship's purse"},
        {DockKind::Scales, "The Owners' quota scales", {4.6f, -8.2f}, "Deliver fish against the quota"},
        {DockKind::Gunsmith, "The Gunsmith", {-7.6f, -8.2f}, "Guns, upgrades, attachments and ammunition"},
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
    g.GiveStartingKit();
    money = START_MONEY;
    quota = QUOTA_BASE * QuotaScale();
    harbour = LagoonHarbour(e, &g.moorHeading, &g.moorPos);
    Moor();
    g.crew[0].p = {0, -6.5f};   // on the quay, by the gangplank
    BeginDeadline();
}
void Session::BeginDeadline() {
    night = 0; sold = carried; carried = 0; glutKg.clear(); phase = Phase::Dock;
    if (G) { G->bossCaught = 0; G->spiceRub = false; }
    RollRequests();
    Tape(TextFormat("QUOTA %.0f SHILLINGS STOP THREE NIGHTS STOP THE OWNERS ARE CONFIDENT STOP", quota));
}
void Session::Moor() {
    G->moored = true;
    G->boat.pos = G->moorPos; G->boat.heading = G->moorHeading; G->boat.vel = {0, 0}; G->boat.yawRate = 0;
    G->boat.telegraph = 0; G->boat.rudder = 0; G->boat.aground = false;
    for (auto& r : G->rods) if (r.state != RodState::Idle) { r.state = RodState::Idle; r.bite = Bite{}; }   // lines in
    G->harpoon.state = RodState::Idle; G->shots.clear(); G->floaters.clear(); G->flares.clear();
    if (G->net.state != NetState::Stowed && G->net.state != NetState::Lost) { G->net = Trawl{}; }   // (the net comes in with her)
    for (auto& r : G->rings) { r.state = 0; r.holder = -1; r.thrower = -1; }
    for (auto& c : G->crew) for (auto& sl : c.slots) if (sl.it == Item::Ring) sl.ammo = 1;
    // left behind (design doc v2, "Guiding the skiff"): anyone not aboard her when she crosses the harbour line is lost
    // for the night (dead, body lost, the Owners' fine); a skiff left out is towed in by the harbour launch for 40
    for (auto& c : G->crew) if (!c.dead && (c.deck >= DECK_SKIFF || c.overboard) && !shake.on) {
        c.dead = true; c.bodyLost = true; c.cause = "left behind";
        G->fines += 25; Tape("HAND LEFT BEHIND AT SEA STOP FINED 25 STOP");
    }
    if (G->skiff.state != SkiffState::Stowed) {
        if (!shake.on) { G->fines += 40; Tape("SKIFF RECOVERED BY HARBOUR LAUNCH STOP 40 STOP"); }
        G->skiff = Skiff{};
    }
    // at the dock the dead revive and the injured are seen to; a body lost to the sea costs 8% for a replacement hand
    for (auto& c : G->crew) {
        if (c.dead && c.bodyLost && money > 0 && !shake.on) { float f = money * 0.08f; money -= f; Tape(TextFormat("HAND DECEASED STOP REPLACEMENT CHARGED %.0f SHILLINGS STOP", f)); }
        if (c.dead || c.overboard || c.deck >= DECK_SKIFF) { c.p = {-1.0f, 0.8f}; c.deck = 0; }
        c.dead = false; c.bodyLost = false; c.overboard = false; c.injuries = 0; c.serious = 0; c.drownT = 0; c.station = -1;
    }
    if (G->fines > 0) { money -= G->fines; Tape(TextFormat("FINES DEDUCTED %.0f SHILLINGS STOP", G->fines)); G->fines = 0; }
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
    static float glutK = getenv("DEPTH_GLUT") ? (float)atof(getenv("DEPTH_GLUT")) : GLUT_PER_10KG;   // (the tuning grid)
    float g = std::max(0.2f, 1 - glutK * (it == glutKg.end() ? 0 : it->second) / 10);
    float b = c.first ? FIRST_CATCH_BONUS : 1;
    if (c.junk) { if (glut) *glut = 1; if (bonus) *bonus = 1; return c.price; }   // junk: a flat price, no freshness or glut
    if (c.boss >= 0) { if (glut) *glut = 1; if (bonus) *bonus = 1; return c.price * c.kg * c.killScore * c.cook; }   // a mini-boss: its flat value, times the Killscore and cooking
    if (glut) *glut = g;
    if (bonus) *bonus = b;
    return c.price * c.kg * c.grade * c.killScore * c.cook * c.fresh * g * b * (c.glimmer ? 3.0f : 1.0f) * (variant == Variant::RedTide ? 0.5f : 1.0f);   // fish from a red tide sell at half; cooked ashore up to 1.5x
}
// Canoe night's answer (design doc, "Eclipse Lagoon": canoes "trade fish for gear, or raid for it"). Trade: a quarter of
// the hold's weight, heaviest first, for two tins of bait, 20 kg of ice and a patch kit. Tribute: a tenth of the money,
// 15 at least. Refuse (or let the minute run out): they snatch every fish still on the deck and shove a hand over the rail.
bool Session::Canoe(int choice) {
    if (canoe != CanoeState::Alongside) return false;
    canoe = CanoeState::Gone;
    if (choice == CANOE_TRADE) {
        float total = 0; for (const auto& h : G->hold) total += h.kg;
        float give = total * 0.25f, given = 0;
        std::sort(G->hold.begin(), G->hold.end(), [](const CatchRec& a, const CatchRec& b) { return a.kg > b.kg; });
        while (!G->hold.empty() && given < give) { given += G->hold.front().kg; G->hold.erase(G->hold.begin()); }
        G->baitShrimp += 10; G->baitSquid += 10; G->ice = std::min(G->iceCap, G->ice + 20); if (!G->crew.empty()) G->crew[0].patchKits++;
        canoeWord = TextFormat("Traded %.0f kg of fish for bait, ice and a patch kit", given);
        Tape("CANOES TRADED STOP OWNERS DISAPPROVE STOP");
    } else if (choice == CANOE_TRIBUTE) {
        float pay = std::max(15.0f, money * 0.10f);
        money -= pay;
        canoeWord = TextFormat("Paid %.0f shillings of tribute", pay);
        Tape(TextFormat("TRIBUTE PAID %.0f STOP NOTED STOP", pay));
    } else {
        int took = 0;
        for (size_t i = 0; i < G->hold.size();) { if (!G->hold[i].gutted) { G->hold.erase(G->hold.begin() + i); took++; } else i++; }
        int shove = -1; float best = -1;
        for (int k = 0; k < (int)G->crew.size(); k++) { const Crew& c = G->crew[k]; if (c.dead || c.overboard || c.deck != 0) continue; float e = fabsf(c.p.y); if (e > best) { best = e; shove = k; } }
        if (shove >= 0 && best > 1.4f) G->GoOverboard(shove, "shoved over the rail by the canoe's crew");
        G->foughtCanoes = true;
        canoeWord = TextFormat("Refused: they snatched %d fish off the deck%s", took, shove >= 0 && best > 1.4f ? " and a hand went over the rail" : "");
        Tape("CANOES RAIDED STOP");
    }
    G->Say(canoeWord);
    return true;
}
float Session::Sell(int idx) {
    // the Fish Market: shillings into the purse, glut applies; it counts nothing toward the quota
    lastSale.clear(); lastSaleTotal = 0;
    for (int i = (int)G->hold.size() - 1; i >= 0; i--) {
        if (idx >= 0 && i != idx) continue;
        const CatchRec& c = G->hold[i];
        float g, b, v = Value(c, &g, &b);
        lastSale.push_back({c.name, c.kg, c.price, c.grade, c.fresh, g, b, v, c.src});
        glutKg[c.name.substr(0, c.name.find(" ("))] += c.kg;   // (the glut counts after each fish: a big haul drives its own price down)
        lastSaleTotal += v;
        G->hold.erase(G->hold.begin() + i);
    }
    money += lastSaleTotal;
    if (!lastSale.empty()) Tape(TextFormat("FISH MARKET PAID %.0f SHILLINGS STOP", lastSaleTotal));
    return lastSaleTotal;
}
float Session::QuotaValue(const CatchRec& c) const {
    if (c.fresh < QUOTA_MIN_FRESH || c.bycatch || c.junk) return 0;
    float b = c.first ? FIRST_CATCH_BONUS : 1;
    if (c.boss >= 0) return c.price * c.kg * c.killScore * c.cook;
    return c.price * c.kg * c.grade * c.killScore * c.cook * c.fresh * b * (c.glimmer ? 3.0f : 1.0f) * (variant == Variant::RedTide ? 0.5f : 1.0f);   // (the scales ignore glut)
}
// ---------------------------------------------------------------- the Gunsmith
bool Session::GunBuy(int ci, const std::string& id, std::string* why) {
    auto no = [&](const char* m) { if (why) *why = m; return false; };
    int wi = WeaponIndex(id);
    if (wi < 0) return no("not in the catalogue");
    const WeaponDef& w = Weapons()[wi];
    bool here = (w.where == "gunsmith" || (w.where == "gunsmith3" && deadline >= 3)) && w.cls != WC_THROWN;   // (thrown weapons wait for throwing: the depth charge stays at the Chandler)
    if (!here) return no(w.where == "gunsmith3" ? "the Gunsmith gets those in from the third deadline" : "not sold at the Gunsmith");
    if (ci < 0 || ci >= (int)G->crew.size()) return no("no such hand");
    if (money < w.price) return no("not enough money");
    Crew& c = G->crew[ci];
    if (w.slots >= 2) for (const auto& s : c.slots) if (s.it == Item::Weapon && s.wpn >= 0 && Weapons()[s.wpn].slots >= 2) return no("a hand carries one long weapon at most");
    Slot ns; ns.it = Item::Weapon; ns.wpn = wi; ns.ammo = w.mag;   // (it comes loaded)
    money -= w.price;
    for (auto& s : c.slots) if (s.it == Item::None) { s = ns; Tape(TextFormat("GUNSMITH SOLD %s STOP", w.name.c_str())); return true; }
    G->locker.push_back(ns);
    Tape(TextFormat("GUNSMITH SOLD %s STOP IN THE LOCKER STOP", w.name.c_str()));
    return true;
}
// The Atoll's elder (design doc v2, "Islands"): he takes only fish, never shillings, at 150% of their value, and
// trades his own goods for them (sold nowhere else); a crew that fought the canoes finds him closed
bool Session::ElderNear(int ci, std::string* why) const {
    auto no = [&](const char* m) { if (why) *why = m; return false; };
    if (ci < 0 || ci >= (int)G->crew.size()) return no("no such hand");
    const Crew& c = G->crew[ci];
    if (c.deck != DECK_SHORE || G->skiff.landing < 0 || G->skiff.landing >= (int)G->landings.size()) return no("the elder is on the Atoll");
    if (Vector2Distance(c.p, G->landings[G->skiff.landing].elder) > 2.2f) return no("go to the elder's shrine");
    if (G->foughtCanoes) return no("the elder turns his back: you fought his people's canoes");
    return true;
}
float Session::ElderGive(int ci, std::string* why) {
    if (!ElderNear(ci, why)) return 0;
    Crew& c = G->crew[ci];
    if (!c.carrying || c.carry.junk) { if (why) *why = "he takes only fish"; return 0; }
    float v = Value(c.carry) * 1.5f;
    G->landings[G->skiff.landing].elderCredit += v;
    G->Say(TextFormat("The elder takes the %s: %.0f in trade", c.carry.name.c_str(), v));
    c.carrying = false; c.carryKg = 0;
    return v;
}
std::vector<std::string> ElderStock() {
    std::vector<std::string> s;
    for (const auto& w : Weapons()) if (w.where == "atoll") s.push_back(w.id);
    for (const auto& a : Attachments()) if (a.where == "atoll") s.push_back("att:" + a.id);
    s.push_back("charm:shark"); s.push_back("charm:anklet");   // (his charms: the shark tooth 120, the tribal anklet 90)
    return s;
}
bool Session::ElderBuy(int ci, const std::string& id, std::string* why) {
    auto no = [&](const char* m) { if (why) *why = m; return false; };
    if (!ElderNear(ci, why)) return false;
    Landing& L = G->landings[G->skiff.landing];
    Crew& c = G->crew[ci];
    if (id.rfind("charm:", 0) == 0) {
        int ch = id == "charm:shark" ? CH_SHARK_TOOTH : CH_ANKLET; int price = ch == CH_SHARK_TOOTH ? 120 : 90;
        if (L.elderCredit < price) return no("give him more fish first");
        L.elderCredit -= price; c.charm = ch;
        G->Say(TextFormat("The elder hangs a %s round your neck (%s)", CharmName(ch), CharmEffect(ch)));
        return true;
    }
    if (id.rfind("att:", 0) == 0) {
        // feather fletching onto the speargun in hand (or any spear weapon carried)
        int ai = AttachmentIndex(id.substr(4));
        if (ai < 0) return no("he has none of that");
        const AttachmentDef& a = Attachments()[ai];
        if (L.elderCredit < a.price) return no("give him more fish first");
        for (auto& s : c.slots) if (s.it == Item::Weapon && s.wpn >= 0 && AttachmentFits(a, Weapons()[s.wpn]) && !HasAttachment(s.att, a.id.c_str())) {
            for (int k = 0; k < 3; k++) if (s.att[k] < 0) { s.att[k] = (int8_t)ai; L.elderCredit -= a.price; G->Say(TextFormat("The elder fits %s", a.name.c_str())); return true; }
        }
        return no("nothing you carry takes it");
    }
    int wi = WeaponIndex(id);
    if (wi < 0 || Weapons()[wi].where != "atoll") return no("he has none of that");
    const WeaponDef& w = Weapons()[wi];
    if (L.elderCredit < w.price) return no("give him more fish first");
    if (w.slots >= 2) for (const auto& s : c.slots) if (s.it == Item::Weapon && s.wpn >= 0 && Weapons()[s.wpn].slots >= 2) return no("a hand carries one long weapon at most");
    Slot ns; ns.it = Item::Weapon; ns.wpn = wi; ns.ammo = w.mag;
    for (auto& s : c.slots) if (s.it == Item::None) { s = ns; L.elderCredit -= w.price; G->Say(TextFormat("The elder gives you %s", w.name.c_str())); return true; }
    return no("your hands are full (four slots)");
}
// ---------------------------------------------------------------- harbour requests (design doc v2, page 48)
void Session::RollRequests() {
    requests.clear();
    std::vector<std::string> named;
    if (E && E->g) for (int sp : E->g->species) { const SpeciesRec& r = Species().sp[sp]; if (r.price > 0 && !r.protectedSp && !r.threat && !r.netOnly && r.band != BAND_AIR) named.push_back(r.name); }
    if (named.empty()) named = {"snapper"};
    uint32_t h = seed * 40503u + (uint32_t)deadline * 2654435761u;
    auto pick = [&]() { h = h * 1664525u + 1013904223u; return named[(h >> 8) % named.size()]; };
    for (int w = 0; w < REQ_COUNT; w++) { Request r; r.who = w; if (w == REQ_COOK || w == REQ_COLLECTOR) r.species = pick(); requests.push_back(r); }
}
std::string Session::RequestText(int i) const {
    if (i < 0 || i >= (int)requests.size()) return "";
    const Request& r = requests[i];
    switch (r.who) {
        case REQ_COOK: return "The cannery cook wants a " + r.species + " cooked to 1.5x.   Gives 3x its value, and the cook's spice rub (cooking 25% faster this deadline)";
        case REQ_NATURALIST: return "The naturalist wants a protected catch tagged and released alive (the Chandler's tag gun).   Gives a lure that brings rare fish for a night";
        case REQ_COLLECTOR: return "The collector wants a Glimmer " + r.species + ".   Gives 3 Pier Wheel tokens and 100 shillings";
        case REQ_APPRENTICE: return "The gunsmith's apprentice wants three kills at 2.5x Killscore or better in one night.   Gives a free attachment";
        default: return "Mother Carey, an old fishwife, wants a mini-boss drop.   Gives a legend lure (for a ground's legendary fish)";
    }
}
bool Session::RequestReady(int ci, int i, std::string* why) const {
    auto no = [&](const char* m) { if (why) *why = m; return false; };
    if (i < 0 || i >= (int)requests.size()) return no("no such request");
    const Request& r = requests[i];
    if (r.done) return no("done this deadline");
    switch (r.who) {
        case REQ_COOK: for (const auto& h : G->hold) if (h.name == r.species && h.cooked && h.cook >= 1.49f) return true; return no("no such fish cooked to 1.5x in the hold");
        case REQ_NATURALIST: return G->tagged > 0 ? true : no("tag a protected catch and return it alive first");
        case REQ_COLLECTOR: for (const auto& h : G->hold) if (h.name == r.species && h.glimmer) return true; return no("no Glimmer of that kind in the hold");
        case REQ_APPRENTICE: return G->highKills >= 3 ? true : no("three 2.5x kills in one night first");
        default: return G->drops.empty() ? no("no mini-boss drop aboard") : true;
    }
    (void)ci;
}
bool Session::FillRequest(int ci, int i, std::string* why) {
    if (!RequestReady(ci, i, why)) return false;
    Request& r = requests[i];
    switch (r.who) {
        case REQ_COOK:
            for (size_t k = 0; k < G->hold.size(); k++) if (G->hold[k].name == r.species && G->hold[k].cooked && G->hold[k].cook >= 1.49f) {
                float v = Value(G->hold[k]) * 3; money += v; G->hold.erase(G->hold.begin() + k);
                G->spiceRub = true;
                Tape(TextFormat("THE COOK PAID %.0f STOP SPICE RUB GIVEN STOP", v));
                break;
            }
            break;
        case REQ_NATURALIST: G->tagged--; G->rareLures++; Tape("THE NATURALIST THANKS YOU STOP A LURE FOR RARE FISH STOP"); break;
        case REQ_COLLECTOR:
            for (size_t k = 0; k < G->hold.size(); k++) if (G->hold[k].name == r.species && G->hold[k].glimmer) { G->hold.erase(G->hold.begin() + k); break; }
            tokens += 3; money += 100; Tape("THE COLLECTOR PAID 100 AND THREE TOKENS STOP"); break;
        case REQ_APPRENTICE: freeAttach++; Tape("THE APPRENTICE WILL FIT AN ATTACHMENT FREE STOP"); break;
        default: G->drops.erase(G->drops.begin()); G->legendLures++; Tape("MOTHER CAREY GAVE A LEGEND LURE STOP"); break;
    }
    r.done = true;
    return true;
}
bool Session::WearDrop(int ci, int di, std::string* why) {
    auto no = [&](const char* m) { if (why) *why = m; return false; };
    if (ci < 0 || ci >= (int)G->crew.size() || di < 0 || di >= (int)G->drops.size()) return no("no such drop");
    int ch = CharmOfDrop(G->drops[di]);
    if (ch == CH_NONE) return no("it won't hang on a cord");
    G->crew[ci].charm = ch;
    G->drops.erase(G->drops.begin() + di);
    G->Say(TextFormat("Worn on a cord: %s (%s)", CharmName(ch), CharmEffect(ch)));
    return true;
}
bool Session::GunUpgrade(int ci, int slot, std::string* why) {
    auto no = [&](const char* m) { if (why) *why = m; return false; };
    if (ci < 0 || ci >= (int)G->crew.size() || slot < 0 || slot > 3) return no("no such slot");
    Slot& s = G->crew[ci].slots[slot];
    if (s.it != Item::Weapon || s.wpn < 0 || !Weapons()[s.wpn].Gun()) return no("only guns take damage upgrades");
    int price = UpgradePrice(Weapons()[s.wpn], s.lvl);
    if (price <= 0) return no("three upgrades already");
    if (money < price) return no("not enough money");
    money -= price; s.lvl++;
    return true;
}
bool Session::GunAttach(int ci, int slot, const std::string& att, std::string* why) {
    auto no = [&](const char* m) { if (why) *why = m; return false; };
    if (ci < 0 || ci >= (int)G->crew.size() || slot < 0 || slot > 3) return no("no such slot");
    Slot& s = G->crew[ci].slots[slot];
    int ai = AttachmentIndex(att);
    if (ai < 0 || s.it != Item::Weapon || s.wpn < 0) return no("no gun to fit it to");
    const AttachmentDef& a = Attachments()[ai];
    if (a.where != "gunsmith") return no("the Gunsmith doesn't stock that");
    if (!AttachmentFits(a, Weapons()[s.wpn])) return no("it doesn't fit that gun");
    if (HasAttachment(s.att, att.c_str())) return no("already fitted");
    int free = -1; for (int k = 0; k < 3; k++) if (s.att[k] < 0) { free = k; break; }
    if (free < 0) return no("three attachments already");
    if (freeAttach > 0) { freeAttach--; s.att[free] = (int8_t)ai; return true; }   // (the apprentice's: fitted free)
    if (money < a.price) return no("not enough money");
    money -= a.price; s.att[free] = (int8_t)ai;
    return true;
}
bool Session::AmmoBuy(const std::string& kind, std::string* why) {
    auto no = [&](const char* m) { if (why) *why = m; return false; };
    int* stock = G->AmmoStock(kind);
    int per = 0; for (const auto& w : Weapons()) if (w.ammo == kind && w.ammoPrice > 0) { per = w.ammoPrice; break; }
    if (!stock || per <= 0) return no("not sold here");
    int n = AmmoPack(kind), price = per * n;
    if (money < price) return no("not enough money");
    money -= price; *stock += n;
    return true;
}
float Session::Deliver(int idx, int* rejected) {
    // the Owners' quota scales: credit toward the quota at full value, no shillings; under 70% fresh is turned away
    lastDelivery.clear(); lastDeliveryTotal = 0; lastRejected = 0;
    for (int i = (int)G->hold.size() - 1; i >= 0; i--) {
        if (idx >= 0 && i != idx) continue;
        const CatchRec& c = G->hold[i];
        if (c.junk) continue;   // (junk never counts toward the quota: the market buys it)
        float v = QuotaValue(c);
        if (v <= 0) { lastRejected++; continue; }   // (it stays in the hold: the market may still take it)
        lastDelivery.push_back({c.name, c.kg, c.price, c.grade, c.fresh, 1, c.first ? FIRST_CATCH_BONUS : 1, v, c.src});
        lastDeliveryTotal += v;
        G->hold.erase(G->hold.begin() + i);
    }
    sold += lastDeliveryTotal;
    if (rejected) *rejected = lastRejected;
    if (!lastDelivery.empty()) Tape(TextFormat("DELIVERED %.0f AGAINST THE QUOTA STOP %.0f OF %.0f STOP", lastDeliveryTotal, sold, quota));
    if (lastRejected) Tape(TextFormat("%d FISH REJECTED STOP NOT FRESH STOP", lastRejected));
    return lastDeliveryTotal;
}
bool Session::Buy(const std::string& id, std::string* why, int ci) {
    const ShopItem* it = nullptr;
    for (const auto& s : ChandlerItems()) if (id == s.id) it = &s;
    if (!it) { if (why) *why = "not sold here"; return false; }
    auto owns = [&](Tackle t) { return G->owned[(int)t]; };
    if ((id == "medium" && owns(Tackle::Medium)) || (id == "heavy" && owns(Tackle::Heavy)) || (id == "deepdrop" && owns(Tackle::DeepDrop)) || (id == "watch" && G->watch)) { if (why) *why = "already aboard"; return false; }
    if (id == "ice" && G->ice + 20 > G->iceCap) { if (why) *why = "the ice hold is full"; return false; }
    int price = it->price;
    if (id == "bosslure" && G->AnyWears(CH_BRASS_LURE)) price /= 2;   // (the brass lure charm: boss lures cost the crew half)
    if (id == "tag" && G->tagGun) { if (why) *why = "already aboard"; return false; }
    if (money < price) { if (why) *why = "not enough money"; return false; }
    money -= price;
    if (id == "bosslure") { G->bossLures++; return true; }
    if (id == "tag") { G->tagGun = true; return true; }
    if (id == "coin") { if (ci < 0 || ci >= (int)G->crew.size()) ci = 0; G->crew[ci].charm = CH_LUCKY_COIN; G->Say("A lucky coin on a cord: Glimmer variants twice as likely"); return true; }
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
    else {
        // hand gear into the first free slot (or the locker); ammunition onto the gun that takes it
        auto ammo = [&](Item gun, int n) {
            for (auto& c : G->crew) for (auto& sl : c.slots) if (sl.it == gun) { sl.ammo += n; return true; }
            for (auto& sl : G->locker) if (sl.it == gun) { sl.ammo += n; return true; }
            return false;
        };
        bool ok = true;
        if (id == "knife") G->AddItem(Item::Knife, 0);
        else if (id == "bandage") G->AddItem(Item::Bandage, 3);
        else if (id == "ring") G->AddItem(Item::Ring, 1);
        else if (id == "longline") G->AddItem(Item::Longline, 1);
        else if (id == "pot") G->AddItem(Item::Pot, 1);
        else if (id == "flare") G->AddItem(Item::Flare, 3);
        else if (id == "speargun") G->AddItem(Item::Speargun, 3);
        else if (id == "rifle") G->AddItem(Item::Rifle, 10);
        else if (id == "shotgun") G->AddItem(Item::Shotgun, 8);
        else if (id == "charge") G->AddItem(Item::Charge, 1);
        else if (id == "flares") ok = ammo(Item::Flare, 6);
        else if (id == "spears") ok = ammo(Item::Speargun, 10);
        else if (id == "rounds") ok = ammo(Item::Rifle, 30);
        else if (id == "shells") ok = ammo(Item::Shotgun, 24);
        else if (id == "explosive") { if (G->harpoonCannon) G->explosives++; else ok = false; }
        if (!ok) { money += it->price; if (why) *why = "nothing aboard takes it"; return false; }
    }
    return true;
}
bool Session::BuySlip(int idx, std::string* why) {
    const auto& I = SlipwayItems();
    if (idx < 0 || idx >= (int)I.size()) return false;
    std::string id = I[idx].id;
    if (id != "plates" && id != "net" && slip[idx]) { if (why) *why = "already fitted"; return false; }
    if (id == "net" && G->net.state != NetState::Lost) { if (why) *why = "she has a net"; return false; }
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
    else if (id == "harpoon") { G->harpoonCannon = true; G->harpoons = 2; }
    else if (id == "bignet") G->biggerNet = true;
    else if (id == "net") G->net = Trawl{};
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
    for (auto& c : G->hold) if (!c.cooked && !c.junk && c.boss < 0) c.fresh = c.iced ? c.fresh * OVERNIGHT_ICED : 0;   // (cooked fish keep)
    G->highKills = 0;
    G->rareLureNight = G->rareLures > 0; if (G->rareLureNight) { G->rareLures--; G->Say("The naturalist's lure goes on: rare fish bite more tonight"); }
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
    // the weather turns in the night about one night in three, between 23:00 and 03:00, mostly for the worse; the
    // telegraph's forecast is right 70% of the time (the other 30% it names the wrong weather)
    wxAt = -1;
    if (R() < 0.35f && !plainNights) {
        wxAt = 180 + R() * 240;
        int w0 = (int)weather, w1 = R() < 0.75f ? std::min(w0 + 1, (int)Weather::Squall) : std::max(w0 - 1, 0);
        if (w1 == w0) w1 = w0 == 0 ? 1 : w0 - 1;
        wxTo = (Weather)w1;
        int said = R() < 0.7f ? w1 : (w1 + 1 + (int)(R() * 3)) % 4;
        int hh = (20 + (int)wxAt / 60) % 24;
        Tape(TextFormat("GLASS %s STOP %s BY %02d00 STOP", (int)wxTo > w0 ? "FALLING" : "RISING", WX[said], hh));
    }
    // the ground remembers across a deadline's nights; a new deadline starts it afresh
    if (night == 0) E->Init(ground, seed + deadline * 97);
    else E->Day(15);
    E->StartNight();
    // tonight's variant (design doc, "Nightly variants": at most one, about 40% of nights none): the web feels it
    // through the Eco's multipliers; the rumour on the tape points at it 70% of the time
    E->forageMul = 1; E->threatHungerMul = 1; E->seaMul = 1; E->turtleMul = 1; E->sharkMul = 1; E->tideHeld = false; E->redTide = false;
    variant = Variant::None; canoe = CanoeState::None; canoeAt = -1; canoeT = 0; canoeWord.clear();
    {
        float v = plainNights ? 1.0f : R();
        if (v < 0.08f) variant = Variant::BaitRun;
        else if (v < 0.14f) variant = Variant::RedTide;
        else if (v < 0.24f) variant = Variant::KingTide;
        else if (v < 0.32f) variant = Variant::TurtleNesting;
        else if (v < 0.40f) variant = Variant::CanoeNight;
        switch (variant) {
            case Variant::BaitRun: E->forageMul = 3; E->threatHungerMul = 1.5f; break;
            case Variant::RedTide: E->forageMul = 0.3f; E->threatHungerMul = 1.5f; E->redTide = true; break;
            case Variant::KingTide: E->tideHeld = true; E->seaMul = 2; break;
            case Variant::TurtleNesting: E->turtleMul = 4; E->sharkMul = 2; break;
            case Variant::CanoeNight: canoeAt = 120 + R() * 240; canoe = CanoeState::Coming; break;
            default: break;
        }
        if (variant != Variant::None) {
            static const char* HINT[(int)Variant::COUNT] = {"", "BAIT THICK AT THE SURFACE", "DEAD FISH FLOATING OFF THE CREST", "KING TIDE TONIGHT", "TURTLES COMING UP THE BEACHES", "DRUMS HEARD ON THE ISLAND"};
            int said = R() < 0.7f ? (int)variant : 1 + (int)(R() * 5) % 5;
            Tape(TextFormat("RUMOUR STOP %s STOP", HINT[said]));
        }
    }
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
        carried = (sold - quota) * CREDIT_CARRY;   // fish delivered past the quota count toward the next at half their value
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
    if (shake.on && phase != Phase::Night) ShakeStep(dt);
    switch (phase) {
        case Phase::SailOut:
            if (!InHarbour()) {
                // past the harbour line: the night begins
                phase = Phase::Night; clockOn = true; clock = 0;
                G->eco = E;
            }
            break;
        case Phase::Night: {
            clock += dt * (shake.on ? (shake.step >= 8 ? 4.0f : 1.5f) : 1.0f);   // the shakedown is a short night, and runs to 05:00 fast once the lesson is done
            if (shake.on && shake.step < 8) clock = std::min(clock, 470.0f);         // (and it waits at 03:50 for a slow learner)
            if (shake.on) ShakeStep(dt);
            // total loss: every hand dead, or the Gannet gone down (design doc, "Death, injury, and ghosts")
            if (G->AllDead() || G->boat.sunk) {
                float charge = shake.on ? 0 : money * 0.25f;
                money -= charge;
                G->hold.clear(); G->longlines.clear(); G->pots.clear();
                Tape(TextFormat("GANNET SALVAGED STOP CATCH LOST STOP SALVAGE CHARGED %.0f SHILLINGS STOP", charge));
                G->boat.sunk = false; G->boat.bilge = 0; for (int s2 = 0; s2 < SEC_COUNT; s2++) { G->boat.integrity[s2] = G->boat.integrityMax[s2]; G->boat.patched[s2] = false; }
                G->boat.roll = G->boat.pitch = 0;
                night++; clockOn = false; G->eco = nullptr; Moor(); phase = Phase::Dock;
                break;
            }
            if (wxAt >= 0 && clock >= wxAt) {
                static const char* WXN[] = {"CALM", "FOG", "RAIN", "SQUALL", "STORM", "GLASS"};
                weather = wxTo; G->sea.Set(wxTo, seed * 31u + (uint32_t)clock);
                Tape(TextFormat("%s STOP WEATHER NOW %s STOP", ClockText().c_str(), WXN[(int)wxTo]));
                G->Say((int)wxTo >= (int)Weather::Rain ? "The weather turns: the sea gets up" : "The weather eases");
                wxAt = -1;
            }
            // Canoe night: drums, then a war canoe alongside for a minute, waiting for an answer (no answer is a refusal)
            if (canoe == CanoeState::Coming && clock >= canoeAt - 20 && !cues[3]) { cues[3] = true; Tape("DRUMS ON THE WATER STOP"); G->Say("Drums on the water, closing"); }
            if (canoe == CanoeState::Coming && clock >= canoeAt) { canoe = CanoeState::Alongside; canoeT = 0; G->Say("A war canoe comes alongside: they want fish, or silver, or they'll take what they can"); }
            if (canoe == CanoeState::Alongside) { canoeT += dt; if (canoeT >= 60) Canoe(CANOE_REFUSE); }
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
    rep.soldAt = S.Deliver();   // (the quota first: everything fresh enough to the Owners' scales, the rest to the market)
    rep.soldAt += S.Sell();
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
        // three ways to use a fish: the Owners' scales (quota credit, no glut, no shillings, under 70% fresh rejected)
        // and the Fish Market (shillings, glut, nothing toward the quota)
        {
            CatchRec fresh = c, stale = c; stale.fresh = 0.65f;
            s.glutKg["snapper"] = 50;
            g.hold = {fresh, stale};
            float m1 = s.money, q1 = s.sold;
            int rej = 0; float credit = s.Deliver(-1, &rej);
            check(fabsf(credit - 5.4f) < 0.01f && fabsf(s.sold - (q1 + 5.4f)) < 0.01f && s.money == m1, TextFormat("the Owners' scales credit %.1f against the quota at full value (no glut) and pay no shillings", credit));
            check(rej == 1 && g.hold.size() == 1 && g.hold[0].fresh < 0.7f, "a fish under 70% fresh is turned away at the scales and stays in the hold");
            float paid = s.Sell();
            check(paid > 0 && fabsf(s.money - (m1 + paid)) < 0.01f && fabsf(s.sold - (q1 + 5.4f)) < 0.01f && g.hold.empty(), TextFormat("the Fish Market takes it for %.2f shillings, which never count toward the quota", paid));
            s.sold = q1; s.glutKg.clear();
        }
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
        check(fabsf(s.sold - 25) < 0.01f, TextFormat("the 50 delivered past the quota carry into the next deadline at half value (%.0f)", s.sold));
        s.night = 3; s.sold = 10; s.Count();
        check(s.phase == Phase::Over, "short of it: GANNET REPOSSESSED, the run is over");
    }
    // junk from the sea (design doc v2): the Lagoon's table; sellable pieces pay their flat value at the market and never
    // count toward the quota; maps, keys and chart pieces are kept for the landings; a lobster trap brings its crabs
    {
        Gannet g; Eco e; Session s; s.Begin(g, e, 1, 77);
        g.hold.clear();
        for (int k = 0; k < 400; k++) g.FindJunk({-8, 0}, "test");
        int junk = 0, crabs = 0; bool notLagoon = false;
        for (const auto& h : g.hold) { if (h.junk) { junk++; if (h.name == "rusted anchor" || h.name == "signet ring" || h.name == "human skull") notLagoon = true; } else if (h.name == "blue crab" || h.name == "spiny lobster") crabs++; }
        check(junk > 250 && !notLagoon, TextFormat("400 pieces of Lagoon junk: %d sellable on deck, none from the deeper grounds", junk));
        check(g.junkBottles > 0 && g.junkKeys > 0 && g.junkCharts > 0, TextFormat("bottles %d, keys %d, chart pieces %d kept for the landings", g.junkBottles, g.junkKeys, g.junkCharts));
        check(crabs > 0, TextFormat("someone's lobster traps (and old boots) bring up %d crabs and lobsters", crabs));
        CatchRec purse; purse.name = "coin purse"; purse.junk = true; purse.price = 30; purse.kg = 0.3f; purse.gutted = purse.iced = true; purse.fresh = 0.2f;
        g.hold = {purse};
        int rej = 0; float q = s.Deliver(-1, &rej);
        check(q == 0 && rej == 0 && g.hold.size() == 1, "junk is never taken at the quota scales (and isn't 'rejected': it stays for the market)");
        float v = s.Sell(-1);
        check(fabsf(v - 30) < 0.01f, TextFormat("the market pays a coin purse its flat 30 (paid %.1f), freshness or not", v));
    }
    // a whole solo deadline played by a bot: bait, cast off, fish the port rod, gut and ice, home before 05:00, sell
    {
        Gannet g; Eco e; Session s; s.Begin(g, e, 1, 20261); s.plainNights = true;
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
        Gannet g2; Eco e2; Session s2; s2.Begin(g2, e2, 1, 7); s2.plainNights = true;
        Bot b2(s2);
        NightReport late = PlayNight(s2, b2, true);
        check(s2.tape.size() > 0 && (late.landed == 0 || late.customs), "a skipper who stays past 05:00 loses the hold to the cutter");
    }
    printf(fails ? "trawl-session-test: %d check(s) failed\n" : "trawl-session-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}


// ---------------------------------------------------------------- the shakedown (design doc, "First night")
// A short night on the Lagoon with a fixed seed, no quota, deaths that don't count, and Kess, an old deckhand aboard
// for this night only, talking in short chalked lines. Eight lessons: the deck and the handline, the rod, the table,
// the sonar, the net, blood (a reef shark comes to the guts), a hand overboard (Kess slips), and home.
namespace {
const char* SHAKE_LINES[Session::SHAKE_STEPS] = {
    "Kess: Steam's up. Take the helm (E at the wheel) and cast off. Keep the quay to port going out, and mind the crest.",
    "Kess: Take a handline at the rail (E). Hold to cast, let go, and strike when the tip dips. Three small fish for the bait well.",
    "Kess: Now the light rod, port side. Cast at the dark water, set the hook on the take, and lean the rod against its run. Land one over a kilo.",
    "Kess: To the table (E). Gut it and ice it, or it is worth half by dawn. I will throw the guts over.",
    "Kess: The sonar, in the wheelhouse. Ping (click), mark the biggest ball (click it), and steer onto it.",
    "Kess: The net. Hold at the winch to shoot it, tow slow ahead through the ball, and haul it in. Two hands haul twice as fast.",
    "Kess: That blood has brought a shark. Watch it take a hooked fish. Stop the blood, stay off the rail, gaff it, or steam away.",
    "Kess: ...the deck is wet. Stop the screw (telegraph to stop) and throw me the ring, quick!",
    "Kess: Home. Cross the harbour line before 05:00 or the cutter takes the hold.",
    "Kess: Ashore to port. The Owners' scales take fish for the quota, the Fish Market pays the purse. Only the scales count. Do one, then read the chalkboard.",
};
int StationIdxOf(StationKind k) { for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == k) return i; return -1; }
}
void Session::BeginShakedown(Gannet& g, Eco& e) {
    Begin(g, e, 2, 777);
    plainNights = true;
    shake = Shakedown{}; shake.on = true; shake.step = 0; shake.line = SHAKE_LINES[0];
    g.botsOn = true; g.botSkill = Skill::OldHand;
    if (g.crew.size() > 1) { g.crew[1].role = Role::Angler; shake.kess = 1; }
    quota = 0;
    tape.clear(); Tape("SHAKEDOWN STOP NO QUOTA STOP KESS ABOARD FOR THE NIGHT STOP");
    g.baitShrimp = 20; g.ice = 60; g.boat.bunker = 60;
    if (!g.crew.empty()) g.crew[0].patchKits = 1;
}
void Session::SkipShakedown() { shake.on = false; shake.done = true; shake.line = "Kess: Suit yourself. You will learn it the hard way."; }
void Session::ShakeStep(float dt) {
    Shakedown& s = shake;
    if (!s.on || !G) return;
    Gannet& g = *G;
    s.stepT += dt;
    if (s.asideT > 0) s.asideT -= dt;
    auto aside = [&](const char* t) { if (s.asideT <= 0 || s.aside != t) { s.aside = t; s.asideT = 4; } };
    auto advance = [&]() { s.step++; s.stepT = 0; if (s.step < SHAKE_STEPS) s.line = SHAKE_LINES[s.step]; };
    int kess = s.kess < (int)g.crew.size() ? s.kess : -1;
    // what the hand is doing, for the asides
    const Crew& me = g.crew[0];
    const Rod* myRod = nullptr; for (const auto& r : g.rods) if (me.station == r.station) myRod = &r;
    if (myRod) {
        if (myRod->bite.stage == BiteStage::Take) aside("Kess: Now! Strike!");
        else if (myRod->bite.stage == BiteStage::Nibble) aside("Kess: Let it nibble. Not yet.");
        else if (myRod->state == RodState::Fighting) {
            float rating = TackleOf(myRod->tackle).strength;
            if (myRod->fight.tension > 0.8f * rating) aside("Kess: She will part! Ease the drag, bow the rod.");
            else if (myRod->fight.tension < 0.08f * rating) aside("Kess: Line is slack. Reel.");
            else if (myRod->fight.alongside) aside("Kess: Alongside. Gaff it (E)!");
        }
    }
    switch (s.step) {
        case 0: if (phase == Phase::Night) advance(); break;
        case 1: {
            if (g.landedSmall >= 3) advance();   // (landed: the gulls may have had them since)
            break;
        }
        case 2: {
            if (g.landedBig >= 1) advance();
            break;
        }
        case 3: {
            bool done = false; for (const auto& h : g.hold) if (h.gutted && h.iced) done = true;
            if (done) advance();
            break;
        }
        case 4: if (!g.sonar.marks.empty()) advance(); break;
        case 5: {
            if (kess >= 0 && s.stepT < 0.1f) g.OrderBot(StationIdxOf(StationKind::NetWinch));   // Kess takes the winch with you
            bool netted = false; for (const auto& h : g.hold) if (h.src == CS_NET) netted = true;
            if (netted) { if (kess >= 0) g.OrderBot(-1); advance(); }
            break;
        }
        case 6: {
            // the guts bring a reef shark: one is called in hungry 55 m off and the water chummed; the lesson ends when
            // it has taken a fish, or it has been seen and left behind, or after two minutes
            if (!s.sharkCalled && E) {
                s.sharkCalled = true;
                int sp = Species().Find("reef shark");
                g.chumLeft += D().chumBlood * 2;
                E->stirOverride = 1;   // (the Stir clock would cull a shark this early; the lesson wants it)
            }
            // the shark: called in 45 m off over water it can swim in, again nearer if it hasn't shown in a minute
            if (E && !s.sharkSeen && (s.stepT < 0.1f || (s.stepT > 60 && s.stepT < 60.1f))) {
                int sp = Species().Find("reef shark");
                for (int k = 0; k < 8 && sp >= 0; k++) {
                    float ang = 0.7f + k * 0.785f, r = s.stepT > 1 ? 32.0f : 45.0f;
                    Vector2 at{g.boat.pos.x + cosf(ang) * r, g.boat.pos.y + sinf(ang) * r};
                    if (!E->InMap(at) || E->DepthAt(at) < 4) continue;
                    int ai = E->SpawnAgentPublic(sp, at); E->agents[ai].hunger = 0.95f; E->agents[ai].count = 1;
                    break;
                }
            }
            if (E && (s.stepT > 120 || s.sharkSeen) && E->stirOverride >= 0 && (s.stepT > 120 || s.speedT > 8)) E->stirOverride = -1;
            if (E) for (const auto& a : E->arrivals) if (a.species == "reef shark") s.sharkSeen = true;
            if (s.sharkSeen) {
                bool fed = false; int near = 0;
                if (E) for (const auto& a : E->agents) if (a.alive && Species().sp[a.sp].name == "reef shark") { if (a.fedT > 0) fed = true; if (Vector2Distance({a.p.x, a.p.y}, g.boat.pos) < 30) near++; }
                if (fabsf(g.boat.Speed()) > 1.5f) s.speedT += dt; else s.speedT = 0;
                if (fed) aside("Kess: There. It took it. Now you know what blood costs.");
                if (fed || s.speedT > 8 || (near == 0 && s.stepT > 40) || s.stepT > 120) advance();
            } else if (s.stepT > 150) advance();
            break;
        }
        case 7: {
            if (E) E->stirOverride = -1;
            if (kess >= 0 && s.stepT < 0.1f && !g.crew[kess].overboard) { g.LeaveStation(kess); g.GoOverboard(kess, "Kess slips on the wet deck"); g.crew[kess].swim = g.boat.ToWorld({-2, 7}); }
            if (kess < 0 || (!g.crew[kess].overboard && s.stepT > 1)) advance();
            else if (kess >= 0 && g.crew[kess].dead) advance();   // (a lost Kess is a lesson too)
            break;
        }
        case 8: if (phase == Phase::Dock) advance(); break;
        case 9: if (lastSaleTotal > 0 || lastDeliveryTotal > 0 || g.hold.empty()) { s.on = false; s.done = true; } break;
        default: s.on = false; s.done = true; break;
    }
}

// ---------------------------------------------------------------- --trawl-shakedown-test
// A scripted hand plays the shakedown through: cast off, a handline, the light rod, the table, the sonar, the net,
// the shark, the ring for Kess, home and the market. Every step must complete.
int RunTrawlShakedownTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Trawl: the shakedown night\n");
    Gannet g; Eco e; Session s;
    s.BeginShakedown(g, e);
    check(s.shake.on && s.shake.step == 0 && s.quota == 0 && g.crew.size() == 2 && g.crew[1].bot, "the shakedown begins: no quota, Kess aboard as a bot");
    const float dt = 1 / 60.0f;
    int hand = StationIdxOf(StationKind::StarRod), port = StationIdxOf(StationKind::PortRod), gut = StationIdxOf(StationKind::Gutting), helm = StationIdxOf(StationKind::Helm), winch = StationIdxOf(StationKind::NetWinch), sonar = StationIdxOf(StationKind::Sonar);
    int handRod = g.RodAt(hand), portRod = g.RodAt(port);
    g.rods[handRod].tackle = Tackle::Handline;
    std::string why;
    g.crew[0].p = {-1, 0.8f}; g.crew[0].station = -1;
    check(s.CastOff(&why), "cast off" + (why.empty() ? std::string() : ": " + why));
    Vector2 spot = Vector2Add(s.harbour, {140, 20});
    float t = 0; int lastStep = -1; float stepAt[Session::SHAKE_STEPS + 1] = {};
    bool thrown = false, kessInSea = false, kessHauled = false;
    auto steerTo = [&](Vector2 tgt, float slowWithin) {
        Vector2 d = Vector2Subtract(tgt, g.boat.pos);
        float want = atan2f(d.y, d.x), err = want - g.boat.heading;
        while (err > PI) err -= 2 * PI;
        while (err < -PI) err += 2 * PI;
        g.boat.rudder = std::clamp(err * 2.5f, -1.0f, 1.0f);
        float dist = Vector2Length(d);
        g.boat.telegraph = dist > slowWithin ? 2 : dist > 6 ? 1 : 0;
    };
    while ((s.shake.on || s.phase != Phase::Dock) && t < 1500) {
        if (g.boat.pressure < D().greenLo + 0.15f && g.boat.firebox < 5) g.boat.Shovel(D().shovelKg * 0.05f);
        int st = s.shake.step;
        if (st != lastStep) { lastStep = st; stepAt[std::min(st, Session::SHAKE_STEPS)] = t; if (getenv("DEPTH_TRACE")) printf("    t%4.0f step %d: %s\n", t, st, s.shake.line.c_str()); }
        Crew& me = g.crew[0];
        auto fish = [&](int station, int ri) {
            me.p = Stations()[station].at; me.station = station; me.deck = 0;
            Rod& r = g.rods[ri];
            bool cast = r.state == RodState::Idle && fmodf(t, 2.0f) < 0.9f;
            Vector2 aim = Vector2Add(r.TipDeck(), Vector2Scale(Vector2Normalize(Vector2Subtract(r.TipDeck(), Stations()[station].at)), 10));
            if (r.state == RodState::Fighting) {
                // (a human hand: the bot angler's decisions go in through the hand's own controls)
                static uint32_t rng = 5; BotFight(r.fight, Skill::OldHand, dt, rng);
                g.RodInput(0, false, aim, r.fight.reeling, false, r.fight.rodLean, r.fight.bowed, r.fight.alongside, 0);
            } else g.RodInput(0, cast, aim, false, r.bite.stage == BiteStage::Take, 0, false, false, 0);
        };
        switch (st) {
            case 0: me.station = helm; me.p = Stations()[helm].at; steerTo(spot, 30); break;
            case 1: case 2: {
                if (Vector2Distance(g.boat.pos, spot) > 20 && s.shake.stepT < 60 && st == 1) { me.station = helm; me.p = Stations()[helm].at; steerTo(spot, 20); }
                else { g.boat.telegraph = 0; fish(st == 1 ? hand : port, st == 1 ? handRod : portRod); }
                break;
            }
            case 3: g.boat.telegraph = 0; for (auto& r : g.rods) r.botAngler = false; me.p = Stations()[gut].at; me.station = gut; g.Primary(0, true, dt); break;
            case 4: me.p = Stations()[sonar].at; me.station = sonar; if (g.sonar.cool <= 0) g.SonarPing(0); if (!g.sonar.ret.empty()) g.SonarMarkAt(0, g.boat.ToDeck({g.sonar.ret[0].p.x, g.sonar.ret[0].p.y})); break;
            case 5: {
                me.p = Stations()[winch].at; me.station = winch;
                bool held = g.net.state == NetState::Stowed || g.net.state == NetState::Shooting || g.net.state == NetState::Hauling || (g.net.state == NetState::Down && (g.net.load > 60 || s.shake.stepT > 150));
                g.NetInput(0, held, false, dt);
                if (g.net.state == NetState::Down) { if (Vector2Distance(g.boat.pos, spot) > 25) { steerTo(spot, 0); g.boat.telegraph = 1; } else { g.boat.telegraph = 1; g.boat.rudder = 0.25f; } }   // tow slow ahead, round the mark over deep water
                else g.boat.telegraph = 0;
                if (g.net.state == NetState::Snagged) g.boat.telegraph = -1;
                if (getenv("DEPTH_TRACE") && fmodf(t, 20) < dt) printf("      net %d load %.0f t %.1f d %.0f depth %.0f tel %d\n", (int)g.net.state, g.net.load, g.net.t, Vector2Distance(g.boat.pos, spot), e.DepthAt(g.boat.pos), g.boat.telegraph);
                break;
            }
            case 6: me.station = helm; me.p = Stations()[helm].at; if (s.shake.sharkSeen) { g.boat.telegraph = 3; g.boat.rudder = 0; } else g.boat.telegraph = 0; break;
            case 7: {
                g.boat.telegraph = 0; g.boat.shaft = 0;
                Crew& k = g.crew[1];
                if (k.overboard) {
                    int slot = -1; for (int i = 0; i < 4; i++) if (me.slots[i].it == Item::Ring) slot = i;
                    if (slot >= 0) {
                        me.station = -1; Vector2 sd = g.boat.ToDeck(k.swim); me.p = {std::clamp(sd.x, -9.0f, 8.0f), sd.y < 0 ? -2.3f : 2.3f}; me.sel = slot;
                        LifeRing* out = nullptr; for (auto& r : g.rings) if (r.thrower == 0 && r.state != 0) out = &r;
                        if (!out) { g.UseItem(0, sd, true, false, false, dt); thrown = true; } else if (out->state == 2) g.UseItem(0, sd, false, true, false, dt);
                    }
                }
                break;
            }
            case 8: me.station = helm; me.p = Stations()[helm].at; steerTo(g.moorPos, 25); break;
            default: break;
        }
        if (s.phase == Phase::Night && g.crew[1].overboard) kessInSea = true;
        if (s.phase == Phase::Night && kessInSea && !g.crew[1].overboard && !g.crew[1].dead) kessHauled = true;
        g.Step(dt); s.Step(dt); t += dt;
        if (s.shake.step == 9 && s.phase == Phase::Dock) s.Sell();
    }
    check(s.shake.step >= 1, TextFormat("1 cast off and out past the harbour line (at %.0f s)", stepAt[1]));
    check(s.shake.step >= 2, TextFormat("2 three small fish on the handline (at %.0f s)", stepAt[2]));
    check(s.shake.step >= 3, TextFormat("3 a fish over a kilo on the light rod (at %.0f s)", stepAt[3]));
    check(s.shake.step >= 4, TextFormat("4 gutted and iced (at %.0f s)", stepAt[4]));
    check(s.shake.step >= 5, TextFormat("5 a sonar mark (at %.0f s)", stepAt[5]));
    check(s.shake.step >= 6, TextFormat("6 the net shot and hauled (at %.0f s)", stepAt[6]));
    check(s.shake.step >= 7 && s.shake.sharkSeen, TextFormat("7 the shark came to the blood and the lesson ended (at %.0f s; seen %d)", stepAt[7], (int)s.shake.sharkSeen));
    check(s.shake.step >= 8 && thrown && kessInSea && kessHauled, TextFormat("8 Kess over the side at sea, the ring thrown, hauled back aboard (at %.0f s)", stepAt[8]));
    check(s.shake.step >= 9, TextFormat("9 home through the harbour line (at %.0f s)", stepAt[9]));
    check(s.shake.done && !s.shake.on, TextFormat("10 sold at the market: the shakedown is done (%.0f s in all, %.1f min)", t, t / 60));
    printf(fails ? "%d FAILED\n" : "trawl-shakedown-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}
} // namespace tw
