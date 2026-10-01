// The Weeds' threats (design doc v2, page 38: "Sirens (sing from the lanes, pull the helm toward the rocks), Kelp Wraiths
// (drift unseen in the canopy, entangle a hand at the rail), a Great White on the seaward edge, Feral Mermen who cut
// nets"). The Great White is a species in the web (trawl_species.json); the other three are timed encounters run here,
// paced by the Stir clock like everything else that comes out of the dark.
//
// Design calls (the doc gives only the one line each):
//  - A Siren sits on rocks 55-85 m off and sings for 40 s. The wheel pulls toward her: a hand at the helm feels it and
//    must steer against it (a third of a full turn of the wheel a second); an empty helm lets her have the Gannet. She
//    leaves when a flare burns within 40 m of her, a shot lands within 5 m, the searchlight finds her inside 45 m, or her
//    song ends. Reaching her (12 m) puts the bow on her rocks: 25 to a bow section.
//  - A Kelp Wraith takes a hand standing at the rail while the Gannet lies in or beside the canopy: the hand can't move,
//    and after 8 s goes over the side. E beside them cuts them free, or E with their own knife; a bot cuts itself free in
//    4 s (its own knife). The kelp crown keeps the wraiths off whoever wears it.
//  - Feral Mermen come to a net towed through or beside the kelp: splashing at the cod end for 12 s (the scene shows it,
//    the sound gives it away). A shot or a flare near the net, or the searchlight on it, sends them off; otherwise they
//    slit the cod end and everything in it is gone.
#include "trawl.h"
#include "trawl_eco.h"
#include "trawl_session.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tw {

namespace {
uint32_t gWr = 91;
float WRand() { gWr = gWr * 1664525u + 1013904223u; return (gWr >> 8) * (1.0f / 16777216.0f); }
const float SIREN_SONG = 40, SIREN_PULL = 0.33f, SIREN_ROCKS = 12, WRAITH_DRAG = 8, BOT_CUT = 4, MERMEN_CUT = 12;
float HalfBeamW(float x) { return x > 5 ? std::max(0.5f, 3.0f - (x - 5) * 0.42f) : x < -10 ? 3.0f - (-10 - x) * 0.8f : 3.0f; }
bool NearKelp(const Eco& e, Vector2 p, float r) {
    if (e.HabAt(p) == H_KELP) return true;
    for (int k = 0; k < 8; k++) { float a = k * PI / 4; if (e.HabAt({p.x + cosf(a) * r, p.y + sinf(a) * r}) == H_KELP) return true; }
    return false;
}
Vector2 CodEnd(const Gannet& g) { const Vector3& q = g.net.node[5 * 8 + 4]; return {q.x, q.y}; }
// anything that drives a thing off at p: a flare burning within fr m, a shot within 5 m, the searchlight on it inside lr m
bool Scared(const Gannet& g, Vector2 p, float fr, float lr) {
    for (const auto& f : g.flares) if (Vector2Distance(f.p, p) < fr) return true;
    for (const auto& s : g.shots) if (Vector2Distance({s.p.x, s.p.y}, p) < 5) return true;
    if (g.boat.lantern == 3) {
        Vector2 d = Vector2Subtract(p, g.boat.pos); float dist = Vector2Length(d);
        if (dist < lr) {
            float bearing = atan2f(d.y, d.x) - (g.boat.heading + g.boat.searchAim);
            while (bearing > PI) bearing -= 2 * PI;
            while (bearing < -PI) bearing += 2 * PI;
            if (fabsf(bearing) < 0.3f) return true;
        }
    }
    return false;
}
}

bool Gannet::Weeds() const { return eco && eco->ground == "weeds"; }

bool Gannet::FreeTangled(int ci) {
    Crew& c = crew[ci];
    if (c.dead || c.overboard || (c.deck != 0 && c.deck != DECK_SHORE)) return false;
    if (c.tangleT > 0) {   // their own knife
        bool knife = false; for (const auto& s : c.slots) if (s.it == Item::Knife) knife = true;
        if (!knife) return false;
        c.tangleT = 0; Say("Cut free with their own knife: the thing in the kelp lets go"); return true;
    }
    for (auto& o : crew) if (&o != &c && o.tangleT > 0 && !o.dead && !o.overboard && o.deck == c.deck && Vector2Distance(o.p, c.p) < 1.4f) {
        o.tangleT = 0; Say("Hauled free of the kelp: whatever had them slides back under"); return true;
    }
    return false;
}

