// Scuffle's network layer (see scuffle_net.h): the inputs, the snapshot Visit, the guest's prediction, the host, and
// the checks (--scuffle-net-test, --net-loop scuffle).
#include "scuffle_net.h"
#include "arcade_session.h"
#include "net.h"
#include "raymath.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <thread>

namespace sf {

// ---------------------------------------------------------------- inputs
static int Q127(float v) { return std::clamp((int)lroundf(v * 127), -127, 127); }
static uint32_t QAng(Vector2 a) { float t = atan2f(a.y, a.x); if (t < 0) t += 2 * PI; return (uint32_t)lroundf(t / (2 * PI) * 65536) & 0xFFFF; }
static Vector2 DeAng(uint32_t q) { float t = q / 65536.0f * 2 * PI; return {cosf(t), sinf(t)}; }
Input QuantizeInput(const Input& in) {
    Input o = in;
    o.moveX = Q127(in.moveX) / 127.0f; o.moveY = Q127(in.moveY) / 127.0f;
    o.aim = Vector2Length(in.aim) > 1e-4f ? DeAng(QAng(in.aim)) : Vector2{1, 0};
    return o;
}
static void PutInput(Writer& w, const Input& in) {
    w.U8((uint32_t)(uint8_t)(int8_t)Q127(in.moveX)); w.U8((uint32_t)(uint8_t)(int8_t)Q127(in.moveY));
    w.U16(Vector2Length(in.aim) > 1e-4f ? QAng(in.aim) : 0);
    w.U8((in.jump ? 1 : 0) | (in.fire ? 2 : 0) | (in.taunt ? 4 : 0) | (in.gear ? 8 : 0) | ((std::clamp(in.pick, 0, 3)) << 4));
}
static Input GetInput(Reader& r) {
    Input in;
    in.moveX = (int8_t)(uint8_t)r.U8() / 127.0f; in.moveY = (int8_t)(uint8_t)r.U8() / 127.0f;
    in.aim = DeAng(r.U16());
    int f = (int)r.U8(); in.jump = f & 1; in.fire = f & 2; in.taunt = f & 4; in.gear = f & 8; in.pick = (f >> 4) & 3;
    in.moveX = std::clamp(in.moveX, -1.0f, 1.0f); in.moveY = std::clamp(in.moveY, -1.0f, 1.0f);
    return in;
}
void WriteInputs(const std::vector<InputFrame>& frames, Writer& w) {
    // (consecutive frames: the first's number, then each input)
    size_t n = std::min<size_t>(frames.size(), 60), at = frames.size() - n;
    w.U8(SA_INPUTS); w.U8((uint32_t)n); w.U32(n ? frames[at].seq : 0);
    for (size_t k = at; k < frames.size(); k++) PutInput(w, frames[k].in);
}
bool ReadInputs(Reader& r, std::vector<InputFrame>& out) {
    int n = (int)r.U8(); uint32_t first = r.U32();
    if (r.bad || n > 60) return false;
    for (int k = 0; k < n; k++) { InputFrame f; f.seq = first + k; f.in = GetInput(r); if (r.bad) return false; out.push_back(f); }
    return true;
}
void OrderHello(Writer& w, const std::string& name, int trinket, int skin, int hat) { w.U8(SA_HELLO); w.Str(name.substr(0, 20)); w.U8((uint32_t)(trinket + 1)); w.U8((uint32_t)(skin + 1)); w.U8((uint32_t)(hat + 1)); }

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
    void v2(Vector2& v) { f(v.x); f(v.y); }
    template <class T, class F> void vec(std::vector<T>& v, F fn, uint32_t = 0) { w.VarU((uint32_t)v.size()); for (auto& x : v) fn(x); }
    bool bad() const { return false; }
};
struct In {
    Reader& r;
    static constexpr bool reading = true;
    void f(float& v) { v = r.F32(); if (!std::isfinite(v)) { v = 0; r.bad = true; } }
    void i(int& v) { uint32_t z = r.VarU(); v = (int)((z >> 1) ^ (0u - (z & 1))); }
    void b(bool& v) { v = r.U8() != 0; }
    void u(uint32_t& v) { v = r.U32(); }
    void s(std::string& v) { v = r.Str(); }
    void v2(Vector2& v) { f(v.x); f(v.y); }
    template <class T, class F> void vec(std::vector<T>& v, F fn, uint32_t max = 8192) { uint32_t n = r.VarU(); if (n > max || r.bad) { r.bad = true; n = 0; } v.resize(n); for (auto& x : v) { fn(x); if (r.bad) break; } }
    bool bad() const { return r.bad; }
};
template <class A> void VisitP(A& a, Particle& p) { a.v2(p.p); a.v2(p.q); a.f(p.r); a.f(p.invMass); }
template <class A> void VisitInput(A& a, Input& in) { a.f(in.moveX); a.f(in.moveY); a.v2(in.aim); a.b(in.jump); a.b(in.fire); a.b(in.taunt); a.b(in.gear); a.i(in.pick); }
template <class A> void VisitStick(A& a, Stick& k) {
    a.i(k.id); a.b(k.alive); a.b(k.present); a.f(k.hp);
    a.v2(k.pos); a.v2(k.vel); a.f(k.halfW); a.f(k.height);
    int st = k.st; a.i(st); if constexpr (A::reading) { if (st < 0 || st > S_DEAD) a.r.bad = true; k.st = (StickState)std::clamp(st, 0, (int)S_DEAD); }
    a.i(k.face); a.b(k.grounded); a.b(k.wallLeft); a.b(k.wallRight); a.i(k.wallSide);
    a.f(k.climbT); a.f(k.coyoteT); a.f(k.jumpHeldT); a.f(k.stunT); a.f(k.ragT); a.f(k.getUpT); a.f(k.fallTop); a.f(k.proneT); a.f(k.hitT); a.f(k.knockT); a.f(k.grabWait);
    a.b(k.jumpWas); a.b(k.fireWas);
    for (auto& p : k.pt) VisitP(a, p);
    a.f(k.stiff);
    a.f(k.punchT); a.f(k.punchCool); a.f(k.comboT); a.i(k.combo); a.b(k.punchHay); a.i(k.punchArm); a.vec(k.punchHit, [&](int& x) { a.i(x); }, 64);
    a.i(k.grabbing); a.i(k.grabbedBy); a.f(k.holdT); a.f(k.thrownT); a.i(k.thrownBy);
    a.f(k.kickT); a.f(k.tauntT);
    a.i(k.weapon); a.f(k.fireCool); a.f(k.spin); a.f(k.swingT); a.b(k.swingHay); a.vec(k.swingHit, [&](int& x) { a.i(x); }, 64); a.f(k.blockT);
    for (int& x : k.killsBy) a.i(x);
    a.f(k.walkPh); a.f(k.breathe);
    a.f(k.swimT); a.f(k.hazT); a.f(k.sharkT); a.b(k.wet);
    a.f(k.burnT); a.f(k.frozenT); a.f(k.bubbleT); a.f(k.netT); a.f(k.gravT); a.f(k.trapT);
    a.i(k.gear); a.f(k.gearFuel); a.f(k.gearCool); a.v2(k.hook); a.b(k.hookOn); a.b(k.gearWas);
    a.i(k.trinket); a.f(k.size); a.b(k.airJump); a.b(k.windUsed); a.i(k.carry); a.f(k.aimT); a.f(k.steadyT);
    a.i(k.team); a.b(k.shark); a.f(k.respawnT); a.f(k.finished); a.f(k.balloonT); a.i(k.persona);
    a.i(k.skin); a.i(k.hat);
    a.i(k.kills); a.i(k.lastHitBy); a.f(k.lastHitT); a.s(k.cause);
    VisitInput(a, k.in);
}
template <class A> void VisitStage(A& a, Stage& s) {
    a.i(s.w); a.i(s.h);
    if constexpr (A::reading) if (s.w < 4 || s.h < 4 || s.w > 256 || s.h > 128) { a.r.bad = true; return; }
    a.vec(s.t, [&](uint8_t& x) { int v = x; a.i(v); if constexpr (A::reading) { if (v < 0 || v >= T_COUNT) a.r.bad = true; } x = (uint8_t)v; }, 256 * 128);
    if constexpr (A::reading) if ((int)s.t.size() != s.w * s.h) { a.r.bad = true; return; }
    a.vec(s.spawns, [&](Vector2& p) { a.v2(p); }, MAX_STICKS * 2);
    a.vec(s.pieces, [&](Piece& p) {
        int kind = p.kind; a.i(kind); if constexpr (A::reading) { if (kind < 0 || kind >= PK_COUNT) a.r.bad = true; p.kind = (uint8_t)std::clamp(kind, 0, PK_COUNT - 1); }
        a.i(p.x); a.i(p.y); a.i(p.w); a.i(p.h); a.i(p.dx); a.i(p.dy);
        a.f(p.travel); a.f(p.period); a.f(p.phase); a.f(p.on); a.f(p.power); a.v2(p.off); a.v2(p.prevOff); a.f(p.cool); a.b(p.broken);
        a.f(p.start); a.f(p.prog); a.i(p.hold);
    }, 256);
    a.vec(s.crateCols, [&](int& c) { a.i(c); }, 256);
    a.s(s.name); a.s(s.author); a.i(s.world); a.b(s.wrap); a.b(s.finale);
}
template <class A> void VisitWorld(A& a, World& w) {
    VisitStage(a, w.stage);
    if (a.bad()) return;
    a.vec(w.sticks, [&](Stick& k) { VisitStick(a, k); }, MAX_STICKS);
    a.u(w.rng); a.u(w.frame); a.f(w.t); a.f(w.gravity);
    // the events: the last 32 and where they start (the scene splashes by cursor)
    std::vector<Event> tail; uint32_t base = w.eventBase;
    if constexpr (!A::reading) { size_t k = std::min<size_t>(w.events.size(), 32); tail.assign(w.events.end() - k, w.events.end()); base = w.eventBase + (uint32_t)(w.events.size() - k); }
    a.u(base);
    a.vec(tail, [&](Event& e) { a.i(e.kind); a.v2(e.at); a.f(e.a); a.i(e.who); a.i(e.by); }, 64);
    if constexpr (A::reading) { w.events = tail; w.eventBase = base; }
    a.vec(w.items, [&](Item& it) {
        a.b(it.alive); a.b(it.crate); a.b(it.chute); a.b(it.shot); a.i(it.weapon); a.i(it.ammo); a.i(it.holder); a.i(it.count);
        VisitP(a, it.a); VisitP(a, it.b); a.f(it.age); a.f(it.thrownT); a.i(it.thrownBy);
    }, 4096);
    a.vec(w.bullets, [&](Bullet& b) {
        a.v2(b.p); a.v2(b.v); a.i(b.owner); a.i(b.weapon); a.i(b.pierce); a.i(b.bounces);
        a.f(b.dmg); a.f(b.knock); a.f(b.life); a.f(b.grav); a.f(b.area); a.f(b.areaDmg); a.f(b.fuse); a.f(b.age);
        a.b(b.explode); a.b(b.alive); a.b(b.deflected); a.vec(b.hit, [&](int& x) { a.i(x); }, 64); a.i(b.hazard);
    }, 4096);
    a.f(w.nextCrate); a.f(w.wallY); a.i(w.arsenal); a.b(w.finale); a.b(w.wallOn); a.i(w.crates);
    a.f(w.ceilY); a.f(w.sideX); a.i(w.wallSide);
    a.vec(w.things, [&](Thing& th) {
        int kind = th.kind; a.i(kind); if constexpr (A::reading) { if (kind < 0 || kind >= TH_COUNT) a.r.bad = true; th.kind = (uint8_t)std::clamp(kind, 0, TH_COUNT - 1); }
        a.b(th.alive); a.v2(th.p); a.v2(th.v); a.v2(th.q); a.f(th.life); a.f(th.t); a.f(th.a); a.f(th.cool); a.i(th.owner); a.i(th.on); a.i(th.hold); a.i(th.weapon);
    }, 2048);
    a.vec(w.fireT, [&](float& f) { a.f(f); }, 256 * 128); a.f(w.inkT); a.f(w.flashT);
    a.u(w.mut); a.i(w.event); a.f(w.eventAt); a.i(w.leader); a.f(w.floodY); a.f(w.lightsT); a.f(w.reachX); a.i(w.reachRow); a.b(w.moversStopped); a.i(w.luckyFor);
    a.vec(w.glassT, [&](float& g) { a.f(g); }, 256 * 128);
    a.i(w.mode); a.b(w.friendlyFire); a.vec(w.pts, [&](float& p) { a.f(p); }, MAX_STICKS);
    a.f(w.plank.x); a.f(w.plank.y); a.f(w.plank.width); a.f(w.plank.height); a.f(w.plankT); a.f(w.potatoNext); a.v2(w.goal); a.i(w.start);
    {   // stage 8: the boss
        Boss& B = w.boss;
        a.i(B.kind); a.f(B.hp); a.f(B.maxHp); a.i(B.phase); a.b(B.dead);
        a.f(B.t); a.f(B.actT); a.f(B.next); a.f(B.hitT); a.f(B.roarT); a.f(B.deadT);
        a.i(B.act); a.i(B.target); a.i(B.face); a.i(B.side); a.i(B.count);
        a.v2(B.pos); a.v2(B.at); a.v2(B.from); for (float& x : B.s) a.f(x);
        a.vec(B.parts, [&](BossPart& p) { a.v2(p.p); a.f(p.r); a.f(p.mul); a.f(p.hurt); }, 64);
        a.vec(B.chain, [&](Vector2& p) { a.v2(p); }, 1024);
        a.vec(B.marks, [&](Vector3& m) { a.f(m.x); a.f(m.y); a.f(m.z); }, 64);
        a.vec(B.danger, [&](Rectangle& r) { a.f(r.x); a.f(r.y); a.f(r.width); a.f(r.height); }, 64);
        for (float& x : B.dmgBy) a.f(x);
        if constexpr (A::reading) if (B.kind < -1 || B.kind >= BK_COUNT || B.target < -1 || B.target >= MAX_STICKS) a.r.bad = true;
    }
    if constexpr (A::reading) {   // (indices that the step follows: all must point somewhere real)
        int ns = (int)w.sticks.size(), ni = (int)w.items.size(), nw = (int)Weapons().size();
        for (int s = 0; s < ns; s++) { const Stick& k = w.sticks[s]; if (k.id != s || k.weapon < -1 || k.weapon >= ni || k.grabbing < -1 || k.grabbing >= ns || k.grabbedBy < -1 || k.grabbedBy >= ns) a.r.bad = true; }
        for (const auto& it : w.items) if (it.weapon < -1 || it.weapon >= nw || it.holder < -1 || it.holder >= ns) a.r.bad = true;
        for (const auto& b : w.bullets) if (b.weapon < -1 || b.weapon >= nw || b.hazard < -2 || b.hazard >= PK_COUNT) a.r.bad = true;
        for (const auto& th : w.things) if (th.on < -1 || th.on >= ns || th.weapon < -1 || th.weapon >= nw) a.r.bad = true;
        for (const auto& k : w.sticks) if (k.gear < -1 || k.gear >= GR_COUNT || k.carry < -1 || k.carry >= ni || k.trinket < 0 || k.trinket >= TK_COUNT || !(k.size > 0.1f && k.size < 4)) a.r.bad = true;
        if (w.event < -1 || w.event >= RE_COUNT) a.r.bad = true;
        if (w.mode < 0 || w.mode >= MD_COUNT) a.r.bad = true;
        for (const auto& k : w.sticks) if (k.team < -1 || k.team >= MAX_STICKS || k.persona < 0 || k.persona >= PE_COUNT) a.r.bad = true;
        for (const auto& p : w.stage.pieces) if ((p.kind == PK_REACHER || p.kind == PK_BOUNCER) && (p.hold < -1 || p.hold >= ns)) a.r.bad = true;   // (others keep a cycle count there)
        if (!w.glassT.empty() && w.glassT.size() != w.stage.t.size()) a.r.bad = true;
    }
}
template <class A> void VisitMatch(A& a, Match& m) {
    a.i(m.players); a.i(m.toWin); a.i(m.round); a.i(m.arsenal); a.u(m.seed);
    if constexpr (A::reading) if (m.players < 1 || m.players > MAX_STICKS) { a.r.bad = true; return; }
    a.vec(m.wins, [&](int& x) { a.i(x); }, MAX_STICKS); a.vec(m.score, [&](int& x) { a.i(x); }, MAX_STICKS); a.vec(m.roundKills, [&](int& x) { a.i(x); }, MAX_STICKS);
    a.i(m.stageIdx);
    int ph = m.phase; a.i(ph); if constexpr (A::reading) { if (ph < 0 || ph > Match::P_OVER) a.r.bad = true; m.phase = (Match::Phase)std::clamp(ph, 0, (int)Match::P_OVER); }
    a.f(m.phaseT); a.i(m.roundWinner); a.i(m.champion); a.i(m.draws); a.u(m.evSeen);
    a.i(m.mode); a.i(m.teamSize); a.b(m.friendlyFire); a.b(m.wallOn); a.f(m.target);
    for (int& x : m.duelOffer) a.i(x); for (int& x : m.duelPick) a.i(x);
    a.i(m.gStage); a.i(m.gDeaths); a.i(m.gWorld); a.f(m.gTime); a.b(m.gFailed);
    if constexpr (A::reading) { int nw = (int)Weapons().size(); for (int x : m.duelOffer) if (x < -1 || x >= nw) a.r.bad = true; if (m.mode < 0 || m.mode >= MD_COUNT) a.r.bad = true; }
    VisitWorld(a, m.w);
    if constexpr (A::reading) if ((int)m.wins.size() != m.players || (int)m.score.size() != m.players || (int)m.w.sticks.size() < m.players) a.r.bad = true;
}
constexpr uint32_t MAGIC = 0x314E4353, MAGIC_Z = 0x5A4E4353, MAGIC_HOST = 0x484E4353;   // "SCN1", "SCNZ", "SCNH"
}  // namespace

