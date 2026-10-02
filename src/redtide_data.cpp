// Red Tide data: loads data/redtide/*.json (exported from Red_Tide_Reference/*.xlsx by tools/export_redtide.py)
// into the records in redtide.h. Nothing here invents numbers: where a value is missing the loader falls back to
// the design doc's shared rules (size classes, Shared beast rules) and says so in a comment.
#include "redtide.h"
#include "raymath.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>

namespace rt {

static std::string Lower(std::string s) { for (char& c : s) c = (char)tolower((unsigned char)c); return s; }
static std::vector<std::string> SplitList(const std::string& s, char sep = ',') {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { size_t a = cur.find_first_not_of(' '), b = cur.find_last_not_of(' '); if (a != std::string::npos) out.push_back(cur.substr(a, b - a + 1)); cur.clear(); }
        else cur += c;
    }
    size_t a = cur.find_first_not_of(' '), b = cur.find_last_not_of(' ');
    if (a != std::string::npos) out.push_back(cur.substr(a, b - a + 1));
    return out;
}
// The first number in a string ("30 melee, 1.0 s" -> 30), or def.
static float FirstNum(const std::string& s, float def = 0) {
    for (size_t i = 0; i < s.size(); i++)
        if (isdigit((unsigned char)s[i]) || (s[i] == '.' && i + 1 < s.size() && isdigit((unsigned char)s[i + 1]))) return (float)atof(s.c_str() + i);
    return def;
}

bool Species::Has(const char* tag) const {
    for (const auto& t : tags) if (t == tag) return true;
    return false;
}

Vector3 Zone::Center() const {
    if (!parts.empty()) {
        // the visible part nearest the bounding box's middle (a ring's middle is outside it)
        Vector3 m{plan.x + plan.width / 2, (y0 + y1) / 2, plan.y + plan.height / 2};
        float bd = 1e18f; Vector3 best = m;
        for (const auto& p : parts) {
            if (p.hidden) continue;
            Vector3 c{p.r.x + p.r.width / 2, m.y, p.r.y + p.r.height / 2};
            float d = (c.x - m.x) * (c.x - m.x) + (c.z - m.z) * (c.z - m.z);
            if (d < bd) { bd = d; best = c; }
        }
        return best;
    }
    if (radial) {
        float a = (a0 + a1) * 0.5f * DEG2RAD, r = (rMin + rMax) * 0.5f;
        return {cosf(a) * r, (y0 + y1) / 2, sinf(a) * r};
    }
    return {plan.x + plan.width / 2, (y0 + y1) / 2, plan.y + plan.height / 2};
}
bool Zone::Contains(Vector3 p, float pad) const {
    if (p.y < y0 - pad || p.y > y1 + pad) return false;
    if (!parts.empty()) {
        for (const auto& q : parts) if (p.x >= q.r.x - pad && p.x <= q.r.x + q.r.width + pad && p.z >= q.r.y - pad && p.z <= q.r.y + q.r.height + pad) return true;
        return false;
    }
    if (radial) {
        float r = sqrtf(p.x * p.x + p.z * p.z);
        if (r < rMin - pad || r > rMax + pad) return false;
        float a = atan2f(p.z, p.x) * RAD2DEG;
        if (a < 0) a += 360;
        float lo = a0, hi = a1;
        if (hi - lo >= 359.9f) return true;
        if (lo < 0) { lo += 360; hi += 360; }
        return (a >= lo && a <= hi) || (a + 360 >= lo && a + 360 <= hi);
    }
    return p.x >= plan.x - pad && p.x <= plan.x + plan.width + pad && p.z >= plan.y - pad && p.z <= plan.y + plan.height + pad;
}
Vector3 Zone::Clamp(Vector3 p, float pad) const {
    Vector3 q = p;
    q.y = std::clamp(q.y, y0 + pad, y1 - pad);
    if (!parts.empty()) {
        // the nearest point in any of the parts
        float bd = 1e18f; Vector3 best = q;
        for (const auto& pt : parts) {
            float px = std::min(pad, pt.r.width / 2), pz = std::min(pad, pt.r.height / 2);
            Vector3 c{std::clamp(p.x, pt.r.x + px, pt.r.x + pt.r.width - px), q.y, std::clamp(p.z, pt.r.y + pz, pt.r.y + pt.r.height - pz)};
            float d = (c.x - p.x) * (c.x - p.x) + (c.z - p.z) * (c.z - p.z);
            if (d < bd) { bd = d; best = c; }
        }
        return best;
    }
    if (radial) {
        float r = std::clamp(sqrtf(p.x * p.x + p.z * p.z), rMin + pad, rMax - pad);
        float a = atan2f(p.z, p.x) * RAD2DEG;
        if (a < 0) a += 360;
        if (a1 - a0 < 359.9f) {
            float lo = a0 < 0 ? a0 + 360 : a0, hi = lo + (a1 - a0);
            float aa = a < lo ? a + 360 : a;
            if (aa > hi) aa = (aa - hi < lo + 360 - aa) ? hi : lo;
            a = aa;
        }
        q.x = cosf(a * DEG2RAD) * r;
        q.z = sinf(a * DEG2RAD) * r;
        return q;
    }
    q.x = std::clamp(q.x, plan.x + pad, plan.x + plan.width - pad);
    q.z = std::clamp(q.z, plan.y + pad, plan.y + plan.height - pad);
    return q;
}
static bool PartsOverlap(const Rectangle& a, const Rectangle& b) {
    return a.x < b.x + b.width - 0.01f && b.x < a.x + a.width - 0.01f && a.y < b.y + b.height - 0.01f && b.y < a.y + a.height - 0.01f;
}
void Zone::BuildPartGraph() {
    partAdj.assign(parts.size(), {});
    for (int i = 0; i < (int)parts.size(); i++) for (int j = i + 1; j < (int)parts.size(); j++)
        if (PartsOverlap(parts[i].r, parts[j].r)) { partAdj[i].push_back(j); partAdj[j].push_back(i); }
}
Vector3 Zone::Waypoint(Vector3 from, Vector3 to) const {
    if (parts.empty() || partAdj.size() != parts.size()) return to;
    auto in = [&](const Part& p, Vector3 v) { return v.x >= p.r.x && v.x <= p.r.x + p.r.width && v.z >= p.r.y && v.z <= p.r.y + p.r.height; };
    // breadth first from every part holding `from` to any part holding `to`
    std::vector<int> prev(parts.size(), -2), q;
    for (int i = 0; i < (int)parts.size(); i++) if (in(parts[i], from)) { prev[i] = -1; q.push_back(i); }
    if (q.empty()) return to;
    int goal = -1;
    for (size_t h = 0; h < q.size() && goal < 0; h++) {
        int c = q[h];
        if (in(parts[c], to)) { goal = c; break; }
        for (int n : partAdj[c]) if (prev[n] == -2) { prev[n] = c; q.push_back(n); }
    }
    if (goal < 0 || prev[goal] == -1) return to;             // unreachable, or already in the goal's part
    int next = goal;
    while (prev[prev[next]] != -1) next = prev[next];        // the second part on the path
    const Rectangle& a = parts[prev[next]].r; const Rectangle& b = parts[next].r;
    // the middle of the overlap: the seam into the next part
    float x0 = std::max(a.x, b.x), x1 = std::min(a.x + a.width, b.x + b.width), z0 = std::max(a.y, b.y), z1 = std::min(a.y + a.height, b.y + b.height);
    return {(x0 + x1) / 2, std::clamp(to.y, y0 + 0.5f, y1 - 0.5f), (z0 + z1) / 2};
}

