// Red Tide: the playable scene. A first-person diver in a Match (redtide_match.h: the rules, the Ecosystem, the
// Wreckers, the Goliath), drawn by the inked low-poly renderer. Stage 3 plays the Sunken Ship; the stage 2 test tank
// is the same Match on a one-room map (kept for --redtide-test, the silhouette check and the species lineup).
#include "redtide.h"
#include "redtide_match.h"
#include "redtide_render.h"
#include "game.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace rt {

struct Particle { Vector3 pos, vel; float life, max; Color col; float size; };
struct RoomBox { Vector3 c, half; Color col; };

struct RedTideScene {
    bool active = false;
    int mode = 1;                  // 0 the test tank, 1 the Sunken Ship
    MapData tank;                  // the tank's map: the Sunken Ship's species in one room
    std::vector<RoomBox> boxes;    // the tank's props
    std::unique_ptr<Match> m;
    Model level{}; bool levelReady = false;
    std::vector<Particle> fx;
    std::vector<Vector3> snow;
    float time = 0;
    bool shotMode = false;         // --shots: fixed camera, no input
    float silhouette = 0;
    int lineup = -1;               // --shots: every species of the Ship posed in rows (page number)
    int lastZone = -1; float zoneT = 0;
    float bob = 0;
};
static RedTideScene S;

static Match& M() { return *S.m; }
static DiverState& Me() { return S.m->divers[0]; }

// ---------------------------------------------------------------- the test tank
// A 30 x 12 x 30 m box room with a few crates and pillars, and a population drawn from the Sunken Ship's species so
// the Ecosystem runs exactly as it does on the real map.
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
    const std::pair<const char*, int> pop[] = {
        {"Bilge Sprat", 30}, {"Silverside", 20}, {"Sergeant Major", 4}, {"Parrotfish", 3}, {"Rabbitfish", 3}, {"Lionfish", 2},
        {"Cleaner Shrimp", 4}, {"Fiddler Crab", 4}, {"Reef Squid", 5}, {"Cuttlefish", 1}, {"Great Barracuda", 1}, {"Grouper", 1},
        {"Hawksbill Turtle", 1}, {"Nurse Shark", 1}, {"Jelly Bloom", 1}, {"Banded Sea Snake", 1}, {"Green Moray", 1}};
    for (const auto& p : pop) {
        SpawnRow r; r.zone = "The Tank"; r.species = p.first; r.count = p.second; r.respawnS = 30; r.capMult = 1;
        m.spawns.push_back(r);
    }
    for (auto& s : m.species) s.homeZone = "The Tank";
    Color iron{78, 86, 92, 255}, rust{122, 72, 46, 255}, kelp{70, 96, 62, 255}, sand{168, 140, 92, 255};
    S.boxes = {
        {{0, -0.5f, 0}, {16, 0.5f, 16}, sand}, {{0, 12.5f, 0}, {16, 0.5f, 16}, iron},
        {{-15.5f, 6, 0}, {0.5f, 6.5f, 16}, iron}, {{15.5f, 6, 0}, {0.5f, 6.5f, 16}, rust},
        {{0, 6, -15.5f}, {16, 6.5f, 0.5f}, rust}, {{0, 6, 15.5f}, {16, 6.5f, 0.5f}, iron},
        {{-7, 1, -6}, {1.5f, 1, 1.5f}, rust}, {{-5, 0.75f, -3}, {1, 0.75f, 1}, rust}, {{8, 1.5f, 5}, {2, 1.5f, 1.2f}, iron},
        {{6, 6, -8}, {0.6f, 6, 0.6f}, iron}, {{-8, 6, 8}, {0.6f, 6, 0.6f}, iron}, {{2, 0.3f, 9}, {3, 0.3f, 2}, kelp},
    };
}

static void ResetFx(float w, float h, float y0, Vector3 c) {
    S.fx.clear();
    S.snow.clear();
    for (int i = 0; i < 260; i++) S.snow.push_back({c.x + GetRandomValue(-(int)(w * 5), (int)(w * 5)) / 10.0f, y0 + GetRandomValue(0, (int)(h * 10)) / 10.0f, c.z + GetRandomValue(-(int)(w * 5), (int)(w * 5)) / 10.0f});
    S.time = 0;
    S.lastZone = -1;
}

static void StartTank() {
    BuildTank();
    S.m = std::make_unique<Match>();
    S.m->InitMap(S.tank, "ship", 1, 20260930, false);
    S.m->phase = TidePhase::Calm;   // the tank has no tides: it's a room to shoot in
    S.m->phaseT = -1e9f;
    Me().pos = {0, 3, -10};
    Me().yaw = 0;
    S.mode = 0;
    ResetFx(30, 12, 0, {0, 0, 0});
    S.active = true;
}

// ---------------------------------------------------------------- the ship's geometry
// Each room is a hollow box at its deck's height, its faces cut where a passage meets them; each passage is a short
// tunnel between the two rooms' faces. Upper-deck rooms are panelled wood, the lower decks riveted iron, the outside
// zones sand under open water. Built once into one mesh.
static void FaceWithHoles(MeshBuilder& mb, int axis, float at, Vector2 lo, Vector2 hi, const std::vector<std::pair<Vector2, Vector2>>& holes, Color c) {
    // axis: the face's normal (0 x, 1 y, 2 z); (u, v) are the other two axes in order
    std::vector<float> us{lo.x, hi.x}, vs{lo.y, hi.y};
    for (const auto& h : holes) { us.push_back(std::clamp(h.first.x, lo.x, hi.x)); us.push_back(std::clamp(h.second.x, lo.x, hi.x)); vs.push_back(std::clamp(h.first.y, lo.y, hi.y)); vs.push_back(std::clamp(h.second.y, lo.y, hi.y)); }
    // split long runs so the panels read as plates and planks
    for (float u = lo.x + 2; u < hi.x; u += 2) us.push_back(u);
    std::sort(us.begin(), us.end()); us.erase(std::unique(us.begin(), us.end()), us.end());
    std::sort(vs.begin(), vs.end()); vs.erase(std::unique(vs.begin(), vs.end()), vs.end());
    auto P = [&](float u, float v) -> Vector3 { return axis == 0 ? Vector3{at, u, v} : axis == 1 ? Vector3{u, at, v} : Vector3{u, v, at}; };
    for (size_t i = 0; i + 1 < us.size(); i++) for (size_t j = 0; j + 1 < vs.size(); j++) {
        float u0 = us[i], u1 = us[i + 1], v0 = vs[j], v1 = vs[j + 1];
        float cu = (u0 + u1) / 2, cv = (v0 + v1) / 2;
        bool hole = false;
        for (const auto& h : holes) if (cu > h.first.x && cu < h.second.x && cv > h.first.y && cv < h.second.y) hole = true;
        if (hole) continue;
        unsigned char k = (unsigned char)(((int)(u0 * 7.3f + v0 * 3.1f) & 3) * 5);
        Color cc{(unsigned char)std::max(0, c.r - k), (unsigned char)std::max(0, c.g - k), (unsigned char)std::max(0, c.b - k), 255};
        mb.Quad(P(u0, v0), P(u1, v0), P(u1, v1), P(u0, v1), cc);
    }
}

