// The Trawl in first person: the Gannet, the sea and the Lagoon's life drawn in 3D through Red Tide's inked
// renderer, from the eyes of the hand you play (see trawl_view3d.h for the frames). Everything here only reads the
// simulation; trawl.cpp feeds it input and puts the shared HUD over it.
#include "trawl_view3d.h"
#include "game.h"
#include "raymath.h"
#include "redtide_render.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace tw {
using rt::MeshBuilder;

// ---------------------------------------------------------------- frames
Matrix BoatMatrix(const Boat& b) {
    Matrix m = MatrixMultiply(MatrixRotateX(b.roll), MatrixRotateZ(b.pitch));   // +roll: starboard down; +pitch: bow up
    m = MatrixMultiply(m, MatrixRotateY(-b.heading));                           // the bow along (cos h, sin h) on the sea
    return MatrixMultiply(m, MatrixTranslate(b.pos.x, b.heave, b.pos.y));
}
Vector3 BoatPoint(const Boat& b, Vector3 l) { return Vector3Transform(l, BoatMatrix(b)); }
static Vector3 BoatDir(const Boat& b, Vector3 d) { Matrix m = BoatMatrix(b); m.m12 = m.m13 = m.m14 = 0; return Vector3Transform(d, m); }
static Vector3 W3(Vector2 w, float y) { return {w.x, y, w.y}; }            // a point on the sea's plane at a height
static Vector3 WD(Vector3 p) { return {p.x, -p.z, p.y}; }                  // world x/y and depth -> 3D
static const float QUAY_Y = 1.45f;                                         // the quay's top over still water
static bool OnQuay(const Gannet& g, const Crew& c) { return g.moored && c.deck == 0 && c.p.y < -2.4f; }

Vector2 LookDeckDir(const Eye3D& e) { return {cosf(e.yaw), sinf(e.yaw)}; }

// Where a hand stands to work a station: beside its fitting, not inside it (the deck frame; the simulation keeps the
// station's own point), and never inside the funnel, the mast or the cannon's mount.
static Vector2 StandSpot(const Crew& c) {
    Vector2 p = c.p;
    if (c.station >= 0 && c.deck == 0) {
        Vector2 at = Stations()[c.station].at;
        switch (Stations()[c.station].kind) {
            case StationKind::PortRod: p = {at.x, at.y + 0.75f}; break;
            case StationKind::StarRod: p = {at.x, at.y - 0.75f}; break;
            case StationKind::SternRodP: p = {at.x + 0.6f, at.y + 0.25f}; break;
            case StationKind::SternRodS: p = {at.x + 0.6f, at.y - 0.25f}; break;
            case StationKind::Lantern: p = {at.x - 0.7f, at.y}; break;
            case StationKind::Harpoon: p = {at.x - 0.85f, at.y}; break;
            case StationKind::Locker: p = {at.x, at.y + 0.75f}; break;
            case StationKind::AirPump: p = {at.x, at.y - 0.7f}; break;
            case StationKind::Helm: p = {at.x - 0.5f, at.y}; break;
            case StationKind::NetWinch: p = {at.x + 0.55f, at.y}; break;
            default: break;
        }
    }
    if (c.deck == 0) {
        struct Post { Vector2 c; float r; };
        for (Post k : {Post{{0.6f, 1.0f}, 0.55f}, Post{{0.2f, 0}, 0.32f}, Post{{9.6f, 0}, 0.5f}}) {
            Vector2 d = Vector2Subtract(p, k.c);
            float l = Vector2Length(d);
            if (l < k.r) {
                p = Vector2Add(k.c, Vector2Scale(l > 1e-3f ? Vector2Scale(d, 1 / l) : Vector2{-1, 0}, k.r));
                if (p.x > 0.8f && fabsf(p.y) < 2.1f) p = {k.c.x - k.r, k.c.y};   // (not through the wheelhouse's aft wall: aft of the post)
            }
        }
    }
    return p;
}

Camera3D EyeCamera(const Gannet& g, int you, const Eye3D& e) {
    const Crew& c = g.crew[you];
    Camera3D cam{};
    cam.fovy = 72; cam.projection = CAMERA_PERSPECTIVE;
    Vector3 dl{cosf(e.pitch) * cosf(e.yaw), sinf(e.pitch), cosf(e.pitch) * sinf(e.yaw)};
    if (c.overboard && !c.dead) {
        // in the water: your eyes ride the swell, and the boat's roll is hers alone
        float h = g.sea.Height(c.swim.x, c.swim.y);
        cam.position = {c.swim.x, h + 0.28f, c.swim.y};
        Vector3 d = Vector3Transform(dl, MatrixRotateY(-g.boat.heading));
        cam.target = Vector3Add(cam.position, d); cam.up = {0, 1, 0};
        return cam;
    }
    if (OnQuay(g, c)) {
        Vector2 w = g.boat.ToWorld(c.p);
        cam.position = W3(w, QUAY_Y + EYE_H);
        Vector3 d = Vector3Transform(dl, MatrixRotateY(-g.boat.heading));
        cam.target = Vector3Add(cam.position, d); cam.up = {0, 1, 0};
        return cam;
    }
    float floorY = c.deck == 1 ? ENGINE_Y : DECK_Y;
    Vector2 sp = StandSpot(c);
    cam.position = BoatPoint(g.boat, {sp.x, floorY + c.z + (c.fallen ? 0.42f : EYE_H), sp.y});
    cam.target = Vector3Add(cam.position, BoatDir(g.boat, dl));
    // your eyes keep half of her roll: enough to feel the deck go, not enough to make the horizon a seesaw
    cam.up = Vector3Normalize(Vector3Lerp({0, 1, 0}, BoatDir(g.boat, {0, 1, 0}), 0.5f));
    return cam;
}

bool AimAtWater(const Gannet& g, const Camera3D& cam, Vector2 screen, Vector2* deckOut) {
    Ray ray = GetScreenToWorldRayEx(screen, cam, SCREEN_W, SCREEN_H);
    Vector3 hit;
    bool water = ray.direction.y < -0.02f;
    if (water) {
        // the swell's height where it lands (twice: the second guess is close enough)
        float h = 0;
        for (int k = 0; k < 2; k++) {
            float t = (h - ray.position.y) / ray.direction.y;
            hit = Vector3Add(ray.position, Vector3Scale(ray.direction, t));
            h = g.sea.Height(hit.x, hit.z);
        }
        float far = Vector3Distance(ray.position, hit);
        if (far > 60) { hit = Vector3Add(ray.position, Vector3Scale(Vector3Normalize(ray.direction), 60)); water = false; }
    } else {
        Vector3 flat = Vector3Normalize({ray.direction.x, 0, ray.direction.z});
        hit = Vector3Add(ray.position, Vector3Scale(flat, 40));
    }
    if (deckOut) *deckOut = g.boat.ToDeck({hit.x, hit.z});
    return water;
}
bool AimAtDeck(const Gannet& g, const Camera3D& cam, Vector2 screen, int deck, Vector2* deckOut) {
    // the ray into the boat's own frame, then onto the plank (or the engine room's floor)
    Ray ray = GetScreenToWorldRayEx(screen, cam, SCREEN_W, SCREEN_H);
    Matrix inv = MatrixInvert(BoatMatrix(g.boat));
    Vector3 o = Vector3Transform(ray.position, inv), e = Vector3Transform(Vector3Add(ray.position, ray.direction), inv);
    Vector3 d = Vector3Subtract(e, o);
    float y = deck == 1 ? ENGINE_Y : DECK_Y;
    if (d.y > -0.01f) return false;
    float t = (y - o.y) / d.y;
    if (t > 30) return false;
    if (deckOut) *deckOut = {o.x + d.x * t, o.z + d.z * t};
    return true;
}
bool DeckPointOnScreen(const Gannet& g, Vector2 deck, float up, const Camera3D& cam, Vector2* out) {
    Vector3 p = BoatPoint(g.boat, {deck.x, DECK_Y + up, deck.y});
    Vector3 fw = Vector3Subtract(cam.target, cam.position);
    if (Vector3DotProduct(Vector3Subtract(p, cam.position), fw) <= 0.1f || Vector3Distance(p, cam.position) > 40) return false;
    *out = GetWorldToScreenEx(p, cam, SCREEN_W, SCREEN_H);
    return out->x > 0 && out->y > 0 && out->x < SCREEN_W && out->y < SCREEN_H;
}
bool CrewHeadOnScreen(const Gannet& g, int ci, const Camera3D& cam, Vector2* out) {
    const Crew& c = g.crew[ci];
    Vector3 p;
    if (c.overboard) p = W3(c.swim, 0.8f);
    else { Vector2 sp = StandSpot(c); p = BoatPoint(g.boat, {sp.x, (c.deck == 1 ? ENGINE_Y : DECK_Y) + 2.05f, sp.y}); }
    Vector3 fw = Vector3Subtract(cam.target, cam.position);
    if (Vector3DotProduct(Vector3Subtract(p, cam.position), fw) <= 0.1f || Vector3Distance(p, cam.position) > 40) return false;
    *out = GetWorldToScreenEx(p, cam, SCREEN_W, SCREEN_H);
    return out->x > 0 && out->y > 0 && out->x < SCREEN_W && out->y < SCREEN_H;
}

// ---------------------------------------------------------------- the models
static float HalfBeam3(float x) { return x > 5 ? std::max(0.5f, 3.0f - (x - 5) * 0.42f) : x < -10 ? 3.0f - (-10 - x) * 0.8f : 3.0f; }
static float KeelY(float x) { return x > 5 ? -1.9f + (x - 5) / 6.0f * 2.3f : -1.9f; }
static Color Mul(Color c, float k) { return {(unsigned char)std::clamp(c.r * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.g * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.b * k, 0.0f, 255.0f), c.a}; }

static Model gBoat{}, gQuay{}, gFish{}, gJelly{}, gGull{}, gSea{}, gLand{};
static bool gReady = false, gSeaReady = false;
static const void* gLandFor = nullptr;
static const int SN = 64;          // the sea's grid: SN x SN cells of SC metres around the eye
static const float SC = 2.0f;

