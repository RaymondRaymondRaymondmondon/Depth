// The Grotto's threats (design doc v2, page 39 and the threat table, pages 49-50). The ground itself (the cave behind
// its sea arch, the echo, the stalactites) is in trawl_eco.cpp and trawl_session.cpp; the Lantern Angler is also a
// species in the web. Here are the four encounters the doc gives tells and counters for:
//  - Lantern Angler ("a second light in the dark that you didn't hang"; lures a crewman to the rail and pulls them over;
//    counters: don't walk toward lights, hood the lantern, a flare burst blinds it). It shows 12-20 m off one side while
//    her lantern burns at low or full; after 6 s it takes the nearest hand on deck on that side and walks them to the
//    rail (0.8 m/s); at the rail they go over. A hand beside them (E) shakes them out of it; a flare within 20 m blinds
//    it; hooding the lantern (level 0) sends it away within 4 s.
//  - Ghost Worm (drawn by vibration: taut lines, the net, the screw; tells: a long shape circling on the sonar, lines
//    hum; bites through lines, snags the net, at full size coils the hull; counters: cut the line, stop the screw, lower
//    the lantern, harpoon plus rifles on the head). Vibration in the cave builds `wormWake`; at 1 it rises and circles
//    15 s (the tell), then every 10 s it bites through a fighting line, or snags the net, or, with the screw turning,
//    coils the hull (she stops dead and takes 6 a section every 4 s). Quiet for 8 s (no line, no net, the screw
//    stopped) and it sinks away; six hits on its head (a harpoon counts three) kill it.
//  - Isopod swarm (drawn by corpses and offal on deck; clicking on the anchor chain; climb aboard, eat the catch, bite
//    ankles). Ungutted fish or blood on the deck for 20 s: 10 s of clicking, then 30 of them aboard: every 6 s they strip
//    an ungutted fish, and bite ankles. A clean deck (nothing ungutted, the blood sluiced) starves them off; a stamp (any
//    melee blow) kills five.
//  - Drowned sailors (drawn by light; knocking on the hull, slow and regular; climb aboard, slow, relentless, grab and
//    drag). Lying within 40 m of a wreck with her lantern lit: 15 s of knocking, then one climbs over the rail, walks at
//    the nearest hand (0.6 m/s), grabs, and drags them to the rail (0.4 m/s) and over. 40 hp: blows and shots put it
//    down (it lets go when hit hard).
#include "trawl.h"
#include "trawl_eco.h"
#include "trawl_session.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tw {

namespace {
uint32_t gGr = 313;
float GRand() { gGr = gGr * 1664525u + 1013904223u; return (gGr >> 8) * (1.0f / 16777216.0f); }
float HalfBeamG(float x) { return x > 5 ? std::max(0.5f, 3.0f - (x - 5) * 0.42f) : x < -10 ? 3.0f - (-10 - x) * 0.8f : 3.0f; }
bool AtRail(Vector2 p) { return fabsf(p.y) > HalfBeamG(p.x) - 0.6f; }
const float WORM_HEAD_HITS = 6;
}

bool Gannet::Grotto() const { return eco && eco->ground == "grotto"; }

bool Gannet::HitDrowned(Vector2 at, float dmg, float reach) {
    int best = -1; float bd = reach;
    for (int i = 0; i < (int)drowned.size(); i++) { float d = Vector2Distance(drowned[i].p, at); if (d < bd) { bd = d; best = i; } }
    if (best >= 0) {
        DrownedSailor& d = drowned[best];
        d.hp -= dmg; d.hitT = 0.3f;
        if (dmg >= 12 && d.grab >= 0) { crew[d.grab].heldT = 0; d.grab = -1; Say("The blow breaks the Drowned sailor's grip"); }
        if (d.hp <= 0) { Say("The Drowned sailor falls apart on the deck, and the sea takes what's left"); drowned.erase(drowned.begin() + best); }
        return true;
    }
    if (isopods.state == 2 && isopods.n > 0) {
        isopods.n = std::max(0, isopods.n - 5);
        Say(isopods.n > 0 ? "A stamp: isopods crunch underfoot" : "The last of the isopods are stamped flat");
        if (isopods.n == 0) isopods.state = 0;
        return true;
    }
    return false;
}

