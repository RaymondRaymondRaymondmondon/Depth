// Red Tide's Ecosystem (design doc, "The ecosystem engine"): one simulation per map in which every species lives by
// the same rules. The seven layers of the web map onto this file as:
//   1 trophic      - hunger, diet (the workbook's Diet matrix), hunting, feeding, fed-and-calm      (Decide, Hunt)
//   2 blood/scent  - a 3D scent grid, diffusion, current advection, per-species thresholds          (UpdateFields)
//   3 sound        - a fast-decaying second field; fearful flee, curious approach, aggressive attack  (UpdateFields)
//   4 territory    - home, defend radius, return when calm                                           (Decide)
//   5 symbiosis    - cleaners calm hosts, parasites attach and bleed their host                       (Decide)
//   6 flora        - patches with units that grazers eat and that regrow; grazer capacity follows    (Population)
//   7 enemy        - the faction is a species; the alarm spawns squads that bleed and get eaten      (AlarmUpdate)
// Every number comes from EngineData (the Constants sheet) or the map's workbook data.
#include "redtide.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace rt {

static float FirstNumOf(const std::string& s) {
    for (size_t i = 0; i < s.size(); i++)
        if (isdigit((unsigned char)s[i])) return (float)atof(s.c_str() + i);
    return 0;
}

const char* StateName(State s) {
    switch (s) {
        case State::Graze: return "Graze"; case State::Rest: return "Rest"; case State::Investigate: return "Investigate";
        case State::Hunt: return "Hunt"; case State::Feed: return "Feed"; case State::Flee: return "Flee";
        case State::Defend: return "Defend"; case State::Return: return "Return"; case State::Attached: return "Attached";
        case State::Dead: return "Dead";
    }
    return "?";
}

// ---------------------------------------------------------------- the grid
bool Field::Cell(Vector3 p, int& x, int& y, int& z) const {
    x = (int)floorf((p.x - origin.x) / cell);
    y = (int)floorf((p.y - origin.y) / cell);
    z = (int)floorf((p.z - origin.z) / cell);
    return x >= 0 && y >= 0 && z >= 0 && x < nx && y < ny && z < nz;
}
float Field::At(Vector3 p) const {
    int x, y, z;
    if (!Cell(p, x, y, z)) return 0;
    return v[Idx(x, y, z)];
}
void Field::Add(Vector3 p, float amount) {
    int x, y, z;
    if (!Cell(p, x, y, z)) return;
    int i = Idx(x, y, z);
    if (zone[i] < 0) {
        // on a wall or in rock: put it in the nearest water cell around it
        for (int dz = -1; dz <= 1; dz++) for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            int X = x + dx, Y = y + dy, Z = z + dz;
            if (X < 0 || Y < 0 || Z < 0 || X >= nx || Y >= ny || Z >= nz) continue;
            if (zone[Idx(X, Y, Z)] >= 0) { v[Idx(X, Y, Z)] += amount; return; }
        }
        return;
    }
    v[i] += amount;
}
float Field::Total() const { double t = 0; for (float f : v) t += f; return (float)t; }
void Field::BuildSat() {
    size_t n = (size_t)(nx + 1) * (ny + 1) * (nz + 1);
    sat.assign(n, 0); satX.assign(n, 0); satY.assign(n, 0); satZ.assign(n, 0);
    auto S = [&](int x, int y, int z) { return ((size_t)z * (ny + 1) + y) * (nx + 1) + x; };
    for (int z = 1; z <= nz; z++) for (int y = 1; y <= ny; y++) for (int x = 1; x <= nx; x++) {
        double c = v[Idx(x - 1, y - 1, z - 1)];
        size_t i = S(x, y, z);
        auto sum = [&](std::vector<double>& t, double val) {
            t[i] = val + t[S(x - 1, y, z)] + t[S(x, y - 1, z)] + t[S(x, y, z - 1)]
                 - t[S(x - 1, y - 1, z)] - t[S(x - 1, y, z - 1)] - t[S(x, y - 1, z - 1)] + t[S(x - 1, y - 1, z - 1)];
        };
        sum(sat, c);
        sum(satX, c * (x - 0.5));
        sum(satY, c * (y - 0.5));
        sum(satZ, c * (z - 0.5));
    }
}
float Field::BoxSum(Vector3 p, float r, Vector3* centroid) const {
    if (sat.empty()) return 0;
    int x0 = (int)floorf((p.x - r - origin.x) / cell), x1 = (int)floorf((p.x + r - origin.x) / cell);
    int y0 = (int)floorf((p.y - r - origin.y) / cell), y1 = (int)floorf((p.y + r - origin.y) / cell);
    int z0 = (int)floorf((p.z - r - origin.z) / cell), z1 = (int)floorf((p.z + r - origin.z) / cell);
    x0 = std::clamp(x0, 0, nx - 1); x1 = std::clamp(x1, 0, nx - 1);
    y0 = std::clamp(y0, 0, ny - 1); y1 = std::clamp(y1, 0, ny - 1);
    z0 = std::clamp(z0, 0, nz - 1); z1 = std::clamp(z1, 0, nz - 1);
    auto S = [&](int x, int y, int z) { return ((size_t)z * (ny + 1) + y) * (nx + 1) + x; };
    auto box = [&](const std::vector<double>& t) {
        int X0 = x0, X1 = x1 + 1, Y0 = y0, Y1 = y1 + 1, Z0 = z0, Z1 = z1 + 1;
        return t[S(X1, Y1, Z1)] - t[S(X0, Y1, Z1)] - t[S(X1, Y0, Z1)] - t[S(X1, Y1, Z0)]
             + t[S(X0, Y0, Z1)] + t[S(X0, Y1, Z0)] + t[S(X1, Y0, Z0)] - t[S(X0, Y0, Z0)];
    };
    double s = box(sat);
    if (centroid && s > 1e-6) {
        centroid->x = origin.x + (float)(box(satX) / s) * cell;
        centroid->y = origin.y + (float)(box(satY) / s) * cell;
        centroid->z = origin.z + (float)(box(satZ) / s) * cell;
    }
    return (float)s;
}

double Field::BoxSumAABB(Vector3 lo, Vector3 hi, Vector3* weighted) const {
    if (sat.empty()) return 0;
    int x0 = (int)floorf((lo.x - origin.x) / cell), x1 = (int)floorf((hi.x - origin.x) / cell);
    int y0 = (int)floorf((lo.y - origin.y) / cell), y1 = (int)floorf((hi.y - origin.y) / cell);
    int z0 = (int)floorf((lo.z - origin.z) / cell), z1 = (int)floorf((hi.z - origin.z) / cell);
    x0 = std::clamp(x0, 0, nx - 1); x1 = std::clamp(x1, 0, nx - 1);
    y0 = std::clamp(y0, 0, ny - 1); y1 = std::clamp(y1, 0, ny - 1);
    z0 = std::clamp(z0, 0, nz - 1); z1 = std::clamp(z1, 0, nz - 1);
    if (x1 < x0 || y1 < y0 || z1 < z0) return 0;
    auto S = [&](int x, int y, int z) { return ((size_t)z * (ny + 1) + y) * (nx + 1) + x; };
    auto box = [&](const std::vector<double>& t) {
        int X0 = x0, X1 = x1 + 1, Y0 = y0, Y1 = y1 + 1, Z0 = z0, Z1 = z1 + 1;
        return t[S(X1, Y1, Z1)] - t[S(X0, Y1, Z1)] - t[S(X1, Y0, Z1)] - t[S(X1, Y1, Z0)]
             + t[S(X0, Y0, Z1)] + t[S(X0, Y1, Z0)] + t[S(X1, Y0, Z0)] - t[S(X0, Y0, Z0)];
    };
    double s = box(sat);
    if (weighted && s > 1e-9) {
        weighted->x += (float)((origin.x / cell) * s + box(satX)) * cell;
        weighted->y += (float)((origin.y / cell) * s + box(satY)) * cell;
        weighted->z += (float)((origin.z / cell) * s + box(satZ)) * cell;
    }
    return s;
}

