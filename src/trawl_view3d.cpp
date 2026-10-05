// The Trawl in first person: the Gannet, the sea and the Lagoon's life drawn in 3D through Red Tide's inked
// renderer, from the eyes of the hand you play (see trawl_view3d.h for the frames). Everything here only reads the
// simulation; trawl.cpp feeds it input and puts the shared HUD over it.
#include "trawl_view3d.h"
#include "game.h"
#include "input.h"
#include "trawl_weapons.h"
#include <functional>
#include "raymath.h"
#include "redtide_render.h"
#include "figure3d.h"
#include "redtide_vis.h"
#include "skins.h"
#include "rlgl.h"
#include "sound.h"
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

static Matrix SkiffMatrix(const Gannet& g);
static const float ATOLL_Y_EYE = 0.6f;   // (= ATOLL_Y below: the landing's sand)
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
    if (c.deck == DECK_SHORE) {
        // on foot on a landing: the sand's height, the look turned as on the quay
        Vector2 w = g.HandWorld(you);
        cam.position = W3(w, ATOLL_Y_EYE + EYE_H);
        Vector3 d = Vector3Transform(dl, MatrixRotateY(-g.boat.heading));
        cam.target = Vector3Add(cam.position, d); cam.up = {0, 1, 0};
        return cam;
    }
    if (c.deck == DECK_SKIFF) {
        // seated in the skiff: low over the water, her roll half felt; the look turns with her (as on the Gannet)
        Matrix M = SkiffMatrix(g);
        cam.position = Vector3Transform({c.p.x, 0.98f, c.p.y}, M);
        Vector3 d = Vector3Transform(dl, MatrixRotateY(-g.skiff.heading));
        cam.target = Vector3Add(cam.position, d);
        Matrix R = M; R.m12 = R.m13 = R.m14 = 0;
        cam.up = Vector3Normalize(Vector3Lerp({0, 1, 0}, Vector3Transform({0, 1, 0}, R), 0.5f));
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
    cam.position = BoatPoint(g.boat, {sp.x, floorY + c.z + (c.fallen ? 0.42f : EYE_H - 0.55f * c.crouchK), sp.y});
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
bool CrewHeadOnScreen(const Gannet& g, int ci, const Camera3D& cam, Vector2* out, float height) {
    const Crew& c = g.crew[ci];
    Vector3 p;
    if (c.overboard) p = W3(c.swim, 0.8f);
    else if (c.deck == DECK_SKIFF) p = Vector3Transform({c.p.x, 1.4f, c.p.y}, SkiffMatrix(g));
    else if (c.deck == DECK_SHORE) p = W3(g.HandWorld(ci), ATOLL_Y_EYE + height);
    else { Vector2 sp = StandSpot(c); p = BoatPoint(g.boat, {sp.x, (c.deck == 1 ? ENGINE_Y : DECK_Y) + height, sp.y}); }
    Vector3 fw = Vector3Subtract(cam.target, cam.position);
    if (Vector3DotProduct(Vector3Subtract(p, cam.position), fw) <= 0.1f || Vector3Distance(p, cam.position) > 40) return false;
    *out = GetWorldToScreenEx(p, cam, SCREEN_W, SCREEN_H);
    return out->x > 0 && out->y > 0 && out->x < SCREEN_W && out->y < SCREEN_H;
}

// ---------------------------------------------------------------- the models
static float HalfBeam3(float x) { return x > 5 ? std::max(0.5f, 3.0f - (x - 5) * 0.42f) : x < -10 ? 3.0f - (-10 - x) * 0.8f : 3.0f; }
static float KeelY(float x) { return x > 5 ? -1.9f + (x - 5) / 6.0f * 2.3f : -1.9f; }
static Color Mul(Color c, float k) { return {(unsigned char)std::clamp(c.r * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.g * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.b * k, 0.0f, 255.0f), c.a}; }

static Model gBoat{}, gQuay{}, gFish{}, gJelly{}, gGull{}, gSea{}, gLand{}, gMould{}; static bool gMouldOn = false;   // (gMould: the Grotto's glowing mould)
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
    // the bulkhead to the hold, with the watertight door's opening in it (the door itself is drawn as it stands)
    WallX(mb, -2.75f, -2.35f, -0.6f, ENGINE_Y, DECK_Y, bulk); WallX(mb, -2.75f, 0.6f, 2.35f, ENGINE_Y, DECK_Y, bulk);
    WallX(mb, -2.75f, -0.6f, 0.6f, ENGINE_Y + 1.9f, DECK_Y, bulk);
    WallX(mb, -8.55f, -2.35f, 2.35f, ENGINE_Y, DECK_Y, bulk);
    // the fish hold: ice pounds along the starboard side, a ladder under the main hatch
    {
        Color holdF{52, 58, 62, 255}, ice{170, 200, 215, 255};
        mb.Box({-1.0f, ENGINE_Y - 0.05f, 0}, {1.8f, 0.05f, 2.35f}, holdF);
        WallX(mb, 0.95f, -2.35f, 2.35f, ENGINE_Y, DECK_Y, bulk);
        WallZ(mb, -2.35f, -2.75f, 0.95f, ENGINE_Y, DECK_Y, bulk); WallZ(mb, 2.35f, -2.75f, 0.95f, ENGINE_Y, DECK_Y, bulk);
        for (int k = 0; k < 3; k++) mb.Box({-2.2f + k * 1.2f, ENGINE_Y + 0.35f, 1.6f}, {0.5f, 0.35f, 0.6f}, ice);
        for (int s = -1; s <= 1; s += 2) mb.Box({-1.0f + s * 0.25f, (ENGINE_Y + DECK_Y) / 2, -1.0f}, {0.03f, (DECK_Y - ENGINE_Y) / 2, 0.03f}, brass);
        for (float y = ENGINE_Y + 0.3f; y < DECK_Y; y += 0.3f) mb.Box({-1.0f, y, -1.0f}, {0.25f, 0.02f, 0.03f}, brass);
    }
    // the fo'c'sle: bunks either side, the magazine locker, the Medic's cot, a ladder under the fore hatch
    {
        Color fore{64, 52, 40, 255}, bunk{120, 100, 74, 255}, cot{200, 196, 180, 255};
        mb.Box({7.1f, ENGINE_Y - 0.05f, 0}, {1.95f, 0.05f, 1.95f}, fore);
        WallX(mb, 5.05f, -1.95f, 1.95f, ENGINE_Y, DECK_Y, bulk); WallX(mb, 9.1f, -1.95f, 1.95f, ENGINE_Y, DECK_Y, bulk);
        WallZ(mb, -1.95f, 5.05f, 9.1f, ENGINE_Y, DECK_Y, bulk); WallZ(mb, 1.95f, 5.05f, 9.1f, ENGINE_Y, DECK_Y, bulk);
        for (int s = -1; s <= 1; s += 2) for (int k = 0; k < 2; k++) { mb.Box({5.9f + k * 1.2f, ENGINE_Y + 0.5f, s * 1.5f}, {0.55f, 0.08f, 0.3f}, bunk); mb.Box({5.9f + k * 1.2f, ENGINE_Y + 1.5f, s * 1.5f}, {0.55f, 0.08f, 0.3f}, bunk); }
        mb.Box({8.3f, ENGINE_Y + 0.6f, 1.1f}, {0.4f, 0.6f, 0.3f}, Color{90, 70, 40, 255});
        mb.Box({8.3f, ENGINE_Y + 0.9f, 0.8f}, {0.25f, 0.04f, 0.02f}, brass);
        mb.Box({6.0f, ENGINE_Y + 0.4f, -1.2f}, {0.7f, 0.06f, 0.3f}, cot);
        for (int s = -1; s <= 1; s += 2) mb.Box({7.6f + s * 0.25f, (ENGINE_Y + DECK_Y) / 2, 0.0f}, {0.03f, (DECK_Y - ENGINE_Y) / 2, 0.03f}, brass);
        for (float y = ENGINE_Y + 0.3f; y < DECK_Y; y += 0.3f) mb.Box({7.6f, y, 0.0f}, {0.25f, 0.02f, 0.03f}, brass);
    }
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

// the skiff: a 4.5 m clinker rowing boat in her own frame (x forward, y up from her waterline, z to starboard)
static float SkiffHB(float x) { float u = (x + 2.25f) / 4.5f; return 0.8f * (u < 0.65f ? 0.88f + 0.12f * sinf(u / 0.65f * PI * 0.5f) : cosf((u - 0.65f) / 0.35f * PI * 0.5f) * 0.97f + 0.03f); }
static void BuildSkiff(MeshBuilder& mb) {
    Color paint{226, 222, 206, 255}, band{150, 46, 38, 255}, wood{150, 108, 66, 255}, dark{96, 70, 44, 255};
    const float BOT = -0.28f, TOP = 0.34f;
    for (int k = 0; k < 10; k++) {
        float x0 = -2.25f + k * 0.45f, x1 = x0 + 0.45f;
        float h0 = SkiffHB(x0), h1 = SkiffHB(x1);
        for (int s = -1; s <= 1; s += 2) {
            // two strakes outside (the upper one painted red), the inside planking, the bottom
            mb.Quad({x0, BOT, s * h0 * 0.55f}, {x1, BOT, s * h1 * 0.55f}, {x1, 0.08f, s * h1 * 0.9f}, {x0, 0.08f, s * h0 * 0.9f}, paint);
            mb.Quad({x0, 0.08f, s * h0 * 0.9f}, {x1, 0.08f, s * h1 * 0.9f}, {x1, TOP, s * h1}, {x0, TOP, s * h0}, band);
            mb.Quad({x0, TOP, s * (h0 - 0.05f)}, {x1, TOP, s * (h1 - 0.05f)}, {x1, BOT + 0.05f, s * (h1 * 0.55f - 0.04f)}, {x0, BOT + 0.05f, s * (h0 * 0.55f - 0.04f)}, wood);
            mb.Quad({x0, TOP, s * h0}, {x1, TOP, s * h1}, {x1, TOP + 0.03f, s * (h1 - 0.05f)}, {x0, TOP + 0.03f, s * (h0 - 0.05f)}, dark);   // the gunwale
        }
        mb.Quad({x0, BOT, -h0 * 0.55f}, {x1, BOT, -h1 * 0.55f}, {x1, BOT, h1 * 0.55f}, {x0, BOT, h0 * 0.55f}, paint);
        mb.Quad({x0, BOT + 0.06f, -h0 * 0.5f}, {x1, BOT + 0.06f, -h1 * 0.5f}, {x1, BOT + 0.06f, h1 * 0.5f}, {x0, BOT + 0.06f, h0 * 0.5f}, dark);   // the bottom boards
    }
    float ht = SkiffHB(-2.25f);
    mb.Quad({-2.25f, BOT, -ht * 0.55f}, {-2.25f, BOT, ht * 0.55f}, {-2.25f, TOP, ht}, {-2.25f, TOP, -ht}, paint);   // the transom
    for (float x : {0.2f, -1.3f, 1.3f}) mb.Box({x, 0.06f, 0}, {0.13f, 0.03f, SkiffHB(x) - 0.06f}, wood);         // the thwarts
    for (int s = -1; s <= 1; s += 2) mb.Box({0.2f, TOP + 0.06f, s * (SkiffHB(0.2f) - 0.02f)}, {0.03f, 0.06f, 0.03f}, Color{180, 150, 80, 255});   // rowlocks
    mb.Box({1.95f, TOP + 0.25f, 0}, {0.02f, 0.25f, 0.02f}, dark);   // the lantern's post
}
static Model gSkiff{};
// the skiff's frame on the sea: hung on the davit, lowered, afloat on the swell, or keel up
static Matrix SkiffMatrix(const Gannet& g) {
    const Skiff& s = g.skiff;
    switch (s.state) {
        case SkiffState::Stowed: return MatrixMultiply(MatrixTranslate(-11.4f, DECK_Y + 1.1f, 0), BoatMatrix(g.boat));
        case SkiffState::Lowering: { float k = std::clamp(s.t / D().skiffLower, 0.0f, 1.0f); return MatrixMultiply(MatrixTranslate(-11.4f - k * 1.2f, (DECK_Y + 1.1f) * (1 - k) + 0.1f * k, 0), BoatMatrix(g.boat)); }
        default: break;
    }
    float h = g.sea.Height(s.p.x, s.p.y);
    if (s.state == SkiffState::Recovering) h += 2.2f * std::clamp(s.t / D().skiffRecover, 0.0f, 1.0f);
    Matrix m = MatrixMultiply(MatrixRotateX(s.state == SkiffState::Capsized ? PI : s.roll), MatrixRotateY(-s.heading));
    return MatrixMultiply(m, MatrixTranslate(s.p.x, h + (s.state == SkiffState::Capsized ? 0.05f : 0.24f), s.p.y));   // (she floats light: her bottom boards just at the waterline, so the sea's glass never shows inside her)
}
static void EnsureModels() {
    if (gReady || !IsWindowReady()) return;
    { MeshBuilder mb; BuildBoat(mb); gBoat = LoadModelFromMesh(mb.Build()); }
    { MeshBuilder mb; BuildSkiff(mb); gSkiff = LoadModelFromMesh(mb.Build()); }
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
    m.normals = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
    for (int i = 0; i < m.vertexCount * 3; i++) m.normals[i] = (i % 3 == 1) ? 1.0f : 0.0f;
    for (int i = 0; i < m.vertexCount; i++) { m.colors[i * 4] = 40; m.colors[i * 4 + 1] = 84; m.colors[i * 4 + 2] = 96; m.colors[i * 4 + 3] = 205; }
    UploadMesh(&m, true);
    gSea = LoadModelFromMesh(m);
    gSeaReady = true;
}
static void UpdateSea(const Sea& sea, Vector3 eye, const Eco* eco = nullptr) {
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
    // smooth normals from the swell's slopes (the water shader lights the surface with them)
    {
        float* n = m.normals; int q = 0;
        auto hh = [&](int i, int j) { i = std::clamp(i, 0, SN); j = std::clamp(j, 0, SN); return H[j * (SN + 1) + i]; };
        auto nput = [&](int i, int j) { Vector3 nn = Vector3Normalize({-(hh(i + 1, j) - hh(i - 1, j)) / (2 * SC), 1.0f, -(hh(i, j + 1) - hh(i, j - 1)) / (2 * SC)}); n[q++] = nn.x; n[q++] = nn.y; n[q++] = nn.z; };
        for (int j = 0; j < SN; j++) for (int i = 0; i < SN; i++) { nput(i, j); nput(i + 1, j); nput(i + 1, j + 1); nput(i, j); nput(i + 1, j + 1); nput(i, j + 1); }
        UpdateMeshBuffer(m, 2, n, m.vertexCount * 3 * sizeof(float), 0);
    }
    // the Weeds' kelp canopy: golden-brown mats on the water where the chart has kelp (the sea's own vertices, so the ink
    // pass sees one surface, not a field of boxes)
    static bool tinted = false;
    bool kelp = eco && eco->g && eco->ground == "weeds";
    if (kelp || tinted) {
        unsigned char* col = m.colors; int q = 0;
        for (int j = 0; j < SN; j++) for (int i = 0; i < SN; i++) {
            Vector2 w{cx + (i + 0.5f) * SC, cz + (j + 0.5f) * SC};
            bool k = kelp && eco->HabAt(w) == H_KELP;
            float n = k ? 0.5f + 0.5f * sinf(w.x * 0.9f + w.y * 1.3f) : 0;
            unsigned char r = k ? (unsigned char)(110 + 40 * n) : 40, g2 = k ? (unsigned char)(86 + 30 * n) : 84, b = k ? 34 : 96, a = k ? 245 : 205;
            for (int t = 0; t < 6; t++) { col[q++] = r; col[q++] = g2; col[q++] = b; col[q++] = a; }
        }
        UpdateMeshBuffer(m, 3, col, m.vertexCount * 4, 0);
        tinted = kelp;
    }
}
// the land in the chart (the atoll, the reef's dry crest): built once per ground from its cells
// ---- the ground as baked-looking terrain: quads gathered per tiling material (sand, wet sand, jungle, rock, cave
// rock, deck, marble from assets/trawl/props/terrain_mats.glb) with smooth normals, UVs in metres and occlusion in the
// vertex colour; drawn by the PBR path like the boat. Its materials are the asset's (never unloaded with it).
enum TerrMat { TM_SAND, TM_WETSAND, TM_JUNGLE, TM_ROCK, TM_CAVEROCK, TM_DECK, TM_MARBLE, TM_COUNT };
struct TerrainBuilder {
    std::vector<float> v[TM_COUNT], n[TM_COUNT], uv[TM_COUNT]; std::vector<unsigned char> c[TM_COUNT];
    void Vert(int m, Vector3 p, Vector3 nn, float ao) {
        v[m].insert(v[m].end(), {p.x, p.y, p.z}); n[m].insert(n[m].end(), {nn.x, nn.y, nn.z});
        // (UVs by the face's facing: tops by x/z, walls by their run and height, 3 m a tile)
        float ax = fabsf(nn.x), ay = fabsf(nn.y), az = fabsf(nn.z);
        float u = ay >= ax && ay >= az ? p.x : ax >= az ? p.z : p.x, w = ay >= ax && ay >= az ? p.z : p.y;
        uv[m].insert(uv[m].end(), {u / 3.0f, w / 3.0f});
        unsigned char a = (unsigned char)std::clamp(ao * 255.0f, 0.0f, 255.0f);
        c[m].insert(c[m].end(), {a, a, a, 255});
    }
    void Quad(int m, Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, Vector3 n0, Vector3 n1, Vector3 n2, Vector3 n3, float a0 = 1, float a1 = 1, float a2 = 1, float a3 = 1) {
        Vert(m, p0, n0, a0); Vert(m, p1, n1, a1); Vert(m, p2, n2, a2);
        Vert(m, p0, n0, a0); Vert(m, p2, n2, a2); Vert(m, p3, n3, a3);
    }
    void QuadFlat(int m, Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, float ao = 1) {
        Vector3 nn = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(p1, p0), Vector3Subtract(p2, p0)));
        if (nn.y < -0.5f) nn = Vector3Negate(nn);   // (the land's tops always face up; double-sided anyway)
        Quad(m, p0, p1, p2, p3, nn, nn, nn, nn, ao, ao, ao, ao);
    }
    // a model with one mesh per material in use; false if the materials asset is missing
    bool Build(Model* out) {
        const Model* mats = rt::LoadAsset("trawl/props/terrain_mats.glb");
        if (!mats) return false;
        static const char* NAME[TM_COUNT] = {"sand", "wetsand", "jungle", "rock", "caverock", "deck", "marble"};
        Model m{}; m.transform = MatrixIdentity();
        int used = 0; for (int k = 0; k < TM_COUNT; k++) if (!v[k].empty()) used++;
        if (!used) return false;
        m.meshCount = used; m.materialCount = used;
        m.meshes = (Mesh*)RL_CALLOC(used, sizeof(Mesh)); m.materials = (Material*)RL_CALLOC(used, sizeof(Material)); m.meshMaterial = (int*)RL_CALLOC(used, sizeof(int));
        int i = 0;
        for (int k = 0; k < TM_COUNT; k++) {
            if (v[k].empty()) continue;
            Mesh& me = m.meshes[i];
            me.vertexCount = (int)v[k].size() / 3; me.triangleCount = me.vertexCount / 3;
            me.vertices = (float*)RL_MALLOC(v[k].size() * sizeof(float)); memcpy(me.vertices, v[k].data(), v[k].size() * sizeof(float));
            me.normals = (float*)RL_MALLOC(n[k].size() * sizeof(float)); memcpy(me.normals, n[k].data(), n[k].size() * sizeof(float));
            me.texcoords = (float*)RL_MALLOC(uv[k].size() * sizeof(float)); memcpy(me.texcoords, uv[k].data(), uv[k].size() * sizeof(float));
            me.colors = (unsigned char*)RL_MALLOC(c[k].size()); memcpy(me.colors, c[k].data(), c[k].size());
            UploadMesh(&me, false);
            if (!rt::AssetMaterial(mats, NAME[k], &m.materials[i])) m.materials[i] = LoadMaterialDefault();
            m.meshMaterial[i] = i;
            i++;
        }
        *out = m;
        rt::MarkVertexOcclusion(out, true);
        return true;
    }
};
static void UnloadTerrain(Model& m) {   // (its meshes only: the materials belong to terrain_mats.glb)
    for (int i = 0; i < m.meshCount; i++) UnloadMesh(m.meshes[i]);
    RL_FREE(m.meshes); RL_FREE(m.materials); RL_FREE(m.meshMaterial);
    m = Model{};
}
// where the baked props stand on the land (palms on the chart's land, stalactites and mould in the Grotto)
struct PropAt { const char* asset; Matrix m; float glow; bool floats = false; float far = 0; };   // floats: rides the swell (m's y is its height over the water); far: its draw distance (0 the default)
static std::vector<PropAt> gLandProps;
static Model gTerrain{};

