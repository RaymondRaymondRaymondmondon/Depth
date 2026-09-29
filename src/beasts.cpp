// ============================================================================
//  DEPTH - the living-AI creature engine (see beasts.h for the overview). The engine and the Hull live here;
//  the other biomes' species, food webs and behaviours are in beasts_biomes.cpp.
// ============================================================================
#include "beasts_internal.h"
#include <queue>
#include <cstdlib>

namespace bk {

// ---------------------------------------------------------------- the Hull: species and food web
// ECOSYSTEM_BESTIARY.md / ParkourReference1.2.pdf, "The Hull": cleaner shrimp groom moray eels in hull breaches;
// camouflaged octopuses ambush shrimp; barnacle crabs pinch an octopus that lands too close; a pinched or bumped
// octopus sprays ink; pufferfish panic in the ink and puff into spiked hazards; hull-leeches ride puffers and
// fall off when they puff; stinging anemones catch drifting leeches; hermit crabs scavenge the scraps and mob
// intruders in numbers; brittle-star mats break under the diver. Sprat schools are the eels' staple, and the
// diver is on the menu for the eels and the crabs.
//            name              move              mass  rad  speed sprint accel sight  fov   hear smell hunger dFear dPrey lethal social band pop  den    scav  traits
const SpeciesDef HULL[HS_COUNT] = {
    {"Sprat",            MoveMode::Swim,    0.2f,  5,   70, 230,  900,  170, 2.6f, 0.7f, 0.2f, 0.010f, 0.9f,  0.0f, false, true,  5, 24, 0.05f, 0.0f},
    {"Cleaner Shrimp",   MoveMode::Swim,    0.05f, 5,   45, 190,  800,  120, 2.8f, 0.6f, 0.5f, 0.012f, 0.8f,  0.0f, false, false, 1,  8, 0.80f, 0.2f, T_GROOMER},
    {"Octopus",          MoveMode::Swim,    3.0f,  9,   55, 260,  700,  200, 2.4f, 0.4f, 0.6f, 0.020f, 0.5f,  0.0f, false, false, 0,  4, 0.70f, 0.3f, T_CAMO | T_HOST},
    {"Pufferfish",       MoveMode::Swim,    1.0f,  8,   40, 150,  500,  140, 2.6f, 0.5f, 0.3f, 0.012f, 0.6f,  0.0f, true,  false, 3,  5, 0.30f, 0.0f, T_HOST},
    {"Hull-Leech",       MoveMode::Swim,    0.1f,  5,   20,  60,  200,   70, PI,   0.2f, 0.8f, 0.020f, 0.1f,  0.0f, false, false, 2,  6, 0.00f, 0.0f, T_PARASITE},
    {"Stinging Anemone", MoveMode::Sessile, 2.0f, 10,    0,   0,    0,   60, PI,   0.0f, 0.5f, 0.010f, 0.0f,  0.0f, false, false, 0,  4, 0.00f, 0.0f, T_TRAP},
    {"Hermit Crab",      MoveMode::Walk,    0.5f,  7,   45, 110,  600,  150, 2.4f, 0.5f, 0.9f, 0.020f, 0.7f,  0.0f, false, true,  0,  7, 0.60f, 1.0f, T_MOBBER},
    {"Brittle-Star",     MoveMode::Sessile, 0.3f, 10,    0,   0,    0,   40, PI,   0.3f, 0.0f, 0.005f, 0.4f,  0.0f, false, false, 0,  5, 0.00f, 0.0f},
    {"Barnacle Crab",    MoveMode::Walk,    2.5f, 13,   70, 150,  900,  190, 1.6f, 0.6f, 0.8f, 0.020f, 0.2f,  0.7f, true,  false, 0,  0, 0.40f, 1.0f, T_STRIKER | T_CHARGER, 90},
    {"Moray Eel",        MoveMode::Swim,    8.0f, 10,  110, 330, 1100,  240, 1.9f, 0.8f, 0.9f, 0.030f, 0.15f, 0.8f, true,  false, 2,  2, 0.90f, 0.6f, T_STRIKER | T_DEN_AMBUSH | T_GROOMED | T_HOST, 120},
};
const FoodEdge HULL_WEB[] = {
    {HS_EEL, HS_SPRAT, 0.9f},    {HS_EEL, HS_PUFFER, 0.5f},   {HS_EEL, HS_OCTOPUS, 0.7f}, {HS_EEL, HS_HERMIT, 0.35f},
    {HS_OCTOPUS, HS_SHRIMP, 1.0f}, {HS_OCTOPUS, HS_SPRAT, 0.6f}, {HS_OCTOPUS, HS_HERMIT, 0.7f},
    {HS_CRAB, HS_HERMIT, 0.5f},  {HS_CRAB, HS_SHRIMP, 0.35f}, {HS_CRAB, HS_OCTOPUS, 0.4f},
    {HS_ANEMONE, HS_LEECH, 1.0f}, {HS_ANEMONE, HS_SPRAT, 0.4f}, {HS_ANEMONE, HS_SHRIMP, 0.25f},
};
const SpeciesDef FALLBACK = {"Creature", MoveMode::Walk, 1, 8, 50, 120, 600, 150, 2.0f, 0.5f, 0.5f, 0.01f, 0.5f, 0, false, false, 0, 0, 0.5f, 0.3f};

const BiomeDef* Biome(int level) {
    const BiomeDef* B = nullptr;
    switch (level) {
    case PL_HULL: B = &HullBiome(); break;
    case PL_PIRATE: B = &PirateBiome(); break;
    case PL_ISLAND: B = &IslandBiome(); break;
    case PL_CAVE: B = &CaveBiome(); break;
    case PL_PIPES: B = &PipesBiome(); break;
    default: break;
    }
    return B && B->count > 0 ? B : nullptr;
}
const SpeciesDef& Sp(int biome, int s) {
    const BiomeDef* B = Biome(biome);
    return B && s >= 0 && s < B->count ? B->species[s] : FALLBACK;
}
float Pref(int biome, int pred, int prey) {
    const BiomeDef* B = Biome(biome);
    if (B) for (int k = 0; k < B->webN; k++) if (B->web[k].pred == pred && B->web[k].prey == prey) return B->web[k].pref;
    return 0;
}

// ---------------------------------------------------------------- the diver, as the beasts see it
Diver SeeDiver(const PlatformState& p) {
    Rectangle r = PlatDiverBox(p);
    return {{r.x + r.width / 2, r.y + r.height / 2}, p.vel, p.deathTimer <= 0 && !p.finished && !p.verifying};
}
bool IgnoresDiver(const BeastWorld& W) { const BiomeDef* B = Biome(W.biome); return B && B->ignoreDiver; }

bool Alive(const Beast& b) { return b.life == BeastLife::Alive; }
bool Valid(const BeastWorld& W, int i, int id) { return i >= 0 && i < (int)W.beasts.size() && W.beasts[i].id == id && W.beasts[i].life != BeastLife::Gone; }
Vector2 Home(const BeastWorld& W, const Beast& b) { return b.den >= 0 && b.den < (int)W.dens.size() ? W.dens[b.den].pos : b.territory; }
Vector2 DenMouth(const BeastWorld& W, int d) { return {W.dens[d].pos.x, W.dens[d].pos.y - 12}; }
int NearestDen(const BeastWorld& W, Vector2 at, float maxD) {
    int best = -1; float bd = maxD;
    for (int d = 0; d < (int)W.dens.size(); d++) { float dd = Dist(W.dens[d].pos, at); if (dd < bd) { bd = dd; best = d; } }
    return best;
}
bool InCloud(const BeastWorld& W, Vector2 at) {
    for (const auto& k : W.ink) if (Dist(k.pos, at) < k.r) return true;
    return false;
}
void AddCloud(BeastWorld& W, Vector2 at, float r, float life, int kind) { W.ink.push_back({at, life, r, life, kind}); }

// Light: the Hull is lit from the surface, fading with height above the deck; the open-air levels by the sky;
// the dark levels (Pipes, Cave) are black except the diver's lamp, the level's light pools ('o') and whatever
// is glowing or flashing this tick (W.lights).
float LightAt(const PlatformState& p, const BeastWorld& W, Vector2 at) {
    int tx = (int)floorf(at.x / TILE), ty = (int)floorf(at.y / TILE);
    const BiomeDef* B = Biome(W.biome);
    float base = B ? B->daylight : 0.1f;
    if (W.biome == PL_HULL) base = std::clamp(0.85f - (W.nav.FloorBelow(tx, ty) - ty) / 30.0f, 0.15f, 0.85f);
    for (const auto& l : W.lamps) { float d = Dist(l, at); if (d < 4 * TILE) base = std::max(base, 0.9f - d / (4 * TILE) * 0.8f); }
    for (const auto& l : W.lights) { float d = Dist(l.pos, at); if (d < l.r) base = std::max(base, l.strength * (1 - d / l.r)); }
    Diver dv = SeeDiver(p);
    if (dv.alive) base = std::max(base, 1.0f - Dist(dv.pos, at) / (6 * TILE));
    (void)p;
    return Clamp01(base);
}

// ---------------------------------------------------------------- memory (reference 2.2)
// Up to BEAST_MEM stimuli, each decaying as M0 * e^(-lambda t). A dim beast forgets in a couple of seconds; a
// sharp one holds a trail for six or more - which is what lets a clever predator keep coming after you've
// ducked out of sight.
float Recall(const Beast& b, const BeastMemory& m, float now) {
    if (m.strength <= 0) return 0;
    float lambda = 0.22f + (1 - b.pers.intelligence) * 0.9f;
    return m.strength * expf(-lambda * (now - m.t));
}
void Remember(Beast& b, uint8_t kind, int source, int sid, Vector2 pos, Vector2 vel, float strength, float now) {
    int slot = -1;
    for (int k = 0; k < BEAST_MEM; k++) if (b.mem[k].kind == kind && b.mem[k].source == source && b.mem[k].sid == sid && b.mem[k].strength > 0) { slot = k; break; }
    float prev = 0;
    if (slot >= 0) prev = Recall(b, b.mem[slot], now);
    else {
        float weakest = 1e9f;
        for (int k = 0; k < BEAST_MEM; k++) { float r = Recall(b, b.mem[k], now); if (r < weakest) { weakest = r; slot = k; } }
    }
    b.mem[slot] = {pos, vel, std::max(strength, prev), now, kind, source, sid};
}
void Forget(Beast& b, int source, int sid) {
    for (auto& m : b.mem) if (m.source == source && m.sid == sid) m.strength = 0;
}

// ---------------------------------------------------------------- spawning
void RollPersonality(BeastWorld& W, Beast& b, const SpeciesDef& S) {
    unsigned k = W.seed * 7919u + (unsigned)b.id * 131u;
    b.pers.aggression = Hash(k, 1);
    b.pers.bravery = Hash(k, 2);
    b.pers.energy = Hash(k, 3);
    b.pers.curiosity = Hash(k, 4);
    b.pers.intelligence = Hash(k, 5);
    b.pers.wanderlust = Hash(k, 6);
    b.pers.abnormal = Abnormal::None;
    if (S.move != MoveMode::Sessile && Hash(k, 7) < 0.15f) { // reference 5: about one beast in seven is abnormal
        b.pers.abnormal = (Abnormal)(1 + (int)(Hash(k, 8) * ((int)Abnormal::COUNT - 1)) % ((int)Abnormal::COUNT - 1));
        W.abnormals++;
    }
    b.straggler = S.social && ((b.pers.bravery < 0.3f && b.pers.intelligence < 0.45f) || Hash(k, 9) < 0.12f);
    b.phase = Hash(k, 10) * 6.28f;
    b.hunger = 0.2f + Hash(k, 11) * 0.5f;
    b.facing = Hash(k, 12) < 0.5f ? -1.0f : 1.0f;
}
int NewBeast(BeastWorld& W, int species, Vector2 at) {
    int slot = -1;
    for (int i = 0; i < (int)W.beasts.size(); i++) if (W.beasts[i].life == BeastLife::Gone) { slot = i; break; }
    if (slot < 0) { W.beasts.emplace_back(); slot = (int)W.beasts.size() - 1; }
    Beast& b = W.beasts[slot];
    b = Beast{};
    b.species = species;
    b.id = W.nextId++;
    b.pos = b.lastPos = b.territory = at;
    const SpeciesDef& S = Sp(W.biome, species);
    b.mass = S.mass;
    RollPersonality(W, b, S);
    b.den = NearestDen(W, at, 30 * TILE);
    for (auto& s : b.spine) s = at;
    return slot;
}

// ---------------------------------------------------------------- senses (reference 2.1)
float Camouflage(const BeastWorld& W, const Beast& b) { return Has(Sp(W.biome, b.species), T_CAMO) ? Clamp01(b.special) : 0.0f; }

// A predator is a threat to me if it eats my kind, or if it's rabid, a tyrant on its home ground, or a glutton
// bigger than me.
bool IsThreat(const BeastWorld& W, const Beast& me, const Beast& o) {
    if (&me == &o || !Alive(o)) return false;
    if (Pref(W.biome, o.species, me.species) > 0) return true;
    Abnormal a = o.pers.abnormal;
    if (a == Abnormal::RabidEnraged) return true;
    if (a == Abnormal::GluttonousDevourer && o.mass > me.mass * 1.2f) return true;
    if (a == Abnormal::TerritorialTyrant && Dist(o.pos, o.territory) < 5 * TILE) return true;
    return false;
}

void Perceive(BeastWorld& W, const PlatformState& p, int i) {
    Beast& b = W.beasts[i];
    const SpeciesDef& S = Sp(W.biome, b.species);
    if (b.hidden && b.act != BeastAct::Ambush) { // inside a den it can only hear
        for (const auto& s : W.sounds) {
            float d = Dist(s.pos, b.pos), heard = s.intensity * S.hearing / (1.0f + 0.00012f * d * d);
            if (heard > 0.12f) Remember(b, MEM_SOUND, s.source, 0, s.pos, {0, 0}, heard, W.time);
        }
        return;
    }
    const BiomeDef* B = Biome(W.biome);
    bool deaf = B && B->ignoreDiver; // the Pipes: the diver simply isn't part of these animals' world
    float clarity = B ? B->clarity : 0.6f;
    if (InCloud(W, b.pos)) clarity *= 0.2f;
    float range = S.sight * (0.85f + 0.3f * b.pers.curiosity);
    Vector2 look = Len(b.vel) > 8 ? Norm(b.vel) : Vector2{b.facing, 0};
    float cosFov = cosf(S.fov);
    auto visual = [&](Vector2 tpos, Vector2 tvel, float camo) -> float {
        Vector2 to = Sub(tpos, b.pos);
        float d = Len(to);
        if (d > range || d < 1) return d < 1 ? 1.0f : 0.0f;
        float c = (to.x * look.x + to.y * look.y) / d;
        if (c < cosFov && d > S.radius * 3) return 0; // outside the cone, and not close enough to feel
        if (!W.nav.LineOfSight(b.pos, tpos)) return 0;
        float light = (B && B->water) || Has(S, T_ECHO) ? 1.0f : std::max(0.15f, LightAt(p, W, tpos)); // a bat's ears see in the dark
        return (1.0f - d / range) * std::max(0.3f, c) * clarity * light * (1.0f + 0.003f * Len(tvel)) * (1.0f - 0.85f * camo);
    };
    // other beasts
    for (int j = 0; j < (int)W.beasts.size(); j++) {
        if (j == i) continue;
        Beast& o = W.beasts[j];
        if (o.life == BeastLife::Gone || o.hidden) continue;
        if (fabsf(o.pos.x - b.pos.x) > range || fabsf(o.pos.y - b.pos.y) > range) continue;
        if (o.life == BeastLife::Corpse) {
            if (S.scavenge <= 0 && Pref(W.biome, b.species, o.species) <= 0) continue;
            float v = visual(o.pos, {0, 0}, 0);
            if (v > 0.06f) Remember(b, MEM_FOOD, j, o.id, o.pos, {0, 0}, v * o.meat, W.time);
            continue;
        }
        bool threat = IsThreat(W, b, o), prey = Pref(W.biome, b.species, o.species) > 0 && o.act != BeastAct::Puffed;
        if (b.pers.abnormal == Abnormal::GluttonousDevourer && o.mass < b.mass * 0.9f) prey = true;
        if (b.pers.abnormal == Abnormal::RabidEnraged || b.pers.abnormal == Abnormal::TerritorialTyrant) prey = true;
        if (!threat && !prey) continue;
        float v = visual(o.pos, o.vel, Camouflage(W, o));
        if (v < 0.06f) continue;
        if (threat) Remember(b, MEM_THREAT, j, o.id, o.pos, o.vel, v, W.time);
        if (prey) Remember(b, MEM_PREY, j, o.id, o.pos, o.vel, v, W.time);
    }
    // the diver
    Diver dv = SeeDiver(p);
    if (dv.alive && !deaf) {
        float v = visual(dv.pos, dv.vel, 0);
        if (v > 0.05f) {
            if (S.diverFear > 0) Remember(b, MEM_THREAT, BEAST_DIVER, 0, dv.pos, dv.vel, v, W.time);
            if (S.diverPrey > 0 || b.pers.abnormal == Abnormal::RabidEnraged || b.pers.abnormal == Abnormal::TerritorialTyrant ||
                b.pers.abnormal == Abnormal::MalignantAlpha)
                Remember(b, MEM_PREY, BEAST_DIVER, 0, dv.pos, dv.vel, v, W.time);
        }
        // smell: a keen nose follows the diver's trail even without a sightline (not a position - a direction)
        if (S.smell > 0.6f && S.diverPrey > 0) {
            float tr = W.scent.Sample(W.scent.trail, b.pos) * S.smell;
            if (tr > 0.15f) {
                Vector2 grad = Norm(W.scent.Gradient(W.scent.trail, b.pos));
                if (Len(grad) > 0) Remember(b, MEM_PREY, BEAST_DIVER, 0, Add(b.pos, Mul(grad, 4 * TILE)), {0, 0}, std::min(0.5f, tr * 0.5f), W.time);
            }
        }
    }
    // the level's own enemies (pirates, warriors, cave spiders) are as frightening as the diver, and their
    // musket balls kill beasts just the same (friendly fire)
    if (!deaf && S.diverFear > 0)
        for (int e = 0; e < (int)p.enemies.size(); e++) {
            const PlatEnemy& en = p.enemies[e];
            Vector2 ep{en.pos.x + 16, en.pos.y + 16};
            if (fabsf(ep.x - b.pos.x) > range || fabsf(ep.y - b.pos.y) > range) continue;
            float v = visual(ep, {0, 0}, 0);
            if (v > 0.06f) Remember(b, MEM_THREAT, SRC_ENEMY - e, 0, ep, {0, 0}, v, W.time);
        }
    // sounds (reference 2.1: I / (1 + k d^2))
    for (const auto& s : W.sounds) {
        if (s.source >= 0 && s.source == i) continue;
        if (deaf && s.source == -1) continue;
        float d = Dist(s.pos, b.pos), heard = s.intensity * S.hearing / (1.0f + 0.00012f * d * d);
        // a hunter listens for its prey: the scurry of a rat in the dark is as good as a sighting (an owl hunts by ear)
        if (s.source >= 0 && s.source < (int)W.beasts.size() && Alive(W.beasts[s.source]) && heard > 0.015f &&
            Pref(W.biome, b.species, W.beasts[s.source].species) > 0 && W.beasts[s.source].act != BeastAct::Puffed)
            Remember(b, MEM_PREY, s.source, W.beasts[s.source].id, s.pos, W.beasts[s.source].vel, std::min(0.6f, heard * 6), W.time);
        if (heard > 0.12f) Remember(b, MEM_SOUND, s.source, 0, s.pos, {0, 0}, heard, W.time);
    }
    // blood in the water: scavengers and keen-nosed hunters smell a kill from well out of sight
    if (S.smell > 0.4f && (S.scavenge > 0 || S.diverPrey > 0)) {
        float bl = W.scent.Sample(W.scent.blood, b.pos) * S.smell;
        if (bl > 0.03f) {
            Vector2 grad = Norm(W.scent.Gradient(W.scent.blood, b.pos));
            if (Len(grad) > 0) Remember(b, MEM_FOOD, -3, 0, Add(b.pos, Mul(grad, 5 * TILE)), {0, 0}, std::min(0.8f, bl * 8), W.time);
        }
    }
}

// ---------------------------------------------------------------- drives and alarm calls
float ThreatWeight(const BeastWorld& W, const Beast& b, const BeastMemory& m) {
    const SpeciesDef& S = Sp(W.biome, b.species);
    float w;
    if (m.source == BEAST_DIVER) w = S.diverFear * (1.0f + std::min(1.0f, Len(m.vel) / 500.0f)) * (1.3f - b.pers.bravery);
    else if (m.source <= SRC_ENEMY) w = S.diverFear * (1.3f - b.pers.bravery);
    else {
        float om = m.source >= 0 && m.source < (int)W.beasts.size() ? W.beasts[m.source].mass : 1.0f;
        w = std::clamp(0.4f + 0.18f * log2f(om / std::max(0.02f, b.mass) + 1.0f), 0.3f, 1.3f) * (1.3f - b.pers.bravery);
        if (m.source >= 0 && m.source < (int)W.beasts.size() && (b.trauma >> (W.beasts[m.source].species & 31) & 1)) w += 0.35f;
    }
    if (m.source == BEAST_DIVER && (b.trauma >> 31 & 1)) w += 0.35f;
    return w;
}
int PackCount(const BeastWorld& W, const Beast& b, float r) {
    int n = 0;
    for (const auto& o : W.beasts) if (Alive(o) && !o.hidden && o.species == b.species && Dist(o.pos, b.pos) < r) n++;
    return n;
}
void Drives(BeastWorld& W, int i, float dt) {
    Beast& b = W.beasts[i];
    const SpeciesDef& S = Sp(W.biome, b.species);
    b.hunger = Clamp01(b.hunger + S.hungerRate * (0.6f + 0.8f * b.pers.energy) * dt);
    float fear = 0;
    bool pack = S.social && S.move == MoveMode::Walk && PackCount(W, b, 100) >= 3; // reference: small beasts in numbers stop being prey
    for (const auto& m : b.mem) if (m.kind == MEM_THREAT) fear = std::max(fear, Recall(b, m, W.time) * ThreatWeight(W, b, m) * (pack ? 0.5f : 1.0f));
    b.fear = Clamp01(fear * 1.6f);
    bool sprinting = Len(b.vel) > S.speed * 1.4f;
    b.fatigue = Clamp01(b.fatigue + (sprinting ? 0.1f : b.hidden ? -0.12f : -0.02f) * dt);
    if (b.hidden) b.hunger = Clamp01(b.hunger + S.hungerRate * dt * 0.5f); // gets hungry waiting it out
    // alarm call (the reference's pack communication): a frightened beast warns its own kind and others that
    // share its predator, passing on where the threat is
    b.alarmT -= dt;
    if (b.fear > 0.55f && b.alarmT <= 0) {
        b.alarmT = 1.2f;
        const BeastMemory* worst = nullptr; float wv = 0;
        for (const auto& m : b.mem) if (m.kind == MEM_THREAT) { float v = Recall(b, m, W.time); if (v > wv) { wv = v; worst = &m; } }
        if (worst) for (int j = 0; j < (int)W.beasts.size(); j++) {
            Beast& o = W.beasts[j];
            if (j == i || !Alive(o) || Dist(o.pos, b.pos) > 110) continue;
            bool related = o.species == b.species;
            if (!related && worst->source >= 0 && worst->source < (int)W.beasts.size()) related = Pref(W.biome, W.beasts[worst->source].species, o.species) > 0;
            if (related) Remember(o, MEM_THREAT, worst->source, worst->sid, worst->pos, worst->vel, wv * 0.7f, W.time);
        }
    }
}

// ---------------------------------------------------------------- choosing what to do (reference 2.3 / 4.2)
void Consider(const Beast& b, Choice& best, BeastAct a, float u, int tgt, int tid, Vector2 g) {
    if (a == b.act) u += 0.12f; // hysteresis
    if (u > best.score) best = {a, tgt, tid, g, u};
}

// How badly it wants to eat this (reference 2.1's W(i,j) = H * (S * pref - risk) * scent, plus isolation and
// ease - so a predator stalking the diver drops you the moment an easier meal wanders close).
float PreyScore(const BeastWorld& W, const Beast& b, const BeastMemory& m) {
    const SpeciesDef& S = Sp(W.biome, b.species);
    float recall = Recall(b, m, W.time);
    if (recall < 0.04f) return 0;
    float pref, suit = 1, risk, iso = 1;
    if (m.source == BEAST_DIVER) {
        pref = std::max(S.diverPrey, b.pers.abnormal == Abnormal::RabidEnraged || b.pers.abnormal == Abnormal::MalignantAlpha ? 0.8f : 0.0f);
        risk = 0.25f * (1.2f - b.pers.bravery) + ((b.trauma >> 31 & 1) ? 0.3f : 0.0f);
    } else {
        if (m.source < 0 || m.source >= (int)W.beasts.size()) return 0;
        const Beast& o = W.beasts[m.source];
        if (o.id != m.sid || !Alive(o) || o.hidden || o.act == BeastAct::Puffed) return 0;
        pref = Pref(W.biome, b.species, o.species);
        if (Has(Sp(W.biome, o.species), T_TOXIC) && (b.trauma >> (o.species & 31) & 1)) pref *= 0.08f; // it learned the hard way
        if (pref <= 0 && (b.pers.abnormal == Abnormal::GluttonousDevourer || b.pers.abnormal == Abnormal::RabidEnraged || b.pers.abnormal == Abnormal::TerritorialTyrant)) pref = 0.6f;
        float ratio = o.mass / std::max(0.01f, b.mass);
        suit = Clamp01(1.0f - 0.35f * fabsf(log10f(std::max(ratio, 0.001f) / 0.2f)));
        risk = (ratio > 0.8f ? 0.35f : 0.05f) * (1.2f - b.pers.bravery);
        if (Sp(W.biome, o.species).social) { // a fish that's strayed from its school is the easy one - and a sharp hunter knows it
            int n = 0;
            for (const auto& k : W.beasts) if (Alive(k) && k.species == o.species && Dist(k.pos, o.pos) < 50) n++;
            iso = 1.0f + (0.5f + 1.2f * b.pers.intelligence) / (float)std::max(1, n - 1);
        }
        if (o.straggler && o.act == BeastAct::Flee) iso += 0.3f + 0.5f * b.pers.intelligence;
    }
    float d = Dist(m.pos, b.pos), ease = std::clamp(1.0f - d / (S.sight * 1.3f), 0.1f, 1.0f);
    return b.hunger * std::max(0.0f, suit * pref - risk) * recall * (0.55f + 0.45f * ease) * iso;
}

Choice ChooseUtility(BeastWorld& W, const PlatformState& p, int i) {
    Beast& b = W.beasts[i];
    const SpeciesDef& S = Sp(W.biome, b.species);
    Choice best;
    auto consider = [&](BeastAct a, float u, int tgt, int tid, Vector2 g) {
        if (a == b.act) u += 0.12f; // hysteresis
        if (u > best.score) best = {a, tgt, tid, g, u};
    };
    Vector2 home = Home(W, b);
    // flee
    float uFlee = Sig(b.fear, 10, 0.3f + 0.35f * b.pers.bravery);
    consider(BeastAct::Flee, uFlee, -1, 0, b.pos);
    // hunt
    float bestW = 0; const BeastMemory* bm = nullptr;
    for (const auto& m : b.mem) if (m.kind == MEM_PREY) { float w = PreyScore(W, b, m); if (w > bestW) { bestW = w; bm = &m; } }
    if (bm) consider(BeastAct::Hunt, Sig(bestW, 11, 0.2f) * (0.5f + 0.5f * b.pers.aggression), bm->source, bm->sid, bm->pos);
    // scavenge
    if (S.scavenge > 0) {
        float bestF = 0; const BeastMemory* fm = nullptr;
        for (const auto& m : b.mem) if (m.kind == MEM_FOOD) { float f = Recall(b, m, W.time); if (f > bestF) { bestF = f; fm = &m; } }
        if (fm) consider(BeastAct::Scavenge, Sig((0.35f + b.hunger) * bestF * S.scavenge, 8, 0.14f), fm->source, fm->sid, fm->pos); // free meat is worth a detour even on a half-full belly
    }
    // rest in the den
    if (b.den >= 0 && S.denAffinity > 0)
        consider(BeastAct::Rest, S.denAffinity * (Sig(b.fatigue, 8, 0.55f) + (b.hunger < 0.2f ? 0.35f : 0.0f)), -1, 0, DenMouth(W, b.den));
    // investigate a sound
    float bestS = 0; const BeastMemory* sm = nullptr;
    for (const auto& m : b.mem) if (m.kind == MEM_SOUND) { float s = Recall(b, m, W.time); if (s > bestS) { bestS = s; sm = &m; } }
    if (sm && Dist(sm->pos, b.pos) > TILE) consider(BeastAct::Investigate, Sig(b.pers.curiosity * bestS, 9, 0.3f) * (1 - b.fear), -1, 0, sm->pos);
    // explore (the "exploratory personality": a far roam, chosen rather than patrolled)
    float explore = Sig(b.pers.wanderlust * (1 - b.fear) * (1 - 0.5f * b.hunger) * (0.5f + 0.5f * b.pers.energy), 8, 0.42f) * 0.75f;
    bool eatsSomething = S.scavenge > 0;
    if (const BiomeDef* BB = Biome(W.biome)) for (int k = 0; k < BB->webN && !eatsSomething; k++) eatsSomething = BB->web[k].pred == b.species;
    if (eatsSomething && !bm) explore = std::max(explore, Sig(b.hunger * (1 - b.fear), 9, 0.55f) * 0.6f); // hungry with nothing in view: it goes foraging
    if (b.act == BeastAct::Wander && b.actT < 3) explore *= 0.3f;
    consider(BeastAct::Explore, explore, -1, 0, b.goal);
    consider(BeastAct::Wander, 0.22f, -1, 0, home);
    // behaviours any species can have (beasts.h, BeastTrait)
    if (Has(S, T_GROOMER)) { // a cleaner tends a resting client - who never eats it
        for (int j = 0; j < (int)W.beasts.size(); j++) {
            const Beast& e = W.beasts[j];
            if (!Alive(e) || !Has(Sp(W.biome, e.species), T_GROOMED) || e.act == BeastAct::Hunt || e.act == BeastAct::Strike || e.act == BeastAct::Coil) continue;
            if (Dist(e.pos, b.pos) < 220) { consider(BeastAct::Groom, 0.45f * (1 - b.fear) * (1 - b.hunger * 0.5f), j, e.id, e.pos); break; }
        }
    }
    if (Has(S, T_MOBBER) && PackCount(W, b, 100) >= 3) { // in numbers they turn on an intruder not too much bigger than them
        for (const auto& m : b.mem) if (m.kind == MEM_THREAT && m.source >= 0 && m.source < (int)W.beasts.size()) {
            const Beast& t = W.beasts[m.source];
            if (Alive(t) && t.mass < b.mass * 7 && Dist(t.pos, b.pos) < 120)
                consider(BeastAct::Mob, 0.55f + 0.3f * b.pers.aggression, m.source, m.sid, t.pos);
        }
    }
    if (Has(S, T_DEFENSIVE) && b.pers.bravery > 0.35f) { // cornered, it turns and charges whatever is pressing it
        for (const auto& m : b.mem) if (m.kind == MEM_THREAT && Recall(b, m, W.time) > 0.35f && Dist(m.pos, b.pos) < 5 * TILE && (m.source == BEAST_DIVER || m.source >= 0)) {
            consider(BeastAct::Hunt, Sig(b.fear * (0.5f + b.pers.bravery) * (0.6f + 0.6f * b.pers.aggression), 8, 0.3f) + 0.1f, m.source, m.sid, m.pos);
            break;
        }
    }
    if ((Has(S, T_CAMO) && b.special > 0.6f) || (Has(S, T_DEN_AMBUSH) && b.den >= 0)) { // lie in wait
        float wait = (1 - b.fear) * (0.35f + 0.4f * b.pers.intelligence) * (1 - b.pers.energy * 0.5f);
        if (Has(S, T_DEN_AMBUSH)) wait = (1 - b.fear) * (0.2f + 0.3f * b.pers.intelligence) * (1 - 0.4f * b.pers.energy) * (1.1f - b.hunger) + 0.25f * b.fatigue; // it mostly patrols, and waits in its hole to rest
        consider(BeastAct::Ambush, wait, -1, 0, Has(S, T_DEN_AMBUSH) ? DenMouth(W, b.den) : b.pos);
    }
    if (Has(S, T_KLEPTO) && b.hunger > 0.3f) { // a thief: somebody else's meal is the easiest one there is
        for (int j = 0; j < (int)W.beasts.size(); j++) {
            const Beast& o = W.beasts[j];
            if (j == i || !Alive(o) || o.act != BeastAct::Eat || !Valid(W, o.target, o.targetId) || Dist(o.pos, b.pos) > 8 * TILE || o.mass > b.mass * 6) continue;
            consider(BeastAct::Scavenge, 0.35f + 0.4f * b.hunger * (0.5f + b.pers.bravery), o.target, o.targetId, W.beasts[o.target].pos);
            break;
        }
    }
    if (const BiomeDef* B = Biome(W.biome)) if (B->extras) B->extras(W, p, i, best);
    return best;
}

// The reference's twelve abnormal profiles: each overrides the utility choice outright when it applies, and
// falls back to ordinary behaviour when it has nothing to do.
bool ChooseAbnormal(BeastWorld& W, const PlatformState& p, int i, Choice& c) {
    Beast& b = W.beasts[i];
    Diver dv = SeeDiver(p);
    auto nearestLiving = [&](float r, bool smallerOnly) -> int {
        int best = -1; float bd = r;
        for (int j = 0; j < (int)W.beasts.size(); j++) {
            const Beast& o = W.beasts[j];
            if (j == i || !Alive(o) || o.hidden) continue;
            if (smallerOnly && o.mass >= b.mass * 0.9f) continue;
            float d = Dist(o.pos, b.pos);
            if (d < bd && W.nav.LineOfSight(b.pos, o.pos)) { bd = d; best = j; }
        }
        return best;
    };
    switch (b.pers.abnormal) {
    case Abnormal::RabidEnraged: { // attacks the nearest living thing, whatever it is, and is dying of it
        int t = nearestLiving(8 * TILE, false);
        bool diver = dv.alive && Dist(dv.pos, b.pos) < 8 * TILE && W.nav.LineOfSight(b.pos, dv.pos) && (t < 0 || Dist(dv.pos, b.pos) < Dist(W.beasts[t].pos, b.pos));
        if (diver) { c = {BeastAct::Hunt, BEAST_DIVER, 0, dv.pos, 1}; return true; }
        if (t >= 0) { c = {BeastAct::Hunt, t, W.beasts[t].id, W.beasts[t].pos, 1}; return true; }
        return false;
    }
    case Abnormal::Kleptomaniac: { // snatches a kill out from under whoever's eating it, and runs for home
        if (b.carry >= 0) { c = {BeastAct::Flee, -1, 0, b.den >= 0 ? DenMouth(W, b.den) : b.territory, 1}; return true; }
        for (int j = 0; j < (int)W.beasts.size(); j++) {
            const Beast& o = W.beasts[j];
            if (j != i && Alive(o) && o.act == BeastAct::Eat && Valid(W, o.target, o.targetId) && Dist(o.pos, b.pos) < 7 * TILE) {
                c = {BeastAct::Scavenge, o.target, o.targetId, W.beasts[o.target].pos, 1}; return true;
            }
        }
        return false;
    }
    case Abnormal::HyperProtectiveParent: { // peaceful until something comes near its den, then furious
        if (b.den < 0) return false;
        Vector2 den = W.dens[b.den].pos;
        if (dv.alive && Dist(dv.pos, den) < 6 * TILE) { c = {BeastAct::Guard, BEAST_DIVER, 0, dv.pos, 1}; return true; }
        for (int j = 0; j < (int)W.beasts.size(); j++) {
            const Beast& o = W.beasts[j];
            if (j != i && Alive(o) && !o.hidden && o.species != b.species && Dist(o.pos, den) < 4 * TILE) { c = {BeastAct::Guard, j, o.id, o.pos, 1}; return true; }
        }
        c = {BeastAct::Wander, -1, 0, DenMouth(W, b.den), 0.5f};
        return true;
    }
    case Abnormal::SuicidalSelfDestructive: { // seeks out the nearest hazard and throws itself on it
        int bx = (int)floorf(b.pos.x / TILE), by = (int)floorf(b.pos.y / TILE), best = -1; float bd = 1e9f; Vector2 bp{0, 0};
        for (int dy = -8; dy <= 8; dy++) for (int dx = -12; dx <= 12; dx++) {
            if (!W.nav.Hazard(bx + dx, by + dy)) continue;
            float d = (float)(dx * dx + dy * dy);
            if (d < bd) { bd = d; best = 1; bp = {(bx + dx) * TILE + 16, (by + dy) * TILE + 16}; }
        }
        if (best < 0) return false;
        c = {BeastAct::Investigate, -1, 0, bp, 1};
        return true;
    }
    case Abnormal::ParasiticHost: { // latches onto something bigger - or the diver - and rides it
        if (b.latched != -1) { c = {BeastAct::Latched, b.latched, b.targetId, b.pos, 1}; return true; }
        int best = -1; float bd = 5 * TILE;
        for (int j = 0; j < (int)W.beasts.size(); j++) {
            const Beast& o = W.beasts[j];
            if (j == i || !Alive(o) || o.hidden || o.mass < b.mass * 2) continue;
            float d = Dist(o.pos, b.pos);
            if (d < bd) { bd = d; best = j; }
        }
        if (dv.alive && Dist(dv.pos, b.pos) < bd && W.nav.LineOfSight(b.pos, dv.pos)) { c = {BeastAct::Follow, BEAST_DIVER, 0, dv.pos, 1}; return true; }
        if (best >= 0) { c = {BeastAct::Follow, best, W.beasts[best].id, W.beasts[best].pos, 1}; return true; }
        return false;
    }
    case Abnormal::CovetousHoarder: { // drags any carcass it finds back to its den
        if (b.carry >= 0) { c = {BeastAct::Flee, -1, 0, b.den >= 0 ? DenMouth(W, b.den) : b.territory, 1}; return true; }
        int best = -1; float bd = 9 * TILE;
        for (int j = 0; j < (int)W.beasts.size(); j++) {
            const Beast& o = W.beasts[j];
            if (o.life != BeastLife::Corpse || o.mass > b.mass * 3) continue;
            float d = Dist(o.pos, b.pos);
            if (d < bd) { bd = d; best = j; }
        }
        if (best >= 0) { c = {BeastAct::Scavenge, best, W.beasts[best].id, W.beasts[best].pos, 1}; return true; }
        return false;
    }
    case Abnormal::SymbioticCompanion: // shadows the diver, lighting the way, harmless
        if (dv.alive && Dist(dv.pos, b.pos) < 14 * TILE) { c = {BeastAct::Follow, BEAST_DIVER, 0, Add(dv.pos, Vector2{-40.0f, -34.0f}), 1}; return true; }
        return false;
    case Abnormal::TerritorialTyrant: { // anything that enters its ground gets attacked
        if (dv.alive && Dist(dv.pos, b.territory) < 5 * TILE) { c = {BeastAct::Guard, BEAST_DIVER, 0, dv.pos, 1}; return true; }
        for (int j = 0; j < (int)W.beasts.size(); j++) {
            const Beast& o = W.beasts[j];
            if (j != i && Alive(o) && !o.hidden && Dist(o.pos, b.territory) < 5 * TILE) { c = {BeastAct::Guard, j, o.id, o.pos, 1}; return true; }
        }
        c = {BeastAct::Wander, -1, 0, b.territory, 0.5f};
        return true;
    }
    case Abnormal::PyromaniacShockSeeker: { // drawn to energy: live plating, vents, torpedoes in flight
        int bx = (int)floorf(b.pos.x / TILE), by = (int)floorf(b.pos.y / TILE); float bd = 1e9f; Vector2 bp{0, 0}; bool any = false;
        for (int dy = -8; dy <= 8; dy++) for (int dx = -14; dx <= 14; dx++) {
            char ch = PlatTileAt(p, bx + dx, by + dy);
            if (ch != 't' && ch != 'v' && ch != 'T') continue;
            float d = (float)(dx * dx + dy * dy);
            if (d < bd) { bd = d; any = true; bp = {(bx + dx) * TILE + 16, (by + dy) * TILE - 8}; }
        }
        for (const auto& s : p.shots) if (Dist(s.pos, b.pos) < 10 * TILE) { any = true; bp = s.pos; break; }
        if (!any) return false;
        c = {BeastAct::Investigate, -1, 0, bp, 1};
        return true;
    }
    case Abnormal::GluttonousDevourer: return false; // handled in the prey scoring: it hunts anything smaller
    case Abnormal::PhobicNyctophobic: { // in the dark it panics for the nearest light - often the diver's lamp
        if (LightAt(p, W, b.pos) >= 0.3f) return false;
        Vector2 lp = dv.alive ? dv.pos : b.territory; float bd = dv.alive ? Dist(dv.pos, b.pos) : 1e9f;
        int bx = (int)floorf(b.pos.x / TILE), by = (int)floorf(b.pos.y / TILE);
        for (int dy = -10; dy <= 10; dy++) for (int dx = -14; dx <= 14; dx++) if (PlatTileAt(p, bx + dx, by + dy) == 'o') {
            Vector2 q{(bx + dx) * TILE + 16, (by + dy) * TILE + 16};
            if (Dist(q, b.pos) < bd) { bd = Dist(q, b.pos); lp = q; }
        }
        c = {BeastAct::Flee, -1, 0, lp, 1};
        return true;
    }
    case Abnormal::MalignantAlpha: { // calls every one of its kind nearby onto its own target
        float bestW = 0; const BeastMemory* bm = nullptr;
        for (const auto& m : b.mem) if (m.kind == MEM_PREY) { float w = PreyScore(W, b, m) + 0.2f; if (w > bestW) { bestW = w; bm = &m; } }
        if (!bm) return false;
        c = {BeastAct::Hunt, bm->source, bm->sid, bm->pos, 1};
        for (auto& o : W.beasts) if (&o != &b && Alive(o) && o.species == b.species && Dist(o.pos, b.pos) < 15 * TILE)
            Remember(o, MEM_PREY, bm->source, bm->sid, bm->pos, bm->vel, 1.0f, W.time);
        return true;
    }
    default: return false;
    }
}

// ---------------------------------------------------------------- movement
// A box of half-extents (hw, hh) moved one axis at a time against the tiles - the same scheme the diver uses.
void Collide(const NavGrid& N, Vector2& pos, Vector2& vel, float hw, float hh, float dt, bool& grounded) {
    grounded = false;
    pos.x += vel.x * dt;
    int y0 = (int)floorf((pos.y - hh) / TILE), y1 = (int)floorf((pos.y + hh - 0.01f) / TILE);
    if (vel.x > 0) {
        int tx = (int)floorf((pos.x + hw - 0.01f) / TILE);
        for (int ty = y0; ty <= y1; ty++) if (N.Solid(tx, ty)) { pos.x = tx * TILE - hw; vel.x = 0; break; }
    } else if (vel.x < 0) {
        int tx = (int)floorf((pos.x - hw) / TILE);
        for (int ty = y0; ty <= y1; ty++) if (N.Solid(tx, ty)) { pos.x = (tx + 1) * TILE + hw; vel.x = 0; break; }
    }
    pos.y += vel.y * dt;
    int x0 = (int)floorf((pos.x - hw) / TILE), x1 = (int)floorf((pos.x + hw - 0.01f) / TILE);
    if (vel.y > 0) {
        int ty = (int)floorf((pos.y + hh - 0.01f) / TILE);
        for (int tx = x0; tx <= x1; tx++) if (N.Solid(tx, ty)) { pos.y = ty * TILE - hh; vel.y = 0; grounded = true; break; }
    } else if (vel.y < 0) {
        int ty = (int)floorf((pos.y - hh) / TILE);
        for (int tx = x0; tx <= x1; tx++) if (N.Solid(tx, ty)) { pos.y = (ty + 1) * TILE + hh; vel.y = 0; break; }
    }
}
float HalfW(const SpeciesDef& S, const Beast& b) { return std::min(14.0f, S.radius * b.scale * 0.8f); }
float HalfH(const SpeciesDef& S, const Beast& b) { return std::min(12.0f, S.radius * b.scale * (S.move == MoveMode::Walk ? 0.6f : 0.7f)); }

// Follows the route toward `target`, replanning as the target moves. A dim beast sometimes skips the planning and
// just heads straight at it - and gets stuck behind the first wall, as a dim animal would.
Vector2 RouteToward(BeastWorld& W, Beast& b, const SpeciesDef& S, Vector2 target, float dt) {
    b.replanT -= dt;
    bool swim = S.move == MoveMode::Swim || S.move == MoveMode::Fly, climb = S.move == MoveMode::Climb;
    if (swim && Dist(target, b.pos) < 10 * TILE && W.nav.LineOfSight(b.pos, target)) { b.path.clear(); return target; }
    bool greedy = b.pers.intelligence < 0.2f && Hash((unsigned)b.id, (unsigned)(W.time * 0.5f)) < 0.5f;
    if (greedy) return target;
    bool stale = b.path.empty() || b.pathI >= (int)b.path.size() || Dist(target, b.pathGoal) > 1.5f * TILE || b.replanT <= 0;
    if (stale && W.replansLeft > 0) {
        W.replansLeft--;
        W.nav.FindPath(S.move, b.pos, target, b.path, 2400);
        b.pathI = 0;
        b.pathGoal = target;
        b.replanT = 0.7f + (1 - b.pers.intelligence) * 1.3f;
        if (b.path.empty()) return target;
        // string-pull for swimmers: skip every waypoint already in plain sight
        if (swim) while (b.pathI + 1 < (int)b.path.size() && W.nav.LineOfSight(b.pos, b.path[b.pathI + 1])) b.pathI++;
    }
    if (b.path.empty() || b.pathI >= (int)b.path.size()) return target;
    Vector2 wp = b.path[b.pathI];
    float reach = swim ? 14.0f : 10.0f;
    if (((swim || climb) && Dist(wp, b.pos) < reach) || (!swim && !climb && fabsf(wp.x - b.pos.x) < reach && fabsf(wp.y - b.pos.y) < TILE * 1.2f)) {
        b.pathI++;
        if (swim) while (b.pathI + 1 < (int)b.path.size() && W.nav.LineOfSight(b.pos, b.path[b.pathI + 1])) b.pathI++;
        if (b.pathI < (int)b.path.size()) wp = b.path[b.pathI]; else wp = target;
    }
    return wp;
}

void Motor(BeastWorld& W, const PlatformState& p, int i, Vector2 target, float speed, float dt) {
    Beast& b = W.beasts[i];
    const SpeciesDef& S = Sp(W.biome, b.species);
    if (S.move == MoveMode::Sessile) { b.vel = {0, 0}; return; }
    target.x = std::min(target.x, W.limitX - TILE);
    Vector2 wp = RouteToward(W, b, S, target, dt);
    float hw = HalfW(S, b), hh = HalfH(S, b);
    if (S.move == MoveMode::Walk) {
        float dx = wp.x - b.pos.x;
        float want = fabsf(dx) < 4 ? 0.0f : (dx > 0 ? 1.0f : -1.0f) * speed * std::min(1.0f, fabsf(dx) / 24.0f + 0.3f);
        // ledge sense: an animal won't walk off into a drop it can't see the bottom of, onto spikes, or into the
        // sea - unless its planned route says the landing over there is good (a hop across a gap)
        int feetY = (int)floorf((b.pos.y + hh - 1) / TILE);
        auto safeCol = [&](int x, int y0) { for (int k = 0; k <= 6; k++) { int y = y0 + k; if (W.nav.Hazard(x, y)) return false; if (W.nav.Solid(x, y)) return true; } return false; };
        bool onPath = !b.path.empty() && b.pathI < (int)b.path.size() && Dist(wp, b.path[b.pathI]) < 1;
        int aheadX = (int)floorf((b.pos.x + (dx > 0 ? 1 : -1) * (hw + 4)) / TILE);
        bool reckless = b.pers.abnormal == Abnormal::SuicidalSelfDestructive || b.act == BeastAct::Strike;
        bool safe = onPath ? safeCol((int)floorf(wp.x / TILE), (int)floorf(wp.y / TILE)) || safeCol(aheadX, feetY) : safeCol(aheadX, feetY);
        if (b.grounded && !safe && !reckless) { want = 0; b.vel.x *= 0.5f; }
        float ax = S.accel * (b.grounded ? 1.0f : 0.45f) * dt;
        b.vel.x += std::clamp(want - b.vel.x, -ax, ax);
        b.hopT -= dt;
        if (b.grounded && b.hopT <= 0) {
            int cy = feetY, wy = (int)floorf(wp.y / TILE), sx = dx > 0 ? 1 : -1;
            int ahead = (int)floorf((b.pos.x + sx * (hw + 6)) / TILE);
            bool wall = W.nav.Solid(ahead, cy);
            int top = cy; // how tall the obstacle is: it only tries what it can clear
            while (wall && W.nav.Solid(ahead, top) && cy - top < 4) top--;
            bool climbable = wall && !W.nav.Solid(ahead, top) && cy - top <= 3 && (onPath || safeCol(ahead, top));
            bool gap = !W.nav.Solid(ahead, cy + 1) && wy <= cy && fabsf(dx) > TILE * 0.7f && safe && onPath;
            bool up = !wall && wy < cy && fabsf(dx) < TILE * 2.2f && (onPath || safeCol((int)floorf(wp.x / TILE), wy));
            if (up || gap || (climbable && wy <= cy)) {
                float rise = (climbable ? (cy - top) : std::max(1, cy - wy)) * TILE + 18;
                b.vel.y = -sqrtf(2 * WALK_G * rise);
                float carry = gap ? fabsf(dx) : std::min(fabsf(dx), TILE * 1.2f); // just far enough to land - not over the bulwark
                b.vel.x = sx * std::min(carry * 2.6f + 30, S.sprint);
                b.hopT = 0.25f; // a hop settles before the next one
            }
        }
        b.vel.y = std::min(b.vel.y + WALK_G * dt, 900.0f);
    } else {
        Vector2 desired = Mul(Norm(Sub(wp, b.pos)), speed);
        if (Dist(wp, b.pos) < 6) desired = {0, 0};
        // schools (Reynolds boids, reference 3.5): keep apart, match heading, drift to the centre - stragglers
        // under threat don't bother, which is exactly how they end up alone
        if (S.social && !(b.straggler && b.act == BeastAct::Flee)) {
            Vector2 sep{0, 0}, ali{0, 0}, coh{0, 0}; int n = 0;
            for (const auto& o : W.beasts) {
                if (&o == &b || !Alive(o) || o.species != b.species || o.hidden) continue;
                Vector2 d = Sub(b.pos, o.pos); float dd = Len(d);
                if (dd < 14 && dd > 0.01f) sep = Add(sep, Mul(d, 1.0f / (dd * dd)));
                if (dd < 70) { ali = Add(ali, o.vel); coh = Add(coh, o.pos); n++; }
            }
            if (n > 0) {
                ali = Mul(ali, 1.0f / n);
                coh = Sub(Mul(coh, 1.0f / n), b.pos);
                float cw = b.straggler ? 0.2f : 0.9f; // a straggler only loosely keeps formation and drifts to the fringe
                desired = Add(desired, Add(Mul(sep, 900.0f), Add(Mul(Sub(ali, b.vel), 0.35f), Mul(coh, cw))));
            }
        }
        // everyone gives each other a little room
        for (const auto& o : W.beasts) {
            if (&o == &b || !Alive(o) || o.hidden) continue;
            float minD = (S.radius + Sp(W.biome, o.species).radius) * 0.9f;
            Vector2 d = Sub(b.pos, o.pos); float dd = Len(d);
            if (dd < minD && dd > 0.01f) desired = Add(desired, Mul(d, 40.0f / dd));
        }
        Vector2 dv = Sub(desired, b.vel);
        float mag = Len(dv), maxA = S.accel * dt;
        if (mag > maxA) dv = Mul(dv, maxA / mag);
        b.vel = Add(b.vel, dv);
        float sp = Len(b.vel), cap = S.sprint * 1.3f * b.scale;
        if (sp > cap) b.vel = Mul(b.vel, cap / sp);
        b.vel = Mul(b.vel, std::max(0.0f, 1.0f - (0.4f + 0.0015f * sp) * dt)); // quadratic water (or air) drag
        if (S.move == MoveMode::Climb) { // a crawler holds on to whatever surface it's on; with nothing to hold, it drops
            bool touching = false;
            for (int dy = -1; dy <= 1 && !touching; dy++)
                for (int dx = -1; dx <= 1 && !touching; dx++)
                    if (W.nav.Solid((int)floorf((b.pos.x + dx * (hw + 3)) / TILE), (int)floorf((b.pos.y + dy * (hh + 3)) / TILE))) touching = true;
            if (!touching) b.vel.y = std::min(b.vel.y + WALK_G * dt, 700.0f);
        }
    }
    bool g;
    Collide(W.nav, b.pos, b.vel, hw, hh, dt, g);
    b.grounded = g;
    b.pos.x = std::clamp(b.pos.x, hw, std::min(W.nav.w * TILE - hw, W.limitX));
    b.pos.y = std::clamp(b.pos.y, hh, W.nav.h * TILE - hh);
    if (fabsf(b.vel.x) > 6) b.facing = b.vel.x > 0 ? 1.0f : -1.0f;
}

// An intelligent flight: the nearest den that isn't past the threat, or else the reachable spot that puts the
// most distance between it and the threat. A dim one just bolts directly away.
Vector2 FleeTarget(BeastWorld& W, int i, Vector2 threat) {
    Beast& b = W.beasts[i];
    const SpeciesDef& S = Sp(W.biome, b.species);
    Vector2 away = Norm(Sub(b.pos, threat));
    if (Len(away) < 0.1f) away = {b.facing, 0};
    if (b.pers.intelligence < 0.25f || (b.straggler && Hash((unsigned)b.id, (unsigned)(W.time)) < 0.4f))
        return Add(b.pos, Mul(away, 6 * TILE));
    if (S.denAffinity > 0.2f) {
        int best = -1; float bd = 16 * TILE;
        for (int d = 0; d < (int)W.dens.size(); d++) {
            Vector2 dp = W.dens[d].pos;
            float dd = Dist(dp, b.pos);
            if (dd < bd && Dist(dp, threat) > dd * 0.8f) { bd = dd; best = d; }
        }
        if (best >= 0) { b.den = best; return DenMouth(W, best); }
    }
    Vector2 bestP = Add(b.pos, Mul(away, 5 * TILE)); float bestScore = -1e9f;
    for (int k = 0; k < 10; k++) {
        float a = k * 2 * PI / 10 + R(W) * 0.3f, dist = (5 + R(W) * 5) * TILE;
        Vector2 q = Add(b.pos, Vector2{cosf(a) * dist, sinf(a) * dist});
        int qx = (int)floorf(q.x / TILE), qy = (int)floorf(q.y / TILE);
        if (S.move == MoveMode::Walk || S.move == MoveMode::Climb) { int f = W.nav.FloorBelow(qx, qy); if (f >= W.nav.h) continue; qy = f - 1; q = {qx * TILE + 16, qy * TILE + 16}; }
        if (!W.nav.Open(qx, qy) || W.nav.Hazard(qx, qy)) continue;
        float score = Dist(q, threat) - 0.4f * Dist(q, b.pos) - (q.x > W.limitX ? 1e6f : 0.0f);
        if (score > bestScore) { bestScore = score; bestP = q; }
    }
    return bestP;
}

// A far target for an explorer: somewhere reachable, well away from home, further the more it likes to roam.
Vector2 ExploreTarget(BeastWorld& W, int i) {
    Beast& b = W.beasts[i];
    const SpeciesDef& S = Sp(W.biome, b.species);
    Vector2 home = Home(W, b);
    for (int k = 0; k < 24; k++) {
        float reach = (6 + 44 * b.pers.wanderlust) * TILE * (0.5f + 0.5f * R(W));
        float x = home.x + (R(W) < 0.5f ? -reach : reach);
        int qx = (int)floorf(x / TILE);
        if (qx < 1 || qx >= W.nav.w - 1 || x > W.limitX) continue;
        int qy;
        if (S.move == MoveMode::Walk || S.move == MoveMode::Climb) { int f = W.nav.FloorBelow(qx, (int)floorf(b.pos.y / TILE) - 6); if (f >= W.nav.h) continue; qy = f - 1; }
        else { int f = W.nav.FloorBelow(qx, 2); if (f >= W.nav.h) continue; qy = f - 1 - S.band - (int)(R(W) * (3 + 6 * b.pers.wanderlust)); }
        if (!W.nav.Open(qx, qy) || W.nav.Hazard(qx, qy)) continue;
        return {qx * TILE + 16, qy * TILE + 16};
    }
    return home;
}
Vector2 WanderTarget(BeastWorld& W, int i) {
    Beast& b = W.beasts[i];
    const SpeciesDef& S = Sp(W.biome, b.species);
    Vector2 home = Home(W, b);
    float leash = (4 + 12 * b.pers.wanderlust) * TILE;
    for (int k = 0; k < 16; k++) {
        float x = home.x + R(W, -leash, leash);
        int qx = (int)floorf(x / TILE);
        if (qx < 1 || qx >= W.nav.w - 1 || x > W.limitX) continue;
        int f = W.nav.FloorBelow(qx, (int)floorf(b.pos.y / TILE) - 4);
        if (f >= W.nav.h) continue;
        int qy = S.move == MoveMode::Walk || S.move == MoveMode::Climb ? f - 1 : f - 1 - S.band + (int)R(W, -2, 2);
        if (!W.nav.Open(qx, qy) || W.nav.Hazard(qx, qy)) continue;
        return {qx * TILE + 16, qy * TILE + 16};
    }
    return b.pos;
}

// ---------------------------------------------------------------- deaths, corpses, dens
void Kill(BeastWorld& W, PlatformState& p, int v, int killer) {
    Beast& b = W.beasts[v];
    if (!Alive(b)) return;
    b.life = BeastLife::Corpse;
    b.corpseT = 0;
    b.meat = 1;
    b.act = BeastAct::Idle;
    b.hidden = false;
    b.vel = {b.vel.x * 0.2f, 20};
    b.latched = -1;
    W.kills++;
    if (killer >= 0) W.deaths[0]++;
    W.scent.Emit(W.scent.blood, b.pos, 30.0f);
    W.sounds.push_back({b.pos, 0.7f, 0.6f, killer});
    if (!p.verifying) { PlatBurst(p, b.pos, 12, Color{150, 30, 40, 255}, 110, 0.6f, 3); if (W.biome == PL_HULL) PlatBubbles(p, b.pos, 4); }
    for (auto& o : W.beasts) {
        if (o.latched == v) { o.latched = -1; o.act = BeastAct::Drift; o.actT = 0; }
        if (o.carry == v) o.carry = -1;
    }
}
void Hurt(BeastWorld& W, PlatformState& p, int v, float dmg, int attackerSpecies, bool byDiver) {
    Beast& b = W.beasts[v];
    b.health -= dmg;
    if (dmg >= 0.3f) b.trauma |= byDiver ? (1u << 31) : (1u << (attackerSpecies & 31)); // reference 2.2's traumatic memory
    if (b.health <= 0) Kill(W, p, v, -1);
}

void EnterDen(BeastWorld& W, Beast& b, int d) {
    b.hidden = true;
    b.den = d;
    b.pos = {W.dens[d].pos.x, W.dens[d].pos.y + 6};
    b.vel = {0, 0};
    b.path.clear();
    W.hides++;
}
void LeaveDen(BeastWorld& W, Beast& b) {
    if (!b.hidden) return;
    b.hidden = false;
    if (b.den >= 0) b.pos = DenMouth(W, b.den);
    b.vel = {b.facing * 30, -40};
}

// ---------------------------------------------------------------- the Hull's own chain reactions
void Ink(BeastWorld& W, PlatformState& p, Beast& o) {
    if (o.cooldown > 0) return;
    o.cooldown = 6;
    o.special = 0; // camouflage lost in the spray
    W.ink.push_back({o.pos, 2.6f, 66});
    W.sounds.push_back({o.pos, 0.4f, 0.4f, -1});
    if (!p.verifying) PlatBurst(p, o.pos, 16, Color{20, 16, 30, 255}, 90, 1.2f, 5);
}
void HullHooks(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    const SpeciesDef& S = HULL[b.species];
    Diver dv = SeeDiver(p);
    Rectangle diverBox = PlatDiverBox(p);
    switch (b.species) {
    case HS_OCTOPUS: {
        bool still = Len(b.vel) < 25;
        b.special = Clamp01(b.special + (still ? 0.35f : -0.8f) * dt); // camouflage builds while it keeps still
        bool pinched = false;
        for (const auto& c : W.beasts) if (Alive(c) && c.species == HS_CRAB && Dist(c.pos, b.pos) < 26) pinched = true;
        bool bumped = dv.alive && CheckCollisionCircleRec(b.pos, S.radius, diverBox);
        if (pinched || bumped) {
            Ink(W, p, b);
            if (bumped) b.trauma |= 1u << 31;
            Remember(b, MEM_THREAT, bumped ? BEAST_DIVER : -1, 0, bumped ? dv.pos : b.pos, {0, 0}, 1.0f, W.time);
            b.act = BeastAct::Flee; b.actT = 0; b.thinkT = 0.4f;
        }
        break;
    }
    case HS_PUFFER: {
        if (b.act == BeastAct::Puffed) {
            if (b.actT > 2.6f) { b.act = BeastAct::Idle; b.actT = 0; }
            for (auto& l : W.beasts) if (Alive(l) && Has(Sp(W.biome, l.species), T_PARASITE) && l.latched == i) { l.latched = -1; l.act = BeastAct::Drift; l.actT = 0; }
            b.vel = Mul(b.vel, 0.9f);
            break;
        }
        bool spooked = InCloud(W, b.pos) || (b.fear > 0.8f && std::any_of(std::begin(b.mem), std::end(b.mem), [&](const BeastMemory& m) { return m.kind == MEM_THREAT && Dist(m.pos, b.pos) < 60 && Recall(b, m, W.time) > 0.3f; }));
        if (spooked) { b.act = BeastAct::Puffed; b.actT = 0; W.sounds.push_back({b.pos, 0.3f, 0.3f, i}); }
        break;
    }
    case HS_BRITTLE: { // a fragile mat: a hard landing or a sprint across it breaks it; it grows back
        if (b.act == BeastAct::Drift) { if (b.actT > 20) { b.act = BeastAct::Idle; b.actT = 0; } break; }
        Rectangle mat{b.pos.x - 12, b.pos.y - 6, 24, 8};
        if (dv.alive && CheckCollisionRecs(diverBox, mat) && (fabsf(dv.vel.x) > 220 || dv.vel.y > 300)) {
            b.act = BeastAct::Drift; b.actT = 0;
            p.vel.y = std::max(p.vel.y, 60.0f);
            W.sounds.push_back({b.pos, 0.35f, 0.3f, i});
            if (!p.verifying) PlatBurst(p, b.pos, 10, Color{170, 160, 150, 255}, 90, 0.5f, 2);
        }
        break;
    }
    default: break;
    }
}

// ---------------------------------------------------------------- behaviours any biome's species can have
// A parasite (hull-leech, flea swarm, cave leech) rides its host and feeds; a hard sprint or the host dying
// shakes it loose; it drifts or crawls a while, then goes after the nearest host it can find.
void ParasiteTick(BeastWorld& W, int i, float dt) {
    Beast& b = W.beasts[i];
    const BiomeDef* B = Biome(W.biome);
    if (b.latched >= 0) {
        Beast& h = W.beasts[b.latched];
        if (!Alive(h) || h.id != b.targetId || h.hidden) { b.latched = -1; b.act = BeastAct::Drift; b.actT = 0; return; }
        const SpeciesDef& HS = Sp(W.biome, h.species);
        b.pos = Add(h.pos, Vector2{-h.facing * HS.radius * 0.6f, -HS.radius * (B && B->water ? -0.5f : 0.7f)});
        b.vel = h.vel;
        b.act = BeastAct::Latched;
        h.hunger = Clamp01(h.hunger + 0.004f * dt); // it's feeding on its host
        if (Len(h.vel) > 260 && h.act != BeastAct::Strike) { b.latched = -1; b.act = BeastAct::Drift; b.actT = 0; } // shaken loose by a hard sprint
        return;
    }
    if (b.act == BeastAct::Drift && Sp(W.biome, b.species).move == MoveMode::Swim) { b.vel.y = std::min(b.vel.y + 30 * dt, 35.0f); b.vel.x = sinf(b.phase * 1.3f) * 14; }
    if (b.act != BeastAct::Follow && b.act != BeastAct::Flee && (b.act != BeastAct::Drift || b.actT > 6)) { // stunned a while, then it goes looking
        if (b.cooldown <= 0) {
            b.cooldown = 3;
            int best = -1; float bd = 220;
            for (int j = 0; j < (int)W.beasts.size(); j++) {
                const Beast& h = W.beasts[j];
                if (!Alive(h) || h.hidden || !Has(Sp(W.biome, h.species), T_HOST)) continue;
                float d = Dist(h.pos, b.pos);
                if (d < bd) { bd = d; best = j; }
            }
            if (best >= 0) { b.act = BeastAct::Follow; b.target = best; b.targetId = W.beasts[best].id; b.actT = 0; }
        }
    }
    if (b.act == BeastAct::Follow && Valid(W, b.target, b.targetId) && Dist(W.beasts[b.target].pos, b.pos) < 14 + Sp(W.biome, W.beasts[b.target].species).radius * 0.5f) b.latched = b.target;
    if (b.act == BeastAct::Follow && b.actT > 15) { b.act = BeastAct::Drift; b.actT = 0; }
}

// A trap (stinging anemone, orb web, tube worm's plume): anything it can eat that touches it, it keeps.
void TrapTick(BeastWorld& W, PlatformState& p, int i) {
    Beast& b = W.beasts[i];
    const SpeciesDef& S = Sp(W.biome, b.species);
    if (b.act == BeastAct::Eat && b.actT > 1.6f) { b.act = BeastAct::Idle; b.actT = 0; }
    if (b.act == BeastAct::Hide) { if (b.actT > 1.2f) { b.act = BeastAct::Idle; b.actT = 0; } return; } // pulled in (a tube worm startled)
    Vector2 at{b.pos.x, b.pos.y - S.radius};
    for (int j = 0; j < (int)W.beasts.size(); j++) {
        Beast& o = W.beasts[j];
        if (!Alive(o) || o.hidden || o.act == BeastAct::Puffed || Pref(W.biome, b.species, o.species) <= 0) continue;
        if (Dist(o.pos, at) < S.radius + Sp(W.biome, o.species).radius + 4) {
            Kill(W, p, j, i);
            o.meat = 0.45f; // it eats most of it; the scraps are what the scavengers come for
            b.act = BeastAct::Eat; b.actT = 0; b.hunger = 0;
        }
    }
}

// A flasher (glow jelly, glow-beetle): a fright makes it light up - which startles some, and shows it to others.
void FlashTick(BeastWorld& W, int i, float dt) {
    Beast& b = W.beasts[i];
    b.flashT -= dt;
    if (b.flashT <= 0 && b.cooldown <= 0 && b.fear > 0.45f) {
        b.flashT = 1.4f; b.cooldown = 4.0f;
        W.sounds.push_back({b.pos, 0.35f, 0.3f, i});
    }
}

// ---------------------------------------------------------------- one beast's tick
void UpdateBeast(BeastWorld& W, PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    const SpeciesDef& S = Sp(W.biome, b.species);
    b.phase += dt * (1.0f + Len(b.vel) / 60.0f);
    b.actT += dt;
    b.cooldown -= dt;
    b.shoveT -= dt;

    const BiomeDef* BD = Biome(W.biome);
    if (S.move == MoveMode::Sessile) { // anemones, webs and brittle-star mats don't decide anything: their hooks are all they do
        if (Has(S, T_TRAP)) TrapTick(W, p, i);
        if (BD && BD->hooks) BD->hooks(W, p, i, dt);
        return;
    }
    Perceive(W, p, i);
    Drives(W, i, dt);
    if (Has(S, T_PARASITE)) ParasiteTick(W, i, dt);
    if (Has(S, T_FLASH)) FlashTick(W, i, dt);
    if (BD && BD->hooks) BD->hooks(W, p, i, dt);
    if (!Alive(b)) return;
    if (b.pers.abnormal == Abnormal::RabidEnraged) { b.health -= dt * 0.015f; if (b.health <= 0) { Kill(W, p, i, -1); return; } }

    // think: re-choose every so often (sooner for sharp beasts), or at once when newly frightened
    bool leech = Has(S, T_PARASITE);
    bool locked = b.act == BeastAct::Coil || b.act == BeastAct::Strike || b.act == BeastAct::Eat || b.act == BeastAct::Puffed ||
                  b.act == BeastAct::Latched || (leech && (b.act == BeastAct::Drift || b.act == BeastAct::Follow));
    b.thinkT -= dt;
    if (!locked && (b.thinkT <= 0 || (b.fear > 0.6f && b.act != BeastAct::Flee && b.act != BeastAct::Hide))) {
        b.thinkT = 0.12f + (1 - b.pers.intelligence) * 0.3f;
        Choice c;
        bool abn = b.pers.abnormal != Abnormal::None && ChooseAbnormal(W, p, i, c);
        if (!abn) c = ChooseUtility(W, p, i);
        // schooling fish follow their school's leader unless they're scared on their own account
        if (S.social && (S.move == MoveMode::Swim || S.move == MoveMode::Fly) && b.school >= 0 && !abn && c.act != BeastAct::Flee && c.act != BeastAct::Hunt) {
            for (int j = 0; j < i; j++) {
                const Beast& l = W.beasts[j];
                if (Alive(l) && l.school == b.school && !l.hidden) {
                    if (l.act == BeastAct::Flee) { if (!b.straggler) c = {BeastAct::Flee, -1, 0, l.goal, 1}; } // a straggler misses the cue - it only bolts when it sees the danger itself, and then alone
                    else c = {BeastAct::Follow, j, l.id, l.goal, 0.5f};
                    break;
                }
            }
        }
        if (c.act != b.act || c.target != b.target) {
            if (b.hidden && c.act != BeastAct::Rest && c.act != BeastAct::Hide && c.act != BeastAct::Ambush) LeaveDen(W, b);
            if (c.act == BeastAct::Explore) c.goal = ExploreTarget(W, i);
            if (c.act == BeastAct::Wander) c.goal = WanderTarget(W, i);
            if (c.act == BeastAct::Flee && !abn) {
                Vector2 threat = b.pos; float wv = 0;
                for (const auto& m : b.mem) if (m.kind == MEM_THREAT) { float v = Recall(b, m, W.time); if (v > wv) { wv = v; threat = m.pos; } }
                c.goal = FleeTarget(W, i, threat);
            }
            b.act = c.act; b.target = c.target; b.targetId = c.targetId; b.goal = c.goal; b.actT = 0;
        } else if (c.act == BeastAct::Hunt || c.act == BeastAct::Scavenge || c.act == BeastAct::Guard || c.act == BeastAct::Follow || c.act == BeastAct::Mob) b.goal = c.goal;
    }

    Diver dv = SeeDiver(p);
    auto targetPos = [&](Vector2& tp, Vector2& tv) -> bool {
        if (b.target == BEAST_DIVER) { if (!dv.alive) return false; tp = dv.pos; tv = dv.vel; return true; }
        if (!Valid(W, b.target, b.targetId)) return false;
        const Beast& t = W.beasts[b.target];
        tp = t.pos; tv = t.vel;
        return true;
    };
    float spd = S.speed * (0.8f + 0.4f * b.pers.energy);
    float sprint = S.sprint * (0.85f + 0.3f * b.pers.energy) * (b.pers.abnormal == Abnormal::HyperProtectiveParent && b.act == BeastAct::Guard ? 2.2f : 1.0f);
    Vector2 go = b.goal;
    float speed = spd * 0.7f;

    switch (b.act) {
    case BeastAct::Idle: speed = 0; go = b.pos; break;
    case BeastAct::Wander:
        if (Dist(b.goal, b.pos) < 16 || b.actT > 8) { b.goal = WanderTarget(W, i); b.actT = 0; }
        go = b.goal; speed = fmodf(b.phase, 5.0f) < 1.2f ? 0.0f : spd * 0.6f; // pauses: grazing, looking about
        break;
    case BeastAct::Explore:
        go = b.goal; speed = spd;
        if (Dist(b.goal, b.pos) < 20) { // arrived somewhere new - a real roamer may settle here for good
            if (Hash((unsigned)b.id, (unsigned)(W.time * 3)) < b.pers.wanderlust * 0.6f) { int d = NearestDen(W, b.pos, 14 * TILE); if (d >= 0) b.den = d; b.territory = b.pos; }
            b.act = BeastAct::Wander; b.actT = 0; b.goal = WanderTarget(W, i);
        }
        break;
    case BeastAct::Follow: {
        Vector2 tp, tv;
        if (!targetPos(tp, tv)) { b.act = BeastAct::Wander; b.thinkT = 0; break; }
        if (b.school >= 0) { go = b.goal; speed = b.straggler ? spd * (0.6f + 0.3f * sinf(b.phase * 0.7f)) : spd; break; } // the straggler dawdles behind
        go = b.target == BEAST_DIVER && b.pers.abnormal == Abnormal::SymbioticCompanion ? Add(tp, Vector2{-dv.vel.x * 0.15f - 30, -30}) : tp;
        speed = Dist(go, b.pos) > 60 ? sprint : spd;
        if (b.pers.abnormal == Abnormal::ParasiticHost && Dist(tp, b.pos) < 16) { b.latched = b.target; b.act = BeastAct::Latched; b.actT = 0; }
        break;
    }
    case BeastAct::Latched: {
        if (leech) return; // a leech riding its host is placed by the Hull's own hook
        Vector2 tp, tv;
        if (!targetPos(tp, tv) || (b.target == BEAST_DIVER && dv.vel.y < -600)) { b.latched = -1; b.act = BeastAct::Drift; b.actT = 0; break; } // a hard jump shakes it off
        b.pos = Add(tp, Vector2{0, -8}); b.vel = tv;
        if (b.target == BEAST_DIVER) p.vel.x *= 0.992f; // riding the diver: a drag on every step
        else if (b.target >= 0) W.beasts[b.target].vel = Mul(W.beasts[b.target].vel, 0.985f);
        return;
    }
    case BeastAct::Drift: go = Add(b.pos, Vector2{0, 20}); speed = 20; break;
    case BeastAct::Hunt: case BeastAct::Guard: case BeastAct::Mob: {
        Vector2 tp, tv;
        bool live = targetPos(tp, tv);
        if (!live) { // lost it: go to where it was last seen, then give up
            if (Dist(b.goal, b.pos) < 20 || b.actT > 6) { b.act = BeastAct::Wander; b.thinkT = 0; }
            go = b.goal; speed = spd; break;
        }
        // is it still perceived? if not, hunt the memory (last-known position) rather than the live one
        bool fresh = false;
        for (const auto& m : b.mem) if ((m.kind == MEM_PREY || m.kind == MEM_THREAT) && m.source == b.target && m.sid == b.targetId && W.time - m.t < 0.3f) fresh = true;
        if (b.act != BeastAct::Hunt) fresh = true; // guards and mobs are right on top of it
        if (!fresh) {
            for (const auto& m : b.mem) if (m.kind == MEM_PREY && m.source == b.target && m.sid == b.targetId) { tp = m.pos; tv = m.vel; }
        }
        float d = Dist(tp, b.pos);
        // lead the target: a sharp hunter aims where it's going, not where it is
        float lead = std::clamp(d / std::max(1.0f, sprint), 0.0f, 1.0f) * b.pers.intelligence * 1.2f;
        go = Add(tp, Mul(tv, lead));
        speed = d < S.sight * 0.7f ? sprint : spd * 1.2f;
        // the big hunters telegraph: an eel coils before it strikes, a crab winds up before it charges
        bool striker = Has(S, T_STRIKER);
        float strikeR = S.reach > 0 ? S.reach : 110.0f;
        if (striker && b.act == BeastAct::Hunt && d < strikeR && b.cooldown <= 0 && W.nav.LineOfSight(b.pos, tp) &&
            (!Has(S, T_CHARGER) || fabsf(tp.y - b.pos.y) < TILE * 1.3f)) {
            b.act = BeastAct::Coil; b.actT = 0; b.goal = go;
            W.sounds.push_back({b.pos, 0.25f, 0.3f, i});
        }
        // stalkers (the clever and the patient) close in slowly while the target isn't looking
        if (b.act == BeastAct::Hunt && b.pers.intelligence > 0.6f && d > strikeR && d < S.sight * 0.8f && b.target != BEAST_DIVER) speed = spd * 0.8f;
        break;
    }
    case BeastAct::Coil: { // a still, visible tell - then the lunge
        speed = 0; go = b.pos;
        b.vel = Mul(b.vel, 0.85f);
        float coilT = (Has(S, T_CHARGER) ? 0.4f : 0.5f) - 0.15f * b.pers.aggression;
        if (b.actT > coilT) {
            Vector2 tp, tv;
            if (targetPos(tp, tv)) { float lead = b.pers.intelligence * 0.25f; b.goal = Add(tp, Mul(tv, lead)); }
            b.act = BeastAct::Strike; b.actT = 0;
            Vector2 dir = Norm(Sub(b.goal, b.pos));
            if (S.move == MoveMode::Walk) { b.vel.x = (dir.x >= 0 ? 1 : -1) * sprint * 1.2f; if (dir.y < -0.5f && b.grounded) b.vel.y = -520; else if (!Has(S, T_CHARGER) && b.grounded) b.vel.y = -260; } // a pounce leaves the ground
            else b.vel = Mul(dir, sprint * 1.25f);
        }
        break;
    }
    case BeastAct::Strike: {
        go = b.goal; speed = sprint * 1.25f;
        float dur = Has(S, T_CHARGER) ? 1.1f : 0.4f;
        if (b.actT > dur) { b.act = BeastAct::Hunt; b.actT = 0; b.cooldown = 1.3f - 0.4f * b.pers.aggression; }
        break;
    }
    case BeastAct::Ambush: { // lying in wait: the octopus camouflaged in place, the eel in its breach with only its head out
        speed = 0; go = b.pos;
        if (Has(S, T_DEN_AMBUSH) && b.den >= 0) {
            if (!b.hidden) { if (Dist(DenMouth(W, b.den), b.pos) < 16) EnterDen(W, b, b.den); else { go = DenMouth(W, b.den); speed = spd; } }
            b.special2 = 1; // head out
        }
        // anything tasty that comes close gets struck at
        for (const auto& m : b.mem) if (m.kind == MEM_PREY && Recall(b, m, W.time) > 0.2f && Dist(m.pos, b.pos) < (Has(S, T_DEN_AMBUSH) ? 110.0f : 60.0f) && PreyScore(W, b, m) > 0.05f) {
            LeaveDen(W, b); b.special2 = 0;
            b.target = m.source; b.targetId = m.sid; b.act = BeastAct::Coil; b.actT = Has(S, T_CAMO) ? 0.3f : 0.0f; b.goal = m.pos;
            break;
        }
        break;
    }
    case BeastAct::Eat: {
        speed = 0; go = b.pos;
        if (!Valid(W, b.target, b.targetId) || W.beasts[b.target].life != BeastLife::Corpse) { b.act = BeastAct::Wander; b.thinkT = 0; break; }
        Beast& c = W.beasts[b.target];
        if (Dist(c.pos, b.pos) > S.radius + Sp(W.biome, c.species).radius + 14) { go = c.pos; speed = spd; break; }
        float bite = std::min(c.meat, 0.45f * dt);
        c.meat -= bite;
        b.hunger = Clamp01(b.hunger - bite * 1.6f);
        if (b.pers.abnormal == Abnormal::GluttonousDevourer) { b.scale = std::min(2.0f, b.scale * (1 + bite * 0.06f)); b.mass *= 1 + bite * 0.1f; }
        if (!p.verifying && GetRandomValue(0, 10) == 0) PlatBurst(p, c.pos, 2, Color{140, 30, 40, 255}, 50, 0.4f, 2);
        W.scent.Emit(W.scent.blood, c.pos, 0.6f * dt);
        bool leftovers = W.beasts[b.target].species != b.species && S.scavenge < 1 && c.meat < 0.35f; // a hunter leaves the scraps
        if (c.meat <= 0.01f || b.hunger < 0.03f || leftovers) {
            if (c.meat <= 0.01f) { c.life = BeastLife::Gone; W.scavenged++; }
            b.act = BeastAct::Wander; b.thinkT = 0; b.actT = 0;
            b.fatigue = Clamp01(b.fatigue + 0.3f); // a full belly wants a rest
        }
        break;
    }
    case BeastAct::Scavenge: {
        if (b.target >= 0 && Valid(W, b.target, b.targetId) && W.beasts[b.target].life == BeastLife::Corpse) {
            Beast& c = W.beasts[b.target];
            go = c.pos; speed = spd * 1.2f;
            if (Dist(c.pos, b.pos) < S.radius + 16) {
                if (b.pers.abnormal == Abnormal::Kleptomaniac || b.pers.abnormal == Abnormal::CovetousHoarder) { b.carry = b.target; b.act = BeastAct::Flee; b.goal = b.den >= 0 ? DenMouth(W, b.den) : b.territory; }
                else {
                    if (Has(S, T_KLEPTO)) for (int j = 0; j < (int)W.beasts.size(); j++) { // the thief barges in: whoever was eating is startled off it
                        Beast& o = W.beasts[j];
                        if (j != i && Alive(o) && o.act == BeastAct::Eat && o.target == b.target) { Remember(o, MEM_THREAT, i, b.id, b.pos, b.vel, 0.9f, W.time); o.act = BeastAct::Flee; o.goal = FleeTarget(W, j, b.pos); o.actT = 0; }
                    }
                    b.act = BeastAct::Eat; b.actT = 0;
                }
            }
        } else { // following a scent, not a body: walk up the gradient
            go = b.goal; speed = spd;
            for (int j = 0; j < (int)W.beasts.size(); j++) if (W.beasts[j].life == BeastLife::Corpse && Dist(W.beasts[j].pos, b.pos) < 3 * TILE) { b.target = j; b.targetId = W.beasts[j].id; }
            if (Dist(b.goal, b.pos) < 20 || b.actT > 8) { b.act = BeastAct::Wander; b.thinkT = 0; }
        }
        break;
    }
    case BeastAct::Flee: {
        go = b.goal; speed = b.straggler ? sprint * 0.75f : sprint;
        if (b.straggler && fmodf(b.phase, 3.0f) < 0.35f) speed = 0; // freezes - doesn't understand it should stay with the others
        if (b.carry >= 0 && Valid(W, b.carry, W.beasts[b.carry].id) && W.beasts[b.carry].life == BeastLife::Corpse) W.beasts[b.carry].pos = Add(b.pos, Vector2{-b.facing * 8, 4});
        if (b.den >= 0 && Dist(DenMouth(W, b.den), go) < 4 && Dist(DenMouth(W, b.den), b.pos) < 16) {
            if (b.carry >= 0) { W.beasts[b.carry].life = BeastLife::Gone; b.carry = -1; b.hunger = 0; } // stored away
            EnterDen(W, b, b.den); b.act = BeastAct::Hide; b.actT = 0;
        } else if (Dist(go, b.pos) < 20 && b.fear < 0.3f) { b.act = BeastAct::Wander; b.thinkT = 0; }
        break;
    }
    case BeastAct::Hide: case BeastAct::Rest: {
        if (b.den < 0) { b.act = BeastAct::Wander; break; }
        if (!b.hidden) {
            go = DenMouth(W, b.den); speed = b.act == BeastAct::Hide ? sprint : spd;
            if (Dist(go, b.pos) < 16) EnterDen(W, b, b.den);
        } else { speed = 0; go = b.pos; }
        if (b.hidden && b.fear < 0.15f && b.fatigue < 0.15f && b.hunger > 0.45f) { LeaveDen(W, b); b.act = BeastAct::Wander; b.thinkT = 0; }
        break;
    }
    case BeastAct::Investigate: {
        go = b.goal; speed = spd;
        if (Dist(b.goal, b.pos) < 24) { speed = 0; if (b.actT > 2.5f) { b.act = BeastAct::Wander; b.thinkT = 0; } else b.facing = fmodf(b.actT, 1.2f) < 0.6f ? 1.0f : -1.0f; }
        if (b.actT > 10) { b.act = BeastAct::Wander; b.thinkT = 0; }
        break;
    }
    case BeastAct::Groom: {
        Vector2 tp, tv;
        if (!targetPos(tp, tv)) { b.act = BeastAct::Wander; break; }
        go = Add(tp, Vector2{W.beasts[b.target].facing * 14, -6}); speed = spd;
        if (Dist(go, b.pos) < 12) W.beasts[b.target].fatigue = Clamp01(W.beasts[b.target].fatigue - 0.05f * dt);
        if (b.actT > 10) { b.act = BeastAct::Wander; b.thinkT = 0; }
        break;
    }
    case BeastAct::Puffed: speed = 0; go = b.pos; break;
    default: break;
    }
    if (b.hidden) return;
    Motor(W, p, i, go, speed, dt);
    // moving about makes a little noise - the scurry, the scrabble of claws - that hunters listen for
    b.noiseT -= dt;
    if (b.noiseT <= 0 && S.move != MoveMode::Swim && Len(b.vel) > S.speed * 0.5f) {
        b.noiseT = 0.5f + 0.4f * Hash((unsigned)b.id, (unsigned)(W.time * 10));
        W.sounds.push_back({b.pos, std::min(0.3f, 0.08f + 0.03f * sqrtf(b.mass)) * (b.act == BeastAct::Hunt && b.pers.intelligence > 0.6f ? 0.3f : 1.0f), 0.25f, i}); // a clever stalker places its feet
    }

    // got stuck (a wall a dim beast can't route around, a ledge a walker can't hop): give up on this goal
    if (Dist(b.pos, b.lastPos) > 6) { b.lastPos = b.pos; b.stuckT = 0; }
    else if (speed > 0 && b.act != BeastAct::Drift && b.act != BeastAct::Latched) { b.stuckT += dt; if (b.stuckT > 2.0f + 2.0f * (1 - b.pers.intelligence)) { b.stuckT = 0; b.path.clear(); b.goal = WanderTarget(W, i); if (b.act != BeastAct::Hunt) b.act = BeastAct::Wander; } }

    // contact: the catch
    if (b.act == BeastAct::Hunt || b.act == BeastAct::Strike || b.act == BeastAct::Guard || b.act == BeastAct::Mob) {
        if (b.target >= 0 && Valid(W, b.target, b.targetId)) {
            Beast& t = W.beasts[b.target];
            const SpeciesDef& TS = Sp(W.biome, t.species);
            if (Alive(t) && !t.hidden && Dist(t.pos, b.pos) < (S.radius + TS.radius) * 0.9f + 4) {
                bool eats = Pref(W.biome, b.species, t.species) > 0 || b.pers.abnormal == Abnormal::GluttonousDevourer || b.pers.abnormal == Abnormal::RabidEnraged;
                if (t.act == BeastAct::Puffed || (Has(TS, T_TOXIC) && b.act != BeastAct::Mob)) { // a mouthful of spines, or of poison: spat out, and remembered
                    Hurt(W, p, i, 0.35f, t.species, false); b.act = BeastAct::Flee; b.goal = FleeTarget(W, i, t.pos); b.actT = 0;
                    if (Has(TS, T_TOXIC)) t.vel = Add(t.vel, Mul(Norm(Sub(t.pos, b.pos)), 200));
                }
                else if (!eats && b.act != BeastAct::Mob && b.act != BeastAct::Guard) { // a defensive charge: it bowls the thing over and hurts it, it doesn't eat it
                    Hurt(W, p, b.target, 0.4f, b.species, false);
                    Remember(t, MEM_THREAT, i, b.id, b.pos, b.vel, 1.0f, W.time);
                    t.vel = Add(t.vel, Vector2{(t.pos.x > b.pos.x ? 1.0f : -1.0f) * 300, -200});
                    b.act = BeastAct::Wander; b.thinkT = 0.5f; b.cooldown = 2.0f;
                }
                else if (b.act == BeastAct::Mob || (b.act == BeastAct::Guard && t.mass > b.mass * 1.5f)) { // harassment, not a kill
                    Remember(t, MEM_THREAT, i, b.id, b.pos, b.vel, 0.9f, W.time);
                    Hurt(W, p, b.target, 0.15f * dt, b.species, false);
                    if (W.biome == PL_HULL && t.species == HS_OCTOPUS) Ink(W, p, t);
                } else if (t.mass < b.mass * 1.6f || b.pers.abnormal == Abnormal::RabidEnraged) {
                    // the confusion effect: striking into a tight school, the jaws close on water more often than
                    // not and the school bursts apart - a lone fish gets no such cover
                    int crowd = 0;
                    if (TS.social) for (const auto& k : W.beasts) if (&k != &t && Alive(k) && k.species == t.species && Dist(k.pos, t.pos) < 28) crowd++;
                    if (crowd >= 2 && R(W) > 0.2f) {
                        Remember(t, MEM_THREAT, i, b.id, b.pos, b.vel, 1.0f, W.time);
                        t.vel = Add(t.vel, Mul(Norm(Sub(t.pos, b.pos)), 260));
                        if (b.act == BeastAct::Strike) { b.act = BeastAct::Hunt; b.actT = 0; b.cooldown = 0.8f; }
                        // having whiffed into the school, a sharp hunter switches to the loneliest fish in view
                        int lone = -1; float bestIso = 1e9f;
                        for (int k = 0; k < (int)W.beasts.size(); k++) {
                            const Beast& q = W.beasts[k];
                            if (!Alive(q) || q.hidden || q.species != t.species || Dist(q.pos, b.pos) > 7 * TILE) continue;
                            int n = 0; for (const auto& z : W.beasts) if (&z != &q && Alive(z) && z.species == q.species && Dist(z.pos, q.pos) < 40) n++;
                            float sc = n * 50 + Dist(q.pos, b.pos) * (1.2f - b.pers.intelligence);
                            if (sc < bestIso) { bestIso = sc; lone = k; }
                        }
                        if (lone >= 0 && R(W) < 0.4f + 0.6f * b.pers.intelligence) { b.target = lone; b.targetId = W.beasts[lone].id; b.act = BeastAct::Hunt; }
                    } else {
                        int v = b.target;
                        Kill(W, p, v, i);
                        b.act = BeastAct::Eat; b.actT = 0; b.target = v; b.targetId = W.beasts[v].id;
                    }
                }
            }
        }
        // a non-lethal attacker only shoves the diver - hard enough to knock them off a ledge
        if (b.target == BEAST_DIVER && dv.alive && !S.lethal && b.shoveT <= 0 && CheckCollisionCircleRec(b.pos, S.radius * b.scale, PlatDiverBox(p))) {
            b.shoveT = 0.8f;
            Vector2 dir = Norm(Sub(dv.pos, b.pos));
            p.vel.x += (dir.x >= 0 ? 1 : -1) * 260;
            p.vel.y = std::min(p.vel.y, -200.0f);
            W.sounds.push_back({b.pos, 0.4f, 0.3f, i});
        }
    }
    // a suicidal beast that reaches its hazard, or anything panicked into one, dies on it
    int tx = (int)floorf(b.pos.x / TILE), ty = (int)floorf(b.pos.y / TILE);
    if (W.nav.Hazard(tx, ty) && (b.pers.abnormal == Abnormal::SuicidalSelfDestructive || S.move == MoveMode::Walk || S.move == MoveMode::Climb)) { W.deaths[1]++; if (getenv("DEPTH_BEASTLOG")) TraceLog(LOG_WARNING, "  hazard death: %s %s at tile (%d,%d) %c below %c waterY %.0f vel (%.0f,%.0f)", Sp(W.biome, b.species).name, BeastActName(b.act), tx, ty, PlatTileAt(p, tx, ty), PlatTileAt(p, tx, ty + 1), p.waterY, b.vel.x, b.vel.y); Kill(W, p, i, -1); return; }
    // stray shots kill what's in their way: torpedoes and cannonballs anything smallish, a musket ball anything
    // small, an explosion anything in its blast (friendly fire - the pirates don't aim around the ship's cat)
    for (const auto& s : p.shots) {
        float r = s.kind == 2 ? 40.0f : S.radius + 8;
        float maxMass = s.kind == 0 ? 3.0f : s.kind == 2 ? 15.0f : (s.kind == 4 || s.kind == 5 || s.kind == 6) ? 5.0f : 0.0f;
        if (b.mass < maxMass && Dist(s.pos, b.pos) < r) { W.deaths[3]++; Kill(W, p, i, -1); return; }
    }
    // the sea: anything that falls in off the Pirate Ship's decks drowns
    if (p.waterY > 0 && b.pos.y > p.waterY + 8 && S.move != MoveMode::Fly) { W.deaths[2]++; Kill(W, p, i, -1); }
}

void UpdateCorpse(BeastWorld& W, const PlatformState& p, int i, float dt) {
    Beast& b = W.beasts[i];
    const SpeciesDef& S = Sp(W.biome, b.species);
    b.corpseT += dt;
    b.vel.x *= 0.95f;
    const BiomeDef* B = Biome(W.biome);
    if (B && !B->water) b.vel.y = std::min(b.vel.y + WALK_G * dt, 700.0f); // in air a body drops
    else b.vel.y = std::min(b.vel.y + 60 * dt, S.move == MoveMode::Walk ? 600.0f : 40.0f); // bodies sink slowly in water
    if (p.waterY > 0 && b.pos.y > p.waterY) { b.vel = {b.vel.x * 0.9f, 10}; if (b.pos.y > p.waterY + 40) b.life = BeastLife::Gone; } // sinks away under the waves
    bool g;
    Collide(W.nav, b.pos, b.vel, HalfW(S, b), HalfH(S, b), dt, g);
    W.scent.Emit(W.scent.blood, b.pos, 8.0f * b.meat * dt * (b.corpseT < 12 ? 1.0f : 0.4f));
    if (b.corpseT > 40 || b.meat <= 0.01f) b.life = BeastLife::Gone;
}

// Dens top up the population: whatever's been eaten comes back, in time, out of a den.
void Repopulate(BeastWorld& W, PlatformState& p, float dt) {
    W.repopT -= dt;
    if (W.repopT > 0 || W.dens.empty()) return;
    W.repopT = 5.0f;
    int n = BeastSpeciesCount(W.biome);
    for (int s = 0; s < n; s++) {
        const SpeciesDef& S = Sp(W.biome, s);
        if (S.population <= 0 || S.move == MoveMode::Sessile) continue;
        int alive = 0;
        for (const auto& b : W.beasts) if (b.species == s && b.life != BeastLife::Gone) alive++;
        if (alive >= S.population) continue;
        int d = (int)(R(W) * W.dens.size()) % (int)W.dens.size();
        int k = NewBeast(W, s, DenMouth(W, d));
        Beast& b = W.beasts[k];
        b.den = d;
        EnterDen(W, b, d);
        b.act = BeastAct::Rest; b.fatigue = 0.5f; b.hunger = 0.5f;
        if (S.social) { // join the nearest school
            float bd = 1e9f;
            for (const auto& o : W.beasts) if (&o != &b && Alive(o) && o.species == s && o.school >= 0 && Dist(o.pos, b.pos) < bd) { bd = Dist(o.pos, b.pos); b.school = o.school; }
        }
        W.births++;
        (void)p;
        return; // one at a time
    }
}

// ---------------------------------------------------------------- building the Hull's population
void SpawnHull(BeastWorld& W, PlatformState& p) {
    const NavGrid& N = W.nav;
    std::vector<Vector2> floorSpots, waterSpots;
    for (int x = 14; x < N.w - 10; x++) {
        if (x * TILE > W.limitX - 2 * TILE) break;
        for (int y = 2; y < N.h - 1; y++) {
            if (!N.Standable(x, y) || N.Hazard(x, y)) continue;
            char below = PlatTileAt(p, x, y + 1);
            if (below == 'x' || below == 'g' || below == 't' || below == 'f') continue;
            floorSpots.push_back({x * TILE + 16.0f, (float)y});
            break;
        }
    }
    if (floorSpots.empty()) return;
    auto floorAt = [&](float h) { return floorSpots[std::min((int)floorSpots.size() - 1, (int)(h * floorSpots.size()))]; };
    auto swimAt = [&](Vector2 f, int band) { int y = std::max(2, (int)f.y - band); while (y < (int)f.y && !N.Open((int)(f.x / TILE), y)) y++; return Vector2{f.x, y * TILE + 16.0f}; };
    unsigned s = W.seed;
    // sprat schools
    for (int sc = 0; sc < 3; sc++) {
        Vector2 c = swimAt(floorAt(Hash(s, 100 + sc) * 0.9f + 0.05f), HULL[HS_SPRAT].band);
        for (int k = 0; k < 8; k++) {
            int b = NewBeast(W, HS_SPRAT, Add(c, Vector2{Hash(s, 200 + sc * 10 + k) * 40 - 20, Hash(s, 300 + sc * 10 + k) * 30 - 15}));
            W.beasts[b].school = sc;
        }
    }
    auto place = [&](int species, int count, int salt) {
        for (int k = 0; k < count; k++) {
            Vector2 f = floorAt(Hash(s, salt + k));
            const SpeciesDef& S = HULL[species];
            Vector2 at = S.move == MoveMode::Swim ? swimAt(f, S.band) : Vector2{f.x, (f.y + 1) * TILE - S.radius * 0.6f - 1};
            int b = NewBeast(W, species, at);
            if (S.social) W.beasts[b].school = 10 + k / 4; // hermits keep to little packs
        }
    };
    place(HS_SHRIMP, HULL[HS_SHRIMP].population, 1000);
    place(HS_OCTOPUS, HULL[HS_OCTOPUS].population, 1100);
    place(HS_PUFFER, HULL[HS_PUFFER].population, 1200);
    place(HS_ANEMONE, HULL[HS_ANEMONE].population, 1300);
    place(HS_HERMIT, HULL[HS_HERMIT].population, 1400);
    place(HS_BRITTLE, HULL[HS_BRITTLE].population, 1500);
    for (int i = 0; i < (int)W.beasts.size(); i++) { // octopuses start camouflaged
        Beast& b = W.beasts[i];
        if (b.species == HS_OCTOPUS) b.special = 1;
        if (b.species == HS_ANEMONE || b.species == HS_BRITTLE) b.pos.y += 2;
    }
    // leeches start on hosts
    std::vector<int> hosts;
    for (int i = 0; i < (int)W.beasts.size(); i++) if (W.beasts[i].species == HS_PUFFER || W.beasts[i].species == HS_OCTOPUS) hosts.push_back(i);
    for (int k = 0; k < HULL[HS_LEECH].population; k++) {
        Vector2 f = floorAt(Hash(s, 1600 + k));
        int b = NewBeast(W, HS_LEECH, swimAt(f, 2));
        if (!hosts.empty() && k < (int)hosts.size()) { W.beasts[b].latched = hosts[k]; W.beasts[b].targetId = W.beasts[hosts[k]].id; W.beasts[b].act = BeastAct::Latched; }
        else W.beasts[b].act = BeastAct::Drift;
    }
    // the generator's crabs and eels become living beasts: crabs on the deck, each eel in a breach of its own
    for (size_t e = 0; e < p.enemies.size();) {
        PlatEnemy& en = p.enemies[e];
        if (en.type == 'c') {
            NewBeast(W, HS_CRAB, {en.home.x + 16, en.home.y + TILE - HULL[HS_CRAB].radius * 0.6f - 1});
            p.enemies.erase(p.enemies.begin() + e);
        } else if (en.type == 'e') {
            int tx = (int)floorf(en.home.x / TILE), ty = (int)floorf(en.home.y / TILE);
            int fy = N.FloorBelow(tx, ty);
            Den d; d.tx = tx; d.ty = fy; d.pos = {tx * TILE + 16.0f, fy * TILE + 0.0f};
            W.dens.push_back(d);
            int b = NewBeast(W, HS_EEL, DenMouth(W, (int)W.dens.size() - 1));
            W.beasts[b].den = (int)W.dens.size() - 1;
            EnterDen(W, W.beasts[b], W.beasts[b].den);
            W.beasts[b].act = BeastAct::Ambush;
            p.enemies.erase(p.enemies.begin() + e);
        } else e++;
    }
    int eels = 0;
    for (const auto& b : W.beasts) if (b.species == HS_EEL) eels++;
    for (int k = 0; eels < HULL[HS_EEL].population && !W.dens.empty(); k++, eels++) {
        int d = (int)(Hash(s, 1700 + k) * W.dens.size()) % (int)W.dens.size();
        int b = NewBeast(W, HS_EEL, DenMouth(W, d));
        W.beasts[b].den = d;
        EnterDen(W, W.beasts[b], d);
        W.beasts[b].act = BeastAct::Ambush;
    }
    W.hides = 0;
}
bool HullLethal(const BeastWorld& W, const Beast& b) {
    (void)W;
    if (b.species == HS_PUFFER) return b.act == BeastAct::Puffed;
    return HULL[b.species].lethal;
}
bool HullTouch(const BeastWorld& W, const Beast& b, Rectangle diver) {
    (void)W;
    if (b.life != BeastLife::Alive) return false;
    if (b.species == HS_EEL && b.hidden && b.act == BeastAct::Ambush) return CheckCollisionCircleRec({b.pos.x, b.pos.y - 22}, 8, diver); // its head out of the breach bites too
    if (b.hidden) return false;
    if (b.species == HS_PUFFER && b.act == BeastAct::Puffed) return CheckCollisionCircleRec(b.pos, 16, diver);
    if (b.species == HS_EEL) // an eel's body is as dangerous as its jaws, a couple of segments back
        for (int k = 1; k <= 3; k++) if (CheckCollisionCircleRec(b.spine[k], HULL[HS_EEL].radius * 0.6f, diver)) return true;
    return false;
}
const BiomeDef& HullBiome() {
    static const BiomeDef B = [] {
        BiomeDef d;
        d.level = PL_HULL; d.species = HULL; d.count = HS_COUNT; d.web = HULL_WEB; d.webN = (int)(sizeof(HULL_WEB) / sizeof(HULL_WEB[0]));
        d.water = true; d.clarity = 0.9f; d.daylight = 0.6f; d.arenaLimit = true;
        d.spawn = SpawnHull; d.hooks = HullHooks; d.lethal = HullLethal; d.touch = HullTouch;
        return d;
    }();
    return B;
}

// ---------------------------------------------------------------- spawning helpers shared by every biome
void Spots::Build(const BeastWorld& W, const PlatformState& p, int x0, int x1) {
    const NavGrid& N = W.nav;
    floor.clear();
    for (int x = x0; x < x1; x++) {
        if (x * TILE > W.limitX - 2 * TILE) break;
        for (int y = 2; y < N.h - 1; y++) {
            if (!N.Standable(x, y) || N.Hazard(x, y)) continue;
            char below = PlatTileAt(p, x, y + 1);
            if (below == 'x' || below == 'g' || below == 't' || below == 'f') continue;
            if (p.waterY > 0 && (y + 1) * TILE > p.waterY) continue;
            floor.push_back({x * TILE + 16.0f, (float)y});
            break;
        }
    }
}
Vector2 Spots::Above(const BeastWorld& W, Vector2 f, int band) const {
    int y = std::max(2, (int)f.y - band);
    while (y < (int)f.y && !W.nav.Open((int)(f.x / TILE), y)) y++;
    return {f.x, y * TILE + 16.0f};
}
int SpawnCorpse(BeastWorld& W, int species, Vector2 at, float meat) {
    int k = NewBeast(W, species, at);
    Beast& c = W.beasts[k];
    c.life = BeastLife::Corpse; c.corpseT = 0; c.meat = meat; c.act = BeastAct::Idle; c.vel = {0, 0};
    return k;
}}  // namespace bk
using namespace bk;

