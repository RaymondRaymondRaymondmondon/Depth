// ============================================================================
//  THE OPEN ABYSS - a fully 3D vertical descent biome, built on raylib's own 3D
//  pipeline (Camera3D / BeginMode3D), separate from the 2D platformer's engine.
//  This is a first vertical slice: the core dive physics (dash, hydro-glide,
//  hydro-parachute, slipstream), a procedurally jagged vertical trench, and a
//  small piece of its ecosystem chain (Glass Sponges -> Giant Isopods -> a
//  Gulper Eel, plus Bioluminescent Plankton) - not the full ten-species chain
//  from the design doc yet. Everything else in the game is untouched by this.
// ============================================================================
#include "game.h"
#include "raymath.h"
#include "rlgl.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace {

float Hash1(float x) { float s = sinf(x * 12.9898f + 1.7f) * 43758.5453f; return s - floorf(s); }
float Hash2(float x, float y) { float s = sinf(x * 12.9898f + y * 78.233f) * 43758.5453f; return s - floorf(s); }

// Three points down the shaft where it bulges into a wide horizontal cavern rather than a plain vertical
// drop - the only way through is to drift toward the bulge's angle and find the gap, not just fall
// straight down, per feedback that the descent was "an easy descent down" with no horizontal sections.
// A pure function of (depth, seed) - not stored state - so the mesh builder (which only ever sees a seed)
// and the player-collision code agree on the shape automatically, and GenerateZones below can locate the
// same bands to place a blocking ledge and drop the vertical gap in the right place.
struct CavernInfo { bool active = false; float angle = 0, t = 0; };
constexpr float CAVERN_CENTERS[3] = {220.0f, 480.0f, 730.0f};
constexpr float CAVERN_SPAN = 85.0f;
CavernInfo CavernAt(float depth, unsigned seed) {
    for (int i = 0; i < 3; i++) {
        float c = CAVERN_CENTERS[i] + (Hash2(CAVERN_CENTERS[i], seed * 11.0f + i) - 0.5f) * 30.0f;
        if (depth > c - CAVERN_SPAN * 0.5f && depth < c + CAVERN_SPAN * 0.5f) {
            float angle = Hash2(c, seed * 13.0f) * 2 * PI;
            float t = 1.0f - fabsf(depth - c) / (CAVERN_SPAN * 0.5f); // 0 at the band's edges, 1 at its centre
            return {true, angle, t};
        }
    }
    return {};
}

// The trench's cross-section radius at a given depth/angle: a base width that narrows and widens in slow
// bands down the shaft, jittered by an angular hash so the wall reads as jagged rock, not a smooth pipe.
// Widened overall per feedback that the playable area felt cramped, and it bulges hard toward a cavern's
// angle where one is present, tapering off with angular distance so it reads as a distinct room, not a
// wider pipe.
float TrenchRadius(float depth, float angle, unsigned seed) {
    float band = 20.0f + 5.0f * sinf(depth * 0.045f + seed * 0.7f); // twice as wide as it was (the user: room to dodge what comes for you)
    float jag = 0;
    for (int o = 0; o < 3; o++) {
        float freq = 3.0f + o * 5.0f;
        jag += (Hash2(cosf(angle) * freq + seed * 3.1f, depth * 0.08f + o * 11.0f) - 0.5f) * (2.6f / (o + 1));
    }
    // ledges and alcoves: every so often the wall steps in (a shelf to shelter under) or caves out (a pocket to hide in)
    float pocket = Hash2(floorf(depth / 18.0f) + seed * 0.37f, floorf(angle * 3.0f / PI));
    if (pocket > 0.86f) band += 5.0f * sinf(fmodf(depth, 18.0f) / 18.0f * PI); // an alcove
    else if (pocket < 0.10f) band -= 3.5f * sinf(fmodf(depth, 18.0f) / 18.0f * PI); // a jutting shelf
    CavernInfo cav = CavernAt(depth, seed);
    if (cav.active) {
        float da = atan2f(sinf(angle - cav.angle), cosf(angle - cav.angle));
        band += cav.t * expf(-(da * da) / (0.85f * 0.85f)) * 24.0f;
    }
    return std::max(3.2f, band + jag);
}

constexpr int RING_SEGMENTS = 40;
constexpr float RING_STEP = 3.0f;

// The trench wall is a GPU mesh built once per seed (never stored in Game/AbyssState - those get plain-
// data-copied around by debug tooling, and a raylib Model holds live GPU handles that must not be duplicated).
Model gTrenchModel{};
unsigned gTrenchSeed = 0;
bool gTrenchReady = false;

void BuildTrenchMesh(unsigned seed) {
    if (gTrenchReady) UnloadModel(gTrenchModel);
    int rings = (int)(ABYSS_DEPTH_SPAN / RING_STEP) + 1;
    Mesh mesh{};
    mesh.vertexCount = rings * RING_SEGMENTS;
    mesh.triangleCount = (rings - 1) * RING_SEGMENTS * 2;
    mesh.vertices = (float*)MemAlloc(sizeof(float) * mesh.vertexCount * 3);
    mesh.normals  = (float*)MemAlloc(sizeof(float) * mesh.vertexCount * 3);
    mesh.colors   = (unsigned char*)MemAlloc(sizeof(unsigned char) * mesh.vertexCount * 4);
    mesh.indices  = (unsigned short*)MemAlloc(sizeof(unsigned short) * mesh.triangleCount * 3);
    for (int r = 0; r < rings; r++) {
        float depth = r * RING_STEP;
        for (int s = 0; s < RING_SEGMENTS; s++) {
            float a = (float)s / RING_SEGMENTS * 2 * PI;
            float rad = TrenchRadius(depth, a, seed);
            int vi = r * RING_SEGMENTS + s;
            Vector3 p{cosf(a) * rad, -depth, sinf(a) * rad};
            mesh.vertices[vi * 3 + 0] = p.x; mesh.vertices[vi * 3 + 1] = p.y; mesh.vertices[vi * 3 + 2] = p.z;
            Vector3 n = Vector3Normalize({-cosf(a), 0, -sinf(a)}); // inward-facing normal (we're inside the shaft)
            mesh.normals[vi * 3 + 0] = n.x; mesh.normals[vi * 3 + 1] = n.y; mesh.normals[vi * 3 + 2] = n.z;
            float shade = 0.40f + 0.34f * Hash2(a * 7.0f, depth * 0.1f + seed);
            mesh.colors[vi * 4 + 0] = (unsigned char)(shade * 90);
            mesh.colors[vi * 4 + 1] = (unsigned char)(shade * 110);
            mesh.colors[vi * 4 + 2] = (unsigned char)(shade * 130);
            mesh.colors[vi * 4 + 3] = 255;
        }
    }
    int ii = 0;
    for (int r = 0; r < rings - 1; r++) {
        for (int s = 0; s < RING_SEGMENTS; s++) {
            int s2 = (s + 1) % RING_SEGMENTS;
            unsigned short a = (unsigned short)(r * RING_SEGMENTS + s), b = (unsigned short)(r * RING_SEGMENTS + s2);
            unsigned short c = (unsigned short)((r + 1) * RING_SEGMENTS + s), d = (unsigned short)((r + 1) * RING_SEGMENTS + s2);
            // wound so the visible face points inward (we view the trench from inside it)
            mesh.indices[ii++] = a; mesh.indices[ii++] = c; mesh.indices[ii++] = b;
            mesh.indices[ii++] = b; mesh.indices[ii++] = c; mesh.indices[ii++] = d;
        }
    }
    UploadMesh(&mesh, false);
    gTrenchModel = LoadModelFromMesh(mesh);
    gTrenchSeed = seed;
    gTrenchReady = true;
}

PersonalityProfile RollPersonality(unsigned seed, int idx) {
    PersonalityProfile p;
    p.aggression = Hash2((float)idx, seed * 1.0f);
    p.bravery    = Hash2((float)idx + 50, seed * 1.0f);
    p.energy     = Hash2((float)idx + 100, seed * 1.0f);
    p.curiosity  = Hash2((float)idx + 150, seed * 1.0f);
    return p;
}

Vector3 ShaftPos(float depth, float angle, float radiusOffset, unsigned seed) {
    float rad = TrenchRadius(depth, angle, seed) - radiusOffset;
    return {cosf(angle) * rad, -depth, sinf(angle) * rad};
}

// True while `depth` sits inside a zone of the given kind (used by both the physics step and the ecosystem
// populator, so a BrineSlug spawned "in the bowling lane" and the lane's own gravity/self-dislodge logic
// always agree on where the lane actually is).
bool InZone(const AbyssState& a, AbyssZoneKind kind, float depth, const AbyssZone** out = nullptr) {
    for (const auto& z : a.zones) if (z.kind == kind && depth >= z.depth && depth < z.depth + z.span) { if (out) *out = &z; return true; }
    return false;
}

// One of each set-piece, spread down the shaft in a sensible teaching order (an early updraft to learn the
// parachute/slipstream pair with, then a bowling lane, a maze that rewards using them together, a brine-pool
// breather, and a second updraft on the run to the floor) with just enough seed-jitter that a replay isn't
// identical, not enough that the pacing falls apart.
void GenerateZones(AbyssState& a) {
    a.zones.clear();
    auto jit = [&](float base, float amt, float salt) { return base + (Hash2(base, a.seed * 5.3f + salt) - 0.5f) * amt; };
    a.zones.push_back({AbyssZoneKind::Vent, jit(140, 40, 1), 70, Hash2(1, (float)a.seed) * 2 * PI, 1.0f});
    a.zones.push_back({AbyssZoneKind::BowlingLane, jit(320, 40, 2), 90, 0, 1.0f});
    a.zones.push_back({AbyssZoneKind::SiphonophoreMaze, jit(480, 30, 3), 70, 0, 1.0f});
    a.zones.push_back({AbyssZoneKind::BrinePool, jit(620, 30, 4), 90, 0, 0.75f});
    a.zones.push_back({AbyssZoneKind::Vent, jit(800, 30, 5), 60, Hash2(2, (float)a.seed) * 2 * PI, 1.15f});
    for (float c : CAVERN_CENTERS) {
        CavernInfo cav = CavernAt(c, a.seed); // the exact centre may have jittered slightly off `c`; re-resolve it
        if (!cav.active) cav = CavernAt(c + 1.0f, a.seed);
        a.zones.push_back({AbyssZoneKind::Cavern, c - CAVERN_SPAN * 0.5f, CAVERN_SPAN, cav.angle, 1.0f});
    }
}

