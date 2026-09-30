// Red Tide's headless tools (design doc, "Simulation tools" and "Ecosystem test plan"):
//   depth.exe --eco-sim <map> <minutes> [pattern]   population curves, blood by zone, predator arrivals
//   depth.exe --web-check <map>                     the food web's structural rules
//   depth.exe --eco-test <map>                      the ecosystem test plan's assertions (sprat chain, blood, alarm...)
// Patterns (scripted players, no bodies yet): none, sprat, quiet (knife), loud (powder guns), camping (one room),
// spread (four rooms).
#include "redtide.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>

namespace rt {

static const float TICK = 0.1f; // 10 Hz decisions; movement steps at the same rate headless (the budget's 60 Hz is for display)

// A scripted player kill: the nearest beast of a species (or any killable beast) to a point dies by "player" hand,
// making the weapon's noise.
static int ScriptKill(Ecosystem& e, Vector3 at, int sp, float noise, bool melee) {
    int best = -1;
    float bd = 1e9f;
    for (int i = 0; i < (int)e.agents.size(); i++) {
        const Agent& a = e.agents[i];
        if (!a.alive || a.diver >= 0) continue;
        const Species& s = e.map->species[a.sp];
        if (sp >= 0 && a.sp != sp) continue;
        if (sp < 0 && (s.tier >= 4 || s.isEnemy)) continue;
        float d = Vector3Distance(a.pos, at);
        if (d < bd) { bd = d; best = i; }
    }
    if (best < 0) return -1;
    Vector3 p = e.agents[best].pos;
    e.Kill(best, -3, melee);
    if (noise > 0) e.AddNoise(p, noise);
    return best;
}

static Vector3 ZoneCenter(const MapData& m, const char* name) {
    int zi = m.ZoneIndex(name);
    return zi >= 0 ? m.zones[zi].Center() : m.zones[0].Center();
}

static float ZoneBlood(const Ecosystem& e, int zi) {
    double t = 0;
    const Field& f = e.scent;
    for (size_t i = 0; i < f.v.size(); i++) if (f.zone[i] == zi) t += f.v[i];
    return (float)t;
}

// Tracks the first time each big species responds to blood and reaches a site.
struct ArrivalWatch {
    std::map<int, float> called, arrived;
    void Update(const Ecosystem& e, Vector3 site, float since, float radius = 10) {
        for (const auto& a : e.agents) {
            if (!a.alive || a.diver >= 0) continue;
            const Species& s = e.map->species[a.sp];
            if (s.tier < 3 && s.size < 3) continue;
            // called: investigating blood (heading somewhere it smells), not just busy with something of its own
            if (a.st == State::Investigate && !called.count(a.sp)) called[a.sp] = e.time - since;
            if (Vector3Distance(a.pos, site) < radius && !arrived.count(a.sp) && called.count(a.sp)) arrived[a.sp] = e.time - since;
        }
    }
};

int RunEcoSim(const std::string& key, float minutes, const std::string& pattern) {
    std::string why;
    if (!DataOk(&why)) { printf("eco-sim: %s\n", why.c_str()); return 1; }
    const MapData& m = Map(key);
    if (m.species.empty()) { printf("eco-sim: no species for map '%s'\n", key.c_str()); return 1; }
    Ecosystem e;
    e.Init(m, 1234, pattern == "loud" || pattern == "spread" ? 8 : 1, 4);
    e.log = true;
    printf("Red Tide eco-sim: %s, %.0f min, pattern '%s' (%d agents, %d flora patches, scent grid %dx%dx%d @ %.1f m)\n",
           m.title.c_str(), minutes, pattern.c_str(), (int)e.agents.size(), (int)e.flora.size(), e.scent.nx, e.scent.ny, e.scent.nz, e.scent.cell);
    std::vector<int> start(m.species.size(), 0);
    for (size_t i = 0; i < m.species.size(); i++) start[i] = e.Count((int)i);
    ArrivalWatch watch;
    Vector3 site = ZoneCenter(m, "Salon");
    float siteT = 0;
    const char* rooms[4] = {"Salon", "Cabins", "Engine", "Keel"};
    float nextKill = 5;
    int kills = 0;
    int steps = (int)(minutes * 60 / TICK);
    for (int st = 0; st < steps; st++) {
        e.Step(TICK);
        // the scripted players
        if (pattern == "sprat" && kills == 0 && e.time >= 10) {
            int sp = m.SpeciesIndex("Bilge Sprat");
            for (int k = 0; k < 15; k++) ScriptKill(e, site, sp, 2, false);
            kills = 15; siteT = e.time;
            printf("  [%6.1f s] 15 Bilge Sprats shot in the %s (Gannet, noise 2)\n", e.time, m.zones[m.ZoneIndex("Salon")].name.c_str());
        } else if (pattern != "none" && pattern != "sprat" && e.time >= nextKill) {
            bool quiet = pattern == "quiet";
            Vector3 at = site;
            if (pattern == "spread") at = ZoneCenter(m, rooms[kills % 4]);
            if (pattern == "loud") at = ZoneCenter(m, rooms[(kills / 6) % 4]);
            if (ScriptKill(e, at, -1, quiet ? 0 : 6, quiet) >= 0) { kills++; if (kills == 1) siteT = e.time; }
            nextKill = e.time + (quiet ? 8.0f : pattern == "camping" ? 4.0f : 5.0f);
        }
        if (siteT > 0) watch.Update(e, site, siteT);
        // once a minute: the curves
        if (st > 0 && (st * TICK) - floorf(st * TICK / 60) * 60 < TICK * 0.5f) {
            int tiers[6] = {0};
            int total = 0;
            for (const auto& a : e.agents) if (a.alive && a.diver < 0) { tiers[std::clamp(m.species[a.sp].tier, 0, 5)]++; total++; }
            printf("  -- minute %d: %d alive (tier0 %d, t1 %d, t2 %d, t3 %d, t4 %d, t5 %d), %d corpses, blood total %.0f, squads %d, alarm",
                   (int)(e.time / 60 + 0.5f), total, tiers[0], tiers[1], tiers[2], tiers[3], tiers[4], tiers[5],
                   (int)std::count_if(e.corpses.begin(), e.corpses.end(), [](const Corpse& c) { return c.active; }), e.scent.Total(), e.squadsSpawned);
            for (float v : e.alarm) printf(" %.0f", v);
            printf("\n     blood by zone:");
            for (int z = 0; z < (int)m.zones.size(); z++) printf(" %s %.0f,", m.zones[z].name.c_str(), ZoneBlood(e, z));
            printf("\n");
        }
    }
    printf("\nPopulation (start -> end, killed by players, died of anything):\n");
    for (size_t i = 0; i < m.species.size(); i++) {
        if (start[i] == 0 && e.Count((int)i) == 0 && e.deathsBySpecies[i] == 0) continue;
        printf("  %-24s %4d -> %4d   kills %3d   deaths %3d\n", m.species[i].name.c_str(), start[i], e.Count((int)i), e.killsBySpecies[i], e.deathsBySpecies[i]);
    }
    if (siteT > 0) {
        printf("\nPredators after the first kill (called = starts investigating or hunting; arrived = within 10 m of the kill site):\n");
        for (const auto& kv : watch.called) {
            auto ar = watch.arrived.find(kv.first);
            printf("  %-24s called %5.1f s   arrived %s\n", m.species[kv.first].name.c_str(), kv.second, ar != watch.arrived.end() ? TextFormat("%5.1f s", ar->second) : "  -");
        }
    }
    return 0;
}

int RunWebCheck(const std::string& key) {
    std::string why;
    if (!DataOk(&why)) { printf("web-check: %s\n", why.c_str()); return 1; }
    const MapData& m = Map(key);
    if (m.species.empty()) { printf("web-check: no species for map '%s'\n", key.c_str()); return 1; }
    int fails = 0;
    auto fail = [&](const std::string& s) { printf("  FAIL %s\n", s.c_str()); fails++; };
    printf("Red Tide web-check: %s (%d species, %d flora, %d zones, %d links)\n", m.title.c_str(), (int)m.species.size(), (int)m.flora.size(), (int)m.zones.size(), (int)m.links.size());
    // every diet row references something that exists
    for (size_t i = 0; i < m.species.size(); i++) {
        const DietRow& d = m.diet[i];
        for (const auto& fi : d.floraItems) {
            bool found = false;
            for (const auto& f : m.flora) if (f.name == fi.first) found = true;
            if (!found) fail(m.species[i].name + " eats '" + fi.first + "', which is neither a species nor a flora on this map");
        }
    }
    // every species has a predator, or a listed reason not to (toxic, apex, boss, cleaner, parasite, the enemy)
    for (size_t i = 0; i < m.species.size(); i++) {
        const Species& s = m.species[i];
        bool eaten = false;
        for (size_t j = 0; j < m.species.size(); j++) for (const auto& pw : m.diet[j].prey) if (pw.first == (int)i) eaten = true;
        bool reason = s.tier >= 4 || s.Has("boss") || s.Has("toxic") || s.Has("venom") || s.Cleaner() || s.Parasite() || s.isEnemy || s.Has("armored") || s.Has("electric") || s.Has("sessile");
        if (!eaten && !reason) fail(s.name + " has no predator and no reason not to (toxic, apex, boss...)");
    }
    // every tier-1 species has a producer (flora, plankton, detritus) or scavenges
    for (size_t i = 0; i < m.species.size(); i++) {
        const Species& s = m.species[i];
        if (s.tier != 1) continue;
        const DietRow& d = m.diet[i];
        if (d.flora <= 0 && d.plankton <= 0 && d.corpse <= 0 && d.parasites <= 0 && d.prey.empty())
            fail(s.name + " (tier 1) has nothing to eat");
    }
    // every corpse has a scavenger: some species eats corpses, and one lives in or next to every zone with beasts
    std::set<int> scavZones;
    for (size_t i = 0; i < m.species.size(); i++)
        if (m.diet[i].corpse > 0 || m.species[i].Scavenger()) { int z = m.ZoneIndex(m.species[i].homeZone); if (z >= 0) scavZones.insert(z); }
    if (scavZones.empty()) fail("nothing on the map eats corpses");
    for (int z = 0; z < (int)m.zones.size(); z++) {
        bool hasBeasts = false;
        for (const auto& s : m.species) if (m.ZoneIndex(s.homeZone) == z) hasBeasts = true;
        if (!hasBeasts) continue;
        bool near = scavZones.count(z) > 0;
        for (const auto& l : m.links) if ((l.from == z && scavZones.count(l.to)) || (l.to == z && scavZones.count(l.from))) near = true;
        if (!near) fail("no scavenger within one region of " + m.zones[z].name);
    }
    // no diet cycle unreachable: every predator's prey is reachable from a producer
    std::vector<int> fedFrom(m.species.size(), 0);
    bool changed = true;
    for (size_t i = 0; i < m.species.size(); i++) if (m.diet[i].flora > 0 || m.diet[i].plankton > 0 || m.diet[i].corpse > 0 || m.diet[i].parasites > 0) fedFrom[i] = 1;
    while (changed) {
        changed = false;
        for (size_t i = 0; i < m.species.size(); i++) {
            if (fedFrom[i]) continue;
            for (const auto& pw : m.diet[i].prey) if ((pw.first >= 0 && fedFrom[pw.first]) || pw.first == -2) { fedFrom[i] = 1; changed = true; break; }
        }
    }
    for (size_t i = 0; i < m.species.size(); i++)
        if (!fedFrom[i] && !m.diet[i].prey.empty()) fail(m.species[i].name + " only eats species that nothing feeds (unreachable diet cycle)");
    // every species' home zone resolves, and the zone graph is connected
    for (const auto& s : m.species) if (!s.isEnemy && m.ZoneIndex(s.homeZone) < 0) fail(s.name + "'s home zone '" + s.homeZone + "' is not on the blockout");
    for (const auto& r : m.spawns) if (m.ZoneIndex(r.zone) < 0 || m.SpeciesIndex(r.species) < 0) fail("spawn row '" + r.zone + " / " + r.species + "' doesn't resolve");
    Ecosystem probe;
    probe.map = &m;
    for (int z = 1; z < (int)m.zones.size(); z++) if (probe.ZonePath(0, z).empty()) fail(m.zones[z].name + " can't be reached from " + m.zones[0].name);
    printf(fails ? "web-check: %d problem(s)\n" : "web-check: OK - every species has a predator or a reason, every tier-1 species a producer, every corpse a scavenger nearby, no unreachable diet cycle\n", fails);
    return fails ? 1 : 0;
}

// The ecosystem test plan (design doc) - the assertions that run before any art exists.
int RunEcoTest(const std::string& key) {
    std::string why;
    if (!DataOk(&why)) { printf("eco-test: %s\n", why.c_str()); return 1; }
    const MapData& m = Map(key);
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("Red Tide eco-test: %s\n", m.title.c_str());

    // 1. population is stable for 10 minutes with no divers (no species to 0 or above 3x its spawn count)
    {
        Ecosystem e;
        e.Init(m, getenv("DEPTH_SEED") ? (uint32_t)atoi(getenv("DEPTH_SEED")) : 777u, 1, 4);
        std::vector<int> start(m.species.size());
        for (size_t i = 0; i < m.species.size(); i++) start[i] = e.Count((int)i);
        std::vector<int> minSeen = start, maxSeen = start;
        for (int s = 0; s < (int)(600 / TICK); s++) {
            e.Step(TICK);
            if (s % 50 == 0) for (size_t i = 0; i < m.species.size(); i++) { int c = e.Count((int)i); minSeen[i] = std::min(minSeen[i], c); maxSeen[i] = std::max(maxSeen[i], c); }
        }
        std::string bad;
        for (size_t i = 0; i < m.species.size(); i++) {
            if (start[i] == 0) continue;
            int end = e.Count((int)i);
            if (end == 0 || maxSeen[i] > 3 * start[i]) {
                bad += TextFormat(" %s %d->%d(max %d", m.species[i].name.c_str(), start[i], end, maxSeen[i]);
                for (const auto& kv : e.eatenBy) if (kv.first.second == (int)i) bad += TextFormat(", %dx by %s", kv.second, m.species[kv.first.first].name.c_str());
                bad += ");";
            }
        }
        check(bad.empty(), "population stable for 10 minutes with no divers" + (bad.empty() ? std::string() : " -" + bad));
    }
    // 2. a size-3 corpse in still water reaches a threshold-30 predator's cue at 20 m within 60 s, and a
    //    threshold-120 apex only when four such corpses exist
    {
        Ecosystem e;
        e.Init(m, 99, 1, 4);
        for (auto& a : e.agents) a.alive = false;                     // an empty sea: only the scent layer
        std::vector<Zone> dummy;
        // find the largest zone to have still water in
        int zi = 0;
        float best = 0;
        for (int z = 0; z < (int)m.zones.size(); z++) { float ar = m.zones[z].plan.width * m.zones[z].plan.height; if (ar > best) { best = ar; zi = z; } }
        Vector3 c = m.zones[zi].Center();
        const_cast<Zone&>(m.zones[zi]).flow = {0, 0, 0};               // still water for this test
        Vector3 cueAt = m.zones[zi].Clamp(Vector3Add(c, {20, 0, 0}));
        float burst = 3 * 20.0f;
        auto one = [&](int n) {
            Ecosystem t;
            t.Init(m, 5, 1, 4);
            for (auto& a : t.agents) a.alive = false;
            for (int k = 0; k < n; k++) { Vector3 p = m.zones[zi].Clamp(Vector3Add(c, {0, 0, k * 3.0f - n * 1.5f})); t.AddBlood(p, burst); Corpse co; co.sp = 0; co.pos = p; co.zone = zi; co.bloodLeft = burst; co.life = t.eng->CBy("blood_corpse_life_s", 2, 60); t.corpses.push_back(co); }
            float first30 = -1, first120 = -1;
            for (int s = 0; s < (int)(90 / TICK); s++) {
                t.Step(TICK);
                float r = t.scent.BoxSum(cueAt, 25);
                if (first30 < 0 && r >= 30) first30 = t.time;
                if (first120 < 0 && t.scent.BoxSum(cueAt, 60) >= 120) first120 = t.time;
            }
            return std::make_pair(first30, first120);
        };
        auto r1 = one(1), r4 = one(4);
        check(r1.first >= 0 && r1.first <= 60, TextFormat("one size-3 corpse cues a threshold-30 predator 20 m away within 60 s (at %.0f s)", r1.first));
        check(r1.second < 0 && r4.second >= 0, TextFormat("a threshold-120 apex cues on four such corpses (at %.0f s) but not on one", r4.second));
        (void)e;
    }
    // 3. the loud pattern at tide 8 produces an alarm squad within 90 s in a single region; spread produces none
    {
        auto run = [&](bool loud) {
            Ecosystem e;
            e.Init(m, 4242, 8, 4);
            const char* rooms[4] = {"Salon", "Cabins", "Engine", "Keel"};
            float next = 1;
            int k = 0;
            for (int s = 0; s < (int)(90 / TICK); s++) {
                e.Step(TICK);
                if (e.time >= next) {
                    // four divers firing. loud: all four in one room, every kill with a powder gun (noise 6) every 2 s.
                    // spread: the four split across the map's alarm regions (the Ship has two, so two per region, each
                    // in its own room), firing Gannets (noise 2) every 4 s - the doc's coordination lesson.
                    for (int d = 0; d < 4; d++) {
                        Vector3 at = ZoneCenter(m, loud ? rooms[0] : rooms[d]);
                        if (!loud && m.alarmRegions.size() >= 2) {
                            int reg = d % (int)m.alarmRegions.size();
                            const auto& zs = m.alarmRegions[reg].zones;
                            if (!zs.empty()) at = m.zones[zs[(d / (int)m.alarmRegions.size()) % zs.size()]].Center();
                        }
                        e.AddNoise(at, loud ? 6 : 2);
                    }
                    next = e.time + (loud ? 2.0f : 4.0f);
                    k++;
                }
            }
            return e.squadsSpawned;
        };
        int loud = run(true), spread = run(false);
        check(loud >= 1, TextFormat("loud pattern at tide 8 brings an alarm squad within 90 s (%d squads)", loud));
        check(spread == 0, TextFormat("spread pattern brings none in the same time (%d squads)", spread));
    }
    // 4. killing every cleaner in a region raises host aggression map-wide within 5 s, decaying after 300 s
    {
        Ecosystem e;
        e.Init(m, 31, 1, 4);
        bool any = false;
        for (size_t i = 0; i < e.agents.size(); i++) if (e.agents[i].alive && m.species[e.agents[i].sp].Cleaner()) { any = true; e.Kill((int)i, -3); }
        for (int s = 0; s < 50; s++) e.Step(TICK);
        bool raised = e.cleanerRage > 0;
        for (int s = 0; s < (int)(300 / TICK); s++) e.Step(TICK);
        check(!any || (raised && e.cleanerRage <= 0), any ? "killing every cleaner raises host aggression within 5 s; it decays after 300 s" : "no cleaners on this map (skipped)");
    }
    // map-specific checks
    if (key == "ship") {
        // 15 sprats killed in the salon calls the barracuda within 30 s and the bull shark within 90 s
        Ecosystem e;
        e.Init(m, 2026, 1, 4);
        for (int s = 0; s < (int)(10 / TICK); s++) e.Step(TICK);
        Vector3 site = ZoneCenter(m, "Salon");
        int sp = m.SpeciesIndex("Bilge Sprat");
        int bar = m.SpeciesIndex("Great Barracuda"), bull = m.SpeciesIndex("Bull Shark");
        // isolate the response: the barracuda and the bull shark at home, calm and not yet fed (the reef's own
        // kills elsewhere would otherwise already have them investigating something)
        for (auto& a : e.agents) if (a.alive && (a.sp == bar || a.sp == bull)) { a.pos = a.home; a.zone = a.homeZone; a.st = State::Graze; a.goal = a.home; a.fedT = 0; a.hunger = 0.4f; a.stateT = 0; }
        for (int k = 0; k < 15; k++) ScriptKill(e, site, sp, 2, false);
        float t0 = e.time;
        if (getenv("DEPTH_ECODBG")) for (const auto& a : e.agents) if (a.alive && (a.sp == m.SpeciesIndex("Bull Shark") || a.sp == m.SpeciesIndex("Great Barracuda")))
            printf("    DBG t0 %s zone=%s state=%s pos=(%.0f,%.0f,%.0f) fed=%.0f hunger=%.2f smell=%.0f\n", m.species[a.sp].name.c_str(), m.zones[a.zone].name.c_str(), StateName(a.st), a.pos.x, a.pos.y, a.pos.z, a.fedT, a.hunger, e.Smell(a.pos, a.zone, m.species[a.sp].scent));
        // called = investigating blood inside the hull (where the sprat plume drains: Salon -> Cabins -> Engine -> Stern)
        float tb = -1, ts = -1, arriveB = -1;
        int zSalon = m.ZoneIndex("Salon");
        auto inHull = [&](Vector3 g) { int z = e.ZoneAt(g); return z >= 0 && m.zones[z].deck != "Outside"; };
        for (int s = 0; s < (int)(100 / TICK); s++) {
            e.Step(TICK);
            for (const auto& a : e.agents) {
                if (!a.alive) continue;
                if (a.sp == bar && tb < 0 && (a.st == State::Investigate || a.st == State::Hunt || a.st == State::Feed) && a.zone == zSalon) tb = e.time - t0;
                if (a.sp == bull && ts < 0 && a.st == State::Investigate && inHull(a.goal)) ts = e.time - t0;
                if (a.sp == bull && arriveB < 0 && ts >= 0 && (a.zone == zSalon || Vector3Distance(a.pos, a.goal) < 4)) arriveB = e.time - t0;
            }
        }
        check(tb >= 0 && tb <= 30, TextFormat("15 sprats killed in the salon call the barracuda within 30 s (%.1f s)", tb));
        check(ts >= 0 && ts <= 90, TextFormat("...and the bull shark within 90 s (%.1f s, from the Stern; reaches the plume %s)", ts, arriveB >= 0 ? TextFormat("at %.1f s", arriveB) : "later"));
        // blood made in the salon reads at the stern within 40 s (the current)
        Ecosystem b;
        b.Init(m, 7, 1, 4);
        for (auto& a : b.agents) a.alive = false;
        b.AddBlood(site, 300);
        Vector3 stern = ZoneCenter(m, "Stern");
        float tr = -1;
        for (int s = 0; s < (int)(60 / TICK); s++) { b.Step(TICK); if (tr < 0 && b.scent.BoxSum(stern, 8) >= 5) tr = b.time; }
        check(tr >= 0 && tr <= 40, TextFormat("blood made in the salon reads at the stern within 40 s (%.0f s)", tr));
    }
    printf(fails ? "eco-test: %d check(s) failed\n" : "eco-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}

} // namespace rt
