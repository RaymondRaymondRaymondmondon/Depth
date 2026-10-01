// The skiff (design doc v2, "The skiff"): the Gannet's 4.5 m rowing boat on the stern davit, the Trawl's facility run.
// A hand at the davit lowers it in 8 s and recovers it in 10 (alongside the stern, the Gannet stopped). Space at the
// stern beside the davit drops into it; E in it climbs back up (or steps ashore when it's beached). The oars are the
// two mouse buttons, a rhythm: 1.5 m/s with one rower, 2.2 with two; a stroke too soon after the last catches a crab
// and stops her a second; each stroke splashes a little noise into the water. One section of 40; past 25 degrees of
// roll she capsizes and everyone aboard goes in (a swimmer beside her rights her in 4 s, then climbs in).
#include "trawl.h"
#include "trawl_eco.h"
#include "trawl_session.h"
#include "trawl_weapons.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tw {

namespace {
const float SKIFF_HALF_L = 2.25f, SKIFF_HALF_B = 0.8f;
const float ROW_DRAG = 0.9f;                       // per second: the hull's drag along her length
const Vector2 SEATS[2] = {{0.2f, 0}, {-1.3f, 0}};  // the rower's thwart; the stern sheets
int Davit() { for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == StationKind::Davit) return i; return -1; }
}

Vector2 Gannet::SkiffBerth() const { return boat.ToWorld({-12.6f, 0}); }
bool Gannet::SkiffAlongside(float r) const { return (skiff.state == SkiffState::Afloat || skiff.state == SkiffState::Recovering) && Vector2Distance(skiff.p, SkiffBerth()) < r; }
int Gannet::SkiffRowers() const { int n = 0; for (const auto& c : crew) if (c.deck == DECK_SKIFF && !c.dead && !c.overboard) n++; return n; }
Vector2 Gannet::HandWorld(int ci) const {
    const Crew& c = crew[ci];
    if (c.overboard) return c.swim;
    if (c.deck == DECK_SKIFF) return skiff.ToWorld(c.p);
    if (c.deck == DECK_SHORE && skiff.landing >= 0 && skiff.landing < (int)landings.size()) return landings[skiff.landing].ToWorld(c.p);
    return boat.ToWorld(c.p);
}

bool Gannet::SwimInSkiffFrame(int ci) const {
    const Crew& c = crew[ci];
    if (!c.overboard || skiff.state == SkiffState::Stowed || skiff.state == SkiffState::Lowering || skiff.state == SkiffState::Lost) return false;
    return Vector2Distance(c.swim, skiff.p) < Vector2Distance(c.swim, boat.pos) - 6;
}
void Gannet::SkiffSwim(int ci, bool held, float dt) {
    Crew& c = crew[ci];
    Skiff& s = skiff;
    if (!c.overboard || c.dead || s.state != SkiffState::Capsized || Vector2Distance(c.swim, s.p) > SKIFF_HALF_L + 1.0f) { c.rightT = 0; return; }
    if (!held) { c.rightT = std::max(0.0f, c.rightT - dt); return; }
    c.rightT += dt;
    if (c.rightT >= D().skiffRight) {
        c.rightT = 0; s.state = SkiffState::Afloat; s.roll = 0; s.rollV = 0;
        Say("The skiff is righted (E climbs in)");
    }
}
void Gannet::DavitWork(int ci, bool held, float dt) {
    Skiff& s = skiff;
    if (!held) { if (s.state == SkiffState::Recovering || (s.state == SkiffState::Afloat && s.t < 0)) { s.state = SkiffState::Afloat; s.t = 0; } else if (s.state == SkiffState::Stowed) s.t = 0; return; }   // (a lowering pauses where it is)
    if (s.state == SkiffState::Stowed && s.t < 0) return;   // (just hauled up: let go first)
    if (s.state == SkiffState::Stowed || s.state == SkiffState::Lowering) {
        s.state = SkiffState::Lowering;
        s.t += dt;
        if (s.t >= D().skiffLower) {
            s.state = SkiffState::Afloat; s.t = -1;   // (-1: let go of the davit before it will haul her back up)
            s.p = SkiffBerth(); s.heading = boat.heading + PI; s.vel = boat.vel; s.yawRate = 0; s.roll = s.rollV = 0;
            s.integrity = D().skiffIntegrity; s.landing = -1;
            Say("The skiff is in the water astern (Space at the davit drops into her)");
        }
        return;
    }
    if (s.state == SkiffState::Afloat && s.t < 0) return;
    if (s.state == SkiffState::Afloat || s.state == SkiffState::Recovering) {
        if (!SkiffAlongside(4)) { s.state = SkiffState::Afloat; s.t = 0; return; }
        if (boat.Speed() > 0.4f) { if (s.state != SkiffState::Recovering || s.t == 0) Say("Stop her first: the skiff can't come up under way"); s.state = SkiffState::Afloat; s.t = 0; return; }
        s.state = SkiffState::Recovering;
        s.t += dt;
        if (s.t >= D().skiffRecover) FinishRecovery();
    }
}

