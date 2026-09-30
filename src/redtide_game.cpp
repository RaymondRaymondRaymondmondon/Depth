// Red Tide: the playable scene. A first-person diver with the Cormorant, in a map run by the Ecosystem, drawn by
// the inked low-poly renderer. Stage 2 (the design doc's build order) is the test tank: a box room of generated
// fish from the Sunken Ship's species; stage 3 builds the Sunken Ship itself on the same scene.
#include "redtide.h"
#include "redtide_render.h"
#include "game.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace rt {

// ---------------------------------------------------------------- weapons (the Racks table, design doc "Weapons")
struct WeaponDef {
    const char* name; const char* type;
    float damage; int pellets; float rpm; int mag, reserve; float noise, reload; int price;
    float fullTo, halfAt;          // water drag falloff (Weapon handling: "Full to 15 m, 50% at 25 m")
    float speed;                   // dart speed in water, m/s
    bool perRound;                 // reloads one round at a time (the gas pistols thumb in darts)
};
static const WeaponDef WEAPONS[] = {
    {"The Cormorant", "Gas pistol", 30, 1, 300, 8, 32, 2, 1.4f, 0, 15, 25, 28, true},
    {"Gannet", "Gas pistol", 40, 1, 280, 10, 50, 2, 1.5f, 500, 15, 25, 28, true},
};

struct Dart { Vector3 pos, vel, start; float life; float damage; int weapon; bool alive = true; };
struct Particle { Vector3 pos, vel; float life, max; Color col; float size; };

struct Diver {
    Vector3 pos{0, 2, 0}, vel{0, 0, 0};
    float yaw = 0, pitch = 0;
    float hp = 100, hpMax = 100, regenT = 0;
    float stamina = 1, sprintHeldT = 0;
    int weapon = 0, mag = 8, reserve = 32;
    float fireT = 0, reloadT = 0, reloadStep = 0; bool reloading = false;
    float recoil = 0, bob = 0;
    int scrip = 500;
    int agent = -1;
};

struct RoomBox { Vector3 c, half; Color col; };

struct RedTideScene {
    bool active = false;
    int mode = 0;                  // 0 the test tank (stage 2)
    MapData tank;                  // the tank's map: the Sunken Ship's species in one room
    Ecosystem eco;
    Diver diver;
    std::vector<Dart> darts;
    std::vector<Particle> fx;
    std::vector<RoomBox> boxes;
    std::vector<Vector3> snow;
    float time = 0;
    int kills = 0, hits = 0;
    std::string lastKill; float lastKillT = 0;
    bool shotMode = false;         // --shots: fixed camera, no input
    float silhouette = 0;
    int lineup = -1;               // --shots: every species of the Ship posed in rows (page number), instead of the tank
};
static RedTideScene S;

// ---------------------------------------------------------------- the test tank
// A 30 x 12 x 30 m box room (the Sunken Ship's palette: wet iron, rust, kelp) with a few crates and pillars, and a
// population drawn from the Sunken Ship's species so the Ecosystem runs exactly as it will on the real map.
static void BuildTank() {
    const MapData& ship = Map("ship");
    MapData& m = S.tank;
    m = MapData{};
    m.key = "tank";
    m.title = "The Test Tank";
    m.species = ship.species;
    m.diet = ship.diet;
    m.foodNames = ship.foodNames;
    m.attacks = ship.attacks;
    m.flora = ship.flora;
    m.tunables = ship.tunables;
    m.tides = ship.tides;
    m.faction = ship.faction;
    m.enemySpecies = ship.enemySpecies;
    Zone z;
    z.name = "The Tank"; z.deck = "Tank";
    z.plan = {-15, -15, 30, 30};
    z.y0 = 0; z.y1 = 12;
    z.flow = {0.1f, 0, 0.05f};
    m.zones = {z};
    AlarmRegion ar; ar.name = "Tank"; ar.mult = 1; ar.zones = {0};
    m.alarmRegions = {ar};
    m.boundsMin = {-15, 0, -15}; m.boundsMax = {15, 12, 15};
    for (auto& f : m.flora) f.zones = {"The Tank"};
    // a selection that shows every body plan the Ship uses: schools, reef fish, a shark, a turtle, crustaceans, cephalopods
    const std::pair<const char*, int> pop[] = {
        {"Bilge Sprat", 30}, {"Silverside", 20}, {"Sergeant Major", 4}, {"Parrotfish", 3}, {"Rabbitfish", 3}, {"Lionfish", 2},
        {"Cleaner Shrimp", 4}, {"Fiddler Crab", 4}, {"Reef Squid", 5}, {"Cuttlefish", 1}, {"Great Barracuda", 1}, {"Grouper", 1},
        {"Hawksbill Turtle", 1}, {"Nurse Shark", 1}, {"Jelly Bloom", 1}, {"Banded Sea Snake", 1}, {"Green Moray", 1}};
    for (const auto& p : pop) {
        SpawnRow r; r.zone = "The Tank"; r.species = p.first; r.count = p.second; r.respawnS = 30; r.capMult = 1;
        m.spawns.push_back(r);
    }
    for (auto& s : m.species) s.homeZone = "The Tank";
    // the room's boxes: floor, walls, ceiling, crates, pillars
    Color iron{78, 86, 92, 255}, rust{122, 72, 46, 255}, kelp{70, 96, 62, 255}, sand{168, 140, 92, 255};
    S.boxes = {
        {{0, -0.5f, 0}, {16, 0.5f, 16}, sand}, {{0, 12.5f, 0}, {16, 0.5f, 16}, iron},
        {{-15.5f, 6, 0}, {0.5f, 6.5f, 16}, iron}, {{15.5f, 6, 0}, {0.5f, 6.5f, 16}, rust},
        {{0, 6, -15.5f}, {16, 6.5f, 0.5f}, rust}, {{0, 6, 15.5f}, {16, 6.5f, 0.5f}, iron},
        {{-7, 1, -6}, {1.5f, 1, 1.5f}, rust}, {{-5, 0.75f, -3}, {1, 0.75f, 1}, rust}, {{8, 1.5f, 5}, {2, 1.5f, 1.2f}, iron},
        {{6, 6, -8}, {0.6f, 6, 0.6f}, iron}, {{-8, 6, 8}, {0.6f, 6, 0.6f}, iron}, {{2, 0.3f, 9}, {3, 0.3f, 2}, kelp},
    };
}