static void WallX(MeshBuilder& mb, float x, float z0, float z1, float y0, float y1, Color c) { mb.Box({x, (y0 + y1) / 2, (z0 + z1) / 2}, {0.05f, (y1 - y0) / 2, (z1 - z0) / 2}, c); }
static void WallZ(MeshBuilder& mb, float z, float x0, float x1, float y0, float y1, Color c) { mb.Box({(x0 + x1) / 2, (y0 + y1) / 2, z}, {(x1 - x0) / 2, (y1 - y0) / 2, 0.05f}, c); }

static void BuildBoat(MeshBuilder& mb) {
    Color hullC{80, 30, 26, 255}, hullLow{44, 26, 22, 255}, boot{150, 140, 118, 255}, inner{104, 80, 58, 255}, cap{150, 118, 78, 255};
    Color plank{118, 88, 58, 255}, plankDk{98, 72, 46, 255}, iron{70, 74, 76, 255}, brass{200, 160, 70, 255}, wood{98, 76, 58, 255};
    const float X0 = -11.0f, X1 = 10.8f, TOP = DECK_Y + 0.85f;
    // the hull: five levels per section (keel, chine, waterline, deck edge, bulwark top), quads between sections
    auto P = [&](float x, float s, int k) -> Vector3 {
        float hb = HalfBeam3(x);
        switch (k) {
            case 0: return {x, KeelY(x), 0};
            case 1: return {x, std::max(-1.0f, KeelY(x) + 0.4f), s * hb * 0.78f};
            case 2: return {x, 0.0f, s * hb * 0.97f};
            case 3: return {x, DECK_Y, s * hb};
            default: return {x, TOP, s * hb};
        }
    };
    for (float x = X0; x < X1 - 0.01f; x += 0.5f) {
        float x2 = std::min(X1, x + 0.5f);
        for (int s = -1; s <= 1; s += 2)
            for (int k = 0; k < 4; k++) {
                Color c = k < 2 ? hullLow : k == 2 ? hullC : hullC;
                mb.Quad(P(x, (float)s, k), P(x2, (float)s, k), P(x2, (float)s, k + 1), P(x, (float)s, k + 1), c);
                if (k == 2) {   // the boot-top stripe along the waterline
                    Vector3 a = P(x, (float)s, 2), b = P(x2, (float)s, 2);
                    Vector3 o{0, 0, s * 0.02f};
                    mb.Quad(Vector3Add(a, o), Vector3Add(b, o), Vector3Add(Vector3Add(b, o), {0, 0.14f, 0}), Vector3Add(Vector3Add(a, o), {0, 0.14f, 0}), boot);
                }
            }
        // the bulwark's inner face and the rail's cap
        for (int s = -1; s <= 1; s += 2) {
            float hb = HalfBeam3(x), hb2 = HalfBeam3(x2);
            mb.Quad({x, DECK_Y, s * (hb - 0.08f)}, {x2, DECK_Y, s * (hb2 - 0.08f)}, {x2, TOP, s * (hb2 - 0.08f)}, {x, TOP, s * (hb - 0.08f)}, inner);
            mb.Quad({x, TOP, s * hb}, {x2, TOP, s * hb2}, {x2, TOP, s * (hb2 - 0.1f)}, {x, TOP, s * (hb - 0.1f)}, cap);
        }
    }
    // the stem at the bow and the transom at the stern
    for (int k = 0; k < 4; k++) {
        Vector3 a = P(X1, -1, k), b = P(X1, 1, k), a2 = P(X1, -1, k + 1), b2 = P(X1, 1, k + 1);
        Vector3 stem{X1 + 0.45f, (a.y + a2.y) / 2 + 0.1f, 0};
        mb.Tri(a, a2, stem, hullC); mb.Tri(b, b2, stem, hullC);
    }
    for (int k = 0; k < 4; k++) mb.Quad(P(X0, -1, k), P(X0, 1, k), P(X0, 1, k + 1), P(X0, -1, k + 1), k < 2 ? hullLow : hullC);
    // the deck, planked fore and aft, with the engine room's hatch open to its ladder
    for (float x = X0 + 0.1f; x < X1 - 0.25f; x += 0.5f) {
        float dhb = HalfBeam3(x + 0.25f) - 0.08f;
        int row = 0;
        for (float y = -dhb; y < dhb - 0.01f; y += 0.25f, row++) {
            float y2 = std::min(dhb, y + 0.25f), cx = x + 0.25f, cy = (y + y2) / 2;
            if (cx > -3.9f && cx < -2.9f && cy > -2.2f && cy < -1.4f) continue;   // the hatch
            bool seam = ((int)((x + 11) * 2) + row * 3) % 7 == 0;
            Color c = row % 2 ? plank : plankDk;
            if (seam) c = Mul(c, 0.82f);
            mb.Quad({x, DECK_Y, y}, {x + 0.5f, DECK_Y, y}, {x + 0.5f, DECK_Y, y2}, {x, DECK_Y, y2}, c);
        }
    }
    // the hatch's coaming and its ladder down
    mb.Box({-3.4f, DECK_Y + 0.08f, -2.25f}, {0.5f, 0.08f, 0.04f}, iron); mb.Box({-3.4f, DECK_Y + 0.08f, -1.35f}, {0.5f, 0.08f, 0.04f}, iron);
    mb.Box({-3.93f, DECK_Y + 0.08f, -1.8f}, {0.04f, 0.08f, 0.45f}, iron); mb.Box({-2.87f, DECK_Y + 0.08f, -1.8f}, {0.04f, 0.08f, 0.45f}, iron);
    for (int s = -1; s <= 1; s += 2) mb.Box({-3.4f + s * 0.25f, (ENGINE_Y + DECK_Y) / 2, -2.05f}, {0.03f, (DECK_Y - ENGINE_Y) / 2, 0.03f}, brass);
    for (float y = ENGINE_Y + 0.3f; y < DECK_Y; y += 0.3f) mb.Box({-3.4f, y, -2.05f}, {0.25f, 0.02f, 0.03f}, brass);
    // the wheelhouse: walls to the window band, posts, the band above, the roof; its door amidships aft
    float wy0 = DECK_Y, wy1 = DECK_Y + 1.05f, wy2 = DECK_Y + 1.75f, wy3 = DECK_Y + 2.2f;
    for (int s = -1; s <= 1; s += 2) {
        WallZ(mb, s * 2.1f, 0.9f, 5.1f, wy0, wy1, wood); WallZ(mb, s * 2.1f, 0.9f, 5.1f, wy2, wy3, wood);
        for (float x = 0.9f; x <= 5.11f; x += 1.05f) mb.Box({x, (wy1 + wy2) / 2, s * 2.1f}, {0.05f, (wy2 - wy1) / 2, 0.06f}, wood);
    }
    WallX(mb, 5.1f, -2.1f, 2.1f, wy0, wy1, wood); WallX(mb, 5.1f, -2.1f, 2.1f, wy2, wy3, wood);
    for (float z = -2.1f; z <= 2.11f; z += 1.05f) mb.Box({5.1f, (wy1 + wy2) / 2, z}, {0.06f, (wy2 - wy1) / 2, 0.05f}, wood);
    WallX(mb, 0.9f, -2.1f, -0.7f, wy0, wy3, wood); WallX(mb, 0.9f, 0.7f, 2.1f, wy0, wy3, wood); WallX(mb, 0.9f, -0.7f, 0.7f, DECK_Y + 1.9f, wy3, wood);
    mb.Box({3.0f, wy3 + 0.05f, 0}, {2.25f, 0.06f, 2.25f}, Color{60, 64, 62, 255});
    // inside: the binnacle and the wheel, the sonar, the printer, the clock
    mb.Box({4.55f, DECK_Y + 0.5f, 0}, {0.16f, 0.5f, 0.22f}, wood);
    {
        std::vector<Vector3> ring;
        for (int k = 0; k <= 16; k++) { float a = k * 6.2832f / 16; ring.push_back({4.36f, DECK_Y + 1.2f + sinf(a) * 0.42f, cosf(a) * 0.42f}); }
        mb.Tube(ring, 0.03f, 0.03f, 5, brass, brass, 0);
        for (int k = 0; k < 4; k++) { float a = k * 0.785f; mb.Tube({{4.36f, DECK_Y + 1.2f - sinf(a) * 0.5f, -cosf(a) * 0.5f}, {4.36f, DECK_Y + 1.2f + sinf(a) * 0.5f, cosf(a) * 0.5f}}, 0.022f, 0.022f, 4, brass, brass, 0); }
    }
    mb.Box({2.4f, DECK_Y + 0.45f, -1.6f}, {0.45f, 0.45f, 0.3f}, Color{40, 52, 46, 255});
    mb.Box({2.4f, DECK_Y + 0.45f, 1.6f}, {0.4f, 0.45f, 0.28f}, brass);
    mb.Box({4.0f, DECK_Y + 1.55f, -2.02f}, {0.2f, 0.2f, 0.03f}, Color{190, 180, 160, 255});
    // the funnel aft of the wheelhouse, the lantern mast, the stern gantry, the net drum
    mb.Tube({{0.6f, DECK_Y, 1.0f}, {0.6f, DECK_Y + 3.4f, 1.0f}}, 0.33f, 0.33f, 10, Color{34, 32, 32, 255}, Color{34, 32, 32, 255}, 0);
    mb.Tube({{0.6f, DECK_Y + 2.8f, 1.0f}, {0.6f, DECK_Y + 3.05f, 1.0f}}, 0.345f, 0.345f, 10, Color{150, 40, 32, 255}, Color{150, 40, 32, 255}, 0);
    mb.Tube({{0.2f, DECK_Y, 0}, {0.2f, DECK_Y + 5.9f, 0}}, 0.09f, 0.06f, 6, iron, iron, 0);
    mb.Box({0.2f, DECK_Y + 5.0f, 0}, {0.05f, 0.05f, 1.3f}, iron);
    mb.Box({0.2f, DECK_Y + 5.6f, 0}, {0.2f, 0.06f, 0.2f}, iron);
    for (int s = -1; s <= 1; s += 2) mb.Tube({{-10.6f, DECK_Y, s * 2.1f}, {-10.6f, DECK_Y + 3.3f, s * 1.5f}}, 0.1f, 0.08f, 6, iron, iron, 0);
    mb.Tube({{-10.6f, DECK_Y + 3.3f, -1.5f}, {-10.6f, DECK_Y + 3.3f, 1.5f}}, 0.09f, 0.09f, 6, iron, iron, 0);
    mb.Tube({{-9.4f, DECK_Y + 0.75f, -1.2f}, {-9.4f, DECK_Y + 0.75f, 1.2f}}, 0.55f, 0.55f, 12, Color{84, 72, 58, 255}, Color{84, 72, 58, 255}, 0);
    for (int s = -1; s <= 1; s += 2) mb.Box({-9.4f, DECK_Y + 0.75f, s * 1.25f}, {0.7f, 0.7f, 0.04f}, iron);
    // the hold hatch, the gutting table, the locker, the air pump, the bell, the harpoon cannon, the rod holders
    mb.Box({-2.2f, DECK_Y + 0.12f, -0.9f}, {0.8f, 0.12f, 0.6f}, Color{60, 48, 34, 255});
    mb.Box({-2.2f, DECK_Y + 0.26f, -0.9f}, {0.66f, 0.03f, 0.46f}, Color{80, 64, 46, 255});
    mb.Box({-2.2f, DECK_Y + 0.9f, 2.2f}, {0.7f, 0.04f, 0.35f}, Color{150, 150, 140, 255});
    for (int sx = -1; sx <= 1; sx += 2) for (int sz = -1; sz <= 1; sz += 2) mb.Box({-2.2f + sx * 0.6f, DECK_Y + 0.44f, 2.2f + sz * 0.28f}, {0.03f, 0.44f, 0.03f}, iron);
    mb.Box({-6.4f, DECK_Y + 0.4f, -2.35f}, {0.5f, 0.4f, 0.25f}, wood);
    mb.Box({-6.2f, DECK_Y + 0.45f, 2.5f}, {0.3f, 0.45f, 0.2f}, brass);
    mb.Tube({{6.0f, DECK_Y, 2.05f}, {6.0f, DECK_Y + 1.6f, 2.05f}}, 0.04f, 0.04f, 5, iron, iron, 0);
    mb.Cone({6.0f, DECK_Y + 1.15f, 2.05f}, {6.0f, DECK_Y + 1.5f, 2.05f}, 0.18f, 10, brass);
    mb.Tube({{9.6f, DECK_Y, 0}, {9.6f, DECK_Y + 0.9f, 0}}, 0.18f, 0.14f, 8, iron, iron, 0);
    mb.Tube({{9.3f, DECK_Y + 1.0f, 0}, {10.6f, DECK_Y + 1.06f, 0}}, 0.09f, 0.07f, 8, Color{44, 48, 50, 255}, Color{44, 48, 50, 255}, 0);
    for (const auto& sd : Stations())
        if (sd.kind == StationKind::PortRod || sd.kind == StationKind::StarRod || sd.kind == StationKind::SternRodP || sd.kind == StationKind::SternRodS)
            mb.Tube({{sd.at.x, DECK_Y, sd.at.y}, {sd.at.x, DECK_Y + 0.75f, sd.at.y}}, 0.04f, 0.04f, 5, Color{40, 36, 30, 255}, Color{40, 36, 30, 255}, 0);
    // the engine room below: its floor, bulkheads, the boiler along the port side, the coal bunker, the pump
    Color bulk{62, 56, 50, 255};
    mb.Box({-5.65f, ENGINE_Y - 0.05f, 0}, {2.9f, 0.05f, 2.35f}, Color{58, 52, 46, 255});
    WallX(mb, -2.75f, -2.35f, 2.35f, ENGINE_Y, DECK_Y, bulk); WallX(mb, -8.55f, -2.35f, 2.35f, ENGINE_Y, DECK_Y, bulk);
    WallZ(mb, -2.35f, -8.55f, -2.75f, ENGINE_Y, DECK_Y, bulk); WallZ(mb, 2.35f, -8.55f, -2.75f, ENGINE_Y, DECK_Y, bulk);
    mb.Tube({{-5.8f, ENGINE_Y + 0.75f, -1.55f}, {-3.8f, ENGINE_Y + 0.75f, -1.55f}}, 0.68f, 0.68f, 12, Color{88, 80, 74, 255}, Color{88, 80, 74, 255}, 0);
    mb.Box({-7.4f, ENGINE_Y + 0.6f, -1.3f}, {0.7f, 0.6f, 0.8f}, Color{34, 30, 28, 255});
    mb.Box({-7.0f, ENGINE_Y + 0.5f, 1.9f}, {0.3f, 0.5f, 0.3f}, Color{90, 110, 120, 255});
    mb.Box({-6.3f, ENGINE_Y + 0.45f, 0.8f}, {1.0f, 0.45f, 0.5f}, Color{76, 70, 64, 255});   // the engine
}