static void BuildField(Field& f, const MapData& m, float cell) {
    // Big maps (Atlantis, the Void) coarsen the grid so it stays about 2 million cells.
    Vector3 lo = Vector3Subtract(m.boundsMin, {2, 2, 2}), hi = Vector3Add(m.boundsMax, {2, 2, 2});
    for (;;) {
        f.nx = std::max(1, (int)ceilf((hi.x - lo.x) / cell));
        f.ny = std::max(1, (int)ceilf((hi.y - lo.y) / cell));
        f.nz = std::max(1, (int)ceilf((hi.z - lo.z) / cell));
        if ((size_t)f.nx * f.ny * f.nz <= 2000000u) break;
        cell *= 1.5f;
    }
    f.cell = cell;
    f.origin = lo;
    size_t n = (size_t)f.nx * f.ny * f.nz;
    f.v.assign(n, 0);
    f.tmp.assign(n, 0);
    f.zone.assign(n, -1);
    for (int z = 0; z < f.nz; z++) for (int y = 0; y < f.ny; y++) for (int x = 0; x < f.nx; x++) {
        Vector3 c{lo.x + (x + 0.5f) * cell, lo.y + (y + 0.5f) * cell, lo.z + (z + 0.5f) * cell};
        for (int zi = 0; zi < (int)m.zones.size(); zi++) if (m.zones[zi].Contains(c, cell * 0.5f)) { f.zone[f.Idx(x, y, z)] = (int16_t)zi; break; }
    }
}

// ---------------------------------------------------------------- helpers
float Ecosystem::Rand() {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (rng & 0xFFFFFF) / (float)0x1000000;
}
void Ecosystem::Event(const std::string& s) {
    events.push_back({time, s});
    if (log) printf("  [%6.1f s] %s\n", time, s.c_str());
}
int Ecosystem::ZoneAt(Vector3 p) const {
    for (int i = 0; i < (int)map->zones.size(); i++) if (map->zones[i].Contains(p, 0.01f)) return i;
    return -1;
}
int Ecosystem::Count(int sp) const {
    int n = 0;
    for (const auto& a : agents) if (a.alive && a.sp == sp) n++;
    return n;
}
int Ecosystem::CountInZone(int sp, int zone) const {
    int n = 0;
    for (const auto& a : agents) if (a.alive && a.sp == sp && a.zone == zone) n++;
    return n;
}
float Ecosystem::DecayRate() const {
    // TideCurve: 2% / 1% / 0.5% by era (the tide table carries it per tide)
    return map->Tide(tide).bloodDecay;
}
std::vector<int> Ecosystem::ZonePath(int from, int to) const {
    std::vector<int> prev(map->zones.size(), -2);
    std::vector<int> q{from};
    prev[from] = -1;
    for (size_t h = 0; h < q.size(); h++) {
        int z = q[h];
        if (z == to) break;
        for (int li = 0; li < (int)map->links.size(); li++) {
            const Link& l = map->links[li];
            if (!LinkOpen(li)) continue;   // a door the divers haven't bought keeps beasts out too
            int n = -1;
            if (l.from == z) n = l.to;
            else if (l.to == z) n = l.from; // water passes both ways; one-way drops are a player rule
            if (n >= 0 && prev[n] == -2) { prev[n] = z; q.push_back(n); }
        }
    }
    std::vector<int> path;
    if (prev[to] == -2) return path;
    for (int z = to; z != -1; z = prev[z]) path.push_back(z);
    std::reverse(path.begin(), path.end());
    return path;
}

float Ecosystem::Smell(Vector3 p, int zone, float r, Vector3* centroid) const {
    if (zone < 0) return scent.BoxSum(p, r, centroid);
    Vector3 lo{p.x - r, p.y - r, p.z - r}, hi{p.x + r, p.y + r, p.z + r};
    Vector3 w{0, 0, 0};
    double total = 0;
    auto zoneBox = [&](int zi) {
        const Zone& z = map->zones[zi];
        Vector3 zl{std::max(lo.x, z.plan.x), std::max(lo.y, z.y0), std::max(lo.z, z.plan.y)};
        Vector3 zh{std::min(hi.x, z.plan.x + z.plan.width), std::min(hi.y, z.y1), std::min(hi.z, z.plan.y + z.plan.height)};
        if (zl.x > zh.x || zl.y > zh.y || zl.z > zh.z) return;
        total += scent.BoxSumAABB(zl, zh, &w);
    };
    zoneBox(zone);
    for (const auto& l : map->links) {
        if (l.from == zone) zoneBox(l.to);
        else if (l.to == zone) zoneBox(l.from);
    }
    if (centroid && total > 1e-6) *centroid = Vector3Scale(w, (float)(1.0 / total));
    return (float)total;
}

bool Ecosystem::Eats(int predSp, int preySp) const {
    if (predSp < 0 || preySp < 0 || predSp >= (int)map->diet.size()) return false;
    for (const auto& pw : map->diet[predSp].prey) if (pw.first == preySp && pw.second > 0) return true;
    return false;
}
float Ecosystem::Aggression(const Agent& a) const {
    float ag = map->species[a.sp].aggression + a.aggrMod;
    if (cleanerRage > 0) ag += eng->C("cleaner_loss_penalty", 0.2f);
    return std::clamp(ag, 0.0f, 1.0f);
}

// ---------------------------------------------------------------- setup
int Ecosystem::Spawn(int sp, Vector3 pos, int zone) {
    const Species& s = map->species[sp];
    Agent a;
    a.sp = sp;
    a.pos = pos;
    a.home = pos;
    a.zone = zone;
    a.homeZone = zone;
    a.hpMax = a.hp = s.hpBase * map->Tide(tide).hpMult;
    // the early tides are "ecosystem calm": apex beasts and bosses start fed, everything else part-hungry
    a.hunger = s.tier >= 4 ? 0.05f + 0.25f * Rand() : 0.2f + 0.4f * Rand();
    a.decideT = Rand() * 0.1f;
    a.rng = (uint32_t)(Rand() * 4e9f) | 1;
    a.goal = pos;
    a.st = s.Sessile() ? State::Rest : State::Graze;
    agents.push_back(a);
    return (int)agents.size() - 1;
}

static Vector3 RandomIn(Ecosystem& e, const Zone& z, float pad = 1.0f) {
    if (z.radial) {
        float a = (z.a0 + (z.a1 - z.a0) * e.Rand()) * DEG2RAD, r = z.rMin + pad + (z.rMax - z.rMin - 2 * pad) * e.Rand();
        return {cosf(a) * r, z.y0 + pad + (z.y1 - z.y0 - 2 * pad) * e.Rand(), sinf(a) * r};
    }
    return {z.plan.x + pad + (z.plan.width - 2 * pad) * e.Rand(), z.y0 + pad + (z.y1 - z.y0 - 2 * pad) * e.Rand(), z.plan.y + pad + (z.plan.height - 2 * pad) * e.Rand()};
}

void Ecosystem::Init(const MapData& m, uint32_t seed, int tideNum, int playerCount) {
    map = &m;
    eng = &Engine();
    rng = seed ? seed : 1;
    tide = tideNum;
    players = playerCount;
    diverSpecies = m.SpeciesIndex("Diver");
    agents.clear(); corpses.clear(); flora.clear(); squads.clear(); events.clear();
    time = 0; scentT = 0; popT = 0; alarmRollT = 0; cleanerRage = 0; squadsSpawned = 0;
    killsBySpecies.assign(m.species.size(), 0);
    deathsBySpecies.assign(m.species.size(), 0);
    alarm.assign(m.alarmRegions.size(), 0);
    BuildField(scent, m, eng->C("scent_cell_m", 2));
    BuildField(sound, m, eng->C("scent_cell_m", 2) * 2); // sound is coarser: it spreads and dies fast
    // flora patches: one per listed zone, at full units
    for (int fi = 0; fi < (int)m.flora.size(); fi++)
        for (const auto& zn : m.flora[fi].zones) {
            int zi = m.ZoneIndex(zn);
            if (zi < 0) continue;
            FloraPatch p;
            p.flora = fi; p.zone = zi; p.pos = RandomIn(*this, m.zones[zi]);
            p.pos.y = m.zones[zi].y0 + 0.5f;
            p.units = eng->C("flora_unit_capacity", 100);
            flora.push_back(p);
        }
    // population from the Spawn sheet
    spawnT.assign(m.spawns.size(), 0);
    spawnAlive.assign(m.spawns.size(), 0);
    int group = 0;
    for (int ri = 0; ri < (int)m.spawns.size(); ri++) {
        const SpawnRow& r = m.spawns[ri];
        int sp = m.SpeciesIndex(r.species);
        if (sp < 0) continue;
        int zi = m.ZoneIndex(r.zone);
        if (zi < 0) zi = m.ZoneIndex(m.species[sp].homeZone);
        if (zi < 0) zi = 0;
        const Species& s = m.species[sp];
        bool grouped = s.social == "school" || s.social == "pack" || s.social == "swarm" || s.social == "group" || s.social == "pair";
        int g = grouped ? group++ : -1;
        Vector3 c = RandomIn(*this, m.zones[zi]);
        for (int k = 0; k < r.count; k++) {
            Vector3 p = grouped ? m.zones[zi].Clamp(Vector3Add(c, {Rand(-2, 2), Rand(-1, 1), Rand(-2, 2)})) : RandomIn(*this, m.zones[zi]);
            int ai = Spawn(sp, p, zi);
            agents[ai].group = g;
        }
        spawnAlive[ri] = r.count;
        spawnT[ri] = r.respawnS;
    }
    UpdateFields(0);
}

