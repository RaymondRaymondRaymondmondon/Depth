// Below decks (design doc v2, "Below decks"): the Gannet's small, dark interior. The engine room (down the aft
// companionway), the fish hold (the main hatch, and the watertight door from the engine room) and the fo'c'sle (the
// fore hatch: the bunks, the magazine locker where the ammunition lives, the Medic's cot). Hatches are open (fast, but
// they let water in), shut (4 s to open) or battened (10 s, and they can't be opened from below). An oil lamp in each
// space goes out past 20 deg of roll and takes 3 s to relight. When the bilge is half full the eels come in with the
// water; a fire from a blown relief valve reaches the coal bunker in 30 s unless someone smothers it. Every trip below
// takes a hand out of the light and out of earshot.
#include "trawl.h"
#include "trawl_eco.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tw {

namespace {
const Vector2 DOOR{-2.9f, 0};          // the watertight door (deck 1)
const Vector2 FIRE{-4.8f, -0.9f};      // the boiler's firebox: where a blown valve starts a fire
const float WORK_SMOTHER = 3, WORK_OPEN = 4, WORK_BATTEN = 10, WORK_LAMP = 3, COT_S = 10, HOOK_ON_S = 25;
enum { W_SMOTHER = 200, W_HATCH = 300, W_BATTEN = 310, W_LAMP = 400, W_HOOK = 600 };
uint32_t gBr = 7;
float BRand() { gBr = gBr * 1664525u + 1013904223u; return (gBr >> 8) * (1.0f / 16777216.0f); }
}

void Gannet::InitBelow() {
    hatches[0] = {{-1.0f, -1.0f}, 0};   // the main hatch: down into the hold
    hatches[1] = {{7.6f, 0.0f}, 0};     // the fore hatch: down into the fo'c'sle
    doorOpen = true;
    lamps[0] = {{-6.6f, 1.6f}, true}; lamps[1] = {{-1.0f, 1.5f}, true}; lamps[2] = {{6.4f, 0.9f}, true};
    fireSpread = cotT = hookT = 0;
}

int Gannet::SpaceAt(Vector2 p, int deck) const {
    if (deck != 1) return 0;
    if (p.x > 5.0f) return 3;
    if (p.x >= -2.9f) return 2;
    return 1;
}

bool Gannet::BelowUse(int ci) {
    Crew& c = crew[ci];
    if (c.station >= 0 || c.overboard || c.deck > 1) return false;
    // a hatch: through it if it's open; open it (4 s), or unbatten it from above (10 s)
    for (int h = 0; h < 2; h++) {
        Hatch& H = hatches[h];
        if (Vector2Distance(c.p, H.at) > 0.9f) continue;
        if (H.state == 0) {
            c.deck = 1 - c.deck; c.v = {0, 0};
            Say(c.deck == 1 ? (h == 0 ? "Down the main hatch into the fish hold" : "Down the fore hatch into the fo'c'sle") : "Up through the hatch");
            return true;
        }
        if (H.state == 2 && c.deck == 1) { Say("Battened from above: it won't open from down here"); return true; }
        c.workOn = W_HATCH + h; c.workT = 0;
        Say(H.state == 1 ? "Opening the hatch (4 s)" : "Knocking out the battens (10 s)");
        return true;
    }
    if (c.deck != 1) return false;
    // the watertight door
    if (Vector2Distance(c.p, DOOR) < 1.0f) { doorOpen = !doorOpen; Say(doorOpen ? "The watertight door swings open" : "The watertight door is dogged shut"); return true; }
    // the fire
    if (boat.fireT > 0 && Vector2Distance(c.p, FIRE) < 1.8f) { c.workOn = W_SMOTHER; c.workT = 0; Say("Smothering the fire (3 s)"); return true; }
    // a lamp gone out
    for (int i = 0; i < 3; i++) if (!lamps[i].lit && Vector2Distance(c.p, lamps[i].at) < 1.3f) { c.workOn = W_LAMP + i; c.workT = 0; Say("Striking a match (3 s)"); return true; }
    return false;
}

bool Gannet::HatchCycle(int ci) {
    Crew& c = crew[ci];
    if (c.deck != 0 || c.station >= 0 || c.overboard) return false;
    for (int h = 0; h < 2; h++) {
        Hatch& H = hatches[h];
        if (Vector2Distance(c.p, H.at) > 0.9f) continue;
        if (H.state == 0) { H.state = 1; Say("The hatch is shut"); }
        else if (H.state == 1) { c.workOn = W_BATTEN + h; c.workT = 0; Say("Battening the hatch down (10 s)"); }
        else { c.workOn = W_HATCH + h; c.workT = 0; Say("Knocking out the battens (10 s)"); }
        return true;
    }
    return false;
}

