// The Trawl's balance simulator (design doc, "Tests, balance, and build order", page 47):
//     depth.exe --trawl-sim <ground> <nights> [crew 1-6] [careful|greedy|reckless] [runs] [green|able|oldhand]
// A bot crew works <nights> nights (deadlines of three) on a ground. Hand 0 is a scripted skipper (the doc's bots never
// captain: it buys the stores, picks the fishing spot from the chart, keeps the fire when there is no one else to,
// and runs for the harbour line in time); every other hand is a real bot from trawl_bots.cpp. It reports the quota-met
// rate, money per night by source (hook, net, gun, set gear, dive), deaths by cause, which threats arrived and when,
// and the time at sea. Headless; the same Gannet, Eco and Session the game runs. DEPTH_TRACE=1 prints each night.
#include "trawl_session.h"
#include "raymath.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace tw {
namespace {

// How the skipper plays. The doc's balance targets name a "careful" and a "depth-charge" pattern (Kraken seen per night).
struct SkipperPattern { const char* name; float leaveAt; int lantern; bool chum, charges; float restless; };
const SkipperPattern PATTERNS[] = {
    {"careful", 430, 1, false, false, 150},   // the lantern low, no chum, turns for home at 03:10, patient
    {"greedy", 465, 2, true, false, 70},      // full lantern, chum on the spot, stays to 03:45, moves when the fishing is slow
    {"reckless", 465, 2, true, true, 70},     // greedy, and a depth charge over the side when a shark shows
};
const float HAUL_AHEAD = 40;                  // minutes before the turn for home the net starts coming in

struct Night {
    float money[CS_COUNT] = {};
    float sold = 0;
    int landed = 0, rams = 0, overboard = 0;
    std::vector<std::pair<std::string, float>> arrivals;   // species, minutes after 20:00
    std::vector<std::string> deaths;
    bool customs = false, sunk = false, lateThreat = false;
    float seaMin = 0;                                      // real minutes between casting off and the quay
    float leftAt = -1;                                     // the clock when the skipper turned for home
    Variant variant = Variant::None; std::string canoeWord;
};

int StationIdx(StationKind k) { for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == k) return i; return -1; }