void WriteMatch(Match& m, std::vector<std::string>& names, int viewer, uint32_t ack, Writer& out) {
    Out o{out};
    uint32_t magic = MAGIC; o.u(magic); o.i(viewer); o.u(ack);
    o.vec(names, [&](std::string& s) { o.s(s); });
    VisitMatch(o, m);
}
void PackMatch(Match& m, std::vector<std::string>& names, int viewer, uint32_t ack, Writer& out) {
    Writer raw; WriteMatch(m, names, viewer, ack, raw);
    int cz = 0;
    unsigned char* z = CompressData(raw.b.data(), (int)raw.b.size(), &cz);
    if (!z || cz <= 0) { out.Bytes(raw.b.data(), raw.b.size()); return; }
    out.U32(MAGIC_Z); out.U32((uint32_t)raw.b.size()); out.U32(Fnv1a(z, (size_t)cz)); out.Bytes(z, (size_t)cz);
    MemFree(z);
}
bool ReadMatch(Reader& r, Match& m, std::vector<std::string>& names, int* viewerOut, uint32_t* ackOut) {
    In in{r};
    uint32_t magic = 0; in.u(magic);
    if (magic == MAGIC_Z) {
        uint32_t rawN = r.U32(), sum = r.U32();
        if (r.bad || r.i >= r.n || rawN == 0 || rawN > (1u << 22)) return false;
        if (Fnv1a(r.p + r.i, r.n - r.i) != sum) return false;   // (checked before inflating: garbage never reaches the decompressor)
        int outN = 0;
        unsigned char* raw = DecompressData(r.p + r.i, (int)(r.n - r.i), &outN);
        r.i = r.n;
        if (!raw) return false;
        bool ok = (uint32_t)outN == rawN;
        if (ok) { Reader inner(raw, (size_t)outN); ok = ReadMatch(inner, m, names, viewerOut, ackOut) && !inner.bad; }
        MemFree(raw);
        return ok;
    }
    if (magic != MAGIC) return false;
    int viewer = 0; uint32_t ack = 0; in.i(viewer); in.u(ack);
    std::vector<std::string> nm; in.vec(nm, [&](std::string& s) { in.s(s); }, MAX_STICKS);
    if (r.bad || viewer < 0 || viewer >= MAX_STICKS) return false;
    Match t = m;   // (read into a copy: a bad snapshot never leaves a half-read match)
    VisitMatch(in, t);
    if (r.bad || viewer >= t.players) return false;
    m = std::move(t); names = std::move(nm);
    if (viewerOut) *viewerOut = viewer;
    if (ackOut) *ackOut = ack;
    return true;
}
void StepMirror(Match& m) {
    if (m.phase == Match::P_OVER) return;
    if (m.phase == Match::P_COUNT) {   // (the countdown holds everyone still, as on the host)
        std::vector<Input> keep; for (auto& k : m.w.sticks) { keep.push_back(k.in); k.in = Input{}; }
        m.w.Step();
        for (size_t i = 0; i < keep.size(); i++) m.w.sticks[i].in = keep[i];
        if ((m.phaseT -= STEP) <= 0) m.phase = Match::P_FIGHT;
        return;
    }
    m.w.Step();
    if (m.phase == Match::P_WIN) m.phaseT = std::max(0.0f, m.phaseT - STEP);
}

