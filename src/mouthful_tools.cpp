// Mouthful's headless checks (main.cpp): --mouthful-test (the rules), --mouthful-round (a bot-only round and its
// report, doc p. 21), --mouthful-duel (two forms at equal mass, the counters table).
#include "mouthful.h"
#include "raymath.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

namespace mf {

static int gFails = 0;
static void Check(bool ok, const char* what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what); if (!ok) gFails++; }
static int AgentOf(World& w, const char* species) { int sp = w.eco.map->SpeciesIndex(species); for (int i = 0; i < (int)w.eco.agents.size(); i++) if (w.eco.agents[i].alive && w.eco.agents[i].sp == sp) return i; return -1; }
static void Face(Mouth& m, Vector3 at) { Vector3 d = Vector3Subtract(at, m.pos); m.yaw = atan2f(d.z, d.x); m.pitch = atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z)); m.in.yaw = m.yaw; m.in.pitch = m.pitch; }
static void SetMass(World& w, Mouth& m, float mass) { m.mass = mass; m.tier = w.TierOfMass(mass); }
static void Clear(World& w, Vector3 p) { for (auto& a : w.eco.agents) if (a.diver < 0 && a.alive && Vector3Distance(a.pos, p) < 12) a.alive = false; }
static void Become(World& w, Mouth& m, const char* key) { int f = FormIndex(key); m.form = f; m.path = D().forms[f].path; m.pendingFork = 0; m.forkOpts.clear(); (void)w; }