// ---------------------------------------------------------------- scent grid (reference 4.1)
void ScentGrid::Init(int cw, int chh, float cellPx) {
    w = cw; h = chh; cell = cellPx;
    blood.assign(w * h, 0);
    trail.assign(w * h, 0);
    tmp.assign(w * h, 0);
}
void ScentGrid::Emit(std::vector<float>& ch, Vector2 at, float amount) {
    int x = (int)floorf(at.x / cell), y = (int)floorf(at.y / cell);
    if (x < 0 || y < 0 || x >= w || y >= h) return;
    ch[y * w + x] += amount;
}
void ScentGrid::Step(float dt, const std::vector<uint8_t>& solidCells) {
    if (w <= 2 || h <= 2) return;
    for (std::vector<float>* chp : {&blood, &trail}) {
        std::vector<float>& c = *chp;
        for (int y = 1; y < h - 1; y++)
            for (int x = 1; x < w - 1; x++) {
                int k = y * w + x;
                if (solidCells[k]) { tmp[k] = 0; continue; }
                int sx = std::clamp(x - (int)roundf(drift.x * dt / cell), 1, w - 2), sy = std::clamp(y - (int)roundf(drift.y * dt / cell), 1, h - 2);
                float src = c[sy * w + sx];
                float lap = 0; int n = 0;
                const int nb[4] = {k - 1, k + 1, k - w, k + w};
                for (int q : nb) if (!solidCells[q]) { lap += c[q]; n++; }
                lap -= n * c[k];
                tmp[k] = std::max(0.0f, src + (diffusion * lap * 0.25f - decay * src) * dt);
            }
        c.swap(tmp);
    }
}
float ScentGrid::Sample(const std::vector<float>& ch, Vector2 at) const {
    int x = (int)floorf(at.x / cell), y = (int)floorf(at.y / cell);
    if (x < 0 || y < 0 || x >= w || y >= h || ch.empty()) return 0;
    return ch[y * w + x];
}
Vector2 ScentGrid::Gradient(const std::vector<float>& ch, Vector2 at) const {
    int x = std::clamp((int)floorf(at.x / cell), 1, w - 2), y = std::clamp((int)floorf(at.y / cell), 1, h - 2);
    if (ch.empty()) return {0, 0};
    return {(ch[y * w + x + 1] - ch[y * w + x - 1]) * 0.5f, (ch[(y + 1) * w + x] - ch[(y - 1) * w + x]) * 0.5f};
}