// ---------------------------------------------------------------- the guest's prediction
void Predictor::Local(const Input& raw, std::vector<InputFrame>& outbox) {
    if (!have || me < 0 || me >= (int)m.w.sticks.size()) return;
    InputFrame f; f.seq = nextSeq++; f.in = QuantizeInput(raw);
    hist.push_back(f); if (hist.size() > 360) hist.pop_front();
    outbox.push_back(f);
    m.w.sticks[me].in = f.in;
    StepMirror(m);
}
bool Predictor::Apply(Reader& r) {
    bool had = have && me >= 0 && me < (int)m.w.sticks.size();
    Vector2 before = had ? m.w.sticks[me].pt[J_PELVIS].p : Vector2{}; int roundBefore = m.round;
    int predSt = had ? (int)m.w.sticks[me].st : -1; float predHit = had ? m.w.t - m.w.sticks[me].lastHitT : 0;
    int viewer = -1; uint32_t a = 0;
    if (!ReadMatch(r, m, names, &viewer, &a)) return false;
    have = true; me = viewer; snaps++;
    if (a > ack) ack = a;
    hostEvents = m.w.events; hostEventBase = m.w.eventBase;
    {   // (free: nothing could touch my stick in the next moment: no one within 3 m, no shot or blast nearby)
        const Stick& k = m.w.sticks[me]; Vector2 c = k.pt[J_PELVIS].p;
        lastFree = k.alive && k.st != S_RAGDOLL && m.w.t - k.lastHitT > 1.0f && k.grabbedBy < 0 && k.grabbing < 0 && k.stunT <= 0 && m.phase == Match::P_FIGHT;
        for (const auto& o : m.w.sticks) if (o.id != me && o.present && Vector2Distance(o.pt[J_PELVIS].p, c) < 3.0f) lastFree = false;
        for (const auto& b : m.w.bullets) if (b.alive && Vector2Distance(b.p, c) < (b.explode ? 6.0f : 3.0f)) lastFree = false;
    }
    while (!hist.empty() && hist.front().seq <= ack) hist.pop_front();
    // everyone else is carried on their last movement but never their last attack: a punch or a shot the host hasn't seen
    // would hit me here and be taken back a moment later (the worst kind of correction); real attacks arrive with the host
    for (auto& k : m.w.sticks) if (k.id != me) { k.in.fire = false; k.in.taunt = false; }
    Stick hostOwn = m.w.sticks[me];
    for (const auto& f : hist) { m.w.sticks[me].in = f.in; StepMirror(m); }   // (my inputs the host hasn't played yet)
    lastCorrection = had && roundBefore == m.round ? Vector2Distance(before, m.w.sticks[me].pt[J_PELVIS].p) : 0;
    if (m.w.sticks[me].hp != hostOwn.hp || !m.w.sticks[me].alive) lastFree = false;   // (the replay itself was hit: not free)
    if (getenv("DEPTH_NETTRACE") && lastFree && lastCorrection > 0.1f) {
        const Stick& k = m.w.sticks[me];
        printf("      corr %.2f me %d t %.2f hist %d st %d->%d gr %d w %d hp %.0f host pel (%.2f %.2f) pred-before (%.2f %.2f) after (%.2f %.2f) predSt %d predHit %.2f\n", lastCorrection, me, m.w.t, (int)hist.size(), (int)hostOwn.st, (int)k.st, (int)k.grounded, k.weapon, k.hp, hostOwn.pt[J_PELVIS].p.x, hostOwn.pt[J_PELVIS].p.y, before.x, before.y, k.pt[J_PELVIS].p.x, k.pt[J_PELVIS].p.y, predSt, predHit);
    }
    return true;
}

