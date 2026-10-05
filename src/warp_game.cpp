// Warp Dodgeball's scene (Scene::Warp): first person, drawn with Red Tide's inked renderer. The rules are warp.cpp's;
// everything you do goes through wd::Input (Gather), the bots write theirs with wd::BotInput.
#include "game.h"
#include "input.h"
#include "redtide_render.h"
#include "rlgl.h"
#include "figure3d.h"
#include "warp.h"
#include "warp_net.h"
#include "arcade_session.h"
#include "sound.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <string>
#include <vector>

namespace {
using namespace wd;

const Color TEAM[2] = {{236, 112, 60, 255}, {60, 168, 214, 255}};
const Color TEAM_DARK[2] = {{120, 50, 30, 255}, {26, 76, 104, 255}};
const char* TEAM_NAME[2] = {"Coral", "Tide"};
const char* BOT_NAMES[12] = {"Bosun", "Kess", "Marlow", "Pip", "Gully", "Rook", "Tamsin", "Ode", "Fen", "Bramble", "Skip", "Wren"};

struct Pop { std::string text; Color col; float t; };
struct Feed { std::string text; float t; };
struct WarpScene {
    bool active = false, shot = false, help = true;
    World Wm; World* hostW = nullptr; arcade::Session* net = nullptr; uint32_t evTotal = 0; uint64_t seenVersion = 0; bool helloSent = false; std::string netName; std::vector<Vector3> smooth;
    int me = 0, perTeam = 4, skill = 1, arena = AR_CLASSIC;
    std::vector<uint32_t> botRng;
    float camYaw = 0, camPitch = 0, t = 0, acc = 0, shake = 0, flash = 0;
    int outView = 0, follow = -1; bool wasAlive = true;   // (out: 0 the broadcast view of the whole court, 1 over your bench, 2 following a teammate still in; Tab cycles)
    size_t evSeen = 0; std::deque<Feed> feed; std::vector<Pop> pops;
    int lastRound = 0; float banner = 0; std::string bannerText; Color bannerCol = WHITE;
    float curveHold = 0, flickX = 0;
    bool overSaved = false;
    Camera3D cam{};
} S;

Model& SphereModel() { static Model m = LoadModelFromMesh(GenMeshSphere(1, 16, 20)); return m; }
Model& RingModel() { static Model m = LoadModelFromMesh(GenMeshTorus(0.12f, 2.0f, 10, 32)); return m; }
Model& DiscModel() { static Model m = LoadModelFromMesh(GenMeshCylinder(1, 1, 28)); return m; }
Color Mix(Color a, Color b, float k) { k = std::clamp(k, 0.0f, 1.0f); return {(unsigned char)(a.r + (b.r - a.r) * k), (unsigned char)(a.g + (b.g - a.g) * k), (unsigned char)(a.b + (b.b - a.b) * k), 255}; }
World& W() { return S.hostW ? *S.hostW : S.Wm; }
Player& Me() { return W().players[std::clamp(S.me, 0, std::max(0, (int)W().players.size() - 1))]; }
std::string NameOf(int id) { return id >= 0 && id < (int)W().players.size() ? W().players[id].name : std::string("the wall"); }

// ---------------------------------------------------------------- the world
// a matrix that lays a unit shape on a panel's plane: x along right, y along up, z out of it
Matrix PanelFrame(Vector3 c, Vector3 n, Vector3 u, float sx, float sy, float sz) {
    Vector3 r = Vector3CrossProduct(u, n);
    Matrix m = {r.x * sx, u.x * sy, n.x * sz, c.x, r.y * sx, u.y * sy, n.y * sz, c.y, r.z * sx, u.z * sy, n.z * sz, c.z, 0, 0, 0, 1};
    return m;
}
// the clip of a portal's view: the exit portal's plane (things behind it are inside the wall, hidden)
bool gClipOn = false; Vector3 gClipP{}, gClipN{};
bool Behind(Vector3 c) { return gClipOn && Vector3DotProduct(Vector3Subtract(c, gClipP), gClipN) < -0.02f; }
bool OnClip(Vector3 c, Vector3 n) { return gClipOn && fabsf(Vector3DotProduct(Vector3Subtract(c, gClipP), gClipN)) < 0.06f && Vector3DotProduct(n, gClipN) > 0.9f; }
void DrawArena() {
    // (a portal's view: nothing behind the exit portal's plane, nor the plate the exit portal sits on)
    auto cube = [&](Vector3 c, Vector3 s, Color col) { if (!Behind(c)) rt::DrawWorldCube(c, s, col); };
    const Arena& a = W().arena; float X = a.OuterX(), Z = a.OuterZ();
    // the floor: the run-off dark, the court in two halves, the lines
    cube({0, -0.1f, 0}, {X * 2, 0.2f, Z * 2}, {70, 62, 58, 255});
    for (int s = 0; s < 2; s++) cube({(s ? 1 : -1) * a.halfL / 2, -0.04f, 0}, {a.halfL, 0.1f, a.halfW * 2}, Mix(Color{176, 132, 88, 255}, TEAM[s], 0.12f));
    auto line = [&](float x0, float z0, float x1, float z1, Color c) { cube({(x0 + x1) / 2, 0.02f, (z0 + z1) / 2}, {std::max(0.06f, fabsf(x1 - x0)), 0.02f, std::max(0.06f, fabsf(z1 - z0))}, c); };
    Color W{236, 232, 220, 255};
    line(0, -a.halfW, 0, a.halfW, {250, 210, 80, 255});
    for (int s = -1; s <= 1; s += 2) { line(s * Cfg().attackLine, -a.halfW, s * Cfg().attackLine, a.halfW, W); line(s * a.halfL, -a.halfW, s * a.halfL, a.halfW, W); }
    line(-a.halfL, -a.halfW, a.halfL, -a.halfW, W); line(-a.halfL, a.halfW, a.halfL, a.halfW, W);
    // the walls (dark) and the ceiling
    Color wall{48, 54, 66, 255};
    cube({-X - 0.15f, a.ceil / 2, 0}, {0.3f, a.ceil, Z * 2}, wall); cube({X + 0.15f, a.ceil / 2, 0}, {0.3f, a.ceil, Z * 2}, wall);
    cube({0, a.ceil / 2, -Z - 0.15f}, {X * 2, a.ceil, 0.3f}, wall); cube({0, a.ceil / 2, Z + 0.15f}, {X * 2, a.ceil, 0.3f}, wall);
    cube({0, a.ceil + 0.15f, 0}, {X * 2, 0.3f, Z * 2}, {40, 44, 54, 255});
    // the light grey portal panels (a raised plate, so they read from across the court)
    for (const auto& p : a.panels) if (!Behind(p.c) && !OnClip(p.c, p.n)) rt::DrawCubeM(PanelFrame(Vector3Add(p.c, Vector3Scale(p.n, 0.015f)), p.n, p.u, p.hw * 2, p.hh * 2, 0.03f), {206, 210, 216, 255});
    // the pieces
    for (const auto& b : a.boxes) {
        Vector3 c{(b.lo.x + b.hi.x) / 2, (b.lo.y + b.hi.y) / 2, (b.lo.z + b.hi.z) / 2}, s{b.hi.x - b.lo.x, b.hi.y - b.lo.y, b.hi.z - b.lo.z};
        Color col = b.kind == 1 ? Color{96, 100, 110, 255} : b.kind == 2 ? Color{60, 64, 76, 255} : b.kind == 3 ? Color{130, 96, 62, 255} : Color{150, 160, 172, 255};
        cube(c, s, col);
        if (b.kind == 3) for (int k = -1; k <= 1; k += 2) for (int j = -1; j <= 1; j += 2) cube({c.x + k * (s.x / 2 - 0.08f), b.lo.y / 2, c.z + j * (s.z / 2 - 0.08f)}, {0.12f, b.lo.y, 0.12f}, {80, 70, 60, 255});   // (the nest's legs)
    }
    for (const auto& l : a.ladders) for (int r = 0; r < 8; r++) {
        cube({l.base.x, (r + 0.5f) * l.top / 8, l.base.z}, {0.06f, 0.06f, 0.6f}, {200, 170, 80, 255});
        if (!r) for (int k = -1; k <= 1; k += 2) cube({l.base.x, l.top / 2, l.base.z + k * 0.3f}, {0.07f, l.top, 0.07f}, {170, 140, 60, 255});
    }
    // banners over each end in the team's colour
    for (int s = 0; s < 2; s++) rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.1f, 0.5f, a.halfW * 1.4f), MatrixTranslate((s ? 1 : -1) * (X - 0.05f), a.wall + (a.ceil - a.wall) * 0.5f, 0)), TEAM[s], 0.25f);
}
// Portal colours as each player sees them (the playtest): your own pair cyan (A) and royal blue (B), a teammate's in
// steel blues, the other team's in reds (orange-red A, crimson B)
Color PortalColour(const Player& owner, int k) {
    if (owner.id == S.me) return k == 0 ? Color{90, 220, 255, 255} : Color{60, 100, 255, 255};
    if (owner.team == Me().team) return k == 0 ? Color{130, 170, 220, 255} : Color{80, 110, 190, 255};
    return k == 0 ? Color{255, 110, 60, 255} : Color{220, 30, 50, 255};
}
// the window: where the view through a portal is seen from (the camera carried through the pair: in at A, out at B)
static Vector3 Through(Vector3 v, const Portal& a, const Portal& b) {
    Vector3 ra = Vector3CrossProduct(a.u, a.n), rb = Vector3CrossProduct(b.u, b.n);
    float x = Vector3DotProduct(v, ra), y = Vector3DotProduct(v, a.u), z = Vector3DotProduct(v, a.n);
    return Vector3Add(Vector3Add(Vector3Scale(rb, -x), Vector3Scale(b.u, y)), Vector3Scale(b.n, -z));
}
Camera3D PortalCam(const Camera3D& c, const Portal& a, const Portal& b) {
    Camera3D v = c;
    v.position = Vector3Add(b.c, Through(Vector3Subtract(c.position, a.c), a, b));
    v.target = Vector3Add(v.position, Through(Vector3Subtract(c.target, c.position), a, b));
    v.up = Through(c.up, a, b);
    return v;
}
struct PortalView { int owner = -1, k = 0; RenderTexture2D rt{}; bool live = false; };
PortalView gPV[2];
// ---------------------------------------------------------------- the dressing (the playtest: "funny posters on walls (portal
// goes over the posters) and benches on the side of the arena"): gym posters painted in code on the portal walls, under
// the portals; team benches along the sidelines, where the out players sit, with towels, bottles and kit bags
struct PosterDef { const char* top; const char* big; const char* small; Color bg, ink; int art; };
static const PosterDef POSTERS[] = {
    {"THE MANAGEMENT SAYS", "THINK WITH\nPORTALS", "throw with your arms", {40, 90, 160, 255}, {250, 240, 220, 255}, 0},
    {"", "DODGE\nDUCK\nDIP\nDIVE\n...WARP", "the five Ds", {200, 60, 40, 255}, {255, 236, 200, 255}, 1},
    {"NOTICE", "NO WARPING\nIN THE\nSHOWERS", "we mean it, Gary", {230, 226, 210, 255}, {30, 30, 36, 255}, 2},
    {"EMPLOYEE OF", "THE MONTH", "the ball (again)", {250, 210, 70, 255}, {60, 30, 20, 255}, 3},
    {"LOST", "ONE LEFT\nSHOE", "last seen going\ninto portal B", {236, 232, 220, 255}, {40, 40, 40, 255}, 4},
    {"", "HYDRATE\nOR\nDIE-DRATE", "water is free", {40, 150, 170, 255}, {250, 250, 240, 255}, 5},
    {"CAUTION", "BALL MAY\nEXIT FROM\nCEILING", "look up sometimes", {250, 200, 30, 255}, {20, 20, 20, 255}, 6},
    {"PORTALS ARE", "NOT FOR\nSNACK\nSTORAGE", "this means you", {120, 60, 160, 255}, {250, 240, 250, 255}, 7},
    {"TEAM SPIRIT!", "(MANDATORY)", "smile for the cameras", {60, 160, 90, 255}, {250, 250, 240, 255}, 8},
    {"IF YOU CAN", "DODGE A\nPORTAL", "you can dodge a ball", {30, 30, 40, 255}, {250, 200, 90, 255}, 9},
};
constexpr int POSTER_N = (int)(sizeof(POSTERS) / sizeof(POSTERS[0]));
static void ImageEllipse(Image* im, int cx, int cy, int rx, int ry, Color c) { for (int y = -ry; y <= ry; y++) { float w = rx * sqrtf(std::max(0.0f, 1 - (float)(y * y) / (ry * ry))); ImageDrawRectangle(im, cx - (int)w, cy + y, (int)(2 * w) + 1, 1, c); } }
static Texture2D PosterTex(int k) {
    static Texture2D t[POSTER_N] = {}; if (t[k].id) return t[k];
    const PosterDef& d = POSTERS[k]; const int W = 256, H = 360;
    Image im = GenImageColor(W, H, d.bg);
    ImageDrawRectangleLines(&im, {6, 6, W - 12.0f, H - 12.0f}, 4, d.ink);
    Font f = GetFontDefault();
    auto centred = [&](const char* s, int y, int size) { int ly = y; std::string str = s; size_t a = 0; while (a <= str.size()) { size_t b = str.find('\n', a); std::string line = str.substr(a, b == std::string::npos ? std::string::npos : b - a); Vector2 m = MeasureTextEx(f, line.c_str(), (float)size, size / 10.0f); ImageDrawTextEx(&im, f, line.c_str(), {(W - m.x) / 2, (float)ly}, (float)size, size / 10.0f, d.ink); ly += size + 4; if (b == std::string::npos) break; a = b + 1; } return ly; };
    int y = 22; if (d.top[0]) y = centred(d.top, y, 20) + 6;
    // a little picture under the heading
    Color c2 = ColorLerp(d.bg, d.ink, 0.5f);
    switch (d.art) {
        case 3: ImageDrawCircle(&im, W / 2, y + 60, 50, Color{220, 70, 60, 255}); ImageDrawCircle(&im, W / 2 - 18, y + 48, 7, WHITE); ImageDrawCircle(&im, W / 2 + 18, y + 48, 7, WHITE); ImageDrawCircle(&im, W / 2 - 18, y + 48, 3, BLACK); ImageDrawCircle(&im, W / 2 + 18, y + 48, 3, BLACK); ImageDrawRectangle(&im, W / 2 - 22, y + 78, 44, 6, BLACK); y += 125; break;
        case 6: for (int r = 0; r < 3; r++) ImageDrawCircleLines(&im, W / 2, y + 40, 22 + r * 8, d.ink); ImageDrawCircle(&im, W / 2, y + 95, 16, Color{220, 70, 60, 255}); y += 125; break;
        case 0: case 7: case 9: ImageEllipse(&im, W / 2 - 40, y + 45, 22, 38, Color{90, 220, 255, 255}); ImageEllipse(&im, W / 2 + 40, y + 45, 22, 38, Color{255, 120, 60, 255}); ImageEllipse(&im, W / 2 - 40, y + 45, 15, 30, c2); ImageEllipse(&im, W / 2 + 40, y + 45, 15, 30, c2); y += 98; break;
        case 4: ImageDrawRectangle(&im, W / 2 - 40, y + 40, 70, 26, d.ink); ImageDrawRectangle(&im, W / 2 - 40, y + 20, 26, 30, d.ink); y += 82; break;
        case 5: ImageDrawRectangle(&im, W / 2 - 16, y + 10, 32, 70, Color{200, 240, 250, 255}); ImageDrawRectangle(&im, W / 2 - 8, y, 16, 12, d.ink); y += 90; break;
        default: ImageDrawCircle(&im, W / 2, y + 30, 26, Color{220, 70, 60, 255}); y += 66; break;
    }
    y = centred(d.big, y + 6, 30);
    centred(d.small, std::max(y + 8, H - 70), 16);
    ImageFlipVertical(&im);   // (the cube's face maps its texture upside down)
    t[k] = LoadTextureFromImage(im); UnloadImage(im); SetTextureFilter(t[k], TEXTURE_FILTER_BILINEAR); GenTextureMipmaps(&t[k]); return t[k];
}
static Model& PosterModel(int k) {
    static Model m[POSTER_N] = {}; static bool made[POSTER_N] = {};
    if (!made[k]) { m[k] = LoadModelFromMesh(GenMeshCube(1, 1, 1)); m[k].materials[0].maps[MATERIAL_MAP_ALBEDO].texture = PosterTex(k); m[k].materials[0].maps[MATERIAL_MAP_METALNESS].value = 0; m[k].materials[0].maps[MATERIAL_MAP_ROUGHNESS].value = 0.85f; made[k] = true; }
    return m[k];
}
void DrawDressing() {
    const Arena& a = W().arena; float X = a.OuterX(), Z = a.OuterZ();
    // posters: on the side walls in a row at eye height and above, two on each end wall; each a different one (a portal opens over them)
    int k = 0;
    auto poster = [&](Vector3 c, Vector3 n) { if (Behind(c) || OnClip(c, n)) { k++; return; } rt::DrawPbr(PosterModel(k % POSTER_N), PanelFrame(Vector3Add(c, Vector3Scale(n, 0.034f)), n, {0, 1, 0}, 0.9f, 1.26f, 0.004f), WHITE, 0.3f); k++; };
    for (int s = -1; s <= 1; s += 2) for (int j = 0; j < 3; j++) poster({(j - 1) * X * 0.55f + s * 1.2f, 2.9f, s * Z}, {0, 0, (float)-s});
    for (int s = -1; s <= 1; s += 2) for (int j = -1; j <= 1; j += 2) poster({s * X, 3.1f, j * Z * 0.5f}, {(float)-s, 0, 0});
    // the benches (where the out players sit), a towel, bottles and a kit bag at each
    bool classic = a.kind == AR_CLASSIC; float bz = classic ? a.halfW + 1.2f + 0.22f : Z - 0.28f;
    auto cube = [&](Vector3 c, Vector3 s, Color col) { if (!Behind(c)) rt::DrawWorldCube(c, s, col); };
    for (int t = 0; t < 2; t++) {
        float s = t == 0 ? -1.0f : 1.0f, x0 = s * 1.5f, x1 = s * 5.5f, cx = (x0 + x1) / 2, len = fabsf(x1 - x0);
        for (float zz : {bz, -bz}) {
            float zs = zz > 0 ? 1.0f : -1.0f;
            cube({cx, 0.43f, zz}, {len, 0.06f, 0.4f}, Color{168, 120, 72, 255});   // the seat
            cube({cx, 0.43f, zz + zs * 0.18f}, {len, 0.07f, 0.06f}, Color{140, 98, 58, 255});
            for (float u : {0.08f, 0.5f, 0.92f}) cube({x0 + (x1 - x0) * u, 0.2f, zz}, {0.06f, 0.4f, 0.32f}, Color{70, 70, 76, 255});   // the legs
            if (zz > 0) {   // (on the far side only: a towel, two bottles and a bag in the team's colour)
                cube({x0 + (x1 - x0) * 0.75f, 0.47f, zz}, {0.45f, 0.03f, 0.32f}, Mix(TEAM[t], WHITE, 0.55f));
                for (int b = 0; b < 2; b++) { Vector3 bc{x0 + (x1 - x0) * (0.86f + b * 0.05f), 0.56f, zz - 0.05f}; cube(bc, {0.07f, 0.2f, 0.07f}, Color{180, 220, 240, 255}); cube({bc.x, 0.69f, bc.z}, {0.035f, 0.05f, 0.035f}, TEAM[t]); }
                cube({x0 - s * 0.6f, 0.17f, zz + 0.05f}, {0.7f, 0.34f, 0.32f}, TEAM_DARK[t]);
            }
        }
    }
}
void DrawPortals(bool views) {
    const Config& C = Cfg();
    for (const auto& p : W().players) for (int k = 0; k < 2; k++) {
        const Portal& o = p.portal[k]; if (!o.on) continue;
        Vector3 cc = Vector3Add(o.c, Vector3Scale(o.n, 0.03f)), rr = Vector3CrossProduct(o.u, o.n);
        if (gClipOn && (Behind(cc) || OnClip(o.c, o.n))) continue;   // (in a portal's own view, not the exit itself)
        bool both = p.portal[0].on && p.portal[1].on;
        Color c = PortalColour(p, k);
        float pulse = 0.8f + 0.2f * sinf(S.t * 5 + p.id + k);
        // the rim: a ring of glowing segments round the oval
        for (int j = 0; j < 28; j++) {
            float a0 = j * 2 * PI / 28 + (both ? S.t * 0.8f : 0), ca = cosf(a0), sa = sinf(a0);
            Vector3 q = Vector3Add(cc, Vector3Add(Vector3Scale(rr, ca * C.portalW / 2), Vector3Scale(o.u, sa * C.portalH / 2)));
            Vector3 tng = Vector3Normalize(Vector3Add(Vector3Scale(rr, -sa * C.portalW / 2), Vector3Scale(o.u, ca * C.portalH / 2)));
            rt::DrawCubeGlow(PanelFrame(q, o.n, tng, 0.07f, 0.2f, 0.05f), j % 2 ? c : Mix(c, WHITE, 0.4f), (both ? 1.6f : 0.7f) * pulse);
        }
        if (!both) continue;
        // the face: the view out of its partner where it's being rendered this frame; elsewhere a swirl in its colour
        const PortalView* pv = nullptr; if (views) for (const auto& v : gPV) if (v.live && v.owner == p.id && v.k == k) pv = &v;
        Matrix face = PanelFrame(cc, o.n, o.u, C.portalW / 2 * 0.93f, C.portalH / 2 * 0.93f, 0.012f);
        if (pv) rt::DrawScreenTex(SphereModel(), face, pv->rt.texture, Mix(WHITE, c, 0.12f));
        else rt::DrawStaticGlow(SphereModel(), face, Mix(c, Color{30, 20, 60, 255}, 0.5f + 0.1f * sinf(S.t * 3 + k)), 0.7f);
    }
}const Model* Body() { return rt::LoadAsset("shared/crew/crew_diver.glb"); }
void DrawPlayer(const Player& p, bool sideline, int slot) {
    if (p.id == S.me && !sideline && p.alive && !gClipOn) return;   // (your own body: first person; seen through a portal it's there)
    if (Behind(p.pos)) return;
    const Model* m = Body(); if (!m) return;
    fig::Pose P; fig::Build B; B.build = 1.0f + 0.04f * (p.id % 3);
    Vector3 feet = p.pos; float yaw = p.yaw;
    if (sideline) {   // out: standing along their side of the court, in catch order
        float s = p.team == 0 ? -1.0f : 1.0f; float z = W().arena.kind == AR_CLASSIC ? W().arena.halfW + 1.2f : W().arena.OuterZ() - 0.5f;
        feet = {s * (2.0f + slot * 0.9f), 0, z}; yaw = -PI / 2; P.breathe = S.t * 2 + p.id;
        P.sit = 1; P.grip = 0.2f; P.look = sinf(S.t * 0.4f + p.id) * 0.3f;   // (sitting on the team bench, watching)
    } else {
        float spd = Vector3Length({p.vel.x, 0, p.vel.z});
        P.walk = std::min(1.0f, spd / 5); P.walkPh = S.t * (4 + spd) + p.id;
        if (p.po == PO_CROUCH) P.crouch = 0.6f; if (p.po == PO_SQUAT) P.crouch = 1;
        if (p.charging) { P.elbow = 0.4f + 0.6f * p.charge; P.reach = 0.2f; }
        if (p.releaseT > 0) P.swingT = 1 - p.releaseT / Cfg().releaseTime;
        if (p.po == PO_LADDER) { P.reach = 1; P.elbow = 1; }
        P.breathe = S.t * 2 + p.id; P.look = 0;
    }
    std::vector<Matrix> skin = fig::PoseFigure(*m, B, P, S.t);
    Matrix frame = fig::Frame(feet, -yaw);
    if (!sideline && (p.po == PO_DIVE || p.po == PO_PRONE)) frame = MatrixMultiply(MatrixMultiply(MatrixRotateZ(-PI * 0.5f), MatrixTranslate(0, 0.25f, 0)), fig::Frame(feet, -yaw));
    Color top = sideline ? Mix(TEAM[p.team], Color{90, 90, 96, 255}, 0.5f) : TEAM[p.team];
    std::vector<rt::Recolor> rc = {{"top", top}, {"trousers", TEAM_DARK[p.team]}, {"hat", TEAM[p.team]}, {"skin", Color{(unsigned char)(200 + 10 * (p.id % 3)), (unsigned char)(150 + 12 * (p.id % 4)), 120, 255}}};
    rt::DrawPbrSkinned(*m, frame, skin, rc, 0.35f, WHITE);
}
void DrawBalls() {
    const Config& C = Cfg();
    for (const auto& b : W().balls) {
        if (b.st == BS_HELD && b.holder == S.me && Me().alive) continue;   // (your own: the viewmodel)
        Color c = b.st == BS_LIVE ? Color{240, 70, 60, 255} : b.st == BS_DEAD ? Color{150, 60, 56, 255} : Color{214, 80, 66, 255};
        float glow = b.st == BS_LIVE ? 0.5f : 0.0f;
        if (b.st == BS_REST && b.mustCarry) glow = 0.25f + 0.2f * sinf(S.t * 4);
        Matrix m = MatrixMultiply(MatrixScale(C.ballR, C.ballR, C.ballR), MatrixTranslate(b.p.x, b.p.y, b.p.z));
        if (glow > 0) rt::DrawStaticGlow(SphereModel(), m, c, glow); else rt::DrawStatic(SphereModel(), m, c);
        if (b.st == BS_LIVE) for (int k = 1; k <= 4; k++) {   // (a short trail)
            Vector3 q = Vector3Subtract(b.p, Vector3Scale(b.v, 0.012f * k)); float r = C.ballR * (1 - 0.18f * k);
            rt::DrawStaticGlow(SphereModel(), MatrixMultiply(MatrixScale(r, r, r), MatrixTranslate(q.x, q.y, q.z)), Mix(c, WHITE, 0.3f), 0.5f - 0.1f * k);
        }
    }
}
void DrawViewmodel() {
    const Player& p = Me(); if (!p.alive || p.ball < 0) return;
    // the ball in your right hand: low right, drawn back with the charge, flung forward on the release
    Vector3 f = Vector3Normalize(Vector3Subtract(S.cam.target, S.cam.position)), r = Vector3Normalize(Vector3CrossProduct(f, {0, 1, 0})), u = Vector3CrossProduct(r, f);
    float back = p.charging ? 0.15f * p.charge : 0, fling = p.releaseT > 0 ? (1 - p.releaseT / Cfg().releaseTime) * 0.4f : 0;
    float shiver = p.overT > Cfg().overAfter ? 0.006f * sinf(S.t * 60) : 0;
    Vector3 at = Vector3Add(S.cam.position, Vector3Add(Vector3Scale(f, 0.62f - back + fling), Vector3Add(Vector3Scale(r, 0.19f + shiver + S.flickX * 0.002f), Vector3Scale(u, -0.2f + back * 0.4f))));
    float R = Cfg().ballR * 0.8f;
    rt::DrawStatic(SphereModel(), MatrixMultiply(MatrixScale(R, R, R), MatrixTranslate(at.x, at.y, at.z)), {214, 80, 66, 255});
}

