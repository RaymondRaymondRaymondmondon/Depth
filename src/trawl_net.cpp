// The Trawl over the network (stage 5; see trawl_net.h): hand inputs and dock commands applied to a TrawlWorld, the
// world's snapshot (one Visit walks every field the screens draw, for writing and for reading, so the two can never
// disagree), the host's GameHost, and the tests: --trawl-net-test and --net-loop trawl.
#include "trawl_net.h"
#include "arcade_session.h"
#include "net.h"
#include "raymath.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <set>
#include <thread>

namespace tw {

// ---------------------------------------------------------------- input
void WriteInput(const HandInput& in, Writer& w) {
    auto q = [](float v) { return (uint32_t)(int8_t)std::clamp((int)lroundf(v * 127), -127, 127) & 0xFF; };
    w.U8(q(in.wish.x)); w.U8(q(in.wish.y)); w.U8(q(in.steer));
    w.F32(in.aim.x); w.F32(in.aim.y); w.F32(in.wheel);
    w.U16(in.btn); w.U8((uint8_t)in.sel); w.U8((uint8_t)in.order);
}
bool ReadInput(Reader& r, HandInput& in) {
    auto dq = [](uint32_t b) { return (int8_t)(uint8_t)b / 127.0f; };
    in.wish.x = dq(r.U8()); in.wish.y = dq(r.U8()); in.steer = dq(r.U8());
    in.aim.x = r.F32(); in.aim.y = r.F32(); in.wheel = r.F32();
    in.btn = (uint16_t)r.U16(); in.sel = (int8_t)(uint8_t)r.U8(); in.order = (int8_t)(uint8_t)r.U8();
    if (r.bad || !std::isfinite(in.aim.x) || !std::isfinite(in.aim.y) || !std::isfinite(in.wheel)) return false;
    if (Vector2Length(in.wish) > 1.01f) in.wish = Vector2Normalize(in.wish);
    in.aim = {std::clamp(in.aim.x, -200.0f, 200.0f), std::clamp(in.aim.y, -200.0f, 200.0f)};
    in.wheel = std::clamp(in.wheel, -20.0f, 20.0f);
    if (in.sel > 3) in.sel = -1;
    return true;
}
void MergeInput(HandInput& into, const HandInput& n) {
    uint16_t presses = (into.btn | n.btn) & HI_PRESSES;
    into.wish = n.wish; into.aim = n.aim; into.steer = n.steer;
    into.btn = (uint16_t)((n.btn & ~HI_PRESSES) | presses);
    into.wheel += n.wheel;
    if (n.sel >= 0) into.sel = n.sel;
    if (n.btn & HI_ORDER) into.order = n.order;
}
void ClearPresses(HandInput& in) { in.btn &= (uint16_t)~HI_PRESSES; in.wheel = 0; in.sel = -1; }

void ApplyInput(TrawlWorld& w, int ci, const HandInput& in, float dt) {
    Gannet& g = w.G;
    if (ci < 0 || ci >= (int)g.crew.size()) return;
    Crew& c = g.crew[ci];
    auto on = [&](uint16_t b) { return (in.btn & b) != 0; };
    // ---- the presses (once)
    if (in.sel >= 0 && c.station < 0) c.sel = in.sel;
    if (on(HI_R_P)) g.Reload(ci);
    if (on(HI_T_P) && c.station >= 0 && Stations()[c.station].kind == StationKind::Harpoon) g.explosiveLoaded = !g.explosiveLoaded && g.explosives > 0;
    if (on(HI_E_P)) {
        int d = g.moored && c.deck == 0 && c.station < 0 ? NearestDock(c.p, 1.4f) : -1;
        if (c.deck == DECK_SKIFF && !c.overboard) g.LeaveSkiff(ci);   // up the stern ladder (or ashore, beached)
        else if (c.overboard && !c.dead) g.BoardSkiff(ci);            // a swimmer beside her climbs in
        else if (d >= 0) {}   // (the dock's panels are the player's own screen: their buttons come back as commands)
        else if (c.station < 0 && !c.dead && (g.GaffFloater(ci) || g.HaulSetGear(ci))) {}
        else if (c.station < 0 && !c.dead && g.CrateFish(ci)) {}   // (a dead fish beside you into a catch crate)
        else if (c.station < 0 && !c.dead && !g.moored && g.StartPatch(ci)) {}
        else g.TakeStation(ci);
    }
    if (on(HI_X_P)) g.LeaveStation(ci);
    if (on(HI_ORDER) && g.botsOn) g.OrderBot(in.order);
    if (on(HI_FOLLOW_P) && g.botsOn) g.OrderFollow(ci);
    bool atRod = c.station >= 0 && g.RodAt(c.station) >= 0;
    if (atRod) { if (on(HI_T_P)) g.CycleTackle(ci); }
    else if (in.wheel != 0) g.Scroll(ci, in.wheel);
    // ---- the held controls (every step)
    bool lmb = on(HI_LMB), rmb = on(HI_RMB);
    if (c.overboard || c.dead) {
        // in the water you swim; dead, you walk the deck as a ghost (and can only ring the bell)
        g.Move(ci, in.wish, false, dt);
        if (c.dead) g.Primary(ci, lmb, dt);
        else g.SkiffSwim(ci, lmb, dt);   // (beside a capsized skiff: right her)
        return;
    }
    if (c.deck == DECK_SKIFF) {
        // in the skiff: the oars are the mouse buttons (left port, right starboard; both together pull straight)
        g.Oar(ci, on(HI_LMB_P), on(HI_RMB_P));
        g.Move(ci, {0, 0}, false, dt);
        return;
    }
    if (c.station < 0 && on(HI_SPACE_P) && g.BoardSkiff(ci)) return;   // Space at the stern by the davit: down into the skiff
    if (c.station >= 0 && Stations()[c.station].kind == StationKind::NetWinch) { g.NetInput(ci, lmb, on(HI_RMB_P), dt); g.Move(ci, {0, 0}, false, dt); return; }
    if (c.station >= 0 && Stations()[c.station].kind == StationKind::Harpoon) { g.HarpoonInput(ci, in.aim, on(HI_LMB_P), lmb, on(HI_RMB_P), dt); g.Move(ci, {0, 0}, false, dt); return; }
    if (c.station >= 0 && Stations()[c.station].kind == StationKind::Sonar) {
        // right mouse or Space pings; a left click on the scope marks the contact under it (the aim is the scope's point)
        if (on(HI_RMB_P) || on(HI_SPACE_P)) g.SonarPing(ci);
        if (on(HI_LMB_P)) g.SonarMarkAt(ci, in.aim);
        g.Move(ci, {0, 0}, false, dt);
        return;
    }
    Vector2 wish = in.wish;
    bool atHelm = c.station >= 0 && Stations()[c.station].kind == StationKind::Helm;
    if (c.station >= 0 && Stations()[c.station].kind == StationKind::Lantern && g.boat.lantern == 3) {
        Vector2 d = Vector2Subtract(in.aim, Stations()[c.station].at);   // the searchlight follows the aim
        if (Vector2Length(d) > 0.5f) g.boat.searchAim = atan2f(d.y, d.x);
    }
    if (atHelm) {
        g.Steer(ci, in.steer, dt);
        if (on(HI_W_P)) g.Scroll(ci, 1);
        if (on(HI_S_P)) g.Scroll(ci, -1);
        wish = {0, 0};
    }
    int ri = c.station >= 0 ? g.RodAt(c.station) : -1;
    if (ri >= 0) {
        // a rod: hold to charge the cast (released, it flies at the aim); once out, hold to reel. Space strikes and
        // gaffs; right bows the rod; the aim's side of the line leans it; the wheel is the drag (or the depth)
        Rod& r = g.rods[ri];
        bool casting = r.state == RodState::Idle || r.state == RodState::Charging;
        float lean = 0;
        if (r.state == RodState::Fighting) {
            Vector2 tip = r.TipDeck(), fish = g.boat.ToDeck({r.fight.p.x, r.fight.p.y});
            Vector2 line = Vector2Subtract(fish, tip), to = Vector2Subtract(in.aim, tip);
            float cr = line.x * to.y - line.y * to.x, d2 = Vector2DotProduct(line, to);
            lean = std::clamp(atan2f(cr, std::max(0.01f, fabsf(d2))) / (45 * DEG2RAD), -1.0f, 1.0f);
        }
        g.RodInput(ci, casting && lmb, in.aim, !casting && lmb, on(HI_SPACE_P), lean, rmb, on(HI_SPACE_P), in.wheel);
        g.Move(ci, {0, 0}, on(HI_SHIFT), dt);
        return;
    }
    if (c.station < 0 && on(HI_SPACE_P)) g.Jump(ci);   // Space off a station: a hop (the rail is only a hop away)
    g.Move(ci, wish, on(HI_SHIFT), dt);
    if (c.station < 0) g.UseItem(ci, in.aim, on(HI_LMB_P), lmb, rmb, dt);
    g.Primary(ci, lmb, dt);
    g.Secondary(ci, rmb, dt);
}

// ---------------------------------------------------------------- commands
bool DoCommand(TrawlWorld& w, int ci, int cmd, const std::string& id, int arg, std::string* why) {
    Session& s = w.sess; Gannet& g = w.G;
    auto no = [&](const char* m) { if (why) *why = m; return false; };
    bool dock = s.phase == Phase::Dock && g.moored;
    switch (cmd) {
        case CMD_BUY: if (!dock) return no("the Chandler is ashore"); return s.Buy(id, why);
        case CMD_SELL: if (!dock) return no("the Fish Market is ashore"); if (g.hold.empty() || arg >= (int)g.hold.size()) return no("nothing to sell"); s.Sell(arg); return true;
        case CMD_GUN_BUY: if (!dock) return no("the Gunsmith is ashore"); return s.GunBuy(ci, id, why);
        case CMD_GUN_UPGRADE: if (!dock) return no("the Gunsmith is ashore"); return s.GunUpgrade(ci, arg, why);
        case CMD_GUN_ATTACH: if (!dock) return no("the Gunsmith is ashore"); return s.GunAttach(ci, arg, id, why);
        case CMD_AMMO: if (!dock) return no("the Gunsmith is ashore"); return s.AmmoBuy(id, why);
        case CMD_DELIVER: { if (!dock) return no("the Owners' scales are ashore"); if (g.hold.empty() || arg >= (int)g.hold.size()) return no("nothing to deliver"); int rej = 0; float v = s.Deliver(arg, &rej); if (v <= 0) return no(rej ? "not fresh enough: the Owners turn it away" : "nothing to deliver"); return true; }
        case CMD_SLIP: if (!dock) return no("the Slipway is ashore"); return s.BuySlip(arg, why);
        case CMD_CASTOFF: return s.CastOff(why);
        case CMD_COUNT: if (s.phase != Phase::Dock || s.night < 3) return no("the Owners count after the third night"); s.Count(); return true;
        case CMD_CONTINUE: if (s.phase != Phase::Result) return no("no count to go on from"); s.Continue(); return true;
        case CMD_CANOE: if (!s.Canoe(arg)) return no("no canoe alongside"); return true;
        case CMD_LOCKER_TAKE: case CMD_LOCKER_STOW: {
            if (ci < 0 || ci >= (int)g.crew.size()) return no("no such hand");
            Crew& c = g.crew[ci];
            if (c.station < 0 || Stations()[c.station].kind != StationKind::Locker) return no("not at the locker");
            Slot& hand = c.slots[c.sel];
            if (cmd == CMD_LOCKER_STOW) { if (hand.it == Item::None) return no("nothing in hand"); g.locker.push_back(hand); hand = Slot{}; return true; }
            if (arg < 0 || arg >= (int)g.locker.size()) return no("no such item");
            Slot tmp = hand; hand = g.locker[arg];
            if (tmp.it == Item::None) g.locker.erase(g.locker.begin() + arg); else g.locker[arg] = tmp;
            return true;
        }
        default: return no("unknown order");
    }
}
void WriteInputAction(const HandInput& in, Writer& w) { w.U8(ACT_INPUT); WriteInput(in, w); }
void WriteCmdAction(int cmd, const std::string& id, int arg, Writer& w) { w.U8(ACT_CMD); w.U8((uint8_t)cmd); w.Str(id); w.I32(arg); }

// ---------------------------------------------------------------- the snapshot
namespace {
const char* Intern(const std::string& s) { static std::set<std::string> pool; return pool.insert(s).first->c_str(); }

struct Out {
    Writer& w;
    static constexpr bool reading = false;
    void f(float& v) { w.F32(v); }
    void i(int& v) { w.VarU(((uint32_t)v << 1) ^ (uint32_t)(v >> 31)); }
    void b(bool& v) { w.U8(v ? 1 : 0); }
    void u(uint32_t& v) { w.U32(v); }
    void s(std::string& v) { w.Str(v); }
    void v2(Vector2& v) { f(v.x); f(v.y); }
    void v3(Vector3& v) { f(v.x); f(v.y); f(v.z); }
    template <class E> void e(E& v) { int k = (int)v; i(k); }
    void cs(const char*& p) { bool has = p != nullptr; b(has); if (has) { std::string t = p; s(t); } }
    template <class T, class F> void vec(std::vector<T>& v, F fn) { w.VarU((uint32_t)v.size()); for (auto& x : v) fn(x); }
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
    void v2(Vector2& v) { f(v.x); f(v.y); }
    void v3(Vector3& v) { f(v.x); f(v.y); f(v.z); }
    template <class E> void e(E& v) { int k; i(k); v = (E)k; }
    void cs(const char*& p) { bool has; b(has); if (has) { std::string t; s(t); p = Intern(t); } else p = nullptr; }
    template <class T, class F> void vec(std::vector<T>& v, F fn) {
        uint32_t n = r.VarU();
        if (n > 20000 || r.bad) { r.bad = true; n = 0; }
        v.resize(n);
        for (auto& x : v) { fn(x); if (r.bad) break; }
    }
    bool bad() const { return r.bad; }
};

template <class A> void VisitSlot(A& a, Slot& s) { a.e(s.it); a.i(s.ammo); a.i(s.wpn); a.i(s.lvl); a.i(s.spare); for (int k = 0; k < 3; k++) { int v = s.att[k]; a.i(v); s.att[k] = (int8_t)v; } }
template <class A> void VisitFight(A& a, Fight& f) {
    a.e(f.tackle); a.e(f.line); a.e(f.hook);
    a.v3(f.tip); a.f(f.L); a.f(f.drag); a.f(f.tension);
    a.vec(f.node, [&](Vector3& v) { a.v3(v); });
    if constexpr (A::reading) f.prev = f.node;
    a.b(f.on); a.v3(f.p); a.v3(f.h); a.b(f.alongside); a.f(f.jumpT); a.f(f.t);
    a.cs(f.spec.name); a.f(f.spec.kg); a.e(f.cur); a.e(f.end); a.v2(f.outboard); a.f(f.rodLean); a.b(f.bowed);
}
template <class A> void VisitRod(A& a, Rod& r) {
    a.i(r.station); a.e(r.tackle); a.e(r.line); a.e(r.hook); a.e(r.state);
    a.f(r.charge); a.v2(r.aim); a.v3(r.lure); a.f(r.lureDepth); a.f(r.lineOut); a.f(r.lastTick); a.f(r.lean);
    a.b(r.botAngler); a.s(r.lastCatch); a.s(r.bait);
    a.e(r.bite.stage); a.f(r.bite.t); a.i(r.bite.nibbles);
    VisitFight(a, r.fight);
    a.cs(r.biteSpec.name); a.f(r.biteSpec.kg); a.i(r.fishSp); a.b(r.headOnly);
}
// a long list kept short: its true length (so "something new" can still be seen) and only its last `keep` lines
template <class A> void VisitTail(A& a, std::vector<std::string>& v, int keep) {
    int n = (int)v.size(); a.i(n);
    int k = std::min(n, keep); a.i(k);
    if constexpr (A::reading) { if (n < 0 || n > 100000 || k < 0 || k > n) { a.r.bad = true; return; } v.assign(n, std::string()); }
    for (int j = n - k; j < n; j++) a.s(v[j]);
}

template <class A> void Visit(A& a, TrawlWorld& w) {
    uint32_t magic = 0x31575254;   // "TRW1"
    a.u(magic);
    if constexpr (A::reading) { if (magic != 0x31575254) { a.r.bad = true; return; } }
    // ---- the run
    Session& s = w.sess;
    a.e(s.phase); a.i(s.deadline); a.i(s.night); a.i(s.players);
    a.f(s.quota); a.f(s.money); a.f(s.sold); a.f(s.clock); a.b(s.clockOn); a.s(s.ground); a.e(s.weather); a.f(s.moon); a.f(s.wxAt); a.e(s.wxTo);
    a.e(s.variant); a.e(s.canoe); a.f(s.canoeAt); a.f(s.canoeT); a.s(s.canoeWord);
    a.i(s.tokens); a.b(s.met); for (bool& x : s.slip) a.b(x); a.v2(s.harbour); a.f(s.harbourR); a.u(s.seed); a.f(s.lastSaleTotal);
    VisitTail(a, s.tape, 14);
    a.vec(s.lastSale, [&](SaleLine& l) { a.s(l.name); a.f(l.kg); a.f(l.price); a.f(l.grade); a.f(l.fresh); a.f(l.glut); a.f(l.bonus); a.f(l.value); a.i(l.src); });
    a.vec(s.lastDelivery, [&](SaleLine& l) { a.s(l.name); a.f(l.kg); a.f(l.price); a.f(l.grade); a.f(l.fresh); a.f(l.glut); a.f(l.bonus); a.f(l.value); a.i(l.src); });
    a.f(s.lastDeliveryTotal); a.i(s.lastRejected); a.f(s.carried);
    {   // the glut and the run's firsts (the market's prices)
        std::vector<std::pair<std::string, float>> glut(s.glutKg.begin(), s.glutKg.end());
        a.vec(glut, [&](std::pair<std::string, float>& p) { a.s(p.first); a.f(p.second); });
        std::vector<std::string> firsts(s.catchLog.begin(), s.catchLog.end());
        a.vec(firsts, [&](std::string& x) { a.s(x); });
        if constexpr (A::reading) { s.glutKg = std::map<std::string, float>(glut.begin(), glut.end()); s.catchLog = std::set<std::string>(firsts.begin(), firsts.end()); }
    }
    // ---- the ground: the chart is rebuilt from its seed; what moves comes every time
    Eco& e = w.eco;
    std::string ground = e.ground; uint32_t eseed = e.initSeed;
    a.s(ground); a.u(eseed);
    if constexpr (A::reading) { if (a.bad()) return; if (!ground.empty() && (ground != e.ground || eseed != e.initSeed || !e.g)) e.Init(ground, eseed); }
    a.f(e.tide); a.f(e.coral); a.f(e.time); a.f(e.clock); a.i(e.night); a.f(e.wake);
    a.vec(e.agents, [&](EcoAgent& ag) { a.i(ag.sp); a.i(ag.count); a.v3(ag.p); a.v3(ag.v); a.b(ag.alive); a.f(ag.flash); a.f(ag.hurt); a.f(ag.t); });
    a.vec(e.rafts, [&](Raft& rf) { a.v2(rf.p); a.f(rf.r); });
    // ---- the sea and the boat
    Gannet& g = w.G;
    Sea& sea = g.sea;
    a.e(sea.weather); a.f(sea.swell); a.f(sea.t); a.v2(sea.wind); a.v2(sea.current); a.u(sea.seed);
    Boat& b = g.boat;
    a.v2(b.pos); a.v2(b.vel); a.f(b.heading); a.f(b.yawRate);
    a.f(b.roll); a.f(b.rollVel); a.f(b.pitch); a.f(b.pitchVel); a.f(b.heave); a.f(b.heaveVel);
    for (int k = 0; k < SEC_COUNT; k++) { a.f(b.integrity[k]); a.f(b.integrityMax[k]); a.b(b.patched[k]); }
    a.f(b.bilge); a.f(b.bunker); a.f(b.firebox); a.f(b.pressure); a.f(b.redT); a.f(b.valveT); a.f(b.fireT);
    a.i(b.telegraph); a.i(b.lantern); a.f(b.searchAim); a.b(b.aground); a.f(b.thrustMult); a.f(b.noiseMult);
    a.f(b.rudder); a.f(b.shaft); a.b(b.sunk); a.f(b.noise);
    // ---- the Gannet's stores and gear
    a.f(g.time); a.b(g.moored); a.v2(g.moorPos); a.f(g.moorHeading);
    a.f(g.ice); a.f(g.iceCap); a.i(g.baitShrimp); a.i(g.baitSquid); a.i(g.chum);
    for (bool& x : g.owned) a.b(x);
    a.b(g.watch); a.b(g.searchlight); a.b(g.secondPump); a.f(g.gutT); a.b(g.biggerNet);
    a.b(g.harpoonCannon); a.i(g.harpoons); a.i(g.explosives); a.b(g.explosiveLoaded); a.f(g.harpoonReload);
    a.f(g.gullT); a.f(g.fines); a.f(g.ramT); a.f(g.chumLeft); a.i(g.chargesUsed); a.b(g.botsOn); a.e(g.botSkill);
    bool hasEco = g.eco != nullptr; a.b(hasEco);
    if constexpr (A::reading) g.eco = hasEco ? &w.eco : nullptr;
    VisitTail(a, g.log, 8);
    // ---- the crew
    a.vec(g.crew, [&](Crew& c) {
        a.i(c.slot); a.b(c.bot); a.e(c.role); a.v2(c.p); a.v2(c.v); a.i(c.deck); a.i(c.station); a.f(c.z); a.f(c.vz);
        a.b(c.braced); a.b(c.fallen); a.b(c.overboard); a.f(c.fallT); a.f(c.strokeT); a.f(c.patchT); a.i(c.patchSec); a.i(c.patchKits);
        a.f(c.carryKg); a.v2(c.facing);
        for (Slot& sl : c.slots) VisitSlot(a, sl);
        a.i(c.sel); a.f(c.cool); a.f(c.reloadT); a.i(c.injuries); a.i(c.serious);
        a.b(c.dead); a.b(c.bodyLost); a.v2(c.swim); a.f(c.drownT); a.f(c.bleedT); a.s(c.cause); a.f(c.inkT);
        a.f(c.oarT); a.f(c.rightT);
    });
    {   // the skiff
        Skiff& s = g.skiff;
        a.e(s.state); a.f(s.t); a.v2(s.p); a.v2(s.vel); a.f(s.heading); a.f(s.yawRate); a.f(s.roll); a.f(s.rollV);
        a.f(s.integrity); a.f(s.crabT); a.f(s.noise); a.i(s.landing);
        a.vec(s.load, [&](CatchRec& h) { a.s(h.name); a.f(h.kg); a.f(h.price); a.i(h.sp); a.f(h.grade); a.f(h.fresh); a.b(h.dead); a.b(h.junk); a.f(h.killScore); a.i(h.src); });
    }
    a.vec(g.brains, [&](Gannet::Brain& br) { a.i(br.order); a.i(br.goal); a.i(br.task); a.i(br.target); a.i(br.follow); a.s(br.bark); a.f(br.barkT); });
    // ---- lines and the catch
    a.vec(g.rods, [&](Rod& r) { VisitRod(a, r); });
    VisitRod(a, g.harpoon); a.i(g.harpoonSp);
    a.vec(g.hold, [&](CatchRec& h) {
        a.s(h.name); a.f(h.kg); a.f(h.price); a.i(h.sp); a.f(h.grade); a.f(h.fresh);
        a.b(h.gutted); a.b(h.iced); a.b(h.first); a.b(h.bycatch); a.b(h.protectedSp); a.f(h.aboardT); a.i(h.src);
        a.b(h.dead); a.f(h.flopT); a.v2(h.deckAt);
        a.f(h.hp); a.f(h.hpMax); a.f(h.heading); a.i(h.deckKind); a.f(h.airT); a.f(h.killScore); a.s(h.killHow); a.f(h.killT); a.i(h.grabbed); a.b(h.crated); a.b(h.junk);
    });
    a.f(g.deckBlood); a.i(g.junkBottles); a.i(g.junkKeys); a.i(g.junkCharts);
    a.i(g.ammoRounds); a.i(g.ammoShells); a.i(g.ammoSpears); a.i(g.ammoFlares); a.i(g.ammoPellets); a.i(g.ammoRivets);
    // ---- what's in the water
    a.vec(g.shots, [&](Projectile& p) { a.e(p.kind); a.v3(p.p); a.v3(p.v); a.i(p.owner); a.f(p.life); a.b(p.tether); a.b(p.inWater); });
    a.vec(g.floaters, [&](Floater& f) { a.s(f.name); a.i(f.sp); a.f(f.kg); a.f(f.price); a.f(f.grade); a.v2(f.p); a.f(f.life); a.b(f.tethered); });
    a.vec(g.flares, [&](FlareLight& f) { a.v2(f.p); a.f(f.t); });
    a.vec(g.thieves, [&](Gannet::Thief& t) { a.i(t.kind); a.s(t.fish.name); a.f(t.fish.kg); a.i(t.fish.sp); a.v2(t.p); a.v2(t.v); a.f(t.z); a.f(t.t); });
    Trawl& n = g.net;
    a.e(n.state); a.f(n.t); a.f(n.load); a.f(n.depth); a.f(n.backT); a.b(n.meshInit);
    if (n.meshInit && n.state != NetState::Stowed) for (int k = 0; k < 8 * 6; k++) { a.v3(n.node[k]); if constexpr (A::reading) n.prev[k] = n.node[k]; }
    a.vec(g.longlines, [&](Longline& l) { a.v2(l.a); a.v2(l.b); a.f(l.age); a.vec(l.hooks, [&](SetHook& h) { a.i(h.sp); a.b(h.head); a.f(h.kg); }); });
    a.vec(g.pots, [&](Pot& p) { a.v2(p.p); a.f(p.age); a.i(p.n); });
    a.vec(g.rings, [&](LifeRing& r) { a.i(r.state); a.v2(r.p); a.v2(r.v); a.i(r.thrower); a.i(r.holder); a.f(r.haulT); });
    a.vec(g.locker, [&](Slot& sl) { VisitSlot(a, sl); });
    // ---- the sonar: the scope's returns, the marks every hand's arrows point at
    SonarState& so = g.sonar;
    a.f(so.cool); a.f(so.sinceP); a.i(so.band);
    a.vec(so.ret, [&](SonarReturn& r) { a.v3(r.p); a.i(r.sp); a.e(r.kind); a.f(r.size); a.f(r.t); a.i(r.count); a.b(r.passive); });
    a.vec(so.marks, [&](SonarMark& m) { a.v2(m.p); a.f(m.t); a.s(m.what); a.i(m.by); });
}
} // namespace

void WriteWorld(const TrawlWorld& w, Writer& out) { Out o{out}; Visit(o, const_cast<TrawlWorld&>(w)); }
bool ReadWorld(Reader& r, TrawlWorld& w) {
    In i{r};
    Visit(i, w);
    w.sess.G = &w.G; w.sess.E = &w.eco;
    return !r.bad;
}

// ---------------------------------------------------------------- the host
namespace {
class TrawlHost : public arcade::GameHost {
public:
    TrawlWorld w;
    HandInput pend[arcade::MAX_PLAYERS];
    int players = 1;
    float acc = 0;
    uint32_t tick = 0;
    mutable uint32_t cachedTick = ~0u;
    mutable std::vector<uint8_t> cache;

