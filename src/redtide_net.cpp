// Red Tide over the network (see redtide_net.h): divers' inputs applied to a Match, the match's snapshot (one Visit
// walks every field the screens draw, for writing and for reading, so the two can never disagree), the host's
// GameHost, and the tests: --redtide-net-test and --net-loop redtide.
#include "redtide_net.h"
#include "arcade_session.h"
#include "net.h"
#include "raymath.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <thread>

namespace rt {

// ---------------------------------------------------------------- input
void WriteDiverInput(const DiverInput& in, Writer& w) {
    auto q = [](float v) { return (uint32_t)(int8_t)std::clamp((int)lroundf(v * 127), -127, 127) & 0xFF; };
    w.F32(in.yaw); w.F32(in.pitch);
    w.U8(q(in.wish.x)); w.U8(q(in.wish.z)); w.U8(q(in.vert));
    w.U16(in.btn); w.U8((uint8_t)in.slot);
}
bool ReadDiverInput(Reader& r, DiverInput& in) {
    auto dq = [](uint32_t b) { return (int8_t)(uint8_t)b / 127.0f; };
    in.yaw = r.F32(); in.pitch = r.F32();
    in.wish.x = dq(r.U8()); in.wish.y = 0; in.wish.z = dq(r.U8()); in.vert = dq(r.U8());
    in.btn = r.U16(); in.slot = (int8_t)(uint8_t)r.U8();
    if (r.bad || !std::isfinite(in.yaw) || !std::isfinite(in.pitch)) return false;
    in.yaw = remainderf(in.yaw, 2 * PI);
    in.pitch = std::clamp(in.pitch, -1.45f, 1.45f);
    if (Vector3Length(in.wish) > 1.01f) in.wish = Vector3Normalize(in.wish);
    if (in.slot > 2) in.slot = -1;
    return true;
}
void MergeDiverInput(DiverInput& into, const DiverInput& n) {
    uint32_t presses = (into.btn | n.btn) & DI_PRESSES;
    into.yaw = n.yaw; into.pitch = n.pitch; into.wish = n.wish; into.vert = n.vert;
    into.btn = (n.btn & ~DI_PRESSES) | presses;
    if (n.slot >= 0) into.slot = n.slot;
}
void ClearDiverPresses(DiverInput& in) { in.btn &= ~DI_PRESSES; in.slot = -1; }

void ApplyDiverInput(Match& m, int di, const DiverInput& in, float dt) {
    if (di < 0 || di >= (int)m.divers.size()) return;
    DiverState& d = m.divers[di];
    d.yaw = in.yaw; d.pitch = in.pitch;
    if (m.over) { m.SteerDiver(di, {0, 0, 0}, 0, false, false, dt); return; }
    m.SteerDiver(di, in.wish, in.vert, in.btn & DI_SPRINT, in.btn & DI_ADS, dt);
    m.Fire(di, in.btn & DI_FIRE, dt);
    if (in.btn & DI_RELOAD_P) m.Reload(di);
    if (in.btn & DI_MELEE_P) m.Melee(di);
    if (in.btn & DI_THROW_P) m.ThrowLimpet(di);
    if (in.btn & DI_TAC_P) m.CycleTactical(di);
    if (in.btn & DI_BUILD_P) m.UseBuild(di);
    if (in.btn & DI_BRUSH_P) m.UseBrush(di);
    if (in.btn & DI_BENCH_P) { int si = m.NearestStation(d.pos, 3.0f); if (si >= 0 && m.level.stations[si].type == StationType::Workbench) m.CycleBench(di); else if (si >= 0 && m.level.stations[si].type == StationType::Forge) m.ForgeBlade(di); }
    if (in.btn & DI_DRUM_P) m.BeatDrum(di);
    if (in.btn & DI_CHARM_P) m.UseCharm(di);
    if (in.btn & DI_PING_P) m.Ping(di);
    if (in.btn & DI_USE_P) m.Interact(di, false, dt);
    else if (in.btn & DI_USE) m.Interact(di, true, dt);
    if (in.slot >= 0) m.SwapWeapon(di, in.slot);
}

void WriteInputAction(const DiverInput& in, Writer& w) { w.U8(RA_INPUT); WriteDiverInput(in, w); }
void WriteLookAction(const std::string& suit, const std::string& helmet, const std::string& skin, const std::string& costume, Writer& w) {
    w.U8(RA_LOOK); w.Str(suit); w.Str(helmet); w.Str(skin); w.Str(costume);
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
    // quantised: 16 bits across lo..hi, 8 bits across 0..max, 8 signed bits across -max..max
    void q16(float& v, float lo, float hi) { w.U16((uint32_t)std::clamp((int)lroundf((v - lo) / (hi - lo) * 65535), 0, 65535)); }
    void q8(float& v, float max) { w.U8((uint32_t)std::clamp((int)lroundf(v / max * 255), 0, 255)); }
    void s8(float& v, float max) { w.U8((uint32_t)(uint8_t)(int8_t)std::clamp((int)lroundf(v / max * 127), -127, 127)); }
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
        if (n > 50000 || r.bad) { r.bad = true; n = 0; }
        v.resize(n);
        for (auto& x : v) { fn(x); if (r.bad) break; }
    }
    void q16(float& v, float lo, float hi) { v = lo + r.U16() / 65535.0f * (hi - lo); }
    void q8(float& v, float max) { v = r.U8() / 255.0f * max; }
    void s8(float& v, float max) { v = (int8_t)(uint8_t)r.U8() / 127.0f * max; }
    bool bad() const { return r.bad; }
};
// the box a map's positions are quantised in (its bounds, with room to spare)
struct Box { Vector3 lo, hi; };
Box BoxOf(const Match& m) { return {Vector3Subtract(m.map->boundsMin, {8, 8, 8}), Vector3Add(m.map->boundsMax, {8, 8, 8})}; }
template <class A> void P16(A& a, Vector3& p, const Box& b) { a.q16(p.x, b.lo.x, b.hi.x); a.q16(p.y, b.lo.y, b.hi.y); a.q16(p.z, b.lo.z, b.hi.z); }