// ---------------------------------------------------------------- events: pops, the feed, sound
void ReadEvents() {
    auto& ev = W().events;
    if (S.evSeen > ev.size()) S.evSeen = 0;
    for (; S.evSeen < ev.size(); S.evSeen++) {
        const Event& e = ev[S.evSeen];
        bool mine = e.who == S.me, byMe = e.by == S.me;
        switch (e.kind) {
            case EV_THROW: if (!S.shot) PlayCue("hit.throw", byMe || mine ? 0.7f : 0.3f); break;
            case EV_CATCH: S.feed.push_back({NameOf(e.who) + " CAUGHT " + NameOf(e.by) + "'s throw", S.t}); if (mine) { S.pops.push_back({"CAUGHT!", {120, 255, 160, 255}, S.t}); } if (!S.shot) PlayCue("ui.click", 0.8f); break;
            case EV_FUMBLE: if (mine) S.pops.push_back({"FUMBLED", {255, 140, 90, 255}, S.t}); break;
            case EV_BLOCK: if (mine) S.pops.push_back({"BLOCKED", {200, 220, 255, 255}, S.t}); if (!S.shot) PlayCue("imp.shell", 0.6f); break;
            case EV_HIT: if (mine) { S.shake = 0.5f; S.flash = 0.6f; } if (byMe) S.pops.push_back({"HIT!", {255, 220, 90, 255}, S.t}); if (!S.shot) PlayCue("imp.flesh", mine || byMe ? 0.9f : 0.4f); break;
            case EV_OUT: { const Player& v = W().players[e.who]; S.feed.push_back({NameOf(e.who) + " out: " + v.outCause + (e.by >= 0 && e.by != e.who ? " (" + NameOf(e.by) + ")" : ""), S.t}); break; }
            case EV_TRANSIT: if (byMe) S.pops.push_back({"WARP!", {190, 150, 255, 255}, S.t}); break;
            case EV_PORTAL_FAIL: if (mine) S.pops.push_back({"No panel there", {200, 200, 200, 255}, S.t}); break;
            case EV_BOUNCE: if (!S.shot) { float d = Vector3Distance(e.at, S.cam.position); if (d < 14) PlayCue("imp.stone", 0.35f * (1 - d / 14)); } break;
            case EV_PICKUP: if (mine && !S.shot) PlayCue("ui.click", 0.4f); break;
            case EV_PORTAL_PLACE: if (!S.shot) { float d = Vector3Distance(e.at, S.cam.position); PlayCue("ui.click", mine ? 0.7f : std::max(0.0f, 0.4f - d / 60)); } break;
            case EV_RETURN: S.feed.push_back({NameOf(e.who) + " is back in", S.t}); break;
            case EV_WHISTLE: S.banner = 1.4f; S.bannerText = "RUSH!"; S.bannerCol = {250, 220, 90, 255}; if (!S.shot) PlayCue("ui.click", 1); break;
            case EV_LINE: if (mine) S.pops.push_back({"Over the line!", {255, 120, 90, 255}, S.t}); break;
            default: break;
        }
    }
    while (S.feed.size() > 5) S.feed.pop_front();
    while (!S.feed.empty() && S.t - S.feed.front().t > 8) S.feed.pop_front();
    S.pops.erase(std::remove_if(S.pops.begin(), S.pops.end(), [](const Pop& p) { return S.t - p.t > 1.2f; }), S.pops.end());
}

