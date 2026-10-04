// Mouthful's headless core (see mouthful.h): the data, the arena, the round, the mouths, the bots and the NPC predators.
#include "mouthful.h"
#include "json.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace mf {

// ---------------------------------------------------------------- data
const char* BandName(int b) { static const char* N[] = {"the Shallows", "the Reef", "the Wall", "the Blue", "the Trench"}; return b >= 0 && b < B_COUNT ? N[b] : "?"; }
const char* ModeName(int m) { static const char* N[M_COUNT] = {"Mouthful", "Blobfish Only", "One Path", "Trench Rush", "Food Chain", "King of the Reef", "Solo Tank"}; return m >= 0 && m < M_COUNT ? N[m] : "?"; }
const char* ModeRule(int m) {
    static const char* R[M_COUNT] = {"As designed: eat, grow, fork, take the crown.", "Everyone's a blobfish; the first to tier 6 wins; nobody has abilities and everyone is sad.",
        "The lobby picks one path for everyone.", "The round starts at tier 4 in the trench; 8 minutes; the leviathan is awake.", "Teams of three share a path and a score; a kill feeds the team.",
        "No respawn for a king: lose the crown and you're out; last crown standing.", "You, the bots, and a longer round for learning the paths."};
    return m >= 0 && m < M_COUNT ? R[m] : "";
}
const char* PathName(int p) { static const char* N[] = {"Eel", "Cephalopod", "Shark", "Crustacean", "Pufferfish", "Blobfish"}; return p >= 0 && p < P_COUNT ? N[p] : "Fry"; }
static int PathKey(const std::string& s) {
    if (s == "eel") return P_EEL; if (s == "ceph") return P_CEPH; if (s == "shark") return P_SHARK;
    if (s == "crust") return P_CRUST; if (s == "puffer") return P_PUFFER; if (s == "blob") return P_BLOB; return -1;
}
static Ability AbilityKey(const std::string& s) {
    static const char* K[] = {"none", "dash", "lunge", "death_roll", "ribbon", "ink", "dazzle", "ink_wall", "hook", "frenzy", "ram", "pin", "breach", "punch", "tail_flip", "jump", "claw_slam", "roll", "inflate", "toxin", "detonate", "ambush"};
    for (int i = 0; i < AB_COUNT; i++) if (s == K[i]) return (Ability)i;
    return AB_NONE;
}
static Passive PassiveKey(const std::string& s) {
    static const char* K[] = {"none", "hole", "venom", "drag", "armor_back", "omen", "jet", "camo", "hover", "eight_arms", "trench_born", "blood_sense", "thick_skin", "electro", "apex", "iron_stomach", "burrow", "spines_back", "crusher", "fortress", "scavenger", "spines", "barbed", "plates", "untouchable", "venom_spines"};
    for (int i = 0; i < PS_COUNT; i++) if (s == K[i]) return (Passive)i;
    return PS_NONE;
}
static Color Col(const Json& j, Color def) { if (!j.IsArr() || j.Size() < 3) return def; return {(unsigned char)j[0].I(), (unsigned char)j[1].I(), (unsigned char)j[2].I(), 255}; }

static Data LoadData() {
    Data d;
    std::string dir = rt::DataDir() + "/../mouthful/";
    Json t = LoadJsonFile(dir + "mouthful_tiers.json");
    for (const Json& r : t["tiers"].a) {
        int k = r["tier"].I(0); if (k < 1 || k > 8) continue;
        TierDef& T = d.tiers[k];
        T.mass = r["mass"].F(0); T.length = r["length_m"].F(0.1f); T.speed = r["speed"].F(4); T.biteCd = r["bite_cd"].F(0.8f); T.turn = r["turn_deg_s"].F(300);
    }
    d.decayPerMin = t["decay_per_minute"].F(0.01f); d.swallowBelow = t["swallow_below"].F(0.6f); d.fightPct = t["fight_pct"].F(0.1f);
    d.swallowS = t["swallow_s"].F(1); d.boostMul = t["boost_mul"].F(1.6f); d.staminaS = t["stamina_s"].F(4); d.staminaRefillS = t["stamina_refill_s"].F(6);
    d.respawnS = t["respawn_s"].F(5); d.immuneS = t["immune_s"].F(10); d.startMass = t["start_mass"].F(15); d.kingBonus = t["king_bonus"].F(0.1f);
    d.streakBonus = t["streak_bonus"].F(0.05f); d.armoredCost = t["armored_cost"].F(5); d.planktonPerS = t["plankton_per_s"].F(1);
    Json p = LoadJsonFile(dir + "mouthful_paths.json");
    for (const Json& r : p["forms"].a) {
        FormDef f;
        f.key = r["key"].Str0(); f.name = r["name"].Str0(f.key); f.art = r["art"].Str0(f.name);
        f.path = r["path"].IsNum() ? r["path"].I(-1) : PathKey(r["path"].Str0()); f.tier = r["tier"].I(1);
        f.speed = r["speed"].F(1); f.hp = r["hp"].F(1); f.bite = r["bite"].F(1); f.reach = r["reach"].F(0); f.walker = r["walker"].Bool0(false);
        f.ab = AbilityKey(r["ability"].Str0("none")); f.ps = PassiveKey(r["passive"].Str0("none"));
        f.cd = r["cd"].F(0); f.p1 = r["p1"].F(0); f.p2 = r["p2"].F(0);
        f.abilityText = r["ability_text"].Str0(); f.passiveText = r["passive_text"].Str0(); f.tell = r["tell"].Str0();
        f.base = Col(r["base"], f.base); f.belly = Col(r["belly"], f.belly); f.accent = Col(r["accent"], f.accent);
        d.forms.push_back(f);
    }
    if (d.forms.empty()) { FormDef f; f.key = "fry"; f.name = "Fry"; f.art = "Fry"; d.forms.push_back(f); }
    Json s = LoadJsonFile(dir + "mouthful_sea.json");
    for (const Json& r : s["prey"].a) {
        PreyDef pd; pd.species = r["species"].Str0(); pd.mass = r["mass"].F(3); pd.armored = r["armored"].Bool0(false);
        pd.npc = r["npc"].Bool0(false); pd.eatsUpTo = r["eats_up_to"].I(0); pd.bite = r["bite"].F(1);
        d.prey.push_back(pd);
    }
    Json sc = LoadJsonFile(dir + "mouthful_scoring.json");
    d.scoreMassPer = sc["mass_per_point"].F(10); d.scoreKill = sc["kill"].F(50); d.scoreKillPerTier = sc["kill_per_tier_above"].F(25);
    d.scoreCrownPerS = sc["crown_per_s"].F(10); d.scoreKingKilled = sc["king_killed"].F(300); d.scoreTierPast4 = sc["tier_past_4"].F(50);
    d.scoreApex = sc["apex_shark"].F(200); d.scoreLeviathan = sc["leviathan"].F(1000); d.scoreOrca = sc["orca_top3"].F(100);
    d.scoreWin = sc["round_won"].F(200); d.scoreBlobKing = sc["blobfish_king"].F(500);
    Json b = LoadJsonFile(dir + "mouthful_bots.json");
    for (const Json& r : b["levels"].a) { int l = r["level"].I(0); if (l < 1 || l > 3) continue; d.botSense[l] = r["sense_m"].F(d.botSense[l]); d.botAbilityUse[l] = r["ability_use"].F(d.botAbilityUse[l]); d.botHuntMouths[l] = r["hunt_mouths"].F(d.botHuntMouths[l]); }
    for (const Json& n : b["names"].a) d.botNames.push_back(n.Str0());
    if (d.botNames.empty()) d.botNames = {"Gary", "Brinefloat", "The Admiral"};
    return d;
}
const Data& D() { static Data d = LoadData(); return d; }
int FormIndex(const std::string& key) { const auto& f = D().forms; for (int i = 0; i < (int)f.size(); i++) if (f[i].key == key) return i; return -1; }
const PreyDef* PreyOf(const std::string& sp) { for (const auto& p : D().prey) if (p.species == sp) return &p; return nullptr; }
std::vector<int> ForkChoices(int path, int tier, int fromForm) {
    std::vector<int> v;
    const auto& F = D().forms;
    if (tier == 2) {   // the first fork: each path's first form (the path you died on is offered first: a free re-pick)
        for (int p = 0; p < P_COUNT; p++) for (int i = 0; i < (int)F.size(); i++) if (F[i].path == p && F[i].tier == 2) { v.push_back(i); break; }
        if (path >= 0) for (int k = 0; k < (int)v.size(); k++) if (F[v[k]].path == path) { std::swap(v[0], v[k]); if (k > 1) std::rotate(v.begin() + 1, v.begin() + k, v.begin() + k + 1); break; }
        return v;
    }
    for (int i = 0; i < (int)F.size(); i++) if (F[i].path == path && F[i].tier == tier) v.push_back(i);
    (void)fromForm;
    return v;
}

// ---------------------------------------------------------------- the arena
static float Hash2(int x, int z) { uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u; h = (h ^ (h >> 13)) * 1274126177u; return ((h ^ (h >> 16)) & 0xffffff) / 16777215.0f; }
static float VNoise(float x, float z) {
    int xi = (int)floorf(x), zi = (int)floorf(z); float fx = x - xi, fz = z - zi;
    fx = fx * fx * (3 - 2 * fx); fz = fz * fz * (3 - 2 * fz);
    float a = Hash2(xi, zi), b = Hash2(xi + 1, zi), c = Hash2(xi, zi + 1), d = Hash2(xi + 1, zi + 1);
    return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fz;
}
static float Smooth(float t) { t = std::clamp(t, 0.0f, 1.0f); return t * t * (3 - 2 * t); }
float FloorY(float x, float z) {
    // the shelf from west to east (doc p. 2): the shallows' sand (3-15 m), the reef's coral floor (15-40 m), the wall's
    // drop to the blue's floor at 120 m, and past it the trench's canyon down to 300 m (|z| < ~80)
    float y;
    if (x < -170) y = -3 - 12 * Smooth((x + 300) / 130);
    else if (x < -42) y = -15 - 25 * Smooth((x + 170) / 128);
    else if (x < -8) y = -40 - 80 * Smooth((x + 42) / 34);
    else y = -120;
    // the lagoon: a bowl in the shallows
    { float dx = x + 255, dz = z - 70, r2 = (dx * dx + dz * dz) / (28.0f * 28.0f); if (r2 < 1) y -= 5 * (1 - r2) * (1 - r2); }
    // relief: seagrass ripples in the shallows, coral bumps on the reef, dunes in the blue
    float n = VNoise(x * 0.06f, z * 0.06f) * 0.6f + VNoise(x * 0.17f + 9, z * 0.17f) * 0.4f;
    if (x < -170) y += (n - 0.5f) * 1.6f;
    else if (x < -42) y += (n - 0.5f) * 4.5f;
    else if (x >= -8) y += (n - 0.5f) * 5.0f;
    // the trench: a canyon east of the blue
    if (x > 120) {
        float ex = Smooth((x - 120) / 40), ez = 1 - Smooth((fabsf(z) - 55) / 40);
        float k = ex * ez;
        float deep = -300 + (VNoise(x * 0.05f, z * 0.05f) - 0.5f) * 10;
        y = y + (deep - y) * k;
    }
    // the leviathan's hollow: a deeper bowl
    { float dx = x - HOLLOW.x, dz = z - HOLLOW.z, r2 = (dx * dx + dz * dz) / (30.0f * 30.0f); if (r2 < 1) y -= 8 * (1 - r2); }
    return y;
}
int BandAt(Vector3 p) {
    if (p.x > 125 && fabsf(p.z) < 95 && p.y < -110) return B_TRENCH;
    if (p.x > 125 && p.y < -120) return B_TRENCH;
    if (p.x < -170) return B_SHALLOWS;
    if (p.x < -42) return B_REEF;
    if (p.x < 6 && p.y < -38) return B_WALL;
    return B_BLUE;
}
float LightAt(Vector3 p, float dusk) {
    float d = std::max(0.0f, -p.y);
    float l = d < 40 ? 1 - d / 40 * 0.25f : d < 120 ? 0.75f - (d - 40) / 80 * 0.45f : std::max(0.05f, 0.3f - (d - 120) / 180 * 0.27f);
    return l * (1 - 0.45f * std::clamp(dusk, 0.0f, 1.0f));
}
Vector3 CurrentAt(Vector3 p) {
    Vector3 c{0, 0, 0};
    if (p.x < -170 && p.y > -6) c.x += 1.0f;                                 // the shallows' surface current runs east
    if (p.x > -36 && p.x < -14 && p.y < -20 && p.y > -118) c.y += 2.0f;       // the upwelling off the wall
    if (BandAt(p) == B_TRENCH) c.y -= 0.6f;                                   // the trench's slow downdraft
    return c;
}
static std::vector<Hole> MakeHoles() {
    std::vector<Hole> v;
    for (int i = 0; i < 34; i++) { float x = -165 + Hash2(i, 1) * 120, z = -140 + Hash2(i, 2) * 280; v.push_back({{x, FloorY(x, z) + 0.4f, z}, 0.9f, 2}); }
    for (int i = 0; i < 10; i++) { float x = -280 + Hash2(i, 3) * 100, z = -140 + Hash2(i, 4) * 280; v.push_back({{x, FloorY(x, z) + 0.3f, z}, 0.8f, 2}); }
    for (int i = 0; i < 12; i++) {   // wall caves: in the wall's face, up to tier 5
        float y = -55 - Hash2(i, 5) * 55, z = -140 + i * 24 + Hash2(i, 6) * 10;
        float x = -42; for (int k = 0; k < 60; k++) { if (FloorY(x, z) < y) break; x += 0.6f; }
        v.push_back({{x - 1.5f, y, z}, 3.2f, 5});
    }
    for (int i = 0; i < 6; i++) { float x = 140 + Hash2(i, 7) * 140, z = -60 + Hash2(i, 8) * 120; v.push_back({{x, FloorY(x, z) + 0.5f, z}, 1.6f, 4}); }   // trench vents' crevices
    return v;
}
const std::vector<Hole>& Holes() { static std::vector<Hole> v = MakeHoles(); return v; }
static std::vector<Coral> MakeCorals() {
    // coral heads on the reef in clusters, the gaps between them the corridors only the small get through
    std::vector<Coral> v;
    for (int c = 0; c < 14; c++) {
        float cx = -160 + Hash2(c, 11) * 110, cz = -130 + Hash2(c, 12) * 260;
        int n = 4 + (int)(Hash2(c, 13) * 4);
        for (int k = 0; k < n; k++) {
            float a = k * 2.4f + c, r = 3.2f + Hash2(c, k + 20) * 4;
            float x = cx + cosf(a) * r, z = cz + sinf(a) * r;
            v.push_back({{x, FloorY(x, z), z}, 1.1f + Hash2(c, k + 40) * 1.2f, 3 + Hash2(c, k + 60) * 6});
        }
    }
    return v;
}
const std::vector<Coral>& Corals() { static std::vector<Coral> v = MakeCorals(); return v; }