// ---------------------------------------------------------------- navigation
bool NavGrid::LineOfSight(Vector2 a, Vector2 b) const {
    float dx = b.x - a.x, dy = b.y - a.y;
    int x = (int)floorf(a.x / tile), y = (int)floorf(a.y / tile);
    int ex = (int)floorf(b.x / tile), ey = (int)floorf(b.y / tile);
    int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
    float tdx = dx != 0 ? fabsf(tile / dx) : 1e30f, tdy = dy != 0 ? fabsf(tile / dy) : 1e30f;
    float tmx = dx != 0 ? (sx > 0 ? (x + 1) * tile - a.x : a.x - x * tile) / fabsf(dx) : 1e30f;
    float tmy = dy != 0 ? (sy > 0 ? (y + 1) * tile - a.y : a.y - y * tile) / fabsf(dy) : 1e30f;
    for (int guard = 0; guard < 400; guard++) {
        if (x == ex && y == ey) return true;
        if (tmx < tmy) { tmx += tdx; x += sx; } else { tmy += tdy; y += sy; }
        if (x == ex && y == ey) return true;
        if (Solid(x, y)) return false;
    }
    return true;
}
int NavGrid::FloorBelow(int x, int y) const {
    if (x < 0 || x >= w) return h;
    for (int yy = std::max(0, y); yy < h; yy++) if (Solid(x, yy)) return yy;
    return h;
}
bool NavGrid::FindPath(MoveMode m, Vector2 from, Vector2 to, std::vector<Vector2>& out, int maxExpand) const {
    out.clear();
    if (w <= 0 || h <= 0 || m == MoveMode::Sessile) return false;
    bool walk = m == MoveMode::Walk, climb = m == MoveMode::Climb;
    auto surface = [&](int x, int y) { for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if ((dx || dy) && Solid(x + dx, y + dy)) return true; return false; };
    auto valid = [&](int x, int y) { return walk ? Standable(x, y) : climb ? Open(x, y) && surface(x, y) : Open(x, y); };
    auto snap = [&](Vector2 v, int& x, int& y) -> bool {
        x = std::clamp((int)floorf(v.x / tile), 0, w - 1);
        y = std::clamp((int)floorf(v.y / tile), 0, h - 1);
        if (valid(x, y)) return true;
        for (int r = 1; r <= 4; r++)
            for (int dy = -r; dy <= (walk ? r + 6 : r); dy++)
                for (int dx = -r; dx <= r; dx++)
                    if (valid(x + dx, y + dy)) { x += dx; y += dy; return true; }
        return false;
    };
    int sx, sy, gx, gy;
    if (!snap(from, sx, sy) || !snap(to, gx, gy)) return false;
    if (sx == gx && sy == gy) { out.push_back({gx * tile + 16, gy * tile + 16}); return true; }
    size_t n = (size_t)w * h;
    if (g.size() != n) { g.assign(n, 0); parent.assign(n, -1); stamp.assign(n, 0); curStamp = 0; }
    if (++curStamp == 0) { std::fill(stamp.begin(), stamp.end(), 0); curStamp = 1; }
    struct Node { float f, g; int k; bool operator<(const Node& o) const { return f > o.f; } };
    std::priority_queue<Node> open;
    auto H = [&](int x, int y) { float dx = fabsf((float)(x - gx)), dy = fabsf((float)(y - gy)); return walk ? dx + 0.6f * dy : std::max(dx, dy) + 0.41f * std::min(dx, dy); };
    int sk = sy * w + sx, gk = gy * w + gx;
    stamp[sk] = curStamp; g[sk] = 0; parent[sk] = -1;
    open.push({H(sx, sy), 0, sk});
    int expanded = 0, bestK = sk; float bestH = H(sx, sy);
    auto push = [&](int from_k, int x, int y, float cost) {
        int k = y * w + x;
        if ((walk || climb) && (Hazard(x, y) || Hazard(x, y + 1))) return; // walkers never plan a route through spikes or the sea
        float ng = g[from_k] + cost + (Hazard(x, y) ? 8.0f : 0.0f);
        if (stamp[k] == curStamp && g[k] <= ng) return;
        stamp[k] = curStamp; g[k] = ng; parent[k] = from_k;
        open.push({ng + H(x, y), ng, k});
    };
    while (!open.empty() && expanded < maxExpand) {
        Node cur = open.top(); open.pop();
        if (cur.g > g[cur.k] + 1e-4f) continue;
        expanded++;
        int x = cur.k % w, y = cur.k / w;
        float hh = H(x, y);
        if (hh < bestH) { bestH = hh; bestK = cur.k; }
        if (cur.k == gk) { bestK = gk; break; }
        if (!walk) {
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    if (!dx && !dy) continue;
                    int nx = x + dx, ny = y + dy;
                    if (!Open(nx, ny) || (climb && !surface(nx, ny))) continue;
                    if (dx && dy && (!Open(x + dx, y) || !Open(x, y + dy))) continue;
                    push(cur.k, nx, ny, dx && dy ? 1.414f : 1.0f);
                }
        } else {
            for (int s = -1; s <= 1; s += 2) {
                if (Standable(x + s, y)) push(cur.k, x + s, y, 1.0f);
                else if (Standable(x + s, y - 1) && Open(x, y - 1)) push(cur.k, x + s, y - 1, 1.6f);   // step up
                if (Open(x + s, y) && !Solid(x + s, y + 1))                                             // walk off an edge
                    for (int k = 1; k <= 8; k++) {
                        if (!Open(x + s, y + k)) break;
                        if (Standable(x + s, y + k)) { push(cur.k, x + s, y + k, 1.0f + 0.25f * k); break; }
                    }
                if (Open(x, y - 1))                                                                      // hop a gap
                    for (int d = 2; d <= 3; d++)
                        for (int dy = -1; dy <= 2; dy++) {
                            int tx = x + s * d, ty = y + dy;
                            if (!Standable(tx, ty)) continue;
                            bool clear = true;
                            for (int k = 1; k < d && clear; k++) clear = Open(x + s * k, y - 1) && Open(x + s * k, y);
                            if (dy < 0 && !Open(tx, ty - 1)) clear = false;
                            if (clear) push(cur.k, tx, ty, 1.2f * d + 1.0f);
                        }
            }
        }
    }
    // reconstruct (to the goal, or the closest point reached if it's out of reach)
    std::vector<Vector2> rev;
    for (int k = bestK; k != -1; k = parent[k]) { rev.push_back({(k % w) * tile + 16, (k / w) * tile + 16}); if (k == sk) break; }
    out.assign(rev.rbegin(), rev.rend());
    if (!out.empty()) out.erase(out.begin()); // drop the start cell
    return bestK == gk;
}