void PopulateEcosystem(AbyssState& a) {
    GenerateZones(a);
    a.creatures.clear();
    int idx = 0;
    auto spawn = [&](AbyssCreatureKind kind, float d, float wallAngle, float radiusOffset, AbyssCreatureState st) -> AbyssCreature& {
        AbyssCreature c;
        c.kind = kind; c.personality = RollPersonality(a.seed, idx++); c.wallAngle = wallAngle; c.depth = d;
        c.radiusOffset = radiusOffset; c.pos = ShaftPos(d, wallAngle, radiusOffset, a.seed); c.home = c.pos; c.state = st;
        a.creatures.push_back(c);
        return a.creatures.back();
    };
    // Glass Sponge ledges: fragile footholds, spaced down the shaft at varying angles.
    for (float d = 40; d < ABYSS_DEPTH_SPAN - 30; d += 55 + Hash1(d) * 25)
        spawn(AbyssCreatureKind::GlassSponge, d, Hash2(d, a.seed * 2.0f) * 2 * PI, 1.4f, AbyssCreatureState::Idle);
    // Rock Ledges: plain stone shelves that never shatter, interleaved with the sponges so there's always
    // somewhere durable to wait out a predator below - per feedback that the descent needed real rest stops.
    for (float d = 65; d < ABYSS_DEPTH_SPAN - 30; d += 60 + Hash1(d + 6100) * 25)
        spawn(AbyssCreatureKind::RockLedge, d, Hash2(d + 6100, a.seed * 2.4f) * 2 * PI, 1.7f, AbyssCreatureState::Idle);
    // Each cavern gets one wide blocking ledge opposite its bulge - falling straight down the old centre
    // line lands on it, so crossing the cavern toward the bulge angle to find the gap is the only way past.
    for (const auto& z : a.zones) if (z.kind == AbyssZoneKind::Cavern) {
        AbyssCreature& led = spawn(AbyssCreatureKind::RockLedge, z.depth + z.span * 0.5f, z.angle + PI, 3.0f, AbyssCreatureState::Idle);
        led.phase = 1.0f; // marks it as a cavern floor: drawn wider/flatter than an ordinary resting ledge
    }
    // Giant Isopods: cling to the wall until a downdraft, a nearby dash, or (in the bowling lane) their own
    // clock dislodges them - the lane's whole gimmick is that they let go on a timer, not just on request.
    for (float d = 70; d < ABYSS_DEPTH_SPAN - 30; d += 70 + Hash1(d + 900) * 40)
        spawn(AbyssCreatureKind::GiantIsopod, d, Hash2(d + 500, a.seed * 3.0f) * 2 * PI, 0.6f, AbyssCreatureState::Cling);
    // A Gulper Eel lurking in the dark - it lunges toward acoustic disturbances.
    for (float d = 260; d < ABYSS_DEPTH_SPAN - 30; d += 320)
        spawn(AbyssCreatureKind::GulperEel, d, Hash2(d + 1500, a.seed * 4.0f) * 2 * PI, 3.5f, AbyssCreatureState::Idle);
    // Vampire Squid: a faster, more aggressive hunter than the Gulper Eel, only in the deeper, colder water.
    for (float d = 500; d < ABYSS_DEPTH_SPAN - 40; d += 260)
        spawn(AbyssCreatureKind::VampireSquid, d, Hash2(d + 2200, a.seed * 4.6f) * 2 * PI, 3.0f, AbyssCreatureState::Idle);
    // Trench Worms: coiled in the wall, rare, lunging at anything that lingers close.
    for (float d = 200; d < ABYSS_DEPTH_SPAN - 30; d += 240 + Hash1(d + 3000) * 60)
        spawn(AbyssCreatureKind::TrenchWorm, d, Hash2(d + 3000, a.seed * 5.6f) * 2 * PI, 1.0f, AbyssCreatureState::Idle);
    // Hatchetfish schools: harmless ambient life, a handful of fish orbiting a shared centre, fleeing a
    // disturbance for a moment before drifting back - texture for the water, not a hazard.
    for (float d = 90; d < ABYSS_DEPTH_SPAN - 40; d += 150 + Hash1(d + 4000) * 60) {
        float schoolAngle = Hash2(d + 4000, a.seed * 6.2f) * 2 * PI;
        for (int k = 0; k < 5; k++) {
            AbyssCreature& f = spawn(AbyssCreatureKind::Hatchetfish, d + (k - 2) * 1.4f, schoolAngle, 4.5f + (k % 2), AbyssCreatureState::Idle);
            f.phase = k * 1.1f + Hash1(d + k) * 6.0f;
        }
    }
    // Brine Slugs: slow, heavy, crushing - the hazard the bowling lane and the brine pool both lean on.
    for (const auto& z : a.zones) if (z.kind == AbyssZoneKind::BowlingLane || z.kind == AbyssZoneKind::BrinePool)
        for (int k = 0; k < 3; k++) spawn(AbyssCreatureKind::BrineSlug, z.depth + z.span * (k + 0.5f) / 3.0f,
                                          Hash2(z.depth + k * 7, a.seed * 7.0f) * 2 * PI, 1.0f, AbyssCreatureState::Cling);
    // Siphonophore Colonies: a maze of static stinging tendrils across one band, meant to be threaded slowly
    // (hydro-parachuting down through the gaps) rather than dashed through.
    for (const auto& z : a.zones) if (z.kind == AbyssZoneKind::SiphonophoreMaze)
        for (int k = 0; k < 6; k++) {
            float d = z.depth + (k + 0.5f) / 6.0f * z.span;
            float ang = Hash2(d + 8000, a.seed * 8.0f) * 2 * PI;
            spawn(AbyssCreatureKind::Siphonophore, d, ang, 1.5f + Hash1(d) * 2.5f, AbyssCreatureState::Idle);
        }
    // Leviathans: a colossal, mostly-background presence. Two patrol slow up/down beats along most of the
    // shaft; only rarely (personality-driven) do they deviate into a fast, dangerous close pass.
    for (int k = 0; k < 2; k++) {
        // near the shaft's centre, not the wall: a body this size is meant to all-but-fill the cross-section
        AbyssCreature& lv = spawn(AbyssCreatureKind::Leviathan, 250.0f + k * 380.0f, Hash2(k + 9000, a.seed * 9.0f) * 2 * PI, 5.5f, AbyssCreatureState::Idle);
        lv.stateTimer = Hash1((float)(k + 1) * 3.7f) * 8.0f; // desynchronise the two patrols
    }
    // ---- ParkourReference1.3's Abyss roster ----
    for (float d : {420.0f, ABYSS_DEPTH_SPAN - 30.0f}) // the Whale-Fall Scavengers on their skeletons: vast, unbothered, a roof against suction
        spawn(AbyssCreatureKind::WhaleFall, d, Hash2(d + 12000, a.seed * 1.7f) * 2 * PI, 4.0f, AbyssCreatureState::Idle);
    for (float d : {300.0f, 560.0f, 780.0f}) { // Angler-Cephalopods, each in a patch of void-moss with its lure out in front
        float ang = Hash2(d + 12100, a.seed * 2.3f) * 2 * PI;
        spawn(AbyssCreatureKind::VoidMoss, d, ang, 0.4f, AbyssCreatureState::Idle);
        AbyssCreature& q = spawn(AbyssCreatureKind::AnglerCephalopod, d, ang, 1.0f, AbyssCreatureState::Idle);
        q.home = ShaftPos(d + 1.5f, ang, 3.2f, a.seed); // where the lure dangles
    }
    for (float d : {350.0f, 650.0f}) { // Pressure-Ghosts drift in the middle of the shaft
        AbyssCreature& gh = spawn(AbyssCreatureKind::PressureGhost, d, 0, 0, AbyssCreatureState::Idle);
        gh.pos = gh.home = {0, -d, 0};
        gh.stateTimer = Hash1(d) * 6;
    }
    for (const auto& z : a.zones) if (z.kind == AbyssZoneKind::Vent) { // tube worms round the vents; krill tracing the updraft
        for (int k = 0; k < 3; k++) spawn(AbyssCreatureKind::VentWorms, z.depth + z.span * (0.2f + 0.3f * k), z.angle + (k - 1) * 0.25f, 0.3f, AbyssCreatureState::Idle);
        for (int k = 0; k < 14; k++) { AbyssCreature& kr = spawn(AbyssCreatureKind::TrenchKrill, z.depth + z.span * Hash1(z.depth + k), z.angle + (Hash1(k * 3.1f) - 0.5f) * 0.4f, 1.5f + Hash1(k * 7.7f) * 2, AbyssCreatureState::Idle); kr.phase = k * 0.7f; }
    }
    for (float d = 110; d < ABYSS_DEPTH_SPAN - 40; d += 95 + Hash1(d + 13000) * 40) { // the other flora along the walls
        float ang = Hash2(d + 13000, a.seed * 3.1f) * 2 * PI;
        int kind = (int)(Hash2(d, a.seed * 3.7f) * 4);
        AbyssCreatureKind k = kind == 0 ? AbyssCreatureKind::PressureBulb : kind == 1 ? AbyssCreatureKind::AbyssalCoral : kind == 2 ? AbyssCreatureKind::GhostKelp : AbyssCreatureKind::VoidMoss;
        spawn(k, d, ang, 0.5f, AbyssCreatureState::Idle);
        if (k == AbyssCreatureKind::AbyssalCoral) for (int j = 0; j < 4; j++) { AbyssCreature& hf = spawn(AbyssCreatureKind::SlimeHagfish, d + j * 1.2f, ang + 0.2f, 2.0f + j * 0.4f, AbyssCreatureState::Idle); hf.phase = j * 1.3f; }
    }
}

// A dash or a shattering sponge disturbs the Bioluminescent Plankton, briefly lighting the trench and
// drawing a curious Gulper Eel's attention to the disturbance's origin.
void Disturb(AbyssState& a, Vector3 at, float radius) {
    a.puffs.push_back({at, 0, 1.3f, radius});
    for (auto& c : a.creatures) {
        if ((c.kind == AbyssCreatureKind::GulperEel || c.kind == AbyssCreatureKind::VampireSquid) && c.state != AbyssCreatureState::Shattered) {
            float d = Vector3Distance(c.pos, at);
            // the squid is the more aggressive hunter: it investigates from further off and on a lower
            // curiosity roll than the eel does, matching its billing as the deeper water's bigger threat
            float range = c.kind == AbyssCreatureKind::VampireSquid ? 85.0f : 60.0f;
            float thresh = c.kind == AbyssCreatureKind::VampireSquid ? 0.22f : 0.35f;
            if (d < range && c.personality.curiosity > thresh && c.state != AbyssCreatureState::Hunting) {
                c.state = AbyssCreatureState::Hunting;
                c.vel = Vector3Scale(Vector3Normalize(Vector3Subtract(at, c.pos)), c.kind == AbyssCreatureKind::VampireSquid ? 7.0f : 6.0f);
            }
        }
        if (c.kind == AbyssCreatureKind::GiantIsopod && c.state == AbyssCreatureState::Cling) {
            float d = Vector3Distance(c.pos, at);
            if (d < radius) c.state = AbyssCreatureState::Dislodged;
        }
        if (c.kind == AbyssCreatureKind::Hatchetfish) {
            float d = Vector3Distance(c.pos, at);
            if (d < radius * 1.5f) { c.state = AbyssCreatureState::Fleeing; c.stateTimer = 0; }
        }
    }
}

// A predictive pursuit: steers toward where the target will be (leading its current velocity), not just
// where it is right now, and does it by accelerating the creature's own velocity toward a desired one
// rather than snapping velocity outright - real inertia, and a genuinely harder target to juke by simply
// changing direction than a creature that teleport-turns to face you every frame.
void Pursue(AbyssCreature& c, Vector3 targetPos, Vector3 targetVel, float maxSpeed, float accel, float dt) {
    float dist = Vector3Distance(c.pos, targetPos);
    float lead = std::clamp(dist / std::max(1.0f, maxSpeed), 0.0f, 0.6f);
    Vector3 predicted = Vector3Add(targetPos, Vector3Scale(targetVel, lead));
    Vector3 toward = Vector3Subtract(predicted, c.pos);
    if (Vector3Length(toward) > 0.01f) {
        Vector3 desired = Vector3Scale(Vector3Normalize(toward), maxSpeed);
        c.vel = Vector3Add(c.vel, Vector3Scale(Vector3Subtract(desired, c.vel), std::min(1.0f, accel * dt)));
    }
    c.pos = Vector3Add(c.pos, Vector3Scale(c.vel, dt));
}

// A scared prey animal accelerates away from whatever spooked it (with its own inertia, not a scripted
// orbit) and only settles once it's put real distance between itself and the threat.
void FleeFrom(AbyssCreature& c, Vector3 threat, float maxSpeed, float accel, float dt) {
    Vector3 away = Vector3Subtract(c.pos, threat);
    if (Vector3Length(away) < 0.05f) away = {0, 1, 0};
    Vector3 desired = Vector3Scale(Vector3Normalize(away), maxSpeed);
    c.vel = Vector3Add(c.vel, Vector3Scale(Vector3Subtract(desired, c.vel), std::min(1.0f, accel * dt)));
    c.pos = Vector3Add(c.pos, Vector3Scale(c.vel, dt));
}

// Every hazard resolves the same way: a hard knockback away from its source and a stamina cost, gated by a
// brief immunity window so one crush/sting/lunge can't chain into a dozen in the same second. Reaching zero
// stamina this way (out of air, exhausted by the cold) is the one thing that ends a dive down here - nothing
// in the Abyss kills the player outright, the water itself does once you've spent everything dodging it.
void HazardHit(AbyssState& a, Vector3 from, float staminaCost, float knock) {
    if (a.hazardIFrame > 0) return;
    a.hazardIFrame = 0.5f;
    a.stamina = std::max(0.0f, a.stamina - staminaCost);
    Vector3 away = Vector3Subtract(a.playerPos, from);
    if (Vector3Length(away) < 0.05f) away = {0, 1, 0};
    a.playerVel = Vector3Add(a.playerVel, Vector3Scale(Vector3Normalize(away), knock));
}

} // namespace

void StartAbyss(Game& g) {
    AbyssState& a = g.abyss;
    bool wasVerifying = a.verifying; // a plain data reset below must not clobber a caller's headless flag
    a = AbyssState{};
    a.verifying = wasVerifying;
    a.seed = (unsigned)(GetRandomValue(1, 1000000));
    a.playerPos = {0, -8, 0};
    a.playerVel = {0, 0, 0};
    a.stamina = 100.0f;
    // --verify-abyss runs before a window/GL context exists, like --verify does for the platformer:
    // the trench is a GPU mesh, so it must never be touched in that headless path.
    if (!a.verifying && (gTrenchSeed != a.seed || !gTrenchReady)) BuildTrenchMesh(a.seed);
    PopulateEcosystem(a);
}

