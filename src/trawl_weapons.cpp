// The Trawl's weapons catalogue and the Gunsmith's rules (see trawl_weapons.h).
#include "trawl_weapons.h"
#include "trawl_eco.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

namespace tw {

namespace {
std::vector<std::vector<std::string>> ReadTsv(const std::string& path) {
    std::vector<std::vector<std::string>> rows;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> cols; std::stringstream ss(line); std::string c;
        while (std::getline(ss, c, '\t')) cols.push_back(c);
        rows.push_back(cols);
    }
    return rows;
}
int ClassOf(const std::string& s) { return s == "melee" ? WC_MELEE : s == "sidearm" ? WC_SIDEARM : s == "longgun" ? WC_LONGGUN : s == "special" ? WC_SPECIAL : WC_THROWN; }
int SpeedOf(const std::string& s) { return s == "fast" ? WS_FAST : s == "slow" ? WS_SLOW : s == "rapid" ? WS_RAPID : WS_NORMAL; }
}

const std::vector<WeaponDef>& Weapons() {
    static std::vector<WeaponDef> W = [] {
        std::vector<WeaponDef> out;
        for (const auto& r : ReadTsv(TrawlDataPath() + "/weapons.tsv")) {
            if (r.size() < 13) continue;
            WeaponDef w;
            w.id = r[0]; w.name = r[1]; w.cls = ClassOf(r[2]); w.dmg = (float)atof(r[3].c_str()); w.pellets = std::max(1, atoi(r[4].c_str()));
            w.speed = SpeedOf(r[5]); w.mag = atoi(r[6].c_str()); w.noise = atoi(r[7].c_str()); w.slots = atoi(r[8].c_str());
            w.where = r[9]; w.price = atoi(r[10].c_str()); w.ammo = r[11] == "-" ? "" : r[11]; w.ammoPrice = atoi(r[12].c_str());
            w.special = r.size() > 13 ? r[13] : "";
            out.push_back(w);
        }
        return out;
    }();
    return W;
}
const std::vector<AttachmentDef>& Attachments() {
    static std::vector<AttachmentDef> A = [] {
        std::vector<AttachmentDef> out;
        for (const auto& r : ReadTsv(TrawlDataPath() + "/attachments.tsv")) {
            if (r.size() < 6) continue;
            AttachmentDef a; a.id = r[0]; a.name = r[1]; a.fits = r[2]; a.effect = r[3]; a.where = r[4]; a.price = atoi(r[5].c_str());
            out.push_back(a);
        }
        return out;
    }();
    return A;
}
int WeaponIndex(const std::string& id) { const auto& W = Weapons(); for (int i = 0; i < (int)W.size(); i++) if (W[i].id == id) return i; return -1; }
int AttachmentIndex(const std::string& id) { const auto& A = Attachments(); for (int i = 0; i < (int)A.size(); i++) if (A[i].id == id) return i; return -1; }

bool AttachmentFits(const AttachmentDef& a, const WeaponDef& w) {
    const std::string& f = a.fits;
    bool cartridge = w.ammo == "rounds" || w.ammo == "shells";
    if (f == "any") return w.Gun();
    if (f == "magazine") return w.Gun() && w.mag >= 4 && w.id != "chatter";
    if (f == "chatter") return w.id == "chatter";
    if (f == "revolver") return w.id == "revolver" || w.id == "pepperbox";
    if (f == "rifles") return w.id == "rifle" || w.id == "carbine" || w.id == "airrifle" || w.id == "nitro";
    if (f == "shotguns") return w.ammo == "shells";
    if (f == "semiauto") return w.Gun() && w.mag >= 4 && w.speed != WS_SLOW;
    if (f == "long") return w.cls == WC_LONGGUN;
    if (f == "cartridge") return cartridge;
    if (f == "spear") return w.ammo == "spears" || w.ammo == "arrows";
    if (f == "scoped") return w.id == "rifle";
    if (f == "rapid") return w.id == "chatter" || w.id == "riveter";
    return false;
}
int UpgradePrice(const WeaponDef& w, int level) {
    static const int P[3] = {60, 150, 300};
    if (level < 0 || level > 2 || !w.Gun()) return 0;
    bool dear = w.id == "carbine" || w.id == "chatter" || w.id == "rifle";
    return P[level] * (dear ? 2 : 1);
}
bool HasAttachment(const int8_t att[3], const char* id) {
    for (int k = 0; k < 3; k++) if (att[k] >= 0 && att[k] < (int)Attachments().size() && Attachments()[att[k]].id == id) return true;
    return false;
}
float WeaponDamage(const WeaponDef& w, int lvl, const int8_t att[3]) {
    float d = w.dmg * (1 + 0.15f * std::clamp(lvl, 0, 3));
    if (HasAttachment(att, "rifled")) d *= 1.10f;
    if (HasAttachment(att, "baffle")) d *= 0.90f;
    return d;
}
int WeaponMagazine(const WeaponDef& w, const int8_t att[3]) {
    if (HasAttachment(att, "drum")) return 75;
    if (HasAttachment(att, "extmag")) return (int)(w.mag * 1.5f);
    return w.mag;
}
float WeaponCooldown(const WeaponDef& w, const int8_t att[3]) {
    float t;
    if (w.cls == WC_MELEE) t = w.speed == WS_FAST ? 0.3f : w.speed == WS_SLOW ? 0.9f : 0.5f;
    else t = w.speed == WS_RAPID ? (w.id == "chatter" ? 1.0f / 12 : 0.15f) : w.speed == WS_SLOW ? 1.4f : (w.cls == WC_LONGGUN ? 0.8f : 0.45f);
    float rate = 1;
    if (HasAttachment(att, "hairtrigger")) rate *= 1.2f;
    if (HasAttachment(att, "steamfeed")) rate *= 1.3f;
    return t / rate;
}
float WeaponReach(const WeaponDef& w) {
    if (w.id == "gaff") return 2.0f;
    if (w.id == "boathook" || w.id == "lance") return 3.0f;
    if (w.id == "flenser") return 2.5f;
    return 1.6f;
}
float WeaponSpread(const WeaponDef& w, const int8_t att[3]) {
    float s = w.pellets > 1 ? 6.0f : w.cls == WC_LONGGUN ? 1.2f : 2.5f;
    if (HasAttachment(att, "sight")) s *= 0.6f;
    if (HasAttachment(att, "choke")) s *= 0.6f;
    return s;
}
float WeaponNoise(const WeaponDef& w, const int8_t att[3]) { return std::max(0.0f, (float)w.noise - (HasAttachment(att, "baffle") ? 3.0f : 0.0f)); }
int AmmoPack(const std::string& kind) {
    if (kind == "shells") return 8;
    if (kind == "spears") return 5;
    if (kind == "flares") return 3;
    if (kind == "rivets") return 20;
    return 10;
}

} // namespace tw