void Gannet::CutScrew(int ci, bool held, float dt) {
    Crew& c = crew[ci];
    if (!screwFouled || !c.overboard || c.dead || Vector2Distance(c.swim, boat.ToWorld({-11.4f, 0})) > 3.0f) return;
    if (!held) { cutT = std::max(0.0f, cutT - dt); return; }
    if (boat.shaft > 0.1f) { Kill(ci, "the screw", true); return; }   // (stop her first)
    cutT += dt;
    if (cutT >= 4) { screwFouled = false; cutT = 0; Say("The kelp is cut away: the screw is clear"); }
}
void Gannet::StepBelow(float dt) {
    float rollDeg = fabsf(boat.RollDeg());
    // work at hand: standing still at the hatch, the fire, the lamp
    for (int k = 0; k < (int)crew.size(); k++) {
        Crew& c = crew[k];
        if (c.workOn < W_SMOTHER || c.workOn >= W_HOOK || c.deck > 1) continue;
        if (c.dead || c.overboard || Vector2Length(c.v) > 0.4f) { c.workOn = -1; c.workT = 0; continue; }
        c.workT += dt;
        if (c.workOn == W_SMOTHER) {
            if (boat.fireT <= 0 || Vector2Distance(c.p, FIRE) > 2.0f) { c.workOn = -1; continue; }
            if (c.workT >= WORK_SMOTHER) { boat.fireT = 0; fireSpread = 0; c.workOn = -1; Say("The fire is out"); }
        } else if (c.workOn >= W_HATCH && c.workOn < W_HATCH + 2) {
            Hatch& H = hatches[c.workOn - W_HATCH];
            if (Vector2Distance(c.p, H.at) > 1.1f) { c.workOn = -1; continue; }
            if (c.workT >= (H.state == 2 ? WORK_BATTEN : WORK_OPEN)) { H.state = H.state == 2 ? 1 : 0; c.workOn = -1; Say(H.state == 0 ? "The hatch is open" : "The battens are out: the hatch is shut"); }
        } else if (c.workOn >= W_BATTEN && c.workOn < W_BATTEN + 2) {
            Hatch& H = hatches[c.workOn - W_BATTEN];
            if (Vector2Distance(c.p, H.at) > 1.1f) { c.workOn = -1; continue; }
            if (c.workT >= WORK_BATTEN) { H.state = 2; c.workOn = -1; Say("The hatch is battened down"); }
        } else if (c.workOn >= W_LAMP && c.workOn < W_LAMP + 3) {
            OilLamp& L = lamps[c.workOn - W_LAMP];
            if (c.workT >= WORK_LAMP) { L.lit = rollDeg < 20; c.workOn = -1; Say(L.lit ? "The lamp is lit" : "The roll blows it out again"); }
        }
    }
    // open hatches ship water when she rolls her rail under
    if (rollDeg > 25) for (const auto& H : hatches) if (H.state == 0) boat.bilge += 30 * dt;
    // the oil lamps go out past 20 deg of roll
    {
        bool any = false;
        if (rollDeg > 20) for (auto& L : lamps) if (L.lit) { L.lit = false; any = true; }
        if (any) { bool below = false; for (const auto& c : crew) if (c.deck == 1 && !c.dead) below = true; if (below) Say("The roll snuffs the lamps below: it's black down there"); }
    }
    // the fire from a blown relief valve: 30 s to the coal bunker unless smothered
    if (boat.fireT > 0) {
        fireSpread += dt;
        boat.fireT = std::max(boat.fireT, 1.0f);   // (it burns until put out)
        for (int k = 0; k < (int)crew.size(); k++) if (crew[k].deck == 1 && !crew[k].dead && Vector2Distance(crew[k].p, FIRE) < 1.2f && BRand() < dt / 6) Injure(k, INJ_BURN, "the fire in the engine room");
        if (fireSpread >= 30) {
            boat.bunker *= 0.5f; boat.Hit(SEC_STERN_P, 15);
            Say("The fire reaches the coal bunker: half the coal goes up and the stern plating buckles");
            boat.fireT = 0; fireSpread = 0;
        }
    } else fireSpread = 0;
    // bilge eels come in with the flooding and bite whoever is below (design doc v2, "What gets in")
    if (boat.bilge > 2000)
        for (int k = 0; k < (int)crew.size(); k++) if (crew[k].deck == 1 && !crew[k].dead && BRand() < dt / 40) Injure(k, INJ_BITE, "a bilge eel");
    // the Medic's cot: a hand lying on it with the Medic beside them for 10 s has one serious injury set
    {
        int patient = -1, medic = -1;
        for (int k = 0; k < (int)crew.size(); k++) {
            const Crew& c = crew[k];
            if (c.station >= 0 && Stations()[c.station].kind == StationKind::Cot && (c.Has(INJ_BROKEN_ARM) || c.Has(INJ_HOOKED_HAND))) patient = k;
        }
        if (patient >= 0) for (int k = 0; k < (int)crew.size(); k++) if (k != patient && crew[k].role == Role::Medic && !crew[k].dead && crew[k].deck == 1 && Vector2Distance(crew[k].p, crew[patient].p) < 2.5f) medic = k;
        if (patient >= 0 && medic >= 0) {
            cotT += dt;
            if (cotT >= COT_S) {
                Crew& p = crew[patient];
                int fix = p.Has(INJ_BROKEN_ARM) ? INJ_BROKEN_ARM : INJ_HOOKED_HAND;
                p.injuries &= ~fix; p.serious = std::max(0, p.serious - 1);
                Say(TextFormat("The Medic %s", fix == INJ_BROKEN_ARM ? "sets the broken arm" : "works the hook out of the hand"));
                cotT = 0;
            }
        } else cotT = 0;
    }
    // nobody at the davit: a skiff alongside can be hooked on from the water, slowly (Space in her, at the oars)
    {
        int davit = -1; for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == StationKind::Davit) davit = i;
        bool manned = false; for (const auto& c : crew) if (c.station == davit && !c.dead) manned = true;
        bool hooking = false; for (const auto& c : crew) if (c.deck == DECK_SKIFF && c.workOn == W_HOOK && !c.overboard) hooking = true;
        if (hooking && !manned && SkiffAlongside(3) && boat.Speed() < 0.4f && skiff.state == SkiffState::Afloat) {
            hookT += dt;
            skiff.p = Vector2Lerp(skiff.p, SkiffBerth(), std::min(1.0f, dt));
            if (hookT >= HOOK_ON_S) FinishRecovery();
        } else if (!hooking) hookT = 0;
    }
}