// ---------------------------------------------------------------- public API
const char* AbnormalName(Abnormal a) {
    static const char* n[] = {"none", "Rabid/Enraged", "Kleptomaniac", "Hyper-Protective Parent", "Suicidal/Self-Destructive", "Parasitic Host",
                              "Covetous Hoarder", "Symbiotic Companion", "Territorial Tyrant", "Pyromaniac/Shock-Seeker", "Gluttonous Devourer",
                              "Phobic/Nyctophobic", "Malignant Alpha"};
    return n[std::min((int)a, (int)Abnormal::COUNT - 1)];
}
const char* BeastActName(BeastAct a) {
    static const char* n[] = {"idle", "wander", "explore", "hunt", "stalk", "coil", "strike", "eat", "scavenge", "flee", "hide", "rest",
                              "investigate", "ambush", "mob", "groom", "guard", "follow", "latched", "drift", "puffed"};
    return n[(int)a];
}
const SpeciesDef& BeastSpecies(int biome, int species) { return Sp(biome, species); }
int BeastSpeciesCount(int biome) { const BiomeDef* B = Biome(biome); return B ? B->count : 0; }
bool BeastsUsed(int level) { return Biome(level) != nullptr; }

void BeastsBuild(PlatformState& p, unsigned seed) {
    BeastWorld& W = p.fauna;
    W = BeastWorld{};
    const BiomeDef* B = Biome(p.level);
    if (!B || p.verifying) return;
    W.biome = p.level;
    W.active = true;
    W.seed = seed * 2654435761u + 977u;
    W.rng = W.seed | 1u;
    NavGrid& N = W.nav;
    N.w = p.w; N.h = p.h; N.tile = TILE;
    N.solid.assign((size_t)N.w * N.h, 0);
    N.hazard.assign((size_t)N.w * N.h, 0);
    for (int y = 0; y < N.h; y++)
        for (int x = 0; x < N.w; x++) {
            N.solid[y * N.w + x] = PlatSolid(p, x, y) ? 1 : 0;
            char c = PlatTileAt(p, x, y);
            N.hazard[y * N.w + x] = (c == 'x' || c == 'g' || (p.waterY > 0 && (y + 1) * TILE > p.waterY + TILE)) ? 1 : 0; // spikes, mines, and the sea under the Pirate Ship
            if (c == 'o') W.lamps.push_back({x * TILE + 16.0f, y * TILE + 16.0f});
        }
    W.scent.Init(N.w, N.h, TILE);
    if (B->arenaLimit && !p.partX.empty()) W.limitX = p.partX.back() * TILE; // stay out of the boss's arena
    for (int y = 0; y < N.h; y++)
        for (int x = 0; x < N.w; x++)
            if (PlatTileAt(p, x, y) == 'D' && x * TILE < W.limitX) { Den d; d.tx = x; d.ty = y; d.pos = {x * TILE + 16.0f, y * TILE + 0.0f}; W.dens.push_back(d); }
    if (B->spawn) B->spawn(W, p);
    W.hides = 0;
}

