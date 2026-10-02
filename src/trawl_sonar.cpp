// The wheelhouse sonar (design doc, "The sonar"): the crew's only view below the surface outside the lantern.
//   Passive (always on): only loud things near the boat; at half speed or more the Gannet's own screw drowns it out.
//   Active ping: one every 3 s out to 150 m; returns show for 6 s and fade; every ping is noise in the water.
//   Depth dial: the operator picks the band the scope shows (all, surface, middle, deep).
//   Marking: the operator clicks a contact; every hand sees a bearing arrow at the edge of their screen for 10 s.
// Headless (the host's step); trawl.cpp draws the scope, the side profile and the arrows. A bot on the sonar pings
// every 6 s, marks the largest school and any threat (design doc, "Bot behaviour").
#include "trawl.h"
#include "trawl_eco.h"
#include "trawl_wreck.h"
#include "raymath.h"
#include <algorithm>

namespace tw {

const char* SonarBandName(int band) { static const char* N[4] = {"ALL DEPTHS", "SURFACE 0-5 m", "MIDDLE 5-20 m", "DEEP 20 m+"}; return N[std::clamp(band, 0, 3)]; }
bool SonarBandHas(int band, float z) {
    switch (band) {
        case 1: return z < 5;
        case 2: return z >= 5 && z < 20;
        case 3: return z >= 20;
        default: return true;
    }
}

static SonarReturn ReturnOf(const EcoAgent& a) {
    const SpeciesRec& s = Species().sp[a.sp];
    SonarReturn r;
    r.p = a.p; r.sp = a.sp; r.count = a.count; r.t = SONAR_LIFE;
    r.kind = s.threat ? SonarKind::Threat : a.count > 3 ? SonarKind::School : SonarKind::Fish;
    r.size = (float)std::max(1, s.size);
    return r;
}

bool Gannet::SonarPing(int ci) {
    (void)ci;
    if (sonar.cool > 0) return false;
    sonar.cool = SONAR_COOL; sonar.sinceP = 0;
    // a fresh sweep replaces what the last one found (passive returns stay until they fade)
    sonar.ret.erase(std::remove_if(sonar.ret.begin(), sonar.ret.end(), [](const SonarReturn& r) { return !r.passive; }), sonar.ret.end());
    if (eco) {
        for (const auto& a : eco->agents) {
            if (!a.alive || a.count <= 0) continue;
            if (Vector2Distance({a.p.x, a.p.y}, boat.pos) > SONAR_RANGE) continue;
            const SpeciesRec& s = Species().sp[a.sp];
            if (s.isStatic && !s.threat) continue;       // (anemones and the like are seabed)
            if (a.p.z < -0.5f) continue;                 // (gulls are in the air)
            sonar.ret.push_back(ReturnOf(a));
        }
        eco->AddNoise({boat.pos.x, boat.pos.y, 1}, 6);   // the ping itself: Ghost Worms and the curious hear it
    }
    // set gear: the buoys and the pot floats, bright squares
    auto gear = [&](Vector2 w) { if (Vector2Distance(w, boat.pos) <= SONAR_RANGE) { SonarReturn r; r.p = {w.x, w.y, 0.5f}; r.kind = SonarKind::Gear; r.t = SONAR_LIFE; sonar.ret.push_back(r); } };
    for (const auto& l : longlines) { gear(l.a); gear(l.b); }
    for (const auto& p : pots) gear(p.p);
    // the wrecks: a long hard return on the floor (where to dive)
    if (wrecks) for (int k = 0; k < (int)wrecks->size(); k++) {
        const Wreck& w = (*wrecks)[k];
        if (Vector2Distance({w.x, w.y}, boat.pos) > SONAR_RANGE) continue;
        SonarReturn r; r.p = {w.x, w.y, w.depth}; r.kind = SonarKind::Wreck; r.size = (float)std::max(2, w.gw); r.count = k; r.t = SONAR_LIFE * 1.5f;   // (a hard return lingers)
        sonar.ret.push_back(r);
    }
    return true;
}

bool Gannet::SonarMarkAt(int ci, Vector2 aimDeck) {
    Vector2 w = boat.ToWorld(aimDeck);
    int best = -1; float bd = 15;
    for (int i = 0; i < (int)sonar.ret.size(); i++) {
        const SonarReturn& r = sonar.ret[i];
        if (!SonarBandHas(sonar.band, r.p.z)) continue;
        float d = Vector2Distance({r.p.x, r.p.y}, w);
        if (d < bd) { bd = d; best = i; }
    }
    if (best < 0) return false;
    const SonarReturn& r = sonar.ret[best];
    SonarMark m; m.p = {r.p.x, r.p.y}; m.t = SONAR_MARK_LIFE; m.by = ci;
    m.what = r.kind == SonarKind::Gear ? "our gear" : r.sp >= 0 ? (r.kind == SonarKind::School ? Species().sp[r.sp].name + " school" : Species().sp[r.sp].name) : "a contact";
    if (r.kind == SonarKind::Threat) m.what = "something big";   // (the scope shows a shape, not a name)
    if (r.kind == SonarKind::Wreck) {
        const Wreck* wk = wrecks && r.count >= 0 && r.count < (int)wrecks->size() ? &(*wrecks)[r.count] : nullptr;
        m.what = wk ? TextFormat("a wreck, %.0f m down%s", wk->depth, wk->bell ? " (the bell's depth)" : "") : "a wreck";
    }
    // one mark per contact: re-marking refreshes it
    for (auto& o : sonar.marks) if (Vector2Distance(o.p, m.p) < 8) { o = m; return true; }
    sonar.marks.push_back(m);
    if (sonar.marks.size() > 4) sonar.marks.erase(sonar.marks.begin());
    Vector2 d = boat.ToDeck(m.p);
    float brg = atan2f(d.y, d.x) * RAD2DEG;                  // 0 dead ahead, + to starboard
    const char* side = fabsf(brg) < 20 ? "dead ahead" : fabsf(brg) > 160 ? "astern" : brg > 0 ? (brg < 90 ? "off the starboard bow" : "off the starboard quarter") : (brg > -90 ? "off the port bow" : "off the port quarter");
    Say(TextFormat("Sonar: %s %s, %.0f m", m.what.c_str(), side, Vector2Length(d)));
    return true;
}

void Gannet::StepSonar(float dt) {
    if (sonar.cool > 0) sonar.cool -= dt;
    sonar.sinceP += dt;
    for (auto& r : sonar.ret) r.t -= dt;
    sonar.ret.erase(std::remove_if(sonar.ret.begin(), sonar.ret.end(), [](const SonarReturn& r) { return r.t <= 0; }), sonar.ret.end());
    for (auto& m : sonar.marks) m.t -= dt;
    sonar.marks.erase(std::remove_if(sonar.marks.begin(), sonar.marks.end(), [](const SonarMark& m) { return m.t <= 0; }), sonar.marks.end());
    // passive: loud things near her (a strike, a chase, anything big moving), unless her own screw drowns them
    if ((sonar.passiveT -= dt) <= 0 && eco) {
        sonar.passiveT = 0.5f;
        if (boat.shaft < 0.5f) for (const auto& a : eco->agents) {
            if (!a.alive || a.p.z < -0.5f || Vector2Distance({a.p.x, a.p.y}, boat.pos) > 45) continue;
            const SpeciesRec& s = Species().sp[a.sp];
            bool loud = a.flash > 0 || a.target >= 0 || (s.threat && !s.isStatic) || s.size >= 4;
            if (!loud) continue;
            SonarReturn r = ReturnOf(a); r.passive = true; r.t = 2;
            bool had = false;
            for (auto& o : sonar.ret) if (o.passive && o.sp == r.sp && Vector3Distance(o.p, r.p) < 6) { o = r; had = true; }
            if (!had) sonar.ret.push_back(r);
        }
    }
    // a bot on the sonar: pings every 6 s, marks the largest school and any threat
    for (int i = 0; i < (int)crew.size(); i++) {
        const Crew& c = crew[i];
        if (!botsOn || !c.bot || c.dead || c.overboard || c.station < 0 || Stations()[c.station].kind != StationKind::Sonar) continue;
        if ((sonar.botT -= dt) > 0) break;
        sonar.botT = 6;
        if (!SonarPing(i)) break;
        int best = -1, most = 0;
        for (int k = 0; k < (int)sonar.ret.size(); k++) {
            const SonarReturn& r = sonar.ret[k];
            if (r.kind == SonarKind::Threat) { SonarMarkAt(i, boat.ToDeck({r.p.x, r.p.y})); continue; }
            if (r.kind == SonarKind::Wreck && r.count >= 0 && r.count < 32 && !(sonar.wrecksMarked & (1u << r.count))) {   // (each wreck called once)
                sonar.wrecksMarked |= 1u << r.count;
                SonarMarkAt(i, boat.ToDeck({r.p.x, r.p.y}));
                continue;
            }
            if (r.kind == SonarKind::School && r.count > most) { most = r.count; best = k; }
        }
        if (best >= 0) SonarMarkAt(i, boat.ToDeck({sonar.ret[best].p.x, sonar.ret[best].p.y}));
        break;
    }
}

} // namespace tw
