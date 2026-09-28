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

constexpr float DEPTH_SPAN = 900.0f;      // how far down the vertical slice's trench is generated
constexpr int RING_SEGMENTS = 18;
constexpr float RING_STEP = 6.0f;

// The trench wall is a GPU mesh built once per seed (never stored in Game/AbyssState - those get plain-
// data-copied around by debug tooling, and a raylib Model holds live GPU handles that must not be duplicated).
Model gTrenchModel{};
unsigned gTrenchSeed = 0;
bool gTrenchReady = false;

void BuildTrenchMesh(unsigned seed) {
    if (gTrenchReady) UnloadModel(gTrenchModel);
    int rings = (int)(DEPTH_SPAN / RING_STEP) + 1;
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

void PopulateEcosystem(AbyssState& a) {
    a.creatures.clear();
    int idx = 0;
    // Glass Sponge ledges: the only safe footholds, spaced down the shaft at varying angles.
    for (float d = 40; d < DEPTH_SPAN - 30; d += 55 + Hash1((float)d) * 25) {
        AbyssCreature c;
        c.kind = AbyssCreatureKind::GlassSponge;
        c.personality = RollPersonality(a.seed, idx++);
        c.wallAngle = Hash2(d, a.seed * 2.0f) * 2 * PI;
        c.depth = d;
        c.radiusOffset = 1.4f; // reaches inward from the wall, so its top is standable
        c.pos = ShaftPos(d, c.wallAngle, c.radiusOffset, a.seed);
        c.health = 1.0f;
        c.state = AbyssCreatureState::Idle;
        a.creatures.push_back(c);
    }
    // Giant Isopods: cling to the wall until a downdraft or a nearby dash dislodges them.
    for (float d = 70; d < DEPTH_SPAN - 30; d += 70 + Hash1((float)d + 900) * 40) {
        AbyssCreature c;
        c.kind = AbyssCreatureKind::GiantIsopod;
        c.personality = RollPersonality(a.seed, idx++);
        c.wallAngle = Hash2(d + 500, a.seed * 3.0f) * 2 * PI;
        c.depth = d;
        c.radiusOffset = 0.6f;
        c.pos = ShaftPos(d, c.wallAngle, c.radiusOffset, a.seed);
        c.state = AbyssCreatureState::Cling;
        a.creatures.push_back(c);
    }
    // A single Gulper Eel lurking in the dark for this slice - it lunges toward acoustic disturbances.
    for (float d = 260; d < DEPTH_SPAN - 30; d += 320) {
        AbyssCreature c;
        c.kind = AbyssCreatureKind::GulperEel;
        c.personality = RollPersonality(a.seed, idx++);
        c.wallAngle = Hash2(d + 1500, a.seed * 4.0f) * 2 * PI;
        c.depth = d;
        c.radiusOffset = 3.5f; // sits out in open water, not against the wall
        c.pos = ShaftPos(d, c.wallAngle, c.radiusOffset, a.seed);
        c.state = AbyssCreatureState::Idle;
        a.creatures.push_back(c);
    }
}

// A dash or a shattering sponge disturbs the Bioluminescent Plankton, briefly lighting the trench and
// drawing a curious Gulper Eel's attention to the disturbance's origin.
void Disturb(AbyssState& a, Vector3 at, float radius) {
    a.puffs.push_back({at, 0, 1.3f, radius});
    for (auto& c : a.creatures) {
        if (c.kind == AbyssCreatureKind::GulperEel && c.state != AbyssCreatureState::Shattered) {
            float d = Vector3Distance(c.pos, at);
            if (d < 60.0f && c.personality.curiosity > 0.35f) { c.state = AbyssCreatureState::Hunting; c.vel = Vector3Scale(Vector3Normalize(Vector3Subtract(at, c.pos)), 6.0f); }
        }
        if (c.kind == AbyssCreatureKind::GiantIsopod && c.state == AbyssCreatureState::Cling) {
            float d = Vector3Distance(c.pos, at);
            if (d < radius) c.state = AbyssCreatureState::Dislodged;
        }
    }
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

    // ---- physics integration ----
    float gravity = 9.0f;
    Vector3 accel{drift.x * 10.0f, -gravity, 0};
    if (inDowndraft) accel.y -= 14.0f;
    a.playerVel = Vector3Add(a.playerVel, Vector3Scale(accel, dt));
    float drag = 1.8f; // ambient fluid drag
    if (a.isDashing) drag *= 0.15f;
    else if (a.glidingUp) drag *= 6.0f;             // hydro-parachute: sheds speed fast
    else if (a.glidingDown && inDowndraft) drag *= 0.05f; // slipstream: nearly frictionless with the current
    else if (a.isGliding) drag *= 0.35f;             // streamlined glide: cuts drag well below ambient
    float damp = 1.0f / (1.0f + drag * dt);
    a.playerVel = Vector3Scale(a.playerVel, damp);
    a.playerPos = Vector3Add(a.playerPos, Vector3Scale(a.playerVel, dt));

    if (!a.isDashing && !a.isGliding) a.stamina = std::min(100.0f, a.stamina + dt * 6.0f);

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
                if (c.state == AbyssCreatureState::Dislodged) {
                    c.vel.y -= gravity * dt * (0.6f + c.personality.energy * 0.8f);
                    c.pos = Vector3Add(c.pos, Vector3Scale(c.vel, dt));
                    // smashes any Glass Sponge it falls through
                    for (auto& s : a.creatures) if (s.kind == AbyssCreatureKind::GlassSponge && s.state != AbyssCreatureState::Shattered)
                        if (Vector3Distance(s.pos, c.pos) < 2.2f) { s.state = AbyssCreatureState::Shattered; Disturb(a, s.pos, 8.0f); }
                    if (Vector3Distance(c.pos, a.playerPos) < 1.4f) {
                        a.playerVel = Vector3Add(a.playerVel, Vector3Scale(Vector3Normalize(Vector3Subtract(a.playerPos, c.pos)), 8.0f));
                        c.state = AbyssCreatureState::Shattered;
                    }
                }
                break;
            case AbyssCreatureKind::GulperEel:
                if (c.state == AbyssCreatureState::Hunting) {
                    Vector3 toPlayer = Vector3Subtract(a.playerPos, c.pos);
                    if (Vector3Length(toPlayer) > 0.5f) c.vel = Vector3Scale(Vector3Normalize(toPlayer), 5.0f + c.personality.aggression * 4.0f);
                    c.pos = Vector3Add(c.pos, Vector3Scale(c.vel, dt));
                    c.stateTimer += dt;
                    if (c.stateTimer > 2.2f) { c.state = AbyssCreatureState::Idle; c.stateTimer = 0; c.vel = {0, 0, 0}; }
                }
                break;
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

    a.depth = depth;
    a.bestDepth = std::max(a.bestDepth, a.depth);
    if (a.depth > DEPTH_SPAN - 10) a.depth = DEPTH_SPAN - 10; // the vertical slice's floor, until the generator streams further
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
        default: return WHITE;
    }
}