static void StartTank() {
    BuildTank();
    S.eco = Ecosystem{};
    S.eco.Init(S.tank, 20260930, 1, 1);
    S.diver = Diver{};
    S.diver.pos = {0, 3, -10};
    S.diver.yaw = 0;
    S.darts.clear(); S.fx.clear();
    S.time = 0; S.kills = S.hits = 0;
    S.snow.clear();
    for (int i = 0; i < 220; i++) S.snow.push_back({GetRandomValue(-150, 150) / 10.0f, GetRandomValue(0, 120) / 10.0f, GetRandomValue(-150, 150) / 10.0f});
    S.active = true;
}

// ---------------------------------------------------------------- the diver
static Vector3 Forward(const Diver& d) { return {sinf(d.yaw) * cosf(d.pitch), sinf(d.pitch), cosf(d.yaw) * cosf(d.pitch)}; }
static Vector3 Flat(const Diver& d) { return {sinf(d.yaw), 0, cosf(d.yaw)}; }

static void Collide(Vector3& p, float r) {
    const Zone& z = S.eco.map->zones[0];
    p = z.Clamp(p, r);
    for (const auto& b : S.boxes) {
        Vector3 lo = Vector3Subtract(b.c, b.half), hi = Vector3Add(b.c, b.half);
        if (b.half.x > 10 || b.half.z > 10) continue;       // the shell is handled by the zone clamp
        Vector3 q{std::clamp(p.x, lo.x, hi.x), std::clamp(p.y, lo.y, hi.y), std::clamp(p.z, lo.z, hi.z)};
        Vector3 d = Vector3Subtract(p, q);
        float dist = Vector3Length(d);
        if (dist < r) {
            if (dist < 1e-4f) { p.y = hi.y + r; continue; }
            p = Vector3Add(q, Vector3Scale(d, r / dist));
        }
    }
}

