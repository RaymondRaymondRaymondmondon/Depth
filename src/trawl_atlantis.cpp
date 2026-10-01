// Atlantis Waters' threats (design doc v2, page 38 and the threat table, pages 49-50). The ground itself is in
// trawl_eco.cpp (BuildAtlantisChart), its landings in trawl_landing.cpp. The doc gives each a draw, tells, what it does
// and counters; the numbers are my design calls:
//  - The Pale Eye: while a hand looks at it (the searchlight swung toward it, or a ping open within 120 m of it) the
//    Wake rises twice as fast. It blinks when something big is coming (the Ghost Ship, the Kraken).
//  - The Deep Choir (a Siren chorus that sings the whole crew; counters: the bell breaks a charm, shoot the singer when
//    it surfaces): with the Wake past 25, 30 s of song; every hand on deck and off station walks to the nearest rail
//    (0.5 m/s) and over. A ring of the bell breaks the charm for 6 s; the singer surfaces 20-30 m off for 4 s in every
//    10, and a shot within 5 m of it ends the song.
//  - A Cult longboat (drawn by the Eye's Wake; chanting from the dark, torches; circles and chums the water round the
//    Gannet; counters: outrun it, rifle the rowers, stop looking at the Eye): it circles at 30 m, chumming blood into
//    the water every few seconds. Twenty-five seconds at more than 3 m/s and it falls behind; three hits on the rowers
//    (a shot within 4 m) and it turns away; while anyone looks at the Eye it stays. It comes at last regardless after 2
//    minutes; taking the cult's hoard brings it at once.
//  - The Ghost Ship (Wake 65, or the bell rung more than six times in a night: a bell answering yours; comes alongside
//    and boards with drowned crew; steals set gear; counters: flee at full steam, or fight: a beaten Ghost Ship leaves a
//    relic chest): a bell answers, 30 s later it is alongside, three Drowned come over the rail and the longlines and
//    pots are gone. Twenty-five seconds at more than 3.5 m/s (before it reaches her) and she leaves it behind; put every boarder down and it leaves a
//    relic chest (300-700) on the deck. Once a night.
//  - The Kraken (Wake 90; the Glass: dead calm, the sonar whites out, the fish vanish; arms take crew and gear off the
//    deck, crush hull sections, can take the boat; counters: cut the net, kill the lantern, run at full steam, harpoon
//    an arm): 20 s of the Glass, then an arm every 6 s: a hand on deck over the side, or the net torn away, or 20 off a
//    section. A harpoon within 6 m of the boat makes the next arm let go; with the net in, the lantern out and her at
//    full steam for 15 s it lets her go; otherwise it gives up after 90 s. Once a night.
#include "trawl.h"
#include "trawl_eco.h"
#include "trawl_session.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tw {

namespace {
uint32_t gAr = 4141;
float ARand() { gAr = gAr * 1664525u + 1013904223u; return (gAr >> 8) * (1.0f / 16777216.0f); }
float HalfBeamA(float x) { return x > 5 ? std::max(0.5f, 3.0f - (x - 5) * 0.42f) : x < -10 ? 3.0f - (-10 - x) * 0.8f : 3.0f; }
float AngleTo(const Boat& b, Vector2 p) { Vector2 d = Vector2Subtract(p, b.pos); float a = atan2f(d.y, d.x) - (b.heading + b.searchAim); while (a > PI) a -= 2 * PI; while (a < -PI) a += 2 * PI; return a; }
}

bool Gannet::Atlantis() const { return eco && eco->ground == "atlantis"; }