void Gannet::StepGrotto(float dt) {
    for (auto& c : crew) c.heldT = 0;
    if (moored) drowned.clear();
    if (!Grotto() || moored) { angler = {}; worm.state = 0; isopods = {}; knockT = -1; return; }
    Eco& e = *eco;
    float stir = e.Stir();
    bool inCave = boat.pos.x > e.archX1;
    anglerCool -= dt; drownedCool -= dt;
    bool lightDraws = boat.lantern >= 1 && !AnyWears(CH_GLASS_LANTERN);   // (the glass lantern: a light its wearer carries draws nothing)

    // ---- the Lantern Angler: a second light off one side
    if (!angler.on && inCave && anglerCool <= 0 && lightDraws && !bloomNight && GRand() < dt * (0.15f + stir) / (eelRun ? 30 : 60)) {
        float side = GRand() < 0.5f ? -1.0f : 1.0f;
        angler.on = true; angler.t = 0; angler.lured = -1;
        angler.p = boat.ToWorld({-4 + GRand() * 10, side * (12 + GRand() * 8)});
        Say("A light in the dark off the rail, low on the water: not one of ours");
    }
    if (angler.on) {
        angler.t += dt;
        Vector2 al = boat.ToDeck(angler.p);
        bool blind = false; for (const auto& f : flares) if (Vector2Distance(f.p, angler.p) < 20) blind = true;
        if (blind || (boat.lantern == 0 && angler.t > 4) || angler.t > 60) {
            Say(blind ? "The flare burst blinds it: the second light gutters out" : "The light under the water dims and drifts away");
            angler = {}; anglerCool = 160;
        } else {
            if (angler.lured < 0 && angler.t > 6) {
                int best = -1; float bd = 1e9f;
                for (int k = 0; k < (int)crew.size(); k++) { const Crew& c = crew[k]; if (c.dead || c.overboard || c.deck != 0 || c.station >= 0 || c.charm == CH_WHITE_SKULL) continue; if ((c.p.y > 0) != (al.y > 0)) continue; float d = Vector2Distance(c.p, al); if (d < bd) { bd = d; best = k; } }
                if (best >= 0) { angler.lured = best; Say("A hand stares at the light and walks toward the rail: shake them out of it (E beside them), hood the lantern, or burn a flare at it"); }
            }
            if (angler.lured >= 0) {
                Crew& c = crew[angler.lured];
                if (c.dead || c.overboard || c.deck != 0) angler.lured = -1;
                else {
                    c.heldT = 1; c.station = -1;
                    float ry = (al.y > 0 ? 1 : -1) * (HalfBeamG(c.p.x) - 0.3f);
                    c.p.y += std::clamp(ry - c.p.y, -0.8f * dt, 0.8f * dt);
                    c.facing = {0, al.y > 0 ? 1.0f : -1.0f};
                    if (fabsf(c.p.y - ry) < 0.05f) { int k = angler.lured; angler = {}; anglerCool = 200; GoOverboard(k, "pulled over the rail by a Lantern Angler"); }
                }
            }
        }
    }

    // ---- the Ghost Worm: woken by vibration in the cave
    float vib = 0;
    for (const auto& r : rods) if (r.state == RodState::Fighting) vib += 0.5f + std::clamp(r.fight.tension / std::max(1.0f, TackleOf(r.tackle).strength), 0.0f, 1.0f);
    if (net.state == NetState::Down || net.state == NetState::Snagged) vib += 1;
    if (boat.shaft > 0.3f) vib += 0.6f;
    if (boat.lantern <= 1) vib *= 0.5f;   // (lower the lantern)
    if (worm.state == 0) {
        if (inCave) wormWake += vib * dt / 90; else wormWake = std::max(0.0f, wormWake - dt / 120);
        wormWake = std::max(0.0f, wormWake - (vib < 0.1f ? dt / 200 : 0));
        if (wormWake >= 1) { worm = {}; worm.state = 1; worm.ang = GRand() * 2 * PI; worm.t = 0; Say("The lines hum on their own: something long is circling under her"); }
    }
    if (worm.state > 0) {
        worm.t += dt;
        float rad = worm.state == 3 ? 0 : 20;
        worm.ang += dt * 0.25f;
        worm.p = Vector2Add(boat.pos, {cosf(worm.ang) * rad, sinf(worm.ang) * rad});
        // the head: shots and harpoons that land within 3 m of it
        for (auto& s : shots) if (s.life > 0 && Vector2Distance({s.p.x, s.p.y}, worm.p) < (worm.state == 3 ? 6.0f : 3.0f) && s.p.z >= -0.5f && (s.kind == Shot::Bullet || s.kind == Shot::Harpoon || s.kind == Shot::Spear)) { worm.hits += s.kind == Shot::Harpoon ? 3 : 1; s.life = -1; }
        if (worm.hits >= WORM_HEAD_HITS) { Say("The Ghost Worm's head comes apart: the long body sinks away into the dark"); if (eco) eco->AddBlood({worm.p.x, worm.p.y, 4}, 120); worm = {}; wormWake = 0; }
        else {
            bool quiet = vib < 0.1f;
            worm.quietT = quiet ? worm.quietT + dt : 0;
            if (worm.quietT > 8) { Say(worm.state == 3 ? "The coils slacken and slide off the hull: it's gone back down" : "The humming fades: the long shape sinks away"); worm = {}; wormWake = 0.3f; }
            else if (worm.state == 1 && worm.t > 15) { worm.state = 2; worm.actT = 0; }
            else if (worm.state == 2) {
                worm.actT -= dt;
                if (worm.actT <= 0) {
                    worm.actT = 10;
                    int fought = -1; for (int i = 0; i < (int)rods.size(); i++) if (rods[i].state == RodState::Fighting) fought = i;
                    if (fought >= 0) { rods[fought].fight.end = FightEnd::Snapped; Say("Something bites clean through the line"); }
                    else if (net.state == NetState::Down) { net.state = NetState::Snagged; net.backT = 0; Say("The warps jerk taut: the Ghost Worm has the net"); }
                    else if (boat.shaft > 0.3f) { worm.state = 3; worm.coilT = 0; Say("The Ghost Worm coils round the hull: she stops dead, and the plating groans"); }
                }
            } else if (worm.state == 3) {
                boat.vel = Vector2Scale(boat.vel, expf(-2.0f * dt));
                worm.coilT += dt;
                if (worm.coilT >= 4) { worm.coilT = 0; boat.Hit((int)(GRand() * SEC_COUNT) % SEC_COUNT, 6); }
            }
        }
    }

    // ---- the isopod swarm: offal on the deck draws them up the anchor chain
    {
        bool offal = DeckFish() > 0 || deckBlood > 2;
        if (isopods.state == 0) {
            isopods.drawT = inCave && offal ? isopods.drawT + dt : std::max(0.0f, isopods.drawT - dt);
            if (isopods.drawT > 20) { isopods.state = 1; isopods.t = 0; isopods.drawT = 0; Say("Clicking on the anchor chain: something is climbing it"); }
        } else if (isopods.state == 1) {
            isopods.t += dt;
            if (!offal && isopods.t > 4) { isopods.state = 0; }
            else if (isopods.t > 10) { isopods.state = 2; isopods.n = 30; isopods.t = 0; isopods.eatT = 6; Say("Isopods pour over the bow: they're after the catch, and they bite"); }
        } else {
            isopods.t += dt;
            isopods.eatT -= dt;
            if (isopods.eatT <= 0) {
                isopods.eatT = 6;
                for (size_t i = 0; i < hold.size(); i++) if (!hold[i].gutted && !hold[i].crated && !hold[i].junk) { Say(TextFormat("The isopods strip the %s to the bones", hold[i].name.c_str())); hold.erase(hold.begin() + i); break; }
            }
            for (int k = 0; k < (int)crew.size(); k++) { Crew& c = crew[k]; if (c.dead || c.overboard || c.deck != 0) continue; if (GRand() < dt / 25 * std::min(1.0f, isopods.n / 15.0f)) Injure(k, INJ_BITE, "isopods at the ankles"); }
            if (!offal) { isopods.n = std::max(0, isopods.n - (GRand() < dt * 3 ? 1 : 0)); }
            if (isopods.n == 0) { isopods.state = 0; Say("Nothing left to eat: the isopods drain back down the chain"); }
        }
    }

    // ---- the Drowned: by the wrecks, drawn by her light
    {
        bool nearWreck = false;
        if (inCave) for (int k = 0; k < 12 && !nearWreck; k++) { float a = k * PI / 6; for (float r = 8; r <= 40; r += 8) if (e.HabAt({boat.pos.x + cosf(a) * r, boat.pos.y + sinf(a) * r}) == H_WRECK) nearWreck = true; }
        if (knockT < 0 && (nearWreck || (bloomNight && inCave)) && (lightDraws || bloomNight) && drownedCool <= 0 && drowned.size() < 2 && GRand() < dt * (0.2f + stir) / 50) { knockT = 0; Say("Knocking on the hull, slow and regular, from below the waterline"); }
        if (knockT >= 0) {
            knockT += dt;
            if (knockT > 15) {
                knockT = -1; drownedCool = 90;
                DrownedSailor d; float side = GRand() < 0.5f ? -1.0f : 1.0f; float x = -8 + GRand() * 13;
                d.p = {x, side * (HalfBeamG(x) - 0.4f)};
                drowned.push_back(d);
                Say("A Drowned sailor hauls itself over the rail, streaming water");
            }
        }
        StepDrowned(dt);
    }
}