int RunMouthfulTest() {
    gFails = 0;
    printf("Mouthful: the rules\n");
    const Data& d = D();
    Check(d.forms.size() >= 28, TextFormat("%d forms loaded", (int)d.forms.size()));
    Check(ForkChoices(-1, 2, 0).size() == 6, "fork 1 offers the six paths");
    for (int p = 0; p < P_COUNT; p++) {
        size_t f4 = ForkChoices(p, 4, 0).size(), f6 = ForkChoices(p, 6, 0).size();
        Check(p == P_BLOB ? (f4 == 1 && f6 == 1) : (f4 == 2 && f6 == 2), TextFormat("%s: %d second forms, %d finals", PathName(p), (int)f4, (int)f6));
    }
    Check(ForkChoices(P_SHARK, 2, 0).size() == 6 && d.forms[ForkChoices(P_SHARK, 2, 0)[0]].path == P_SHARK, "a dead shark's next fork offers the shark first");
    Check(d.tiers[2].mass == 30 && d.tiers[3].mass == 100 && d.tiers[8].mass == 4500, "tier thresholds 30/100/.../4500");
    Check(FloorY(-280, 0) > -8 && FloorY(-100, 0) < -14 && FloorY(50, 0) < -110 && FloorY(240, 0) < -280, "the shelf: shallows, reef, blue, trench");
    Check(BandAt({-250, -3, 0}) == B_SHALLOWS && BandAt({-100, -20, 0}) == B_REEF && BandAt({-15, -80, 0}) == B_WALL && BandAt({60, -40, 0}) == B_BLUE && BandAt({220, -200, 0}) == B_TRENCH, "bands by place");

    World w; Opts o; o.humans = 1; o.bots = 0; o.seed = 7;
    w.Init(o);
    int alive = 0; for (const auto& a : w.eco.agents) if (a.alive && a.diver < 0) alive++;
    Check(alive > 400, TextFormat("the web: %d fish", alive));
    Check(w.leviathan >= 0, "the leviathan sleeps in its hollow");
    Mouth& me = w.mouths[0];
    Check(me.alive && me.mass == d.startMass && me.immuneT > 0 && BandAt(me.pos) == B_SHALLOWS, "a fry spawns in the shallows, glowing, with 15 mass");

    // a fry eats a minnow
    int mi = AgentOf(w, "Minnow");
    me.immuneT = 0;
    w.eco.agents[mi].pos = Vector3Add(me.pos, {0.25f, 0, 0}); Face(me, w.eco.agents[mi].pos);
    w.Bite(me);
    Check(!w.eco.agents[mi].alive && fabsf(me.mass - 18) < 0.01f, TextFormat("a fry swallows a minnow (mass %.1f)", me.mass));
    // the first fork
    SetMass(w, me, 35); w.GrowCheck(me);
    Check(me.pendingFork == 2 && me.forkOpts.size() == 6, "tier 2 offers the first fork");
    int sharkPick = -1; for (int k = 0; k < (int)me.forkOpts.size(); k++) if (d.forms[me.forkOpts[k]].path == P_SHARK) sharkPick = k;
    w.PickFork(me, sharkPick);
    Check(me.path == P_SHARK && d.forms[me.form].key == "dogfish", "picking the shark makes a Dogfish");
    // a fight with a tuna: too big to swallow, so bites
    SetMass(w, me, 90); w.GrowCheck(me); me.swallowT = 0; Clear(w, me.pos);
    int ti = AgentOf(w, "Tuna");
    rt::Agent& tuna = w.eco.agents[ti];
    tuna.pos = Vector3Add(me.pos, {0.5f, 0, 0}); Face(me, tuna.pos);
    float hp0 = tuna.hp; me.biteCd = 0; w.Bite(me);
    Check(tuna.alive && tuna.hp < hp0, "a tuna over 60% of your mass is a fight, not a meal");
    // a mouth swallows a smaller mouth
    int b = w.AddMouth("Victim", true, 1, -1);
    Mouth& v = w.mouths[b]; Mouth& m0 = w.mouths[0];
    v.immuneT = 0; SetMass(w, v, 200); SetMass(w, m0, 500); w.GrowCheck(m0); Clear(w, m0.pos);
    v.pos = Vector3Add(m0.pos, {0.6f, 0, 0}); Face(m0, v.pos); m0.biteCd = 0; m0.swallowT = 0;
    w.Bite(m0);
    Check(!v.alive && m0.kills == 1 && m0.mass > 690, TextFormat("a mouth under 60%% is swallowed whole (%.0f mass)", m0.mass));
    // equal mass: a fight
    w.Respawn(v, false); v.immuneT = 0; SetMass(w, v, 450); v.pos = Vector3Add(m0.pos, {0.6f, 0, 0}); Face(m0, v.pos); m0.biteCd = 0; m0.swallowT = 0; SetMass(w, m0, 500);
    w.Bite(m0);
    Check(v.alive && v.mass < 450 && v.mass > 380, TextFormat("between 60%% and 100%%: a bite takes ~10%% (%.0f left)", v.mass));
    // a glowing fry marks its biter
    w.Respawn(v, false); v.pos = Vector3Add(m0.pos, {0.6f, 0, 0}); Face(m0, v.pos); m0.biteCd = 0; m0.markT = 0;
    w.Bite(m0);
    Check(v.alive && m0.markT > 0, "biting a glowing fry does nothing but mark you for the sharks");
    // the crown
    SetMass(w, m0, 4600); w.GrowCheck(m0);
    Check(w.king == m0.id && m0.king, "the first to tier 8 is King");
    v.immuneT = 0; SetMass(w, v, 4500); w.GrowCheck(v);
    SetMass(w, m0, 4000); m0.tier = 7; Clear(w, m0.pos);
    for (int k = 0; k < 40 && m0.alive; k++) { v.pos = Vector3Add(m0.pos, {0.8f, 0, 0}); Face(v, m0.pos); v.biteCd = 0; v.swallowT = 0; w.Bite(v); }
    Check(!m0.alive && w.king == v.id, "killing the king passes the crown");
    // an NPC reef shark eats a tier-3
    w.Respawn(m0, false); m0.immuneT = 0; SetMass(w, m0, 150);
    int si = AgentOf(w, "Reef Shark");
    w.OnDiverHit(m0.agent, si, 100);
    Check(!m0.alive && w.deathsBy[1] == 1, "a reef shark swallows a tier-3");
    // respawn
    for (int k = 0; k < 6 * 20; k++) w.StepMouth(m0, 0.05f);
    Check(m0.alive && m0.mass == d.startMass && m0.immuneT > 0, "five seconds later, a fry again");
    // decay
    m0.immuneT = 0; SetMass(w, m0, 1000); float t0 = w.time; (void)t0;
    for (int k = 0; k < 600; k++) { m0.in = Input{}; w.StepMouth(m0, 0.1f); }
    Check(m0.mass < 995 && m0.mass > 985, TextFormat("a tier-5 decays ~1%% a minute (%.1f)", m0.mass));
    // abilities: the dash, the ink, inflation
    Become(w, m0, "moray"); SetMass(w, m0, 60); m0.abCd = 0; m0.stunT = 0; m0.swallowT = 0; m0.pos = {-100, -20, 0}; m0.yaw = 0; m0.pitch = 0; m0.vel = {0, 0, 0};
    Vector3 p0 = m0.pos; w.UseAbility(m0);
    for (int k = 0; k < 6; k++) { m0.in = Input{}; m0.in.yaw = 0; w.StepMouth(m0, 0.05f); }
    Check(m0.pos.x - p0.x > 4.5f, TextFormat("the Moray's dash: %.1f m", m0.pos.x - p0.x));
    Become(w, m0, "reef_squid"); m0.abCd = 0; w.UseAbility(m0);
    Check(!w.clouds.empty() && w.clouds.back().owner == m0.id, "the squid's ink leaves a cloud");
    Become(w, v, "puffer"); SetMass(w, v, 100); v.immuneT = 0; v.abCd = 0; v.swallowT = 0; v.stunT = 0; w.UseAbility(v);
    Become(w, m0, "bull"); SetMass(w, m0, 400);
    Check(!SwallowOk(w, m0, v), "an inflated puffer can't be swallowed by anything under tier 5");
    v.abT = 0;
    Check(SwallowOk(w, m0, v), "...and can once it lets the air out");
    // hiding: a fry in an eel hole is out of a big mouth's reach
    const Hole& h = Holes()[0];
    w.Respawn(v, false); v.immuneT = 0; SetMass(w, v, 20); v.pos = h.pos; v.vel = {0, 0, 0}; v.in = Input{}; w.StepMouth(v, 0.01f);
    SetMass(w, m0, 400); m0.pos = Vector3Add(h.pos, {0.7f, 0, 0}); Face(m0, v.pos); m0.biteCd = 0; m0.swallowT = 0;
    w.Bite(m0);
    Check(v.alive && v.hidden, "a fry in an eel hole can't be reached by a tier-4");
    // the leviathan eats a king in the trench
    w.Respawn(v, false); v.immuneT = 0; SetMass(w, v, 5000); w.GrowCheck(v); v.pos = Vector3Add(HOLLOW, {6, 10, 0});
    for (int k = 0; k < 400 && v.alive; k++) { v.in = Input{}; w.StepMouth(v, 0.05f); w.StepNpc(0.05f); }
    Check(!v.alive && w.levAte, "a king in the hollow wakes the leviathan, and it eats");
    printf(gFails ? "Mouthful: %d check(s) FAILED\n" : "Mouthful: all checks passed\n", gFails);
    return gFails ? 1 : 0;
}