void SceneAbyss(Game& g) {
    AbyssState& a = g.abyss;
    if (!a.verifying) {
        if (IsKeyPressed(KEY_ESCAPE)) { g.scene = Scene::Helm; return; }
        UpdateAbyss(g, GetFrameTime());
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

    for (auto& c : a.creatures) {
        if (c.state == AbyssCreatureState::Shattered) continue;
        Color col = KindColor(c.kind);
        switch (c.kind) {
            case AbyssCreatureKind::GlassSponge: DrawCylinderEx(c.pos, Vector3Add(c.pos, {0, 0.6f, 0}), 1.5f, 0.3f, 6, Fade(col, 0.55f)); break;
            case AbyssCreatureKind::GiantIsopod:  DrawCapsule(c.pos, Vector3Add(c.pos, {0.7f, 0, 0}), 0.35f, 6, 4, col); break;
            case AbyssCreatureKind::GulperEel:    DrawCapsule(c.pos, Vector3Add(c.pos, {1.6f, 0, 0}), 0.4f, 6, 4, col); break;
            default: DrawSphere(c.pos, 0.4f, col); break;
        }
    }
    for (auto& p : a.puffs) {
        float k = 1.0f - p.life / p.maxLife;
        DrawSphere(p.pos, p.radius / 12.0f * k, Fade(Color{140, 255, 220, 255}, 0.35f * k));
    }
    Color playerCol = a.isGliding ? Color{200, 240, 255, 255} : Color{230, 200, 160, 255};
    DrawCapsule(Vector3Add(a.playerPos, {0, 0.5f, 0}), Vector3Add(a.playerPos, {0, -0.5f, 0}), 0.4f, 8, 4, playerCol);
    DrawSphere(a.playerPos, 0.55f, Fade(Color{180, 255, 240, 255}, 0.18f)); // the player's own weak bioluminescent glow
    EndMode3D();
    EndLayer(); // back to the main scene canvas

    RenderTexture2D& rt3d = Mode3DRT();
    DrawTexturePro(rt3d.texture, {0, 0, (float)rt3d.texture.width, -(float)rt3d.texture.height},
                   {0, 0, (float)SCREEN_W, (float)SCREEN_H}, {0, 0}, 0, WHITE);

    TxtBold("The Open Abyss", 20, 16, 24, Color{140, 220, 255, 255});
    Txt(TextFormat("Depth: %d m", (int)a.depth), 20, 46, 16, Pal::Paper);
    DrawBar({20, 68, 220, 14}, a.stamina / 100.0f, Color{120, 230, 255, 255});
    Txt("Stamina", 20, 84, 12, Fade(Pal::Paper, 0.7f));
    Txt("WASD/arrows drift - Space dash - Shift glide (aim up: parachute, down in a downdraft: slipstream) - Esc: leave", 20, SCREEN_H - 30, 13, Fade(Pal::Paper, 0.6f));
}

bool VerifyAbyss() {
    Game g;
    g.abyss.verifying = true;
    StartAbyss(g); // preserves verifying=true (see StartAbyss) and skips the GPU trench mesh entirely
    const float dt = 1.0f / 240.0f;
    for (int i = 0; i < 6000 && !g.abyss.dead; i++) {
        // a simple scripted diver: drift toward the shaft centre, and hold the hydro-parachute (aim up while
        // gliding) whenever falling fast, so it actually has to use the mechanic to survive the descent.
        Vector2 drift{0, 0};
        float horiz = sqrtf(g.abyss.playerPos.x * g.abyss.playerPos.x + g.abyss.playerPos.z * g.abyss.playerPos.z);
        if (horiz > 0.1f) drift = Vector2Normalize({-g.abyss.playerPos.x, -g.abyss.playerPos.z});
        bool glide = g.abyss.playerVel.y < -6.0f;
        StepAbyss(g, dt, drift, false, glide, glide, false);
        if (std::isnan(g.abyss.playerPos.x) || std::isnan(g.abyss.playerPos.y)) return false;
        if (g.abyss.depth >= DEPTH_SPAN - 15) return true;
    }
    return g.abyss.depth > 50.0f; // didn't finish the slice's depth, but didn't stall at the top either
}