void BeastsNoise(PlatformState& p, Vector2 at, float intensity) {
    if (!p.fauna.active) return;
    p.fauna.sounds.push_back({at, intensity, 0.35f, -1});
}

void BeastsDiverRespawned(PlatformState& p, Vector2 at) {
    BeastWorld& W = p.fauna;
    if (!W.active) return;
    for (int i = 0; i < (int)W.beasts.size(); i++) {
        Beast& b = W.beasts[i];
        if (!Alive(b) || b.hidden || !Sp(W.biome, b.species).lethal || Dist(b.pos, at) > 8 * TILE) continue;
        for (auto& m : b.mem) if (m.source == BEAST_DIVER) m.strength = 0; // loses the trail
        b.act = BeastAct::Flee; b.actT = 0; b.thinkT = 1.5f; b.cooldown = 2.5f; b.target = -1;
        b.goal = FleeTarget(W, i, at);
    }
}

// ---------------------------------------------------------------- the gait (procedural animation, see ik.h)
// A walker's feet stay planted where they landed while the body moves over them; when a foot falls too far
// behind where it ought to be, it lifts and steps ahead of the body - diagonal pairs taking turns, so a trot
// reads as a trot at any speed, and a beast on a slope or a step puts its feet on the real ground.
Vector2 BeastHip(const Beast& b, const SpeciesDef& S, int leg) {
    float r = S.radius * b.scale;
    float x = leg < 2 ? r * 0.55f : -r * 0.5f;
    float depth = (leg % 2 == 0) ? 1.5f : -1.5f;
    return {b.pos.x + b.facing * x + depth, b.pos.y + r * 0.15f};
}
void UpdateGait(const BeastWorld& W, Beast& b, const SpeciesDef& S, float dt) {
    float r = S.radius * b.scale, legLen = HalfH(S, b) + r * 0.2f;
    float speed = fabsf(b.vel.x), stride = r * 0.6f + speed * 0.07f;
    float dur = std::clamp(0.3f - speed / 900.0f, 0.09f, 0.3f);
    for (int k = 0; k < LEGS; k++) {
        Leg& L = b.legs[k];
        Vector2 hip = BeastHip(b, S, k);
        // the ground under this hip: the top of the first solid tile within reach
        int tx = (int)floorf(hip.x / TILE), ty = (int)floorf(hip.y / TILE);
        float ground = -1;
        for (int y = ty; y <= ty + 1 + (int)(legLen / TILE); y++) if (W.nav.Solid(tx, y)) { ground = y * TILE; break; }
        bool reach = ground >= 0 && ground - hip.y <= legLen * 1.6f;
        Vector2 rest = reach ? Vector2{hip.x + b.vel.x * 0.08f, ground} : Vector2{hip.x - b.vel.x * 0.03f, hip.y + legLen * 0.75f};
        if (!L.init) { L.foot = L.from = L.to = rest; L.t = 1; L.init = true; }
        if (!reach || !b.grounded) { L.foot = Add(L.foot, Mul(Sub(rest, L.foot), std::min(1.0f, dt * 14))); L.t = 1; continue; } // tucked up in a leap
        if (L.t < 1) {
            L.t = std::min(1.0f, L.t + dt / dur);
            float e = L.t * L.t * (3 - 2 * L.t);
            L.foot = Add(L.from, Mul(Sub(L.to, L.from), e));
            L.foot.y -= sinf(L.t * PI) * std::min(r * 0.5f, 3 + speed * 0.02f); // the lift
            continue;
        }
        const Leg& partner = b.legs[k ^ 1]; // the other leg of the same end must be down before this one goes
        const Leg& diag = b.legs[3 - k];     // and its diagonal partner steps with it
        if (partner.t < 1 && diag.t >= 1 && partner.t < 0.6f) continue;
        if (Dist(L.foot, rest) > stride || fabsf(L.foot.y - ground) > 2) {
            L.from = L.foot;
            L.to = {rest.x + b.vel.x * dur * 0.7f, ground};
            L.t = 0;
        } else L.foot.y = ground;
    }
}

