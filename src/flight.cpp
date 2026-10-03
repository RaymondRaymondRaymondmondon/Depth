// The Flight's headless core (see flight.h): the founders, the island, the wind, the Founder's flight and fishing.
#include "flight.h"
#include "json.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fl {

// ---------------------------------------------------------------- data
std::string FlightDataDir() { return rt::DataDir() + "/../flight"; }
static Color ColorOf(const Json& a, Color def) { return a.IsArr() && a.a.size() >= 3 ? Color{(unsigned char)a[0].I(), (unsigned char)a[1].I(), (unsigned char)a[2].I(), 255} : def; }
const std::vector<FounderDef>& Founders() {
    static std::vector<FounderDef> v;
    if (!v.empty()) return v;
    Json j = LoadJsonFile(FlightDataDir() + "/flight_founders.json");
    for (const Json& r : j.a) {
        FounderDef d;
        d.key = r["key"].Str0(); d.name = r["name"].Str0(); d.bird = r["bird"].Str0();
        d.boost = r["boost"].Str0(); d.unique = r["unique"].Str0(); d.weakness = r["weakness"].Str0(); d.playstyle = r["playstyle"].Str0();
        const Json& s = r["stats"];
        d.cruise = s["cruise"].F(12); d.sprint = s["sprint"].F(20); d.stamina = s["stamina_s"].F(8); d.carry = s["carry"].I(3);
        d.talon = s["talon"].F(1); d.attack = s["attack"].F(20); d.hp = s["hp"].F(100);
        const Json& l = r["look"];
        d.span = l["wingspan"].F(1.5f); d.beak = l["beak"].F(0.1f); d.tail = l["tail"].F(0.5f); d.crest = l["crest"].F(0);
        d.back = ColorOf(l["back"], d.back); d.belly = ColorOf(l["belly"], d.belly); d.accent = ColorOf(l["accent"], d.accent);
        if (!d.key.empty()) v.push_back(d);
    }
    if (v.empty()) { FounderDef d; d.key = "taloned"; d.name = "The Taloned"; d.bird = "an osprey"; v.push_back(d); }   // (no data: one plain bird)
    return v;
}
int FounderIndex(const std::string& key) { const auto& v = Founders(); for (int i = 0; i < (int)v.size(); i++) if (v[i].key == key) return i; return -1; }