// containers
template <class A> void SetStr(A& a, std::set<std::string>& v) {
    std::vector<std::string> t(v.begin(), v.end());
    a.vec(t, [&](std::string& x) { a.s(x); });
    if constexpr (A::reading) v = std::set<std::string>(t.begin(), t.end());
}
template <class A> void SetInt(A& a, std::set<int>& v) {
    std::vector<int> t(v.begin(), v.end());
    a.vec(t, [&](int& x) { a.i(x); });
    if constexpr (A::reading) v = std::set<int>(t.begin(), t.end());
}
template <class A, class V, class F> void MapInt(A& a, std::map<int, V>& m, F fn) {
    std::vector<std::pair<int, V>> t(m.begin(), m.end());
    a.vec(t, [&](std::pair<int, V>& kv) { a.i(kv.first); fn(kv.second); });
    if constexpr (A::reading) { m.clear(); for (auto& kv : t) m[kv.first] = kv.second; }
}
template <class A> void Chars(A& a, std::vector<char>& v) { a.vec(v, [&](char& c) { bool x = c != 0; a.b(x); c = x ? 1 : 0; }); }
template <class A> void Ints(A& a, std::vector<int>& v) { a.vec(v, [&](int& x) { a.i(x); }); }

template <class A> void VisitHeld(A& a, Held& h) { a.i(h.def); a.i(h.mag); a.i(h.reserve); a.b(h.forged); a.i(h.altAmmo); }

template <class A> void VisitDiver(A& a, DiverState& d) {
    a.i(d.slot); a.b(d.bot); a.b(d.invulnerable);
    a.v3(d.pos); a.v3(d.vel); a.f(d.yaw); a.f(d.pitch); a.i(d.zone);
    a.f(d.hp); a.f(d.hpMax); a.f(d.regenT);
    a.b(d.downed); a.b(d.dead); a.f(d.downT); a.f(d.reviveT); a.f(d.reviveTouchT); a.f(d.selfReviveT); a.i(d.selfRevives); a.i(d.quickBought); a.i(d.reviver);
    a.vec(d.weapons, [&](Held& h) { VisitHeld(a, h); }); a.i(d.cur); a.i(d.slots);
    VisitHeld(a, d.downHeld); a.b(d.harpoonHour);
    a.f(d.fireT); a.f(d.reloadT); a.b(d.reloading); a.i(d.burstLeft); a.f(d.spin); a.f(d.meleeT); a.b(d.ads); a.f(d.recoil);
    a.i(d.hitCount); a.i(d.limpets);
    SetStr(a, d.tonics);
    a.i(d.scrip); a.i(d.scripEarned); a.f(d.stamina);
    a.f(d.heldT); a.i(d.holder); a.i(d.struggle);
    a.f(d.stunT); a.f(d.aimSway); a.f(d.poisonT); a.f(d.bleedT); a.f(d.slowT); a.f(d.slowMult); a.f(d.flinchT);
    a.i(d.agent); a.i(d.slipLink); a.f(d.slipT); a.f(d.driftT); a.i(d.drumUses);
    a.b(d.spark); a.b(d.ichorJar); a.b(d.egg); a.f(d.wormT); a.f(d.voidT); a.f(d.decoyCd);
    a.vec(d.pouch, [&](std::string& s) { a.s(s); }); a.i(d.pouchNext);
    a.i(d.inkBombs); a.i(d.tactical); a.i(d.chumBags); a.i(d.flares); a.b(d.brush); a.i(d.partsMask); a.e(d.build);
    a.f(d.shieldHP); a.f(d.bashCd); a.i(d.benchSel); a.i(d.inkCaps); a.b(d.drumClean);
    a.f(d.circleT); a.f(d.finsT); a.f(d.shellT); a.f(d.ghostT); a.v3(d.circlePos); a.i(d.luckKills); a.b(d.keepBrines); a.b(d.luckyLocker);
    a.f(d.cutT); a.f(d.pingT); a.f(d.pingCd); a.i(d.blade); a.b(d.bladeForged); a.b(d.repairKit); a.f(d.repairT); a.i(d.repairPaid);
    a.i(d.kills); a.i(d.headshots); a.i(d.downs); a.i(d.revives);
    a.s(d.suit); a.s(d.helmet); a.s(d.skin); a.s(d.costume);
    a.f(d.hitMarker); a.b(d.hitWeak); a.f(d.hurtT); a.v3(d.hurtFrom);
    a.s(d.lastHitBy); a.s(d.lastKill); a.f(d.lastKillT);
}