int MapData::SpeciesIndex(const std::string& name) const {
    for (size_t i = 0; i < species.size(); i++) if (species[i].name == name) return (int)i;
    return -1;
}
int MapData::ZoneIndex(const std::string& name) const {
    if (name.empty()) return -1;
    for (size_t i = 0; i < zones.size(); i++) if (zones[i].name == name) return (int)i;
    auto al = zoneAlias.find(name);
    if (al != zoneAlias.end()) for (size_t i = 0; i < zones.size(); i++) if (zones[i].name == al->second) return (int)i;
    // The Species sheets name home zones by their short form ("Salon", "Keel", "Dry Chambers"): match a zone whose
    // name contains it (or whose first significant word it is).
    std::string n = Lower(name);
    for (size_t i = 0; i < zones.size(); i++) if (Lower(zones[i].name).find(n) != std::string::npos) return (int)i;
    for (size_t i = 0; i < zones.size(); i++) {
        std::string z = Lower(zones[i].name);
        if (z.rfind("the ", 0) == 0) z = z.substr(4);
        std::string first = n.substr(0, n.find(' '));
        if (!first.empty() && z.find(first) != std::string::npos) return (int)i;
    }
    return -1;
}
const TideRow& MapData::Tide(int t) const {
    static TideRow fallback;
    if (tides.empty()) return fallback;
    int i = std::clamp(t, 1, (int)tides.size()) - 1;
    return tides[i];
}

float EngineData::C(const char* name, float def) const {
    const Json& v = constants[name]["value"];
    if (v.IsArr()) return v[0].F(def);
    return v.F(def);
}
float EngineData::CBy(const char* name, int index, float def) const {
    const Json& v = constants[name]["value"];
    if (!v.IsArr()) return v.F(def);
    if (v.Size() == 0) return def;
    return v[(size_t)std::clamp(index, 0, (int)v.Size() - 1)].F(def);
}
float EngineData::M(const char* name, float def) const { return movement[name].F(def); }

static bool FileExists(const std::string& p) { std::ifstream f(p); return (bool)f; }

std::string DataDir() {
    static std::string dir;
    if (!dir.empty()) return dir;
    std::vector<std::string> cand = {"data/redtide", "./data/redtide"};
    std::string app = GetApplicationDirectory();
    for (int up = 0; up <= 3; up++) {
        std::string p = app;
        for (int k = 0; k < up; k++) p += "../";
        cand.push_back(p + "data/redtide");
    }
    for (const auto& c : cand)
        if (FileExists(c + "/engine/constants.json")) { dir = c; return dir; }
    dir = "data/redtide";
    return dir;
}

static EngineData* gEngine = nullptr;
const EngineData& Engine() {
    if (!gEngine) {
        gEngine = new EngineData;
        std::string d = DataDir();
        gEngine->constants = LoadJsonFile(d + "/engine/constants.json");
        gEngine->movement = LoadJsonFile(d + "/engine/movement.json");
        {   // hand tuning over the workbook's export (the player's playtest calls; tools/export_redtide.py never writes this file)
            Json tune = LoadJsonFile(d + "/engine/movement_tuning.json");
            for (auto& kv : tune.o) if (kv.first.rfind("_", 0) != 0) {
                bool found = false;
                for (auto& mv : gEngine->movement.o) if (mv.first == kv.first) { mv.second = kv.second; found = true; }
                if (!found) gEngine->movement.o.push_back(kv);
            }
        }
        gEngine->drops = LoadJsonFile(d + "/engine/drops.json");
        gEngine->progression = LoadJsonFile(d + "/engine/progression.json");
        gEngine->barks = LoadJsonFile(d + "/engine/barks.json");
        gEngine->bodyplans = LoadJsonFile(d + "/art/bodyplans.json");
        gEngine->palettes = LoadJsonFile(d + "/art/palettes.json");
    }
    return *gEngine;
}

bool DataOk(std::string* why) {
    const EngineData& e = Engine();
    if (e.constants.Size() < 10) { if (why) *why = "data/redtide/engine/constants.json missing (run tools/export_redtide.py)"; return false; }
    return true;
}

// Size-class fallbacks (design doc, Shared beast rules: "Size classes") for rows that leave a cell blank.
static const float SIZE_HP[7] = {20, 20, 60, 250, 900, 3000, 15000};
static const float SIZE_BOUNTY[7] = {40, 40, 80, 180, 400, 900, 1500};

static void LoadSpecies(MapData& m, const Json& arr) {
    for (const Json& r : arr.a) {
        Species s;
        s.id = r["id"].I();
        s.name = r["name"].Str0();
        if (s.name.empty()) continue;
        s.cls = r["class"].Str0();
        s.size = std::clamp(r["size"].I(1), 1, 6);
        s.tier = r["tier"].I(1);
        for (auto& t : SplitList(r["tags"].Str0())) s.tags.push_back(Lower(t));
        s.archetype = r["archetype"].Str0();
        s.social = Lower(r["social"].Str0());
        s.groupSize = std::max(1, r["group_size"].I(1));
        s.homeZone = r["home_zone"].Str0();
        s.homeFlora = r["home_flora"].Str0();
        s.defendR = r["defend_radius_m"].F();
        s.bloodThreshold = r["blood_threshold"].F();
        s.aggression = r["aggression"].F();
        s.fear = r["fear"].F();
        s.curiosity = r["curiosity"].F();
        s.sight = r["sight_m"].F(10);
        s.scent = r["scent_m"].F(10);
        s.hearing = r["hearing_m"].F(10);
        s.electro = r["electro_m"].F();
        s.weakPoint = r["weak_point"].Str0();
        // the workbook gives armour as a level (1, 2): "Armored fronts ... halve frontal damage" (design doc); a value
        // under 1 is read as the fraction it takes off
        s.armorFront = r["armor_front"].F();
        if (s.armorFront >= 1) s.armorFront = 0.5f;
        s.hpBase = r["hp_base"].F(SIZE_HP[s.size]);
        s.bountyBase = r["bounty_base"].F(SIZE_BOUNTY[s.size]);
        s.bloodDeath = r["blood_death"].F(s.size * 20.0f);
        s.bloodPerS = r["blood_per_s_wounded"].F(s.size * 2.0f);
        s.speed = r["speed_mps"].F(2);
        s.turnDeg = r["turn_deg_s"].F(180);
        s.dropPct = r["drop_pct"].F(1);
        s.special = r["special"].Str0();
        m.species.push_back(s);
    }
}

