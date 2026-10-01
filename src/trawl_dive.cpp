// Diving, the deck's side and the diver's moves (design doc v2, "Diving and salvage", page 55). The wrecks are
// trawl_wreck.cpp's. Design calls where the doc leaves numbers open:
//  - Going down: a hand with the hardhat suit at the stern (x < -10), she lying still (under 0.4 m/s) within 15 m of a
//    wreck that a hardhat reaches (a bell wreck needs the diving bell: not yet built). The descent takes the wreck's
//    depth at 1 m/s; the diver arrives in its first breach's room.
//  - Air: the pump's gauge falls 0.08 a second and each stroke at the air pump adds 0.12 (strokes closer than 0.3 s
//    apart are lost: it's a rhythm). Out of the green (below 0.4) the diver breathes the helmet's 30 s; back in the
//    green it refills at 3 s a second. At 0 they drown (body lost with the wreck).
//  - The hose: she drifting more than 15 m from the wreck fouls it (the air cut, as an empty gauge).
//  - Salvage: an item lifted in the room (both hands; a locked cabin opens to a crowbar in the diver's slots, or a
//    Diver's own Wreck Rat skill - the Diver role); carried to a breach room it goes into the basket and up its own line
//    (into the hold when she's aboard: junk, CS_DIVE, its value flat). Cursed idols raise the Wake by 10.
//  - Recall: two tugs (or the deck) and the winch hauls at 1 m/s; a diver brought up faster (ascentRate > 1.2) has
//    the bends (a burn-like injury: slowed for the night).
#include "trawl.h"
#include "trawl_eco.h"
#include "trawl_wreck.h"
#include "trawl_session.h"
#include "trawl_weapons.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tw {

int Gannet::WreckNear(float r) const {
    if (!wrecks) return -1;
    int best = -1; float bd = r;
    for (int i = 0; i < (int)wrecks->size(); i++) { float d = Vector2Distance(boat.pos, {(*wrecks)[i].x, (*wrecks)[i].y}); if (d < bd) { bd = d; best = i; } }
    return best;
}

bool Gannet::StartDive(int ci) {
    Crew& c = crew[ci];
    if (dive.diver >= 0 || !hardhat || c.dead || c.overboard || c.deck != 0 || c.p.x > -10 || boat.Speed() > 0.4f) return false;
    int w = WreckNear(15);
    if (w < 0) { Say("No wreck under her here: lie still within 15 m of one (the sonar shows them)"); return false; }
    if ((*wrecks)[w].bell) { Say("Too deep for a hardhat: that wreck wants the diving bell"); return false; }
    dive = DiveState{}; dive.diver = ci; dive.wreck = w; dive.room = -1; dive.depth = 0; dive.air = 30; dive.gauge = 0.7f;
    c.station = -1; c.deck = DECK_DIVE;
    Say(TextFormat("Over the side in the hardhat: down to the %s, %.0f m", WreckTypeName((*wrecks)[w].type), (*wrecks)[w].depth));
    return true;
}

void Gannet::DivePump(int ci, bool stroke) {
    if (dive.diver < 0 || !stroke) return;
    const Crew& c = crew[ci];
    if (c.station < 0 || Stations()[c.station].kind != StationKind::AirPump) return;
    if (dive.pumpT < 0.5f) return;   // (a rhythm: a stroke every half second at most)
    dive.pumpT = 0;
    dive.gauge = std::min(1.0f, dive.gauge + 0.1f);
}

bool Gannet::DiveMove(int to) {
    if (dive.diver < 0 || dive.room < 0 || !wrecks) return false;
    const Wreck& w = (*wrecks)[dive.wreck];
    for (const auto& L : w.links) {
        int o = L.a == dive.room ? L.b : L.b == dive.room ? L.a : -1;
        if (o != to) continue;
        if (L.kind == 2 && dive.carrying) { Say("Too tight with salvage in your arms: the squeeze is one diver, empty-handed"); return false; }
        dive.room = to;
        return true;
    }
    return false;
}

bool Gannet::DiveTake() {
    if (dive.diver < 0 || dive.room < 0 || dive.carrying || !wrecks) return false;
    Wreck& w = (*wrecks)[dive.wreck];
    WreckRoom& R = w.rooms[dive.room];
    if (R.locked) {
        const Crew& c = crew[dive.diver];
        bool bar = false; for (const auto& s : c.slots) if (s.it == Item::Weapon && s.wpn >= 0 && Weapons()[s.wpn].id == "crowbar") bar = true;
        if (!bar && c.role != Role::Diver) { Say("A locked cabin: a crowbar, or a Diver's knack with old locks"); return false; }
        R.locked = false; Say("The cabin door gives");
    }
    for (int i = 0; i < (int)w.salvage.size(); i++) if (w.salvage[i].room == dive.room && !w.salvage[i].taken) {
        w.salvage[i].taken = true; dive.carrying = true; dive.item = i;
        Say(TextFormat("In your arms: %s (%.0f kg)", w.salvage[i].name.c_str(), w.salvage[i].kg));
        return true;
    }
    Say("Nothing more worth lifting in here");
    return false;
}