// ---------------------------------------------------------------- the host
static int gUnder = 0, gDrop = 0, gUsed = 0;   // (the input queues: steps with no input waiting, inputs dropped, inputs played)
namespace {
class ScuffleHost : public arcade::GameHost {
public:
    Match m; std::vector<std::string> names;
    int toWin = 5, arsenal = AR_CLASSIC, skill = 2, players = 2, world = -1; uint32_t muts = 0; bool randomMut = false, test = false; int mode = 0;
    struct Seat { std::deque<InputFrame> q; Input cur; uint32_t ack = 0, last = 0; double heard = -10; uint32_t rng = 1; };
    std::vector<Seat> seats;
    float acc = 0; double time = 0; uint32_t tick = 0;
    mutable std::vector<std::vector<uint8_t>> cache = std::vector<std::vector<uint8_t>>(MAX_STICKS);
    mutable std::vector<uint32_t> cacheTick = std::vector<uint32_t>(MAX_STICKS, ~0u);
    void Configure(const std::string& opts) override {
        int t = 5, a = 0, s = 2, wd = -1, rnd = 0, md = 0; unsigned mu = 0;
        if (sscanf(opts.c_str(), "%d:%d:%d:%d:%u:%d:%d", &t, &a, &s, &wd, &mu, &rnd, &md) >= 1) { mode = std::clamp(md, 0, MD_COUNT - 1); toWin = std::clamp(t, 1, 20); arsenal = std::clamp(a, 0, AR_COUNT - 1); skill = std::clamp(s, 0, 2); world = std::clamp(wd, -1, (int)WD_COUNT); muts = mu & ((1u << MU_COUNT) - 1); randomMut = rnd != 0; }
        test = opts.find(":test") != std::string::npos;
    }
    void Start(int n, uint32_t seed) override {
        players = std::clamp(n, 2, MAX_STICKS);
        m = Match{}; m.mode = mode; m.world = world; m.mutators = muts; m.randomMutator = randomMut; m.Start(players, toWin, seed ? seed : 1, arsenal);
        names.assign(players, std::string());
        for (int p = 0; p < players; p++) names[p] = p == 0 ? "Host" : TextFormat("Player %d", p + 1);
        seats.assign(players, Seat{});
        for (int p = 0; p < players; p++) { seats[p].rng = 1234567u + p * 7919u + seed; seats[p].heard = 0; }
        acc = 0; time = 0; tick = 0; std::fill(cacheTick.begin(), cacheTick.end(), ~0u);
    }
    bool Act(int p, Reader& r) override {
        if (p < 0 || p >= players) return false;
        int kind = (int)r.U8();
        if (r.bad) return false;
        if (kind == SA_INPUTS) {
            std::vector<InputFrame> f; if (!ReadInputs(r, f)) return false;
            Seat& s = seats[p];
            for (auto& x : f) if (x.seq > s.last) { s.q.push_back(x); s.last = x.seq; }
            while (s.q.size() > 24) s.q.pop_front();   // (a backlog: the oldest are dropped, the guest's replay corrects)
            s.heard = time;
            return false;
        }
        if (kind == SA_HELLO) {
            std::string n = r.Str(); if (r.bad || n.empty()) return false; names[p] = n.substr(0, 20);
            int tk = (int)r.U8() - 1;   // (their trinket: for this match, and for this round if it has only begun)
            if (!r.bad && tk >= 0 && tk < TK_COUNT && p < (int)m.trinkets.size()) { m.trinkets[p] = tk; if (m.w.t < 3 && p < (int)m.w.sticks.size()) m.w.sticks[p].trinket = tk; }
            int sk = (int)r.U8() - 1, ht = (int)r.U8() - 1;   // (their skin and hat: worn from now on)
            if (!r.bad && sk >= -1 && sk < (int)Skins().size() && ht >= -1 && ht < (int)Hats().size() && p < (int)m.skins.size() && p < (int)m.hats.size()) { m.skins[p] = sk; m.hats[p] = ht; if (p < (int)m.w.sticks.size()) { m.w.sticks[p].skin = sk; if (m.w.t < 3) m.w.sticks[p].hat = ht; } }
            std::fill(cacheTick.begin(), cacheTick.end(), ~0u); return true;
        }
        return false;
    }
    bool Tick(float dt, uint32_t ai) override {
        acc += std::min(dt, 0.25f);
        int steps = 0;
        while (acc >= STEP - 1e-5f && steps < 30) {
            acc -= STEP; steps++; time += STEP;
            for (int p = 0; p < players; p++) {
                Seat& s = seats[p];
                bool bot = ((ai >> p) & 1) || time - s.heard > 1.5;   // (an AI seat, or a person gone quiet: the bot fights on for them)
                if (bot) { s.q.clear(); BotInput(m.w, p, s.cur, s.rng, skill); }
                else if (!s.q.empty()) {
                    // a small queue keeps the guest's inputs in order; one that grows (the clocks drifting) is caught up two at a time
                    if (s.q.size() > 12) { s.ack = s.q.front().seq; s.q.pop_front(); gDrop++; }
                    gUsed++; s.cur = s.q.front().in; s.ack = s.q.front().seq; s.q.pop_front();
                } else { s.cur.taunt = false; gUnder++; }
                m.w.sticks[p].in = s.cur;
            }
            m.Step();
            tick++;
        }
        return steps > 0;
    }
    void Snapshot(int viewer, Writer& out) const override {
        if (viewer < 0 || viewer >= players) viewer = 0;
        if (viewer == 0 && !test) { out.U32(MAGIC_HOST); return; }   // (the host's own screen draws the real match)
        if (cacheTick[viewer] != tick) {
            Writer t; auto* self = const_cast<ScuffleHost*>(this);
            PackMatch(self->m, self->names, viewer, seats[viewer].ack, t);
            cache[viewer] = std::move(t.b); cacheTick[viewer] = tick;
        }
        out.Bytes(cache[viewer].data(), cache[viewer].size());
    }
    bool Over() const override { return m.Over(); }
};
}  // namespace
std::unique_ptr<arcade::GameHost> MakeScuffleHost() { return std::make_unique<ScuffleHost>(); }
Match* ScuffleHostMatch(arcade::GameHost* h) { auto* s = dynamic_cast<ScuffleHost*>(h); return s ? &s->m : nullptr; }
const std::vector<std::string>* ScuffleHostNames(arcade::GameHost* h) { auto* s = dynamic_cast<ScuffleHost*>(h); return s ? &s->names : nullptr; }
std::string ScuffleHostOpts(int toWin, int arsenal, int skill, int world, uint32_t mutators, bool randomMutator, int mode) { return TextFormat("%d:%d:%d:%d:%u:%d:%d", toWin, arsenal, skill, world, mutators, randomMutator ? 1 : 0, mode); }
uint32_t ScuffleDataHash() {
    // the rules every peer must share: the arsenal (the stages travel in the snapshots)
    Writer w;
    for (const auto& d : Weapons()) { w.Str(d.key); w.Str(d.kind); w.I32(d.stage); w.I32(d.ammo); w.I32(d.pellets); w.F32(d.dmg); w.F32(d.rate); w.F32(d.knock); w.F32(d.speed); w.F32(d.spread); w.F32(d.area); w.F32(d.fuse); w.F32(d.reach); }
    const ArmsTuning& a = Arms(); w.F32(a.crateFirst); w.F32(a.crateEvery); w.F32(a.wallStart); w.F32(a.wallAll); w.F32(a.finaleWall);
    w.F32(STEP); w.F32(TILE);
    for (const auto& c : Skins()) w.Str(c.id); for (const auto& c : Hats()) w.Str(c.id);   // (cosmetics travel as indices)
    return Fnv1a(w.b.data(), w.b.size());
}