void Gannet::CutNet(const std::string& why) {
    float kg = 0; for (const auto& k : net.catchKg) kg += k.second;
    net.catchKg.clear(); net.load = 0; net.kelpKg = 0;
    Say(TextFormat("%s: the cod end is slit and %.0f kg of fish spill out", why.c_str(), kg));
    if (eco) { Vector2 p = CodEnd(*this); eco->AddBlood({p.x, p.y, 3}, std::min(40.0f, kg * 0.2f)); }
}

void Gannet::StepWeeds(float dt) {
    if (!Weeds() || moored) { siren.on = false; mermen.on = false; for (auto& c : crew) c.tangleT = 0; return; }
    Eco& e = *eco;
    float stir = e.Stir();
    sirenCool -= dt; wraithCool -= dt; mermenCool -= dt;

    // ---- the Siren
    if (!siren.on && sirenCool <= 0 && WRand() < dt * (0.15f + stir) / 50) {
        // rocks or the barrens 55-85 m off: the lanes' edges, where the kelp has been grazed down to stone
        Vector2 best = {}; bool found = false;
        for (int k = 0; k < 24 && !found; k++) {
            float a = WRand() * 2 * PI, d = 55 + WRand() * 30;
            Vector2 p{boat.pos.x + cosf(a) * d, boat.pos.y + sinf(a) * d};
            int h = e.HabAt(p);
            if (h == H_REEF || h == H_BARREN || (k > 16 && h != H_LAND)) { best = p; found = true; }
        }
        if (found) { siren.on = true; siren.p = best; siren.t = 0; Say("A voice out over the lanes, singing: the wheel turns under the helmsman's hands"); }
        else sirenCool = 20;
    }
    if (siren.on) {
        siren.t += dt;
        // the pull on the wheel toward her
        Vector2 d = Vector2Subtract(siren.p, boat.pos);
        float err = atan2f(d.y, d.x) - boat.heading;
        while (err > PI) err -= 2 * PI;
        while (err < -PI) err += 2 * PI;
        bool manned = false;
        for (const auto& c : crew) if (!c.dead && c.station >= 0 && Stations()[c.station].kind == StationKind::Helm) manned = true;
        float pull = (manned ? SIREN_PULL : 1.0f) * (err > 0 ? 1 : -1) * std::min(1.0f, fabsf(err) * 3);
        boat.rudder = std::clamp(boat.rudder + pull * dt, -1.0f, 1.0f);
        if (!manned) boat.rudder = std::clamp(boat.rudder + pull * dt * 2, -1.0f, 1.0f);
        if (Vector2Length(d) < SIREN_ROCKS) {
            boat.Hit(err > 0 ? SEC_BOW_S : SEC_BOW_P, 25);
            Say("The song stops: the bow grinds onto her rocks");
            siren.on = false; sirenCool = 200;
        } else if (Scared(*this, siren.p, 40, 45)) {
            Say("A shriek, and the singing stops: the Siren is gone into the kelp");
            siren.on = false; sirenCool = 200;
        } else if (siren.t >= SIREN_SONG) {
            Say("The song fades over the lanes");
            siren.on = false; sirenCool = 160;
        }
    }

    // ---- the Kelp Wraiths: a hand at the rail while she lies in or beside the canopy
    // (and on the Cannery Pier: they live in the pilings, and take a hand standing at the stage's edge)
    bool inKelp = NearKelp(e, boat.pos, 12);
    const Landing* pierL = skiff.landing >= 0 && skiff.landing < (int)landings.size() && landings[skiff.landing].kind == LK_CANNERY ? &landings[skiff.landing] : nullptr;
    for (int k = 0; k < (int)crew.size(); k++) {
        Crew& c = crew[k];
        bool onPier = pierL && c.deck == DECK_SHORE;
        if (c.tangleT > 0) {
            if (c.dead || c.overboard || (c.deck != 0 && !onPier)) { c.tangleT = 0; continue; }
            c.tangleT += dt; c.v = {0, 0};
            if (c.bot && c.tangleT >= BOT_CUT) { c.tangleT = 0; Say("A hand saws through the kelp round their ankle and staggers clear"); continue; }
            if (c.tangleT >= WRAITH_DRAG) {
                c.tangleT = 0;
                if (onPier) {
                    float l = std::max(0.1f, Vector2Length(c.p));
                    Vector2 out = pierL->ToWorld(Vector2Scale(c.p, (pierL->r + 1.5f) / l));
                    c.deck = 0; c.carrying = false; c.carryKg = 0;
                    GoOverboard(k, "pulled off the pier by a Kelp Wraith"); c.swim = out;
                } else GoOverboard(k, "dragged over the rail by a Kelp Wraith");
            }
            continue;
        }
        if (wraithCool > 0 || c.dead || c.overboard || c.station >= 0) continue;
        if (c.charm == CH_KELP_CROWN) continue;                   // the crown: the wraiths won't touch whoever wears it
        if (onPier) { if (Vector2Length(c.p) < pierL->r - 1.3f) continue; }   // only at the stage's edge
        else {
            if (!inKelp || c.deck != 0) continue;
            if (fabsf(c.p.y) < HalfBeamW(c.p.x) - 0.8f) continue;     // only at the rail
        }
        if (WRand() < dt * (0.2f + stir) / 35) {
            c.tangleT = dt; wraithCool = 90;
            Say(onPier ? "Something reaches up out of the pilings and has a hand by the ankle: cut them free (E beside them)"
                       : "Something cold in the kelp has a hand by the ankle: cut them free (E beside them) before it drags them over");
        }
    }

    // ---- the Feral Mermen at a net towed near the kelp
    bool netDown = net.state == NetState::Down;
    if (!mermen.on && netDown && mermenCool <= 0 && NearKelp(e, CodEnd(*this), 18) && WRand() < dt * (0.2f + stir) / 40) {
        mermen.on = true; mermen.t = 0; mermen.p = CodEnd(*this);
        Say("Splashing at the cod end: something is at the net");
    }
    if (mermen.on) {
        mermen.t += dt; mermen.p = CodEnd(*this);
        if (!netDown) { mermen.on = false; mermenCool = 90; }
        else if (Scared(*this, mermen.p, 25, 40)) { Say("Pale shapes break away from the net and dive"); mermen.on = false; mermenCool = 150; }
        else if (mermen.t >= MERMEN_CUT) { CutNet("Feral Mermen"); mermen.on = false; mermenCool = 180; }
    }
}