// (an agent in about 25 bytes: the snapshot has to fit GameNetworkingSockets' unreliable message, 16 KB)
template <class A> void VisitAgent(A& a, Agent& g, const Box& box) {
    a.b(g.alive);
    a.i(g.sp); a.i(g.diver);
    if (!g.alive) return;   // (the dead keep only their species: their bodies are corpses)
    P16(a, g.pos, box);
    // velocity (it only turns the drawing and dead-reckons between snapshots)
    a.s8(g.vel.x, 12.7f); a.s8(g.vel.y, 12.7f); a.s8(g.vel.z, 12.7f);
    a.i(g.zone); a.e(g.st);
    int hm = (int)lroundf(g.hpMax * 4); a.i(hm);
    float frac = g.hpMax > 0 ? std::clamp(g.hp / g.hpMax, 0.0f, 1.0f) : 0;
    if constexpr (A::reading) { g.hpMax = hm / 4.0f; a.q8(frac, 1); g.hp = frac * g.hpMax; }
    else a.q8(frac, 1);
    a.i(g.target); a.i(g.host); a.i(g.squad); a.i(g.unit); a.i(g.group);
    a.q8(g.wound, 1); a.q8(g.stun, 25.5f); a.q8(g.held, 25.5f);
    int flags = (g.downed ? 1 : 0) | (g.oil ? 2 : 0); a.i(flags);
    if constexpr (A::reading) { g.downed = flags & 1; g.oil = flags & 2; }
}

// the scent field: only the cells that hold blood (the rest is zero), at most a few thousand
template <class A> void VisitScent(A& a, Field& f) {
    std::vector<std::pair<uint32_t, float>> cells;
    if constexpr (!A::reading) {
        for (uint32_t k = 0; k < (uint32_t)f.v.size(); k++) if (f.v[k] >= 0.12f) cells.push_back({k, f.v[k]});
        if (cells.size() > 4000) { std::partial_sort(cells.begin(), cells.begin() + 4000, cells.end(), [](auto& x, auto& y) { return x.second > y.second; }); cells.resize(4000); }
    }
    uint32_t prev = 0;
    std::sort(cells.begin(), cells.end());
    a.vec(cells, [&](std::pair<uint32_t, float>& c) {
        int gap = (int)(c.first - prev); a.i(gap);
        if constexpr (A::reading) c.first = prev + (uint32_t)gap;
        prev = c.first;
        int q = (int)lroundf(c.second * 8); a.i(q);   // (eighths: the mirror keeps the same quantised value)
        if constexpr (A::reading) c.second = q / 8.0f;
    });
    if constexpr (A::reading) {
        std::fill(f.v.begin(), f.v.end(), 0.0f);
        for (auto& c : cells) if (c.first < f.v.size()) f.v[c.first] = c.second;
        f.BuildSat();
    }
}

template <class A> void VisitEco(A& a, Ecosystem& e, const Box& box) {
    a.f(e.time); a.i(e.tide); a.i(e.squadsSpawned); a.f(e.flowSign); a.f(e.cleanerRage);
    a.vec(e.agents, [&](Agent& g) { VisitAgent(a, g, box); });
    if constexpr (A::reading) for (size_t k = 0; k < e.agents.size(); k++) e.agents[k].rng = (uint32_t)(k + 1) * 2654435761u;   // (a steady per-beast phase for the drawing)
    a.vec(e.corpses, [&](Corpse& c) { a.b(c.active); if (!c.active) return; a.i(c.sp); P16(a, c.pos, box); a.i(c.zone); a.f(c.bloodLeft); a.f(c.life); a.f(c.age); a.b(c.byPlayer); });
    a.vec(e.flora, [&](FloraPatch& p) { a.f(p.units); });   // (the patches themselves are the map's: the mirror placed the same ones)
    a.vec(e.squads, [&](Squad& s) { a.b(s.alive); a.b(s.hunt); Ints(a, s.members); });
    a.vec(e.alarm, [&](float& x) { a.f(x); });
    a.vec(e.inks, [&](Ecosystem::Ink& k) { a.v3(k.pos); a.f(k.r); a.f(k.t); });
    a.vec(e.curtains, [&](Ecosystem::Curtain& c) { a.v3(c.a); a.v3(c.b); a.f(c.y0); a.f(c.y1); a.f(c.t); a.i(c.maxSize); });
    {   // beasts eaten by beasts (the results panel)
        std::vector<std::pair<std::pair<int, int>, int>> t(e.eatenBy.begin(), e.eatenBy.end());
        a.vec(t, [&](std::pair<std::pair<int, int>, int>& kv) { a.i(kv.first.first); a.i(kv.first.second); a.i(kv.second); });
        if constexpr (A::reading) { e.eatenBy.clear(); for (auto& kv : t) e.eatenBy[kv.first] = kv.second; }
    }
    VisitScent(a, e.scent);
}