// ---------------------------------------------------------------- the island
static float Hash2(int x, int z, uint32_t s) { uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u + s * 2246822519u; h = (h ^ (h >> 13)) * 1274126177u; return ((h ^ (h >> 16)) & 0xFFFFFF) / 16777216.0f; }
static float ValueNoise(float x, float z, uint32_t s) {
    int xi = (int)floorf(x), zi = (int)floorf(z); float fx = x - xi, fz = z - zi;
    fx = fx * fx * (3 - 2 * fx); fz = fz * fz * (3 - 2 * fz);
    float a = Hash2(xi, zi, s), b = Hash2(xi + 1, zi, s), c = Hash2(xi, zi + 1, s), d = Hash2(xi + 1, zi + 1, s);
    return (a + (b - a) * fx) * (1 - fz) + (c + (d - c) * fx) * fz;
}
static float Fbm(float x, float z, uint32_t s) { float t = 0, a = 1, n = 0; for (int o = 0; o < 4; o++) { t += ValueNoise(x, z, s + o * 17) * a; n += a; a *= 0.5f; x *= 2.03f; z *= 2.03f; } return t / n; }
static float Smooth(float a, float b, float x) { float t = std::clamp((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3 - 2 * t); }

void Island::Generate(uint32_t s) {
    seed = s;
    n = 161; cell = 2; x0 = -160; z0 = -160;
    h.assign((size_t)n * n, 0);
    hill = {-14, 0, -26};
    for (int zi = 0; zi < n; zi++) for (int xi = 0; xi < n; xi++) {
        float x = x0 + xi * cell, z = z0 + zi * cell;
        // a coast near 82 m (wobbling with noise), a low plateau, a hill to the north-west, the shelf falling away outside
        float wob = (Fbm(x * 0.02f, z * 0.02f, s) - 0.5f) * 18;
        float r = sqrtf(x * x + z * z) + wob;
        float plateau = 6 * (1 - Smooth(52, 88, r)) - 1.6f;
        float dh = sqrtf((x - hill.x) * (x - hill.x) + (z - hill.z) * (z - hill.z));
        float dome = 30 * expf(-(dh / 34) * (dh / 34));
        float land = plateau + dome * (1 - Smooth(60, 80, r)) + (Fbm(x * 0.06f, z * 0.06f, s + 5) - 0.5f) * 3 * (1 - Smooth(60, 85, r));
        float sea = -1.6f - 10 * Smooth(88, 150, r);
        float y = r < 88 ? land : sea;
        // the lagoon bay to the south: 2-3 m of water, opening onto the reef flat
        float bay = Smooth(44, 38, fabsf(x)) * Smooth(46, 54, z);
        y = y * (1 - bay) + std::min(y, -2.6f + 0.6f * Fbm(x * 0.1f, z * 0.1f, s + 9)) * bay;
        h[(size_t)zi * n + xi] = y;
    }
    hill.y = Height(hill.x, hill.z);
    // palms on the land, thinning up the hill; none on the beach's wet edge
    palms.clear();
    uint32_t rs = s * 9781u + 13;
    auto R = [&]() { rs = rs * 1664525u + 1013904223u; return (rs >> 8) / 16777216.0f; };
    for (int k = 0; k < 900 && palms.size() < 70; k++) {
        float x = -90 + 180 * R(), z = -90 + 180 * R();
        float y = Height(x, z);
        if (y < 0.9f || y > 20) continue;
        if (Normal(x, z).y < 0.82f) continue;
        bool close = false; for (const auto& p : palms) if ((p.x - x) * (p.x - x) + (p.z - z) * (p.z - z) < 36) close = true;
        if (close) continue;
        palms.push_back({x, y, z});
    }
    // the first nest: in the palm nearest the hill's south slope (the Founder's home)
    Vector3 want{hill.x + 10, 0, hill.z + 22};
    float best = 1e9f; nest = {hill.x + 10, 0, hill.z + 22};
    for (const auto& p : palms) { float d = (p.x - want.x) * (p.x - want.x) + (p.z - want.z) * (p.z - want.z); if (d < best) { best = d; nest = p; } }
    nest.y += 7.5f;   // (up in the crown)
}
float Island::Height(float x, float z) const {
    if (h.empty()) return -12;
    float fx = (x - x0) / cell, fz = (z - z0) / cell;
    if (fx < 0 || fz < 0 || fx >= n - 1 || fz >= n - 1) return -12;
    int xi = (int)fx, zi = (int)fz; float tx = fx - xi, tz = fz - zi;
    float a = h[(size_t)zi * n + xi], b = h[(size_t)zi * n + xi + 1], c = h[(size_t)(zi + 1) * n + xi], d = h[(size_t)(zi + 1) * n + xi + 1];
    return (a + (b - a) * tx) * (1 - tz) + (c + (d - c) * tx) * tz;
}
Vector3 Island::Normal(float x, float z) const {
    float e = cell;
    float hx = Height(x + e, z) - Height(x - e, z), hz = Height(x, z + e) - Height(x, z - e);
    return Vector3Normalize({-hx, 2 * e, -hz});
}

// ---------------------------------------------------------------- the wind
Vector2 Wind::At(float t) const {
    float k = Smooth(0, 20, shiftT);   // (a shift turns over 20 s)
    Vector2 d = Vector2Normalize(Vector2Lerp(dir, nextDir, k));
    float s = speed + (nextSpeed - speed) * k;
    s *= 1 + 0.15f * sinf(t * 0.7f) + 0.08f * sinf(t * 2.3f);
    return Vector2Scale(d, s);
}

// ---------------------------------------------------------------- the Founder
const char* FStateName(FState s) {
    switch (s) { case FState::Fly: return "flying"; case FState::Strike: return "striking"; case FState::Struggle: return "struggling"; case FState::Under: return "under";
                 case FState::Perched: return "perched"; case FState::Floating: return "floating"; case FState::Fainted: return "fainted"; case FState::Dead: return "dead"; }
    return "?";
}
float Founder::StatMul() const { return (chick ? 0.5f : 1.0f) * (hunger < 0.25f ? 0.7f : 1.0f) * (deaths >= 3 ? 0.9f : 1.0f); }
float Founder::Cruise(const FounderDef& d) const { return d.cruise * std::max(0.5f, StatMul()); }
float Founder::Sprint(const FounderDef& d) const { return d.sprint * std::max(0.5f, StatMul()); }
int Founder::Carry(const FounderDef& d) const { return std::max(1, chick ? d.carry / 2 : d.carry); }

// ---------------------------------------------------------------- the world
float World::Rand() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 16777216.0f; }
void World::Say(const std::string& s) { log.push_back(s); if (log.size() > 60) log.erase(log.begin()); }
float World::DayPhase() const { return fmodf(time / DAY + 0.22f, 1.0f); }   // (the match opens just before dawn's rise)
float World::FeedValue(int sp) const { return sp >= 0 && eco.map && sp < (int)eco.map->species.size() ? (float)eco.map->species[sp].size : 1; }
float World::Thermal(Vector3 p) const {
    // thermals rise off the hill in the afternoon: free altitude
    float ph = DayPhase(), k = Smooth(0.42f, 0.5f, ph) * (1 - Smooth(0.68f, 0.76f, ph));
    if (k <= 0 || p.y > 180) return 0;
    float d = Vector2Distance({p.x, p.z}, {island.hill.x, island.hill.z});
    return d < 34 ? 3.6f * k * (1 - d / 34) : 0;
}
static bool Catchable(const rt::Species& s) { return !s.isDiver && !s.isEnemy && s.tier < 4 && s.size <= 4 && !s.Has("protected"); }
int World::FishNear(Vector3 p, float r, float maxDepth, int* count) const {
    int best = -1; float bd = r * r; int n = 0;
    for (int i = 0; i < (int)eco.agents.size(); i++) {
        const rt::Agent& a = eco.agents[i];
        if (!a.alive || a.diver >= 0 || a.pos.y < -maxDepth || !Catchable(eco.map->species[a.sp])) continue;
        float d = (a.pos.x - p.x) * (a.pos.x - p.x) + (a.pos.z - p.z) * (a.pos.z - p.z);
        if (d < r * r) n++;
        if (d < bd) { bd = d; best = i; }
    }
    if (count) *count = n;
    return best;
}

void World::Init(const std::string& founderKey, uint32_t seed) {
    *this = World{};
    rng = seed ? seed * 2654435761u + 1 : 7;
    island.Generate(seed ? seed : 1);
    const rt::MapData& sea = rt::Map(seaKey);
    eco.Init(sea, seed ? seed : 1, 1, 1);
    // a bird low over the water is a body in the web: a shark's strike on it is the game's to resolve
    me.agent = eco.AddDiver(0, {0, 50, 0});
    if (me.agent >= 0) eco.agents[me.agent].alive = false;
    eco.onDiverHit = [this](int, int attacker, float) {
        if (me.st == FState::Dead || me.pos.y > 10 || island.Land(me.pos.x, me.pos.z)) return;
        std::string who = attacker >= 0 && attacker < (int)eco.agents.size() ? eco.map->species[eco.agents[attacker].sp].name : std::string("something below");
        Kill("taken by a " + who);
    };
    wind.dir = Vector2Normalize({1, 0.4f}); wind.nextDir = wind.dir; wind.speed = wind.nextSpeed = 6; wind.shiftT = 999;
    int fi = FounderIndex(founderKey);
    me.def = fi >= 0 ? fi : 0;
    me.st = FState::Perched; me.pos = island.nest; me.yaw = PI * 0.5f; me.stamina = Def().stamina; me.hunger = 0.8f; me.hp = Def().hp;
    me.strikeLen = Def().key == "taloned" ? 1.0f : 0.5f;   // (the Taloned's strike window is twice as long)
    Say(Def().name + " (" + Def().bird + ") wakes in its nest. Fish to eat, fish to grow.");
}