// the Drowned on the deck: they walk at the nearest hand, grab, and drag them to the rail and over; blows and shots put
// them down (the Grotto's, and Atlantis's Ghost Ship's boarders)
void Gannet::StepDrowned(float dt) {
    for (size_t i = 0; i < drowned.size(); i++) {
        DrownedSailor& d = drowned[i];
        d.hitT = std::max(0.0f, d.hitT - dt);
        if (d.grab >= 0) {
            Crew& c = crew[d.grab];
            if (c.dead || c.overboard || c.deck != 0) { d.grab = -1; continue; }
            c.heldT = 1; c.station = -1;
            if (c.charm == CH_BEAK) { d.grab = -1; continue; }   // (grabbers can't hold the wearer)
            float ry = (d.p.y >= 0 ? 1 : -1) * (HalfBeamG(c.p.x) - 0.3f);
            float step = std::clamp(ry - c.p.y, -0.4f * dt, 0.4f * dt);
            c.p.y += step; d.p.y += step;
            if (fabsf(c.p.y - ry) < 0.05f && AtRail(c.p)) {
                int k = d.grab; drowned.erase(drowned.begin() + i);
                GoOverboard(k, "dragged over the rail by a Drowned sailor");
                break;
            }
            continue;
        }
        if (d.hitT > 0) continue;   // (staggered by a blow)
        int best = -1; float bd = 1e9f;
        for (int k = 0; k < (int)crew.size(); k++) { const Crew& c = crew[k]; if (c.dead || c.overboard || c.deck != 0) continue; float dd = Vector2Distance(c.p, d.p); if (dd < bd) { bd = dd; best = k; } }
        if (best < 0) continue;
        Vector2 to = Vector2Subtract(crew[best].p, d.p);
        if (bd < 0.7f) {
            bool taken = false; for (const auto& o : drowned) if (o.grab == best) taken = true;
            if (!taken) { d.grab = best; Say("The Drowned sailor's hands close on a hand: hit it hard to break the grip"); }
        } else d.p = Vector2Add(d.p, Vector2Scale(Vector2Normalize(to), 0.6f * dt));
    }
    // shots fired along the deck
    for (auto& s : shots) if (s.life > 0) {
        Vector2 l = boat.ToDeck({s.p.x, s.p.y});
        for (size_t i = 0; i < drowned.size(); i++) if (Vector2Distance(l, drowned[i].p) < 0.8f) { s.life = -1; HitDrowned(drowned[i].p, s.kind == Shot::Pellet ? 8.0f : 25.0f, 0.1f); break; }
    }
}