bool Gannet::DiveBasket() {
    if (dive.diver < 0 || !dive.carrying || !wrecks) return false;
    Wreck& w = (*wrecks)[dive.wreck];
    if (std::find(w.entries.begin(), w.entries.end(), dive.room) == w.entries.end()) { Say("Carry it to a breach: the basket comes down there"); return false; }
    const SalvageItem& s = w.salvage[dive.item];
    CatchRec r; r.name = s.name; r.junk = true; r.kg = s.kg; r.price = s.value; r.dead = r.gutted = r.iced = true; r.src = CS_DIVE; r.deckAt = {-9.5f, 0.5f};
    hold.push_back(r);
    if (s.cursed && eco) { eco->wake = std::min(100.0f, eco->wake + 10); eyeBlinkT = 0.6f; Say("The idol comes over the rail and the water goes very still: the Wake rises"); }
    dive.carrying = false; dive.item = -1;
    Say(TextFormat("Up in the basket: %s", r.name.c_str()));
    return true;
}

void Gannet::DiveRecall() { if (dive.diver >= 0 && !dive.recall) { dive.recall = true; Say("Two sharp tugs on the line: haul away"); } }

void Gannet::StepDive(float dt) {
    if (dive.diver < 0) return;
    Crew& c = crew[dive.diver];
    if (c.dead) { dive = DiveState{}; return; }
    if (!wrecks || dive.wreck < 0 || dive.wreck >= (int)wrecks->size()) { c.deck = 0; dive = DiveState{}; return; }
    const Wreck& w = (*wrecks)[dive.wreck];
    dive.pumpT += dt;
    // the pump's gauge, and the hose fouled if she drifts off the wreck
    bool fouled = Vector2Distance(boat.pos, {w.x, w.y}) > 15;
    dive.gauge = fouled ? 0 : std::max(0.0f, dive.gauge - 0.08f * dt);
    bool green = dive.gauge >= 0.4f;
    dive.air = green ? std::min(30.0f, dive.air + 3 * dt) : dive.air - dt;
    if (dive.air <= 0) {
        int k = dive.diver; dive = DiveState{};
        crew[k].deck = 0;
        Kill(k, fouled ? "the air hose fouled" : "the air ran out in the helmet", true);
        return;
    }
    if (dive.recall) {
        dive.room = -1;
        dive.depth -= dive.ascentRate * dt;
        if (dive.depth <= 0) {
            int k = dive.diver; bool bends = dive.ascentRate > 1.2f;
            dive = DiveState{};
            crew[k].deck = 0; crew[k].p = {-10.4f, 0.6f}; crew[k].v = {0, 0};
            if (bends) Injure(k, INJ_BURN, "the bends (brought up too fast)");
            Say(bends ? "The diver comes up too fast: the bends" : "The diver is hauled aboard, streaming");
        }
        return;
    }
    if (dive.room < 0) {   // going down the line
        dive.depth += 1.0f * dt;
        if (dive.depth >= w.depth) { dive.depth = w.depth; dive.room = w.entries.empty() ? 0 : w.entries[0]; Say(TextFormat("On the wreck: in through the breach, into the %s", w.rooms[dive.room].kind.c_str())); }
    }
}