// ---------------------------------------------------------------- the round
float World::Rand() { rng = rng * 1664525u + 1013904223u; return ((rng >> 8) & 0xffffff) / 16777216.0f; }
void World::Say(const std::string& s, Color c) { feed.push_back({s, time, c}); if (feed.size() > 60) feed.erase(feed.begin()); }
float Mouth::bodyR() const { return 0.05f; }
const FormDef& World::FormOf(const Mouth& m) const { const auto& F = D().forms; return F[std::clamp(m.form, 0, (int)F.size() - 1)]; }
int World::TierOfMass(float mass) const { int t = 1; for (int k = 2; k <= 8; k++) if (mass >= D().tiers[k].mass) t = k; return t; }
static float StreakK(const Mouth& m) { return m.streak >= 3 && m.path >= 0 && m.path == m.lastPath ? 1 + D().streakBonus : 1.0f; }
float World::Speed(const Mouth& m) const { return D().tiers[m.tier].speed * FormOf(m).speed * (m.king ? 1 + D().kingBonus : 1) * StreakK(m); }
float World::BiteMul(const Mouth& m) const { return FormOf(m).bite * (m.king ? 1 + D().kingBonus : 1) * StreakK(m) * (m.frenzyT > 0 ? 1 + m.frenzyK : 1); }
float World::HpMul(const Mouth& m) const { return FormOf(m).hp * (m.king ? 1 + D().kingBonus : 1) * StreakK(m); }
float World::Length(const Mouth& m) const {
    const Data& d = D();
    int t = TierOfMass(m.mass);
    float a = d.tiers[t].length, b = t < 8 ? d.tiers[t + 1].length : d.tiers[8].length * 1.3f;
    float m0 = d.tiers[t].mass, m1 = t < 8 ? d.tiers[t + 1].mass : d.tiers[8].mass * 2;
    float k = std::clamp((m.mass - m0) / std::max(1.0f, m1 - m0), 0.0f, 1.0f);
    float L = a + (b - a) * k;
    if (m.abT > 0 && FormOf(m).ab == AB_INFLATE) L *= 1.6f;   // (inflated: double size)
    return L;
}
float World::Reach(const Mouth& m) const {
    float L = Length(m);
    const FormDef& F = FormOf(m);
    if (F.reach > 0) return F.reach * (0.25f + 0.5f * L);   // (a crustacean's claw out-reaches any bite)
    return 0.3f + 0.6f * L;
}
static float BodyRadius(float length) { return 0.04f + 0.16f * length; }

static Vector3 Fwd(float yaw, float pitch) { return {cosf(pitch) * cosf(yaw), sinf(pitch), cosf(pitch) * sinf(yaw)}; }

static bool Touch(Rectangle a, Rectangle b, float pad) { return a.x <= b.x + b.width + pad && b.x <= a.x + a.width + pad && a.y <= b.y + b.height + pad && b.y <= a.y + a.height + pad; }

void World::Init(const Opts& o) {
    opts = o; rng = o.seed ? o.seed : 1; time = 0; roundLen = std::max(1.0f, o.minutes) * 60; over = false; winner = -1; king = -1;
    if (o.mode == M_TRENCH_RUSH) roundLen = 8 * 60;
    if (o.mode == M_SOLO_TANK) roundLen = std::max(roundLen, 25.0f * 60);
    mouths.clear(); clouds.clear(); feed.clear(); plankton.clear();
    firstKingT = -1; crownsChanged = 0; for (int& d : deathsBy) d = 0; for (int& t : bestTierByPath) t = 0;
    levAwakeT = 0; levNoise = 0; levAte = false;
    boat = Boat{}; bloom = Bloom{}; fall = WhaleFall{}; orcas = OrcaPod{}; duskDone = false;
    // the sea: Red Tide's species and web (sea/reef/), the five bands as zones, the spawn rows from mouthful_sea.json
    const rt::MapData& base = rt::Map("mouthful_reef");
    sea = std::make_unique<rt::MapData>();
    rt::MapData& m = *sea;
    m.key = base.key; m.title = "Mouthful's reef"; m.species = base.species; m.attacks = base.attacks; m.diet = base.diet; m.foodNames = base.foodNames;
    m.tunables = base.tunables; m.tides = base.tides; m.extra = base.extra; m.faction = base.faction; m.enemySpecies = base.enemySpecies;
    auto zone = [&](const char* name, float x0, float x1, float z0, float z1, float y0, float y1) { rt::Zone z; z.name = name; z.plan = {x0, z0, x1 - x0, z1 - z0}; z.y0 = y0; z.y1 = y1; m.zones.push_back(z); };
    zone("The Shallows", X0, -170, Z0, Z1, -16, -0.5f);
    zone("The Reef", -170, -42, Z0, Z1, -42, -0.5f);
    zone("The Wall", -42, 6, Z0, Z1, -120, -38);
    zone("The Blue", 6, 125, Z0, Z1, -118, -0.5f);
    zone("The Trench", 125, X1, -95, 95, -300, -110);
    Json sj = LoadJsonFile(rt::DataDir() + "/../mouthful/mouthful_sea.json");
    std::map<std::string, float> respawn;
    for (const Json& r : sj["spawns"].a) {
        rt::SpawnRow row; row.zone = r["zone"].Str0(); row.species = r["species"].Str0(); row.count = r["count"].I(0); row.respawnS = 0;   // (the World revives the dead in place: see Step)
        if (m.SpeciesIndex(row.species) < 0 || m.ZoneIndex(row.zone) < 0 || row.count <= 0) continue;
        m.spawns.push_back(row);
        float rs = r["respawn_s"].F(20); auto it = respawn.find(row.species); respawn[row.species] = it == respawn.end() ? rs : std::min(it->second, rs);
    }
    for (int a = 0; a < (int)m.zones.size(); a++) for (int b = a + 1; b < (int)m.zones.size(); b++)
        if (Touch(m.zones[a].plan, m.zones[b].plan, 0.5f)) { rt::Link l; l.from = a; l.to = b; l.passage = "open water"; m.links.push_back(l); }
    rt::RebuildMapGeometry(m);
    eco = rt::Ecosystem{};
    eco.Init(m, rng, 1, 1);
    eco.decideHook = [this](rt::Agent& a, int idx) { return DecideHook(a, idx); };
    eco.onDiverHit = [this](int d, int at, float dmg) { OnDiverHit(d, at, dmg); };
    preyMass.assign(m.species.size(), 0); npcEats.assign(m.species.size(), 0); reviveS.assign(m.species.size(), 0);
    for (int i = 0; i < (int)m.species.size(); i++) {
        const PreyDef* p = PreyOf(m.species[i].name);
        preyMass[i] = p ? p->mass : 2.0f * m.species[i].size * m.species[i].size;
        npcEats[i] = p && p->npc ? p->eatsUpTo : 0;
        auto it = respawn.find(m.species[i].name); reviveS[i] = it == respawn.end() ? 0 : it->second;
    }
    leviathan = -1;
    for (int i = 0; i < (int)eco.agents.size(); i++) {
        rt::Agent& a = eco.agents[i];
        float fy = FloorY(a.pos.x, a.pos.z);
        if (a.pos.y < fy + 0.6f) a.pos.y = fy + 0.6f + Rand() * 3;
        if (a.home.y < fy + 0.6f) a.home.y = fy + 0.6f + Rand() * 3;
        if (m.species[a.sp].name == "Leviathan") { leviathan = i; a.pos = a.home = Vector3Add(HOLLOW, {0, 6, 0}); }
    }
    deadT.assign(eco.agents.size(), 0);
    // the round's clock (doc p. 13, scaled to its length): the orca pod at a random minute 5-10, dusk at 10:00, the
    // whale fall at 11:00, the red tide somewhere in the middle; the first boat a minute or so in
    duskAt = roundLen * 10 / 15; fallAt = roundLen * 11 / 15;
    orcas.at = roundLen * (5 + 5 * Rand()) / 15; bloomAt = roundLen * (4 + 4 * Rand()) / 15;
    boat.nextT = 50 + Rand() * 40;
    // plankton clouds: a fry's first meal (1 mass a second inside one), drifting over the shallows and the reef's edge
    for (int i = 0; i < 18; i++) {
        float x = -290 + Rand() * (i < 12 ? 120 : 220), z = Z0 + 10 + Rand() * (Z1 - Z0 - 20);
        float y = std::max(FloorY(x, z) + 2, -1.5f - Rand() * 8);
        plankton.push_back({{x, y, z}, 4.5f + Rand() * 2.5f, {0.2f + Rand() * 0.3f, 0, (Rand() - 0.5f) * 0.4f}});
    }
    // the mouths: the people first, then bots to fill
    int humans = std::clamp(o.humans, 0, 12), bots = std::clamp(o.bots, 0, 12 - humans);
    for (int i = 0; i < humans; i++) AddMouth(i == 0 ? "You" : TextFormat("Player %d", i + 1), false, 0, i);
    std::vector<int> names(D().botNames.size()); for (int i = 0; i < (int)names.size(); i++) names[i] = i;
    for (int i = (int)names.size() - 1; i > 0; i--) std::swap(names[i], names[(int)(Rand() * (i + 1)) % (i + 1)]);
    for (int i = 0; i < bots; i++) {
        int lvl = o.botLevel > 0 ? o.botLevel : 1 + (int)(Rand() * 3) % 3;
        AddMouth(D().botNames[names[i % names.size()]], true, lvl, -1);
    }
    if (o.mode == M_FOOD_CHAIN) {   // teams of three, each on its own path
        static const int TP[5] = {P_SHARK, P_EEL, P_CEPH, P_PUFFER, P_CRUST};
        int start = (int)(Rand() * 5);
        for (int k = 0; k < 4; k++) teamPath[k] = TP[(start + k) % 5];
        for (auto& m : mouths) m.team = m.id % 4;
    }
}
int World::AddMouth(const std::string& name, bool bot, int level, int seat) {
    Mouth m; m.id = (int)mouths.size(); m.name = name; m.bot = bot; m.botLevel = std::clamp(level, 1, 3); m.seat = seat;
    m.agent = eco.AddDiver(m.id, {-250, -5, 0});
    m.thinkT = Rand() * 0.2f;
    if (bot && Rand() < 0.35f) {   // (some bots wear a skin or two from the crate's commons and rares)
        std::vector<const SkinDef*> pool; for (const auto& s : Skins()) if (s.tier >= 1 && s.tier <= 2) pool.push_back(&s);
        for (int p = 0; p < P_COUNT && !pool.empty(); p++) { if (p) m.look += ";"; if (Rand() < 0.6f) m.look += pool[(int)(Rand() * pool.size()) % pool.size()]->id; }
    }
    mouths.push_back(m);
    Respawn(mouths.back(), true);
    return m.id;
}
void World::Respawn(Mouth& m, bool first) {
    // a fry in the shallows with a 10 s immunity glow, 15 mass and its path memory (doc p. 10); a late joiner starts with
    // the lowest living mouth's mass, capped at tier 3 (p. 13)
    if (m.out) { m.alive = false; m.respawnT = 1e9f; return; }
    float mass = D().startMass;
    if (first && time > 5) { float lo = 1e9f; for (const auto& o : mouths) if (o.alive && o.id != m.id) lo = std::min(lo, o.mass); if (lo < 1e8f) mass = std::clamp(lo, D().startMass, D().tiers[4].mass - 1); }
    m.alive = true; m.mass = mass; m.tier = TierOfMass(mass); m.form = 0; m.path = -1;
    m.pos = {-285 + Rand() * 70, -2 - Rand() * 4, Z0 + 20 + Rand() * (Z1 - Z0 - 40)};
    m.pos.y = std::max(m.pos.y, FloorY(m.pos.x, m.pos.z) + 1);
    if (opts.mode == M_TRENCH_RUSH) { m.mass = D().tiers[4].mass + 10; m.tier = 4; m.pos = {Rand(150, 280), 0, Rand(-50, 50)}; m.pos.y = FloorY(m.pos.x, m.pos.z) + 8 + Rand() * 40; }
    m.vel = {0, 0, 0}; m.yaw = Rand() * 6.28f; m.pitch = 0;
    m.stamina = 1; m.biteCd = m.abCd = m.abT = 0; m.immuneT = D().immuneS; m.swallowT = m.stunT = m.blindT = m.reverseT = m.poisonT = m.bleedT = m.holdT = m.jetT = m.frenzyT = 0;
    m.dashT = 0; m.dashLeft = 0; m.ambush = m.hidden = m.airborne = false; m.pendingFork = 0; m.forkOpts.clear();
    m.king = false; m.tgtMouth = m.tgtAgent = -1; m.fleeing = false; m.markT = 0; m.lastHurtBy = -1;
    if (m.agent >= 0) { rt::Agent& a = eco.agents[m.agent]; a.alive = true; a.pos = m.pos; a.vel = {0, 0, 0}; a.zone = std::max(0, eco.ZoneAt(m.pos)); }
    GrowCheck(m);
}
float World::MassOfAgent(int ai) const {
    if (ai < 0 || ai >= (int)eco.agents.size()) return 0;
    const rt::Agent& a = eco.agents[ai];
    if (a.diver >= 0) { int mi = MouthOfAgent(ai); return mi >= 0 ? mouths[mi].mass : 0; }
    return preyMass[a.sp] * std::clamp(a.hp / std::max(0.01f, a.hpMax), 0.05f, 1.0f);
}
int World::MouthOfAgent(int ai) const {
    if (ai < 0 || ai >= (int)eco.agents.size() || eco.agents[ai].diver < 0) return -1;
    int d = eco.agents[ai].diver;
    return d >= 0 && d < (int)mouths.size() ? d : -1;
}
static float AgentRadius(const rt::MapData& m, int sp) {
    static std::map<int, float> cache;
    auto it = cache.find(sp); if (it != cache.end()) return it->second;
    rt::Body b = rt::BodyOf(m.key, m.species[sp].name);
    float r = std::max(b.radius, b.length * 0.25f);
    if (m.species[sp].name == "Leviathan") r = 6;
    cache[sp] = r; return r;
}