template <class A> void Visit(A& a, Match& m) {
    // ---- the match itself
    a.f(m.time); a.i(m.tide); a.i(m.quota); a.i(m.tideKills); a.e(m.phase); a.f(m.phaseT); a.i(m.maxTide);
    a.b(m.over); a.s(m.overReason); a.b(m.power);
    a.i(m.lockerSpot); a.i(m.lockerSpots); a.i(m.lockerPulls); a.i(m.lockerMoveAt); a.i(m.teamPulls); a.b(m.wonderHeld); a.f(m.lockerMovedT);
    a.f(m.fireSaleT); a.f(m.doubleScripT); a.f(m.frenzyT); a.f(m.harpoonT);
    a.b(m.predatorHunt); a.i(m.huntApexLeft);
    a.b(m.bossActive); a.i(m.bossAgent); a.i(m.bossPhase); a.i(m.bossWind); a.f(m.bossWindT); a.i(m.bossTarget);
    a.f(m.bossGillsT); a.f(m.bossInhaleT); a.i(m.bossInhaleDiver); a.b(m.bossProvoked); a.b(m.bossKilled);
    a.f(m.trapT); SetStr(a, m.keys); a.b(m.safeOpen);
    a.vec(m.floorKeys, [&](Match::FloorKey& k) { a.s(k.name); a.v3(k.pos); });
    Chars(a, m.linkOpen);
    a.vec(m.level.doors, [&](Door& d) { a.b(d.open); });
    MapInt(a, m.enemyTellT, [&](float& v) { a.f(v); });
    a.vec(m.barricades, [&](Match::Barricade& b) { a.i(b.strands); a.f(b.tearT); });   // (where they are is the map's: the mirror built the same ones)
    a.b(m.breachOpen); a.b(m.cacheOpen); a.i(m.drumBeats); a.b(m.alliesHostile); a.f(m.tideTurnT);
    a.vec(m.polyps, [&](Match::Polyp& p) { a.v3(p.pos); a.f(p.t); a.i(p.owner); a.b(p.forged); });
    SetStr(a, m.dossierSeen); a.f(m.forgeAt); a.b(m.questDone); a.b(m.logRead);
    SetInt(a, m.lanternsOut); a.b(m.nesting); a.i(m.nests); a.i(m.questStep); Ints(a, m.questAt);
    a.vec(m.bonusEarned, [&](std::string& s) { a.s(s); });
    a.vec(m.ichor, [&](Match::Ichor& k) { a.v3(k.pos); a.f(k.t); });
    a.i(m.wyrmState); a.i(m.wyrmGrate); a.f(m.wyrmT); a.f(m.wyrmUp); a.i(m.horror); a.b(m.revealAll);
    MapInt(a, m.beaconOn, [&](bool& v) { a.b(v); });
    a.vec(m.gas, [&](Match::Gas& g) { a.v3(g.pos); a.f(g.t); a.f(g.r); });
    a.vec(m.lures, [&](Match::LurePt& l) { a.v3(l.pos); a.f(l.t); a.i(l.owner); a.b(l.forged); });
    a.f(m.lureHP); a.f(m.darkT); a.f(m.tentacleT); a.v3(m.tentaclePos);
    a.b(m.eggPlaced); a.v3(m.eggPos); a.b(m.relictGone); a.b(m.ledgeDropped);
    a.vec(m.salvage, [&](Match::SalvagePart& p) { a.i(p.build); a.i(p.part); a.v3(p.pos); a.i(p.zone); a.b(p.taken); a.i(p.carrier); });
    a.vec(m.deployed, [&](Match::Deployed& d) { a.e(d.type); a.v3(d.pos); a.v3(d.dir); a.f(d.t); a.i(d.owner); a.b(d.alive); a.b(d.stopped); a.i(d.held); });
    a.vec(m.flareLights, [&](Match::FlareLight& f) { a.v3(f.pos); a.f(f.t); a.i(f.owner); });
    a.vec(m.captions, [&](Caption& c) { a.s(c.who); a.s(c.text); a.f(c.t); });
    a.vec(m.darts, [&](Dart& d) { a.v3(d.pos); a.v3(d.vel); a.v3(d.start); a.f(d.life); a.i(d.weapon); a.i(d.owner); a.b(d.forged); a.b(d.alive); a.i(d.enemy); a.i(d.kind); a.i(d.stuck); });
    a.vec(m.drops, [&](FloorDrop& d) { a.e(d.type); a.v3(d.pos); a.f(d.t); a.b(d.alive); a.i(d.weapon); });
    a.vec(m.crates, [&](Crate& c) { a.v3(c.pos); a.f(c.t); a.b(c.fallen); a.i(c.kind); a.f(c.radius); a.f(c.top); });
    a.i(m.deathsByBeast); a.i(m.deathsByEnemy); a.i(m.deathsByHazard); a.f(m.timeToTide10); a.i(m.scripAt10);
    // ---- the divers
    a.vec(m.divers, [&](DiverState& d) { VisitDiver(a, d); });
    // ---- the web
    VisitEco(a, m.eco, BoxOf(m));
    // ---- the newest effects, numbered (a mirror appends the ones it hasn't had)
    uint32_t end = m.FxEnd();
    a.u(end);
    std::vector<FxEvent> tail;
    if constexpr (!A::reading) { size_t k = std::min<size_t>(m.fx.size(), 48); tail.assign(m.fx.end() - k, m.fx.end()); }
    a.vec(tail, [&](FxEvent& e) { a.i(e.kind); a.v3(e.pos); a.v3(e.dir); });
    if constexpr (A::reading) {
        uint32_t first = end - (uint32_t)tail.size(), have = m.FxEnd();
        if (have > end || end - have > 4096) { m.fx.clear(); m.fxBase = first; have = first; }   // (a new match, or far behind: start at the window)
        if (have < first) { m.fx.clear(); m.fxBase = first; have = first; }
        for (uint32_t k = have; k < end; k++) m.fx.push_back(tail[k - first]);
        m.TrimFx(256);
    }
}
} // namespace

