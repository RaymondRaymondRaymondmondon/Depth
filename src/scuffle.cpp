// Scuffle's headless core (see scuffle.h), stage 1: the stage, the particle physics, the active-ragdoll stick (run, jump,
// wall climb and wall jump, duck, dive, prone, ragdoll and get-up), fists (the jab, the haymaker every third, the
// kick, the grab and the throw), damage, death and falling out, the fists-only bot, and the checks.
#include "scuffle.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>

namespace sf {

// ---------------------------------------------------------------- stages
Stage StoneStage() {
    // one stone stage: a floor with a gap in the middle, two raised ledges, a high shelf, a wall to climb (32 x 18)
    return StageFromText({
        "................................",
        "................................",
        "................................",
        "................................",
        "................................",
        "...........##########...........",
        "................................",
        "................................",
        "....######..............######..",
        "................................",
        "................................",
        "..............S....S............",
        "...........##########...........",
        "#...........................####",
        "#..S........................S..#",
        "#...........................####",
        "#########...############...#####",
        "#########...############...#####",
    }, "The Stone Yard");
}

// ---------------------------------------------------------------- small things
static float Sgn(float v) { return v > 0 ? 1.0f : v < 0 ? -1.0f : 0.0f; }
static Vector2 V(float x, float y) { return {x, y}; }
float World::Rand() { rng = rng * 1664525u + 1013904223u; return ((rng >> 8) & 0xffffff) / 16777216.0f; }
int World::Living() const { int n = 0; for (const auto& k : sticks) n += k.present && k.alive; return n; }
void World::Emit(int kind, Vector2 at, int who, int by, float a) { Event e; e.kind = kind; e.at = at; e.who = who; e.by = by; e.a = a; events.push_back(e); if (events.size() > 256) { events.erase(events.begin(), events.begin() + 128); eventBase += 128; } }
bool World::BoxHits(float x0, float y0, float x1, float y1) const {
    int ix0 = (int)floorf(x0 / TILE), ix1 = (int)floorf((x1 - 1e-4f) / TILE), iy0 = (int)floorf(y0 / TILE), iy1 = (int)floorf((y1 - 1e-4f) / TILE);
    for (int y = iy0; y <= iy1; y++) for (int x = ix0; x <= ix1; x++) if (stage.Solid(x, y)) return true;
    return MoverHits(x0, y0, x1, y1);
}

// ---------------------------------------------------------------- the pose: where each joint wants to be (feet-relative)
static Vector2 PoseOffset1(const Stick& k, int j, float t);
Vector2 PoseOffset(const Stick& k, int j, float t) { Vector2 o = PoseOffset1(k, j, t); return k.size == 1 ? o : Vector2Scale(o, k.size); }   // (Giants and Tiny: the same pose, scaled)
static Vector2 PoseOffset1(const Stick& k, int j, float t) {
    float f = (float)k.face, h = k.height;
    float run = std::clamp(fabsf(k.vel.x) / 8.0f, 0.0f, 1.0f), lean = 0.14f * run * Sgn(k.vel.x);
    float br = 0.012f * sinf(k.breathe * 2.2f);
    Vector2 neck{lean, 1.33f + br}, pelvis{lean * 0.3f, 0.86f};
    if (k.st == S_DUCK) { neck = {0.16f * f, 0.66f}; pelvis = {-0.05f * f, 0.42f}; }
    if (k.st == S_DIVE || k.st == S_PRONE) { neck = {0.5f * f, 0.24f}; pelvis = {-0.05f * f, 0.2f}; }
    if (k.st == S_WALL) { neck = {k.wallSide * 0.06f, 1.36f}; pelvis = {-k.wallSide * 0.04f, 0.88f}; }
    if (k.tauntT > 0) neck.y += 0.05f * sinf(t * 18);
    Vector2 up = Vector2Normalize(Vector2Subtract(neck, pelvis));
    switch (j) {
        case J_HEAD: return Vector2Add(neck, Vector2Scale(up, 0.22f));
        case J_NECK: return neck;
        case J_PELVIS: return pelvis;
        default: break;
    }
    bool arm = j >= J_ELBOW_L && j <= J_HAND_R, hand = j == J_HAND_L || j == J_HAND_R;
    bool lead = (j == J_ELBOW_R || j == J_HAND_R);   // (the right arm leads: it aims and punches)
    if (arm) {
        Vector2 sh = Vector2Add(neck, Vector2Scale(up, -0.04f));
        Vector2 aim = Vector2Normalize(k.in.aim); if (Vector2Length(k.in.aim) < 0.1f) aim = V(f, 0);
        if (k.st == S_WALL) { Vector2 h2 = Vector2Add(sh, V(k.wallSide * 0.26f, lead ? 0.36f : 0.24f)); return hand ? h2 : Vector2Lerp(sh, h2, 0.5f); }
        if (lead) {
            float ext = 0.36f;                                                   // (the guard: the fist half out)
            if (k.punchT > 0) { float u = 1 - k.punchT / (k.punchHay ? 0.35f : 0.25f); ext = u < 0.55f ? 0.36f + 0.26f * (u / 0.55f) : 0.62f - 0.26f * ((u - 0.55f) / 0.45f); }
            if (k.grabbing >= 0) ext = 0.55f;
            Vector2 hp = Vector2Add(sh, Vector2Scale(aim, ext));
            if (hand) return hp;
            Vector2 mid = Vector2Lerp(sh, hp, 0.5f); Vector2 perp{-aim.y, aim.x}; if (perp.y > 0) perp = Vector2Negate(perp);
            return Vector2Add(mid, Vector2Scale(perp, std::max(0.0f, 0.3f - ext * 0.45f)));
        }
        // the off arm: the guard up by the chin when fighting, swinging when running
        float sw = sinf(k.walkPh) * 0.25f * run;
        Vector2 hp = Vector2Add(sh, V(0.16f * f + sw, -0.05f));
        if (k.st == S_AIR) hp = Vector2Add(sh, V(-0.25f * f, 0.2f));
        if (k.tauntT > 0) hp = Vector2Add(sh, V(0.1f * f, 0.45f + 0.1f * sinf(t * 14)));
        if (hand) return hp;
        return Vector2Add(Vector2Lerp(sh, hp, 0.5f), V(-0.1f * f, -0.12f));
    }
    // the legs: planted, a running stride, tucked in the air, pressed to the wall, crouched, stretched out when prone
    bool left = j == J_KNEE_L || j == J_FOOT_L, foot = j == J_FOOT_L || j == J_FOOT_R;
    float side = left ? -1.0f : 1.0f;
    Vector2 ft{side * 0.13f, 0.02f};
    if (k.st == S_STAND && run > 0.05f) { float ph = k.walkPh + (left ? PI : 0); ft = V(sinf(ph) * 0.38f * f * run + side * 0.04f, std::max(0.0f, cosf(ph)) * 0.22f * run + 0.02f); }
    if (k.st == S_AIR) ft = V(side * 0.12f + 0.05f * f, 0.28f + (left ? 0.06f : 0));
    if (k.st == S_WALL) ft = V(k.wallSide * 0.22f, left ? 0.15f : 0.45f);
    if (k.st == S_DUCK) ft = V(side * 0.22f, 0.02f);
    if (k.st == S_DIVE || k.st == S_PRONE) ft = V(-0.75f * f + side * 0.05f, 0.12f);
    if (k.kickT > 0) ft = left ? V(0.85f * f, 0.25f) : V(0.7f * f, 0.15f);
    (void)h;
    if (foot) return ft;
    Vector2 mid = Vector2Lerp(pelvis, ft, 0.5f);
    float bend = (k.st == S_DUCK ? 0.22f : k.st == S_AIR ? 0.18f : 0.06f + 0.05f * run);
    return Vector2Add(mid, V(bend * f, 0));
}

// ---------------------------------------------------------------- water, brine and low gravity (stage 5)
bool World::InLiquid(Vector2 p) const {
    int x = (int)floorf(p.x / TILE), y = (int)floorf(p.y / TILE);
    if (stage.Liquid(x, y)) return true;
    if ((stage.world == WD_REEF || stage.world == WD_ATLANTIS) && p.y < wallY) return true;   // (the tide; the sinking city)
    if (p.y < floodY) return true;                                                          // (the Flood event)
    for (const auto& q : stage.pieces) if (q.kind == PK_SLUICE && q.prog > 0 && x >= q.x && x < q.x + q.w && y >= q.y && y < q.y + q.h) return true;
    return false;
}
bool World::BrineAt(Vector2 p) const { return stage.At((int)floorf(p.x / TILE), (int)floorf(p.y / TILE)) == T_BRINE; }
float World::GravityAt(Vector2 p) const {
    int x = (int)floorf(p.x / TILE), y = (int)floorf(p.y / TILE);
    for (const auto& q : stage.pieces) if (q.kind == PK_LOWG && t >= q.start && x >= q.x && x < q.x + q.w && y >= q.y && y < q.y + q.h) return std::clamp(q.power, 0.05f, 1.0f);
    return 1;
}

// ---------------------------------------------------------------- spawning
void World::Init(const Stage& s, int players, uint32_t seed) {
    stage = s; rng = (seed ? seed : 1) * 2654435761u + 0x6D2B79F5u; for (int i = 0; i < 3; i++) Rand();   // (mixed: neighbouring seeds draw unlike numbers)
    frame = 0; t = 0; events.clear(); eventBase = 0;
    items.clear(); bullets.clear(); nextCrate = Arms().crateFirst; wallY = -10; crates = 0;
    ceilY = 1e9f; sideX = -10; glassT.clear();
    // the Void's abyss is on the side with more bottomless columns; the Salon's bouncer comes in from the door (the left)
    { int l = 0, r = 0; for (int x = 0; x < stage.w / 3; x++) { bool bl = true, br = true; for (int y = 0; y < stage.h; y++) { bl &= !stage.Solid(x, y); br &= !stage.Solid(stage.w - 1 - x, y); } l += bl; r += br; }
      wallSide = stage.world == WD_SALON ? -1 : l > r ? -1 : 1; }
    sticks.assign(std::clamp(players, 1, MAX_STICKS), Stick{});
    for (int i = 0; i < (int)sticks.size(); i++) {
        sticks[i].id = i;
        // spread out: the spawns sorted left to right, the players picked evenly across them
        std::vector<Vector2> sp = stage.spawns; std::sort(sp.begin(), sp.end(), [](const Vector2& a, const Vector2& b) { return a.x < b.x; });
        int n = (int)sticks.size(), m = (int)sp.size();
        Vector2 at = sp.empty() ? V(stage.Width() * (i + 1) / (n + 1), stage.Height() * 0.6f) : sp[n <= 1 ? 0 : std::min(m - 1, (int)lroundf(i * (m - 1) / (float)(n - 1)) % std::max(1, m))];
        if (n > m && !sp.empty()) at.x += 0.45f * (i % 2 ? 1 : -1) * (i / m);
        SpawnStick(sticks[i], at, at.x < stage.Width() / 2 ? 1 : -1);   // (facing inward)
    }
}
void World::SpawnStick(Stick& k, Vector2 feet, int face) {
    int id = k.id; k = Stick{}; k.id = id; k.pos = feet; k.face = face; k.st = S_AIR; k.fallTop = feet.y;
    static const float R[J_COUNT] = {0.16f, 0.07f, 0.09f, 0.06f, 0.07f, 0.06f, 0.07f, 0.06f, 0.07f, 0.06f, 0.07f};
    for (int j = 0; j < J_COUNT; j++) { Vector2 p = Vector2Add(feet, PoseOffset(k, j, 0)); k.pt[j].p = k.pt[j].q = p; k.pt[j].r = R[j]; k.pt[j].invMass = j == J_PELVIS || j == J_NECK ? 0.5f : 1.0f; }
}
static const int BONES[][2] = {{J_HEAD, J_NECK}, {J_NECK, J_PELVIS}, {J_NECK, J_ELBOW_L}, {J_ELBOW_L, J_HAND_L}, {J_NECK, J_ELBOW_R}, {J_ELBOW_R, J_HAND_R},
                               {J_PELVIS, J_KNEE_L}, {J_KNEE_L, J_FOOT_L}, {J_PELVIS, J_KNEE_R}, {J_KNEE_R, J_FOOT_R}, {J_HEAD, J_PELVIS}};
static const float BONE_LEN[] = {0.22f, 0.47f, 0.31f, 0.30f, 0.31f, 0.30f, 0.43f, 0.43f, 0.43f, 0.43f, 0.69f};
constexpr int NBONES = 11;

// ---------------------------------------------------------------- the controller (the platformer half)
static bool Overlaps(const World& w, const Stick& k, Vector2 p, float hgt) { return w.BoxHits(p.x - k.halfW, p.y, p.x + k.halfW, p.y + hgt); }
void World::StepController(Stick& k) {
    const float dt = STEP;
    if (!k.alive) return;
    Input& in = k.in;
    bool jumpPress = in.jump && !k.jumpWas;
    k.breathe += dt;
    // the ragdolled stick: the controller follows the pelvis; getting up takes 0.4 s once the body is still
    if (k.st == S_RAGDOLL) {
        Vector2 pv = Vector2Scale(Vector2Subtract(k.pt[J_PELVIS].p, k.pt[J_PELVIS].q), 1 / STEP);
        k.pos = {k.pt[J_PELVIS].p.x, k.pt[J_PELVIS].p.y - 0.5f}; k.vel = pv;
        k.ragT -= dt;
        bool wants = fabsf(in.moveX) > 0.2f || in.jump || in.fire || in.moveY > 0.5f;
        if (k.ragT <= 0 && k.grabbedBy < 0 && (wants || k.ragT < -1.2f) && Vector2Length(pv) < 1.6f) {
            // up: find the floor under the pelvis, stand there
            Vector2 p{k.pt[J_PELVIS].p.x, std::max(0.0f, k.pt[J_PELVIS].p.y - 0.9f)};
            for (int i = 0; i < 40 && Overlaps(*this, k, p, k.height); i++) p.y += 0.05f;
            k.pos = p; k.vel = {0, 0}; k.st = S_STAND; k.getUpT = k.trinket == TK_THICK_SKULL ? 0.7f : 0.4f; k.stiff = 0; k.fallTop = p.y; k.steadyT = k.getUpT + 0.6f;
        }
        return;
    }
    k.steadyT = std::max(0.0f, k.steadyT - dt);
    if (k.getUpT > 0) { k.getUpT -= dt; in = Input{in.moveX * 0, 0, in.aim}; }   // (getting up: no control yet)
    if (k.proneT > 0) { k.proneT -= dt; if (k.proneT <= 0 && !Overlaps(*this, k, k.pos, 1.7f)) { k.st = S_STAND; k.height = 1.7f; } }
    bool ground = k.grounded;
    // the stick's height: standing, ducked, diving or prone
    float want = (k.st == S_DUCK ? 0.9f : (k.st == S_DIVE || k.st == S_PRONE) ? 0.5f : 1.7f) * k.size;
    if (want > k.height && Overlaps(*this, k, k.pos, want)) want = k.height;   // (no standing up under a ledge)
    k.height = want;
    // running (8 m/s with a lean), ducking (no moving), diving (a headfirst dash)
    float mx = std::clamp(in.moveX, -1.0f, 1.0f);
    if (k.st == S_DUCK || k.st == S_PRONE || k.grabbedBy >= 0) mx = 0;
    if (fabsf(mx) > 0.2f && k.st != S_DIVE && k.punchT <= 0) k.face = mx > 0 ? 1 : -1;
    if (k.punchT > 0 && fabsf(in.aim.x) > 0.2f) k.face = in.aim.x > 0 ? 1 : -1;
    // water (stage 5): you swim slowly (a stroke for each jump press), sink gently, and drown after 8 s with your head
    // under (3 s in the Void's brine)
    Vector2 mid{k.pos.x, k.pos.y + k.height * 0.5f}, head{k.pos.x, k.pos.y + k.height - 0.12f};
    k.wet = InLiquid(mid);
    if (InLiquid(head)) {
        k.swimT += dt;
        bool brine = BrineAt(head);
        if (k.gear != GR_FISHBOWL && k.swimT > (brine ? 3.0f : 8.0f) * (k.trinket == TK_BIG_LUNGS ? 2.0f : 1.0f)) { Kill(k, k.lastHitBy >= 0 && t - k.lastHitT < 4 ? k.lastHitBy : -1, brine ? "the brine" : "drowned"); return; }
    } else k.swimT = std::max(0.0f, k.swimT - dt * 4);
    // the floor under the feet: ice is slippery, a conveyor carries you
    uint8_t under = stage.At((int)floorf(k.pos.x / TILE), (int)floorf((k.pos.y - 0.05f) / TILE));
    bool oar = k.wet && k.weapon >= 0 && k.weapon < (int)items.size() && items[k.weapon].weapon >= 0 && Weapons()[items[k.weapon].weapon].key == "oar";   // (an oar: row twice as fast)
    float target = mx * (k.wet ? (oar ? 8.0f : k.trinket == TK_BIG_LUNGS ? 3.0f : 4.0f) : 8.0f) * (k.trinket == TK_PACK_RAT ? 0.9f : 1.0f), accel = ground ? 70.0f : k.wet ? 20.0f : 34.0f;
    if (ground && (under == T_CONV_L || under == T_CONV_R)) k.pos.x += (under == T_CONV_R ? 3.0f : -3.0f) * dt;
    if (k.st == S_DIVE) accel = 2;
    if (ground && fabsf(mx) < 0.1f) accel = 55;
    if (ground && under == T_ICE) accel *= 0.12f;   // (ice: slow to start, slower to stop)
    if (k.knockT > 0) { k.knockT -= dt; accel = 9; target = 0; }   // (knocked back: the feet skid; no control for a moment)
    k.vel.x += Clamp(target - k.vel.x, -accel * dt, accel * dt);
    // duck and dive
    if (ground && k.st != S_PRONE && in.moveY < -0.5f) k.st = S_DUCK;
    else if (k.st == S_DUCK && in.moveY >= -0.5f) k.st = S_STAND;
    if (!ground && in.moveY < -0.5f && k.st != S_DIVE && k.st != S_WALL && k.st != S_PRONE && k.trinket != TK_MAGNET_PALMS) { k.st = S_DIVE; k.vel.x = k.face * 10.0f; k.vel.y = std::min(k.vel.y, -1.0f); }
    // gravity, the variable jump (a release while rising cuts it), coyote time
    float g = gravity * GravityAt(mid) * (k.wet ? 0.22f : 1.0f) * (k.gravT > 0 ? -1.0f : 1.0f) * (k.bubbleT > 0 ? 0.0f : 1.0f);
    k.vel.y -= g * dt;
    if (k.wet) {   // (a stroke on each press; held, you rise slowly; the water drags)
        if (jumpPress && !ground) { k.vel.y = std::max(k.vel.y, 5.0f); Emit(EV_JUMP, k.pos, k.id); }
        if (in.jump) k.vel.y += 4.0f * dt;
        k.vel.y = std::max(k.vel.y * (1 - 1.2f * dt), -3.5f);
    }
    if (!in.jump && k.jumpWas && k.vel.y > 0 && k.jumpHeldT > 0) { k.vel.y *= 0.45f; k.jumpHeldT = 0; }
    k.coyoteT = ground ? 0.08f : std::max(0.0f, k.coyoteT - dt);
    // the wall: hold toward it in the air to climb (3 m/s for 1.5 s, then a slide); jump off at 45 degrees
    int side = k.wallRight ? 1 : k.wallLeft ? -1 : 0;
    if (!ground && side != 0 && mx * side > 0.3f && k.st != S_DIVE && k.trinket != TK_THICK_COAT) { k.st = S_WALL; k.wallSide = side; k.face = side; }
    else if (k.st == S_WALL && (side == 0 || mx * side <= 0.3f || ground)) k.st = ground ? S_STAND : S_AIR;
    if (k.st == S_WALL) {
        k.climbT += dt; k.vel.x = side * 0.5f;
        if (k.trinket == TK_MAGNET_PALMS) k.climbT = std::min(k.climbT, 1.0f);   // (it never slides)
        k.vel.y = k.climbT < 1.5f ? 3.0f : std::max(k.vel.y, -2.0f);
        if (jumpPress) { k.vel = {-side * 12.5f * 0.7071f, 12.5f * 0.7071f}; k.st = S_AIR; k.face = -side; k.jumpHeldT = 0.01f; Emit(EV_JUMP, k.pos, k.id); }
    }
    if (ground) k.airJump = false;
    if (jumpPress && (ground || k.coyoteT > 0) && k.st != S_DUCK && k.st != S_PRONE) { k.vel.y = k.trinket == TK_SPRING_HEELS ? 11.5f : 13.4f; k.coyoteT = 0; k.jumpHeldT = 0.01f; k.st = S_AIR; Emit(EV_JUMP, k.pos, k.id); }
    else if (jumpPress && !ground && k.coyoteT <= 0 && k.st != S_WALL && k.trinket == TK_SPRING_HEELS && !k.airJump && !k.wet) { k.vel.y = 11.5f; k.airJump = true; k.jumpHeldT = 0.01f; Emit(EV_JUMP, k.pos, k.id); }   // (Spring Heels: the double jump)
    if (k.vel.y > 0 && k.jumpHeldT > 0) k.jumpHeldT += dt; if (k.vel.y <= 0) k.jumpHeldT = 0;
    k.vel.y = std::max(k.vel.y, -24.0f);
    if (GravityAt(mid) < 0.99f || k.wet) k.fallTop = std::min(k.fallTop, k.pos.y + 3.0f);   // (no long-fall stun from a float down a low-gravity pocket, or a sink through water)
    // move: x, then y, against the tiles
    Vector2 p = k.pos;
    float nx = p.x + k.vel.x * dt;
    k.wallLeft = k.wallRight = false;
    // a low step or a ledge's lip (up to 0.36 m): step up onto it rather than stop (walking, climbing out of a pit)
    if (Overlaps(*this, k, {nx, p.y}, k.height) && fabsf(k.vel.x) > 0.05f && k.vel.y < 4 && k.st != S_DIVE && k.st != S_PRONE) {
        for (float up = 0.05f; up <= 0.36f; up += 0.05f) if (!Overlaps(*this, k, {nx, p.y + up}, k.height) && !Overlaps(*this, k, {p.x, p.y + up}, k.height)) { p.y += up; k.vel.y = std::max(k.vel.y, 0.0f); if (k.st == S_WALL) k.st = S_AIR; break; }
    }
    if (Overlaps(*this, k, {nx, p.y}, k.height)) {
        if (fabsf(k.vel.x) > 6) Emit(EV_BONK, p, k.id);
        float s = Sgn(k.vel.x); if (s == 0) s = 1;
        while (!Overlaps(*this, k, {p.x + s * 0.01f, p.y}, k.height) && fabsf(nx - p.x) > 0.01f) p.x += s * 0.01f;
        nx = p.x; k.vel.x = 0;
    }
    p.x = nx;
    k.wallLeft = Overlaps(*this, k, {p.x - 0.03f, p.y + 0.2f}, k.height - 0.4f);
    k.wallRight = Overlaps(*this, k, {p.x + 0.03f, p.y + 0.2f}, k.height - 0.4f);
    float ny = p.y + k.vel.y * dt;
    bool landed = false;
    if (Overlaps(*this, k, {p.x, ny}, k.height)) {
        float s = Sgn(k.vel.y); if (s == 0) s = -1;
        while (!Overlaps(*this, k, {p.x, p.y + s * 0.01f}, k.height) && fabsf(ny - p.y) > 0.01f) p.y += s * 0.01f;
        if (s < 0) landed = true;
        ny = p.y; k.vel.y = 0;
    }
    // a rope bridge: landed on from above; held down, you drop through
    bool dropThrough = in.moveY < -0.5f;
    if (!landed && k.vel.y <= 0 && !dropThrough && RopeUnder(p.x - k.halfW, p.x + k.halfW, p.y, ny)) { ny = floorf(p.y / TILE + 1e-3f) * TILE; if (ny > p.y + 1e-3f) ny -= TILE; landed = true; k.vel.y = 0; }
    p.y = ny;
    k.grounded = landed || (k.vel.y <= 0 && (Overlaps(*this, k, {p.x, p.y - 0.03f}, k.height) || (!dropThrough && RopeUnder(p.x - k.halfW, p.x + k.halfW, p.y + 0.001f, p.y - 0.03f))));
    // a wrapping stage: off one edge, in at the other (the ragdoll comes too)
    if (stage.wrap && (p.x < 0 || p.x > stage.Width())) { float sh = p.x < 0 ? stage.Width() : -stage.Width(); p.x += sh; for (auto& a : k.pt) { a.p.x += sh; a.q.x += sh; } }
    // standing on another stick's head
    for (const auto& o : sticks) {
        if (o.id == k.id || !o.present || !o.alive || o.st == S_RAGDOLL) continue;
        float top = o.pos.y + o.height;
        if (fabsf(o.pos.x - p.x) < o.halfW + k.halfW - 0.05f && k.vel.y <= 0 && k.pos.y >= top - 0.12f && p.y < top) { p.y = top; k.vel.y = 0; k.grounded = landed = true; }
    }
    if (!k.grounded) k.fallTop = std::max(k.fallTop, p.y);
    if (landed && !ground) {
        Emit(EV_LAND, p, k.id, -1, k.fallTop - p.y);
        if (k.st == S_DIVE) { k.st = S_PRONE; k.proneT = 0.6f; k.vel.x *= 0.3f; }
        else if (k.st != S_DUCK) k.st = S_STAND;
        if (k.fallTop - p.y > 10 && k.trinket != TK_CAT_LEGS) { k.st = S_RAGDOLL; k.ragT = 0.5f; k.stiff = 0; k.cause = "a long fall"; }   // (a 10 m fall stuns for 0.5 s)
        k.climbT = 0;
    }
    if (k.grounded) k.fallTop = p.y;
    if (!k.grounded && k.st == S_STAND) k.st = S_AIR;
    if (k.grounded && k.st == S_AIR) k.st = S_STAND;
    k.pos = p;
    if (k.st == S_STAND) k.walkPh += fabsf(k.vel.x) * dt * 2.4f;
    k.jumpWas = in.jump;
}

// ---------------------------------------------------------------- the pose drive (the ragdoll half follows the controller)
void World::StepPose(Stick& k) {
    if (k.frozenT > 0 && k.alive) return;
    float target = (!k.alive || k.st == S_RAGDOLL) ? 0.0f : (k.grabbedBy >= 0 ? 0.15f : k.hitT > 0 ? 0.35f : 1.0f);
    if (k.getUpT > 0) target = std::min(target, 1 - k.getUpT / 0.4f);
    k.stiff += (target - k.stiff) * std::min(1.0f, STEP * 14);
    k.hitT = std::max(0.0f, k.hitT - STEP);
    if (k.stiff < 0.02f) return;
    Vector2 cv = Vector2Scale(k.vel, STEP);
    for (int j = 0; j < J_COUNT; j++) {
        Particle& q = k.pt[j];
        Vector2 tgt = Vector2Add(k.pos, PoseOffset(k, j, t));
        float pull = 0.38f * k.stiff;
        Vector2 v = Vector2Subtract(q.p, q.q);
        v = Vector2Lerp(v, cv, 0.5f * k.stiff);
        Vector2 d = Vector2Scale(Vector2Subtract(tgt, q.p), pull);
        q.p = Vector2Add(q.p, d); q.q = Vector2Subtract(q.p, v);
    }
}

// ---------------------------------------------------------------- the particle physics (Verlet, bones, tiles, bodies)
static void CollideTiles(const World& w, Particle& a) {
    int x0 = (int)floorf((a.p.x - a.r) / TILE), x1 = (int)floorf((a.p.x + a.r) / TILE), y0 = (int)floorf((a.p.y - a.r) / TILE), y1 = (int)floorf((a.p.y + a.r) / TILE);
    // the movers first (a piston's block, an elevator's floor), as boxes
    for (const auto& pc : w.stage.pieces) {
        if (pc.kind != PK_PISTON && pc.kind != PK_ELEVATOR) continue;
        Rectangle r{pc.x * TILE + pc.off.x, pc.y * TILE + pc.off.y, pc.w * TILE, pc.h * TILE};
        Vector2 c{std::clamp(a.p.x, r.x, r.x + r.width), std::clamp(a.p.y, r.y, r.y + r.height)}; Vector2 d = Vector2Subtract(a.p, c); float L = Vector2Length(d);
        if (L >= a.r) continue;
        Vector2 n = L > 1e-5f ? Vector2Scale(d, 1 / L) : Vector2{0, 1};
        a.p = Vector2Add(a.p, Vector2Scale(n, a.r - L)); a.q = Vector2Add(a.q, Vector2Scale(Vector2Subtract(a.p, a.q), 0.2f));
    }
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        if (!w.stage.Solid(x, y)) continue;
        Vector2 c{std::clamp(a.p.x, x * TILE, (x + 1) * TILE), std::clamp(a.p.y, y * TILE, (y + 1) * TILE)};
        Vector2 d = Vector2Subtract(a.p, c); float L = Vector2Length(d);
        if (L >= a.r) continue;
        Vector2 n;
        if (L < 1e-5f) {   // (the centre inside the tile: out the nearest face)
            float l = a.p.x - x * TILE, rr = (x + 1) * TILE - a.p.x, b = a.p.y - y * TILE, tp = (y + 1) * TILE - a.p.y, m = std::min({l, rr, b, tp});
            n = m == l ? V(-1, 0) : m == rr ? V(1, 0) : m == b ? V(0, -1) : V(0, 1);
            a.p = Vector2Add(a.p, Vector2Scale(n, m + a.r));
        } else { n = Vector2Scale(d, 1 / L); a.p = Vector2Add(a.p, Vector2Scale(n, a.r - L)); }
        // friction: kill most of the motion along the surface
        Vector2 v = Vector2Subtract(a.p, a.q); float vn = Vector2DotProduct(v, n);
        Vector2 vt = Vector2Subtract(v, Vector2Scale(n, vn));
        a.q = Vector2Add(a.q, Vector2Scale(vt, 0.25f));
        if (vn < 0) a.q = Vector2Add(a.q, Vector2Scale(n, vn * 1.2f));   // (a little bounce)
    }
}
void World::StepParticles() {
    const float dt = STEP;
    for (auto& k : sticks) {
        if (!k.present) continue;
        if (k.frozenT > 0 && k.alive) { for (auto& a : k.pt) a.q = a.p; continue; }   // (frozen: a statue)
        float g = gravity * (k.gravT > 0 ? -1.0f : 1.0f);
        for (auto& a : k.pt) { Vector2 v = Vector2Scale(Vector2Subtract(a.p, a.q), 0.997f); a.q = a.p; a.p = Vector2Add(a.p, Vector2Add(v, V(0, -g * dt * dt))); }
    }
    for (int it = 0; it < 6; it++) {
        for (auto& k : sticks) {
            if (!k.present) continue;
            for (int b = 0; b < NBONES; b++) {
                Particle& A = k.pt[BONES[b][0]]; Particle& B = k.pt[BONES[b][1]];
                Vector2 d = Vector2Subtract(B.p, A.p); float L = Vector2Length(d); if (L < 1e-5f) continue;
                float wsum = A.invMass + B.invMass; float diff = (L - BONE_LEN[b] * k.size) / (L * wsum) * (b == NBONES - 1 ? 0.4f : 1.0f);
                A.p = Vector2Add(A.p, Vector2Scale(d, diff * A.invMass)); B.p = Vector2Subtract(B.p, Vector2Scale(d, diff * B.invMass));
            }
            for (auto& a : k.pt) CollideTiles(*this, a);
        }
    }
    // bodies against bodies: a little room between different sticks' particles (living sticks stand on dead ones)
    for (size_t i = 0; i < sticks.size(); i++) for (size_t j = i + 1; j < sticks.size(); j++) {
        Stick& A = sticks[i]; Stick& B = sticks[j]; if (!A.present || !B.present) continue;
        if (fabsf(A.pt[J_PELVIS].p.x - B.pt[J_PELVIS].p.x) > 2.5f || fabsf(A.pt[J_PELVIS].p.y - B.pt[J_PELVIS].p.y) > 2.5f) continue;
        if (A.grabbing == B.id || B.grabbing == A.id) continue;
        for (auto& a : A.pt) for (auto& b : B.pt) {
            Vector2 d = Vector2Subtract(b.p, a.p); float L = Vector2Length(d), r = a.r + b.r;
            if (L >= r || L < 1e-5f) continue;
            Vector2 n = Vector2Scale(d, (r - L) / L * 0.5f);
            a.p = Vector2Subtract(a.p, n); b.p = Vector2Add(b.p, n);
        }
    }
}