static void BuildLevelModel() {
    if (S.levelReady) { UnloadModel(S.level); S.levelReady = false; }
    if (!IsWindowReady() || S.mode != 1) return;
    const Match& m = M();
    const MapData& map = *m.map;
    MeshBuilder mb;
    for (const auto& v : m.level.vols) {
        if (v.zone < 0) continue;
        const Zone& z = map.zones[v.zone];
        bool outside = z.deck == "Outside", upper = z.deck == "Upper";
        Color wall = outside ? Color{58, 64, 66, 255} : upper ? Color{112, 80, 54, 255} : Color{82, 90, 96, 255};
        Color floor = outside ? Color{166, 142, 96, 255} : upper ? Color{118, 58, 46, 255} : Color{70, 72, 66, 255};
        Color ceil = upper ? Color{88, 70, 50, 255} : Color{62, 68, 72, 255};
        // the holes: every passage volume that crosses one of this room's faces
        for (int axis = 0; axis < 3; axis++) for (int side = 0; side < 2; side++) {
            float at = side ? (&v.hi.x)[axis] : (&v.lo.x)[axis];
            int ua = axis == 0 ? 1 : 0, va = axis == 2 ? 1 : 2;
            Vector2 lo{(&v.lo.x)[ua], (&v.lo.x)[va]}, hi{(&v.hi.x)[ua], (&v.hi.x)[va]};
            std::vector<std::pair<Vector2, Vector2>> holes;
            for (const auto& p : m.level.vols) {
                if (p.link < 0) continue;
                const Link& l = map.links[p.link];
                if (l.from != v.zone && l.to != v.zone) continue;
                if ((&p.lo.x)[axis] > at || (&p.hi.x)[axis] < at) continue;
                holes.push_back({{(&p.lo.x)[ua], (&p.lo.x)[va]}, {(&p.hi.x)[ua], (&p.hi.x)[va]}});
            }
            if (axis == 1 && side == 1 && outside) continue;   // open water above the outside zones
            FaceWithHoles(mb, axis, at, lo, hi, holes, axis == 1 ? (side ? ceil : floor) : wall);
        }
    }
    // passages: tunnels between the rooms' faces along their main axis
    for (const auto& p : m.level.vols) {
        if (p.link < 0) continue;
        const Link& l = map.links[p.link];
        const Volume& A = m.level.vols[l.from];
        const Volume& B = m.level.vols[l.to];
        Vector3 d = Vector3Subtract(l.b, l.a);
        int k = fabsf(d.x) > fabsf(d.z) ? 0 : 2;
        if (fabsf(d.y) > fabsf((&d.x)[k])) k = 1;
        float a0, a1;
        if ((&A.hi.x)[k] <= (&B.lo.x)[k]) { a0 = (&A.hi.x)[k]; a1 = (&B.lo.x)[k]; }
        else if ((&B.hi.x)[k] <= (&A.lo.x)[k]) { a0 = (&B.hi.x)[k]; a1 = (&A.lo.x)[k]; }
        else continue;
        Vector3 lo = p.lo, hi = p.hi;
        (&lo.x)[k] = a0; (&hi.x)[k] = a1;
        Color c = map.zones[l.from].deck == "Upper" && map.zones[l.to].deck == "Upper" ? Color{104, 76, 52, 255} : Color{76, 82, 86, 255};
        for (int axis = 0; axis < 3; axis++) {
            if (axis == k) continue;
            int ua = axis == 0 ? 1 : 0, va = axis == 2 ? 1 : 2;
            for (int side = 0; side < 2; side++) FaceWithHoles(mb, axis, side ? (&hi.x)[axis] : (&lo.x)[axis], {(&lo.x)[ua], (&lo.x)[va]}, {(&hi.x)[ua], (&hi.x)[va]}, {}, c);
        }
    }
    // fixed dressing from the blockout's features
    for (const Poi& p : map.pois) {
        if (p.zone < 0) continue;
        const Zone& z = map.zones[p.zone];
        Vector3 at{p.pos.x, z.y0, p.pos.z};
        const std::string& n = p.name;
        if (n.find("Air pocket") != std::string::npos) mb.Box({at.x, z.y1 - 0.9f, at.z}, {3.2f, 0.04f, 2.4f}, Color{200, 214, 220, 255});   // the silver skin of trapped air
        else if (n.find("Moray pipes") != std::string::npos) for (int k = 0; k < 6; k++) mb.Box({at.x + k * 2.2f, z.y0 + 0.6f, z.plan.y + 0.5f}, {0.45f, 0.45f, 0.5f}, Color{60, 58, 52, 255});
        else if (n.find("Mast") != std::string::npos) { mb.Box({at.x, z.y0 + 8, at.z}, {0.35f, 8, 0.35f}, Color{96, 74, 50, 255}); mb.Box({at.x, z.y0 + 12, at.z}, {4, 0.2f, 0.2f}, Color{96, 74, 50, 255}); }
        else if (n.find("Hull breach") != std::string::npos) mb.Box({std::min(at.x, z.plan.x + z.plan.width - 0.3f), z.y0 + 2, at.z}, {0.2f, 1.6f, 1.6f}, Color{30, 34, 36, 255});
        else if (n.find("Stonefish") != std::string::npos || n.find("Mantis") != std::string::npos) { mb.Box({at.x, z.y0 + 0.3f, at.z}, {0.9f, 0.3f, 0.7f}, Color{110, 100, 84, 255}); mb.Box({at.x + 0.8f, z.y0 + 0.2f, at.z + 0.5f}, {0.5f, 0.2f, 0.5f}, Color{96, 90, 76, 255}); }
        else if (n.find("crane") != std::string::npos) { mb.Box({at.x, z.y0 + 3, at.z}, {0.4f, 3, 0.4f}, Color{150, 110, 40, 255}); mb.Box({at.x + 2.5f, z.y0 + 6, at.z}, {2.8f, 0.3f, 0.3f}, Color{150, 110, 40, 255}); }
        else if (n.find("Workbench") != std::string::npos) mb.Box({at.x, z.y0 + 0.5f, at.z}, {1.2f, 0.5f, 0.6f}, Color{120, 84, 52, 255});
    }
    // the salon's pillars, the engine room's boiler, the cabins' partitions (so the rooms read as rooms)
    for (int zi = 0; zi < (int)map.zones.size(); zi++) {
        const Zone& z = map.zones[zi];
        if (z.name.find("Salon") != std::string::npos) for (int k = 0; k < 3; k++) mb.Box({z.plan.x + 5 + k * 5.0f, (z.y0 + z.y1) / 2, z.plan.y + 6}, {0.3f, (z.y1 - z.y0) / 2, 0.3f}, Color{170, 130, 70, 255});
        if (z.name.find("Engine") != std::string::npos) { mb.Box({z.plan.x + 4, z.y0 + 2, z.plan.y + 3}, {2.2f, 2, 1.6f}, Color{96, 60, 40, 255}); mb.Box({z.plan.x + 10, z.y0 + 1.2f, z.plan.y + 2}, {1.5f, 1.2f, 1.0f}, Color{80, 84, 88, 255}); }
        if (z.name.find("Cabin") != std::string::npos) for (int k = 1; k < 6; k++) mb.Box({z.plan.x + k * z.plan.width / 6, z.y0 + 2.5f, z.plan.y + z.plan.height - 1.2f}, {0.08f, 2.5f, 1.2f}, Color{90, 66, 46, 255});
    }
    S.level = LoadModelFromMesh(mb.Build());
    S.levelReady = true;
}

// ---------------------------------------------------------------- starting a match
static void StartShip(int players, uint32_t seed) {
    S.m = std::make_unique<Match>();
    S.m->Init("ship", players, seed, false);
    S.mode = 1;
    Vector3 lo = M().map->boundsMin, hi = M().map->boundsMax;
    ResetFx(std::max(hi.x - lo.x, hi.z - lo.z), hi.y - lo.y, lo.y, Vector3Lerp(lo, hi, 0.5f));
    BuildLevelModel();
    S.active = true;
}

// ---------------------------------------------------------------- effects
static void Burst(Vector3 p, int n, Color c, float speed, float life, float size) {
    for (int i = 0; i < n; i++) {
        Vector3 v{GetRandomValue(-100, 100) / 100.0f, GetRandomValue(-100, 100) / 100.0f, GetRandomValue(-100, 100) / 100.0f};
        S.fx.push_back({p, Vector3Scale(v, speed), life, life, c, size});
    }
}
static void DrainFx() {
    for (const FxEvent& e : M().fx) {
        switch (e.kind) {
            case 0: Burst(e.pos, 6, {150, 20, 20, 255}, 0.6f, 1.2f, 0.07f); break;     // blood
            case 1: Burst(e.pos, 3, {170, 160, 140, 255}, 0.5f, 0.5f, 0.05f); break;   // a wall
            case 2: Burst(e.pos, 4, {200, 235, 250, 255}, 0.8f, 0.6f, 0.04f); break;   // gas from the muzzle
            case 3: Burst(e.pos, 40, {255, 210, 140, 255}, 3.0f, 0.9f, 0.12f); Burst(e.pos, 30, {220, 230, 240, 255}, 1.5f, 2.0f, 0.08f); break;
            case 4: Burst(e.pos, 20, {140, 240, 200, 255}, 1.2f, 1.0f, 0.06f); break;
            case 5: Burst(e.pos, 60, {200, 220, 230, 255}, 5.0f, 0.6f, 0.05f); break;  // the Goliath's boom
            case 6: { Vector3 to = Vector3Add(e.pos, e.dir); for (int k = 0; k < 10; k++) S.fx.push_back({Vector3Lerp(e.pos, to, k / 10.0f), {0, 0, 0}, 0.25f, 0.25f, {170, 220, 255, 255}, 0.06f}); break; }
            case 7: Burst(e.pos, 30, {150, 130, 100, 255}, 2.0f, 1.5f, 0.1f); break;   // a crate hits the deck
            case 8: Burst(e.pos, 3, {220, 230, 240, 255}, 0.4f, 0.3f, 0.03f); break;
        }
    }
    M().fx.clear();
}