// ---------------------------------------------------------------- checks
int RunScuffleNetTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("Scuffle: the network layer\n");
    {   // an input frame round-trips quantised, and a guest predicts with exactly what the host will read
        Input in; in.moveX = 0.73f; in.moveY = -1; in.aim = Vector2Normalize({0.3f, 0.8f}); in.jump = true; in.taunt = true;
        std::vector<InputFrame> fr{{7, in}, {8, in}};
        Writer w; WriteInputs(fr, w); Reader r(w.b); r.U8(); std::vector<InputFrame> got;
        Input q = QuantizeInput(in);
        check(ReadInputs(r, got) && got.size() == 2 && got[1].seq == 8 && got[0].in.moveX == q.moveX && got[0].in.aim.x == q.aim.x && got[0].in.aim.y == q.aim.y && got[0].in.jump && got[0].in.taunt && !got[0].in.fire, "input frames round-trip (quantised the same on both ends)");
    }
    Match M; M.Start(4, 5, 777);
    uint32_t rr[4] = {1, 2, 3, 4};
    for (int f = 0; f < 120 * 14; f++) { for (int i = 0; i < 4; i++) BotInput(M.w, i, M.w.sticks[i].in, rr[i], 2); M.Step(); }
    std::vector<std::string> names{"Host", "Ann", "Bo", "Cy"};
    Match mir; std::vector<std::string> mn;
    { Writer a; WriteMatch(M, names, 2, 55, a); Reader r(a.b); int v = -1; uint32_t ack = 0; bool ok = ReadMatch(r, mir, mn, &v, &ack); check(ok && v == 2 && ack == 55 && mn.size() == 4 && mn[1] == "Ann" && mir.w.sticks.size() == M.w.sticks.size(), TextFormat("a guest builds the mirror from a snapshot (%d sticks, %d items)", (int)mir.w.sticks.size(), (int)mir.w.items.size())); }
    { Writer a; WriteMatch(M, names, 2, 55, a); Writer b; WriteMatch(mir, mn, 2, 55, b); check(a.b == b.b, TextFormat("the mirror writes back byte for byte (%d bytes)", (int)a.b.size())); }
    { Writer a; PackMatch(M, names, 2, 55, a); Reader r(a.b); Match g; std::vector<std::string> gn; check(ReadMatch(r, g, gn) && g.w.Hash() == M.w.Hash(), TextFormat("a packed snapshot reads to the same world (%.1f KB)", a.b.size() / 1024.0f)); }
    for (int md : {MD_KING, MD_EGG, MD_HUNT, MD_TEAMS, MD_GAUNTLET, MD_BOSS}) {   // (stage 7: each mode's state travels; a mirror steps on as the host does)
        Match X; X.mode = md; X.friendlyFire = md != MD_TEAMS; X.Start(md == MD_GAUNTLET ? 2 : 4, 3, 99);
        uint32_t r2[4] = {5, 6, 7, 8}; int n = X.players;
        for (int f = 0; f < 120 * 8; f++) { for (int i = 0; i < n; i++) BotInput(X.w, i, X.w.sticks[i].in, r2[i], 2); X.Step(); }
        std::vector<std::string> xn(n, "x"), yn; Match Y;
        Writer a; WriteMatch(X, xn, 0, 1, a); Reader r(a.b); bool ok = ReadMatch(r, Y, yn); Writer b; WriteMatch(Y, yn, 0, 1, b);
        for (int f = 0; f < 240; f++) { X.Step(); Y.Step(); }
        check(ok && a.b == b.b && Y.mode == md && X.w.Hash() == Y.w.Hash() && Y.w.pts == X.w.pts, TextFormat("%s: the snapshot carries the mode, and the mirror stays in step", ModeName(md)));
    }
    {   // the mirror steps on exactly as the host does (same inputs: same world)
        Match a = M, b = mir;
        for (int f = 0; f < 240; f++) { a.w.Step(); b.w.Step(); }   // (the world itself: the round's end and the next stage are the host's business)
        check(a.w.Hash() == b.w.Hash(), "the mirror's world steps exactly like the host's for two seconds");
    }
    {   // garbage and truncation are refused, and leave the mirror as it was
        Writer a; PackMatch(M, names, 1, 0, a);
        std::vector<uint8_t> bad = a.b; bad[bad.size() / 2] ^= 0x5A;
        Match keep = mir; Reader r(bad); std::vector<std::string> gn = mn;
        bool refused = !ReadMatch(r, mir, gn);
        Writer raw; WriteMatch(M, names, 1, 0, raw); raw.b.resize(raw.b.size() / 3); Reader r2(raw.b);
        refused = refused && !ReadMatch(r2, mir, gn);
        check(refused && mir.w.Hash() == keep.w.Hash(), "a damaged or cut-short snapshot is refused and changes nothing");
    }
    // the host: a guest's numbered inputs are played in order and acknowledged
    auto host = MakeScuffleHost();
    host->Configure("3:0:2:test");
    host->Start(2, 4242);
    Match& H = *ScuffleHostMatch(host.get());
    uint32_t idleSeq = 1;
    auto idle = [&]() { std::vector<InputFrame> fr{{idleSeq++, Input{}}}; Writer w; WriteInputs(fr, w); Reader r(w.b); host->Act(0, r); };   // (stick 1 stands still: only my own stick moves)
    for (int k = 0; k < 150; k++) { idle(); host->Tick(1 / 120.0f, 0); }   // (past the countdown)
    Predictor P;
    { Writer s; host->Snapshot(1, s); Reader r(s.b); check(P.Apply(r) && P.me == 1, "the guest reads its first snapshot (it is stick 2)"); }
    float x0 = H.w.sticks[1].pos.x;
    std::vector<InputFrame> box;
    float worst = 0, far = 0; int applied = 0;
    for (int f = 0; f < 120 * 3; f++) {
        Input in; in.moveX = f < 180 ? 1.0f : -1.0f; in.jump = (f / 40) % 3 == 0; in.aim = {1, 0};
        P.Local(in, box);
        if (f % 2 == 1) { Writer w; WriteInputs(box, w); box.clear(); Reader r(w.b); host->Act(1, r); }
        // the host runs a little behind (a 6-step lag on its queue) and snapshots at 30 Hz
        idle(); if (f >= 6) host->Tick(1 / 120.0f, 0);
        far = std::max(far, fabsf(H.w.sticks[1].pos.x - x0));
        if (f % 4 == 3) { Writer s; host->Snapshot(1, s); Reader r(s.b); if (P.Apply(r)) { applied++; worst = std::max(worst, P.lastCorrection); if (getenv("DEPTH_NETTRACE")) printf("    f %d ack %u hist %d corr %.4f\n", f, P.ack, (int)P.hist.size(), P.lastCorrection); } }
    }
    check(far > 2, TextFormat("the guest's inputs move their stick on the host (%.1f m at the furthest)", far));
    check(applied > 50 && worst < 0.02f, TextFormat("the guest's prediction agrees with the host: %d snapshots, the worst correction %.3f m", applied, worst));
    { Writer w; OrderHello(w, "Kipper"); Reader r(w.b); host->Act(1, r); Writer s; host->Snapshot(1, s); Reader r2(s.b); P.Apply(r2); check(P.names.size() == 2 && P.names[1] == "Kipper", "a name arrives with hello and reaches the guest"); }
    {   // a guest gone quiet: the bot fights on for them
        Vector2 p0 = H.w.sticks[1].pos;
        for (int k = 0; k < 120 * 4; k++) { idle(); host->Tick(1 / 120.0f, 0); }
        check(Vector2Distance(p0, H.w.sticks[1].pos) > 0.3f || !H.w.sticks[1].alive || H.phase != Match::P_FIGHT, "a guest gone quiet is played by the bot");
    }
    printf(fails ? "Scuffle net: %d FAILED\n" : "Scuffle net: all checks passed\n", fails);
    return fails ? 1 : 0;
}