// ---------------------------------------------------------------- input
void Gather() {
    Player& p = Me(); Input in;
    Vector2 md = MouseLook(!S.shot && p.alive && W().phase != PH_MATCH_END);
    S.camYaw += md.x * 0.0026f; S.camPitch = std::clamp(S.camPitch - md.y * 0.0026f, -1.4f, 1.4f);
    S.flickX = S.flickX * 0.85f + md.x * 0.15f;
    in.yaw = S.camYaw; in.pitch = S.camPitch;
    in.moveX = (IsKeyDown(KEY_W) ? 1.0f : 0) - (IsKeyDown(KEY_S) ? 1.0f : 0); in.moveZ = (IsKeyDown(KEY_D) ? 1.0f : 0) - (IsKeyDown(KEY_A) ? 1.0f : 0);
    in.sprint = IsKeyDown(KEY_LEFT_SHIFT); in.jump = IsKeyPressed(KEY_SPACE); in.crouch = IsKeyDown(KEY_LEFT_CONTROL);
    in.squat = IsKeyPressed(KEY_C); in.dive = IsKeyPressed(KEY_Q);
    in.throwHeld = IsMouseButtonDown(MOUSE_BUTTON_LEFT); in.cancel = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
    // curve: Z or X held at the release, or a sideways flick of the mouse as you let go
    float c = (IsKeyDown(KEY_X) ? 1.0f : 0) - (IsKeyDown(KEY_Z) ? 1.0f : 0);
    if (c == 0 && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) c = std::clamp(S.flickX / 25.0f, -1.0f, 1.0f);
    in.curve = c;
    in.portalA = IsKeyPressed(KEY_E); in.portalB = IsKeyPressed(KEY_R);
    in.catchP = IsKeyPressed(KEY_F) || IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE);
    if (IsKeyPressed(KEY_H)) S.help = !S.help;
    p.in = in;
}