void Ecosystem::SetTide(int t) {
    tide = std::max(1, t);
    // HP rises 8% per tide for anything spawned from now on; living beasts keep what they have (the design's
    // "HP rises 8% per tide" applies to each tide's population as it is replenished)
}

int Ecosystem::AddDiver(int slot, Vector3 pos) {
    int zi = ZoneAt(pos);
    Agent a;
    a.sp = diverSpecies;
    a.diver = slot;
    a.pos = a.home = pos;
    a.zone = a.homeZone = zi < 0 ? 0 : zi;
    a.hpMax = a.hp = eng->C("player_hp", 100);
    a.st = State::Graze;
    agents.push_back(a);
    return (int)agents.size() - 1;
}

// ---------------------------------------------------------------- blood, noise, death
void Ecosystem::AddBlood(Vector3 pos, float amount) { if (amount > 0 && !suppressBlood) scent.Add(pos, amount * bloodMult); }

void Ecosystem::AddNoise(Vector3 pos, float noise, bool explosion) {
    float field = explosion ? eng->C("noise_explosion", 30) : noise * 10.0f * eng->C("noise_gunshot_base", 1);
    sound.Add(pos, field);
    int zi = ZoneAt(pos);
    if (zi < 0 || noise <= 0) return;
    int reg = map->zones[zi].alarmRegion;
    float mult = map->alarmRegions[reg].mult * (explosion ? eng->C("alarm_explosion_mult", 3) : eng->C("alarm_gunshot_mult", 1));
    alarm[reg] += noise * mult;
}

void Ecosystem::Damage(int ai, float dmg, int attacker, bool melee, bool weakPoint) {
    if (ai < 0 || ai >= (int)agents.size()) return;
    Agent& a = agents[ai];
    if (!a.alive) return;
    const Species& s = map->species[a.sp];
    if (a.diver >= 0 && onDiverHit) { onDiverHit(ai, attacker, dmg); return; }
    if (weakPoint) dmg *= 2;
    a.hp -= dmg;
    a.wound = std::clamp(1 - a.hp / a.hpMax, 0.0f, 1.0f);
    // weak-point hits bleed 2x (Damage model)
    AddBlood(a.pos, s.size * (weakPoint ? 2.0f : 1.0f) * (melee ? eng->C("blood_melee_mult", 0.5f) : 1.0f));
    if (a.hp <= 0) { Kill(ai, attacker, melee); return; }
    // cephalopods ink when attacked (the Reef Squid "inks when threatened"; the octopus hides in its den):
    // the attacker loses it in the cloud and gives up, and it flees home
    if (s.cls == "Cephalopod" && attacker >= 0 && attacker < (int)agents.size() && agents[attacker].alive && agents[attacker].diver < 0 && Rand() < 0.85f) {
        Agent& att = agents[attacker];
        att.st = State::Return; att.goal = att.home; att.target = -1; att.stateT = 0; att.cooldown = 3;
        att.lostPrey = ai; att.lostPreyT = 60;
        a.st = State::Flee; a.target = attacker; a.goal = a.home; a.stateT = 0;
        return;
    }
    // a territorial defender's blow (not a hunt) drives an intruder off rather than starting a fight to the death:
    // the intruder leaves the radius (Territory layer: "Leave the radius and it forgets you")
    if (attacker >= 0 && attacker < (int)agents.size() && agents[attacker].alive && agents[attacker].st == State::Defend && a.diver < 0 && !Eats(agents[attacker].sp, a.sp)) {
        a.st = State::Flee; a.target = attacker; a.stateT = 0; a.goal = a.home;
        return;
    }
    // anything hurt that can fight back turns on what hurt it; fearful things flee
    if (attacker >= 0 && attacker < (int)agents.size() && agents[attacker].alive && a.diver < 0) {
        if (s.fear > 0.5f && a.hp < a.hpMax * 0.6f) { a.st = State::Flee; a.target = attacker; a.stateT = 0; }
        else if (s.aggression > 0.3f || s.isEnemy) { a.st = State::Defend; a.target = attacker; a.targetCorpse = false; a.stateT = 0; }
    }
}

void Ecosystem::Kill(int ai, int killer, bool melee) {
    Agent& a = agents[ai];
    if (!a.alive) return;
    a.alive = false;
    a.st = State::Dead;
    const Species& s = map->species[a.sp];
    deathsBySpecies[a.sp]++;
    bool byPlayer = killer >= 0 && killer < (int)agents.size() && agents[killer].diver >= 0;
    if (killer == -3) byPlayer = true; // scripted player kills in the headless tools
    if (byPlayer) killsBySpecies[a.sp]++;
    else if (killer >= 0 && killer < (int)agents.size()) eatenBy[{agents[killer].sp, a.sp}]++;
    float burst = s.bloodDeath * eng->C("blood_corpse_burst", 1) * (melee ? eng->C("blood_melee_mult", 0.5f) : 1.0f);
    if (a.diver < 0) {
        AddBlood(a.pos, burst);
        Corpse c;
        c.sp = a.sp; c.pos = a.pos; c.zone = a.zone; c.bloodLeft = burst; c.life = eng->CBy("blood_corpse_life_s", s.size - 1, 60);
        c.byPlayer = byPlayer;
        corpses.push_back(c);
    }
    // parasites fall off
    for (auto& o : agents) if (o.alive && o.host == ai) { o.host = -1; o.st = State::Graze; }
    // killing every cleaner in a region raises every host's aggression map-wide (Symbiosis)
    if (s.Cleaner()) {
        bool any = false;
        for (const auto& o : agents) if (o.alive && map->species[o.sp].Cleaner() && map->zones[o.zone].alarmRegion == map->zones[a.zone].alarmRegion) any = true;
        if (!any && byPlayer) { cleanerRage = 300; Event("the last cleaner in the region is dead: hosts are angry map-wide"); }
    }
    if (a.squad >= 0 && a.squad < (int)squads.size()) {
        auto& mem = squads[a.squad].members;
        mem.erase(std::remove(mem.begin(), mem.end(), ai), mem.end());
        if (mem.empty()) squads[a.squad].alive = false;
    }
    if (onDeath) onDeath(ai, killer);
}

