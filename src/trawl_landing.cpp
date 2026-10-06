// The landings (design doc v2, "Skiff destinations" and "Islands: fires, traders, and treasure"): small islands the
// skiff runs up on, walked on foot. The Atoll first: a palm islet with an open fire pit (rain puts it out), the
// tribe's elder (fish only, at 150% of their value, for goods sold nowhere else; closed to crews who fought the
// canoes), a beached smuggler sloop with a locked strongbox (a brass key from junk opens it), one more cache (a sea
// chest under the palms, or one buried where a bottle's map or three chart pieces mark it), crabs on the beach and a
// moray in its little lagoon. Cooking happens only here (the Owners forbid open flame aboard). Everything carried is
// carried one thing at a time, in the arms.
#include "trawl.h"
#include "trawl_eco.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tw {

namespace {
uint32_t gLr = 99;
float LR() { gLr = gLr * 1664525u + 1013904223u; return (gLr >> 8) * (1.0f / 16777216.0f); }
bool Raining(const Sea& s) { return s.weather == Weather::Rain || s.weather == Weather::Squall || s.weather == Weather::Storm; }
const float REACH = 1.4f;
}

float CookMultiplier(float kg, float t) {
    float T = 10 + kg;
    if (t <= 0) return 1;
    if (t < T) return 1 + 0.5f * t / T;
    if (t < T + 5) return 1.5f;
    if (t < T + 10) return 1.5f - 1.2f * (t - T - 5) / 5;
    return 0.3f;
}