void World::Feed(Mouth& m, float mass, bool kill) {
    if (mass <= 0) return;
    if (m.bot && opts.humans > 0 && king < 0) mass = std::min(mass, std::max(0.0f, D().tiers[8].mass - 10 - m.mass));   // (a bot never takes the crown from the people: it throttles itself, doc p. 19)
    m.mass += mass; m.massEaten += mass;
    if (kill && opts.mode == M_FOOD_CHAIN && m.team >= 0) for (auto& o : mouths) if (o.alive && o.id != m.id && o.team == m.team && Vector3Distance(o.pos, m.pos) < 25) { o.mass += mass * 0.25f; o.massEaten += mass * 0.25f; GrowCheck(o); }   // (the team eats together)
    GrowCheck(m);
}
float World::TeamScore(int team) const { float s = 0; for (const auto& m : mouths) if (m.team == team) s += ScoreOf(*this, m); return s; }
void World::GrowCheck(Mouth& m) {
    int t = TierOfMass(m.mass);
    m.tier = t;
    m.bestTier = std::max(m.bestTier, t);
    if (m.path >= 0) bestTierByPath[m.path] = std::max(bestTierByPath[m.path], t);
    // a fork wants picking: tiers 2, 4 and 6 (the blobfish's crown at 8 comes by itself)
    int ft = FormOf(m).tier;
    int need = ft == 1 ? 2 : ft == 2 ? 4 : ft == 4 ? 6 : 0;
    if (FormOf(m).path == P_BLOB && ft == 6 && t >= 8) { int k = FormIndex("blob_king"); if (k >= 0) { m.form = k; m.morphT = 1; if (!m.blobKing) { m.blobKing = true; m.score += D().scoreBlobKing; Say(m.name + " is the Blobfish King. Nobody knows what to do.", Color{255, 200, 220, 255}); } } }
    if (need && t >= need && m.pendingFork == 0) {
        m.forkOpts = ForkChoices(need == 2 ? m.lastPath : m.path, need, m.form);
        // the modes' restrictions on the first fork: everyone a blobfish, one path for all, a team's shared path
        int only = opts.mode == M_BLOBFISH_ONLY ? P_BLOB : opts.mode == M_ONE_PATH ? opts.path : opts.mode == M_FOOD_CHAIN && m.team >= 0 ? teamPath[m.team % 4] : -1;
        if (need == 2 && only >= 0) { std::vector<int> v; for (int fi : m.forkOpts) if (D().forms[fi].path == only) v.push_back(fi); if (!v.empty()) m.forkOpts = v; }
        if (m.forkOpts.size() == 1 || (FormOf(m).path == P_BLOB)) { PickFork(m, 0); return; }
        if (!m.forkOpts.empty()) { m.pendingFork = need; m.forkT = 10; }
        if (m.bot) {
            // a bot's pick: its old path most of the time; the blobfish rarely, because someone has to
            int pick = 0;
            if (need == 2) {
                std::vector<float> w; for (int fi : m.forkOpts) { int p = D().forms[fi].path; w.push_back(p == P_BLOB ? 0.15f : p == m.lastPath ? 3.0f : 1.0f); }
                float tot = 0; for (float x : w) tot += x; float r = Rand() * tot; for (int k = 0; k < (int)w.size(); k++) { r -= w[k]; if (r <= 0) { pick = k; break; } }
            } else pick = (int)(Rand() * m.forkOpts.size()) % (int)m.forkOpts.size();
            PickFork(m, pick);
        }
    }
    if (opts.mode == M_BLOBFISH_ONLY && t >= 6 && !over && m.alive) { over = true; winner = m.id; m.score += D().scoreWin; Say(m.name + " reached tier 6 first, which is a sentence nobody expected to hear.", Color{255, 200, 220, 255}); return; }
    // the crown: the first mouth to tier 8
    if (t >= 8 && king < 0 && m.alive && !(opts.mode == M_KING_OF_REEF && m.out)) {
        king = m.id; m.king = true; crownsChanged++;
        if (firstKingT < 0) firstKingT = time;
        Say(m.name + " takes the crown!", Color{255, 220, 110, 255});
    }
}
void World::PickFork(Mouth& m, int choice) {
    if (m.forkOpts.empty()) { m.pendingFork = 0; return; }
    choice = std::clamp(choice, 0, (int)m.forkOpts.size() - 1);
    int fi = m.forkOpts[choice];
    const FormDef& F = D().forms[fi];
    if (F.tier == 2) {   // the path is chosen for this life; a third life on one path is a streak (+5%)
        m.streak = F.path == m.lastPath ? m.streak + 1 : 1;
        m.path = F.path;
    }
    m.form = fi; m.morphT = 1; m.pendingFork = 0; m.forkOpts.clear(); m.abCd = 0;
    bestTierByPath[m.path >= 0 ? m.path : 0] = std::max(bestTierByPath[m.path >= 0 ? m.path : 0], m.tier);
    if (!m.bot) Say("You are a " + F.name + ".", Color{180, 240, 220, 255});
    GrowCheck(m);
}
void World::Hurt(Mouth& m, float mass, int byMouth, int byAgent, const char* cause) {
    if (!m.alive || mass <= 0) return;
    if (m.immuneT > 0) return;
    if (m.abT > 0 && FormOf(m).ab == AB_ROLL) return;
    m.mass -= mass; m.hurtT = 0.35f;
    if (byMouth >= 0) m.lastHurtBy = byMouth;
    if (m.mass < 3) { KillMouth(m, byMouth >= 0 ? byMouth : m.lastHurtBy, byAgent, cause); return; }
    m.tier = TierOfMass(m.mass);
}
void World::KillMouth(Mouth& m, int byMouth, int byAgent, const char* cause) {
    if (!m.alive) return;
    m.alive = false; m.respawnT = D().respawnS; m.deaths++;
    m.lastPath = m.path >= 0 ? m.path : m.lastPath;
    m.lastCause = cause;
    if (m.agent >= 0) eco.agents[m.agent].alive = false;
    eco.AddBlood(m.pos, 4 + m.mass * 0.02f);
    std::string c = cause;
    int kind = DeathKind(c);
    deathsBy[kind]++;
    if (kind == 3) levAte = true;
    Mouth* k = byMouth >= 0 && byMouth < (int)mouths.size() && byMouth != m.id ? &mouths[byMouth] : nullptr;
    if (k && kind == 0) {
        k->kills++;
        k->score += D().scoreKill + D().scoreKillPerTier * std::max(0, m.tier - k->tier);
    }
    std::string by = k && kind == 0 ? k->name : byAgent >= 0 && byAgent < (int)eco.agents.size() ? "a " + eco.map->species[eco.agents[byAgent].sp].name : kind == 2 ? std::string(cause) : "the sea";
    if (kind == 3) by = "the Leviathan";
    Say(by + " ate " + m.name + (m.tier >= 4 ? TextFormat(" (tier %d)", m.tier) : ""), m.bot ? Color{220, 220, 220, 255} : Color{255, 170, 150, 255});
    if (m.king && opts.mode == M_KING_OF_REEF) { m.out = true; Say(m.name + " has lost the crown, and is out.", Color{255, 200, 150, 255}); }
    if (m.king) {
        m.king = false; king = -1;
        if (k && k->alive && kind == 0) { k->score += D().scoreKingKilled; king = k->id; k->king = true; crownsChanged++; Say(k->name + " killed the king and takes the crown!", Color{255, 220, 110, 255}); }
        else Say("The king is dead. The crown is free.", Color{255, 220, 110, 255});
    }
    m.path = -1;
}