// ---------------------------------------------------------------- fists (doc p. 3)
static bool Near(const Stick& o, Vector2 at, float r) {
    for (int j : {J_HEAD, J_NECK, J_PELVIS, J_HAND_L, J_KNEE_L, J_KNEE_R}) if (Vector2Distance(o.pt[j].p, at) < r + o.pt[j].r) return true;
    // the torso as a segment
    Vector2 a = o.pt[J_NECK].p, b = o.pt[J_PELVIS].p, ab = Vector2Subtract(b, a); float L2 = Vector2LengthSqr(ab);
    float u = L2 > 0 ? std::clamp(Vector2DotProduct(Vector2Subtract(at, a), ab) / L2, 0.0f, 1.0f) : 0;
    return Vector2Distance(Vector2Add(a, Vector2Scale(ab, u)), at) < r + 0.08f;
}
void World::StepFists(Stick& k) {
    const float dt = STEP;
    k.punchT = std::max(0.0f, k.punchT - dt); k.punchCool = std::max(0.0f, k.punchCool - dt); k.comboT = std::max(0.0f, k.comboT - dt);
    k.kickT = std::max(0.0f, k.kickT - dt); k.tauntT = std::max(0.0f, k.tauntT - dt); k.thrownT = std::max(0.0f, k.thrownT - dt); k.blockT = std::max(0.0f, k.blockT - dt);
    if (!k.alive || k.st == S_RAGDOLL || k.getUpT > 0 || k.grabbedBy >= 0) { if (k.grabbing >= 0) { sticks[k.grabbing].grabbedBy = -1; k.grabbing = -1; } k.fireWas = k.in.fire; return; }
    Input& in = k.in;
    if (in.taunt && k.tauntT <= 0) {
        k.tauntT = 1.0f;
        if (k.trinket == TK_LOUD_MOUTH) for (auto& o : sticks) if (o.id != k.id && o.alive && o.present && Vector2Distance(o.pt[J_PELVIS].p, k.pt[J_PELVIS].p) < 1.0f + o.halfW) Hit(o, k.id, 0, Vector2Normalize({o.pos.x > k.pos.x ? 1.0f : -1.0f, 0.4f}), 8, false, "a taunt");   // (Loud Mouth: the taunt shoves)
    }
    if (k.weapon >= 0 && k.grabbing < 0) { Fire(k); return; }   // (armed: the trigger or the swing; fists are for the empty-handed)
    bool press = in.fire && !k.fireWas;
    // holding someone: carry them by the collar; let go of fire to throw them where you aim
    if (k.grabbing >= 0) {
        Stick& o = sticks[k.grabbing];
        k.holdT += dt;
        Vector2 hold = Vector2Add(k.pt[J_HAND_R].p, V(0, 0.1f));
        Vector2 d = Vector2Subtract(hold, o.pt[J_NECK].p);
        for (auto& a : o.pt) { a.p = Vector2Add(a.p, Vector2Scale(d, 0.5f)); a.q = Vector2Add(a.q, Vector2Scale(d, 0.45f)); }
        if (!in.fire || k.holdT > 2.5f || (o.alive && o.in.fire && k.holdT > 1.2f)) {
            Vector2 aim = Vector2Normalize(in.aim); Vector2 v = Vector2Add(Vector2Scale(aim, 14), V(0, 3));
            for (auto& a : o.pt) a.q = Vector2Subtract(a.p, Vector2Scale(v, STEP));
            o.grabbedBy = -1; o.thrownT = 1.0f; o.thrownBy = k.id; if (o.alive) { o.st = S_RAGDOLL; o.ragT = 0.8f; o.stiff = 0; }
            Emit(EV_THROW, o.pt[J_NECK].p, o.id, k.id);
            k.grabbing = -1; k.holdT = 0;
        }
        k.fireWas = in.fire; return;
    }
    // the kick: fire while diving
    // a press against someone already touching you waits a moment: held, it's a grab; let go, it's a jab
    if (press && k.st != S_DIVE) { for (const auto& o : sticks) if (o.id != k.id && o.present && o.grabbedBy < 0 && Vector2Distance(o.pt[J_PELVIS].p, k.pt[J_PELVIS].p) < 0.68f) { k.grabWait = 0.15f; press = false; break; } }
    if (k.grabWait > 0) {
        k.grabWait -= dt;
        if (!in.fire) { k.grabWait = 0; press = true; }   // (a tap: the jab after all)
        else if (k.grabWait <= 0) {
            for (auto& o : sticks) if (o.id != k.id && o.present && o.grabbedBy < 0 && Vector2Distance(o.pt[J_PELVIS].p, k.pt[J_PELVIS].p) < 0.9f) { k.grabbing = o.id; o.grabbedBy = k.id; k.holdT = 0; if (o.alive) { o.st = S_RAGDOLL; o.ragT = 3; } Emit(EV_GRAB, o.pt[J_NECK].p, o.id, k.id); break; }
        }
        if (k.grabbing >= 0) { k.fireWas = in.fire; return; }
    }
    if (press && (k.st == S_DIVE || (k.st == S_PRONE && k.proneT > 0.45f)) && k.kickT <= 0) { k.kickT = 0.3f; k.punchHit.clear(); Emit(EV_KICK, k.pos, k.id); }
    else if (press && k.punchCool <= 0) {
        // a jab with the lead arm; every third in a row is a haymaker
        k.combo = k.comboT > 0 ? k.combo + 1 : 1; k.comboT = 0.7f;
        k.punchHay = k.combo >= 3; if (k.punchHay) k.combo = 0;
        float slow = k.trinket == TK_LONG_ARMS ? 0.1f : 0.0f;
        k.punchT = (k.punchHay ? 0.35f : 0.25f) + slow; k.punchCool = (k.punchHay ? 0.42f : 0.25f) + slow; k.punchHit.clear(); k.blockT = Arms().blockWindow;   // (a punch thrown at a bullet in time deflects it)
        Emit(k.punchHay ? EV_HAYMAKER : EV_PUNCH, k.pt[J_HAND_R].p, k.id);
    }
    // the grab: hold fire against someone (alive or dead) for a moment
    if (in.fire && k.fireWas && k.punchT <= 0 && k.grabbing < 0) {
        k.holdT += dt;
        if (k.holdT > 0.18f) for (auto& o : sticks) if (o.id != k.id && o.present && o.grabbedBy < 0 && Near(o, k.pt[J_HAND_R].p, 0.32f)) { k.grabbing = o.id; o.grabbedBy = k.id; k.holdT = 0; if (o.alive) { o.st = S_RAGDOLL; o.ragT = 3; } Emit(EV_GRAB, o.pt[J_NECK].p, o.id, k.id); break; }
    } else if (!in.fire) k.holdT = 0;
    // the jab lands in the middle of its swing (once per target)
    float full = (k.punchHay ? 0.35f : 0.25f) + (k.trinket == TK_LONG_ARMS ? 0.1f : 0.0f), u = k.punchT > 0 ? 1 - k.punchT / full : 2;
    if (u > 0.25f && u < 0.75f) {
        Vector2 hand = k.pt[J_HAND_R].p, aim = Vector2Normalize(in.aim); if (Vector2Length(in.aim) < 0.1f) aim = V((float)k.face, 0);
        for (auto& o : sticks) {
            if (o.id == k.id || !o.present || std::find(k.punchHit.begin(), k.punchHit.end(), o.id) != k.punchHit.end()) continue;
            if (!Near(o, hand, 0.16f) && !(k.trinket == TK_LONG_ARMS && Near(o, Vector2Add(hand, Vector2Scale(aim, 0.35f)), 0.16f))) continue;   // (Long Arms: half again the reach)
            k.punchHit.push_back(o.id);
            Vector2 dir = Vector2Normalize(Vector2Add(aim, V(0, 0.25f)));
            if (k.punchHay) Hit(o, k.id, 20, dir, 11.5f, true, "a haymaker");   // (3 m)
            else Hit(o, k.id, 10, dir, 5.2f, false, "fists");                   // (1.5 m)
        }
    }
    if (k.kickT > 0) {
        for (auto& o : sticks) {
            if (o.id == k.id || !o.present || std::find(k.punchHit.begin(), k.punchHit.end(), o.id) != k.punchHit.end()) continue;
            if (!Near(o, k.pt[J_FOOT_L].p, 0.28f) && !Near(o, k.pt[J_FOOT_R].p, 0.28f) && !Near(o, k.pt[J_KNEE_L].p, 0.2f)) continue;
            k.punchHit.push_back(o.id);
            Hit(o, k.id, 15, Vector2Normalize(V((float)k.face, 0.35f)), 13, true, "a kick");   // (4 m)
        }
    }
    k.fireWas = in.fire;
}