static void BuildQuay(MeshBuilder& mb) {
    Color stone{74, 72, 68, 255}, top{96, 92, 84, 255}, wood{100, 76, 52, 255}, roof{58, 44, 40, 255};
    // the quay beside her port side (the moored frame: x along her, z toward her starboard; the quay is at z < -3.9)
    mb.Box({0, QUAY_Y - 2.0f, -9.75f}, {15.0f, 2.0f, 5.85f}, stone);
    mb.Box({0, QUAY_Y - 0.03f, -9.75f}, {15.0f, 0.03f, 5.85f}, top);
    mb.Box({0, QUAY_Y - 0.05f, -3.15f}, {1.1f, 0.04f, 0.8f}, wood);          // the gangplank
    for (float x : {-11.0f, -4.0f, 4.0f, 11.0f}) mb.Tube({{x, QUAY_Y, -4.2f}, {x, QUAY_Y + 0.5f, -4.2f}}, 0.16f, 0.12f, 8, Color{40, 40, 42, 255}, Color{40, 40, 42, 255}, 0);   // bollards
    for (float x : {-9.0f, -1.0f, 7.0f, 13.0f}) mb.Tube({{x, QUAY_Y, -6.8f}, {x, QUAY_Y + 3.2f, -6.8f}}, 0.07f, 0.05f, 6, Color{40, 40, 42, 255}, Color{40, 40, 42, 255}, 0);   // lamp posts
    // the sheds behind the stations: the Chandler, the market, the office, the slipway's hut; the chalkboard
    struct Hut { float x, w; Color c; };
    for (Hut h : {Hut{-3.5f, 1.8f, Color{110, 90, 70, 255}}, Hut{3.0f, 2.2f, Color{120, 110, 96, 255}}, Hut{8.0f, 1.6f, Color{90, 96, 104, 255}}, Hut{12.5f, 1.6f, Color{96, 84, 70, 255}}}) {
        mb.Box({h.x, QUAY_Y + 1.4f, -11.2f}, {h.w, 1.4f, 1.4f}, h.c);
        mb.Box({h.x, QUAY_Y + 2.9f, -11.2f}, {h.w + 0.2f, 0.12f, 1.6f}, roof);
        mb.Box({h.x, QUAY_Y + 1.0f, -9.78f}, {0.45f, 1.0f, 0.03f}, Color{50, 36, 28, 255});   // its door
    }
    mb.Box({-8.5f, QUAY_Y + 1.2f, -5.5f}, {0.8f, 0.6f, 0.05f}, Color{24, 30, 26, 255});        // the chalkboard
    for (int s = -1; s <= 1; s += 2) mb.Box({-8.5f + s * 0.75f, QUAY_Y + 0.6f, -5.5f}, {0.04f, 0.6f, 0.04f}, wood);
    mb.Box({3.0f, QUAY_Y + 0.9f, -8.2f}, {0.35f, 0.9f, 0.35f}, Color{150, 130, 80, 255});      // the market's scales
    mb.Box({12.0f, QUAY_Y + 0.05f, -5.2f}, {1.4f, 0.05f, 1.2f}, wood);                          // the slipway's cradle
}

static void BuildFish(MeshBuilder& mb) {
    // a unit fish along +Z (its head at +0.5), pale so the species tint colours it; a forked tail at -0.5
    Color top{190, 190, 190, 255}, belly{250, 250, 250, 255};
    mb.Lathe(1.0f, 10, 8, [](float u) { return 0.13f * sinf(PI * (0.08f + 0.9f * u)); }, [](float u) { return 0.2f * sinf(PI * (0.08f + 0.9f * u)); }, top, belly);
    mb.Tri({0, 0, -0.44f}, {0, 0.2f, -0.66f}, {0, 0.02f, -0.5f}, top);
    mb.Tri({0, 0, -0.44f}, {0, -0.2f, -0.66f}, {0, -0.02f, -0.5f}, top);
    mb.Tri({0, 0.18f, 0.05f}, {0, 0.32f, -0.12f}, {0, 0.17f, -0.2f}, top);   // the dorsal fin
}
static void BuildJelly(MeshBuilder& mb) {
    Color c{230, 235, 250, 255};
    mb.Lathe(0.5f, 6, 10, [](float u) { return 0.5f * sinf(PI * 0.5f * (1 - u) + 0.3f); }, [](float u) { return 0.5f * sinf(PI * 0.5f * (1 - u) + 0.3f); }, c, c, {0, 0, 0}, 0);
    for (int k = 0; k < 6; k++) { float a = k * 1.047f; mb.Tube({{cosf(a) * 0.3f, sinf(a) * 0.3f, -0.2f}, {cosf(a) * 0.25f, sinf(a) * 0.25f, -1.1f}}, 0.02f, 0.01f, 3, c, c, 0); }
}
static void BuildGull(MeshBuilder& mb) {
    Color w{240, 240, 236, 255}, g{150, 156, 160, 255};
    mb.Lathe(0.45f, 5, 6, [](float u) { return 0.06f * sinf(PI * (0.1f + 0.8f * u)); }, [](float u) { return 0.06f * sinf(PI * (0.1f + 0.8f * u)); }, w, w);
    mb.Tri({0, 0, 0.05f}, {0.55f, 0.08f, -0.05f}, {0, 0, -0.1f}, g);
    mb.Tri({0, 0, 0.05f}, {-0.55f, 0.08f, -0.05f}, {0, 0, -0.1f}, g);
}