void World::Kill(const std::string& cause) {
    if (me.st == FState::Dead) return;
    me.st = FState::Dead; me.respawnT = 30; me.deaths++; me.lastCause = cause;
    if (me.carrySp >= 0) { me.carrySp = -1; me.carrySize = 0; fishLost++; }
    me.vel = {0, 0, 0}; me.airspeed = 0;
    eco.AddBlood({me.pos.x, -0.5f, me.pos.z}, 15);
    Say("The Founder is " + cause + ". The colony waits (respawn in 30 s)." + (me.deaths == 3 ? " Three deaths: the colony's confidence is shaken (-10%)." : ""));
}
void World::Respawn() {
    me.st = FState::Perched; me.pos = island.nest; me.vel = {0, 0, 0}; me.airspeed = 0; me.pitch = 0;
    me.chick = true; me.chickFish = 0; me.adultT = 0;
    me.hunger = std::max(me.hunger, 0.5f); me.stamina = Def().stamina * 0.5f; me.hp = Def().hp * 0.5f;
    Say("Back in the nest as a chick-leader: half speed, half carry. Three fish make you the Founder again.");
}
void World::SyncBody() {
    // the body in the web: at the surface under the bird while it is low over the water (a shark can breach to 10 m)
    if (me.agent < 0 || me.agent >= (int)eco.agents.size()) return;
    rt::Agent& a = eco.agents[me.agent];
    bool low = me.st != FState::Dead && me.pos.y < 10 && !island.Land(me.pos.x, me.pos.z);
    Vector3 at{me.pos.x, me.st == FState::Struggle || me.st == FState::Under ? -0.6f : -0.3f, me.pos.z};
    int zi = low ? eco.ZoneAt(at) : -1;
    a.alive = low && zi >= 0;
    if (a.alive) { a.pos = at; a.zone = zi; a.vel = me.vel; a.downed = false; }
}

void World::StartStrike() {
    me.st = FState::Strike; me.strikeT = 0;
    me.strikeSpeed = Vector3Length(me.vel);
    float tImpact = me.vel.y < -0.1f ? me.pos.y / -me.vel.y : 0.2f;
    me.strikeAt = {me.pos.x + me.vel.x * tImpact, 0, me.pos.z + me.vel.z * tImpact};
    me.strikeAim = me.strikeAt;
    timeScale = 0.25f;
}
void World::ResolveStrike() {
    timeScale = 1;
    const FounderDef& d = Def();
    eco.AddNoise(me.strikeAim, 2);   // (every dive is a splash the web hears)
    me.pos = {me.strikeAim.x, -0.4f, me.strikeAim.z};
    float reach = 1.2f + std::min(me.strikeSpeed, 35.0f) * 0.045f;   // (a plunge from high goes deeper)
    int fi = FishNear(me.strikeAim, 3.0f, reach);
    bool hit = false;
    if (fi >= 0) {
        const rt::Agent& a = eco.agents[fi];
        const rt::Species& s = eco.map->species[a.sp];
        float dist = Vector2Distance({a.pos.x, a.pos.z}, {me.strikeAim.x, me.strikeAim.z});
        float clarity = eco.ZoneAt(a.pos) >= 0 && eco.map->zones[eco.ZoneAt(a.pos)].name == "The Blue" ? 0.9f : 1.0f;
        float chance = 0.62f * d.talon * (1.15f - 0.1f * s.size) * std::clamp(me.strikeSpeed / 20, 0.6f, 1.25f) * clarity * (1 - dist / 3.2f);
        if (d.key == "beaked") chance *= 0.85f;
        hit = forceHit || Rand() < std::clamp(chance, 0.05f, 0.95f);
        if (getenv("DEPTH_TRACE")) printf("    strike: %s at %.1f m from the aim, depth %.1f, speed %.0f, chance %.2f -> %s\n", s.name.c_str(), dist, a.pos.y, me.strikeSpeed, chance, hit ? "hit" : "miss");
        if (hit) {
            if (s.size <= me.Carry(d)) {
                me.carrySp = a.sp; me.carrySize = s.size;
                eco.agents[fi].alive = false;   // (out of the sea: in the talons)
                if (a.sp < (int)eco.deathsBySpecies.size()) eco.deathsBySpecies[a.sp]++;
                fishCaught++;
                me.st = FState::Fly; me.airspeed = 9; me.pitch = 0.55f; me.pos.y = 0.4f;
                Say("Got it: a " + s.name + " (size " + std::to_string(s.size) + ").");
                return;
            }
            // too heavy: it pulls the bird under for two seconds, bleeding, and escapes
            me.st = FState::Struggle; me.struggleT = 2; me.struggleSp = a.sp; me.struggleAgent = fi;
            eco.agents[fi].held = 2;
            Say("A " + s.name + " is too heavy (size " + std::to_string(s.size) + "): it fights the talons!");
            return;
        }
    }
    if (getenv("DEPTH_TRACE") && fi < 0) { int n = 0; FishNear(me.strikeAim, 8, 6, &n); printf("    strike: nothing within 3 m (reach %.1f m); %d within 8 m and 6 m deep\n", reach, n); }
    fishMissed++;
    me.st = FState::Under; me.underT = 0.6f;
    Say(fi >= 0 ? "Missed." : "Nothing there.");
}

