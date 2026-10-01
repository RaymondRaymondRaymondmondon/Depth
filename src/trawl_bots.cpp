// The Trawl's bot crew (design doc, "Bot crew"): a small utility brain per bot that picks a station from its role's
// watch, the skipper's orders and what is happening aboard, walks there (down the ladder, in by the wheelhouse door,
// round what it bumps into) and works it. Bots are good hands, not good captains: they never steer, never choose a
// course and never decide to go home. All of it runs inside the host's 60 Hz step (Gannet::Step), headless.
#include "trawl.h"
#include "trawl_eco.h"
#include "trawl_session.h"
#include "raymath.h"
#include <cstdio>
#include <algorithm>
#include <cmath>

namespace tw {

static const Vector2 LADDER_P{-3.4f, -1.8f};
static float BR(uint32_t& s) { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0f; }
static int StationOfKind(StationKind k) { const auto& S = Stations(); for (int i = 0; i < (int)S.size(); i++) if (S[i].kind == k) return i; return -1; }
static bool InWheelhouse(Vector2 p) { return p.x > 1.05f && p.x < 4.95f && fabsf(p.y) < 1.95f; }
static bool IsRod(StationKind k) { return k == StationKind::PortRod || k == StationKind::StarRod || k == StationKind::SternRodP || k == StationKind::SternRodS; }

// The next point on the way to a spot: the ladder if it's on the other deck, the wheelhouse door in or out.
static Vector2 Waypoint(const Crew& c, int deck, Vector2 at, bool* climb) {
    *climb = false;
    if (c.deck != deck) { if (Vector2Distance(c.p, LADDER_P) < 0.45f) *climb = true; return LADDER_P; }
    if (c.deck == 0) {
        bool inC = InWheelhouse(c.p), inT = InWheelhouse(at);
        if (inT && !inC) return c.p.x < 0.85f && (fabsf(c.p.y) > 0.4f || c.p.x < 0.4f) ? Vector2{0.7f, 0} : Vector2{1.7f, 0};
        if (!inT && inC) return c.p.x > 1.5f || fabsf(c.p.y) > 0.45f ? Vector2{1.4f, 0} : Vector2{0.5f, 0};
    }
    return at;
}

std::string Gannet::BotDoing(int ci) const {
    const Crew& c = crew[ci];
    if (c.dead) return "dead";
    if (c.overboard) return "in the water";
    if (c.fallen) return "down on the deck";
    if (c.tangleT > 0) return "caught in the kelp!";
    if (ci < (int)brains.size()) {
        if (brains[ci].task == 4) return "cutting a hand free";
        const Brain& b = brains[ci];
        if (b.task == 1) return "the life ring!";
        if (b.task == 2) return c.patchSec >= 0 ? TextFormat("patching (%.0f s)", std::max(0.0f, c.patchT)) : "to the leak";
        if (b.task == 3) return b.follow == 0 ? "following you" : "following";
    }
    if (c.station >= 0) {
        int ri = RodAt(c.station);
        if (ri >= 0 && rods[ri].state == RodState::Fighting) return std::string(Stations()[c.station].name) + ": fish on";
        return Stations()[c.station].name;
    }
    if (ci < (int)brains.size() && brains[ci].goal >= 0) return std::string("to the ") + Stations()[brains[ci].goal].name;
    return "standing by";
}

int Gannet::OrderBot(int station) {
    if ((int)brains.size() < (int)crew.size()) brains.resize(crew.size());
    if (station < 0) {   // everyone back to their watch
        for (int i = 1; i < (int)crew.size(); i++) if (crew[i].bot) { brains[i].order = -1; brains[i].think = 0; }
        return -1;
    }
    // the nearest bot that isn't fighting a fish (or the one already ordered there)
    int best = -1; float bd = 1e9f;
    Vector2 at = Stations()[station].at;
    for (int i = 1; i < (int)crew.size(); i++) {
        const Crew& c = crew[i];
        if (!c.bot || c.dead || c.overboard) continue;
        if (brains[i].order == station) return i;
        int ri = c.station >= 0 ? RodAt(c.station) : -1;
        float d = Vector2Distance(c.p, at) + (c.deck != Stations()[station].deck ? 6.0f : 0) + (ri >= 0 && rods[ri].state == RodState::Fighting ? 50.0f : 0);
        if (d < bd) { bd = d; best = i; }
    }
    if (best >= 0) {
        for (int i = 1; i < (int)crew.size(); i++) if (brains[i].order == station) brains[i].order = -1;
        brains[best].order = station; brains[best].follow = -1; brains[best].think = 0;
        brains[best].bark = std::string("Aye: the ") + Stations()[station].name; brains[best].barkT = 2.5f;
    }
    return best;
}

int Gannet::OrderFollow(int leader) {
    if ((int)brains.size() < (int)crew.size()) brains.resize(crew.size());
    // asked again: whoever was following goes back to its watch
    bool any = false;
    for (int i = 1; i < (int)crew.size(); i++) if (brains[i].follow == leader) { brains[i].follow = -1; brains[i].think = 0; brains[i].bark = "Back to it"; brains[i].barkT = 2; any = true; }
    if (any) return -1;
    int best = -1; float bd = 1e9f;
    const Crew& L = crew[leader];
    for (int i = 1; i < (int)crew.size(); i++) {
        const Crew& c = crew[i];
        if (!c.bot || c.dead || c.overboard || i == leader || brains[i].task == 1 || brains[i].task == 2) continue;
        int ri = c.station >= 0 ? RodAt(c.station) : -1;
        float d = Vector2Distance(c.p, L.p) + (c.deck != L.deck ? 6.0f : 0) + (ri >= 0 && rods[ri].state == RodState::Fighting ? 50.0f : 0);
        if (d < bd) { bd = d; best = i; }
    }
    if (best >= 0) { brains[best].follow = leader; brains[best].order = -1; brains[best].think = 0; brains[best].bark = "Right behind you"; brains[best].barkT = 2.5f; }
    return best;
}

void Gannet::StepBots(float dt) {
    if ((int)brains.size() < (int)crew.size()) {
        size_t was = brains.size();
        brains.resize(crew.size());
        for (size_t i = was; i < brains.size(); i++) brains[i].rng = 0x9E3779B9u * (uint32_t)(i + 1) + 17;
    }
    const auto& S = Stations();
    const SkillDef& sk = SkillOf(botSkill);
    // who is where: one hand per station (the humans' claims count)
    auto takenBy = [&](int st, int except) { for (int j = 0; j < (int)crew.size(); j++) if (j != except && !crew[j].overboard && !crew[j].dead && (crew[j].station == st || (crew[j].bot && brains[j].goal == st))) return j; return -1; };
    bool anyOverboard = false; for (const auto& c : crew) if (c.overboard && !c.dead) anyOverboard = true;
    // the skipper holds the Bosun's slot but stands at the helm: if no bot is a Bosun, the first able bot keeps the fire
    int fireman = -1;
    for (int j = 1; j < (int)crew.size(); j++) if (crew[j].bot && !crew[j].dead && !crew[j].overboard && crew[j].role == Role::Bosun) { fireman = j; break; }
    if (fireman < 0) for (int j = 1; j < (int)crew.size(); j++) if (crew[j].bot && !crew[j].dead && !crew[j].overboard) { fireman = j; break; }
    // keep the rods' fight brains in step with who holds them
    for (auto& r : rods) {
        int holder = -1; for (int j = 0; j < (int)crew.size(); j++) if (crew[j].station == r.station && !crew[j].overboard) holder = j;
        if (holder >= 0) r.botAngler = crew[holder].bot;
    }
    // ---- emergencies, handed out once a tick to the nearest free bot: a hand in the water (the life ring), a leak
    // (a patch kit). A bot fighting a fish keeps fighting it; the fireman is the last to be pulled off the boiler.
    auto fighting = [&](int j) { int ri = crew[j].station >= 0 ? RodAt(crew[j].station) : -1; return ri >= 0 && rods[ri].state == RodState::Fighting; };
    auto nearestFree = [&](Vector2 at, int deck, int notThis) {
        int best = -1; float bd = 1e9f;
        for (int j = 1; j < (int)crew.size(); j++) {
            const Crew& o = crew[j];
            if (!o.bot || o.dead || o.overboard || o.fallen || o.tangleT > 0 || j == notThis || brains[j].task == 1 || brains[j].task == 2 || brains[j].task == 4 || fighting(j)) continue;
            float d = Vector2Distance(o.p, at) + (o.deck != deck ? 8.0f : 0) + (j == fireman ? 12.0f : 0);
            if (d < bd) { bd = d; best = j; }
        }
        return best;
    };
    for (int k = 0; k < (int)crew.size(); k++) {
        if (!crew[k].overboard || crew[k].dead || rings.empty()) continue;
        bool has = false; for (int j = 1; j < (int)brains.size(); j++) if (brains[j].task == 1 && brains[j].target == k) has = true;
        if (has) continue;
        int j = nearestFree(boat.ToDeck(crew[k].swim), 0, k);
        if (j < 0) continue;
        if (crew[j].station >= 0) LeaveStation(j);
        brains[j].task = 1; brains[j].target = k; brains[j].taskT = 0; brains[j].goal = -1;
        brains[j].bark = "Man overboard! The ring!"; brains[j].barkT = 3;
    }
    if (PatchKits() > 0) for (int s = 0; s < SEC_COUNT; s++) {
        if (boat.integrity[s] >= D().leakBelow || boat.patched[s]) continue;
        bool has = false;
        for (int j = 0; j < (int)crew.size(); j++) if (crew[j].patchSec == s || (j < (int)brains.size() && brains[j].task == 2 && brains[j].target == s)) has = true;
        if (has) continue;
        int j = nearestFree(SectionSpot(s), 0, -1);
        if (j < 0) continue;
        if (crew[j].station >= 0) LeaveStation(j);
        brains[j].task = 2; brains[j].target = s; brains[j].taskT = 0; brains[j].goal = -1;
        brains[j].bark = TextFormat("She's holed, %s! I'll patch it", SectionName(s)); brains[j].barkT = 3;
    }
    // a hand in a Kelp Wraith's grip at the rail (the Weeds): the nearest free bot runs to cut them loose
    for (int k = 0; k < (int)crew.size(); k++) {
        if (crew[k].tangleT <= 0 || crew[k].deck != 0) continue;
        bool has = false; for (int j = 1; j < (int)brains.size(); j++) if (brains[j].task == 4 && brains[j].target == k) has = true;
        if (has) continue;
        int j = nearestFree(crew[k].p, 0, k);
        if (j < 0) continue;
        if (crew[j].station >= 0) LeaveStation(j);
        brains[j].task = 4; brains[j].target = k; brains[j].taskT = 0; brains[j].goal = -1;
        brains[j].bark = "Hold on! I've got a knife!"; brains[j].barkT = 3;
    }
    // walking to a spot on a deck: the ladder, the wheelhouse door, a sidestep when stuck; true once there
    auto walk = [&](int i, int deck, Vector2 at, float arrive, bool* climbed) {
        Crew& c = crew[i]; Brain& b = brains[i];
        *climbed = false;
        if (c.deck == deck && Vector2Distance(c.p, at) < arrive) return true;
        bool climb = false;
        Vector2 wp = Waypoint(c, deck, at, &climb);
        if (climb) { TakeStation(i); *climbed = true; return false; }
        Vector2 to = Vector2Subtract(wp, c.p);
        float d = Vector2Length(to);
        Vector2 wish = d > 0.05f ? Vector2Scale(to, 1 / d) : Vector2{0, 0};
        // stuck on the drum, the table or the mast: step sideways for a moment
        b.stuckT = Vector2Distance(c.p, b.lastP) < 0.6f * dt ? b.stuckT + dt : 0;
        b.lastP = c.p;
        if (b.stuckT > 0.35f) { b.sideT = 0.7f; b.side = -b.side; b.stuckT = 0; }
        if (b.sideT > 0) { b.sideT -= dt; wish = Vector2Add(Vector2Scale(wish, 0.3f), Vector2Scale({-wish.y, wish.x}, (float)b.side)); }
        if (d < 0.6f && wp.x == at.x && wp.y == at.y) wish = Vector2Scale(wish, std::max(0.35f, d / 0.6f));   // ease into place
        Move(i, wish, false, dt);
        return false;
    };
    for (int i = 0; i < (int)crew.size(); i++) {
        Crew& c = crew[i];
        if (!c.bot) continue;
        Brain& b = brains[i];
        if (b.barkT > 0) b.barkT -= dt;
        if (c.dead) { Move(i, {0, 0}, false, dt); continue; }
        if (c.overboard) {
            if (skiff.Up() && Vector2Distance(c.swim, skiff.p) < 3.0f && BoardSkiff(i)) continue;   // (the skiff's nearer: over her gunwale)
            // swim for the stern ladder
            Vector2 at = boat.ToDeck(c.swim), to = Vector2Subtract({-11.3f, 0}, at);
            Move(i, Vector2Length(to) > 0.1f ? Vector2Normalize(to) : Vector2{0, 0}, false, dt);
            if (b.barkT <= 0) { b.bark = "Help! Stop the screw!"; b.barkT = 3; }
            continue;
        }
        if (c.fallen) { Move(i, {0, 0}, false, dt); continue; }   // (Move is where a fallen hand gets back up)
        // self-defence: a landed fish that has hold of this hand, or anything but a flopper within reach (a biter, a
        // grabber, a pincher...), gets clubbed first, whatever the watch is (the deck kill, design doc v2)
        if (c.deck == 0) {
            bool threat = false;
            for (const auto& h : hold) if (!h.dead && !h.gutted && h.hp >= 0 && (h.grabbed == i || (h.deckKind != DB_FLOPPER && Vector2Distance(h.deckAt, c.p) < 1.6f))) threat = true;
            for (const auto& dr : drowned) if (Vector2Distance(dr.p, c.p) < 1.6f) threat = true;   // (the Grotto: a Drowned sailor within reach)
            if (isopods.state == 2) threat = true;   // (and isopods underfoot: stamp on them)
            if (threat) { b.defT -= dt; if (b.defT <= 0) { b.defT = 1.0f; KillDeckFish(i, 1.6f); } }
            else b.defT = 0.3f;
        }
        if (b.task == 0 && b.follow >= 0) b.task = 3;
        if (b.task == 3 && b.follow < 0) b.task = 0;
        // the skiff (design doc v2, "The skiff"): a bot following a hand goes down into her after them and rows on their
        // beat (StepSkiff), keeps her while they're ashore, and climbs back aboard the Gannet when they do; it never works
        // the Gannet's stations from her
        if (c.deck == DECK_SKIFF) {
            const Crew* lead = b.follow >= 0 ? &crew[b.follow] : nullptr;
            if (!lead || (lead->deck <= 1 && !lead->overboard)) {
                if (!LeaveSkiff(i) && lead && b.barkT <= 0) { b.bark = "Bring her alongside and I'll come up"; b.barkT = 5; }
            } else if (lead->deck == DECK_SHORE && b.barkT <= 0 && BR(b.rng) < 0.002f) { b.bark = "I'll keep her off the rocks"; b.barkT = 4; }
            Move(i, {0, 0}, false, dt);
            continue;
        }
        if (c.deck == DECK_SHORE) { Move(i, {0, 0}, false, dt); continue; }
        if (b.task == 3 && b.follow >= 0 && crew[b.follow].deck >= DECK_SKIFF && !crew[b.follow].overboard) {
            int dv = -1; for (int k = 0; k < (int)Stations().size(); k++) if (Stations()[k].kind == StationKind::Davit) dv = k;
            if (c.station >= 0) LeaveStation(i);
            bool cl;
            if (dv >= 0 && walk(i, 0, Stations()[dv].at, 1.0f, &cl)) {
                Move(i, {0, 0}, false, dt);
                if (SkiffAlongside(4)) BoardSkiff(i);
                else if (b.barkT <= 0) { b.bark = "Waiting at the davit"; b.barkT = 5; }
            }
            continue;
        }
        // ---- the life ring: to the rail nearest the swimmer, throw, haul in a miss and throw again, haul them home
        if (b.task == 1) {
            if (b.target < 0 || b.target >= (int)crew.size() || !crew[b.target].overboard || crew[b.target].dead) {
                // aboard (or lost): haul in a ring still out, then back to work
                LifeRing* out = nullptr; for (auto& r : rings) if (r.thrower == i && r.state != 0) out = &r;
                if (out && out->state == 2) { UseItem(i, c.p, false, true, false, dt); Move(i, {0, 0}, false, dt); continue; }
                b.task = 0; b.think = 0; continue;
            }
            const Crew& sw = crew[b.target];
            if (c.station >= 0) LeaveStation(i);
            // the ring itself: from its own slot, else from another hand's or the locker
            int slot = -1; for (int k = 0; k < 4; k++) if (c.slots[k].it == Item::Ring) slot = k;
            if (slot < 0) {
                Slot got{}; bool found = false;
                // (the ring is the ship's: it hangs by the rail even when the hand who "carries" it is in the water)
                for (int j = 0; j < (int)crew.size() && !found; j++) if (j != i)
                    for (auto& s : crew[j].slots) if (s.it == Item::Ring) { bool busy = false; for (auto& r : rings) if (r.thrower == j && r.state != 0) busy = true; if (!busy) { got = s; s = Slot{}; found = true; break; } }
                for (size_t k = 0; k < locker.size() && !found; k++) if (locker[k].it == Item::Ring) { got = locker[k]; locker.erase(locker.begin() + k); found = true; }
                if (!found) { if (b.barkT <= 0) { b.bark = "Where's the ring?!"; b.barkT = 3; } b.task = 0; continue; }
                for (int k = 0; k < 4 && slot < 0; k++) if (c.slots[k].it == Item::None) slot = k;
                if (slot < 0) { slot = 3; locker.push_back(c.slots[3]); }
                c.slots[slot] = got;
                Say("A hand grabs the life ring");
            }
            c.sel = slot;
            Vector2 sd = boat.ToDeck(sw.swim);
            Vector2 spot{std::clamp(sd.x, -9.4f, 8.4f), sd.y < 0 ? -2.3f : 2.3f};
            if (spot.x > 0.9f && spot.x < 5.2f) spot.y = sd.y < 0 ? -2.4f : 2.4f;   // (the wheelhouse's sides)
            bool climbed;
            if (!walk(i, 0, spot, 0.8f, &climbed)) continue;
            Move(i, {0, 0}, false, dt);
            LifeRing* r = nullptr; for (auto& rr : rings) if (rr.thrower == i && rr.state != 0) r = &rr;
            if (!r) {
                if (Vector2Distance(RailWorld(i), sw.swim) < 17.5f) { UseItem(i, sd, true, false, false, dt); b.taskT = 0; }
                else if (b.barkT <= 0) { b.bark = "Too far! Bring her round!"; b.barkT = 3; }
            } else if (r->state == 2) {
                b.taskT += dt;
                bool miss = r->holder < 0 && Vector2Distance(r->p, sw.swim) > 2.5f && b.taskT > 2.5f;
                if (r->holder >= 0 || miss) UseItem(i, sd, false, true, false, dt);   // haul (them, or the ring to throw again)
                if (r->holder >= 0 && b.barkT <= 0) { b.bark = "Hold on! Hauling!"; b.barkT = 3; }
            }
            continue;
        }
        // ---- a leak: stand in the section and patch it (with the ship's kits)
        if (b.task == 2) {
            int s = b.target;
            if (s < 0 || boat.patched[s] || boat.integrity[s] >= D().leakBelow) { b.task = 0; b.think = 0; continue; }
            if (c.station >= 0) LeaveStation(i);
            bool climbed;
            if (!walk(i, 0, SectionSpot(s), 0.5f, &climbed)) continue;
            Move(i, {0, 0}, false, dt);
            if (c.patchSec < 0 && Vector2Length(c.v) < 0.3f && !StartPatch(i) && PatchKits() == 0) { b.task = 0; b.think = 0; }
            continue;
        }
        // ---- a hand tangled at the rail: stand beside them, inboard, and cut them free (E)
        if (b.task == 4) {
            if (b.target < 0 || b.target >= (int)crew.size() || crew[b.target].tangleT <= 0) { b.task = 0; b.think = 0; continue; }
            if (c.station >= 0) LeaveStation(i);
            Vector2 tp = crew[b.target].p, spot{tp.x, tp.y - (tp.y > 0 ? 0.9f : -0.9f)};
            bool climbed;
            if (!walk(i, 0, spot, 0.45f, &climbed)) continue;
            Move(i, {0, 0}, false, dt);
            TakeStation(i);
            continue;
        }
        // ---- following a hand about (F): keep a couple of metres off them, down the ladder after them
        if (b.task == 3) {
            const Crew& L = crew[b.follow];
            if (L.dead || L.overboard) { Move(i, {0, 0}, false, dt); continue; }
            if (c.station >= 0) LeaveStation(i);
            Vector2 away = Vector2Subtract(c.p, L.p);
            Vector2 at = c.deck == L.deck && Vector2Length(away) > 0.1f ? Vector2Add(L.p, Vector2Scale(Vector2Normalize(away), 1.3f)) : L.p;
            bool climbed;
            if (c.deck == L.deck && Vector2Distance(c.p, L.p) < 1.8f) { Move(i, {0, 0}, false, dt); continue; }
            walk(i, L.deck, at, 0.4f, &climbed);
            continue;
        }
        // ---- choose (twice a second): orders, then what's happening, then the role's watch
        b.think -= dt;
        if (b.think <= 0) {
            b.think = 0.5f;
            int goal = -1;
            int pumps = StationOfKind(StationKind::Pumps), boiler = StationOfKind(StationKind::Boiler), gut = StationOfKind(StationKind::Gutting);
            bool water = boat.bilge > 300 || (b.goal == pumps && boat.bilge > 10);   // once at the pumps, pump her dry
            for (int s = 0; s < SEC_COUNT; s++) if (boat.integrity[s] < D().leakBelow && !boat.patched[s]) water = water || boat.bilge > 60;
            if (b.order >= 0) goal = b.order;
            else if (water && takenBy(pumps, i) < 0 && i == fireman) {
                goal = pumps;
                if (b.goal != pumps) { b.bark = "Water in her! To the pumps"; b.barkT = 3; Say("A hand goes below to the pumps"); }
            } else {
                // the watch by role (design doc): Bosun the boiler; Medic the gutting table watching the rails; Angler a
                // rod; Diver the gutting table when there's fish on deck, else a rod
                std::vector<int> want;
                auto rodsInOrder = [&]() { for (StationKind k : {StationKind::PortRod, StationKind::StarRod, StationKind::SternRodS, StationKind::SternRodP}) want.push_back(StationOfKind(k)); };
                Role role = i == fireman ? Role::Bosun : c.role == Role::Bosun ? Role::Angler : c.role;
                switch (role) {
                    case Role::Bosun: want.push_back(boiler); break;
                    case Role::Medic: want.push_back(gut); rodsInOrder(); break;
                    case Role::Diver: if (DeckFish() > 0) want.push_back(gut); rodsInOrder(); break;
                    default: rodsInOrder(); want.push_back(gut); break;
                }
                // keep the station it already holds if it's still on the list (no musical chairs)
                for (int w : want) if (w == c.station) { goal = w; break; }
                if (goal < 0) for (int w : want) if (w >= 0 && takenBy(w, i) < 0) { goal = w; break; }
            }
            if (anyOverboard && c.role == Role::Medic && b.barkT <= 0) { b.bark = "Man overboard! Ring off the screw!"; b.barkT = 3; }
            if (goal != b.goal && c.station >= 0 && c.station != goal) LeaveStation(i);
            b.goal = goal;
            b.goalDeck = goal >= 0 ? S[goal].deck : c.deck;
        }
        // ---- walk there
        if (b.goal >= 0 && c.station != b.goal) {
            Vector2 at = S[b.goal].at;
            if (c.station >= 0) LeaveStation(i);
            bool climbed;
            if (walk(i, b.goalDeck, at, 0.55f, &climbed)) {
                if (!TakeStation(i)) b.think = 0;   // (somebody beat it there: choose again)
                Move(i, {0, 0}, false, dt);
            }
            continue;
        }
        Move(i, {0, 0}, false, dt);
        if (c.station < 0) continue;
        // ---- work the station
        const StationDef& sd = S[c.station];
        switch (sd.kind) {
            case StationKind::Boiler:
                Primary(i, boat.pressure < D().greenLo + 0.12f && boat.firebox < 5.5f, dt);
                Secondary(i, boat.pressure > D().greenHi + 0.04f, dt);   // bleed at the first red
                break;
            case StationKind::Pumps:
                Primary(i, boat.bilge > 10, dt);
                if (boat.bilge <= 10 && b.order < 0) b.think = 0;        // dry: back to the watch
                break;
            case StationKind::Gutting: {
                bool any = DeckFish() > 0;
                for (const auto& h : hold) if (h.bycatch || h.protectedSp) any = true;
                // the table's hand steps over and clubs whatever is flopping for the rail before it gets there
                if (c.cool <= 0) { if (KillDeckFish(i, 12.0f)) c.cool = 2.5f; } else c.cool -= dt;
                // birds over her: crate the dead ones still waiting for the knife before they're taken
                if (c.cool <= 0 && gullT > 0 && CrateFish(i, 12.0f)) { c.cool = 1.5f; if (b.barkT <= 0) { b.bark = "Birds! Crating the catch"; b.barkT = 3; } }
                Primary(i, any, dt);
                break;
            }
            case StationKind::NetWinch:
                if (b.order == c.station) {   // shoots and hauls only when ordered
                    bool held = (net.state == NetState::Stowed && !moored && !netLast) || net.state == NetState::Shooting || net.state == NetState::Hauling || (net.state == NetState::Down && net.load > 180);
                    NetInput(i, held, false, dt);
                }
                break;
            default:
                if (IsRod(sd.kind)) {
                    int ri = RodAt(c.station);
                    if (ri < 0) break;
                    Rod& r = rods[ri];
                    Vector2 tip = r.TipDeck(), out = Vector2Normalize(Vector2Subtract(tip, sd.at));
                    Vector2 aim = Vector2Add(tip, Vector2Add(Vector2Scale(out, 14), Vector2Scale({-out.y, out.x}, (BR(b.rng) - 0.5f) * 6)));
                    if (r.state == RodState::Fighting && b.lastRod != RodState::Fighting) {
                        b.bark = sd.kind == StationKind::PortRod ? "Fish on, port!" : sd.kind == StationKind::StarRod ? "Fish on, starboard!" : "Fish on, aft!";
                        b.barkT = 2.5f;
                    }
                    b.lastRod = r.state;
                    if (moored || r.state == RodState::Fighting) { RodInput(i, false, aim, false, false, 0, false, false, 0); break; }
                    if (r.state == RodState::Idle || r.state == RodState::Charging) {
                        // a cast: hold, then let go at about three quarters
                        b.castT += dt;
                        bool hold = b.castT < 0.9f;
                        if (b.castT > 1.4f) b.castT = 0;
                        RodInput(i, hold, aim, false, false, 0, false, false, 0);
                        b.struck = false;
                    } else {
                        // out: set the hook on the take, at the bot's skill; never strike at nibbles
                        bool strike = false;
                        if (r.bite.stage == BiteStage::Take && !b.struck) { b.struck = true; strike = BR(b.rng) < sk.hookSet; }
                        if (r.bite.stage == BiteStage::None) b.struck = false;
                        RodInput(i, false, aim, false, strike, 0, false, false, 0);
                    }
                }
                break;
        }
    }
}

// ---------------------------------------------------------------- --trawl-sail-diag (what puts water in her under way)
int RunTrawlSailDiag() {
    const float dt = 1 / 60.0f;
    for (int w = 0; w < 5; w++) for (int tel = 1; tel <= 3; tel += 2) {
        Gannet g; g.Init(1, 7 + w, (Weather)w);
        g.boat.telegraph = tel; g.boat.pressure = 0.7f; g.boat.firebox = 6;
        g.crew[0].p = {3.0f, 0.8f};
        float maxRoll = 0, minRail = 9, sunkAt = -1;
        for (int k = 0; k < 60 * 240; k++) {
            float t = k * dt;
            g.boat.rudder = fmodf(t, 40) < 20 ? (fmodf(t, 80) < 40 ? 1.0f : -1.0f) : 0.0f;
            if (g.boat.pressure < 0.6f && g.boat.firebox < 5) g.boat.Shovel(0.1f);
            g.Step(dt);
            maxRoll = std::max(maxRoll, fabsf(g.boat.RollDeg()));
            float rail = g.boat.Freeboard() - fabsf(sinf(g.boat.roll)) * D().beam * 0.5f;
            minRail = std::min(minRail, rail);
            if (g.boat.sunk && sunkAt < 0) sunkAt = t;
        }
        printf("%-7s tel %d: speed %.1f kn, max roll %4.1f deg, lowest rail %.2f m, green water %5.0f kg, bilge %5.0f kg%s\n",
               WeatherName((Weather)w), tel, g.boat.Speed() * 1.944f, maxRoll, minRail, g.boat.greenWater, g.boat.bilge, sunkAt >= 0 ? TextFormat(", SUNK at %.0f s", sunkAt) : "");
    }
    return 0;
}

// ---------------------------------------------------------------- --trawl-bot-test
int RunTrawlBotTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Trawl: the bot crew\n");
    const float dt = 1 / 60.0f;
    auto kindAt = [](const Crew& c) { return c.station >= 0 ? Stations()[c.station].kind : StationKind::COUNT; };
    // the watch: four hands, three of them bots, under way on a calm sea
    {
        Gannet g; g.Init(4, 7); g.botsOn = true;
        g.boat.telegraph = 1; g.boat.pressure = 0.7f;
        g.crew[0].p = {3.0f, 0.8f};   // the skipper stays in the wheelhouse
        float t = 0;
        while (t < 25) { g.Step(dt); t += dt; }
        check(kindAt(g.crew[1]) == StationKind::Boiler && g.crew[1].deck == 1, TextFormat("with the skipper at the helm, the first bot went down the ladder to the boiler (%s)", g.BotDoing(1).c_str()));
        check(IsRod(kindAt(g.crew[2])), TextFormat("the Diver took a rod (%s)", g.BotDoing(2).c_str()));
        check(kindAt(g.crew[3]) == StationKind::Gutting, TextFormat("the Medic stands by the gutting table (%s)", g.BotDoing(3).c_str()));
        // steam for three minutes at half ahead
        float lo = 9, hi = 0, red = 0;
        for (int k = 0; k < 60 * 180; k++) { g.Step(dt); lo = std::min(lo, g.boat.pressure); hi = std::max(hi, g.boat.pressure); if (g.boat.pressure >= D().redAt) red += dt; }
        check(lo > D().greenLo - 0.12f && red < 1, TextFormat("the Bosun keeps steam for three minutes at half ahead (%.2f-%.2f, %.1f s in the red)", lo, hi, red));
        // water in her
        g.boat.bilge = 500;
        float b0 = g.boat.bilge; for (int k = 0; k < 60 * 30; k++) g.Step(dt);
        check(g.boat.bilge < b0 * 0.5f, TextFormat("500 kg in the bilge: a hand goes to the pumps (%.0f kg after 30 s)", g.boat.bilge));
        bool anyPumps = false; for (int i = 1; i < 4; i++) anyPumps = anyPumps || kindAt(g.crew[i]) == StationKind::Pumps;
        for (int k = 0; k < 60 * 30; k++) g.Step(dt);
        bool back = kindAt(g.crew[1]) == StationKind::Boiler;
        check(back || !anyPumps, TextFormat("dry again, the Bosun goes back to the boiler (%s)", g.BotDoing(1).c_str()));
        // an order: the net winch
        int ws = StationOfKind(StationKind::NetWinch);
        int who = g.OrderBot(ws);
        for (int k = 0; k < 60 * 20 && g.crew[who].station != ws; k++) g.Step(dt);
        check(who > 0 && g.crew[who].station == ws, TextFormat("ordered to the net winch, hand %d is on it", who));
        for (int k = 0; k < 60 * 20; k++) g.Step(dt);
        check(g.net.state == NetState::Down || g.net.state == NetState::Shooting, "and shoots the net");
        g.OrderBot(-1);
        for (int k = 0; k < 60 * 20; k++) g.Step(dt);
        check(g.crew[who].station != ws, TextFormat("back to the watch, it leaves the winch (%s)", g.BotDoing(who).c_str()));
    }
    // overboard: a bot swims for the stern ladder and climbs aboard once the screw stops
    {
        Gannet g; g.Init(2, 3); g.botsOn = true;
        g.GoOverboard(1, "test"); g.crew[1].swim = g.boat.ToWorld({-6, 6});
        g.boat.telegraph = 0; g.boat.shaft = 0;
        for (int k = 0; k < 60 * 30 && g.crew[1].overboard; k++) g.Step(dt);
        check(!g.crew[1].overboard && !g.crew[1].dead, "a bot overboard swims to the stern ladder and climbs aboard");
    }
    // a hole amidships to port: the nearest free bot fetches the ship's patch kit and patches it
    {
        Gannet g; g.Init(4, 11); g.botsOn = true; g.GiveStartingKit();
        g.crew[0].p = {3.0f, 0.8f};
        for (int k = 0; k < 60 * 15; k++) g.Step(dt);
        g.boat.Hit(SEC_MID_P, g.boat.integrity[SEC_MID_P] - D().leakBelow + 15);
        bool leaking = g.boat.integrity[SEC_MID_P] < D().leakBelow;
        int kits = g.PatchKits();
        for (int k = 0; k < 60 * 40 && !g.boat.patched[SEC_MID_P]; k++) g.Step(dt);
        check(leaking && g.boat.patched[SEC_MID_P] && g.PatchKits() == kits - 1, TextFormat("holed amidships to port: a bot patches it with the ship's kit (%d kit(s) left)", g.PatchKits()));
        bool back = false; for (int k = 0; k < 60 * 20 && !back; k++) { g.Step(dt); back = true; for (int i = 1; i < 4; i++) back = back && g.brains[i].task == 0 && g.crew[i].station >= 0; }
        check(back, "and every bot is back at a station after");
    }
    // the skipper overboard off the beam: a bot throws the ring, hauls them in to the rail
    {
        Gannet g; g.Init(3, 5); g.botsOn = true; g.GiveStartingKit();
        for (int k = 0; k < 60 * 15; k++) g.Step(dt);
        g.GoOverboard(0, "test"); g.crew[0].swim = g.boat.ToWorld({-2, 9});
        g.boat.telegraph = 0;
        bool thrown = false, held = false;
        for (int k = 0; k < 60 * 45 && g.crew[0].overboard && !g.crew[0].dead; k++) {
            g.Step(dt);
            for (const auto& r : g.rings) { if (r.state != 0) thrown = true; if (r.holder == 0) held = true; }
            if (getenv("DEPTH_TRACE") && k % 120 == 0) { int ri = 0; printf("    t%2d swim d %.1f drown %.0f | 1:%s (%.1f,%.1f) 2:%s | ring %d holder %d\n", k / 60, Vector2Distance(g.crew[0].swim, g.boat.pos), g.crew[0].drownT, g.BotDoing(1).c_str(), g.crew[1].p.x, g.crew[1].p.y, g.BotDoing(2).c_str(), g.rings.empty() ? -1 : g.rings[ri].state, g.rings.empty() ? -9 : g.rings[ri].holder); }
        }
        check(thrown && held, "the skipper in the water: a bot fetches the ring and throws it to them");
        check(!g.crew[0].overboard && !g.crew[0].dead, "and hauls them back aboard over the rail");
    }
    // F: a bot follows the skipper below
    {
        Gannet g; g.Init(3, 9); g.botsOn = true;
        for (int k = 0; k < 60 * 15; k++) g.Step(dt);
        int who = g.OrderFollow(0);
        g.crew[0].deck = 1; g.crew[0].p = {-4.6f, 0.4f};
        for (int k = 0; k < 60 * 25; k++) { g.Step(dt); if (getenv("DEPTH_TRACE") && k % 120 == 0) printf("    t%d bot deck %d (%.1f,%.1f) %s\n", k / 60, g.crew[who].deck, g.crew[who].p.x, g.crew[who].p.y, g.BotDoing(who).c_str()); }
        check(who > 0 && g.crew[who].deck == 1 && Vector2Distance(g.crew[who].p, g.crew[0].p) < 2.5f, TextFormat("F: hand %d follows the skipper down the ladder (%.1f m off)", who, who > 0 ? Vector2Distance(g.crew[who].p, g.crew[0].p) : -1.0f));
        g.OrderFollow(0);
        for (int k = 0; k < 60 * 25; k++) g.Step(dt);
        check(who > 0 && g.crew[who].station >= 0, TextFormat("F again: it goes back to its watch (%s)", who > 0 ? g.BotDoing(who).c_str() : "-"));
    }
    // a night's fishing on a real ground: the bots land fish without the skipper touching a rod
    {
        Gannet g; Eco e; Session s; s.Begin(g, e, 4, 20262); s.plainNights = true;
        g.botsOn = true;
        s.Buy("shrimp"); s.Buy("shrimp");
        while (g.boat.bunker < 40 && s.Buy("coal")) {}
        std::string why;
        g.crew[0].p = {3.0f, 0.8f};
        bool off = s.CastOff(&why);
        check(off, "casting off with the crew aboard" + (why.empty() ? std::string() : ": " + why));
        g.boat.pos = Vector2Add(s.harbour, {s.harbourR + 40, 10});
        g.crew[0].p = {3.0f, 0.8f};
        size_t h0 = 0; int landed = 0, gutted = 0;
        for (int k = 0; k < 60 * 480; k++) {
            g.boat.telegraph = 0;
            g.Step(dt); s.Step(dt);
            if (g.hold.size() > h0) landed += (int)(g.hold.size() - h0);
            h0 = g.hold.size();
            if (getenv("DEPTH_TRACE") && k % 600 == 0) {
                printf("    t%3d phase %s bait %d |", k / 60, PhaseName(s.phase), g.baitShrimp);
                for (int i = 1; i < 4; i++) { int ri = g.crew[i].station >= 0 ? g.RodAt(g.crew[i].station) : -1; printf(" %d:%s rod %d bite %d", i, g.BotDoing(i).c_str(), ri >= 0 ? (int)g.rods[ri].state : -1, ri >= 0 ? (int)g.rods[ri].bite.stage : -1); }
                printf("\n");
            }
        }
        for (const auto& h : g.hold) if (h.gutted) gutted++;
        check(landed >= 2, TextFormat("eight minutes hove to: the bots land %d fish", landed));
        check(landed == 0 || gutted * 2 >= (int)g.hold.size(), TextFormat("and gut them (%d of %d gutted)", gutted, (int)g.hold.size()));
    }
    printf(fails ? "%d FAILED\n" : "All bot crew checks passed\n", fails);
    return fails ? 1 : 0;
}

} // namespace tw