void Gannet::BuildLandings() {
    landings.clear();
    if (!eco) return;
    for (size_t k = 0; k < eco->landingAt.size(); k++) {
        int kind = k < eco->landingKind.size() ? eco->landingKind[k] : LK_ATOLL;
        if (kind != LK_ATOLL) {
            Landing L; L.kind = kind; L.at = eco->landingAt[k];
            gLr = eco->initSeed * 2654435761u + 31 + (uint32_t)k * 977;
            if (kind == LK_LIGHTHOUSE) {
                // (the Lagoon) a bare rock on the reef inside the crest: the old lighthouse tower (the keeper's hearth at
                // its foot, sheltered), his strongbox in the tower (100-250, locked: a brass key), his logbook on the
                // gallery steps (it marks where the Sandbar's chests are buried), and the great lens, cracked out of
                // its frame (salvage: 3 kg, breaks if it's dropped)
                L.name = "The Old Lighthouse rock"; L.r = 8;
                L.sloop = {0.0f, -2.6f}; L.sloopHead = 0; L.fire = {1.6f, 1.8f}; L.elder = {999, 999}; L.pond = {0, 0}; L.pondR = 0;
                Cache box; box.p = Vector2Add(L.sloop, {-1.4f, 0.2f}); box.kind = 1; box.value = 100 + LR() * 150; box.kg = 16 + LR() * 8; box.what = "the keeper's strongbox";
                L.caches.push_back(box);
                CatchRec log; log.name = "the keeper's logbook"; log.junk = true; log.kg = 1; log.price = 30; log.dead = log.gutted = log.iced = true; log.deckAt = {2.6f, 2.4f}; L.onBeach.push_back(log);
                CatchRec lens; lens.name = "the lighthouse lens"; lens.junk = true; lens.kg = 3; lens.price = 160 + LR() * 80; lens.dead = lens.gutted = lens.iced = true; lens.deckAt = {-2.8f, 2.6f}; L.onBeach.push_back(lens);
                for (int i = 0; i < 2; i++) { float a = LR() * 6.2832f; L.crabs.push_back({cosf(a) * (L.r - 1.0f), sinf(a) * (L.r - 1.0f)}); }
            } else if (kind == LK_SANDBAR) {
                // (the Lagoon) a bar of bare sand: no fire, no trader; 1-3 chests buried in it (80-300), found from a
                // bottle's map, three chart pieces or the keeper's logbook; the tide makes over it at 02:00
                L.name = "The Sandbar"; L.r = 10;
                L.sloop = {999, 999}; L.sloopHead = 0; L.fire = {999, 999}; L.fireLit = false; L.elder = {999, 999}; L.pond = {0, 0}; L.pondR = 0;
                int n = 1 + (int)(LR() * 3);
                for (int i = 0; i < n; i++) {
                    float a = 0.6f + i * 2.1f + LR() * 0.6f, rr = 3 + LR() * 4.5f;
                    Cache c; c.p = {cosf(a) * rr, sinf(a) * rr}; c.kind = 2; c.found = false; c.value = 80 + LR() * 220; c.kg = 14 + LR() * 18; c.what = "a buried chest";
                    L.caches.push_back(c);
                }
                for (int i = 0; i < 3; i++) { float a = LR() * 6.2832f; L.crabs.push_back({cosf(a) * (L.r - 1.2f), sinf(a) * (L.r - 1.2f)}); }
            } else if (kind == LK_STAIR) {
                // (Atlantis) a broad stairway climbing out of the terraces to a landing of white stone: the eternal
                // brazier (cooks twice as fast, +2 Wake a fish), the Keeper of the Stair (a drowned priest, harmless
                // while he is paid in offerings), offering bowls (400-1200) that he lets go for an offering
                L.name = "The Drowned Stair"; L.r = 9;
                L.sloop = {0.0f, 4.0f}; L.sloopHead = 0; L.fire = {0.0f, -1.5f}; L.elder = {-3.5f, 1.5f}; L.pond = {0, 0}; L.pondR = 0;
                for (int i = 0; i < 2; i++) { Cache b; b.p = {3.5f + i * 1.6f, -3.5f + i * 3.0f}; b.kind = 0; b.value = 400 + LR() * 800; b.kg = 10 + LR() * 6; b.what = "an offering bowl"; L.caches.push_back(b); }
            } else if (kind == LK_TOWER) {
                // the Watchtower stump: a signal fire (unlit; lighting it shows every skiff mark on the sonar and draws
                // everything on the ground), no trader, the tower cache (300-800, locked)
                L.name = "The Watchtower stump"; L.r = 8;
                L.sloop = {0.0f, 0.0f}; L.sloopHead = 0; L.fire = {3.0f, 2.5f}; L.elder = {999, 999}; L.pond = {0, 0}; L.pondR = 0; L.fireLit = false;
                Cache c; c.p = {-1.6f, 1.2f}; c.kind = 1; c.value = 300 + LR() * 500; c.kg = 24; c.what = "the tower cache"; L.caches.push_back(c);
            } else if (kind == LK_CULT) {
                // the Cult Landing: the cult bonfire (fast; the longboats come back), the cult quartermaster (dark goods
                // for dark money), the cult's hoard (500-1200, guarded: taking it brings the longboats)
                L.name = "The Cult Landing"; L.r = 10;
                L.sloop = {2.5f, -4.0f}; L.sloopHead = 0.4f; L.fire = {-1.0f, 0.5f}; L.elder = {3.5f, 2.5f}; L.pond = {0, 0}; L.pondR = 0;
                Cache h; h.p = {Vector2Add({2.5f, -4.0f}, {0.6f, 0.2f})}; h.kind = 0; h.value = 500 + LR() * 700; h.kg = 30; h.what = "the cult's hoard"; L.caches.push_back(h);
            } else if (kind == LK_SHELF) {
                // a rock shelf on the cave's north wall: the smugglers' lean-to of crates (stove at its mouth), the
                // quartermaster among his crates, 2-3 stashes (200-600), one of them under a lock
                L.name = "The Smugglers' Shelf"; L.r = 10;
                L.sloop = {0.0f, -4.6f}; L.sloopHead = 0.0f; L.fire = {2.6f, -1.6f}; L.elder = {-3.4f, -1.2f};
                L.pond = {0, 0}; L.pondR = 0;
                int n = 2 + (LR() < 0.5f ? 1 : 0);
                for (int i = 0; i < n; i++) { Cache s; s.p = {-5.0f + i * 4.5f, 3.0f + LR() * 2}; s.kind = i == 0 ? 1 : 0; s.value = 200 + LR() * 400; s.kg = 12 + LR() * 14; s.what = i == 0 ? "a smugglers' locked stash" : "a smugglers' stash"; L.caches.push_back(s); }
            } else if (kind == LK_BONEBEACH) {
                // a black-sand beach on the south wall: volcanic vents (steam: fish cook 30% slower and never burn), the
                // hermit, bone piles and a lost crew's kit (200-500)
                L.name = "Bone Beach"; L.r = 11;
                L.sloop = {-4.0f, 4.2f}; L.sloopHead = 0.3f; L.fire = {2.8f, 1.0f}; L.elder = {-1.0f, -3.4f};
                L.pond = {0, 0}; L.pondR = 0;
                Cache kit; kit.p = {5.0f, -4.0f}; kit.kind = 0; kit.value = 200 + LR() * 300; kit.kg = 20 + LR() * 10; kit.what = "a lost crew's kit"; L.caches.push_back(kit);
                for (int i = 0; i < 3; i++) { float a = 1.2f + i * 1.7f; CatchRec b; b.name = i == 2 ? "an old skull" : "a bundle of bones"; b.junk = true; b.kg = 2 + i; b.price = 25 + LR() * 30; b.dead = b.gutted = b.iced = true; b.deckAt = {cosf(a) * 6.5f, sinf(a) * 6.5f}; L.onBeach.push_back(b); }
            } else if (kind == LK_SEALROCK) {
                // a bare rock: the sealers' hut (stove at its door, Old Hoskins on the step), the bull seal's haul-out
                L.name = "Seal Rock"; L.r = 11;
                L.sloop = {-3.5f, -4.0f}; L.sloopHead = 0.2f; L.fire = {-0.6f, -2.6f}; L.elder = {-5.4f, -1.6f};
                L.pond = {3.6f, 3.4f}; L.pondR = 3.0f;
                Cache box; box.p = Vector2Add(L.sloop, {0.8f, -0.4f}); box.kind = 0; box.value = 100 + LR() * 250; box.kg = 14 + LR() * 12; box.what = "the sealers' sea chest";
                L.caches.push_back(box);
                Cache bur; bur.p = {5.5f, -3.5f}; bur.kind = 2; bur.found = false; bur.value = 100 + LR() * 250; bur.kg = 16 + LR() * 18; bur.what = "a buried tin trunk";
                L.caches.push_back(bur);
                for (int i = 0; i < 3; i++) { float a = LR() * 6.2832f; L.crabs.push_back({cosf(a) * (L.r - 1.2f), sinf(a) * (L.r - 1.2f)}); }
            } else {
                // a round loading stage on pilings: the cannery shed across its middle, the boiler, the last foreman; the
                // safe in the shed (locked: a brass key)
                L.name = "The Cannery Pier"; L.r = 10;
                L.sloop = {0.0f, -3.4f}; L.sloopHead = 0.0f; L.fire = {3.8f, -0.4f}; L.elder = {-3.6f, 0.4f};
                L.pond = {0, 0}; L.pondR = 0;
                Cache safe; safe.p = Vector2Add(L.sloop, {-1.6f, 1.2f}); safe.kind = 1; safe.value = 150 + LR() * 200; safe.kg = 30; safe.what = "the cannery safe";
                L.caches.push_back(safe);
            }
            L.moray = L.pond;
            landings.push_back(L);
            continue;
        }
        Landing L; L.name = "The Atoll"; L.at = eco->landingAt[k];
        gLr = eco->initSeed * 2654435761u + 17;
        // palms round the shore and the pond, clear of the fire, the hut and the sloop
        for (int i = 0; i < 40 && L.palms.size() < 11; i++) {
            float a = LR() * 6.2832f, rr = 3 + LR() * (L.r - 4.2f);
            Vector2 p{cosf(a) * rr, sinf(a) * rr};
            bool clear = Vector2Distance(p, L.fire) > 2.4f && Vector2Distance(p, L.elder) > 2.6f && Vector2Distance(p, L.sloop) > 3.6f && Vector2Distance(p, L.pond) > L.pondR + 0.8f;
            for (const auto& q : L.palms) if (Vector2Distance(p, q) < 2.0f) clear = false;
            if (clear) L.palms.push_back(p);
        }
        // treasure: the sloop's strongbox (locked), and either a sea chest under the palms or a buried cache
        Cache box; box.p = Vector2Add(L.sloop, {0.4f, 0.3f}); box.kind = 1; box.value = 50 + LR() * 150; box.kg = 14 + LR() * 10; box.what = "the smuggler's strongbox";
        L.caches.push_back(box);
        if (LR() < 0.5f) { Cache c; c.p = {-1.5f + LR() * 3, -8.5f}; c.kind = 0; c.value = 50 + LR() * 150; c.kg = 12 + LR() * 18; c.what = "a sea chest"; L.caches.push_back(c); }
        else { Cache c; c.p = {7.5f, 3.5f + LR() * 2}; c.kind = 2; c.found = false; c.value = 80 + LR() * 120; c.kg = 18 + LR() * 20; c.what = "a buried chest"; L.caches.push_back(c); }
        for (int i = 0; i < 4; i++) { float a = LR() * 6.2832f; L.crabs.push_back({cosf(a) * (L.r - 1.2f), sinf(a) * (L.r - 1.2f)}); }
        L.moray = L.pond;
        landings.push_back(L);
    }
}