// ---------------------------------------------------------------- biting
static bool InFront(Vector3 from, Vector3 fwd, Vector3 at, float reach, float rad, float cosA, float* dOut) {
    Vector3 d = Vector3Subtract(at, from);
    float L = Vector3Length(d);
    if (L - rad > reach) return false;
    if (L > 0.05f && Vector3DotProduct(Vector3Scale(d, 1 / L), fwd) < cosA && L > rad + 0.2f) return false;
    *dOut = L - rad;
    return true;
}
bool SwallowOk(const World& w, const Mouth& by, const Mouth& t) {
    const FormDef& F = w.FormOf(t);
    if (F.ab == AB_INFLATE && t.abT > 0 && by.tier < 5) return false;
    if (F.ps == PS_UNTOUCHABLE && by.tier < 7) return false;
    return t.mass < D().swallowBelow * by.mass;
}
void World::Bite(Mouth& m, bool free) {
    if (!m.alive || m.swallowT > 0 || m.stunT > 0) return;
    if (!free && m.biteCd > 0) return;
    const Data& d = D();
    const FormDef& F = FormOf(m);
    m.biteCd = d.tiers[m.tier].biteCd * (m.frenzyT > 0 ? 0.6f : 1.0f);
    m.biteAnim = 0.3f;
    Vector3 fwd = Fwd(m.yaw, m.pitch);
    Vector3 mouthAt = m.pos;   // (measured from the body's middle: the reach runs on past the snout)
    float reach = Reach(m) + Length(m) * 0.5f;
    float cosA = F.walker ? 0.2f : 0.45f;
    int bestM = -1, bestA = -1, bestC = -1; float bd = 1e9f, dd;
    for (auto& o : mouths) {
        if (!o.alive || o.id == m.id || Friends(m, o)) continue;
        if (o.hidden && m.tier > 3 && !(FormOf(o).ps == PS_HOLE && m.tier <= 3)) continue;   // (in a hole it fits and the biter doesn't)
        if (InFront(mouthAt, fwd, o.pos, reach, BodyRadius(Length(o)), cosA, &dd) && dd < bd) { bd = dd; bestM = o.id; }
    }
    for (int i = 0; i < (int)eco.agents.size(); i++) {
        const rt::Agent& a = eco.agents[i];
        if (!a.alive || a.diver >= 0) continue;
        if (fabsf(a.pos.x - m.pos.x) > reach + 8 || fabsf(a.pos.z - m.pos.z) > reach + 8) continue;
        if (InFront(mouthAt, fwd, a.pos, reach, AgentRadius(*eco.map, a.sp), cosA, &dd) && dd < bd) { bd = dd; bestA = i; bestM = -1; }
    }
    // a baited hook in front: its bait is a trap (held 3 s, a fifth of you); a bite on someone else's line frees them
    if (boat.on) for (auto& hk : boat.hookList) {
        if (hk.gone || hk.held == m.id) continue;
        float hd;
        if (!InFront(mouthAt, fwd, hk.pos, reach, 0.15f, cosA, &hd) || hd > bd) continue;
        if (hk.held >= 0) { mouths[hk.held].holdT = 0; Say(m.name + " bit through the line and freed " + mouths[hk.held].name + ".", Color{180, 240, 200, 255}); hk.held = -1; hk.gone = true; return; }
        hk.held = m.id; hk.heldT = 0; m.holdT = 0.5f;
        if (!m.bot) Say("HOOKED! Three seconds on the line (a friend can bite it through).", Color{255, 170, 140, 255});
        return;
    }
    // the whale fall: tear off a mouthful
    if (fall.on && Vector3Distance(m.pos, fall.pos) < reach + 4) {
        float chunk = std::min(fall.left, 8 + m.mass * 0.04f) ;
        fall.left -= chunk;
        Feed(m, chunk * (F.ps == PS_SCAVENGER ? 2 : 1), false);
        eco.AddBlood(fall.pos, 2);
        if (bestM < 0 && bestA < 0) return;
    }
    if (bestM < 0 && bestA < 0) {
        for (int i = 0; i < (int)eco.corpses.size(); i++) { const rt::Corpse& c = eco.corpses[i]; if (!c.active) continue; if (InFront(mouthAt, fwd, c.pos, reach, 0.3f, cosA, &dd) && dd < bd) { bd = dd; bestC = i; } }
        if (bestC >= 0) {   // a corpse: eaten whole (a scavenger gets double)
            rt::Corpse& c = eco.corpses[bestC];
            float got = preyMass[c.sp] * 0.6f * (F.ps == PS_SCAVENGER ? 2 : 1);
            c.active = false;
            Feed(m, got, false); m.swallowT = d.swallowS * 0.5f;
        }
        return;
    }
    bool ambush = m.ambush; m.ambush = false;
    if (m.blindT > 0 && Rand() < 0.6f) return;   // (blind: the bite mostly closes on ink)
    if (bestM >= 0) {
        Mouth& t = mouths[bestM];
        if (t.immuneT > 0) {   // a glowing fry: the bite does nothing and marks the biter for the sharks
            m.markT = 20;
            if (!m.bot) Say("You bit a glowing fry: the sharks have your scent.", Color{255, 190, 150, 255});
            return;
        }
        if (t.abT > 0 && FormOf(t).ab == AB_ROLL) return;
        // evasion: a quicker fish on the move slips some bites (a held, stunned, swallowing or still one doesn't)
        if (!ambush && t.holdT <= 0 && t.stunT <= 0 && t.swallowT <= 0 && Vector3Length(t.vel) > 1.0f) {
            float ev = 0.12f + 0.6f * (FormOf(t).speed - F.speed) + (t.dashT > 0 ? 0.3f : 0.0f) + (t.boosting ? 0.1f : 0.0f);
            if (Rand() < std::clamp(ev, 0.0f, 0.6f)) { if (duel && getenv("DEPTH_DUELTRACE")) printf("        t=%.1f %s misses %s (evaded)\n", time, F.name.c_str(), FormOf(t).name.c_str()); return; }
        }
        // the cleaning station's truce: a bite inside it marks you
        if (Vector3Distance(m.pos, CLEANING) < 10) m.markT = 25;
        Vector3 toM = Vector3Normalize(Vector3Subtract(m.pos, t.pos));
        Vector3 tf = Fwd(t.yaw, t.pitch);
        if (SwallowOk(*this, m, t) || (ambush && t.mass < 0.4f * m.mass * 2.5f && SwallowOk(*this, m, t))) {
            float got = t.mass;
            bool wasKing = t.king;
            KillMouth(t, m.id, -1, "player");
            Feed(m, got, true);
            m.swallowT = d.swallowS;
            (void)wasKing;
            return;
        }
        float dmg = d.fightPct * t.mass * BiteMul(m) / std::max(0.2f, HpMul(t));
        if (ambush) dmg = std::max(dmg, F.p1 * t.mass);
        float k = 1;
        if (m.pos.y < t.pos.y - 0.2f * Length(t) && toM.y < -0.5f) k *= 1.5f;                // (the belly: the weak point)
        else if (Vector3DotProduct(tf, toM) < -0.55f) k *= 0.5f;                            // (the tail)
        const FormDef& T = FormOf(t);
        if (T.ps == PS_THICK_SKIN) k *= 0.8f;
        if (T.ps == PS_PLATES) k *= 0.7f;
        if (T.ps == PS_FORTRESS && Vector3DotProduct(tf, toM) > 0.5f) k *= 0.6f;
        if (T.ps == PS_ARMOR_BACK && toM.y > 0.5f) k *= 0.5f;
        dmg *= k;
        float before = t.mass;
        Hurt(t, dmg, m.id, -1, "player");
        Feed(m, std::min(dmg, before) * 0.3f, false);   // (a fight bite is mostly blood in the water: the meal is the swallow)
        eco.AddBlood(t.pos, 1 + dmg * 0.05f);
        if (BandAt(t.pos) == B_TRENCH) levNoise += 1;
        if (t.alive) {
            // knocked back, and 5% of the boost lost (doc p. 10)
            t.vel = Vector3Add(t.vel, Vector3Scale(Vector3Negate(toM), 3));
            t.stamina = std::max(0.0f, t.stamina - 0.05f);
            if (F.ps == PS_VENOM) t.poisonT = std::max(t.poisonT, 3.0f);
            if (F.ps == PS_DRAG && t.mass < m.mass) t.pos = Vector3Add(t.pos, Vector3Scale(toM, 3));
            if (F.ps == PS_EIGHT_ARMS) t.holdT = std::max(t.holdT, 1.5f);
            if (F.ps == PS_CAMO) t.holdT = std::max(t.holdT, 1.0f);
        }
        // the target's payback: spines, inflation, barbs, venom (a dash-bite or a belly-bite is in and out before they matter: the eel's answer)
        float pay = 0;
        bool inAndOut = m.dashT > 0 || m.sinceDash < 0.35f || (m.pos.y < t.pos.y - 0.2f * Length(t) && toM.y < -0.5f);
        if (inAndOut && F.path == P_EEL) { pay = -1; }
        if (pay >= 0) {
            if (T.ps == PS_SPINES) pay += 0.03f * m.mass;
            if (T.ps == PS_SPINES_BACK) pay += 0.025f * m.mass;
            if (T.ab == AB_INFLATE && t.abT > 0) pay += T.p2 * m.mass;
            if (T.ps == PS_VENOM_SPINES) { pay += 0.10f * m.mass; m.poisonT = std::max(m.poisonT, 5.0f); }
            if (T.ps == PS_BARBED) m.bleedT = std::max(m.bleedT, 3.0f);
        }
        if (duel && getenv("DEPTH_DUELTRACE")) printf("        t=%.1f %s bites %s: %.1f (k %.2f) pay %.1f%s\n", time, FormOf(m).name.c_str(), FormOf(t).name.c_str(), dmg, k, pay, inAndOut ? " in-and-out" : "");
        if (pay > 0) { Hurt(m, pay, t.id, -1, "player"); if (m.bot && (F.path == P_EEL || m.botLevel >= 3)) m.retreatT = 0.8f; }
        return;
    }
    // a fish of the web
    rt::Agent& a = eco.agents[bestA];
    const rt::Species& sp = eco.map->species[a.sp];
    const PreyDef* pd = PreyOf(sp.name);
    bool armored = pd && pd->armored;
    if (armored && F.path != P_CRUST && F.ps != PS_IRON_STOMACH && F.ps != PS_CRUSHER) Hurt(m, std::min(d.armoredCost, m.mass * 0.2f), -1, bestA, "teeth");   // (others break teeth)
    if (!m.alive) return;
    float pm = MassOfAgent(bestA);
    bool npc = pd && pd->npc;
    if (pm < d.swallowBelow * m.mass || (ambush && pm < m.mass)) {
        eco.Kill(bestA, m.agent, true);
        if (!eco.corpses.empty() && Vector3Distance(eco.corpses.back().pos, a.pos) < 0.01f) eco.corpses.back().active = false;   // (swallowed whole: no corpse)
        Feed(m, pm, true);
        m.swallowT = std::min(d.swallowS, 0.25f + pm / std::max(1.0f, m.mass) * 2);
        if (npc) {
            if (sp.name == "Leviathan") { m.score += d.scoreLeviathan; m.leviathanKill = true; Say(m.name + " ATE THE LEVIATHAN.", Color{255, 230, 120, 255}); }
            else if (pd->eatsUpTo >= 6) { m.score += d.scoreApex; m.apexKills++; Say(m.name + " ate a " + sp.name + ".", Color{200, 230, 255, 255}); }
        }
        return;
    }
    // a fight: 10% of its mass a bite, x our bite
    float frac = d.fightPct * BiteMul(m) * (ambush ? 4 : 1);
    float dmgHp = a.hpMax * frac;
    float gained = pm * frac * 0.5f;
    eco.Damage(bestA, dmgHp, m.agent, true);
    if (bestA == leviathan && a.hp < a.hpMax * 0.9f && !m.levTooth) { m.levTooth = true; Say(m.name + " has drawn a tenth of the Leviathan's blood.", Color{255, 150, 130, 255}); }
    eco.AddBlood(a.pos, 1.5f);
    if (BandAt(a.pos) == B_TRENCH) levNoise += 1;
    Feed(m, gained, false);
    if (!a.alive) {   // finished: what's left is eaten
        if (!eco.corpses.empty()) eco.corpses.back().active = false;
        Feed(m, pm * 0.5f, true);
        if (npc && pd->eatsUpTo >= 6) { m.score += d.scoreApex; m.apexKills++; Say(m.name + " killed a " + sp.name + ".", Color{200, 230, 255, 255}); }
    } else if (sp.name == "Tuna" || sp.name == "Barracuda" || npc) {
        // they bite back
        float back = (npc ? 0.05f : 0.03f) * m.mass;
        if (!(m.abT > 0 && F.ab == AB_ROLL)) Hurt(m, back, -1, bestA, npc ? "npc" : "teeth");
    }
}