void WriteMatch(const Match& mc, Writer& out) {
    Match& m = const_cast<Match&>(mc);
    Out o{out};
    uint32_t magic = 0x31545452;   // "RTT1"
    o.u(magic);
    o.s(m.mapKey); int players = m.players; o.i(players); o.u(m.seed); int mode = m.mode; o.i(mode); int season = m.season; o.i(season);
    Visit(o, m);
}

bool ReadMatch(Reader& r, Match& m, int keepLook) {
    In in{r};
    uint32_t magic = 0; in.u(magic);
    if (magic != 0x31545452) return false;
    std::string key; int players = 1; uint32_t seed = 0;
    int mode = 0, season = 0;
    in.s(key); in.i(players); in.u(seed); in.i(mode); in.i(season);
    if (r.bad || players < 1 || players > 4 || mode < 0 || mode >= RM_COUNT) return false;
    if (!m.map || m.mapKey != key || m.seed != seed || m.players != players || m.mode != mode || m.season != season) {
        m.mode = mode; m.season = std::clamp(season, 0, 99);
        std::string why;
        if (!DataOk(&why)) return false;
        m.Init(key, players, seed, false);
        keepLook = -1;
    }
    bool keep = keepLook >= 0 && keepLook < (int)m.divers.size();
    DiverState mine = keep ? m.divers[keepLook] : DiverState{};
    size_t nAgents = m.eco.agents.size();
    Visit(in, m);
    if (r.bad) return false;
    if (keep && keepLook < (int)m.divers.size()) {
        DiverState& d = m.divers[keepLook];
        d.yaw = mine.yaw; d.pitch = mine.pitch;
        // the guest's own diver: its predicted position, eased toward the host's (a jump only when far apart)
        if (!d.dead && !d.downed && d.slipLink < 0 && Vector3Distance(mine.pos, d.pos) < 2.0f) d.pos = Vector3Lerp(mine.pos, d.pos, 0.3f);
    }
    (void)nAgents;
    return true;
}

// ---------------------------------------------------------------- the host
namespace {
class RedTideHost : public arcade::GameHost {
public:
    std::unique_ptr<Match> m = std::make_unique<Match>();
    std::string mapKey = "ship";
    int mode = RM_STANDARD, season = 0;
    DiverInput pend[4];
    bool looked[4] = {};
    int players = 1;
    float acc = 0;
    uint32_t tick = 0;
    mutable uint32_t cachedTick = ~0u;
    mutable std::vector<uint8_t> cache;

    void Configure(const std::string& opts) override {
        static const char* MAPS[] = {"ship", "cave", "reef", "atlantis", "void"};
        std::string mk = opts.substr(0, opts.find(':'));
        std::string rest = opts.find(':') == std::string::npos ? std::string() : opts.substr(opts.find(':') + 1);
        mode = rest.empty() ? RM_STANDARD : ModeFromKey(rest.substr(0, rest.find(':')));
        season = rest.find(':') == std::string::npos ? 0 : std::max(0, atoi(rest.substr(rest.find(':') + 1).c_str()));
        mapKey = "ship";
        for (const char* k : MAPS) if (mk == k) mapKey = k;
    }
    void Start(int n, uint32_t seed) override {
        players = std::clamp(n, 1, 4);
        m = std::make_unique<Match>();
        m->mode = mode; m->season = season;
        m->Init(mapKey, players, seed, false);
        for (int i = 0; i < 4; i++) { pend[i] = DiverInput{}; looked[i] = false; }
        for (int i = 0; i < players && i < (int)m->divers.size(); i++) { pend[i].yaw = m->divers[i].yaw; pend[i].pitch = m->divers[i].pitch; }
        acc = 0; tick = 0; cachedTick = ~0u;
    }
    bool Act(int p, Reader& r) override {
        if (p < 0 || p >= players || p >= (int)m->divers.size()) return false;
        int kind = (int)r.U8();
        if (kind == RA_INPUT) { DiverInput in; if (!ReadDiverInput(r, in)) return false; MergeDiverInput(pend[p], in); return false; }
        if (kind == RA_LOOK) {
            std::string a = r.Str(), b = r.Str(), c = r.Str(), d = r.Str();
            if (r.bad) return false;
            DiverState& dv = m->divers[p];
            dv.suit = a; dv.helmet = b; dv.skin = c; dv.costume = d; looked[p] = true;
            return true;
        }
        return false;
    }
    bool Tick(float dt, uint32_t ai) override {
        // the AI seats are bot divers (a dropped player's diver is taken by a bot after the session's grace)
        for (int i = 0; i < players && i < (int)m->divers.size(); i++) m->divers[i].bot = (ai >> i) & 1;
        const float step = 1 / 60.0f;
        acc += std::min(dt, 0.1f);
        while (acc >= step) {
            acc -= step;
            for (int p = 0; p < players && p < (int)m->divers.size(); p++) {
                if ((ai >> p) & 1) { pend[p] = DiverInput{}; pend[p].yaw = m->divers[p].yaw; pend[p].pitch = m->divers[p].pitch; continue; }
                ApplyDiverInput(*m, p, pend[p], step);
                ClearDiverPresses(pend[p]);
            }
            m->Step(step);
            tick++;
        }
        return true;
    }
    void Snapshot(int, Writer& out) const override {
        // (every diver sees the same water: written once a step, copied to each player)
        if (cachedTick != tick) { Writer t; WriteMatch(*m, t); cache = std::move(t.b); cachedTick = tick; }
        out.Bytes(cache.data(), cache.size());
    }
    bool Over() const override { return m->over; }
};
}
std::unique_ptr<arcade::GameHost> MakeRedTideHost() { return std::make_unique<RedTideHost>(); }
Match* RedTideHostMatch(arcade::GameHost* h) { auto* t = dynamic_cast<RedTideHost*>(h); return t ? t->m.get() : nullptr; }
uint32_t RedTideDataHash() {
    Writer w;
    const WeaponsData& W = Weapons();
    w.U32((uint32_t)W.weapons.size());
    for (const auto& x : W.weapons) { w.Str(x.id); w.F32(x.damage); w.F32(x.rpm); w.I32(x.mag); w.I32(x.price); }
    for (const auto& t : W.tonics) { w.Str(t.id); w.I32(t.price); }
    static const char* MAPS[] = {"ship", "cave", "reef", "atlantis", "void"};
    if (DataOk()) for (const char* k : MAPS) {
        const MapData& md = Map(k);
        w.Str(md.key); w.U32((uint32_t)md.species.size()); w.U32((uint32_t)md.zones.size()); w.U32((uint32_t)md.links.size());
        for (const auto& s : md.species) { w.Str(s.name); w.I32(s.size); }
    }
    return Fnv1a(w.b.data(), w.b.size());
}