int Gannet::LandingNear(Vector2 w, float extra) const {
    for (int i = 0; i < (int)landings.size(); i++) if (Vector2Distance(w, landings[i].at) < landings[i].r + extra) return i;
    return -1;
}

// ashore: sand inside the shore, round the palms and the sloop's hull; the pond is wading (half speed)
static bool ShoreWalkable(const Landing& L, Vector2 p) {
    if (Vector2Length(p) > L.r - 0.3f) return false;
    for (const auto& q : L.palms) if (Vector2Distance(p, q) < 0.35f) return false;
    Vector2 d = Vector2Subtract(p, L.sloop); float c = cosf(L.sloopHead), s = sinf(L.sloopHead);
    Vector2 ls{d.x * c + d.y * s, -d.x * s + d.y * c};
    if (L.kind == LK_ATOLL && fabsf(ls.x) < 2.5f && fabsf(ls.y) < 0.9f && !(ls.x < -1.6f)) return false;   // (her stern is stove in: a gap to climb aboard)
    // the hut and the shed: walls, with a door on the side facing the middle (the chest and the safe are inside)
    if (L.kind != LK_ATOLL && fabsf(ls.x) < 2.6f && fabsf(ls.y) < 1.6f && !(fabsf(ls.x) < 0.7f && ls.y > 0.6f) && !(fabsf(ls.x) < 2.2f && fabsf(ls.y) < 1.2f)) return false;
    if (Vector2Distance(p, L.fire) < 0.45f) return false;
    if (Vector2Distance(p, L.elder) < 0.5f) return false;
    return true;
}
void Gannet::ShoreMove(int ci, Vector2 wish, float dt) {
    Crew& c = crew[ci];
    if (c.dead || c.overboard || c.deck != DECK_SHORE || skiff.landing < 0 || skiff.landing >= (int)landings.size()) return;
    const Landing& L = landings[skiff.landing];
    float l = Vector2Length(wish);
    if (l > 1) wish = Vector2Scale(wish, 1 / l);
    // the wish is in the Gannet's frame (the screen's): turned onto the sea, which the landing's frame shares
    Vector2 f = boat.Forward(), sd{-f.y, f.x};
    Vector2 w{f.x * wish.x + sd.x * wish.y, f.y * wish.x + sd.y * wish.y};
    if (c.tangleT > 0) { w = {0, 0}; c.v = {0, 0}; }   // (a Kelp Wraith from the pilings has them)
    if (l > 0.1f && c.tangleT <= 0) c.facing = Vector2Normalize(wish);
    float speed = D().walk * (c.carryKg > 30 ? 0.5f : c.carryKg > 10 ? 0.75f : 1.0f) * (Vector2Distance(c.p, L.pond) < L.pondR ? 0.5f : 1.0f) * (c.sprint ? 1.7f : 1.0f);
    c.v = Vector2Lerp(c.v, Vector2Scale(w, speed), std::min(1.0f, dt * 12));
    Vector2 np = Vector2Add(c.p, Vector2Scale(c.v, dt));
    if (ShoreWalkable(L, np)) c.p = np;
    else if (ShoreWalkable(L, {np.x, c.p.y})) c.p.x = np.x;
    else if (ShoreWalkable(L, {c.p.x, np.y})) c.p.y = np.y;
    else c.v = {0, 0};
    if (Vector2Length(c.v) > 0.4f) { c.workOn = -1; c.workT = 0; }   // (walking off stops the digging)
}

