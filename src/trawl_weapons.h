#pragma once
// The Trawl's weapons catalogue and the Gunsmith (design doc v2, "Weapons": the catalogue, the Gunsmith's upgrades and
// attachments, carrying and ammunition). The numbers are data: data/trawl/weapons.tsv (46 weapons) and
// data/trawl/attachments.tsv (18 attachments), transcribed from the doc's tables. Headless.
//
// A catalogue weapon rides in a hand's slot as Item::Weapon with Slot::wpn its row; the older items (gaff, priest,
// knife, speargun, flare pistol, rifle, shotgun, depth charge) keep their own behaviour.
#include <cstdint>
#include <string>
#include <vector>

namespace tw {

enum WeaponClass { WC_MELEE, WC_SIDEARM, WC_LONGGUN, WC_SPECIAL, WC_THROWN };
enum WeaponSpeed { WS_FAST, WS_NORMAL, WS_SLOW, WS_RAPID };
struct WeaponDef {
    std::string id, name, where, ammo, special;
    int cls = WC_MELEE, speed = WS_NORMAL;
    float dmg = 0; int pellets = 1;
    int mag = 0, noise = 0, slots = 1, price = 0, ammoPrice = 0;
    bool Gun() const { return cls == WC_SIDEARM || cls == WC_LONGGUN; }
};
struct AttachmentDef { std::string id, name, fits, effect, where; int price = 0; };

const std::vector<WeaponDef>& Weapons();
const std::vector<AttachmentDef>& Attachments();
int WeaponIndex(const std::string& id);              // -1 if unknown
int AttachmentIndex(const std::string& id);
bool AttachmentFits(const AttachmentDef& a, const WeaponDef& w);
int UpgradePrice(const WeaponDef& w, int level);       // the next damage upgrade from `level` (0-2): 60/150/300, doubled for the carbine, chatter gun and long rifle; 0 past the third
bool HasAttachment(const int8_t att[3], const char* id);
// what a weapon does with its upgrades and attachments
float WeaponDamage(const WeaponDef& w, int lvl, const int8_t att[3]);      // per projectile (or per blow): +15% an upgrade; rifled barrel +10%, baffle -10%
int WeaponMagazine(const WeaponDef& w, const int8_t att[3]);               // extended +50%, drum 75
float WeaponCooldown(const WeaponDef& w, const int8_t att[3]);             // seconds between shots/blows (hair trigger +20% rate, steam feed +30%)
float WeaponReach(const WeaponDef& w);                                      // melee reach (m)
float WeaponSpread(const WeaponDef& w, const int8_t att[3]);               // degrees (sight -40%, choke tighter)
float WeaponNoise(const WeaponDef& w, const int8_t att[3]);                // (baffle -3)
int AmmoPack(const std::string& kind);                                      // rounds a pack at the Gunsmith (10 rounds, 8 shells, 5 spears, 3 flares...)

} // namespace tw