struct Skipper {
    Gannet& G; Session& S; Eco& E; const SkipperPattern& P;
    uint32_t rng;
    int helm, gut, portRod, winch;
    struct Spot { Vector2 p, a, b; bool tow = false, edge = false; };   // the mark, and a tow leg across open water either side of it
    std::vector<Spot> spots; int spotI = 0;
    int towLeg = 0, netHand = -1; bool towing = false;
    bool onSpot = false, homeward = false;
    float slowT = 0, chumT = 0, t = 0; size_t h0 = 0;
    bool chargeNow = false;
    float groundT = 0, fouledT = 0, snagT = 0; int groundSide = 1, groundN = 0, snagN = 0; bool wasSnag = false, viaMark = false, viaDone = false;
    float leftAt = -1, lastLoad = 0;             // the minute the skipper turned for home; the net load last seen
    Skipper(Gannet& g, Session& s, Eco& e, const SkipperPattern& p, uint32_t seed) : G(g), S(s), E(e), P(p), rng(seed * 7919u + 13) {
        helm = StationIdx(StationKind::Helm); gut = StationIdx(StationKind::Gutting); portRod = StationIdx(StationKind::PortRod); winch = StationIdx(StationKind::NetWinch);
    }
    float R() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) * (1.0f / 16777216.0f); }
    bool NearLanding(Vector2 p, float extra) const { for (Vector2 la : E.landingAt) if (Vector2Distance(p, la) < 13 + extra) return true; return false; }
    // (the Grotto) the arch's two ends: into and out of the cave every course goes through them
    bool Cave() const { return E.ground == "grotto"; }
    Vector2 ArchOut() const { return {E.archX0 - 18, E.archY}; }
    Vector2 ArchIn() const { return {E.archX1 + 26, E.archY}; }
    Vector2 Via(Vector2 from, Vector2 to) const {
        if (!Cave()) return to;
        bool fromIn = from.x > E.archX0, toIn = to.x > E.archX1;
        if (!fromIn && toIn) return (fabsf(from.y - E.archY) > 4 || from.x < E.archX0 - 22) ? ArchOut() : ArchIn();
        if (fromIn && !toIn) return (from.x > E.archX1 + 20 && fabsf(from.y - E.archY) > 4) ? ArchIn() : ArchOut();
        return to;
    }
    // a straight course that never crosses water she would ground on (the reef, the shoals, the island)
    bool RouteClear(Vector2 a, Vector2 b) const {
        if (Cave() && (a.x > E.archX1) != (b.x > E.archX1)) {   // (in or out of the cave: through the arch)
            bool aIn = a.x > E.archX1;
            return RouteClear0(a, aIn ? ArchIn() : ArchOut()) && RouteClear0(aIn ? ArchOut() : ArchIn(), b);
        }
        return RouteClear0(a, b);
    }
    bool RouteClear0(Vector2 a, Vector2 b) const {
        float L = Vector2Distance(a, b);
        for (float s = 10; s < L; s += 6) { Vector2 p = Vector2Lerp(a, b, s / L); if (E.DepthAt(p) < 2.6f || E.MarkAt(p) >= 0 || E.HabAt(p) == H_KELP || NearLanding(p, 20)) return false; }   // (and never through skiff water: the weed fouls her screw)
        return true;
    }

    // The chart: the richest water the crew's tackle can fish, 80-260 m from the harbour mouth, deep enough to lie in.
    void ChooseSpots() {
        spots.clear();
        const auto& SP = Species().sp;
        std::vector<std::pair<float, Vector2>> cand;
        float size = E.n * E.cell;
        for (float y = 20; y < size - 20; y += 24)
            for (float x = 20; x < size - 20; x += 24) {
                Vector2 p{x + (R() - 0.5f) * 10, y + (R() - 0.5f) * 10};
                float dh = Vector2Distance(p, S.harbour);
                if (dh < (E.ground == "weeds" ? 140 : 80) || dh > 260 + (S.CoalToReach() - 10) * 4) continue;
                if (E.ground == "weeds" && p.x < 130) continue;   // (the thin apron along the island, north and south of the quay)   // (a ground further out is fished further out; the Weeds' apron off the island is thin water)
                bool atl = E.ground == "atlantis";
                if (atl && p.x < 150) continue;   // (Atlantis: out over the terraces, not the island's shelf)
                float d = E.DepthAt(p);
                if (d < (atl ? 20 : 6) || d > (atl ? 90 : 35)) continue;
                bool clear = true;
                for (int k = 0; k < 8 && clear; k++) { float a = k * PI / 4; if (E.DepthAt({p.x + cosf(a) * 14, p.y + sinf(a) * 14}) < 3.5f) clear = false; }
                if (!clear || !RouteClear(S.harbour, p) || E.MarkAt(p) >= 0 || E.HabAt(p) == H_KELP || NearLanding(p, 30)) continue;
                float score = 0;
                for (int sp : E.g->species) {
                    const SpeciesRec& r = SP[sp];
                    if (r.price <= 0 || r.threat || r.protectedSp || r.netOnly) continue;
                    bool takes = false; for (Tackle tk : {Tackle::Light, Tackle::Handline, Tackle::Medium}) if (G.owned[(int)tk] && r.Takes(tk)) takes = true;
                    if (!takes) continue;
                    score += E.DensityAt(sp, p) * r.price * r.MeanKg();
                }
                cand.push_back({score, p});
            }
        std::sort(cand.begin(), cand.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (size_t i = 0; i < cand.size() && spots.size() < 6; i++) {
            bool far = true; for (const auto& s : spots) if (Vector2Distance(s.p, cand[i].second) < 50) far = false;
            if (!far) continue;
            Spot s; s.p = cand[i].second;
            // a tow leg: 40 m either side of the mark over water the net can run in (9 m or more, no reef under the mouth)
            // (the Weeds: a strict pass first, then the plain rule; a leg that snags twice is given up on the spot)
            for (int k = 0; k < (E.ground == "weeds" ? 16 : 8) && !s.tow; k++) {
                bool strict = E.ground == "weeds" && k < 8;
                float a = (k % 8) * PI / 8; Vector2 d{cosf(a) * 40, sinf(a) * 40};
                Vector2 A = Vector2Add(s.p, d), B = Vector2Subtract(s.p, d);
                bool ok = true;
                for (float u = 0; u <= 1.001f && ok; u += 0.1f) { Vector2 q = Vector2Lerp(A, B, u); int h = E.HabAt(q); if (E.DepthAt(q) < 10 || h == H_CREST || h == H_LAND || h == H_KELP || E.MarkAt(q) >= 0) ok = false; }
                // (the Weeds: the pinnacles and the kelp's fingers are small, and the net swings wide of the line: look
                // every 3 m, 6 m either side, for 12 m of clear water)
                if (ok && strict) {
                    Vector2 side = Vector2Scale(Vector2Normalize({-d.y, d.x}), 6);
                    for (float u = 0; u <= 1.001f && ok; u += 0.04f) for (int sgn = -1; sgn <= 1 && ok; sgn++) {
                        Vector2 q = Vector2Add(Vector2Lerp(A, B, u), Vector2Scale(side, (float)sgn));
                        int h = E.HabAt(q); if (E.DepthAt(q) < 12 || h == H_REEF || h == H_KELP || h == H_LAND) ok = false;
                    }
                }
                if (ok) { s.a = A; s.b = B; s.tow = true; }
            }
            spots.push_back(s);
        }
        // the Weeds: the kelp's edge, fished on the rods with the engine stopped (kelp bass, opaleye and sheephead live in
        // the canopy and feed out of it; a cast reaches the edge from open water 8-14 m off it)
        if (E.ground == "weeds") {
            std::vector<std::pair<float, Vector2>> edge;
            for (float y = 20; y < size - 20; y += 12)
                for (float x = 130; x < size - 20; x += 12) {
                    Vector2 p{x, y};
                    float dh = Vector2Distance(p, S.harbour);
                    if (dh < 100 || dh > 240 + (S.CoalToReach() - 10) * 4) continue;
                    float d = E.DepthAt(p);
                    if (d < 6 || d > 35 || E.HabAt(p) == H_KELP || E.MarkAt(p) >= 0 || NearLanding(p, 30)) continue;
                    bool clear = true; Vector2 kelpAt{}; bool kelp = false;
                    for (int k = 0; k < 8; k++) {
                        float a = k * PI / 4; Vector2 q{p.x + cosf(a) * 8, p.y + sinf(a) * 8};
                        int h = E.HabAt(q);
                        if (h == H_KELP) { if (!kelp) kelpAt = Vector2Add(p, {cosf(a) * 12, sinf(a) * 12}); kelp = true; continue; }
                        if (E.DepthAt(q) < 3.5f || h == H_LAND) clear = false;
                    }
                    if (!kelp || !clear || !RouteClear(S.harbour, p)) continue;
                    float score = 0;
                    for (int sp : E.g->species) {
                        const SpeciesRec& r = SP[sp];
                        if (r.price <= 0 || r.threat || r.protectedSp || r.netOnly) continue;
                        bool takes = false; for (Tackle tk : {Tackle::Light, Tackle::Medium}) if (G.owned[(int)tk] && r.Takes(tk)) takes = true;
                        if (!takes) continue;
                        score += (E.DensityAt(sp, p) + E.DensityAt(sp, kelpAt)) * 0.5f * r.price * r.MeanKg();
                    }
                    edge.push_back({score, p});
                }
            std::sort(edge.begin(), edge.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
            int added = 0;
            for (size_t i = 0; i < edge.size() && added < 3; i++) {
                bool far = true; for (const auto& s : spots) if (Vector2Distance(s.p, edge[i].second) < 40) far = false;
                if (!far) continue;
                Spot s; s.p = edge[i].second; s.edge = true; spots.push_back(s); added++;
            }
        }
        if (G.crew.size() >= 3) std::stable_partition(spots.begin(), spots.end(), [](const Spot& s) { return s.tow; });   // (with hands for the net, the marks it can tow come first)
        else std::stable_partition(spots.begin(), spots.end(), [](const Spot& s) { return s.edge; });   // (no net hands: the kelp's edge first)
        if (spots.empty()) { Spot s; s.p = Vector2Add(S.harbour, {140, 20}); spots.push_back(s); }
        spotI = 0;
    }
    void SteerTo(Vector2 target, float slowWithin) {
        Vector2 via = Via(G.boat.pos, target);
        if (via.x != target.x || via.y != target.y) { target = via; slowWithin = 0; }   // (a waypoint in the arch: through it at speed)
        Vector2 d = Vector2Subtract(target, G.boat.pos);
        float want = atan2f(d.y, d.x), err = want - G.boat.heading;
        while (err > PI) err -= 2 * PI;
        while (err < -PI) err += 2 * PI;
        G.boat.rudder = std::clamp(err * 2.5f, -1.0f, 1.0f);
        float dist = Vector2Length(d);
        G.boat.telegraph = dist > slowWithin ? 2 : dist > 6 ? 1 : 0;
    }
    // the dock: consumables for the night (the doc's targets are measured with the starting gear)
    void Shop() {
        while (G.boat.bunker < 35 + S.CoalToReach() && S.Buy("coal")) {}   // coal first: a poor purse must still reach the ground
        G.RestockAtLocker(0);   // (spears for the speargun from the magazine's stock)
        while (G.baitShrimp < 20 && S.Buy("shrimp")) {}
        if (S.money > 60) while (G.baitSquid < 10 && S.Buy("squid")) {}
        while (G.ice < 60 && S.money > 20 && S.Buy("ice")) {}
        if (G.PatchKits() == 0 && S.money > 40) S.Buy("patch");
        if (P.chum) while (G.chum < 2 && S.money > 50 && S.Buy("chum")) {}
        if (P.charges) { int have = 0; for (const auto& sl : G.crew[0].slots) if (sl.it == Item::Charge) have += sl.ammo; for (const auto& sl : G.locker) if (sl.it == Item::Charge) have += sl.ammo; if (have == 0 && S.money > 150) S.Buy("charge"); }
    }
    void Begin() {
        ChooseSpots();
        onSpot = false; homeward = false; slowT = 0; chumT = 0; t = 0; groundN = 0; snagN = 0; wasSnag = false; viaMark = false; viaDone = false; h0 = G.hold.size(); chargeNow = false; towing = false; netHand = -1; towLeg = 0; leftAt = -1;
        G.boat.lantern = std::min(P.lantern, G.searchlight ? 3 : 2);
        if (getenv("DEPTH_TRACE")) { printf("    marks:"); for (const auto& s : spots) printf("  (%.0f,%.0f d%.0f%s)", s.p.x, s.p.y, E.DepthAt(s.p), s.tow ? " tow" : s.edge ? " edge" : ""); printf("  harbour (%.0f,%.0f)\n", S.harbour.x, S.harbour.y); }
    }
    void AtHelm() { Crew& c = G.crew[0]; if (c.dead || c.overboard) return; c.deck = 0; c.p = Stations()[helm].at; c.station = helm; }
    void Step(float dt) {
        t += dt;
        Crew& me = G.crew[0];
        bool solo = G.crew.size() == 1;
        // the fire, when no bot keeps it (solo), and the pumps
        bool boilerManned = false; for (const auto& c : G.crew) if (c.station >= 0 && Stations()[c.station].kind == StationKind::Boiler && !c.overboard && !c.dead) boilerManned = true;
        if (solo || !boilerManned) {   // (a short crew: the winch hand may be the fireman, so the skipper keeps the fire from the wheelhouse)
            if (G.boat.pressure < D().greenLo + 0.15f && G.boat.firebox < 5) G.boat.Shovel(D().shovelKg * 0.05f);
            if (G.boat.pressure > D().greenHi + 0.04f) G.boat.Bleed(dt);
            if (G.boat.bilge > 300) G.boat.Pump(D().pumpKgPerStroke * dt / D().strokeTime);
        }
        if (me.dead || me.overboard) { G.boat.telegraph = 0; return; }
        // (further out, leave earlier: a game minute a second at ~3.5 m/s, half an hour in hand; on a ground with kelp
        // the way home bends round the canopy and a fouled screw costs half a minute, so budget 2.6 m/s and 45 minutes)
        bool kelpy = E.ground == "weeds";
        float leaveAt = std::min(P.leaveAt, 540 - (kelpy ? 45 : 30) - Vector2Distance(G.boat.pos, S.harbour) / (kelpy ? 2.6f : 3.5f));
        // (the Grotto: out through the arch before it closes, with twenty minutes in hand)
        if (S.archCloseAt >= 0 && G.boat.pos.x > E.archX0) leaveAt = std::min(leaveAt, S.archCloseAt - 20 - Vector2Distance(G.boat.pos, {E.archX0, E.archY}) / 3.0f);
        if (S.phase == Phase::Night && S.clock > leaveAt - (G.net.state == NetState::Down ? HAUL_AHEAD : 0) && !homeward) { homeward = true; leftAt = S.clock; G.netLast = true; G.Say("The skipper turns for home"); }
        // Canoe night: the careful skipper pays, the greedy one trades fish it can spare, the reckless one refuses
        if (S.canoe == CanoeState::Alongside) S.Canoe(S.variant == Variant::MermenMarket ? CANOE_TRADE : P.charges ? CANOE_REFUSE : P.chum ? CANOE_TRADE : CANOE_TRIBUTE);   // (the mermen's abalone are worth more than the fish)
        if (G.boat.sunk) return;
        // mermen at the net (the Weeds): after 5 s of splashing a hand at the stern looses a spear at the cod end (a
        // flare if the spears are gone; the sim's shorthand for walking aft with the speargun)
        if (G.mermen.on && G.mermen.t > 5 && G.mermen.t < 5 + dt * 1.5f) {
            bool spear = false;
            for (auto& sl : me.slots) if (sl.it == Item::Speargun && sl.ammo > 0) { sl.ammo--; spear = true; break; }
            if (spear) { Projectile s; s.kind = Shot::Spear; s.p = {G.mermen.p.x + 1, G.mermen.p.y, 0}; s.life = 0.2f; G.shots.push_back(s); }
            else if (G.ammoFlares > 0) { G.ammoFlares--; G.flares.push_back({G.mermen.p, 30}); }
        }
        // Atlantis: the bell against the Deep Choir (the skipper rings it from the wheelhouse); against the Kraken, the
        // net cut away, the lantern out and full steam for home
        if (G.choir.on && G.choir.t > 3 && G.bellT > 15) { G.bellT = 0; G.bellRings++; G.Say("The skipper rings the bell"); }
        if (G.kraken.state == 2) {
            if (G.net.state == NetState::Down || G.net.state == NetState::Snagged || G.net.state == NetState::Hauling) { G.net.state = NetState::Lost; G.net.catchKg.clear(); G.net.load = 0; G.Say("The skipper cuts the net away"); }
            G.boat.lantern = 0; AtHelm(); G.boat.telegraph = 3; SteerTo(G.moorPos, 0); G.boat.telegraph = 3;
            return;
        }
        // kelp round the screw (the Weeds): stop her and send a hand over the stern to cut it free (half a minute)
        if (G.screwFouled) { fouledT += dt; G.boat.telegraph = 0; if (fouledT > 30 && G.boat.shaft < 0.05f) { G.screwFouled = false; fouledT = 0; } return; }
        // aground: back off for a few seconds with the helm over, then try again
        if (G.boat.aground && groundT <= 0) {
            groundT = 8; groundSide = R() < 0.5f ? -1 : 1;
            // a third grounding on the way to the same mark: give it up for another
            if (++groundN >= 3 && spots.size() > 1) { groundN = 0; for (int k = 1; k < (int)spots.size(); k++) { int j = (spotI + k) % (int)spots.size(); if (RouteClear(G.boat.pos, spots[j].p)) { spotI = j; break; } } onSpot = false; }
        }
        if (groundT > 0) { groundT -= dt; AtHelm(); G.boat.telegraph = -1; G.boat.rudder = (float)groundSide; return; }
        // a depth charge over the side when a shark has come (the reckless pattern)
        if (P.charges && chargeNow) {
            int slot = -1; for (int k = 0; k < 4; k++) if (me.slots[k].it == Item::Charge && me.slots[k].ammo > 0) slot = k;
            if (slot >= 0) { me.station = -1; me.p = {-6, 1.5f}; me.sel = slot; G.UseItem(0, {-6, 14}, true, false, false, dt); }
            chargeNow = false;
        }
        if (homeward) {
            AtHelm();
            // the net comes in first (the winch hand hauls once told to), then home
            bool shortHanded = G.crew.size() < 3;   // (one or two hands: the skipper hauls it himself, as when towing)
            if (!shortHanded && G.net.state != NetState::Stowed && G.net.state != NetState::Lost && (netHand < 0 || G.crew[netHand].dead || G.crew[netHand].overboard)) netHand = G.OrderBot(winch);
            if (G.net.state == NetState::Down || G.net.state == NetState::Hauling) {
                if (shortHanded) { me.station = winch; me.p = Stations()[winch].at; G.NetInput(0, true, false, dt); }
                else if (netHand >= 0) G.NetInput(netHand, true, false, dt);
            }
            if (G.net.state == NetState::Hauling || G.net.state == NetState::Shooting) { G.boat.telegraph = 1; G.boat.rudder *= powf(0.3f, dt); return; }
            if (G.net.state == NetState::Snagged) { snagT += dt; G.boat.telegraph = -1; if (snagT > 60) { G.net.state = NetState::Lost; snagT = 0; G.Say("The skipper cuts the snagged net away"); } return; }
            if (netHand >= 0) { G.OrderBot(-1); netHand = -1; }
            // home: straight for the quay when that course is clear, else back to the mark first (its course home was checked)
            // (once a night: a course home that still isn't clear from the mark is taken as it is)
            if (!spots.empty() && !viaMark && !viaDone && !RouteClear(G.boat.pos, S.harbour) && Vector2Distance(G.boat.pos, spots[spotI].p) > 15) { viaMark = true; viaDone = true; }
            if (viaMark && (spots.empty() || Vector2Distance(G.boat.pos, spots[spotI].p) < 15)) viaMark = false;
            if (viaMark) SteerTo(spots[spotI].p, 0); else SteerTo(G.moorPos, 25);
            return;
        }
        Vector2 spot = spots[spotI].p;
        if (!onSpot) {
            AtHelm();
            SteerTo(spot, 20);
            if (Vector2Distance(G.boat.pos, spot) <= 12) { onSpot = true; slowT = 0; if (P.chum) { G.ThrowChum(0); chumT = 0; } }
            return;
        }
        // the trawl: with three hands or more and open water to tow in, a hand is sent to the winch (it shoots, fishes
        // the net to a load and hauls, on its own) and the skipper tows slow ahead between the leg's ends
        bool short_ = G.crew.size() < 3;   // one or two hands: the skipper works the winch itself (the helm set, as a real hand would run between them)
        bool canTow = spots[spotI].tow && G.net.state != NetState::Lost;
        if (canTow && netHand < 0 && !short_) { netHand = G.OrderBot(winch); if (netHand < 0) canTow = false; }
        towing = canTow && (short_ || (netHand >= 0 && !G.crew[netHand].dead && !G.crew[netHand].overboard));
        if (towing) {
            AtHelm();
            if (short_) {   // the skipper shoots, fishes the net to a load and hauls it, by the bots' own rule
                bool held = (G.net.state == NetState::Stowed) || G.net.state == NetState::Shooting || G.net.state == NetState::Hauling || (G.net.state == NetState::Down && G.net.load > 180);
                if (held) { me.station = winch; me.p = Stations()[winch].at; G.NetInput(0, true, false, dt); }
            }
            bool snagNow = G.net.state == NetState::Snagged;
            if (snagNow && !wasSnag) snagN++;
            wasSnag = snagNow;
            if (G.net.state == NetState::Snagged) { snagT += dt; G.boat.telegraph = -1; G.boat.rudder = 0; if (snagT > 60) { G.net.state = NetState::Lost; snagT = 0; G.Say("The skipper cuts the snagged net away"); } return; }
            // a leg that keeps snagging: haul in, and fish the mark from the rods instead
            if (snagN >= 2) {
                if (G.net.state == NetState::Down || G.net.state == NetState::Hauling) {
                    if (short_) { me.station = winch; me.p = Stations()[winch].at; G.NetInput(0, true, false, dt); }
                    else if (netHand >= 0) G.NetInput(netHand, true, false, dt);
                    G.boat.telegraph = 1; G.boat.rudder *= powf(0.3f, dt); return;
                }
                if (G.net.state == NetState::Stowed) {
                    spots[spotI].tow = false; snagN = 0; if (netHand >= 0) { G.OrderBot(-1); netHand = -1; }
                    // (the Weeds: off to the kelp's edge to fish it on the rods)
                    int best = -1; float bd = 1e9f;
                    for (int k = 0; k < (int)spots.size(); k++) if (spots[k].edge && RouteClear(G.boat.pos, spots[k].p) && Vector2Distance(G.boat.pos, spots[k].p) < bd) { bd = Vector2Distance(G.boat.pos, spots[k].p); best = k; }
                    if (best >= 0) { spotI = best; onSpot = false; G.Say("Foul ground: the skipper takes her to the kelp's edge for the rods"); }
                    else G.Say("Foul ground: the skipper fishes the mark on the rods");
                    return;
                }
            }
            Vector2 leg = towLeg ? spots[spotI].b : spots[spotI].a;
            if (Vector2Distance(G.boat.pos, leg) < 10) towLeg = !towLeg;
            SteerTo(leg, 0); G.boat.telegraph = G.net.state == NetState::Hauling ? 1 : 1;
            if (Vector2Distance(G.boat.pos, spot) > 90) { onSpot = false; }
            if (G.hold.size() > h0 || G.net.load > lastLoad + 0.5f) slowT = 0; else slowT += dt;
            lastLoad = G.net.load;
            h0 = G.hold.size();
            if (P.chum) { chumT += dt; if (chumT > 180 && G.chum > 0) { G.ThrowChum(0); chumT = 0; } }
            if (slowT > P.restless * 2 && spots.size() > 1 && G.net.state == NetState::Stowed) { G.OrderBot(-1); netHand = -1; for (int k = 1; k < (int)spots.size(); k++) { int j = (spotI + k) % (int)spots.size(); if (RouteClear(G.boat.pos, spots[j].p)) { spotI = j; break; } } onSpot = false; slowT = 0; }
            return;
        }
        if (Vector2Distance(G.boat.pos, spot) > 35) { onSpot = false; return; }   // drifted off: steam back
        // on the spot: engines stopped; fish where she lies
        G.boat.telegraph = 0; G.boat.rudder *= powf(0.3f, dt);
        if (P.chum) { chumT += dt; if (chumT > 180 && G.chum > 0) { G.ThrowChum(0); chumT = 0; } }
        if (G.hold.size() > h0) slowT = 0; else slowT += dt;
        h0 = G.hold.size();
        if (slowT > P.restless && spots.size() > 1) {
            for (int k = 1; k < (int)spots.size(); k++) { int j = (spotI + k) % (int)spots.size(); if (RouteClear(G.boat.pos, spots[j].p)) { spotI = j; break; } }
            onSpot = false; slowT = 0; G.Say("Nothing doing: the skipper tries another mark"); return;
        }
        if (!solo) { AtHelm(); return; }
        // solo: the skipper works the port rod and the table itself (between stations by magic; the rest of the sim is real)
        Rod& r = G.rods[G.RodAt(portRod)];
        r.botAngler = true;
        if (G.DeckFish() > 0 && r.state == RodState::Idle) { me.p = Stations()[gut].at; me.station = gut; G.Primary(0, true, dt); }
        else {
            me.p = Stations()[portRod].at; me.station = portRod;
            bool cast = r.state == RodState::Idle && fmodf(t, 2.0f) < 0.9f;
            Vector2 aim = Vector2Add(r.TipDeck(), {1, -9});
            G.RodInput(0, cast, aim, false, r.bite.stage == BiteStage::Take, 0, false, false, 0);
        }
    }
};

const char* Clock(float minutes) { static char b[16]; int m = (int)minutes; snprintf(b, sizeof b, "%02d:%02d", (20 + m / 60) % 24, m % 60); return b; }

} // namespace

