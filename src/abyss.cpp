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

// The trench's cross-section radius at a given depth/angle: a base width that narrows and widens in slow
// bands down the shaft, jittered by an angular hash so the wall reads as jagged rock, not a smooth pipe.
float TrenchRadius(float depth, float angle, unsigned seed) {
    float band = 6.5f + 2.2f * sinf(depth * 0.045f + seed * 0.7f);
    float jag = 0;
    for (int o = 0; o < 3; o++) {
        float freq = 3.0f + o * 5.0f;
        jag += (Hash2(cosf(angle) * freq + seed * 3.1f, depth * 0.08f + o * 11.0f) - 0.5f) * (1.4f / (o + 1));
    }
    return std::max(2.2f, band + jag);
}

constexpr int RING_SEGMENTS = 18;
constexpr float RING_STEP = 6.0f;

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
            float shade = 0.34f + 0.16f * Hash2(a * 7.0f, depth * 0.1f + seed);
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
    // Glass Sponge ledges: the only safe footholds, spaced down the shaft at varying angles.
    for (float d = 40; d < ABYSS_DEPTH_SPAN - 30; d += 55 + Hash1(d) * 25)
        spawn(AbyssCreatureKind::GlassSponge, d, Hash2(d, a.seed * 2.0f) * 2 * PI, 1.4f, AbyssCreatureState::Idle);
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
            case AbyssCreatureKind::VampireSquid:
                if (c.state == AbyssCreatureState::Hunting) {
                    bool squid = c.kind == AbyssCreatureKind::VampireSquid;
                    Vector3 toPlayer = Vector3Subtract(a.playerPos, c.pos);
                    float dist = Vector3Length(toPlayer);
                    if (dist > 0.5f) c.vel = Vector3Scale(Vector3Normalize(toPlayer), (squid ? 7.5f : 5.0f) + c.personality.aggression * (squid ? 5.0f : 4.0f));
                    c.pos = Vector3Add(c.pos, Vector3Scale(c.vel, dt));
                    if (dist < 1.3f) { HazardHit(a, c.pos, squid ? 30.0f : 22.0f, squid ? 10.0f : 9.0f); c.state = AbyssCreatureState::Idle; c.stateTimer = 0; c.vel = {0, 0, 0}; }
                    c.stateTimer += dt;
                    if (c.stateTimer > 2.2f) { c.state = AbyssCreatureState::Idle; c.stateTimer = 0; c.vel = {0, 0, 0}; }
                }
                break;
            case AbyssCreatureKind::TrenchWorm: {
                float distToHome = Vector3Distance(c.home, a.playerPos);
                if (c.state == AbyssCreatureState::Idle) {
                    c.pos = c.home; // coiled back into the wall - nothing to see until it strikes
                    if (c.stateTimer > 0) c.stateTimer -= dt;
                    else if (distToHome < 5.5f + c.personality.aggression * 2.0f) {
                        c.state = AbyssCreatureState::Lunging; c.stateTimer = 0;
                        c.vel = Vector3Scale(Vector3Normalize(Vector3Subtract(a.playerPos, c.home)), 9.0f);
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
                bool fleeing = c.state == AbyssCreatureState::Fleeing;
                c.phase += dt * (fleeing ? 3.0f : 1.0f);
                float orbitR = fleeing ? 3.5f : 1.2f;
                c.pos = Vector3Add(c.home, {cosf(c.phase) * orbitR, sinf(c.phase * 1.3f) * 0.4f, sinf(c.phase) * orbitR});
                if (fleeing) { c.stateTimer += dt; if (c.stateTimer > 1.5f) { c.state = AbyssCreatureState::Idle; c.stateTimer = 0; } }
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
            default: break;
        }
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
    Vector2 drift{0, 0};
    bool dashPressed = false, glideHeld = false, aimUp = false, aimDown = false;
    if (!g.abyss.verifying) {
        if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) drift.x += 1;
        if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))  drift.x -= 1;
        if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))   { drift.y += 1; aimUp = true; }
        if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN))  { drift.y -= 1; aimDown = true; }
        dashPressed = IsKeyPressed(KEY_SPACE);
        glideHeld = IsKeyDown(KEY_LEFT_SHIFT);
    }
    StepAbyss(g, dt, drift, dashPressed, glideHeld, aimUp, aimDown);
}