static void EnsureLand(const Eco* e) {
    if (!e || !e->g || gLandFor == (const void*)e->g) return;
    if (gLandFor) UnloadModel(gLand);
    MeshBuilder mb, mould;
    Color sand{150, 132, 96, 255}, sandDk{110, 96, 70, 255}, palm{50, 80, 44, 255};
    bool grotto = e->ground == "grotto";
    for (int y = 0; y < e->n; y++) for (int x = 0; x < e->n; x++) {
        float d = e->depth[(size_t)y * e->n + x];
        Vector2 wc{(x + 0.5f) * e->cell, (y + 0.5f) * e->cell};
        bool caveSide = grotto && wc.x > e->archX0 - 2;
        // (the Grotto) the roof over the black water, hung with stalactites, and the mould glowing on the ledges
        if (caveSide && d > 0.01f && wc.x > e->archX1 && x % 2 == 0 && y % 2 == 0) {
            float rh = 24 + 4 * sinf(x * 0.31f) * sinf(y * 0.27f);
            mb.Box({wc.x + e->cell / 2, rh + 1.5f, wc.y + e->cell / 2}, {e->cell, 1.5f, e->cell}, Color{46, 44, 44, 255});
            if (((x * 11 + y * 17) % 13) == 0) mb.Cone({wc.x, rh, wc.y}, {wc.x, rh - 4 - 6 * fabsf(sinf(x * 1.7f + y)), wc.y}, 0.9f, 6, Color{70, 66, 62, 255});
        }
        if (grotto && d > 0.01f && e->hab[(size_t)y * e->n + x] == H_WALL && ((x + y) % 2 == 0)) mould.Box({wc.x, 0.6f + 0.5f * sinf(x * 2.1f + y), wc.y}, {e->cell * 0.45f, 0.25f, e->cell * 0.45f}, Color{70, 170, 150, 255});
        if (d > 0.01f) continue;
        bool landing = false; for (Vector2 la : e->landingAt) if (Vector2Distance({(x + 0.5f) * e->cell, (y + 0.5f) * e->cell}, la) < 16) landing = true;
        if (landing) continue;   // (a landing has its own smooth island: BuildAtoll)
        if (caveSide) {   // the cave's rock: walls up to the roof
            float h = 18 + 8 * fabsf(sinf(x * 0.7f + y * 1.1f));
            mb.Box({wc.x, h / 2 - 0.4f, wc.y}, {e->cell / 2, h / 2 + 0.4f, e->cell / 2}, ((x + y) % 3) ? Color{64, 60, 58, 255} : Color{52, 50, 48, 255});
            continue;
        }
        float h = 0.7f + 0.35f * sinf(x * 1.7f + y * 2.3f) * sinf(x * 0.9f);
        mb.Box({(x + 0.5f) * e->cell, h / 2 - 0.4f, (y + 0.5f) * e->cell}, {e->cell / 2, h / 2 + 0.4f, e->cell / 2}, ((x + y) % 3) ? sand : sandDk);
        if (((x * 7 + y * 13) % 23) == 0) mb.Tube({{(x + 0.5f) * e->cell, h, (y + 0.5f) * e->cell}, {(x + 0.7f) * e->cell, h + 4.5f, (y + 0.4f) * e->cell}}, 0.12f, 0.08f, 5, Color{90, 70, 50, 255}, Color{90, 70, 50, 255}, 0);
        if (((x * 7 + y * 13) % 23) == 0) mb.Octa({(x + 0.7f) * e->cell, h + 4.6f, (y + 0.4f) * e->cell}, 1.1f, palm);
    }
    gLand = LoadModelFromMesh(mb.Build());
    if (gMouldOn) { UnloadModel(gMould); gMouldOn = false; }
    if (grotto) { gMould = LoadModelFromMesh(mould.Build()); gMouldOn = true; }
    gLandFor = (const void*)e->g;
    // the terrain (the old boxes above stay as the fallback): a smooth heightfield over the chart's cells
    if (gTerrain.meshCount > 0) UnloadTerrain(gTerrain);
    gLandProps.clear();
    {
        const int n = e->n; const float C = e->cell;
        auto inLanding = [&](Vector2 w) { for (Vector2 la : e->landingAt) if (Vector2Distance(w, la) < 16) return true; return false; };
        std::vector<float> H((size_t)n * n);
        std::vector<char> cave((size_t)n * n);
        for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) {
            size_t i = (size_t)y * n + x;
            float d = e->depth[i]; Vector2 wc{(x + 0.5f) * C, (y + 0.5f) * C};
            cave[i] = grotto && wc.x > e->archX0 - 2;
            if (d > 0.01f || inLanding(wc)) H[i] = -std::min(3.0f, std::max(0.6f, d)) * 0.6f;
            else if (cave[i]) H[i] = 18 + 8 * fabsf(sinf(x * 0.7f + y * 1.1f));
            else H[i] = 0.6f + 0.35f * sinf(x * 1.7f + y * 2.3f) * sinf(x * 0.9f);
        }
        auto cellH = [&](int x, int y) { x = std::clamp(x, 0, n - 1); y = std::clamp(y, 0, n - 1); return H[(size_t)y * n + x]; };
        auto cornerH = [&](int x, int y) { return 0.25f * (cellH(x - 1, y - 1) + cellH(x, y - 1) + cellH(x - 1, y) + cellH(x, y)); };   // (corner (x,y) of cell (x,y))
        auto cornerN = [&](int x, int y) { float dx = cornerH(x + 1, y) - cornerH(x - 1, y), dz = cornerH(x, y + 1) - cornerH(x, y - 1); return Vector3Normalize({-dx / (2 * C), 1, -dz / (2 * C)}); };
        auto cornerAO = [&](int x, int y) { float h = cornerH(x, y), m = 0; for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) m += cornerH(x + dx, y + dy); m /= 9; return std::clamp(1.0f - std::max(0.0f, m - h) * 0.12f, 0.45f, 1.0f); };
        TerrainBuilder tb;
        for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) {
            float h00 = cornerH(x, y), h10 = cornerH(x + 1, y), h11 = cornerH(x + 1, y + 1), h01 = cornerH(x, y + 1);
            if (std::max(std::max(h00, h10), std::max(h11, h01)) < -0.75f) continue;   // (open water: the sea covers it)
            size_t i = (size_t)y * n + x;
            int mat;
            if (cave[i]) mat = TM_CAVEROCK;
            else {
                float mean = 0.25f * (h00 + h10 + h11 + h01);
                bool inner = true; for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (cellH(x + dx, y + dy) < 0) inner = false;
                mat = mean < 0.12f ? TM_WETSAND : inner && mean > 0.5f ? TM_JUNGLE : TM_SAND;
            }
            Vector3 p00{x * C, h00, y * C}, p10{(x + 1) * C, h10, y * C}, p11{(x + 1) * C, h11, (y + 1) * C}, p01{x * C, h01, (y + 1) * C};
            tb.Quad(mat, p00, p10, p11, p01, cornerN(x, y), cornerN(x + 1, y), cornerN(x + 1, y + 1), cornerN(x, y + 1), cornerAO(x, y), cornerAO(x + 1, y), cornerAO(x + 1, y + 1), cornerAO(x, y + 1));
            // (a palm here and there on the open land, as before; the Grotto's mould on its wall ledges)
            Vector2 wc{(x + 0.5f) * C, (y + 0.5f) * C};
            if (!cave[i] && H[i] > 0 && ((x * 7 + y * 13) % 23) == 0)
                gLandProps.push_back({"trawl/props/palm.glb", MatrixMultiply(MatrixMultiply(MatrixScale(0.9f + 0.1f * (x % 4), 0.9f + 0.1f * (x % 4), 0.9f + 0.1f * (x % 4)), MatrixRotateY(x * 1.3f + y * 0.7f)), MatrixTranslate(wc.x, H[i] - 0.05f, wc.y)), 0});
            if (grotto && e->depth[i] > 0.01f && e->hab[i] == H_WALL && ((x + y) % 2 == 0))
                gLandProps.push_back({"trawl/props/mould.glb", MatrixMultiply(MatrixRotateY(x * 2.1f + y), MatrixTranslate(wc.x, 0.6f + 0.5f * sinf(x * 2.1f + y), wc.y)), 0.6f});
        }
        // the grounds' scenery (tools/artgen/props.py): bushes in the jungle, rocks and reeds on the shore, kelp and
        // sargassum riding the swell, a wreck's ribs over the smugglers' wrecks, Atlantis's columns and arches over its
        // reef, stalagmites under the Grotto's walls. A hash per cell decides, so every client builds the same ground.
        {
            auto hsh = [](int x, int y, int salt) { uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u ^ (uint32_t)salt * 83492791u; h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15; return (h & 0xffff) / 65535.0f; };
            auto place = [&](const char* asset, Vector2 w, float y, float s, float yaw, bool floats, float far) {
                PropAt p{asset, MatrixMultiply(MatrixMultiply(MatrixScale(s, s, s), MatrixRotateY(yaw)), MatrixTranslate(w.x, y, w.y)), 0};
                p.floats = floats; p.far = far; gLandProps.push_back(p);
            };
            bool atl = e->ground == "atlantis", weeds = e->ground == "weeds";
            int wreckCells = 0;
            for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) {
                size_t i = (size_t)y * n + x;
                Vector2 wc{(x + 0.5f) * C, (y + 0.5f) * C};
                if (inLanding(wc)) continue;
                if ((x & 1) || (y & 1)) continue;   // (one cell in four: 8 m apart at most, so a few hundred near the boat, not thousands)
                Vector2 jit{(hsh(x, y, 1) - 0.5f) * C * 1.2f, (hsh(x, y, 2) - 0.5f) * C * 1.2f};
                Vector2 w = Vector2Add(wc, jit);
                float r = hsh(x, y, 3), yaw = hsh(x, y, 4) * 6.283f, sc = 0.8f + 0.5f * hsh(x, y, 5);
                int hab = e->hab[i]; float d = e->depth[i];
                bool shore = false; for (int dy = -1; dy <= 1 && !shore; dy++) for (int dx = -1; dx <= 1; dx++) { int xx = x + dx, yy = y + dy; if (xx >= 0 && yy >= 0 && xx < n && yy < n && (e->depth[(size_t)yy * n + xx] > 0.01f) != (d > 0.01f)) { shore = true; break; } }
                if (cave[i]) {
                    if (grotto && shore && d <= 0.01f && r < 0.35f) place("trawl/props/stalagmite.glb", w, 0, 1.2f + sc, yaw, false, 90);
                    continue;
                }
                if (d <= 0.01f) {   // the land
                    float mean = H[i];
                    bool inner = true; for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) if (cellH(x + dx, y + dy) < 0) inner = false;
                    if (inner && mean > 0.5f && r < 0.45f) place("trawl/props/bush.glb", w, mean - 0.05f, sc * 1.3f, yaw, false, 110);
                    else if (shore && r < 0.22f) place("trawl/props/shorerock.glb", w, 0.0f, sc * 1.2f, yaw, false, 140);
                    else if (shore && r < 0.34f && !atl) place("trawl/props/reeds.glb", w, 0.1f, sc, yaw, false, 70);
                    continue;
                }
                // the water
                if (hab == H_KELP && r < 0.4f) place("trawl/props/kelpfloat.glb", w, 0, sc * 1.4f, yaw, true, 90);
                else if (hab == H_SARGASSUM && r < 0.45f) place("trawl/props/sargassum.glb", w, 0, sc * 1.5f, yaw, true, 90);
                else if (hab == H_WRECK && (wreckCells++ % 3) == 0) place("trawl/props/wreckribs.glb", w, -0.2f, 1.4f, yaw, false, 200);
                else if (atl && (hab == H_REEF || hab == H_CREST) && r < 0.12f) place(r < 0.04f ? "trawl/props/archruin.glb" : "trawl/props/column.glb", w, -0.3f, sc * 1.4f, yaw, false, 220);
                else if (!weeds && !atl && hab == H_CREST && r < 0.1f) place("trawl/props/shorerock.glb", w, -0.1f, sc, yaw, false, 140);
                else if (weeds && hab == H_BARREN && r < 0.05f) place("trawl/props/shorerock.glb", w, -0.35f, sc * 0.9f, yaw, false, 140);
                else if (shore && r < 0.25f && !atl && !grotto) place("trawl/props/shorerock.glb", w, -0.3f, sc * 0.8f, yaw, false, 140);
            }
        }
        // (the Grotto) the cave's roof over its black water, its dripstones
        if (grotto) for (int y = 0; y < n; y++) for (int x = 0; x < n; x++) {
            size_t i = (size_t)y * n + x;
            Vector2 wc{(x + 0.5f) * C, (y + 0.5f) * C};
            if (!(cave[i] && e->depth[i] > 0.01f && wc.x > e->archX1)) continue;
            auto rh = [&](int xx, int yy) { return 24 + 4 * sinf(xx * 0.31f) * sinf(yy * 0.27f); };
            tb.QuadFlat(TM_CAVEROCK, {x * C, rh(x, y), y * C}, {(x + 1) * C, rh(x + 1, y), y * C}, {(x + 1) * C, rh(x + 1, y + 1), (y + 1) * C}, {x * C, rh(x, y + 1), (y + 1) * C}, 0.6f);
            if (((x * 11 + y * 17) % 13) == 0) {
                float len = 4 + 6 * fabsf(sinf(x * 1.7f + y));
                gLandProps.push_back({"trawl/props/stalactite.glb", MatrixMultiply(MatrixScale(2.8f, len, 2.8f), MatrixTranslate(wc.x, rh(x, y), wc.y)), 0});
            }
        }
        if (!tb.Build(&gTerrain)) gTerrain = Model{};
    }
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
// ---- the baked fish (tools/artgen/fish.py): a body archetype per kind of fish, recoloured per species ('back',
// 'belly', 'fin'), swum on its four-bone spine. Only fish near the eye get one (a per-frame budget); the rest, and
// anything that isn't a fish, keep the old unit model.
static const char* FishArchOf(const std::string& n) {
    struct A { const char* k; const char* a; };
    static const A T[] = {
        {"shark", "shark"}, {"dogfish", "shark"}, {"great white", "shark"}, {"sturgeon", "shark"}, {" ray", "ray"},
        {"halibut", "flat"}, {"swordfish", "billfish"}, {"marlin", "billfish"}, {"barracuda", "pike"}, {"lingcod", "pike"},
        {"eel", "eel"}, {"conger", "eel"}, {"hagfish", "eel"}, {"oarfish", "eel"},
        {"tuna", "tuna"}, {"bluefin", "tuna"}, {"bonito", "tuna"}, {"skipjack", "tuna"}, {"mackerel", "tuna"}, {"yellowtail", "tuna"},
        {"jack", "tuna"}, {"mahi", "tuna"}, {"silverside", "herring"}, {"sardine", "herring"}, {"anchovy", "herring"},
        {"flying fish", "herring"}, {"lantern fish", "herring"}, {"barreleye", "herring"},
        {"surgeon", "deep"}, {"trigger", "deep"}, {"opah", "deep"}, {"sunfish", "deep"},
        {"snapper", "perch"}, {"grunt", "perch"}, {"grouper", "perch"}, {"bass", "perch"}, {"sheephead", "perch"}, {"rockfish", "perch"},
        {"perch", "perch"}, {"opaleye", "perch"}, {"parrotfish", "perch"}, {" cod", "perch"}, {"cavefish", "perch"}, {"coelacanth", "perch"},
        {"angler", "perch"}};
    std::string s = " " + n;
    for (const auto& x : T) if (s.find(x.k) != std::string::npos) return x.a;
    return nullptr;
}
static const Model* FishModelOf(const std::string& species) {
    const char* a = FishArchOf(species);
    return a ? rt::LoadAsset(std::string("trawl/fish/fish_") + a + ".glb") : nullptr;
}
static void FishColours(const std::string& n, Color fallback, Color* back, Color* belly) {
    struct FC { const char* k; Color back, belly; };
    static const FC T[] = {
        {"mahi", {40, 120, 70, 255}, {210, 190, 60, 255}}, {"vermilion", {180, 50, 40, 255}, {225, 140, 120, 255}},
        {"snapper", {170, 64, 52, 255}, {225, 160, 150, 255}}, {"parrotfish", {40, 140, 130, 255}, {130, 200, 180, 255}},
        {"yellowfin", {25, 36, 70, 255}, {210, 205, 170, 255}}, {"bluefin", {22, 32, 72, 255}, {200, 205, 212, 255}},
        {"skipjack", {32, 42, 72, 255}, {205, 208, 214, 255}}, {"bonito", {42, 62, 82, 255}, {205, 208, 214, 255}},
        {"mackerel", {40, 92, 90, 255}, {215, 218, 218, 255}}, {"yellowtail", {70, 90, 104, 255}, {225, 212, 150, 255}},
        {"jack", {70, 86, 96, 255}, {210, 205, 170, 255}}, {"sheephead", {32, 26, 32, 255}, {200, 80, 66, 255}},
        {"gold", {170, 130, 40, 255}, {225, 195, 95, 255}}, {"grouper", {92, 82, 62, 255}, {160, 150, 120, 255}},
        {"bass", {72, 72, 62, 255}, {175, 170, 155, 255}}, {"great white", {92, 98, 106, 255}, {235, 235, 230, 255}},
        {"leopard", {124, 112, 90, 255}, {222, 212, 192, 255}}, {"shark", {92, 102, 112, 255}, {222, 222, 216, 255}},
        {"dogfish", {100, 100, 96, 255}, {210, 206, 196, 255}}, {" ray", {72, 62, 52, 255}, {222, 216, 210, 255}},
        {"ghost", {190, 192, 196, 255}, {232, 232, 232, 255}}, {"halibut", {92, 82, 62, 255}, {230, 226, 216, 255}},
        {"barracuda", {82, 96, 106, 255}, {214, 218, 222, 255}}, {"swordfish", {44, 42, 62, 255}, {185, 185, 195, 255}},
        {"marlin", {22, 42, 92, 255}, {205, 208, 218, 255}}, {"moray", {84, 92, 42, 255}, {150, 152, 82, 255}},
        {"conger", {200, 200, 196, 255}, {232, 232, 228, 255}}, {"opah", {164, 72, 82, 255}, {205, 145, 145, 255}},
        {"sunfish", {140, 142, 144, 255}, {205, 205, 205, 255}}, {"coelacanth", {42, 62, 92, 255}, {125, 142, 162, 255}},
        {"surgeon", {42, 62, 122, 255}, {84, 104, 152, 255}}, {"trigger", {92, 82, 62, 255}, {155, 145, 122, 255}},
        {"oarfish", {205, 205, 214, 255}, {225, 225, 232, 255}}, {"cave", {200, 196, 200, 255}, {230, 226, 230, 255}},
        {"pale", {196, 196, 192, 255}, {228, 228, 224, 255}}, {"lingcod", {92, 96, 70, 255}, {180, 182, 150, 255}},
        {"sturgeon", {120, 118, 110, 255}, {210, 208, 200, 255}}, {"angler", {60, 50, 46, 255}, {110, 96, 88, 255}}};
    std::string s = " " + n;
    for (const auto& x : T) if (s.find(x.k) != std::string::npos) { *back = x.back; *belly = x.belly; return; }
    *back = fallback;
    *belly = {(unsigned char)std::min(255, fallback.r / 2 + 115), (unsigned char)std::min(255, fallback.g / 2 + 118), (unsigned char)std::min(255, fallback.b / 2 + 120), 255};
}
static int gFishBudget = 0;   // the baked fish left this frame
static float gFlashNow = 0, gThunderPending = -1;   // lightning this frame; seconds until its thunder (-1: none)
static const Model& DropModel() { static Model m{}; if (m.meshCount == 0) m = LoadModelFromMesh(GenMeshSphere(0.5f, 5, 7)); return m; }   // (spray, splashes, drips)
// A baked fish: 'swim' is its phase, 'amp' how hard it beats its tail (a flop on deck is a big slow one); 'dim' the
// water's darkening. Returns false (and draws nothing) when there's no model or the frame's budget is spent.
static float gFishDull = 0, gFishCut = 0;   // for the next DrawFishPbr: dulled after death (0 fresh .. 1), opened by the knife (0 .. 1)
static bool gFishGlimmer = false;           // for the next DrawFishPbr: a Glimmer variant (it shimmers like oil on water)
static bool DrawFishPbr(const std::string& species, Vector3 p, Vector3 heading, float len, float roll, float swim, float amp, Color fallback, float dim = 1) {
    const float dull = gFishDull, cut = gFishCut; const bool glim = gFishGlimmer; gFishDull = gFishCut = 0; gFishGlimmer = false;   // (this call's, whatever happens below)
    if (gFishBudget <= 0 || getenv("DEPTH_OLDBOAT")) return false;
    const Model* m = FishModelOf(species);
    if (!m) return false;
    gFishBudget--;
    const rt::RigInfo& R = rt::RigOf(*m);
    rt::RigPose P; P.Reset((int)R.parent.size());
    for (int k = 0; k < 4; k++) {
        int b = R.Find(TextFormat("s%d", k));
        if (b >= 0) P.rot[b] = QuaternionFromAxisAngle({0, 1, 0}, amp * sinf(swim - k * 1.25f) * (0.25f + 0.3f * k));
    }
    // a ray flies on its wings: both beat together, slow and deep, and its tail barely swings
    int wl = R.Find("wing.L"), wr = R.Find("wing.R");
    if (wl >= 0 && wr >= 0) {
        float f = (0.25f + amp * 1.2f) * sinf(swim * 0.45f);
        P.rot[wl] = QuaternionFromAxisAngle({0, 0, 1}, f);
        P.rot[wr] = QuaternionFromAxisAngle({0, 0, 1}, -f);
    }
    auto sk = rt::SolveRig(R, P);
    float yaw = atan2f(heading.x, heading.z), pitch = atan2f(heading.y, sqrtf(heading.x * heading.x + heading.z * heading.z));
    Matrix w = MatrixMultiply(MatrixScale(len, len, len), MatrixRotateZ(roll));
    w = MatrixMultiply(w, MatrixRotateX(-pitch));
    w = MatrixMultiply(w, MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(p.x, p.y, p.z)));
    Color back, belly; FishColours(species, fallback, &back, &belly);
    // a fresh fish is bright and slick; dead, it dulls toward grey as it loses its freshness; opened, the belly is red
    auto grey = [](Color c, float k) { unsigned char g = (unsigned char)((c.r + c.g + c.b) / 3); return Color{(unsigned char)(c.r + (g - c.r) * k), (unsigned char)(c.g + (g - c.g) * k), (unsigned char)(c.b + (g - c.b) * k), 255}; };
    if (dull > 0) { back = Mul(grey(back, 0.55f * dull), 1 - 0.25f * dull); belly = Mul(grey(belly, 0.6f * dull), 1 - 0.3f * dull); }
    if (cut > 0) belly = ColorLerp(belly, Color{120, 22, 20, 255}, std::min(1.0f, cut * 1.4f));
    Color fin = Mul(back, 0.8f);
    if (glim) {   // a Glimmer: the colours run through the spectrum along the body like oil on water, and it glows faintly
        float tt = (float)GetTime(), hue = fmodf(tt * 45 + p.x * 30 + p.z * 20, 360.0f);
        back = ColorLerp(back, ColorFromHSV(hue, 0.7f, 0.95f), 0.55f); belly = ColorLerp(belly, ColorFromHSV(fmodf(hue + 120, 360.0f), 0.45f, 1.0f), 0.45f); fin = ColorFromHSV(fmodf(hue + 220, 360.0f), 0.8f, 0.9f);
        rt::AddLateLight(Vector3Add(p, {0, 0.25f, 0}), 1.2f + len, ColorFromHSV(hue, 0.6f, 1.0f), 0.5f);
    }
    unsigned char d = (unsigned char)std::clamp(dim * 255.0f, 0.0f, 255.0f);
    rt::DrawPbrSkinned(*m, w, sk, {{"back", back}, {"belly", belly}, {"fin", fin}}, 0.0f, Color{d, d, d, 255});
    return true;
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
int gSprayWarm = 0;   // frames of spray to run before the next draw (shots)
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
        case Item::Cup: mb.Tube({{0.06f, -0.06f, 0}, {0.06f, 0.06f, 0}}, 0.035f, 0.045f, 10, Color{196, 198, 194, 255}, Color{210, 212, 208, 255}, 0); mb.Box({0.06f, 0.055f, 0}, {0.04f, 0.004f, 0.04f}, Color{236, 206, 60, 255}); mb.Tube({{0.105f, 0.03f, 0}, {0.13f, 0.0f, 0}, {0.105f, -0.03f, 0}}, 0.008f, 0.008f, 4, Color{196, 198, 194, 255}, Color{196, 198, 194, 255}, 0); break;   // (a tin cup brimming yellow, its handle)
        case Item::Walkie: mb.Box({0.04f, 0.02f, 0}, {0.035f, 0.08f, 0.02f}, Color{52, 58, 50, 255}); mb.Box({0.04f, 0.075f, 0.021f}, {0.02f, 0.02f, 0.002f}, Color{150, 160, 140, 255}); mb.Tube({{0.065f, 0.1f, 0}, {0.065f, 0.24f, 0}}, 0.006f, 0.004f, 4, Color{30, 30, 30, 255}, Color{30, 30, 30, 255}, 0); break;   // (a field radio and its aerial)
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

static void DrawSkiff3D(const Gannet& g, float t) {
    const Skiff& s = g.skiff;
    if (s.state == SkiffState::Lost || gSkiff.meshCount == 0) return;
    Matrix M = SkiffMatrix(g);
    const bool baked = !getenv("DEPTH_OLDBOAT");
    const Model* skM = baked ? rt::LoadAsset("trawl/props/skiff.glb") : nullptr;
    const Model* oarM = baked ? rt::LoadAsset("trawl/props/oar.glb") : nullptr;
    if (skM) rt::DrawPbr(*skM, M); else rt::DrawStatic(gSkiff, M, WHITE);
    if (s.state == SkiffState::Capsized) return;
    if (s.Up()) Glow(Vector3Transform({1.95f, 0.85f, 0}, M), 0.12f, Color{255, 214, 140, 255}, 2.2f);   // the bow lantern
    // the oars: each rower's pair sweeps from the catch (blades forward) to the finish and feathers back
    for (const auto& c : g.crew) {
        if (c.deck != DECK_SKIFF || c.overboard || c.dead) continue;
        float ph = std::clamp(c.oarT / D().skiffStroke, 0.0f, 1.0f);
        float sweep = ph < 0.45f ? 0.55f - ph / 0.45f * 1.1f : -0.55f + (ph - 0.45f) / 0.55f * 1.1f;   // the pull, then the recovery
        float lift = ph < 0.45f ? -0.12f : 0.12f;
        for (int sd = -1; sd <= 1; sd += 2) {
            Vector3 lock{0.2f, 0.42f, sd * (SkiffHB(0.2f) - 0.02f)};
            // (the baked oar runs handle -z to blade +z: mirrored for the port one; the blade squares in the pull and
            // feathers flat on the recovery)
            Matrix shape = oarM ? MatrixMultiply(MatrixRotateZ(ph < 0.45f ? 0.0f : 1.4f), MatrixScale(1, 1, (float)sd)) : MatrixScale(0.04f, 0.04f, 2.6f);
            Matrix o = MatrixMultiply(MatrixMultiply(shape, MatrixTranslate(0, 0, sd * 0.95f)), MatrixRotateY(sd * sweep));
            o = MatrixMultiply(MatrixMultiply(o, MatrixRotateX(sd * (0.18f + lift))), MatrixTranslate(lock.x, lock.y, lock.z));
            if (oarM) rt::DrawPbr(*oarM, MatrixMultiply(o, M)); else rt::DrawCubeM(MatrixMultiply(o, M), Color{170, 130, 80, 255});
        }
    }
    // her line: the rod over the starboard quarter, bent by the fish, and the line to the lure or the fish
    {
        const Rod& r = g.skiffRod;
        bool lineUp = false; for (const auto& c : g.crew) if (c.deck == DECK_SKIFF && c.skiffLine && !c.overboard) lineUp = true;
        if (lineUp || r.state != RodState::Idle) {
            float bend = r.state == RodState::Fighting ? std::clamp(r.fight.tension / TackleOf(r.tackle).strength, 0.0f, 1.2f) : 0;
            Vector3 base = Vector3Transform({-1.2f, 0.5f, 0.5f}, M), tip = Vector3Transform({-1.5f, 1.9f - bend * 0.5f, 1.5f}, M);
            Seg(base, tip, 0.025f, Color{120, 86, 50, 255});
            if (r.state == RodState::Out) Seg(tip, W3({r.lure.x, r.lure.y}, g.sea.Height(r.lure.x, r.lure.y)), 0.008f, Color{210, 210, 190, 255});
            if (r.state == RodState::Fighting) { Vector3 fp = WD(r.fight.p); fp.y = std::min(fp.y, g.sea.Height(r.fight.p.x, r.fight.p.y)); Seg(tip, fp, 0.01f, Color{220, 220, 200, 255}); }
        }
        // the tow line astern of her quarter
        for (size_t i = 0; i < g.towed.size(); i++) {
            Vector3 a = Vector3Transform({-2.25f, 0.3f, 0}, M);
            Vector2 bw = g.skiff.ToWorld({-3.2f - i * 0.9f, 0.3f * (i % 2 ? 1 : -1)});
            Vector3 b = W3(bw, g.sea.Height(bw.x, bw.y) + 0.05f);
            Seg(a, b, 0.012f, Color{200, 190, 160, 255});
            float tl = std::clamp(0.4f + sqrtf(g.towed[i].kg) * 0.25f, 0.6f, 3.0f);
            if (!DrawFishPbr(g.towed[i].name, b, Vector3Normalize(Vector3Subtract(b, a)), tl, 1.5f, t * 2, 0.08f, Color{150, 158, 164, 255}, 0.8f))
                DrawFishAt(gFish, b, Vector3Normalize(Vector3Subtract(b, a)), tl, Color{150, 158, 164, 255}, 1.5f);
        }
    }
    // her load: fish and salvage in the bottom
    for (size_t i = 0; i < s.load.size() && i < 12; i++) {
        float x = -0.6f - (float)(i % 4) * 0.35f, z = -0.3f + (float)(i / 4) * 0.3f;
        float len = std::clamp(0.3f + sqrtf(s.load[i].kg) * 0.22f, 0.3f, 1.4f);
        if (s.load[i].junk) rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.25f, 0.18f, 0.2f), MatrixTranslate(x, -0.12f, z)), M), Color{110, 100, 80, 255});
        else if (!DrawFishPbr(s.load[i].name, Vector3Transform({x, -0.15f, z}, M), Vector3Normalize({1, 0, 0.2f}), len, 1.5f, 0, 0, Color{170, 178, 184, 255}, 0.85f))
            DrawFishAt(gFish, Vector3Transform({x, -0.15f, z}, M), Vector3Normalize({1, 0, 0.2f}), len, Color{170, 178, 184, 255}, 1.5f);
    }
}

