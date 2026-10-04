// A Night Off's network layer (see nightoff_net.h): the input, the snapshot Visit, the host, and the checks. Stage 6.
#include "nightoff_net.h"
#include "arcade_session.h"
#include "net.h"
#include "redtide.h"
#include "raymath.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <thread>

namespace no {

// ---------------------------------------------------------------- input
void WriteInput(const Input& in, Writer& w) {
    w.U8(NA_INPUT);
    w.F32(in.moveX); w.F32(in.moveZ); w.F32(in.faceYaw);
    uint32_t bits = (in.run ? 1u : 0) | (in.use ? 2u : 0) | (in.leave ? 4u : 0) | (in.block ? 8u : 0) | (in.dodge ? 16u : 0) | (in.pickUp ? 32u : 0) | (in.smash ? 64u : 0) | (in.feedDog ? 128u : 0)
                  | (in.grabGun ? 256u : 0) | (in.askTrouble ? 512u : 0) | (in.fortuneYes ? 1024u : 0) | (in.buyRound ? 2048u : 0) | (in.carry ? 4096u : 0) | (in.drawFace ? 8192u : 0);
    w.VarU(bits);
    auto I = [&](int v) { w.VarU(((uint32_t)v << 1) ^ (uint32_t)(v >> 31)); };
    I(in.order); I(in.talkTo); I(in.say); I(in.startGame); I(in.gameMachine); I(in.gameOpp); I(in.gameStake); I(in.gameAct);
    w.F32(in.gameAim.x); w.F32(in.gameAim.y); w.F32(in.gamePower); w.F32(in.gameEnglish);
    I(in.attack); I(in.flirtWith); I(in.flirtSay); I(in.offer); I(in.spike); I(in.wager);
}
bool ReadInput(Reader& r, Input& in) {
    in.moveX = r.F32(); in.moveZ = r.F32(); in.faceYaw = r.F32();
    uint32_t b = r.VarU();
    in.run = b & 1; in.use = b & 2; in.leave = b & 4; in.block = b & 8; in.dodge = b & 16; in.pickUp = b & 32; in.smash = b & 64; in.feedDog = b & 128;
    in.grabGun = b & 256; in.askTrouble = b & 512; in.fortuneYes = b & 1024; in.buyRound = b & 2048; in.carry = b & 4096; in.drawFace = b & 8192;
    auto I = [&]() { uint32_t z = r.VarU(); return (int)((z >> 1) ^ (0u - (z & 1))); };
    in.order = I(); in.talkTo = I(); in.say = I(); in.startGame = I(); in.gameMachine = I(); in.gameOpp = I(); in.gameStake = I(); in.gameAct = I();
    in.gameAim.x = r.F32(); in.gameAim.y = r.F32(); in.gamePower = r.F32(); in.gameEnglish = r.F32();
    in.attack = I(); in.flirtWith = I(); in.flirtSay = I(); in.offer = I(); in.spike = I(); in.wager = I();
    if (r.bad || !std::isfinite(in.moveX) || !std::isfinite(in.moveZ) || !std::isfinite(in.faceYaw) || !std::isfinite(in.gameAim.x) || !std::isfinite(in.gameAim.y) || !std::isfinite(in.gamePower) || !std::isfinite(in.gameEnglish)) return false;
    in.moveX = std::clamp(in.moveX, -1.0f, 1.0f); in.moveZ = std::clamp(in.moveZ, -1.0f, 1.0f);
    in.gamePower = std::clamp(in.gamePower, 0.0f, 8.0f); in.gameEnglish = std::clamp(in.gameEnglish, -1.0f, 1.0f);
    in.gameStake = std::clamp(in.gameStake, 0, 1000); in.attack = std::clamp(in.attack, 0, (int)MV_THROW);
    return true;
}
void OrderHello(Writer& w, const std::string& name, int crew) { w.U8(NA_HELLO); w.Str(name.substr(0, 24)); w.U8((uint32_t)std::clamp(crew, 0, 5)); }
// a press survives until a step uses it (a click between steps isn't lost); the stick is always the newest
static void Merge(Input& q, const Input& in) {
    q.moveX = in.moveX; q.moveZ = in.moveZ; q.faceYaw = in.faceYaw; q.run = in.run; q.block = in.block;
    q.use |= in.use; q.leave |= in.leave; q.dodge |= in.dodge; q.pickUp |= in.pickUp; q.smash |= in.smash; q.feedDog |= in.feedDog; q.grabGun |= in.grabGun;
    q.askTrouble |= in.askTrouble; q.fortuneYes |= in.fortuneYes; q.buyRound |= in.buyRound; q.carry |= in.carry; q.drawFace |= in.drawFace;
    auto ev = [](int& a, int b, int none) { if (b != none) a = b; };
    ev(q.order, in.order, -1); ev(q.talkTo, in.talkTo, -1); ev(q.say, in.say, -1); ev(q.flirtWith, in.flirtWith, -1); ev(q.flirtSay, in.flirtSay, -1);
    ev(q.offer, in.offer, 0); ev(q.spike, in.spike, -1); ev(q.wager, in.wager, -1); ev(q.attack, in.attack, 0);
    if (in.startGame >= 0) { q.startGame = in.startGame; q.gameMachine = in.gameMachine; q.gameOpp = in.gameOpp; q.gameStake = in.gameStake; }
    if (in.gameAct) { q.gameAct = in.gameAct; q.gameAim = in.gameAim; q.gamePower = in.gamePower; q.gameEnglish = in.gameEnglish; }
}

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
    template <class T, class F> void vec(std::vector<T>& v, F fn) { uint32_t n = r.VarU(); if (n > 5000 || r.bad) { r.bad = true; n = 0; } v.resize(n); for (auto& x : v) { fn(x); if (r.bad) break; } }
    void q16(float& v, float lo, float hi) { v = lo + r.U16() / 65535.0f * (hi - lo); }
    bool bad() const { return r.bad; }
};
template <class A> void P2(A& a, Vector2& v) { a.q16(v.x, -12, 52); a.q16(v.y, -12, 52); }
template <class A> void P3(A& a, Vector3& v) { a.q16(v.x, -12, 52); a.q16(v.y, -1, 7); a.q16(v.z, -12, 52); }
template <class A> void F2(A& a, Vector2& v) { a.f(v.x); a.f(v.y); }
template <class A> void W(A& a, Who& w) { a.i(w.kind); a.i(w.idx); }
template <class A> void Col(A& a, Color& c) { int k = c.r | c.g << 8 | c.b << 16 | c.a << 24; a.i(k); if constexpr (A::reading) c = {(unsigned char)(k & 255), (unsigned char)((k >> 8) & 255), (unsigned char)((k >> 16) & 255), (unsigned char)((k >> 24) & 255)}; }
template <class A> void VisitCombat(A& a, Combat& c) {
    a.i(c.brawl); a.i(c.side); a.f(c.hp); a.f(c.hpMax); a.i(c.move); a.f(c.windT); a.f(c.recT); a.f(c.swingT); F2(a, c.swingDir);
    a.f(c.blockT); a.f(c.dodgeT); a.f(c.stunT); a.f(c.downT); a.f(c.bleedT); a.f(c.hitT); a.f(c.fallT); a.f(c.smashT);
    W(a, c.grabbing); W(a, c.grabbedBy); a.i(c.held); a.i(c.knockouts); a.f(c.afterT); a.f(c.afterK); F2(a, c.push); a.b(c.flying);
}
template <class A> void Balls(A& a, pool::Table& t) { for (auto& b : t.b) { F2(a, b.p); a.b(b.in); if constexpr (A::reading) b.v = {0, 0}; } }
template <class A> void VisitSeat(A& a, GameSeat& g) {
    a.i(g.kind); a.i(g.machine); a.i(g.opp); a.i(g.stake); a.f(g.oppSkill); a.s(g.oppName); a.s(g.tell); a.b(g.hustler); a.b(g.sandbag);
    a.b(g.over); a.b(g.tellShown); a.i(g.result); a.f(g.botT); a.f(g.replayT); a.i(g.replayLen); a.s(g.caption); a.f(g.captionT);
    if (g.kind < 0) return;
    { darts::Match& m = g.darts; a.b(m.clock); for (int k = 0; k < 2; k++) { a.i(m.left[k]); a.i(m.next[k]); a.i(m.big[k]); } a.i(m.turn); a.i(m.dart); a.i(m.turnStart); a.i(m.winner); a.i(m.turns); a.i(m.turnPts); a.b(m.bust); a.vec(m.marks, [&](Vector2& v) { F2(a, v); }); }
    { pool::Match& m = g.pool; Balls(a, m.t); a.i(m.turn); a.i(m.winner); a.i(m.group[0]); a.i(m.group[1]); a.b(m.ballInHand); a.b(m.broken); a.i(m.shots); a.s(m.last); }
    { golf::Match& m = g.golf; a.i(m.hole); for (auto& row : m.strokes) for (int& s : row) a.i(s); a.i(m.turn); a.i(m.winner);
      for (auto& b : m.ball) { F2(a, b.p); a.b(b.holed); } a.b(m.done[0]); a.b(m.done[1]); a.f(m.sim.t); F2(a, m.sim.dog); a.i(m.maxStrokes); a.b(m.rain); a.b(m.solo);
      a.b(m.lastHoled); a.i(m.lastWho); a.i(m.lastHole); a.i(m.lastStrokes); a.i(g.golfWho); a.i(g.golfHole); }
    a.i(g.shotSerial); Balls(a, g.shotTable); a.f(g.shot.ang); a.f(g.shot.power); a.f(g.shot.english); a.b(g.shotMiss);
    F2(a, g.shotBall.p); a.b(g.shotBall.holed); a.f(g.shotSim.t); F2(a, g.shotSim.dog); a.b(g.shotRain);
    for (int& k : g.pull.reel) a.i(k); a.i(g.pull.pays); a.b(g.pull.kidney); a.i(g.pulls); a.f(g.spinT); a.f(g.autoT);
    a.i(g.ticket.prize); a.b(g.ticket.map); for (int& k : g.ticket.sym) a.i(k); a.b(g.haveTicket); a.b(g.paid);
    for (int k = 0; k < 3; k++) { a.i(g.reading.card[k]); a.s(g.reading.text[k]); } a.b(g.haveReading);
}
template <class A> void VisitPlayer(A& a, Player& p, bool own, bool over) {
    a.s(p.name); a.i(p.crew); a.i(p.crew2); a.b(p.bot);
    int st = (int)p.st; a.i(st); if constexpr (A::reading) p.st = (State)std::clamp(st, 0, (int)State::Down);
    a.i(p.ending); a.s(p.wokeAt); P2(a, p.pos); F2(a, p.vel); a.f(p.yaw);
    a.f(p.drunk); a.f(p.money); a.f(p.tab); a.i(p.kidneys); a.f(p.actT); a.i(p.acting);
    a.f(p.charBuffT); a.f(p.charBuff); a.f(p.toughBuffT); a.f(p.toughBuff); a.f(p.honestT); a.f(p.visionsT); a.f(p.shakesT);
    a.f(p.swayPh); a.f(p.stumbleT); a.f(p.stumbleDir); a.f(p.vomitT); a.f(p.lurch); a.i(p.drinks); a.f(p.peakDrunk); a.b(p.barred);
    VisitCombat(a, p.fight);
    a.f(p.leavingT); a.i(p.leavingWith); a.i(p.carrying); a.i(p.carriedBy); a.b(p.faceDrawn);
    a.s(p.homeWith); a.s(p.homeKind); a.b(p.homeBad); a.s(p.card); a.i(p.gamesWon); a.i(p.fightsWon); a.i(p.fightsWonSober);
    a.i(p.dare); a.i(p.daresDone); a.b(p.adopted); a.b(p.jacket); a.b(p.bribed); a.b(p.checked); a.b(p.promised); a.b(p.helpingRobbers); a.b(p.gaveRobbers);
    a.f(p.debt); a.f(p.roundT); a.f(p.damageCaused); a.f(p.lastFightT); a.i(p.cartelDue); a.b(p.watchingSafe); a.i(p.eventsSurvived);
    if (own || over) a.i(p.wager);
    if (!own) return;
    { Talk& t = p.talk; a.i(t.patron); a.i(t.exchanges); a.i(t.wins); a.i(t.losses); a.i(t.target); a.s(t.theirLine); a.s(t.myCaption); a.s(t.result); a.b(t.over); a.f(t.overT); a.f(t.listenT); a.i(t.lastOption); a.b(t.lastWin); a.b(t.substituted); }
    { Flirt& f = p.flirt; a.i(f.patron); a.i(f.round); a.i(f.wins); a.i(f.losses); a.i(f.need); a.i(f.lastOption); a.s(f.theirLine); a.s(f.myCaption); a.s(f.tell); a.s(f.result); a.b(f.over); a.b(f.offer); a.b(f.substituted); a.b(f.lastWin); a.f(f.overT); }
    VisitSeat(a, p.game);
    a.vec(p.items, [&](std::string& s) { a.s(s); }); a.vec(p.known, [&](std::string& s) { a.s(s); }); a.b(p.fortuneAsked);
}
template <class A> void VisitPatron(A& a, Patron& c, const Player* viewer) {
    a.i(c.reg); a.s(c.name); a.i(c.type); a.u(c.traits); a.b(c.thief); a.b(c.rich);
    a.f(c.mood); a.i(c.bothers); a.b(c.inside); a.b(c.gone); a.b(c.leaving); P2(a, c.pos); F2(a, c.vel); a.f(c.yaw); a.f(c.walkPh);
    a.b(c.sitting); a.s(c.seatKind); a.f(c.drunk); a.f(c.drinkT); a.i(c.talkingTo); a.i(c.playing);
    a.i(c.look.model); Col(a, c.look.top); Col(a, c.look.hat); a.f(c.look.build); a.f(c.look.height);
    bool known = viewer && std::find(viewer->known.begin(), viewer->known.end(), c.name) != viewer->known.end();
    if (known) a.s(c.secret);
    a.i(c.ev); a.s(c.role); a.f(c.backAt);
    VisitCombat(a, c.fight); a.b(c.outForNight); int fo = c.friendOf; a.i(fo); if constexpr (A::reading) c.friendOf = (uint8_t)fo;
}
template <class A> void Visit(A& a, Night& n, int viewer) {
    a.f(n.t); a.b(n.over); a.i(n.winner); a.f(n.policeT); a.f(n.policeInT); a.f(n.damage); a.b(n.bartenderDarts); a.b(n.midnightBrawl); a.f(n.crewTab);
    { Bartender& b = n.bar; a.f(b.mood); P2(a, b.pos); a.f(b.busyT); a.i(b.servingFor); a.f(b.polishPh); }
    std::vector<std::string> tail;
    if constexpr (!A::reading) { size_t k = std::min<size_t>(n.say.size(), 8); tail.assign(n.say.end() - k, n.say.end()); }
    a.vec(A::reading ? n.say : tail, [&](std::string& s) { a.s(s); });
    int np = (int)n.players.size(); a.i(np);
    if (np != (int)n.players.size()) { if constexpr (A::reading) a.r.bad = true; return; }
    for (auto& p : n.players) { VisitPlayer(a, p, p.id == viewer, n.over); if (a.bad()) return; }
    const Player* me = viewer >= 0 && viewer < (int)n.players.size() ? &n.players[viewer] : nullptr;
    a.vec(n.patrons, [&](Patron& c) { VisitPatron(a, c, me); });
    if constexpr (A::reading) for (int k = 0; k < (int)n.patrons.size(); k++) { n.patrons[k].id = k; n.patrons[k].mem.resize(n.players.size()); }
    a.vec(n.props, [&](Prop& p) { a.s(p.kind); P3(a, p.pos); a.f(p.yaw); a.f(p.tilt); int st = p.state; a.i(st); if constexpr (A::reading) p.state = (uint8_t)std::clamp(st, 0, (int)PS_GONE); W(a, p.holder); a.i(p.weapon); a.i(p.brawl); F2(a, p.size); });
    a.vec(n.pops, [&](Pop& p) { a.f(p.pos.x); a.f(p.pos.y); a.f(p.pos.z); a.s(p.text); a.f(p.t); Col(a, p.col); });
    a.vec(n.events, [&](Night::EventRun& e) { a.i(e.def); a.f(e.startH); a.f(e.endH); a.b(e.started); a.b(e.done); a.i(e.stage); a.f(e.a); a.f(e.b); a.i(e.target); a.s(e.outcome); a.vec(e.people, [&](int& k) { a.i(k); }); });
    a.b(n.raining); a.b(n.lockIn); a.b(n.freeDrinks); a.b(n.scratchEaten); a.b(n.safeOpened); a.b(n.goatOn); a.b(n.partied); a.f(n.rainH); a.f(n.endMinutes); P2(a, n.goatPos); a.f(n.goatYaw); a.f(n.goatPh);
    { Dog& d = n.dog; P2(a, d.pos); a.f(d.yaw); a.f(d.walkPh); a.i(d.owner); a.b(d.sleeping); for (int& f : d.fed) a.i(f); }
    // the morning: the host's headline, stories and scoreboards (a guest draws them as they are)
    if (n.over) {
        if constexpr (!A::reading) if (!n.mirror) {
            n.headlineCache = n.Headline(); n.storyCache.clear(); n.scoreCache.clear();
            for (const auto& p : n.players) { n.storyCache.push_back(n.MorningStory(p)); n.scoreCache.push_back(n.ScoreBreakdown(p)); }
        }
        a.s(n.headlineCache);
        a.vec(n.storyCache, [&](std::vector<std::string>& v) { a.vec(v, [&](std::string& s) { a.s(s); }); });
        a.vec(n.scoreCache, [&](std::vector<Night::ScoreLine>& v) { a.vec(v, [&](Night::ScoreLine& l) { a.s(l.what); a.i(l.points); }); });
    }
}
}  // namespace