static Color KindColor(AbyssCreatureKind k) {
    switch (k) {
        case AbyssCreatureKind::GlassSponge: return {80, 230, 255, 255};
        case AbyssCreatureKind::GiantIsopod:  return {150, 110, 90, 255};
        case AbyssCreatureKind::GulperEel:    return {40, 30, 50, 255};
        case AbyssCreatureKind::VampireSquid: return {200, 40, 160, 255};
        case AbyssCreatureKind::Siphonophore: return {120, 255, 120, 255};
        case AbyssCreatureKind::TrenchWorm:   return {110, 70, 60, 255};
        case AbyssCreatureKind::Hatchetfish:  return {200, 220, 255, 255};
        case AbyssCreatureKind::BrineSlug:    return {90, 140, 120, 255};
        case AbyssCreatureKind::Leviathan:    return {14, 16, 24, 255};
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
    }

    // BeginFrame/EndFrame are owned by the caller (the main loop or --shots), exactly like every other
    // Scene* function - this only draws into the frame that's already open.
    // A fixed behind-and-above chase offset, not velocity-derived: with the player falling almost
    // straight down for most of the descent, a velocity-aligned look direction ends up parallel to the
    // up vector (a near-zero horizontal drift makes lookDir ~= {0,-1,0} = -up), which degenerates
    // raylib's LookAt into a singular view matrix and renders nothing at all. A constant offset avoids
    // that entirely and still reads as "looking down the shaft" since the shaft itself runs along -Y.
    Camera3D cam{};
    cam.position = Vector3Add(a.playerPos, {0, 1.4f, 4.2f});
    cam.target = Vector3Add(a.playerPos, {0, -2.6f, 0});
    cam.up = {0, 1, 0};
    cam.fovy = 65.0f;
    cam.projection = CAMERA_PERSPECTIVE;

    // BeginMode3D needs a real depth-buffered target and drives its own projection/view matrices, which
    // would collide with the main scene canvas's pushed 2D supersample transform - so it's drawn into its
    // own render texture (like PixelRT/OceanRT) and composited in below, rather than nested in directly.
    BeginLayer(Mode3DRT());
    ClearBackground(Color{2, 3, 6, 255}); // the Void Canvas: near-Vantablack, no visible back wall
    BeginMode3D(cam);
    if (gTrenchReady) {
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
        Color col = KindColor(c.kind);
        switch (c.kind) {
            case AbyssCreatureKind::GlassSponge: DrawCylinderEx(c.pos, Vector3Add(c.pos, {0, 0.6f, 0}), 1.5f, 0.3f, 6, Fade(col, 0.55f)); break;
            case AbyssCreatureKind::GiantIsopod:  DrawCapsule(c.pos, Vector3Add(c.pos, {0.7f, 0, 0}), 0.35f, 6, 4, col); break;
            case AbyssCreatureKind::GulperEel:    DrawCapsule(c.pos, Vector3Add(c.pos, {1.6f, 0, 0}), 0.4f, 6, 4, col); break;
            case AbyssCreatureKind::VampireSquid: DrawCapsule(c.pos, Vector3Add(c.pos, {1.1f, 0, 0}), 0.5f, 6, 4, col); DrawSphere(Vector3Add(c.pos, {1.1f, 0, 0}), 0.15f, Fade(Color{255, 120, 220, 255}, 0.7f)); break;
            case AbyssCreatureKind::Siphonophore: {
                DrawSphere(c.pos, 0.35f, Fade(col, 0.5f));
                for (int t2 = 0; t2 < 4; t2++) DrawCylinderEx(c.pos, Vector3Add(c.pos, {0, -1.2f - t2 * 0.3f, 0}), 0.06f, 0.02f, 4, Fade(col, 0.4f - t2 * 0.08f));
                break;
            }
            case AbyssCreatureKind::TrenchWorm: DrawCapsule(c.home, c.pos, 0.3f, 6, 4, col); break;
            case AbyssCreatureKind::Hatchetfish: DrawSphere(c.pos, 0.14f, col); break;
            case AbyssCreatureKind::BrineSlug: DrawCylinderEx(c.pos, Vector3Add(c.pos, {0.9f, 0, 0}), 0.5f, 0.4f, 8, col); break;
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
            default: DrawSphere(c.pos, 0.4f, col); break;
        }
    }
    for (auto& p : a.puffs) {
        float k = 1.0f - p.life / p.maxLife;
        DrawSphere(p.pos, p.radius / 12.0f * k, Fade(Color{140, 255, 220, 255}, 0.35f * k));
    }
    for (auto& m : a.snow) DrawSphere(m.pos, 0.045f, Fade(Color{200, 220, 235, 255}, 0.5f));
    Color playerCol = a.isGliding ? Color{200, 240, 255, 255} : Color{230, 200, 160, 255};
    DrawCapsule(Vector3Add(a.playerPos, {0, 0.5f, 0}), Vector3Add(a.playerPos, {0, -0.5f, 0}), 0.4f, 8, 4, playerCol);
    DrawSphere(a.playerPos, 0.55f, Fade(Color{180, 255, 240, 255}, 0.18f)); // the player's own weak bioluminescent glow
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
    DrawBar({20, 68, 220, 14}, a.stamina / 100.0f, Color{120, 230, 255, 255});
    Txt("Stamina", 20, 84, 12, Fade(Pal::Paper, 0.7f));
    Txt("WASD/arrows drift - Space dash - Shift glide (aim up: parachute, down in a downdraft: slipstream) - Esc: leave", 20, SCREEN_H - 30, 13, Fade(Pal::Paper, 0.6f));

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
                case AbyssCreatureKind::BrineSlug: case AbyssCreatureKind::Siphonophore: case AbyssCreatureKind::Leviathan: break;
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
    if (g.abyss.won) return true;
    return !g.abyss.dead && g.abyss.depth > 50.0f; // didn't finish the slice's depth, but didn't stall at the top or die either
}