// ---------------------------------------------------------------- depth.exe --trawl-dive-test
int RunTrawlDiveTest() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Trawl: diving (the deck's side)\n");
    const float dt = 1 / 60.0f;
    std::vector<Wreck> ws = GroundWrecks("weeds", 9);
    Wreck* sl = nullptr; for (auto& w : ws) if (!w.bell && (!sl || w.Rooms() > sl->Rooms())) sl = &w;
    check(sl != nullptr, "the Weeds have a wreck a hardhat reaches");
    if (!sl) return 1;
    sl->x = 300; sl->y = 300;
    auto setup = [&](Gannet& g, int n) { g.Init(n, 5); g.wrecks = &ws; g.hardhat = true; g.boat.pos = {305, 300}; g.boat.vel = {0, 0}; g.boat.telegraph = 0; g.crew[0].p = {-10.5f, 0}; g.crew[0].role = Role::Diver; };
    int pump = -1; for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == StationKind::AirPump) pump = i;
    {
        Gannet g; setup(g, 2);
        g.crew[1].p = Stations()[pump].at; g.crew[1].station = pump;
        check(g.StartDive(0) && g.crew[0].deck == DECK_DIVE, "with the suit, at the stern, still over a wreck: over the side");
        float pT = 0;
        for (int i = 0; i < 60 * ((int)sl->depth + 2); i++) { pT += dt; if (pT > 0.6f) { pT = 0; g.DivePump(1, true); } g.boat.vel = {0, 0}; g.StepDive(dt); }
        check(g.dive.room >= 0 && fabsf(g.dive.depth - sl->depth) < 0.1f, TextFormat("down the line at 1 m/s to %.0f m, in through the breach", sl->depth));
        check(g.dive.air >= 29 && g.dive.gauge >= 0.4f, "a hand stroking the pump in rhythm keeps the gauge in the green");
        // walk to a room with salvage, lift it, carry it back to the breach, into the basket
        int target = -1; for (const auto& s : sl->salvage) if (!sl->rooms[s.room].locked) { target = s.room; break; }
        std::vector<int> path;   // BFS over links
        { std::vector<int> prev(sl->Rooms(), -2); std::vector<int> q{g.dive.room}; prev[g.dive.room] = -1;
          for (size_t h = 0; h < q.size(); h++) for (const auto& L : sl->links) { int o = L.a == q[h] ? L.b : L.b == q[h] ? L.a : -1; if (o >= 0 && prev[o] == -2 && L.kind != 2) { prev[o] = q[h]; q.push_back(o); } }
          for (int r = target; r >= 0 && r != g.dive.room; r = prev[r]) path.insert(path.begin(), r); }
        int start = g.dive.room; bool walked = true; for (int r : path) walked &= g.DiveMove(r);
        bool took = g.DiveTake();
        std::vector<int> back(path.rbegin(), path.rend()); back.erase(back.begin()); back.push_back(start);
        if (path.empty()) back.clear();
        for (int r : back) walked &= g.DiveMove(r);
        size_t h0 = g.hold.size(); bool up = g.DiveBasket();
        check(walked && took && up && g.hold.size() == h0 + 1 && g.hold.back().src == CS_DIVE, "room to room to the salvage, lifted, carried back to the breach and up in the basket");
        g.DiveRecall(); for (int i = 0; i < 60 * ((int)sl->depth + 2); i++) { pT += dt; if (pT > 0.6f) { pT = 0; g.DivePump(1, true); } g.StepDive(dt); }
        check(g.dive.diver < 0 && g.crew[0].deck == 0 && !g.crew[0].Has(INJ_BURN), "two tugs: hauled up at 1 m/s, no bends");
    }
    {
        Gannet g; setup(g, 1);
        g.StartDive(0);
        for (int i = 0; i < 60 * 80 && !g.crew[0].dead; i++) { g.boat.vel = {0, 0}; g.StepDive(dt); }
        check(g.crew[0].dead, "nobody at the pump: the gauge falls out of the green, 30 s in the helmet, and the diver drowns");
        Gannet h; setup(h, 2); h.crew[1].p = Stations()[pump].at; h.crew[1].station = pump;
        h.StartDive(0); for (int i = 0; i < 60 * 3; i++) { h.DivePump(1, true); h.StepDive(dt); }
        float g0 = h.dive.gauge; for (int i = 0; i < 60; i++) { h.DivePump(1, true); h.StepDive(dt); }
        check(h.dive.gauge < g0 + 0.5f, "pumping without a rhythm (every frame) doesn't fill the gauge any faster");
        h.boat.pos = {340, 300};
        for (int i = 0; i < 60 * 35 && !h.crew[0].dead; i++) { h.DivePump(1, true); h.StepDive(dt); }
        check(h.crew[0].dead, "she drifts 35 m off the wreck: the hose fouls and the air is cut");
        Gannet b; setup(b, 2); b.crew[1].p = Stations()[pump].at; b.crew[1].station = pump; b.StartDive(0);
        float pT = 0; for (int i = 0; i < 60 * ((int)sl->depth + 2); i++) { pT += dt; if (pT > 0.6f) { pT = 0; b.DivePump(1, true); } b.StepDive(dt); }
        b.dive.ascentRate = 3; b.DiveRecall(); for (int i = 0; i < 60 * 30; i++) { pT += dt; if (pT > 0.6f) { pT = 0; b.DivePump(1, true); } b.StepDive(dt); }
        check(b.crew[0].Has(INJ_BURN), "winched up three times too fast: the bends");
        Gannet n; setup(n, 1); n.hardhat = false;
        check(!n.StartDive(0), "no suit, no dive");
    }
    printf(fails ? "trawl-dive-test: %d FAILED\n" : "trawl-dive-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}

} // namespace tw
