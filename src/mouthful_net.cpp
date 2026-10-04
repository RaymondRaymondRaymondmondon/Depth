// Mouthful's network layer (see mouthful_net.h): the input, the snapshot Visit, the host, and the checks.
#include "mouthful_net.h"
#include "arcade_session.h"
#include "net.h"
#include "raymath.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <thread>

namespace mf {

// ---------------------------------------------------------------- input
void WriteInput(const Input& in, Writer& w) {
    w.U8(MA_INPUT); w.F32(in.yaw); w.F32(in.pitch);
    w.U8((in.swim ? 1 : 0) | (in.boost ? 2 : 0) | (in.bite ? 4 : 0) | (in.ability ? 8 : 0) | (in.brake ? 16 : 0));
    w.U8((uint32_t)(in.fork + 1));
}
bool ReadInput(Reader& r, Input& in) {
    in.yaw = r.F32(); in.pitch = r.F32();
    int f = (int)r.U8(); in.swim = f & 1; in.boost = f & 2; in.bite = f & 4; in.ability = f & 8; in.brake = f & 16;
    in.fork = (int)r.U8() - 1;
    if (r.bad || !std::isfinite(in.yaw) || !std::isfinite(in.pitch) || in.fork > 8) return false;
    in.pitch = std::clamp(in.pitch, -1.5f, 1.5f);
    return true;
}
void OrderHello(Writer& w, const std::string& name) { w.U8(MA_HELLO); w.Str(name.substr(0, 24)); }

// ---------------------------------------------------------------- the snapshot (one Visit for writing and reading)
namespace {
struct Out {
    Writer& w;
    static constexpr bool reading = false;
    void f(float& v) { w.F32(v); }
    void i(int& v) { w.VarU(((uint32_t)v << 1) ^ (uint32_t)(v >> 31)); }
    void b(bool& v) { w.U8(v ? 1 : 0); }
    void u(uint32_t& v) { w.U32(v); }
    void s(std::string& v) { w.Str(v); }
    template <class T, class F> void vec(std::vector<T>& v, F fn) { w.VarU((uint32_t)v.size()); for (auto& x : v) fn(x); }
    void q16(float& v, float lo, float hi) { w.U16((uint32_t)std::clamp((int)lroundf((v - lo) / (hi - lo) * 65535), 0, 65535)); }
    void q8(float& v, float max) { w.U8((uint32_t)std::clamp((int)lroundf(v / max * 255), 0, 255)); }
    void s8(float& v, float max) { w.U8((uint32_t)(uint8_t)(int8_t)std::clamp((int)lroundf(v / max * 127), -127, 127)); }
    void ang(float& v) { float a = v - 2 * PI * floorf(v / (2 * PI)); w.U16((uint32_t)lroundf(a / (2 * PI) * 65536) & 0xFFFF); }
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
    template <class T, class F> void vec(std::vector<T>& v, F fn) { uint32_t n = r.VarU(); if (n > 20000 || r.bad) { r.bad = true; n = 0; } v.resize(n); for (auto& x : v) { fn(x); if (r.bad) break; } }
    void q16(float& v, float lo, float hi) { v = lo + r.U16() / 65535.0f * (hi - lo); }
    void q8(float& v, float max) { v = r.U8() / 255.0f * max; }
    void s8(float& v, float max) { v = (int8_t)(uint8_t)r.U8() / 127.0f * max; }
    void ang(float& v) { v = r.U16() / 65536.0f * 2 * PI; }
    bool bad() const { return r.bad; }
};
template <class A> void P16(A& a, Vector3& p) { a.q16(p.x, X0 - 10, X1 + 10); a.q16(p.y, -320, 5); a.q16(p.z, Z0 - 10, Z1 + 10); }
template <class A> void T8(A& a, float& v, float max) { a.q8(v, max); }
// (what a position reads back as: the filters judge the quantised one, so a mirror writing again keeps the same)
float Qv(float v, float lo, float hi) { return lo + std::clamp((int)lroundf((v - lo) / (hi - lo) * 65535), 0, 65535) / 65535.0f * (hi - lo); }
Vector3 Q3(Vector3 p) { return {Qv(p.x, X0 - 10, X1 + 10), Qv(p.y, -320, 5), Qv(p.z, Z0 - 10, Z1 + 10)}; }
constexpr float NEAR_M = 100;

template <class A> void VisitMouth(A& a, Mouth& m, bool own) {
    a.s(m.name); a.b(m.bot); a.b(m.alive);
    P16(a, m.pos); a.s8(m.vel.x, 30); a.s8(m.vel.y, 30); a.s8(m.vel.z, 30);
    a.ang(m.yaw); a.s8(m.pitch, 1.6f); a.s8(m.bank, 1.6f);
    a.f(m.mass); a.i(m.tier); a.i(m.form); a.i(m.path);
    int fl = (m.boosting ? 1 : 0) | (m.hidden ? 2 : 0) | (m.ambush ? 4 : 0) | (m.king ? 8 : 0) | (m.airborne ? 16 : 0) | (m.blobKing ? 32 : 0);
    a.i(fl);
    if constexpr (A::reading) { m.boosting = fl & 1; m.hidden = fl & 2; m.ambush = fl & 4; m.king = fl & 8; m.airborne = fl & 16; m.blobKing = fl & 32; }
    T8(a, m.immuneT, 12.75f); T8(a, m.swallowT, 2.55f); T8(a, m.stunT, 5.1f); T8(a, m.morphT, 1.02f); T8(a, m.biteAnim, 0.51f); T8(a, m.hurtT, 0.51f);
    T8(a, m.tellT, 0.51f); T8(a, m.abT, 12.75f); T8(a, m.dashT, 1.02f); T8(a, m.respawnT, 12.75f); T8(a, m.stillT, 5.1f); T8(a, m.buriedT, 5.1f);
    a.f(m.score); a.f(m.massEaten); a.f(m.crownT); a.i(m.kills); a.i(m.deaths); a.i(m.bestTier); a.i(m.apexKills);
    a.s(m.lastCause);
    if (own) {   // what only its own screen shows: the cooldowns, the effects on it, the fork
        a.q8(m.stamina, 1); T8(a, m.abCd, 25.5f); T8(a, m.biteCd, 2.55f); T8(a, m.blindT, 5.1f); T8(a, m.reverseT, 5.1f); T8(a, m.poisonT, 5.1f); T8(a, m.bleedT, 5.1f);
        T8(a, m.holdT, 5.1f); T8(a, m.jetT, 2.55f); T8(a, m.frenzyT, 12.75f); T8(a, m.markT, 25.5f);
        a.i(m.lastPath); a.i(m.streak); a.i(m.pendingFork); a.f(m.forkT);
        a.vec(m.forkOpts, [&](int& k) { a.i(k); });
    }
}
template <class A> void Visit(A& a, World& w, int viewer) {
    a.f(w.time); a.b(w.over); a.i(w.winner); a.i(w.king); a.f(w.levAwakeT); a.f(w.firstKingT); a.i(w.crownsChanged); a.b(w.levAte);
    for (int& d : w.deathsBy) a.i(d);
    int n = (int)w.mouths.size(); a.i(n);
    if (n != (int)w.mouths.size()) { if constexpr (A::reading) a.r.bad = true; return; }
    for (auto& m : w.mouths) { VisitMouth(a, m, m.id == viewer); if (a.bad()) return; }
    a.vec(w.clouds, [&](Cloud& c) { P16(a, c.pos); a.f(c.r); a.f(c.t); a.i(c.owner); a.i(c.kind); });
    a.vec(w.plankton, [&](Plankton& p) { P16(a, p.pos); a.f(p.r); });
    // the dangers that aren't players (stage 5)
    { Boat& b = w.boat; a.b(b.on); P16(a, b.pos); a.f(b.dirZ); a.b(b.net); a.b(b.hooks); a.b(b.chum); a.f(b.nextT);
      a.vec(b.hookList, [&](Hook& h) { P16(a, h.pos); a.i(h.held); a.b(h.gone); });
      a.vec(b.netted, [&](int& k) { a.i(k); }); }
    { Bloom& r = w.bloom; a.b(r.on); a.b(r.done); P16(a, r.pos); a.f(r.r); a.f(r.t); }
    { WhaleFall& f = w.fall; a.b(f.on); a.b(f.done); P16(a, f.pos); a.f(f.left); }
    { OrcaPod& o = w.orcas; a.b(o.on); a.b(o.done); a.f(o.t); a.vec(o.agents, [&](int& k) { a.i(k); }); }
    a.b(w.duskDone);
    std::vector<FeedLine> tail;
    if constexpr (!A::reading) { size_t k = std::min<size_t>(w.feed.size(), 12); tail.assign(w.feed.end() - k, w.feed.end()); }
    a.vec(A::reading ? w.feed : tail, [&](FeedLine& f) { a.s(f.text); a.f(f.t); int c = f.c.r | f.c.g << 8 | f.c.b << 16; a.i(c); if constexpr (A::reading) f.c = {(unsigned char)(c & 255), (unsigned char)((c >> 8) & 255), (unsigned char)((c >> 16) & 255), 255}; });
    // the web near the viewer (the leviathan always: it's the trench's whole story)
    Vector3 eye = viewer >= 0 && viewer < (int)w.mouths.size() ? Q3(w.mouths[viewer].pos) : Vector3{0, 0, 0};
    if (viewer >= 0 && viewer < (int)w.mouths.size() && !w.mouths[viewer].alive) eye = {-250, -4, 0};
    int na = (int)w.eco.agents.size(); a.i(na);
    if constexpr (A::reading) { if (na < 0 || na > 20000) { a.r.bad = true; return; } w.eco.agents.resize(na); for (auto& g : w.eco.agents) if (g.diver < 0) g.alive = false; }
    std::vector<int> idx;
    if constexpr (!A::reading) for (int k = 0; k < na; k++) { const rt::Agent& g = w.eco.agents[k]; if (g.alive && g.diver < 0 && (k == w.leviathan || Vector3Distance(Q3(g.pos), eye) < NEAR_M)) idx.push_back(k); }
    int cnt = (int)idx.size(); a.i(cnt);
    if constexpr (A::reading) { if (cnt < 0 || cnt > na) { a.r.bad = true; return; } idx.resize(cnt); }
    int prev = -1;
    for (int j = 0; j < cnt && !a.bad(); j++) {
        int gap = idx[j] - prev; a.i(gap);
        if constexpr (A::reading) { idx[j] = prev + gap; if (idx[j] <= prev || idx[j] >= na) { a.r.bad = true; return; } }
        prev = idx[j];
        rt::Agent& g = w.eco.agents[idx[j]];
        a.i(g.sp); P16(a, g.pos); a.s8(g.vel.x, 12.7f); a.s8(g.vel.y, 12.7f); a.s8(g.vel.z, 12.7f);
        float hf = g.hpMax > 0 ? std::clamp(g.hp / g.hpMax, 0.0f, 1.0f) : 1; a.q8(hf, 1); a.q8(g.wound, 1);
        if constexpr (A::reading) {
            if (g.sp < 0 || g.sp >= (int)w.eco.map->species.size()) { a.r.bad = true; return; }
            g.alive = true; g.diver = -1; g.rng = (uint32_t)(idx[j] + 1) * 2654435761u;
            g.hpMax = std::max(1.0f, w.eco.map->species[g.sp].hpBase); g.hp = hf * g.hpMax;
        }
    }
    a.i(w.leviathan);
    std::vector<rt::Corpse> cs;
    if constexpr (!A::reading) for (const auto& c : w.eco.corpses) if (c.active && Vector3Distance(Q3(c.pos), eye) < NEAR_M) cs.push_back(c);
    a.vec(cs, [&](rt::Corpse& c) { a.i(c.sp); P16(a, c.pos); });
    if constexpr (A::reading) { w.eco.corpses = cs; for (auto& c : w.eco.corpses) c.active = true; }
}
}  // namespace

void WriteWorld(World& w, int viewer, Writer& out) {
    Out o{out};
    uint32_t magic = 0x3154464D; o.u(magic);   // "MFT1"
    uint32_t seed = w.opts.seed; o.u(seed);
    int humans = w.opts.humans, bots = w.opts.bots, lvl = w.opts.botLevel; float minutes = w.opts.minutes;
    o.i(humans); o.i(bots); o.i(lvl); o.f(minutes); o.i(viewer);
    Visit(o, w, viewer);
}
void PackWorld(World& w, int viewer, Writer& out) {
    Writer raw; WriteWorld(w, viewer, raw);
    int cz = 0;
    unsigned char* z = CompressData(raw.b.data(), (int)raw.b.size(), &cz);
    if (!z || cz <= 0) { out.Bytes(raw.b.data(), raw.b.size()); return; }
    out.U32(0x5A54464D); out.U32((uint32_t)raw.b.size()); out.Bytes(z, (size_t)cz);   // "MFTZ"
    MemFree(z);
}
bool ReadWorld(Reader& r, World& w, bool keepOwn, int* viewerOut) {
    In in{r};
    uint32_t magic = 0; in.u(magic);
    if (magic == 0x5A54464D) {
        uint32_t rawN = r.U32();
        if (r.bad || r.i >= r.n || rawN == 0 || rawN > (1u << 23)) return false;
        int outN = 0;
        unsigned char* raw = DecompressData(r.p + r.i, (int)(r.n - r.i), &outN);
        r.i = r.n;
        if (!raw) return false;
        bool ok = (uint32_t)outN == rawN;
        if (ok) { Reader inner(raw, (size_t)outN); ok = ReadWorld(inner, w, keepOwn, viewerOut) && !inner.bad; }
        MemFree(raw);
        return ok;
    }
    if (magic != 0x3154464D) return false;
    uint32_t seed = 0; int humans = 0, bots = 0, lvl = 0, viewer = 0; float minutes = 0;
    in.u(seed); in.i(humans); in.i(bots); in.i(lvl); in.f(minutes); in.i(viewer);
    if (r.bad || humans < 0 || humans > 12 || bots < 0 || bots > 12 || humans + bots > 12 || humans + bots < 1 || lvl < 0 || lvl > 3 || !(minutes > 0 && minutes <= 60) || viewer < 0 || viewer >= humans + bots) return false;
    if (!w.mirror || w.opts.seed != seed || w.opts.humans != humans || w.opts.bots != bots || w.opts.botLevel != lvl || w.opts.minutes != minutes) {
        std::string why;
        if (!rt::DataOk(&why)) return false;
        Opts o; o.humans = humans; o.bots = bots; o.botLevel = lvl; o.minutes = minutes; o.seed = seed;
        w.Init(o);
        w.mirror = true;
        keepOwn = false;
    }
    Mouth mine = viewer < (int)w.mouths.size() ? w.mouths[viewer] : Mouth{};
    Visit(in, w, viewer);
    if (r.bad) return false;
    if (viewerOut) *viewerOut = viewer;
    // the guest's own mouth: its swimming is its own prediction, eased toward where the host has it
    if (keepOwn && viewer < (int)w.mouths.size()) {
        Mouth& h = w.mouths[viewer];
        if (h.alive && mine.alive && Vector3Distance(mine.pos, h.pos) < 4) { h.pos = Vector3Lerp(mine.pos, h.pos, 0.12f); h.vel = mine.vel; h.yaw = mine.yaw; h.pitch = mine.pitch; h.bank = mine.bank; }
    }
    for (auto& m : w.mouths) if (m.agent >= 0 && m.agent < (int)w.eco.agents.size()) { w.eco.agents[m.agent].pos = m.pos; w.eco.agents[m.agent].alive = m.alive; w.eco.agents[m.agent].diver = m.id; }
    return true;
}

// ---------------------------------------------------------------- the host
namespace {
class MouthfulHost : public arcade::GameHost {
public:
    std::unique_ptr<World> w = std::make_unique<World>();
    int minutes = 15, botLevel = 0, fill = 12, players = 1;
    bool test = false; float stepDt = 1 / 30.0f;
    std::vector<Input> pend;
    float acc = 0; uint32_t tick = 0;
    mutable std::vector<std::vector<uint8_t>> cache = std::vector<std::vector<uint8_t>>(arcade::MAX_PLAYERS);
    mutable std::vector<uint32_t> cacheTick = std::vector<uint32_t>(arcade::MAX_PLAYERS, ~0u);
    void Configure(const std::string& opts) override {
        int m = 15, l = 0, f = 12;
        if (sscanf(opts.c_str(), "%d:%d:%d", &m, &l, &f) >= 1) { minutes = std::clamp(m, 1, 60); botLevel = std::clamp(l, 0, 3); fill = std::clamp(f, 1, 12); }
        test = opts.find(":test") != std::string::npos;
        size_t sp = opts.find(":step="); stepDt = sp != std::string::npos ? std::clamp((float)atof(opts.c_str() + sp + 6), 1 / 60.0f, 0.1f) : 1 / 30.0f;
    }
    void Start(int n, uint32_t seed) override {
        players = std::clamp(n, 1, arcade::MAX_PLAYERS);
        Opts o; o.humans = players; o.bots = std::max(0, fill - players); o.minutes = (float)minutes; o.botLevel = botLevel; o.seed = seed ? seed : 1;
        w = std::make_unique<World>();
        w->Init(o);
        for (int p = 0; p < players; p++) w->mouths[p].name = p == 0 ? "Host" : TextFormat("Player %d", p + 1);
        pend.assign(players, Input{});
        for (int p = 0; p < players; p++) pend[p].yaw = w->mouths[p].yaw;
        acc = 0; tick = 0; std::fill(cacheTick.begin(), cacheTick.end(), ~0u);
    }
    bool Act(int p, Reader& r) override {
        if (p < 0 || p >= players) return false;
        int kind = (int)r.U8();
        if (r.bad) return false;
        if (kind == MA_INPUT) {
            Input in; if (!ReadInput(r, in)) return false;
            // presses survive until a step uses them (a click between steps isn't lost)
            Input& q = pend[p]; bool ab = q.ability; int fk = q.fork;
            q = in; q.ability = in.ability || ab; if (in.fork < 0) q.fork = fk;
            return false;
        }
        if (kind == MA_HELLO) { std::string n = r.Str(); if (r.bad || n.empty()) return false; w->mouths[p].name = n; return true; }
        return false;
    }
    bool Tick(float dt, uint32_t ai) override {
        World& W = *w;
        // a person the AI stands in for swims as a Hunter bot; one who comes back swims again
        for (int p = 0; p < players; p++) { bool aiNow = (ai >> p) & 1; if (aiNow && !W.mouths[p].bot) { W.mouths[p].bot = true; W.mouths[p].botLevel = 2; } else if (!aiNow && W.mouths[p].bot) W.mouths[p].bot = false; }
        acc += std::min(dt, 0.25f);
        int steps = 0;
        while (acc >= stepDt && steps < 8) {
            acc -= stepDt; steps++;
            for (int p = 0; p < players; p++) if (!W.mouths[p].bot) W.mouths[p].in = pend[p];
            W.Step(stepDt);
            for (auto& q : pend) { q.ability = false; q.fork = -1; }
            tick++;
        }
        return steps > 0;
    }
    void Snapshot(int viewer, Writer& out) const override {
        if (viewer < 0 || viewer >= players) viewer = 0;
        if (viewer == 0 && !test) { out.U32(0x3048464D); return; }   // "MFH0": the host's own screen draws the real world
        if (cacheTick[viewer] != tick) { Writer t; PackWorld(*w, viewer, t); cache[viewer] = std::move(t.b); cacheTick[viewer] = tick; }
        out.Bytes(cache[viewer].data(), cache[viewer].size());
    }
    bool Over() const override { return w->over; }
};
}  // namespace
std::unique_ptr<arcade::GameHost> MakeMouthfulHost() { return std::make_unique<MouthfulHost>(); }
World* MouthfulHostWorld(arcade::GameHost* h) { auto* m = dynamic_cast<MouthfulHost*>(h); return m ? m->w.get() : nullptr; }
std::string MouthfulHostOpts(int minutes, int botLevel, int fill) { return TextFormat("%d:%d:%d", minutes, botLevel, fill); }
uint32_t MouthfulDataHash() {
    // every rule a peer must share: the tiers, every form, the prey's worth, the scoring
    const Data& d = D();
    Writer w;
    for (int t = 1; t <= 8; t++) { w.F32(d.tiers[t].mass); w.F32(d.tiers[t].length); w.F32(d.tiers[t].speed); w.F32(d.tiers[t].biteCd); w.F32(d.tiers[t].turn); }
    for (const auto& f : d.forms) { w.Str(f.key); w.I32(f.path); w.I32(f.tier); w.F32(f.speed); w.F32(f.hp); w.F32(f.bite); w.F32(f.reach); w.U8(f.ab); w.U8(f.ps); w.F32(f.cd); w.F32(f.p1); w.F32(f.p2); w.U8(f.walker); }
    for (const auto& p : d.prey) { w.Str(p.species); w.F32(p.mass); w.U8(p.armored); w.U8(p.npc); w.I32(p.eatsUpTo); w.F32(p.bite); }
    w.F32(d.swallowBelow); w.F32(d.fightPct); w.F32(d.decayPerMin); w.F32(d.startMass); w.F32(d.scoreKill); w.F32(d.scoreCrownPerS);
    return Fnv1a(w.b.data(), w.b.size());
}

// ---------------------------------------------------------------- checks
int RunMouthfulNetTest() {
    int fails = 0;
    auto check = [&](bool ok, const char* what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) fails++; };
    printf("Mouthful: the network layer\n");
    Input in; in.yaw = 1.25f; in.pitch = -0.3f; in.swim = true; in.bite = true; in.fork = 2;
    { Writer w; WriteInput(in, w); Reader r(w.b); r.U8(); Input o; check(ReadInput(r, o) && o.yaw == 1.25f && o.swim && o.bite && !o.boost && o.fork == 2, "an input frame round-trips"); }
    World W; Opts o; o.humans = 2; o.bots = 10; o.seed = 99; o.minutes = 10;
    W.Init(o);
    for (int i = 0; i < 300; i++) W.Step(1 / 20.0f);
    auto mir = std::make_unique<World>();
    { Writer a; WriteWorld(W, 1, a); Reader r(a.b); int v = -1; check(ReadWorld(r, *mir, false, &v) && v == 1 && mir->mirror && mir->mouths.size() == 12, "a guest builds the mirror from the first snapshot"); }
    { Writer a; WriteWorld(W, 1, a); Writer b; WriteWorld(*mir, 1, b); check(a.b == b.b, TextFormat("the mirror writes back byte for byte (%d bytes)", (int)a.b.size())); }
    { Writer a; PackWorld(W, 1, a); Reader r(a.b); check(ReadWorld(r, *mir, true) && mir->mouths[1].name == W.mouths[1].name, TextFormat("a packed snapshot reads (%.1f KB)", a.b.size() / 1024.0f)); }
    // the host: a guest's input swims its mouth
    auto host = MakeMouthfulHost();
    host->Configure("10:2:12:test:step=0.05");
    host->Start(2, 1234);
    World& H = *MouthfulHostWorld(host.get());
    Vector3 p0 = H.mouths[1].pos;
    for (int k = 0; k < 100; k++) { Input g; g.yaw = 0; g.swim = true; Writer w; WriteInput(g, w); Reader r(w.b); host->Act(1, r); host->Tick(0.05f, 0); }
    check(Vector3Distance(H.mouths[1].pos, p0) > 8, TextFormat("a guest's input swims their mouth (%.0f m)", Vector3Distance(H.mouths[1].pos, p0)));
    { Writer w; OrderHello(w, "Fishface"); Reader r(w.b); host->Act(1, r); check(H.mouths[1].name == "Fishface", "a name arrives with hello"); }
    { Writer s; host->Snapshot(1, s); Reader r(s.b); auto g = std::make_unique<World>(); check(ReadWorld(r, *g, false) && g->mouths[1].name == "Fishface", "the guest's snapshot shows it"); }
    host->Tick(0.05f, 2);
    check(H.mouths[1].bot, "a lost guest's mouth swims on as a bot");
    printf(fails ? "Mouthful net: %d FAILED\n" : "Mouthful net: all checks passed\n", fails);
    return fails ? 1 : 0;
}