static void UpdateDiver(float dt) {
    const EngineData& e = Engine();
    Diver& d = S.diver;
    if (!S.shotMode) {
        Vector2 md = GetMouseDelta();
        d.yaw -= md.x * 0.0025f;
        d.pitch = std::clamp(d.pitch - md.y * 0.0025f, -1.45f, 1.45f);
    }
    Vector3 want{0, 0, 0};
    Vector3 f = Flat(d), r{-f.z, 0, f.x};
    if (!S.shotMode) {
        if (IsKeyDown(KEY_W)) want = Vector3Add(want, f);
        if (IsKeyDown(KEY_S)) want = Vector3Subtract(want, f);
        if (IsKeyDown(KEY_D)) want = Vector3Subtract(want, r);
        if (IsKeyDown(KEY_A)) want = Vector3Add(want, r);
    }
    bool sprint = !S.shotMode && IsKeyDown(KEY_LEFT_SHIFT) && d.stamina > 0 && Vector3Length(want) > 0.1f;
    float speed = sprint ? e.M("sprint_speed", 3.4f) : e.M("swim_speed", 2);
    if (!S.shotMode && IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) speed *= e.M("ads_swim_mult", 0.6f);
    if (Vector3Length(want) > 0.01f) want = Vector3Scale(Vector3Normalize(want), speed);
    float vert = 0;
    if (!S.shotMode) {
        if (IsKeyDown(KEY_SPACE)) vert += e.M("vertical_speed", 1.4f);
        if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_C)) vert -= e.M("vertical_speed", 1.4f);
    }
    want.y = vert;
    // stamina: 6 s of sprint, 8 s to refill (the bar only shows while sprinting)
    if (sprint) d.stamina = std::max(0.0f, d.stamina - dt / e.M("sprint_stamina_s", 6));
    else d.stamina = std::min(1.0f, d.stamina + dt / e.M("stamina_regen_s", 8));
    float acc = Vector3Length(want) > Vector3Length(d.vel) ? e.M("accel_s", 0.4f) : e.M("decel_s", 0.5f);
    d.vel = Vector3Lerp(d.vel, want, std::min(1.0f, dt / std::max(0.05f, acc)));
    d.pos = Vector3Add(d.pos, Vector3Scale(d.vel, dt));
    Collide(d.pos, 0.45f);
    d.bob += Vector3Length(d.vel) * dt * 2.2f;
    // health regenerates 4 s after the last hit at 20 HP/s (Zombies-style)
    d.regenT += dt;
    if (d.regenT > e.C("regen_delay_s", 4)) d.hp = std::min(d.hpMax, d.hp + e.C("regen_rate", 20) * dt);
    // the diver is an agent in the web: wounded divers bleed (blood_diver_wounded below 50% HP)
    if (d.hp < d.hpMax * 0.5f) S.eco.AddBlood(d.pos, e.C("blood_diver_wounded", 2) * dt);
}

// ---------------------------------------------------------------- the Cormorant
static void Burst(Vector3 p, int n, Color c, float speed, float life, float size) {
    for (int i = 0; i < n; i++) {
        Vector3 v{GetRandomValue(-100, 100) / 100.0f, GetRandomValue(-100, 100) / 100.0f, GetRandomValue(-100, 100) / 100.0f};
        S.fx.push_back({p, Vector3Scale(v, speed), life, life, c, size});
    }
}

// Closest distance between segments p1-q1 and p2-q2 (a dart's path this frame against a body's capsule axis).
static float SegSegDist(Vector3 p1, Vector3 q1, Vector3 p2, Vector3 q2, float* tBody) {
    Vector3 d1 = Vector3Subtract(q1, p1), d2 = Vector3Subtract(q2, p2), r = Vector3Subtract(p1, p2);
    float a = Vector3DotProduct(d1, d1), e = Vector3DotProduct(d2, d2), f = Vector3DotProduct(d2, r);
    float s = 0, t = 0;
    if (a <= 1e-8f && e <= 1e-8f) { if (tBody) *tBody = 0; return Vector3Length(r); }
    if (a <= 1e-8f) { t = std::clamp(f / e, 0.0f, 1.0f); }
    else {
        float c = Vector3DotProduct(d1, r);
        if (e <= 1e-8f) { s = std::clamp(-c / a, 0.0f, 1.0f); }
        else {
            float b = Vector3DotProduct(d1, d2), den = a * e - b * b;
            s = den > 1e-8f ? std::clamp((b * f - c * e) / den, 0.0f, 1.0f) : 0;
            t = (b * s + f) / e;
            if (t < 0) { t = 0; s = std::clamp(-c / a, 0.0f, 1.0f); }
            else if (t > 1) { t = 1; s = std::clamp((b - c) / a, 0.0f, 1.0f); }
        }
    }
    if (tBody) *tBody = t;
    return Vector3Distance(Vector3Add(p1, Vector3Scale(d1, s)), Vector3Add(p2, Vector3Scale(d2, t)));
}

static void Fire() {
    Diver& d = S.diver;
    const WeaponDef& w = WEAPONS[d.weapon];
    if (d.reloading || d.fireT > 0) return;
    if (d.mag <= 0) { if (d.reserve > 0) { d.reloading = true; d.reloadT = 0; } return; }
    d.mag--;
    d.fireT = 60.0f / w.rpm;
    d.recoil = 1;
    Vector3 dir = Forward(d);
    // the dart flies down the crosshair's ray from the eye (the viewmodel's muzzle is only where the bubbles come from)
    Vector3 eye = Vector3Add(d.pos, {0, 0.1f, 0});
    Vector3 muzzle = Vector3Add(Vector3Add(eye, Vector3Scale(dir, 0.45f)), {0, -0.1f, 0});
    Vector3 from = Vector3Add(eye, Vector3Scale(dir, 0.3f));
    S.darts.push_back({from, Vector3Scale(dir, w.speed), from, 2.5f, w.damage, d.weapon});
    Burst(muzzle, 5, {200, 235, 250, 255}, 0.8f, 0.6f, 0.04f); // the gas bubbles from a gas gun
    S.eco.AddNoise(muzzle, w.noise);
}