// The actual simulation step, taking its intent as plain arguments so both the real (keyboard-reading)
// path and the headless verifier's scripted-diver path share one implementation.
static void StepAbyss(Game& g, float dt, Vector2 drift, bool dashPressed, bool glideHeld, bool aimUp, bool aimDown) {
    AbyssState& a = g.abyss;
    if (a.dead) return;
    dt = std::min(dt, 1.0f / 30.0f);
    a.time += dt;

    // ---- dash: a short, sharp impulse that overrides drag and displaces nearby small life ----
    if (a.dashCooldown > 0) a.dashCooldown -= dt;
    if (a.dashTimer > 0) a.dashTimer -= dt;
    if (dashPressed && a.dashCooldown <= 0 && a.stamina >= 20.0f) {
        Vector3 dir = (drift.x != 0 || drift.y != 0) ? Vector3Normalize({drift.x, drift.y, 0}) : Vector3{0, -1, 0};
        a.playerVel = Vector3Add(a.playerVel, Vector3Scale(dir, 22.0f));
        a.isDashing = true; a.dashTimer = 0.3f; a.dashCooldown = 0.8f;
        a.stamina -= 20.0f;
        Disturb(a, a.playerPos, 2.0f * 12.0f); // "2-meter radius" from the design doc, in world units (1 unit ~ 6 world px of scale)
    }
    if (a.dashTimer <= 0) a.isDashing = false;

    // ---- hydro-glide: streamlines the body, cutting drag; aiming up parachutes the fall, aiming down
    // inside a downdraft rides the slipstream instead of fighting it ----
    a.isGliding = glideHeld;
    a.glidingUp = glideHeld && aimUp;
    a.glidingDown = glideHeld && aimDown;

    // ---- downdraft: a Leviathan dive, periodic, pulls everything near the shaft's centre downward hard ----
    a.downdraftTimer -= dt;
    if (a.downdraftTimer < -6.0f) { a.downdraftActive = !a.downdraftActive; a.downdraftTimer = a.downdraftActive ? 2.5f : (6.0f + Hash1(a.time) * 6.0f); }
    bool inDowndraft = a.downdraftActive && fabsf(a.playerPos.x) < 3.0f && fabsf(a.playerPos.z) < 3.0f;

    // ---- set-piece zones: layered on as physics effects, never as changes to the trench's own geometry ----
    float curDepth0 = -a.playerPos.y, curAngle0 = atan2f(a.playerPos.z, a.playerPos.x);
    const AbyssZone* ventZone = nullptr;
    bool inVentColumn = InZone(a, AbyssZoneKind::Vent, curDepth0, &ventZone) && fabsf(atan2f(sinf(curAngle0 - ventZone->angle), cosf(curAngle0 - ventZone->angle))) < 0.7f;
    const AbyssZone* poolZone = nullptr;
    bool inBrinePool = InZone(a, AbyssZoneKind::BrinePool, curDepth0, &poolZone);

    // ---- physics integration ----
    float gravity = 9.0f;
    Vector3 accel{drift.x * 10.0f, -gravity, 0};
    if (inDowndraft) accel.y -= 14.0f;
    if (inVentColumn) accel.y += 14.0f * ventZone->strength;               // a thermal-vent updraft: climb without gliding
    if (inBrinePool) accel.y *= (1.0f - poolZone->strength);               // the pool's near-weightless arena
    a.playerVel = Vector3Add(a.playerVel, Vector3Scale(accel, dt));
    float drag = 1.8f; // ambient fluid drag
    if (inBrinePool) drag *= 0.6f;                   // slower to slow down in the pool too - it reads as a different body of water
    if (a.isDashing) drag *= 0.15f;
    else if (a.glidingUp) drag *= 6.0f;             // hydro-parachute: sheds speed fast
    else if (a.glidingDown && inDowndraft) drag *= 0.05f; // slipstream: nearly frictionless with the current
    else if (a.isGliding) drag *= 0.35f;             // streamlined glide: cuts drag well below ambient
    float damp = 1.0f / (1.0f + drag * dt);
    a.playerVel = Vector3Scale(a.playerVel, damp);
    a.playerPos = Vector3Add(a.playerPos, Vector3Scale(a.playerVel, dt));

    if (!a.isDashing && !a.isGliding) a.stamina = std::min(100.0f, a.stamina + dt * 6.0f);
    if (a.hazardIFrame > 0) a.hazardIFrame -= dt;
    if (a.stamina <= 0.0f) a.dead = true; // exhausted by the cold and the current - the Abyss's one fail state

    // ---- trench wall: frictionless soft collision, keeps the player inside the jagged shaft ----
    float depth = -a.playerPos.y;
    float angle = atan2f(a.playerPos.z, a.playerPos.x);
    float horiz = sqrtf(a.playerPos.x * a.playerPos.x + a.playerPos.z * a.playerPos.z);
    float wallR = TrenchRadius(depth, angle, a.seed) - 0.6f;
    if (horiz > wallR && wallR > 0.01f) {
        float k = wallR / horiz;
        a.playerPos.x *= k; a.playerPos.z *= k;
        a.playerVel.x *= 0.1f; a.playerVel.z *= 0.1f; // the slime coating kills outward velocity, not vertical
    }

    // ---- creatures ----
    for (auto& c : a.creatures) {
        if (c.state == AbyssCreatureState::Shattered) continue;
        switch (c.kind) {
            case AbyssCreatureKind::GiantIsopod:
                if (c.state == AbyssCreatureState::Cling) {
                    // the bowling lane's whole gimmick: these let go on their own clock, not just on request
                    if (InZone(a, AbyssZoneKind::BowlingLane, c.depth)) {
                        c.stateTimer += dt;
                        float threshold = 3.0f + Hash1(c.depth + c.wallAngle) * 4.0f - c.personality.energy * 1.5f;
                        if (c.stateTimer > threshold && fabsf(depth - c.depth) < 60.0f) c.state = AbyssCreatureState::Dislodged;
                    }
                } else if (c.state == AbyssCreatureState::Dislodged) {
                    c.vel.y -= gravity * dt * (0.6f + c.personality.energy * 0.8f);
                    c.pos = Vector3Add(c.pos, Vector3Scale(c.vel, dt));
                    // smashes any Glass Sponge it falls through
                    for (auto& s : a.creatures) if (s.kind == AbyssCreatureKind::GlassSponge && s.state != AbyssCreatureState::Shattered)
                        if (Vector3Distance(s.pos, c.pos) < 2.2f) { s.state = AbyssCreatureState::Shattered; Disturb(a, s.pos, 8.0f); }
                    if (Vector3Distance(c.pos, a.playerPos) < 1.4f) { HazardHit(a, c.pos, 16.0f, 8.0f); c.state = AbyssCreatureState::Shattered; }
                }
                break;
            case AbyssCreatureKind::GulperEel:
            case AbyssCreatureKind::VampireSquid: {
                // Smart, physical predators: they notice the player on their own (ambient perception, gated
                // by curiosity/aggression - not just when a dash or a shattering sponge disturbs them), then
                // pursue with real inertia and a predictive lead on the player's own velocity, so changing
                // direction alone doesn't shake them the way snapping straight at the current position would.
                bool squid = c.kind == AbyssCreatureKind::VampireSquid;
                float maxSpd = (squid ? 8.0f : 5.4f) + c.personality.aggression * (squid ? 5.5f : 4.5f) + c.personality.energy * 1.5f;
                float accel = (squid ? 10.0f : 6.5f) + c.personality.energy * 4.0f; // how fast it can turn onto a new heading
                float perception = (squid ? 24.0f : 17.0f) * (0.6f + c.personality.curiosity * 0.9f);
                float dist = Vector3Distance(c.pos, a.playerPos);
                if (c.state == AbyssCreatureState::Idle) {
                    if (dist < perception && c.personality.aggression > 0.12f) { c.state = AbyssCreatureState::Hunting; c.stateTimer = 0; }
                    else { c.vel = Vector3Scale(c.vel, 1.0f / (1.0f + 3.0f * dt)); c.pos = Vector3Add(c.pos, Vector3Scale(c.vel, dt)); } // drifts to rest, doesn't teleport-stop
                } else if (c.state == AbyssCreatureState::Hunting) {
                    Pursue(c, a.playerPos, a.playerVel, maxSpd, accel, dt);
                    dist = Vector3Distance(c.pos, a.playerPos);
                    if (dist < 1.3f) { HazardHit(a, c.pos, squid ? 30.0f : 22.0f, squid ? 10.0f : 9.0f); c.state = AbyssCreatureState::Idle; c.stateTimer = 0; c.vel = Vector3Scale(c.vel, 0.25f); }
                    c.stateTimer += dt;
                    // persistence scales with aggression/energy - a relentless roll keeps coming far longer
                    // than a half-hearted one, and gives up only once the player has genuinely broken away
                    float patience = 2.5f + c.personality.aggression * 4.0f + c.personality.energy * 2.0f;
                    if (c.stateTimer > patience || dist > perception * 1.8f) { c.state = AbyssCreatureState::Idle; c.stateTimer = 0; }
                }
                break;
            }
            case AbyssCreatureKind::TrenchWorm: {
                float distToHome = Vector3Distance(c.home, a.playerPos);
                float perception = 5.5f + c.personality.aggression * 2.5f + c.personality.curiosity * 1.5f;
                if (c.state == AbyssCreatureState::Idle) {
                    c.pos = c.home; // coiled back into the wall - nothing to see until it strikes
                    if (c.stateTimer > 0) c.stateTimer -= dt;
                    else if (distToHome < perception) {
                        c.state = AbyssCreatureState::Lunging; c.stateTimer = 0;
                        // leads the strike at where the player is heading, not just where they stood when it coiled
                        Vector3 lead = Vector3Add(a.playerPos, Vector3Scale(a.playerVel, 0.25f));
                        c.vel = Vector3Scale(Vector3Normalize(Vector3Subtract(lead, c.home)), 10.0f + c.personality.energy * 3.0f);
                    }
                } else if (c.state == AbyssCreatureState::Lunging) {
                    c.pos = Vector3Add(c.pos, Vector3Scale(c.vel, dt));
                    c.stateTimer += dt;
                    if (Vector3Distance(c.pos, a.playerPos) < 1.1f) HazardHit(a, c.pos, 26.0f, 9.0f);
                    if (c.stateTimer > 0.4f || Vector3Distance(c.pos, c.home) > 7.0f) {
                        c.state = AbyssCreatureState::Idle; c.stateTimer = 1.6f + c.personality.energy * 1.0f; c.pos = c.home; c.vel = {0, 0, 0};
                    }
                }
                break;
            }
            case AbyssCreatureKind::BrineSlug: {
                // slow, heavy, and it never lets go of the wall - the hazard the bowling lane and brine pool lean on
                c.phase += dt * (0.15f + c.personality.energy * 0.15f);
                float ang = c.wallAngle + sinf(c.phase) * 0.6f;
                c.pos = ShaftPos(c.depth, ang, c.radiusOffset, a.seed);
                if (Vector3Distance(c.pos, a.playerPos) < 1.5f) HazardHit(a, c.pos, 24.0f, 9.0f);
                break;
            }
            case AbyssCreatureKind::Siphonophore: {
                c.phase += dt;
                c.pos.y = c.home.y + sinf(c.phase * 0.8f) * 0.4f; // a slow bob, mostly stationary
                if (Vector3Distance(c.pos, a.playerPos) < 1.3f) HazardHit(a, c.pos, 22.0f, 6.0f);
                break;
            }
            case AbyssCreatureKind::Hatchetfish: {
                // a real scared-prey reaction: accelerate away from whatever spooked it, with its own
                // inertia, then settle back toward its school's home point once it's actually put distance
                // between itself and the threat - not a fixed-radius orbit that always looks the same.
                bool fleeing = c.state == AbyssCreatureState::Fleeing;
                if (fleeing) {
                    FleeFrom(c, a.playerPos, 6.0f + c.personality.energy * 3.0f, 14.0f, dt);
                    c.stateTimer += dt;
                    if (c.stateTimer > 1.2f && Vector3Distance(c.pos, c.home) > 2.5f) { c.state = AbyssCreatureState::Idle; c.stateTimer = 0; }
                } else {
                    c.phase += dt * 1.0f;
                    Vector3 driftTarget = Vector3Add(c.home, {cosf(c.phase) * 1.2f, sinf(c.phase * 1.3f) * 0.4f, sinf(c.phase) * 1.2f});
                    c.vel = Vector3Add(c.vel, Vector3Scale(Vector3Subtract(driftTarget, c.pos), std::min(1.0f, 4.0f * dt)));
                    c.pos = Vector3Add(c.pos, Vector3Scale(c.vel, dt));
                }
                break;
            }
            case AbyssCreatureKind::Leviathan: {
                c.stateTimer += dt;
                if (c.state == AbyssCreatureState::Idle) {
                    float baseDepth = -c.home.y;
                    float patrolDepth = std::clamp(baseDepth + sinf(c.stateTimer * 0.06f) * 150.0f, 20.0f, ABYSS_DEPTH_SPAN - 20.0f);
                    c.pos = ShaftPos(patrolDepth, c.wallAngle, c.radiusOffset, a.seed);
                    // a colossal body passing close is rare and its own aggression-gated roll, not a scripted ambush
                    if (c.stateTimer > 6.0f && Vector3Distance(c.pos, a.playerPos) < 220.0f && c.personality.aggression > 0.82f && Hash1(a.time * 3.0f + c.wallAngle) < 0.002f) {
                        c.state = AbyssCreatureState::Passing; c.stateTimer = 0;
                        c.vel = Vector3Scale(Vector3Normalize(Vector3Subtract(a.playerPos, c.pos)), 26.0f);
                    }
                } else if (c.state == AbyssCreatureState::Passing) {
                    c.pos = Vector3Add(c.pos, Vector3Scale(c.vel, dt));
                    if (Vector3Distance(c.pos, a.playerPos) < 4.5f) HazardHit(a, c.pos, 30.0f, 14.0f);
                    if (c.stateTimer > 1.2f) c.state = AbyssCreatureState::Idle;
                }
                break;
            }
            // ---- ParkourReference1.3 ----
            case AbyssCreatureKind::WhaleFall: // it grazes the skeleton, slowly shifting along the wall; nothing bothers it
                c.wallAngle += sinf(a.time * 0.05f + c.phase) * 0.02f * dt;
                c.pos = ShaftPos(c.depth, c.wallAngle, 4.0f, a.seed);
                break;
            case AbyssCreatureKind::AnglerCephalopod: {
                c.stateTimer += dt;
                float dLure = Vector3Distance(a.playerPos, c.home), dBody = Vector3Distance(a.playerPos, c.pos);
                if (c.state == AbyssCreatureState::Idle && a.invisT <= 0 && (dLure < 2.6f || dBody < 3.0f)) { c.state = AbyssCreatureState::Investigating; c.stateTimer = 0; } // the tell: the lure goes dark
                else if (c.state == AbyssCreatureState::Investigating && c.stateTimer > 0.6f) { c.state = AbyssCreatureState::Lunging; c.stateTimer = 0; c.vel = Vector3Scale(Vector3Normalize(Vector3Subtract(a.playerPos, c.pos)), 16.0f); }
                else if (c.state == AbyssCreatureState::Lunging) {
                    c.pos = Vector3Add(c.pos, Vector3Scale(c.vel, dt));
                    if (Vector3Distance(c.pos, a.playerPos) < 1.4f) HazardHit(a, c.pos, 34.0f, 10.0f); // the beak
                    if (c.stateTimer > 0.5f) { c.state = AbyssCreatureState::Fleeing; c.stateTimer = 0; }
                } else if (c.state == AbyssCreatureState::Fleeing) { // back into its moss
                    Vector3 lair = ShaftPos(c.depth, c.wallAngle, 1.0f, a.seed);
                    c.pos = Vector3Add(c.pos, Vector3Scale(Vector3Subtract(lair, c.pos), std::min(1.0f, dt * 2)));
                    if (c.stateTimer > 3.0f) c.state = AbyssCreatureState::Idle;
                }
                break;
            }
            case AbyssCreatureKind::PressureGhost: { // drifts, swells (the tell), then inhales: suction toward it
                c.stateTimer += dt;
                c.pos = Vector3Add(c.home, {sinf(a.time * 0.2f + c.phase) * 3, sinf(a.time * 0.13f) * 4, cosf(a.time * 0.17f + c.phase) * 3});
                float cyc = fmodf(c.stateTimer, 10.0f);
                c.state = cyc < 6.5f ? AbyssCreatureState::Idle : cyc < 8.0f ? AbyssCreatureState::Investigating : AbyssCreatureState::Hunting;
                if (c.state == AbyssCreatureState::Hunting) {
                    Vector3 to = Vector3Subtract(c.pos, a.playerPos);
                    float d = Vector3Length(to);
                    bool sheltered = false;
                    for (const auto& w : a.creatures) if (w.kind == AbyssCreatureKind::WhaleFall && Vector3Distance(w.pos, a.playerPos) < 6.0f && a.playerPos.y < w.pos.y + 1.0f) sheltered = true; // under its back
                    if (d < 30.0f && !sheltered) a.playerVel = Vector3Add(a.playerVel, Vector3Scale(Vector3Normalize(to), 26.0f * (1 - d / 30.0f) * dt * 2.2f));
                    if (d < 3.2f) HazardHit(a, c.pos, 20.0f, 6.0f);
                }
                break;
            }
            case AbyssCreatureKind::TrenchMaw: { // rises up the shaft; driven back by a pressure-bulb's blast; sinks away in time
                c.stateTimer += dt;
                if (c.state == AbyssCreatureState::Fleeing || c.stateTimer > 25.0f) {
                    c.state = AbyssCreatureState::Fleeing; c.pos.y -= 12.0f * dt;
                    if (c.pos.y < a.playerPos.y - 60.0f) { c.alive = false; c.state = AbyssCreatureState::Shattered; a.mawT = 0; }
                    break;
                }
                float speed = c.state == AbyssCreatureState::Dislodged ? -10.0f : (a.playerPos.y - c.pos.y > 25 ? 11.0f : 6.5f); // (recoiling, it sinks)
                if (c.state == AbyssCreatureState::Dislodged && c.stateTimer > 3.0f) { c.state = AbyssCreatureState::Hunting; }
                c.pos.y += speed * dt;
                c.pos.x += (a.playerPos.x - c.pos.x) * std::min(1.0f, dt * 0.6f); c.pos.z += (a.playerPos.z - c.pos.z) * std::min(1.0f, dt * 0.6f);
                if (fabsf(a.playerPos.y - c.pos.y) < 3.0f && Vector2Length({a.playerPos.x - c.pos.x, a.playerPos.z - c.pos.z}) < 8.0f) HazardHit(a, c.pos, 60.0f, 18.0f); // swallowed
                break;
            }
            case AbyssCreatureKind::SlimeHagfish: { // writhing near the coral; drawn to blood
                Vector3 target = c.home;
                for (const auto& o : a.creatures) if (o.kind == AbyssCreatureKind::AbyssalCoral && o.stateTimer > 0 && Vector3Distance(o.pos, c.pos) < 40) target = o.pos; // fresh blood on the coral
                Vector3 goal = Vector3Add(target, {sinf(a.time * 2 + c.phase) * 1.5f, cosf(a.time * 1.7f + c.phase) * 1.0f, cosf(a.time * 2.3f + c.phase) * 1.5f});
                c.pos = Vector3Add(c.pos, Vector3Scale(Vector3Subtract(goal, c.pos), std::min(1.0f, dt * 1.5f)));
                break;
            }
            case AbyssCreatureKind::VoidMoss: if (a.isDashing && Vector3Distance(c.pos, a.playerPos) < 2.2f) a.invisT = 3.0f; break; // dash through it: unseen
            case AbyssCreatureKind::GhostKelp:
                if (Vector3Distance(c.pos, a.playerPos) < 1.8f && c.stateTimer <= 0) { a.stamina = std::min(100.0f, a.stamina + 35); a.dashCooldown = 0; c.stateTimer = 8; } // everything reset
                c.stateTimer -= dt;
                break;
            case AbyssCreatureKind::PressureBulb:
                if (c.stateTimer <= 0 && Vector3Distance(c.pos, a.playerPos) < 1.8f) { // it implodes: a concussive shockwave
                    c.stateTimer = 12;
                    Disturb(a, c.pos, 30.0f);
                    for (auto& o : a.creatures) {
                        float d2 = Vector3Distance(o.pos, c.pos);
                        if (o.kind == AbyssCreatureKind::TrenchMaw && d2 < 18.0f) { o.state = AbyssCreatureState::Dislodged; o.stateTimer = 0; }
                        else if (d2 < 9.0f && (o.kind == AbyssCreatureKind::GulperEel || o.kind == AbyssCreatureKind::VampireSquid || o.kind == AbyssCreatureKind::AnglerCephalopod || o.kind == AbyssCreatureKind::TrenchWorm)) { o.state = AbyssCreatureState::Idle; o.vel = {0, 0, 0}; o.stateTimer = -2.5f; }
                    }
                }
                c.stateTimer -= dt;
                break;
            case AbyssCreatureKind::AbyssalCoral: {
                c.stateTimer -= dt;
                for (auto& o : a.creatures) if ((o.kind == AbyssCreatureKind::GulperEel || o.kind == AbyssCreatureKind::VampireSquid) && o.state == AbyssCreatureState::Hunting && Vector3Distance(o.pos, c.pos) < 2.0f) {
                    o.health -= 0.5f * dt; o.vel = Vector3Scale(o.vel, 0.7f); c.stateTimer = 10; // shredded: blood in the water
                }
                if (Vector3Distance(c.pos, a.playerPos) < 1.4f) HazardHit(a, c.pos, 10.0f, 5.0f);
                break;
            }
            default: break;
        }
    }
    // hunters lose a diver they can't see (void-moss)
    a.invisT = std::max(0.0f, a.invisT - dt);
    if (a.invisT > 0) for (auto& c : a.creatures) if ((c.kind == AbyssCreatureKind::GulperEel || c.kind == AbyssCreatureKind::VampireSquid) && c.state == AbyssCreatureState::Hunting) c.state = AbyssCreatureState::Idle;
    // slime-hagfish mucus: the water round them is slick - you drop faster
    for (const auto& c : a.creatures) if (c.kind == AbyssCreatureKind::SlimeHagfish && Vector3Distance(c.pos, a.playerPos) < 5.0f) { a.playerVel.y -= 6.0f * dt; break; }
    // the director: after a long enough calm, the Trench-Maw rises from below
    if (a.hazardIFrame > 0.45f) a.lastHitT = a.time;
    a.mawT += dt;
    bool maw = false;
    for (const auto& c : a.creatures) if (c.kind == AbyssCreatureKind::TrenchMaw && c.alive && c.state != AbyssCreatureState::Shattered) maw = true;
    if (!maw && !a.verifying && a.mawT > (a.mawVisits == 0 ? 75.0f : 120.0f) && a.time - a.lastHitT > 20.0f && depth > 150 && depth < ABYSS_DEPTH_SPAN - 80) {
        AbyssCreature m; m.kind = AbyssCreatureKind::TrenchMaw; m.state = AbyssCreatureState::Hunting; m.depth = depth + 45;
        m.pos = m.home = {a.playerPos.x, a.playerPos.y - 45.0f, a.playerPos.z};
        a.creatures.push_back(m);
        a.mawVisits++; a.mawT = 0;
    }
    // downdraft dislodges clinging isopods near the shaft centre
    if (inDowndraft) for (auto& c : a.creatures)
        if (c.kind == AbyssCreatureKind::GiantIsopod && c.state == AbyssCreatureState::Cling && -c.pos.y > depth - 40 && -c.pos.y < depth + 5)
            c.state = AbyssCreatureState::Dislodged;

    // ---- Glass Sponge landings: only a soft-enough vertical speed (i.e. parachuted) survives contact ----
    for (auto& s : a.creatures) {
        if (s.kind != AbyssCreatureKind::GlassSponge || s.state == AbyssCreatureState::Shattered) continue;
        if (Vector3Distance(s.pos, a.playerPos) < 1.6f) {
            if (a.playerVel.y > -5.0f) { a.playerVel.y = 0; a.playerPos.y = s.pos.y; }
            else { s.state = AbyssCreatureState::Shattered; Disturb(a, s.pos, 8.0f); }
        }
    }
    // ---- Rock Ledges: a plain stone shelf, always safe to land on at any speed - the dependable rest stop
    // a Glass Sponge can't be, since those are meant to punish a hard landing ----
    for (auto& s : a.creatures) {
        if (s.kind != AbyssCreatureKind::RockLedge) continue;
        float rx = fabsf(s.pos.x - a.playerPos.x), rz = fabsf(s.pos.z - a.playerPos.z);
        float reach = s.phase > 0.5f ? 3.2f : 1.8f; // the cavern's blocking ledge is wider than an ordinary one
        if (rx < reach && rz < reach && a.playerPos.y <= s.pos.y + 0.3f && a.playerVel.y <= 0.0f) {
            a.playerVel.y = 0; a.playerPos.y = s.pos.y;
        }
    }

    // plankton puffs fade
    for (auto& p : a.puffs) p.life += dt;
    a.puffs.erase(std::remove_if(a.puffs.begin(), a.puffs.end(), [](const AbyssPlanktonPuff& p) { return p.life > p.maxLife; }), a.puffs.end());

    // ---- marine snow: ambient drift that streaks faster in the seconds before a downdraft hits, so the
    // water itself telegraphs the hazard instead of it just switching on ----
    if (a.snow.empty() && !a.verifying) for (int i = 0; i < 90; i++)
        a.snow.push_back({{Hash2((float)i, 1.0f) * 16 - 8, -Hash2((float)i, 2.0f) * ABYSS_DEPTH_SPAN, Hash2((float)i, 3.0f) * 16 - 8},
                           0.4f + Hash1((float)i) * 0.6f, Hash1((float)i + 500) * 6.28f});
    float telegraph = std::clamp(1.2f - a.downdraftTimer, 0.0f, 1.0f); // rises as the next downdraft approaches
    for (auto& m : a.snow) {
        m.pos.y -= m.speed * dt * (1.0f + telegraph * 5.0f); // sinks, and streaks harder just before the current hits
        if (m.pos.y < -a.playerPos.y - 20.0f) { m.pos.y += ABYSS_DEPTH_SPAN; m.pos.x = Hash2(m.pos.y, m.phase) * 16 - 8; m.pos.z = Hash2(m.pos.y, m.phase + 1) * 16 - 8; }
    }

    a.depth = depth;
    a.bestDepth = std::max(a.bestDepth, a.depth);
    if (depth >= ABYSS_DEPTH_SPAN - 12.0f) a.won = true; // reached the bottom of this vertical slice's trench
    if (a.depth > ABYSS_DEPTH_SPAN - 10) a.depth = ABYSS_DEPTH_SPAN - 10; // the vertical slice's floor, until the generator streams further
}

