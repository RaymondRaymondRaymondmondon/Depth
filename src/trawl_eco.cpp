// The Trawl's food web (see trawl_eco.h). The population model, the chart, the three fields, the agents near the
// boat, bites and depredation, Wake; and --trawl-eco / --trawl-eco-test.
#include "trawl_eco.h"
#include "json.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>

namespace tw {

const char* ResName(int r) { static const char* N[R_COUNT] = {"plankton", "benthos", "algae", "seagrass", "carrion"}; return N[r]; }
const char* HabitatName(int h) { static const char* N[H_COUNT] = {"land", "open", "seagrass", "reef", "crest", "sea", "holes", "sargassum", "kelp", "barren"}; return N[h]; }

// ---------------------------------------------------------------- the species file
namespace {
bool FileThere(const std::string& p) { std::ifstream f(p); return (bool)f; }
std::string TrawlDataDir() {
    std::vector<std::string> cand = {"data/trawl", "./data/trawl"};
    std::string app = GetApplicationDirectory();
    for (int up = 0; up <= 3; up++) { std::string p = app; for (int k = 0; k < up; k++) p += "../"; cand.push_back(p + "data/trawl"); }
    for (const auto& c : cand) if (FileThere(c + "/trawl_species.json")) return c;
    return "data/trawl";
}
}  // namespace
std::string TrawlDataPath() { return TrawlDataDir(); }   // (the weapons catalogue reads its tables from here too)
namespace {
int BandFromName(const std::string& s) {
    if (s == "air") return BAND_AIR;
    if (s == "surface") return BAND_SURFACE;
    if (s == "upper") return BAND_UPPER;
    if (s == "mid") return BAND_MID;
    if (s == "deep") return BAND_DEEP;
    if (s == "abyss") return BAND_ABYSS;
    if (s == "floor") return BAND_FLOOR;
    return BAND_UPPER;
}
Pattern PatternFromName(const std::string& s) {
    for (int i = 0; i < (int)Pattern::COUNT; i++) if (s == PatternName((Pattern)i)) return (Pattern)i;
    return Pattern::None;
}
int ResFromName(const std::string& s) { for (int r = 0; r < R_COUNT; r++) if (s == ResName(r)) return r; return -1; }
int HabFromName(const std::string& s) { for (int h = 0; h < H_COUNT; h++) if (s == HabitatName(h)) return h; return -1; }

SpeciesDB* LoadSpecies() {
    auto* db = new SpeciesDB;
    Json j = LoadJsonFile(TrawlDataDir() + "/trawl_species.json");
    if (!j.IsObj()) { db->why = "data/trawl/trawl_species.json missing or unreadable"; return db; }
    const Json& S = j["species"];
    for (size_t i = 0; i < S.Size(); i++) {
        const Json& s = S[i];
        SpeciesRec r;
        r.name = s["name"].Str0(); r.cls = s["class"].Str0("fish");
        r.size = s["size"].I(1); r.tier = s["tier"].I(1);
        r.kgLo = s["kg"][0].F(1); r.kgHi = s["kg"][1].F(r.kgLo);
        r.band = BandFromName(s["band"].Str0("upper")); r.nightBand = BandFromName(s["night"].Str0(s["band"].Str0("upper")));
        for (const auto& kv : s["habitat"].o) { int h = HabFromName(kv.first); if (h >= 0) r.habitat[h] = kv.second.F(); }
        r.start = s["start"].F(); r.schoolLo = s["school"][0].I(1); r.schoolHi = s["school"][1].I(r.schoolLo);
        const Json& se = s["senses"];
        r.sight = se["sight"].F(10); r.scent = se["scent"].F(5); r.hearing = se["hearing"].F(5); r.lateral = se["lateral"].F(3);
        std::string lr = s["light"].Str0("neutral"); r.light = lr == "drawn" ? LR_DRAWN : lr == "shy" ? LR_SHY : LR_NEUTRAL;
        r.fear = s["fear"].F(0.5); r.aggression = s["aggression"].F(0); r.curiosity = s["curiosity"].F(0); r.speed = s["speed"].F(1);
        const Json& b = s["bite"];
        for (size_t k = 0; k < b["baits"].Size(); k++) r.baits.push_back(b["baits"][k].Str0());
        for (size_t k = 0; k < b["tackle"].Size(); k++) r.tackle.push_back(b["tackle"][k].Str0());
        r.nightBite = b["night"].Bool0(); r.needsWire = b["wire"].Bool0();
        r.fa = PatternFromName(s["fight"][0].Str0("None")); r.fb = PatternFromName(s["fight"][1].Str0("None"));
        r.price = s["price"].F(0);
        r.bait = s["bait"].Bool0(); r.netOnly = s["netOnly"].Bool0(); r.pots = s["pots"].Bool0(); r.reef = s["reef"].Bool0();
        r.grazesCoral = s["grazesCoral"].Bool0(); r.teeth = s["teeth"].Bool0(); r.inks = s["inks"].Bool0();
        r.threat = s["threat"].Bool0(); r.protectedSp = s["protected"].Bool0(); r.bycatchOnly = s["bycatchOnly"].Bool0();
        r.stings = s["stings"].Bool0(); r.isStatic = s["static"].Bool0(); r.stealsDeck = s["stealsDeck"].Bool0(); r.lifts = s["lifts"].F(3); r.ramsHull = s["ramsHull"].Bool0();
        std::string th = s["thief"].Str0(""); r.thief = th == "head" ? 1 : th == "whole" ? 2 : 0;
        r.bloodThreshold = s["bloodThreshold"].F(60);
        r.pullK = s["pullK"].F(1); r.staminaK = s["staminaK"].F(1); r.softMouth = s["softMouth"].F(1);
        db->sp.push_back(r);
    }
    // diets need every name first
    for (size_t i = 0; i < S.Size(); i++) {
        const Json& e = S[i]["eats"];
        float sum = 0;
        for (const auto& kv : e.o) sum += kv.second.F();
        for (const auto& kv : e.o) {
            int res = ResFromName(kv.first);
            int node = res >= 0 ? -1 - res : db->Find(kv.first);
            if (res < 0 && node < 0) { printf("trawl_species.json: %s eats unknown '%s'\n", db->sp[i].name.c_str(), kv.first.c_str()); continue; }
            db->sp[i].eats.push_back({node, kv.second.F() / std::max(1e-6f, sum)});
        }
    }
    for (const auto& kv : j["grounds"].o) {
        GroundDef g; g.key = kv.first;
        const Json& G = kv.second;
        g.name = G["name"].Str0(kv.first); g.size = G["size"].F(600); g.cell = G["cell"].F(4);
        g.depthMin = G["depthMin"].F(3); g.depthMax = G["depthMax"].F(40); g.bloodDecay = G["bloodDecay"].F(0.03);
        g.current = {G["current"][0].F(0.08), G["current"][1].F(0.03)};
        g.stirSafe = G["stir"]["safe"].F(120); g.stirCurve = G["stir"]["curve"].F(1.5); g.stirFloor = G["stir"]["floor"].F(0.1);
        for (int r = 0; r < R_COUNT; r++) { const Json& R = G["resources"][ResName(r)]; g.res[r].turnover = R["turnover"].F(12); }
        for (size_t k = 0; k < G["flora"].Size(); k++) g.flora.push_back(G["flora"][k].Str0());
        for (size_t k = 0; k < G["species"].Size(); k++) {
            int s = db->Find(G["species"][k].Str0());
            if (s >= 0) g.species.push_back(s); else printf("trawl_species.json: ground %s lists unknown '%s'\n", g.key.c_str(), G["species"][k].Str0().c_str());
        }
        db->grounds[g.key] = g;
    }
    db->ok = !db->sp.empty() && !db->grounds.empty();
    if (!db->ok) db->why = "no species or grounds in trawl_species.json";
    return db;
}
} // namespace

int SpeciesDB::Find(const std::string& name) const {
    for (int i = 0; i < (int)sp.size(); i++) if (sp[i].name == name) return i;
    return -1;
}
const SpeciesDB& Species() { static SpeciesDB* db = LoadSpecies(); return *db; }
bool SpeciesRec::Takes(Tackle t) const {
    for (const auto& s : tackle) if (s == TackleOf(t).name) return true;
    return false;
}
std::string DefaultBait(Tackle t) {
    switch (t) {
        case Tackle::Handline: return "tiny hook";
        case Tackle::Light: return "shrimp";
        case Tackle::Medium: return "squid strip";
        case Tackle::Heavy: return "live bait";
        case Tackle::DeepDrop: return "dead bait";
        case Tackle::Chair: return "live bait";
        default: return "shrimp";
    }
}

// ---------------------------------------------------------------- the fields
int Grid::BandOf(float d) { return d < 2 ? 0 : d < 15 ? 1 : d < 60 ? 2 : d < 200 ? 3 : 4; }
bool Grid::Cell(Vector3 p, int& x, int& y, int& b) const {
    x = (int)floorf(p.x / cell); y = (int)floorf(p.y / cell); b = BandOf(p.z);
    return x >= 0 && y >= 0 && x < n && y < n;
}
void Grid::Add(Vector3 p, float a) { int x, y, b; if (Cell(p, x, y, b)) v[Idx(x, y, b)] += a; }
float Grid::At(Vector3 p) const { int x, y, b; return Cell(p, x, y, b) ? v[Idx(x, y, b)] : 0; }
float Grid::Near(Vector3 p, int r) const {
    int x, y, b; Cell(p, x, y, b);
    float s = 0;
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
        int X = x + dx, Y = y + dy;
        if (X >= 0 && Y >= 0 && X < n && Y < n) s += v[Idx(X, Y, b)];
    }
    return s;
}
float Grid::Total() const { double s = 0; for (float f : v) s += f; return (float)s; }
void Grid::Step(float dt, float decay, float diffuse, Vector2 current) {
    float keep = powf(1 - decay, dt), k = std::min(0.9f, diffuse * dt);
    float sx = current.x * dt / cell, sy = current.y * dt / cell;
    for (int b = 0; b < nz; b++) for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) {
        // carried down-current (sampled from up-current), spread to the neighbours, a little up and down
        float fx = x - sx, fy = y - sy;
        int x0 = (int)floorf(fx), y0 = (int)floorf(fy); float tx = fx - x0, ty = fy - y0;
        auto S = [&](int X, int Y) { return (X >= 0 && Y >= 0 && X < n && Y < n) ? v[Idx(X, Y, b)] : 0.0f; };
        float adv = (S(x0, y0) * (1 - tx) + S(x0 + 1, y0) * tx) * (1 - ty) + (S(x0, y0 + 1) * (1 - tx) + S(x0 + 1, y0 + 1) * tx) * ty;
        float nb = (S(x - 1, y) + S(x + 1, y) + S(x, y - 1) + S(x, y + 1)) * 0.25f;
        float val = adv + k * (nb - adv);
        float up = b > 0 ? v[Idx(x, y, b - 1)] : val, dn = b + 1 < nz ? v[Idx(x, y, b + 1)] : val;
        val += 0.05f * dt * ((up + dn) * 0.5f - val);
        tmp[Idx(x, y, b)] = val * keep;
    }
    v.swap(tmp);
}

// ---------------------------------------------------------------- the ground
float Eco::Rand() { rng = rng * 1664525u + 1013904223u; return (rng >> 8) * (1.0f / 16777216.0f); }
int Eco::CellIdx(Vector2 p) const {
    int x = std::clamp((int)(p.x / cell), 0, n - 1), y = std::clamp((int)(p.y / cell), 0, n - 1);
    return y * n + x;
}
float Eco::DepthAt(Vector2 p) const {
    if (!InMap(p)) return g ? g->depthMax : 40;
    float d = depth[CellIdx(p)];
    return d <= 0 ? 0 : std::max(0.0f, d - tide);
}
int Eco::MarkAt(Vector2 p) const { for (int i = 0; i < (int)marks.size(); i++) if (Vector2Distance(p, marks[i].at) < marks[i].r) return i; return -1; }
int Eco::HabAt(Vector2 p) const { return InMap(p) ? hab[CellIdx(p)] : H_SEA; }