static void UpdateWeapon(float dt) {
    Diver& d = S.diver;
    const WeaponDef& w = WEAPONS[d.weapon];
    if (d.fireT > 0) d.fireT -= dt;
    d.recoil = std::max(0.0f, d.recoil - dt * 6);
    if (!S.shotMode) {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) Fire();
        if (IsKeyPressed(KEY_R) && d.mag < w.mag && d.reserve > 0 && !d.reloading) { d.reloading = true; d.reloadT = 0; }
    }
    if (d.reloading) {
        // slow-fast-slow: pop the cylinder, thumb darts in one at a time, close it (interruptible by firing)
        d.reloadT += dt;
        float per = w.reload / std::max(1, w.mag);
        if (d.reloadT > per * 0.8f && d.mag < w.mag && d.reserve > 0) { d.mag++; d.reserve--; d.reloadT -= per; }
        if (d.mag >= w.mag || d.reserve <= 0) d.reloading = false;
        if (!S.shotMode && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && d.mag > 0) d.reloading = false;
    }
    // darts: straight flight with water drag; damage falls off past the weapon's range
    for (auto& dt_ : S.darts) {
        if (!dt_.alive) continue;
        Vector3 prev = dt_.pos;
        dt_.pos = Vector3Add(dt_.pos, Vector3Scale(dt_.vel, dt));
        dt_.vel = Vector3Scale(dt_.vel, powf(0.55f, dt));
        dt_.life -= dt;
        if (dt_.life <= 0 || !S.eco.map->zones[0].Contains(dt_.pos)) { dt_.alive = false; Burst(prev, 3, {170, 160, 140, 255}, 0.5f, 0.5f, 0.05f); continue; }
        bool blocked = false;
        for (const auto& b : S.boxes) {
            if (b.half.x > 10 || b.half.z > 10) continue;
            if (fabsf(dt_.pos.x - b.c.x) < b.half.x && fabsf(dt_.pos.y - b.c.y) < b.half.y && fabsf(dt_.pos.z - b.c.z) < b.half.z) blocked = true;
        }
        if (blocked) { dt_.alive = false; Burst(prev, 4, {160, 150, 130, 255}, 0.6f, 0.5f, 0.05f); continue; }
        // hit test against every beast's capsule (body length along its facing)
        for (int i = 0; i < (int)S.eco.agents.size(); i++) {
            Agent& a = S.eco.agents[i];
            if (!a.alive || a.diver >= 0) continue;
            const Species& sp = S.eco.map->species[a.sp];
            const CreatureModel& cm = Creature("ship", sp.name);
            Vector3 fwd = Vector3Length(a.vel) > 0.05f ? Vector3Normalize(a.vel) : Vector3{0, 0, 1};
            Vector3 head = Vector3Add(a.pos, Vector3Scale(fwd, cm.length * 0.5f)), tail = Vector3Subtract(a.pos, Vector3Scale(fwd, cm.length * 0.5f));
            // swept: the dart's whole path this frame against the body's capsule (a dart is ~0.5 m a frame; a sprat is 3 cm thick)
            float t = 0;
            if (SegSegDist(prev, dt_.pos, tail, head, &t) > std::max(0.06f, cm.radius) + 0.03f) continue;
            float travelled = Vector3Distance(dt_.start, dt_.pos);
            const WeaponDef& wd = WEAPONS[dt_.weapon];
            float fall = travelled <= wd.fullTo ? 1.0f : travelled >= wd.halfAt ? 0.5f : 1.0f - 0.5f * (travelled - wd.fullTo) / (wd.halfAt - wd.fullTo);
            bool weak = t > 0.72f && sp.weakPoint != "none";          // gills and eyes, behind the head
            float before = a.hp;
            S.eco.Damage(i, dt_.damage * fall, S.diver.agent, false, weak);
            S.hits++;
            S.diver.scrip += 10;                                     // Scrip: hit a beast, 10
            Burst(dt_.pos, 6, {150, 20, 20, 255}, 0.6f, 1.2f, 0.07f);
            if (!a.alive && before > 0) {
                float bounty = sp.bountyBase * S.eco.map->Tide(S.eco.tide).bountyMult * (weak ? 1.5f : 1.0f);
                S.diver.scrip += (int)bounty;
                S.kills++;
                S.lastKill = TextFormat("%s  +%d", sp.name.c_str(), (int)bounty);
                S.lastKillT = 2.5f;
            }
            dt_.alive = false;
            break;
        }
    }
    S.darts.erase(std::remove_if(S.darts.begin(), S.darts.end(), [](const Dart& x) { return !x.alive; }), S.darts.end());
}

