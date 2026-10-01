// The Trawl stage 6: shooting, the net, set gear, and the crew's lives (design doc: "Shooting", "The trawl", "Set
// gear", "Death, injury, and ghosts"). Hand gear lives in four slots a hand; projectiles are simulated (gravity in the
// air; a round dies in the first metre and a half of water, a spear or a harpoon keeps going); a shot fish floats until
// gaffed or reeled in on its tether; the net is a mesh towed on two warps that fills from the schools it sweeps; set
// gear fishes the population where it lies; a hand overboard has 25 s.
#include "trawl.h"
#include "trawl_eco.h"
#include "trawl_session.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tw {

// ---------------------------------------------------------------- the numbers (design doc, "Weapons", "The Chandler")
const ItemDef& ItemOf(Item i) {
    //                                   price ammoPer ammoPrice noise
    static const ItemDef D[(int)Item::COUNT] = {
        {"(empty)",            0,  0,  0, 0,  ""},
        {"Gaff",               0,  0,  0, 1,  "Land a fish alongside; gaff a shot fish afloat"},
        {"Fish priest",        0,  0,  0, 1,  "Kills a landed fish"},
        {"Knife",             10,  0,  0, 0,  "Cuts a snagged net loose"},
        {"Speargun",         150,  3,  5, 2,  "8 m in water, 12 m in air, on a tether"},
        {"Flare pistol",      60,  3, 10, 3,  "A 40 m arc of light: scares the shy, draws the curious"},
        {"Rifle",            250, 10,  2, 6,  "Fish at or breaking the surface, gulls, boarders"},
        {"Shotgun",          200,  8,  3, 7,  "15 m in air: gull flocks, boarders"},
        {"Depth charge",     120,  1,120, 10, "12 m blast: everything in it floats up"},
        {"Life ring",         40,  1,  0, 0,  "Thrown on a rope to a hand in the water"},
        {"Bandages",          10,  3, 10, 0,  "Stop a bite bleeding"},
        {"Longline",          60,  1,  0, 1,  "20 hooks between two buoys, set off the stern"},
        {"Crab pot",          25,  1,  0, 1,  "Dropped on a reef, hauled on a later pass"},
    };
    return D[(int)i];
}
const char* InjuryName(int bit) {
    switch (bit) { case INJ_HOOKED_HAND: return "a hooked hand"; case INJ_BROKEN_ARM: return "a broken arm"; case INJ_BURN: return "a burn"; case INJ_BITE: return "a bite"; default: return "hurt"; }
}

namespace {
const float G = 9.81f;
const float RAIL_H = 2.6f;               // a hand's shoulder over the water at the rail (freeboard + 1.4)
const float DROWN_S = 25, DROWN_STORM_S = 12;
const float NET_SHOOT_S = 15, NET_WARP = 30;
float SlotDmg(Shot k) { switch (k) { case Shot::Bullet: return 40; case Shot::Pellet: return 6; case Shot::Spear: return 35; case Shot::Harpoon: return 60; case Shot::Explosive: return 400; default: return 0; } }
float ShotGrade(Shot k) { switch (k) { case Shot::Spear: return 0.85f; case Shot::Harpoon: return 0.75f; case Shot::Explosive: return 0.3f; default: return 0.7f; } }
float RandF(uint32_t& s) { s = s * 1664525u + 1013904223u; return (s >> 8) * (1.0f / 16777216.0f); }
uint32_t gRng = 12345;
}

// ---------------------------------------------------------------- the kit
void Gannet::GiveStartingKit() {
    // two gaffs, a fish priest, one life ring (design doc, "The Chandler": the starting gear)
    for (auto& c : crew) for (auto& s : c.slots) s = Slot{};
    if (!crew.empty()) {
        crew[0].slots[0] = {Item::Gaff, 0};
        crew[0].slots[1] = {Item::Priest, 0};
        crew[0].slots[2] = {Item::Ring, 1};
        crew[0].slots[3] = {Item::Speargun, 3};   // (the playtest: a basic weapon from the start; the Chandler sells the rest cheaper)
    }
    if (crew.size() > 1) crew[1].slots[0] = {Item::Gaff, 0};
    else locker.push_back({Item::Gaff, 0});
    rings.assign(1, LifeRing{});
}
bool Gannet::AddItem(Item it, int ammo) {
    Crew& c = crew[0];
    bool stacks = it == Item::Charge || it == Item::Bandage;
    for (auto& s : c.slots) if (stacks && s.it == it) { s.ammo += ammo; return true; }
    for (auto& s : c.slots) if (s.it == Item::None) { s = {it, ammo}; if (it == Item::Ring) rings.push_back(LifeRing{}); return true; }
    for (auto& s : locker) if (stacks && s.it == it) { s.ammo += ammo; return true; }
    locker.push_back({it, ammo});
    if (it == Item::Ring) rings.push_back(LifeRing{});
    return true;
}
Vector2 Gannet::RailWorld(int ci) const {
    const Crew& c = crew[ci];
    Vector2 d = c.p;
    // the nearest rail: port or starboard for most of her length, the transom at the stern, the stem at the bow
    if (d.x < -10.2f) d.x = -11.2f;
    else if (d.x > 9.5f) d.x = 10.8f;
    else d.y = d.y < 0 ? -3.1f : 3.1f;
    return boat.ToWorld(d);
}