void ReplayShot(GameSeat& g) {
    if (g.kind == GK_POOL) {
        pool::Table t = g.shotTable;
        if (!g.shotMiss) t.Simulate(g.shot, true);
        g.pool.t.frames = t.frames; g.replayLen = (int)t.frames.size();
    } else if (g.kind == GK_GOLF) {
        const auto& C = golf::Course(); const golf::Hole& h = C[std::clamp(g.golfHole, 0, 8)];
        golf::Ball b = g.shotBall; golf::Sim s = g.shotSim;
        b.v = {cosf(g.shot.ang) * g.shot.power, sinf(g.shot.ang) * g.shot.power};
        if (g.shotRain && (g.golfHole == 3 || g.golfHole == 6)) b.v.y += 0.18f;
        g.golfPath.clear(); g.golfPath.push_back(b.p);
        golf::Roll(h, b, s, 12, &g.golfPath);
        g.replayLen = (int)g.golfPath.size();
    }
}
void WriteNight(Night& n, int viewer, Writer& out) {
    Out o{out};
    uint32_t magic = 0x31464F4E; o.u(magic);   // "NOF1"
    uint32_t seed = n.opts.seed; o.u(seed);
    int players = (int)n.players.size(), crowd = n.opts.crowd, mode = n.opts.mode; bool pvp = n.opts.pvp; float start = n.opts.startMinutes;
    o.i(players); o.i(crowd); o.i(mode); o.b(pvp); o.f(start); o.i(viewer);
    Visit(o, n, viewer);
}
void PackNight(Night& n, int viewer, Writer& out) {
    Writer raw; WriteNight(n, viewer, raw);
    int cz = 0;
    unsigned char* z = CompressData(raw.b.data(), (int)raw.b.size(), &cz);
    if (!z || cz <= 0) { out.Bytes(raw.b.data(), raw.b.size()); return; }
    out.U32(0x5A464F4E); out.U32((uint32_t)raw.b.size()); out.Bytes(z, (size_t)cz);   // "NOFZ"
    MemFree(z);
}
bool ReadNight(Reader& r, Night& n, int* viewerOut) {
    In in{r};
    uint32_t magic = 0; in.u(magic);
    if (magic == 0x5A464F4E) {
        uint32_t rawN = r.U32();
        if (r.bad || r.i >= r.n || rawN == 0 || rawN > (1u << 23)) return false;
        int outN = 0;
        unsigned char* raw = DecompressData(r.p + r.i, (int)(r.n - r.i), &outN);
        r.i = r.n;
        if (!raw) return false;
        bool ok = (uint32_t)outN == rawN;
        if (ok) { Reader inner(raw, (size_t)outN); ok = ReadNight(inner, n, viewerOut) && !inner.bad; }
        MemFree(raw);
        return ok;
    }
    if (magic != 0x31464F4E) return false;
    uint32_t seed = 0; int players = 0, crowd = 0, mode = 0, viewer = 0; bool pvp = true; float start = 0;
    in.u(seed); in.i(players); in.i(crowd); in.i(mode); in.b(pvp); in.f(start); in.i(viewer);
    if (r.bad || players < 1 || players > 6 || crowd < 0 || crowd > 3 || mode < 0 || mode >= MD_COUNT || viewer < 0 || viewer >= players || start < 0 || start > 480) return false;
    if (!n.mirror || n.opts.seed != seed || (int)n.players.size() != players || n.opts.mode != mode) {
        Opts o; o.players = players; o.seed = seed; o.crowd = crowd; o.mode = mode; o.pvp = pvp; o.startMinutes = start;
        n.Init(o); n.mirror = true;
    }
    int serial = viewer < (int)n.players.size() ? n.players[viewer].game.shotSerial : 0, kind0 = viewer < (int)n.players.size() ? n.players[viewer].game.kind : -1;
    Visit(in, n, viewer);
    if (r.bad) return false;
    if (viewerOut) *viewerOut = viewer;
    // a new pool or golf shot: replay it here (the frames and the path aren't sent)
    GameSeat& g = n.players[viewer].game;
    if (g.kind >= 0 && (g.shotSerial != serial || g.kind != kind0) && g.shotSerial > 0 && (g.kind == GK_POOL || g.kind == GK_GOLF)) ReplayShot(g);
    return true;
}