void World::StepFounder(float dt, const FounderInput& in) {
    const FounderDef& d = Def();
    Founder& f = me;
    float maxStam = d.stamina * (f.chick ? 0.5f : 1.0f);
    // hunger drains over a game day (a chick-leader's twice as fast); at 0 the Founder faints
    if (f.st != FState::Dead) {
        f.hunger -= dt / DAY * (f.chick ? 2.0f : 1.0f);
        if (f.hunger <= 0 && f.st != FState::Fainted) { f.hunger = 0; f.st = FState::Fainted; f.faintT = 10; Say("The Founder faints from hunger!"); }
    }
    if (f.chick && !f.adultT && f.chickFish >= 3) { f.chick = false; f.adultT = 0.001f; Say("Fed back up: the Founder again. Ten seconds more and the colony takes orders."); }
    if (f.adultT > 0) f.adultT += dt;
    // a carried fish is weight and scent
    float carryMul = 1 - 0.06f * f.carrySize;
    if (f.carrySp >= 0 && f.pos.y < 12 && !island.Land(f.pos.x, f.pos.z)) eco.AddBlood({f.pos.x, -0.3f, f.pos.z}, 0.04f * f.carrySize * dt);
    float ground = std::max(0.0f, island.Height(f.pos.x, f.pos.z));
    bool overLand = island.Land(f.pos.x, f.pos.z);
    switch (f.st) {
    case FState::Fly: {
        // steering: toward the player's aim, banking into the turn
        float dyaw = atan2f(sinf(in.yaw - f.yaw), cosf(in.yaw - f.yaw));
        float turn = (f.sprinting ? 1.5f : 1.9f) * dt;
        f.yaw += std::clamp(dyaw, -turn, turn);
        f.bank += (std::clamp(dyaw * 1.4f, -1.0f, 1.0f) * 0.85f - f.bank) * std::min(1.0f, dt * 4);
        float pt = std::clamp(in.pitch, -1.4f, f.exhausted ? 0.05f : 0.85f);   // (exhausted: no climb)
        f.pitch += std::clamp(pt - f.pitch, -1.6f * dt, 1.6f * dt);
        f.flapping = in.flap;
        f.sprinting = f.flapping && in.sprint && !f.exhausted && f.stamina > 0.05f;
        f.gliding = !f.flapping;
        float sinP = sinf(f.pitch);
        // gravity along the path: diving is speed, climbing costs it
        f.airspeed += -9.81f * sinP * dt * 0.9f;
        float glide = 9;
        if (f.flapping) {
            float target = (f.sprinting ? f.Sprint(d) : f.Cruise(d)) * carryMul * (f.exhausted ? 0.5f : 1.0f);   // (exhausted: half speed)
            if (f.airspeed < target) f.airspeed += (target - f.airspeed) * std::min(1.0f, dt * 1.4f);
        } else if (f.airspeed > glide && sinP > -0.35f) f.airspeed -= (f.airspeed - glide) * 0.25f * dt;
        if (!f.flapping && f.airspeed < glide && sinP > -0.2f) f.airspeed += (glide * 0.85f - f.airspeed) * 0.3f * dt;   // (a glide holds its speed by trading height)
        if (in.brake) f.airspeed -= 8 * dt;   // (the flare)
        float diveMax = 35 * (d.key == "swift" ? 1.4f : 1.0f);
        f.airspeed = std::clamp(f.airspeed, 0.0f, diveMax);
        // stamina: flapping and climbing spend it, gliding (and a thermal, and a tailwind) buy it back
        Vector2 w = WindAt();
        float windK = std::clamp(Vector2Length(w) / 8, 0.0f, 1.0f);
        Vector2 fx{cosf(f.yaw), sinf(f.yaw)};
        float along = Vector2Length(w) > 0.1f ? Vector2DotProduct(fx, Vector2Normalize(w)) : 0;
        float lift = Thermal(f.pos);
        float drain = 0;
        if (f.sprinting) drain += 1.0f;                                     // (sprints and climbs cost breath; a level cruise is the bird's own pace)
        if (f.flapping && sinP > 0.05f) drain += 0.9f * sinP;
        if (drain > 0) f.stamina -= drain * dt;
        else if (f.flapping) f.stamina += 0.15f * dt;                     // (cruising level, it slowly gets its breath back)
        else f.stamina += (0.6f + (lift > 0.5f ? 0.8f : 0.0f) + (along > 0.5f ? 0.4f : 0.0f)) * dt;
        f.stamina = std::clamp(f.stamina, 0.0f, maxStam);
        if (f.stamina <= 0.05f) { if (!f.exhausted) Say("Out of breath: glide to get it back."); f.exhausted = true; }
        if (f.exhausted && f.stamina > std::min(2.0f, maxStam * 0.5f)) f.exhausted = false;
        // velocity: the air's speed along the heading, the wind's 30% with or against, the glide's sink, a thermal's lift
        float pen = d.key == "albatross" && along < 0 ? 0.15f : 0.3f;
        float gs = 1 + pen * along * windK;
        Vector3 fwd{cosf(f.pitch) * cosf(f.yaw), sinP, cosf(f.pitch) * sinf(f.yaw)};
        f.vel = Vector3Scale(fwd, f.airspeed);
        f.vel.x *= gs; f.vel.z *= gs;
        f.vel.x += w.x * 0.12f; f.vel.z += w.y * 0.12f;
        if (!f.flapping) f.vel.y -= 1.1f;
        if (f.airspeed < 5 && !f.flapping) { f.pitch -= 0.8f * dt; f.vel.y -= (5 - f.airspeed) * 0.6f; }   // (a stall drops the nose)
        f.vel.y += lift;
        if (f.pos.y > 200 && f.vel.y > 0) f.vel.y = 0;
        f.pos = Vector3Add(f.pos, Vector3Scale(f.vel, dt));
        // the strike: a fast dive about to meet open water
        if (!overLand && f.vel.y < -4 && f.airspeed > 10 && f.pos.y > 0.3f && f.pos.y / -f.vel.y < 0.35f) { StartStrike(); break; }
        // the ground and the sea
        ground = std::max(0.0f, island.Height(f.pos.x, f.pos.z));
        overLand = island.Land(f.pos.x, f.pos.z);
        if (f.pos.y < ground + 0.3f) {
            float sp = Vector3Length(f.vel);
            if (overLand) {
                if (sp < 11 && f.pitch > -0.6f) { f.st = FState::Perched; f.pos.y = ground; f.vel = {0, 0, 0}; f.airspeed = 0; f.pitch = 0; f.bank = 0; }
                else { f.pos.y = ground + 0.3f; f.airspeed *= 0.4f; f.pitch = 0.25f; Say("Bump!"); }
            } else if (sp < 9) { f.st = FState::Floating; f.pos.y = 0; f.vel = {0, 0, 0}; f.airspeed = 0; f.pitch = 0; }
            else { f.pos.y = 0.3f; f.pitch = std::max(f.pitch, 0.05f); f.airspeed *= 0.9f; }   // (a skim off the swell)
        }
        // low over the nest: drop the fish in it
        if (in.interact && f.carrySp >= 0 && Vector3Distance(f.pos, island.nest) < 4) {
            if ((int)cache.size() < cacheCap) { cache.push_back({f.carrySp, f.carrySize, 0}); Say("Into the nest: " + eco.map->species[f.carrySp].name + "."); }
            else Say("The nest's cache is full.");
            f.carrySp = -1; f.carrySize = 0;
        }
    } break;
    case FState::Strike: break;   // (Step runs the strike on real time)
    case FState::Struggle: {
        f.pos.y = -0.3f; f.vel = {0, 0, 0};
        f.struggleT -= dt;
        eco.AddBlood({f.pos.x, -0.5f, f.pos.z}, 3 * dt);   // (it bleeds: the sharks notice)
        if (f.struggleAgent >= 0 && f.struggleAgent < (int)eco.agents.size() && eco.agents[f.struggleAgent].alive) eco.agents[f.struggleAgent].pos = {f.pos.x, -0.6f, f.pos.z};
        if (f.struggleT <= 0) {
            if (f.struggleAgent >= 0 && f.struggleAgent < (int)eco.agents.size()) { rt::Agent& a = eco.agents[f.struggleAgent]; a.wound = std::max(a.wound, 0.5f); a.held = 0; }
            fishLost++;
            Say("It tore free and is gone, bleeding.");
            f.st = FState::Fly; f.airspeed = 6; f.pitch = 0.6f; f.pos.y = 0.4f; f.struggleAgent = -1;
        }
    } break;
    case FState::Under:
        f.pos.y = -0.6f; f.vel = {0, 0, 0};
        f.underT -= dt;
        if (f.underT <= 0) { f.st = FState::Fly; f.airspeed = 7; f.pitch = 0.55f; f.pos.y = 0.4f; f.stamina = std::max(0.0f, f.stamina - 0.5f); }
        break;
    case FState::Perched:
    case FState::Floating: {
        f.vel = {0, 0, 0}; f.airspeed = 0; f.bank = 0;
        if (f.st == FState::Perched) f.pos.y = Vector3Distance({f.pos.x, 0, f.pos.z}, {island.nest.x, 0, island.nest.z}) < 1.5f ? island.nest.y : ground;
        else f.pos.y = 0;
        f.yaw += atan2f(sinf(in.yaw - f.yaw), cosf(in.yaw - f.yaw)) * std::min(1.0f, dt * 6);
        f.stamina = std::min(maxStam, f.stamina + (f.st == FState::Perched ? 1.2f : 0.6f) * dt);
        bool atNest = Vector3Distance(f.pos, island.nest) < 3;
        if (in.interact && f.carrySp >= 0 && atNest) {
            if ((int)cache.size() < cacheCap) { cache.push_back({f.carrySp, f.carrySize, 0}); Say("Into the nest: " + eco.map->species[f.carrySp].name + "."); f.carrySp = -1; f.carrySize = 0; }
            else Say("The nest's cache is full.");
        }
        if (in.eat && f.st == FState::Perched) {
            // eat what you carry, or from the nest's cache
            int sz = 0;
            if (f.carrySp >= 0) { sz = f.carrySize; f.carrySp = -1; f.carrySize = 0; }
            else if (atNest && !cache.empty()) { size_t best = 0; for (size_t k = 1; k < cache.size(); k++) if (cache[k].age > cache[best].age) best = k; sz = cache[best].size; cache.erase(cache.begin() + best); }   // (the oldest first, before it spoils)
            if (sz > 0) {
                f.hunger = std::min(1.0f, f.hunger + 0.25f * sz); fishEaten++;
                if (f.chick) f.chickFish++;
                Say(TextFormat("Ate a size-%d fish.%s", sz, f.chick ? TextFormat(" (%d of 3)", f.chickFish) : ""));
            }
        }
        if (in.takeoff && f.stamina > 0.4f) {
            f.st = FState::Fly; f.airspeed = f.pos.y <= 0.01f && !overLand ? 6.0f : 8.0f; f.pitch = 0.6f; f.pos.y += 0.4f;
            f.stamina -= f.pos.y <= 0.5f ? 1.0f : 0.4f;   // (off the water is hard work)
        }
    } break;
    case FState::Fainted: {
        f.faintT -= dt;
        // it drops: onto the land, or into the sea (where it floats, helpless)
        if (f.pos.y > ground + 0.05f) f.pos.y = std::max(ground, f.pos.y - 6 * dt);
        f.vel = {0, 0, 0};
        if (f.faintT <= 0) { f.hunger = 0.12f; f.st = overLand ? FState::Perched : FState::Floating; Say("The Founder comes to, weak with hunger."); }
    } break;
    case FState::Dead:
        f.respawnT -= dt;
        if (f.respawnT <= 0) Respawn();
        break;
    }
    // stay in the world's box
    f.pos.x = std::clamp(f.pos.x, -1400.0f, 1400.0f); f.pos.z = std::clamp(f.pos.z, -1400.0f, 1400.0f);
}

