// Warp Dodgeball's network host and snapshot (see warp_net.h).
#include "warp_net.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <fstream>
#include "redtide.h"
#include "arcade_session.h"
#include "net.h"
#include <chrono>
#include <functional>
#include <thread>

namespace wd {
namespace {
// one Visit for both directions
struct Out { Writer& w; void f(float& v) { w.F32(v); } void i(int& v) { w.I32(v); } void u8(uint8_t& v) { w.U8(v); } void b(bool& v) { w.U8(v ? 1 : 0); } void s(std::string& v) { w.Str(v); } void v3(Vector3& v) { f(v.x); f(v.y); f(v.z); } void n(size_t k) { w.VarU((uint32_t)k); } };
struct In { Reader& r; void f(float& v) { v = r.F32(); } void i(int& v) { v = r.I32(); } void u8(uint8_t& v) { v = (uint8_t)r.U8(); } void b(bool& v) { v = r.U8() != 0; } void s(std::string& v) { v = r.Str(); } void v3(Vector3& v) { f(v.x); f(v.y); f(v.z); } };
template <class IO> void VisitPlayer(IO& io, Player& p) {
    io.i(p.id); io.i(p.team); io.b(p.alive); io.b(p.present); io.s(p.name);
    io.v3(p.pos); io.v3(p.vel); io.f(p.yaw); io.f(p.pitch); io.b(p.grounded);
    uint8_t po = (uint8_t)p.po; io.u8(po); p.po = (Posture)po; io.f(p.poT); io.f(p.squatCool); io.v3(p.diveDir); io.i(p.ladder);
    io.i(p.ball); io.f(p.charge); io.f(p.heldT); io.f(p.overT); io.f(p.releaseT); io.b(p.charging); io.f(p.relCurve);
    for (auto& q : p.portal) { io.b(q.on); io.v3(q.c); io.v3(q.n); io.v3(q.u); io.f(q.cool); }
    io.f(p.catchPressT); io.i(p.pendingHit); io.f(p.pendingT); io.i(p.outs); io.i(p.catches); io.i(p.sidelineOrder); io.s(p.outCause);
}
template <class IO> void VisitBall(IO& io, Ball& b) {
    io.v3(b.p); io.v3(b.v); io.v3(b.spin); uint8_t st = (uint8_t)b.st; io.u8(st); b.st = (BallState)st;
    io.i(b.holder); io.i(b.thrower); io.i(b.team); io.b(b.viaPortal); io.b(b.viaWall); io.b(b.mustCarry); io.f(b.age); io.f(b.ignoreT);
}
template <class IO> void VisitHead(IO& io, World& w) {
    io.f(w.t); uint8_t ph = (uint8_t)w.phase; io.u8(ph); w.phase = (Phase)ph; io.f(w.phaseT);
    io.i(w.round); io.i(w.wins[0]); io.i(w.wins[1]); io.i(w.roundWinner); io.i(w.champion); io.b(w.suddenDeath);
}
}

void WriteWorld(const World& cw, Writer& o, uint32_t evTotal) {
    World& w = const_cast<World&>(cw); Out io{o};
    o.U8(w.arena.kind); VisitHead(io, w);
    o.VarU((uint32_t)w.players.size()); for (auto& p : w.players) VisitPlayer(io, p);
    o.VarU((uint32_t)w.balls.size()); for (auto& b : w.balls) VisitBall(io, b);
    for (int s = 0; s < 2; s++) { o.VarU((uint32_t)w.sideline[s].size()); for (int id : w.sideline[s]) o.VarU(id); }
    // the newest events (a guest keeps those past its own count)
    int n = std::min<int>((int)w.events.size(), 12); o.U32(evTotal); o.VarU(n);
    for (int k = (int)w.events.size() - n; k < (int)w.events.size(); k++) { Event e = w.events[k]; o.U8(e.kind); io.v3(e.at); o.I32(e.who); o.I32(e.by); o.F32(e.a); }
}
bool ReadWorld(Reader& r, World& w, uint32_t* evTotal) {
    In io{r};
    int kind = (int)r.U8(); if (w.arena.panels.empty() || w.arena.kind != kind) w.arena = MakeArena(kind);
    VisitHead(io, w);
    uint32_t np = r.VarU(); if (np > MAX_PLAYERS) return false; w.players.resize(np); for (auto& p : w.players) VisitPlayer(io, p);
    uint32_t nb = r.VarU(); if (nb > 64) return false; w.balls.resize(nb); for (auto& b : w.balls) VisitBall(io, b);
    for (int s = 0; s < 2; s++) { uint32_t k = r.VarU(); if (k > MAX_PLAYERS) return false; w.sideline[s].resize(k); for (auto& id : w.sideline[s]) id = (int)r.VarU(); }
    uint32_t total = r.U32(), n = r.VarU(); if (n > 12) return false;
    std::vector<Event> evs(n); for (auto& e : evs) { e.kind = (int)r.U8(); io.v3(e.at); e.who = r.I32(); e.by = r.I32(); e.a = r.F32(); }
    // keep only the events this mirror hasn't had yet, appended to its own log
    uint32_t had = evTotal ? *evTotal : 0; uint32_t first = total - n;
    for (uint32_t k = 0; k < n; k++) if (first + k >= had) w.Emit(evs[k].kind, evs[k].at, evs[k].who, evs[k].by, evs[k].a);
    if (evTotal) *evTotal = std::max(had, total);
    return !r.bad;
}

void WriteInput(const Input& in, Writer& w) {
    w.F32(in.moveX); w.F32(in.moveZ); w.F32(in.yaw); w.F32(in.pitch); w.F32(in.curve);
    uint32_t bits = (in.sprint ? 1 : 0) | (in.jump ? 2 : 0) | (in.crouch ? 4 : 0) | (in.squat ? 8 : 0) | (in.dive ? 16 : 0) | (in.throwHeld ? 32 : 0) | (in.cancel ? 64 : 0) | (in.portalA ? 128 : 0) | (in.portalB ? 256 : 0) | (in.catchP ? 512 : 0);
    w.U16(bits);
}
void ReadInput(Reader& r, Input& in) {
    in.moveX = std::clamp(r.F32(), -1.0f, 1.0f); in.moveZ = std::clamp(r.F32(), -1.0f, 1.0f); in.yaw = r.F32(); in.pitch = r.F32(); in.curve = std::clamp(r.F32(), -1.0f, 1.0f);
    if (!std::isfinite(in.yaw)) in.yaw = 0; if (!std::isfinite(in.pitch)) in.pitch = 0;
    uint32_t b = r.U16();
    in.sprint = b & 1; in.jump = b & 2; in.crouch = b & 4; in.squat = b & 8; in.dive = b & 16; in.throwHeld = b & 32; in.cancel = b & 64; in.portalA = b & 128; in.portalB = b & 256; in.catchP = b & 512;
}
uint32_t WarpDataHash() {   // (the rules file: friends must play by the same numbers)
    std::ifstream f(rt::DataDir() + "/../warp/warp_config.json", std::ios::binary); uint32_t h = 2166136261u; char ch;
    while (f.get(ch)) if (ch != 13) { h ^= (uint8_t)ch; h *= 16777619u; }
    return h;
}
std::string WarpOpts(int arena, int skill, int fill) { std::ostringstream s; s << arena << " " << skill << " " << fill; return s.str(); }

namespace {
class WarpHost : public arcade::GameHost {
public:
    World W; int arena = AR_CLASSIC, skill = 1, fill = 4, seats = 0; std::vector<int> seatPlayer; std::vector<Input> held, pressed; std::vector<uint32_t> rng;
    float acc = 0; uint32_t evTotal = 0; size_t evSeen = 0;
    void Configure(const std::string& opts) override { std::istringstream s(opts); s >> arena >> skill >> fill; arena = std::clamp(arena, 0, 1); skill = std::clamp(skill, 0, 2); fill = std::clamp(fill, 1, 6); }
    void Start(int players, uint32_t seed) override {
        seats = players; int per = std::clamp(std::max((players + 1) / 2, fill), 1, 6);
        W.Init(arena, per, seed);
        static const char* BOTS[12] = {"Bosun", "Kess", "Marlow", "Pip", "Gully", "Rook", "Tamsin", "Ode", "Fen", "Bramble", "Skip", "Wren"};
        for (auto& p : W.players) p.name = BOTS[p.id % 12];
        seatPlayer.assign(players, -1);
        for (int s = 0; s < players; s++) { int team = s % 2, idx = s / 2; if (idx < per) seatPlayer[s] = team * per + idx; }
        for (int s = 0; s < players; s++) if (seatPlayer[s] >= 0) W.players[seatPlayer[s]].name = "Seat " + std::to_string(s + 1);
        held.assign(W.players.size(), Input{}); pressed.assign(W.players.size(), Input{}); rng.resize(W.players.size()); for (size_t i = 0; i < rng.size(); i++) rng[i] = seed * 31 + (uint32_t)i * 7919 + 1;
        evTotal = 0; evSeen = 0; W.events.clear();
    }
    bool Act(int player, Reader& r) override {
        if (player < 0 || player >= (int)seatPlayer.size() || seatPlayer[player] < 0) return false;
        uint8_t kind = (uint8_t)r.U8();
        if (kind == 1) { std::string nm = r.Str(); if (!r.bad && !nm.empty()) W.players[seatPlayer[player]].name = nm.substr(0, 16); return true; }
        Input in; ReadInput(r, in); if (r.bad) return false;
        int id = seatPlayer[player]; Input& pr = pressed[id];
        bool j = pr.jump || in.jump, sq = pr.squat || in.squat, dv = pr.dive || in.dive, pa = pr.portalA || in.portalA, pb = pr.portalB || in.portalB, cp = pr.catchP || in.catchP;
        held[id] = in; pr = in; pr.jump = j; pr.squat = sq; pr.dive = dv; pr.portalA = pa; pr.portalB = pb; pr.catchP = cp;
        return false;
    }
    bool Tick(float dt, uint32_t ai) override {
        acc += std::min(dt, 0.1f); bool any = false;
        while (acc >= STEP) {
            for (auto& p : W.players) {
                int seat = -1; for (int s = 0; s < (int)seatPlayer.size(); s++) if (seatPlayer[s] == p.id) seat = s;
                if (seat < 0 || (ai >> seat) & 1) { BotInput(W, p.id, p.in, rng[p.id], skill); continue; }
                p.in = pressed[p.id]; pressed[p.id] = held[p.id]; pressed[p.id].jump = pressed[p.id].squat = pressed[p.id].dive = pressed[p.id].portalA = pressed[p.id].portalB = pressed[p.id].catchP = false;
            }
            W.Step(); acc -= STEP; any = true; evTotal = W.evCount;
        }
        return any;
    }
    void Snapshot(int viewer, Writer& w) const override { (void)viewer; WriteWorld(W, w, evTotal); }
    bool Over() const override { return W.phase == PH_MATCH_END; }
};
}
std::unique_ptr<arcade::GameHost> MakeWarpHost() { return std::make_unique<WarpHost>(); }
World* WarpHostWorld(arcade::GameHost* h) { auto* w = dynamic_cast<WarpHost*>(h); return w ? &w->W : nullptr; }
int WarpSeatPlayer(arcade::GameHost* h, int seat) { auto* w = dynamic_cast<WarpHost*>(h); return w && seat >= 0 && seat < (int)w->seatPlayer.size() ? w->seatPlayer[seat] : -1; }

// the snapshot round-trips byte-identical, a guest's inputs drive its player, events arrive once
int RunWarpNetTest() {
    int fails = 0; auto check = [&](bool ok, const char* what) { std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what); if (!ok) fails++; };
    std::printf("Warp Dodgeball net test\n");
    auto h = MakeWarpHost(); h->Configure(WarpOpts(0, 1, 3)); h->Start(4, 77);
    World* W = WarpHostWorld(h.get());
    check(W && W->players.size() == 6 && WarpSeatPlayer(h.get(), 0) == 0 && WarpSeatPlayer(h.get(), 1) == 3, "seats alternate teams, empty places are bots (3 a side for 4 seats)");
    World mirror; uint32_t seen = 0; size_t mirrorEvents = 0; bool identical = true;
    for (int f = 0; f < 60 * 40; f++) {
        // seat 0 walks forward and throws when it holds a ball; seat 1..3 are AI
        Writer a; a.U8(0); Input in; in.moveX = 1; in.yaw = 0; in.throwHeld = (f / 40) % 2 == 0; WriteInput(in, a); Reader ra(a.b); h->Act(0, ra);
        h->Tick(1 / 60.0f, 0b1110);
        if (f % 2 == 0) {
            Writer s; h->Snapshot(0, s); Reader rs(s.b);
            if (!ReadWorld(rs, mirror, &seen)) { identical = false; break; }
            Writer again; WriteWorld(mirror, again, seen);
            // (the events block differs: each log keeps its own newest; the state before it must match byte for byte)
            size_t a1 = s.b.size() - (5 + std::min<size_t>(W->events.size(), 12) * 25), a2 = again.b.size() - (5 + std::min<size_t>(mirror.events.size(), 12) * 25);
            if (a1 != a2 || !std::equal(s.b.begin(), s.b.begin() + a1, again.b.begin())) identical = false;
            mirrorEvents = mirror.events.size();
        }
    }
    check(identical, "the mirror rewrites the host's snapshot byte for byte");
    check(W->players[0].pos.x > -8.5f || !W->players[0].alive || W->round > 1, "seat 0's input moved its player");
    check(mirrorEvents > 0 && seen > 0, "events reach the mirror");
    // each event arrives exactly once: count throws on host and mirror over a fresh run
    auto h2 = MakeWarpHost(); h2->Configure(WarpOpts(1, 2, 2)); h2->Start(2, 5); World m2; uint32_t s2 = 0; int hostThrows = 0, mirThrows = 0;
    World* W2 = WarpHostWorld(h2.get()); size_t cur = 0;
    for (int f = 0; f < 60 * 30; f++) {
        // (count by each log's running total: the logs trim themselves from the front)
        auto fresh = [](const World& w, uint32_t& last, int& throws) { uint32_t n = w.evCount - last; last = w.evCount; for (size_t k = w.events.size() - std::min<size_t>(n, w.events.size()); k < w.events.size(); k++) if (w.events[k].kind == EV_THROW) throws++; };
        static uint32_t lh = 0, lm = 0; if (f == 0) lh = lm = 0;
        h2->Tick(1 / 60.0f, 0b11); fresh(*W2, lh, hostThrows);
        if (f % 2 == 0) { Writer s; h2->Snapshot(0, s); Reader rs(s.b); ReadWorld(rs, m2, &s2); fresh(m2, lm, mirThrows); }
        (void)cur;
    }
    check(hostThrows > 0 && hostThrows == mirThrows, "every throw reaches the mirror exactly once");
    std::printf("  (host throws %d, mirror %d)\n", hostThrows, mirThrows);
    std::printf(fails ? "Warp net: %d FAILED\n" : "Warp net: all checks passed\n", fails);
    return fails ? 1 : 0;
}
// --net-loop warp [lagMs] [mem]: a host and three guests play a whole match over the session (loopback UDP, or the
// in-memory transport); every guest's mind is a bot reading its own mirror and sending Inputs like a person would
int RunWarpNetLoop(int lagMs, bool forceMemory) {
    using namespace arcade;
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::string err; bool real = !forceMemory && net::Init(&err);
    if (real && lagMs > 0) net::SetFakeLag(lagMs, 0.0f);
    auto make = [&]() { return real ? net::MakeTransport() : net::MakeMemoryTransport(); };
    uint16_t port = 47880; std::string addr = real ? "127.0.0.1:" + std::to_string(port) : "mem:" + std::to_string(port);
    std::printf("net-loop warp over %s: a host and three guests, best of five\n", real ? "loopback UDP" : "the in-memory transport");
    int fails = 0; auto check = [&](bool ok, const std::string& what) { std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    const int NG = 3; Session host, gs[NG]; Profile ph{"Host", 91};
    if (!host.Host(ph, G_WARP, &err, port, make(), false)) { std::printf("FAIL: host: %s\n", err.c_str()); return 1; }
    host.gameOpts = WarpOpts(0, 1, 2);
    const float dt = 1 / 60.0f; double t = 0; bool playing = false;
    World mirror[NG]; uint32_t evt[NG] = {}; int seenV[NG] = {}; int readFails = 0; size_t biggest = 0; uint32_t brng[NG] = {7, 8, 9}, hrng = 3;
    auto pace = [&]() { if (real) std::this_thread::sleep_for(std::chrono::milliseconds(16)); };
    auto mySeatPlayer = [&](const World& w, int seat) { int per = (int)w.players.size() / 2; return per > 0 && seat >= 0 && seat / 2 < per ? (seat % 2) * per + seat / 2 : -1; };
    auto step = [&](int frames) {
        for (int f = 0; f < frames; f++) {
            t += dt; host.Update(t, dt);
            if (playing && host.stage == S_PLAYING) { World* hw = WarpHostWorld(host.HostGame()); int me = WarpSeatPlayer(host.HostGame(), host.MyPlayer()); if (hw && me >= 0) { Input in; BotInput(*hw, me, in, hrng, 1); Writer w; w.U8(0); WriteInput(in, w); host.Act(w); } }
            for (int k = 0; k < NG; k++) {
                gs[k].Update(t, dt); if (gs[k].stage != S_PLAYING) continue;
                if (gs[k].stateVersion != seenV[k] && !gs[k].Snapshot().empty()) { seenV[k] = gs[k].stateVersion; biggest = std::max(biggest, gs[k].Snapshot().size()); Reader r(gs[k].Snapshot()); if (!ReadWorld(r, mirror[k], &evt[k])) readFails++; }
                int me = mySeatPlayer(mirror[k], gs[k].MyPlayer()); if (me < 0) continue;
                Input in; BotInput(mirror[k], me, in, brng[k], 1); Writer w; w.U8(0); WriteInput(in, w); gs[k].Act(w);
            }
            pace();
        }
    };
    auto until = [&](std::function<bool()> ok, float seconds) { for (int i = 0; i < seconds / dt && !ok(); i++) step(1); };
    for (int k = 0; k < NG; k++) { Profile p{std::string("Guest ") + char('A' + k), (uint64_t)(200 + k)}; if (!gs[k].Join(p, addr, &err, 0, make())) { std::printf("FAIL: join: %s\n", err.c_str()); return 1; } }
    until([&] { for (auto& g : gs) if (g.stage != S_LOBBY) return false; return true; }, 20);
    for (auto& g : gs) g.SetReady(true);
    until([&] { for (int s = 1; s <= NG; s++) if (!host.seats[s].ready) return false; return true; }, 10);
    std::string why; check(host.Launch(&why), "four players launch Warp Dodgeball" + (why.empty() ? std::string() : ": " + why));
    playing = true;
    until([&] { World* hw = WarpHostWorld(host.HostGame()); return hw && hw->phase == PH_MATCH_END; }, real ? 600 : 1500);
    step(30);
    World* hw = WarpHostWorld(host.HostGame());
    check(hw && hw->phase == PH_MATCH_END && hw->champion >= 0, "the match plays to a winner");
    bool agree = true; for (auto& m : mirror) agree = agree && hw && m.wins[0] == hw->wins[0] && m.wins[1] == hw->wins[1] && m.champion == hw->champion;
    check(agree, "every guest agrees on the score");
    check(readFails == 0, "every snapshot decodes");
    check(biggest < 2000, "snapshots stay small (" + std::to_string(biggest) + " bytes at 30 Hz)");
    for (auto& g : gs) g.Leave(); host.Leave();
    for (int i = 0; i < 30; i++) { t += dt; for (auto& g : gs) g.Update(t, dt); }
    if (real) net::Shutdown();
    std::printf(fails ? "net-loop warp: %d FAILED\n" : "net-loop warp: passed\n", fails);
    return fails ? 1 : 0;
}
}