// ---------------------------------------------------------------- abilities (one per form, on a cooldown; doc pp. 6-9)
static int NearestMouthInFront(World& w, const Mouth& m, float range, float cosA) {
    Vector3 f = Fwd(m.yaw, m.pitch); int best = -1; float bd = range;
    for (const auto& o : w.mouths) {
        if (!o.alive || o.id == m.id || o.immuneT > 0) continue;
        Vector3 d = Vector3Subtract(o.pos, m.pos); float L = Vector3Length(d);
        if (L > bd || L < 0.01f || Vector3DotProduct(Vector3Scale(d, 1 / L), f) < cosA) continue;
        bd = L; best = o.id;
    }
    return best;
}
static int NearestAgentInFront(World& w, const Mouth& m, float range, float cosA) {
    Vector3 f = Fwd(m.yaw, m.pitch); int best = -1; float bd = range;
    for (int i = 0; i < (int)w.eco.agents.size(); i++) {
        const rt::Agent& a = w.eco.agents[i];
        if (!a.alive || a.diver >= 0 || i == w.leviathan) continue;
        if (fabsf(a.pos.x - m.pos.x) > range || fabsf(a.pos.z - m.pos.z) > range) continue;
        Vector3 d = Vector3Subtract(a.pos, m.pos); float L = Vector3Length(d);
        if (L > bd || L < 0.01f || Vector3DotProduct(Vector3Scale(d, 1 / L), f) < cosA) continue;
        bd = L; best = i;
    }
    return best;
}
void World::UseAbility(Mouth& m) {
    const FormDef& F = FormOf(m);
    if (!m.alive || F.ab == AB_NONE || m.abCd > 0 || m.stunT > 0 || m.swallowT > 0) return;
    m.abCd = F.cd; m.tellT = 0.35f;
    Vector3 f = Fwd(m.yaw, m.pitch);
    float L = Length(m), scale = std::max(1.0f, L * 0.8f);   // (ranges grow a little with the body)
    switch (F.ab) {
        case AB_DASH: m.dashT = F.p2; m.dashV = Vector3Scale(f, F.p1 * scale / F.p2); m.dashKind = 0; break;
        case AB_RIBBON: m.dashT = F.p2; m.dashV = Vector3Scale(f, F.p1 * scale / F.p2); m.dashKind = 0; m.dashLeft = 2; break;
        case AB_LUNGE: m.dashT = F.p2; m.dashV = Vector3Scale(f, F.p1 * scale / F.p2); m.dashKind = 1; break;
        case AB_DEATH_ROLL: m.dashT = 0.35f; m.dashV = Vector3Scale(f, F.p1 * scale / 0.35f); m.dashKind = 2; break;
        case AB_RAM: m.dashT = F.p2; m.dashV = Vector3Scale(f, F.p1 * scale / F.p2); m.dashKind = 3; break;
        case AB_BREACH: { m.dashT = F.p2; m.dashV = Vector3Scale(Vector3Normalize(Vector3Add(f, {0, 1.5f, 0})), 10 * scale / F.p2); m.dashKind = 4; break; }
        case AB_TAIL_FLIP: m.dashT = F.p2; m.dashV = Vector3Add(Vector3Scale(f, -F.p1 * scale / F.p2), {0, 3, 0}); m.dashKind = 0; m.airborne = true; break;
        case AB_JUMP: m.dashT = F.p2; m.dashV = Vector3Scale(Vector3Normalize(Vector3Add(f, {0, 0.6f, 0})), F.p1 * scale / F.p2); m.dashKind = 0; m.airborne = true; break;
        case AB_INK: case AB_INK_WALL: {
            Vector3 at = F.ab == AB_INK_WALL ? Vector3Add(m.pos, Vector3Scale(f, -4 * scale)) : m.pos;
            float r = F.ab == AB_INK_WALL ? F.p1 * 0.5f * scale : F.p1 * 0.5f * scale + 1;
            clouds.push_back({at, r, F.p2, m.id, 0});
            eco.AddInk(at, r, F.p2);
            if (F.ps == PS_JET || F.path == P_CEPH) m.jetT = 1;
            break;
        }
        case AB_TOXIN: clouds.push_back({m.pos, F.p1 * 0.5f * scale + 1, F.p2, m.id, 1}); break;
        case AB_DAZZLE: { int t = NearestMouthInFront(*this, m, F.p1 * scale, 0.3f); if (t >= 0) mouths[t].reverseT = F.p2; break; }
        case AB_HOOK: {
            int t = NearestMouthInFront(*this, m, F.p1 * scale, 0.75f);
            Vector3 beak = Vector3Add(m.pos, Vector3Scale(f, L * 0.6f));
            if (t >= 0) { mouths[t].pos = Vector3Lerp(mouths[t].pos, beak, 0.85f); mouths[t].stunT = std::max(mouths[t].stunT, 0.4f); }
            else { int a = NearestAgentInFront(*this, m, F.p1 * scale, 0.75f); if (a >= 0) { eco.agents[a].pos = Vector3Lerp(eco.agents[a].pos, beak, 0.85f); eco.agents[a].stun = 0.5f; } }
            Bite(m, true);
            break;
        }
        case AB_FRENZY: if (m.blindT > 0) { m.abCd = 1; break; } m.frenzyT = F.p2; m.frenzyK = F.p1; break;   // (a shark can't frenzy what it can't see)
        case AB_PIN: {
            int t = NearestMouthInFront(*this, m, Reach(m) * 1.8f + 1, 0.4f);
            if (t >= 0) { Mouth& o = mouths[t]; o.holdT = std::max(o.holdT, F.p1); o.stunT = std::max(o.stunT, F.p1 * 0.5f); Bite(m, true); }
            else { int a = NearestAgentInFront(*this, m, Reach(m) * 1.8f + 1, 0.4f); if (a >= 0) { eco.agents[a].held = F.p1; Bite(m, true); } }
            break;
        }
        case AB_PUNCH: {
            int t = NearestMouthInFront(*this, m, F.p1 * scale * 0.6f + Reach(m), 0.35f);
            if (t >= 0) { Mouth& o = mouths[t]; o.stunT = std::max(o.stunT, F.p2); o.vel = Vector3Add(o.vel, Vector3Scale(f, 3)); }
            else { int a = NearestAgentInFront(*this, m, F.p1 * scale * 0.6f + Reach(m), 0.35f); if (a >= 0) { eco.agents[a].stun = F.p2; Bite(m, true); } }
            break;
        }
        case AB_CLAW_SLAM: {
            for (auto& o : mouths) {
                if (!o.alive || o.id == m.id || o.immuneT > 0) continue;
                if (Vector3Distance(o.pos, m.pos) > F.p1 * scale || o.pos.y - FloorY(o.pos.x, o.pos.z) > 4) continue;
                o.vel.y += 5; o.stunT = std::max(o.stunT, F.p2);
                Hurt(o, D().fightPct * 0.6f * o.mass * BiteMul(m) / std::max(0.2f, HpMul(o)), m.id, -1, "player");
            }
            break;
        }
        case AB_ROLL: m.abT = F.p1; break;
        case AB_INFLATE: m.abT = F.p1; break;
        case AB_DETONATE: {
            for (auto& o : mouths) { if (!o.alive || o.id == m.id || o.immuneT > 0) continue; if (Vector3Distance(o.pos, m.pos) < F.p1 * scale) { float x = F.p2 * o.mass; Hurt(o, x, m.id, -1, "player"); Feed(m, x * 0.3f, false); } }
            for (int i = 0; i < (int)eco.agents.size(); i++) { rt::Agent& a = eco.agents[i]; if (a.alive && a.diver < 0 && i != leviathan && Vector3Distance(a.pos, m.pos) < F.p1 * scale) eco.Damage(i, a.hpMax * F.p2, m.agent); }
            int t = TierOfMass(m.mass); m.mass = t > 1 ? D().tiers[t - 1].mass + (D().tiers[t].mass - D().tiers[t - 1].mass) * 0.5f : m.mass;   // (you drop a tier)
            m.tier = TierOfMass(m.mass);
            break;
        }
        case AB_AMBUSH: m.ambush = true; m.stillT = 0; break;
        default: break;
    }
    if (F.ab == AB_INK || F.ab == AB_INK_WALL || F.ab == AB_TOXIN) m.tellT = 0.25f;
}

// ---------------------------------------------------------------- the web's choices: prey flees the mouths that can eat it; the NPCs hunt them
bool World::DecideHook(rt::Agent& a, int idx) {
    if (idx == leviathan) return true;   // (the World moves the leviathan itself)
    int sp = a.sp;
    float dusk = time > roundLen * 2 / 3 ? 1 : 0;
    if (npcEats[sp] > 0) {
        // an NPC predator: a mouth it can eat, in sight (or carrying blood, or marked for biting a glowing fry or in the truce)
        const rt::Species& s = eco.map->species[sp];
        bool reef = s.name == "Reef Shark";
        if (reef && Vector3Distance(a.pos, CLEANING) < 10) return false;   // (calm at the cleaning station)
        int best = -1; float bd = 1e9f;
        for (const auto& m : mouths) {
            if (!m.alive || m.immuneT > 0 || m.tier > npcEats[sp]) continue;
            if (FormOf(m).ps == PS_APEX && s.name != "Orca") continue;   // (a Great White player is ignored by them)
            if (m.hidden || m.buriedT >= 2 || m.ambush) continue;
            if (reef && Vector3Distance(m.pos, CLEANING) < 10 && m.markT <= 0) continue;
            float d = Vector3Distance(a.pos, m.pos);
            float sense = s.sight * (0.4f + 0.6f * LightAt(m.pos, dusk)) + std::min(25.0f, eco.BloodNear(m.pos, 4) * 2);
            if (m.markT > 0) sense = 80;
            if (d > sense) continue;
            float score = d - (m.markT > 0 ? 40 : 0) - m.tier * 2;
            if (score < bd) { bd = score; best = m.id; }
        }
        if (best >= 0 && (a.hunger > 0.25f || mouths[best].markT > 0) && a.fedT <= 0) {
            a.st = rt::State::Hunt; a.target = mouths[best].agent; a.targetCorpse = false; if (a.stateT > 25) a.stateT = 0;
            return true;
        }
        return false;
    }
    // prey: bolt from a mouth that can swallow it, near and seen
    float pm = preyMass[sp];
    const rt::Species& s = eco.map->species[sp];
    if (s.fear <= 0.05f) return false;
    float r = (2.5f + 6 * s.fear) * (0.5f + 0.5f * LightAt(a.pos, dusk));
    int best = -1; float bd = r;
    for (const auto& m : mouths) {
        if (!m.alive || m.ambush || m.buriedT >= 2 || m.hidden) continue;
        if (m.mass * D().swallowBelow <= pm) continue;
        if (m.tier >= 5 && pm < 5) continue;   // (a tier-5 can't be bothered, and the plankton knows it)
        if (FormOf(m).ps == PS_CAMO && m.stillT > 1) continue;
        if (fabsf(m.pos.x - a.pos.x) > bd || fabsf(m.pos.z - a.pos.z) > bd) continue;
        float d = Vector3Distance(a.pos, m.pos);
        if (d < bd) { bd = d; best = m.id; }
    }
    if (best >= 0) { a.st = rt::State::Flee; a.target = mouths[best].agent; a.stateT = 0; return true; }
    return false;
}
void World::OnDiverHit(int diverAgent, int attacker, float) {
    int mi = MouthOfAgent(diverAgent);
    if (mi < 0 || attacker < 0 || attacker >= (int)eco.agents.size()) return;
    Mouth& m = mouths[mi];
    if (!m.alive || m.immuneT > 0) return;
    const rt::Agent& at = eco.agents[attacker];
    const rt::Species& s = eco.map->species[at.sp];
    const PreyDef* pd = PreyOf(s.name);
    float am = MassOfAgent(attacker);
    bool lev = s.name == "Leviathan";
    const char* cause = lev ? "leviathan" : pd && pd->npc ? "npc" : "teeth";
    const FormDef& F = FormOf(m);
    if (F.ps == PS_APEX && pd && pd->npc && s.name != "Orca" && !lev) return;
    if (m.abT > 0 && F.ab == AB_ROLL) return;
    bool swallow = lev || (m.mass < D().swallowBelow * am && !(F.ab == AB_INFLATE && m.abT > 0 && (pd ? pd->eatsUpTo : 0) < 5) && !(F.ps == PS_UNTOUCHABLE && (pd ? pd->eatsUpTo : 0) < 7));
    if (swallow) { KillMouth(m, -1, attacker, cause); return; }
    float k = 1;
    if (F.ps == PS_THICK_SKIN) k *= 0.8f; if (F.ps == PS_PLATES) k *= 0.7f;
    float dmg = D().fightPct * m.mass * (pd ? pd->bite : 1) / std::max(0.2f, HpMul(m)) * k;
    Hurt(m, dmg, -1, attacker, cause);
    if (m.alive) {
        // the payback, on an NPC: spines and inflation hurt it
        if (F.ps == PS_SPINES || F.ps == PS_VENOM_SPINES || (F.ab == AB_INFLATE && m.abT > 0)) eco.Damage(attacker, eco.agents[attacker].hpMax * 0.1f, m.agent);
        Vector3 away = Vector3Normalize(Vector3Subtract(m.pos, at.pos));
        m.vel = Vector3Add(m.vel, Vector3Scale(away, 4));
    }
}

