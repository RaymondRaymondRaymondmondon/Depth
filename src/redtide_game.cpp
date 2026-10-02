// Red Tide: the playable scene. A first-person diver in a Match (redtide_match.h: the rules, the Ecosystem, the
// Wreckers, the Goliath), drawn by the inked low-poly renderer. Stage 3 plays the Sunken Ship; the stage 2 test tank
// is the same Match on a one-room map (kept for --redtide-test, the silhouette check and the species lineup).
#include "redtide.h"
#include "redtide_match.h"
#include "redtide_render.h"
#include "redtide_profile.h"
#include "redtide_vis.h"
#include "game.h"
#include "sound.h"
#include "input.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace rt {

bool RedTidePageFrame(Game& g, float t);
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
    int studio = -1;               // --shots: the Visual Overhaul's studio (redtide_vis.cpp), which set
    bool freeze = false;           // --shots: hold the match still (a squad lined up for its portrait)
    struct Dress { std::string path; Matrix m; Color tint = WHITE; };
    std::vector<Dress> dress;      // the map kit's placed models (ShipDressing)
    int lastZone = -1; float zoneT = 0;
    float bob = 0;
    bool awarded = false; int awardTokens = 0; std::vector<std::string> awardLines;   // the arcade profile's pay for the match
    // sound (stage 9d): what was heard last frame, to diff against
    struct BeastEar { bool alive = false; float hp = 0; int st = 0; float painCd = 0, chewT = 0, alarmCd = 0; };
    std::vector<BeastEar> ears;
    float callCd = 0;
    int sndPhase = -1, sndSquads = 0, sndTonics = 0, sndDrops = 0, sndKills = 0, sndCrates = 0; bool sndDown = false, sndReload = false; float sndHurt = 0;
    bool audioOn = false;
};
static RedTideScene S;

static Match& M() { return *S.m; }
static std::string gSndMap = "ship";   // the map being played (for its sound palette)
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

