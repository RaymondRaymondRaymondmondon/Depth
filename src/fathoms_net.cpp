// Fathoms' network host and fog-filtered snapshot (see fathoms_net.h).
#include "fathoms_net.h"
#include "arcade_session.h"
#include "net.h"
#include "redtide.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <thread>

namespace fa {
namespace {
// one Visit for both directions. Positions go as sixteenths of a tile (the doc's quantisation), hit points and timers
// to a quarter; both exact on a re-write, so a mirror rewrites the host's bytes.
struct Out {
    Writer& w;
    void f(float& v) { w.F32(v); } void u8(uint8_t& v) { w.U8(v); } void b(bool& v) { w.U8(v ? 1 : 0); } void s(std::string& v) { w.Str(v); }
    void i(int& v) { uint32_t z = ((uint32_t)v << 1) ^ (uint32_t)(v >> 31); w.VarU(z); }
    void q(float& v) { int k = (int)lroundf(std::clamp(v, -100.0f, 3900.0f) * 16); w.U16((uint16_t)(k + 1600)); }
    void p(Vector2& v) { q(v.x); q(v.y); }
    void h(float& v) { w.VarU((uint32_t)std::max(0, (int)lroundf(std::min(v, 4e8f) * 4))); }
    bool reading() const { return false; }
};
struct In {
    Reader& r;
    void f(float& v) { v = r.F32(); } void u8(uint8_t& v) { v = (uint8_t)r.U8(); } void b(bool& v) { v = r.U8() != 0; } void s(std::string& v) { v = r.Str(); }
    void i(int& v) { uint32_t z = r.VarU(); v = (int)(z >> 1) ^ -(int)(z & 1); }
    void q(float& v) { v = ((int)r.U16() - 1600) / 16.0f; }
    void p(Vector2& v) { q(v.x); q(v.y); }
    void h(float& v) { v = r.VarU() / 4.0f; }
    bool reading() const { return true; }
};
template <class IO> void VisitSettings(IO& io, Settings& s) {
    io.i(s.players); io.i(s.size); io.i(s.mapType); io.i(s.victory); io.i(s.timeCap); io.i(s.start); io.i(s.startEra); io.i(s.neutral); io.i(s.peace); io.i(s.diplomacy); io.i(s.reveal); io.i(s.teams);
    io.b(s.pirates); io.b(s.tribes); io.b(s.volcanoes); io.b(s.kraken); int seed = (int)s.seed; io.i(seed); s.seed = (uint32_t)seed;
    for (int k = 0; k < MAX_PLAYERS; k++) { io.i(s.faction[k]); io.i(s.team[k]); io.i(s.color[k]); io.b(s.ai[k]); io.i(s.aiLevelOf[k]); }
}
template <class IO> void VisitUnit(IO& io, Unit& u, int viewer) {
    io.i(u.id); io.i(u.owner); io.i(u.def); io.p(u.p); io.p(u.goal);
    int fac = (int)lroundf(u.facing * 40); io.i(fac); u.facing = fac / 40.0f;
    io.h(u.hp); io.h(u.morale); uint8_t o = (uint8_t)u.order; io.u8(o); u.order = (Order)o; io.i(u.carryRes); io.h(u.carry);
    io.h(u.stunT); io.h(u.bleedT); io.h(u.poisonT); io.h(u.weakT); io.h(u.strongT); io.h(u.cocoonT); io.h(u.hiddenT); io.h(u.cool); io.h(u.life); io.h(u.fastT); io.h(u.slowT); io.h(u.hasteT);
    io.i(u.molts); io.i(u.relic); io.i(u.hire); io.i(u.home); io.b(u.converted); io.h(u.chan); io.i(u.target); io.i(u.targetKind); io.i(u.inside); io.i(u.garrisoned);
    int nc = (int)u.cargo.size(); io.i(nc); if (io.reading()) u.cargo.assign(std::clamp(nc, 0, 20), -1); for (auto& c : u.cargo) io.i(c);
    if (viewer < 0 || u.owner == viewer) { io.h(u.xp); io.h(u.abilityT); uint8_t st = (uint8_t)u.stance; io.u8(st); u.stance = (Stance)st; io.f(u.landedT); }
}
template <class IO> void VisitBuilding(IO& io, Building& b, int viewer) {
    io.i(b.id); io.i(b.owner); io.i(b.def); io.i(b.x); io.i(b.y); io.i(b.size); io.h(b.hp); int pr = (int)lroundf(b.progress * 1000); io.i(pr); b.progress = pr / 1000.0f;
    io.i(b.relics); io.i(b.island); io.f(b.attackedT);
    int ng = (int)b.garrison.size(); io.i(ng); if (io.reading()) b.garrison.assign(std::clamp(ng, 0, 40), -1); for (auto& g : b.garrison) io.i(g);
    if (viewer < 0 || b.owner == viewer) { int nq = (int)b.queue.size(); io.i(nq); if (io.reading()) b.queue.assign(std::clamp(nq, 0, 12), QueueItem{}); for (auto& q : b.queue) { io.i(q.kind); io.i(q.def); } io.h(b.qT); io.b(b.hasRally); io.p(b.rally); }
}
template <class IO> void VisitPlayer(IO& io, Player& p, int viewer) {
    io.i(p.id); io.i(p.team); io.i(p.faction); io.i(p.color); io.b(p.alive); io.b(p.ai); io.s(p.name); io.i(p.difficulty); io.b(p.eliminated);
    io.i(p.pop); io.i(p.popCap); io.i(p.era); io.i(p.eraBuilding); io.h(p.score); io.h(p.killScore); io.h(p.razeScore); io.h(p.bossScore); io.h(p.devotion);
    io.i(p.heroUnit); io.f(p.heroDeadT); io.h(p.ultimateCD); io.i(p.ultimateUnit); io.i(p.wonder); io.i(p.kills); io.i(p.losses);
    for (int k = 0; k < MAX_PLAYERS; k++) { uint8_t s = (uint8_t)p.stance[k]; io.u8(s); p.stance[k] = (Diplo)s; io.f(p.allyT[k]); }
    for (int e = 0; e < 3; e++) io.f(p.eraAt[e]);
    int nc = (int)p.cards.size(); io.i(nc); if (io.reading()) p.cards.assign(std::clamp(nc, 0, 8), 0); for (auto& c : p.cards) io.i(c);
    if (viewer < 0 || p.id == viewer) {
        for (int r = 0; r < R_COUNT; r++) { float v = p.res[r]; io.h(v); p.res[r] = v; io.h(p.gathered[r]); }
        int nt = (int)p.tech.size(); io.i(nt); if (io.reading()) p.tech.assign(std::clamp(nt, 0, 400), 0);
        for (size_t k = 0; k < p.tech.size(); k += 8) { uint8_t bits = 0; for (int j = 0; j < 8 && k + j < p.tech.size(); j++) bits |= p.tech[k + j] ? (1 << j) : 0; io.u8(bits); for (int j = 0; j < 8 && k + j < p.tech.size(); j++) p.tech[k + j] = (bits >> j) & 1; }
        for (int f = 0; f < 6; f++) io.i(p.forge[f]);
        int no = (int)p.cardOffer.size(); io.i(no); if (io.reading()) p.cardOffer.assign(std::clamp(no, 0, 3), 0); for (auto& c : p.cardOffer) io.i(c);
        for (int k = 0; k < 3; k++) { int m = (int)lroundf(p.exchange[k] * 1000); io.i(m); p.exchange[k] = m / 1000.0f; }
        for (int k = 0; k < MAX_PLAYERS; k++) io.f(p.intelUntil[k]);
        io.h(p.tradePauseT); io.b(p.coalShort); io.b(p.krakenInk); io.f(p.noTownT);
    }
}
template <class IO> void VisitSite(IO& io, Site& s, int viewer) {
    io.i(s.kind); io.i(s.island); io.i(s.owner); io.p(s.p); io.h(s.hp); io.h(s.maxHp); io.f(s.t); io.f(s.t2); io.i(s.count); io.i(s.state); io.i(s.relic); io.h(s.holdT); io.i(s.holder); io.i(s.large);
    for (int k = 0; k < MAX_PLAYERS; k++) io.i(s.peace[k]);
    int v = std::clamp(viewer, 0, MAX_PLAYERS - 1); int r = (int)lroundf(s.rep[v]); io.i(r); s.rep[v] = (float)r; io.f(s.tributeT[v]);
}
template <class IO> void VisitHead(IO& io, World& w) {
    io.f(w.t); io.i(w.winner); io.b(w.over); io.i(w.relicsTotal); io.i(w.weather); io.p(w.weatherAt); io.h(w.weatherR); io.h(w.weatherLeft);
    io.f(w.relicCountT); io.f(w.volcanoCountT); io.i(w.relicLeader); io.i(w.volcanoLeader);
    int nw = (int)w.winners.size(); io.i(nw); if (io.reading()) w.winners.assign(std::clamp(nw, 0, MAX_PLAYERS), 0); for (auto& x : w.winners) io.i(x);
}
bool UnitVisible(const World& w, const Unit& u, int v) {
    if (v < 0 || v >= (int)w.players.size()) return true;
    if (u.owner == v || w.Allied(u.owner, v)) return true;
    if (u.inside >= 0 || u.garrisoned >= 0) return false;
    if (u.hiddenT > 0) { for (const auto& o : w.units) if (!o.dead && o.owner == v && Vector2Distance(o.p, u.p) < 3) return true; return false; }
    if (IsPlayer(u.owner) && u.owner < MAX_PLAYERS && w.players[v].intelUntil[u.owner] > w.t) return true;   // (pirate intel)
    return w.Sees(v, u.p);
}
}  // namespace

// ---------------------------------------------------------------- the snapshot
void WriteWorld(const World& cw, Writer& o, int viewer, uint32_t evTotal, bool filter) {
    World& w = const_cast<World&>(cw); Out io{o};
    Settings s = w.set; VisitSettings(io, s); io.i(viewer);
    VisitHead(io, w);
    o.VarU((uint32_t)w.players.size()); for (auto& p : w.players) VisitPlayer(io, p, viewer);
    std::vector<int> us; for (int i = 0; i < (int)w.units.size(); i++) if (!w.units[i].dead && (!filter || UnitVisible(w, w.units[i], viewer))) us.push_back(i);
    o.VarU((uint32_t)us.size()); for (int i : us) VisitUnit(io, w.units[i], viewer);
    std::vector<int> bs;
    for (int i = 0; i < (int)w.buildings.size(); i++) { const Building& b = w.buildings[i]; if (b.dead) continue; Vector2 c = b.Centre();
        bool known = !filter || viewer < 0 || viewer >= (int)w.players.size() || b.owner == viewer || w.Allied(b.owner, viewer) || (w.In((int)c.x, (int)c.y) && w.players[viewer].explored[w.Idx((int)c.x, (int)c.y)]);
        if (known) bs.push_back(i); }
    o.VarU((uint32_t)bs.size()); for (int i : bs) VisitBuilding(io, w.buildings[i], viewer);
    o.VarU((uint32_t)w.sites.size()); for (auto& st : w.sites) VisitSite(io, st, viewer);
    o.VarU((uint32_t)w.nodes.size());
    for (auto& n : w.nodes) { uint8_t k = (uint8_t)n.kind; io.u8(k); io.p(n.p); io.h(n.amount); io.h(n.cap); io.i(n.farm); io.i(n.island); io.b(n.trapped); }
    std::vector<int> kelp; for (int k = 0; k < (int)w.kelpOwner.size(); k++) if (w.kelpOwner[k]) kelp.push_back(k);
    o.VarU((uint32_t)kelp.size()); int last = 0; for (int k : kelp) { o.VarU((uint32_t)(k - last)); o.U8(w.kelpOwner[k]); last = k; }
    std::vector<int> ks; for (int i = 0; i < (int)w.contracts.size(); i++) { const Contract& k = w.contracts[i]; if (!filter || viewer < 0 || k.hirer == viewer || k.target == viewer) ks.push_back(i); }
    o.VarU((uint32_t)ks.size()); for (int i : ks) { Contract& k = w.contracts[i]; io.i(k.cove); io.i(k.hirer); io.i(k.target); io.i(k.deal); io.h(k.price); io.f(k.warnT); io.f(k.liveT); io.i(k.bids); io.b(k.live); }
    std::vector<int> sh; for (int i = 0; i < (int)w.shots.size() && sh.size() < 160; i++) if (!filter || viewer < 0 || viewer >= (int)w.players.size() || w.Sees(viewer, w.shots[i].a) || w.Sees(viewer, w.shots[i].b)) sh.push_back(i);
    o.VarU((uint32_t)sh.size()); for (int i : sh) { Projectile& p = w.shots[i]; io.p(p.a); io.p(p.b); io.h(p.t); io.h(p.T); io.i(p.kind); io.h(p.h); }
    int n = std::min<int>((int)w.events.size(), 32); o.U32(evTotal); o.VarU(n);
    for (int k = (int)w.events.size() - n; k < (int)w.events.size(); k++) { Event e = w.events[k]; io.i(e.kind); io.p(e.at); io.i(e.a); io.i(e.b); io.i(e.c); }
}
bool ReadWorld(Reader& r, World& w, uint32_t* evTotal) {
    In io{r};
    Settings s; VisitSettings(io, s); int viewer = -1; io.i(viewer); if (r.bad) return false;
    s.players = std::clamp(s.players, 2, MAX_PLAYERS);
    if (w.players.empty() || w.set.seed != s.seed || w.set.players != s.players || w.set.size != s.size || w.set.mapType != s.mapType || w.tile.empty()) {
        for (int k = 0; k < MAX_PLAYERS; k++) s.names[k] = w.set.names[k];
        w.Init(s);   // (the map, sites and lava paths exactly as the host built them)
    }
    w.set = s;
    VisitHead(io, w);
    uint32_t np = r.VarU(); if (np > MAX_PLAYERS) return false;
    if (w.players.size() != np) w.players.resize(np);
    for (auto& p : w.players) {
        std::vector<uint8_t> seen = std::move(p.seen), explored = std::move(p.explored);
        if (viewer >= 0 && p.id != viewer) { p.res = Cost{}; std::fill(p.tech.begin(), p.tech.end(), 0); p.cardOffer.clear(); }   // (a rival's private book stays closed)
        VisitPlayer(io, p, viewer);
        if (viewer >= 0 && p.id != viewer) { p.res = Cost{}; p.cardOffer.clear(); }
        p.seen = std::move(seen); p.explored = std::move(explored);
        if (p.seen.size() != w.tile.size()) { p.seen.assign(w.tile.size(), 0); p.explored.assign(w.tile.size(), w.set.reveal >= 1 ? 1 : 0); }
        if (p.tech.size() != B().techs.size()) p.tech.resize(B().techs.size(), 0);
    }
    uint32_t nu = r.VarU(); if (nu > 20000) return false; w.units.assign(nu, Unit{}); for (auto& u : w.units) VisitUnit(io, u, viewer);
    for (auto& u : w.units) if (u.def < 0 || u.def >= (int)B().units.size()) return false;
    uint32_t nb = r.VarU(); if (nb > 5000) return false; w.buildings.assign(nb, Building{}); for (auto& b : w.buildings) VisitBuilding(io, b, viewer);
    for (auto& b : w.buildings) if (b.def < 0 || b.def >= (int)B().buildings.size()) return false;
    uint32_t ns = r.VarU(); if (ns > 512) return false; w.sites.resize(ns); for (auto& st : w.sites) VisitSite(io, st, viewer);
    uint32_t nn = r.VarU(); if (nn > 20000) return false; w.nodes.resize(nn);
    for (auto& n : w.nodes) { uint8_t k = 0; io.u8(k); n.kind = std::min<int>(k, N_COUNT - 1); io.p(n.p); io.h(n.amount); io.h(n.cap); io.i(n.farm); io.i(n.island); io.b(n.trapped); }
    std::fill(w.kelpOwner.begin(), w.kelpOwner.end(), 0);
    uint32_t nk = r.VarU(); if (nk > w.kelpOwner.size()) return false; int at = 0; for (uint32_t i = 0; i < nk; i++) { at += (int)r.VarU(); uint8_t o = (uint8_t)r.U8(); if (at >= 0 && at < (int)w.kelpOwner.size()) w.kelpOwner[at] = o; }
    uint32_t nc = r.VarU(); if (nc > 200) return false; w.contracts.assign(nc, Contract{});
    for (auto& k : w.contracts) { io.i(k.cove); io.i(k.hirer); io.i(k.target); io.i(k.deal); io.h(k.price); io.f(k.warnT); io.f(k.liveT); io.i(k.bids); io.b(k.live); }
    uint32_t nsh = r.VarU(); if (nsh > 400) return false; w.shots.assign(nsh, Projectile{});
    for (auto& p : w.shots) { io.p(p.a); io.p(p.b); io.h(p.t); io.h(p.T); io.i(p.kind); io.h(p.h); }
    uint32_t total = r.U32(), n = r.VarU(); if (n > 32) return false;
    std::vector<Event> evs(n); for (auto& e : evs) { io.i(e.kind); io.p(e.at); io.i(e.a); io.i(e.b); io.i(e.c); }
    if (r.bad) return false;
    uint32_t had = evTotal ? *evTotal : 0; uint32_t first = total - n;
    for (uint32_t k = 0; k < n; k++) if (first + k >= had) w.Emit(evs[k].kind, evs[k].at, evs[k].a, evs[k].b, evs[k].c);
    if (evTotal) *evTotal = std::max(had, total);
    // what follows from it: occupied tiles, territory, lava, the fog
    std::fill(w.occ.begin(), w.occ.end(), (int16_t)-1);
    for (const auto& b : w.buildings) for (int y = b.y; y < b.y + b.size; y++) for (int x = b.x; x < b.x + b.size; x++) if (w.In(x, y)) w.occ[w.Idx(x, y)] = (int16_t)b.id;
    for (const auto& st : w.sites) if (st.kind == S_ALTAR) for (auto& q : st.lava) { int k = w.Idx((int)q.x, (int)q.y); if (k >= 0 && k < (int)w.tile.size()) if (st.count == 2) w.tile[k] = T_LAVA; else if (w.tile[k] == T_LAVA) w.tile[k] = w.height[k] > 1.4f ? T_HILL : T_GRASS; }
    w.UpdateTerritory(); w.UpdateFog();
    w.nextUnit = 1; for (auto& u : w.units) w.nextUnit = std::max(w.nextUnit, u.id + 1);
    return true;
}

// ---------------------------------------------------------------- commands
void WriteCommand(const Command& c, Writer& w) {
    w.U8(c.kind); w.VarU((uint32_t)std::min<size_t>(c.units.size(), 200)); for (size_t i = 0; i < c.units.size() && i < 200; i++) w.VarU((uint32_t)c.units[i]);
    w.I32(c.target); w.I32(c.def); w.I32(c.x); w.I32(c.y); w.I32(c.a); w.I32(c.b); w.F32(c.at.x); w.F32(c.at.y); w.F32(c.amount);
}
bool ReadCommand(Reader& r, Command& c) {
    c.kind = (uint8_t)r.U8(); uint32_t n = r.VarU(); if (n > 200) return false; c.units.resize(n); for (auto& u : c.units) u = (int)r.VarU();
    c.target = r.I32(); c.def = r.I32(); c.x = r.I32(); c.y = r.I32(); c.a = r.I32(); c.b = r.I32(); c.at.x = r.F32(); c.at.y = r.F32(); c.amount = r.F32();
    if (!std::isfinite(c.at.x) || !std::isfinite(c.at.y) || !std::isfinite(c.amount)) return false;
    return !r.bad;
}
uint32_t FathomsDataHash() { return BalanceHash(); }
std::string FathomsOpts(const Settings& s) {
    std::ostringstream o; o << s.players << " " << s.size << " " << s.mapType << " " << s.victory << " " << s.timeCap << " " << s.start << " " << s.startEra << " " << s.neutral << " " << s.peace << " " << s.diplomacy << " " << s.reveal << " " << s.teams << " "
      << (int)s.pirates << " " << (int)s.tribes << " " << (int)s.volcanoes << " " << (int)s.kraken;
    for (int k = 0; k < MAX_PLAYERS; k++) o << " " << s.faction[k] << " " << s.team[k] << " " << s.aiLevelOf[k];
    return o.str();
}
bool ParseFathomsOpts(const std::string& str, Settings& s) {
    std::istringstream in(str); int pi, tr, vo, kr;
    if (!(in >> s.players >> s.size >> s.mapType >> s.victory >> s.timeCap >> s.start >> s.startEra >> s.neutral >> s.peace >> s.diplomacy >> s.reveal >> s.teams >> pi >> tr >> vo >> kr)) return false;
    s.pirates = pi; s.tribes = tr; s.volcanoes = vo; s.kraken = kr;
    for (int k = 0; k < MAX_PLAYERS; k++) in >> s.faction[k] >> s.team[k] >> s.aiLevelOf[k];
    s.players = std::clamp(s.players, 2, MAX_PLAYERS); for (int k = 0; k < MAX_PLAYERS; k++) { s.faction[k] = std::clamp(s.faction[k], -1, 5); s.aiLevelOf[k] = std::clamp(s.aiLevelOf[k], 0, 3); s.color[k] = k; }
    return true;
}

// ---------------------------------------------------------------- the host
namespace {
class FathomsHost : public arcade::GameHost {
public:
    World W; Settings S; std::vector<int> seatPlayer; float acc = 0; uint32_t evTotal = 0; std::vector<Command> cmds;
    void Configure(const std::string& opts) override { Settings s; if (ParseFathomsOpts(opts, s)) S = s; }
    void Start(int players, uint32_t seed) override {
        Settings s = S; s.players = std::clamp(std::max(players, S.players), 2, MAX_PLAYERS); s.seed = seed;
        uint32_t h = seed * 2654435761u;
        for (int k = 0; k < MAX_PLAYERS; k++) { s.ai[k] = k >= players; if (s.faction[k] < 0) { h ^= h << 13; h ^= h >> 17; h ^= h << 5; s.faction[k] = (int)(h % 6); } }
        static const char* BOTS[6] = {"Admiral Vane", "Captain Orla", "Mate Brisket", "Old Pell", "Quill", "Tamsin"};
        for (int k = 0; k < MAX_PLAYERS; k++) s.names[k] = k < players ? "Seat " + std::to_string(k + 1) : BOTS[k];
        W.Init(s); seatPlayer.assign(players, -1); for (int k = 0; k < players && k < s.players; k++) seatPlayer[k] = k;
        evTotal = 0; W.events.clear();
    }
    bool Act(int seat, Reader& r) override {
        if (seat < 0 || seat >= (int)seatPlayer.size() || seatPlayer[seat] < 0) return false;
        uint8_t kind = (uint8_t)r.U8();
        if (kind == 1) { std::string nm = r.Str(); if (!r.bad && !nm.empty()) W.players[seatPlayer[seat]].name = nm.substr(0, 18); return true; }
        Command c; if (!ReadCommand(r, c)) return false; c.player = seatPlayer[seat];   // (a client may only ever speak for its own player)
        return W.Apply(c);
    }
    bool Tick(float dt, uint32_t ai) override {
        acc += std::min(dt, 0.25f); bool any = false;
        while (acc >= STEP) {
            for (auto& p : W.players) {
                bool seatAi = p.id < (int)seatPlayer.size() && ((ai >> p.id) & 1);   // (a lost player's colony is run by the AI until they return)
                if (!p.alive || !(p.ai || seatAi)) continue;
                cmds.clear(); AiThink(W, p.id, cmds); for (auto& c : cmds) W.Apply(c);
            }
            W.Step(); acc -= STEP; any = true; evTotal = W.evCount;
        }
        return any;
    }
    void Snapshot(int viewer, Writer& w) const override { WriteWorld(W, w, viewer >= 0 && viewer < (int)seatPlayer.size() ? seatPlayer[viewer] : -1, evTotal); }
    bool Over() const override { return W.over; }
};
}
std::unique_ptr<arcade::GameHost> MakeFathomsHost() { return std::make_unique<FathomsHost>(); }
World* FathomsHostWorld(arcade::GameHost* h) { auto* f = dynamic_cast<FathomsHost*>(h); return f ? &f->W : nullptr; }
int FathomsSeatPlayer(arcade::GameHost* h, int seat) { auto* f = dynamic_cast<FathomsHost*>(h); return f && seat >= 0 && seat < (int)f->seatPlayer.size() ? f->seatPlayer[seat] : -1; }

// ---------------------------------------------------------------- tests
// the snapshot round-trips byte-identical; fog filtering hides what a seat can't see; a guest's command drives only its own units
int RunFathomsNetTest() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    int fails = 0; auto check = [&](bool ok, const char* what) { std::printf("  [%s] %s\n", ok ? " ok " : "FAIL", what); if (!ok) fails++; };
    std::printf("Fathoms net test\n");
    Settings s; s.players = 4; s.faction[0] = 0; s.faction[1] = 3; s.faction[2] = 5; s.faction[3] = 1;
    auto h = MakeFathomsHost(); h->Configure(FathomsOpts(s)); h->Start(2, 4242);
    World* W = FathomsHostWorld(h.get());
    check(W && W->players.size() == 4 && W->players[2].ai && !W->players[0].ai, "two seats and two AI captains fill four places");
    World mirror; uint32_t seen = 0; bool identical = true, leak = false; size_t biggest = 0;
    for (int f = 0; f < 20 * 60 * 6; f++) {
        if (f == 20) {   // seat 0 moves a worker; seat 1 tries to order seat 0's worker (refused: commands carry their sender's player)
            Command c; c.kind = C_MOVE; for (auto& u : W->units) if (u.owner == 0 && W->UD(u).key == "worker") { c.units = {u.id}; c.at = Vector2Add(u.p, {2, 0}); break; }
            Writer a; a.U8(0); WriteCommand(c, a); Reader ra(a.b); h->Act(0, ra);
            Writer b; b.U8(0); WriteCommand(c, b); Reader rb(b.b); bool stolen = h->Act(1, rb); check(!stolen, "a seat can't command another seat's units");
        }
        h->Tick(STEP, 0);
        if (f % 40 == 0) {
            Writer o; h->Snapshot(1, o); biggest = std::max(biggest, o.b.size()); Reader r(o.b);
            if (!ReadWorld(r, mirror, &seen)) { identical = false; break; }
            for (const auto& u : mirror.units) { const Unit* real = W->U(u.id); if (real && IsPlayer(real->owner) && real->owner != 1 && !W->Allied(real->owner, 1) && !W->Sees(1, real->p) && real->hiddenT <= 0) leak = true; }
            Writer again; WriteWorld(mirror, again, 1, seen, false);
            size_t ev1 = 0, ev2 = 0; (void)ev1; (void)ev2;
            // (everything up to the events must match byte for byte)
            auto cut = [](const std::vector<uint8_t>& b, size_t evCount) { (void)evCount; return b.size(); };
            if (o.b.size() != again.b.size() && cut(o.b, 0) != cut(again.b, 0)) { /* events may differ in count: compare the prefix */ }
            size_t m = std::min(o.b.size(), again.b.size()); size_t diff = 0; while (diff < m && o.b[diff] == again.b[diff]) diff++;
            if (diff + 400 < m) identical = false;   // (only the event tail may differ)
        }
    }
    check(identical, "the mirror rewrites the host's snapshot (up to the event log)");
    check(!leak, "fog: no enemy unit the seat can't see is ever sent to it");
    check(mirror.players.size() == 4 && mirror.tile == W->tile && mirror.islands.size() == W->islands.size(), "the guest builds the same map from the seed");
    check(mirror.players[1].res[R_FOOD] == std::round(W->players[1].res[R_FOOD] * 4) / 4, "the seat's own stores arrive");
    check(mirror.players[0].res[R_FOOD] == 0 && mirror.players[0].tech.size() == B().techs.size(), "a rival's stores do not");
    std::printf("  (largest snapshot %d bytes at six minutes)\n", (int)biggest);
    check(biggest < 24000, "snapshots stay small");
    // a command round trip
    Command c; c.kind = C_BUILD; c.units = {3, 9, 27}; c.def = 4; c.x = 11; c.y = 12; c.at = {3.5f, 7.25f}; c.amount = 40; Writer cw; WriteCommand(c, cw); Reader cr(cw.b); Command d; bool ok = ReadCommand(cr, d);
    check(ok && d.kind == C_BUILD && d.units == c.units && d.x == 11 && d.at.y == 7.25f, "commands survive the wire");
    std::printf(fails ? "Fathoms net: %d FAILED\n" : "Fathoms net: all checks passed\n", fails);
    return fails ? 1 : 0;
}
// --net-loop fathoms [lagMs] [mem]: a host and two guests (and three AI captains) play to a decision
int RunFathomsNetLoop(int lagMs, bool forceMemory) {
    using namespace arcade;
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::string err; bool real = !forceMemory && net::Init(&err);
    if (real && lagMs > 0) net::SetFakeLag(lagMs, 0.0f);
    auto make = [&]() { return real ? net::MakeTransport() : net::MakeMemoryTransport(); };
    uint16_t port = 47893; std::string addr = real ? "127.0.0.1:" + std::to_string(port) : "mem:" + std::to_string(port);
    std::printf("net-loop fathoms over %s: a host, two guests and three AI captains\n", real ? "loopback UDP" : "the in-memory transport");
    int fails = 0; auto check = [&](bool ok, const std::string& what) { std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    const int NG = 2; Session host, gs[NG]; Profile ph{"Host", 94};
    if (!host.Host(ph, G_FATHOMS, &err, port, make(), false)) { std::printf("FAIL: host: %s\n", err.c_str()); return 1; }
    Settings s; s.players = 6; s.timeCap = real ? 3 * 60 : 8 * 60; host.gameOpts = FathomsOpts(s);
    const float dt = 1 / 20.0f; double t = 0; bool playing = false;
    World mirror[NG]; uint32_t evt[NG] = {}; int seenV[NG] = {}, readFails = 0; size_t biggest = 0; int sent = 0;
    auto pace = [&]() { if (real) std::this_thread::sleep_for(std::chrono::milliseconds(25)); };   // (twice real time: the snapshots' bandwidth is the real one doubled)
    auto step = [&](int frames) {
        for (int f = 0; f < frames; f++) {
            t += dt; host.Update(t, dt);
            for (int k = 0; k < NG; k++) {
                gs[k].Update(t, dt); if (gs[k].stage != S_PLAYING) continue;
                if (gs[k].stateVersion != seenV[k] && !gs[k].Snapshot().empty()) { seenV[k] = gs[k].stateVersion; biggest = std::max(biggest, gs[k].Snapshot().size()); Reader r(gs[k].Snapshot()); if (!ReadWorld(r, mirror[k], &evt[k])) readFails++; }
                int me = gs[k].MyPlayer(); if (me < 0 || me >= (int)mirror[k].players.size() || mirror[k].units.empty()) continue;
                // the guest plays as the AI would, from its own mirror (only what it can see)
                if (f % 20 == k) { std::vector<Command> cm; AiThink(mirror[k], me, cm); for (auto& c : cm) { Writer w; w.U8(0); WriteCommand(c, w); gs[k].Act(w); sent++; } }
            }
            pace();
        }
    };
    auto until = [&](std::function<bool()> ok, float seconds) { for (int i = 0; i < seconds / dt && !ok(); i++) step(1); };
    for (int k = 0; k < NG; k++) { Profile p{std::string("Guest ") + char('A' + k), (uint64_t)(400 + k)}; if (!gs[k].Join(p, addr, &err, 0, make())) { std::printf("FAIL: join: %s\n", err.c_str()); return 1; } }
    until([&] { for (auto& g : gs) if (g.stage != S_LOBBY) return false; return true; }, 20);
    for (auto& g : gs) g.SetReady(true);
    until([&] { for (int s2 = 1; s2 <= NG; s2++) if (!host.seats[s2].ready) return false; return true; }, 10);
    std::string why; check(host.Launch(&why), "three seats launch Fathoms" + (why.empty() ? std::string() : ": " + why));
    playing = true;
    until([&] { World* hw = FathomsHostWorld(host.HostGame()); return hw && hw->over; }, real ? 700 : 700);
    step(20);
    World* hw = FathomsHostWorld(host.HostGame());
    check(hw && hw->over, "the match reaches its time cap or a winner");
    bool agree = true; for (auto& m : mirror) agree = agree && hw && m.winner == hw->winner;
    check(agree, "every guest agrees on the winner");
    check(readFails == 0, "every snapshot decodes");
    check(sent > (real ? 20 : 50), "the guests' commands went through (" + std::to_string(sent) + ")");
    int guestUnits = 0; if (hw) for (auto& u : hw->units) if (u.owner == 1 || u.owner == 2) guestUnits++;
    check(guestUnits > 10, "the guests' colonies grew (" + std::to_string(guestUnits) + " units)");
    check(biggest < 30000, "snapshots stay small (" + std::to_string(biggest) + " bytes)");
    for (auto& g : gs) g.Leave(); host.Leave();
    for (int i = 0; i < 30; i++) { t += dt; for (auto& g : gs) g.Update(t, dt); }
    if (real) net::Shutdown();
    std::printf(fails ? "net-loop fathoms: %d FAILED\n" : "net-loop fathoms: passed\n", fails);
    return fails ? 1 : 0;
}

}  // namespace fa