// ---------------------------------------------------------------- input
static void Input(float dt) {
    Match& m = M();
    DiverState& d = Me();
    if (S.shotMode || m.over) { m.SteerDiver(0, {0, 0, 0}, 0, false, false, dt); return; }
    Vector2 md = GetMouseDelta();
    float sens = d.ads ? 0.0016f : 0.0025f;
    d.yaw -= md.x * sens;
    d.pitch = std::clamp(d.pitch - md.y * sens, -1.45f, 1.45f);
    // aim sway (a cuttlefish's flash, a flinch)
    if (d.aimSway > 0 || d.flinchT > 0) { d.yaw += sinf(S.time * 7.1f) * 0.004f; d.pitch += cosf(S.time * 5.3f) * 0.003f; }
    Vector3 f{sinf(d.yaw), 0, cosf(d.yaw)}, r{-f.z, 0, f.x};
    Vector3 want{0, 0, 0};
    if (IsKeyDown(KEY_W)) want = Vector3Add(want, f);
    if (IsKeyDown(KEY_S)) want = Vector3Subtract(want, f);
    if (IsKeyDown(KEY_D)) want = Vector3Subtract(want, r);
    if (IsKeyDown(KEY_A)) want = Vector3Add(want, r);
    float vert = (IsKeyDown(KEY_SPACE) ? 1.0f : 0.0f) - (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_C) ? 1.0f : 0.0f);
    m.SteerDiver(0, want, vert, IsKeyDown(KEY_LEFT_SHIFT), IsMouseButtonDown(MOUSE_BUTTON_RIGHT), dt);
    m.Fire(0, IsMouseButtonDown(MOUSE_BUTTON_LEFT), dt);
    if (IsKeyPressed(KEY_R)) m.Reload(0);
    if (IsKeyPressed(KEY_V) || IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) m.Melee(0);
    if (IsKeyPressed(KEY_G)) m.ThrowLimpet(0);
    if (IsKeyPressed(KEY_E)) m.Interact(0, false, dt);
    else if (IsKeyDown(KEY_E)) m.Interact(0, true, dt);
    for (int k = 0; k < 3; k++) if (IsKeyPressed(KEY_ONE + k)) m.SwapWeapon(0, k);
    float wheel = GetMouseWheelMove();
    if (wheel != 0 && d.weapons.size() > 1) m.SwapWeapon(0, (d.cur + (wheel > 0 ? 1 : (int)d.weapons.size() - 1)) % (int)d.weapons.size());
}

// ---------------------------------------------------------------- drawing
// The viewmodel: the held weapon, a small model per weapon class built once and drawn in the camera's frame.
static Model gGuns[8]{};
static bool gGunsReady = false;
static int GunModelFor(const std::string& cls) {
    static const char* order[] = {"gas", "needle", "powder", "scatter", "spear", "lmg", "launcher", "melee"};
    for (int i = 0; i < 8; i++) if (cls == order[i]) return i;
    return 0;
}
static void BuildGuns() {
    Color brass{214, 168, 72, 255}, dark{96, 74, 44, 255}, wood{104, 66, 40, 255}, glass{120, 176, 190, 255}, iron{80, 86, 90, 255}, copper{186, 110, 60, 255};
    for (int i = 0; i < 8; i++) {
        MeshBuilder mb;
        switch (i) {
            case 0:   // gas pistol (the Cormorant)
                mb.Box({0, 0, 0.13f}, {0.018f, 0.018f, 0.14f}, brass);
                mb.Box({0, -0.004f, 0.27f}, {0.022f, 0.022f, 0.012f}, dark);
                mb.Lathe(0.09f, 3, 8, [](float) { return 0.034f; }, [](float) { return 0.034f; }, dark, dark, {0, -0.005f, -0.005f});
                mb.Box({0, -0.07f, -0.05f}, {0.018f, 0.06f, 0.028f}, wood);
                mb.Box({0, -0.035f, 0.02f}, {0.004f, 0.012f, 0.02f}, glass);
                break;
            case 1:   // needler: a glass cartridge under a slim barrel
                mb.Box({0, 0, 0.12f}, {0.02f, 0.02f, 0.2f}, iron);
                mb.Box({0, -0.05f, 0.08f}, {0.016f, 0.03f, 0.06f}, glass);
                mb.Box({0, -0.07f, -0.07f}, {0.018f, 0.06f, 0.03f}, wood);
                break;
            case 2:   // powder carbine
                mb.Box({0, 0, 0.18f}, {0.016f, 0.016f, 0.28f}, iron);
                mb.Box({0, -0.02f, -0.02f}, {0.028f, 0.04f, 0.1f}, wood);
                mb.Box({0, -0.06f, 0.06f}, {0.012f, 0.04f, 0.02f}, brass);
                break;
            case 3:   // scatter gun: a fat short barrel
                mb.Lathe(0.3f, 3, 8, [](float) { return 0.035f; }, [](float) { return 0.035f; }, iron, iron, {0, 0, 0.12f});
                mb.Box({0, -0.05f, -0.07f}, {0.03f, 0.05f, 0.08f}, wood);
                break;
            case 4:   // speargun: a long rail and a spear
                mb.Box({0, 0, 0.2f}, {0.012f, 0.02f, 0.4f}, wood);
                mb.Box({0, 0.03f, 0.25f}, {0.004f, 0.004f, 0.45f}, iron);
                mb.Box({0, -0.05f, -0.1f}, {0.018f, 0.05f, 0.03f}, dark);
                break;
            case 5:   // gatling needler
                for (int k = 0; k < 4; k++) mb.Box({cosf(k * 1.57f) * 0.025f, sinf(k * 1.57f) * 0.025f, 0.18f}, {0.009f, 0.009f, 0.22f}, iron);
                mb.Box({0, -0.02f, -0.04f}, {0.05f, 0.06f, 0.1f}, brass);
                break;
            case 6:   // launcher: a copper tube
                mb.Lathe(0.4f, 3, 10, [](float) { return 0.045f; }, [](float) { return 0.045f; }, copper, copper, {0, 0, 0.1f});
                mb.Box({0, -0.07f, -0.02f}, {0.02f, 0.05f, 0.03f}, dark);
                break;
            default:  // melee: a gaff or a trident's shaft
                mb.Box({0, 0, 0.2f}, {0.012f, 0.012f, 0.38f}, wood);
                mb.Box({0, 0, 0.58f}, {0.04f, 0.006f, 0.03f}, iron);
                break;
        }
        gGuns[i] = LoadModelFromMesh(mb.Build());
    }
    gGunsReady = true;
}

static void DrawGun(const Camera3D& cam) {
    if (!gGunsReady && IsWindowReady()) BuildGuns();
    if (!gGunsReady) return;
    DiverState& d = Me();
    if (d.dead) return;
    const WeaponDef& w = M().W(M().Cur(d));
    Vector3 f = M().Forward(d);
    Vector3 right = Vector3Normalize(Vector3CrossProduct(f, {0, 1, 0}));
    Vector3 up = Vector3CrossProduct(right, f);
    float bobx = sinf(S.bob) * 0.008f, boby = fabsf(cosf(S.bob)) * 0.006f;
    float kick = d.recoil * 0.035f * w.handling.recoil, dip = d.reloading ? 0.06f : 0;
    float side = d.ads ? 0.0f : 0.13f, low = d.ads ? -0.075f : -0.12f;
    float melee = d.meleeT > 0 ? sinf(std::min(1.0f, d.meleeT * 3) * 3.14f) * 0.12f : 0;
    Vector3 p = Vector3Add(cam.position, Vector3Add(Vector3Scale(f, 0.30f - kick + melee), Vector3Add(Vector3Scale(right, side + bobx), Vector3Scale(up, low - boby - dip))));
    Matrix m = MatrixIdentity();
    Vector3 rx = Vector3Scale(right, -1);
    m.m0 = rx.x; m.m1 = rx.y; m.m2 = rx.z;
    m.m4 = up.x; m.m5 = up.y; m.m6 = up.z;
    m.m8 = f.x; m.m9 = f.y; m.m10 = f.z;
    m.m12 = p.x; m.m13 = p.y; m.m14 = p.z;
    Matrix tilt = MatrixRotateX(-d.recoil * 0.25f * w.handling.recoil);
    DrawStatic(gGuns[GunModelFor(w.cls)], MatrixMultiply(tilt, m));
}