static void ShipDressing(); void BuildLevelModel() {
    if (S.levelReady) { UnloadModel(S.level); S.levelReady = false; }
    if (!IsWindowReady() || S.mode != 1) return;
    S.dress.clear();
    const Match& m = M();
    const MapData& map = *m.map;
    MeshBuilder mb;
    // the map kits' models (tools/artgen/maps_rt.py) stand in for these props' boxes where they've been built
    static const Color CORAL[6] = {{236, 110, 96, 255}, {244, 170, 70, 255}, {176, 96, 196, 255}, {96, 196, 160, 255}, {240, 140, 160, 255}, {226, 200, 90, 255}};
    int coralN = 0;
    auto kit = [&](const char* id, Vector3 base, float sc, float yaw) {
        std::string path = std::string("redtide/maps/") + id + ".glb";
        if (!LoadAsset(path)) return false;
        std::string sid = id; bool coral = sid == "brain" || sid == "staghorn" || sid == "table" || sid == "fan";
        S.dress.push_back({path, MatrixMultiply(MatrixMultiply(MatrixScale(sc, sc, sc), MatrixRotateY(yaw)), MatrixTranslate(base.x, base.y, base.z)), coral ? CORAL[coralN++ % 6] : WHITE});   // (the reef's saturated colours on the pale baked coral)
        return true;
    };
    const Json& pal = map.extra["palette"];
    auto pc = [&](const Json& j, Color def) { return j.IsArr() ? Color{(unsigned char)j[0].I(), (unsigned char)j[1].I(), (unsigned char)j[2].I(), 255} : def; };
    for (const auto& v : m.level.vols) {
        if (v.zone < 0 || v.hidden) continue;                  // (a connector between a zone's boxes has no faces)
        const Zone& z = map.zones[v.zone];
        bool isVoid = false, open = false;
        for (const Json& vz : map.extra["void_zones"].a) if (vz.Str0() == z.name) isVoid = true;
        for (const Json& oz : map.extra["open_zones"].a) if (oz.Str0() == z.name) open = true;
        if (isVoid) continue;                                  // the abyss: no walls, no floor, only the dark
        bool outside = z.deck == "Outside" || !z.diverOk || open, upper = z.deck == "Upper";
        Color wall = outside ? Color{58, 64, 66, 255} : upper ? Color{112, 80, 54, 255} : Color{82, 90, 96, 255};
        Color floor = outside ? Color{166, 142, 96, 255} : upper ? Color{118, 58, 46, 255} : Color{70, 72, 66, 255};
        Color ceil = upper ? Color{88, 70, 50, 255} : Color{62, 68, 72, 255};
        if (pal.IsObj()) {
            // a map's own palette (the Cave's rock, its dry chambers' paler stone)
            const Json& pz = z.air && pal["Air"].IsObj() ? pal["Air"] : pal[z.deck].IsObj() ? pal[z.deck] : pal;
            wall = pc(pz["wall"], pc(pal["wall"], wall)); floor = pc(pz["floor"], pc(pal["floor"], floor)); ceil = pc(pz["ceil"], pc(pal["ceil"], ceil));
        }
        // the holes: every passage volume that crosses one of this room's faces
        for (int axis = 0; axis < 3; axis++) for (int side = 0; side < 2; side++) {
            float at = side ? (&v.hi.x)[axis] : (&v.lo.x)[axis];
            int ua = axis == 0 ? 1 : 0, va = axis == 2 ? 1 : 2;
            Vector2 lo{(&v.lo.x)[ua], (&v.lo.x)[va]}, hi{(&v.hi.x)[ua], (&v.hi.x)[va]};
            std::vector<std::pair<Vector2, Vector2>> holes;
            for (const auto& p : m.level.vols) {
                if (p.link >= 0) {
                    const Link& l = map.links[p.link];
                    if (l.from != v.zone && l.to != v.zone) continue;
                } else if (p.window >= 0) {
                    const Window& w = map.windows[p.window];
                    if (w.zone != v.zone && w.outside != v.zone) continue;
                } else if (p.zone == v.zone && &p != &v && !p.hidden && axis != 1) {
                    // another box of the same zone carrying on past this face: no wall where it does
                    float o = (&p.lo.x)[axis], e = (&p.hi.x)[axis];
                    bool beyond = side ? (o <= at + 0.01f && e > at + 0.01f) : (e >= at - 0.01f && o < at - 0.01f);
                    if (!beyond) continue;
                    holes.push_back({{(&p.lo.x)[ua], (&p.lo.x)[va]}, {(&p.hi.x)[ua], (&p.hi.x)[va]}});
                    continue;
                } else continue;
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
        // each end's box: the zone's box holding (or nearest) that mouth (a zone of parts has many)
        auto boxAt = [&](int zi, Vector3 mouth) -> const Volume& {
            const Volume* best = &m.level.vols[zi]; float bd = 1e18f;
            for (const auto& q : m.level.vols) {
                if (q.zone != zi || q.hidden) continue;
                Vector3 c{std::clamp(mouth.x, q.lo.x, q.hi.x), mouth.y, std::clamp(mouth.z, q.lo.z, q.hi.z)};
                float dd = Vector3Distance(c, mouth);
                if (dd < bd) { bd = dd; best = &q; }
            }
            return *best;
        };
        const Volume& A = boxAt(l.from, l.a);
        const Volume& B = boxAt(l.to, l.b);
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
    // portholes: a short tunnel through the hull and a brass ring on the inside
    for (const auto& w : map.windows) {
        int k = w.axis;
        bool outsideHigh = w.outHigh;
        float a0 = w.g0, a1 = w.g1;
        float zlo = w.g0, zhi = w.g0;   // (the room's face: the low side of the wall if the water is high, else the high side)
        if (!outsideHigh) zlo = w.g1;
        Vector3 lo = w.lo, hi = w.hi;
        (&lo.x)[k] = a0; (&hi.x)[k] = a1;
        Color hull{54, 58, 60, 255}, brass{196, 150, 70, 255};
        for (int axis = 0; axis < 3; axis++) {
            if (axis == k) continue;
            int ua = axis == 0 ? 1 : 0, va = axis == 2 ? 1 : 2;
            for (int side = 0; side < 2; side++) FaceWithHoles(mb, axis, side ? (&hi.x)[axis] : (&lo.x)[axis], {(&lo.x)[ua], (&lo.x)[va]}, {(&hi.x)[ua], (&hi.x)[va]}, {}, hull);
        }
        float face = outsideHigh ? zhi - 0.03f : zlo + 0.03f;
        Vector3 c = Vector3Lerp(w.lo, w.hi, 0.5f);
        float h = (w.hi.y - w.lo.y) / 2;
        (&c.x)[k] = face;
        Vector3 bar = k == 0 ? Vector3{0.05f, 0.12f, h + 0.12f} : Vector3{h + 0.12f, 0.12f, 0.05f};
        Vector3 post = k == 0 ? Vector3{0.05f, h + 0.12f, 0.12f} : Vector3{0.12f, h + 0.12f, 0.05f};
        Vector3 side = k == 0 ? Vector3{0, 0, h + 0.06f} : Vector3{h + 0.06f, 0, 0};
        mb.Box(Vector3Add(c, {0, h + 0.06f, 0}), bar, brass);
        mb.Box(Vector3Add(c, {0, -h - 0.06f, 0}), bar, brass);
        mb.Box(Vector3Add(c, side), post, brass);
        mb.Box(Vector3Subtract(c, side), post, brass);
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
    // the map's dressing (Level::props, placed by BuildLevel from extra.json "dressing")
    for (const Prop& pr : m.level.props) {
        const Zone& z = map.zones[pr.zone];
        uint32_t r = pr.seed;
        auto rnd = [&]() { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return (r & 0xFFFF) / 65535.0f; };
        Color rock = pc(pal["wall"], Color{70, 72, 70, 255});
        Color lighter{(unsigned char)std::min(255, rock.r + 20), (unsigned char)std::min(255, rock.g + 18), (unsigned char)std::min(255, rock.b + 12), 255};
        Vector3 c = pr.pos, h = pr.half;
        switch (pr.kind) {
            case PropKind::Column: mb.Box(c, h, Color{(unsigned char)(rock.r + 8), (unsigned char)(rock.g + 8), (unsigned char)(rock.b + 6), 255}); break;
            case PropKind::Stalagmite: if (kit("stalagmites", c, std::max(0.4f, h.y), rnd() * 6.28f)) break; mb.Cone(c, {c.x, c.y + h.y, c.z}, h.x, 5, rock); break;
            case PropKind::Stalactite: mb.Cone(c, {c.x, c.y - h.y, c.z}, h.x, 5, lighter); break;
            case PropKind::Crystal: if (kit("crystal", c, 2.6f, rnd() * 6.28f)) break; for (int j = 0; j < 5; j++) mb.Cone({c.x + (rnd() - 0.5f), c.y, c.z + (rnd() - 0.5f)}, {c.x + (rnd() - 0.5f) * 1.5f, c.y + 1 + rnd() * 2.5f, c.z + (rnd() - 0.5f) * 1.5f}, 0.18f + rnd() * 0.2f, 4, Color{150, 220, 230, 255}); break;
            case PropKind::Root: for (int j = 0; j < 4; j++) mb.Box({c.x + (rnd() - 0.5f) * 1.2f, c.y - 1.2f, c.z + (rnd() - 0.5f) * 1.2f}, {0.05f, 1.2f + rnd(), 0.05f}, Color{92, 76, 52, 255}); break;
            case PropKind::Ledge: mb.Box(c, h, rock); break;
            case PropKind::Pool: mb.Box(c, h, Color{40, 86, 96, 255}); break;
            case PropKind::Silt: mb.Box(c, h, Color{84, 76, 60, 255}); break;
            case PropKind::Machine:
                mb.Box(c, h, Color{96, 70, 50, 255});
                mb.Lathe(2.4f, 3, 10, [](float) { return 0.7f; }, [](float) { return 0.7f; }, Color{110, 96, 80, 255}, Color{80, 70, 60, 255}, {c.x + 2.4f, c.y - 0.5f, c.z});
                break;
            case PropKind::CoralWall: {
                // a wall of coral: a solid core with lumpy heads along its top and sides in the reef's colours
                static const Color cols[] = {{214, 120, 104, 255}, {226, 176, 92, 255}, {160, 104, 170, 255}, {110, 170, 140, 255}, {232, 150, 150, 255}};
                mb.Box(c, h, Color{156, 120, 96, 255});
                int n = (int)((h.x + h.z) * 2);
                for (int j = 0; j < n; j++) {
                    float t = rnd() * 2 - 1;
                    Vector3 q = h.x > h.z ? Vector3{c.x + t * h.x, c.y + h.y, c.z} : Vector3{c.x, c.y + h.y, c.z + t * h.z};
                    float rr = 0.4f + rnd() * 0.5f;
                    Vector3 hc{q.x, q.y - rnd() * h.y * 1.6f, q.z + (h.x > h.z ? (rnd() - 0.5f) * 1.4f : 0)};
                    // (every other head a coral model on the wall's top: brain and staghorn by turns)
                    if (j % 2 == 0 && kit(j % 4 == 0 ? "brain" : "staghorn", {hc.x, c.y + h.y - 0.2f, hc.z}, rr * 1.8f, rnd() * 6.28f)) continue;
                    mb.Box(hc, {rr, rr * 0.8f, rr}, cols[j % 5]);
                }
                break;
            }
            case PropKind::Brain: if (kit("brain", {c.x, z.y0, c.z}, std::max(0.5f, h.x * 2.0f), rnd() * 6.28f)) break; mb.Lathe(h.x * 2, 4, 8, [&](float u) { return h.x * sinf(u * 3.14159f) + 0.05f; }, [&](float u) { return h.y * sinf(u * 3.14159f) + 0.05f; }, Color{206, 180, 120, 255}, Color{176, 150, 100, 255}, c); break;
            case PropKind::Table: if (kit("table", {c.x, z.y0, c.z}, std::max(0.5f, (c.y - z.y0) / 0.47f), rnd() * 6.28f)) break; mb.Box(c, h, Color{176, 196, 150, 255}); mb.Box({c.x, (z.y0 + c.y) / 2, c.z}, {0.25f, (c.y - z.y0) / 2, 0.25f}, Color{150, 150, 120, 255}); break;
            case PropKind::Staghorn: if (kit("staghorn", c, std::max(0.6f, h.y * 1.4f), rnd() * 6.28f)) break; for (int j = 0; j < 7; j++) { float a = rnd() * 6.28f, lean = 0.3f + rnd() * 0.5f; mb.Cone({c.x, c.y, c.z}, {c.x + cosf(a) * lean * h.y, c.y + h.y * (0.6f + rnd() * 0.5f), c.z + sinf(a) * lean * h.y}, 0.09f, 4, Color{(unsigned char)(190 + rnd() * 40), (unsigned char)(140 + rnd() * 40), 110, 255}); } break;
            case PropKind::Seagrass: for (int j = 0; j < 6; j++) mb.Box({c.x + (rnd() - 0.5f) * h.x * 2, c.y + h.y / 2, c.z + (rnd() - 0.5f) * h.z * 2}, {0.03f, h.y / 2, 0.12f}, Color{96, 150, 80, 255}); break;
            case PropKind::Mangrove: for (int j = 0; j < 5; j++) { Vector3 top{c.x + (rnd() - 0.5f), c.y, c.z + (rnd() - 0.5f)}; mb.Cone(top, {top.x + (rnd() - 0.5f) * 2.5f, z.y0, top.z + (rnd() - 0.5f) * 2.5f}, 0.12f, 4, Color{100, 80, 58, 255}); } break;
            case PropKind::Mound: mb.Lathe(h.x * 2, 5, 10, [&](float u) { return h.x * sinf(u * 3.14159f) + 0.1f; }, [&](float u) { return h.y * 1.4f * sinf(u * 3.14159f) + 0.1f; }, Color{200, 150, 120, 255}, Color{150, 120, 100, 255}, c); break;
            case PropKind::Building: {
                // a drowned house of marble: walls, a roof slab, a dark doorway, a pair of columns at its front, weed
                Color marble{(unsigned char)(rock.r - 10 + rnd() * 20), (unsigned char)(rock.g - 10 + rnd() * 18), (unsigned char)(rock.b - 10 + rnd() * 16), 255};
                mb.Box(c, h, marble);
                mb.Box({c.x, c.y + h.y + 0.2f, c.z}, {h.x + 0.4f, 0.2f, h.z + 0.4f}, Color{(unsigned char)(marble.r - 30), (unsigned char)(marble.g - 30), (unsigned char)(marble.b - 26), 255});
                bool alongX = rnd() < 0.5f; float sgn = rnd() < 0.5f ? -1.0f : 1.0f;
                Vector3 door = alongX ? Vector3{c.x, z.y0 + 1.1f, c.z + sgn * (h.z + 0.02f)} : Vector3{c.x + sgn * (h.x + 0.02f), z.y0 + 1.1f, c.z};
                mb.Box(door, alongX ? Vector3{0.6f, 1.1f, 0.03f} : Vector3{0.03f, 1.1f, 0.6f}, Color{20, 26, 30, 255});
                for (int j = -1; j <= 1; j += 2) {
                    Vector3 col = alongX ? Vector3{c.x + j * 1.3f, c.y, c.z + sgn * (h.z + 0.35f)} : Vector3{c.x + sgn * (h.x + 0.35f), c.y, c.z + j * 1.3f};
                    mb.Box(col, {0.22f, h.y, 0.22f}, Color{(unsigned char)std::min(255, marble.r + 20), (unsigned char)std::min(255, marble.g + 20), (unsigned char)std::min(255, marble.b + 18), 255});
                }
                for (int j = 0; j < 3; j++) mb.Box({c.x + (rnd() - 0.5f) * h.x * 2, c.y + h.y - 0.5f, c.z + (rnd() - 0.5f) * h.z * 2}, {0.05f, 0.6f + rnd(), 0.05f}, Color{70, 110, 70, 255});
                break;
            }
            case PropKind::Terrace: mb.Box(c, h, Color{120, 110, 84, 255}); mb.Box({c.x, c.y + h.y + 0.02f, c.z}, {h.x - 0.2f, 0.02f, h.z - 0.2f}, Color{90, 130, 70, 255}); break;
            case PropKind::Fan: if (kit("fan", {c.x, z.y0, c.z}, std::max(0.6f, (c.y + h.y - z.y0) / 0.84f), rnd() * 6.28f)) break; for (int j = 0; j < 5; j++) mb.Box({c.x + (rnd() - 0.5f) * 0.6f, c.y + (rnd() - 0.3f) * h.y, c.z}, {h.x * (0.5f + rnd() * 0.5f), h.y * 0.35f, 0.03f}, Color{(unsigned char)(150 + rnd() * 60), 70, (unsigned char)(110 + rnd() * 50), 255}); mb.Box({c.x, z.y0 + (c.y - z.y0) / 2, c.z}, {0.05f, (c.y - z.y0) / 2, 0.05f}, Color{120, 60, 80, 255}); break;
            case PropKind::Amphora: if (kit("amphora", {c.x, c.y - h.y, c.z}, std::max(0.5f, h.y * 2.2f), rnd() * 6.28f)) break; mb.Lathe(h.y * 2, 4, 8, [&](float u) { return h.x * (0.4f + 0.6f * sinf(u * 3.14159f)); }, [&](float u) { return h.y * 2 * u - h.y; }, Color{170, 100, 60, 255}, Color{140, 80, 50, 255}, c); break;
            case PropKind::Grate: {
                mb.Box({c.x, c.y - 0.01f, c.z}, {h.x, 0.01f, h.z}, Color{12, 16, 18, 255});
                for (int j = -2; j <= 2; j++) { mb.Box({c.x + j * h.x * 0.4f, c.y + 0.02f, c.z}, {0.05f, 0.03f, h.z}, Color{70, 74, 70, 255}); mb.Box({c.x, c.y + 0.02f, c.z + j * h.z * 0.4f}, {h.x, 0.03f, 0.05f}, Color{70, 74, 70, 255}); }
                break;
            }
            case PropKind::Stake: mb.Box(c, h, Color{214, 206, 184, 255}); mb.Box({c.x, c.y + h.y, c.z}, {0.35f, 0.12f, 0.12f}, Color{200, 190, 170, 255}); break;
            case PropKind::Tank: { if (kit("tank", {c.x, z.y0, c.z}, std::max(0.6f, h.y), 0)) break;
                mb.Box({c.x, z.y0 + 0.15f, c.z}, {h.x + 0.1f, 0.15f, h.z + 0.1f}, Color{70, 74, 72, 255});
                mb.Box(c, {h.x, h.y, 0.04f}, Color{120, 170, 160, 255}); mb.Box(c, {0.04f, h.y, h.z}, Color{120, 170, 160, 255});
                mb.Box({c.x, c.y + h.y + 0.1f, c.z}, {h.x + 0.1f, 0.1f, h.z + 0.1f}, Color{70, 74, 72, 255});
                break;
            }
            case PropKind::Crenel: mb.Box(c, h, Color{(unsigned char)(rock.r - 20), (unsigned char)(rock.g - 20), (unsigned char)(rock.b - 16), 255}); break;
            default: break;
        }
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
    ShipDressing();
}

// The Sunken Ship's kit placed in its rooms (the Visual Overhaul, phase 6: tools/artgen/ship_rt.py): the salon's
// chandelier hanging askew, its armchairs piled against the wall, a table tipped on its side, the piano; the galley's
// range with its pots, crates and barrels; a bunk in every cabin; the engine room's generator; the bridge's wheel and
// binnacle; the funnel on the foredeck; barrels on the stern. (The models' frame: +x forward, +y up, the base at 0.)
// the other maps' landmarks from their kits (maps_rt.py): statues in Atlantis's forum and gate, braziers in its chapel,
// columns down its streets; giant clams on the reef's sand flats; glass sponges in the Void's galleries
static void MapDressing() {
    const MapData& map = *M().map;
    const std::string& key = M().mapKey;
    auto put = [&](const char* id, Vector3 at, float sc, float yaw) {
        std::string path = std::string("redtide/maps/") + id + ".glb";
        if (LoadAsset(path)) S.dress.push_back({path, MatrixMultiply(MatrixMultiply(MatrixScale(sc, sc, sc), MatrixRotateY(yaw)), MatrixTranslate(at.x, at.y, at.z)), WHITE});
    };
    for (const Zone& z : map.zones) {
        float x0 = z.plan.x, z0 = z.plan.y, w = z.plan.width, h = z.plan.height, cx = x0 + w / 2, cz = z0 + h / 2;
        auto has = [&](const char* k) { return z.name.find(k) != std::string::npos; };
        if (key == "atlantis") {
            if (has("Forum") || has("Gate")) { put("statue", {cx, z.y0, cz}, 1.6f, 0.5f); for (int k = -1; k <= 1; k += 2) put("column", {cx + k * w * 0.3f, z.y0, cz - h * 0.3f}, 1.3f, 0); }
            if (has("Chapel") || has("Temple")) for (int k = -1; k <= 1; k += 2) put("brazier", {cx + k * 3.0f, z.y0, cz}, 1.2f, 0);
            if (has("Town") || has("Market")) for (int k = 0; k < 3; k++) put("column", {x0 + w * (0.25f + 0.25f * k), z.y0, z0 + 1.2f}, 1.1f, 0);
        } else if (key == "reef") {
            if (has("Sand") || has("Lagoon")) for (int k = 0; k < 3; k++) put("clam", {x0 + w * (0.2f + 0.3f * k), z.y0, z0 + h * (0.3f + 0.2f * (k % 2))}, 0.9f + 0.3f * k, k * 1.3f);
        } else if (key == "void") {
            if (has("Galler") || has("Warren")) for (int k = 0; k < 5; k++) put("sponge", {x0 + w * (0.15f + 0.17f * k), z.y0, z0 + h * (0.25f + 0.5f * (k % 2))}, 0.9f + 0.25f * (k % 3), k * 0.7f);
        }
    }
}

static void ShipDressing() {
    if (M().mapKey != "ship") { MapDressing(); return; }
    const MapData& map = *M().map;
    auto put = [&](const char* id, Vector3 at, float yaw, Matrix tilt = MatrixIdentity()) {
        S.dress.push_back({std::string("redtide/ship/") + id + ".glb", MatrixMultiply(MatrixMultiply(tilt, MatrixRotateY(yaw)), MatrixTranslate(at.x, at.y, at.z))});
    };
    for (const Zone& z : map.zones) {
        float x0 = z.plan.x, z0 = z.plan.y, w = z.plan.width, h = z.plan.height, cx = x0 + w / 2, cz = z0 + h / 2;
        if (z.name == "Grand Salon") {
            put("chandelier", {cx + 1.5f, z.y1 - 1.5f, cz}, 0.3f, MatrixRotateZ(0.32f));
            for (int k = 0; k < 3; k++) put("armchair", {x0 + 1.0f, z.y0, z0 + 1.4f + k * 1.0f}, 0.4f * k - 0.3f);
            put("armchair", {x0 + 1.1f, z.y0 + 0.75f, z0 + 2.0f}, 1.9f, MatrixRotateX(1.1f));       // (thrown on top of the others)
            put("table", {cx + 2.5f, z.y0, cz - 1.5f}, 0.2f);
            put("table", {x0 + 2.6f, z.y0 + 0.55f, z0 + h - 1.6f}, 0.8f, MatrixRotateZ(1.5708f));  // (tipped on its side)
            put("piano", {x0 + w - 0.5f, z.y0, cz + 2.0f}, PI);
        } else if (z.name.find("Galley") != std::string::npos) {
            put("range", {x0 + 0.5f, z.y0, z0 + 2.0f}, 0.0f);
            for (int k = 0; k < 3; k++) put("crate", {x0 + w - 1.0f, z.y0 + (k == 2 ? 0.6f : 0.0f), z0 + 1.0f + (k % 2) * 0.9f}, 0.2f * k);
            for (int k = 0; k < 2; k++) put("barrel", {x0 + w - 0.8f - k * 0.7f, z.y0, z0 + h - 0.8f}, 0.0f);
        } else if (z.name == "Cabin Deck") {
            for (int k = 0; k < 6; k++) put("bunk", {x0 + (k + 0.5f) * w / 6, z.y0, z0 + h - 1.2f}, 1.5708f);
        } else if (z.name == "Engine Room") {
            put("generator", {x0 + 9.0f, z.y0, z0 + h - 2.0f}, 0.0f);
            for (int k = 0; k < 2; k++) put("barrel", {x0 + w - 1.0f, z.y0, z0 + 1.0f + k * 0.7f}, 0.0f);
        } else if (z.name == "Bridge") {
            put("wheel", {cx, z.y0, z0 + 1.0f}, -1.5708f);
        } else if (z.name.find("Foredeck") != std::string::npos) {
            put("funnel", {cx, z.y0, cz}, 0.0f);
        } else if (z.name.find("Stern") != std::string::npos) {
            for (int k = 0; k < 3; k++) put("barrel", {x0 + 1.0f + k * 0.75f, z.y0, z0 + 1.0f}, 0.0f);
            put("crate", {x0 + 1.4f, z.y0, z0 + 2.2f}, 0.4f);
        }
    }
}

// ---------------------------------------------------------------- starting a match
static void StartShip(int players, uint32_t seed, const std::string& key = "ship") {
    S.m = std::make_unique<Match>();
    S.m->Init(key, players, seed, false);
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
// ---------------------------------------------------------------- sound (stage 9d)
// The Match is headless and knows nothing of audio; the scene hears it by diffing its state each frame.
static float EarPan(Vector3 p) {
    const DiverState& d = Me();
    Vector3 to = Vector3Subtract(p, d.pos); to.y = 0;
    float l = Vector3Length(to);
    if (l < 0.01f) return 0;
    Vector3 right{cosf(d.yaw), 0, -sinf(d.yaw)};
    return std::clamp(Vector3DotProduct(Vector3Scale(to, 1 / l), right), -1.0f, 1.0f);
}
static float EarDist(Vector3 p) { return Vector3Distance(p, Me().pos); }
static int RtMapIndex(const std::string& key) { return key == "cave" ? 1 : key == "reef" ? 2 : key == "atlantis" ? 3 : key == "void" ? 4 : 0; }
static void FxSound(const FxEvent& e) {
    static const int K[11] = {RTC_HIT, RTC_WALL, RTC_SHOT, RTC_BLAST, RTC_PICKUP, RTC_BLAST, RTC_ARC, RTC_CRATE, RTC_MELEE, RTC_BLAST, RTC_ARC};
    if (e.kind < 0 || e.kind > 10) return;
    float vol = e.kind == 5 ? 1.3f : e.kind == 9 ? 0.4f : e.kind == 1 ? 0.6f : 1.0f;
    RedTideCue(K[e.kind], vol, EarPan(e.pos), EarDist(e.pos));
}
static void SoundFrame(float dt) {
    Match& m = M();
    DiverState& d = Me();
    // the match's state for the music
    RtAudio a;
    a.on = S.audioOn;
    a.map = RtMapIndex(S.mode == 0 ? "ship" : gSndMap);
    a.mode = m.over ? 3 : m.phase == TidePhase::Hunt ? 2 : m.phase == TidePhase::Tide ? 1 : 0;
    a.tide = m.tide;
    a.quota = m.quota > 0 ? std::clamp((float)m.tideKills / m.quota, 0.0f, 1.0f) : 0;
    a.scent = std::clamp(m.eco.Smell(d.pos, m.eco.ZoneAt(d.pos), 8) / 80.0f, 0.0f, 1.0f);
    float calm = m.map->tunables.count("calm_seconds") ? (float)m.map->tunables.at("calm_seconds") : 20.0f;
    a.countdown = m.phase == TidePhase::Calm ? calm - m.phaseT : 0;
    float best = 40;
    for (int i = 0; i < (int)m.eco.agents.size(); i++) {
        const Agent& g = m.eco.agents[i];
        if (!g.alive || g.diver >= 0 || m.IsBoss(i) || m.map->species[g.sp].tier < 4) continue;
        float dd = EarDist(g.pos);
        if (dd < best) { best = dd; a.predatorPan = EarPan(g.pos); }
    }
    a.predator = best < 40 ? 1 - best / 40 : 0;
    a.boss = m.bossActive; a.bossPhase = m.bossPhase;
    a.downed = d.downed; a.hp = d.hpMax > 0 ? d.hp / d.hpMax : 1;
    AudioRedTide(a);
    if (!S.audioOn) return;
    // the quips
    for (const auto& q : m.quipOut) RedTideQuip(q.voice, q.syllables, q.diver == 0 ? 0 : EarPan(q.pos));
    m.quipOut.clear();
    // the match's moments
    int ph = (int)m.phase;
    if (S.sndPhase >= 0 && ph != S.sndPhase) {
        if (m.phase == TidePhase::Tide || m.phase == TidePhase::Hunt) RedTideCue(RTC_BELL, 1, 0, 0);
        else if (m.phase == TidePhase::Calm) RedTideCue(RTC_CLEAR, 0.8f, 0, 0);
    }
    S.sndPhase = ph;
    if (m.eco.squadsSpawned > S.sndSquads) RedTideCue(RTC_ARRIVAL, 1, 0, 0);
    S.sndSquads = m.eco.squadsSpawned;
    if ((int)d.tonics.size() > S.sndTonics) RedTideCue(RTC_TONIC, 1, 0, 0);
    S.sndTonics = (int)d.tonics.size();
    if ((int)m.drops.size() > S.sndDrops) { const FloorDrop& fd = m.drops.back(); if (fd.weapon < 0 || true) RedTideCue(RTC_DROP, 0.9f, EarPan(fd.pos), EarDist(fd.pos) * 0.5f); }
    S.sndDrops = (int)m.drops.size();
    if (d.kills > S.sndKills) RedTideCue(RTC_KILL, 1, 0, 0);
    S.sndKills = d.kills;
    if ((int)m.crates.size() > S.sndCrates) { const Crate& c = m.crates.back(); RedTideCue(RTC_HAZARD, 0.8f, EarPan(c.pos), EarDist(c.pos) * 0.5f); }
    S.sndCrates = (int)m.crates.size();
    if (d.downed && !S.sndDown) RedTideCue(RTC_DOWN, 1, 0, 0);
    if (!d.downed && S.sndDown && !d.dead) RedTideCue(RTC_REVIVE, 1, 0, 0);
    S.sndDown = d.downed;
    if (d.reloading && !S.sndReload) RedTideCue(RTC_RELOAD, 0.8f, 0.3f, 0);
    S.sndReload = d.reloading;
    if (d.hurtT > S.sndHurt + 0.05f) RedTideCue(RTC_HIT, 0.5f, EarPan(d.hurtFrom), 0);
    S.sndHurt = d.hurtT;
    // the beasts: deaths, pain, alarm, feeding and their idle calls; far ones muffled, only a few a frame
    auto& E = S.ears;
    if (E.size() != m.eco.agents.size()) E.resize(m.eco.agents.size());
    S.callCd -= dt;
    int budget = 3;
    for (int i = 0; i < (int)m.eco.agents.size(); i++) {
        const Agent& g = m.eco.agents[i];
        auto& e = E[i];
        bool wasAlive = e.alive; float wasHp = e.hp; int wasSt = e.st;
        e.alive = g.alive; e.hp = g.hp; e.st = (int)g.st;
        e.painCd -= dt; e.alarmCd -= dt;
        if (g.diver >= 0) continue;
        const Species& sp = m.map->species[g.sp];
        float dd = EarDist(g.pos);
        if (dd > 70 || budget <= 0) continue;
        float size = (float)sp.size * (m.IsBoss(i) ? 1.5f : 1.0f), pan = EarPan(g.pos);
        const char* nm = sp.name.c_str();
        if (wasAlive && !g.alive) { RedTideBeast(nm, size, CUE_DEATH, dd, pan); budget--; continue; }
        if (!g.alive) continue;
        if (wasAlive && g.hp < wasHp - 0.5f && e.painCd <= 0) { RedTideBeast(nm, size, CUE_PAIN, dd, pan); e.painCd = 0.7f; budget--; continue; }
        if (wasAlive && g.st == State::Hunt && wasSt != (int)State::Hunt && e.alarmCd <= 0 && sp.size >= 2) { RedTideBeast(nm, size, CUE_ALARM, dd, pan); e.alarmCd = 6; budget--; continue; }
        if (g.st == State::Feed && dd < 40 && (e.chewT -= dt) <= 0) { e.chewT = 1.6f; RedTideBeast(nm, size, CUE_CHEW, dd, pan); budget--; continue; }
        if (S.callCd <= 0 && dd < 30 && GetRandomValue(0, 9999) < (int)(dt * 150)) { RedTideBeast(nm, size, CUE_CALL, dd, pan); S.callCd = 0.5f; budget--; }
    }
}

static void DrainFx() {
    for (const FxEvent& e : M().fx) {
        if (S.audioOn) FxSound(e);
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
            case 9: Burst(e.pos, 50, {20, 18, 26, 255}, 1.6f, 3.0f, 0.18f); break;     // an ink cap's cloud
            case 10: { for (int k = 0; k < 40; k++) { float f = k / 40.0f; Vector3 p = Vector3Add(e.pos, Vector3Scale(e.dir, f)); S.fx.push_back({p, Vector3Scale(Vector3Normalize(e.dir), 2.0f), 0.4f, 0.4f, {200, 220, 255, 255}, 0.05f + f * 0.2f}); } break; }   // the Resonator's ring
        }
    }
    M().fx.clear();
}

// ---------------------------------------------------------------- input
static void Input(float dt) {
    Match& m = M();
    DiverState& d = Me();
    if (S.shotMode || m.over) { m.SteerDiver(0, {0, 0, 0}, 0, false, false, dt); return; }
    Vector2 md = MouseLook(true);
    float sens = d.ads ? 0.0016f : 0.0025f;
    d.yaw -= md.x * sens;
    d.pitch = std::clamp(d.pitch - md.y * sens, -1.45f, 1.45f);
    // aim sway (a cuttlefish's flash, a flinch)
    if (d.aimSway > 0 || d.flinchT > 0) { d.yaw += sinf(S.time * 7.1f) * 0.004f; d.pitch += cosf(S.time * 5.3f) * 0.003f; }
    Vector3 f{sinf(d.yaw), 0, cosf(d.yaw)}, r{-f.z, 0, f.x};
    Vector3 want{0, 0, 0};
    if (IsKeyDown(KEY_W)) want = Vector3Add(want, f);
    if (IsKeyDown(KEY_S)) want = Vector3Subtract(want, f);
    if (IsKeyDown(KEY_D)) want = Vector3Add(want, r);        // r is the camera's right: D strafes right, A left
    if (IsKeyDown(KEY_A)) want = Vector3Subtract(want, r);
    float vert = (IsKeyDown(KEY_SPACE) ? 1.0f : 0.0f) - (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_C) ? 1.0f : 0.0f);
    m.SteerDiver(0, want, vert, IsKeyDown(KEY_LEFT_SHIFT), IsMouseButtonDown(MOUSE_BUTTON_RIGHT), dt);
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && !d.downed && !d.reloading && m.Cur(d).mag <= 0) RedTideCue(RTC_EMPTY, 1, 0, 0);
    m.Fire(0, IsMouseButtonDown(MOUSE_BUTTON_LEFT), dt);
    if (IsKeyPressed(KEY_R)) m.Reload(0);
    if (IsKeyPressed(KEY_V) || IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) m.Melee(0);
    if (IsKeyPressed(KEY_G)) m.ThrowLimpet(0);
    if (IsKeyPressed(KEY_Q)) m.CycleTactical(0);
    if (IsKeyPressed(KEY_B)) m.UseBuild(0);
    if (IsKeyPressed(KEY_X)) m.UseBrush(0);
    if (IsKeyPressed(KEY_Z)) { int si = m.NearestStation(d.pos, 3.0f); if (si >= 0 && m.level.stations[si].type == StationType::Workbench) m.CycleBench(0); }
    if (IsKeyPressed(KEY_F)) m.BeatDrum(0);
    if (IsKeyPressed(KEY_T)) m.UseCharm(0);
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
    // Verne-era salvage: brass and copper fittings, dark iron, oiled wood, glass gauges and cartridges, rubber. Each gun
    // is built from a dozen or so parts so it reads as a thing with a mechanism (the playtest found the old ones bare).
    Color brass{176, 134, 62, 255}, brassD{128, 94, 38, 255}, dark{70, 56, 36, 255}, wood{104, 66, 40, 255}, woodL{134, 92, 56, 255};
    Color glass{120, 176, 190, 255}, glassG{140, 220, 170, 255}, iron{80, 86, 90, 255}, ironD{52, 56, 60, 255}, copper{186, 110, 60, 255}, rubber{34, 30, 30, 255}, steel{160, 166, 170, 255};
    auto cyl = [&](MeshBuilder& mb, Vector3 at, float len, float r, Color c, int segs = 8) { mb.Lathe(len, 2, segs, [r](float) { return r; }, [r](float) { return r; }, c, c, at); };
    auto ring = [&](MeshBuilder& mb, Vector3 at, float r, float w, Color c) { mb.Lathe(w, 2, 10, [r](float) { return r; }, [r](float) { return r; }, c, c, at); };
    auto grip = [&](MeshBuilder& mb, Vector3 at, float h, Color c) { mb.Box(at, {0.016f, h, 0.024f}, c); mb.Box({at.x + 0.017f, at.y, at.z}, {0.002f, h * 0.8f, 0.018f}, woodL); mb.Box({at.x - 0.017f, at.y, at.z}, {0.002f, h * 0.8f, 0.018f}, woodL); };
    auto trigger = [&](MeshBuilder& mb, float z) { mb.Box({0, -0.028f, z}, {0.003f, 0.012f, 0.004f}, steel); mb.Box({0, -0.042f, z + 0.004f}, {0.004f, 0.003f, 0.022f}, ironD); mb.Box({0, -0.03f, z + 0.024f}, {0.004f, 0.012f, 0.003f}, ironD); };
    auto sight = [&](MeshBuilder& mb, float z, float y) { mb.Box({0, y, z}, {0.003f, 0.01f, 0.004f}, ironD); };
    for (int i = 0; i < 8; i++) {
        MeshBuilder mb;
        switch (i) {
            case 0:   // gas pistol (the Cormorant): a brass barrel over a pressure tank with a gauge, a wooden grip
                cyl(mb, {0, 0, 0.0f}, 0.26f, 0.014f, brass);
                ring(mb, {0, 0, 0.24f}, 0.02f, 0.02f, brassD);
                cyl(mb, {0, -0.03f, 0.02f}, 0.14f, 0.026f, iron, 10);           // the tank
                ring(mb, {0, -0.03f, 0.02f}, 0.03f, 0.01f, brassD); ring(mb, {0, -0.03f, 0.14f}, 0.03f, 0.01f, brassD);
                mb.Octa({0.0f, 0.005f, 0.11f}, 0.014f, glass);                   // the gauge
                mb.Box({0, 0.01f, 0.11f}, {0.016f, 0.004f, 0.016f}, brass);
                mb.Box({0, -0.06f, 0.06f}, {0.006f, 0.02f, 0.006f}, copper);     // the feed pipe
                grip(mb, {0, -0.085f, -0.03f}, 0.05f, wood);
                trigger(mb, -0.01f);
                sight(mb, 0.24f, 0.022f);
                break;
            case 1:   // needler: a slim barrel on rails, a glass clip of needles under it, a wooden stock
                cyl(mb, {0, 0, 0.0f}, 0.34f, 0.011f, iron);
                mb.Box({0, 0.016f, 0.15f}, {0.004f, 0.002f, 0.17f}, steel); mb.Box({0, -0.016f, 0.15f}, {0.004f, 0.002f, 0.17f}, steel);   // rails
                mb.Box({0, -0.045f, 0.1f}, {0.014f, 0.026f, 0.07f}, glass);     // the clip
                for (int k = 0; k < 5; k++) mb.Box({0, -0.045f, 0.045f + k * 0.027f}, {0.011f, 0.02f, 0.002f}, glassG);   // needles in it
                mb.Box({0, -0.045f, 0.1f}, {0.016f, 0.004f, 0.072f}, brassD);
                mb.Box({0, -0.02f, -0.07f}, {0.016f, 0.028f, 0.07f}, wood);     // the stock
                grip(mb, {0, -0.075f, -0.04f}, 0.045f, wood);
                trigger(mb, -0.02f);
                sight(mb, 0.32f, 0.018f); sight(mb, 0.0f, 0.02f);
                break;
            case 2:   // powder carbine: an iron barrel in a wooden fore-end, a bolt, a brass butt plate
                cyl(mb, {0, 0, 0.0f}, 0.46f, 0.012f, iron);
                mb.Box({0, -0.018f, 0.12f}, {0.022f, 0.02f, 0.2f}, wood);        // the fore-end
                ring(mb, {0, 0, 0.3f}, 0.018f, 0.012f, brass);                   // a barrel band
                mb.Box({0, -0.02f, -0.07f}, {0.024f, 0.034f, 0.1f}, wood);      // the stock
                mb.Box({0, -0.02f, -0.17f}, {0.026f, 0.036f, 0.006f}, brass);   // the butt plate
                mb.Box({0, 0.012f, -0.02f}, {0.016f, 0.012f, 0.05f}, ironD);    // the receiver
                mb.Box({0.028f, 0.018f, -0.03f}, {0.012f, 0.004f, 0.004f}, steel); mb.Octa({0.04f, 0.018f, -0.03f}, 0.007f, steel);   // the bolt
                trigger(mb, -0.03f);
                sight(mb, 0.44f, 0.018f); mb.Box({0, 0.024f, 0.0f}, {0.008f, 0.004f, 0.006f}, ironD);
                break;
            case 3:   // scatter gun: two fat barrels side by side, a break hinge, a short wooden stock
                cyl(mb, {-0.017f, 0, 0.02f}, 0.3f, 0.015f, iron); cyl(mb, {0.017f, 0, 0.02f}, 0.3f, 0.015f, iron);
                ring(mb, {-0.017f, 0, 0.3f}, 0.017f, 0.014f, ironD); ring(mb, {0.017f, 0, 0.3f}, 0.017f, 0.014f, ironD);
                mb.Box({0, -0.004f, 0.16f}, {0.034f, 0.006f, 0.12f}, ironD);    // the rib between them
                mb.Box({0, -0.02f, 0.0f}, {0.036f, 0.028f, 0.03f}, brassD);     // the hinge block
                mb.Box({0, -0.025f, -0.08f}, {0.03f, 0.04f, 0.09f}, wood);      // the stock
                mb.Box({0, -0.025f, -0.17f}, {0.03f, 0.04f, 0.005f}, rubber);   // the pad
                trigger(mb, -0.03f);
                sight(mb, 0.3f, 0.016f);
                break;
            case 4:   // speargun: a long wooden rail, rubber bands at the muzzle, a line reel, and the spear on top
                mb.Box({0, 0, 0.2f}, {0.013f, 0.018f, 0.4f}, wood);
                mb.Box({0, 0.022f, 0.2f}, {0.004f, 0.004f, 0.4f}, ironD);       // the spear's groove
                mb.Box({0, 0.03f, 0.25f}, {0.004f, 0.004f, 0.45f}, steel);      // the spear
                mb.Cone({0, 0.03f, 0.7f}, {0, 0.03f, 0.76f}, 0.008f, 6, steel); // its tip
                for (int s = -1; s <= 1; s += 2) { mb.Box({s * 0.02f, 0.012f, 0.4f}, {0.005f, 0.005f, 0.2f}, rubber); mb.Box({s * 0.024f, 0.012f, 0.6f}, {0.008f, 0.008f, 0.012f}, ironD); }   // the bands
                mb.Lathe(0.02f, 2, 12, [](float) { return 0.03f; }, [](float) { return 0.03f; }, brassD, brassD, {-0.03f, -0.02f, 0.02f});   // the line reel
                mb.Box({0, -0.045f, -0.1f}, {0.018f, 0.045f, 0.03f}, dark);     // the grip
                trigger(mb, -0.07f);
                break;
            case 5:   // gatling needler: four barrels in a ring with a crank, a feed drum of glass
                for (int k = 0; k < 4; k++) cyl(mb, {cosf(k * 1.57f) * 0.025f, sinf(k * 1.57f) * 0.025f, 0.02f}, 0.36f, 0.008f, iron, 6);
                ring(mb, {0, 0, 0.1f}, 0.036f, 0.012f, brassD); ring(mb, {0, 0, 0.34f}, 0.036f, 0.012f, brassD);
                mb.Box({0, -0.02f, -0.05f}, {0.045f, 0.05f, 0.08f}, brass);     // the receiver
                mb.Lathe(0.05f, 2, 12, [](float) { return 0.032f; }, [](float) { return 0.032f; }, glass, glass, {0, 0.06f, -0.05f});   // the drum
                for (int k = 0; k < 6; k++) mb.Box({cosf(k * 1.05f) * 0.02f, 0.06f + sinf(k * 1.05f) * 0.02f, -0.025f}, {0.004f, 0.004f, 0.016f}, glassG);
                mb.Box({0.06f, -0.02f, -0.05f}, {0.016f, 0.004f, 0.004f}, steel); mb.Box({0.075f, -0.035f, -0.05f}, {0.004f, 0.016f, 0.004f}, steel); mb.Octa({0.075f, -0.05f, -0.05f}, 0.009f, wood);   // the crank
                grip(mb, {0, -0.09f, -0.06f}, 0.04f, wood);
                trigger(mb, -0.08f);
                break;
            case 6:   // launcher: a copper tube with a breech, a pressure gauge, a shoulder stock and a leaf sight
                cyl(mb, {0, 0, 0.0f}, 0.5f, 0.045f, copper, 12);
                ring(mb, {0, 0, 0.0f}, 0.05f, 0.03f, brassD); ring(mb, {0, 0, 0.48f}, 0.05f, 0.02f, brassD);
                mb.Box({0, -0.06f, 0.1f}, {0.02f, 0.016f, 0.05f}, ironD);       // the breech latch
                mb.Octa({0.0f, 0.052f, 0.2f}, 0.014f, glass); mb.Box({0, 0.056f, 0.2f}, {0.016f, 0.004f, 0.016f}, brass);   // the gauge
                mb.Box({0, -0.07f, -0.08f}, {0.022f, 0.03f, 0.08f}, wood);      // the stock
                grip(mb, {0, -0.1f, 0.0f}, 0.04f, wood);
                trigger(mb, 0.02f);
                mb.Box({0, 0.05f, 0.42f}, {0.003f, 0.02f, 0.003f}, ironD); mb.Box({0, 0.07f, 0.42f}, {0.012f, 0.003f, 0.003f}, ironD);   // the leaf sight
                break;
            default:  // melee: a gaff hook on a wrapped shaft, with a trident's three tines
                mb.Box({0, 0, 0.2f}, {0.012f, 0.012f, 0.38f}, wood);
                for (int k = 0; k < 6; k++) ring(mb, {0, 0, -0.1f + k * 0.03f}, 0.014f, 0.012f, k % 2 ? rubber : dark);   // the wrapped grip
                ring(mb, {0, 0, 0.56f}, 0.016f, 0.02f, brassD);                 // the ferrule
                mb.Box({0, 0, 0.62f}, {0.004f, 0.004f, 0.05f}, steel); mb.Cone({0, 0, 0.66f}, {0, 0, 0.72f}, 0.006f, 6, steel);          // the middle tine
                for (int s = -1; s <= 1; s += 2) { mb.Box({s * 0.025f, 0, 0.61f}, {0.004f, 0.004f, 0.04f}, steel); mb.Cone({s * 0.025f, 0, 0.64f}, {s * 0.025f, 0, 0.7f}, 0.006f, 6, steel); }
                mb.Box({0, 0, 0.59f}, {0.03f, 0.006f, 0.006f}, steel);          // the crossbar
                mb.Box({0, 0.02f, 0.5f}, {0.004f, 0.012f, 0.004f}, steel); mb.Cone({0, 0.03f, 0.5f}, {0, 0.045f, 0.46f}, 0.005f, 6, steel);   // the gaff hook
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
    float kick = d.recoil * 0.035f * w.handling.recoil;
    // (a melee weapon is carried upright in the right fist at the bottom corner, blade up and forward, as any shooter's knife)
    bool upright = w.cls == "melee" && !d.ads;
    float side = d.ads ? 0.0f : upright ? 0.2f : 0.18f, low = d.ads ? -0.075f : upright ? -0.17f : -0.115f;   // (far enough out that the hands on it are in view)
    // the reload: the gun drops and rolls out to the side (0-35%), the magazine, clip or drum comes out and a fresh
    // one goes in (35-75%), the gun snaps back up with a little overshoot (75-100%); a thumb-loaded gun just dips
    // for each round
    float rl = d.reloading ? std::clamp(d.reloadT / std::max(0.2f, w.reload), 0.0f, 1.0f) : -1;
    float dip = 0, roll = 0, magOut = 0;
    if (rl >= 0) {
        if (w.perRound) { dip = 0.03f + 0.02f * sinf(d.reloadT * 14); roll = 0.3f; }
        else {
            float drop = rl < 0.35f ? rl / 0.35f : rl < 0.75f ? 1.0f : 1 - (rl - 0.75f) / 0.25f;
            dip = 0.07f * drop - (rl > 0.9f ? 0.015f * sinf((rl - 0.9f) / 0.1f * 3.14f) : 0);
            roll = 0.55f * drop;
            magOut = rl < 0.35f ? 0 : rl < 0.55f ? (rl - 0.35f) / 0.2f : rl < 0.75f ? 1 - (rl - 0.55f) / 0.2f : 0;
        }
    }
    // the hands' own moments (local to your view): a tonic just bought is drunk (the gun drops a little, the left hand
    // brings the bottle to the helmet's valve), and reviving a teammate puts the gun down out of view for both hands
    static std::set<std::string> lastTonics; static float drinkT = -1; static std::string drinkId; static const Match* lastMatch = nullptr;
    if (lastMatch != &M()) { lastMatch = &M(); lastTonics = d.tonics; drinkT = -1; }   // (a new match: what it starts with isn't drunk)
    for (const auto& tn : d.tonics) if (!lastTonics.count(tn)) { drinkT = 0; drinkId = tn; }
    lastTonics = d.tonics;
    if (const char* dk = getenv("DEPTH_VMDRINK")) { drinkT = (float)atof(dk) * 1.4f; drinkId = "juggernaut"; }   // (shots: hold the drink at k)
    if (drinkT >= 0 && !getenv("DEPTH_VMDRINK")) { drinkT += GetFrameTime(); if (drinkT > 1.4f) drinkT = -1; }
    bool reviving = false;
    for (const auto& o : M().divers) if (&o != &d && o.downed && o.reviveT > 0 && o.reviveTouchT > 0 && Vector3Distance(o.pos, d.pos) < 3.5f) reviving = true;
    if (getenv("DEPTH_VMREVIVE")) reviving = true;
    dip += drinkT >= 0 ? sinf(std::min(1.0f, drinkT / 1.4f) * PI) * 0.1f : 0;
    if (reviving) dip += 0.45f;
    // the melee swing: wound back and up, then chopped down and across, then back to the guard
    float ms = d.meleeT > 0 ? std::clamp(d.meleeT * 2.2f, 0.0f, 1.0f) : -1;
    float meleeFwd = 0, meleeYaw = 0, meleePitch = 0, meleeUp = 0;
    if (ms >= 0) {
        float wind = ms < 0.25f ? ms / 0.25f : ms < 0.55f ? 1 - (ms - 0.25f) / 0.3f * 2 : -1 + (ms - 0.55f) / 0.45f;   // +1 wound, -1 struck through
        meleeFwd = (1 - fabsf(wind)) * 0.14f; meleeYaw = wind * 0.7f; meleePitch = -wind * 0.9f; meleeUp = wind > 0 ? wind * 0.05f : wind * 0.02f;
    }
    Vector3 p = Vector3Add(cam.position, Vector3Add(Vector3Scale(f, 0.40f - kick + meleeFwd), Vector3Add(Vector3Scale(right, side + bobx + meleeYaw * 0.08f), Vector3Scale(up, low - boby - dip + meleeUp))));
    Matrix m = MatrixIdentity();
    Vector3 rx = Vector3Scale(right, -1);
    m.m0 = rx.x; m.m1 = rx.y; m.m2 = rx.z;
    m.m4 = up.x; m.m5 = up.y; m.m6 = up.z;
    m.m8 = f.x; m.m9 = f.y; m.m10 = f.z;
    m.m12 = p.x; m.m13 = p.y; m.m14 = p.z;
    // (turned a little in toward the crosshair, so its left side shows past the fist)
    Matrix tilt = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixRotateX(upright ? -1.05f : 0.0f), MatrixRotateZ(roll + (upright ? 0.25f : 0.0f))), MatrixRotateY(meleeYaw + (d.ads ? 0.0f : 0.1f))), MatrixRotateX(-d.recoil * 0.25f * w.handling.recoil + meleePitch));
    // the Locker room's finish on the gun, and the suit's colour on the glove that holds it
    const Profile& prof = GetProfile();
    Color fin = FinishColor(prof.finish), tint = WHITE;
    if (fin.a > 0) tint = {(unsigned char)(fin.r * 0.6f + 102), (unsigned char)(fin.g * 0.6f + 102), (unsigned char)(fin.b * 0.6f + 102), 255};
    Matrix gunM = MatrixMultiply(tilt, m);
    Color suit = SuitColor(prof.suit);
    // the baked gun (the Visual Overhaul, phase 4) where one has been built: its own grip where the old model's was,
    // its parts posed by the shot, the reload and what's left in it; the fists on its own grip markers
    {
        std::string wid = w.id == "knife" || w.id == "diversknife" ? "knife" : w.id;
        if (const Model* bm = RtWeaponModel(wid)) {
            (void)bm;
            static const Vector3 OLDGRIP[8] = {{0, -0.08f, -0.03f}, {0, -0.07f, -0.04f}, {0, -0.04f, -0.06f}, {0, -0.04f, -0.05f}, {0, -0.06f, -0.1f}, {0, -0.085f, -0.06f}, {0, -0.1f, 0.0f}, {0, 0, -0.05f}};
            Vector3 g = RtWeaponMarker(wid, "grip_r", {0, 0, 0}), og = OLDGRIP[GunModelFor(w.cls)];
            // (the asset: +x along the barrel, +y up, +z its right; the gun's frame here: +z ahead, +y up, +x its left)
            // (a viewmodel's licence: half as big again, so the gun, not the stylised glove, fills the hands)
            Matrix frame = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixTranslate(-g.x, -g.y, -g.z), MatrixScale(1.5f, 1.5f, 1.5f)), MatrixRotateY(-PI / 2)), MatrixMultiply(MatrixTranslate(og.x, og.y, og.z), gunM));
            const Held& h = d.weapons.empty() ? d.downHeld : d.weapons[std::clamp(d.cur, 0, (int)d.weapons.size() - 1)];
            RtGunAnim an;
            an.fire = std::clamp((d.recoil - 0.6f) * 2.5f, 0.0f, 1.0f);
            an.cycle = d.fireT > 0 ? std::clamp(1 - d.fireT * w.rpm / 60.0f, 0.0f, 1.0f) : 0;
            an.reload = rl;
            an.steps = std::max(0, w.mag - h.mag);
            an.loaded = h.mag > 0 || w.mag <= 0;
            an.gas = w.mag > 0 ? (float)h.mag / w.mag : 1;
            Vector3 gR, gL, mz;
            DrawRtWeapon(wid, frame, an, tint, &gR, &gL, &mz);
            // a shot: gas, needle and spear guns breathe out a burst of bubbles from the muzzle's vents (spec: "Firing underwater")
            if (d.recoil > 0.9f && (w.cls == "gas" || w.cls == "needle" || w.cls == "spear" || w.cls == "lmg")) FxBubbles(mz, 4, 0.05f, 0.02f);
            if (wid == "twingannets") {   // the pair: a mirrored Gannet in the left fist
                Matrix frame2 = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixTranslate(-g.x, -g.y, -g.z), MatrixScale(1.5f, 1.5f, -1.5f)), MatrixRotateY(-PI / 2)), MatrixTranslate(og.x + 0.27f, og.y, og.z)), gunM);
                Vector3 g2; DrawRtWeapon(wid, frame2, an, tint, &g2, nullptr, nullptr); gL = g2;
            }
            // the hands: the viewmodel's own gloves closed on this gun's grip markers (the slant of its grip by kind)
            {
                VmHold vh; vh.gun = frame;
                vh.gripR = g; vh.gripL = RtWeaponMarker(wid, "grip_l", g);
                static const std::map<std::string, float> SLANT = {
                    {"cormorant", 72.0f}, {"gannet", 72.0f}, {"twingannets", 72.0f}, {"boltharpoon", 72.0f}, {"needler1", 76.0f}, {"needler2", 76.0f},
                    {"stormlock", 76.0f}, {"longspeargun", 78.0f}, {"carbine", 58.0f}, {"flechette12", 58.0f}, {"trawlerman", 58.0f}, {"drumflechette", 58.0f},
                    {"knife", 0.0f}, {"boardingaxe", 0.0f}, {"teslagaff", 0.0f}, {"trident", 0.0f}, {"galvanicrod", 0.0f}, {"tidestaff", 0.0f}, {"abyssallure", 0.0f}};
                auto it = SLANT.find(wid);
                vh.angle = it != SLANT.end() ? it->second : 74;
                bool near = Vector3Distance(vh.gripL, vh.gripR) < 0.05f;
                vh.left = w.cls == "melee" || ms >= 0 || near ? 0 : wid == "twingannets" ? 3 : 1;   // (a sidearm in one hand, as in any shooter)
                if (vh.left == 3) vh.leftShift = Vector3Subtract(gL, gR);
                vh.magOut = magOut;
                if (drinkT >= 0 || reviving) {
                    vh.left = 0;   // (the left hand is busy)
                    Color liquid = drinkId == "juggernaut" ? Color{200, 60, 50, 255} : drinkId == "quick" ? Color{80, 180, 220, 255} : drinkId == "speed" ? Color{120, 220, 90, 255} : drinkId == "double" ? Color{230, 170, 60, 255} : Color{170, 120, 220, 255};
                    DrawViewmodelAction(M().VoiceOf(0), cam, reviving ? 1 : 0, std::clamp(drinkT / 1.4f, 0.0f, 1.0f), liquid, S.time, prof.suit, prof.helmet);
                }
                if (DrawViewmodelHands(M().VoiceOf(0), cam, vh, S.time, prof.suit, prof.helmet)) goto muzzle;
            }
            if (DiversReady() && DrawFirstPersonArms(M().VoiceOf(0), cam, gR, gL, ms < 0, S.time, prof.suit, prof.helmet)) goto muzzle;
            goto muzzle;
        }
    }
    DrawStatic(gGuns[GunModelFor(w.cls)], gunM, tint);
    // your own arms in your suit (the Visual Overhaul, phase 3): the right fist on the grip, the left on the fore-end
    // (a pistol's under the grip), or pulling the magazine and pushing the new one home
    if (DiversReady()) {
        // per gun model (BuildGuns): the grip, and where the other hand goes (the pistol's cupped under it, the
        // carbine's fore-end, the needler's clip, the speargun's rail, the gatling's crank, the launcher's tube)
        static const Vector3 GR[8] = {{0, -0.08f, -0.03f}, {0, -0.07f, -0.04f}, {0, -0.04f, -0.06f}, {0, -0.04f, -0.05f}, {0, -0.06f, -0.1f}, {0, -0.085f, -0.06f}, {0, -0.1f, 0.0f}, {0, 0, -0.05f}};
        static const Vector3 GL[8] = {{-0.02f, -0.11f, -0.02f}, {0, -0.045f, 0.1f}, {0, -0.025f, 0.15f}, {0, -0.012f, 0.14f}, {0, -0.01f, 0.2f}, {0.075f, -0.05f, -0.05f}, {0, -0.06f, 0.2f}, {0, 0, 0.12f}};
        int gm = GunModelFor(w.cls);
        Vector3 gR = Vector3Transform(GR[gm], gunM);
        Vector3 gL = magOut > 0 ? Vector3Transform({0, -0.1f - 0.1f * magOut, 0.08f}, gunM) : Vector3Transform(GL[gm], gunM);
        if (magOut > 0) DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.03f, 0.05f, 0.05f), MatrixTranslate(0, -0.06f - 0.1f * magOut, 0.08f)), gunM), w.cls == "needle" || w.cls == "lmg" ? Color{120, 176, 190, 255} : Color{70, 74, 78, 255});
        if (DrawFirstPersonArms(M().VoiceOf(0), cam, gR, gL, ms < 0, S.time, prof.suit, prof.helmet)) goto muzzle;
    }
    DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.045f, 0.05f, 0.08f), MatrixTranslate(0, -0.035f, -0.06f)), gunM), suit);
    // the other glove: on the fore-end, or pulling the magazine out and pushing the new one home
    if (magOut > 0) {
        Vector3 magAt{0, -0.06f - 0.1f * magOut, 0.08f};
        DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.03f, 0.05f, 0.05f), MatrixTranslate(magAt.x, magAt.y, magAt.z)), gunM), w.cls == "needle" || w.cls == "lmg" ? Color{120, 176, 190, 255} : Color{70, 74, 78, 255});
        DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.045f, 0.04f, 0.06f), MatrixTranslate(magAt.x, magAt.y - 0.04f, magAt.z)), gunM), suit);
    } else if (ms < 0 && !d.ads) DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.045f, 0.04f, 0.06f), MatrixTranslate(-0.01f, -0.045f, 0.16f)), gunM), suit);