void Gannet::StepAtlantis(float dt) {
    bellT += dt;
    if (!Atlantis() || moored) {
        if (eco) { eco->wakeMul = 1; }
        choir = {}; longboat = {}; ghost.state = 0; kraken.state = 0; eyeLooked = false;
        if (moored) { ghostDone = krakenDone = false; bellRings = 0; cultRaid = false; }
        return;
    }
    Eco& e = *eco;
    float wake = e.wake;
    choirCool -= dt; longboatCool -= dt;

    // ---- the Pale Eye
    float eyeD = Vector2Distance(boat.pos, e.eyeP);
    eyeLooked = (boat.lantern == 3 && fabsf(AngleTo(boat, e.eyeP)) < 0.35f && eyeD < 260) || (sonar.sinceP < 6 && eyeD < 120);
    e.wakeMul = eyeLooked ? 2.0f : 1.0f;
    eyeBlinkT = std::max(0.0f, eyeBlinkT - dt);
    if ((ghost.state == 1 || kraken.state == 1) && eyeBlinkT <= 0 && ARand() < dt / 3) eyeBlinkT = 0.4f;

    // ---- the Deep Choir
    float stir = e.Stir();
    if (!choir.on && choirCool <= 0 && (wake >= 25 || stir >= 0.45f) && ARand() < dt / 40) {
        choir = {}; choir.on = true;
        Say("Voices rise out of the water all round her, many of them, singing: the crew's heads turn to the rails. Ring the bell!");
    }
    if (choir.on) {
        choir.t += dt; choir.calmT = std::max(0.0f, choir.calmT - dt);
        if (bellT < dt + 0.001f) { choir.calmT = 6; Say("The bell cuts through the song: the crew shake their heads"); }
        // the singer: up 20-30 m off for 4 s in every 10
        bool up = fmodf(choir.t, 10) < 4;
        if (up && !choir.surfaced) { float a = ARand() * 2 * PI; choir.singer = Vector2Add(boat.pos, {cosf(a) * (20 + ARand() * 10), sinf(a) * (20 + ARand() * 10)}); Say("A pale head breaks the surface: the singer"); }
        choir.surfaced = up;
        bool shot = false; if (up) for (auto& s : shots) if (s.life > 0 && Vector2Distance({s.p.x, s.p.y}, choir.singer) < 5 && (s.kind == Shot::Bullet || s.kind == Shot::Pellet || s.kind == Shot::Spear || s.kind == Shot::Harpoon)) { s.life = -1; shot = true; }
        if (shot || choir.t > 30) {
            Say(shot ? "The singer is hit: the chorus breaks off in a scream" : "The song sinks away into the deep");
            choir = {}; choirCool = 220;
        } else if (choir.calmT <= 0) {
            for (int k = 0; k < (int)crew.size(); k++) {
                Crew& c = crew[k];
                if (c.dead || c.overboard || c.deck != 0 || c.station >= 0) continue;
                c.heldT = 1;
                float ry = (c.p.y >= 0 ? 1 : -1) * (HalfBeamA(c.p.x) - 0.3f);
                c.p.y += std::clamp(ry - c.p.y, -0.5f * dt, 0.5f * dt);
                if (fabsf(c.p.y - ry) < 0.05f) GoOverboard(k, "sung over the rail by the Deep Choir");
            }
        }
    }

    // ---- a Cult longboat
    if (!longboat.on && (cultRaid || (longboatCool <= 0 && (wake >= 15 || stir >= 0.35f) && ARand() < dt / 60))) {
        longboat = {}; longboat.on = true; longboat.ang = ARand() * 2 * PI; cultRaid = false;
        Say("Chanting from the dark, and torches on the water: a cult longboat");
    }
    if (longboat.on) {
        longboat.t += dt;
        longboat.ang += dt * 0.35f;
        longboat.p = Vector2Add(boat.pos, {cosf(longboat.ang) * 30, sinf(longboat.ang) * 30});
        if (fmodf(longboat.t, 4) < dt) e.AddBlood({longboat.p.x, longboat.p.y, 1}, 30);   // chumming the ring round her
        for (auto& s : shots) if (s.life > 0 && Vector2Distance({s.p.x, s.p.y}, longboat.p) < 4 && (s.kind == Shot::Bullet || s.kind == Shot::Pellet)) { s.life = -1; longboat.hits++; Say("A rower slumps over his oar"); }
        longboat.fleeT = boat.Speed() > 3.0f ? longboat.fleeT + dt : std::max(0.0f, longboat.fleeT - dt);
        if (longboat.hits >= 3 || (longboat.fleeT > 25 && !eyeLooked) || (longboat.t > 120 && !eyeLooked)) {
            Say(longboat.hits >= 3 ? "The longboat turns away, short of rowers" : longboat.fleeT > 25 ? "The torches fall away astern" : "The chanting fades: the longboat is gone");
            longboat = {}; longboatCool = 240;
        }
    }

    // ---- the Ghost Ship
    if (ghost.state == 0 && !ghostDone && (wake >= 65 || bellRings > 6 || (stir >= 0.7f && ARand() < dt / 90))) {
        ghost = {}; ghost.state = 1; ghost.p = boat.ToWorld({-60, 0});
        Say("A bell answers yours, out in the dark: slow, and closer each time");
    }
    if (ghost.state > 0) {
        ghost.t += dt;
        ghost.fleeT = boat.Speed() > 3.5f ? ghost.fleeT + dt : std::max(0.0f, ghost.fleeT - dt * 0.5f);
        Vector2 want = boat.ToWorld(ghost.state == 2 ? Vector2{0, -9} : Vector2{-60 + std::min(1.0f, ghost.t / 30) * 50, -12});
        ghost.p = Vector2Lerp(ghost.p, want, std::min(1.0f, dt * 0.5f));
        if (ghost.fleeT > 25) { Say("The Ghost Ship's bell falls away astern: she has outrun it"); ghost.state = 0; ghostDone = true; }
        else if (ghost.state == 1 && ghost.t > 30) {
            ghost.state = 2; ghost.t = 0;
            for (int i = 0; i < 3; i++) { DrownedSailor d; float x = -6 + i * 4.0f; d.p = {x, -(HalfBeamA(x) - 0.4f)}; drowned.push_back(d); }
            int stolen = (int)(longlines.size() + pots.size()); longlines.clear(); pots.clear();
            Say(stolen > 0 ? "The Ghost Ship grinds alongside: drowned crew come over the rail, and the set gear is gone from the water" : "The Ghost Ship grinds alongside: drowned crew come over the rail");
        } else if (ghost.state == 2 && drowned.empty()) {
            CatchRec chest; chest.name = "a relic chest"; chest.junk = true; chest.kg = 25; chest.price = 300 + ARand() * 400; chest.dead = chest.gutted = chest.iced = true; chest.deckAt = {-2, 0};
            hold.push_back(chest);
            Say("The last boarder goes down: the Ghost Ship drifts off, and leaves a relic chest on the deck");
            ghost.state = 0; ghostDone = true;
        }
    }
    StepDrowned(dt);

    // ---- the Kraken
    if (kraken.state == 0 && !krakenDone && (wake >= 90 || (stir >= 0.85f && ARand() < dt / 150))) {
        kraken = {}; kraken.state = 1;
        Say("The Glass: the sea goes dead flat, the sonar whites out, and the fish are gone");
    }
    if (kraken.state > 0) {
        kraken.t += dt;
        sonar.sinceP = 0; sonar.ret.clear();   // (the sonar whites out)
        if (kraken.state == 1 && kraken.t > 20) { kraken.state = 2; kraken.t = 0; kraken.armT = 3; Say("Arms the width of a man come up over the rail"); }
        else if (kraken.state == 2) {
            for (auto& s : shots) if (s.life > 0 && s.kind == Shot::Harpoon && Vector2Distance({s.p.x, s.p.y}, boat.pos) < 14) { s.life = -1; kraken.letGo++; Say("The harpoon goes into an arm: it lets go and sinks back"); }
            bool safe = (net.state == NetState::Stowed || net.state == NetState::Lost) && boat.lantern == 0 && boat.Speed() > 3.5f;
            kraken.fleeT = safe ? kraken.fleeT + dt : 0;
            if (kraken.fleeT > 15 || kraken.t > 90) { Say(kraken.fleeT > 15 ? "Dark and running, she slips out of its reach: the arms fall away" : "The arms slide back under: the Kraken has had enough of her"); kraken.state = 0; krakenDone = true; }
            else {
                kraken.armT -= dt;
                if (kraken.armT <= 0) {
                    kraken.armT = 6;
                    if (kraken.letGo > 0) { kraken.letGo--; }
                    else {
                        float r = ARand();
                        int victim = -1; for (int k = 0; k < (int)crew.size(); k++) { const Crew& c = crew[k]; if (!c.dead && !c.overboard && c.deck == 0 && c.charm != CH_BEAK && fabsf(c.p.y) > 1.2f) victim = k; }
                        if (r < 0.4f && victim >= 0) GoOverboard(victim, "taken off the deck by the Kraken");
                        else if (r < 0.6f && (net.state == NetState::Down || net.state == NetState::Snagged)) { net.state = NetState::Lost; net.catchKg.clear(); net.load = 0; Say("An arm tears the net away"); }
                        else { boat.Hit((int)(ARand() * SEC_COUNT) % SEC_COUNT, 20); Say("An arm crushes the plating"); }
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------- depth.exe --trawl-atlantis-test
int RunTrawlAtlantisTest() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Trawl: Atlantis Waters\n");
    const float dt = 1 / 60.0f;
    Eco e; if (!e.Init("atlantis", 6161)) { printf("  FAIL  Atlantis won't load\n"); return 1; }
    float size = e.n * e.cell;
    check(e.DepthAt({size * 0.45f, size * 0.5f}) >= 25 && e.DepthAt({size * 0.95f, size * 0.5f}) > 300 && e.landingAt.size() == 3 && e.marks.size() == 3,
          TextFormat("the chart: terraces at %.0f m, the Trench's edge at %.0f m, three landings, three skiff marks", e.DepthAt({size * 0.45f, size * 0.5f}), e.DepthAt({size * 0.95f, size * 0.5f})));
    Vector2 open{size * 0.62f, size * 0.5f};
    auto fresh = [&](Gannet& g, int n) { g.Init(n, 31); g.eco = &e; g.boat.pos = open; g.boat.telegraph = 0; g.choirCool = g.longboatCool = 1e9f; g.ghostDone = g.krakenDone = true; e.wake = 0; };
    // the Pale Eye
    {
        Gannet g; fresh(g, 1); g.boat.pos = Vector2Add(e.eyeP, {-100, 0}); g.boat.heading = 0; g.boat.lantern = 3; g.boat.searchAim = 0;
        g.StepAtlantis(dt);
        bool looking = g.eyeLooked && e.wakeMul == 2;
        g.boat.searchAim = PI; g.StepAtlantis(dt);
        check(looking && !g.eyeLooked && e.wakeMul == 1, "the searchlight on the Pale Eye doubles the Wake's rise; turned away, it doesn't");
    }
    // the Deep Choir
    {
        Gannet g; fresh(g, 2); e.wake = 30; g.choirCool = 0;
        g.crew[0].p = {-3, 1.0f}; g.crew[1].p = {-3, -1.0f};
        for (int i = 0; i < 60 * 900 && !g.choir.on; i++) { e.wake = 30; g.StepAtlantis(dt); }
        bool came = g.choir.on;
        for (int i = 0; i < 60 * 8; i++) g.StepAtlantis(dt);
        check(came && g.crew[0].overboard && g.crew[1].overboard, "the Deep Choir sings the whole crew on deck to the rails and over");
        Gannet h; fresh(h, 1); h.choir.on = true; h.crew[0].p = {-3, 0.5f};
        h.bellT = 0; h.StepAtlantis(dt);
        float y0 = h.crew[0].p.y; for (int i = 0; i < 60 * 5; i++) h.StepAtlantis(dt);
        check(fabsf(h.crew[0].p.y - y0) < 0.01f && !h.crew[0].overboard, "a ring of the bell breaks the charm");
        Gannet k; fresh(k, 1); k.choir.on = true; k.choir.t = 0.5f; k.choir.calmT = 99; k.StepAtlantis(dt);
        Projectile s; s.kind = Shot::Bullet; s.life = 1; s.p = {k.choir.singer.x, k.choir.singer.y, 0}; k.shots.push_back(s); k.StepAtlantis(dt);
        check(!k.choir.on, "a shot at the singer when it surfaces ends the song");
    }
    // the Cult longboat
    {
        Gannet g; fresh(g, 1); g.cultRaid = true; g.StepAtlantis(dt);
        float b0 = e.blood.Total();
        for (int i = 0; i < 60 * 10; i++) g.StepAtlantis(dt);
        check(g.longboat.on && e.blood.Total() > b0 + 20, "taking the cult's hoard brings a longboat: it circles and chums the water");
        for (int n = 0; n < 3; n++) { Projectile s; s.kind = Shot::Bullet; s.life = 1; s.p = {g.longboat.p.x, g.longboat.p.y, 0}; g.shots.push_back(s); g.StepAtlantis(dt); g.shots.clear(); }
        check(!g.longboat.on, "three rowers shot: it turns away");
        Gannet h; fresh(h, 1); h.cultRaid = true; h.StepAtlantis(dt);
        for (int i = 0; i < 60 * 26; i++) { h.boat.vel = Vector2Scale(h.boat.Forward(), 3.6f); h.StepAtlantis(dt); }
        check(!h.longboat.on, "or outrun it");
    }
    // the Ghost Ship
    {
        Gannet g; fresh(g, 3); g.ghostDone = false; g.bellRings = 7;
        Longline ll; g.longlines.push_back(ll);
        g.crew[0].p = {6, 0}; g.crew[1].p = {6, 0.5f}; g.crew[2].p = {6, -0.5f};
        for (int i = 0; i < 60 * 31; i++) g.StepAtlantis(dt);
        check(g.ghost.state == 2 && g.drowned.size() == 3 && g.longlines.empty(), "a bell rung too often is answered: the Ghost Ship comes alongside, boards with drowned crew and takes the set gear");
        size_t h0 = g.hold.size();
        while (!g.drowned.empty()) g.HitDrowned(g.drowned[0].p, 50, 1.0f);
        g.StepAtlantis(dt);
        bool chest = g.hold.size() == h0 + 1 && g.hold.back().name == "a relic chest";
        check(chest && g.ghost.state == 0, "fight it off: it leaves a relic chest");
        Gannet h; fresh(h, 1); h.ghostDone = false; e.wake = 70; h.StepAtlantis(dt);
        for (int i = 0; i < 60 * 31; i++) { e.wake = 70; h.boat.vel = Vector2Scale(h.boat.Forward(), 3.8f); h.StepAtlantis(dt); }
        check(h.ghost.state == 0 && h.drowned.empty(), "at Wake 65 it comes; at full steam she outruns it before it can board");
    }
    // the Kraken
    {
        Gannet g; fresh(g, 2); g.krakenDone = false; e.wake = 95;
        g.crew[0].p = {-3, 2.0f}; g.crew[1].p = {2, 0};
        g.StepAtlantis(dt);
        check(g.kraken.state == 1, "at Wake 90: the Glass");
        float hp0 = 0; for (int s = 0; s < SEC_COUNT; s++) hp0 += g.boat.integrity[s];
        for (int i = 0; i < 60 * 45; i++) g.StepAtlantis(dt);
        float hp1 = 0; for (int s = 0; s < SEC_COUNT; s++) hp1 += g.boat.integrity[s];
        check(g.kraken.state == 2 && (hp1 < hp0 || g.crew[0].overboard), "then the arms: crew taken off the deck, the plating crushed");
        Gannet h; fresh(h, 1); h.krakenDone = false; e.wake = 95; h.boat.lantern = 0; h.net.state = NetState::Stowed;
        for (int i = 0; i < 60 * 40; i++) { e.wake = 95; h.boat.vel = Vector2Scale(h.boat.Forward(), 3.8f); h.StepAtlantis(dt); }
        check(h.kraken.state == 0 && h.krakenDone, "the net in, the lantern out and full steam: it lets her go");
    }
    // the landings, and the Keeper's offerings
    {
        Gannet g; fresh(g, 1); g.BuildLandings();
        bool kinds = g.landings.size() == 3 && g.landings[0].kind == LK_STAIR && g.landings[1].kind == LK_TOWER && g.landings[2].kind == LK_CULT;
        check(kinds, "the Drowned Stair, the Watchtower stump and the Cult Landing");
        if (kinds) {
            Session s; s.G = &g; s.E = &e;
            Crew& c = g.crew[0]; g.skiff.state = SkiffState::Beached; g.skiff.landing = 0; c.deck = DECK_SHORE;
            Landing& st = g.landings[0];
            c.p = Vector2Add(st.caches[0].p, {0.5f, 0}); size_t n0 = st.caches.size(); bool shut = g.ShoreUse(0) && !st.caches[0].open;
            check(shut && n0 == 2, "the Keeper won't let a bowl go without an offering");
            c.p = Vector2Add(st.elder, {1, 0.4f});
            CatchRec f; f.name = "skipjack"; f.kg = 30; f.price = 4; f.dead = true; c.carrying = true; c.carry = f;
            s.ElderGive(0);
            c.p = Vector2Add(st.caches[0].p, {0.5f, 0}); g.ShoreUse(0);
            check(st.caches[0].open && c.carrying, TextFormat("an offering of fish (%.0f) and he lets a bowl go (%.0f)", s.Value(f), st.caches[0].value));
            c.carrying = false; c.carryKg = 0;
            g.skiff.landing = 1; Landing& tw = g.landings[1]; c.p = Vector2Add(tw.fire, {0.5f, 0});
            float w0 = e.wake; g.ShoreUse(0); for (int i = 0; i < 60 * 11; i++) g.StepLandings(dt);
            check(tw.fireLit && g.sonar.marks.size() >= 3 && e.wake >= w0 + 14, "the Watchtower's signal fire: every skiff mark on the sonar, and the Wake leaps");
            g.skiff.landing = 2; Landing& cu = g.landings[2]; c.p = Vector2Add(cu.elder, {1, 0.4f});
            s.money = 500; c.slots[3] = Slot{};
            check(s.ElderBuy(0, "obsidian") && s.money < 500, "the cult quartermaster sells dark goods (the obsidian dagger) for shillings");
            c.p = Vector2Add(cu.caches[0].p, {0.4f, 0}); g.cultRaid = false; g.ShoreUse(0);
            check(g.cultRaid, "taking the cult's hoard brings the longboats");
        }
    }
    e.wake = 0;
    printf(fails ? "trawl-atlantis-test: %d FAILED\n" : "trawl-atlantis-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}
} // namespace tw