// ---------------------------------------------------------------- the HUD
void DrawHud(Game& g) {
    const Config& C = Cfg(); const World& w = W(); const Player& p = Me();
    float cx = SCREEN_W / 2.0f, cy = SCREEN_H / 2.0f;
    if (S.flash > 0) DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{255, 60, 40, (unsigned char)(120 * S.flash)});
    // the scoreboard: round pips, the clock, who's standing
    Rectangle sb{cx - 210, 10, 420, 58}; DrawRectangleRounded(sb, 0.3f, 6, Color{14, 18, 26, 210});
    for (int s = 0; s < 2; s++) {
        float x = s ? cx + 60 : cx - 60; TxtBold(TEAM_NAME[s], s ? x : x - MeasureTxt(TEAM_NAME[s], 18, true), 16, 18, TEAM[s]);
        for (int k = 0; k < C.roundsToWin; k++) { float px = s ? x + 6 + k * 16 : x - 12 - k * 16; DrawCircle((int)px, 50, 6, k < w.wins[s] ? TEAM[s] : Color{60, 66, 80, 255}); }
        for (int k = 0; k < (int)w.players.size() / 2; k++) { const Player& q = w.players[s * ((int)w.players.size() / 2) + k]; float px = s ? cx + 200 - k * 13 : cx - 200 + k * 13; DrawRectangle((int)px - 4, 22, 8, 22, q.alive ? TEAM[s] : Color{50, 50, 60, 255}); }
    }
    float left = w.phase == PH_LIVE || w.phase == PH_RUSH ? std::max(0.0f, C.roundTime - w.phaseT) : C.roundTime;
    DrawTextCenteredBold(TextFormat("%d:%02d", (int)left / 60, (int)left % 60), cx, 18, 24, left < 20 ? Color{255, 120, 90, 255} : WHITE);
    DrawTextCentered(w.suddenDeath ? "SUDDEN DEATH" : TextFormat("Round %d", w.round), cx, 46, 13, Color{180, 190, 210, 255});
    // the feed
    for (size_t i = 0; i < S.feed.size(); i++) Txt(S.feed[i].text, SCREEN_W - 20 - MeasureTxt(S.feed[i].text, 14), 84 + i * 20, 14, Color{230, 230, 236, (unsigned char)(255 * std::min(1.0f, (8 - (S.t - S.feed[i].t)) / 1.5f))});
    // warm-up and banners
    if (w.phase == PH_WARMUP) DrawTextCenteredBold(TextFormat("%d", (int)ceilf(C.rushWhistle - w.phaseT)), cx, cy - 120, 54, {250, 220, 90, 255});
    if (S.banner > 0) DrawTextCenteredBold(S.bannerText, cx, cy - 130, 48, Fade(S.bannerCol, std::min(1.0f, S.banner)));
    if (w.phase == PH_ROUND_END && w.roundWinner >= 0) DrawTextCenteredBold(TextFormat("%s take round %d", TEAM_NAME[w.roundWinner], w.round), cx, cy - 140, 36, TEAM[w.roundWinner]);
    for (size_t i = 0; i < S.pops.size(); i++) { float a = S.t - S.pops[i].t; DrawTextCenteredBold(S.pops[i].text, cx, cy + 50 + i * 30 - a * 30, 26, Fade(S.pops[i].col, 1 - a / 1.2f)); }
    if (w.phase == PH_MATCH_END) {
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{0, 0, 0, 140});
        bool won = w.champion == p.team;
        DrawTextCenteredBold(won ? "YOUR TEAM WINS" : TextFormat("%s WIN", TEAM_NAME[w.champion]), cx, cy - 120, 48, TEAM[w.champion]);
        DrawTextCentered(TextFormat("%d - %d", w.wins[0], w.wins[1]), cx, cy - 60, 28, WHITE);
        // the stat line per player
        for (size_t i = 0; i < w.players.size(); i++) { const Player& q = w.players[i]; float x = cx + (q.team ? 40 : -360), y = cy - 10 + (i % (w.players.size() / 2)) * 22; Txt(TextFormat("%-10s  catches %d   outs %d", q.name.c_str(), q.catches, q.outs), x, y, 15, i == (size_t)S.me ? WHITE : TEAM[q.team]); }
        if (S.net) {
            if (S.net->role == arcade::R_HOST) { if (Button({cx - 230, cy + 140, 200, 40}, "Rematch", true, 16)) { std::string why; S.net->Rematch(&why); S.lastRound = 0; return; } }
            else DrawTextCentered("Waiting for the host: a rematch, or back to the lobby", cx, cy + 150, 15, Color{200, 210, 230, 255});
            if (Button({cx + 30, cy + 140, 200, 40}, S.net->role == arcade::R_HOST ? "Back to the lobby" : "Leave", true, 16)) { LeaveWarp(g); return; }
            return;
        }
        if (Button({cx - 230, cy + 140, 200, 40}, "Again", true, 16)) { StartWarp(g, S.perTeam, S.skill, S.arena); return; }
        if (Button({cx + 30, cy + 140, 200, 40}, "Back to the arcade", true, 16)) { LeaveWarp(g); return; }
        return;
    }
    if (!p.alive) {
        // (out: the court stays clear; the word and the line at the top, the view's name at the bottom)
        DrawRectangle(0, 70, SCREEN_W, 74, Color{0, 0, 0, 110});
        DrawTextCenteredBold(p.outCause.empty() ? "OUT" : ("OUT  (" + p.outCause + ")").c_str(), cx, 76, 30, {255, 120, 90, 255});
        int pos = 0; for (size_t i = 0; i < w.sideline[p.team].size(); i++) if (w.sideline[p.team][i] == S.me) pos = (int)i + 1;
        DrawTextCentered(TextFormat("A catch by your team brings you back (you're %s in line)", pos == 1 ? "first" : pos == 2 ? "second" : pos == 3 ? "third" : "further back"), cx, 114, 15, {200, 210, 230, 255});
        std::string vn = S.outView == 0 ? "The whole court" : S.outView == 1 ? "Over your bench" : (S.follow >= 0 ? "Following " + NameOf(S.follow) : "Following");
        DrawTextCentered(vn + "   (Tab: change view, mouse: look)", cx, SCREEN_H - 40.0f, 15, {220, 226, 236, 255});
        return;
    }
    // the crosshair and the charge ring
    DrawCircleLines((int)cx, (int)cy, 3, WHITE);
    if (p.charging || p.releaseT > 0) {
        float k = p.charge; Color c = p.overT > C.overAfter ? Color{255, 90, 70, 255} : Mix(Color{250, 230, 120, 255}, Color{255, 140, 60, 255}, k);
        DrawRing({cx, cy}, 18, 23, -90, -90 + 360 * k, 40, c);
        if (p.overT > C.overAfter) DrawTextCentered("shaking - let it go", cx, cy + 30, 13, c);
        float cv = (IsKeyDown(KEY_X) ? 1.0f : 0) - (IsKeyDown(KEY_Z) ? 1.0f : 0);
        if (cv != 0) DrawTextCentered(cv > 0 ? "curve >" : "< curve", cx, cy - 40, 14, {200, 200, 255, 255});
    }
    // the catch prompt: an enemy ball coming at you inside the cone, 0.6 s out (spec "Catching")
    int bi; float tc;
    if (w.Threat(p, &bi, &tc)) {
        float win = w.CatchWindow(Vector3Length(w.balls[bi].v));
        bool now = tc <= win * 0.5f;
        float r = 34 + 60 * (tc / C.lookahead);
        DrawRing({cx, cy}, r, r + 4, 0, 360, 40, now ? Color{120, 255, 150, 255} : Color{255, 255, 255, 160});
        DrawRing({cx, cy}, 34, 37, 0, 360, 40, Color{120, 255, 150, 200});
        DrawTextCenteredBold(now ? "F - CATCH!" : "F", cx, cy + 46, now ? 26 : 20, now ? Color{120, 255, 150, 255} : WHITE);
    }
    // the portals: A and B with their cooldowns
    for (int k = 0; k < 2; k++) {
        float x = 30 + k * 64, y = SCREEN_H - 70; Color c = k == 0 ? Color{255, 190, 90, 255} : Color{140, 140, 255, 255};
        DrawEllipseLines((int)x + 20, (int)y + 24, 14, 22, p.portal[k].on ? c : Fade(c, 0.35f));
        if (p.portal[k].on) DrawEllipse((int)x + 20, (int)y + 24, 10, 17, Fade(c, 0.45f));
        if (p.portal[k].cool > 0) DrawRectangle((int)x + 4, (int)y + 50, (int)(32 * p.portal[k].cool / C.portalCool), 4, c);
        TxtBold(k == 0 ? "E" : "R", x + 16, y - 16, 14, WHITE);
    }
    if (p.ball >= 0 && W().balls[p.ball].mustCarry) DrawTextCentered("Carry it back behind your attack line first", cx, SCREEN_H - 120.0f, 16, {250, 220, 90, 255});
    if (p.po == PO_SQUAT || p.po == PO_DIVE || p.po == PO_PRONE) DrawTextCentered(p.po == PO_SQUAT ? "squat" : p.po == PO_DIVE ? "dive!" : "getting up...", cx, SCREEN_H - 96.0f, 15, {200, 210, 230, 255});
    if (S.help) {
        Rectangle r{SCREEN_W - 300.0f, SCREEN_H - 250.0f, 284, 236}; DrawRectangleRounded(r, 0.08f, 6, Color{10, 14, 22, 200});
        const char* L[] = {"WASD move, Shift sprint, Space jump", "Ctrl crouch, C squat (quick duck), Q dive", "Hold LMB to charge, release to throw", "RMB cancels; Z / X (or a flick) curves", "E / R: portal A / B on a grey panel", "F: catch when the ring goes green", "Hit with a live ball: out. Caught: the", "thrower's out and a teammate is back.", "Your own ball off a wall or through", "a portal can get YOU out.  H hides this"};
        for (int i = 0; i < 10; i++) Txt(L[i], r.x + 12, r.y + 10 + i * 22, 14, i < 6 ? WHITE : Color{200, 210, 230, 255});
    }
}