muzzle:
    // the muzzle: a flash on a powder shot, a puff of bubbles from a gas gun, for the first moment of the recoil
    if (d.recoil > 0.8f && w.cls != "melee") {
        float z = w.cls == "launcher" ? 0.5f : w.cls == "scatter" ? 0.32f : w.cls == "powder" ? 0.47f : w.cls == "lmg" ? 0.38f : w.cls == "spear" ? 0.6f : 0.28f;
        bool powder = w.cls == "powder" || w.cls == "scatter" || w.cls == "launcher";
        DrawCubeGlow(MatrixMultiply(MatrixMultiply(MatrixScale(powder ? 0.07f : 0.04f, powder ? 0.07f : 0.04f, powder ? 0.1f : 0.05f), MatrixTranslate(0, 0, z)), gunM), powder ? Color{255, 220, 150, 255} : Color{200, 230, 240, 255}, powder ? 1.0f : 0.5f);
    }
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

// the interactables' own models (tools/artgen/stations_rt.py; the spec: "each get a distinct, well-lit, detailed model
// that never gets lost in the decor"): standing on the station's floor, facing into its room; a few lit from within
static bool DrawStationModel(const Match& m, const Station& s, bool dead) {
    if (getenv("DEPTH_OLDSTATIONS")) return false;
    static const char* ID[] = {"rack", "tonic", "locker", "forge", "power", "workbench", "", "", "", "", "", "", "", "", "cache"};
    int ti = (int)s.type;
    if (ti < 0 || ti >= (int)(sizeof(ID) / sizeof(ID[0])) || !ID[ti][0]) return false;
    // (the floor under it, and its front turned toward the middle of the room)
    float floorY = s.zone >= 0 && s.zone < (int)m.map->zones.size() ? m.map->zones[s.zone].y0 : s.pos.y - 1;
    Vector3 c = s.zone >= 0 && s.zone < (int)m.map->zones.size() ? m.map->zones[s.zone].Center() : Vector3Add(s.pos, {1, 0, 0});
    float dx = c.x - s.pos.x, dz = c.z - s.pos.z;
    float yaw = fabsf(dx) + fabsf(dz) > 0.01f ? atan2f(-dz, dx) : 0;
    Matrix frame = MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(s.pos.x, floorY, s.pos.z));
    Color tint = dead ? Color{120, 120, 120, 255} : WHITE;
    std::string path = std::string("redtide/stations/") + ID[ti] + ".glb";
    auto at = [&](Vector3 local) { return Vector3Transform(local, frame); };
    auto glowBox = [&](Vector3 local, Vector3 size, Color col, float k) { DrawCubeGlow(MatrixMultiply(MatrixMultiply(MatrixScale(size.x, size.y, size.z), MatrixTranslate(local.x, local.y, local.z)), frame), col, k); };
    switch (s.type) {
        case StationType::Tonic: {
            if (!DrawRtProp(path, frame, nullptr, tint)) return false;
            Color glass = s.tonic == "juggernaut" ? Color{200, 60, 50, 255} : s.tonic == "quick" ? Color{80, 180, 220, 255} : s.tonic == "speed" ? Color{120, 220, 90, 255} : s.tonic == "double" ? Color{230, 170, 60, 255} : Color{170, 120, 220, 255};
            if (dead) glass = {50, 50, 50, 255};
            glowBox({0.22f, 0.98f, 0}, {0.1f, 0.26f, 0.1f}, glass, dead ? 0.0f : 0.8f + 0.2f * sinf(S.time * 3 + s.pos.x));   // the bottle behind the port
            break;
        }
        case StationType::Locker: {
            if (!DrawRtProp(path, frame, nullptr, tint)) return false;
            if (m.LockerLiveAt(s)) {   // the lantern buoy on its chain, riding the swell, its lamp alight
                float bob = sinf(S.time * 1.4f + s.pos.z) * 0.08f;
                Matrix bf = MatrixMultiply(MatrixMultiply(MatrixRotateZ(0.08f * sinf(S.time * 0.9f)), MatrixTranslate(-0.2f, 1.7f + bob, 0)), frame);
                DrawRtProp("redtide/stations/buoy.glb", bf, nullptr, WHITE);
                DrawCubeGlow(MatrixMultiply(MatrixMultiply(MatrixScale(0.1f, 0.14f, 0.1f), MatrixTranslate(0, 0.65f, 0)), bf), {255, 210, 120, 255}, 1.0f);
            }
            break;
        }
        case StationType::Forge:
            if (!DrawRtProp(path, frame, nullptr, tint)) return false;
            glowBox({0.6f, 1.0f, 0}, {0.04f, 0.36f, 0.36f}, dead ? Color{30, 40, 40, 255} : Color{120, 230, 210, 255}, dead ? 0.0f : 0.9f + 0.1f * sinf(S.time * 2));   // sea-glass light
            break;
        case StationType::Power:
            if (!DrawRtProp(path, frame, [&](const std::string& g) { return g == "lever" ? (m.power ? 0.0f : 1.0f) : 0.0f; }, WHITE)) return false;
            glowBox({0.06f, 1.56f, 0}, {0.02f, 0.05f, 0.05f}, m.power ? Color{120, 230, 110, 255} : Color{230, 70, 50, 255}, 1.0f);   // the pilot lamp
            break;
        case StationType::Workbench:
            if (!DrawRtProp(path, frame, nullptr, tint)) return false;
            glowBox({-0.2f, 1.03f, -0.75f}, {0.06f, 0.1f, 0.06f}, {255, 200, 120, 255}, 0.9f);   // its lamp
            break;
        case StationType::Cache:
            if (!DrawRtProp(path, frame, [&](const std::string& g) { return g == "lid" && m.cacheOpen ? 1.0f : 0.0f; }, WHITE)) return false;
            break;
        case StationType::Rack: {
            // a weapon chalked on the wall: a weathered board, the gun itself hung across it on two pegs, a rough chalk
            // outline round it (the spec: "Racks (weapons chalked on walls)")
            const auto& WW = Weapons().weapons;
            if (s.weapon < 0 || s.weapon >= (int)WW.size()) return false;
            std::string wid = WW[s.weapon].id == "diversknife" ? "knife" : WW[s.weapon].id;
            if (!RtWeaponModel(wid)) return false;
            Matrix wall = MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(s.pos.x, s.pos.y, s.pos.z));
            DrawCubeM(MatrixMultiply(MatrixScale(0.08f, 0.9f, 1.4f), wall), Color{86, 64, 42, 255});
            for (int k = 0; k < 2; k++) DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.12f, 0.04f, 0.04f), MatrixTranslate(0.06f, 0.0f, k ? 0.3f : -0.3f)), wall), Color{60, 44, 30, 255});
            Color chalk{150, 148, 138, 255};
            for (int k = 0; k < 4; k++) {   // (the outline, drawn freehand: a little crooked)
                float w = k < 2 ? 1.2f : 0.03f, h = k < 2 ? 0.03f : 0.5f, y = k == 0 ? 0.26f : k == 1 ? -0.24f : 0.01f, z = k == 2 ? -0.6f : k == 3 ? 0.61f : 0.0f;
                DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(0.01f, h, w), MatrixRotateX(0.02f * (k - 1.5f))), MatrixTranslate(0.045f, y, z)), wall), chalk);
            }
            RtGunAnim an;
            float L = 1.0f;
            { float len = RtWeaponMarker(wid, "muzzle", {0.3f, 0, 0}).x - RtWeaponMarker(wid, "grip_r", {0, 0, 0}).x + 0.3f; L = std::clamp(1.0f / std::max(0.2f, len), 1.0f, 1.6f); }   // (its length from its own marks: a pistol drawn up, a long gun to the board)
            float cx = 0.5f * (RtWeaponMarker(wid, "muzzle", {0.3f, 0, 0}).x + RtWeaponMarker(wid, "grip_r", {0, 0, 0}).x - 0.12f);   // (centred on the board)
            DrawRtWeapon(wid, MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixTranslate(-cx, 0, 0), MatrixScale(L, L, L)), MatrixRotateY(yaw + PI / 2)), MatrixTranslate(s.pos.x + cosf(yaw) * 0.1f, s.pos.y, s.pos.z - sinf(yaw) * 0.1f)), an, WHITE);
            break;
        }
        default: return false;
    }
    (void)at;
    return true;
}