// ---------------------------------------------------------------- depth.exe --trawl-weeds-test
int RunTrawlWeedsTest() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Trawl: the Weeds' threats\n");
    const float dt = 1 / 60.0f;
    Eco e; if (!e.Init("weeds", 4242)) { printf("  FAIL  the Weeds won't load\n"); return 1; }
    // a point in the kelp, and one in open water well clear of it
    Vector2 kelp{}, open{}; bool fk = false, fo = false;
    for (int y = 0; y < 400 && !(fk && fo); y += 4) for (int x = 0; x < 600 && !(fk && fo); x += 4) {
        Vector2 p{(float)x, (float)y};
        if (!fk && e.HabAt(p) == H_KELP && e.DepthAt(p) > 6) { kelp = p; fk = true; }
        if (!fo && e.HabAt(p) == H_OPEN && e.DepthAt(p) > 8 && !NearKelp(e, p, 30)) { open = p; fo = true; }
    }
    check(fk && fo, "the Weeds' chart has kelp and clear water");
    auto fresh = [&](Gannet& g, int n, Vector2 at) { g.Init(n, 77); g.eco = &e; g.boat.pos = at; g.boat.telegraph = 0; g.sirenCool = g.wraithCool = g.mermenCool = 1e9f; e.stirOverride = 0.6f; };

    // the Siren: an empty helm lets her have the wheel; a shot near her sends her off
    {
        Gannet g; fresh(g, 1, open);
        g.siren.on = true; g.siren.p = {open.x, open.y + 60}; g.boat.heading = 0;
        for (int i = 0; i < 60 * 3; i++) g.StepWeeds(dt);
        check(g.boat.rudder > 0.5f, TextFormat("a Siren off the starboard beam: the empty wheel swings toward her (rudder %.2f)", g.boat.rudder));
        g.boat.rudder = 0;
        g.crew[0].station = -1; for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == StationKind::Helm) { g.crew[0].p = Stations()[i].at; g.crew[0].station = i; }
        for (int i = 0; i < 60 * 1; i++) { g.Steer(0, -1, dt); g.StepWeeds(dt); }
        check(g.boat.rudder < 0, "a hand at the helm can steer against the pull");
        Projectile s; s.kind = Shot::Bullet; s.p = {g.siren.p.x + 2, g.siren.p.y, 0}; g.shots.push_back(s);
        g.StepWeeds(dt);
        check(!g.siren.on, "a shot at her drives her off");
        g.siren.on = true; g.siren.p = Vector2Add(g.boat.pos, {6, 0}); g.siren.t = 0; g.shots.clear();
        float hp = g.boat.integrity[SEC_BOW_P] + g.boat.integrity[SEC_BOW_S];
        g.StepWeeds(dt);
        check(!g.siren.on && g.boat.integrity[SEC_BOW_P] + g.boat.integrity[SEC_BOW_S] < hp - 20, "reaching her puts the bow on her rocks");
    }
    // the Kelp Wraith: a hand at the rail in the kelp is taken; free in 8 s goes over; E beside them cuts them free
    {
        Gannet g; fresh(g, 2, kelp); g.wraithCool = 0;
        Crew& a = g.crew[0]; a.p = {0, HalfBeamW(0) - 0.4f}; a.bot = false;
        g.crew[1].p = {-4, 0}; g.crew[1].bot = false;
        for (int i = 0; i < 60 * 600 && a.tangleT <= 0; i++) g.StepWeeds(dt);
        check(a.tangleT > 0, "a hand at the rail in the kelp is taken by the ankle");
        Vector2 at = a.p; for (int i = 0; i < 60; i++) { g.Move(0, {0, -1}, false, dt); g.StepWeeds(dt); }
        check(Vector2Distance(at, a.p) < 0.05f, "held: they can't walk away from the rail");
        for (int i = 0; i < 60 * 8; i++) g.StepWeeds(dt);
        check(a.overboard, "nobody cuts them free: over the side in 8 s");
        Gannet h; fresh(h, 2, kelp);
        Crew& b = h.crew[0]; b.p = {0, HalfBeamW(0) - 0.4f}; b.tangleT = 0.1f; b.bot = false;
        h.crew[1].p = {0, b.p.y - 1.0f}; h.crew[1].bot = false;
        bool freed = h.TakeStation(1) && b.tangleT == 0;
        check(freed, "E beside them hauls them free");
        b.tangleT = 0.1f; b.slots[3] = {Item::Knife, 0};
        check(h.TakeStation(0) && b.tangleT == 0, "their own knife cuts them free");
        // a bot crew answers it: the nearest free bot runs over and cuts the hand loose
        {
            Gannet bt; fresh(bt, 3, kelp); bt.botsOn = true;
            Crew& v = bt.crew[0]; v.p = {-6, HalfBeamW(-6) - 0.4f}; v.tangleT = 0.1f;
            for (int i = 0; i < 60 * 8 && v.tangleT > 0; i++) { bt.boat.vel = {0, 0}; bt.Step(dt); }
            check(v.tangleT == 0 && !v.overboard, "a bot crew runs to the rail and cuts the hand free before it's dragged over");
        }
        Gannet k; fresh(k, 1, kelp); k.wraithCool = 0;
        k.crew[0].p = {0, HalfBeamW(0) - 0.4f}; k.crew[0].charm = CH_KELP_CROWN;
        for (int i = 0; i < 60 * 600; i++) k.StepWeeds(dt);
        check(k.crew[0].tangleT == 0, "the kelp crown: the wraiths never touch whoever wears it");
        Gannet o; fresh(o, 1, open); o.wraithCool = 0;
        o.crew[0].p = {0, HalfBeamW(0) - 0.4f};
        for (int i = 0; i < 60 * 600; i++) o.StepWeeds(dt);
        check(o.crew[0].tangleT == 0, "in clear water, away from the canopy, the rail is safe");
    }
    // the Feral Mermen: a net at the kelp is slit in 12 s; a flare near it sends them off
    {
        Gannet g; fresh(g, 1, kelp);
        g.net.state = NetState::Down; g.net.catchKg = {{0, 80}}; g.net.load = 80;
        for (int i = 0; i < 48; i++) { g.net.node[i] = {kelp.x - 20, kelp.y, 3}; }
        g.mermenCool = 0;
        for (int i = 0; i < 60 * 600 && !g.mermen.on; i++) g.StepWeeds(dt);
        bool came = g.mermen.on;
        for (int i = 0; i < 60 * 13; i++) g.StepWeeds(dt);
        check(came && g.net.catchKg.empty() && g.net.load == 0, "Feral Mermen at a net in the kelp: left alone, they slit the cod end");
        g.net.catchKg = {{0, 80}}; g.net.load = 80; g.mermenCool = 0;
        for (int i = 0; i < 60 * 600 && !g.mermen.on; i++) g.StepWeeds(dt);
        g.flares.push_back({{g.mermen.p.x + 5, g.mermen.p.y}, 30});
        g.StepWeeds(dt);
        check(!g.mermen.on && !g.net.catchKg.empty(), "a flare over the net sends them off");
    }
    // the landings: Seal Rock (Old Hoskins buys birds at double; the hut's stove keeps alight in the rain; the bull seal)
    // and the Cannery Pier (the foreman buys cooked fish at 150%; the safe; Kelp Wraiths in the pilings)
    {
        Gannet g; fresh(g, 2, open); g.BuildLandings();
        bool kinds = g.landings.size() == 2 && g.landings[0].kind == LK_SEALROCK && g.landings[1].kind == LK_CANNERY;
        check(kinds && e.DepthAt(g.landings[0].at) <= 0 && e.DepthAt(g.landings[1].at) <= 0 && e.DepthAt(Vector2Add(g.landings[0].at, {14, 0})) > 0.5f,
              "the Weeds have Seal Rock and the Cannery Pier, each with water round it for the skiff");
        if (kinds) {
            Session vs; vs.G = &g;
            Crew& c = g.crew[0];
            Landing& S1 = g.landings[0];
            g.skiff.state = SkiffState::Beached; g.skiff.landing = 0; c.deck = DECK_SHORE; c.p = Vector2Add(S1.elder, {1.0f, 0.4f});
            CatchRec bird; bird.name = BirdOf(BIRD_GULL).name; bird.kg = 1; bird.price = BirdOf(BIRD_GULL).value; bird.dead = true; bird.sp = Species().Find("gull flock");
            CatchRec fish; fish.name = "kelp bass"; fish.sp = Species().Find("kelp bass"); fish.kg = 3; fish.price = 3; fish.dead = true;
            std::string why;
            c.carrying = true; c.carry = fish; c.carryKg = fish.kg;
            bool refused = vs.ElderGive(0, &why) == 0 && c.carrying;
            check(refused, TextFormat("Old Hoskins won't take a fish (%s)", why.c_str()));
            c.carry = bird; c.carryKg = 1;
            float m0 = vs.money, v = vs.ElderGive(0);
            check(v > 0 && fabsf(v - 2 * vs.Value(bird)) < 0.01f && fabsf(vs.money - m0 - v) < 0.01f && !c.carrying, TextFormat("Old Hoskins pays twice a gull's value in shillings (%.1f)", v));
            g.sea.weather = Weather::Rain; S1.fireLit = true; g.StepLandings(1.0f);
            check(S1.fireLit, "rain on Seal Rock: the hut's stove stays alight");
            g.sea.weather = Weather::Calm;
            Landing& P = g.landings[1];
            g.skiff.landing = 1; c.p = Vector2Add(P.elder, {1.0f, 0.4f});
            c.carrying = true; c.carry = fish; c.carryKg = fish.kg;
            refused = vs.ElderGive(0, &why) == 0;
            check(refused, TextFormat("the foreman won't take a raw fish (%s)", why.c_str()));
            CatchRec cooked = fish; cooked.cooked = true; cooked.cook = 1.5f; c.carry = cooked;
            m0 = vs.money; v = vs.ElderGive(0);
            check(v > 0 && fabsf(v - 1.5f * vs.Value(cooked)) < 0.01f && vs.money > m0, TextFormat("the foreman pays 150%% for cooked fish, in shillings (%.1f)", v));
            check(P.caches.size() == 1 && P.caches[0].kind == 1 && P.caches[0].value >= 150 && P.caches[0].value <= 350, "the cannery safe: locked, 150-350");
            c.p = {P.r - 0.6f, 0}; g.wraithCool = 0;
            for (int i = 0; i < 60 * 900 && c.tangleT <= 0; i++) g.StepWeeds(dt);
            bool caught = c.tangleT > 0;
            for (int i = 0; i < 60 * 9; i++) g.StepWeeds(dt);
            check(caught && c.overboard && Vector2Distance(c.swim, P.at) > P.r, "at the pier's edge a Kelp Wraith comes up out of the pilings and pulls the hand off into the water");
        }
    }
    e.stirOverride = -1;
    printf(fails ? "trawl-weeds-test: %d FAILED\n" : "trawl-weeds-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}
} // namespace tw