// ---------------------------------------------------------------- damage
void World::Hit(Stick& o, int by, float dmg, Vector2 dir, float knock, bool ragdoll, const char* cause) {
    if (!friendlyFire && by >= 0 && by != o.id && SameTeam(by, o.id)) return;   // (Teams with friendly fire off; the Gauntlet)
    if (o.balloonT > 0) { o.balloonT = 0; Emit(EV_BUBBLE, o.pt[J_HEAD].p, o.id, by, 1); }   // (the balloon pops)
    // the body flies whether or not it's alive
    knock *= o.trinket == TK_CAT_LEGS ? 1.2f : o.trinket == TK_DEADWEIGHT ? 0.5f : 1.0f;
    if (Mut(MU_ONE_HIT) && dmg > 0 && o.alive) dmg = std::max(dmg, o.hp + 1);   // (One Hit: everything kills)
    float pk = knock * (ragdoll ? 1.0f : 0.6f);
    // (the kick tops the body up to pk along dir rather than adding to it: eight pellets at once are one big shove, not eight)
    for (auto& a : o.pt) { float along = Vector2DotProduct(Vector2Subtract(a.p, a.q), dir) / STEP; float add = std::max(0.0f, pk - std::max(0.0f, along)); a.q = Vector2Subtract(a.q, Vector2Scale(dir, add * STEP)); }
    Emit(ragdoll ? EV_HAYMAKER : EV_HIT, o.pt[J_NECK].p, o.id, by, dmg);
    if (!o.alive) return;
    // stage 6: a hit pops a bubble, frees a bear trap, and shatters ice (a frozen stick takes 20 more and falls apart)
    if (o.bubbleT > 0) { o.bubbleT = 0; Emit(EV_BUBBLE, o.pt[J_PELVIS].p, o.id, by, 1); }
    if (o.trapT > 0 && by >= 0) o.trapT = 0;
    if (o.frozenT > 0) { o.frozenT = 0; dmg += 20; ragdoll = true; Emit(EV_FREEZE, o.pt[J_PELVIS].p, o.id, by, 1); }
    o.hp -= dmg; o.lastHitBy = by; o.lastHitT = t; o.hitT = 0.25f;
    o.vel = Vector2Add(Vector2Scale(o.vel, 0.3f), Vector2Scale(dir, knock)); o.knockT = std::max(o.knockT, 0.35f * std::min(1.0f, knock / 8));   // (a small knock is a small stagger: pistols don't pin you)
    if (o.grabbing >= 0) { sticks[o.grabbing].grabbedBy = -1; o.grabbing = -1; }
    if (ragdoll && o.steadyT > 0 && knock < 14) ragdoll = false;   // (no stunlock: a stick just up stays up unless the blow is huge)
    if (ragdoll) { o.st = S_RAGDOLL; o.ragT = std::max(o.st == S_RAGDOLL ? o.ragT : 0.0f, 0.45f + knock * 0.02f); o.stiff = 0; }
    if (o.hp <= 0 && o.trinket == TK_SECOND_WIND && !o.windUsed) { o.hp = 1; o.windUsed = true; o.burnT = std::max(o.burnT, 2.0f); Emit(EV_BURN, o.pt[J_PELVIS].p, o.id, by, 1); }   // (Second Wind: once, at 1 HP, on fire)
    if (o.hp <= 0) Kill(o, by, cause);
}
void World::Kill(Stick& k, int by, const char* cause) {
    if (!k.alive) return;
    k.alive = false; k.hp = 0; k.st = S_DEAD; k.cause = cause ? cause : "";
    if (k.grabbing >= 0) { sticks[k.grabbing].grabbedBy = -1; k.grabbing = -1; }
    if (k.weapon >= 0) DropWeapon(k, Vector2Scale(k.vel, 0.5f), false);
    if (k.carry >= 0 && k.carry < (int)items.size()) { items[k.carry].holder = -1; k.carry = -1; }   // (the Pack Rat's second weapon falls too)
    if (by >= 0 && by < (int)sticks.size() && by != k.id) { sticks[by].kills++; if (Mut(MU_VAMPIRE) && sticks[by].alive) sticks[by].hp = std::min(100.0f, sticks[by].hp + 50); }
    // the kill's bonus (doc p. 5): knocked into a hazard +10, a block-deflect +20
    float bonus = (k.cause == "fell out" || k.cause == "the wall") && by >= 0 ? 1.0f : k.cause.rfind("a deflected", 0) == 0 ? 2.0f : 0.0f;
    Emit(EV_DIE, k.pt[J_NECK].p, k.id, by, bonus);
}