static void DrawStations() {
    const Match& m = M();
    for (const auto& s : m.level.stations) {
        Vector3 p = s.pos;
        bool dead = s.needsPower && !m.power;
        if (DrawStationModel(m, s, dead)) continue;
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
            case StationType::Quest:
                if (s.name.find("lantern") != std::string::npos) {   // a Drowned lantern, burning until the ink puts it out
                    bool out = m.lanternsOut.count((int)(&s - &m.level.stations[0])) > 0;
                    DrawWorldCube({p.x, p.y - 0.9f, p.z}, {0.3f, 0.5f, 0.3f}, {60, 56, 48, 255});
                    DrawWorldCube({p.x, p.y - 0.5f, p.z}, {0.26f, 0.3f, 0.26f}, out ? Color{30, 30, 36, 255} : Color{255, (unsigned char)(190 + 30 * sinf(S.time * 7)), 110, 255});
                } else if (s.name.find("log") != std::string::npos) DrawWorldCube({p.x, p.y - 0.55f, p.z}, {0.5f, 0.1f, 0.35f}, m.logRead ? Color{120, 90, 60, 255} : Color{90, 60, 40, 255});
                else if (m.map->extra["quest_altar"].IsObj()) {
                    DrawWorldCube({p.x, p.y - 0.8f, p.z}, {1.4f, 0.6f, 1.0f}, {150, 140, 120, 255});
                    for (int k = 0; k < m.nests; k++) DrawWorldCube({p.x - 2.5f + k * 1.2f, p.y - 1.05f, p.z + 2.0f}, {0.7f, 0.15f, 0.6f}, {200, 190, 150, 255});   // the nests in the sand
                    if (m.nesting) DrawWorldCube({p.x, p.y - 0.4f, p.z}, {0.45f, 0.3f, 0.45f}, {130, 80, 50, 255});                                            // the drum on the altar
                } else DrawWorldCube(p, {0.8f, 0.9f, 0.7f}, m.safeOpen ? Color{60, 60, 60, 255} : Color{70, 76, 70, 255});
                break;
            case StationType::Cleaning: DrawWorldCube({p.x, p.y - 0.6f, p.z}, {1.4f, 0.8f, 1.2f}, {110, 100, 88, 255}); break;
            case StationType::Trap:
                if (s.name.find("Beacon") != std::string::npos && s.name.find("control") == std::string::npos) {
                    // a lure beacon: a mast and a lamp (amber and pulsing when the Remnant has it on)
                    auto it = m.beaconOn.find((int)(&s - &m.level.stations[0]));
                    bool on = it != m.beaconOn.end() && it->second && m.darkT <= 0;
                    DrawWorldCube({p.x, p.y + 0.3f, p.z}, {0.2f, 2.6f, 0.2f}, {70, 70, 66, 255});
                    DrawWorldCube({p.x, p.y + 1.7f, p.z}, {0.5f, 0.4f, 0.5f}, on ? Color{255, (unsigned char)(150 + 80 * sinf(S.time * 6)), 60, 255} : Color{50, 54, 56, 255});
                }
                break;
            case StationType::Cache: DrawWorldCube({p.x, p.y - 0.6f, p.z}, {1.0f, 0.6f, 0.7f}, m.cacheOpen ? Color{60, 50, 40, 255} : Color{110, 84, 50, 255}); break;
            case StationType::QuestStep:
                if (m.map->extra["quests"].IsArr() && !m.map->extra["quests"].a.empty()) {
                    // Atlantis: the treasury crystal, the chapel's braziers, the plaza's marker, the god-pool's rim,
                    // the wall-fires and the lighthouse (lit once their step is behind the chain)
                    int c = m.QuestChainOf(s.step);
                    bool lit = c >= 0 && c < (int)m.questAt.size() && m.questAt[c] > s.step;
                    std::string k = c >= 0 ? m.map->extra["quests"].a[c]["steps"][std::to_string(s.step)]["kind"].Str0() : "";
                    if (k == "carry") { DrawWorldCube({p.x, p.y - 0.2f, p.z}, {0.5f, 0.6f, 0.5f}, {90, 86, 80, 255}); DrawWorldCube({p.x, p.y + 0.5f + sinf(S.time * 2) * 0.05f, p.z}, {0.3f, 0.7f, 0.3f}, {150, 230, 255, 255}); }
                    else if (k == "spark" || k == "key") {
                        float tall = k == "key" ? 6.0f : 1.0f;
                        DrawWorldCube({p.x, p.y - 1.2f + tall / 2, p.z}, {k == "key" ? 1.4f : 0.5f, tall, k == "key" ? 1.4f : 0.5f}, {120, 110, 96, 255});
                        DrawWorldCube({p.x, p.y - 1.2f + tall + 0.1f, p.z}, {0.8f, 0.2f, 0.8f}, {70, 60, 50, 255});
                        if (lit) DrawWorldCube({p.x, p.y - 1.0f + tall + 0.25f + sinf(S.time * 9) * 0.05f, p.z}, {0.45f, 0.6f + sinf(S.time * 13) * 0.1f, 0.45f}, {255, 170, 60, 255});
                    } else if (k == "hold") DrawWorldCube({p.x, p.y - 1.15f, p.z}, {2.4f, 0.03f, 2.4f}, lit ? Color{90, 90, 80, 255} : Color{150, 120, 60, 255});
                    else if (k == "ichor") DrawWorldCube({p.x, p.y - 1.1f, p.z}, {1.6f, 0.1f, 0.4f}, lit ? Color{40, 120, 140, 255} : Color{120, 110, 96, 255});
                    break;
                }
                // the Supper Call's props: the log on a bunk shelf, the whistle among the tins, the cord on the boiler
                if (s.step == 1) DrawWorldCube({p.x, p.y - 0.5f, p.z}, {0.35f, 0.08f, 0.25f}, {92, 60, 40, 255});
                else if (s.step == 2 && m.questStep < 2) DrawWorldCube({p.x, p.y - 0.4f, p.z}, {0.1f, 0.25f, 0.1f}, {150, 150, 90, 255});
                else if (s.step == 3) { DrawWorldCube({p.x, p.y + 0.8f, p.z}, {0.03f, 1.6f, 0.03f}, {120, 100, 70, 255}); DrawWorldCube({p.x, p.y + 1.7f, p.z}, {0.14f, 0.3f, 0.14f}, m.questStep >= 2 ? Color{190, 160, 80, 255} : Color{70, 70, 66, 255}); }
                break;
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
        // the door in the map's own style (stations_rt.py: built for a 2 m x 2.4 m doorway, scaled to this one)
        {
            const std::string& mk = M().mapKey;
            const char* door = mk == "cave" ? "door_rubble" : mk == "reef" ? "door_net" : mk == "atlantis" ? "door_grille" : "door_bulkhead";
            float wide = k == 0 ? size.z : size.x;
            Matrix fr = MatrixMultiply(MatrixMultiply(MatrixScale(1, size.y / 2.4f, wide / 2.0f), MatrixRotateY(k == 0 ? 0.0f : PI / 2)), MatrixTranslate(d.pos.x, v.lo.y, d.pos.z));
            if (!getenv("DEPTH_OLDSTATIONS") && DrawRtProp(std::string("redtide/stations/") + door + ".glb", fr, nullptr, WHITE)) continue;
        }
        (&size.x)[k] = 0.35f;
        DrawWorldCube(d.pos, Vector3Scale(size, 0.98f), {92, 70, 50, 255});
        DrawWorldCube(Vector3Add(d.pos, {0, 0.4f, 0}), {k == 0 ? 0.5f : size.x * 0.6f, 0.3f, k == 2 ? 0.5f : size.z * 0.6f}, {120, 96, 64, 255});
    }
}