// E beside a hand the Angler has lured: shake them out of it (called from FreeTangled's place in TakeStation)
static bool ShakeLured(Gannet& g, int ci) {
    if (g.angler.lured < 0 || g.angler.lured == ci) return false;
    const Crew& me = g.crew[ci]; const Crew& o = g.crew[g.angler.lured];
    if (me.deck != 0 || o.deck != 0 || Vector2Distance(me.p, o.p) > 1.4f) return false;
    g.angler.lured = -1; g.angler.t = 0; g.Say("Shaken by the shoulder, the hand blinks and backs away from the rail");
    return true;
}
bool GrottoShake(Gannet& g, int ci) { return ShakeLured(g, ci); }

// ---------------------------------------------------------------- depth.exe --trawl-grotto-test
int RunTrawlGrottoTest() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Trawl: the Grotto\n");
    const float dt = 1 / 60.0f;
    Eco e; if (!e.Init("grotto", 5151)) { printf("  FAIL  the Grotto won't load\n"); return 1; }
    float size = e.n * e.cell;
    // the chart: open water, the headland, the arch, the cave behind it
    Vector2 arch{(e.archX0 + e.archX1) / 2, e.archY}, cave{430, size * 0.5f}, wall{e.archX0 + 10, e.archY + e.archHalf + 20};
    check(e.DepthAt(arch) > 4 && e.DepthAt(cave) > 15 && e.DepthAt(wall) <= 0, TextFormat("the chart: the arch has %.0f m of water, the cave %.0f m, the headland beside the arch is rock", e.DepthAt(arch), e.DepthAt(cave)));
    int wrecks = 0, walls = 0; for (int i = 0; i < e.n * e.n; i++) { if (e.hab[i] == H_WRECK) wrecks++; if (e.hab[i] == H_WALL) walls++; }
    check(wrecks > 10 && walls > 100 && e.landingAt.size() == 2 && e.marks.size() == 2, TextFormat("wrecks (%d cells), mould walls (%d cells), two landings and two skiff marks", wrecks, walls));
    e.archOpen = false;
    check(e.DepthAt(arch) <= 0, "the arch closed by the tide: no water under her keel");
    e.archOpen = true;
    // the echo
    {
        Eco a; a.Init("grotto", 5152); Eco b; b.Init("lagoon", 5152);
        Vector3 p{cave.x, cave.y, 1}, q{200, 300, 1};
        float s0 = a.sound.Total(); a.AddNoise(p, 10); float s1 = a.sound.Total();
        float t0 = b.sound.Total(); b.AddNoise(q, 10); float t1 = b.sound.Total();
        check(fabsf((s1 - s0) - 2 * (t1 - t0)) < 0.5f, TextFormat("sound doubles in the Grotto (%.1f vs %.1f)", s1 - s0, t1 - t0));
    }
    auto fresh = [&](Gannet& g, int n) { g.Init(n, 99); g.eco = &e; g.boat.pos = cave; g.boat.telegraph = 0; g.anglerCool = g.drownedCool = 1e9f; e.stirOverride = 0.6f; };
    // the stalactites
    {
        Gannet g; fresh(g, 1);
        float hp0 = 0; for (int s = 0; s < SEC_COUNT; s++) hp0 += g.boat.integrity[s];
        e.rockfalls.push_back(g.boat.ToWorld({0, 1})); g.Step(dt);
        float hp1 = 0; for (int s = 0; s < SEC_COUNT; s++) hp1 += g.boat.integrity[s];
        check(hp1 < hp0 - 10, "a stalactite brought down on her holes the deck plating");
        int falls = 0; for (int i = 0; i < 200; i++) { e.AddNoise({g.boat.pos.x, g.boat.pos.y, 1}, 60); falls += (int)e.rockfalls.size(); e.rockfalls.clear(); }
        check(falls > 20, TextFormat("a loud noise in the cave brings stalactites down (%d in 200 shots)", falls));
    }
    // the Lantern Angler
    {
        Gannet g; fresh(g, 2); g.anglerCool = 0; g.boat.lantern = 2;
        g.crew[0].p = {-3, 1.5f}; g.crew[1].p = {-3, -1.5f};
        for (int i = 0; i < 60 * 900 && !g.angler.on; i++) g.StepGrotto(dt);
        bool came = g.angler.on;
        for (int i = 0; i < 60 * 7; i++) g.StepGrotto(dt);
        int lured = g.angler.lured;
        check(came && lured >= 0, "a second light off the rail; after a few seconds it lures the nearest hand on that side");
        for (int i = 0; i < 60 * 10 && !g.crew[lured].overboard; i++) g.StepGrotto(dt);
        check(g.crew[lured].overboard, "left to it, the hand walks to the rail and is pulled over");
        Gannet h; fresh(h, 2); h.angler.on = true; h.angler.t = 7; h.angler.p = h.boat.ToWorld({0, 15}); h.angler.lured = 0;
        h.crew[0].p = {-3, 1.0f}; h.crew[1].p = {-3, 0.0f};
        check(GrottoShake(h, 1) && h.angler.lured < 0, "a hand beside them shakes them out of it");
        h.flares.push_back({h.angler.p, 20}); h.StepGrotto(dt);
        check(!h.angler.on, "a flare burst blinds it");
        Gannet k; fresh(k, 1); k.angler.on = true; k.angler.t = 1; k.angler.p = k.boat.ToWorld({0, 15}); k.boat.lantern = 0;
        for (int i = 0; i < 60 * 5; i++) k.StepGrotto(dt);
        check(!k.angler.on, "hooding the lantern sends it away");
    }
    // the Ghost Worm
    {
        Gannet g; fresh(g, 1); g.net.state = NetState::Down; g.boat.shaft = 0.6f; g.boat.lantern = 2;
        for (int i = 0; i < 60 * 300 && g.worm.state == 0; i++) { g.boat.shaft = 0.6f; g.StepGrotto(dt); }
        check(g.worm.state == 1, "the net down and the screw turning in the cave wake a Ghost Worm: it circles, and the lines hum");
        for (int i = 0; i < 60 * 17 && g.net.state == NetState::Down; i++) { g.boat.shaft = 0.6f; g.StepGrotto(dt); }
        check(g.net.state == NetState::Snagged, "then it takes the net");
        g.net.state = NetState::Stowed;
        for (int i = 0; i < 60 * 12 && g.worm.state != 3; i++) { g.boat.shaft = 0.6f; g.StepGrotto(dt); }
        check(g.worm.state == 3, "with nothing else to take and the screw turning, it coils the hull");
        for (int i = 0; i < 60 * 10; i++) { g.boat.shaft = 0; g.StepGrotto(dt); }
        check(g.worm.state == 0, "stop the screw: it slackens off and sinks away");
        Gannet h; fresh(h, 1); h.worm.state = 2; h.worm.actT = 100; h.boat.shaft = 0.6f;
        for (int n = 0; n < 2; n++) { Projectile p; p.kind = Shot::Harpoon; p.life = 1; h.StepGrotto(dt); p.p = {h.worm.p.x, h.worm.p.y, 0}; h.shots.push_back(p); h.StepGrotto(dt); h.shots.clear(); }
        check(h.worm.state == 0, "two harpoons in the head kill it");
    }
    // the isopods
    {
        Gannet g; fresh(g, 1);
        for (int i = 0; i < 3; i++) { CatchRec f; f.name = "pale cod"; f.kg = 3; f.price = 4; f.dead = true; f.deckAt = {-4.0f + i, 0}; g.hold.push_back(f); }
        g.crew[0].p = {2, 0};
        for (int i = 0; i < 60 * 60 && g.isopods.state != 2; i++) g.StepGrotto(dt);
        check(g.isopods.state == 2, "ungutted fish on the deck: clicking on the anchor chain, then the isopods come aboard");
        for (int i = 0; i < 60 * 13; i++) g.StepGrotto(dt);
        check(g.hold.size() < 3, TextFormat("they strip the catch on the deck (%d of 3 left)", (int)g.hold.size()));
        int n0 = g.isopods.n; g.HitDrowned(g.crew[0].p, 12, 1.6f);
        check(g.isopods.n == std::max(0, n0 - 5), "a stamp kills five");
        g.hold.clear(); g.deckBlood = 0;
        for (int i = 0; i < 60 * 30 && g.isopods.state != 0; i++) g.StepGrotto(dt);
        check(g.isopods.state == 0, "a clean deck starves them off");
    }
    // the Drowned
    {
        Gannet g; fresh(g, 2); g.drownedCool = 0; g.boat.lantern = 2;
        Vector2 wr{}; bool found = false;
        for (int i = 0; i < e.n * e.n && !found; i++) if (e.hab[i] == H_WRECK) { wr = {(i % e.n + 0.5f) * e.cell, (i / e.n + 0.5f) * e.cell}; found = true; }
        g.boat.pos = Vector2Add(wr, {0, 20}); if (e.DepthAt(g.boat.pos) <= 2) g.boat.pos = Vector2Add(wr, {20, 0});
        g.crew[0].p = {0, 0}; g.crew[1].p = {6, 0};
        for (int i = 0; i < 60 * 900 && g.drowned.empty(); i++) g.StepGrotto(dt);
        check(!g.drowned.empty(), "lying by a wreck with her lantern lit: knocking on the hull, then a Drowned sailor over the rail");
        for (int i = 0; i < 60 * 30 && !g.drowned.empty() && g.drowned[0].grab < 0; i++) g.StepGrotto(dt);
        check(!g.drowned.empty() && g.drowned[0].grab >= 0, "it walks at the nearest hand and grabs them");
        int who = g.drowned.empty() ? 0 : g.drowned[0].grab;
        Gannet h = g;
        for (int i = 0; i < 60 * 15 && !g.crew[who].overboard; i++) g.StepGrotto(dt);
        check(g.crew[who].overboard, "and drags them to the rail and over");
        for (int n = 0; n < 4 && !h.drowned.empty(); n++) h.HitDrowned(h.drowned[0].p, 14, 1.0f);
        check(h.drowned.empty() && h.crew[who].heldT <= 1, "hard blows put it down");
    }
    // the arch on the night's clock, and the landings
    {
        Gannet g; Session s; Eco se; g.Init(1, 7); s.plainNights = true; s.Begin(g, se, 1, 7);
        s.SetGround("grotto");
        g.crew[0].p = {3, 0.8f};
        while (g.boat.bunker < 80 && s.Buy("coal")) {}
        s.CastOff();
        bool posted = false; for (const auto& t : s.tape) if (t.find("ARCH CLOSES") != std::string::npos) posted = true;
        check(posted && s.archCloseAt >= 390 && s.archCloseAt <= 480, TextFormat("the telegraph posts the arch's closing time (%.0f minutes after 20:00)", s.archCloseAt));
        g.eco = &se; g.BuildLandings();   // (cast off, the web joins at the harbour line: attach it for the landings)
        check(g.landings.size() == 2 && g.landings[0].kind == LK_SHELF && g.landings[1].kind == LK_BONEBEACH, "the Smugglers' Shelf and Bone Beach");
        if (g.landings.size() == 2) {
            Landing& sh = g.landings[0]; Landing& bb = g.landings[1];
            int stash = 0; for (const auto& c : sh.caches) if (c.value >= 200 && c.value <= 600) stash++;
            check(stash >= 2, TextFormat("the Shelf has %d smugglers' stashes (200-600)", stash));
            Crew& c = g.crew[0]; g.skiff.state = SkiffState::Beached; g.skiff.landing = 0; c.deck = DECK_SHORE; c.p = Vector2Add(sh.elder, {1, 0.4f});
            CatchRec junk; junk.name = "oil lantern"; junk.junk = true; junk.kg = 1.5f; junk.price = 10; junk.dead = true;
            c.carrying = true; c.carry = junk; float m0 = s.money;
            float v = s.ElderGive(0);
            check(v > 9.9f && s.money - m0 > 9.9f, "the quartermaster buys salvage at its full worth, in shillings");
            s.money = 400; c.slots[3] = Slot{};
            bool bought = s.ElderBuy(0, "airpistol");
            check(bought && s.money < 400, "and sells his own goods (an air pistol) for shillings");
            g.skiff.landing = 1; c.p = Vector2Add(bb.elder, {1, 0.4f});
            c.carrying = true; c.carry = bb.onBeach.empty() ? junk : bb.onBeach[0];
            float cr = s.ElderGive(0);
            check(cr > 0 && bb.elderCredit > 0, "the hermit takes bones in trade");
            CatchRec fish; fish.name = "pale cod"; fish.kg = 3; fish.price = 4; fish.dead = true; fish.deckAt = Vector2Add(bb.fire, {0.2f, 0}); fish.cookT = 0;
            bb.onFire.push_back(fish);
            g.sea.weather = Weather::Rain;
            for (int i = 0; i < 60 * 60; i++) g.StepLandings(dt);
            check(bb.fireLit && bb.onFire[0].cook > 1.45f, TextFormat("the vents cook in the rain and never burn (x%.2f after a minute)", bb.onFire[0].cook));
        }
    }
    e.stirOverride = -1;
    printf(fails ? "trawl-grotto-test: %d FAILED\n" : "trawl-grotto-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}
} // namespace tw