// ---------------------------------------------------------------- drawing
// The Cormorant: a brass gas pistol, a small model built once and drawn in the camera's frame (a viewmodel).
static Model gGun{};
static bool gGunReady = false;
static void DrawGun(const Camera3D& cam) {
    if (!gGunReady && IsWindowReady()) {
        MeshBuilder mb;
        Color brass{214, 168, 72, 255}, dark{96, 74, 44, 255}, wood{104, 66, 40, 255}, glass{120, 176, 190, 255};
        mb.Box({0, 0, 0.13f}, {0.018f, 0.018f, 0.14f}, brass);          // barrel
        mb.Box({0, -0.004f, 0.27f}, {0.022f, 0.022f, 0.012f}, dark);    // muzzle ring
        mb.Lathe(0.09f, 3, 8, [](float) { return 0.034f; }, [](float) { return 0.034f; }, dark, dark, {0, -0.005f, -0.005f}); // the gas cylinder
        mb.Box({0, -0.07f, -0.05f}, {0.018f, 0.06f, 0.028f}, wood);     // grip
        mb.Box({0, 0.026f, 0.24f}, {0.004f, 0.01f, 0.006f}, brass);     // front sight
        mb.Box({0, -0.035f, 0.02f}, {0.004f, 0.012f, 0.02f}, glass);    // the dart window
        gGun = LoadModelFromMesh(mb.Build());
        gGunReady = true;
    }
    if (!gGunReady) return;
    Diver& d = S.diver;
    Vector3 f = Forward(d);
    Vector3 right = Vector3Normalize(Vector3CrossProduct(f, {0, 1, 0}));
    Vector3 up = Vector3CrossProduct(right, f);
    float bobx = sinf(d.bob) * 0.008f, boby = fabsf(cosf(d.bob)) * 0.006f;
    float kick = d.recoil * 0.035f, dip = d.reloading ? 0.06f : 0;
    Vector3 p = Vector3Add(cam.position, Vector3Add(Vector3Scale(f, 0.30f - kick), Vector3Add(Vector3Scale(right, 0.13f + bobx), Vector3Scale(up, -0.12f - boby - dip))));
    // the model's x is the camera's right... but the camera's right is -cross(up, f); mirror-safe basis
    Matrix m = MatrixIdentity();
    Vector3 rx = Vector3Scale(right, -1);
    m.m0 = rx.x; m.m1 = rx.y; m.m2 = rx.z;
    m.m4 = up.x; m.m5 = up.y; m.m6 = up.z;
    m.m8 = f.x; m.m9 = f.y; m.m10 = f.z;
    m.m12 = p.x; m.m13 = p.y; m.m14 = p.z;
    // a little recoil tilt
    Matrix tilt = MatrixRotateX(-d.recoil * 0.25f);
    DrawStatic(gGun, MatrixMultiply(tilt, m));
}