// The Visual Overhaul's water (phase 2): absorption by channel, the in-scatter darkening with depth, caustics, light
// shafts (through the room's portholes; from the surface out in the open), bloom, the helmet port's lens, the ink line
// tinted by the water and fading into it, the stipple off; the PBR figures lit by the light from above.
static float gSurfY = 34;   // the surface's height (for the particles)
 static void WaterLook(SceneLight& L, int z, const Camera3D& cam) {
    gSurfY = L.surfaceY;
    const Settings& st = GameSettings();
    const MapData& map = *M().map;
    bool air = z >= 0 && map.zones[z].air;
    bool open = z >= 0 && (map.zones[z].deck == "Outside" || map.extra["open_zones"].IsArr() && [&] { for (size_t i = 0; i < map.extra["open_zones"].Size(); i++) if (map.extra["open_zones"][i].Str0() == map.zones[z].name) return true; return false; }());
    L.water = air ? 0.0f : 1.0f;
    { const std::string& k = M().mapKey; L.surf = k == "cave" ? 2.0f : k == "reef" ? 3.0f : k == "atlantis" ? 4.0f : k == "void" ? 5.0f : 1.0f; }   // (the map kit's surfaces)
    const Json& pal = map.extra["palette"];
    if (pal.IsObj() && pal["absorb"].IsArr()) L.absorb = {pal["absorb"][0].F(0.075f), pal["absorb"][1].F(0.034f), pal["absorb"][2].F(0.026f)};
    L.depthDark = pal.IsObj() ? pal["depth_dark"].F(0.012f) : 0.012f;
    L.causticK = open ? 0.9f : 0.35f;
    L.bloom = 0.35f;
    L.lens = st.rtLens ? 1.0f : 0.0f;
    L.inkFade = 1;
    L.outline = st.rtInk == 0 ? 0.0f : st.rtInk == 1 ? 0.6f : 1.0f;
    L.outlineTint = {(unsigned char)(L.fog.r * 0.35f), (unsigned char)(L.fog.g * 0.35f), (unsigned char)(L.fog.b * 0.35f), 255};
    L.stipple = st.rtStipple ? 1.0f : 0.0f;
    L.grain = 0.5f;
    L.aoK = 0.55f; L.aoRadius = 0.5f;
    L.filmic = 0.6f; L.exposure = 1.05f; L.saturation = 1.0f;
    // the light from above for the PBR figures and caustics: the fill colour, brighter near the surface
    float depthBelow = std::max(0.0f, L.surfaceY - cam.position.y);
    float sun = std::clamp(expf(-depthBelow * 0.03f), 0.15f, 1.0f);
    L.moonDir = Vector3Normalize({0.2f, -1.0f, 0.15f});
    L.moon = {(unsigned char)std::min(255, L.fill.r * 3), (unsigned char)std::min(255, L.fill.g * 3), (unsigned char)std::min(255, L.fill.b * 3), 255};
    L.moonK = (open ? 0.7f : 0.3f) * sun;
    L.ambK = 0.55f; L.skyAmb = {(unsigned char)std::min(255, L.fill.r * 2), (unsigned char)std::min(255, L.fill.g * 2), (unsigned char)std::min(255, L.fill.b * 2), 255}; L.seaAmb = L.fog;
    if (air) return;
    // shafts: the nearest portholes of this room throw a beam in and down; in open water, beams from the surface on a
    // fixed grid round the eye (so they stand still as you swim through them)
    Color sc{(unsigned char)std::min(255, L.fill.r * 4 + 40), (unsigned char)std::min(255, L.fill.g * 4 + 40), (unsigned char)std::min(255, L.fill.b * 4 + 30), 255};
    std::vector<std::pair<float, int>> near;
    for (int i = 0; i < (int)map.windows.size(); i++) if (map.windows[i].zone == z) {
        Vector3 c = Vector3Scale(Vector3Add(map.windows[i].lo, map.windows[i].hi), 0.5f);
        near.push_back({Vector3Distance(c, cam.position), i});
    }
    std::sort(near.begin(), near.end());
    for (int k = 0; k < (int)near.size() && k < 4; k++) {
        const Window& w = map.windows[near[k].second];
        Vector3 c = Vector3Scale(Vector3Add(w.lo, w.hi), 0.5f);
        Vector3 in{0, 0, 0};
        if (w.axis == 0) in.x = w.outHigh ? -1.0f : 1.0f; else in.z = w.outHigh ? -1.0f : 1.0f;
        Vector3 dir = Vector3Normalize(Vector3Add(in, {0, -0.75f, 0}));
        L.AddShaft(c, dir, 0.35f, 8, sc, 0.35f * sun + 0.12f);
    }
    if (open) {
        float cell = 9;
        int cx = (int)floorf(cam.position.x / cell), cz = (int)floorf(cam.position.z / cell);
        for (int dz = -1; dz <= 1 && L.nShafts < SceneLight::MAX_SHAFTS; dz++) for (int dx = -1; dx <= 1 && L.nShafts < SceneLight::MAX_SHAFTS; dx++) {
            uint32_t h = (uint32_t)(cx + dx) * 73856093u ^ (uint32_t)(cz + dz) * 19349663u;
            if ((h >> 7) % 3 == 0) continue;   // (some cells have none)
            float ox = ((h >> 11) % 100) / 100.0f, oz = ((h >> 17) % 100) / 100.0f;
            Vector3 top{(cx + dx + ox) * cell, L.surfaceY, (cz + dz + oz) * cell};
            L.AddShaft(top, Vector3Normalize({0.2f, -1.0f, 0.15f}), 0.6f + 0.5f * ox, std::min(40.0f, L.surfaceY - (cam.position.y - 12)), sc, 0.16f * sun);
        }
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
    if (M().darkT > 0 && M().bossAgent >= 0 && Vector3Distance(M().eco.agents[M().bossAgent].pos, d.pos) < 30) L.lampRange = 2.5f;   // the Leviathan's Dark
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
    const Json& pal = M().map->extra["palette"];
    if (pal.IsObj() && S.mode == 1) {
        if (pal["fog"].IsArr()) L.fog = {(unsigned char)pal["fog"][0].I(), (unsigned char)pal["fog"][1].I(), (unsigned char)pal["fog"][2].I(), 255};
        L.fogDensity = pal["fog_density"].F(L.fogDensity);
        if (pal["fill"].IsArr()) L.fill = {(unsigned char)pal["fill"][0].I(), (unsigned char)pal["fill"][1].I(), (unsigned char)pal["fill"][2].I(), 255};
        L.surfaceY = pal["surface_y"].F(L.surfaceY);
        if (z >= 0 && M().map->zones[z].air) { L.fogDensity *= 0.5f; L.fog = {20, 22, 22, 255}; }   // dry air: clearer, and black
    }
    WaterLook(L, z, cam);
    return cam;
}

static void DrawLineup() {
    const MapData& ship = Map("ship");
    Camera3D cam{};
    cam.position = {0, 0, -9}; cam.target = {0, 0, 0}; cam.up = {0, 1, 0}; cam.fovy = 50; cam.projection = CAMERA_PERSPECTIVE;
    SceneLight L;
    L.lampPos = {0, 6, -9}; L.lampDir = Vector3Normalize({0, -0.4f, 1}); L.lampRange = 40; L.lampCone = 0.2f; L.fogDensity = 0.004f; L.fog = {26, 52, 58, 255};
    L.time = S.time;
    L.moonK = 0.5f; L.ambK = 0.7f; L.skyAmb = {90, 140, 150, 255}; L.seaAmb = {20, 40, 46, 255};
    CreatureBudget(40);
    RenderBegin(cam, L);
    int per = 20, first = S.lineup * per, cols = 5;
    for (int k = 0; k < per && first + k < (int)ship.species.size(); k++) {
        const Species& sp = ship.species[first + k];
        if (sp.isEnemy || sp.isDiver) continue;
        const CreatureModel& cm = Creature("ship", sp.name);
        int cx = k % cols, cy = k / cols;
        Vector3 at{(cx - (cols - 1) * 0.5f) * 2.3f, (1.5f - cy) * 2.0f, 0};
        float sc = 0.95f / std::max(0.05f, cm.extent);
        if (!DrawCreaturePbr(cm, at, 1.5708f, 0, sc, S.time * cm.freq, 0.8f, WHITE)) DrawCreature(cm, at, 1.5708f, 0, sc, S.time * cm.freq, 0.8f);
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

// a teammate (or a bot diver) on the shared figure in their suit (the Visual Overhaul, phase 3): treading water when
// still, lying into a flutter-kick when swimming, sinking on their back when downed
static bool DrawTeammate(const Match& m, const Agent& a) {
    if (!DiversReady()) return false;
    const DiverState* d = nullptr;
    int di = -1;
    for (int k = 0; k < (int)m.divers.size(); k++) if (m.divers[k].slot == a.diver) { d = &m.divers[k]; di = k; }
    if (!d) return false;
    float spd = Vector3Length(d->vel);
    fig::Pose P;
    P.breathe = S.time * (1.6f + std::min(1.0f, spd * 0.3f)) + di;
    P.swim = std::clamp((spd - 0.4f) / 1.6f, 0.0f, 1.0f);
    P.kickPh = S.time * (3.5f + 2.0f * P.swim) + di * 1.7f;
    P.tread = (1 - P.swim) * 0.7f;
    P.reach = 0.55f; P.elbow = 0.35f; P.grip = 0.85f;   // (the gun held before them: phase 4 puts the real one in the fists)
    P.blink = fmodf(S.time * 0.25f + di * 0.37f, 1.0f) < 0.03f ? 1.0f : 0.0f;
    float tilt = P.swim * 1.15f + std::clamp(d->pitch, -0.6f, 0.6f) * P.swim;
    if (d->downed) { tilt = -1.2f; P.tread = 0.3f; P.swim = 0; P.reach = 0.2f; }
    // the hips at the agent's position; the figure faces its yaw (its +x along the look), tipped about the hips
    Matrix tip = MatrixMultiply(MatrixMultiply(MatrixTranslate(0, -1.0f, 0), MatrixRotateZ(-tilt)), MatrixTranslate(0, 1.0f, 0));
    Matrix frame = MatrixMultiply(tip, fig::Frame(Vector3Subtract(a.pos, {0, 1.0f, 0}), d->yaw - PI / 2));
    std::vector<Matrix> tsk = DrawDiverFigure(m.VoiceOf(di), frame, P, S.time, d->dead ? Color{170, 200, 220, 160} : WHITE);
    // their gun in the right fist, the same baked model as in first person (spec: "the third-person model of every gun")
    if (!d->dead && !tsk.empty() && !d->weapons.empty()) {
        const WeaponDef& w = m.W(m.Cur(*d));
        std::string wid = w.id;
        if (RtWeaponModel(wid)) {
            Vector3 fist = fig::FistWorld(*DiverModel(m.VoiceOf(di)), tsk, frame);
            Vector3 g = RtWeaponMarker(wid, "grip_r", {0, 0, 0});
            Matrix rotOnly = frame; rotOnly.m12 = rotOnly.m13 = rotOnly.m14 = 0;
            Matrix wm = MatrixMultiply(MatrixMultiply(MatrixTranslate(-g.x, -g.y, -g.z), MatrixMultiply(MatrixRotateZ(-0.2f), rotOnly)), MatrixTranslate(fist.x, fist.y, fist.z));
            RtGunAnim an; an.fire = std::clamp((d->recoil - 0.6f) * 2.5f, 0.0f, 1.0f);
            DrawRtWeapon(wid, wm, an, WHITE);
        }
    }
    // a breath out through the helmet's exhaust every few seconds, quicker when swimming hard
    if (!d->dead) { float rate = 0.33f + 0.25f * P.swim, ph = fmodf(S.time * rate + di * 0.31f, 1.0f); if (ph < rate / 60.0f) FxBubbles(Vector3Transform({-0.12f, 1.88f, 0}, frame), 7, 0.08f); }
    return true;
}

// A faction diver on the shared figure (the Visual Overhaul, phase 6): the suit and the weapon make each unit type's
// silhouette (spec: "every unit type in a squad must be identifiable by silhouette at 30 m"). The Wreckers in rusted,
// patched gear (the Cutter with a knife, the Speargunner's long speargun, the Netman's net gun, the Foreman in a hard
// suit with the Harpoon Cannon); the Drowned pale as ghosts in their hoods; the Remnant's pressure suits, brass
// sentinels and hooded cultists. Others keep their CreatureBuilder models.
static bool DrawFactionFigure(const Match& m, const Agent& a, float yaw) {
    if (!DiversReady() || a.unit < 0 || a.unit >= (int)m.map->faction.units.size()) return false;
    const FactionUnit& u = m.map->faction.units[a.unit];
    std::string fn = m.map->faction.name, un = u.unit;
    auto has = [](const std::string& s, const char* k) { return s.find(k) != std::string::npos; };
    int voice = -1; std::string weapon, suit, helmet; Color tint = WHITE;
    std::vector<Recolor> extra;
    if (has(fn, "Wreck")) {
        voice = has(un, "Foreman") ? 3 : has(un, "Spear") ? 1 : 2;
        weapon = has(un, "Foreman") ? "harpooncannon" : has(un, "Spear") ? "longspeargun" : has(un, "Net") ? "netgun" : "knife";
        suit = "redtide"; helmet = "h_redtide";
    } else if (has(fn, "Drowned")) {
        voice = 1; tint = {176, 214, 224, 255};
        weapon = has(un, "Deckhand") ? "boardingaxe" : has(un, "Lantern") ? "abyssallure" : has(un, "Bosun") ? "boardingaxe" : "trident";
        suit = "bone"; helmet = "h_bone";
    } else if (has(fn, "Remnant")) {
        if (has(un, "Chief")) return false;
        voice = has(un, "Sentinel") ? 3 : has(un, "Cultist") ? 1 : 3;
        weapon = has(un, "Sentinel") ? "gatling" : has(un, "Cultist") ? "tidestaff" : "needler1";
        if (has(un, "Sentinel")) helmet = "h_atlantean"; else if (has(un, "Cultist")) { suit = "atlantean"; helmet = "h_verdigris"; } else { suit = "pearl"; helmet = "h_pearl"; }
    } else return false;
    float spd = Vector3Length(a.vel);
    fig::Pose P;
    P.breathe = S.time * 1.8f + a.rng % 7;
    P.swim = std::clamp((spd - 0.4f) / 1.6f, 0.0f, 1.0f);
    P.kickPh = S.time * 4.5f + (a.rng % 13);
    P.tread = (1 - P.swim) * 0.15f;   // (weapon-ready: the arms in, not out treading)
    P.reach = 0.75f; P.elbow = 0.45f; P.grip = 0.9f;
    Matrix tip = MatrixMultiply(MatrixMultiply(MatrixTranslate(0, -1.0f, 0), MatrixRotateZ(-P.swim * 1.1f)), MatrixTranslate(0, 1.0f, 0));
    Matrix frame = MatrixMultiply(tip, fig::Frame(Vector3Subtract(a.pos, {0, 1.0f, 0}), yaw - PI / 2));
    std::vector<Matrix> skin = DrawDiverFigure(voice, frame, P, S.time, tint, suit, helmet);
    if (skin.empty() || weapon.empty()) return true;
    // the weapon in the right fist, along the figure's forward
    const Model* dm = DiverModel(voice);
    Vector3 fist = fig::FistWorld(*dm, skin, frame);
    Vector3 g = RtWeaponMarker(weapon, "grip_r", {0, 0, 0});
    Matrix rotOnly = frame; rotOnly.m12 = rotOnly.m13 = rotOnly.m14 = 0;
    Matrix w = MatrixMultiply(MatrixMultiply(MatrixTranslate(-g.x, -g.y, -g.z), MatrixMultiply(MatrixRotateZ(-0.25f), rotOnly)), MatrixTranslate(fist.x, fist.y, fist.z));
    RtGunAnim an; an.fire = 0;
    DrawRtWeapon(weapon, w, an, tint);
    return true;
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
        for (const auto& dr : S.dress) if (Vector3Distance({dr.m.m12, dr.m.m13, dr.m.m14}, eye) < 50) if (const Model* dm = LoadAsset(dr.path)) DrawPbr(*dm, dr.m, dr.tint);
        for (const auto& f : m.drops) {
            float pulse = 0.8f + 0.2f * sinf(S.time * 5);
            if (f.weapon >= 0) DrawWorldCube(f.pos, {0.8f, 0.15f, 0.25f}, {160, 150, 130, 255});
            else if (f.weapon == -2) {   // the Shaman's drum: a hide drum on the sand
                DrawWorldCube(Vector3Add(f.pos, {0, 0.2f, 0}), {0.55f, 0.4f, 0.55f}, {120, 78, 44, 255});
                DrawWorldCube(Vector3Add(f.pos, {0, 0.42f, 0}), {0.6f * pulse, 0.05f, 0.6f * pulse}, {226, 204, 160, 255});
            }
            else DrawWorldCube(Vector3Add(f.pos, {0, sinf(S.time * 2) * 0.15f, 0}), {0.25f * pulse, 0.45f * pulse, 0.25f * pulse}, {120, 240, 200, 255});
        }
        // salvage parts: small crates with a brass band, bobbing, the build's colour on the lid
        static const Color buildCol[6] = {{0, 0, 0, 255}, {150, 120, 70, 255}, {90, 150, 170, 255}, {170, 160, 120, 255}, {220, 120, 60, 255}, {140, 200, 220, 255}};
        for (const auto& sp : m.salvage) if (!sp.taken) {
            Vector3 c = Vector3Add(sp.pos, {0, sinf(S.time * 2 + sp.part) * 0.08f, 0});
            DrawWorldCube(c, {0.5f, 0.35f, 0.4f}, {110, 86, 60, 255});
            DrawWorldCube(Vector3Add(c, {0, 0.19f, 0}), {0.52f, 0.05f, 0.42f}, buildCol[std::clamp(sp.build, 0, 5)]);
            DrawWorldCube(c, {0.53f, 0.08f, 0.43f}, {200, 160, 80, 255});
        }
        // what's been set down
        for (const auto& dp : m.deployed) {
            Vector3 c = dp.pos;
            switch (dp.type) {
                case BuildType::Turbine: {
                    DrawWorldCube(Vector3Add(c, {0, -0.2f, 0}), {0.6f, 0.5f, 0.6f}, {90, 96, 100, 255});
                    float a = dp.stopped ? 0.3f : S.time * 9;
                    for (int k = 0; k < 3; k++) { float b = a + k * 2.094f; DrawWorldCube(Vector3Add(c, {cosf(b) * 0.35f, 0.25f, sinf(b) * 0.35f}), {0.6f, 0.04f, 0.14f}, dp.stopped ? Color{80, 80, 80, 255} : Color{200, 200, 190, 255}); }
                    if (!dp.stopped) DrawWorldCube(Vector3Add(c, {0, 0.45f, 0}), {0.12f, 0.12f, 0.12f}, {150, 230, 255, 255});
                    break;
                }
                case BuildType::NetTripwire: {
                    // two stakes across the diver's way and the net strung between them (a line of knots)
                    Vector3 side{dp.dir.z, 0, -dp.dir.x};
                    for (int k = -1; k <= 1; k += 2) DrawWorldCube(Vector3Add(c, Vector3Add(Vector3Scale(side, k * 1.4f), {0, 0.4f, 0})), {0.08f, 0.9f, 0.08f}, {140, 110, 70, 255});
                    for (int k = 0; k <= 14; k++) for (int row = 0; row < 3; row++) {
                        Vector3 q = Vector3Add(c, Vector3Add(Vector3Scale(side, -1.4f + k * 0.2f), {0, 0.15f + row * 0.3f, 0}));
                        DrawWorldCube(q, {0.05f, 0.05f, 0.05f}, dp.held >= 0 ? Color{200, 80, 60, 255} : Color{200, 190, 150, 255});
                    }
                    break;
                }
                case BuildType::DecoyBuoy: {
                    float bob = sinf(S.time * 1.5f) * 0.15f;
                    DrawWorldCube(Vector3Add(c, {0, bob, 0}), {0.6f, 0.7f, 0.6f}, {220, 110, 50, 255});
                    float pulse = 0.5f + 0.5f * sinf(S.time * 6);
                    DrawWorldCube(Vector3Add(c, {0, 0.55f + bob, 0}), {0.22f, 0.22f, 0.22f}, {255, (unsigned char)(200 + 55 * pulse), 120, 255});
                    break;
                }
                case BuildType::BubbleWall: {
                    Vector3 side{-dp.dir.z, 0, dp.dir.x};
                    for (int k = 0; k < 40; k++) {
                        float u = (k % 10) / 9.0f * 8 - 4, h = fmodf(S.time * 1.6f + k * 0.37f, 1.0f) * 6;
                        DrawWorldCube(Vector3Add(Vector3Add(c, Vector3Scale(side, u)), {0, h - 2.5f, 0}), {0.1f, 0.1f, 0.1f}, {210, 235, 245, 255});
                    }
                    break;
                }
                default: break;
            }
        }
        for (const auto& fl : m.flareLights) {   // a flare burning where it landed
            float fl2 = 0.8f + 0.2f * sinf(S.time * 23 + fl.pos.x);
            DrawWorldCube(fl.pos, {0.15f, 0.15f, 0.15f}, {255, 240, 200, 255});
            DrawWorldCube(fl.pos, {0.5f * fl2, 0.5f * fl2, 0.5f * fl2}, Fade(Color{255, 120, 60, 255}, 0.5f));
        }
        for (const auto& k : m.floorKeys) {   // a key a fallen diver dropped: brass, turning slowly, easy to spot
            Vector3 c = Vector3Add(k.pos, {0, 0.3f + sinf(S.time * 2) * 0.1f, 0});
            DrawWorldCube(c, {0.12f, 0.5f, 0.12f}, {220, 180, 70, 255});
            DrawWorldCube(Vector3Add(c, {0, 0.3f, 0}), {0.3f, 0.2f, 0.12f}, {230, 190, 80, 255});
        }
        for (const auto& ic : m.ichor) {   // Atlantis: a dead Lost One's black ichor, spreading on the stones
            float f = std::clamp(ic.t / 60.0f, 0.0f, 1.0f), rad = 0.6f + (1 - f) * 1.4f;
            int zi = m.eco.ZoneAt(ic.pos);
            float y = zi >= 0 ? m.map->zones[zi].y0 + 0.03f : ic.pos.y;
            DrawWorldCube({ic.pos.x, y, ic.pos.z}, {rad, 0.02f, rad}, {(unsigned char)(10 + 20 * f), (unsigned char)(8 + 10 * f), (unsigned char)(24 + 30 * f), 255});
        }
        for (const auto& g : m.gas) for (int k = 0; k < 8; k++) {   // the Researchers' gas: a green haze
            float a = k * 0.785f + S.time * 0.4f;
            DrawWorldCube(Vector3Add(g.pos, {cosf(a) * g.r * 0.5f, sinf(a * 1.3f) * 0.6f, sinf(a) * g.r * 0.5f}), {g.r * 0.35f, g.r * 0.25f, g.r * 0.35f}, {110, 150, 70, 255});
        }
        if (m.tentacleT > 0) for (int k = 0; k < 14; k++) {   // a colossal squid's arm over the overlook
            float f = k / 13.0f, sw = sinf(S.time * 1.6f + f * 3) * 1.5f * f;
            DrawWorldCube(Vector3Add(m.tentaclePos, {-3 + f * 2 + sw, -4 + f * 9, sinf(f * 4 + S.time) * 1.2f}), {0.6f - f * 0.35f, 0.6f, 0.6f - f * 0.35f}, {150, 60, 70, 255});
        }
        if (m.map->extra["worm"].IsObj()) {
            // the Sand Worm on the horizon: a vast back rising and sinking through the sand, far past the stakes
            float ang = S.time * 0.02f + 0.6f, rad = 520;
            int rim = m.map->ZoneIndex("The Rim");
            if (rim >= 0) for (int k = 0; k < 22; k++) {
                float a = ang - k * 0.012f, y = sinf(S.time * 0.3f - k * 0.35f) * 7 - 2;
                if (y < -4 || !m.map->zones[rim].Contains({cosf(a) * rad, m.map->zones[rim].y0 + 1, sinf(a) * rad})) continue;
                DrawWorldCube({cosf(a) * rad, m.map->zones[rim].y0 + y, sinf(a) * rad}, {4.5f, 4.5f, 4.5f}, {70, 58, 48, 255});
            }
        }
        if (m.wyrmState == 1 && m.wyrmGrate >= 0) {   // the grate rattles: silt and bubbles burst up through it
            Vector3 g = m.map->links[m.wyrmGrate].b;
            for (int k = 0; k < 10; k++) { float t = fmodf(S.time * 2.3f + k * 0.13f, 1.0f); DrawWorldCube({g.x + sinf(k * 2.1f) * 0.7f, g.y + t * 3.0f, g.z + cosf(k * 1.7f) * 0.7f}, {0.12f, 0.12f, 0.12f}, {200, 210, 200, 255}); }
        }
        for (const auto& p : m.polyps) {   // the Anemone Gun's rooted polyps: a crown of stinging tentacles
            for (int k = 0; k < 6; k++) {
                float an = k * 1.047f + S.time * 0.6f, sw = sinf(S.time * 3 + k) * 0.08f;
                DrawWorldCube(Vector3Add(p.pos, {cosf(an) * 0.18f + sw, 0.22f, sinf(an) * 0.18f}), {0.06f, 0.4f, 0.06f}, p.forged ? Color{250, 150, 90, 255} : Color{236, 120, 170, 255});
            }
            DrawWorldCube(p.pos, {0.3f, 0.16f, 0.3f}, {180, 90, 120, 255});
        }
        for (const auto& c : m.crates) {
            // the set pieces (stations_rt.py): the stalactite trembling at the roof, dropping, and the heap it leaves; the
            // cargo crate in its net swinging on the crane's chain, then dropped, and lying canted where it struck
            if (!getenv("DEPTH_OLDSTATIONS")) {
                int zc = m.eco.ZoneAt(c.pos); float floorY = zc >= 0 ? m.map->zones[zc].y0 : c.pos.y - 1.2f;
                float spin = (float)((int)(c.pos.x * 7 + c.pos.z * 13) % 628) * 0.01f;
                if (c.kind == 1) {
                    if (!c.fallen) {
                        float y = std::min(c.top, c.pos.y + std::max(0.0f, c.t) * 14);
                        float tremble = sinf(S.time * 40) * 0.03f;
                        if (DrawRtProp("redtide/stations/stalactite.glb", MatrixMultiply(MatrixRotateY(spin), MatrixTranslate(c.pos.x + tremble, y + 1.6f, c.pos.z)), nullptr, WHITE)) continue;
                    } else if (DrawRtProp("redtide/stations/rubble.glb", MatrixMultiply(MatrixRotateY(spin), MatrixTranslate(c.pos.x, floorY, c.pos.z)), nullptr, WHITE)) continue;
                } else {
                    Vector3 ctr = c.fallen ? Vector3{c.pos.x, floorY + 0.6f, c.pos.z} : Vector3{c.pos.x, c.pos.y + std::max(0.0f, c.t) * 3 + 0.4f, c.pos.z};
                    float sway = c.fallen ? 0.0f : sinf(S.time * 1.3f + spin) * 0.06f;
                    Matrix fr = MatrixMultiply(MatrixMultiply(MatrixRotateY(spin), MatrixRotateZ(c.fallen ? 0.14f : sway)), MatrixTranslate(ctr.x, ctr.y, ctr.z));
                    if (DrawRtProp("redtide/stations/trap_crate.glb", fr, nullptr, WHITE)) {
                        if (!c.fallen && c.top > ctr.y + 1.3f) {   // the chain up to the crane (cargo hung from one)
                            float top = c.top;
                            DrawCubeM(MatrixMultiply(MatrixScale(0.05f, top - ctr.y - 1.2f, 0.05f), MatrixTranslate(ctr.x, (top + ctr.y + 1.2f) * 0.5f, ctr.z)), {70, 66, 60, 255});
                        }
                        continue;
                    }
                }
            }
            if (c.kind == 1) {
                // a stalactite: hangs trembling at the ceiling, then drops and shatters
                float y = c.fallen ? c.pos.y - 0.2f : std::min(c.top, c.pos.y + std::max(0.0f, c.t) * 14);
                if (!c.fallen || c.t > -1.0f) DrawWorldCube({c.pos.x + (c.fallen ? 0 : sinf(S.time * 40) * 0.03f), y + 0.8f, c.pos.z}, c.fallen ? Vector3{1.2f, 0.4f, 1.0f} : Vector3{0.5f, 1.6f, 0.5f}, {150, 146, 132, 255});
            } else DrawWorldCube(c.fallen ? Vector3{c.pos.x, c.pos.y - 0.6f, c.pos.z} : Vector3{c.pos.x, c.pos.y + std::max(0.0f, c.t) * 3 + 0.4f, c.pos.z}, {1.2f, 1.2f, 1.2f}, {120, 92, 60, 255});
        }
    }
    CreatureBudget(60);   // (the nearest fish on the rigged models: the Visual Overhaul's creature kit)
    for (int i = 0; i < (int)m.eco.agents.size(); i++) {
        const Agent& a = m.eco.agents[i];
        if (!a.alive || a.diver == 0) continue;               // (diver 0 is you)
        if (Vector3Distance(a.pos, eye) > 55) continue;
        if (a.diver > 0 && DrawTeammate(m, a)) continue;
        const Species& sp = m.map->species[a.sp];
        const CreatureModel& cm = Creature(m.artKey, m.ArtName(a.sp));
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
        // states on the body (the spec's creature states): a badly wounded animal leaves a thread of blood in the water
        // behind it; a camouflaged one lying still goes the colour of the water round it until it moves
        if (a.wound > 0.45f && Vector3Distance(a.pos, eye) < 25 && fmodf(S.time * (1.5f + a.wound * 3) + (a.rng % 97) * 0.01f, 1.0f) < GetFrameTime() * (1.5f + a.wound * 3))
            Burst(Vector3Subtract(a.pos, Vector3Scale(Vector3Normalize(v), cm.length * (a.sp < (int)m.bodyScale.size() ? m.bodyScale[a.sp] : 1.0f) * 0.4f)), 1, {120, 14, 14, 255}, 0.08f, 1.6f, 0.05f + a.wound * 0.05f);
        if (sp.Has("camouflage") && (a.st == State::Rest || spd < 0.05f)) {
            Color w = L.fog;
            tint = {(unsigned char)((tint.r * 2 + w.r) / 3), (unsigned char)((tint.g * 2 + w.g) / 3), (unsigned char)((tint.b * 2 + w.b) / 3), 255};
        }
        if (sp.isEnemy && a.unit >= 0 && Vector3Distance(a.pos, eye) < 45 && DrawFactionFigure(m, a, yaw)) continue;   // (the factions on the figure)
        float bsc = a.sp < (int)m.bodyScale.size() ? m.bodyScale[a.sp] : 1.0f;
        if (m.IsBoss(i) && DrawBossPbr(m.bossKind, cm, a.pos, yaw, pitch, bsc, phase, inten, WHITE, m.bossGillsT > 0 ? 1.0f : 0.0f)) continue;   // (the boss's own model)
        if (Vector3Distance(a.pos, eye) < (cm.length * bsc < 0.35f ? 10.0f : 28.0f) && !m.IsBoss(i) && DrawCreaturePbr(cm, a.pos, yaw, pitch, bsc, phase, inten, tint)) continue;   // (the rigged fish, near)
        DrawCreature(cm, a.pos, yaw, pitch, bsc, phase, inten, tint);
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
        // blood in the water: soft dark-red clouds (never orange or pink), near black in the deep (the Visual Overhaul)
        float deep = std::clamp((gSurfY - c.y) / 60.0f, 0.0f, 1.0f);
        DrawBillboard(cam, SoftDot(), c, sf.cell * 1.6f, Fade(Color{(unsigned char)(96 - 60 * deep), (unsigned char)(6 - 3 * deep), (unsigned char)(10 - 5 * deep), 255}, a * 0.9f));
    }
    for (const auto& p : S.fx) DrawCube(p.pos, p.size, p.size, p.size, Fade(p.col, p.life / p.max));
    // the Void's lights: the Leviathan's lure (and its two decoys in phase 3), the Abyssal Lure's lanterns
    if (m.bossKind == 4 && m.bossActive && m.bossAgent >= 0 && m.lureHP > 0) {
        Vector3 lp = m.LurePos();
        float blink = 0.7f + 0.3f * sinf(S.time * (m.bossWind == 2 ? 3.0f : 1.2f));
        DrawSphere(lp, 0.35f, Fade(Color{255, 80, 60, 255}, blink));
        DrawSphere(lp, 1.4f, Fade(Color{255, 90, 60, 255}, 0.12f * blink));
        if (m.bossPhase >= 3) {
            const Agent& b = m.eco.agents[m.bossAgent];
            Vector3 side = Vector3Normalize({-(lp.z - b.pos.z), 0, lp.x - b.pos.x});
            for (int k = -1; k <= 1; k += 2) DrawSphere(Vector3Add(lp, Vector3Scale(side, 8.0f * k)), 0.35f, Fade(Color{255, 80, 60, 255}, blink));
        }
    }
    for (const auto& l : m.lures) { DrawSphere(l.pos, 0.3f, Color{150, 255, 230, 255}); DrawSphere(l.pos, 2.0f + sinf(S.time * 5) * 0.3f, Fade(Color{120, 255, 220, 255}, 0.1f)); }
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
    for (const auto& c : m.crates) if (!c.fallen) DrawCube({c.pos.x, c.kind == 1 ? c.pos.y - 0.5f : c.pos.y - 1.1f, c.pos.z}, 1.4f, 0.03f, 1.4f, Fade(RED, 0.5f + 0.3f * sinf(S.time * 12)));
    // light shafts from above (the Cave's Mouth)
    for (int zi = 0; zi < (int)m.map->zones.size(); zi++) {
        const Json& d = m.map->extra["dressing"][m.map->zones[zi].name];
        if (!d.IsObj() || !d["light_shaft"].Bool0()) continue;
        const Zone& z = m.map->zones[zi];
        Vector3 c = z.Center();
        for (int k = 0; k < 60; k++) {
            float f = k / 60.0f;
            float y = z.y1 + 6 - f * (z.y1 - z.y0 + 6);
            float r = 1.0f + f * 3.0f;
            float a = k * 2.4f + S.time * 0.2f;
            DrawCube({c.x + cosf(a) * r * 0.7f, y, c.z + sinf(a) * r * 0.7f}, 0.08f, 0.6f, 0.08f, Fade(Color{220, 240, 220, 255}, 0.12f * (1 - f)));
        }
    }
    // slipstream mouths: a spiral of silt and bubbles drawn into the current
    for (const auto& l : m.map->links) {
        if (!l.slip || Vector3Distance(l.a, eye) > 40) continue;
        Vector3 dir = Vector3Normalize(Vector3Subtract(l.b, l.a));
        Vector3 side = Vector3Normalize(Vector3CrossProduct(dir, {0, 1, 0}));
        if (Vector3Length(side) < 0.1f) side = {1, 0, 0};
        Vector3 up = Vector3CrossProduct(side, dir);
        for (int k = 0; k < 36; k++) {
            float f = fmodf(S.time * 0.8f + k / 36.0f, 1.0f), ang = k * 0.7f + S.time * 4;
            float r = 1.3f * (1 - f);
            Vector3 p = Vector3Add(l.a, Vector3Add(Vector3Scale(dir, f * 1.5f), Vector3Add(Vector3Scale(side, cosf(ang) * r), Vector3Scale(up, sinf(ang) * r))));
            DrawCube(p, 0.05f, 0.05f, 0.05f, Fade(Color{190, 230, 240, 255}, 0.7f * (1 - f)));
        }
    }
    // marine snow: soft specks that fade with distance; then the bubbles
    for (const auto& s : S.snow) { float dd = Vector3Distance(s, eye); if (dd < 22) DrawBillboard(cam, SoftDot(), s, 0.05f, Fade(Color{220, 232, 226, 255}, 0.55f * (1 - dd / 22))); }
    FxDrawBubbles(cam);
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
    for (const auto& sp : m.salvage) if (!sp.taken) label(Vector3Add(sp.pos, {0, 0.6f, 0}), 14, std::string(Build((BuildType)sp.build).parts[sp.part]) + " (" + Build((BuildType)sp.build).name + ")", {226, 200, 140, 255});
    for (const auto& k : m.floorKeys) label(Vector3Add(k.pos, {0, 0.9f, 0}), 20, "the " + k.name + " key", {240, 200, 90, 255});
    for (const auto& f : m.drops) label(Vector3Add(f.pos, {0, 0.6f, 0}), 20, f.weapon >= 0 ? Weapons().weapons[f.weapon].name : DropName(f.type), {150, 250, 210, 255});
    // the lighthouse lit: every Lost One and the Wyrm on the sonar, through walls
    if (m.revealAll) for (int i = 0; i < (int)m.eco.agents.size(); i++) {
        const Agent& a = m.eco.agents[i];
        if (!a.alive || a.diver >= 0 || !(m.map->species[a.sp].isEnemy || m.IsBoss(i) || m.map->species[a.sp].tier >= 4)) continue;
        if (Vector3DotProduct(Vector3Subtract(a.pos, eye), fwd) <= 0 || Vector3Distance(a.pos, eye) > 120) continue;
        Vector2 sp = GetWorldToScreenEx(a.pos, cam, SCREEN_W, SCREEN_H);
        Color c = m.IsBoss(i) ? Color{255, 90, 70, 255} : m.map->species[a.sp].isEnemy ? Color{190, 150, 255, 255} : Color{255, 200, 120, 255};
        DrawRing(sp, 5, 7, 0, 360, 12, c);
    }
}

// ---------------------------------------------------------------- the HUD
// The helmet's instruments (the spec's HUD: "gauges as real dials with needles (the scent meter, the predator pulse, air,
// scrip as a mechanical counter) ... cracks when the diver is badly hurt"): a brass bezel, an aged-paper face with its
// ticks and a red zone, a needle with its shadow and a cap, a glint on the glass.
static void DrawDial(Vector2 c, float r, float v, const char* label, Color needle, float redFrom = 2, bool redLow = false, const char* readout = nullptr) {
    const float A0 = -225 * DEG2RAD, SWEEP = 270 * DEG2RAD;   // (the sweep: 7:30 round to 4:30)
    DrawCircleV({c.x + 2, c.y + 3}, r + 4, Fade(BLACK, 0.35f));                                    // its shadow on the helmet
    DrawRing(c, r, r + 5, 0, 360, 48, Color{150, 110, 46, 255});                                    // the bezel
    DrawRing(c, r + 1, r + 4, 200, 340, 24, Color{226, 186, 104, 255});                             // (lit along its top)
    DrawCircleV(c, r, Color{214, 202, 170, 255});                                                   // the face, aged paper
    DrawCircleV(c, r * 0.96f, Color{224, 214, 184, 255});
    if (redFrom <= 1) {   // the red zone
        float a = redLow ? 0 : redFrom, b = redLow ? redFrom : 1;
        DrawRing(c, r * 0.72f, r * 0.86f, (A0 + SWEEP * a) * RAD2DEG + 90 - 90, (A0 + SWEEP * b) * RAD2DEG, 16, Color{176, 44, 32, 255});
    }
    for (int k = 0; k <= 10; k++) {   // the ticks
        float a = A0 + SWEEP * k / 10.0f, l = k % 5 == 0 ? 0.24f : 0.13f;
        DrawLineEx({c.x + cosf(a) * r * (0.9f - l), c.y + sinf(a) * r * (0.9f - l)}, {c.x + cosf(a) * r * 0.9f, c.y + sinf(a) * r * 0.9f}, k % 5 == 0 ? 2.0f : 1.0f, Color{40, 34, 28, 255});
    }
    if (label) DrawTextCentered(label, c.x, c.y + r * 0.34f, (int)std::max(9.0f, r * 0.22f), Color{70, 58, 44, 255});
    if (readout) DrawTextCentered(readout, c.x, c.y - r * 0.5f, (int)std::max(10.0f, r * 0.26f), Color{40, 34, 28, 255});
    float a = A0 + SWEEP * std::clamp(v, 0.0f, 1.0f);
    Vector2 tip{c.x + cosf(a) * r * 0.82f, c.y + sinf(a) * r * 0.82f}, tail{c.x - cosf(a) * r * 0.18f, c.y - sinf(a) * r * 0.18f};
    DrawLineEx({tail.x + 2, tail.y + 2}, {tip.x + 2, tip.y + 2}, 3, Fade(BLACK, 0.25f));            // the needle's shadow on the face
    DrawLineEx(tail, tip, 2.5f, needle);
    DrawCircleV(c, r * 0.1f, Color{60, 50, 36, 255}); DrawCircleV(c, r * 0.06f, Color{200, 160, 80, 255});
    DrawRing(c, r * 0.55f, r * 0.95f, 200, 250, 16, Fade(WHITE, 0.16f));                          // a glint on the glass
}
// scrip on a mechanical counter: brass-framed drums whose digits roll over as the count changes
static void DrawCounter(float x, float y, int value, float shown, int digits) {
    float w = 22, h = 32;
    DrawRectangleRounded({x - 6, y - 5, digits * w + 12, h + 10}, 0.2f, 6, Color{150, 110, 46, 255});
    DrawRectangleRounded({x - 3, y - 2, digits * w + 6, h + 4}, 0.15f, 6, Color{30, 26, 22, 255});
    for (int k = 0; k < digits; k++) {
        float place = powf(10.0f, (float)(digits - 1 - k));
        float dv = shown / place;                       // this drum's position (fractional while it rolls)
        int dig = (int)floorf(dv) % 10; float frac = dv - floorf(dv);
        if (k < digits - 1) frac = std::max(0.0f, (frac - 0.9f) * 10.0f);   // (a higher drum turns only as the one below passes 9)
        Rectangle cell{x + k * w, y, w - 2, h};
        DrawRectangleRec(cell, Color{226, 216, 190, 255});
        // (no scissor: the HUD draws into a scaled target; each digit slides a little and fades as it rolls out of its window)
        for (int s = 0; s < 2; s++) {
            int dd = (dig + s) % 10;
            float off = s - frac;   // 0 in the window, -1 gone above, +1 waiting below
            if (fabsf(off) >= 1) continue;
            TxtBold(TextFormat("%d", dd), cell.x + 5, cell.y + 3 + off * h * 0.45f, 24, Fade(Color{30, 26, 22, 255}, 1 - fabsf(off)));
        }
        DrawRectangleGradientV((int)cell.x, (int)cell.y, (int)cell.width, 8, Fade(BLACK, 0.45f), Fade(BLACK, 0.0f));   // (the drum's curve)
        DrawRectangleGradientV((int)cell.x, (int)(cell.y + h - 8), (int)cell.width, 8, Fade(BLACK, 0.0f), Fade(BLACK, 0.45f));
    }
    (void)value;
}
// cracks across the port when the diver is badly hurt: fixed fracture lines from two impact points, more as it worsens
static void DrawPortCracks(float k) {
    if (k <= 0) return;
    static std::vector<std::pair<Vector2, Vector2>> lines;
    if (lines.empty()) {
        uint32_t h = 0x51ED27u; auto R = [&]() { h ^= h << 13; h ^= h >> 17; h ^= h << 5; return (h & 0xffff) / 65535.0f; };
        Vector2 hits[2] = {{SCREEN_W * 0.22f, SCREEN_H * 0.3f}, {SCREEN_W * 0.8f, SCREEN_H * 0.7f}};
        for (auto hp : hits) for (int s = 0; s < 9; s++) {
            float a = s * 0.7f + R() * 0.5f; Vector2 p = hp;
            for (int seg = 0; seg < 4; seg++) { float l = 20 + R() * 60; a += (R() - 0.5f) * 0.6f; Vector2 q{p.x + cosf(a) * l, p.y + sinf(a) * l}; lines.push_back({p, q}); p = q; }
        }
    }
    int n = (int)(lines.size() * std::clamp(k, 0.0f, 1.0f));
    for (int i = 0; i < n; i++) {
        DrawLineEx(lines[i].first, lines[i].second, 2.2f, Fade(WHITE, 0.35f));
        DrawLineEx({lines[i].first.x + 1, lines[i].first.y + 1}, {lines[i].second.x + 1, lines[i].second.y + 1}, 1.0f, Fade(BLACK, 0.35f));
    }
}

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
    {   // (the counter's drums roll toward the new figure)
        static float shown = -1; if (shown < 0 || fabsf(shown - d.scrip) > 50000) shown = (float)d.scrip;
        shown += ((float)d.scrip - shown) * std::min(1.0f, GetFrameTime() * 6); if (fabsf(shown - d.scrip) < 0.02f) shown = (float)d.scrip;
        DrawCounter(SCREEN_W - 150.0f, 20, d.scrip, shown, 5);
        Txt("scrip", SCREEN_W - 150, 60, 14, Fade(paper, 0.7f));
    }
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
    {   // the tacticals G throws (Q picks): the selected one bright, the empty ones left out (limpets always shown)
        int have[TAC_COUNT] = {d.limpets, d.inkBombs, d.chumBags, d.flares};
        int rows = 0; for (int t = 0; t < TAC_COUNT; t++) if (t == 0 || have[t] > 0) rows++;
        float y = SCREEN_H - 20.0f - rows * 16;
        for (int t = 0; t < TAC_COUNT; t++) if (t == 0 || have[t] > 0) { Txt(TextFormat("%s %d", TacticalName(t), have[t]), SCREEN_W - 120, y, 14, Fade(paper, d.tactical == t ? 0.95f : rows > 1 ? 0.45f : 0.8f)); y += 16; }
    }
    {   // salvage: the build held, the Shell Shield's strength, the parts carried, the brush
        float y = SCREEN_H - 250.0f;
        if (d.build != BuildType::None) {
            Txt(d.build == BuildType::ShellShield ? TextFormat("Shell Shield %d  (B: bash%s)", (int)d.shieldHP, d.bashCd > 0 ? TextFormat(" %.0f", d.bashCd) : "") : TextFormat("%s  (B: set it down)", Build(d.build).name), SCREEN_W - 330, y, 15, Color{226, 196, 120, 255});
            y += 18;
        }
        for (int b = 1; b < (int)BuildType::COUNT; b++) {
            int n = 0; for (int k = 0; k < 3; k++) if (d.partsMask & (1 << (b * 3 + k))) n++;
            if (n) { Txt(TextFormat("parts: %s %d/3%s", Build((BuildType)b).name, n, n == 3 ? " (a workbench)" : ""), SCREEN_W - 330, y, 13, Fade(paper, 0.85f)); y += 15; }
        }
        if (d.brush) Txt("cleaning brush (X)", SCREEN_W - 330, y, 13, Fade(paper, 0.7f));
    }
    // the charm pouch: the next charm (T) and what's running
    if (d.pouchNext < (int)d.pouch.size()) {
        std::string n; for (const auto& c : Charms()) if (c.id == d.pouch[d.pouchNext]) n = c.name;
        Txt(TextFormat("T: %s  (%d left)", n.c_str(), (int)d.pouch.size() - d.pouchNext), 140, SCREEN_H - 100.0f, 14, Color{230, 220, 190, 255});
    }
    {
        std::string run;
        if (d.circleT > 0) run += TextFormat("Salt Circle %.0f  ", d.circleT);
        if (d.finsT > 0) run += TextFormat("Slick Fins %.0f  ", d.finsT);
        if (d.shellT > 0) run += TextFormat("Hard Shell %.0f  ", d.shellT);
        if (d.ghostT > 0) run += TextFormat("Ghost Fin %.0f  ", d.ghostT);
        if (d.luckKills > 0) run += TextFormat("Fisher's Luck x%d  ", d.luckKills);
        if (d.keepBrines) run += "Brines kept  ";
        if (d.luckyLocker) run += "Lucky Locker  ";
        if (!run.empty()) Txt(run, 140, SCREEN_H - 120.0f, 14, Color{160, 230, 210, 255});
    }
    if (d.voidT > 0 || d.wormT > 0) {
        // the Void's two ends of the world: a countdown in red at the middle of the screen
        const char* what = d.voidT > 0 ? "THE VOID PULLS YOU DOWN: SWIM BACK" : "THE SAND SHAKES: BACK INSIDE THE STAKES";
        float left = d.voidT > 0 ? m.map->extra["void"]["pull_s"].F(5) - d.voidT : m.map->extra["worm"]["tremor_s"].F(8) - d.wormT;
        DrawTextCenteredBold(TextFormat("%s  %.1f", what, std::max(0.0f, left)), SCREEN_W / 2.0f, SCREEN_H * 0.3f, 22, Color{255, 90, 70, (unsigned char)(200 + 55 * sinf(S.time * 10))});
    }
    if (d.inkCaps > 0) Txt(TextFormat("ink caps: %d", d.inkCaps), SCREEN_W - 280, SCREEN_H - 218, 13, Color{170, 170, 200, 255});
    if (d.egg) Txt("the Relict egg (it wants it back)", SCREEN_W - 280, SCREEN_H - 202, 13, Color{240, 200, 150, 255});
    if (d.spark || d.ichorJar) Txt(std::string(d.spark ? "a jar of the crystal's spark  " : "") + (d.ichorJar ? "a jar of ichor" : ""), SCREEN_W - 280, SCREEN_H - 186, 13, Color{150, 220, 240, 255});
    if (d.drumUses > 0) Txt(TextFormat("F: the drum (%d beats; %d to the fifth)", d.drumUses, 5 - m.drumBeats % 5), SCREEN_W - 280, SCREEN_H - 170, 13, Color{226, 190, 120, 255});
    if (!d.downed) for (int i = 0; i < (int)d.weapons.size(); i++) Txt(TextFormat("%d %s", i + 1, m.W(d.weapons[i]).name.c_str()), SCREEN_W - 280, SCREEN_H - 150 + i * 16.0f, 13, i == d.cur ? paper : Fade(paper, 0.5f));
    // health: a brass pressure gauge, bottom left; the tonics' bottles beside it
    Vector2 g{80, SCREEN_H - 80.0f};
    float frac = std::clamp(d.hp / d.hpMax, 0.0f, 1.0f);
    DrawDial(g, 44, frac, "PRESSURE", Color{150, 26, 20, 255}, 0.25f, true, TextFormat("%d", (int)std::max(0.0f, d.hp)));
    (void)brass;
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
    // the scent meter, the predator pulse (the nearest hunter's closeness; the needle trembles with it) and the air (your
    // wind) up the left of the helmet above the pressure gauge
    float pulse = 0;
    for (int i = 0; i < (int)m.eco.agents.size(); i++) {
        const Agent& ga = m.eco.agents[i];
        if (!ga.alive || ga.diver >= 0 || m.IsBoss(i) || m.map->species[ga.sp].tier < 4) continue;
        pulse = std::max(pulse, 1 - Vector3Distance(ga.pos, d.pos) / 40);
    }
    pulse = std::clamp(pulse, 0.0f, 1.0f);
    float tremble = pulse > 0.3f ? sinf(S.time * (14 + pulse * 20)) * 0.02f * pulse : 0;
    DrawDial({52, SCREEN_H - 172.0f}, 26, scent, "SCENT", Color{150, 26, 20, 255}, 0.7f);
    DrawDial({52, SCREEN_H - 238.0f}, 26, pulse + tremble, "PULSE", Color{40, 34, 28, 255}, 0.75f);
    DrawDial({52, SCREEN_H - 304.0f}, 26, d.stamina, "AIR", Color{40, 70, 110, 255}, 0.2f, true);
    DrawPortCracks(d.downed ? 1.0f : (0.3f - frac) / 0.3f);
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
        Rectangle p{cx - 300, cy - 230, 600, 460};
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
        y += 8;
        TxtBold(TextFormat("Arcade tokens: %d", S.awardTokens), p.x + 40, y, 18, brass); y += 26;
        for (size_t k = 0; k < S.awardLines.size() && k < 7; k++) { Txt(S.awardLines[k], p.x + 56, y, 14, Fade(paper, 0.85f)); y += 19; }
        DrawTextCentered("Enter: dive again     Esc: leave the match", cx, p.y + p.height - 34, 15, Fade(paper, 0.8f));
    }
    if (S.mode == 1 && m.time < 12 && !S.shotMode)
        DrawTextCentered("WASD swim, Space/Ctrl up/down, Shift sprint, RMB aim, LMB fire, R reload, E use, V knife, G limpet, 1-3 weapons", cx, SCREEN_H - 20, 13, Fade(paper, 0.6f));
}

} // namespace rt