static float H2(int x, int y, uint32_t s) { uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + s * 2246822519u; h = (h ^ (h >> 13)) * 1274126177u; return ((h ^ (h >> 16)) & 0xFFFF) / 65535.0f; }
static float Noise2(float x, float y, uint32_t s) {
    int xi = (int)floorf(x), yi = (int)floorf(y); float tx = x - xi, ty = y - yi;
    tx = tx * tx * (3 - 2 * tx); ty = ty * ty * (3 - 2 * ty);
    float a = H2(xi, yi, s), b = H2(xi + 1, yi, s), c = H2(xi, yi + 1, s), d = H2(xi + 1, yi + 1, s);
    return (a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty;
}

// The Eclipse Lagoon (design doc: "a turquoise atoll lagoon under the Island's eclipse moon, with a coral crest
// between the lagoon and the open sea"): the atoll's island along the west edge, seagrass flats off it, a basin of
// 12-22 m studded with coral heads, the crest curving down the east side (3-6 m at high water, caves in its face),
// and the open sea beyond it shelving to 40 m.
// The Weeds (design doc v2, "The Weeds"): the same island along the west edge (the quay is where it always is), then a
// kelp forest whose golden canopy reaches the surface, cut by clear lanes; urchin barrens where it's been grazed away;
// rock reefs among it; the seaward edge beyond, shelving to 60 m, where the big fish run and the Great White patrols.
void Eco::BuildWeedsChart(uint32_t seed) {
    float size = n * cell;
    for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) {
        float wx = (x + 0.5f) * cell, wy = (y + 0.5f) * cell;
        int i = y * n + x;
        float shore = 34 + 14 * sinf(wy * 0.021f + seed) + 8 * Noise2(wy * 0.03f, 3.1f, seed);
        float edge = size * 0.70f + 18 * sinf(wy * 0.013f + seed * 0.3f);
        float nz = Noise2(wx * 0.04f, wy * 0.04f, seed + 5) - 0.5f;
        float d; int h;
        if (wx < shore) { d = 0; h = H_LAND; }
        else if (wx > edge) { d = std::min(g->depthMax, 22 + (wx - edge) * 0.35f + nz * 4); h = H_SEA; }
        else {
            d = std::clamp(6.0f + (wx - shore) * 0.07f + nz * 5, 5.0f, 30.0f);
            bool inForest = wx > shore + 70;   // (a clear apron off the island: the quay's way out)
            float lane = Noise2(wx * 0.018f, wy * 0.05f, seed + 13);   // the lanes run roughly north-south
            h = inForest && lane > 0.56f ? H_KELP : (d < 10 ? H_SEAGRASS : H_OPEN);   // (the lanes between run clear)
            if (h == H_KELP && Noise2(wx * 0.03f, wy * 0.03f, seed + 29) > 0.74f) h = H_BARREN;   // grazed bare
            if (Noise2(wx * 0.06f, wy * 0.06f, seed + 41) > 0.8f) { h = H_REEF; d = std::max(4.0f, d - 12); }   // (rock pinnacles: a net that crosses one snags)
        }
        depth[i] = d; hab[i] = (uint8_t)h;
        if (h == H_REEF && H2(x, y, seed + 21) < 0.3f) holes[i] = 1;
    }
    // the skiff water: the Inner Lanes (bass, sheephead; the Kelp King's boss water), the Otter Raft (abalone dives),
    // the Seaward Rocks (yellowtail; Gold Tail's boss water)
    marks.clear(); landingAt.clear();
    marks.push_back({"The Inner Lanes", {size * 0.45f, size * (0.35f + 0.3f * H2(1, 3, seed))}, 26, 0});
    marks.push_back({"The Otter Raft", {size * 0.55f, size * (0.2f + 0.6f * H2(4, 2, seed))}, 22, 2});
    marks.push_back({"The Seaward Rocks", {size * 0.74f, size * (0.3f + 0.4f * H2(6, 8, seed))}, 24, 0});
    // the landings (design doc v2, page 38): Seal Rock, a bare islet near the seaward edge with a sealers' hut; the
    // Cannery Pier, an old loading stage on pilings off the island, its cannery shed and boiler still standing
    {
        float my = marks[2].at.y;
        Vector2 seal{size * 0.63f, my + (my > size * 0.5f ? -1 : 1) * size * 0.22f};
        float py = size * (H2(9, 2, seed) < 0.5f ? 0.2f : 0.8f);
        float pshore = 34 + 14 * sinf(py * 0.021f + seed) + 8 * Noise2(py * 0.03f, 3.1f, seed);
        Vector2 pier{pshore + 42, py};
        landingAt = {seal, pier}; landingKind = {1, 2};
        for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) {
            Vector2 w{(x + 0.5f) * cell, (y + 0.5f) * cell};
            int i = y * n + x;
            float ds = Vector2Distance(w, seal), dp = Vector2Distance(w, pier);
            if (ds < 22) {   // its rock (11 m), a skirt of boulders, then the drop
                depth[i] = ds < 11 ? 0 : ds < 16 ? 1.5f + (ds - 11) * 0.3f : std::max(depth[i], 3.0f + (ds - 16) * 1.2f);
                hab[i] = (uint8_t)(ds < 11 ? H_LAND : ds < 16 ? H_REEF : H_OPEN);
            }
            if (dp < 10) { depth[i] = 0; hab[i] = H_LAND; }   // (the stage itself: planks on pilings over 6 m of water)
            else if (dp < 16 && hab[i] == H_KELP) hab[i] = H_OPEN;   // (a clear berth round it)
        }
    }
    rafts.clear();
    for (int i = 0; i < 7; i++) rafts.push_back({{size * 0.6f + Rand() * size * 0.35f, 40 + Rand() * (size - 80)}, 5 + Rand() * 6});   // drift kelp mats
}

void Eco::AddDriftMats(int k) {
    float size = n * cell;
    for (int i = 0; i < k; i++) rafts.push_back({{size * 0.45f + Rand() * size * 0.5f, 40 + Rand() * (size - 80)}, 5 + Rand() * 7});
    extraRafts += k;
}

void Eco::BuildChart(uint32_t seed) {
    depth.assign((size_t)n * n, 0); hab.assign((size_t)n * n, H_OPEN); holes.assign((size_t)n * n, 0);
    if (ground == "weeds") { BuildWeedsChart(seed); return; }
    float size = n * cell;
    struct Head { Vector2 c; float r; };
    std::vector<Head> heads;
    for (int i = 0; i < 28; i++) heads.push_back({{130 + Rand() * (size * 0.72f - 130), 30 + Rand() * (size - 60)}, 6 + Rand() * 9});
    // the Atoll (design doc v2, "Skiff destinations"): a palm islet out in the basin, a skiff's row from the island
    Vector2 atoll{size * 0.60f + (H2(3, 5, seed) - 0.5f) * size * 0.04f, size * (H2(7, 1, seed) < 0.5f ? 0.22f : 0.78f)};   // (hashed, not drawn from Rand: the rest of the chart is unchanged)
    landingAt.assign(1, atoll); landingKind.assign(1, 0);
    for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) {
        float wx = (x + 0.5f) * cell, wy = (y + 0.5f) * cell;
        int i = y * n + x;
        float da = Vector2Distance({wx, wy}, atoll);
        if (da < 30 && !getenv("DEPTH_NOATOLL")) {   // its sand (13 m), a shelf of 1-2 m, then its reef dropping to the basin
            depth[i] = da < 13 ? 0 : da < 19 ? 1.0f + (da - 13) * 0.15f : 2.0f + (da - 19) * 0.5f;
            hab[i] = (uint8_t)(da < 13 ? H_LAND : da < 19 ? H_SEAGRASS : H_REEF);
            continue;
        }
        float shore = 34 + 14 * sinf(wy * 0.021f + seed) + 8 * Noise2(wy * 0.03f, 3.1f, seed);
        float crest = size * 0.75f + 22 * sinf(wy * 0.011f + seed * 0.7f) + 8 * Noise2(wy * 0.02f, 7.7f, seed);
        float nz = Noise2(wx * 0.04f, wy * 0.04f, seed + 5) - 0.5f;
        float d; int h;
        if (wx < shore) { d = 0; h = H_LAND; }
        else if (fabsf(wx - crest) < 11) { d = 3.2f + 2.5f * Noise2(wx * 0.1f, wy * 0.1f, seed + 9); h = H_CREST; }
        else if (wx > crest) { d = std::min(g->depthMax, 9 + (wx - crest - 11) * 0.26f + nz * 3); h = H_SEA; }
        else {
            d = std::min(22.0f, 3.5f + (wx - shore) * 0.11f) + nz * 4;
            d = std::max(3.0f, d);
            h = (d < 10 && wx < size * 0.38f) ? H_SEAGRASS : H_OPEN;
            if (wx > crest - 26) h = H_REEF;   // the crest's lagoon-side apron
            for (const auto& hd : heads) {
                float dd = Vector2Distance({wx, wy}, hd.c);
                if (dd < hd.r * 1.3f) { h = H_REEF; if (dd < hd.r) d = std::min(d, 3.4f + (d - 3.4f) * (dd / hd.r)); }
            }
        }
        depth[i] = d; hab[i] = (uint8_t)h;
        if ((h == H_REEF || h == H_CREST) && H2(x, y, seed + 21) < 0.22f) holes[i] = 1;
    }
    // the skiff-only marks: the Crest Pass, a coral maze through the crest cut to 1.2 m (her keel wants 1.8), and the
    // Sargassum Line, a weed bank across the basin that fouls a turning screw
    marks.clear();
    {
        float y0 = size * (0.42f + 0.16f * H2(11, 4, seed));
        float cx = size * 0.75f + 22 * sinf(y0 * 0.011f + seed * 0.7f) + 8 * Noise2(y0 * 0.02f, 7.7f, seed);
        Vector2 pass{cx, y0};
        for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) {
            Vector2 w{(x + 0.5f) * cell, (y + 0.5f) * cell};
            float d = Vector2Distance(w, pass);
            if (d < 20 && depth[y * n + x] > 0) { depth[y * n + x] = std::min(depth[y * n + x], 1.2f + 0.3f * (d / 20)); hab[y * n + x] = H_CREST; holes[y * n + x] = 1; }
        }
        marks.push_back({"The Crest Pass", pass, 26, 0});
        marks.push_back({"The Sargassum Line", {size * 0.66f, size * (0.30f + 0.40f * H2(5, 9, seed))}, 24, 1});   // (out toward the crest: skiff water, not the basin she fishes)
    }
    // sargassum rafts drift on the wind across the lagoon and the sea
    rafts.clear();
    for (int i = 0; i < 9; i++) rafts.push_back({{80 + Rand() * (size - 120), 40 + Rand() * (size - 80)}, 5 + Rand() * 6});
}

// The population model is calibrated so the start is its balance: top down through the diet graph, each consumer
// eats q * B * f per game hour (f = its prey's abundance through a saturating response, 1/2 at the start), keeps e
// of it, and loses the rest of its balance half to a constant rate and half to crowding (which is what keeps the
// balance stable). A species eaten hard gets a faster turnover to match; each resource regrows logistically to a
// carrying capacity set so it just feeds what eats it.
void Eco::Calibrate() {
    const auto& S = Species().sp;
    int N = (int)S.size();
    std::vector<float> pred(N, 0), predR(R_COUNT, 0);
    std::vector<int> preds(N, 0);
    std::vector<char> inGround(N, 0);
    for (int s : g->species) inGround[s] = 1;
    auto consumer = [&](int j) { return inGround[j] && !S[j].isStatic && !S[j].eats.empty(); };
    for (int j = 0; j < N; j++) if (consumer(j)) for (const auto& e : S[j].eats) if (e.first >= 0 && inGround[e.first]) preds[e.first]++;
    std::vector<int> order, q;
    for (int j = 0; j < N; j++) if (inGround[j] && preds[j] == 0) q.push_back(j);
    while (!q.empty()) {
        int j = q.back(); q.pop_back(); order.push_back(j);
        if (!consumer(j)) continue;
        for (const auto& e : S[j].eats) if (e.first >= 0 && inGround[e.first] && --preds[e.first] == 0) q.push_back(e.first);
    }
    auto& SM = const_cast<std::vector<SpeciesRec>&>(S);
    for (int j : order) {
        SpeciesRec& r = SM[j];
        if (!consumer(j)) { r.q = 0; r.m = r.c = 0; continue; }
        float B0j = std::max(1e-3f, B0[j]);
        float lossPred = pred[j] / B0j;
        float qsize = 0.05f * powf(r.MeanKg(), -0.25f);
        r.q = std::max(qsize, lossPred / (0.6f * r.e * 0.5f));
        float rest = r.e * r.q * 0.5f - lossPred;
        r.c = 0.5f * rest; r.m = 0.5f * rest;
        float intake = r.q * B0j * 0.5f;
        for (const auto& e : r.eats) {
            if (e.first >= 0) { if (inGround[e.first]) pred[e.first] += intake * e.second; }
            else predR[-1 - e.first] += intake * e.second;
        }
    }
    if ((int)order.size() < (int)g->species.size()) printf("trawl eco: the diet graph has a loop; %d of %d species calibrated\n", (int)order.size(), (int)g->species.size());
    auto& GR = const_cast<GroundDef&>(*g);
    // each resource's standing stock is what its eaters get through in its turnover time; it regrows at 1.5x that
    // pace toward a capacity three times the stock (so grazed down it recovers, and left alone it piles up)
    for (int k = 0; k < R_COUNT; k++) {
        ResourceRec& rr = GR.res[k];
        float P = std::max(1.0f, predR[k]);
        R[k] = R0[k] = rr.start = P * rr.turnover;
        rr.r = 1.5f / rr.turnover;
        rr.K = R0[k] / (1 - P / (rr.r * R0[k]));
    }
}