static Color FloraColor(const std::string& n) {
    if (n.find("Fire coral") != std::string::npos) return {226, 116, 44, 255};
    if (n.find("hydroid") != std::string::npos) return {222, 214, 186, 255};
    if (n.find("Barrel") != std::string::npos) return {150, 92, 60, 255};
    if (n.find("Sea fan") != std::string::npos) return {156, 62, 124, 255};
    if (n.find("Anemone") != std::string::npos) return {232, 124, 152, 255};
    if (n.find("kelp") != std::string::npos) return {84, 122, 52, 255};
    if (n.find("Coralline") != std::string::npos) return {204, 122, 132, 255};
    if (n.find("Dead man") != std::string::npos) return {236, 224, 168, 255};
    if (n.find("Sargassum") != std::string::npos) return {136, 112, 52, 255};
    if (n.find("Luminous") != std::string::npos) return {126, 226, 176, 255};
    return {76, 116, 62, 255};
}

static void DrawFlora() {
    const Match& m = M();
    Vector3 eye = m.Eye(Me());
    for (const auto& p : m.eco.flora) {
        if (p.units <= 0 || Vector3Distance(p.pos, eye) > 45) continue;
        const std::string& n = m.map->flora[p.flora].name;
        Color c = FloraColor(n);
        float s = 0.4f + 0.6f * std::clamp(p.units / 100.0f, 0.0f, 1.0f);
        uint32_t h = (uint32_t)(fabsf(p.pos.x) * 73 + fabsf(p.pos.z) * 31);
        if (n.find("kelp") != std::string::npos) {
            for (int k = 0; k < 4; k++) { float sw = sinf(S.time * 0.8f + k) * 0.3f; DrawWorldCube({p.pos.x + (k - 1.5f) * 0.4f + sw, p.pos.y + 3 * s, p.pos.z + ((h >> k) & 1) * 0.3f}, {0.12f, 6 * s, 0.05f}, c); }
        } else if (n.find("Sea fan") != std::string::npos) {
            DrawWorldCube({p.pos.x, p.pos.y + 0.8f * s, p.pos.z}, {1.4f * s, 1.6f * s, 0.05f}, c);
        } else if (n.find("Fire coral") != std::string::npos || n.find("Dead man") != std::string::npos || n.find("hydroid") != std::string::npos) {
            for (int k = 0; k < 5; k++) DrawWorldCube({p.pos.x + ((int)(h >> (k * 3)) % 5 - 2) * 0.22f, p.pos.y + 0.3f * s, p.pos.z + ((int)(h >> (k * 2 + 1)) % 5 - 2) * 0.22f}, {0.08f, 0.6f * s + 0.1f * k, 0.08f}, c);
        } else if (n.find("Barrel") != std::string::npos) {
            DrawWorldCube({p.pos.x, p.pos.y + 0.5f * s, p.pos.z}, {0.7f * s, 1.0f * s, 0.7f * s}, c);
        } else if (n.find("Anemone") != std::string::npos) {
            for (int k = 0; k < 4; k++) DrawWorldCube({p.pos.x + (k % 2 - 0.5f) * 0.5f, p.pos.y + 0.2f, p.pos.z + (k / 2 - 0.5f) * 0.5f}, {0.3f, 0.35f * s, 0.3f}, c);
        } else if (n.find("Sargassum") != std::string::npos) {
            DrawWorldCube(p.pos, {2.4f * s, 0.2f, 2.0f * s}, c);
        } else {
            DrawWorldCube({p.pos.x, p.pos.y + 0.05f, p.pos.z}, {1.6f * s, 0.1f, 1.4f * s}, c);   // mats and crusts
        }
    }
}

static void DrawStations() {
    const Match& m = M();
    for (const auto& s : m.level.stations) {
        Vector3 p = s.pos;
        bool dead = s.needsPower && !m.power;
        switch (s.type) {
            case StationType::Rack:
                DrawWorldCube({p.x, p.y + 0.4f, p.z}, {1.2f, 0.8f, 0.12f}, {96, 70, 44, 255});
                DrawWorldCube({p.x, p.y + 0.45f, p.z - 0.08f}, {0.8f, 0.1f, 0.06f}, {150, 150, 150, 255});   // the gun on its pegs
                break;
            case StationType::Tonic: {
                DrawWorldCube({p.x, p.y + 0.4f, p.z}, {0.8f, 1.8f, 0.6f}, dead ? Color{90, 76, 50, 255} : Color{196, 150, 70, 255});
                Color glass = s.tonic == "juggernaut" ? Color{200, 60, 50, 255} : s.tonic == "quick" ? Color{80, 180, 220, 255} : s.tonic == "speed" ? Color{120, 220, 90, 255} : s.tonic == "double" ? Color{230, 170, 60, 255} : Color{170, 120, 220, 255};
                if (dead) glass = {60, 60, 60, 255};
                DrawWorldCube({p.x, p.y + 0.8f, p.z - 0.32f}, {0.3f, 0.5f, 0.05f}, glass);
                break;
            }
            case StationType::Locker: {
                bool live = m.LockerLiveAt(s);
                DrawWorldCube({p.x, p.y - 0.4f, p.z}, {1.1f, 0.7f, 0.7f}, live ? Color{110, 80, 46, 255} : Color{60, 52, 40, 255});
                if (live) DrawWorldCube({p.x, p.y + 2.2f + sinf(S.time * 2) * 0.1f, p.z}, {0.25f, 0.35f, 0.25f}, {255, 210, 120, 255});   // the lantern buoy
                break;
            }
            case StationType::Forge:
                DrawWorldCube({p.x, p.y + 0.3f, p.z}, {1.6f, 2.2f, 1.4f}, dead ? Color{70, 66, 60, 255} : Color{120, 110, 96, 255});
                DrawWorldCube({p.x, p.y + 0.6f, p.z - 0.72f}, {0.6f, 0.6f, 0.05f}, dead ? Color{40, 50, 50, 255} : Color{120, 220, 200, 255});
                break;
            case StationType::Power:
                DrawWorldCube({p.x, p.y + 0.3f, p.z}, {0.5f, 1.0f, 0.3f}, {80, 80, 76, 255});
                DrawWorldCube({p.x, p.y + (m.power ? 0.9f : 0.5f), p.z - 0.2f}, {0.08f, 0.5f, 0.08f}, m.power ? Color{120, 220, 110, 255} : Color{220, 70, 50, 255});
                break;
            case StationType::Quest: DrawWorldCube(p, {0.8f, 0.9f, 0.7f}, m.safeOpen ? Color{60, 60, 60, 255} : Color{70, 76, 70, 255}); break;
            case StationType::Cleaning: DrawWorldCube({p.x, p.y - 0.6f, p.z}, {1.4f, 0.8f, 1.2f}, {110, 100, 88, 255}); break;
            default: break;
        }
    }
    // closed doors: debris and grating across the passage
    for (const auto& d : m.level.doors) {
        if (d.open) continue;
        const Volume& v = m.level.vols[m.map->zones.size() + d.link];
        Vector3 size = Vector3Subtract(v.hi, v.lo);
        const Link& l = m.map->links[d.link];
        Vector3 dir = Vector3Subtract(l.b, l.a);
        int k = fabsf(dir.x) > fabsf(dir.z) ? 0 : 2;
        (&size.x)[k] = 0.35f;
        DrawWorldCube(d.pos, Vector3Scale(size, 0.98f), {92, 70, 50, 255});
        DrawWorldCube(Vector3Add(d.pos, {0, 0.4f, 0}), {k == 0 ? 0.5f : size.x * 0.6f, 0.3f, k == 2 ? 0.5f : size.z * 0.6f}, {120, 96, 64, 255});
    }
}