// Faction units from a map's Faction sheet (rows: title, description, header, units..., then tactics and barks).
static void LoadFactionSheet(MapData& m, const Json& rows) {
    if (!rows.IsArr() || rows.Size() < 4) return;
    Faction& f = m.faction;
    f.name = rows[0][0].Str0();
    size_t i = 0;
    while (i < rows.Size() && rows[i][0].Str0() != "unit") i++;
    for (i++; i < rows.Size(); i++) {
        const Json& r = rows[i];
        if (r.Size() < 5) break;
        FactionUnit u;
        u.unit = r[0].Str0();
        u.hp = r[1].F(120);
        u.weapon = r[2].Str0();
        std::string dmg = r[3].Str0();
        u.damage = FirstNum(dmg, 20);
        size_t comma = dmg.find(',');
        u.interval = comma != std::string::npos ? FirstNum(dmg.substr(comma), 2) : 2;
        if (u.interval <= 0 || u.interval > 10) u.interval = 2;
        u.range = dmg.find("melee") != std::string::npos ? 1.5f : 12;
        std::string spd = Lower(r[4].Str0());
        size_t par = spd.find('(');
        u.speed = par != std::string::npos ? FirstNum(spd.substr(par), 2) : (spd.find("fast") != std::string::npos ? 2.4f : spd.find("slow") != std::string::npos ? 1.4f : 2.0f);
        u.role = r[5].Str0();
        u.tell = r[6].Str0();
        std::string loot = r[7].Str0();
        u.loot = (int)FirstNum(loot.substr(loot.find_last_of(',') == std::string::npos ? 0 : loot.find_last_of(',')), 200);
        u.huntOnly = Lower(u.unit).find("hunt") != std::string::npos || u.hp >= 700;
        f.units.push_back(u);
    }
    for (const auto& u : f.units) if (!u.huntOnly) f.composition.push_back(u.unit);
}