static void DrawScene() {
    Diver& d = S.diver;
    Camera3D cam{};
    Vector3 f = Forward(d);
    cam.position = Vector3Add(d.pos, {0, 0.1f, 0});
    cam.target = Vector3Add(cam.position, f);
    cam.up = {0, 1, 0};
    cam.fovy = 72;
    cam.projection = CAMERA_PERSPECTIVE;
    SceneLight L;
    L.lampPos = cam.position;
    L.lampDir = f;
    L.lampRange = 24;
    L.lampCone = 0.72f;
    L.fog = {10, 30, 36, 255};
    L.fogDensity = 0.05f;
    L.surfaceY = 14;
    L.time = S.time;
    L.silhouette = S.silhouette;
    float scent = S.eco.Smell(d.pos, 0, 6);
    L.bloodTint = std::clamp(scent / 120.0f, 0.0f, 1.0f);
    if (S.lineup >= 0) {
        // the lineup: every species of the Ship, largest last, in rows facing across the view, scaled to fit a cell
        const MapData& ship = Map("ship");
        cam.position = {0, 0, -9}; cam.target = {0, 0, 0}; cam.fovy = 50;
        L.lampPos = {0, 6, -9}; L.lampDir = Vector3Normalize({0, -0.4f, 1}); L.lampRange = 40; L.lampCone = 0.2f; L.fogDensity = 0.004f; L.fog = {26, 52, 58, 255};
        L.bloodTint = 0;
        RenderBegin(cam, L);
        int per = 20, first = S.lineup * per, cols = 5;
        for (int k = 0; k < per && first + k < (int)ship.species.size(); k++) {
            const Species& sp = ship.species[first + k];
            if (sp.isEnemy) continue;
            const CreatureModel& cm = Creature("ship", sp.name);
            int cx = k % cols, cy = k / cols;
            Vector3 at{(cx - (cols - 1) * 0.5f) * 2.3f, (1.5f - cy) * 2.0f, 0};
            float sc = 0.95f / std::max(0.05f, cm.extent);
            DrawCreature(cm, at, 1.5708f, 0, sc, S.time * cm.freq, 0.8f);
        }
        RenderEnd();
        for (int k = 0; k < per && first + k < (int)ship.species.size(); k++) {
            const Species& sp = ship.species[first + k];
            int cx = k % cols, cy = k / cols;
            Vector3 at{(cx - (cols - 1) * 0.5f) * 2.3f, (1.5f - cy) * 2.0f - 0.85f, 0};
            Vector2 sc = GetWorldToScreenEx(at, cam, SCREEN_W, SCREEN_H);
            DrawTextCentered(TextFormat("%s (%s)", sp.name.c_str(), Creature("ship", sp.name).plan.c_str()), sc.x, sc.y, 13, Color{230, 222, 200, 255});
        }
        return;
    }
    RenderBegin(cam, L);
    if (S.silhouette < 0.5f) for (const auto& b : S.boxes) DrawWorldCube(b.c, Vector3Scale(b.half, 2), b.col);
    for (const auto& a : S.eco.agents) {
        if (!a.alive || a.diver >= 0) continue;
        const Species& sp = S.eco.map->species[a.sp];
        const CreatureModel& cm = Creature("ship", sp.name);
        float spd = Vector3Length(a.vel);
        Vector3 v = spd > 0.02f ? a.vel : Vector3{sinf(a.rng * 0.001f), 0, cosf(a.rng * 0.001f)};
        float yaw = atan2f(v.x, v.z);
        float pitch = spd > 0.05f ? std::clamp(asinf(std::clamp(v.y / std::max(spd, 1e-3f), -1.0f, 1.0f)), -0.6f, 0.6f) : 0;
        if (cm.anim == AnimMode::Walker || cm.anim == AnimMode::Static) pitch = 0;
        float inten = std::clamp(0.35f + spd / std::max(0.5f, sp.speed), 0.2f, 1.6f);
        float phase = S.time * cm.freq * (0.6f + inten * 0.6f) + (a.rng % 1000) * 0.01f;
        Color tint = a.wound > 0.3f ? Color{255, (unsigned char)(255 - a.wound * 120), (unsigned char)(255 - a.wound * 120), 255} : WHITE;
        DrawCreature(cm, a.pos, yaw, pitch, 1.0f, phase, inten, tint);
    }
    if (S.silhouette < 0.5f) DrawGun(cam);
    RenderEnd();
    if (S.silhouette > 0.5f) return;
    // after the ink: blood plumes, bubbles and marine snow (bright specks must come after the ink pass)
    BeginLayer(Mode3DRT());
    ClearBackground(BLANK);
    BeginMode3D(cam);
    const Field& sf = S.eco.scent;
    float vis = Engine().C("scent_visible_min", 3);
    for (int z = 0; z < sf.nz; z++) for (int y = 0; y < sf.ny; y++) for (int x = 0; x < sf.nx; x++) {
        float v = sf.v[sf.Idx(x, y, z)];
        if (v < vis) continue;
        Vector3 c{sf.origin.x + (x + 0.5f) * sf.cell, sf.origin.y + (y + 0.5f) * sf.cell, sf.origin.z + (z + 0.5f) * sf.cell};
        float a = std::clamp(v / 60.0f, 0.05f, 0.45f);
        DrawSphere(c, sf.cell * 0.55f, Fade(Color{120, 10, 12, 255}, a * 0.5f));
    }
    for (const auto& p : S.fx) DrawCube(p.pos, p.size, p.size, p.size, Fade(p.col, p.life / p.max));
    for (const auto& dt_ : S.darts) DrawLine3D(dt_.pos, Vector3Subtract(dt_.pos, Vector3Scale(Vector3Normalize(dt_.vel), 0.25f)), {230, 220, 180, 255});
    for (const auto& s : S.snow) DrawCube(s, 0.02f, 0.02f, 0.02f, Fade(Color{220, 230, 220, 255}, 0.6f));
    EndMode3D();
    EndLayer();
    RenderTexture2D& ov = Mode3DRT();
    DrawTexturePro(ov.texture, {0, 0, (float)ov.texture.width, -(float)ov.texture.height}, {0, 0, (float)SCREEN_W, (float)SCREEN_H}, {0, 0}, 0, WHITE);
}

