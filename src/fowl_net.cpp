// Fowl Play's network host and snapshot (see fowl_net.h).
#include "fowl_net.h"
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

namespace fp {
namespace {
struct Out { Writer& w; void f(float& v) { w.F32(v); } void i(int& v) { w.I32(v); } void u8(uint8_t& v) { w.U8(v); } void u32(uint32_t& v) { w.U32(v); } void b(bool& v) { w.U8(v ? 1 : 0); } void s(std::string& v) { w.Str(v); } void v3(Vector3& v) { f(v.x); f(v.y); f(v.z); } size_t n(size_t k) { w.VarU((uint32_t)k); return k; } bool reading() const { return false; } };
struct In { Reader& r; void f(float& v) { v = r.F32(); } void i(int& v) { v = r.I32(); } void u8(uint8_t& v) { v = (uint8_t)r.U8(); } void u32(uint32_t& v) { v = r.U32(); } void b(bool& v) { v = r.U8() != 0; } void s(std::string& v) { v = r.Str(); } void v3(Vector3& v) { f(v.x); f(v.y); f(v.z); } size_t n(size_t) { uint32_t k = r.VarU(); return k > 512 ? 0 : k; } bool reading() const { return true; } };
template <class IO> void VGun(IO& io, Gun& g) { io.i(g.def); for (int& a : g.att) io.i(a); io.i(g.ammo); io.i(g.reserve); io.f(g.cool); io.f(g.reloadT); io.f(g.spin); io.i(g.shotN); io.i(g.paint); }
template <class IO> void VSab(IO& io, std::vector<Sabotage>& v) { size_t k = io.n(v.size()); if (io.reading()) v.resize(k); for (auto& s : v) { io.i(s.item); io.i(s.by); io.b(s.countered); } }
template <class IO> void VPlayer(IO& io, Player& p) {
    io.i(p.id); io.i(p.stall); io.i(p.team); io.b(p.present); io.b(p.bot); io.s(p.name);
    io.v3(p.pos); io.f(p.yaw); io.f(p.pitch); io.f(p.crouchK); io.f(p.lean); io.i(p.room);
    VGun(io, p.guns[0]); VGun(io, p.guns[1]); io.i(p.hand); VGun(io, p.hook); io.b(p.lockbox);
    io.i(p.money); io.i(p.birds); io.i(p.shots); io.i(p.hits); io.i(p.roundBirds); io.i(p.roundMoney); io.i(p.roundSpent); io.f(p.roundPay); io.i(p.grudgeMoney);
    { size_t k = io.n(p.best.size()); if (io.reading()) p.best.resize(k); for (int& x : p.best) io.i(x); }
    io.i(p.hat); io.i(p.paint); io.i(p.dance); io.i(p.flag); io.i(p.dogCoat); io.i(p.killSound); io.i(p.tracer); io.b(p.hatOff); io.f(p.hatOffT);
    io.u32(p.counters); VSab(io, p.incoming); VSab(io, p.active);
    io.f(p.beesT); io.f(p.smudgeT); io.f(p.wipeT); io.f(p.reverseT); io.f(p.pepperT); io.f(p.repelT); io.f(p.cardboardT); io.f(p.gunDropT); io.f(p.jamT);
    io.i(p.cardboardHP); io.i(p.dropsLeft); io.i(p.rubberLeft); io.f(p.nextDropT);
    io.b(p.bagpipe); io.b(p.grudge); io.b(p.glitter); io.i(p.taxBy); io.i(p.swapWith); io.b(p.menace); io.u32(p.sabotaged);
    io.b(p.dogSausage); io.i(p.dogFirst); io.b(p.dogShot);
    for (int& x : p.scratchPocket) io.i(x); io.i(p.scratchOpen); io.f(p.scratchT);
    io.f(p.slotT); io.i(p.slotBet); for (int& x : p.reels) io.i(x); io.i(p.slotWin);
    io.i(p.deal); io.b(p.dealIsAtt); io.b(p.boughtThisInter); io.b(p.mysteryFree); io.i(p.pendingCapsule);
    io.i(p.perfect); io.i(p.ufoKills); io.i(p.mostExpensive); io.i(p.mostExpensiveBirds); io.i(p.gambleWon); io.i(p.gambleLost); io.i(p.sabGiven); io.i(p.sabTaken);
    io.b(p.inBonus); io.b(p.lastFire);
}
template <class IO> void VBird(IO& io, Bird& b) {
    io.i(b.def); io.i(b.id); io.i(b.wave); uint8_t st = (uint8_t)b.st; io.u8(st); b.st = (BirdState)st; io.v3(b.p); io.v3(b.v); io.f(b.t); io.f(b.life); io.f(b.hp); io.f(b.freezeT); io.f(b.seed);
    io.i(b.killer); io.i(b.gunOf); io.b(b.escaped); io.b(b.retrieved); io.b(b.golden);
    io.f(b.breadT); io.f(b.bubbleT); io.f(b.clampT); io.f(b.honkT); io.f(b.abductT); io.f(b.angryT); io.f(b.blowT); io.i(b.plungers);
}
template <class IO> void VWorld(IO& io, World& w) {
    uint8_t ph = (uint8_t)w.phase; io.u8(ph); w.phase = (Phase)ph; io.f(w.phaseT); io.f(w.t); io.i(w.round); io.i(w.wave); io.i(w.wavesThisRound); io.f(w.nextWaveT);
    io.i(w.mode); io.i(w.roundsTotal); io.f(w.interLen); io.b(w.ufoDone); io.f(w.flareT); io.i(w.champion);
    { size_t k = io.n(w.players.size()); if (io.reading()) w.players.resize(std::min<size_t>(k, MAX_PLAYERS)); for (auto& p : w.players) VPlayer(io, p); }
    {   // (birds that have gone for good are not sent)
        auto seen = [](const Bird& b) { return !(b.st == BI_GONE && (b.escaped || b.p.y < -5)); };
        if (io.reading()) { size_t k = io.n(0); w.birds.resize(k); for (auto& b : w.birds) VBird(io, b); }
        else { size_t k = 0; for (const auto& b : w.birds) k += seen(b); io.n(k); for (auto& b : w.birds) if (seen(b)) VBird(io, b); }
    }
    { size_t k = io.n(w.projs.size()); if (io.reading()) w.projs.resize(k); for (auto& q : w.projs) { io.u8(q.kind); io.i(q.owner); io.v3(q.p); io.v3(q.v); io.f(q.t); io.i(q.state); } }
    { size_t k = io.n(w.floor.size()); if (io.reading()) w.floor.resize(k); for (auto& f : w.floor) { VGun(io, f.g); io.v3(f.p); } }
    io.i(w.dog.state); io.v3(w.dog.p); io.f(w.dog.t); io.i(w.dog.holding); io.i(w.dog.laughs);
}
}

void WriteWorld(const World& cw, Writer& o) {
    World& w = const_cast<World&>(cw); Out io{o}; VWorld(io, w);
    int n = std::min<int>((int)w.events.size(), 24); o.U32(w.evCount); o.VarU(n);
    for (int k = (int)w.events.size() - n; k < (int)w.events.size(); k++) { Event e = w.events[k]; o.U8(e.kind); io.v3(e.at); o.I32(e.who); o.I32(e.by); o.F32(e.a); }
}
bool ReadWorld(Reader& r, World& w, uint32_t* evSeen) {
    In io{r}; VWorld(io, w);
    uint32_t total = r.U32(), n = r.VarU(); if (n > 24) return false;
    std::vector<Event> evs(n); for (auto& e : evs) { e.kind = (int)r.U8(); io.v3(e.at); e.who = r.I32(); e.by = r.I32(); e.a = r.F32(); }
    uint32_t had = evSeen ? *evSeen : 0, first = total - n;
    for (uint32_t k = 0; k < n; k++) if (first + k >= had) w.Emit(evs[k].kind, evs[k].at, evs[k].who, evs[k].by, evs[k].a);
    if (evSeen) *evSeen = std::max(had, total);
    return !r.bad;
}
void WriteInput(const Input& in, Writer& w) {
    w.F32(in.yaw); w.F32(in.pitch); w.F32(in.moveX); w.F32(in.moveZ); w.F32(in.lean); w.U8((uint8_t)std::clamp(in.lagSteps, 0, 255));
    uint32_t b = (in.fire ? 1 : 0) | (in.alt ? 2 : 0) | (in.reload ? 4 : 0) | (in.swap ? 8 : 0) | (in.use ? 16 : 0) | (in.hook ? 32 : 0) | (in.wipe ? 64 : 0) | (in.crouch ? 128 : 0);
    w.U8(b);
}
void ReadInput(Reader& r, Input& in) {
    in.yaw = r.F32(); in.pitch = r.F32(); in.moveX = std::clamp(r.F32(), -1.0f, 1.0f); in.moveZ = std::clamp(r.F32(), -1.0f, 1.0f); in.lean = std::clamp(r.F32(), -1.0f, 1.0f); in.lagSteps = std::min((int)r.U8(), HIST);
    if (!std::isfinite(in.yaw)) in.yaw = 0; if (!std::isfinite(in.pitch)) in.pitch = 0;
    uint32_t b = r.U8(); in.fire = b & 1; in.alt = b & 2; in.reload = b & 4; in.swap = b & 8; in.use = b & 16; in.hook = b & 32; in.wipe = b & 64; in.crouch = b & 128;
}
void WriteCommand(const Command& c, Writer& w) { w.U8(c.kind); w.I32(c.a); w.I32(c.b); w.Str(c.s); }
bool ReadCommand(Reader& r, Command& c) { c.kind = (uint8_t)r.U8(); c.a = r.I32(); c.b = r.I32(); c.s = r.Str(); return !r.bad && c.s.size() < 64; }
std::string FowlOpts(int mode, int skill, int fill) { std::ostringstream s; s << mode << " " << skill << " " << fill; return s.str(); }
uint32_t FowlDataHash() {
    uint32_t h = 2166136261u;
    for (const char* f : {"fowlplay_guns.json", "fowlplay_attachments.json", "fowlplay_birds.json", "fowlplay_rounds.json", "fowlplay_gambling.json", "fowlplay_slop.json", "fowlplay_modes.json"}) {
        std::ifstream in(rt::DataDir() + "/../fowl/" + f, std::ios::binary); char ch; while (in.get(ch)) if (ch != 13) { h ^= (uint8_t)ch; h *= 16777619u; }
    }
    return h;
}

namespace {
// the host: seats are stalls 0.. in seat order; bots fill to the chosen number of stalls
class FowlHost : public arcade::GameHost {
public:
    World W; int mode = 0, skill = 1, fill = 6; std::vector<int> seatPlayer; std::vector<Input> held; std::vector<uint8_t> pressed; std::vector<uint32_t> rng; float acc = 0;
    void Configure(const std::string& opts) override { std::istringstream s(opts); s >> mode >> skill >> fill; mode = std::clamp(mode, 0, (int)D().modes.size() - 1); skill = std::clamp(skill, 0, 2); fill = std::clamp(fill, 1, MAX_PLAYERS); }
    void Start(int players, uint32_t seed) override {
        int humans = std::min(players, MAX_PLAYERS), bots = std::max(0, std::max(fill, humans) - humans);
        W.Init(humans, bots, mode, seed);
        static const char* BOTS[6] = {"Gus", "Mabel", "Otis", "Pearl", "Hank", "Dottie"};
        seatPlayer.assign(players, -1); for (int s = 0; s < humans; s++) { seatPlayer[s] = s; W.players[s].name = "Stall " + std::to_string(s + 1); W.players[s].bot = false; }
        for (auto& p : W.players) if (p.id >= humans) { p.name = BOTS[p.id % 6]; p.bot = true; }
        held.assign(W.players.size(), Input{}); pressed.assign(W.players.size(), 0); rng.resize(W.players.size()); for (size_t i = 0; i < rng.size(); i++) rng[i] = seed * 17 + (uint32_t)i * 101 + 3;
    }
    bool Act(int player, Reader& r) override {
        if (player < 0 || player >= (int)seatPlayer.size() || seatPlayer[player] < 0) return false;
        int id = seatPlayer[player]; uint8_t kind = (uint8_t)r.U8();
        if (kind == 0) { Input in; ReadInput(r, in); if (r.bad) return false; uint8_t pr = (in.reload ? 1 : 0) | (in.swap ? 2 : 0) | (in.use ? 4 : 0) | (in.hook ? 8 : 0); held[id] = in; pressed[id] |= pr; return false; }
        if (kind == 2) { Command c; if (!ReadCommand(r, c)) return false; W.players[id].cmds.push_back(c); return true; }
        return false;
    }
    bool Tick(float dt, uint32_t ai) override {
        acc += std::min(dt, 0.1f); bool any = false;
        while (acc >= STEP) {
            for (auto& p : W.players) {
                int seat = -1; for (int s = 0; s < (int)seatPlayer.size(); s++) if (seatPlayer[s] == p.id) seat = s;
                if (seat < 0 || (ai >> seat) & 1) { BotInput(W, p.id, p.in, p.cmds, rng[p.id], skill); continue; }
                p.in = held[p.id]; uint8_t pr = pressed[p.id]; p.in.reload = pr & 1; p.in.swap = pr & 2; p.in.use = pr & 4; p.in.hook = pr & 8; pressed[p.id] = 0;
            }
            W.Step(); acc -= STEP; any = true;
        }
        return any;
    }
    void Snapshot(int viewer, Writer& w) const override { (void)viewer; WriteWorld(W, w); }
    bool Over() const override { return W.phase == PH_OVER; }
};
}
std::unique_ptr<arcade::GameHost> MakeFowlHost() { return std::make_unique<FowlHost>(); }
World* FowlHostWorld(arcade::GameHost* h) { auto* f = dynamic_cast<FowlHost*>(h); return f ? &f->W : nullptr; }
int FowlSeatPlayer(arcade::GameHost* h, int seat) { auto* f = dynamic_cast<FowlHost*>(h); return f && seat >= 0 && seat < (int)f->seatPlayer.size() ? f->seatPlayer[seat] : -1; }

int RunFowlNetTest() {
    int fails = 0; auto check = [&](bool ok, const char* what) { std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what); if (!ok) fails++; };
    std::printf("Fowl Play net test\n");
    auto h = MakeFowlHost(); h->Configure(FowlOpts(0, 1, 6)); h->Start(2, 33); World* W = FowlHostWorld(h.get());
    check(W && W->players.size() == 6 && !W->players[0].bot && W->players[2].bot, "two seats and four bots fill the six stalls");
    World mirror; uint32_t seen = 0; bool same = true; int frames = 0;
    for (int f = 0; f < 60 * 140 && W->phase != PH_OVER; f++) {
        Writer a; a.U8(0); Input in; in.yaw = 0.1f * sinf(f * 0.01f); in.pitch = 0.3f; in.fire = (f / 10) % 2 == 0; WriteInput(in, a); Reader ra(a.b); h->Act(0, ra);
        if (W->phase == PH_INTER && f % 120 == 0) { Writer c; c.U8(2); Command cm; cm.kind = CMD_SLOT; cm.a = 10; WriteCommand(cm, c); Reader rc(c.b); h->Act(1, rc); }
        h->Tick(1 / 60.0f, 0);
        if (f % 3 == 0) {
            Writer s; h->Snapshot(0, s); Reader rs(s.b); if (!ReadWorld(rs, mirror, &seen)) { same = false; break; }
            Writer again; WriteWorld(mirror, again);
            size_t a1 = s.b.size() - (5 + std::min<size_t>(W->events.size(), 24) * 25), a2 = again.b.size() - (5 + std::min<size_t>(mirror.events.size(), 24) * 25);
            if (a1 != a2 || !std::equal(s.b.begin(), s.b.begin() + a1, again.b.begin())) same = false;
            frames++;
        }
    }
    check(same && frames > 100, "the mirror rewrites every snapshot byte for byte (two minutes of play)");
    check(W->players[0].shots > 0, "seat 0's trigger reaches the host");
    Writer big; WriteWorld(*W, big); std::printf("  (a snapshot is %d bytes; birds %d)\n", (int)big.b.size(), (int)W->birds.size());
    std::printf(fails ? "Fowl net: %d FAILED\n" : "Fowl net: all checks passed\n", fails);
    return fails ? 1 : 0;
}