static int gLoadSeason = 0;   // the season Map is building (MapSeason)
static void LoadExtra(MapData& m, const Json& ex) {
    if (!ex.IsObj()) return;

    for (const auto& kv : ex["zone_alias"].o) m.zoneAlias[kv.first] = kv.second.Str0();
    m.extra = ex;
    if (ex.Has("title")) m.title = ex["title"].Str0();
    // a radial blockout laid out as a box plan (Atlantis: the engine's rooms are boxes); the points scale with it
    const Json& po = ex["plan_override"];
    for (auto& z : m.zones) {
        const Json& b = po[z.name];
        if (!b.IsArr()) continue;
        z.radial = false;
        z.plan = {b[0].F(), b[1].F(), b[2].F(), b[3].F()};
    }
    // zones the blockout leaves in prose (Atlantis's cisterns under every district, the open sea beyond the wall)
    for (const Json& za : ex["zone_add"].a) {
        Zone zn; zn.name = za["name"].Str0(); zn.deck = za["deck"].Str0(); zn.notes = za["notes"].Str0();
        const Json& b = za["box"];
        zn.plan = {b[0].F(), b[1].F(), b[2].F(), b[3].F()};
        zn.y0 = za["y"][0].F(0); zn.y1 = za["y"][1].F(8);
        zn.diverOk = za["divers"].Bool0(true);
        m.zones.push_back(zn);
    }
    // zones built from many boxes (the Atlantis ring wall and the sea beyond it): a ring rasterised on a grid, each
    // row's cells merged into runs, never over another zone's box, and a hidden connector over every seam between two
    // runs that touch (so the diver and the beasts pass where the runs meet)
    for (const auto& kv : ex["zone_parts"].o) for (auto& z : m.zones) if (z.name == kv.first) z.plan = {0, 0, 0, 0};   // (their old boxes don't block)
    for (const auto& kv : ex["zone_parts"].o) {
        int zi = -1;
        for (int i = 0; i < (int)m.zones.size(); i++) if (m.zones[i].name == kv.first) zi = i;
        if (zi < 0) continue;
        Zone& z = m.zones[zi];
        const Json& r = kv.second["ring"];
        std::vector<Rectangle> runs;
        if (r.IsArr()) {
            float cx = r[0].F(), cz = r[1].F(), r0 = r[2].F(), r1 = r[3].F(), c = kv.second["cell"].F(4);
            float aLo = kv.second["angles"][0].F(0), aHi = kv.second["angles"][1].F(360);
            auto blocked = [&](float x0, float z0, float x1, float z1) {
                for (int o = 0; o < (int)m.zones.size(); o++) {
                    if (o == zi) continue;
                    const Zone& oz = m.zones[o];
                    if (oz.y1 <= z.y0 || oz.y0 >= z.y1) continue;   // (another level: the cisterns under the city)
                    auto hits = [&](const Rectangle& b) { return x0 < b.x + b.width - 0.01f && b.x < x1 - 0.01f && z0 < b.y + b.height - 0.01f && b.y < z1 - 0.01f; };
                    if (oz.parts.empty() ? hits(oz.plan) : std::any_of(oz.parts.begin(), oz.parts.end(), [&](const Zone::Part& p) { return hits(p.r); })) return true;
                }
                return false;
            };
            int n = (int)ceilf(r1 / c) + 1;
            for (int j = -n; j < n; j++) {
                float z0 = cz + j * c, zc = z0 + c / 2;
                float runX = 0; bool open = false;
                for (int i = -n; i <= n; i++) {
                    float x0 = cx + i * c, xc = x0 + c / 2;
                    float rr = sqrtf((xc - cx) * (xc - cx) + (zc - cz) * (zc - cz));
                    float ang = atan2f(zc - cz, xc - cx) * RAD2DEG; if (ang < 0) ang += 360;
                    bool on = i < n && rr >= r0 && rr < r1 && ((ang >= aLo && ang <= aHi) || (ang + 360 >= aLo && ang + 360 <= aHi)) && !blocked(x0, z0, x0 + c, z0 + c);
                    if (on && !open) { runX = x0; open = true; }
                    if (!on && open) { runs.push_back({runX, z0, x0 - runX, c}); open = false; }
                }
            }
        }
        for (const Json& b : kv.second["boxes"].a) runs.push_back({b[0].F(), b[1].F(), b[2].F(), b[3].F()});
        if (runs.empty()) continue;
        z.radial = false;
        z.parts.clear();
        for (const auto& rr : runs) z.parts.push_back(Zone::Part{rr, false});
        // connectors over seams: two runs touching along x or z with enough shared length
        float pad = kv.second["seam_m"].F(1.5f);
        for (size_t a = 0; a < runs.size(); a++) for (size_t b = 0; b < runs.size(); b++) {
            const Rectangle& A = runs[a]; const Rectangle& B = runs[b];
            if (fabsf(A.y + A.height - B.y) < 0.01f) {   // B just north of A
                float x0 = std::max(A.x, B.x), x1 = std::min(A.x + A.width, B.x + B.width);
                if (x1 - x0 >= 2) z.parts.push_back(Zone::Part{{x0, B.y - pad, x1 - x0, pad * 2}, true});
            }
            if (fabsf(A.x + A.width - B.x) < 0.01f) {    // B just east of A (two zones' runs never share a row, but boxes may)
                float z0 = std::max(A.y, B.y), z1 = std::min(A.y + A.height, B.y + B.height);
                if (z1 - z0 >= 2) z.parts.push_back(Zone::Part{{B.x - pad, z0, pad * 2, z1 - z0}, true});
            }
        }
        float x0 = 1e9f, z0 = 1e9f, x1 = -1e9f, z1 = -1e9f;
        for (const auto& p : z.parts) { x0 = std::min(x0, p.r.x); z0 = std::min(z0, p.r.y); x1 = std::max(x1, p.r.x + p.r.width); z1 = std::max(z1, p.r.y + p.r.height); }
        z.plan = {x0, z0, x1 - x0, z1 - z0};
        z.BuildPartGraph();
    }
    for (const Json& la : ex["link_add"].a) {
        Link ln; ln.from = m.ZoneIndex(la["from"].Str0()); ln.to = m.ZoneIndex(la["to"].Str0());
        if (ln.from < 0 || ln.to < 0) continue;
        ln.cost = la["cost"].I(0); ln.passage = la["passage"].Str0(); ln.oneWay = la["one_way"].Bool0(false);
        ln.diverOk = la["divers"].Bool0(true);
        std::string b = la["beasts"].Str0();
        if (b == "never") ln.beastRule = 1; else if (b == "breach") ln.beastRule = 2;
        ln.openTide = la["open_tide"].I(99);
        ln.flow = la["flow"].F(0);
        m.links.push_back(ln);
    }
    for (auto& l : m.links) {
        const Json& lp = ex["link_patch"][l.passage];
        if (!lp.IsObj()) continue;
        if (lp.Has("cost")) l.cost = lp["cost"].I(l.cost);
        if (lp.Has("one_way")) l.oneWay = lp["one_way"].Bool0(l.oneWay);
    }
    for (auto& l : m.links) {
        const Json& lp = ex["link_patch"][l.passage];
        if (!lp.IsObj() || !lp.Has("opens_with")) continue;
        for (int k = 0; k < (int)m.links.size(); k++) if (m.links[k].passage == lp["opens_with"].Str0()) l.opensWith = k;
    }
    // points of interest moved or added (a confined map moves what stood outside; quests add their steps)
    for (auto& p : m.pois) {
        const Json& pp = ex["poi_patch"][p.name];
        if (!pp.IsObj()) continue;
        if (pp.Has("zone")) { p.zoneName = pp["zone"].Str0(); }
        if (pp.Has("x")) p.pos.x = pp["x"].F();
        if (pp.Has("y")) p.pos.z = pp["y"].F();
        if (pp.Has("type")) p.type = pp["type"].Str0();
        if (pp.Has("step")) p.step = pp["step"].I(0);
    }
    for (const Json& pa : ex["poi_add"].a) {
        Poi po; po.name = pa["name"].Str0(); po.type = pa["type"].Str0(); po.zoneName = pa["zone"].Str0();
        po.pos = {pa["x"].F(), 0, pa["y"].F()};
        po.step = pa["step"].I(0);
        m.pois.push_back(po);
    }
    if (ex.Has("poi_scale")) { float s = ex["poi_scale"].F(1); for (auto& p : m.pois) { p.pos.x *= s; p.pos.z *= s; } }   // (the blockout's, patched and added alike)
    for (auto& p : m.pois) p.zone = m.ZoneIndex(p.zoneName);
    for (const auto& kv : ex["life_near"].o) {
        int zi = m.ZoneIndex(kv.first);
        if (zi < 0) continue;
        for (const auto& p : m.pois) if (p.name == kv.second["poi"].Str0()) { m.zones[zi].life = p.pos; m.zones[zi].lifeR = kv.second["radius_m"].F(80); }
    }
    for (const Json& sr : ex["spawn_remove"].a) m.spawns.erase(std::remove_if(m.spawns.begin(), m.spawns.end(), [&](const SpawnRow& r) { return r.species == sr.Str0(); }), m.spawns.end());   // (the Void's Sand Worm: scripted, not a beast in the web)
    for (const Json& sa : ex["spawn_add"].a) {
        SpawnRow r; r.zone = sa["zone"].Str0(); r.species = sa["species"].Str0(); r.count = sa["count"].I(); r.respawnS = sa["respawn_s"].F(60); r.capMult = 1.03f;
        if (r.count > 0) m.spawns.push_back(r);
    }
    // a season's species from another map (its row and its attacks), under a new name and home if it gives them
    for (const Json& si : ex["species_import"].a) {
        std::string from = si["from"].Str0(), name = si["species"].Str0();
        if (from.empty() || from == m.key) continue;
        int keep = gLoadSeason; gLoadSeason = 0; const MapData& src = Map(from); gLoadSeason = keep;
        int bi = src.SpeciesIndex(name);
        if (bi < 0) continue;
        Species n = src.species[bi];
        n.name = si["name"].Str0(name); n.homeZone = si["home_zone"].Str0(n.homeZone);
        for (const std::string& tg : SplitList(si["tags_add"].Str0())) n.tags.push_back(Lower(tg));
        n.id = (int)m.species.size();
        m.species.push_back(n);
        for (const auto& at : src.attacks) if (at.beast == name) { Attack c = at; c.beast = n.name; m.attacks.push_back(c); }
    }
    // species added (the Reef's pod orcas), cloned from a base row
    for (const Json& sa : ex["species_add"].a) {
        int bi = m.SpeciesIndex(sa["base"].Str0());
        if (bi < 0) continue;
        Species n = m.species[bi];
        n.name = sa["name"].Str0(); n.size = sa["size"].I(n.size); n.tier = sa["tier"].I(n.tier);
        n.hpBase = sa["hp"].F(n.hpBase); n.bountyBase = sa["bounty"].F(n.bountyBase);
        n.archetype = sa["archetype"].Str0(n.archetype); n.homeZone = sa["home_zone"].Str0(n.homeZone);
        n.tags.clear(); for (const std::string& t : SplitList(sa["tags"].Str0())) n.tags.push_back(Lower(t));
        n.art = sa["art"].Str0(); n.artScale = sa["art_scale"].F(1); n.attacksAs = sa["base"].Str0(); n.attackScale = sa["attack_scale"].F(0.6);
        n.id = (int)m.species.size();
        m.species.push_back(n);
    }
    // species rows reconciled with the design doc (tags, homes)
    for (auto& sp : m.species) {
        const Json& pp = ex["species_patch"][sp.name];
        if (!pp.IsObj()) continue;
        for (const std::string& t : SplitList(pp["tags_add"].Str0())) sp.tags.push_back(Lower(t));
        if (pp.Has("home_zone")) sp.homeZone = pp["home_zone"].Str0();
        if (pp.Has("bloodless")) sp.bloodless = pp["bloodless"].Bool0();
    }
    for (const Json& zn : ex["outside_zones"].a) { int zi = m.ZoneIndex(zn.Str0()); if (zi >= 0) m.zones[zi].diverOk = false; }
    for (auto& l : m.links) {
        for (int dir = 0; dir < 2; dir++) {
            std::string k = dir ? m.zones[l.to].name + ">" + m.zones[l.from].name : m.zones[l.from].name + ">" + m.zones[l.to].name;
            const Json& r = ex["link_rules"][k];
            if (!r.IsObj()) continue;
            if (r.Has("divers")) l.diverOk = r["divers"].Bool0(true);
            std::string b = r["beasts"].Str0();
            if (b == "never") l.beastRule = 1; else if (b == "breach") l.beastRule = 2;
            l.openTide = r["open_tide"].I(99);
        }
        if (!m.zones[l.from].diverOk || !m.zones[l.to].diverOk) l.diverOk = false;
        if (!l.diverOk && l.beastRule == 0) l.cost = 0;      // nothing to buy: divers never pass it
    }
    for (auto& z : m.zones) {
        if (ex["zone_height_m"].Has(z.deck)) z.y1 = z.y0 + ex["zone_height_m"][z.deck].F(8);
        if (ex["zone_floor_m"].Has(z.deck)) { float h = z.y1 - z.y0; z.y0 = ex["zone_floor_m"][z.deck].F(); z.y1 = z.y0 + h; }
        const Json& fl = ex["flow"][z.name];
        if (fl.IsArr()) z.flow = {fl[0].F(), fl[1].F(), fl[2].F()};
        const Json& zy = ex["zone_y"][z.name];
        if (zy.IsArr()) { z.y0 = zy[0].F(); z.y1 = zy[1].F(); }
    }
    for (const Json& zn : ex["air_zones"].a) { int zi = m.ZoneIndex(zn.Str0()); if (zi >= 0) m.zones[zi].air = true; }
    // slipstreams: one-way currents between halls (links with no passage to swim, only a ride)
    for (const Json& sj : ex["slipstreams"].a) {
        int a = m.ZoneIndex(sj["from"].Str0()), b = m.ZoneIndex(sj["to"].Str0());
        if (a < 0 || b < 0) continue;
        Link l; l.from = a; l.to = b; l.cost = 0; l.passage = "Slipstream " + sj["name"].Str0(); l.oneWay = true; l.slip = true;
        l.slipSpeed = sj["speed"].F(8); l.flow = sj["flow"].F(1.0f);
        m.links.push_back(l);
    }
    for (auto& l : m.links) {
        std::string k = m.zones[l.from].name + ">" + m.zones[l.to].name;
        if (ex["link_flow"].Has(k)) l.flow = ex["link_flow"][k].F();
        k = m.zones[l.to].name + ">" + m.zones[l.from].name;
        if (ex["link_flow"].Has(k)) l.flow = -ex["link_flow"][k].F();
    }
    for (const auto& kv : ex["alarm_regions"].o) {
        AlarmRegion r;
        r.name = kv.first;
        r.mult = kv.second["mult"].F(1);
        r.entry = kv.second["entry"].Str0();
        for (const Json& zn : kv.second["zones"].a) { int zi = m.ZoneIndex(zn.Str0()); if (zi >= 0) r.zones.push_back(zi); }
        m.alarmRegions.push_back(r);
    }
    const Json& f = ex["faction"];
    if (f.IsObj()) {
        Faction& fa = m.faction;
        fa = Faction{};
        fa.name = f["name"].Str0();
        fa.speciesName = f["species_name"].Str0();
        fa.entryPoi = f["entry_poi"].Str0();
        fa.huntStaging = f["hunt_staging"].Str0();
        for (const Json& p : f["patrol"].a) fa.patrol.push_back(p.Str0());
        for (const Json& p : f["squad_composition"].a) fa.composition.push_back(p.Str0());
        fa.fleeFromSize = f["flee_from_size"].I(4);
        fa.ignoreBelowSize = f["ignore_below_size"].I(3);
        for (const Json& u : f["units"].a) {
            FactionUnit fu;
            fu.unit = u["unit"].Str0(); fu.hp = u["hp"].F(120); fu.weapon = u["weapon"].Str0(); fu.damage = u["damage"].F(20);
            fu.interval = u["interval_s"].F(2); fu.range = u["range_m"].F(2); fu.speed = u["speed_mps"].F(2); fu.role = u["role"].Str0();
            fu.tell = u["tell"].Str0(); fu.loot = u["loot_scrip"].I(200); fu.drops = u["drops"].Str0(); fu.huntOnly = u["hunt_only"].Bool0();
            fa.units.push_back(fu);
        }
        for (const auto& kv : f["barks"].o) for (const Json& l : kv.second.a) fa.barks[kv.first].push_back(l.Str0());
    }
    // a faction sheet's prose, pinned down for the game: roles, patrol, staging, barks, what they do and don't do
    const Json& fp = ex["faction_patch"];
    if (fp.IsObj()) {
        Faction& fa = m.faction;
        if (fp.Has("species_name")) fa.speciesName = fp["species_name"].Str0();
        if (fp.Has("entry_poi")) fa.entryPoi = fp["entry_poi"].Str0();
        if (fp.Has("hunt_staging")) fa.huntStaging = fp["hunt_staging"].Str0();
        if (fp.Has("patrol")) { fa.patrol.clear(); for (const Json& p : fp["patrol"].a) fa.patrol.push_back(p.Str0()); }
        if (fp.Has("bleeds")) fa.bleeds = fp["bleeds"].Bool0(true);
        if (fp.Has("slipstreams")) fa.slipstreams = fp["slipstreams"].Bool0(true);
        if (fp.Has("ignore_beasts")) fa.ignoreBeasts = fp["ignore_beasts"].Bool0(false);
        if (fp.Has("hunts_beasts")) fa.huntsBeasts = fp["hunts_beasts"].Bool0(false);
        if (fp.Has("shoots_allies")) fa.shootsAllies = fp["shoots_allies"].Bool0(false);
        for (auto& u : fa.units) {
            const Json& up = fp["units"][u.unit];
            if (!up.IsObj()) continue;
            if (up.Has("role")) u.role = up["role"].Str0();
            if (up.Has("range")) u.range = up["range"].F(u.range);
            if (up.Has("damage")) u.damage = up["damage"].F(u.damage);
            if (up.Has("interval")) u.interval = up["interval"].F(u.interval);
            if (up.Has("speed")) u.speed = up["speed"].F(u.speed);
            if (up.Has("loot")) u.loot = up["loot"].I(u.loot);
            if (up.Has("drops")) u.drops = up["drops"].Str0();
            if (up.Has("hunt_only")) u.huntOnly = up["hunt_only"].Bool0();
            if (up.Has("bloodless")) u.bloodless = up["bloodless"].Bool0();
        }
        if (fp.Has("composition")) { fa.composition.clear(); for (const Json& p : fp["composition"].a) fa.composition.push_back(p.Str0()); }
        if (fp.Has("barks")) { fa.barks.clear(); for (const auto& kv : fp["barks"].o) for (const Json& l : kv.second.a) fa.barks[kv.first].push_back(l.Str0()); }
    }
}