// ---------------------------------------------------------------- a landing (the Atoll), in the world's frame
static const float ATOLL_Y = 0.6f;   // the sand's top over still water
static Model gAtoll[4]{}; static Vector2 gAtollFor[4]{{-1e9f, -1e9f}, {-1e9f, -1e9f}, {-1e9f, -1e9f}, {-1e9f, -1e9f}};
static void BuildAtoll(MeshBuilder& mb, const Landing& L) {
    bool seal = L.kind == LK_SEALROCK || L.kind == LK_SHELF || L.kind == LK_BONEBEACH, pier = L.kind == LK_CANNERY;
    bool stair = L.kind == LK_STAIR, tower = L.kind == LK_TOWER, cultL = L.kind == LK_CULT;
    Color sand = stair ? Color{196, 192, 182, 255} : tower ? Color{140, 136, 126, 255} : cultL ? Color{80, 70, 60, 255} : L.kind == LK_BONEBEACH ? Color{54, 50, 48, 255} : seal ? Color{118, 114, 106, 255} : pier ? Color{122, 94, 62, 255} : Color{196, 178, 132, 255};
    Color sandWet = seal ? Color{70, 72, 66, 255} : Color{150, 132, 96, 255}, pond = seal ? Color{66, 80, 70, 255} : Color{30, 96, 104, 255}, trunk{110, 82, 52, 255}, frond{58, 112, 56, 255};
    Color thatch{160, 128, 72, 255}, hutWall{120, 90, 56, 255}, hull{88, 62, 42, 255}, hullDk{56, 40, 28, 255}, stone{110, 108, 100, 255};
    auto W = [&](Vector2 l, float y) { return Vector3{L.at.x + l.x, y, L.at.y + l.y}; };
    const int N = 36;
    for (int k = 0; k < N; k++) {
        float a0 = k * 2 * PI / N, a1 = a0 + 2 * PI / N;
        Vector2 p0{cosf(a0), sinf(a0)}, p1{cosf(a1), sinf(a1)};
        mb.Tri(W({0, 0}, ATOLL_Y), W(Vector2Scale(p1, L.r), ATOLL_Y), W(Vector2Scale(p0, L.r), ATOLL_Y), sand);
        mb.Tri(W({0, 0}, ATOLL_Y), W(Vector2Scale(p0, L.r), ATOLL_Y), W(Vector2Scale(p1, L.r), ATOLL_Y), sand);
        if (pier) {
            // the stage's edge beam, and a piling every other step down into the water
            mb.Quad(W(Vector2Scale(p0, L.r), ATOLL_Y), W(Vector2Scale(p1, L.r), ATOLL_Y), W(Vector2Scale(p1, L.r), ATOLL_Y - 0.35f), W(Vector2Scale(p0, L.r), ATOLL_Y - 0.35f), hullDk);
            mb.Quad(W(Vector2Scale(p0, L.r), ATOLL_Y - 0.35f), W(Vector2Scale(p1, L.r), ATOLL_Y - 0.35f), W(Vector2Scale(p1, L.r), ATOLL_Y), W(Vector2Scale(p0, L.r), ATOLL_Y), hullDk);
            if (k % 2 == 0) mb.Tube({W(Vector2Scale(p0, L.r - 0.2f), -3.0f), W(Vector2Scale(p0, L.r - 0.2f), ATOLL_Y)}, 0.18f, 0.18f, 6, Color{58, 46, 34, 255}, Color{40, 46, 36, 255}, 0);
            if (k % 3 == 0) mb.Box(W(Vector2Scale(p0, L.r * 0.5f), ATOLL_Y + 0.005f), {L.r * 0.5f, 0.005f, 0.04f}, Color{96, 72, 48, 255});   // a seam between planks
            continue;
        }
        mb.Quad(W(Vector2Scale(p0, L.r), ATOLL_Y), W(Vector2Scale(p1, L.r), ATOLL_Y), W(Vector2Scale(p1, L.r + 2.2f), -0.9f), W(Vector2Scale(p0, L.r + 2.2f), -0.9f), sandWet);   // the beach running under
        mb.Quad(W(Vector2Scale(p0, L.r + 2.2f), -0.9f), W(Vector2Scale(p1, L.r + 2.2f), -0.9f), W(Vector2Scale(p1, L.r), ATOLL_Y), W(Vector2Scale(p0, L.r), ATOLL_Y), sandWet);
        // the little lagoon (Seal Rock: the weed-slick haul-out)
        Vector2 q0 = Vector2Add(L.pond, Vector2Scale(p0, L.pondR)), q1 = Vector2Add(L.pond, Vector2Scale(p1, L.pondR));
        mb.Tri(W(L.pond, ATOLL_Y + 0.02f), W(q1, ATOLL_Y + 0.02f), W(q0, ATOLL_Y + 0.02f), pond);
        mb.Tri(W(L.pond, ATOLL_Y + 0.02f), W(q0, ATOLL_Y + 0.02f), W(q1, ATOLL_Y + 0.02f), pond);
    }
    if (stair) {
        // the Drowned Stair: broad white treads climbing out of the sea to a landing, a shrine of two columns and a lintel
        for (int s = 0; s < 6; s++) mb.Box(W({-6.0f + s * 0.9f, 0}, ATOLL_Y - 0.8f + s * 0.3f), {0.45f, 0.15f + s * 0.15f, 6.5f}, Color{206, 202, 190, 255});
        for (int s = -1; s <= 1; s += 2) mb.Tube({W(Vector2Add(L.sloop, {s * 1.8f, 0}), ATOLL_Y), W(Vector2Add(L.sloop, {s * 1.8f, 0}), ATOLL_Y + 3.6f)}, 0.32f, 0.28f, 8, Color{214, 210, 198, 255}, Color{214, 210, 198, 255}, 0);
        mb.Box(W(L.sloop, ATOLL_Y + 3.8f), {2.4f, 0.25f, 0.6f}, Color{200, 196, 186, 255});
        mb.Tube({W(L.fire, ATOLL_Y), W(L.fire, ATOLL_Y + 0.9f)}, 0.5f, 0.7f, 8, Color{140, 110, 60, 255}, Color{150, 120, 66, 255}, 0);   // the brazier
        return;
    }
    if (tower) {
        // the Watchtower stump: a ring of broken masonry, the signal fire's stones
        mb.Tube({W(L.sloop, ATOLL_Y - 1), W(L.sloop, ATOLL_Y + 5.5f)}, 2.6f, 2.4f, 12, Color{150, 146, 136, 255}, Color{130, 126, 118, 255}, 0);
        for (int k = 0; k < 8; k++) { float a = k * 0.785f; mb.Box(W(Vector2Add(L.fire, {cosf(a) * 0.5f, sinf(a) * 0.5f}), ATOLL_Y + 0.08f), {0.12f, 0.08f, 0.12f}, stone); }
        return;
    }
    if (cultL) {
        // the Cult Landing: a red tent, carved posts with skulls, the bonfire's stones
        mb.Cone(W(L.sloop, ATOLL_Y), W(L.sloop, ATOLL_Y + 3.4f), 2.6f, 8, Color{120, 28, 28, 255});
        for (int k = 0; k < 4; k++) { float a = k * 1.57f + 0.4f; Vector2 pp = Vector2Add(L.fire, {cosf(a) * 3.2f, sinf(a) * 3.2f}); mb.Tube({W(pp, ATOLL_Y), W(pp, ATOLL_Y + 2.4f)}, 0.12f, 0.1f, 5, Color{70, 50, 36, 255}, Color{70, 50, 36, 255}, 0); mb.Box(W(pp, ATOLL_Y + 2.55f), {0.18f, 0.18f, 0.18f}, Color{220, 214, 196, 255}); }
        for (int k = 0; k < 8; k++) { float a = k * 0.785f; mb.Box(W(Vector2Add(L.fire, {cosf(a) * 0.5f, sinf(a) * 0.5f}), ATOLL_Y + 0.08f), {0.12f, 0.08f, 0.12f}, stone); }
        return;
    }
    if (seal) {
        // boulders on the rock, the sealers' stone hut with a turf roof, the iron stove by its door
        for (int i = 0; i < 9; i++) { float a = i * 2.39f, rr = 3 + fmodf(i * 3.7f, L.r - 4.5f); Vector2 b{cosf(a) * rr, sinf(a) * rr}; if (Vector2Distance(b, L.sloop) < 3.4f || Vector2Distance(b, L.fire) < 1.8f || Vector2Distance(b, L.elder) < 1.6f || Vector2Distance(b, L.pond) < L.pondR + 0.6f) continue; mb.Box(W(b, ATOLL_Y + 0.3f), {0.5f + 0.1f * (i % 3), 0.3f + 0.1f * (i % 2), 0.45f}, Color{96, 94, 88, 255}); }
        mb.Box(W(L.sloop, ATOLL_Y + 1.0f), {2.6f, 1.0f, 1.6f}, Color{104, 100, 92, 255});
        mb.Box(W(L.sloop, ATOLL_Y + 2.15f), {2.8f, 0.18f, 1.8f}, Color{86, 98, 60, 255});
        mb.Box(W(Vector2Add(L.sloop, {0, 1.62f}), ATOLL_Y + 0.8f), {0.6f, 0.8f, 0.02f}, Color{30, 24, 20, 255});   // the door
        mb.Box(W(L.fire, ATOLL_Y + 0.35f), {0.4f, 0.35f, 0.3f}, Color{46, 44, 42, 255});
        mb.Tube({W(Vector2Add(L.fire, {0.2f, 0}), ATOLL_Y + 0.7f), W(Vector2Add(L.fire, {0.2f, 0}), ATOLL_Y + 2.6f)}, 0.08f, 0.08f, 5, Color{40, 38, 36, 255}, Color{40, 38, 36, 255}, 0);
        return;
    }
    if (pier) {
        // the cannery shed (corrugated iron), its door; the boiler (a riveted drum on its side) and its stack
        mb.Box(W(L.sloop, ATOLL_Y + 1.4f), {2.6f, 1.4f, 1.6f}, Color{98, 84, 74, 255});
        mb.Box(W(L.sloop, ATOLL_Y + 2.9f), {2.75f, 0.12f, 1.75f}, Color{124, 114, 104, 255});
        for (int r2 = -2; r2 <= 2; r2++) mb.Box(W(Vector2Add(L.sloop, {(float)r2, 0}), ATOLL_Y + 3.03f), {0.05f, 0.03f, 1.75f}, Color{90, 82, 74, 255});
        mb.Box(W(Vector2Add(L.sloop, {0, 1.62f}), ATOLL_Y + 1.0f), {0.7f, 1.0f, 0.02f}, Color{30, 24, 20, 255});
        mb.Tube({W(Vector2Add(L.fire, {-1.0f, 0}), ATOLL_Y + 0.8f), W(Vector2Add(L.fire, {1.0f, 0}), ATOLL_Y + 0.8f)}, 0.75f, 0.75f, 10, Color{72, 64, 60, 255}, Color{56, 50, 48, 255}, 0);
        mb.Tube({W(Vector2Add(L.fire, {0.4f, 0}), ATOLL_Y + 1.4f), W(Vector2Add(L.fire, {0.4f, 0}), ATOLL_Y + 4.6f)}, 0.2f, 0.18f, 6, Color{40, 36, 34, 255}, Color{40, 36, 34, 255}, 0);
        return;
    }
    // palms: a leaning trunk and a crown of drooping fronds
    for (size_t i = 0; i < L.palms.size(); i++) {
        Vector2 b = L.palms[i]; float lean = 0.6f + 0.2f * (i % 3);
        Vector2 dir = Vector2Normalize(b.x == 0 && b.y == 0 ? Vector2{1, 0} : b);
        Vector3 base = W(b, ATOLL_Y), top = W(Vector2Add(b, Vector2Scale(dir, lean)), ATOLL_Y + 5.0f);
        mb.Tube({base, Vector3Lerp(base, top, 0.5f), top}, 0.16f, 0.1f, 5, trunk, trunk, 0);
        for (int f = 0; f < 7; f++) {
            float a = f * 0.9f + i;
            Vector3 tip{top.x + cosf(a) * 2.6f, top.y - 1.1f, top.z + sinf(a) * 2.6f};
            Vector3 side{-sinf(a) * 0.45f, 0, cosf(a) * 0.45f};
            Vector3 mid{(top.x + tip.x) / 2, top.y + 0.25f, (top.z + tip.z) / 2};
            mb.Tri(top, Vector3Add(mid, side), tip, frond); mb.Tri(top, tip, Vector3Add(mid, side), frond);
            mb.Tri(top, Vector3Subtract(mid, side), tip, frond); mb.Tri(top, tip, Vector3Subtract(mid, side), frond);
        }
    }
    // the elder's hut: low walls and a thatched cone
    Vector2 h = Vector2Add(L.elder, {0, -1.6f});
    mb.Box(W(h, ATOLL_Y + 0.8f), {1.1f, 0.8f, 1.1f}, hutWall);
    mb.Cone(W(h, ATOLL_Y + 1.6f), W(h, ATOLL_Y + 3.4f), 1.7f, 9, thatch);
    // the sloop on her side: a hull wedge, keel up to the sky
    {
        float c = cosf(L.sloopHead), s = sinf(L.sloopHead);
        auto P = [&](float x, float y, float z) { return W(Vector2Add(L.sloop, {x * c - z * s, x * s + z * c}), ATOLL_Y + y); };
        mb.Quad(P(2.6f, 0, 0), P(1.4f, 0, 0.9f), P(1.4f, 1.6f, 0.4f), P(2.6f, 1.0f, 0), hull);
        mb.Quad(P(1.4f, 0, 0.9f), P(-1.6f, 0, 0.9f), P(-1.6f, 1.6f, 0.4f), P(1.4f, 1.6f, 0.4f), hull);
        mb.Quad(P(2.6f, 0, 0), P(1.4f, 0, -0.9f), P(1.4f, 0.3f, -0.9f), P(2.6f, 0.3f, 0), hullDk);
        mb.Quad(P(1.4f, 1.6f, 0.4f), P(-1.6f, 1.6f, 0.4f), P(-1.6f, 0.3f, -0.9f), P(1.4f, 0.3f, -0.9f), hullDk);   // her deck, tipped toward the sand
        mb.Quad(P(1.4f, 0, -0.9f), P(-1.6f, 0, -0.9f), P(-1.6f, 0.3f, -0.9f), P(1.4f, 0.3f, -0.9f), hullDk);
    }
    for (int k = 0; k < 8; k++) { float a = k * 0.785f; mb.Box(W(Vector2Add(L.fire, {cosf(a) * 0.45f, sinf(a) * 0.45f}), ATOLL_Y + 0.08f), {0.12f, 0.08f, 0.12f}, stone); }
}
// the landings in baked props on terrain: the island's top in its kind's material (sand, rock, the Stair's marble, the
// cannery's planked stage), a beach skirt running under the water, and the props where BuildAtoll put its boxes
static Model gAtollT[4]{}, gAtollX[4]{};
static std::vector<PropAt> gAtollProps[4];
static void BuildAtollBaked(int li, const Landing& L) {
    bool seal = L.kind == LK_SEALROCK || L.kind == LK_SHELF || L.kind == LK_BONEBEACH, pier = L.kind == LK_CANNERY;
    bool stair = L.kind == LK_STAIR, tower = L.kind == LK_TOWER, cultL = L.kind == LK_CULT;
    bool lightH = L.kind == LK_LIGHTHOUSE, sandB = L.kind == LK_SANDBAR;
    int top = stair ? TM_MARBLE : tower || seal || lightH ? TM_ROCK : cultL || L.kind == LK_BONEBEACH ? TM_CAVEROCK : pier ? TM_DECK : TM_SAND;
    int skirt = pier ? -1 : stair || tower || seal || lightH ? TM_ROCK : cultL ? TM_CAVEROCK : TM_WETSAND;
    auto W = [&](Vector2 l, float y) { return Vector3{L.at.x + l.x, y, L.at.y + l.y}; };
    TerrainBuilder tb;
    const int N = 36, RINGS = 4;
    auto domeY = [&](float f) { return ATOLL_Y + (pier ? 0.0f : 0.18f * (1 - f * f)); };   // (a low crown in the middle)
    for (int k = 0; k < N; k++) {
        float a0 = k * 2 * PI / N, a1 = a0 + 2 * PI / N;
        Vector2 p0{cosf(a0), sinf(a0)}, p1{cosf(a1), sinf(a1)};
        for (int r = 0; r < RINGS; r++) {
            float f0 = (float)r / RINGS, f1 = (float)(r + 1) / RINGS;
            tb.QuadFlat(top, W(Vector2Scale(p0, L.r * f0), domeY(f0)), W(Vector2Scale(p1, L.r * f0), domeY(f0)), W(Vector2Scale(p1, L.r * f1), domeY(f1)), W(Vector2Scale(p0, L.r * f1), domeY(f1)), 1 - 0.15f * f1);
        }
        if (skirt >= 0) tb.QuadFlat(skirt, W(Vector2Scale(p0, L.r), ATOLL_Y), W(Vector2Scale(p1, L.r), ATOLL_Y), W(Vector2Scale(p1, L.r + 2.2f), -0.9f), W(Vector2Scale(p0, L.r + 2.2f), -0.9f), 0.8f);
        else tb.QuadFlat(TM_DECK, W(Vector2Scale(p0, L.r), ATOLL_Y), W(Vector2Scale(p1, L.r), ATOLL_Y), W(Vector2Scale(p1, L.r), ATOLL_Y - 0.35f), W(Vector2Scale(p0, L.r), ATOLL_Y - 0.35f), 0.7f);   // (the stage's edge)
    }
    if (gAtollT[li].meshCount > 0) UnloadTerrain(gAtollT[li]);
    if (!tb.Build(&gAtollT[li])) gAtollT[li] = Model{};
    // the bits that stay procedural: the little lagoon's surface, the stage's pilings
    {
        MeshBuilder mb;
        Color pond = seal ? Color{66, 80, 70, 255} : Color{30, 96, 104, 255};
        for (int k = 0; k < N && !pier && !stair && !tower && !lightH && !sandB; k++) {
            float a0 = k * 2 * PI / N, a1 = a0 + 2 * PI / N;
            Vector2 q0 = Vector2Add(L.pond, Vector2Scale({cosf(a0), sinf(a0)}, L.pondR)), q1 = Vector2Add(L.pond, Vector2Scale({cosf(a1), sinf(a1)}, L.pondR));
            mb.Tri(W(L.pond, ATOLL_Y + 0.2f), W(q1, ATOLL_Y + 0.2f), W(q0, ATOLL_Y + 0.2f), pond);
            mb.Tri(W(L.pond, ATOLL_Y + 0.2f), W(q0, ATOLL_Y + 0.2f), W(q1, ATOLL_Y + 0.2f), pond);
        }
        if (lightH) {   // the old lighthouse: a white tower, a red band, the lamp room's dark glass and its cap
            Vector3 b = W(L.sloop, ATOLL_Y);
            mb.Tube({b, Vector3Add(b, {0, 7.5f, 0})}, 2.1f, 1.6f, 14, Color{220, 216, 206, 255}, Color{200, 196, 186, 255}, 0);
            mb.Tube({Vector3Add(b, {0, 7.5f, 0}), Vector3Add(b, {0, 9.0f, 0})}, 1.6f, 1.5f, 14, Color{170, 50, 40, 255}, Color{150, 44, 36, 255}, 0);
            mb.Tube({Vector3Add(b, {0, 9.0f, 0}), Vector3Add(b, {0, 10.4f, 0})}, 1.2f, 1.2f, 12, Color{40, 50, 56, 255}, Color{30, 38, 44, 255}, 0);
            mb.Tube({Vector3Add(b, {0, 10.4f, 0}), Vector3Add(b, {0, 11.4f, 0})}, 1.45f, 0.2f, 12, Color{60, 64, 60, 255}, Color{40, 44, 40, 255}, 0);
        }
        if (pier) for (int k = 0; k < N; k += 2) { float a0 = k * 2 * PI / N; Vector2 p0{cosf(a0), sinf(a0)}; mb.Tube({W(Vector2Scale(p0, L.r - 0.2f), -3.0f), W(Vector2Scale(p0, L.r - 0.2f), ATOLL_Y)}, 0.18f, 0.18f, 6, Color{58, 46, 34, 255}, Color{40, 46, 36, 255}, 0); }
        if (gAtollX[li].meshCount > 0) UnloadModel(gAtollX[li]);
        gAtollX[li] = LoadModelFromMesh(mb.Build());
    }
    // the props
    auto& P = gAtollProps[li]; P.clear();
    auto at = [&](const char* a, Vector2 l, float y, float yaw = 0, float s = 1, float glow = 0) { P.push_back({a, MatrixMultiply(MatrixMultiply(MatrixScale(s, s, s), MatrixRotateY(yaw)), MatrixTranslate(L.at.x + l.x, y, L.at.y + l.y)), glow}); };
    float gy = ATOLL_Y + 0.12f;
    if (stair) { at("trawl/props/stair.glb", {0, 0}, ATOLL_Y); at("trawl/props/shrine.glb", L.sloop, ATOLL_Y); at("trawl/props/brazier.glb", L.fire, gy); return; }
    if (tower) { at("trawl/props/tower.glb", L.sloop, ATOLL_Y); at("trawl/props/firering.glb", L.fire, gy); return; }
    if (lightH) { at("trawl/props/stove.glb", L.fire, gy); for (int i = 0; i < 6; i++) { float a = i * 1.1f + 0.5f; Vector2 b{cosf(a) * 6.0f, sinf(a) * 6.0f}; at("trawl/props/boulder.glb", b, gy - 0.05f, a, 0.8f + 0.2f * (i % 3)); } return; }   // (the keeper's hearth: a stove in the tower's lee)
    if (sandB) return;   // (bare sand)
    if (cultL) {
        at("trawl/props/tent.glb", L.sloop, ATOLL_Y); at("trawl/props/firering.glb", L.fire, gy);
        for (int k = 0; k < 4; k++) { float a = k * 1.57f + 0.4f; at("trawl/props/skullpost.glb", Vector2Add(L.fire, {cosf(a) * 3.2f, sinf(a) * 3.2f}), gy, -a); }
        return;
    }
    if (seal) {
        for (int i = 0; i < 9; i++) {
            float a = i * 2.39f, rr = 3 + fmodf(i * 3.7f, L.r - 4.5f); Vector2 b{cosf(a) * rr, sinf(a) * rr};
            if (Vector2Distance(b, L.sloop) < 3.4f || Vector2Distance(b, L.fire) < 1.8f || Vector2Distance(b, L.elder) < 1.6f || Vector2Distance(b, L.pond) < L.pondR + 0.6f) continue;
            at("trawl/props/boulder.glb", b, gy - 0.05f, a, 0.9f + 0.25f * (i % 3));
        }
        at("trawl/props/stonehut.glb", L.sloop, gy); at("trawl/props/stove.glb", L.fire, gy);
        return;
    }
    if (pier) { at("trawl/props/cannery.glb", L.sloop, ATOLL_Y); at("trawl/props/boiler.glb", L.fire, ATOLL_Y); return; }
    for (size_t i = 0; i < L.palms.size(); i++) {   // palms leaning out toward the sea
        Vector2 b = L.palms[i];
        Vector2 dir = Vector2Normalize(b.x == 0 && b.y == 0 ? Vector2{1, 0} : b);
        at("trawl/props/palm.glb", b, gy - 0.05f, -atan2f(dir.y, dir.x), 0.9f + 0.1f * (i % 3));
    }
    at("trawl/props/hut.glb", Vector2Add(L.elder, {0, -1.6f}), gy, PI);
    at("trawl/props/beached_sloop.glb", L.sloop, ATOLL_Y, -L.sloopHead);
    at("trawl/props/firering.glb", L.fire, gy);
}
static bool DrawElder(int kind, Vector3 at, float yaw, float t);   // (below, with the sailors)
// the working clutter of a boat (tools/artgen/props.py), against the bulwarks and in the corners, clear of every
// station and the walkways: purely for the look (nothing collides with them). M: the boat's frame.
static void DrawDeckClutter(Matrix M) {
    if (getenv("DEPTH_OLDBOAT") || getenv("DEPTH_NOCLUTTER")) return;
    struct Bit { const char* m; float x, y, z, yaw; };
    static const Bit BITS[] = {
        {"bucket", -2.95f, 0, 2.42f, 0.4f}, {"bucket", 6.25f, 0, -1.95f, 1.3f}, {"mop", 6.55f, 0, -1.6f, -PI / 2 + 0.2f},
        {"lobsterpot", 7.55f, 0, 1.55f, 0.15f}, {"lobsterpot", 7.5f, 0.28f, 1.55f, -0.1f}, {"lobsterpot", 6.85f, 0, 1.7f, 0.5f},
        {"fishbox", -3.25f, 0, -2.42f, 0.05f}, {"fishbox", -3.2f, 0.25f, -2.4f, -0.12f}, {"fishbox", -3.9f, 0, -2.45f, 0.1f},
        {"crate", -8.9f, 0, -2.0f, 0.2f}, {"tacklebox", -1.35f, 0, 2.38f, -0.3f}, {"oilcan", -2.9f, 0, -1.95f, 0.8f},
        {"ropecoil", 4.4f, 0, 2.4f, 0.0f}, {"oilskin", 0.84f, 1.78f, -1.55f, PI}, {"oilskin", 0.84f, 1.78f, -1.05f, PI},
        {"netpile", -8.7f, 0, 1.55f, 0.4f}, {"bucket", -8.3f, 0, 2.35f, 2.0f},
    };
    for (const auto& bt : BITS) if (const Model* pm = rt::LoadAsset(std::string("trawl/props/") + bt.m + ".glb"))
        rt::DrawPbr(*pm, MatrixMultiply(MatrixMultiply(MatrixRotateY(-bt.yaw), MatrixTranslate(bt.x, DECK_Y + bt.y, bt.z)), M));
}
static const Model& PuffModel() { static Model m{}; if (m.meshCount == 0) m = LoadModelFromMesh(GenMeshSphere(0.5f, 9, 12)); return m; }   // (smoke)
static void DrawLanding3D(const Gannet& g, float t) {
    for (size_t li = 0; li < g.landings.size() && li < 4; li++) {
        const Landing& L = g.landings[li];
        if (L.flooded) continue;   // (the Sandbar under the tide)
        if (gAtollFor[li].x != L.at.x || gAtollFor[li].y != L.at.y) {
            if (gAtoll[li].meshCount > 0) UnloadModel(gAtoll[li]);
            MeshBuilder mb; BuildAtoll(mb, L); gAtoll[li] = LoadModelFromMesh(mb.Build()); gAtollFor[li] = L.at;
            BuildAtollBaked((int)li, L);
        }
        if (gAtollT[li].meshCount > 0 && !getenv("DEPTH_OLDBOAT")) {
            rt::DrawPbr(gAtollT[li], MatrixIdentity());
            if (gAtollX[li].meshCount > 0) rt::DrawStatic(gAtollX[li], MatrixIdentity(), WHITE);
            for (const auto& pa : gAtollProps[li]) if (const Model* pm = rt::LoadAsset(pa.asset)) rt::DrawPbr(*pm, pa.m);
        } else rt::DrawStatic(gAtoll[li], MatrixIdentity(), WHITE);
        auto W = [&](Vector2 l, float y) { return Vector3{L.at.x + l.x, y, L.at.y + l.y}; };
        // the fire, the fish on it, and the smoke going grey and black as they burn
        float worst = 0;
        for (size_t i = 0; i < L.onFire.size(); i++) {
            const CatchRec& r = L.onFire[i]; float T = 10 + r.kg; worst = std::max(worst, r.cookT / (T + 10));
            float len = std::clamp(0.3f + sqrtf(r.kg) * 0.22f, 0.3f, 1.2f);
            Vector3 at = W(Vector2Add(L.fire, {(float)i * 0.2f - 0.3f, 0}), ATOLL_Y + 0.5f);
            Color raw = r.cookT > T + 5 ? Color{50, 36, 26, 255} : r.cookT > T ? Color{200, 140, 70, 255} : Color{200, 200, 196, 255};
            gFishDull = std::clamp(r.cookT / (T + 5), 0.0f, 1.0f);   // (it browns, then blackens, on the stick)
            if (!DrawFishPbr(r.name, at, {1, 0, 0.1f}, len, 1.5f, 0, 0, raw, r.cookT > T + 5 ? 0.25f : r.cookT > T ? 0.6f : 0.9f)) DrawFishAt(gFish, at, {1, 0, 0.1f}, len, raw, 1.5f);
        }
        // the smoke: low-poly puffs rising, swelling and thinning away, grey to black as what's on the fire burns
        // (in the world, so the palms and the huts stand in front of it; a thin wisp from any lit fire)
        if (L.fireLit && L.kind != LK_STAIR) {
            Color sm = worst > 0.75f ? Color{22, 21, 20, 255} : worst > 0.5f ? Color{48, 47, 46, 255} : Color{70, 70, 70, 255};
            bool cooking = !L.onFire.empty();
            int n = cooking ? 90 : 45;
            float rise = cooking ? 5.0f : 3.2f;
            for (int k = 0; k < n; k++) {
                float u = fmodf(t * 0.16f + fmodf(k * 0.618034f, 1.0f), 1.0f);   // (scattered through the column, not in step)
                float jx = sinf(k * 12.9898f), jz = cosf(k * 78.233f);   // (each wisp its own drift off the line, the breeze leaning it all)
                Vector3 p = W(Vector2Add(L.fire, {u * 2.2f + (jx * 0.6f + 0.3f * sinf(u * 5 + t * 0.6f)) * u, (jz * 0.6f + 0.25f * cosf(u * 4 + t * 0.5f)) * u}), ATOLL_Y + 0.6f + u * rise);
                // (a haze of small wisps curling out of the flames, swelling as they rise and thinning to nothing)
                float s = (cooking ? 0.06f : 0.045f) + u * (cooking ? 0.22f : 0.13f);
                s *= std::min(1.0f, (1 - u) * 2.2f) * std::min(1.0f, u * 6 + 0.35f);
                rt::DrawStatic(PuffModel(), MatrixMultiply(MatrixMultiply(MatrixScale(s, s * 0.8f, s * 1.1f), MatrixRotateY(k * 1.3f)), MatrixTranslate(p.x, p.y, p.z)), sm);
            }
            // the flames themselves: licking, lit-from-within tongues over the embers
            for (int k = 0; k < 7; k++) {
                float ph = fmodf(t * 1.8f + k * 0.37f, 1.0f);
                Vector3 p = W(Vector2Add(L.fire, {sinf(k * 2.1f + t * 3) * 0.12f, cosf(k * 1.3f + t * 2) * 0.1f}), ATOLL_Y + 0.12f + ph * 0.5f);
                float s = 0.16f * (1 - ph) + 0.03f;
                rt::DrawStaticGlow(DropModel(), MatrixMultiply(MatrixScale(s, s * 1.6f, s), MatrixTranslate(p.x, p.y, p.z)), k % 3 ? Color{255, 180, 70, 255} : Color{255, 110, 40, 255}, 2.0f * (1 - ph) + 0.4f);
            }
        }
        // the elder before his hut
        if (L.kind != LK_TOWER && L.kind != LK_LIGHTHOUSE && L.kind != LK_SANDBAR) {   // (nobody keeps the Watchtower, the lighthouse or the bar)
            if (!DrawElder(L.kind, W(L.elder, ATOLL_Y), 0.5f + 0.2f * sinf(t * 0.3f), t)) {
                // (no sailor art: the old figure) the elder; Old Hoskins in yellow oilskins; the foreman in a leather
                // apron; the quartermaster; the hermit; the Keeper of the Stair, pale and drowned; the cult quartermaster
                Color cl = L.kind == LK_SEALROCK ? Color{176, 154, 62, 255} : L.kind == LK_CANNERY ? Color{96, 70, 50, 255} : L.kind == LK_SHELF ? Color{44, 44, 54, 255}
                         : L.kind == LK_BONEBEACH ? Color{110, 100, 84, 255} : L.kind == LK_STAIR ? Color{150, 176, 170, 255} : L.kind == LK_CULT ? Color{120, 26, 26, 255} : Color{170, 110, 80, 255};
                Matrix fr = Frame(W(L.elder, ATOLL_Y), 0.5f + 0.2f * sinf(t * 0.3f));
                rt::DrawStatic(gBody[(int)Role::Medic], fr, cl);
                for (int s = -1; s <= 1; s += 2) rt::DrawStatic(gArm[(int)Role::Medic], MatrixMultiply(MatrixMultiply(MatrixRotateZ(0.3f * s + 0.1f * sinf(t)), MatrixTranslate(0, 1.38f, s * 0.27f)), fr), cl);
            }
            if (L.kind == LK_ATOLL) Glow(W(L.elder, ATOLL_Y + 2.05f), 0.05f, Color{240, 70, 50, 255}, 0.4f);   // his feathers
        }
        if (L.kind == LK_STAIR) Glow(W(L.fire, ATOLL_Y + 1.2f + 0.1f * sinf(t * 5)), 0.35f, Color{170, 240, 210, 255}, 2.0f);   // the eternal brazier's pale flame
        else if ((L.kind == LK_TOWER || L.kind == LK_CULT) && L.fireLit) Glow(W(L.fire, ATOLL_Y + 0.4f + 0.1f * sinf(t * 9)), L.kind == LK_TOWER ? 0.8f : 0.5f, Color{255, 150, 60, 255}, 2.5f);   // the signal fire, the bonfire
        else if (L.kind != LK_ATOLL && L.fireLit) Glow(W(Vector2Add(L.fire, {0, L.kind == LK_CANNERY ? 0.78f : 0.32f}), ATOLL_Y + 0.35f), 0.18f, Color{255, 150, 60, 255}, 1.2f);   // the firebox door
        // a modelled prop on the sand (tools/artgen/props.py), or the old box if the art is missing
        auto prop = [&](const char* name, Vector2 l, float yaw, float scale, float y, Vector3 boxSize, Color boxCol) {
            if (const Model* pm = getenv("DEPTH_OLDBOAT") ? nullptr : rt::LoadAsset(std::string("trawl/props/") + name + ".glb"))
                rt::DrawPbr(*pm, MatrixMultiply(MatrixMultiply(MatrixScale(scale, scale, scale), MatrixRotateY(-yaw)), MatrixTranslate(L.at.x + l.x, ATOLL_Y + y, L.at.y + l.y)));
            else rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(boxSize.x, boxSize.y, boxSize.z), MatrixRotateY(-yaw)), MatrixTranslate(L.at.x + l.x, ATOLL_Y + boxSize.y * 0.5f, L.at.y + l.y)), boxCol);
        };
        if (L.kind == LK_SEALROCK) {   // the seals on their haul-out, the bull among them (raising his head now and then)
            for (int k = 0; k < 4; k++) { Vector2 sp = Vector2Add(L.pond, {cosf(k * 1.7f) * 1.6f, sinf(k * 1.7f) * 1.3f}); prop("seal", sp, k * 1.1f + 0.1f * sinf(t * 0.4f + k), 0.95f + 0.1f * (k % 2), 0, {1.3f, 0.35f, 0.5f}, Color{96, 90, 84, 255}); }
            prop("seal", L.moray, 2.6f, 1.55f, 0.03f * sinf(t * 2), {2.0f, 0.6f, 0.8f}, Color{70, 62, 56, 255});
        }
        for (const auto& k : L.caches) {
            if (k.kind == 2 && !k.found) continue;
            if (k.kind == 2 && !k.open) { rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.9f, 0.03f, 0.12f), MatrixRotateY(0.785f)), MatrixTranslate(L.at.x + k.p.x, ATOLL_Y + 0.03f, L.at.y + k.p.y)), Color{200, 40, 30, 255}); rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.9f, 0.03f, 0.12f), MatrixRotateY(-0.785f)), MatrixTranslate(L.at.x + k.p.x, ATOLL_Y + 0.03f, L.at.y + k.p.y)), Color{200, 40, 30, 255}); continue; }   // (X marks the spot, painted on the sand)
            if (k.open) continue;
            prop("seachest", k.p, k.p.x * 1.3f, 1.0f, -0.04f, {0.8f, 0.5f, 0.5f}, Color{120, 80, 40, 255});
        }
        for (size_t c = 0; c < L.crabs.size(); c++) prop("crab", L.crabs[c], t * 0.3f + c * 1.9f, 1.3f, 0, {0.3f, 0.12f, 0.22f}, Color{200, 90, 60, 255});
        for (const auto& b : L.onBeach) {
            if (b.junk) prop("crate", b.deckAt, b.deckAt.x * 0.7f, 0.8f, 0, {0.6f, 0.4f, 0.4f}, Color{120, 80, 40, 255});
            else { Vector3 at = W(b.deckAt, ATOLL_Y + 0.08f); float len = std::clamp(0.3f + sqrtf(b.kg) * 0.22f, 0.3f, 1.4f); gFishDull = 0.5f; if (!DrawFishPbr(b.name, at, {1, 0, 0.3f}, len, 1.5f, 0, 0, Color{170, 178, 184, 255})) DrawFishAt(gFish, at, {1, 0, 0.3f}, len, Color{170, 178, 184, 255}, 1.5f); }
        }
        // what a landing leaves lying about: driftwood on every beach, a lobster pot and a bucket by a lived-in hut
        {
            uint32_t h = (uint32_t)(L.at.x * 73856093.0f) ^ (uint32_t)(L.at.y * 19349663.0f);
            auto R = [&]() { h = h * 1664525u + 1013904223u; return (h >> 8) / 16777216.0f; };
            if (L.kind != LK_STAIR) for (int k = 0; k < 3; k++) { float a = R() * 6.283f, rr = 5 + R() * 4; prop("driftwood", {cosf(a) * rr, sinf(a) * rr}, R() * 6.283f, 0.8f + R() * 0.6f, 0, {1.6f, 0.1f, 0.15f}, Color{140, 128, 110, 255}); }
            if (L.kind == LK_ATOLL || L.kind == LK_SEALROCK || L.kind == LK_CANNERY || L.kind == LK_BONEBEACH) {
                prop("lobsterpot", Vector2Add(L.elder, {1.6f, 0.9f}), 0.4f, 1, 0, {0.6f, 0.4f, 0.4f}, Color{110, 80, 40, 255});
                prop("bucket", Vector2Add(L.elder, {1.2f, -0.8f}), 1.0f, 1, 0, {0.3f, 0.3f, 0.3f}, Color{120, 124, 126, 255});
                prop("netpile", Vector2Add(L.elder, {-1.8f, 1.2f}), 2.0f, 1, 0, {1.0f, 0.3f, 0.7f}, Color{90, 76, 52, 255});
            }
        }
        if (L.kind == LK_ATOLL) rt::DrawCubeM(MatrixMultiply(MatrixScale(1.2f, 0.08f, 0.2f), MatrixTranslate(L.at.x + L.moray.x, ATOLL_Y - 0.05f, L.at.y + L.moray.y)), Color{24, 34, 26, 255});   // the moray, dark under the surface
    }
}

