// The Flight, stage 6: research, faith, trade, and the twelve founders' bends (design doc pp. 6-10, 20-22, 25-27).
// Headless; every number is data (data/flight/flight_research.json, flight_bends.json, flight_towns.json).
//
// Research runs at the Roost, one line at a time, and costs pearls, shells and days; tier 4s want a colony of 25.
// Fervour (0-100) is a colony stat: the shrine's priests raise it, offerings and the Founder's prayer burst it, hunger,
// routs and the Founder's death lower it; it decides how long the colony sleeps, how firm its flocks are, and at 100
// (with Zeal) it never sleeps and never routs. Fishing towns run a dock market priced in feed; Traders carry the
// colony's surplus there and buy what the colony needs (pearls first: pearls are research). Colonies barter with each
// other (Trade 2): goods each way and a truce. Each founder species bends its whole colony (a Bend).
#include "flight.h"
#include "json.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>

namespace fl {

// ---------------------------------------------------------------- data
namespace {
struct ResearchData {
    ResearchTier tiers[(int)Tree::COUNT][4];
    ResearchCost cost[4];
    int stTwigs[ST_COUNT] = {10, 20, 16, 14, 20}, stShells[ST_COUNT] = {0, 5, 0, 10, 15};
    float fBase = 20, fDrift = 10, fPriest = 7, fPrayer = 15, fPrayerHours = 1, fOffer = 2.5f, fStarve = -25, fDeath = -10, fZealDeath = -30, fRout = -8, convert = 1, zealHunger = 1.2f;
    float guano = 0.25f, deepPearl = 0.07f, goldenPearls = 1;
};
const char* TREE_KEYS[(int)Tree::COUNT] = {"nesting", "fishing", "flight", "caches", "war", "bombing", "chemistry", "faith", "trade"};
const ResearchData& RD() {
    static ResearchData d; static bool loaded = false;
    if (loaded) return d;
    loaded = true;
    d.cost[0] = {2, 10, 0.5f, 0}; d.cost[1] = {5, 20, 1, 0}; d.cost[2] = {10, 40, 2, 0}; d.cost[3] = {20, 80, 3, 25};
    Json j = LoadJsonFile(FlightDataDir() + "/flight_research.json");
    if (!j.IsObj()) return d;
    for (const Json& c : j["costs"].a) { int t = std::clamp(c["tier"].I(1), 1, 4) - 1; d.cost[t].pearls = c["pearls"].I(d.cost[t].pearls); d.cost[t].shells = c["shells"].I(d.cost[t].shells); d.cost[t].days = c["days"].F(d.cost[t].days); d.cost[t].birds = c["birds"].I(0); }
    for (int t = 0; t < (int)Tree::COUNT; t++) {
        const Json& tr = j["trees"][TREE_KEYS[t]];
        for (int k = 0; k < 4 && k < (int)tr.a.size(); k++) { d.tiers[t][k].name = tr[k]["name"].Str0(); d.tiers[t][k].what = tr[k]["what"].Str0(); }
    }
    static const char* SK[ST_COUNT] = {"hedge", "tower", "roost", "shrine", "works"};
    for (int s = 2; s < ST_COUNT; s++) { const Json& o = j["structures"][SK[s]]; if (o.IsObj()) { d.stTwigs[s] = o["twigs"].I(d.stTwigs[s]); d.stShells[s] = o["shells"].I(d.stShells[s]); } }
    const Json& f = j["fervour"];
    auto F = [&](const char* k, float& v) { if (f[k].IsNum()) v = f[k].F(v); };
    F("base", d.fBase); F("drift_per_day", d.fDrift); F("priest_per_day", d.fPriest); F("prayer", d.fPrayer); F("prayer_hours", d.fPrayerHours);
    F("offering_per_size", d.fOffer); F("starving_per_day", d.fStarve); F("founder_death", d.fDeath); F("zeal_founder_death", d.fZealDeath);
    F("rout", d.fRout); F("convert_per_priest_day", d.convert); F("zeal_hunger", d.zealHunger);
    if (j["guano_per_bird_day"].IsNum()) d.guano = j["guano_per_bird_day"].F(d.guano);
    if (j["deep_dive_pearl_chance"].IsNum()) d.deepPearl = j["deep_dive_pearl_chance"].F(d.deepPearl);
    if (j["golden_nest_pearls_per_day"].IsNum()) d.goldenPearls = j["golden_nest_pearls_per_day"].F(d.goldenPearls);
    return d;
}
struct TownData {
    float sell[G_COUNT] = {1, 0.5f, 1, 4}, morning = 0.8f, evening = 1.3f, glut = 0.006f, recover = 0.5f;
    float stock[G_COUNT] = {0, 80, 60, 12}, restock[G_COUNT] = {0, 40, 25, 5};
    float repTrade = 1, repShoo = -50, repTrader = -25, pelican = 1, surplusDays = 2;
};
const TownData& TD() {
    static TownData d; static bool loaded = false;
    if (loaded) return d;
    loaded = true;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_towns.json");
    if (!j.IsObj()) return d;
    static const char* GK[G_COUNT] = {"fish", "twigs", "shells", "pearls"};
    for (int g = 1; g < G_COUNT; g++) { if (j["sell_price"][GK[g]].IsNum()) d.sell[g] = j["sell_price"][GK[g]].F(d.sell[g]); if (j["restock_per_day"][GK[g]].IsNum()) d.restock[g] = j["restock_per_day"][GK[g]].F(d.restock[g]); }
    for (int g = 0; g < G_COUNT; g++) if (j["stock"][GK[g]].IsNum()) d.stock[g] = j["stock"][GK[g]].F(d.stock[g]);
    auto F = [&](const char* k, float& v) { if (j[k].IsNum()) v = j[k].F(v); };
    F("fish_price_morning", d.morning); F("fish_price_evening", d.evening); F("fish_glut_per_feed", d.glut); F("recover_per_day", d.recover);
    F("rep_per_trade", d.repTrade); F("rep_shoo", d.repShoo); F("rep_trader_killed", d.repTrader); F("pelican_pearls", d.pelican); F("trader_surplus_days", d.surplusDays);
    return d;
}
}  // namespace

const char* TreeName(Tree t) { static const char* N[(int)Tree::COUNT] = {"Nesting", "Fishing", "Flight", "Caches and craft", "War", "Bombing", "Chemistry", "Faith", "Trade and parasitism"}; return N[std::clamp((int)t, 0, (int)Tree::COUNT - 1)]; }
const ResearchTier& ResearchOf(Tree t, int tier) { return RD().tiers[std::clamp((int)t, 0, (int)Tree::COUNT - 1)][std::clamp(tier, 1, 4) - 1]; }
ResearchCost ResearchCostOf(int tier) { return RD().cost[std::clamp(tier, 1, 4) - 1]; }
const char* GoodName(int g) { static const char* N[G_COUNT] = {"fish", "twigs", "shells", "pearls"}; return N[std::clamp(g, 0, G_COUNT - 1)]; }
const char* StructureName(int k) { static const char* N[ST_COUNT] = {"hedge", "tower", "Roost", "shrine", "Works"}; return N[std::clamp(k, 0, ST_COUNT - 1)]; }
const char* RoleAbbrev(Role r) {
    static const char* A[(int)Role::COUNT] = {"", "Fsh", "Fdr", "Bld", "Sct", "Trd", "Pst", "Chm", "Pth", "Skm", "Tnk", "Str", "Wch", "Scr", "FM", "Bmb", "Prt"};
    return A[std::clamp((int)r, 0, (int)Role::COUNT - 1)];
}
int StructureTwigsOf(int kind) { return RD().stTwigs[std::clamp(kind, 0, ST_COUNT - 1)]; }
int StructureShellsOf(int kind) { return RD().stShells[std::clamp(kind, 0, ST_COUNT - 1)]; }

const Bend& BendOf(int def) {
    static std::vector<Bend> v; static bool loaded = false;
    if (!loaded) {
        loaded = true;
        const auto& F = Founders();
        v.assign(F.size(), Bend{});
        Json j = LoadJsonFile(FlightDataDir() + "/flight_bends.json");
        for (size_t i = 0; i < F.size(); i++) {
            const Json& o = j[F[i].key];
            if (!o.IsObj()) continue;
            Bend& b = v[i];
            auto Fl = [&](const char* k, float& x) { if (o[k].IsNum()) x = o[k].F(x); };
            auto In = [&](const char* k, int& x) { if (o[k].IsNum()) x = o[k].I(x); };
            auto Bo = [&](const char* k, bool& x) { if (o.Has(k)) x = o[k].Bool0(x); };
            Fl("fish_hit", b.fishHit); Fl("speed", b.speed); Fl("attack", b.attack); Fl("mate_time", b.mateTime); Fl("fervour_gain", b.fervourGain); Fl("fervour_cap", b.fervourCap);
            Fl("trade", b.trade); Fl("trader_carry", b.traderCarry); Fl("convert", b.convert); Fl("rest", b.rest); Fl("guano", b.guano); Fl("research", b.research);
            Fl("stamina", b.stamina); Fl("wind", b.wind); Fl("scout", b.scout); Fl("reach", b.reach); Fl("nest_twigs", b.nestTwigs); Fl("predator_range", b.predatorRange);
            In("carry", b.carry); In("clutch", b.clutch); In("fledge_days", b.fledgeDays);
            Bo("night_fishing", b.nightFishing); Bo("egg_theft", b.eggTheft); Bo("no_faith", b.noFaith); Bo("boom", b.boom); Bo("golden_nest", b.goldenNest);
            Bo("talon_lock", b.talonLock); Bo("skim", b.skim); Bo("tear", b.tear); Bo("serenade", b.serenade); Bo("dusk_raid", b.duskRaid); Bo("cornering", b.cornering);
            Bo("pilgrimage", b.pilgrimage); Bo("brood", b.brood); Bo("long_reach", b.longReach);
            for (const Json& t : o["early_trees"].a) for (int k = 0; k < (int)Tree::COUNT; k++) if (t.Str0() == TREE_KEYS[k]) b.earlyTrees |= 1 << k;
        }
    }
    static const Bend none;
    return def >= 0 && def < (int)v.size() ? v[def] : none;
}

// ---------------------------------------------------------------- research
const Structure* World::Built(int kind) const { for (const auto& s : col.builds) if (s.kind == kind && s.built) return &s; return nullptr; }
bool World::CanResearch(Tree t, std::string* why) const {
    auto no = [&](const char* w) { if (why) *why = w; return false; };
    int n = col.tier[(int)t] + 1;
    if (n > 4) return no("every tier is done");
    if (!Built(ST_ROOST)) return no("research needs a Roost (the colony panel's builds)");
    if (col.resTree >= 0) return no("the Roost is busy");
    if (t == Tree::Faith && BendNow().noFaith) return no("this founder has no faith tree");
    bool early = (BendNow().earlyTrees >> (int)t) & 1;
    if ((t == Tree::Bombing || t == Tree::Chemistry) && time < (early ? 3 : 4) * DAY) return no(early ? "the Works open on day 4" : "the Works open on day 5");
    ResearchCost c = ResearchCostOf(n);
    if (c.birds > 0 && Alive() < c.birds) { if (why) *why = TextFormat("tier 4 wants a colony of %d", c.birds); return false; }
    if (col.pearls < c.pearls || col.shells < c.shells) { if (why) *why = TextFormat("%d pearls and %d shells", c.pearls, c.shells); return false; }
    return true;
}
bool World::StartResearch(Tree t) {
    if (!CanResearch(t)) return false;
    int n = col.tier[(int)t] + 1;
    ResearchCost c = ResearchCostOf(n);
    col.pearls -= c.pearls; col.shells -= c.shells;
    float speed = BendNow().research * (((BendNow().earlyTrees >> (int)t) & 1) ? 1.5f : 1.0f);
    col.resTree = (int)t; col.resDays = col.resLeft = c.days / std::max(0.1f, speed);
    Say(TextFormat("The Roost begins %s %d: %s (%.1f days).", TreeName(t), n, ResearchOf(t, n).name.c_str(), col.resDays));
    return true;
}
bool World::RoleUnlocked(Role r) const {
    switch (r) {
    case Role::Skirmisher: case Role::Striker: return col.HasTier(Tree::War, 2);
    case Role::Flockmaster: return col.HasTier(Tree::War, 4);
    case Role::Pirate: return col.HasTier(Tree::War, 3);
    case Role::Trader: return col.HasTier(Tree::Trade, 1);
    case Role::Priest: return col.HasTier(Tree::Faith, 1);
    case Role::Chemist: return col.HasTier(Tree::Chemistry, 1);
    case Role::Bomber: return col.HasTier(Tree::Bombing, 2);
    case Role::Pathfinder: return col.HasTier(Tree::Trade, 4);
    default: return true;
    }
}
bool World::BuildUnlocked(int kind) const {
    switch (kind) {
    case ST_HEDGE: case ST_TOWER: return col.HasTier(Tree::War, 3);
    case ST_SHRINE: return col.HasTier(Tree::Faith, 1);
    case ST_WORKS: return col.HasTier(Tree::Bombing, 1) || col.HasTier(Tree::Chemistry, 1);
    default: return true;   // (the Roost)
    }
}

// ---------------------------------------------------------------- fervour (doc p26)
float World::FervourBand() const {
    float f = std::min(BendNow().fervourCap, col.fervour + (col.prayerT > 0 ? RD().fPrayer : 0));
    if (f >= 100 && col.HasTier(Tree::Faith, 4)) return 4;
    return f < 20 ? 0 : f < 50 ? 1 : f < 80 ? 2 : 3;
}
int World::FervourBandOf(int side) const {
    const Colony& C = ColOf(side);
    float f = std::min(BendOfSide(side).fervourCap, C.fervour + (C.prayerT > 0 ? RD().fPrayer : 0));
    if (f >= 100 && C.HasTier(Tree::Faith, 4)) return 4;
    return f < 20 ? 0 : f < 50 ? 1 : f < 80 ? 2 : 3;
}
float World::FervourRout() const { return RD().fRout; }
float World::WindPenalty(int side) const { return BendOfSide(side).wind * (ColOf(side).HasTier(Tree::Flight, 3) ? 0.5f : 1.0f); }
float World::FightStamina(int side) const { return 25 * BendOfSide(side).stamina * (ColOf(side).HasTier(Tree::Flight, 2) ? 1.5f : 1.0f); }
int World::CacheCap() const { return col.HasTier(Tree::Caches, 2) ? 50 : Econ().cacheCap; }
float World::SpoilDays() const { return col.HasTier(Tree::Caches, 1) ? 6 : Econ().spoilDays; }
int World::NestEggs() const { return col.HasTier(Tree::Nesting, 2) ? 6 : Econ().nestEggs; }
int World::ShellsWanted() const {
    // the builders keep a stock of shells at the cache: enough for the cheapest next research and a shrine
    // (enough for the next tier of the colony's furthest tree: a colony climbing a tree must stock for its next step)
    int top = 0; for (int t = 0; t < (int)Tree::COUNT; t++) if (col.tier[t] < 4) top = std::max(top, (int)col.tier[t]);
    int need = ResearchCostOf(std::min(4, top + 1)).shells;
    return std::max(need, StructureShellsOf(ST_SHRINE)) + 5;
}
int World::NestTwigs() const { return (int)lroundf(Econ().nestTwigs * BendNow().nestTwigs); }
float World::DeepDivePearl() const { return RD().deepPearl; }
float World::MaxHp(Role r) const { return RoleOf(r).hp + (r == Role::Tank && col.HasTier(Tree::Caches, 3) ? 60.0f : 0.0f); }
float World::NightRest() const {
    static const float REST[5] = {1, 1, 0.5f, 0.15f, 0};
    return REST[(int)FervourBand()] * BendNow().rest;
}
bool World::Pray() {
    const Structure* sh = Built(ST_SHRINE);
    if (!sh || Vector3Distance(me.pos, sh->pos) > 5) return false;
    int day = (int)(time / DAY);
    if (col.prayedDay == day) { Say("You have prayed today."); return false; }
    col.prayedDay = day; col.prayerT = RD().fPrayerHours * DAY / 24;
    Say(TextFormat("You pray at the shrine: fervour +%.0f for an hour.", RD().fPrayer));
    return true;
}
bool World::Offer(int size) {
    if (!col.HasTier(Tree::Faith, 2) || size <= 0) return false;
    float gain = (size >= 4 ? 10.0f : size * RD().fOffer) * BendNow().fervourGain;
    col.fervour = std::min(BendNow().fervourCap, col.fervour + gain);
    Say(TextFormat("An offering at the shrine: fervour +%.0f (%.0f).", gain, col.fervour));
    return true;
}

// ---------------------------------------------------------------- the founders' buttons
bool World::StartBoom() {
    if (!BendNow().boom || col.boomState != 0 || col.boomCd > 0) return false;
    col.boomState = 1; col.boomT = 2 * DAY; col.boomCd = 3 * DAY;
    for (int s = 0; s <= (int)sides.size(); s++) if (s != cur) SayTo(s, SideName(cur) + "'s banner turns gold: the Tycoon booms (two days of plenty, then a day of bust).");
    Say("BOOM: two days of +60% feed, building and research; then a day of -40%.");
    return true;
}
bool World::Corner(int town) {
    if (!BendNow().cornering || col.cornered || town < 0 || town >= (int)towns.size()) return false;
    float ph = DayPhase();
    if (ph < 0.2f || ph > 0.45f) { Say("A corner is bought in the morning (and sold at evening)."); return false; }
    Town& T = towns[town];
    col.cornered = true; col.cornerT = time; col.cornerTown = town; col.cornerBuy = T.stock[G_FISH] * FishPrice(town);
    Say(TextFormat("You buy %s's whole catch at the morning price: it sells back at evening.", isles[T.isle].name.c_str()));
    return true;
}
bool World::Serenade(int nest) {
    if (!BendNow().serenade || nest < 0 || nest >= (int)col.nests.size()) return false;
    Nest& n = col.nests[nest];
    int day = (int)(time / DAY);
    if (!n.built || n.mate >= 0 || col.wildMates <= 0 || col.serenadeDay == day) return false;
    col.serenadeDay = day; n.mateT = 0.01f; n.bowl = n.bowlNeed;
    Say("The Founder sings at the nest: a mate answers at once.");
    return true;
}

// ---------------------------------------------------------------- towns (doc p26)
void World::InitTowns() {
    towns.clear();
    const TownData& D = TD();
    for (int i = 0; i < (int)isles.size(); i++) {
        if (isles[i].type != IsleType::Town) continue;
        Town t; t.isle = i;
        t.dock = isles[i].c;
        float bd = -1;   // (the dock: the town's longest jetty, its end over the water)
        for (const auto& p : isles[i].props) if (p.kind == 3) { float r = Vector2Distance({p.c.x, p.c.z}, {isles[i].c.x, isles[i].c.z}); if (r > bd) { bd = r; t.dock = {p.c.x, p.c.y + p.half.y + 0.3f, p.c.z}; } }
        if (bd < 0) t.dock = GroundAt(isles[i].c.x + isles[i].radius * 0.6f, isles[i].c.z);
        for (int g = 0; g < G_COUNT; g++) { t.price[g] = D.sell[g]; t.stock[g] = D.stock[g]; }
        t.stock[G_FISH] = 120;   // (the fleet's morning catch, in feed)
        t.rep.assign(sides.size() + 1, 0);
        towns.push_back(t);
    }
}
int World::NearestTown(Vector3 p, float r) const {
    int best = -1; float bd = r;
    for (int i = 0; i < (int)towns.size(); i++) { float d = Vector3Distance(p, towns[i].dock); if (d < bd) { bd = d; best = i; } }
    return best;
}
float World::FishPrice(int town) const {
    if (town < 0 || town >= (int)towns.size()) return 0;
    const TownData& D = TD();
    const Town& T = towns[town];
    float ph = DayPhase(), k = std::clamp((ph - 0.25f) / 0.5f, 0.0f, 1.0f);   // (the price climbs from the morning to the evening)
    float p = D.morning + (D.evening - D.morning) * k;
    p *= std::max(0.35f, 1 - D.glut * std::max(0.0f, T.stock[G_FISH] - 120));   // (a glut of fish: it pays less)
    if (T.storm > 0) p *= 2;
    return p;
}
float World::SellPrice(int town, int good) const {
    if (town < 0 || town >= (int)towns.size() || good <= G_FISH || good >= G_COUNT) return 1e9f;
    const Town& T = towns[town];
    float full = std::max(1.0f, TD().stock[good]);
    return T.price[good] * std::clamp(1.5f - T.stock[good] / full * 0.5f, 0.8f, 2.0f);   // (scarce: dearer)
}
bool World::TradeAt(int town, int feedIn, int good, int* got) {
    if (got) *got = 0;
    if (town < 0 || town >= (int)towns.size() || feedIn < 0 || good <= G_FISH || good >= G_COUNT) return false;
    Town& T = towns[town];
    if ((int)T.rep.size() <= cur) T.rep.resize(cur + 1, 0);
    if (T.rep[cur] <= TD().repShoo) { Say(isles[T.isle].name + "'s people shoo your birds away (your reputation there)."); return false; }
    // the colony's credit at the town (feed sold and not yet spent) buys as many of the good as it covers
    float value = feedIn * FishPrice(town) * BendNow().trade * (T.rep[cur] >= 50 ? 1.1f : 1.0f);
    T.stock[G_FISH] += feedIn;
    if (townCredit.size() < towns.size() * 8) townCredit.resize(towns.size() * 8, 0);
    float& cr = townCredit[town * 8 + cur];   // (each colony's credit at each town: feed sold, not yet spent)
    cr += value;
    float price = SellPrice(town, good);
    int n = 0;
    while (cr >= price && T.stock[good] >= 1) { cr -= price; T.stock[good] -= 1; n++; price = SellPrice(town, good); }
    if (n > 0) {
        if (good == G_TWIGS) col.twigs += n; else if (good == G_SHELLS) col.shells += n; else col.pearls += n;
        T.rep[cur] = std::min(100.0f, T.rep[cur] + TD().repTrade);
    }
    if (got) *got = n;
    return true;
}
void World::StepTowns(float dt) {
    const TownData& D = TD();
    for (auto& T : towns) {
        for (int g = 1; g < G_COUNT; g++) T.stock[g] = std::min(D.stock[g] * 1.5f, T.stock[g] + D.restock[g] * dt / DAY);
        T.stock[G_FISH] += (120 - T.stock[G_FISH]) * std::min(1.0f, D.recover * dt / DAY);   // (the town eats its fish; the fleet brings more)
        if (T.storm > 0) T.storm -= dt;
    }
    for (auto& o : offers) if (o.state == 0 && time - o.t > DAY) o.state = 3;   // (an offer unanswered for a day lapses)
    if (offers.size() > 64) offers.erase(offers.begin(), offers.begin() + 32);
}
bool World::Pelican(int town, int isle) {
    if (town < 0 || town >= (int)towns.size() || isle < 0 || isle >= (int)isles.size()) return false;
    if (Vector3Distance(me.pos, towns[town].dock) > 12) { Say("The old pelican sits on the town's dock: fly there."); return false; }
    int cost = (int)TD().pelican;
    if (col.pearls < cost) { Say("The old pelican wants a pearl."); return false; }
    col.pearls -= cost;
    Sighting s = TrueSighting(isle);
    s.exact = true; s.scouts = 2;
    know.sight[isle] = s;
    if (know.isle[isle] < 1) know.isle[isle] = 1;
    int owner = OwnerOf(isle);
    std::string line = TextFormat("The old pelican: %s holds %d nests, %d caches and %d birds.", owner >= 0 ? (owner == cur ? "your colony" : SideName(owner).c_str()) : isles[isle].name.c_str(), s.nests, s.caches, s.birds);
    know.log.push_back({time, 1, line, isles[isle].c});
    Say(line);
    return true;
}

// ---------------------------------------------------------------- barter between colonies (Trade 2)
bool World::Truce(int a, int b) const {
    size_t N = sides.size() + 1;
    if (a < 0 || b < 0 || (size_t)a >= N || (size_t)b >= N || truceUntil.size() < N * N) return false;
    return truceUntil[a * N + b] > time;
}
bool World::PayGoods(int side, const int goods[G_COUNT], bool take) {
    Colony& C = ColOf(side);
    float feed = 0; for (const auto& c : C.caches) for (const auto& f : c.fish) feed += f.size;
    if (feed < goods[G_FISH] || C.twigs < goods[G_TWIGS] || C.shells < goods[G_SHELLS] || C.pearls < goods[G_PEARLS]) return false;
    if (!take) return true;
    C.twigs -= goods[G_TWIGS]; C.shells -= goods[G_SHELLS]; C.pearls -= goods[G_PEARLS];
    float need = (float)goods[G_FISH];
    for (auto& c : C.caches) while (need > 0 && !c.fish.empty()) { need -= c.fish.back().size; c.fish.pop_back(); }
    return true;
}
void World::GiveGoods(int side, const int goods[G_COUNT]) {
    Colony& C = ColOf(side);
    C.twigs += goods[G_TWIGS]; C.shells += goods[G_SHELLS]; C.pearls += goods[G_PEARLS];
    int feed = goods[G_FISH];
    int sp = eco.map ? eco.map->SpeciesIndex("Mullet") : 0;
    for (auto& c : C.caches) while (feed > 0 && (int)c.fish.size() < Econ().cacheCap * 3) { int sz = std::min(2, feed); c.fish.push_back({sp, sz, 0}); feed -= sz; }
}
int World::MakeOffer(int from, int to, const int give[G_COUNT], const int get[G_COUNT], float truceDays) {
    if (from == to || from < 0 || to < 0 || from > (int)sides.size() || to > (int)sides.size()) return -1;
    if (!ColOf(from).HasTier(Tree::Trade, 2)) { SayTo(from, "Barter wants Trade 2 at the Roost."); return -1; }
    for (int g = 0; g < G_COUNT; g++) if (give[g] < 0 || get[g] < 0 || give[g] > 500 || get[g] > 500) return -1;
    if (!PayGoods(from, give, false)) { SayTo(from, "You don't have what you offer."); return -1; }
    Barter o; o.id = nextOffer++; o.from = from; o.to = to; o.truceDays = std::clamp(truceDays, 0.0f, 5.0f); o.t = time;
    for (int g = 0; g < G_COUNT; g++) { o.give[g] = give[g]; o.get[g] = get[g]; }
    offers.push_back(o);
    std::string what; for (int g = 0; g < G_COUNT; g++) if (give[g]) what += TextFormat("%s%d %s", what.empty() ? "" : ", ", give[g], GoodName(g));
    std::string want; for (int g = 0; g < G_COUNT; g++) if (get[g]) want += TextFormat("%s%d %s", want.empty() ? "" : ", ", get[g], GoodName(g));
    SayTo(to, TextFormat("%s offers %s for %s%s (the trade page).", SideName(from).c_str(), what.empty() ? "nothing" : what.c_str(), want.empty() ? "nothing" : want.c_str(), o.truceDays > 0 ? TextFormat(" and a truce of %.0f days", o.truceDays) : ""));
    SayTo(from, "Your offer is sent.");
    return o.id;
}
bool World::AnswerOffer(int side, int id, bool accept) {
    for (auto& o : offers) {
        if (o.id != id || o.state != 0 || o.to != side) continue;
        if (!accept) { o.state = 2; SayTo(o.from, SideName(side) + " refuses your offer."); return true; }
        if (!PayGoods(o.from, o.give, false) || !PayGoods(o.to, o.get, false)) { o.state = 3; SayTo(side, "The trade can't be made: someone lacks the goods."); return false; }
        PayGoods(o.from, o.give, true); PayGoods(o.to, o.get, true);
        GiveGoods(o.to, o.give); GiveGoods(o.from, o.get);
        size_t N = sides.size() + 1;
        if (o.truceDays > 0) { truceUntil.resize(N * N, -1); truceUntil[o.from * N + o.to] = truceUntil[o.to * N + o.from] = time + o.truceDays * DAY; }
        o.state = 1;
        SayTo(o.from, SideName(side) + " accepts your offer" + (o.truceDays > 0 ? TextFormat(": a truce for %.0f days.", o.truceDays) : "."));
        SayTo(side, "The trade is made.");
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- the colony's step (research, fervour, priests, the boom)
void World::StepSociety(float dt) {
    const ResearchData& D = RD();
    Colony& C = col;
    const Bend& B = BendNow();
    float day = dt / DAY;
    // the boom and bust
    float boom = C.boomState == 1 ? 1.6f : C.boomState == 2 ? 0.6f : 1.0f;
    if (C.boomCd > 0) C.boomCd -= dt;
    if (C.boomState > 0) {
        C.boomT -= dt;
        if (C.boomT <= 0) {
            if (C.boomState == 1) { C.boomState = 2; C.boomT = DAY; Say("The boom is over: a day of bust (-40%)."); for (int s = 0; s <= (int)sides.size(); s++) if (s != cur) SayTo(s, SideName(cur) + "'s banner turns grey: the Tycoon's bust day."); }
            else { C.boomState = 0; Say("The bust is over."); }
        }
    }
    // research at the Roost
    if (C.resTree >= 0) {
        if (!Built(ST_ROOST)) { /* (its Roost destroyed: the line stops) */ }
        else C.resLeft -= day * boom;
        if (C.resLeft <= 0) {
            Tree t = (Tree)C.resTree;
            int n = ++C.tier[C.resTree];
            C.resTree = -1;
            Say(TextFormat("Research: %s %d, %s - %s.", TreeName(t), n, ResearchOf(t, n).name.c_str(), ResearchOf(t, n).what.c_str()));
            if (t == Tree::Nesting && n == 3) {   // cliff nests: half as many sites again, on the rock round the old ones
                int add = (int)C.sites.size() / 2;
                for (int k = 0; k < add; k++) {
                    const Site& o = C.sites[k % C.sites.size()];
                    float a = k * 2.4f;
                    Site s; s.pos = GroundAt(o.pos.x + cosf(a) * 3, o.pos.z + sinf(a) * 3); s.pos.y = std::max(s.pos.y, o.pos.y * 0.8f);
                    if (LandAt(s.pos.x, s.pos.z)) C.sites.push_back(s);
                }
            }
        }
    }
    // guano: every bird makes it; it fattens the island's trees (twigs) and is the chemistry tree's base
    int birds = Alive();
    C.guano += birds * D.guano * B.guano * day;
    // the Tycoon's golden nest: a pearl a day
    if (B.goldenNest) { C.goldenT += day; while (C.goldenT >= 1) { C.goldenT -= 1; C.pearls += (int)D.goldenPearls; Say("The golden nest gives a pearl."); } }
    // fervour: priests raise it, it drifts back to its base, hunger drains it; the prayer is a burst
    if (C.prayerT > 0) C.prayerT -= dt;
    int priests = 0; const Structure* shrine = Built(ST_SHRINE);
    for (const auto& b : C.birds) priests += b.alive && b.stage == BStage::Adult && b.role == Role::Priest && b.retrainT <= 0;
    if (!shrine) priests = 0;
    float target = D.fBase + priests * 18.0f;
    float f = C.fervour;
    float gain = B.fervourGain * (ape.isle >= 0 && HolderOf(ape.isle) == cur ? 1 + SkullShrineFervour() : 1.0f);   // (skull island's summit shrine: +20% to its holder)
    if (f < target) f = std::min(target, f + (D.fDrift + priests * D.fPriest) * gain * day);
    else f = std::max(target, f - D.fDrift * day);
    if (birds > 2 && DaysOfFood() < 0.3f && FeedPerDayEstimate() < MouthsPerDay()) f += D.fStarve * day;
    C.fervour = std::clamp(f, 0.0f, B.fervourCap);
    // priests: conversion (Faith 3: wild birds; at Zeal, enemy birds within sight of the shrine); offerings (Faith 2)
    if (shrine && priests > 0) {
        static const float CONV_STEP = 1;
        float conv = priests * D.convert * B.convert * day;
        C.convertAcc += conv;
        while (C.convertAcc >= CONV_STEP) {
            C.convertAcc -= CONV_STEP;
            if (FervourBand() >= 4) {   // an enemy bird near the shrine turns
                bool done = false;
                for (int s = 0; s <= (int)sides.size() && !done; s++) {
                    if (s == cur || Truce(cur, s)) continue;
                    for (auto& e : ColOf(s).birds) {
                        if (!e.alive || e.stage != BStage::Adult || Vector2Distance({e.pos.x, e.pos.z}, {shrine->pos.x, shrine->pos.z}) > 70) continue;
                        e.alive = false; e.cause = "converted by " + SideName(cur) + "'s priests";
                        Bird nb; nb.id = C.nextId++; nb.stage = BStage::Adult; nb.role = e.role; nb.hp = RoleOf(nb.role).hp; nb.fight = 25; nb.hunger = 1; nb.pos = e.pos;
                        C.birds.push_back(nb); C.converted++;
                        SayTo(s, TextFormat("One of your %ss turns to %s's shrine!", RoleName(e.role), SideName(cur).c_str()));
                        Say(TextFormat("A %s of %s's turns to your shrine.", RoleName(e.role), SideName(s).c_str()));
                        done = true; break;
                    }
                }
                if (done) continue;
            }
            if (C.HasTier(Tree::Faith, 3) && C.wildMates > 0) {
                C.wildMates--; C.converted++;
                Bird nb; nb.id = C.nextId++; nb.stage = BStage::Adult; nb.role = Role::Fisher; nb.hp = RoleOf(Role::Fisher).hp; nb.fight = 25; nb.hunger = 0.8f; nb.pos = Vector3Add(shrine->pos, {0, 2, 0});
                C.birds.push_back(nb);
                Say("A wild bird joins the colony at the shrine (a fisher).");
            }
        }
        if (C.HasTier(Tree::Faith, 2) && DaysOfFood() > 2.5f && fmodf(time, DAY) < dt) {   // (the priests offer the surplus each dawn)
            for (int k = 0; k < priests; k++) {
                int ci = NearestCache(shrine->pos, true, false); if (ci < 0) break;
                auto& fish = col.caches[ci].fish; int sz = fish.back().size; fish.pop_back();
                C.fervour = std::min(B.fervourCap, C.fervour + (sz >= 4 ? 10.0f : sz * D.fOffer) * B.fervourGain);
            }
        }
    }
    // the Sigma's corner sells at evening
    if (C.cornered && C.cornerTown >= 0 && C.cornerT > 0 && DayPhase() > 0.7f && time - C.cornerT < DAY) {
        Town& T = towns[C.cornerTown];
        float sell = T.stock[G_FISH] * FishPrice(C.cornerTown);
        int pearls = std::max(0, (int)((sell - C.cornerBuy) / std::max(1.0f, SellPrice(C.cornerTown, G_PEARLS))));
        C.pearls += pearls; C.cornerT = -1;
        Say(TextFormat("The cornered catch sells at evening: %d pearls of profit.", pearls));
    }
    // the Cuckoo's chicks in other nests report every day
    if (fmodf(time, DAY) < dt) for (int isle : C.spies) if (isle >= 0 && isle < (int)isles.size()) {
        know.sight[isle] = TrueSighting(isle); know.sight[isle].exact = true;
        know.log.push_back({time, 1, "Your cuckoo chick reports from " + isles[isle].name + TextFormat(": %d nests, %d birds.", know.sight[isle].nests, know.sight[isle].birds), isles[isle].c});
    }
}

// ---------------------------------------------------------------- Traders and Priests
void World::TraderStep(Bird& b, float dt) {
    const RoleDef& R = RoleOf(Role::Trader);
    const TownData& D = TD();
    if (b.hunger < 0.35f && b.carrySp < 0 && b.carryN == 0 && BirdEatsAtCache(b, dt)) return;
    // home with goods: into the stores
    if (b.carryN > 0) {
        b.task = Task::Deliver;
        if (MoveTo(b, Vector3Add(col.caches[0].pos, {0, 0.6f, 0}), R.speed, dt)) {
            if (b.carryGood == G_TWIGS) col.twigs += b.carryN; else if (b.carryGood == G_SHELLS) col.shells += b.carryN; else if (b.carryGood == G_PEARLS) col.pearls += b.carryN;
            Say(TextFormat("A Trader comes home with %d %s.", b.carryN, GoodName(b.carryGood)));
            b.carryN = 0; b.carryGood = -1; b.task = Task::Idle;
        }
        return;
    }
    int town = NearestTown(b.pos, 3000);
    if (Walled(home, cur) && Vector3Distance(b.pos, col.caches[0].pos) < 40 && b.carrySp < 0) town = -1;   // (a hostile Wall over the island: the Traders stay in)
    if (town < 0) { b.task = Task::Sit; MoveTo(b, Vector3Add(col.caches[0].pos, {1.6f, 0.4f, 1.6f}), R.speed, dt, 0.4f); return; }
    // out with fish: to the dock, sold for the colony's chosen good
    if (b.carrySp >= 0) {
        b.task = Task::Fly;
        if (MoveTo(b, Vector3Add(towns[town].dock, {0, 1, 0}), R.speed * BendNow().speed, dt, 2)) {
            int got = 0;
            TradeAt(town, b.carrySize, col.tradeFor, &got);
            b.carrySp = -1; b.carrySize = 0;
            if (got > 0) { b.carryGood = col.tradeFor; b.carryN = got; }
            else b.task = Task::Idle;
        }
        return;
    }
    // the surplus: what's beyond two days of food
    float surplus = CacheFeed() - MouthsPerDay() * D.surplusDays;
    int cap = (int)(4 * BendNow().traderCarry);
    if (surplus < 2) { b.task = Task::Sit; MoveTo(b, Vector3Add(col.caches[0].pos, {1.6f, 0.4f, 1.6f}), R.speed, dt, 0.4f); return; }
    int ci = NearestCache(b.pos, true, false);
    if (ci < 0) return;
    b.task = Task::Fetch;
    if (MoveTo(b, Vector3Add(col.caches[ci].pos, {0, 0.6f, 0}), R.speed, dt)) {
        auto& fish = col.caches[ci].fish;
        int load = 0; int sp = -1;
        while (!fish.empty() && load + fish.back().size <= cap && load < surplus) { load += fish.back().size; sp = fish.back().sp; fish.pop_back(); }
        if (load > 0) { b.carrySp = sp; b.carrySize = load; }
    }
}
void World::PriestStep(Bird& b, float dt) {
    if (b.hunger < 0.35f && BirdEatsAtCache(b, dt)) return;
    const Structure* sh = Built(ST_SHRINE);
    Vector3 at = sh ? Vector3Add(sh->pos, {1.2f * cosf(b.id * 1.9f), 0.6f, 1.2f * sinf(b.id * 1.9f)}) : Vector3Add(col.caches[0].pos, {-1.5f, 0.4f, -1.5f});
    b.task = MoveTo(b, at, RoleOf(Role::Priest).speed, dt, 0.4f) ? Task::Sit : Task::Fly;
    if (b.task == Task::Sit) b.vel = {0, 0, 0};
}

// ---------------------------------------------------------------- a bot's society: the Roost, research, the shrine, trade
void World::BotSociety(float dt) {
    Colony& C = col;
    const Bend& B = BendNow();
    int alive = Alive();
    // the Roost once the colony is under way; the shrine once it can
    bool roost = false, shrine = false; for (const auto& s : C.builds) { roost |= s.kind == ST_ROOST; shrine |= s.kind == ST_SHRINE; }
    auto layOut = [&](int kind, Vector3 off) { Structure s; s.kind = kind; s.pos = GroundAt(C.caches[0].pos.x + off.x, C.caches[0].pos.z + off.z); s.isle = home; C.builds.push_back(s); };
    if (!roost && alive >= 3) layOut(ST_ROOST, {-5, 0, 3});
    if (!shrine && C.HasTier(Tree::Faith, 1) && alive >= 8) layOut(ST_SHRINE, {4, 0, -5});
    // research: the founder's order of work (doc p22: Nesting 1 and Fishing 1 by day 2, War 1-2 by day 4, Faith or
    // Trade by temperament, the Works from day 5, tier 4s from day 10)
    if (C.resTree < 0 && Built(ST_ROOST)) {
        static const Tree BASE[] = {Tree::Nesting, Tree::Fishing, Tree::War, Tree::War, Tree::Trade, Tree::Caches, Tree::Fishing, Tree::Faith, Tree::Nesting, Tree::Caches, Tree::War,
                                    Tree::Flight, Tree::Trade, Tree::Faith, Tree::Fishing, Tree::Flight, Tree::Nesting, Tree::Caches, Tree::Faith, Tree::War, Tree::Fishing, Tree::Trade};
        std::vector<Tree> order;
        auto pri = [&](Tree t, int times) { for (int k = 0; k < times; k++) order.push_back(t); };
        if (B.cornering || B.trade > 1.2f) pri(Tree::Trade, 2);
        if (B.fervourGain > 1.3f) pri(Tree::Faith, 3);
        if (B.attack > 1.2f) pri(Tree::War, 2);
        if (B.earlyTrees) { pri(Tree::Bombing, 2); pri(Tree::Chemistry, 2); }
        for (Tree t : BASE) order.push_back(t);
        int seen[(int)Tree::COUNT] = {};
        for (Tree t : order) {
            int want = ++seen[(int)t];
            if (C.tier[(int)t] >= want) continue;
            if (CanResearch(t)) { StartResearch(t); break; }
            if (C.tier[(int)t] + 1 == want) break;   // (save for this one)
        }
    }
    // Traders: the colony's pearls (pearls first, shells when the research wants them)
    ResearchCost next = ResearchCostOf(std::min(4, 1 + C.tier[(int)Tree::Fishing]));
    C.tradeFor = C.shells < next.shells && C.pearls >= next.pearls ? G_SHELLS : G_PEARLS;
    // the founders' buttons
    if (B.boom && C.boomState == 0 && C.boomCd <= 0 && alive >= 6 && DaysOfFood() > 1) StartBoom();
    if (B.cornering && !C.cornered && time > DAY * 1.5f) { int t = NearestTown(C.caches[0].pos, 5000); if (t >= 0 && DayPhase() > 0.22f && DayPhase() < 0.4f) Corner(t); }
    if (B.serenade && C.serenadeDay != (int)(time / DAY)) for (int i = 0; i < (int)C.nests.size(); i++) if (C.nests[i].built && C.nests[i].mate < 0 && C.nests[i].mateT < 0 && (DaysOfFood() > 1 || Count(BStage::Mate) == 0)) { Serenade(i); break; }
    // offers to this colony: fair or better, and it can pay
    auto value = [](const int g[G_COUNT]) { return g[G_FISH] * 1.0f + g[G_TWIGS] * 0.5f + g[G_SHELLS] * 1.0f + g[G_PEARLS] * 8.0f; };
    for (auto& o : offers) if (o.state == 0 && o.to == cur) {
        bool fair = value(o.give) + o.truceDays * 3 >= value(o.get) * 1.1f;
        AnswerOffer(cur, o.id, fair && PayGoods(cur, o.get, false));
    }
    (void)dt;
}

// ---------------------------------------------------------------- --flight-sim founders: every founder's window (the stage-6 gate)
// Each species founds a bot colony on the same maps (the same rivals, the same seeds), and its score is read early
// (day 3), in the middle (day 6) and late (the last day). A founder's window is the phase where it stands highest
// against the field: the doc wants the Taloned, Strongbird and Lyrebird strong early; the Beaked, Swift, Shadow and
// Cuckoo mid-game; the Sigma, Ibis, Tycoon, Alchemist and Albatross late (doc p10). The phase model's +/-3% overall is
// stage 8's balance pass; this run shows where each one stands now.
int RunFlightFounders(int days, int seeds) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::string why;
    if (!rt::DataOk(&why)) { printf("no data: %s\n", why.c_str()); return 1; }
    const auto& F = Founders();
    int nf = (int)F.size();
    days = std::max(6, days); seeds = std::max(1, seeds);
    int marks[3] = {3, 6, days};
    { World warm; MapOpts o; o.players = 3; warm.Init("taloned", 1, o); for (int k = 0; k < 20; k++) warm.Step(0.1f, FounderInput{}); BendOf(0); }   // (every table loaded before the threads)
    std::vector<std::array<double, 3>> score(nf, {0, 0, 0});
    std::vector<std::array<double, 3>> birds(nf, {0, 0, 0});
    std::vector<int> research(nf, 0);
    auto run = [&](int fi) {
        for (int s = 0; s < seeds; s++) {
            auto w = std::make_unique<World>();
            MapOpts o; o.players = 3; o.arr = Arrangement::Archipelago; o.home = IsleType::Tropical;
            w->Init(F[fi].key, 4001 + s * 977, o);
            w->founderBot = true; w->me.st = FState::Fly; w->me.pos = Vector3Add(w->island.nest, {0, 2, 0}); w->me.hunger = 1;
            int m = 0;
            for (float t = 0; t < days * World::DAY && m < 3; t += 0.1f) {
                w->BotGovern(0.1f); w->BotWar(0, 0.1f);
                w->Step(0.1f, FounderInput{});
                if (w->time >= marks[m] * World::DAY) { score[fi][m] += w->Score(0).total; birds[fi][m] += w->Alive(); m++; }
            }
            for (int t = 0; t < (int)Tree::COUNT; t++) research[fi] += w->col.tier[t];
        }
    };
    unsigned nt = std::max(1u, std::min(std::thread::hardware_concurrency(), (unsigned)nf));
    if (getenv("DEPTH_THREADS")) nt = std::max(1, atoi(getenv("DEPTH_THREADS")));
    printf("The Flight --flight-sim founders: %d founders x %d seeds, %d days each (%u threads)\n", nf, seeds, days, nt);
    auto t0 = std::chrono::steady_clock::now();
    std::atomic<int> next{0};
    std::vector<std::thread> pool;
    for (unsigned k = 0; k < nt; k++) pool.emplace_back([&] { int fi; while ((fi = next++) < nf) run(fi); });
    for (auto& th : pool) th.join();
    double mean[3] = {};
    for (int m = 0; m < 3; m++) { for (int fi = 0; fi < nf; fi++) mean[m] += score[fi][m]; mean[m] /= std::max(1, nf); }
    static const char* PH[3] = {"early", "mid", "late"};
    static const char* WANT[12][2] = {{"taloned", "early"}, {"strongbird", "early"}, {"lyrebird", "early"}, {"beaked", "mid"}, {"swift", "mid"}, {"shadow", "mid"}, {"cuckoo", "mid"},
                                      {"sigma", "late"}, {"ibis", "late"}, {"tycoon", "late"}, {"alchemist", "late"}, {"albatross", "late"}};
    printf("  %-16s %9s %9s %9s   %-6s %-6s  overall  research  birds(late)\n", "founder", "day 3", "day 6", TextFormat("day %d", days), "window", "doc");
    int matched = 0;
    for (int fi = 0; fi < nf; fi++) {
        double rel[3]; int best = 0;
        for (int m = 0; m < 3; m++) { rel[m] = mean[m] > 0 ? score[fi][m] / mean[m] : 1; if (rel[m] > rel[best]) best = m; }
        double overall = (rel[0] + rel[1] + rel[2]) / 3;
        const char* doc = "?"; for (auto& wnt : WANT) if (F[fi].key == wnt[0]) doc = wnt[1];
        matched += std::string(doc) == PH[best];
        printf("  %-16s %8.0f%% %8.0f%% %8.0f%%   %-6s %-6s  %+5.0f%%   %5.1f    %5.1f\n", F[fi].name.c_str(), rel[0] * 100, rel[1] * 100, rel[2] * 100, PH[best], doc, (overall - 1) * 100, research[fi] / (double)seeds, birds[fi][2] / seeds);
    }
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("  (scores relative to the field at each mark; mean scores %.0f / %.0f / %.0f; %d of %d windows where the doc puts them; %.0f s)\n", mean[0] / seeds, mean[1] / seeds, mean[2] / seeds, matched, nf, secs);
    bool ok = nf == 12 && mean[2] > mean[0];
    printf(ok ? "flight-sim founders: every founder's window shown\n" : "flight-sim founders: FAILED\n");
    return ok ? 0 : 1;
}

// ---------------------------------------------------------------- --flight-society-test
namespace {
Bird& AddAdult(World& w, Role r, Vector3 p) { Bird b; b.id = w.col.nextId++; b.stage = BStage::Adult; b.role = r; b.hp = RoleOf(r).hp; b.fight = 25; b.hunger = 1; b.pos = p; w.col.birds.push_back(b); return w.col.birds.back(); }
void BuildIt(World& w, int kind) { Structure s; s.kind = kind; s.pos = w.GroundAt(w.col.caches[0].pos.x + 4, w.col.caches[0].pos.z + 3); s.built = true; s.isle = w.home; w.col.builds.push_back(s); }
}
int RunFlightSocietyTest() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Flight, stage 6: research, faith, trade, the founders\n");
    std::string why;
    if (!rt::DataOk(&why)) { printf("FAIL: no data: %s\n", why.c_str()); return 1; }
    // the founders' bends
    {
        int set = 0; for (int i = 0; i < (int)Founders().size(); i++) { const Bend& b = BendOf(i); set += b.fishHit != 1 || b.carry || b.speed != 1 || b.attack != 1 || b.boom || b.eggTheft || b.mateTime != 1 || b.fervourCap != 100 || b.trade != 1 || b.fervourGain != 1 || b.guano != 1 || b.stamina != 1; }
        check(set == 12, TextFormat("all twelve founders bend their colonies (%d of 12 read)", set));
        check(BendOf(FounderIndex("tycoon")).boom && BendOf(FounderIndex("cuckoo")).eggTheft && BendOf(FounderIndex("sigma")).noFaith && BendOf(FounderIndex("alchemist")).earlyTrees == ((1 << (int)Tree::Bombing) | (1 << (int)Tree::Chemistry)), "the uniques: the Tycoon's boom, the Cuckoo's egg theft, the Sigma's lack of faith, the Alchemist's early trees");
    }
    auto fresh = [](const char* founder, int players = 4, uint32_t seed = 31) { auto w = std::make_unique<World>(); MapOpts o; o.players = players; w->Init(founder, seed, o); return w; };
    // research: a Roost, pearls and shells, half a day
    {
        auto w = fresh("taloned");
        w->col.pearls = 0; w->col.shells = 0;
        check(!w->CanResearch(Tree::Nesting), "no Roost, no research");
        BuildIt(*w, ST_ROOST);
        std::string no; w->CanResearch(Tree::Nesting, &no);
        check(!w->CanResearch(Tree::Nesting) && no.find("pearls") != std::string::npos, "a Roost but no pearls: " + no);
        w->col.pearls = 3; w->col.shells = 12;
        check(w->StartResearch(Tree::Nesting) && w->col.pearls == 1 && w->col.shells == 2, "Nesting 1 costs 2 pearls and 10 shells");
        for (float t = 0; t < World::DAY * 0.55f; t += 0.1f) w->Step(0.1f, FounderInput{w->me.yaw});
        check(w->col.tier[(int)Tree::Nesting] == 1 && w->col.resTree < 0, "and takes half a day");
        check(!w->RoleUnlocked(Role::Skirmisher) && !w->Retrain(Role::Skirmisher), "Skirmishers want War 2: no retraining into one before it");
        w->col.tier[(int)Tree::War] = 2;
        AddAdult(*w, Role::Fisher, w->col.caches[0].pos);
        check(w->RoleUnlocked(Role::Skirmisher) && w->Retrain(Role::Skirmisher, Role::Fisher), "and with it, a fisher retrains as one");
        w->col.pearls = 50; w->col.shells = 200; w->col.tier[(int)Tree::Bombing] = 0;
        check(!w->CanResearch(Tree::Bombing), "the Works don't open before day 5 (the Alchemist's, day 4)");
        w->col.tier[(int)Tree::Caches] = 2;
        check(w->CacheCap() == 50 && w->SpoilDays() >= 6, "Smokehouse and Big caches: fish keep six days, a cache holds 50");
    }
    // towns and trade
    {
        auto w = fresh("sigma", 4, 7);
        w->col.pearls = 0;
        check(!w->towns.empty(), TextFormat("the map has %d fishing towns", (int)w->towns.size()));
        if (!w->towns.empty()) {
            float morning = 0, evening = 0;
            w->time = World::DAY * (0.3f - 0.22f); morning = w->FishPrice(0);
            w->time = World::DAY * (0.72f - 0.22f); evening = w->FishPrice(0);
            check(evening > morning * 1.3f, TextFormat("fish pays more at evening (%.2f) than in the morning (%.2f)", evening, morning));
            int got = 0;
            w->TradeAt(0, 6, G_PEARLS, &got);
            int got2 = 0; w->TradeAt(0, 6, G_PEARLS, &got2);
            check(got + got2 >= 1 && w->col.pearls == got + got2, TextFormat("12 feed of fish sold buys %d pearls for the Sigma (credit carries over)", got + got2));
            auto t2 = fresh("ibis", 4, 7); t2->col.pearls = 0; int g3 = 0, g4 = 0; t2->time = w->time; t2->TradeAt(0, 6, G_PEARLS, &g3); t2->TradeAt(0, 6, G_PEARLS, &g4);
            check(got + got2 >= g3 + g4, TextFormat("the Sigma trades better than the Ibis (%d vs %d pearls)", got + got2, g3 + g4));
            w->me.pos = w->towns[0].dock; w->col.pearls = 2;
            int target = w->HomeOf(1);
            check(w->Pelican(0, target) && w->know.sight[target].exact && w->col.pearls == 1, "the old pelican: a pearl for an exact count of a rival's island");
        }
    }
    // faith
    {
        auto w = fresh("ibis");
        w->col.tier[(int)Tree::Faith] = 3; BuildIt(*w, ST_SHRINE);
        for (int k = 0; k < 3; k++) AddAdult(*w, Role::Priest, w->col.caches[0].pos);
        for (int k = 0; k < 20; k++) w->col.caches[0].fish.push_back({w->eco.map->SpeciesIndex("Mullet"), 2, 0});
        float f0 = w->col.fervour; int wild0 = w->col.wildMates;
        for (float t = 0; t < World::DAY; t += 0.1f) w->Step(0.1f, FounderInput{w->me.yaw});
        check(w->col.fervour > f0 + 20, TextFormat("three priests at a shrine raise fervour over a day: %.0f -> %.0f", f0, w->col.fervour));
        check(w->col.converted >= 3 && w->col.wildMates < wild0, TextFormat("and convert wild birds (Faith 3, the Ibis twice as fast): %d in a day", w->col.converted));
        w->me.st = FState::Perched; w->me.pos = w->Built(ST_SHRINE)->pos;
        float b0 = w->FervourBand();
        bool prayed = w->Pray();
        check(prayed && !w->Pray() && w->col.prayerT > 0, TextFormat("the Founder prays once a day (band %.0f -> %.0f)", b0, w->FervourBand()));
        auto s = fresh("shadow"); s->col.fervour = 99;
        check(s->FervourBand() <= 3 && BendOf(s->me.def).fervourCap == 80, "the Shadow's fervour can't pass 80");
    }
    // the Tycoon's boom; the Lyrebird's serenade
    {
        auto w = fresh("tycoon");
        check(w->StartBoom() && w->col.boomState == 1 && !w->StartBoom(), "the Tycoon booms (once, then a cooldown)");
        for (float t = 0; t < World::DAY * 2.1f; t += 0.1f) w->Step(0.1f, FounderInput{w->me.yaw});
        check(w->col.boomState == 2, "two days of boom, then the bust");
        for (float t = 0; t < World::DAY * 1.0f; t += 0.1f) w->Step(0.1f, FounderInput{w->me.yaw});
        check(w->col.boomState == 0 && w->col.pearls >= 4, TextFormat("a day of bust, then over; the golden nest gave %d pearls", w->col.pearls));
        auto l = fresh("lyrebird");
        check(l->Serenade(0) && !l->Serenade(0), "the Lyrebird's song calls a mate at once, once a day");
    }
    // barter and truces
    {
        auto w = fresh("sigma");
        w->col.tier[(int)Tree::Trade] = 2; w->col.pearls = 6; w->col.shells = 0;
        Colony& B = w->ColOf(1); B.shells = 60; B.pearls = 0;
        int give[G_COUNT] = {0, 0, 0, 3}, get[G_COUNT] = {0, 0, 20, 0};
        int id = w->MakeOffer(0, 1, give, get, 2);
        check(id > 0, "an offer to a bot colony: 3 pearls for 20 shells and a two-day truce");
        w->SwapSide(0); w->BotSociety(0.1f); w->SwapSide(0);
        check(w->col.pearls == 3 && w->col.shells >= 20 && B.pearls == 3 && w->Truce(0, 1), "the bot accepts a fair offer: the goods change hands, the truce holds");
        check(!w->Truce(0, 2), "(only between those two)");
        int give2[G_COUNT] = {0, 0, 0, 0}, get2[G_COUNT] = {0, 0, 0, 30};
        w->ColOf(1).pearls = 40;
        int id2 = w->MakeOffer(0, 1, give2, get2, 0);
        w->SwapSide(0); w->BotSociety(0.1f); w->SwapSide(0);
        bool refused = false; for (const auto& o : w->offers) if (o.id == id2) refused = o.state == 2;
        check(refused, "and refuses a bad one");
    }
    // egg theft (the Cuckoo): a Skirmisher at an enemy nest carries an egg home
    {
        auto w = fresh("cuckoo", 2, 13);
        for (int k = 0; k < 20; k++) w->col.caches[0].fish.push_back({w->eco.map->SpeciesIndex("Mullet"), 2, 0});   // (a fed colony: its flock holds together)
        Colony& T = w->ColOf(1);
        for (int k = 0; k < 2; k++) { Bird e; e.id = T.nextId++; e.stage = BStage::Egg; e.nest = 0; e.pos = T.nests[0].pos; e.hunger = 1; T.birds.push_back(e); }
        std::vector<int> ids;
        for (int k = 0; k < 2; k++) ids.push_back(AddAdult(*w, Role::Skirmisher, Vector3Add(T.nests[0].pos, {-60.0f - k * 2, 20, 0})).id);
        int f = w->MakeFlock(0, ids, Formation::Chevron, Alt::Mid, Stance::Raid);
        w->OrderFlock(0, f, Target::Nests, 1, w->HomeOf(1), -1, -1, T.nests[0].pos);
        for (float t = 0; t < 120 && w->col.eggsStolen == 0; t += 0.05f) w->Step(0.05f, FounderInput{w->me.yaw});
        if (w->col.eggsStolen == 0) { Flock* F = w->FindFlock(0, f); int eggs = 0; for (const auto& e : T.birds) eggs += e.alive && e.stage == BStage::Egg;
            printf("    (flock %s, target %d, members %d; eggs left %d; nest at %.0f,%.0f,%.0f;", F ? "alive" : "gone", F ? (int)F->target : -1, F ? (int)F->members.size() : 0, eggs, T.nests[0].pos.x, T.nests[0].pos.y, T.nests[0].pos.z);
            for (const auto& b : w->col.birds) if (b.alive && b.role == Role::Skirmisher) printf(" bird at %.0f,%.0f,%.0f carry %d tgt %d;", b.pos.x, b.pos.y, b.pos.z, b.carrySp, b.tgtSide); printf(")\n"); }
        check(w->col.eggsStolen >= 1, TextFormat("the Cuckoo's Skirmishers steal %d egg(s) home", w->col.eggsStolen));
    }
    // a bot colony's society over six days: the Roost, shells, pearls by trade, research
    {
        auto w = fresh("taloned", 4, 41);
        w->founderBot = true; w->me.st = FState::Fly; w->me.pos = Vector3Add(w->island.nest, {0, 2, 0}); w->me.hunger = 1;
        for (int d = 1; d <= 8; d++) {
            for (float t = 0; t < World::DAY; t += 0.1f) { w->BotGovern(0.1f); w->Step(0.1f, FounderInput{}); }
            std::string tiers; for (int t = 0; t < (int)Tree::COUNT; t++) tiers += TextFormat("%d", w->col.tier[t]);
            bool roost = w->Built(ST_ROOST) != nullptr;
            printf("    day %d: %d birds, roost %s, pearls %d, shells %d, research %s%s, fervour %.0f, traders %d\n", d, w->Alive(), roost ? "built" : "no", w->col.pearls, w->col.shells, tiers.c_str(),
                   w->col.resTree >= 0 ? TextFormat(" (+%s)", TreeName((Tree)w->col.resTree)) : "", w->col.fervour, w->Count(BStage::Adult, Role::Trader));
        }
        { std::string d; for (const auto& x : w->col.deaths) d += TextFormat(" %s %d;", x.first.c_str(), x.second); printf("    deaths:%s builders %d\n", d.c_str(), w->Count(BStage::Adult, Role::Builder)); }
        int total = 0; for (int t = 0; t < (int)Tree::COUNT; t++) total += w->col.tier[t];
        check(w->Built(ST_ROOST) && total >= 1, TextFormat("a bot colony raises its Roost and researches %d tier(s) in eight days (the pace is stage 8's balance)", total));
    }
    printf(fails ? "flight-society-test: %d check(s) failed\n" : "flight-society-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}

}  // namespace fl