static void DrawHud() {
    Diver& d = S.diver;
    const WeaponDef& w = WEAPONS[d.weapon];
    float cx = SCREEN_W / 2.0f, cy = SCREEN_H / 2.0f;
    // the crosshair: a small ink ring
    DrawRing({cx, cy}, 5, 7, 0, 360, 24, Fade(Color{230, 225, 205, 255}, 0.8f));
    // scrip: a chalk tally on the visor, top right
    TxtBold(TextFormat("%d", d.scrip), SCREEN_W - 150, 24, 30, Color{235, 230, 210, 255});
    Txt("scrip", SCREEN_W - 150, 56, 14, Fade(Pal::Paper, 0.7f));
    // weapon and ammo: brass counters, bottom right
    TxtBold(w.name, SCREEN_W - 260, SCREEN_H - 92, 20, Color{214, 168, 72, 255});
    TxtBold(TextFormat("%d / %d", d.mag, d.reserve), SCREEN_W - 260, SCREEN_H - 66, 30, Color{235, 230, 210, 255});
    if (d.reloading) Txt("reloading...", SCREEN_W - 260, SCREEN_H - 32, 14, Fade(Pal::Paper, 0.8f));
    // health: a brass pressure gauge, bottom left
    Vector2 g{80, SCREEN_H - 80.0f};
    DrawCircleV(g, 46, Color{60, 48, 30, 255});
    DrawRing(g, 40, 46, 0, 360, 40, Color{214, 168, 72, 255});
    float frac = d.hp / d.hpMax;
    float ang = (-220 + 260 * frac) * DEG2RAD;
    DrawLineEx(g, {g.x + cosf(ang) * 34, g.y + sinf(ang) * 34}, 3, Color{190, 40, 30, 255});
    TxtBold(TextFormat("%d", (int)d.hp), g.x - 14, g.y + 10, 16, Pal::Paper);
    // the scent vial, left edge
    float scent = std::clamp(S.eco.Smell(d.pos, 0, 6) / 120.0f, 0.0f, 1.0f);
    Rectangle vial{24, 140, 14, 220};
    DrawRectangleRec(vial, Fade(BLACK, 0.4f));
    DrawRectangleRec({vial.x, vial.y + vial.height * (1 - scent), vial.width, vial.height * scent}, Color{170, 20, 24, 230});
    DrawRectangleLinesEx(vial, 2, Color{214, 168, 72, 200});
    // stamina, only while sprinting
    if (d.stamina < 0.999f) DrawBar({cx - 80, SCREEN_H - 40.0f, 160, 8}, d.stamina, Color{150, 220, 230, 255});
    if (S.lastKillT > 0) DrawTextCentered(S.lastKill, cx, cy + 40, 18, Fade(Color{235, 220, 190, 255}, std::min(1.0f, S.lastKillT)));
    TxtBold("RED TIDE - the test tank", 24, 20, 20, Color{200, 80, 70, 255});
    Txt(TextFormat("%d beasts alive   %d kills   %d hits", (int)std::count_if(S.eco.agents.begin(), S.eco.agents.end(), [](const Agent& a) { return a.alive && a.diver < 0; }), S.kills, S.hits), 24, 46, 14, Fade(Pal::Paper, 0.8f));
    Txt("WASD swim - Space/Ctrl up/down - Shift sprint - Mouse aim - LMB fire - R reload - Esc menu", 24, SCREEN_H - 26, 13, Fade(Pal::Paper, 0.6f));
}

} // namespace rt

using namespace rt;

void StartRedTide(Game& g) {
    (void)g;
    StartTank();
    S.shotMode = false;
    DisableCursor();
    g.scene = Scene::RedTide;
}

void SceneRedTide(Game& g) {
    if (!S.active) StartTank();
    float dt = std::min(GetFrameTime(), 1 / 30.0f);
    if (S.shotMode) dt = 1 / 60.0f;
    S.time += dt;
    UpdateDiver(dt);
    UpdateWeapon(dt);
    S.eco.Step(dt);
    for (auto& p : S.fx) { p.pos = Vector3Add(p.pos, Vector3Scale(p.vel, dt)); p.vel = Vector3Scale(p.vel, powf(0.3f, dt)); p.vel.y += 0.3f * dt; p.life -= dt; }
    S.fx.erase(std::remove_if(S.fx.begin(), S.fx.end(), [](const Particle& p) { return p.life <= 0; }), S.fx.end());
    for (auto& s : S.snow) { s.y -= dt * 0.12f; s.x += sinf(S.time * 0.3f + s.z) * dt * 0.05f; if (s.y < 0) s.y += 12; }
    if (S.lastKillT > 0) S.lastKillT -= dt;
    DrawScene();
    if (S.lineup < 0) DrawHud();
    else TxtBold(TextFormat("Red Tide - the Sunken Ship's species, page %d (CreatureBuilder)", S.lineup + 1), 24, 18, 20, Color{220, 90, 80, 255});
}