void BeastsUpdate(PlatformState& p, float dt) {    BeastWorld& W = p.fauna;
    if (!W.active) return;
    const BiomeDef* B = Biome(W.biome);
    dt = std::min(dt, 0.05f);
    W.time += dt;
    W.replansLeft = 8;
    for (auto& s : W.sounds) s.life -= dt;
    W.sounds.erase(std::remove_if(W.sounds.begin(), W.sounds.end(), [](const SoundEvent& s) { return s.life <= 0; }), W.sounds.end());
    for (auto& k : W.ink) k.life -= dt;
    W.ink.erase(std::remove_if(W.ink.begin(), W.ink.end(), [](const InkPuff& k) { return k.life <= 0; }), W.ink.end());
    // this tick's lights: whatever is flashing
    W.lights.clear();
    for (const auto& b : W.beasts) if (Alive(b) && b.flashT > 0) W.lights.push_back({b.pos, 5 * TILE, 1.0f});
    Diver dv = SeeDiver(p);
    bool deaf = B && B->ignoreDiver;
    W.scentT += dt;
    if (W.scentT >= 0.05f) {
        if (dv.alive && !deaf) W.scent.Emit(W.scent.trail, dv.pos, 1.2f);
        W.scent.Step(W.scentT, W.nav.solid);
        W.scentT = 0;
    }
    // the diver's footfalls: running is quieter than landing, but it carries
    W.noiseT -= dt;
    if (dv.alive && !deaf && p.onGround && fabsf(p.vel.x) > 200 && W.noiseT <= 0) { W.noiseT = 0.35f; W.sounds.push_back({dv.pos, 0.18f, 0.3f, -1}); }
    if (B && B->tick) B->tick(W, p, dt);
    for (int i = 0; i < (int)W.beasts.size(); i++) {
        Beast& b = W.beasts[i];
        if (b.life == BeastLife::Gone) continue;
        if (b.life == BeastLife::Corpse) { UpdateCorpse(W, p, i, dt); continue; }
        UpdateBeast(W, p, i, dt);
        // segmented bodies trail behind the head
        const SpeciesDef& S = Sp(W.biome, b.species);
        float seg = S.radius * 0.75f * b.scale;
        b.spine[0] = b.pos;
        for (int k = 1; k < SPINE; k++) {
            Vector2 d = Sub(b.spine[k], b.spine[k - 1]);
            float l = Len(d);
            if (l > seg) b.spine[k] = Add(b.spine[k - 1], Mul(d, seg / l));
        }
        if (S.move == MoveMode::Walk && !b.hidden && b.life == BeastLife::Alive) UpdateGait(W, b, S, dt);
    }
    Repopulate(W, p, dt);
}