void Eco::StepPop(float hours) {
    const auto& S = Species().sp;
    int N = (int)S.size();
    std::vector<float> dB(N, 0);
    float dR[R_COUNT] = {};
    auto A = [&](int node) { return node >= 0 ? (B0[node] > 0 ? B[node] / B0[node] : 0) : R[-1 - node] / std::max(1e-3f, R0[-1 - node]); };
    for (int j : g->species) {
        const SpeciesRec& r = S[j];
        if (r.isStatic || r.eats.empty()) { fed[j] = 1; continue; }
        float Sj = 0;
        for (const auto& e : r.eats) Sj += e.second * A(e.first);
        float f = Sj / (Sj + 1);
        fed[j] = f / 0.5f;
        float intake = r.q * B[j] * f;
        if (Sj > 1e-6f) for (const auto& e : r.eats) {
            float take = intake * e.second * A(e.first) / Sj;
            if (e.first >= 0) dB[e.first] -= take; else dR[-1 - e.first] -= take;
        }
        float m = r.m * (r.reef ? 1 + 1.5f * (1 - coral) : 1);
        dB[j] += r.e * intake - m * B[j] - r.c * B[j] * B[j] / std::max(1e-3f, B0[j]);
    }
    for (int k = 0; k < R_COUNT; k++) { const ResourceRec& rr = g->res[k]; dR[k] += rr.r * R[k] * (1 - R[k] / std::max(1e-3f, rr.K)); }
    for (int j : g->species) if (!S[j].isStatic) B[j] = std::max(0.0f, B[j] + dB[j] * hours);
    for (int k = 0; k < R_COUNT; k++) R[k] = std::max(0.0f, R[k] + dR[k] * hours);
    // algae left ungrazed smothers the coral heads; a clean reef slowly recovers
    float algae = R[R_ALGAE] / std::max(1e-3f, R0[R_ALGAE]);
    coral = std::clamp(coral + hours * (-0.05f * std::max(0.0f, algae - 1.2f) * coral + 0.01f * (1 - coral)), 0.0f, 1.0f);
}

float Eco::Hunger(int sp) const { return std::clamp(1 - 0.5f * (sp < (int)fed.size() ? fed[sp] : 1), 0.0f, 1.0f); }
// The Stir clock (design doc, "Night pacing"): the ground's baseline threat curve rises through the night from near
// nothing in the first minutes to full by 05:00, and the crew's Wake adds to it. Threats materialise near the boat, and
// grow hungry, in proportion.
float Eco::Stir() const {
    if (stirOverride >= 0) return stirOverride;
    if (!g) return 1;
    float base = std::clamp((clock - g->stirSafe) / std::max(1.0f, 540 - g->stirSafe), 0.0f, 1.0f);
    base = g->stirFloor + (1 - g->stirFloor) * powf(base, g->stirCurve);
    return std::clamp(base + wake / 100.0f, 0.0f, 1.0f);
}

bool Eco::Init(const std::string& key, uint32_t seed) {
    const SpeciesDB& db = Species();
    if (!db.ok) { printf("trawl eco: %s\n", db.why.c_str()); return false; }
    auto it = db.grounds.find(key);
    if (it == db.grounds.end()) { printf("trawl eco: no ground '%s'\n", key.c_str()); return false; }
    *this = Eco{};
    g = &it->second; ground = key; initSeed = seed;
    rng = seed * 2654435761u + 7; Rand();
    cell = g->cell; n = (int)(g->size / cell);
    BuildChart(seed);
    int N = (int)db.sp.size();
    B.assign(N, 0); B0.assign(N, 0); fed.assign(N, 1);
    for (int s : g->species) { B[s] = B0[s] = db.sp[s].start * db.sp[s].MeanKg(); }
    for (int k = 0; k < R_COUNT; k++) R[k] = R0[k] = 1;
    Calibrate();
    // where each species lives: its habitat weights over the chart (the floor-dwellers need a floor they can reach)
    suit.assign(N, {});
    for (int s : g->species) {
        const SpeciesRec& r = db.sp[s];
        auto& w = suit[s]; w.assign((size_t)n * n, 0);
        double sum = 0;
        for (int i = 0; i < n * n; i++) {
            if (hab[i] == H_LAND) continue;
            float v = r.habitat[hab[i]] + (holes[i] ? r.habitat[H_HOLES] : 0);
            if (r.band == BAND_FLOOR && depth[i] > 45) v = 0;
            w[i] = v; sum += v;
        }
        if (sum > 0) for (auto& v : w) v = (float)(v / sum);
    }
    blood.Init(n, cell); sound.Init(n, cell); vib.Init(n, cell);
    observer = {n * cell * 0.4f, n * cell * 0.5f};
    StartNight();
    return true;
}

void Eco::StartNight() {
    time = 0; clock = 0; tide = 0; agents.clear(); arrivals.clear(); gullT = -1;
    for (auto& f : blood.v) f = 0;
    for (auto& f : sound.v) f = 0;
    for (auto& f : vib.v) f = 0;
}
void Eco::Day(float hours) {
    for (float h = 0; h < hours; h += 1 / 60.0f) StepPop(1 / 60.0f);
    wake = std::max(0.0f, wake - hours * 60);   // a quiet day lets the water settle
    night++;
}

// ---------------------------------------------------------------- the layers
float Eco::LightAt(Vector3 p) const {
    float L = 0;
    for (const auto& l : lamps) {
        float d = Vector2Distance({p.x, p.y}, {l.p.x, l.p.y});
        if (d < l.r) L += l.k * (1 - d / l.r) * (1 - d / l.r);
    }
    return L * expf(-std::max(0.0f, p.z) / 8.0f);    // water drinks the light
}
// how bright the boat's glow looks from here: what draws the forage in from beyond the lit pool
static float Glow(const Eco& e, Vector3 p, Vector2* dir) {
    float G = 0; Vector2 best{0, 0};
    for (const auto& l : e.lamps) {
        float d = Vector2Distance({p.x, p.y}, {l.p.x, l.p.y});
        if (d > l.r * 6) continue;
        float gl = l.k * expf(-d / (l.r * 2.2f)) * expf(-std::max(0.0f, p.z) / 20.0f);
        if (gl > G) { G = gl; best = Vector2Subtract({l.p.x, l.p.y}, {p.x, p.y}); }
    }
    if (dir) *dir = Vector2Length(best) > 0.01f ? Vector2Normalize(best) : Vector2{0, 0};
    return G;
}
void Eco::AddBlood(Vector3 p, float a) { blood.Add(p, a); }
void Eco::AddNoise(Vector3 p, float a) { sound.Add(p, a); }
void Eco::AddVibration(Vector3 p, float a) { vib.Add(p, a); }