// a hand: lying down when fallen, treading water overboard, pale and adrift when a ghost
// ---------------------------------------------------------------- the sailors (Visual Overhaul phase 3)
// Every hand, player or bot, is the shared skinned rig in its role's outfit (tools/artgen/crew.py), posed here in code
// and given an identity from its slot: a skin tone, outfit shades, a build and a head shape, the same on every client.
struct SailorLook { Role role = Role::Bosun; Color skin{}, top{}, trousers{}, hat{}, hair{}; float build = 1, height = 1, headW = 1, headH = 1; int beard = 0; const char* costume = nullptr; float wet = 0, blood = 0; };   // costume: a model of skins::Costume   // beard: 0 none, 1 full, 2 moustache, 3 chops
static int gLocalSlot = -1;   // your own hand's slot (DrawTrawl3D): it wears the Wardrobe's skin
static SailorLook LookOf(const Crew& c) {
    SailorLook L; L.role = c.role;
    uint32_t h = (uint32_t)c.slot * 2654435761u + 0x9E37u; auto R = [&]() { h ^= h << 13; h ^= h >> 17; h ^= h << 5; return (h & 0xffff) / 65535.0f; };
    static const Color SKIN[6] = {{238, 196, 160, 255}, {222, 170, 130, 255}, {198, 140, 104, 255}, {160, 108, 76, 255}, {118, 78, 54, 255}, {84, 56, 40, 255}};
    L.skin = SKIN[(int)(R() * 5.99f)];
    auto shade = [&](Color c, float k) { return Color{(unsigned char)std::clamp(c.r * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.g * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.b * k, 0.0f, 255.0f), 255}; };
    // the role's colours (spec: faded yellow, tar black, oxblood, bottle green oilskins; navy, khaki, waxed brown), varied
    static const Color OIL[4] = {{214, 168, 52, 255}, {48, 46, 44, 255}, {112, 40, 34, 255}, {44, 78, 54, 255}};
    switch (c.role) {
        case Role::Angler: {   // (the user's reference: a shirt, dark trousers, a bucket hat, the orange life vest over it)
            static const Color SHIRT[4] = {{70, 88, 120, 255}, {140, 50, 44, 255}, {96, 100, 86, 255}, {170, 160, 140, 255}};
            static const Color BUCKET[3] = {{120, 112, 76, 255}, {70, 84, 60, 255}, {160, 140, 100, 255}};
            L.top = SHIRT[(int)(R() * 3.99f)]; L.trousers = shade({42, 52, 78, 255}, 0.85f + 0.3f * R()); L.hat = BUCKET[(int)(R() * 2.99f)]; (void)OIL; break;
        }
        case Role::Bosun: L.top = shade({40, 52, 86, 255}, 0.85f + 0.3f * R()); L.trousers = shade({150, 134, 104, 255}, 0.85f + 0.3f * R()); L.hat = shade(L.top, 0.8f); break;
        case Role::Diver: {   // (the user's reference: an orange boiler suit)
            static const Color SUIT[3] = {{220, 96, 32, 255}, {200, 70, 30, 255}, {230, 130, 40, 255}};
            L.top = SUIT[(int)(R() * 2.99f)]; L.trousers = L.top; L.hat = L.top; break;
        }
        default: L.top = shade({86, 74, 50, 255}, 0.85f + 0.3f * R()); L.trousers = {58, 54, 48, 255}; L.hat = L.top; break;
    }
    L.build = 0.92f + 0.2f * R(); L.height = 0.95f + 0.1f * R();
    L.headW = 0.98f + 0.07f * R(); L.headH = 0.98f + 0.07f * R();   // (heads in proportion: the user found the oversized ones uncanny, 2026-10-05)
    static const Color HAIR[5] = {{40, 30, 24, 255}, {84, 56, 34, 255}, {150, 104, 60, 255}, {170, 160, 150, 255}, {120, 52, 30, 255}};
    L.hair = HAIR[(int)(R() * 4.99f)];
    float bd = R(); L.beard = bd < 0.35f ? 0 : bd < 0.6f ? 1 : bd < 0.82f ? 2 : 3;
    // the skin and costume each player wears (skins.h): yours straight from your Wardrobe, everyone else's as their
    // CMD_WARDROBE told the boat (Crew::skin, Crew::costume)
    const skins::Skin* sk = c.slot == gLocalSlot ? skins::Find(skins::TRAWL, skins::Get(skins::TRAWL).worn) : skins::Find(skins::TRAWL, c.skin);
    if (sk) { L.top = sk->top; L.trousers = sk->trousers; L.hat = sk->hat; }
    const skins::Costume* co = c.slot == gLocalSlot ? skins::WornCostume(skins::TRAWL) : skins::FindCostume(skins::TRAWL, c.costume);
    if (co) { L.costume = co->model; L.top = co->sleeve; }
    // wet and bloodied (the Visual Overhaul Spec, characters): soaked after a swim (darker, drying over two minutes);
    // blood from the gutting table (it builds while they work there and fades over a minute; a swim rinses it) or a wound
    {   static float bloodK[16] = {}; static double lastT[16] = {}; int s = std::clamp(c.slot, 0, 15); double now = GetTime(); float dt = (float)std::min(0.25, now - lastT[s]); lastT[s] = now;
        bool gutting = c.station >= 0 && c.station < (int)Stations().size() && Stations()[c.station].kind == StationKind::Gutting;
        bloodK[s] = c.overboard ? 0.0f : std::clamp(bloodK[s] + (gutting ? dt * 0.06f : -dt / 60.0f), 0.0f, 0.85f);
        L.blood = std::max(bloodK[s], c.bleedT > 0 ? 0.6f : 0.0f);
        L.wet = c.overboard ? 1.0f : std::clamp(c.wetT / 120.0f, 0.0f, 1.0f); }
    return L;
}
using SailorPose = fig::Pose;   // (the pose and its IK live in figure3d.cpp, shared with Red Tide's divers)
static const Model* SailorModel(Role r) {
    static const char* F[4] = {"shared/crew/crew_bosun.glb", "shared/crew/crew_angler.glb", "shared/crew/crew_diver.glb", "shared/crew/crew_medic.glb"};
    return rt::LoadAsset(F[std::clamp((int)r, 0, 3)]);
}
// the pose: rotations in the model's axes (x forward, y up, z the figure's right) about each bind joint
static std::vector<Matrix> PoseSailor(const Model& m, const SailorLook& L, const SailorPose& P, float t) {
    return fig::PoseFigure(m, fig::Build{L.build, L.height, L.headW, L.headH}, P, t);   // (the shared figure: figure3d.cpp)
}
// Each catalogue weapon is a baked model with moving parts (tools/artgen/weapons*.py): assets/shared/weapons/<id>.glb.
// The old starter items wear their catalogue counterparts.
static std::string WeaponModelId(const Slot& s) {
    if (s.it == Item::Weapon && s.wpn >= 0 && s.wpn < (int)Weapons().size()) return Weapons()[s.wpn].id;
    switch (s.it) {
        case Item::Rifle: return "carbine"; case Item::Shotgun: return "shotgun"; case Item::Speargun: return "speargun";
        case Item::Flare: return "flarepistol"; case Item::Gaff: return "gaff"; case Item::Priest: return "priest";
        case Item::Knife: return "knife"; case Item::Charge: return "depthcharge"; default: return "";
    }
}
static const Model* WeaponModel(const Slot& s) {
    std::string id = WeaponModelId(s);
    if (id.empty()) return nullptr;
    return rt::LoadAsset("shared/weapons/" + id + ".glb");
}
// what the gun is doing: a shot's first instant (the hammer falls, the trigger is in), the action cycling after it,
// a reload's progress (0..1, or -1), the rounds fired from the cylinder or drum, and whether it is loaded
struct GunAnim { float fire = 0, cycle = 0, reload = -1; int steps = 0; bool loaded = true; };
static GunAnim GunAnimOf(const Crew& c) {
    GunAnim a;
    const Slot& s = c.slots[c.sel];
    float coolMax = 1.0f; int mag = 1;
    if (s.it == Item::Weapon && s.wpn >= 0 && s.wpn < (int)Weapons().size()) { const WeaponDef& w = Weapons()[s.wpn]; coolMax = std::max(0.15f, WeaponCooldown(w, s.att)); mag = std::max(1, WeaponMagazine(w, s.att)); }
    else { coolMax = s.it == Item::Rifle ? 1.2f : s.it == Item::Shotgun ? 0.8f : s.it == Item::Speargun ? 2.0f : 1.0f; mag = s.it == Item::Rifle ? 8 : s.it == Item::Shotgun ? 2 : 1; }
    if (c.cool > 0) { float since = coolMax - c.cool; a.fire = std::clamp(1 - since * 8, 0.0f, 1.0f); a.cycle = std::clamp(since / coolMax, 0.0f, 1.0f); }
    float reloadLen = s.it == Item::Speargun ? 1.2f : 1.8f;
    if (c.reloadT > 0) a.reload = std::clamp(1 - c.reloadT / reloadLen, 0.0f, 1.0f);
    a.steps = std::max(0, mag - s.ammo);
    a.loaded = s.ammo > 0 || mag <= 0;
    return a;
}
static float GroupValue(const std::string& g, const GunAnim& a) {
    auto open = [&](float r0, float r1, float r2, float r3) { return a.reload < 0 ? 0.0f : (r0 >= r1 ? 1.0f : std::clamp((a.reload - r0) / (r1 - r0), 0.0f, 1.0f)) * (1 - std::clamp((a.reload - r2) / (r3 - r2), 0.0f, 1.0f)); };
    if (g == "hammer" || g == "hammer2") return 1 - a.fire;
    if (g == "trigger") return a.fire;
    if (g == "frizzen") return a.fire > 0 || (a.cycle > 0 && a.cycle < 0.6f) ? 1.0f : 0.0f;   // (the flintlock's: thrown open by the cock)
    if (g == "cylinder" || g == "drum" || g == "barrels") return (float)a.steps;
    if (g == "lever" || g == "bolt") return a.cycle > 0 && a.cycle < 1 ? sinf(a.cycle * PI) : open(0.1f, 0.25f, 0.75f, 0.9f);
    if (g == "break") return open(0.0f, 0.15f, 0.85f, 1.0f);
    if (g == "latch") return open(0.0f, 0.08f, 0.15f, 0.25f);
    if (g == "ejector") return open(0.15f, 0.25f, 0.35f, 0.45f);
    if (g == "pump") return a.reload < 0 ? 0.0f : 0.5f + 0.5f * sinf(a.reload * PI * 8);
    if (g == "load") return a.loaded && !(a.reload > 0.25f && a.reload < 0.75f) ? 1.0f : 0.0f;
    if (g == "string" || g == "string2") return a.loaded ? 1.0f : 0.0f;   // (the bow's string, drawn back while an arrow is on)
    return 0;
}
// the moving parts' local transforms: each part about its pivot by its group's value, then whatever it rides on
static std::vector<Matrix> PoseWeapon(const rt::AssetInfo& A, const GunAnim& a) {
    int n = (int)A.parts.size();
    std::vector<Matrix> M(n, MatrixIdentity());
    std::vector<char> done(n, 0);
    std::function<Matrix(int)> part = [&](int i) -> Matrix {
        if (done[i]) return M[i];
        const rt::AssetPart& p = A.parts[i];
        Matrix local = MatrixIdentity();
        if (p.group != "static") {
            float v = GroupValue(p.group, a);
            if (p.kind == "slide") local = MatrixTranslate(p.axis.x * p.amount * v, p.axis.y * p.amount * v, p.axis.z * p.amount * v);
            else if (p.kind == "show") local = v < 0.5f ? MatrixMultiply(MatrixMultiply(MatrixTranslate(-p.pivot.x, -p.pivot.y, -p.pivot.z), MatrixScale(0, 0, 0)), MatrixTranslate(p.pivot.x, p.pivot.y, p.pivot.z)) : MatrixIdentity();
            else local = MatrixMultiply(MatrixMultiply(MatrixTranslate(-p.pivot.x, -p.pivot.y, -p.pivot.z), MatrixRotate(p.axis, p.amount * v)), MatrixTranslate(p.pivot.x, p.pivot.y, p.pivot.z));
        }
        if (!p.parent.empty())
            for (int k = 0; k < n; k++) if (k != i && A.parts[k].group == p.parent) { local = MatrixMultiply(local, part(k)); break; }
        M[i] = local; done[i] = 1;
        return M[i];
    };
    for (int i = 0; i < n; i++) part(i);
    return M;
}
// Draws the slot's weapon with its grip in the fist at `grip` (world; +X along the barrel), its parts posed; returns
// false (draw the old model) if it has no baked model yet. leftHand/muzzle: where the left hand goes and the muzzle.
static bool DrawWeapon(const Slot& s, const GunAnim& a, Matrix grip, Color tint, float glow, Vector3* leftHand = nullptr, Vector3* muzzle = nullptr, Vector3* ejectAt = nullptr) {
    const Model* m = WeaponModel(s);
    const rt::AssetInfo* A = m ? rt::AssetInfoOf(m) : nullptr;
    if (!m || !A) return false;
    Vector3 g = A->Marker("grip_r") ? A->Marker("grip_r")->p : Vector3{0, 0, 0};
    Matrix M = MatrixMultiply(MatrixTranslate(-g.x, -g.y, -g.z), grip);
    // the Gunsmith's damage upgrades show as the finish: cleaned and oiled, fresh bluing, then a warm gold cast
    // (an approximation of the spec's engraved, gold-inlaid tier three)
    static const Color FINISH[4] = {{255, 255, 255, 255}, {255, 255, 255, 255}, {222, 232, 255, 255}, {255, 228, 170, 255}};
    Color ft = FINISH[std::clamp(s.lvl, 0, 3)];
    Color tt{(unsigned char)(tint.r * ft.r / 255), (unsigned char)(tint.g * ft.g / 255), (unsigned char)(tint.b * ft.b / 255), tint.a};
    rt::DrawPbrParts(*m, M, PoseWeapon(*A, a), tt, glow);
    // the attachments, each on its mount (the speed loader only while a revolver reloads)
    auto mount = [&](const char* id) -> const char* {
        std::string s2 = id;
        if (s2 == "compensator" || s2 == "choke" || s2 == "baffle" || s2 == "bayonet") return "mount_muzzle";
        if (s2 == "sight" || s2 == "nightglass" || s2 == "eyeglass" || s2 == "lodestone") return "mount_top";
        if (s2 == "extmag" || s2 == "drum") return "mount_under";
        if (s2 == "steamfeed") return "mount_top";
        if (s2 == "oilskin") return "eject";
        return nullptr;
    };
    for (int k = 0; k < 3; k++) {
        if (s.att[k] < 0 || s.att[k] >= (int)Attachments().size()) continue;
        const std::string& aid = Attachments()[s.att[k]].id;
        const char* mk = mount(aid.c_str());
        const rt::AssetMarker* mm = mk ? A->Marker(mk) : nullptr;
        const Model* am = mm ? rt::LoadAsset("shared/attachments/att_" + aid + ".glb") : nullptr;
        if (am) rt::DrawPbrParts(*am, MatrixMultiply(MatrixTranslate(mm->p.x, mm->p.y, mm->p.z), M), {}, tt, glow);
    }
    if (a.reload > 0.35f && a.reload < 0.7f && HasAttachment(s.att, "speedloader")) {
        const rt::AssetMarker* ej = A->Marker("eject");
        if (const Model* sl = ej ? rt::LoadAsset("shared/attachments/att_speedloader.glb") : nullptr)
            rt::DrawPbrParts(*sl, MatrixMultiply(MatrixTranslate(ej->p.x - 0.05f + 0.04f * (a.reload - 0.35f) / 0.35f, ej->p.y, ej->p.z), M), {}, tt, glow);
    }
    if (leftHand) { const rt::AssetMarker* L = A->Marker("grip_l"); *leftHand = Vector3Transform(L ? L->p : g, M); }
    if (muzzle) { const rt::AssetMarker* Mu = A->Marker("muzzle"); *muzzle = Vector3Transform(Mu ? Mu->p : Vector3{0.5f, 0, 0}, M); }
    if (ejectAt) { const rt::AssetMarker* E = A->Marker("eject"); *ejectAt = Vector3Transform(E ? E->p : g, M); }
    return true;
}

// spent cases and powder smoke (world space; your own gun's): cases tumble from the ejection port and bounce on the
// deck, smoke drifts and spreads from the muzzle (thick and white from black powder, a wisp from smokeless)
struct Casing { Vector3 p, v; float rot, spin, life; };
struct Puff { Vector3 p, v; float r, life, maxLife, dense; };
static std::vector<Casing> gCases;
static std::vector<Puff> gSmoke;
// a gun that burns powder (a flash, smoke, a case): not the spring, air, bow, riveter or the prod
static bool PowderGun(const Slot& s) {
    std::string ammo = s.it == Item::Weapon && s.wpn >= 0 && s.wpn < (int)Weapons().size() ? Weapons()[s.wpn].ammo : s.it == Item::Rifle ? "rounds" : s.it == Item::Shotgun ? "shells" : s.it == Item::Flare ? "flares" : "";
    return ammo == "rounds" || ammo == "shells" || ammo == "flares" || ammo == "junk" || ammo == "rockets";
}
static bool BlackPowder(const std::string& id) { return id == "blunderbuss" || id == "puntgun" || id == "captainpistol" || id == "pepperbox" || id == "derringer" || id == "shotgun"; }
static void GunEvents(const Crew& me, const GunAnim& a, Vector3 muzzle, Vector3 eject, Vector3 fwd, Vector3 rgt, float deckY, float dt) {
    static float prevCool = 0, prevReload = 0;
    const Slot& s = me.slots[me.sel];
    std::string id = WeaponModelId(s);
    const Model* m = WeaponModel(s);
    const rt::AssetInfo* A = m ? rt::AssetInfoOf(m) : nullptr;
    bool repeater = false, opens = false, gun = false;
    if (A) for (const auto& p : A->parts) { if (p.group == "lever" || p.group == "bolt" || p.group == "drum") repeater = true; if (p.group == "break" || p.group == "cylinder") opens = true; }
    std::string ammo = s.it == Item::Weapon && s.wpn >= 0 && s.wpn < (int)Weapons().size() ? Weapons()[s.wpn].ammo : s.it == Item::Rifle ? "rounds" : s.it == Item::Shotgun ? "shells" : "";
    gun = PowderGun(s);
    static uint32_t r = 77; auto R = [&]() { r = r * 1664525u + 1013904223u; return (r >> 8) / 16777216.0f; };
    bool shot = me.cool > prevCool + 0.05f && gun;
    if (shot) {
        bool bp = BlackPowder(id);
        for (int k = 0; k < (bp ? 14 : 3); k++)
            gSmoke.push_back({muzzle, Vector3Add(Vector3Scale(fwd, (bp ? 2.2f : 1.2f) * (0.3f + R())), {(R() - 0.5f) * 0.4f, 0.15f + 0.2f * R(), (R() - 0.5f) * 0.4f}),
                              bp ? 0.08f : 0.03f, 0, bp ? 3.5f + R() * 2 : 1.2f, bp ? 0.55f : 0.18f});
        if (repeater && (ammo == "rounds" || ammo == "shells"))
            gCases.push_back({eject, Vector3Add(Vector3Scale(rgt, 1.6f + R()), {0, 1.4f + R(), 0}), R() * 6, 8 + R() * 10, 4});
    }
    bool reloadStart = me.reloadT > prevReload + 0.05f;
    if (reloadStart && opens && (ammo == "rounds" || ammo == "shells"))
        for (int k = 0; k < std::min(6, a.steps); k++)
            gCases.push_back({Vector3Add(eject, {(R() - 0.5f) * 0.02f, 0, (R() - 0.5f) * 0.02f}), {(R() - 0.5f) * 0.4f, -0.3f, (R() - 0.5f) * 0.4f}, R() * 6, 4 + R() * 6, 4});
    prevCool = me.cool; prevReload = me.reloadT;
    // step them
    for (auto& c : gCases) {
        c.v.y -= 9.8f * dt; c.p = Vector3Add(c.p, Vector3Scale(c.v, dt)); c.rot += c.spin * dt; c.life -= dt;
        if (c.p.y < deckY && c.v.y < 0) {
            if (c.v.y < -0.6f) TrawlCue(TWC_CASE, std::min(1.0f, -c.v.y * 0.25f), 0.3f, 0.9f + 0.2f * R());   // (tink: fainter with each bounce)
            c.p.y = deckY; c.v = {c.v.x * 0.45f, -c.v.y * 0.35f, c.v.z * 0.45f}; c.spin *= 0.6f;
        }
    }
    gCases.erase(std::remove_if(gCases.begin(), gCases.end(), [](const Casing& c) { return c.life <= 0; }), gCases.end());
    for (auto& p : gSmoke) { p.p = Vector3Add(p.p, Vector3Scale(p.v, dt)); p.v = Vector3Scale(p.v, expf(-1.6f * dt)); p.v.y += 0.06f * dt; p.r += 0.25f * dt; p.life += dt; }
    gSmoke.erase(std::remove_if(gSmoke.begin(), gSmoke.end(), [](const Puff& p) { return p.life >= p.maxLife; }), gSmoke.end());
    if (gCases.size() > 40) gCases.erase(gCases.begin(), gCases.begin() + (gCases.size() - 40));
    for (const auto& c : gCases)
        rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.022f, 0.009f, 0.009f), MatrixRotateXYZ({c.rot, c.rot * 0.7f, c.rot * 0.3f})), MatrixTranslate(c.p.x, c.p.y, c.p.z)), Color{196, 150, 70, 255});
}
// the smoke, drawn over the frame (soft grey discs that grow and thin)
static void DrawGunSmoke(const Camera3D& cam) {
    for (const auto& p : gSmoke) {
        Vector3 d = Vector3Subtract(p.p, cam.position);
        if (Vector3DotProduct(d, Vector3Subtract(cam.target, cam.position)) <= 0.05f) continue;
        Vector2 s = GetWorldToScreenEx(p.p, cam, SCREEN_W, SCREEN_H);
        float dist = std::max(0.2f, Vector3Length(d));
        float rad = SCREEN_H * p.r / dist;
        float a = p.dense * (1 - p.life / p.maxLife) * std::min(1.0f, p.life * 6);
        DrawCircleGradient((int)s.x, (int)s.y, rad, Fade(Color{150, 154, 160, 255}, a * 0.7f), Fade(Color{150, 154, 160, 255}, 0));
    }
}

static Vector3 SailorGrip(const Model& m, const std::vector<Matrix>& skin, Matrix frame);
// a held tool: the baked model where one has been made (the rifle is the Visual Overhaul's lever carbine), else the old one
static void DrawHeldItem(Item it, Matrix m, Color tint, float glow = 0) {
    if (it == Item::Rifle) { if (const Model* gun = rt::LoadAsset("shared/test/carbine_test.glb")) { rt::DrawPbr(*gun, MatrixMultiply(MatrixTranslate(0.06f, 0.0f, 0), m), tint); return; } }
    if (glow > 0) rt::DrawStaticGlow(gItem[(int)it], m, tint, glow); else rt::DrawStatic(gItem[(int)it], m, tint);
}
// draws one sailor at frame (feet on the deck, x forward), and anything held in the right hand
static void DrawSailor(const SailorLook& L, const SailorPose& P, Matrix frame, float t, Item held, Color tint, const Crew* who = nullptr) {
    const Model* m = SailorModel(L.role);
    if (!m) return;
    std::vector<Matrix> skin = PoseSailor(*m, L, P, t);
    auto soak = [&](Color c) { float k = 1 - 0.38f * L.wet; c = {(unsigned char)(c.r * k), (unsigned char)(c.g * k), (unsigned char)(c.b * k), c.a}; return ColorLerp(c, Color{104, 18, 14, 255}, L.blood * 0.42f); };
    std::vector<rt::Recolor> rc = {{"skin", ColorLerp(L.skin, Color{150, 40, 30, 255}, L.blood * 0.12f)}, {"top", soak(L.top)}, {"trousers", soak(L.trousers)}, {"hat", soak(L.hat)}, {"hair", soak(L.hair.a ? L.hair : Color{60, 40, 28, 255})}};
    rt::DrawPbrSkinned(*m, frame, skin, rc, 0.35f, tint);
    if (L.costume && !P.fp) fig::DrawCostume(L.costume, *m, skin, frame, tint);   // (in first person only its sleeves' colour)
    if (L.beard > 0 && !P.fp) {   // facial hair rides the head bone, in the sailor's hair colour
        static const char* BEARD[4] = {nullptr, "shared/crew/beard_full.glb", "shared/crew/beard_moustache.glb", "shared/crew/beard_chops.glb"};
        const Model* bm = rt::LoadAsset(BEARD[L.beard]);
        int hd = rt::RigOf(*m).Find("head");
        if (bm && hd >= 0) {
            Color hc{(unsigned char)(L.hair.r * tint.r / 255), (unsigned char)(L.hair.g * tint.g / 255), (unsigned char)(L.hair.b * tint.b / 255), tint.a};
            rt::DrawPbr(*bm, MatrixMultiply(skin[hd], frame), hc, 0.2f);
        }
    }
    if (held != Item::None && gItem[(int)held].meshCount > 0) {
        // in the fist: at the hand, the tool pointing ahead along the figure and tipped down a little
        Vector3 at = SailorGrip(*m, skin, frame);
        Matrix rotOnly = frame; rotOnly.m12 = rotOnly.m13 = rotOnly.m14 = 0;
        Matrix hold = MatrixMultiply(MatrixMultiply(MatrixTranslate(-0.08f, 0, 0), MatrixRotateZ(-0.35f)), rotOnly);
        hold.m12 += at.x; hold.m13 += at.y; hold.m14 += at.z;
        if (!who || !DrawWeapon(who->slots[who->sel], GunAnimOf(*who), hold, tint, 0)) DrawHeldItem(held, hold, tint);
    }
}
// where the right fist closes (the middle finger's root, a little in toward the palm), in the world
static Vector3 SailorGrip(const Model& m, const std::vector<Matrix>& skin, Matrix frame) {
    const rt::RigInfo& rig = rt::RigOf(m);
    int b = rig.Find("middle1.R");
    if (b < 0) b = rig.Find("hand.R");
    Matrix w = rt::BoneWorld(rig, skin, b, frame);
    return {w.m12, w.m13, w.m14};
}
// a landing's keeper on the shared sailor rig: the elder in his feathers and wraps, Old Hoskins in yellow oilskins (the
// Angler's sou'wester), the cannery's foreman in his leather apron (the Bosun's), the quartermaster, the hermit, the
// Keeper of the Stair (pale and drowned), the cult's quartermaster in red; idling, turning his head, gesturing now and then
static bool DrawElder(int kind, Vector3 at, float yaw, float t) {
    Role r = kind == LK_SEALROCK ? Role::Angler : kind == LK_CANNERY ? Role::Bosun : Role::Medic;
    if (!SailorModel(r) || getenv("DEPTH_OLDBOAT")) return false;
    SailorLook L; L.role = r;
    L.skin = kind == LK_STAIR ? Color{170, 186, 180, 255} : kind == LK_ATOLL ? Color{150, 100, 70, 255} : Color{214, 166, 128, 255};
    Color cl = kind == LK_SEALROCK ? Color{186, 150, 50, 255} : kind == LK_CANNERY ? Color{86, 66, 50, 255} : kind == LK_SHELF ? Color{44, 44, 54, 255}
             : kind == LK_BONEBEACH ? Color{110, 100, 84, 255} : kind == LK_STAIR ? Color{120, 150, 146, 255} : kind == LK_CULT ? Color{120, 26, 26, 255} : Color{170, 110, 80, 255};
    L.top = cl; L.trousers = kind == LK_SEALROCK ? cl : Color{(unsigned char)(cl.r * 0.6f), (unsigned char)(cl.g * 0.6f), (unsigned char)(cl.b * 0.6f), 255}; L.hat = cl;
    L.hair = kind == LK_STAIR ? Color{60, 80, 70, 255} : Color{196, 190, 180, 255};   // (grey with age)
    L.beard = kind == LK_SHELF || kind == LK_BONEBEACH ? 1 : kind == LK_SEALROCK ? 3 : kind == LK_CANNERY ? 2 : 0;
    L.build = kind == LK_CANNERY ? 1.15f : 0.95f; L.height = kind == LK_STAIR ? 1.05f : 0.97f; L.headW = 1.12f; L.headH = 1.12f;
    SailorPose P; P.breathe = t * 1.4f;
    float talk = fmodf(t * 0.21f + at.x * 0.1f, 1.0f);   // (every few seconds a gesture: an arm comes up and lowers)
    if (talk < 0.2f) { P.reach = 0.5f * sinf(talk / 0.2f * PI); P.elbow = 0.6f; }
    P.nod = 0.15f * sinf(t * 0.7f);
    DrawSailor(L, P, Frame(at, yaw), t, Item::None, WHITE);
    return true;
}
// your own hands in first person (the user's references, 2026-10-02): the bare-handed viewmodel (tools/artgen/
// rt_fphands.py -> fp_sailor.glb: hands built closed on a grip, rolled sleeves) placed straight on what they hold, the
// forearms out of the bottom corners; false if it isn't built (the whole first-person body is drawn instead)
static Matrix HandFrame(Vector3 p, Vector3 fwd, Vector3 axis, float s) {   // a grip hand at p: its grip along axis, its front toward fwd
    Vector3 y = Vector3Normalize(axis);
    Vector3 x = Vector3Normalize(Vector3Subtract(fwd, Vector3Scale(y, Vector3DotProduct(fwd, y))));
    Vector3 z = Vector3CrossProduct(x, y);
    x = Vector3Scale(x, s); y = Vector3Scale(y, s); z = Vector3Scale(z, s);
    return Matrix{x.x, y.x, z.x, p.x, x.y, y.y, z.y, p.y, x.z, y.z, z.z, p.z, 0, 0, 0, 1};
}
static bool SailorArms(const Crew& me, const Camera3D& cam, float t, const Matrix* hr, const Matrix* hl, bool leftGrip) {
    const Model* m = rt::LoadAsset("shared/divers/fp_sailor.glb");
    if (!m) return false;
    SailorLook L = LookOf(me);
    std::vector<rt::Recolor> rc = {{"skin", L.skin}, {"top", L.top}, {"accent", L.top}};
    return rt::DrawVmArms(*m, cam, hr, hl, leftGrip, 1.0f, t, rc, WHITE);
}
static const float VM_HAND = 1.1f;   // (the hands' scale beside the tools: a viewmodel's licence, a touch over life size)
// your own body in first person: drawn with the camera (it turns and tips with your look), head hidden, arms up
// before you; returns where the right fist is, for the tool
static Vector3 DrawFirstPersonBody(const Crew& me, const Camera3D& cam, float t, float aimUp, float twoHand, bool working, const Vector3* grips = nullptr, const bool* gripOn = nullptr) {
    const Model* m = SailorModel(me.role);
    if (!m) return cam.position;
    SailorLook L = LookOf(me);
    SailorPose P; P.fp = true; P.aimUp = aimUp; P.twoHand = twoHand; P.grip = working ? 0.85f : 0.9f; P.reach = 1.0f; P.elbow = 0.45f;
    P.breathe = t * 1.7f; P.walkPh = t * 9; P.walk = Vector2Length(me.v) > 0.3f ? 1.0f : 0.0f;
    Vector3 f = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 flat = Vector3Normalize({f.x, 0, f.z});
    // the body faces where you look on the level, the arms follow the look up and down (a viewmodel's freedom)
    Vector3 up{0, 1, 0}, rgt = Vector3Normalize(Vector3CrossProduct(flat, up));
    float pitch = asinf(std::clamp(f.y, -1.0f, 1.0f));
    P.aimUp += std::clamp(pitch, -0.9f, 0.9f);
    Vector3 o = Vector3Add(cam.position, Vector3Add(Vector3Scale(up, -1.63f * L.height), Vector3Scale(flat, -0.2f)));   // (the shoulders behind the eye, out of the view: the arms reach forward)
    Matrix frame = {flat.x, up.x, rgt.x, o.x, flat.y, up.y, rgt.y, o.y, flat.z, up.z, rgt.z, o.z, 0, 0, 0, 1};
    if (grips) {   // fists locked to a tool's grip and fore-end, the rod or the spokes
        Matrix inv = MatrixInvert(frame);
        for (int s = 0; s < 2; s++) if (!gripOn || gripOn[s]) { P.ik[s] = true; P.target[s] = Vector3Transform(grips[s], inv); }
    }
    std::vector<Matrix> skin = PoseSailor(*m, L, P, t);
    auto soak = [&](Color c) { float k = 1 - 0.38f * L.wet; c = {(unsigned char)(c.r * k), (unsigned char)(c.g * k), (unsigned char)(c.b * k), c.a}; return ColorLerp(c, Color{104, 18, 14, 255}, L.blood * 0.42f); };
    std::vector<rt::Recolor> rc = {{"skin", ColorLerp(L.skin, Color{150, 40, 30, 255}, L.blood * 0.25f)}, {"top", soak(L.top)}, {"trousers", soak(L.trousers)}, {"hat", soak(L.hat)}};   // (your own hands show the blood more)
    rt::DrawPbrSkinned(*m, frame, skin, rc, 0.35f, WHITE);
    return SailorGrip(*m, skin, frame);
}
static bool SailorsReady() { return SailorModel(Role::Bosun) != nullptr; }