bool BeastLethalNow(const PlatformState& p, const Beast& b) {
    const BeastWorld& W = p.fauna;
    if (b.life != BeastLife::Alive || b.hidden) return false;
    const BiomeDef* B = Biome(W.biome);
    if (B && B->lethal) return B->lethal(W, b);
    return Sp(W.biome, b.species).lethal;
}
bool BeastsTouchDiver(const PlatformState& p, Rectangle diver) {
    const BeastWorld& W = p.fauna;
    if (!W.active) return false;
    const BiomeDef* B = Biome(W.biome);
    for (const auto& b : W.beasts) {
        if (B && B->touch && B->touch(W, b, diver)) return true;
        if (!BeastLethalNow(p, b)) continue;
        const SpeciesDef& S = Sp(W.biome, b.species);
        if (CheckCollisionCircleRec(b.pos, S.radius * b.scale * 0.85f, diver)) return true;
    }
    return false;
}
// ---------------------------------------------------------------- depth.exe --verify-beasts
bool VerifyBeasts() {
    bool ok = true;
    auto fail = [&](const char* what) { TraceLog(LOG_WARNING, "verify-beasts: FAILED - %s", what); ok = false; };
    // a synthetic arena: an open water box over a floor, with walls; the diver parked far away
    auto arena = [](PlatformState& p, int w, int h) {
        p = PlatformState{};
        p.level = PL_HULL;
        p.w = w; p.h = h;
        p.tiles.assign(h, std::string(w, '.'));
        for (int x = 0; x < w; x++) { p.tiles[h - 1][x] = '#'; p.tiles[h - 2][x] = '#'; p.tiles[0][x] = '#'; }
        for (int y = 0; y < h; y++) { p.tiles[y][0] = '#'; p.tiles[y][w - 1] = '#'; }
        p.pos = {-5000, -5000};
        p.deathTimer = 1; // the diver is out of the picture
    };
    auto build = [](PlatformState& p) {
        BeastWorld& W = p.fauna;
        W = BeastWorld{};
        W.biome = PL_HULL; W.active = true; W.seed = 12345; W.rng = 99991;
        W.nav.w = p.w; W.nav.h = p.h;
        W.nav.solid.assign((size_t)p.w * p.h, 0); W.nav.hazard.assign((size_t)p.w * p.h, 0);
        for (int y = 0; y < p.h; y++) for (int x = 0; x < p.w; x++) W.nav.solid[y * p.w + x] = PlatSolid(p, x, y) ? 1 : 0;
        W.scent.Init(p.w, p.h, TILE);
        for (int y = 0; y < p.h; y++) for (int x = 0; x < p.w; x++) if (p.tiles[y][x] == 'D') { Den d; d.tx = x; d.ty = y; d.pos = {x * TILE + 16.0f, y * TILE + 0.0f}; W.dens.push_back(d); }
    };
    auto sane = [&](const PlatformState& p) {
        for (const auto& b : p.fauna.beasts) {
            if (b.life == BeastLife::Gone) continue;
            if (std::isnan(b.pos.x) || std::isnan(b.pos.y) || b.pos.x < -50 || b.pos.y < -50 || b.pos.x > p.w * TILE + 50 || b.pos.y > p.h * TILE + 50) return false;
        }
        return true;
    };
    // 1) navigation: a walker hops a gap and climbs a step; a swimmer routes around a wall
    {
        PlatformState p; arena(p, 30, 14);
        for (int x = 10; x <= 11; x++) { p.tiles[12][x] = '.'; p.tiles[13][x] = '.'; } // a pit
        p.tiles[11][20] = '#';                                                        // a step
        for (int y = 3; y <= 11; y++) p.tiles[y][15] = '#';                            // a wall with a gap at the top
        build(p);
        std::vector<Vector2> path;
        bool walk = p.fauna.nav.FindPath(MoveMode::Walk, {3 * TILE + 16, 11 * TILE + 16}, {8 * TILE + 16, 11 * TILE + 16}, path, 4000);
        if (!walk) fail("a walker couldn't route along plain floor");
        bool hop = p.fauna.nav.FindPath(MoveMode::Walk, {6 * TILE + 16, 11 * TILE + 16}, {13 * TILE + 16, 11 * TILE + 16}, path, 4000);
        if (!hop) fail("a walker couldn't hop a two-tile pit");
        bool swim = p.fauna.nav.FindPath(MoveMode::Swim, {12 * TILE + 16, 8 * TILE + 16}, {18 * TILE + 16, 8 * TILE + 16}, path, 4000);
        if (!swim) fail("a swimmer couldn't route over a wall");
        for (const auto& q : path) if (p.fauna.nav.Solid((int)(q.x / TILE), (int)(q.y / TILE))) { fail("a swimmer's route went through rock"); break; }
        if (!p.fauna.nav.LineOfSight({12 * TILE + 16, 8 * TILE + 16}, {13 * TILE + 16, 8 * TILE + 16}) ||
            p.fauna.nav.LineOfSight({12 * TILE + 16, 8 * TILE + 16}, {18 * TILE + 16, 8 * TILE + 16})) fail("line of sight doesn't respect walls");
    }
    // 2) a moray eel hunts down a sprat, kills it and eats it; a hermit crab smells the leftovers and finishes them
    {
        PlatformState p; arena(p, 40, 16);
        build(p);
        BeastWorld& W = p.fauna;
        int eel = NewBeast(W, HS_EEL, {10 * TILE, 8 * TILE});
        W.beasts[eel].pers.aggression = 0.9f; W.beasts[eel].pers.intelligence = 0.8f; W.beasts[eel].hunger = 0.9f; W.beasts[eel].pers.abnormal = Abnormal::None;
        int sprat = NewBeast(W, HS_SPRAT, {17 * TILE, 8 * TILE});
        W.beasts[sprat].pers.abnormal = Abnormal::None; W.beasts[sprat].straggler = false;
        int hermit = NewBeast(W, HS_HERMIT, {30 * TILE, 13 * TILE + 10});
        W.beasts[hermit].pers.abnormal = Abnormal::None; W.beasts[hermit].hunger = 0.9f;
        bool killed = false, ate = false, scav = false;
        for (int f = 0; f < 60 * 40 && !(killed && scav); f++) {
            BeastsUpdate(p, 1 / 60.0f);
            if (W.beasts[sprat].life != BeastLife::Alive) killed = true;
            if (W.beasts[eel].act == BeastAct::Eat) ate = true;
            if (W.beasts[hermit].act == BeastAct::Eat || (killed && W.beasts[sprat].life == BeastLife::Gone)) scav = true;
            if (getenv("DEPTH_BEASTLOG") && f % 120 == 0) { const Beast& h = W.beasts[hermit]; const Beast& s = W.beasts[sprat];
                TraceLog(LOG_WARNING, "  hunt t=%.0f hermit %s (%.0f,%.0f) hunger %.2f | sprat life %d (%.0f,%.0f) meat %.2f | eel %s", f / 60.0f, BeastActName(h.act), h.pos.x, h.pos.y, h.hunger, (int)s.life, s.pos.x, s.pos.y, s.meat, BeastActName(W.beasts[eel].act)); }
        }
        if (!killed) fail("a hungry, aggressive eel never caught a sprat in open water");
        if (!ate) fail("the eel never ate its kill");
        if (!scav) fail("the kill was never finished off (by the hermit crab or the eel)");
        if (!sane(p)) fail("positions blew up in the hunt test");
        TraceLog(LOG_WARNING, "verify-beasts: hunt test - kills %d, bodies cleared %d", W.kills, W.scavenged);
    }
    // 3) a cleaner shrimp flees an eel into a den and hides there
    {
        PlatformState p; arena(p, 40, 16);
        p.tiles[14][20] = 'D';
        build(p);
        BeastWorld& W = p.fauna;
        int shrimp = NewBeast(W, HS_SHRIMP, {19 * TILE, 12 * TILE});
        W.beasts[shrimp].pers.abnormal = Abnormal::None; W.beasts[shrimp].pers.bravery = 0.1f; W.beasts[shrimp].pers.intelligence = 0.8f;
        int oct = NewBeast(W, HS_OCTOPUS, {15 * TILE, 12 * TILE});
        W.beasts[oct].pers.abnormal = Abnormal::None; W.beasts[oct].hunger = 1; W.beasts[oct].pers.aggression = 1; W.beasts[oct].special = 0; W.beasts[oct].facing = 1;
        bool hid = false;
        for (int f = 0; f < 60 * 15 && !hid; f++) { BeastsUpdate(p, 1 / 60.0f); if (W.beasts[shrimp].hidden) hid = true; }
        if (!hid) fail("a frightened shrimp with a den nearby never hid in it");
    }
    // 4) a school: the eel picks off the straggler more often than any one schooled fish
    {
        int stragglerFirst = 0, trials = 12;
        for (int t = 0; t < trials; t++) {
            PlatformState p; arena(p, 50, 18);
            build(p);
            BeastWorld& W = p.fauna;
            W.seed = 777 + t * 31; W.rng = W.seed | 1;
            int strag = -1;
            for (int k = 0; k < 8; k++) {
                int s = NewBeast(W, HS_SPRAT, {30 * TILE + (k % 4) * 12.0f, 8 * TILE + (k / 4) * 12.0f});
                W.beasts[s].school = 0; W.beasts[s].straggler = k == 5; W.beasts[s].pers.abnormal = Abnormal::None;
                if (k == 5) { strag = s; W.beasts[s].pos.x += 30; W.beasts[s].pers.bravery = 0.05f; W.beasts[s].pers.intelligence = 0.1f; }
            }
            int eel = NewBeast(W, HS_EEL, {18 * TILE, 8 * TILE});
            W.beasts[eel].pers.abnormal = Abnormal::None; W.beasts[eel].hunger = 1; W.beasts[eel].pers.aggression = 0.9f; W.beasts[eel].pers.intelligence = 0.9f;
            int dead = -1, f = 0;
            for (; f < 60 * 25 && dead < 0; f++) {
                BeastsUpdate(p, 1 / 60.0f);
                for (int i = 0; i < (int)W.beasts.size(); i++) if (W.beasts[i].species == HS_SPRAT && W.beasts[i].life != BeastLife::Alive) { dead = i; break; }
            }
            if (dead == strag) stragglerFirst++;
            if (getenv("DEPTH_BEASTLOG")) TraceLog(LOG_WARNING, "  school trial %d: first kill %s after %.1fs", t, dead < 0 ? "none" : dead == strag ? "the straggler" : "a schooled fish", f / 60.0f);
        }
        TraceLog(LOG_WARNING, "verify-beasts: straggler taken first in %d of %d school attacks (1 in 8 fish)", stragglerFirst, trials);
        if (stragglerFirst * 8 < trials * 2) fail("predators don't single out stragglers any more often than chance");
    }
    // 5) sight needs a clear line: an eel behind a wall doesn't notice the diver, one in the open does
    {
        PlatformState p; arena(p, 40, 16);
        for (int y = 2; y <= 13; y++) p.tiles[y][20] = '#';
        build(p);
        p.deathTimer = 0; p.pos = {24 * TILE, 12 * TILE};
        BeastWorld& W = p.fauna;
        int blind = NewBeast(W, HS_EEL, {17 * TILE, 12 * TILE}); W.beasts[blind].facing = 1; W.beasts[blind].pers.abnormal = Abnormal::None;
        int open = NewBeast(W, HS_EEL, {28 * TILE, 12 * TILE}); W.beasts[open].facing = -1; W.beasts[open].pers.abnormal = Abnormal::None;
        W.time = 1;
        Perceive(W, p, blind); Perceive(W, p, open);
        auto sawDiver = [&](int i) { for (const auto& m : W.beasts[i].mem) if (m.source == BEAST_DIVER && m.strength > 0) return true; return false; };
        if (sawDiver(blind)) fail("an eel saw the diver through solid rock");
        if (!sawDiver(open)) fail("an eel facing the diver in open water didn't see them");
    }
    // 6) a roamer goes far; a homebody stays near its den
    {
        PlatformState p; arena(p, 120, 14);
        build(p);
        BeastWorld& W = p.fauna;
        int roam = NewBeast(W, HS_HERMIT, {60 * TILE, 11 * TILE + 20}); W.beasts[roam].pers = {0.2f, 0.5f, 0.8f, 0.2f, 0.8f, 1.0f, Abnormal::None}; W.beasts[roam].hunger = 0;
        int home = NewBeast(W, HS_HERMIT, {60 * TILE, 11 * TILE + 20}); W.beasts[home].pers = {0.2f, 0.5f, 0.4f, 0.2f, 0.8f, 0.0f, Abnormal::None}; W.beasts[home].hunger = 0;
        float farR = 0, farH = 0;
        for (int f = 0; f < 60 * 60; f++) {
            BeastsUpdate(p, 1 / 60.0f);
            W.beasts[roam].hunger = W.beasts[home].hunger = 0;
            farR = std::max(farR, fabsf(W.beasts[roam].pos.x - 60 * TILE));
            farH = std::max(farH, fabsf(W.beasts[home].pos.x - 60 * TILE));
        }
        TraceLog(LOG_WARNING, "verify-beasts: in a minute the roamer ranged %.0f tiles, the homebody %.0f", farR / TILE, farH / TILE);
        if (farR < 18 * TILE) fail("a high-wanderlust beast never roamed far from home");
        if (farH > farR * 0.8f) fail("a homebody ranged as far as a roamer");
        if (!sane(p)) fail("positions blew up in the roaming test");
    }
    // 7) about 15% of beasts roll an abnormal profile, spread across all twelve
    {
        BeastWorld W; W.biome = PL_HULL; W.seed = 4242;
        int counts[(int)Abnormal::COUNT] = {0}, n = 2000;
        for (int k = 0; k < n; k++) { int i = NewBeast(W, HS_EEL, {0, 0}); counts[(int)W.beasts[i].pers.abnormal]++; W.beasts[i].life = BeastLife::Gone; }
        int abn = n - counts[0], kinds = 0;
        for (int a = 1; a < (int)Abnormal::COUNT; a++) if (counts[a] > 0) kinds++;
        TraceLog(LOG_WARNING, "verify-beasts: %d of %d abnormal (%.1f%%), %d of 12 profiles seen", abn, n, 100.0f * abn / n, kinds);
        if (abn < n * 0.10f || abn > n * 0.20f) fail("the abnormal-personality rate is outside 10-20%");
        if (kinds < 12) fail("not every abnormal profile can occur");
    }
    // 8) a real generated Hull: everything spawns, dens exist, and a minute of life stays sane
    {
        PlatformState p;
        p.level = PL_HULL; p.layout = {404, 100};
        PlatBuildLevel(p);
        p.deathTimer = 1;
        const BeastWorld& W = p.fauna;
        int kinds[HS_COUNT] = {0};
        for (const auto& b : W.beasts) if (b.life == BeastLife::Alive) kinds[b.species]++;
        int distinct = 0;
        for (int k = 0; k < HS_COUNT; k++) if (kinds[k]) distinct++;
        TraceLog(LOG_WARNING, "verify-beasts: a real Hull has %d dens, %d beasts of %d species (%d crabs, %d eels, %d abnormal)", (int)W.dens.size(), (int)W.beasts.size(), distinct, kinds[HS_CRAB], kinds[HS_EEL], W.abnormals);
        if (W.dens.size() < 4) fail("the Hull generated fewer than four dens");
        if (distinct < 8) fail("the Hull spawned fewer than eight species");
        if (kinds[HS_EEL] < 2) fail("the Hull has fewer than two eels");
        for (int f = 0; f < 60 * 60; f++) {
            BeastsUpdate(p, 1 / 60.0f);
            if (getenv("DEPTH_BEASTLOG") && f % 600 == 599) { // what everyone's doing, every ten seconds
                std::string line;
                for (int s = 0; s < HS_COUNT; s++) {
                    int acts[32] = {0}, n = 0, hid = 0;
                    for (const auto& b : W.beasts) if (b.species == s && b.life == BeastLife::Alive) { acts[(int)b.act]++; n++; hid += b.hidden; }
                    if (!n) continue;
                    line += std::string(" | ") + HULL[s].name + " " + std::to_string(n) + ":";
                    for (int a = 0; a < 32; a++) if (acts[a]) line += std::string(" ") + BeastActName((BeastAct)a) + "=" + std::to_string(acts[a]);
                    if (hid) line += " (in den " + std::to_string(hid) + ")";
                }
                int corpses = 0; for (const auto& b : W.beasts) corpses += b.life == BeastLife::Corpse;
                TraceLog(LOG_WARNING, "  t=%ds corpses=%d kills=%d cleared=%d%s", (f + 1) / 60, corpses, W.kills, W.scavenged, line.c_str());
            }
        }
        if (!sane(p)) fail("a minute of Hull life blew up a position");
        TraceLog(LOG_WARNING, "verify-beasts: a minute of Hull life - %d kills, %d bodies cleared, %d births, %d den visits", W.kills, W.scavenged, W.births, W.hides);
    }
    if (ok) TraceLog(LOG_WARNING, "verify-beasts: OK - routing, sight lines, hunting, eating, scavenging, hiding, schooling, roaming and the abnormal profiles all check out");
    return ok;
}