static void EnsureModels() {
    if (gReady || !IsWindowReady()) return;
    { MeshBuilder mb; BuildBoat(mb); gBoat = LoadModelFromMesh(mb.Build()); }
    { MeshBuilder mb; BuildQuay(mb); gQuay = LoadModelFromMesh(mb.Build()); }
    { MeshBuilder mb; BuildFish(mb); gFish = LoadModelFromMesh(mb.Build()); }
    { MeshBuilder mb; BuildJelly(mb); gJelly = LoadModelFromMesh(mb.Build()); }
    { MeshBuilder mb; BuildGull(mb); gGull = LoadModelFromMesh(mb.Build()); }
    gReady = true;
}

// the sea: a grid that follows the eye, its heights refreshed every frame from the swell
static void EnsureSea() {
    if (gSeaReady || !IsWindowReady()) return;
    Mesh m{};
    m.vertexCount = SN * SN * 6; m.triangleCount = SN * SN * 2;
    m.vertices = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
    m.texcoords = (float*)MemAlloc(m.vertexCount * 2 * sizeof(float));
    m.colors = (unsigned char*)MemAlloc(m.vertexCount * 4);
    for (int i = 0; i < m.vertexCount; i++) { m.colors[i * 4] = 40; m.colors[i * 4 + 1] = 84; m.colors[i * 4 + 2] = 96; m.colors[i * 4 + 3] = 205; }
    UploadMesh(&m, true);
    gSea = LoadModelFromMesh(m);
    gSeaReady = true;
}
static void UpdateSea(const Sea& sea, Vector3 eye) {
    float cx = floorf(eye.x / SC) * SC - SN * SC / 2, cz = floorf(eye.z / SC) * SC - SN * SC / 2;
    static std::vector<float> H;
    H.resize((SN + 1) * (SN + 1));
    for (int j = 0; j <= SN; j++) for (int i = 0; i <= SN; i++) H[j * (SN + 1) + i] = sea.Height(cx + i * SC, cz + j * SC);
    Mesh& m = gSea.meshes[0];
    float* v = m.vertices;
    int k = 0;
    auto put = [&](int i, int j) { v[k++] = cx + i * SC; v[k++] = H[j * (SN + 1) + i] - 0.02f; v[k++] = cz + j * SC; };
    for (int j = 0; j < SN; j++) for (int i = 0; i < SN; i++) { put(i, j); put(i + 1, j); put(i + 1, j + 1); put(i, j); put(i + 1, j + 1); put(i, j + 1); }
    UpdateMeshBuffer(m, 0, v, m.vertexCount * 3 * sizeof(float), 0);
}
// the land in the chart (the atoll, the reef's dry crest): built once per ground from its cells
static void EnsureLand(const Eco* e) {
    if (!e || !e->g || gLandFor == (const void*)e->g) return;
    if (gLandFor) UnloadModel(gLand);
    MeshBuilder mb;
    Color sand{150, 132, 96, 255}, sandDk{110, 96, 70, 255}, palm{50, 80, 44, 255};
    for (int y = 0; y < e->n; y++) for (int x = 0; x < e->n; x++) {
        float d = e->depth[(size_t)y * e->n + x];
        if (d > 0.01f) continue;
        float h = 0.7f + 0.35f * sinf(x * 1.7f + y * 2.3f) * sinf(x * 0.9f);
        mb.Box({(x + 0.5f) * e->cell, h / 2 - 0.4f, (y + 0.5f) * e->cell}, {e->cell / 2, h / 2 + 0.4f, e->cell / 2}, ((x + y) % 3) ? sand : sandDk);
        if (((x * 7 + y * 13) % 23) == 0) mb.Tube({{(x + 0.5f) * e->cell, h, (y + 0.5f) * e->cell}, {(x + 0.7f) * e->cell, h + 4.5f, (y + 0.4f) * e->cell}}, 0.12f, 0.08f, 5, Color{90, 70, 50, 255}, Color{90, 70, 50, 255}, 0);
        if (((x * 7 + y * 13) % 23) == 0) mb.Octa({(x + 0.7f) * e->cell, h + 4.6f, (y + 0.4f) * e->cell}, 1.1f, palm);
    }
    gLand = LoadModelFromMesh(mb.Build());
    gLandFor = (const void*)e->g;
}



// ---------------------------------------------------------------- drawing helpers
static Matrix Frame(Vector3 at, float yaw) { return MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(at.x, at.y, at.z)); }
static void Part(const Matrix& frame, Vector3 c, Vector3 size, Color col) { rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(size.x, size.y, size.z), MatrixTranslate(c.x, c.y, c.z)), frame), col); }
static void Glow(Vector3 c, float s, Color col, float glow) { rt::DrawCubeGlow(MatrixMultiply(MatrixScale(s, s, s), MatrixTranslate(c.x, c.y, c.z)), col, glow); }
static void Seg(Vector3 a, Vector3 b, float th, Color col, float glow = 0) {
    Vector3 d = Vector3Subtract(b, a);
    float L = Vector3Length(d);
    if (L < 1e-4f) return;
    Vector3 z = Vector3Scale(d, 1 / L);
    Vector3 up = fabsf(z.y) > 0.9f ? Vector3{1, 0, 0} : Vector3{0, 1, 0};
    Vector3 x = Vector3Normalize(Vector3CrossProduct(up, z)), y = Vector3CrossProduct(z, x);
    Matrix m = {x.x * th, y.x * th, z.x * L, (a.x + b.x) / 2,
                x.y * th, y.y * th, z.y * L, (a.y + b.y) / 2,
                x.z * th, y.z * th, z.z * L, (a.z + b.z) / 2,
                0, 0, 0, 1};
    if (glow > 0) rt::DrawCubeGlow(m, col, glow); else rt::DrawCubeM(m, col);
}
static void DrawFishAt(const Model& m, Vector3 p, Vector3 heading, float len, Color c, float roll = 0) {
    float yaw = atan2f(heading.x, heading.z), pitch = atan2f(heading.y, sqrtf(heading.x * heading.x + heading.z * heading.z));
    Matrix w = MatrixMultiply(MatrixScale(len, len, len), MatrixRotateZ(roll));
    w = MatrixMultiply(w, MatrixRotateX(-pitch));
    w = MatrixMultiply(w, MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(p.x, p.y, p.z)));
    rt::DrawStatic(m, w, c);
}
static Color SpeciesTint(const SpeciesRec& r) {
    Color base = r.cls == "jelly" ? Color{190, 200, 230, 255} : r.cls == "reptile" ? Color{80, 110, 70, 255} : r.cls == "invert" ? Color{170, 110, 90, 255}
               : r.tier == 1 ? Color{170, 190, 200, 255} : r.tier >= 4 ? Color{70, 80, 90, 255} : Color{120, 140, 130, 255};
    if (r.name == "mahi-mahi") base = Color{150, 190, 80, 255};
    if (r.name == "parrotfish") base = Color{90, 170, 160, 255};
    if (r.name == "snapper") base = Color{190, 100, 90, 255};
    if (r.tier == 1 && r.cls == "fish") base = Color{205, 220, 228, 255};
    return base;
}
static float H01(int a, int b, int c) { uint32_t h = (uint32_t)a * 73856093u ^ (uint32_t)b * 19349663u ^ (uint32_t)c * 83492791u; h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15; return (h & 0xffffff) / 16777216.0f; }