void UpdateAbyss(Game& g, float dt) {
    AbyssState& a = g.abyss;
    Vector2 drift{0, 0};
    bool dashPressed = false, glideHeld = false, aimUp = false, aimDown = false;
    if (!a.verifying) {
        bool right = IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT), left = IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT);
        bool up = IsKeyDown(KEY_W) || IsKeyDown(KEY_UP), down = IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN);
        if (right) drift.x += 1;
        if (left)  drift.x -= 1;
        if (up)   { drift.y += 1; aimUp = true; }
        if (down)  { drift.y -= 1; aimDown = true; }
        glideHeld = IsKeyDown(KEY_LEFT_SHIFT);
        // Dash on Space, or (per feedback that Space alone wasn't discoverable) a double-tap of a movement
        // key within 0.3s, the way most games with a dodge/dash actually teach it.
        dashPressed = IsKeyPressed(KEY_SPACE);
        bool tapped[4] = {IsKeyPressed(KEY_D) || IsKeyPressed(KEY_RIGHT), IsKeyPressed(KEY_A) || IsKeyPressed(KEY_LEFT),
                           IsKeyPressed(KEY_W) || IsKeyPressed(KEY_UP), IsKeyPressed(KEY_S) || IsKeyPressed(KEY_DOWN)};
        for (int i = 0; i < 4; i++) if (tapped[i]) {
            if (a.time - a.tapTime[i] < 0.3f) dashPressed = true;
            a.tapTime[i] = a.time;
        }
    }
    StepAbyss(g, dt, drift, dashPressed, glideHeld, aimUp, aimDown);
}

// Creatures are drawn with raylib's unlit primitives, so they get a cheap cel light instead: each opaque part is
// drawn dark, then a smaller copy shifted toward the diver's lamp in the base tone, then a small highlight -
// the shifted copies poke out of the lit side, which reads as a lit, rounded form rather than a flat blob.
static Vector3 gLamp{0, 0, 0};
static void LitSphere(Vector3 p, float r, Color c) {
    Vector3 L = Vector3Normalize(Vector3Subtract(gLamp, p));
    DrawSphere(p, r, Tone(c, -0.45f));
    DrawSphere(Vector3Add(p, Vector3Scale(L, r * 0.3f)), r * 0.78f, c);
    DrawSphere(Vector3Add(p, Vector3Scale(L, r * 0.62f)), r * 0.42f, Tone(c, 0.3f));
}
static void LitCyl(Vector3 a0, Vector3 a1, float r0, float r1, int sides, Color c) {
    Vector3 m = Vector3Scale(Vector3Add(a0, a1), 0.5f), L = Vector3Normalize(Vector3Subtract(gLamp, m));
    float rr = std::max(r0, r1);
    DrawCylinderEx(a0, a1, r0, r1, sides, Tone(c, -0.45f));
    Vector3 o = Vector3Scale(L, rr * 0.34f);
    DrawCylinderEx(Vector3Add(a0, o), Vector3Add(a1, o), r0 * 0.72f, r1 * 0.72f, sides, c);
}
static void LitCapsule(Vector3 a0, Vector3 a1, float r, Color c) {
    Vector3 m = Vector3Scale(Vector3Add(a0, a1), 0.5f), L = Vector3Normalize(Vector3Subtract(gLamp, m)), o = Vector3Scale(L, r * 0.34f);
    DrawCapsule(a0, a1, r, 8, 4, Tone(c, -0.45f));
    DrawCapsule(Vector3Add(a0, o), Vector3Add(a1, o), r * 0.72f, 8, 4, c);
}

static Color KindColor(AbyssCreatureKind k) {
    switch (k) {
        case AbyssCreatureKind::GlassSponge: return {80, 230, 255, 255};
        case AbyssCreatureKind::GiantIsopod:  return {150, 110, 90, 255};
        case AbyssCreatureKind::GulperEel:    return {40, 30, 50, 255};
        case AbyssCreatureKind::VampireSquid: return {120, 34, 52, 255};
        case AbyssCreatureKind::Siphonophore: return {120, 255, 120, 255};
        case AbyssCreatureKind::TrenchWorm:   return {110, 70, 60, 255};
        case AbyssCreatureKind::Hatchetfish:  return {200, 220, 255, 255};
        case AbyssCreatureKind::BrineSlug:    return {90, 140, 120, 255};
        case AbyssCreatureKind::Leviathan:    return {14, 16, 24, 255};
        case AbyssCreatureKind::RockLedge:    return {90, 88, 96, 255};
        default: return WHITE;
    }
}

