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
void OrderHello(Writer& w, const std::string& name, const std::string& founderKey) { w.U8(FA_HELLO); w.Str(name.substr(0, 24)); w.Str(founderKey); }
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
        std::string name = r.Str(), key = r.Str();
        if (r.bad) return false;
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
        int f = w.MakeFlock(side, ids, (Formation)fo, (Alt)al, (Stance)st);
        if (f >= 0) w.Say(TextFormat("Flock %d formed: pick a target on the chart (M).", f));
        return f >= 0;
    }
    case FA_FLOCK_SET: {
        int id = r.I32(); int fo = (int)r.U8(), al = (int)r.U8(), st = (int)r.U8();
        if (r.bad || fo >= (int)Formation::COUNT || al > 2 || st >= (int)Stance::COUNT) return false;
        Flock* f = w.FindFlock(side, id);
        if (!f) return false;
        f->form = (Formation)fo; f->alt = (Alt)al; f->stance = (Stance)st;
        return true;
    }
    case FA_FLOCK_ORDER: {
        int id = r.I32(); int t = (int)r.U8(); int ts = r.I32(), ti = r.I32(), tz = r.I32(), tf = r.I32(); float x = r.F32(), z = r.F32();
        if (r.bad || t >= (int)Target::COUNT || ts < -1 || ts >= nSides || ti < -1 || ti >= nIsles || tz < -1 || tz >= nZones || !std::isfinite(x) || !std::isfinite(z)) return false;
        if ((t == (int)Target::Cache || t == (int)Target::Nests || t == (int)Target::Flock) && (ts < 0 || ts == side)) return false;
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
        if (r.bad || kind > 1 || C.caches.empty()) return false;
        for (const auto& s : C.builds) if (s.kind == kind) return false;
        Structure n; n.kind = kind;
        n.pos = kind == 0 ? C.caches[0].pos : w.GroundAt(C.caches[0].pos.x + 6, C.caches[0].pos.z + 4);
        C.builds.push_back(n);
        w.Say(std::string("Your builders will raise a ") + (kind == 0 ? "hedge." : "tower."));
        return true;
    }
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
    float bird = 2, chick = 1, nest = 5, island = 30, cachePer = 5, kill = 1, founder = 60;
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
    F("living_bird", s.bird); F("chick", s.chick); F("nest", s.nest); F("island_held", s.island); F("cache_fish_per_point", s.cachePer);
    F("enemy_bird_killed", s.kill); F("founder_never_died", s.founder);
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
    c.isles = nests > 0 || adults > 1 ? (int)K.island : 0;   // (its home, while it holds a colony there)
    c.cache = (int)(fish / std::max(1.0f, K.cachePer));
    c.kills = (int)lroundf(C.kills * K.kill);
    c.founder = F.deaths == 0 ? (int)K.founder : 0;
    c.total = c.birds + c.nests + c.isles + c.cache + c.kills + c.founder;
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
    a.i(f.def); a.e(f.st); a.v3(f.pos); a.v3(f.vel); a.f(f.yaw); a.f(f.pitch); a.f(f.bank); a.f(f.airspeed);
    a.f(f.stamina); a.f(f.hunger); a.f(f.hp);
    int fl = (f.flapping ? 1 : 0) | (f.sprinting ? 2 : 0) | (f.gliding ? 4 : 0) | (f.exhausted ? 8 : 0) | (f.chick ? 16 : 0);
    a.i(fl);
    if constexpr (A::reading) { f.flapping = fl & 1; f.sprinting = fl & 2; f.gliding = fl & 4; f.exhausted = fl & 8; f.chick = fl & 16; }
    a.i(f.carrySp); a.i(f.carrySize); a.i(f.carryTwigs);
    a.f(f.strikeT); a.f(f.strikeLen); a.v3(f.strikeAt); a.v3(f.strikeAim); a.f(f.strikeSpeed);
    a.f(f.struggleT); a.i(f.struggleSp); a.f(f.underT); a.f(f.faintT); a.f(f.respawnT);
    a.i(f.chickFish); a.f(f.adultT); a.i(f.deaths); a.i(f.agent); a.s(f.lastCause);
}
template <class A> void VisitBird(A& a, Bird& b, bool own) {
    a.i(b.id); a.e(b.stage); a.e(b.role); a.e(b.retrainTo); a.i(b.nest);
    P16(a, b.pos); a.s8(b.vel.x, 60); a.s8(b.vel.y, 60); a.s8(b.vel.z, 60);
    a.ang(b.yaw); a.ang(b.flapPh);
    a.q8(b.hunger, 1); a.f(b.age); a.f(b.chillT); a.f(b.retrainT);
    a.e(b.task); a.i(b.carrySp); a.i(b.carrySize); a.i(b.carryTwigs); a.i(b.carryShells);
    a.q8(b.hp, 255); a.q8(b.fight, 25.5f); a.q8(b.netT, 25.5f); a.i(b.flock); a.i(b.tgtSide); a.i(b.tgtId);
    if (own) { a.i(b.scoutIsle); a.i(b.scoutZone); a.e(b.alt); a.b(b.hasOrder); a.b(b.observed); a.i(b.caught); }
}
template <class A> void VisitColony(A& a, Colony& c, bool own, bool full, const std::function<bool(const Bird&)>& keep) {
    // the living birds (another colony's only where the viewer can see them)
    std::vector<Bird> sent;
    if constexpr (!A::reading) { sent.reserve(c.birds.size()); for (const auto& b : c.birds) if (b.alive && (own || keep(b))) sent.push_back(b); }
    a.vec(A::reading ? c.birds : sent, [&](Bird& b) { VisitBird(a, b, own); if constexpr (A::reading) b.alive = true; });
    a.vec(c.nests, [&](Nest& n) { a.i(n.site); a.v3(n.pos); a.f(n.twigs); a.b(n.built); a.b(n.founders); a.i(n.mate); a.i(n.bowl); a.i(n.bowlNeed); a.f(n.mateT); a.f(n.larder); a.i(n.shells); });
    a.vec(c.caches, [&](Cache& k) { a.v3(k.pos); a.b(k.built); a.f(k.twigs); a.vec(k.fish, [&](CachedFish& f) { a.i(f.sp); a.i(f.size); a.f(f.age); }); });
    a.vec(c.sites, [&](Site& s) { a.i(s.nest); });          // (where the sites are is the island's: the mirror placed the same)
    a.vec(c.twigSrc, [&](TwigSource& t) { a.f(t.twigs); });
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
        P16(a, f.pos); a.s8(f.vel.x, 60); a.s8(f.vel.y, 60); a.s8(f.vel.z, 60);
        a.f(f.morale); a.f(f.wins); a.f(f.engagedT); a.i(f.startSize); a.i(f.lost);
        a.b(f.retreating); a.b(f.scattered); a.b(f.leaderDead); a.s(f.name);
    });
    a.vec(c.builds, [&](Structure& s) { a.i(s.kind); a.v3(s.pos); a.f(s.twigs); a.i(s.shells); a.b(s.built); a.i(s.site); });
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
        if (s > 0) { Side& sd = w.sides[s - 1]; a.s(sd.name); int c = sd.livery.r | sd.livery.g << 8 | sd.livery.b << 16; a.i(c); if constexpr (A::reading) sd.livery = {(unsigned char)(c & 255), (unsigned char)((c >> 8) & 255), (unsigned char)((c >> 16) & 255), 255}; }
    }
    if (full) {
        VisitKnowledge(a, w.know);
        a.vec(w.log, [&](std::string& s) { a.s(s); });
        a.vec(w.scores, [&](ScoreCard& c) { a.i(c.birds); a.i(c.nests); a.i(c.isles); a.i(c.cache); a.i(c.kills); a.i(c.founder); a.i(c.total); });
        std::vector<float> st;
        if constexpr (!A::reading) st = ZoneStocks(w);
        a.vec(st, [&](float& v) { a.q8(v, 1.5f); });
        if constexpr (A::reading) w.mirrorStock = st;
    }
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
    uint32_t seed = 0, mask = 0; int players = 0, arr = 0, home = 0, viewer = 0; float minutes = 0; bool full = true;
    in.u(seed); in.i(players); in.i(arr); in.i(home); in.u(mask); in.f(minutes); in.i(viewer); in.b(full);
    if (r.bad || players < 2 || players > 6 || arr < 0 || arr >= (int)Arrangement::COUNT || home < 0 || home > 3 || viewer < 0 || viewer >= players) return false;
    if (!w.wholeMap || !w.mirror || w.opts.seed != seed || w.opts.players != players || (int)w.opts.arr != arr || (int)w.opts.home != home || w.opts.humanMask != mask || w.opts.minutes != minutes) {
        // a new match (or the first snapshot): the same map from the same seed and options
        std::string why;
        if (!rt::DataOk(&why)) return false;
        MapOpts o; o.players = players; o.arr = (Arrangement)arr; o.home = (IsleType)home; o.humanMask = mask; o.minutes = minutes; o.multi = true;
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
    bool test = false;
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
        if (sscanf(opts.c_str(), "%d:%d:%d", &a, &h, &m) >= 1) { arr = std::clamp(a, 0, (int)Arrangement::COUNT - 1); homeType = std::clamp(h, 0, 3); minutes = std::clamp(m, 1, 120); }
        test = opts.find(":test") != std::string::npos;
        size_t sp = opts.find(":step=");
        stepDt = sp != std::string::npos ? std::clamp((float)atof(opts.c_str() + sp + 6), 1 / 60.0f, 0.1f) : 1 / 30.0f;
    }
    void Start(int n, uint32_t seed) override {
        players = std::clamp(n, 2, 6);
        MapOpts o; o.players = players; o.arr = (Arrangement)arr; o.home = (IsleType)homeType;
        o.humanMask = (1u << players) - 1; o.minutes = (float)minutes; o.multi = true;
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

}  // namespace fl
