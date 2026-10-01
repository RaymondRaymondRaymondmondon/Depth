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

#include "trawl_weapons.h"

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
        {"(weapon)",           0,  0,  0, 0,  "A weapon from the Gunsmith's catalogue"},
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
    if (s.it == Item::Weapon && s.wpn >= 0 && s.wpn < (int)Weapons().size()) {
        // the spare reload goes into the gun; the spare is refilled at the locker
        const WeaponDef& w = Weapons()[s.wpn];
        if (!w.Gun()) return;
        int take = std::min(WeaponMagazine(w, s.att) - s.ammo, s.spare);
        if (take <= 0) { if (s.spare <= 0) Say("No spare reload: restock at the locker"); return; }
        s.ammo += take; s.spare -= take;
        c.reloadT = (w.cls == WC_LONGGUN ? 1.8f : 1.2f) * (HasAttachment(s.att, "speedloader") ? 0.5f : 1.0f);
    }
}
void Gannet::UseItem(int ci, Vector2 aimDeck, bool pressed, bool held, bool sight, float dt) {
    Crew& c = crew[ci];
    if (c.dead || c.overboard || c.station >= 0) return;
    if (c.cool > 0) c.cool -= dt;
    if (c.reloadT > 0) c.reloadT -= dt;
    Slot& s = c.slots[c.sel];
    Vector2 aimW = boat.ToWorld(aimDeck), from = boat.ToWorld(c.p);
    Vector3 muzzle{from.x, from.y, -RAIL_H};
    // aimed at something on the deck (a fish), the shot goes at deck height, not at the sea under it
    bool aimOnDeck = fabsf(aimDeck.y) < 2.8f && aimDeck.x > -11.0f && aimDeck.x < 10.0f;
    auto fire = [&](Shot k, float speed, float spreadDeg, int n) {
        for (int i = 0; i < n; i++) {
            Vector3 to{aimW.x, aimW.y, aimOnDeck ? -RAIL_H + 0.7f : 0.0f};
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
            if (pressed) { if (c.cool <= 0) c.cool = 0.5f; if (!GaffFloater(ci)) KillDeckFish(ci); }   // (cool: the swing the screens draw; a gun's cool-down never stops the gaff)
            break;
        case Item::Priest: case Item::Knife:
            if (pressed && c.cool <= 0) { c.cool = 0.4f; KillDeckFish(ci); }
            break;
        case Item::Weapon: {   // a row of the Gunsmith's catalogue (trawl_weapons.h)
            if (s.wpn < 0 || s.wpn >= (int)Weapons().size()) break;
            const WeaponDef& w = Weapons()[s.wpn];
            if (w.cls == WC_MELEE) {
                if (pressed && c.cool <= 0) { c.cool = WeaponCooldown(w, s.att); KillDeckFish(ci, WeaponReach(w), WeaponDamage(w, s.lvl, s.att) * (w.id == "coralclub" ? 1.5f : 1.0f), w.id == "coralclub"); }
                break;
            }
            if (!w.Gun()) break;
            bool rapid = w.speed == WS_RAPID;
            if (!(rapid ? held : pressed) || c.cool > 0 || c.reloadT > 0) break;
            if (s.ammo <= 0) { if (pressed) Say(s.spare > 0 ? "Click: empty (R to reload)" : "Click: empty, no spare reload (restock at the locker)"); break; }
            s.ammo--; c.cool = WeaponCooldown(w, s.att);
            // wet powder: cartridge guns misfire in rain (10%), a squall (25%), a storm or after a swim (40%), unless oilskinned
            bool cartridge = w.ammo == "rounds" || w.ammo == "shells";
            float misfire = 0;
            if (cartridge && !HasAttachment(s.att, "oilskin") && w.id != "captainpistol")
                misfire = sea.weather == Weather::Rain ? 0.10f : sea.weather == Weather::Squall ? 0.25f : sea.weather == Weather::Storm ? 0.40f : 0;
            static uint32_t wet = 0x9E3779B9u;   // (its own generator: the spread's draws from gRng follow this one)
            wet ^= wet << 13; wet ^= wet >> 17; wet ^= wet << 5;
            if ((wet >> 8) * (1.0f / 16777216.0f) < misfire) { Say(TextFormat("Misfire: the %s's powder is wet", w.name.c_str())); break; }
            size_t before = shots.size();
            Shot k = w.ammo == "spears" ? Shot::Spear : w.pellets > 1 ? Shot::Pellet : Shot::Bullet;
            fire(k, k == Shot::Spear ? 40.0f : 300.0f, WeaponSpread(w, s.att) * (sight ? 0.3f : 1.0f), w.pellets);
            float dmg = WeaponDamage(w, s.lvl, s.att);
            for (size_t q = before; q < shots.size(); q++) shots[q].dmg = dmg;
            if (eco) eco->AddNoise({muzzle.x, muzzle.y, 1}, WeaponNoise(w, s.att) * 6);
            if (w.id == "nitro" || w.id == "puntgun") {   // the recoil
                Vector2 back = Vector2Normalize(Vector2Subtract(c.p, aimDeck));
                c.v = Vector2Add(c.v, Vector2Scale(back, 3.0f));
                if (w.id == "puntgun") { c.fallen = true; c.fallT = D().fallTime; }
            }
            break;
        }
        default: break;
    }
}
Item DrawItemOf(const Slot& s) {
    if (s.it != Item::Weapon || s.wpn < 0 || s.wpn >= (int)Weapons().size()) return s.it;
    const WeaponDef& w = Weapons()[s.wpn];
    if (w.cls == WC_MELEE) return WeaponReach(w) >= 2 ? Item::Gaff : w.dmg >= 18 ? Item::Priest : Item::Knife;
    if (w.ammo == "spears" || w.ammo == "arrows") return Item::Speargun;
    if (w.ammo == "flares") return Item::Flare;
    if (w.ammo == "shells" || w.pellets > 1) return Item::Shotgun;
    return Item::Rifle;
}
const char* SlotName(const Slot& s) {
    if (s.it == Item::Weapon && s.wpn >= 0 && s.wpn < (int)Weapons().size()) return Weapons()[s.wpn].name.c_str();
    return ItemOf(s.it).name;
}
int* Gannet::AmmoStock(const std::string& k) {
    if (k == "rounds") return &ammoRounds;
    if (k == "shells") return &ammoShells;
    if (k == "spears") return &ammoSpears;
    if (k == "flares") return &ammoFlares;
    if (k == "pellets") return &ammoPellets;
    if (k == "rivets") return &ammoRivets;
    return nullptr;
}
void Gannet::RestockAtLocker(int ci) {
    for (auto& s : crew[ci].slots) {
        if (s.it != Item::Weapon || s.wpn < 0) continue;
        const WeaponDef& w = Weapons()[s.wpn];
        int* stock = AmmoStock(w.ammo);
        if (!stock || !w.Gun()) continue;
        int want = WeaponMagazine(w, s.att) - s.spare;
        int take = std::min(want, *stock);
        if (take > 0) { s.spare += take; *stock -= take; }
    }
}
// ---------------------------------------------------------------- fish on the deck (the playtest, 2026-10-01)
// A landed fish lies where it came aboard, alive, and every so often flops toward the nearest rail; one that
// reaches it goes back over the side. The priest, a gaff or a knife kills it where it lies (so does a shot, at a
// little cost to the grade); the gutting table kills what it guts. Netted fish come up stunned, and flop less.
const char* DeckBehaviourName(int b) { static const char* N[DB_COUNT] = {"flopper", "thrasher", "biter", "spearer", "grabber", "pincher", "stinger"}; return N[std::clamp(b, 0, DB_COUNT - 1)]; }
int DeckBehaviourOf(const std::string& name, float kg) {
    auto has = [&](const char* k) { return name.find(k) != std::string::npos; };
    if (has("marlin") || has("swordfish") || has("sailfish")) return DB_SPEARER;
    if (has("barracuda") || has("moray") || has("conger") || has("eel") || has("lingcod") || has("shark") || has("dogfish")) return DB_BITER;
    if ((has("octopus") || has("squid")) && kg >= 1.5f) return DB_GRABBER;
    if (has("crab") || has("lobster") || has("isopod")) return DB_PINCHER;
    if (has("ray") || has("jelly") || has("urchin") || has("trigger") || has("lionfish") || has("scorpion")) return DB_STINGER;
    if (kg >= 20 || has("tuna") || has("grouper") || has("halibut") || has("sturgeon") || has("sea bass")) return DB_THRASHER;
    return DB_FLOPPER;
}
float DeckFishHP(float kg) { return 8 + 6 * powf(std::max(0.0f, kg), 0.75f); }
static float FishLen(float kg) { return std::clamp(0.25f + sqrtf(std::max(0.01f, kg)) * 0.32f, 0.25f, 1.8f); }

// A blow on a deck fish (design doc v2, "The kill"): hit points off, blood on the planking, and if it dies the finishing
// blow's Killscore: melee 1.2, one-hit (from full HP) 1.5, a headshot 1.25, in the air 1.3, a long shot 1.3, heavy seas
// (rolled past 15 deg) 1.15, out of all light 1.2; multiplied together up to 4x. An explosive hit of more than twice its
// remaining HP blows it to chum: the Killscore is void and only 40% of the weight is recovered.
bool Gannet::HitDeckFish(int idx, float dmg, int by, int how, bool head, float range) {
    if (idx < 0 || idx >= (int)hold.size()) return false;
    CatchRec& h = hold[idx];
    if (h.dead || h.gutted) return false;
    if (h.hp < 0) { h.hpMax = h.hp = DeckFishHP(h.kg); }
    bool fullHP = h.hp >= h.hpMax - 0.01f;
    float blood = (how == KH_PELLET || how == KH_EXPLOSIVE) ? 3.0f : 1.0f;
    if (how == KH_EXPLOSIVE && dmg > 2 * h.hp) {
        // overkill: chum on the deck and in the sea, 40% of the weight left to sell
        float lost = h.kg * 0.6f;
        h.kg *= 0.4f; h.dead = true; h.hp = 0; h.killScore = 1; h.killHow = "OVERKILL: blown to chum"; h.killT = 0;
        deckBlood += 3 + lost;
        if (eco) eco->AddBlood({boat.ToWorld(h.deckAt).x, boat.ToWorld(h.deckAt).y, 0.5f}, lost * 2);
        Say(TextFormat("The %s is blown to chum", h.name.c_str()));
        return true;
    }
    h.hp -= dmg;
    if (h.hp > 0) { deckBlood += blood; h.actT = std::min(h.actT, 0.6f); return false; }   // (hurt, it fights harder for a moment)
    // the finishing blow
    float k = 1; std::string why;
    auto bonus = [&](bool on, float m, const char* name) { if (on) { k *= m; why += (why.empty() ? "" : ", "); why += name; } };
    bonus(how == KH_MELEE, 1.2f, "melee");
    bonus(fullHP, 1.5f, "one-hit");
    bonus(head, 1.25f, "headshot");
    bonus(h.airT > 0, 1.3f, "airborne");
    bonus(range > 25, 1.3f, "long shot");
    bonus(fabsf(boat.RollDeg()) > 15, 1.15f, "heavy seas");
    if (eco) { Vector2 w = boat.ToWorld(h.deckAt); bonus(eco->LightAt({w.x, w.y, 0}) < 0.04f, 1.2f, "in the dark"); }
    h.killScore = std::min(KILLSCORE_MAX, k);
    h.killHow = why.empty() ? "a plain kill" : why;
    h.killT = 0; h.dead = true; h.hp = 0; h.grabbed = -1;
    deckBlood += head ? blood * 0.5f : blood;
    Say(TextFormat("Killscore x%.2f on the %s (%s)", h.killScore, h.name.c_str(), h.killHow.c_str()));
    (void)by;
    return true;
}
const BirdDef& BirdOf(int k) {
    static const BirdDef B[BIRD_COUNT] = {{"herring gull", 3, 4}, {"brown pelican", 5, 12}, {"frigatebird", 2, 15}};
    return B[std::clamp(k, 0, BIRD_COUNT - 1)];
}
// junk in the net (design doc v2, "Junk"): worth nothing at the market; the bottle's map and the brass key open a
// landing's cache, three chart pieces reveal a hidden mark (the skiff and the Atoll)
const std::vector<JunkDef>& JunkTable() {
    // (design doc v2, pages 25-27; the weights - how often each comes up - are my call: pure junk is commonest)
    const unsigned ALL = 15, LW = 1 | 2, WG = 2 | 4, GA = 4 | 8, AT = 8;
    static const std::vector<JunkDef> T = {
        {"tin can", 0.5f, 1, 0.3f, JU_SELL, 10, ALL}, {"cork float", 0.5f, 1, 0.2f, JU_SELL, 10, ALL}, {"empty bottle", 0.5f, 1, 0.4f, JU_SELL, 10, ALL},
        {"old boot", 1, 1, 1.0f, JU_BOOT, 10, ALL},
        {"teacup", 3, 7, 0.2f, JU_SELL, 3, ALL}, {"spectacles", 3, 7, 0.1f, JU_SELL, 3, ALL}, {"brass doorknob", 3, 7, 0.4f, JU_SELL, 3, ALL},
        {"sea glass", 2, 2, 0.1f, JU_SELL, 14, LW},
        {"someone's lobster trap", 5, 5, 6.0f, JU_TRAP, 5, LW},
        {"tangled net", 2, 2, 3.0f, JU_SELL, 6, ALL},
        {"rusted anchor", 8, 8, 40.0f, JU_SELL, 3, WG}, {"whaler's harpoon head", 6, 6, 2.0f, JU_SELL, 3, WG},
        {"oil lantern", 10, 10, 1.5f, JU_SELL, 3, ALL}, {"rusty knife", 2, 2, 0.3f, JU_SELL, 5, ALL},
        {"waterlogged pistol", 3, 3, 1.0f, JU_SELL, 3, GA}, {"coin purse", 15, 60, 0.3f, JU_SELL, 3, ALL},
        {"drowned pocket watch", 25, 25, 0.2f, JU_SELL, 2, GA}, {"scrimshaw tooth", 30, 30, 0.3f, JU_SELL, 2, WG},
        {"old diving helmet", 50, 50, 15.0f, JU_SELL, 1, GA}, {"signet ring", 80, 80, 0.05f, JU_SELL, 0.5f, AT},
        {"idol fragment", 40, 40, 3.0f, JU_SELL, 1, AT}, {"human skull", 0, 0, 1.0f, JU_SELL, 1, GA},
        {"letters in oilcloth", 0, 0, 0.2f, JU_SELL, 2, ALL},
        {"message in a bottle", 0, 0, 0.5f, JU_MAP, 4, ALL}, {"brass key", 0, 0, 0.1f, JU_KEY, 4, ALL}, {"torn chart piece", 0, 0, 0.1f, JU_CHART, 5, ALL},
    };
    return T;
}
// junk from the sea: a cast brings it up 10% of the time on the Lagoon, a net haul one to three pieces. Sellable pieces go
// on the deck as stowed junk (the market buys them; never the quota); a bottle's map, a brass key and chart pieces are kept
// for the landings (the skiff); someone's lobster trap holds one to three crabs or lobsters; an old boot sometimes a crab.
void Gannet::CastJunk(Vector2 deckAt) { if (RandF(gRng) < D().junkPerCast) FindJunk(deckAt, "On the hook"); }
void Gannet::FindJunk(Vector2 deckAt, const char* how) {
    unsigned bit = 1;   // (the Lagoon; the other grounds come with their charts)
    const auto& T = JunkTable();
    float total = 0; for (const auto& j : T) if (j.grounds & bit) total += j.weight;
    float r = RandF(gRng) * total; const JunkDef* d = &T[0];
    for (const auto& j : T) if (j.grounds & bit) { if (r < j.weight) { d = &j; break; } r -= j.weight; }
    if (d->use == JU_MAP) { junkBottles++; Say(TextFormat("%s: a message in a bottle (a treasure map to a spot on the landings)", how)); return; }
    if (d->use == JU_KEY) { junkKeys++; Say(TextFormat("%s: a brass key (it opens a named chest ashore)", how)); return; }
    if (d->use == JU_CHART) { junkCharts++; Say(junkCharts % 3 == 0 ? TextFormat("%s: a torn chart piece - three of them make a chart!", how) : TextFormat("%s: a torn chart piece (%d of 3)", how, junkCharts % 3)); return; }
    CatchRec j; j.name = d->name; j.kg = d->kg; j.price = d->lo + (d->hi - d->lo) * RandF(gRng); j.junk = true; j.dead = true; j.gutted = j.iced = true;
    j.src = CS_HOOK; j.deckAt = deckAt;
    hold.push_back(j);
    Say(TextFormat("%s: %s", how, d->name));
    int crabs = d->use == JU_TRAP ? 1 + (int)(RandF(gRng) * 3) : d->use == JU_BOOT && RandF(gRng) < 0.3f ? 1 : 0;
    for (int k = 0; k < crabs; k++) {
        bool lob = d->use == JU_TRAP && RandF(gRng) < 0.4f;
        int sp = Species().Find(lob ? "spiny lobster" : "blue crab");
        CatchRec c; c.name = lob ? "spiny lobster" : "blue crab"; c.sp = sp; c.kg = lob ? 1.2f + RandF(gRng) : 0.4f + RandF(gRng) * 0.4f;
        c.price = sp >= 0 ? Species().sp[sp].price : (lob ? 6 : 2); c.src = CS_HOOK; c.deckAt = Vector2Add(deckAt, {0.3f * (k + 1), 0});
        hold.push_back(c);
    }
    if (crabs) Say(TextFormat("...with %d %s in it", crabs, d->use == JU_TRAP ? "crabs and lobsters" : "crab"));
}
int BirdKindOf(const std::string& s) {
    if (s == "gull flock") return BIRD_GULL;
    if (s == "brown pelican") return BIRD_PELICAN;
    if (s == "frigatebird") return BIRD_FRIGATE;
    return -1;
}
void Gannet::Steal(int idx, int kind) {
    if (idx < 0 || idx >= (int)hold.size()) return;
    Thief t; t.kind = kind; t.fish = hold[idx];
    t.p = boat.ToWorld(hold[idx].deckAt); t.z = -RAIL_H - 0.5f;
    // it climbs away over the nearer rail, slowly at first with the weight (a long shot's chance)
    Vector2 out = boat.ToWorld({hold[idx].deckAt.x, hold[idx].deckAt.y < 0 ? -6.0f : 6.0f});
    Vector2 d = Vector2Normalize(Vector2Subtract(out, t.p)); t.v = Vector2Scale(d, 5 - t.fish.kg * 0.4f);
    hold.erase(hold.begin() + idx);
    Say(TextFormat("A %s takes the %s: shoot it down!", BirdOf(kind).name, t.fish.name.c_str()));
    thieves.push_back(t);
}
void Gannet::StepThieves(float dt) {
    for (auto& t : thieves) { t.t += dt; t.p = Vector2Add(t.p, Vector2Scale(t.v, dt)); t.z = std::max(-14.0f, t.z - 1.6f * dt); }
    thieves.erase(std::remove_if(thieves.begin(), thieves.end(), [](const Thief& t) { return t.t > 8; }), thieves.end());   // (gone with it)
}
void Gannet::DropFish(int ti) {   // the thief lets go: the fish falls on her deck or into the sea; the bird flies on
    if (ti < 0 || ti >= (int)thieves.size()) return;
    Thief t = thieves[ti]; thieves.erase(thieves.begin() + ti);
    Vector2 lp = boat.ToDeck(t.p);
    if (fabsf(lp.x) < 9 && fabsf(lp.y) < 2.8f) { t.fish.deckAt = lp; hold.push_back(t.fish); }
    else { Floater f; f.name = t.fish.name; f.sp = t.fish.sp; f.kg = t.fish.kg; f.price = t.fish.price; f.grade = t.fish.grade; f.p = t.p; floaters.push_back(f); }
}
void Gannet::DropThief(int ti, int by) {
    if (ti < 0 || ti >= (int)thieves.size()) return;
    Thief t = thieves[ti]; thieves.erase(thieves.begin() + ti);
    const BirdDef& bd = BirdOf(t.kind);
    CatchRec bird; bird.name = bd.name; bird.kg = t.kind == BIRD_PELICAN ? 3.5f : t.kind == BIRD_FRIGATE ? 1.3f : 1.0f;
    bird.price = bd.value / bird.kg; bird.grade = 1; bird.src = CS_GUN; bird.dead = true; bird.sp = Species().Find(t.kind == BIRD_GULL ? "gull flock" : bd.name);
    bird.killScore = 1.3f; bird.killHow = KH_BULLET; bird.killT = 0;   // every bird kill is Airborne (x1.3)
    t.fish.killScore = std::max(t.fish.killScore, 1.0f);
    Vector2 lp = boat.ToDeck(t.p);
    if (fabsf(lp.x) < 9 && fabsf(lp.y) < 2.8f) {   // over her deck: both drop where they fall
        bird.deckAt = lp; t.fish.deckAt = Vector2Add(lp, {0.4f, 0});
        hold.push_back(t.fish); hold.push_back(bird);
        Say(TextFormat("Shot down! The %s and the %s drop on the deck", bd.name, t.fish.name.c_str()));
    } else {                                        // over the water: both float, to be gaffed or tethered
        Floater f; f.name = t.fish.name; f.sp = t.fish.sp; f.kg = t.fish.kg; f.price = t.fish.price; f.grade = t.fish.grade; f.p = t.p; floaters.push_back(f);
        Floater fb; fb.name = bird.name; fb.sp = bird.sp; fb.kg = bird.kg; fb.price = bird.price; fb.grade = 1; fb.p = Vector2Add(t.p, {0.6f, 0}); floaters.push_back(fb);
        Say(TextFormat("Shot down! The %s and the %s fall in the water", bd.name, t.fish.name.c_str()));
    }
    (void)by;
}
bool Gannet::CrateFish(int ci, float reach) {
    const Crew& c = crew[ci];
    int best = -1; float bd = reach;
    for (int i = 0; i < (int)hold.size(); i++) { const CatchRec& h = hold[i]; if (!h.dead || h.gutted || h.crated) continue; float d = Vector2Distance(h.deckAt, c.p); if (d < bd) { bd = d; best = i; } }
    if (best < 0) return false;
    hold[best].crated = true;
    hold[best].deckAt = {-9.6f, hold[best].deckAt.y >= 0 ? 1.6f : -1.6f};   // (the crates on the aft deck)
    Say(TextFormat("Into the catch crate: %s", hold[best].name.c_str()));
    return true;
}
bool Gannet::KillDeckFish(int ci, float reach, float dmgIn, bool headIn) {
    const Crew& c = crew[ci];
    int best = -1; float bd = reach;
    for (int i = 0; i < (int)hold.size(); i++) if (!hold[i].dead && !hold[i].gutted) { float d = Vector2Distance(hold[i].deckAt, c.p); if (d < bd) { bd = d; best = i; } }
    if (best < 0) return false;
    // what's in hand: the priest is made for the head; a knife or a gaff anywhere; bare hands for a little
    Item it = c.slots[c.sel].it;
    if (c.bot && it != Item::Knife && it != Item::Gaff) it = Item::Priest;   // (a bot at the table uses the table's priest)
    float dmg = it == Item::Priest ? 14.0f : it == Item::Knife ? 11.0f : it == Item::Gaff ? 9.0f : 5.0f;
    bool head = it == Item::Priest;
    if (dmgIn >= 0) { dmg = dmgIn; head = headIn; }   // (a catalogue weapon's own blow)
    CatchRec& h = hold[best];
    // a stinger handled bare-handed stings
    if (h.deckKind == DB_STINGER && it != Item::Priest && it != Item::Gaff && it != Item::Knife) Injure(ci, INJ_BURN, TextFormat("stung by the %s", h.name.c_str()));
    HitDeckFish(best, dmg, ci, KH_MELEE, head, bd);
    return true;
}
void Gannet::StepDeckFish(float dt) {
    // blood on the planking runs out through the scuppers into the sea (20% a second)
    if (deckBlood > 0.01f) {
        float out = deckBlood * std::min(1.0f, 0.2f * dt);
        deckBlood -= out;
        if (eco) { Vector2 w = boat.ToWorld({-6, boat.roll >= 0 ? 3.0f : -3.0f}); eco->AddBlood({w.x, w.y, 0.5f}, out); }
    } else deckBlood = 0;
    for (auto& c : crew) if (c.inkT > 0) c.inkT -= dt;
    for (size_t i = 0; i < hold.size();) {
        CatchRec& h = hold[i];
        if (h.killT >= 0) h.killT += dt;
        if (h.gutted || moored) { i++; continue; }
        if (h.hp < 0) {   // just aboard: a forage fish under a kilo dies on landing; anything bigger comes over alive and angry
            h.hpMax = h.hp = DeckFishHP(h.kg);
            h.deckKind = DeckBehaviourOf(h.name, h.kg);
            h.heading = RandF(gRng) * 6.2832f;
            h.actT = 2 + RandF(gRng) * 3;
            if (h.kg < 1 && !h.dead) h.dead = true;
            if (h.dead) h.hp = 0;
        }
        if (h.dead) { i++; continue; }
        if (h.airT > 0) h.airT -= dt;
        // ---- what it does on the deck
        auto nearest = [&](float r, bool ahead) {
            int best = -1; float bd = r;
            Vector2 fwd{cosf(h.heading), sinf(h.heading)};
            for (int k = 0; k < (int)crew.size(); k++) {
                const Crew& c = crew[k];
                if (c.dead || c.overboard || c.deck != 0 || c.z > 0.3f) continue;
                Vector2 d = Vector2Subtract(c.p, h.deckAt);
                float dist = Vector2Length(d);
                if (ahead && Vector2DotProduct(d, fwd) < dist * 0.5f) continue;   // (a bill only reaches what's in front of it)
                if (dist < bd) { bd = dist; best = k; }
            }
            return best;
        };
        static const bool noActs = getenv("DEPTH_NODECKACT") != nullptr;   // (diagnostics: the fish only flop)
        h.actT -= noActs ? 0 : dt;
        if (h.deckKind == DB_GRABBER && h.grabbed >= 0) {
            // it has a hand and hauls it toward the rail; ink over the eyes
            Crew& c = crew[h.grabbed];
            if (c.dead || c.overboard) h.grabbed = -1;
            else {
                Vector2 rail{c.p.x, c.p.y >= 0 ? 2.9f : -2.9f};
                c.p = Vector2MoveTowards(c.p, rail, 0.35f * dt);
                h.deckAt = Vector2Add(c.p, {0, c.p.y >= 0 ? -0.35f : 0.35f});
                if (fabsf(c.p.y) > 2.85f) { GoOverboard(h.grabbed, TextFormat("dragged over the rail by the %s", h.name.c_str())); h.grabbed = -1; }
                if (h.actT <= 0) { h.grabbed = -1; h.actT = 5; }   // (it lets go after a while)
            }
        }
        if (h.actT <= 0) {
            switch (h.deckKind) {
                case DB_THRASHER: {   // tail slaps knock the crew down (20 kg and over)
                    h.actT = 4 + RandF(gRng) * 3;
                    if (h.kg >= 20) for (auto& c : crew) if (!c.dead && !c.overboard && c.deck == 0 && Vector2Distance(c.p, h.deckAt) < 1.3f && !c.fallen) { c.fallen = true; c.fallT = D().fallTime; c.station = -1; Say(TextFormat("The %s's tail knocks a hand flat", h.name.c_str())); }
                    break;
                }
                case DB_BITER: {      // bites anyone within reach
                    h.actT = 5 + RandF(gRng) * 3;
                    int k = nearest(0.75f, false);
                    if (k >= 0) Injure(k, INJ_BITE, TextFormat("bitten by the landed %s", h.name.c_str()));
                    break;
                }
                case DB_SPEARER: {    // lunges with the bill every 6-10 s
                    h.actT = 6 + RandF(gRng) * 4;
                    int k = nearest(2.0f, true);
                    if (k >= 0) Injure(k, INJ_BROKEN_ARM, TextFormat("run through by the %s's bill", h.name.c_str()));
                    break;
                }
                case DB_GRABBER: {    // grabs a hand and drags them toward the rail; inks
                    h.actT = 3.5f;
                    int k = nearest(1.3f, false);
                    if (k >= 0 && h.grabbed < 0) { h.grabbed = k; crew[k].inkT = 3; crew[k].station = -1; Say(TextFormat("The %s grabs a hand and inks!", h.name.c_str())); }
                    else h.actT = 6;
                    break;
                }
                case DB_PINCHER: {    // pinches (the hand is slowed until treated)
                    h.actT = 4 + RandF(gRng) * 2;
                    int k = nearest(0.6f, false);
                    if (k >= 0 && !crew[k].Has(INJ_HOOKED_HAND)) Injure(k, INJ_HOOKED_HAND, TextFormat("pinched by the %s", h.name.c_str()));
                    break;
                }
                default: h.actT = 3; break;
            }
        }
        // ---- the flop: every so often it throws itself across the deck (in the air for a moment), mostly toward the
        // nearer rail; one that reaches the rail goes back over the side. Thrashers and grabbers barely move.
        h.flopT += dt;
        float every = h.src == CS_NET ? 16.0f : 7.0f + std::min(8.0f, h.kg * 0.5f);   // the small ones are the liveliest
        if (h.deckKind == DB_THRASHER || h.deckKind == DB_GRABBER) every *= 2.5f;
        if (h.grabbed < 0 && h.flopT >= every) {
            h.flopT = RandF(gRng) * 2;
            Vector2 toRail{0, h.deckAt.y >= 0 ? 1.0f : -1.0f};
            if (RandF(gRng) < 0.35f) { float a = RandF(gRng) * 6.2832f; toRail = {cosf(a), sinf(a)}; }
            float hop = 0.3f + RandF(gRng) * 0.5f;
            h.deckAt = Vector2Add(h.deckAt, Vector2Add(Vector2Scale(toRail, hop), {(RandF(gRng) - 0.5f) * 0.4f, 0}));
            h.deckAt.x = std::clamp(h.deckAt.x, -10.5f, 9.5f);
            h.heading += (RandF(gRng) - 0.5f) * 2.0f;
            h.airT = 0.45f;   // (a fish killed in the air scores Airborne)
            if (fabsf(h.deckAt.y) > 2.8f) {
                Say(TextFormat("The %s flops back over the side", h.name.c_str()));
                if (eco) eco->AddBlood({boat.ToWorld(h.deckAt).x, boat.ToWorld(h.deckAt).y, 0.5f}, h.kg * 0.5f);
                hold.erase(hold.begin() + i);
                continue;
            }
        }
        i++;
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
        r.dead = true;                                     // (it was dead in the water)
        r.deckAt = {c.p.x, c.p.y * 0.6f};                  // swung inboard of the hand
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
                    rec.deckAt = {-8.6f + (RandF(gRng) - 0.5f) * 2.0f, (RandF(gRng) - 0.5f) * 2.4f};   // spilled across the sorting deck
                    if (r.stings) jellies = true;
                    hold.push_back(rec);
                }
                Say(TextFormat("The cod end opens: %.0f kg on the sorting deck", n.load));
                if (jellies) Injure(ci, INJ_BURN, "moon jellies in the net");
                for (int k = 1 + (int)(RandF(gRng) * 3); k > 0; k--) FindJunk({-8.6f + (RandF(gRng) - 0.5f) * 2.0f, (RandF(gRng) - 0.5f) * 2.4f}, "Junk in the net");
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
            r.dead = h.head; r.deckAt = {crew[ci].p.x + (RandF(gRng) - 0.5f) * 1.5f, crew[ci].p.y * 0.6f};
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
        for (const auto& kv : p.catchKg) { CatchRec r; r.name = S[kv.first].name; r.kg = kv.second; r.price = S[kv.first].price; r.sp = kv.first; r.src = CS_SET; r.deckAt = {crew[ci].p.x, crew[ci].p.y * 0.6f}; hold.push_back(r); }
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
        bool tethered = p.tether || p.kind == Shot::Harpoon;
        if (eco->DamageAgent(hit, p.dmg, head, at)) {
            if (air) { Say("A gull drops"); }
            else if (tethered || at.z < 1.5f) {
                // on a tether, or killed right at the surface: it floats and can be gaffed (at a shot fish's grade)
                Floater f; f.name = r.name; f.sp = a.sp; f.kg = r.kgLo + (r.kgHi - r.kgLo) * RandF(gRng); f.price = r.price; f.grade = ShotGrade(p.kind);
                f.p = {at.x, at.y}; f.tethered = tethered;
                floaters.push_back(f);
                Say(TextFormat("%s: %s dead%s", p.kind == Shot::Spear ? "Speared" : "Shot", r.name.c_str(), f.tethered ? " (on the tether)" : ", afloat"));
            } else {
                // shot dead under the surface with nothing on it: it sinks away in a cloud of blood. Bullets sprayed at
                // passing fish feed the water, not the hold (design doc: a shot trades value for speed and noise)
                eco->AddBlood(at, r.MeanKg() * 6);
                Say(TextFormat("Shot: the %s sinks, bleeding", r.name.c_str()));
            }
        } else if (!air) {
            // wounded: it runs, and bleeds into the scent grid as it goes
            a.flash = 0.5f; a.hunger = std::min(a.hunger, 0.2f);
            eco->AddBlood(at, r.MeanKg() * 2);
        }
        if (eco) eco->AddNoise(at, ItemOf(p.kind == Shot::Bullet ? Item::Rifle : p.kind == Shot::Pellet ? Item::Shotgun : Item::Speargun).noise * 6);
        p.life = -1;
}

// ---------------------------------------------------------------- the step
void Gannet::StepGear(float dt) {
    const auto& SP = Species().sp;
    Vector2 fwd = boat.Forward();
    StepDeckFish(dt);
    for (int ci = 0; ci < (int)crew.size(); ci++) if (crew[ci].station >= 0 && Stations()[crew[ci].station].kind == StationKind::Locker) RestockAtLocker(ci);
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
            // a round into a bird making off with a fish: both come down (on her deck if it's over her, else afloat)
            if (!done && p.kind != Shot::Harpoon) for (int ti = 0; ti < (int)thieves.size(); ti++) {
                Thief& t = thieves[ti];
                if (Vector3Distance(p.p, {t.p.x, t.p.y, t.z}) > (p.kind == Shot::Pellet ? 1.1f : 0.7f)) continue;
                DropThief(ti, p.owner);
                p.life = -1; done = true; break;
            }
            // a round into a live fish on the deck kills it where it lies (a little off the grade: a hole in the flank)
            if ((p.kind == Shot::Bullet || p.kind == Shot::Pellet || p.kind == Shot::Spear) && p.p.z < -RAIL_H + 1.0f && p.p.z > -RAIL_H - 0.6f) {
                Vector2 lp = boat.ToDeck({p.p.x, p.p.y});
                for (int hi = 0; hi < (int)hold.size(); hi++) {
                    CatchRec& h = hold[hi];
                    if (h.dead || h.gutted || Vector2Distance(h.deckAt, lp) > 0.45f) continue;
                    // the head is the front fifth of the fish: a round through it is a headshot
                    Vector2 headAt = Vector2Add(h.deckAt, Vector2Scale({cosf(h.heading), sinf(h.heading)}, FishLen(h.kg) * 0.4f));
                    bool head = Vector2Distance(headAt, lp) < 0.15f + FishLen(h.kg) * 0.08f;
                    float range = p.owner >= 0 && p.owner < (int)crew.size() ? Vector2Distance(crew[p.owner].p, h.deckAt) : 0;
                    int how = p.kind == Shot::Pellet ? KH_PELLET : p.kind == Shot::Spear ? KH_SPEAR : KH_BULLET;
                    h.grade *= 0.97f;   // (a hole in the flank)
                    HitDeckFish(hi, p.dmg, p.owner, how, head, range);
                    p.life = -1; done = true; break;
                }
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
    if (eco) for (auto& a : eco->agents) if (a.alive && SP[a.sp].band == BAND_AIR && SP[a.sp].schoolHi > 1 && a.count < 4) { a.alive = false; Say("The gulls give up and wheel away"); }
    StepThieves(dt);
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
            if (L < 2.5f) { CatchRec r; r.name = f.name; r.kg = f.kg; r.price = f.price; r.sp = f.sp; r.grade = f.grade; r.src = CS_GUN; r.dead = true; Vector2 rl = boat.ToDeck(rail); r.deckAt = {rl.x, rl.y * 0.6f}; hold.push_back(r); Say(TextFormat("Reeled in on the tether: %s", f.name.c_str())); f.life = -1; }
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
                r.dead = true; r.deckAt = {8.4f, 0};      // winched up over the bow, dead on the iron
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
        CatchRec small; small.name = "grunt"; small.kg = 1; small.price = 1.5f; small.dead = true; CatchRec iced = small; iced.gutted = iced.iced = true;
        g.hold = {small, iced};
        run(g, 4.2f);
        check(g.hold.size() == 1 && g.hold[0].gutted, "a gull flock over the deck takes a dead grunt left out; the gutted one is safe");
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
        // the deck kill (playtest, 2026-10-01): a landed fish flops for the rail until it's clubbed, shot or gutted
        CatchRec live; live.name = "snapper"; live.kg = 4; live.price = 3; live.deckAt = {-2, 1.6f};   // (4 kg: the gulls take anything under 3 left on deck)
        Gannet g4; Eco e4; setup(g4, e4, 1, 18);
        g4.crew[0].p = {6, 0};
        g4.hold = {live}; run(g4, 120);
        check(g4.hold.empty(), "a live fish nobody tends flops back over the rail within two minutes");
        Gannet g5; Eco e5; setup(g5, e5, 1, 19);
        g5.hold = {live}; g5.crew[0].p = {-2, 0.6f};
        run(g5, dt);   // (aboard: its hit points are set)
        g5.crew[0].sel = 1;   // the fish priest
        bool far = !g5.KillDeckFish(0, 0.5f);
        int blows = 0; while (blows < 6 && !g5.hold.empty() && !g5.hold[0].dead) { g5.KillDeckFish(0); blows++; }
        run(g5, 120);
        check(far && g5.hold.size() == 1 && g5.hold[0].dead, TextFormat("the priest reaches it from a step away (not from across the deck), and a dead fish stays put (%d blows)", blows));
        check(fabsf(DeckFishHP(4) - 25) < 0.6f && fabsf(DeckFishHP(40) - 103) < 1 && fabsf(DeckFishHP(250) - 385) < 2, "hit points 8 + 6 x kg^0.75: a 4 kg snapper 25, a 40 kg fish 103, a 250 kg marlin 385");
        check(blows == 2 && !g5.hold.empty() && fabsf(g5.hold[0].killScore - 1.5f) < 0.01f, TextFormat("two blows of the priest on a 4 kg snapper; the finishing one scores melee x headshot (x%.2f: %s)", g5.hold.empty() ? 0 : g5.hold[0].killScore, g5.hold.empty() ? "" : g5.hold[0].killHow.c_str()));
        {
            Gannet k; Eco ek; setup(k, ek, 1, 30);
            CatchRec a = live; k.hold = {a, a, a}; run(k, dt);
            k.HitDeckFish(0, 999, 0, KH_BULLET, true, 30);
            float big = k.hold[0].killScore;
            check(fabsf(big - std::min(KILLSCORE_MAX, 1.5f * 1.25f * 1.3f)) < 0.01f, TextFormat("a one-hit headshot from 30 m: x%.2f (one-hit, headshot, long shot)", big));
            k.hold[1].airT = 0.3f; k.hold[1].hp = 3;
            k.HitDeckFish(1, 10, 0, KH_MELEE, false, 1);
            check(fabsf(k.hold[1].killScore - 1.2f * 1.3f) < 0.01f, "a club on a fish in the air: melee x airborne");
            float kg0 = k.hold[2].kg;
            k.HitDeckFish(2, 400, 0, KH_EXPLOSIVE, false, 3);
            check(k.hold[2].dead && k.hold[2].killScore == 1 && fabsf(k.hold[2].kg - kg0 * 0.4f) < 0.01f, "an explosive overkill blows it to chum: no Killscore and 40% of the weight");
            CatchRec sold = k.hold[0]; sold.fresh = 1; sold.price = 3;
            Session sv; sv.G = &k; sv.E = &ek;
            CatchRec plain = sold; plain.killScore = 1;
            check(fabsf(sv.Value(sold) / sv.Value(plain) - big) < 0.01f, "the Killscore multiplies what the fish sells for");
            float b0 = k.deckBlood; run(k, 10);
            check(b0 > 1 && k.deckBlood < b0 * 0.2f, TextFormat("blood on the deck (%.1f) runs out through the scuppers at 20%% a second", b0));
        }
        {   // step 3: the Gunsmith's catalogue, buying, the one-long-weapon rule, a revolver on a deck fish, upgrades,
            // attachments, the spare reload and the locker, wet powder
            check(Weapons().size() == 41 && Attachments().size() == 18, TextFormat("the catalogue loads: %d weapons, %d attachments", (int)Weapons().size(), (int)Attachments().size()));
            Gannet gs; Eco es; Session ss; ss.Begin(gs, es, 1, 40); ss.money = 5000;
            gs.crew[0].slots[2] = Slot{}; gs.crew[0].slots[3] = Slot{};   // (the starting kit fills all four: make room)
            std::string why;
            bool rev = ss.GunBuy(0, "revolver", &why);
            int rs = -1; for (int k = 0; k < 4; k++) if (gs.crew[0].slots[k].it == Item::Weapon) rs = k;
            check(rev && rs >= 0 && gs.crew[0].slots[rs].ammo == 6 && std::string(SlotName(gs.crew[0].slots[rs])) == "Service revolver", "a service revolver bought at the Gunsmith goes into a free slot, loaded with six");
            bool nitro = !ss.GunBuy(0, "nitro", &why);
            check(nitro, TextFormat("the Nitro express waits for the third deadline (%s)", why.c_str()));
            ss.GunBuy(0, "shotgun"); bool second = !ss.GunBuy(0, "carbine", &why);
            check(second, TextFormat("a hand carries one long weapon at most (%s)", why.c_str()));
            const WeaponDef& R = Weapons()[gs.crew[0].slots[rs].wpn];
            float d0 = WeaponDamage(R, 0, gs.crew[0].slots[rs].att);
            check(ss.GunUpgrade(0, rs) && fabsf(WeaponDamage(R, gs.crew[0].slots[rs].lvl, gs.crew[0].slots[rs].att) - d0 * 1.15f) < 0.01f && UpgradePrice(Weapons()[WeaponIndex("rifle")], 0) == 120, "a damage upgrade adds 15% (60 sh; doubled for the long rifle)");
            bool sight = ss.GunAttach(0, rs, "sight"), choke = !ss.GunAttach(0, rs, "choke", &why), drum = !AttachmentFits(Attachments()[AttachmentIndex("drum")], R);
            check(sight && choke && drum && HasAttachment(gs.crew[0].slots[rs].att, "sight"), "attachments fit by the table: a sight on the revolver, but no choke, and the drum only on the chatter gun");
            // out at sea: the revolver into a live fish on the deck
            gs.moored = false;
            CatchRec f; f.name = "snapper"; f.kg = 4; f.price = 3; f.deckAt = {-2, 1.2f};
            gs.hold = {f}; gs.crew[0].p = {-2, -0.6f}; gs.crew[0].sel = rs; gs.crew[0].station = -1;
            run(gs, dt);
            for (int sh = 0; sh < 6 && !gs.hold.empty() && !gs.hold[0].dead; sh++) {
                size_t n0 = gs.shots.size();
                gs.crew[0].cool = 0;   // (the gun's cool-down only runs inside UseItem, which a frame calls every step)
                gs.UseItem(0, {-2, 1.2f}, true, true, false, dt);
                if (getenv("DEPTH_TRACE") && gs.shots.size() > n0) { const Projectile& p = gs.shots.back(); printf("    shot from (%.1f,%.1f,%.1f) v (%.0f,%.0f,%.0f) fish at deck (%.1f,%.1f) hp %.0f\n", p.p.x, p.p.y, p.p.z, p.v.x, p.v.y, p.v.z, gs.hold[0].deckAt.x, gs.hold[0].deckAt.y, gs.hold[0].hp); }
                run(gs, 0.6f);
            }
            check(!gs.hold.empty() && gs.hold[0].dead, TextFormat("revolver rounds aimed at a fish on the deck kill it (Killscore x%.2f: %s)", gs.hold.empty() ? 0 : gs.hold[0].killScore, gs.hold.empty() ? "" : gs.hold[0].killHow.c_str()));
            // the spare reload and the locker
            Slot& sl = gs.crew[0].slots[rs];
            sl.ammo = 0; sl.spare = 0; gs.ammoRounds = 20;
            gs.Reload(0);
            bool noSpare = sl.ammo == 0;
            int lk = -1; for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == StationKind::Locker) lk = i;
            gs.crew[0].station = lk; run(gs, dt); gs.crew[0].station = -1;
            bool restocked = sl.spare == 6 && gs.ammoRounds == 14;
            gs.Reload(0);
            check(noSpare && restocked && sl.ammo == 6 && sl.spare == 0, "with no spare there's no reload; at the locker the spare fills from the ship's stock, and R loads it");
            bool ammo = ss.AmmoBuy("shells") && gs.ammoShells == 8;
            check(ammo, "the Gunsmith sells ammunition into the locker by the pack (8 shells)");
            // wet powder in a squall
            gs.sea.weather = Weather::Squall; sl.ammo = 1000;
            int fired = 0; size_t before = gs.shots.size(); int misfires = 0;
            for (int sh = 0; sh < 400; sh++) {
                size_t n0 = gs.shots.size(), l0 = gs.log.size();
                gs.crew[0].cool = 0; gs.crew[0].reloadT = 0;
                gs.UseItem(0, {6, 12}, true, true, false, dt);
                if (gs.shots.size() > n0) fired++;
                for (size_t q = l0; q < gs.log.size(); q++) if (gs.log[q].find("Misfire") != std::string::npos) misfires++;
                gs.shots.clear();
            }
            (void)before;
            (void)misfires;   // (the log is capped: count what fired instead)
            float rate = (400 - fired) / 400.0f;
            check(rate > 0.18f && rate < 0.32f, TextFormat("in a squall a cartridge gun misfires about a quarter of the time (%.0f%%, %d of 400 fired)", rate * 100, fired));
        }
        {   // step 4: the catch crates and the birds (a gull flock over the deck)
            Gannet b; Eco eb; setup(b, eb, 1, 33);
            int gull = Species().Find("gull flock");
            CatchRec dead; dead.name = "grunt"; dead.kg = 1.5f; dead.price = 1.5f; dead.dead = true; dead.deckAt = {-3, 0.5f};
            CatchRec alive = dead; alive.dead = false; alive.name = "snapper"; alive.kg = 2.5f; alive.deckAt = {2, -0.5f};
            CatchRec boxed = dead; boxed.name = "jack crevalle"; boxed.kg = 2.0f; boxed.deckAt = {-4, -0.5f};
            b.hold = {dead, alive, boxed};
            b.crew[0].p = {-4, -0.1f};
            run(b, dt);
            bool crated = b.CrateFish(0) && b.hold[2].crated;
            int ai = eb.SpawnAgentPublic(gull, b.boat.pos); eb.agents[ai].count = 8; eb.agents[ai].p.z = -3;
            for (int i = 0; i < 60 * 9; i++) { eb.agents[ai].p = {b.boat.pos.x, b.boat.pos.y, -3}; eb.agents[ai].alive = true; b.Step(dt); }
            bool deadGone = true, aliveKept = false, boxKept = false;
            for (const auto& h : b.hold) { if (h.name == "grunt") deadGone = false; if (h.name == "snapper") aliveKept = true; if (h.name == "jack crevalle") boxKept = true; }
            check(crated, "E beside a dead fish swings it into a catch crate");
            check(deadGone && boxKept && aliveKept, "the gulls take the dead fish left on the deck, but not one in a crate or one still alive");
        }
        {   // a pelican lifts what a gull can't (5 kg); shot on its way off, it and the fish drop on the deck, a bird worth 12
            Gannet b; Eco eb; setup(b, eb, 1, 34);
            int pel = Species().Find("brown pelican");
            CatchRec heavy; heavy.name = "jack crevalle"; heavy.kg = 4.5f; heavy.price = 2; heavy.dead = true; heavy.deckAt = {-2, 0.3f};
            b.hold = {heavy};
            int ai = pel >= 0 ? eb.SpawnAgentPublic(pel, b.boat.pos) : -1;
            for (int i = 0; i < 60 * 5 && ai >= 0 && b.thieves.empty(); i++) { eb.agents[ai].p = {b.boat.pos.x, b.boat.pos.y, -3}; eb.agents[ai].alive = true; b.Step(dt); }
            bool took = b.hold.empty() && b.thieves.size() == 1 && b.thieves[0].kind == BIRD_PELICAN;
            check(took, "a brown pelican takes a 4.5 kg fish left out (a gull couldn't lift it)");
            if (took) {
                eb.agents[ai].alive = false;
                Gannet::Thief& t = b.thieves[0];
                Projectile p; p.kind = Shot::Bullet; p.owner = 0; p.life = 2; p.dmg = 10;
                p.p = {t.p.x - 3, t.p.y, t.z}; p.v = {300, 0, 0};
                b.shots.push_back(p); run(b, 0.1f);
                bool fish = false, bird = false; float birdVal = 0;
                for (const auto& h : b.hold) { if (h.name == "jack crevalle") fish = true; if (h.name == "brown pelican") { bird = true; birdVal = h.price * h.kg * h.killScore; } }
                check(b.thieves.empty() && fish && bird && birdVal > 12 * 1.25f, TextFormat("a round brings the pelican down: it and the fish drop on her deck (the bird an Airborne catch, %.1f sh)", birdVal));
            }
            Gannet f; Eco ef; setup(f, ef, 1, 35);   // a frigatebird harries a gull for its fish
            int gull = Species().Find("gull flock"), fr = Species().Find("frigatebird");
            CatchRec small; small.name = "grunt"; small.kg = 1.2f; small.price = 1.5f; small.dead = true; small.deckAt = {-2, 0.3f};
            f.hold = {small};
            int ga = ef.SpawnAgentPublic(gull, f.boat.pos); ef.agents[ga].count = 8;
            for (int i = 0; i < 60 * 5 && f.thieves.empty(); i++) { ef.agents[ga].p = {f.boat.pos.x, f.boat.pos.y, -3}; ef.agents[ga].alive = true; f.Step(dt); }
            int fa = ef.SpawnAgentPublic(fr, f.boat.pos);
            for (int i = 0; i < 60 * 2; i++) { ef.agents[fa].p = {f.boat.pos.x, f.boat.pos.y, -3}; ef.agents[fa].alive = true; f.Step(dt); }
            bool dropped = f.thieves.empty() && (!f.floaters.empty() || (!f.hold.empty() && f.hold[0].name == "grunt"));
            check(dropped, "a frigatebird harries the gull until it drops the fish in mid-air");
        }
        {   // the deck behaviours: a landed barracuda bites, a reef octopus grabs and drags toward the rail
            Gannet k; Eco ek; setup(k, ek, 1, 31);
            CatchRec bar; bar.name = "barracuda"; bar.kg = 6; bar.price = 2; bar.deckAt = {-2, 0.5f};
            k.hold = {bar}; k.crew[0].p = {-2, 0.9f};
            for (int i = 0; i < 60 * 12 && k.crew[0].injuries == 0; i++) { k.Move(0, {0, 0}, false, dt); k.Step(dt); if (!k.hold.empty()) k.hold[0].deckAt = {-2, 0.5f}; }
            check(k.crew[0].Has(INJ_BITE) && DeckBehaviourOf("barracuda", 6) == DB_BITER, "a landed barracuda bites the hand standing over it");
            Gannet o; Eco eo; setup(o, eo, 1, 32);
            CatchRec oc; oc.name = "reef octopus"; oc.kg = 4; oc.price = 3; oc.deckAt = {-2, 1.5f};
            o.hold = {oc}; o.crew[0].p = {-2, 1.9f};
            bool grabbed = false;
            for (int i = 0; i < 60 * 20 && !o.crew[0].overboard; i++) { o.Move(0, {0, 0}, false, dt); o.Step(dt); if (!o.hold.empty() && o.hold[0].grabbed == 0) grabbed = true; }
            check(grabbed && o.crew[0].overboard, "a landed reef octopus grabs a hand, inks, and drags them over the rail if nobody kills it");
            check(DeckBehaviourOf("spiny lobster", 2) == DB_PINCHER && DeckBehaviourOf("triggerfish", 1) == DB_STINGER && DeckBehaviourOf("grouper", 12) == DB_THRASHER && DeckBehaviourOf("snapper", 3) == DB_FLOPPER, "the Lagoon's species: lobsters pinch, triggerfish sting, groupers thrash, snapper flop");
        }
        Gannet g6; Eco e6; setup(g6, e6, 4, 20);   // (four hands: the Medic bot stands by the gutting table)
        g6.botsOn = true; g6.crew[0].p = {6, 0};
        g6.hold = {live};
        run(g6, 40);
        check(!g6.hold.empty() && (g6.hold[0].dead || g6.hold[0].gutted), "a bot at the gutting table clubs (or guts) the fish on deck before it gets away");
        // a careless jump at the rail is a swim
        Gannet g7; Eco e7; setup(g7, e7, 1, 21);
        g7.crew[0].p = {-2, 2.55f};
        bool hop = g7.Jump(0);
        for (int i = 0; i < 60 * 2 && !g7.crew[0].overboard; i++) { g7.Move(0, {0, 1}, false, dt); g7.Step(dt); }
        check(hop && g7.crew[0].overboard, "a jump at the rail, pushing outboard, puts the hand in the sea");
        Gannet g8; Eco e8; setup(g8, e8, 1, 22);
        g8.crew[0].p = {-2, 0};
        g8.Jump(0);
        for (int i = 0; i < 60 * 2; i++) { g8.Move(0, {0, 0}, false, dt); g8.Step(dt); }
        check(!g8.crew[0].overboard && g8.crew[0].z <= 0.001f, "a jump amidships comes down on the deck");
    }
    printf(fails ? "trawl-gear-test: %d check(s) failed\n" : "trawl-gear-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}

} // namespace tw
