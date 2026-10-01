// The skiff (design doc v2, "The skiff"): the Gannet's 4.5 m rowing boat on the stern davit, the Trawl's facility run.
// A hand at the davit lowers it in 8 s and recovers it in 10 (alongside the stern, the Gannet stopped). Space at the
// stern beside the davit drops into it; E in it climbs back up (or steps ashore when it's beached). The oars are the
// two mouse buttons, a rhythm: 1.5 m/s with one rower, 2.2 with two; a stroke too soon after the last catches a crab
// and stops her a second; each stroke splashes a little noise into the water. One section of 40; past 25 degrees of
// roll she capsizes and everyone aboard goes in (a swimmer beside her rights her in 4 s, then climbs in).
#include "trawl.h"
#include "trawl_eco.h"
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
        if (s.t >= D().skiffRecover) {
            // hoisted: the catch onto the aft deck (dead, for the crates), and anyone still in her steps off at the davit
            for (auto& r : s.load) { CatchRec h = r; h.deckAt = {-9.6f + (float)(hold.size() % 3) * 0.3f, -1.0f + (float)(hold.size() % 5) * 0.4f}; hold.push_back(h); }
            if (!s.load.empty()) Say(TextFormat("The skiff comes up: %d things out of her onto the aft deck", (int)s.load.size()));
            else Say("The skiff comes up on the davit");
            s.load.clear();
            for (auto& c : crew) if (c.deck == DECK_SKIFF) { c.deck = 0; c.p = {-10.2f, c.p.y < -0.5f ? -0.8f : 0.8f}; c.v = {0, 0}; }
            s.state = SkiffState::Stowed; s.t = -1;
        }
    }
}

bool Gannet::BoardSkiff(int ci) {
    Crew& c = crew[ci];
    Skiff& s = skiff;
    if (c.dead || !s.Up() || s.state == SkiffState::Beached) return false;
    int aboard = 0; for (const auto& o : crew) if (o.deck == DECK_SKIFF && !o.overboard && !o.dead) aboard++;
    if (c.overboard) {
        if (Vector2Distance(c.swim, s.p) > SKIFF_HALF_L + 0.8f) return false;
        if (aboard >= 2) { Say("The skiff takes two"); return false; }
        c.overboard = false; c.drownT = 0; Say("Hauled over the gunwale into the skiff");
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
    if (SkiffAlongside(4)) { c.deck = 0; c.p = {-10.2f, 0}; c.v = {0, 0}; Say("Up the stern ladder onto the Gannet"); return true; }
    return false;
}

void Gannet::Oar(int ci, bool port, bool star) {
    Crew& c = crew[ci];
    Skiff& s = skiff;
    if ((!port && !star) || c.deck != DECK_SKIFF || c.overboard || c.dead || s.state != SkiffState::Afloat) return;
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
    vf -= vf * ROW_DRAG * dt;
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
    printf(fails ? "trawl-skiff-test: %d FAILED\n" : "trawl-skiff-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}
} // namespace tw