static Camera3D MakeCamera(SceneLight& L) {
    DiverState& d = Me();
    Camera3D cam{};
    Vector3 f = M().Forward(d);
    cam.position = M().Eye(d);
    if (d.downed || d.dead) cam.position.y -= 0.35f;
    cam.target = Vector3Add(cam.position, f);
    cam.up = {0, 1, 0};
    cam.fovy = d.ads ? 55.0f : 72.0f;
    cam.projection = CAMERA_PERSPECTIVE;
    L.lampPos = cam.position;
    L.lampDir = f;
    L.lampRange = 24;
    L.lampCone = 0.72f;
    L.fog = {10, 30, 36, 255};
    L.fogDensity = 0.05f;
    L.surfaceY = S.mode == 1 ? 34 : 14;
    L.time = S.time;
    L.silhouette = S.silhouette;
    int z = M().eco.ZoneAt(d.pos);
    float scent = M().eco.Smell(d.pos, z, 6);
    L.bloodTint = std::clamp(scent / 120.0f + (M().frenzyT > 0 ? 0.25f : 0.0f), 0.0f, 1.0f);
    if (z >= 0 && M().map->zones[z].deck == "Outside") { L.fog = {16, 44, 54, 255}; L.fogDensity = 0.035f; }
    return cam;
}

static void DrawLineup() {
    const MapData& ship = Map("ship");
    Camera3D cam{};
    cam.position = {0, 0, -9}; cam.target = {0, 0, 0}; cam.up = {0, 1, 0}; cam.fovy = 50; cam.projection = CAMERA_PERSPECTIVE;
    SceneLight L;
    L.lampPos = {0, 6, -9}; L.lampDir = Vector3Normalize({0, -0.4f, 1}); L.lampRange = 40; L.lampCone = 0.2f; L.fogDensity = 0.004f; L.fog = {26, 52, 58, 255};
    L.time = S.time;
    RenderBegin(cam, L);
    int per = 20, first = S.lineup * per, cols = 5;
    for (int k = 0; k < per && first + k < (int)ship.species.size(); k++) {
        const Species& sp = ship.species[first + k];
        if (sp.isEnemy || sp.isDiver) continue;
        const CreatureModel& cm = Creature("ship", sp.name);
        int cx = k % cols, cy = k / cols;
        Vector3 at{(cx - (cols - 1) * 0.5f) * 2.3f, (1.5f - cy) * 2.0f, 0};
        float sc = 0.95f / std::max(0.05f, cm.extent);
        DrawCreature(cm, at, 1.5708f, 0, sc, S.time * cm.freq, 0.8f);
    }
    RenderEnd();
    for (int k = 0; k < per && first + k < (int)ship.species.size(); k++) {
        const Species& sp = ship.species[first + k];
        if (sp.isEnemy || sp.isDiver) continue;
        int cx = k % cols, cy = k / cols;
        Vector3 at{(cx - (cols - 1) * 0.5f) * 2.3f, (1.5f - cy) * 2.0f - 0.85f, 0};
        Vector2 sc = GetWorldToScreenEx(at, cam, SCREEN_W, SCREEN_H);
        DrawTextCentered(TextFormat("%s (%s)", sp.name.c_str(), Creature("ship", sp.name).plan.c_str()), sc.x, sc.y, 13, Color{230, 222, 200, 255});
    }
}

static void DrawScene() {
    if (S.lineup >= 0) { DrawLineup(); return; }
    Match& m = M();
    SceneLight L;
    Camera3D cam = MakeCamera(L);
    Vector3 eye = cam.position;
    Vector3 fwd = Vector3Subtract(cam.target, cam.position);
    RenderBegin(cam, L);
    if (S.silhouette < 0.5f) {
        if (S.mode == 0) for (const auto& b : S.boxes) DrawWorldCube(b.c, Vector3Scale(b.half, 2), b.col);
        if (S.mode == 1 && S.levelReady) DrawStatic(S.level, MatrixIdentity());
        if (S.mode == 1) { DrawStations(); DrawFlora(); }
        for (const auto& f : m.drops) {
            float pulse = 0.8f + 0.2f * sinf(S.time * 5);
            if (f.weapon >= 0) DrawWorldCube(f.pos, {0.8f, 0.15f, 0.25f}, {160, 150, 130, 255});
            else DrawWorldCube(Vector3Add(f.pos, {0, sinf(S.time * 2) * 0.15f, 0}), {0.25f * pulse, 0.45f * pulse, 0.25f * pulse}, {120, 240, 200, 255});
        }
        for (const auto& c : m.crates) DrawWorldCube(c.fallen ? Vector3{c.pos.x, c.pos.y - 0.6f, c.pos.z} : Vector3{c.pos.x, c.pos.y + std::max(0.0f, c.t) * 3 + 0.4f, c.pos.z}, {1.2f, 1.2f, 1.2f}, {120, 92, 60, 255});
    }
    for (int i = 0; i < (int)m.eco.agents.size(); i++) {
        const Agent& a = m.eco.agents[i];
        if (!a.alive || a.diver == 0) continue;               // (diver 0 is you)
        if (Vector3Distance(a.pos, eye) > 55) continue;
        const Species& sp = m.map->species[a.sp];
        const CreatureModel& cm = Creature(m.artKey, sp.name);
        float spd = Vector3Length(a.vel);
        Vector3 v = spd > 0.02f ? a.vel : Vector3{sinf(a.rng * 0.001f), 0, cosf(a.rng * 0.001f)};
        float yaw = atan2f(v.x, v.z);
        float pitch = spd > 0.05f ? std::clamp(asinf(std::clamp(v.y / std::max(spd, 1e-3f), -1.0f, 1.0f)), -0.6f, 0.6f) : 0;
        if (cm.anim == AnimMode::Walker || cm.anim == AnimMode::Static) pitch = 0;
        float inten = std::clamp(0.35f + spd / std::max(0.5f, sp.speed), 0.2f, 1.6f);
        if (a.stun > 0 || a.held > 0) inten = 0.1f;
        float phase = S.time * cm.freq * (0.6f + inten * 0.6f) + (a.rng % 1000) * 0.01f;
        Color tint = a.wound > 0.3f ? Color{255, (unsigned char)(255 - a.wound * 120), (unsigned char)(255 - a.wound * 120), 255} : WHITE;
        if (m.IsBoss(i) && m.bossGillsT > 0) tint = {255, 170, 150, 255};
        DrawCreature(cm, a.pos, yaw, pitch, a.sp < (int)m.bodyScale.size() ? m.bodyScale[a.sp] : 1.0f, phase, inten, tint);
    }
    if (S.silhouette < 0.5f) DrawGun(cam);
    RenderEnd();
    if (S.silhouette > 0.5f) return;
    // after the ink: blood plumes, darts, tells, bubbles and marine snow (bright specks come after the ink pass)
    BeginLayer(Mode3DRT());
    ClearBackground(BLANK);
    BeginMode3D(cam);
    const Field& sf = m.eco.scent;
    float vis = Engine().C("scent_visible_min", 3);
    int cx = (int)((eye.x - sf.origin.x) / sf.cell), cy = (int)((eye.y - sf.origin.y) / sf.cell), cz = (int)((eye.z - sf.origin.z) / sf.cell);
    const int R = 14;
    for (int z = std::max(0, cz - R); z < std::min(sf.nz, cz + R); z++) for (int y = std::max(0, cy - R); y < std::min(sf.ny, cy + R); y++) for (int x = std::max(0, cx - R); x < std::min(sf.nx, cx + R); x++) {
        float v = sf.v[sf.Idx(x, y, z)];
        if (v < vis) continue;
        Vector3 c{sf.origin.x + (x + 0.5f) * sf.cell, sf.origin.y + (y + 0.5f) * sf.cell, sf.origin.z + (z + 0.5f) * sf.cell};
        float a = std::clamp(v / 60.0f, 0.05f, 0.45f);
        DrawSphere(c, sf.cell * 0.55f, Fade(Color{120, 10, 12, 255}, a * 0.5f));
    }
    for (const auto& p : S.fx) DrawCube(p.pos, p.size, p.size, p.size, Fade(p.col, p.life / p.max));
    for (const auto& t : m.darts) {
        if (t.kind == 4) { DrawCube(t.pos, 0.12f, 0.12f, 0.12f, fmodf(S.time * 6, 1) < 0.5f ? Color{255, 80, 60, 255} : Color{80, 30, 20, 255}); continue; }
        Color c = t.enemy >= 0 ? Color{255, 120, 90, 255} : Color{230, 220, 180, 255};
        DrawLine3D(t.pos, Vector3Subtract(t.pos, Vector3Scale(Vector3Normalize(t.vel), 0.3f)), c);
    }
    // the Wreckers' tells: the Speargunner's red laser dot, the Foreman's winch
    for (const auto& kv : m.enemyTellT) {
        const Agent& a = m.eco.agents[kv.first];
        if (!a.alive || a.unit < 0) continue;
        const FactionUnit& u = m.map->faction.units[a.unit];
        if (u.role != "cover" && u.role != "leader") continue;
        int di = -1; float bd = 1e9f;
        for (const auto& d : m.divers) if (!d.dead && Vector3Distance(d.pos, a.pos) < bd) { bd = Vector3Distance(d.pos, a.pos); di = d.slot; }
        if (di >= 0) DrawLine3D(Vector3Add(a.pos, {0, 0.3f, 0}), m.divers[di].pos, Color{255, 40, 30, 200});
    }
    // the Goliath's inhale: a current of silt toward its mouth
    if (m.bossInhaleT >= 0 && m.bossAgent >= 0) {
        const Agent& b = m.eco.agents[m.bossAgent];
        for (int k = 0; k < 30; k++) {
            float t = fmodf(S.time * 1.5f + k * 0.37f, 1.0f);
            Vector3 from = Vector3Add(b.pos, {sinf(k * 2.1f) * 4, cosf(k * 1.3f) * 2, cosf(k * 2.1f) * 4});
            DrawCube(Vector3Lerp(from, b.pos, t), 0.04f, 0.04f, 0.04f, Fade(Color{220, 230, 220, 255}, 0.7f));
        }
    }
    // the crates' marked squares
    for (const auto& c : m.crates) if (!c.fallen) DrawCube({c.pos.x, c.pos.y - 1.1f, c.pos.z}, 1.4f, 0.03f, 1.4f, Fade(RED, 0.5f + 0.3f * sinf(S.time * 12)));
    for (const auto& s : S.snow) DrawCube(s, 0.02f, 0.02f, 0.02f, Fade(Color{220, 230, 220, 255}, 0.6f));
    EndMode3D();
    EndLayer();
    RenderTexture2D& ov = Mode3DRT();
    DrawTexturePro(ov.texture, {0, 0, (float)ov.texture.width, -(float)ov.texture.height}, {0, 0, (float)SCREEN_W, (float)SCREEN_H}, {0, 0}, 0, WHITE);
    // world labels: the Locker's buoy, closed doors' prices, drops (only the close ones in front)
    auto label = [&](Vector3 at, float range, const std::string& text, Color c) {
        if (Vector3Distance(at, eye) > range || Vector3DotProduct(Vector3Subtract(at, eye), fwd) <= 0) return;
        if (S.mode == 1 && !m.level.Sight(eye, Vector3Subtract(at, {0, 0.5f, 0}), m.linkOpen)) return;   // not through walls
        Vector2 sp = GetWorldToScreenEx(at, cam, SCREEN_W, SCREEN_H);
        DrawTextCentered(text, sp.x, sp.y, 13, c);
    };
    for (const auto& s : m.level.stations) if (s.type == StationType::Locker && m.LockerLiveAt(s)) label(Vector3Add(s.pos, {0, 2.8f, 0}), 30, "Davy's Locker", {255, 214, 140, 255});
    for (const auto& d : m.level.doors) if (!d.open) label(Vector3Add(d.pos, {0, 0.9f, 0}), 14, TextFormat("%s  %d", d.name.c_str(), d.cost), {235, 210, 160, 255});
    for (const auto& f : m.drops) label(Vector3Add(f.pos, {0, 0.6f, 0}), 20, f.weapon >= 0 ? Weapons().weapons[f.weapon].name : DropName(f.type), {150, 250, 210, 255});
}

