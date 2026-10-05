// NOCLIP's network host and snapshot (see noclip_net.h).
#include "noclip_net.h"
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

namespace nc {
namespace {
struct Out { Writer& w; int viewerLevel = -1; Vector3 viewerAt{}; void f(float& v) { w.F32(v); } void i(int& v) { w.I32(v); } void u8(uint8_t& v) { w.U8(v); } void u32(uint32_t& v) { w.U32(v); } void b(bool& v) { w.U8(v ? 1 : 0); } void s(std::string& v) { w.Str(v); } void v3(Vector3& v) { f(v.x); f(v.y); f(v.z); } size_t n(size_t k) { w.VarU((uint32_t)k); return k; } bool reading() const { return false; } };
struct In { Reader& r; int viewerLevel = -1; Vector3 viewerAt{}; void f(float& v) { v = r.F32(); } void i(int& v) { v = r.I32(); } void u8(uint8_t& v) { v = (uint8_t)r.U8(); } void u32(uint32_t& v) { v = r.U32(); } void b(bool& v) { v = r.U8() != 0; } void s(std::string& v) { v = r.Str(); } void v3(Vector3& v) { f(v.x); f(v.y); f(v.z); } size_t n(size_t) { uint32_t k = r.VarU(); return k > 4000 ? 0 : k; } bool reading() const { return true; } };
template <class IO> void VLoot(IO& io, Loot& l) { io.i(l.def); io.i(l.value); io.b(l.damaged); io.i(l.foundOn); io.u32(l.uid); }
template <class IO, class T, class F> void VVec(IO& io, std::vector<T>& v, F each) { size_t k = io.n(v.size()); if (io.reading()) v.resize(k); for (auto& x : v) each(x); }
template <class IO> void VPlayer(IO& io, Player& p) {
    io.i(p.id); io.b(p.present); io.b(p.bot); io.s(p.name); uint8_t st = (uint8_t)p.st; io.u8(st); p.st = (PState)st; io.i(p.level); io.v3(p.p); io.f(p.yaw); io.f(p.pitch); io.v3(p.vel);
    io.f(p.health); io.f(p.sanity); io.f(p.stamina); io.u8(p.injuries); io.f(p.downT); io.f(p.stunT);
    for (auto& t : p.tools) { io.i(t.item); io.i(t.charges); io.f(t.fuel); } io.i(p.toolSlots); io.i(p.sel); VLoot(io, p.pocket[0]); VLoot(io, p.pocket[1]); VLoot(io, p.hands); io.i(p.carryWith);
    io.b(p.lamp); io.f(p.battery); io.f(p.lampFlicker); io.f(p.lostT); io.f(p.blackoutT); io.f(p.jumpCool); io.f(p.swimT); io.f(p.tankAir); io.b(p.crouched);
    io.f(p.stayT); io.f(p.stayPromptT); io.b(p.impostor); io.i(p.takenOnLevel);
    io.u32(p.suits); io.i(p.hat); io.i(p.vest); io.i(p.lamp_c); io.i(p.costume); io.i(p.suitCos); io.i(p.deaths); io.s(p.lastCause); io.i(p.broughtValue); io.i(p.photos);
}
template <class IO> void VLab(IO& io, LabState& l) {
    io.i(l.level); io.i(l.idx); io.b(l.online); io.b(l.doorOpen); io.b(l.locked); io.f(l.fuel); VVec(io, l.crate, [&](Loot& x) { VLoot(io, x); });
    io.f(l.charge); io.b(l.charging); io.f(l.openT); io.f(l.cooldown); io.i(l.jumpFor); io.f(l.jumpT); io.i(l.jumpTo); io.u32(l.upgrades); io.f(l.sirenT); io.f(l.cargoCool);
}
template <class IO> void VCampaign(IO& io, World& w) {
    io.i(w.mode); io.i(w.week); io.i(w.day); io.i(w.quota); io.i(w.credit); io.i(w.cash); io.b(w.failed); io.i(w.weeksSurvived); io.i(w.contract); io.b(w.contractDone); io.b(w.won); io.i(w.score); io.i(w.contractLevel); io.i(w.contractTarget);
    VVec(io, w.marks, [&](World::Mark& m) { io.i(m.level); io.v3(m.at); io.f(m.yaw); });
    VVec(io, w.forecast, [&](int& x) { io.i(x); }); VVec(io, w.fenceRate, [&](int& x) { io.i(x); });
    VVec(io, w.sold, [&](Sale& s) { io.s(s.what); io.i(s.value); io.b(s.fence); io.i(s.level); });
    VVec(io, w.bay, [&](Loot& x) { VLoot(io, x); });
    io.u32(w.dossier); VVec(io, w.learned, [&](std::string& s) { io.s(s); });
    io.b(w.inDay); io.f(w.clock); io.b(w.overtime); io.i(w.insertion); io.u32(w.daySeed); io.f(w.lightsOutT); io.f(w.lockdownT); io.s(w.memo);
    VVec(io, w.crew, [&](Player& p) { VPlayer(io, p); });
    VVec(io, w.labs, [&](LabState& l) { VLab(io, l); });
}
}

void WriteWorld(const World& cw, int viewer, Writer& o) {
    World& w = const_cast<World&>(cw); Out io{o};
    // (the sold list is long-lived: only its last twenty go out)
    std::vector<Sale> keep; if (w.sold.size() > 20) { keep.assign(w.sold.begin(), w.sold.end() - 20); w.sold.erase(w.sold.begin(), w.sold.end() - 20); }
    VCampaign(io, w);
    if (!keep.empty()) w.sold.insert(w.sold.begin(), keep.begin(), keep.end());
    int lvl = viewer >= 0 && viewer < (int)w.crew.size() ? w.crew[viewer].level : (w.crew.empty() ? 0 : w.crew[0].level); Vector3 at = viewer >= 0 && viewer < (int)w.crew.size() ? w.crew[viewer].p : Vector3{};
    // loot and entities on the viewer's level, near them
    std::vector<WorldItem*> its; for (auto& it : w.items) if (it.level == lvl && Vector3Distance(it.p, at) < 60) its.push_back(&it);
    o.VarU((uint32_t)its.size()); for (auto* it : its) { VLoot(io, it->loot); io.i(it->level); io.v3(it->p); io.f(it->noiseT); }
    std::vector<Entity*> es; for (auto& e : w.ents) if (e.level == lvl && Vector3Distance(e.p, at) < 70) es.push_back(&e);
    o.VarU((uint32_t)es.size()); for (auto* e : es) { io.i(e->def); io.u32(e->uid); io.i(e->level); io.v3(e->p); io.v3(e->v); io.f(e->yaw); uint8_t st = (uint8_t)e->st; io.u8(st); io.i(e->mimicOf); io.f(e->stunT); }
    int n = std::min<int>((int)w.events.size(), 24); o.U32(w.evCount); o.VarU(n);
    for (int k = (int)w.events.size() - n; k < (int)w.events.size(); k++) { Event e = w.events[k]; o.U8(e.kind); o.I32(e.who); o.I32(e.by); o.I32(e.level); io.v3(e.at); o.F32(e.a); o.Str(e.s); }
}
bool ReadWorld(Reader& r, World& w, uint32_t* evSeen) {
    In io{r}; uint32_t oldSeed = w.daySeed; VCampaign(io, w);
    if (w.daySeed != oldSeed) { w.levels.clear(); w.seenCells.clear(); }
    w.mirror = true;
    uint32_t ni = r.VarU(); if (ni > 4000) return false; w.items.resize(ni); for (auto& it : w.items) { VLoot(io, it.loot); io.i(it.level); io.v3(it.p); io.f(it.noiseT); }
    uint32_t ne = r.VarU(); if (ne > 2000) return false; w.ents.resize(ne); for (auto& e : w.ents) { io.i(e.def); io.u32(e.uid); io.i(e.level); io.v3(e.p); io.v3(e.v); io.f(e.yaw); uint8_t st; io.u8(st); e.st = (EState)st; io.i(e.mimicOf); io.f(e.stunT); }
    uint32_t total = r.U32(), n = r.VarU(); if (n > 24) return false;
    uint32_t had = evSeen ? *evSeen : 0, first = total - n;
    for (uint32_t k = 0; k < n; k++) { Event e; e.kind = (int)r.U8(); e.who = r.I32(); e.by = r.I32(); e.level = r.I32(); io.v3(e.at); e.a = r.F32(); e.s = r.Str(); if (first + k >= had) w.Emit(e.kind, e.who, e.by, e.level, e.at, e.a, e.s); }
    if (evSeen) *evSeen = std::max(had, total);
    return !r.bad;
}
void WriteInput(const Input& in, Writer& w) {
    w.F32(in.yaw); w.F32(in.pitch); w.F32(in.moveX); w.F32(in.moveZ); w.I32(in.slot);
    uint32_t b = (in.sprint ? 1 : 0) | (in.crouch ? 2 : 0) | (in.use ? 4 : 0) | (in.useHeld ? 8 : 0) | (in.drop ? 16 : 0) | (in.throwIt ? 32 : 0) | (in.lamp ? 64 : 0) | (in.primary ? 128 : 0) | (in.primaryHeld ? 256 : 0);
    w.U16(b);
}
void ReadInput(Reader& r, Input& in) {
    in.yaw = r.F32(); in.pitch = r.F32(); in.moveX = std::clamp(r.F32(), -1.0f, 1.0f); in.moveZ = std::clamp(r.F32(), -1.0f, 1.0f); in.slot = std::clamp(r.I32(), -1, 4);
    if (!std::isfinite(in.yaw)) in.yaw = 0; if (!std::isfinite(in.pitch)) in.pitch = 0;
    uint32_t b = r.U16(); in.sprint = b & 1; in.crouch = b & 2; in.use = b & 4; in.useHeld = b & 8; in.drop = b & 16; in.throwIt = b & 32; in.lamp = b & 64; in.primary = b & 128; in.primaryHeld = b & 256;
}
void WriteCommand(const Command& c, Writer& w) { w.U8(c.kind); w.I32(c.a); w.I32(c.b); w.Str(c.s); }
bool ReadCommand(Reader& r, Command& c) { c.kind = (uint8_t)r.U8(); c.a = r.I32(); c.b = r.I32(); c.s = r.Str(); return !r.bad && c.s.size() < 64; }
std::string NoclipOpts(int bots, int mode) { std::ostringstream s; s << bots << " " << mode; return s.str(); }
uint32_t NoclipDataHash() {
    uint32_t h = 2166136261u;
    for (const char* f : {"noclip_levels.json", "noclip_loot.json", "noclip_items.json", "noclip_entities.json", "noclip_contracts.json", "noclip_cosmetics.json"}) { std::ifstream in(rt::DataDir() + "/../noclip/" + f, std::ios::binary); char ch; while (in.get(ch)) if (ch != 13) { h ^= (uint8_t)ch; h *= 16777619u; } }
    return h;
}

namespace {
class NoclipHost : public arcade::GameHost {
public:
    World W; int bots = 0, mode = 0, humans = 1; std::vector<Input> held; std::vector<uint8_t> pressed; std::vector<int> slotPressed; std::vector<uint32_t> rng; float acc = 0;
    void Configure(const std::string& opts) override { std::istringstream s(opts); s >> bots >> mode; bots = std::clamp(bots, 0, MAX_CREW - 1); }
    void Start(int players, uint32_t seed) override {
        humans = std::clamp(players, 1, MAX_CREW); W.Init(humans, std::min(bots, MAX_CREW - humans), mode, seed);
        held.assign(W.crew.size(), Input{}); pressed.assign(W.crew.size(), 0); slotPressed.assign(W.crew.size(), -1); rng.resize(W.crew.size()); for (size_t i = 0; i < rng.size(); i++) rng[i] = seed * 13 + (uint32_t)i * 7 + 1;
    }
    bool Act(int player, Reader& r) override {
        if (player < 0 || player >= (int)W.crew.size()) return false;
        uint8_t kind = (uint8_t)r.U8();
        if (kind == 0) { Input in; ReadInput(r, in); if (r.bad) return false; uint8_t pr = (in.use ? 1 : 0) | (in.drop ? 2 : 0) | (in.throwIt ? 4 : 0) | (in.lamp ? 8 : 0) | (in.primary ? 16 : 0); held[player] = in; pressed[player] |= pr; if (in.slot >= 0) slotPressed[player] = in.slot; return false; }
        if (kind == 2) { Command c; if (!ReadCommand(r, c)) return false; W.crew[player].cmds.push_back(c); return true; }
        return false;
    }
    bool Tick(float dt, uint32_t ai) override {
        acc += std::min(dt, 0.1f); bool any = false;
        while (acc >= STEP) {
            for (auto& p : W.crew) {
                bool aiSeat = p.id < humans && ((ai >> p.id) & 1);
                if (p.bot || aiSeat) { BotInput(W, p.id, p.in, p.cmds, rng[p.id]); continue; }
                p.in = held[p.id]; uint8_t pr = pressed[p.id]; p.in.use = pr & 1; p.in.drop = pr & 2; p.in.throwIt = pr & 4; p.in.lamp = pr & 8; p.in.primary = pr & 16; p.in.slot = slotPressed[p.id]; pressed[p.id] = 0; slotPressed[p.id] = -1;
            }
            W.Step(); acc -= STEP; any = true;
        }
        return any;
    }
    void Snapshot(int viewer, Writer& w) const override { WriteWorld(W, viewer, w); }
    bool Over() const override { return W.failed; }
};
}
std::unique_ptr<arcade::GameHost> MakeNoclipHost() { return std::make_unique<NoclipHost>(); }
World* NoclipHostWorld(arcade::GameHost* h) { auto* x = dynamic_cast<NoclipHost*>(h); return x ? &x->W : nullptr; }
int NoclipSeatPlayer(arcade::GameHost* h, int seat) { auto* x = dynamic_cast<NoclipHost*>(h); return x && seat >= 0 && seat < (int)x->W.crew.size() ? seat : -1; }

int RunNoclipNetTest() {
    int fails = 0; auto check = [&](bool ok, const char* what) { std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what); if (!ok) fails++; };
    std::printf("NOCLIP net test\n");
    auto h = MakeNoclipHost(); h->Configure(NoclipOpts(2, 0)); h->Start(2, 41); World* W = NoclipHostWorld(h.get());
    check(W && W->crew.size() == 4 && !W->crew[0].bot && W->crew[2].bot, "two seats and two bot salvagers");
    { Writer c; c.U8(2); Command cm; cm.kind = C_START_DAY; WriteCommand(cm, c); Reader rc(c.b); h->Act(0, rc); }
    World mirror; uint32_t seen = 0; bool same = true; int frames = 0;
    for (int f = 0; f < 30 * 90; f++) {
        Writer a; a.U8(0); Input in; in.moveX = (f / 60) % 2 ? 1.0f : 0.0f; in.yaw = f * 0.01f; WriteInput(in, a); Reader ra(a.b); h->Act(0, ra);
        h->Tick(1 / 30.0f, 0b10);
        if (f % 3 == 0) {
            Writer s; h->Snapshot(0, s); Reader rs(s.b); if (!ReadWorld(rs, mirror, &seen)) { same = false; break; }
            Writer again; WriteWorld(mirror, 0, again);
            // (the events block differs: compare everything before it)
            auto evBytes = [](const World& w) { size_t n = 5; int k = std::min<int>((int)w.events.size(), 24); for (int i = (int)w.events.size() - k; i < (int)w.events.size(); i++) n += 1 + 4 + 4 + 4 + 12 + 4 + 1 + w.events[i].s.size(); return n; };
            size_t a1 = s.b.size() - evBytes(*W), a2 = again.b.size() - evBytes(mirror);
            if (a1 != a2 || !std::equal(s.b.begin(), s.b.begin() + std::min(a1, a2), again.b.begin())) same = false;
            frames++;
        }
    }
    check(same && frames > 100, "the mirror rewrites every snapshot byte for byte (90 s of a day)");
    check(mirror.inDay && mirror.crew.size() == 4 && mirror.L(mirror.crew[0].level).w > 0, "the guest's mirror builds the level from the day's seed");
    Writer big; WriteWorld(*W, 0, big); std::printf("  (a snapshot is %d bytes)\n", (int)big.b.size());
    std::printf(fails ? "NOCLIP net: %d FAILED\n" : "NOCLIP net: all checks passed\n", fails);
    return fails ? 1 : 0;
}