void StepCamera(float dt) {
    const Player& p = Me();
    const Arena& ar = W().arena;
    auto aimAt = [&](Vector3 from, Vector3 to) { Vector3 d = Vector3Normalize(Vector3Subtract(to, from)); S.camYaw = atan2f(d.z, d.x); S.camPitch = asinf(std::clamp(d.y, -1.0f, 1.0f)); };
    if (p.alive) { S.cam.position = p.Eye(); S.wasAlive = true; }
    else {   // out (the playtest: "when out you should get a full view of the game"): a broadcast camera high on the long side that frames the whole court; Tab for the view over your bench or to follow a teammate
        bool entering = S.wasAlive; S.wasAlive = false;
        if (IsKeyPressed(KEY_TAB)) { S.outView = (S.outView + 1) % 3; entering = true; S.follow = -1; }
        if (entering && S.outView == 0) S.cam.position = {0, ar.ceil - 0.7f, ar.OuterZ() - 0.4f};
        Vector3 want{};
        if (S.outView == 2) {   // over a living teammate's shoulder, looking where they look (any living player if your side is all out)
            const auto& ps = W().players; if (S.follow < 0 || S.follow >= (int)ps.size() || !ps[S.follow].alive) { S.follow = -1; for (int pass = 0; pass < 2 && S.follow < 0; pass++) for (size_t i = 0; i < ps.size(); i++) if (ps[i].alive && ps[i].present && (pass == 1 || ps[i].team == p.team)) { S.follow = (int)i; break; } }
            if (S.follow >= 0) { const Player& f = ps[S.follow]; Vector3 fw{cosf(f.yaw), 0, sinf(f.yaw)}; want = Vector3Add(f.Eye(), Vector3Add(Vector3Scale(fw, -2.4f), {0, 0.9f, 0})); S.camYaw = f.yaw; S.camPitch = std::clamp(f.pitch - 0.25f, -1.2f, 1.2f); }
            else S.outView = 0;
        }
        if (S.outView == 0) { want = {0, ar.ceil - 0.7f, ar.OuterZ() - 0.4f}; if (entering) aimAt(want, {0, 0.6f, -0.6f}); }
        if (S.outView == 1) { float s = p.team == 0 ? -1.0f : 1.0f; want = {s * (ar.OuterX() - 0.8f), ar.wall * 0.8f, 0}; if (entering) aimAt(want, {0, 0.8f, 0}); }
        S.cam.position = Vector3Lerp(S.cam.position, want, std::min(1.0f, dt * (S.outView == 2 ? 8.0f : 3.0f)));
    }
    if (S.shake > 0) { S.cam.position.x += sinf(S.t * 70) * 0.03f * S.shake; S.cam.position.y += cosf(S.t * 61) * 0.03f * S.shake; }
    Vector3 look{cosf(S.camPitch) * cosf(S.camYaw), sinf(S.camPitch), cosf(S.camPitch) * sinf(S.camYaw)};
    S.cam.target = Vector3Add(S.cam.position, look); S.cam.up = {0, 1, 0}; S.cam.fovy = !p.alive && S.outView == 0 ? 80.0f : 74.0f; S.cam.projection = CAMERA_PERSPECTIVE;
}
void Render() {
    rt::SceneLight L;
    L.fog = {26, 30, 38, 255}; L.fogDensity = 0.006f; L.fill = {90, 96, 110, 255}; L.rim = {120, 150, 190, 255}; L.key = {255, 240, 220, 255};
    L.surfaceY = 1e5f; L.time = S.t;
    L.moonDir = Vector3Normalize({0.2f, -1, 0.3f}); L.moon = {255, 245, 230, 255}; L.moonK = 0.45f;
    L.ambK = 0.8f; L.skyAmb = {150, 156, 170, 255}; L.seaAmb = {70, 60, 56, 255};
    L.outline = 0.8f; L.outlineTint = {16, 18, 24, 255}; L.stipple = 0.3f; L.grain = 0.3f; L.aoK = 0.4f; L.aoRadius = 0.5f; L.filmic = 0.2f; L.saturation = 1.15f;
    const Arena& a = W().arena;
    for (int i = -1; i <= 1; i++) L.AddPoint({i * a.halfL * 0.66f, a.ceil - 0.6f, 0}, a.ceil * 1.8f, {255, 240, 220, 255}, 0.45f);
    int n = 0; for (const auto& p : W().players) for (int k = 0; k < 2 && n < 4; k++) if (p.portal[k].on && p.portal[0].on && p.portal[1].on) { L.AddPoint(Vector3Add(p.portal[k].c, Vector3Scale(p.portal[k].n, 0.6f)), 3.5f, k ? Color{140, 140, 255, 255} : Color{255, 190, 90, 255}, 0.6f); n++; }
    rt::ApplyGameQuality();
    auto scene = [&](bool views) {
        DrawArena(); DrawDressing(); DrawPortals(views);
        for (const auto& p : W().players) if (p.present) { if (p.alive) DrawPlayer(p, false, 0); else { int k = 0; for (size_t i = 0; i < W().sideline[p.team].size(); i++) if (W().sideline[p.team][i] == p.id) k = (int)i; DrawPlayer(p, true, k); } }
        DrawBalls();
    };
    {   // the views through the two nearest open portals facing you (the playtest: "the portals don't show what is through them")
        Vector3 fwd = Vector3Normalize(Vector3Subtract(S.cam.target, S.cam.position));
        struct Cand { int owner, k; float d; }; std::vector<Cand> cs;
        for (const auto& p : W().players) for (int k = 0; k < 2; k++) {
            const Portal& o = p.portal[k]; if (!o.on || !p.portal[1 - k].on) continue;
            Vector3 to = Vector3Subtract(o.c, S.cam.position); float d = Vector3Length(to);
            if (d > 45 || Vector3DotProduct(Vector3Scale(to, -1), o.n) < 0.05f || Vector3DotProduct(Vector3Scale(to, 1 / std::max(0.01f, d)), fwd) < 0.15f) continue;
            cs.push_back({p.id, k, d});
        }
        std::sort(cs.begin(), cs.end(), [](const Cand& a, const Cand& b) { return a.d < b.d; });
        Vector2 vs = rt::ViewSize(); int tw = std::max(64, (int)vs.x / 2), th = std::max(36, (int)vs.y / 2);   // (half the view's resolution: the face is small)
        for (int i = 0; i < 2; i++) {
            PortalView& v = gPV[i]; v.live = false;
            if (i >= (int)cs.size()) continue;
            if (v.rt.texture.width != tw) { if (v.rt.id) UnloadRenderTexture(v.rt); v.rt = LoadRenderTexture(tw, th); SetTextureFilter(v.rt.texture, TEXTURE_FILTER_BILINEAR); }
            const Player& owner = W().players[cs[i].owner]; const Portal& a = owner.portal[cs[i].k]; const Portal& b = owner.portal[1 - cs[i].k];
            Camera3D vc = PortalCam(S.cam, a, b);
            gClipOn = true; gClipP = b.c; gClipN = b.n;
            float plane = Vector3DotProduct(Vector3Subtract(b.c, vc.position), b.n);   // (how far behind the exit's plane the eye is)
            rlSetClipPlanes(std::max(0.05, (double)plane * 0.9), RL_CULL_DISTANCE_FAR);
            rt::RenderBegin(vc, L); scene(false); rt::RenderCapture(v.rt);
            rlSetClipPlanes(RL_CULL_DISTANCE_NEAR, RL_CULL_DISTANCE_FAR);
            gClipOn = false;
            v.owner = cs[i].owner; v.k = cs[i].k; v.live = true;
        }
    }
    rt::RenderBegin(S.cam, L);
    scene(true); DrawViewmodel();
    rt::RenderEnd();
}
}  // namespace