using namespace rt;

static std::string gRtMap = "ship";
bool RedTideAudioActive() { return S.audioOn; }
void StartRedTide(Game& g, const char* map) {
    gRtMap = map ? map : "ship";
    gSndMap = gRtMap;
    StartShip(1, (uint32_t)GetRandomValue(1, 1 << 30), gRtMap);
    S.shotMode = false; S.awarded = false; S.awardLines.clear(); S.awardTokens = 0;
    Me().pouch = GetProfile().pouch;                    // the Salt Charms the diver brought
    S.silhouette = 0;
    S.lineup = -1; S.studio = -1; S.freeze = false;
    g.scene = Scene::RedTide;   // (the mouse look takes the pointer itself: MouseLook in Input)
}

void SceneRedTide(Game& g) {
    S.audioOn = false;
    if (RedTidePageFrame(g, (float)GetTime())) { AudioRedTide(RtAudio{}); return; }   // an arcade page (the dossier, records, ...)
    if (S.studio >= 0) { S.time += 1 / 60.0f; DrawRedTideStudio(S.studio, S.time); return; }
    if (!S.active || !S.m) { StartRedTide(g, gRtMap.c_str()); return; }
    if (S.mode == 1 && !S.levelReady && IsWindowReady()) BuildLevelModel();
    float dt = std::min(GetFrameTime(), 1 / 30.0f);
    if (S.shotMode) dt = 1 / 60.0f;
    S.time += dt;
    if (S.lineup < 0) {
        Input(dt);
        Match& m = M();
        if (S.mode == 0) { m.phase = TidePhase::Calm; m.phaseT = -1e9f; }   // the tank never tides
        if (!S.freeze) m.Step(dt);
        S.audioOn = !S.shotMode;
        SoundFrame(dt);
        DrainFx();
        if (m.over && !S.awarded && !S.shotMode && S.mode == 1) {
            // the match's end: the profile's tokens, records, milestone charms and the dossier pages earned
            S.awarded = true;
            MatchSummary ms; ms.map = gRtMap; ms.tide = m.tide; ms.scrip = Me().scripEarned; ms.timeS = m.time; ms.forgeAtS = m.forgeAt;
            ms.bossKilled = m.bossKilled; ms.questDone = m.questDone;
            ms.pages.assign(m.dossierSeen.begin(), m.dossierSeen.end()); ms.bonusPages = m.bonusEarned;
            S.awardLines = AwardMatch(ms, &S.awardTokens);
        }
        if (m.over && !S.shotMode && IsKeyPressed(KEY_ENTER)) { StartRedTide(g, gRtMap.c_str()); return; }
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
    FxStep(dt, gSurfY);
    if (S.lineup < 0 && S.m && !Me().dead) {   // your own breath: a burst from the exhaust above the helmet's rim, rising across the top of the view
        float rate = 0.3f + 0.25f * (1 - std::clamp(Me().stamina, 0.0f, 1.0f)) + (Me().hurtT > 0 ? 0.25f : 0.0f), ph = fmodf(S.time * rate, 1.0f);   // (quicker winded or hurt)
        if (ph < rate * dt) { Vector3 f = M().Forward(Me()); FxBubbles(Vector3Add(M().Eye(Me()), Vector3Add(Vector3Scale(f, 0.35f), {0, 0.32f, 0})), 9, 0.1f); }
    }
    DrawScene();
    if (S.lineup < 0 && S.m && Me().slipLink >= 0) {
        // riding a slipstream: the rock streams past in the dark
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(Color{4, 14, 18, 255}, 0.85f));
        for (int k = 0; k < 90; k++) {
            float a = k * 2.399f, rr = fmodf(S.time * 900 + k * 97, 900.0f) + 20;
            Vector2 c{SCREEN_W / 2.0f, SCREEN_H / 2.0f};
            Vector2 p0{c.x + cosf(a) * rr, c.y + sinf(a) * rr * 0.6f}, p1{c.x + cosf(a) * (rr + 40), c.y + sinf(a) * (rr + 40) * 0.6f};
            DrawLineEx(p0, p1, 2, Fade(Color{170, 220, 230, 255}, 0.5f));
        }
        DrawTextCenteredBold(M().map->links[Me().slipLink].passage, SCREEN_W / 2.0f, SCREEN_H / 2.0f - 12, 26, Color{220, 240, 240, 255});
    }
    if (S.lineup < 0 && S.silhouette < 0.5f && GameSettings().rtLens) { int zz = M().eco.ZoneAt(Me().pos); DrawHelmetPort(zz >= 0 && M().map->zones[zz].air ? 1.0f : 0.0f, S.time); }   // the helmet's port rim (the Visual Overhaul)
    if (S.lineup < 0 && S.silhouette < 0.5f) DrawHud();
    else if (S.lineup >= 0) TxtBold(TextFormat("Red Tide - the Sunken Ship's species, page %d (CreatureBuilder)", S.lineup + 1), 24, 18, 20, Color{220, 90, 80, 255});
}