bool Gannet::BeachSkiff(int ci) {
    Crew& c = crew[ci];
    Skiff& s = skiff;
    if (c.deck != DECK_SKIFF || c.overboard || !(s.state == SkiffState::Afloat || s.state == SkiffState::Beached)) return false;
    int li = s.state == SkiffState::Beached ? s.landing : LandingNear(s.p, 3.5f);
    if (li < 0) return false;
    Landing& L = landings[li];
    if (L.flooded) { Say("The Sandbar is under the tide: nothing to land on"); return false; }
    Vector2 out = Vector2Subtract(s.p, L.at); float d = Vector2Length(out);
    out = d > 0.01f ? Vector2Scale(out, 1 / d) : Vector2{1, 0};
    if (s.state == SkiffState::Afloat) {
        // run her up on the sand, bow to the shore
        s.state = SkiffState::Beached; s.landing = li; s.vel = {0, 0}; s.yawRate = 0; s.roll = 0; s.rollV = 0;
        s.p = Vector2Add(L.at, Vector2Scale(out, L.r + 1.2f)); s.heading = atan2f(-out.y, -out.x);
        Say("The skiff runs up on the sand");
    }
    c.deck = DECK_SHORE; c.p = Vector2Scale(out, L.r - 1.0f); c.v = {0, 0};
    Say(TextFormat("Ashore on %s", L.name.c_str()));
    return true;
}