// A bot-only round (doc p. 21): time to the first king, crowns changed, deaths by cause, the best tier per path,
// whether the leviathan ate anyone.
int RunMouthfulRound(int bots, float minutes, uint32_t seed, int runs) {
    printf("Mouthful: %d bots, %.0f-minute rounds, %d run(s)\n", bots, minutes, runs);
    double firstKing = 0; int kings = 0, crowns = 0, deaths[4] = {}, levRounds = 0, best[P_COUNT] = {};
    for (int r = 0; r < runs; r++) {
        World w; Opts o; o.humans = 0; o.bots = bots; o.minutes = minutes; o.seed = seed + r * 7919u;
        w.Init(o);
        auto t0 = std::chrono::steady_clock::now();
        int lastMin = -1;
        while (!w.over) {
            w.Step(1 / 20.0f);
            if (getenv("DEPTH_MFBOT") && fmodf(w.time, 5.0f) < 0.05f) for (const auto& m : w.mouths) if (m.name.rfind(getenv("DEPTH_MFBOT"), 0) == 0) {
                Vector3 g = m.tgtAgent >= 0 ? w.eco.agents[m.tgtAgent].pos : m.tgtMouth >= 0 ? w.mouths[m.tgtMouth].pos : m.goal;
                printf("   %5.0f %s t%d %.0f %s pos(%.0f,%.0f,%.0f) %s tgt %s (%.0f,%.0f,%.0f) d %.1f flee %d spd %.1f\n", w.time, m.name.c_str(), m.tier, m.mass, BandName(BandAt(m.pos)), m.pos.x, m.pos.y, m.pos.z, D().forms[m.form].name.c_str(),
                       m.tgtAgent >= 0 ? w.eco.map->species[w.eco.agents[m.tgtAgent].sp].name.c_str() : m.tgtMouth >= 0 ? "mouth" : "goal", g.x, g.y, g.z, Vector3Distance(g, m.pos), m.fleeing, Vector3Length(m.vel));
                if (m.tgtMouth >= 0) { const Mouth& o = w.mouths[m.tgtMouth]; printf("        target %s t%d %.0f imm %.1f hid %d bur %.1f amb %d biteCd %.2f swal %.2f stun %.2f hold %.2f reach %.2f\n", o.name.c_str(), o.tier, o.mass, o.immuneT, o.hidden, o.buriedT, o.ambush, m.biteCd, m.swallowT, m.stunT, m.holdT, w.Reach(m)); }
            }
            if (getenv("DEPTH_MFTRACE") && (int)(w.time / 60) != lastMin) {
                lastMin = (int)(w.time / 60);
                printf("   [%2d min]", lastMin);
                for (int id : w.Board()) { const Mouth& m = w.mouths[id]; printf(" %s:t%d/%.0f%s", m.name.substr(0, 6).c_str(), m.tier, m.mass, m.alive ? "" : "x"); }
                printf("\n");
            }
        }
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        if (w.firstKingT >= 0) { firstKing += w.firstKingT; kings++; }
        crowns += w.crownsChanged; for (int k = 0; k < 4; k++) deaths[k] += w.deathsBy[k]; if (w.levAte) levRounds++;
        for (int p = 0; p < P_COUNT; p++) best[p] = std::max(best[p], w.bestTierByPath[p]);
        int tot = w.deathsBy[0] + w.deathsBy[1] + w.deathsBy[2] + w.deathsBy[3];
        printf("  run %d: first king %s, crowns changed %d, deaths %d (players %d, NPC %d, hazards %d, leviathan %d), %.0f s of CPU\n", r,
               w.firstKingT >= 0 ? TextFormat("%.1f min", w.firstKingT / 60) : "never", w.crownsChanged, tot, w.deathsBy[0], w.deathsBy[1], w.deathsBy[2], w.deathsBy[3], secs);
        printf("     standings:");
        for (int id : w.Board()) { const Mouth& m = w.mouths[id]; printf(" %s (%s, best tier %d, %d kills, %.0f)", m.name.c_str(), m.botLevel == 1 ? "Minnow" : m.botLevel == 2 ? "Hunter" : "Shark", m.bestTier, m.kills, ScoreOf(w, m)); }
        printf("\n");
    }
    int tot = deaths[0] + deaths[1] + deaths[2] + deaths[3];
    printf("Summary: first king %s on average (%d of %d rounds); crowns changed %.1f a round (target 3-6)\n", kings ? TextFormat("%.1f min", firstKing / kings / 60) : "never", kings, runs, crowns / (double)runs);
    if (tot) printf("Deaths by cause: players %.0f%%, NPC predators %.0f%%, hazards %.0f%%, the leviathan %.0f%% (targets 60/25/10/5)\n", 100.0 * deaths[0] / tot, 100.0 * deaths[1] / tot, 100.0 * deaths[2] / tot, 100.0 * deaths[3] / tot);
    printf("Best tier per path:"); for (int p = 0; p < P_COUNT; p++) printf(" %s %d", PathName(p), best[p]); printf("\n");
    printf("The leviathan ate someone in %d of %d rounds\n", levRounds, runs);
    return 0;
}