// --shots: 0 the tank, 1 its silhouettes, 2-3 the species lineup, 10+ the Sunken Ship from set places
void DebugRedTideShot(Game& g, int which) {
    S.studio = which >= 200 ? which - 200 : -1;
    S.freeze = false;
    if (S.studio >= 0) { S.time = 2.0f; g.scene = Scene::RedTide; return; }   // (the studio: 200 + its set)
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
    StartShip(which == 17 ? 4 : 1, 20260930, which == 19 && getenv("DEPTH_RTMAP") ? getenv("DEPTH_RTMAP") : which >= 50 ? "void" : which >= 40 ? "atlantis" : which >= 30 ? "reef" : which >= 20 ? "cave" : "ship");
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
    auto view = [&](const char* zone, float yUp, float pitch) {
        int zi = m.map->ZoneIndex(zone);
        const Zone& z = m.map->zones[zi];
        Vector3 c = z.Center(); c.y = z.y0 + yUp;
        float best = -1;
        for (int k = 0; k < 120; k++) {
            Vector3 p{z.plan.x + 1.5f + (z.plan.width - 3) * ((k * 37) % 120) / 120.0f, z.y0 + yUp, z.plan.y + 1.5f + (z.plan.height - 3) * ((k * 53) % 120) / 120.0f};
            if (!m.level.Inside(p, 0.5f, m.linkOpen)) continue;
            bool nearStation = false; for (const auto& st : m.level.stations) if (Vector3Distance({st.pos.x, p.y, st.pos.z}, p) < 3.5f) nearStation = true;
            for (const auto& pr : m.level.props) if (pr.kind == PropKind::Fan && Vector3Distance({pr.pos.x, p.y, pr.pos.z}, p) < 6) nearStation = true;
            if (nearStation) continue;
            Vector3 dir = Vector3Subtract(c, p); dir.y = 0;
            if (Vector3Length(dir) < 3) continue;
            dir = Vector3Normalize(dir);
            float free = 0;
            while (free < 40 && m.level.Inside(Vector3Add(p, Vector3Scale(dir, free + 0.5f)), 0.3f, m.linkOpen)) free += 0.5f;
            if (free > best) { best = free; d.pos = p; d.zone = zi; d.yaw = atan2f(dir.x, dir.z); d.pitch = pitch; }
        }
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
        case 17: {                                                                            // the crew: three divers in the salon before you
            place("Grand Salon", {2, 3, 2}, 0.0f, -0.02f);
            Vector3 f{sinf(d.yaw), 0, cosf(d.yaw)}, r{cosf(d.yaw), 0, -sinf(d.yaw)};
            const int zi = d.zone; const Zone& z = m.map->zones[zi];
            for (int k = 1; k < (int)m.divers.size() && k < 4; k++) {
                DiverState& o = m.divers[k];
                o.pos = z.Clamp(Vector3Add(d.pos, Vector3Add(Vector3Scale(f, 3.2f + k * 0.9f), Vector3Scale(r, (k - 2) * 1.7f))), 0.8f);
                o.yaw = d.yaw + PI + (k - 2) * 0.5f; o.pitch = 0;
                o.vel = k == 3 ? Vector3Scale(r, -2.5f) : Vector3{0, 0, 0};   // (one swimming across)
                if (k == 3) o.yaw = atan2f(-r.x, -r.z);
                if (o.agent >= 0) m.eco.agents[o.agent].pos = o.pos;
            }
            break;
        }
        case 18: case 26: case 58: {                                                          // a faction squad lined up before you (spec shot set 4)
            if (which == 18) place("Stern & Swim Platform", {2, 2, 2}, 0.0f, 0);
            else if (which == 26) place("The Flooded Gallery", {2, 2, 2}, 0.0f, 0);
            else view("The Station: Specimen Labs", 2.0f, 0.0f);
            m.BeginTidePublic(5);
            for (int i = 0; i < 600 && [&] { for (const auto& a : m.eco.agents) if (a.alive && a.unit >= 0) return false; return true; }(); i++) m.Step(1 / 20.0f);
            Vector3 f{sinf(d.yaw), 0, cosf(d.yaw)}, r{cosf(d.yaw), 0, -sinf(d.yaw)};
            int k = 0;
            for (auto& a : m.eco.agents) if (a.alive && a.unit >= 0 && k < 5) {
                a.pos = Vector3Add(d.pos, Vector3Add(Vector3Scale(f, 4.5f + (k % 2) * 1.5f), Vector3Add(Vector3Scale(r, (k - 2) * 1.5f), {0, 0.2f, 0})));
                a.vel = Vector3Scale(f, -0.05f); k++;
            }
            d.pitch = -0.05f; d.hp = d.hpMax = 250;
            TraceLog(LOG_INFO, "squad shot: %d faction divers placed", k);
            S.freeze = true;
            break;
        }
        case 15: place("Cabin Deck", {3, 2, 3}, 0.0f, 0); break;                             // the cabins and the moray pipes
        case 19: {                                                                            // the viewmodel: a gun in the hands (DEPTH_RTGUN=<id>)
            if (m.map->ZoneIndex("Engine Room") >= 0) place("Engine Room", {12.5f, 3, 11}, 0.4f, -0.05f);
            const char* id = getenv("DEPTH_RTGUN") ? getenv("DEPTH_RTGUN") : "cormorant";
            const auto& WW = Weapons().weapons;
            for (int i = 0; i < (int)WW.size(); i++) if (WW[i].id == id) { Held h; h.def = i; h.mag = WW[i].mag; d.weapons = {h}; d.cur = 0; }
            if (const char* st = getenv("DEPTH_STATION")) {   // (DEPTH_STATION=tonic|locker|forge|power|workbench|cache: stand before the first one)
                static const char* N[] = {"rack", "tonic", "locker", "forge", "power", "workbench", "trap", "quest", "cleaning", "feature", "hazard", "entry", "boss", "queststep", "cache"};
                int want = -1; for (int k = 0; k < 15; k++) if (std::string(st) == N[k]) want = k;
                if (std::string(st) == "traps") {   // (the set pieces before you: a crate and a stalactite each falling, each fallen)
                    Vector3 f{sinf(d.yaw), 0, cosf(d.yaw)}, r{cosf(d.yaw), 0, -sinf(d.yaw)};
                    int zi = d.zone; float y0 = zi >= 0 ? m.map->zones[zi].y0 : d.pos.y - 1;
                    auto put = [&](int kind, float side, bool fallen) {
                        Crate c; c.kind = kind; c.fallen = fallen; c.t = fallen ? -2 : 0.5f;
                        c.pos = Vector3Add(d.pos, Vector3Add(Vector3Scale(f, 5), Vector3Scale(r, side))); c.pos.y = y0 + 1.2f; c.top = y0 + 4.5f;
                        m.crates.push_back(c);
                    };
                    put(0, -2.4f, false); put(0, -0.8f, true); put(1, 0.8f, false); put(1, 2.4f, true);
                    d.pitch = 0.05f;
                }
                if (std::string(st) == "door" && !m.level.doors.empty()) {   // (the doors shut again, and stand before the first)
                    for (auto& dr : m.level.doors) dr.open = false;
                    const auto& dr = m.level.doors[getenv("DEPTH_DOOR") ? std::clamp(atoi(getenv("DEPTH_DOOR")), 0, (int)m.level.doors.size() - 1) : 0];
                    const Link& l = m.map->links[dr.link];
                    Vector3 dir = Vector3Normalize(Vector3Subtract(l.a, l.b)); dir.y = 0; dir = Vector3Normalize(dir);
                    d.pos = Vector3Add(dr.pos, Vector3Scale(dir, 3.5f)); d.pos.y = dr.pos.y + 0.2f;
                    d.zone = m.eco.ZoneAt(d.pos);
                    Vector3 to = Vector3Subtract(dr.pos, d.pos);
                    d.yaw = atan2f(to.x, to.z); d.pitch = -0.05f;
                }
                for (const auto& s : m.level.stations) if ((int)s.type == want) {
                    Vector3 c = s.zone >= 0 ? m.map->zones[s.zone].Center() : Vector3Add(s.pos, {3, 0, 0});
                    Vector3 dir = Vector3Normalize({c.x - s.pos.x, 0, c.z - s.pos.z});
                    d.pos = Vector3Add(s.pos, Vector3Scale(dir, 3.2f)); d.zone = s.zone;
                    if (s.zone >= 0) d.pos = m.map->zones[s.zone].Clamp(d.pos, 0.5f);
                    Vector3 to = Vector3Subtract(s.pos, d.pos);
                    d.yaw = atan2f(to.x, to.z); d.pitch = -0.12f;
                    break;
                }
            }
            break;
        }
        case 16: {                                                                            // salvage: the builds set down before a workbench
            int wb = -1; for (int i = 0; i < (int)m.level.stations.size(); i++) if (m.level.stations[i].type == StationType::Workbench) wb = i;
            const Station& st = m.level.stations[wb];
            const Zone& z = m.map->zones[st.zone];
            Vector3 c = z.Center(); c.y = st.pos.y;
            d.pos = z.Clamp(Vector3Lerp(st.pos, c, 0.15f), 1.0f); d.zone = st.zone;
            Vector3 to = Vector3Subtract(c, d.pos); d.yaw = atan2f(to.x, to.z) + 0.45f; d.pitch = -0.2f;
            // each build set down a little way off, a part left lying, a flare burning, and a full pocket
            Vector3 f{sinf(d.yaw), 0, cosf(d.yaw)}, r{cosf(d.yaw), 0, -sinf(d.yaw)};
            BuildType kinds[4] = {BuildType::Turbine, BuildType::DecoyBuoy, BuildType::NetTripwire, BuildType::BubbleWall};
            Vector3 at[4] = {Vector3Add(Vector3Scale(f, 3.5f), Vector3Scale(r, -2.0f)), Vector3Add(Vector3Scale(f, 5.0f), Vector3Scale(r, 1.8f)), Vector3Scale(f, 2.5f), Vector3Scale(f, 4.0f)};
            Vector3 home = d.pos;
            for (int k = 0; k < 4; k++) { d.pos = z.Clamp(Vector3Add(home, at[k]), 1.0f); d.build = kinds[k]; if (kinds[k] == BuildType::BubbleWall) d.pos = z.Clamp(Vector3Add(home, Vector3Scale(f, 4.5f)), 1.0f); m.UseBuild(0); }
            d.pos = home;
            if (!m.salvage.empty()) { m.salvage[0].pos = z.Clamp(Vector3Add(home, Vector3Add(Vector3Scale(f, 2.0f), Vector3Scale(r, 1.2f))), 0.6f); m.salvage[0].pos.y = z.y0 + 0.6f; m.salvage[0].taken = false; }
            Match::FlareLight fl; fl.pos = z.Clamp(Vector3Add(home, Vector3Add(Vector3Scale(f, 6.0f), Vector3Scale(r, -1.0f))), 0.5f); fl.pos.y = z.y0 + 0.3f; m.flareLights.push_back(fl);
            d.build = BuildType::ShellShield; d.shieldHP = 220; d.partsMask = (1 << ((int)BuildType::Turbine * 3)) | (1 << ((int)BuildType::Turbine * 3 + 2));
            d.inkBombs = 1; d.chumBags = 2; d.flares = 1; d.tactical = TAC_CHUM; d.brush = true;
            break;
        }
        // the Underwater Cave
        case 20: place("The Mouth", {2, 4, 2}, 0.2f, 0.05f); break;                           // the start pocket under the light shaft
        case 21: place("The Flooded Gallery", {3, 4, 3}, 0.1f, 0); break;                     // the long hall, slipstream A's mouth
        case 22: place("The Chimney", {2, 4, 2}, 0.3f, 0.6f); break;                          // looking up the shaft
        case 23: place("Dry Chamber II: the Toad Pool", {3, 1, 3}, 0.2f, 0); break;           // an air chamber, on foot
        case 24: place("The Cathedral", {6, 6, 6}, 0.0f, 0.05f); break;                       // the crystal coral and the Lobster
        case 25: place("The Sump", {4, 3, 4}, 0.2f, 0); break;                                // the silt, the sturgeon, the sleeper shark
        // Approaching the Void
        case 50: d.pos = {2, 3, -3}; d.zone = m.level.startZone; d.yaw = 0.9f; d.pitch = 0.02f; break;   // the rim from the hatch: sea pens, the dark plain
        case 51: view("The Upper Galleries", 3.0f, 0.0f); break;                               // glass sponges, bamboo coral, the overlook
        case 52: view("The Station: Specimen Labs", 2.0f, 0.0f); break;                        // the broken tanks
        case 53: {                                                                              // the vault: the Leviathan's lure in the dark
            m.Step(1 / 20.0f);
            int v = m.map->ZoneIndex("The Lowest Vault");
            const Zone& z = m.map->zones[v];
            if (m.bossAgent >= 0) { Agent& b = m.eco.agents[m.bossAgent]; b.pos = z.Clamp({0, z.y0 + 12, -370}, 3); b.zone = v; b.vel = {-1, 0, 0}; m.bossActive = true; }
            Vector3 lp = m.LurePos();
            d.pos = z.Clamp(Vector3Add(lp, {-18, -2, 4}), 1); d.zone = v;
            Vector3 to = Vector3Subtract(lp, m.Eye(d)); d.yaw = atan2f(to.x, to.z); d.pitch = std::clamp(asinf(Vector3Normalize(to).y), -0.5f, 0.5f);
            break;
        }
        case 54: view("The Warrens", 2.0f, 0.0f); break;                                       // the tubes
        case 55: {                                                                              // an overlook: the void
            int g = m.map->ZoneIndex("The Upper Galleries");
            const Zone& z = m.map->zones[g];
            d.pos = {z.plan.x + 4, z.y0 + 4, -20}; d.zone = g; d.yaw = -1.5708f; d.pitch = -0.15f;
            break;
        }
        // Atlantis (a clear view: the spot in the district with the longest open line toward its middle)
        case 40: view("Harbor Gate", 2.0f, 0.05f); break;                                     // the gatehouse, the avenue up the hill
        case 41: view("The Lower Town", 3.0f, 0.02f); break;                                  // the streets and houses
        case 42: view("The Forum", 4.0f, 0.05f); break;                                       // the plaza, sea fans, the crystal
        case 43: view("The Grand Chapel", 3.0f, -0.08f); break;                               // the nave, braziers, the god-pool
        case 44: {                                                                            // the rampart and the sea beyond
            int w = m.map->ZoneIndex("The Wall & Ramparts");
            const Zone& z = m.map->zones[w];
            // on the north-west of the ring, looking along it as it curves away to the west
            d.pos = z.Clamp({-66, z.y0 + 4.0f, 70}, 0.8f); d.zone = w;
            d.yaw = -2.2f; d.pitch = -0.12f;
            break;
        }
        case 45: {                                                                            // the Wyrm up through a chapel grate
            int ch = m.map->ZoneIndex("The Grand Chapel");
            const Zone& z = m.map->zones[ch];
            d.pos = z.Clamp({-9, z.y0 + 3.0f, -10}, 0.6f); d.zone = ch;
            m.Step(1 / 20.0f);
            for (int i = 0; i < 60 && m.wyrmState < 2; i++) m.Step(1 / 20.0f);
            for (int i = 0; i < 10; i++) m.Step(1 / 20.0f);
            if (m.bossAgent >= 0) { Vector3 to = Vector3Subtract(m.eco.agents[m.bossAgent].pos, m.Eye(d)); d.yaw = atan2f(to.x, to.z); d.pitch = std::clamp(asinf(Vector3Normalize(to).y), -0.6f, 0.6f); }
            break;
        }
        // the Coral Reef
        case 30: place("The Lagoon", {3, 2.5f, 3}, 0.2f, 0.1f); break;                        // the shallows under the surface
        case 31: place("Staghorn Forest", {3, 6, 3}, 0.1f, 0); break;                         // the thicket's corridors
        case 32: place("The Bommie", {4, 8, 4}, 0.0f, 0.1f); break;                           // the coral head and the cleaners
        case 33: place("The Drop-off Wall", {4, 40, 4}, 0.3f, -0.2f); break;                  // the wall falling into the blue
        case 34: place("The Blue Hole", {6, 20, 6}, 0.0f, -0.3f); break;                      // looking down the hole
        case 35: {                                                                            // the Matriarch and her pod
            m.Step(1 / 20.0f);
            int b = m.bossAgent;
            if (b < 0) break;
            m.bossActive = true; m.bossProvoked = true;
            for (int i = 0; i < 6; i++) m.Step(1 / 20.0f);
            const Agent& B = m.eco.agents[b];
            const Zone& z = m.map->zones[B.zone];
            d.pos = z.Clamp(Vector3Add(B.pos, {26, 4, 12}), 0.6f); d.zone = B.zone;
            Vector3 to = Vector3Subtract(B.pos, m.Eye(d));
            d.yaw = atan2f(to.x, to.z); d.pitch = std::clamp(asinf(Vector3Normalize(to).y), -0.5f, 0.5f);
            break;
        }
        default: break;
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
    float swim = Engine().M("swim_speed", 2);   // the workbook's 2.0, or movement_tuning.json's
    check(fabsf(speed - swim) < 0.05f && moved > 1.6f * swim && moved < 2.0f * swim, TextFormat("swims at %.2f m/s (swim_speed %.1f), %.2f m in 2 s", speed, swim, moved));
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

// --audio-test: every species of a Red Tide map (0 ship .. 4 void), with its size, for the voice checks
void RedTideSpeciesForAudio(int map, std::vector<std::pair<std::string, int>>& out) {
    static const char* K[5] = {"ship", "cave", "reef", "atlantis", "void"};
    out.clear();
    const MapData& m = Map(K[std::clamp(map, 0, 4)]);
    for (const Species& s : m.species) if (!s.isDiver) out.push_back({s.name, s.size});
}