// ---------------------------------------------------------------- the crew's figures
// A hand in oilskins (the coat in the role's colour), sea boots and a sou'wester, built in the figure's frame (x
// forward, y up, z to its right) as a body, two arms and two legs that swing from the shoulders and hips.
static Color CoatOf(Role r) { return r == Role::Bosun ? Color{56, 74, 110, 255} : r == Role::Angler ? Color{214, 180, 50, 255} : r == Role::Diver ? Color{120, 132, 92, 255} : Color{224, 222, 210, 255}; }
// a lathe (built along +Z) appended standing up: +Z becomes +Y
static void LatheUp(MeshBuilder& dst, float len, int rings, int segs, float (*rx)(float), float (*ry)(float), Color top, Color under, Vector3 at) {
    MeshBuilder tmp;
    tmp.Lathe(len, rings, segs, rx, ry, top, under);
    for (size_t i = 0; i + 2 < tmp.pos.size(); i += 3) { dst.pos.push_back(tmp.pos[i] + at.x); dst.pos.push_back(tmp.pos[i + 2] + at.y); dst.pos.push_back(-tmp.pos[i + 1] + at.z); }
    dst.uv.insert(dst.uv.end(), tmp.uv.begin(), tmp.uv.end());
    dst.col.insert(dst.col.end(), tmp.col.begin(), tmp.col.end());
}
static Model gBody[(int)Role::COUNT]{}, gArm[(int)Role::COUNT]{}, gLeg{}, gStars{}, gMoon{}, gRain{}, gItem[(int)Item::COUNT]{};
static bool gCrewReady = false;
static void BuildBody(MeshBuilder& mb, Color coat) {
    Color skin{214, 172, 132, 255}, hat{212, 178, 62, 255}, dark{34, 28, 24, 255}, coatDk = Mul(coat, 0.75f);
    LatheUp(mb, 0.62f, 6, 10, [](float u) { return 0.16f + 0.04f * u; }, [](float u) { return 0.24f + 0.04f * u; }, coat, coatDk, {0, 1.13f, 0});     // the coat
    LatheUp(mb, 0.3f, 3, 10, [](float u) { return 0.2f + 0.03f * u; }, [](float u) { return 0.28f + 0.03f * u; }, coatDk, coatDk, {0, 0.74f, 0});  // its skirt
    for (int k = 0; k < 4; k++) mb.Box({0.19f, 1.36f - k * 0.14f, 0}, {0.012f, 0.018f, 0.018f}, dark);                                              // the toggles
    LatheUp(mb, 0.1f, 2, 8, [](float) { return 0.07f; }, [](float) { return 0.07f; }, skin, skin, {0, 1.46f, 0});                                // the neck
    LatheUp(mb, 0.3f, 6, 10, [](float u) { return 0.115f * sinf(PI * (0.12f + 0.76f * u)) + 0.015f; }, [](float u) { return 0.11f * sinf(PI * (0.12f + 0.76f * u)) + 0.015f; }, skin, skin, {0, 1.63f, 0});   // the head
    mb.Box({0.13f, 1.61f, 0}, {0.03f, 0.035f, 0.022f}, Mul(skin, 0.85f));                                                                         // the nose
    for (int s = -1; s <= 1; s += 2) mb.Octa({0.118f, 1.665f, s * 0.045f}, 0.018f, dark);                                                         // the eyes
    LatheUp(mb, 0.16f, 3, 10, [](float u) { return 0.14f - 0.02f * (1 - u); }, [](float u) { return 0.14f - 0.02f * (1 - u); }, hat, Mul(hat, 0.8f), {0, 1.81f, 0});   // the sou'wester's crown
    mb.Box({-0.05f, 1.745f, 0}, {0.25f, 0.014f, 0.21f}, hat);                                                                                     // its brim, long at the back
    for (int s = -1; s <= 1; s += 2) mb.Box({0, 1.62f, s * 0.13f}, {0.02f, 0.1f, 0.01f}, Mul(hat, 0.8f));                                          // the ties
}
static void BuildArm(MeshBuilder& mb, Color coat) {
    Color skin{214, 172, 132, 255};
    mb.Tube({{0, 0, 0}, {0.02f, -0.3f, 0}, {0.07f, -0.56f, 0}}, 0.085f, 0.07f, 6, coat, Mul(coat, 0.85f), 0);
    mb.Octa({0.08f, -0.63f, 0}, 0.06f, skin);
}
static void BuildLeg(MeshBuilder& mb) {
    Color trousers{50, 48, 46, 255}, boot{30, 28, 26, 255};
    mb.Tube({{0, 0, 0}, {0.03f, -0.42f, 0}, {0, -0.55f, 0}}, 0.105f, 0.09f, 6, trousers, trousers, 0);
    mb.Tube({{0, -0.5f, 0}, {0, -0.8f, 0}}, 0.1f, 0.1f, 6, boot, boot, 0);
    mb.Box({0.05f, -0.83f, 0}, {0.15f, 0.055f, 0.085f}, boot);
}
// what a hand holds, built along +X (seen at the bottom right of your view, and in other hands' grip)
static void BuildItem(MeshBuilder& mb, Item it) {
    Color wood{120, 86, 54, 255}, iron{70, 74, 78, 255}, brass{200, 160, 70, 255}, red{200, 60, 44, 255}, white{230, 228, 220, 255};
    switch (it) {
        case Item::Gaff: mb.Tube({{-0.35f, 0, 0}, {0.6f, 0, 0}}, 0.02f, 0.02f, 5, wood, wood, 0); mb.Tube({{0.6f, 0, 0}, {0.7f, -0.05f, 0}, {0.66f, -0.13f, 0}}, 0.012f, 0.008f, 4, iron, iron, 0); break;
        case Item::Priest: mb.Tube({{-0.12f, 0, 0}, {0.3f, 0.01f, 0}}, 0.022f, 0.045f, 6, wood, wood, 0); break;
        case Item::Knife: mb.Box({0, 0, 0}, {0.06f, 0.016f, 0.014f}, wood); mb.Box({0.13f, 0.005f, 0}, {0.08f, 0.012f, 0.003f}, Color{190, 196, 200, 255}); break;
        case Item::Speargun: mb.Tube({{-0.2f, 0, 0}, {0.55f, 0, 0}}, 0.025f, 0.02f, 6, iron, iron, 0); mb.Tube({{0.0f, 0.03f, 0}, {0.75f, 0.03f, 0}}, 0.008f, 0.008f, 4, white, white, 0); mb.Box({-0.05f, -0.06f, 0}, {0.03f, 0.06f, 0.02f}, wood); break;
        case Item::Flare: mb.Box({0, 0, 0}, {0.09f, 0.04f, 0.025f}, Color{220, 110, 40, 255}); mb.Tube({{0.06f, 0.01f, 0}, {0.2f, 0.01f, 0}}, 0.025f, 0.025f, 6, Color{220, 110, 40, 255}, Color{220, 110, 40, 255}, 0); mb.Box({-0.04f, -0.06f, 0}, {0.025f, 0.05f, 0.02f}, Color{60, 40, 30, 255}); break;
        case Item::Rifle: mb.Box({-0.15f, -0.02f, 0}, {0.2f, 0.035f, 0.025f}, wood); mb.Tube({{0.0f, 0.01f, 0}, {0.6f, 0.01f, 0}}, 0.012f, 0.01f, 5, iron, iron, 0); mb.Box({0.05f, 0.025f, 0}, {0.06f, 0.012f, 0.01f}, iron); break;
        case Item::Shotgun: mb.Box({-0.15f, -0.02f, 0}, {0.2f, 0.04f, 0.028f}, wood); for (int s = -1; s <= 1; s += 2) mb.Tube({{0.0f, 0.01f, s * 0.012f}, {0.5f, 0.01f, s * 0.012f}}, 0.013f, 0.013f, 5, iron, iron, 0); break;
        case Item::Charge: mb.Tube({{-0.1f, 0, 0}, {0.2f, 0, 0}}, 0.08f, 0.08f, 8, Color{60, 70, 54, 255}, Color{60, 70, 54, 255}, 0); mb.Box({0.21f, 0, 0}, {0.01f, 0.03f, 0.03f}, brass); break;
        case Item::Ring: for (int k = 0; k < 10; k++) { float a0 = k * 0.628f, a1 = a0 + 0.628f; mb.Tube({{0.15f + cosf(a0) * 0.16f, sinf(a0) * 0.16f, 0}, {0.15f + cosf(a1) * 0.16f, sinf(a1) * 0.16f, 0}}, 0.035f, 0.035f, 5, k % 2 ? white : red, k % 2 ? white : red, 0); } break;
        case Item::Bandage: mb.Tube({{0, 0, -0.04f}, {0, 0, 0.04f}}, 0.05f, 0.05f, 8, white, white, 0); break;
        case Item::Longline: for (int k = 0; k < 10; k++) { float a0 = k * 0.628f, a1 = a0 + 0.628f; mb.Tube({{0.1f + cosf(a0) * 0.12f, sinf(a0) * 0.12f, 0}, {0.1f + cosf(a1) * 0.12f, sinf(a1) * 0.12f, 0}}, 0.03f, 0.03f, 4, Color{150, 140, 110, 255}, Color{150, 140, 110, 255}, 0); } break;
        case Item::Pot: mb.Box({0.15f, 0, 0}, {0.16f, 0.1f, 0.12f}, Color{100, 84, 60, 255}); break;
        default: mb.Box({0, 0, 0}, {0.01f, 0.01f, 0.01f}, iron); break;
    }
}
static void EnsureCrewModels() {
    if (gCrewReady || !IsWindowReady()) return;
    for (int r = 0; r < (int)Role::COUNT; r++) {
        { MeshBuilder mb; BuildBody(mb, CoatOf((Role)r)); gBody[r] = LoadModelFromMesh(mb.Build()); }
        { MeshBuilder mb; BuildArm(mb, CoatOf((Role)r)); gArm[r] = LoadModelFromMesh(mb.Build()); }
    }
    { MeshBuilder mb; BuildLeg(mb); gLeg = LoadModelFromMesh(mb.Build()); }
    for (int i = 1; i < (int)Item::COUNT; i++) { MeshBuilder mb; BuildItem(mb, (Item)i); gItem[i] = LoadModelFromMesh(mb.Build()); }
    // the night sky: stars on a dome round the eye, the moon, and a column of rain to stand in
    {
        MeshBuilder mb;
        uint32_t s = 12345;
        auto rnd = [&]() { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0f; };
        for (int k = 0; k < 420; k++) {
            float az = rnd() * 6.2832f, el = asinf(0.04f + 0.96f * rnd()), R = 80, sz = 0.08f + 0.22f * rnd() * rnd();
            Vector3 c{cosf(el) * cosf(az) * R, sinf(el) * R, cosf(el) * sinf(az) * R};
            Vector3 n = Vector3Normalize(c), up = fabsf(n.y) > 0.9f ? Vector3{1, 0, 0} : Vector3{0, 1, 0};
            Vector3 a = Vector3Scale(Vector3Normalize(Vector3CrossProduct(up, n)), sz), b = Vector3Scale(Vector3Normalize(Vector3CrossProduct(n, a)), sz);
            unsigned char lv = (unsigned char)(150 + 105 * rnd());
            Color col{lv, lv, (unsigned char)std::min(255, lv + 20), 255};
            mb.Quad(Vector3Subtract(Vector3Subtract(c, a), b), Vector3Subtract(Vector3Add(c, a), b), Vector3Add(Vector3Add(c, a), b), Vector3Add(Vector3Subtract(c, a), b), col);
        }
        gStars = LoadModelFromMesh(mb.Build());
    }
    {
        MeshBuilder mb;
        Color m1{236, 232, 214, 255}, m2{200, 196, 180, 255};
        mb.Lathe(0.2f, 2, 20, [](float) { return 3.4f; }, [](float) { return 3.4f; }, m1, m2);
        gMoon = LoadModelFromMesh(mb.Build());
    }
    {
        MeshBuilder mb;
        uint32_t s = 777;
        auto rnd = [&]() { s = s * 1664525u + 1013904223u; return (s >> 8) / 16777216.0f; };
        for (int k = 0; k < 700; k++) {
            Vector3 p{rnd() * 24 - 12, rnd() * 12, rnd() * 24 - 12};
            mb.Quad(p, {p.x + 0.012f, p.y, p.z}, {p.x + 0.07f, p.y + 0.55f, p.z + 0.02f}, {p.x + 0.058f, p.y + 0.55f, p.z + 0.02f}, Color{170, 182, 200, 110});
        }
        gRain = LoadModelFromMesh(mb.Build());
    }
    gCrewReady = true;
}