// Where a hand's fists close at its station, in the world: the rod (the right hand up the handle, the left on the
// reel below it) and the helm (both hands on the spokes, turning with the rudder). False for the other stations.
static bool StationGrips(const Gannet& g, const Crew& c, Vector3 out[2]) {
    if (c.station < 0 || c.deck != 0) return false;
    const StationDef& sd = Stations()[c.station];
    const Boat& b = g.boat;
    if (sd.kind == StationKind::Helm) {
        Vector3 ctr{4.36f, DECK_Y + 1.2f, 0};
        float turn = b.rudder * 1.4f;
        for (int s = 0; s < 2; s++) {
            float th = (s ? 0.75f : PI - 0.75f) + turn;
            out[s] = BoatPoint(b, {ctr.x - 0.02f, ctr.y + sinf(th) * 0.42f, cosf(th) * 0.42f});
        }
        return true;
    }
    for (const auto& r : g.rods) {
        if (r.station != c.station) continue;
        Vector2 tip = r.TipDeck();
        float bend = r.state == RodState::Fighting ? std::clamp(r.fight.tension / TackleOf(r.tackle).strength, 0.0f, 1.2f) : 0;
        Vector3 base{sd.at.x, DECK_Y + 0.75f, sd.at.y}, tip3{tip.x, DECK_Y + 2.3f - bend * 0.5f, tip.y};
        Vector3 dir = Vector3Normalize(Vector3Subtract(tip3, base));
        out[1] = BoatPoint(b, Vector3Add(base, Vector3Scale(dir, 0.48f)));   // the fore grip
        out[0] = BoatPoint(b, Vector3Add(base, Vector3Scale(dir, 0.2f)));    // the reel
        return true;
    }
    return false;
}

static void DrawHandSailor(const Gannet& g, const Crew& c, float t) {
    SailorLook L = LookOf(c);
    SailorPose P;
    P.breathe = t * 1.7f + c.slot; P.walkPh = t * 9 + c.slot;
    P.blink = fmodf(t * 0.23f + c.slot * 0.37f, 1.0f) < 0.03f ? 1.0f : 0.0f;   // a blink every four seconds or so
    {   // a bot barking ("Fish on, port!") shouts it: the mouth works with the words
        int ci = (int)(&c - &g.crew[0]);
        if (ci >= 0 && ci < (int)g.brains.size() && g.brains[ci].barkT > 0) P.shout = 0.55f + 0.45f * sinf(t * 18);
        if (c.talk > 0.05f) P.shout = std::max(P.shout, 0.2f + 0.7f * c.talk * (0.55f + 0.45f * sinf(t * 23 + c.slot)));   // (a player talking: the mouth works with their voice)
    }
    {   // faces: fear in the water, held or bleeding; strain on a heavy fish or a heavy load; a grin when a fish comes aboard
        static int seenLanded = -1; static float grinUntil = -1; int landed = g.landedSmall + g.landedBig;
        if (seenLanded >= 0 && landed > seenLanded) grinUntil = t + 2.5f; seenLanded = landed;
        P.fear = c.overboard ? 0.9f : (c.tangleT > 0 || c.heldT > 0) ? 0.8f : c.fallen ? 0.55f : c.bleedT > 0 ? 0.4f : 0;
        for (const auto& r : g.rods) if (r.station == c.station && c.station >= 0 && r.state == RodState::Fighting) P.strain = std::max(P.strain, std::clamp(r.fight.tension / std::max(1.0f, TackleOf(r.tackle).strength) * 1.3f, 0.0f, 1.0f));
        if (c.carryKg > 15) P.strain = std::max(P.strain, std::clamp((c.carryKg - 15) / 30, 0.0f, 0.8f));
        if (t < grinUntil && !c.overboard && !c.dead) P.grin = std::clamp((grinUntil - t) / 0.6f, 0.0f, 1.0f) * (1 - P.strain);
        if (c.dead) { P.fear = P.strain = P.grin = 0; }
    }
    Color tint = c.dead ? Color{190, 225, 245, 120} : c.yellow > 0.01f ? ColorLerp(WHITE, Color{240, 212, 70, 255}, c.yellow * 0.65f) : WHITE;   // (drenched in something yellow)
    Item held = Item::None;
    Matrix frame;
    float yawLocal = -atan2f(c.facing.y, c.facing.x);
    if (c.overboard && !c.dead) {
        float h = g.sea.Height(c.swim.x, c.swim.y);
        frame = Frame(W3(c.swim, h - 1.5f), sinf(t * 0.7f) * 0.6f);
        P.tread = 1; P.grip = 0.1f; P.nod = -0.2f;
    } else if (c.deck == DECK_SHORE) {
        Vector2 w = g.HandWorld((int)(&c - &g.crew[0]));
        frame = Frame(W3(w, ATOLL_Y), yawLocal - g.boat.heading);
        P.walk = Vector2Length(c.v) > 0.3f ? 1.0f : 0.0f;
        if (c.carrying) { P.reach = 0.75f; P.elbow = 0.35f; P.grip = 0.8f; }
    } else if (c.deck == DECK_SKIFF) {
        frame = MatrixMultiply(Frame({c.p.x, -0.62f, c.p.y}, PI), SkiffMatrix(g));
        float ph = std::clamp(c.oarT / D().skiffStroke, 0.0f, 1.0f);
        float pull = ph < 0.45f ? 1.4f - ph / 0.45f * 0.9f : 0.5f + (ph - 0.45f) / 0.55f * 0.9f;
        P.sit = 1; P.reach = pull / 1.4f; P.grip = 0.9f;
    } else {
        if (OnQuay(g, c)) frame = Frame(W3(g.boat.ToWorld(c.p), QUAY_Y), yawLocal - g.boat.heading);
        else {
            // sea legs: standing against her roll and pitch (pivoting at the feet), so the hands stay upright as
            // the deck tilts under them; past 12 degrees the arms come out to brace
            Vector2 sp = StandSpot(c);
            Vector3 feet{sp.x, (c.deck == 1 ? ENGINE_Y : DECK_Y) + c.z, sp.y};
            Matrix legs = MatrixMultiply(MatrixMultiply(MatrixTranslate(-feet.x, -feet.y, -feet.z), MatrixMultiply(MatrixRotateX(-g.boat.roll * 0.65f), MatrixRotateZ(-g.boat.pitch * 0.5f))), MatrixTranslate(feet.x, feet.y, feet.z));
            frame = MatrixMultiply(MatrixMultiply(Frame(feet, yawLocal), legs), BoatMatrix(g.boat));
            if (c.station < 0 && !c.fallen) P.tread = std::clamp((fabsf(g.boat.roll) - 0.2f) * 3.0f, 0.0f, 0.6f) * 0.5f;
        }
        if (c.dead) frame = MatrixMultiply(MatrixTranslate(0, 0.08f + 0.05f * sinf(t * 2 + c.slot), 0), frame);
        if (c.fallen) frame = MatrixMultiply(MatrixMultiply(MatrixRotateZ(1.5f), MatrixTranslate(0, 0.2f, 0)), frame);
        P.walk = c.station < 0 && Vector2Length(c.v) > 0.3f && !c.fallen ? 1.0f : 0.0f; P.crouch = c.crouchK;
        if (c.station >= 0) { P.reach = 0.7f + 0.15f * sinf(t * 5 + c.slot); P.elbow = 0.3f; P.grip = 0.85f; P.nod = 0.25f; }
        if (!c.dead && c.station < 0) { held = DrawItemOf(c.slots[c.sel]); if (held != Item::None) P.grip = 0.85f; }
    }
    if (c.dead) P.grip = 0.1f;
    Vector3 grips[2];
    if (!c.dead && !c.fallen && StationGrips(g, c, grips)) {   // the fists on the rod or the spokes
        Matrix inv = MatrixInvert(frame);
        for (int s = 0; s < 2; s++) { P.ik[s] = true; P.target[s] = Vector3Transform(grips[s], inv); }
        P.grip = 0.9f;
    }
    DrawSailor(L, P, frame, t, held, tint, &c);
    if (c.yellow > 0.15f && !c.dead && !c.overboard) {
        // drenched: a yellow puddle round the boots and drips running off the shoulders (a tint alone is lost on dark oilskins)
        Color y{236, 208, 62, 255};
        float r = 0.32f + 0.2f * c.yellow;
        rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(r, 0.012f, r), MatrixTranslate(0, 0.015f, 0)), frame), y);
        for (int k = 0; k < 5; k++) {
            float u = fmodf(t * 1.6f + k * 0.37f + c.slot * 0.11f, 1.0f);
            float a = k * 1.26f + c.slot;
            rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.025f, 0.05f, 0.025f), MatrixTranslate(cosf(a) * 0.24f, 1.45f * (1 - u * u), sinf(a) * 0.2f)), frame), y);
        }
    }
    if (c.pourT > 0 && !c.dead && !c.overboard && (c.deck == 0 || c.deck == 1) && !OnQuay(g, c)) {
        // the cup's stream: from the raised hand down to where it lands on the planks, in her frame
        float y0 = c.deck == 1 ? ENGINE_Y : DECK_Y;
        Vector3 a{c.p.x + c.facing.x * 0.45f, y0 + 1.25f, c.p.y + c.facing.y * 0.45f}, b{c.pourAt.x, y0 + 0.04f, c.pourAt.y};
        Matrix boat = BoatMatrix(g.boat);
        for (int s = 0; s < 14; s++) {
            float u = fmodf(s / 14.0f + t * 2.5f, 1.0f);
            Vector3 q = Vector3Lerp(a, b, u); q.y += sinf(u * PI) * 0.08f - u * u * 0.1f;
            rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.025f, 0.05f, 0.025f), MatrixTranslate(q.x, q.y, q.z)), boat), Color{238, 208, 64, 255});
        }
        rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.22f + 0.05f * sinf(t * 17), 0.01f, 0.22f), MatrixTranslate(b.x, b.y, b.z)), boat), Color{232, 204, 70, 255});   // (the puddle)
    }
}

static void DrawHand(const Gannet& g, const Crew& c, float t) {
    if (SailorsReady()) { DrawHandSailor(g, c, t); return; }
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
    if (c.deck == DECK_SHORE) {
        Vector2 w = g.HandWorld((int)(&c - &g.crew[0]));
        frame = Frame(W3(w, ATOLL_Y), yawLocal - g.boat.heading);   // (facing is in the Gannet's frame, as on the quay)
        bool walking = Vector2Length(c.v) > 0.3f;
        float swing = walking ? sinf(t * 9 + c.slot) * 0.45f : 0;
        put(gBody[r], MatrixIdentity(), frame);
        for (int s = -1; s <= 1; s += 2) put(gLeg, MatrixMultiply(MatrixRotateZ(swing * s), MatrixTranslate(0, 0.88f, s * 0.12f)), frame);
        if (c.tangleT > 0)   // (the Cannery Pier's pilings: the same grip, up through the planks)
            for (int k = 0; k < 5; k++) rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(0.05f, 0.7f, 0.05f), MatrixRotateX(0.9f * sinf(k * 1.3f) + 0.25f * sinf(t * 3 + k))), MatrixMultiply(MatrixRotateY(k * 1.26f), MatrixTranslate(0, 0.25f, 0))), frame), Color{50, 92, 52, 255});
        for (int s = -1; s <= 1; s += 2) put(gArm[r], MatrixMultiply(MatrixMultiply(MatrixRotateZ(c.carrying ? 1.2f : -swing * s * 1.1f), MatrixRotateX(s * 0.1f)), MatrixTranslate(0, 1.38f, s * 0.27f)), frame);
        if (c.carrying) rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(c.carry.junk ? 0.6f : 0.7f, c.carry.junk ? 0.4f : 0.15f, c.carry.junk ? 0.4f : 0.2f), MatrixTranslate(0.55f, 1.1f, 0)), frame), c.carry.junk ? Color{120, 80, 40, 255} : Color{190, 194, 196, 255});
        return;
    }
    if (c.deck == DECK_SKIFF) {
        // seated on the thwart facing aft (as a rower does), pulling: the arms come in with the stroke
        frame = MatrixMultiply(Frame({c.p.x, -0.62f, c.p.y}, PI), SkiffMatrix(g));
        put(gBody[r], MatrixIdentity(), frame);
        float ph = std::clamp(c.oarT / D().skiffStroke, 0.0f, 1.0f);
        float pull = ph < 0.45f ? 1.4f - ph / 0.45f * 0.9f : 0.5f + (ph - 0.45f) / 0.55f * 0.9f;
        for (int s = -1; s <= 1; s += 2) put(gArm[r], MatrixMultiply(MatrixMultiply(MatrixRotateZ(pull), MatrixRotateX(s * 0.2f)), MatrixTranslate(0, 1.38f, s * 0.27f)), frame);
        return;
    }
    if (OnQuay(g, c)) frame = Frame(W3(g.boat.ToWorld(c.p), QUAY_Y), yawLocal - g.boat.heading);   // (her deck's turn, then her heading)
    else { Vector2 sp = StandSpot(c); frame = MatrixMultiply(Frame({sp.x, (c.deck == 1 ? ENGINE_Y : DECK_Y) + c.z, sp.y}, yawLocal), BoatMatrix(g.boat)); }
    if (c.dead) frame = MatrixMultiply(MatrixTranslate(0, 0.08f + 0.05f * sinf(t * 2 + c.slot), 0), frame);
    if (c.fallen) frame = MatrixMultiply(MatrixMultiply(MatrixRotateZ(1.5f), MatrixTranslate(0, 0.2f, 0)), frame);   // flat on the deck
    bool walking = c.station < 0 && Vector2Length(c.v) > 0.3f && !c.fallen;
    float swing = walking ? sinf(t * 9 + c.slot) * 0.45f : 0;
    put(gBody[r], walking ? MatrixTranslate(0, fabsf(sinf(t * 9 + c.slot)) * 0.03f, 0) : MatrixIdentity(), frame);
    for (int s = -1; s <= 1; s += 2) put(gLeg, MatrixMultiply(MatrixRotateZ(swing * s), MatrixTranslate(0, 0.88f, s * 0.12f)), frame);
    if (c.tangleT > 0)   // a Kelp Wraith's grip: wet strands wound round the ankles, writhing
        for (int k = 0; k < 5; k++) rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(0.05f, 0.7f, 0.05f), MatrixRotateX(0.9f * sinf(k * 1.3f) + 0.25f * sinf(t * 3 + k))), MatrixMultiply(MatrixRotateY(k * 1.26f), MatrixTranslate(0, 0.25f, 0))), frame), Color{50, 92, 52, 255});
    // the arms: forward to the work at a station (the shovel's rhythm at the boiler, the pump's stroke), swinging
    // when walking, hanging otherwise
    float work = c.station >= 0 ? 0.9f + 0.25f * sinf(t * 5 + c.slot) : 0;
    for (int s = -1; s <= 1; s += 2) {
        float a = c.station >= 0 ? work : -swing * s * 1.1f;
        put(gArm[r], MatrixMultiply(MatrixMultiply(MatrixRotateZ(a), MatrixRotateX(s * 0.1f)), MatrixTranslate(0, 1.38f, s * 0.27f)), frame);
    }
    // what's in the right hand
    Item it = DrawItemOf(c.slots[c.sel]);
    if (!c.dead && c.station < 0 && it != Item::None && gItem[(int)it].meshCount > 0) {
        Matrix hand = MatrixMultiply(MatrixMultiply(MatrixTranslate(0.08f, -0.63f, 0), MatrixRotateZ(-swing * 1.1f)), MatrixTranslate(0, 1.38f, 0.27f));
        rt::DrawStatic(gItem[(int)it], MatrixMultiply(hand, frame), tint);
    }
}

// ---------------------------------------------------------------- the studio (the visual overhaul's harness)
// Turnarounds on a neutral stage under a lantern and the moon: 0 every role front, side and back; 1 the faces close
// up; 2 the guns side and three-quarter (the baked test carbine beside the old box rifle); 3 the baked test head;
// 4 four bots side by side. The current figures draw as they do aboard, so these are the "before" set.
static void DrawFigureAt(Role role, Matrix frame, float t, int slot, int beard = -1, int expr = 0) {
    if (SailorsReady()) {
        Crew c; c.role = role; c.slot = slot;
        SailorPose P; P.breathe = t * 1.7f + slot;
        SailorLook L = LookOf(c); if (beard >= 0) L.beard = beard;
        if (expr == 1) { P.fear = 1; L.wet = 1; } else if (expr == 2) { P.strain = 1; P.reach = 0.8f; P.grip = 0.95f; } else if (expr == 3) { P.grin = 1; L.blood = 0.8f; }   // (the studio's faces: afraid and soaked, straining, grinning and bloodied)
        DrawSailor(L, P, frame, t, Item::None, WHITE);
        return;
    }
    int r = std::clamp((int)role, 0, (int)Role::COUNT - 1);
    rt::DrawStatic(gBody[r], frame, WHITE);
    for (int s = -1; s <= 1; s += 2) rt::DrawStatic(gLeg, MatrixMultiply(MatrixTranslate(0, 0.88f, s * 0.12f), frame), WHITE);
    for (int s = -1; s <= 1; s += 2) rt::DrawStatic(gArm[r], MatrixMultiply(MatrixMultiply(MatrixRotateZ(0.05f * sinf(t + slot + s)), MatrixRotateX(s * 0.1f)), MatrixMultiply(MatrixTranslate(0, 1.38f, s * 0.27f), frame)), WHITE);
}
void DrawTrawlStudio(int which, float t) {
    EnsureCrewModels();
    if (!gCrewReady) return;
    rt::SceneLight L;
    L.fog = {58, 60, 66, 255}; L.fogDensity = 0.004f;
    L.fill = {44, 48, 60, 255}; L.rim = {90, 110, 140, 255}; L.key = {255, 214, 160, 255};
    L.surfaceY = 1e5f; L.time = t;
    L.lampRange = 14; L.lampCone = 0.55f;
    L.moonK = 0.5f; L.ambK = 0.8f; L.skyAmb = {70, 80, 100, 255}; L.seaAmb = {30, 28, 26, 255};
    Camera3D cam{}; cam.fovy = 40; cam.projection = CAMERA_PERSPECTIVE; cam.up = {0, 1, 0};
    auto lantern = [&](Vector3 at, Vector3 target) { L.lampPos = at; L.lampDir = Vector3Normalize(Vector3Subtract(target, at)); };
    const float FRONT = -PI / 2, SIDE = 0, BACK = PI / 2;   // the figure's yaw: its +x (forward) toward the camera, to the right, away
    if (which == 0) {
        cam.position = {0, 1.4f, 10.5f}; cam.target = {0, 1.0f, 0}; cam.fovy = 34;
        lantern({-3, 5, 7}, {0, 1, 0});
        L.AddPoint({4, 3, 4}, 9, {255, 190, 120, 255}, 0.7f);
        rt::RenderBegin(cam, L);
        for (int r = 0; r < 4; r++) for (int v = 0; v < 3; v++) {
            float x = (r - 1.5f) * 2.6f + (v - 1) * 0.75f;
            DrawFigureAt((Role)r, Frame({x, 0, (float)(v == 1 ? -0.3f : 0)}, v == 0 ? FRONT : v == 1 ? SIDE : BACK), t, r * 3 + v);
        }
    } else if (which == 1) {
        cam.position = {0, 1.66f, 2.2f}; cam.target = {0, 1.62f, 0}; cam.fovy = 30;
        lantern({-1.2f, 2.6f, 2.2f}, {0, 1.6f, 0});
        rt::RenderBegin(cam, L);
        for (int r = 0; r < 4; r++) DrawFigureAt((Role)r, Frame({(r - 1.5f) * 0.42f, 0, 0}, FRONT - 0.35f), t, r, (r + 1) % 4);   // (one of each beard)
    } else if (which == 14) {   // the faces (the overhaul's expressions): at rest, afraid and soaked, straining, grinning with blood on them
        cam.position = {0, 1.62f, 2.4f}; cam.target = {0, 1.45f, 0}; cam.fovy = 34;
        lantern({-1.2f, 2.6f, 2.2f}, {0, 1.5f, 0});
        rt::RenderBegin(cam, L);
        for (int r = 0; r < 4; r++) DrawFigureAt((Role)r, Frame({(r - 1.5f) * 0.5f, 0, 0}, FRONT - 0.25f), t, r + 4, 0, r);
    } else if (which == 2 || which == 3) {
        cam.position = which == 2 ? Vector3{0.05f, 0.25f, 1.55f} : Vector3{0, 0.15f, 0.75f};
        cam.target = which == 2 ? Vector3{0.05f, 0.0f, 0} : Vector3{0, 0.13f, 0};
        lantern(which == 2 ? Vector3{-0.6f, 1.2f, 1.4f} : Vector3{-0.5f, 0.5f, 0.9f}, cam.target);
        L.AddPoint({0.9f, 0.6f, 0.8f}, 4, {255, 200, 140, 255}, 0.5f);
        rt::RenderBegin(cam, L);
        if (which == 2) {
            // every baked weapon of the catalogue on a rack, side on, each scaled to its cell: the firearms (or, with
            // DEPTH_RACK=melee, the melee and thrown weapons)
            bool melee = getenv("DEPTH_RACK") && std::string(getenv("DEPTH_RACK")) == "melee";
            std::vector<const Model*> ms;
            for (const auto& w : Weapons()) {
                if (w.id == "fists" || ((w.cls == WC_MELEE || w.cls == WC_THROWN) != melee)) continue;
                if (const Model* m = rt::LoadAsset("shared/weapons/" + w.id + ".glb")) ms.push_back(m);
            }
            const int COLS = 4; const float CW = 0.56f, RH = 0.24f;
            int rows = std::max(1, ((int)ms.size() + COLS - 1) / COLS);
            cam.position = {0, 0, 0.6f + rows * RH * 1.45f}; cam.target = {0, 0, 0}; cam.fovy = 40;
            lantern({-0.8f, 1.5f, cam.position.z}, {0, 0, 0});
            rt::RenderBegin(cam, L);
            for (int k = 0; k < (int)ms.size(); k++) {
                float ext = 0.01f; BoundingBox all{{1e9f, 1e9f, 1e9f}, {-1e9f, -1e9f, -1e9f}};
                for (int i = 0; i < ms[k]->meshCount; i++) { BoundingBox bb = GetMeshBoundingBox(ms[k]->meshes[i]); all.min = Vector3Min(all.min, bb.min); all.max = Vector3Max(all.max, bb.max); }
                ext = std::max(all.max.x - all.min.x, all.max.y - all.min.y);
                float sc = std::min(1.0f, CW * 0.86f / ext);
                Vector3 ctr = Vector3Scale(Vector3Add(all.min, all.max), 0.5f);
                float x = (k % COLS - (COLS - 1) / 2.0f) * CW, y = ((rows - 1) / 2.0f - k / COLS) * RH;
                rt::DrawPbrParts(*ms[k], MatrixMultiply(MatrixMultiply(MatrixTranslate(-ctr.x, -ctr.y, -ctr.z), MatrixScale(sc, sc, sc)), MatrixTranslate(x, y, 0)), {}, WHITE);
            }
            rt::RenderEnd();
            return;
        } else {
            const Model* head = rt::LoadAsset("shared/test/head_test.glb");
            for (int k = 0; k < 3 && head; k++) rt::DrawPbr(*head, MatrixMultiply(MatrixRotateY(-0.9f + k * 0.9f), MatrixTranslate((k - 1) * 0.26f, 0, 0)), WHITE, 0.45f);
        }
    } else if (which == 12) {
        // the skins gallery (skins.h): ten of the Trawl's skins a page (DEPTH_SKINPAGE 0-7), each on a deckhand three
        // quarters on (the roles in turn), its name and rarity under it
        int page = getenv("DEPTH_SKINPAGE") ? atoi(getenv("DEPTH_SKINPAGE")) : 0;
        const auto& cat = skins::Catalogue(skins::TRAWL);
        cam.position = {0, 1.0f, 9.6f}; cam.target = {0, 0.85f, 0}; cam.fovy = 40;
        lantern({-3, 5, 8}, {0, 1, 0});
        L.AddPoint({4, 3, 4}, 9, {255, 190, 120, 255}, 0.7f);
        rt::RenderBegin(cam, L);
        for (int k = 0; k < 10; k++) {
            int i = page * 10 + k;
            if (i >= (int)cat.size()) break;
            const skins::Skin& s = cat[i];
            Crew c; c.role = (Role)(k % 4); c.slot = k;
            SailorLook Lk = LookOf(c); Lk.top = s.top; Lk.trousers = s.trousers; Lk.hat = s.hat;
            SailorPose P; P.breathe = t * 1.7f + k;
            Vector3 at{(k % 5 - 2) * 1.75f, k < 5 ? 1.15f : -1.55f, 0};
            if (SailorsReady()) DrawSailor(Lk, P, MatrixMultiply(MatrixScale(0.82f, 0.82f, 0.82f), Frame(at, FRONT + 0.4f)), t, Item::None, WHITE);
            skins::gGallery.push_back({GetWorldToScreenEx({at.x, at.y - 0.12f, at.z}, cam, SCREEN_W, SCREEN_H), s.name, skins::RarityName(s.rarity), skins::RarityColor(s.rarity), s.price});
        }
        rt::RenderEnd();
        skins::DrawGallery(skins::TRAWL, page);
        return;
    } else if (which == 13) {
        // the costumes gallery: ten of the Trawl's costumes a page (DEPTH_SKINPAGE 0-1), the roles in turn
        int page = getenv("DEPTH_SKINPAGE") ? atoi(getenv("DEPTH_SKINPAGE")) : 0;
        const auto& cat = skins::Costumes(skins::TRAWL);
        cam.position = {0, 1.0f, 9.6f}; cam.target = {0, 0.85f, 0}; cam.fovy = 40;
        lantern({-3, 5, 8}, {0, 1, 0});
        L.AddPoint({4, 3, 4}, 9, {255, 190, 120, 255}, 0.7f);
        rt::RenderBegin(cam, L);
        for (int k = 0; k < 10; k++) {
            int i = page * 10 + k;
            if (i >= (int)cat.size()) break;
            const skins::Costume& co = cat[i];
            Crew c; c.role = (Role)(k % 4); c.slot = k;
            SailorLook Lk = LookOf(c); Lk.costume = co.model; Lk.top = co.sleeve;
            SailorPose P; P.breathe = t * 1.7f + k;
            Vector3 at{(k % 5 - 2) * 1.75f, k < 5 ? 1.15f : -1.55f, 0};
            if (SailorsReady()) DrawSailor(Lk, P, MatrixMultiply(MatrixScale(0.82f, 0.82f, 0.82f), Frame(at, FRONT + 0.5f)), t, Item::None, WHITE);
            skins::gGallery.push_back({GetWorldToScreenEx({at.x, at.y - 0.12f, at.z}, cam, SCREEN_W, SCREEN_H), co.name, skins::CostumeTierName(co.tier), skins::RarityColor(co.tier), co.price});
        }
        rt::RenderEnd();
        skins::DrawGallery(skins::TRAWL, page, true);
        return;
    } else if (which == 6) {
        // one weapon in three states: at rest, the instant of a shot, half way through a reload (the action open)
        static const char* ID = getenv("DEPTH_GUN") ? getenv("DEPTH_GUN") : "revolver";
        cam.position = {0, 0.05f, 0.95f}; cam.target = {0, 0.0f, 0}; cam.fovy = 34;
        lantern({-0.5f, 0.8f, 1.0f}, {0, 0, 0});
        L.AddPoint({0.6f, 0.4f, 0.6f}, 3, {255, 200, 140, 255}, 0.6f);
        rt::RenderBegin(cam, L);
        Slot s; s.it = Item::Weapon; s.wpn = WeaponIndex(ID);
        const Model* m = WeaponModel(s);
        const rt::AssetInfo* A = m ? rt::AssetInfoOf(m) : nullptr;
        if (m && A) {
            GunAnim st[3]; st[1].fire = 1; st[1].cycle = 0.05f; st[2].reload = 0.5f; st[2].steps = 3;
            float ext = 0; for (int i = 0; i < m->meshCount; i++) { BoundingBox bb = GetMeshBoundingBox(m->meshes[i]); ext = std::max(ext, bb.max.x - bb.min.x); }
            float sc = std::clamp(0.24f / std::max(0.05f, ext), 0.2f, 3.0f);
            for (int k = 0; k < 3; k++) rt::DrawPbrParts(*m, MatrixMultiply(MatrixScale(sc, sc, sc), MatrixMultiply(MatrixRotateY(k == 1 ? 0.0f : 0.35f), MatrixTranslate((k - 1) * 0.29f - 0.03f, -0.03f, 0))), PoseWeapon(*A, st[k]), WHITE);
        }
    } else if (which == 11) {
        // a catch on the Gannet's deck by the gutting table under the lamp: lying on their sides, one arched in a slap
        EnsureModels();
        cam.position = {-1.2f, DECK_Y + 1.5f, 0.2f}; cam.target = {-2.3f, DECK_Y, 1.5f}; cam.fovy = 50;
        lantern({-2.2f, DECK_Y + 3.0f, 1.0f}, {-2.3f, DECK_Y, 1.5f}); L.lampRange = 8; L.lampCone = 0.3f;
        L.filmic = 1; L.exposure = 1.1f; L.aoK = 0.75f; L.aoRadius = 0.4f; L.outline = 0; L.stipple = 0;
        rt::RenderBegin(cam, L);
        if (const Model* bm = rt::LoadAsset("trawl/boat.glb")) rt::DrawPbr(*bm, MatrixIdentity());
        DrawDeckClutter(MatrixIdentity());
        gFishBudget = 8;
        static const char* SP[4] = {"snapper", "mahi-mahi", "bonito", "kelp bass"};
        static const Vector3 AT[4] = {{-2.6f, 0, 1.2f}, {-1.9f, 0, 0.9f}, {-3.0f, 0, 1.75f}, {-2.1f, 0, 1.6f}};
        for (int k = 0; k < 4; k++) {
            float arch = k == 1 ? 0.8f : 0;
            Vector3 hd = Vector3Normalize({cosf(k * 1.9f), arch * 0.5f, sinf(k * 1.9f)});
            DrawFishPbr(SP[k], {AT[k].x, DECK_Y + 0.06f + arch * 0.1f, AT[k].z}, hd, k == 1 ? 0.9f : 0.5f, 1.5f + arch * 0.4f, t * 18 + k, k == 1 ? 0.9f : 0.12f, GRAY);
        }
    } else if (which == 10) {
        // the fish: one species of each body archetype, mid-stroke, in two rows (DEPTH_FISHPHASE moves the stroke)
        static const char* SP[10] = {"bluefin tuna", "mahi-mahi", "snapper", "sheephead", "opah", "moray eel", "reef shark", "bat ray", "halibut", "blue marlin"};
        cam.position = {0, 1.1f, 4.6f}; cam.target = {0, 0.0f, 0}; cam.fovy = 40;
        lantern({-2, 4, 4}, {0, 0, 0});
        L.AddPoint({2.5f, 1.5f, 2}, 7, {200, 220, 255, 255}, 0.5f);
        rt::RenderBegin(cam, L);
        gFishBudget = 20;
        float ph = getenv("DEPTH_FISHPHASE") ? (float)atof(getenv("DEPTH_FISHPHASE")) : 0.8f;
        for (int k = 0; k < 10; k++) {
            float x = (k % 5 - 2) * 1.25f, y = k < 5 ? 0.55f : -0.6f;
            DrawFishPbr(SP[k], {x, y, 0}, {1, 0, 0.35f}, 1.05f, 0, ph + k, 0.3f, GRAY);
        }
    } else if (which >= 7 && which <= 9) {
        // the Gannet on a still dark sea: off her starboard bow, off her port quarter, and over her deck from aloft
        // (DEPTH_OLDBOAT=1 draws the old box model for the before and after)
        EnsureModels();
        static const Vector3 POS[3] = {{19, 4.5f, 13}, {-20, 6, -12}, {-13, 9.5f, 4}}, TGT[3] = {{0, 1.2f, 0}, {-1, 1.6f, 0}, {1, 1.0f, 0}};
        cam.position = POS[which - 7]; cam.target = TGT[which - 7]; cam.fovy = which == 9 ? 50 : 40;
        L.fog = {14, 18, 26, 255}; L.fogDensity = 0.006f;
        L.filmic = 1; L.exposure = 1.1f; L.gradeK = 0.4f; L.gradeLo = {104, 126, 150, 255}; L.gradeHi = {140, 128, 114, 255};
        L.aoK = 0.75f; L.aoRadius = 0.4f; L.grain = 0.35f; L.outline = 0; L.stipple = 0;
        L.moonDir = Vector3Normalize({-0.5f, -0.6f, -0.6f}); L.moonK = 0.9f; L.ambK = 0.9f;
        L.skyAmb = {60, 72, 96, 255}; L.seaAmb = {16, 20, 26, 255};
        lantern({0.2f, DECK_Y + 5.45f, 0}, {0.2f, 0, 0}); L.lampRange = 16; L.lampCone = 0.25f;
        L.AddPoint({3.0f, DECK_Y + 2.05f, 0}, 5, {255, 214, 150, 255}, 0.8f);
        L.AddPoint({-10.4f, DECK_Y + 3.15f, 0}, 7, {255, 220, 170, 255}, 0.7f);
        rt::RenderBegin(cam, L);
        if (const Model* bm = getenv("DEPTH_OLDBOAT") ? nullptr : rt::LoadAsset("trawl/boat.glb")) rt::DrawPbr(*bm, MatrixIdentity());
        else rt::DrawStatic(gBoat, MatrixIdentity(), WHITE);
        DrawDeckClutter(MatrixIdentity());
        rt::DrawWorldCube({0, -0.05f, 0}, {200, 0.1f, 200}, Color{8, 16, 22, 255});
        rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.24f, 0.24f, 0.24f), MatrixTranslate(0.2f, DECK_Y + 5.45f, 0)), Color{255, 226, 160, 255}, 2.5f);
        rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.12f, 0.12f, 0.12f), MatrixTranslate(3.0f, DECK_Y + 2.05f, 0)), Color{255, 214, 150, 255}, 1.8f);
        rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.16f, 0.16f, 0.16f), MatrixTranslate(-10.4f, DECK_Y + 3.15f, 0)), Color{255, 220, 170, 255}, 1.8f);
    } else if (which == 5) {
        // the first-person pose from the side (a debug view of your own arms)
        cam.position = {0.6f, 1.5f, 2.6f}; cam.target = {0.3f, 1.3f, 0};
        lantern({-1, 3, 2}, {0.3f, 1.3f, 0});
        rt::RenderBegin(cam, L);
        if (SailorsReady()) { Crew c; c.role = Role::Angler; c.slot = 1; SailorPose P; P.fp = true; P.reach = 1.0f; P.elbow = 0.45f; P.grip = 0.9f; const Model* m = SailorModel(c.role); auto sk = PoseSailor(*m, LookOf(c), P, t); rt::DrawPbrSkinned(*m, MatrixIdentity(), sk, {}, 0.35f, WHITE); Vector3 g = SailorGrip(*m, sk, MatrixIdentity()); rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.03f, 0.03f, 0.03f), MatrixTranslate(g.x, g.y, g.z)), RED, 1); rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.03f, 0.03f, 0.03f), MatrixTranslate(-0.1f, 1.63f, 0)), GREEN, 1); }
    } else {
        cam.position = {0, 1.5f, 5.2f}; cam.target = {0, 1.15f, 0};
        lantern({-2, 4, 4}, {0, 1, 0});
        L.AddPoint({2.5f, 2.6f, 2}, 7, {255, 190, 120, 255}, 0.6f);
        rt::RenderBegin(cam, L);
        const Role roles[4] = {Role::Bosun, Role::Angler, Role::Diver, Role::Medic};
        for (int k = 0; k < 4; k++) DrawFigureAt(roles[k], Frame({(k - 1.5f) * 0.95f, 0, 0}, FRONT + (k - 1.5f) * 0.15f), t, k);
    }
    rt::RenderEnd();
}