int RunNoclipNetLoop(int lagMs, bool forceMemory) {
    using namespace arcade;
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::string err; bool real = !forceMemory && net::Init(&err);
    if (real && lagMs > 0) net::SetFakeLag(lagMs, 0.0f);
    auto make = [&]() { return real ? net::MakeTransport() : net::MakeMemoryTransport(); };
    uint16_t port = 47900; std::string addr = real ? "127.0.0.1:" + std::to_string(port) : "mem:" + std::to_string(port);
    std::printf("net-loop noclip over %s: a host and three guests work a day\n", real ? "loopback UDP" : "the in-memory transport");
    int fails = 0; auto check = [&](bool ok, const std::string& what) { std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    const int NG = 3; Session host, gs[NG]; Profile ph{"Host", 94};
    if (!host.Host(ph, G_NOCLIP, &err, port, make(), false)) { std::printf("FAIL: host: %s\n", err.c_str()); return 1; }
    host.gameOpts = NoclipOpts(0, 0);
    const float dt = 1 / 30.0f; double t = 0; bool playing = false; World mirror[NG]; uint32_t seen[NG] = {}; int ver[NG] = {}; int readFails = 0; size_t biggest = 0; uint32_t brng[NG] = {3, 5, 7}, hrng = 9;
    auto pace = [&]() { if (real) std::this_thread::sleep_for(std::chrono::milliseconds(33)); };
    auto step = [&](int frames) {
        for (int f = 0; f < frames; f++) {
            t += dt; host.Update(t, dt);
            if (playing && host.stage == S_PLAYING) { World* hw = NoclipHostWorld(host.HostGame()); if (hw) { Input in; std::vector<Command> cs; BotInput(*hw, 0, in, cs, hrng); if (!hw->inDay && hw->day == 1) cs.push_back(Command{C_START_DAY}); Writer w; w.U8(0); WriteInput(in, w); host.Act(w); for (auto& c : cs) { Writer x; x.U8(2); WriteCommand(c, x); host.Act(x); } } }
            for (int k = 0; k < NG; k++) {
                gs[k].Update(t, dt); if (gs[k].stage != S_PLAYING) continue;
                if (gs[k].stateVersion != ver[k] && !gs[k].Snapshot().empty()) { ver[k] = gs[k].stateVersion; biggest = std::max(biggest, gs[k].Snapshot().size()); Reader r(gs[k].Snapshot()); if (!ReadWorld(r, mirror[k], &seen[k])) readFails++; }
                int me = gs[k].MyPlayer(); if (me < 0 || me >= (int)mirror[k].crew.size()) continue;
                mirror[k].crew[me].botThink = 0;   // (the guest's mind reads its mirror)
                Input in; std::vector<Command> cs; World& m = mirror[k]; if (m.inDay) BotInput(m, me, in, cs, brng[k]);
                Writer w; w.U8(0); WriteInput(in, w); gs[k].Act(w);
            }
            pace();
        }
    };
    auto until = [&](std::function<bool()> ok, float seconds) { for (int i = 0; i < seconds / dt && !ok(); i++) step(1); };
    for (int k = 0; k < NG; k++) { Profile p{std::string("Salvager ") + char('A' + k), (uint64_t)(400 + k)}; if (!gs[k].Join(p, addr, &err, 0, make())) { std::printf("FAIL: join: %s\n", err.c_str()); return 1; } }
    until([&] { for (auto& g : gs) if (g.stage != S_LOBBY) return false; return true; }, 20);
    for (auto& g : gs) g.SetReady(true);
    until([&] { for (int s = 1; s <= NG; s++) if (!host.seats[s].ready) return false; return true; }, 10);
    std::string why; check(host.Launch(&why), "four salvagers launch NOCLIP" + (why.empty() ? std::string() : ": " + why));
    playing = true;
    until([&] { World* hw = NoclipHostWorld(host.HostGame()); return hw && hw->inDay; }, 30);
    until([&] { World* hw = NoclipHostWorld(host.HostGame()); return hw && !hw->inDay && hw->day > 1; }, real ? 1200 : 1500);
    World* hw = NoclipHostWorld(host.HostGame());
    check(hw && hw->day > 1, "the crew works a day to its end (" + std::string(hw ? TextFormat("%d items in the bay", (int)hw->bay.size()) : "") + ")");
    bool agree = true; for (auto& m : mirror) if (hw) agree = agree && m.day == hw->day && m.bay.size() == hw->bay.size();
    check(agree, "every guest agrees on the day and the bay");
    check(readFails == 0, "every snapshot decodes");
    std::printf("  (the biggest snapshot: %d bytes)\n", (int)biggest);
    for (auto& g : gs) g.Leave(); host.Leave();
    for (int i = 0; i < 30; i++) { t += dt; for (auto& g : gs) g.Update(t, dt); }
    if (real) net::Shutdown();
    std::printf(fails ? "net-loop noclip: %d FAILED\n" : "net-loop noclip: passed\n", fails);
    return fails ? 1 : 0;
}
}