// The Sandbar floods (02:00): the bar goes under. Whoever is still on it is in the water (the skiff floats off with
// them if she's there), and whatever lies on it - fish, salvage, chests not dug - is gone with the tide.
void Gannet::FloodSandbar(int li) {
    if (li < 0 || li >= (int)landings.size() || landings[li].flooded) return;
    Landing& L = landings[li];
    L.flooded = true;
    L.onBeach.clear(); L.onFire.clear(); L.crabs.clear();
    for (auto& k : L.caches) k.open = true;   // (what wasn't dug is lost)
    if (skiff.landing == li && skiff.state == SkiffState::Beached) { skiff.state = SkiffState::Afloat; skiff.landing = -1; }
    for (int i = 0; i < (int)crew.size(); i++) {
        Crew& c = crew[i];
        if (c.dead || c.deck != DECK_SHORE) continue;
        if (skiff.landing >= 0 && skiff.landing != li) continue;
        Vector2 w = L.ToWorld(c.p);
        c.carrying = false; c.carryKg = 0;
        c.deck = 0; c.v = {0, 0};
        GoOverboard(i, "the Sandbar went under");
        c.swim = w;   // (in the water where they stood)
    }
    Say("The tide makes over the Sandbar: the bar is gone under the water");
}

// E ashore: in order, the beached skiff (load what you carry, or unload her), the fire, a cache, the elder is the
// screen's panel (not here), something lying on the beach, a crab; carrying with nothing near, set it down
bool Gannet::ShoreUse(int ci) {
    Crew& c = crew[ci];
    if (c.dead || c.deck != DECK_SHORE || skiff.landing < 0 || skiff.landing >= (int)landings.size()) return false;
    Landing& L = landings[skiff.landing];
    if (FreeTangled(ci)) return true;   // (the Cannery Pier's pilings: a hand caught at the edge)
    auto take = [&](const CatchRec& r) { c.carrying = true; c.carry = r; c.carryKg = r.kg; };
    auto drop = [&]() { c.carrying = false; c.carryKg = 0; };
    // the skiff
    Vector2 sk = Vector2Subtract(skiff.p, L.at);
    if (skiff.state == SkiffState::Beached && Vector2Distance(c.p, sk) < 2.8f) {
        if (c.carrying) { if (SkiffLand(c.carry)) { Say(TextFormat("Into the skiff: %s", c.carry.name.c_str())); drop(); } return true; }
        if (!skiff.load.empty()) {
            int best = 0; for (int i = 0; i < (int)skiff.load.size(); i++) if (!skiff.load[i].junk && (skiff.load[best].junk || skiff.load[i].kg > skiff.load[best].kg)) best = i;
            take(skiff.load[best]); skiff.load.erase(skiff.load.begin() + best);
            Say(TextFormat("Out of the skiff: %s", c.carry.name.c_str()));
            return true;
        }
    }
    // the fire: a fish on, a fish off, or light it again
    if (Vector2Distance(c.p, L.fire) < 1.7f) {
        if (!L.fireLit) {
            if (Raining(sea) && L.kind == LK_ATOLL) { Say("The rain beats the fire out as fast as it catches"); return true; }
            c.workOn = 100; c.workT = 0; Say("Relighting the fire (10 s with dry kindling: stand by it)"); return true;
        }
        if (c.carrying && !c.carry.junk) {
            if (L.onFire.size() >= 4) { Say("The fire takes four at a time"); return true; }
            CatchRec r = c.carry; r.deckAt = Vector2Add(L.fire, {(float)L.onFire.size() * 0.3f - 0.45f, 0.2f}); if (r.cookT < 0) r.cookT = 0;
            L.onFire.push_back(r); drop();
            if (L.kind == LK_STAIR && eco) eco->wake += 2;   // (the eternal brazier: +2 Wake a fish)
            Say(TextFormat("On the fire: %s", r.name.c_str()));
            return true;
        }
        if (!c.carrying && !L.onFire.empty()) {
            // off the fire: the one that's furthest along (the one most at risk of burning)
            int best = 0; for (int i = 0; i < (int)L.onFire.size(); i++) if (L.onFire[i].cookT - 10 - L.onFire[i].kg > L.onFire[best].cookT - 10 - L.onFire[best].kg) best = i;
            CatchRec r = L.onFire[best]; L.onFire.erase(L.onFire.begin() + best);
            r.cooked = true; r.fresh = std::max(r.fresh, 0.0f);
            take(r);
            Say(TextFormat("Off the fire: %s, %s (x%.2f)", r.name.c_str(), r.cook >= 1.45f ? "done to a turn" : r.cook < 1 ? "burnt" : "underdone", r.cook));
            return true;
        }
    }
    // the caches
    for (int i = 0; i < (int)L.caches.size(); i++) {
        Cache& k = L.caches[i];
        if (k.open || Vector2Distance(c.p, k.p) > REACH + 0.3f) continue;
        if (c.carrying) { Say("Your arms are full"); return true; }
        if (k.kind == 2 && !k.found) continue;   // (nothing marks it yet)
        if (L.kind == LK_STAIR) {   // the Keeper of the Stair: harmless while he is paid in offerings (100 of fish a bowl)
            if (L.elderCredit < 100) { Say("The Keeper of the Stair lifts a hand: an offering first (give him fish)"); return true; }
            L.elderCredit -= 100;
        }
        if (L.kind == LK_CULT && k.what == "the cult's hoard") { cultRaid = true; Say("Torches move on the water: the cult saw you take it"); }
        if (k.kind == 1) {
            if (junkKeys <= 0) { Say("Locked: a brass key would open it"); return true; }
            junkKeys--; Say("The brass key turns in the strongbox's lock");
        }
        if (k.kind == 2) { c.workOn = i; c.workT = 0; Say("Digging (stand at the mark: 5 s)"); return true; }
        k.open = true;
        CatchRec r; r.name = k.what; r.kg = k.kg; r.price = k.value; r.junk = true; r.dead = true; r.gutted = r.iced = true;
        take(r);
        Say(TextFormat("You heave up %s (%.0f kg): carry it to the skiff", k.what.c_str(), k.kg));
        return true;
    }
    // a bottle's map or three chart pieces mark the buried cache: read here, ashore
    for (auto& k : L.caches) if (k.kind == 2 && !k.found && (junkBottles > 0 || junkCharts >= 3)) {
        if (junkBottles > 0) junkBottles--; else junkCharts -= 3;
        k.found = true;
        Say("The map marks a spot on this beach: an X by the palms (dig there)");
        return true;
    }
    // something on the beach
    if (!c.carrying) {
        for (int i = 0; i < (int)L.onBeach.size(); i++) if (Vector2Distance(c.p, L.onBeach[i].deckAt) < REACH) {
            take(L.onBeach[i]); L.onBeach.erase(L.onBeach.begin() + i); Say(TextFormat("Picked up: %s", c.carry.name.c_str()));
            if (c.carry.name == "the keeper's logbook") {
                // his last entries: where the wreckers buried their chests on the Sandbar
                int marked = 0;
                for (auto& B : landings) if (B.kind == LK_SANDBAR && !B.flooded) for (auto& k : B.caches) if (k.kind == 2 && !k.found) { k.found = true; marked++; }
                if (marked) Say(TextFormat("The keeper's last pages: %d chest%s buried on the Sandbar, the marks drawn in. It floods at 02:00", marked, marked == 1 ? "" : "s"));
            }
            return true;
        }
        for (int i = 0; i < (int)L.crabs.size(); i++) if (Vector2Distance(c.p, L.crabs[i]) < 1.0f) {
            int sp = Species().Find("blue crab");
            CatchRec r; r.name = "blue crab"; r.sp = sp; r.kg = 0.5f + LR() * 0.4f; r.price = sp >= 0 ? Species().sp[sp].price : 2; r.dead = true; r.src = CS_HOOK;
            take(r); L.crabs.erase(L.crabs.begin() + i);
            if (LR() < 0.3f) Injure(ci, INJ_HOOKED_HAND, "a crab's pinch");
            Say("Caught a crab by the shell");
            return true;
        }
        return false;
    }
    // set it down
    CatchRec r = c.carry; r.deckAt = Vector2Add(c.p, Vector2Scale(c.facing, 0.5f));
    L.onBeach.push_back(r); drop();
    Say(TextFormat("Set down on the sand: %s", r.name.c_str()));
    return true;
}