// ---------------------------------------------------------------- the HUD
static void DrawHud() {
    Match& m = M();
    DiverState& d = Me();
    float cx = SCREEN_W / 2.0f, cy = SCREEN_H / 2.0f;
    Color paper{235, 230, 210, 255}, brass{214, 168, 72, 255}, blood{190, 40, 30, 255};
    // the crosshair (tightens on aim) and the hit marker
    float spread = d.ads ? 4 : 7;
    DrawRing({cx, cy}, spread - 2, spread, 0, 360, 24, Fade(paper, 0.8f));
    if (d.hitMarker > 0) {
        Color hc = d.hitWeak ? Color{255, 90, 70, 255} : paper;
        for (int k = 0; k < 4; k++) { float a = (45 + k * 90) * DEG2RAD; DrawLineEx({cx + cosf(a) * 9, cy + sinf(a) * 9}, {cx + cosf(a) * 16, cy + sinf(a) * 16}, 2, hc); }
    }
    // the tide bell, top left
    const char* phase = m.phase == TidePhase::Calm ? "CALM" : m.phase == TidePhase::Hunt ? "HUNT" : "TIDE";
    TxtBold(TextFormat("%s %d", phase, m.tide), 24, 18, 30, m.phase == TidePhase::Hunt ? Color{230, 80, 60, 255} : paper);
    if (m.phase == TidePhase::Tide) {
        DrawBar({24, 56, 180, 8}, m.tideKills / (float)std::max(1, m.quota), blood);
        Txt(TextFormat("%d / %d kills", m.tideKills, m.quota), 24, 68, 14, Fade(paper, 0.8f));
    } else if (m.phase == TidePhase::Calm) {
        float calm = m.map->tunables.count("calm_seconds") ? (float)m.map->tunables.at("calm_seconds") : 20.0f;
        if (S.mode == 1) Txt(TextFormat("the water calms: %d s to buy, build, revive", (int)std::max(0.0f, calm - m.phaseT)), 24, 56, 14, Fade(paper, 0.8f));
        else Txt("the test tank (no tides)", 24, 56, 14, Fade(paper, 0.8f));
    } else if (m.predatorHunt) Txt(TextFormat("apex beasts to kill: %d", std::max(0, m.huntApexLeft)), 24, 56, 14, Fade(paper, 0.8f));
    else { int left = 0; for (const auto& s : m.eco.squads) if (s.hunt && s.alive) left += (int)s.members.size(); Txt(TextFormat("%s left: %d", m.map->faction.name.c_str(), left), 24, 56, 14, Fade(paper, 0.8f)); }
    // active drops and keys
    float dy = 90;
    auto timer = [&](const char* name, float t) { if (t > 0) { Txt(TextFormat("%s  %d s", name, (int)t + 1), 24, dy, 14, Color{150, 250, 210, 255}); dy += 18; } };
    timer("Blood Frenzy", m.frenzyT); timer("Double Scrip", m.doubleScripT); timer("Fire Sale", m.fireSaleT); timer("Harpoon Hour", m.harpoonT);
    if (!m.keys.empty()) Txt(TextFormat("safe keys: %d of 3", (int)m.keys.size()), 24, dy, 14, brass);
    // scrip, top right
    TxtBold(TextFormat("%d", d.scrip), SCREEN_W - 150, 20, 30, paper);
    Txt("scrip", SCREEN_W - 150, 52, 14, Fade(paper, 0.7f));
    // the zone's name as you enter
    if (S.zoneT > 0 && S.lastZone >= 0) DrawTextCenteredBold(m.map->zones[S.lastZone].name, cx, 60, 22, Fade(paper, std::min(1.0f, S.zoneT)));
    // the Goliath's bar
    if (m.bossActive && m.bossAgent >= 0) {
        const Agent& b = m.eco.agents[m.bossAgent];
        DrawTextCenteredBold(TextFormat("%s  (phase %d)", m.map->species[b.sp].name.c_str(), m.bossPhase), cx, 92, 18, Color{230, 120, 90, 255});
        DrawBar({cx - 200, 116, 400, 10}, b.hp / std::max(1.0f, b.hpMax), Color{170, 50, 40, 255});
        if (m.bossGillsT > 0) DrawTextCentered("the gills are open", cx, 132, 14, Color{255, 170, 140, 255});
    }
    // weapon and ammo, bottom right
    const Held& h = m.Cur(d);
    const WeaponDef& w = m.W(h);
    std::string wname = h.forged && !w.forged.empty() ? w.forged : w.name;
    TxtBold(wname, SCREEN_W - 280, SCREEN_H - 96, 20, h.forged ? Color{120, 230, 210, 255} : brass);
    if (w.melee) TxtBold("melee", SCREEN_W - 280, SCREEN_H - 70, 26, paper);
    else TxtBold(d.harpoonHour ? "infinite" : TextFormat("%d / %d", h.mag, h.reserve), SCREEN_W - 280, SCREEN_H - 70, 30, h.mag == 0 ? blood : paper);
    if (d.reloading) Txt("reloading...", SCREEN_W - 280, SCREEN_H - 36, 14, Fade(paper, 0.8f));
    Txt(TextFormat("limpets %d", d.limpets), SCREEN_W - 120, SCREEN_H - 36, 14, Fade(paper, 0.8f));
    if (!d.downed) for (int i = 0; i < (int)d.weapons.size(); i++) Txt(TextFormat("%d %s", i + 1, m.W(d.weapons[i]).name.c_str()), SCREEN_W - 280, SCREEN_H - 150 + i * 16.0f, 13, i == d.cur ? paper : Fade(paper, 0.5f));
    // health: a brass pressure gauge, bottom left; the tonics' bottles beside it
    Vector2 g{80, SCREEN_H - 80.0f};
    DrawCircleV(g, 46, Color{60, 48, 30, 255});
    DrawRing(g, 40, 46, 0, 360, 40, brass);
    float frac = std::clamp(d.hp / d.hpMax, 0.0f, 1.0f);
    float ang = (-220 + 260 * frac) * DEG2RAD;
    DrawLineEx(g, {g.x + cosf(ang) * 34, g.y + sinf(ang) * 34}, 3, blood);
    TxtBold(TextFormat("%d", (int)std::max(0.0f, d.hp)), g.x - 14, g.y + 10, 16, Pal::Paper);
    float tx = 140;
    for (const auto& t : d.tonics) {
        const TonicDef* td = Weapons().Tonic(t);
        DrawRectangleRounded({tx, SCREEN_H - 58.0f, 16, 30}, 0.4f, 4, Color{196, 150, 70, 255});
        if (td) Txt(td->name.substr(0, td->name.find(' ')), tx - 6, SCREEN_H - 24.0f, 11, Fade(paper, 0.8f));
        tx += 60;
    }
    std::string st;
    if (d.bleedT > 0) st += "bleeding  ";
    if (d.poisonT > 0) st += "poisoned  ";
    if (d.slowT > 0) st += "slowed  ";
    if (d.stunT > 0) st += "stunned  ";
    if (!st.empty()) Txt(st, 140, SCREEN_H - 78.0f, 14, Color{230, 120, 100, 255});
    // the scent vial, left edge
    int z = m.eco.ZoneAt(d.pos);
    float scent = std::clamp(m.eco.Smell(d.pos, z, 6) / 120.0f, 0.0f, 1.0f);
    Rectangle vial{24, 180, 14, 200};
    DrawRectangleRec(vial, Fade(BLACK, 0.4f));
    DrawRectangleRec({vial.x, vial.y + vial.height * (1 - scent), vial.width, vial.height * scent}, Color{170, 20, 24, 230});
    DrawRectangleLinesEx(vial, 2, Color{214, 168, 72, 200});
    if (d.stamina < 0.999f) DrawBar({cx - 80, SCREEN_H - 40.0f, 160, 8}, d.stamina, Color{150, 220, 230, 255});
    // where the last hit came from: a red arc at the screen's edge
    if (d.hurtT > 0) {
        Vector3 to = Vector3Subtract(d.hurtFrom, d.pos);
        float rel = atan2f(to.x, to.z) - d.yaw;
        float a = (-rel - 1.5708f) * RAD2DEG;
        DrawRing({cx, cy}, 150, 160, a - 20, a + 20, 12, Fade(blood, d.hurtT));
    }
    // the prompt
    int cost = 0;
    std::string prompt = m.PromptFor(0, &cost);
    if (!prompt.empty()) DrawTextCentered(prompt, cx, cy + 70, 18, cost > d.scrip ? Color{230, 110, 90, 255} : paper);
    if (d.lastKillT > 0) DrawTextCentered(d.lastKill, cx, cy + 40, 16, Fade(Color{235, 220, 190, 255}, std::min(1.0f, d.lastKillT)));
    // captions: barks, the tide bell, what the ecosystem just did
    float capY = SCREEN_H - 200;
    for (const auto& c : m.captions) {
        Txt(c.who.empty() ? c.text : c.who + ": " + c.text, 150, capY, 15, Fade(paper, std::min(1.0f, c.t)));
        capY += 18;
    }
    // down, held, and the end of the match
    if (d.downed) {
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(Color{80, 0, 0, 255}, 0.25f));
        DrawTextCenteredBold(TextFormat("DOWN  %d", (int)std::max(0.0f, d.downT)), cx, cy - 80, 34, Color{240, 200, 180, 255});
        if (d.reviveT > 0) DrawBar({cx - 100, cy - 40, 200, 10}, d.reviveT / Engine().C("revive_s", 4), Color{150, 220, 160, 255});
    }
    if (d.heldT > 0) DrawTextCenteredBold(TextFormat("HELD  %.1f", d.heldT), cx, cy - 80, 30, Color{240, 150, 120, 255});
    if (d.dead && !m.over) DrawTextCenteredBold("Bled out: back at the next tide", cx, cy - 80, 26, paper);
    if (m.over) {
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.55f));
        Rectangle p{cx - 260, cy - 150, 520, 300};
        DrawRectangleRounded(p, 0.05f, 6, Color{30, 26, 22, 240});
        DrawRectangleRoundedLines(p, 0.05f, 6, brass);
        DrawTextCenteredBold("THE RED TIDE TAKES YOU", cx, p.y + 24, 26, Color{220, 90, 70, 255});
        float y = p.y + 76;
        auto row = [&](const std::string& a, const std::string& b) { Txt(a, p.x + 40, y, 17, Fade(paper, 0.8f)); TxtBold(b, p.x + 330, y, 17, paper); y += 26; };
        row("Tide reached", TextFormat("%d", m.tide));
        row("Kills (headshots)", TextFormat("%d (%d)", d.kills, d.headshots));
        row("Scrip earned", TextFormat("%d", d.scripEarned));
        row("Downs / revives", TextFormat("%d / %d", d.downs, d.revives));
        int eaten = 0; for (const auto& kv : m.eco.eatenBy) eaten += kv.second;
        row("Beasts eaten by beasts", TextFormat("%d", eaten));
        row("Time", TextFormat("%d:%02d", (int)m.time / 60, (int)m.time % 60));
        DrawTextCentered("Enter: dive again     Esc: leave the match", cx, p.y + p.height - 34, 15, Fade(paper, 0.8f));
    }
    if (S.mode == 1 && m.time < 12 && !S.shotMode)
        DrawTextCentered("WASD swim, Space/Ctrl up/down, Shift sprint, RMB aim, LMB fire, R reload, E use, V knife, G limpet, 1-3 weapons", cx, SCREEN_H - 20, 13, Fade(paper, 0.6f));
}

} // namespace rt