// ---------------------------------------------------------------- depth.exe --trawl-below-test
int RunTrawlBelowTest() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Trawl: below decks\n");
    const float dt = 1 / 60.0f;
    auto run = [&](Gannet& g, float s) { for (int i = 0; i < (int)(s * 60); i++) g.Step(dt); };
    Gannet g; g.Init(2, 51);
    Crew& c = g.crew[0];
    // the main hatch: down into the hold and up again
    c.p = g.hatches[0].at;
    check(g.TakeStation(0) && c.deck == 1 && g.SpaceAt(c.p, 1) == 2, "E at the open main hatch: down into the fish hold");
    check(g.TakeStation(0) && c.deck == 0, "and up again");
    // shut: 4 s to open; battened: 10 s, and never from below
    g.hatches[0].state = 1; g.TakeStation(0); run(g, 4.2f);
    check(g.hatches[0].state == 0, "a shut hatch takes 4 s to open");
    g.HatchCycle(0); bool shut = g.hatches[0].state == 1; g.HatchCycle(0); run(g, 10.2f);
    check(shut && g.hatches[0].state == 2, "R shuts it at once, then battens it down in 10 s");
    c.deck = 1; c.p = g.hatches[0].at; g.TakeStation(0); run(g, 5);
    check(g.hatches[0].state == 2 && c.deck == 1, "battened, it won't open from below");
    c.deck = 0; g.TakeStation(0); run(g, 10.2f);
    check(g.hatches[0].state == 1, "from above the battens come out in 10 s");
    g.hatches[0].state = 0;
    // the watertight door
    c.deck = 1; c.p = {-1.8f, 0}; g.doorOpen = false;
    for (int i = 0; i < 60 * 3; i++) { g.Move(0, {-1, 0}, false, dt); g.Step(dt); }
    bool blocked = g.SpaceAt(c.p, 1) == 2;
    c.p = {-2.2f, 0}; g.TakeStation(0);
    bool opened = g.doorOpen;
    for (int i = 0; i < 60 * 3; i++) { g.Move(0, {-1, 0}, false, dt); g.Step(dt); }
    check(blocked && opened && g.SpaceAt(c.p, 1) == 1, "the watertight door: shut, the hold ends at the bulkhead; open (E), through into the engine room");
    // the fo'c'sle: its own hatch, the magazine locker
    c.deck = 0; c.p = g.hatches[1].at; g.TakeStation(0);
    bool fore = c.deck == 1 && g.SpaceAt(c.p, 1) == 3;
    c.slots[3] = Slot{}; c.slots[3].it = Item::Rifle; c.slots[3].ammo = 0;
    int mag = -1; for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == StationKind::Magazine) mag = i;
    check(fore && mag >= 0 && Stations()[mag].deck == 1 && g.SpaceAt(Stations()[mag].at, 1) == 3, "the fore hatch: down into the fo'c'sle, where the magazine locker is");
    // the lamps: out past 20 deg of roll, 3 s to relight
    g.boat.roll = 22 * DEG2RAD; g.boat.rollVel = 0; g.StepBelow(dt);
    bool dark = !g.lamps[0].lit && !g.lamps[1].lit && !g.lamps[2].lit;
    g.boat.roll = 0; g.boat.rollVel = 0;
    c.p = g.lamps[2].at; c.station = -1; g.TakeStation(0);
    for (int i = 0; i < 60 * 3.2f; i++) { g.boat.roll = 0; g.boat.rollVel = 0; g.StepBelow(dt); }
    check(dark && g.lamps[2].lit, "past 20 deg of roll the oil lamps go out; a match relights one in 3 s");
    // open hatches ship water in a hard roll
    float b0 = g.boat.bilge; g.boat.roll = 28 * DEG2RAD; g.StepBelow(1.0f); g.boat.roll = 0;
    check(g.boat.bilge > b0 + 50, TextFormat("rolling her rail under, two open hatches ship %.0f kg in a second", g.boat.bilge - b0));
    // the fire: 30 s to the coal bunker unless smothered
    {
        Gannet f; f.Init(1, 52); f.boat.bunker = 40; f.boat.fireT = 8;
        for (int i = 0; i < 60 * 31; i++) f.StepBelow(dt);
        check(f.boat.bunker < 21 && f.boat.fireT <= 0, TextFormat("a fire left alone reaches the coal bunker in 30 s (%.0f of 40 kg of coal left)", f.boat.bunker));
        f.boat.bunker = 40; f.boat.fireT = 8;
        Crew& h = f.crew[0]; h.deck = 1; h.p = {-4.8f, -0.4f};
        f.TakeStation(0);
        for (int i = 0; i < 60 * 3.3f; i++) f.StepBelow(dt);
        check(f.boat.fireT <= 0 && f.boat.bunker == 40, "smothered in 3 s, it never reaches the bunker");
    }
    // the bilge eels
    {
        Gannet e; e.Init(1, 53); e.boat.bilge = 2500; e.crew[0].deck = 1; e.crew[0].p = {-6, 1};
        for (int i = 0; i < 60 * 120 && !e.crew[0].Has(INJ_BITE); i++) e.StepBelow(dt);
        check(e.crew[0].Has(INJ_BITE), "with the bilge half full the eels come in and bite whoever is below");
    }
    // the Medic's cot
    {
        Gannet m; m.Init(4, 54);
        int cot = -1; for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == StationKind::Cot) cot = i;
        Crew& p = m.crew[0]; p.deck = 1; p.p = Stations()[cot].at; p.station = cot; p.injuries = INJ_BROKEN_ARM; p.serious = 1;
        int medic = -1; for (int k = 1; k < 4; k++) if (m.crew[k].role == Role::Medic) medic = k;
        m.crew[medic].deck = 1; m.crew[medic].p = Vector2Add(p.p, {1, 0.4f});
        for (int i = 0; i < 60 * 10.2f; i++) m.StepBelow(dt);
        check(!p.Has(INJ_BROKEN_ARM) && p.serious == 0, "on the Medic's cot with the Medic beside them, a broken arm is set in 10 s");
    }
    // the skiff hooked on from the water with nobody at the davit
    {
        Gannet s; s.Init(1, 55); s.boat.telegraph = 0;
        s.skiff.state = SkiffState::Afloat; s.skiff.p = s.SkiffBerth(); s.skiff.integrity = D().skiffIntegrity;
        s.crew[0].deck = DECK_SKIFF; s.crew[0].p = {0.2f, 0}; s.crew[0].workOn = 600;
        for (int i = 0; i < 60 * 26; i++) { s.boat.vel = {0, 0}; s.Step(dt); }
        check(s.skiff.state == SkiffState::Stowed && s.crew[0].deck == 0, "nobody at the davit: hooked on from the water, she comes up in 25 s");
    }
    printf(fails ? "trawl-below-test: %d FAILED\n" : "trawl-below-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}
} // namespace tw