// ---------------------------------------------------------------- the frame
void DrawTrawl3D(const Gannet& g, const Eco* eco, const Session& sess, int you, const Camera3D& cam, float ghostSee) {
    gLocalSlot = you >= 0 && you < (int)g.crew.size() ? g.crew[you].slot : -1;
    EnsureModels(); EnsureSea(); EnsureLand(eco);
    if (!gReady || !gSeaReady) return;
    gFishBudget = 36;
    const Boat& b = g.boat;
    const Crew& me = g.crew[you];
    float t = g.time;
    Matrix M = BoatMatrix(b);
    bool below = me.deck == 1 && !me.overboard;
    // ---- the lights: the lantern mast is the key (a downward pool, or the searchlight's cone); deck lamps, the
    // fire below, flares and the quay's lamps are points, the nearest eight
    rt::SceneLight L;
    L.fog = g.sea.weather == Weather::Fog ? Color{34, 38, 42, 255} : g.sea.weather == Weather::Calm || g.sea.weather == Weather::Glass ? Color{13, 18, 28, 255} : Color{5, 8, 13, 255};   // (a clear night's haze is moonlit blue: the land and the ruins stand dark against it)
    L.fogDensity = g.sea.weather == Weather::Fog ? 0.09f : g.sea.weather == Weather::Rain || g.sea.weather == Weather::Squall ? 0.05f : 0.028f;
    L.fill = {34, 40, 58, 255}; L.rim = {70, 90, 112, 255}; L.key = {255, 226, 170, 255};
    L.surfaceY = 1e5f; L.time = t;
    // the Visual Overhaul's lit look: no hard ink (a thin tinted line if the player asks for it), no stipple, a faint
    // grain; contact shadows from screen-space occlusion; a filmic curve with cold darks and warm lamplight, greyer in
    // fog and a touch cooler in rain; the lamp casts shadows across the deck
    L.outline = GameSettings().trawlOutline ? 0.5f : 0.0f; L.outlineTint = {34, 44, 58, 255};
    L.stipple = 0; L.grain = 0.35f;
    L.aoK = 0.75f; L.aoRadius = 0.4f;
    L.fogBanks = g.sea.weather == Weather::Fog ? 0.9f : g.sea.weather == Weather::Rain || g.sea.weather == Weather::Squall ? 0.5f : 0.35f;   // (drifting banks, not a wall)
    // wet: rain soaks everything above the deck (fog leaves a dew on it); dry below
    {
        Weather wx = g.sea.weather;
        float target = wx == Weather::Rain ? 0.75f : wx == Weather::Squall || wx == Weather::Storm ? 1.0f : wx == Weather::Fog ? 0.3f : 0.0f;
        static float wet = 0; static int frames = 0;
        wet += (target - wet) * std::min(1.0f, GetFrameTime() / 20.0f);   // (it soaks in and dries out over half a minute)
        if (frames++ < 3) wet = target;                                    // (a night that starts in rain starts soaked)
        L.wet = wet; L.wetFloor = BoatPoint(b, {0, DECK_Y, 0}).y - 1.0f;
        // lightning in a squall or storm: a double flash every 15-45 s
        static float nextBolt = 20, boltT = -1; static uint32_t lr = 99;
        auto LR = [&]() { lr = lr * 1664525u + 1013904223u; return (lr >> 8) / 16777216.0f; };
        bool stormy = wx == Weather::Squall || wx == Weather::Storm;
        if (stormy) { nextBolt -= GetFrameTime(); if (nextBolt <= 0) { boltT = 0; nextBolt = 15 + LR() * 30; gThunderPending = 0.8f + LR() * 2.5f; } }
        if (boltT >= 0) { boltT += GetFrameTime(); if (boltT > 0.45f) boltT = -1; }
        float fl = boltT < 0 ? 0 : boltT < 0.06f ? 1.5f : boltT < 0.14f ? 0.2f : boltT < 0.2f ? 1.1f : std::max(0.0f, 1.1f - (boltT - 0.2f) * 4.4f);
        if (getenv("DEPTH_FLASH")) fl = 1.1f;   // (shots: the frame a bolt lights)
        L.flash = fl; gFlashNow = fl;
    }
    L.filmic = 1; L.exposure = 1.05f; L.gradeK = 0.45f;
    L.gradeLo = {104, 126, 150, 255}; L.gradeHi = {140, 128, 114, 255};
    L.saturation = g.sea.weather == Weather::Fog ? 0.75f : g.sea.weather == Weather::Rain || g.sea.weather == Weather::Squall ? 0.85f : 0.92f;
    if (g.sea.weather == Weather::Fog) { L.gradeLo = {112, 124, 132, 255}; L.gradeHi = {140, 130, 116, 255}; }
    L.keyShadow = true;
    L.moonDir = Vector3Negate(Vector3Normalize({cosf(0.45f) * cosf(2.2f), sinf(0.45f), cosf(0.45f) * sinf(2.2f)}));   // (the way the moonlight travels: from the moon in the sky dome)
    {   // a faint moonlight and night-sky ambient, so the land, the ruins and the kelp read as shapes beyond the lamps
        // (brighter at the full, dimmed under cloud and fog, none below decks or under the Grotto's roof)
        Weather wxm = g.sea.weather;
        bool cloud = wxm == Weather::Fog || wxm == Weather::Rain || wxm == Weather::Squall || wxm == Weather::Storm;
        bool roofed = (g.eco && g.eco->ground == "grotto" && g.boat.pos.x > g.eco->archX0) || (me.deck == 1 && !me.overboard);
        float full = 1 - fabsf(sess.moon - 0.5f) * 2;   // (sess.moon 0 new .. 0.5 full .. 1 new again)
        if (sess.moon > 1.0f || sess.moon < 0.0f) full = 0.5f;
        L.moonK = roofed ? 0.0f : (cloud ? 0.06f : 0.1f + 0.2f * full);
        L.moon = {120, 140, 180, 255};
        L.ambK = roofed ? 0.0f : 0.35f; L.skyAmb = {26, 34, 52, 255}; L.seaAmb = {6, 10, 16, 255};
    }
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
    if (below) for (const auto& lp : g.lamps) if (lp.lit) pts.push_back({BoatPoint(b, {lp.at.x, DECK_Y - 0.3f, lp.at.y}), 4.5f, {255, 200, 130, 255}, 0.7f});   // the oil lamps
    for (const auto& fl : g.flares) pts.push_back({W3(fl.p, 1.0f), 20.0f, {255, 90, 60, 255}, 1.5f});
    if (g.siren.on) pts.push_back({W3(g.siren.p, 2.0f), 9.0f, {160, 210, 225, 255}, 0.7f});   // the Siren's cold light
    if (g.skiff.Up()) pts.push_back({Vector3Transform({1.95f, 0.9f, 0}, SkiffMatrix(g)), D().skiffLantern, {255, 214, 140, 255}, 0.9f});   // the skiff's bow lantern
    for (const auto& La : g.landings) if (La.fireLit) pts.push_back({{La.at.x + La.fire.x, ATOLL_Y_EYE + 0.8f, La.at.y + La.fire.y}, 10.0f, {255, 170, 90, 255}, 1.1f + 0.1f * sinf(t * 9)});   // a landing's fire
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
    bool underRoof = g.eco && g.eco->ground == "grotto" && g.boat.pos.x > g.eco->archX0;   // (the Grotto: no sky under the cave's roof, no rain)
    if (gCrewReady && !below && !underRoof) {
        // the sky dome: the night's gradient, the moon in its phase, clouds lit from behind by it, a fogged horizon
        rt::SkyLook sk;
        sk.moonDir = Vector3Normalize({cosf(0.45f) * cosf(2.2f), sinf(0.45f), cosf(0.45f) * sinf(2.2f)});
        sk.moonPhase = sess.moon;
        sk.cloudCover = wx == Weather::Fog ? 0.95f : wx == Weather::Storm || wx == Weather::Squall ? 0.85f : wx == Weather::Rain ? 0.7f : 0.25f;
        sk.time = t;
        if (wx == Weather::Fog) { sk.zenith = {30, 34, 38, 255}; sk.horizon = {36, 40, 44, 255}; sk.cloudCover = 0.95f; }
        rt::DrawSkyDome(sk);
        if (!clouded) {
            rt::DrawSky(gStars, MatrixTranslate(cam.position.x, 0, cam.position.z), WHITE);
        } else if (wx != Weather::Fog && !(me.deck == 0 && !me.overboard && me.p.x > 0.9f && me.p.x < 5.1f && fabsf(me.p.y) < 2.1f)) {   // (dry under the wheelhouse roof)
            float fall = fmodf(t * (wx == Weather::Rain ? 9.0f : 13.0f), 12.0f);
            for (int k = 0; k < 2; k++) rt::DrawSky(gRain, MatrixTranslate(cam.position.x, cam.position.y - 6 - fall + 12 * k, cam.position.z), WHITE);
        }
    }
    // ---- spray: wherever the bow or a rail dips under the sea's surface with way on her, white water flies up and
    // falls back (read from the boat's pose and the sea, so a guest's mirror throws the same spray)
    {
        struct Drop { Vector3 p, v; float life; };
        static std::vector<Drop> spray; static uint32_t srng = 99;
        auto R = [&]() { srng = srng * 1664525u + 1013904223u; return (srng >> 8) / 16777216.0f; };
        float fdt = std::min(GetFrameTime(), 0.05f);
        float spd = b.Speed();
        int iters = gSprayWarm > 0 ? gSprayWarm : 1; if (gSprayWarm > 0) fdt = 1 / 60.0f;   // (a shot warms it up)
        gSprayWarm = 0;
        for (int it = 0; it < iters; it++) {
        const Vector3 cut[] = {{10.0f, 0.55f, 0}, {8.2f, 0.5f, -1.5f}, {8.2f, 0.5f, 1.5f}, {0, 0.35f, -2.25f}, {0, 0.35f, 2.25f}, {-7, 0.35f, -2.1f}, {-7, 0.35f, 2.1f}};
        Vector3 fwd = BoatDir(b, {1, 0, 0});
        for (int k = 0; k < 7 && !g.moored; k++) {
            Vector3 w = BoatPoint(b, cut[k]);
            float under = g.sea.Height(w.x, w.z) + 0.12f - w.y;
            if (under <= 0) continue;
            float n = under * (k == 0 ? 60.0f : 25.0f) * (0.4f + std::max(0.0f, spd)) * fdt;
            Vector3 side = BoatDir(b, {0, 0, cut[k].z < 0 ? -1.0f : cut[k].z > 0 ? 1.0f : (R() < 0.5f ? -1.0f : 1.0f)});
            for (; n > 0 && spray.size() < 500; n -= 1) {
                if (n < 1 && R() > n) break;
                Drop d; d.p = Vector3Add(w, {(R() - 0.5f) * 0.6f, 0.05f, (R() - 0.5f) * 0.6f});
                d.v = Vector3Add(Vector3Add(Vector3Scale(fwd, spd * (0.3f + 0.4f * R())), Vector3Scale(side, 0.8f + 1.8f * R())), {0, 2.0f + 3.5f * R() * std::min(1.5f, under * 3), 0});
                d.life = 0.8f + 0.8f * R();
                spray.push_back(d);
            }
        }
        // the bow wave: the stem cuts the sea with way on her, a steady curl of white either side of the cutwater
        if (!g.moored && spd > 1.5f) {
            float n = (spd - 1.5f) * 28 * fdt;
            for (; n > 0 && spray.size() < 500; n -= 1) {
                if (n < 1 && R() > n) break;
                float sgn = R() < 0.5f ? -1.0f : 1.0f;
                Vector3 w = BoatPoint(b, {9.6f + 0.4f * R(), 0, sgn * 0.25f});
                w.y = g.sea.Height(w.x, w.z) + 0.05f;
                Drop d; d.p = w;
                d.v = Vector3Add(Vector3Add(Vector3Scale(fwd, spd * 0.5f), Vector3Scale(BoatDir(b, {0, 0, sgn}), 1.0f + 1.2f * R())), {0, 1.4f + 0.45f * spd * R(), 0});
                d.life = 0.6f + 0.4f * R();
                spray.push_back(d);
            }
        }
        for (auto& d : spray) {
            d.v.y -= 9.8f * fdt; d.v = Vector3Scale(d.v, expf(-0.6f * fdt));
            d.p = Vector3Add(d.p, Vector3Scale(d.v, fdt)); d.life -= fdt;
            if (d.v.y < 0 && d.p.y < g.sea.Height(d.p.x, d.p.z)) d.life = 0;
        }
        spray.erase(std::remove_if(spray.begin(), spray.end(), [](const Drop& d) { return d.life <= 0; }), spray.end());
        }
        for (const auto& d : spray) { float s = 0.11f + 0.09f * std::min(1.0f, d.life); rt::DrawStaticGlow(DropModel(), MatrixMultiply(MatrixScale(s, s * 1.3f, s), MatrixTranslate(d.p.x, d.p.y, d.p.z)), Color{210, 224, 230, 255}, 0.9f); }   // (faintly self-lit: white water catches what light there is)
    }
    // rain on her: drops bursting on the deck and the rail round you, and drips running off the yard, the gantry and the
    // rail's edge (the frame's own clock; only in the open, never below)
    {
        Weather wx = g.sea.weather;
        float rain = wx == Weather::Rain ? 0.7f : wx == Weather::Squall || wx == Weather::Storm ? 1.0f : 0.0f;
        struct Splash { Vector3 p; float t; bool drip; Vector3 v; };
        static std::vector<Splash> sp; static uint32_t sr = 4242;
        auto SR = [&]() { sr = sr * 1664525u + 1013904223u; return (sr >> 8) / 16777216.0f; };
        float fdt = GetFrameTime();
        if (getenv("DEPTH_NORAINFX")) rain = 0;
        if (rain > 0 && !below && me.deck == 0) {
            for (float n = rain * 90 * fdt; n > 0; n -= 1) {   // bursts on the planks within a few metres of you
                if (n < 1 && SR() > n) break;
                Vector2 at{me.p.x + (SR() - 0.5f) * 9, me.p.y + (SR() - 0.5f) * 6};
                if (fabsf(at.y) > HalfBeam3(at.x) - 0.15f) continue;
                sp.push_back({BoatPoint(b, {at.x, DECK_Y + 0.01f, at.y}), 0, false, {0, 0, 0}});
            }
            for (float n = rain * 14 * fdt; n > 0; n -= 1) {   // and on the cap rail
                if (n < 1 && SR() > n) break;
                float x = me.p.x + (SR() - 0.5f) * 10; int s = SR() < 0.5f ? -1 : 1;
                sp.push_back({BoatPoint(b, {x, DECK_Y + 0.89f, s * (HalfBeam3(x) - 0.02f)}), 0, false, {0, 0, 0}});
            }
            static const Vector3 DRIP[] = {{0.2f, DECK_Y + 4.97f, -1.25f}, {0.2f, DECK_Y + 4.97f, 1.25f}, {0.2f, DECK_Y + 4.97f, -0.6f}, {0.2f, DECK_Y + 4.97f, 0.7f},
                                            {-10.6f, DECK_Y + 3.2f, -1.2f}, {-10.6f, DECK_Y + 3.2f, 0.4f}, {-10.6f, DECK_Y + 3.2f, 1.3f}, {3.0f, DECK_Y + 2.12f, 2.36f}, {3.0f, DECK_Y + 2.12f, -2.36f}, {5.3f, DECK_Y + 2.12f, 0.8f}};
            for (const auto& dp : DRIP) if (SR() < rain * 1.6f * fdt) sp.push_back({BoatPoint(b, {dp.x + (SR() - 0.5f) * 0.3f, dp.y, dp.z}), 0, true, {0, 0, 0}});
        }
        for (auto& s : sp) {
            s.t += fdt;
            if (s.drip) { s.v.y -= 9.8f * fdt; s.p = Vector3Add(s.p, Vector3Scale(s.v, fdt)); if (s.p.y < BoatPoint(b, {0, DECK_Y, 0}).y) { s.drip = false; s.t = 0; s.p.y = BoatPoint(b, {0, DECK_Y, 0}).y + 0.01f; } }
        }
        sp.erase(std::remove_if(sp.begin(), sp.end(), [](const Splash& s) { return !s.drip && s.t > 0.16f; }), sp.end());
        if (sp.size() > 400) sp.erase(sp.begin(), sp.begin() + (sp.size() - 400));
        for (const auto& s : sp) {
            if (s.drip) { rt::DrawStaticGlow(DropModel(), MatrixMultiply(MatrixScale(0.018f, 0.05f, 0.018f), MatrixTranslate(s.p.x, s.p.y, s.p.z)), Color{200, 215, 225, 255}, 0.5f); continue; }
            float k = s.t / 0.16f, r = 0.03f + 0.07f * k;   // a burst: a flat crown widening, a bead thrown up in its middle
            rt::DrawStaticGlow(DropModel(), MatrixMultiply(MatrixScale(r, 0.008f, r), MatrixTranslate(s.p.x, s.p.y, s.p.z)), Color{190, 205, 215, 255}, 0.45f * (1 - k));
            if (k < 0.6f) rt::DrawStaticGlow(DropModel(), MatrixMultiply(MatrixScale(0.012f, 0.02f, 0.012f), MatrixTranslate(s.p.x, s.p.y + 0.05f * sinf(k / 0.6f * PI), s.p.z)), Color{205, 220, 230, 255}, 0.5f);
        }
    }
    // ---- the land, the quay, the harbour's buoys
    if (gTerrain.meshCount > 0 && !getenv("DEPTH_OLDBOAT")) {
        // the baked-look land, and its props near enough to matter (palms to 140 m, dripstones 90, the mould 60)
        rt::DrawPbr(gTerrain, MatrixIdentity());
        for (const auto& pa : gLandProps) {
            if (getenv("DEPTH_NOLANDPROPS")) break;
            Vector3 at{pa.m.m12, pa.m.m13, pa.m.m14};
            float far = pa.far > 0 ? pa.far : pa.glow > 0 ? 60.0f : strstr(pa.asset, "palm") ? 140.0f : 90.0f;
            if (Vector3Distance(at, cam.position) > far) continue;
            const Model* pm = rt::LoadAsset(pa.asset);
            if (!pm) continue;
            if (pa.glow > 0) rt::DrawPbrParts(*pm, pa.m, {}, WHITE, pa.glow * (0.9f + 0.25f * sinf(t * 0.6f + at.x)));   // (the mould is the cave's own light)
            else if (pa.floats) {   // (kelp and weed riding the swell: up and down with the sea, tipped by its slope)
                float h = g.sea.Height(at.x, at.z), hx = g.sea.Height(at.x + 1, at.z) - h, hz = g.sea.Height(at.x, at.z + 1) - h;
                Matrix m = pa.m; m.m12 = m.m13 = m.m14 = 0;
                rt::DrawPbr(*pm, MatrixMultiply(MatrixMultiply(m, MatrixMultiply(MatrixRotateZ(hx * 0.8f), MatrixRotateX(-hz * 0.8f))), MatrixTranslate(at.x, h + at.y, at.z)));
            }
            else rt::DrawPbr(*pm, pa.m);
        }
    } else {
        if (gLandFor) rt::DrawStatic(gLand, MatrixIdentity(), WHITE);
        if (gMouldOn) rt::DrawStaticGlow(gMould, MatrixIdentity(), WHITE, 0.55f + 0.15f * sinf(t * 0.6f));   // (the mould is the cave's own light)
    }
    Matrix Q = MatrixMultiply(MatrixRotateY(-g.moorHeading), MatrixTranslate(g.moorPos.x, 0, g.moorPos.y));
    if (Vector2Distance(b.pos, g.moorPos) < 120) {
        // (the baked quay from tools/artgen/dock.py; its sheds' lit windows are its WINDOWS list)
        if (const Model* dm = getenv("DEPTH_OLDBOAT") ? nullptr : rt::LoadAsset("trawl/dock.glb")) {
            rt::DrawPbr(*dm, Q);
            static const float SHED[4][2] = {{-7.6f, 1.5f}, {-3.5f, 1.7f}, {8.0f, 1.5f}, {12.5f, 1.4f}};
            for (const auto& s : SHED)
                rt::DrawCubeGlow(MatrixMultiply(MatrixMultiply(MatrixScale(0.6f, 0.56f, 0.01f), MatrixTranslate(s[0] + 0.55f * s[1], QUAY_Y + 1.55f, -9.755f)), Q), Color{255, 196, 120, 255}, 1.1f);
            // the quay's clutter between the doors and along the edge, clear of the stations' spots
            if (!getenv("DEPTH_NOCLUTTER")) {
                struct Bit { const char* m; float x, y, z, yaw; };
                static const Bit QB[] = {
                    {"lobsterpot", -5.6f, 0, -9.25f, 0.1f}, {"lobsterpot", -5.55f, 0.28f, -9.25f, -0.2f}, {"lobsterpot", -5.0f, 0, -9.3f, 0.6f},
                    {"netpile", 2.95f, 0, -9.05f, 0.3f}, {"bucket", -1.3f, 0, -9.35f, 0.5f}, {"fishbox", 2.2f, 0, -9.4f, 0.1f}, {"fishbox", 2.25f, 0.25f, -9.4f, -0.1f},
                    {"tarp", 10.3f, 0, -9.0f, 0.0f}, {"ropecoil", -6.0f, 0, -4.1f, 0.0f}, {"ropecoil", 5.6f, 0, -4.1f, 1.0f}, {"oilskin", -0.2f, 1.9f, -9.74f, PI / 2},
                    {"crate", 6.4f, 0, -9.3f, 0.3f}, {"tacklebox", 6.4f, 0.5f, -9.3f, 0.6f}, {"oilcan", 10.9f, 0, -7.6f, 0.3f},
                };
                for (const auto& bt : QB) if (const Model* pm = rt::LoadAsset(std::string("trawl/props/") + bt.m + ".glb"))
                    rt::DrawPbr(*pm, MatrixMultiply(MatrixMultiply(MatrixRotateY(-bt.yaw), MatrixTranslate(bt.x, QUAY_Y + bt.y, bt.z)), Q));
            }
        } else rt::DrawStatic(gQuay, Q, WHITE);
        for (float x : {-9.0f, -1.0f, 7.0f, 13.0f}) Glow(Vector3Transform({x, QUAY_Y + 3.2f, -6.8f}, Q), 0.22f, Color{255, 220, 160, 255}, 2.2f);
    }
    for (int k = 0; k < 16; k++) {
        float a = k * PI / 8;
        Vector2 w = Vector2Add(sess.harbour, {cosf(a) * sess.harbourR, sinf(a) * sess.harbourR});
        if (eco && eco->g && eco->DepthAt(w) < 1) continue;
        if (Vector2Distance(w, {cam.position.x, cam.position.z}) > 90) continue;
        float h = g.sea.Height(w.x, w.y);
        bool nearBuoy = Vector2Distance(w, {cam.position.x, cam.position.z}) < 45;   // (further off, at night, only its lamp shows)
        if (!nearBuoy && !getenv("DEPTH_OLDBOAT")) { bool bl = fmodf(t + k * 0.37f, 2.0f) < 1.2f; Color c2 = cosf(a) > 0 ? Color{90, 230, 120, 255} : Color{240, 80, 70, 255}; Glow(W3(w, h + 1.05f), 0.2f, bl ? c2 : Mul(c2, 0.25f), bl ? 2.5f : 0.1f); continue; }
        const Model* bu = getenv("DEPTH_OLDBOAT") ? nullptr : rt::LoadAsset(cosf(a) > 0 ? "trawl/props/buoy_green.glb" : "trawl/props/buoy_red.glb");
        if (bu) rt::DrawPbr(*bu, MatrixMultiply(MatrixRotateZ(0.08f * sinf(t * 0.9f + k)), MatrixTranslate(w.x, h, w.y)));   // (riding the swell)
        else rt::DrawWorldCube(W3(w, h + 0.4f), {0.5f, 0.9f, 0.5f}, Color{50, 50, 54, 255});
        bool blink = fmodf(t + k * 0.37f, 2.0f) < 1.2f;
        Color lc = cosf(a) > 0 ? Color{90, 230, 120, 255} : Color{240, 80, 70, 255};
        Glow(W3(w, h + 1.05f), 0.2f, blink ? lc : Mul(lc, 0.25f), blink ? 2.5f : 0.1f);
    }
    // ---- the Gannet, her lamps and the parts that move
    // (the baked model from tools/artgen/boat.py, on the same layout; the old box model if the asset is missing)
    if (const Model* bm = getenv("DEPTH_OLDBOAT") ? nullptr : rt::LoadAsset("trawl/boat.glb")) { if (getenv("DEPTH_BOATSTATIC")) rt::DrawStatic(*bm, M, WHITE); else rt::DrawPbr(*bm, M); }
    else rt::DrawStatic(gBoat, M, WHITE);
    DrawDeckClutter(M);
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
    // below: the watertight door as it stands, the oil lamps (lit or dark), the hatches' covers seen from beneath
    const Model* coverM = getenv("DEPTH_OLDBOAT") ? nullptr : rt::LoadAsset("trawl/props/hatch_cover.glb");
    const Model* battenM = coverM ? rt::LoadAsset("trawl/props/batten.glb") : nullptr;
    if (coverM && battenM) {
        // the baked hatch covers: on the coaming when shut (seen from above or below), battened with two bars; an
        // open hatch is the deck's own opening, the ladder showing down it
        for (const auto& h : g.hatches) {
            if (h.state == 0) continue;
            rt::DrawPbr(*coverM, MatrixMultiply(MatrixTranslate(h.at.x, DECK_Y + 0.16f, h.at.y), M));
            if (h.state == 2) for (int s = -1; s <= 1; s += 2) rt::DrawPbr(*battenM, MatrixMultiply(MatrixTranslate(h.at.x, DECK_Y + 0.25f, h.at.y + s * 0.3f), M));
        }
    }
    if (below) {
        if (!g.doorOpen) {
            if (const Model* dm = getenv("DEPTH_OLDBOAT") ? nullptr : rt::LoadAsset("trawl/props/door.glb")) rt::DrawPbr(*dm, MatrixMultiply(MatrixTranslate(-2.72f, ENGINE_Y + 0.95f, 0), M));
            else rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.08f, 1.9f, 1.2f), MatrixTranslate(-2.75f, ENGINE_Y + 0.95f, 0)), M), Color{120, 120, 126, 255});
        }
        for (const auto& lp : g.lamps) localGlow({lp.at.x, DECK_Y - 0.3f, lp.at.y}, 0.1f, lp.lit ? Color{255, 200, 120, 255} : Color{50, 44, 38, 255}, lp.lit ? 2.0f : 0.0f);
        if (!coverM) for (const auto& h : g.hatches) if (h.state != 0) rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(1.0f, 0.06f, 1.0f), MatrixTranslate(h.at.x, DECK_Y - 0.04f, h.at.y)), M), Color{86, 64, 42, 255});
    } else if (!coverM) for (const auto& h : g.hatches) {
        // on deck: a coaming round each hatch, a cover on it unless it's open
        rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(1.05f, 0.18f, 1.05f), MatrixTranslate(h.at.x, DECK_Y + 0.09f, h.at.y)), M), h.state == 0 ? Color{20, 18, 16, 255} : Color{120, 92, 60, 255});
        if (h.state == 2) for (int s = -1; s <= 1; s += 2) rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(1.1f, 0.05f, 0.08f), MatrixTranslate(h.at.x, DECK_Y + 0.2f, h.at.y + s * 0.3f)), M), Color{70, 74, 76, 255});
    }
    if (b.bilge > 50 && below) {
        float k = std::clamp(b.bilge / 20000, 0.05f, 1.0f);
        rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(5.7f, 0.05f + k * 1.2f, 4.6f), MatrixTranslate(-5.65f, ENGINE_Y + 0.02f + k * 0.6f, 0)), M), Color{40, 90, 110, 120});
    }
    DrawSkiff3D(g, t);
    DrawLanding3D(g, t);
    // ---- the crew (not you: you see through your own eyes)
    for (int i = 0; i < (int)g.crew.size(); i++) {
        const Crew& c = g.crew[i];
        if (i == you) continue;
        if (!c.overboard && c.deck == 1 && !below) continue;       // below decks, out of sight
        if (c.dead && !me.dead && !me.Up(UP_GHOSTSPEAKER)) continue;   // only the dead see the dead aboard (and a Ghost Speaker)
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
            float beat = std::clamp(0.3f + f.tension / std::max(1.0f, f.Strength()) * 0.5f, 0.3f, 0.8f);
            if (!DrawFishPbr(f.spec.name, WD(f.p), {f.h.x, -f.h.z, f.h.y}, len, f.alongside ? 1.3f : 0, t * (f.alongside ? 4.0f : 11.0f), f.alongside ? 0.2f : beat, fc))
                DrawFishAt(gFish, WD(f.p), {f.h.x, -f.h.z, f.h.y}, len, fc, f.alongside ? 1.3f : 0);
        }
    }
    if (g.harpoon.state == RodState::Fighting) {
        const Fight& f = g.harpoon.fight;
        Vector3 prev = BoatPoint(b, {10.6f, DECK_Y + 1.06f, 0});
        for (size_t i = 1; i < f.node.size(); i++) { Vector3 q = WD(f.node[i]); Seg(prev, q, 0.02f, Color{120, 110, 90, 255}); prev = q; }
        float hl = std::clamp(0.35f + sqrtf(f.spec.kg) * 0.22f, 0.4f, 4.5f);
        if (!DrawFishPbr(f.spec.name, WD(f.p), {f.h.x, -f.h.z, f.h.y}, hl, 0, t * 9, 0.6f, Color{60, 70, 80, 255}))
            DrawFishAt(gFish, WD(f.p), {f.h.x, -f.h.z, f.h.y}, hl, Color{60, 70, 80, 255});
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
        if (const Model* lr = getenv("DEPTH_OLDBOAT") ? nullptr : rt::LoadAsset("trawl/props/life_ring.glb")) {
            // (spinning flat through the air, riding the water once it lands)
            float spin = rg.state == 1 ? t * 9.0f : 0.3f;
            rt::DrawPbr(*lr, MatrixMultiply(MatrixMultiply(MatrixRotateY(spin), MatrixRotateX(rg.state == 1 ? 0.35f : 0.05f * sinf(t))), MatrixTranslate(rg.p.x, h, rg.p.y)));
            if (rg.thrower >= 0 && rg.thrower < (int)g.crew.size()) Seg(BoatPoint(b, {g.crew[rg.thrower].p.x, DECK_Y + 1.0f, g.crew[rg.thrower].p.y}), W3(rg.p, h), 0.012f, Color{200, 190, 160, 255});
            continue;
        }
        for (int k = 0; k < 8; k++) { float a0 = k * 0.785f, a1 = a0 + 0.785f; Seg(W3({rg.p.x + cosf(a0) * 0.35f, rg.p.y + sinf(a0) * 0.35f}, h), W3({rg.p.x + cosf(a1) * 0.35f, rg.p.y + sinf(a1) * 0.35f}, h), 0.1f, k % 2 ? Color{240, 240, 230, 255} : Color{220, 70, 50, 255}); }
        if (rg.thrower >= 0 && rg.thrower < (int)g.crew.size()) Seg(BoatPoint(b, {g.crew[rg.thrower].p.x, DECK_Y + 1.0f, g.crew[rg.thrower].p.y}), W3(rg.p, h), 0.012f, Color{200, 190, 160, 255});
    }
    for (const auto& s : g.shots) Glow(WD(s.p), s.kind == Shot::Harpoon || s.kind == Shot::Spear ? 0.12f : 0.06f, s.kind == Shot::Flare ? Color{255, 100, 70, 255} : Color{230, 220, 190, 255}, s.kind == Shot::Flare ? 3.0f : 0.6f);
    for (const auto& fl : g.flares) Glow(W3(fl.p, g.sea.Height(fl.p.x, fl.p.y) + 0.1f), 0.25f, Color{255, 90, 60, 255}, 3.0f);
    // the Weeds: a Siren on her rock, pale against the dark, with a cold glow about her; mermen splashing at the cod end
    if (g.siren.on) {
        float h = g.sea.Height(g.siren.p.x, g.siren.p.y);
        Seg(W3(g.siren.p, h - 0.8f), W3(g.siren.p, h + 0.7f), 1.6f, Color{44, 42, 40, 255});
        Seg(W3({g.siren.p.x + 0.9f, g.siren.p.y + 0.4f}, h - 0.6f), W3({g.siren.p.x + 0.7f, g.siren.p.y + 0.3f}, h + 0.3f), 1.0f, Color{52, 50, 46, 255});
        float sway = sinf(t * 1.3f) * 0.12f;
        Seg(W3(g.siren.p, h + 0.8f), W3({g.siren.p.x + sway, g.siren.p.y}, h + 1.9f), 0.32f, Color{214, 222, 226, 255});
        Glow(W3({g.siren.p.x + sway, g.siren.p.y}, h + 2.2f), 0.22f, Color{230, 236, 240, 255}, 0.6f);
        Glow(W3({g.siren.p.x + sway, g.siren.p.y}, h + 1.5f), 0.3f, Color{160, 210, 225, 255}, 0.5f + 0.3f * sinf(t * 2.2f));
    }
    if (g.mermen.on) {
        Vector2 m = g.mermen.p; float h = g.sea.Height(m.x, m.y);
        for (int k = 0; k < 6; k++) {
            float a = k * 1.05f + t * 2.0f, r = 0.8f + 0.9f * fabsf(sinf(t * 4.7f + k));
            Glow(W3({m.x + cosf(a) * r * 1.5f, m.y + sinf(a) * r}, h + 0.15f + 0.3f * fabsf(sinf(t * 9 + k))), 0.18f, Color{225, 238, 242, 255}, 0.5f);
        }
        if (fmodf(t, 1.6f) < 0.4f) Seg(W3({m.x - 0.5f, m.y}, h), W3({m.x + 0.2f, m.y + 0.3f}, h + 0.9f), 0.12f, Color{150, 172, 160, 255});   // a pale arm over the floats
    }
    // the Grotto: the Angler's lure light low on the water, the Ghost Worm's pale back breaking the surface as it
    // circles (or wrapped round her), isopods swarming the bow, the Drowned on deck
    if (g.angler.on) {
        float h = g.sea.Height(g.angler.p.x, g.angler.p.y);
        Glow(W3(g.angler.p, h + 0.25f + 0.08f * sinf(t * 3)), 0.12f, Color{200, 255, 230, 255}, 2.5f);
        Seg(W3(g.angler.p, h + 0.25f), W3({g.angler.p.x - 0.8f, g.angler.p.y}, h - 0.4f), 0.03f, Color{60, 70, 66, 255});
    }
    if (g.worm.state > 0) {
        for (int k = 0; k < 22; k++) {
            float a = g.worm.ang - k * (g.worm.state == 3 ? 0.3f : 0.08f);
            Vector2 wp = g.worm.state == 3 ? g.boat.ToWorld({cosf(a) * 11.5f, sinf(a) * 3.6f}) : Vector2Add(g.boat.pos, {cosf(a) * 20, sinf(a) * 20});
            float h = g.sea.Height(wp.x, wp.y) + (g.worm.state == 1 ? -0.3f : 0.1f) + 0.25f * sinf(t * 2 + k * 0.6f);
            Glow(W3(wp, h), std::max(0.25f, 0.7f - k * 0.02f), Color{196, 200, 190, 255}, 0.15f);
        }
    }
    // Atlantis: the Pale Eye deep under the water, the singer, the longboat's torches, the Ghost Ship, the Kraken's arms
    if (g.eco && g.eco->ground == "atlantis") {
        float open = g.eyeBlinkT > 0 ? 0.1f : 1.0f;
        Glow(W3(g.eco->eyeP, -40), 22 * open, Color{200, 214, 220, 255}, (g.eyeLooked ? 1.2f : 0.7f) * (0.85f + 0.15f * sinf(t * 0.7f)));
        Glow(W3(g.eco->eyeP, -38), 6 * open, Color{20, 24, 30, 255}, 0.0f);
    }
    const bool newArt = !getenv("DEPTH_OLDBOAT") && SailorsReady();
    if (g.choir.on && g.choir.surfaced) {
        float h = g.sea.Height(g.choir.singer.x, g.choir.singer.y);
        if (newArt) {   // a pale, drowned figure risen to the chest out of the water, singing, its arms half raised
            SailorLook Ls; Ls.role = Role::Medic; Ls.skin = {196, 212, 214, 255}; Ls.top = {150, 170, 172, 255}; Ls.trousers = Ls.top; Ls.hat = Ls.top; Ls.hair = {60, 80, 80, 255}; Ls.wet = 1;
            SailorPose P; P.breathe = t * 0.8f; P.reach = 0.35f + 0.1f * sinf(t * 0.5f); P.elbow = 0.3f; P.nod = -0.2f;
            Vector2 to = Vector2Subtract(g.boat.pos, g.choir.singer);
            DrawSailor(Ls, P, Frame(W3(g.choir.singer, h - 1.1f), atan2f(-to.y, to.x)), t, Item::None, WHITE);
        } else Seg(W3(g.choir.singer, h - 0.4f), W3(g.choir.singer, h + 0.9f), 0.3f, Color{214, 222, 226, 255});
        Glow(W3(g.choir.singer, h + 1.1f), 0.2f, Color{230, 236, 240, 255}, 0.7f);
    }
    if (g.longboat.on) {
        float h = g.sea.Height(g.longboat.p.x, g.longboat.p.y), a = g.longboat.ang + PI / 2;
        Vector2 dir{cosf(a), sinf(a)};
        const Model* sk = newArt ? rt::LoadAsset("trawl/props/skiff.glb") : nullptr;
        if (sk) {   // the war canoe: the skiff's lines drawn out long, six paddlers dark against the torches
            float yaw = atan2f(-dir.y, dir.x), roll = 0.04f * sinf(t * 1.1f);
            Matrix F = MatrixMultiply(MatrixMultiply(MatrixScale(2.2f, 1.0f, 0.9f), MatrixRotateX(roll)), MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(g.longboat.p.x, h + 0.15f, g.longboat.p.y)));
            rt::DrawPbr(*sk, F, Color{110, 84, 64, 255});
            for (int k = 0; k < 6; k++) {
                SailorLook Lr; Lr.role = Role::Diver; Lr.skin = {96, 64, 44, 255}; Lr.top = {70, 40, 30, 255}; Lr.trousers = {60, 44, 30, 255}; Lr.hat = Lr.top;
                SailorPose P; P.crouch = 0.65f; float st = t * 2.4f + (k % 2) * 0.2f; P.reach = 0.6f + 0.35f * sinf(st); P.elbow = 0.3f + 0.2f * cosf(st); P.grip = 0.9f;
                float along = -3.0f + k * 1.2f, side = k % 2 ? 0.35f : -0.35f;
                Vector3 at = Vector3Transform({along / 2.2f, 0.05f, side / 0.9f}, F);
                DrawSailor(Lr, P, Frame(at, yaw), t + k, Item::None, WHITE);
            }
        } else Seg(W3(Vector2Add(g.longboat.p, Vector2Scale(dir, 5)), h + 0.2f), W3(Vector2Subtract(g.longboat.p, Vector2Scale(dir, 5)), h + 0.2f), 1.2f, Color{40, 30, 24, 255});
        for (int k = -1; k <= 1; k += 2) Glow(W3(Vector2Add(g.longboat.p, Vector2Scale(dir, k * 3.5f)), h + 1.8f + 0.1f * sinf(t * 9 + k)), 0.25f, Color{255, 170, 60, 255}, 3.0f);
    }
    if (g.ghost.state > 0) {
        float h = g.sea.Height(g.ghost.p.x, g.ghost.p.y);
        Vector2 f = g.boat.Forward();
        const Model* gb = newArt ? rt::LoadAsset("trawl/boat.glb") : nullptr;
        if (gb) {   // a drowned twin of the Gannet, half again her size, grey-green and rotten, riding too still
            float yaw = atan2f(-f.y, f.x);
            Matrix F = MatrixMultiply(MatrixMultiply(MatrixScale(1.5f, 1.5f, 1.5f), MatrixRotateZ(0.06f)), MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(g.ghost.p.x, h - 0.6f, g.ghost.p.y)));
            rt::DrawPbr(*gb, F, g.ghost.state == 1 ? Color{60, 80, 72, 255} : Color{104, 132, 118, 255});
            for (int m = -1; m <= 1; m++) {   // her masts and their tattered sails
                Vector3 base = Vector3Transform({m * 5.0f, 1.0f, 0}, F), top = Vector3Transform({m * 5.0f, 9.0f, 0}, F);
                Seg(base, top, 0.25f, Color{50, 56, 52, 255});
                for (int r = 0; r < 3; r++) {
                    Vector3 a0 = Vector3Transform({m * 5.0f, 3.5f + r * 1.8f, -2.2f + r * 0.3f}, F), a1 = Vector3Transform({m * 5.0f + 0.4f * sinf(t + r + m), 2.6f + r * 1.8f, 2.0f - r * 0.4f}, F);
                    Seg(a0, a1, 0.9f - r * 0.2f, Color{70, 84, 78, 255});   // (a rag of sail hanging off its yard)
                }
            }
        } else {
            Seg(W3(Vector2Add(g.ghost.p, Vector2Scale(f, 16)), h + 1.5f), W3(Vector2Subtract(g.ghost.p, Vector2Scale(f, 16)), h + 1.5f), 5.0f, Color{56, 64, 60, g.ghost.state == 1 ? (unsigned char)120 : (unsigned char)255});
            for (int m = -1; m <= 1; m++) Seg(W3(Vector2Add(g.ghost.p, Vector2Scale(f, m * 8.0f)), h + 3), W3(Vector2Add(g.ghost.p, Vector2Scale(f, m * 8.0f)), h + 14), 0.3f, Color{50, 56, 52, 255});   // her masts
        }
        Glow(W3(g.ghost.p, h + 6), 0.4f, Color{150, 230, 190, 255}, 3.0f);   // a cold green lantern
    }
    if (g.kraken.state == 2) for (int arm = 0; arm < 3; arm++) {
        // (the Visual Overhaul's heavy threat: slow, ponderous arms, thick at the root and tapering to a curling tip,
        // dark and wet at the base and paler toward the tip, pale suckers along the inside, old scars across them)
        float x = -6 + arm * 6.0f + sinf(t * 0.25f + arm) * 1.5f, side = arm % 2 ? 1.0f : -1.0f;
        Vector3 prev = BoatPoint(b, {x, -1.5f, side * 6.0f});
        const int N = 18;
        for (int k = 1; k <= N; k++) {
            float u = k / (float)N, sway = sinf(t * 0.55f + arm + u * 3.2f) * (0.4f + 1.1f * u);
            float curl = u > 0.7f ? (u - 0.7f) / 0.3f : 0;   // (the last stretch curls back over itself)
            Vector3 lp{x + sway + curl * curl * 1.2f * side, -1.5f + u * 5.5f - u * u * 2.5f - curl * curl * 1.1f, side * (6.0f - u * 4.0f + curl * 0.9f)};
            Vector3 q = BoatPoint(b, lp);
            float th = (1 - u) * 0.85f + 0.08f;
            Color col = ColorLerp(Color{96, 22, 28, 255}, Color{176, 78, 70, 255}, u);
            Seg(prev, q, th, col);
            if (k % 2 == 0 && u < 0.92f) Glow(BoatPoint(b, {lp.x, lp.y - th * 0.35f, lp.z - side * th * 0.45f}), th * 0.32f, Color{232, 196, 180, 255}, 0.08f);   // a sucker on the inside
            if ((k * 7 + arm * 3) % 11 == 0) Seg(BoatPoint(b, {lp.x - 0.05f, lp.y + th * 0.4f, lp.z + side * th * 0.3f}), BoatPoint(b, {lp.x + 0.12f, lp.y + th * 0.1f, lp.z + side * th * 0.5f}), th * 0.12f, Color{214, 170, 160, 255});   // an old scar
            prev = q;
        }
    }
    if (g.isopods.state == 2) for (int k = 0; k < std::min(g.isopods.n, 30); k++) {
        float ax = 8.5f - fmodf(k * 1.37f + t * 0.2f * (k % 3 + 1), 12.0f), ay = sinf(k * 2.1f + t * 0.5f) * 1.8f;
        Part(BoatMatrix(b), {ax, DECK_Y + 0.05f, ay}, {0.14f, 0.05f, 0.08f}, Color{200, 196, 176, 255});
    }
    for (const auto& d : g.drowned) {
        Matrix fr = MatrixMultiply(Frame({d.p.x, DECK_Y, d.p.y}, sinf(t * 0.7f + d.p.x)), BoatMatrix(b));
        Color body = d.hitT > 0 ? Color{220, 220, 200, 255} : Color{82, 96, 86, 255};
        rt::DrawStatic(gBody[0], fr, body);
        for (int s = -1; s <= 1; s += 2) rt::DrawStatic(gArm[0], MatrixMultiply(MatrixMultiply(MatrixRotateZ(d.grab >= 0 ? 1.4f : 0.9f + 0.2f * sinf(t * 1.3f + s)), MatrixTranslate(0, 1.38f, s * 0.27f)), fr), body);
        Glow(Vector3Transform({0.18f, 1.72f, 0}, fr), 0.04f, Color{200, 240, 220, 255}, 0.8f);   // its eyes
    }
    for (const auto& f : g.floaters) {
        float len = std::clamp(0.35f + sqrtf(f.kg) * 0.25f, 0.4f, 3.0f);
        DrawFishAt(gFish, W3(f.p, g.sea.Height(f.p.x, f.p.y) + 0.05f), {1, 0, 0.3f}, len, Color{200, 205, 210, 255}, 1.5f);
    }
    // ---- birds making off with a fish: the fish hangs under them (shoot them down)
    for (const auto& th : g.thieves) {
        int bk = th.kind;
        float sz = bk == BIRD_PELICAN ? 1.9f : bk == BIRD_FRIGATE ? 1.6f : 1.0f;
        Color bc = bk == BIRD_PELICAN ? Color{150, 120, 90, 255} : bk == BIRD_FRIGATE ? Color{40, 40, 46, 255} : WHITE;
        float h = g.sea.Height(th.p.x, th.p.y) - th.z;
        Vector3 dir = Vector3Normalize({th.v.x, 0, th.v.y});
        DrawFishAt(gGull, W3(th.p, h), dir, sz, bc, sinf(t * 7) * 0.4f);
        float len = std::clamp(0.3f + sqrtf(th.fish.kg) * 0.22f, 0.3f, 1.2f);
        DrawFishAt(gFish, W3(th.p, h - 0.35f), {0, -1, 0.2f}, len, Color{170, 178, 184, 255}, 1.5f);
    }
    // ---- fish on the deck: on their sides where they came aboard; the live ones arch and slap every so often
    // the gutting table: the fish under the knife lies on the zinc and is opened as the work goes on; past halfway its
    // fillet lies beside it
    int onTable = -1;
    {
        int gutI = -1; for (int i = 0; i < (int)Stations().size(); i++) if (Stations()[i].kind == StationKind::Gutting) gutI = i;
        bool worked = false; for (const auto& c : g.crew) if (c.station == gutI && !c.dead && !c.overboard) worked = true;
        if (worked && g.gutT > 0) for (int i = 0; i < (int)g.hold.size(); i++) if (!g.hold[i].gutted && !g.hold[i].bycatch && !g.hold[i].protectedSp) { onTable = i; break; }
        if (onTable >= 0) {
            const CatchRec& h = g.hold[onTable];
            float need = 1.2f + std::min(3.0f, h.kg * 0.08f), k = std::clamp(g.gutT / need, 0.0f, 1.0f);
            float len = std::clamp(0.3f + sqrtf(h.kg) * 0.22f, 0.3f, 1.3f);
            Vector3 at = BoatPoint(b, {-2.3f, DECK_Y + 0.93f, 2.2f});
            gFishCut = k; gFishDull = 0.3f;
            if (!DrawFishPbr(h.name, at, BoatDir(b, {1, 0, 0}), len, 1.55f, 0, 0, Color{196, 206, 214, 255})) DrawFishAt(gFish, at, BoatDir(b, {1, 0, 0}), len, Color{196, 206, 214, 255}, 1.5f);
            if (k > 0.5f) {   // the fillet: a pale pink slab beside it
                Matrix fm = MatrixMultiply(MatrixMultiply(MatrixScale(len * 0.5f, 0.025f, len * 0.16f), MatrixTranslate(-1.95f, DECK_Y + 0.92f, 2.05f)), M);
                rt::DrawStaticGlow(DropModel(), fm, Color{232, 168, 150, 255}, 0.05f);
            }
        }
    }
    for (size_t i = 0; i < g.hold.size(); i++) {
        const CatchRec& h = g.hold[i];
        if (h.gutted || h.crated || (int)i == onTable) continue;   // (crated fish are under the crates' lids; the one being gutted is on the table)
        float len = std::clamp(0.3f + sqrtf(h.kg) * 0.22f, 0.3f, 2.6f);
        float arch = 0, hop = 0;
        if (!h.dead) { float ph = fmodf(g.time * 1.3f + i * 0.7f, 1.0f); if (ph < 0.18f) { float s = sinf(ph / 0.18f * 3.1416f); arch = s * 0.9f; hop = s * 0.12f; } }
        if (!h.dead && h.airT > 0) { float s = sinf(h.airT / 0.45f * 3.1416f); hop = std::max(hop, s * 0.35f); arch = std::max(arch, s); }   // (thrown across the deck by a flop)
        Vector3 hd = Vector3Normalize(BoatDir(b, {cosf(h.heading), arch * 0.5f, sinf(h.heading)}));   // (lying the way the sim has it: its head is where a headshot lands)
        Color col = h.dead ? Color{150, 158, 164, 255} : Color{196, 206, 214, 255};
        Vector3 at = BoatPoint(b, {h.deckAt.x, DECK_Y + 0.06f + hop, h.deckAt.y});
        // (a live fish on deck lies gasping, its tail twitching, and arches through a slap; the dead lie still and dull)
        float twitch = h.dead ? 0.0f : 0.12f + arch * 0.9f;
        gFishDull = h.dead ? std::clamp(0.35f + (1 - h.fresh) * 4, 0.0f, 1.0f) : 0.0f; gFishGlimmer = h.glimmer;
        if (!DrawFishPbr(h.name, at, hd, len, 1.5f + arch * 0.4f, g.time * (arch > 0 ? 18.0f : 5.0f) + i, twitch, col, h.dead ? 0.8f : 1.0f))
            DrawFishAt(gFish, at, hd, len, col, 1.5f + arch * 0.4f);
    }
    // ---- the gutted catch below: opened fish laid in rows on the ice pounds of the hold (the latest on top)
    {
        int n = 0;
        for (int i = (int)g.hold.size() - 1; i >= 0 && n < 18; i--) {
            const CatchRec& h = g.hold[i];
            if (!h.gutted || h.crated) continue;
            int pound = n % 3, row = n / 3;
            float len = std::clamp(0.3f + sqrtf(h.kg) * 0.2f, 0.3f, 1.0f);
            Vector3 at = BoatPoint(b, {-2.2f + pound * 1.2f - 0.2f + (row % 2) * 0.12f, ENGINE_Y + 0.72f + (row / 2) * 0.05f, 1.35f + (row % 3) * 0.18f});
            gFishCut = 1; gFishDull = h.iced ? 0.25f : 0.45f; gFishGlimmer = h.glimmer;
            if (!DrawFishPbr(h.name, at, BoatDir(b, {1, 0, 0.1f * (row % 2 ? 1 : -1)}), len, 1.55f, 0, 0, Color{176, 186, 194, 255}, 0.9f)) break;
            n++;
        }
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
                int bk = BirdKindOf(r.name);   // a pelican is big and brown, a frigatebird black and long-winged
                float sz = bk == BIRD_PELICAN ? 1.9f : bk == BIRD_FRIGATE ? 1.6f : 1.0f;
                Color bc = bk == BIRD_PELICAN ? Color{150, 120, 90, 255} : bk == BIRD_FRIGATE ? Color{40, 40, 46, 255} : WHITE;
                for (int k = 0; k < std::min(a.count, 14); k++) {
                    float ang = H01((int)i, k, 1) * 6.2832f + t * (0.6f + H01(k, 2, (int)i)), rr = 1.5f + H01(k, (int)i, 3) * 4;
                    Vector3 q{p2.x + cosf(ang) * rr, 5.0f + H01(k, 7, (int)i) * 5 + sinf(t * 2 + k) * 0.4f, p2.y + sinf(ang) * rr};
                    DrawFishAt(gGull, q, {-sinf(ang), 0, cosf(ang)}, sz, bc, sinf(t * (bk == BIRD_GULL ? 9 : 4) + k) * 0.3f);
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
                    bool nearEye = !jelly && Vector3Distance(q, cam.position) < 15;
                    if (!nearEye || !DrawFishPbr(r.name, q, hd, len, 0, t * 12 + k * 1.7f, 0.3f, SpeciesTint(r), 0.4f + 0.6f * dim))
                        DrawFishAt(jelly ? gJelly : gFish, q, hd, jelly ? len * 1.4f : len, c);
                }
            } else {
                float len = std::clamp(0.3f + sqrtf(r.MeanKg()) * 0.3f, 0.3f, 3.5f);
                for (int k = 0; k < a.count; k++) {
                    Vector3 q{p2.x + H01((int)i, k, 5) * 1.2f - 0.6f, -depth, p2.y + H01(k, (int)i, 6) * 1.2f - 0.6f};
                    bool nearEye = !jelly && Vector3Distance(q, cam.position) < 25;
                    bool heavy = r.threat && r.size >= 5;   // (the big threats, the White among them: a slow, heavy beat with a roll into the turn, and old scars down the flank)
                    float beat = heavy ? 1.7f : r.size >= 4 ? 3.0f : 7.0f, roll = heavy ? sinf(t * 0.45f + i) * 0.18f : 0;
                    if (heavy) q.y += sinf(t * 0.6f + i) * 0.15f;
                    if (!nearEye || !DrawFishPbr(r.name, q, hd, len, roll, t * beat + k * 1.3f + i, heavy ? 0.22f : 0.28f, SpeciesTint(r), 0.4f + 0.6f * dim))
                        DrawFishAt(jelly ? gJelly : gFish, q, hd, len, c);
                    if (heavy && nearEye) {
                        Vector3 f = Vector3Normalize(hd), rgt = Vector3Normalize(Vector3CrossProduct(f, {0, 1, 0}));
                        for (int sc = 0; sc < 3; sc++) {   // pale rake marks across both flanks
                            float along = (-0.15f + sc * 0.12f) * len, side = len * 0.12f;
                            for (float sd : {-1.0f, 1.0f}) { Vector3 a0 = Vector3Add(q, Vector3Add(Vector3Scale(f, along), Vector3Add(Vector3Scale(rgt, sd * side), {0, len * 0.06f, 0}))); Seg(a0, Vector3Add(a0, Vector3Add(Vector3Scale(f, len * 0.05f), {0, -len * 0.09f, 0})), len * 0.012f, Mul(Color{226, 220, 210, 255}, 0.4f + 0.6f * dim)); }
                        }
                    }
                }
            }
        }
    }
    // ---- what you hold, at the bottom right of your view (off station, alive, aboard)
    Item held = DrawItemOf(me.slots[me.sel]);
    // your own arms and hands (the shared sailor, head hidden), with or without a tool, at a station reaching to the work
    bool fpBody = SailorsReady() && !me.dead && !me.overboard && me.deck != DECK_SKIFF && me.deck != DECK_DIVE;
    // (shots: DEPTH_CARRYFISH=<species> puts one in your hand)
    CatchRec shotCarry; const CatchRec* carry = me.carrying ? &me.carry : nullptr;
    if (const char* cf = getenv("DEPTH_CARRYFISH")) { shotCarry.name = cf; shotCarry.kg = 4; for (int i = 0; i < (int)Species().sp.size(); i++) if (Species().sp[i].name == cf) shotCarry.sp = i; carry = &shotCarry; }
    if (fpBody && carry && !carry->junk && me.station < 0) {
        // a fish in the hand (the user's reference): held up by the tail in the right fist before you, hanging head
        // down and swaying with your step, its weight in how far it hangs
        Vector3 f = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
        Vector3 rgt = Vector3Normalize(Vector3CrossProduct(f, cam.up)), up = Vector3CrossProduct(rgt, f);
        float sway = sinf(t * 2.1f) * 0.06f + (Vector2Length(me.v) > 0.3f ? sinf(t * 9) * 0.05f : 0);
        Vector3 fist = Vector3Add(cam.position, Vector3Add(Vector3Scale(f, 0.6f), Vector3Add(Vector3Scale(rgt, 0.12f), Vector3Scale(up, 0.08f))));
        Vector3 at = Vector3Add(fist, Vector3Scale(up, -0.035f));
        Matrix hr = HandFrame(fist, f, up, VM_HAND);   // (the tail upright through the fist)
        if (!SailorArms(me, cam, t, &hr, nullptr, false)) { Vector3 grips[2] = {fist, fist}; bool on[2] = {false, true}; at = DrawFirstPersonBody(me, cam, t, 0.3f, 0, false, grips, on); }
        float len = std::clamp(0.28f + sqrtf(std::max(0.1f, carry->kg)) * 0.2f, 0.28f, 0.85f);
        Vector3 hang = Vector3Normalize(Vector3Add(Vector3Scale(up, -1), Vector3Add(Vector3Scale(rgt, sway), Vector3Scale(f, 0.18f))));
        Vector3 c = Vector3Add(at, Vector3Scale(hang, len * 0.5f + 0.03f));
        const auto& SP = Species().sp;
        Color tintF = carry->sp >= 0 && carry->sp < (int)SP.size() ? SpeciesTint(SP[carry->sp]) : Color{150, 150, 140, 255};
        float roll = getenv("DEPTH_FISHROLL") ? (float)atof(getenv("DEPTH_FISHROLL")) : 1.57f;   // (its flank to you)
        if (!DrawFishPbr(carry->name, c, hang, len, roll, t * 4, 0.12f, tintF)) DrawFishAt(gFish, c, hang, len, tintF, roll);
    } else if (fpBody && (held == Item::None || me.station >= 0)) {
        Vector3 grips[2];
        bool on = StationGrips(g, me, grips);
        Vector3 f = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
        Vector3 rgt = Vector3Normalize(Vector3CrossProduct(f, cam.up)), up = Vector3CrossProduct(rgt, f);
        bool drawn = false;
        if (on) {   // on the rod (along it, the right hand on the fore grip, the left on the reel) or the wheel's spokes
            bool wheel = fabsf(grips[0].y - grips[1].y) < 0.1f;
            Vector3 axis = wheel ? up : Vector3Normalize(Vector3Subtract(grips[1], grips[0]));
            Matrix hr = HandFrame(grips[1], f, axis, VM_HAND), hl = HandFrame(grips[0], f, axis, VM_HAND);
            drawn = SailorArms(me, cam, t, &hr, &hl, true);
        } else if (me.station < 0) {   // empty-handed: two loose fists low in the corners, swinging a little with the step
            float sw = Vector2Length(me.v) > 0.3f ? sinf(t * 9) * 0.02f : 0.004f * sinf(t * 1.7f);
            Vector3 pr = Vector3Add(cam.position, Vector3Add(Vector3Scale(f, 0.42f), Vector3Add(Vector3Scale(rgt, 0.24f), Vector3Scale(up, -0.27f + sw))));
            Vector3 pl = Vector3Add(cam.position, Vector3Add(Vector3Scale(f, 0.42f), Vector3Add(Vector3Scale(rgt, -0.24f), Vector3Scale(up, -0.27f - sw))));
            Vector3 ax = Vector3Normalize(Vector3Add(up, Vector3Scale(f, 0.6f)));
            Matrix hr = HandFrame(pr, f, ax, VM_HAND), hl = HandFrame(pl, f, ax, VM_HAND);
            drawn = SailorArms(me, cam, t, &hr, &hl, true);
        }
        if (!drawn) DrawFirstPersonBody(me, cam, t, me.station >= 0 ? 0.1f * sinf(t * 5) : -0.25f, 0, me.station >= 0, on ? grips : nullptr);
    }
    // the viewmodel's own moments (local to your view: the game's state doesn't change): switching slots lowers what you
    // held out of view and raises the new one; an inspect (I, or after a long while standing idle) turns it to show its
    // side; a misfire is cleared with a tip and a slap; right mouse brings the sights to your eye
    static struct ViewAnim { int key = -1; Slot cur{}, prev{}; float changeT = 9, inspectT = -1, idleT = 0, clearT = -1, ads = 0; std::string lastLog; } va;
    {
        const Slot& s0 = me.slots[me.sel];
        int key = me.sel * 100000 + (int)s0.it * 1000 + s0.wpn + 1;
        if (key != va.key) { if (va.key >= 0) { va.prev = va.cur; va.changeT = va.prev.it == Item::None ? 0.2f : 0; } va.cur = s0; va.key = key; va.inspectT = va.clearT = -1; }
        va.cur = s0;
        float fdt = std::min(GetFrameTime(), 0.05f);
        va.changeT += fdt;
        if (va.inspectT >= 0 && (va.inspectT += fdt) > 2.4f) va.inspectT = -1;
        if (va.clearT >= 0 && (va.clearT += fdt) > 1.0f) va.clearT = -1;
        bool quiet = Vector2Length(me.v) < 0.1f && me.cool <= 0 && me.reloadT <= 0 && va.changeT > 0.6f;
        va.idleT = quiet ? va.idleT + fdt : 0;
        if (me.station < 0 && quiet && va.inspectT < 0 && (IsKeyPressed(KEY_I) || va.idleT > 16)) { va.inspectT = 0; va.idleT = 0; }
        if (!g.log.empty() && g.log.back() != va.lastLog) { if (g.log.back().find("Misfire") != std::string::npos) va.clearT = 0; va.lastLog = g.log.back(); }
        // (shots: DEPTH_VM=inspect|clear|draw holds that moment; DEPTH_ADS=1 holds the sights to the eye)
        if (const char* vm = getenv("DEPTH_VM")) { std::string s = vm; if (s == "inspect") va.inspectT = 1.0f; if (s == "clear") va.clearT = 0.4f; if (s == "draw") va.changeT = 0.4f; }
    }
    if (gCrewReady && held != Item::None && me.station < 0 && !me.dead && !me.overboard) {
        // (mid-switch the old item goes down before the new one comes up)
        bool holstering = va.changeT < 0.2f && va.prev.it != Item::None;
        const Slot& drawSl = holstering ? va.prev : me.slots[me.sel];
        held = DrawItemOf(drawSl);
        float lower = holstering ? va.changeT / 0.2f : std::max(0.0f, 1 - (va.changeT - 0.2f) / 0.35f);
        lower = lower * lower * (3 - 2 * lower);
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
        float dip = rl >= 0 ? sinf(rl * PI) * 0.05f : 0;   // (the gun stays in view through a reload, its action open to you)
        float back = kick * 0.07f, pitchUp = kick * 0.35f;
        float swingPitch = sw >= 0 ? (0.6f - sinf(sw * PI) * 1.6f) : 0;                 // raised, then chopped down past level
        float swingYaw = sw >= 0 ? (sw - 0.5f) * 0.8f : 0;
        // a broken arm: a long gun hangs one-handed and low (it can't be shouldered or fired); no sights for it
        const Slot& sl = drawSl;
        bool longGun = held == Item::Rifle || held == Item::Shotgun || held == Item::Speargun;
        if (sl.it == Item::Weapon && sl.wpn >= 0 && sl.wpn < (int)Weapons().size()) longGun = Weapons()[sl.wpn].cls == WC_LONGGUN || Weapons()[sl.wpn].cls == WC_SPECIAL;
        bool slung = me.Has(INJ_BROKEN_ARM) && longGun;
        bool canAim = gun && !slung && rl < 0 && sw < 0 && va.changeT > 0.55f && va.inspectT < 0 && va.clearT < 0 && (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || getenv("DEPTH_ADS"));
        if (sl.it == Item::Weapon && sl.wpn >= 0 && sl.wpn < (int)Weapons().size() && !Weapons()[sl.wpn].Gun()) canAim = false;
        va.ads += ((canAim ? 1.0f : 0.0f) - va.ads) * std::min(1.0f, GetFrameTime() * 10);
        float insp = va.inspectT >= 0 ? sinf(va.inspectT / 2.4f * PI) : 0;            // turned to show its side
        float clr = va.clearT >= 0 ? sinf(va.clearT / 1.0f * PI) : 0;                  // tipped to clear the misfire
        float slap = va.clearT > 0.45f && va.clearT < 0.6f ? (0.6f - va.clearT) / 0.15f : 0;   // ...and the slap
        Vector3 p = Vector3Add(cam.position, Vector3Add(Vector3Scale(f, 0.42f - back - 0.05f * insp), Vector3Add(Vector3Scale(rgt, 0.2f + swingYaw * 0.1f - 0.08f * insp), Vector3Scale(up, -0.2f + bob - dip + (sw >= 0 ? 0.08f * sinf(sw * PI) : 0) - 0.38f * lower + 0.04f * insp - (slung ? 0.16f : 0) - 0.015f * slap))));
        if (fpBody && va.ads < 0.05f) {   // (within the arm's reach of the right shoulder: the body stands a little behind the eye)
            Vector3 sh = Vector3Add(cam.position, Vector3Add(Vector3Scale(up, -0.24f), Vector3Add(Vector3Scale(rgt, 0.19f), Vector3Scale(f, -0.2f))));
            Vector3 d = Vector3Subtract(p, sh); float dl = Vector3Length(d);
            if (dl > 0.5f) p = Vector3Add(sh, Vector3Scale(d, 0.5f / dl));
        }
        // the item's +X along the look, tipped a little up and in; the swing and the kick tilt it; lowered, drawn, held
        // slung, or turned for a look, it pitches and rolls with that
        Vector3 ax = Vector3Normalize(Vector3Add(f, Vector3Add(Vector3Scale(up, 0.12f + pitchUp + swingPitch - 0.9f * lower - (slung ? 0.45f : 0)), Vector3Scale(rgt, -0.12f + swingYaw - 0.55f * insp))));
        Vector3 az = Vector3Normalize(Vector3CrossProduct(ax, up)), ay = Vector3CrossProduct(az, ax);
        // aiming down the sights: the sight (or the top of the action) on the line of your look, 30 cm out, level
        if (va.ads > 0.01f) {
            const Model* wm = WeaponModel(sl);
            const rt::AssetInfo* A = wm ? rt::AssetInfoOf(wm) : nullptr;
            Vector3 gp = A && A->Marker("grip_r") ? A->Marker("grip_r")->p : Vector3{0, 0, 0};
            Vector3 sp = A && A->Marker("sight") ? A->Marker("sight")->p : Vector3{gp.x + 0.15f, gp.y + 0.07f, gp.z};
            Vector3 fa = f, za = Vector3Normalize(Vector3CrossProduct(fa, up)), ya = Vector3CrossProduct(za, fa);
            Vector3 o = Vector3Subtract(sp, gp);
            Vector3 pa = Vector3Subtract(Vector3Add(cam.position, Vector3Scale(f, 0.40f - back * 0.5f)), Vector3Add(Vector3Scale(fa, o.x), Vector3Add(Vector3Scale(ya, o.y), Vector3Scale(za, o.z))));
            float k = va.ads * va.ads * (3 - 2 * va.ads);
            p = Vector3Lerp(p, pa, k);
            ax = Vector3Normalize(Vector3Lerp(ax, Vector3Normalize(Vector3Add(fa, Vector3Scale(up, pitchUp * 0.5f))), k));
            az = Vector3Normalize(Vector3CrossProduct(ax, up)); ay = Vector3CrossProduct(az, ax);
        }
        Matrix hm = {ax.x, ay.x, az.x, p.x, ax.y, ay.y, az.y, p.y, ax.z, ay.z, az.z, p.z, 0, 0, 0, 1};
        if (rl >= 0) hm = MatrixMultiply(MatrixMultiply(MatrixRotateZ(0.35f * sinf(rl * PI)), MatrixRotateX(-0.6f * sinf(rl * PI))), hm);   // (tipped up and rolled toward you so the open action shows)
        if (insp > 0) hm = MatrixMultiply(MatrixRotateX(-1.1f * insp), hm);                               // (rolled to show its flank)
        if (clr > 0) hm = MatrixMultiply(MatrixMultiply(MatrixRotateX(0.75f * clr), MatrixRotateZ(0.25f * clr + 0.12f * slap)), hm);   // (canted over, the action worked, slapped)
        // the weapon: its baked model posed by what it's doing (or the old one, until every weapon is baked)
        GunAnim ga = GunAnimOf(me);
        Vector3 leftHand{}, muzzle{}, ejectAt{};
        bool baked = DrawWeapon(sl, ga, hm, WHITE, 0.25f, &leftHand, &muzzle, &ejectAt);   // (a touch of light from the lamp at your shoulder)
        if (baked) GunEvents(me, ga, muzzle, ejectAt, f, rgt, cam.position.y - 1.62f, std::min(GetFrameTime(), 0.05f));
        if (!baked) {
            DrawHeldItem(held, hm, WHITE, 0.25f);
            leftHand = Vector3Transform({held == Item::Gaff ? 0.3f : 0.26f, -0.01f, 0}, hm);
            muzzle = Vector3Transform({held == Item::Rifle ? 0.62f : 0.5f, 0.02f, 0}, hm);
        }
        if (fpBody) {
            // your hands on it: the right fist at the grip, the left on the fore-end of a long gun or cupping a pistol
            // (it lets go to work the action through a reload); the weapon keeps its own swing, kick and dip
            bool longTool = held == Item::Rifle || held == Item::Shotgun || held == Item::Speargun || held == Item::Gaff;
            if (sl.it == Item::Weapon && sl.wpn >= 0 && sl.wpn < (int)Weapons().size()) longTool = Weapons()[sl.wpn].cls == WC_LONGGUN || Weapons()[sl.wpn].cls == WC_SPECIAL || WeaponReach(Weapons()[sl.wpn]) >= 2;
            bool twoHands = baked ? (longTool || held != Item::Knife) : longTool;
            Vector3 grips[2] = {leftHand, Vector3Transform({0.0f, -0.015f, 0}, hm)};
            bool on[2] = {twoHands && rl < 0 && !slung && va.clearT < 0, true};   // (a broken arm hangs; the left hand works a misfire clear)
            // (the right hand closed on the grip, slanted by the kind of grip; the left cupping the fore-end)
            bool gunGrip = gun || (sl.it == Item::Weapon && sl.wpn >= 0 && sl.wpn < (int)Weapons().size() && Weapons()[sl.wpn].Gun());
            float slant = !gunGrip ? 0.0f : longTool ? 58.0f : 74.0f;
            Matrix hr = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(VM_HAND, VM_HAND, VM_HAND), MatrixRotateZ(-(90 - slant) * DEG2RAD)), MatrixTranslate(0.0f, -0.015f, 0)), hm);
            Matrix hl = MatrixMultiply(MatrixScale(VM_HAND, VM_HAND, VM_HAND), hm); hl.m12 = leftHand.x; hl.m13 = leftHand.y + 0.01f; hl.m14 = leftHand.z;
            if (!SailorArms(me, cam, t, &hr, on[0] ? &hl : nullptr, false))
                DrawFirstPersonBody(me, cam, t, -0.2f, longTool ? 1.0f : 0.0f, false, grips, on);
        }
        // the old speargun's spear slides home in the last third of the reload; a muzzle flash on a powder shot that
        // lights the deck for an instant
        if (!baked && held == Item::Speargun && rl > 0.66f) { float s = (rl - 0.66f) / 0.34f; rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.5f * s, 0.006f, 0.006f), MatrixTranslate(0.1f + 0.25f * s, 0.03f, 0)), hm), Color{150, 156, 160, 255}); }
        if (ga.fire > 0.5f && PowderGun(sl)) {
            rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.06f * ga.fire, 0.06f * ga.fire, 0.06f * ga.fire), MatrixTranslate(muzzle.x, muzzle.y, muzzle.z)), Color{255, 220, 140, 255}, 1.0f);
            rt::AddLateLight(muzzle, 7, Color{255, 200, 130, 255}, 1.5f * ga.fire);
        }
    }
    // ---- the sea, last (its surface is glass the rest is seen through)
    UpdateSea(g.sea, cam.position, eco);
    {   // the sea as water: the sky and the moon in it, the lamps, foam at the hull and in the wake, crests, rain rings
        rt::WaterLook w;
        Weather wx2 = g.sea.weather;
        bool overcast = wx2 == Weather::Fog || wx2 == Weather::Rain || wx2 == Weather::Squall || wx2 == Weather::Storm;
        w.boatPos = b.pos; w.boatHeading = b.heading; w.boatSpeed = fabsf(b.Speed()); w.boatLen = 21; w.boatBeam = 5.6f;
        w.crest = wx2 == Weather::Storm ? 1.0f : wx2 == Weather::Squall ? 0.7f : wx2 == Weather::Rain ? 0.25f : 0.05f;
        w.rain = wx2 == Weather::Rain ? 0.7f : wx2 == Weather::Squall || wx2 == Weather::Storm ? 1.0f : 0.0f;
        w.moonK = overcast ? 0.08f : 0.25f + 0.75f * (1 - fabsf(sess.moon - 0.5f) * 2);
        if (eco && eco->ground == "grotto" && b.pos.x > eco->archX0) { w.moonK = 0; w.zenith = {2, 4, 6, 255}; w.horizon = {6, 10, 12, 255}; }
        w.deep = eco && eco->ground == "lagoon" ? Color{8, 26, 30, 255} : Color{6, 18, 26, 255};
        // blood off her: the deck's blood drains out through the freeing ports (and chum and guts at the gutting rail),
        // leaving stains on the water that spread and fade where they fell, a trail astern when she's under way
        {
            struct Stain { Vector2 p; float age, k; };
            static std::vector<Stain> st; static float spawnT = 0; static uint32_t sr2 = 77;
            auto SR = [&]() { sr2 = sr2 * 1664525u + 1013904223u; return (sr2 >> 8) / 16777216.0f; };
            float fdt = GetFrameTime();
            spawnT -= fdt;
            float bleed = std::min(1.0f, g.deckBlood / 15.0f), chum = g.chumLeft > 0 ? 0.8f : 0.0f;
            if (spawnT <= 0 && (bleed > 0.05f || chum > 0)) {
                spawnT = 1.2f;
                static const float PORTS[5] = {-8.75f, -5.75f, -1.75f, 2.25f, 6.25f};
                Vector2 at;
                if (chum > bleed) at = g.boat.ToWorld({-2.2f, 3.2f});
                else { float x = PORTS[(int)(SR() * 5) % 5]; int sd = SR() < 0.5f ? -1 : 1; at = g.boat.ToWorld({x, sd * (HalfBeam3(x) + 0.4f)}); }
                st.push_back({at, 0, std::max(bleed, chum)});
                if (st.size() > 8) st.erase(st.begin());
            }
            for (auto& s : st) s.age += fdt;
            st.erase(std::remove_if(st.begin(), st.end(), [](const Stain& s) { return s.age > 45; }), st.end());
            w.stains = 0;
            for (const auto& s : st) if (w.stains < 8) w.stain[w.stains++] = {s.p.x, s.p.y, 0.6f + std::min(3.2f, s.age * 0.25f), s.k * std::clamp(1 - s.age / 45, 0.0f, 1.0f)};
        }
        rt::DrawWater(gSea, w);
    }
    rt::RenderEnd();
    // halos: each lamp blooms in the wet air (a soft glow round it, wide in fog, a ring of it in rain, faint on a
    // clear night), drawn over the frame
    {
        float wet = g.sea.weather == Weather::Fog ? 1.0f : g.sea.weather == Weather::Rain || g.sea.weather == Weather::Squall || g.sea.weather == Weather::Storm ? 0.6f : 0.25f;
        Vector3 fw = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
        DrawGunSmoke(cam);
        BeginBlendMode(BLEND_ADDITIVE);
        // light shafts: in rain and fog the lamps' light hangs in the air as cones down to the deck or the water
        // (screen-space: a soft fan from the lamp to the ring it lights, brightest at the lamp)
        if (wet > 0.3f && !below) {
            auto shaft = [&](Vector3 top, float r, float drop, Color c, float k) {
                Vector3 d = Vector3Subtract(top, cam.position);
                if (Vector3DotProduct(d, fw) <= 0.5f || Vector3Length(d) > 45) return;
                Vector2 s0 = GetWorldToScreenEx(top, cam, SCREEN_W, SCREEN_H);
                const int SEG = 14;
                Vector2 ring[SEG + 1]; bool ok = true;
                for (int i = 0; i <= SEG; i++) {
                    float a = i * 2 * PI / SEG;
                    Vector3 q{top.x + cosf(a) * r, top.y - drop, top.z + sinf(a) * r};
                    if (Vector3DotProduct(Vector3Subtract(q, cam.position), fw) <= 0.3f) { ok = false; break; }
                    ring[i] = GetWorldToScreenEx(q, cam, SCREEN_W, SCREEN_H);
                }
                if (!ok) return;
                float a0 = std::clamp(k * 0.09f * wet, 0.0f, 0.22f);
                rlBegin(RL_TRIANGLES);
                for (int i = 0; i < SEG; i++) {
                    rlColor4ub(c.r, c.g, c.b, (unsigned char)(a0 * 255)); rlVertex2f(s0.x, s0.y);
                    rlColor4ub(c.r, c.g, c.b, 0); rlVertex2f(ring[i + 1].x, ring[i + 1].y);
                    rlColor4ub(c.r, c.g, c.b, 0); rlVertex2f(ring[i].x, ring[i].y);
                    rlColor4ub(c.r, c.g, c.b, (unsigned char)(a0 * 255)); rlVertex2f(s0.x, s0.y);
                    rlColor4ub(c.r, c.g, c.b, 0); rlVertex2f(ring[i].x, ring[i].y);
                    rlColor4ub(c.r, c.g, c.b, 0); rlVertex2f(ring[i + 1].x, ring[i + 1].y);
                }
                rlEnd();
            };
            float lamp = b.lantern == 0 ? 0.25f : b.lantern / 2.0f;
            shaft(BoatPoint(b, {0.2f, DECK_Y + 5.4f, 0}), 2.8f + b.lantern * 0.8f, 4.2f, Color{255, 228, 170, 255}, lamp * 1.4f);
            shaft(BoatPoint(b, {-10.4f, DECK_Y + 3.1f, 0}), 1.8f, 2.0f, Color{255, 222, 172, 255}, 0.8f);
            if (Vector2Distance(b.pos, g.moorPos) < 80) {
                Matrix Qm = MatrixMultiply(MatrixRotateY(-g.moorHeading), MatrixTranslate(g.moorPos.x, 0, g.moorPos.y));
                for (float x : {-9.0f, -1.0f, 7.0f, 13.0f}) shaft(Vector3Transform({x, QUAY_Y + 3.15f, -6.8f}, Qm), 2.2f, 3.1f, Color{255, 220, 160, 255}, 0.9f);
            }
        }
        // lightning: the whole frame washed white for an instant; the thunder follows by the distance
        if (gFlashNow > 0) DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(Color{200, 210, 235, 255}, std::min(0.35f, gFlashNow * 0.22f)));
        if (gThunderPending >= 0) { gThunderPending -= GetFrameTime(); if (gThunderPending < 0) { TrawlCue(TWC_THUNDER, 0.9f, 0.0f); gThunderPending = -1; } }
        if (rt::GetQuality().fog > 0) for (const auto& p : pts) {
            Vector3 d = Vector3Subtract(p.p, cam.position);
            float dist = Vector3Length(d);
            if (dist < 0.6f || dist > 60 || Vector3DotProduct(d, fw) <= 0.2f * dist) continue;
            Vector2 s = GetWorldToScreenEx(p.p, cam, SCREEN_W, SCREEN_H);
            float rad = SCREEN_H * (0.35f + 0.9f * wet) * std::min(1.5f, p.r * 0.25f) / dist * 1.6f;
            float a = std::clamp(0.10f + 0.22f * wet, 0.0f, 0.4f) * std::min(1.0f, p.k);
            DrawCircleGradient((int)s.x, (int)s.y, rad, Fade(p.c, a), Fade(p.c, 0));
            DrawCircleGradient((int)s.x, (int)s.y, rad * 0.25f, Fade(p.c, a * 1.4f), Fade(p.c, 0));
        }
        EndBlendMode();
    }
}