// ---------------------------------------------------------------- the NPC director: the leviathan (doc p. 11)
void World::StepNpc(float dt) {
    if (leviathan < 0 || leviathan >= (int)eco.agents.size()) return;
    rt::Agent& L = eco.agents[leviathan];
    if (!L.alive) return;
    L.held = 1;   // (the eco leaves it be; it moves here)
    bool highTide = time > roundLen - 60;
    levNoise = std::max(0.0f, levNoise - dt * 0.5f);
    float blood = eco.BloodNear(HOLLOW, 30);
    // wakes to blood or noise in the trench, or to something lingering in its hollow
    int lingering = -1;
    for (const auto& m : mouths) if (m.alive && Vector3Distance(m.pos, HOLLOW) < 28 && m.immuneT <= 0) lingering = m.id;
    if (opts.mode == M_TRENCH_RUSH && levAwakeT <= 0 && !highTide) levAwakeT = 45;
    if (levAwakeT <= 0 && !highTide && (levNoise > 12 || blood > 40 || lingering >= 0)) {
        levAwakeT = 45;
        Say("A single deep note from the trench. The Leviathan wakes.", Color{255, 140, 120, 255});
    }
    if (highTide && levAwakeT > 0) levAwakeT = std::min(levAwakeT, 1.0f);
    Vector3 goal = Vector3Add(HOLLOW, {0, 6, 0});
    float speed = 3;
    if (levAwakeT > 0) {
        levAwakeT -= dt;
        // it hunts the biggest thing in the trench
        int best = -1; float bm = 0;
        for (const auto& m : mouths) if (m.alive && m.immuneT <= 0 && BandAt(m.pos) == B_TRENCH && Vector3Distance(m.pos, L.pos) < 140 && m.mass > bm) { bm = m.mass; best = m.id; }
        if (best >= 0) {
            goal = mouths[best].pos; speed = 7.5f;
            if (Vector3Distance(L.pos, goal) < 8 + BodyRadius(Length(mouths[best]))) {
                KillMouth(mouths[best], -1, leviathan, "leviathan");
                levAwakeT = 0; levNoise = 0;   // (one bite, then sleep)
                Say("The Leviathan sleeps again.", Color{200, 170, 170, 255});
            }
        }
    }
    Vector3 to = Vector3Subtract(goal, L.pos); float d = Vector3Length(to);
    if (d > 0.5f) { Vector3 v = Vector3Scale(to, std::min(speed, d) / d); L.vel = Vector3Lerp(L.vel, v, std::min(1.0f, dt * 1.5f)); }
    else L.vel = Vector3Scale(L.vel, 0.9f);
    L.pos = Vector3Add(L.pos, Vector3Scale(L.vel, dt));
    L.pos.y = std::max(L.pos.y, FloorY(L.pos.x, L.pos.z) + 4);
    L.zone = std::max(0, eco.ZoneAt(L.pos));
}

// ---------------------------------------------------------------- a mouth swims
void World::StepMouth(Mouth& m, float dt) {
    const Data& d = D();
    if (!m.alive) {
        m.respawnT -= dt;
        if (m.respawnT <= 0 && !over) Respawn(m, false);
        return;
    }
    auto dec = [&](float& t) { t = std::max(0.0f, t - dt); };
    dec(m.biteCd); dec(m.abCd); dec(m.immuneT); dec(m.swallowT); dec(m.stunT); dec(m.blindT); dec(m.reverseT); dec(m.holdT); dec(m.jetT); dec(m.frenzyT);
    dec(m.morphT); dec(m.biteAnim); dec(m.hurtT); dec(m.tellT); dec(m.markT); dec(m.abT);
    const FormDef& F = FormOf(m);
    Input in = m.in;
    if (m.pendingFork) {
        m.forkT -= dt;
        if (in.fork >= 0) PickFork(m, in.fork);
        else if (m.forkT <= 0) PickFork(m, 0);
    }
    if (m.reverseT > 0) { in.yaw = m.yaw - (in.yaw - m.yaw); in.pitch = -in.pitch; }
    // turning: the heading eases toward the aim at the tier's turn rate (big things turn slowly)
    float turn = d.tiers[m.tier].turn * DEG2RAD * (m.dashT > 0 ? 0.2f : 1.0f);
    float dy = atan2f(sinf(in.yaw - m.yaw), cosf(in.yaw - m.yaw));
    m.yaw += std::clamp(dy, -turn * dt, turn * dt);
    float tp = F.walker && !m.airborne ? 0.0f : std::clamp(in.pitch, -1.35f, 1.35f);
    m.pitch += std::clamp(tp - m.pitch, -turn * dt, turn * dt);
    m.bank += (std::clamp(-dy * 1.5f, -0.7f, 0.7f) - m.bank) * std::min(1.0f, dt * 5);
    Vector3 f = Fwd(m.yaw, m.pitch);
    bool frozen = m.stunT > 0 || m.holdT > 0 || (m.abT > 0 && F.ab == AB_ROLL) || m.ambush;
    // boost (stamina: 4 s, refilled in 6)
    m.boosting = in.boost && in.swim && m.stamina > 0.02f && !frozen;
    if (m.boosting) m.stamina = std::max(0.0f, m.stamina - dt / d.staminaS);
    else m.stamina = std::min(1.0f, m.stamina + dt / d.staminaRefillS * (F.ps == PS_HOVER && Vector3Length(m.vel) < 0.5f ? 2 : 1));
    float target = in.swim && !frozen ? Speed(m) * (m.boosting ? d.boostMul : 1) * (m.swallowT > 0 ? 0.35f : 1) * (m.jetT > 0 ? 1.3f : 1) : 0;
    if (in.brake) target = 0;
    m.sinceDash += dt;
    if (m.dashT > 0 && m.blindT > 0 && m.dashKind != 4) { m.dashT = 0; m.dashLeft = 0; }   // (ink ruins a dash)
    if (m.dashT > 0 && !frozen) {
        m.dashT -= dt; m.sinceDash = 0;
        m.vel = m.dashV;
        // a dash that meets something: the lunge bites, the death roll grabs, the ram knocks back, the breach tears
        if (m.dashKind >= 1) {
            int t = NearestMouthInFront(*this, m, Reach(m) + 0.6f, 0.3f);
            int a = t < 0 ? NearestAgentInFront(*this, m, Reach(m) + 0.6f, 0.3f) : -1;
            if (t >= 0 || a >= 0) {
                if (m.dashKind == 1) { Bite(m, true); m.dashT = 0; }
                else if (m.dashKind == 2) {   // the death roll: hold 2 s, then 15% of its mass
                    if (t >= 0) { Mouth& o = mouths[t]; if (SwallowOk(*this, m, o)) Bite(m, true); else { o.holdT = std::max(o.holdT, F.p2); m.holdOf = t; m.holdAgent = false; m.abT = F.p2; } }
                    else { eco.agents[a].held = F.p2; m.holdOf = a; m.holdAgent = true; m.abT = F.p2; }
                    m.dashT = 0;
                } else if (m.dashKind == 3) {   // the ram: knocked back 5 m, stunned 1 s
                    if (t >= 0) { Mouth& o = mouths[t]; o.pos = Vector3Add(o.pos, Vector3Scale(f, 5)); o.stunT = std::max(o.stunT, 1.0f); Hurt(o, d.fightPct * o.mass * BiteMul(m) / std::max(0.2f, HpMul(o)), m.id, -1, "player"); }
                    else { eco.agents[a].pos = Vector3Add(eco.agents[a].pos, Vector3Scale(f, 5)); eco.agents[a].stun = 1; eco.Damage(a, eco.agents[a].hpMax * d.fightPct * BiteMul(m), m.agent, true); }
                    m.dashT = 0;
                } else if (m.dashKind == 4) {   // the breach: a quarter of what it hits
                    if (t >= 0) { Mouth& o = mouths[t]; if (SwallowOk(*this, m, o)) Bite(m, true); else { float x = F.p1 * o.mass; Hurt(o, x, m.id, -1, "player"); Feed(m, x * 0.5f, false); } }
                    else Bite(m, true);
                    m.dashT = 0;
                }
            }
        }
        if (m.dashT <= 0 && m.dashLeft > 0) { m.dashLeft--; m.dashT = F.p2; m.dashV = Vector3Scale(f, Vector3Length(m.dashV)); }
    } else {
        float acc = F.walker ? 6.0f : 3.2f;
        Vector3 want = Vector3Scale(F.walker && !m.airborne ? Vector3Normalize({cosf(m.yaw), 0, sinf(m.yaw)}) : f, target);
        if (F.walker && !m.airborne) want.y = m.vel.y;
        m.vel = Vector3Lerp(m.vel, want, std::min(1.0f, dt * acc));
    }
    // the death roll's hold ends: 15% of the held one's mass
    if (m.holdOf >= 0 && m.abT <= 0 && F.ab == AB_DEATH_ROLL) {
        if (!m.holdAgent && m.holdOf < (int)mouths.size() && mouths[m.holdOf].alive) { Mouth& o = mouths[m.holdOf]; float x = 0.15f * o.mass; Hurt(o, x, m.id, -1, "player"); Feed(m, x, false); }
        else if (m.holdAgent && m.holdOf < (int)eco.agents.size() && eco.agents[m.holdOf].alive) { float pm = MassOfAgent(m.holdOf); eco.Damage(m.holdOf, eco.agents[m.holdOf].hpMax * 0.15f, m.agent, true); Feed(m, pm * 0.15f, false); }
        m.holdOf = -1;
    }
    if (m.holdOf >= 0 && m.abT > 0) {   // (rolling with it)
        Vector3 at = Vector3Add(m.pos, Vector3Scale(f, Length(m) * 0.5f));
        if (!m.holdAgent && m.holdOf < (int)mouths.size()) mouths[m.holdOf].pos = at; else if (m.holdAgent && m.holdOf < (int)eco.agents.size()) eco.agents[m.holdOf].pos = at;
        m.bank += dt * 14;
    }
    // walkers: gravity, the floor, a jump with Space (doc p. 9: they fall at 3 m/s)
    float fy = FloorY(m.pos.x, m.pos.z);
    float r = BodyRadius(Length(m));
    if (F.walker) {
        if (m.airborne || m.pos.y > fy + r + 0.05f) { m.airborne = true; if (m.dashT <= 0) m.vel.y = std::max(m.vel.y - 9 * dt, -3.0f); }
        if (in.brake == false && m.in.ability == false && m.in.swim && m.in.pitch > 0.6f && !m.airborne && m.stunT <= 0 && m.dashT <= 0) { m.vel.y = 4.5f; m.airborne = true; }   // (Space: a jump)
    }
    Vector3 cur = CurrentAt(m.pos);
    if (F.walker && !m.airborne) cur = {0, 0, 0};
    m.pos = Vector3Add(m.pos, Vector3Scale(Vector3Add(m.vel, cur), dt));
    // the water's bounds: the surface, the floor, the arena's edge
    fy = FloorY(m.pos.x, m.pos.z);
    if (m.pos.y < fy + r) { m.pos.y = fy + r; if (m.vel.y < 0) m.vel.y = 0; if (F.walker) m.airborne = false; }
    if (F.walker && !m.airborne && m.dashT <= 0) m.pos.y = fy + r;
    if (m.pos.y > -0.3f) { m.pos.y = -0.3f; if (m.vel.y > 0) m.vel.y = 0; }
    if (m.pos.x < X0 + 2 || m.pos.x > X1 - 2) { m.pos.x = std::clamp(m.pos.x, X0 + 2, X1 - 2); m.vel.x = 0; }
    if (m.pos.z < Z0 + 2 || m.pos.z > Z1 - 2) { m.pos.z = std::clamp(m.pos.z, Z0 + 2, Z1 - 2); m.vel.z = 0; }
    if (m.pos.x > 125 && BandAt(m.pos) != B_TRENCH && m.pos.y < -118) m.pos.y = std::max(m.pos.y, -118.0f);
    // coral heads: the big can't squeeze through the corridors
    if (m.pos.x < -40 && m.pos.x > -175) for (const auto& c : Corals()) {
        float dx = m.pos.x - c.pos.x, dz = m.pos.z - c.pos.z, dd = dx * dx + dz * dz, rr = c.r + r * 1.6f;
        if (dd < rr * rr && m.pos.y < c.pos.y + c.h && dd > 1e-4f) { float k = rr / sqrtf(dd); m.pos.x = c.pos.x + dx * k; m.pos.z = c.pos.z + dz * k; }
    }
    // hidden in a hole it fits
    m.hidden = false;
    for (const auto& h : Holes()) {
        int maxT = h.maxTier + (F.ps == PS_HOLE && h.maxTier < 3 ? 1 : 0);
        if (m.tier <= maxT && Vector3Distance(m.pos, h.pos) < h.r) { m.hidden = true; break; }
    }
    // still: camouflage, the burrow, the ambush (moving ends it)
    float spd = Vector3Length(m.vel);
    if (spd < 0.35f) m.stillT += dt; else { m.stillT = 0; m.buriedT = 0; if (m.ambush && spd > 1.0f) m.ambush = false; }
    if (F.ps == PS_BURROW && m.stillT > 0 && m.pos.y < fy + r + 0.3f) m.buriedT += dt;
    // the water's hazards: the brine pool, clouds, poison, bleeding
    if (Vector3Distance({m.pos.x, 0, m.pos.z}, {BRINE.x, 0, BRINE.z}) < BRINE_R && m.pos.y < fy + 3 && F.ps != PS_SCAVENGER) Hurt(m, 10 * dt, -1, -1, "the brine pool");
    for (const auto& c : clouds) {
        if (c.owner == m.id || Vector3Distance(c.pos, m.pos) > c.r) continue;
        if (c.kind == 0) m.blindT = std::max(m.blindT, std::min(c.t, 3.0f));   // (ink blinds for its seconds: you see through only your own)
        if (c.kind == 1) Hurt(m, 0.03f * m.mass * dt, c.owner, -1, "player");
    }
    if (m.poisonT > 0) Hurt(m, 0.02f * m.mass * dt, m.lastHurtBy, -1, "player");
    if (m.bleedT > 0) { Hurt(m, 0.02f * m.mass * dt, m.lastHurtBy, -1, "player"); eco.AddBlood(m.pos, dt); }
    if (!m.alive) return;
    // plankton: a fry's meal (tiers 1-2: a tier-5 can't be bothered)
    if (m.tier <= 2) for (const auto& p : plankton) if (Vector3Distance(p.pos, m.pos) < p.r) { Feed(m, d.planktonPerS * dt, false); break; }
    // big fish are hungry: 1% a minute above tier 3, not at all in the final minute
    if (m.tier >= 4 && time < roundLen - 60) m.mass -= m.mass * d.decayPerMin / 60 * dt;
    if (in.bite) Bite(m);
    if (in.ability) UseAbility(m);
    GrowCheck(m);
    if (m.king) m.crownT += dt;
    if (m.agent >= 0) { rt::Agent& a = eco.agents[m.agent]; a.pos = m.pos; a.vel = m.vel; a.alive = m.alive; a.zone = std::max(0, eco.ZoneAt(m.pos)); }
}

