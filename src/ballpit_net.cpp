// Ball Pit Brawl's network host and snapshot (see ballpit_net.h).
#include "ballpit_net.h"
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

namespace bp {
namespace {
// one Visit for both directions; darts and balls go as centimetres in 16 bits (exact on a re-write)
struct Out {
    Writer& w;
    void f(float& v) { w.F32(v); } void i(int& v) { w.I32(v); } void u8(uint8_t& v) { w.U8(v); } void b(bool& v) { w.U8(v ? 1 : 0); } void s(std::string& v) { w.Str(v); }
    void v3(Vector3& v) { f(v.x); f(v.y); f(v.z); }
    void q(float& v) { int k = (int)lroundf(std::clamp(v, -327.0f, 327.0f) * 100); w.U16((uint16_t)(int16_t)k); }
    void q3(Vector3& v) { q(v.x); q(v.y); q(v.z); }
    void n(size_t k) { w.VarU((uint32_t)k); }
};
struct In {
    Reader& r;
    void f(float& v) { v = r.F32(); } void i(int& v) { v = r.I32(); } void u8(uint8_t& v) { v = (uint8_t)r.U8(); } void b(bool& v) { v = r.U8() != 0; } void s(std::string& v) { v = r.Str(); }
    void v3(Vector3& v) { f(v.x); f(v.y); f(v.z); }
    void q(float& v) { v = (int16_t)(uint16_t)r.U16() / 100.0f; }
    void q3(Vector3& v) { q(v.x); q(v.y); q(v.z); }
};
template <class IO> void VisitPlayer(IO& io, Player& p) {
    io.i(p.id); io.i(p.team); io.b(p.present); io.b(p.bot); io.s(p.name);
    io.v3(p.pos); io.v3(p.vel); io.f(p.yaw); io.f(p.pitch); io.b(p.grounded);
    uint8_t po = (uint8_t)p.po; io.u8(po); p.po = (Posture)po; io.f(p.poT); io.i(p.climb); io.i(p.slide); io.f(p.slideS);
    io.b(p.inPit); io.b(p.submerged); io.b(p.inTunnel); io.b(p.onBridge);
    io.f(p.hp); io.f(p.sinceHurt); io.f(p.respawnT); io.b(p.alive);
    io.i(p.gun); io.i(p.wield); io.i(p.mag[0]); io.i(p.mag[1]); io.i(p.reserve); io.f(p.cool); io.f(p.reloadT); io.f(p.spin); io.f(p.knifeSwing); io.b(p.vacuuming); io.f(p.swapT);
    io.i(p.ball); io.f(p.grabT); io.f(p.throwT); io.i(p.cannon); io.f(p.mountT); io.i(p.carry); io.b(p.bomb); io.b(p.kit); io.f(p.plantT); io.f(p.defuseT);
    io.i(p.score); io.i(p.cash); io.i(p.streak); io.i(p.kos); io.i(p.deaths); io.i(p.caps); io.i(p.rewardPick); io.u8(p.rewardReady); io.u8(p.rewardUsed);
    io.f(p.storeT); io.b(p.inStore); io.f(p.jokeCool); io.f(p.fingerT); io.b(p.crown); io.b(p.beanie); io.i(p.prizes); for (auto& j : p.jokes) io.u8(j); io.i(p.drive);
}
template <class IO> void VisitHead(IO& io, World& w) {
    io.i(w.mode); io.f(w.t); io.f(w.phaseT); uint8_t ph = (uint8_t)w.phase; io.u8(ph); w.phase = (Phase)ph;
    io.i(w.teamKOs[0]); io.i(w.teamKOs[1]); io.i(w.caps[0]); io.i(w.caps[1]); io.i(w.round); io.i(w.roundWins[0]); io.i(w.roundWins[1]); io.i(w.attackers); io.i(w.winner); io.i(w.roundWinner); io.i(w.jokeLine); io.b(w.noRespawn);
    for (auto& f : w.flags) { io.v3(f.home); io.v3(f.p); io.i(f.carrier); io.b(f.home_); io.f(f.dropT); }
    io.v3(w.bomb.p); io.i(w.bomb.carrier); io.b(w.bomb.planted); io.b(w.bomb.done); io.i(w.bomb.site); io.f(w.bomb.fuseT);
}
template <class IO> void VisitCannon(IO& io, Cannon& c) { io.i(c.op); io.i(c.hopper); io.f(c.heat); io.f(c.coolT); io.f(c.shotT); io.f(c.yaw); io.f(c.pitch); io.b(c.overheated); }
template <class IO> void VisitEnt(IO& io, Ent& e) { io.u8(e.kind); io.i(e.owner); io.i(e.team); io.q3(e.p); io.q3(e.v); io.f(e.yaw); io.f(e.hp); io.f(e.life); io.f(e.t); io.i(e.uses); io.f(e.armor); }
template <class IO> void VisitBall(IO& io, Ball& b) { io.q3(b.p); io.q3(b.v); uint8_t st = (uint8_t)b.st; io.u8(st); b.st = (BallState)st; io.i(b.holder); io.i(b.thrower); io.i(b.team); io.b(b.fromCannon); }
template <class IO> void VisitDart(IO& io, Dart& d) { io.q3(d.p); io.q3(d.v); io.i(d.owner); io.i(d.team); io.u8(d.kind); io.b(d.live); }
}

void WriteWorld(const World& cw, Writer& o, uint32_t evTotal, int viewer) {
    World& w = const_cast<World&>(cw); Out io{o};
    VisitHead(io, w);
    o.VarU((uint32_t)w.players.size()); for (auto& p : w.players) VisitPlayer(io, p);
    o.VarU((uint32_t)w.cannons.size()); for (auto& c : w.cannons) VisitCannon(io, c);
    o.VarU((uint32_t)w.arena.pits.size()); for (auto& p : w.arena.pits) o.VarU((uint32_t)p.balls);
    o.VarU((uint32_t)w.belt.arrive.size());
    o.VarU((uint32_t)w.balls.size()); for (auto& b : w.balls) VisitBall(io, b);
    o.VarU((uint32_t)w.ents.size()); for (auto& e : w.ents) VisitEnt(io, e);
    // darts: every live one; the floor's only near the viewer (the nearest 150)
    Vector3 at = viewer >= 0 && viewer < (int)w.players.size() ? w.players[viewer].pos : Vector3{0, 0, 0};
    std::vector<int> pick; for (int i = 0; i < (int)w.darts.size(); i++) if (w.darts[i].live || Vector3Distance(w.darts[i].p, at) < 28) pick.push_back(i);
    if (pick.size() > 220) pick.resize(220);
    o.VarU((uint32_t)pick.size()); for (int i : pick) VisitDart(io, w.darts[i]);
    int n = std::min<int>((int)w.events.size(), 24); o.U32(evTotal); o.VarU(n);
    for (int k = (int)w.events.size() - n; k < (int)w.events.size(); k++) { Event e = w.events[k]; o.U8(e.kind); io.v3(e.at); o.I32(e.who); o.I32(e.by); o.F32(e.a); }
}
bool ReadWorld(Reader& r, World& w, uint32_t* evTotal) {
    In io{r};
    if (w.arena.solids.empty()) { w.arena = MakeArena(); }
    VisitHead(io, w);
    uint32_t np = r.VarU(); if (np > MAX_PLAYERS) return false; w.players.resize(np); for (auto& p : w.players) VisitPlayer(io, p);
    uint32_t nc = r.VarU(); if (nc > 16) return false; w.cannons.resize(nc); for (auto& c : w.cannons) VisitCannon(io, c);
    uint32_t npit = r.VarU(); if (npit != w.arena.pits.size()) return false; for (auto& p : w.arena.pits) p.balls = (int)r.VarU();
    uint32_t belt = r.VarU(); if (belt > 10000) return false; w.belt.arrive.assign(belt, w.t + 4);
    uint32_t nb = r.VarU(); if (nb > 4000) return false; w.balls.resize(nb); for (auto& b : w.balls) VisitBall(io, b);
    uint32_t ne = r.VarU(); if (ne > 200) return false; w.ents.resize(ne); for (auto& e : w.ents) VisitEnt(io, e);
    uint32_t nd = r.VarU(); if (nd > 400) return false; w.darts.resize(nd); for (auto& d : w.darts) VisitDart(io, d);
    uint32_t total = r.U32(), n = r.VarU(); if (n > 24) return false;
    std::vector<Event> evs(n); for (auto& e : evs) { e.kind = (int)r.U8(); io.v3(e.at); e.who = r.I32(); e.by = r.I32(); e.a = r.F32(); }
    uint32_t had = evTotal ? *evTotal : 0; uint32_t first = total - n;
    for (uint32_t k = 0; k < n; k++) if (first + k >= had) w.Emit(evs[k].kind, evs[k].at, evs[k].who, evs[k].by, evs[k].a);
    if (evTotal) *evTotal = std::max(had, total);
    return !r.bad;
}

void WriteInput(const Input& in, Writer& w) {
    w.F32(in.moveX); w.F32(in.moveZ); w.F32(in.yaw); w.F32(in.pitch);
    uint32_t bits = (in.jump ? 1 : 0) | (in.crouch ? 2 : 0) | (in.fire ? 4 : 0) | (in.aim ? 8 : 0) | (in.knife ? 16 : 0) | (in.reload ? 32 : 0) | (in.use ? 64 : 0) | (in.vacuum ? 128 : 0) | (in.grab ? 256 : 0);
    w.U16(bits); w.U8((uint8_t)(in.slot + 1)); w.U16((uint16_t)(in.buy + 1)); w.U8((uint8_t)(in.joke + 1)); w.U8((uint8_t)(in.reward + 1)); w.U8((uint8_t)(in.pickRewards + 1));
}
void ReadInput(Reader& r, Input& in) {
    in.moveX = std::clamp(r.F32(), -1.0f, 1.0f); in.moveZ = std::clamp(r.F32(), -1.0f, 1.0f); in.yaw = r.F32(); in.pitch = r.F32();
    if (!std::isfinite(in.yaw)) in.yaw = 0; if (!std::isfinite(in.pitch)) in.pitch = 0; if (!std::isfinite(in.moveX)) in.moveX = 0; if (!std::isfinite(in.moveZ)) in.moveZ = 0;
    uint32_t b = r.U16();
    in.jump = b & 1; in.crouch = b & 2; in.fire = b & 4; in.aim = b & 8; in.knife = b & 16; in.reload = b & 32; in.use = b & 64; in.vacuum = b & 128; in.grab = b & 256;
    in.slot = (int)r.U8() - 1; in.buy = (int)r.U16() - 1; in.joke = (int)r.U8() - 1; in.reward = (int)r.U8() - 1; in.pickRewards = (int)r.U8() - 1;
    in.slot = std::clamp(in.slot, -1, 1); in.reward = std::clamp(in.reward, -1, 5); in.joke = std::clamp(in.joke, -1, 15);
}
uint32_t BallPitDataHash() {   // (the rules file: friends must play by the same numbers)
    std::ifstream f(rt::DataDir() + "/../ballpit/ballpit_config.json", std::ios::binary); uint32_t h = 2166136261u; char ch;
    while (f.get(ch)) if (ch != 13) { h ^= (uint8_t)ch; h *= 16777619u; }
    return h;
}
std::string BallPitOpts(int mode, int skill, int fill) { std::ostringstream s; s << mode << " " << skill << " " << fill; return s.str(); }

namespace {
class BallPitHost : public arcade::GameHost {
public:
    World W; int mode = MD_TDM, skill = 1, fill = 8; std::vector<int> seatPlayer; std::vector<Input> held, pressed; std::vector<uint32_t> rng;
    float acc = 0; uint32_t evTotal = 0;
    void Configure(const std::string& opts) override { std::istringstream s(opts); s >> mode >> skill >> fill; mode = std::clamp(mode, 0, MD_COUNT - 1); skill = std::clamp(skill, 0, 2); fill = std::clamp(fill, 2, 12); }
    void Start(int players, uint32_t seed) override {
        int n = std::clamp(std::max(players, fill), 2, mode == MD_BOMB ? 10 : 12);
        W.Init(mode, n, seed);
        static const char* BOTS[12] = {"Bosun", "Kess", "Marlow", "Pip", "Gully", "Rook", "Tamsin", "Ode", "Fen", "Bramble", "Skip", "Wren"};
        for (auto& p : W.players) { p.name = BOTS[p.id % 12]; p.bot = true; }
        seatPlayer.assign(players, -1);
        for (int s = 0; s < players && s < n; s++) { seatPlayer[s] = s; W.players[s].name = "Seat " + std::to_string(s + 1); W.players[s].bot = false; }
        held.assign(W.players.size(), Input{}); pressed.assign(W.players.size(), Input{}); rng.resize(W.players.size()); for (size_t i = 0; i < rng.size(); i++) rng[i] = seed * 31 + (uint32_t)i * 7919 + 1;
        evTotal = 0; W.events.clear();
    }
    bool Act(int player, Reader& r) override {
        if (player < 0 || player >= (int)seatPlayer.size() || seatPlayer[player] < 0) return false;
        uint8_t kind = (uint8_t)r.U8();
        if (kind == 1) { std::string nm = r.Str(); if (!r.bad && !nm.empty()) W.players[seatPlayer[player]].name = nm.substr(0, 16); return true; }
        Input in; ReadInput(r, in); if (r.bad) return false;
        int id = seatPlayer[player]; Input& pr = pressed[id];
        // (presses stick until a step has seen them)
        Input merged = in;
        merged.jump = pr.jump || in.jump; merged.knife = pr.knife || in.knife; merged.reload = pr.reload || in.reload; merged.use = pr.use || in.use; merged.grab = pr.grab || in.grab;
        if (in.slot < 0) merged.slot = pr.slot; if (in.buy < 0) merged.buy = pr.buy; if (in.joke < 0) merged.joke = pr.joke; if (in.reward < 0) merged.reward = pr.reward; if (in.pickRewards < 0) merged.pickRewards = pr.pickRewards;
        held[id] = in; pr = merged;
        return false;
    }
    bool Tick(float dt, uint32_t ai) override {
        acc += std::min(dt, 0.1f); bool any = false;
        while (acc >= STEP) {
            for (auto& p : W.players) {
                int seat = p.id < (int)seatPlayer.size() && seatPlayer[p.id] == p.id ? p.id : -1;
                if (seat < 0 || (ai >> seat) & 1) { p.bot = true; BotInput(W, p.id, p.in, rng[p.id], skill); continue; }
                p.bot = false;
                p.in = pressed[p.id]; pressed[p.id] = held[p.id];
                Input& h = pressed[p.id]; h.jump = h.knife = h.reload = h.use = h.grab = false; h.slot = h.buy = h.joke = h.reward = h.pickRewards = -1;
            }
            W.Step(); acc -= STEP; any = true; evTotal = W.evCount;
        }
        return any;
    }
    void Snapshot(int viewer, Writer& w) const override { WriteWorld(W, w, evTotal, viewer >= 0 && viewer < (int)seatPlayer.size() ? seatPlayer[viewer] : -1); }
    bool Over() const override { return W.phase == PH_OVER; }
};
}
std::unique_ptr<arcade::GameHost> MakeBallPitHost() { return std::make_unique<BallPitHost>(); }
World* BallPitHostWorld(arcade::GameHost* h) { auto* w = dynamic_cast<BallPitHost*>(h); return w ? &w->W : nullptr; }
int BallPitSeatPlayer(arcade::GameHost* h, int seat) { auto* w = dynamic_cast<BallPitHost*>(h); return w && seat >= 0 && seat < (int)w->seatPlayer.size() ? w->seatPlayer[seat] : -1; }

// the snapshot round-trips byte-identical, a guest's inputs drive its player, events arrive once
int RunBallPitNetTest() {
    int fails = 0; auto check = [&](bool ok, const char* what) { std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what); if (!ok) fails++; };
    std::printf("Ball Pit Brawl net test\n");
    auto h = MakeBallPitHost(); h->Configure(BallPitOpts(MD_TDM, 1, 8)); h->Start(3, 77);
    World* W = BallPitHostWorld(h.get());
    check(W && W->players.size() == 8 && BallPitSeatPlayer(h.get(), 2) == 2, "three seats and five bots fill eight places");
    World mirror; uint32_t seen = 0; bool identical = true; size_t biggest = 0; Vector3 start = W->players[0].pos;
    for (int f = 0; f < 60 * 40; f++) {
        Writer a; a.U8(0); Input in; in.moveX = 1; in.yaw = W->players[0].team == 0 ? 0 : PI; in.fire = (f / 30) % 2 == 0; WriteInput(in, a); Reader ra(a.b); h->Act(0, ra);
        h->Tick(1 / 60.0f, 0b110);
        if (f % 3 == 0) {
            Writer s; h->Snapshot(0, s); Reader rs(s.b); biggest = std::max(biggest, s.b.size());
            if (!ReadWorld(rs, mirror, &seen)) { identical = false; break; }
            Writer again; WriteWorld(mirror, again, seen, 0);
            size_t evBytes = [&](const World& w) { return 5 + std::min<size_t>(w.events.size(), 24) * 25; }(*W), evBytes2 = 5 + std::min<size_t>(mirror.events.size(), 24) * 25;
            size_t a1 = s.b.size() - evBytes, a2 = again.b.size() - evBytes2;
            if (a1 != a2 || !std::equal(s.b.begin(), s.b.begin() + a1, again.b.begin())) identical = false;
        }
    }
    check(identical, "the mirror rewrites the host's snapshot byte for byte");
    check(Vector3Distance(W->players[0].pos, start) > 2 || W->players[0].deaths > 0, "seat 0's input moved its player");
    check(mirror.players.size() == W->players.size() && mirror.cannons.size() == W->cannons.size(), "the mirror has every player and cannon");
    std::printf("  (largest snapshot %d bytes)\n", (int)biggest);
    check(biggest < 9000, "snapshots stay small enough for 20 a second");
    std::printf(fails ? "Ball Pit net: %d FAILED\n" : "Ball Pit net: all checks passed\n", fails);
    return fails ? 1 : 0;
}
// --net-loop ballpit [lagMs] [mem]: a host and three guests play a short team deathmatch over the session
int RunBallPitNetLoop(int lagMs, bool forceMemory) {
    using namespace arcade;
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::string err; bool real = !forceMemory && net::Init(&err);
    if (real && lagMs > 0) net::SetFakeLag(lagMs, 0.0f);
    auto make = [&]() { return real ? net::MakeTransport() : net::MakeMemoryTransport(); };
    uint16_t port = 47890; std::string addr = real ? "127.0.0.1:" + std::to_string(port) : "mem:" + std::to_string(port);
    std::printf("net-loop ballpit over %s: a host and three guests\n", real ? "loopback UDP" : "the in-memory transport");
    int fails = 0; auto check = [&](bool ok, const std::string& what) { std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    const int NG = 3; Session host, gs[NG]; Profile ph{"Host", 93};
    if (!host.Host(ph, G_BALLPIT, &err, port, make(), false)) { std::printf("FAIL: host: %s\n", err.c_str()); return 1; }
    host.gameOpts = BallPitOpts(MD_TDM, 1, 6);
    Config& cm = CfgMutable(); ModeDef keep = cm.modes[MD_TDM]; cm.modes[MD_TDM].toWin = 10;
    const float dt = 1 / 60.0f; double t = 0; bool playing = false;
    World mirror[NG]; uint32_t evt[NG] = {}; int seenV[NG] = {}; int readFails = 0; size_t biggest = 0; uint32_t brng[NG] = {7, 8, 9}, hrng = 3;
    auto pace = [&]() { if (real) std::this_thread::sleep_for(std::chrono::milliseconds(16)); };
    auto step = [&](int frames) {
        for (int f = 0; f < frames; f++) {
            t += dt; host.Update(t, dt);
            if (playing && host.stage == S_PLAYING) { World* hw = BallPitHostWorld(host.HostGame()); int me = BallPitSeatPlayer(host.HostGame(), host.MyPlayer()); if (hw && me >= 0) { Input in; BotInput(*hw, me, in, hrng, 1); Writer w; w.U8(0); WriteInput(in, w); host.Act(w); } }
            for (int k = 0; k < NG; k++) {
                gs[k].Update(t, dt); if (gs[k].stage != S_PLAYING) continue;
                if (gs[k].stateVersion != seenV[k] && !gs[k].Snapshot().empty()) { seenV[k] = gs[k].stateVersion; biggest = std::max(biggest, gs[k].Snapshot().size()); Reader r(gs[k].Snapshot()); if (!ReadWorld(r, mirror[k], &evt[k])) readFails++; }
                int me = gs[k].MyPlayer(); if (me < 0 || me >= (int)mirror[k].players.size()) continue;
                Input in; BotInput(mirror[k], me, in, brng[k], 1); Writer w; w.U8(0); WriteInput(in, w); gs[k].Act(w);
            }
            pace();
        }
    };
    auto until = [&](std::function<bool()> ok, float seconds) { for (int i = 0; i < seconds / dt && !ok(); i++) step(1); };
    for (int k = 0; k < NG; k++) { Profile p{std::string("Guest ") + char('A' + k), (uint64_t)(300 + k)}; if (!gs[k].Join(p, addr, &err, 0, make())) { std::printf("FAIL: join: %s\n", err.c_str()); return 1; } }
    until([&] { for (auto& g : gs) if (g.stage != S_LOBBY) return false; return true; }, 20);
    for (auto& g : gs) g.SetReady(true);
    until([&] { for (int s = 1; s <= NG; s++) if (!host.seats[s].ready) return false; return true; }, 10);
    std::string why; check(host.Launch(&why), "four players launch Ball Pit Brawl" + (why.empty() ? std::string() : ": " + why));
    playing = true;
    until([&] { World* hw = BallPitHostWorld(host.HostGame()); return hw && hw->phase == PH_OVER; }, real ? 600 : 1500);
    step(30);
    World* hw = BallPitHostWorld(host.HostGame());
    check(hw && hw->phase == PH_OVER, "the match plays to a winner");
    bool agree = true; for (auto& m : mirror) agree = agree && hw && m.teamKOs[0] == hw->teamKOs[0] && m.teamKOs[1] == hw->teamKOs[1] && m.winner == hw->winner;
    check(agree, "every guest agrees on the score");
    check(readFails == 0, "every snapshot decodes");
    check(biggest < 12000, "snapshots stay small (" + std::to_string(biggest) + " bytes)");
    cm.modes[MD_TDM] = keep;
    for (auto& g : gs) g.Leave(); host.Leave();
    for (int i = 0; i < 30; i++) { t += dt; for (auto& g : gs) g.Update(t, dt); }
    if (real) net::Shutdown();
    std::printf(fails ? "net-loop ballpit: %d FAILED\n" : "net-loop ballpit: passed\n", fails);
    return fails ? 1 : 0;
}
}