int RunTrawlSim(int argc, char** argv) {
    std::string ground = argc >= 3 ? argv[2] : "lagoon";
    int nights = argc >= 4 ? std::max(1, atoi(argv[3])) : 3;
    int crew = argc >= 5 ? std::clamp(atoi(argv[4]), 1, 6) : 6;
    std::string pat = argc >= 6 ? argv[5] : "careful";
    int runs = argc >= 7 ? std::max(1, atoi(argv[6])) : 8;
    std::string skillS = argc >= 8 ? argv[7] : "able";
    const SkipperPattern* P = &PATTERNS[0];
    for (const auto& p : PATTERNS) if (pat == p.name) P = &p;
    Skill skill = skillS == "green" ? Skill::Green : skillS == "oldhand" || skillS == "old" ? Skill::OldHand : Skill::Able;
    if (!Species().ok) { printf("species: %s\n", Species().why.c_str()); return 1; }
    if (!Species().grounds.count(ground)) { printf("no such ground: %s (lagoon)\n", ground.c_str()); return 1; }
    bool trace = getenv("DEPTH_TRACE") != nullptr;
    printf("The Trawl: %d run(s) of %d night(s) on the %s, %d hand(s), the %s skipper, %s bots\n", runs, nights, Species().grounds.at(ground).name.c_str(), crew, P->name, SkillOf(skill).name);

    std::vector<Night> all;
    int deadlines = 0, metN = 0, firstMet = 0, firstN = 0, over = 0;
    double wall = 0;
    const float dt = 1 / 60.0f;
    for (int run = 0; run < runs; run++) {
        auto t0 = std::chrono::steady_clock::now();
        Gannet G; Eco E; Session S;
        S.ground = ground;
        S.Begin(G, E, crew, 1000 + run * 7919);
        G.botsOn = crew > 1; G.botSkill = skill;
        Skipper K(G, S, E, *P, 1000 + run);
        int done = 0;
        bool ended = false;
        while (done < nights && !ended) {
            Night N;
            K.Shop();
            std::string why;
            G.crew[0].p = {-1, 0.8f}; G.crew[0].station = -1;
            for (auto& c : G.crew) if (c.deck == 0 && c.p.y < -3) c.p.y = 0.8f;
            if (!S.CastOff(&why)) { if (trace) printf("  run %d night %d: cast off refused: %s\n", run, done + 1, why.c_str()); over++; ended = true; break; }
            K.Begin();
            std::vector<bool> wasDead(G.crew.size(), false), wasOver(G.crew.size(), false);
            size_t arrivalsSeen = 0; int rams0 = 0; float t = 0;
            int firstOver = 0;
            while (S.phase == Phase::SailOut || S.phase == Phase::Night) {
                K.Step(dt);
                G.Step(dt); S.Step(dt);
                t += dt;
                for (size_t i = 0; i < G.crew.size(); i++) {
                    if (G.crew[i].dead && !wasDead[i]) { N.deaths.push_back(G.crew[i].cause); wasDead[i] = true; }
                    if (G.crew[i].overboard && !wasOver[i]) { N.overboard++; wasOver[i] = true; }
                    if (!G.crew[i].overboard) wasOver[i] = false;
                }
                while (arrivalsSeen < E.arrivals.size()) { N.arrivals.push_back({E.arrivals[arrivalsSeen].species, E.arrivals[arrivalsSeen].t / 60.0f}); if (P->charges && E.arrivals[arrivalsSeen].species.find("shark") != std::string::npos) K.chargeNow = true; arrivalsSeen++; }
                for (const auto& l : G.log) if (l.find("struck the hull") != std::string::npos) rams0++;
                if (trace) for (const auto& l : G.log) if (l.find("Siren") != std::string::npos || l.find("net") != std::string::npos || l.find("kelp") != std::string::npos || l.find("screw") != std::string::npos || l.find("cod end") != std::string::npos || l.find("singing") != std::string::npos || l.find("rocks") != std::string::npos) printf("      [%s] %s\n", S.ClockText().c_str(), l.c_str());
                G.log.clear();
                if (S.phase == Phase::Night && S.clock > 420) for (const auto& a : E.agents) { const auto& r = Species().sp[a.sp]; if (a.alive && r.threat && r.size >= 3 && Vector2Distance({a.p.x, a.p.y}, G.boat.pos) < 40) N.lateThreat = true; }
                if (G.boat.sunk) N.sunk = true;
                if (trace && fmodf(t, 60) < dt) printf("    run %d night %d  t%4.0f %s %s pos (%.0f,%.0f) spot %d d %.0f tel %d p %.2f bilge %.0f hold %d wake %.0f net %d/%.0fkg tow %d hand %d foul %d shaft %.2f aground %d spd %.1f\n", run, done + 1, t, PhaseName(S.phase), S.ClockText().c_str(), G.boat.pos.x, G.boat.pos.y, K.spotI, Vector2Distance(G.boat.pos, K.spots[K.spotI].p), G.boat.telegraph, G.boat.pressure, G.boat.bilge, (int)G.hold.size(), E.wake, (int)G.net.state, G.net.load, (int)K.towing, K.netHand, (int)G.screwFouled, G.boat.shaft, (int)G.boat.aground, G.boat.Speed());
                if (trace && fmodf(t, 20) < dt && K.netHand >= 0 && K.netHand < (int)G.crew.size()) {
                    const Crew& nh = G.crew[K.netHand]; const auto& nb = G.brains[K.netHand];
                    printf("      winch hand: station %d (%.1f,%.1f) fallen %d ink %.1f inj %d task %d order %d goal %d doing '%s'  net %d t %.1f\n", nh.station, nh.p.x, nh.p.y, nh.fallen, nh.inkT, nh.injuries, nb.task, nb.order, nb.goal, G.BotDoing(K.netHand).c_str(), (int)G.net.state, G.net.t);
                }
                if (t > 900) { if (trace) printf("    (night cut at 900 s: %s)\n", PhaseName(S.phase)); break; }
            }
            (void)firstOver;
            N.rams = rams0;
            N.seaMin = t / 60.0f;
            N.leftAt = K.leftAt; N.variant = S.variant; N.canoeWord = S.canoeWord;
            N.customs = !S.tape.empty() && S.tape.back().find("SEIZED") != std::string::npos;
            // home: the hands gut what's on deck, then the Fish Market
            for (int k = 0; k < 60 * 45 && G.DeckFish() > 0; k++) { if (crew == 1) { G.crew[0].station = K.gut; G.crew[0].p = Stations()[K.gut].at; G.Primary(0, true, dt); } G.Step(dt); }
            G.crew[0].station = -1;
            int landed = (int)G.hold.size();
            // the doc's "decision every night": deliver enough to the Owners' scales to stay on pace for the quota
            // (a third of it a night, and a tenth over), the best fish first; sell the rest at the market for the purse
            {
                float pace = S.quota * std::max(1, S.night) / 3.0f * 1.1f;   // (S.night already counts tonight once she's moored)
                std::sort(G.hold.begin(), G.hold.end(), [&](const CatchRec& a, const CatchRec& b) { return S.QuotaValue(a) > S.QuotaValue(b); });
                float deliveredNow = 0; std::vector<SaleLine> del;
                // ...but keep enough back for the market that tomorrow's bait, ice and coal are paid (not on the last night)
                float marketLeft = 0; for (const auto& c : G.hold) marketLeft += S.Value(c);
                bool lastNight = S.night >= 3;
                while (!G.hold.empty() && S.sold < pace && S.QuotaValue(G.hold.front()) > 0) {
                    if (!lastNight && S.money + marketLeft - S.Value(G.hold.front()) < 120) break;
                    marketLeft -= S.Value(G.hold.front());
                    deliveredNow += S.Deliver(0);
                    del.insert(del.end(), S.lastDelivery.begin(), S.lastDelivery.end());
                }
                S.Sell();
                for (const auto& l : del) N.money[l.src < 0 || l.src >= CS_COUNT ? 0 : l.src] += l.value;
                for (const auto& l : S.lastSale) N.money[l.src < 0 || l.src >= CS_COUNT ? 0 : l.src] += l.value;
                N.sold = S.lastSaleTotal + deliveredNow; N.landed = landed;
            }
            if (trace) {
                std::map<std::string, std::pair<float, float>> by;
                for (const auto& l : S.lastSale) { auto& e = by[l.name]; e.first += l.kg; e.second += l.value; }
                std::vector<std::pair<std::string, std::pair<float, float>>> v(by.begin(), by.end());
                std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.second.second > b.second.second; });
                printf("    sold:"); for (size_t i = 0; i < v.size() && i < 7; i++) printf("  %s %.1f kg %.1f", v[i].first.c_str(), v[i].second.first, v[i].second.second); printf("\n");
            }
            if (trace) printf("  run %d night %d: %s, %d fish sold for %.0f (hook %.0f net %.0f gun %.0f set %.0f), %d death(s), %d overboard, %d ram(s), threats: %d, %.1f min at sea%s%s\n", run, done + 1, S.ClockText().c_str(), landed, N.sold, N.money[0], N.money[1], N.money[2], N.money[3], (int)N.deaths.size(), N.overboard, N.rams, (int)N.arrivals.size(), N.seaMin, N.customs ? ", SEIZED" : "", N.sunk ? ", SUNK" : "");
            all.push_back(N);
            done++;
            if (S.night >= 3) {
                S.Count(); deadlines++;
                if (S.deadline == 1) { firstN++; if (S.met) firstMet++; }
                if (S.met) { metN++; if (done < nights) S.Continue(); }
                else { over++; ended = true; }
            }
        }
        wall += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    }
    // ---- the report
    int n = (int)all.size();
    if (n == 0) { printf("no nights played\n"); return 1; }
    float sold = 0, src[CS_COUNT] = {}, sea = 0; int landed = 0, deaths = 0, customs = 0, sunk = 0, late = 0, rams = 0, overb = 0;
    std::map<std::string, int> cause;
    std::map<std::string, std::vector<float>> first;
    for (const auto& N : all) {
        sold += N.sold; landed += N.landed; sea += N.seaMin; customs += N.customs; sunk += N.sunk; late += N.lateThreat; rams += N.rams; overb += N.overboard;
        for (int s = 0; s < CS_COUNT; s++) src[s] += N.money[s];
        deaths += (int)N.deaths.size();
        for (const auto& d : N.deaths) cause[d]++;
        std::map<std::string, float> f;
        for (const auto& a : N.arrivals) if (!f.count(a.first)) f[a.first] = a.second;
        for (const auto& kv : f) first[kv.first].push_back(kv.second);
    }
    printf("\nNights played: %d (%d deadline(s) counted, %d run(s) ended early)\n", n, deadlines, over);
    if (deadlines) printf("Quota met: %d of %d deadlines (%.0f%%); first deadline %d of %d (%.0f%%)   [doc: Lagoon 85%%, Weeds 65%%, Grotto 45%%, Atlantis 25%%]\n", metN, deadlines, 100.0f * metN / deadlines, firstMet, std::max(1, firstN), 100.0f * firstMet / std::max(1, firstN));
    printf("Money per night: %.0f shillings from %.1f fish   by source: hook %.0f%%  net %.0f%%  gun %.0f%%  set gear %.0f%%  dive %.0f%%\n", sold / n, (float)landed / n,
           sold > 0 ? 100 * src[0] / sold : 0, sold > 0 ? 100 * src[1] / sold : 0, sold > 0 ? 100 * src[2] / sold : 0, sold > 0 ? 100 * src[3] / sold : 0, sold > 0 ? 100 * src[4] / sold : 0);
    printf("Deaths per night: %.2f   [doc: Lagoon 0.3, Atlantis 2.5]   overboard %.2f/night, hull rams %.2f/night, seized %d, sunk %d\n", (float)deaths / n, (float)overb / n, (float)rams / n, customs, sunk);
    if (!cause.empty()) { printf("  by cause:"); for (const auto& kv : cause) printf("  %s x%d", kv.first.c_str(), kv.second); printf("\n"); }
    printf("Threats (nights seen, first sighting at the lantern's edge):\n");
    if (first.empty()) printf("  none came within 25 m\n");
    for (const auto& kv : first) {
        float mean = 0, lo = 1e9f; for (float v : kv.second) { mean += v; lo = std::min(lo, v); }
        mean /= kv.second.size();
        printf("  %-16s %3d of %d nights   mean %s   earliest %s\n", kv.first.c_str(), (int)kv.second.size(), n, Clock(mean), Clock(lo));
    }
    printf("A large threat within 40 m after 03:00: %d of %d nights (%.0f%%)   [doc Stir clock: about half of ordinary nights]\n", late, n, 100.0f * late / n);
    {
        float leftSum = 0; int leftN = 0; std::map<std::string, int> vars; std::map<std::string, int> canoes;
        for (const auto& N : all) { if (N.leftAt >= 0) { leftSum += N.leftAt; leftN++; } vars[VariantName(N.variant)]++; if (!N.canoeWord.empty()) canoes[N.canoeWord.substr(0, N.canoeWord.find(' '))]++; }
        printf("Turned for home at %s on average.  Nights:", leftN ? Clock(leftSum / leftN) : "--:--");
        for (const auto& kv : vars) printf("  %s x%d", kv.first.c_str(), kv.second);
        if (!canoes.empty()) { printf("   canoes:"); for (const auto& kv : canoes) printf("  %s x%d", kv.first.c_str(), kv.second); }
        printf("\n");
    }
    printf("Time: %.1f min at sea per night (%.1f min per deadline with the dock)   %.1f s of CPU per night\n", sea / n, 3 * (sea / n + 2.5f), wall / n);
    return 0;
}

} // namespace tw