int RunMouthfulNetLoop(bool forceMemory) {
    // the doc's gate (p. 21): six players finish a 15-minute round on LAN. A host and five guests over loopback (or the
    // in-memory transport, fast), every guest's mirror read, the round to its end, everyone agreeing on the winner
    using namespace arcade;
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::string err;
    if (!rt::DataOk(&err)) { printf("FAIL: no data: %s\n", err.c_str()); return 1; }
    bool real = !forceMemory && net::Init(&err);
    auto make = [&]() { return real ? net::MakeTransport() : net::MakeMemoryTransport(); };
    uint16_t port = 47830;
    std::string addr = real ? "127.0.0.1:" + std::to_string(port) : "mem:" + std::to_string(port);
    int minutes = getenv("DEPTH_MF_MINUTES") ? std::max(1, atoi(getenv("DEPTH_MF_MINUTES"))) : real ? 2 : 15;
    printf("net-loop mouthful over %s: a host and five guests (and six bots), a %d-minute round\n", real ? "GameNetworkingSockets (loopback UDP)" : "the in-memory transport", minutes);
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    const int NG = 5;
    Session host, gs[NG];
    Profile ph{"Host", 70};
    if (!host.Host(ph, G_MOUTHFUL, &err, port, make(), false)) { printf("FAIL: host: %s\n", err.c_str()); return 1; }
    host.gameOpts = TextFormat("%d:2:12:test%s", minutes, real ? "" : ":step=0.1");
    const float dt = real ? 1 / 30.0f : 0.1f;
    double t = 0;
    std::vector<std::unique_ptr<World>> mirror; for (int k = 0; k < NG; k++) mirror.push_back(std::make_unique<World>());
    int seen[NG] = {}, mirrorOk[NG] = {}, readFails = 0, snaps = 0; size_t bytes = 0, biggest = 0;
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
    for (int k = 0; k < NG; k++) { Profile p{std::string("Fish ") + char('A' + k), (uint64_t)(80 + k)}; if (!gs[k].Join(p, addr, &err, 0, make())) { printf("FAIL: join: %s\n", err.c_str()); return 1; } }
    until([&] { for (auto& g : gs) if (g.stage != S_LOBBY) return false; return true; }, 15);
    for (auto& g : gs) g.SetReady(true);
    until([&] { for (int s = 1; s <= NG; s++) if (!host.seats[s].ready) return false; return true; }, 10);
    std::string why;
    check(host.Launch(&why), "six mouths launch Mouthful" + (why.empty() ? std::string() : ": " + why));
    until([&] { for (int k = 0; k < NG; k++) if (mirrorOk[k] == 0) return false; return true; }, 20);
    World& truth = *MouthfulHostWorld(host.HostGame());
    bool all = true; for (int k = 0; k < NG; k++) all = all && mirror[k]->mouths.size() == 12;
    check(all && truth.mouths.size() == 12, "every guest mirrors the twelve mouths (six people, six bots)");
    for (int k = 0; k < NG; k++) { Writer o; OrderHello(o, std::string("Fish ") + char('A' + k)); gs[k].Act(o); }
    step(5);
    int me = gs[0].MyPlayer();
    check(me >= 1 && truth.mouths[me].name == "Fish A", "names arrive");
    // the people: guest 0 swims by hand; the rest let a bot brain choose their input (a stand-in for a person)
    World brain; Opts bo; bo.humans = 0; bo.bots = 12; bo.seed = 5; bo.minutes = (float)minutes;
    Vector3 start = truth.mouths[me].pos; float far = 0;
    int lastMin = -1;
    while (!truth.over && t < minutes * 60 + 120) {
        for (int k = 0; k < NG; k++) {
            World& m = *mirror[k];
            int p = gs[k].MyPlayer();
            if (p < 0 || p >= (int)m.mouths.size()) continue;
            Input in;
            if (k == 0 && truth.time < 20) { in.yaw = 0.3f; in.swim = true; }
            else { m.mirror = false; m.StepBot(m.mouths[p], dt); m.mirror = true; in = m.mouths[p].in; }   // (the bot's mind on the mirror)
            Writer w; WriteInput(in, w); gs[k].Act(w);
        }
        { Input in; in.swim = true; in.yaw = truth.mouths[0].yaw + 0.2f; Writer w; WriteInput(in, w); host.Act(w); }
        step(1);
        far = std::max(far, Vector3Distance(truth.mouths[me].pos, start));
        int mm = (int)(truth.time / 60);
        if (mm != lastMin && mm % 5 == 0) { lastMin = mm; printf("    [%2d min] leader %s (tier %d), %d snapshots read\n", mm, truth.mouths[truth.Leader()].name.c_str(), truth.mouths[truth.Leader()].tier, snaps); }
    }
    check(far > 20, TextFormat("guest 0 swims their mouth by input (%.0f m)", far));
    check(truth.over, "the round ends");
    step(20);
    bool agree = true; for (int k = 0; k < NG; k++) agree = agree && mirror[k]->over && mirror[k]->winner == truth.winner;
    check(agree, TextFormat("every guest sees the same end: %s wins", truth.winner >= 0 ? truth.mouths[truth.winner].name.c_str() : "nobody"));
    int grew = 0; for (int p = 1; p <= NG; p++) grew += truth.mouths[p].bestTier >= 3;
    check(grew >= 3, TextFormat("the people grew (%d of 5 guests reached tier 3+)", grew));
    check(readFails == 0 && snaps > 0, TextFormat("%d snapshots, none refused; %.1f KB on average, %.1f KB the biggest", snaps, snaps ? bytes / 1024.0 / snaps : 0.0, biggest / 1024.0));
    for (auto& g : gs) g.Leave();
    host.Leave();
    step(5);
    if (real) net::Shutdown();
    printf(fails ? "%d FAILED\n" : "net-loop mouthful: all checks passed\n", fails);
    return fails ? 1 : 0;
}

}  // namespace mf