void UnloadTrawl3D() {
    for (int i = 0; i < 4; i++) if (gAtoll[i].meshCount > 0) { UnloadModel(gAtoll[i]); gAtoll[i] = Model{}; gAtollFor[i] = {-1e9f, -1e9f}; }
    for (int i = 0; i < 4; i++) { if (gAtollT[i].meshCount > 0) UnloadTerrain(gAtollT[i]); if (gAtollX[i].meshCount > 0) { UnloadModel(gAtollX[i]); gAtollX[i] = Model{}; } gAtollProps[i].clear(); }
    if (gReady) { UnloadModel(gSkiff); UnloadModel(gBoat); UnloadModel(gQuay); UnloadModel(gFish); UnloadModel(gJelly); UnloadModel(gGull); gReady = false; }
    if (gSeaReady) { UnloadModel(gSea); gSeaReady = false; }
    if (gLandFor) { UnloadModel(gLand); gLandFor = nullptr; }
    if (gTerrain.meshCount > 0) UnloadTerrain(gTerrain);
    gLandProps.clear();
    if (gMouldOn) { UnloadModel(gMould); gMouldOn = false; }
    if (gCrewReady) {
        for (int r = 0; r < (int)Role::COUNT; r++) { UnloadModel(gBody[r]); UnloadModel(gArm[r]); }
        UnloadModel(gLeg); UnloadModel(gStars); UnloadModel(gMoon); UnloadModel(gRain);
        for (int i = 1; i < (int)Item::COUNT; i++) UnloadModel(gItem[i]);
        gCrewReady = false;
    }
}