// A subtle screen-space wobble that grows with depth: the trench "pressing in" on the lens, cheap enough to
// run as a single pass over the already-composited 3D render texture. Never touched from the headless
// verifier (no GL context exists there), same rule as the trench mesh and the 3D camera itself.
static Shader gPressureShader{};
static bool gPressureReady = false;
static int gPresTimeLoc = -1, gPresDepthLoc = -1;
static const char* PRESSURE_FS = R"(#version 330
in vec2 fragTexCoord;
in vec4 fragColor;
uniform sampler2D texture0;
uniform float uTime;
uniform float uDepth;
out vec4 finalColor;
void main() {
    vec2 uv = fragTexCoord;
    float amt = clamp(uDepth / 900.0, 0.0, 1.0) * 0.006;
    uv.x += sin(uv.y * 40.0 + uTime * 1.7) * amt;
    uv.y += cos(uv.x * 36.0 + uTime * 1.3) * amt;
    finalColor = texture(texture0, uv) * fragColor;
}
)";
static void EnsurePressureShader() {
    if (gPressureReady) return;
    gPressureShader = LoadShaderFromMemory(nullptr, PRESSURE_FS);
    gPresTimeLoc = GetShaderLocation(gPressureShader, "uTime");
    gPresDepthLoc = GetShaderLocation(gPressureShader, "uDepth");
    gPressureReady = true;
}

// The trench rock, lit for real: the diver's lamp is a spotlight (a cone with a hot centre and soft edge, falling
// off with distance), the rock gets grain and relief from 3D value noise perturbing its normal and albedo, faint
// bioluminescent flecks glow on their own, and everything drowns in an exponential black fog - past a dozen metres
// there is nothing. (ParkourReference1.2: void black, cyan glass, abyssal magenta; the user: semi-real, terrifying,
// reduced visibility.)
static Shader gRockShader{};
static bool gRockReady = false;
static int gRkLight = -1, gRkDir = -1, gRkCam = -1, gRkTime = -1, gRkFog = -1;
static const char* ROCK_VS = R"(#version 330
in vec3 vertexPosition; in vec3 vertexNormal; in vec4 vertexColor;
uniform mat4 mvp; uniform mat4 matModel;
out vec3 vPos; out vec3 vNormal; out vec4 vColor;
void main() { vPos = (matModel * vec4(vertexPosition, 1.0)).xyz; vNormal = vertexNormal; vColor = vertexColor; gl_Position = mvp * vec4(vertexPosition, 1.0); }
)";
static const char* ROCK_FS = R"(#version 330
in vec3 vPos; in vec3 vNormal; in vec4 vColor;
uniform vec3 uLight; uniform vec3 uDir; uniform vec3 uCam; uniform float uTime; uniform float uFog;
out vec4 finalColor;
float h(vec3 p) { return fract(sin(dot(p, vec3(12.9898, 78.233, 45.164))) * 43758.5453); }
float vnoise(vec3 p) { vec3 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
  return mix(mix(mix(h(i), h(i + vec3(1,0,0)), f.x), mix(h(i + vec3(0,1,0)), h(i + vec3(1,1,0)), f.x), f.y),
             mix(mix(h(i + vec3(0,0,1)), h(i + vec3(1,0,1)), f.x), mix(h(i + vec3(0,1,1)), h(i + vec3(1,1,1)), f.x), f.y), f.z); }
float fbm(vec3 p) { float a = 0.5, s = 0.0; for (int k = 0; k < 4; k++) { s += a * vnoise(p); p *= 2.07; a *= 0.5; } return s; }
void main() {
  vec3 p = vPos;
  float e = 0.15, n0 = fbm(p * 0.9);
  vec3 grad = vec3(fbm((p + vec3(e,0,0)) * 0.9) - n0, fbm((p + vec3(0,e,0)) * 0.9) - n0, fbm((p + vec3(0,0,e)) * 0.9) - n0) / e;
  vec3 N = normalize(normalize(vNormal) - grad * 0.9);
  float strata = 0.5 + 0.5 * sin(p.y * 1.7 + fbm(p * 0.3) * 6.0);
  vec3 albedo = vColor.rgb * mix(0.55, 1.15, n0) * mix(0.8, 1.05, strata);
  albedo = mix(albedo, vec3(0.05, 0.10, 0.08), smoothstep(0.55, 0.75, fbm(p * 2.3)));
  vec3 L = uLight - p; float d = length(L); L /= d;
  float cone = smoothstep(0.55, 0.92, dot(-L, normalize(uDir)));
  float atten = 1.0 / (1.0 + 0.02 * d + 0.0022 * d * d);
  float diff = max(dot(N, L), 0.0);
  vec3 V = normalize(uCam - p), H = normalize(L + V);
  float spec = pow(max(dot(N, H), 0.0), 24.0) * 0.35;
  float halo = 0.10 / (1.0 + 0.5 * d * d);
  vec3 lit = albedo * (0.02 + (diff * cone * 3.2 + halo + diff * 0.10) * atten) + vec3(spec * cone * atten);
  float fleck = step(0.985, h(floor(p * 3.0))) * (0.5 + 0.5 * sin(uTime * 1.3 + h(floor(p * 3.0)) * 40.0));
  lit += vec3(0.1, 0.8, 0.9) * fleck * 0.25;
  float fog = 1.0 - exp(-uFog * length(uCam - p));
  finalColor = vec4(mix(lit, vec3(0.004, 0.006, 0.012), clamp(fog, 0.0, 1.0)), 1.0);
}
)";
static void EnsureRockShader() {
    if (gRockReady) return;
    gRockShader = LoadShaderFromMemory(ROCK_VS, ROCK_FS);
    gRockShader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(gRockShader, "mvp");
    gRockShader.locs[SHADER_LOC_MATRIX_MODEL] = GetShaderLocation(gRockShader, "matModel");
    gRkLight = GetShaderLocation(gRockShader, "uLight"); gRkDir = GetShaderLocation(gRockShader, "uDir"); gRkCam = GetShaderLocation(gRockShader, "uCam");
    gRkTime = GetShaderLocation(gRockShader, "uTime"); gRkFog = GetShaderLocation(gRockShader, "uFog");
    gRockReady = true;
}
// Everything else in the trench fades into the same fog (creatures and props are drawn unlit).
static Vector3 gFogCam{0, 0, 0};
static float gFogDensity = 0.09f;
static Color Fogged(Color c, Vector3 at) {
    float f = std::clamp(1.0f - expf(-gFogDensity * Vector3Distance(gFogCam, at)), 0.0f, 1.0f);
    return Color{(unsigned char)(c.r * (1 - f) + 1 * f), (unsigned char)(c.g * (1 - f) + 2 * f), (unsigned char)(c.b * (1 - f) + 3 * f), (unsigned char)(c.a * (1 - f * 0.6f))};
}