// ---------------------------------------------------------------- --redtide-net-test
int RunRedTideNetTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("Red Tide: network play (inputs, snapshots, the host)\n");
    std::string why;
    if (!DataOk(&why)) { printf("FAIL: no data: %s\n", why.c_str()); return 1; }
    const float dt = 1 / 60.0f;
    {
        DiverInput in; in.yaw = 1.25f; in.pitch = -0.4f; in.wish = {0.6f, 0, -0.8f}; in.vert = 1; in.btn = DI_FIRE | DI_SPRINT | DI_RELOAD_P; in.slot = 1;
        Writer w; WriteDiverInput(in, w); Reader r(w.b); DiverInput o;
        check(ReadDiverInput(r, o) && o.yaw == 1.25f && fabsf(o.wish.z + 0.8f) < 0.01f && o.vert > 0.99f && o.btn == in.btn && o.slot == 1,
              TextFormat("an input frame round-trips in %d bytes", (int)w.b.size()));
        DiverInput acc; MergeDiverInput(acc, in); DiverInput next; next.btn = DI_FIRE; next.yaw = 2; MergeDiverInput(acc, next);
        check((acc.btn & DI_RELOAD_P) && acc.yaw == 2 && acc.slot == 1, "presses and picks add up between steps; the look and held keys are the newest");
        ClearDiverPresses(acc);
        check(acc.btn == DI_FIRE && acc.slot == -1, "and once a step has used them only the held keys remain");
        Reader junk(w.b.data(), 5); DiverInput o2;
        check(!ReadDiverInput(junk, o2), "a short frame is refused");
    }
    static const char* MAPS[] = {"ship", "cave", "reef", "atlantis", "void"};
    for (const char* key : MAPS) {
        // a busy match: four bot divers through the first tides, written, read into a mirror, written again
        Match m; m.Init(key, 4, 20261002, true);
        for (auto& d : m.divers) { d.suit = "verdigris"; d.skin = "r_kelp"; d.costume = "rc_lighthouse"; }
        for (int k = 0; k < 60 * 90 && !m.over; k++) m.Step(dt);
        Writer a; WriteMatch(m, a);
        Match mir; Reader r(a.b);
        bool ok = ReadMatch(r, mir);
        Writer b; WriteMatch(mir, b);
        int alive = 0; for (const auto& g : m.eco.agents) alive += g.alive;
        check(ok && r.Done(), TextFormat("%s: tide %d, %d beasts alive of %d; the snapshot is %d bytes and the mirror reads all of it", key, m.tide, alive, (int)m.eco.agents.size(), (int)a.b.size()));
        check(a.b == b.b, TextFormat("%s: the mirror writes back exactly the bytes it read", key));
        {   // where the bytes go
            Writer wa, wsc, wdv; Out oa{wa}, osc{wsc}, odv{wdv}; Box bx = BoxOf(m);
            oa.vec(m.eco.agents, [&](Agent& g) { VisitAgent(oa, g, bx); });
            VisitScent(osc, m.eco.scent);
            odv.vec(m.divers, [&](DiverState& d) { VisitDiver(odv, d); });
            printf("        (%s: beasts %d B, blood %d B, divers %d B, the rest %d B)\n", key, (int)wa.b.size(), (int)wsc.b.size(), (int)wdv.b.size(), (int)(a.b.size() - wa.b.size() - wsc.b.size() - wdv.b.size()));
        }
        bool same = mir.divers.size() == 4 && mir.level.stations.size() == m.level.stations.size() && mir.level.props.size() == m.level.props.size() && mir.eco.flora.size() == m.eco.flora.size();
        for (int i = 0; i < 4 && same; i++) same = Vector3Distance(mir.divers[i].pos, m.divers[i].pos) < 1e-4f && mir.divers[i].costume == "rc_lighthouse";
        check(same, TextFormat("%s: the mirror built the same level from the seed, with every diver where the host has it (and in their costume)", key));
        m.eco.scent.BuildSat();   // (the host's sums are rebuilt on the field's own clock; compare against fresh ones)
        float smell = m.eco.Smell(m.divers[0].pos, m.eco.ZoneAt(m.divers[0].pos), 8), smellM = mir.eco.Smell(mir.divers[0].pos, mir.eco.ZoneAt(mir.divers[0].pos), 8);
        check(fabsf(smell - smellM) <= 0.05f * smell + 2.0f, TextFormat("%s: the blood in the water reads the same on the mirror (%.0f / %.0f)", key, smell, smellM));
    }
    {   // the effects log: a mirror replays each event once, however many snapshots carry it
        Match m; m.Init("ship", 2, 7, true);
        Match mir;
        for (int k = 0; k < 3; k++) m.fx.push_back({k, {0, 0, 0}, {0, 0, 0}});
        Writer a; WriteMatch(m, a); Reader r(a.b); ReadMatch(r, mir);
        uint32_t seen = mir.FxEnd();
        Writer a2; WriteMatch(m, a2); Reader r2(a2.b); ReadMatch(r2, mir);
        check(mir.FxEnd() == seen && seen == m.FxEnd(), "a snapshot that carries the same effects again adds none");
        m.fx.push_back({3, {1, 2, 3}, {0, 0, 0}});
        Writer a3; WriteMatch(m, a3); Reader r3(a3.b); ReadMatch(r3, mir);
        check(mir.FxEnd() == m.FxEnd() && mir.fx.back().kind == 3 && mir.fx.back().pos.y == 2, "a new effect arrives once, in order");
        Reader cut(a3.b.data(), a3.b.size() / 2); Match m2;
        check(!ReadMatch(cut, m2), "a cut-off snapshot is refused");
    }
    {   // the host game: two players and an AI seat
        auto h = MakeRedTideHost();
        h->Configure("cave");
        h->Start(3, 99);
        Match* m = RedTideHostMatch(h.get());
        check(m && m->mapKey == "cave" && m->divers.size() == 3, "the host's options pick the map (the Cave, three divers)");
        Vector3 p0 = m->divers[1].pos;
        for (int k = 0; k < 90; k++) {
            DiverInput in; in.yaw = m->divers[1].yaw; in.wish = {sinf(in.yaw), 0, cosf(in.yaw)};
            Writer w; WriteInputAction(in, w); Reader r(w.b); h->Act(1, r);
            h->Tick(dt, 1u << 2);
        }
        check(Vector3Distance(m->divers[1].pos, p0) > 0.5f, TextFormat("the host moves diver 1 by its player's input (%.1f m)", Vector3Distance(m->divers[1].pos, p0)));
        check(m->divers[2].bot && !m->divers[1].bot && !m->divers[0].bot, "the AI seat is a bot diver; the players' divers aren't");
        Writer lk; WriteLookAction("bone", "h_pearl", "r_kelp", "rc_lighthouse", lk); Reader rl(lk.b);
        check(h->Act(1, rl) && m->divers[1].suit == "bone" && m->divers[1].costume == "rc_lighthouse", "a player's look reaches the host's diver");
        Writer s1, s2; h->Snapshot(0, s1); h->Snapshot(1, s2);
        check(!s1.b.empty() && s1.b == s2.b, TextFormat("every player gets the same water (%d bytes)", (int)s1.b.size()));
        Match mir; Reader rs(s1.b);
        check(ReadMatch(rs, mir, 1) && mir.divers[1].costume == "rc_lighthouse" && mir.mapKey == "cave", "a guest's mirror has the Cave and the teammate's costume");
        int my = 0; DiverState keep = mir.divers[my]; keep.yaw = 2.5f; mir.divers[my].yaw = 2.5f;
        Writer s3; h->Tick(dt, 1u << 2); h->Snapshot(0, s3); Reader r3(s3.b); ReadMatch(r3, mir, my);
        check(mir.divers[my].yaw == 2.5f, "a guest keeps its own view across snapshots");
    }
    printf(fails ? "%d FAILED\n" : "redtide-net-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}

// ---------------------------------------------------------------- --net-loop redtide
int RunRedTideNetLoop(bool forceMemory) {
    using namespace arcade;
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::string err;
    if (!DataOk(&err)) { printf("FAIL: no data: %s\n", err.c_str()); return 1; }
    bool real = !forceMemory && net::Init(&err);
    auto make = [&]() { return real ? net::MakeTransport() : net::MakeMemoryTransport(); };
    uint16_t port = 47810;
    std::string addr = real ? "127.0.0.1:" + std::to_string(port) : "mem:" + std::to_string(port);
    printf("net-loop redtide over %s: a host and three guests\n", real ? "GameNetworkingSockets (loopback UDP)" : "the in-memory transport");
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    const int NG = 3;
    Session host, gs[NG];
    Profile ph{"Diver", 30};
    if (!host.Host(ph, G_RED_TIDE, &err, port, make(), false)) { printf("FAIL: host: %s\n", err.c_str()); return 1; }
    std::string mapKey = getenv("DEPTH_RTMAP") ? getenv("DEPTH_RTMAP") : "atlantis";   // (the biggest water: its snapshots go in parts)
    host.gameOpts = mapKey;
    double t = 0; const float dt = 1 / 60.0f;
    Match mirror[NG]; int seen[NG] = {}; int mirrorOk[NG] = {}; size_t bytes = 0, biggest = 0; int snaps = 0;
    auto pace = [&]() { if (real) std::this_thread::sleep_for(std::chrono::milliseconds(16)); };
    auto step = [&](int frames) {
        for (int f = 0; f < frames; f++) {
            t += dt; host.Update(t, dt);
            for (int k = 0; k < NG; k++) {
                gs[k].Update(t, dt);
                if (gs[k].stage == S_PLAYING && gs[k].stateVersion != seen[k] && !gs[k].Snapshot().empty()) {
                    seen[k] = gs[k].stateVersion;
                    Reader r(gs[k].Snapshot());
                    if (ReadMatch(r, mirror[k], gs[k].MyPlayer())) mirrorOk[k]++;
                    bytes += gs[k].Snapshot().size(); biggest = std::max(biggest, gs[k].Snapshot().size()); snaps++;
                }
            }
            pace();
        }
    };
    auto until = [&](std::function<bool()> ok, float seconds) { for (int i = 0; i < seconds * 60 && !ok(); i++) step(1); };
    for (int k = 0; k < NG; k++) { Profile p{std::string("Diver ") + char('A' + k), (uint64_t)(40 + k)}; if (!gs[k].Join(p, addr, &err, 0, make())) { printf("FAIL: join: %s\n", err.c_str()); return 1; } }
    until([&] { for (auto& g : gs) if (g.stage != S_LOBBY) return false; return true; }, 15);
    for (auto& g : gs) g.SetReady(true);
    until([&] { for (int s = 1; s <= NG; s++) if (!host.seats[s].ready) return false; return true; }, 10);
    std::string why;
    check(host.Launch(&why), "four divers launch Red Tide" + (why.empty() ? std::string() : ": " + why));
    until([&] { for (int k = 0; k < NG; k++) if (mirrorOk[k] == 0) return false; return true; }, 15);
    bool all = true; for (int k = 0; k < NG; k++) all = all && mirrorOk[k] > 0 && mirror[k].divers.size() == 4 && mirror[k].mapKey == mapKey;
    check(all, TextFormat("every guest mirrors %s with four divers in it", mapKey.c_str()));
    Match* truth = RedTideHostMatch(host.HostGame());
    auto send = [&](int k, const DiverInput& in) { Writer w; WriteInputAction(in, w); gs[k].Act(w); };
    // every guest sends its look; every mirror shows each teammate's
    for (int k = 0; k < NG; k++) { Writer w; WriteLookAction("bone", "h_pearl", "", k == 1 ? "rc_lighthouse" : "", w); gs[k].Act(w); }
    int p1 = gs[1].MyPlayer();
    until([&] { return mirror[0].divers[p1].costume == "rc_lighthouse"; }, 5);
    check(mirror[0].divers[p1].costume == "rc_lighthouse" && mirror[2].divers[p1].suit == "bone", "guest 1's costume shows on the other guests' screens");
    // guest 0 swims forward by input alone
    int me = gs[0].MyPlayer();
    Vector3 start = truth->divers[me].pos;
    for (int f = 0; f < 60 * 3; f++) { DiverInput in; in.yaw = mirror[0].divers[me].yaw; in.wish = {sinf(in.yaw), 0, cosf(in.yaw)}; send(0, in); step(1); }
    step(10);
    float moved = Vector3Distance(truth->divers[me].pos, start), agree = Vector3Distance(truth->divers[me].pos, mirror[0].divers[me].pos);
    check(moved > 1.0f && agree < 1.0f, TextFormat("guest 0 swims %.1f m by input; its mirror agrees within %.2f m", moved, agree));
    // the tide comes: the host's beasts, darts and kills show on every mirror
    until([&] { return truth->phase != TidePhase::Calm; }, 40);
    for (int f = 0; f < 60 * 20; f++) { for (int k = 0; k < NG; k++) { DiverInput in; in.yaw = mirror[k].divers[gs[k].MyPlayer()].yaw; send(k, in); } step(1); }
    int liveT = 0, liveM = 0; for (auto& a : truth->eco.agents) liveT += a.alive; for (auto& a : mirror[2].eco.agents) liveM += a.alive;
    check(truth->phase != TidePhase::Calm && abs(liveT - liveM) <= 3 && mirror[2].tide == truth->tide, TextFormat("the tide (%d) runs on every screen: %d beasts on the host, %d on a mirror", truth->tide, liveT, liveM));
    // a guest leaves: its diver goes on as a bot at once
    int gone = gs[2].MyPlayer();
    gs[2].Leave();
    until([&] { return mirror[0].divers[gone].bot; }, 8);
    check(mirror[0].divers[gone].bot, TextFormat("guest 2 leaves: diver %d goes on as a bot", gone));
    check(snaps > 0, TextFormat("%d snapshots, %.1f KB on average, %.1f KB the biggest", snaps, snaps ? bytes / 1024.0 / snaps : 0.0, biggest / 1024.0));
    for (auto& g : gs) g.Leave();
    host.Leave();
    step(10);
    if (real) net::Shutdown();
    printf(fails ? "%d FAILED\n" : "net-loop redtide: all checks passed\n", fails);
    return fails ? 1 : 0;
}

} // namespace rt