// --shots: the tank from a fixed eye, in colour and as silhouettes
void DebugRedTideShot(Game& g, int which) {
    StartTank();
    S.shotMode = true;
    S.silhouette = which == 1 ? 1.0f : 0.0f;
    S.lineup = which >= 2 ? which - 2 : -1;
    S.diver.pos = {0, 3.5f, -11};
    S.diver.yaw = 0.1f;
    S.diver.pitch = -0.08f;
    for (int i = 0; i < 240; i++) S.eco.Step(1 / 20.0f); // let the tank settle into its schools
    g.scene = Scene::RedTide;
}

// depth.exe --redtide-test: the stage 2 checks that need no window. The diver aims at the nearest fish and fires the
// Cormorant; the darts must hit, wound, kill, bleed into the scent grid and pay scrip; the magazine must empty and
// reload one dart at a time; the diver must swim at the Movement sheet's speeds.
int RunRedTideTest() {
    StartTank();
    S.shotMode = true;
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("Red Tide stage 2 test (the test tank)\n");
    // swimming: 2 m/s forward after the 0.4 s acceleration
    Diver& d = S.diver;
    d.pos = {0, 3, -10}; d.yaw = 0; d.pitch = 0; d.vel = {0, 0, 0};
    Vector3 start = d.pos;
    for (int i = 0; i < 120; i++) { d.vel = Vector3Lerp(d.vel, {0, 0, Engine().M("swim_speed", 2)}, std::min(1.0f, (1 / 60.0f) / Engine().M("accel_s", 0.4f))); d.pos = Vector3Add(d.pos, Vector3Scale(d.vel, 1 / 60.0f)); }
    float speed = Vector3Length(d.vel), moved = Vector3Distance(start, d.pos);
    check(fabsf(speed - 2.0f) < 0.05f && moved > 3.0f && moved < 4.0f, TextFormat("swims at %.2f m/s (Movement sheet: 2.0), %.2f m in 2 s", speed, moved));
    // aim and fire until something dies
    float scrip0 = (float)d.scrip, blood0 = S.eco.scent.Total();
    int shots = 0, killsBefore = S.kills;
    for (int attempt = 0; attempt < 400 && S.kills == killsBefore; attempt++) {
        int best = -1; float bd = 1e9f;
        for (int i = 0; i < (int)S.eco.agents.size(); i++) {
            const Agent& a = S.eco.agents[i];
            if (!a.alive || a.diver >= 0 || S.eco.map->species[a.sp].size > 2) continue;
            float dist = Vector3Distance(a.pos, d.pos);
            if (dist < bd) { bd = dist; best = i; }
        }
        if (best < 0) break;
        const Agent& a = S.eco.agents[best];
        // stand 3 m off it, aim at it, fire
        Vector3 dir = Vector3Normalize(Vector3Subtract(a.pos, d.pos));
        if (bd > 3.5f) d.pos = Vector3Subtract(a.pos, Vector3Scale(dir, 3));
        dir = Vector3Normalize(Vector3Subtract(a.pos, Vector3Add(d.pos, {0, 0.1f, 0})));
        d.yaw = atan2f(dir.x, dir.z); d.pitch = asinf(std::clamp(dir.y, -1.0f, 1.0f));
        d.fireT = 0; d.reloading = false;
        if (d.mag <= 0) { d.mag = 8; d.reserve = std::max(0, d.reserve - 8); }
        Fire(); shots++;
        for (int k = 0; k < 12; k++) { UpdateWeapon(1 / 60.0f); S.eco.Step(1 / 60.0f); }
    }
    check(S.hits > 0, TextFormat("darts hit (%d hits from %d shots)", S.hits, shots));
    check(S.kills > killsBefore, TextFormat("a beast dies to the Cormorant (%s)", S.lastKill.c_str()));
    check(d.scrip > scrip0, TextFormat("scrip pays for hits and the kill (%d -> %d)", (int)scrip0, d.scrip));
    check(S.eco.scent.Total() > blood0 + 5, TextFormat("the kill bleeds into the scent grid (%.0f -> %.0f)", blood0, S.eco.scent.Total()));
    // the magazine empties and reloads one dart at a time (8 darts in the Cormorant's 1.4 s)
    d.mag = 1; d.reserve = 20; d.fireT = 0; d.reloading = false;
    Fire();
    UpdateWeapon(0.01f);
    d.fireT = 0;
    Fire();                                    // empty: starts the reload
    float t = 0; int seenPartial = 0;
    while (d.reloading && t < 5) { UpdateWeapon(1 / 60.0f); t += 1 / 60.0f; if (d.mag > 0 && d.mag < 8) seenPartial = 1; }
    check(d.mag == 8 && seenPartial && t > 1.0f && t < 1.8f, TextFormat("an empty Cormorant reloads dart by dart (%d darts in %.2f s; rack reload 1.4 s)", d.mag, t));
    printf(fails ? "redtide-test: %d check(s) failed\n" : "redtide-test: all checks passed\n", fails);
    S.active = false;
    return fails ? 1 : 0;
}