// The fire's flames and smoke, over the finished frame (soft shapes the ink pass would only box): projected from the
// fire, the smoke white to grey to black as the fish on it cook and burn
void DrawLandingFx2D(const Gannet& g, const Camera3D& cam) {
    float t = g.time;
    if (!getenv("DEPTH_OLDBOAT")) return;   // (the flames and the smoke are in the world now: DrawLanding3D)
    for (const auto& L : g.landings) {
        Vector3 f3{L.at.x + L.fire.x, ATOLL_Y_EYE + 0.15f, L.at.y + L.fire.y};
        Vector3 fw = Vector3Subtract(cam.target, cam.position);
        if (Vector3DotProduct(Vector3Subtract(f3, cam.position), fw) <= 0.1f || Vector3Distance(f3, cam.position) > 60) continue;
        float dist = std::max(1.0f, Vector3Distance(f3, cam.position));
        float px = SCREEN_H / dist;   // (about a metre's height in pixels at that range)
        if (L.fireLit) {
            Vector2 c = GetWorldToScreenEx(f3, cam, SCREEN_W, SCREEN_H);
            DrawCircleV(c, px * 0.4f, Fade(Color{255, 150, 60, 255}, 0.12f));
            for (int k = 0; k < 9; k++) {
                float ph = fmodf(t * 1.8f + k * 0.37f, 1.0f);
                Vector2 q{c.x + sinf(k * 2.1f + t * 3) * px * 0.12f, c.y - ph * px * 0.5f};
                DrawCircleV(q, px * 0.07f * (1 - ph), Fade(k % 3 ? Color{255, 190, 80, 255} : Color{255, 110, 40, 255}, 0.85f * (1 - ph)));
            }
        }
        if (L.onFire.empty() || !L.fireLit) continue;
        float worst = 0; for (const auto& r : L.onFire) worst = std::max(worst, r.cookT / (10 + r.kg + 10));
        Color sm = worst > 0.75f ? Color{30, 30, 30, 255} : worst > 0.5f ? Color{120, 120, 120, 255} : Color{215, 215, 215, 255};
        for (int k = 0; k < 18; k++) {
            float u = fmodf(t * 0.25f + k / 18.0f, 1.0f);
            Vector3 p{f3.x + u * 1.8f + sinf(k * 2.3f + t) * 0.3f * u, f3.y + 0.4f + u * 5, f3.z + cosf(k * 1.7f + t) * 0.3f * u};
            if (Vector3DotProduct(Vector3Subtract(p, cam.position), fw) <= 0.1f) continue;
            Vector2 q = GetWorldToScreenEx(p, cam, SCREEN_W, SCREEN_H);
            float d = std::max(1.0f, Vector3Distance(p, cam.position));
            DrawCircleV(q, SCREEN_H / d * (0.04f + u * 0.16f), Fade(sm, 0.22f * (1 - u)));
        }
    }
}
} // namespace tw

namespace tw {
// the Wardrobe's costume preview (skins::gPreview): a deckhand in your skin, turning slowly on the right of the screen
static void TrawlWardrobePreview(int game, const char* model, float t) {
    (void)game;
    EnsureCrewModels();
    if (!gCrewReady || !SailorsReady()) return;
    rt::SceneLight L;
    L.fog = {58, 60, 66, 255}; L.fogDensity = 0.004f;
    L.fill = {44, 48, 60, 255}; L.rim = {90, 110, 140, 255}; L.key = {255, 214, 160, 255};
    L.surfaceY = 1e5f; L.time = t; L.lampRange = 14; L.lampCone = 0.55f;
    L.moonK = 0.5f; L.ambK = 0.8f; L.skyAmb = {70, 80, 100, 255}; L.seaAmb = {30, 28, 26, 255};
    Camera3D cam{}; cam.projection = CAMERA_PERSPECTIVE; cam.up = {0, 1, 0}; cam.fovy = 34;
    cam.position = {-1.35f, 1.25f, 4.6f}; cam.target = {-1.35f, 1.0f, 0};
    L.lampPos = {-2, 4, 5}; L.lampDir = Vector3Normalize({2, -3, -5});
    L.AddPoint({2, 2.5f, 3}, 9, {255, 190, 120, 255}, 0.6f);
    rt::RenderBegin(cam, L);
    Crew c; c.role = Role::Bosun; c.slot = 0;
    int keep = gLocalSlot; gLocalSlot = 0;
    SailorLook Lk = LookOf(c);
    gLocalSlot = keep;
    Lk.costume = model && model[0] ? model : nullptr;
    SailorPose P; P.breathe = t * 1.7f;
    DrawSailor(Lk, P, Frame({0, 0, 0}, -PI / 2 + 0.6f * sinf(t * 0.35f) + 0.3f), t, Item::None, WHITE);
    rt::RenderEnd();
}
static bool gTrawlPreviewSet = (skins::gPreview[skins::TRAWL] = &TrawlWardrobePreview, true);
} // namespace tw