void Gannet::FinishRecovery() {
    // hoisted: the catch onto the aft deck (dead, for the crates), and anyone still in her steps off at the davit
    Skiff& s = skiff;
    for (auto& r : s.load) { CatchRec h = r; h.deckAt = {-9.6f + (float)(hold.size() % 3) * 0.3f, -1.0f + (float)(hold.size() % 5) * 0.4f}; hold.push_back(h); }
    for (auto& r : towed) { CatchRec h = r; h.deckAt = {-9.0f, 0.0f}; hold.push_back(h); }
    if (!towed.empty()) Say(TextFormat("The tow line comes aboard: %d fish onto the aft deck", (int)towed.size()));
    towed.clear();
    if (!s.load.empty()) Say(TextFormat("The skiff comes up: %d things out of her onto the aft deck", (int)s.load.size()));
    else Say("The skiff comes up on the davit");
    s.load.clear();
    for (auto& c : crew) if (c.deck == DECK_SKIFF) { c.deck = 0; c.p = {-10.2f, c.p.y < -0.5f ? -0.8f : 0.8f}; c.v = {0, 0}; c.workOn = -1; }
    s.state = SkiffState::Stowed; s.t = -1; hookT = 0;
}
bool Gannet::BoardSkiff(int ci) {
    Crew& c = crew[ci];
    Skiff& s = skiff;
    if (c.dead || !s.Up()) return false;
    if (s.state == SkiffState::Beached && !(c.deck == DECK_SHORE && s.landing >= 0 && s.landing < (int)landings.size() && Vector2Distance(c.p, Vector2Subtract(s.p, landings[s.landing].at)) < 2.8f)) return false;
    int aboard = 0; for (const auto& o : crew) if (o.deck == DECK_SKIFF && !o.overboard && !o.dead) aboard++;
    if (c.overboard) {
        if (Vector2Distance(c.swim, s.p) > SKIFF_HALF_L + 0.8f) return false;
        if (aboard >= 2) { Say("The skiff takes two"); return false; }
        c.overboard = false; c.drownT = 0; Say("Hauled over the gunwale into the skiff");
    } else if (c.deck == DECK_SHORE) {
        if (aboard >= 2) { Say("The skiff takes two"); return false; }
        Say("Into the skiff");
    } else {
        int d = Davit();
        if (c.deck != 0 || d < 0 || Vector2Distance(c.p, Stations()[d].at) > 1.6f || !SkiffAlongside(4)) return false;
        if (aboard >= 2) { Say("The skiff takes two"); return false; }
        Say("Down into the skiff");
    }
    c.deck = DECK_SKIFF; c.station = -1; c.v = {0, 0}; c.z = c.vz = 0; c.oarT = 9;
    c.p = SEATS[aboard];
    for (const auto& o : crew) if (&o != &c && o.deck == DECK_SKIFF && !o.overboard && Vector2Distance(o.p, c.p) < 0.3f) c.p = SEATS[1 - aboard];
    return true;
}

bool Gannet::LeaveSkiff(int ci) {
    Crew& c = crew[ci];
    if (c.deck != DECK_SKIFF || c.overboard) return false;
    if (BeachSkiff(ci)) return true;
    if (SkiffAlongside(4)) { c.deck = 0; c.p = {-10.2f, 0}; c.v = {0, 0}; Say("Up the stern ladder onto the Gannet"); return true; }
    return false;
}

void Gannet::Oar(int ci, bool port, bool star) {
    Crew& c = crew[ci];
    Skiff& s = skiff;
    if ((!port && !star) || c.deck != DECK_SKIFF || c.overboard || c.dead) return;
    if (s.state == SkiffState::Beached && s.landing >= 0 && s.landing < (int)landings.size()) {
        // shoved off the sand, bow out
        Vector2 out = Vector2Normalize(Vector2Subtract(s.p, landings[s.landing].at));
        s.state = SkiffState::Afloat; s.heading = atan2f(out.y, out.x); s.vel = Vector2Scale(out, 0.8f); s.p = Vector2Add(s.p, Vector2Scale(out, 0.6f)); c.oarT = 0;
        Say("Pushed off the beach");
        return;
    }
    if (s.state != SkiffState::Afloat) return;
    if (c.oarT < D().skiffCrab || s.crabT > 0) {
        // rushed: the blade digs in and stops her
        s.crabT = 1.0f; s.vel = Vector2Scale(s.vel, 0.35f); s.yawRate *= 0.5f; c.oarT = 0;
        if (eco) eco->AddNoise({s.p.x, s.p.y, 0.3f}, 2.0f);
        Say("Caught a crab! (keep the rhythm)");
        return;
    }
    c.oarT = 0;
    int rowers = std::max(1, SkiffRowers());
    float top = rowers >= 2 ? D().skiffRowTwo / 2 : D().skiffRowOne;    // each rower's share of the steady speed
    float k = 1.12f * top * ROW_DRAG * D().skiffStroke * (c.Has(INJ_BROKEN_ARM) ? 0.5f : 1.0f);
    if (port && star) { s.vel = Vector2Add(s.vel, Vector2Scale(s.Forward(), k)); }
    else {
        // one oar: way on and a turn away from it (left, right, left... runs straight) (the port oar swings her bow to starboard)
        s.vel = Vector2Add(s.vel, Vector2Scale(s.Forward(), k));
        s.yawRate += port ? 0.42f : -0.42f;
    }
    s.noise = std::min(3.0f, s.noise + 0.5f);
    if (eco) eco->AddNoise({s.p.x, s.p.y, 0.3f}, 0.6f);
}

void Gannet::SkiffRock(float rad) { skiff.rollV += rad; }