static void LoadBlockout(MapData& m, const Json& bo) {
    for (const Json& z : bo["zones"].a) {
        Zone zn;
        zn.name = z.Has("zone") ? z["zone"].Str0() : z["district"].Str0();
        zn.deck = z.Has("deck") ? z["deck"].Str0() : z.Has("level") ? z["level"].Str0() : z["shape"].Str0();
        zn.notes = z["notes"].Str0();
        zn.doorCost = z["door_cost_scrip"].I();
        if (z.Has("r_min_m")) {
            zn.radial = true;
            zn.rMin = z["r_min_m"].F(); zn.rMax = z["r_max_m"].F(); zn.a0 = z["angle_min_deg"].F(); zn.a1 = z["angle_max_deg"].F(360);
            if (zn.a1 < zn.a0) zn.a1 += 360;
            float minx = 1e9f, maxx = -1e9f, minz = 1e9f, maxz = -1e9f;
            for (int k = 0; k <= 24; k++) {
                float a = (zn.a0 + (zn.a1 - zn.a0) * k / 24.0f) * DEG2RAD;
                for (float r : {zn.rMin, zn.rMax}) { minx = std::min(minx, cosf(a) * r); maxx = std::max(maxx, cosf(a) * r); minz = std::min(minz, sinf(a) * r); maxz = std::max(maxz, sinf(a) * r); }
            }
            zn.plan = {minx, minz, maxx - minx, maxz - minz};
        } else {
            float x = z.Has("x") ? z["x"].F() : z["x_plan"].F(), y = z.Has("y") ? z["y"].F() : z["y_plan"].F();
            zn.plan = {x, y, z["w"].F(10), z["h"].F(10)};
        }
        // depth_m (cave, reef, atlantis, void) is how deep the zone lies; the zone's own water column is its height.
        zn.y0 = 0;
        zn.y1 = std::clamp(std::min(zn.plan.width, zn.plan.height) * 0.6f, 6.0f, 24.0f);
        m.zones.push_back(zn);
    }
    for (const Json& l : bo["links"].a) {
        Link ln;
        ln.from = m.ZoneIndex(l["from"].Str0());
        ln.to = m.ZoneIndex(l["to"].Str0());
        if (ln.from < 0 || ln.to < 0) continue;
        ln.cost = l["cost_scrip"].I();
        ln.passage = l["passage"].Str0();
        ln.oneWay = Lower(ln.passage).find("one-way") != std::string::npos;
        m.links.push_back(ln);
    }
    for (const Json& p : bo["pois"].a) {
        Poi po;
        po.name = p["name"].Str0();
        po.type = p["type"].Str0();
        po.zoneName = p["zone"].Str0();
        po.zone = m.ZoneIndex(po.zoneName);
        po.pos = {p["x"].F(), 0, p["y"].F()};
        m.pois.push_back(po);
    }
    for (const Json& n : bo["notes"].a) m.notes.push_back(n.Str0());
}