int gWarpArena = 0, gWarpFill = 4;
void StartWarp(Game& g, int perTeam, int skill, int arena) {
    S = WarpScene{}; S.active = true; S.perTeam = std::clamp(perTeam, 1, 6); S.skill = std::clamp(skill, 0, 2); S.arena = arena;
    W().Init(arena, S.perTeam, (uint32_t)GetRandomValue(1, 1 << 30));
    for (auto& p : W().players) p.name = p.id == 0 ? "You" : BOT_NAMES[p.id % 12];
    S.botRng.resize(W().players.size()); for (size_t i = 0; i < S.botRng.size(); i++) S.botRng[i] = 99991u * (uint32_t)(i + 1) + (uint32_t)GetRandomValue(0, 1 << 20);
    S.camYaw = 0; S.camPitch = -0.05f; S.lastRound = W().round; S.banner = 0;
    g.scene = Scene::Warp;
}
void StartWarpNet(Game& g, arcade::Session* net, const char* name) {
    S = WarpScene{}; S.active = true; S.net = net; S.netName = name ? name : "Diver"; S.help = true;
    S.camYaw = 0; S.camPitch = -0.05f;
    g.scene = Scene::Warp;
}
void LeaveWarp(Game& g) {
    if (S.net) { if (S.net->role == arcade::R_HOST) S.net->BackToLobby(); else S.net->Leave(); S.net = nullptr; }
    S.hostW = nullptr; S.active = false; MouseLook(false); g.scene = Scene::Arcade;
}
// a networked frame: the host steps the real world inside the session; a guest mirrors the snapshots (smoothed)
static bool NetFrame(Game& g, float dt) {
    arcade::Session& N = *S.net;
    N.Update(GetTime(), dt);
    if (N.stage != arcade::S_PLAYING) { S.net = nullptr; S.hostW = nullptr; S.active = false; MouseLook(false); g.scene = Scene::Arcade; return false; }
    int seat = N.MyPlayer();
    if (N.role == arcade::R_HOST) { S.hostW = WarpHostWorld(N.HostGame()); S.me = WarpSeatPlayer(N.HostGame(), seat); }
    else if (N.stateVersion != (int)S.seenVersion && !N.Snapshot().empty()) {
        S.seenVersion = (uint64_t)N.stateVersion;
        std::vector<Vector3> was; for (const auto& p : S.Wm.players) was.push_back(p.pos);
        Reader r(N.Snapshot()); ReadWorld(r, S.Wm, &S.evTotal);
        int per = (int)S.Wm.players.size() / 2; S.me = per > 0 && seat >= 0 && seat / 2 < per ? (seat % 2) * per + seat / 2 : 0;
        if (S.smooth.size() != S.Wm.players.size()) { S.smooth.clear(); for (const auto& p : S.Wm.players) S.smooth.push_back(p.pos); }
    }
    if (W().players.empty() || S.me < 0) { ClearBackground(Color{14, 18, 26, 255}); DrawTextCenteredBold("Into the arena...", SCREEN_W / 2.0f, SCREEN_H / 2.0f - 12, 24, WHITE); return false; }
    if (!S.helloSent) { Writer o; o.U8(1); o.Str(S.netName); N.Act(o); S.helloSent = true; S.camYaw = Me().team == 0 ? 0 : PI; }
    Gather();
    Writer iw; iw.U8(0); WriteInput(Me().in, iw); N.Act(iw);
    if (N.role != arcade::R_HOST) {   // (between snapshots: bodies glide to where the host has them, live balls fly on)
        for (size_t i = 0; i < S.smooth.size() && i < S.Wm.players.size(); i++) {
            Vector3& sp = S.smooth[i]; Vector3 to = S.Wm.players[i].pos;
            if (Vector3Distance(sp, to) > 2.5f) sp = to; else sp = Vector3Lerp(sp, to, std::min(1.0f, dt * 18));
        }
        for (auto& b : S.Wm.balls) if (b.st == BS_LIVE || b.st == BS_DEAD) { b.p = Vector3Add(b.p, Vector3Scale(b.v, dt)); b.p.y = std::max(b.p.y, Cfg().ballR); }
    }
    if (W().round != S.lastRound) { S.lastRound = W().round; S.camYaw = Me().team == 0 ? 0 : PI; S.camPitch = -0.05f; }
    return true;
}
void SceneWarp(Game& g) {
    if (!S.active) { StartWarp(g, 4, 1, AR_CLASSIC); }
    float dt = S.shot ? 1 / 60.0f : std::min(GetFrameTime(), 1 / 20.0f);
    S.t += dt; S.shake = std::max(0.0f, S.shake - dt); S.flash = std::max(0.0f, S.flash - dt * 2); S.banner = std::max(0.0f, S.banner - dt);
    if (S.net) {
        if (!NetFrame(g, dt)) return;
        ReadEvents(); StepCamera(dt);
        // (a guest draws the smoothed positions)
        std::vector<Vector3> real; bool guest = !S.hostW && S.smooth.size() == S.Wm.players.size();
        if (guest) for (size_t i = 0; i < S.Wm.players.size(); i++) { real.push_back(S.Wm.players[i].pos); if ((int)i != S.me) S.Wm.players[i].pos = S.smooth[i]; }
        Render();
        if (guest) for (size_t i = 0; i < real.size(); i++) S.Wm.players[i].pos = real[i];
        DrawHud(g);
        return;
    }
    if (!S.shot) {
        Gather();
        // the fixed 120 Hz step: your input stays held across the sub-steps, presses fire once
        S.acc += dt; Input mine = Me().in; bool first = true;
        while (S.acc >= STEP) {
            for (auto& p : W().players) if (p.id != S.me) BotInput(W(), p.id, p.in, S.botRng[p.id], S.skill);
            Me().in = mine;
            if (!first) { Me().in.jump = Me().in.squat = Me().in.dive = Me().in.portalA = Me().in.portalB = Me().in.catchP = false; }
            W().Step(); S.acc -= STEP; first = false;
        }
        if (W().round != S.lastRound) { S.lastRound = W().round; S.camYaw = Me().team == 0 ? 0 : PI; S.camPitch = -0.05f; }
    }
    ReadEvents();
    StepCamera(dt);
    Render();
    DrawHud(g);
}
void WarpMenuTick(float) {}
bool WarpActive() { return S.active; }
// --shots: 0 the rush (classic), 1 mid-round with portals open, 2 the Extreme, 3 the catch prompt, 4 out on the bench, 5 the match won
void DebugWarpShot(Game& g, int which) {
    StartWarp(g, 4, 2, which == 2 ? AR_EXTREME : AR_CLASSIC); S.shot = true; S.help = which == 0;
    World& w = W(); uint32_t r = 5;
    auto run = [&](float secs) { for (int i = 0; i < (int)(secs / STEP) && w.phase != PH_MATCH_END; i++) { for (auto& p : w.players) BotInput(w, p.id, p.in, r, 2); w.Step(); } };
    if (which == 0) { run(2.6f); S.camYaw = 0; S.camPitch = -0.15f; }
    if (which == 1 || which == 2) {
        run(9); if (!Me().alive) { auto& sl = w.sideline[0]; sl.erase(std::remove(sl.begin(), sl.end(), S.me), sl.end()); Me().alive = true; } Me().pos = {-6, 0, -1};
        Player& q = w.players[5 % w.players.size()];   // (an enemy's portal pair, open, for the look)
        q.portal[0] = {true, {w.arena.OuterX() - 0.01f, 3.2f, 1.5f}, {-1, 0, 0}, {0, 1, 0}, 0}; q.portal[1] = {true, {-2.5f, w.arena.wall * 0.75f, w.arena.OuterZ() - 0.01f}, {0, 0, -1}, {0, 1, 0}, 0};
        Player& m = Me(); m.portal[0] = {true, {-4, w.arena.wall * 0.8f, -w.arena.OuterZ() + 0.01f}, {0, 0, 1}, {0, 1, 0}, 0};
        S.camYaw = 0.25f; S.camPitch = 0.05f;
        if (Me().ball < 0) for (size_t i = 0; i < w.balls.size(); i++) if (w.balls[i].st != BS_HELD) { w.balls[i].st = BS_HELD; w.balls[i].holder = S.me; Me().ball = (int)i; w.balls[i].mustCarry = false; break; }
        Me().charging = true; Me().charge = 0.7f;
    }
    if (which == 3) {
        run(3); Player& m = Me(); m.alive = true; m.ball = -1; m.pos = {-5, 0, 0}; m.yaw = 0; S.camYaw = 0; S.camPitch = 0;
        Ball b; b.p = {-1.2f, 1.3f, 0.1f}; b.v = {-14, 0, 0}; b.st = BS_LIVE; b.team = 1; b.thrower = w.players.size() / 2; w.balls.push_back(b);
    }
    if (which == 4) { run(4); w.Out(Me(), (int)w.players.size() / 2, "hit"); Me().outCause = "hit"; S.camYaw = 0; S.camPitch = -0.35f; for (int i = 0; i < 300; i++) StepCamera(1 / 60.0f); }
    if (which == 5) { w.wins[0] = 3; w.wins[1] = 1; w.champion = 0; w.phase = PH_MATCH_END; S.camPitch = 0; }
    if (which == 6) {   // a window: your pair on the two end walls; looking into the far one shows the court from behind the near one (you in it)
        run(2); Player& m = Me(); m.alive = true; m.pos = {4, 0, 0.5f}; m.yaw = 0;
        m.portal[0] = {true, {w.arena.OuterX() - 0.01f, 2.0f, 0}, {-1, 0, 0}, {0, 1, 0}, 0}; m.portal[1] = {true, {-w.arena.OuterX() + 0.01f, 2.0f, 0}, {1, 0, 0}, {0, 1, 0}, 0};
        S.camYaw = 0; S.camPitch = 0.0f;
    }
    ReadEvents();
}
