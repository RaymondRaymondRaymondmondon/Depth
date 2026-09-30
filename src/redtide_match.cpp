// Red Tide: the match rules. See redtide_match.h. Sources: the design doc's "Core loop and rules" (tides, quota,
// scrip, health, downs and revives, doors and power, drops, Hunts), "Weapons" and "Weapon handling", "Davy's Locker",
// "The Pressure Forge", "Tonics", "Enemy factions" and the Sunken Ship's boss sheet; numbers from data/redtide.
#include "redtide_match.h"
#include "redtide_profile.h"
#include <cstdio>
#include "raymath.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>

namespace rt {

// ---------------------------------------------------------------- salvage builds and tacticals (design doc, "Items" and "Salvage builds")
const BuildDef& Build(BuildType b) {
    static const BuildDef D[(int)BuildType::COUNT] = {
        {"nothing", {"", "", ""}, ""},
        {"Shell Shield", {"Turtle shell", "Strap", "Brass rim"}, "A back-mounted shield that blocks bites (300); B bashes with it"},
        {"Turbine", {"Fan", "Dynamo", "Mount"}, "Powers the machines within 12 m for 90 s before the map's power is on"},
        {"Net Tripwire", {"Net", "Two stakes", "Bell"}, "Holds the first beast up to size 3 that crosses it"},
        {"Decoy Buoy", {"Buoy", "Lantern", "Chum tin"}, "Draws beasts and enemies to it for 45 s"},
        {"Bubble Wall", {"Compressor", "Hose", "Valve"}, "A curtain small beasts won't cross; 60 s"},
    };
    return D[std::clamp((int)b, 0, (int)BuildType::COUNT - 1)];
}
const char* TacticalName(int t) { static const char* N[TAC_COUNT] = {"limpets", "ink bombs", "chum bags", "flares"}; return N[std::clamp(t, 0, TAC_COUNT - 1)]; }
int Match::BenchPrice(int item) { static const int P[BENCH_ITEMS] = {750, 500, 500, 1000}; return P[std::clamp(item, 0, BENCH_ITEMS - 1)]; }
const char* Match::BenchName(int item) { static const char* N[BENCH_ITEMS] = {"an ink bomb", "a chum bag", "a flare", "a cleaning brush"}; return N[std::clamp(item, 0, BENCH_ITEMS - 1)]; }

// ---------------------------------------------------------------- small helpers
static std::string Lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
static bool HasW(const std::string& s, const char* w) { return Lower(s).find(w) != std::string::npos; }
// Every number in a string, in order ("Holds and rolls; teammates have 4 s" -> {4}).
static std::vector<float> Nums(const std::string& s) {
    std::vector<float> out;
    for (size_t i = 0; i < s.size(); i++) {
        if (isdigit((unsigned char)s[i]) || (s[i] == '.' && i + 1 < s.size() && isdigit((unsigned char)s[i + 1]))) {
            size_t j = i;
            while (j < s.size() && (isdigit((unsigned char)s[j]) || s[j] == '.')) j++;
            out.push_back((float)atof(s.substr(i, j - i).c_str()));
            i = j;
        }
    }
    return out;
}
// The number that follows a word ("bleed 3 s" -> 3), or def.
static float NumAfter(const std::string& text, const char* word, float def) {
    std::string l = Lower(text);
    size_t p = l.find(word);
    if (p == std::string::npos) return def;
    std::vector<float> n = Nums(l.substr(p));
    return n.empty() ? def : n[0];
}
static float SegPointDist(Vector3 a, Vector3 b, Vector3 p) {
    Vector3 ab = Vector3Subtract(b, a);
    float L = Vector3DotProduct(ab, ab);
    float t = L > 1e-8f ? std::clamp(Vector3DotProduct(Vector3Subtract(p, a), ab) / L, 0.0f, 1.0f) : 0;
    return Vector3Distance(Vector3Add(a, Vector3Scale(ab, t)), p);
}
// Closest distance between segments p1-q1 and p2-q2; tBody is the parameter on the second (a dart's path this frame
// against a body's capsule axis, tail 0 .. head 1).
static float SegSegDist(Vector3 p1, Vector3 q1, Vector3 p2, Vector3 q2, float* tBody) {
    Vector3 d1 = Vector3Subtract(q1, p1), d2 = Vector3Subtract(q2, p2), r = Vector3Subtract(p1, p2);
    float a = Vector3DotProduct(d1, d1), e = Vector3DotProduct(d2, d2), f = Vector3DotProduct(d2, r);
    float s = 0, t = 0;
    if (a <= 1e-8f && e <= 1e-8f) { if (tBody) *tBody = 0; return Vector3Length(r); }
    if (a <= 1e-8f) { t = std::clamp(f / e, 0.0f, 1.0f); }
    else {
        float c = Vector3DotProduct(d1, r);
        if (e <= 1e-8f) { s = std::clamp(-c / a, 0.0f, 1.0f); }
        else {
            float b = Vector3DotProduct(d1, d2), den = a * e - b * b;
            s = den > 1e-8f ? std::clamp((b * f - c * e) / den, 0.0f, 1.0f) : 0;
            t = (b * s + f) / e;
            if (t < 0) { t = 0; s = std::clamp(-c / a, 0.0f, 1.0f); }
            else if (t > 1) { t = 1; s = std::clamp((b - c) / a, 0.0f, 1.0f); }
        }
    }
    if (tBody) *tBody = t;
    return Vector3Distance(Vector3Add(p1, Vector3Scale(d1, s)), Vector3Add(p2, Vector3Scale(d2, t)));
}
static Vector3 Facing(const Agent& a) { return Vector3Length(a.vel) > 0.05f ? Vector3Normalize(a.vel) : Vector3{0, 0, 1}; }


// ---------------------------------------------------------------- weapons.json
int WeaponsData::Index(const std::string& id) const {
    for (size_t i = 0; i < weapons.size(); i++) if (weapons[i].id == id || weapons[i].name == id) return (int)i;
    return -1;
}
const TonicDef* WeaponsData::Tonic(const std::string& id) const {
    for (const auto& t : tonics) if (t.id == id) return &t;
    return nullptr;
}

const WeaponsData& Weapons() {
    static std::unique_ptr<WeaponsData> W;
    if (W) return *W;
    W = std::make_unique<WeaponsData>();
    Json j = LoadJsonFile(DataDir() + "/engine/weapons.json");
    std::map<std::string, WeaponClass> cls;
    for (const auto& kv : j["handling"].o) {
        WeaponClass c;
        const Json& h = kv.second;
        c.spreadHip = h["spread_hip"].F(2); c.spreadAds = h["spread_ads"].F(1); c.adsS = h["ads_s"].F(0.2);
        c.swimAim = h["swim_aim"].F(0.9); c.headshot = h["headshot"].F(2); c.fullTo = h["full_to"].F(15);
        c.halfAt = h["half_at"].F(25); c.speed = h["speed"].F(28); c.recoil = h["recoil"].F(1);
        cls[kv.first] = c;
    }
    for (const Json& w : j["weapons"].a) {
        WeaponDef d;
        d.id = w["id"].Str0(); d.name = w["name"].Str0(); d.cls = w["class"].Str0(); d.source = w["source"].Str0();
        d.forged = w["forged"].Str0(); d.forgedTwist = w["forged_twist"].Str0();
        d.damage = w["damage"].F(30); d.rpm = w["rpm"].F(300); d.noise = w["noise"].F(2); d.reload = w["reload"].F(1.4);
        d.chum = w["chum"].F(0); d.arc = w["arc_m"].F(0); d.splash = w["splash"].F(0); d.reach = w["reach"].F(0); d.spinup = w["spinup"].F(0); d.cone = w["cone_m"].F(0); d.coneDeg = w["cone_deg"].F(30); d.polyp = w["polyp"].Bool0(); d.wave = w["wave"].Bool0(); d.lure = w["lure"].Bool0();
        d.pellets = w["pellets"].I(1); d.mag = w["mag"].I(8); d.reserve = w["reserve"].I(32); d.price = w["price"].I(0);
        d.burst = w["burst"].I(0); d.chain = w["chain"].I(0); d.explodesOver = w["explodes_over_size"].I(0);
        d.perRound = w["per_round"].Bool0(); d.pins = w["pins"].Bool0(); d.net = w["net"].Bool0(); d.melee = d.cls == "melee";
        if (cls.count(d.cls)) d.handling = cls[d.cls];
        W->weapons.push_back(d);
    }
    for (const Json& t : j["tonics"].a) {
        TonicDef d;
        d.id = t["id"].Str0(); d.name = t["name"].Str0(); d.effect = t["effect"].Str0(); d.price = t["price"].I(2000); d.priceSolo = t["price_solo"].I(0);
        W->tonics.push_back(d);
    }
    for (const auto& kv : j["rack_for"].o) W->rackFor[kv.first] = kv.second.Str0();
    for (const auto& kv : j["tonic_poi"].o) W->tonicPoi[kv.first] = kv.second.Str0();
    const Json& f = j["forge"];
    W->forgePrice = f["price"].F(5000); W->forgeDmg = f["damage_mult"].F(2.5); W->forgeAmmo = f["ammo_mult"].F(1.5);
    W->reroll = f["reroll"].F(2500); W->rearm = f["rearm"].F(4500);
    for (const Json& a : f["ammo"].a) W->forgeAmmoTypes.push_back(a.Str0());
    const Json& l = j["locker"];
    W->lockerPull = l["pull"].I(950); W->fireSalePull = l["fire_sale"].I(10);
    if (l["moves_after"].Size() == 2) { W->lockerMoveMin = l["moves_after"][0].I(8); W->lockerMoveMax = l["moves_after"][1].I(12); }
    W->wonder = l["wonder"].Str0();
    for (const Json& m : j["melee"].a) if (m["id"].Str0() == "knife") { W->knifeDamage = m["damage"].F(100); W->knifeReach = m["reach"].F(1.5); W->knifeRate = m["rate"].F(1.2); }
    const Json& s = j["spawn_in"];
    W->sidearm = s["sidearm"].Str0("cormorant"); W->startScrip = s["scrip"].I(500); W->startLimpets = s["limpets"].I(2);
    return *W;
}

const char* DropName(DropType d) {
    static const char* n[] = {"Resupply", "Blood Frenzy", "Double Scrip", "Purge", "Shipwright", "Fire Sale", "Harpoon Hour"};
    return (int)d < (int)DropType::COUNT ? n[(int)d] : "?";
}

// ---------------------------------------------------------------- the level
// Rooms are the blockout's zones at their deck heights; passages are boxes around each link's two mouths (2.4 m wide,
// 2.6 m high), reaching a little into both rooms so a diver can swim from one into the other.
void BuildLevel(const MapData& m, Level& L) {
    L = Level{};
    const WeaponsData& W = Weapons();
    for (int i = 0; i < (int)m.zones.size(); i++) {
        const Zone& z = m.zones[i];
        Volume v;
        Rectangle pr = z.plan;
        for (const auto& p : z.parts) if (!p.hidden) { pr = p.r; break; }   // (a zone of parts: its first box here, the rest after the windows)
        v.lo = {pr.x, z.y0, pr.y};
        v.hi = {pr.x + pr.width, z.y1, pr.y + pr.height};
        v.zone = i;
        v.diverOk = z.diverOk;
        L.vols.push_back(v);
    }
    for (int li = 0; li < (int)m.links.size(); li++) {
        const Link& k = m.links[li];
        Vector3 lo = Vector3Min(k.a, k.b), hi = Vector3Max(k.a, k.b);
        Volume v;
        v.lo = Vector3Subtract(lo, {1.2f, 1.3f, 1.2f});
        v.hi = Vector3Add(hi, {1.2f, 1.3f, 1.2f});
        v.link = li;
        v.diverOk = k.diverOk;
        if (k.slip) { v.lo = v.hi = k.a; v.diverOk = false; }   // a slipstream is a ride, not a passage to swim
        L.vols.push_back(v);
        Door d;
        d.link = li;
        d.cost = k.diverOk ? k.cost : 0;
        d.open = d.cost <= 0 && k.opensWith < 0;
        d.pos = Vector3Lerp(k.a, k.b, 0.5f);
        d.name = k.passage;
        L.doors.push_back(d);
    }
    for (int wi = 0; wi < (int)m.windows.size(); wi++) {
        Volume v; v.lo = m.windows[wi].lo; v.hi = m.windows[wi].hi; v.window = wi; v.diverOk = false;
        L.vols.push_back(v);
    }
    // the other boxes of the zones built from parts (vols[zone] stays each zone's first box; vols[zones + link] its link)
    for (int i = 0; i < (int)m.zones.size(); i++) {
        const Zone& z = m.zones[i];
        bool first = true;
        for (const auto& p : z.parts) {
            if (!p.hidden && first) { first = false; continue; }
            Volume v; v.lo = {p.r.x, z.y0, p.r.y}; v.hi = {p.r.x + p.r.width, z.y1, p.r.y + p.r.height};
            v.zone = i; v.diverOk = z.diverOk; v.hidden = p.hidden;
            L.vols.push_back(v);
        }
    }
    int lockerSpot = 0;
    for (const Poi& p : m.pois) {
        if (p.zone < 0) continue;
        Station s;
        s.name = p.name;
        s.zone = p.zone;
        const Zone& z = m.zones[p.zone];
        s.pos = z.Clamp({p.pos.x, z.y0 + 1.2f, p.pos.z}, 0.6f);
        std::string t = Lower(p.type);
        if (t == "rack") {
            s.type = StationType::Rack;
            auto it = W.rackFor.find(p.name);
            s.weapon = W.Index(it != W.rackFor.end() ? it->second : p.name.substr(0, p.name.find(" rack")));
        } else if (t == "tonic") {
            s.type = StationType::Tonic;
            auto it = W.tonicPoi.find(p.name);
            s.tonic = it != W.tonicPoi.end() ? it->second : "";
            if (s.tonic.empty()) for (const auto& tn : W.tonics) if (p.name.compare(0, tn.name.size(), tn.name) == 0) s.tonic = tn.id;   // "Quick Brine (L2)", "Widow's Ink"
            // "the first two are always Juggernaut and Quick Brine"; the rest need power
            s.needsPower = s.tonic != "juggernaut" && s.tonic != "quick";
        } else if (t == "locker") { s.type = StationType::Locker; s.lockerSpot = lockerSpot++; s.needsPower = s.lockerSpot > 0; }
        else if (t == "forge") { s.type = StationType::Forge; s.needsPower = true; }
        else if (t == "power") s.type = StationType::Power;
        else if (t == "build") s.type = StationType::Workbench;
        else if (t == "trap") { s.type = StationType::Trap; s.needsPower = !(HasW(p.name, "beacon") && !HasW(p.name, "control")); }   // (a lure beacon is turned by hand)
        else if (t == "quest" || t == "lantern") s.type = StationType::Quest;
        else if (t == "hazard") s.type = StationType::Hazard;
        else if (t == "entry") s.type = StationType::Entry;
        else if (t == "boss") s.type = StationType::Boss;
        else if (t == "queststep") { s.type = StationType::QuestStep; s.step = p.step; }
        else s.type = HasW(p.name, "cleaning") ? StationType::Cleaning : StationType::Feature;
        L.stations.push_back(s);
    }
    // the map's dressing: what each zone's notes describe, placed from a fixed seed (the renderer draws these)
    const Json& dr = m.extra["dressing"];
    for (int zi = 0; zi < (int)m.zones.size() && dr.IsObj(); zi++) {
        const Zone& z = m.zones[zi];
        const Json& d = dr[z.name];
        if (!d.IsObj() || z.radial) continue;
        uint32_t r = 1234567u + zi * 7919u;
        auto rnd = [&]() { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return (r & 0xFFFF) / 65535.0f; };
        // keep solids clear of the doorways, the stations and the start
        std::vector<Vector3> keep;
        for (const auto& l : m.links) { if (l.from == zi) keep.push_back(l.a); if (l.to == zi) keep.push_back(l.b); }
        for (const auto& st : L.stations) if (st.zone == zi) keep.push_back(st.pos);
        auto clear = [&](Vector3 c, Vector3 h) {
            for (const auto& k : keep) {
                float dx = std::max(0.0f, fabsf(k.x - c.x) - h.x), dz = std::max(0.0f, fabsf(k.z - c.z) - h.z);
                if (dx * dx + dz * dz < 3.0f * 3.0f) return false;
            }
            return true;
        };
        auto spot = [&](float pad) {
            Rectangle b = z.plan;
            if (!z.parts.empty()) {   // a zone of parts: one of its boxes, by area
                float tot = 0; for (const auto& p : z.parts) if (!p.hidden) tot += p.r.width * p.r.height;
                float u = rnd() * tot;
                for (const auto& p : z.parts) if (!p.hidden) { b = p.r; u -= p.r.width * p.r.height; if (u <= 0) break; }
                pad = std::min(pad, std::min(b.width, b.height) * 0.4f);
            }
            return Vector3{b.x + pad + (b.width - 2 * pad) * rnd(), 0, b.y + pad + (b.height - 2 * pad) * rnd()};
        };
        float h = z.y1 - z.y0;
        auto add = [&](PropKind k, Vector3 pos, Vector3 half, bool solid) {
            Prop p; p.kind = k; p.pos = pos; p.half = half; p.zone = zi; p.solid = solid; p.seed = r;
            if (solid && !clear(pos, half)) return;
            L.props.push_back(p);
        };
        for (int k = 0; k < d["columns"].I(0); k++) { Vector3 c = spot(2.5f); float w = 0.5f + rnd() * 0.7f; add(PropKind::Column, {c.x, (z.y0 + z.y1) / 2, c.z}, {w, h / 2, w * (0.8f + rnd() * 0.4f)}, true); }
        for (int k = 0; k < d["stalactites"].I(0); k++) {
            Vector3 c = spot(1.0f); float len = 0.8f + rnd() * std::min(3.0f, h * 0.25f), rad = 0.25f + rnd() * 0.35f;
            add(PropKind::Stalactite, {c.x, z.y1, c.z}, {rad, len, rad}, false);
            if (rnd() < 0.5f && !z.air) add(PropKind::Stalagmite, {c.x + 0.4f, z.y0, c.z}, {0.3f, len * 0.6f, 0.3f}, false);
        }
        for (int k = 0; k < d["crystals"].I(0); k++) { Vector3 c = spot(2.0f); add(PropKind::Crystal, {c.x, z.y0, c.z}, {1, 2.5f, 1}, false); }
        for (int k = 0; k < d["roots"].I(0); k++) { Vector3 c = spot(1.5f); add(PropKind::Root, {c.x, z.y1, c.z}, {0.6f, 1.6f, 0.6f}, false); }
        for (int k = 0; k < d["ledges"].I(0); k++) {
            float y = z.y0 + h * (k + 1) / (d["ledges"].I(0) + 1);
            bool west = k % 2 == 0;
            add(PropKind::Ledge, {west ? z.plan.x + 1.2f : z.plan.x + z.plan.width - 1.2f, y, z.plan.y + z.plan.height * (0.3f + 0.4f * rnd())}, {1.2f, 0.3f, 2.5f}, false);
        }
        if (d["pool"].F(0) > 0) { float f = d["pool"].F(0); Vector3 c = z.Center(); add(PropKind::Pool, {c.x, z.y0 + 0.02f, c.z}, {z.plan.width * f / 2, 0.02f, z.plan.height * f / 2}, false); }
        if (d["silt"].Bool0()) for (int k = 0; k < 12; k++) { Vector3 c = spot(1.0f); add(PropKind::Silt, {c.x, z.y0 + 0.1f, c.z}, {1.5f + rnd() * 2, 0.1f + rnd() * 0.2f, 1.5f + rnd() * 2}, false); }
        if (d["machine"].Bool0()) { Vector3 c = spot(3.0f); add(PropKind::Machine, {c.x, z.y0 + 1.5f, c.z}, {1.8f, 1.5f, 1.2f}, false); }
        // the Reef: coral walls that make corridors (8-12 m fire lanes), table and brain coral, staghorn, seagrass,
        // mangrove roots, a coral head
        for (int k = 0; k < d["coral_walls"].I(0); k++) {
            bool alongX = k % 2 == 0;
            Vector3 c = spot(3.0f);
            float len = (alongX ? z.plan.width : z.plan.height) * (0.25f + 0.25f * rnd());
            float wh = std::min(h, 3.0f + rnd() * (h - 3.0f));
            add(PropKind::CoralWall, {c.x, z.y0 + wh / 2, c.z}, alongX ? Vector3{len / 2, wh / 2, 0.6f} : Vector3{0.6f, wh / 2, len / 2}, true);
        }
        for (int k = 0; k < d["brain"].I(0); k++) { Vector3 c = spot(2.5f); float rad = 0.8f + rnd() * 1.2f; add(PropKind::Brain, {c.x, z.y0 + rad * 0.6f, c.z}, {rad, rad * 0.6f, rad}, true); }
        for (int k = 0; k < d["tables"].I(0); k++) { Vector3 c = spot(2.5f); float rad = 1.2f + rnd() * 1.5f; add(PropKind::Table, {c.x, z.y0 + 1.5f + rnd() * std::min(3.0f, h * 0.4f), c.z}, {rad, 0.15f, rad}, false); }
        for (int k = 0; k < d["staghorn"].I(0); k++) { Vector3 c = spot(1.0f); add(PropKind::Staghorn, {c.x, z.y0, c.z}, {0.8f, 1.2f + rnd(), 0.8f}, false); }
        for (int k = 0; k < d["seagrass"].I(0); k++) { Vector3 c = spot(0.5f); add(PropKind::Seagrass, {c.x, z.y0, c.z}, {0.6f, 0.5f + rnd() * 0.4f, 0.6f}, false); }
        for (int k = 0; k < d["mangrove"].I(0); k++) { Vector3 c = spot(1.0f); add(PropKind::Mangrove, {c.x, z.y1, c.z}, {1.0f, h, 1.0f}, false); }
        // Atlantis: buildings on a street grid (4 m streets, the radial avenue at x = 0 kept clear), farm terraces,
        // forum sea fans, amphorae in the lower town's streets, the rampart's crenels
        if (d["buildings"].I(0) > 0) {
            float g = d["grid"].F(10);
            std::vector<Vector3> cells;
            for (float x = z.plan.x + g * 0.5f; x <= z.plan.x + z.plan.width - g * 0.5f + 0.01f; x += g)
                for (float zz = z.plan.y + g * 0.5f; zz <= z.plan.y + z.plan.height - g * 0.5f + 0.01f; zz += g) cells.push_back({x, 0, zz});
            for (int k = (int)cells.size() - 1; k > 0; k--) std::swap(cells[k], cells[(int)(rnd() * (k + 1)) % (k + 1)]);
            int placed = 0;
            for (const Vector3& c : cells) {
                if (placed >= d["buildings"].I(0)) break;
                float hx = (g - 4) * 0.5f * (0.75f + 0.25f * rnd()), hz = (g - 4) * 0.5f * (0.75f + 0.25f * rnd());
                if (fabsf(c.x) < hx + 3.0f) continue;                       // the avenue up the hill
                float bh = std::min(h * 0.6f, 4.0f + rnd() * 6.0f);
                size_t n0 = L.props.size();
                add(PropKind::Building, {c.x, z.y0 + bh / 2, c.z}, {hx, bh / 2, hz}, true);
                if (L.props.size() > n0) placed++;
            }
        }
        for (int k = 0; k < d["terraces"].I(0); k++) { Vector3 c = spot(3.0f); add(PropKind::Terrace, {c.x, z.y0 + 0.4f, c.z}, {3.0f + rnd() * 2, 0.4f, 1.5f + rnd()}, false); }
        for (int k = 0; k < d["fans"].I(0); k++) { Vector3 c = spot(2.0f); add(PropKind::Fan, {c.x, z.y0 + 1.2f, c.z}, {1.2f, 1.2f, 0.1f}, false); }
        for (int k = 0; k < d["amphorae"].I(0); k++) { Vector3 c = spot(1.5f); add(PropKind::Amphora, {c.x, z.y0 + 0.5f, c.z}, {0.35f, 0.5f, 0.35f}, false); }
        for (int k = 0; k < d["tanks"].I(0); k++) { Vector3 c = spot(2.0f); add(PropKind::Tank, {c.x, z.y0 + 1.4f, c.z}, {0.9f, 1.4f, 0.9f}, false); }
        if (d["stakes"].Bool0()) {
            // the bone stakes: the Sand Worm's line, a ring round the hatch
            const Json& wm = m.extra["worm"];
            Vector3 c{};
            for (const auto& p : m.pois) if (p.name == wm["center_poi"].Str0()) c = p.pos;
            float rad = wm["radius_m"].F(350);
            for (int k = 0; k < 220; k++) {
                float a = k * 6.2832f / 220;
                Vector3 p{c.x + cosf(a) * rad, z.y0 + 1.5f, c.z + sinf(a) * rad};
                if (!z.Contains(p, -0.5f)) continue;
                Prop pr; pr.kind = PropKind::Stake; pr.pos = p; pr.half = {0.15f, 1.5f, 0.15f}; pr.zone = zi; pr.seed = r + k; L.props.push_back(pr);
            }
        }
        if (d["crenels"].Bool0() && z.parts.empty()) for (float zz = z.plan.y + 2; zz < z.plan.y + z.plan.height - 1; zz += 3) add(PropKind::Crenel, {z.plan.x + 0.5f, z.y0 + 1.0f, zz}, {0.5f, 1.0f, 0.7f}, false);
        if (d["crenels"].Bool0() && !z.parts.empty()) {
            // a ring of parts: crenels along every open face that looks outward (away from the ring's middle)
            Vector3 mid{z.plan.x + z.plan.width / 2, 0, z.plan.y + z.plan.height / 2};
            for (const auto& p : z.parts) {
                if (p.hidden) continue;
                for (int side = 0; side < 4; side++) {
                    bool alongX = side < 2;   // faces at z = lo/hi run along x
                    float at = side == 0 ? p.r.y : side == 1 ? p.r.y + p.r.height : side == 2 ? p.r.x : p.r.x + p.r.width;
                    float nx = side == 2 ? -1.0f : side == 3 ? 1.0f : 0, nz = side == 0 ? -1.0f : side == 1 ? 1.0f : 0;
                    float len = alongX ? p.r.width : p.r.height, s0 = alongX ? p.r.x : p.r.y;
                    for (float u = s0 + 1.0f; u < s0 + len - 0.5f; u += 3) {
                        Vector3 c = alongX ? Vector3{u, 0, at} : Vector3{at, 0, u};
                        if ((c.x - mid.x) * nx + (c.z - mid.z) * nz <= 0) continue;   // an inner face
                        if (z.Contains({c.x + nx * 0.3f, (z.y0 + z.y1) / 2, c.z + nz * 0.3f})) continue;   // the zone carries on past it
                        add(PropKind::Crenel, {c.x - nx * 0.5f, z.y0 + 1.0f, c.z - nz * 0.5f}, alongX ? Vector3{0.7f, 1.0f, 0.5f} : Vector3{0.5f, 1.0f, 0.7f}, false);
                    }
                }
            }
        }
        if (d["mound"].F(0) > 0) { Vector3 c = z.Center(); float f = d["mound"].F(0); add(PropKind::Mound, {c.x, z.y0 + h * 0.3f, c.z}, {z.plan.width * f / 2, h * 0.3f, z.plan.height * f / 2}, true); }
    }
    // drain grates (Atlantis): drawn over their cistern mouths
    for (const Poi& p : m.pois) if (p.zone >= 0 && Lower(p.type) == "grate") {
        const Zone& z = m.zones[p.zone];
        Prop g; g.kind = PropKind::Grate; g.pos = z.Clamp({p.pos.x, z.y0 + 0.03f, p.pos.z}, 0.5f); g.pos.y = z.y0 + 0.03f; g.half = {0.9f, 0.03f, 0.9f}; g.zone = p.zone;
        L.props.push_back(g);
    }
    // a boss key's cache (the Cave's Lantern Cache: the expedition's key opens its crate)
    const Json& bk = m.extra["boss_key"];
    if (bk.IsObj()) for (const Poi& p : m.pois) if (p.name == bk["poi"].Str0() && p.zone >= 0) {
        Station s; s.type = StationType::Cache; s.name = p.name; s.zone = p.zone;
        const Zone& z = m.zones[p.zone];
        s.pos = z.Clamp({p.pos.x, z.y0 + 1.2f, p.pos.z}, 0.6f);
        L.stations.push_back(s);
    }
    // the start pocket: the first zone (the blockout lists it first), at mid-height by its door-free side
    L.startZone = 0;
    while (L.startZone + 1 < (int)m.zones.size() && !m.zones[L.startZone].diverOk) L.startZone++;
    const Zone& z0 = m.zones[L.startZone];
    L.start = z0.Clamp({z0.Center().x, z0.y0 + 2, z0.Center().z}, 1);
    for (const Poi& p : m.pois) if (p.name == m.extra["start_poi"].Str0() && p.zone == L.startZone) L.start = z0.Clamp({p.pos.x, z0.y0 + 2, p.pos.z}, 1);   // (the Void: the hatch, not the middle of the plain)
    L.BuildGrid();
}
void Level::BuildGrid() {
    gVols.clear(); gProps.clear(); gnx = gnz = 0;
    if (vols.empty()) return;
    float x0 = 1e9f, z0 = 1e9f, x1 = -1e9f, z1 = -1e9f;
    for (const auto& v : vols) { x0 = std::min(x0, v.lo.x); z0 = std::min(z0, v.lo.z); x1 = std::max(x1, v.hi.x); z1 = std::max(z1, v.hi.z); }
    gCell = std::max(8.0f, std::max(x1 - x0, z1 - z0) / 256);   // (the Void's rim is 800 m across)
    gx0 = x0 - 1; gz0 = z0 - 1;
    gnx = (int)ceilf((x1 - gx0 + 1) / gCell) + 1; gnz = (int)ceilf((z1 - gz0 + 1) / gCell) + 1;
    gVols.assign((size_t)gnx * gnz, {}); gProps.assign((size_t)gnx * gnz, {});
    auto put = [&](std::vector<std::vector<int>>& g, int idx, float lx, float lz, float hx, float hz) {
        int a0 = std::clamp((int)floorf((lx - gx0) / gCell), 0, gnx - 1), a1 = std::clamp((int)floorf((hx - gx0) / gCell), 0, gnx - 1);
        int b0 = std::clamp((int)floorf((lz - gz0) / gCell), 0, gnz - 1), b1 = std::clamp((int)floorf((hz - gz0) / gCell), 0, gnz - 1);
        for (int b = b0; b <= b1; b++) for (int a = a0; a <= a1; a++) g[(size_t)b * gnx + a].push_back(idx);
    };
    for (int i = 0; i < (int)vols.size(); i++) put(gVols, i, vols[i].lo.x, vols[i].lo.z, vols[i].hi.x, vols[i].hi.z);
    for (int i = 0; i < (int)props.size(); i++) if (props[i].solid) put(gProps, i, props[i].pos.x - props[i].half.x - 1, props[i].pos.z - props[i].half.z - 1, props[i].pos.x + props[i].half.x + 1, props[i].pos.z + props[i].half.z + 1);
}

bool Level::Inside(Vector3 p, float r, const std::vector<char>& linkOpen, bool darts) const {
    if (gnx > 0) {
        int a = (int)floorf((p.x - gx0) / gCell), b = (int)floorf((p.z - gz0) / gCell);
        if (a < 0 || b < 0 || a >= gnx || b >= gnz) return false;
        size_t c = (size_t)b * gnx + a;
        for (int i : gProps[c]) {
            const Prop& pr = props[i];
            if (fabsf(p.x - pr.pos.x) < pr.half.x + r && fabsf(p.y - pr.pos.y) < pr.half.y + r && fabsf(p.z - pr.pos.z) < pr.half.z + r) return false;
        }
        for (int i : gVols[c]) {
            const Volume& v = vols[i];
            if (!darts && !v.diverOk) continue;
            if (v.link >= 0 && (v.link >= (int)linkOpen.size() || !linkOpen[v.link])) continue;
            if (p.x >= v.lo.x + r && p.x <= v.hi.x - r && p.y >= v.lo.y + r && p.y <= v.hi.y - r && p.z >= v.lo.z + r && p.z <= v.hi.z - r) return true;
        }
        return false;
    }
    for (const auto& pr : props) {
        if (!pr.solid) continue;
        if (fabsf(p.x - pr.pos.x) < pr.half.x + r && fabsf(p.y - pr.pos.y) < pr.half.y + r && fabsf(p.z - pr.pos.z) < pr.half.z + r) return false;
    }
    for (const auto& v : vols) {
        if (!darts && !v.diverOk) continue;
        if (v.link >= 0 && (v.link >= (int)linkOpen.size() || !linkOpen[v.link])) continue;
        if (p.x >= v.lo.x + r && p.x <= v.hi.x - r && p.y >= v.lo.y + r && p.y <= v.hi.y - r && p.z >= v.lo.z + r && p.z <= v.hi.z - r) return true;
    }
    return false;
}

Vector3 Level::Move(Vector3 from, Vector3 to, float r, const std::vector<char>& linkOpen) const {
    if (Inside(to, r, linkOpen)) return to;
    Vector3 p = from;
    Vector3 t{to.x, p.y, p.z}; if (Inside(t, r, linkOpen)) p = t;
    t = {p.x, to.y, p.z}; if (Inside(t, r, linkOpen)) p = t;
    t = {p.x, p.y, to.z}; if (Inside(t, r, linkOpen)) p = t;
    if (!Inside(p, r, linkOpen)) {
        // pushed out (a knockback into a wall, a door that closed): back to the nearest room
        float best = 1e9f; Vector3 q = p;
        for (const auto& v : vols) {
            if (v.link >= 0 || v.window >= 0 || !v.diverOk) continue;
            Vector3 c{std::clamp(p.x, v.lo.x + r + 0.01f, v.hi.x - r - 0.01f), std::clamp(p.y, v.lo.y + r + 0.01f, v.hi.y - r - 0.01f), std::clamp(p.z, v.lo.z + r + 0.01f, v.hi.z - r - 0.01f)};
            float d = Vector3Distance(c, p);
            if (d < best) { best = d; q = c; }
        }
        p = q;
    }
    return p;
}

bool Level::Sight(Vector3 a, Vector3 b, const std::vector<char>& linkOpen, bool darts) const {
    float d = Vector3Distance(a, b);
    int n = std::max(1, (int)(d / 0.4f));
    for (int i = 1; i < n; i++) if (!Inside(Vector3Lerp(a, b, (float)i / n), 0.02f, linkOpen, darts)) return false;
    return true;
}

// ---------------------------------------------------------------- the match
float Match::Rand() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / (float)0x1000000; }
// ---------------------------------------------------------------- quips (stage 9)
const char* Match::VoiceName(int voice) { static const char* N[4] = {"Diver", "Whaler", "Stowaway", "Mechanic"}; return N[std::clamp(voice, 0, 3)]; }
// diver >= 0: that diver says it; -1: anyone standing; <= -2: anyone but diver (-2 - diver), or them if alone
bool Match::Quip(const std::string& sit, int diver, float delay, bool answer) {
    if (!map || divers.empty()) return false;
    if (!answer) {
        static const std::map<std::string, float> CD = {{"Quiet", 120}, {"Scent high", 45}, {"Predator pulse", 60}, {"Enemy eaten", 40}, {"Harmless big thing shot", 40}, {"Cleaner killed", 40}, {"Chummed", 20}, {"Tonic bought", 30}};
        auto it = quipSitAt.find(sit);
        float cd = CD.count(sit) ? CD.at(sit) : 15.0f;
        if (it != quipSitAt.end() && time - it->second < cd) return false;
    }
    if (delay > 0 || quipBusyT > 0) {
        bool urgent = sit == "Downed" || sit == "Boss appears" || sit == "Swallowed" || sit == "Match over" || sit == "Last standing";
        if (quipQueue.size() >= 3 && !urgent) return false;
        quipQueue.push_back({sit, diver, std::max(delay, 0.0f), answer});
        quipSitAt[sit] = time;
        return true;
    }
    // who speaks
    std::vector<int> can;
    int not_ = diver <= -2 ? -2 - diver : -1;
    for (int i = 0; i < (int)divers.size(); i++) if (!divers[i].dead && (sit == "Downed" || sit == "Match over" || !divers[i].downed) && i != not_) can.push_back(i);
    int who = diver >= 0 ? diver : !can.empty() ? can[(int)(Rand() * can.size()) % can.size()] : not_;
    if (who < 0 || who >= (int)divers.size()) return false;
    const char* vn = VoiceName(VoiceOf(who));
    std::vector<std::string> lines;
    for (const Json& b : Engine().barks.a) if (b["situation"].Str0() == sit && b["diver"].Str0() == vn) {
        std::string l = b["line"].Str0();
        auto at = quipLineAt.find(l);
        if (at == quipLineAt.end() || time - at->second >= 180) lines.push_back(l);   // no line again within 3 minutes
    }
    if (lines.empty()) return false;
    const std::string line = lines[(int)(Rand() * lines.size()) % lines.size()];
    quipLineAt[line] = time; quipSitAt[sit] = time;
    int syl = std::max(1, (int)line.size() / 3);
    quipBusyT = 0.5f + syl * 0.13f;
    Say(vn, line, std::max(2.5f, quipBusyT + 1.2f));
    quipOut.push_back({VoiceOf(who), who, syl, divers[who].pos});
    if (quipOut.size() > 16) quipOut.erase(quipOut.begin());
    quipDone++;
    // a teammate answers now and then (the call and its callback)
    if (!answer && players > 1 && sit != "Match over" && Rand() < 0.3f) {
        std::vector<int> other;
        for (int i = 0; i < (int)divers.size(); i++) if (i != who && !divers[i].dead && !divers[i].downed) other.push_back(i);
        if (!other.empty()) quipQueue.push_back({sit, other[(int)(Rand() * other.size()) % other.size()], quipBusyT + 0.4f, true});
    }
    return true;
}
void Match::UpdateQuips(float dt) {
    if (quipBusyT > 0) quipBusyT -= dt;
    for (auto& q : quipQueue) q.delay -= dt;
    for (size_t i = 0; i < quipQueue.size(); i++) if (quipQueue[i].delay <= 0 && quipBusyT <= 0) {
        QuipPend q = quipQueue[i];
        quipQueue.erase(quipQueue.begin() + i);
        Quip(q.sit, q.diver, 0, true);
        break;
    }
    quipQueue.erase(std::remove_if(quipQueue.begin(), quipQueue.end(), [](const QuipPend& q) { return q.delay < -5; }), quipQueue.end());
    // what changed since the last tick
    if (!quipStarted && time > 1.5f) { quipStarted = true; Quip("Match start"); }
    if (phase != quipPhase) {
        if (phase == TidePhase::Hunt) Quip(predatorHunt ? "Predator pulse" : "Hunt begins");
        else if (phase == TidePhase::Calm && quipPhase != TidePhase::Calm) Quip("Tide cleared");
        quipPhase = phase;
    }
    if (bossActive && !quipBoss) Quip("Boss appears");
    quipBoss = bossActive;
    quipDown.resize(divers.size(), 0); quipHeldBoss.resize(divers.size(), 0); quipScentT.resize(divers.size(), 0);
    for (int i = 0; i < (int)divers.size(); i++) {
        const DiverState& d = divers[i];
        bool down = d.downed && !d.dead;
        if (down && !quipDown[i]) Quip("Downed", i);
        quipDown[i] = down;
        bool held = d.holder >= 0 && IsBoss(d.holder);
        if (held && !quipHeldBoss[i]) Quip("Swallowed", i);
        quipHeldBoss[i] = held;
        if (!d.dead && !d.downed && (quipScentT[i] += dt) >= 1.0f) {
            quipScentT[i] = 0;
            if (eco.Smell(d.pos, eco.ZoneAt(d.pos), 8) >= 60) Quip("Scent high", i);
        }
    }
    bool last = players > 1 && Living() == 1;
    if (last && !quipLast) { for (int i = 0; i < (int)divers.size(); i++) if (!divers[i].dead && !divers[i].downed) Quip("Last standing", i); }
    quipLast = last;
    quipQuietT = phase == TidePhase::Calm && phaseT > 10 ? quipQuietT + dt : 0;
    if (quipQuietT > 8 && quipBusyT <= 0 && quipQueue.empty()) { auto it = quipSitAt.begin(); float lastAny = -1e9f; for (; it != quipSitAt.end(); ++it) lastAny = std::max(lastAny, it->second); if (time - lastAny > 40) Quip("Quiet"); }
}

void Match::Say(const std::string& who, const std::string& text, float t) {
    captions.push_back({who, text, t});
    if (captions.size() > 6) captions.erase(captions.begin());
}
const WeaponDef& Match::W(const Held& h) const { return Weapons().weapons[std::clamp(h.def, 0, (int)Weapons().weapons.size() - 1)]; }
Held& Match::Cur(DiverState& d) { return d.downed ? d.downHeld : d.weapons[std::clamp(d.cur, 0, (int)d.weapons.size() - 1)]; }
const Held& Match::Cur(const DiverState& d) const { return d.downed ? d.downHeld : d.weapons[std::clamp(d.cur, 0, (int)d.weapons.size() - 1)]; }
Vector3 Match::Forward(const DiverState& d) const { return {sinf(d.yaw) * cosf(d.pitch), sinf(d.pitch), cosf(d.yaw) * cosf(d.pitch)}; }
int Match::Living() const { int n = 0; for (const auto& d : divers) if (!d.dead && !d.downed) n++; return n; }
int Match::QuotaFor(int t) const {
    static const float scale[] = {0.4f, 0.6f, 0.8f, 1.0f};   // "Quota scales 0.4 / 0.6 / 0.8 / 1.0 with 1-4 players"
    return std::max(1, (int)lroundf(map->Tide(t).quota4p * scale[std::clamp(players, 1, 4) - 1]));
}
int Match::DiverOfAgent(int agent) const {
    if (agent < 0 || agent >= (int)eco.agents.size()) return -1;
    int s = eco.agents[agent].diver;
    return s >= 0 && s < (int)divers.size() ? s : -1;
}
static float MagMax(const WeaponDef& w, const Held& h) { return w.mag * (h.forged ? Weapons().forgeAmmo : 1.0f); }
static float ResMax(const WeaponDef& w, const Held& h) { return w.reserve * (h.forged ? Weapons().forgeAmmo : 1.0f); }
static Held NewHeld(int def, bool forged) {
    const WeaponDef& w = Weapons().weapons[def];
    Held h; h.def = def; h.forged = forged;
    h.mag = (int)MagMax(w, h); h.reserve = (int)ResMax(w, h);
    return h;
}

void Match::Init(const std::string& key, int playerCount, uint32_t seed, bool bots) { InitMap(Map(key), key, playerCount, seed, bots); }

void Match::InitMap(const MapData& m, const std::string& art, int playerCount, uint32_t seed, bool bots) {
    std::string style = botStyle;
    *this = Match{};
    botStyle = style;
    map = &m; mapKey = m.key; artKey = art;
    players = std::clamp(playerCount, 1, 4);
    rng = seed ? seed * 2654435761u + 1 : 99;
    BuildLevel(m, level);
    linkOpen.assign(m.links.size(), 0);
    for (size_t i = 0; i < level.doors.size(); i++) linkOpen[level.doors[i].link] = level.doors[i].open ? 1 : 0;
    eco.Init(m, seed ? seed : 20260930, 1, players);
    eco.onDeath = [this](int a, int k) { OnDeath(a, k); };
    eco.onDiverHit = [this](int d, int a, float dmg) { OnDiverHit(d, a, dmg); };
    eco.decideHook = [this](Agent& a, int i) { return DecideHook(a, i); };
    eco.linkClosed.assign(m.links.size(), 0);
    attacksBySp.assign(m.species.size(), {});
    // bodies for hits and drawing: the art workbook's lengths, capped to half the home room's smaller floor side
    // (its Goliath is 23.4 m against a 14 m engine room; the boss sheet calls it "the size of a boiler")
    bodies.clear(); bodyScale.clear();
    for (const auto& sp : m.species) {
        Body b = BodyOf(art, sp.art.empty() ? sp.name : sp.art);
        float scale = sp.artScale;
        int hz = m.ZoneIndex(sp.homeZone);
        if (hz >= 0 && !m.zones[hz].radial) {
            float cap = 0.5f * std::min(m.zones[hz].plan.width, m.zones[hz].plan.height);
            if (b.length > cap && cap > 0.5f) scale = cap / b.length;
        }
        b.length *= scale; b.radius *= scale;
        bodies.push_back(b); bodyScale.push_back(scale);
    }
    for (int i = 0; i < (int)m.attacks.size(); i++) {
        int sp = m.SpeciesIndex(m.attacks[i].beast);
        if (sp >= 0) attacksBySp[sp].push_back(i);
    }
    // the blockout's lairs: the crocodile lives in the salon's air pocket, the boss at its home
    for (const Poi& p : m.pois) {
        if (p.zone < 0) continue;
        bool pocket = HasW(p.name, "air pocket"), boss = Lower(p.type) == "boss";
        if (!pocket && !boss) continue;
        const Zone& z = m.zones[p.zone];
        Vector3 at = z.Clamp({p.pos.x, pocket ? z.y1 - 1.5f : z.y0 + 2.5f, p.pos.z}, 1.2f);
        for (auto& a : eco.agents) {
            const Species& sp = m.species[a.sp];
            if (!a.alive || a.diver >= 0 || a.homeZone != p.zone) continue;
            if ((pocket && sp.Has("amphibious") && sp.size >= 4) || (boss && sp.tier == 5)) { a.home = a.pos = a.goal = at; }
        }
    }
    noFireZone.assign(m.zones.size(), 0); rockZone.assign(m.zones.size(), 0); crawlZone.assign(m.zones.size(), 1.0f);
    for (const Json& zn : m.extra["no_fire_zones"].a) { int zi = m.ZoneIndex(zn.Str0()); if (zi >= 0) noFireZone[zi] = 1; }
    voidZone.assign(m.zones.size(), 0);
    for (const Json& zn : m.extra["void_zones"].a) { int zi = m.ZoneIndex(zn.Str0()); if (zi >= 0) voidZone[zi] = 1; }
    for (const Json& zn : m.extra["rockfall"]["zones"].a) { int zi = m.ZoneIndex(zn.Str0()); if (zi >= 0) rockZone[zi] = 1; }
    for (const auto& kv : m.extra["crawl_zones"].o) { int zi = m.ZoneIndex(kv.first); if (zi >= 0) crawlZone[zi] = kv.second.F(0.4); }
    for (const auto& sp : m.species) if (sp.tier == 5) bossKind = HasW(sp.name, "goliath") ? 0 : HasW(sp.name, "lobster") ? 1 : HasW(sp.name, "matriarch") ? 2 : HasW(sp.name, "wyrm") ? 3 : HasW(sp.name, "leviathan") ? 4 : 9;
    for (int i = 0; i < (int)m.species.size(); i++) if (!m.species[i].attacksAs.empty()) { int bi = m.SpeciesIndex(m.species[i].attacksAs); if (bi >= 0) attacksBySp[i] = attacksBySp[bi]; }
    for (const auto& s : level.stations) if (s.type == StationType::Locker) lockerSpots = std::max(lockerSpots, s.lockerSpot + 1);
    const WeaponsData& W = Weapons();
    lockerMoveAt = W.lockerMoveMin + (int)(Rand() * (W.lockerMoveMax - W.lockerMoveMin + 1));
    int side = W.Index(W.sidearm);
    for (int i = 0; i < players; i++) {
        DiverState d;
        d.slot = i;
        d.bot = bots || i > 0;
        d.pos = level.Move(level.start, Vector3Add(level.start, {(i % 2) * 1.2f - 0.6f, 0, (i / 2) * 1.2f - 0.6f}), 0.4f, linkOpen);
        d.zone = level.startZone;
        d.yaw = 1.5708f;
        d.hpMax = d.hp = Engine().C("player_hp", 100);
        d.weapons = {NewHeld(side, false)};
        d.downHeld = NewHeld(side, false);
        d.scrip = W.startScrip;
        d.limpets = W.startLimpets;
        d.agent = eco.AddDiver(i, d.pos);
        divers.push_back(d);
    }
    PlaceSalvage();
    BeginTide(1);
}

// ---------------------------------------------------------------- tides
void Match::BeginTide(int t) {
    tide = t;
    maxTide = std::max(maxTide, t);
    eco.SetTide(t);
    quota = QuotaFor(t);
    tideKills = 0;
    phaseT = 0;
    predatorHunt = false;
    if (t >= 10 && timeToTide10 < 0) {
        timeToTide10 = time;
        int s = 0; for (const auto& d : divers) s += d.scripEarned;
        scripAt10 = s / std::max(1, (int)divers.size());
    }
    const TideRow& tr = map->Tide(t);
    if (HasW(tr.hunt, "faction") || HasW(tr.hunt, "predator")) {
        huntsSeen++;
        bool apexAround = false;
        for (const auto& a : eco.agents) if (a.alive && a.diver < 0 && map->species[a.sp].tier == 4) apexAround = true;
        if (HasW(tr.hunt, "predator") && apexAround && (Rand() < 0.5f || map->faction.units.empty())) {
            // a Predator Hunt: the apex beasts come to the divers because the water is bloody enough
            predatorHunt = true;
            huntApexLeft = std::max(1, (int)lroundf(4 * QuotaFor(t) / (float)std::max(1, map->Tide(t).quota4p)));
            huntApexLeft = std::clamp(huntApexLeft, 1, 4);
            for (auto& a : eco.agents) if (a.alive && a.diver < 0 && map->species[a.sp].tier == 4) { a.hunger = 1; a.fedT = 0; }
            Say("Tide " + std::to_string(t), "PREDATOR HUNT: the water is red enough. The apex beasts are coming.", 6);
        } else if (!map->faction.units.empty()) {
            static const float scale[] = {0.4f, 0.6f, 0.8f, 1.0f};
            int count = std::max(3, (int)lroundf(10 * scale[players - 1])) + 1;   // "10 plus a Foreman"
            int region = 0;
            for (const auto& p : map->pois) if (p.name == map->faction.entryPoi && p.zone >= 0) region = map->zones[p.zone].alarmRegion;
            size_t before = eco.squads.size();
            eco.SpawnSquad(region, true, count, true);
            if (eco.squads.size() > before) {
                auto& b = map->faction.barks;
                auto it = b.find("foreman");
                Say(map->faction.name, it != b.end() && !it->second.empty() ? it->second[0] : "They're here.", 6);
            }
            Say("Tide " + std::to_string(t), "HUNT: " + map->faction.name + " come through the breach in force.", 6);
        }
        phase = TidePhase::Hunt;
        return;
    }
    phase = TidePhase::Tide;
    Say("Tide " + std::to_string(t), "Kill quota " + std::to_string(quota) + (t >= 4 && t < 10 ? ". Predators answer blood now." : t >= 10 ? ". Apex beasts wander." : "."), 4);
}

void Match::EndTide() {
    const TideRow& tr = map->Tide(tide);
    int living = 0;
    for (const auto& d : divers) if (!d.dead) living++;
    for (auto& d : divers) if (!d.dead) Pay(d, (float)tr.tideBonus / std::max(1, living) / (doubleScripT > 0 ? 2.0f : 1.0f));
    if (phase == TidePhase::Hunt) {
        // "The reward for either is a Resupply and a guaranteed Locker weapon."
        ApplyDrop(DropType::Resupply, level.start);
        for (auto& d : divers) if (!d.dead && !d.downed) GiveLockerWeapon(d);
    }
    // the dead come back for the next tide with the spawn-in kit (and keep their scrip)
    for (auto& d : divers) if (d.dead) {
        d.dead = false; d.downed = false;
        d.hpMax = d.hp = Engine().C("player_hp", 100);
        d.tonics.clear(); d.slots = 2;
        d.weapons = {NewHeld(Weapons().Index(Weapons().sidearm), false)}; d.cur = 0;
        d.pos = level.start; d.vel = {0, 0, 0};
        d.limpets = Weapons().startLimpets;
        if (d.agent >= 0) { eco.agents[d.agent].alive = true; eco.agents[d.agent].downed = false; eco.agents[d.agent].pos = d.pos; }
    }
    Say("Tide " + std::to_string(tide), "cleared. The water calms for " + std::to_string((int)map->tunables.count("calm_seconds") ? (int)map->tunables.at("calm_seconds") : 20) + " s.", 4);
    phase = TidePhase::Calm;
    phaseT = 0;
}

// ---------------------------------------------------------------- the step
void Match::Step(float dt) {
    if (over) return;
    time += dt;
    phaseT += dt;
    for (auto* t : {&fireSaleT, &doubleScripT, &frenzyT, &trapT, &lockerMovedT}) if (*t > 0) *t -= dt;
    if (whistleT > 0) { whistleT -= dt; if (whistleT <= 0 && questStep == 2) whistlePulls = 0; }   // three blasts, close together
    if (harpoonT > 0) {
        harpoonT -= dt;
        if (harpoonT <= 0) for (auto& d : divers) if (d.harpoonHour) { d.weapons = d.savedWeapons; d.cur = d.savedCur; d.harpoonHour = false; }
    }
    eco.bloodMult = frenzyT > 0 ? 5.0f : floodT > 0 ? 2.0f : 1.0f;   // Blood Frenzy: the water fills with blood at 5x (the Wyrm's Flood: 2x)
    // beasts: doors until bought; some openings never (a porthole-hatch); a breach from its tide, or once the
    // faction has come through it
    if (!breachOpen) for (const auto& l : map->links) if (l.beastRule == 2 && (tide >= l.openTide || eco.squadsSpawned > 0)) {
        breachOpen = true;
        if (l.openTide > 1 || eco.squadsSpawned > 0) Say("", map->extra["breach_text"].Str0("Something tears the hull breach wide: the sharks can get in now"), 5);
    }
    for (size_t i = 0; i < linkOpen.size(); i++) {
        const Link& l = map->links[i];
        eco.linkClosed[i] = l.beastRule == 1 ? 1 : l.beastRule == 2 ? (breachOpen ? 0 : 1) : (linkOpen[i] ? 0 : 1);
    }
    for (auto& d : divers) if (d.bot) Bot(d, dt);
    for (auto& d : divers) UpdateDiver(d, dt);
    UpdateDarts(dt);
    if (phase == TidePhase::Calm) for (auto& a : eco.alarm) a *= powf(0.5f, dt);   // the calm: no squads answer
    eco.Step(dt);
    BeastsVsDivers(dt);
    EnemiesVsDivers(dt);
    FloraHazards(dt);
    UpdateBoss(dt);
    UpdateReef(dt);
    UpdateAtlantis(dt);
    UpdateVoid(dt);
    UpdateDossier(dt);
    UpdateCharms(dt);
    UpdateQuests(dt);
    UpdateDrops(dt);
    UpdateSalvage(dt);
    UpdateQuips(dt);
    for (auto& c : crates) {
        c.t -= dt;
        if (!c.fallen && c.t <= 0) {
            c.fallen = true;
            fx.push_back({7, c.pos, {0, -1, 0}});
            if (c.kind == 0) {
                for (auto& d : divers) if (!d.dead && !d.downed && Vector3Distance(d.pos, c.pos) < 1.6f) HitDiver(d, 60, "loose cargo", "knockback 2 m", c.pos);
                // "Loose cargo can be shot to drop on it (200 damage)"
                if (bossAgent >= 0 && eco.agents[bossAgent].alive && Vector3Distance(eco.agents[bossAgent].pos, c.pos) < 3.0f) HitAgent(nullptr, bossAgent, 200, false, false, {0, -1, 0}, nullptr);
            } else {
                // a stalactite: everything under it, beast, enemy or diver
                auto under = [&](Vector3 p) { return Vector2Distance({p.x, p.z}, {c.pos.x, c.pos.z}) < c.radius; };
                for (auto& d : divers) if (!d.dead && !d.downed && under(d.pos)) HitDiver(d, c.dmg, "a stalactite", "knockback 1 m", c.pos);
                DiverState* owner = c.owner >= 0 && c.owner < (int)divers.size() ? &divers[c.owner] : nullptr;
                for (int i = 0; i < (int)eco.agents.size(); i++) {
                    const Agent& a = eco.agents[i];
                    if (a.alive && a.diver < 0 && under(a.pos) && eco.ZoneAt(a.pos) == eco.ZoneAt(c.pos)) HitAgent(owner, i, c.dmg, false, false, {0, -1, 0}, nullptr);
                }
            }
        }
    }
    for (auto it = floraBleed.begin(); it != floraBleed.end();) {
        it->second -= dt;
        if (it->first < (int)eco.flora.size()) eco.AddBlood(eco.flora[it->first].pos, 1.5f * dt);
        if (it->second <= 0) it = floraBleed.erase(it); else ++it;
    }
    crates.erase(std::remove_if(crates.begin(), crates.end(), [](const Crate& c) { return c.t < -2; }), crates.end());
    for (auto& c : captions) c.t -= dt;
    captions.erase(std::remove_if(captions.begin(), captions.end(), [](const Caption& c) { return c.t <= 0; }), captions.end());
    // the tide's end
    float calm = map->tunables.count("calm_seconds") ? (float)map->tunables.at("calm_seconds") : 20.0f;
    if (phase == TidePhase::Calm && phaseT >= calm) BeginTide(tide + 1);
    else if (phase == TidePhase::Tide && tideKills >= quota) EndTide();
    else if (phase == TidePhase::Hunt) {
        bool done;
        if (predatorHunt) done = huntApexLeft <= 0;
        else {
            int left = 0;
            for (const auto& s : eco.squads) if (s.hunt && s.alive) left += (int)s.members.size();
            done = left == 0;
        }
        if (done || phaseT > 360) EndTide();                 // a force that fled the ship and never came back ends it too
    }
    // "If every diver is down, the match ends" (a solo diver with a Quick Brine self-revive in hand isn't down yet)
    bool anyUp = false;
    for (const auto& d : divers) if (!d.dead && (!d.downed || d.selfReviveT > 0)) anyUp = true;
    if (!anyUp) {
        over = true; phase = TidePhase::Over; overReason = "Every diver is down";
        quipQueue.clear(); quipBusyT = 0; Quip("Match over", (int)(Rand() * divers.size()) % std::max(1, (int)divers.size()));
    }
}

// ---------------------------------------------------------------- divers
void Match::Pay(DiverState& d, float amount) {
    int a = (int)lroundf(amount * (doubleScripT > 0 ? 2.0f : 1.0f));
    d.scrip += a;
    d.scripEarned += a;
}

void Match::SteerDiver(int di, Vector3 wish, float vert, bool sprint, bool ads, float dt) {
    DiverState& d = divers[di];
    if (d.dead) return;
    const EngineData& e = Engine();
    d.ads = ads && !d.downed;
    bool kick = d.tonics.count("kick") > 0;
    wish.y = 0;
    bool moving = Vector3Length(wish) > 0.05f;
    if (d.stunT > 0 || d.heldT > 0 || d.slipLink >= 0) { wish = {0, 0, 0}; vert = 0; moving = false; }
    bool air = d.zone >= 0 && map->zones[d.zone].air;
    bool sprinting = sprint && moving && d.stamina > 0 && !d.downed && !d.ads;
    float speed = sprinting ? e.M("sprint_speed", 3.4f) : e.M("swim_speed", 2);
    if (kick) speed *= e.M("kick_brine_swim_mult", 1.25f);
    if (d.ads) speed *= e.M("ads_swim_mult", 0.6f);
    if (air) speed = std::min(speed, e.M("walk_speed_air", 1.6f)) * (sprinting ? 1.4f : 1.0f);   // "divers walk there, slowly"
    if (d.zone >= 0 && d.zone < (int)crawlZone.size()) speed *= crawlZone[d.zone];
    if (d.downed) speed = e.M("downed_crawl_speed", 0.6f);
    if (d.slowT > 0) speed *= d.slowMult;
    if (d.drumUses > 0 && map->extra["quest_altar"].IsObj()) speed *= map->extra["quest_altar"]["carry_speed"].F(0.8f);   // (the drum's weight)
    Vector3 want = moving ? Vector3Scale(Vector3Normalize(wish), speed) : Vector3{0, 0, 0};
    want.y = std::clamp(vert, -1.0f, 1.0f) * e.M("vertical_speed", 1.4f) * (d.downed ? 0.4f : 1.0f);
    if (air) want.y = -4.0f;                                    // on foot in an air chamber: gravity, no swimming up
    if (sprinting) d.stamina = std::max(0.0f, d.stamina - dt / (e.M("sprint_stamina_s", 6) * (kick ? e.M("kick_brine_stamina_mult", 1.5f) : 1.0f)));
    else d.stamina = std::min(1.0f, d.stamina + dt / e.M("stamina_regen_s", 8));
    float acc = Vector3Length(want) > Vector3Length(d.vel) ? e.M("accel_s", 0.4f) : e.M("decel_s", 0.5f);
    if (d.driftT > 0) acc *= 4;                                 // "arrives with 2 s of drift"
    if (d.slipLink >= 0) return;
    d.vel = Vector3Lerp(d.vel, want, std::min(1.0f, dt / std::max(0.05f, acc)));
}

void Match::Reload(int di) {
    DiverState& d = divers[di];
    if (d.dead) return;
    Held& h = Cur(d);
    const WeaponDef& w = W(h);
    if (w.melee || d.reloading || h.mag >= (int)MagMax(w, h) || h.reserve <= 0 || d.harpoonHour) return;
    d.reloading = true;
    d.reloadT = 0;
    d.burstLeft = 0;
    if (d.tonics.count("shocking")) {
        // Shocking Salt: reloading releases a shock that stuns beasts within 4 m
        for (auto& a : eco.agents) if (a.alive && a.diver < 0 && !map->species[a.sp].isDiver && Vector3Distance(a.pos, d.pos) < 4) a.stun = std::max(a.stun, 1.5f);
        fx.push_back({6, d.pos, {0, 0, 0}});
    }
}

void Match::SwapWeapon(int di, int slot) {
    DiverState& d = divers[di];
    if (d.downed || d.dead || slot < 0 || slot >= (int)d.weapons.size() || slot == d.cur) return;
    d.cur = slot;
    d.reloading = false; d.burstLeft = 0; d.spin = 0;
    d.fireT = std::max(d.fireT, 0.4f);
}

void Match::UpdateDiver(DiverState& d, float dt) {
    const EngineData& e = Engine();
    for (float* t : {&d.fireT, &d.meleeT, &d.hitMarker, &d.hurtT, &d.lastKillT, &d.stunT, &d.aimSway, &d.flinchT}) if (*t > 0) *t -= dt;
    if (d.slowT > 0) { d.slowT -= dt; if (d.slowT <= 0) d.slowMult = 1; }
    d.recoil = std::max(0.0f, d.recoil - dt * 6);
    Agent* body = d.agent >= 0 ? &eco.agents[d.agent] : nullptr;
    if (d.dead) { if (body) body->alive = false; return; }
    // move (the velocity was set by SteerDiver; knockbacks add to it)
    if (d.driftT > 0) d.driftT -= dt;
    // slipstreams: a mouth catches a diver who swims into it and carries them to the far hall at the stream's speed
    if (d.slipLink >= 0) {
        const Link& l = map->links[d.slipLink];
        float len = std::max(1.0f, Vector3Distance(l.a, l.b));
        d.slipT += l.slipSpeed * dt / len;
        Vector3 dir = Vector3Normalize(Vector3Subtract(l.b, l.a));
        d.pos = Vector3Lerp(l.a, l.b, std::min(1.0f, d.slipT));
        d.vel = Vector3Scale(dir, l.slipSpeed);
        if (d.slipT >= 1) {
            d.slipLink = -1; d.driftT = 2;
            d.pos = map->zones[l.to].Clamp(l.b, 1.0f); d.zone = l.to;
            d.vel = Vector3Scale(dir, 3.0f);
        }
    } else if (!d.downed && d.heldT <= 0) {
        for (int li = 0; li < (int)map->links.size(); li++) {
            const Link& l = map->links[li];
            if (!l.slip || l.from != d.zone || Vector3Distance(d.pos, l.a) > 1.4f) continue;
            d.slipLink = li; d.slipT = 0; d.reloading = false; d.burstLeft = 0;
            break;
        }
    }
    if (d.heldT <= 0 && d.slipLink < 0) {
        Vector3 v = d.vel;
        // a one-way passage (the slide down the hull, the dumbwaiter drop) runs with a current no diver can swim up
        if (eco.ZoneAt(d.pos) < 0) for (const auto& vol : level.vols) {
            if (vol.link < 0 || !map->links[vol.link].oneWay || !linkOpen[vol.link]) continue;
            if (d.pos.x < vol.lo.x || d.pos.x > vol.hi.x || d.pos.y < vol.lo.y || d.pos.y > vol.hi.y || d.pos.z < vol.lo.z || d.pos.z > vol.hi.z) continue;
            const Link& l = map->links[vol.link];
            v = Vector3Add(v, Vector3Scale(Vector3Normalize(Vector3Subtract(l.b, l.a)), 3.5f));
            break;
        }
        const Json& zc = d.zone >= 0 ? map->extra["zone_currents"][map->zones[d.zone].name] : Json::NullValue();
        if (zc.IsObj() && eco.ZoneAt(d.pos) == d.zone) {
            // the Reef's surge channel: a current toward its exit mouth
            int to = map->ZoneIndex(zc["to"].Str0());
            for (const auto& l : map->links) if ((l.from == d.zone && l.to == to) || (l.to == d.zone && l.from == to)) {
                Vector3 c = map->zones[d.zone].Center();
                Vector3 exitP = Vector3Distance(l.a, c) > Vector3Distance(l.b, c) ? l.a : l.b;   // the end out in the next zone
                Vector3 dir = Vector3Subtract(exitP, d.pos); dir.y *= 0.3f;
                if (Vector3Length(dir) > 0.5f) v = Vector3Add(v, Vector3Scale(Vector3Normalize(dir), zc["speed"].F(5)));
                break;
            }
        }
        d.pos = level.Move(d.pos, Vector3Add(d.pos, Vector3Scale(v, dt)), 0.4f, linkOpen);
    }
    int z = eco.ZoneAt(d.pos);
    if (z >= 0) d.zone = z;
    if (body) { body->pos = d.pos; body->vel = d.vel; body->zone = d.zone; body->downed = d.downed; body->alive = true; }
    if (d.downed) {
        d.downT -= dt;
        eco.AddBlood(d.pos, e.C("blood_diver_downed", 8) * dt);   // a downed diver is a strong blood source
        if (d.reviveTouchT > 0) d.reviveTouchT -= dt; else d.reviveT = 0;
        if (d.selfReviveT > 0) { d.selfReviveT -= dt; if (d.selfReviveT <= 0) { d.selfRevives--; Revive(d, -1); return; } }
        if (d.downT <= 0) {
            d.downed = false; d.dead = true;
            if (body) body->alive = false;
            DropKeys(d, d.pos);
            Say("", "Diver " + std::to_string(d.slot + 1) + " bled out (back next tide)", 4);
        }
        if (!d.reloading) return;
    }
    // held by a beast: pinned to its mouth; teammates break the hold with damage, or the diver struggles free
    if (d.heldT > 0 && !d.downed && d.holder <= -2) {
        // held by Reacher coral: until it's shot to 0, a teammate cuts the diver free, or they hack themselves loose
        int pi = -2 - d.holder;
        const Json& rule = pi < (int)eco.flora.size() ? map->extra["flora_rules"][map->flora[eco.flora[pi].flora].name] : Json::NullValue();
        if (pi < (int)eco.flora.size()) {
            Vector3 at = Vector3Add(eco.flora[pi].pos, {0, 0.7f, 0});
            if (level.Inside(at, 0.3f, linkOpen)) d.pos = Vector3Lerp(d.pos, at, std::min(1.0f, dt * 5));
            d.hp -= rule["dps"].F(20) * map->Tide(tide).dmgMult * dt; d.regenT = 0; d.lastHitBy = "Reacher coral";
        }
        d.vel = {0, 0, 0};
        if (d.cutT > 0) d.cutT -= dt * 0.5f;
        if (pi >= (int)eco.flora.size() || reacherHP[pi] <= 0 || d.struggle >= 16 || d.cutT >= 1.5f) {
            if (pi < (int)eco.flora.size() && reacherHP[pi] <= 0) { eco.flora[pi].units = 0; eco.flora[pi].regrowT = 0; reacherHP.erase(pi); }
            d.heldT = 0; d.holder = -1; d.struggle = 0; d.cutT = 0;
            Say("", "Cut free of the Reacher coral", 2);
        }
        if (d.hp <= 0) { DownDiver(d, "Reacher coral"); return; }
    } else if (d.heldT > 0 && !d.downed) {
        d.heldT -= dt;
        bool release = false;
        if (d.holder < 0 || d.holder >= (int)eco.agents.size() || !eco.agents[d.holder].alive) release = true;
        else {
            Agent& h = eco.agents[d.holder];
            Vector3 mouth = Vector3Add(h.pos, Vector3Scale(Facing(h), bodies[h.sp].length * 0.45f));
            Vector3 np = Vector3Lerp(d.pos, mouth, std::min(1.0f, dt * 6));
            if (level.Inside(np, 0.3f, linkOpen)) d.pos = np;
            float breakAt = e.M("grab_break_teammate_dmg", 0.15f) * h.hpMax;
            int taps = (int)e.M("grab_break_taps", 6);
            if (d.tonics.count("kick")) taps = (taps + 1) / 2 + 1;   // Kick Brine: breaks free of grabs 50% faster
            d.holdDmg = d.holdHp0 - h.hp;                 // whatever hurt it: teammates' darts, a limpet, another beast
            if (d.holdLethal) taps *= 2;                  // a death roll takes twice the struggle (teammates are faster)
            if (d.holdDmg >= breakAt || d.struggle >= taps) {
                release = true;
                h.stun = 1; h.held = 0; h.st = State::Flee; h.target = d.agent; h.stateT = 0;
                Say("", map->species[h.sp].name + " lets go", 2);
            }
        }
        if (release || d.heldT <= 0) {
            bool lethal = d.holdLethal && !release;
            if (d.holder >= 0 && d.holder < (int)eco.agents.size()) eco.agents[d.holder].held = 0;
            std::string by = d.holder >= 0 && d.holder < (int)eco.agents.size() ? map->species[eco.agents[d.holder].sp].name : "a hold";
            float pending = d.holdPending;
            d.heldT = 0; d.holder = -1; d.holdDmg = 0; d.struggle = 0; d.holdLethal = false; d.holdPending = 0;
            if (lethal) HitDiver(d, pending, by, "", d.pos);   // teammates didn't shoot it off in time: the roll lands
            if (d.downed || d.dead) return;
        }
    }
    // reloading: slow-fast-slow; gas pistols and the scatter gun thumb in one round at a time
    Held& h = Cur(d);
    const WeaponDef& w = W(h);
    if (d.reloading) {
        float t = w.reload * (d.tonics.count("speed") ? 0.7f : 1.0f);
        int mm = (int)MagMax(w, h);
        d.reloadT += dt;
        if (w.perRound) {
            float per = t / std::max(1, mm);
            while (d.reloadT > per * 0.8f && h.mag < mm && h.reserve > 0) { h.mag++; h.reserve--; d.reloadT -= per; }
            if (h.mag >= mm || h.reserve <= 0) d.reloading = false;
        } else if (d.reloadT >= t) {
            int n = std::min(mm - h.mag, h.reserve);
            h.mag += n; h.reserve -= n;
            d.reloading = false;
        }
    }
    if (d.downed) return;
    // a burst's remaining rounds
    if (d.burstLeft > 0 && d.fireT <= 0 && h.mag > 0 && !d.reloading) {
        FireRound(d);
        d.burstLeft--;
        if (d.burstLeft == 0) d.fireT += 0.25f;
    }
    // health: regenerates 4 s after the last hit at 20 HP/s; bleeding and poison hold it off
    d.regenT += dt;
    if (d.bleedT > 0) { d.bleedT -= dt; d.hp -= 4 * dt; eco.AddBlood(d.pos, 2 * dt); d.regenT = 0; }
    if (d.poisonT > 0) { d.poisonT -= dt; d.hp -= d.poisonDps * dt; d.regenT = 0; }
    // parasites: a remora slows, a lamprey bleeds its host (the Ecosystem adds its blood)
    for (auto& a : eco.agents) {
        if (!a.alive || a.host != d.agent || d.agent < 0) continue;
        const Species& s = map->species[a.sp];
        if (HasW(s.name, "remora")) { d.slowT = std::max(d.slowT, 0.3f); d.slowMult = std::min(d.slowMult, 0.85f); }
        else { d.hp -= 2 * dt; d.regenT = 0; }
    }
    if (d.regenT > e.C("regen_delay_s", 4)) d.hp = std::min(d.hpMax, d.hp + e.C("regen_rate", 20) * dt);
    if (d.invulnerable) d.hp = std::max(d.hp, 1.0f);
    if (d.hp < d.hpMax * 0.5f) eco.AddBlood(d.pos, e.C("blood_diver_wounded", 2) * dt);
    if (d.hp <= 0) DownDiver(d, d.lastHitBy.empty() ? "wounds" : d.lastHitBy);
}

void Match::DownDiver(DiverState& d, const std::string& by) {
    if (d.downed || d.dead) return;
    d.downed = true;
    d.hp = 0;
    d.downT = Engine().C("down_bleedout_s", 45);
    d.reviveT = 0;
    d.heldT = 0; d.holder = -1; d.holdDmg = 0; d.holdLethal = false;
    d.bleedT = d.poisonT = 0;
    d.reloading = false; d.burstLeft = 0;
    d.downs++;
    d.downHeld = NewHeld(Weapons().Index(Weapons().sidearm), false);
    downsBy[by]++;
    bool hazard = false, enemy = false;
    for (const auto& f : map->flora) if (f.name == by) hazard = true;
    if (by == "loose cargo") hazard = true;
    for (const auto& u : map->faction.units) if (u.unit == by) enemy = true;
    if (hazard) deathsByHazard++; else if (enemy) deathsByEnemy++; else deathsByBeast++;
    if (bossInhaleDiver == d.slot) { bossInhaleT = -1; bossInhaleDiver = -1; }
    if (d.agent >= 0) eco.agents[d.agent].downed = true;
    // solo: one self-revive per Quick Brine bought (three per match)
    if (players == 1 && d.selfRevives > 0) d.selfReviveT = 3;
    Say("", "Diver " + std::to_string(d.slot + 1) + " is down (" + by + ")" + (d.selfReviveT > 0 ? ": the Quick Brine kicks in" : ""), 4);
}

void Match::Revive(DiverState& d, int by) {
    d.downed = false;
    d.reviveT = 0;
    d.selfReviveT = 0;
    // "A revived diver loses all tonics" (unless a Keep Your Brines was spent)
    if (d.keepBrines) { d.keepBrines = false; Say("", "Keep Your Brines: the tonics stay", 3); }
    else d.tonics.clear();
    d.hpMax = Engine().C("player_hp", 100);
    d.hp = d.hpMax;
    if (d.slots > 2) { d.slots = 2; if (d.weapons.size() > 2) d.weapons.resize(2); d.cur = std::min(d.cur, 1); }
    d.regenT = 0;
    if (d.agent >= 0) eco.agents[d.agent].downed = false;
    if (by >= 0) { divers[by].revives++; Pay(divers[by], 100); Quip("Revive", by); }
    Say("", "Diver " + std::to_string(d.slot + 1) + (by >= 0 ? " is back up" : " gets back up"), 3);
}

void Match::HitDiver(DiverState& d, float dmg, const std::string& by, const std::string& effect, Vector3 from, int attacker) {
    if (d.dead || d.invulnerable) return;
    if (d.downed) { d.downT -= dmg * 0.05f; return; }   // chewed while down: bleeds out faster
    if (d.shellT > 0) dmg *= 0.5f;                       // Hard Shell
    // the Shell Shield takes a beast's bite (not a sting or a spine); the mantis shrimp breaks it in two punches,
    // the Lobster in its last phase crushes it outright
    if (d.shieldHP > 0 && dmg > 0 && attacker >= 0 && attacker < (int)eco.agents.size()) {
        const Species& as = map->species[eco.agents[attacker].sp];
        std::string ef = Lower(effect);
        bool bite = !as.isEnemy && ef.find("touch") == std::string::npos && ef.find("contact") == std::string::npos && ef.find("sting") == std::string::npos;
        if (IsBoss(attacker) && bossKind == 1 && bossPhase >= 3) {
            d.shieldHP = 0; d.build = BuildType::None;
            Say(as.name, "crushes the Shell Shield", 3);
        } else if (bite) {
            d.shieldHP -= dmg * (HasW(as.name, "mantis") ? 5.0f : 1.0f);
            d.hurtT = 0.5f; d.hurtFrom = from;
            fx.push_back({8, d.pos, {0, 0, 0}});
            if (d.shieldHP <= 0) { d.shieldHP = 0; d.build = BuildType::None; Say("", "The Shell Shield splits", 3); }
            return;
        }
    }
    std::string e = Lower(effect);
    bool hold = e.find("hold") != std::string::npos || e.find("carr") != std::string::npos || e.find("roll") != std::string::npos || e.find("pin") != std::string::npos || e.find("drag") != std::string::npos;
    bool deferred = hold && e.find("teammate") != std::string::npos;   // "teammates have 4 s": the damage lands if nobody shoots it off
    if (!deferred) d.hp -= dmg;
    d.regenT = 0;
    d.hurtT = 1; d.hurtFrom = from;
    d.lastHitBy = by;
    if (e.find("bleed") != std::string::npos) d.bleedT = std::max(d.bleedT, NumAfter(e, "bleed", 3));
    if (e.find("poison") != std::string::npos) {
        if (e.find("over") != std::string::npos) { std::vector<float> n = Nums(e.substr(e.find("poison"))); float tot = n.size() > 0 ? n[0] : 20, over = n.size() > 1 ? n[1] : 10; d.poisonT = over; d.poisonDps = tot / std::max(1.0f, over); }
        else { d.poisonT = std::max(d.poisonT, NumAfter(e, "poison", 4)); d.poisonDps = 3; }
    }
    if (hold && attacker >= 0 && attacker < (int)eco.agents.size()) {
        float t = e.find("carr") != std::string::npos ? 3.0f : NumAfter(e, e.find("teammates have") != std::string::npos ? "have" : "hold", 2);
        if (e.find("pin") != std::string::npos) t = NumAfter(e, "pin", 3);
        size_t sh = e.find(" s hold");
        if (sh != std::string::npos) {                              // "40 and 1 s hold", "2 s hold, then Bite"
            size_t b = sh;
            while (b > 0 && (isdigit((unsigned char)e[b - 1]) || e[b - 1] == '.')) b--;
            if (b < sh) t = (float)atof(e.substr(b, sh - b).c_str());
        }
        d.heldT = std::max(0.5f, t);
        d.holder = attacker;
        d.holdDmg = 0;
        d.holdLethal = deferred;
        d.holdPending = deferred ? dmg : 0;
        d.holdHp0 = eco.agents[attacker].hp;
        d.struggle = 0;
        d.reloading = false;
        // the beast stays put while it holds (a drag carries the diver with it)
        if (e.find("carr") == std::string::npos) eco.agents[attacker].held = d.heldT;
        if (HasW(by, "crocodile")) crocHolds++;
        Say(by, deferred ? "has a diver! Shoot it off!" : "grabs a diver", 3);
    }
    if (e.find("knock") != std::string::npos) {
        float m = NumAfter(e, "knock", 2);
        Vector3 dir = Vector3Subtract(d.pos, from);
        dir.y *= 0.3f;
        if (Vector3Length(dir) < 0.01f) dir = {0, 0, 1};
        d.vel = Vector3Add(d.vel, Vector3Scale(Vector3Normalize(dir), m * 2.2f));
    }
    if (e.find("stun") != std::string::npos) d.stunT = std::max(d.stunT, NumAfter(e, "stun", 1));
    if (e.find("sway") != std::string::npos) d.aimSway = std::max(d.aimSway, NumAfter(e, "for", 3));
    if (e.find("flinch") != std::string::npos) d.flinchT = 0.4f;
    if (e.find("swim speed") != std::string::npos) { d.slowT = std::max(d.slowT, 6.0f); d.slowMult = 0.85f; }
    else if (e.find("slow") != std::string::npos) {
        // "slow 30% for 5 s", "Diver slowed 40% until brushed"
        float pct = NumAfter(e, "slow", 30);
        d.slowMult = std::min(d.slowMult, 1 - std::clamp(pct, 5.0f, 90.0f) / 100);
        d.slowT = std::max(d.slowT, e.find("until") != std::string::npos ? 8.0f : NumAfter(e, "for", 4));
    }
    if (e.find("net") == 0) { d.slowT = 3; d.slowMult = 0.35f; }
    if (e.find("steal") != std::string::npos) {
        if (d.limpets > 0) d.limpets--; else d.scrip -= std::min(d.scrip, 100);
        if (attacker >= 0) { Agent& a = eco.agents[attacker]; a.st = State::Flee; a.goal = a.home; a.target = d.agent; a.stateT = 0; }
        Say(by, "steals from a diver and flees home", 3);
    }
    if (d.hp <= 0) DownDiver(d, by);
}

// ---------------------------------------------------------------- weapons
void Match::Fire(int di, bool held, float dt) {
    DiverState& d = divers[di];
    if (d.dead || d.stunT > 0 || d.slipLink >= 0) return;          // "a diver can't fire inside" a slipstream
    if (d.zone >= 0 && d.zone < (int)noFireZone.size() && noFireZone[d.zone]) return;   // (the Squeeze: no weapons drawn)
    Held& h = Cur(d);
    const WeaponDef& w = W(h);
    if (w.melee) { if (held) Melee(di); return; }
    if (!held) { d.spin = 0; return; }
    if (d.reloading) {
        if (w.perRound && h.mag > 0) d.reloading = false;   // a thumb-loaded reload can be interrupted
        else return;
    }
    if (d.fireT > 0 || d.burstLeft > 0) return;
    if (h.mag <= 0) { Reload(di); return; }
    if (w.spinup > 0 && !h.forged) { d.spin += dt; if (d.spin < w.spinup) return; }   // the Gatling spins up (the Swarm doesn't)
    FireRound(d);
    if (w.burst > 0) d.burstLeft = (h.forged ? w.burst + 1 : w.burst) - 1;
}

void Match::FireRound(DiverState& d) {
    Held& h = Cur(d);
    const WeaponDef& w = W(h);
    const WeaponsData& WD = Weapons();
    if (!d.harpoonHour) h.mag--;
    d.fireT = 60.0f / std::max(1.0f, w.rpm) / (d.tonics.count("double") ? 1.33f : 1.0f);
    d.recoil = 1;
    Vector3 dir = Forward(d), eye = Eye(d);
    fx.push_back({2, Vector3Add(eye, Vector3Scale(dir, 0.45f)), dir});
    eco.AddNoise(eye, w.noise);
    float dmg = w.damage * (h.forged ? WD.forgeDmg : 1.0f);
    // rockfall: a loud shot in the Chimney or the Cathedral can bring a stalactite down near what it was aimed at
    const Json& rf = map->extra["rockfall"];
    if (d.zone >= 0 && d.zone < (int)rockZone.size() && rockZone[d.zone] && w.noise >= rf["min_noise"].F(6) && Rand() < rf["chance"].F(0.05)) {
        Vector3 p = eye;
        for (float t = 0; t < 25; t += 0.5f) { Vector3 q = Vector3Add(eye, Vector3Scale(dir, t)); if (!level.Inside(q, 0.1f, linkOpen, true)) break; p = q; }
        DropRocks(p, 1, rf["radius"].F(8) * 0.4f, rf["damage"].F(200), 1.6f, 1.0f, d.slot);
        Say("", "Rock shifts overhead...", 2);
    }
    if (w.cone > 0) { FireCone(d, w, dmg); return; }
    if (w.arc > 0) {
        // the Galvanic Rod: an arc that chains through everything in 10 m, stunning
        int best = -1; float bd = w.arc;
        for (int i = 0; i < (int)eco.agents.size(); i++) {
            const Agent& a = eco.agents[i];
            if (!a.alive || a.diver >= 0) continue;
            Vector3 to = Vector3Subtract(a.pos, eye);
            float along = Vector3DotProduct(to, dir);
            if (along < 0 || along > w.arc) continue;
            float off = Vector3Length(Vector3Subtract(to, Vector3Scale(dir, along)));
            if (off > 1.2f + bodies[a.sp].radius) continue;
            if (along < bd && level.Sight(eye, a.pos, linkOpen, true)) { bd = along; best = i; }
        }
        if (best >= 0) Arc(d, best, dmg, h.forged ? 6 : 4, w.arc, 1.5f);
        return;
    }
    float spread = (d.ads ? w.handling.spreadAds : w.handling.spreadHip);
    if (Vector3Length(d.vel) > 0.8f) spread /= std::max(0.3f, w.handling.swimAim);
    if (d.aimSway > 0 || d.flinchT > 0) spread *= 1.3f;
    Vector3 right = Vector3Normalize(Vector3CrossProduct(dir, {0, 1, 0}));
    Vector3 up = Vector3CrossProduct(right, dir);
    for (int p = 0; p < std::max(1, w.pellets); p++) {
        float ang = Rand() * 6.2832f, rad = sqrtf(Rand()) * spread * DEG2RAD;
        Vector3 v = Vector3Normalize(Vector3Add(dir, Vector3Add(Vector3Scale(right, cosf(ang) * tanf(rad)), Vector3Scale(up, sinf(ang) * tanf(rad)))));
        Dart t;
        t.pos = t.start = Vector3Add(eye, Vector3Scale(dir, 0.3f));
        t.vel = Vector3Scale(v, w.handling.speed);
        t.damage = dmg;
        t.weapon = h.def;
        t.owner = d.slot;
        t.forged = h.forged;
        t.alt = h.altAmmo;
        t.kind = w.chum > 0 ? 1 : w.net ? 2 : (w.splash > 0 || w.explodesOver > 0) ? 3 : w.polyp ? 9 : w.lure ? 10 : 0;
        if (h.forged && w.id == "trawlerman") t.pierce = 1;   // the Skipper: penetrates two beasts in a line
        darts.push_back(t);
    }
}

void Match::CycleTactical(int di) {
    DiverState& d = divers[di];
    int have[TAC_COUNT] = {1, d.inkBombs, d.chumBags, d.flares};   // (limpets always keep their place, even at 0)
    for (int k = 1; k <= TAC_COUNT; k++) { int t = (d.tactical + k) % TAC_COUNT; if (have[t] > 0) { d.tactical = t; return; } }
}
// an ink bomb's cloud (and an ink cap's): 5 m for 8 s; nothing sees or smells through it, and a Drowned lantern in it goes out
void Match::InkBurst(Vector3 at, int owner) {
    fx.push_back({9, at, {0, 0, 0}});
    eco.AddInk(at, 5, 8);
    for (int si = 0; si < (int)level.stations.size(); si++) {
        const Station& s = level.stations[si];
        if (s.type != StationType::Quest || s.name.find("lantern") == std::string::npos || lanternsOut.count(si) || Vector3Distance(s.pos, at) > 5) continue;
        lanternsOut.insert(si);
        Say("", TextFormat("The lantern gutters out under the ink: a page of the expedition's log (%d of 3). The swiftlets fall silent.", (int)lanternsOut.size()), 5);
    }
    (void)owner;
}
void Match::ThrowLimpet(int di) {
    DiverState& d = divers[di];
    if (d.dead || d.downed) return;
    int* stock = d.tactical == TAC_INK ? &d.inkBombs : d.tactical == TAC_CHUM ? &d.chumBags : d.tactical == TAC_FLARE ? &d.flares : nullptr;
    if (stock && *stock > 0) {
        // an ink bomb (kind 11), a chum bag (12) or a flare (13): thrown, it bursts on what it meets or on its fuse
        int kind = d.tactical == TAC_INK ? 11 : d.tactical == TAC_CHUM ? 12 : 13;
        if (--*stock == 0) d.tactical = TAC_LIMPET;
        Dart t;
        t.pos = t.start = Vector3Add(Eye(d), Vector3Scale(Forward(d), 0.4f));
        t.vel = Vector3Scale(Forward(d), 12);
        t.damage = 0; t.kind = kind; t.fuse = kind == 11 ? 1.2f : 1.5f; t.life = 6; t.owner = d.slot; t.weapon = -1;
        darts.push_back(t);
        return;
    }
    if (d.tactical != TAC_LIMPET) d.tactical = TAC_LIMPET;
    if (d.limpets <= 0) return;
    d.limpets--;
    Dart t;
    t.pos = t.start = Vector3Add(Eye(d), Vector3Scale(Forward(d), 0.4f));
    t.vel = Vector3Scale(Forward(d), 11);
    t.damage = 350; t.kind = 4; t.fuse = 3; t.life = 12; t.owner = d.slot; t.weapon = -1;
    darts.push_back(t);
}

void Match::Melee(int di) {
    DiverState& d = divers[di];
    if (d.dead || d.downed || d.meleeT > 0 || d.stunT > 0 || d.slipLink >= 0) return;
    const WeaponsData& WD = Weapons();
    if (d.heldT > 0 && d.holder <= -2) d.meleeT = 0;   // (hacking at the coral isn't slowed by the swing)
    const Held& h = Cur(d);
    const WeaponDef& w = W(h);
    bool weaponMelee = w.melee;
    float dmg = weaponMelee ? w.damage * (h.forged ? WD.forgeDmg : 1.0f) : WD.knifeDamage;
    float reach = weaponMelee ? (w.reach > 0 ? w.reach : 1.7f) : WD.knifeReach;
    float rate = weaponMelee ? w.rpm / 60.0f : WD.knifeRate;
    d.meleeT = 1.0f / std::max(0.2f, rate);
    Vector3 f = Forward(d);
    fx.push_back({8, Vector3Add(Eye(d), Vector3Scale(f, 0.6f)), f});
    // held by Reacher coral: the knife goes into the coral
    if (d.heldT > 0 && d.holder <= -2) { reacherHP[-2 - d.holder] -= dmg; return; }
    // a parasite on you comes off first (the knife scrapes it away)
    for (int i = 0; i < (int)eco.agents.size(); i++) {
        Agent& a = eco.agents[i];
        if (a.alive && a.host == d.agent && d.agent >= 0) { pendingMelee = true; eco.Kill(i, d.agent, true); pendingMelee = false; return; }
    }
    // the body nearest the crosshair within reach (not merely the nearest: a shark's flank is close to everything)
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < (int)eco.agents.size(); i++) {
        const Agent& a = eco.agents[i];
        if (!a.alive || a.diver >= 0) continue;
        Body b = bodies[a.sp];
        Vector3 to = Vector3Subtract(a.pos, Eye(d));
        float dist = Vector3Length(to) - b.radius - b.length * 0.3f;
        if (dist > reach) continue;
        float cosA = Vector3Length(to) > 0.2f ? Vector3DotProduct(Vector3Normalize(to), f) : 1.0f;
        if (cosA < 0.55f) continue;                              // a 60-degree cone
        float score = (1 - cosA) * 4 + std::max(0.0f, dist) * 0.3f;
        if (score < bd) { bd = score; best = i; }
    }
    if (best < 0) return;
    HitAgent(&d, best, dmg, false, true, f, nullptr);
    if (weaponMelee && w.chain > 0) Arc(d, best, dmg * 0.5f, h.forged ? 4 : w.chain, 6, h.forged ? 2.0f : 1.0f);
}

void Match::Arc(DiverState& d, int first, float dmg, int chain, float radius, float stun) {
    std::set<int> hit;
    int cur = first;
    Vector3 from = Eye(d);
    for (int k = 0; k <= chain && cur >= 0; k++) {
        hit.insert(cur);
        Vector3 at = eco.agents[cur].pos;
        fx.push_back({6, at, Vector3Subtract(from, at)});
        HitAgent(&d, cur, dmg * powf(0.85f, (float)k), false, false, Vector3Normalize(Vector3Subtract(at, from)), nullptr);
        if (cur < (int)eco.agents.size() && eco.agents[cur].alive) eco.agents[cur].stun = std::max(eco.agents[cur].stun, stun);
        from = at;
        int next = -1; float bd = radius;
        for (int i = 0; i < (int)eco.agents.size(); i++) {
            const Agent& a = eco.agents[i];
            if (!a.alive || a.diver >= 0 || hit.count(i)) continue;
            float dd = Vector3Distance(a.pos, at);
            if (dd < bd) { bd = dd; next = i; }
        }
        cur = next;
    }
}

void Match::Explode(Vector3 p, float dmg, float radius, int owner) {
    DiverState* d = owner >= 0 && owner < (int)divers.size() ? &divers[owner] : nullptr;
    fx.push_back({3, p, {0, 0, 0}});
    eco.AddNoise(p, 9, true);
    for (int i = 0; i < (int)eco.agents.size(); i++) {
        const Agent& a = eco.agents[i];
        if (!a.alive || a.diver >= 0) continue;
        float dd = Vector3Distance(a.pos, p);
        if (dd > radius + bodies[a.sp].radius) continue;
        HitAgent(d, i, dmg * (1 - 0.5f * std::min(1.0f, dd / std::max(0.1f, radius))), false, false, Vector3Normalize(Vector3Subtract(a.pos, p)), nullptr);
    }
    for (auto& o : divers) {
        if (o.dead || o.downed || Vector3Distance(o.pos, p) > radius) continue;
        if (o.tonics.count("bubbleskin")) continue;   // Bubbleskin: no self-damage
        HitDiver(o, std::min(60.0f, dmg * 0.15f), "a blast", "knockback 2 m", p);
    }
}

// A dart or blade lands on an agent: damage (weak point, armour, falloff already applied by the caller), scrip, the
// forged ammunition, and the Ecosystem's reaction (blood, a hurt beast turning on you, death and its bounty).
void Match::HitAgent(DiverState* d, int ai, float dmg, bool weak, bool melee, Vector3 dir, const Dart* dart) {
    if (ai < 0 || ai >= (int)eco.agents.size() || !eco.agents[ai].alive) return;
    Agent& a = eco.agents[ai];
    const Species& s = map->species[a.sp];
    if (s.isDiver) return;
    const Json& al = map->extra["allies"];
    if (d && al.IsObj() && s.Has(al["tag"].Str0("ally").c_str()) && !alliesHostile) {
        // "One shot turns the pod hostile for the match"
        alliesHostile = true;
        Say(s.name, "pod turns on the divers: they're hostile for the rest of the match", 5);
    }
    if (d && !s.isEnemy && s.size >= 4 && s.aggression < 0.35f && s.tier < 4 && !IsBoss(ai)) Quip("Harmless big thing shot", Living() > 1 ? -2 - d->slot : d->slot);
    const WeaponDef* w = dart && dart->weapon >= 0 ? &Weapons().weapons[dart->weapon] : nullptr;
    if (s.weakPoint == "none" || s.weakPoint.empty()) weak = false;
    bool frontal = Vector3DotProduct(dir, Facing(a)) < -0.3f;
    if (IsBoss(ai) && bossKind == 4) {
        // the Leviathan: its dark body takes half; a swallowed diver's teammates count toward the 200 on its gills
        if (swallowDiver >= 0) swallowDmg += dmg * (weak || bossGillsT > 0 ? 2 : 1);   // (the swallowed diver's teammates aim at the gills behind the lure)
        if (bossGillsT <= 0) dmg *= 0.5f;
    }
    if (IsBoss(ai) && bossKind == 3) {
        // the Wyrm: nothing gets through the cistern stone; up at a grate, its gills count toward a dragged diver
        if (wyrmState < 2 && wyrmStrandT <= 0) return;
        if (wyrmDragDiver >= 0) wyrmDragDmg += dmg * (weak ? 2 : 1);
    }
    if (s.isEnemy && a.unit >= 0) {
        // the Lost Ones' shields: a Legionnaire's tower shield soaks frontal hits (400); a blast or the wave tears it
        // away; the Armored Lost One's titan shield can't be shot through from the front at all ("must be flanked")
        const FactionUnit& u = map->faction.units[a.unit];
        bool blast = !dart && !melee;
        if (u.role == "phalanx") {
            if (!shieldHP.count(ai)) shieldHP[ai] = map->extra["shields"][u.unit].F(400);
            if (blast) shieldHP[ai] = 0;
            else if (frontal && shieldHP[ai] > 0) { shieldHP[ai] -= dmg; if (shieldHP[ai] > 0) { fx.push_back({1, a.pos, dir}); return; } dmg = -shieldHP[ai]; }
        }
        if (u.role == "wall" && frontal && !blast) { fx.push_back({1, a.pos, dir}); return; }
    }
    if (IsBoss(ai)) {
        if (d) bossProvoked = true;
        bossRecentDmg += dmg;
        // the Goliath's gills are behind the plates: exposed only in the windows after Inhale and Tail Slam
        bool exposed = bossGillsT > 0 || bossInhaleT >= 0;
        if (!exposed) weak = false;
        if (bossInhaleT >= 0 && (weak || frontal)) bossGillDmg += dmg * (weak ? 2 : 1);   // shooting into the open mouth counts
    }
    if (!weak && frontal && s.armorFront > 0) dmg *= 1 - std::clamp(s.armorFront, 0.0f, 0.9f);
    if (weak) dmg *= d && d->tonics.count("deadeye") ? 3.0f : (w ? w->handling.headshot : 1.5f);
    if (d && d->tonics.count("double")) dmg *= 2;          // Double Shot: darts count double
    if (frenzyT > 0 && !IsBoss(ai) && d) dmg = a.hp + 1;   // Blood Frenzy: every hit kills
    bool clean = false;
    if (dart && dart->forged && dart->alt >= 0) {
        const std::string& alt = Weapons().forgeAmmoTypes[std::clamp(dart->alt, 0, (int)Weapons().forgeAmmoTypes.size() - 1)];
        if (d) d->hitCount++;
        if (alt == "Galvanic" && d && d->hitCount % 6 == 0) Arc(*d, ai, dmg * 0.5f, 3, 6, 0.5f);
        else if (alt == "Chum Rounds") eco.AddBlood(a.pos, 6);
        else if (alt == "Flare Burst") a.stun = std::max(a.stun, 1.0f);
        else if (alt == "Riptide" && d && d->hitCount % 6 == 0) a.pos = map->zones[a.zone].Clamp(Vector3Add(a.pos, Vector3Scale(dir, 8)));
        else if (alt == "Boiling") { dmg *= 1.2f; clean = true; }
        if (!eco.agents[ai].alive) return;
    }
    if (dart && dart->forged && w && w->id == "longspeargun" && s.size <= 2) clean = true;   // the Quiet Word
    if (dart && w && w->pins && dart->forged && s.size <= 4) a.held = std::max(a.held, s.size <= 3 ? 3.0f : 1.0f);
    if (dart && dart->forged && w && w->id == "carbine" && weak && d) { Held& h = Cur(*d); h.mag = std::min((int)MagMax(*w, h), h.mag + 2); }
    if (getenv("DEPTH_HITLOG") && s.size >= 4) printf("   [dmg] t=%.1f %s takes %.0f (%s, dart %d kind %d)\n", time, s.name.c_str(), dmg, melee ? "melee" : "ranged", dart ? dart->weapon : -1, dart ? dart->kind : -1);
    a.weakHit = weak;
    pendingMelee = melee;
    pendingKiller = d ? d->slot : -1;
    if (clean) eco.suppressBlood = true;
    eco.Damage(ai, dmg, d ? d->agent : -1, melee, false);
    eco.suppressBlood = false;
    pendingMelee = false;
    pendingKiller = -1;
    fx.push_back({0, a.pos, dir});
    if (d) {
        Pay(*d, 10);                                        // "Hit a beast or enemy: 10"
        d->hitMarker = 0.2f; d->hitWeak = weak;
    }
}

void Match::UpdateDarts(float dt) {
    const WeaponsData& WD = Weapons();
    for (size_t k = 0; k < darts.size(); k++) {
        Dart& t = darts[k];
        if (!t.alive) continue;
        // a thrown limpet: sticks to what it touches and blows on its 3 s fuse
        if (t.kind == 11 || t.kind == 12 || t.kind == 13) {
            // a thrown ink bomb, chum bag or flare: bursts on whatever it meets, or on its fuse
            t.fuse -= dt;
            Vector3 np = Vector3Add(t.pos, Vector3Scale(t.vel, dt));
            t.vel = Vector3Scale(t.vel, powf(0.3f, dt)); t.vel.y -= 1.5f * dt;
            bool hit = !level.Inside(np, 0.05f, linkOpen, true);
            if (!hit) t.pos = np;
            for (int i = 0; i < (int)eco.agents.size() && !hit; i++) { const Agent& a = eco.agents[i]; if (a.alive && a.diver < 0 && Vector3Distance(a.pos, t.pos) < bodies[a.sp].radius + 0.15f) hit = true; }
            if (hit || t.fuse <= 0) {
                if (t.kind == 11) InkBurst(t.pos, t.owner);
                else if (t.kind == 12) { eco.AddChum(t.pos, 60); fx.push_back({0, t.pos, {0, 0, 0}}); }   // "a corpse chunk to lure predators to a spot"
                else { FlareLight fl; fl.pos = t.pos; fl.owner = t.owner; flareLights.push_back(fl); fx.push_back({4, t.pos, {0, 0, 0}}); }
                t.alive = false;
            }
            continue;
        }
        if (t.kind == 4) {
            t.fuse -= dt;
            if (t.stuck >= 0 && t.stuck < (int)eco.agents.size() && eco.agents[t.stuck].alive) t.pos = Vector3Add(eco.agents[t.stuck].pos, t.stuckOff);
            else if (Vector3Length(t.vel) > 0.01f) {
                Vector3 np = Vector3Add(t.pos, Vector3Scale(t.vel, dt));
                t.vel = Vector3Scale(t.vel, powf(0.3f, dt));
                t.vel.y -= 1.5f * dt;
                if (!level.Inside(np, 0.05f, linkOpen, true)) t.vel = {0, 0, 0};
                else t.pos = np;
                for (int i = 0; i < (int)eco.agents.size() && t.stuck < 0; i++) {
                    const Agent& a = eco.agents[i];
                    if (!a.alive || a.diver >= 0) continue;
                    if (Vector3Distance(a.pos, t.pos) < bodies[a.sp].radius + 0.15f) { t.stuck = i; t.stuckOff = Vector3Subtract(t.pos, a.pos); t.vel = {0, 0, 0}; }
                }
            }
            if (t.fuse <= 0) { Explode(t.pos, t.damage, 3, t.owner); t.alive = false; }
            continue;
        }
        Vector3 prev = t.pos;
        t.pos = Vector3Add(t.pos, Vector3Scale(t.vel, dt));
        t.vel = Vector3Scale(t.vel, powf(0.55f, dt));
        if (t.kind == 8) t.vel.y -= 4.0f * dt;                    // a lobbed coconut bomb sinks
        t.life -= dt;
        const WeaponDef* w = t.weapon >= 0 ? &WD.weapons[t.weapon] : nullptr;
        if (t.life <= 0 || !level.Inside(t.pos, 0.02f, linkOpen, true)) {
            if (t.kind == 8) EnemyBlast(prev, t.damage, 2.0f, t.enemy);
            else if (t.kind == 9) { Polyp pp; pp.pos = prev; pp.owner = t.owner; pp.forged = t.forged; polyps.push_back(pp); fx.push_back({4, prev, {0, 0, 0}}); }
            else if (t.kind == 10) { LurePt lp; lp.pos = prev; lp.owner = t.owner; lp.forged = t.forged; lures.push_back(lp); fx.push_back({4, prev, {0, 0, 0}}); }
            else if (t.kind == 1 || t.kind == 6) eco.AddChum(prev, w ? w->chum : 40);
            else if (t.kind == 3 && w && w->splash > 0) Explode(prev, t.damage, w->splash, t.owner);
            else fx.push_back({1, prev, t.vel});
            t.alive = false;
            continue;
        }
        if (t.enemy >= 0) {
            // an enemy's shot: hits divers only
            for (auto& d : divers) {
                if (d.dead || SegPointDist(prev, t.pos, d.pos) > 0.6f) continue;
                std::string by = "a Wrecker";
                if (t.enemy < (int)eco.agents.size() && eco.agents[t.enemy].unit >= 0) by = map->faction.units[eco.agents[t.enemy].unit].unit;
                if (t.kind == 8) { EnemyBlast(t.pos, t.damage, 2.0f, t.enemy); t.alive = false; break; }
                if (t.kind == 7) { HitDiver(d, t.damage, by, "hold 3 s", prev, t.enemy); auto it = map->faction.barks.find("net"); if (it != map->faction.barks.end() && !it->second.empty()) Say(map->faction.speciesName.empty() ? map->faction.name : map->faction.speciesName, it->second[0], 3); }
                else if (t.kind == 5) { HitDiver(d, 0, by, "net", prev); auto it = map->faction.barks.find("net"); if (it != map->faction.barks.end() && !it->second.empty()) Say(map->faction.speciesName, it->second[(int)(Rand() * it->second.size()) % it->second.size()], 3); }
                else if (t.kind == 6) { eco.AddChum(d.pos, 40); Quip("Chummed", d.slot); auto it = map->faction.barks.find("chum"); if (it != map->faction.barks.end() && !it->second.empty()) Say(map->faction.speciesName, it->second[0], 3); }
                else HitDiver(d, t.damage, by, by == "Speargunner" || by == "Foreman" ? "bleed 3 s" : "", prev);
                t.alive = false;
                break;
            }
            continue;
        }
        // a diver's dart: swept against every body's capsule (a dart moves ~0.5 m a frame; a sprat is 3 cm thick)
        Vector3 dir = Vector3Normalize(t.vel);
        if (bossKind == 4 && bossActive && lureHP > 0 && bossAgent >= 0 && SegPointDist(prev, t.pos, LurePos()) < 0.8f) {
            // the Leviathan's lure: 500 shoots it out, blinding it 5 s and baring the gills
            lureHP -= t.damage; t.alive = false; fx.push_back({1, t.pos, dir});
            if (lureHP <= 0) { lureRegrowT = 20; bossGillsT = map->extra["leviathan"]["gills_s"].F(5); eco.agents[bossAgent].stun = 5; Say(map->species[eco.agents[bossAgent].sp].name, "'s lure is shot out: blind, its gills bare", 4); }
            continue;
        }
        for (int i = 0; i < (int)eco.agents.size() && t.alive; i++) {
            Agent& a = eco.agents[i];
            if (!a.alive || a.diver >= 0) continue;
            const Species& sp = map->species[a.sp];
            Body b = bodies[a.sp];
            if (Vector3Distance(a.pos, t.pos) > b.length + 1.0f + Vector3Length(t.vel) * dt) continue;
            Vector3 fwd = Facing(a);
            Vector3 head = Vector3Add(a.pos, Vector3Scale(fwd, b.length * 0.5f)), tail = Vector3Subtract(a.pos, Vector3Scale(fwd, b.length * 0.5f));
            float tb = 0;
            if (SegSegDist(prev, t.pos, tail, head, &tb) > std::max(0.06f, b.radius) + 0.03f) continue;
            if (!level.Sight(t.pos, a.pos, linkOpen, true)) continue;   // not through a wall into a body on the far side
            if (t.kind == 1) { eco.AddChum(a.pos, w ? w->chum : 40); t.alive = false; break; }
            if (t.kind == 10) { LurePt lp; lp.pos = t.pos; lp.owner = t.owner; lp.forged = t.forged; lures.push_back(lp); t.alive = false; break; }
            if (t.kind == 9) {
                // a polyp roots in whatever it hits and grabs it
                a.held = std::max(a.held, sp.size <= 3 ? 6.0f : sp.size == 4 ? 3.0f : 1.0f);
                HitAgent(t.owner >= 0 && t.owner < (int)divers.size() ? &divers[t.owner] : nullptr, i, t.damage, false, false, dir, &t);
                t.alive = false; break;
            }
            if (t.kind == 2) {
                // a net: holds a beast up to size 3 for 6 s, size 4 for 3 s (forged: size 4 for 4 s, size 5 for 2 s)
                float hold = sp.size <= 3 ? 6.0f : sp.size == 4 ? (t.forged ? 4.0f : 3.0f) : sp.size == 5 && t.forged ? 2.0f : 0.0f;
                a.held = std::max(a.held, hold);
                t.alive = false; break;
            }
            float travelled = Vector3Distance(t.start, t.pos);
            const WeaponClass& hc = w ? w->handling : WeaponClass{};
            float fall = travelled <= hc.fullTo ? 1.0f : travelled >= hc.halfAt ? 0.5f : 1.0f - 0.5f * (travelled - hc.fullTo) / std::max(1.0f, hc.halfAt - hc.fullTo);
            bool weak = tb > 0.72f;                                // gills and eyes, behind the head
            if (t.kind == 3 && w && (w->splash > 0 || (w->explodesOver > 0 && (sp.size > w->explodesOver || t.forged)))) {
                Explode(t.pos, t.damage, w->splash > 0 ? w->splash : 3, t.owner);
                t.alive = false; break;
            }
            HitAgent(t.owner >= 0 && t.owner < (int)divers.size() ? &divers[t.owner] : nullptr, i, t.damage * fall, weak, false, dir, &t);
            if (t.pierce > 0) { t.pierce--; continue; }
            t.alive = false;
        }
        // flora tools: a dart into Bloodvine, an ink cap, a crystal coral cluster
        for (int pi = 0; pi < (int)eco.flora.size() && t.alive; pi++) {
            const FloraPatch& fp = eco.flora[pi];
            bool grab = map->extra["flora_rules"][map->flora[fp.flora].name]["grab"].Bool0();
            if (fp.units <= 0 || (!grab && !HasW(map->flora[fp.flora].type, "tool"))) continue;
            Vector3 c = Vector3Add(fp.pos, {0, 0.4f, 0});
            if (SegPointDist(prev, t.pos, c) > 0.9f) continue;
            if (grab) {
                // Reacher coral shot: 400 HP frees whoever it holds
                if (!reacherHP.count(pi)) reacherHP[pi] = map->extra["flora_rules"][map->flora[fp.flora].name]["hp"].F(400);
                reacherHP[pi] -= t.damage;
                fx.push_back({1, c, t.vel});
                t.alive = false;
                continue;
            }
            FloraTool(pi, c, t.owner >= 0 && t.owner < (int)divers.size() ? &divers[t.owner] : nullptr);
            t.alive = false;
        }
    }
    darts.erase(std::remove_if(darts.begin(), darts.end(), [](const Dart& x) { return !x.alive; }), darts.end());
}

// ---------------------------------------------------------------- deaths: scrip, quota, keys, drops
void Match::OnDeath(int ai, int killer) {
    Agent& a = eco.agents[ai];
    const Species& s = map->species[a.sp];
    int di = DiverOfAgent(killer);
    if (di < 0 && killer == -1 && pendingKiller >= 0) di = pendingKiller;
    if (ai == bossAgent) {
        bossKilled = true;
        if (di >= 0 && !divers[di].bot) dossierSeen.insert(s.name);
        // "Kill reward: 1,500 base scrip x tide, a guaranteed Locker weapon for each diver, the captain's safe key"
        for (auto& d : divers) if (!d.dead) {
            Pay(d, 1500.0f * tide * (supperCall ? 2 : 1));
            if (!d.downed) GiveLockerWeapon(d);
            if (supperCall && !d.downed) {
                // the Supper Call's reward: the weapon in hand comes out of the Forge
                Held& h = Cur(d);
                if (!h.forged && W(h).source != "drop") { h.forged = true; h.altAmmo = (int)(Rand() * Weapons().forgeAmmoTypes.size()) % std::max(1, (int)Weapons().forgeAmmoTypes.size()); h.mag = (int)MagMax(W(h), h); h.reserve = (int)ResMax(W(h), h); }
            }
        }
        if (supperCall) Say("", "Supper Call: the engineer would be proud. Every weapon in hand is pressure-forged.", 6);
        supperCall = false;
        GiveKey("Goliath", di, a.pos);
        const Json& bk = map->extra["boss_key"];
        if (bk.IsObj()) { GiveKey(bk["key"].Str0(), di, a.pos); Say("", bk["text"].Str0(), 6); }
        bossActive = false; bossAgent = -1; bossInhaleT = -1; bossInhaleDiver = -1;
        for (auto& d : divers) if (d.holder == ai) { d.heldT = 0; d.holder = -1; }
        if (bossKind == 2) {
            for (int pi : pod) if (pi < (int)eco.agents.size() && eco.agents[pi].alive) { eco.agents[pi].st = State::Flee; eco.agents[pi].goal = eco.agents[pi].home; }
            bool held = false; for (auto& d : divers) if (d.drumUses > 0) held = true;
            if (!held && di >= 0) { divers[di].drumUses = 5; Say("", "The Shaman's drum is yours (F to beat it)", 4); }
            pod.clear(); podSpawned = false;
        }
        if (bossKind == 4) { bossReformT = map->extra["leviathan"]["reform_s"].F(360); swallowDiver = -1; lureDriftT = 0; darkT = 0; }
        if (bossKind == 3) {
            wyrmReformT = map->extra["wyrm"]["reform_s"].F(360); wyrmState = 0; wyrmDragDiver = -1; wyrmStrandT = 0; eco.bloodMult = 1; floodT = 0;
        }
        if (bossKind == 0) Say(s.name, "is dead. Its key lies in the engine room's silt.", 6);
        else Say(s.name, "is dead.", 5);
        if (di >= 0) { divers[di].kills++; if (phase == TidePhase::Tide) tideKills++; }
        return;
    }
    if (di >= 0 && HasW(s.name, "octopus") && !keys.count("Octopus")) { GiveKey("Octopus", di, a.pos); Say("", "The octopus drops a brass key (the captain's safe)", 4); }
    if (di >= 0 && s.isEnemy && a.unit >= 0 && map->faction.units[a.unit].huntOnly && !keys.count("Foreman")) { GiveKey("Foreman", di, a.pos); Say("", "The Foreman's key (the captain's safe)", 4); }
    if (s.name == "Sperm Whale") for (auto& o : eco.agents) if (o.alive && map->species[o.sp].name == "The Relict" && !relictGone) { o.st = State::Investigate; o.goal = a.pos; o.stateT = 0; o.hunger = 1; Say("", "Something vast rises from the void toward the whale's body", 4); }   // a whale kill brings the Relict
    if (s.isEnemy && map->extra["ichor"].IsObj()) ichor.push_back({a.pos, map->extra["ichor"]["seconds"].F(60)});   // black ichor in the street
    if (s.isEnemy) {
        auto it = map->faction.barks.find("death");
        if (it != map->faction.barks.end() && !it->second.empty() && Rand() < 0.4f) Say(map->faction.speciesName, it->second[(int)(Rand() * it->second.size()) % it->second.size()], 3);
    }
    if (di >= 0 && !divers[di].bot && dossierSeen.insert(DossierName(ai)).second) Say("", "Dossier: " + DossierName(ai), 2);
    if (di < 0 && s.isEnemy && killer >= 0 && killer < (int)eco.agents.size() && eco.agents[killer].diver < 0) Quip("Enemy eaten");
    if (di >= 0 && !quipFirst) { quipFirst = true; Quip("First blood", di); }
    else if (di >= 0 && s.Cleaner()) Quip("Cleaner killed", Living() > 1 ? -2 - di : di);
    if (di < 0) return;                                      // eaten or killed by the reef: no scrip, not the quota
    DiverState& d = divers[di];
    float bounty;
    if (s.isEnemy && a.unit >= 0) {
        const FactionUnit& u = map->faction.units[a.unit];
        bounty = u.loot * map->Tide(tide).bountyMult;
        int wi = Weapons().Index(u.drops);
        if (wi >= 0) { FloorDrop fd; fd.weapon = wi; fd.pos = a.pos; fd.t = 30; drops.push_back(fd); }   // "plus their dropped weapon"
        if (u.drops == "drum") { FloorDrop fd; fd.weapon = -2; fd.pos = a.pos; fd.t = 60; drops.push_back(fd); }   // the Shaman's drum
    } else bounty = s.bountyBase * map->Tide(tide).bountyMult;
    if (a.weakHit) { bounty *= 1.5f; d.headshots++; }
    if (pendingMelee) bounty += 30;
    if (d.luckKills > 0) { bounty *= 2; d.luckKills--; }   // Fisher's Luck
    Pay(d, bounty);
    d.kills++;
    killsBy[s.name]++;
    if (phase == TidePhase::Tide) tideKills++;
    if (predatorHunt && s.tier == 4) huntApexLeft--;
    char buf[96];
    snprintf(buf, sizeof buf, "%s  +%d", s.name.c_str(), (int)lroundf(bounty * (doubleScripT > 0 ? 2 : 1)));
    d.lastKill = buf; d.lastKillT = 2.5f;
    MaybeDrop(a.pos);
}

// ---------------------------------------------------------------- beasts and divers
bool Match::DecideHook(Agent& a, int idx) {
    const Species& s = map->species[a.sp];
    if (s.isDiver) return true;
    if (s.isEnemy) return DecideEnemy(a, idx);
    if (idx == bossAgent && bossKind == 3) {
        // the Wyrm's script places it; only in phase 3 does it hunt the streets itself
        int di = wyrmState == 3 ? NearestDiver(a.pos, 40, false) : -1;
        if (di >= 0) { a.st = State::Investigate; a.goal = map->zones[a.zone].Clamp(divers[di].pos, 1.0f); a.stateT = 0; a.target = -1; }
        else { a.st = State::Rest; a.goal = a.pos; a.vel = {0, 0, 0}; }
        return true;
    }
    if (idx == bossAgent && bossActive) return DecideBoss(a, idx);
    // the Reef's drum quest done: hammerheads keep off Turtle Beach
    if (hammerAvoid && HasW(s.name, "hammerhead") && map->zones[a.zone].name == "Turtle Beach") { a.st = State::Return; a.goal = a.home; a.target = -1; a.stateT = 0; return true; }
    // the Void's Relict: it comes for its egg wherever it is, and attacks the Station Chief's mech on sight
    if (s.name == "The Relict") {
        if (relictGone) { a.st = State::Rest; a.vel = {0, 0, 0}; return true; }
        for (const auto& d : divers) if (d.egg && !d.dead && d.agent >= 0) { a.st = State::Hunt; a.target = d.agent; a.targetCorpse = false; a.stateT = 0; a.hunger = 1; return true; }
        if (eggPlaced) { a.st = State::Investigate; a.goal = eggPos; a.stateT = 0; return true; }
        for (int i = 0; i < (int)eco.agents.size(); i++) {
            const Agent& o = eco.agents[i];
            if (o.alive && o.unit >= 0 && map->species[o.sp].isEnemy && map->faction.units[o.unit].role == "chief" && Vector3Distance(o.pos, a.pos) < 30) {
                if (a.target != i) { auto it = map->faction.barks.find("relict"); if (it != map->faction.barks.end() && !it->second.empty()) Say(map->faction.speciesName, it->second[0], 4); }
                a.st = State::Hunt; a.target = i; a.targetCorpse = false; a.stateT = 0; return true;
            }
        }
    }
    // Atlantis: dead Lost Ones' ichor keeps the apex beasts out of a street and draws the scavengers in
    if (!ichor.empty()) {
        float repel = map->extra["ichor"]["repel_m"].F(20);
        if (s.tier >= 4 || HasW(s.name, "shark")) {
            for (const auto& ic : ichor) if (Vector3Distance(ic.pos, a.pos) < repel) {
                Vector3 away = Vector3Subtract(a.pos, ic.pos); away.y = 0;
                if (Vector3Length(away) < 0.1f) away = {1, 0, 0};
                a.st = State::Investigate; a.goal = map->zones[a.zone].Clamp(Vector3Add(a.pos, Vector3Scale(Vector3Normalize(away), repel)), 1.0f); a.stateT = 0; a.target = -1;
                return true;
            }
        } else if ((s.Scavenger() || map->diet[a.sp].corpse > 0.1f) && a.st != State::Flee && a.st != State::Feed) {
            for (const auto& ic : ichor) if (Vector3Distance(ic.pos, a.pos) < 40 && Vector3Distance(ic.pos, a.pos) > 1.5f && eco.ZoneAt(ic.pos) == a.zone) {
                a.st = State::Investigate; a.goal = ic.pos; a.stateT = 0;
                return true;
            }
        }
    }
    // the Matriarch's pod hunts as one while she's up
    if (s.Has("pod")) {
        if (!bossActive) return false;
        int di = NearestDiver(a.pos, 1e9f, false);
        if (di >= 0) { a.st = State::Hunt; a.target = divers[di].agent; a.targetCorpse = false; a.stateT = 0; a.fedT = 0; }
        return di >= 0;
    }
    // allies (the Reef's dolphins): follow the divers and drive off any shark that comes near one, until shot
    const Json& al = map->extra["allies"];
    if (al.IsObj() && s.Has(al["tag"].Str0("ally").c_str()) && !alliesHostile) {
        if (a.st == State::Hunt && !a.targetCorpse && a.target >= 0 && a.target < (int)eco.agents.size() && eco.agents[a.target].alive && eco.agents[a.target].diver < 0 && a.stateT < 12) return true;
        float guard = al["guard_m"].F(10);
        for (int i = 0; i < (int)eco.agents.size(); i++) {
            const Agent& o = eco.agents[i];
            if (!o.alive || o.diver >= 0 || i == idx) continue;
            const Species& os = map->species[o.sp];
            if (os.Has(al["tag"].Str0("ally").c_str()) || !(HasW(os.name, "shark") || HasW(os.name, "hammerhead"))) continue;
            for (const auto& d : divers) if (!d.dead && Vector3Distance(d.pos, o.pos) < guard) { a.st = State::Hunt; a.target = i; a.targetCorpse = false; a.stateT = 0; return true; }
        }
        Vector3 c{0, 0, 0}; int n = 0;
        for (const auto& d : divers) if (!d.dead) { c = Vector3Add(c, d.pos); n++; }
        if (n == 0) return false;
        c = Vector3Scale(c, 1.0f / n);
        Vector3 off{cosf(idx * 1.7f + time * 0.1f), 0.3f, sinf(idx * 1.7f + time * 0.1f)};
        Vector3 goal = Vector3Add(c, Vector3Scale(off, al["follow_m"].F(25) * 0.5f));
        int gz = eco.ZoneAt(goal);
        if (gz < 0 || gz != eco.ZoneAt(c)) goal = c;
        a.st = State::Investigate; a.goal = goal; a.stateT = 0;
        return true;
    }
    if (idx == bossAgent) {
        // dozing: it doesn't rise to defend its territory against divers (it wakes by UpdateBoss's rules)
        for (const auto& d : divers) if (!d.dead && Vector3Distance(d.pos, a.home) < std::max(s.defendR, 8.0f) + 4) {
            if (a.st == State::Defend || a.st == State::Hunt) { a.st = State::Return; a.target = -1; }
            if (Vector3Distance(a.pos, a.home) < 2) { a.st = State::Rest; a.vel = {0, 0, 0}; }
            else { a.st = State::Return; a.goal = a.home; }
            return true;
        }
    }
    // already on a diver: keep after it, or let it go once it's down (a downed diver is blood, not prey)
    if (!a.targetCorpse && a.target >= 0 && a.target < (int)eco.agents.size() && (a.st == State::Hunt || a.st == State::Defend)) {
        const Agent& t = eco.agents[a.target];
        if (t.diver >= 0) {
            if (!t.alive || t.downed) { a.st = State::Return; a.goal = a.home; a.target = -1; a.stateT = 0; return true; }
            if (a.st == State::Hunt) {
                if ((a.stateT < 20 && Vector3Distance(a.pos, t.pos) < std::max(s.sight, 12.0f) * 1.8f) || (predatorHunt && s.tier == 4)) return true;
                a.st = State::Return; a.goal = a.home; a.target = -1; a.stateT = 0;
                return true;
            }
            return false;
        }
    }
    if (a.fedT > 0 || s.size < 2 || s.Sessile() || s.Parasite()) return false;
    bool pred = predatorHunt && s.tier == 4;
    if (phase == TidePhase::Calm && !pred) return false;
    // tides 1-3 are 'ecosystem calm': nothing hunts a diver unprovoked (territory and being shot still start fights)
    if (tide < 4 && !pred) return false;
    // the nearest diver it can see (its own room; the whole map in a Predator Hunt)
    int best = -1; float bd = pred ? 1e9f : std::max(s.sight, 8.0f);
    for (const auto& d : divers) {
        if (d.dead || d.downed || d.agent < 0) continue;
        if (!pred && d.zone != a.zone) continue;
        float dist = Vector3Distance(d.pos, a.pos);
        if (dist < bd) { bd = dist; best = d.slot; }
    }
    if (best < 0) return false;
    const DiverState& d = divers[best];
    float aggr = s.aggression + a.aggrMod + (eco.cleanerRage > 0 ? 0.2f : 0);
    bool want = pred;
    if (alliesHostile && al.IsObj() && s.Has(al["tag"].Str0("ally").c_str()) && bd < 15) want = true;   // the pod, turned
    // "Tier 4 apex: eat everything below, including enemies and divers; arrive by blood from tide 4"
    if (s.tier >= 4 && tide >= 4 && a.hunger >= 0.45f) want = true;
    // mid predators that came for the blood find a diver in it
    if (s.tier == 3 && s.size >= 3 && a.st == State::Investigate && aggr >= 0.4f) want = true;
    // a bleeding diver smells like a meal (the apex only from tide 4: tides 1-3 are 'ecosystem calm')
    if (s.tier >= 3 && (s.tier == 3 || tide >= 4) && (d.hp < d.hpMax * 0.5f || d.bleedT > 0) && bd < 10 && aggr >= 0.4f) want = true;
    // the plainly aggressive go for anything close
    if (aggr >= 0.85f && s.size >= 2 && bd < 5 && (s.tier < 4 || tide >= 4)) want = true;
    if (!want || Rand() > 0.25f) return false;
    a.st = State::Hunt;
    a.target = d.agent;
    a.targetCorpse = false;
    a.stateT = 0;
    return true;
}

void Match::OnDiverHit(int diverAgent, int attacker, float dmg) {
    int di = DiverOfAgent(diverAgent);
    if (di < 0) return;
    DiverState& d = divers[di];
    if (attacker < 0 || attacker >= (int)eco.agents.size()) { HitDiver(d, dmg, "the reef", "", d.pos); return; }
    Agent& a = eco.agents[attacker];
    const Species& s = map->species[a.sp];
    if (s.isEnemy || IsBoss(attacker) || s.isDiver) return;  // the Wreckers and the Goliath fight by their own rules below
    if (getenv("DEPTH_HITLOG")) printf("   [hit] t=%.1f tide %d: %s (%s, stateT %.1f, hunger %.2f, fed %.0f) hits diver %d\n", time, tide, s.name.c_str(), StateName(a.st), a.stateT, a.hunger, a.fedT, di);
    // pick one of the beast's attacks (Attacks sheet) that reaches
    float dist = Vector3Distance(a.pos, d.pos);
    std::vector<int> ok;
    auto usable = [&](const Attack& at) { return !HasW(at.name, "inhale") && !HasW(at.name, "herd") && !HasW(at.name, "sharks") && !(HasW(at.name, "if shot") && false); };
    for (int k : attacksBySp[a.sp]) { const Attack& at = map->attacks[k]; if (usable(at) && at.range + 0.8f >= dist) ok.push_back(k); }
    if (ok.empty()) for (int k : attacksBySp[a.sp]) if (usable(map->attacks[k])) ok.push_back(k);
    if (ok.empty()) { HitDiver(d, dmg, s.name, "", a.pos, attacker); return; }
    float tot = 0;
    for (int k : ok) tot += map->attacks[k].damage + 10;
    float r = Rand() * tot;
    int pick = ok.back();
    for (int k : ok) { r -= map->attacks[k].damage + 10; if (r <= 0) { pick = k; break; } }
    const Attack& at = map->attacks[pick];
    // tides 1-3 are 'ecosystem calm': an apex a diver provoked (a stray dart) gives one warning strike and leaves
    if (tide < 4 && s.tier >= 4 && s.defendR <= 0 && a.st == State::Defend) {
        HitDiver(d, at.damage * 0.35f, s.name, "knockback 3 m", a.pos, attacker);
        a.st = State::Return; a.goal = a.home; a.target = -1; a.stateT = 0; a.cooldown = 4;
        return;
    }
    HitDiver(d, at.damage * s.attackScale * map->Tide(tide).dmgMult, s.name, at.effect, a.pos, attacker);
    a.cooldown = std::max(a.cooldown, std::max(0.6f, at.cooldown));
    if (HasW(at.effect, "passes through")) { a.st = State::Return; a.goal = a.home; a.stateT = 0; a.target = -1; }
}

// Contact attacks (stonefish, lionfish and rabbitfish spines, the jelly bloom's sting): touching them is the attack.
void Match::BeastsVsDivers(float dt) {
    for (auto& kv : contactT) kv.second -= dt;
    for (auto& d : divers) {
        if (d.dead || d.downed) continue;
        for (int i = 0; i < (int)eco.agents.size(); i++) {
            const Agent& a = eco.agents[i];
            if (!a.alive || a.diver >= 0) continue;
            const auto& atk = attacksBySp[a.sp];
            if (atk.empty()) continue;
            float dist = Vector3Distance(a.pos, d.pos);
            if (dist > 3) continue;
            for (int k : atk) {
                const Attack& at = map->attacks[k];
                if (at.windup > 0 || !(HasW(at.effect, "touch") || HasW(at.effect, "contact"))) continue;
                if (dist > at.range + 0.45f + bodies[a.sp].radius) continue;
                float& cd = contactT[i];
                if (cd > 0) continue;
                cd = std::max(1.0f, at.cooldown);
                HitDiver(d, at.damage * map->Tide(tide).dmgMult, map->species[a.sp].name, at.effect, a.pos, i);
            }
        }
    }
}

void Match::FloraHazards(float dt) {
    for (auto& kv : patchT) kv.second -= dt;
    for (auto& d : divers) {
        if (d.dead || d.downed) continue;
        for (int i = 0; i < (int)eco.flora.size(); i++) {
            const FloraPatch& p = eco.flora[i];
            const Flora& f = map->flora[p.flora];
            const Json& rule = map->extra["flora_rules"][f.name];
            if (p.units <= 0 || Vector3Distance(d.pos, p.pos) > 1.4f) continue;
            float& cd = patchT[i * 8 + d.slot];
            if (cd > 0) continue;
            if (rule["grab"].Bool0()) {
                // Reacher coral grabs a diver who brushes it (the diver can still fire; teammates cut them loose)
                if (d.heldT > 0 || d.invulnerable) continue;
                if (!reacherHP.count(i)) reacherHP[i] = rule["hp"].F(400);
                d.heldT = 999; d.holder = -2 - i; d.holdLethal = false; d.struggle = 0; d.cutT = 0; d.reloading = false;
                Say("Reacher coral", "has a diver: cut them loose or shoot it", 3);
                cd = 2;
                continue;
            }
            if (rule["noise"].F(0) > 0) { eco.AddNoise(p.pos, rule["noise"].F(4)); cd = 2; continue; }   // Halimeda shatters loudly
            if (f.contactDamage <= 0) continue;
            cd = 1.0f;
            HitDiver(d, f.contactDamage * map->Tide(tide).dmgMult, f.name, f.effect, p.pos);
        }
    }
}

int Match::NearestDiver(Vector3 p, float r, bool needSight, bool upOnly) const {
    int best = -1; float bd = r;
    for (const auto& d : divers) {
        if (d.dead || (upOnly && d.downed) || d.ghostT > 0) continue;   // (Ghost Fin: lost to scent and sound)
        if (!eco.inks.empty() && (eco.InInk(d.pos) || eco.InInk(p))) continue;   // (in the ink: not seen, not smelled)
        float dist = Vector3Distance(d.pos, p);
        if (dist >= bd) continue;
        if (needSight && !level.Sight(p, Eye(d), linkOpen, true)) continue;
        bd = dist; best = d.slot;
    }
    return best;
}

// ---------------------------------------------------------------- the Wreckers
bool Match::DecideEnemy(Agent& a, int idx) {
    if (a.unit < 0) return false;
    // a Decoy Buoy draws the faction to it too
    for (const auto& dp : deployed) if (dp.alive && dp.type == BuildType::DecoyBuoy && Vector3Distance(dp.pos, a.pos) < 35 && !IsBoss(idx)) { a.st = State::Investigate; a.goal = dp.pos; a.target = -1; return true; }
    const FactionUnit& u = map->faction.units[a.unit];
    const Faction& F = map->faction;
    // "They bleed and panic; sharks hunt them": anything big hunting nearby sends them back to the breach
    // (the Cave's Drowned "ignore beasts entirely")
    if (!F.ignoreBeasts) for (const auto& o : eco.agents) {
        if (!o.alive || o.diver >= 0) continue;
        const Species& os = map->species[o.sp];
        if (os.isEnemy || os.size < F.fleeFromSize) continue;
        if ((o.st == State::Hunt || o.st == State::Investigate) && Vector3Distance(o.pos, a.pos) < 10) {
            if (a.st != State::Flee) {
                auto it = F.barks.find("shark");
                if (it != F.barks.end() && !it->second.empty() && Rand() < 0.3f) Say(F.speciesName, it->second[(int)(Rand() * it->second.size()) % it->second.size()], 3);
            }
            return false;                                     // the Ecosystem's own decision: flee
        }
    }
    bool hunt = a.squad >= 0 && a.squad < (int)eco.squads.size() && eco.squads[a.squad].hunt;
    // the Reef's Raiders shoot dolphins on purpose, to turn the pod on the divers
    const Json& al = map->extra["allies"];
    if (F.shootsAllies && !alliesHostile && al.IsObj() && Rand() < 0.01f) {
        for (int i = 0; i < (int)eco.agents.size(); i++) {
            const Agent& o = eco.agents[i];
            if (!o.alive || !map->species[o.sp].Has(al["tag"].Str0("ally").c_str()) || Vector3Distance(o.pos, a.pos) > 20) continue;
            eco.Damage(i, 30, idx);
            alliesHostile = true;
            auto it = F.barks.find("ally");
            Say(F.speciesName.empty() ? F.name : F.speciesName, it != F.barks.end() && !it->second.empty() ? it->second[0] : "The pod is ours now.", 4);
            Say(map->species[o.sp].name, "pod turns on the divers", 4);
            break;
        }
    }
    if (u.role == "sentinel") {
        // a Sentinel takes the nearest doorway of its room and holds it: it never advances
        Vector3 best = a.pos; float bd = 1e9f;
        for (const auto& l : map->links) { if (!l.diverOk) continue; Vector3 m = l.from == a.zone ? l.a : l.to == a.zone ? l.b : Vector3{1e9f, 0, 0}; float dd = Vector3Distance(m, a.pos); if (dd < bd) { bd = dd; best = m; } }
        a.st = State::Investigate; a.goal = best; a.stateT = 0; a.target = -1;
        return true;
    }
    if (u.role == "chief") {
        // the Station Chief holds the mech bay and switches Beacon 4 on (drawing the beasts to the Forge)
        for (int si = 0; si < (int)level.stations.size(); si++) if (HasW(level.stations[si].name, "beacon 4") && !beaconOn[si]) { beaconOn[si] = true; auto it = F.barks.find("chief"); if (it != F.barks.end() && !it->second.empty()) Say(F.speciesName, it->second[0], 4); }
        for (const auto& p : map->pois) if (HasW(p.name, "mech bay") && p.zone >= 0) { a.st = State::Investigate; a.goal = map->zones[p.zone].Clamp(p.pos, 1.5f); a.stateT = 0; a.target = -1; return true; }
    }
    if (u.role == "researcher") {
        // Researchers switch on any lure beacon they pass
        for (int si = 0; si < (int)level.stations.size(); si++) {
            const Station& st = level.stations[si];
            if (!HasW(st.name, "beacon") || HasW(st.name, "control") || HasW(st.name, "re-aim") || beaconOn[si] || Vector3Distance(st.pos, a.pos) > 25) continue;
            beaconOn[si] = true;
            auto it = F.barks.find("beacon"); if (it != F.barks.end() && !it->second.empty()) Say(F.speciesName, it->second[0], 3);
        }
    }
    if (u.role == "voidcult") {
        // a Void Cultist walks for the nearest overlook; at the edge it calls the deep
        Vector3 best{}; float bd = 1e9f;
        for (const auto& l : map->links) {
            bool toVoid = l.to < (int)voidZone.size() && voidZone[l.to];
            if (!toVoid) continue;
            float dd = Vector3Distance(l.a, a.pos) + (l.from == a.zone ? 0 : 200);
            if (dd < bd) { bd = dd; best = l.a; }
        }
        if (bd < 1e8f) {
            if (Vector3Distance(best, a.pos) < 3 && tentacleT <= 0) {
                tentacleT = 20; tentacleCd = 1; tentaclePos = best;
                int hf = map->SpeciesIndex("Hatchetfish");
                if (hf >= 0) for (int k = 0; k < 3; k++) eco.Spawn(hf, map->zones[a.zone].Clamp(Vector3Add(best, {Rand(-3, 3), Rand(-1, 1), Rand(-3, 3)}), 1), a.zone);
                auto it = F.barks.find("cult"); if (it != F.barks.end() && !it->second.empty()) Say(F.speciesName, it->second[0], 4);
                Say("", "A colossal squid's arm rises over the overlook", 4);
            }
            int dz = NearestDiver(a.pos, 10, false);
            if (dz < 0 || Vector3Distance(divers[dz].pos, a.pos) > 2.5f) { a.st = State::Investigate; a.goal = best; a.stateT = 0; a.target = -1; return true; }
        }
    }
    if (u.role == "priest") {
        // the Priest stands in the plaza and chants; he never comes to the divers
        for (const auto& p : map->pois) if (HasW(p.name, "plaza") && p.zone >= 0) { a.st = State::Investigate; a.goal = map->zones[p.zone].Clamp(p.pos, 1.0f); a.stateT = 0; a.target = -1; return true; }
    }
    int di = NearestDiver(a.pos, hunt ? 1e9f : 30.0f, !hunt);
    if (di >= 0) {
        const DiverState& d = divers[di];
        if (a.squad >= 0 && !squadSpotted.count(a.squad)) {
            squadSpotted.insert(a.squad);
            auto it = F.barks.find("spot");
            if (it != F.barks.end() && !it->second.empty()) Say(F.speciesName, it->second[(int)(Rand() * it->second.size()) % it->second.size()], 4);
        }
        // hold each role's distance: Cutters close in, the Speargunner covers from 12 m, the Netman from 6 m
        float want = u.role == "flank" ? 1.0f : u.role == "cover" ? 12.0f : u.role == "net" ? 6.0f : u.role == "chum" ? 10.0f
                   : u.role == "lure" ? 9.0f : u.role == "bell" ? 14.0f : u.role == "grapple" ? 7.0f
                   : u.role == "phalanx" || u.role == "wall" ? 1.2f : u.role == "slinger" ? 14.0f : u.role == "chant" ? 7.0f
                   : u.role == "researcher" ? 10.0f : u.role == "voidcult" ? 1.2f : 14.0f;
        want = std::min(want, u.range * 0.8f);
        Vector3 to = Vector3Subtract(a.pos, d.pos);
        float dist = Vector3Length(to);
        Vector3 goal = a.pos;
        if (dist > want + 1.5f || !level.Sight(a.pos, Eye(d), linkOpen)) goal = d.pos;
        else if (dist < want - 2.5f && dist > 0.1f) goal = Vector3Add(a.pos, Vector3Scale(Vector3Normalize(to), 3));
        if (u.role == "phalanx" && dist > 0.1f) {
            // three in a line across the street, shields locked: each keeps its own place in the line
            Vector3 side = Vector3Normalize({-to.z, 0, to.x});
            goal = Vector3Add(goal, Vector3Scale(side, ((int)(a.rng % 3) - 1) * 1.6f));
            auto it = F.barks.find("phalanx");
            if (it != F.barks.end() && !it->second.empty() && Rand() < 0.002f) Say(F.speciesName, it->second[(int)(Rand() * it->second.size()) % it->second.size()], 3);
        }
        a.st = State::Investigate; a.goal = goal; a.stateT = 0; a.target = -1;
        return true;
    }
    // no divers in sight: a raiding party spears beasts for meat on the way (and leaves the blood behind)
    if (F.huntsBeasts) {
        if (a.st == State::Hunt && !a.targetCorpse && a.target >= 0 && a.target < (int)eco.agents.size() && eco.agents[a.target].alive && eco.agents[a.target].diver < 0 && a.stateT < 15) return true;
        for (int i = 0; i < (int)eco.agents.size(); i++) {
            const Agent& o = eco.agents[i];
            const Species& os = map->species[o.sp];
            if (!o.alive || o.diver >= 0 || os.isEnemy || os.size < 3 || os.tier > 2 || Vector3Distance(o.pos, a.pos) > 15) continue;
            a.st = State::Hunt; a.target = i; a.targetCorpse = false; a.stateT = 0;
            auto it = F.barks.find("chum");
            if (it != F.barks.end() && !it->second.empty() && Rand() < 0.3f) Say(F.speciesName.empty() ? F.name : F.speciesName, it->second[0], 3);
            return true;
        }
    }
    // no divers in sight: patrol the ship (stern, engine room, cabins, salon)
    if (F.patrol.empty()) return false;
    int& step = enemyPatrol[idx];
    int zi = map->ZoneIndex(F.patrol[step % F.patrol.size()]);
    if (zi < 0) return false;
    Vector3 c = map->zones[zi].Center();
    c.y = map->zones[zi].y0 + 2;
    if (Vector3Distance(a.pos, c) < 3) { step++; }
    a.st = State::Investigate; a.goal = c; a.stateT = 0;
    return true;
}

void Match::EnemiesVsDivers(float dt) {
    for (int i = 0; i < (int)eco.agents.size(); i++) {
        const Agent& a = eco.agents[i];
        if (!a.alive || !map->species[a.sp].isEnemy || a.unit < 0) { enemyTellT.erase(i); continue; }
        const FactionUnit& u = map->faction.units[a.unit];
        float& ft = enemyFireT[i];
        if (ft > 0) ft -= dt;
        if (a.st == State::Flee || a.stun > 0 || a.held > 0) { enemyTellT.erase(i); continue; }
        if (u.role == "sentinel") {
            // the Sentinel's rotary needler: it turns slowly (a 90-degree arc), spins up for 1 s (a rising whine, a red
            // lamp), then fires 3 s bursts; flank it or blow it up
            int tgt = NearestDiver(a.pos, u.range + 0.5f, true);
            float& yaw = sentinelYaw[i]; float& bt = sentinelBurst[i];
            if (tgt >= 0) {
                Vector3 to = Vector3Subtract(divers[tgt].pos, a.pos);
                float want = atan2f(to.x, to.z), diff = remainderf(want - yaw, 6.2832f);
                yaw += std::clamp(diff, -1.0f * dt, 1.0f * dt);      // ~57 degrees a second
                bool inArc = fabsf(diff) < 0.785f;
                if (bt <= 0 && inArc) bt = 4.0f;                        // 1 s spin-up, then 3 s of fire
                if (bt > 0) {
                    bt -= dt;
                    if (bt > 3.0f) { enemyTellT[i] = bt; continue; }
                    if (bt <= 0) { bt = -2.0f; continue; }             // a 2 s cool-down between bursts
                    if (ft <= 0 && inArc) {
                        ft = u.interval;
                        Dart t; t.enemy = i; t.pos = t.start = Vector3Add(a.pos, {0, 0.3f, 0});
                        Vector3 aim = Vector3Normalize(Vector3Subtract(divers[tgt].pos, t.pos));
                        float err = 4.0f * DEG2RAD;
                        aim = Vector3Normalize(Vector3Add(aim, {Rand(-err, err), Rand(-err, err), Rand(-err, err)}));
                        t.vel = Vector3Scale(aim, 30); t.damage = u.damage * (1 + 0.15f * (tide / 5)); t.life = 2; darts.push_back(t);
                        eco.AddNoise(t.pos, 4); fx.push_back({2, t.pos, aim});
                    }
                } else bt = std::min(0.0f, bt + dt);
            } else { enemyTellT.erase(i); if (bt > 0) bt = 0; }
            // a Researcher's torch mends it
            for (const auto& o : eco.agents) if (o.alive && o.unit >= 0 && map->faction.units[o.unit].role == "researcher" && Vector3Distance(o.pos, a.pos) < 3) { Agent& me = eco.agents[i]; me.hp = std::min(me.hpMax, me.hp + 20 * dt); }
            continue;
        }
        int di = NearestDiver(a.pos, u.range + 0.5f, true);
        if (u.role == "voidcult") {
            // the chant: divers within 10 m drift 0.3 m/s toward the nearest overlook
            Vector3 edge{}; float bd = 1e9f;
            for (const auto& l : map->links) if (l.to < (int)voidZone.size() && voidZone[l.to]) { float dd = Vector3Distance(l.a, a.pos); if (dd < bd) { bd = dd; edge = l.a; } }
            if (bd < 1e8f) for (auto& o : divers) if (!o.dead && !o.downed && Vector3Distance(o.pos, a.pos) < 10) o.vel = Vector3Add(o.vel, Vector3Scale(Vector3Normalize(Vector3Subtract(edge, o.pos)), 0.3f * dt * 3));
        }
        if (di < 0) { enemyTellT.erase(i); continue; }
        if (ft > 0) continue;
        // the tell first (a red laser dot, the net swung twice, the winch wound): the diver gets a moment to react
        float tell = u.role == "researcher" ? 0.6f : u.role == "voidcult" ? 0.4f : u.role == "chief" ? 1.2f : u.role == "phalanx" ? 0.5f : u.role == "wall" ? 1.0f : u.role == "slinger" ? 0.7f : u.role == "chant" ? 0.2f : u.role == "priest" ? 99.0f : u.role == "drum" ? 1.0f : u.role == "sling" ? 0.9f : u.role == "leader" ? 2.0f : u.role == "cover" ? 1.0f : u.role == "net" || u.role == "grapple" ? 0.8f : u.role == "chum" || u.role == "bell" ? 0.6f : u.role == "lure" ? 1.0f : 0.3f;
        auto it = enemyTellT.find(i);
        if (it == enemyTellT.end()) { enemyTellT[i] = tell; continue; }
        it->second -= dt;
        if (it->second > 0) continue;
        enemyTellT.erase(it);
        ft = u.interval;
        if (u.role == "slinger") {
            // "targets reloading divers"
            for (auto& o : divers) if (!o.dead && !o.downed && o.reloading && Vector3Distance(o.pos, a.pos) <= u.range && level.Sight(a.pos, Eye(o), linkOpen)) { di = o.slot; break; }
        }
        DiverState& d = divers[di];
        // "better weapons every 5 tides"
        float dmg = u.damage * (1 + 0.15f * (tide / (int)Engine().C("enemy_weapon_tier_every", 5)));
        Vector3 from = Vector3Add(a.pos, {0, 0.3f, 0});
        if (u.role == "researcher") {
            // a gas grenade into the diver's corridor: 15/s in 4 m for 6 s
            Gas g; g.pos = d.pos; g.dps = u.damage; gas.push_back(g);
            fx.push_back({5, d.pos, {0, 0, 0}});
            continue;
        }
        if (u.role == "voidcult") { if (Vector3Distance(a.pos, d.pos) <= u.range + 0.6f) HitDiver(d, dmg, u.unit, "", a.pos); continue; }
        if (u.role == "chief") {
            // the mech: the claw close in (a 3 s hold, then 120), the twin harpoon cannons beyond (200 a bolt)
            if (Vector3Distance(a.pos, d.pos) < 3.5f) { HitDiver(d, 120 * (1 + 0.15f * (tide / 5)), u.unit, "hold 3 s", a.pos); continue; }
        }
        if (u.role == "flank" || u.role == "phalanx") {
            if (Vector3Distance(a.pos, d.pos) <= u.range + 0.6f) HitDiver(d, dmg, u.unit, "", a.pos);
            continue;
        }
        if (u.role == "wall") {
            // the titan shield's crush: 90, a 40% stun
            if (Vector3Distance(a.pos, d.pos) <= u.range + 0.6f) { HitDiver(d, dmg, u.unit, "", a.pos); if (Rand() < 0.4f) d.stunT = std::max(d.stunT, 1.5f); }
            continue;
        }
        if (u.role == "chant") {
            // the void chant: every diver within 10 m swims 30% slower
            for (auto& o : divers) if (!o.dead && !o.downed && Vector3Distance(o.pos, a.pos) <= u.range) o.slowT = std::max(o.slowT, 1.2f);
            fx.push_back({6, Vector3Add(a.pos, {0, 1, 0}), {0, 1, 0}});
            continue;
        }
        if (u.role == "priest") continue;
        if (u.role == "drum") {
            // the Shaman's drum: every beast gets angrier (20 s); the third beat calls the sharks, the fifth the Matriarch
            drumBeats++;
            eco.AddNoise(a.pos, 6);
            eco.cleanerRage = std::max(eco.cleanerRage, 20.0f);
            fx.push_back({5, a.pos, {0, 0, 0}});
            if (drumBeats % 5 == 3) {
                for (auto& o : eco.agents) if (o.alive && o.diver < 0 && (HasW(map->species[o.sp].name, "shark") || HasW(map->species[o.sp].name, "hammerhead")) && !map->species[o.sp].Has("ally")) { o.st = State::Investigate; o.goal = d.pos; o.stateT = 0; }
                Say(map->faction.name, "The third beat: the sharks come", 3);
            }
            if (drumBeats % 5 == 0 && bossAgent >= 0 && !bossActive) {
                bossActive = true; bossProvoked = true; bossIdleT = 0;
                auto it = map->faction.barks.find("foreman");
                Say(map->faction.name, it != map->faction.barks.end() && it->second.size() > 1 ? it->second[1] : "She comes.", 4);
            }
            continue;
        }
        if (u.role == "lure" || u.role == "bell") {
            // the Lantern Bearer's light draws the curious and the luminous onto whoever stands near it; the Bell
            // Ringer rings and every beast in 30 m investigates the diver it points at
            float r = u.role == "bell" ? 30.0f : 25.0f;
            for (auto& o : eco.agents) {
                if (!o.alive || o.diver >= 0 || map->species[o.sp].isEnemy || IsBoss((int)(&o - &eco.agents[0]))) continue;
                const Species& os = map->species[o.sp];
                if (u.role == "lure" && os.curiosity < 0.4f && !os.Has("luminous")) continue;
                if (os.size < 2 || Vector3Distance(o.pos, a.pos) > r) continue;
                o.st = State::Investigate; o.goal = d.pos; o.stateT = 0;
            }
            if (u.role == "bell") {
                eco.AddNoise(a.pos, 8);
                auto it = map->faction.barks.find("bell");
                if (it != map->faction.barks.end() && !it->second.empty() && Rand() < 0.5f) Say(map->faction.name, it->second[(int)(Rand() * it->second.size()) % it->second.size()], 3);
            }
            fx.push_back({4, a.pos, {0, 0, 0}});
            continue;
        }
        Dart t;
        t.enemy = i;
        t.pos = t.start = from;
        Vector3 aim = Vector3Normalize(Vector3Subtract(Vector3Add(d.pos, Vector3Scale(d.vel, Vector3Distance(from, d.pos) / 28.0f)), from));
        float err = 2.5f * DEG2RAD;
        aim = Vector3Normalize(Vector3Add(aim, {Rand(-err, err), Rand(-err, err), Rand(-err, err)}));
        t.vel = Vector3Scale(aim, u.role == "leader" ? 40.0f : u.role == "cover" ? 30.0f : 18.0f);
        t.damage = dmg;
        t.kind = u.role == "net" ? 5 : u.role == "chum" ? 6 : u.role == "grapple" ? 7 : u.role == "sling" ? 8 : 0;
        if (t.kind == 8) { t.vel = Vector3Add(Vector3Scale(aim, 12.0f), {0, 3.0f, 0}); }   // a coconut bomb, lobbed
        t.life = 3;
        darts.push_back(t);
        eco.AddNoise(from, u.role == "leader" ? 10.0f : u.role == "cover" ? 3.0f : 2.0f);
        fx.push_back({2, from, aim});
    }
}

// ---------------------------------------------------------------- the Goliath (boss sheet)
bool Match::DecideBoss(Agent& a, int idx) {
    (void)idx;
    int di = NearestDiver(a.pos, 40.0f, true);
    if (di < 0) return false;
    Vector3 goal = divers[di].pos;
    if (bossWind >= 0 || bossInhaleT >= 0) goal = a.pos;     // it holds still to wind up, and while it inhales
    else if (bossPhase == 1) {
        // "Stays within 12 m of its home"
        Vector3 off = Vector3Subtract(goal, a.home);
        if (Vector3Length(off) > 12) goal = Vector3Add(a.home, Vector3Scale(Vector3Normalize(off), 12));
    } else if (bossPhase == 2) goal = map->zones[a.homeZone].Clamp(goal, 1.5f);   // "Roams the Engine Room"
    if (bossKind == 1) {
        // the Lobster patrols the Cathedral floor; in phase 2 it takes to the walls and ceiling; in phase 3 it roams
        // wherever the slipstream left it
        const Zone& z = map->zones[bossPhase >= 3 ? a.zone : a.homeZone];
        goal = z.Clamp(goal, 1.5f);
        goal.y = bossPhase == 2 ? z.y1 - 2.0f : z.y0 + 1.5f;
        if (bossWind >= 0) goal = a.pos;
    } else if (bossKind == 2) {
        // the Matriarch keeps to open water (the Wall and the Blue Hole; the Flats and Lagoon from tide 12); in phase 3
        // she breaches into the Lagoon shallows; she rises to the surface to breathe
        int lag = map->ZoneIndex("The Lagoon");
        const Zone& z = map->zones[bossPhase >= 3 && lag >= 0 ? lag : a.zone];
        if (bossPhase >= 3 && lag >= 0 && a.zone != lag) goal = z.Center();
        else goal = z.Clamp(goal, 1.5f);
        if (bossGillsT > 0 && breathT < 5) goal.y = z.y1 - 1.0f;
        if (bossWind >= 0 && bossWind != 1) goal = a.pos;
    } else if (bossKind != 0) goal = map->zones[bossPhase >= 3 ? a.zone : a.homeZone].Clamp(goal, 1.5f);
    a.st = State::Investigate; a.goal = goal; a.stateT = 0; a.target = -1; a.fedT = 0;
    return true;
}

void Match::UpdateBoss(float dt) {
    if (bossAgent < 0 || bossAgent >= (int)eco.agents.size() || !eco.agents[bossAgent].alive) {
        bossAgent = -1; bossActive = false;
        for (int i = 0; i < (int)eco.agents.size(); i++) if (eco.agents[i].alive && map->species[eco.agents[i].sp].tier == 5) { bossAgent = i; break; }
        if (bossAgent < 0) return;
    }
    if (bossKind == 3) { UpdateBossWyrm(dt); return; }
    Agent& b = eco.agents[bossAgent];
    const Species& s = map->species[b.sp];
    float frac = b.hp / std::max(1.0f, b.hpMax);
    int ph = frac > 0.6f ? 1 : frac > 0.3f ? 2 : 3;
    // it wakes when a diver comes within 5 m, shoots it, or lingers in its room (25 s; less when it's hungry, never
    // while it's digesting: "the Forge is safe only right after it has fed"); from tide 12 it wanders and hunts
    int near = -1; float nd = 1e9f;
    bool inRoom = false;
    for (const auto& d : divers) {
        if (d.dead || d.downed) continue;
        float dist = Vector3Distance(d.pos, b.pos);
        bool sees = level.Sight(b.pos, Eye(d), linkOpen);
        if (d.zone == b.homeZone) inRoom = true;
        if ((d.zone == b.zone || bossActive) && sees && dist < nd) { nd = dist; near = d.slot; }
    }
    bossLingerT = inRoom ? bossLingerT + dt : std::max(0.0f, bossLingerT - dt * 0.5f);
    bool provoked = bossProvoked;                             // a diver's shot, blade or blast landed on it
    float patience = b.fedT > 0 ? 1e9f : 25.0f * (1.2f - b.hunger);
    // before the power is on it dozes: only a shot or a touch wakes it ("turning on power also wakes the
    // ecosystem's larger predators, so the choice of when to do it is real")
    float wakeAt = power ? 5.0f : 1.2f;
    if (!power) patience = 1e9f;
    float snout = nd - bodies[b.sp].length * 0.5f;
    bool wake = snout < wakeAt || provoked || bossLingerT > patience || (tide >= 12 && nd < 30);
    if (bossKind != 0) {
        // the other bosses wake to a diver in their arena (the Lobster ignores divers on the overlooks), a shot, or a call
        // (the crystal coral rung); from tide 12 they wander
        bool inArena = near >= 0 && divers[near].zone == b.homeZone && nd < 22;
        wake = inArena || provoked || bossRearReq || (tide >= 12 && nd < 30);
    }
    if (!bossActive && near >= 0 && wake) {
        bossActive = true; bossIdleT = 0;
        if (getenv("DEPTH_HITLOG")) printf("   [boss] t=%.1f wakes: snout %.1f provoked %d linger %.1f/%.1f power %d tide %d\n", time, snout, (int)provoked, bossLingerT, patience, (int)power, tide);
        Say(s.name, bossKind == 0 ? "wakes in the engine room's shadows" : "stirs in " + map->zones[b.homeZone].name, 4);
    }
    if (!bossActive) return;
    if (near < 0 || nd > 34) { bossIdleT += dt; if (bossIdleT > 20) { bossActive = false; bossProvoked = false; bossLingerT = 0; b.st = State::Return; b.goal = b.home; return; } }
    else bossIdleT = 0;
    if (bossKind == 1) { UpdateBossLobster(dt, near, nd); return; }
    if (bossKind == 2) { UpdateBossMatriarch(dt, near, nd); return; }
    if (bossKind == 4) { UpdateBossLeviathan(dt, near, nd); return; }
    if (bossKind != 0) { UpdateBossGeneric(dt, near, nd); return; }
    if (ph != bossPhase) {
        bossPhase = ph;
        if (ph == 2) Say(s.name, "thrashes: the cargo comes loose", 4);
        if (ph == 3) {
            Say(s.name, "ENRAGED: it breaks the aft bulkhead", 5);
            for (int li = 0; li < (int)map->links.size(); li++) {
                const Link& l = map->links[li];
                if ((l.from == b.homeZone || l.to == b.homeZone) && HasW(l.passage, "aft")) { linkOpen[li] = 1; level.doors[li].open = true; }
            }
        }
    }
    // phase 2 "calls 2 Groupers"
    if (ph >= 2 && !bossCalled) {
        bossCalled = true;
        int gi = map->SpeciesIndex("Grouper");
        if (gi >= 0) for (int k = 0; k < 2; k++) {
            Vector3 home = b.pos; int hz = b.homeZone;
            int n = eco.Spawn(gi, map->zones[hz].Clamp(Vector3Add(home, {Rand(-3, 3), 0, Rand(-3, 3)})), hz);
            Agent& g = eco.agents[n];
            if (near >= 0) { g.st = State::Hunt; g.target = divers[near].agent; g.hunger = 1; }
        }
    }
    Agent& B = eco.agents[bossAgent];   // (Spawn may have moved the vector)
    for (float& c : bossCd) if (c > 0) c -= dt;
    if (bossGillsT > 0) bossGillsT -= dt;
    if (ph == 3 && B.stun <= 0 && B.held <= 0) B.pos = map->zones[B.zone].Clamp(Vector3Add(B.pos, Vector3Scale(B.vel, dt * 0.5f)), 1.5f);   // speed x1.5
    // an inhale in progress: 5 s to deal 150 to the gills, or the diver is swallowed
    if (bossInhaleT >= 0) {
        bossInhaleT -= dt;
        if (bossInhaleDiver >= 0 && bossInhaleDiver < (int)divers.size()) {
            DiverState& d = divers[bossInhaleDiver];
            Vector3 mouth = Vector3Add(B.pos, Vector3Scale(Facing(B), bodies[B.sp].length * 0.5f));
            Vector3 np = Vector3Lerp(d.pos, mouth, std::min(1.0f, dt * 2));
            if (level.Inside(np, 0.3f, linkOpen)) d.pos = np;
            d.vel = {0, 0, 0};
            if (bossGillDmg >= 150) {
                bossInhaleT = -1; bossInhaleDiver = -1;
                bossGillsT = 2; B.stun = 1;
                d.vel = Vector3Scale(Vector3Normalize(Vector3Subtract(d.pos, B.pos)), 6);
                Say(s.name, "chokes and spits the diver out: the gills are open!", 3);
            } else if (bossInhaleT <= 0 || d.downed || d.dead) {
                bossInhaleT = -1; bossInhaleDiver = -1; bossGillsT = 2;
                if (!d.downed && !d.dead) DownDiver(d, s.name + " (swallowed)");
            }
        } else bossInhaleT = -1;
        return;
    }
    if (B.stun > 0 || B.held > 0) { bossWind = -1; return; }
    if (near < 0) return;
    DiverState& T = divers[near];
    float dist = Vector3Distance(B.pos, T.pos);
    float reach = bodies[B.sp].length * 0.5f;       // measured from its snout, not its middle
    auto attack = [&](const char* name) -> const Attack* {
        for (int k : attacksBySp[B.sp]) if (HasW(map->attacks[k].name, name)) return &map->attacks[k];
        return nullptr;
    };
    const Attack* inhale = attack("inhale"); const Attack* lunge = attack("lunge"); const Attack* boom = attack("boom"); const Attack* slam = attack("slam");
    float dm = map->Tide(tide).dmgMult;
    if (bossWind >= 0) {
        bossWindT -= dt;
        if (bossWindT > 0) return;
        int k = bossWind;
        bossWind = -1;
        DiverState& t = divers[std::clamp(bossTarget, 0, (int)divers.size() - 1)];
        float td = Vector3Distance(B.pos, t.pos) - reach;
        if (k == 0 && inhale && !t.downed && !t.dead && td <= inhale->range + 0.5f) {
            bossInhaleT = 5; bossInhaleDiver = t.slot; bossGillDmg = 0; bossInhales++;
            Say(s.name, "INHALES a diver: shoot the gills!", 4);
        } else if (k == 1 && lunge && td <= lunge->range) HitDiver(t, lunge->damage * dm, s.name, lunge->effect, B.pos, bossAgent);
        else if (k == 2 && boom) {
            eco.AddNoise(B.pos, 10, true);
            fx.push_back({5, B.pos, {0, 0, 0}});
            for (auto& d : divers) if (!d.dead && !d.downed && Vector3Distance(d.pos, B.pos) - reach <= boom->range) HitDiver(d, boom->damage * dm, s.name, "stun 1 s", B.pos);
        } else if (k == 3 && slam) {
            for (auto& d : divers) if (!d.dead && !d.downed && Vector3Distance(d.pos, B.pos) - reach <= slam->range) HitDiver(d, slam->damage * dm, s.name, "knockback 6 m", B.pos);
            bossGillsT = 2;
            // "drops loose cargo: 3 crates fall on marked squares"
            for (int c = 0; c < 3; c++) {
                const DiverState& d = divers[(near + c) % divers.size()];
                Crate cr; cr.pos = map->zones[B.homeZone].Clamp(Vector3Add(d.pos, {Rand(-2.5f, 2.5f), 0, Rand(-2.5f, 2.5f)}), 0.6f); cr.t = 1.5f;
                crates.push_back(cr);
            }
        }
        return;
    }
    float d0 = dist - reach;
    int pick = -1;
    if (inhale && d0 <= inhale->range && bossCd[0] <= 0) pick = 0;
    else if (slam && ph >= 2 && d0 <= slam->range && bossCd[3] <= 0) pick = 3;
    else if (lunge && d0 <= lunge->range && bossCd[1] <= 0) pick = 1;
    else if (boom && d0 <= boom->range && bossCd[2] <= 0) pick = 2;
    if (pick < 0) return;
    const Attack* at = pick == 0 ? inhale : pick == 1 ? lunge : pick == 2 ? boom : slam;
    bossWind = pick; bossWindT = at->windup; bossTarget = near;
    bossCd[pick] = pick == 0 ? (ph == 3 ? 8.0f : at->cooldown) : pick == 2 ? (ph >= 2 ? 3.0f : at->cooldown * 2) : at->cooldown;
    if (!at->tell.empty() && pick == 0) Say(s.name, at->tell, 2);
}


// ---------------------------------------------------------------- map mechanics (extra.json)
std::string Match::WonderId() const { return map->extra["wonder"].Str0(Weapons().wonder); }

// Stalactites (the Cave's rockfall and trap, the Lobster's Clack): each falls on a marked spot after `delay` and hits
// whatever stands under it (a column: height doesn't matter).
void Match::DropRocks(Vector3 at, int n, float spread, float dmg, float radius, float delay, int owner) {
    int zi = eco.ZoneAt(at);
    if (zi < 0) zi = 0;
    const Zone& z = map->zones[zi];
    for (int k = 0; k < n; k++) {
        Crate c;
        c.kind = 1;
        Vector3 p = k == 0 && n == 1 ? at : Vector3Add(at, {Rand(-spread, spread), 0, Rand(-spread, spread)});
        c.pos = z.Clamp({p.x, z.y0 + 0.6f, p.z}, 0.8f);
        c.top = z.y1 - 0.5f;
        c.t = delay + k * 0.12f; c.dmg = dmg; c.radius = radius; c.owner = owner;
        crates.push_back(c);
    }
}

// The Resonator: a sonic cone that kills small beasts outright, knocks large ones back, and shatters stalactites.
void Match::FireCone(DiverState& d, const WeaponDef& w, float dmg) {
    Vector3 eye = Eye(d), dir = Forward(d);
    float half = w.coneDeg * 0.5f * DEG2RAD;
    if (w.wave) {
        // the Tide Staff: a wave that carries what it hits 8 m along it (into a trap, off a diver); Forged, it drowns
        // the Lost Ones it throws
        bool forged = Cur(d).forged;
        for (int i = 0; i < (int)eco.agents.size(); i++) {
            Agent& a = eco.agents[i];
            if (!a.alive || a.diver >= 0) continue;
            Vector3 to = Vector3Subtract(a.pos, eye);
            float dist = Vector3Length(to);
            if (dist > w.cone || dist < 0.01f) continue;
            if (acosf(std::clamp(Vector3DotProduct(to, dir) / dist, -1.0f, 1.0f)) > half) continue;
            if (!level.Sight(eye, a.pos, linkOpen, true)) continue;
            bool enemy = map->species[a.sp].isEnemy;
            if (!IsBoss(i)) {
                a.pos = map->zones[a.zone].Clamp(Vector3Add(a.pos, Vector3Scale(dir, 8)), 0.6f);
                a.stun = std::max(a.stun, 1.5f); a.target = -1;
                if (a.unit >= 0) shieldHP[i] = 0;
            }
            HitAgent(&d, i, dmg + (forged && enemy ? 150.0f : 0.0f), false, false, dir, nullptr);
        }
        if (forged) eco.AddBlood(Vector3Add(eye, Vector3Scale(dir, w.cone * 0.8f)), 10);
        fx.push_back({10, Vector3Add(eye, Vector3Scale(dir, 1.0f)), Vector3Scale(dir, w.cone)});
        return;
    }
    for (int i = 0; i < (int)eco.agents.size(); i++) {
        Agent& a = eco.agents[i];
        if (!a.alive || a.diver >= 0) continue;
        Vector3 to = Vector3Subtract(a.pos, eye);
        float dist = Vector3Length(to);
        if (dist > w.cone || dist < 0.01f) continue;
        if (acosf(std::clamp(Vector3DotProduct(to, dir) / dist, -1.0f, 1.0f)) > half) continue;
        if (!level.Sight(eye, a.pos, linkOpen, true)) continue;
        const Species& sp = map->species[a.sp];
        if (sp.size <= 2) HitAgent(&d, i, a.hp + 1, false, false, dir, nullptr);
        else {
            HitAgent(&d, i, dmg, false, false, dir, nullptr);
            if (i < (int)eco.agents.size() && eco.agents[i].alive && !IsBoss(i)) {
                Agent& b = eco.agents[i];
                b.pos = map->zones[b.zone].Clamp(Vector3Add(b.pos, Vector3Scale(Vector3Normalize(to), 3)), 0.5f);
                b.stun = std::max(b.stun, 1.0f);
            }
        }
    }
    fx.push_back({10, Vector3Add(eye, Vector3Scale(dir, 1.0f)), Vector3Scale(dir, w.cone)});
    // in the rockfall halls the ring brings the ceiling down where it points
    if (d.zone >= 0 && d.zone < (int)rockZone.size() && rockZone[d.zone]) {
        Vector3 p = eye;
        for (float t = 0; t < w.cone; t += 0.5f) { Vector3 q = Vector3Add(eye, Vector3Scale(dir, t)); if (!level.Inside(q, 0.1f, linkOpen, true)) break; p = q; }
        DropRocks(p, 2, 2.0f, map->extra["rockfall"]["damage"].F(200), 1.6f, 0.8f, d.slot);
    }
}

// A dart into a flora patch the map lists as a tool: Bloodvine smells of blood, an ink cap bursts, crystal coral rings.
void Match::FloraTool(int pi, Vector3 at, DiverState* d) {
    FloraPatch& p = eco.flora[pi];
    const std::string& n = map->flora[p.flora].name;
    (void)d;
    if (HasW(n, "bloodvine")) {
        eco.AddBlood(at, 40);                                    // "cut or shot: smells like 40 blood for 30 s"
        floraBleed[pi] = 30;
        Say("", "The Bloodvine weeps: every predator nearby smells it", 3);
    } else if (HasW(n, "ink cap")) {
        InkBurst(at, d ? d->slot : -1);
        for (int i = 0; i < (int)eco.agents.size(); i++) {
            Agent& a = eco.agents[i];
            if (!a.alive || a.diver >= 0 || Vector3Distance(a.pos, at) > 5) continue;
            a.stun = std::max(a.stun, IsBoss(i) ? 5.0f : 3.0f);  // blinded in the ink (the Lobster for 5 s)
        }
        p.units = 0; p.regrowT = 0;
    } else if (HasW(n, "crystal coral")) {
        eco.AddNoise(at, 6);                                     // "shooting a cluster rings it: calls the Lobster and makes it Rear"
        fx.push_back({6, at, {0, 1.5f, 0}});
        bossRearReq = true;
        Say("", "The crystal coral rings through the Cathedral", 3);
    }
}

// The Lobster (the Cave's boss sheet): Crush, Sweep, Clack, Rear; the walls and ceiling in phase 2 with Troglobite
// Crabs from the crystal; in phase 3 it rides slipstream E backward to the Dynamo Sump and cuts the power.
void Match::UpdateBossLobster(float dt, int near, float nd) {
    Agent& b = eco.agents[bossAgent];
    const Species& s = map->species[b.sp];
    float frac = b.hp / std::max(1.0f, b.hpMax);
    int ph = frac > 0.6f ? 1 : frac > 0.3f ? 2 : 3;
    if (ph != bossPhase) {
        bossPhase = ph;
        if (ph == 2) {
            Say(s.name, "climbs onto the walls: the crystal spits out crabs", 4);
            int ci = map->SpeciesIndex("Troglobite Crab");
            if (ci >= 0) for (int k = 0; k < 4; k++) {
                int hz = b.homeZone;
                int n = eco.Spawn(ci, map->zones[hz].Clamp(Vector3Add(b.pos, {Rand(-4, 4), 0, Rand(-4, 4)})), hz);
                if (near >= 0) { eco.agents[n].st = State::Hunt; eco.agents[n].target = divers[near].agent; }
            }
        }
        if (ph == 3 && !bossMoved) {
            // "rides slipstream E backward into the Dynamo Sump and cuts the power"
            for (const auto& l : map->links) if (l.slip && l.to == eco.agents[bossAgent].zone) {
                Agent& B = eco.agents[bossAgent];
                B.pos = map->zones[l.from].Clamp(l.a, 2.0f); B.zone = l.from;
                bossMoved = true;
                break;
            }
            if (power) { power = false; Say("", "The generator dies: the Forge and the traps go dark until the switch is reset", 5); }
            Say(s.name, "rides the slipstream backward into the dark", 4);
        }
    }
    Agent& B = eco.agents[bossAgent];
    for (float& c : bossCd) if (c > 0) c -= dt;
    if (bossGillsT > 0) bossGillsT -= dt;
    bossRecentDmg *= powf(0.5f, dt / 2.5f);                      // (about the last 5 s of damage)
    if (ph < 3 && bossRecentDmg > 800) { bossRearReq = true; bossRecentDmg = 0; }   // "or after taking 800 damage in 5 s"
    if (B.stun > 0 || B.held > 0) { bossWind = -1; return; }
    auto attack = [&](const char* name) -> const Attack* { for (int k : attacksBySp[B.sp]) if (HasW(map->attacks[k].name, name)) return &map->attacks[k]; return nullptr; };
    const Attack* crush = attack("crush"); const Attack* sweep = attack("sweep"); const Attack* clack = attack("clack"); const Attack* rear = attack("rear");
    float dm = map->Tide(tide).dmgMult, reach = bodies[B.sp].length * 0.5f;
    if (bossWind >= 0) {
        bossWindT -= dt;
        if (bossWindT > 0) return;
        int k = bossWind; bossWind = -1;
        DiverState& t = divers[std::clamp(bossTarget, 0, (int)divers.size() - 1)];
        float td = Vector3Distance(B.pos, t.pos) - reach;
        if (k == 0 && rear) { bossGillsT = 3; Say(s.name, "rears: the underside is open!", 3); }
        else if (k == 1 && crush && td <= crush->range + 0.5f && !t.downed && !t.dead) HitDiver(t, crush->damage * dm, s.name, "hold 2 s", B.pos, bossAgent);
        else if (k == 2 && sweep) { for (auto& d : divers) if (!d.dead && !d.downed && Vector3Distance(d.pos, B.pos) - reach <= sweep->range) HitDiver(d, sweep->damage * dm, s.name, "knockback 6 m", B.pos); }
        else if (k == 3 && clack) {
            eco.AddNoise(B.pos, 10, true);
            fx.push_back({5, B.pos, {0, 0, 0}});
            for (auto& d : divers) if (!d.dead && !d.downed && Vector3Distance(d.pos, B.pos) - reach <= clack->range) HitDiver(d, clack->damage * dm, s.name, "stun 1 s", B.pos);
            if (ph >= 2) DropRocks(t.pos, ph == 3 ? 3 : 1, 2.5f, map->extra["rockfall"]["damage"].F(200), 1.6f, 1.5f, -1);
        }
        return;
    }
    if (bossRearReq && rear && bossCd[0] <= 0) {
        bossRearReq = false;
        bossWind = 0; bossWindT = rear->windup; bossCd[0] = 4;
        return;
    }
    if (near < 0) return;
    float d0 = nd - reach;
    int pick = -1;
    if (crush && d0 <= crush->range && bossCd[1] <= 0) pick = 1;
    else if (sweep && d0 <= sweep->range && bossCd[2] <= 0) pick = 2;
    else if (clack && d0 <= clack->range && bossCd[3] <= 0) pick = 3;
    if (pick < 0) return;
    const Attack* at = pick == 1 ? crush : pick == 2 ? sweep : clack;
    bossWind = pick; bossWindT = at->windup; bossTarget = near; bossCd[pick] = std::max(1.0f, at->cooldown);
}

// Any other boss: its sheet's attacks by range, their effects through HitDiver, a weak window after its slowest one.
void Match::UpdateBossGeneric(float dt, int near, float nd) {
    Agent& B = eco.agents[bossAgent];
    const Species& s = map->species[B.sp];
    float frac = B.hp / std::max(1.0f, B.hpMax);
    bossPhase = frac > 0.6f ? 1 : frac > 0.3f ? 2 : 3;
    for (float& c : bossCd) if (c > 0) c -= dt;
    if (bossGillsT > 0) bossGillsT -= dt;
    if (B.stun > 0 || B.held > 0 || near < 0) { bossWind = -1; return; }
    const auto& atk = attacksBySp[B.sp];
    float reach = bodies[B.sp].length * 0.5f, dm = map->Tide(tide).dmgMult;
    if (bossWind >= 0) {
        bossWindT -= dt;
        if (bossWindT > 0) return;
        const Attack& at = map->attacks[atk[bossWind]];
        bossWind = -1;
        for (auto& d : divers) {
            if (d.dead || d.downed || Vector3Distance(d.pos, B.pos) - reach > at.range + 0.5f) continue;
            if (at.range < 4 && d.slot != bossTarget) continue;   // short reaches hit one diver, the long ones everyone in range
            HitDiver(d, at.damage * dm, s.name, at.effect, B.pos, bossAgent);
        }
        if (at.windup >= 0.8f) bossGillsT = 2;
        return;
    }
    for (int k = 0; k < (int)atk.size() && k < 4; k++) {
        const Attack& at = map->attacks[atk[k]];
        if (bossCd[k] > 0 || nd - reach > at.range || at.damage <= 0) continue;
        bossWind = k; bossWindT = at.windup; bossTarget = near; bossCd[k] = std::max(1.5f, at.cooldown);
        return;
    }
}


// ---------------------------------------------------------------- the Reef
std::string Match::ArtName(int sp) const { return map->species[sp].art.empty() ? map->species[sp].name : map->species[sp].art; }

void Match::EnemyBlast(Vector3 at, float dmg, float radius, int enemy) {
    fx.push_back({3, at, {0, 0, 0}});
    eco.AddNoise(at, 7, true);
    std::string by = enemy >= 0 && enemy < (int)eco.agents.size() && eco.agents[enemy].unit >= 0 ? map->faction.units[eco.agents[enemy].unit].unit : "a blast";
    for (auto& d : divers) if (!d.dead && Vector3Distance(d.pos, at) < radius + 0.4f) HitDiver(d, dmg * map->Tide(tide).dmgMult, by, "knockback 2 m", at);
    for (int i = 0; i < (int)eco.agents.size(); i++) {
        const Agent& a = eco.agents[i];
        if (a.alive && a.diver < 0 && i != enemy && Vector3Distance(a.pos, at) < radius + bodies[a.sp].radius) eco.Damage(i, dmg, enemy);   // friendly fire too
    }
}

// The Shaman's drum in a diver's hands: the beasts within 40 m turn on the nearest Raider; the fifth beat calls her.
void Match::BeatDrum(int di) {
    DiverState& d = divers[di];
    if (d.dead || d.downed || d.drumUses <= 0) return;
    d.drumUses--;
    if (d.drumClean) { d.drumClean = false; Say("", "The drum has been beaten: the altar won't take it now", 3); }
    drumBeats++;
    eco.AddNoise(d.pos, 6);
    fx.push_back({5, d.pos, {0, 0, 0}});
    int raider = -1; float rd = 40;
    for (int i = 0; i < (int)eco.agents.size(); i++) if (eco.agents[i].alive && map->species[eco.agents[i].sp].isEnemy && Vector3Distance(eco.agents[i].pos, d.pos) < rd) { rd = Vector3Distance(eco.agents[i].pos, d.pos); raider = i; }
    for (int i = 0; i < (int)eco.agents.size(); i++) {
        Agent& a = eco.agents[i];
        const Species& sp = map->species[a.sp];
        if (!a.alive || a.diver >= 0 || sp.isEnemy || sp.size < 3 || IsBoss(i) || Vector3Distance(a.pos, d.pos) > 40) continue;
        if (raider >= 0) { a.st = State::Hunt; a.target = raider; a.targetCorpse = false; a.stateT = 0; }
        else { a.st = State::Investigate; a.goal = d.pos; a.stateT = 0; }   // (with no Raider near, they come to the drummer)
    }
    if (drumBeats % 5 == 0 && bossAgent >= 0 && !bossActive) { bossActive = true; bossProvoked = true; bossIdleT = 0; Say("", "The fifth beat: the sea's mother hears the drum", 4); }
    else Say("", raider >= 0 ? "The drum turns the reef on the Raiders" : "The drum carries across the reef...", 2);
}

void Match::UpdateReef(float dt) {
    // the tide turns the set every few minutes (ebb: Lagoon -> Forest -> Flats -> Wall; the flood runs back)
    float period = map->extra["tide_flow_period"].F(0);
    if (period > 0) {
        tideTurnT += dt;
        if (tideTurnT >= period) { tideTurnT = 0; eco.flowSign = -eco.flowSign; Say("The tide mill", eco.flowSign > 0 ? "turns: the ebb runs out to the Wall" : "turns: the flood runs in to the Lagoon", 4); }
    }
    // Reacher coral grabs beasts and Raiders that brush it and feeds on them
    const Json& fr = map->extra["flora_rules"];
    if (fr.IsObj()) {
        for (auto it = reacherPrey.begin(); it != reacherPrey.end();) {
            it->second -= dt;
            if (it->first < (int)eco.agents.size() && eco.agents[it->first].alive) eco.agents[it->first].held = std::max(eco.agents[it->first].held, 0.2f);
            if (it->second <= 0) { if (it->first < (int)eco.agents.size() && eco.agents[it->first].alive) eco.Kill(it->first, -1); it = reacherPrey.erase(it); }
            else ++it;
        }
        reefScanT += dt;
        if (reefScanT > 0.5f) {
            reefScanT = 0;
            for (int pi = 0; pi < (int)eco.flora.size(); pi++) {
                const FloraPatch& fp = eco.flora[pi];
                if (fp.units <= 0 || !fr[map->flora[fp.flora].name]["grab"].Bool0()) continue;
                for (int i = 0; i < (int)eco.agents.size(); i++) {
                    const Agent& a = eco.agents[i];
                    if (!a.alive || a.diver >= 0 || IsBoss(i) || map->species[a.sp].size > 3 || reacherPrey.count(i)) continue;
                    if (Vector2Distance({a.pos.x, a.pos.z}, {fp.pos.x, fp.pos.z}) < 1.5f && fabsf(a.pos.y - fp.pos.y) < 2.5f) reacherPrey[i] = 3.0f;
                }
            }
        }
    }
    // the Anemone Gun's rooted polyps grab what passes
    for (auto& p : polyps) {
        p.t -= dt; if (p.cd > 0) p.cd -= dt;
        if (p.cd > 0) continue;
        for (int i = 0; i < (int)eco.agents.size(); i++) {
            Agent& a = eco.agents[i];
            if (!a.alive || a.diver >= 0 || a.held > 0 || Vector3Distance(a.pos, p.pos) > 1.4f) continue;
            const Species& sp = map->species[a.sp];
            a.held = sp.size <= 3 ? 4.0f : 2.0f;
            DiverState* o = p.owner >= 0 && p.owner < (int)divers.size() ? &divers[p.owner] : nullptr;
            HitAgent(o, i, p.forged ? 120.0f : 60.0f, false, false, {0, -1, 0}, nullptr);
            p.cd = 2;
            break;
        }
    }
    polyps.erase(std::remove_if(polyps.begin(), polyps.end(), [](const Polyp& p) { return p.t <= 0; }), polyps.end());
}

// The Matriarch (the Reef's boss sheet): the pod hunts as one and herds; she Rams from afar, Bites and carries, Tail
// Slaps; she breathes at the surface every 90 s (the blowhole open 4 s); with two of the pod dead she charges in straight
// lines; at 30% she rams the Bommie (the cleaning station collapses: every host angrier) and breaches into the Lagoon.
void Match::UpdateBossMatriarch(float dt, int near, float nd) {
    Agent& b = eco.agents[bossAgent];
    const Species& s = map->species[b.sp];
    int podSp = -1;
    for (int i = 0; i < (int)map->species.size(); i++) if (map->species[i].Has("pod")) podSp = i;
    if (!podSpawned && podSp >= 0) {
        podSpawned = true;
        int hz = b.zone;
        for (int k = 0; k < 3; k++) { int n = eco.Spawn(podSp, map->zones[hz].Clamp(Vector3Add(eco.agents[bossAgent].pos, {Rand(-6, 6), Rand(-2, 2), Rand(-6, 6)}), 1.5f), hz); pod.push_back(n); }
        Say(s.name, "comes up the Wall with her pod", 4);
    }
    Agent& B = eco.agents[bossAgent];
    int podDead = 0; for (int i : pod) if (!eco.agents[i].alive) podDead++;
    float frac = B.hp / std::max(1.0f, B.hpMax);
    int ph = frac <= 0.3f ? 3 : (frac <= 0.6f || podDead >= 2) ? 2 : 1;
    if (ph != bossPhase) {
        bossPhase = ph;
        if (ph == 2) Say(s.name, "charges in straight lines now", 4);
        if (ph == 3) {
            // she rams the Bommie: the cleaning station collapses, every host on the map is angrier
            eco.cleanerRage = 300;
            Say(s.name, "rams the Bommie: the cleaning station collapses; every host on the reef is angry", 6);
            for (auto& o : eco.agents) if (o.alive && map->species[o.sp].Cleaner() && map->zones[o.zone].name == "The Bommie") eco.Kill((int)(&o - &eco.agents[0]), -1);
        }
    }
    for (float& c : bossCd) if (c > 0) c -= dt;
    if (bossGillsT > 0) bossGillsT -= dt;
    // breath: every 90 s she surfaces for 4 s (the blowhole)
    breathT += dt;
    if (breathT > 90) { breathT = 0; bossGillsT = 4; Say(s.name, "surfaces to breathe: the blowhole is open", 3); }
    if (B.stun > 0 || B.held > 0) { bossWind = -1; return; }
    auto attack = [&](const char* name) -> const Attack* { for (int k : attacksBySp[B.sp]) if (HasW(map->attacks[k].name, name)) return &map->attacks[k]; return nullptr; };
    const Attack* ram = attack("ram"); const Attack* bite = attack("bite"); const Attack* slap = attack("slap"); const Attack* herd = attack("herd");
    float dm = map->Tide(tide).dmgMult, reach = bodies[B.sp].length * 0.5f;
    if (bossWind >= 0) {
        bossWindT -= dt;
        DiverState& t = divers[std::clamp(bossTarget, 0, (int)divers.size() - 1)];
        if (bossWind == 1 && ram) {
            // the charge: straight at where the diver was
            Vector3 to = Vector3Subtract(t.pos, B.pos);
            float len = Vector3Length(to);
            if (len > reach + 1) B.pos = map->zones[B.zone].Clamp(Vector3Add(B.pos, Vector3Scale(to, std::min(1.0f, dt * (ph >= 2 ? 16.0f : 10.0f) / std::max(0.1f, len)))), 1.5f);
        }
        if (bossWindT > 0) return;
        int k = bossWind; bossWind = -1;
        float td = Vector3Distance(B.pos, t.pos) - reach;
        if (k == 1 && ram && td <= 2.5f) HitDiver(t, ram->damage * dm, s.name, ram->effect, B.pos, bossAgent);
        else if (k == 2 && bite && td <= bite->range + 0.5f) HitDiver(t, bite->damage * dm, s.name, "hold 3 s; carries a diver 15 m", B.pos, bossAgent);
        else if (k == 3 && slap) { for (auto& d : divers) if (!d.dead && !d.downed && Vector3Distance(d.pos, B.pos) - reach <= slap->range) HitDiver(d, slap->damage * dm, s.name, "knockback 6 m", B.pos); }
        else if (k == 4) {
            // Herd: the pod fans wide and pushes divers off cover toward her
            for (int pi : pod) {
                if (!eco.agents[pi].alive) continue;
                for (auto& d : divers) if (!d.dead && !d.downed && Vector3Distance(d.pos, eco.agents[pi].pos) < 10) {
                    Vector3 toHer = Vector3Normalize(Vector3Subtract(B.pos, d.pos));
                    d.vel = Vector3Add(d.vel, Vector3Scale(toHer, 6));
                    d.lastHitBy = "the pod";
                }
            }
            Say(s.name, "'s pod herds the divers out into the open", 2);
        }
        return;
    }
    if (near < 0) return;
    float d0 = nd - reach;
    int pick = -1;
    if (bite && d0 <= bite->range && bossCd[2] <= 0 && (ph == 3 || Rand() < 0.5f)) pick = 2;
    else if (slap && d0 <= slap->range && bossCd[3] <= 0) pick = 3;
    else if (ram && d0 <= ram->range && bossCd[1] <= 0) pick = 1;
    else if (herd && ph == 1 && bossCd[0] <= 0 && podDead < 3) pick = 4;
    if (pick < 0) return;
    const Attack* at = pick == 1 ? ram : pick == 2 ? bite : pick == 3 ? slap : herd;
    bossWind = pick; bossWindT = at ? at->windup : 1.0f; bossTarget = near;
    bossCd[pick == 4 ? 0 : pick] = at ? std::max(1.5f, at->cooldown) : 8;
}

// ---------------------------------------------------------------- Atlantis
int Match::QuestChainOf(int step) const {
    const Json& qs = map->extra["quests"];
    for (int c = 0; c < (int)qs.a.size(); c++) if (step >= qs.a[c]["first"].I() && step <= qs.a[c]["last"].I()) return c;
    return -1;
}

// A quest chain's current step is done: say so, move on, and pay out at the end.
void Match::QuestAdvance(int c, DiverState* d) {
    const Json& q = map->extra["quests"].a[c];
    int step = questAt[c];
    const Json& st = q["steps"][std::to_string(step)];
    Say(q["name"].Str0(), st["done"].Str0(), 4);
    questAt[c]++;
    if (questAt[c] <= q["last"].I()) return;
    std::string rw = q["reward"].Str0();
    questDone = true;
    if (rw == "void") bonusEarned.push_back("finallog");
    if (rw == "wonder" && d) { GiveWeapon(*d, WonderIdx()); d->lastKill = "The " + W(Cur(*d)).name; d->lastKillT = 4; }
    if (rw == "void") {
        // the final log: the Abyssal Lure (Forged) for the one who dropped the ledge, 5,000 split, and what the station bred
        int n = 0; for (const auto& o : divers) if (!o.dead) n++;
        for (auto& o : divers) if (!o.dead) Pay(o, 5000.0f / std::max(1, n));
        if (d) { GiveWeapon(*d, WonderIdx(), true); for (auto& h : d->weapons) if (h.def == WonderIdx() && !h.forged) { h.forged = true; h.mag = (int)MagMax(W(h), h); h.reserve = (int)ResMax(W(h), h); } }
        Say("The final log", "'We were breeding it back. The Relict was ours, once: the station's, the Chief's. We gave it the abyss to grow in.'", 7);
    }
    if (rw == "lighthouse") {
        // "the whole city appears on the sonar"; "the treasury vault: 5,000 scrip split"; "the Tide Staff (Forged) for the lighter"
        revealAll = true;
        int n = 0; for (const auto& o : divers) if (!o.dead) n++;
        for (auto& o : divers) if (!o.dead) Pay(o, 5000.0f / std::max(1, n));
        if (d) {
            GiveWeapon(*d, WonderIdx(), true);
            for (auto& h : d->weapons) if (h.def == WonderIdx() && !h.forged) { h.forged = true; h.mag = (int)MagMax(W(h), h); h.reserve = (int)ResMax(W(h), h); }
        }
        Say("", "The treasury vault opens: 5,000 scrip split between the divers", 5);
    }
}

// The grates the Wyrm may use: phase 1 the god-pool and the chapel's two; phase 2 the district of the diver it wants
// and the district below it ("blood flows downhill, and so does it"); never one beside fresh ichor.
std::vector<int> Match::WyrmGrates(int ph, int zone) const {
    std::vector<int> out;
    const Json& wy = map->extra["wyrm"];
    float repel = map->extra["ichor"]["repel_m"].F(20);
    for (int li = 0; li < (int)map->links.size(); li++) {
        const Link& l = map->links[li];
        if (map->zones[l.from].name != "The Cisterns") continue;
        if (!HasW(l.passage, "grate") && !HasW(l.passage, "god-pool")) continue;
        bool ok = false;
        if (ph == 1) { for (const Json& g : wy["phase1_grates"].a) if (g.Str0() == l.passage) ok = true; }
        else if (zone >= 0) {
            if (l.to == zone) ok = true;
            for (const auto& k : map->links) {   // the district just below the diver's (linked, lower floor)
                int o = k.from == zone ? k.to : k.to == zone ? k.from : -1;
                if (o == l.to && o >= 0 && map->zones[o].y0 < map->zones[zone].y0 && map->zones[o].diverOk) ok = true;
            }
        }
        for (const auto& ic : ichor) if (Vector3Distance(ic.pos, l.b) < repel) ok = false;
        if (ok) out.push_back(li);
    }
    return out;
}

// The Cistern Wyrm (boss sheet): it lives in the cisterns under every district and comes up through the grates.
void Match::UpdateBossWyrm(float dt) {
    Agent& b = eco.agents[bossAgent];
    const Species& s = map->species[b.sp];
    int cis = map->ZoneIndex("The Cisterns");
    auto attack = [&](const char* name) -> const Attack* { for (int k : attacksBySp[b.sp]) if (HasW(map->attacks[k].name, name)) return &map->attacks[k]; return nullptr; };
    float frac = b.hp / std::max(1.0f, b.hpMax);
    int ph = frac > 0.6f ? 1 : frac > 0.3f ? 2 : 3;
    auto bark = [&](const char* k) { auto it = map->faction.barks.find(k); if (it != map->faction.barks.end() && !it->second.empty()) Say(map->faction.speciesName, it->second[0], 3); };
    if (ph != bossPhase) {
        bossPhase = ph;
        if (ph == 2) {
            Say(s.name, "leaves the summit: every grate in the district can hide it now", 4);
            if (!wyrmCongers) {
                // "congers pour from the other grates"
                wyrmCongers = true;
                int ce = map->SpeciesIndex("Conger Eel");
                if (ce >= 0) for (int li = 0, n = 0; li < (int)map->links.size() && n < 3; li++) if (HasW(map->links[li].passage, "grate") && map->zones[map->links[li].from].name == "The Cisterns" && Rand() < 0.4f) {
                    int a = eco.Spawn(ce, map->links[li].b, map->links[li].to); n++;
                    int di = NearestDiver(map->links[li].b, 40, false);
                    if (di >= 0) { eco.agents[a].st = State::Hunt; eco.agents[a].target = divers[di].agent; eco.agents[a].hunger = 1; }
                }
            }
        }
        if (ph == 3) {
            // Flood: every grate in the district surges (slow 30%, blood doubled); the great white comes over the wall
            floodT = 12; wyrmFlooded = true;
            Say(s.name, "FLOODS the district: every grate surges, and something big comes over the wall", 5);
            int gw = map->SpeciesIndex("Great White");
            int bleeder = -1; float bd = 1e9f;
            for (const auto& d : divers) if (!d.dead && !d.downed && d.agent >= 0) { float v = d.bleedT > 0 ? 0 : Vector3Distance(d.pos, b.pos); if (v < bd) { bd = v; bleeder = d.slot; } }
            for (auto& o : eco.agents) if (o.alive && o.sp == gw && bleeder >= 0) { o.st = State::Hunt; o.target = divers[bleeder].agent; o.targetCorpse = false; o.hunger = 1; o.stateT = 0; }
        }
    }
    if (bossGillsT > 0) bossGillsT -= dt;
    // who it wants: phase 1 only divers on the summit's chapel floor; later the nearest diver anywhere
    int want = -1; float wd = 1e9f;
    int chapel = map->ZoneIndex("The Grand Chapel");
    for (const auto& d : divers) {
        if (d.dead || d.downed) continue;
        if (ph == 1 && d.zone != chapel) continue;
        float dist = Vector3Distance(d.pos, b.pos);
        if (dist < wd) { wd = dist; want = d.slot; }
    }
    // a diver dragged below: 6 s for the team to put 200 into the gills
    if (wyrmDragDiver >= 0) {
        DiverState& d = divers[wyrmDragDiver];
        wyrmDragT -= dt;
        if (wyrmGrate >= 0) d.pos = Vector3Lerp(d.pos, map->links[wyrmGrate].b, std::min(1.0f, dt * 4));
        d.vel = {0, 0, 0};
        if (wyrmDragDmg >= 200 || d.dead || d.downed) {
            d.heldT = 0; d.holder = -1; wyrmDragDiver = -1;
            if (wyrmDragDmg >= 200) { b.stun = 2; Say(s.name, "lets go: the gills took it", 3); }
        } else if (wyrmDragT <= 0) {
            d.heldT = 0; d.holder = -1; wyrmDragDiver = -1;
            d.lastHitBy = s.name;
            DownDiver(d, s.name + " (dragged into the drains)");
        }
    }
    auto sink = [&]() {
        wyrmState = 0; wyrmT = ph == 1 ? 6.0f : 4.0f; wyrmStruck = false;
        if (wyrmGrate >= 0) { b.pos = map->links[wyrmGrate].a; b.zone = cis; }
        b.vel = {0, 0, 0};
    };
    if (wyrmStrandT > 0) {
        // stranded by the god-pool trap: exposed and helpless
        wyrmStrandT -= dt; bossGillsT = std::max(bossGillsT, 0.2f); b.vel = {0, 0, 0};
        if (wyrmStrandT <= 0) sink();
        return;
    }
    switch (wyrmState) {
        case 0: {   // below: it follows under the streets and picks a grate near the diver it wants
            b.zone = cis; b.vel = {0, 0, 0}; b.st = State::Rest;
            if (want >= 0) b.pos = map->zones[cis].Clamp({divers[want].pos.x, b.pos.y, divers[want].pos.z}, 1);
            wyrmT -= dt;
            if (want < 0 || wyrmT > 0) break;
            std::vector<int> gs = WyrmGrates(ph, divers[want].zone);
            int best = -1; float bg = ph == 1 ? 40.0f : 14.0f;
            for (int li : gs) { float dd = Vector3Distance(map->links[li].b, divers[want].pos); if (dd < bg) { bg = dd; best = li; } }
            if (best < 0) { wyrmT = 1; break; }
            wyrmGrate = best; wyrmState = 1; wyrmT = 1.0f;   // "The grate rattles, then bursts"
            b.pos = map->links[best].a;
            if (!bossActive) { bossActive = true; Say(s.name, "stirs under " + map->zones[map->links[best].to].name, 4); }
            if (Rand() < 0.5f) bark("wyrm");
            fx.push_back({5, map->links[best].b, {0, 0, 0}});
            break;
        }
        case 1:     // rising
            wyrmT -= dt;
            if (wyrmT > 0) break;
            wyrmState = ph >= 3 ? 3 : 2; wyrmUp = 0; wyrmT = 3.0f; wyrmStruck = false;
            b.pos = Vector3Add(map->links[wyrmGrate].b, {0, 1.2f, 0}); b.zone = map->links[wyrmGrate].to;
            break;
        case 2: {   // surfaced: the strike at 0.6 s, the gills bare from 2 s
            wyrmUp += dt; wyrmT -= dt; b.vel = {0, 0, 0};
            if (wyrmGrate >= 0) b.pos = Vector3Add(map->links[wyrmGrate].b, {0, 1.2f, 0});
            if (wyrmUp >= 0.6f && !wyrmStruck) {
                wyrmStruck = true;
                const Attack* ss = attack("surface"); const Attack* dg = attack("drag");
                float dm = map->Tide(tide).dmgMult;
                int hit = NearestDiver(map->links[wyrmGrate].b, (ss ? ss->range : 4) + 0.5f, false);
                if (hit >= 0) {
                    DiverState& d = divers[hit];
                    if (dg && wyrmDragDiver < 0 && Rand() < 0.35f) {
                        wyrmDragDiver = hit; wyrmDragT = 6; wyrmDragDmg = 0;
                        d.heldT = 6.5f; d.holder = bossAgent; d.holdLethal = true;
                        Say(s.name, "drags a diver into the drains: 200 into its gills in 6 s!", 4);
                    } else if (ss) HitDiver(d, ss->damage * dm, s.name, ss->effect, b.pos, bossAgent);
                }
            }
            if (wyrmUp >= 2.0f && wyrmUp - dt < 2.0f) bossGillsT = 1.5f;
            if (wyrmT <= 0 && wyrmDragDiver < 0) sink();
            break;
        }
        case 3: {   // phase 3: it surfaces fully and hunts the streets like a beast
            const Attack* cl = attack("coil");
            int near = NearestDiver(b.pos, 40, false);
            float nd = near >= 0 ? Vector3Distance(divers[near].pos, b.pos) : 1e9f;
            for (float& c : bossCd) if (c > 0) c -= dt;
            if (near >= 0 && cl && nd - bodies[b.sp].length * 0.3f < cl->range && bossCd[1] <= 0) {
                bossCd[1] = cl->cooldown;
                HitDiver(divers[near], cl->damage * map->Tide(tide).dmgMult, s.name, cl->effect, b.pos, bossAgent);
            }
            if (floodT > 0) {
                floodT -= dt;
                for (auto& d : divers) if (!d.dead && d.zone == b.zone) d.slowT = std::max(d.slowT, 0.5f);
            }
            break;
        }
    }
}

void Match::UpdateAtlantis(float dt) {
    // ichor fades
    for (auto& ic : ichor) ic.t -= dt;
    ichor.erase(std::remove_if(ichor.begin(), ichor.end(), [](const Ichor& i) { return i.t <= 0; }), ichor.end());
    // the Wyrm reforms in the god-pool six minutes after its death, at half health
    if (wyrmReformT > 0) {
        wyrmReformT -= dt;
        if (wyrmReformT <= 0) {
            int wi = -1; for (int i = 0; i < (int)map->species.size(); i++) if (map->species[i].tier == 5) wi = i;
            int cis = map->ZoneIndex("The Cisterns");
            if (wi >= 0 && cis >= 0) {
                int a = eco.Spawn(wi, map->zones[cis].Center(), cis);
                eco.agents[a].hp = eco.agents[a].hpMax * 0.5f;
                bossAgent = a; bossActive = false; bossPhase = 1; wyrmState = 0; wyrmT = 10; wyrmCongers = false;
                Say(map->species[wi].name, "has reformed in the god-pool", 5);
            }
        }
    }
    // the Priest: 60 s of chanting in the plaza calls the Alien Horror for 90 s (killing him first prevents it)
    int priest = -1;
    for (int i = 0; i < (int)eco.agents.size(); i++) {
        const Agent& a = eco.agents[i];
        if (a.alive && a.unit >= 0 && map->species[a.sp].isEnemy && map->faction.units[a.unit].role == "priest") priest = i;
    }
    if (priest >= 0 && !horrorCalled) {
        if (NearestDiver(eco.agents[priest].pos, 60, false) >= 0) priestT += dt;
        if (priestT >= 60) {
            horrorCalled = true;
            int hs = map->SpeciesIndex("Alien Horror");
            if (hs >= 0) {
                const Agent& P = eco.agents[priest];
                horror = eco.Spawn(hs, map->zones[P.zone].Clamp(Vector3Add(P.pos, {0, 2, 0}), 1.5f), P.zone);
                horrorT = 90; horrorTeleT = 8;
                auto it = map->faction.barks.find("priest");
                Say(map->faction.speciesName, it != map->faction.barks.end() && !it->second.empty() ? it->second[0] : "The sigil is drawn.", 4);
                Say("", "The Alien Horror tears through the plaza", 4);
            }
        }
    } else if (priest < 0) priestT = 0;
    if (horror >= 0) {
        if (horror >= (int)eco.agents.size() || !eco.agents[horror].alive) horror = -1;
        else {
            Agent& h = eco.agents[horror];
            horrorT -= dt; horrorTeleT -= dt;
            int di = NearestDiver(h.pos, 30, false);
            if (di >= 0) { h.st = State::Hunt; h.target = divers[di].agent; h.targetCorpse = false; }
            if (horrorTeleT <= 0 && di >= 0) {
                // "teleports one diver 20 m every 8 s"
                horrorTeleT = 8;
                DiverState& d = divers[di];
                for (int k = 0; k < 16; k++) {
                    float an = Rand(0, 6.2832f);
                    Vector3 q = Vector3Add(d.pos, {cosf(an) * 20, Rand(-1, 1), sinf(an) * 20});
                    if (level.Inside(q, 0.5f, linkOpen)) { d.pos = q; d.vel = {0, 0, 0}; d.lastHitBy = "the Alien Horror"; fx.push_back({4, q, {0, 0, 0}}); break; }
                }
            }
            if (horrorT <= 0) { eco.suppressBlood = true; eco.Kill(horror, -1); eco.suppressBlood = false; horror = -1; Say("", "The Alien Horror folds back into the sigil", 3); }
        }
    }
    // the quests' timed step: hold the plaza
    const Json& qs = map->extra["quests"];
    if ((int)questAt.size() != (int)qs.a.size()) { questAt.clear(); for (const Json& q : qs.a) questAt.push_back(q["first"].I()); }
    for (int c = 0; c < (int)qs.a.size(); c++) {
        if (questAt[c] > qs.a[c]["last"].I()) continue;
        const Json& st = qs.a[c]["steps"][std::to_string(questAt[c])];
        if (st["kind"].Str0() != "hold") continue;
        int zi = map->ZoneIndex(st["zone"].Str0());
        DiverState* in = nullptr;
        for (auto& d : divers) if (!d.dead && !d.downed && d.zone == zi) in = &d;
        holdT = in ? holdT + dt : std::max(0.0f, holdT - dt);
        if (holdT >= st["seconds"].F(45)) { holdT = 0; QuestAdvance(c, in); }
    }
    for (auto& d : divers) if (d.downed && d.spark) { d.spark = false; Say("", "The spark's jar breaks", 2); }
}

// ---------------------------------------------------------------- hidden quests (the design doc's step lists)
// A long open: the captain's safe (20 s; the ship's bell rings) or the Lantern Cache's crate (15 s; a mini-Hunt of 6
// comes for it). Held E (or a bot at it) runs the clock; leaving it for 1.5 s resets only that step.
bool Match::LongOpen(DiverState& d, int si, float need, float dt) {
    const Station& s = level.stations[si];
    const Json& hq = map->extra["hidden_quest"];
    bool safe = s.type == StationType::Quest;
    openAwayT = 0;
    if (!openStarted) {
        openStarted = true;
        if (safe) { eco.AddNoise(s.pos, hq["bell_noise"].F(10), true); Say("The ship's bell", "rings as the safe's wheel turns", 4); }
        else { eco.SpawnSquad(s.zone >= 0 ? map->zones[s.zone].alarmRegion : 0, true, hq["hunt"].I(6), false); Say("", "The crate groans open, and the Drowned come for it", 4); }
    }
    openT += std::max(dt, 0.01f);
    if (openT < need) return true;
    openSt = -1; openT = 0; openStarted = false;
    questDone = true;
    int wi = WonderIdx();
    if (safe) {
        safeOpen = true; bonusEarned.push_back("owners");
        for (auto& o : divers) if (!o.dead) Pay(o, hq["reward_scrip"].F(3000));
        if (wi >= 0) { GiveWeapon(d, wi, true); for (auto& h : d.weapons) if (h.def == wi) h.forged = true; }
        Say("The captain's safe", "swings open: 3,000 scrip each, the Lightning Keel for the opener, and a letter from the Owners", 6);
    } else {
        cacheOpen = true; bonusEarned.push_back("expedition");
        for (auto& o : divers) if (!o.dead) Pay(o, hq["reward_scrip"].F(2000));
        if (wi >= 0) GiveWeapon(d, wi);
        if (hq.Has("moved_entry")) eco.entryOverride = hq["moved_entry"].Str0();
        Say("", map->extra["boss_key"]["text"].Str0() + " The Drowned will come by the Chimney now.", 6);
    }
    return true;
}

void Match::UpdateQuests(float dt) {
    if (openSt >= 0) {
        bool near = false;
        for (const auto& d : divers) if (!d.dead && !d.downed && Vector3Distance(d.pos, level.stations[openSt].pos) < 2.5f) near = true;
        if (!near) { openAwayT += dt; if (openAwayT > 1.5f) { openSt = -1; openT = 0; openStarted = false; openAwayT = 0; Say("", "Left half-open: it has to start again", 3); } }
    }
    if (nesting) {
        // the Reef's turtles nest while a diver holds the beach during a tide; five nests finish the quest
        const Json& qa = map->extra["quest_altar"];
        int beach = -1; for (const auto& s : level.stations) if (s.name == qa["poi"].Str0()) beach = s.zone;
        bool held = false;
        for (const auto& d : divers) if (!d.dead && !d.downed && d.zone == beach) held = true;
        if (held && phase == TidePhase::Tide) nestT += dt;
        if (nestT >= qa["nest_s"].F(12)) {
            nestT = 0; nests++;
            Say("", TextFormat("A turtle lays her eggs in the sand: %d of %d", nests, qa["nests"].I(5)), 3);
            if (nests >= qa["nests"].I(5)) {
                nesting = false; safeOpen = true; questDone = true;
                alliesHostile = false; hammerAvoid = true;
                int wi = WonderIdx();
                if (wi >= 0 && nestPlacer >= 0 && nestPlacer < (int)divers.size()) { DiverState& p = divers[nestPlacer]; GiveWeapon(p, wi, true); for (auto& h : p.weapons) if (h.def == wi) h.forged = true; }
                Say("", qa["done"].Str0(), 7);
            }
        }
    }
}

// ---------------------------------------------------------------- Salt Charms
bool Match::UseCharm(int di) {
    DiverState& d = divers[di];
    if (d.dead || d.downed || d.pouchNext >= (int)d.pouch.size()) return false;
    std::string id = d.pouch[d.pouchNext++];
    std::string name = id;
    for (const auto& c : Charms()) if (c.id == id) name = c.name;
    Say("", "Salt Charm: " + name, 3);
    fx.push_back({4, d.pos, {0, 0, 0}});
    if (id == "brines") d.keepBrines = true;
    else if (id == "circle") { d.circleT = 20; d.circlePos = d.pos; }
    else if (id == "locker") d.luckyLocker = true;
    else if (id == "shares") {
        // every living diver's scrip pooled and split evenly
        int total = 0, n = 0;
        for (const auto& o : divers) if (!o.dead) { total += o.scrip; n++; }
        for (auto& o : divers) if (!o.dead) o.scrip = total / std::max(1, n);
        Say("", TextFormat("Fair Shares: %d scrip each", total / std::max(1, n)), 4);
    }
    else if (id == "fins") d.finsT = 30;
    else if (id == "clean") {
        // the blood within 30 m, gone
        Field& f = eco.scent;
        int cx, cy, cz;
        if (f.Cell(d.pos, cx, cy, cz)) {
            int r = (int)ceilf(30 / f.cell);
            for (int z = std::max(0, cz - r); z < std::min(f.nz, cz + r + 1); z++) for (int y = std::max(0, cy - r); y < std::min(f.ny, cy + r + 1); y++) for (int x = std::max(0, cx - r); x < std::min(f.nx, cx + r + 1); x++) {
                Vector3 c{f.origin.x + (x + 0.5f) * f.cell, f.origin.y + (y + 0.5f) * f.cell, f.origin.z + (z + 0.5f) * f.cell};
                if (Vector3Distance(c, d.pos) <= 30) f.v[f.Idx(x, y, z)] = 0;
            }
            f.BuildSat();
        }
    }
    else if (id == "luck") d.luckKills = 10;
    else if (id == "chum") {
        // a big chum cloud 30 m off along the diver's look (as far as open water goes)
        Vector3 fw = Forward(d), at = d.pos;
        for (float t = 0; t < 30; t += 0.5f) { Vector3 q = Vector3Add(d.pos, Vector3Scale(fw, t)); if (!level.Inside(q, 0.3f, linkOpen, true)) break; at = q; }
        eco.AddChum(at, 200);
        fx.push_back({5, at, {0, 0, 0}});
    }
    else if (id == "shell") d.shellT = 60;
    else if (id == "ghost") d.ghostT = 15;
    return true;
}

void Match::UpdateCharms(float dt) {
    for (auto& d : divers) {
        if (d.circleT > 0) {
            // Salt Circle: beasts won't cross it: anything inside 4 m is pushed out and forgets the diver
            d.circleT -= dt;
            for (auto& a : eco.agents) {
                if (!a.alive || a.diver >= 0 || map->species[a.sp].isEnemy) continue;
                Vector3 off = Vector3Subtract(a.pos, d.circlePos);
                float dist = Vector3Length(off);
                if (dist > 4) continue;
                if (dist < 0.1f) off = {1, 0, 0};
                Vector3 dir = Vector3Normalize(off);
                for (int k = 0; k < 8; k++) {   // (turn the push until it clears the circle inside the room)
                    float an = k * 0.785f;
                    Vector3 dk{dir.x * cosf(an) - dir.z * sinf(an), dir.y, dir.x * sinf(an) + dir.z * cosf(an)};
                    Vector3 q = map->zones[a.zone].Clamp(Vector3Add(d.circlePos, Vector3Scale(Vector3Normalize(dk), 4.1f)), 0.3f);
                    if (Vector3Distance(q, d.circlePos) >= 4.0f || k == 7) { a.pos = q; break; }
                }
                a.vel = {0, 0, 0};
                if (a.target == d.agent) { a.st = State::Return; a.target = -1; a.goal = a.home; }
            }
        }
        if (d.finsT > 0) { d.finsT -= dt; d.stamina = 1; }
        if (d.shellT > 0) d.shellT -= dt;
        if (d.ghostT > 0) {
            d.ghostT -= dt;
            for (auto& a : eco.agents) if (a.alive && a.target == d.agent && !a.targetCorpse) { a.st = State::Return; a.target = -1; a.goal = a.home; }
        }
    }
}

// ---------------------------------------------------------------- the dossier
std::string Match::DossierName(int ai) const {
    const Species& s = map->species[eco.agents[ai].sp];
    if (s.isEnemy) return map->faction.name.empty() ? map->faction.speciesName : map->faction.name;
    return s.name;
}

void Match::UpdateDossier(float dt) {
    // a page for every beast, flora and the faction: killed, or watched for 30 s in all (in sight within 25 m; flora
    // within 12 m)
    dossierTick += dt;
    if (dossierTick < 0.5f) return;
    float step = dossierTick; dossierTick = 0;
    std::set<std::string> seenNow;
    for (const auto& d : divers) {
        if (d.dead || d.downed || d.bot) continue;             // (the profile is the human diver's)
        for (int i = 0; i < (int)eco.agents.size(); i++) {
            const Agent& a = eco.agents[i];
            if (!a.alive || a.diver >= 0 || Vector3Distance(a.pos, d.pos) > 25) continue;
            std::string n = DossierName(i);
            if (dossierSeen.count(n) || seenNow.count(n)) continue;
            if (!level.Sight(Eye(d), a.pos, linkOpen, true)) continue;
            seenNow.insert(n);
        }
        for (const auto& fp : eco.flora) {
            if (fp.units <= 0 || Vector3Distance(fp.pos, d.pos) > 12) continue;
            const std::string& n = map->flora[fp.flora].name;
            if (!dossierSeen.count(n)) seenNow.insert(n);
        }
    }
    for (const auto& n : seenNow) {
        float& t = watchT[n];
        t += step;
        if (t >= 30) { dossierSeen.insert(n); Say("", "Dossier: " + n, 2); }
    }
}

// ---------------------------------------------------------------- Approaching the Void
// A diver lost for good this tide: the void's pull, the Sand Worm (no down, no revive; back next tide).
void Match::VoidDeath(DiverState& d, const std::string& by) {
    if (d.dead) return;
    if (!d.downed) DownDiver(d, by);
    d.downed = false; d.dead = true; d.voidT = 0; d.wormT = 0;
    DropKeys(d, level.start);   // (a key can't lie in the void: the current washes it back to the start pocket)
    if (d.agent >= 0) eco.agents[d.agent].alive = false;
    if (d.egg) {
        // the egg goes with them: the Relict follows it into the void and the final log ends without the weapon
        d.egg = false; relictGone = true;
        for (auto& a : eco.agents) if (a.alive && map->species[a.sp].name == "The Relict") { eco.suppressBlood = true; eco.Kill((int)(&a - &eco.agents[0]), -1); eco.suppressBlood = false; }
        if (!questAt.empty()) questAt[0] = map->extra["quests"].a[0]["last"].I() + 1;
        Say("", "The egg falls into the void; the Relict follows it down. The final log ends here.", 5);
    }
    Say("", "Diver " + std::to_string(d.slot + 1) + " is gone: " + by + " (back next tide)", 5);
}

bool Match::Forbidden(Vector3 p, float margin) const {
    int z = eco.ZoneAt(p);
    if (z >= 0 && z < (int)voidZone.size() && voidZone[z]) return true;
    const Json& wm = map->extra["worm"];
    if (wm.IsObj() && z >= 0 && map->zones[z].name == "The Rim") {
        for (const auto& po : map->pois) if (po.name == wm["center_poi"].Str0() && Vector2Distance({p.x, p.z}, {po.pos.x, po.pos.z}) > wm["radius_m"].F(350) - margin) return true;
    }
    return false;
}

// The Leviathan's lure: ahead of and above its mouth, an emergency light in the dark.
Vector3 Match::LurePos() const {
    if (bossAgent < 0 || bossAgent >= (int)eco.agents.size()) return {0, 0, 0};
    const Agent& b = eco.agents[bossAgent];
    Vector3 f = Vector3Length(b.vel) > 0.05f ? Vector3Normalize(b.vel) : Vector3{0, 0, 1};
    Vector3 p = Vector3Add(b.pos, Vector3Add(Vector3Scale(f, bodies[b.sp].length * 0.5f + bodies[b.sp].radius + 1.2f), {0, bodies[b.sp].radius * 0.6f, 0}));   // on its stalk, clear of the jaws
    return b.zone >= 0 ? map->zones[b.zone].Clamp(p, 1.0f) : p;   // (always inside its hall, where a dart can reach it)
}

// The vault's collapsing ledge (Overlook 5): everything on it falls into the void.
void Match::DropLedge(DiverState* by) {
    const std::string ln = map->extra["leviathan"]["ledge"].Str0("Overlook 5");
    Vector3 at{}; bool found = false;
    for (const auto& s : level.stations) if (s.name.find(ln.substr(0, 10)) != std::string::npos) { at = s.pos; found = true; }
    if (!found) return;
    ledgeDropped = true;
    fx.push_back({5, at, {0, 0, 0}});
    eco.AddNoise(at, 9, true);
    bool relictFell = false;
    for (int i = 0; i < (int)eco.agents.size(); i++) {
        Agent& a = eco.agents[i];
        if (!a.alive || a.diver >= 0 || Vector3Distance(a.pos, at) > 8) continue;
        if (map->species[a.sp].name == "The Relict") relictFell = true;
        eco.suppressBlood = true; eco.Kill(i, -1); eco.suppressBlood = false;
    }
    for (auto& d : divers) if (!d.dead && Vector3Distance(d.pos, at) < 7) VoidDeath(d, "the collapsing ledge");
    if (relictFell) {
        relictGone = true;
        const Json& qs = map->extra["quests"];
        int c = qs.IsArr() && !qs.a.empty() ? QuestChainOf(8) : -1;
        if (c >= 0 && c < (int)questAt.size() && questAt[c] == 8 && eggPlaced) QuestAdvance(c, by);
        else Say("", "The Relict falls with the ledge into the void. It won't be back this match.", 5);
    } else if (eggPlaced) {
        eggPlaced = false; relictGone = true;
        if (!questAt.empty()) questAt[0] = map->extra["quests"].a[0]["last"].I() + 1;
        Say("", "The ledge falls with the egg and nothing else; the Relict follows its egg into the dark. The final log ends here.", 5);
    }
}

// The Lantern Leviathan (boss sheet): the lure, the Swallow, the Bite, the Dark; three lures in phase 3.
void Match::UpdateBossLeviathan(float dt, int near, float nd) {
    Agent& B = eco.agents[bossAgent];
    const Species& s = map->species[B.sp];
    const Json& lv = map->extra["leviathan"];
    auto attack = [&](const char* name) -> const Attack* { for (int k : attacksBySp[B.sp]) if (HasW(map->attacks[k].name, name)) return &map->attacks[k]; return nullptr; };
    float frac = B.hp / std::max(1.0f, B.hpMax);
    int ph = frac > 0.6f ? 1 : frac > 0.3f ? 2 : 3;
    if (ph != bossPhase) {
        bossPhase = ph;
        if (ph == 2) Say(s.name, "moves through the black coral: the Dark comes now", 4);
        if (ph == 3) {
            Say(s.name, "splits its lure into three", 5);
            relictDuel = Rand() < 1.0f / 3.0f ? 1 : 0; duelT = 0;
            for (auto& a : eco.agents) if (a.alive && map->species[a.sp].name == "The Relict" && !relictGone) { a.st = State::Investigate; a.goal = B.pos; a.stateT = 0; a.hunger = 1; }
        }
    }
    for (float& c : bossCd) if (c > 0) c -= dt;
    if (bossGillsT > 0) bossGillsT -= dt;
    if (lureHP <= 0) { lureRegrowT -= dt; if (lureRegrowT <= 0) { lureHP = lv["lure_hp"].F(500); Say(s.name, "'s lure glows again", 2); } }
    Vector3 lure = LurePos();
    Vector3 mouth = Vector3Add(B.pos, Vector3Scale(Vector3Length(B.vel) > 0.05f ? Vector3Normalize(B.vel) : Vector3{0, 0, 1}, bodies[B.sp].length * 0.5f));
    // the lure draws: divers and curious beasts drift to it
    if (lureDriftT > 0 && lureHP > 0) {
        lureDriftT -= dt;
        for (auto& d : divers) if (!d.dead && !d.downed && Vector3Distance(d.pos, lure) < 40 && Vector3Distance(d.pos, lure) > 3) d.vel = Vector3Add(d.vel, Vector3Scale(Vector3Normalize(Vector3Subtract(lure, d.pos)), 0.5f * dt * 4));
    }
    // phase 3's decoys hold a diver who swims into them for 2 s
    if (ph == 3 && lureHP > 0) {
        Vector3 side{-(lure.z - B.pos.z), 0, lure.x - B.pos.x};
        side = Vector3Length(side) > 0.1f ? Vector3Normalize(side) : Vector3{1, 0, 0};
        for (int k = -1; k <= 1; k += 2) {
            Vector3 dp = Vector3Add(lure, Vector3Scale(side, 8.0f * k));
            for (auto& d : divers) if (!d.dead && !d.downed && d.decoyCd <= 0 && Vector3Distance(d.pos, dp) < 2.5f) { d.stunT = std::max(d.stunT, 2.0f); d.decoyCd = 6; Say("", "A decoy lure: it holds you", 2); }
        }
        // the ledge: it drops Overlook 5 if a diver stands on it
        if (!ledgeDropped) for (const auto& st : level.stations) if (st.name.find(lv["ledge"].Str0("Overlook 5").substr(0, 10)) != std::string::npos)
            for (const auto& d : divers) if (!d.dead && !d.downed && Vector3Distance(d.pos, st.pos) < 6) { Say(s.name, "brings the ledge down", 4); DropLedge(nullptr); break; }
        // the Relict comes for the corpses: it wins one time in three
        for (int i = 0; i < (int)eco.agents.size(); i++) {
            Agent& r = eco.agents[i];
            if (!r.alive || map->species[r.sp].name != "The Relict" || Vector3Distance(r.pos, B.pos) > 14) continue;
            duelT += dt;
            if (duelT > 10) {
                duelT = -1e9f;
                if (relictDuel == 1) { B.hp = std::max(1.0f, B.hp - B.hpMax * 0.3f); Say("The Relict", "tears into the Leviathan", 4); }
                else { eco.Kill(i, bossAgent); Say(s.name, "swallows the Relict", 4); }
            }
        }
    }
    // a diver swallowed whole: 5 s for 200 into the gills
    if (swallowDiver >= 0) {
        DiverState& d = divers[swallowDiver];
        swallowT -= dt;
        d.pos = Vector3Lerp(d.pos, mouth, std::min(1.0f, dt * 6)); d.vel = {0, 0, 0};
        if (swallowDmg >= lv["swallow_dmg"].F(200) || d.dead || d.downed) {
            d.heldT = 0; d.holder = -1; swallowDiver = -1; bossGillsT = 2;
            if (!d.downed && !d.dead) Say(s.name, "spits the diver out: the gills took it", 3);
        } else if (swallowT <= 0) {
            d.heldT = 0; d.holder = -1; swallowDiver = -1; bossGillsT = 2;
            DownDiver(d, s.name + " (swallowed whole)");
        }
        return;
    }
    if (B.stun > 0 || B.held > 0) { bossWind = -1; return; }
    const Attack* lu = attack("lure"); const Attack* sw = attack("swallow"); const Attack* bi = attack("bite"); const Attack* dk = attack("dark");
    float dm = map->Tide(tide).dmgMult;
    if (bossWind >= 0) {
        bossWindT -= dt;
        if (bossWindT > 0) return;
        int k = bossWind; bossWind = -1;
        DiverState& t = divers[std::clamp(bossTarget, 0, (int)divers.size() - 1)];
        float td = Vector3Distance(t.pos, mouth);
        if (k == 1 && lu) { lureDriftT = 4; for (auto& a : eco.agents) if (a.alive && a.diver < 0 && !map->species[a.sp].isEnemy && map->species[a.sp].curiosity > 0.4f && Vector3Distance(a.pos, lure) < 40) { a.st = State::Investigate; a.goal = lure; a.stateT = 0; } }
        else if (k == 2 && sw && td <= sw->range && !t.dead && !t.downed) {
            swallowDiver = t.slot; swallowT = lv["swallow_s"].F(5); swallowDmg = 0;
            t.heldT = swallowT + 0.5f; t.holder = bossAgent; t.holdLethal = true;
            Say(s.name, "swallows a diver whole: 200 into its gills in 5 s!", 4);
        } else if (k == 3 && bi && td <= bi->range + 0.5f) HitDiver(t, bi->damage * dm, s.name, bi->effect, B.pos, bossAgent);
        else if (k == 4 && dk) { darkT = 6; Say(s.name, "puts out every light", 3); }
        return;
    }
    if (near < 0) return;
    float md = Vector3Distance(divers[near].pos, mouth);
    int pick = -1;
    if (sw && md <= sw->range && bossCd[2] <= 0) pick = 2;
    else if (bi && md <= bi->range && bossCd[3] <= 0) pick = 3;
    else if (dk && ph >= 2 && nd < dk->range && bossCd[4 % 4] <= 0 && darkT <= 0) pick = 4;
    else if (lu && nd < lu->range && bossCd[1] <= 0 && lureHP > 0) pick = 1;
    if (pick < 0) return;
    const Attack* at = pick == 1 ? lu : pick == 2 ? sw : pick == 3 ? bi : dk;
    bossWind = pick; bossWindT = at ? at->windup : 1.0f; bossTarget = near;
    bossCd[pick % 4] = at ? at->cooldown : 8;
}

void Match::UpdateVoid(float dt) {
    if (darkT > 0) darkT -= dt;
    if (bossReformT > 0) {
        // the Leviathan reforms in its vault six minutes after its death, at half health
        bossReformT -= dt;
        if (bossReformT <= 0) {
            int wi = -1; for (int i = 0; i < (int)map->species.size(); i++) if (map->species[i].tier == 5) wi = i;
            int hz = wi >= 0 ? map->ZoneIndex(map->species[wi].homeZone) : -1;
            if (wi >= 0 && hz >= 0) {
                int a = eco.Spawn(wi, map->zones[hz].Center(), hz);
                eco.agents[a].hp = eco.agents[a].hpMax * 0.5f;
                bossAgent = a; bossActive = false; bossPhase = 1; lureHP = map->extra["leviathan"]["lure_hp"].F(500);
                Say(map->species[wi].name, "has reformed in the dark of " + map->zones[hz].name, 5);
            }
        }
    }
    for (auto& d : divers) if (d.decoyCd > 0) d.decoyCd -= dt;
    // the void: a diver out over the abyss sinks for 5 s, then is lost
    const Json& vz = map->extra["void_zones"];
    for (auto& d : divers) {
        if (d.dead || d.zone < 0) continue;
        bool over = false;
        for (const Json& z : vz.a) if (map->zones[d.zone].name == z.Str0()) over = true;
        if (over && eco.ZoneAt(d.pos) == d.zone) {
            if (d.voidT <= 0) Say("", "The void pulls you down: swim back to the edge!", 3);
            d.voidT += dt;
            d.vel.y -= 4.0f * dt;
            if (d.voidT >= map->extra["void"]["pull_s"].F(5)) VoidDeath(d, "the void");
        } else d.voidT = std::max(0.0f, d.voidT - dt * 2);
    }
    // the Sand Worm: beyond the bone stakes the sand shakes for 8 s, then it surfaces
    const Json& wm = map->extra["worm"];
    if (wm.IsObj()) {
        Vector3 c{};
        for (const auto& p : map->pois) if (p.name == wm["center_poi"].Str0()) c = p.pos;
        float rad = wm["radius_m"].F(350), tremor = wm["tremor_s"].F(8);
        int rim = map->ZoneIndex("The Rim");
        for (auto& d : divers) {
            if (d.dead) continue;
            bool out = d.zone == rim && Vector2Distance({d.pos.x, d.pos.z}, {c.x, c.z}) > rad;
            if (out) {
                if (d.wormT <= 0) Say("", "The sand shakes beyond the bone stakes...", 3);
                d.wormT += dt;
                if (d.wormT >= tremor) VoidDeath(d, "the Sand Worm");
            } else d.wormT = 0;
        }
    }
    // beacons: an on beacon pulls every beast within 40 m onto the diver nearest it
    float br = map->extra["beacons"]["radius_m"].F(40);
    beaconTick += dt;
    if (beaconTick > 2) {
        beaconTick = 0;
        for (auto& kv : beaconOn) {
            if (!kv.second || darkT > 0) continue;
            const Station& st = level.stations[kv.first];
            int di = NearestDiver(st.pos, 60, false);
            if (di < 0) continue;
            for (int i = 0; i < (int)eco.agents.size(); i++) {
                Agent& a = eco.agents[i];
                if (!a.alive || a.diver >= 0 || IsBoss(i) || map->species[a.sp].isEnemy || map->species[a.sp].size < 2 || Vector3Distance(a.pos, st.pos) > br) continue;
                a.st = State::Investigate; a.goal = divers[di].pos; a.stateT = 0;
            }
        }
    }
    // the Researchers' gas
    for (auto& g : gas) {
        g.t -= dt;
        for (auto& d : divers) if (!d.dead && !d.downed && Vector3Distance(d.pos, g.pos) < g.r) { d.hp -= g.dps * dt; d.regenT = 0; d.lastHitBy = "gas"; if (d.hp <= 0) DownDiver(d, "Researcher"); }
    }
    gas.erase(std::remove_if(gas.begin(), gas.end(), [](const Gas& g) { return g.t <= 0; }), gas.end());
    // the Abyssal Lure's lanterns: every beast in 60 m comes to the light, then it bursts
    for (auto& l : lures) {
        l.t -= dt;
        for (int i = 0; i < (int)eco.agents.size(); i++) {
            Agent& a = eco.agents[i];
            if (!a.alive || a.diver >= 0 || IsBoss(i) || Vector3Distance(a.pos, l.pos) > 60) continue;
            if (map->species[a.sp].isEnemy && a.oil) continue;   // clockwork doesn't care for lights
            a.st = State::Investigate; a.goal = l.pos; a.stateT = 0; a.target = -1;
        }
        if (l.t <= 0) Explode(l.pos, (l.forged ? 300.0f : 150.0f), 6, l.owner);
    }
    lures.erase(std::remove_if(lures.begin(), lures.end(), [](const LurePt& l) { return l.t <= 0; }), lures.end());
    // a colossal squid's arm at an overlook, summoned by a Void Cultist: it grabs whoever's close and hauls them to the edge
    if (tentacleT > 0) {
        tentacleT -= dt; tentacleCd -= dt;
        if (tentacleCd <= 0) {
            int di = NearestDiver(tentaclePos, 8, false);
            if (di >= 0) {
                DiverState& d = divers[di];
                HitDiver(d, 70 * map->Tide(tide).dmgMult, "a colossal squid's arm", "", tentaclePos);
                d.vel = Vector3Add(d.vel, Vector3Scale(Vector3Normalize(Vector3Subtract(tentaclePos, d.pos)), 6));
                tentacleCd = 3;
            }
        }
    }
    // specimens breed if nobody culls them (only the caiman eats them)
    for (const auto& kv : map->extra["breeders"].o) {
        if (kv.first[0] == '_') continue;
        int sp = map->SpeciesIndex(kv.first), zi = map->ZoneIndex(kv.second["zone"].Str0());
        if (sp < 0 || zi < 0) continue;
        int n = eco.CountInZone(sp, zi);
        if (n < 2 || n >= kv.second["cap"].I(12)) { breedT[sp] = 0; continue; }
        breedT[sp] += dt * (n / 2);
        if (breedT[sp] >= kv.second["every_s"].F(90)) { breedT[sp] = 0; eco.Spawn(sp, map->zones[zi].Clamp(Vector3Add(map->zones[zi].Center(), {Rand(-5, 5), 0, Rand(-5, 5)}), 1), zi); }
    }
}

// ---------------------------------------------------------------- drops
void Match::MaybeDrop(Vector3 at) {
    // "a 2.5% chance (1 in 40), rising to 4% when the team has had no drop for 3 minutes; the last two drops excluded"
    float chance = time - lastDropT > 180 ? 0.04f : 0.025f;
    if (Rand() >= chance) return;
    const Json& rows = Engine().drops["drops"];
    std::vector<std::pair<DropType, float>> pool;
    for (const Json& r : rows.a) {
        std::string n = r["drop"].Str0();
        DropType t = DropType::COUNT;
        for (int k = 0; k < (int)DropType::COUNT; k++) if (n == DropName((DropType)k)) t = (DropType)k;
        if (t == DropType::COUNT || r["min_tide"].I(1) > tide) continue;
        if (std::find(recentDrops.begin(), recentDrops.end(), t) != recentDrops.end()) continue;
        if (t == DropType::Shipwright) continue;               // "only if a barricade net is damaged" (the Ship has none yet)
        if (t == DropType::FireSale && tide - lastFireSaleTide < 3) continue;
        float w = tide >= 20 ? r["weight_tides_20+"].F(5) : tide >= 10 ? r["weight_tides_10_19"].F(5) : r["weight_tides_1_9"].F(5);
        pool.push_back({t, w});
    }
    float tot = 0;
    for (auto& p : pool) tot += p.second;
    if (tot <= 0) return;
    float r = Rand() * tot;
    DropType pick = pool.back().first;
    for (auto& p : pool) { r -= p.second; if (r <= 0) { pick = p.first; break; } }
    FloorDrop fd; fd.type = pick; fd.t = 30;
    fd.pos = level.Inside(at, 0.3f, linkOpen) ? at : level.Move(at, at, 0.3f, linkOpen);
    drops.push_back(fd);
    recentDrops.push_back(pick);
    if (recentDrops.size() > 2) recentDrops.erase(recentDrops.begin());
    lastDropT = time;
}

void Match::ApplyDrop(DropType t, Vector3 at) {
    Say("", std::string(DropName(t)) + "!", 3);
    switch (t) {
        case DropType::Resupply:
            for (auto& d : divers) { for (auto& h : d.weapons) { const WeaponDef& w = W(h); h.mag = (int)MagMax(w, h); h.reserve = (int)ResMax(w, h); } d.inkBombs = std::min(2, d.inkBombs + 1); }
            break;
        case DropType::BloodFrenzy: frenzyT = 30; break;
        case DropType::DoubleScrip: doubleScripT = 30; break;
        case DropType::Purge: {
            // "Every beast within 40 m dies and leaves no blood; enemies flee"
            eco.suppressBlood = true;
            size_t c0 = eco.corpses.size();
            int n = 0;
            for (int i = 0; i < (int)eco.agents.size(); i++) {
                Agent& a = eco.agents[i];
                if (!a.alive || a.diver >= 0 || IsBoss(i) || Vector3Distance(a.pos, at) > 40) continue;
                if (map->species[a.sp].isEnemy) { a.st = State::Flee; a.goal = a.home; a.stateT = 0; continue; }
                eco.Kill(i, -1); n++;
            }
            for (size_t c = c0; c < eco.corpses.size(); c++) { eco.corpses[c].bloodLeft = 0; eco.corpses[c].active = false; }
            eco.suppressBlood = false;
            if (phase == TidePhase::Tide) tideKills += n;
            break;
        }
        case DropType::Shipwright: break;
        case DropType::FireSale: fireSaleT = 30; lastFireSaleTide = tide; break;
        case DropType::HarpoonHour: {
            int hc = Weapons().Index("harpooncannon");
            if (hc < 0) break;
            harpoonT = 30;
            for (auto& d : divers) if (!d.dead && !d.harpoonHour) {
                d.savedWeapons = d.weapons; d.savedCur = d.cur;
                d.weapons = {NewHeld(hc, false)}; d.cur = 0; d.harpoonHour = true; d.reloading = false;
            }
            break;
        }
        default: break;
    }
}

// ---------------------------------------------------------------- salvage
bool Match::PlaceOnePart(SalvagePart& sp) {
    std::vector<char> open(map->links.size(), 1);
    std::vector<int> zones;
    for (int zi = 0; zi < (int)map->zones.size(); zi++) {
        const Zone& z = map->zones[zi];
        if (!z.diverOk || zi == level.startZone || (zi < (int)voidZone.size() && voidZone[zi])) continue;
        bool twin = false;   // (a build's parts lie in different rooms where the map allows)
        for (const auto& o : salvage) if (&o != &sp && o.build == sp.build && o.zone == zi && !o.taken) twin = true;
        if (!twin) zones.push_back(zi);
    }
    if (zones.empty()) for (int zi = 0; zi < (int)map->zones.size(); zi++) if (map->zones[zi].diverOk) zones.push_back(zi);
    for (int tries = 0; tries < 60 && !zones.empty(); tries++) {
        int zi = zones[(int)(Rand() * zones.size()) % zones.size()];
        const Zone& z = map->zones[zi];
        Rectangle b = z.plan;
        if (!z.parts.empty()) { std::vector<Rectangle> vis; for (const auto& p : z.parts) if (!p.hidden) vis.push_back(p.r); if (!vis.empty()) b = vis[(int)(Rand() * vis.size()) % vis.size()]; }
        Vector3 p{b.x + 1 + (b.width - 2) * Rand(), z.y0 + 0.6f, b.y + 1 + (b.height - 2) * Rand()};
        if (!level.Inside(p, 0.4f, open)) continue;
        bool nearStation = false;
        for (const auto& st : level.stations) if (Vector3Distance(st.pos, p) < 2) nearStation = true;
        if (nearStation) continue;
        sp.pos = p; sp.zone = zi; sp.taken = false; sp.carrier = -1; sp.respawnT = 0;
        return true;
    }
    return false;
}
void Match::PlaceSalvage() {
    salvage.clear();
    for (int b = 1; b < (int)BuildType::COUNT; b++) for (int k = 0; k < 3; k++) {
        SalvagePart sp; sp.build = b; sp.part = k;
        salvage.push_back(sp);
        if (!PlaceOnePart(salvage.back())) salvage.pop_back();
    }
}
bool Match::Powered(const Station& s) const {
    if (power) return true;
    for (const auto& dp : deployed) if (dp.alive && !dp.stopped && dp.type == BuildType::Turbine && Vector3Distance(dp.pos, s.pos) <= 12) return true;
    return false;
}
void Match::CycleBench(int di) { DiverState& d = divers[di]; d.benchSel = (d.benchSel + 1) % BENCH_ITEMS; }
bool Match::UseBuild(int di) {
    DiverState& d = divers[di];
    if (d.dead || d.downed || d.build == BuildType::None) return false;
    Vector3 f = Forward(d); f.y = 0;
    if (Vector3Length(f) < 0.01f) f = {0, 0, 1};
    f = Vector3Normalize(f);
    if (d.build == BuildType::ShellShield) {
        // the bash: whatever is in front within 2.5 m is knocked back and stunned
        if (d.bashCd > 0) return false;
        d.bashCd = 4;
        bool any = false;
        for (int i = 0; i < (int)eco.agents.size(); i++) {
            Agent& a = eco.agents[i];
            if (!a.alive || a.diver >= 0) continue;
            Vector3 to = Vector3Subtract(a.pos, d.pos);
            float dist = Vector3Length(to);
            if (dist > 2.5f + bodies[a.sp].radius || Vector3DotProduct(Vector3Scale(to, 1 / std::max(dist, 0.01f)), f) < 0.3f) continue;
            a.stun = std::max(a.stun, IsBoss(i) ? 0.5f : 1.5f);
            if (!IsBoss(i)) a.pos = map->zones[a.zone].Clamp(Vector3Add(a.pos, Vector3Scale(f, 3)), 0.4f);
            any = true;
        }
        fx.push_back({8, Vector3Add(d.pos, f), f});
        return any;
    }
    Deployed dp; dp.type = d.build; dp.owner = d.slot; dp.dir = f;
    const Zone& z = map->zones[std::clamp(eco.ZoneAt(d.pos) >= 0 ? eco.ZoneAt(d.pos) : d.zone, 0, (int)map->zones.size() - 1)];
    dp.pos = level.Move(d.pos, Vector3Add(d.pos, f), 0.4f, linkOpen);
    switch (d.build) {
        case BuildType::Turbine: dp.t = 90; break;
        case BuildType::NetTripwire: dp.t = 240; dp.pos.y = z.y0 + 0.5f; break;
        case BuildType::DecoyBuoy: dp.t = 45; break;
        case BuildType::BubbleWall: {
            dp.t = 60;
            dp.pos = level.Move(d.pos, Vector3Add(d.pos, Vector3Scale(f, 3)), 0.4f, linkOpen);
            Vector3 side{-f.z, 0, f.x};
            Ecosystem::Curtain c; c.a = Vector3Add(dp.pos, Vector3Scale(side, -4)); c.b = Vector3Add(dp.pos, Vector3Scale(side, 4)); c.y0 = z.y0 - 1; c.y1 = z.y1 + 1; c.t = 60; c.maxSize = 2;
            eco.curtains.push_back(c);
            break;
        }
        default: break;
    }
    deployed.push_back(dp);
    fx.push_back({4, dp.pos, {0, 0, 0}});
    d.build = BuildType::None;
    return true;
}
bool Match::UseBrush(int di) {
    DiverState& d = divers[di];
    if (d.dead || d.downed || !d.brush) return false;
    auto parasites = [&](const DiverState& o) { int n = 0; if (o.agent >= 0) for (const auto& a : eco.agents) if (a.alive && a.host == o.agent) n++; return n; };
    int best = -1; float bd = 2.5f;
    for (int i = 0; i < (int)divers.size(); i++) {   // a teammate first, then yourself
        const DiverState& o = divers[i];
        if (i == di || o.dead || parasites(o) == 0) continue;
        float dd = Vector3Distance(o.pos, d.pos);
        if (dd < bd) { bd = dd; best = i; }
    }
    if (best < 0 && parasites(d) > 0) best = di;
    if (best < 0) return false;
    DiverState& o = divers[best];
    for (auto& a : eco.agents) if (a.alive && a.host == o.agent) { a.host = -1; a.st = State::Flee; a.goal = a.home; a.stateT = 0; }
    o.slowT = 0; o.slowMult = 1;
    Say("", best == di ? "You scrape the parasites off" : "You scrape the parasites off Diver " + std::to_string(o.slot + 1), 2);
    return true;
}
void Match::UpdateSalvage(float dt) {
    // the parts: turning up again after a build, picked up by swimming over them, dropped where a carrier died
    for (auto& sp : salvage) {
        if (sp.taken && sp.carrier < 0 && sp.respawnT > 0) { sp.respawnT -= dt; if (sp.respawnT <= 0 && !PlaceOnePart(sp)) sp.respawnT = 5; }
        if (sp.carrier >= 0 && sp.carrier < (int)divers.size() && divers[sp.carrier].dead) {
            DiverState& c = divers[sp.carrier];
            c.partsMask &= ~(1 << (sp.build * 3 + sp.part));
            sp.carrier = -1; sp.taken = false;
            bool overVoid = eco.ZoneAt(c.pos) >= 0 && eco.ZoneAt(c.pos) < (int)voidZone.size() && voidZone[eco.ZoneAt(c.pos)];
            if (overVoid || c.pos.y < -1e8f) PlaceOnePart(sp); else { sp.pos = c.pos; sp.zone = eco.ZoneAt(c.pos); }
        }
        if (sp.taken) continue;
        for (auto& d : divers) {
            int bit = 1 << (sp.build * 3 + sp.part);
            if (d.dead || d.downed || (d.partsMask & bit) || Vector3Distance(d.pos, sp.pos) > 1.4f) continue;
            d.partsMask |= bit; sp.taken = true; sp.carrier = d.slot;
            int n = 0; for (int k = 0; k < 3; k++) if (d.partsMask & (1 << (sp.build * 3 + k))) n++;
            fx.push_back({4, sp.pos, {0, 0, 0}});
            Say("", TextFormat("Salvage: %s (%s, %d of 3)%s", Build((BuildType)sp.build).parts[sp.part], Build((BuildType)sp.build).name, n, n == 3 ? ": take them to a workbench" : ""), 3);
            break;
        }
    }
    for (auto& d : divers) if (d.bashCd > 0) d.bashCd -= dt;
    // what's been set down
    for (auto& dp : deployed) {
        if (!dp.alive) continue;
        dp.t -= dt;
        if (dp.type == BuildType::Turbine && !dp.stopped) {
            // "a shock stops a Turbine" (the electric ray)
            for (const auto& a : eco.agents) if (a.alive && a.diver < 0 && (HasW(map->species[a.sp].name, "electric") || map->species[a.sp].Has("electric")) && Vector3Distance(a.pos, dp.pos) < 4) {
                dp.stopped = true; fx.push_back({6, a.pos, Vector3Subtract(dp.pos, a.pos)}); Say(map->species[a.sp].name, "shocks the Turbine dead", 3); break;
            }
        }
        if (dp.type == BuildType::NetTripwire && dp.held < 0) {
            for (int i = 0; i < (int)eco.agents.size(); i++) {
                Agent& a = eco.agents[i];
                const Species& sp = map->species[a.sp];
                if (!a.alive || a.diver >= 0 || sp.isEnemy || IsBoss(i) || sp.size > 3 || sp.Sessile() || Vector3Distance(a.pos, dp.pos) > 1.5f + bodies[a.sp].radius) continue;
                dp.held = i; dp.t = 10; a.held = 10; a.stun = std::max(a.stun, 1.0f);
                eco.AddNoise(dp.pos, 3);   // (the bell)
                Say("", "The tripwire's bell: the net holds a " + sp.name, 3);
                break;
            }
        }
        if (dp.type == BuildType::NetTripwire && dp.held >= 0 && dp.held < (int)eco.agents.size() && eco.agents[dp.held].alive) { Agent& a = eco.agents[dp.held]; a.pos = Vector3Lerp(a.pos, dp.pos, std::min(1.0f, dt * 4)); a.held = std::max(a.held, 0.2f); }
        if (dp.type == BuildType::DecoyBuoy) {
            for (int i = 0; i < (int)eco.agents.size(); i++) {
                Agent& a = eco.agents[i];
                if (!a.alive || a.diver >= 0 || IsBoss(i) || map->species[a.sp].Sessile() || Vector3Distance(a.pos, dp.pos) > 35) continue;
                if (a.st == State::Flee || a.st == State::Feed) continue;
                a.st = State::Investigate; a.goal = dp.pos; a.stateT = 0; a.target = -1;
            }
        }
        if (dp.t <= 0) { dp.alive = false; if (dp.held >= 0 && dp.held < (int)eco.agents.size()) eco.agents[dp.held].held = 0; }
    }
    deployed.erase(std::remove_if(deployed.begin(), deployed.end(), [](const Deployed& x) { return !x.alive; }), deployed.end());
    // flares: the curious come to the light, the light-shy (nocturnal) flee it, and a camouflaged ambusher in it shows itself
    for (auto& fl : flareLights) {
        fl.t -= dt;
        for (int i = 0; i < (int)eco.agents.size(); i++) {
            Agent& a = eco.agents[i];
            const Species& sp = map->species[a.sp];
            if (!a.alive || a.diver >= 0 || IsBoss(i) || sp.Sessile() || sp.isEnemy) continue;
            float dd = Vector3Distance(a.pos, fl.pos);
            if (dd > 20) continue;
            if (sp.Has("nocturnal")) {
                Vector3 away = Vector3Subtract(a.pos, fl.pos); if (Vector3Length(away) < 0.1f) away = {1, 0, 0};
                a.st = State::Flee; a.goal = map->zones[a.zone].Clamp(Vector3Add(a.pos, Vector3Scale(Vector3Normalize(away), 12)), 0.4f); a.stateT = 0; a.target = -1;
            } else if (sp.Has("camouflage") && a.st == State::Rest && dd < 15) { a.st = State::Graze; a.stateT = 0; }
            else if (sp.curiosity >= 0.5f && (a.st == State::Graze || a.st == State::Rest || a.st == State::Return)) { a.st = State::Investigate; a.goal = fl.pos; a.stateT = 0; }
        }
    }
    flareLights.erase(std::remove_if(flareLights.begin(), flareLights.end(), [](const FlareLight& f) { return f.t <= 0; }), flareLights.end());
}

void Match::GiveKey(const std::string& name, int di, Vector3 at) {
    if (di < 0 || di >= (int)divers.size() || divers[di].dead) {
        float bd = 1e9f; di = -1;
        for (int i = 0; i < (int)divers.size(); i++) if (!divers[i].dead) { float dd = Vector3Distance(divers[i].pos, at) + (divers[i].downed ? 1000 : 0); if (dd < bd) { bd = dd; di = i; } }
    }
    keys.insert(name);
    keyHolder[name] = di;
}
void Match::DropKeys(DiverState& d, Vector3 at) {
    for (auto it = keyHolder.begin(); it != keyHolder.end(); ++it) {
        if (it->second != d.slot || !keys.count(it->first)) continue;
        keys.erase(it->first); it->second = -1;
        floorKeys.push_back({it->first, at});
        Say("", "The " + it->first + " key sinks where Diver " + std::to_string(d.slot + 1) + " fell", 4);
    }
}
void Match::UpdateDrops(float dt) {
    for (auto& k : floorKeys) for (auto& d : divers) if (!d.dead && !d.downed && Vector3Distance(d.pos, k.pos) < 1.4f) {
        keys.insert(k.name); keyHolder[k.name] = d.slot; fx.push_back({4, k.pos, {0, 0, 0}});
        Say("", "Diver " + std::to_string(d.slot + 1) + " has the " + k.name + " key", 3);
        k.name.clear(); break;
    }
    floorKeys.erase(std::remove_if(floorKeys.begin(), floorKeys.end(), [](const FloorKey& k) { return k.name.empty(); }), floorKeys.end());
    for (auto& f : drops) {
        f.t -= dt;
        if (f.t <= 0) { f.alive = false; continue; }
        if (f.weapon >= 0) continue;                           // guns are taken with Interact
        for (auto& d : divers) if (!d.dead && !d.downed && Vector3Distance(d.pos, f.pos) < 1.4f) {
            ApplyDrop(f.type, f.pos); fx.push_back({4, f.pos, {0, 0, 0}}); f.alive = false; break;
        }
    }
    drops.erase(std::remove_if(drops.begin(), drops.end(), [](const FloorDrop& f) { return !f.alive; }), drops.end());
}

// ---------------------------------------------------------------- stations, doors, revives
void Match::GiveWeapon(DiverState& d, int def, bool forged) {
    if (def < 0) return;
    if (d.harpoonHour) { d.savedWeapons.push_back(NewHeld(def, forged)); if ((int)d.savedWeapons.size() > d.slots) d.savedWeapons.erase(d.savedWeapons.begin() + d.savedCur); return; }
    for (int i = 0; i < (int)d.weapons.size(); i++) if (d.weapons[i].def == def) {
        Held& h = d.weapons[i]; const WeaponDef& w = W(h);
        h.mag = (int)MagMax(w, h); h.reserve = (int)ResMax(w, h); d.cur = i;
        return;
    }
    if ((int)d.weapons.size() < d.slots) { d.weapons.push_back(NewHeld(def, forged)); d.cur = (int)d.weapons.size() - 1; }
    else d.weapons[d.cur] = NewHeld(def, forged);
    d.reloading = false; d.burstLeft = 0;
    if (Weapons().weapons[def].id == WonderId()) wonderHeld = true;
}

void Match::GiveLockerWeapon(DiverState& d) {
    // the Locker deals from its pool plus every rack weapon and the map's wonder weapon
    // (2% per pull, guaranteed within 12 pulls if not yet held by the team)
    const WeaponsData& WD = Weapons();
    int wonder = WD.Index(WonderId());
    if (wonder >= 0 && !wonderHeld && (teamPulls >= 12 || Rand() < 0.02f)) { GiveWeapon(d, wonder, d.luckyLocker); d.luckyLocker = false; d.lastKill = "Davy's Locker: " + WD.weapons[wonder].name; d.lastKillT = 3; Quip("Locker wonder", d.slot, 0.8f); return; }
    std::vector<int> pool;
    for (int i = 0; i < (int)WD.weapons.size(); i++) {
        const WeaponDef& w = WD.weapons[i];
        if (w.source != "locker" && w.source != "rack") continue;
        bool have = false;
        for (const auto& h : d.weapons) if (h.def == i) have = true;
        if (!have) pool.push_back(i);
    }
    if (pool.empty()) return;
    int pick = pool[(int)(Rand() * pool.size()) % pool.size()];
    GiveWeapon(d, pick, d.luckyLocker);                   // (Lucky Locker: out of the Forge)
    if (d.luckyLocker) {
        d.luckyLocker = false; Say("", "Lucky Locker: it comes out pressure-forged", 3);
        int na = (int)Weapons().forgeAmmoTypes.size();
        for (auto& h : d.weapons) if (h.def == pick && h.forged && na > 0) h.altAmmo = (int)(Rand() * na) % na;   // (with an alternate ammunition)
    }
    d.lastKill = "Davy's Locker: " + WD.weapons[pick].name; d.lastKillT = 3;
    if (WD.weapons[pick].source == "rack" && !d.bot) Quip("Locker bad", d.slot, 0.8f);   // a rack gun out of the Locker: money down the drain
}

bool Match::LockerLiveAt(const Station& s) const {
    if (s.type != StationType::Locker) return false;
    if (fireSaleT > 0) return !s.needsPower || Powered(s);     // Fire Sale: it appears at every location
    return s.lockerSpot == lockerSpot;
}

int Match::NearestStation(Vector3 p, float r) const {
    int best = -1; float bd = r;
    for (int i = 0; i < (int)level.stations.size(); i++) {
        const Station& s = level.stations[i];
        if (s.type == StationType::Feature || s.type == StationType::Hazard || s.type == StationType::Entry || s.type == StationType::Boss) continue;
        if (s.type == StationType::Locker && !LockerLiveAt(s)) continue;
        if (s.type == StationType::QuestStep) {
            const Json& qs = map->extra["quests"];
            if (qs.IsArr() && !qs.a.empty()) {
                int c = QuestChainOf(s.step);
                if (c < 0) continue;
                bool source = qs.a[c]["steps"][std::to_string(s.step)]["kind"].Str0() == "carry";   // the spark's crystal: always there
                if (!source && (c >= (int)questAt.size() || questAt[c] != s.step)) continue;
            } else if (s.step != questStep + 1) continue;
        }
        float d = Vector3Distance(s.pos, p);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

int Match::NearestDoor(Vector3 p, float r) const {
    int best = -1; float bd = r;
    for (int i = 0; i < (int)level.doors.size(); i++) {
        const Door& d = level.doors[i];
        if (d.open || map->links[d.link].opensWith >= 0) continue;
        const Link& l = map->links[d.link];
        float dist = SegPointDist(l.a, l.b, p);
        if (dist < bd) { bd = dist; best = i; }
    }
    return best;
}

static int ForgePrice(const Held& h) { return (int)(h.forged ? Weapons().reroll : Weapons().forgePrice); }

std::string Match::PromptFor(int di, int* cost) const {
    const DiverState& d = divers[di];
    if (cost) *cost = 0;
    if (d.dead) return "";
    if (d.downed) return d.selfReviveT > 0 ? "Quick Brine: getting back up..." : "Downed: hold on for a teammate";
    if (d.heldT > 0 && d.holder <= -2) return "Reacher coral! Shoot or knife it (V), or tap E to hack free";
    if (d.heldT > 0) return d.holdLethal ? "Held! Tap E hard (or get it shot off)" : "Tap E to struggle free";
    for (const auto& o : divers) if (&o != &d && o.downed && Vector3Distance(o.pos, d.pos) < 1.8f) return "Hold E to revive diver " + std::to_string(o.slot + 1);
    for (const auto& f : drops) if (f.weapon >= 0 && Vector3Distance(f.pos, d.pos) < 1.6f) return "E: take the " + Weapons().weapons[f.weapon].name;
    for (const auto& f : drops) if (f.weapon == -2 && Vector3Distance(f.pos, d.pos) < 1.6f) return "E: take the Shaman's drum";
    for (const auto& o : divers) if (&o != &d && o.heldT > 0 && o.holder <= -2 && Vector3Distance(o.pos, d.pos) < 1.8f) return "Hold E to cut diver " + std::to_string(o.slot + 1) + " free";
    if (!d.ichorJar && map->extra["quests"].IsArr()) for (const auto& ic : ichor) if (Vector3Distance(ic.pos, d.pos) < 2.2f) {
        const Json& qs = map->extra["quests"];
        for (int c = 0; c < (int)qs.a.size() && c < (int)questAt.size(); c++) for (const auto& kv : qs.a[c]["steps"].o) if (kv.second["kind"].Str0() == "ichor" && questAt[c] <= std::stoi(kv.first)) return "E: fill a jar with the Lost One's ichor";
    }
    for (int i = 0; i < (int)eco.flora.size(); i++) {
        const FloraPatch& fp = eco.flora[i];
        float heal = map->extra["flora_rules"][map->flora[fp.flora].name]["edible"].F(0);
        if (heal > 0 && fp.units > 0 && Vector3Distance(fp.pos, d.pos) < 1.8f) return "E: eat the " + map->flora[fp.flora].name + " (+" + std::to_string((int)heal) + " HP)";
    }
    int si = NearestStation(d.pos, 2.0f);
    int doorI = NearestDoor(d.pos, 2.2f);
    if (doorI >= 0 && (si < 0 || SegPointDist(map->links[level.doors[doorI].link].a, map->links[level.doors[doorI].link].b, d.pos) < Vector3Distance(level.stations[si].pos, d.pos))) {
        const Door& dr = level.doors[doorI];
        if (cost) *cost = dr.cost;
        return "E: clear the " + dr.name + " (" + std::to_string(dr.cost) + ")";
    }
    if (si < 0) return "";
    const Station& s = level.stations[si];
    const WeaponsData& WD = Weapons();
    if (s.needsPower && !Powered(s)) return s.name + ": needs power";
    switch (s.type) {
        case StationType::Rack: {
            if (s.weapon < 0) return "";
            const WeaponDef& w = WD.weapons[s.weapon];
            for (const auto& h : d.weapons) if (h.def == s.weapon) { int c = h.forged ? (int)WD.rearm : w.price / 2; if (cost) *cost = c; return "E: " + w.name + " ammo (" + std::to_string(c) + ")"; }
            if (cost) *cost = w.price;
            return "E: take the " + w.name + " (" + std::to_string(w.price) + ")";
        }
        case StationType::Tonic: {
            const TonicDef* t = WD.Tonic(s.tonic);
            if (!t) return "";
            if (d.tonics.count(t->id)) return t->name + " (drunk)";
            int c = t->id == "quick" && players == 1 && t->priceSolo > 0 ? t->priceSolo : t->price;
            if (cost) *cost = c;
            return "E: " + t->name + ": " + t->effect + " (" + std::to_string(c) + ")";
        }
        case StationType::Locker: { int c = fireSaleT > 0 ? WD.fireSalePull : WD.lockerPull; if (cost) *cost = c; return "E: Davy's Locker (" + std::to_string(c) + ")"; }
        case StationType::Forge: { int c = ForgePrice(Cur(d)); if (cost) *cost = c; return std::string("E: ") + (Cur(d).forged ? "re-roll the Forge's ammunition" : "pressure-forge the " + W(Cur(d)).name) + " (" + std::to_string(c) + ")"; }
        case StationType::Power: return power ? (bossActive && !bossStunUsed ? "E: cycle the power (stuns the Goliath)" : "Power is on") : "E: throw the power switch";
        case StationType::Trap: {
            const Json* tjp = &map->extra["trap"];
            for (const Json& tr : map->extra["traps"].a) if (s.name.find(tr["match"].Str0()) != std::string::npos) tjp = &tr;
            if (tjp->IsObj() && (*tjp)["kind"].Str0() == "beacon") return (beaconOn.count(si) && beaconOn.at(si)) ? (*tjp)["prompt"].Str0() : s.name + ": dark (pointing at the abyss)";
            if (trapT > 0) return tjp->IsObj() ? s.name + ": resetting" : "The crane is winding back";
            if (cost) *cost = 1000;
            if (tjp->IsObj() && tjp->Has("prompt")) return (*tjp)["prompt"].Str0();
            if (tjp->IsObj()) return "E: spring the " + s.name + " (1000)";
            return "E: drop the cargo crane's container (1000)";
        }
        case StationType::Quest:
            if (map->extra["quest_altar"].IsObj()) return safeOpen ? "The altar is quiet" : nesting ? TextFormat("The turtles are nesting: %d of 5 (hold the beach)", nests) : d.drumUses > 0 ? (d.drumClean ? "E: lay the Shaman's drum on the altar" : "The altar won't take a drum that's been beaten") : "An altar on the turtles' beach (something belongs here)";
            if (s.name.find("lantern") != std::string::npos) return lanternsOut.count(si) ? "The lantern is out" : d.inkCaps > 0 ? "E: put the lantern out with an ink cap" : "A Drowned lantern, still burning (an ink cap would put it out)";
            if (s.name.find("log") != std::string::npos) return logRead ? "The captain's log (read)" : tide <= map->extra["hidden_quest"]["log_after_tide"].I(3) ? "A logbook, its pages blank for now" : "E: read the captain's log (chalk marks on the cover)";
            if (openSt == si && openT > 0) return TextFormat("Opening the safe: %.0f s (the bell is ringing)", std::max(0.0f, map->extra["hidden_quest"]["open_s"].F(20) - openT));
            return safeOpen ? "The captain's safe (open)" : !logRead ? "The captain's safe (whose keys? the log would say)" : "The captain's safe: " + std::to_string(keys.size()) + " of 3 keys" + (keys.size() >= 3 ? ". Hold E: open it (20 s)" : "");
        case StationType::Cleaning: return "E: the cleaner shrimp scrape off parasites";
        case StationType::Workbench: {
            for (int b = 1; b < (int)BuildType::COUNT; b++) {
                int set = 7 << (b * 3);
                if ((d.partsMask & set) == set) return d.build != BuildType::None ? std::string("Workbench: you already carry the ") + Build(d.build).name : std::string("E: build the ") + Build((BuildType)b).name;
            }
            int item = std::clamp(d.benchSel, 0, BENCH_ITEMS - 1);
            int have = item == 0 ? d.inkBombs : item == 1 ? d.chumBags : item == 2 ? d.flares : (d.brush ? 2 : 0);
            std::string parts;
            for (int b = 1; b < (int)BuildType::COUNT; b++) { int n = 0; for (int k = 0; k < 3; k++) if (d.partsMask & (1 << (b * 3 + k))) n++; if (n) parts += TextFormat("  [%s %d/3]", Build((BuildType)b).name, n); }
            if (have >= 2) return std::string("Workbench: you have all the ") + (item == 3 ? "brushes" : TacticalName(item + 1)) + " you can carry (Z: next)" + parts;
            if (cost) *cost = BenchPrice(item);
            return TextFormat("E: %s (%d)   Z: next item", BenchName(item), BenchPrice(item)) + parts;
        }
        case StationType::Cache: {
            const Json& bk = map->extra["boss_key"];
            if (cacheOpen) return "The cache is empty";
            if (!keys.count(bk["key"].Str0())) return "A locked crate (the boss holds its key)";
            if (map->extra["hidden_quest"]["lanterns"].I(0) > 0 && (int)lanternsOut.size() < map->extra["hidden_quest"]["lanterns"].I(0)) return TextFormat("A locked crate: the key, and the expedition's log (%d of 3 pages)", (int)lanternsOut.size());
            if (map->extra["hidden_quest"]["lanterns"].I(0) > 0 && phase != TidePhase::Tide) return "The crate won't open in the calm: wait for the tide";
            if (openSt == si && openT > 0) return TextFormat("Opening the crate: %.0f s (the Drowned are coming)", std::max(0.0f, map->extra["hidden_quest"]["open_s"].F(15) - openT));
            return "E: open the crate with the " + bk["key"].Str0() + " key";
        }
        case StationType::QuestStep: {
            if (map->extra["quests"].IsArr() && !map->extra["quests"].a.empty()) {
                int c = QuestChainOf(s.step);
                if (c < 0) return "";
                const Json& st = map->extra["quests"].a[c]["steps"][std::to_string(s.step)];
                std::string k = st["kind"].Str0();
                if (k == "carry") return d.spark ? "The crystal hums (you carry its spark)" : st["prompt"].Str0();
                if (k == "spark" && !d.spark) return s.name + ": needs the treasury crystal's spark";
                if (k == "ichor" && !d.ichorJar) return s.name + ": needs a Lost One's ichor";
                if (k == "ichor" && wyrmState != 0) return "The Wyrm is up: wait until it's below";
                if (k == "key" && !keys.count(st["key"].Str0())) return s.name + ": locked (the " + st["key"].Str0() + " key)";
                if (k == "egg" && !keys.count(st["key"].Str0())) return s.name + ": locked (the station's master key)";
                if (k == "place_egg" && !d.egg) return s.name + ": empty (something belongs here)";
                if (k == "ledge" || k == "vent") return st["prompt"].Str0();
                if (k == "hold") return st["prompt"].Str0() + TextFormat(" (%.0f s)", holdT);
                return st["prompt"].Str0();
            }
            if (s.step != questStep + 1 && !(s.step == 3 && questStep == 2)) return "";
            if (s.step == 1) return "E: read the oil-stained log";
            if (s.step == 2) return "E: take the brass steam whistle";
            if (power) return "The boiler's too hot to whistle: the power is on";
            return "E: pull the whistle cord (" + std::to_string(whistlePulls) + "/3)";
        }
        default: return "";
    }
}

bool Match::Interact(int di, bool hold, float dt) {
    DiverState& d = divers[di];
    if (d.dead || d.downed) return false;
    if (d.heldT > 0) { if (!hold) { d.struggle++; return true; } return false; }
    // a teammate in Reacher coral: hold to cut them free
    for (auto& o : divers) if (&o != &d && o.heldT > 0 && o.holder <= -2 && Vector3Distance(o.pos, d.pos) < 1.8f) { o.cutT += dt * 1.5f + (hold ? 0 : 0.2f); return true; }
    // revives are held (4 s; 2 s with Quick Brine)
    for (auto& o : divers) if (&o != &d && o.downed && Vector3Distance(o.pos, d.pos) < 1.8f) {
        o.reviveT += dt;
        o.reviveTouchT = 0.25f;
        float need = Engine().C("revive_s", 4) * (d.tonics.count("quick") ? 0.5f : 1.0f);
        if (o.reviveT >= need) Revive(o, d.slot);
        return true;
    }
    {
        // a long open in progress (the captain's safe, the Lantern Cache's crate): held E, or bots standing at it
        int si0 = NearestStation(d.pos, 2.0f);
        if (si0 >= 0 && si0 == openSt) { const Station& s0 = level.stations[si0]; return LongOpen(d, si0, s0.type == StationType::Cache ? map->extra["hidden_quest"]["open_s"].F(15) : map->extra["hidden_quest"]["open_s"].F(20), dt); }
    }
    if (hold) return false;
    const WeaponsData& WD = Weapons();
    for (auto& f : drops) if (f.alive && f.weapon >= 0 && Vector3Distance(f.pos, d.pos) < 1.6f) { GiveWeapon(d, f.weapon); f.alive = false; return true; }
    for (auto& f : drops) if (f.alive && f.weapon == -2 && Vector3Distance(f.pos, d.pos) < 1.6f) { d.drumUses = 5; d.drumClean = true; f.alive = false; Say("", "The Shaman's drum: carry it to the turtles' altar without a beat (F beats it: 5 beats; the fifth calls the Matriarch)", 5); return true; }
    // a Lost One's ichor into a jar (the Tide Staff's offering)
    if (!d.ichorJar) for (auto& ic : ichor) if (Vector3Distance(ic.pos, d.pos) < 2.2f) {
        bool wanted = false;
        const Json& qs = map->extra["quests"];
        for (int c = 0; c < (int)qs.a.size() && c < (int)questAt.size(); c++) for (const auto& kv : qs.a[c]["steps"].o) if (kv.second["kind"].Str0() == "ichor" && questAt[c] <= std::stoi(kv.first)) wanted = true;
        if (!wanted) break;
        d.ichorJar = true; Say("", "A jar of black ichor", 2); return true;
    }
    // the Cave's ink caps, picked to put out the Drowned's lanterns
    if (map->extra["hidden_quest"]["lanterns"].I(0) > 0 && d.inkCaps < 3) for (int i = 0; i < (int)eco.flora.size(); i++) {
        FloraPatch& fp = eco.flora[i];
        if (map->flora[fp.flora].name != "Ink cap" || fp.units <= 0 || Vector3Distance(fp.pos, d.pos) > 1.8f) continue;
        d.inkCaps++; fp.units = std::max(0.0f, fp.units - 0.34f); fp.regrowT = 0;   // (three caps to a patch)
        Say("", TextFormat("An ink cap in your pouch (%d)", d.inkCaps), 2);
        return true;
    }
    // edible flora (the Reef's sea grape): +10 HP, once per cluster
    for (int i = 0; i < (int)eco.flora.size(); i++) {
        FloraPatch& fp = eco.flora[i];
        float heal = map->extra["flora_rules"][map->flora[fp.flora].name]["edible"].F(0);
        if (heal <= 0 || fp.units <= 0 || Vector3Distance(fp.pos, d.pos) > 1.8f) continue;
        d.hp = std::min(d.hpMax, d.hp + heal); fp.units = 0; fp.regrowT = 0;
        Say("", map->flora[fp.flora].name + ": +" + std::to_string((int)heal) + " HP", 2);
        return true;
    }
    int si = NearestStation(d.pos, 2.0f);
    int doorI = NearestDoor(d.pos, 2.2f);
    if (doorI >= 0 && (si < 0 || SegPointDist(map->links[level.doors[doorI].link].a, map->links[level.doors[doorI].link].b, d.pos) < Vector3Distance(level.stations[si].pos, d.pos))) {
        Door& dr = level.doors[doorI];
        if (d.scrip < dr.cost) return false;
        d.scrip -= dr.cost;
        dr.open = true;
        linkOpen[dr.link] = 1;
        for (auto& o : level.doors) if (map->links[o.link].opensWith == dr.link) { o.open = true; linkOpen[o.link] = 1; }
        doorsOpened++;
        Say("", "The " + dr.name + " is clear", 3);
        return true;
    }
    if (si < 0) return false;
    const Station& s = level.stations[si];
    if (s.needsPower && !Powered(s)) return false;
    auto pay = [&](int c) { if (d.scrip < c) return false; d.scrip -= c; return true; };
    switch (s.type) {
        case StationType::Rack: {
            if (s.weapon < 0) return false;
            const WeaponDef& w = WD.weapons[s.weapon];
            for (auto& h : d.weapons) if (h.def == s.weapon) {
                if (!pay(h.forged ? (int)WD.rearm : w.price / 2)) return false;
                h.mag = (int)MagMax(w, h); h.reserve = (int)ResMax(w, h);
                return true;
            }
            if (!pay(w.price)) return false;
            GiveWeapon(d, s.weapon);
            return true;
        }
        case StationType::Tonic: {
            const TonicDef* t = WD.Tonic(s.tonic);
            if (!t || d.tonics.count(t->id) || d.tonics.size() >= 4) return false;
            bool soloQuick = t->id == "quick" && players == 1;
            if (soloQuick && d.quickBought >= 3) return false;   // three per match
            if (!pay(soloQuick && t->priceSolo > 0 ? t->priceSolo : t->price)) return false;
            d.tonics.insert(t->id);
            if (t->id == "juggernaut") { d.hpMax = 250; d.hp = std::max(d.hp, d.hpMax * (d.hp / 100.0f)); }
            if (t->id == "mule") d.slots = 3;
            if (soloQuick) { d.quickBought++; d.selfRevives++; }
            Say("", t->name + ": " + t->effect, 3);
            Quip("Tonic bought", Living() > 1 ? -2 - d.slot : d.slot, 1.5f);
            return true;
        }
        case StationType::Locker: {
            if (!LockerLiveAt(s)) return false;
            if (!pay(fireSaleT > 0 ? WD.fireSalePull : WD.lockerPull)) return false;
            lockerPulls++; teamPulls++;
            // "a moray eel inside shuffles the guns and, after 8-12 pulls, snaps the chest shut and it sinks to another location"
            int spots = power ? lockerSpots : 1;
            if (fireSaleT <= 0 && lockerPulls >= lockerMoveAt && spots > 1) {
                d.scrip += WD.lockerPull;
                lockerSpot = (lockerSpot + 1 + (int)(Rand() * (spots - 1))) % spots;
                lockerPulls = 0; lockerMovedT = 6; Quip("Locker bad", d.slot, 0.8f);
                lockerMoveAt = WD.lockerMoveMin + (int)(Rand() * (WD.lockerMoveMax - WD.lockerMoveMin + 1));
                for (const auto& o : level.stations) if (o.type == StationType::Locker && o.lockerSpot == lockerSpot) Say("Davy's Locker", "The moray snaps the chest shut. It sinks away to the " + map->zones[o.zone].name + " (a lantern buoy marks it).", 5);
                return true;
            }
            GiveLockerWeapon(d);
            return true;
        }
        case StationType::Forge: {
            if (bossActive && bossPhase >= 2 && bossAgent >= 0 && Vector3Distance(eco.agents[bossAgent].pos, s.pos) < 8) { Say("", "The Goliath is too close to the Forge", 2); return false; }
            Held& h = Cur(d);
            const WeaponDef& w = W(h);
            if (w.source == "drop" || d.harpoonHour) return false;
            if (!pay(ForgePrice(h))) return false;
            h.forged = true;
            if (forgeAt < 0) forgeAt = time;
            h.altAmmo = (int)(Rand() * WD.forgeAmmoTypes.size()) % std::max(1, (int)WD.forgeAmmoTypes.size());
            h.mag = (int)MagMax(w, h); h.reserve = (int)ResMax(w, h);
            Quip("Forge", d.slot, 1.2f);
            Say("The Pressure Forge", (w.forged.empty() ? w.name + " (forged)" : w.forged) + ", with " + (WD.forgeAmmoTypes.empty() ? "" : WD.forgeAmmoTypes[h.altAmmo]) + " rounds", 4);
            return true;
        }
        case StationType::Power:
            if (!power) {
                power = true;
                // "Turning on power also wakes the ecosystem's larger predators"
                for (auto& a : eco.agents) if (a.alive && a.diver < 0 && map->species[a.sp].tier >= 4 && map->species[a.sp].tier < 5) { a.hunger = std::max(a.hunger, 0.8f); a.fedT = 0; }
                Say("", "Power: the Forge, the second Locker spot, the far tonic machines and the crane are live. Something big stirs.", 5);
                return true;
            }
            if (bossActive && !bossStunUsed && bossAgent >= 0) { bossStunUsed = true; eco.agents[bossAgent].stun = 2; Say("", "The lights cycle: the Goliath is stunned", 3); return true; }
            return false;
        case StationType::Trap: {
            const Json* tjp = &map->extra["trap"];
            for (const Json& tr : map->extra["traps"].a) if (s.name.find(tr["match"].Str0()) != std::string::npos) tjp = &tr;
            if ((*tjp)["kind"].Str0() == "beacon") {
                // a lure beacon: turned toward the abyss (off) it pulls nothing onto anyone; turning it is loud
                if (!beaconOn[si]) return false;
                beaconOn[si] = false; eco.AddNoise(s.pos, 6);
                Say("", "The beacon turns toward the abyss", 2);
                return true;
            }
            if (trapT > 0 || !pay(1000)) return false;
            trapT = 30;
            const Json& tj = *tjp;
            if (tj["kind"].Str0() == "beacons") { for (auto& kv : beaconOn) kv.second = false; Say("", "Every lure beacon goes dark", 3); return true; }
            if (tj["kind"].Str0() == "pressure") {
                // the pressure door slams: everything in the corridor is crushed (Sentinels too)
                Vector3 at = s.pos;
                for (const auto& l : map->links) if (HasW(l.passage, "pressure door")) at = Vector3Lerp(l.a, l.b, 0.5f);
                fx.push_back({5, at, {0, 0, 0}});
                for (int i = 0; i < (int)eco.agents.size(); i++) { const Agent& a = eco.agents[i]; if (a.alive && a.diver < 0 && !IsBoss(i) && Vector3Distance(a.pos, at) < 7) { pendingKiller = d.slot; eco.Kill(i, d.agent); pendingKiller = -1; } }
                for (auto& o : divers) if (!o.dead && !o.downed && Vector3Distance(o.pos, at) < 4) HitDiver(o, 150, "the pressure door", "", at);
                return true;
            }
            if (tj["kind"].Str0() == "vent") {
                // the reactor vent: a scalding jet (200 to everything in 8 m); it scalds the Leviathan for 10%, and bares
                // the Relict's belly (the Abyssal Lure's last step)
                fx.push_back({5, s.pos, {0, 1, 0}});
                bool relict = false;
                for (int i = 0; i < (int)eco.agents.size(); i++) {
                    Agent& a = eco.agents[i];
                    if (!a.alive || a.diver >= 0 || Vector3Distance(a.pos, s.pos) > (IsBoss(i) ? 12.0f : 8.0f)) continue;
                    if (IsBoss(i)) { a.hp = std::max(1.0f, a.hp - a.hpMax * 0.1f); bossGillsT = std::max(bossGillsT, 2.0f); Say(map->species[a.sp].name, "is scalded by the reactor vent", 3); continue; }
                    if (map->species[a.sp].name == "The Relict") { relict = true; a.stun = 3; continue; }
                    HitAgent(&d, i, 200, false, false, {0, 1, 0}, nullptr);
                }
                for (auto& o : divers) if (!o.dead && !o.downed && Vector3Distance(o.pos, s.pos) < 5) HitDiver(o, 40, "the reactor vent", "", s.pos);
                const Json& qs = map->extra["quests"];
                for (int c = 0; c < (int)qs.a.size() && c < (int)questAt.size(); c++) {
                    const Json& st = qs.a[c]["steps"][std::to_string(questAt[c])];
                    if (st["kind"].Str0() == "vent" && relict) QuestAdvance(c, &d);
                }
                return true;
            }
            if (tj["kind"].Str0() == "ledge") { DropLedge(&d); return true; }
            if (tj["kind"].Str0() == "reacher_gate") {
                // a Reacher coral gate closes on the corridor and eats whatever it holds
                float rad = tj["radius"].F(5);
                eco.AddNoise(s.pos, 4);
                fx.push_back({7, s.pos, {0, 1, 0}});
                for (int i = 0; i < (int)eco.agents.size(); i++) {
                    const Agent& a = eco.agents[i];
                    if (!a.alive || a.diver >= 0 || IsBoss(i) || Vector3Distance(a.pos, s.pos) > rad) continue;
                    pendingKiller = d.slot; eco.Kill(i, d.agent); pendingKiller = -1;
                }
                return true;
            }
            if (tj["kind"].Str0() == "surge") {
                // the surge channel flushes the Bommie's top (and flips the Matriarch if she's in the channel)
                int bom = map->ZoneIndex("The Bommie"), ch = s.zone;
                for (int i = 0; i < (int)eco.agents.size(); i++) {
                    Agent& a = eco.agents[i];
                    if (!a.alive || a.diver >= 0) continue;
                    if (IsBoss(i) && a.zone == ch) { bossGillsT = 4; a.stun = 4; Say(map->species[a.sp].name, "is flipped by the surge: her belly is open!", 4); continue; }
                    if (a.zone == bom && a.pos.y > (map->zones[bom].y0 + map->zones[bom].y1) / 2 && !IsBoss(i)) { pendingKiller = d.slot; eco.Kill(i, d.agent); pendingKiller = -1; }
                }
                for (auto& o : divers) if (!o.dead && o.zone == bom && o.pos.y > (map->zones[bom].y0 + map->zones[bom].y1) / 2) HitDiver(o, 20, "the surge", "knockback 6 m", s.pos);
                fx.push_back({5, s.pos, {0, 0, 0}});
                return true;
            }
            if (tj["kind"].Str0() == "sluice") {
                // the canal sluice floods a terrace: a phalanx drowns in its armour; beasts caught in it are thrown about
                float rad = tj["radius"].F(14);
                fx.push_back({5, s.pos, {0, 0, 0}});
                for (int i = 0; i < (int)eco.agents.size(); i++) {
                    Agent& a = eco.agents[i];
                    if (!a.alive || a.diver >= 0 || IsBoss(i) || Vector3Distance(a.pos, s.pos) > rad) continue;
                    if (map->species[a.sp].isEnemy) { pendingKiller = d.slot; eco.Kill(i, d.agent); pendingKiller = -1; }
                    else { HitAgent(&d, i, 100, false, false, {0, 1, 0}, nullptr); if (i < (int)eco.agents.size() && eco.agents[i].alive) eco.agents[i].stun = std::max(eco.agents[i].stun, 3.0f); }
                }
                for (auto& o : divers) if (!o.dead && Vector3Distance(o.pos, s.pos) < rad) o.slowT = std::max(o.slowT, 4.0f);
                return true;
            }
            if (tj["kind"].Str0() == "godpool") {
                // the god-pool drains: if the Wyrm is home it's stranded, gills bare, for 6 s
                fx.push_back({5, s.pos, {0, 0, 0}});
                bool home = bossAgent >= 0 && bossKind == 3 && bossPhase == 1;
                if (home) {
                    int gp = -1; for (int li = 0; li < (int)map->links.size(); li++) if (map->links[li].passage == map->extra["wyrm"]["home"].Str0()) gp = li;
                    if (gp >= 0) {
                        Agent& b = eco.agents[bossAgent];
                        wyrmGrate = gp; wyrmState = 2; wyrmUp = 3; wyrmStruck = true; wyrmT = 6; wyrmStrandT = 6; bossGillsT = 6; bossActive = true;
                        b.pos = Vector3Add(map->links[gp].b, {0, 0.8f, 0}); b.zone = map->links[gp].to; b.vel = {0, 0, 0};
                        Say(map->species[b.sp].name, "is stranded in the drained god-pool: its gills are bare!", 5);
                    }
                } else Say("", "The god-pool drains and refills: nothing was home", 3);
                return true;
            }
            if (tj["kind"].Str0() == "boil") {
                // the hypocaust boils a room: everything in the baths is scalded (divers too, less)
                fx.push_back({5, s.pos, {0, 0, 0}});
                for (int i = 0; i < (int)eco.agents.size(); i++) {
                    const Agent& a = eco.agents[i];
                    if (!a.alive || a.diver >= 0 || IsBoss(i) || a.zone != s.zone) continue;
                    HitAgent(&d, i, tj["damage"].F(150), false, false, {0, 1, 0}, nullptr);
                }
                for (auto& o : divers) if (!o.dead && !o.downed && o.zone == s.zone) HitDiver(o, 25, "the hypocaust", "", s.pos);
                return true;
            }
            if (tj["kind"].Str0() == "rockfall") {
                // the Chimney's stalactites, shot loose from their anchor onto whatever is below
                int zi = s.zone;
                const Zone& z = map->zones[zi];
                Vector3 c{s.pos.x, z.y0, s.pos.z};
                c = z.Clamp(Vector3Lerp(c, z.Center(), 0.4f), 1.0f);
                DropRocks(c, tj["rocks"].I(7), tj["spread"].F(6), tj["damage"].F(400), 2.0f, 0.8f, d.slot);
                eco.AddNoise(c, 9, true);
                return true;
            }
            // the cargo crane drops a container across the salon door, crushing whatever is in the doorway
            int li = -1;
            for (int k = 0; k < (int)map->links.size(); k++) if (HasW(map->links[k].passage, "salon door")) li = k;
            Vector3 c = li >= 0 ? Vector3Lerp(map->links[li].a, map->links[li].b, 0.5f) : s.pos;
            fx.push_back({7, c, {0, -1, 0}});
            eco.AddNoise(c, 9, true);
            for (int i = 0; i < (int)eco.agents.size(); i++) {
                const Agent& a = eco.agents[i];
                if (!a.alive || a.diver >= 0 || IsBoss(i) || Vector3Distance(a.pos, c) > 5) continue;
                pendingKiller = d.slot;
                eco.Kill(i, d.agent);
                pendingKiller = -1;
            }
            return true;
        }
        case StationType::Quest:
            if (map->extra["quest_altar"].IsObj()) {
                // the Reef's drum: on the altar (unbeaten), the turtles nest while the divers hold the beach
                const Json& qa = map->extra["quest_altar"];
                if (safeOpen || nesting || s.name != qa["poi"].Str0() || d.drumUses <= 0 || !d.drumClean) return false;
                nesting = true; nests = 0; nestT = 0; nestPlacer = d.slot; d.drumUses = 0; d.drumClean = false;
                Say("", qa["text"].Str0(), 5);
                eco.SpawnSquad(s.zone >= 0 ? map->zones[s.zone].alarmRegion : 0, true, players >= 3 ? 4 : 3, false);   // "Raiders target the beach"
                return true;
            }
            if (s.name.find("lantern") != std::string::npos) {
                // the Cave: a Drowned lantern put out with an ink cap
                if (lanternsOut.count(si) || d.inkCaps <= 0) return false;
                d.inkCaps--; lanternsOut.insert(si);
                Say("", TextFormat("The lantern gutters out under the ink: a page of the expedition's log (%d of 3). The swiftlets fall silent.", (int)lanternsOut.size()), 5);
                return true;
            }
            if (s.name.find("log") != std::string::npos) {
                // the Sunken Ship: the captain's log (the chalk shows after tide 3)
                const Json& hq = map->extra["hidden_quest"];
                if (logRead || tide <= hq["log_after_tide"].I(3)) return false;
                logRead = true; Say("The captain's log", hq["log"].Str0(), 8);
                return true;
            }
            if (safeOpen || !logRead || keys.size() < 3) return false;
            openSt = si; openT = 0; openStarted = false;
            return LongOpen(d, si, map->extra["hidden_quest"]["open_s"].F(20), dt);
        case StationType::Cache: {
            const Json& bk = map->extra["boss_key"];
            if (cacheOpen || !keys.count(bk["key"].Str0())) return false;
            int need = map->extra["hidden_quest"]["lanterns"].I(0);
            if (need > 0 && ((int)lanternsOut.size() < need || phase != TidePhase::Tide)) return false;   // three pages, and during a tide
            openSt = si; openT = 0; openStarted = false;
            return LongOpen(d, si, map->extra["hidden_quest"]["open_s"].F(15), dt);
        }
        case StationType::QuestStep: {
            if (map->extra["quests"].IsArr() && !map->extra["quests"].a.empty()) {
                int c = QuestChainOf(s.step);
                if (c < 0) return false;
                const Json& st = map->extra["quests"].a[c]["steps"][std::to_string(s.step)];
                std::string k = st["kind"].Str0();
                if (k == "carry") {
                    if (d.spark) return false;
                    d.spark = true;
                    if (c < (int)questAt.size() && questAt[c] == s.step) QuestAdvance(c, &d);
                    else Say("", "The jar glows with the crystal's spark", 2);
                    return true;
                }
                if (c >= (int)questAt.size() || questAt[c] != s.step) return false;
                if (k == "spark" && !d.spark) return false;
                if (k == "ichor") { if (!d.ichorJar || wyrmState != 0) return false; d.ichorJar = false; }
                if (k == "key" && !keys.count(st["key"].Str0())) return false;
                if (k == "hold" || k == "ledge" || k == "vent") return false;
                if (k == "egg") { if (!keys.count(st["key"].Str0()) || d.egg) return false; d.egg = true; }
                if (k == "place_egg") { if (!d.egg) return false; d.egg = false; eggPlaced = true; eggPos = s.pos; }
                if (k == "read") {
                    // each terminal read wakes the station: a Remnant squad comes through the nearest airlock
                    int reg = s.zone >= 0 ? map->zones[s.zone].alarmRegion : 0;
                    eco.SpawnSquad(reg, false, players >= 3 ? 3 : 2, false);
                }
                fx.push_back({4, s.pos, {0, 0, 0}});
                QuestAdvance(c, &d);
                return true;
            }
            // Supper Call: the chief engineer's log, his whistle, three blasts on the boiler's cord with the power off
            const Json& q = map->extra["quest"];
            if (s.step == 1 && questStep == 0) { questStep = 1; Say("Chief engineer's log", q["steps"][0].Str0(), 7); return true; }
            if (s.step == 2 && questStep == 1) { questStep = 2; Say("", q["steps"][1].Str0(), 5); return true; }
            if (s.step == 3 && questStep == 2 && !power) {
                whistlePulls++; whistleT = 6;
                eco.AddNoise(s.pos, 8);
                fx.push_back({5, s.pos, {0, 0, 0}});
                Say("", whistlePulls < 3 ? "The whistle shrieks through the wreck..." : "A third blast. The boiler's shadow moves.", 3);
                if (whistlePulls >= 3) {
                    questStep = 3; supperCall = true;
                    if (bossAgent >= 0 && eco.agents[bossAgent].alive) {
                        bossActive = true; bossIdleT = 0; bossProvoked = true;
                        Agent& b = eco.agents[bossAgent]; b.hunger = 1; b.fedT = 0;
                        Say(map->species[b.sp].name, "comes up for its supper", 5);
                    }
                }
                return true;
            }
            return false;
        }
        case StationType::Workbench: {
            // a full set of a build's parts: build it (one build is held at a time)
            for (int b = 1; b < (int)BuildType::COUNT; b++) {
                int set = 7 << (b * 3);
                if ((d.partsMask & set) != set) continue;
                if (d.build != BuildType::None) { Say("", std::string("One build at a time: you already carry the ") + Build(d.build).name, 3); return false; }
                d.partsMask &= ~set;
                for (auto& sp : salvage) if (sp.build == b && sp.carrier == d.slot) { sp.carrier = -1; sp.respawnT = 90; }   // (a new set turns up elsewhere)
                d.build = (BuildType)b;
                if (d.build == BuildType::ShellShield) d.shieldHP = 300;
                fx.push_back({4, s.pos, {0, 0, 0}});
                Say("The workbench", std::string(Build(d.build).name) + ": " + Build(d.build).effect + (d.build == BuildType::ShellShield ? "" : ". B sets it down."), 5);
                return true;
            }
            // otherwise the bench's stock (the design doc leaves open where the tacticals and the brush come from)
            int item = std::clamp(d.benchSel, 0, BENCH_ITEMS - 1);
            int* n = item == 0 ? &d.inkBombs : item == 1 ? &d.chumBags : item == 2 ? &d.flares : nullptr;
            if (n ? *n >= 2 : d.brush) return false;
            if (!pay(BenchPrice(item))) return false;
            if (n) { ++*n; d.tactical = item == 0 ? TAC_INK : item == 1 ? TAC_CHUM : TAC_FLARE; } else d.brush = true;
            Say("", n ? TextFormat("%s (%d). Q picks the tactical, G throws it.", BenchName(item), *n) : "A cleaning brush: X scrapes the parasites off you or a teammate", 3);
            return true;
        }
        case StationType::Cleaning: {
            bool any = false;
            for (int i = 0; i < (int)eco.agents.size(); i++) if (eco.agents[i].alive && eco.agents[i].host == d.agent && d.agent >= 0) { eco.agents[i].host = -1; eco.agents[i].st = State::Flee; any = true; }
            d.slowT = 0; d.slowMult = 1;
            return any;
        }
        default: return false;
    }
}

// ---------------------------------------------------------------- navigation (room to room through open passages)
Vector3 Match::NavStep(const DiverState& d, Vector3 goal) const {
    // inside a passage: carry on to the far mouth (or back, if the goal is behind)
    for (const auto& v : level.vols) {
        if (v.link < 0 || !v.diverOk || !linkOpen[v.link]) continue;
        if (d.pos.x < v.lo.x || d.pos.x > v.hi.x || d.pos.y < v.lo.y || d.pos.y > v.hi.y || d.pos.z < v.lo.z || d.pos.z > v.hi.z) continue;
        if (eco.ZoneAt(d.pos) >= 0) break;                    // the passage box reaches into the room: we're in the room
        const Link& l = map->links[v.link];
        int gz = eco.ZoneAt(goal);
        if (gz == l.from) return l.a;
        if (gz == l.to) return l.b;
        return d.zone == l.from ? l.b : l.a;
    }
    int gz = eco.ZoneAt(goal);
    if (gz < 0) {
        // a goal in a passage (a door): its nearer mouth
        for (const auto& v : level.vols) if (v.link >= 0 && goal.x >= v.lo.x && goal.x <= v.hi.x && goal.y >= v.lo.y && goal.y <= v.hi.y && goal.z >= v.lo.z && goal.z <= v.hi.z) {
            const Link& l = map->links[v.link];
            gz = Vector3Distance(l.a, d.pos) < Vector3Distance(l.b, d.pos) ? l.from : l.to;
            if (gz == d.zone) return linkOpen[v.link] ? goal : (l.from == d.zone ? l.a : l.b);
            break;
        }
    }
    if (gz < 0 || gz == d.zone) return Skirt(d, goal);
    // BFS over rooms through open passages: first without the one-way drops, then with them (downhill only)
    std::vector<int> prev, via;
    for (int pass = 0; pass < 2; pass++) {
        prev.assign(map->zones.size(), -2); via.assign(map->zones.size(), -1);
        std::vector<int> q{d.zone};
        prev[d.zone] = -1;
        for (size_t h = 0; h < q.size(); h++) {
            int z = q[h];
            if (z == gz) break;
            for (int li = 0; li < (int)map->links.size(); li++) {
                if (!DiverLink(li)) continue;
                const Link& l = map->links[li];
                if (l.oneWay && pass == 0) continue;
                int n = -1;
                if (l.from == z) n = l.to; else if (l.to == z && !l.oneWay) n = l.from;
                if (n >= 0 && prev[n] == -2) { prev[n] = z; via[n] = li; q.push_back(n); }
            }
        }
        if (prev[gz] != -2) break;
    }
    if (prev[gz] == -2) return goal;
    int step = gz;
    while (prev[step] != d.zone && prev[step] >= 0) step = prev[step];
    const Link& l = map->links[via[step]];
    Vector3 here = l.from == d.zone ? l.a : l.b, there = l.from == d.zone ? l.b : l.a;
    if (Vector3Distance(d.pos, here) < 1.0f) return there;
    return Skirt(d, here);
}

// Crossing the boss's room while it sleeps: keep 6.5 m off it along the far wall (it wakes at 5 m).
Vector3 Match::Skirt(const DiverState& d, Vector3 to) const {
    if (d.zone >= 0 && d.zone < (int)map->zones.size() && !map->zones[d.zone].parts.empty() && map->zones[d.zone].Contains(d.pos, 0.3f)) to = map->zones[d.zone].Waypoint(d.pos, to);   // (round a ring of parts)
    if (bossActive || bossAgent < 0 || !eco.agents[bossAgent].alive || eco.agents[bossAgent].zone != d.zone) return to;
    Vector3 b = eco.agents[bossAgent].pos;
    if (SegPointDist(d.pos, to, b) > 6.5f) return to;
    Vector3 mid = Vector3Lerp(d.pos, to, 0.5f);
    Vector3 away = Vector3Subtract(mid, b); away.y = 0;
    if (Vector3Length(away) < 0.1f) away = {0, 0, 1};
    Vector3 wp = map->zones[d.zone].Clamp(Vector3Add(b, Vector3Scale(Vector3Normalize(away), 7.0f)), 0.6f);
    if (Vector3Distance(d.pos, wp) < 1.0f) return to;
    return wp;
}

// ---------------------------------------------------------------- bots (--redtide-sim, and the empty slots in a solo match)
// A careful bot hunts grazers and small fry one at a time, spreads out, leaves cleaners and apex beasts alone unless
// they come for it, buys doors, a better gun, Juggernaut and the tonics, and turns the power on; a careless bot shoots
// whatever is nearest, schools and cleaners included.
void Match::Bot(DiverState& d, float dt) {
    if (d.dead) return;
    const WeaponsData& WD = Weapons();
    bool careful = botStyle != "careless";
    d.botThinkT -= dt;
    // the boss's room is off limits to a careful bot until it's strong: Juggernaut and a gun worth 3 Cormorants
    int bossZone = bossAgent >= 0 && eco.agents[bossAgent].alive ? eco.agents[bossAgent].homeZone : -1;
    float bestVal = 0;
    for (const auto& h : d.weapons) { const WeaponDef& w = W(h); bestVal = std::max(bestVal, w.damage * w.pellets * w.rpm * (h.forged ? 2.5f : 1.0f)); }
    bool strong = !careful || (d.tonics.count("juggernaut") && bestVal >= 27000);
    auto avoidZone = [&](int z) { return careful && !strong && z >= 0 && z == bossZone; };
    auto aimAt = [&](Vector3 p, float errDeg) {
        Vector3 dir = Vector3Normalize(Vector3Subtract(p, Eye(d)));
        float e = errDeg * DEG2RAD;
        d.yaw = atan2f(dir.x, dir.z) + Rand(-e, e);
        d.pitch = asinf(std::clamp(dir.y, -1.0f, 1.0f)) + Rand(-e, e);
    };
    auto target = [&]() -> const Agent* { return d.botTarget >= 0 && d.botTarget < (int)eco.agents.size() && eco.agents[d.botTarget].alive ? &eco.agents[d.botTarget] : nullptr; };
    auto shootTarget = [&]() {
        const Agent* t = target();
        if (!t) return;
        if (getenv("DEPTH_HITLOG") && IsBoss(d.botTarget) && fmodf(time, 1.0f) < dt) printf("   [bot] t=%.1f diver %d targets the boss: plan '%s' downed %d held %d flee %d active %d\n", time, d.slot, d.botPlan.c_str(), (int)d.downed, (int)(d.heldT > 0), d.botFlee, (int)bossActive);
        Vector3 p = Vector3Add(t->pos, Vector3Scale(t->vel, Vector3Distance(t->pos, d.pos) / 28.0f));
        aimAt(p, careful ? 0.4f : 1.2f);
        Held& h = Cur(d);
        bool dry = h.mag + h.reserve <= 0 && !W(h).melee;
        if (Vector3Distance(t->pos, d.pos) < (dry ? 2.0f : 1.4f) && (map->species[t->sp].size <= 2 || dry)) { Melee(d.slot); return; }
        if (dry) return;                                        // out of darts: the knife's the only thing left
        if (h.mag <= 0) { Reload(d.slot); return; }
        if (!level.Sight(Eye(d), t->pos, linkOpen, true)) return;
        if (careful) {
            // hold fire if something big is in the line of fire (a stray dart into a shark starts a fight)
            Vector3 eye = Eye(d), dir = Vector3Normalize(Vector3Subtract(t->pos, eye));
            float tdist = Vector3Distance(t->pos, eye);
            for (int i = 0; i < (int)eco.agents.size(); i++) {
                const Agent& o = eco.agents[i];
                if (!o.alive || o.diver >= 0 || &o == t || map->species[o.sp].size < 4) continue;
                Vector3 to = Vector3Subtract(o.pos, eye);
                float along = Vector3DotProduct(to, dir);
                if (along < 0 || along > 30) continue;           // a miss flies on: anything big behind the target counts too
                if (Vector3Length(Vector3Subtract(to, Vector3Scale(dir, along))) < bodies[o.sp].radius + bodies[o.sp].length * 0.5f + 0.5f) return;
            }
        }
        Fire(d.slot, true, dt);
    };
    if (d.downed) {
        // float and fire the Cormorant at whatever is on you
        int best = -1; float bd = 10;
        for (int i = 0; i < (int)eco.agents.size(); i++) {
            const Agent& a = eco.agents[i];
            if (!a.alive || a.diver >= 0 || (IsBoss(i) && !bossActive)) continue;
            bool onMe = !a.targetCorpse && a.target == d.agent && (a.st == State::Hunt || a.st == State::Defend);
            if ((onMe || map->species[a.sp].isEnemy) && Vector3Distance(a.pos, d.pos) < bd) { bd = Vector3Distance(a.pos, d.pos); best = i; }
        }
        d.botTarget = best;
        shootTarget();
        return;
    }
    if (d.heldT > 0) {
        if (d.botThinkT <= 0) { Interact(d.slot, false, dt); d.botThinkT = 0.18f; }
        d.botTarget = d.holder;
        shootTarget();
        return;
    }
    if (d.botThinkT <= 0) {
        d.botThinkT = 0.25f;
        d.botPlan.clear();
        // 1. a downed teammate
        for (const auto& o : divers) if (&o != &d && o.downed) { d.botGoal = o.pos; d.botPlan = "revive"; break; }
        // 2. what's hunting me
        int threat = -1; float td = 12;
        for (int i = 0; i < (int)eco.agents.size(); i++) {
            const Agent& a = eco.agents[i];
            if (!a.alive || a.diver >= 0) continue;
            bool onMe = (a.st == State::Hunt || a.st == State::Defend) && !a.targetCorpse && a.target == d.agent;
            bool enemy = map->species[a.sp].isEnemy && (a.st == State::Investigate || a.st == State::Defend);
            float dist = Vector3Distance(a.pos, d.pos);
            bool boss = IsBoss(i) && bossActive;                  // a sleeping Goliath is left sleeping
            if (IsBoss(i) && !bossActive) continue;
            if ((onMe || (enemy && dist < 25) || boss) && dist < (enemy || boss ? 25 : td) && level.Sight(Eye(d), a.pos, linkOpen, true)) { td = dist; threat = i; }
        }
        // 3. prey
        int prey = -1; float pd = 22;
        if (threat < 0) for (int i = 0; i < (int)eco.agents.size(); i++) {
            const Agent& a = eco.agents[i];
            if (!a.alive || a.diver >= 0 || IsBoss(i)) continue;
            const Species& s = map->species[a.sp];
            if (s.isDiver) continue;
            if (careful && (s.size > 3 || s.tier >= 4 || s.Cleaner() || a.host >= 0)) continue;
            if (careful && (s.Has("pack") || s.social == "pack") && W(Cur(d)).damage * W(Cur(d)).pellets < 60) continue;   // a pack answers for its own
            if (Forbidden(a.pos, 30)) continue;                                          // not out over the void or past the stakes
            if (careful && s.size <= 1 && s.social == "school" && eco.CountInZone(a.sp, a.zone) > 8 && tide >= 3) continue;   // don't bleed the school
            float dist = Vector3Distance(a.pos, d.pos);
            if (!careful) dist *= 0.8f;
            if (dist < pd && (a.zone == d.zone || dist < 10 || !map->zones[a.zone].diverOk) && level.Sight(Eye(d), a.pos, linkOpen, true)) { pd = dist; prey = i; }
        }
        d.botTarget = threat >= 0 ? threat : prey;
        // a territorial beast's warning (or an apex coming): back out of its radius rather than fight it
        d.botFlee = -1;
        for (int i = 0; i < (int)eco.agents.size(); i++) {
            const Agent& a = eco.agents[i];
            if (!a.alive || a.diver >= 0 || IsBoss(i)) continue;
            const Species& s = map->species[a.sp];
            if (s.size < 4 || a.target != d.agent || a.targetCorpse) continue;
            if (a.st == State::Defend || (careful && a.st == State::Hunt && Cur(d).forged == false && W(Cur(d)).damage * W(Cur(d)).pellets < 60)) { d.botFlee = i; break; }
        }
        if (bossActive && bossAgent >= 0 && !strong && Vector3Distance(eco.agents[bossAgent].pos, d.pos) < 16) d.botFlee = bossAgent;
        if (d.botPlan.empty()) {
            // 4. drops on the floor
            for (const auto& f : drops) if (f.weapon < 0 && Vector3Distance(f.pos, d.pos) < 25) { d.botGoal = f.pos; d.botPlan = "drop"; break; }
        }
        if (d.botPlan.empty() && (threat < 0 || phase == TidePhase::Calm)) {
            // 5. shopping: a list of wants in order, the first affordable one that's reachable
            // a room is open to this bot if open passages lead there from where it is (one-way drops count only
            // when it's already below them: it doesn't choose to jump down a slide it can't swim back up)
            std::vector<char> seen(map->zones.size(), 0);
            {
                std::vector<int> q{d.zone}; seen[d.zone] = 1;
                for (size_t h = 0; h < q.size(); h++) for (int li = 0; li < (int)map->links.size(); li++) {
                    if (!DiverLink(li)) continue; const Link& l = map->links[li];
                    if (l.oneWay) continue;
                    int n = l.from == q[h] ? l.to : (l.to == q[h] ? l.from : -1);
                    if (n >= 0 && !seen[n]) { seen[n] = 1; q.push_back(n); }
                }
            }
            auto zoneOpen = [&](int zone) { return zone >= 0 && seen[zone]; };
            int reserve = careful ? 200 : 0;
            const Station* want = nullptr;
            int bestScore = -1;
            for (const auto& s : level.stations) {
                if (!zoneOpen(s.zone) || (s.needsPower && !Powered(s)) || avoidZone(s.zone)) continue;
                int score = -1, cost = 0;
                if (s.type == StationType::Tonic) {
                    const TonicDef* t = WD.Tonic(s.tonic);
                    if (!t || d.tonics.count(t->id) || d.tonics.size() >= 4) continue;
                    cost = t->id == "quick" && players == 1 && t->priceSolo > 0 ? t->priceSolo : t->price;
                    if (t->id == "quick" && players == 1 && d.quickBought >= 3) continue;
                    score = t->id == "juggernaut" ? 90 : t->id == "quick" ? (players == 1 ? 95 : 60) : t->id == "speed" ? 50 : t->id == "double" ? 70 : 40;
                } else if (s.type == StationType::Rack && s.weapon >= 0) {
                    const WeaponDef& w = WD.weapons[s.weapon];
                    bool have = false; for (const auto& h : d.weapons) if (h.def == s.weapon) have = true;
                    const Held& cur = Cur(d);
                    float curVal = W(cur).damage * W(cur).pellets * W(cur).rpm * (cur.forged ? 2.5f : 1.0f);
                    float val = w.damage * w.pellets * w.rpm;
                    if (have) { const Held* hh = nullptr; for (const auto& h : d.weapons) if (h.def == s.weapon) hh = &h; if (hh && hh->reserve < ResMax(w, *hh) * 0.25f) { cost = hh->forged ? (int)WD.rearm : w.price / 2; score = hh->mag + hh->reserve == 0 ? 98 : 85; } }
                    else if (Cur(d).mag + Cur(d).reserve == 0 && w.price <= 1200) { cost = w.price; score = 97; }   // dry: any gun beats the knife
                    else if (val > curVal * 1.3f) { cost = w.price; score = 75; }
                } else if (s.type == StationType::Power && !power) { if (tide < 8 || !strong) continue; score = 65; cost = 0; }   // power wakes the Goliath: not before the team is ready
                else if (s.type == StationType::Forge && !Cur(d).forged && W(Cur(d)).source != "start") { cost = (int)WD.forgePrice; score = 80; }
                else if (s.type == StationType::Locker && LockerLiveAt(s) && d.scrip > 4000) { cost = fireSaleT > 0 ? WD.fireSalePull : WD.lockerPull; score = 30; }
                else if (s.type == StationType::Quest && keys.size() >= 3 && !safeOpen) { score = 99; }
                if (score < 0 || cost + reserve > d.scrip) continue;
                if (score > bestScore) { bestScore = score; want = &s; }
            }
            // doors: the cheapest closed one on the edge of the open ship, once the stations here are bought
            // (trapped below a one-way drop, the way out is worth every coin)
            const Door* door = nullptr;
            for (const auto& dr : level.doors) {
                if (dr.open) continue;
                const Link& l = map->links[dr.link];
                if (!zoneOpen(l.from) && !zoneOpen(l.to)) continue;
                bool trapped = true;
                for (int z2 = 0; z2 < (int)map->zones.size(); z2++) if (seen[z2] && z2 == level.startZone) trapped = false;
                if (dr.cost + (trapped ? 0 : reserve) > d.scrip) continue;
                if (!door || dr.cost < door->cost) door = &dr;
            }
            if (door && (bestScore < 70 || !want)) {
                const Link& l = map->links[door->link];
                d.botGoal = zoneOpen(l.from) ? l.a : l.b; d.botPlan = "door";
            } else if (want) { d.botGoal = want->pos; d.botPlan = "buy"; }
        }
        if (d.botPlan.empty() && d.botTarget < 0) {
            // 6. go where the prey is: the reachable room with the most killable beasts (down a one-way drop only
            // when nothing up here has any: the Sunken Ship's opening is out the free hatch and down to the Keel)
            int bestZ = -1, bestN = 0;
            for (int pass = 0; pass < 2 && bestZ < 0; pass++) {
                std::vector<char> seen(map->zones.size(), 0); std::vector<int> q{d.zone}; seen[d.zone] = 1;
                for (size_t h = 0; h < q.size(); h++) for (int li = 0; li < (int)map->links.size(); li++) {
                    if (!DiverLink(li)) continue; const Link& l = map->links[li];
                    if (l.oneWay && pass == 0) continue;
                    int nn = l.from == q[h] ? l.to : (l.to == q[h] && !l.oneWay ? l.from : -1);
                    if (nn >= 0 && !seen[nn]) { seen[nn] = 1; q.push_back(nn); }
                }
                for (int z = 0; z < (int)map->zones.size(); z++) {
                    if (!seen[z] || avoidZone(z)) continue;
                    int n = 0;
                    for (const auto& a : eco.agents) if (a.alive && a.diver < 0 && a.zone == z && map->species[a.sp].size <= (careful ? 2 : 4) && map->species[a.sp].tier <= (careful ? 2 : 4)) n++;
                    if (careful && z != d.zone) n = n * 3 / 4;   // careful bots stay put a little longer
                    if (n > bestN) { bestN = n; bestZ = z; }
                }
            }
            if (bestZ >= 0 && bestZ != d.zone) { d.botGoal = map->zones[bestZ].Center(); d.botPlan = "roam"; }
            else if (bestZ == d.zone) {
                // prey in this room but out of reach of the eye: swim to the nearest
                float nd = 1e9f;
                for (const auto& a : eco.agents) {
                    if (!a.alive || a.diver >= 0 || a.zone != d.zone) continue;
                    const Species& s = map->species[a.sp];
                    if (s.size > (careful ? 2 : 4) || s.tier > (careful ? 2 : 4) || (careful && s.Cleaner())) continue;
                    float dd = Vector3Distance(a.pos, d.pos);
                    if (dd < nd) { nd = dd; d.botGoal = a.pos; d.botPlan = "roam"; }
                }
            }
        }
    }
    // act
    const Agent* t = target();
    Vector3 wish{0, 0, 0}; float vert = 0; bool sprint = false;
    auto go = [&](Vector3 goal, float stopAt) {
        Vector3 step = NavStep(d, goal);
        Vector3 to = Vector3Subtract(step, d.pos);
        if (Vector3Length(to) < stopAt && Vector3Distance(step, goal) < 0.01f) return true;
        wish = {to.x, 0, to.z};
        vert = std::clamp(to.y, -1.0f, 1.0f);
        return false;
    };
    if (d.botFlee >= 0 && eco.agents[d.botFlee].alive) {
        const Agent& a = eco.agents[d.botFlee];
        Vector3 away = Vector3Subtract(d.pos, a.st == State::Defend && map->species[a.sp].defendR > 0 ? a.home : a.pos);
        if (IsBoss(d.botFlee) && d.zone == a.homeZone) {
            // out of its room the way we came: the nearest open passage
            float bd = 1e9f; Vector3 exitTo = d.pos;
            for (int li = 0; li < (int)map->links.size(); li++) {
                if (!DiverLink(li)) continue; const Link& l = map->links[li];
                if (l.from != d.zone && l.to != d.zone) continue;
                Vector3 far = l.from == d.zone ? l.b : l.a;
                float dd = Vector3Distance(far, d.pos);
                if (dd < bd) { bd = dd; exitTo = far; }
            }
            Vector3 st = NavStep(d, exitTo);
            away = Vector3Subtract(st, d.pos);
        }
        wish = {away.x, 0, away.z}; vert = std::clamp(away.y * 0.3f, -1.0f, 1.0f); sprint = true;
        d.botPlan = "flee";
    } else if (d.botPlan == "revive") {
        for (auto& o : divers) if (&o != &d && o.downed) { if (go(o.pos, 1.2f)) Interact(d.slot, true, dt); break; }
    } else if (d.botPlan == "door" || d.botPlan == "buy") {
        if (go(d.botGoal, 1.2f)) { Interact(d.slot, false, dt); d.botThinkT = 0; }
        sprint = !t;
    } else if (d.botPlan == "drop" || d.botPlan == "roam") {
        go(d.botGoal, 1.0f);
        sprint = !t;
    }
    if (t) {
        float dist = Vector3Distance(t->pos, d.pos);
        const Species& s = map->species[t->sp];
        if (d.botPlan.empty()) {
            const Held& ch = Cur(d);
            bool dry = ch.mag + ch.reserve <= 0 && !W(ch).melee;
            float keep = dry ? 0.8f : s.size >= 3 || s.isEnemy ? 7.0f : 3.0f;
            if (dist > keep + 3 || !level.Sight(Eye(d), t->pos, linkOpen)) go(t->pos, keep);
            else if (dist < keep - 1.5f) { Vector3 away = Vector3Subtract(d.pos, t->pos); wish = {away.x, 0, away.z}; vert = std::clamp(away.y, -1.0f, 1.0f); }
        }
        shootTarget();
    } else {
        Fire(d.slot, false, dt);
        Held& h = Cur(d);
        if (h.mag < MagMax(W(h), h) * 0.5f) Reload(d.slot);
        // carry the best gun
        int best = d.cur; float bv = -1;
        for (int i = 0; i < (int)d.weapons.size(); i++) { const WeaponDef& w = W(d.weapons[i]); float v = w.damage * w.pellets * w.rpm * (d.weapons[i].forged ? 2.5f : 1) * (d.weapons[i].mag + d.weapons[i].reserve > 0 ? 1 : 0); if (v > bv) { bv = v; best = i; } }
        if (best != d.cur) SwapWeapon(d.slot, best);
    }
    // unstick
    d.botStuckT += dt;
    if (d.botStuckT > 3) {
        if (Vector3Distance(d.pos, d.botLastPos) < 0.4f && Vector3Length(wish) > 0.1f) { d.botThinkT = 0; d.pos = level.Move(d.pos, Vector3Add(d.pos, {Rand(-1, 1), Rand(-0.5f, 0.5f), Rand(-1, 1)}), 0.4f, linkOpen); }
        d.botStuckT = 0; d.botLastPos = d.pos;
    }
    // never out over the void or past the bone stakes: turn back toward the start
    if (Forbidden(Vector3Add(d.pos, Vector3Scale(wish, 4)), 25) || Forbidden(d.pos, 20)) { Vector3 home = Vector3Subtract(level.start, d.pos); wish = Vector3Normalize({home.x, 0, home.z}); vert = std::clamp(home.y * 0.2f, -1.0f, 1.0f); }
    SteerDiver(d.slot, wish, vert, sprint, t != nullptr && !careful ? false : t != nullptr, dt);
}

// ---------------------------------------------------------------- --redtide-sim
// Plays bot teams to a tide count and reports the design doc's balance targets.
int RunRedTideSim(const std::string& mapKey, int tides, const std::string& style, int runs, int players) {
    std::string why;
    if (!DataOk(&why)) { printf("redtide-sim: %s\n", why.c_str()); return 1; }
    printf("Red Tide sim: %s, %d player bot team (%s), to tide %d, %d runs\n", Map(mapKey).title.c_str(), players, style.c_str(), tides, runs);
    double sumTide = 0, sumT10 = 0, sumScrip10 = 0; int n10 = 0, reached = 0;
    std::map<std::string, int> downs;
    int beasts = 0, enemies = 0, hazards = 0, hunts = 0, inhales = 0, crocs = 0, doors = 0, squads = 0, squadTides = 0;
    uint32_t base = getenv("DEPTH_SEED") ? (uint32_t)atoi(getenv("DEPTH_SEED")) : 1000;
    for (int r = 0; r < runs; r++) {
        auto M = std::make_unique<Match>();
        M->botStyle = style;
        M->Init(mapKey, players, base + r * 7919, true);
        float dt = 1 / 20.0f, limit = 60.0f * 90;
        int lastTide = 1; float lastLog = 0;
        while (!M->over && M->tide <= tides && M->time < limit) {
            M->Step(dt);
            M->fx.clear();
            if (getenv("DEPTH_SIMLOG") && M->time - lastLog > (getenv("DEPTH_SIMLOG")[0] == '3' ? 2 : 60)) {
                lastLog = M->time;
                const DiverState& d = M->divers[0];
                if (d.botTarget >= 0) { const Agent& ta = M->eco.agents[d.botTarget]; printf("      target %s (%s) at %.1f m, sight %d, alive %d, pos %.1f %.1f %.1f me %.1f %.1f %.1f\n", M->map->species[ta.sp].name.c_str(), StateName(ta.st), Vector3Distance(ta.pos, d.pos), (int)M->level.Sight(M->Eye(d), ta.pos, M->linkOpen), (int)ta.alive, ta.pos.x, ta.pos.y, ta.pos.z, d.pos.x, d.pos.y, d.pos.z); }
                if (getenv("DEPTH_SIMLOG")[0] >= '2') { std::map<std::string, int> c; for (const auto& a : M->eco.agents) if (a.alive && a.zone == d.zone && a.diver < 0) c[M->map->species[a.sp].name]++; for (auto& kv : c) printf("      %s %d\n", kv.first.c_str(), kv.second); }
                printf("   t=%4.0f tide %d %s kills %d/%d  hp %3.0f scrip %5d  zone %s plan %s  weapons %d\n", M->time, M->tide, M->phase == TidePhase::Calm ? "calm" : M->phase == TidePhase::Hunt ? "HUNT" : "tide", M->tideKills, M->quota, d.hp, d.scrip, M->map->zones[d.zone].name.c_str(), d.botPlan.c_str(), (int)d.weapons.size());
            }
            if (M->tide != lastTide) lastTide = M->tide;
        }
        int reachedTide = M->over ? M->tide : std::min(M->tide, tides + 1) - (M->tide > tides ? 1 : 0);
        if (M->tide > tides) reached++;
        sumTide += M->over ? M->tide : M->tide;
        if (M->timeToTide10 >= 0) { sumT10 += M->timeToTide10; sumScrip10 += M->scripAt10; n10++; }
        for (auto& kv : M->downsBy) downs[kv.first] += kv.second;
        beasts += M->deathsByBeast; enemies += M->deathsByEnemy; hazards += M->deathsByHazard;
        hunts += M->huntsSeen; inhales += M->bossInhales; crocs += M->crocHolds; doors += M->doorsOpened;
        int nonHunt = 0; for (const auto& s : M->eco.squads) if (!s.hunt) nonHunt++;
        squads += nonHunt; squadTides += std::max(1, M->maxTide);
        int scrip = 0; for (const auto& d : M->divers) scrip += d.scripEarned;
        printf("  run %2d: %s tide %d at %.1f min, %d kills, scrip earned %d/diver, %d downs, doors %d, power %s, %s\n", r + 1, M->over ? "wiped at" : "reached", reachedTide, M->time / 60,
               [&] { int k = 0; for (const auto& d : M->divers) k += d.kills; return k; }(), scrip / players,
               [&] { int k = 0; for (const auto& d : M->divers) k += d.downs; return k; }(), M->doorsOpened, M->power ? "on" : "off", M->over ? M->overReason.c_str() : "");
    }
    printf("\n  average tide reached: %.1f   (target: careful 25 +/- 3, careless 12 +/- 3, for a team of four)\n", sumTide / runs);
    printf("  reached tide %d: %d of %d\n", tides, reached, runs);
    if (n10) printf("  time to tide 10: %.1f min (target 15-20)   scrip at tide 10: %.0f per diver (target 15,000-20,000 careful)\n", sumT10 / n10 / 60, sumScrip10 / n10);
    int tot = std::max(1, beasts + enemies + hazards);
    printf("  downs by beasts / enemies / hazards: %d%% / %d%% / %d%%   (target about 60 / 25 / 15 at tide 15)\n", beasts * 100 / tot, enemies * 100 / tot, hazards * 100 / tot);
    printf("  alarm squads per tide: %.2f   (target careful 0.5, careless 3)\n", squads / (double)std::max(1, squadTides));
    printf("  hunts %d, Goliath inhales %d, crocodile holds %d, doors opened %d\n", hunts, inhales, crocs, doors);
    std::vector<std::pair<int, std::string>> top;
    for (auto& kv : downs) top.push_back({kv.second, kv.first});
    std::sort(top.rbegin(), top.rend());
    printf("  downed by:");
    for (size_t i = 0; i < top.size() && i < 8; i++) printf(" %s %d%s", top[i].second.c_str(), top[i].first, i + 1 < std::min<size_t>(top.size(), 8) ? "," : "");
    printf("\n");
    return 0;
}

// ---------------------------------------------------------------- --redtide-match-test
int RunRedTideMatchTest() {
    std::string why;
    if (!DataOk(&why)) { printf("redtide-match-test: %s\n", why.c_str()); return 1; }
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("Red Tide stage 3 test (the Sunken Ship's rules)\n");
    const WeaponsData& WD = Weapons();
    check(WD.weapons.size() >= 20 && WD.tonics.size() == 12 && WD.Index("cormorant") >= 0, TextFormat("weapons.json: %d weapons, %d tonics", (int)WD.weapons.size(), (int)WD.tonics.size()));
    auto M = std::make_unique<Match>();
    Match& m = *M;
    m.Init("ship", 1, 7, false);
    DiverState& d = m.divers[0];
    check(m.map->zones[m.eco.ZoneAt(d.pos)].name == "Bridge", "the diver spawns in the start pocket (the Bridge) with the Cormorant, 500 scrip and 2 limpets");
    check(m.quota == 5, TextFormat("solo tide 1 quota: %d (12 x 0.4)", m.quota));
    int racks = 0, tonics = 0, lockers = 0;
    for (const auto& s : m.level.stations) { racks += s.type == StationType::Rack; tonics += s.type == StationType::Tonic; lockers += s.type == StationType::Locker; }
    check(racks == 6 && tonics == 5 && lockers == 2, TextFormat("stations from the blockout: %d racks, %d tonic machines, %d Locker spots", racks, tonics, lockers));
    // the closed Salon door stops the diver; buying it lets them through
    int salonDoor = -1;
    for (int i = 0; i < (int)m.level.doors.size(); i++) if (m.level.doors[i].name == "Salon door") salonDoor = i;
    const Link& sl = m.map->links[m.level.doors[salonDoor].link];
    d.pos = sl.a;
    Vector3 tryTo = Vector3Add(sl.b, {1.5f, 0, 0});
    for (int i = 0; i < 200; i++) { m.SteerDiver(0, Vector3Subtract(tryTo, d.pos), 0, false, false, 1 / 30.0f); m.Step(1 / 30.0f); }
    check(m.eco.ZoneAt(d.pos) != m.map->ZoneIndex("Grand Salon"), "the closed Salon door keeps the diver in the Bridge");
    d.pos = sl.a; d.scrip = 700;
    check(!m.Interact(0, false, 0.01f), "the door costs 750: 700 scrip isn't enough");
    d.scrip = 800;
    check(m.Interact(0, false, 0.01f) && m.linkOpen[m.level.doors[salonDoor].link] && d.scrip == 50, "buying it opens it for good (800 -> 50)");
    for (int i = 0; i < 200; i++) { m.SteerDiver(0, Vector3Subtract(tryTo, d.pos), 0, false, false, 1 / 30.0f); m.Step(1 / 30.0f); }
    check(m.eco.ZoneAt(d.pos) == m.map->ZoneIndex("Grand Salon"), "and the diver swims through into the Grand Salon");
    // a rack
    for (const auto& s : m.level.stations) if (s.name == "Needler Mk I rack") d.pos = s.pos;
    d.scrip = 1000;
    check(m.Interact(0, false, 0.01f) && m.W(m.Cur(d)).id == "needler1" && d.weapons.size() == 2 && d.scrip == 0, "the Needler Mk I rack sells its gun (1000) into the second slot");
    d.scrip = 400;
    check(!m.Interact(0, false, 0.01f) && d.scrip == 400, "ammo is half price (500): 400 scrip isn't enough");
    d.scrip = 600; m.Cur(d).reserve = 0;
    check(m.Interact(0, false, 0.01f) && m.Cur(d).reserve == 150 && d.scrip == 100, "ammo refills the reserve for 500");
    // the quota ends the tide, pays the bonus, calms for 20 s (the diver is made sturdy: the salon's beasts bite)
    d.hp = d.hpMax = 1e6f;
    int s0 = d.scrip;
    for (int k = 0; k < 5; k++) {
        int best = -1;
        for (int i = 0; i < (int)m.eco.agents.size(); i++) { const Agent& a = m.eco.agents[i]; if (a.alive && a.diver < 0 && m.map->species[a.sp].size == 1 && !m.map->species[a.sp].Cleaner()) { best = i; break; } }
        if (best >= 0) m.eco.Damage(best, 1e6f, d.agent);
    }
    m.Step(0.05f);
    check(m.phase == TidePhase::Calm && d.scrip > s0 + 100, TextFormat("5 kills end tide 1 (bounties and the 100 tide bonus: %d -> %d)", s0, d.scrip));
    for (int i = 0; i < 21 * 20; i++) m.Step(0.05f);
    d.heldT = 0; d.holder = -1; d.bleedT = d.poisonT = d.stunT = 0;
    check(m.tide == 2 && m.phase == TidePhase::Tide && m.quota == 6, TextFormat("after the 20 s calm, tide 2 (quota %d)", m.quota));
    // a beast's bite goes through the Attacks sheet
    {
        int bull = -1;
        for (int i = 0; i < (int)m.eco.agents.size(); i++) if (m.eco.agents[i].alive && m.map->species[m.eco.agents[i].sp].name == "Bull Shark") bull = i;
        check(bull >= 0, "the Bull Shark is on the map");
        if (bull >= 0) {
            d.hp = d.hpMax = 250; d.tonics.insert("juggernaut");
            m.eco.agents[bull].pos = Vector3Add(d.pos, {1.0f, 0, 0});
            float hp0 = d.hp;
            // (its attack is picked at random: a hold defers its damage, so strike again until a bite lands)
            for (int k = 0; k < 8 && d.hp >= hp0; k++) { d.heldT = 0; d.holder = -1; d.holdPending = 0; m.eco.agents[bull].held = 0; m.eco.agents[bull].cooldown = 0; m.eco.Damage(d.agent, 80, bull); }
            check(d.hp < hp0 && d.lastHitBy == "Bull Shark", TextFormat("a Bull Shark strike lands on the diver (%.0f -> %.0f HP) as one of its three attacks", hp0, d.hp));
            d.heldT = 0; d.holder = -1; m.eco.agents[bull].held = 0;
        }
    }
    // the crocodile's death roll: held for 4 s; the damage lands unless it's shot off
    {
        int croc = -1;
        for (int i = 0; i < (int)m.eco.agents.size(); i++) if (m.eco.agents[i].alive && m.map->species[m.eco.agents[i].sp].name == "Saltwater Crocodile") croc = i;
        if (croc >= 0) {
            d.hp = d.hpMax = 100; d.tonics.clear(); d.bleedT = 0; d.poisonT = 0; d.stunT = 0;
            m.eco.agents[croc].pos = Vector3Add(d.pos, {1.5f, 0, 0});
            const Attack* roll = nullptr;
            for (const auto& at : m.map->attacks) if (at.beast == "Saltwater Crocodile" && at.name == "Death Roll") roll = &at;
            m.HitDiverPublic(d, roll->damage, "Saltwater Crocodile", roll->effect, m.eco.agents[croc].pos, croc);
            check(d.heldT > 3.5f && d.holdLethal && d.hp == 100, TextFormat("the Death Roll holds (%.1f s, 'teammates have 4 s'); no damage yet", d.heldT));
            float need = Engine().M("grab_break_teammate_dmg", 0.15f) * m.eco.agents[croc].hpMax;
            m.eco.Damage(croc, need + 1, d.agent);
            m.Step(0.05f);
            check(d.heldT <= 0 && !d.downed, TextFormat("shooting it for 15%% of its HP (%.0f) breaks the hold", need));
            m.eco.agents[croc].stun = 30;
            m.HitDiverPublic(d, roll->damage, "Saltwater Crocodile", roll->effect, m.eco.agents[croc].pos, croc);
            for (int i = 0; i < 5 * 20; i++) m.Step(0.05f);
            check(d.downed || m.over, "left alone, the roll lands (120) and the diver is down");
        }
    }
    check(m.over, "solo, no Quick Brine: a down ends the match");
    // Quick Brine solo self-revive
    m.Init("ship", 1, 8, false);
    {
        DiverState& q = m.divers[0];
        q.scrip = 500;
        m.linkOpen.assign(m.linkOpen.size(), 1);
        for (auto& dr : m.level.doors) dr.open = true;
        for (const auto& s : m.level.stations) if (s.tonic == "quick") q.pos = s.pos;
        check(m.Interact(0, false, 0.01f) && q.selfRevives == 1 && q.scrip == 0, "solo Quick Brine costs 500 and gives a self-revive");
        m.HitDiverPublic(q, 500, "test", "", q.pos, -1);
        check(q.downed && !m.over, "downed with a self-revive in hand: the match goes on");
        for (int i = 0; i < 4 * 20; i++) m.Step(0.05f);
        check(!q.downed && !m.over && q.tonics.empty() && q.selfRevives == 0, "the diver gets back up and loses the tonics");
        // power, the Forge, the Locker
        for (const auto& s : m.level.stations) if (s.type == StationType::Forge) q.pos = s.pos;
        q.scrip = 20000;
        check(!m.Interact(0, false, 0.01f), "the Forge needs power");
        for (const auto& s : m.level.stations) if (s.type == StationType::Power) q.pos = s.pos;
        check(m.Interact(0, false, 0.01f) && m.power, "the power switch");
        for (const auto& s : m.level.stations) if (s.type == StationType::Forge) q.pos = s.pos;
        float before = m.W(m.Cur(q)).damage;
        check(m.Interact(0, false, 0.01f) && m.Cur(q).forged && q.scrip == 15000 && m.Cur(q).mag == 12, TextFormat("the Pressure Forge (5000): %s, magazine 8 -> %d", m.W(m.Cur(q)).forged.c_str(), m.Cur(q).mag));
        (void)before;
        for (const auto& s : m.level.stations) if (s.type == StationType::Locker && s.lockerSpot == m.lockerSpot) q.pos = s.pos;
        int pulls = 0, moved = 0, spot0 = m.lockerSpot;
        for (int k = 0; k < 14 && !moved; k++) {
            for (const auto& s : m.level.stations) if (s.type == StationType::Locker && s.lockerSpot == m.lockerSpot) q.pos = s.pos;
            if (m.Interact(0, false, 0.01f)) pulls++;
            if (m.lockerSpot != spot0) moved = 1;
        }
        check(moved && pulls >= WD.lockerMoveMin && pulls <= WD.lockerMoveMax, TextFormat("Davy's Locker deals weapons, and the moray moves the chest after %d pulls", pulls));
        // drops
        q.weapons[0].reserve = 0;
        m.ApplyDropPublic(DropType::Resupply, q.pos);
        check(q.weapons[0].reserve > 0, "Resupply refills every gun");
        int alive0 = 0; for (const auto& a : m.eco.agents) if (a.alive && a.diver < 0 && Vector3Distance(a.pos, q.pos) < 40 && !m.map->species[a.sp].isEnemy && m.map->species[a.sp].tier < 5) alive0++;
        float blood0 = m.eco.scent.Total();
        m.ApplyDropPublic(DropType::Purge, q.pos);
        int alive1 = 0; for (const auto& a : m.eco.agents) if (a.alive && a.diver < 0 && Vector3Distance(a.pos, q.pos) < 40 && !m.map->species[a.sp].isEnemy && m.map->species[a.sp].tier < 5) alive1++;
        check(alive0 > 0 && alive1 == 0 && m.eco.scent.Total() <= blood0 + 0.01f, TextFormat("Purge: %d beasts within 40 m die, and the water stays clean", alive0));
    }
    // a Hunt at tide 5 brings the Wreckers with their Foreman; they shoot back
    m.Init("ship", 1, 9, false);
    {
        DiverState& q = m.divers[0];
        m.linkOpen.assign(m.linkOpen.size(), 1);
        for (auto& dr : m.level.doors) dr.open = true;
        q.hp = q.hpMax = 5000;
        m.BeginTidePublic(5);
        int wreckers = 0, foreman = 0;
        for (const auto& a : m.eco.agents) if (a.alive && m.map->species[a.sp].isEnemy) { wreckers++; if (a.unit >= 0 && m.map->faction.units[a.unit].huntOnly) foreman++; }
        check(m.phase == TidePhase::Hunt && wreckers >= 4 && foreman == 1, TextFormat("tide 5 is a Hunt: %d Wreckers through the breach, led by the Foreman", wreckers));
        for (const auto& s : m.level.stations) if (s.name == "Bolt Harpoon rack") q.pos = s.pos;
        float hp0 = q.hp;
        for (int i = 0; i < 60 * 20 && q.hp >= hp0; i++) { m.Step(0.05f); q.pos = q.pos; }
        check(q.hp < hp0, TextFormat("the Wreckers find the diver and hurt them (%s)", q.lastHitBy.c_str()));
        for (auto& a : m.eco.agents) if (a.alive && m.map->species[a.sp].isEnemy) m.eco.Damage((int)(&a - &m.eco.agents[0]), 1e6f, q.agent);
        m.Step(0.05f);
        check(m.phase == TidePhase::Calm, "killing the whole force ends the Hunt");
    }
    // the Goliath inhales a diver near its home; 150 to the gills spits them out
    m.Init("ship", 1, 10, false);
    {
        DiverState& q = m.divers[0];
        m.linkOpen.assign(m.linkOpen.size(), 1);
        for (auto& dr : m.level.doors) dr.open = true;
        q.hp = q.hpMax = 5000;
        int g = -1;
        for (int i = 0; i < (int)m.eco.agents.size(); i++) if (m.eco.agents[i].alive && m.map->species[m.eco.agents[i].sp].tier == 5) g = i;
        check(g >= 0, "the Goliath lives in the engine room");
        if (g >= 0) {
            Agent& G = m.eco.agents[g];
            m.power = true;                                       // (before the power is on it only wakes to a shot or a touch)
            q.pos = m.level.Move(G.home, Vector3Add(G.home, {2.5f, 0, 0}), 0.4f, m.linkOpen);
            bool inhaled = false;
            for (int i = 0; i < 40 * 20 && !inhaled; i++) {
                Vector3 keep = Vector3Add(m.eco.agents[g].pos, Vector3Scale(Facing(m.eco.agents[g]), 3.5f));
                if (m.level.Inside(keep, 0.4f, m.linkOpen)) q.pos = keep;
                m.Step(0.05f);
                inhaled = m.bossInhaleT >= 0;
            }
            check(m.bossActive && inhaled, "it wakes and INHALES the diver from 4 m");
            if (inhaled) {
                m.bossGillDmg = 151;
                m.Step(0.05f);
                check(m.bossInhaleT < 0 && !q.downed && m.bossGillsT > 0, "150 to the gills: it spits the diver out and the gills open for 2 s");
            }
        }
    }
    // the ship confines its divers; portholes let darts out; the breach opens to beasts at tide 4
    m.Init("ship", 1, 12, false);
    {
        DiverState& q = m.divers[0];
        q.invulnerable = true;
        int bridge = m.map->ZoneIndex("Bridge"), fore = m.map->ZoneIndex("Foredeck (outside)"), keel = m.map->ZoneIndex("The Keel & Sand");
        int bw = 0, all = (int)m.map->windows.size();
        for (const auto& w : m.map->windows) if (w.zone == bridge) bw++;
        check(all >= 8 && bw >= 2, TextFormat("portholes: %d in all, %d in the Bridge looking onto the Foredeck", all, bw));
        // swim at the hatch: stay in the Bridge
        int hatch = -1;
        for (int li = 0; li < (int)m.map->links.size(); li++) if (m.map->links[li].to == fore || m.map->links[li].from == fore) if (m.map->links[li].from == bridge || m.map->links[li].to == bridge) hatch = li;
        const Link& hl = m.map->links[hatch];
        q.pos = hl.from == bridge ? hl.a : hl.b;
        Vector3 out = hl.from == bridge ? hl.b : hl.a;
        for (int i = 0; i < 200; i++) { m.SteerDiver(0, Vector3Subtract(out, q.pos), Vector3Subtract(out, q.pos).y, false, false, 0.05f); m.Step(0.05f); }
        check(m.eco.ZoneAt(q.pos) == bridge, "the Bridge hatch is a window now: divers can't leave the ship");
        // a fish outside, shot through a porthole
        const Window* w0 = nullptr;
        for (const auto& w : m.map->windows) if (w.zone == bridge) { w0 = &w; break; }
        Vector3 wc = Vector3Lerp(w0->lo, w0->hi, 0.5f);
        Vector3 inside = wc, outside = wc;
        (&inside.x)[w0->axis] = (&m.map->zones[bridge].plan.x)[0] * 0;   // (set below)
        const Zone& bz = m.map->zones[bridge];
        const Zone& oz = m.map->zones[fore];
        float bmid = w0->axis == 0 ? bz.plan.x + bz.plan.width / 2 : bz.plan.y + bz.plan.height / 2;
        float omid = w0->axis == 0 ? oz.plan.x + oz.plan.width / 2 : oz.plan.y + oz.plan.height / 2;
        (&inside.x)[w0->axis] = bmid + (omid > bmid ? 1.0f : -1.0f) * ((w0->axis == 0 ? bz.plan.width : bz.plan.height) / 2 - 1.2f);
        (&outside.x)[w0->axis] = omid;
        int fish = -1;
        for (int i = 0; i < (int)m.eco.agents.size(); i++) { const Agent& a = m.eco.agents[i]; if (a.alive && a.zone == fore && m.map->species[a.sp].size <= 2) { fish = i; break; } }
        check(fish >= 0, "fish swim outside the Bridge's windows");
        if (fish >= 0) {
            Agent& f = m.eco.agents[fish];
            f.pos = outside; f.vel = {0, 0, 0}; f.stun = 5;
            q.pos = inside; q.vel = {0, 0, 0};
            Vector3 dir = Vector3Normalize(Vector3Subtract(f.pos, m.Eye(q)));
            q.yaw = atan2f(dir.x, dir.z); q.pitch = asinf(std::clamp(dir.y, -1.0f, 1.0f)); q.ads = true;
            float hp0 = f.hp; int scrip0 = q.scrip;
            for (int k = 0; k < 6 && f.alive && f.hp >= hp0; k++) { q.fireT = 0; m.Fire(0, true, 0.05f); for (int j = 0; j < 20; j++) { f.pos = outside; f.vel = {0, 0, 0}; m.Step(0.05f); } }
            check(!f.alive || f.hp < hp0, TextFormat("a dart through a porthole hits the %s outside (scrip %d -> %d)", m.map->species[f.sp].name.c_str(), scrip0, q.scrip));
        }
        int bl = -1;
        for (int li = 0; li < (int)m.map->links.size(); li++) if (m.map->links[li].beastRule == 2) bl = li;
        check(bl >= 0 && m.eco.linkClosed[bl] == 1 && !m.breachOpen, "the hull breach is shut to the sharks at tide 1");
        m.BeginTidePublic(4); m.Step(0.05f);
        check(m.breachOpen && m.eco.linkClosed[bl] == 0, "and torn open at tide 4");
        (void)keel;
    }
    // Supper Call: the log, the whistle, three blasts on the boiler cord (power off) wake the Goliath into its fight
    m.Init("ship", 1, 13, false);
    {
        DiverState& q = m.divers[0];
        q.invulnerable = true;
        m.linkOpen.assign(m.linkOpen.size(), 1);
        for (auto& dr : m.level.doors) dr.open = true;
        auto at = [&](int step) { for (const auto& st : m.level.stations) if (st.type == StationType::QuestStep && st.step == step) q.pos = st.pos; };
        at(2); check(!m.Interact(0, false, 0.01f), "the whistle can't be found before the log is read");
        at(1); check(m.Interact(0, false, 0.01f) && m.questStep == 1, "the chief engineer's log");
        at(2); check(m.Interact(0, false, 0.01f) && m.questStep == 2, "the brass steam whistle");
        at(3);
        for (int k = 0; k < 3; k++) { m.Interact(0, false, 0.01f); m.Step(0.5f); }
        check(m.questStep == 3 && m.bossActive && m.supperCall, "three blasts on the boiler's cord: the Goliath comes up for its supper");
    }
    // --redtide-sim runs without crashing (a short one)
    {
        auto B = std::make_unique<Match>();
        B->botStyle = "careful";
        B->Init("ship", 2, 11, true);
        for (int i = 0; i < 20 * 150 && !B->over; i++) B->Step(0.05f);
        int k = 0; for (const auto& q : B->divers) k += q.kills;
        check(k > 0, TextFormat("two bots play 150 s: %d kills, tide %d, %d doors", k, B->tide, B->doorsOpened));
    }
    printf(fails ? "redtide-match-test: %d check(s) failed\n" : "redtide-match-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}

// ---------------------------------------------------------------- --redtide-map-test <map>
// Each later map's own mechanics (stages 5-8), headless.
static int CaveTest(int& fails, const std::function<void(bool, const std::string&)>& check) {
    auto M = std::make_unique<Match>();
    Match& m = *M;
    m.Init("cave", 1, 21, false);
    DiverState& q = m.divers[0];
    q.invulnerable = true;
    m.linkOpen.assign(m.linkOpen.size(), 1);
    for (auto& dr : m.level.doors) dr.open = true;
    const MapData& map = *m.map;
    auto zi = [&](const char* n) { return map.ZoneIndex(n); };
    check(map.zones[m.level.startZone].name == "The Mouth", "the diver starts in the Mouth");
    int slips = 0; for (const auto& l : map.links) slips += l.slip;
    check(slips == 6, TextFormat("six slipstreams (A-F): %d", slips));
    // slipstream A: Gallery -> Chimney at 8 m/s, no firing inside, 2 s of drift
    int la = -1; for (int li = 0; li < (int)map.links.size(); li++) if (map.links[li].passage == "Slipstream A") la = li;
    const Link& A = map.links[la];
    q.pos = A.a; q.zone = A.from; q.vel = {0, 0, 0};
    m.Step(0.05f);
    check(q.slipLink == la, "swimming into slipstream A's mouth catches the diver");
    int mag0 = m.Cur(q).mag; m.Fire(0, true, 0.05f);
    check(m.Cur(q).mag == mag0, "no firing inside a slipstream");
    float t = 0; while (q.slipLink >= 0 && t < 30) { m.Step(0.05f); t += 0.05f; }
    float len = Vector3Distance(A.a, A.b);
    check(q.zone == zi("The Chimney") && fabsf(t - len / 8.0f) < 0.4f && q.driftT > 1.5f, TextFormat("it carries them %.0f m to the Chimney in %.1f s (8 m/s) with 2 s of drift", len, t));
    // beasts ride them too; blood rides them
    check(!m.eco.ZonePath(A.from, A.to).empty() && m.eco.ZonePath(A.from, A.to).size() == 2, "beasts path through a slipstream");
    {
        int roost = zi("Dry Chamber III: the Roost"), dyn = zi("The Dynamo Sump");   // slipstream C: no other way between them
        check(m.eco.ZonePath(roost, dyn).size() == 2 && m.eco.ZonePath(dyn, roost).size() != 2, "slipstream C runs one way only (the Roost's pool to the Dynamo Sump)");
        check(m.eco.ZonePath(roost, dyn, true).size() != 2, "the Drowned never ride one");
    }
    {
        Ecosystem& e = m.eco;
        Vector3 g = map.zones[A.from].Clamp(A.a, 2);
        float c0 = e.Smell(map.zones[A.to].Center(), A.to, 20);
        for (int i = 0; i < 12; i++) e.AddBlood(g, 30);
        float tt = 0; while (e.Smell(map.zones[A.to].Center(), A.to, 20) < c0 + 20 && tt < 40) { e.Step(0.1f); tt += 0.1f; }
        check(tt < 40, TextFormat("blood made at the Gallery's slipstream reads in the Chimney after %.0f s", tt));
    }
    // the dry chambers: divers walk
    int dry = zi("Dry Chamber II: the Toad Pool");
    const Zone& dz = map.zones[dry];
    q.pos = {dz.Center().x, dz.y1 - 1, dz.Center().z}; q.zone = dry; q.vel = {0, 0, 0}; q.driftT = 0;
    Vector3 p0 = q.pos;
    for (int i = 0; i < 60; i++) { m.SteerDiver(0, {1, 0, 0}, 1, false, false, 0.05f); m.Step(0.05f); }
    float moved = Vector2Distance({p0.x, p0.z}, {q.pos.x, q.pos.z}) / 3.0f;
    check(q.pos.y < dz.y0 + 0.8f && moved <= Engine().M("walk_speed_air", 1.6f) + 0.05f, TextFormat("in an air chamber the diver walks on the floor (%.2f m/s, can't swim up)", moved));
    // the Squeeze: no weapons
    int sq = zi("The Squeeze");
    q.pos = map.zones[sq].Center(); q.zone = sq; mag0 = m.Cur(q).mag; q.fireT = 0;
    m.Fire(0, true, 0.05f);
    check(m.Cur(q).mag == mag0, "no weapons drawn in the Squeeze");
    // rockfall and the trap
    int ch = zi("The Chimney");
    const Zone& cz = map.zones[ch];
    int prey = -1;
    for (int i = 0; i < (int)m.eco.agents.size(); i++) if (m.eco.agents[i].alive && m.eco.agents[i].diver < 0 && map.species[m.eco.agents[i].sp].size <= 2) { prey = i; break; }
    Agent& pr = m.eco.agents[prey];
    pr.pos = {cz.Center().x, cz.y0 + 1, cz.Center().z}; pr.zone = ch; pr.stun = 10; pr.hp = 150; pr.hpMax = 150;
    m.DropRocksPublic(pr.pos, 1, 0, 200, 1.6f, 0.3f, 0);
    for (int i = 0; i < 12; i++) { m.eco.agents[prey].pos = pr.pos; m.Step(0.05f); }
    check(!m.eco.agents[prey].alive, "a falling stalactite (200) kills what's under it, and it pays the diver who brought it down");
    int trapIdx = -1; for (int i = 0; i < (int)m.level.stations.size(); i++) if (m.level.stations[i].type == StationType::Trap) trapIdx = i;
    q.pos = m.level.stations[trapIdx].pos; q.zone = ch; q.scrip = 2000; m.power = true;
    size_t c0 = m.crates.size();
    check(m.Interact(0, false, 0.01f) && m.crates.size() >= c0 + 7 && q.scrip == 1000, "the Chimney's trap (1000): the anchor shot loose, seven stalactites fall");
    // flora tools
    int bv = -1, ink = -1, coral = -1;
    for (int i = 0; i < (int)m.eco.flora.size(); i++) {
        const std::string& n = map.flora[m.eco.flora[i].flora].name;
        if (n == "Bloodvine" && bv < 0) bv = i; if (n == "Ink cap" && ink < 0) ink = i; if (n == "Crystal coral" && coral < 0) coral = i;
    }
    check(bv >= 0 && ink >= 0 && coral >= 0, "Bloodvine, ink caps and crystal coral grow where the flora sheet puts them");
    float b0 = m.eco.scent.Total();
    m.FloraToolPublic(bv, m.eco.flora[bv].pos);
    check(m.eco.scent.Total() > b0 + 30 && m.floraBleed.count(bv), "shooting Bloodvine: it smells like 40 blood, and keeps smelling for 30 s");
    int blindMe = prey;
    for (int i = 0; i < (int)m.eco.agents.size(); i++) if (m.eco.agents[i].alive && m.eco.agents[i].diver < 0) { blindMe = i; break; }
    m.eco.agents[blindMe].pos = m.eco.flora[ink].pos; m.eco.agents[blindMe].stun = 0;
    m.FloraToolPublic(ink, m.eco.flora[ink].pos);
    check(m.eco.agents[blindMe].stun > 2, "an ink cap bursts and blinds what's in the cloud");
    m.FloraToolPublic(coral, m.eco.flora[coral].pos);
    check(m.bossRearReq, "the crystal coral rings: the Lobster is called and will Rear");
    // the Drowned: no blood, and the Ghost Worm eats them
    {
        int en = map.enemySpecies;
        check(en >= 0 && map.species[en].bloodless, "the Drowned are bloodless");
        int gw = map.SpeciesIndex("Ghost Worm");
        bool eats = false; for (const auto& pw : map.diet[gw].prey) if (pw.first == en) eats = true;
        check(eats, "the Ghost Worm hunts the Drowned");
        int ai = m.eco.Spawn(en, map.zones[zi("The Flooded Gallery")].Center(), zi("The Flooded Gallery"));
        float s0 = m.eco.scent.Total();
        m.eco.Kill(ai, -1);
        check(m.eco.scent.Total() <= s0 + 0.01f, "a Drowned dies without a drop of blood");
    }
    // the toad's tongue pulls a diver into the pool (a 2 s hold)
    {
        const Attack* tongue = nullptr;
        for (const auto& at : map.attacks) if (at.beast == "Giant Cave Toad" && at.name == "Tongue") tongue = &at;
        int toad = -1; for (int i = 0; i < (int)m.eco.agents.size(); i++) if (m.eco.agents[i].alive && map.species[m.eco.agents[i].sp].name == "Giant Cave Toad") toad = i;
        q.invulnerable = false; q.hp = q.hpMax = 1000; q.heldT = 0;
        m.HitDiverPublic(q, tongue->damage, "Giant Cave Toad", tongue->effect, m.eco.agents[toad].pos, toad);
        check(q.heldT > 1.5f && q.heldT < 2.5f, TextFormat("the toad's tongue: '%s' holds %.1f s", tongue->effect.c_str(), q.heldT));
        q.heldT = 0; q.holder = -1; m.eco.agents[toad].held = 0; q.invulnerable = true;
    }
    // the Resonator: small beasts die in its cone
    {
        int rz = m.WonderIdx();
        check(rz >= 0 && Weapons().weapons[rz].id == "resonator", "the Cave's wonder weapon is the Resonator");
        q.weapons = {Match::NewHeldPublic(rz)}; q.cur = 0;
        int gal = zi("The Flooded Gallery");
        const Zone& gz = map.zones[gal];
        q.pos = {gz.plan.x + 4, (gz.y0 + gz.y1) / 2, gz.plan.y + 8}; q.zone = gal; q.yaw = 1.5708f; q.pitch = 0; q.vel = {0, 0, 0};
        for (int k = 0; k < 40; k++) {   // somewhere with a clear 6 m ahead (the Gallery's columns are solid)
            Vector3 cand{gz.plan.x + 3 + (k % 8) * 4.0f, (gz.y0 + gz.y1) / 2, gz.plan.y + 3 + (k / 8) * 2.5f};
            if (m.level.Inside(cand, 0.4f, m.linkOpen) && m.level.Sight(cand, Vector3Add(cand, {6, 0.1f, 0}), m.linkOpen, true)) { q.pos = cand; break; }
        }
        int fish = -1;
        for (int i = 0; i < (int)m.eco.agents.size(); i++) { const Agent& a = m.eco.agents[i]; if (a.alive && a.diver < 0 && map.species[a.sp].size == 1) { fish = i; break; } }
        m.eco.agents[fish].pos = Vector3Add(q.pos, {5, 0.1f, 0}); m.eco.agents[fish].zone = gal;
        q.fireT = 0; m.Fire(0, true, 0.05f);
        check(!m.eco.agents[fish].alive, "the Resonator's cone kills a small beast outright");
    }
    // the Lobster: Rear exposes the underside; phase 3 rides slipstream E backward and cuts the power
    {
        check(m.bossKind == 1, "the Cave's boss runs the Lobster's script");
        int lob = -1; for (int i = 0; i < (int)m.eco.agents.size(); i++) if (m.eco.agents[i].alive && map.species[m.eco.agents[i].sp].tier == 5) lob = i;
        m.Step(0.05f);
        int cat = zi("The Cathedral");
        const Zone& kz = map.zones[cat];
        Agent& L = m.eco.agents[lob];
        q.pos = kz.Clamp(Vector3Add(L.pos, {6, 0, 0}), 1); q.zone = cat;
        m.bossRearReq = true;
        float tt = 0; while (m.bossGillsT <= 0 && tt < 6) { m.Step(0.05f); tt += 0.05f; }
        check(m.bossActive && m.bossGillsT > 0, TextFormat("rung coral wakes it and it Rears: the underside is open (%.1f s)", tt));
        m.power = true;
        m.eco.agents[lob].hp = m.eco.agents[lob].hpMax * 0.25f;
        m.Step(0.05f); m.Step(0.05f);
        check(m.bossPhase == 3 && m.eco.agents[lob].zone == zi("The Dynamo Sump") && !m.power, "at 30% it rides slipstream E backward into the Dynamo Sump and the power dies");
        m.eco.Kill(lob, q.agent);
        check(m.keys.count("Expedition"), "its kill leaves the expedition's key");
        int cache = -1; for (int i = 0; i < (int)m.level.stations.size(); i++) if (m.level.stations[i].type == StationType::Cache) cache = i;
        q.pos = m.level.stations[cache].pos; q.weapons = {Match::NewHeldPublic(Weapons().Index("cormorant"))}; q.cur = 0;
        check(!m.Interact(0, false, 0.01f), "the key alone doesn't open the Lantern Cache's crate");
        // the expedition's log: three lanterns put out with ink caps
        q.heldT = 0; q.holder = -1; q.downed = false; q.stunT = 0;
        for (int i = 0; i < (int)m.eco.flora.size() && q.inkCaps < 3; i++) if (map.flora[m.eco.flora[i].flora].name == "Ink cap") {
            m.eco.flora[i].units = 1;   // (the one burst earlier has grown back)
            q.pos = m.eco.flora[i].pos; q.zone = m.eco.ZoneAt(q.pos);
            for (int k = 0; k < 3 && q.inkCaps < 3; k++) m.Interact(0, false, 0.01f);
        }
        if (getenv("DEPTH_DBG")) { int n = 0, live = 0; for (const auto& fp : m.eco.flora) if (map.flora[fp.flora].name == "Ink cap") { n++; if (fp.units > 0) live++; } printf("    ink caps %d (%d live), got %d, held %.1f dead %d prompt '%s' hq %d\n", n, live, q.inkCaps, q.heldT, (int)q.dead, m.PromptFor(0).c_str(), map.extra["hidden_quest"]["lanterns"].I(0)); }
        check(q.inkCaps == 3, "ink caps picked from the Gallery and the Cathedral");
        int out = 0;
        for (int i = 0; i < (int)m.level.stations.size(); i++) if (m.level.stations[i].name.find("lantern") != std::string::npos) { q.pos = m.level.stations[i].pos; q.zone = m.level.stations[i].zone; if (m.Interact(0, false, 0.01f)) out++; }
        check(out == 3 && (int)m.lanternsOut.size() == 3, "each puts out a Drowned lantern: three pages of the expedition's log");
        q.pos = m.level.stations[cache].pos; q.zone = m.level.stations[cache].zone;
        m.phase = TidePhase::Calm;
        check(!m.Interact(0, false, 0.01f), "the crate won't open in the calm");
        m.phase = TidePhase::Tide;
        int sq0 = m.eco.squadsSpawned, s0 = q.scrip; float t = 0.05f;
        m.Interact(0, false, 0.05f);   // (E pressed, then held)
        while (!m.cacheOpen && t < 20) { m.Interact(0, true, 0.05f); t += 0.05f; }
        check(m.cacheOpen && t > 14 && t < 16 && m.eco.squadsSpawned > sq0, TextFormat("held open during a tide it takes %.0f s, and the Drowned come for it", t));
        check(m.W(m.Cur(q)).id == "resonator" && q.scrip >= s0 + 2000 && m.eco.entryOverride == "The Chimney", "the Resonator, 2,000 each, and the Drowned enter by the Chimney from now on");
    }
    return fails;
}

static int ReefTest(int& fails, const std::function<void(bool, const std::string&)>& check) {
    auto M = std::make_unique<Match>();
    Match& m = *M;
    m.Init("reef", 2, 33, false);
    DiverState& q = m.divers[0]; DiverState& r = m.divers[1];
    q.invulnerable = r.invulnerable = true;
    m.linkOpen.assign(m.linkOpen.size(), 1);
    for (auto& dr : m.level.doors) dr.open = true;
    const MapData& map = *m.map;
    auto zi = [&](const char* n) { return map.ZoneIndex(n); };
    auto patchOf = [&](const char* n) { for (int i = 0; i < (int)m.eco.flora.size(); i++) if (map.flora[m.eco.flora[i].flora].name == n && m.eco.flora[i].units > 0) return i; return -1; };
    check(m.bossKind == 2, "the Reef's boss runs the Matriarch's script");
    check(m.WonderIdx() >= 0 && Weapons().weapons[m.WonderIdx()].id == "anemonegun", "the Reef's wonder weapon is the Anemone Gun");
    // the tide mill reverses the flow every 4 minutes
    {
        float s0 = m.eco.flowSign;
        m.tideTurnT = 239.99f; m.Step(0.05f);
        check(m.eco.flowSign == -s0, "the tide turns every 240 s: the set reverses");
    }
    // the Surge Channel carries a diver
    {
        int sc = zi("The Surge Channel");
        const Zone& z = map.zones[sc];
        q.pos = z.Center(); q.zone = sc; q.vel = {0, 0, 0};
        for (const auto& l : map.links) if (l.to == sc && l.from == zi("The Bommie")) q.pos = z.Clamp(l.b, 1);   // at the Bommie's mouth
        Vector3 p0 = q.pos;
        for (int i = 0; i < 10; i++) m.Step(0.05f);
        float moved = Vector3Distance(q.pos, p0);
        check(moved > 1.5f, TextFormat("the Surge Channel's current carries a diver (%.1f m in 0.5 s)", moved));
    }
    // Reacher coral: grabs a diver; a teammate cuts them loose, or they hack themselves free
    {
        int rc = patchOf("Reacher coral");
        check(rc >= 0, "Reacher coral grows on the reef");
        const FloraPatch& fp = m.eco.flora[rc];
        q.invulnerable = false; q.hp = q.hpMax = 1000;
        q.pos = fp.pos; q.zone = fp.zone; m.patchT.clear();
        m.FloraHazardsPublic(0.05f);
        check(q.heldT > 0 && q.holder == -2 - rc, "brushing Reacher coral: it holds the diver");
        float hp0 = q.hp; for (int i = 0; i < 20; i++) m.Step(0.05f);
        check(q.hp < hp0 && q.heldT > 0, TextFormat("it squeezes (%.0f HP a second) and doesn't let go", (hp0 - q.hp)));
        r.pos = q.pos; r.zone = q.zone;
        float t = 0; while (q.heldT > 0 && t < 5) { m.Interact(1, true, 0.05f); r.pos = q.pos; m.Step(0.05f); t += 0.05f; }
        check(q.heldT <= 0 && t < 3, TextFormat("a teammate holding E cuts them free in %.1f s", t));
        m.patchT.clear(); q.pos = fp.pos; m.FloraHazardsPublic(0.05f);
        r.pos = {0, -500, 0};
        for (int i = 0; i < 16; i++) m.Interact(0, false, 0.01f);
        m.Step(0.05f);
        check(q.heldT <= 0, "sixteen taps of E hack a diver loose alone");
        q.invulnerable = true; q.pos = map.zones[m.level.startZone].Center(); q.zone = m.level.startZone;
        // it feeds on a small beast that brushes it
        int prey = -1;
        for (int i = 0; i < (int)m.eco.agents.size(); i++) { const Agent& a = m.eco.agents[i]; if (a.alive && a.diver < 0 && map.species[a.sp].size <= 2 && !map.species[a.sp].isEnemy) { prey = i; break; } }
        float tt = 0;
        while (m.eco.agents[prey].alive && tt < 6) { if (!m.reacherPrey.count(prey)) { m.eco.agents[prey].pos = fp.pos; m.eco.agents[prey].zone = fp.zone; } m.Step(0.05f); tt += 0.05f; }
        check(!m.eco.agents[prey].alive && tt > 2.5f, TextFormat("a small beast that brushes it is held and eaten (%.1f s)", tt));
    }
    // the dolphins: allies until a diver shoots one
    {
        const std::string tag = map.extra["allies"]["tag"].Str0("ally");
        int dol = -1;
        for (int i = 0; i < (int)m.eco.agents.size(); i++) if (m.eco.agents[i].alive && map.species[m.eco.agents[i].sp].Has(tag.c_str())) { dol = i; break; }
        check(dol >= 0, "a dolphin pod swims the reef");
        check(!m.alliesHostile, "they start as the divers' allies");
        m.HitAgentPublic(0, dol, 1);
        check(m.alliesHostile, "one shot turns the pod hostile for the match");
        m.alliesHostile = false;
    }
    // the crown-of-thorns: once the staghorn is grazed below 70% the Forest is open to the big sharks
    {
        int fz = zi("Staghorn Forest");
        check(m.eco.zoneMaxSize[fz] == 4, "the Staghorn Forest is too tight for anything bigger than size 4");
        m.eco.erosionT = 4.99f; m.eco.Step(0.05f);
        for (auto& p : m.eco.flora) if (p.zone == fz && map.flora[p.flora].name == "Staghorn thicket") p.units *= 0.5f;
        m.eco.erosionT = 4.99f; m.eco.Step(0.05f);
        check(m.eco.zoneMaxSize[fz] == 99, "eaten below 70%, its corridors open");
    }
    // the Anemone Gun's polyps root and grab
    {
        int prey = -1;
        for (int i = 0; i < (int)m.eco.agents.size(); i++) { const Agent& a = m.eco.agents[i]; if (a.alive && a.diver < 0 && !map.species[a.sp].isEnemy && map.species[a.sp].size >= 2 && a.hp > 100 && !map.species[a.sp].Has("untouchable")) { prey = i; break; } }
        Match::Polyp pp; pp.pos = m.eco.agents[prey].pos; pp.owner = 0; m.polyps.push_back(pp);
        for (auto& a : m.eco.agents) a.held = 0;
        std::vector<char> was; for (const auto& a : m.eco.agents) was.push_back(a.alive && Vector3Distance(a.pos, pp.pos) < 4);
        m.Step(0.05f);
        int held = 0;
        for (int i = 0; i < (int)was.size(); i++) if (was[i] && ((m.eco.agents[i].alive && m.eco.agents[i].held > 1) || !m.eco.agents[i].alive)) held++;
        check(held > 0, "an Anemone Gun polyp stings and holds what swims into it");
        m.polyps.clear();
    }
    // sea grape: +10 HP
    {
        int sg = patchOf("Sea grape");
        check(sg >= 0, "sea grape grows on the reef");
        q.pos = m.eco.flora[sg].pos; q.hp = q.hpMax - 30; float h0 = q.hp;
        m.Interact(0, false, 0.01f);
        check(fabsf(q.hp - h0 - 10) < 0.5f, "eating sea grape: +10 HP");
    }
    // the drum: five beats call the Matriarch; the altar takes it
    {
        q.drumUses = 5;
        for (int i = 0; i < 4; i++) m.BeatDrum(0);
        check(!m.bossActive, "four beats: the reef stirs, nothing more");
        m.BeatDrum(0);
        check(m.bossActive, "the fifth beat wakes the Matriarch");
        int alt = -1; for (int i = 0; i < (int)m.level.stations.size(); i++) if (m.level.stations[i].type == StationType::Quest) alt = i;
        check(alt >= 0, "the drum altar stands on Turtle Beach");
        q.pos = m.level.stations[alt].pos; q.zone = m.level.stations[alt].zone;
        q.drumUses = 1; q.drumClean = false;
        check(!m.Interact(0, false, 0.01f), "the altar won't take a drum that's been beaten");
        q.drumUses = 5; q.drumClean = true; m.alliesHostile = true;
        check(m.Interact(0, false, 0.01f) && m.nesting, "an unbeaten drum on the altar: the turtles come up to nest");
        m.phase = TidePhase::Tide; m.phaseT = 0;
        for (int k = 0; k < 20 * 62 && m.nesting; k++) { q.pos = m.level.stations[alt].pos; m.Step(0.05f); m.phase = TidePhase::Tide; }
        bool forged = false; for (const auto& h : q.weapons) if (h.def == m.WonderIdx() && h.forged) forged = true;
        check(!m.nesting && m.nests == 5 && forged && !m.alliesHostile && m.hammerAvoid, "five nests while the beach is held: the Anemone Gun (Forged), the dolphins back, the hammerheads off the beach");
    }
    // the Matriarch: the pod, the breath, the phases
    {
        int boss = m.bossAgent;
        Agent& B = m.eco.agents[boss];
        q.pos = map.zones[B.zone].Clamp(Vector3Add(B.pos, {10, 0, 0}), 1); q.zone = B.zone;
        m.bossActive = true;
        for (int i = 0; i < 3; i++) m.Step(0.05f);
        int live = 0; for (int p : m.pod) live += m.eco.agents[p].alive;
        check(m.podSpawned && live == 3, "she comes up with a pod of three");
        m.breathT = 89.99f; m.Step(0.05f);
        check(m.bossGillsT > 3, "every 90 s she surfaces: the blowhole is open 4 s");
        m.eco.Kill(m.pod[0], -1); m.eco.Kill(m.pod[1], -1); m.Step(0.05f);
        check(m.bossPhase == 2, "two of the pod dead: phase 2 (straight-line charges)");
        m.eco.agents[boss].hp = m.eco.agents[boss].hpMax * 0.25f; m.Step(0.05f);
        check(m.bossPhase == 3 && m.eco.cleanerRage > 200, "at 30% she rams the Bommie: the cleaning station collapses and every host is angry");
    }
    return fails;
}

static int AtlantisTest(int& fails, const std::function<void(bool, const std::string&)>& check) {
    auto M = std::make_unique<Match>();
    Match& m = *M;
    m.Init("atlantis", 2, 44, false);
    DiverState& q = m.divers[0]; DiverState& r = m.divers[1];
    q.invulnerable = r.invulnerable = true;
    const MapData& map = *m.map;
    auto zi = [&](const char* n) { return map.ZoneIndex(n); };
    auto park = [&](DiverState& d) { d.pos = map.zones[m.level.startZone].Center(); d.zone = m.level.startZone; d.vel = {0, 0, 0}; };
    auto unitIdx = [&](const char* n) { for (int u = 0; u < (int)map.faction.units.size(); u++) if (map.faction.units[u].unit == n) return u; return -1; };
    auto lostOne = [&](const char* unit, Vector3 at, int zone) {
        int ai = m.eco.Spawn(map.enemySpecies, at, zone);
        Agent& a = m.eco.agents[ai]; a.unit = unitIdx(unit); a.hp = a.hpMax = map.faction.units[a.unit].hp; a.stun = 0;
        return ai;
    };
    check(map.title == "Atlantis, the Forgotten City" && map.zones[m.level.startZone].name == "Harbor Gate", "the divers start in the Harbor Gate");
    // the city: 60+ buildings, none overlapping, the avenue up the hill clear
    {
        int n = 0; bool overlap = false, avenue = false;
        std::vector<const Prop*> bs;
        for (const auto& p : m.level.props) if (p.kind == PropKind::Building) { n++; bs.push_back(&p); if (fabsf(p.pos.x) < p.half.x + 2.9f) avenue = true; }
        for (size_t i = 0; i < bs.size(); i++) for (size_t j = i + 1; j < bs.size(); j++)
            if (fabsf(bs[i]->pos.x - bs[j]->pos.x) < bs[i]->half.x + bs[j]->half.x && fabsf(bs[i]->pos.z - bs[j]->pos.z) < bs[i]->half.z + bs[j]->half.z && fabsf(bs[i]->pos.y - bs[j]->pos.y) < bs[i]->half.y + bs[j]->half.y) overlap = true;
        check(n >= 60 && !overlap && !avenue, TextFormat("the city generator lays %d buildings by district, none overlapping, the avenue clear", n));
        int grates = 0; for (const auto& l : map.links) if (map.zones[l.from].name == "The Cisterns") grates++;
        check(grates == 13, TextFormat("twelve drain grates and the god-pool open into the cisterns (%d)", grates));
    }
    // doors: the summit is walled from the lower town; the parade stair opens with the garrison gate
    {
        int lt = zi("The Lower Town"), ch = zi("The Grand Chapel");
        Vector3 a = map.zones[lt].Clamp({0, -60, -31}, 0.6f), b = map.zones[ch].Clamp({0, -29, -29}, 0.6f);
        check(!m.level.Sight(a, b, m.linkOpen), "the Lower Town and the Chapel share a wall, not a street");
        int gg = -1, ps = -1, fort = -1;
        for (int li = 0; li < (int)map.links.size(); li++) { const std::string& p = map.links[li].passage; if (p == "The garrison gate (west)") gg = li; if (HasW(p, "parade stair")) ps = li; if (p == "The wall fort") fort = li; }
        check(gg >= 0 && ps >= 0 && map.links[ps].opensWith == gg && !m.linkOpen[ps], "the parade stair is shut until the garrison gate opens");
        m.linkOpen[gg] = 1; m.level.doors[gg].open = true;
        for (auto& o : m.level.doors) if (map.links[o.link].opensWith == gg) { o.open = true; m.linkOpen[o.link] = 1; }
        check(m.linkOpen[ps] == 1, "and opens with it");
        check(fort >= 0 && m.level.doors[fort].cost == 1500, "the wall fort's postern costs 1,500 (no free way round the doors)");
    }
    // the ring wall: many boxes round the hill, broken by the gate, the open sea ringing it
    {
        int wz = zi("The Wall & Ramparts"), bz = zi("Beyond the Wall"), hz = zi("Harbor Gate");
        const Zone& W = map.zones[wz]; const Zone& B = map.zones[bz]; const Zone& H = map.zones[hz];
        int vis = 0, con = 0; for (const auto& p : W.parts) (p.hidden ? con : vis)++;
        check(vis >= 40 && con >= 20, TextFormat("the wall is a ring of %d boxes with %d hidden seams", vis, con));
        auto ov = [](const Rectangle& a, const Rectangle& b) { return a.x < b.x + b.width - 0.01f && b.x < a.x + a.width - 0.01f && a.y < b.y + b.height - 0.01f && b.y < a.y + a.height - 0.01f; };
        bool gateClear = true, seaClear = true;
        for (const auto& p : W.parts) { if (ov(p.r, H.plan)) gateClear = false; for (const auto& q : B.parts) if (ov(p.r, q.r)) seaClear = false; }
        check(gateClear, "the Harbor Gate stands in the wall's gap (no wall box overlaps it)");
        check(seaClear && B.parts.size() > 40, TextFormat("the sea beyond is a ring of its own (%d boxes) that never overlaps the wall", (int)B.parts.size()));
        int crenels = 0; for (const auto& w : map.windows) if (w.zone == wz && w.outside == bz) crenels++;
        check(crenels >= 30, TextFormat("%d crenellations to shoot over the wall", crenels));
        // a diver swims the rampart from the west round the north to the east, seam by seam
        Vector3 west = W.Clamp({-96, W.y0 + 1.5f, 0}, 0.8f), east = W.Clamp({96, W.y0 + 1.5f, 0}, 0.8f);
        DiverState& q = m.divers[0];
        q.pos = west; q.zone = wz; q.vel = {0, 0, 0};
        float travelled = 0; int steps = 0;
        while (steps++ < 4000 && Vector3Distance(q.pos, east) > 2) {
            Vector3 wp = W.Waypoint(q.pos, east);
            Vector3 dir = Vector3Subtract(wp, q.pos); float l = Vector3Length(dir);
            if (l < 0.01f) break;
            Vector3 np = m.level.Move(q.pos, Vector3Add(q.pos, Vector3Scale(dir, std::min(l, 0.3f) / l)), 0.4f, m.linkOpen);
            travelled += Vector3Distance(np, q.pos); q.pos = np;
        }
        check(Vector3Distance(q.pos, east) <= 2 && travelled > 250 && travelled < 400, TextFormat("a diver swims the rampart from the west wall round to the east (%.0f m of wall walk)", travelled));
        // and a beast steers round it the same way (it can't cut across the city inside the wall)
        int ag = -1; for (int i = 0; i < (int)m.eco.agents.size(); i++) if (m.eco.agents[i].alive && m.eco.agents[i].diver < 0) { ag = i; break; }
        if (ag >= 0) {
            Agent& a = m.eco.agents[ag];
            a.pos = west; a.zone = wz; a.vel = {0, 0, 0}; a.stun = 0; a.held = 0;
            bool left = false;
            for (int k = 0; k < 3000 && Vector3Distance(a.pos, east) > 2; k++) { m.eco.SteerToPublic(a, east, 4, 0.05f); if (!W.Contains(a.pos, 0.35f)) left = true; }
            check(Vector3Distance(a.pos, east) <= 2 && !left, "a beast on the rampart follows the ring to the far side without leaving it");
        }
    }
    m.linkOpen.assign(m.linkOpen.size(), 1);
    for (auto& dr : m.level.doors) dr.open = true;
    // the aqueduct carries the plaza's blood downhill
    {
        Ecosystem& e = m.eco;
        Vector3 plaza{}, market{}, outfall{};
        for (const auto& p : map.pois) { if (HasW(p.name, "plaza (tuna")) plaza = p.pos; if (HasW(p.name, "speed brine")) market = p.pos; if (HasW(p.name, "aqueduct outfall")) outfall = p.pos; }
        int mz = zi("The Lower Town"), gz = zi("Harbor Gate");
        // (only the water moves the blood here: the beasts are set aside so a kill near the gate can't beat it there)
        auto savedAgents = e.agents; auto savedCorpses = e.corpses;
        for (auto& ag : e.agents) if (ag.diver < 0) ag.alive = false;
        for (auto& c : e.corpses) c.active = false;
        float m0 = e.Smell(market, mz, 20), g0 = e.Smell(outfall, gz, 20);
        float tm = -1, tg = -1, t = 0;
        while (t < 240 && (tm < 0 || tg < 0)) {
            if (t < 30) e.AddBlood(plaza, 20);
            e.Step(0.1f); t += 0.1f;
            if (tm < 0 && e.Smell(market, mz, 20) > m0 + 5) tm = t;
            if (tg < 0 && e.Smell(outfall, gz, 20) > g0 + 5) tg = t;
        }
        e.agents = savedAgents; e.corpses = savedCorpses;
        check(tm > 0 && tm < 110, TextFormat("the plaza's blood reaches the market in %.0f s (the doc: 75 s)", tm));
        check(tg > 0 && tg < 200 && tg > tm, TextFormat("and the outfall at the gate in %.0f s (the doc: 120 s)", tg));
    }
    // the Lost Ones
    {
        int lt = zi("The Lower Town");
        const Zone& z = map.zones[lt];
        q.pos = z.Clamp({-20, z.y0 + 2, -45}, 1); q.zone = lt;
        int leg = lostOne("Legionnaire", Vector3Add(q.pos, {0, 0, 6}), lt);
        m.eco.agents[leg].vel = {-1, 0, 0};              // facing into the dart
        float h0 = m.eco.agents[leg].hp;
        m.HitAgentPublic(0, leg, 100);
        if (getenv("DEPTH_DBG")) { std::map<int,int> bz; for (const auto& p : m.level.props) if (p.kind == PropKind::Building) bz[p.zone]++; for (auto& kv : bz) printf("    %s: %d buildings\n", map.zones[kv.first].name.c_str(), kv.second); printf("    legionnaire hp %.0f/%.0f shield %.0f unit %d role %s\n", m.eco.agents[leg].hp, h0, m.shieldHP.count(leg) ? m.shieldHP[leg] : -1.0f, m.eco.agents[leg].unit, map.faction.units[m.eco.agents[leg].unit].role.c_str()); }
        check(m.eco.agents[leg].hp == h0 && m.shieldHP[leg] > 250 && m.shieldHP[leg] < 350, "a Legionnaire's tower shield takes a frontal hit (400 shield HP)");
        m.eco.agents[leg].vel = {1, 0, 0};               // turned away: the dart lands
        m.HitAgentPublic(0, leg, 100);
        check(m.eco.agents[leg].hp < h0, "from behind, the dart gets through");
        int arm = lostOne("Armored Lost One (Hunts)", Vector3Add(q.pos, {3, 0, 6}), lt);
        m.eco.agents[arm].vel = {-1, 0, 0};
        float a0 = m.eco.agents[arm].hp;
        for (int k = 0; k < 5; k++) m.HitAgentPublic(0, arm, 300);
        check(m.eco.agents[arm].hp == a0, "the Armored Lost One's titan shield can't be shot through from the front");
        m.eco.agents[leg].st = State::Rest;
        int cul = lostOne("Cultist", Vector3Add(q.pos, {-2, 0, 4}), lt);
        q.slowT = 0;
        for (int k = 0; k < 20 && q.slowT <= 0; k++) m.Step(0.05f);
        check(q.slowT > 0, "a Cultist's chant slows a diver within 10 m");
        // ichor: a dead Lost One keeps the sharks off and brings the crabs
        Vector3 at = m.eco.agents[cul].pos;
        m.eco.Kill(cul, q.agent);
        check(!m.ichor.empty() && Vector3Distance(m.ichor.back().pos, at) < 0.5f, "a dead Lost One leaves black ichor in the street");
        int ss = map.SpeciesIndex("Sandbar Shark");
        int sh = m.eco.Spawn(ss, map.zones[lt].Clamp(Vector3Add(at, {4, 0, 0}), 1), lt);
        float d0 = Vector3Distance(m.eco.agents[sh].pos, at);
        for (int k = 0; k < 60; k++) m.eco.Step(0.05f);
        check(Vector3Distance(m.eco.agents[sh].pos, at) > d0 + 1.5f, "a shark turns away from the ichor");
        int cu = -1; for (int i = 0; i < (int)m.eco.agents.size(); i++) if (m.eco.agents[i].alive && map.species[m.eco.agents[i].sp].name == "Sea Cucumber" && m.eco.agents[i].zone == lt) { cu = i; break; }
        if (cu < 0) cu = m.eco.Spawn(map.SpeciesIndex("Sea Cucumber"), at, lt);
        if (cu >= 0) { m.eco.agents[cu].pos = map.zones[lt].Clamp(Vector3Add(at, {-10, 0, 0}), 1); m.eco.agents[cu].st = State::Graze; m.eco.agents[cu].fedT = 0; }
        bool comes = false;
        for (int k = 0; k < 60 && cu >= 0 && !comes; k++) { m.eco.Step(0.05f); comes = m.eco.agents[cu].st == State::Investigate && Vector3Distance(m.eco.agents[cu].goal, at) < 1; }
        if (getenv("DEPTH_DBG") && cu >= 0) printf("    cucumber %d state %s goal %.1f %.1f %.1f at %.1f %.1f %.1f zone %d/%d ichor %d\n", cu, StateName(m.eco.agents[cu].st), m.eco.agents[cu].goal.x, m.eco.agents[cu].goal.y, m.eco.agents[cu].goal.z, at.x, at.y, at.z, m.eco.agents[cu].zone, m.eco.ZoneAt(at), (int)m.ichor.size());
        check(comes, "a sea cucumber comes for it");
        // the sluice drowns a phalanx
        int fz = zi("The Farms"); int sl = -1;
        for (int i = 0; i < (int)m.level.stations.size(); i++) if (HasW(m.level.stations[i].name, "sluice")) sl = i;
        int l2 = lostOne("Legionnaire", map.zones[fz].Clamp(Vector3Add(m.level.stations[sl].pos, {3, 0, 3}), 1), fz);
        q.pos = m.level.stations[sl].pos; q.zone = fz; q.scrip = 5000; m.power = true;
        if (getenv("DEPTH_DBG")) printf("    prompt at the sluice: '%s' near %d (sl %d) dead %d downed %d type %d pos %.1f %.1f %.1f\n", m.PromptFor(0).c_str(), m.NearestStation(q.pos, 2), sl, q.dead, q.downed, (int)m.level.stations[sl].type, q.pos.x, q.pos.y, q.pos.z);
        check(m.Interact(0, false, 0.01f) && !m.eco.agents[l2].alive, "the canal sluice floods the terrace: a Legionnaire drowns in his armour");
        m.trapT = 0;
    }
    // the Priest: 60 s of chanting calls the Alien Horror; it throws a diver 20 m
    {
        int fo = zi("The Forum");
        Vector3 plaza{}; for (const auto& p : map.pois) if (HasW(p.name, "plaza (tuna")) plaza = p.pos;
        q.pos = map.zones[fo].Clamp(Vector3Add(plaza, {6, 0, 0}), 1); q.zone = fo;
        int pr = lostOne("The Priest (Hunts)", map.zones[fo].Clamp(plaza, 1), fo);
        m.priestT = 59.9f;
        m.Step(0.05f); m.Step(0.05f); m.Step(0.05f);
        check(m.horror >= 0 && map.species[m.eco.agents[m.horror].sp].name == "Alien Horror", "after 60 s of the Priest's chant the Alien Horror arrives");
        Vector3 p0 = q.pos; m.horrorTeleT = 0.01f; m.Step(0.05f);
        check(Vector3Distance(q.pos, p0) > 15, TextFormat("it throws a diver %.0f m", Vector3Distance(q.pos, p0)));
        m.horrorT = 0.01f; m.Step(0.05f);
        check(m.horror < 0, "after 90 s it folds back into the sigil");
        m.eco.Kill(pr, -1);
    }
    // the Cistern Wyrm
    {
        check(m.bossKind == 3, "Atlantis's boss runs the Cistern Wyrm's script");
        int boss = m.bossAgent; if (boss < 0) { m.Step(0.05f); boss = m.bossAgent; }
        int ch = zi("The Grand Chapel");
        const Zone& cz = map.zones[ch];
        park(q); park(r);
        for (int k = 0; k < 40; k++) m.Step(0.05f);
        check(m.wyrmState == 0 && map.zones[m.eco.agents[boss].zone].name == "The Cisterns", "it waits in the cisterns while nobody is on the chapel floor");
        float h0 = m.eco.agents[boss].hp;
        m.HitAgentPublic(0, boss, 500);
        check(m.eco.agents[boss].hp == h0, "below, nothing reaches it");
        q.invulnerable = false; q.hp = q.hpMax = 5000;
        q.pos = cz.Clamp({0, cz.y0 + 1.5f, -14}, 1); q.zone = ch;
        float t = 0; while (m.wyrmState < 2 && t < 12) { m.Step(0.05f); t += 0.05f; }
        std::string grate = m.wyrmGrate >= 0 ? map.links[m.wyrmGrate].passage : "";
        check(m.wyrmState == 2 && (grate == "The god-pool" || grate == "Grate G11" || grate == "Grate G12"), TextFormat("a diver in the chapel: it comes up through %s after %.1f s", grate.c_str(), t));
        float hp0 = q.hp; bool dragged = false;
        for (int k = 0; k < 16; k++) { m.Step(0.05f); if (m.wyrmDragDiver >= 0) dragged = true; }
        check(q.hp < hp0 || dragged, "the surface strike lands (or it drags the diver below)");
        if (m.wyrmDragDiver >= 0) { m.HitAgentPublic(1, boss, 250); m.Step(0.05f); check(m.wyrmDragDiver < 0 && q.heldT <= 0, "200 into its gills and it lets go"); }
        float hb = m.eco.agents[boss].hp; m.HitAgentPublic(0, boss, 100);
        check(m.eco.agents[boss].hp < hb, "surfaced, it can be hurt");
        // a forced drag, freed by the team
        if (m.wyrmState == 2) {
            m.wyrmDragDiver = 0; m.wyrmDragT = 6; m.wyrmDragDmg = 0; q.heldT = 6.5f; q.holder = boss;
            m.HitAgentPublic(1, boss, 210); m.Step(0.05f);
            check(m.wyrmDragDiver < 0, "Drag below: 200 into the gills within 6 s frees the diver");
        }
        q.holder = -1; q.heldT = 0;
        // the god-pool trap strands it while it's home
        while (m.wyrmState != 0) m.Step(0.05f);
        int tp = -1; for (int i = 0; i < (int)m.level.stations.size(); i++) if (HasW(m.level.stations[i].name, "god-pool trap")) tp = i;
        r.pos = m.level.stations[tp].pos; r.zone = ch; r.scrip = 5000; m.trapT = 0;
        if (getenv("DEPTH_DBG")) printf("    prompt at the god-pool trap: '%s' phase %d state %d\n", m.PromptFor(1).c_str(), m.bossPhase, m.wyrmState);
        check(m.Interact(1, false, 0.01f) && m.wyrmStrandT > 5 && m.bossGillsT > 5, "the god-pool trap (1,000) strands it for 6 s, gills bare");
        // phases
        m.eco.agents[boss].hp = m.eco.agents[boss].hpMax * 0.5f;
        int cong0 = 0; for (const auto& a : m.eco.agents) if (a.alive && map.species[a.sp].name == "Conger Eel") cong0++;
        m.Step(0.05f);
        int cong1 = 0; for (const auto& a : m.eco.agents) if (a.alive && map.species[a.sp].name == "Conger Eel") cong1++;
        check(m.bossPhase == 2 && cong1 > cong0, TextFormat("phase 2: congers pour from the other grates (%d more)", cong1 - cong0));
        m.eco.agents[boss].hp = m.eco.agents[boss].hpMax * 0.25f;
        m.Step(0.05f);
        int gw = map.SpeciesIndex("Great White"); bool whiteHunts = false;
        for (const auto& a : m.eco.agents) if (a.alive && a.sp == gw && a.st == State::Hunt) whiteHunts = true;
        check(m.bossPhase == 3 && m.floodT > 0 && whiteHunts, "phase 3: the Flood, and the great white comes over the wall");
        q.invulnerable = true; q.hp = q.hpMax;
        m.eco.Kill(boss, q.agent);
        check(m.keys.count("Lighthouse") && m.wyrmReformT > 300, "its kill leaves the lighthouse key; it will reform in 6 minutes");
        m.wyrmReformT = 0.01f; m.Step(0.05f);
        check(m.bossAgent >= 0 && m.eco.agents[m.bossAgent].alive && fabsf(m.eco.agents[m.bossAgent].hp - m.eco.agents[m.bossAgent].hpMax * 0.5f) < 1, "it reforms in the god-pool at half health");
    }
    // the Tide Staff quest
    {
        auto st = [&](int step) { for (const auto& s : m.level.stations) if (s.type == StationType::QuestStep && s.step == step) return s.pos; return Vector3{0, 0, 0}; };
        auto go = [&](int step) { q.pos = st(step); q.zone = m.eco.ZoneAt(q.pos); };
        m.wyrmState = 0; m.wyrmT = 999;
        go(1); check(m.Interact(0, false, 0.01f) && q.spark && m.questAt[0] == 2, "the treasury's crystal: a spark in a jar");
        go(2); m.Interact(0, false, 0.01f); go(3); m.Interact(0, false, 0.01f); go(4); m.Interact(0, false, 0.01f);
        check(m.questAt[0] == 5, "the spark lights the chapel's three braziers");
        go(5); r.pos = q.pos; r.zone = q.zone;
        for (int k = 0; k < 20 * 46; k++) { q.pos = st(5); m.Step(0.05f); }
        check(m.questAt[0] == 6, "holding the plaza for 45 s");
        int ek = lostOne("Legionnaire", map.zones[q.zone].Clamp(Vector3Add(q.pos, {2, 0, 0}), 1), q.zone);
        m.eco.Kill(ek, q.agent);
        q.pos = m.ichor.back().pos;
        check(m.Interact(0, false, 0.01f) && q.ichorJar, "a jar filled with a dead Lost One's ichor");
        go(6); int w0 = (int)q.weapons.size(); (void)w0;
        check(m.Interact(0, false, 0.01f) && m.W(m.Cur(q)).id == "tidestaff", "offered at the god-pool while the Wyrm is below: the Tide Staff");
    }
    // the Tide Staff's wave
    {
        int fz = zi("The Farms");
        const Zone& z = map.zones[fz];
        q.pos = z.Clamp({-40, z.y0 + 2, -84}, 1); q.zone = fz; q.yaw = 0; q.pitch = 0; q.fireT = 0; q.vel = {0, 0, 0};
        int b = -1; for (int i = 0; i < (int)m.eco.agents.size(); i++) if (m.eco.agents[i].alive && map.species[m.eco.agents[i].sp].name == "Salema") { b = i; break; }
        m.eco.agents[b].pos = Vector3Add(q.pos, {0, 0.1f, 5}); m.eco.agents[b].zone = fz; m.eco.agents[b].hp = m.eco.agents[b].hpMax = 5000;
        Vector3 b0 = m.eco.agents[b].pos;
        m.Fire(0, true, 0.05f);
        check(m.eco.agents[b].pos.z > b0.z + 6, TextFormat("the Tide Staff's wave carries a beast %.0f m", m.eco.agents[b].pos.z - b0.z));
    }
    // the lighthouse
    {
        auto st = [&](int step) { for (const auto& s : m.level.stations) if (s.type == StationType::QuestStep && s.step == step) return s.pos; return Vector3{0, 0, 0}; };
        q.spark = true;
        for (int s = 11; s <= 14; s++) { q.pos = st(s); q.zone = m.eco.ZoneAt(q.pos); m.Interact(0, false, 0.01f); }
        check(m.questAt[1] == 15, "the spark lights the four wall-fires");
        int s0 = r.scrip;
        q.pos = st(15); q.zone = m.eco.ZoneAt(q.pos);
        check(m.Interact(0, false, 0.01f) && m.revealAll && r.scrip >= s0 + 2400 && m.Cur(q).forged, "the lighthouse burns: the city on the sonar, the vault's 5,000 split, the Sovereign's Staff for the lighter");
    }
    return fails;
}

static int VoidTest(int& fails, const std::function<void(bool, const std::string&)>& check) {
    auto M = std::make_unique<Match>();
    Match& m = *M;
    m.Init("void", 2, 55, false);
    DiverState& q = m.divers[0]; DiverState& r = m.divers[1];
    q.invulnerable = r.invulnerable = true;
    const MapData& map = *m.map;
    auto zi = [&](const char* n) { return map.ZoneIndex(n); };
    auto unitIdx = [&](const char* n) { for (int u = 0; u < (int)map.faction.units.size(); u++) if (map.faction.units[u].unit == n) return u; return -1; };
    auto remnant = [&](const char* unit, Vector3 at, int zone) {
        int ai = m.eco.Spawn(map.enemySpecies, at, zone);
        Agent& a = m.eco.agents[ai]; a.unit = unitIdx(unit); a.hp = a.hpMax = map.faction.units[a.unit].hp; a.oil = map.faction.units[a.unit].bloodless; a.stun = 0;
        return ai;
    };
    auto stationPos = [&](const char* name) { for (const auto& s : m.level.stations) if (HasW(s.name, name)) return s.pos; return Vector3{1e9f, 0, 0}; };
    auto stationIdx = [&](const char* name) { for (int i = 0; i < (int)m.level.stations.size(); i++) if (HasW(m.level.stations[i].name, name)) return i; return -1; };
    int rim = zi("The Rim"), abyss = zi("The Abyss");
    check(map.title == "Approaching the Void" && m.level.startZone == rim && Vector2Length({q.pos.x, q.pos.z}) < 5, "the divers start at the station's surface hatch on the rim");
    check(map.zones[rim].plan.width >= 800 && map.zones[rim].plan.height >= 800, "the rim is 800 m across");
    // the Sand Worm
    {
        q.invulnerable = false;
        q.pos = {360, 2, 0}; q.zone = rim; q.vel = {0, 0, 0};
        float t = 0; while (!q.dead && t < 12) { q.pos = {360, 2, 0}; m.Step(0.05f); t += 0.05f; }
        check(q.dead && t > 7.5f && t < 8.5f && m.downsBy.count("the Sand Worm"), TextFormat("past the bone stakes the sand shakes for 8 s and the worm takes the diver (%.1f s; no down, no revive)", t));
        q.dead = false; q.downed = false; q.hp = q.hpMax; if (q.agent >= 0) { m.eco.agents[q.agent].alive = true; m.eco.agents[q.agent].downed = false; }
        q.pos = {360, 2, 0}; for (int k = 0; k < 60; k++) { q.pos = {360, 2, 0}; m.Step(0.05f); }
        q.pos = {100, 2, 0}; m.Step(0.05f);
        check(q.wormT == 0 && !q.dead, "stepping back inside the stakes stills the sand");
    }
    // the void
    {
        const Zone& az = map.zones[abyss];
        q.pos = {az.plan.x + az.plan.width * 0.5f, -50, 0}; q.zone = abyss; q.vel = {0, 0, 0};
        float t = 0; while (!q.dead && t < 8) { m.Step(0.05f); t += 0.05f; }
        check(q.dead && t > 4.5f && t < 5.5f, TextFormat("out over the abyss the void pulls a diver down for 5 s, then they're gone (%.1f s)", t));
        q.dead = false; q.downed = false; q.hp = q.hpMax; q.voidT = 0; if (q.agent >= 0) { m.eco.agents[q.agent].alive = true; m.eco.agents[q.agent].downed = false; }
        q.invulnerable = true; q.pos = m.level.start; q.zone = rim;
        bool plan = false; for (int li = 0; li < (int)map.links.size(); li++) if ((map.links[li].to == abyss || map.links[li].from == abyss) && m.DiverLink(li)) plan = true;
        check(!plan && m.Forbidden({az.Center().x, -50, 0}) && m.Forbidden({500, 2, 0}) && !m.Forbidden({0, 2, 0}), "no route is planned through the void, and the bots keep out of it and past the stakes");
    }
    m.linkOpen.assign(m.linkOpen.size(), 1);
    for (auto& dr : m.level.doors) dr.open = true;
    // the Remnant
    {
        int gal = zi("The Upper Galleries");
        const Zone& gz = map.zones[gal];
        int sen = remnant("Sentinel", gz.Clamp({0, gz.y0 + 2, -30}, 1), gal);
        check(m.eco.agents[sen].oil, "a Sentinel leaks oil, not blood");
        float s0 = m.eco.scent.Total();
        m.HitAgentPublic(0, sen, 100);
        check(m.eco.scent.Total() <= s0 + 0.01f, "a hit Sentinel puts no blood in the water");
        // its arc: behind it, a diver is safe; in front, after the spin-up, it fires
        m.eco.agents[sen].pos = gz.Clamp({0, gz.y0 + 2, -30}, 1);
        m.sentinelYaw[sen] = 0;                                   // facing +z
        q.pos = Vector3Add(m.eco.agents[sen].pos, {0, 0, -10}); q.zone = gal; q.invulnerable = false; q.hp = q.hpMax = 5000;
        size_t d0 = m.darts.size(); int fired = 0;
        for (int k = 0; k < 10; k++) { m.eco.agents[sen].pos = gz.Clamp({0, gz.y0 + 2, -30}, 1); m.eco.agents[sen].vel = {0, 0, 0}; m.Step(0.05f); for (size_t j = d0; j < m.darts.size(); j++) if (m.darts[j].enemy == sen) fired++; d0 = m.darts.size(); }
        check(fired == 0, "behind a Sentinel (outside its 90-degree arc) it doesn't fire at once");
        q.pos = Vector3Add(m.eco.agents[sen].pos, {0, 0, 10}); m.sentinelYaw[sen] = 0;
        float t = 0, first = -1;
        for (int k = 0; k < 60; k++) { m.eco.agents[sen].pos = gz.Clamp({0, gz.y0 + 2, -30}, 1); m.Step(0.05f); t += 0.05f; for (auto& dt : m.darts) if (dt.enemy == sen && first < 0) first = t; }
        check(first > 0.8f && first < 1.4f, TextFormat("in front, it spins up (1 s) and fires (%.2f s)", first));
        m.eco.Kill(sen, -1); m.darts.clear(); q.invulnerable = true;
        // a Researcher's gas, and the beacons it lights
        int res = remnant("Researcher", gz.Clamp({80, gz.y0 + 2, -30}, 1), gal);
        int b1 = stationIdx("beacon 1");
        m.eco.agents[res].pos = Vector3Add(m.level.stations[b1].pos, {2, 0, 0});
        m.Step(0.05f); m.Step(0.05f);
        check(m.beaconOn[b1], "a Researcher switches on the lure beacon it passes");
        int fang = -1; for (int i = 0; i < (int)m.eco.agents.size(); i++) if (m.eco.agents[i].alive && map.species[m.eco.agents[i].sp].size >= 2 && !map.species[m.eco.agents[i].sp].isEnemy && !m.IsBoss(i) && m.eco.agents[i].diver < 0) { fang = i; break; }
        m.eco.agents[fang].pos = gz.Clamp(Vector3Add(m.level.stations[b1].pos, {-10, 0, 0}), 1); m.eco.agents[fang].zone = gal; m.eco.agents[fang].st = State::Graze;
        q.pos = gz.Clamp(Vector3Add(m.level.stations[b1].pos, {-20, 0, 0}), 1); q.zone = gal;
        m.beaconTick = 2.1f; m.Step(0.05f);
        check(m.eco.agents[fang].st == State::Investigate && Vector3Distance(m.eco.agents[fang].goal, q.pos) < 2, "an on beacon pulls the beasts around it onto the nearest diver");
        q.pos = m.level.stations[b1].pos;
        check(m.Interact(0, false, 0.01f) && !m.beaconOn[b1], "a diver turns the beacon toward the abyss (E)");
        m.eco.Kill(res, -1);
        m.gas.push_back({q.pos, 6, 15, 4});
        q.invulnerable = false; float hp0 = q.hp; for (int k = 0; k < 20; k++) m.Step(0.05f);
        check(hp0 - q.hp > 10, TextFormat("a gas grenade's cloud: 15/s (%.0f in 1 s)", hp0 - q.hp));
        m.gas.clear(); q.invulnerable = true; q.hp = q.hpMax;
        // a Void Cultist at the overlook calls the deep
        int cu = remnant("Void Cultist", gz.Clamp({-150, gz.y0 + 2, -20}, 1), gal);
        q.pos = gz.Clamp({-100, gz.y0 + 2, -20}, 1);
        for (int k = 0; k < 200 && m.tentacleT <= 0; k++) m.Step(0.05f);
        check(m.tentacleT > 0, "a Void Cultist reaches the overlook and a colossal squid's arm rises over it");
        m.eco.Kill(cu, -1); m.tentacleT = 0;
    }
    // specimens breed if nobody culls them
    {
        int mess = zi("The Station: Mess & Quarters"), sal = map.SpeciesIndex("Lab Salamander");
        for (auto& a : m.eco.agents) if (a.alive && a.sp == map.SpeciesIndex("Pale Caiman")) a.alive = false;
        int n0 = m.eco.CountInZone(sal, mess);
        if (n0 < 2) { m.eco.Spawn(sal, map.zones[mess].Center(), mess); m.eco.Spawn(sal, map.zones[mess].Center(), mess); n0 = m.eco.CountInZone(sal, mess); }
        for (int k = 0; k < 20 * 100; k++) m.Step(0.05f);
        check(m.eco.CountInZone(sal, mess) > n0, TextFormat("salamanders breed in the bunks (%d -> %d in 100 s)", n0, m.eco.CountInZone(sal, mess)));
    }
    // a whale kill brings the Relict
    {
        int wh = -1, rel = -1;
        for (int i = 0; i < (int)m.eco.agents.size(); i++) { if (!m.eco.agents[i].alive) continue; const std::string& n = map.species[m.eco.agents[i].sp].name; if (n == "Sperm Whale") wh = i; if (n == "The Relict") rel = i; }
        check(wh >= 0 && rel >= 0, "the sperm whale and the Relict swim the abyss");
        Vector3 at = m.eco.agents[wh].pos;
        m.eco.Kill(wh, q.agent);
        check(m.eco.agents[rel].st == State::Investigate && Vector3Distance(m.eco.agents[rel].goal, at) < 1, "a whale kill brings the Relict up to its body");
    }
    // the Lantern Leviathan
    {
        check(m.bossKind == 4, "the Void's boss runs the Lantern Leviathan's script");
        if (m.bossAgent < 0) m.Step(0.05f);
        int boss = m.bossAgent;
        int vault = zi("The Lowest Vault");
        const Zone& vz = map.zones[vault];
        m.bossActive = true;
        m.eco.agents[boss].vel = {1, 0, 0}; m.eco.agents[boss].pos = vz.Center(); m.eco.agents[boss].zone = vault;
        float h0 = m.eco.agents[boss].hp;
        m.HitAgentPublic(0, boss, 100);
        check(fabsf((h0 - m.eco.agents[boss].hp) - 50) < 1, "its dark body takes half");
        // the lure shot out
        Vector3 lp = m.LurePos();
        q.pos = vz.Clamp(Vector3Add(lp, {6, 0, 0}), 1); q.zone = vault;   // in front of it, facing the light q.yaw = atan2f(lp.x - q.pos.x, lp.z - q.pos.z); q.pitch = asinf(std::clamp((lp.y - m.Eye(q).y) / std::max(0.1f, Vector3Distance(lp, m.Eye(q))), -1.0f, 1.0f));
        m.lureHP = 10;
        Dart t; t.pos = t.start = m.Eye(q); t.vel = Vector3Scale(Vector3Normalize(Vector3Subtract(lp, m.Eye(q))), 30); t.damage = 60; t.owner = 0; t.weapon = 0;
        m.darts.push_back(t);
        for (int k = 0; k < 10 && m.lureHP > 0; k++) { m.eco.agents[boss].vel = {1, 0, 0}; m.eco.agents[boss].pos = vz.Center(); m.Step(0.02f); }
        if (getenv("DEPTH_DBG")) printf("    lureHP %.0f darts %d active %d lure %.1f %.1f %.1f eye %.1f %.1f %.1f inside %d sight %d\n", m.lureHP, (int)m.darts.size(), (int)m.bossActive, lp.x, lp.y, lp.z, m.Eye(q).x, m.Eye(q).y, m.Eye(q).z, (int)m.level.Inside(lp, 0.1f, m.linkOpen, true), (int)m.level.Sight(m.Eye(q), lp, m.linkOpen, true));
        check(m.lureHP <= 0 && m.bossGillsT > 4 && m.eco.agents[boss].stun > 3, "shooting out the lure (500) blinds it 5 s and bares the gills");
        // the swallow
        m.eco.agents[boss].stun = 0; m.bossGillsT = 0;
        m.swallowDiver = 0; m.swallowT = 5; m.swallowDmg = 0; q.heldT = 5.5f; q.holder = boss;
        m.HitAgentPublic(1, boss, 220);
        m.Step(0.05f);
        check(m.swallowDiver < 0 && m.bossGillsT > 1, "swallowed whole: 200 into it in 5 s and it spits the diver out, gills bare for 2 s");
        q.heldT = 0; q.holder = -1;
        m.eco.Kill(boss, q.agent);
        check(m.keys.count("Master") && m.bossReformT > 300, "its kill leaves the station's master key; it reforms in 6 minutes");
    }
    // traps
    {
        int lab = zi("The Station: Specimen Labs");
        Vector3 door{}; for (const auto& l : map.links) if (HasW(l.passage, "pressure door")) door = Vector3Lerp(l.a, l.b, 0.5f);
        int sen = remnant("Sentinel", door, lab);
        m.eco.agents[sen].pos = door;
        q.pos = stationPos("pressure door control"); q.zone = lab; q.scrip = 9000; m.power = true; m.trapT = 0;
        check(m.Interact(0, false, 0.01f) && !m.eco.agents[sen].alive, "the pressure door crushes the corridor: a Sentinel dies in it");
    }
    // the final log
    {
        auto st = [&](int step) { for (const auto& s : m.level.stations) if (s.type == StationType::QuestStep && s.step == step) return s.pos; return Vector3{0, 0, 0}; };
        auto go = [&](int step) { q.pos = st(step); q.zone = m.eco.ZoneAt(q.pos); };
        int sq0 = m.eco.squadsSpawned;
        for (int s = 1; s <= 5; s++) { go(s); m.Interact(0, false, 0.01f); }
        check(m.questAt[0] == 6 && m.eco.squadsSpawned >= sq0 + 5, "five terminals read in order, a Remnant squad on each");
        go(6); check(m.Interact(0, false, 0.01f) && q.egg, "tank 7 opens with the master key: the Relict egg");
        int rel = -1; for (int i = 0; i < (int)m.eco.agents.size(); i++) if (m.eco.agents[i].alive && map.species[m.eco.agents[i].sp].name == "The Relict") rel = i;
        m.eco.agents[rel].decideT = 0; m.Step(0.2f);
        check(m.eco.agents[rel].st == State::Hunt && m.eco.agents[rel].target == q.agent, "the Relict comes for the egg wherever it is");
        go(7); check(m.Interact(0, false, 0.01f) && m.eggPlaced && !q.egg, "the egg set in the ledge's cradle");
        m.eco.agents[rel].pos = m.eggPos; m.eco.agents[rel].zone = m.eco.ZoneAt(m.eggPos);
        int s0 = r.scrip;
        q.pos = stationPos("overlook 5"); q.pos.x += 9; q.zone = zi("The Lowest Vault"); m.trapT = 0; q.scrip = 9000;
        q.pos = stationPos("overlook 5");
        Vector3 safe = Vector3Add(q.pos, {12, 0, 0});
        bool ok = m.Interact(0, false, 0.01f);
        (void)safe;
        check(ok && m.relictGone && m.questAt[0] == 9, "the ledge drops with the Relict on it: it falls into the void");
        check(r.scrip >= s0 + 2400, "the final log's reward: 5,000 scrip split");
    }
    // the Abyssal Lure: a lantern every beast in 60 m comes to, then a burst
    {
        int wi = m.WonderIdx();
        check(wi >= 0 && Weapons().weapons[wi].id == "abyssallure", "the Void's wonder weapon is the Abyssal Lure");
        int gal = zi("The Upper Galleries");
        const Zone& gz = map.zones[gal];
        Match::LurePt lp; lp.pos = gz.Clamp({0, gz.y0 + 3, -20}, 1); lp.owner = 0; m.lures.push_back(lp);
        int b = -1; for (int i = 0; i < (int)m.eco.agents.size(); i++) if (m.eco.agents[i].alive && !m.IsBoss(i) && m.eco.agents[i].diver < 0 && !map.species[m.eco.agents[i].sp].isEnemy && Vector3Distance(m.eco.agents[i].pos, lp.pos) < 50) { b = i; break; }
        m.Step(0.05f);
        check(b >= 0 && m.eco.agents[b].st == State::Investigate && Vector3Distance(m.eco.agents[b].goal, lp.pos) < 1, "every beast in 60 m comes to the Abyssal Lure's light");
        m.lures.back().t = 0.01f; m.Step(0.05f);
        check(m.lures.empty(), "after 8 s it bursts");
    }
    return fails;
}

static int ShipQuestTest(int& fails, const std::function<void(bool, const std::string&)>& check) {
    auto M = std::make_unique<Match>();
    Match& m = *M;
    m.Init("ship", 1, 31, false);
    DiverState& q = m.divers[0]; q.invulnerable = true;
    int logI = -1, safeI = -1;
    for (int i = 0; i < (int)m.level.stations.size(); i++) { const Station& s = m.level.stations[i]; if (s.type != StationType::Quest) continue; if (s.name.find("log") != std::string::npos) logI = i; else safeI = i; }
    check(logI >= 0 && safeI >= 0, "the captain's log and the safe are on the bridge");
    q.pos = m.level.stations[logI].pos;
    check(!m.Interact(0, false, 0.01f) && !m.logRead, "before tide 4 the log's pages are blank");
    m.tide = 4;
    check(m.Interact(0, false, 0.01f) && m.logRead, "after tide 3 the chalk shows: the log tells whose keys");
    m.keys = {"Goliath", "Octopus", "Foreman"};
    q.pos = m.level.stations[safeI].pos;
    float n0 = m.eco.sound.Total();
    float t = 0; int s0 = q.scrip;
    m.Interact(0, false, 0.05f);
    check(m.eco.sound.Total() > n0 + 5, "turning the wheel rings the ship's bell");
    while (!m.safeOpen && t < 25) { m.Interact(0, true, 0.05f); t += 0.05f; }
    bool keel = false; for (const auto& h : q.weapons) if (h.def == m.WonderIdx() && h.forged) keel = true;
    check(m.safeOpen && t > 19 && t < 21, TextFormat("the safe takes %.0f s to open", t));
    check(q.scrip >= s0 + 3000 && keel && std::find(m.bonusEarned.begin(), m.bonusEarned.end(), std::string("owners")) != m.bonusEarned.end(), "3,000 scrip, the Lightning Keel (Forged) for the opener, and the Owners' page");
    return fails;
}

int RunRedTideMapTest(const std::string& key) {
    std::string why;
    if (!DataOk(&why)) { printf("redtide-map-test: %s\n", why.c_str()); return 1; }
    int fails = 0;
    std::function<void(bool, const std::string&)> check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("Red Tide map test: %s\n", Map(key).title.c_str());
    if (key == "ship") ShipQuestTest(fails, check);
    else if (key == "cave") CaveTest(fails, check);
    else if (key == "reef") ReefTest(fails, check);
    else if (key == "atlantis") AtlantisTest(fails, check);
    else if (key == "void") VoidTest(fails, check);
    else { printf("  (no map test for '%s' yet)\n", key.c_str()); }
    printf(fails ? "redtide-map-test: %d check(s) failed\n" : "redtide-map-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}

} // namespace rt

namespace rt {
// depth.exe --redtide-profile-test: the arcade profile (tokens, ranks, unlocks, records, save and load) and the
// dossier's pages (a kill, 30 s of watching). The real profile file is set aside and put back.
int RunRedTideProfileTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("Red Tide profile test\n");
    std::string path = ProfilePath(), bak = path + ".testbak";
    bool had = FileExists(path.c_str());
    if (had) std::rename(path.c_str(), bak.c_str());
    LoadProfile();
    Profile& p = GetProfile();
    check(p.tokens == 0 && p.Rank() == 1 && p.PouchSlots() == 1 && p.charms.empty(), "a new profile: rank 1, no tokens, one pouch slot, no charms");
    MatchSummary s; s.map = "ship"; s.tide = 12; s.scrip = 9000; s.timeS = 1500; s.bossKilled = true; s.pages = {"Bilge Sprat", "Goliath Grouper"};
    int tok = 0; auto lines = AwardMatch(s, &tok);
    check(tok == 10 + 25 + 4 + 30, TextFormat("a tide-12 match with a first boss kill pays %d tokens (10 + 25 + 2 x 2 + 30)", tok));
    check(p.bestTide["ship"] == 12 && p.dossier.count("ship|Bilge Sprat"), "its record and its dossier pages are kept");
    tok = 0; AwardMatch(s, &tok);
    check(tok == 39, "the first-boss bonus pays once");
    check(p.Rank() == 1 && p.earned == 108, TextFormat("108 tokens earned: rank %d (rank 2 at 151)", p.Rank()));
    s.tide = 16; AwardMatch(s, &tok);
    check(p.Rank() == 2 && p.charms.count("brines") && p.charms.count("locker"), "rank 2 unlocks Keep Your Brines; tide 15 brings Lucky Locker");
    p.pouch = {"brines"}; p.suit = "verdigris"; SaveProfile();
    LoadProfile();
    Profile& q = GetProfile();
    check(q.tokens == p.tokens && q.pouch.size() == 1 && q.suit == "verdigris" && q.dossier.count("ship|Goliath Grouper") && q.bestTide["ship"] == 16, "saved and loaded intact");
    std::remove(path.c_str());
    if (had) std::rename(bak.c_str(), path.c_str());
    LoadProfile();
    // the dossier in a match
    auto M = std::make_unique<Match>();
    Match& m = *M;
    m.Init("ship", 1, 9, false);
    DiverState& d = m.divers[0]; d.invulnerable = true;
    int prey = -1;
    for (int i = 0; i < (int)m.eco.agents.size(); i++) { const Agent& a = m.eco.agents[i]; if (a.alive && a.diver < 0 && !m.map->species[a.sp].isEnemy && !m.IsBoss(i)) { prey = i; break; } }
    std::string n = m.map->species[m.eco.agents[prey].sp].name;
    m.HitAgentPublic(0, prey, 1e6f);
    check(m.dossierSeen.count(n), "a kill writes the beast's dossier page: " + n);
    int watch = -1;
    for (int i = 0; i < (int)m.eco.agents.size(); i++) { const Agent& a = m.eco.agents[i]; if (a.alive && a.diver < 0 && !m.map->species[a.sp].isEnemy && !m.IsBoss(i) && !m.dossierSeen.count(m.map->species[a.sp].name)) { watch = i; break; } }
    std::string wn = m.map->species[m.eco.agents[watch].sp].name;
    for (int k = 0; k < 20 * 31; k++) { m.eco.agents[watch].pos = Vector3Add(d.pos, {0, 0.2f, 3}); m.eco.agents[watch].zone = d.zone; m.Step(0.05f); m.phase = TidePhase::Calm; }
    check(m.dossierSeen.count(wn), "30 s of watching writes one too: " + wn);
    // Salt Charms
    {
        auto M2 = std::make_unique<Match>();
        Match& c = *M2;
        c.Init("ship", 2, 11, false);
        DiverState& a = c.divers[0]; DiverState& b = c.divers[1];
        a.invulnerable = false; b.invulnerable = true;
        a.pouch = {"shares", "shell", "luck", "clean", "circle", "ghost", "fins", "brines", "locker"};
        a.scrip = 1000; b.scrip = 0;
        check(c.UseCharm(0) && a.scrip == 500 && b.scrip == 500, "Fair Shares pools and splits the scrip");
        c.UseCharm(0); a.hp = a.hpMax = 1000; c.HitDiverPublic(a, 100, "test", "", a.pos, -1);
        check(fabsf(a.hp - 950) < 0.5f, "Hard Shell halves the damage");
        c.UseCharm(0);
        int fish = -1; for (int i = 0; i < (int)c.eco.agents.size(); i++) { const Agent& g = c.eco.agents[i]; if (g.alive && g.diver < 0 && !c.map->species[g.sp].isEnemy && !c.IsBoss(i)) { fish = i; break; } }
        int s0 = a.scrip; float bounty = c.map->species[c.eco.agents[fish].sp].bountyBase * c.map->Tide(c.tide).bountyMult;
        c.HitAgentPublic(0, fish, 1e6f);
        check(a.scrip - s0 >= (int)(bounty * 2) - 1 && a.luckKills == 9, "Fisher's Luck pays the next kills double");
        c.eco.AddBlood(a.pos, 200); c.eco.scent.BuildSat();
        c.UseCharm(0);
        check(c.eco.Smell(a.pos, a.zone, 6) < 1, "Clean Water clears the blood");
        c.UseCharm(0);
        int g = -1; for (int i = 0; i < (int)c.eco.agents.size(); i++) { const Agent& x = c.eco.agents[i]; if (x.alive && x.diver < 0 && !c.map->species[x.sp].isEnemy && !c.IsBoss(i) && c.map->species[x.sp].size >= 3 && c.map->species[x.sp].tier < 5) { g = i; break; } }
        c.eco.agents[g].pos = Vector3Add(a.circlePos, {1, 0, 0}); c.eco.agents[g].zone = a.zone; c.eco.agents[g].hp = c.eco.agents[g].hpMax = 1e5f;
        c.Step(0.05f);
        if (getenv("DEPTH_DBG")) printf("    circle: over %d (%s) %s dist %.2f circleT %.1f zone %d/%d st %s\n", (int)c.over, c.overReason.c_str(), c.map->species[c.eco.agents[g].sp].name.c_str(), Vector3Distance(c.eco.agents[g].pos, a.circlePos), a.circleT, c.eco.agents[g].zone, a.zone, StateName(c.eco.agents[g].st));
        check(Vector3Distance(c.eco.agents[g].pos, a.circlePos) >= 3.9f, "Salt Circle: no beast inside it");
        c.UseCharm(0);
        check(c.NearestDiverPublic(a.pos, 5) != 0, "Ghost Fin: nothing can find the diver");
        c.UseCharm(0); a.stamina = 0.2f; c.Step(0.05f);
        check(a.stamina > 0.99f, "Slick Fins: full sprint");
        c.UseCharm(0); a.tonics = {"juggernaut"}; a.downed = true; c.ApplyDropPublic(DropType::Resupply, a.pos);
        a.downed = false;
        check(a.keepBrines, "Keep Your Brines is armed until the next revive");
        c.UseCharm(0);
        check(a.luckyLocker && !c.UseCharm(0), "Lucky Locker armed; the pouch is spent");
    }
    {   // the ink bomb (bought at the workbench, thrown with G) and keys dropped where a carrier died
        auto K = std::make_unique<Match>(); Match& k = *K;
        k.Init("ship", 2, 5, false);
        DiverState& a = k.divers[0];
        int wb = -1; for (int i = 0; i < (int)k.level.stations.size(); i++) if (k.level.stations[i].type == StationType::Workbench) wb = i;
        check(wb >= 0, "the Ship has a workbench");
        if (wb >= 0) {
            a.pos = k.level.stations[wb].pos; a.scrip = 1000;
            int cost = 0; std::string pr = k.PromptFor(0, &cost);
            check(k.Interact(0, false, 0.01f) && a.inkBombs == 1 && a.scrip == 250 && cost == 750, TextFormat("an ink bomb from the workbench for 750 (prompt '%s')", pr.c_str()));
            a.scrip = 5000; k.Interact(0, false, 0.01f);
            check(a.inkBombs == 2 && !k.Interact(0, false, 0.01f), "two is all a diver carries");
        }
        k.CycleTactical(0); k.CycleTactical(0);
        check(a.tactical == 1, "Q picks the ink bomb");
        int limp = a.limpets;
        k.ThrowLimpet(0);
        check(a.inkBombs == 1 && a.limpets == limp && !k.darts.empty() && k.darts.back().kind == 11, "G throws the ink bomb, not a limpet");
        for (int i = 0; i < 40 && k.eco.inks.empty(); i++) k.Step(0.05f);
        check(!k.eco.inks.empty(), "it bursts into a cloud");
        // a hunter loses a diver in the ink; enemies and bosses can't find them in it
        k.eco.inks.clear(); k.eco.AddInk(a.pos, 5, 8);
        int hunter = -1;
        for (int i = 0; i < (int)k.eco.agents.size(); i++) { const Agent& g = k.eco.agents[i]; if (g.alive && g.diver < 0 && k.map->species[g.sp].size >= 3) { hunter = i; break; } }
        if (hunter >= 0) {
            Agent& g = k.eco.agents[hunter];
            g.pos = Vector3Add(a.pos, {3, 0, 0}); g.st = State::Hunt; g.target = a.agent; g.targetCorpse = false;
            k.Step(0.05f);
            check(k.eco.agents[hunter].target != a.agent && k.eco.agents[hunter].lostPrey == a.agent, "a hunting beast loses the diver in the ink");
        }
        check(k.NearestDiverPublic(a.pos, 10) != 0 && k.eco.Smell(a.pos, k.eco.ZoneAt(a.pos), 8) == 0, "in the cloud a diver can't be seen or smelled");
        for (int i = 0; i < 180; i++) k.Step(0.05f);
        check(k.eco.inks.empty(), "the cloud thins out after 8 s");
        // keys
        k.GiveKey("Goliath", 0, a.pos);
        check(k.keys.count("Goliath") && k.keyHolder["Goliath"] == 0, "the killer carries the key");
        a.invulnerable = false; a.downed = true; a.downT = 0.01f; Vector3 fell = a.pos;
        k.Step(0.05f);
        check(a.dead && !k.keys.count("Goliath") && k.floorKeys.size() == 1 && Vector3Distance(k.floorKeys[0].pos, fell) < 0.01f, "a carrier who bleeds out drops it where they fell");
        k.divers[1].pos = fell; k.divers[1].downed = false;
        k.Step(0.05f);
        check(k.keys.count("Goliath") && k.keyHolder["Goliath"] == 1 && k.floorKeys.empty(), "a teammate swims over it and carries it on");
    }
    {   // salvage builds, the chum bag, the flare and the cleaning brush
        for (const char* key : {"ship", "cave", "reef", "atlantis", "void"}) {
            auto Q = std::make_unique<Match>(); Match& q = *Q;
            q.Init(key, 1, 3, false);
            int ok = 0, bench = 0;
            std::vector<char> open(q.map->links.size(), 1);
            for (const auto& sp : q.salvage) if (sp.zone >= 0 && q.map->zones[sp.zone].diverOk && q.level.Inside(sp.pos, 0.4f, open)) ok++;
            for (const auto& st : q.level.stations) if (st.type == StationType::Workbench) bench++;
            check(q.salvage.size() == 15 && ok == 15 && bench >= 1, TextFormat("%s: 15 salvage parts lie where a diver can reach them (%d ok), %d workbench(es)", key, ok, bench));
        }
        auto K = std::make_unique<Match>(); Match& k = *K;
        k.Init("ship", 2, 9, false);
        DiverState& a = k.divers[0];
        a.invulnerable = false;
        for (auto& sp : k.salvage) if (sp.build == (int)BuildType::ShellShield) { a.pos = sp.pos; k.Step(0.05f); }
        int set = 7 << ((int)BuildType::ShellShield * 3);
        check((a.partsMask & set) == set, "swimming over the Turtle shell, the Strap and the Brass rim picks them up");
        int wb = -1; for (int i = 0; i < (int)k.level.stations.size(); i++) if (k.level.stations[i].type == StationType::Workbench) wb = i;
        a.pos = k.level.stations[wb].pos;
        std::string pr = k.PromptFor(0);
        check(k.Interact(0, false, 0.01f) && a.build == BuildType::ShellShield && a.shieldHP == 300 && (a.partsMask & set) == 0, TextFormat("the workbench builds the Shell Shield (prompt '%s')", pr.c_str()));
        bool respawning = true; for (const auto& sp : k.salvage) if (sp.build == (int)BuildType::ShellShield && !(sp.taken && sp.respawnT > 0)) respawning = false;
        check(respawning, "its parts turn up again elsewhere in 90 s");
        int beast = -1, mantis = -1;
        for (int i = 0; i < (int)k.eco.agents.size(); i++) { const Agent& g = k.eco.agents[i]; if (!g.alive || g.diver >= 0) continue; const Species& sp = k.map->species[g.sp]; if (sp.isEnemy) continue; if (HasW(sp.name, "mantis")) mantis = i; else if (beast < 0 && sp.size >= 3) beast = i; }
        float hp0 = a.hp;
        k.HitDiverPublic(a, 50, "a bite", "", a.pos, beast);
        check(a.hp == hp0 && a.shieldHP == 250, "the shield takes a bite (300 -> 250)");
        if (mantis >= 0) { k.HitDiverPublic(a, 30, "a punch", "", a.pos, mantis); k.HitDiverPublic(a, 30, "a punch", "", a.pos, mantis); }
        check(mantis >= 0 && a.build == BuildType::None && a.shieldHP == 0, "the mantis shrimp breaks it in two punches");
        a.build = BuildType::ShellShield; a.shieldHP = 300; a.bashCd = 0;
        { Agent& g = k.eco.agents[beast]; Vector3 f = k.Forward(a); f.y = 0; f = Vector3Normalize(f); g.pos = Vector3Add(a.pos, Vector3Scale(f, 1.5f)); g.zone = k.eco.ZoneAt(a.pos); g.stun = 0; }
        check(k.UseBuild(0) && k.eco.agents[beast].stun >= 1.4f && a.bashCd > 3, "B bashes with it: the beast in front is stunned and knocked back");
        // the Turbine
        int tonic = -1; for (int i = 0; i < (int)k.level.stations.size(); i++) if (k.level.stations[i].needsPower) tonic = i;
        k.power = false;
        a.pos = k.level.stations[tonic].pos; a.build = BuildType::Turbine;
        check(!k.Powered(k.level.stations[tonic]) && k.UseBuild(0) && k.Powered(k.level.stations[tonic]), "a Turbine set down powers the machine beside it before the power is on");
        for (auto& dp : k.deployed) dp.t = 0.01f;
        k.Step(0.05f);
        check(!k.Powered(k.level.stations[tonic]), "after 90 s it runs down");
        // the Net Tripwire: not a big beast, the first small one
        a.pos = k.level.stations[wb].pos; a.build = BuildType::NetTripwire; k.UseBuild(0);
        Vector3 net = k.deployed.back().pos;
        int big = -1, small = -1;
        for (int i = 0; i < (int)k.eco.agents.size(); i++) { const Agent& g = k.eco.agents[i]; if (!g.alive || g.diver >= 0 || k.IsBoss(i)) continue; const Species& sp = k.map->species[g.sp]; if (sp.isEnemy || sp.Sessile()) continue; if (sp.size >= 4 && big < 0) big = i; if (sp.size <= 3 && sp.size >= 2 && small < 0 && i != beast) small = i; }
        if (big >= 0) { k.eco.agents[big].pos = net; k.eco.agents[big].zone = k.eco.ZoneAt(net); }
        k.Step(0.05f);
        check(big >= 0 && k.deployed.back().held < 0, "a big beast swims through the net");
        if (big >= 0) k.eco.agents[big].pos = Vector3Add(net, {0, 0, 30});
        k.eco.agents[small].pos = net; k.eco.agents[small].zone = k.eco.ZoneAt(net);
        k.Step(0.05f);
        check(k.deployed.back().held == small && k.eco.agents[small].held > 5, "the first small one is held (the bell rings)");
        k.deployed.clear();
        // the Decoy Buoy
        a.build = BuildType::DecoyBuoy; k.UseBuild(0);
        Vector3 buoy = k.deployed.back().pos;
        Agent& g2 = k.eco.agents[beast]; g2.pos = k.map->zones[k.eco.ZoneAt(buoy)].Clamp(Vector3Add(buoy, {8, 0, 0}), 0.5f); g2.st = State::Graze; g2.stun = 0; g2.held = 0;
        k.Step(0.05f);
        check(g2.st == State::Investigate && Vector3Distance(g2.goal, buoy) < 0.5f, "the Decoy Buoy draws a beast to it");
        k.deployed.clear();
        // the Bubble Wall
        a.build = BuildType::BubbleWall; k.UseBuild(0);
        check(k.eco.curtains.size() == 1, "the Bubble Wall hangs a curtain across the water");
        const auto& c = k.eco.curtains.back();
        Vector3 mid = Vector3Lerp(c.a, c.b, 0.5f), n{-(c.b.z - c.a.z), 0, c.b.x - c.a.x}; n = Vector3Normalize(n);
        Vector3 p0 = Vector3Add(mid, Vector3Scale(n, -1)), p1 = Vector3Add(mid, Vector3Scale(n, 1)); p0.y = p1.y = a.pos.y;
        check(k.eco.CrossesCurtain(p0, p1, 2) && !k.eco.CrossesCurtain(p0, p1, 4), "small beasts can't cross it; big ones can");
        k.eco.curtains.clear(); k.deployed.clear();
        // the chum bag and the flare
        a.pos = k.level.stations[wb].pos; a.scrip = 5000; a.benchSel = 0;
        k.CycleBench(0);
        check(a.benchSel == 1 && k.Interact(0, false, 0.01f) && a.chumBags == 1 && a.scrip == 4500 && a.tactical == TAC_CHUM, "Z turns the workbench's stock: a chum bag for 500");
        float b0 = k.eco.BloodNear(a.pos, 8);
        k.ThrowLimpet(0);
        for (int i = 0; i < 40 && !k.darts.empty(); i++) k.Step(0.05f);
        for (int i = 0; i < 25; i++) k.Step(0.05f);   // (the scent grid's sums rebuild once a second)
        check(k.eco.BloodNear(a.pos, 12) > b0 + 20 && a.chumBags == 0, "a thrown chum bag fills the water with blood");
        a.flares = 1; a.tactical = TAC_FLARE; k.ThrowLimpet(0);
        for (int i = 0; i < 40 && k.flareLights.empty(); i++) k.Step(0.05f);
        check(k.flareLights.size() == 1, "a thrown flare burns where it lands");
        Vector3 fl = k.flareLights[0].pos; int zf = k.eco.ZoneAt(fl);
        int curious = -1, shy = -1;
        for (int i = 0; i < (int)k.eco.agents.size(); i++) { const Agent& g = k.eco.agents[i]; if (!g.alive || g.diver >= 0 || k.IsBoss(i)) continue; const Species& sp = k.map->species[g.sp]; if (sp.isEnemy || sp.Sessile()) continue; if (sp.Has("nocturnal") && shy < 0) shy = i; else if (sp.curiosity >= 0.5f && !sp.Has("camouflage") && curious < 0 && sp.size <= 3) curious = i; }
        for (int i : {curious, shy}) if (i >= 0) { Agent& g = k.eco.agents[i]; g.pos = k.map->zones[zf].Clamp(Vector3Add(fl, {5, 0, 0}), 0.5f); g.zone = zf; g.st = State::Graze; g.stun = 0; g.held = 0; g.target = -1; }
        k.Step(0.05f);
        check(curious >= 0 && k.eco.agents[curious].st == State::Investigate && Vector3Distance(k.eco.agents[curious].goal, fl) < 0.5f, "a curious fish comes to the flare's light");
        check(shy >= 0 && k.eco.agents[shy].st == State::Flee && Vector3Distance(k.eco.agents[shy].goal, fl) > Vector3Distance(k.eco.agents[shy].pos, fl), "a nocturnal one flees it");
        // the cleaning brush
        a.benchSel = 3; a.scrip = 5000;
        check(k.Interact(0, false, 0.01f) && a.brush && !k.Interact(0, false, 0.01f), "a cleaning brush for 1,000 (one is all you need)");
        int para = -1; for (int i = 0; i < (int)k.eco.agents.size(); i++) if (k.eco.agents[i].alive && k.eco.agents[i].diver < 0 && k.map->species[k.eco.agents[i].sp].Parasite()) { para = i; break; }
        DiverState& mate = k.divers[1];
        mate.pos = Vector3Add(a.pos, {1, 0, 0}); mate.dead = false; mate.downed = false;
        if (para >= 0) { k.eco.agents[para].host = mate.agent; mate.slowT = 5; mate.slowMult = 0.6f; }
        check(para >= 0 && k.UseBrush(0) && k.eco.agents[para].host < 0 && mate.slowT == 0, "X scrapes the parasite off a teammate");
        {   // the Cave's electric ray stops a Turbine
            auto C = std::make_unique<Match>(); Match& c = *C;
            c.Init("cave", 1, 4, false);
            DiverState& q = c.divers[0];
            q.build = BuildType::Turbine; c.UseBuild(0);
            int ray = -1; for (int i = 0; i < (int)c.eco.agents.size(); i++) if (c.eco.agents[i].alive && c.map->species[c.eco.agents[i].sp].Has("electric")) ray = i;
            if (ray >= 0) { c.eco.agents[ray].pos = Vector3Add(c.deployed.back().pos, {2, 0, 0}); c.eco.agents[ray].stun = 5; }
            c.Step(0.05f);
            check(ray >= 0 && !c.deployed.empty() && c.deployed.back().stopped, "the Cave's electric ray shocks a Turbine dead");
        }
    }
    {   // quips: barks.json's lines, one speaker at a time, no repeat within 3 minutes, answers from teammates
        auto Q = std::make_unique<Match>(); Match& q = *Q;
        q.Init("ship", 2, 11, false);
        for (int i = 0; i < 60; i++) q.Step(0.05f);
        bool start = false; for (const auto& c : q.captions) if (c.who == "Diver" || c.who == "Whaler") start = true;
        check(start && !q.quipOut.empty(), TextFormat("a match start quip is said (%d out)", (int)q.quipOut.size()));
        check(q.VoiceOf(0) == 0 && q.VoiceOf(1) == 1, "divers 1 and 2 speak as the Diver and the Whaler");
        q.quipOut.clear(); q.quipBusyT = 0; q.quipQueue.clear();
        bool a1 = q.Quip("Forge", 0); bool a2 = q.Quip("Tonic bought", 1);
        check(a1 && q.quipOut.size() == 1 && q.quipQueue.size() >= 1, "a second quip waits while someone is talking");
        for (int i = 0; i < 200; i++) q.Step(0.05f);
        check(a2 && q.quipOut.size() >= 2, "and is said once they've finished");
        std::set<std::string> said; int repeats = 0, n = 0;
        for (int k = 0; k < 12; k++) { q.quipBusyT = 0; q.quipQueue.clear(); q.quipSitAt.clear(); size_t before = q.captions.size(); if (q.Quip("Downed", 0)) { n++; std::string l = q.captions.back().text; if (!said.insert(l).second) repeats++; } (void)before; }
        check(n == 3 && repeats == 0, TextFormat("no line repeats within 3 minutes (%d Downed lines for the Diver, then silence)", n));
        q.divers[1].downed = true;
        q.quipBusyT = 0; q.quipQueue.clear(); q.quipOut.clear(); q.quipSitAt.clear();
        for (int i = 0; i < 4; i++) q.Step(0.05f);
        bool down = false, last = false; for (const auto& c : q.captions) { for (const Json& b : Engine().barks.a) if (b["line"].Str0() == c.text) { if (b["situation"].Str0() == "Downed") down = true; if (b["situation"].Str0() == "Last standing") last = true; } }
        for (int i = 0; i < 100; i++) q.Step(0.05f);
        for (const auto& c : q.captions) for (const Json& b : Engine().barks.a) if (b["line"].Str0() == c.text && b["situation"].Str0() == "Last standing") last = true;
        check(down && last, "a downed teammate calls it, and the last diver standing answers");
    }
    printf(fails ? "redtide-profile-test: %d check(s) failed\n" : "redtide-profile-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}
} // namespace rt