void World::Predict(Mouth& m, const Input& in0, float dt) {
    if (!m.alive) return;
    const Data& d = D();
    const FormDef& F = FormOf(m);
    Input in = in0;
    if (m.reverseT > 0) { in.yaw = m.yaw - (in.yaw - m.yaw); in.pitch = -in.pitch; }
    float turn = d.tiers[std::clamp(m.tier, 1, 8)].turn * DEG2RAD;
    float dy = atan2f(sinf(in.yaw - m.yaw), cosf(in.yaw - m.yaw));
    m.yaw += std::clamp(dy, -turn * dt, turn * dt);
    float tp = F.walker && !m.airborne ? 0.0f : std::clamp(in.pitch, -1.35f, 1.35f);
    m.pitch += std::clamp(tp - m.pitch, -turn * dt, turn * dt);
    m.bank += (std::clamp(-dy * 1.5f, -0.7f, 0.7f) - m.bank) * std::min(1.0f, dt * 5);
    bool frozen = m.stunT > 0 || m.holdT > 0 || m.ambush;
    float target = in.swim && !frozen && !in.brake ? Speed(m) * (in.boost && m.stamina > 0.02f ? d.boostMul : 1) * (m.swallowT > 0 ? 0.35f : 1) : 0;
    Vector3 f = Fwd(m.yaw, m.pitch);
    if (m.dashT <= 0) {
        Vector3 want = Vector3Scale(F.walker && !m.airborne ? Vector3Normalize({cosf(m.yaw), 0, sinf(m.yaw)}) : f, target);
        m.vel = Vector3Lerp(m.vel, want, std::min(1.0f, dt * (F.walker ? 6.0f : 3.2f)));
    }
    m.pos = Vector3Add(m.pos, Vector3Scale(m.vel, dt));
    float r = BodyRadius(Length(m)), fy = FloorY(m.pos.x, m.pos.z);
    if (m.pos.y < fy + r) m.pos.y = fy + r;
    if (F.walker && !m.airborne) m.pos.y = fy + r;
    m.pos.y = std::min(m.pos.y, -0.3f);
    m.pos.x = std::clamp(m.pos.x, X0 + 2, X1 - 2); m.pos.z = std::clamp(m.pos.z, Z0 + 2, Z1 - 2);
}

// ---------------------------------------------------------------- bots (doc p. 19): Minnow, Hunter, Shark
bool World::Visible(const Mouth& v, const Mouth& m) const {
    float d = Vector3Distance(v.pos, m.pos);
    bool electro = FormOf(v).ps == PS_ELECTRO && d < 15;
    if (m.hidden && !electro) return d < 2;
    if ((m.buriedT >= 2 || m.ambush) && !electro) return false;
    if (FormOf(m).ps == PS_CAMO && m.stillT > 1 && !electro) return false;
    if (v.blindT > 0 && d > 1.5f) return false;
    if (!electro) for (const auto& c : clouds) if (c.kind == 0 && c.owner != v.id && Vector3Distance(c.pos, m.pos) < c.r) return false;
    return true;
}
static float Angle(Vector3 d, float* pitch) { float h = sqrtf(d.x * d.x + d.z * d.z); *pitch = atan2f(d.y, std::max(0.01f, h)); return atan2f(d.z, d.x); }
static Vector3 BandGoal(World& w, int tier, int level) {
    // where a mouth of this tier wants to be (doc p. 15, "Where to be")
    float x, y;
    if (tier <= 2) { x = w.Rand(-285, -175); }
    else if (tier == 3) { x = w.Rand(-165, -50); }
    else if (tier == 4) { x = w.Rand(-150, -10); }
    else if (tier <= 6) { x = w.Rand(-30, 115); }
    else { x = level >= 3 && w.Rand() < 0.3f ? w.Rand(130, 200) : w.Rand(0, 120); }
    float z = w.Rand(Z0 + 15, Z1 - 15);
    (void)level;
    float fy = FloorY(x, z);
    y = fy + 2 + w.Rand() * std::min(25.0f, std::max(2.0f, -fy - 4));
    return {x, std::min(y, -1.5f), z};
}
void World::StepBot(Mouth& m, float dt) {
    if (!m.alive) return;
    const Data& d = D();
    const FormDef& F = FormOf(m);
    Input& in = m.in;
    in.fork = m.pendingFork ? 0 : -1;
    in.bite = in.ability = false; in.brake = false;
    m.thinkT -= dt;
    float dusk = time > roundLen * 2 / 3 ? 1 : 0;
    if (m.thinkT <= 0) {
        m.thinkT = 0.2f + Rand() * 0.1f;
        float light = F.ps == PS_TRENCH_BORN ? 1 : std::max(0.35f, LightAt(m.pos, dusk));
        float sense = d.botSense[m.botLevel] * light;
        // threats: anything that could swallow us
        float td = 1e9f; Vector3 tpos{};
        for (const auto& o : mouths) {
            if (!o.alive || o.id == m.id || !SwallowOk(*this, o, m) || !Visible(m, o) || Friends(m, o)) continue;
            float dd = Vector3Distance(o.pos, m.pos);
            float omen = FormOf(o).ps == PS_OMEN ? 20 : 0;
            if (dd < std::max(sense * 0.8f, omen) && dd < td) { td = dd; tpos = o.pos; }
        }
        for (int i = 0; i < (int)eco.agents.size(); i++) {
            const rt::Agent& a = eco.agents[i];
            if (!a.alive || a.diver >= 0 || npcEats[a.sp] < m.tier) continue;
            if (fabsf(a.pos.x - m.pos.x) > sense || fabsf(a.pos.z - m.pos.z) > sense) continue;
            if (F.ps == PS_APEX && eco.map->species[a.sp].name != "Orca" && i != leviathan) continue;
            if (i == leviathan && levAwakeT <= 0) continue;
            float dd = Vector3Distance(a.pos, m.pos);
            if (dd < sense * (i == leviathan ? 2.5f : 0.8f) && dd < td) { td = dd; tpos = a.pos; }
        }
        // the net: anything tier 3 or under keeps away from it
        if (boat.on && boat.net && m.tier <= 3) { Vector3 nc = boat.NetCentre(); float dd = Vector3Distance(nc, m.pos); if (dd < 22 && dd < td) { td = dd; tpos = nc; } }
        // a bot that knows better keeps out of the leviathan's hollow once it's big
        bool levFear = m.botLevel >= 2 && m.tier >= 6 && Vector3Distance(m.pos, HOLLOW) < 60;
        if (levFear && td > 40) { td = 40; tpos = HOLLOW; }
        m.fleeing = td < 1e8f;
        m.tgtMouth = -1; m.tgtAgent = -1;
        if (m.fleeing) {
            m.fleeFrom = tpos;
            Vector3 away = Vector3Normalize(Vector3Subtract(m.pos, tpos));
            // a hole it fits, if one's near; else away and toward the shallows
            Vector3 g = Vector3Add(m.pos, Vector3Scale(away, 25));
            g.x -= 8;
            // (never into a corner: a goal off the edge slides along it, toward the middle)
            if (g.x < X0 + 15) { g.x = X0 + 15; g.z += (m.pos.z > 0 ? -20.0f : 20.0f); }
            if (g.z < Z0 + 15) { g.z = Z0 + 15; g.x += 20; }
            if (g.z > Z1 - 15) { g.z = Z1 - 15; g.x += 20; }
            if (g.x > X1 - 15) g.x = X1 - 15;
            g.y = std::clamp(g.y, FloorY(g.x, g.z) + 1.5f, -1.5f);
            for (const auto& h : Holes()) if (m.tier <= h.maxTier + (F.ps == PS_HOLE ? 1 : 0) && Vector3Distance(h.pos, m.pos) < 18 && Vector3Distance(h.pos, tpos) > Vector3Distance(m.pos, tpos)) { g = h.pos; break; }
            m.goal = g;
        } else {
            // food: the best value for the distance
            float best = 0;
            for (int i = 0; i < (int)eco.agents.size(); i++) {
                const rt::Agent& a = eco.agents[i];
                if (!a.alive || a.diver >= 0 || i == leviathan) continue;
                if (fabsf(a.pos.x - m.pos.x) > sense || fabsf(a.pos.z - m.pos.z) > sense) continue;
                if (m.banT > 0 && m.banId == i) continue;
                float pm = MassOfAgent(i);
                bool canEat = pm < d.swallowBelow * m.mass;
                bool fightIt = !canEat && m.botLevel >= 2 && pm < m.mass * 0.95f && npcEats[a.sp] < m.tier;   // (a fight it should win)
                if (!canEat && !fightIt) continue;
                if (pm < m.mass * 0.01f && m.tier >= 4) continue;
                const PreyDef* pd = PreyOf(eco.map->species[a.sp].name);
                if (pd && pd->armored && F.path != P_CRUST && F.ps != PS_IRON_STOMACH) pm -= d.armoredCost * 2;
                if (F.walker && a.pos.y - FloorY(a.pos.x, a.pos.z) > 4) continue;   // (a crab can't reach the tuna)
                float dd = Vector3Distance(a.pos, m.pos);
                float v = pm / (dd + 4) * (fightIt ? 0.5f : 1);
                if (v > best) { best = v; m.tgtAgent = i; m.tgtMouth = -1; }
            }
            if (Rand() < d.botHuntMouths[m.botLevel] + 0.15f) for (const auto& o : mouths) {
                if (!o.alive || o.id == m.id || o.immuneT > 0 || !Visible(m, o) || Friends(m, o)) continue;
                if (m.banT > 0 && m.banId == 100000 + o.id) continue;
                bool canEat = SwallowOk(*this, m, o);
                bool goKing = m.botLevel >= 3 && o.king && m.mass > o.mass * 0.75f;
                if (!canEat && !goKing) continue;
                if (F.walker && o.pos.y - FloorY(o.pos.x, o.pos.z) > 4) continue;
                float dd = Vector3Distance(o.pos, m.pos);
                if (dd > sense) continue;
                float v = o.mass / (dd + 4) * (FormOf(o).path == P_BLOB ? 0.3f : 1.5f) * (goKing ? 3 : 1);
                if (v > best) { best = v; m.tgtMouth = o.id; m.tgtAgent = -1; }
            }
            if (fall.on && m.tier >= 5 && !m.fleeing && Vector3Distance(fall.pos, m.pos) < 260 && best < 3) { m.tgtAgent = -1; m.tgtMouth = -1; m.goal = fall.pos; best = 3; }   // (the feast)
            if (boat.on && boat.chum && m.tier <= 4 && m.botLevel <= 2) { Vector3 c{boat.pos.x, -2, boat.pos.z - boat.dirZ * 12}; float dd = Vector3Distance(c, m.pos); if (dd < 60 && 2.0f / (dd + 4) > best) { best = 2.0f / (dd + 4); m.tgtAgent = -1; m.tgtMouth = -1; m.goal = c; } }
            if (m.tier <= 2 && best < 0.15f) for (const auto& p : plankton) { float dd = Vector3Distance(p.pos, m.pos); if (dd < sense * 2 && 1.2f / (dd + 4) > best) { best = 1.2f / (dd + 4); m.tgtAgent = -1; m.tgtMouth = -1; m.goal = p.pos; } }
            if (m.tgtAgent < 0 && m.tgtMouth < 0 && (best <= 0 || Vector3Distance(m.goal, m.pos) < 4)) {
                if (Vector3Distance(m.goal, m.pos) < 6 || m.goal.x == 0 || Rand() < 0.02f) { m.goal = BandGoal(*this, m.tier, m.botLevel); m.goal.z = std::clamp(m.pos.z + (m.goal.z - m.pos.z) * 0.35f, Z0 + 10, Z1 - 10); m.goal.y = std::clamp(m.goal.y, FloorY(m.goal.x, m.goal.z) + 1.5f, -1.5f); }
            }
        }
    }
    // a chase that isn't closing in 6 s is given up for 15
    m.banT = std::max(0.0f, m.banT - dt);
    {
        int cid = m.tgtAgent >= 0 ? m.tgtAgent : m.tgtMouth >= 0 ? 100000 + m.tgtMouth : -1;
        if (cid != m.chaseId) { m.chaseId = cid; m.chaseBest = 1e9f; m.chaseT = 0; }
        else if (cid >= 0) {
            Vector3 tp = m.tgtAgent >= 0 ? eco.agents[m.tgtAgent].pos : mouths[m.tgtMouth].pos;
            float dd = Vector3Distance(tp, m.pos);
            if (dd < m.chaseBest - 1.0f) { m.chaseBest = dd; m.chaseT = 0; }
            else if ((m.chaseT += dt) > 6 && !duel) { m.banId = cid; m.banT = 15; m.tgtAgent = m.tgtMouth = -1; m.chaseId = -1; m.thinkT = 0; }
        }
    }
    if (duel) { m.fleeing = false; m.tgtAgent = -1; m.tgtMouth = -1; for (const auto& o : mouths) if (o.id != m.id && o.alive && (Visible(m, o) || Vector3Distance(o.pos, m.pos) < 2)) m.tgtMouth = o.id; if (m.tgtMouth < 0) m.goal = Vector3Add(m.pos, {Rand(-6, 6), Rand(-2, 2), Rand(-6, 6)}); }
    m.retreatT = std::max(0.0f, m.retreatT - dt);
    // steer for the target or the goal
    Vector3 tgt = m.goal; float tr = 0;
    if (!m.fleeing && m.tgtAgent >= 0) { if (!eco.agents[m.tgtAgent].alive) m.tgtAgent = -1; else { tgt = eco.agents[m.tgtAgent].pos; tr = AgentRadius(*eco.map, eco.agents[m.tgtAgent].sp); } }
    if (!m.fleeing && m.tgtMouth >= 0) { const Mouth& o = mouths[m.tgtMouth]; if (!o.alive) m.tgtMouth = -1; else { float dd = Vector3Distance(o.pos, m.pos); tgt = Vector3Add(o.pos, Vector3Scale(o.vel, std::clamp((dd - 2) / 15, 0.0f, 0.3f))); tr = BodyRadius(Length(o)); } }   // (leads a far target, not one in its face)
    if (m.botLevel <= 2 && m.tier >= 5 && tgt.x > 125 && m.botLevel == 2) tgt.x = 120;   // (a Hunter avoids the trench)
    Vector3 to = Vector3Subtract(tgt, m.pos);
    float dist = Vector3Length(to);
    if (F.path == P_EEL && m.tgtMouth >= 0 && dist < 4 && !F.walker) to.y -= tr * 1.2f;   // (an eel comes up under the belly)
    if (m.retreatT > 0 && m.tgtMouth >= 0) to = Vector3Scale(to, -1);   // (backing off)
    float pitch, yaw = Angle(to, &pitch);
    in.yaw = yaw; in.pitch = pitch; in.swim = dist > 0.2f;
    bool chasing = m.tgtAgent >= 0 || m.tgtMouth >= 0;
    float reach = Reach(m);
    in.boost = (m.fleeing && Vector3Distance(m.pos, m.fleeFrom) < 12 && m.stamina > 0.05f) || (chasing && dist < 7 && dist > reach && m.stamina > 0.4f);
    // the nearest fight: a mouth near us that we can't swallow and that can't swallow us (or can)
    float fightD = 1e9f;
    for (const auto& o : mouths) if (o.alive && o.id != m.id && o.mass > m.mass * d.swallowBelow && Visible(m, o)) fightD = std::min(fightD, Vector3Distance(o.pos, m.pos));
    if (m.tgtMouth >= 0 && m.botLevel >= 3) { const Mouth& o = mouths[m.tgtMouth]; if (o.tellT > 0 && (FormOf(o).ab == AB_INFLATE || FormOf(o).ab == AB_DETONATE || FormOf(o).ab == AB_TOXIN) && dist < 4) m.retreatT = std::max(m.retreatT, 0.4f); }
    if (m.tgtMouth >= 0 && F.path == P_EEL) { const Mouth& o = mouths[m.tgtMouth]; if (FormOf(o).ab == AB_INFLATE && o.abT > 0 && dist < 3) m.retreatT = std::max(m.retreatT, 0.3f); }
    if (chasing && m.retreatT <= 0 && dist - tr < reach * 1.1f) {
        Vector3 f = Fwd(m.yaw, m.pitch);
        if (dist < 0.05f || Vector3DotProduct(Vector3Scale(to, 1 / std::max(0.05f, dist)), f) > 0.4f) in.bite = true;
        if (dist - tr < reach * 0.5f) in.swim = false;
    }
    if (chasing && m.tgtMouth >= 0 && m.retreatT <= 0 && dist - tr < reach * 1.8f) {
        // close quarters: don't overshoot; check the swim, turn to face it, and bite on the turn
        const Mouth& o = mouths[m.tgtMouth];
        Vector3 away = Vector3Subtract(o.pos, m.pos); float closing = Vector3DotProduct(o.vel, Vector3Normalize(away));
        if (closing < 1.0f) in.swim = dist - tr > reach * 0.8f; else in.swim = true;
    }
    // abilities, by how the bot reads the moment
    if (F.ab != AB_NONE && m.abCd <= 0 && Rand() < d.botAbilityUse[m.botLevel] * dt * 4) {
        float fd = m.fleeing ? Vector3Distance(m.pos, m.fleeFrom) : 1e9f;
        bool use = false;
        switch (F.ab) {
            case AB_DASH: case AB_RIBBON: use = (chasing && dist > reach + 1 && dist < F.p1 * 1.5f) || fd < 6; break;
            case AB_LUNGE: case AB_DEATH_ROLL: case AB_RAM: use = chasing && dist > reach && dist < F.p1 * 1.2f; break;
            case AB_HOOK: use = m.tgtMouth >= 0 && dist < F.p1 && dist > reach; break;
            case AB_BREACH: use = chasing && to.y > 1 && dist < 10; break;
            case AB_INK: case AB_INK_WALL: case AB_DAZZLE: use = fd < 7 || fightD < 4; break;
            case AB_INFLATE: case AB_ROLL: use = fd < 4 || fightD < 2.5f; break;
            case AB_TAIL_FLIP: use = fd < 4 || (fightD < 1.5f && m.biteCd > 0); break;
            case AB_TOXIN: use = fd < 5 || fightD < 3; break;
            case AB_FRENZY: case AB_PIN: case AB_PUNCH: use = chasing && dist - tr < reach * 1.5f + (F.ab == AB_PUNCH ? 2 : 0); break;
            case AB_JUMP: use = (chasing && to.y > 2 && dist < 7) || fd < 5; break;
            case AB_CLAW_SLAM: { int n = 0; for (const auto& o : mouths) if (o.alive && o.id != m.id && Vector3Distance(o.pos, m.pos) < F.p1) n++; use = n > 0; break; }
            case AB_DETONATE: { int n = 0; for (const auto& o : mouths) if (o.alive && o.id != m.id && Vector3Distance(o.pos, m.pos) < F.p1) n++; use = n >= 2 || fd < 3; break; }
            case AB_AMBUSH: use = !chasing && !m.fleeing && m.pos.y - FloorY(m.pos.x, m.pos.z) < 1.5f && Rand() < 0.1f; break;
            default: break;
        }
        if (use) in.ability = true;
    }
    if (m.ambush && !chasing && !m.fleeing) { in.swim = false; }
    if (fall.on && !m.fleeing && Vector3Distance(m.goal, fall.pos) < 1 && Vector3Distance(m.pos, fall.pos) < Reach(m) + 4) { in.bite = true; in.swim = false; }
    if (m.dashT > 0 && chasing) in.bite = true;   // (a dash is a pass with the jaws open)
    // a walker jumps toward what's above it
    if (F.walker && !m.airborne && to.y > 2.5f && dist < 6) in.pitch = 1;
}