// ---------------------------------------------------------------- the fields (scent and sound)
void Ecosystem::UpdateFields(float dt) {
    // Scent at scent_update_hz (1 Hz): diffusion among water cells of the same zone, advection by the zone's
    // current, exchange through each link's two mouths, decay, and the floor that keeps the grid sparse.
    scentT += dt;
    float period = 1.0f / std::max(0.1f, eng->C("scent_update_hz", 1));
    bool doScent = scentT >= period || dt == 0;
    if (doScent) {
        float step = dt == 0 ? period : scentT;
        scentT = 0;
        Field& f = scent;
        float diff = eng->C("scent_diffusion", 0.15f) * step;     // share spread to the 6 neighbours
        float adv = eng->C("scent_current_advect", 0.8f);
        float decay = DecayRate() * step;
        float floorV = eng->C("scent_floor", 0.5f);
        std::fill(f.tmp.begin(), f.tmp.end(), 0.0f);
        // each zone's drain: the passage its water leaves by (the link with the most flow out of it)
        std::vector<const Link*> drain(map->zones.size(), nullptr);
        std::vector<float> drainFlow(map->zones.size(), 0);
        for (const auto& l : map->links) {
            if (l.flow > drainFlow[l.from]) { drainFlow[l.from] = l.flow; drain[l.from] = &l; }
            if (-l.flow > drainFlow[l.to]) { drainFlow[l.to] = -l.flow; drain[l.to] = &l; }
        }
        auto zoneFlowDir = [&](int zi, int x, int y, int z) -> Vector3 {
            Vector3 fl = map->zones[zi].flow;
            float speed = Vector3Length(fl);
            if (speed < 0.01f) return Vector3Zero();
            const Link* L = drain[zi];
            if (!L) return fl;
            Vector3 mouth = L->from == zi ? L->a : L->b;
            Vector3 c{f.origin.x + (x + 0.5f) * f.cell, f.origin.y + (y + 0.5f) * f.cell, f.origin.z + (z + 0.5f) * f.cell};
            Vector3 d = Vector3Subtract(mouth, c);
            float len = Vector3Length(d);
            if (len < 0.5f) return fl;
            // mostly toward the drain, a little of the zone's own set, so eddies still cover the room
            Vector3 dir = Vector3Normalize(Vector3Add(Vector3Scale(d, 0.8f / len), Vector3Scale(fl, 0.2f / speed)));
            return Vector3Scale(dir, speed);
        };
        static const int D[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (int z = 0; z < f.nz; z++) for (int y = 0; y < f.ny; y++) for (int x = 0; x < f.nx; x++) {
            int i = f.Idx(x, y, z);
            float val = f.v[i];
            if (val <= 0 || f.zone[i] < 0) continue;
            int zi = f.zone[i];
            float keep = val;
            // diffusion
            float share = val * diff / 6.0f;
            for (const auto& d : D) {
                int X = x + d[0], Y = y + d[1], Z = z + d[2];
                if (X < 0 || Y < 0 || Z < 0 || X >= f.nx || Y >= f.ny || Z >= f.nz) continue;
                int j = f.Idx(X, Y, Z);
                if (f.zone[j] != zi) continue;
                f.tmp[j] += share;
                keep -= share;
            }
            // advection: the zone's current carries the cell downstream. Where the zone drains through a
            // passage (a link with flow out of it), the water heads for that doorway; otherwise it follows the
            // zone's own current vector. Semi-Lagrangian: the blood lands where the current takes it this step.
            Vector3 fl = zoneFlowDir(zi, x, y, z);
            float sp = Vector3Length(fl);
            if (sp > 0.01f) {
                float dist = sp * adv * step;               // metres travelled this update
                Vector3 c{f.origin.x + (x + 0.5f) * f.cell, f.origin.y + (y + 0.5f) * f.cell, f.origin.z + (z + 0.5f) * f.cell};
                Vector3 dst = Vector3Add(c, Vector3Scale(fl, dist / sp));
                int X, Y, Z;
                if (f.Cell(dst, X, Y, Z) && f.zone[f.Idx(X, Y, Z)] == zi) {
                    int j = f.Idx(X, Y, Z);
                    if (j != i) { float mv = keep * std::min(1.0f, dist / f.cell); f.tmp[j] += mv; keep -= mv; }
                    else {
                        // not a whole cell yet: move the fraction to the next cell along the flow
                        Vector3 nb = Vector3Add(c, Vector3Scale(fl, f.cell / sp));
                        if (f.Cell(nb, X, Y, Z) && f.zone[f.Idx(X, Y, Z)] == zi && f.Idx(X, Y, Z) != i) { float mv = keep * std::min(0.95f, dist / f.cell); f.tmp[f.Idx(X, Y, Z)] += mv; keep -= mv; }
                    }
                } else {
                    // the current runs into a wall: slide along it toward the drain (the nearest cell along the flow's main axis)
                    Vector3 nb = Vector3Add(c, fabsf(fl.x) > fabsf(fl.z) ? Vector3{fl.x > 0 ? f.cell : -f.cell, 0, 0} : Vector3{0, 0, fl.z > 0 ? f.cell : -f.cell});
                    if (f.Cell(nb, X, Y, Z) && f.zone[f.Idx(X, Y, Z)] == zi) { float mv = keep * std::min(0.8f, dist / f.cell); f.tmp[f.Idx(X, Y, Z)] += mv; keep -= mv; }
                }
            }
            f.tmp[i] += keep;
        }
        // links: water through each passage. Each mouth's neighbourhood exchanges toward the other side, and a
        // link with flow (the current running through a doorway) carries blood downstream much faster.
        for (const auto& l : map->links) {
            float ra = f.cell * 1.6f;
            for (int dir = 0; dir < 2; dir++) {
                Vector3 src = dir == 0 ? l.a : l.b, dst = dir == 0 ? l.b : l.a;
                float along = dir == 0 ? l.flow : -l.flow;
                float rate = (0.06f + std::max(0.0f, along) * 0.6f) * step;
                // gather from the source mouth's cells
                int sx, sy, sz;
                if (!f.Cell(src, sx, sy, sz)) continue;
                int r = (int)ceilf(ra / f.cell);
                float moved = 0;
                for (int dz = -r; dz <= r; dz++) for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
                    int X = sx + dx, Y = sy + dy, Z = sz + dz;
                    if (X < 0 || Y < 0 || Z < 0 || X >= f.nx || Y >= f.ny || Z >= f.nz) continue;
                    int j = f.Idx(X, Y, Z);
                    if (f.zone[j] != (dir == 0 ? l.from : l.to)) continue;
                    float mv = f.tmp[j] * std::min(0.85f, rate);
                    f.tmp[j] -= mv;
                    moved += mv;
                }
                if (moved > 0) {
                    int dx_, dy_, dz_;
                    if (f.Cell(dst, dx_, dy_, dz_) && f.zone[f.Idx(dx_, dy_, dz_)] >= 0) f.tmp[f.Idx(dx_, dy_, dz_)] += moved;
                    else f.Add(dst, moved);
                }
            }
        }
        for (size_t i = 0; i < f.v.size(); i++) {
            float val = f.tmp[i] * (1 - decay);
            f.v[i] = val < floorV * 0.02f ? 0 : val;
        }
        f.BuildSat();
    }
    // Sound: fast diffusion and a 20%/s decay, updated every tick it's asked for (its reactions are immediate).
    if (dt > 0) {
        Field& s = sound;
        float decay = std::min(1.0f, eng->C("sound_cell_decay", 0.2f) * dt * 5); // 20%/s, applied in larger steps below
        static float acc = 0;
        acc += dt;
        if (acc >= 0.2f) {
            float diff = eng->C("sound_diffusion", 0.3f) * acc;
            std::fill(s.tmp.begin(), s.tmp.end(), 0.0f);
            for (int z = 0; z < s.nz; z++) for (int y = 0; y < s.ny; y++) for (int x = 0; x < s.nx; x++) {
                int i = s.Idx(x, y, z);
                float val = s.v[i];
                if (val <= 0.01f) continue;
                float share = val * diff / 6.0f, keep = val;
                const int D[6][3] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
                for (const auto& d : D) {
                    int X = x + d[0], Y = y + d[1], Z = z + d[2];
                    if (X < 0 || Y < 0 || Z < 0 || X >= s.nx || Y >= s.ny || Z >= s.nz) continue;
                    int j = s.Idx(X, Y, Z);
                    if (s.zone[j] < 0) continue;
                    s.tmp[j] += share; keep -= share;
                }
                s.tmp[i] += keep;
            }
            float k = powf(1 - eng->C("sound_cell_decay", 0.2f), acc);
            for (size_t i = 0; i < s.v.size(); i++) s.v[i] = s.tmp[i] * k;
            acc = 0;
            s.BuildSat();
        }
        (void)decay;
    }
}