bool Gannet::EatCooked(int ci) {
    Crew& c = crew[ci];
    if (!c.carrying || !c.carry.cooked || c.carry.kg < 2 || c.carry.junk) return false;
    int minor[3] = {INJ_BURN, INJ_HOOKED_HAND, INJ_BITE};
    for (int m : minor) if (c.Has(m)) { c.injuries &= ~m; c.carrying = false; c.carryKg = 0; Say(TextFormat("A cooked %s, eaten on the beach: %s mends", c.carry.name.c_str(), InjuryName(m))); return true; }
    Say("Nothing to mend: the fish would only sell");
    return false;
}

void Gannet::StealFrom(std::vector<CatchRec>& v, int idx, Vector2 world, int kind) {
    if (idx < 0 || idx >= (int)v.size()) return;
    Thief t; t.kind = kind; t.fish = v[idx]; t.p = world; t.z = -0.6f;
    float a = LR() * 6.2832f; t.v = {cosf(a) * (5 - t.fish.kg * 0.4f), sinf(a) * (5 - t.fish.kg * 0.4f)};
    v.erase(v.begin() + idx);
    Say(TextFormat("A %s takes the %s: shoot it down!", BirdOf(kind).name, t.fish.name.c_str()));
    thieves.push_back(t);
}

void Gannet::StepLandings(float dt) {
    if (landings.empty() && eco && !eco->landingAt.empty()) BuildLandings();
    for (int li = 0; li < (int)landings.size(); li++) {
        Landing& L = landings[li];
        bool exposed = L.kind == LK_ATOLL || L.kind == LK_TOWER || L.kind == LK_CULT;   // (the stoves and the boiler are under a roof; vents and the eternal brazier don't care)
        if (exposed && L.fireLit && Raining(sea) && !L.onFire.empty()) Say("Rain puts the fire out");
        if (exposed && Raining(sea)) L.fireLit = false;
        // the fire: the fish cook (and burn); the smell into the water and the air (5 a second a fish, doubled burning)
        for (auto& r : L.onFire) {
            if (L.fireLit) r.cookT += dt * (spiceRub ? 1.25f : 1.0f) * (L.kind == LK_BONEBEACH ? 0.7f : L.kind == LK_STAIR ? 2.0f : L.kind == LK_CULT ? 1.5f : 1.0f);   // (the cook's spice rub: 25% faster; Bone Beach's steam 30% slower; the eternal brazier twice as fast; the cult bonfire fast)
            if (L.kind == LK_BONEBEACH) r.cookT = std::min(r.cookT, 10 + r.kg + 4.9f);   // (steam never burns: it holds at its best)
            r.cook = CookMultiplier(r.kg, r.cookT); r.cooked = r.cookT > 1;
            bool burning = r.cookT > 10 + r.kg + 5;
            if (eco) eco->AddBlood({L.at.x + L.fire.x, L.at.y + L.fire.y, 0.5f}, 5 * dt * (burning ? 2 : 1) * 0.1f);
        }
        // the crabs scuttle round the beach
        for (size_t i = 0; i < L.crabs.size(); i++) {
            Vector2& p = L.crabs[i];
            float a = atan2f(p.y, p.x) + dt * 0.05f * (i % 2 ? 1 : -1);
            float rr = L.r - 1.2f + 0.4f * sinf(time * 0.7f + i);
            p = {cosf(a) * rr, sinf(a) * rr};
        }
        // the moray: it hangs in the pond, and bites a hand wading near it (then sulks a while)
        L.morayT -= dt;
        L.moray = Vector2Add(L.pond, {cosf(time * 0.3f) * 1.4f, sinf(time * 0.4f) * 1.2f});
        for (int k = 0; k < (int)crew.size(); k++) {
            Crew& c = crew[k];
            if (c.deck != DECK_SHORE || c.dead || skiff.landing != li) continue;
            if (Vector2Distance(c.p, L.moray) < 1.3f && Vector2Distance(c.p, L.pond) < L.pondR && L.morayT <= 0) { Injure(k, INJ_BITE, L.kind == LK_SEALROCK ? "the bull seal on its haul-out" : "the moray in the Atoll's lagoon"); L.morayT = 15; }
            // work: digging a cache or relighting the fire, standing still at it
            if (c.workOn >= 0) {
                bool at = c.workOn == 100 ? Vector2Distance(c.p, L.fire) < 1.8f : c.workOn < (int)L.caches.size() && Vector2Distance(c.p, L.caches[c.workOn].p) < REACH + 0.3f;
                if (!at) { c.workOn = -1; c.workT = 0; continue; }
                c.workT += dt;
                if (c.workOn == 100 && c.workT >= 10 && L.kind == LK_TOWER && !L.fireLit) {
                    // the Watchtower's signal fire: every skiff mark on the sonar, and everything on the ground drawn to it
                    if (eco) { for (const auto& m : eco->marks) { SonarMark sm; sm.p = m.at; sm.t = 240; sm.what = m.name; sm.by = k; sonar.marks.push_back(sm); } eco->wake += 15; }
                    Say("The signal fire roars up: every mark on the ground shows on the sonar, and everything out there sees it");
                }
                if (c.workOn == 100 && c.workT >= 10) { L.fireLit = !exposed || !Raining(sea); Say(L.fireLit ? "The fire catches" : "The rain puts it out again"); c.workOn = -1; c.workT = 0; }
                else if (c.workOn < 100 && c.workT >= 5) {
                    Cache& kk = L.caches[c.workOn];
                    kk.open = true;
                    CatchRec r; r.name = kk.what; r.kg = kk.kg; r.price = kk.value; r.junk = true; r.dead = true; r.gutted = r.iced = true;
                    if (!c.carrying) { c.carrying = true; c.carry = r; c.carryKg = r.kg; } else { r.deckAt = kk.p; L.onBeach.push_back(r); }
                    Say(TextFormat("Dug up %s (%.0f kg)", kk.what.c_str(), kk.kg));
                    c.workOn = -1; c.workT = 0;
                }
            }
        }
    }
    // hands ashore when the skiff has gone without them are still ashore (left behind at the harbour line)
}

} // namespace tw