void SceneAbyss(Game& g) {
    AbyssState& a = g.abyss;
    bool over = a.dead || a.won;
    if (!a.verifying) {
        if (IsKeyPressed(KEY_ESCAPE)) { g.scene = Scene::Helm; return; }
        if (!over) UpdateAbyss(g, GetFrameTime());
        else if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_ENTER)) {
            if (!a.awarded) {
                a.awarded = true;
                if (a.won) {
                    g.abyssCleared = true;
                    g.gold += ABYSS_PAYOUT;
                    g.relicStorage.push_back(GetRandomValue(0, (int)Relics().size() - 1));
                    Toast(g, TextFormat("Surfaced with %d gold and a relic.", ABYSS_PAYOUT));
                } else {
                    Toast(g, "The cold and the current finally won. Better luck next dive.");
                }
            }
            g.scene = Scene::Helm;
            return;
        }
        g.abyss.bestDepth = std::max(g.abyss.bestDepth, g.abyss.depth);
        g.abyssBest = std::max(g.abyssBest, g.abyss.bestDepth);
        for (const auto& cr : g.abyss.creatures) if (Vector3Distance(cr.pos, g.abyss.playerPos) < 14.0f) g.platSeen[PL_COUNT] |= 1ull << (int)cr.kind; // the dossier: met down here
        AbyssAudio(g.abyss, GetFrameTime());
    }

    // BeginFrame/EndFrame are owned by the caller (the main loop or --shots), exactly like every other
    // Scene* function - this only draws into the frame that's already open.
    //
    // Free-look orbit around the player (mouse), not a fixed offset - per feedback that the camera
    // couldn't look around at all. Pitch stays clamped well short of +-90 degrees: a look direction
    // exactly parallel to the up vector degenerates raylib's LookAt into a singular view matrix and
    // renders nothing (the bug the old fixed-offset comment above used to describe), so this both fixes
    // that and gives a real look-around range without reintroducing it.
    if (!a.verifying) {
        Vector2 md = GetMouseDelta();
        a.camYaw -= md.x * 0.0035f;
        a.camPitch = std::clamp(a.camPitch - md.y * 0.0035f, -1.15f, 0.95f);
    }
    float camDist = 5.2f;
    Vector3 orbit{cosf(a.camPitch) * sinf(a.camYaw) * camDist, 1.5f + sinf(a.camPitch) * camDist, cosf(a.camPitch) * cosf(a.camYaw) * camDist};
    Vector3 camPos = Vector3Add(a.playerPos, orbit);
    // clamped inside the trench so getting close to a wall pulls the camera in rather than letting it
    // poke through the mesh and show the void/outside beyond it - the other half of the same complaint.
    {
        float camDepth = -camPos.y, camAngle = atan2f(camPos.z, camPos.x);
        float camHoriz = sqrtf(camPos.x * camPos.x + camPos.z * camPos.z);
        float camWallR = TrenchRadius(camDepth, camAngle, a.seed) - 0.5f;
        if (camHoriz > camWallR && camWallR > 0.1f) { float k = camWallR / camHoriz; camPos.x *= k; camPos.z *= k; }
    }
    Camera3D cam{};
    cam.position = camPos;
    cam.target = Vector3Add(a.playerPos, {0, -1.3f, 0}); // biased down the shaft (the fall direction), not dead-on the player
    cam.up = {0, 1, 0};
    cam.fovy = 65.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    // BeginMode3D needs a real depth-buffered target and drives its own projection/view matrices, which
    // would collide with the main scene canvas's pushed 2D supersample transform - so it's drawn into its
    // own render texture (like PixelRT/OceanRT) and composited in below, rather than nested in directly.
    BeginLayer(Mode3DRT());
    ClearBackground(Color{2, 3, 6, 255}); // the Void Canvas: near-Vantablack, no visible back wall
    BeginMode3D(cam);
    gFogCam = camPos;
    gLamp = Vector3Add(a.playerPos, {0, 1.0f, 0});
    gFogDensity = 0.035f + 0.035f * std::clamp(a.depth / ABYSS_DEPTH_SPAN, 0.0f, 1.0f); // it closes in the deeper you go
    if (gTrenchReady) {
        EnsureRockShader();
        Vector3 lampPos = Vector3Add(a.playerPos, {0, 0.4f, 0});
        Vector3 lampDir = Vector3Normalize(Vector3Subtract(cam.target, cam.position)); // the helmet lamp points where you look
        lampDir = Vector3Normalize(Vector3Add(lampDir, {0, -0.35f, 0}));                // and a little down the shaft
        SetShaderValue(gRockShader, gRkLight, &lampPos, SHADER_UNIFORM_VEC3);
        SetShaderValue(gRockShader, gRkDir, &lampDir, SHADER_UNIFORM_VEC3);
        SetShaderValue(gRockShader, gRkCam, &camPos, SHADER_UNIFORM_VEC3);
        SetShaderValue(gRockShader, gRkTime, &a.time, SHADER_UNIFORM_FLOAT);
        SetShaderValue(gRockShader, gRkFog, &gFogDensity, SHADER_UNIFORM_FLOAT);
        gTrenchModel.materials[0].shader = gRockShader;
        // seen from inside the shaft, looking outward at the wall: whichever winding that is, don't
        // gamble on it - the trench must never disappear because the camera ended up on its "back" side.
        rlDisableBackfaceCulling();
        DrawModel(gTrenchModel, {0, 0, 0}, 1.0f, WHITE);
        rlEnableBackfaceCulling();
    }

    // vent columns get a rising trail of bubbles so the updraft reads as a place, not an invisible force
    for (const auto& z : a.zones) if (z.kind == AbyssZoneKind::Vent) {
        for (int i = 0; i < 14; i++) {
            float t2 = fmodf(a.time * (1.6f + 0.3f * i) + i * 3.7f, z.span);
            Vector3 p = ShaftPos(z.depth + t2, z.angle, 1.0f, a.seed);
            DrawSphere(p, 0.12f + 0.05f * sinf(a.time * 3 + i), Fade(Color{140, 220, 255, 255}, 0.35f));
        }
    }
    // the brine pool reads as a distinctly different body of water: a broad, dim teal haze filling the band
    for (const auto& z : a.zones) if (z.kind == AbyssZoneKind::BrinePool) {
        Vector3 mid = ShaftPos(z.depth + z.span * 0.5f, 0, 0, a.seed);
        mid.x = mid.z = 0;
        DrawSphere(mid, 9.0f, Fade(Color{60, 140, 120, 255}, 0.08f));
    }

    for (auto& c : a.creatures) {
        if (c.state == AbyssCreatureState::Shattered) continue;
        Color col = Fogged(KindColor(c.kind), c.pos);
        // Facing along current velocity when moving, otherwise a stable per-entity default - so a lunging
        // Trench Worm or a hunting Eel visibly orients toward its target instead of always pointing one way.
        Vector3 face = Vector3LengthSqr(c.vel) > 0.05f ? Vector3Normalize(c.vel) : Vector3{cosf(c.wallAngle + PI), 0, sinf(c.wallAngle + PI)};
        switch (c.kind) {
            case AbyssCreatureKind::GlassSponge: {
                // a Venus's-flower-basket: a tall glass vase woven of silica lattice, lit from inside, with a fringe of rootlets
                Color glass = Fade(col, 0.28f), fibre = Fade(Tone(col, 0.3f), 0.7f);
                const int RINGS = 6, RIBS = 10;
                for (int r = 0; r < RINGS; r++) {
                    float y0 = r * 0.28f, rad = 0.45f + 0.18f * sinf(r * 0.7f) + r * 0.03f;
                    for (int k = 0; k < RIBS; k++) {
                        float a0 = k * 2 * PI / RIBS, a1 = (k + 1) * 2 * PI / RIBS;
                        Vector3 p0 = Vector3Add(c.pos, {cosf(a0) * rad, y0, sinf(a0) * rad}), p1 = Vector3Add(c.pos, {cosf(a1) * rad, y0, sinf(a1) * rad});
                        DrawLine3D(p0, p1, fibre);                                                                          // the ring
                        float rad2 = 0.45f + 0.18f * sinf((r + 1) * 0.7f) + (r + 1) * 0.03f;
                        if (r < RINGS - 1) DrawLine3D(p0, Vector3Add(c.pos, {cosf(a1) * rad2, y0 + 0.28f, sinf(a1) * rad2}), Fade(fibre, 0.5f)); // the diagonal weave
                    }
                }
                DrawCylinderEx(c.pos, Vector3Add(c.pos, {0, RINGS * 0.28f, 0}), 0.5f, 0.6f, 10, glass);                    // the glassy body
                DrawSphere(Vector3Add(c.pos, {0, 0.7f, 0}), 0.28f, Fade(Color{200, 250, 255, 255}, 0.18f + 0.08f * sinf(a.time * 1.3f + c.phase))); // the shrimp pair living inside, glowing faintly
                for (int k = 0; k < 8; k++) { float ra = k * 0.8f + c.phase; DrawLine3D(c.pos, Vector3Add(c.pos, {cosf(ra) * 0.8f, -0.4f, sinf(ra) * 0.8f}), Fade(fibre, 0.6f)); } // anchoring rootlets
                break;
            }
            case AbyssCreatureKind::GiantIsopod: {
                // a real giant isopod: seven overlapping armour plates, a pair of long antennae, fourteen legs rowing along the rock
                Vector3 side{-face.z, 0, face.x}, up{0, 1, 0};
                for (int s = 0; s < 7; s++) {
                    float k = s / 6.0f, w = 0.46f - fabsf(k - 0.35f) * 0.3f;
                    Vector3 p = Vector3Add(c.pos, Vector3Scale(face, 0.45f - k * 1.0f));
                    LitCyl(Vector3Subtract(p, Vector3Scale(side, w)), Vector3Add(p, Vector3Scale(side, w)), 0.2f, 0.2f, 8, Tone(col, (s % 2) * -0.12f - k * 0.15f)); // a plate, curved over the back
                    float stroke = sinf(a.time * 6 + s * 0.9f) * 0.12f;
                    for (int sd = -1; sd <= 1; sd += 2) LitCyl(Vector3Add(p, Vector3Scale(side, w * sd)), Vector3Add(p, Vector3Add(Vector3Scale(side, (w + 0.22f) * sd), Vector3Add(Vector3Scale(face, stroke), {0, -0.22f, 0}))), 0.04f, 0.02f, 4, Tone(col, -0.35f)); // legs
                }
                LitCyl(Vector3Add(c.pos, Vector3Scale(face, -0.62f)), Vector3Add(c.pos, Vector3Scale(face, -0.8f)), 0.26f, 0.12f, 6, Tone(col, -0.3f)); // the tail fan
                Vector3 head = Vector3Add(c.pos, Vector3Scale(face, 0.58f));
                LitSphere(head, 0.22f, Tone(col, 0.1f));
                for (int sd = -1; sd <= 1; sd += 2) {
                    DrawSphere(Vector3Add(head, Vector3Add(Vector3Scale(side, 0.14f * sd), Vector3Scale(up, 0.05f))), 0.07f, Fogged(Color{30, 34, 30, 255}, c.pos)); // the big compound eyes
                    Vector3 an = Vector3Add(head, Vector3Add(Vector3Scale(face, 0.9f), Vector3Add(Vector3Scale(side, 0.5f * sd), {0, 0.1f + sinf(a.time * 2 + sd) * 0.1f, 0})));
                    LitCyl(head, an, 0.025f, 0.008f, 4, Tone(col, -0.2f));                                      // the antennae, feeling ahead
                }
                break;
            }
            case AbyssCreatureKind::GulperEel: {
                // the pelican eel: a whip of a body ending in a glowing pink tail-lure, and a mouth that is almost all of it
                bool hunting = c.state == AbyssCreatureState::Hunting;
                Vector3 side{-face.z, 0, face.x}, up{0, 1, 0};
                Vector3 prev = c.pos;
                for (int s = 1; s <= 14; s++) {
                    float k = s / 14.0f;
                    Vector3 p = Vector3Add(c.pos, Vector3Scale(face, -k * 4.0f));
                    p = Vector3Add(p, Vector3Scale(side, sinf(a.time * 4.0f - k * 5.0f + c.phase) * 0.35f * k * (hunting ? 1.5f : 1.0f)));
                    LitCyl(prev, p, 0.3f * (1 - k) + 0.03f, 0.3f * (1 - k - 1 / 14.0f) + 0.02f, 6, Tone(col, -k * 0.2f));
                    prev = p;
                }
                DrawSphere(prev, 0.09f, Color{255, 90, 150, 255});                                   // the tail light, the only colour it has
                DrawSphere(prev, 0.3f, Fade(Color{255, 90, 150, 255}, 0.2f + 0.1f * sinf(a.time * 5)));
                float gape = hunting ? 0.9f : 0.25f;
                Vector3 hinge = Vector3Add(c.pos, Vector3Scale(face, 0.1f)), jawTip = Vector3Add(c.pos, Vector3Scale(face, 1.5f));
                Vector3 upperTip = Vector3Add(jawTip, Vector3Scale(up, gape)), lowerTip = Vector3Add(jawTip, Vector3Scale(up, -gape * 1.3f));
                Color jaw = Tone(col, 0.15f), pouch = Fogged(Color{70, 30, 50, 255}, c.pos);
                DrawTriangle3D(hinge, Vector3Add(upperTip, Vector3Scale(side, 0.3f)), Vector3Add(upperTip, Vector3Scale(side, -0.3f)), jaw);
                DrawTriangle3D(hinge, Vector3Add(upperTip, Vector3Scale(side, -0.3f)), Vector3Add(upperTip, Vector3Scale(side, 0.3f)), jaw);
                DrawTriangle3D(hinge, Vector3Add(lowerTip, Vector3Scale(side, 0.45f)), Vector3Add(lowerTip, Vector3Scale(side, -0.45f)), pouch); // the loose pouch of the lower jaw
                DrawTriangle3D(hinge, Vector3Add(lowerTip, Vector3Scale(side, -0.45f)), Vector3Add(lowerTip, Vector3Scale(side, 0.45f)), pouch);
                DrawSphere(Vector3Add(hinge, Vector3Add(Vector3Scale(up, 0.15f), Vector3Scale(face, 0.2f))), 0.05f, Fade(Color{220, 230, 255, 255}, 0.7f)); // a tiny eye at the hinge
                break;
            }
            case AbyssCreatureKind::VampireSquid: {
                // a cloak of webbed arms, blue eyes the size of its head, ear-like fins, and bioluminescent arm tips
                Vector3 side{-face.z, 0, face.x}, up{0, 1, 0};
                float pulse = 0.5f + 0.5f * sinf(a.time * 2.2f + c.phase); // it opens and closes the cloak as it swims
                LitSphere(c.pos, 0.5f, col);
                LitSphere(Vector3Add(c.pos, Vector3Scale(face, -0.35f)), 0.42f, Tone(col, -0.15f));                // the mantle
                for (int sd = -1; sd <= 1; sd += 2) {
                    Vector3 fin = Vector3Add(c.pos, Vector3Add(Vector3Scale(face, -0.55f), Vector3Scale(side, 0.45f * sd)));
                    DrawTriangle3D(fin, Vector3Add(fin, Vector3Add(Vector3Scale(side, 0.35f * sd), Vector3Scale(up, 0.3f + pulse * 0.1f))), Vector3Add(fin, Vector3Scale(face, -0.3f)), Tone(col, -0.1f));
                    DrawTriangle3D(fin, Vector3Add(fin, Vector3Scale(face, -0.3f)), Vector3Add(fin, Vector3Add(Vector3Scale(side, 0.35f * sd), Vector3Scale(up, 0.3f + pulse * 0.1f))), Tone(col, -0.1f));
                    DrawSphere(Vector3Add(c.pos, Vector3Add(Vector3Scale(face, 0.25f), Vector3Add(Vector3Scale(side, 0.3f * sd), Vector3Scale(up, 0.15f)))), 0.13f, Fogged(Color{90, 160, 255, 255}, c.pos)); // the eyes
                }
                Vector3 prevTip{};
                for (int t2 = 0; t2 <= 8; t2++) {
                    float ta = (t2 % 8) * 2 * PI / 8 + c.phase;
                    float spread = 0.4f + 0.6f * pulse;
                    Vector3 dir = Vector3Add(Vector3Scale(face, 0.9f - spread * 0.5f), Vector3Add(Vector3Scale(side, cosf(ta) * spread), Vector3Scale(up, sinf(ta) * spread)));
                    Vector3 tip = Vector3Add(c.pos, Vector3Scale(dir, 1.1f));
                    if (t2 < 8) {
                        LitCyl(c.pos, tip, 0.07f, 0.03f, 4, Tone(col, -0.2f));
                        DrawSphere(tip, 0.05f, Fade(Color{120, 200, 255, 255}, 0.5f + 0.5f * pulse));  // glowing tips
                    }
                    if (t2 > 0) { DrawTriangle3D(c.pos, prevTip, tip, Fade(Tone(col, -0.3f), 0.75f)); DrawTriangle3D(c.pos, tip, prevTip, Fade(Tone(col, -0.3f), 0.75f)); } // the web between the arms
                    prevTip = tip;
                }
                break;
            }
            case AbyssCreatureKind::Siphonophore: {
                // a colony: a gas float, a chain of swimming bells, then a long curtain of stinging threads that pulse with light
                Color bell = Fade(col, 0.35f);
                DrawSphere(Vector3Add(c.pos, {0, 0.25f, 0}), 0.16f, Fade(Color{255, 150, 90, 255}, 0.7f)); // the float
                for (int b2 = 0; b2 < 5; b2++) {
                    Vector3 bp = Vector3Add(c.pos, {sinf(a.time * 1.3f + b2) * 0.05f, -b2 * 0.28f, 0});
                    DrawCylinderEx(bp, Vector3Add(bp, {0, -0.22f, 0}), 0.12f, 0.2f + 0.03f * sinf(a.time * 4 + b2), 6, bell); // a bell, pumping
                }
                for (int t2 = 0; t2 < 7; t2++) {
                    Vector3 base = Vector3Add(c.pos, {(t2 - 3) * 0.07f, -1.4f, 0});
                    Vector3 prev = base;
                    for (int s = 1; s <= 6; s++) {
                        Vector3 q = Vector3Add(base, {sinf(a.time * 0.8f + t2 + s * 0.6f) * 0.12f * s, -s * 0.45f, cosf(a.time * 0.6f + t2 * 2 + s) * 0.08f * s});
                        float glow = 0.5f + 0.5f * sinf(a.time * 3 - s * 0.8f + t2); // light running down each thread
                        DrawLine3D(prev, q, Fade(col, 0.25f + 0.4f * glow));
                        if (s % 2 == 0) DrawSphere(q, 0.035f, Fade(col, 0.4f + 0.5f * glow)); // clustered stingers
                        prev = q;
                    }
                }
                break;
            }
            case AbyssCreatureKind::TrenchWorm: {
                // a ringed, bristled tube with a radial, fanged mouth flaring open only while lunging
                bool lunging = c.state == AbyssCreatureState::Lunging;
                Vector3 side{-face.z, 0, face.x}, up{0, 1, 0};
                for (int s = 0; s <= 8; s++) {
                    float k = s / 8.0f;
                    Vector3 p = Vector3Add(c.home, Vector3Scale(Vector3Subtract(c.pos, c.home), k));
                    p = Vector3Add(p, Vector3Scale(side, sinf(a.time * 3 + k * 4) * 0.1f * (1 - k)));
                    LitSphere(p, 0.3f - k * 0.04f, Tone(col, (s % 2) * -0.15f));
                    for (int b2 = 0; b2 < 4; b2++) { float ba = b2 * PI / 2 + s; DrawLine3D(p, Vector3Add(p, Vector3Add(Vector3Scale(side, cosf(ba) * 0.45f), Vector3Scale(up, sinf(ba) * 0.45f))), Fade(Color{200, 170, 140, 255}, 0.4f)); } // bristles
                }
                for (int f2 = 0; f2 < 6; f2++) {
                    float fa = f2 * 2 * PI / 6, open = lunging ? 0.45f : 0.12f;
                    Vector3 tip = Vector3Add(c.pos, Vector3Add(Vector3Scale(face, 0.5f), Vector3Add(Vector3Scale(side, cosf(fa) * open), Vector3Scale(up, sinf(fa) * open))));
                    DrawCylinderEx(c.pos, tip, 0.05f, 0.01f, 4, Fogged(Color{230, 220, 200, 255}, c.pos)); // the fangs
                }
                if (lunging) DrawSphere(Vector3Add(c.pos, Vector3Scale(face, 0.15f)), 0.2f, Fogged(Color{120, 20, 30, 255}, c.pos)); // the red throat
                break;
            }
            case AbyssCreatureKind::Hatchetfish: {
                // a silver hatchet: deep, flat, mirror-sided, a row of glowing photophores along the belly and huge tubular eyes looking up
                Vector3 side{-face.z, 0, face.x}, up{0, 1, 0};
                Vector3 nose = Vector3Add(c.pos, Vector3Scale(face, 0.2f)), tail = Vector3Add(c.pos, Vector3Scale(face, -0.22f));
                Vector3 keel = Vector3Add(c.pos, {0, -0.22f, 0}), back = Vector3Add(c.pos, {0, 0.08f, 0});
                Color silver = Tone(col, 0.1f);
                DrawTriangle3D(nose, back, keel, silver); DrawTriangle3D(nose, keel, back, silver);
                DrawTriangle3D(back, tail, keel, Tone(silver, -0.15f)); DrawTriangle3D(back, keel, tail, Tone(silver, -0.15f));
                DrawTriangle3D(tail, Vector3Add(tail, Vector3Add(Vector3Scale(face, -0.12f), Vector3Scale(up, 0.08f))), Vector3Add(tail, Vector3Add(Vector3Scale(face, -0.12f), Vector3Scale(up, -0.08f))), Fade(silver, 0.7f)); // the tail
                for (int k = 0; k < 4; k++) DrawSphere(Vector3Add(keel, Vector3Add(Vector3Scale(face, 0.1f - k * 0.07f), {0, 0.03f + k * 0.03f, 0})), 0.02f, Color{120, 200, 255, 255}); // photophores
                DrawSphere(Vector3Add(nose, Vector3Add(Vector3Scale(face, -0.05f), Vector3Scale(up, 0.03f))), 0.045f, Fade(Color{220, 240, 255, 255}, 0.8f)); // the eye
                (void)side;
                break;
            }
            case AbyssCreatureKind::BrineSlug: {
                // a sea cucumber of the brine pools: a soft translucent body walking on tube feet, a crown of feeding tentacles
                Vector3 side{-face.z, 0, face.x}, up{0, 1, 0};
                for (int s = 0; s < 6; s++) {
                    float k = s / 5.0f, sq = 1 + 0.08f * sinf(a.time * 2 - k * 4); // peristalsis along the body
                    Vector3 p = Vector3Add(c.pos, Vector3Scale(face, 0.95f * k));
                    DrawSphere(p, (0.42f - fabsf(k - 0.4f) * 0.25f) * sq, Fade(Tone(col, -k * 0.1f), 0.85f));
                    for (int sd = -1; sd <= 1; sd += 2) LitCyl(Vector3Add(p, Vector3Scale(side, 0.3f * sd)), Vector3Add(p, Vector3Add(Vector3Scale(side, 0.38f * sd), Vector3Scale(up, -0.35f))), 0.04f, 0.02f, 4, Tone(col, 0.2f)); // tube feet
                    LitSphere(Vector3Add(p, Vector3Scale(up, 0.36f)), 0.06f, Tone(col, 0.25f)); // papillae along the back
                }
                Vector3 mouth = Vector3Add(c.pos, Vector3Scale(face, 1.1f));
                for (int k = 0; k < 8; k++) { float ta = k * PI / 4 + a.time; DrawCylinderEx(mouth, Vector3Add(mouth, Vector3Add(Vector3Scale(face, 0.25f), Vector3Add(Vector3Scale(side, cosf(ta) * 0.2f), Vector3Scale(up, sinf(ta) * 0.2f)))), 0.03f, 0.01f, 4, Fogged(Color{230, 200, 160, 255}, c.pos)); }
                break;
            }
            case AbyssCreatureKind::RockLedge: {
                bool cavernFloor = c.phase > 0.5f;
                float r = cavernFloor ? 3.4f : 1.9f;
                DrawCylinderEx(c.pos, Vector3Add(c.pos, {0, 0.35f, 0}), r, r * 0.92f, 8, col);
                DrawCylinderEx(Vector3Add(c.pos, {r * 0.3f, 0.1f, r * 0.1f}), Vector3Add(c.pos, {r * 0.3f, 0.55f, r * 0.1f}), r * 0.22f, r * 0.18f, 6, Tone(col, 0.15f)); // an uneven outcrop, not a flat disc
                break;
            }
            case AbyssCreatureKind::Leviathan: {
                // suggested, not fully rendered: a huge, near-black, low-alpha mass with two faint eyes -
                // it should read as "something is down here", not as a modelled monster
                bool passing = c.state == AbyssCreatureState::Passing;
                Color lv = Fade(col, passing ? 0.5f : 0.22f);
                DrawCapsule(Vector3Add(c.pos, {0, 0, -7.0f}), Vector3Add(c.pos, {0, 0, 7.0f}), 3.2f, 10, 6, lv);
                DrawSphere(Vector3Add(c.pos, {0.9f, 0.6f, -7.5f}), 0.2f, Fade(Color{255, 220, 140, 255}, passing ? 0.9f : 0.4f));
                DrawSphere(Vector3Add(c.pos, {-0.9f, 0.6f, -7.5f}), 0.2f, Fade(Color{255, 220, 140, 255}, passing ? 0.9f : 0.4f));
                break;
            }
            // ---- ParkourReference1.3 ----
            case AbyssCreatureKind::WhaleFall: { // a whale's ribcage on the wall, and the vast isopod grazing it
                Vector3 inward = Vector3Normalize({-c.pos.x, 0, -c.pos.z});
                Vector3 side{-inward.z, 0, inward.x};
                for (int r = 0; r < 7; r++) { Vector3 base = Vector3Add(c.pos, Vector3Add(Vector3Scale(side, (r - 3) * 0.9f), {0, -1.2f, 0})); LitCyl(base, Vector3Add(base, Vector3Add(Vector3Scale(inward, 2.2f), {0, 2.4f, 0})), 0.14f, 0.06f, 5, Fogged(Color{210, 200, 180, 255}, base)); } // ribs
                Color shell = Fogged(Color{130, 120, 112, 255}, c.pos);
                for (int s = 0; s < 7; s++) { float k = s / 6.0f; Vector3 p = Vector3Add(c.pos, Vector3Add(Vector3Scale(side, (k - 0.5f) * 5.0f), Vector3Scale(inward, 0.8f))); DrawSphere(p, 1.25f - fabsf(k - 0.5f) * 0.9f, Tone(shell, (s % 2) * -0.12f)); } // armoured segments
                for (int l = 0; l < 7; l++) { Vector3 p = Vector3Add(c.pos, Vector3Add(Vector3Scale(side, (l - 3) * 0.7f), Vector3Scale(inward, 1.4f))); DrawCylinderEx(p, Vector3Add(p, {sinf(a.time + l) * 0.1f, -1.0f, 0}), 0.08f, 0.04f, 4, Tone(shell, -0.3f)); }
                break;
            }
            case AbyssCreatureKind::AnglerCephalopod: {
                bool dark = c.state != AbyssCreatureState::Idle;
                if (!dark) { DrawSphere(c.home, 0.22f, Color{190, 255, 230, 255}); DrawSphere(c.home, 0.7f, Fade(Color{150, 255, 220, 255}, 0.12f)); } // the lure: it looks like ghost-kelp
                LitCyl(c.pos, c.home, 0.03f, 0.02f, 4, Fogged(Color{90, 40, 60, 255}, c.home));
                Color sk = Fogged(Color{120, 30, 50, 255}, c.pos);
                DrawSphere(c.pos, 0.7f, sk); DrawSphere(Vector3Add(c.pos, {0, 0.5f, 0}), 0.45f, Tone(sk, 0.1f));
                for (int t2 = 0; t2 < 8; t2++) { float ta = t2 * PI / 4 + c.phase; Vector3 dir{cosf(ta) * 0.6f, -0.8f, sinf(ta) * 0.6f}; if (c.state == AbyssCreatureState::Lunging) dir = Vector3Add(Vector3Scale(Vector3Normalize(c.vel), 0.8f), Vector3Scale(dir, 0.3f)); DrawCylinderEx(c.pos, Vector3Add(c.pos, Vector3Scale(dir, 1.6f + 0.2f * sinf(a.time * 4 + t2))), 0.1f, 0.02f, 4, sk); }
                if (c.state == AbyssCreatureState::Investigating) DrawSphere(c.pos, 1.2f, Fade(Color{255, 60, 60, 255}, 0.08f + 0.06f * sinf(a.time * 30))); // the tell
                break;
            }
            case AbyssCreatureKind::PressureGhost: { // translucent, enormous, its bell swelling before it breathes in
                float swell = c.state == AbyssCreatureState::Investigating ? 1.0f + 0.25f * sinf(a.time * 12) : c.state == AbyssCreatureState::Hunting ? 1.35f : 1.0f;
                Color jelly = Fogged(Color{170, 150, 255, 255}, c.pos);
                DrawSphere(c.pos, 3.6f * swell, Fade(jelly, 0.10f));
                DrawSphere(Vector3Add(c.pos, {0, 0.6f, 0}), 2.6f * swell, Fade(jelly, 0.10f));
                DrawSphere(c.pos, 1.0f, Fade(Color{220, 200, 255, 255}, 0.25f)); // its glowing gut
                for (int t2 = 0; t2 < 12; t2++) { float ta = t2 * PI / 6 + a.time * 0.2f; Vector3 root = Vector3Add(c.pos, {cosf(ta) * 3.0f, -1.5f, sinf(ta) * 3.0f}); DrawCylinderEx(root, Vector3Add(root, {sinf(a.time + t2) * 0.8f, -7.0f, cosf(a.time * 0.8f + t2) * 0.8f}), 0.06f, 0.01f, 3, Fade(jelly, 0.25f)); }
                if (c.state == AbyssCreatureState::Hunting) for (int k = 0; k < 10; k++) { float u = fmodf(a.time * 1.5f + k * 0.1f, 1.0f); float ta = k * 0.63f; Vector3 far = Vector3Add(c.pos, {cosf(ta) * 14 * (1 - u), sinf(ta * 1.7f) * 8 * (1 - u), sinf(ta) * 14 * (1 - u)}); DrawSphere(far, 0.08f, Fade(WHITE, 0.4f)); } // the water rushing in
                break;
            }
            case AbyssCreatureKind::TrenchMaw: { // the colossal gulper: a jaw wider than the trench, rising out of the black
                Color flesh = Fogged(Color{40, 26, 36, 255}, c.pos);
                DrawCylinderEx(Vector3Add(c.pos, {0, -30, 0}), c.pos, 3.0f, 9.0f, 24, flesh); // its throat and body, below
                for (int k = 0; k < 28; k++) { float ta = k * 2 * PI / 28; Vector3 root = Vector3Add(c.pos, {cosf(ta) * 8.5f, 0, sinf(ta) * 8.5f}); LitCyl(root, Vector3Add(root, {-cosf(ta) * 1.2f, 2.2f + (k % 3) * 0.6f, -sinf(ta) * 1.2f}), 0.25f, 0.02f, 4, Fogged(Color{230, 220, 200, 255}, root)); } // a ring of needle teeth
                LitCyl(Vector3Add(c.pos, {0, -0.3f, 0}), c.pos, 8.4f, 8.4f, 28, Fogged(Color{90, 20, 30, 255}, c.pos)); // the maw's dark red throat
                for (int k = 0; k < 6; k++) { float ta = k * PI / 3 + a.time * 0.3f; DrawSphere(Vector3Add(c.pos, {cosf(ta) * 9.4f, 0.5f, sinf(ta) * 9.4f}), 0.25f, Color{255, 230, 150, 255}); } // photophores round the lip - the only warning you see coming
                break;
            }
            case AbyssCreatureKind::TrenchKrill: { // only lit in your wake
                float d = Vector3Distance(c.pos, a.playerPos);
                if (d < 7.0f) DrawSphere(Vector3Add(c.pos, {sinf(a.time * 2 + c.phase) * 0.3f, fmodf(a.time * 2 + c.phase, 3.0f), 0}), 0.06f, Fade(Color{120, 240, 255, 255}, 1 - d / 7.0f));
                break;
            }
            case AbyssCreatureKind::SlimeHagfish: { Vector3 f2 = Vector3Add(c.pos, {cosf(a.time * 3 + c.phase) * 0.4f, 0, sinf(a.time * 3 + c.phase) * 0.4f}); DrawCylinderEx(c.pos, f2, 0.1f, 0.06f, 5, Fogged(Color{170, 140, 150, 255}, c.pos)); DrawSphere(c.pos, 0.35f, Fade(Color{200, 220, 200, 255}, 0.08f)); break; } // pale eels in a cloud of mucus
            case AbyssCreatureKind::VentWorms: for (int k = 0; k < 7; k++) { Vector3 b2 = Vector3Add(c.pos, {(k % 3 - 1) * 0.3f, 0, (k / 3 - 1) * 0.3f}); DrawCylinderEx(b2, Vector3Add(b2, {0, 1.4f + (k % 2) * 0.5f, 0}), 0.08f, 0.07f, 5, Fogged(Color{230, 225, 210, 255}, b2)); DrawSphere(Vector3Add(b2, {0, 1.5f + (k % 2) * 0.5f, 0}), 0.13f, Fogged(Color{220, 40, 50, 255}, b2)); } break;
            case AbyssCreatureKind::VoidMoss: DrawSphere(c.pos, 1.3f, Color{0, 0, 0, 255}); DrawSphere(c.pos, 1.6f, Fade(BLACK, 0.5f)); break; // a hole in the light itself
            case AbyssCreatureKind::PressureBulb: if (c.stateTimer <= 0) { DrawSphere(c.pos, 0.55f, Fogged(Color{120, 200, 150, 255}, c.pos)); DrawSphere(c.pos, 0.7f, Fade(Color{150, 255, 180, 255}, 0.12f + 0.06f * sinf(a.time * 3))); } else if (c.stateTimer > 11.3f) DrawSphere(c.pos, (12 - c.stateTimer) * 15, Fade(WHITE, (c.stateTimer - 11.3f))); break;
            case AbyssCreatureKind::AbyssalCoral: for (int k = 0; k < 6; k++) { float ta = k * 1.05f; Vector3 tip = Vector3Add(c.pos, {cosf(ta) * 0.7f, 0.8f + (k % 2) * 0.4f, sinf(ta) * 0.7f}); DrawCylinderEx(c.pos, tip, 0.12f, 0.01f, 4, Fogged(c.stateTimer > 0 ? Color{200, 40, 50, 255} : Color{160, 60, 90, 255}, c.pos)); } break;
            case AbyssCreatureKind::GhostKelp: for (int k = 0; k < 4; k++) { Vector3 b2 = Vector3Add(c.pos, {(k - 1.5f) * 0.25f, 0, 0}); DrawCylinderEx(b2, Vector3Add(b2, {sinf(a.time + k) * 0.4f, 2.5f, 0}), 0.06f, 0.03f, 4, Fade(Color{220, 240, 250, 255}, c.stateTimer > 0 ? 0.2f : 0.6f)); } break;
            default: DrawSphere(c.pos, 0.4f, col); break;
        }
    }
    { // depth markers: old survey buoys every hundred metres, and the floor at the bottom
        for (int m = 100; m < (int)ABYSS_DEPTH_SPAN; m += 100) { Vector3 bp = ShaftPos((float)m, 0.3f, 0.6f, a.seed); if (fabsf(bp.y - a.playerPos.y) < 40) { DrawSphere(bp, 0.3f, Fogged(Color{255, 120, 60, 255}, bp)); DrawSphere(bp, 0.6f, Fade(Color{255, 140, 80, 255}, 0.15f + 0.1f * sinf(a.time * 4))); } }
        float floorY = -(ABYSS_DEPTH_SPAN - 8.0f);
        if (a.playerPos.y - floorY < 60) DrawCylinderEx({0, floorY - 1, 0}, {0, floorY, 0}, 24, 24, 32, Fogged(Color{50, 48, 44, 255}, {0, floorY, 0}));
    }
    for (auto& p : a.puffs) {
        float k = 1.0f - p.life / p.maxLife;
        DrawSphere(p.pos, p.radius / 12.0f * k, Fade(Color{140, 255, 220, 255}, 0.35f * k));
    }
    for (auto& m : a.snow) DrawSphere(m.pos, 0.045f, Fade(Color{200, 220, 235, 255}, 0.5f));
    { // the diver: brass helmet with its porthole and lamp, a teal canvas suit, an air tank, kicking legs and fins
        Vector3 P = a.playerPos, V = a.playerVel;
        gLamp = Vector3Add(cam.position, {0, 4.0f, 0}); // the diver is lit from above and behind, the way the camera sees them
        Vector3 up = Vector3Normalize({-V.x * 0.08f, 1.0f, -V.z * 0.08f}); // leans into the way it drifts
        if (a.isGliding) up = Vector3Normalize({-V.x * 0.3f, 0.6f, -V.z * 0.3f});
        Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position)); fwd.y = 0;
        if (Vector3Length(fwd) < 0.01f) fwd = {0, 0, 1};
        fwd = Vector3Normalize(Vector3Subtract(fwd, Vector3Scale(up, Vector3DotProduct(fwd, up))));
        Vector3 right = Vector3CrossProduct(fwd, up);
        auto at = [&](float r, float u, float f) { return Vector3Add(P, Vector3Add(Vector3Scale(right, r), Vector3Add(Vector3Scale(up, u), Vector3Scale(fwd, f)))); };
        Color suit{40, 110, 118, 255}, suitDk{26, 76, 84, 255}, brass{196, 150, 70, 255}, steel{120, 126, 132, 255};
        float kick = sinf(a.time * (a.isGliding ? 4.0f : 7.0f)) * 0.18f;
        LitCyl(at(0, -0.05f, -0.26f), at(0, 0.45f, -0.26f), 0.13f, 0.13f, 10, steel);  // the air tank
        LitCapsule(at(0, -0.25f, 0), at(0, 0.3f, 0), 0.24f, suit);                       // torso
        LitSphere(at(0, 0.05f, 0.05f), 0.2f, suitDk);                                            // weight belt
        for (int sd = -1; sd <= 1; sd += 2) {
            LitCapsule(at(sd * 0.28f, 0.28f, 0), at(sd * 0.42f, -0.05f, 0.12f + sd * kick * 0.3f), 0.08f, suit); // arms
            LitSphere(at(sd * 0.43f, -0.08f, 0.13f), 0.08f, brass);                                                      // gloves
            Vector3 knee = at(sd * 0.12f, -0.55f, sd * kick);
            Vector3 foot = at(sd * 0.13f, -0.95f, -sd * kick * 1.4f);
            LitCapsule(at(sd * 0.11f, -0.25f, 0), knee, 0.1f, suit);
            LitCapsule(knee, foot, 0.09f, suitDk);
            DrawTriangle3D(foot, at(sd * 0.13f, -1.25f, -sd * kick * 1.8f + 0.15f), at(sd * 0.13f, -1.22f, -sd * kick * 1.8f - 0.12f), Color{30, 36, 40, 255}); // fins
            DrawTriangle3D(foot, at(sd * 0.13f, -1.22f, -sd * kick * 1.8f - 0.12f), at(sd * 0.13f, -1.25f, -sd * kick * 1.8f + 0.15f), Color{30, 36, 40, 255});
        }
        Vector3 head = at(0, 0.62f, 0.02f);
        LitSphere(head, 0.3f, brass);                                                 // the brass helmet
        LitCyl(at(0, 0.36f, 0), at(0, 0.44f, 0), 0.3f, 0.3f, 12, Tone(brass, -0.3f)); // its collar
        Vector3 port = at(0, 0.62f, 0.28f);
        DrawSphere(port, 0.15f, Color{20, 40, 46, 255});                               // the porthole
        DrawSphere(Vector3Add(port, Vector3Scale(fwd, 0.04f)), 0.1f, Color{120, 220, 230, 200});
        Vector3 lamp = at(0, 0.9f, 0.1f);
        DrawSphere(lamp, 0.08f, Color{255, 250, 220, 255});                           // the helmet lamp
        Vector3 beamDir = Vector3Normalize(Vector3Add(Vector3Normalize(Vector3Subtract(cam.target, cam.position)), {0, -0.35f, 0}));
        DrawCylinderEx(lamp, Vector3Add(lamp, Vector3Scale(beamDir, 7.0f)), 0.08f, 2.6f, 18, Fade(Color{255, 245, 210, 255}, 0.045f)); // the beam, in the silt
        DrawCylinderEx(lamp, Vector3Add(lamp, Vector3Scale(beamDir, 3.5f)), 0.05f, 1.0f, 12, Fade(Color{255, 245, 210, 255}, 0.05f));
        if (a.isGliding) DrawSphere(P, 0.9f, Fade(Color{180, 255, 240, 255}, 0.06f));
        for (int k = 0; k < 3; k++) { // breath bubbles rising from the helmet
            float t2 = fmodf(a.time * 0.9f + k * 0.33f, 1.0f);
            DrawSphere(Vector3Add(head, {sinf(a.time * 3 + k) * 0.1f, 0.3f + t2 * 1.4f, 0}), 0.04f + 0.02f * k, Fade(Color{200, 240, 255, 255}, 0.5f * (1 - t2)));
        }
    }
    EndMode3D();
    EndLayer(); // back to the main scene canvas

    RenderTexture2D& rt3d = Mode3DRT();
    if (!a.verifying) {
        EnsurePressureShader();
        SetShaderValue(gPressureShader, gPresTimeLoc, &a.time, SHADER_UNIFORM_FLOAT);
        SetShaderValue(gPressureShader, gPresDepthLoc, &a.depth, SHADER_UNIFORM_FLOAT);
        BeginShaderMode(gPressureShader);
    }
    DrawTexturePro(rt3d.texture, {0, 0, (float)rt3d.texture.width, -(float)rt3d.texture.height},
                   {0, 0, (float)SCREEN_W, (float)SCREEN_H}, {0, 0}, 0, WHITE);
    if (!a.verifying) EndShaderMode();

    TxtBold("The Open Abyss", 20, 16, 24, Color{140, 220, 255, 255});
    Txt(TextFormat("Depth: %d m", (int)a.depth), 20, 46, 16, Pal::Paper);
    for (const auto& c : a.creatures) if (c.kind == AbyssCreatureKind::TrenchMaw && c.state != AbyssCreatureState::Shattered && c.state != AbyssCreatureState::Fleeing) { // the one warning the Abyss gives
        float pulse = 0.5f + 0.5f * sinf(a.time * 6);
        TxtBold("Something vast is rising beneath you", 20, 100, 18, Fade(Color{255, 160, 120, 255}, 0.5f + 0.5f * pulse));
        Txt("A pressure-bulb's blast will drive it back. Or climb a vent.", 20, 124, 14, Fade(Pal::Paper, 0.7f));
        break;
    }
    if (a.invisT > 0) Txt("Hidden in the void-moss", 20, 150, 14, Color{150, 150, 200, 255});
    DrawBar({20, 68, 220, 14}, a.stamina / 100.0f, Color{120, 230, 255, 255});
    Txt("Stamina", 20, 84, 12, Fade(Pal::Paper, 0.7f));
    Txt("WASD/arrows drift (double-tap to dash) - Space also dashes - Shift glide (aim up: parachute, down in a downdraft: slipstream) - Mouse: look - Esc: leave", 20, SCREEN_H - 30, 13, Fade(Pal::Paper, 0.6f));

    if (over) {
        Rectangle panel{SCREEN_W / 2.0f - 260, SCREEN_H / 2.0f - 110, 520, 220};
        DrawRectangleRec(panel, Fade(BLACK, 0.72f));
        DrawRectangleLinesEx(panel, 2, a.won ? Color{140, 255, 210, 255} : Color{200, 90, 90, 255});
        const char* title = a.won ? "You surfaced." : "The Abyss claims another diver.";
        DrawTextCenteredBold(title, panel.x + panel.width / 2, panel.y + 46, 26, a.won ? Color{200, 255, 230, 255} : Color{255, 200, 200, 255});
        std::string body = a.won ? TextFormat("Reached the bottom, %d m down.\nPayout: %d gold and a relic.", (int)ABYSS_DEPTH_SPAN, ABYSS_PAYOUT)
                                  : TextFormat("Reached %d m before the stamina ran out.", (int)a.bestDepth);
        DrawWrapped(body, {panel.x + 30, panel.y + 90, panel.width - 60, 70}, 16, Pal::Paper);
        Txt("Space / Enter to return to the Helm", panel.x + panel.width / 2 - 130, panel.y + panel.height - 30, 15, Fade(Pal::Paper, 0.75f));
    }
}