int RunFowlNetLoop(int lagMs, bool forceMemory) {
    using namespace arcade;
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::string err; bool real = !forceMemory && net::Init(&err);
    if (real && lagMs > 0) net::SetFakeLag(lagMs, 0.0f);
    auto make = [&]() { return real ? net::MakeTransport() : net::MakeMemoryTransport(); };
    uint16_t port = 47890; std::string addr = real ? "127.0.0.1:" + std::to_string(port) : "mem:" + std::to_string(port);
    std::printf("net-loop fowl over %s: a host and five guests, Quick Draw (7 rounds)\n", real ? "loopback UDP" : "the in-memory transport");
    int fails = 0; auto check = [&](bool ok, const std::string& what) { std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    const int NG = 5; Session host, gs[NG]; Profile ph{"Host", 93};
    if (!host.Host(ph, G_FOWL, &err, port, make(), false)) { std::printf("FAIL: host: %s\n", err.c_str()); return 1; }
    host.gameOpts = FowlOpts(ModeIndex("quick"), 1, 6);
    const float dt = 1 / 60.0f; double t = 0; bool playing = false; World mirror[NG]; uint32_t seen[NG] = {}; int ver[NG] = {}; int readFails = 0; size_t biggest = 0; uint32_t brng[NG] = {3, 5, 7, 9, 11}, hrng = 13;
    auto pace = [&]() { if (real) std::this_thread::sleep_for(std::chrono::milliseconds(16)); };
    auto step = [&](int frames) {
        for (int f = 0; f < frames; f++) {
            t += dt; host.Update(t, dt);
            if (playing && host.stage == S_PLAYING) { World* hw = FowlHostWorld(host.HostGame()); int me = FowlSeatPlayer(host.HostGame(), host.MyPlayer()); if (hw && me >= 0) { Input in; std::vector<Command> cs; BotInput(*hw, me, in, cs, hrng, 1); Writer w; w.U8(0); WriteInput(in, w); host.Act(w); for (auto& c : cs) { Writer x; x.U8(2); WriteCommand(c, x); host.Act(x); } } }
            for (int k = 0; k < NG; k++) {
                gs[k].Update(t, dt); if (gs[k].stage != S_PLAYING) continue;
                if (gs[k].stateVersion != ver[k] && !gs[k].Snapshot().empty()) { ver[k] = gs[k].stateVersion; biggest = std::max(biggest, gs[k].Snapshot().size()); Reader r(gs[k].Snapshot()); if (!ReadWorld(r, mirror[k], &seen[k])) readFails++; }
                int me = gs[k].MyPlayer(); if (me < 0 || me >= (int)mirror[k].players.size()) continue;
                Input in; std::vector<Command> cs; BotInput(mirror[k], me, in, cs, brng[k], 1); in.lagSteps = 6;
                Writer w; w.U8(0); WriteInput(in, w); gs[k].Act(w); for (auto& c : cs) { Writer x; x.U8(2); WriteCommand(c, x); gs[k].Act(x); }
            }
            pace();
        }
    };
    auto until = [&](std::function<bool()> ok, float seconds) { for (int i = 0; i < seconds / dt && !ok(); i++) step(1); };
    for (int k = 0; k < NG; k++) { Profile p{std::string("Shooter ") + char('A' + k), (uint64_t)(300 + k)}; if (!gs[k].Join(p, addr, &err, 0, make())) { std::printf("FAIL: join: %s\n", err.c_str()); return 1; } }
    until([&] { for (auto& g : gs) if (g.stage != S_LOBBY) return false; return true; }, 20);
    for (auto& g : gs) g.SetReady(true);
    until([&] { for (int s = 1; s <= NG; s++) if (!host.seats[s].ready) return false; return true; }, 10);
    std::string why; check(host.Launch(&why), "six shooters launch Fowl Play" + (why.empty() ? std::string() : ": " + why));
    playing = true;
    until([&] { World* hw = FowlHostWorld(host.HostGame()); return hw && hw->phase == PH_OVER; }, real ? 900 : 1800);
    step(30);
    World* hw = FowlHostWorld(host.HostGame());
    check(hw && hw->phase == PH_OVER && hw->champion >= 0, "the match plays to the podium");
    int guestBirds = 0; if (hw) for (int s = 1; s <= NG; s++) guestBirds += hw->players[s].birds;
    check(guestBirds > 0, "the guests' shots land on the host (" + std::to_string(guestBirds) + " birds)");
    bool agree = true; for (auto& m : mirror) if (hw) for (size_t i = 0; i < hw->players.size() && i < m.players.size(); i++) agree = agree && m.players[i].birds == hw->players[i].birds;
    check(agree, "every guest agrees on the board");
    check(readFails == 0, "every snapshot decodes");
    std::printf("  (the biggest snapshot: %d bytes)\n", (int)biggest);
    for (auto& g : gs) g.Leave(); host.Leave();
    for (int i = 0; i < 30; i++) { t += dt; for (auto& g : gs) g.Update(t, dt); }
    if (real) net::Shutdown();
    std::printf(fails ? "net-loop fowl: %d FAILED\n" : "net-loop fowl: passed\n", fails);
    return fails ? 1 : 0;
}
}