    void Start(int n, uint32_t seed) override {
        players = std::clamp(n, 1, arcade::MAX_PLAYERS);
        w.sess.Begin(w.G, w.eco, players, seed);
        for (auto& c : w.G.crew) c.bot = false;
        w.G.botsOn = false;
        for (auto& p : pend) p = HandInput{};
        acc = 0; tick = 0; cachedTick = ~0u;
    }
    bool Act(int p, Reader& r) override {
        if (p < 0 || p >= players) return false;
        int kind = (int)r.U8();
        if (kind == ACT_INPUT) { HandInput in; if (!ReadInput(r, in)) return false; MergeInput(pend[p], in); return false; }
        if (kind == ACT_CMD) {
            int cmd = (int)r.U8(); std::string id = r.Str(); int arg = r.I32();
            if (r.bad || cmd >= CMD_N) return false;
            return DoCommand(w, p, cmd, id, arg, nullptr);
        }
        return false;
    }
    bool Tick(float dt, uint32_t ai) override {
        // the AI seats are the bot crew (a dropped player's hand is taken by a bot after the session's grace)
        bool any = false;
        for (int i = 0; i < (int)w.G.crew.size(); i++) { bool bot = (ai >> i) & 1; w.G.crew[i].bot = bot; any |= bot; }
        w.G.botsOn = any;
        const float step = 1 / 60.0f;
        acc += std::min(dt, 0.1f);
        while (acc >= step) {
            acc -= step;
            for (int p = 0; p < players && p < (int)w.G.crew.size(); p++) {
                if ((ai >> p) & 1) { pend[p] = HandInput{}; continue; }
                ApplyInput(w, p, pend[p], step);
                ClearPresses(pend[p]);
            }
            w.G.Step(step);
            w.sess.Step(step);
            tick++;
        }
        return true;
    }
    void Snapshot(int, Writer& out) const override {
        // (every hand sees the same boat: the world is written once a step, and copied to each player)
        if (cachedTick != tick) { Writer t; WriteWorld(w, t); cache = std::move(t.b); cachedTick = tick; }
        out.Bytes(cache.data(), cache.size());
    }
    bool Over() const override { return w.sess.phase == Phase::Over; }
};
}
std::unique_ptr<arcade::GameHost> MakeTrawlHost() { return std::make_unique<TrawlHost>(); }
TrawlWorld* TrawlHostWorld(arcade::GameHost* h) { auto* t = dynamic_cast<TrawlHost*>(h); return t ? &t->w : nullptr; }
uint32_t TrawlDataHash() {
    Writer w;
    w.Bytes(&D(), sizeof(TrawlData));
    w.U32((uint32_t)Stations().size()); for (const auto& s : Stations()) { w.F32(s.at.x); w.F32(s.at.y); w.U8(s.deck); }
    for (const auto& i : ChandlerItems()) { w.Str(i.id); w.I32(i.price); }
    for (const auto& i : SlipwayItems()) { w.Str(i.id); w.I32(i.price); }
    const SpeciesDB& db = Species();
    w.U32((uint32_t)db.sp.size()); for (const auto& s : db.sp) { w.Str(s.name); w.F32(s.price); w.F32(s.kgLo); w.F32(s.kgHi); }
    return Fnv1a(w.b.data(), w.b.size());
}

// ---------------------------------------------------------------- --trawl-net-test
int RunTrawlNetTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Trawl: network play (inputs, commands, snapshots)\n");
    const float dt = 1 / 60.0f;
    // the input frame survives the wire
    {
        HandInput in; in.wish = {0.6f, -0.8f}; in.aim = {12.5f, -3.25f}; in.steer = -1; in.wheel = 2; in.btn = HI_LMB | HI_E_P | HI_ORDER; in.sel = 2; in.order = 7;
        Writer w; WriteInput(in, w); Reader r(w.b); HandInput o;
        check(ReadInput(r, o) && fabsf(o.wish.x - 0.6f) < 0.01f && fabsf(o.wish.y + 0.8f) < 0.01f && o.aim.x == 12.5f && o.steer == -1 && o.btn == in.btn && o.sel == 2 && o.order == 7,
              TextFormat("an input frame round-trips in %d bytes", (int)w.b.size()));
        HandInput acc; MergeInput(acc, in); HandInput next; next.btn = HI_LMB; next.wheel = 1; MergeInput(acc, next);
        check((acc.btn & HI_E_P) && acc.wheel == 3 && acc.sel == 2, "presses and the wheel add up between steps; held keys are the newest");
        ClearPresses(acc);
        check(acc.btn == HI_LMB && acc.wheel == 0 && acc.sel == -1, "and once a step has used them only the held keys remain");
        Reader junk(w.b.data(), 5); HandInput o2;
        check(!ReadInput(junk, o2), "a short frame is refused");
    }
    // a hand walks to the port rod and takes it by input alone; a command buys bait
    {
        TrawlWorld w; w.sess.Begin(w.G, w.eco, 2, 41);
        w.G.crew[0].p = {-1, 0.8f}; w.G.crew[1].p = {-2, -0.8f};
        int port = -1; for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == StationKind::PortRod) port = i;
        Vector2 at = Stations()[port].at;
        for (int k = 0; k < 60 * 10 && w.G.crew[1].station != port; k++) {
            HandInput in;
            Vector2 d = Vector2Subtract(at, w.G.crew[1].p);
            if (Vector2Length(d) > 0.4f) in.wish = Vector2Normalize(d); else in.btn |= HI_E_P;
            ApplyInput(w, 1, in, dt); w.G.Step(dt);
        }
        check(w.G.crew[1].station == port, "walking by input and pressing E, hand 1 takes the port rod");
        float m0 = w.sess.money; std::string why;
        check(DoCommand(w, 0, CMD_BUY, "shrimp", 0, &why) && w.G.baitShrimp == 10 && w.sess.money < m0, "a command buys a tin of shrimp at the Chandler");
        bool refused = !DoCommand(w, 0, CMD_CONTINUE, "", 0, &why);
        check(refused, TextFormat("a command that makes no sense now is refused (%s)", why.c_str()));
    }
    // the snapshot: a busy night out, written, read into a mirror, and written again: the same bytes
    {
        TrawlWorld w; w.sess.Begin(w.G, w.eco, 4, 20262);
        w.G.botsOn = true;
        w.sess.Buy("shrimp"); w.sess.Buy("shrimp");
        while (w.G.boat.bunker < 40 && w.sess.Buy("coal")) {}
        w.G.crew[0].p = {3.0f, 0.8f};
        w.sess.CastOff();
        w.G.boat.pos = Vector2Add(w.sess.harbour, {w.sess.harbourR + 40, 10});
        for (int k = 0; k < 60 * 120; k++) { w.G.Step(dt); w.sess.Step(dt); }
        // the sonar: a ping finds what's under her; the next waits 3 s; a mark goes to everyone
        bool pinged = w.G.SonarPing(0);
        int found = (int)w.G.sonar.ret.size();
        check(pinged && found > 0 && !w.G.SonarPing(0), TextFormat("a sonar ping finds %d contacts within 150 m, and the next waits its 3 s", found));
        bool marked = found > 0 && w.G.SonarMarkAt(0, w.G.boat.ToDeck({w.G.sonar.ret[0].p.x, w.G.sonar.ret[0].p.y}));
        check(marked && w.G.sonar.marks.size() == 1, TextFormat("a click on a contact marks it: \"%s\"", w.G.sonar.marks.empty() ? "" : w.G.sonar.marks[0].what.c_str()));
        Writer a; WriteWorld(w, a);
        TrawlWorld m;
        Reader r(a.b);
        bool ok = ReadWorld(r, m);
        Writer b; WriteWorld(m, b);
        check(ok && r.Done(), TextFormat("a night's world is %d bytes; the mirror reads all of it", (int)a.b.size()));
        check(a.b == b.b, "the mirror writes back exactly the bytes it read (nothing drawn is left out)");
        check(m.G.crew.size() == 4 && m.eco.g && m.eco.n == w.eco.n && m.eco.DepthAt(w.G.boat.pos) == w.eco.DepthAt(w.G.boat.pos) && m.G.eco == &m.eco,
              "the mirror rebuilt the same chart from the ground's seed");
        int out = 0; for (auto& rd : m.G.rods) out += rd.state != RodState::Idle;
        check(m.sess.phase == Phase::Night && !m.G.hold.empty() == !w.G.hold.empty() && out > 0, TextFormat("the mirror has the night: %d fish aboard, %d lines out", (int)m.G.hold.size(), out));
        check(m.G.sonar.ret.size() == w.G.sonar.ret.size() && m.G.sonar.marks.size() == 1, "every hand's mirror has the sonar's returns and the mark (their arrows)");
        // a mirror is reused snapshot after snapshot (its chart is kept while the ground stays the same)
        for (int k = 0; k < 60; k++) { w.G.Step(dt); w.sess.Step(dt); }
        Writer a2; WriteWorld(w, a2); Reader r2(a2.b);
        const float* depth0 = m.eco.depth.data();
        bool ok2 = ReadWorld(r2, m);
        check(ok2 && m.eco.depth.data() == depth0, "the next snapshot reuses the mirror's chart");
        Reader bad(a.b.data(), a.b.size() / 2); TrawlWorld m2;
        check(!ReadWorld(bad, m2), "a cut-off snapshot is refused");
    }
    // the host game: two players and an AI seat; the AI's hand is a bot, a player's input moves its hand
    {
        auto h = MakeTrawlHost();
        h->Start(3, 77);
        TrawlWorld* w = TrawlHostWorld(h.get());
        w->G.crew[0].p = {-1, 0.8f}; w->G.crew[1].p = {-2, -0.8f}; w->G.crew[2].p = {-3, 0.8f};
        Vector2 p0 = w->G.crew[1].p;
        for (int k = 0; k < 60; k++) {
            HandInput in; in.wish = {1, 0};
            Writer a; WriteInputAction(in, a); Reader r(a.b); h->Act(1, r);
            h->Tick(dt, 1u << 2);
        }
        check(w->G.crew[1].p.x > p0.x + 1.5f, "the host moves hand 1 by its player's input");
        check(w->G.botsOn && w->G.crew[2].bot && !w->G.crew[1].bot, "the AI seat is a bot hand; the players' hands aren't");
        Writer s1, s2; h->Snapshot(0, s1); h->Snapshot(1, s2);
        check(!s1.b.empty() && s1.b == s2.b, TextFormat("every player gets the same world (%d bytes)", (int)s1.b.size()));
        Writer c; WriteCmdAction(CMD_BUY, "ice", 0, c); Reader rc(c.b);
        float ice0 = w->G.ice;
        check(h->Act(0, rc) && w->G.ice > ice0, "a command from a player buys ice on the host");
    }
    printf(fails ? "%d FAILED\n" : "trawl-net-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}

// ---------------------------------------------------------------- --net-loop trawl
int RunTrawlNetLoop(bool forceMemory) {
    using namespace arcade;
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::string err;
    bool real = !forceMemory && net::Init(&err);
    auto make = [&]() { return real ? net::MakeTransport() : net::MakeMemoryTransport(); };
    uint16_t port = 47800;
    std::string addr = real ? "127.0.0.1:" + std::to_string(port) : "mem:" + std::to_string(port);
    printf("net-loop trawl over %s: a host and five guests\n", real ? "GameNetworkingSockets (loopback UDP)" : "the in-memory transport");
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    const int NG = 5;
    arcade::Session host, gs[NG];
    Profile ph{"Skipper", 10};
    if (!host.Host(ph, G_TRAWL, &err, port, make(), false)) { printf("FAIL: host: %s\n", err.c_str()); return 1; }
    double t = 0; const float dt = 1 / 60.0f;
    TrawlWorld mirror[NG]; int seen[NG] = {}; int mirrorOk[NG] = {}; size_t bytes = 0; int snaps = 0;
    auto pace = [&]() { if (real) std::this_thread::sleep_for(std::chrono::milliseconds(16)); };
    auto step = [&](int frames) {
        for (int f = 0; f < frames; f++) {
            t += dt; host.Update(t, dt);
            for (int k = 0; k < NG; k++) {
                gs[k].Update(t, dt);
                if (gs[k].stage == S_PLAYING && gs[k].stateVersion != seen[k] && !gs[k].Snapshot().empty()) {
                    seen[k] = gs[k].stateVersion;
                    Reader r(gs[k].Snapshot());
                    if (ReadWorld(r, mirror[k])) mirrorOk[k]++;
                    bytes += gs[k].Snapshot().size(); snaps++;
                }
            }
            pace();
        }
    };
    auto until = [&](std::function<bool()> ok, float seconds) { for (int i = 0; i < seconds * 60 && !ok(); i++) step(1); };
    for (int k = 0; k < NG; k++) { Profile p{std::string("Hand ") + char('A' + k), (uint64_t)(20 + k)}; if (!gs[k].Join(p, addr, &err, 0, make())) { printf("FAIL: join: %s\n", err.c_str()); return 1; } }
    until([&] { for (auto& g : gs) if (g.stage != S_LOBBY) return false; return true; }, 15);
    for (auto& g : gs) g.SetReady(true);
    until([&] { for (int s = 1; s <= NG; s++) if (!host.seats[s].ready) return false; return true; }, 10);
    std::string why;
    check(host.Launch(&why), "six hands launch the Trawl" + (why.empty() ? std::string() : ": " + why));
    until([&] { for (int k = 0; k < NG; k++) if (mirrorOk[k] == 0) return false; return true; }, 10);
    bool all = true; for (int k = 0; k < NG; k++) all = all && mirrorOk[k] > 0 && mirror[k].G.crew.size() == 6;
    check(all, "every guest mirrors the Gannet with six hands aboard");
    TrawlWorld* truth = TrawlHostWorld(host.HostGame());
    // guest 0 walks to the port rod and takes it, by input alone
    auto send = [&](int k, const HandInput& in) { Writer w; WriteInputAction(in, w); gs[k].Act(w); };
    auto cmd = [&](int k, int c, const std::string& id) { Writer w; WriteCmdAction(c, id, 0, w); gs[k].Act(w); };
    int port0 = -1; for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == StationKind::PortRod) port0 = i;
    int me = gs[0].MyPlayer();
    for (int f = 0; f < 60 * 15 && mirror[0].G.crew[me].station != port0; f++) {
        HandInput in;
        Vector2 d = Vector2Subtract(Stations()[port0].at, mirror[0].G.crew[me].p);
        if (Vector2Length(d) > 0.4f) in.wish = Vector2Normalize(d); else if (f % 20 == 0) in.btn |= HI_E_P;
        send(0, in); step(1);
    }
    check(mirror[0].G.crew[me].station == port0, TextFormat("guest 0 (hand %d) walks to the port rod and takes it; its own mirror shows it", me));
    // a guest's command at the dock
    float m0 = mirror[1].sess.money;
    cmd(1, CMD_BUY, "shrimp"); cmd(1, CMD_BUY, "shrimp");
    until([&] { return mirror[1].G.baitShrimp >= 20; }, 5);
    check(mirror[1].G.baitShrimp >= 20 && mirror[1].sess.money < m0, "a guest buys bait at the Chandler; everyone's mirror shows it");
    // the skipper is on the quay: cast-off waits until every hand is aboard
    cmd(2, CMD_CASTOFF, "");
    step(30);
    check(mirror[2].sess.phase == Phase::Dock, "cast-off is refused with the skipper still on the quay");
    {
        Writer w0; HandInput in; in.wish = {0, 1};
        for (int f = 0; f < 60 * 4 && truth->G.crew[host.MyPlayer()].p.y < 0.4f; f++) { Writer w; WriteInputAction(in, w); host.Act(w); step(1); }
        HandInput stop; Writer w; WriteInputAction(stop, w); host.Act(w); step(5);
    }
    while (truth->G.boat.bunker < 40 && truth->sess.Buy("coal")) {}
    cmd(2, CMD_CASTOFF, "");
    until([&] { return mirror[3].sess.phase == Phase::SailOut; }, 5);
    check(mirror[3].sess.phase == Phase::SailOut && !mirror[3].G.moored, "the skipper aboard, a guest casts off: she's under way on every screen");
    // out on the ground (the test tows her there), guest 0 casts its line
    truth->G.boat.pos = Vector2Add(truth->sess.harbour, {truth->sess.harbourR + 40, 10});
    step(30);
    int ri = -1; for (int i = 0; i < (int)mirror[0].G.rods.size(); i++) if (mirror[0].G.rods[i].station == port0) ri = i;
    for (int f = 0; f < 60 * 4 && ri >= 0 && mirror[0].G.rods[ri].state == RodState::Idle; f++) {
        HandInput in; in.aim = {-2, -14};
        if (f % 120 < 55) in.btn |= HI_LMB;
        send(0, in); step(1);
    }
    check(ri >= 0 && mirror[0].G.rods[ri].state != RodState::Idle, "guest 0 holds and lets go: its line goes out over the port rail");
    // a guest leaves: a bot takes its hand at once
    int gone = gs[4].MyPlayer();
    gs[4].Leave();
    until([&] { return mirror[0].G.crew[gone].bot; }, 8);
    check(mirror[0].G.crew[gone].bot && mirror[0].G.botsOn, TextFormat("guest 4 leaves: hand %d goes on as a bot", gone));
    // the mirrors agree with the host
    step(20);
    Writer tw0; WriteWorld(*truth, tw0);
    TrawlWorld chk; Reader rc(tw0.b); ReadWorld(rc, chk);
    float drift = Vector2Distance(chk.G.boat.pos, mirror[1].G.boat.pos);
    check(drift < 1.0f, TextFormat("a guest's Gannet is where the host's is (%.2f m behind)", drift));
    printf("  (%d snapshots, %.1f kB each on average, %.0f kB/s to each guest at 20 Hz)\n", snaps, snaps ? bytes / 1024.0 / snaps : 0.0, snaps ? bytes / 1024.0 / snaps * 20 : 0.0);
    host.Leave();
    for (auto& g : gs) g.Leave();
    if (real) net::Shutdown();
    printf(fails ? "net-loop trawl: %d FAILED\n" : "net-loop trawl: all checks passed\n", fails);
    return fails ? 1 : 0;
}

} // namespace tw
