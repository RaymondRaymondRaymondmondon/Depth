// The Flight over the network (see flight_net.h): a Founder's input, the orders a colony takes, the score and the end
// of a match, the per-player snapshot (one Visit walks every field the screens draw, writing and reading, so the two
// can never disagree), the host's GameHost, and the tests: --flight-net-test and --net-loop flight (the stage-5 gate).
#include "flight_net.h"
#include "arcade_session.h"
#include "net.h"
#include "json.h"
#include "raymath.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <thread>

namespace fl {

// ---------------------------------------------------------------- input
void WriteInput(const FounderInput& in, Writer& w) {
    auto q = [](float v) { return (uint32_t)(int8_t)std::clamp((int)lroundf(v * 127), -127, 127) & 0xFF; };
    w.U8(FA_INPUT);
    w.F32(in.yaw); w.F32(in.pitch);
    w.U8(q(in.steer.x)); w.U8(q(in.steer.y));
    w.U8((in.flap ? FI_FLAP : 0) | (in.sprint ? FI_SPRINT : 0) | (in.brake ? FI_BRAKE : 0) | (in.interact ? FI_INTERACT : 0) | (in.eat ? FI_EAT : 0) | (in.takeoff ? FI_TAKEOFF : 0));
}
bool ReadInput(Reader& r, FounderInput& in) {
    auto dq = [](uint32_t b) { return (int8_t)(uint8_t)b / 127.0f; };
    in.yaw = r.F32(); in.pitch = r.F32();
    in.steer.x = dq(r.U8()); in.steer.y = dq(r.U8());
    uint32_t b = r.U8();
    if (r.bad || !std::isfinite(in.yaw) || !std::isfinite(in.pitch)) return false;
    in.yaw = remainderf(in.yaw, 2 * PI);
    in.pitch = std::clamp(in.pitch, -1.5f, 1.5f);
    in.flap = b & FI_FLAP; in.sprint = b & FI_SPRINT; in.brake = b & FI_BRAKE;
    in.interact = b & FI_INTERACT; in.eat = b & FI_EAT; in.takeoff = b & FI_TAKEOFF;
    return true;
}
void MergeInput(FounderInput& into, const FounderInput& n) {
    bool i = into.interact || n.interact, e = into.eat || n.eat, t = into.takeoff || n.takeoff;
    into = n;
    into.interact = i; into.eat = e; into.takeoff = t;
}
void ClearPresses(FounderInput& in) { in.interact = in.eat = in.takeoff = false; }

// ---------------------------------------------------------------- orders
void OrderHello(Writer& w, const std::string& name, const std::string& founderKey, const std::string& look) { w.U8(FA_HELLO); w.Str(name.substr(0, 24)); w.Str(founderKey); w.Str(look.substr(0, 80)); }
void OrderPlan(Writer& w, Role r, float share) { w.U8(FA_PLAN); w.U8((uint8_t)r); w.F32(share); }
void OrderNests(Writer& w, int n) { w.U8(FA_NESTS); w.I32(n); }
void OrderGround(Writer& w, int zone) { w.U8(FA_GROUND); w.I32(zone); }
void OrderRest(Writer& w, float below) { w.U8(FA_REST); w.F32(below); }
void OrderRetrain(Writer& w, Role to, Role from) { w.U8(FA_RETRAIN); w.U8((uint8_t)to); w.U8((uint8_t)from); }
void OrderScout(Writer& w, int isle, int zone, Vector3 at, Alt alt) { w.U8(FA_SCOUT); w.I32(isle); w.I32(zone); w.F32(at.x); w.F32(at.z); w.U8((uint8_t)alt); }
void OrderFlockMake(Writer& w, const std::vector<int>& ids, Formation f, Alt a, Stance s) {
    w.U8(FA_FLOCK_MAKE); w.VarU((uint32_t)std::min<size_t>(ids.size(), 64));
    for (size_t k = 0; k < ids.size() && k < 64; k++) w.I32(ids[k]);
    w.U8((uint8_t)f); w.U8((uint8_t)a); w.U8((uint8_t)s);
}
void OrderFlockSet(Writer& w, int flock, Formation f, Alt a, Stance s) { w.U8(FA_FLOCK_SET); w.I32(flock); w.U8((uint8_t)f); w.U8((uint8_t)a); w.U8((uint8_t)s); }
void OrderFlockTarget(Writer& w, int flock, Target t, int tSide, int tIsle, int tZone, int tFlock, Vector3 at) {
    w.U8(FA_FLOCK_ORDER); w.I32(flock); w.U8((uint8_t)t); w.I32(tSide); w.I32(tIsle); w.I32(tZone); w.I32(tFlock); w.F32(at.x); w.F32(at.z);
}
void OrderFlockHome(Writer& w, int flock) { w.U8(FA_FLOCK_HOME); w.I32(flock); }
void OrderFlockDisband(Writer& w, int flock) { w.U8(FA_FLOCK_DISBAND); w.I32(flock); }
void OrderLead(Writer& w) { w.U8(FA_LEAD); }
void OrderBuild(Writer& w, int kind) { w.U8(FA_BUILD); w.U8((uint8_t)kind); }
void OrderResearch(Writer& w, Tree t) { w.U8(FA_RESEARCH); w.U8((uint8_t)t); }
void OrderTradeFor(Writer& w, int good) { w.U8(FA_TRADEFOR); w.U8((uint8_t)good); }
void OrderBoom(Writer& w) { w.U8(FA_BOOM); }
void OrderCorner(Writer& w, int town) { w.U8(FA_CORNER); w.I32(town); }
void OrderPelican(Writer& w, int town, int isle) { w.U8(FA_PELICAN); w.I32(town); w.I32(isle); }
void OrderBarter(Writer& w, int to, const int give[G_COUNT], const int get[G_COUNT], float truceDays) {
    w.U8(FA_BARTER); w.I32(to);
    for (int g = 0; g < G_COUNT; g++) w.I32(give[g]);
    for (int g = 0; g < G_COUNT; g++) w.I32(get[g]);
    w.F32(truceDays);
}
void OrderAnswer(Writer& w, int offer, bool accept) { w.U8(FA_ANSWER); w.I32(offer); w.U8(accept ? 1 : 0); }
void OrderFound(Writer& w, int isle) { w.U8(FA_FOUND); w.I32(isle); }
void OrderDose(Writer& w, int flock, int stim) { w.U8(FA_DOSE); w.I32(flock); w.U8((uint8_t)stim); }
void OrderBrew(Writer& w, int stim) { w.U8(FA_BREW); w.U8((uint8_t)stim); }
void OrderDecree(Writer& w, int k) { w.U8(FA_DECREE); w.U8((uint8_t)k); }
void OrderPerk(Writer& w, int k) { w.U8(FA_PERK); w.U8((uint8_t)k); }
void OrderWantTrait(Writer& w, int trait) { w.U8(FA_WANT_TRAIT); w.I32(trait); }
void OrderHire(Writer& w, int target) { w.U8(FA_HIRE); w.I32(target); }
void OrderTribute(Writer& w) { w.U8(FA_TRIBUTE); }
void OrderPact(Writer& w, int to) { w.U8(FA_PACT); w.I32(to); }
void OrderLoan(Writer& w, int to, int flock, int fish) { w.U8(FA_LOAN); w.I32(to); w.I32(flock); w.I32(fish); }
void OrderBounty(Writer& w, int target, int fish) { w.U8(FA_BOUNTY); w.I32(target); w.I32(fish); }
void OrderBreak(Writer& w, int with) { w.U8(FA_BREAK); w.I32(with); }
void OrderTech(Writer& w, int tech) { w.U8(FA_TECH); w.I32(tech); }
void OrderNestStyle(Writer& w, int style) { w.U8(FA_NEST_STYLE); w.I32(style); }
void OrderBeacon(Writer& w) { w.U8(FA_BEACON); }
void OrderHeir(Writer& w, int id) { w.U8(FA_HEIR); w.I32(id); }
void OrderSuccession(Writer& w, int choice) { w.U8(FA_SUCC); w.I32(choice); }
void OrderKeepPerk(Writer& w, int perk) { w.U8(FA_KEEP_PERK); w.I32(perk); }
void OrderDynasty(Writer& w, int pick) { w.U8(FA_DYNASTY); w.I32(pick); }
bool FormationUnlocked(const Colony& c, Formation f) { return f == Formation::Chevron || f == Formation::Scatter || c.HasTier(Tree::War, 1); }

std::string TargetText(World& w, const Flock& f) {
    switch (f.target) {
    case Target::Home: return "guarding home";
    case Target::Cache: return "raiding " + w.SideName(f.tSide) + "'s caches";
    case Target::Nests: return "taking " + w.SideName(f.tSide) + "'s chicks";
    case Target::Ground: return f.tZone >= 0 && f.tZone < (int)w.eco.map->zones.size() ? "harassing " + w.eco.map->zones[f.tZone].name : "over a ground";
    case Target::Flock: { Flock* t = w.FindFlock(f.tSide, f.tFlock); return t ? "intercepting " + w.SideName(f.tSide) + "'s " + t->name : "intercepting"; }
    default: return "flying to a mark";
    }
}

namespace {
// an order on the side swapped into the World's fields (its colony is w.col, its Founder w.me)
bool OrderIn(World& w, int side, int kind, Reader& r) {
    Colony& C = w.col;
    int nZones = (int)w.eco.map->zones.size(), nSides = (int)w.sides.size() + 1, nIsles = (int)w.isles.size();
    switch (kind) {
    case FA_HELLO: {
        std::string name = r.Str(), key = r.Str(), look = r.Str();
        if (r.bad) return false;
        w.LookOf(side) = look.substr(0, 80);   // (a costume and a livery: how everyone else sees this colony)
        name = name.substr(0, 24);
        if (!name.empty()) { if (side == 0) w.name0 = name; else w.sides[side - 1].name = name; }
        int fi = FounderIndex(key);
        if (fi >= 0 && w.time < 30 && fi != w.me.def) {   // (a founder is picked before the match is under way)
            w.me.def = fi; w.me.stamina = w.Def().stamina; w.me.hp = w.Def().hp;
            w.me.strikeLen = w.Def().key == "taloned" ? 1.0f : 0.5f;
        }
        return true;
    }
    case FA_PLAN: {
        int ro = (int)r.U8(); float v = r.F32();
        if (r.bad || ro <= 0 || ro >= (int)Role::COUNT || !std::isfinite(v)) return false;
        C.plan[ro] = std::clamp(v, 0.0f, 5.0f);
        return true;
    }
    case FA_NESTS: { int n = r.I32(); if (r.bad) return false; C.nestsWanted = std::clamp(n, 1, std::max(1, (int)C.sites.size())); return true; }
    case FA_GROUND: {
        int z = r.I32();
        if (r.bad || z < -1 || z >= nZones) return false;
        C.ground = z;
        if (z >= 0) w.Say("The fishers will work " + w.eco.map->zones[z].name + ".");
        return true;
    }
    case FA_REST: { float v = r.F32(); if (r.bad || !std::isfinite(v)) return false; C.restBelow = std::clamp(v, 0.0f, 1.0f); return true; }
    case FA_RETRAIN: {
        int to = (int)r.U8(), from = (int)r.U8();
        if (r.bad || to <= 0 || to >= (int)Role::COUNT || from >= (int)Role::COUNT) return false;
        return w.Retrain((Role)to, (Role)from);
    }
    case FA_SCOUT: {
        int isle = r.I32(), zone = r.I32(); float x = r.F32(), z = r.F32(); int alt = (int)r.U8();
        if (r.bad || isle < -1 || isle >= nIsles || zone < -1 || zone >= nZones || alt > 2 || !std::isfinite(x) || !std::isfinite(z)) return false;
        if (w.SendScout(isle, isle < 0 ? zone : -1, {x, 0, z}, (Alt)alt)) return true;
        w.Say(w.Count(BStage::Adult, Role::Scout) > 0 ? "Every scout is out." : "No scouts in the colony.");
        return false;
    }
    case FA_FLOCK_MAKE: {
        uint32_t n = r.VarU();
        if (r.bad || n > 64) return false;
        std::vector<int> ids; for (uint32_t k = 0; k < n; k++) ids.push_back(r.I32());
        int fo = (int)r.U8(), al = (int)r.U8(), st = (int)r.U8();
        if (r.bad || fo >= (int)Formation::COUNT || al > 2 || st >= (int)Stance::COUNT) return false;
        if (!FormationUnlocked(C, (Formation)fo)) fo = (int)Formation::Chevron;   // (formations want War 1)
        int f = w.MakeFlock(side, ids, (Formation)fo, (Alt)al, (Stance)st);
        if (f >= 0) w.Say(TextFormat("Flock %d formed: pick a target on the chart (M).", f));
        return f >= 0;
    }
    case FA_FLOCK_SET: {
        int id = r.I32(); int fo = (int)r.U8(), al = (int)r.U8(), st = (int)r.U8();
        if (r.bad || fo >= (int)Formation::COUNT || al > 2 || st >= (int)Stance::COUNT) return false;
        Flock* f = w.FindFlock(side, id);
        if (!f) return false;
        if (!FormationUnlocked(C, (Formation)fo)) { w.Say("Formations want War 1 at the Roost (the chevron and the scatter need none)."); return false; }
        f->form = (Formation)fo; f->alt = (Alt)al; f->stance = (Stance)st;
        return true;
    }
    case FA_FLOCK_ORDER: {
        int id = r.I32(); int t = (int)r.U8(); int ts = r.I32(), ti = r.I32(), tz = r.I32(), tf = r.I32(); float x = r.F32(), z = r.F32();
        if (r.bad || t >= (int)Target::COUNT || ts < -1 || ts >= nSides || ti < -1 || ti >= nIsles || tz < -1 || tz >= nZones || !std::isfinite(x) || !std::isfinite(z)) return false;
        if ((t == (int)Target::Cache || t == (int)Target::Nests || t == (int)Target::Flock) && (ts < 0 || ts == side)) return false;
        if (ts >= 0 && w.Truce(side, ts)) { w.Say("A truce holds with " + w.SideName(ts) + "."); return false; }
        Flock* f = w.FindFlock(side, id);
        if (!f) return false;
        w.OrderFlock(side, id, (Target)t, ts, ti, tz, tf, {x, 0, z});
        if ((f = w.FindFlock(side, id))) { if (f->leader == -2) f->leader = -1; w.Say(f->name + ": " + TargetText(w, *f) + "."); }
        return true;
    }
    case FA_FLOCK_HOME: { int id = r.I32(); if (r.bad || !w.FindFlock(side, id)) return false; w.OrderFlock(side, id, Target::Home, -1, -1, -1, -1, {}); return true; }
    case FA_FLOCK_DISBAND: {
        int id = r.I32();
        Flock* f = r.bad ? nullptr : w.FindFlock(side, id);
        if (!f) return false;
        f->retreating = true; f->target = Target::Home; f->engagedT = 0;
        return true;
    }
    case FA_LEAD: {
        Founder& F = w.me;
        if (F.st == FState::Dead || F.chick) { w.Say("A chick-leader can't lead a flock."); return false; }
        Flock* best = nullptr; float bd = 70;
        for (auto& fk : C.flocks) { float d = Vector3Distance(fk.pos, F.pos); if (d < bd) { bd = d; best = &fk; } }
        if (best && best->leader == -2) { best->leader = -1; w.Say("You leave " + best->name + "."); }
        else if (best) { for (auto& fk : C.flocks) if (fk.leader == -2) fk.leader = -1; best->leader = -2; best->target = Target::Home; w.Say("You lead " + best->name + ": it follows you (+20 morale, +10% speed)."); }
        else { w.Say("No flock of yours within 70 m to lead."); return false; }
        return true;
    }
    case FA_BUILD: {
        int kind = (int)r.U8();
        if (r.bad || kind >= ST_COUNT || C.caches.empty()) return false;
        for (const auto& s : C.builds) if (s.kind == kind && (kind != ST_MONUMENT || !s.built)) return false;
        if (!w.BuildUnlocked(kind)) { w.Say(std::string("The ") + StructureName(kind) + (kind == ST_MONUMENT ? " waits for the autumn." : " isn't researched yet.")); return false; }
        if (!w.LayStructure(kind)) return false;
        w.Say(std::string("Your builders will raise the ") + StructureName(kind) + TextFormat(" (%d twigs, %d shells).", StructureTwigs(kind), StructureShells(kind)));
        return true;
    }
    case FA_RESEARCH: {
        int t = (int)r.U8();
        if (r.bad || t >= (int)Tree::COUNT) return false;
        std::string why;
        if (!w.CanResearch((Tree)t, &why)) { w.Say(std::string(TreeName((Tree)t)) + ": " + why + "."); return false; }
        return w.StartResearch((Tree)t);
    }
    case FA_TRADEFOR: { int g = (int)r.U8(); if (r.bad || g <= G_FISH || g >= G_COUNT) return false; C.tradeFor = g; w.Say(std::string("The Traders will buy ") + GoodName(g) + "."); return true; }
    case FA_BOOM: return w.StartBoom();
    case FA_CORNER: { int t = r.I32(); if (r.bad) return false; return w.Corner(t); }
    case FA_PELICAN: { int t = r.I32(), i = r.I32(); if (r.bad) return false; return w.Pelican(t, i); }
    case FA_BARTER: {
        int to = r.I32(), give[G_COUNT], get[G_COUNT];
        for (int g = 0; g < G_COUNT; g++) give[g] = r.I32();
        for (int g = 0; g < G_COUNT; g++) get[g] = r.I32();
        float truce = r.F32();
        if (r.bad || !std::isfinite(truce)) return false;
        return w.MakeOffer(side, to, give, get, truce) >= 0;
    }
    case FA_ANSWER: { int id = r.I32(); bool yes = r.U8() != 0; if (r.bad) return false; return w.AnswerOffer(side, id, yes); }
    case FA_FOUND: {
        int isle = r.I32();
        if (r.bad || isle < 0 || isle >= nIsles || isle == w.home) return false;
        if (!w.RoleUnlocked(Role::Pathfinder) || w.Count(BStage::Adult, Role::Pathfinder) == 0) { w.Say("An outpost without the Founder wants a Pathfinder (Trade 4)."); return false; }
        C.expandTo = isle; w.Say("A Pathfinder sets out to found an outpost on " + w.isles[isle].name + ".");
        return true;
    }
    case FA_DOSE: { int id = r.I32(); int s = (int)r.U8(); if (r.bad) return false; return w.Dose(id, s); }
    case FA_DECREE: { int k = (int)r.U8(); if (r.bad) return false; return w.PickDecree(k); }
    case FA_PERK: { int k = (int)r.U8(); if (r.bad) return false; return w.PickPerk(k); }
    case FA_HIRE: { int t = r.I32(); if (r.bad) return false; return w.HirePirates(t); }
    case FA_TRIBUTE: return w.PayTribute();
    case FA_PACT: { int t = r.I32(); if (r.bad) return false; return w.OfferPact(t) >= 0; }
    case FA_LOAN: { int t = r.I32(), f = r.I32(), n = r.I32(); if (r.bad) return false; return w.OfferLoan(t, f, n) >= 0; }
    case FA_BOUNTY: { int t = r.I32(), n = r.I32(); if (r.bad || n < 1 || n > 100) return false; return w.PostBounty(t, n); }
    case FA_BEACON: return w.LightBeacon();
    case FA_HEIR: { int id = r.I32(); if (r.bad || !w.LongFlight()) return false; return id < 0 ? w.MarkNextHeir() : w.MarkHeir(id); }
    case FA_SUCC: { int c = r.I32(); if (r.bad || c < 0 || c > 2 || !w.LongFlight()) return false; w.col.succChoice = c; return true; }
    case FA_KEEP_PERK: { int p = r.I32(); if (r.bad || p < -1 || p >= 32 || !w.LongFlight()) return false; w.col.keepPerk = p; return true; }
    case FA_DYNASTY: { int p = r.I32(); if (r.bad || p < 0 || p >= (int)DynastyNames().size() || !w.LongFlight() || w.col.gen > 0) return false; w.col.dynastyPick = p; return true; }
    case FA_NEST_STYLE: { int t = r.I32(); if (r.bad || t < 0 || t >= NS_COUNT || w.seasons <= 0) return false; w.col.nestStyle = t; return true; }
    case FA_TECH: { int t = r.I32(); if (r.bad || t < -1 || t >= TK_COUNT || w.seasons <= 0) return false; w.col.tech = t; return true; }
    case FA_BREAK: { int t = r.I32(); if (r.bad || t < 0 || t > (int)w.sides.size()) return false; return w.BreakTruce(t); }
    case FA_WANT_TRAIT: { int k = r.I32(); if (r.bad || k < -1 || k >= MT_COUNT) return false; C.wantTrait = k; for (auto& n : C.nests) n.favFish = 0; if (k >= 0) w.Say("The courtship bowls ask for a " + MateTraits()[k].name + " mate: fill them with " + MateTraits()[k].favorite + "."); return true; }
    case FA_BREW: { int s = (int)r.U8(); if (r.bad || s <= STIM_NONE || s >= STIM_COUNT) return false; C.brewFor = s; w.Say(std::string("The Chemists will brew ") + StimName(s) + "."); return true; }
    default: return false;
    }
}
}  // namespace

bool ApplyOrder(World& w, int side, Reader& r) {
    int kind = (int)r.U8();
    if (r.bad || kind == FA_INPUT || kind >= FA_AUTOPILOT || side < 0 || side > (int)w.sides.size() || w.over) return false;
    bool ok = false;
    w.WithSide(side, [&] { ok = OrderIn(w, side, kind, r); });
    return ok;
}
bool ApplyOrder(World& w, int side, const Writer& order) { Reader r(order.b); return ApplyOrder(w, side, r); }

// ---------------------------------------------------------------- the score and the end (doc p27; flight_scoring.json)
namespace {
struct Scoring {
    float bird = 2, chick = 1, nest = 5, island = 30, danger = 80, kraken = 150, cachePer = 5, kill = 1, founder = 60, pearl = 3, tier = 10, tier4 = 40, fervour = 50, theft = 5;
    float holdShare = 2.0f / 3, holdDay = 3, holdPer = 4;
    std::vector<int> lengths{20, 30, 45};
};
const Scoring& SC() {
    static Scoring s; static bool loaded = false;
    if (loaded) return s;
    loaded = true;
    std::string err; Json j;
    if (!LoadJsonFile(FlightDataDir() + "/flight_scoring.json", j, &err)) return s;
    auto F = [&](const char* k, float& v) { if (j[k].IsNum()) v = j[k].F(v); };
    F("living_bird", s.bird); F("dangerous_island_held", s.danger); F("kraken_killed", s.kraken); F("chick", s.chick); F("nest", s.nest); F("island_held", s.island); F("cache_fish_per_point", s.cachePer);
    F("enemy_bird_killed", s.kill); F("founder_never_died", s.founder); F("pearl", s.pearl); F("research_tier", s.tier); F("research_tier4", s.tier4); F("fervour_full", s.fervour); F("egg_stolen", s.theft);
    F("hold_share", s.holdShare); F("hold_from_day", s.holdDay); F("hold_min_nests_per_player", s.holdPer);
    if (j["match_minutes"].IsArr()) { s.lengths.clear(); for (const Json& m : j["match_minutes"].a) s.lengths.push_back(m.I(30)); }
    return s;
}
}  // namespace
const std::vector<int>& MatchLengths() { return SC().lengths; }

ScoreCard World::Score(int side) const {
    if (mirror) return side >= 0 && side < (int)scores.size() ? scores[side] : ScoreCard{};
    const Scoring& K = SC();
    ScoreCard c;
    if (side < 0 || side > (int)sides.size()) return c;
    const Colony& C = ColOf(side);
    const Founder& F = FounderOf(side);
    int adults = F.st != FState::Dead ? 1 : 0, chicks = 0, nests = 0, fish = 0;
    for (const auto& b : C.birds) if (b.alive) { if (b.stage == BStage::Adult || b.stage == BStage::Mate) adults++; else if (b.stage == BStage::Chick) chicks++; }
    for (const auto& n : C.nests) nests += n.built;
    for (const auto& ca : C.caches) fish += (int)ca.fish.size();
    c.birds = (int)lroundf(adults * K.bird + chicks * K.chick);
    c.nests = (int)lroundf(nests * K.nest);
    for (int i = 0; i < (int)isles.size(); i++) if (HolderOf(i) == side) c.isles += IsDangerous(isles[i].type) ? (int)K.danger : (int)K.island;   // (islands held: the most nests on them)
    if (seasons >= 4 && Season() == SEASON_WINTER) { c.isles = (int)lroundf(c.isles * WinterHoldings()); c.nests = (int)lroundf(c.nests * WinterHoldings()); }   // (Winter's holdings count double)
    c.cache = (int)(fish / std::max(1.0f, K.cachePer));
    c.kills = (int)lroundf(C.kills * K.kill);
    c.founder = F.deaths == 0 ? (int)K.founder : 0;
    for (int t = 0; t < (int)Tree::COUNT; t++) for (int n = 1; n <= C.tier[t]; n++) c.research += (int)(n == 4 ? K.tier4 : K.tier);
    c.pearls = (int)(C.pearls * K.pearl);
    c.faith = C.fervour >= 100 ? (int)K.fervour : 0;
    c.thefts = (int)((C.eggsStolen + C.nestsDestroyed) * K.theft);
    c.kraken = C.krakenKill ? (int)K.kraken : 0;
    c.legacy = LegacyScore(side);
    c.total = c.birds + c.nests + c.isles + c.cache + c.kills + c.founder + c.research + c.pearls + c.faith + c.thefts + c.kraken + c.legacy;
    return c;
}
void World::CheckEnd() {
    if (over || mirror || !wholeMap) return;
    const Scoring& K = SC();
    int N = (int)sides.size() + 1;
    scoreT -= 1;   // (called once a step; the scores are kept fresh about once a second)
    if (scoreT <= 0 || (int)scores.size() != N) { scoreT = 30; scores.resize(N); for (int s = 0; s < N; s++) scores[s] = Score(s); }
    std::string why;
    int holder = -1;
    if (matchLen > 0 && time >= matchLen) why = TextFormat("Time: the %.0f minutes are up.", matchLen / 60);
    else if (time >= K.holdDay * DAY) {
        // one colony holds two-thirds of the nests in use on the map
        int total = 0, best = 0; std::vector<int> per(N, 0);
        for (int s = 0; s < N; s++) { for (const auto& n : ColOf(s).nests) per[s] += n.built; total += per[s]; if (per[s] > per[best]) best = s; }
        if (total >= K.holdPer * N && per[best] >= K.holdShare * total) { holder = best; why = TextFormat("%s holds %d of the %d nests on the map.", SideName(best).c_str(), per[best], total); }
    }
    if (why.empty()) return;
    for (int s = 0; s < N; s++) scores[s] = Score(s);
    winner = holder;
    if (winner < 0) { winner = 0; for (int s = 1; s < N; s++) if (scores[s].total > scores[winner].total) winner = s; }
    over = true; overReason = why;
    for (int s = 0; s < N; s++) SayTo(s, why + " " + (s == winner ? std::string("Your colony wins") : SideName(winner) + " wins") + TextFormat(" (%d).", scores[winner].total));
}

// ---------------------------------------------------------------- the snapshot
namespace {
struct Out {
    Writer& w;
    static constexpr bool reading = false;
    void f(float& v) { w.F32(v); }
    void i(int& v) { w.VarU(((uint32_t)v << 1) ^ (uint32_t)(v >> 31)); }
    void b(bool& v) { w.U8(v ? 1 : 0); }
    void u(uint32_t& v) { w.U32(v); }
    void s(std::string& v) { w.Str(v); }
    void v3(Vector3& v) { f(v.x); f(v.y); f(v.z); }
    template <class E> void e(E& v) { int k = (int)v; i(k); }
    template <class T, class F> void vec(std::vector<T>& v, F fn) { w.VarU((uint32_t)v.size()); for (auto& x : v) fn(x); }
    void q16(float& v, float lo, float hi) { w.U16((uint32_t)std::clamp((int)lroundf((v - lo) / (hi - lo) * 65535), 0, 65535)); }
    void q8(float& v, float max) { w.U8((uint32_t)std::clamp((int)lroundf(v / max * 255), 0, 255)); }
    void s8(float& v, float max) { w.U8((uint32_t)(uint8_t)(int8_t)std::clamp((int)lroundf(v / max * 127), -127, 127)); }
    void ang(float& v) { float a = v - 2 * PI * floorf(v / (2 * PI)); w.U8((uint32_t)lroundf(a / (2 * PI) * 256) & 0xFF); }
    bool bad() const { return false; }
};
struct In {
    Reader& r;
    static constexpr bool reading = true;
    void f(float& v) { v = r.F32(); if (!std::isfinite(v)) v = 0; }
    void i(int& v) { uint32_t z = r.VarU(); v = (int)((z >> 1) ^ (0u - (z & 1))); }
    void b(bool& v) { v = r.U8() != 0; }
    void u(uint32_t& v) { v = r.U32(); }
    void s(std::string& v) { v = r.Str(); }
    void v3(Vector3& v) { f(v.x); f(v.y); f(v.z); }
    template <class E> void e(E& v) { int k; i(k); v = (E)k; }
    template <class T, class F> void vec(std::vector<T>& v, F fn) {
        uint32_t n = r.VarU();
        if (n > 60000 || r.bad) { r.bad = true; n = 0; }
        v.resize(n);
        for (auto& x : v) { fn(x); if (r.bad) break; }
    }
    void q16(float& v, float lo, float hi) { v = lo + r.U16() / 65535.0f * (hi - lo); }
    void q8(float& v, float max) { v = r.U8() / 255.0f * max; }
    void s8(float& v, float max) { v = (int8_t)(uint8_t)r.U8() / 127.0f * max; }
    void ang(float& v) { v = r.U8() / 256.0f * 2 * PI; }
    bool bad() const { return r.bad; }
};
// positions: the world's box (the Founder is kept within 1400 m of the middle)
constexpr float BX = 1500, BY0 = -80, BY1 = 280;
template <class A> void P16(A& a, Vector3& p) { a.q16(p.x, -BX, BX); a.q16(p.y, BY0, BY1); a.q16(p.z, -BX, BX); }
// (what a position reads back as: the filters below judge the quantised one, so a mirror writing again keeps the same)
float Qv(float v, float lo, float hi) { return lo + std::clamp((int)lroundf((v - lo) / (hi - lo) * 65535), 0, 65535) / 65535.0f * (hi - lo); }
Vector2 Qxz(Vector3 p) { return {Qv(p.x, -BX, BX), Qv(p.z, -BX, BX)}; }

bool& HumanRef(World& w, int s) { return s == w.cur ? w.human : s == 0 ? w.sides[w.cur - 1].human : w.sides[s - 1].human; }
bool& BotRef(World& w, int s) { return s == w.cur ? w.founderBot : s == 0 ? w.sides[w.cur - 1].founderBot : w.sides[s - 1].founderBot; }

template <class A> void VisitFounder(A& a, Founder& f) {
    a.f(f.ageSpeed); a.f(f.ageAttack); a.b(f.old); a.i(f.oldCarry);   // (the Long Flight: its age)
    a.i(f.def); a.e(f.st); a.v3(f.pos); a.v3(f.vel); a.f(f.yaw); a.f(f.pitch); a.f(f.bank); a.f(f.airspeed);
    a.f(f.stamina); a.f(f.hunger); a.f(f.hp);
    int fl = (f.flapping ? 1 : 0) | (f.sprinting ? 2 : 0) | (f.gliding ? 4 : 0) | (f.exhausted ? 8 : 0) | (f.chick ? 16 : 0);
    a.i(fl);
    if constexpr (A::reading) { f.flapping = fl & 1; f.sprinting = fl & 2; f.gliding = fl & 4; f.exhausted = fl & 8; f.chick = fl & 16; }
    a.i(f.carrySp); a.i(f.carrySize); a.i(f.carryTwigs); a.f(f.airT);
    { int pk = (int)f.perks; a.i(pk); f.perks = (uint32_t)pk; for (int& o : f.perkOffer) a.i(o); a.i(f.perkLevel); a.b(f.nineUsed); }   // (the long match's perks)
    a.f(f.strikeT); a.f(f.strikeLen); a.v3(f.strikeAt); a.v3(f.strikeAim); a.f(f.strikeSpeed);
    a.f(f.struggleT); a.i(f.struggleSp); a.f(f.underT); a.f(f.faintT); a.f(f.respawnT);
    a.i(f.chickFish); a.f(f.adultT); a.i(f.deaths); a.i(f.agent); a.s(f.lastCause);
}
template <class A> void VisitBird(A& a, Bird& b, bool own) {
    a.b(b.elder); a.i(b.kin);   // (the Long Flight)
    a.i(b.id); a.e(b.stage); a.e(b.role); a.e(b.retrainTo); a.i(b.nest);
    P16(a, b.pos); a.s8(b.vel.x, 60); a.s8(b.vel.y, 60); a.s8(b.vel.z, 60);
    a.ang(b.yaw); a.ang(b.flapPh);
    a.q8(b.hunger, 1); a.f(b.age); a.f(b.chillT); a.f(b.retrainT);
    a.e(b.task); a.i(b.carrySp); a.i(b.carrySize); a.i(b.carryTwigs); a.i(b.carryShells);
    a.q8(b.hp, 255); a.q8(b.fight, 25.5f); a.q8(b.netT, 25.5f); a.i(b.flock); a.i(b.tgtSide); a.i(b.tgtId);
    a.i(b.carryGood); a.i(b.carryN);
    a.i(b.vet); a.i(b.vetName); a.i(b.trait);   // (the long match: veterans and mates' traits)
    if (own) { a.i(b.scoutIsle); a.i(b.scoutZone); a.e(b.alt); a.b(b.hasOrder); a.b(b.observed); a.i(b.caught); }
}
template <class A> void VisitColony(A& a, Colony& c, bool own, bool full, const std::function<bool(const Bird&)>& keep) {
    // the living birds (another colony's only where the viewer can see them)
    std::vector<Bird> sent;
    if constexpr (!A::reading) { sent.reserve(c.birds.size()); for (const auto& b : c.birds) if (b.alive && (own || keep(b))) sent.push_back(b); }
    a.vec(A::reading ? c.birds : sent, [&](Bird& b) { VisitBird(a, b, own); if constexpr (A::reading) b.alive = true; });
    a.vec(c.nests, [&](Nest& n) { a.i(n.site); a.v3(n.pos); a.f(n.twigs); a.b(n.built); a.b(n.founders); a.i(n.mate); a.i(n.bowl); a.i(n.bowlNeed); a.f(n.mateT); a.f(n.larder); a.i(n.shells); a.i(n.isle); a.f(n.tear); });
    a.vec(c.caches, [&](Cache& k) { a.v3(k.pos); a.b(k.built); a.f(k.twigs); a.i(k.isle); a.vec(k.fish, [&](CachedFish& f) { a.i(f.sp); a.i(f.size); a.f(f.age); }); });
    a.vec(c.sites, [&](Site& s) { a.v3(s.pos); a.i(s.nest); a.i(s.isle); });   // (an outpost adds another island's sites)
    a.vec(c.twigSrc, [&](TwigSource& t) { a.v3(t.pos); a.f(t.twigs); a.f(t.cap); a.b(t.shells); });
    a.i(c.twigs); a.i(c.shells); a.i(c.wildMates); a.i(c.nextId); a.i(c.nextFlock);
    for (int r = 0; r < (int)Role::COUNT; r++) a.f(c.plan[r]);
    a.i(c.ground); a.f(c.restBelow); a.i(c.nestsWanted); a.f(c.feedToday); a.f(c.feedYesterday); a.i(c.caughtToday); a.i(c.deathsToday);
    a.b(c.leaderless); a.i(c.stolen); a.i(c.lostToRaids); a.i(c.kills); a.i(c.losses);
    if (own && full) {
        a.vec(c.deaths, [&](std::pair<std::string, int>& d) { a.s(d.first); a.i(d.second); });
        a.vec(c.days, [&](DayStats& d) { a.i(d.day); a.i(d.birds); a.i(d.eggs); a.i(d.chicks); a.i(d.mates); a.i(d.nests); a.i(d.caught); a.i(d.deaths); a.f(d.feedCaught); a.f(d.mouths); a.f(d.cacheFeed); a.f(d.lagoon); });
    }
    a.vec(c.flocks, [&](Flock& f) {
        a.i(f.id); a.vec(f.members, [&](int& m) { a.i(m); }); a.i(f.leader);
        a.e(f.form); a.e(f.alt); a.e(f.stance); a.e(f.target); a.i(f.tSide); a.i(f.tIsle); a.i(f.tZone); a.i(f.tFlock); a.v3(f.tAt);
    a.i(f.loanTo); a.f(f.loanUntil);
        P16(a, f.pos); a.s8(f.vel.x, 60); a.s8(f.vel.y, 60); a.s8(f.vel.z, 60);
        a.f(f.morale); a.f(f.wins); a.f(f.engagedT); a.i(f.startSize); a.i(f.lost);
        a.b(f.retreating); a.b(f.scattered); a.b(f.leaderDead); a.s(f.name);
        a.i(f.stim); a.f(f.stimT); a.f(f.crashT);
    });
    a.vec(c.builds, [&](Structure& s) { a.i(s.kind); a.v3(s.pos); a.f(s.twigs); a.i(s.shells); a.b(s.built); a.i(s.site); a.f(s.hp); a.i(s.isle); });
    // stage 6: the stores, research, fervour, the buttons
    a.i(c.pearls); a.f(c.guano); a.f(c.sulfur);
    for (int t = 0; t < (int)Tree::COUNT; t++) { int v = c.tier[t]; a.i(v); c.tier[t] = (uint8_t)std::clamp(v, 0, 4); }
    a.i(c.resTree); a.f(c.resLeft); a.f(c.resDays); a.f(c.fervour); a.f(c.prayerT); a.i(c.prayedDay); a.i(c.tradeFor);
    a.i(c.boomState); a.f(c.boomT); a.f(c.boomCd); a.b(c.cornered); a.i(c.serenadeDay); a.i(c.eggsStolen); a.i(c.nestsDestroyed); a.i(c.converted);
    if (own) a.vec(c.spies, [&](int& s) { a.i(s); });
    a.i(c.bombs); a.i(c.blockbusters); for (int k = 0; k < STIM_COUNT; k++) a.i(c.stims[k]); a.i(c.brewFor); a.b(c.bell); a.f(c.offeredKraken); a.i(c.krakenKill); a.i(c.expandTo);
    a.i(c.wantTrait);
    { int rl = (int)c.relics; a.i(rl); c.relics = (uint32_t)rl; a.i(c.legend); a.b(c.legendAlive); }
    a.i(c.pact); a.i(c.bounty); a.i(c.bountyBy);
    a.f(c.beaconT); a.b(c.rookeryWarm);
    a.i(c.gen); a.i(c.heirId); a.i(c.heirTrait); a.i(c.succChoice); a.i(c.keepPerk); a.i(c.dynastyPick); a.b(c.regent); a.f(c.genStart); a.f(c.successionT); { int rk = (int)c.relicsKept; a.i(rk); c.relicsKept = (uint32_t)rk; } a.s(c.dynasty);
    if (own) a.vec(c.chronicle, [&](ChronLine& l) { a.i(l.day); a.i(l.season); a.i(l.year); a.i(l.kind); a.s(l.text); });   // (the Long Flight: a colony's own Chronicle)
    a.i(c.nestStyle); for (auto& n : c.nests) { a.i(n.style); a.f(n.rainT); }
    a.i(c.tech); for (float& m : c.techMastery) a.f(m); a.vec(c.techLog, [&](int& n) { a.i(n); });
    a.i(c.decree); a.i(c.yesterday); for (int& o : c.offer) a.i(o); a.i(c.dealtDay); a.i(c.lastRaider); { int u = (int)c.decreesUsed; a.i(u); c.decreesUsed = (uint32_t)u; }   // (the long match's decrees)
}
// each zone's stock as a share of what it holds (one pass over the fish)
std::vector<float> ZoneStocks(const World& w) {
    if (w.mirror) return w.mirrorStock;
    size_t nz = w.eco.map->zones.size();
    std::vector<float> n(nz, 0), k(nz, 0);
    std::vector<std::vector<int>> rows(w.eco.map->species.size());
    for (const auto& s : w.stocks) {
        if (s.zone < 0 || s.zone >= (int)nz) continue;
        const rt::Species& sp = w.eco.map->species[s.sp];
        if (sp.isDiver || sp.isEnemy || sp.tier >= 4 || sp.size > 4 || sp.Has("protected")) continue;
        k[s.zone] += s.K;
        if (s.zone < (int)w.liveZone.size() && !w.liveZone[s.zone]) n[s.zone] += s.pop;
        else rows[s.sp].push_back(s.zone);
    }
    for (const auto& g : w.eco.agents) {
        if (!g.alive || g.diver >= 0 || g.sp < 0 || g.sp >= (int)rows.size() || g.homeZone < 0 || g.homeZone >= (int)nz) continue;
        for (int z : rows[g.sp]) if (z == g.homeZone) { n[z] += 1; break; }
    }
    std::vector<float> out(nz, 0);
    for (size_t z = 0; z < nz; z++) out[z] = k[z] > 0 ? std::min(1.5f, n[z] / k[z]) : 0;
    return out;
}
// the sea round the viewer's bird: the fish within 160 m (everything else isn't drawn, and waits)
template <class A> void VisitSea(A& a, World& w, Vector3 eye) {
    int n = (int)w.eco.agents.size(); a.i(n);
    if constexpr (A::reading) {
        if (n < 0 || n > 60000) { a.r.bad = true; return; }
        w.eco.agents.resize(n);
        for (auto& g : w.eco.agents) g.alive = false;
    }
    std::vector<int> idx;
    if constexpr (!A::reading) {
        Vector2 e{eye.x, eye.z};
        for (int k = 0; k < n; k++) { const rt::Agent& g = w.eco.agents[k]; if (g.alive && g.diver < 0 && Vector2Distance(Qxz(g.pos), e) < 160) idx.push_back(k); }
    }
    int m = (int)idx.size(); a.i(m);
    if constexpr (A::reading) { if (m < 0 || m > n) { a.r.bad = true; return; } idx.resize(m); }
    int prev = -1;
    for (int j = 0; j < m && !a.bad(); j++) {
        int gap = idx[j] - prev; a.i(gap);
        if constexpr (A::reading) { idx[j] = prev + gap; if (idx[j] <= prev || idx[j] >= n) { a.r.bad = true; return; } }
        prev = idx[j];
        rt::Agent& g = w.eco.agents[idx[j]];
        a.i(g.sp); P16(a, g.pos); a.s8(g.vel.x, 12.7f); a.s8(g.vel.y, 12.7f); a.s8(g.vel.z, 12.7f);
        a.q8(g.wound, 1); a.q8(g.held, 25.5f);
        if constexpr (A::reading) {
            if (g.sp < 0 || g.sp >= (int)w.eco.map->species.size()) { a.r.bad = true; return; }
            g.alive = true; g.diver = -1; g.rng = (uint32_t)(idx[j] + 1) * 2654435761u;
        }
    }
}
template <class A> void VisitKnowledge(A& a, Knowledge& k) {
    a.vec(k.isle, [&](uint8_t& v) { int x = v; a.i(x); v = (uint8_t)std::clamp(x, 0, 2); });
    a.vec(k.sight, [&](Sighting& s) { a.f(s.t); if (s.t < 0) return; a.i(s.nests); a.i(s.caches); a.i(s.birds); a.i(s.alt); a.b(s.exact); a.i(s.scouts); });
    a.vec(k.ground, [&](GroundInfo& g) { a.f(g.t); if (g.t < 0) return; a.f(g.stock); a.f(g.predators); a.i(g.fishers); });
    std::vector<Report> tail;
    if constexpr (!A::reading) { size_t from = k.log.size() > 40 ? k.log.size() - 40 : 0; tail.assign(k.log.begin() + from, k.log.end()); }
    a.vec(A::reading ? k.log : tail, [&](Report& r) { a.f(r.t); a.i(r.kind); a.s(r.text); a.v3(r.at); });
}

template <class A> void Visit(A& a, World& w, bool full) {
    a.f(w.time);
    a.f(w.wind.dir.x); a.f(w.wind.dir.y); a.f(w.wind.speed); a.f(w.wind.nextDir.x); a.f(w.wind.nextDir.y); a.f(w.wind.nextSpeed); a.f(w.wind.shiftT); a.f(w.wind.shiftLen);
    a.b(w.over); a.i(w.winner); a.s(w.overReason); a.f(w.matchLen);
    a.s(w.name0);
    a.f(w.dayAcc); a.i(w.dayNum);
    a.vec(w.relicSpots, [&](World::RelicSpot& r) { a.v3(r.pos); a.i(r.relic); a.i(r.isle); a.b(r.taken); });
    a.v3(w.pirates.pos); a.f(w.pirates.hp); a.f(w.pirates.scatterUntil); a.f(w.pirates.hireUntil); a.i(w.pirates.target); a.b(w.pirates.on);
    a.vec(w.fleet, [&](World::Boat& b) { a.v3(b.pos); });
    a.i(w.grey.isle); a.v3(w.grey.crag); a.v3(w.grey.hunter); a.f(w.grey.hp); a.f(w.grey.hunterT); a.b(w.grey.dead); a.vec(w.grey.peaceUntil, [&](float& p) { a.f(p); });
    a.i(w.greatEvent); a.f(w.greatDay); a.f(w.greatUntil); a.i(w.treasure); a.i(w.legendFree); a.v3(w.greatPos); a.v3(w.legendPos); a.v3(w.walkFrom); a.v3(w.walkTo);
    { a.i(w.seasons); a.i(w.seasonEvent); a.f(w.eventUntil); int ed = (int)w.eventsDone; a.i(ed); w.eventsDone = (uint32_t)ed; for (float& d : w.eventDay) a.f(d); }   // (the long match's seasons and events)
    int N = (int)w.sides.size(); a.i(N);
    if (N != (int)w.sides.size()) { if constexpr (A::reading) a.r.bad = true; return; }
    // every side, in absolute order: its Founder, who flies it, its colony (yours in full), its name and livery
    Vector2 eye{w.me.pos.x, w.me.pos.z};
    auto near = [&](const Bird& b) { return Vector2Distance(Qxz(b.pos), eye) < 330; };
    std::function<bool(const Bird&)> keep = near;
    for (int s = 0; s <= N && !a.bad(); s++) {
        VisitFounder(a, w.FounderOf(s));
        a.b(HumanRef(w, s)); a.b(BotRef(w, s));
        VisitColony(a, w.ColOf(s), s == w.cur, full, keep);
        a.s(w.LookOf(s));
        if (s > 0) { Side& sd = w.sides[s - 1]; a.s(sd.name); int c = sd.livery.r | sd.livery.g << 8 | sd.livery.b << 16; a.i(c); if constexpr (A::reading) sd.livery = {(unsigned char)(c & 255), (unsigned char)((c >> 8) & 255), (unsigned char)((c >> 16) & 255), 255}; }
    }
    if (full) {
        VisitKnowledge(a, w.know);
        a.vec(w.log, [&](std::string& s) { a.s(s); });
        a.vec(w.scores, [&](ScoreCard& c) { a.i(c.birds); a.i(c.nests); a.i(c.isles); a.i(c.cache); a.i(c.kills); a.i(c.founder); a.i(c.total); a.i(c.research); a.i(c.pearls); a.i(c.faith); a.i(c.thefts); a.i(c.kraken); a.i(c.legacy); });
        // the towns' markets (what they have, your standing), offers made to you and by you, truces
        a.vec(w.towns, [&](Town& t) { a.i(t.isle); a.v3(t.dock); for (int g = 0; g < G_COUNT; g++) { a.f(t.price[g]); a.f(t.stock[g]); } a.f(t.storm); a.vec(t.rep, [&](float& r) { a.f(r); }); });
        a.vec(w.townCredit, [&](float& c) { a.f(c); });
        std::vector<Barter> mine;
        if constexpr (!A::reading) for (const auto& o : w.offers) if ((o.from == w.cur || o.to == w.cur) && w.time - o.t < World::DAY * 2) mine.push_back(o);
        a.vec(A::reading ? w.offers : mine, [&](Barter& o) { a.i(o.id); a.i(o.from); a.i(o.to); for (int g = 0; g < G_COUNT; g++) { a.i(o.give[g]); a.i(o.get[g]); } a.f(o.truceDays); a.f(o.t); a.i(o.state); a.b(o.pact); a.i(o.loanFlock); });
        a.vec(w.truceUntil, [&](float& v) { a.f(v); });
        std::vector<float> st;
        if constexpr (!A::reading) st = ZoneStocks(w);
        a.vec(st, [&](float& v) { a.q8(v, 1.5f); });
        if constexpr (A::reading) w.mirrorStock = st;
    }
    // the dangerous islands and the weather
    { Kraken& k = w.kraken; a.i(k.isle); a.i(k.mood); a.f(k.hp); a.f(k.hpMax); a.b(k.dead); a.i(k.killedBy); a.v3(k.arm); a.f(k.armT); }
    { Ape& p = w.ape; a.i(p.isle); a.f(p.sleepT); a.v3(p.pos); a.v3(p.rockFrom); a.v3(p.rockTo); a.f(p.rockT); a.f(p.plantsBurnt); }
    { Volcano& v = w.volcano; a.i(v.isle); a.f(v.next); a.f(v.tremorT); a.f(v.ashT); a.i(v.eruptions); }
    { WreckState& r = w.wreck; a.i(r.isle); a.i(r.hold); a.b(r.bell); }
    { Weather& e = w.weather; a.i(e.kind); a.f(e.t); a.f(e.next); }
    { IsleState& x = w.isx; a.i(x.ghost); a.v3(x.ghostC0); a.i(x.whale); a.f(x.whaleNext); a.f(x.whaleUnderT); a.i(x.dives); a.vec(x.birdConv, [&](float& v) { a.f(v); }); }
    if constexpr (A::reading) { w.SetWreckPose(); w.SetGhostPose(); }
    VisitSea(a, w, w.me.pos);
    if (a.bad()) return;
    // the war's effects, numbered (a mirror appends the ones it hasn't had)
    uint32_t end = (uint32_t)(w.warFxBase + w.warFx.size());
    a.u(end);
    std::vector<World::WarFx> tail;
    if constexpr (!A::reading) { size_t k = std::min<size_t>(w.warFx.size(), 24); tail.assign(w.warFx.end() - k, w.warFx.end()); }
    a.vec(tail, [&](World::WarFx& e) { P16(a, e.p); a.i(e.kind); a.i(e.side); a.e(e.role); a.ang(e.yaw); });
    if constexpr (A::reading) {
        uint32_t first = end - (uint32_t)tail.size(), have = (uint32_t)(w.warFxBase + w.warFx.size());
        if (have > end || end - have > 4096 || have < first) { w.warFx.clear(); w.warFxBase = first; have = first; }
        for (uint32_t k = have; k < end; k++) w.warFx.push_back(tail[k - first]);
        if (w.warFx.size() > 256) { w.warFx.erase(w.warFx.begin(), w.warFx.begin() + 128); w.warFxBase += 128; }
    }
}
}  // namespace

void WriteWorld(World& w, int viewer, Writer& out, bool full) {
    viewer = std::clamp(viewer, 0, (int)w.sides.size());
    Out o{out};
    uint32_t magic = 0x31544C46;   // "FLT1"
    o.u(magic);
    uint32_t seed = w.opts.seed; o.u(seed);
    int players = w.opts.players, arr = (int)w.opts.arr, home = (int)w.opts.home; o.i(players); o.i(arr); o.i(home);
    uint32_t mask = w.opts.humanMask; o.u(mask);
    float minutes = w.opts.minutes; o.f(minutes);
    int seasons = w.opts.seasons; o.i(seasons);   // (the long match: its map has the expansion's islands)
    o.i(viewer);
    o.b(full);
    w.WithSide(viewer, [&] { Visit(o, w, full); });
}
void PackWorld(World& w, int viewer, Writer& out, bool full) {
    Writer raw; WriteWorld(w, viewer, raw, full);
    int cz = 0;
    unsigned char* z = CompressData(raw.b.data(), (int)raw.b.size(), &cz);
    if (!z || cz <= 0) { out.Bytes(raw.b.data(), raw.b.size()); return; }   // (no compressor: the plain snapshot reads too)
    out.U32(0x5A544C46); out.U32((uint32_t)raw.b.size()); out.Bytes(z, (size_t)cz);   // "FLTZ"
    MemFree(z);
}
bool ReadWorld(Reader& r, World& w, bool keepOwn) {
    In in{r};
    uint32_t magic = 0; in.u(magic);
    if (magic == 0x5A544C46) {
        uint32_t rawN = r.U32();
        if (r.bad || r.i >= r.n || rawN == 0 || rawN > (1u << 23)) return false;
        int outN = 0;
        unsigned char* raw = DecompressData(r.p + r.i, (int)(r.n - r.i), &outN);
        r.i = r.n;
        if (!raw) return false;
        bool ok = (uint32_t)outN == rawN;
        if (ok) { Reader inner(raw, (size_t)outN); ok = ReadWorld(inner, w, keepOwn) && !inner.bad; }
        MemFree(raw);
        return ok;
    }
    if (magic != 0x31544C46) return false;
    uint32_t seed = 0, mask = 0; int players = 0, arr = 0, home = 0, viewer = 0, seasons = 0; float minutes = 0; bool full = true;
    in.u(seed); in.i(players); in.i(arr); in.i(home); in.u(mask); in.f(minutes); in.i(seasons); in.i(viewer); in.b(full);
    if (r.bad || players < 2 || players > 6 || arr < 0 || arr >= (int)Arrangement::COUNT || home < 0 || home >= (int)IsleType::COUNT || !IsStartType((IsleType)home) || seasons < 0 || seasons > 8 || viewer < 0 || viewer >= players) return false;
    if (!w.wholeMap || !w.mirror || w.opts.seed != seed || w.opts.players != players || (int)w.opts.arr != arr || (int)w.opts.home != home || w.opts.humanMask != mask || w.opts.minutes != minutes || w.opts.seasons != seasons) {
        // a new match (or the first snapshot): the same map from the same seed and options
        std::string why;
        if (!rt::DataOk(&why)) return false;
        MapOpts o; o.players = players; o.arr = (Arrangement)arr; o.home = (IsleType)home; o.humanMask = mask; o.minutes = minutes; o.multi = true; o.seasons = seasons;
        w.Init("taloned", seed, o);
        w.mirror = true;
        if ((int)w.sides.size() + 1 != players) return false;
        keepOwn = false;
    }
    if (viewer > (int)w.sides.size()) return false;
    if (w.cur != viewer) { if (w.cur != 0) w.SwapSide(w.cur - 1); if (viewer != 0) w.SwapSide(viewer - 1); }
    Founder mine = w.me;
    Visit(in, w, full);
    if (r.bad) return false;
    if (keepOwn) {
        // the guest's own bird: its flight is its own prediction, eased toward where the host has it (a little ahead,
        // for the time the snapshot took); anything but flight is the host's to say
        Founder& h = w.me;
        Vector3 hostAt = Vector3Add(h.pos, Vector3Scale(h.vel, 0.06f));
        if (h.st == FState::Fly && mine.st == FState::Fly && Vector3Distance(mine.pos, hostAt) < 8) {
            Vector3 p = Vector3Lerp(mine.pos, hostAt, 0.15f);
            h.pos = p; h.vel = mine.vel; h.yaw = mine.yaw; h.pitch = mine.pitch; h.bank = mine.bank; h.airspeed = mine.airspeed;
            h.flapping = mine.flapping; h.sprinting = mine.sprinting; h.gliding = mine.gliding;
        }
    }
    return true;
}

// ---------------------------------------------------------------- the host
namespace {
class FlightHost : public arcade::GameHost {
public:
    std::unique_ptr<World> w = std::make_unique<World>();
    int arr = 0, homeType = 0, minutes = 30;
    bool test = false; int seasonsOpt = 0;
    int players = 2;
    std::vector<FounderInput> pend;
    uint32_t lobbyAi = 0, autoMask = 0;
    bool first = true;
    float acc = 0, stepDt = 1 / 30.0f;
    uint32_t tick = 0;
    mutable std::vector<std::vector<uint8_t>> cache = std::vector<std::vector<uint8_t>>(6);
    mutable uint32_t cacheTick[6] = {~0u, ~0u, ~0u, ~0u, ~0u, ~0u}, sent[6] = {};

