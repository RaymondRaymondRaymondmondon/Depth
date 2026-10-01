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
    if (fabsf(ls.x) < 2.5f && fabsf(ls.y) < 0.9f && !(ls.x < -1.6f)) return false;   // (her stern is stove in: a gap to climb aboard)
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
    if (l > 0.1f) c.facing = Vector2Normalize(wish);
    float speed = D().walk * (c.carryKg > 30 ? 0.5f : c.carryKg > 10 ? 0.75f : 1.0f) * (Vector2Distance(c.p, L.pond) < L.pondR ? 0.5f : 1.0f);
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

// E ashore: in order, the beached skiff (load what you carry, or unload her), the fire, a cache, the elder is the
// screen's panel (not here), something lying on the beach, a crab; carrying with nothing near, set it down
bool Gannet::ShoreUse(int ci) {
    Crew& c = crew[ci];
    if (c.dead || c.deck != DECK_SHORE || skiff.landing < 0 || skiff.landing >= (int)landings.size()) return false;
    Landing& L = landings[skiff.landing];
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
            if (Raining(sea)) { Say("The rain beats the fire out as fast as it catches"); return true; }
            c.workOn = 100; c.workT = 0; Say("Relighting the fire (10 s with dry kindling: stand by it)"); return true;
        }
        if (c.carrying && !c.carry.junk) {
            if (L.onFire.size() >= 4) { Say("The fire takes four at a time"); return true; }
            CatchRec r = c.carry; r.deckAt = Vector2Add(L.fire, {(float)L.onFire.size() * 0.3f - 0.45f, 0.2f}); if (r.cookT < 0) r.cookT = 0;
            L.onFire.push_back(r); drop();
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
        for (int i = 0; i < (int)L.onBeach.size(); i++) if (Vector2Distance(c.p, L.onBeach[i].deckAt) < REACH) { take(L.onBeach[i]); L.onBeach.erase(L.onBeach.begin() + i); Say(TextFormat("Picked up: %s", c.carry.name.c_str())); return true; }
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
        if (L.fireLit && Raining(sea) && !L.onFire.empty()) Say("Rain puts the fire out");
        if (Raining(sea)) L.fireLit = false;
        // the fire: the fish cook (and burn); the smell into the water and the air (5 a second a fish, doubled burning)
        for (auto& r : L.onFire) {
            if (L.fireLit) r.cookT += dt;
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
            if (Vector2Distance(c.p, L.moray) < 1.3f && Vector2Distance(c.p, L.pond) < L.pondR && L.morayT <= 0) { Injure(k, INJ_BITE, "the moray in the Atoll's lagoon"); L.morayT = 15; }
            // work: digging a cache or relighting the fire, standing still at it
            if (c.workOn >= 0) {
                bool at = c.workOn == 100 ? Vector2Distance(c.p, L.fire) < 1.8f : c.workOn < (int)L.caches.size() && Vector2Distance(c.p, L.caches[c.workOn].p) < REACH + 0.3f;
                if (!at) { c.workOn = -1; c.workT = 0; continue; }
                c.workT += dt;
                if (c.workOn == 100 && c.workT >= 10) { L.fireLit = !Raining(sea); Say(L.fireLit ? "The fire catches" : "The rain puts it out again"); c.workOn = -1; c.workT = 0; }
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