// ---------------------------------------------------------------- the host
namespace {
class NightHost : public arcade::GameHost {
public:
    std::unique_ptr<Night> n = std::make_unique<Night>();
    int players = 1, mode = 0, crowd = 1; bool pvp = true, test = false; float start = 0, stepDt = 1 / 30.0f;
    std::vector<Input> pend;
    float acc = 0; uint32_t tick = 0;
    mutable std::vector<std::vector<uint8_t>> cache = std::vector<std::vector<uint8_t>>(arcade::MAX_PLAYERS);
    mutable std::vector<uint32_t> cacheTick = std::vector<uint32_t>(arcade::MAX_PLAYERS, ~0u);
    void Configure(const std::string& opts) override {
        int md = 0, cr = 1, pv = 1; float st = 0;
        if (sscanf(opts.c_str(), "%d:%d:%d:%f", &md, &cr, &pv, &st) >= 1) { mode = std::clamp(md, 0, MD_COUNT - 1); crowd = std::clamp(cr, 0, 3); pvp = pv != 0; start = std::clamp(st, 0.0f, 470.0f); }
        test = opts.find(":test") != std::string::npos;
        size_t sp = opts.find(":step="); stepDt = sp != std::string::npos ? std::clamp((float)atof(opts.c_str() + sp + 6), 1 / 60.0f, 0.1f) : 1 / 30.0f;
    }
    void Start(int np, uint32_t seed) override {
        players = std::clamp(np, 1, 6);
        Opts o; o.players = players; o.seed = seed ? seed : 1; o.crowd = crowd; o.mode = mode; o.pvp = pvp; o.startMinutes = start;
        n = std::make_unique<Night>(); n->Init(o);
        for (int p = 0; p < players; p++) n->players[p].name = p == 0 ? "Host" : TextFormat("Player %d", p + 1);
        pend.assign(players, Input{});
        acc = 0; tick = 0; std::fill(cacheTick.begin(), cacheTick.end(), ~0u);
    }
    bool Act(int p, Reader& r) override {
        if (p < 0 || p >= players) return false;
        int kind = (int)r.U8();
        if (r.bad) return false;
        if (kind == NA_INPUT) { Input in; if (!ReadInput(r, in)) return false; Merge(pend[p], in); return false; }
        if (kind == NA_HELLO) { std::string nm = r.Str(); int crew = (int)r.U8(); if (r.bad || nm.empty()) return false; n->players[p].name = nm; n->players[p].crew = std::clamp(crew, 0, 5); return true; }
        return false;
    }
    bool Tick(float dt, uint32_t ai) override {
        Night& N = *n;
        // a lost (or AI) seat is played by the bot until its person comes back
        for (int p = 0; p < players; p++) { bool aiNow = (ai >> p) & 1; Player& q = N.players[p]; if (aiNow && !q.bot) { q.bot = true; q.botGoal = -1; q.botDrinkTo = 45; q.botLeaveH = 25.5f; } else if (!aiNow && q.bot) q.bot = false; }
        acc += std::min(dt, 0.25f);
        int steps = 0;
        while (acc >= stepDt && steps < 8) {
            acc -= stepDt; steps++;
            for (int p = 0; p < players; p++) { Player& q = N.players[p]; if (q.bot) N.BotPlayer(q, stepDt); else { q.in = pend[p]; } }
            N.Step(stepDt);
            for (int p = 0; p < players; p++) { Input keep; keep.moveX = pend[p].moveX; keep.moveZ = pend[p].moveZ; keep.faceYaw = pend[p].faceYaw; keep.run = pend[p].run; keep.block = pend[p].block; pend[p] = keep; }
            tick++;
        }
        return steps > 0;
    }
    void Snapshot(int viewer, Writer& out) const override {
        if (viewer < 0 || viewer >= players) viewer = 0;
        if (viewer == 0 && !test) { out.U32(0x30484F4E); return; }   // "NOH0": the host's own screen draws the real night
        if (cacheTick[viewer] != tick) { Writer t; PackNight(*n, viewer, t); cache[viewer] = std::move(t.b); cacheTick[viewer] = tick; }
        out.Bytes(cache[viewer].data(), cache[viewer].size());
    }
    bool Over() const override { return n->over; }
};
}  // namespace
std::unique_ptr<arcade::GameHost> MakeNightHost() { return std::make_unique<NightHost>(); }
Night* NightHostWorld(arcade::GameHost* h) { auto* m = dynamic_cast<NightHost*>(h); return m ? m->n.get() : nullptr; }
std::string NightHostOpts(int mode, int crowd, bool pvp, float startMinutes) { return TextFormat("%d:%d:%d:%.0f", mode, crowd, pvp ? 1 : 0, startMinutes); }
uint32_t NightDataHash() {
    // every rule a peer must share: the meter, the menu, the games' curves, the fights, the regulars
    const Data& d = D(); Writer w;
    for (const auto& b : d.bands) { w.F32(b.from); w.F32(b.charisma); w.F32(b.toughness); }
    for (const auto& x : d.drinks) { w.Str(x.key); w.F32(x.drunk); w.F32(x.price); w.F32(x.minutes); }
    for (const auto& r : d.regulars) { w.Str(r.name); w.Str(r.home); w.I32(r.type); w.F32(r.arrive); w.F32(r.leave); }
    for (const auto& c : GD().aimCurve) { w.F32(c[0]); w.F32(c[1]); }
    w.F32(GD().dartSigma); w.F32(GD().poolSigma); w.F32(GD().golfSigma);
    for (const auto& x : FD().weapons) { w.Str(x.key); w.F32(x.damage); w.F32(x.thrown); }
    w.F32(FD().hp); w.F32(FD().jab); w.F32(FD().hay);
    return Fnv1a(w.b.data(), w.b.size());
}