void Eco::Harvest(int sp, float kg, Vector3 at, bool bleed) {
    if (sp < 0 || sp >= (int)B.size()) return;
    B[sp] = std::max(0.0f, B[sp] - kg);
    if (bleed) AddBlood(at, kg * 2);
}
void Eco::DepthCharge(Vector3 p, std::vector<std::pair<int, float>>* floated) {
    // design doc: "12 m blast radius. Stuns everything in range, floating fish to the surface"
    const auto& S = Species().sp;
    const float RAD = 12;
    wake = std::min(100.0f, wake + 15);
    AddNoise(p, 400); AddVibration(p, 400); AddBlood(p, 80);
    auto addF = [&](int s, float kg) {
        if (!floated || kg <= 0) return;
        for (auto& f : *floated) if (f.first == s) { f.second += kg; return; }
        floated->push_back({s, kg});
    };
    for (auto& a : agents) if (a.alive && Vector2Distance({a.p.x, a.p.y}, {p.x, p.y}) < RAD && S[a.sp].band != BAND_AIR) {
        float kg = a.count * S[a.sp].MeanKg();
        Harvest(a.sp, kg, a.p, true);
        addF(a.sp, kg);
        a.count = 0; a.alive = false;
    }
    // and the fish in the blast that no one saw (the population's share of the 12 m around it) float up too
    int ci = CellIdx({p.x, p.y}); int cx = ci % n, cy = ci / n, r = (int)(RAD / cell);
    for (int s : g->species) {
        if (S[s].band == BAND_AIR) continue;
        double share = 0;
        for (int y = cy - r; y <= cy + r; y++) for (int x = cx - r; x <= cx + r; x++)
            if (x >= 0 && y >= 0 && x < n && y < n && (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r) share += suit[s][y * n + x];
        float kg = B[s] * (float)share * 0.8f;
        Harvest(s, kg, p, false);
        addF(s, kg * 0.5f);                     // (half of it floats where the boat can reach it; the rest sinks for the scavengers)
        R[R_CARRION] += kg * 0.5f;
    }
    log.push_back(TextFormat("minute %.0f: a depth charge", clock));
}

float Eco::SpeciesHP(int sp) const {
    const SpeciesRec& r = Species().sp[sp];
    if (r.name == "reef shark") return 80;       // design doc, "Threat stats"
    if (r.name == "barracuda") return 30;
    if (r.name == "gull flock") return 1;
    return 4 + r.MeanKg() * 5;
}
int Eco::HitAgent(Vector3 p, float r, bool air) const {
    const auto& S = Species().sp;
    int best = -1; float bd = 1e9f;
    for (int i = 0; i < (int)agents.size(); i++) {
        const EcoAgent& a = agents[i];
        if (!a.alive) continue;
        bool isAir = S[a.sp].band == BAND_AIR;
        if (isAir != air) continue;
        float body = air ? 3.0f : 0.25f + S[a.sp].size * 0.3f + (a.count > 3 ? std::min(3.0f, sqrtf((float)a.count) * 0.12f) : 0);
        float d = air ? Vector2Distance({a.p.x, a.p.y}, {p.x, p.y}) : Vector3Distance(a.p, p);
        if (d < body + r && d < bd) { bd = d; best = i; }
    }
    return best;
}
bool Eco::DamageAgent(int idx, float dmg, bool head, Vector3 at) {
    if (idx < 0 || idx >= (int)agents.size()) return false;
    EcoAgent& a = agents[idx];
    const SpeciesRec& r = Species().sp[a.sp];
    float hp = SpeciesHP(a.sp);
    a.hurt += dmg * (head ? 2 : 1);
    a.flash = 0.6f; a.fedT = std::max(a.fedT, 8.0f);   // it bolts
    if (a.hurt < hp) { AddBlood(at, r.size * std::min(1.0f, a.hurt / hp) * 2); return false; }
    // one of them is dead: off the population, and it bleeds (size x 20, the doc's death blood)
    a.hurt = 0; a.count--;
    if (a.count <= 0) a.alive = false;
    Harvest(a.sp, r.MeanKg(), at, false);
    if (r.band != BAND_AIR) AddBlood(at, r.size * 20.0f);
    return true;
}
float Eco::Sweep(Vector3 m, Vector2 dir, float width, float speed, float dt, std::vector<std::pair<int, float>>& out, float yield) {
    // catch rate = school density x mouth width x speed (design doc, "The trawl"): a group whose centre lies in the
    // mouth's path this step loses the share of it the mouth swept through
    const auto& S = Species().sp;
    float total = 0;
    Vector2 side{-dir.y, dir.x};
    for (auto& a : agents) {
        if (!a.alive || S[a.sp].band == BAND_AIR) continue;
        Vector2 rel{a.p.x - m.x, a.p.y - m.y};
        float along = Vector2DotProduct(rel, dir), across = Vector2DotProduct(rel, side);
        float spread = a.count > 3 ? 1.5f + sqrtf((float)a.count) * 0.12f : 1.0f;
        if (fabsf(across) > width / 2 + spread || along < -spread - speed * dt || along > spread || fabsf(a.p.z - m.z) > 4 + spread) continue;
        // the share of the school the mouth passes through this step: its length traversed, times how much of its
        // breadth the mouth spans
        float frac = std::clamp(speed * dt / (2 * spread), 0.0f, 1.0f) * std::min(1.0f, width / (2 * spread));
        float want = a.count * frac * yield;   // (the rest of the school slips round the wings)
        int take = (int)want + (Rand() < want - (int)want ? 1 : 0);
        if (take <= 0) continue;
        take = std::min(take, a.count);
        float kg = take * S[a.sp].MeanKg();
        a.count -= take; if (a.count <= 0) a.alive = false;
        Harvest(a.sp, kg, a.p, false);
        bool found = false; for (auto& o : out) if (o.first == a.sp) { o.second += kg; found = true; }
        if (!found) out.push_back({a.sp, kg});
        total += kg;
    }
    return total;
}
float Eco::DensityAt(int sp, Vector2 p) const {
    if (sp < 0 || sp >= (int)suit.size() || suit[sp].empty() || !InMap(p)) return 0;
    return Pop(sp) * suit[sp][CellIdx(p)];
}

// ---------------------------------------------------------------- the agents near the boat
namespace {
float BandDepth(int band, float floorD) {
    switch (band) {
        case BAND_AIR: return -3;
        case BAND_SURFACE: return 1;
        case BAND_UPPER: return std::min(7.0f, floorD * 0.5f);
        case BAND_MID: return std::min(25.0f, floorD - 2);
        case BAND_DEEP: return std::min(100.0f, floorD - 2);
        case BAND_ABYSS: return std::min(250.0f, floorD - 2);
        default: return floorD - 0.4f;   // the floor
    }
}
}

int Eco::SpawnAgent(int sp, Vector2 at) {
    const SpeciesRec& r = Species().sp[sp];
    EcoAgent a; a.sp = sp;
    a.count = std::max(1, (int)(r.schoolLo + Rand() * (r.schoolHi - r.schoolLo + 1)));
    float fd = DepthAt(at);
    a.p = {at.x, at.y, std::clamp(BandDepth(r.band, fd), r.band == BAND_AIR ? -3.0f : 0.3f, std::max(0.4f, fd - 0.3f))};
    float ang = Rand() * 6.2832f; a.wander = {cosf(ang), sinf(ang)};
    a.hunger = std::clamp(Hunger(sp) + (Rand() - 0.5f) * 0.3f, 0.0f, 1.0f);
    a.t = Rand() * 10;
    agents.push_back(a);
    return (int)agents.size() - 1;
}

void Eco::Materialize() {
    const auto& S = Species().sp;
    const float Rd = 150;
    Vector2 o = boat ? boatPos : observer;
    // the web lives round the Gannet, and round her skiff when it's out on its own (a second bubble)
    std::vector<Vector2> ctr{o};
    if (boat && skiffOn) ctr.push_back(skiffPos);
    auto nearC = [&](Vector2 p) { float d = 1e9f; for (Vector2 c : ctr) d = std::min(d, Vector2Distance(p, c)); return d; };
    agents.erase(std::remove_if(agents.begin(), agents.end(), [&](const EcoAgent& a) {
        return !a.alive || a.count <= 0 || nearC({a.p.x, a.p.y}) > Rd + 20; }), agents.end());
    int r = (int)(Rd / cell);
    std::vector<float> frac(S.size(), 0);
    std::vector<int> cells;
    std::vector<char> inSet((size_t)n * n, 0);
    for (Vector2 cc : ctr) {
        int ci = CellIdx(cc), cx = ci % n, cy = ci / n;
        for (int y = cy - r; y <= cy + r; y++) for (int x = cx - r; x <= cx + r; x++)
            if (x >= 0 && y >= 0 && x < n && y < n && (x - cx) * (x - cx) + (y - cy) * (y - cy) <= r * r && !inSet[y * n + x]) { inSet[y * n + x] = 1; cells.push_back(y * n + x); }
    }
    std::vector<int> have(S.size(), 0);
    for (const auto& a : agents) have[a.sp]++;
    for (int s : g->species) {
        if (S[s].band == BAND_AIR) continue;   // gulls come to the boat (below)
        double f = 0; for (int c : cells) f += suit[s][c];
        float groups = Pop(s) * (float)f / S[s].MeanSchool();
        float want = std::min(groups, 14.0f);    // (past 14 groups a species' agents stand for bigger schools)
        // threats keep to the Stir clock: few about the boat early, the ground's full share by the small hours
        bool threat = S[s].threat && !S[s].isStatic;
        if (threat) want *= S[s].size >= 5 ? std::max(0.0f, Stir() - g->stirFloor) / (1 - g->stirFloor) : Stir();   // the big ones get no floor: nothing large early
        // tonight's variant: more forage (or dead forage), open-sea fish inside, turtles and the sharks after them
        if (S[s].bait) want *= forageMul;
        if (S[s].habitat[H_SEA] > 0.5f && S[s].habitat[H_OPEN] < 0.5f) want *= seaMul;
        if (S[s].protectedSp) want *= turtleMul;
        if (S[s].ramsHull) want *= sharkMul;
        if (!speciesMul.empty()) { auto it = speciesMul.find(S[s].name); if (it != speciesMul.end()) want *= it->second; }
        want = std::min(want, 14.0f);
        if (have[s] < want - 0.5f && (int)agents.size() < agentBudget) {
            // appear out in the dark, where the density says, never in the lamp's pool; a threat never nearer than 60 m,
            // so its arrival at the lantern's edge is a real approach the crew can read
            float nearest = threat ? 60.0f : 22.0f;
            for (int tries = 0; tries < 30; tries++) {
                int c = cells[(size_t)(Rand() * cells.size()) % cells.size()];
                float maxw = 0; for (int k = 0; k < 6; k++) maxw = std::max(maxw, suit[s][cells[(size_t)(Rand() * cells.size()) % cells.size()]]);
                Vector2 at{(c % n + Rand()) * cell, (c / n + Rand()) * cell};
                if (nearC(at) < nearest || LightAt({at.x, at.y, 1}) > 0.005f) continue;
                if (suit[s][c] < Rand() * std::max(1e-9f, maxw)) continue;
                int ai = SpawnAgent(s, at);
                if (groups > 14) agents[ai].count = std::max(1, (int)(agents[ai].count * groups / 14));
                break;
            }
        } else if (have[s] > want + 1.5f) {
            int far = -1; float fd = 0;
            for (int i = 0; i < (int)agents.size(); i++) if (agents[i].sp == s) {
                float d = nearC({agents[i].p.x, agents[i].p.y});
                if (d > fd && LightAt(agents[i].p) < 0.02f) { fd = d; far = i; }
            }
            if (far >= 0) agents[far].alive = false;
        }
    }
    // gulls: fish on the deck or blood at the surface by the boat bring a flock in a little later
    if (boat) {
        float surf = blood.Near({boatPos.x, boatPos.y, 0.5f}, 3);
        bool draw = deckFish > 0 || surf > 20 || birdDrawOn;
        Vector2 drawAt = deckFish > 0 || surf > 20 || !birdDrawOn ? boatPos : birdDraw;   // (cooking smoke ashore, fish in a skiff)
        int gs = Species().Find("gull flock");
        bool have2 = false; for (const auto& a : agents) if (a.sp == gs && a.alive) have2 = true;
        if (draw && !have2 && gs >= 0) {
            if (gullT < 0) gullT = 20 + Rand() * 20;
            gullT -= 1;
            if (gullT <= 0) {
                float ang = Rand() * 6.2832f;
                int ai = SpawnAgent(gs, {drawAt.x + cosf(ang) * 140, drawAt.y + sinf(ang) * 140});
                agents[ai].hunger = 0.9f; gullT = -1;
                // the bigger thieves follow the gulls in now and then: a pelican (lifts 5 kg), a frigatebird (harries the rest)
                const char* big[2] = {"brown pelican", "frigatebird"}; float chance[2] = {0.35f, 0.25f};
                for (int k = 0; k < 2; k++) {
                    int bs = Species().Find(big[k]);
                    bool have = false; for (const auto& a : agents) if (a.sp == bs && a.alive) have = true;
                    if (bs < 0 || have || std::find(g->species.begin(), g->species.end(), bs) == g->species.end() || Rand() > chance[k]) continue;
                    float an = ang + 0.8f + k;
                    int bi = SpawnAgent(bs, {drawAt.x + cosf(an) * 150, drawAt.y + sinf(an) * 150});
                    agents[bi].hunger = 0.9f;
                }
            }
        } else if (!draw) gullT = -1;
    }
}

void Eco::StepAgents(float dt) {
    const auto& S = Species().sp;
    Vector2 o = boat ? boatPos : observer;
    int nightBlend = 0; (void)nightBlend;
    float rise = std::clamp(clock / 40.0f, 0.0f, 1.0f) * std::clamp((540 - clock) / 40.0f, 0.0f, 1.0f);   // dusk rise, dawn fall
    static std::set<int> arrivedThisNight; if (time < dt * 1.5f) arrivedThisNight.clear();
    for (size_t i = 0; i < agents.size(); i++) {
        EcoAgent& a = agents[i];
        if (!a.alive) continue;
        const SpeciesRec& r = S[a.sp];
        a.t += dt;
        a.hunger = std::min(1.0f, a.hunger + dt / 240.0f * (r.threat && boat ? (0.5f + Stir()) * threatHungerMul : 1.0f));   // threats grow bold with the Stir clock
        if (a.fedT > 0) a.fedT -= dt;
        if (a.flash > 0) a.flash -= dt;
        Vector2 p2{a.p.x, a.p.y};
        float fd = DepthAt(p2);
        // where it wants to be in the column: its band, risen at night
        float zb = BandDepth(r.band, fd), zn = BandDepth(r.nightBand, fd);
        float zp = zb + (zn - zb) * rise;
        Vector2 want = Vector2Scale(a.wander, 0.3f * r.speed);
        if (Rand() < dt * 0.5f) { float ang = (Rand() - 0.5f) * 1.6f; a.wander = Vector2Normalize(Vector2{a.wander.x * cosf(ang) - a.wander.y * sinf(ang), a.wander.x * sinf(ang) + a.wander.y * cosf(ang)}); }
        // home: lean toward better ground
        if (r.band != BAND_AIR) {
            int c0 = CellIdx(p2); float best = suit[a.sp][c0]; Vector2 bd{0, 0};
            const Vector2 D4[4] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (auto d : D4) { Vector2 q = Vector2Add(p2, Vector2Scale(d, 10)); if (!InMap(q)) continue; float s = suit[a.sp][CellIdx(q)]; if (s > best * 1.05f) { best = s; bd = d; } }
            want = Vector2Add(want, Vector2Scale(bd, 0.25f * r.speed));
        }
        // light: the forage and the curious come up to the lamp; the shy leave
        Vector2 ld; float G = Glow(*this, a.p, &ld);
        // predators follow the forage: a hungry hunter whose prey is drawn to the light comes up after it
        bool follows = false;
        if (r.light != LR_SHY && a.hunger > 0.35f && r.tier >= 2) for (const auto& e2 : r.eats) if (e2.first >= 0 && S[e2.first].light == LR_DRAWN) follows = true;
        if ((r.light == LR_DRAWN || follows) && G > 0.03f && r.band != BAND_AIR) {
            // in to the light, then milling round its pool (under the lamp is under the hull)
            float dl = 1e9f, lr = 8; for (const auto& l : lamps) { float d = Vector2Distance(p2, {l.p.x, l.p.y}); if (d < dl) { dl = d; lr = l.r; } }
            float ring = std::max(6.0f, lr * 0.55f) + (float)(a.sp % 3) * 1.5f;
            Vector2 tang{-ld.y, ld.x}; if (i % 2) tang = Vector2Scale(tang, -1);
            Vector2 pull = dl > ring ? ld : Vector2Scale(ld, -0.6f);
            float pullK = r.light == LR_DRAWN ? std::min(1.0f, G * 3) : std::min(0.35f, G * 1.2f);   // the hunters drift up after it, slower
            want = Vector2Add(Vector2Scale(want, 0.3f), Vector2Scale(Vector2Add(pull, Vector2Scale(tang, dl < ring * 1.6f ? 0.9f : 0.2f)), r.speed * pullK));
            zp = std::max(0.4f, zp - G * 6);
        }
        if (r.light == LR_SHY && G > 0.15f) want = Vector2Add(want, Vector2Scale(ld, -r.speed * 0.8f));
        // sargassum: mahi and flying fish shelter under the rafts
        if (r.habitat[H_SARGASSUM] > 1) {
            float bd = 60; Vector2 to{0, 0};
            for (const auto& rf : rafts) { float d = Vector2Distance(p2, rf.p); if (d < bd) { bd = d; to = Vector2Subtract(rf.p, p2); } }
            if (bd < 60 && bd > rafts[0].r) want = Vector2Add(want, Vector2Scale(Vector2Normalize(to), r.speed * 0.4f));
        }
        float speedCap = r.speed;
        // blood: past its threshold, a hungry hunter follows the smell up-current
        if (r.scent > 5 && a.hunger > 0.25f && r.band != BAND_AIR) {
            int R = std::clamp((int)(r.scent / cell), 1, 10); float o = R * cell * 0.5f;
            float here = std::max(blood.Near(a.p, R), blood.Near({a.p.x, a.p.y, 1}, R));
            if (here > r.bloodThreshold * 0.5f) {
                float gx = blood.Near({a.p.x + o, a.p.y, a.p.z}, R) - blood.Near({a.p.x - o, a.p.y, a.p.z}, R);
                float gy = blood.Near({a.p.x, a.p.y + o, a.p.z}, R) - blood.Near({a.p.x, a.p.y - o, a.p.z}, R);
                Vector2 gdir{gx, gy};
                if (here > r.bloodThreshold) {
                    if (Vector2Length(gdir) > 0.01f) want = Vector2Add(want, Vector2Scale(Vector2Normalize(gdir), r.speed));
                    else want = Vector2Add(want, Vector2Scale(Vector2Normalize(Vector2{-g->current.x, -g->current.y}), r.speed * 0.6f));
                    zp = std::min(zp, 3.0f); speedCap = r.speed * 1.2f;
                }
            }
        }
        // vibration: a lateral line reads a thrashing fish or a humming line
        if (r.lateral >= 8 && a.hunger > 0.3f && r.band != BAND_AIR) {
            int R = std::clamp((int)(r.lateral / cell), 1, 10); float o = R * cell * 0.5f;
            float here = vib.Near(a.p, R);
            if (here > 1) {
                float gx = vib.Near({a.p.x + o, a.p.y, a.p.z}, R) - vib.Near({a.p.x - o, a.p.y, a.p.z}, R);
                float gy = vib.Near({a.p.x, a.p.y + o, a.p.z}, R) - vib.Near({a.p.x, a.p.y - o, a.p.z}, R);
                if (fabsf(gx) + fabsf(gy) > 0.01f) want = Vector2Add(want, Vector2Scale(Vector2Normalize(Vector2{gx, gy}), r.speed * 0.8f));
            }
        }
        // noise: the timid flee a loud screw or a blast
        float loud = sound.Near(a.p, 1);
        if (r.fear > 0.3f && loud > 40 * (1.3f - r.fear)) {
            Vector2 away = Vector2Subtract(p2, o);
            if (Vector2Length(away) > 0.1f) want = Vector2Add(want, Vector2Scale(Vector2Normalize(away), r.speed * 1.2f));
            speedCap = r.speed * 1.4f; a.flash = 0.3f;
        }
        // a hunter after prey it can sense (sight by the light; a lateral line in the dark)
        if (a.target >= 0 && (a.target >= (int)agents.size() || !agents[a.target].alive)) a.target = -1;
        if (a.target < 0 && a.hunger > 0.35f && a.fedT <= 0 && r.band != BAND_AIR) {
            float bestD = 1e9f; int best = -1;
            for (size_t k = 0; k < agents.size(); k++) {
                if (k == i || !agents[k].alive) continue;
                bool eats = false; for (const auto& e : r.eats) if (e.first == agents[k].sp) eats = true;
                if (!eats) continue;
                float d = Vector3Distance(a.p, agents[k].p);
                float det = std::max(r.sight * (0.25f + 2 * LightAt(agents[k].p)), r.lateral);
                if (d < det && d < bestD) { bestD = d; best = (int)k; }
            }
            if (best >= 0) { a.target = best; a.chaseT = 0; }
        }
        if (a.target >= 0) {
            EcoAgent& pr = agents[a.target];
            Vector3 to = Vector3Subtract(pr.p, a.p);
            float d = Vector3Length(to);
            want = Vector2Scale(Vector2Normalize(Vector2{to.x, to.y}), r.speed * 1.3f);
            zp = pr.p.z; speedCap = r.speed * 1.3f;
            a.chaseT += dt;
            if (d < 1.8f && Rand() < dt * 1.5f) {
                pr.count--; pr.flash = 0.5f; if (pr.count <= 0) pr.alive = false;
                AddBlood(pr.p, S[pr.sp].MeanKg() * 2);
                a.hunger = 0.1f; a.fedT = 20 + 40 * std::min(1.0f, S[pr.sp].MeanKg() / std::max(0.1f, r.MeanKg()) * 4); a.target = -1; a.flash = 0.4f;
            } else if (a.chaseT > 15) { a.target = -1; a.fedT = 5; }
        }
        // a thief going for a hooked fish
        if (a.goalT > 0) {
            a.goalT -= dt;
            Vector3 to = Vector3Subtract(a.goal, a.p);
            if (Vector2Length({to.x, to.y}) > 0.3f) want = Vector2Scale(Vector2Normalize(Vector2{to.x, to.y}), r.speed * 1.3f);
            zp = a.goal.z; speedCap = r.speed * 1.3f;
        }
        // gulls circle the boat
        if (r.band == BAND_AIR && boat) {
            Vector2 home = boatPos;   // (or the smoke and fish ashore / a laden skiff, when that's nearer)
            if (birdDrawOn && Vector2Distance(birdDraw, p2) < Vector2Distance(boatPos, p2)) home = birdDraw;
            Vector2 to = Vector2Subtract(home, p2); float d = Vector2Length(to);
            Vector2 tang{-to.y, to.x};
            want = d > 18 ? Vector2Scale(Vector2Normalize(to), r.speed) : Vector2Scale(Vector2Normalize(tang), r.speed * 0.7f);
        }
        // move
        float wl = Vector2Length(want); if (wl > speedCap) want = Vector2Scale(want, speedCap / wl);
        float k = std::min(1.0f, dt * 2.5f);
        a.v.x += (want.x - a.v.x) * k; a.v.y += (want.y - a.v.y) * k;
        Vector2 np{a.p.x + a.v.x * dt, a.p.y + a.v.y * dt};
        if (r.band != BAND_AIR && (DepthAt(np) < 0.6f || !InMap(np))) { a.v.x = -a.v.x; a.v.y = -a.v.y; a.wander = Vector2Scale(a.wander, -1); np = p2; }
        a.p.x = np.x; a.p.y = np.y;
        if (r.band == BAND_AIR) a.p.z = -3;
        else {
            float nfd = DepthAt(np);
            a.p.z += std::clamp(zp - a.p.z, -0.6f * dt, 0.6f * dt);
            a.p.z = std::clamp(a.p.z, 0.2f, std::max(0.3f, nfd - 0.2f));
        }
        // a threat at the edge of the light: the crew's first tell
        if (boat && r.threat && Vector2Distance(np, boatPos) < 25 && !arrivedThisNight.count(a.sp)) {
            arrivedThisNight.insert(a.sp);
            arrivals.push_back({r.name, time});
            if (getenv("DEPTH_TRACE")) printf("      [eco] %s at the lantern's edge, clock %.0f, stir %.2f, wake %.1f, age %.0f s, hunger %.2f\n", r.name.c_str(), clock, Stir(), wake, a.t, a.hunger);
        }
    }
    agents.erase(std::remove_if(agents.begin(), agents.end(), [](const EcoAgent& a) { return !a.alive || a.count <= 0; }), agents.end());
}

void Eco::Step(float dt) {
    time += dt; clock = time;   // one real second, one minute of the night
    if (ground == "lagoon") tide = tideHeld ? 0 : 1.4f * std::clamp(clock / 540.0f, 0.0f, 1.0f);   // the reef tide falls all night (a king tide holds)
    // a red tide: the dead forage floats and leaks blood all over the ground, not just where the crew is
    if (redTide && boat && fieldAcc >= 1 - dt) for (int k = 0; k < 3; k++) { float a = Rand() * 6.2832f, d = 15 + Rand() * 60; AddBlood({boatPos.x + cosf(a) * d, boatPos.y + sinf(a) * d, 0.5f}, 4); }
    popAcc += dt; fieldAcc += dt; spawnAcc += dt; agentAcc += dt;
    while (popAcc >= 1) { popAcc -= 1; StepPop(1 / 60.0f); }
    while (fieldAcc >= 1) {
        fieldAcc -= 1;
        blood.Step(1, g->bloodDecay, 0.35f, g->current);
        sound.Step(1, 0.5f, 0.6f, {0, 0});
        vib.Step(1, 0.4f, 0.8f, {0, 0});
        for (auto& rf : rafts) {   // the rafts drift on the wind
            rf.p.x += 0.25f; rf.p.y += 0.1f * sinf(time * 0.01f + rf.r);
            if (rf.p.x > n * cell - 20) rf.p.x = 60;
            if (rf.p.y > n * cell - 20 || rf.p.y < 20) rf.p.y = n * cell * 0.5f;
        }
        // Wake: blood above the ambient, loud noise, bright light; it settles a point a minute when quiet and dark
        float crewBlood = blood.Near({boatPos.x, boatPos.y, 1}, 6) + blood.Near({boatPos.x, boatPos.y, 8}, 6);
        float lampTerm = 0; for (const auto& l : lamps) lampTerm += l.r >= 30 ? 1.0f : l.r >= 14 ? 0.2f : 0;
        float add = (boat ? std::max(0.0f, crewBlood - 5) * 0.07f + screwNoise * 0.11f + lampTerm : 0) / 60.0f;
        bool quiet = add < 0.3f / 60.0f;
        wake = std::clamp(wake + add - (quiet ? 1 / 60.0f : 0), 0.0f, 100.0f);
    }
    if (boat) {
        if (screwNoise > 0) AddNoise({boatPos.x, boatPos.y, 1}, screwNoise * dt * 4);
    }
    if (agentsOn) {
        while (spawnAcc >= 1) { spawnAcc -= 1; Materialize(); }
        while (agentAcc >= 0.1f) { agentAcc -= 0.1f; StepAgents(0.1f); }
    }
}

// ---------------------------------------------------------------- fishing: bites and thieves
int Eco::TryBite(Vector3 lure, Tackle t, const std::string& bait, float dt, int* agentIdx) {
    const auto& S = Species().sp;
    for (size_t i = 0; i < agents.size(); i++) {
        EcoAgent& a = agents[i];
        if (!a.alive || a.fedT > 0) continue;
        const SpeciesRec& r = S[a.sp];
        if (!r.Takes(t)) continue;
        float dh = Vector2Distance({a.p.x, a.p.y}, {lure.x, lure.y});
        if (dh > 16 || fabsf(a.p.z - lure.z) > 8) continue;   // it smells or sees the bait from a way off and comes to it
        bool match = false; for (const auto& b : r.baits) if (b == bait) match = true;
        float m = match ? 1.0f : 0.33f;                            // a matched presentation roughly triples the bite chance
        float wary = r.fear > 0.6f ? 0.6f : 1.0f;
        float rate = 0.08f * powf((float)a.count, 0.3f) * m * wary * (0.4f + 1.6f * a.hunger) * (1 - dh / 18);
        if (Rand() < rate * dt) { if (agentIdx) *agentIdx = (int)i; return a.sp; }
    }
    return -1;
}
FishSpec Eco::SpecOf(int sp, float u) const {
    const SpeciesRec& r = Species().sp[sp];
    FishSpec f{};
    f.name = r.name.c_str();
    f.kg = r.kgLo + (r.kgHi - r.kgLo) * powf(u, 1.6f);   // most are small
    f.a = r.fa; f.b = r.fb; f.wary = r.fear > 0.6f; f.teeth = r.teeth;
    f.depth = std::max(1.0f, BandDepth(r.band, 20)); f.floor = 40; f.price = r.price;
    f.pullK = r.pullK; f.staminaK = r.staminaK; f.softMouth = r.softMouth;
    return f;
}
void Eco::TakeFromAgent(int ai) {
    if (ai < 0 || ai >= (int)agents.size()) return;
    agents[ai].count--; if (agents[ai].count <= 0) agents[ai].alive = false;
}
void Eco::TakeNear(int sp, Vector3 at) {
    int best = -1; float bd = 30;
    for (int i = 0; i < (int)agents.size(); i++) if (agents[i].alive && agents[i].sp == sp) { float d = Vector3Distance(agents[i].p, at); if (d < bd) { bd = d; best = i; } }
    TakeFromAgent(best);
}
int Eco::Depredate(Vector3 fp, float kg, float dt, int* kind) {
    const auto& S = Species().sp;
    for (auto& a : agents) {
        if (!a.alive || a.fedT > 0) continue;
        const SpeciesRec& r = S[a.sp];
        if (!r.thief || a.hunger < 0.3f || kg > r.MeanKg() * (r.thief == 2 ? 2.0f : 1.2f)) continue;
        float d = Vector3Distance(a.p, fp);
        if (d > 25) continue;
        // close enough to see it struggling: it goes straight for it
        if (d < std::max(8.0f, r.sight * 0.6f)) { a.goal = fp; a.goalT = 0.5f; }
        if (d < 3 && Rand() < dt * (r.thief == 2 ? 0.5f : 0.7f)) {
            a.hunger = 0.1f; a.fedT = 60; a.flash = 0.6f;
            AddBlood(fp, kg * 2);
            if (kind) *kind = r.thief;
            return a.sp;
        }
    }
    return -1;
}

// ---------------------------------------------------------------- the Gannet in the water
// What she puts into the web each step (her lamps, her screw, her lines and the fish on them), and the web's own
// step. Also the chart under her keel: the Lagoon's crest and shallows put her aground.
void EcoTick(Eco& e, Gannet& gn, float dt) {
    const Boat& b = gn.boat;
    e.boat = true; e.boatPos = b.pos;
    e.skiffOn = gn.skiff.Up() && Vector2Distance(gn.skiff.p, b.pos) > 60; e.skiffPos = gn.skiff.p;
    e.lamps.clear();
    Vector2 mast = b.ToWorld(Stations()[(int)StationKind::Lantern].at);
    float r = LanternRadius(b.lantern);
    if (b.lantern == 3) {   // the searchlight's cone: a pool 20 m out along its bearing
        Vector2 dir{cosf(b.heading + b.searchAim), sinf(b.heading + b.searchAim)};
        e.lamps.push_back({{mast.x + dir.x * 18, mast.y + dir.y * 18, -4}, 14, 1.3f});
        e.lamps.push_back({{mast.x, mast.y, -4}, 5, 0.4f});
    } else e.lamps.push_back({{mast.x, mast.y, -4}, r, 1.0f});
    Vector2 stern = b.ToWorld({-10.4f, 0});
    e.lamps.push_back({{stern.x, stern.y, -2}, 5, 0.4f});
    for (const auto& fl : gn.flares) e.lamps.push_back({{fl.p.x, fl.p.y, -1}, 18, 1.4f});   // a flare burning on the water
    if (gn.skiff.Up()) { Vector2 bow = gn.skiff.ToWorld({1.9f, 0}); e.lamps.push_back({{bow.x, bow.y, -1}, D().skiffLantern, 0.8f}); }   // the skiff's bow lantern (6 m)
    // gulls over a deck with fish on it: one under 3 kg every 4 s (design doc, "Threat stats")
    {
        // every deck thief over her (gulls, a pelican, a frigatebird) takes the heaviest dead fish it can lift
        float lift = 0; int liftKind = BIRD_GULL; bool frigate = false;
        for (const auto& a : e.agents) {
            if (!a.alive || !Species().sp[a.sp].stealsDeck || Vector2Distance({a.p.x, a.p.y}, b.pos) >= 22) continue;
            int kind = BirdKindOf(Species().sp[a.sp].name);
            if (kind == BIRD_FRIGATE) frigate = true;
            if (Species().sp[a.sp].lifts > lift) { lift = Species().sp[a.sp].lifts; liftKind = kind; }
        }
        if (lift > 0) {
            gn.gullT += dt;
            if (gn.gullT >= 4) {
                gn.gullT = 0;
                // (design doc v2, "Birds and the catch crates": a dead fish is safe only crated, gutted into the hold, or
                // still alive and fighting)
                int best = -1;
                for (size_t i = 0; i < gn.hold.size(); i++) { const CatchRec& h = gn.hold[i]; if (h.gutted || h.crated || !h.dead || h.kg > lift) continue; if (best < 0 || h.kg > gn.hold[best].kg) best = (int)i; }
                if (best >= 0) {
                    // the lightest bird that can lift it takes it (gulls for the small fry, the pelican for the heavy ones)
                    int kind = liftKind;
                    for (int k = 0; k < BIRD_COUNT; k++) {
                        bool here = false;
                        for (const auto& a : e.agents) if (a.alive && BirdKindOf(Species().sp[a.sp].name) == k && Vector2Distance({a.p.x, a.p.y}, b.pos) < 22) here = true;
                        if (here && BirdOf(k).lifts >= gn.hold[best].kg && BirdOf(k).lifts < BirdOf(kind).lifts) kind = k;
                    }
                    gn.Steal(best, kind);
                }
            }
        } else gn.gullT = 0;
        e.birdDrawOn = false;
        for (const auto& L : gn.landings) if (!L.onFire.empty() || !L.onBeach.empty()) { e.birdDrawOn = true; e.birdDraw = L.ToWorld(L.fire); }
        if (!e.birdDrawOn && gn.skiff.Up() && !gn.skiff.load.empty() && Vector2Distance(gn.skiff.p, b.pos) > 30) { e.birdDrawOn = true; e.birdDraw = gn.skiff.p; }
        // away from her: fish in the skiff, on a beach or on a cooking fire are fair game too (design doc v2, "Cooking":
        // "fish waiting on the beach or in the skiff are fair game for birds")
        {
            struct Spot { std::vector<CatchRec>* v; Vector2 at; };
            std::vector<Spot> spots;
            if (gn.skiff.Up()) spots.push_back({&gn.skiff.load, gn.skiff.p});
            for (auto& L : gn.landings) { spots.push_back({&L.onBeach, L.at}); spots.push_back({&L.onFire, L.ToWorld(L.fire)}); }
            gn.awayGullT += dt;
            if (gn.awayGullT >= 4) {
                gn.awayGullT = 0;
                for (auto& sp : spots) {
                    float lift2 = 0; int kind2 = BIRD_GULL;
                    for (const auto& a : e.agents) if (a.alive && Species().sp[a.sp].stealsDeck && Vector2Distance({a.p.x, a.p.y}, sp.at) < 22 && Species().sp[a.sp].lifts > lift2) { lift2 = Species().sp[a.sp].lifts; kind2 = BirdKindOf(Species().sp[a.sp].name); }
                    if (lift2 <= 0) continue;
                    int best = -1;
                    for (size_t i = 0; i < sp.v->size(); i++) { const CatchRec& h = (*sp.v)[i]; if (h.junk || h.kg > lift2) continue; if (best < 0 || h.kg > (*sp.v)[best].kg) best = (int)i; }
                    Vector2 w = sp.at;
                    if (best >= 0) { if (sp.v != &gn.skiff.load) { for (auto& L : gn.landings) if (sp.v == &L.onBeach) w = L.ToWorld((*sp.v)[best].deckAt); } gn.StealFrom(*sp.v, best, w, kind2); break; }
                }
            }
        }
        // a frigatebird harries the other thieves until they drop their fish in mid-air (design doc v2, the birds' table)
        if (frigate) for (int ti = (int)gn.thieves.size() - 1; ti >= 0; ti--) if (gn.thieves[ti].kind != BIRD_FRIGATE && gn.thieves[ti].t > 1.0f) {
            gn.Say(TextFormat("A frigatebird harries the %s: it drops the %s", BirdOf(gn.thieves[ti].kind).name, gn.thieves[ti].fish.name.c_str()));
            gn.DropFish(ti);
        }
    }
    // a hand in the water: the reef shark comes for a thrashing, bleeding swimmer (a bite at the waterline)
    for (int k = 0; k < (int)gn.crew.size(); k++) {
        const Crew& c = gn.crew[k];
        if (!c.overboard || c.dead) continue;
        Vector3 sw{c.swim.x, c.swim.y, 0.5f};
        for (auto& a : e.agents) {
            const SpeciesRec& r = Species().sp[a.sp];
            if (!a.alive || r.aggression < 0.6f || r.band == BAND_AIR || a.fedT > 0) continue;
            float d = Vector3Distance(a.p, sw);
            if (d < 25 && a.hunger > 0.3f) { a.goal = sw; a.goalT = 0.5f; }
            if (d < 2.0f && e.Rand() < dt * 0.6f) { a.fedT = 20; a.flash = 0.6f; gn.Injure(k, INJ_BITE, std::string("a ") + r.name); break; }
        }
    }
    // chum: a thrown bucket bleeds out at the gutting rail (design doc, "The Chandler": 40 blood over 60 s)
    if (gn.chumLeft > 0) {
        float a = std::min(gn.chumLeft, dt * D().chumBlood / D().chumSeconds);
        Vector2 rail = b.ToWorld(Vector2Add(Stations()[(int)StationKind::Gutting].at, {0, 2.5f}));
        e.AddBlood({rail.x, rail.y, 1}, a); gn.chumLeft -= a;
    }
    // a shark on the blood rams the hull (design doc, "Threats": "rams the hull"; the Great White takes 25 off a section). A
    // hungry rammer alongside her, in blood past its threshold, strikes a section every D().ramEvery s while it stays.
    gn.ramT = std::max(0.0f, gn.ramT - dt);
    // the skiff first (design doc v2, "Why the skiff is dangerous": predators weight their target toward the smallest
    // vessel with the most blood round it): a hungry rammer close to her, in less blood than the Gannet would need,
    // strikes her - a hard heel that can roll her over, and a third of her planking
    if (gn.ramT <= 0 && gn.skiff.state == SkiffState::Afloat) {
        for (auto& a : e.agents) {
            const SpeciesRec& r = Species().sp[a.sp];
            if (!a.alive || !r.ramsHull || a.hunger < 0.4f || a.fedT > 0) continue;
            if (Vector2Distance({a.p.x, a.p.y}, gn.skiff.p) > 4.0f) continue;
            int R = std::clamp((int)(r.scent / e.cell), 1, 10);
            float sb = std::max(e.blood.Near({gn.skiff.p.x, gn.skiff.p.y, 1}, R), e.blood.Near({gn.skiff.p.x, gn.skiff.p.y, 5}, R));
            if (sb < r.bloodThreshold * 0.6f) continue;
            float k = std::clamp(r.MeanKg() / 40.0f, 0.6f, 1.7f);
            Vector2 side{-gn.skiff.Forward().y, gn.skiff.Forward().x};
            float s = Vector2DotProduct(Vector2Subtract({a.p.x, a.p.y}, gn.skiff.p), side) > 0 ? -1.0f : 1.0f;
            gn.SkiffRock(s * 1.6f * k);
            gn.SkiffHit(D().ramDamage * 0.9f * k, std::string("a ") + r.name + " rams her");
            e.AddVibration({a.p.x, a.p.y, 1}, 3); a.flash = 0.6f;
            gn.ramT = D().ramEvery;
            break;
        }
    }
    if (gn.ramT <= 0) {
        for (auto& a : e.agents) {
            const SpeciesRec& r = Species().sp[a.sp];
            if (!a.alive || !r.ramsHull || a.hunger < 0.4f || a.fedT > 0) continue;
            Vector2 dk = b.ToDeck({a.p.x, a.p.y});
            if (fabsf(dk.x) > 12 || fabsf(dk.y) > 6.5f) continue;
            // the blood it smells about her, over its own scent radius (as the agents hunt it)
            int R = std::clamp((int)(r.scent / e.cell), 1, 10);
            float hullBlood = std::max(e.blood.Near({b.pos.x, b.pos.y, 1}, R), e.blood.Near({b.pos.x, b.pos.y, 6}, R));
            if (hullBlood < r.bloodThreshold) continue;
            int sec = SectionAt({std::clamp(dk.x, -10.0f, 10.0f), dk.y < 0 ? -2.0f : 2.0f});
            float dmg = D().ramDamage * std::clamp(r.MeanKg() / 40.0f, 0.6f, 1.7f);
            gn.boat.Hit(sec, dmg);
            gn.boat.rollVel += (dk.y > 0 ? -1 : 1) * D().ramHeel * std::clamp(r.MeanKg() / 40.0f, 0.6f, 1.7f);   // the blow heels her: unbraced hands slide
            gn.Say(TextFormat("Something struck the hull: %s", SectionName(sec)));
            e.AddVibration({a.p.x, a.p.y, 2}, 3); a.flash = 0.6f;
            gn.ramT = D().ramEvery;
            break;
        }
    }
    e.screwNoise = b.noise;
    if (b.noise > 0) e.AddVibration({stern.x, stern.y, 2}, b.noise * 0.3f * dt);
    // the tow line: fish killed alongside the skiff bleed all the way home
    for (const auto& tw : gn.towed) if (gn.skiff.Up()) e.AddBlood({gn.skiff.p.x, gn.skiff.p.y, 0.8f}, 0.04f * tw.kg * dt);
    std::vector<const Rod*> fighting;
    for (const auto& rd : gn.rods) fighting.push_back(&rd);
    fighting.push_back(&gn.skiffRod);
    for (const Rod* rp : fighting) {
        const Rod& rd = *rp;
        if (rd.state != RodState::Fighting) continue;
        const Fight& f = rd.fight;
        e.AddVibration(f.p, (0.5f + f.effort * 2) * dt);   // a thrashing fish is a beacon
        float rating = TackleOf(f.tackle).strength;
        if (f.tension > 0.3f * rating) e.AddVibration(Vector3Lerp(f.tip, f.p, 0.5f), dt);   // a taut line hums
    }
    e.deckFish = gn.DeckFish();
    // the Weeds: running the engine through the kelp canopy wraps the screw; she makes half her way until a hand in the
    // water at the stern cuts it free (Gannet::CutScrew)
    bool inMat = false;
    if (e.ground == "weeds") for (const auto& rf : e.rafts) if (Vector2Distance(rf.p, b.pos) < rf.r) inMat = true;   // (a drift mat fouls her too)
    if ((e.HabAt(b.pos) == H_KELP || inMat) && b.shaft > 0.3f && !gn.screwFouled && e.Rand() < dt / 15 * e.foulMul) {   /* (about once in 15 s under way in the canopy) */ gn.screwFouled = true; gn.Say("Kelp round the screw! She's making half her way: someone has to go over the stern and cut it free"); }
    if (gn.screwFouled && b.shaft > 0.05f) gn.boat.vel = Vector2Scale(gn.boat.vel, expf(-0.5f * dt));
    // the Sargassum Line: a turning screw in the weed bank fouls (she wallows to a crawl; the skiff slips through)
    {
        int mk = e.MarkAt(b.pos);
        static bool fouled = false;
        bool now = mk >= 0 && e.marks[mk].kind == 1 && b.shaft > 0.05f;
        if (now) { gn.boat.vel = Vector2Scale(gn.boat.vel, expf(-0.45f * dt)); if (!fouled) gn.Say("Weed round the screw: the Sargassum Line is no water for her (take the skiff)"); }
        fouled = now;
    }
    // aground: the chart under her keel (she draws about 1.8 m)
    float d = e.DepthAt(b.pos);
    bool was = gn.boat.aground;
    gn.boat.aground = e.InMap(b.pos) && d < 1.8f;
    if (gn.boat.aground) {
        // the ground stops her going further in; backing (or steering) toward deeper water frees her
        float sp = Vector2Length(gn.boat.vel);
        Vector2 dir = sp > 0.01f ? Vector2Scale(gn.boat.vel, 1 / sp) : Vector2{0, 0};
        bool off = sp > 0.01f && e.DepthAt(Vector2Add(b.pos, Vector2Scale(dir, 1.5f))) > d + 0.05f;
        if (!off) {
            Vector2 back = Vector2Scale(gn.boat.vel, -dt * 1.5f);
            gn.boat.pos = Vector2Add(gn.boat.pos, back);
            gn.boat.vel = Vector2Scale(gn.boat.vel, 0.2f);
        }
        if (!was) gn.Say(d <= 0 ? "She's run up on the island" : "She's aground on the crest");
    }
    e.Step(dt);
}

// ---------------------------------------------------------------- --trawl-eco
namespace {
std::string Abbr(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size() && o.size() < 5; i++) if (s[i] != ' ' && s[i] != '-') o += s[i];
    return o;
}
std::string Clock(float minutes) {
    int h = 20 + (int)minutes / 60, m = (int)minutes % 60;
    return TextFormat("%02d:%02d", h % 24, m);
}
// a scripted crew for the patterns: the Gannet anchored mid-lagoon, lamp low, screw stopped
void PatternStep(Eco& e, const std::string& pat, float dt, float& acc, int& charges) {
    const auto& S = Species().sp;
    e.boat = pat != "none";
    e.lamps.clear();
    if (!e.boat) return;
    e.lamps.push_back({{e.boatPos.x, e.boatPos.y, -3}, 8, 1.0f});
    e.screwNoise = 0;
    acc += dt;
    if (pat == "chumming" && acc >= 30) { acc -= 30; e.AddBlood({e.boatPos.x - 10, e.boatPos.y, 1}, 40); }
    if (pat == "trawling") {
        e.screwNoise = 5;   // half ahead, towing
        if (acc >= 60) {
            acc -= 60;
            for (const char* nm : {"silverside", "sardine", "flying fish", "moon jelly", "green turtle", "grunt", "snapper"}) {
                int s = Species().Find(nm); if (s < 0) continue;
                float frac = strcmp(nm, "silverside") == 0 || strcmp(nm, "sardine") == 0 ? 0.015f : 0.004f;
                float kg = e.B[s] * frac;
                e.Harvest(s, kg, {e.boatPos.x, e.boatPos.y, 3}, false);
                if (S[s].price <= 0.6f || S[s].protectedSp || S[s].stings) { e.R[R_CARRION] += kg; e.AddBlood({e.boatPos.x - 12, e.boatPos.y, 1}, kg * 0.2f); }   // bycatch over the side
            }
        }
    }
    if (pat == "depth-charging" && acc >= 180) { acc -= 180; charges++; e.DepthCharge({e.boatPos.x + 30, e.boatPos.y, 6}); }
}
} // namespace