int RunScuffleNetLoop(int lagMs, bool forceMemory) {
    // the doc's gate (p. 21): eight players at 100 ms with no visible correction. A host and seven guests over loopback
    // (GameNetworkingSockets with fake lag on every packet) or the in-memory transport; each guest predicts its own stick
    // with a bot's mind choosing its input (a stand-in for a person); the corrections it needed are measured
    using namespace arcade;
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::string err;
    bool real = !forceMemory && net::Init(&err);
    if (real && lagMs > 0) net::SetFakeLag(lagMs, 0.0f);
    auto make = [&]() { return real ? net::MakeTransport() : net::MakeMemoryTransport(); };
    uint16_t port = 47870;
    std::string addr = real ? "127.0.0.1:" + std::to_string(port) : "mem:" + std::to_string(port);
    printf("net-loop scuffle over %s%s: a host and seven guests, first to 2\n", real ? "GameNetworkingSockets (loopback UDP)" : "the in-memory transport", real && lagMs > 0 ? TextFormat(", %d ms each way", lagMs) : "");
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    const int NG = 7;
    Session host, gs[NG];
    Profile ph{"Host", 90};
    if (!host.Host(ph, G_SCUFFLE, &err, port, make(), false)) { printf("FAIL: host: %s\n", err.c_str()); return 1; }
    host.gameOpts = "2:0:2";
    const float dt = 1 / 60.0f;
    double t = 0;
    std::vector<Predictor> pred(NG);
    int seen[NG] = {}, readFails = 0, snaps = 0; size_t bytes = 0, biggest = 0;
    std::vector<float> freeCorr, nearCorr; int visible = 0;
    uint32_t brng[NG]; for (int k = 0; k < NG; k++) brng[k] = 99 + k * 31;
    uint32_t hostSeq = 1, hostRng = 5;
    double acc[NG] = {};
    auto pace = [&]() { if (real) std::this_thread::sleep_for(std::chrono::milliseconds(16)); };
    bool playing = false;
    auto step = [&](int frames) {
        for (int f = 0; f < frames; f++) {
            t += dt; host.Update(t, dt);
            if (playing && host.stage == S_PLAYING) {   // the host's own stick (a bot's mind, sent as numbered inputs like anyone's)
                Match* hm = ScuffleHostMatch(host.HostGame());
                if (hm) { std::vector<InputFrame> fr; for (int s = 0; s < 2; s++) { Input in; BotInput(hm->w, 0, in, hostRng, 2); fr.push_back({hostSeq++, QuantizeInput(in)}); } Writer w; WriteInputs(fr, w); host.Act(w); }
            }
            for (int k = 0; k < NG; k++) {
                gs[k].Update(t, dt);
                if (gs[k].stage != S_PLAYING) continue;
                Predictor& P = pred[k];
                if (gs[k].stateVersion != seen[k] && !gs[k].Snapshot().empty()) {
                    seen[k] = gs[k].stateVersion;
                    const auto& sn = gs[k].Snapshot();
                    bytes += sn.size(); biggest = std::max(biggest, sn.size()); snaps++;
                    Reader r(sn);
                    if (P.Apply(r)) {
                        if (P.lastFree) { freeCorr.push_back(P.lastCorrection); visible += P.lastCorrection > 0.1f; }
                        else if (P.lastCorrection > 0) nearCorr.push_back(P.lastCorrection); else nearCorr.push_back(0);
                    } else readFails++;
                }
                if (!P.have) continue;
                acc[k] += dt;
                std::vector<InputFrame> box;
                while (acc[k] >= STEP) { acc[k] -= STEP; Input in; if (P.me >= 0 && P.me < (int)P.m.w.sticks.size()) BotInput(P.m.w, P.me, in, brng[k], 2); P.Local(in, box); }
                if (!box.empty()) { Writer w; WriteInputs(box, w); gs[k].Act(w); }
            }
            pace();
        }
    };
    auto until = [&](std::function<bool()> ok, float seconds) { for (int i = 0; i < seconds / dt && !ok(); i++) step(1); };
    for (int k = 0; k < NG; k++) { Profile p{std::string("Stick ") + char('A' + k), (uint64_t)(100 + k)}; if (!gs[k].Join(p, addr, &err, 0, make())) { printf("FAIL: join: %s\n", err.c_str()); return 1; } }
    until([&] { for (auto& g : gs) if (g.stage != S_LOBBY) return false; return true; }, 20);
    for (auto& g : gs) g.SetReady(true);
    until([&] { for (int s = 1; s <= NG; s++) if (!host.seats[s].ready) return false; return true; }, 10);
    std::string why;
    check(host.Launch(&why), "eight sticks launch Scuffle" + (why.empty() ? std::string() : ": " + why));
    playing = true;
    until([&] { for (auto& p : pred) if (!p.have) return false; return true; }, 20);
    Match& truth = *ScuffleHostMatch(host.HostGame());
    bool all = true; for (auto& p : pred) all = all && p.have && p.m.w.sticks.size() == 8;
    check(all && truth.players == 8, "every guest mirrors the eight sticks");
    for (int k = 0; k < NG; k++) { Writer o; OrderHello(o, std::string("Stick ") + char('A' + k)); gs[k].Act(o); }
    step(30);
    int me0 = gs[0].MyPlayer();
    check(me0 >= 1 && (*ScuffleHostNames(host.HostGame()))[me0] == "Stick A" && pred[0].names.size() == 8 && pred[0].names[me0] == "Stick A", "names arrive");
    double t0 = t; int lastRound = truth.round;
    while (!truth.Over() && t - t0 < 300) {
        step(1);
        if (truth.round != lastRound) { lastRound = truth.round; printf("    [%3.0f s] round %d (wins:", t - t0, truth.round); for (int x : truth.wins) printf(" %d", x); printf(")\n"); }
    }
    printf("    host input queues: %d played, %d steps with none waiting, %d dropped\n", gUsed, gUnder, gDrop);
    printf("    [%3.0f s] stopped: phase %d, round clock %.1f s, %d living, paused %d, match over %d\n", t - t0, (int)truth.phase, truth.w.t, truth.w.Living(), (int)host.paused, (int)truth.Over());
    step(40);
    check(truth.round >= 2, TextFormat("rounds are fought (%d)", truth.round));
    bool agree = true; for (auto& p : pred) agree = agree && p.m.wins == truth.wins && p.m.round == truth.round && p.m.champion == truth.champion && p.m.Over() == truth.Over();
    check(agree && truth.Over(), TextFormat("the match ends and every guest agrees on the rounds won and the champion (%s)", truth.champion >= 0 ? (*ScuffleHostNames(host.HostGame()))[truth.champion].c_str() : "nobody"));
    std::sort(freeCorr.begin(), freeCorr.end());
    auto pct = [&](float q) { return freeCorr.empty() ? 0.0f : freeCorr[std::min(freeCorr.size() - 1, (size_t)(q * freeCorr.size()))]; };
    printf("    own-stick corrections with no one in reach: %d snapshots, median %.4f m, 95th %.4f m, 99th %.4f m, worst %.3f m (%d over 10 cm)\n",
           (int)freeCorr.size(), pct(0.5f), pct(0.95f), pct(0.99f), freeCorr.empty() ? 0.0f : freeCorr.back(), visible);
    std::sort(nearCorr.begin(), nearCorr.end());
    auto npc = [&](float q) { return nearCorr.empty() ? 0.0f : nearCorr[std::min(nearCorr.size() - 1, (size_t)(q * nearCorr.size()))]; };
    printf("    ... and in a fight (someone within 3 m, a shot or blast near, or hit): %d snapshots, median %.3f m, 90th %.3f m, 99th %.3f m (the scene glides these out over a tenth of a second)\n", (int)nearCorr.size(), npc(0.5f), npc(0.9f), npc(0.99f));
    check(!freeCorr.empty() && pct(0.99f) < 0.1f, "no visible correction: 99% of own-stick corrections under 10 cm (the scene eases the rest)");
    check(readFails == 0 && snaps > 0, TextFormat("%d snapshots, none refused; %.1f KB on average, %.1f KB the biggest", snaps, snaps ? bytes / 1024.0 / snaps : 0.0, biggest / 1024.0));
    for (auto& g : gs) g.Leave();
    step(10);
    host.Leave();
    step(10);
    if (real) { net::SetFakeLag(0, 0); net::Shutdown(); }
    printf(fails ? "%d FAILED\n" : "net-loop scuffle: all checks passed\n", fails);
    return fails ? 1 : 0;
}

}  // namespace sf