// Where two zones meet: the closest pair of points between their plans (the passage's two mouths).
static void LinkMouths(const MapData& m, Link& l) {
    const Zone& A = m.zones[l.from];
    const Zone& B = m.zones[l.to];
    Vector3 ca = A.Center(), cb = B.Center();
    Vector3 a = A.Clamp(cb, 0.8f), b = B.Clamp(ca, 0.8f);
    if (!A.parts.empty() || !B.parts.empty()) {
        // (a zone of parts isn't convex: start each side from the other's real nearest point)
        if (!A.parts.empty()) { a = A.Clamp(cb, 0.8f); b = B.Clamp(a, 0.8f); }
        else { b = B.Clamp(ca, 0.8f); a = A.Clamp(b, 0.8f); }
    }
    // refine twice: each mouth is the closest point in its zone to the other mouth
    for (int k = 0; k < 3; k++) { a = A.Clamp(b, 0.8f); b = B.Clamp(a, 0.8f); }
    float y = std::max(A.y0, B.y0) + 1.0f;
    a.y = std::clamp(y, A.y0 + 0.5f, A.y1 - 0.5f);
    b.y = std::clamp(y, B.y0 + 0.5f, B.y1 - 0.5f);
    l.a = a; l.b = b;
}

static std::map<std::string, std::unique_ptr<MapData>> gMaps;