int RunTrawlEco(int argc, char** argv) {
    // depth.exe --trawl-eco <ground> <minutes> [pattern: none|quiet|chumming|trawling|depth-charging]
    std::string ground = argc >= 3 ? argv[2] : "lagoon";
    float minutes = argc >= 4 ? (float)atof(argv[3]) : 27;
    std::string pat = argc >= 5 ? argv[4] : "none";
    Eco e;
    if (!e.Init(ground, 20261)) return 2;
    const auto& S = Species().sp;
    e.agentsOn = pat != "none";
    e.boatPos = {e.n * e.cell * 0.42f, e.n * e.cell * 0.5f};
    printf("The Trawl: %s, %.0f minutes of night (%s)%s\n", e.g->name.c_str(), minutes, pat.c_str(), pat == "none" ? ", no crew; a day between nights" : "");
    printf("  %-5s", "time");
    for (int s : e.g->species) printf(" %5s", Abbr(S[s].name).c_str());
    printf("  coral  blood  wake\n");
    std::vector<float> lo(S.size(), 1e9f), hi(S.size(), 0);
    float t = 0, acc = 0; int charges = 0;
    const float dt = 1 / 20.0f;
    int arrivalsShown = 0;
    while (t < minutes * 60 - 1e-3f) {
        PatternStep(e, pat, dt, acc, charges);
        e.Step(dt); t += dt;
        for (int s : e.g->species) { lo[s] = std::min(lo[s], e.PopFrac(s)); hi[s] = std::max(hi[s], e.PopFrac(s)); }
        for (; arrivalsShown < (int)e.arrivals.size(); arrivalsShown++) printf("  %s  arrival: %s\n", Clock(e.arrivals[arrivalsShown].t).c_str(), e.arrivals[arrivalsShown].species.c_str());
        bool hour = fmodf(e.time + 1e-3f, 60) < dt;
        bool nightEnd = e.time >= 540 - 1e-3f;
        if (hour || nightEnd) {
            printf("  %s", Clock(roundf(e.clock)).c_str());
            for (int s : e.g->species) printf(" %5.0f", e.PopFrac(s) * 100);
            printf("  %5.2f %6.0f %5.1f\n", e.coral, e.blood.Total(), e.wake);
        }
        if (nightEnd && t < minutes * 60 - 1) {
            e.Day(15);
            printf("  -- day %d (15 h, populations only) --\n", e.night);
            e.StartNight();
        }
    }
    int fails = 0;
    printf("  lowest / highest share of the start over the run:\n");
    for (int s : e.g->species) {
        bool bad = lo[s] * S[s].start < 1 || hi[s] > 1.5f;
        if (bad) fails++;
        printf("    %-14s %5.0f%% .. %5.0f%%%s\n", S[s].name.c_str(), lo[s] * 100, hi[s] * 100, bad ? "   OUT (extinct or above 150%)" : "");
    }
    for (int k = 0; k < R_COUNT; k++) printf("    %-14s %5.0f%% of its start at the end\n", ResName(k), e.R[k] / e.R0[k] * 100);
    printf("  Wake at the end: %.1f   threat arrivals: %d%s\n", e.wake, (int)e.arrivals.size(), charges ? TextFormat("   depth charges: %d", charges) : "");
    if (pat == "none") printf(fails ? "%d species out of bounds\n" : "The ground is stable with no crew\n", fails);
    return pat == "none" && fails ? 1 : 0;
}