// ---------------------------------------------------------------- the step
void World::Step() {
    frame++; t += STEP;
    StepRules();
    StepMode();
    StepPieces();
    for (auto& k : sticks) if (k.present) { StepStatus(k); StepController(k); StepGear(k); StepFists(k); }
    for (auto& k : sticks) if (k.present) StepPose(k);
    StepParticles();
    StepArms();
    StepThings();
    for (auto& k : sticks) {
        if (!k.present) continue;
        // a thrown stick is a projectile: 15 to it and to whoever it hits
        if (k.thrownT > 0) {
            Vector2 v = Vector2Scale(Vector2Subtract(k.pt[J_PELVIS].p, k.pt[J_PELVIS].q), 1 / STEP);
            if (Vector2Length(v) > 5) for (auto& o : sticks) {
                if (o.id == k.id || o.id == k.thrownBy || !o.present || !Near(o, k.pt[J_PELVIS].p, 0.2f)) continue;
                k.thrownT = 0; Hit(o, k.thrownBy, 15, Vector2Normalize(v), 7, true, "a thrown body"); Hit(k, k.thrownBy, 15, Vector2Negate(Vector2Normalize(v)), 2, true, "a thrown body"); break;
            }
        }
        // falling out of the stage
        Vector2 c = k.pt[J_PELVIS].p;
        if (stage.wrap && k.st == S_RAGDOLL && (c.x < -0.3f || c.x > stage.Width() + 0.3f)) { float sh = c.x < 0 ? stage.Width() : -stage.Width(); for (auto& a : k.pt) { a.p.x += sh; a.q.x += sh; } c.x += sh; }
        if (k.alive && (c.y < -2.5f || (!stage.wrap && (c.x < -3 || c.x > stage.Width() + 3)))) { Emit(EV_FALL_OUT, c, k.id); Kill(k, k.lastHitBy >= 0 && t - k.lastHitT < 4 ? k.lastHitBy : -1, "fell out"); }
        if (k.alive && c.y > stage.Height() + 8) { Emit(EV_FALL_OUT, c, k.id); Kill(k, k.lastHitBy >= 0 && t - k.lastHitT < 4 ? k.lastHitBy : -1, "fell into the sky"); }   // (the gravity gun, a bubble)
        if (!k.alive && c.y < -8) k.present = false;   // (gone off the bottom: no more body)
    }
}