void World::Step(float realDt, const FounderInput& in) {
    realDt = std::min(realDt, 0.1f);
    // the strike runs on real time while the world crawls: steer the talons onto a fish
    if (me.st == FState::Strike) {
        me.strikeT += realDt;
        Vector3 right{-sinf(me.yaw), 0, cosf(me.yaw)}, fwd{cosf(me.yaw), 0, sinf(me.yaw)};
        Vector3 aim = Vector3Add(me.strikeAt, Vector3Add(Vector3Scale(right, std::clamp(in.steer.x, -1.0f, 1.0f) * 2.6f), Vector3Scale(fwd, std::clamp(in.steer.y, -1.0f, 1.0f) * 2.6f)));
        me.strikeAim = Vector3Lerp(me.strikeAim, aim, std::min(1.0f, realDt * 10));
        float k = std::min(1.0f, me.strikeT / me.strikeLen);
        me.pos = Vector3Lerp(me.pos, {me.strikeAim.x, 0.5f * (1 - k), me.strikeAim.z}, std::min(1.0f, realDt * 3));
        if (me.strikeT >= me.strikeLen) ResolveStrike();
    }
    float dt = realDt * timeScale;
    time += dt;
    // the wind shifts every few minutes
    wind.shiftT += dt;
    if (wind.shiftT > wind.shiftLen) {
        wind.dir = wind.nextDir; wind.speed = wind.nextSpeed;
        float a = Rand() * 2 * PI; wind.nextDir = {cosf(a), sinf(a)}; wind.nextSpeed = 3 + Rand() * 7; wind.shiftT = 0; wind.shiftLen = 150 + Rand() * 90;
        Say(TextFormat("The wind shifts: %.0f m/s from the %s.", wind.nextSpeed, fabsf(wind.nextDir.x) > fabsf(wind.nextDir.y) ? (wind.nextDir.x > 0 ? "west" : "east") : (wind.nextDir.y > 0 ? "north" : "south")));
    }
    if (me.st != FState::Strike) StepFounder(dt, in);
    // fish rise at dawn and dusk and sink by day and night (the best fishing is at the rises)
    {
        float ph = DayPhase();
        float rise = std::max(Smooth(0.17f, 0.25f, ph) * (1 - Smooth(0.3f, 0.36f, ph)), Smooth(0.66f, 0.72f, ph) * (1 - Smooth(0.78f, 0.84f, ph)));
        float day = ph > 0.3f && ph < 0.7f ? 1.0f : 0.0f;
        float target = rise > 0.01f ? -1.4f : day > 0 ? -4.5f : -11;
        for (auto& a : eco.agents) {
            if (!a.alive || a.diver >= 0) continue;
            const rt::Species& s = eco.map->species[a.sp];
            if (!Catchable(s) || s.Has("scavenger")) continue;
            int zi = a.zone;
            if (zi < 0 || zi >= (int)eco.map->zones.size()) continue;
            const rt::Zone& z = eco.map->zones[zi];
            float t = std::clamp(target, z.y0 + 0.4f, z.y1 - 0.3f);
            a.pos.y += (t - a.pos.y) * std::min(1.0f, 0.35f * dt);
        }
    }
    SyncBody();
    eco.Step(dt);
    // the cache: fish spoil after two game days
    for (auto it = cache.begin(); it != cache.end();) { it->age += dt; if (it->age > 2 * DAY) { Say("A fish in the nest has spoiled."); it = cache.erase(it); } else ++it; }
}