// ---------------------------------------------------------------- the round's clock
void World::Step(float dt) {
    if (over || mirror) return;
    time += dt;
    // the web, then the mouths (people's input is set by the scene or the session; bots think here)
    for (auto& m : mouths) if (m.bot) StepBot(m, dt);
    eco.Step(dt);
    StepNpc(dt);
    StepEvents(dt);
    if (((int)(time * 30)) & 1) { for (int i = (int)mouths.size() - 1; i >= 0; i--) StepMouth(mouths[i], dt); }
    else for (auto& m : mouths) StepMouth(m, dt);   // (alternating who moves first: nobody always bites first)
    // the web's fish stay above the seabed, and the dead come back in their home water (the World's own refill)
    if (deadT.size() < eco.agents.size()) deadT.resize(eco.agents.size(), 0);
    for (int i = 0; i < (int)eco.agents.size(); i++) {
        rt::Agent& a = eco.agents[i];
        if (a.diver >= 0) continue;
        if (a.alive) {
            float fy = FloorY(a.pos.x, a.pos.z);
            if (a.pos.y < fy + 0.4f) { a.pos.y = fy + 0.4f; if (a.vel.y < 0) a.vel.y = 0; }
            if (a.pos.y > -0.4f) a.pos.y = -0.4f;
            continue;
        }
        float rs = reviveS[a.sp];
        if (rs <= 0) continue;
        deadT[i] += dt;
        if (deadT[i] < rs) continue;
        deadT[i] = 0;
        const rt::Zone& z = eco.map->zones[std::clamp(a.homeZone, 0, (int)eco.map->zones.size() - 1)];
        float x = z.plan.x + Rand() * z.plan.width, zz = z.plan.y + Rand() * z.plan.height;
        float fy = std::max(FloorY(x, zz), z.y0);
        float y = std::min(z.y1 - 0.5f, fy + 1 + Rand() * std::max(1.0f, std::min(30.0f, z.y1 - fy - 2)));
        a.alive = true; a.hp = a.hpMax; a.wound = 0; a.st = rt::State::Graze; a.target = -1; a.targetCorpse = false; a.stun = a.held = 0;
        a.pos = a.home = a.goal = {x, y, zz}; a.vel = {0, 0, 0}; a.zone = a.homeZone; a.hunger = 0.3f; a.stateT = 0;
    }
    for (auto& c : clouds) c.t -= dt;
    clouds.erase(std::remove_if(clouds.begin(), clouds.end(), [](const Cloud& c) { return c.t <= 0; }), clouds.end());
    for (auto& p : plankton) {
        p.pos = Vector3Add(p.pos, Vector3Scale(p.drift, dt));
        if (p.pos.x > -60) p.pos.x = -290;
        p.pos.z = std::clamp(p.pos.z, Z0 + 5, Z1 - 5);
        p.pos.y = std::max(p.pos.y, FloorY(p.pos.x, p.pos.z) + 1.5f);
    }
    // the score: the crown's time, the tiers past 4 (counted at the end)
    for (auto& m : mouths) if (m.king && m.alive) m.score += D().scoreCrownPerS * dt;
    if (time >= roundLen) {
        over = true;
        for (auto& m : mouths) m.score += D().scoreTierPast4 * std::max(0, m.bestTier - 4);
        winner = Leader();
        if (opts.mode == M_FOOD_CHAIN) {
            int bt = 0; for (int k = 1; k < 4; k++) if (TeamScore(k) > TeamScore(bt)) bt = k;
            for (auto& m : mouths) if (m.team == bt) m.score += D().scoreWin;
            Say(TextFormat("Team %d (%s) wins the round.", bt + 1, PathName(teamPath[bt])), Color{255, 220, 110, 255});
            winner = -1; float bs = -1; for (const auto& m : mouths) if (m.team == bt && ScoreOf(*this, m) > bs) { bs = ScoreOf(*this, m); winner = m.id; }
        } else if (winner >= 0) { mouths[winner].score += D().scoreWin; Say(mouths[winner].name + " wins the round.", Color{255, 220, 110, 255}); }
    }
}
float ScoreOf(const World& w, const Mouth& m) { return m.score + m.massEaten / D().scoreMassPer + (w.over ? 0 : D().scoreTierPast4 * std::max(0, m.bestTier - 4)); }
int World::Leader() const { int b = -1; float bs = -1; for (const auto& m : mouths) { float s = ScoreOf(*this, m); if (s > bs) { bs = s; b = m.id; } } return b; }
std::vector<int> World::Board() const {
    std::vector<int> v; for (const auto& m : mouths) v.push_back(m.id);
    std::sort(v.begin(), v.end(), [&](int a, int b) { return ScoreOf(*this, mouths[a]) > ScoreOf(*this, mouths[b]); });
    return v;
}

} // namespace mf