// a hand: lying down when fallen, treading water overboard, pale and adrift when a ghost
static void DrawHand(const Gannet& g, const Crew& c, float t) {
    int r = std::clamp((int)c.role, 0, (int)Role::COUNT - 1);
    Color tint = c.dead ? Color{190, 225, 245, 120} : WHITE;
    auto put = [&](const Model& m, const Matrix& local, const Matrix& frame) { rt::DrawStatic(m, MatrixMultiply(local, frame), tint); };
    Matrix frame;
    if (c.overboard && !c.dead) {
        // treading water: shoulders under, arms working, the head up
        float h = g.sea.Height(c.swim.x, c.swim.y);
        frame = Frame(W3(c.swim, h - 1.45f), sinf(t * 0.7f) * 0.6f);
        put(gBody[r], MatrixIdentity(), frame);
        for (int s = -1; s <= 1; s += 2) put(gArm[r], MatrixMultiply(MatrixMultiply(MatrixRotateZ(1.2f + 0.5f * sinf(t * 4 + s)), MatrixRotateX(s * 0.5f)), MatrixTranslate(0, 1.38f, s * 0.27f)), frame);
        return;
    }
    float yawLocal = -atan2f(c.facing.y, c.facing.x);
    if (OnQuay(g, c)) frame = Frame(W3(g.boat.ToWorld(c.p), QUAY_Y), yawLocal - g.boat.heading);   // (her deck's turn, then her heading)
    else { Vector2 sp = StandSpot(c); frame = MatrixMultiply(Frame({sp.x, (c.deck == 1 ? ENGINE_Y : DECK_Y) + c.z, sp.y}, yawLocal), BoatMatrix(g.boat)); }
    if (c.dead) frame = MatrixMultiply(MatrixTranslate(0, 0.08f + 0.05f * sinf(t * 2 + c.slot), 0), frame);
    if (c.fallen) frame = MatrixMultiply(MatrixMultiply(MatrixRotateZ(1.5f), MatrixTranslate(0, 0.2f, 0)), frame);   // flat on the deck
    bool walking = c.station < 0 && Vector2Length(c.v) > 0.3f && !c.fallen;
    float swing = walking ? sinf(t * 9 + c.slot) * 0.45f : 0;
    put(gBody[r], walking ? MatrixTranslate(0, fabsf(sinf(t * 9 + c.slot)) * 0.03f, 0) : MatrixIdentity(), frame);
    for (int s = -1; s <= 1; s += 2) put(gLeg, MatrixMultiply(MatrixRotateZ(swing * s), MatrixTranslate(0, 0.88f, s * 0.12f)), frame);
    // the arms: forward to the work at a station (the shovel's rhythm at the boiler, the pump's stroke), swinging
    // when walking, hanging otherwise
    float work = c.station >= 0 ? 0.9f + 0.25f * sinf(t * 5 + c.slot) : 0;
    for (int s = -1; s <= 1; s += 2) {
        float a = c.station >= 0 ? work : -swing * s * 1.1f;
        put(gArm[r], MatrixMultiply(MatrixMultiply(MatrixRotateZ(a), MatrixRotateX(s * 0.1f)), MatrixTranslate(0, 1.38f, s * 0.27f)), frame);
    }
    // what's in the right hand
    Item it = c.slots[c.sel].it;
    if (!c.dead && c.station < 0 && it != Item::None && gItem[(int)it].meshCount > 0) {
        Matrix hand = MatrixMultiply(MatrixMultiply(MatrixTranslate(0.08f, -0.63f, 0), MatrixRotateZ(-swing * 1.1f)), MatrixTranslate(0, 1.38f, 0.27f));
        rt::DrawStatic(gItem[(int)it], MatrixMultiply(hand, frame), tint);
    }
}