uint64_t World::Hash() const {
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; for (size_t i = 0; i < n; i++) { h ^= b[i]; h *= 1099511628211ull; } };
    for (const auto& k : sticks) { mix(&k.pos, sizeof(k.pos)); mix(&k.vel, sizeof(k.vel)); mix(&k.hp, sizeof(k.hp)); uint8_t s = k.st; mix(&s, 1); for (const auto& a : k.pt) { mix(&a.p, sizeof(a.p)); mix(&a.q, sizeof(a.q)); } }
    for (const auto& it : items) { mix(&it.a.p, sizeof(it.a.p)); mix(&it.b.p, sizeof(it.b.p)); mix(&it.ammo, sizeof(it.ammo)); }
    for (const auto& b : bullets) { mix(&b.p, sizeof(b.p)); mix(&b.v, sizeof(b.v)); }
    mix(&rng, sizeof(rng));
    return h;
}

// ---------------------------------------------------------------- the bot (stage 2: it fetches crates and guns, keeps its weapon's range, aims with lead)
static const WeaponDef* HeldDef(const World& w, const Stick& k) { if (k.weapon < 0 || k.weapon >= (int)w.items.size()) return nullptr; int wi = w.items[k.weapon].weapon; return wi >= 0 && wi < (int)Weapons().size() ? &Weapons()[wi] : nullptr; }
static bool Bottomless(const World& w, int col) { for (int y = 0; y < w.stage.h; y++) if (w.stage.Solid(col, y)) return false; return true; }
static void MoveToward(const World& w, const Stick& k, Vector2 goal, Input& in, bool careful) {
    // never aim for a spot over a bottomless pit: the nearest column with a floor instead
    int gc = (int)floorf(goal.x / TILE);
    if (careful && Bottomless(w, gc)) { for (int d = 1; d < w.stage.w; d++) { int c = gc + (goal.x < k.pos.x ? d : -d), c2 = gc - (goal.x < k.pos.x ? d : -d); if (c >= 0 && c < w.stage.w && !Bottomless(w, c)) { goal.x = (c + 0.5f) * TILE; break; } if (c2 >= 0 && c2 < w.stage.w && !Bottomless(w, c2)) { goal.x = (c2 + 0.5f) * TILE; break; } } }
    // in the air over a pit: steer for the nearer edge
    if (!k.grounded && Bottomless(w, (int)floorf(k.pos.x / TILE))) { int c = (int)floorf(k.pos.x / TILE); for (int d = 1; d < 8; d++) { if (!Bottomless(w, c + (k.vel.x >= 0 ? d : -d))) { goal.x = (c + (k.vel.x >= 0 ? d : -d) + 0.5f) * TILE; break; } if (!Bottomless(w, c - (k.vel.x >= 0 ? d : -d))) { goal.x = (c - (k.vel.x >= 0 ? d : -d) + 0.5f) * TILE; break; } } }
    float dx = goal.x - k.pos.x, dy = goal.y - k.pos.y;
    if (fabsf(dx) > 0.3f) in.moveX = Sgn(dx);
    int fx = (int)floorf((k.pos.x + Sgn(dx) * 0.55f) / TILE), fy = (int)floorf((k.pos.y - 0.1f) / TILE);
    bool floorAhead = w.stage.Solid(fx, fy) || w.stage.Solid(fx, fy - 1) || w.stage.Solid(fx, fy - 2);
    bool wallAhead = w.stage.Solid(fx, (int)floorf((k.pos.y + 0.3f) / TILE)) || w.stage.Solid(fx, (int)floorf((k.pos.y + 1.0f) / TILE));
    if (k.grounded && (wallAhead || (dy > 1.2f && fabsf(dx) < 4))) in.jump = true;
    if (k.grounded && !floorAhead && dy > -0.5f) { bool gapSmall = false; for (int s = 1; s <= 6; s++) if (w.stage.Solid(fx + (int)Sgn(dx) * s, fy)) gapSmall = true; if (gapSmall) in.jump = true; else if (careful) in.moveX = 0; }
    if (!k.grounded && k.vel.y > 0) in.jump = true;
    if (k.st == S_WALL) { in.moveX = (float)k.wallSide; if (k.climbT > 1.2f || dy < 0) in.jump = true; }
}
// ---------------------------------------------------------------- the bots' map (stage 7): the standing cells and how to get between them
// (a walk, a step, a drop, a jump within reach), searched back from a goal; a bot follows the cells downhill. Cached per
// stage layout and goal (a crumbled tile or a new goal makes a new one).
namespace {
struct NavField { uint64_t key = ~0ull; int w = 0, h = 0; std::vector<int> dist; std::vector<std::vector<int>> next; };
bool StandCell(const Stage& s, int x, int y) { return x >= 0 && x < s.w && y >= 1 && y < s.h - 2 && (s.Solid(x, y - 1) || s.OneWay(x, y - 1)) && s.At(x, y - 1) != T_URCHIN && s.At(x, y - 1) != T_RAIL && !s.Solid(x, y) && !s.Solid(x, y + 1) && !s.Solid(x, y + 2); }
bool Open(const Stage& s, int x, int y) { return !s.Solid(x, y) && !s.Solid(x, y + 1); }
bool ArcClear(const Stage& s, int x0, int y0, int x1, int y1) {
    int top = std::max(y0, y1) + 1, sx = x1 > x0 ? 1 : -1;
    for (int y = y0; y <= top; y++) if (!Open(s, x0, y)) return false;
    for (int x = x0; x != x1; x += sx) if (!Open(s, x, top)) return false;
    for (int y = top; y >= y1; y--) if (!Open(s, x1, y)) return false;
    return true;
}
const NavField& Nav(const Stage& s, int gx, int gy) {
    static NavField cache[4]; static int turn = 0;
    uint64_t key = 1469598103934665603ull; for (uint8_t t : s.t) { key ^= t; key *= 1099511628211ull; } key ^= (uint64_t)(gx * 4096 + gy) * 0x9E3779B97F4A7C15ull; key ^= (uint64_t)s.w << 40;
    for (auto& c : cache) if (c.key == key) return c;
    NavField& f = cache[turn++ % 4]; f = NavField{}; f.key = key; f.w = s.w; f.h = s.h;
    int n = s.w * s.h; f.dist.assign(n, -1); f.next.assign(n, {});
    std::vector<std::vector<int>> back(n);
    for (int y = 1; y < s.h - 2; y++) for (int x = 0; x < s.w; x++) {
        if (!StandCell(s, x, y)) continue;
        int from = y * s.w + x;
        auto link = [&](int tx, int ty) { if (tx < 0 || tx >= s.w || ty < 1 || ty >= s.h - 2 || !StandCell(s, tx, ty)) return; int to = ty * s.w + tx; f.next[from].push_back(to); back[to].push_back(from); };
        for (int dx : {-1, 1}) {
            link(x + dx, y);
            if (Open(s, x, y + 2)) link(x + dx, y + 1);
            if (!StandCell(s, x + dx, y) && Open(s, x + dx, y)) for (int yy = y - 1; yy >= 1; yy--) { if (s.Solid(x + dx, yy)) break; if (StandCell(s, x + dx, yy)) { link(x + dx, yy); break; } }   // (a drop)
        }
        for (int dx = -5; dx <= 5; dx++) for (int dy = -3; dy <= 4; dy++) if ((abs(dx) >= 2 || dy >= 2) && ArcClear(s, x, y, x + dx, y + dy)) link(x + dx, y + dy);   // (a jump)
    }
    // back from the goal (the standing cell under it)
    int g = -1; for (int yy = gy; yy >= 1 && g < 0; yy--) for (int dx = 0; dx <= 3 && g < 0; dx++) for (int sgn : {1, -1}) if (g < 0 && StandCell(s, gx + dx * sgn, yy)) g = yy * s.w + gx + dx * sgn;
    if (g < 0) return f;
    std::vector<int> q{g}; f.dist[g] = 0;
    for (size_t i = 0; i < q.size(); i++) for (int p : back[q[i]]) if (f.dist[p] < 0) { f.dist[p] = f.dist[q[i]] + 1; q.push_back(p); }
    return f;
}
}  // namespace
// head for `goal` by the map: to the next cell downhill, jumping when the next cell needs it; off the map, straight there
static void MoveVia(const World& w, const Stick& k, Vector2 goal, Input& in) {
    const Stage& s = w.stage;
    const NavField& f = Nav(s, (int)floorf(goal.x / TILE), (int)floorf((goal.y + 0.05f) / TILE));
    int cx = (int)floorf(k.pos.x / TILE), cy = (int)floorf((k.pos.y + 0.05f) / TILE), cur = cy * s.w + cx;
    if (!k.grounded || cx < 0 || cx >= s.w || cy < 0 || cy >= s.h || f.dist.empty() || f.dist[cur] < 0) { MoveToward(w, k, goal, in, true); return; }
    if (f.dist[cur] == 0) { MoveToward(w, k, goal, in, true); return; }
    int best = -1, bd = f.dist[cur];
    for (int to : f.next[cur]) if (f.dist[to] >= 0 && f.dist[to] < bd) { bd = f.dist[to]; best = to; }
    if (best < 0) { MoveToward(w, k, goal, in, true); return; }
    int tx = best % s.w, ty = best / s.w;
    Vector2 at{(tx + 0.5f) * TILE, ty * TILE};
    in.moveX = at.x > k.pos.x + 0.05f ? 1.0f : at.x < k.pos.x - 0.05f ? -1.0f : 0.0f;
    bool jump = ty > cy || abs(tx - cx) >= 2;
    if (jump && (abs(tx - cx) <= 1 || fabsf(k.pos.x - (cx + 0.5f) * TILE) < 0.2f || !StandCell(s, cx + (tx > cx ? 1 : -1), cy))) in.jump = true;
    if (!k.grounded && k.vel.y > 0) in.jump = true;
}
void BotInput(const World& w, int me, Input& in, uint32_t& rng, int skill) {
    auto R = [&]() { rng = rng * 1664525u + 1013904223u; return ((rng >> 8) & 0xffffff) / 16777216.0f; };
    const Stick& k = w.sticks[me];
    bool fireWas = k.fireWas;
    in = Input{};
    if (!k.alive) return;
    if (k.st == S_RAGDOLL) { in.moveX = R() < 0.5f ? 1.0f : -1.0f; return; }   // (flailing to get up)
    // the Gauntlet: no one to fight, an exit to reach
    if (w.mode == MD_GAUNTLET) { if (k.finished < 0) MoveVia(w, k, w.goal, in); return; }
    // the target: the nearest stick that isn't a teammate (the hunted go for the sharks, the sharks for the hunted)
    int tgt = -1; float bd = 1e9f;
    for (const auto& o : w.sticks) {
        if (o.id == me || !o.present || !o.alive || w.SameTeam(me, o.id)) continue;
        if (w.mode == MD_HUNT && o.shark == k.shark) continue;
        float d = Vector2Distance(o.pos, k.pos); if (d < bd) { bd = d; tgt = o.id; }
    }
    if (tgt < 0) { in.taunt = R() < 0.01f; return; }
    // King of the Plank: get on the plank and fight whoever's near it; the Egg: get it, keep it, or chase whoever has it
    if (w.mode == MD_KING) {
        Vector2 pc{(w.plank.x + w.plank.width * 0.5f) * TILE, w.plank.y * TILE};
        bool on = k.grounded && fabsf(k.pos.y - pc.y) < 0.25f && fabsf(k.pos.x - pc.x) < w.plank.width * TILE * 0.5f;
        if (!on && bd > 2.2f) { MoveVia(w, k, pc, in); in.aim = Vector2Normalize(Vector2Subtract(w.sticks[tgt].pt[J_NECK].p, k.pt[J_NECK].p)); return; }
    }
    if (w.mode == MD_EGG) {
        for (const auto& th : w.things) if (th.kind == TH_EGG && th.a <= 0) {
            if (th.hold == me) { const Stick& o2 = w.sticks[tgt]; MoveToward(w, k, {k.pos.x - Sgn(o2.pos.x - k.pos.x) * 5, k.pos.y}, in, true); if (bd < 1.2f) { in.aim = Vector2Normalize(Vector2Subtract(o2.pt[J_NECK].p, k.pt[J_NECK].p)); in.fire = !fireWas; } return; }
            if (th.hold < 0) { MoveVia(w, k, th.p, in); return; }
            if (th.hold != tgt && th.hold >= 0 && th.hold < (int)w.sticks.size() && !w.SameTeam(me, th.hold)) { tgt = th.hold; bd = Vector2Distance(w.sticks[tgt].pos, k.pos); }
        }
    }
    if (k.persona == PE_TAUNTER && bd > 5 && R() < 0.004f) { in.taunt = true; return; }   // (the taunter: a moment of showing off)
    const Stick& o = w.sticks[tgt];
    const WeaponDef* d = HeldDef(w, k);
    float err = skill >= 2 ? 0.03f : skill == 1 ? 0.09f : 0.2f;
    if (w.flashT > 0 || w.inkT > 0) err *= 4;   // (blinded: it sprays)
    // gear (stage 6): the jetpack, the parachute or the hook out of a fall; the shield against a gun; the rest now and then
    if (k.gear >= 0) {
        bool overPit = !k.grounded && Bottomless(w, (int)floorf(k.pos.x / TILE));
        const WeaponDef* od = HeldDef(w, o);
        switch (k.gear) {
            case GR_JETPACK: in.gear = (overPit && k.vel.y < 0) || w.wallY > k.pos.y - 1.5f; break;
            case GR_PARACHUTE: in.gear = !k.grounded && k.vel.y < -6; break;
            case GR_SHIELD: in.gear = od && od->kind == "gun" && bd < 12 && w.LineOfSight(k.pt[J_NECK].p, o.pt[J_NECK].p); break;
            case GR_HOOK:
                if (overPit && k.vel.y < -2) {   // (a line up and toward the nearer edge)
                    in.gear = true; int c = (int)floorf(k.pos.x / TILE), side = 1;
                    for (int dd = 1; dd < 12; dd++) { if (!Bottomless(w, c + dd)) { side = 1; break; } if (!Bottomless(w, c - dd)) { side = -1; break; } }
                    in.aim = Vector2Normalize({(float)side * 0.7f, 1}); in.moveX = (float)side; return;
                }
                break;
            default: in.gear = R() < 0.003f; break;
        }
    }
    // the wall is up: get above it first
    if (w.wallY > k.pos.y - 2.5f) {
        float best = 1e9f; Vector2 goal = k.pos;
        for (int x = 0; x < w.stage.w; x++) for (int y = w.stage.h - 2; y >= 0; y--) { if (!(w.stage.Solid(x, y) && !w.stage.Solid(x, y + 1) && !w.stage.Solid(x, y + 2))) continue; float top = (y + 1) * TILE; if (top > w.wallY + 2.0f) { float c = fabsf((x + 0.5f) * TILE - k.pos.x) + fabsf(top - k.pos.y) * 0.5f; if (c < best) { best = c; goal = {(x + 0.5f) * TILE, top}; } } break; }
        if (best < 1e8f && (fabsf(goal.x - k.pos.x) > 0.5f || goal.y > k.pos.y + 0.3f)) { MoveToward(w, k, goal, in, false); in.aim = Vector2Normalize(Vector2Subtract(o.pt[J_NECK].p, k.pt[J_NECK].p)); return; }
    }
    // the melee-only bot drops any gun it's given (duck and throw it at them)
    if (k.persona == PE_MELEE && d && d->kind != "melee" && k.grounded) { in.moveY = -1; in.aim = Vector2Normalize(Vector2Subtract(w.sticks[tgt].pt[J_NECK].p, k.pt[J_NECK].p)); in.fire = !fireWas; return; }
    // empty-handed and nobody close (a camper: whenever there's one near): fetch the nearest crate or loose weapon
    if (!k.shark && w.mode != MD_EGG && ((!d && bd > 2.5f) || (k.persona == PE_CAMPER && !d && bd > 1.2f))) {
        int best = -1; float bdist = 14;
        for (int i = 0; i < (int)w.items.size(); i++) { const Item& it = w.items[i]; if (!it.alive || it.holder >= 0 || it.thrownT > 0 || (!it.crate && it.ammo <= 0 && it.weapon >= 0 && Weapons()[it.weapon].kind == "gun")) continue; float dd = Vector2Distance(it.a.p, k.pos) + (it.crate && it.chute ? 3 : 0); if (dd < bdist) { bdist = dd; best = i; } }
        if (best >= 0) { MoveToward(w, k, w.items[best].a.p, in, true); in.aim = {(float)k.face, 0}; return; }
    }
    Vector2 from = k.pt[J_NECK].p, at = o.pt[o.st == S_DUCK ? J_HEAD : J_NECK].p;
    if (d && (d->kind == "gun" || d->kind == "thrown")) {
        // keep the weapon's range: close in with a scatter gun, back off with a sniper
        // (long guns keep their distance; short ones close in: a flamethrower, a scatter gun, a tesla want to be near)
        bool longGun = k.persona != PE_RUSHER && (d->key == "sniper" || d->key == "carbine" || d->key == "harpoon" || d->key == "speargun" || d->key == "rocket" || d->key == "grenadelauncher" || d->key == "chum" || d->key == "flare");
        float want = d->range > 0 ? d->range * 0.4f : d->key == "sniper" || d->key == "carbine" ? 7.0f : d->key == "rocket" || d->key == "grenadelauncher" ? 5.0f : longGun ? 5.0f : 3.0f;
        Vector2 lead = Vector2Scale(o.vel, d->speed > 0 ? Vector2Distance(from, at) / d->speed * (skill >= 1 ? 0.8f : 0.0f) : 0);
        Vector2 aim = Vector2Normalize(Vector2Subtract(Vector2Add(at, lead), from));
        // a lob for anything gravity pulls down: aim above the target by the drop over the flight
        { float g = d->kind == "thrown" ? w.gravity : d->gravity * w.gravity, sp = d->kind == "thrown" ? 13.0f : std::max(1.0f, d->speed), up = d->kind == "thrown" ? 3.0f : 0.0f;
          if (g > 0) { Vector2 to = Vector2Subtract(Vector2Add(at, lead), from); float tt = Vector2Length(to) / sp; to.y += 0.5f * g * tt * tt - up * tt; aim = Vector2Normalize(to); } }
        float a = atan2f(aim.y, aim.x) + (R() - 0.5f) * 2 * err; in.aim = {cosf(a), sinf(a)};
        bool los = w.LineOfSight(from, at);
        if (!los || bd > want * 1.6f) MoveToward(w, k, o.pos, in, true);
        else if (longGun && bd < want * 0.5f && skill >= 1) { MoveToward(w, k, {k.pos.x - Sgn(o.pos.x - k.pos.x) * 3, k.pos.y}, in, true); }
        else if (!longGun && bd > want) MoveToward(w, k, o.pos, in, true);
        int ammo = d->kind == "thrown" ? w.items[k.weapon].count : w.items[k.weapon].ammo;
        bool shoot = los && bd < std::max(want * 2.2f, 6.0f) && R() < (skill >= 2 ? 0.9f : skill == 1 ? 0.5f : 0.25f);
        if (ammo <= 0) shoot = bd < 7 && los;   // (empty: throw it at them)
        if (d->hold) in.fire = shoot; else in.fire = shoot && !fireWas;
        if (skill >= 1 && o.punchT > 0 && bd < 1.2f && R() < 0.15f * skill) in.moveY = -1;
        return;
    }
    // fists, or a melee weapon: close in and swing
    in.aim = Vector2Normalize(Vector2Subtract(at, from));
    float reach = d ? 0.45f + d->reach : 0.85f;
    if (bd > (d ? 0.55f : reach * 0.8f)) MoveToward(w, k, o.pos, in, true);   // (an armed bot keeps pressing in: a retreating target doesn't get away)
    float react = skill >= 2 ? 0.9f : skill == 1 ? 0.55f : 0.3f;
    if (bd < reach + (d ? 0.35f : 0.25f) && fabsf(o.pos.y - k.pos.y) < 1.0f && R() < react) in.fire = !fireWas;
    if (skill >= 1 && o.punchT > 0 && bd < 1.2f && R() < 0.15f * skill) in.moveY = -1;
    // a skilled bot blocks: a punch toward an incoming bullet
    if (skill >= 2 && !d) for (const auto& b : w.bullets) if (b.owner != me && Vector2Distance(b.p, k.pt[J_HAND_R].p) < 1.2f && Vector2DotProduct(b.v, Vector2Subtract(k.pt[J_NECK].p, b.p)) > 0 && R() < 0.5f) { in.aim = Vector2Normalize(Vector2Negate(b.v)); in.fire = !fireWas; break; }
}
// ---------------------------------------------------------------- --scuffle-test
static int Fails = 0;
int ScuffleArmsChecks();
int ScuffleStageChecks();
int ScuffleWorldChecks();
int ScuffleArsenalChecks();
int ScuffleRulesChecks();
int ScuffleModeChecks();
static void Check(bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) Fails++; }
static void Run(World& w, float seconds, void (*fn)(World&) = nullptr) { int n = (int)(seconds / STEP); for (int i = 0; i < n; i++) { if (fn) fn(w); w.Step(); } }
static Stage Flat(int w = 40, int h = 18) { std::vector<std::string> rows(h, std::string(w, '.')); rows[h - 1] = std::string(w, '#'); rows[h - 2] = std::string(w, '#'); return StageFromText(rows, "Flat"); }
int RunScuffleTest() {
    Fails = 0;
    printf("Scuffle: stage 1 (the ragdoll)\n");
    Stage s = StoneStage();
    Check(s.w == 32 && s.h == 18 && s.spawns.size() >= 4, TextFormat("the stone stage: %d x %d tiles, %d spawns", s.w, s.h, (int)s.spawns.size()));
    // standing on the floor
    { World w; w.Init(Flat(), 1, 1); Stick& k = w.sticks[0]; k.pos = {5, 3}; Run(w, 1.0f);
      Check(k.grounded && fabsf(k.pos.y - 2 * TILE) < 0.05f && k.st == S_STAND, TextFormat("a stick lands and stands on the floor (feet %.2f)", k.pos.y));
      float headY = k.pt[J_HEAD].p.y - k.pos.y; Check(headY > 1.4f && headY < 1.75f, TextFormat("its ragdoll stands up with it (head %.2f m up)", headY)); }
    // running: 8 m/s
    { World w; w.Init(Flat(), 1, 1); Stick& k = w.sticks[0]; k.pos = {3, 1.2f}; Run(w, 0.5f); float x0 = k.pos.x;
      Run(w, 1.0f, [](World& w) { w.sticks[0].in.moveX = 1; });
      Check(fabsf(k.vel.x - 8) < 0.3f && k.pos.x - x0 > 6, TextFormat("runs at 8 m/s (%.1f; %.1f m in a second)", k.vel.x, k.pos.x - x0)); }
    // the jump: a 3 m hop held, lower tapped
    { World w; w.Init(Flat(), 1, 1); Stick& k = w.sticks[0]; k.pos = {5, 1.2f}; Run(w, 0.5f); float y0 = k.pos.y, top = y0;
      for (int i = 0; i < 200; i++) { k.in.jump = true; w.Step(); top = std::max(top, k.pos.y); }
      Check(top - y0 > 2.6f && top - y0 < 3.4f, TextFormat("a held jump: %.2f m", top - y0));
      Run(w, 1.0f, [](World& w) { w.sticks[0].in.jump = false; }); top = y0;
      for (int i = 0; i < 200; i++) { k.in.jump = i < 8; w.Step(); top = std::max(top, k.pos.y); }
      Check(top - y0 < 1.8f && top - y0 > 0.4f, TextFormat("a tapped jump: %.2f m", top - y0)); }
    // the wall: climb 3 m/s for 1.5 s, then slide; jump off at 45 degrees
    { std::vector<std::string> rows(18, std::string(30, '.')); for (int y = 0; y < 18; y++) rows[y][20] = '#'; rows[17] = std::string(30, '#'); rows[16] = std::string(30, '#');
      World w; w.Init(StageFromText(rows, "Wall"), 1, 1); Stick& k = w.sticks[0]; k.pos = {11.0f, 1.2f}; Run(w, 0.5f);
      for (int i = 0; i < 40; i++) { k.in.moveX = 1; k.in.jump = true; w.Step(); }
      float y0 = k.pos.y; int climbFrames = 0;
      for (int i = 0; i < 120; i++) { k.in.moveX = 1; k.in.jump = false; w.Step(); climbFrames += k.st == S_WALL && k.vel.y > 2.5f; }
      Check(climbFrames > 80 && k.pos.y > y0 + 2.0f, TextFormat("holding toward a wall: climbs (%.1f m in a second)", k.pos.y - y0));
      for (int i = 0; i < 120; i++) { k.in.moveX = 1; w.Step(); }
      Check(k.st == S_WALL && k.vel.y <= 0, "after 1.5 s it slides");
      k.in.jump = true; w.Step(); float ang = atan2f(k.vel.y, -k.vel.x) * RAD2DEG;
      Check(k.vel.x < -6 && fabsf(ang - 45) < 8, TextFormat("a wall jump pushes off at %.0f degrees", ang)); }
    // two sticks and fists: the jab (10, about 1.5 m), the haymaker (20, about 3 m)
    { World w; w.Init(Flat(), 2, 1); Stick& a = w.sticks[0]; Stick& b = w.sticks[1]; a.pos = {5, 1.2f}; b.pos = {5.75f, 1.2f}; a.face = 1; b.face = -1; Run(w, 0.6f);
      float bx = b.pos.x; a.in.aim = {1, 0}; a.in.fire = true; w.Step(); a.in.fire = false;
      Run(w, 1.2f);
      Check(b.hp == 90, TextFormat("a jab: 10 damage (hp %.0f)", b.hp));
      Check(b.pos.x - bx > 0.9f && b.pos.x - bx < 2.4f, TextFormat("and about 1.5 m of knockback (%.2f m)", b.pos.x - bx));
      // three in a row: the third is the haymaker
      b.pos = {a.pos.x + 0.75f, 1.2f}; b.vel = {0, 0}; Run(w, 0.3f); float hp0 = b.hp;
      for (int n = 0; n < 3; n++) { b.pos.x = a.pos.x + 0.72f; b.vel.x = 0; a.in.fire = true; w.Step(); a.in.fire = false; Run(w, n < 2 ? 0.27f : 0.0f); }
      float bx2 = b.pos.x; Run(w, 1.5f);
      Check(hp0 - b.hp >= 39 && hp0 - b.hp <= 41, TextFormat("three in a row: the third is a haymaker (%.0f damage in all)", hp0 - b.hp));
      Check(b.pos.x - bx2 > 2.0f, TextFormat("and the haymaker carries about 3 m (%.1f)", b.pos.x - bx2)); }
    // ducking: the head goes under the jab's line
    { World w; w.Init(Flat(), 2, 1); Stick& a = w.sticks[0]; Stick& b = w.sticks[1]; a.pos = {5, 1.2f}; b.pos = {5.7f, 1.2f}; Run(w, 0.5f);
      for (int i = 0; i < 30; i++) { b.in.moveY = -1; w.Step(); }
      a.in.aim = {1, 0.15f}; a.in.fire = true; b.in.moveY = -1; w.Step(); a.in.fire = false; for (int i = 0; i < 60; i++) { b.in.moveY = -1; w.Step(); }
      Check(b.hp == 100, "a ducked stick's head is under the shot line: the jab misses"); }
    // the kick: fire while diving (15, a ragdoll)
    { World w; w.Init(Flat(), 2, 1); Stick& a = w.sticks[0]; Stick& b = w.sticks[1]; a.pos = {4, 1.2f}; b.pos = {6.2f, 1.2f}; Run(w, 0.5f);
      a.pos.x = 4.6f; a.in.jump = true; for (int i = 0; i < 10; i++) w.Step(); a.in.jump = false; a.in.moveY = -1; a.in.fire = true; w.Step(); a.in.moveY = 0;
      for (int i = 0; i < 60 && b.hp == 100; i++) { a.in.fire = (i % 2) == 0; w.Step(); }
      Check(b.hp == 85 && (b.st == S_RAGDOLL || b.vel.x > 3), TextFormat("a diving kick: 15 and over they go (hp %.0f)", b.hp)); }
    // the grab and the throw: a thrown stick is a projectile (15 to both)
    { World w; w.Init(Flat(), 3, 1); Stick& a = w.sticks[0]; Stick& b = w.sticks[1]; Stick& c = w.sticks[2]; a.pos = {5, 1.2f}; b.pos = {5.6f, 1.2f}; c.pos = {9.5f, 1.2f}; Run(w, 0.5f);
      a.in.aim = {1, 0}; a.in.fire = true; for (int i = 0; i < 60 && a.grabbing < 0; i++) w.Step();
      Check(a.grabbing == 1 && b.grabbedBy == 0, "hold fire against a stick: a grab");
      a.in.aim = Vector2Normalize({1, 0.05f}); a.in.fire = false; w.Step();
      for (int i = 0; i < 120 && c.hp == 100; i++) w.Step();
      Check(c.hp == 85 && b.hp <= 85, TextFormat("thrown into a third: 15 each (%.0f, %.0f)", b.hp, c.hp)); }
    // death: a ragdoll that stays on the stage
    { World w; w.Init(Flat(), 2, 1); Stick& b = w.sticks[1]; b.pos = {6, 1.2f}; Run(w, 0.4f);
      w.Hit(b, 0, 200, {1, 0}, 4, true, "a test"); Run(w, 2.0f);
      Check(!b.alive && b.present && b.pt[J_PELVIS].p.y > 1.0f && b.pt[J_PELVIS].p.y < 1.8f && w.sticks[0].kills == 1, "dead: a limp ragdoll on the floor, a kill for the killer"); }
    // falling out: off the bottom is death
    { std::vector<std::string> rows(18, std::string(30, '.')); rows[17] = "##########..........##########"; rows[16] = rows[17];
      World w; w.Init(StageFromText(rows, "Gap"), 1, 1); Stick& k = w.sticks[0]; k.pos = {9, 4}; Run(w, 2.5f);
      Check(!k.alive && k.cause == "fell out", "off the bottom of the stage: gone"); }
    // a 10 m fall stuns for 0.5 s
    { std::vector<std::string> rows(30, std::string(20, '.')); rows[29] = std::string(20, '#'); rows[28] = rows[29];
      World w; w.Init(StageFromText(rows, "Drop"), 1, 1); Stick& k = w.sticks[0]; k.pos = {6, 1.2f + 11}; k.fallTop = k.pos.y;
      bool stunned = false; for (int i = 0; i < 400; i++) { w.Step(); stunned |= k.st == S_RAGDOLL; }
      Check(stunned && k.alive, "a 10 m fall: a moment flat on the floor"); }
    // the stone stage: two bots, fists only: someone wins
    { int done = 0; float total = 0; std::string why;
      for (uint32_t seed = 1; seed <= 12; seed++) {
          World w; w.Init(StoneStage(), 2, seed); uint32_t r[2] = {seed * 7 + 1, seed * 13 + 5}; float tt = 0;
          while (w.Living() > 1 && tt < 90) { for (int i = 0; i < 2; i++) BotInput(w, i, w.sticks[i].in, r[i], 2); w.Step(); tt += STEP; }
          if (getenv("DEPTH_SFTRACE")) for (const auto& k : w.sticks) printf("    seed %u stick %d: %s at (%.1f, %.1f) %s weapon %d\n", seed, k.id, k.alive ? "alive" : "dead", k.pt[J_PELVIS].p.x, k.pt[J_PELVIS].p.y, k.cause.c_str(), k.weapon);
          if (w.Living() <= 1) { done++; total += tt; for (const auto& k : w.sticks) if (!k.alive && seed <= 4) why += TextFormat(" [%.1fs %s]", tt, k.cause.c_str()); }
      }
      Check(done >= 10, TextFormat("two Sharp bots, fists only, on the stone stage: %d of 12 rounds end (%.0f s on average)%s", done, done ? total / done : 0, why.c_str())); }
    Fails += ScuffleArmsChecks();
    Fails += ScuffleStageChecks();
    Fails += ScuffleWorldChecks();
    Fails += ScuffleArsenalChecks();
    Fails += ScuffleRulesChecks();
    Fails += ScuffleModeChecks();
    printf(Fails ? "Scuffle: %d check(s) FAILED\n" : "Scuffle: all checks passed\n", Fails);
    return Fails ? 1 : 0;
}

// ---------------------------------------------------------------- --scuffle-determinism <seed>
int RunScuffleDeterminism(uint32_t seed) {
    auto run = [&](std::vector<uint64_t>& hashes) {
        World w; w.Init(StoneStage(), 4, seed); uint32_t r[4] = {seed + 1, seed + 2, seed + 3, seed + 4};
        for (int f = 0; f < 120 * 40; f++) { for (int i = 0; i < 4; i++) BotInput(w, i, w.sticks[i].in, r[i], 1 + i % 2); w.Step(); if (f % 60 == 0) hashes.push_back(w.Hash()); }
        hashes.push_back(w.Hash());
    };
    std::vector<uint64_t> a, b; run(a); run(b);
    bool same = a == b;
    printf("scuffle-determinism seed %u: %d checkpoints over 40 s with four bots: %s (final %016llx)\n", seed, (int)a.size(), same ? "identical" : "DIVERGED", (unsigned long long)a.back());
    return same ? 0 : 1;
}

} // namespace sf