bool VerifyAbyss() {
    Game g;
    g.abyss.verifying = true;
    StartAbyss(g); // preserves verifying=true (see StartAbyss) and skips the GPU trench mesh entirely
    const float dt = 1.0f / 240.0f;
    for (int i = 0; i < 6000 && !g.abyss.dead && !g.abyss.won; i++) {
        // a simple scripted diver: drift toward the shaft centre, hold the hydro-parachute (aim up while
        // gliding) whenever falling fast, and steer away from whatever hazard is nearest - not smart, but
        // enough to prove the descent is survivable with the mechanics as tuned, not just theoretically so.
        Vector2 drift{0, 0};
        float horiz = sqrtf(g.abyss.playerPos.x * g.abyss.playerPos.x + g.abyss.playerPos.z * g.abyss.playerPos.z);
        if (horiz > 0.1f) drift = Vector2Normalize({-g.abyss.playerPos.x, -g.abyss.playerPos.z});
        float bestD = 1e9f; Vector3 threat{};
        for (auto& c : g.abyss.creatures) {
            if (c.state == AbyssCreatureState::Shattered) continue;
            switch (c.kind) {
                case AbyssCreatureKind::GulperEel: case AbyssCreatureKind::VampireSquid: case AbyssCreatureKind::TrenchWorm:
                case AbyssCreatureKind::BrineSlug: case AbyssCreatureKind::Siphonophore: case AbyssCreatureKind::Leviathan:
                case AbyssCreatureKind::AnglerCephalopod: case AbyssCreatureKind::PressureGhost: case AbyssCreatureKind::TrenchMaw: case AbyssCreatureKind::AbyssalCoral: break;
                default: continue;
            }
            float d = Vector3Distance(c.pos, g.abyss.playerPos);
            if (d < bestD) { bestD = d; threat = c.pos; }
        }
        if (bestD < 6.0f) {
            Vector2 away = Vector2Normalize({g.abyss.playerPos.x - threat.x, g.abyss.playerPos.z - threat.z});
            drift = Vector2Normalize(Vector2Add(Vector2Scale(drift, 0.4f), away));
        }
        bool glide = g.abyss.playerVel.y < -6.0f;
        StepAbyss(g, dt, drift, false, glide, glide, false);
        if (std::isnan(g.abyss.playerPos.x) || std::isnan(g.abyss.playerPos.y)) return false;
    }
    bool descent = g.abyss.won || (!g.abyss.dead && g.abyss.depth > 50.0f); // didn't finish the slice's depth, but didn't stall at the top or die either
    if (!descent) return false;
    // the 1.3 roster: a pressure-bulb's blast drives the Trench-Maw back; dashing through void-moss hides you; ghost-kelp refills you
    {
        Game h; h.abyss.verifying = true; StartAbyss(h);
        AbyssState& a = h.abyss;
        a.creatures.clear();
        a.playerPos = {0, -300, 0}; a.playerVel = {0, 0, 0};
        AbyssCreature maw; maw.kind = AbyssCreatureKind::TrenchMaw; maw.state = AbyssCreatureState::Hunting; maw.pos = {0, -312, 0}; a.creatures.push_back(maw);
        AbyssCreature bulb; bulb.kind = AbyssCreatureKind::PressureBulb; bulb.pos = {0.5f, -300, 0}; a.creatures.push_back(bulb);
        AbyssCreature moss; moss.kind = AbyssCreatureKind::VoidMoss; moss.pos = {0, -300, 0.5f}; a.creatures.push_back(moss);
        AbyssCreature kelp; kelp.kind = AbyssCreatureKind::GhostKelp; kelp.pos = {0.4f, -300, 0.4f}; a.creatures.push_back(kelp);
        a.isDashing = true; a.dashTimer = 0.3f; a.stamina = 40;
        StepAbyss(h, dt, {0, 0}, false, false, false, false);
        if (a.creatures[0].state != AbyssCreatureState::Dislodged) { TraceLog(LOG_WARNING, "verify-abyss: FAILED - a pressure-bulb didn't drive the Trench-Maw back"); return false; }
        if (a.invisT <= 0) { TraceLog(LOG_WARNING, "verify-abyss: FAILED - dashing through void-moss didn't hide the diver"); return false; }
        if (a.stamina < 60) { TraceLog(LOG_WARNING, "verify-abyss: FAILED - ghost-kelp didn't refill the diver"); return false; }
    }
    return true;
}