void Gannet::SkiffHit(float dmg, const std::string& why) {
    Skiff& s = skiff;
    if (!s.Up()) return;
    s.integrity -= dmg;
    Say(TextFormat("The skiff is hit (%s): %.0f of %.0f left", why.c_str(), std::max(0.0f, s.integrity), D().skiffIntegrity));
    if (s.integrity <= 0) { SkiffCapsize("stove in: " + why); s.state = SkiffState::Lost; Say("The skiff goes down"); }
}

void Gannet::SkiffCapsize(const std::string& why) {
    Skiff& s = skiff;
    if (!s.Up()) return;
    s.state = SkiffState::Capsized; s.t = 0; s.roll = 0; s.rollV = 0; s.crabT = 0;
    for (int k = 0; k < (int)crew.size(); k++) {
        Crew& c = crew[k];
        if (c.deck != DECK_SKIFF || c.overboard || c.dead) continue;
        Vector2 at = s.ToWorld({c.p.x, c.p.y < 0 ? -1.4f : 1.4f});
        c.deck = 0; c.station = -1;
        GoOverboard(k, "the skiff capsized: " + why);
        c.swim = at;
    }
    // her catch spills: the fish float off (gaff them, or the sea has them); salvage sinks
    for (const auto& r : s.load) if (!r.junk) { Floater f; f.name = r.name; f.sp = r.sp; f.kg = r.kg; f.price = r.price; f.grade = r.grade; f.p = s.ToWorld({0, 0}); floaters.push_back(f); }
    s.load.clear();
    if (!towed.empty()) { Say("The tow line parts"); towed.clear(); }
    Say("The skiff capsizes! (" + why + ") - a swimmer beside her can right her: hold left mouse");
}

bool Gannet::SkiffLand(const CatchRec& r) {
    if (skiff.LoadKg() + r.kg > D().skiffLoad) { Say("The skiff can't take any more weight"); return false; }
    skiff.load.push_back(r);
    return true;
}

void Gannet::StepSkiff(float dt) {
    Skiff& s = skiff;
    for (auto& c : crew) c.oarT += dt;
    if (s.state == SkiffState::Stowed || s.state == SkiffState::Lowering) {
        // she hangs on the davit over the stern
        s.p = boat.ToWorld({-11.4f, 0}); s.heading = boat.heading; s.vel = boat.vel; s.roll = boat.roll; s.rollV = 0;
        return;
    }
    if (s.state == SkiffState::Lost) return;
    if (s.state == SkiffState::Recovering) {
        // the falls take her weight: drawn in under the davit
        Vector2 to = Vector2Subtract(SkiffBerth(), s.p);
        s.p = Vector2Add(s.p, Vector2Scale(to, std::min(1.0f, dt * 1.5f)));
        s.vel = boat.vel; s.heading += (boat.heading + PI - s.heading) * std::min(1.0f, dt);
        return;
    }
    if (s.state == SkiffState::Beached) { s.vel = {0, 0}; s.yawRate = 0; s.roll *= 0.9f; return; }
    // a bot aboard with a human rower pulls in time with them (both oars on the beat); a bot alone holds water
    bool humanRowing = false;
    for (const auto& c : crew) if (c.deck == DECK_SKIFF && !c.bot && !c.overboard && c.oarT < D().skiffStroke * 1.6f) humanRowing = true;
    if (s.state == SkiffState::Afloat && humanRowing)
        for (int k = 0; k < (int)crew.size(); k++) { Crew& c = crew[k]; if (c.bot && c.deck == DECK_SKIFF && !c.overboard && !c.dead && c.oarT >= D().skiffStroke) Oar(k, true, true); }
    // the water: drag along her length, a keel against sliding sideways, the current
    Vector2 f = s.Forward(), side{-f.y, f.x};
    float vf = Vector2DotProduct(s.vel, f), vs = Vector2DotProduct(s.vel, side);
    float vmax = (SkiffRowers() >= 2 ? D().skiffRowTwo : D().skiffRowOne) * 1.15f;
    float towKg = 0; for (const auto& t : towed) towKg += t.kg;
    vf -= vf * (ROW_DRAG + towKg / 120.0f) * dt;   // (a fish on the tow line drags at her)
    if (vf > vmax) vf -= (vf - vmax) * std::min(1.0f, 4 * dt);
    vs *= expf(-3.0f * dt);
    if (s.crabT > 0) { s.crabT -= dt; vf *= expf(-6 * dt); }
    s.vel = Vector2Add(Vector2Scale(f, vf), Vector2Scale(side, vs));
    if (s.state == SkiffState::Capsized) s.vel = Vector2Scale(s.vel, expf(-1.5f * dt));
    s.p = Vector2Add(s.p, Vector2Scale(Vector2Add(s.vel, Vector2Scale(sea.current, 0.5f)), dt));
    s.yawRate *= expf(-2.2f * dt);
    s.heading += s.yawRate * dt;
    s.noise = std::max(0.0f, s.noise - dt);
    // she can't pass through the Gannet: pushed clear of her hull
    Vector2 inG = boat.ToDeck(s.p);
    if (inG.x > -12.2f && inG.x < 11.5f && fabsf(inG.y) < 3.0f + SKIFF_HALF_B) {
        float out = (inG.y < 0 ? -1 : 1) * (3.0f + SKIFF_HALF_B);
        if (inG.x < -11.0f) inG.x = -12.2f; else inG.y = out;
        s.p = boat.ToWorld(inG);
    }
    for (const auto& L : landings) {
        Vector2 d = Vector2Subtract(s.p, L.at); float l = Vector2Length(d);
        if (l < L.r + 1.0f && l > 0.01f) { Vector2 n = Vector2Scale(d, 1 / l); s.p = Vector2Add(L.at, Vector2Scale(n, L.r + 1.0f)); float in = Vector2DotProduct(s.vel, n); if (in < 0) s.vel = Vector2Subtract(s.vel, Vector2Scale(n, in)); }
    }
    for (auto& r : s.load) if (!r.cooked && !r.junk) r.fresh = std::max(0.0f, r.fresh - dt / 60.0f * 0.01f);
    if (s.state != SkiffState::Afloat) return;
    // the roll: the sea's slope across her beam (a small boat follows it closely) and whatever shoves her
    Vector2 stb = s.ToWorld({0, SKIFF_HALF_B * 2}), prt = s.ToWorld({0, -SKIFF_HALF_B * 2});
    float slope = atanf((sea.Height(stb.x, stb.y) - sea.Height(prt.x, prt.y)) / (SKIFF_HALF_B * 4));
    float target = slope * 1.25f;
    s.rollV += (30.0f * (target - s.roll) - 5.0f * s.rollV) * dt;
    s.roll += s.rollV * dt;
    if (fabsf(s.roll) * RAD2DEG > D().skiffCapsize) SkiffCapsize(fabsf(slope) * RAD2DEG > 10 ? "a wave" : "she rolled over");
    // her lantern and the hands in her: the light and noise go into the water (EcoTick reads the skiff for the lamp)
}