// ---------------------------------------------------------------- --flight-test
int RunFlightTest() {
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("The Flight, stage 1: the Founder, the island, the sea\n");
    std::string why;
    if (!rt::DataOk(&why)) { printf("FAIL: no data: %s\n", why.c_str()); return 1; }
    check(Founders().size() == 12 && FounderIndex("albatross") >= 0 && Founders()[FounderIndex("albatross")].stamina == 16, TextFormat("the twelve founders load (%d)", (int)Founders().size()));
    World w; w.Init("taloned", 3);
    const Island& is = w.island;
    check(is.hill.y > 20 && is.Height(0, 70) < -1.5f && is.Height(0, 70) > -4 && is.Height(0, -10) > 1 && is.Height(160, 0) < -8, TextFormat("the island: a %.0f m hill, a 2-3 m lagoon bay to the south, land in the middle, the shelf falling away", is.hill.y));
    check(is.palms.size() >= 30 && is.Land(is.nest.x, is.nest.z) && is.nest.y > 7, TextFormat("%d palms on the land; the nest up in a palm by the hill", (int)is.palms.size()));
    const rt::MapData& sea = *w.eco.map;
    int alive = 0; for (const auto& a : w.eco.agents) alive += a.alive && a.diver < 0;
    check(sea.zones.size() == 7 && sea.species.size() >= 15 && alive > 200, TextFormat("the sea runs on Red Tide's engine: %d zones, %d species, %d animals", (int)sea.zones.size(), (int)sea.species.size(), alive));
    for (int i = 0; i < 60 * 30; i++) w.eco.Step(1 / 60.0f);
    int alive2 = 0; for (const auto& a : w.eco.agents) alive2 += a.alive && a.diver < 0;
    check(alive2 > alive * 0.6f, TextFormat("half a minute later the web still holds (%d)", alive2));
    // ---- flight
    auto fly = [&](World& v, FounderInput in, float secs) { for (int i = 0; i < (int)(secs * 60); i++) v.Step(1 / 60.0f, in); };
    {
        World v; v.Init("taloned", 4);
        v.me.st = FState::Fly; v.me.pos = {0, 60, 300}; v.me.yaw = 0; v.me.airspeed = 12; v.me.pitch = 0; v.wind.speed = v.wind.nextSpeed = 0.01f;
        FounderInput in; in.yaw = 0; in.pitch = 0.09f; in.flap = true;
        fly(v, in, 10);
        float cruise = v.me.airspeed, stam = v.me.stamina, alt = v.me.pos.y;
        check(fabsf(cruise - v.Def().cruise) < 1.0f && alt > 50 && alt < 75 && stam > v.Def().stamina * 0.4f, TextFormat("flapping holds cruise (%.1f m/s, the Taloned's %.1f) at about the same height (%.0f m)", cruise, v.Def().cruise, alt));
        in.sprint = true;
        float t = 0; v.me.stamina = v.Def().stamina; v.me.exhausted = false;
        while (!v.me.exhausted && t < 20) { v.Step(1 / 60.0f, in); t += 1 / 60.0f; }
        check(v.me.airspeed > v.Def().sprint * 0.9f && t > 6 && t < 12, TextFormat("sprinting reaches %.0f m/s and runs out of breath in %.1f s", v.me.airspeed, t));
        in.sprint = false; in.flap = false; in.pitch = -0.1f;
        float s0 = v.me.stamina, y0 = v.me.pos.y; fly(v, in, 4);
        check(v.me.stamina > s0 + 1.5f && v.me.pos.y < y0, "gliding sinks, and buys the breath back");
        v.me.pos.y = 180; in.pitch = -1.3f; fly(v, in, 6);
        check(v.me.airspeed > 32, TextFormat("a dive from high reaches %.0f m/s, the fastest thing in the game", v.me.airspeed));
    }
    {
        auto speedWith = [&](float windDir) {
            World v; v.Init("taloned", 5);
            v.wind.dir = v.wind.nextDir = {windDir, 0}; v.wind.speed = v.wind.nextSpeed = 8; v.wind.shiftT = 999;
            v.me.st = FState::Fly; v.me.pos = {0, 60, 300}; v.me.yaw = 0; v.me.airspeed = 12;
            FounderInput in; in.yaw = 0; in.pitch = 0.09f; in.flap = true;
            fly(v, in, 3);
            return v.me.vel.x;
        };
        float tail = speedWith(1), head = speedWith(-1);
        check(tail > head * 1.6f && tail > 13 && head < 10, TextFormat("downwind %.1f m/s, upwind %.1f m/s (the wind's 30%% either way)", tail, head));
    }
    // ---- fishing: a strike on a fish at the surface
    auto setFish = [&](World& v, const char* sp, Vector3 at) {
        int si = v.eco.map->SpeciesIndex(sp);
        int ai = v.eco.Spawn(si, at, v.eco.ZoneAt(at));
        v.eco.agents[ai].pos = at; v.eco.agents[ai].vel = {0, 0, 0}; v.eco.agents[ai].held = 30;   // (holds still for the test)
        return ai;
    };
    {
        World v; v.Init("taloned", 6); v.forceHit = true;
        for (auto& a : v.eco.agents) if (a.diver < 0 && v.eco.map->species[a.sp].tier >= 3) a.alive = false;   // (no sharks or hunters)
        Vector3 fish{0, -0.6f, 70};
        setFish(v, "Mullet", fish);
        v.me.st = FState::Fly; v.me.pos = {fish.x - 12, 18, fish.z}; v.me.yaw = 0; v.me.airspeed = 18; v.me.pitch = -0.9f;
        FounderInput in; in.yaw = 0; in.pitch = -0.95f;
        bool struck = false, slow = false;
        for (int i = 0; i < 60 * 4 && v.me.carrySp < 0; i++) {
            if (v.me.st == FState::Strike) { struck = true; slow = slow || v.timeScale < 0.5f; Vector3 dlt = Vector3Subtract(fish, v.me.strikeAt); in.steer = {std::clamp(dlt.z / 2.6f, -1.0f, 1.0f), std::clamp(dlt.x / 2.6f, -1.0f, 1.0f)}; }
            v.Step(1 / 60.0f, in);
        }
        check(struck && slow && v.me.carrySp == v.eco.map->SpeciesIndex("Mullet") && v.fishCaught == 1, "a dive at the water enters the slow strike; steering the talons onto a mullet catches it");
        // fly it home and drop it in the nest; eat it
        v.me.st = FState::Perched; v.me.pos = v.island.nest;
        float h0 = v.me.hunger = 0.3f;
        FounderInput drop; drop.interact = true; v.Step(1 / 60.0f, drop);
        bool cached = v.cache.size() == 1 && v.me.carrySp < 0;
        FounderInput eat; eat.eat = true; v.Step(1 / 60.0f, eat);
        check(cached && v.cache.empty() && fabsf(v.me.hunger - (h0 + 0.5f)) < 0.02f, TextFormat("into the nest's cache, then eaten: hunger %.2f -> %.2f", h0, v.me.hunger));
    }
    {
        World v; v.Init("swift", 7); v.forceHit = true;   // (the Swift lifts only size 2: a snapper is too heavy)
        for (auto& a : v.eco.agents) if (a.diver < 0 && v.eco.map->species[a.sp].tier >= 3) a.alive = false;
        Vector3 fish{0, -0.6f, 110};
        int fa = setFish(v, "Snapper", fish);
        v.me.st = FState::Fly; v.me.pos = {fish.x - 12, 18, fish.z}; v.me.yaw = 0; v.me.airspeed = 18; v.me.pitch = -0.9f;
        FounderInput in; in.yaw = 0; in.pitch = -0.95f;
        bool fought = false;
        float blood0 = v.eco.scent.Total();
        for (int i = 0; i < 60 * 5; i++) {
            if (v.me.st == FState::Strike) { Vector3 dlt = Vector3Subtract(fish, v.me.strikeAt); in.steer = {std::clamp(dlt.z / 2.6f, -1.0f, 1.0f), std::clamp(dlt.x / 2.6f, -1.0f, 1.0f)}; }
            if (v.me.st == FState::Struggle) fought = true;
            v.Step(1 / 60.0f, in);
            if (fought && v.me.st == FState::Fly) break;
        }
        check(fought && v.me.carrySp < 0 && v.fishLost == 1 && v.eco.agents[fa].wound > 0.3f && v.eco.scent.Total() > blood0, "a fish too heavy for the Swift fights the talons, bleeds, and gets away");
    }
    // ---- hunger, faint, the shark, the chick-leader
    {
        World v; v.Init("taloned", 8);
        v.me.hunger = 0.01f; FounderInput none;
        for (int i = 0; i < 60 * 5 && v.me.st != FState::Fainted; i++) v.Step(1 / 60.0f, none);
        check(v.me.st == FState::Fainted, "at no hunger the Founder faints");
        World s; s.Init("taloned", 9);
        int sh = s.eco.map->SpeciesIndex("Reef Shark");
        Vector3 at{0, -0.3f, 115};
        s.me.st = FState::Floating; s.me.pos = {at.x, 0, at.z};
        int ai = s.eco.Spawn(sh, {at.x + 3, -2, at.z}, s.eco.ZoneAt(at));
        s.eco.agents[ai].hunger = 1; s.eco.agents[ai].fedT = 0;
        for (int i = 0; i < 60 * 40 && s.me.st != FState::Dead; i++) { s.eco.agents[ai].hunger = 1; s.Step(1 / 60.0f, none); }
        check(s.me.st == FState::Dead && s.me.lastCause.find("Shark") != std::string::npos, TextFormat("floating over the reef with a hungry reef shark below: %s", s.me.lastCause.empty() ? "nothing happened" : s.me.lastCause.c_str()));
        for (int i = 0; i < 60 * 31; i++) s.Step(1 / 60.0f, none);
        bool chick = s.me.st == FState::Perched && s.me.chick && s.me.Carry(s.Def()) < s.Def().carry && s.me.Cruise(s.Def()) < s.Def().cruise * 0.6f;
        for (int k = 0; k < 3; k++) { s.cache.push_back({1, 1, 0}); FounderInput e; e.eat = true; s.Step(1 / 60.0f, e); s.Step(1 / 60.0f, none); }
        check(chick && !s.me.chick, "30 s later it's back in the nest as a chick-leader (half speed and carry); three fish make it the Founder again");
    }
    // ---- ten minutes of fishing by an autopilot (real dives, no forced hits): the stage's gate in numbers
    {
        World v; v.Init("taloned", 21);
        v.me.st = FState::Fly; v.me.pos = {0, 25, 60}; v.me.airspeed = 11; v.me.hunger = 1;
        int dives = 0; FState was = v.me.st; bool resting = false, stoop = false;
        for (int i = 0; i < 60 * 600; i++) {
            Founder& f = v.me;
            FounderInput in; in.yaw = f.yaw; in.pitch = 0; if (f.stamina < 1.5f) resting = true; if (f.stamina > 5) resting = false;
            in.flap = !f.exhausted && (!resting || f.pos.y < 4);   // (glide to get the breath back, as a player would)
            auto climb = [&](float p) { return !resting ? p : std::min(p, 0.0f); };
            auto toward = [&](Vector3 p) { in.yaw = atan2f(p.z - f.pos.z, p.x - f.pos.x); };
            if (f.st == FState::Perched || f.st == FState::Floating) {
                if (f.carrySp >= 0 && Vector3Distance(f.pos, v.island.nest) < 3) in.interact = true;
                else if (f.hunger < 0.6f && !v.cache.empty() && Vector3Distance(f.pos, v.island.nest) < 3) in.eat = true;
                else if (f.stamina > 3) in.takeoff = true;
            } else if (f.st == FState::Strike) {
                int fi = v.FishNear(f.strikeAt, 3.5f, 3);
                if (fi >= 0) {
                    Vector3 d = Vector3Subtract(v.eco.agents[fi].pos, f.strikeAt);
                    Vector3 r{-sinf(f.yaw), 0, cosf(f.yaw)}, fw{cosf(f.yaw), 0, sinf(f.yaw)};
                    in.steer = {std::clamp(Vector3DotProduct(d, r) / 2.6f, -1.0f, 1.0f), std::clamp(Vector3DotProduct(d, fw) / 2.6f, -1.0f, 1.0f)};
                }
            } else if (f.st == FState::Fly) {
                if (f.carrySp >= 0) {   // home with it, gliding down onto the nest
                    toward(v.island.nest);
                    float dh = Vector2Distance({f.pos.x, f.pos.z}, {v.island.nest.x, v.island.nest.z});
                    in.pitch = std::clamp(atan2f(v.island.nest.y + 1 - f.pos.y, std::max(dh, 1.0f)), -0.5f, 0.5f);
                    if (dh < 12) in.brake = true;
                    in.interact = dh < 4;
                } else {
                    int fi = v.FishNear(f.pos, 160, 2.2f);
                    if (fi < 0) { in.yaw = f.yaw + 0.3f; in.pitch = climb((20 - f.pos.y) * 0.03f); }
                    else {
                        Vector3 p = v.eco.agents[fi].pos;
                        float dh = Vector2Distance({f.pos.x, f.pos.z}, {p.x, p.z});
                        toward(p);
                        if (!stoop && dh < f.pos.y * 0.9f && f.pos.y > 11) stoop = true;                          // the stoop: steep, from height
                        if (stoop) in.pitch = -atan2f(f.pos.y, std::max(dh, 0.5f)) - 0.1f;
                        else if (dh < 10 && f.pos.y < 11) { in.yaw = f.yaw + 1.2f; in.pitch = climb(0.3f); }     // (too close to stoop: go round, climbing)
                        else in.pitch = climb(std::clamp((16 - f.pos.y) * 0.06f, -0.4f, 0.35f));                                       // (too close and low: go round)
                    }
                }
            }
            if (getenv("DEPTH_TRACE") && i % 300 == 0) {
                int nShallow = 0, nAll = 0; v.FishNear(f.pos, 400, 2.2f, &nShallow); v.FishNear(f.pos, 400, 60, &nAll);
                int fi = v.FishNear(f.pos, 160, 2.2f);
                printf("    t=%5.0f %-9s pos %5.0f %5.0f %5.0f  air %4.1f vy %5.1f pitch %5.2f stam %4.1f  shallow %d/%d  target %s\n", i / 60.0f, FStateName(f.st), f.pos.x, f.pos.y, f.pos.z, f.airspeed, f.vel.y, f.pitch, f.stamina, nShallow, nAll,
                       fi >= 0 ? TextFormat("%.0f m away, y %.1f", Vector2Distance({f.pos.x, f.pos.z}, {v.eco.agents[fi].pos.x, v.eco.agents[fi].pos.z}), v.eco.agents[fi].pos.y) : "none");
            }
            v.Step(1 / 60.0f, in);
            if (v.me.st != FState::Fly || v.me.pos.y < 1) stoop = false;
            if (v.me.st == FState::Strike && was != FState::Strike) dives++;
            was = v.me.st;
        }
        printf("  (ten minutes: %d strikes, %d caught, %d missed, %d lost, %d eaten, %d in the nest, hunger %.2f, %s)\n", dives, v.fishCaught, v.fishMissed, v.fishLost, v.fishEaten, (int)v.cache.size(), v.me.hunger, FStateName(v.me.st));
        check(dives >= 10 && v.fishCaught >= 4 && v.me.st != FState::Dead, TextFormat("an autopilot fishing for ten minutes strikes %d times and catches %d", dives, v.fishCaught));
    }
    printf(fails ? "flight-test: %d check(s) failed\n" : "flight-test: all checks passed\n", fails);
    return fails ? 1 : 0;
}

}  // namespace fl