using namespace rt;

void StartRedTide(Game& g) {
    StartShip(1, (uint32_t)GetRandomValue(1, 1 << 30));
    S.shotMode = false;
    S.silhouette = 0;
    S.lineup = -1;
    DisableCursor();
    g.scene = Scene::RedTide;
}

void SceneRedTide(Game& g) {
    if (!S.active || !S.m) { StartRedTide(g); return; }
    if (S.mode == 1 && !S.levelReady && IsWindowReady()) BuildLevelModel();
    float dt = std::min(GetFrameTime(), 1 / 30.0f);
    if (S.shotMode) dt = 1 / 60.0f;
    S.time += dt;
    if (S.lineup < 0) {
        Input(dt);
        Match& m = M();
        if (S.mode == 0) { m.phase = TidePhase::Calm; m.phaseT = -1e9f; }   // the tank never tides
        m.Step(dt);
        DrainFx();
        if (m.over && !S.shotMode && IsKeyPressed(KEY_ENTER)) { StartRedTide(g); return; }
        DiverState& d = Me();
        S.bob += Vector3Length(d.vel) * dt * 2.2f;
        int z = m.eco.ZoneAt(d.pos);
        if (z >= 0 && z != S.lastZone) { S.lastZone = z; S.zoneT = S.mode == 1 ? 2.5f : 0; }
        if (S.zoneT > 0) S.zoneT -= dt;
    }
    for (auto& p : S.fx) { p.pos = Vector3Add(p.pos, Vector3Scale(p.vel, dt)); p.vel = Vector3Scale(p.vel, powf(0.3f, dt)); p.vel.y += 0.3f * dt; p.life -= dt; }
    S.fx.erase(std::remove_if(S.fx.begin(), S.fx.end(), [](const Particle& p) { return p.life <= 0; }), S.fx.end());
    Vector3 lo = M().map->boundsMin, hi = M().map->boundsMax;
    for (auto& s : S.snow) { s.y -= dt * 0.12f; s.x += sinf(S.time * 0.3f + s.z) * dt * 0.05f; if (s.y < lo.y) s.y += hi.y - lo.y; }
    DrawScene();
    if (S.lineup < 0 && S.silhouette < 0.5f) DrawHud();
    else if (S.lineup >= 0) TxtBold(TextFormat("Red Tide - the Sunken Ship's species, page %d (CreatureBuilder)", S.lineup + 1), 24, 18, 20, Color{220, 90, 80, 255});
}