    void Configure(const std::string& opts) override {
        int a = 0, h = 0, m = 30;
        if (sscanf(opts.c_str(), "%d:%d:%d", &a, &h, &m) >= 1) { arr = std::clamp(a, 0, (int)Arrangement::COUNT - 1); homeType = std::clamp(h, 0, 9); minutes = std::clamp(m, 1, 120); }
        test = opts.find(":test") != std::string::npos;
        size_t ss = opts.find(":seasons="); seasonsOpt = ss != std::string::npos ? std::clamp(atoi(opts.c_str() + ss + 9), 0, 8) : 0;
        size_t sp = opts.find(":step=");
        stepDt = sp != std::string::npos ? std::clamp((float)atof(opts.c_str() + sp + 6), 1 / 60.0f, 0.1f) : 1 / 30.0f;
    }
    void Start(int n, uint32_t seed) override {
        players = std::clamp(n, 2, 6);
        MapOpts o; o.players = players; o.arr = (Arrangement)arr; o.home = StartTypeOf(homeType);
        o.humanMask = (1u << players) - 1; o.minutes = (float)minutes; o.multi = true; o.seasons = seasonsOpt;
        w = std::make_unique<World>();
        w->Init("taloned", seed, o);
        w->name0 = "Host";
        pend.assign(players, FounderInput{});
        for (int s = 0; s < players; s++) { pend[s].yaw = w->FounderOf(s).yaw; }
        first = true; lobbyAi = autoMask = 0; acc = 0; tick = 0;
        for (int v = 0; v < 6; v++) { cacheTick[v] = ~0u; sent[v] = 0; }
    }
    bool Act(int p, Reader& r) override {
        if (p < 0 || p >= players) return false;
        size_t at = r.i;
        int kind = (int)r.U8();
        if (r.bad) return false;
        if (kind == FA_INPUT) { FounderInput in; if (!ReadInput(r, in)) return false; MergeInput(pend[p], in); return false; }
        if (kind == FA_AUTOPILOT) { int on = (int)r.U8(); if (r.bad || !test) return false; autoMask = on ? autoMask | (1u << p) : autoMask & ~(1u << p); return true; }
        r.i = at;   // (the order reads its own kind)
        return ApplyOrder(*w, p, r);
    }
    bool Tick(float dt, uint32_t ai) override {
        World& W = *w;
        if (first) {
            // the lobby's AI seats are bots from the start (a bot's colony: it raids); the people's stay people's
            first = false; lobbyAi = ai;
            for (int s = 1; s < players; s++) if ((ai >> s) & 1) { HumanRef(W, s) = false; BotRef(W, s) = true; W.sides[s - 1].name = TextFormat("Bot %d", s); }
        }
        // a person the AI stands in for (dropped for two minutes) is played cautiously; one who comes back plays again
        uint32_t cautious = 0;
        for (int s = 0; s < players; s++) {
            if ((lobbyAi >> s) & 1) continue;
            if ((ai >> s) & 1) autoMask &= ~(1u << s);   // (a person who's gone keeps no autopilot: the cautious AI has it)
            bool aiNow = ((ai >> s) & 1) || ((autoMask >> s) & 1);
            BotRef(W, s) = aiNow;
            if (((ai >> s) & 1) && !((autoMask >> s) & 1)) cautious |= 1u << s;
        }
        W.cautiousMask = cautious;
        acc += std::min(dt, 0.25f);
        int steps = 0;
        while (acc >= stepDt && steps < 8) {
            acc -= stepDt; steps++;
            W.sideIn.assign(pend.begin(), pend.end());
            W.Step(stepDt, pend[0]);
            for (auto& p : pend) ClearPresses(p);
            tick++;
        }
        return true;
    }
    void Snapshot(int viewer, Writer& out) const override {
        if (viewer < 0 || viewer >= players) viewer = 0;
        if (viewer == 0 && !test) { out.U32(0x30484C46); return; }   // "FLH0": the host's own screen draws the real world
        if (cacheTick[viewer] != tick) {
            Writer t; PackWorld(*w, viewer, t, (sent[viewer]++ % SNAP_FULL_EVERY) == 0);
            cache[viewer] = std::move(t.b); cacheTick[viewer] = tick;
        }
        out.Bytes(cache[viewer].data(), cache[viewer].size());
    }
    bool Over() const override { return w->over; }
};
}  // namespace
std::unique_ptr<arcade::GameHost> MakeFlightHost() { return std::make_unique<FlightHost>(); }
World* FlightHostWorld(arcade::GameHost* h) { auto* f = dynamic_cast<FlightHost*>(h); return f ? f->w.get() : nullptr; }
std::string FlightHostOpts(int arrangement, int homeType, int minutes) { return TextFormat("%d:%d:%d", arrangement, homeType, minutes); }
uint32_t FlightDataHash() {
    // every rule a peer must share: the founders, the economy, the roles, the war, the score, the sea's stocks
    uint32_t h = 2166136261u;
    static const char* FILES[] = {"flight_founders.json", "flight_economy.json", "flight_roles.json", "flight_war.json", "flight_scoring.json", "sea/tropical/spawn.json"};
    for (const char* f : FILES) {
        FILE* fp = fopen((FlightDataDir() + "/" + f).c_str(), "rb");
        if (!fp) { h = Fnv1a(f, strlen(f), h); continue; }
        std::vector<uint8_t> b; uint8_t buf[4096]; size_t k;
        while ((k = fread(buf, 1, sizeof buf, fp)) > 0) b.insert(b.end(), buf, buf + k);
        fclose(fp);
        h = Fnv1a(b.data(), b.size(), h);
    }
    return h;
}

// ---------------------------------------------------------------- --flight-net-test
namespace {
void Autopilot(arcade::GameHost& h, int player, bool on) { Writer w; w.U8(FA_AUTOPILOT); w.U8(on ? 1 : 0); Reader r(w.b); h.Act(player, r); }
bool Send(arcade::GameHost& h, int player, const Writer& w) { Reader r(w.b); return h.Act(player, r); }
int Birds(const Colony& c) { int n = 0; for (const auto& b : c.birds) n += b.alive && b.stage != BStage::Egg; return n; }
}
int RunFlightNetTest() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Flight, stage 5: network play (inputs, orders, snapshots, the host)\n");
    std::string why;
    if (!rt::DataOk(&why)) { printf("FAIL: no data: %s\n", why.c_str()); return 1; }
    {   // input frames
        FounderInput in; in.yaw = 1.25f; in.pitch = -0.4f; in.steer = {0.5f, -1}; in.flap = true; in.sprint = true; in.interact = true;
        Writer w; WriteInput(in, w); Reader r(w.b); r.U8(); FounderInput o;
        check(ReadInput(r, o) && o.yaw == 1.25f && o.flap && o.sprint && !o.brake && o.interact && fabsf(o.steer.y + 1) < 0.01f, TextFormat("an input frame round-trips in %d bytes", (int)w.b.size()));
        FounderInput acc; MergeInput(acc, in); FounderInput next; next.yaw = 2; next.flap = true; MergeInput(acc, next);
        check(acc.interact && acc.yaw == 2, "presses add up between steps; the steering is the newest");
        ClearPresses(acc);
        check(!acc.interact && acc.flap, "and a step's use leaves only the held keys");
    }
    // a four-seat match: three people (one of them the host) and a lobby AI seat; the people's colonies run on the
    // autopilot (tests only) for two game days so there's something to see
    auto host = MakeFlightHost();
    host->Configure("0:0:30:test:step=0.1");
    host->Start(4, 20261003);
    World& W = *FlightHostWorld(host.get());
    check(W.sides.size() == 3 && W.multi && W.matchLen == 30 * 60, TextFormat("the host's options: four starting islands, a %.0f-minute match", W.matchLen / 60));
    for (int p = 0; p < 3; p++) Autopilot(*host, p, true);
    { Writer o; OrderHello(o, "Ana", "albatross", "admiral;lagoon;caps;120"); check(Send(*host, 1, o) && W.SideName(1) == "Ana" && Founders()[W.FounderOf(1).def].key == "albatross" && W.LookOf(1) == "admiral;lagoon;caps;120", "a player's hello names their colony, picks their founder and dresses it (costume, livery)"); }
    host->Tick(0.1f, 1u << 3);
    check(!W.HumanOf(3) && W.HumanOf(1) && W.SideName(3) == "Bot 3", "the lobby's AI seat is a bot colony; the people's are theirs");
    for (int k = 0; k < (int)(World::DAY * 2 / 0.1f); k++) host->Tick(0.1f, 1u << 3);
    int b1 = Birds(W.ColOf(1));
    int eggs1 = 0; for (const auto& b : W.ColOf(1).birds) eggs1 += b.alive && b.stage == BStage::Egg;
    check(b1 + eggs1 >= 2, TextFormat("two days on: colonies of %d, %d, %d and %d birds (player 1's: %d eggs too)", Birds(W.ColOf(0)), b1, Birds(W.ColOf(2)), Birds(W.ColOf(3)), eggs1));
    // the snapshot for player 1: their side in the fields, a mirror that writes back the same bytes
    {
        Writer a; WriteWorld(W, 1, a, true);
        auto mir = std::make_unique<World>(); Reader r(a.b);
        bool ok = ReadWorld(r, *mir);
        Writer b; WriteWorld(*mir, 1, b, true);
        check(ok && r.Done() && mir->mirror && mir->cur == 1, TextFormat("player 1's snapshot (%d bytes) builds a mirror with their colony in the fields", (int)a.b.size()));
        check(a.b == b.b, "the mirror writes back exactly the bytes it read");
        check(mir->LookOf(1) == "admiral;lagoon;caps;120", "the costume and livery reach the mirror");
        check(Vector3Distance(mir->me.pos, W.FounderOf(1).pos) < 1e-4f && Birds(mir->col) == b1 && mir->know.isle == W.sides[0].know.isle, "the mirror's own Founder, colony and knowledge are player 1's");
        int near = 0; Vector2 e{W.FounderOf(1).pos.x, W.FounderOf(1).pos.z};
        for (const auto& b : W.ColOf(2).birds) if (b.alive && Vector2Distance(Qxz(b.pos), e) < 330) near++;
        check((int)mir->ColOf(2).birds.size() == near && near < Birds(W.ColOf(2)) + 1, TextFormat("another colony's birds only near player 1: %d of %d", near, Birds(W.ColOf(2))));
        int fish = 0; for (const auto& g : mir->eco.agents) fish += g.alive;
        int hostFish = 0; for (const auto& g : W.eco.agents) hostFish += g.alive && g.diver < 0;
        check(fish > 0 && fish < hostFish, TextFormat("the sea's fish only round player 1's bird: %d of %d", fish, hostFish));
        Writer pf, pp; PackWorld(W, 1, pf, true); PackWorld(W, 1, pp, false);
        float kbs = (pf.b.size() + 3.0f * pp.b.size()) / 4 * 20 / 1024;
        check(kbs < 120, TextFormat("on the wire: a full snapshot %d B, the ones between %d B: %.0f KB/s to each guest at 20 Hz", (int)pf.b.size(), (int)pp.b.size(), kbs));
        Writer v0; host->Snapshot(0, v0);
        check(!v0.b.empty(), "the host's own snapshot (a test host writes it in full)");
    }
    // orders
    {
        Writer o; OrderPlan(o, Role::Scout, 0.7f);
        check(Send(*host, 2, o) && fabsf(W.ColOf(2).plan[(int)Role::Scout] - 0.7f) < 1e-5f, "an order reaches its own colony: player 2's fledging plan");
        Writer n; OrderNests(n, 5); Send(*host, 2, n);
        check(W.ColOf(2).nestsWanted == 5 || W.ColOf(2).nestsWanted == (int)W.ColOf(2).sites.size(), "player 2's nests wanted");
        // warriors for player 2, a flock of them, sent to raid player 1
        Colony& C = W.ColOf(2);
        std::vector<int> ids;
        for (int k = 0; k < 4; k++) { Bird b; b.id = C.nextId++; b.stage = BStage::Adult; b.role = k < 2 ? Role::Skirmisher : Role::Striker; b.hp = RoleOf(b.role).hp; b.fight = 25; b.hunger = 1; b.pos = C.caches[0].pos; C.birds.push_back(b); ids.push_back(b.id); }
        Writer mk; OrderFlockMake(mk, ids, Formation::Hammer, Alt::High, Stance::Raid);
        check(Send(*host, 2, mk) && !W.ColOf(2).flocks.empty(), "player 2 forms a flock by order");
        int fid = W.ColOf(2).flocks.back().id;
        Writer bad; OrderFlockTarget(bad, fid, Target::Cache, 2, -1, -1, -1, {});
        check(!Send(*host, 2, bad), "a raid on your own caches is refused");
        Writer go; OrderFlockTarget(go, fid, Target::Cache, 1, W.HomeOf(1), -1, -1, W.isles[W.HomeOf(1)].c);
        check(Send(*host, 2, go) && W.FindFlock(2, fid)->target == Target::Cache && W.FindFlock(2, fid)->tSide == 1, "and sends it to raid player 1's caches");
        Writer other; OrderFlockHome(other, fid);
        check(!Send(*host, 1, other) || W.FindFlock(2, fid)->target == Target::Cache, "player 1 can't order player 2's flock");
        Writer junk; junk.U8(FA_PLAN); junk.U8(99);
        check(!Send(*host, 2, junk), "a malformed order is refused");
    }
    // a person flies their own Founder by input
    {
        Autopilot(*host, 1, false);
        host->Tick(0.1f, 1u << 3);
        Founder& F = W.FounderOf(1);
        F.st = FState::Perched; F.pos = W.isles[W.HomeOf(1)].nest; F.stamina = 8; F.hunger = 1;
        Vector3 p0 = F.pos;
        for (int k = 0; k < 30; k++) {
            FounderInput in; in.yaw = F.yaw; in.pitch = 0.3f; in.flap = true; in.takeoff = k == 0;
            Writer w; WriteInput(in, w); Reader r(w.b); host->Act(1, r);
            host->Tick(0.1f, 1u << 3);
        }
        check(F.st == FState::Fly && Vector3Distance(F.pos, p0) > 15, TextFormat("player 1's input lifts their Founder off its nest and away (%.0f m in 3 s)", Vector3Distance(F.pos, p0)));
        // a guest's prediction: the mirror flies ahead on the same input and stays with the host
        auto mir = std::make_unique<World>();
        { Writer a; PackWorld(W, 1, a, true); Reader r(a.b); ReadWorld(r, *mir); }
        float worst = 0;
        for (int k = 0; k < 40; k++) {
            FounderInput in; in.yaw = F.yaw + 0.4f; in.pitch = 0.1f; in.flap = true;
            { Writer w; WriteInput(in, w); Reader r(w.b); host->Act(1, r); }
            host->Tick(0.1f, 1u << 3);
            for (int s = 0; s < 6; s++) mir->PredictFounder(1 / 60.0f, in);
            if (k % 2 == 1) { Writer a; PackWorld(W, 1, a, false); Reader r(a.b); ReadWorld(r, *mir, true); }
            worst = std::max(worst, Vector3Distance(mir->me.pos, W.FounderOf(1).pos));
        }
        check(worst < 4, TextFormat("a guest's own Founder, flown ahead between snapshots, stays within %.1f m of the host's", worst));
    }
    // a person who drops: their colony is the AI's (cautious: it doesn't raid), and theirs again when they're back
    {
        host->Tick(0.1f, (1u << 3) | (1u << 1));
        check(W.BotFlown(1) && ((W.cautiousMask >> 1) & 1) && W.HumanOf(1), "player 1 drops: a cautious AI flies and runs their colony");
        int before = (int)W.ColOf(1).flocks.size();
        for (int k = 0; k < 600; k++) host->Tick(0.1f, (1u << 3) | (1u << 1));
        int raids = 0; for (const auto& f : W.ColOf(1).flocks) raids += f.target == Target::Cache || f.target == Target::Nests;
        check(raids == 0, TextFormat("and in a minute it sends no raids (%d flocks before, %d now)", before, (int)W.ColOf(1).flocks.size()));
        host->Tick(0.1f, 1u << 3);
        check(!W.BotFlown(1), "player 1 rejoins: their Founder is theirs again");
    }
    // the end: a one-minute match ends at the limit with a winner, on every screen
    {
        auto h2 = MakeFlightHost();
        h2->Configure("1:2:1:test:step=0.1");
        h2->Start(2, 9);
        World& V = *FlightHostWorld(h2.get());
        for (int k = 0; k < 620 && !h2->Over(); k++) h2->Tick(0.1f, 0);
        check(h2->Over() && V.winner >= 0 && V.scores.size() == 2 && V.overReason.find("Time") != std::string::npos, TextFormat("a one-minute match ends at the limit: %s (%d to %d)", V.overReason.c_str(), V.scores.size() > 0 ? V.scores[0].total : -1, V.scores.size() > 1 ? V.scores[1].total : -1));
        auto mir = std::make_unique<World>(); Writer a; WriteWorld(V, 1, a, true); Reader r(a.b);
        check(ReadWorld(r, *mir) && mir->over && mir->winner == V.winner && mir->Score(0).total == V.scores[0].total, "the guest's mirror shows the same end and the same scores");
        Writer late; OrderPlan(late, Role::Fisher, 1);
        check(!Send(*h2, 0, late), "a finished match takes no orders");
    }
    printf(fails ? "flight-net-test: %d check(s) failed\n" : "flight-net-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}

// ---------------------------------------------------------------- --net-loop flight: the stage-5 gate
int RunFlightNetLoop(bool forceMemory) {
    using namespace arcade;
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::string err;
    if (!rt::DataOk(&err)) { printf("FAIL: no data: %s\n", err.c_str()); return 1; }
    bool real = !forceMemory && net::Init(&err);
    auto make = [&]() { return real ? net::MakeTransport() : net::MakeMemoryTransport(); };
    uint16_t port = 47820;
    std::string addr = real ? "127.0.0.1:" + std::to_string(port) : "mem:" + std::to_string(port);
    // the doc's gate is six players finishing a 30-minute match; over real sockets the test runs in real time, so it
    // plays a shorter one there (DEPTH_FLIGHT_MINUTES overrides)
    int minutes = getenv("DEPTH_FLIGHT_MINUTES") ? std::max(1, atoi(getenv("DEPTH_FLIGHT_MINUTES"))) : real ? 3 : 30;
    printf("net-loop flight over %s: a host and five guests, a %d-minute match\n", real ? "GameNetworkingSockets (loopback UDP)" : "the in-memory transport", minutes);
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    const int NG = 5;
    Session host, gs[NG];
    Profile ph{"Host", 50};
    if (!host.Host(ph, G_FLIGHT, &err, port, make(), false)) { printf("FAIL: host: %s\n", err.c_str()); return 1; }
    host.gameOpts = TextFormat("0:0:%d:test%s", minutes, real ? "" : ":step=0.1");
    const float dt = real ? 1 / 30.0f : 0.1f;
    double t = 0;
    std::vector<std::unique_ptr<World>> mirror; for (int k = 0; k < NG; k++) mirror.push_back(std::make_unique<World>());
    int seen[NG] = {}, mirrorOk[NG] = {}, readFails = 0;
    size_t bytes = 0, biggest = 0; int snaps = 0;
    auto pace = [&]() { if (real) std::this_thread::sleep_for(std::chrono::milliseconds(33)); };
    auto step = [&](int frames) {
        for (int f = 0; f < frames; f++) {
            t += dt; host.Update(t, dt);
            for (int k = 0; k < NG; k++) {
                gs[k].Update(t, dt);
                if (gs[k].stage == S_PLAYING && gs[k].stateVersion != seen[k] && !gs[k].Snapshot().empty()) {
                    seen[k] = gs[k].stateVersion;
                    Reader r(gs[k].Snapshot());
                    if (ReadWorld(r, *mirror[k], mirror[k]->mirror)) mirrorOk[k]++; else readFails++;
                    bytes += gs[k].Snapshot().size(); biggest = std::max(biggest, gs[k].Snapshot().size()); snaps++;
                }
            }
            pace();
        }
    };
    auto until = [&](std::function<bool()> ok, float seconds) { for (int i = 0; i < seconds / dt && !ok(); i++) step(1); };
    for (int k = 0; k < NG; k++) { Profile p{std::string("Bird ") + char('A' + k), (uint64_t)(60 + k)}; if (!gs[k].Join(p, addr, &err, 0, make())) { printf("FAIL: join: %s\n", err.c_str()); return 1; } }
    until([&] { for (auto& g : gs) if (g.stage != S_LOBBY) return false; return true; }, 15);
    for (auto& g : gs) g.SetReady(true);
    until([&] { for (int s = 1; s <= NG; s++) if (!host.seats[s].ready) return false; return true; }, 10);
    std::string why;
    check(host.Launch(&why), "six founders launch the Flight" + (why.empty() ? std::string() : ": " + why));
    until([&] { for (int k = 0; k < NG; k++) if (mirrorOk[k] == 0) return false; return true; }, 20);
    bool all = true; for (int k = 0; k < NG; k++) all = all && mirror[k]->sides.size() == 5 && mirror[k]->cur == gs[k].MyPlayer();
    check(all, "every guest mirrors the six-colony map from their own side");
    World& truth = *FlightHostWorld(host.HostGame());
    // everyone says hello; the host and guests 1-4 let the autopilot fly and run their colonies (a test seat's
    // stand-in for a person); guest 0 plays by hand: input and orders
    { Writer o; OrderHello(o, "Host", "taloned"); host.Act(o); }
    for (int k = 0; k < NG; k++) { Writer o; OrderHello(o, std::string("Bird ") + char('A' + k), Founders()[(k * 3 + 1) % Founders().size()].key); gs[k].Act(o); }
    { Writer w; w.U8(FA_AUTOPILOT); w.U8(1); host.Act(w); }
    for (int k = 1; k < NG; k++) { Writer w; w.U8(FA_AUTOPILOT); w.U8(1); gs[k].Act(w); }
    step(5);
    int me = gs[0].MyPlayer();
    bool autoOk = !truth.BotFlown(me); for (int k = 1; k < NG; k++) autoOk = autoOk && truth.BotFlown(gs[k].MyPlayer());
    check(truth.SideName(me) == "Bird A" && autoOk, "names arrive; the autopilot seats fly themselves, guest 0 flies by hand");
    // guest 0: off the nest by input, a circuit, back
    Vector3 start = truth.FounderOf(me).pos;
    float far = 0;
    for (int f = 0; f < (int)(20 / dt); f++) {
        World& m = *mirror[0];
        FounderInput in; in.yaw = m.me.yaw + 0.15f; in.pitch = 0.15f; in.flap = true; in.takeoff = f < 3;
        Writer w; WriteInput(in, w); gs[0].Act(w);
        m.PredictFounder(dt, in);
        step(1);
        far = std::max(far, Vector3Distance(truth.FounderOf(me).pos, start));
    }
    check(far > 30, TextFormat("guest 0 flies their Founder by input: %.0f m from the nest", far));
    { Writer o; OrderPlan(o, Role::Fisher, 0.8f); gs[0].Act(o); Writer n; OrderNests(n, 4); gs[0].Act(n); }
    step(10);
    check(fabsf(truth.ColOf(me).plan[(int)Role::Fisher] - 0.8f) < 1e-4f && truth.ColOf(me).nestsWanted == 4 && fabsf(mirror[0]->col.plan[(int)Role::Fisher] - 0.8f) < 1e-4f, "guest 0's orders run their colony, and their mirror shows it");
    // the match runs; guest 0 sends a heartbeat of input; halfway, guest 4 leaves (its colony goes to a cautious AI)
    int gone = gs[4].MyPlayer();
    bool left = false;
    int lastMin = -1;
    while (!truth.over && t < minutes * 60 + 120) {
        { FounderInput in; in.yaw = mirror[0]->me.yaw; Writer w; WriteInput(in, w); gs[0].Act(w); }
        step(1);
        if (!left && truth.time > truth.matchLen * 0.5f) { gs[4].Leave(); left = true; }
        int mm = (int)(truth.time / 60);
        if (mm != lastMin && mm % 5 == 0) { lastMin = mm; int birds = 0; for (int s = 0; s < 6; s++) birds += Birds(truth.ColOf(s)); printf("    [%2d min] %d birds on the map, %d snapshots read\n", mm, birds, snaps); }
    }
    check(truth.over, TextFormat("the match ends: %s", truth.overReason.c_str()));
    step(30);
    check(left && truth.BotFlown(gone) && ((truth.cautiousMask >> gone) & 1), TextFormat("guest 4 left halfway: a cautious AI flew and ran their colony to the end (left %d, bot %d, cautious %d)", (int)left, (int)truth.BotFlown(gone), (int)((truth.cautiousMask >> gone) & 1)));
    bool agree = true;
    for (int k = 0; k < NG - 1; k++) agree = agree && mirror[k]->over && mirror[k]->winner == truth.winner && mirror[k]->Score(truth.winner).total == truth.scores[truth.winner].total;
    check(agree, TextFormat("every remaining guest sees the same end: %s wins with %d", truth.SideName(truth.winner).c_str(), truth.scores.empty() ? -1 : truth.scores[truth.winner].total));
    int lost = 0; for (int s = 1; s < MAX_PLAYERS; s++) lost += host.seats[s].used && host.seats[s].lost && s != host.SeatOfPlayer(gone);
    check(lost == 0 && readFails == 0, TextFormat("nobody else was lost; %d snapshots read, none refused", snaps));
    int birds = 0; for (int s = 0; s < 6; s++) birds += Birds(truth.ColOf(s));
    check(birds >= 6 * std::clamp(minutes / 10, 1, 3), TextFormat("six colonies lived through it: %d birds at the end (%d minutes)", birds, minutes));   // (a short test match is a day or two: a bird or two each)
    printf("    standings:"); for (int s = 0; s < 6; s++) printf("  %s %d", truth.SideName(s).c_str(), truth.scores[s].total); printf("\n");
    check(snaps > 0, TextFormat("%d snapshots, %.1f KB on average, %.1f KB the biggest", snaps, snaps ? bytes / 1024.0 / snaps : 0.0, biggest / 1024.0));
    for (auto& g : gs) g.Leave();
    host.Leave();
    step(5);
    if (real) net::Shutdown();
    printf(fails ? "%d FAILED\n" : "net-loop flight: all checks passed\n", fails);
    return fails ? 1 : 0;
}

}  // namespace fl