// ---------------------------------------------------------------- perception
int Ecosystem::FindPrey(const Agent& a, int idx, float range) const {
    const Species& s = map->species[a.sp];
    int best = -1;
    float bestScore = 0;
    for (int i = 0; i < (int)agents.size(); i++) {
        if (i == idx || i == a.lostPrey) continue;
        const Agent& o = agents[i];
        if (!o.alive || o.host >= 0) continue;
        float w = 0;
        for (const auto& pw : map->diet[a.sp].prey) if (pw.first == o.sp || (pw.first == -2 && o.diver >= 0)) { w = pw.second; break; }
        if (w <= 0) continue;
        const Species& os = map->species[o.sp];
        // a predator eats prey up to 60% of its size, or larger when it hunts in a school or pack
        bool group = s.social == "pack" || s.social == "school";
        if (o.diver < 0 && os.size > s.size && !group) continue;
        float d = Vector3Distance(a.pos, o.pos);
        float r = range;
        if (os.Has("camouflage") && o.st == State::Rest) r *= 0.3f;
        if (d > r) continue;
        // prefer the wounded, the preferred, the near (Persistent injuries / diet weights)
        float score = w * (1.5f - d / r) * (1 + o.wound);
        if (score > bestScore) { bestScore = score; best = i; }
    }
    return best;
}
int Ecosystem::FindThreat(const Agent& a, int idx) const {
    const Species& s = map->species[a.sp];
    float r = std::max(s.sight, s.electro) * 0.8f;
    int best = -1;
    float bd = r;
    for (int i = 0; i < (int)agents.size(); i++) {
        if (i == idx) continue;
        const Agent& o = agents[i];
        if (!o.alive) continue;
        bool threat = Eats(o.sp, a.sp) && (o.st == State::Hunt || o.st == State::Investigate || map->species[o.sp].size >= s.size + 2);
        if (s.isEnemy && map->species[o.sp].size >= map->faction.fleeFromSize && !map->species[o.sp].isEnemy && o.diver < 0) threat = true;
        if (!threat) continue;
        float d = Vector3Distance(a.pos, o.pos);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}
int Ecosystem::FindCorpse(const Agent& a, float range) const {
    int best = -1;
    float bd = range;
    for (int i = 0; i < (int)corpses.size(); i++) {
        const Corpse& c = corpses[i];
        if (!c.active) continue;
        float d = Vector3Distance(a.pos, c.pos);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

// ---------------------------------------------------------------- decisions (10 Hz)
static float HuntWeight(const EngineData& e, float hunger) {
    // hunt_weight_at_hunger: weights at hunger 0 / 0.5 / 0.8 / 1
    float w0 = e.CBy("hunt_weight_at_hunger", 0), w1 = e.CBy("hunt_weight_at_hunger", 1), w2 = e.CBy("hunt_weight_at_hunger", 2), w3 = e.CBy("hunt_weight_at_hunger", 3, 1);
    if (hunger < 0.5f) return w0 + (w1 - w0) * hunger / 0.5f;
    if (hunger < 0.8f) return w1 + (w2 - w1) * (hunger - 0.5f) / 0.3f;
    return w2 + (w3 - w2) * std::min(1.0f, (hunger - 0.8f) / 0.2f);
}

void Ecosystem::Decide(Agent& a, int idx) {
    const Species& s = map->species[a.sp];
    const Zone& home = map->zones[a.homeZone];
    if (a.diver >= 0) return;          // the game layer drives divers
    if (a.stun > 0 || a.held > 0) return;
    if (decideHook && decideHook(a, idx)) return;
    // symbiosis: hosts near a cleaner are calmer
    a.aggrMod = 0;
    if (!s.Cleaner() && s.size >= 2) {
        float cr = eng->C("cleaner_calm_radius_m", 6);
        for (const auto& o : agents) if (o.alive && map->species[o.sp].Cleaner() && Vector3Distance(o.pos, a.pos) < cr) { a.aggrMod = -0.2f; break; }
    }
    // parasites ride their host
    if (s.Parasite()) {
        if (a.host >= 0 && agents[a.host].alive) { a.st = State::Attached; return; }
        a.host = -1;
        for (int i = 0; i < (int)agents.size(); i++) {
            const Agent& o = agents[i];
            if (!o.alive || i == idx || map->species[o.sp].size < 3 || map->species[o.sp].Parasite()) continue;
            if (Vector3Distance(o.pos, a.pos) < 1.2f) { a.host = i; a.st = State::Attached; return; }
        }
    }
    if (a.st == State::Feed) return;   // finishes its meal (Move handles it)
    // 1. danger: flee what eats you, or loud noise if fearful, or low HP
    float fear = s.fear;
    if (fear >= 0.3f) {
        int t = FindThreat(a, idx);
        if (t >= 0) { a.st = State::Flee; a.target = t; a.targetCorpse = false; a.stateT = 0; return; }
    }
    float snd = sound.At(a.pos);
    if (fear > 0.6f && snd > eng->C("fear_flee_threshold", 15)) {
        Vector3 c;
        sound.BoxSum(a.pos, s.hearing * 0.5f, &c);
        a.st = State::Flee; a.target = -1; a.goal = home.Clamp(Vector3Add(a.pos, Vector3Scale(Vector3Normalize(Vector3Subtract(a.pos, c)), 10)));
        a.stateT = 0;
        return;
    }
    if (a.hp < a.hpMax * eng->C("flee_hp_fraction", 0.2f) && fear > 0.3f) { a.st = State::Flee; a.target = -1; a.goal = a.home; return; }
    if (a.st == State::Flee && a.stateT < 3) return; // keep running a moment
    // 2. defending: a territorial beast that's been hit, or anything big in its radius
    if (a.st == State::Defend && a.target >= 0 && a.target < (int)agents.size() && agents[a.target].alive && a.stateT < 12) {
        // territorial defence stops when the intruder is out of the radius (it was only ever a warning)
        if (s.defendR > 0 && Vector3Distance(agents[a.target].pos, a.home) > s.defendR * 1.5f && agents[a.target].diver < 0) { a.st = State::Return; a.goal = a.home; a.stateT = 0; }
        return;
    }
    if (s.defendR > 0) {
        for (int i = 0; i < (int)agents.size(); i++) {
            const Agent& o = agents[i];
            if (!o.alive || i == idx || o.sp == a.sp) continue;
            const Species& os = map->species[o.sp];
            bool intruder = o.diver >= 0 || os.isEnemy || (os.size >= s.size && !Eats(o.sp, a.sp) && os.tier >= 3);
            if (!intruder) continue;
            if (Vector3Distance(o.pos, a.home) < s.defendR) { a.st = State::Defend; a.target = i; a.targetCorpse = false; a.stateT = 0; return; }
        }
    }
    // 3. eating: hunting live prey when hungry, scavenging corpses
    bool fed = a.fedT > 0;
    float hw = HuntWeight(*eng, a.hunger) + Aggression(a) * 0.3f;
    float huntAt = s.tier >= 4 ? 0.7f : 0.5f;   // hunger weight 0.3-0.7 (hunt_weight_at_hunger): apex eat rarely
    if (!fed && !map->diet[a.sp].prey.empty() && hw > 0.35f && a.hunger >= huntAt) {
        float range = std::max(s.sight, s.electro);
        if (s.archetype == "Ambusher") range = std::min(range, 2.5f);   // strikes anything within 2 m
        int p = FindPrey(a, idx, range);
        // big prey (within one size class of the hunter) only when it's starving
        if (p >= 0 && agents[p].diver < 0 && map->species[agents[p].sp].size >= s.size - 1 && a.hunger < 0.9f) p = -1;
        if (p >= 0) { a.st = State::Hunt; a.target = p; a.targetCorpse = false; a.stateT = 0; return; }
    }
    if (!fed && (map->diet[a.sp].corpse > 0 || s.Scavenger()) && a.hunger > 0.25f) {
        int c = FindCorpse(a, std::max(s.scent, s.sight));
        if (c >= 0) { a.st = State::Hunt; a.target = c; a.targetCorpse = true; a.stateT = 0; return; }
    }
    // 4. blood in the water: follow the gradient once it's past this species' threshold (hysteresis 70%)
    if (s.bloodThreshold > 0) {
        Vector3 c = a.pos;
        float reading = Smell(a.pos, a.zone, s.scent * eng->C("sample_range_mult", 1), &c);
        float need = a.st == State::Investigate ? s.bloodThreshold * eng->C("threshold_hysteresis", 0.7f) : s.bloodThreshold;
        // "A well-fed predator ignores prey": fed, it answers only blood well past its threshold (3x)
        if (fed) need *= 3;
        if (reading >= need && a.stateT < eng->C("investigate_timeout_s", 30) + (a.st == State::Investigate ? 0 : 1e9f)) {
            if (a.st != State::Investigate) {
                a.stateT = 0;
                if (s.tier >= 3 || s.size >= 3) Event(s.name + " smells blood (" + std::to_string((int)reading) + ") and investigates");
            }
            a.st = State::Investigate;
            a.goal = c;
            return;
        }
        if (a.st == State::Investigate && (reading < need || a.stateT >= eng->C("investigate_timeout_s", 30))) { a.st = State::Return; a.stateT = 0; }
    }
    // 5. sound: curious things come to look, aggressive things come to fight
    if (snd > 0 && s.hearing > 0) {
        Vector3 c;
        float heard = sound.BoxSum(a.pos, s.hearing * 0.5f, &c);
        (void)heard;
        float here = sound.At(c);
        if (Aggression(a) > 0.7f && here > eng->C("aggressive_attack_threshold", 20)) { a.st = State::Investigate; a.goal = c; a.stateT = 0; return; }
        if (s.curiosity > 0.5f && here > eng->C("curious_approach_threshold", 8) && here < 25) { a.st = State::Investigate; a.goal = c; a.stateT = 0; return; }
    }
    // 6. otherwise live at home: graze, rest, patrol; apex beasts wander between zones from tide 10
    if (a.st == State::Investigate && a.stateT < 6) return;
    float calm = eng->C("return_home_calm_s", 20);
    if (a.zone != a.homeZone || Vector3Distance(a.pos, a.home) > std::max(8.0f, s.defendR * 2)) {
        bool wanderer = s.archetype == "Wanderer" || s.tier >= 4;
        if (!(wanderer && map->Tide(tide).apexWander == "yes") && (a.st != State::Return || a.stateT > calm)) { a.st = State::Return; a.goal = a.home; a.stateT = 0; return; }
    }
    if (s.Sessile() || s.archetype == "Ambusher") { a.st = State::Rest; a.goal = a.home; return; }
    if (a.st == State::Return && Vector3Distance(a.pos, a.home) > 3) return;
    if (a.st != State::Graze || Vector3Distance(a.pos, a.goal) < 1.5f || a.stateT > 12) {
        a.st = State::Graze;
        a.stateT = 0;
        // grazers head for a flora patch they eat; others drift around home
        int best = -1;
        float bd = 1e9f;
        for (int i = 0; i < (int)flora.size(); i++) {
            const FloraPatch& p = flora[i];
            if (p.zone != a.homeZone || p.units < 5) continue;
            bool eats = false;
            for (const auto& fi : map->diet[a.sp].floraItems) if (fi.first == map->flora[p.flora].name) eats = true;
            if (!eats) continue;
            float d = Vector3Distance(p.pos, a.pos);
            if (d < bd) { bd = d; best = i; }
        }
        if (best >= 0 && Rand() < 0.7f) a.goal = map->zones[a.homeZone].Clamp(Vector3Add(flora[best].pos, {Rand(-2, 2), Rand(0, 1.5f), Rand(-2, 2)}));
        else {
            bool wanderer = (s.archetype == "Wanderer" || s.tier >= 4) && map->Tide(tide).apexWander == "yes";
            int zi = a.homeZone;
            if (wanderer && Rand() < 0.3f) zi = (int)(Rand() * map->zones.size()) % (int)map->zones.size();
            a.goal = RandomIn(*this, map->zones[zi]);
            if (zi == a.homeZone) a.goal = Vector3Lerp(a.home, a.goal, 0.6f);
        }
    }
}

// ---------------------------------------------------------------- movement (every tick)
void Ecosystem::SteerTo(Agent& a, Vector3 goal, float speed, float dt) {
    int gz = ZoneAt(goal);
    if (gz < 0) gz = a.zone;
    Vector3 target = goal;
    if (gz != a.zone) {
        // cross the zone graph one passage at a time
        std::vector<int> path = ZonePath(a.zone, gz);
        if (path.size() >= 2) {
            int next = path[1];
            const Link* L = nullptr;
            for (const auto& l : map->links) if ((l.from == a.zone && l.to == next) || (l.to == a.zone && l.from == next)) { L = &l; break; }
            if (L) {
                Vector3 mouthHere = L->from == a.zone ? L->a : L->b, mouthThere = L->from == a.zone ? L->b : L->a;
                if (Vector3Distance(a.pos, mouthHere) < 1.2f) {
                    // through the passage
                    float gap = Vector3Distance(mouthHere, mouthThere);
                    Vector3 dir = Vector3Normalize(Vector3Subtract(mouthThere, a.pos));
                    a.pos = Vector3Add(a.pos, Vector3Scale(dir, std::min(gap + 0.1f, speed * dt * 1.5f + gap)));
                    if (Vector3Distance(a.pos, mouthThere) < 0.6f || map->zones[next].Contains(a.pos, 0.2f)) { a.pos = map->zones[next].Clamp(mouthThere); a.zone = next; }
                    return;
                }
                target = mouthHere;
            }
        } else target = map->zones[a.zone].Clamp(goal);
    }
    Vector3 to = Vector3Subtract(target, a.pos);
    float d = Vector3Length(to);
    Vector3 want = d > 0.05f ? Vector3Scale(to, speed / d) : Vector3Zero();
    if (d < speed * 0.3f) want = Vector3Scale(want, d / (speed * 0.3f + 1e-4f));
    float acc = std::min(1.0f, dt / std::max(0.05f, eng->M("accel_s", 0.4f)));
    a.vel = Vector3Lerp(a.vel, want, acc);
    a.pos = Vector3Add(a.pos, Vector3Scale(a.vel, dt));
    a.pos = map->zones[a.zone].Clamp(a.pos, 0.3f);
}

void Ecosystem::Move(Agent& a, int idx, float dt) {
    const Species& s = map->species[a.sp];
    a.stateT += dt;
    if (a.cooldown > 0) a.cooldown -= dt;
    if (a.fedT > 0) a.fedT -= dt;
    if (a.lostPreyT > 0) { a.lostPreyT -= dt; if (a.lostPreyT <= 0) a.lostPrey = -1; }
    if (a.stun > 0) { a.stun -= dt; return; }
    if (a.held > 0) { a.held -= dt; return; }
    if (a.diver >= 0) return;
    if (s.social == "swarm" && a.hp < a.hpMax) { a.hp = std::min(a.hpMax, a.hp + a.hpMax * 0.01f * dt); a.wound = 1 - a.hp / a.hpMax; if (a.hp > a.hpMax * 0.9f) a.wound = 0; }
    // hunger rises by size (per minute)
    a.hunger = std::min(1.0f, a.hunger + eng->CBy("hunger_rate", s.size - 1, 0.3f) / 60.0f * dt);
    // wounds bleed; a fleeing beast that reaches home stops after 10 s
    if (a.wound > 0) {
        float rate = s.bloodPerS * std::max(a.wound, eng->C("blood_wound_fraction_min", 0.1f));
        if (a.st == State::Flee) rate *= eng->C("flee_trail_blood_mult", 0.5f);
        if (Vector3Distance(a.pos, a.home) < 3) { a.fleeBleedT += dt; if (a.fleeBleedT > 10) a.wound = 0; }
        AddBlood(a.pos, rate * dt);
    }
    float speed = s.speed;
    switch (a.st) {
        case State::Attached: {
            if (a.host < 0 || !agents[a.host].alive) { a.st = State::Graze; a.host = -1; break; }
            Agent& h = agents[a.host];
            a.pos = Vector3Add(h.pos, {0.3f, 0.2f, 0});
            a.zone = h.zone;
            // a lamprey bleeds its host: the host becomes a blood source that draws predators
            for (const auto& at : map->attacks) if (at.beast == s.name && at.effect.find("bleeds") != std::string::npos) { AddBlood(h.pos, FirstNumOf(at.effect) * dt); break; }
            return;
        }
        case State::Feed: {
            a.eatT -= dt;
            if (a.targetCorpse && a.target >= 0 && a.target < (int)corpses.size() && corpses[a.target].active) {
                Corpse& c = corpses[a.target];
                float rate = s.Scavenger() ? eng->CBy("blood_scavenger_rate", std::min(3, s.size - 1), 0.5f) : c.bloodLeft / std::max(0.5f, a.eatT + dt);
                c.bloodLeft -= rate * dt;
                if (c.bloodLeft <= 0) c.active = false;
            }
            if (a.eatT <= 0 || (a.targetCorpse && (a.target < 0 || a.target >= (int)corpses.size() || !corpses[a.target].active))) {
                a.hunger = 0;
                a.fedT = eng->CBy("fed_duration_s", s.size - 1, 45);
                a.st = State::Return;
                a.goal = a.home;
                a.stateT = 0;
                a.target = -1;
            }
            return;
        }
        case State::Hunt:
        case State::Defend: {
            Vector3 tp;
            bool corpse = a.targetCorpse;
            if (corpse) {
                if (a.target < 0 || a.target >= (int)corpses.size() || !corpses[a.target].active) { a.st = State::Graze; return; }
                tp = corpses[a.target].pos;
            } else {
                if (a.target < 0 || a.target >= (int)agents.size() || !agents[a.target].alive) { a.st = State::Graze; a.target = -1; return; }
                tp = agents[a.target].pos;
            }
            float giveUp = s.archetype == "Wanderer" ? 200.0f : eng->C("hunt_give_up_distance_m", 60);
            if (Vector3Distance(a.pos, a.home) > giveUp && s.archetype != "Wanderer" && !s.isEnemy) { a.st = State::Return; a.goal = a.home; a.stateT = 0; return; }
            if (a.stateT > 25) { a.st = State::Return; a.goal = a.home; a.stateT = 0; return; }
            float sp = speed * (a.st == State::Hunt ? 1.25f : 1.0f);
            // a territorial warning display first (defend_escalation_s), holding at the radius's edge
            if (a.st == State::Defend && !corpse && s.defendR > 0 && a.stateT < eng->C("defend_escalation_s", 3)) { SteerTo(a, Vector3Lerp(a.home, tp, 0.5f), speed * 0.5f, dt); return; }
            SteerTo(a, tp, sp, dt);
            float reach = 0.6f + 0.25f * s.size;
            for (const auto& at : map->attacks) if (at.beast == s.name) reach = std::max(reach, std::min(at.range, 3.0f));
            if (Vector3Distance(a.pos, tp) <= reach) {
                if (corpse) {
                    a.st = State::Feed;
                    a.eatT = eng->CBy("eat_time_s", map->species[corpses[a.target].sp].size - 1, 5);
                    corpses[a.target].feeders++;
                    return;
                }
                Agent& prey = agents[a.target];
                const Species& ps = map->species[prey.sp];
                if (a.cooldown > 0) return;
                float dmg = 0;
                float cd = 1.5f;
                for (const auto& at : map->attacks) if (at.beast == s.name && at.damage > dmg) { dmg = at.damage; cd = std::max(0.5f, at.cooldown); }
                if (dmg <= 0) dmg = 10.0f * s.size * s.size;
                dmg *= map->Tide(tide).dmgMult;
                a.cooldown = cd;
                // Not every strike lands: prey escapes by size and by nature (a feather duster retracts, a fearful
                // fish bolts). The chance rises with how much bigger the hunter is.
                if (a.st == State::Hunt && prey.diver < 0) {
                    float catchP = std::clamp(0.35f + 0.18f * (s.size - ps.size), 0.12f, 0.9f);
                    if (ps.Sessile()) catchP *= 0.12f;          // retracts into its tube the moment anything comes near
                    if (ps.Has("armored")) catchP *= 0.5f;
                    if (ps.Has("camouflage") && prey.st == State::Rest) catchP *= 0.5f;
                    if (s.social == "pack" || s.social == "school") catchP = std::min(0.95f, catchP * 1.3f);
                    if (Rand() > catchP) {
                        // missed: the prey flees if it can, and the hunter needs a moment
                        if (ps.fear > 0.2f && !ps.Sessile()) { prey.st = State::Flee; prey.target = idx; prey.stateT = 0; }
                        if (Rand() < 0.35f) { a.st = State::Return; a.goal = a.home; a.stateT = 0; a.target = -1; }
                        return;
                    }
                }
                if (ps.social == "swarm" && a.st == State::Hunt && prey.diver < 0) {
                    // a swarm is one agent with many bodies: a meal thins it rather than ending it
                    prey.hp -= prey.hpMax * 0.15f;
                    prey.wound = std::clamp(1 - prey.hp / prey.hpMax, 0.0f, 1.0f);
                    a.st = State::Feed; a.targetCorpse = false; a.target = -1; a.eatT = eng->CBy("eat_time_s", 0, 2);
                    if (prey.hp <= 0) Kill(a.target >= 0 ? a.target : (int)(&prey - &agents[0]), idx);
                    return;
                }
                if (ps.size < s.size && a.st == State::Hunt && prey.diver < 0) {
                    // a predator takes smaller prey outright, then eats the corpse where it fell
                    Kill(a.target, idx);
                    int ci = (int)corpses.size() - 1;
                    if (ci >= 0) { a.target = ci; a.targetCorpse = true; a.st = State::Feed; a.eatT = eng->CBy("eat_time_s", ps.size - 1, 5); corpses[ci].feeders++; }
                    if (s.tier >= 3) Event(s.name + " takes a " + ps.name);
                } else {
                    Damage(a.target, dmg, idx);
                    if (!agents[a.target].alive && a.st == State::Hunt) {
                        int ci = (int)corpses.size() - 1;
                        if (ci >= 0) { a.target = ci; a.targetCorpse = true; a.st = State::Feed; a.eatT = eng->CBy("eat_time_s", ps.size - 1, 5); }
                    }
                }
            }
            return;
        }
        case State::Flee: {
            Vector3 away = a.goal;
            if (a.target >= 0 && a.target < (int)agents.size() && agents[a.target].alive) {
                Vector3 dir = Vector3Subtract(a.pos, agents[a.target].pos);
                if (Vector3Length(dir) < 0.01f) dir = {1, 0, 0};
                away = Vector3Add(a.pos, Vector3Scale(Vector3Normalize(dir), 8));
                // cornered: head for home, or through a passage
                if (!map->zones[a.zone].Contains(away, -0.5f)) away = map->zones[a.zone].Clamp(away);
            }
            SteerTo(a, away, speed * 1.4f, dt);
            if (a.stateT > 6) { a.st = State::Return; a.goal = a.home; a.stateT = 0; }
            return;
        }
        case State::Investigate:
            SteerTo(a, a.goal, speed * 1.1f, dt);
            return;
        case State::Return:
            SteerTo(a, a.home, speed, dt);
            if (Vector3Distance(a.pos, a.home) < 2) { a.st = State::Graze; a.stateT = 0; }
            return;
        case State::Rest:
            a.vel = Vector3Scale(a.vel, 0.9f);
            return;
        case State::Graze:
        default: {
            SteerTo(a, a.goal, speed * 0.45f, dt);
            // grazing eats the patch it's on
            float rate = eng->C("graze_rate", 0.5f);
            for (auto& p : flora) if (p.zone == a.zone && p.units > 0 && Vector3Distance(p.pos, a.pos) < 3) {
                bool eats = false;
                for (const auto& fi : map->diet[a.sp].floraItems) if (fi.first == map->flora[p.flora].name) eats = true;
                if (eats) { p.units -= rate * dt; a.hunger = std::max(0.0f, a.hunger - dt * 0.02f); if (p.units <= 0) { p.units = 0; p.regrowT = map->flora[p.flora].regrowDelay > 0 ? map->flora[p.flora].regrowDelay : eng->C("flora_regrow_delay_s", 60); } }
                break;
            }
            if (map->diet[a.sp].plankton > 0) a.hunger = std::max(0.0f, a.hunger - dt * 0.01f); // filter feeders graze the water itself
            return;
        }
    }
}

void Ecosystem::Schooling(Agent& a, int idx, float dt) {
    (void)idx; (void)dt; (void)a;
}

// ---------------------------------------------------------------- population (Spawn sheet) and flora
void Ecosystem::Population(float dt) {
    for (auto& p : flora) {
        const Flora& f = map->flora[p.flora];
        if (p.regrowT > 0) { p.regrowT -= dt; continue; }
        p.units = std::min(eng->C("flora_unit_capacity", 100), p.units + f.regrowPerS * dt);
    }
    popT += dt;
    float interval = eng->C("spawn_check_interval_s", 10);
    if (popT < interval) return;
    float step = popT;
    popT = 0;
    std::vector<int> alive(map->spawns.size(), 0);
    // who belongs to which row: species + zone
    for (int ri = 0; ri < (int)map->spawns.size(); ri++) {
        const SpawnRow& r = map->spawns[ri];
        int sp = map->SpeciesIndex(r.species), zi = map->ZoneIndex(r.zone);
        if (sp < 0) continue;
        for (const auto& a : agents) if (a.alive && a.sp == sp && (zi < 0 || a.homeZone == zi)) alive[ri]++;
    }
    for (int ri = 0; ri < (int)map->spawns.size(); ri++) {
        const SpawnRow& r = map->spawns[ri];
        if (r.respawnS <= 0) continue;     // bosses and placed ambushers never respawn
        int sp = map->SpeciesIndex(r.species), zi = map->ZoneIndex(r.zone);
        if (sp < 0) continue;
        if (zi < 0) zi = std::max(0, map->ZoneIndex(map->species[sp].homeZone));
        float cap = r.count * powf(r.capMult, (float)(tide - 1));
        // grazer capacity follows the local flora (0.5-1.5x)
        if (map->diet[sp].flora > 0.3f) {
            float units = 0, full = 0;
            for (const auto& p : flora) if (p.zone == zi) { units += p.units; full += eng->C("flora_unit_capacity", 100); }
            if (full > 0) cap *= std::clamp(units / full * 1.5f, 0.5f, 1.5f) * eng->C("capacity_flora_mult", 1);
        }
        if (alive[ri] >= (int)ceilf(cap)) { spawnT[ri] = r.respawnS; continue; }
        spawnT[ri] -= step;
        if (spawnT[ri] > 0) continue;
        spawnT[ri] = r.respawnS * powf(eng->C("respawn_tide_mult", 0.97f), (float)(tide - 1));
        int n = std::max(1, std::min((int)ceilf(cap) - alive[ri], std::max(1, map->species[sp].groupSize / 4)));
        Vector3 c = RandomIn(*this, map->zones[zi]);
        int g = -1;
        for (const auto& a : agents) if (a.alive && a.sp == sp && a.homeZone == zi && a.group >= 0) { g = a.group; break; }
        for (int k = 0; k < n; k++) {
            int ai = Spawn(sp, map->zones[zi].Clamp(Vector3Add(c, {Rand(-2, 2), Rand(-1, 1), Rand(-2, 2)})), zi);
            agents[ai].group = g;
            agents[ai].fromRespawn = true;
        }
    }
    // compact: drop long-dead agents and spent corpses so the arrays stay small (indices are only held briefly)
    corpses.erase(std::remove_if(corpses.begin(), corpses.end(), [](const Corpse& c) { return !c.active && c.feeders == 0; }), corpses.end());
    for (auto& a : agents) if (a.targetCorpse && (a.target >= (int)corpses.size())) { a.target = -1; a.targetCorpse = false; if (a.st == State::Feed || a.st == State::Hunt) a.st = State::Return; }
}

// ---------------------------------------------------------------- the enemy alarm and squads
void Ecosystem::SpawnSquad(int region, bool hunt, int count, bool leader) {
    if (map->enemySpecies < 0 || map->faction.units.empty()) return;
    const Faction& f = map->faction;
    // the faction's entry point (at least 35 m from every diver)
    Vector3 at{};
    int zi = -1;
    for (const auto& p : map->pois) if (p.name == f.entryPoi && p.zone >= 0) { at = p.pos; zi = p.zone; }
    if (zi < 0) for (const auto& p : map->pois) if (p.type == "Entry" && p.zone >= 0) { at = p.pos; zi = p.zone; }
    if (zi < 0) { zi = (int)map->zones.size() - 1; at = map->zones[zi].Center(); }
    float minD = eng->C("alarm_min_distance_m", 35);
    for (const auto& a : agents) if (a.alive && a.diver >= 0 && Vector3Distance(a.pos, at) < minD) {
        // too close: the map's other faction entry, or the hunt staging point
        for (const auto& p : map->pois) if ((p.name == f.huntStaging || p.zoneName == f.huntStaging) && p.zone >= 0) { at = p.pos; zi = p.zone; }
    }
    at = map->zones[zi].Clamp(at);
    int size = tide >= 20 ? 5 : tide >= 10 ? 4 : 3;
    if (players <= 1) size = 2; else if (players <= 3) size = 3;
    if (count > 0) size = count;
    Squad sq;
    sq.region = region;
    sq.hunt = hunt;
    sq.goal = map->zones[map->alarmRegions[region].zones.empty() ? zi : map->alarmRegions[region].zones[0]].Center();
    int squadIdx = (int)squads.size();
    std::vector<std::string> comp = f.composition;
    if (leader) for (const auto& u : f.units) if (u.huntOnly) comp.insert(comp.begin(), u.unit);   // the Foreman leads a Hunt
    for (int k = 0; k < size && !comp.empty(); k++) {
        const std::string& un = comp[k % comp.size()];
        int ui = 0;
        for (int u = 0; u < (int)f.units.size(); u++) if (f.units[u].unit == un) ui = u;
        int ai = Spawn(map->enemySpecies, map->zones[zi].Clamp(Vector3Add(at, {Rand(-1.5f, 1.5f), 0, Rand(-1.5f, 1.5f)})), zi);
        Agent& e = agents[ai];
        e.unit = ui;
        e.squad = squadIdx;
        e.hpMax = e.hp = f.units[ui].hp * powf(eng->C("enemy_hp_growth", 1.08f), (float)(tide - 1));
        e.st = State::Investigate;
        e.goal = sq.goal;
        e.homeZone = zi;
        e.home = at;
        sq.members.push_back(ai);
    }
    squads.push_back(sq);
    squadsSpawned++;
    Event(std::string(hunt ? "HUNT: " : "") + f.name + " squad of " + std::to_string(size) + " enters at " + map->zones[zi].name + " (alarm in " + map->alarmRegions[region].name + ")");
}

void Ecosystem::AlarmUpdate(float dt) {
    float decay = eng->C("alarm_decay", 0.05f);
    for (auto& v : alarm) v *= powf(1 - decay, dt);
    alarmRollT += dt;
    float interval = eng->C("alarm_roll_interval_s", 5);
    if (alarmRollT < interval) return;
    alarmRollT = 0;
    const TideRow& tr = map->Tide(tide);
    int alive = 0;
    for (const auto& s : squads) if (s.alive && !s.hunt) alive++;
    int maxSquads = tide >= 20 ? 3 : 2;
    for (int r = 0; r < (int)alarm.size(); r++) {
        if (alarm[r] <= tr.alarmThreshold || alive >= maxSquads) continue;
        if (Rand() < tr.enemySpawnChance) {
            SpawnSquad(r, false);
            alarm[r] *= eng->C("alarm_reset_on_spawn", 0.5f);
            alive++;
        }
    }
}

// ---------------------------------------------------------------- the step
void Ecosystem::Step(float dt) {
    time += dt;
    if (cleanerRage > 0) cleanerRage -= dt;
    // corpses bleed and rot
    float trickle = eng->C("blood_corpse_trickle", 0.05f);
    for (auto& c : corpses) {
        if (!c.active) continue;
        c.age += dt;
        AddBlood(c.pos, c.bloodLeft * trickle * dt);
        if (c.age > c.life) c.active = false;
    }
    UpdateFields(dt);
    // decisions at 10 Hz, staggered
    for (int i = 0; i < (int)agents.size(); i++) {
        Agent& a = agents[i];
        if (!a.alive) continue;
        a.decideT -= dt;
        if (a.decideT <= 0) { a.decideT += 0.1f; Decide(a, i); }
    }
    // schools: cohesion and alignment toward each group's centroid (boids, the Constants sheet's weights)
    struct G { Vector3 c{}; Vector3 v{}; int n = 0; };
    std::vector<G> groups;
    for (const auto& a : agents) if (a.alive && a.group >= 0) {
        if ((int)groups.size() <= a.group) groups.resize(a.group + 1);
        groups[a.group].c = Vector3Add(groups[a.group].c, a.pos);
        groups[a.group].v = Vector3Add(groups[a.group].v, a.vel);
        groups[a.group].n++;
    }
    float coh = eng->C("school_cohesion", 0.6f), ali = eng->C("school_alignment", 0.8f);
    for (int i = 0; i < (int)agents.size(); i++) {
        Agent& a = agents[i];
        if (!a.alive) continue;
        Move(a, i, dt);
        if (a.group >= 0 && a.group < (int)groups.size() && groups[a.group].n > 1 && (a.st == State::Graze || a.st == State::Return)) {
            const G& g = groups[a.group];
            Vector3 c = Vector3Scale(g.c, 1.0f / g.n), v = Vector3Scale(g.v, 1.0f / g.n);
            Vector3 toC = Vector3Subtract(c, a.pos);
            a.vel = Vector3Add(a.vel, Vector3Scale(toC, coh * dt));
            a.vel = Vector3Lerp(a.vel, v, std::min(1.0f, ali * dt));
            // keep the school's members on the school's goal
            a.goal = Vector3Lerp(a.goal, c, 0.02f);
        }
    }
    Population(dt);
    AlarmUpdate(dt);
}

} // namespace rt