// ---------------------------------------------------------------- species seasons (design doc, "Species seasons")
// "A season is a data drop: species rows, diet matrix columns, spawn entries, and dossier pages, with models from the
// existing body plans": data/redtide/seasons/<n>.json carries, per map, a fragment of extra.json merged into the map's
// own (arrays appended, objects merged a level deep) when that season is played.
static void MergeJson(Json& into, const Json& add) {
    if (!add.IsObj()) return;
    if (!into.IsObj()) { into = add; return; }
    for (const auto& kv : add.o) {
        Json* t = nullptr;
        for (auto& p : into.o) if (p.first == kv.first) t = &p.second;
        if (!t) { into.o.push_back(kv); continue; }
        if (t->IsArr() && kv.second.IsArr()) t->a.insert(t->a.end(), kv.second.a.begin(), kv.second.a.end());
        else if (t->IsObj() && kv.second.IsObj()) for (const auto& sub : kv.second.o) {
            bool done = false;
            for (auto& p : t->o) if (p.first == sub.first) { p.second = sub.second; done = true; }
            if (!done) t->o.push_back(sub);
        }
        else *t = kv.second;
    }
}
static Json ExtraFor(const std::string& d, const std::string& key) {
    Json ex = FileExists(d + "/extra.json") ? LoadJsonFile(d + "/extra.json") : Json{};
    if (gLoadSeason > 0) {
        Json s = LoadJsonFile(DataDir() + "/seasons/" + std::to_string(gLoadSeason) + ".json");
        if (s["maps"][key].IsObj()) { if (!ex.IsObj()) { ex = Json{}; ex.type = Json::Obj; } MergeJson(ex, s["maps"][key]); }
    }
    return ex;
}
int SeasonCount() {
    int n = 0;
    while (FileExists(DataDir() + "/seasons/" + std::to_string(n + 1) + ".json")) n++;
    return n;
}
std::string SeasonName(int season) {
    if (season <= 0) return "No season";
    Json s = LoadJsonFile(DataDir() + "/seasons/" + std::to_string(season) + ".json");
    return s["name"].Str0("Season " + std::to_string(season));
}
static const MapData& MapLoad(const std::string& cacheKey, const std::string& key);
const MapData& Map(const std::string& key) { return MapLoad(key, key); }
const MapData& MapSeason(const std::string& key, int season) {
    if (season <= 0 || season > SeasonCount()) return Map(key);
    std::string ck = key + "#s" + std::to_string(season);
    auto it = gMaps.find(ck);
    if (it != gMaps.end()) return *it->second;
    Map(key);   // (the plain map first: a season's imports read other maps' rows)
    int keep = gLoadSeason; gLoadSeason = season;
    const MapData& m = MapLoad(ck, key);
    gLoadSeason = keep;
    return m;
}
static const MapData& MapLoad(const std::string& cacheKey, const std::string& key) {
    auto it = gMaps.find(cacheKey);
    if (it != gMaps.end()) return *it->second;
    auto md = std::make_unique<MapData>();
    MapData& m = *md;
    m.key = key;
    std::string d = DataDir() + "/maps/" + key;
    Json readme = LoadJsonFile(d + "/readme.json");
    for (const Json& r : readme.a) m.readme.push_back(r.Str0());
    if (!m.readme.empty()) { m.title = m.readme[0]; size_t p = m.title.find("The "); if (p != std::string::npos) { m.title = m.title.substr(p); size_t c = m.title.find(':'); if (c != std::string::npos) m.title = m.title.substr(0, c); } }
    LoadSpecies(m, LoadJsonFile(d + "/species.json"));
    for (const Json& a : LoadJsonFile(d + "/attacks.json").a) {
        Attack at;
        at.beast = a["beast"].Str0(); at.name = a["attack"].Str0(); at.damage = a["damage"].F(); at.windup = a["windup_s"].F();
        at.cooldown = a["cooldown_s"].F(); at.range = a["range_m"].F(1); at.tell = a["tell (what the player sees)"].Str0(); at.effect = a["effect"].Str0();
        if (!at.beast.empty()) m.attacks.push_back(at);
    }
    for (const Json& f : LoadJsonFile(d + "/flora.json").a) {
        Flora fl;
        fl.name = f["flora"].Str0(); fl.type = f["type"].Str0(); fl.effect = f["effect"].Str0();
        fl.zones = SplitList(f["zones"].Str0()); fl.grazedBy = SplitList(f["grazed_by"].Str0());
        fl.contactDamage = f["contact_damage"].F(); fl.regrowPerS = f["regrowth_rate_per_s"].F(); fl.regrowDelay = f["regrow_delay_s"].F();
        if (!fl.name.empty()) m.flora.push_back(fl);
    }
    for (const Json& s : LoadJsonFile(d + "/spawn.json").a) {
        SpawnRow r;
        r.zone = s["zone"].Str0(); r.species = s["species"].Str0(); r.count = s["count_tide1"].I(); r.respawnS = s["respawn_s"].F(); r.capMult = s["capacity_mult_per_tide"].F(1.03f);
        if (!r.species.empty() && r.count > 0) m.spawns.push_back(r);
    }
    Json tc = LoadJsonFile(d + "/tidecurve.json");
    for (const auto& kv : tc["tunables"].o) m.tunables[kv.first] = kv.second.Num0();
    for (const Json& t : tc["tides"].a) {
        TideRow r;
        r.tide = t["tide"].I(1); r.quota4p = t["kill_quota_4p"].I(12); r.hpMult = t["hp_mult"].F(1); r.bountyMult = t["bounty_mult"].F(1);
        r.dmgMult = t["beast_dmg_mult"].F(1); r.alarmThreshold = t["alarm_threshold"].F(90); r.enemySpawnChance = t["enemy_spawn_chance"].F();
        r.bloodDecay = t["blood_decay_per_s"].F(0.02f); r.hunt = t["hunt"].Str0(); r.apexWander = t["apex_wander"].Str0();
        r.bossMayAppear = t["boss_may_appear"].Str0(); r.tideBonus = t["tide_bonus_scrip"].I(100);
        m.tides.push_back(r);
    }
    LoadBlockout(m, LoadJsonFile(d + "/blockout.json"));
    for (const Json& b : LoadJsonFile(d + "/boss.json").a) m.boss.push_back(b.Str0());
    if (FileExists(d + "/faction.json")) LoadFactionSheet(m, LoadJsonFile(d + "/faction.json"));
    { Json ex = ExtraFor(d, key); if (ex.IsObj()) LoadExtra(m, ex); }
    // The faction joins the web as a species record ("Enemy factions are species in the same web").
    if (!m.faction.units.empty()) {
        Species e;
        e.name = m.faction.speciesName.empty() ? m.faction.name : m.faction.speciesName;
        e.cls = "enemy"; e.isEnemy = true; e.size = 3; e.tier = 3; e.archetype = "Squad"; e.social = "pack";
        e.hpBase = m.faction.units[0].hp; e.bountyBase = (float)m.faction.units[0].loot;
        e.bloodDeath = Engine().C("enemy_blood_yield", 20); e.bloodPerS = 6; e.speed = m.faction.units[0].speed;
        e.sight = 25; e.scent = 30; e.hearing = 40; e.aggression = 0.8f; e.fear = 0.3f; e.curiosity = 0.5f; e.bloodThreshold = 30;
        e.bloodless = !m.faction.bleeds;
        if (e.bloodless) e.bloodDeath = 0;
        m.enemySpecies = (int)m.species.size();
        m.species.push_back(e);
    }
    // The divers' own record: a size-3 body in the web (beasts that eat 'Divers' hunt it; territorial beasts see an intruder).
    {
        Species dv;
        dv.name = "Diver"; dv.cls = "diver"; dv.isDiver = true; dv.size = 3; dv.tier = 3; dv.archetype = "Player"; dv.social = "solitary";
        dv.hpBase = Engine().C("player_hp", 100); dv.bloodDeath = 0; dv.bloodPerS = Engine().C("blood_diver_wounded", 2);
        dv.speed = Engine().M("swim_speed", 2); dv.weakPoint = "none";
        m.species.push_back(dv);
    }
    // Diet rows by species index.
    Json diet = LoadJsonFile(d + "/diet.json");
    for (const Json& f : diet["foods"].a) m.foodNames.push_back(f.Str0());
    m.diet.assign(m.species.size(), DietRow{});
    for (const auto& kv : diet["rows"].o) {
        int pi = m.SpeciesIndex(kv.first);
        if (pi < 0) continue;
        DietRow& row = m.diet[pi];
        for (const auto& fw : kv.second.o) {
            float w = fw.second.F();
            std::string fn = fw.first;
            if (m.extra["food_alias"].Has(fn)) fn = m.extra["food_alias"][fn].Str0();   // the diet's short names for flora
            std::string lf = Lower(fn);
            int si = m.SpeciesIndex(fn);
            if (si >= 0) { row.prey.push_back({si, w}); continue; }
            if (lf == "corpse" || lf == "corpses") row.corpse += w;
            else if (lf == "plankton" || lf == "detritus") row.plankton += w;
            else if (lf == "parasites") row.parasites += w;
            else if (lf == "diver" || lf == "divers") row.enemy += w * 0.5f, row.prey.push_back({-2, w}); // -2: divers
            else if (m.enemySpecies >= 0 && (lf.find(Lower(m.species[m.enemySpecies].name)) != std::string::npos || Lower(m.species[m.enemySpecies].name).find(lf) != std::string::npos)) row.prey.push_back({m.enemySpecies, w}), row.enemy += w;
            else { row.flora += w; row.floraItems.push_back({fn, w}); }
        }
    }
    // Diet patches from extra.json (reconciling the workbook with the design doc), renormalised per row.
    {
        Json ex = ExtraFor(d, key);
        for (const auto& kv : ex["diet_patch"].o) {
            int pi = m.SpeciesIndex(kv.first);
            if (pi < 0) continue;
            DietRow& row = m.diet[pi];
            for (const auto& fw : kv.second.o) {
                int si = m.SpeciesIndex(fw.first);
                if (si >= 0) row.prey.push_back({si, fw.second.F()});
                else if (Lower(fw.first) == "corpse") row.corpse += fw.second.F();
                else if (Lower(fw.first) == "plankton") row.plankton += fw.second.F();   // (a season's grazer)
            }
            float sum = row.corpse + row.plankton + row.parasites + row.flora;
            for (const auto& pw : row.prey) sum += pw.second;
            if (sum > 0) {
                for (auto& pw : row.prey) pw.second /= sum;
                for (auto& fi : row.floraItems) fi.second /= sum;
                row.corpse /= sum; row.plankton /= sum; row.parasites /= sum; row.flora /= sum; row.enemy /= sum;
            }
        }
    }
    // Geometry: bounds, link mouths, alarm regions (one region per zone deck if the map doesn't define them).
    m.boundsMin = {1e9f, 1e9f, 1e9f}; m.boundsMax = {-1e9f, -1e9f, -1e9f};
    for (const auto& z : m.zones) {
        m.boundsMin.x = std::min(m.boundsMin.x, z.plan.x); m.boundsMax.x = std::max(m.boundsMax.x, z.plan.x + z.plan.width);
        m.boundsMin.z = std::min(m.boundsMin.z, z.plan.y); m.boundsMax.z = std::max(m.boundsMax.z, z.plan.y + z.plan.height);
        m.boundsMin.y = std::min(m.boundsMin.y, z.y0); m.boundsMax.y = std::max(m.boundsMax.y, z.y1);
    }
    for (auto& l : m.links) LinkMouths(m, l);
    // an added link that opens at a marked point (Atlantis's drain grates): its mouth there, the other end straight below
    for (const Json& la : m.extra["link_add"].a) {
        if (!la.Has("at")) continue;
        for (auto& l : m.links) {
            if (l.passage != la["passage"].Str0()) continue;
            for (const auto& p : m.pois) if (p.name == la["at"].Str0() && p.zone == l.to) {
                const Zone& B = m.zones[l.to]; const Zone& A = m.zones[l.from];
                l.b = B.Clamp({p.pos.x, B.y0 + 0.6f, p.pos.z}, 0.5f);
                l.a = A.Clamp({p.pos.x, A.y1 - 0.6f, p.pos.z}, 0.5f);
            }
        }
    }
    // a slipstream's mouth where the blockout marks one ("Slipstream C mouth")
    for (auto& l : m.links) {
        if (!l.slip) continue;
        std::string tag = Lower(l.passage) + " mouth";
        for (const auto& p : m.pois) if (Lower(p.name) == tag && p.zone == l.from) { const Zone& z = m.zones[l.from]; l.a = z.Clamp({p.pos.x, (z.y0 + z.y1) / 2, p.pos.z}, 1.0f); }
    }
    // portholes: wherever a room the divers use faces open water they don't, across a gap of a few metres
    const Json& wj = m.extra["windows"];
    if (wj.IsObj()) {
        float gapMax = wj["gap_max_m"].F(6), spacing = wj["spacing_m"].F(4), size = wj["size_m"].F(1.2);
        for (int i = 0; i < (int)m.zones.size(); i++) for (int j = 0; j < (int)m.zones.size(); j++) {
            const Zone& A = m.zones[i]; const Zone& B = m.zones[j];
            if (!A.diverOk || B.diverOk || A.radial || B.radial) continue;
            float y0 = std::max(A.y0, B.y0) + 0.5f, y1 = std::min(A.y1, B.y1) - 0.5f;
            if (y1 - y0 < size) continue;
            // every visible box of each (a zone of parts has many; a plain zone its plan)
            std::vector<Rectangle> ra, rb;
            if (A.parts.empty()) ra.push_back(A.plan); else for (const auto& p : A.parts) if (!p.hidden) ra.push_back(p.r);
            if (B.parts.empty()) rb.push_back(B.plan); else for (const auto& p : B.parts) if (!p.hidden) rb.push_back(p.r);
            for (const Rectangle& PA : ra) for (const Rectangle& PB : rb)
            for (int axis : {0, 2}) {
                float aLo = axis == 0 ? PA.x : PA.y, aHi = aLo + (axis == 0 ? PA.width : PA.height);
                float bLo = axis == 0 ? PB.x : PB.y, bHi = bLo + (axis == 0 ? PB.width : PB.height);
                float g0, g1;
                if (aHi <= bLo && bLo - aHi <= gapMax) { g0 = aHi; g1 = bLo; }
                else if (bHi <= aLo && aLo - bHi <= gapMax) { g0 = bHi; g1 = aLo; }
                else continue;
                // the other horizontal axis must overlap
                float cLo = std::max(axis == 0 ? PA.y : PA.x, axis == 0 ? PB.y : PB.x) + 1;
                float cHi = std::min(axis == 0 ? PA.y + PA.height : PA.x + PA.width, axis == 0 ? PB.y + PB.height : PB.x + PB.width) - 1;
                if (cHi - cLo < size) continue;
                float yc = (y0 + y1) / 2;
                for (float c = std::min(cLo + spacing * 0.5f, (cLo + cHi) / 2); c <= cHi - size * 0.5f + 0.01f; c += spacing) {   // (a short face still gets one, in its middle)
                    Window w; w.zone = i; w.outside = j; w.axis = axis; w.g0 = g0; w.g1 = g1; w.outHigh = bLo >= aHi - 0.01f;
                    float h = size / 2;
                    if (axis == 0) { w.lo = {g0 - 0.6f, yc - h, c - h}; w.hi = {g1 + 0.6f, yc + h, c + h}; }
                    else { w.lo = {c - h, yc - h, g0 - 0.6f}; w.hi = {c + h, yc + h, g1 + 0.6f}; }
                    m.windows.push_back(w);
                }
            }
        }
    }
    if (m.alarmRegions.empty()) {
        AlarmRegion r; r.name = "All"; r.mult = 1;
        for (int i = 0; i < (int)m.zones.size(); i++) r.zones.push_back(i);
        m.alarmRegions.push_back(r);
    }
    for (int ri = 0; ri < (int)m.alarmRegions.size(); ri++) for (int zi : m.alarmRegions[ri].zones) m.zones[zi].alarmRegion = ri;
    for (auto& p : m.pois) if (p.zone >= 0) { const Zone& z = m.zones[p.zone]; p.pos.y = z.y0 + 1; }
    gMaps[cacheKey] = std::move(md);
    return *gMaps[cacheKey];
}

} // namespace rt