// --shots: 0 the tank, 1 its silhouettes, 2-3 the species lineup, 10+ the Sunken Ship from set places
void DebugRedTideShot(Game& g, int which) {
    S.lineup = -1;
    S.silhouette = 0;
    if (which < 10) {
        StartTank();
        S.shotMode = true;
        S.silhouette = which == 1 ? 1.0f : 0.0f;
        S.lineup = which >= 2 ? which - 2 : -1;
        Me().pos = {0, 3.5f, -11};
        Me().yaw = 0.1f;
        Me().pitch = -0.08f;
        for (int i = 0; i < 240; i++) M().eco.Step(1 / 20.0f);   // let the tank settle into its schools
        g.scene = Scene::RedTide;
        return;
    }
    StartShip(1, 20260930);
    S.shotMode = true;
    Match& m = M();
    if (which != 10) {                                           // every door open, so the views can see through
        m.linkOpen.assign(m.linkOpen.size(), 1);
        for (auto& d : m.level.doors) d.open = true;
    }
    for (int i = 0; i < 200; i++) m.eco.Step(1 / 20.0f);
    DiverState& d = Me();
    d.invulnerable = true;
    // each view stands at an offset in a room and looks across it (yaw offset from the room's centre)
    auto place = [&](const char* zone, Vector3 off, float yawOff, float pitch) {
        int zi = m.map->ZoneIndex(zone);
        const Zone& z = m.map->zones[zi];
        d.pos = z.Clamp(Vector3Add({z.plan.x, z.y0, z.plan.y}, off), 0.6f);
        Vector3 c = z.Center();
        d.zone = zi; d.yaw = atan2f(c.x - d.pos.x, c.z - d.pos.z) + yawOff; d.pitch = pitch;
    };
    switch (which) {
        case 10: place("Bridge", {1.5f, 2.2f, 1.5f}, 0.3f, -0.05f); break;                  // the start pocket, the Gannet rack, the Salon door
        case 11: place("Grand Salon", {2, 3, 2}, 0.0f, 0.05f); break;                        // the salon: pillars, the air pocket, the croc
        case 12: place("Engine Room", {12.5f, 3, 11}, 0.0f, -0.05f); break;                  // the engine room and the Goliath, from across it
        case 13: place("The Keel & Sand", {6, 3, 4}, 0.4f, 0.12f); break;                    // outside: the sand, the mast, the hull
        case 14: {                                                                            // the HUD in a fight: a Wrecker Hunt
            place("Stern & Swim Platform", {2, 2, 2}, 0.0f, 0);
            m.BeginTidePublic(5);
            d.scrip = 3150; d.tonics = {"juggernaut", "quick"};
            m.ApplyDropPublic(DropType::DoubleScrip, d.pos);
            for (int i = 0; i < 60; i++) m.Step(1 / 20.0f);
            place("Stern & Swim Platform", {2, 2, 2}, 0.0f, 0);
            float bd = 1e9f;
            for (const auto& a : m.eco.agents) if (a.alive && m.map->species[a.sp].isEnemy && Vector3Distance(a.pos, d.pos) < bd) {
                bd = Vector3Distance(a.pos, d.pos);
                Vector3 to = Vector3Subtract(a.pos, m.Eye(d));
                d.yaw = atan2f(to.x, to.z); d.pitch = -0.05f;
            }
            d.hp = 140; d.hpMax = 250; d.heldT = 0; d.stunT = 0; d.vel = {0, 0, 0};
            break;
        }
        default: place("Cabin Deck", {3, 2, 3}, 0.0f, 0); break;                             // the cabins and the moray pipes
    }
    g.scene = Scene::RedTide;
}

// depth.exe --redtide-test: the stage 2 checks that need no window, on the test tank through the Match's rules. The
// diver aims at the nearest fish and fires the Cormorant; the darts must hit, wound, kill, bleed into the scent grid
// and pay scrip; the magazine must empty and reload one dart at a time; the diver must swim at the Movement sheet's
// speeds.
int RunRedTideTest() {
    StartTank();
    S.shotMode = true;
    int fails = 0;
    auto check = [&](bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; };
    printf("Red Tide stage 2 test (the test tank)\n");
    Match& m = M();
    DiverState& d = Me();
    d.hp = d.hpMax = 1e6f;
    // swimming: 2 m/s forward after the 0.4 s acceleration
    d.pos = {0, 3, -10}; d.yaw = 0; d.pitch = 0; d.vel = {0, 0, 0};
    Vector3 start = d.pos;
    for (int i = 0; i < 120; i++) { m.SteerDiver(0, {0, 0, 1}, 0, false, false, 1 / 60.0f); m.Step(1 / 60.0f); m.phase = TidePhase::Calm; }
    float speed = Vector3Length(d.vel), moved = Vector3Distance(start, d.pos);
    check(fabsf(speed - 2.0f) < 0.05f && moved > 3.0f && moved < 4.0f, TextFormat("swims at %.2f m/s (Movement sheet: 2.0), %.2f m in 2 s", speed, moved));
    // aim and fire until something dies
    int scrip0 = d.scrip;
    float blood0 = m.eco.scent.Total();
    int shots = 0, kills0 = d.kills, hits = 0;
    for (int attempt = 0; attempt < 400 && d.kills == kills0; attempt++) {
        int best = -1; float bd = 1e9f;
        for (int i = 0; i < (int)m.eco.agents.size(); i++) {
            const Agent& a = m.eco.agents[i];
            if (!a.alive || a.diver >= 0 || m.map->species[a.sp].size > 2) continue;
            float dist = Vector3Distance(a.pos, d.pos);
            if (dist < bd) { bd = dist; best = i; }
        }
        if (best < 0) break;
        const Agent& a = m.eco.agents[best];
        Vector3 dir = Vector3Normalize(Vector3Subtract(a.pos, d.pos));
        if (bd > 3.5f) d.pos = m.level.Move(d.pos, Vector3Subtract(a.pos, Vector3Scale(dir, 3)), 0.4f, m.linkOpen);
        dir = Vector3Normalize(Vector3Subtract(a.pos, m.Eye(d)));
        d.yaw = atan2f(dir.x, dir.z); d.pitch = asinf(std::clamp(dir.y, -1.0f, 1.0f));
        d.fireT = 0; d.reloading = false; d.vel = {0, 0, 0}; d.ads = true;
        Held& h = m.Cur(d);
        if (h.mag <= 0) { h.mag = 8; h.reserve = std::max(0, h.reserve - 8); }
        int before = d.scrip;
        m.Fire(0, true, 1 / 60.0f); shots++;
        for (int k = 0; k < 12; k++) { m.Step(1 / 60.0f); m.phase = TidePhase::Calm; }
        if (d.scrip > before) hits++;
    }
    check(hits > 0, TextFormat("darts hit (%d hits from %d shots)", hits, shots));
    check(d.kills > kills0, TextFormat("a beast dies to the Cormorant (%s)", d.lastKill.c_str()));
    check(d.scrip > scrip0, TextFormat("scrip pays for hits and the kill (%d -> %d)", scrip0, d.scrip));
    check(m.eco.scent.Total() > blood0 + 5, TextFormat("the kill bleeds into the scent grid (%.0f -> %.0f)", blood0, m.eco.scent.Total()));
    // the magazine empties and reloads one dart at a time (8 darts in the Cormorant's 1.4 s)
    Held& h = m.Cur(d);
    h.mag = 1; h.reserve = 20; d.fireT = 0; d.reloading = false;
    m.Fire(0, true, 0.01f);
    m.Step(0.01f);
    d.fireT = 0;
    m.Fire(0, true, 0.01f);                     // empty: starts the reload
    float t = 0; int seenPartial = 0;
    while (d.reloading && t < 5) { m.Step(1 / 60.0f); m.phase = TidePhase::Calm; t += 1 / 60.0f; if (h.mag > 0 && h.mag < 8) seenPartial = 1; }
    check(h.mag == 8 && seenPartial && t > 1.0f && t < 1.8f, TextFormat("an empty Cormorant reloads dart by dart (%d darts in %.2f s; rack reload 1.4 s)", h.mag, t));
    printf(fails ? "redtide-test: %d check(s) failed\n" : "redtide-test: all checks passed\n", fails);
    S.active = false;
    return fails ? 1 : 0;
}