// ---------------------------------------------------------------- checks
int RunNightNetTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("A Night Off: the network layer\n");
    Input in; in.moveX = 0.5f; in.order = 3; in.attack = MV_HAYMAKER; in.gameAim = {12, -40}; in.buyRound = true; in.flirtSay = 2;
    { Writer w; WriteInput(in, w); Reader r(w.b); r.U8(); Input o; check(ReadInput(r, o) && o.moveX == 0.5f && o.order == 3 && o.attack == MV_HAYMAKER && o.gameAim.y == -40 && o.buyRound && o.flirtSay == 2 && o.talkTo == -1, "an input frame round-trips"); }
    Night N; Opts o; o.players = 3; o.seed = 77; o.startMinutes = 150; o.events = false; N.Init(o);
    for (auto& p : N.players) { p.bot = true; p.botStyle = p.id % 2; }
    for (int i = 0; i < 600; i++) { for (auto& p : N.players) N.BotPlayer(p, 0.1f); N.Step(0.1f); }
    auto mir = std::make_unique<Night>();
    { Writer a; WriteNight(N, 1, a); Reader r(a.b); int v = -1; check(ReadNight(r, *mir, &v) && v == 1 && mir->mirror && mir->players.size() == 3 && mir->patrons.size() == N.patrons.size(), TextFormat("a guest builds the mirror from the first snapshot (%d patrons, %d props)", (int)mir->patrons.size(), (int)mir->props.size())); }
    { Writer a; WriteNight(N, 1, a); Writer b; WriteNight(*mir, 1, b); check(a.b == b.b, TextFormat("the mirror writes back byte for byte (%d bytes)", (int)a.b.size())); }
    { Writer a; PackNight(N, 1, a); Reader r(a.b); check(ReadNight(r, *mir) && mir->players[1].name == N.players[1].name, TextFormat("a packed snapshot reads (%.1f KB)", a.b.size() / 1024.0f)); }
    // a pool shot on the host replays itself on the guest
    { Player& p = N.players[1]; p.bot = false; N.EndTalk(p); N.EndFlirt(p); N.EndGame(p); p.fight = Combat{}; p.leavingT = 0; p.st = State::Active; p.pos = {3.3f, 5.0f}; p.in = Input{}; p.in.startGame = GK_POOL; p.in.gameOpp = -1; N.Step(0.02f);
      pool::Shot s; GRng rr; rr.s = 3; s = pool::BotShot(p.game.pool, 1, rr, true); p.in.gameAct = 1; p.in.gameAim = {s.ang, 0}; p.in.gamePower = s.power; N.Step(0.02f);
      Writer a; PackNight(N, 1, a); Reader r(a.b); bool ok = ReadNight(r, *mir);
      const auto& hf = N.players[1].game.pool.t.frames; const auto& gf = mir->players[1].game.pool.t.frames;
      check(ok && !hf.empty() && hf.size() == gf.size() && Vector2Distance(hf.back()[0], gf.back()[0]) < 1e-4f, TextFormat("a pool shot replays itself on the guest (%d frames)", (int)gf.size())); }
    // the host: a guest's input walks their sailor; a hello names them; a lost seat is played by the bot
    auto host = MakeNightHost();
    host->Configure("0:1:1:120:test:step=0.05");
    host->Start(2, 1234);
    Night& H = *NightHostWorld(host.get());
    Vector2 p0 = H.players[1].pos;
    for (int k = 0; k < 60; k++) { Input g; g.moveZ = 1; Writer w; WriteInput(g, w); Reader r(w.b); r.U8(); Reader r2(w.b); host->Act(1, r2); host->Tick(0.05f, 0); }
    check(Vector2Distance(H.players[1].pos, p0) > 3, TextFormat("a guest's input walks their sailor (%.1f m)", Vector2Distance(H.players[1].pos, p0)));
    { Writer w; OrderHello(w, "Barnacle", 3); Reader r(w.b); host->Act(1, r); check(H.players[1].name == "Barnacle" && H.players[1].crew == 3, "a name and a crew arrive with hello"); }
    { Writer s; host->Snapshot(1, s); Reader r(s.b); auto g = std::make_unique<Night>(); check(ReadNight(r, *g) && g->players[1].name == "Barnacle", "the guest's snapshot shows it"); }
    host->Tick(0.05f, 2);
    check(H.players[1].bot, "a lost guest's sailor is played by the bot");
    host->Tick(0.05f, 0);
    check(!H.players[1].bot, "and handed back when they return");
    printf(fails ? "A Night Off net: %d FAILED\n" : "A Night Off net: all checks passed\n", fails);
    return fails ? 1 : 0;
}