// Two forms at equal mass, scripted to fight each other (doc p. 21: the counters table within 55-65%; mirrors 50 +/- 3)
int RunMouthfulDuel(const std::string& a, const std::string& b, float mass, int runs) {
    int fa = FormIndex(a), fb = FormIndex(b);
    if (fa < 0 || fb < 0) { printf("unknown form (keys are in data/mouthful/mouthful_paths.json)\n"); return 1; }
    int wa = 0, wb = 0, draws = 0;
    for (int r = 0; r < runs; r++) {
        World w; Opts o; o.humans = 0; o.bots = 2; o.minutes = 30; o.seed = 1000 + r * 31u; o.botLevel = 3;
        w.Init(o);
        for (auto& ag : w.eco.agents) if (ag.diver < 0) ag.alive = false;   // (the arena empty: only the two)
        std::fill(w.reviveS.begin(), w.reviveS.end(), 0.0f);
        w.duel = true;
        bool floor = D().forms[fa].walker || D().forms[fb].walker;
        Vector3 c = floor ? Vector3{-120, 0, (float)(r % 9) * 20 - 80} : Vector3{60, -50, (float)(r % 9) * 20 - 80};
        for (int k = 0; k < 2; k++) {
            Mouth& m = w.mouths[k];
            m.form = k ? fb : fa; m.path = D().forms[m.form].path; m.mass = mass; m.tier = w.TierOfMass(mass); m.immuneT = 0;
            m.pos = Vector3Add(c, {k ? 5.0f : -5.0f, 0, (r % 2 ? 1.0f : -1.0f) * 0.5f});
            if (floor) m.pos.y = FloorY(m.pos.x, m.pos.z) + 0.3f;
            m.yaw = k ? PI : 0;
        }
        while (w.time < 120 && w.mouths[0].alive && w.mouths[1].alive && w.mouths[0].deaths == 0 && w.mouths[1].deaths == 0) w.Step(1 / 30.0f);
        bool aDead = w.mouths[0].deaths > 0 || !w.mouths[0].alive, bDead = w.mouths[1].deaths > 0 || !w.mouths[1].alive;
        if (aDead && !bDead) wb++;
        else if (bDead && !aDead) wa++;
        else if (w.mouths[0].mass > w.mouths[1].mass * 1.05f) wa++;
        else if (w.mouths[1].mass > w.mouths[0].mass * 1.05f) wb++;
        else draws++;
    }
    int dec = std::max(1, wa + wb);
    printf("%s vs %s at %.0f mass: %d-%d (%d draws): %s wins %.0f%%\n", D().forms[fa].name.c_str(), D().forms[fb].name.c_str(), mass, wa, wb, draws, D().forms[fa].name.c_str(), 100.0 * wa / dec);
    return 0;
}

} // namespace mf