// ---------------------------------------------------------------- firing
void Gannet::Reload(int ci) {
    Crew& c = crew[ci];
    Slot& s = c.slots[c.sel];
    if (s.it == Item::Rifle || s.it == Item::Shotgun || s.it == Item::Speargun || s.it == Item::Flare) c.reloadT = s.it == Item::Speargun ? 1.2f : 1.8f;
}
void Gannet::UseItem(int ci, Vector2 aimDeck, bool pressed, bool held, bool sight, float dt) {
    Crew& c = crew[ci];
    if (c.dead || c.overboard || c.station >= 0) return;
    if (c.cool > 0) c.cool -= dt;
    if (c.reloadT > 0) c.reloadT -= dt;
    Slot& s = c.slots[c.sel];
    Vector2 aimW = boat.ToWorld(aimDeck), from = boat.ToWorld(c.p);
    Vector3 muzzle{from.x, from.y, -RAIL_H};
    auto fire = [&](Shot k, float speed, float spreadDeg, int n) {
        for (int i = 0; i < n; i++) {
            Vector3 to{aimW.x, aimW.y, 0};
            Vector3 d = Vector3Normalize(Vector3Subtract(to, muzzle));
            float a = (RandF(gRng) - 0.5f) * 2 * spreadDeg * DEG2RAD, e = (RandF(gRng) - 0.5f) * spreadDeg * DEG2RAD;
            Vector2 hd = Vector2Rotate({d.x, d.y}, a);
            float hl = sqrtf(d.x * d.x + d.y * d.y);
            Vector3 v{hd.x, hd.y, d.z + e};
            v = Vector3Scale(Vector3Normalize(Vector3{v.x * hl / std::max(0.01f, Vector2Length(hd)), v.y * hl / std::max(0.01f, Vector2Length(hd)), v.z}), speed);
            Projectile p; p.kind = k; p.p = muzzle; p.v = v; p.owner = ci; p.dmg = SlotDmg(k); p.tether = k == Shot::Spear;
            p.life = k == Shot::Flare ? 6 : 3;
            shots.push_back(p);
        }
        if (eco) eco->AddNoise({muzzle.x, muzzle.y, 1}, ItemOf(s.it).noise * 6);
    };
    switch (s.it) {
        case Item::Rifle: case Item::Shotgun: case Item::Speargun: case Item::Flare: {
            if (!pressed || c.cool > 0 || c.reloadT > 0) break;
            if (s.ammo <= 0) { Say("Click: empty (R to reload from the locker's rounds)"); break; }
            s.ammo--;
            if (s.it == Item::Rifle) { fire(Shot::Bullet, 300, sight ? 0.3f : 1.5f, 1); c.cool = 1.2f; }
            else if (s.it == Item::Shotgun) { fire(Shot::Pellet, 250, sight ? 3.0f : 6.0f, 8); c.cool = 0.8f; }
            else if (s.it == Item::Speargun) { fire(Shot::Spear, 40, sight ? 0.5f : 1.2f, 1); c.cool = 2.0f; }
            else {
                // a flare goes up in an arc and comes down burning where it's aimed (up to 40 m)
                Vector2 d = Vector2Subtract(aimW, from); float L = std::min(40.0f, Vector2Length(d));
                if (L < 0.1f) d = boat.Forward(); else d = Vector2Normalize(d);
                float T = 2.2f, vz = -(G * T / 2) + RAIL_H / T;
                Projectile p; p.kind = Shot::Flare; p.p = muzzle; p.v = {d.x * L / T, d.y * L / T, vz}; p.owner = ci; p.life = 6;
                shots.push_back(p); c.cool = 1.0f;
                if (eco) eco->AddNoise({muzzle.x, muzzle.y, 1}, 18);
            }
            break;
        }
        case Item::Charge: {
            if (!pressed || s.ammo <= 0 || c.cool > 0) break;
            s.ammo--; if (s.ammo <= 0) s.it = Item::None;
            Projectile p; p.kind = Shot::Charge; p.owner = ci; p.life = 12;
            if (c.p.x < -9.0f) { Vector2 w = boat.ToWorld({-11.8f, c.p.y}); p.p = {w.x, w.y, 0}; p.v = {0, 0, 0}; Say("A depth charge rolls off the stern"); }
            else {
                Vector2 d = Vector2Subtract(aimW, from); float L = std::clamp(Vector2Length(d), 4.0f, 10.0f);
                d = Vector2Length(d) > 0.1f ? Vector2Normalize(d) : boat.Forward();
                float T = 1.2f;
                p.p = muzzle; p.v = {d.x * L / T, d.y * L / T, -(G * T / 2) + RAIL_H / T};
                Say("A depth charge thrown");
            }
            shots.push_back(p); c.cool = 1.5f; chargesUsed++;
            break;
        }
        case Item::Ring: {
            // thrown on its rope; once out, hold to haul it (and whoever holds it) back to the rail
            LifeRing* r = nullptr;
            for (auto& rr : rings) if (rr.thrower == ci && rr.state != 0) r = &rr;
            if (!r && pressed && s.ammo > 0) {
                for (auto& rr : rings) if (rr.state == 0) { r = &rr; break; }
                if (!r) break;
                Vector2 rail = RailWorld(ci), d = Vector2Subtract(aimW, rail);
                float L = std::min(18.0f, Vector2Length(d));
                d = Vector2Length(d) > 0.1f ? Vector2Normalize(d) : Vector2Normalize(Vector2Subtract(rail, boat.pos));
                r->state = 1; r->p = rail; r->v = Vector2Scale(d, L / 0.9f); r->thrower = ci; r->holder = -1; r->haulT = 0.9f;
                s.ammo = 0; Say("The life ring goes out on its rope");
            } else if (r && r->state == 2 && held) {
                Vector2 rail = RailWorld(ci), d = Vector2Subtract(rail, r->p);
                float L = Vector2Length(d), sp = r->holder >= 0 ? 1.5f : 3.0f;
                if (L > 0.01f) r->p = Vector2Add(r->p, Vector2Scale(d, std::min(1.0f, sp * dt / L)));
                if (r->holder >= 0) crew[r->holder].swim = r->p;
                if (L < 2.5f) {
                    if (r->holder >= 0) {
                        Crew& h = crew[r->holder];
                        h.overboard = false; h.p = c.p; h.p.y = c.p.y < 0 ? -2.5f : 2.5f; h.v = {0, 0}; h.drownT = 0;
                        Say("Hauled aboard over the rail");
                    }
                    r->state = 0; r->holder = -1; r->thrower = -1; s.ammo = 1;
                }
            }
            break;
        }
        case Item::Bandage:
            if (pressed && s.ammo > 0 && c.Has(INJ_BITE)) { c.injuries &= ~INJ_BITE; s.ammo--; if (s.ammo <= 0) s.it = Item::None; Say("Bandaged: the bleeding stops"); }
            break;
        case Item::Longline: {
            // set off the stern: a 60 m line astern on its two buoys, 20 hooks baited from the stores
            if (!pressed || c.p.x > -8.5f) { if (pressed) Say("Set the longline from the stern"); break; }
            Longline L;
            Vector2 a = boat.ToWorld({-11.8f, 0});
            Vector2 back = Vector2Scale(boat.Forward(), -1);
            L.a = a; L.b = Vector2Add(a, Vector2Scale(back, 60));
            for (int k = 0; k < 20; k++) {
                SetHook h;
                if (baitSquid > 0) { baitSquid--; } else if (baitShrimp > 0) { baitShrimp--; } else h.kg = -1;   // (kg -1: a bare hook)
                L.hooks.push_back(h);
            }
            longlines.push_back(L); s = Slot{};
            Say("The longline pays out astern between its buoys");
            break;
        }
        case Item::Pot: {
            if (!pressed) break;
            Pot p; p.p = RailWorld(ci);
            pots.push_back(p); s = Slot{};
            Say("A pot goes down on its float line");
            break;
        }
        case Item::Gaff:
            if (pressed) { if (c.cool <= 0) c.cool = 0.5f; GaffFloater(ci); }   // (cool: the swing the screens draw; a gun's cool-down never stops the gaff)
            break;
        case Item::Priest: case Item::Knife:
            if (pressed && c.cool <= 0) c.cool = 0.4f;
            break;
        default: break;
    }
}
bool Gannet::ThrowChum(int ci) {
    const Crew& c = crew[ci];
    if (c.dead || c.overboard || chum <= 0 || moored) return false;
    chum--; chumLeft += D().chumBlood;
    Say("A bucket of chum goes over the side");
    return true;
}
bool Gannet::GaffFloater(int ci) {
    Crew& c = crew[ci];
    if (c.dead || c.overboard) return false;
    Vector2 rail = RailWorld(ci);
    for (size_t i = 0; i < floaters.size(); i++) {
        Floater& f = floaters[i];
        if (Vector2Distance(f.p, rail) > 3.5f) continue;
        CatchRec r; r.name = f.name; r.kg = f.kg; r.price = f.price; r.sp = f.sp; r.grade = f.grade; r.src = CS_GUN;
        r.bycatch = f.price <= 0;
        hold.push_back(r);
        Say(TextFormat("Gaffed aboard: %s, %.1f kg", f.name.c_str(), f.kg));
        floaters.erase(floaters.begin() + i);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- the harpoon cannon (bow)
void Gannet::HarpoonInput(int ci, Vector2 aimDeck, bool fire, bool held, bool release, float dt) {
    (void)ci;
    if (!harpoonCannon) return;
    if (harpoonReload > 0) harpoonReload -= dt * (crew.size() > 1 ? 2 : 1);
    Fight& f = harpoon.fight;
    if (harpoon.state == RodState::Fighting) {
        f.reeling = held; f.drag = 0.5f * TackleOf(Tackle::Chair).strength;
        if (release) { f.end = FightEnd::Snapped; Say("The winch let go"); }
        return;
    }
    if (!fire || harpoonReload > 0) return;
    bool ex = explosiveLoaded && explosives > 0;
    if (!ex && harpoons <= 0) { Say("No harpoon loaded"); return; }
    Vector2 bow = boat.ToWorld({10.2f, 0}), aimW = boat.ToWorld(aimDeck);
    Vector2 d = Vector2Subtract(aimW, bow);
    float L = std::min(35.0f, Vector2Length(d));
    d = Vector2Length(d) > 0.1f ? Vector2Normalize(d) : boat.Forward();
    Vector3 from{bow.x, bow.y, -2.2f}, to{bow.x + d.x * L, bow.y + d.y * L, 0};
    Projectile p; p.kind = ex ? Shot::Explosive : Shot::Harpoon; p.p = from; p.v = Vector3Scale(Vector3Normalize(Vector3Subtract(to, from)), 60);
    p.owner = -2; p.dmg = SlotDmg(p.kind); p.tether = !ex; p.life = 2;
    shots.push_back(p);
    if (ex) { explosives--; explosiveLoaded = explosives > 0 && explosiveLoaded; } else harpoons--;
    harpoonReload = 4;
    if (eco) eco->AddNoise({bow.x, bow.y, 1}, 30);
}

// ---------------------------------------------------------------- the net
void Gannet::NetInput(int ci, bool held, bool cut, float dt) {
    Crew& c = crew[ci];
    float rate = (c.role == Role::Bosun ? 1.25f : 1.0f) * (c.Has(INJ_BROKEN_ARM) ? 0.5f : 1.0f);
    Trawl& n = net;
    if (cut && n.state == NetState::Snagged) {
        bool knife = false; for (const auto& s : c.slots) if (s.it == Item::Knife) knife = true;
        if (knife) { n = Trawl{}; n.state = NetState::Lost; Say("Cut loose: the net is gone (the Slipway sells another)"); }
        else Say("It needs a knife to cut the warps");
        return;
    }
    if (!held) return;
    switch (n.state) {
        case NetState::Stowed:
            n.state = NetState::Shooting; n.t = 0; Say("Shooting the trawl");
            break;
        case NetState::Shooting:
            n.t += dt * rate;
            if (n.t >= NET_SHOOT_S) { n.state = NetState::Down; n.t = 0; n.meshInit = false; Say("The trawl is down"); }
            break;
        case NetState::Down:
            n.state = NetState::Hauling; n.t = 0; Say("Hauling");
            break;
        case NetState::Hauling: {
            // a hand on the winch and one guiding the boom; alone, half speed
            bool helper = false;
            for (int k = 0; k < (int)crew.size(); k++) if (k != ci && !crew[k].dead && !crew[k].overboard && crew[k].deck == 0 && Vector2Distance(crew[k].p, Stations()[(int)StationKind::NetWinch].at) < 3.5f) helper = true;
            n.t += dt * rate * (helper ? 1.0f : 0.5f);
            if (n.t >= 20 + n.load / 40) {
                // the cod end opened over the sorting deck: one wet, flopping pile
                const auto& S = Species().sp;
                bool jellies = false;
                for (const auto& kv : n.catchKg) {
                    if (kv.second < 0.05f) continue;
                    const SpeciesRec& r = S[kv.first];
                    CatchRec rec; rec.name = r.name; rec.kg = kv.second; rec.price = r.price; rec.sp = kv.first; rec.grade = 0.9f; rec.src = CS_NET;
                    rec.bycatch = r.price <= 0 && !r.protectedSp; rec.protectedSp = r.protectedSp;
                    if (r.stings) jellies = true;
                    hold.push_back(rec);
                }
                Say(TextFormat("The cod end opens: %.0f kg on the sorting deck", n.load));
                if (jellies) Injure(ci, INJ_BURN, "moon jellies in the net");
                n = Trawl{};
            }
            break;
        }
        default: break;
    }
}

// ---------------------------------------------------------------- set gear
bool Gannet::HaulSetGear(int ci) {
    Crew& c = crew[ci];
    if (c.dead || c.overboard) return false;
    Vector2 rail = RailWorld(ci);
    const auto& S = Species().sp;
    for (size_t i = 0; i < longlines.size(); i++) {
        Longline& L = longlines[i];
        if (Vector2Distance(rail, L.a) > 10 && Vector2Distance(rail, L.b) > 10) continue;
        int fish = 0, heads = 0, empty = 0;
        for (const auto& h : L.hooks) {
            if (h.sp < 0) { empty++; continue; }
            CatchRec r; r.name = S[h.sp].name + (h.head ? " (head)" : ""); r.kg = h.kg; r.price = S[h.sp].price; r.sp = h.sp; r.grade = h.head ? 0.9f : 1.0f; r.src = CS_SET;
            hold.push_back(r); fish++; heads += h.head;
        }
        Say(TextFormat("Longline hauled: %d fish, %d heads, %d empty hooks", fish - heads, heads, empty));
        longlines.erase(longlines.begin() + i);
        AddItem(Item::Longline, 1);
        return true;
    }
    for (size_t i = 0; i < pots.size(); i++) {
        Pot& p = pots[i];
        if (Vector2Distance(rail, p.p) > 5) continue;
        for (const auto& kv : p.catchKg) { CatchRec r; r.name = S[kv.first].name; r.kg = kv.second; r.price = S[kv.first].price; r.sp = kv.first; r.src = CS_SET; hold.push_back(r); }
        Say(TextFormat("Pot hauled: %d aboard", p.n));
        pots.erase(pots.begin() + i);
        AddItem(Item::Pot, 1);
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- the crew's lives
void Gannet::Injure(int ci, int inj, const std::string& cause) {
    Crew& c = crew[ci];
    if (c.dead) return;
    bool serious = inj == INJ_BROKEN_ARM || inj == INJ_BITE;
    c.injuries |= inj;
    Say(TextFormat("%s: %s", cause.c_str(), InjuryName(inj)));
    if (serious && ++c.serious >= 2) Kill(ci, cause, c.overboard);   // a second serious injury in one night is death
}
void Gannet::Kill(int ci, const std::string& cause, bool lost) {
    Crew& c = crew[ci];
    if (c.dead) return;
    c.dead = true; c.bodyLost = lost; c.cause = cause; c.station = -1;
    // the dead walk the deck (a ghost): a body lost to the sea still leaves its ghost aboard
    if (c.overboard) { c.overboard = false; c.p = {-10.2f, 0}; }
    for (auto& r : rings) if (r.holder == ci) r.holder = -1;
    Say(TextFormat("A hand is dead: %s%s", cause.c_str(), lost ? " (lost to the sea)" : ""));
}
void Gannet::GoOverboard(int ci, const std::string& why) {
    Crew& c = crew[ci];
    if (c.overboard || c.dead) return;
    c.overboard = true; c.station = -1;
    Vector2 d = c.p; d.y = d.y < 0 ? -4.2f : 4.2f;
    c.swim = boat.ToWorld(d);
    c.drownT = (sea.weather == Weather::Storm || sea.weather == Weather::Squall) ? DROWN_STORM_S : DROWN_S;
    Say("Man overboard! (" + why + ")");
}
bool Gannet::AllDead() const {
    if (crew.empty()) return false;
    for (const auto& c : crew) if (!c.dead) return false;
    return true;
}

void Gannet::HitShot(Projectile& p, int hit) {
    const auto& SP = Species().sp;
    Vector2 fwd = boat.Forward();
    bool air = p.p.z < -1;
        EcoAgent& a = eco->agents[hit];
        const SpeciesRec& r = SP[a.sp];
        bool head = RandF(gRng) < (r.size <= 2 ? 0.35f : 0.2f);
        Vector3 at = p.p;
        // a tethered harpoon in something big turns into a fight on the cannon's winch
        if (p.kind == Shot::Harpoon && r.kgLo >= 15 && !eco->DamageAgent(hit, p.dmg * 0.3f, false, at)) {
            FishSpec f = eco->SpecOf(a.sp, RandF(gRng));
            Fight& F = harpoon.fight;
            F = Fight{}; F.tackle = Tackle::Chair; F.line = LineType::Wire; F.hook = Hook::Treble; F.drag = 60;
            Vector2 bow = boat.ToWorld({10.2f, 0});
            F.tip = {bow.x, bow.y, -2.2f}; F.outboard = fwd;
            F.HookFish(f, a.p, gRng++);
            harpoon.state = RodState::Fighting; harpoon.biteSpec = f; harpoonSp = a.sp;
            eco->TakeNear(a.sp, a.p);
            Say(std::string("Harpoon fast in a ") + r.name + ": the winch takes the strain");
            p.life = -1;
            return;
        }
        if (eco->DamageAgent(hit, p.dmg, head, at)) {
            if (air) { Say("A gull drops"); }
            else {
                Floater f; f.name = r.name; f.sp = a.sp; f.kg = r.kgLo + (r.kgHi - r.kgLo) * RandF(gRng); f.price = r.price; f.grade = ShotGrade(p.kind);
                f.p = {at.x, at.y}; f.tethered = p.tether || p.kind == Shot::Harpoon;
                floaters.push_back(f);
                Say(TextFormat("%s: %s dead%s", p.kind == Shot::Spear ? "Speared" : "Shot", r.name.c_str(), f.tethered ? " (on the tether)" : ", afloat"));
            }
        }
        if (eco) eco->AddNoise(at, ItemOf(p.kind == Shot::Bullet ? Item::Rifle : p.kind == Shot::Pellet ? Item::Shotgun : Item::Speargun).noise * 6);
        p.life = -1;
}

// ---------------------------------------------------------------- the step
void Gannet::StepGear(float dt) {
    const auto& SP = Species().sp;
    Vector2 fwd = boat.Forward();
    // projectiles
    for (auto& p : shots) {
        p.life -= dt;
        if (p.kind == Shot::Charge) {
            // in the air it arcs; in the water it sinks to 5 m and goes off
            if (p.p.z < 0) { p.v.z += G * dt; p.p = Vector3Add(p.p, Vector3Scale(p.v, dt)); if (p.p.z >= 0) { p.p.z = 0.01f; p.v = {0, 0, 1.5f}; } }
            else { p.p.z += 1.5f * dt; p.v = {0, 0, 1.5f}; }
            float floorD = eco ? eco->DepthAt({p.p.x, p.p.y}) : 30;
            if (p.p.z >= std::min(5.0f, floorD - 0.3f) || p.life <= 0) {
                std::vector<std::pair<int, float>> fl;
                if (eco) eco->DepthCharge(p.p, &fl);
                for (const auto& f : fl) if (f.second > 0.05f) {
                    Floater fo; fo.name = SP[f.first].name; fo.sp = f.first; fo.kg = f.second; fo.price = SP[f.first].price; fo.grade = 0.5f;
                    fo.p = {p.p.x + (RandF(gRng) - 0.5f) * 10, p.p.y + (RandF(gRng) - 0.5f) * 10};
                    floaters.push_back(fo);
                }
                // anyone in the water within 12 m is killed by the blast
                for (int k = 0; k < (int)crew.size(); k++) if (crew[k].overboard && !crew[k].dead && Vector2Distance(crew[k].swim, {p.p.x, p.p.y}) < 12) Kill(k, "the depth charge", true);
                if (eco && eco->ground == "lagoon") { fines += 30; Say("DEPTH CHARGE IN THE LAGOON: the Owners fine 30"); }
                Say("The charge goes off: stunned fish float up");
                p.life = -1;
            }
            continue;
        }
        // sub-steps of at most 0.25 m, so a round at 300 m/s can't step over a fish, a gull or a hand
        int subs = std::clamp((int)ceilf(Vector3Length(p.v) * dt / 0.25f), 1, 80);
        float h = dt / subs;
        bool done = false;
        for (int si = 0; si < subs && !done; si++) {
            Vector3 prev = p.p;
            if (p.p.z < 0) { p.v.z += G * h; p.v = Vector3Scale(p.v, 1 - 0.02f * h); }   // gravity and light drag in the air
            else {
                // the water: a round loses 90% in its first 0.3 m and stops within 1.5 m; a spear or harpoon keeps on
                float drag = (p.kind == Shot::Bullet || p.kind == Shot::Pellet) ? 12.0f : 1.2f;
                p.v = Vector3Scale(p.v, std::max(0.0f, 1 - drag * h));
            }
            p.p = Vector3Add(p.p, Vector3Scale(p.v, h));
            if (!p.inWater && p.p.z >= 0) {
                p.inWater = true;
                if (p.kind == Shot::Bullet || p.kind == Shot::Pellet) p.v = Vector3Scale(p.v, 0.1f);
                else if (p.kind == Shot::Flare) { flares.push_back({{p.p.x, p.p.y}, 20}); p.life = -1; done = true; break; }
                else p.v = Vector3Scale(p.v, 0.6f);
                prev = p.p;   // (the water's share of this sub-step starts here)
            }
            if (p.inWater) p.travel += Vector3Distance(prev, p.p);
            float maxWater = (p.kind == Shot::Bullet || p.kind == Shot::Pellet) ? 1.5f : p.kind == Shot::Spear ? 8.0f : 12.0f;
            if (p.inWater && (p.travel > maxWater || Vector3Length(p.v) < 1.0f || (p.kind == Shot::Harpoon && p.p.z > 6))) { p.life = -1; done = true; }
            if (p.kind == Shot::Flare) continue;
            // friendly fire: a round crossing a hand on deck (not its owner) hits them
            if (p.p.z < -0.8f && p.p.z > -2.8f) for (int k = 0; k < (int)crew.size(); k++) {
                if (k == p.owner || crew[k].dead || crew[k].overboard) continue;
                if (Vector2Distance(boat.ToWorld(crew[k].p), {p.p.x, p.p.y}) < 0.45f) { Injure(k, INJ_BITE, "a stray shot"); p.life = -1; done = true; break; }
            }
            if (done || !eco) continue;
            bool air = p.p.z < -1;
            int hit = eco->HitAgent(p.p, 0.2f, air);
            if (hit >= 0) { HitShot(p, hit); done = true; }
        }
        if (p.kind == Shot::Flare && p.life <= 0 && !p.inWater) flares.push_back({{p.p.x, p.p.y}, 20});
    }
    shots.erase(std::remove_if(shots.begin(), shots.end(), [](const Projectile& p) { return p.life <= 0; }), shots.end());
    // gulls hit hard enough give up
    if (eco) for (auto& a : eco->agents) if (a.alive && SP[a.sp].band == BAND_AIR && a.count < 4) { a.alive = false; Say("The gulls give up and wheel away"); }
    // shot fish afloat: they drift, bleed, and sink or are taken in a minute and a half; a tether reels them in
    for (auto& f : floaters) {
        f.life -= dt;
        if (eco) { f.p.x += eco->g->current.x * dt * 0.5f; f.p.y += eco->g->current.y * dt * 0.5f; eco->AddBlood({f.p.x, f.p.y, 0.5f}, 0.3f * dt); }
        if (f.tethered) {
            Vector2 rail = boat.ToWorld({0, 3.1f});
            float best = 1e9f;
            for (int k = 0; k < (int)crew.size(); k++) if (!crew[k].dead && !crew[k].overboard) { Vector2 r2 = RailWorld(k); float d = Vector2Distance(r2, f.p); if (d < best) { best = d; rail = r2; } }
            Vector2 d = Vector2Subtract(rail, f.p); float L = Vector2Length(d);
            if (L > 0.01f) f.p = Vector2Add(f.p, Vector2Scale(d, std::min(1.0f, 2.5f * dt / L)));
            if (L < 2.5f) { CatchRec r; r.name = f.name; r.kg = f.kg; r.price = f.price; r.sp = f.sp; r.grade = f.grade; r.src = CS_GUN; hold.push_back(r); Say(TextFormat("Reeled in on the tether: %s", f.name.c_str())); f.life = -1; }
        }
    }
    floaters.erase(std::remove_if(floaters.begin(), floaters.end(), [](const Floater& f) { return f.life <= 0; }), floaters.end());
    for (auto& fl : flares) fl.t -= dt;
    flares.erase(std::remove_if(flares.begin(), flares.end(), [](const FlareLight& f) { return f.t <= 0; }), flares.end());

    // the harpoon's fight: the winch is the reel, 120 kg steel the line; the fish tows her
    if (harpoon.state == RodState::Fighting) {
        Fight& F = harpoon.fight;
        Vector2 bow = boat.ToWorld({10.2f, 0});
        F.tip = {bow.x, bow.y, -2.2f}; F.outboard = fwd;
        bool manned = false; for (const auto& c : crew) if (c.station >= 0 && Stations()[c.station].kind == StationKind::Harpoon) manned = true;
        if (!manned) F.reeling = false;
        F.Step(dt);
        Vector2 pw = F.PullOnBoat();
        boat.extraForce = Vector2Add(boat.extraForce, Vector2Scale(pw, 9.81f));
        if (F.alongside && F.reeling) F.Land(0.9f);   // the tail rope, then the winch hauls it aboard
        if (F.end != FightEnd::None) {
            if (F.end == FightEnd::Landed) {
                CatchRec r; r.name = F.spec.name; r.kg = F.spec.kg; r.price = F.spec.price; r.sp = harpoonSp; r.grade = 0.75f; r.src = CS_GUN;
                hold.push_back(r); if (eco) eco->Harvest(harpoonSp, F.spec.kg, F.p, false);
                Say(TextFormat("Winched aboard: a %.0f kg %s", F.spec.kg, F.spec.name));
            } else if (eco) eco->AddBlood(F.p, F.spec.kg);
            harpoon.state = RodState::Idle; harpoonSp = -1;
        }
    }

    // the net: towed astern on 30 m of warp; its mouth runs deeper the slower she goes; it fills from what it sweeps
    Trawl& n = net;
    if (n.state == NetState::Down || n.state == NetState::Snagged || n.state == NetState::Hauling) {
        float speed = std::max(0.0f, boat.Speed());
        float haulOut = n.state == NetState::Hauling ? std::max(0.0f, 1 - n.t / (20 + n.load / 40)) : 1.0f;
        Vector2 stern = boat.ToWorld({-11.2f, 0});
        Vector2 mouth2 = Vector2Subtract(stern, Vector2Scale(fwd, NET_WARP * 0.8f * haulOut));
        float floorD = eco ? eco->DepthAt(mouth2) : 40;
        float want = std::clamp(9.0f - speed * 1.5f, 2.0f, 12.0f) * haulOut;
        n.depth += std::clamp(want - n.depth, -1.0f * dt, 1.0f * dt);
        if (n.depth > floorD - 0.6f) n.depth = std::max(0.5f, floorD - 0.6f);
        float width = n.Width(biggerNet);
        if (n.state == NetState::Down) {
            static float netYield = getenv("DEPTH_NETYIELD") ? (float)atof(getenv("DEPTH_NETYIELD")) : D().netYield;   // (the tuning grid)
            if (eco && speed > 0.4f) n.load += eco->Sweep({mouth2.x, mouth2.y, n.depth}, fwd, width, speed, dt, n.catchKg, netYield);
            // a snag on the reef or the crest: the bottom comes up under a deep-running mouth
            if (eco && floorD - n.depth < 0.9f && speed > 0.6f) {
                int h = eco->HabAt(mouth2);
                if (h == H_REEF || h == H_CREST || floorD < 4) { n.state = NetState::Snagged; n.backT = 0; Say("The warps shriek: the net is snagged. Back her off (astern) or cut it loose"); }
            }
        }
        if (n.state == NetState::Snagged) {
            boat.extraForce = Vector2Add(boat.extraForce, Vector2Scale(boat.vel, -30000.0f));
            boat.yawRate += 0.15f * dt;
            if (boat.telegraph < 0) { n.backT += dt; if (n.backT > 4) { n.state = NetState::Down; n.depth = std::max(0.5f, n.depth - 2.5f); Say("Backed off: the net comes free"); } }
            else n.backT = 0;
        } else {
            // the load drags and trims her by the stern
            boat.extraForce = Vector2Add(boat.extraForce, Vector2Scale(boat.vel, -(1500 + 40 * n.load)));
        }
        boat.loads.push_back({{-11.0f, 0}, n.load * 0.4f + 250});
        // the mesh (8 across, 6 back) for drawing: the mouth held open on the warps, the rest trailing and sagging
        if (!n.meshInit) {
            for (int j = 0; j < 6; j++) for (int i = 0; i < 8; i++) { Vector3 q{mouth2.x, mouth2.y, n.depth}; n.node[j * 8 + i] = n.prev[j * 8 + i] = q; }
            n.meshInit = true;
        }
        Vector2 side{-fwd.y, fwd.x};
        for (int i = 0; i < 8; i++) {
            float u = (i / 7.0f - 0.5f) * width;
            n.node[i] = {mouth2.x + side.x * u, mouth2.y + side.y * u, n.depth};
        }
        float seg = 2.2f + n.load / 400;
        for (int j = 1; j < 6; j++) for (int i = 0; i < 8; i++) {
            Vector3& q = n.node[j * 8 + i]; Vector3 v = Vector3Scale(Vector3Subtract(q, n.prev[j * 8 + i]), 0.85f);
            n.prev[j * 8 + i] = q;
            q = Vector3Add(q, v);
            q.z += (0.3f + n.load / 800) * dt;
            if (eco) { float fd = eco->DepthAt({q.x, q.y}); if (q.z > fd - 0.2f) q.z = fd - 0.2f; }
        }
        for (int it = 0; it < 4; it++) for (int j = 1; j < 6; j++) for (int i = 0; i < 8; i++) {
            Vector3& q = n.node[j * 8 + i]; const Vector3& up = n.node[(j - 1) * 8 + i];
            Vector3 d = Vector3Subtract(q, up); float L = Vector3Length(d);
            float taper = 1 - j * 0.12f;
            if (L > seg) q = Vector3Add(up, Vector3Scale(d, seg / L));
            // the cod end gathers toward the middle
            Vector3 mid = Vector3Lerp(n.node[j * 8 + 3], n.node[j * 8 + 4], 0.5f);
            q = Vector3Lerp(q, mid, (1 - taper) * 0.08f);
        }
    }

    // set gear: it drifts with the current and fishes the population where it lies (the agents are only near the boat)
    if (eco) {
        Vector2 cur = eco->g->current;
        for (auto& L : longlines) {
            L.age += dt;
            L.a = Vector2Add(L.a, Vector2Scale(cur, 0.3f * dt)); L.b = Vector2Add(L.b, Vector2Scale(cur, 0.3f * dt));
            for (int k = 0; k < (int)L.hooks.size(); k++) {
                SetHook& h = L.hooks[k];
                Vector2 at = Vector2Lerp(L.a, L.b, (k + 0.5f) / L.hooks.size());
                if (h.sp < 0 && h.kg >= 0) {
                    // a bite: each species that takes squid on a medium rod, by its density here and its hunger
                    for (int s : eco->g->species) {
                        const SpeciesRec& r = SP[s];
                        if (!r.Takes(Tackle::Medium) || r.netOnly || r.pots) continue;
                        float rate = 0.02f * eco->DensityAt(s, at) * (0.5f + eco->Hunger(s));
                        if (RandF(gRng) < rate * dt) { h.sp = s; h.kg = r.kgLo + (r.kgHi - r.kgLo) * powf(RandF(gRng), 1.6f); eco->Harvest(s, h.kg, {at.x, at.y, 4}, false); break; }
                    }
                } else if (h.sp >= 0 && !h.head) {
                    // depredation: the thieves strip a hooked fish (a barracuda leaves the head, a shark nothing)
                    for (int s : eco->g->species) {
                        if (!SP[s].thief) continue;
                        float rate = 0.004f * eco->DensityAt(s, at) * 20;
                        if (RandF(gRng) < rate * dt) { if (SP[s].thief == 1) { h.head = true; h.kg *= 0.45f; } else { h.sp = -1; h.kg = 0; } eco->AddBlood({at.x, at.y, 4}, 2); break; }
                    }
                }
            }
        }
        for (auto& p : pots) {
            p.age += dt;
            p.p = Vector2Add(p.p, Vector2Scale(cur, 0.05f * dt));   // (weighted: it barely drags)
            if (p.n >= 6) continue;
            for (int s : eco->g->species) {
                const SpeciesRec& r = SP[s];
                if (!r.pots && r.name != "reef octopus") continue;
                float rate = 0.05f * eco->DensityAt(s, p.p);
                if (RandF(gRng) < rate * dt) {
                    float kg = r.kgLo + (r.kgHi - r.kgLo) * RandF(gRng);
                    bool found = false; for (auto& kv : p.catchKg) if (kv.first == s) { kv.second += kg; found = true; }
                    if (!found) p.catchKg.push_back({s, kg});
                    p.n++; eco->Harvest(s, kg, {p.p.x, p.p.y, 5}, false);
                    break;
                }
            }
        }
    }

    // the life rings in flight and in the water
    for (auto& r : rings) {
        if (r.state == 1) {
            r.p = Vector2Add(r.p, Vector2Scale(r.v, dt)); r.haulT -= dt;
            if (r.haulT <= 0) r.state = 2;
        }
        if (r.state == 2) {
            if (eco) r.p = Vector2Add(r.p, Vector2Scale(eco->g->current, dt * 0.5f));
            if (r.holder < 0) for (int k = 0; k < (int)crew.size(); k++)
                if (crew[k].overboard && !crew[k].dead && Vector2Distance(crew[k].swim, r.p) < 2.0f) { r.holder = k; Say("They've got the ring: haul them in"); }
            if (r.holder >= 0) crew[r.holder].swim = r.p;
        }
    }

    // hands in the water: the cold, the screw, the stern ladder
    Vector2 stern = boat.ToWorld({-11.4f, 0});
    for (int k = 0; k < (int)crew.size(); k++) {
        Crew& c = crew[k];
        if (c.dead) continue;
        if (c.Has(INJ_BITE)) { c.bleedT += dt; if (eco && c.bleedT > 1) { c.bleedT = 0; Vector2 w = c.overboard ? c.swim : boat.ToWorld(c.p); eco->AddBlood({w.x, w.y, 1}, 1.5f); } }
        if (!c.overboard) continue;
        bool onRing = false; for (const auto& r : rings) if (r.holder == k) onRing = true;
        if (!onRing && eco) c.swim = Vector2Add(c.swim, Vector2Scale(eco->g->current, dt * 0.5f));
        c.drownT -= dt * (onRing ? 0.3f : 1.0f);
        if (eco) { eco->AddNoise({c.swim.x, c.swim.y, 0.5f}, 3 * dt); eco->AddVibration({c.swim.x, c.swim.y, 0.5f}, 2 * dt); }
        if (Vector2Distance(c.swim, stern) < 4.5f && boat.shaft > 0.1f) { Kill(k, "the screw", true); continue; }
        if (Vector2Distance(c.swim, stern) < 2.0f && boat.shaft < 0.05f) { c.overboard = false; c.p = {-10.4f, 0}; c.v = {0, 0}; Say("Up the stern ladder, aboard again"); continue; }
        if (c.drownT <= 0) Kill(k, "drowned", true);
    }
    // bycatch that's protected: the Owners fine a landed turtle not returned within 60 s
    for (auto& h : hold) if (h.protectedSp) { float was = h.aboardT; h.aboardT += dt; if (was < 60 && h.aboardT >= 60) { fines += 50; Say(TextFormat("LANDING OF PROTECTED %s NOTED: the Owners fine 50", h.name.c_str())); } }
}

// ---------------------------------------------------------------- --trawl-gear-test
int RunTrawlGearTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Trawl stage 6: shooting, the net, set gear, death and ghosts\n");
    const float dt = 1 / 60.0f;
    auto sp = [](const char* n) { return Species().Find(n); };
    auto setup = [&](Gannet& g, Eco& e, int crewN, uint32_t seed) {
        g.Init(crewN, seed, Weather::Calm);
        e.Init("lagoon", seed); e.agentBudget = 0; e.StartNight();
        g.eco = &e;
        g.boat.pos = {e.n * e.cell * 0.42f, e.n * e.cell * 0.5f}; g.boat.heading = 0;
        g.GiveStartingKit();
    };
    auto run = [&](Gannet& g, float secs) { for (int i = 0; i < (int)(secs * 60); i++) g.Step(dt); };
    // the kit
    {
        Gannet g; Eco e; setup(g, e, 1, 1);
        check(g.crew[0].slots[0].it == Item::Gaff && g.crew[0].slots[1].it == Item::Priest && g.crew[0].slots[2].it == Item::Ring && g.locker.size() == 1,
              "the starting kit: a gaff, a fish priest and the life ring in hand, the second gaff in the locker");
    }
    // ballistics: a rifle round dies in the first 1.5 m of water; in the air it falls
    {
        Gannet g; Eco e; setup(g, e, 1, 2);
        g.crew[0].slots[3] = {Item::Rifle, 10}; g.crew[0].sel = 3; g.crew[0].p = {0, 2.4f};
        g.UseItem(0, {0, 14}, true, true, true, dt);
        float maxWater = 0; bool entered = false;
        for (int i = 0; i < 120 && !g.shots.empty(); i++) { g.Step(dt); if (!g.shots.empty() && g.shots[0].inWater) { entered = true; maxWater = std::max(maxWater, g.shots[0].travel); } }
        check(entered && maxWater <= 1.6f && g.shots.empty(), TextFormat("a rifle round into the water stops within 1.5 m (%.2f m)", maxWater));
    }
    // a rifle kills a fish breaking the surface; the round never reaches one 4 m down, the speargun does
    {
        Gannet g; Eco e; setup(g, e, 1, 3);
        int bon = sp("bonito");
        Vector2 at = g.boat.ToWorld({0, 10});
        int ai = e.SpawnAgentPublic(bon, at); e.agents[ai].p = {at.x, at.y, 0.6f}; e.agents[ai].count = 1; e.agents[ai].fedT = 1000;
        g.crew[0].slots[3] = {Item::Rifle, 10}; g.crew[0].sel = 3; g.crew[0].p = {0, 2.4f};
        for (int k = 0; k < 6 && g.floaters.empty(); k++) {
            // aim at the fish: from the rail at 2.6 m, the round meets the water a little short of it and runs on 1.5 m
            e.agents.empty() ? (void)0 : (void)(e.agents[0].p = {at.x, at.y, 0.6f});
            g.crew[0].cool = 0; g.UseItem(0, {0, 9.3f}, true, true, true, dt);
            run(g, 0.5f);
        }
        check(!g.floaters.empty() && fabsf(g.floaters[0].grade - 0.7f) < 0.01f, TextFormat("rifle rounds kill a bonito at the surface: it floats, graded 70%% (%d afloat)", (int)g.floaters.size()));
        size_t h0 = g.hold.size();
        if (!g.floaters.empty()) g.floaters[0].p = g.RailWorld(0);
        g.crew[0].sel = 0; g.UseItem(0, {0, 5}, true, true, false, dt);
        check(g.hold.size() == h0 + 1, "the gaff takes it aboard from the rail");
        // 4 m down
        int sn = sp("snapper");
        Vector2 a2 = g.boat.ToWorld({0, 9});
        int bi = e.SpawnAgentPublic(sn, a2); e.agents[bi].count = 1; e.agents[bi].fedT = 1000;
        float hp0 = e.SpeciesHP(sn);
        for (int k = 0; k < 4; k++) { e.agents[bi].p = {a2.x, a2.y, 4}; g.crew[0].sel = 3; g.crew[0].cool = 0; g.UseItem(0, {0, 7.5f}, true, true, true, dt); run(g, 0.4f); }
        bool alive = false; for (const auto& a : e.agents) if (a.sp == sn && a.alive && a.hurt < 1) alive = true;
        check(alive, TextFormat("rounds never reach a snapper 4 m down (HP %.0f untouched)", hp0));
        g.crew[0].slots[3] = {Item::Speargun, 3}; size_t f0 = g.floaters.size(), hh = g.hold.size();
        for (int k = 0; k < 3 && g.floaters.size() == f0 && g.hold.size() == hh; k++) {
            for (auto& a : e.agents) if (a.sp == sn) a.p = {a2.x, a2.y, 4};
            g.crew[0].cool = 0; g.UseItem(0, {0, 5.0f}, true, true, true, dt); run(g, 0.8f);
        }
        run(g, 6);
        if (getenv("DEPTH_TRACE")) { printf("      floaters %d hold %d shots %d:", (int)g.floaters.size(), (int)g.hold.size(), (int)g.shots.size()); for (auto& h : g.hold) printf(" %s/%.2f", h.name.c_str(), h.grade); printf("\n"); for (auto& a : e.agents) printf("      agent %s z %.1f d %.1f hurt %.0f\n", Species().sp[a.sp].name.c_str(), a.p.z, Vector2Distance({a.p.x,a.p.y}, g.RailWorld(0)), a.hurt); }
        bool speared = false; for (const auto& h : g.hold) if (h.name == "snapper" && fabsf(h.grade - 0.85f) < 0.01f) speared = true;
        check(speared, "the speargun reaches it, and the tether reels it in, graded 85%");
    }
    // the shotgun and the gulls; gulls steal small fish off the deck, not gutted ones
    {
        Gannet g; Eco e; setup(g, e, 1, 4);
        int gs = sp("gull flock");
        int ai = e.SpawnAgentPublic(gs, g.boat.pos); e.agents[ai].count = 12; e.agents[ai].p.z = -3;
        CatchRec small; small.name = "grunt"; small.kg = 1; small.price = 1.5f; CatchRec iced = small; iced.gutted = iced.iced = true;
        g.hold = {small, iced};
        run(g, 4.2f);
        check(g.hold.size() == 1 && g.hold[0].gutted, "a gull flock over the deck takes an ungutted grunt; the gutted one is safe");
        int before = 12;
        g.crew[0].slots[3] = {Item::Shotgun, 8}; g.crew[0].sel = 3;
        for (int k = 0; k < 4; k++) {
            for (auto& a : e.agents) if (a.sp == gs) { Vector2 w = g.boat.ToWorld({2, 6}); a.p = {w.x, w.y, -3}; }
            g.crew[0].cool = 0;
            // aim high: the pellets cross the flock in the air
            Vector2 aim = {2, 6 * 3.0f};
            g.UseItem(0, aim, true, true, true, dt); run(g, 0.3f);
        }
        int left = 0; bool gone = true; for (const auto& a : e.agents) if (a.sp == gs && a.alive) { left = a.count; gone = false; }
        check(gone || left < before, TextFormat("the shotgun brings gulls down (%d left%s)", gone ? 0 : left, gone ? ": the rest wheel away" : ""));
    }
    // a depth charge: everything in 12 m floats up, Wake +15, the Lagoon fine, a swimmer in range dies
    {
        Gannet g; Eco e; setup(g, e, 2, 5);
        int sar = sp("sardine");
        Vector2 at = g.boat.ToWorld({-18, 0});
        int ai = e.SpawnAgentPublic(sar, at); e.agents[ai].count = 200; e.agents[ai].p.z = 4;
        g.crew[1].overboard = true; g.crew[1].swim = g.boat.ToWorld({-20, 3}); g.crew[1].drownT = 25;
        float w0 = e.wake;
        g.crew[0].slots[3] = {Item::Charge, 1}; g.crew[0].sel = 3; g.crew[0].p = {-10, 0};
        g.UseItem(0, {-14, 0}, true, true, false, dt);
        run(g, 6);
        float kg = 0; for (const auto& f : g.floaters) kg += f.kg;
        check(kg > 10 && fabsf(g.floaters.empty() ? 0 : g.floaters[0].grade - 0.5f) < 0.01f, TextFormat("the charge floats %.0f kg of stunned fish, graded 50%%", kg));
        check(e.wake >= w0 + 15 - 0.5f && g.fines >= 30, TextFormat("Wake +15 (%.0f) and the Owners' 30 for a charge in the Lagoon", e.wake));
        check(g.crew[1].dead && g.crew[1].bodyLost, "a hand in the water within 12 m is killed by the blast");
    }
    // the harpoon cannon: fast in a reef shark, the winch takes the strain and she's towed
    {
        Gannet g; Eco e; setup(g, e, 1, 6);
        g.harpoonCannon = true; g.harpoons = 2;
        int sh = sp("reef shark");
        Vector2 at = g.boat.ToWorld({22, 2});
        int ai = e.SpawnAgentPublic(sh, at); e.agents[ai].count = 1; e.agents[ai].fedT = 1000;
        int hs = -1; for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == StationKind::Harpoon) hs = i;
        g.crew[0].p = Stations()[hs].at; g.crew[0].station = hs;
        bool fast = false;
        for (int k = 0; k < 2 && !fast; k++) {
            for (auto& a : e.agents) if (a.sp == sh) a.p = {at.x, at.y, 1.0f};
            g.harpoonReload = 0; g.HarpoonInput(0, {21.5f, 2}, true, false, false, dt);
            for (int i = 0; i < 60 && !fast; i++) { g.Step(dt); fast = g.harpoon.state == RodState::Fighting; }
        }
        float pull = 0; for (int i = 0; i < 120 && fast; i++) { g.HarpoonInput(0, {20, 2}, false, false, false, dt); g.Step(dt); pull = std::max(pull, Vector2Length(g.harpoon.fight.PullOnBoat())); }
        check(fast && pull > 1, TextFormat("a harpoon fast in a reef shark: a fight on 120 kg steel, towing her (%.0f kgf)", pull));
        g.HarpoonInput(0, {20, 2}, false, false, true, dt); g.Step(dt);
        check(g.harpoon.state == RodState::Idle, "the winch can be let go in an emergency");
    }
    // the net: 15 s to shoot, it fills from the schools it sweeps, loads the stern, and hauls slower alone
    {
        Gannet g; Eco e; setup(g, e, 1, 7);
        int ws = -1; for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == StationKind::NetWinch) ws = i;
        g.crew[0].p = Stations()[ws].at; g.crew[0].station = ws;
        float t = 0; while (g.net.state != NetState::Down && t < 20) { g.NetInput(0, true, false, dt); g.Step(dt); t += dt; }
        check(g.net.state == NetState::Down && t > 11 && t < 13.5f, TextFormat("shooting the trawl takes %.0f s (15 s, the Bosun 25%% faster)", t));
        // tow through sardine balls
        g.boat.telegraph = 1; g.boat.pressure = 0.7f; g.boat.firebox = 6;
        int sar = sp("sardine"), jel = sp("moon jelly");
        for (int k = 0; k < 900; k++) {
            if (k % 60 == 0) { Vector2 w = g.boat.ToWorld({-(NET_WARP * 0.8f) - 11.2f + 4, 0}); int ai = e.SpawnAgentPublic(k == 300 ? jel : sar, w); e.agents[ai].count = 150; e.agents[ai].p.z = g.net.depth; }
            if (g.boat.pressure < 0.6f) g.boat.Shovel(1);
            g.Step(dt);
        }
        float load = g.net.load;
        if (getenv("DEPTH_TRACE")) { Vector2 st = g.boat.ToWorld({-11.2f, 0}); Vector2 m = Vector2Subtract(st, Vector2Scale(g.boat.Forward(), NET_WARP * 0.8f)); printf("      net state %d depth %.2f floor %.1f speed %.2f pos (%.0f,%.0f) agents %d\n", (int)g.net.state, g.net.depth, e.DepthAt(m), g.boat.Speed(), g.boat.pos.x, g.boat.pos.y, (int)e.agents.size()); }
        check(load > 20, TextFormat("towed through sardine balls the net fills (%.0f kg, running %.1f m down)", load, g.net.depth));
        g.boat.telegraph = 0;
        size_t h0 = g.hold.size();
        t = 0; g.NetInput(0, true, false, dt);
        while (g.net.state == NetState::Hauling && t < 200) { g.NetInput(0, true, false, dt); g.Step(dt); t += dt; }
        float wantT = (20 + load / 40) / 1.25f / 0.5f;
        check(g.net.state == NetState::Stowed && fabsf(t - wantT) < 3, TextFormat("hauling alone takes %.0f s (half speed without a hand on the boom; %.0f expected)", t, wantT));
        bool sard = false, jelly = false; for (size_t i = h0; i < g.hold.size(); i++) { if (g.hold[i].name == "sardine" && fabsf(g.hold[i].grade - 0.9f) < 0.01f) sard = true; if (g.hold[i].name == "moon jelly") jelly = g.hold[i].bycatch; }
        check(sard, "the cod end empties onto the sorting deck: sardine by the kilo, graded 90%");
        check(!jelly || g.crew[0].Has(INJ_BURN), "moon jellies in the net sting whoever empties it (a burn)");
    }
    // a snag on the crest: backing her off frees it; a knife cuts it loose and the net is lost
    {
        Gannet g; Eco e; setup(g, e, 1, 8);
        g.net.state = NetState::Down; g.net.depth = 6;
        // put the mouth over the crest: find a crest cell and set the boat 24 m ahead of it
        Vector2 crest{0, 0};
        for (float x = 0; x < e.n * e.cell && crest.x == 0; x += 4) if (e.HabAt({x, 300}) == H_CREST) crest = {x, 300};
        g.boat.heading = 0; g.boat.pos = {crest.x + NET_WARP * 0.8f + 11.2f, 300}; g.boat.vel = {1.5f, 0};
        for (int i = 0; i < 60 && g.net.state == NetState::Down; i++) { g.boat.vel = {1.5f, 0}; g.Step(dt); }
        check(g.net.state == NetState::Snagged, "over the crest the deep-running net snags");
        g.boat.telegraph = -1;
        for (int i = 0; i < 60 * 5 && g.net.state == NetState::Snagged; i++) g.Step(dt);
        check(g.net.state == NetState::Down, "backed off astern for 4 s, it comes free");
        g.net.state = NetState::Snagged;
        int ws = -1; for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == StationKind::NetWinch) ws = i;
        g.crew[0].station = ws; g.NetInput(0, false, true, dt);
        check(g.net.state == NetState::Snagged, "without a knife it can't be cut");
        g.crew[0].slots[3] = {Item::Knife, 0}; g.NetInput(0, false, true, dt);
        check(g.net.state == NetState::Lost, "with one, cut loose: the net is gone");
    }
    // set gear: a longline left over the reef catches (and is robbed); a pot on the reef takes crabs and lobster
    {
        Gannet g; Eco e; setup(g, e, 1, 9);
        g.baitSquid = 20;
        g.crew[0].slots[3] = {Item::Longline, 1}; g.crew[0].sel = 3; g.crew[0].p = {-10, 0};
        g.UseItem(0, {-12, 0}, true, true, false, dt);
        check(g.longlines.size() == 1 && g.longlines[0].hooks.size() == 20 && g.baitSquid == 0, "the longline is set astern: 20 hooks on 20 squid baits");
        g.crew[0].slots[3] = {Item::Pot, 1}; g.crew[0].p = {0, 2.4f};
        g.UseItem(0, {0, 5}, true, true, false, dt);
        // leave them for three hours of the night (three real minutes)
        Vector2 pot = g.pots.empty() ? Vector2{0, 0} : g.pots[0].p;
        // (the pot on the best crab and lobster ground on the chart)
        float bestD = 0; Vector2 best = pot;
        for (int k = 0; k < e.n * e.n; k += 3) { Vector2 q{(k % e.n + 0.5f) * e.cell, (k / e.n + 0.5f) * e.cell}; float d = e.DensityAt(sp("blue crab"), q) + e.DensityAt(sp("spiny lobster"), q); if (d > bestD) { bestD = d; best = q; } }
        if (!g.pots.empty()) g.pots[0].p = best;
        run(g, 180);
        int onHooks = 0, heads = 0; for (const auto& h : g.longlines[0].hooks) { onHooks += h.sp >= 0; heads += h.head; }
        check(onHooks >= 2, TextFormat("after three hours the longline has %d fish on it (%d heads)", onHooks, heads));
        int inPot = g.pots.empty() ? 0 : g.pots[0].n;
        if (getenv("DEPTH_TRACE")) printf("      pots %d best density %.3f depth %.1f\n", (int)g.pots.size(), bestD, e.DepthAt(best));
        check(inPot >= 1, TextFormat("the pot has %d in it", inPot));
        // haul both
        g.boat.pos = Vector2Subtract(g.longlines[0].a, Vector2Scale(g.boat.Forward(), -11.2f));
        g.crew[0].p = {-10.5f, 0};
        size_t h0 = g.hold.size();
        bool hauled = g.HaulSetGear(0);
        check(hauled && g.hold.size() >= h0 + (size_t)(onHooks) && g.longlines.empty(), "hauled at a buoy: the catch comes aboard and the longline goes back in hand");
    }
    // overboard: 25 s in the water; the screw kills; the stern ladder with the screw stopped; the life ring
    {
        Gannet g; Eco e; setup(g, e, 2, 10);
        g.crew[1].p = {-2, 2.5f}; g.GoOverboard(1, "slipped");
        check(g.crew[1].overboard && fabsf(g.crew[1].drownT - 25) < 0.01f, "a hand overboard has 25 s before the cold takes them");
        // the ring: thrown by hand 0 from the starboard rail, grabbed, hauled
        g.crew[0].p = {-2, 2.5f}; g.crew[0].sel = 2;
        Vector2 target = g.boat.ToDeck(g.crew[1].swim);
        g.UseItem(0, target, true, true, false, dt);
        for (int i = 0; i < 60; i++) g.Step(dt);
        bool grabbed = g.rings[0].holder == 1;
        for (int i = 0; i < 60 * 12 && g.crew[1].overboard; i++) { g.UseItem(0, target, false, true, false, dt); g.Step(dt); }
        check(grabbed && !g.crew[1].overboard && !g.crew[1].dead, "the life ring goes out on its rope, they grab it, and are hauled aboard");
        g.GoOverboard(1, "again");
        for (int i = 0; i < 60 * 26 && !g.crew[1].dead; i++) { g.crew[1].swim = Vector2Add(g.boat.pos, {0, 40}); g.Step(dt); }
        check(g.crew[1].dead && g.crew[1].bodyLost && g.crew[1].cause == "drowned", "left in the water, they drown at 25 s (lost to the sea)");
        Gannet g2; Eco e2; setup(g2, e2, 1, 11);
        g2.GoOverboard(0, "slipped"); g2.boat.telegraph = 2; g2.boat.shaft = 0.6f; g2.boat.pressure = 0.7f;
        g2.crew[0].swim = g2.boat.ToWorld({-13, 0});
        g2.Step(dt);
        check(g2.crew[0].dead && g2.crew[0].cause == "the screw", "near the stern with the screw turning: the screw");
        Gannet g3; Eco e3; setup(g3, e3, 1, 12);
        g3.GoOverboard(0, "slipped"); g3.boat.shaft = 0;
        g3.crew[0].swim = g3.boat.ToWorld({-13, 0});
        for (int i = 0; i < 60 * 3 && g3.crew[0].overboard; i++) { g3.Move(0, {1, 0}, false, dt); g3.Step(dt); }
        check(!g3.crew[0].overboard && !g3.crew[0].dead, "with the screw stopped, a swimmer climbs the stern ladder");
    }
    // injuries: a second serious one is death; a burn slows; a hooked hand can't reel; a bite bleeds until bandaged
    {
        Gannet g; Eco e; setup(g, e, 2, 13);
        g.Injure(0, INJ_BURN, "test");
        Vector2 p0 = g.crew[0].p; for (int i = 0; i < 60; i++) g.Move(0, {1, 0}, false, dt);
        float burned = g.crew[0].p.x - p0.x;
        check(burned < D().walk * 0.75f && !g.crew[0].dead, TextFormat("a burn slows a hand (%.1f m in a second)", burned));
        g.Injure(0, INJ_BITE, "a reef shark");
        float b0 = e.blood.Total(); run(g, 3);
        check(e.blood.Total() > b0, "a bite bleeds into the water");
        g.crew[0].slots[3] = {Item::Bandage, 3}; g.crew[0].sel = 3; g.UseItem(0, {0, 0}, true, true, false, dt);
        check(!g.crew[0].Has(INJ_BITE), "bandaged, it stops");
        g.Injure(0, INJ_BROKEN_ARM, "a fall");
        check(g.crew[0].dead && !g.crew[0].bodyLost, "a second serious injury in one night: dead (the body aboard)");
        g.Injure(1, INJ_HOOKED_HAND, "a hook");
        int rs = -1; for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == StationKind::PortRod) rs = i;
        g.crew[1].p = Stations()[rs].at; g.crew[1].station = rs;
        g.RodInput(1, false, {0, -10}, true, false, 0, false, false, 0);
        check(!g.rods[g.RodAt(rs)].reel, "a hooked hand can't reel");
        // friendly fire
        g.crew[1].station = -1; g.crew[1].p = {0, 1.2f}; g.crew[1].injuries = 0; g.crew[1].serious = 0;
        Gannet g4; Eco e4; setup(g4, e4, 2, 14);
        g4.crew[0].p = {-3, 0}; g4.crew[1].p = {0, 0};
        g4.crew[0].slots[3] = {Item::Rifle, 5}; g4.crew[0].sel = 3;
        g4.UseItem(0, {40, 0}, true, true, true, dt); run(g4, 0.2f);
        check(g4.crew[1].Has(INJ_BITE), "friendly fire is on: a round across the deck hits a hand");
    }
    // total loss, the dock's revival, the fines; a landed turtle
    {
        Gannet g; Eco e; Session ss; ss.Begin(g, e, 1, 15);
        ss.money = 200; g.crew[0].p = {-1, 0.8f};
        ss.CastOff(); g.boat.pos = Vector2Add(ss.harbour, {ss.harbourR + 10, 0}); g.Step(dt); ss.Step(dt);
        CatchRec f; f.name = "snapper"; f.kg = 3; f.price = 3; g.hold = {f};
        g.GoOverboard(0, "test"); g.crew[0].swim = Vector2Add(g.boat.pos, {0, 40});
        for (int i = 0; i < 60 * 27 && ss.phase == Phase::Night; i++) { g.Step(dt); ss.Step(dt); }
        check(ss.phase == Phase::Dock && g.hold.empty() && ss.night == 1, "the only hand drowned: total loss, the catch gone, the next night at the dock");
        check(fabsf(ss.money - 200 * 0.75f * 0.92f) < 0.5f, TextFormat("the Owners charge 25%% to salvage her and 8%% for a hand lost to the sea (%.0f of 200 left)", ss.money));
        check(!g.crew[0].dead && !g.crew[0].overboard && g.crew[0].injuries == 0, "at the dock the dead hand is back, injuries seen to");
        g.fines = 30; float m0 = ss.money; ss.Moor();
        check(fabsf(ss.money - (m0 - 30)) < 0.01f && g.fines == 0, "fines are taken at the dock");
        Gannet g2; Eco e2; setup(g2, e2, 1, 16);
        CatchRec t; t.name = "green turtle"; t.kg = 60; t.protectedSp = true; t.sp = sp("green turtle");
        g2.hold = {t}; run(g2, 61);
        check(g2.fines >= 50, "a landed turtle kept aboard past 60 s: the Owners fine 50");
        Gannet g3; Eco e3; setup(g3, e3, 1, 17);
        g3.hold = {t};
        int gt = -1; for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == StationKind::Gutting) gt = i;
        g3.crew[0].p = Stations()[gt].at; g3.crew[0].station = gt;
        for (int i = 0; i < 60 * 2; i++) { g3.Primary(0, true, dt); g3.Step(dt); }
        run(g3, 60);
        check(g3.hold.empty() && g3.fines == 0, "returned over the side at the sorting table in time: no fine");
    }
    printf(fails ? "trawl-gear-test: %d check(s) failed\n" : "trawl-gear-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}

} // namespace tw