int RunNightNetLoop(bool forceMemory) {
    // the doc's gate (p. 29): six players on LAN finish a night. A host and five guests over loopback (or the in-memory
    // transport, fast); the guests are bot-driven from their own mirrors; one goes silent for a while and the bot takes
    // their seat; the night runs to its morning, and everyone agrees on the headline
    using namespace arcade;
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::string err;
    if (!rt::DataOk(&err)) { printf("FAIL: no data: %s\n", err.c_str()); return 1; }
    bool real = !forceMemory && net::Init(&err);
    auto make = [&]() { return real ? net::MakeTransport() : net::MakeMemoryTransport(); };
    uint16_t port = 47840;
    std::string addr = real ? "127.0.0.1:" + std::to_string(port) : "mem:" + std::to_string(port);
    float startMin = real ? 420 : 300;   // (the loopback run is paced in real time: the last hour; the in-memory one, the last three hours)
    printf("net-loop night over %s: a host and five guests, from %s\n", real ? "GameNetworkingSockets (loopback UDP)" : "the in-memory transport", real ? "2 a.m." : "midnight");
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    const int NG = 5;
    Session host, gs[NG];
    Profile ph{"Host", 71};
    if (!host.Host(ph, G_NIGHT_OFF, &err, port, make(), false)) { printf("FAIL: host: %s\n", err.c_str()); return 1; }
    host.gameOpts = NightHostOpts(MD_NIGHT_OFF, 1, true, startMin) + (real ? ":test" : ":test:step=0.1");
    const float dt = real ? 1 / 30.0f : 0.1f;
    double t = 0;
    std::vector<std::unique_ptr<Night>> mirror; for (int k = 0; k < NG; k++) mirror.push_back(std::make_unique<Night>());
    int seen[NG] = {}, mirrorOk[NG] = {}, readFails = 0, snaps = 0; size_t bytes = 0, biggest = 0;
    bool silent[NG] = {};
    auto pace = [&]() { if (real) std::this_thread::sleep_for(std::chrono::milliseconds(33)); };
    auto step = [&](int frames) {
        for (int f = 0; f < frames; f++) {
            t += dt; host.Update(t, dt);
            for (int k = 0; k < NG; k++) {
                if (silent[k]) continue;
                gs[k].Update(t, dt);
                if (gs[k].stage == S_PLAYING && gs[k].stateVersion != seen[k] && !gs[k].Snapshot().empty()) {
                    seen[k] = gs[k].stateVersion;
                    Reader r(gs[k].Snapshot());
                    if (ReadNight(r, *mirror[k])) mirrorOk[k]++; else readFails++;
                    bytes += gs[k].Snapshot().size(); biggest = std::max(biggest, gs[k].Snapshot().size()); snaps++;
                }
            }
            pace();
        }
    };
    auto until = [&](std::function<bool()> ok, float seconds) { for (int i = 0; i < seconds / dt && !ok(); i++) step(1); };
    for (int k = 0; k < NG; k++) { Profile p{std::string("Sailor ") + char('A' + k), (uint64_t)(90 + k)}; if (!gs[k].Join(p, addr, &err, 0, make())) { printf("FAIL: join: %s\n", err.c_str()); return 1; } }
    until([&] { for (auto& g : gs) if (g.stage != S_LOBBY) return false; return true; }, 15);
    for (auto& g : gs) g.SetReady(true);
    until([&] { for (int s = 1; s <= NG; s++) if (!host.seats[s].ready) return false; return true; }, 10);
    std::string why;
    check(host.Launch(&why), "six sailors go ashore" + (why.empty() ? std::string() : ": " + why));
    until([&] { for (int k = 0; k < NG; k++) if (mirrorOk[k] == 0) return false; return true; }, 20);
    Night& truth = *NightHostWorld(host.HostGame());
    bool all = true; for (int k = 0; k < NG; k++) all = all && mirror[k]->players.size() == 6;
    check(all && truth.players.size() == 6, "every guest mirrors the six sailors and the room");
    for (int k = 0; k < NG; k++) { Writer o; OrderHello(o, std::string("Sailor ") + char('A' + k), k % 6); gs[k].Act(o); }
    step(5);
    int me = gs[0].MyPlayer();
    check(me >= 1 && truth.players[me].name == "Sailor A", "names arrive");
    // everyone plays by the bot's mind on their own mirror (a stand-in for a person); the host's own sailor too
    for (auto& m : mirror) for (auto& p : m->players) { p.botStyle = p.id % 2; p.botDrinkTo = p.botStyle ? 85 : 40; p.botLeaveH = 26.6f; }
    truth.players[0].botStyle = 1; truth.players[0].botDrinkTo = 70; truth.players[0].botLeaveH = 26.6f;
    int lastHour = -1; bool sawBot = false, handedBack = false; double silentFrom = -1;
    while (!truth.over && t < 3600 * 3) {
        for (int k = 0; k < NG; k++) {
            if (silent[k]) continue;
            Night& m = *mirror[k];
            int p = gs[k].MyPlayer();
            if (p < 0 || p >= (int)m.players.size() || m.over) continue;
            m.BotPlayer(m.players[p], dt); m.players[p].in.faceYaw = m.players[p].yaw;
            Writer w; WriteInput(m.players[p].in, w); gs[k].Act(w);
            m.players[p].in = Input{};
        }
        { Player& h = truth.players[0]; truth.BotPlayer(h, dt); h.in.faceYaw = h.yaw; Writer w; WriteInput(h.in, w); host.Act(w); h.in = Input{}; }
        // one guest drops out (their machine goes quiet): after the session's two minutes the bot plays their seat
        int h = (int)truth.Hour();
        if (silentFrom < 0 && truth.Minutes() > startMin + 5) { silent[4] = true; silentFrom = t; }
        step(1);
        int p4 = gs[4].MyPlayer();
        if (p4 >= 0 && p4 < (int)truth.players.size() && silent[4] && truth.players[p4].bot) sawBot = true;
        handedBack = true;
        if (h != lastHour) { lastHour = h; int in = 0; for (const auto& q : truth.players) in += q.st != State::Gone && q.st != State::PassedOut; printf("    [%s] %d of 6 sailors still in, %d snapshots read\n", truth.Clock().c_str(), in, snaps); }
    }
    check(truth.over, TextFormat("the night ends (%s)", truth.Clock().c_str()));
    step(30);
    std::string head = truth.Headline();
    bool agree = true; for (int k = 0; k < NG; k++) if (!silent[k]) agree = agree && mirror[k]->over && mirror[k]->headlineCache == head && mirror[k]->storyCache.size() == 6;
    check(agree, "every guest sees the same morning: " + head);
    bool ended = true; for (const auto& q : truth.players) ended = ended && q.ending != E_NONE;
    check(ended, "every sailor's night has an ending");
    check(sawBot, "a dropped guest's seat is played by the bot to the morning");
    int drank = 0; for (const auto& q : truth.players) drank += q.drinks > 0;
    check(drank >= (real ? 3 : 4), TextFormat("the sailors drank (%d of 6)", drank));
    check(readFails == 0 && snaps > 0, TextFormat("%d snapshots, none refused; %.1f KB on average, %.1f KB the biggest", snaps, snaps ? bytes / 1024.0 / snaps : 0.0, biggest / 1024.0));
    for (auto& g : gs) g.Leave();
    host.Leave();
    step(5);
    if (real) net::Shutdown();
    printf(fails ? "%d FAILED\n" : "net-loop night: all checks passed\n", fails);
    return fails ? 1 : 0;
}

} // namespace no