// ---------------------------------------------------------------- the frame
void DrawTrawl3D(const Gannet& g, const Eco* eco, const Session& sess, int you, const Camera3D& cam, float ghostSee) {
    EnsureModels(); EnsureSea(); EnsureLand(eco);
    if (!gReady || !gSeaReady) return;
    const Boat& b = g.boat;
    const Crew& me = g.crew[you];
    float t = g.time;
    Matrix M = BoatMatrix(b);
    bool below = me.deck == 1 && !me.overboard;
    // ---- the lights: the lantern mast is the key (a downward pool, or the searchlight's cone); deck lamps, the
    // fire below, flares and the quay's lamps are points, the nearest eight
    rt::SceneLight L;
    L.fog = g.sea.weather == Weather::Fog ? Color{34, 38, 42, 255} : Color{5, 8, 13, 255};
    L.fogDensity = g.sea.weather == Weather::Fog ? 0.09f : g.sea.weather == Weather::Rain || g.sea.weather == Weather::Squall ? 0.05f : 0.028f;
    L.fill = {34, 40, 58, 255}; L.rim = {70, 90, 112, 255}; L.key = {255, 226, 170, 255};
    L.surfaceY = 1e5f; L.time = t;
    float lantern = LanternRadius(b.lantern) * (g.sea.weather == Weather::Fog ? 0.6f : 1.0f);
    L.lampPos = BoatPoint(b, {0.2f, DECK_Y + 5.45f, 0});
    if (below) {
        float fire = std::clamp(b.firebox / 6, 0.1f, 1.0f);
        L.lampPos = BoatPoint(b, {-4.8f, ENGINE_Y + 0.5f, -0.7f}); L.lampDir = BoatDir(b, {0, 0.2f, 1});
        L.lampRange = 5 + 3 * fire; L.lampCone = -0.6f; L.key = {255, (unsigned char)(150 + 60 * fire), 90, 255};
    } else if (b.lantern == 3) {
        L.lampDir = Vector3Normalize(BoatDir(b, {cosf(b.searchAim), -0.28f, sinf(b.searchAim)}));
        L.lampRange = 36; L.lampCone = 0.93f;
    } else {
        float h = 5.45f + DECK_Y;
        L.lampDir = {0, -1, 0};
        L.lampRange = sqrtf(lantern * lantern + h * h) * 1.2f;
        L.lampCone = cosf(atanf(lantern / h)) - 0.12f;
    }
    struct PL { Vector3 p; float r; Color c; float k; };
    std::vector<PL> pts;
    pts.push_back({BoatPoint(b, {3.0f, DECK_Y + 1.9f, 0}), 5.5f, {255, 214, 150, 255}, 0.8f});   // the wheelhouse lamp
    pts.push_back({BoatPoint(b, {-10.4f, DECK_Y + 3.0f, 0}), 7.0f, {255, 220, 170, 255}, 0.6f}); // the stern work lamp
    if (b.lantern == 3 && !below) pts.push_back({L.lampPos, 5.0f, {255, 226, 170, 255}, 0.5f});
    if (below) pts.push_back({BoatPoint(b, {-5.6f, DECK_Y - 0.2f, 0.4f}), 4.0f, {200, 190, 170, 255}, 0.35f});
    for (const auto& fl : g.flares) pts.push_back({W3(fl.p, 1.0f), 20.0f, {255, 90, 60, 255}, 1.5f});
    if (g.moored || Vector2Distance(b.pos, g.moorPos) < 80) {
        Matrix Q = MatrixMultiply(MatrixRotateY(-g.moorHeading), MatrixTranslate(g.moorPos.x, 0, g.moorPos.y));
        for (float x : {-9.0f, -1.0f, 7.0f, 13.0f}) pts.push_back({Vector3Transform({x, QUAY_Y + 3.1f, -6.8f}, Q), 9.0f, {255, 205, 140, 255}, 0.9f});
    }
    std::sort(pts.begin(), pts.end(), [&](const PL& a, const PL& c) { return Vector3Distance(a.p, cam.position) - a.r < Vector3Distance(c.p, cam.position) - c.r; });
    for (const auto& p : pts) L.AddPoint(p.p, p.r, p.c, p.k);

    rt::RenderBegin(cam, L);
    EnsureCrewModels();
    // ---- the sky: stars and the moon on a clear night; rain falling round you in wet weather
    Weather wx = g.sea.weather;
    bool clouded = wx == Weather::Fog || wx == Weather::Rain || wx == Weather::Squall || wx == Weather::Storm;
    if (gCrewReady && !below) {
        if (!clouded) {
            rt::DrawSky(gStars, MatrixTranslate(cam.position.x, 0, cam.position.z), WHITE);
            Vector3 md = Vector3Normalize({cosf(0.45f) * cosf(2.2f), sinf(0.45f), cosf(0.45f) * sinf(2.2f)});
            Vector3 mp = Vector3Add({cam.position.x, 0, cam.position.z}, Vector3Scale(md, 78));
            Vector3 z = Vector3Scale(md, -1), x = Vector3Normalize(Vector3CrossProduct({0, 1, 0}, z)), y = Vector3CrossProduct(z, x);
            Matrix mm = {x.x, y.x, z.x, mp.x, x.y, y.y, z.y, mp.y, x.z, y.z, z.z, mp.z, 0, 0, 0, 1};
            rt::DrawSky(gMoon, mm, WHITE);
        } else if (wx != Weather::Fog && !(me.deck == 0 && !me.overboard && me.p.x > 0.9f && me.p.x < 5.1f && fabsf(me.p.y) < 2.1f)) {   // (dry under the wheelhouse roof)
            float fall = fmodf(t * (wx == Weather::Rain ? 9.0f : 13.0f), 12.0f);
            for (int k = 0; k < 2; k++) rt::DrawSky(gRain, MatrixTranslate(cam.position.x, cam.position.y - 6 - fall + 12 * k, cam.position.z), WHITE);
        }
    }
    // ---- the land, the quay, the harbour's buoys
    if (gLandFor) rt::DrawStatic(gLand, MatrixIdentity(), WHITE);
    Matrix Q = MatrixMultiply(MatrixRotateY(-g.moorHeading), MatrixTranslate(g.moorPos.x, 0, g.moorPos.y));
    if (Vector2Distance(b.pos, g.moorPos) < 120) {
        rt::DrawStatic(gQuay, Q, WHITE);
        for (float x : {-9.0f, -1.0f, 7.0f, 13.0f}) Glow(Vector3Transform({x, QUAY_Y + 3.2f, -6.8f}, Q), 0.22f, Color{255, 220, 160, 255}, 2.2f);
    }
    for (int k = 0; k < 16; k++) {
        float a = k * PI / 8;
        Vector2 w = Vector2Add(sess.harbour, {cosf(a) * sess.harbourR, sinf(a) * sess.harbourR});
        if (eco && eco->g && eco->DepthAt(w) < 1) continue;
        if (Vector2Distance(w, {cam.position.x, cam.position.z}) > 90) continue;
        float h = g.sea.Height(w.x, w.y);
        rt::DrawWorldCube(W3(w, h + 0.4f), {0.5f, 0.9f, 0.5f}, Color{50, 50, 54, 255});
        bool blink = fmodf(t + k * 0.37f, 2.0f) < 1.2f;
        Color lc = cosf(a) > 0 ? Color{90, 230, 120, 255} : Color{240, 80, 70, 255};
        Glow(W3(w, h + 1.05f), 0.2f, blink ? lc : Mul(lc, 0.25f), blink ? 2.5f : 0.1f);
    }
    // ---- the Gannet, her lamps and the parts that move
    rt::DrawStatic(gBoat, M, WHITE);
    auto localGlow = [&](Vector3 c, float s, Color col, float glow) { rt::DrawCubeGlow(MatrixMultiply(MatrixMultiply(MatrixScale(s, s, s), MatrixTranslate(c.x, c.y, c.z)), M), col, glow); };
    localGlow({0.2f, DECK_Y + 5.45f, 0}, 0.24f, Color{255, 226, 160, 255}, b.lantern == 0 ? 0.3f : 2.5f);
    localGlow({3.0f, DECK_Y + 2.05f, 0}, 0.12f, Color{255, 214, 150, 255}, 1.8f);
    localGlow({-10.4f, DECK_Y + 3.15f, 0}, 0.16f, Color{255, 220, 170, 255}, 1.8f);
    {
        float fire = std::clamp(b.firebox / 6, 0.1f, 1.0f) * (0.8f + 0.2f * sinf(t * 9));
        rt::DrawCubeGlow(MatrixMultiply(MatrixMultiply(MatrixScale(0.5f, 0.35f, 0.04f), MatrixTranslate(-4.8f, ENGINE_Y + 0.55f, -0.86f)), M), Color{255, (unsigned char)(120 + 90 * fire), 50, 255}, 1.5f * fire);
        rt::DrawCubeGlow(MatrixMultiply(MatrixMultiply(MatrixScale(0.5f, 0.3f, 0.04f), MatrixTranslate(2.4f, DECK_Y + 0.75f, -1.28f)), M), Color{70, (unsigned char)(170 + 50 * sinf(t * 2)), 110, 255}, 1.2f);   // the sonar's screen
        Color gauge = b.pressure > D().redAt ? Color{230, 60, 40, 255} : b.pressure > D().greenHi ? Color{230, 190, 60, 255} : b.pressure > D().greenLo ? Color{90, 200, 90, 255} : Color{150, 150, 150, 255};
        localGlow({-4.0f, ENGINE_Y + 1.2f, -0.85f}, 0.12f, gauge, 1.0f);
    }
    if (b.bilge > 50 && below) {
        float k = std::clamp(b.bilge / 20000, 0.05f, 1.0f);
        rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(5.7f, 0.05f + k * 1.2f, 4.6f), MatrixTranslate(-5.65f, ENGINE_Y + 0.02f + k * 0.6f, 0)), M), Color{40, 90, 110, 120});
    }
    // ---- the crew (not you: you see through your own eyes)
    for (int i = 0; i < (int)g.crew.size(); i++) {
        const Crew& c = g.crew[i];
        if (i == you) continue;
        if (!c.overboard && c.deck == 1 && !below) continue;       // below decks, out of sight
        if (c.dead && !me.dead) continue;                          // only the dead see the dead aboard
        DrawHand(g, c, t);
    }
    // ---- rods and lines
    Color lineC{210, 210, 190, 255}, rodC{120, 86, 50, 255};
    for (const auto& r : g.rods) {
        const StationDef& sd = Stations()[r.station];
        Vector2 tip = r.TipDeck();
        bool fighting = r.state == RodState::Fighting;
        float bend = fighting ? std::clamp(r.fight.tension / TackleOf(r.tackle).strength, 0.0f, 1.2f) : 0;
        Vector3 base = BoatPoint(b, {sd.at.x, DECK_Y + 0.75f, sd.at.y});
        Vector3 tip3 = BoatPoint(b, {tip.x, DECK_Y + 2.3f - bend * 0.5f, tip.y});
        if (r.state == RodState::Out) tip3.y += r.lastTick * 0.06f * sinf(t * 40);
        Vector3 mid = Vector3Lerp(base, tip3, 0.5f); mid.y += 0.1f - bend * 0.2f;
        Seg(base, mid, 0.05f, rodC); Seg(mid, tip3, 0.03f, rodC);
        if (r.state == RodState::Out) {
            float h = g.sea.Height(r.lure.x, r.lure.y);
            Vector3 fl = W3({r.lure.x, r.lure.y}, h + 0.05f - r.lastTick * 0.1f);
            Seg(tip3, fl, 0.012f, lineC);
            Glow(fl, 0.12f, Color{230, 90, 60, 255}, 0.3f);
        }
        if (fighting) {
            const Fight& f = r.fight;
            Vector3 prev = tip3;
            Color lc = f.tension > 0.9f * f.Strength() ? Color{240, 120, 90, 255} : lineC;
            for (size_t i = 1; i < f.node.size(); i++) { Vector3 q = WD(f.node[i]); Seg(prev, q, 0.014f, lc); prev = q; }
            float len = std::clamp(0.35f + sqrtf(f.spec.kg) * 0.22f, 0.4f, 4.5f);
            Color fc = f.jumpT >= 0 ? Color{200, 215, 225, 255} : Color{70, 96, 110, 255};
            DrawFishAt(gFish, WD(f.p), {f.h.x, -f.h.z, f.h.y}, len, fc, f.alongside ? 1.3f : 0);
        }
    }
    if (g.harpoon.state == RodState::Fighting) {
        const Fight& f = g.harpoon.fight;
        Vector3 prev = BoatPoint(b, {10.6f, DECK_Y + 1.06f, 0});
        for (size_t i = 1; i < f.node.size(); i++) { Vector3 q = WD(f.node[i]); Seg(prev, q, 0.02f, Color{120, 110, 90, 255}); prev = q; }
        DrawFishAt(gFish, WD(f.p), {f.h.x, -f.h.z, f.h.y}, std::clamp(0.35f + sqrtf(f.spec.kg) * 0.22f, 0.4f, 4.5f), Color{60, 70, 80, 255});
    }
    // ---- the net, set gear, rings, shots, flares, shot fish afloat
    const Trawl& n = g.net;
    if (n.meshInit && (n.state == NetState::Down || n.state == NetState::Snagged || n.state == NetState::Hauling)) {
        Color net{150, 150, 130, 255};
        for (int s = -1; s <= 1; s += 2) Seg(BoatPoint(b, {-10.6f, DECK_Y + 3.3f, s * 1.4f}), WD(n.node[s < 0 ? 0 : 7]), 0.03f, Color{80, 80, 76, 255});
        for (int j = 0; j < 6; j++) for (int i = 0; i < 8; i++) {
            Vector3 q = WD(n.node[j * 8 + i]);
            if (i < 7) Seg(q, WD(n.node[j * 8 + i + 1]), 0.025f, net);
            if (j < 5) Seg(q, WD(n.node[(j + 1) * 8 + i]), 0.025f, net);
        }
    }
    for (const auto& ll : g.longlines) {
        float ha = g.sea.Height(ll.a.x, ll.a.y), hb2 = g.sea.Height(ll.b.x, ll.b.y);
        Glow(W3(ll.a, ha + 0.15f), 0.35f, Color{240, 110, 40, 255}, 0.2f); Glow(W3(ll.b, hb2 + 0.15f), 0.35f, Color{240, 110, 40, 255}, 0.2f);
        Seg(W3(ll.a, ha - 0.3f), W3(ll.b, hb2 - 0.3f), 0.02f, Color{120, 120, 110, 255});
    }
    for (const auto& p : g.pots) Glow(W3(p.p, g.sea.Height(p.p.x, p.p.y) + 0.15f), 0.3f, Color{230, 200, 60, 255}, 0.2f);
    for (const auto& rg : g.rings) {
        if (rg.state == 0) continue;
        float h = g.sea.Height(rg.p.x, rg.p.y) + (rg.state == 1 ? 1.2f : 0.05f);
        for (int k = 0; k < 8; k++) { float a0 = k * 0.785f, a1 = a0 + 0.785f; Seg(W3({rg.p.x + cosf(a0) * 0.35f, rg.p.y + sinf(a0) * 0.35f}, h), W3({rg.p.x + cosf(a1) * 0.35f, rg.p.y + sinf(a1) * 0.35f}, h), 0.1f, k % 2 ? Color{240, 240, 230, 255} : Color{220, 70, 50, 255}); }
        if (rg.thrower >= 0 && rg.thrower < (int)g.crew.size()) Seg(BoatPoint(b, {g.crew[rg.thrower].p.x, DECK_Y + 1.0f, g.crew[rg.thrower].p.y}), W3(rg.p, h), 0.012f, Color{200, 190, 160, 255});
    }
    for (const auto& s : g.shots) Glow(WD(s.p), s.kind == Shot::Harpoon || s.kind == Shot::Spear ? 0.12f : 0.06f, s.kind == Shot::Flare ? Color{255, 100, 70, 255} : Color{230, 220, 190, 255}, s.kind == Shot::Flare ? 3.0f : 0.6f);
    for (const auto& fl : g.flares) Glow(W3(fl.p, g.sea.Height(fl.p.x, fl.p.y) + 0.1f), 0.25f, Color{255, 90, 60, 255}, 3.0f);
    for (const auto& f : g.floaters) {
        float len = std::clamp(0.35f + sqrtf(f.kg) * 0.25f, 0.4f, 3.0f);
        DrawFishAt(gFish, W3(f.p, g.sea.Height(f.p.x, f.p.y) + 0.05f), {1, 0, 0.3f}, len, Color{200, 205, 210, 255}, 1.5f);
    }
    // ---- fish on the deck: on their sides where they came aboard; the live ones arch and slap every so often
    for (size_t i = 0; i < g.hold.size(); i++) {
        const CatchRec& h = g.hold[i];
        if (h.gutted) continue;
        float len = std::clamp(0.3f + sqrtf(h.kg) * 0.22f, 0.3f, 2.6f);
        float arch = 0, hop = 0;
        if (!h.dead) { float ph = fmodf(g.time * 1.3f + i * 0.7f, 1.0f); if (ph < 0.18f) { float s = sinf(ph / 0.18f * 3.1416f); arch = s * 0.9f; hop = s * 0.12f; } }
        if (!h.dead && h.airT > 0) { float s = sinf(h.airT / 0.45f * 3.1416f); hop = std::max(hop, s * 0.35f); arch = std::max(arch, s); }   // (thrown across the deck by a flop)
        Vector3 hd = Vector3Normalize(BoatDir(b, {cosf(h.heading), arch * 0.5f, sinf(h.heading)}));   // (lying the way the sim has it: its head is where a headshot lands)
        Color col = h.dead ? Color{150, 158, 164, 255} : Color{196, 206, 214, 255};
        DrawFishAt(gFish, BoatPoint(b, {h.deckAt.x, DECK_Y + 0.06f + hop, h.deckAt.y}), hd, len, col, 1.5f + arch * 0.4f);
    }
    // ---- the web's life: schools and hunters in the water, gulls over it
    if (eco) {
        const auto& S = Species().sp;
        Vector2 eye2{cam.position.x, cam.position.z};
        for (size_t i = 0; i < eco->agents.size(); i++) {
            const EcoAgent& a = eco->agents[i];
            if (!a.alive) continue;
            const SpeciesRec& r = S[a.sp];
            Vector2 p2{a.p.x, a.p.y};
            if (Vector2Distance(p2, eye2) > 55) continue;
            Vector3 hd{a.v.x, 0, a.v.y};
            if (Vector3Length(hd) < 0.01f) hd = {a.wander.x, 0, a.wander.y};
            if (r.band == BAND_AIR) {
                for (int k = 0; k < std::min(a.count, 14); k++) {
                    float ang = H01((int)i, k, 1) * 6.2832f + t * (0.6f + H01(k, 2, (int)i)), rr = 1.5f + H01(k, (int)i, 3) * 4;
                    Vector3 q{p2.x + cosf(ang) * rr, 5.0f + H01(k, 7, (int)i) * 5 + sinf(t * 2 + k) * 0.4f, p2.y + sinf(ang) * rr};
                    DrawFishAt(gGull, q, {-sinf(ang), 0, cosf(ang)}, 1.0f, WHITE, sinf(t * 9 + k) * 0.3f);
                }
                continue;
            }
            float depth = std::max(0.0f, a.p.z);
            Color c = SpeciesTint(r);
            float dim = expf(-depth / 9.0f);
            if (r.threat && me.dead && ghostSee > 0 && Vector2Distance(p2, g.boat.ToWorld(me.p)) < 10) { c = Color{210, 235, 255, 255}; dim = 1.0f; }
            c = Mul(c, 0.4f + 0.6f * dim);
            bool jelly = r.cls == "jelly";
            if (a.count > 3) {
                int nd = std::min(a.count, 16);
                float spread = std::min(4.0f, 0.4f + sqrtf((float)a.count) * 0.12f);
                float len = std::clamp(0.12f + sqrtf(r.MeanKg()) * 0.25f, 0.15f, 0.8f);
                for (int k = 0; k < nd; k++) {
                    float ang = H01((int)i, k, 11) * 6.2832f, rr = sqrtf(H01(k, (int)i, 12)) * spread;
                    Vector3 q{p2.x + cosf(ang) * rr + sinf(t * 2 + k) * 0.15f, -depth - H01(k, (int)i, 13) * spread * 0.4f, p2.y + sinf(ang) * rr};
                    DrawFishAt(jelly ? gJelly : gFish, q, hd, jelly ? len * 1.4f : len, c);
                }
            } else {
                float len = std::clamp(0.3f + sqrtf(r.MeanKg()) * 0.3f, 0.3f, 3.5f);
                for (int k = 0; k < a.count; k++) {
                    Vector3 q{p2.x + H01((int)i, k, 5) * 1.2f - 0.6f, -depth, p2.y + H01(k, (int)i, 6) * 1.2f - 0.6f};
                    DrawFishAt(jelly ? gJelly : gFish, q, hd, len, c);
                }
            }
        }
    }
    // ---- what you hold, at the bottom right of your view (off station, alive, aboard)
    Item held = me.slots[me.sel].it;
    if (gCrewReady && held != Item::None && me.station < 0 && !me.dead && !me.overboard) {
        Vector3 f = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
        Vector3 rgt = Vector3Normalize(Vector3CrossProduct(f, cam.up)), up = Vector3CrossProduct(rgt, f);
        float bob = Vector2Length(me.v) > 0.3f ? sinf(t * 9) * 0.012f : 0;
        // the item's motion: a gaff, priest or knife swings down and through on a use (Crew::cool runs down from the
        // swing's length), a gun kicks back and tips up on a shot and drops out of view to reload, a speargun's spear
        // slides home along the rail as the reload ends
        bool melee = held == Item::Gaff || held == Item::Priest || held == Item::Knife;
        bool gun = held == Item::Rifle || held == Item::Shotgun || held == Item::Speargun || held == Item::Flare;
        float swingLen = held == Item::Gaff ? 0.5f : 0.4f;
        float sw = melee && me.cool > 0 ? 1 - me.cool / swingLen : -1;                 // 0..1 through the swing
        float coolMax = held == Item::Rifle ? 1.2f : held == Item::Shotgun ? 0.8f : held == Item::Speargun ? 2.0f : 1.0f;
        float kick = gun && me.cool > 0 ? std::max(0.0f, 1 - (coolMax - me.cool) * 7) : 0;   // the first seventh of a second
        float reloadLen = held == Item::Speargun ? 1.2f : 1.8f;
        float rl = gun && me.reloadT > 0 ? 1 - me.reloadT / reloadLen : -1;             // 0..1 through the reload
        float dip = rl >= 0 ? sinf(rl * PI) * 0.16f : 0;
        float back = kick * 0.07f, pitchUp = kick * 0.35f;
        float swingPitch = sw >= 0 ? (0.6f - sinf(sw * PI) * 1.6f) : 0;                 // raised, then chopped down past level
        float swingYaw = sw >= 0 ? (sw - 0.5f) * 0.8f : 0;
        Vector3 p = Vector3Add(cam.position, Vector3Add(Vector3Scale(f, 0.42f - back), Vector3Add(Vector3Scale(rgt, 0.2f + swingYaw * 0.1f), Vector3Scale(up, -0.2f + bob - dip + (sw >= 0 ? 0.08f * sinf(sw * PI) : 0)))));
        // the item's +X along the look, tipped a little up and in; the swing and the kick tilt it
        Vector3 ax = Vector3Normalize(Vector3Add(f, Vector3Add(Vector3Scale(up, 0.12f + pitchUp + swingPitch), Vector3Scale(rgt, -0.12f + swingYaw))));
        Vector3 az = Vector3Normalize(Vector3CrossProduct(ax, up)), ay = Vector3CrossProduct(az, ax);
        Matrix hm = {ax.x, ay.x, az.x, p.x, ax.y, ay.y, az.y, p.y, ax.z, ay.z, az.z, p.z, 0, 0, 0, 1};
        if (rl >= 0) hm = MatrixMultiply(MatrixRotateZ(-0.5f * sinf(rl * PI)), hm);   // (rolled out to the side while the hands work)
        rt::DrawStaticGlow(gItem[(int)held], hm, WHITE, 0.25f);   // (a touch of light from the lamp at your shoulder)
        // the speargun's spear slides home in the last third of the reload; a muzzle flash on a powder shot
        if (held == Item::Speargun && rl > 0.66f) { float s = (rl - 0.66f) / 0.34f; rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.5f * s, 0.006f, 0.006f), MatrixTranslate(0.1f + 0.25f * s, 0.03f, 0)), hm), Color{150, 156, 160, 255}); }
        if ((held == Item::Rifle || held == Item::Shotgun) && kick > 0.5f) rt::DrawCubeGlow(MatrixMultiply(MatrixMultiply(MatrixScale(0.06f, 0.06f, 0.06f), MatrixTranslate(held == Item::Rifle ? 0.62f : 0.5f, 0.02f, 0)), hm), Color{255, 220, 140, 255}, 1.0f);
    }
    // ---- the sea, last (its surface is glass the rest is seen through)
    UpdateSea(g.sea, cam.position);
    rt::DrawStatic(gSea, MatrixIdentity(), WHITE);
    rt::RenderEnd();
}

void UnloadTrawl3D() {
    if (gReady) { UnloadModel(gBoat); UnloadModel(gQuay); UnloadModel(gFish); UnloadModel(gJelly); UnloadModel(gGull); gReady = false; }
    if (gSeaReady) { UnloadModel(gSea); gSeaReady = false; }
    if (gLandFor) { UnloadModel(gLand); gLandFor = nullptr; }
    if (gCrewReady) {
        for (int r = 0; r < (int)Role::COUNT; r++) { UnloadModel(gBody[r]); UnloadModel(gArm[r]); }
        UnloadModel(gLeg); UnloadModel(gStars); UnloadModel(gMoon); UnloadModel(gRain);
        for (int i = 1; i < (int)Item::COUNT; i++) UnloadModel(gItem[i]);
        gCrewReady = false;
    }
}

} // namespace tw