// ---------------------------------------------------------------- --trawl-eco-test
int RunTrawlEcoTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Trawl stage 3: the Lagoon's food web\n");
    const auto& S = Species().sp;
    check(Species().ok, TextFormat("trawl_species.json loads (%d species)", (int)S.size()));
    if (!Species().ok) return 1;
    auto sp = [&](const char* n) { return Species().Find(n); };
    auto nights = [](Eco& e, int k) { for (int i = 0; i < k; i++) { for (int t = 0; t < 540; t++) e.Step(1); if (i + 1 < k) { e.Day(15); e.StartNight(); } } };
    // 1. the ground with no crew holds its start for three nights (the doc's gate)
    {
        Eco e; e.Init("lagoon", 1); e.agentsOn = false;
        check((int)e.g->species.size() >= 20, TextFormat("the Lagoon holds %d species (the doc's roster)", (int)e.g->species.size()));
        float lo = 1e9f, hi = 0;
        for (int i = 0; i < 3; i++) { for (int t = 0; t < 540; t++) { e.Step(1); for (int s : e.g->species) { lo = std::min(lo, e.PopFrac(s)); hi = std::max(hi, e.PopFrac(s)); } } if (i < 2) { e.Day(15); e.StartNight(); } }
        check(lo > 0.99f && hi < 1.01f, TextFormat("with no crew nothing moves over three nights (%.1f%%..%.1f%%)", lo * 100, hi * 100));
    }
    // 2. knocked 30% off balance, it settles back rather than swinging wider
    {
        Eco e; e.Init("lagoon", 2); e.agentsOn = false;
        double dev0 = 0; int k = 0;
        for (int s : e.g->species) { if (S[s].isStatic) continue; float f = 0.7f + 0.6f * e.Rand(); e.B[s] *= f; dev0 += fabs(f - 1); k++; }
        for (int r = 0; r < R_COUNT; r++) e.R[r] *= 0.8f + 0.4f * e.Rand();
        float lo = 1e9f;
        for (int i = 0; i < 3; i++) { for (int t = 0; t < 540; t++) { e.Step(1); for (int s : e.g->species) lo = std::min(lo, e.PopFrac(s)); } e.Day(15); e.StartNight(); }
        for (int d = 0; d < 20; d++) e.Day(24);
        double dev1 = 0; for (int s : e.g->species) if (!S[s].isStatic) dev1 += fabs(e.PopFrac(s) - 1);
        check(dev1 < dev0 * 0.5 && lo > 0.3f, TextFormat("a ground knocked off balance settles back (mean deviation %.0f%% -> %.0f%% after three weeks; lowest %.0f%%)", dev0 / k * 100, dev1 / k * 100, lo * 100));
    }
    // 3. netting the forage leaves the predators hungry the next night (the doc's cascade)
    {
        Eco e; e.Init("lagoon", 3); e.agentsOn = false;
        float h0 = e.Hunger(sp("snapper"));
        e.Harvest(sp("silverside"), e.B[sp("silverside")] * 0.6f, {0, 0, 0}, false);
        e.Harvest(sp("sardine"), e.B[sp("sardine")] * 0.6f, {0, 0, 0}, false);
        nights(e, 1); e.Day(15); e.StartNight(); e.Step(1);
        float h1 = e.Hunger(sp("snapper")), hb = e.Hunger(sp("bonito")), hj = e.Hunger(sp("jack crevalle"));
        check(h1 > h0 + 0.05f && hb > h0 + 0.05f, TextFormat("with 60%% of the forage netted, snapper/bonito/jacks are hungrier on night two (%.2f -> %.2f / %.2f / %.2f)", h0, h1, hb, hj));
    }
    // 4. netting the grazers lets algae smother the coral, and the reef fish thin out
    {
        Eco e; e.Init("lagoon", 4); e.agentsOn = false;
        e.Harvest(sp("parrotfish"), e.B[sp("parrotfish")] * 0.85f, {0, 0, 0}, false);
        e.Harvest(sp("surgeonfish"), e.B[sp("surgeonfish")] * 0.85f, {0, 0, 0}, false);
        float g0 = e.PopFrac(sp("grunt"));
        nights(e, 3);
        check(e.R[R_ALGAE] > e.R0[R_ALGAE] * 1.3f && e.coral < 0.9f, TextFormat("parrotfish netted out: algae %.0f%%, coral %.2f by night three", e.R[R_ALGAE] / e.R0[R_ALGAE] * 100, e.coral));
        check(e.PopFrac(sp("grunt")) < g0 - 0.01f, TextFormat("the reef fish thin out with the coral (grunt %.0f%% -> %.0f%%)", g0 * 100, e.PopFrac(sp("grunt")) * 100));
    }
    // 5. the lantern: a full lamp pulls the forage (and what eats it) up under the hull; a hooded one doesn't
    {
        auto underHull = [&](float r) {
            Eco e; e.Init("lagoon", 5);
            e.boat = true; e.boatPos = {e.n * e.cell * 0.42f, e.n * e.cell * 0.5f};
            for (int t = 0; t < 30 * 20; t++) { e.lamps = {{{e.boatPos.x, e.boatPos.y, -3}, r, 1.0f}}; e.Step(0.05f); }
            int forage = 0, preds = 0;
            for (const auto& a : e.agents) if (Vector2Distance({a.p.x, a.p.y}, e.boatPos) < 20) { if (S[a.sp].tier == 1 && S[a.sp].light == LR_DRAWN) forage += a.count; else if (S[a.sp].tier >= 2) preds++; }
            return std::make_pair(forage, preds);
        };
        auto full = underHull(14), hood = underHull(4);
        check(full.first >= 2 * std::max(1, hood.first), TextFormat("after 30 minutes a full lantern has %d forage fish under the hull, a hooded one %d", full.first, hood.first));
        printf("        (hunters within 20 m: %d with the full lamp, %d hooded)\n", full.second, hood.second);
    }
    // 6. many species rise after dark
    {
        Eco e; e.Init("lagoon", 6); e.boat = true; e.boatPos = {e.n * e.cell * 0.42f, e.n * e.cell * 0.5f};
        auto meanZ = [&](int s) { double z = 0; int k = 0; for (const auto& a : e.agents) if (a.sp == s) { z += a.p.z; k++; } return k ? z / k : -1.0; };
        for (int t = 0; t < 4 * 20; t++) e.Step(0.05f);
        double z0 = meanZ(sp("sardine"));
        for (int t = 0; t < 80 * 20; t++) e.Step(0.05f);
        double z1 = meanZ(sp("sardine"));
        check(z0 > 0 && z1 >= 0 && z1 < z0 - 1, TextFormat("sardines rise after dark (mean depth %.1f m at 20:04, %.1f m at 21:24)", z0, z1));
    }
    // 7. blood: a hungry reef shark down-current follows a chum slick to the boat
    {
        Eco e; e.Init("lagoon", 7); e.boat = true; e.boatPos = {e.n * e.cell * 0.42f, e.n * e.cell * 0.5f}; e.agentsOn = true;
        e.agentBudget = 0;   // only the actors placed here
        Vector2 cur = Vector2Normalize(e.g->current);
        int shark = sp("reef shark");
        int ai = e.SpawnAgentPublic(shark, Vector2Add(e.boatPos, Vector2Scale(cur, 30)));
        e.agents[ai].hunger = 0.8f; e.agents[ai].p.z = 2;
        float d0 = Vector2Distance({e.agents[ai].p.x, e.agents[ai].p.y}, e.boatPos), dmin = d0;
        for (int t = 0; t < 180 * 20; t++) {
            if (t % (30 * 20) == 0) e.AddBlood({e.boatPos.x, e.boatPos.y, 1}, 60);
            e.lamps.clear(); e.Step(0.05f);
            for (const auto& a : e.agents) if (a.sp == shark) dmin = std::min(dmin, Vector2Distance({a.p.x, a.p.y}, e.boatPos));
            if (getenv("DEPTH_TRACE2") && t % 200 == 0) for (const auto& a : e.agents) printf("      t%4d shark (%.0f,%.0f,%.1f) d %.0f blood here %.1f hunger %.2f n=%d\n", t/20, a.p.x, a.p.y, a.p.z, Vector2Distance({a.p.x,a.p.y}, e.boatPos), e.blood.Near(a.p, 5), a.hunger, (int)e.agents.size());
        }
        check(dmin < 15, TextFormat("a reef shark 30 m down-current follows the chum to the boat (closest %.0f m)", dmin));
        bool arrived = false; for (const auto& a : e.arrivals) if (a.species == "reef shark") arrived = true;
        check(arrived, "its arrival is logged as a threat at the lantern's edge");
    }
    // 8. vibration: a thrashing fish draws a barracuda pack by its lateral line
    {
        Eco e; e.Init("lagoon", 8); e.boat = true; e.boatPos = {e.n * e.cell * 0.42f, e.n * e.cell * 0.5f}; e.agentBudget = 0;
        int bar = sp("barracuda");
        Vector3 fish{e.boatPos.x + 12, e.boatPos.y, 3};
        int ai = e.SpawnAgentPublic(bar, {fish.x + 22, fish.y + 5});
        e.agents[ai].hunger = 0.8f; e.agents[ai].p.z = 3;
        float dmin = 1e9f;
        for (int t = 0; t < 90 * 20; t++) { e.AddVibration(fish, 8 * 0.05f); e.Step(0.05f); for (const auto& a : e.agents) if (a.sp == bar) dmin = std::min(dmin, Vector3Distance(a.p, fish)); }
        check(dmin < 8, TextFormat("a hooked fish's thrashing draws a barracuda pack from 22 m (closest %.0f m)", dmin));
        int kind = 0, thief = -1;
        for (int t = 0; t < 30 * 20 && thief < 0; t++) { e.AddVibration(fish, 0.4f); e.Step(0.05f); thief = e.Depredate(fish, 2.0f, 0.05f, &kind); }
        check(thief == bar && kind == 1, TextFormat("the pack strikes the hooked fish to the head (%s)", thief >= 0 ? S[thief].name.c_str() : "nothing"));
    }
    // 9. Wake: chum raises it; quiet and dark, it settles a point a minute; a depth charge is +15
    {
        Eco e; e.Init("lagoon", 9); e.boat = true; e.boatPos = {e.n * e.cell * 0.42f, e.n * e.cell * 0.5f}; e.agentsOn = false;
        for (int t = 0; t < 300; t++) { if (t % 30 == 0) e.AddBlood({e.boatPos.x - 6, e.boatPos.y, 1}, 40); e.Step(1); }
        float w1 = e.wake;
        for (int t = 0; t < 240; t++) e.Step(1);
        float w2 = e.wake;
        e.DepthCharge({e.boatPos.x + 30, e.boatPos.y, 6});
        check(w1 > 3, TextFormat("five minutes of chumming raise Wake to %.1f", w1));
        check(w2 < w1 - 1.5f && w2 > w1 - 4.5f, TextFormat("four quiet, dark minutes settle it to %.1f (a point a minute once the slick has gone)", w2));
        check(fabsf(e.wake - w2 - 15) < 0.01f, "a depth charge adds 15");
    }
    // 10. bites: fish take what they eat; a matched bait roughly triples the chance
    {
        Eco e; e.Init("lagoon", 10); e.agentBudget = 0; e.agentsOn = false;
        int sn = sp("snapper");
        Vector3 lure{300, 300, 6};
        for (int i = 0; i < 6; i++) { int ai = e.SpawnAgentPublic(sn, {lure.x + (i - 3) * 1.5f, lure.y}); e.agents[ai].p.z = 6; e.agents[ai].hunger = 0.5f; e.agents[ai].count = 8; }
        int hitM = 0, hitU = 0;
        for (int k = 0; k < 20000; k++) { if (e.TryBite(lure, Tackle::Light, "shrimp", 0.05f, nullptr) >= 0) hitM++; if (e.TryBite(lure, Tackle::Light, "popper", 0.05f, nullptr) >= 0) hitU++; }
        float ratio = (float)hitM / std::max(1, hitU);
        check(ratio > 2.3f && ratio < 4.0f, TextFormat("snapper take shrimp %.1fx as often as a popper", ratio));
        int never = 0; for (int k = 0; k < 2000; k++) if (e.TryBite(lure, Tackle::Chair, "live bait", 0.05f, nullptr) >= 0) never++;
        check(never == 0, "nothing in the Lagoon's reef takes the big-game chair");
        FishSpec f = e.SpecOf(sn, 0.5f);
        check(f.kg >= 1 && f.kg <= 6 && f.a == Pattern::Run && std::string(f.name) == "snapper", TextFormat("a hooked snapper is %.1f kg with the Run pattern (from its record)", f.kg));
    }
    // 11. the crew's harvest comes off the population
    {
        Eco e; e.Init("lagoon", 11); e.agentsOn = false;
        int gr = sp("grouper"); float b0 = e.B[gr];
        e.Harvest(gr, 30, {0, 0, 0}, true);
        check(fabsf(e.B[gr] - (b0 - 30)) < 0.01f && e.blood.Total() > 0, "a landed grouper comes off the ground's biomass and bleeds");
    }
    // 12. the Gannet on the Lagoon: her lamp doubles the bites after half an hour, and her rods catch the web's fish
    {
        auto bites1 = [&](int lantern, uint32_t seed) {
            Eco e; e.Init("lagoon", seed);
            Gannet gn; gn.Init(1, seed, Weather::Calm); gn.eco = &e;
            gn.boat.pos = {e.n * e.cell * 0.42f, e.n * e.cell * 0.5f}; gn.boat.lantern = lantern;
            Vector2 lw = gn.boat.ToWorld({-0.8f, 9});
            int n = 0;
            for (int t = 0; t < 90 * 60; t++) {
                gn.Step(1 / 60.0f);
                if (t >= 30 * 60 && t % 6 == 0) for (Vector3 lure : {Vector3{lw.x, lw.y, 2}, Vector3{lw.x, lw.y, 5}, Vector3{lw.x, lw.y, 9}})
                    if (e.TryBite(lure, Tackle::Light, "shrimp", 0.1f, nullptr) >= 0) n++;
            }
            return n;
        };
        auto bites = [&](int lantern) { int n = 0; for (uint32_t sd = 12; sd < 20; sd++) n += bites1(lantern, sd); return n; };   // eight seeds: one dark hour can draw nothing by chance
        int full = bites(2), hood = bites(0);
        // (over eight seeds the ratio runs 3.4x-4.6x depending on how the chart's reefs fall - the Atoll's ring moved it)
        check(full >= hood * 1.6f && full <= hood * 5 && hood > 0, TextFormat("from 20:30 to 21:30 a light rod by the rail draws %d bites under a full lantern, %d hooded", full, hood));
        Eco e; e.Init("lagoon", 13);
        Gannet gn; gn.Init(1, 13, Weather::Calm); gn.eco = &e;
        gn.boat.pos = {e.n * e.cell * 0.42f, e.n * e.cell * 0.5f};
        int ri = -1; for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == StationKind::StarRod) { gn.crew[0].p = Stations()[i].at; gn.crew[0].station = i; ri = gn.RodAt(i); }
        gn.rods[ri].tackle = Tackle::Light; gn.rods[ri].fight.drag = 2; gn.rods[ri].botAngler = true;
        Vector2 aim = Vector2Add(gn.rods[ri].TipDeck(), {0, 8});
        std::set<std::string> kinds; int landed = 0; float b0 = 0; for (float v : e.B) b0 += v;
        for (int t = 0; t < 400 * 60; t++) {
            Rod& r = gn.rods[ri];
            bool cast = r.state == RodState::Idle && (t % 120) < 60;
            gn.RodInput(0, cast, aim, false, r.bite.stage == BiteStage::Take, 0, false, false, 0);
            size_t h0 = gn.hold.size();
            gn.Step(1 / 60.0f);
            if (gn.hold.size() > h0) { landed++; kinds.insert(gn.hold.back().name); }
        }
        float b1 = 0; for (float v : e.B) b1 += v;
        std::string ks; for (const auto& k : kinds) ks += (ks.empty() ? "" : ", ") + k;
        check(landed >= 3, TextFormat("a light rod fished for most of the night lands %d fish from the web (%s)", landed, ks.c_str()));
        (void)b0; (void)b1;
    }
    printf(fails ? "trawl-eco-test: %d check(s) failed\n" : "trawl-eco-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}

} // namespace tw