// ---------------------------------------------------------------- depth.exe --trawl-skiff-test
int RunTrawlSkiffTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("The Trawl: the skiff\n");
    const float dt = 1 / 60.0f;
    auto setup = [&](Gannet& g, Eco& e, int crewN, uint32_t seed) {
        g.Init(crewN, seed, Weather::Calm);
        e.Init("lagoon", seed); e.agentBudget = 0; e.StartNight();
        g.eco = &e;
        g.boat.pos = {e.n * e.cell * 0.42f, e.n * e.cell * 0.5f}; g.boat.heading = 0; g.boat.telegraph = 0;
    };
    auto run = [&](Gannet& g, float secs) { for (int i = 0; i < (int)(secs * 60); i++) g.Step(dt); };
    int davit = Davit();
    // lower her: 8 s held at the davit (a pause keeps the progress)
    Gannet g; Eco e; setup(g, e, 2, 11);
    g.crew[0].p = Stations()[davit].at; check(g.TakeStation(0), "a hand takes the skiff davit");
    for (int i = 0; i < 60 * 4; i++) { g.Primary(0, true, dt); g.Step(dt); }
    for (int i = 0; i < 60; i++) { g.Primary(0, false, dt); g.Step(dt); }
    bool half = g.skiff.state == SkiffState::Lowering && g.skiff.t > 3.5f;
    for (int i = 0; i < 60 * 4 + 10; i++) { g.Primary(0, true, dt); g.Step(dt); }
    check(half && g.skiff.state == SkiffState::Afloat && g.SkiffAlongside(1.0f), TextFormat("8 s at the davit (a pause keeps the progress) lowers her alongside the stern (%.1f m off the berth)", Vector2Distance(g.skiff.p, g.SkiffBerth())));
    // down into her
    g.LeaveStation(0);
    check(g.BoardSkiff(0) && g.crew[0].deck == DECK_SKIFF, "Space at the davit: down into the skiff");
    // row: the left and right oars in turn on the beat
    float t = 0, dist = 0; Vector2 p0 = g.skiff.p; bool port = true; float top = 0;
    for (int i = 0; i < 60 * 30; i++) {
        t += dt;
        if (t >= D().skiffStroke) { t = 0; g.Oar(0, port, !port); port = !port; }
        g.Step(dt);
        if (i == 60 * 20) p0 = g.skiff.p;
    }
    dist = Vector2Distance(g.skiff.p, p0);
    float mean = dist / 10;   // (the last 10 s: the beat's average, not the trough between strokes)
    (void)top;
    check(mean > 1.3f && mean < 1.7f, TextFormat("one rower on the beat makes about 1.5 m/s (%.2f m/s over the last 10 s)", mean));
    // a rushed stroke catches a crab
    g.crew[0].oarT = 0.1f; float v0 = Vector2Length(g.skiff.vel);
    g.Oar(0, true, false);
    run(g, 0.5f);
    check(g.skiff.crabT > 0 && Vector2Length(g.skiff.vel) < v0 * 0.5f, TextFormat("a stroke 0.1 s after the last catches a crab: she stops (%.2f -> %.2f m/s)", v0, Vector2Length(g.skiff.vel)));
    // two rowers: 2.2 m/s (a bot pulls on the human's beat)
    {
        Gannet h; Eco he; setup(h, he, 2, 12);
        h.skiff.state = SkiffState::Afloat; h.skiff.p = h.SkiffBerth(); h.skiff.heading = PI; h.skiff.integrity = D().skiffIntegrity;
        h.crew[1].bot = true;
        h.crew[0].p = h.crew[1].p = Stations()[davit].at;
        check(h.BoardSkiff(0) && h.BoardSkiff(1) && h.crew[0].p.x != h.crew[1].p.x, "two hands sit in her (the rower's thwart and the stern sheets)");
        h.crew[2 % 2].bot = false;
        float tt = 0; bool pp = true;
        for (int i = 0; i < 60 * 30; i++) { tt += dt; if (tt >= D().skiffStroke) { tt = 0; h.Oar(0, pp, !pp); pp = !pp; } h.Step(dt); }
        float v2 = Vector2Length(h.skiff.vel);
        check(v2 > 1.9f && v2 < 2.6f, TextFormat("two rowers (a bot pulling on the beat) make about 2.2 m/s (%.2f)", v2));
        Gannet s3; Eco e3; setup(s3, e3, 3, 13);
        s3.skiff.state = SkiffState::Afloat; s3.skiff.p = s3.SkiffBerth();
        for (int k = 0; k < 3; k++) s3.crew[k].p = Stations()[davit].at;
        check(s3.BoardSkiff(0) && s3.BoardSkiff(1) && !s3.BoardSkiff(2), "she takes two, not three");
    }
    // back alongside: E up the stern ladder; the recovery needs her stopped, 10 s at the davit; the catch comes aboard
    {
        Gannet h; Eco he; setup(h, he, 2, 14);
        h.skiff.state = SkiffState::Afloat; h.skiff.p = h.SkiffBerth(); h.skiff.heading = PI;
        h.crew[1].p = Stations()[davit].at; h.BoardSkiff(1);
        CatchRec fish; fish.name = "snapper"; fish.kg = 4; fish.price = 3; fish.dead = true;
        check(h.SkiffLand(fish), "a fish into the skiff");
        CatchRec big = fish; big.kg = 148;
        check(!h.SkiffLand(big), "but never past 150 kg");
        check(h.LeaveSkiff(1) && h.crew[1].deck == 0, "E alongside: up the stern ladder onto the Gannet");
        h.crew[0].p = Stations()[davit].at; h.TakeStation(0);
        h.boat.telegraph = 3; for (int i = 0; i < 60 * 12; i++) { h.Primary(0, true, dt); h.Step(dt); }
        bool refused = h.skiff.state != SkiffState::Stowed;
        h.boat.telegraph = 0; h.boat.vel = {0, 0}; h.skiff.p = h.SkiffBerth(); h.skiff.vel = {0, 0};
        size_t h0 = h.hold.size();
        for (int i = 0; i < 60 * 11; i++) { h.boat.vel = {0, 0}; h.Primary(0, true, dt); h.Step(dt); if (i % 60 == 0 && getenv("DEPTH_TRACE")) printf("    state %d t %.1f d %.1f speed %.2f\n", (int)h.skiff.state, h.skiff.t, Vector2Distance(h.skiff.p, h.SkiffBerth()), h.boat.Speed()); }
        check(refused && h.skiff.state == SkiffState::Stowed && h.hold.size() == h0 + 1, "she can't be recovered under way; stopped, 10 s at the davit brings her up and her catch onto the aft deck");
    }
    // capsizing: a hard shove past 25 deg; everyone goes in; a swimmer rights her in 4 s and climbs back in
    {
        Gannet h; Eco he; setup(h, he, 1, 15);
        h.skiff.state = SkiffState::Afloat; h.skiff.p = Vector2Add(h.boat.pos, {-40, 0}); h.skiff.integrity = D().skiffIntegrity;
        h.crew[0].deck = DECK_SKIFF; h.crew[0].p = {0.2f, 0};
        CatchRec fish; fish.name = "snapper"; fish.kg = 4; fish.price = 3; fish.dead = true; h.SkiffLand(fish);
        h.SkiffRock(5.0f); run(h, 0.5f);
        check(h.skiff.state == SkiffState::Capsized && h.crew[0].overboard && h.floaters.size() == 1, "a hard shove rolls her past 25 deg: she capsizes, the hand goes in, the fish floats off");
        for (int i = 0; i < 60 * 4 + 10; i++) { h.SkiffSwim(0, true, dt); h.Step(dt); }
        check(h.skiff.state == SkiffState::Afloat, "the swimmer rights her in 4 s");
        check(h.BoardSkiff(0) && !h.crew[0].overboard && h.crew[0].deck == DECK_SKIFF, "and climbs back in (E)");
        h.SkiffHit(25, "a reef shark"); h.SkiffHit(20, "a reef shark");
        check(h.skiff.state == SkiffState::Lost && h.crew[0].overboard, "40 of hull: a second ram stoves her in and she goes down");
    }
    // calm water never capsizes her; a storm can
    {
        Gannet h; Eco he; setup(h, he, 1, 16);
        h.skiff.state = SkiffState::Afloat; h.skiff.p = Vector2Add(h.boat.pos, {-40, 0}); h.crew[0].deck = DECK_SKIFF; h.crew[0].p = {0.2f, 0};
        float worst = 0; for (int i = 0; i < 60 * 60; i++) { h.Step(dt); worst = std::max(worst, fabsf(h.skiff.roll) * RAD2DEG); }
        check(h.skiff.state == SkiffState::Afloat, TextFormat("a calm minute: she rolls at most %.1f deg", worst));
    }
    // the Atoll: beach her, carry a fish to the fire, cook it to a turn (and burn one), the rain, the caches, a crab,
    // the moray, the elder, and off again
    {
        Gannet h; Eco he; setup(h, he, 1, 21);
        Session ss; (void)ss;
        h.BuildLandings();
        check(h.landings.size() == 1 && he.DepthAt(h.landings[0].at) <= 0 && he.DepthAt(Vector2Add(h.landings[0].at, {16, 0})) > 0.5f, "the Lagoon has the Atoll: sand at its centre, a shelf the skiff can reach");
        Landing& L = h.landings[0];
        h.skiff.state = SkiffState::Afloat; h.skiff.integrity = D().skiffIntegrity; h.skiff.p = Vector2Add(L.at, {L.r + 2.5f, 0}); h.skiff.heading = PI;
        h.crew[0].deck = DECK_SKIFF; h.crew[0].p = {0.2f, 0};
        CatchRec f; f.name = "snapper"; f.sp = Species().Find("snapper"); f.kg = 3; f.price = 3; f.dead = true;
        CatchRec f2 = f; f2.name = "grunt"; f2.kg = 1.5f;
        h.SkiffLand(f); h.SkiffLand(f2);
        check(h.LeaveSkiff(0) && h.skiff.state == SkiffState::Beached && h.crew[0].deck == DECK_SHORE, "E close to the Atoll runs the skiff up on the sand and steps ashore");
        Crew& c = h.crew[0];
        check(h.ShoreUse(0) && c.carrying && c.carry.name == "snapper", "E at the beached skiff lifts the heaviest fish out");
        // walk it to the fire on foot (the ShoreMove wish is in the Gannet's frame: aim straight at the fire)
        for (int i = 0; i < 60 * 12 && Vector2Distance(c.p, L.fire) > 1.2f; i++) {
            Vector2 to = Vector2Normalize(Vector2Subtract(L.fire, c.p)); Vector2 bf = h.boat.Forward();
            h.Move(0, {to.x * bf.x + to.y * bf.y, -to.x * bf.y + to.y * bf.x}, false, dt); h.Step(dt);
        }
        check(Vector2Distance(c.p, L.fire) < 1.7f, TextFormat("on foot across the sand to the fire pit (%.1f m off it)", Vector2Distance(c.p, L.fire)));
        check(h.ShoreUse(0) && L.onFire.size() == 1 && !c.carrying, "E at the fire: the fish goes on");
        run(h, 10 + 3 + 2);
        bool off = h.ShoreUse(0);
        check(off && c.carrying && c.carry.cooked && fabsf(c.carry.cook - 1.5f) < 0.01f, TextFormat("taken off after 15 s (10 + 1 a kg, then the hold): done to a turn, x%.2f", c.carry.cook));
        Session vs; vs.G = &h;
        CatchRec raw = c.carry; raw.cook = 1; raw.cooked = false;
        check(fabsf(vs.Value(c.carry) - 1.5f * vs.Value(raw)) < 0.01f, "a fish cooked to a turn sells at 1.5x");
        float f0 = c.carry.fresh; CatchRec keep = c.carry; keep.gutted = keep.iced = true; c.carrying = false; c.carryKg = 0;   // (stowed in the hold)
        h.hold.push_back(keep); run(h, 30);
        check(!h.hold.empty() && h.hold.back().fresh == f0, "cooked fish keep (no freshness lost in the hold)");
        if (!h.hold.empty()) h.hold.pop_back();
        // a burnt one
        CatchRec b2 = f2; b2.cookT = 0; L.onFire.push_back(b2);
        for (int i = 0; i < 60 * 25; i++) { for (auto& a : he.agents) if (Species().sp[a.sp].stealsDeck) a.alive = false; h.Step(dt); }   // (no birds for this one: the smoke would bring them)
        check(!L.onFire.empty() && L.onFire.back().cook > 0.29f && L.onFire.back().cook < 0.31f, TextFormat("left on, it burns down to 0.3x (x%.2f after %.0f s, %d on the fire)", L.onFire.empty() ? -1.0f : L.onFire.back().cook, L.onFire.empty() ? 0.0f : L.onFire.back().cookT, (int)L.onFire.size()));
        L.onFire.clear();
        // the rain puts the fire out; relighting takes 10 s standing by it (and only out of the rain)
        h.sea.weather = Weather::Rain; run(h, 0.1f);
        check(!L.fireLit, "rain puts the open fire out");
        h.sea.weather = Weather::Calm; h.ShoreUse(0);
        bool working = c.workOn == 100; run(h, 10.2f);
        check(working && L.fireLit, "E at the cold pit: 10 s with dry kindling relights it");
        // the strongbox: locked until a brass key; a chest is heavy in the arms
        Cache& box = L.caches[0];
        c.p = Vector2Add(box.p, {0.6f, -0.9f}); if (!h.landings.empty()) {}
        h.junkKeys = 0; h.ShoreUse(0);
        bool locked = !box.open;
        h.junkKeys = 1; bool opened = h.ShoreUse(0); (void)opened;
        check(locked && box.open && c.carrying && c.carry.junk && h.junkKeys == 0 && c.carryKg > 10, TextFormat("the sloop's strongbox: locked, then a brass key opens it (%s, %.0f kg, worth %.0f)", c.carry.name.c_str(), c.carry.kg, c.carry.price));
        c.carrying = false; c.carryKg = 0;
        if (L.caches.size() > 1 && L.caches[1].kind == 2) {
            Cache& bur = L.caches[1];
            h.junkBottles = 1; h.ShoreUse(0);
            c.p = Vector2Add(bur.p, {0.5f, 0}); h.ShoreUse(0); run(h, 5.2f);
            check(bur.found && bur.open && c.carrying, "a bottle's map marks the buried cache; 5 s of digging at the X brings it up");
            c.carrying = false; c.carryKg = 0;
        } else if (L.caches.size() > 1) {
            c.p = Vector2Add(L.caches[1].p, {0.5f, 0}); h.ShoreUse(0);
            check(L.caches[1].open && c.carrying, "a sea chest lies under the palms");
            c.carrying = false; c.carryKg = 0;
        }
        // a crab off the beach
        if (!L.crabs.empty()) { c.p = Vector2Scale(Vector2Normalize(L.crabs[0]), L.r - 1.2f); L.crabs[0] = c.p; h.ShoreUse(0); }
        check(c.carrying && c.carry.name == "blue crab", "E by a crab on the beach: caught by the shell");
        c.carrying = false; c.carryKg = 0;
        // the moray in the little lagoon
        int inj0 = c.injuries; c.injuries = 0; L.morayT = 0;
        for (int i = 0; i < 60 * 4 && !c.Has(INJ_BITE); i++) { c.p = L.moray; h.Step(dt); }
        check(c.Has(INJ_BITE), "wading in the Atoll's lagoon beside the moray: bitten");
        c.injuries = inj0;
        // the elder: fish at 150%, his goods; closed to a crew that fought the canoes
        c.p = Vector2Add(L.elder, {1.0f, 0.4f});
        c.carrying = true; c.carry = f; c.carryKg = f.kg;
        float v = vs.ElderGive(0);
        check(fabsf(v - 1.5f * vs.Value(f)) < 0.01f && !c.carrying, TextFormat("the elder takes a fish at 150%% of its value (%.1f in trade)", v));
        L.elderCredit = 300; c.slots[3] = Slot{};
        std::string why;
        bool bought = vs.ElderBuy(0, "coralclub", &why); bool club = false; for (const auto& sl : c.slots) if (sl.it == Item::Weapon && sl.wpn >= 0 && Weapons()[sl.wpn].id == "coralclub") club = true;
        check(bought && club && L.elderCredit < 300, TextFormat("his coral club (sold nowhere else) for fish credit%s", bought ? "" : (" - " + why).c_str()));
        h.foughtCanoes = true;
        c.carrying = true; c.carry = f;
        check(vs.ElderGive(0, &why) == 0 && why.find("canoes") != std::string::npos, "a crew that fought the canoes finds him closed");
        c.carrying = false;
        // back into the skiff and off the beach
        c.p = Vector2Subtract(h.skiff.p, L.at); c.p = Vector2Scale(Vector2Normalize(c.p), L.r - 1.0f);
        check(h.BoardSkiff(0) && c.deck == DECK_SKIFF, "Space beside the beached skiff: into her");
        h.Oar(0, true, true); run(h, 1);
        check(h.skiff.state == SkiffState::Afloat && Vector2Distance(h.skiff.p, L.at) > L.r + 1.0f, "the first stroke shoves her off the sand");
        // birds come to the beach: a fish set down there is taken
        h.crew[0].deck = DECK_SHORE; h.crew[0].p = {0, 0}; h.skiff.state = SkiffState::Beached;
        CatchRec bf = f2; bf.deckAt = {1, 1}; L.onBeach.push_back(bf); h.skiff.load.clear();
        int gs = Species().Find("gull flock"); int ai = he.SpawnAgentPublic(gs, L.at); he.agents[ai].count = 8;
        for (int i = 0; i < 60 * 9 && !L.onBeach.empty(); i++) { he.agents[ai].p = {L.at.x, L.at.y, -3}; he.agents[ai].alive = true; h.Step(dt); }
        check(L.onBeach.empty() && !h.thieves.empty(), "a fish left on the beach goes to the gulls");
    }
    // 5d: the skiff marks, her line, the tow, the ram, a bot after you, the weed round the Gannet's screw
    {
        Gannet h; Eco he; setup(h, he, 2, 31);
        check(he.marks.size() == 2 && he.marks[0].name == "The Crest Pass" && he.DepthAt(he.marks[0].at) < 1.8f && he.marks[1].name == "The Sargassum Line",
              TextFormat("the Lagoon's skiff water: the Crest Pass (%.1f m: her keel wants 1.8) and the Sargassum Line", he.DepthAt(he.marks[0].at)));
        // the line: a small fish into her bottom boards, a thrasher rocks her, a big one goes on the tow
        h.skiff.state = SkiffState::Afloat; h.skiff.integrity = D().skiffIntegrity; h.skiff.p = Vector2Add(h.boat.pos, {-60, 0});
        h.crew[0].deck = DECK_SKIFF; h.crew[0].p = {0.2f, 0}; h.crew[0].skiffLine = true;
        auto land = [&](float kg) {
            Rod& r = h.skiffRod;
            FishSpec f = DummyFish()[0]; f.kg = kg;
            r.fight = Fight{}; r.fight.tackle = r.tackle; r.fight.HookFish(f, {h.skiff.p.x + 3, h.skiff.p.y, 1}, 9);
            r.state = RodState::Fighting; r.fishSp = -1; r.fight.end = FightEnd::Landed;
            h.StepSkiffRod(dt);
        };
        size_t l0 = h.skiff.load.size();
        land(3);
        check(h.skiff.load.size() == l0 + 1 && h.skiff.load.back().dead, "a 3 kg fish on the skiff's line comes over the gunwale, killed at the waterline");
        float rv0 = fabsf(h.skiff.rollV);
        land(16);
        check(fabsf(h.skiff.rollV) > rv0 + 1.0f, TextFormat("a 16 kg thrasher landed in her rocks her hard (roll rate %.1f rad/s)", h.skiff.rollV));
        h.skiff.rollV = 0; h.skiff.roll = 0;
        land(48);
        check(h.towed.size() == 1 && h.towed[0].kg == 48, "a 48 kg fish is too big for her: killed alongside, onto the tow line");
        // the tow drags at her
        auto rowFor = [&](Gannet& gg, float secs) { float tt = 0; bool pp = true; Vector2 p0 = gg.skiff.p; for (int i = 0; i < (int)(secs * 60); i++) { tt += dt; if (tt >= D().skiffStroke) { tt = 0; gg.Oar(0, pp, !pp); pp = !pp; } gg.Step(dt); } return Vector2Distance(gg.skiff.p, p0) / secs; };
        h.crew[0].skiffLine = false;
        float towing = rowFor(h, 15);
        auto keep = h.towed; h.towed.clear(); h.skiff.vel = {0, 0};
        float free = rowFor(h, 15);
        check(towing < free * 0.8f, TextFormat("rowing with 48 kg on the tow line: %.2f m/s against %.2f free", towing, free));
        h.towed = keep;
        // recovered: the tow line comes aboard
        h.skiff.p = h.SkiffBerth(); h.skiff.vel = {0, 0}; h.boat.vel = {0, 0}; h.boat.telegraph = 0; h.skiff.load.clear();
        h.crew[0].deck = 0; h.crew[0].p = Stations()[Davit()].at; h.TakeStation(0);
        size_t hh = h.hold.size();
        for (int i = 0; i < 60 * 11; i++) { h.boat.vel = {0, 0}; h.skiff.p = h.SkiffBerth(); h.Primary(0, true, dt); h.Step(dt); }
        check(h.towed.empty() && h.hold.size() == hh + 1, "hauled up, the tow line comes aboard with her");
    }
    {   // a reef shark in the blood round her rams the skiff
        Gannet h; Eco he; setup(h, he, 1, 32);
        h.skiff.state = SkiffState::Afloat; h.skiff.integrity = D().skiffIntegrity; h.skiff.p = Vector2Add(h.boat.pos, {-70, 0});
        h.crew[0].deck = DECK_SKIFF; h.crew[0].p = {0.2f, 0};
        int rs = Species().Find("reef shark");
        int ai = he.SpawnAgentPublic(rs, h.skiff.p);
        he.AddBlood({h.skiff.p.x, h.skiff.p.y, 1}, 400);
        bool hit = false; float worst = 0;
        for (int i = 0; i < 60 * 4 && !hit; i++) {
            if (ai < (int)he.agents.size()) { he.agents[ai].p = {h.skiff.p.x + 1.5f, h.skiff.p.y, 1}; he.agents[ai].hunger = 0.9f; he.agents[ai].fedT = 0; he.agents[ai].alive = true; }
            h.Step(dt); hit = h.skiff.integrity < D().skiffIntegrity; worst = std::max(worst, fabsf(h.skiff.rollV));
        }
        check(hit && worst > 1.0f, TextFormat("a hungry reef shark in the blood round her rams the skiff: %.0f of %.0f left, a hard heel", h.skiff.integrity, D().skiffIntegrity));
    }
    {   // a bot told to follow you goes down into the skiff after you
        Gannet h; Eco he; setup(h, he, 2, 33);
        h.botsOn = true; h.crew[1].bot = true; h.brains.assign(2, Gannet::Brain{});
        h.skiff.state = SkiffState::Afloat; h.skiff.p = h.SkiffBerth(); h.skiff.heading = PI; h.skiff.integrity = D().skiffIntegrity;
        h.crew[0].p = Stations()[Davit()].at; h.BoardSkiff(0);
        h.crew[1].p = {-2, 1};
        h.brains[1].follow = 0; h.brains[1].task = 3;
        for (int i = 0; i < 60 * 12 && h.crew[1].deck != DECK_SKIFF; i++) { h.skiff.p = h.SkiffBerth(); h.skiff.vel = {0, 0}; h.Step(dt); }
        check(h.crew[1].deck == DECK_SKIFF, "a bot following you walks to the davit and drops into the skiff after you");
        h.LeaveSkiff(0);
        for (int i = 0; i < 60 * 2 && h.crew[1].deck == DECK_SKIFF; i++) { h.skiff.p = h.SkiffBerth(); h.Step(dt); }
        check(h.crew[1].deck == 0, "and climbs back aboard when you do");
    }
    {   // the Sargassum Line fouls a turning screw
        Gannet h; Eco he; setup(h, he, 1, 34);
        h.boat.pos = he.marks[1].at; h.boat.telegraph = 2; h.boat.pressure = 0.7f; h.boat.firebox = 5;
        Gannet o; Eco oe; setup(o, oe, 1, 34); o.boat.pos = Vector2Add(oe.marks[1].at, {0, -80}); o.boat.telegraph = 2; o.boat.pressure = 0.7f; o.boat.firebox = 5;
        for (int i = 0; i < 60 * 20; i++) { h.Step(dt); o.Step(dt); if (Vector2Distance(h.boat.pos, he.marks[1].at) > he.marks[1].r - 2) h.boat.pos = he.marks[1].at; }
        check(h.boat.Speed() < o.boat.Speed() * 0.6f, TextFormat("in the Sargassum Line weed fouls her screw: %.2f m/s against %.2f in open water", h.boat.Speed(), o.boat.Speed()));
    }
    printf(fails ? "trawl-skiff-test: %d FAILED\n" : "trawl-skiff-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}
} // namespace tw
