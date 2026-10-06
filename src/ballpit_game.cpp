// Ball Pit Brawl's scene (Scene::BallPit): first person in the play centre, drawn with Red Tide's inked renderer. The
// rules are ballpit.cpp's; everything you do goes through bp::Input (Gather), the bots write theirs with bp::BotInput.
// The hall is one static mesh built from the arena kit (decks, pipe frames, nets, cargo nets, ladders, slides, tunnels,
// the conveyor, the warehouse); the pits' ball surfaces, the balls, darts, cannons and people are drawn each frame.
#include "game.h"
#include "input.h"
#include "redtide_render.h"
#include "rlgl.h"
#include "figure3d.h"
#include "ballpit.h"
#include "ballpit_net.h"
#include "arcade_session.h"
#include "sound.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace rt { bool DrawVmArms(const Model& m, const Camera3D& cam, const Matrix* handR, const Matrix* handL, bool leftGrip, float girth, float t, const std::vector<Recolor>& rc, Color tint); }

namespace {
using namespace bp;

const Color TEAM[2] = {{238, 104, 52, 255}, {52, 140, 228, 255}};
const Color TEAM_DARK[2] = {{128, 50, 26, 255}, {24, 62, 116, 255}};
const char* TEAM_NAME[2] = {"Orange", "Blue"};
const char* BOT_NAMES[12] = {"Bosun", "Kess", "Marlow", "Pip", "Gully", "Rook", "Tamsin", "Ode", "Fen", "Bramble", "Skip", "Wren"};
const Color BALLC[6] = {{232, 52, 48, 255}, {250, 204, 40, 255}, {44, 128, 232, 255}, {70, 190, 80, 255}, {248, 136, 36, 255}, {170, 90, 210, 255}};

Color Mix(Color a, Color b, float k) { k = std::clamp(k, 0.0f, 1.0f); return {(unsigned char)(a.r + (b.r - a.r) * k), (unsigned char)(a.g + (b.g - a.g) * k), (unsigned char)(a.b + (b.b - a.b) * k), 255}; }
Color Shade(Color c, float k) { return {(unsigned char)std::clamp(c.r * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.g * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.b * k, 0.0f, 255.0f), c.a}; }
Color Pal(int i) {
    static const Color P[20] = {{150, 150, 156, 255}, {240, 120, 54, 255}, {58, 140, 226, 255}, {250, 206, 42, 255}, {228, 58, 48, 255}, {74, 186, 82, 255}, {40, 116, 210, 255}, {150, 92, 204, 255},
        {236, 236, 230, 255}, {168, 120, 72, 255}, {66, 70, 92, 255}, {116, 188, 232, 255}, {214, 52, 50, 255}, {250, 200, 60, 255}, {88, 196, 120, 255}, {48, 50, 58, 255}, {240, 150, 40, 255}, {250, 210, 60, 255}, {98, 196, 88, 255}, {226, 70, 60, 255}};
    return P[std::clamp(i, 0, 19)];
}

struct Pop { std::string text; Color col; float t; };
struct Feed { std::string text; float t; Color c; };
struct Fx { int kind; Vector3 p, v; float t, life; Color c; };   // particles: confetti, dart puffs, splashes
struct BPScene {
    bool active = false, shot = false, help = true;
    World Wm; World* hostW = nullptr; arcade::Session* net = nullptr; uint32_t evTotal = 0; int seenVersion = -1; bool helloSent = false; std::string netName; std::vector<Vector3> smooth;
    int me = 0, mode = MD_TDM, players = 8, skill = 1;
    std::vector<uint32_t> botRng;
    float camYaw = 0, camPitch = 0, t = 0, acc = 0, shake = 0, flash = 0, recoil = 0, kick = 0, bob = 0, swayX = 0, swayY = 0, fov = 78, hurtYaw = 0, hurtT = 0, hitMark = 0;
    size_t evSeen = 0; uint32_t evCountSeen = 0; std::deque<Feed> feed; std::vector<Pop> pops; std::vector<Fx> fx;
    bool storeOpen = false, scoreboard = false, loadout = false; int jokeSel = 0; std::string dadLine; float dadT = 0;
    float bannerT = 0; std::string banner; Color bannerCol = WHITE; int lastRound = 0;
    Camera3D cam{};
    bool overSaved = false, freeCam = false;
} S;

World& W() { return S.hostW ? *S.hostW : S.Wm; }
Player& Me() { return W().players[std::clamp(S.me, 0, std::max(0, (int)W().players.size() - 1))]; }
std::string NameOf(int id) { return id >= 0 && id < (int)W().players.size() ? W().players[id].name : std::string("the hall"); }
Color TeamCol(const Player& p) { if (W().mode == MD_FFA) { static const Color F[12] = {{238, 104, 52, 255}, {52, 140, 228, 255}, {80, 190, 80, 255}, {230, 70, 160, 255}, {250, 200, 40, 255}, {150, 90, 210, 255}, {40, 190, 190, 255}, {200, 60, 50, 255}, {120, 120, 240, 255}, {250, 140, 90, 255}, {100, 160, 60, 255}, {240, 240, 240, 255}}; return F[p.id % 12]; } return TEAM[p.team & 1]; }
bool Friend(const Player& p) { return W().mode != MD_FFA && p.team == Me().team; }

// ---------------------------------------------------------------- mesh helpers (two-sided where you can see both faces)
void TwoTri(rt::MeshBuilder& mb, Vector3 a, Vector3 b, Vector3 c, Color col) { mb.Tri(a, b, c, col); mb.Tri(a, c, b, col); }
void BoxC(rt::MeshBuilder& mb, Vector3 lo, Vector3 hi, Color top, Color side, Color bottom) {
    Vector3 v[8]; for (int i = 0; i < 8; i++) v[i] = {(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z};
    mb.Quad(v[2], v[6], v[7], v[3], top); mb.Quad(v[0], v[1], v[5], v[4], bottom);
    mb.Quad(v[0], v[4], v[6], v[2], side); mb.Quad(v[1], v[3], v[7], v[5], side);
    mb.Quad(v[0], v[2], v[3], v[1], Shade(side, 0.92f)); mb.Quad(v[4], v[5], v[7], v[6], Shade(side, 0.92f));
}
void Cyl(rt::MeshBuilder& mb, Vector3 a, Vector3 b, float r, Color c, int segs = 10) { mb.Tube({a, b}, r, r, segs, c, c, 0); Vector3 d = Vector3Normalize(Vector3Subtract(b, a)); mb.Cone(a, Vector3Subtract(a, Vector3Scale(d, 0.001f)), r, segs, c); mb.Cone(b, Vector3Add(b, Vector3Scale(d, 0.001f)), r, segs, c); }
void Sphere(rt::MeshBuilder& mb, Vector3 c, float r, Color col, int seg = 8, int rings = 6) {
    for (int i = 0; i < rings; i++) for (int j = 0; j < seg; j++) {
        float a0 = PI * i / rings - PI / 2, a1 = PI * (i + 1) / rings - PI / 2, b0 = 2 * PI * j / seg, b1 = 2 * PI * (j + 1) / seg;
        auto P = [&](float a, float b) { return Vector3{c.x + r * cosf(a) * cosf(b), c.y + r * sinf(a), c.z + r * cosf(a) * sinf(b)}; };
        Color k = Shade(col, 0.82f + 0.3f * (sinf(a0) * 0.5f + 0.5f));
        mb.Tri(P(a0, b0), P(a1, b1), P(a1, b0), k); mb.Tri(P(a0, b0), P(a0, b1), P(a1, b1), k);
    }
}
Model Upload(rt::MeshBuilder& mb) { return LoadModelFromMesh(mb.Build()); }

// ---------------------------------------------------------------- the hall (one static mesh)
Color PipeCol(float x, float z) { static const Color P[4] = {{250, 206, 42, 255}, {86, 196, 76, 255}, {44, 128, 228, 255}, {228, 58, 48, 255}}; int k = ((int)floorf(fabsf(x) / 4) + (int)floorf(fabsf(z) / 5)) & 3; return P[k]; }
void BuildNet(rt::MeshBuilder& mb, Vector3 lo, Vector3 hi, Color c, float step) {
    // a net in the plane of its thinnest axis: thin strings both ways, and a pipe round its edge
    Vector3 s = Vector3Subtract(hi, lo); int ax = s.x <= s.y && s.x <= s.z ? 0 : s.y <= s.z ? 1 : 2;
    Vector3 m = Vector3Lerp(lo, hi, 0.5f); float th = 0.012f;
    auto P = [&](float u, float v) { if (ax == 0) return Vector3{m.x, lo.y + v, lo.z + u}; if (ax == 1) return Vector3{lo.x + u, m.y, lo.z + v}; return Vector3{lo.x + u, lo.y + v, m.z}; };
    float U = ax == 0 ? s.z : s.x, V = ax == 1 ? s.z : s.y;
    for (float u = 0; u <= U + 1e-3f; u += step) { Vector3 a = P(u, 0), b = P(u, V); Vector3 h{ax == 0 ? th : th, ax == 1 ? th : V / 2, ax == 2 ? th : th}; (void)h; BoxC(mb, Vector3Subtract(Vector3Lerp(a, b, 0.5f), {ax == 0 ? th : th, ax == 1 ? th : V / 2, ax == 2 ? th : th}), Vector3Add(Vector3Lerp(a, b, 0.5f), {ax == 0 ? th : th, ax == 1 ? th : V / 2, ax == 2 ? th : th}), c, c, c); }
    for (float v = 0; v <= V + 1e-3f; v += step) { Vector3 a = P(0, v), b = P(U, v); Vector3 mid = Vector3Lerp(a, b, 0.5f); Vector3 h{ax == 0 ? th : U / 2, ax == 1 ? th : th, ax == 2 ? th : th}; if (ax == 1) h = {U / 2, th, th}; if (ax == 0) h = {th, th, U / 2}; BoxC(mb, Vector3Subtract(mid, h), Vector3Add(mid, h), c, c, c); }
}
void BuildRail(rt::MeshBuilder& mb, const Solid& s) {
    Vector3 d = Vector3Subtract(s.hi, s.lo); bool alongX = d.x > d.z; float L = alongX ? d.x : d.z; Color c = PipeCol(s.lo.x, s.lo.z);
    float mx = (s.lo.x + s.hi.x) / 2, mz = (s.lo.z + s.hi.z) / 2;
    for (float y : {s.lo.y + 0.5f, s.hi.y}) { Vector3 a = alongX ? Vector3{s.lo.x, y, mz} : Vector3{mx, y, s.lo.z}, b = alongX ? Vector3{s.hi.x, y, mz} : Vector3{mx, y, s.hi.z}; Cyl(mb, a, b, 0.045f, c, 8); }
    int n = std::max(1, (int)(L / 1.2f)); for (int i = 0; i <= n; i++) { float u = (float)i / n; Vector3 p = alongX ? Vector3{s.lo.x + d.x * u, s.lo.y, mz} : Vector3{mx, s.lo.y, s.lo.z + d.z * u}; Cyl(mb, p, {p.x, s.hi.y, p.z}, 0.04f, c, 8); }
}
void BuildDeck(rt::MeshBuilder& mb, const Solid& s) {
    Color base = Pal(s.colour);
    // the padded mats on top: 1.5 m tiles in two shades with a soft seam between them
    float y = s.hi.y + 0.002f;
    for (float x = s.lo.x; x < s.hi.x - 0.01f; x += 1.5f) for (float z = s.lo.z; z < s.hi.z - 0.01f; z += 1.5f) {
        float x1 = std::min(s.hi.x, x + 1.5f), z1 = std::min(s.hi.z, z + 1.5f); int k = ((int)floorf(x / 1.5f + 100) + (int)floorf(z / 1.5f + 100)) & 1;
        Color c = k ? base : Shade(base, 0.9f);
        mb.Quad({x + 0.03f, y, z + 0.03f}, {x + 0.03f, y, z1 - 0.03f}, {x1 - 0.03f, y, z1 - 0.03f}, {x1 - 0.03f, y, z + 0.03f}, c);
        mb.Quad({x, y - 0.001f, z}, {x, y - 0.001f, z1}, {x1, y - 0.001f, z1}, {x1, y - 0.001f, z}, Shade(base, 0.62f));
    }
    BoxC(mb, s.lo, {s.hi.x, s.hi.y - 0.004f, s.hi.z}, Shade(base, 0.7f), Shade(base, 0.78f), Mix(Shade(base, 0.55f), Color{150, 154, 170, 255}, 0.5f));
    // the coloured pipe along every edge (the frame the deck sits in)
    float ey = s.hi.y - 0.12f; Color pc = PipeCol(s.lo.x + 1, s.lo.z + 2);
    if (s.hi.x - s.lo.x > 2.5f && s.hi.z - s.lo.z > 1.5f) {
        Cyl(mb, {s.lo.x, ey, s.lo.z}, {s.hi.x, ey, s.lo.z}, 0.07f, pc); Cyl(mb, {s.lo.x, ey, s.hi.z}, {s.hi.x, ey, s.hi.z}, 0.07f, pc);
        Cyl(mb, {s.lo.x, ey, s.lo.z}, {s.lo.x, ey, s.hi.z}, 0.07f, pc); Cyl(mb, {s.hi.x, ey, s.lo.z}, {s.hi.x, ey, s.hi.z}, 0.07f, pc);
    }
}
void BuildPad(rt::MeshBuilder& mb, const Solid& s) {
    Color c = Pal(s.colour); float r = 0.06f;
    BoxC(mb, {s.lo.x + r, s.lo.y, s.lo.z + r}, {s.hi.x - r, s.hi.y, s.hi.z - r}, Mix(c, WHITE, 0.15f), c, Shade(c, 0.6f));
    BoxC(mb, {s.lo.x, s.lo.y + r, s.lo.z + r}, {s.hi.x, s.hi.y - r, s.hi.z - r}, c, Shade(c, 0.95f), c);
    BoxC(mb, {s.lo.x + r, s.lo.y + r, s.lo.z}, {s.hi.x - r, s.hi.y - r, s.hi.z}, c, Shade(c, 0.9f), c);
    // (a punching bag is a cylinder: drawn round)
    if (s.colour == 12) { Vector3 m{(s.lo.x + s.hi.x) / 2, 0, (s.lo.z + s.hi.z) / 2}; Cyl(mb, {m.x, s.hi.y, m.z}, {m.x, s.hi.y + 0.9f, m.z}, 0.02f, {60, 60, 60, 255}, 6); }
}
void BuildClimb(rt::MeshBuilder& mb, const Climb& c) {
    bool xf = fabsf(c.into.x) > 0.5f; float face = xf ? (c.into.x > 0 ? c.hi.x : c.lo.x) : (c.into.z > 0 ? c.hi.z : c.lo.z);
    float a0 = xf ? c.lo.z : c.lo.x, a1 = xf ? c.hi.z : c.hi.x; float y0 = c.lo.y, y1 = c.hi.y;
    auto P = [&](float u, float y, float off) { return xf ? Vector3{face - c.into.x * off, y, u} : Vector3{u, y, face - c.into.z * off}; };
    if (c.net) {   // a cargo net: a rope lattice, slack at the bottom
        Color rope{58, 44, 36, 255};
        int nu = (int)((a1 - a0) / 0.32f), nv = (int)((y1 - y0) / 0.32f);
        for (int i = 0; i <= nu; i++) { std::vector<Vector3> pts; for (int k = 0; k <= nv; k++) { float y = y0 + (y1 - y0) * k / nv; float sag = 0.12f * sinf(PI * k / nv); pts.push_back(P(a0 + (a1 - a0) * i / nu, y, 0.12f + sag)); } mb.Tube(pts, 0.018f, 0.018f, 5, rope, rope, 0); }
        for (int k = 0; k <= nv; k++) { float y = y0 + (y1 - y0) * k / nv; float sag = 0.12f * sinf(PI * k / nv); mb.Tube({P(a0, y, 0.12f + sag), P(a1, y, 0.12f + sag)}, 0.016f, 0.016f, 5, rope, rope, 0); }
        Cyl(mb, P(a0 - 0.05f, y1, 0.1f), P(a1 + 0.05f, y1, 0.1f), 0.05f, {250, 206, 42, 255});
    } else {   // a ladder: two rails, rungs every 30 cm
        Color rail{250, 206, 42, 255}, rung{230, 230, 226, 255}; float m = (a0 + a1) / 2;
        Cyl(mb, P(m - 0.25f, y0, 0.08f), P(m - 0.25f, y1 + 0.9f, 0.08f), 0.035f, rail); Cyl(mb, P(m + 0.25f, y0, 0.08f), P(m + 0.25f, y1 + 0.9f, 0.08f), 0.035f, rail);
        for (float y = y0 + 0.25f; y < y1; y += 0.3f) Cyl(mb, P(m - 0.25f, y, 0.08f), P(m + 0.25f, y, 0.08f), 0.025f, rung, 6);
    }
}
void BuildSlide(rt::MeshBuilder& mb, const Slide& sl, int idx) {
    // the shell round the feet path (centre 0.5 m up), coloured in sections; a spiral is an open half-pipe
    static const Color SEC[4][3] = {{{44, 128, 228, 255}, {74, 186, 82, 255}, {228, 58, 48, 255}}, {{228, 58, 48, 255}, {250, 206, 42, 255}, {44, 128, 228, 255}}, {{250, 170, 40, 255}, {250, 206, 42, 255}, {250, 170, 40, 255}}, {{74, 186, 82, 255}, {44, 128, 228, 255}, {250, 206, 42, 255}}};
    std::vector<Vector3> c; for (const auto& p : sl.pts) c.push_back({p.x, p.y + 0.5f, p.z});
    // resample every ~0.4 m for a smooth tube
    std::vector<Vector3> s; for (size_t i = 1; i < c.size(); i++) { float L = Vector3Distance(c[i - 1], c[i]); int n = std::max(1, (int)(L / 0.4f)); for (int k = 0; k < n; k++) s.push_back(Vector3Lerp(c[i - 1], c[i], (float)k / n)); } s.push_back(c.back());
    const int SEG = 14; float R = 0.58f; int scheme = sl.spiral ? 2 + (idx & 1) * 0 : (idx & 1);
    std::vector<Vector3> prev; std::vector<Vector3> prevN;
    for (size_t i = 0; i < s.size(); i++) {
        Vector3 d = i + 1 < s.size() ? Vector3Subtract(s[i + 1], s[i]) : Vector3Subtract(s[i], s[i - 1]); d = Vector3Normalize(d);
        Vector3 side = Vector3Normalize(Vector3CrossProduct(d, {0, 1, 0})); if (Vector3Length(side) < 0.1f) side = {1, 0, 0}; Vector3 up = Vector3CrossProduct(side, d);
        std::vector<Vector3> ring;
        for (int k = 0; k <= SEG; k++) { float a = sl.spiral ? PI + PI * k / SEG : 2 * PI * k / SEG; ring.push_back(Vector3Add(s[i], Vector3Add(Vector3Scale(side, cosf(a) * R), Vector3Scale(up, sinf(a) * R)))); }
        if (i > 0) {
            Color col = SEC[scheme][(i / 6) % 3];
            for (int k = 0; k < SEG; k++) {
                Color ck = (!sl.spiral && (k == 3 || k == 4) && (i % 6 == 3)) ? Mix(col, Color{200, 230, 255, 255}, 0.6f) : Shade(col, 0.9f + 0.1f * sinf((float)k));
                TwoTri(mb, prev[k], ring[k], ring[k + 1], ck); TwoTri(mb, prev[k], ring[k + 1], prev[k + 1], ck);
            }
            if (i % 6 == 0) for (int k = 0; k < SEG; k++) Cyl(mb, ring[k], ring[k + 1], 0.03f, Shade(col, 0.7f), 5);   // (the seams between sections)
        }
        prev = ring;
    }
    if (sl.spiral) for (size_t i = 0; i + 1 < s.size(); i += 1) { Vector3 side = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(s[i + 1], s[i]), {0, 1, 0})); for (int k = -1; k <= 1; k += 2) Cyl(mb, Vector3Add(s[i], Vector3Scale(side, k * R)), Vector3Add(s[i + 1], Vector3Scale(side, k * R)), 0.05f, {250, 206, 42, 255}, 6); }
    // a centre pole for the spiral
    if (sl.spiral && sl.pts.size() > 4) { Vector3 a = sl.pts[sl.pts.size() / 2]; Vector3 ax{0, 0, 0}; for (size_t i = 2; i + 1 < sl.pts.size(); i++) ax = Vector3Add(ax, sl.pts[i]); ax = Vector3Scale(ax, 1.0f / (sl.pts.size() - 3)); (void)a; Cyl(mb, {ax.x, 0, ax.z}, {ax.x, sl.pts[0].y + 1.0f, ax.z}, 0.12f, {250, 206, 42, 255}); }
}
void BuildHall(rt::MeshBuilder& mb, const Arena& a) {
    // the warehouse floor: speckled rubber tiles, 2 m, with painted lanes
    for (int x = -30; x < 30; x += 2) for (int z = -15; z < 15; z += 2) { int k = ((x + z) / 2) & 1; Color c = k ? Color{86, 96, 118, 255} : Color{80, 90, 110, 255}; mb.Quad({(float)x, 0.001f, (float)z}, {(float)x, 0.001f, z + 2.0f}, {x + 2.0f, 0.001f, z + 2.0f}, {x + 2.0f, 0.001f, (float)z}, c); }
    for (float z : {-10.0f, 10.0f}) mb.Quad({-24, 0.004f, z - 0.06f}, {-24, 0.004f, z + 0.06f}, {24, 0.004f, z + 0.06f}, {24, 0.004f, z - 0.06f}, {230, 200, 70, 255});
    // the walls: pale with a band of colour, high windows of daylight (glow drawn per frame), the ceiling dark
    auto wall = [&](Vector3 a0, Vector3 a1, float h) {
        Vector3 d = Vector3Subtract(a1, a0);
        mb.Quad(a0, {a0.x, h, a0.z}, {a1.x, h, a1.z}, a1, {196, 204, 214, 255});
        mb.Quad({a0.x, 0, a0.z}, {a0.x, 1.2f, a0.z}, {a1.x, 1.2f, a1.z}, {a1.x, 0, a1.z}, {60, 90, 160, 255});
        for (int i = 0; i < 3; i++) { float y0 = 1.25f + i * 0.18f; Color c = i == 0 ? Color{230, 70, 60, 255} : i == 1 ? Color{250, 200, 50, 255} : Color{80, 180, 90, 255}; mb.Quad({a0.x, y0, a0.z}, {a0.x, y0 + 0.14f, a0.z}, {a1.x, y0 + 0.14f, a1.z}, {a1.x, y0, a1.z}, c); }
        (void)d;
    };
    float X = a.halfX - 0.01f, Z = a.halfZ - 0.01f, H = a.ceil;
    wall({-X, 0, Z}, {-X, 0, -Z}, H); wall({X, 0, -Z}, {X, 0, Z}, H); wall({-X, 0, -Z}, {X, 0, -Z}, H); wall({X, 0, Z}, {-X, 0, Z}, H);
    mb.Quad({-X, H, -Z}, {X, H, -Z}, {X, H, Z}, {-X, H, Z}, {40, 44, 60, 255});
    // the roof trusses and the ducts (the overhead pipes from the reference)
    for (float x = -27; x <= 27; x += 6) { BoxC(mb, {x - 0.12f, H - 0.9f, -Z}, {x + 0.12f, H - 0.7f, Z}, {70, 74, 90, 255}, {70, 74, 90, 255}, {60, 64, 80, 255}); for (float z = -Z; z < Z; z += 1.5f) Cyl(mb, {x, H - 0.7f, z}, {x, H - 0.02f, z + 0.75f}, 0.03f, {80, 84, 100, 255}, 5); }
    for (float z : {-9.0f, 9.0f}) Cyl(mb, {-X, H - 1.4f, z}, {X, H - 1.4f, z}, 0.35f, {170, 176, 186, 255}, 12);
    for (float z : {-4.0f, 4.0f}) Cyl(mb, {-X, H - 1.0f, z}, {X, H - 1.0f, z}, 0.18f, {200, 120, 60, 255}, 10);
    // the lift pipes: from each hub up the wall, along the ceiling, and down to every cannon's hopper (clear: hoops)
    for (int h = 0; h < 2; h++) {
        Vector3 hub = a.hub[h]; float zs = hub.z > 0 ? 1.0f : -1.0f;
        std::vector<Vector3> trunk = {{hub.x, 2.2f, hub.z}, {hub.x, H - 2.0f, hub.z}, {hub.x, H - 2.0f, zs * 6.5f}};
        for (size_t i = 1; i < trunk.size(); i++) { float L = Vector3Distance(trunk[i - 1], trunk[i]); for (float u = 0; u < L; u += 0.35f) { Vector3 p = Vector3Lerp(trunk[i - 1], trunk[i], u / L); Vector3 d = Vector3Normalize(Vector3Subtract(trunk[i], trunk[i - 1])); Vector3 s1 = fabsf(d.y) > 0.9f ? Vector3{1, 0, 0} : Vector3{0, 1, 0}; Vector3 s2 = Vector3CrossProduct(d, s1); std::vector<Vector3> hoop; for (int k = 0; k <= 10; k++) { float an = 2 * PI * k / 10; hoop.push_back(Vector3Add(p, Vector3Add(Vector3Scale(s1, cosf(an) * 0.22f), Vector3Scale(s2, sinf(an) * 0.22f)))); } mb.Tube(hoop, 0.012f, 0.012f, 4, {190, 220, 240, 255}, {190, 220, 240, 255}, 0); } }
    }
    for (const auto& c : a.cannons) {   // (a drop pipe from the ceiling main to each hopper)
        Vector3 top{c.pivot.x, H - 2.0f, c.pivot.z}, bot{c.pivot.x - cosf(c.yaw0) * 0.3f, c.pivot.y + 0.7f, c.pivot.z - sinf(c.yaw0) * 0.3f};
        float L = Vector3Distance(top, bot); for (float u = 0; u < L; u += 0.35f) { Vector3 p = Vector3Lerp(top, bot, u / L); std::vector<Vector3> hoop; for (int k = 0; k <= 10; k++) { float an = 2 * PI * k / 10; hoop.push_back({p.x + cosf(an) * 0.18f, p.y, p.z + sinf(an) * 0.18f}); } mb.Tube(hoop, 0.012f, 0.012f, 4, {190, 220, 240, 255}, {190, 220, 240, 255}, 0); }
        Cyl(mb, {c.pivot.x, H - 2.0f, -6.5f}, {c.pivot.x, H - 2.0f, 6.5f}, 0.05f, {170, 200, 220, 255}, 6);
    }
    // the conveyor: belts along the walls (dark, ribbed), gutters under every deck's edge
    for (float zs : {-1.0f, 1.0f}) for (float x = -28; x < 28; x += 0.5f) mb.Quad({x, 0.452f, zs * 13 - 0.55f}, {x, 0.452f, zs * 13 + 0.55f}, {x + 0.42f, 0.452f, zs * 13 + 0.55f}, {x + 0.42f, 0.452f, zs * 13 - 0.55f}, {34, 36, 42, 255});
}
Model& HallModel() {
    static Model m{}; static bool made = false; if (made) return m;
    const Arena& a = W().arena; rt::MeshBuilder mb;
    BuildHall(mb, a);
    for (const auto& s : a.solids) {
        switch (s.mat) {
            case M_DECK: BuildDeck(mb, s); break;
            case M_PAD: BuildPad(mb, s); break;
            case M_PANEL: { Color c = Pal(s.colour == 0 ? 8 : s.colour); BoxC(mb, s.lo, s.hi, Mix(c, WHITE, 0.1f), c, Shade(c, 0.6f)); break; }
            case M_FRAME: { Vector3 m2{(s.lo.x + s.hi.x) / 2, 0, (s.lo.z + s.hi.z) / 2}; Cyl(mb, {m2.x, s.lo.y, m2.z}, {m2.x, s.hi.y, m2.z}, 0.085f, PipeCol(m2.x + 2, m2.z), 10); Sphere(mb, {m2.x, s.hi.y, m2.z}, 0.11f, PipeCol(m2.x + 2, m2.z), 8, 4); break; }
            case M_RAIL: BuildRail(mb, s); break;
            case M_NET: BuildNet(mb, s.lo, s.hi, {26, 34, 62, 255}, 0.22f); break;
            default: break;
        }
    }
    for (const auto& c : a.climbs) BuildClimb(mb, c);
    for (size_t i = 0; i < a.slides.size(); i++) BuildSlide(mb, a.slides[i], (int)i);
    // the tunnels' portholes and the pits' rims are the pads already; the stores get an awning and shelves
    for (const auto& st : a.stores) {
        float s = st.p.x > 0 ? 1.0f : -1.0f; Vector3 b{st.p.x, st.p.y, -13.6f};
        for (int k = 0; k < 6; k++) BoxC(mb, {b.x - 0.7f + k * 0.24f, b.y + 2.0f, -14.0f}, {b.x - 0.48f + k * 0.24f, b.y + 2.1f, -13.0f}, k % 2 ? Color{240, 60, 50, 255} : Color{250, 250, 240, 255}, k % 2 ? Color{220, 50, 40, 255} : Color{230, 230, 220, 255}, {200, 200, 200, 255});
        BoxC(mb, {b.x - 0.75f, b.y, -14.0f}, {b.x - 0.68f, b.y + 2.1f, -13.95f}, {120, 120, 130, 255}, {120, 120, 130, 255}, {100, 100, 110, 255}); BoxC(mb, {b.x + 0.68f, b.y, -14.0f}, {b.x + 0.75f, b.y + 2.1f, -13.95f}, {120, 120, 130, 255}, {120, 120, 130, 255}, {100, 100, 110, 255});
        for (int sh = 0; sh < 2; sh++) BoxC(mb, {b.x - 0.66f, b.y + 1.3f + sh * 0.35f, -14.0f}, {b.x + 0.66f, b.y + 1.33f + sh * 0.35f, -13.75f}, {210, 210, 200, 255}, {190, 190, 180, 255}, {170, 170, 160, 255});
        (void)s;
    }
    for (const auto& f : a.flags) { Cyl(mb, {f.p.x, f.p.y, f.p.z}, {f.p.x, f.p.y + 0.18f, f.p.z}, 0.5f, {230, 230, 226, 255}, 16); }
    for (const auto& bs : a.bombSites) { for (int k = 0; k < 4; k++) { float an = k * PI / 2; Vector3 c{bs.p.x + cosf(an) * 1.2f, bs.p.y + 0.003f, bs.p.z + sinf(an) * 1.2f}; mb.Quad({c.x - 0.25f, c.y, c.z - 0.25f}, {c.x - 0.25f, c.y, c.z + 0.25f}, {c.x + 0.25f, c.y, c.z + 0.25f}, {c.x + 0.25f, c.y, c.z - 0.25f}, {250, 200, 40, 255}); } }
    m = Upload(mb); made = true; return m;
}

// ---------------------------------------------------------------- props (each a small mesh, drawn by transform)
Model& SphereM() { static Model m = [] { rt::MeshBuilder mb; Sphere(mb, {0, 0, 0}, 1, WHITE, 12, 8); return Upload(mb); }(); return m; }
Model& DartM(bool mega) {
    static Model m[2]; static bool made[2] = {};
    int k = mega ? 1 : 0; if (made[k]) return m[k];
    rt::MeshBuilder mb; float L = mega ? 0.34f : 0.072f, r = mega ? 0.045f : 0.0065f;
    Cyl(mb, {-L / 2, 0, 0}, {L / 2 - r * 1.5f, 0, 0}, r, mega ? Color{250, 120, 30, 255} : Color{248, 126, 36, 255}, 8);
    Cyl(mb, {L / 2 - r * 1.5f, 0, 0}, {L / 2, 0, 0}, r * 1.05f, mega ? Color{40, 110, 220, 255} : Color{40, 110, 220, 255}, 8);
    m[k] = Upload(mb); made[k] = true; return m[k];
}
Model& RocketM() { static Model m = [] { rt::MeshBuilder mb; Cyl(mb, {-0.18f, 0, 0}, {0.12f, 0, 0}, 0.045f, {70, 190, 80, 255}); mb.Cone({0.12f, 0, 0}, {0.24f, 0, 0}, 0.05f, 10, {250, 206, 42, 255}); for (int k = 0; k < 4; k++) { float a = k * PI / 2; mb.Tri({-0.18f, 0, 0}, {-0.06f, 0, 0}, {-0.2f, cosf(a) * 0.1f, sinf(a) * 0.1f}, {228, 58, 48, 255}); mb.Tri({-0.18f, 0, 0}, {-0.2f, cosf(a) * 0.1f, sinf(a) * 0.1f}, {-0.06f, 0, 0}, {228, 58, 48, 255}); } return Upload(mb); }(); return m; }

// The foam blasters: each built in code in bright toy plastics (x forward, y up, z right; the grip at the origin)
struct GunVis { Vector3 gripR{0, 0, 0}, gripL{0.18f, 0.02f, 0}, muzzle{0.3f, 0.08f, 0}; bool twoHand = true; };
GunVis GunInfo(int g) {
    GunVis v;
    switch (g) {
        case G_STARTER: v.gripL = {0.04f, -0.06f, 0}; v.twoHand = false; v.muzzle = {0.26f, 0.07f, 0}; break;
        case G_PISTOL: v.gripL = {0.0f, -0.03f, 0}; v.twoHand = false; v.muzzle = {0.24f, 0.07f, 0}; break;
        case G_REVOLVER: v.gripL = {0.0f, -0.03f, 0}; v.twoHand = false; v.muzzle = {0.3f, 0.08f, 0}; break;
        case G_DUAL: v.twoHand = false; v.muzzle = {0.22f, 0.06f, 0}; break;
        case G_PUMP: v.gripL = {0.26f, 0.0f, 0}; v.muzzle = {0.5f, 0.07f, 0}; break;
        case G_BURST: v.gripL = {0.24f, -0.02f, 0}; v.muzzle = {0.46f, 0.08f, 0}; break;
        case G_FLYWHEEL: v.gripL = {0.2f, -0.03f, 0}; v.muzzle = {0.44f, 0.09f, 0}; break;
        case G_CROSSBOW: v.gripL = {0.22f, -0.01f, 0}; v.muzzle = {0.6f, 0.1f, 0}; break;
        case G_BELT: v.gripL = {0.3f, 0.02f, 0}; v.muzzle = {0.62f, 0.08f, 0}; break;
        case G_LONG: v.gripL = {0.3f, -0.01f, 0}; v.muzzle = {0.86f, 0.08f, 0}; break;
        case G_ROCKET: v.gripL = {0.2f, -0.02f, 0}; v.muzzle = {0.5f, 0.14f, 0}; break;
        default: break;
    }
    return v;
}
void GunMesh(rt::MeshBuilder& mb, int g) {
    const Color OR{248, 128, 36, 255}, YE{250, 210, 46, 255}, BL{40, 112, 222, 255}, GR{92, 96, 108, 255}, DK{50, 52, 60, 255}, WH{236, 236, 230, 255}, GN{72, 190, 84, 255}, RD{226, 56, 48, 255}, PU{150, 90, 210, 255};
    auto box = [&](Vector3 c, Vector3 h, Color col) { BoxC(mb, Vector3Subtract(c, h), Vector3Add(c, h), Mix(col, WHITE, 0.12f), col, Shade(col, 0.7f)); };
    auto barrel = [&](Vector3 a, Vector3 b, float r, Color col) { Cyl(mb, a, b, r, col, 12); };
    auto grip = [&](Color col) { box({-0.01f, -0.06f, 0}, {0.025f, 0.065f, 0.02f}, col); box({0.02f, -0.025f, 0}, {0.012f, 0.02f, 0.006f}, DK); };   // (the handle and a trigger)
    switch (g) {
        case G_STARTER: grip(GR); box({0.08f, 0.04f, 0}, {0.12f, 0.035f, 0.025f}, OR); barrel({0.18f, 0.06f, 0}, {0.26f, 0.06f, 0}, 0.018f, YE); barrel({-0.1f, 0.05f, 0}, {-0.04f, 0.05f, 0}, 0.012f, BL); Sphere(mb, {-0.105f, 0.05f, 0}, 0.02f, BL, 8, 5); break;
        case G_PISTOL: grip(DK); box({0.07f, 0.045f, 0}, {0.13f, 0.03f, 0.022f}, BL); box({0.08f, 0.08f, 0}, {0.11f, 0.012f, 0.018f}, OR); box({-0.01f, -0.1f, 0}, {0.02f, 0.02f, 0.016f}, YE); barrel({0.18f, 0.05f, 0}, {0.23f, 0.05f, 0}, 0.014f, OR); break;
        case G_REVOLVER: grip({120, 70, 40, 255}); box({0.06f, 0.045f, 0}, {0.08f, 0.03f, 0.022f}, GR); barrel({0.12f, 0.07f, 0}, {0.3f, 0.07f, 0}, 0.018f, OR);
            for (int k = 0; k < 6; k++) { float a = k * PI / 3; barrel({0.05f, 0.06f + cosf(a) * 0.028f, sinf(a) * 0.028f}, {0.11f, 0.06f + cosf(a) * 0.028f, sinf(a) * 0.028f}, 0.012f, YE); } break;
        case G_DUAL: grip(DK); box({0.06f, 0.035f, 0}, {0.09f, 0.03f, 0.02f}, YE); barrel({0.13f, 0.05f, 0}, {0.21f, 0.05f, 0}, 0.014f, PU); box({0.04f, 0.075f, 0}, {0.04f, 0.012f, 0.014f}, PU); break;
        case G_PUMP: grip(DK); box({0.13f, 0.05f, 0}, {0.2f, 0.04f, 0.03f}, GN); barrel({0.25f, 0.07f, 0}, {0.5f, 0.07f, 0}, 0.03f, OR); box({0.27f, 0.015f, 0}, {0.07f, 0.025f, 0.034f}, YE); box({-0.12f, 0.03f, 0}, {0.06f, 0.035f, 0.022f}, GN); break;
        case G_BURST: grip(GR); box({0.14f, 0.05f, 0}, {0.2f, 0.038f, 0.026f}, WH); box({0.14f, 0.1f, 0}, {0.12f, 0.012f, 0.02f}, OR); box({0.06f, -0.06f, 0}, {0.03f, 0.06f, 0.02f}, OR); barrel({0.32f, 0.06f, 0}, {0.46f, 0.06f, 0}, 0.016f, BL); box({0.17f, 0.13f, 0}, {0.02f, 0.02f, 0.006f}, DK); box({-0.11f, 0.03f, 0}, {0.06f, 0.035f, 0.02f}, WH); break;
        case G_FLYWHEEL: grip(DK); box({0.13f, 0.05f, 0}, {0.18f, 0.045f, 0.032f}, BL); barrel({0.3f, 0.06f, 0}, {0.44f, 0.06f, 0}, 0.035f, YE); for (int k = 0; k < 8; k++) { float a = k * PI / 4; barrel({0.31f, 0.06f + cosf(a) * 0.04f, sinf(a) * 0.04f}, {0.43f, 0.06f + cosf(a) * 0.04f, sinf(a) * 0.04f}, 0.006f, DK); } Cyl(mb, {0.1f, -0.06f, -0.05f}, {0.1f, -0.06f, 0.05f}, 0.06f, {180, 220, 240, 255}, 14); break;
        case G_CROSSBOW: grip(DK); box({0.2f, 0.05f, 0}, {0.26f, 0.03f, 0.025f}, OR); for (int s = -1; s <= 1; s += 2) mb.Tube({{0.38f, 0.06f, 0}, {0.36f, 0.07f, s * 0.14f}, {0.3f, 0.075f, s * 0.24f}}, 0.016f, 0.01f, 8, BL, BL, 0); mb.Tube({{0.3f, 0.075f, 0.24f}, {0.1f, 0.07f, 0}, {0.3f, 0.075f, -0.24f}}, 0.003f, 0.003f, 4, WH, WH, 0); break;
        case G_BELT: grip(DK); box({0.2f, 0.05f, 0}, {0.26f, 0.07f, 0.06f}, GR); box({0.1f, -0.05f, 0.08f}, {0.08f, 0.07f, 0.04f}, YE); for (int k = 0; k < 6; k++) { float a = k * PI / 3; barrel({0.44f, 0.06f + cosf(a) * 0.035f, sinf(a) * 0.035f}, {0.62f, 0.06f + cosf(a) * 0.035f, sinf(a) * 0.035f}, 0.012f, RD); } box({0.3f, 0.15f, 0}, {0.1f, 0.012f, 0.012f}, DK); break;
        case G_LONG: grip(DK); box({0.18f, 0.05f, 0}, {0.26f, 0.035f, 0.024f}, PU); barrel({0.4f, 0.06f, 0}, {0.86f, 0.06f, 0}, 0.018f, YE); barrel({0.1f, 0.13f, 0}, {0.32f, 0.13f, 0}, 0.026f, DK); box({-0.16f, 0.03f, 0}, {0.08f, 0.04f, 0.02f}, PU); break;
        case G_ROCKET: grip(DK); barrel({-0.18f, 0.12f, 0}, {0.5f, 0.12f, 0}, 0.07f, GN); Cyl(mb, {0.48f, 0.12f, 0}, {0.52f, 0.12f, 0}, 0.08f, YE, 14); box({0.1f, 0.05f, 0}, {0.1f, 0.03f, 0.02f}, YE); box({0.15f, 0.21f, 0.0f}, {0.04f, 0.025f, 0.012f}, RD); break;
        default: break;
    }
}
Model& GunModel(int g) { static std::map<int, Model> c; auto it = c.find(g); if (it != c.end()) return it->second; rt::MeshBuilder mb; if (g == 100) { Cyl(mb, {-0.04f, 0, 0}, {0.06f, 0, 0}, 0.022f, {40, 112, 222, 255}); BoxC(mb, {0.06f, -0.012f, -0.01f}, {0.3f, 0.03f, 0.01f}, {250, 160, 60, 255}, {248, 128, 36, 255}, {200, 100, 30, 255}); }
    else if (g == 101) { BoxC(mb, {-0.06f, -0.05f, -0.05f}, {0.2f, 0.06f, 0.05f}, {90, 200, 90, 255}, {72, 190, 84, 255}, {50, 140, 60, 255}); Cyl(mb, {0.2f, 0.0f, 0}, {0.42f, 0.02f, 0}, 0.04f, {60, 60, 70, 255}); BoxC(mb, {-0.03f, -0.13f, -0.02f}, {0.02f, -0.05f, 0.02f}, {50, 52, 60, 255}, {50, 52, 60, 255}, {50, 52, 60, 255}); }
    else if (g == 102) { BoxC(mb, {-0.03f, -0.05f, -0.06f}, {0.04f, 0.42f, 0.06f}, {60, 80, 220, 255}, {50, 70, 210, 255}, {40, 50, 160, 255}); BoxC(mb, {-0.02f, 0.42f, -0.02f}, {0.03f, 0.62f, 0.03f}, {60, 80, 220, 255}, {50, 70, 210, 255}, {40, 50, 160, 255}); }
    else GunMesh(mb, g);
    c[g] = Upload(mb); return c[g]; }
Model& CannonBase() { static Model m = [] { rt::MeshBuilder mb; Cyl(mb, {0, -1.0f, 0}, {0, -0.35f, 0}, 0.16f, {90, 94, 110, 255}); Cyl(mb, {0, -0.35f, 0}, {0, -0.15f, 0}, 0.42f, {250, 206, 42, 255}, 16); for (int s = -1; s <= 1; s += 2) BoxC(mb, {-0.25f, -0.15f, s * 0.36f - 0.05f}, {0.25f, 0.25f, s * 0.36f + 0.05f}, {250, 206, 42, 255}, {230, 186, 32, 255}, {200, 160, 30, 255}); return Upload(mb); }(); return m; }
Model& CannonBarrel() { static Model m = [] { rt::MeshBuilder mb; Cyl(mb, {-0.5f, 0, 0}, {1.3f, 0.1f, 0}, 0.2f, {226, 58, 48, 255}, 16); Cyl(mb, {1.15f, 0.09f, 0}, {1.35f, 0.1f, 0}, 0.24f, {250, 206, 42, 255}, 16); Cyl(mb, {-0.55f, 0, 0}, {-0.4f, 0, 0}, 0.24f, {250, 206, 42, 255}, 16); for (int s = -1; s <= 1; s += 2) { Cyl(mb, {-0.7f, 0, s * 0.18f}, {-0.95f, -0.05f, s * 0.22f}, 0.03f, {50, 52, 60, 255}, 6); Sphere(mb, {-0.97f, -0.05f, s * 0.22f}, 0.05f, {50, 52, 60, 255}, 8, 5); }
    // the hopper above the breech (clear, the balls inside drawn per frame)
    for (int k = 0; k < 8; k++) { float a0 = k * PI / 4, a1 = (k + 1) * PI / 4; Vector3 b0{0.1f + cosf(a0) * 0.12f, 0.0f, 0.5f + sinf(a0) * 0.12f}, b1{0.1f + cosf(a1) * 0.12f, 0.0f, 0.5f + sinf(a1) * 0.12f}, t0{0.1f + cosf(a0) * 0.26f, 0.42f, 0.5f + sinf(a0) * 0.26f}, t1{0.1f + cosf(a1) * 0.26f, 0.42f, 0.5f + sinf(a1) * 0.26f}; Cyl(mb, b0, t0, 0.015f, {200, 220, 240, 255}, 4); Cyl(mb, t0, t1, 0.02f, {250, 206, 42, 255}, 4); Cyl(mb, b0, b1, 0.02f, {250, 206, 42, 255}, 4); } Cyl(mb, {0.1f, 0, 0.2f}, {0.1f, 0, 0.42f}, 0.05f, {200, 220, 240, 255}, 8);
    return Upload(mb); }(); return m; }

// ---------------------------------------------------------------- the pits' surfaces: a tiled texture of balls, and real balls on top
Texture2D PitTexture() {
    static Texture2D t{}; if (t.id) return t;
    const int N = 256; Image im = GenImageColor(N, N, {30, 40, 80, 255});
    uint32_t r = 7; auto rnd = [&]() { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return (r & 0xFFFF) / 65535.0f; };
    for (int pass = 0; pass < 3; pass++) for (int k = 0; k < 70; k++) {
        float cx = rnd() * N, cy = rnd() * N, rad = 15 + rnd() * 3; Color c = BALLC[(int)(rnd() * 6) % 6];
        for (int dy = -20; dy <= 20; dy++) for (int dx = -20; dx <= 20; dx++) {
            float d = sqrtf((float)(dx * dx + dy * dy)); if (d > rad) continue;
            float lit = 0.62f + 0.38f * std::clamp((-dx - dy) / (rad * 1.5f) + 0.5f, 0.0f, 1.0f) * (1 - d / rad * 0.4f);
            if (d > rad - 1.5f) lit *= 0.6f;
            Color o = Shade(c, lit); if ((dx + 6) * (dx + 6) + (dy + 6) * (dy + 6) < 10) o = Mix(o, WHITE, 0.6f);
            ImageDrawPixel(&im, ((int)cx + dx + N) % N, ((int)cy + dy + N) % N, o);
        }
    }
    t = LoadTextureFromImage(im); UnloadImage(im); GenTextureMipmaps(&t); SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR); SetTextureWrap(t, TEXTURE_WRAP_REPEAT); return t;
}
struct PitVis { Model surf{}, caps{}; bool made = false; };
PitVis& PitModel(int i) {
    static std::vector<PitVis> v; if (v.size() < W().arena.pits.size()) v.resize(W().arena.pits.size());
    PitVis& p = v[i]; if (p.made) return p;
    const Pit& q = W().arena.pits[i];
    { rt::MeshBuilder mb; float x0 = q.lo.x, x1 = q.hi.x, z0 = q.lo.z, z1 = q.hi.z; float s = 0.6f;
      for (float x = x0; x < x1 - 1e-3f; x += 1) for (float z = z0; z < z1 - 1e-3f; z += 1) { float xa = std::min(x1, x + 1), za = std::min(z1, z + 1); mb.Tri({x, 0, z}, {x, 0, za}, {xa, 0, za}, WHITE, WHITE, WHITE, {x / s, z / s}, {x / s, za / s}, {xa / s, za / s}); mb.Tri({x, 0, z}, {xa, 0, za}, {xa, 0, z}, WHITE, WHITE, WHITE, {x / s, z / s}, {xa / s, za / s}, {xa / s, z / s}); }
      (void)s; mb = rt::MeshBuilder{}; uint32_t rr = 17 + i; auto rn = [&]() { rr ^= rr << 13; rr ^= rr >> 17; rr ^= rr << 5; return (rr & 0xFFFF) / 65535.0f; };
      mb.Quad({x0, -0.01f, z0}, {x0, -0.01f, z1}, {x1, -0.01f, z1}, {x1, -0.01f, z0}, {24, 30, 60, 255});
      const float c = 0.15f;
      for (float x = x0; x < x1 - 1e-3f; x += c) for (float z = z0; z < z1 - 1e-3f; z += c) {
          float jx = (rn() - 0.5f) * 0.05f, jz = (rn() - 0.5f) * 0.05f, h = rn() * 0.03f; Color k = BALLC[(int)(rn() * 6) % 6]; Vector3 m{x + c / 2 + jx, h, z + c / 2 + jz}; float r = c * 0.47f;
          // a little dome: a lit centre and a darker rim (a ball seen from above)
          Vector3 e[6]; for (int q = 0; q < 6; q++) { float a = q * PI / 3; e[q] = {m.x + cosf(a) * r, h - 0.02f, m.z + sinf(a) * r}; }
          Vector3 top{m.x - r * 0.15f, h + 0.03f, m.z - r * 0.15f};
          for (int q = 0; q < 6; q++) mb.Tri(top, e[(q + 1) % 6], e[q], Shade(k, 0.75f + 0.35f * (0.5f + 0.5f * cosf(q * PI / 3 + 2.4f))));
      }
      p.surf = Upload(mb); }
    { rt::MeshBuilder mb; uint32_t r = 99 + i; auto rnd = [&]() { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return (r & 0xFFFF) / 65535.0f; };
      float area = (q.hi.x - q.lo.x) * (q.hi.z - q.lo.z); int n = std::clamp((int)(area * 4.0f), 80, 1200);
      for (int k = 0; k < n; k++) { Vector3 c{q.lo.x + 0.08f + rnd() * (q.hi.x - q.lo.x - 0.16f), (rnd() - 0.3f) * 0.07f, q.lo.z + 0.08f + rnd() * (q.hi.z - q.lo.z - 0.16f)}; Sphere(mb, c, Cfg().ballR, BALLC[(int)(rnd() * 6) % 6], 6, 4); }
      p.caps = Upload(mb); }
    p.made = true; return p;
}
float PitSurfaceY(const Pit& q) { return q.lo.y + 0.25f + (q.hi.y - q.lo.y - 0.25f) * std::clamp((float)q.balls / std::max(1.0f, q.cap * 0.62f), 0.0f, 1.0f); }

// ---------------------------------------------------------------- drawing the world
const Model* Body() { return rt::LoadAsset("shared/crew/crew_diver.glb"); }
Matrix Orient(Vector3 p, float yaw, float pitch, float s = 1) { return MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(s, s, s), MatrixRotateZ(pitch)), MatrixRotateY(-yaw)), MatrixTranslate(p.x, p.y, p.z)); }
void DrawGunAt(int g, Matrix m) { rt::DrawStatic(GunModel(g), m, WHITE); if (g == G_DUAL) rt::DrawStatic(GunModel(g), MatrixMultiply(MatrixTranslate(0, 0, -0.12f), m), WHITE); }
void DrawPerson(const Player& p, Vector3 feet, float scale, Color top, bool kid, float t, int heldGun) {
    const Model* m = Body(); if (!m) return;
    fig::Pose P; fig::Build B; B.build = kid ? 1.15f : 1.0f + 0.04f * (p.id % 3); B.headW = kid ? 1.25f : 1; B.headH = kid ? 1.2f : 1;
    float spd = Vector3Length({p.vel.x, 0, p.vel.z});
    P.walk = std::min(1.0f, spd / 5); P.walkPh = t * (4 + spd * 1.5f) + p.id; P.breathe = t * 2 + p.id;
    if (p.po == PO_CROUCH || p.submerged || p.inTunnel) P.crouch = 0.8f;
    if (p.po == PO_CLIMB) { P.reach = 1; P.elbow = 1; P.grip = 1; P.walk = 0.6f; P.walkPh = p.pos.y * 4; }
    if (p.po == PO_SLIDE) { P.sit = 1; }
    if (!kid && p.po != PO_CLIMB && p.po != PO_SLIDE) { P.reach = 0.55f; P.elbow = 0.5f; P.grip = 1; }
    if (p.knifeSwing > 0) P.swingT = 1 - p.knifeSwing / 0.3f;
    if (p.fingerT > 0) { P.elbow = 1; P.reach = 0.3f + 0.2f * sinf(t * 9); }
    if (kid) { P.shout = 0.6f + 0.4f * sinf(t * 7 + p.id); P.fear = 0.5f; P.elbow = 0.8f + 0.2f * sinf(t * 9 + p.id); P.reach = 0.6f; }
    std::vector<Matrix> skin = fig::PoseFigure(*m, B, P, t);
    Matrix frame = MatrixMultiply(MatrixScale(scale, scale, scale), fig::Frame(feet, -p.yaw));
    std::vector<rt::Recolor> rc = {{"top", top}, {"trousers", kid ? Mix(top, Color{40, 40, 60, 255}, 0.5f) : TEAM_DARK[p.team & 1]}, {"hat", top}, {"skin", Color{(unsigned char)(196 + 12 * (p.id % 4)), (unsigned char)(146 + 12 * (p.id % 5)), 116, 255}}};
    rt::DrawPbrSkinned(*m, frame, skin, rc, 0.35f, WHITE);
    if (heldGun >= 0 && p.po != PO_CLIMB && p.po != PO_SLIDE) {
        Vector3 fist = fig::FistWorld(*m, skin, frame, 1);
        DrawGunAt(heldGun, Orient(fist, p.yaw, p.pitch * 0.7f, 1.2f * scale));
    }
    // hats and silly things (the joke shop)
    Vector3 head{feet.x, feet.y + (p.submerged ? 0.55f : (P.crouch > 0 ? 1.25f : 1.82f)) * scale, feet.z};
    if (p.crown) { rt::MeshBuilder* none = nullptr; (void)none; for (int k = 0; k < 5; k++) { float a = k * 2 * PI / 5 + p.yaw; rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.06f, 0.1f, 0.02f), MatrixRotateY(-a)), MatrixTranslate(head.x + cosf(a) * 0.1f, head.y + 0.05f, head.z + sinf(a) * 0.1f)), {250, 210, 60, 255}); } }
    if (p.beanie) { rt::DrawStatic(SphereM(), MatrixMultiply(MatrixScale(0.13f, 0.07f, 0.13f), MatrixTranslate(head.x, head.y, head.z)), {228, 58, 48, 255}); float spin = S.t * (4 + spd * 6); for (int k = 0; k < 2; k++) rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.22f, 0.01f, 0.04f), MatrixRotateY(spin + k * PI / 2)), MatrixTranslate(head.x, head.y + 0.1f, head.z)), k ? Color{250, 206, 42, 255} : Color{44, 128, 228, 255}); }
    if (p.carry >= 0) { Color c = TEAM[p.carry & 1]; rt::DrawCubeM(MatrixMultiply(MatrixScale(0.03f, 1.2f, 0.03f), MatrixTranslate(feet.x - cosf(p.yaw) * 0.25f, feet.y + 1.4f, feet.z - sinf(p.yaw) * 0.25f)), {230, 230, 230, 255}); rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.5f, 0.32f, 0.02f), MatrixTranslate(feet.x - cosf(p.yaw) * 0.25f, feet.y + 1.85f, feet.z - sinf(p.yaw) * 0.25f + 0.25f)), c, 0.4f); }
    if (p.bomb) rt::DrawStatic(SphereM(), MatrixMultiply(MatrixScale(0.25f, 0.25f, 0.25f), MatrixTranslate(feet.x - cosf(p.yaw) * 0.32f, feet.y + 1.2f, feet.z - sinf(p.yaw) * 0.32f)), {40, 40, 46, 255});
}
void DrawCannons() {
    const World& w = W(); float t = S.t;
    for (int c = 0; c < (int)w.cannons.size(); c++) {
        const Cannon& k = w.cannons[c]; const CannonDef& d = w.arena.cannons[c];
        rt::DrawStatic(CannonBase(), MatrixMultiply(MatrixRotateY(-k.yaw), MatrixTranslate(d.pivot.x, d.pivot.y, d.pivot.z)), WHITE);
        Color team = d.team < 0 ? Color{250, 250, 250, 255} : TEAM[d.team];
        float rec = k.shotT > 0 ? k.shotT * 0.6f : 0;
        Matrix bm = MatrixMultiply(MatrixMultiply(MatrixTranslate(-rec, 0, 0), MatrixRotateZ(k.pitch)), MatrixMultiply(MatrixRotateY(-k.yaw), MatrixTranslate(d.pivot.x, d.pivot.y, d.pivot.z)));
        rt::DrawStatic(CannonBarrel(), bm, k.overheated ? Mix(WHITE, Color{255, 120, 80, 255}, 0.4f + 0.3f * sinf(t * 12)) : Mix(WHITE, team, 0.25f));
        if (k.heat > 1.5f || k.overheated) rt::DrawCubeGlow(MatrixMultiply(MatrixMultiply(MatrixScale(0.5f, 0.05f, 0.05f), MatrixTranslate(0.6f, 0.26f, 0)), bm), {255, 120, 60, 255}, 0.5f + 0.5f * k.heat / Cfg().cannonHeat);
        // the hopper's balls (up to 40, stacked)
        int n = std::min(k.hopper, 40); for (int i = 0; i < n; i++) { float ring = 0.06f + 0.16f * ((i % 8) / 8.0f), a = i * 2.39f, y = 0.04f + (i / 8) * 0.075f; Vector3 lp{0.1f + cosf(a) * ring, y, 0.5f + sinf(a) * ring}; Vector3 wp = Vector3Transform(lp, bm); float r = Cfg().ballR; rt::DrawStatic(SphereM(), MatrixMultiply(MatrixScale(r, r, r), MatrixTranslate(wp.x, wp.y, wp.z)), BALLC[i % 6]); }
    }
}
void DrawBallsAndDarts() {
    const World& w = W(); const Config& C = Cfg(); float r = C.ballR;
    for (size_t i = 0; i < w.balls.size(); i++) {
        const Ball& b = w.balls[i]; if (b.st == BS_HELD && b.holder == S.me) continue;
        Color c = BALLC[(i * 7 + (size_t)(b.p.x * 3)) % 6];
        rt::DrawStatic(SphereM(), MatrixMultiply(MatrixScale(r, r, r), MatrixTranslate(b.p.x, b.p.y, b.p.z)), c);
        if (b.st == BS_LIVE) { rt::DrawStaticGlow(SphereM(), MatrixMultiply(MatrixScale(r * 1.2f, r * 1.2f, r * 1.2f), MatrixTranslate(b.p.x, b.p.y, b.p.z)), Mix(c, WHITE, 0.3f), 0.25f); for (int k = 1; k <= 3; k++) { Vector3 q = Vector3Subtract(b.p, Vector3Scale(b.v, 0.012f * k)); float rr = r * (1 - 0.22f * k); rt::DrawStaticGlow(SphereM(), MatrixMultiply(MatrixScale(rr, rr, rr), MatrixTranslate(q.x, q.y, q.z)), Mix(c, WHITE, 0.5f), 0.3f - 0.07f * k); } }
    }
    Vector3 cp = S.cam.position;
    for (size_t i = 0; i < w.darts.size(); i++) {
        const Dart& d = w.darts[i]; if (Vector3Distance(d.p, cp) > (d.live ? 70.0f : 26.0f)) continue;
        if (d.kind == 2) { float yaw = atan2f(d.v.z, d.v.x), pitch = atan2f(d.v.y, sqrtf(d.v.x * d.v.x + d.v.z * d.v.z)); rt::DrawStatic(RocketM(), Orient(d.p, yaw, pitch), WHITE); rt::DrawStaticGlow(SphereM(), MatrixMultiply(MatrixScale(0.06f, 0.06f, 0.06f), MatrixTranslate(d.p.x - cosf(yaw) * 0.22f, d.p.y, d.p.z - sinf(yaw) * 0.22f)), {255, 200, 120, 255}, 0.8f); continue; }
        if (d.live) { float yaw = atan2f(d.v.z, d.v.x), pitch = atan2f(d.v.y, sqrtf(d.v.x * d.v.x + d.v.z * d.v.z)); rt::DrawStatic(DartM(d.kind == 1), Orient(d.p, yaw, pitch, d.kind == 1 ? 1.0f : 1.6f), WHITE); }
        else { float yaw = (i * 2.4f) + d.p.x; rt::DrawStatic(DartM(d.kind == 1), Orient({d.p.x, d.p.y + 0.01f, d.p.z}, yaw, 0, d.kind == 1 ? 1.0f : 1.6f), WHITE); }
    }
}
void DrawPits() {
    const World& w = W();
    for (int i = 0; i < (int)w.arena.pits.size(); i++) {
        const Pit& q = w.arena.pits[i]; PitVis& v = PitModel(i); float y = PitSurfaceY(q);
        float bobT = sinf(S.t * 0.8f + i) * 0.006f;
        rt::DrawStatic(v.surf, MatrixTranslate(0, y - 0.04f + bobT, 0), WHITE);
        rt::DrawStatic(v.caps, MatrixTranslate(0, y - 0.02f + bobT, 0), WHITE);
    }
}
void DrawConveyor() {
    const World& w = W(); int n = std::min<int>((int)w.belt.arrive.size(), 120); float H = w.arena.ceil;
    for (int k = 0; k < n; k++) {
        float due = k < (int)w.belt.arrive.size() ? w.belt.arrive[k] - w.t : 4; float u = std::clamp(1 - due / Cfg().conveyorTime, 0.0f, 1.0f);
        int h = k & 1; float zs = h ? 1.0f : -1.0f; float startX = ((k * 37) % 56) - 28.0f;
        Vector3 p; float r = Cfg().ballR;
        if (u < 0.7f) { float v = u / 0.7f; p = {startX * (1 - v), 0.45f + r, zs * 13 + ((k % 3) - 1) * 0.2f}; }   // (riding the belt to the hub)
        else { float v = (u - 0.7f) / 0.3f; p = {0, 2.2f + (H - 4.2f) * v, zs * 14.3f}; }   // (up the lift pipe)
        rt::DrawStatic(SphereM(), MatrixMultiply(MatrixScale(r, r, r), MatrixTranslate(p.x, p.y, p.z)), BALLC[k % 6]);
    }
}
void DrawEnts() {
    const World& w = W();
    for (const auto& e : w.ents) {
        switch (e.kind) {
            case E_HEALTHBOX: rt::DrawCubeM(MatrixMultiply(MatrixScale(0.5f, 0.32f, 0.36f), MatrixTranslate(e.p.x, e.p.y + 0.16f, e.p.z)), {245, 245, 240, 255}); rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.32f, 0.08f, 0.38f), MatrixTranslate(e.p.x, e.p.y + 0.18f, e.p.z)), {228, 40, 40, 255}, 0.3f); rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.08f, 0.33f, 0.38f), MatrixTranslate(e.p.x, e.p.y + 0.16f, e.p.z)), {228, 40, 40, 255}, 0.3f); break;
            case E_KID: { Player kid; kid.id = e.owner * 7 + (int)(e.goal.x * 3); kid.team = e.team & 1; kid.pos = e.p; kid.vel = e.v; kid.yaw = e.yaw; static const Color KC[5] = {{250, 90, 160, 255}, {80, 200, 230, 255}, {250, 210, 60, 255}, {130, 220, 90, 255}, {200, 120, 250, 255}}; DrawPerson(kid, e.p, 0.6f, KC[(int)fabsf(e.goal.z * 10) % 5], true, S.t + e.goal.x, -1); break; }
            case E_RCCAR: { Matrix m = Orient(e.p, e.yaw, 0); rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.5f, 0.14f, 0.3f), MatrixTranslate(0, 0.13f, 0)), m), {228, 58, 48, 255}); for (int k = 0; k < 4; k++) rt::DrawStatic(SphereM(), MatrixMultiply(MatrixMultiply(MatrixScale(0.07f, 0.07f, 0.04f), MatrixTranslate((k & 1) ? 0.17f : -0.17f, 0.07f, (k & 2) ? 0.16f : -0.16f)), m), {30, 30, 34, 255}); DrawGunAt(G_FLYWHEEL, MatrixMultiply(MatrixMultiply(MatrixScale(0.8f, 0.8f, 0.8f), MatrixTranslate(-0.05f, 0.22f, 0)), m)); break; }
            case E_DRONE: { Matrix m = Orient(e.p, e.yaw, 0); rt::DrawCubeM(MatrixMultiply(MatrixScale(0.26f, 0.08f, 0.26f), m), {60, 64, 80, 255}); for (int k = 0; k < 4; k++) { float a = k * PI / 2 + PI / 4; Vector3 q{cosf(a) * 0.24f, 0.05f, sinf(a) * 0.24f}; rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.2f, 0.01f, 0.03f), MatrixRotateY(S.t * 40 + k)), MatrixMultiply(MatrixTranslate(q.x, q.y, q.z), m)), {200, 200, 210, 255}); } rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.05f, 0.05f, 0.05f), MatrixMultiply(MatrixTranslate(0.13f, 0, 0), m)), e.team == Me().team ? Color{80, 255, 120, 255} : Color{255, 60, 60, 255}, 1.0f); DrawGunAt(G_PISTOL, MatrixMultiply(MatrixTranslate(0, -0.1f, 0), m)); break; }
            case E_TANK: { Matrix m = Orient(e.p, e.yaw, 0); Color c = e.team >= 0 ? TEAM[e.team & 1] : Color{120, 160, 90, 255};
                rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(1.7f, 0.55f, 1.2f), MatrixTranslate(0, 0.38f, 0)), m), c);
                for (int s = -1; s <= 1; s += 2) rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(1.8f, 0.4f, 0.28f), MatrixTranslate(0, 0.2f, s * 0.62f)), m), {50, 52, 60, 255});
                rt::DrawStatic(SphereM(), MatrixMultiply(MatrixMultiply(MatrixScale(0.5f, 0.32f, 0.5f), MatrixTranslate(0, 0.72f, 0)), m), Mix(c, WHITE, 0.2f));
                for (int s = -1; s <= 1; s += 2) DrawGunAt(G_FLYWHEEL, MatrixMultiply(MatrixMultiply(MatrixScale(1.3f, 1.3f, 1.3f), MatrixTranslate(0.25f, 0.85f, s * 0.3f)), m));
                break; }
            case E_CHICKEN: { Matrix m = Orient(e.p, e.yaw, sinf(S.t * 10) * 0.4f); rt::DrawStatic(SphereM(), MatrixMultiply(MatrixScale(0.13f, 0.06f, 0.06f), m), {250, 220, 40, 255}); rt::DrawStatic(SphereM(), MatrixMultiply(MatrixMultiply(MatrixScale(0.05f, 0.05f, 0.05f), MatrixTranslate(0.14f, 0.04f, 0)), m), {250, 220, 40, 255}); rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.04f, 0.02f, 0.02f), MatrixTranslate(0.2f, 0.04f, 0)), m), {240, 120, 30, 255}); rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.02f, 0.04f, 0.01f), MatrixTranslate(0.15f, 0.1f, 0)), m), {220, 40, 40, 255}); break; }
            case E_CUSHION: if (Friend(w.players[std::clamp(e.owner, 0, (int)w.players.size() - 1)]) || e.owner == S.me) rt::DrawStatic(SphereM(), MatrixMultiply(MatrixScale(0.2f, 0.04f, 0.2f), MatrixTranslate(e.p.x, e.p.y + 0.04f, e.p.z)), {240, 110, 160, 255}); break;
            default: break;
        }
    }
    // flags and the bomb
    if (w.mode == MD_CTF) for (int s = 0; s < 2; s++) {
        const Flag& f = w.flags[s]; if (f.carrier >= 0) continue; Vector3 p = f.p;
        rt::DrawCubeM(MatrixMultiply(MatrixScale(0.04f, 2.2f, 0.04f), MatrixTranslate(p.x, p.y + 1.1f, p.z)), {230, 230, 230, 255});
        float wave = sinf(S.t * 3) * 0.08f; rt::DrawCubeGlow(MatrixMultiply(MatrixMultiply(MatrixScale(0.02f, 0.5f, 0.8f), MatrixRotateY(wave)), MatrixTranslate(p.x, p.y + 1.9f, p.z + 0.4f)), TEAM[s], 0.4f);
    }
    if (w.mode == MD_BOMB && w.bomb.carrier < 0 && !w.bomb.done) {
        Vector3 p = w.bomb.p; rt::DrawStatic(SphereM(), MatrixMultiply(MatrixScale(0.32f, 0.32f, 0.32f), MatrixTranslate(p.x, p.y + 0.32f, p.z)), {40, 40, 46, 255});
        rt::DrawCubeM(MatrixMultiply(MatrixScale(0.66f, 0.08f, 0.1f), MatrixTranslate(p.x, p.y + 0.32f, p.z)), {250, 206, 42, 255});
        if (w.bomb.planted) rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.06f, 0.06f, 0.06f), MatrixTranslate(p.x, p.y + 0.7f, p.z)), {255, 60, 40, 255}, fmodf(S.t, w.bomb.fuseT < 10 ? 0.3f : 1.0f) < 0.15f ? 2.0f : 0.2f);
    }
}
void DrawPlayers() {
    const World& w = W();
    for (const auto& p : w.players) {
        if (!p.present || !p.alive) continue;
        if (p.id == S.me && Me().drive < 0) continue;   // (your own body: first person)
        if (p.submerged) continue;                       // (under the balls: hidden)
        if (p.po == PO_SLIDE) continue;                  // (inside the tube)
        int g = p.ball >= 0 ? -1 : p.fingerT > 0 ? 102 : (p.wield == 1 && p.gun < G_COUNT ? p.gun : G_STARTER);
        Vector3 feet = p.pos; if (p.inPit) feet.y -= 0.0f;
        DrawPerson(p, feet, 1.0f, TeamCol(p), false, S.t, g);
        if (p.ball >= 0) { Vector3 fwd{cosf(p.yaw), 0, sinf(p.yaw)}; Vector3 b = Vector3Add(p.pos, Vector3Add(Vector3Scale(fwd, 0.4f), {0, 1.3f, 0})); float r = Cfg().ballR; rt::DrawStatic(SphereM(), MatrixMultiply(MatrixScale(r, r, r), MatrixTranslate(b.x, b.y, b.z)), BALLC[p.id % 6]); }
    }
}
void DrawFx(float dt) {
    for (auto& f : S.fx) { f.t += dt; f.v.y -= (f.kind == 0 ? 2.0f : 9.0f) * dt; f.p = Vector3Add(f.p, Vector3Scale(f.v, dt)); if (f.kind == 0) { f.v.x *= 1 - dt; f.v.z *= 1 - dt; } }
    S.fx.erase(std::remove_if(S.fx.begin(), S.fx.end(), [](const Fx& f) { return f.t > f.life; }), S.fx.end());
    for (const auto& f : S.fx) { float s = f.kind == 0 ? 0.03f : 0.025f * (1 - f.t / f.life); rt::DrawCubeGlow(MatrixMultiply(MatrixMultiply(MatrixScale(s, f.kind == 0 ? 0.004f : s, s), MatrixRotateY(f.t * 9 + f.p.x)), MatrixTranslate(f.p.x, f.p.y, f.p.z)), f.c, f.kind == 0 ? 0.6f : 0.9f); }
}

// ---------------------------------------------------------------- the viewmodel: your blaster in your hands
void HandMatrix(Matrix* out, Vector3 at, Vector3 fwd, Vector3 axis, float s) {
    Vector3 y = Vector3Normalize(axis), x = Vector3Normalize(Vector3Subtract(fwd, Vector3Scale(y, Vector3DotProduct(fwd, y)))), z = Vector3CrossProduct(x, y);
    x = Vector3Scale(x, s); y = Vector3Scale(y, s); z = Vector3Scale(z, s);
    *out = Matrix{x.x, y.x, z.x, at.x, x.y, y.y, z.y, at.y, x.z, y.z, z.z, at.z, 0, 0, 0, 1};
}
void DrawViewmodel() {
    const Player& p = Me(); if (!p.alive || p.po == PO_SLIDE || p.po == PO_CANNON || p.po == PO_DRIVE || p.submerged || S.freeCam || W().phase == PH_OVER) return;
    const Camera3D& cam = S.cam;
    Vector3 f = Vector3Normalize(Vector3Subtract(cam.target, cam.position)), rgt = Vector3Normalize(Vector3CrossProduct(f, {0, 1, 0})), up = Vector3CrossProduct(rgt, f);
    auto P = [&](float fw, float side, float h) { return Vector3Add(cam.position, Vector3Add(Vector3Scale(f, fw), Vector3Add(Vector3Scale(rgt, side), Vector3Scale(up, h)))); };
    const Model* arms = rt::LoadAsset("shared/divers/fp_sailor.glb");
    std::vector<rt::Recolor> rc = {{"top", TeamCol(p)}, {"leather", {60, 52, 50, 255}}};
    float bob = sinf(S.bob * 2) * 0.012f, bobX = cosf(S.bob) * 0.01f;
    if (p.po == PO_CLIMB) {   // both hands on the net, reaching up in turn
        float c = sinf(p.pos.y * 4); Matrix hr, hl;
        HandMatrix(&hr, P(0.42f, 0.2f, -0.1f + c * 0.12f), f, up, 1.5f); HandMatrix(&hl, P(0.42f, -0.2f, -0.1f - c * 0.12f), f, up, 1.5f);
        if (arms) rt::DrawVmArms(*arms, cam, &hr, &hl, true, 1.1f, S.t, rc, {200, 200, 200, 255});
        return;
    }
    if (p.ball >= 0) {   // a ball held up in the right hand, drawn back for the throw
        float wind = p.throwT > 0 ? 1 - p.throwT / Cfg().throwWindup : 0;
        Vector3 at = P(0.42f - wind * 0.15f, 0.2f + wind * 0.05f, -0.16f + wind * 0.12f + bob);
        float r = Cfg().ballR * 0.9f; rt::DrawStatic(SphereM(), MatrixMultiply(MatrixScale(r, r, r), MatrixTranslate(at.x, at.y + 0.04f, at.z)), BALLC[S.me % 6]);
        Matrix hr; HandMatrix(&hr, Vector3Subtract(at, Vector3Scale(up, 0.03f)), f, up, 1.5f); if (arms) rt::DrawVmArms(*arms, cam, &hr, nullptr, false, 1.1f, S.t, rc, {200, 200, 200, 255});
        return;
    }
    int g = p.vacuuming ? 101 : p.fingerT > 0 ? 102 : (p.knifeSwing > 0 || p.inTunnel) ? 100 : (p.wield == 1 && p.gun < G_COUNT ? p.gun : G_STARTER);
    GunVis gv = g < G_COUNT ? GunInfo(g) : GunVis{};
    bool ads = p.in.aim && g < G_COUNT && Cfg().guns[g].ads && p.reloadT <= 0;
    float side = ads ? 0.0f : 0.15f, hgt = ads ? -0.085f : -0.15f, fwd = ads ? 0.3f : 0.36f;
    float rl = p.reloadT > 0 ? sinf(std::clamp(1 - p.reloadT / std::max(0.1f, Cfg().guns[std::min(g, (int)G_COUNT - 1)].reload), 0.0f, 1.0f) * PI) : 0;
    float sw = p.swapT > 0 ? p.swapT / 0.3f : 0;
    Vector3 at = P(fwd - S.recoil * 0.06f, side + bobX + S.swayX, hgt + bob - rl * 0.1f - sw * 0.2f + S.swayY);
    // the gun's frame: x along the look (kicked up by the recoil, rolled while reloading), y up, z right
    Vector3 gx = Vector3Normalize(Vector3Add(f, Vector3Scale(up, S.recoil * 0.25f + rl * 0.3f))), gz = Vector3Normalize(Vector3Add(rgt, Vector3Scale(up, rl * 0.5f))), gy = Vector3Normalize(Vector3CrossProduct(gz, gx));
    if (g == 100 && p.knifeSwing > 0) { float k = 1 - p.knifeSwing / 0.3f; float a = (k - 0.4f) * 2.4f; gx = Vector3Normalize(Vector3Add(Vector3Scale(f, cosf(a)), Vector3Scale(rgt, -sinf(a)))); at = Vector3Add(at, Vector3Scale(f, sinf(k * PI) * 0.12f)); gy = Vector3Normalize(Vector3CrossProduct(gz, gx)); }
    if (g == 102) { gx = up; gy = Vector3Scale(f, -1); at = Vector3Add(at, Vector3Scale(up, 0.08f + 0.04f * sinf(S.t * 9))); }
    float sc = 0.72f;
    Matrix gm = Matrix{gx.x * sc, gy.x * sc, gz.x * sc, at.x, gx.y * sc, gy.y * sc, gz.y * sc, at.y, gx.z * sc, gy.z * sc, gz.z * sc, at.z, 0, 0, 0, 1};
    rt::DrawStatic(GunModel(g), gm, WHITE);
    if (g == G_DUAL) { Matrix gm2 = gm; Vector3 o = Vector3Scale(rgt, -0.34f + side * -0.0f); gm2.m12 += o.x; gm2.m13 += o.y; gm2.m14 += o.z; rt::DrawStatic(GunModel(g), gm2, WHITE); }
    // the muzzle flash of foam: a little puff for a frame
    if (S.kick > 0 && g < G_COUNT) { Vector3 mz = Vector3Transform(gv.muzzle, gm); rt::DrawStaticGlow(SphereM(), MatrixMultiply(MatrixScale(0.02f + S.kick * 0.04f, 0.02f + S.kick * 0.04f, 0.02f + S.kick * 0.04f), MatrixTranslate(mz.x, mz.y, mz.z)), {255, 240, 220, 255}, 0.6f); }
    // the hands: right on the grip, left under the fore-end (or the second blaster)
    if (arms) {
        Matrix hr, hl; HandMatrix(&hr, Vector3Transform(gv.gripR, gm), gx, Vector3Normalize(Vector3Add(gy, Vector3Scale(gx, -0.25f))), 1.5f);
        bool two = g < G_COUNT && (gv.twoHand || g == G_DUAL);
        if (g == G_DUAL) { Matrix gm2 = gm; Vector3 o = Vector3Scale(rgt, -0.34f); HandMatrix(&hl, Vector3Add(Vector3Transform(gv.gripR, gm), o), gx, Vector3Normalize(Vector3Add(gy, Vector3Scale(gx, -0.25f))), 1.5f); (void)gm2; rt::DrawVmArms(*arms, cam, &hr, &hl, true, 1.1f, S.t, rc, {200, 200, 200, 255}); }
        else if (two) { HandMatrix(&hl, Vector3Transform(gv.gripL, gm), gx, gy, 1.5f); rt::DrawVmArms(*arms, cam, &hr, &hl, true, 1.1f, S.t, rc, {200, 200, 200, 255}); }
        else rt::DrawVmArms(*arms, cam, &hr, nullptr, false, 1.1f, S.t, rc, {200, 200, 200, 255});
    }
}

// ---------------------------------------------------------------- events: pops, the feed, sound
void Cue(const char* name, Vector3 at, float vol) { if (S.shot) return; float d = Vector3Distance(at, S.cam.position); float v = vol * std::clamp(1 - d / 40, 0.0f, 1.0f); if (v > 0.02f) PlayCue(name, v); }
void ReadEvents() {
    World& w = W(); auto& ev = w.events; const Config& C = Cfg();
    // (the log trims itself from the front: read by the running total)
    uint32_t fresh = w.evCount - S.evCountSeen; S.evCountSeen = w.evCount;
    size_t from = ev.size() - std::min<size_t>(fresh, ev.size());
    for (size_t i = from; i < ev.size(); i++) {
        const Event& e = ev[i]; bool mine = e.who == S.me, byMe = e.by == S.me;
        switch (e.kind) {
            case EV_SHOT: if (mine && (int)e.a < 90) { S.recoil = 1; S.kick = 0.06f; } Cue("hit.shot", e.at, mine ? 0.45f : 0.3f); for (int k = 0; k < 3; k++) S.fx.push_back({1, e.at, {(GetRandomValue(-10, 10)) * 0.05f, 0.5f, (GetRandomValue(-10, 10)) * 0.05f}, 0, 0.2f, {255, 250, 240, 255}}); break;
            case EV_ROCKET: if (mine) { S.recoil = 1.6f; S.kick = 0.1f; } Cue("hit.cast", e.at, 0.6f); break;
            case EV_CANNON: if (mine) { S.recoil = 0.4f; S.shake = std::max(S.shake, 0.15f); } Cue("hit.blunt", e.at, 0.5f); break;
            case EV_HIT: if (mine && e.a > 0) { S.flash = std::min(1.0f, S.flash + e.a / 120); S.hurtT = 1; if (e.by >= 0 && e.by < (int)w.players.size()) { Vector3 d = Vector3Subtract(w.players[e.by].pos, Me().pos); S.hurtYaw = atan2f(d.z, d.x); } } if (byMe && !mine) { S.hitMark = 0.25f; Cue("imp.flesh", S.cam.position, 0.5f); } else Cue("imp.flesh", e.at, 0.25f); break;
            case EV_KO: {
                std::string how = e.a > 0.5f ? "cannon" : "";
                Color c = e.by == S.me ? Color{255, 220, 90, 255} : e.who == S.me ? Color{255, 120, 100, 255} : Color{230, 230, 236, 255};
                S.feed.push_back({(e.by >= 0 ? NameOf(e.by) : std::string("The fall")) + (how.empty() ? "  >  " : "  [cannon]  >  ") + NameOf(e.who), S.t, c});
                if (byMe && !mine) { S.pops.push_back({"KNOCKOUT  +" + std::to_string(e.a > 0.5f ? C.scoreCannonKO : C.scoreKO), {255, 220, 90, 255}, S.t}); Cue("mus.kill", S.cam.position, 0.6f); }
                if (mine) { S.shake = 0.5f; Cue("hero.pain", S.cam.position, 0.6f); }
                for (int k = 0; k < 14; k++) S.fx.push_back({0, Vector3Add(e.at, {0, 1.2f, 0}), {GetRandomValue(-20, 20) * 0.1f, GetRandomValue(5, 30) * 0.1f, GetRandomValue(-20, 20) * 0.1f}, 0, 1.4f, BALLC[k % 6]});
                break; }
            case EV_ASSIST: if (mine) S.pops.push_back({"Assist  +" + std::to_string(C.scoreAssist), {200, 220, 255, 255}, S.t}); break;
            case EV_THROW: Cue("hit.throw", e.at, mine ? 0.7f : 0.4f); break;
            case EV_BOUNCE: Cue("imp.shell", e.at, 0.25f); break;
            case EV_GRAB: if (mine) Cue("ui.drop", e.at, 0.5f); break;
            case EV_KNIFE: Cue("hit.slash", e.at, mine ? 0.6f : 0.35f); break;
            case EV_OVERHEAT: if (mine) S.pops.push_back({"OVERHEATED", {255, 140, 80, 255}, S.t}); Cue("exp.static", e.at, 0.5f); break;
            case EV_BUY: if (mine) { Cue("ui.confirm", S.cam.position, 0.7f); int b = (int)e.a; S.pops.push_back({b < G_COUNT ? "Bought: " + C.guns[b].name : b == 100 ? std::string("Darts +12") : b == 101 ? std::string("Disarm kit") : "Bought: " + C.jokes[std::clamp(b - 200, 0, (int)C.jokes.size() - 1)].name, {160, 255, 170, 255}, S.t}); } break;
            case EV_VACUUM: if (mine && fmodf(S.t, 0.12f) < 0.05f) Cue("arc.current", S.cam.position, 0.25f); break;
            case EV_RESPAWN: if (mine) { S.camYaw = Me().yaw; S.camPitch = 0; } break;
            case EV_STREAK: if (mine) { S.pops.push_back({"STREAK!  " + std::string(RewardName((int)e.a)) + " ready", {255, 200, 80, 255}, S.t}); Cue("ui.levelup", S.cam.position, 0.7f); } break;
            case EV_REWARD: if ((int)e.a < 10) { S.feed.push_back({NameOf(e.who) + " used " + RewardName((int)e.a), S.t, {255, 200, 80, 255}}); Cue("mus.boss", e.at, mine ? 0.6f : 0.4f); } else if ((int)e.a == 12) Cue("hero.pain", e.at, 0.15f); else if ((int)e.a == 11 && mine) S.pops.push_back({"+50 health", {120, 255, 140, 255}, S.t}); break;
            case EV_JOKE: {
                int a = (int)e.a; const Player& who = w.players[std::clamp(e.who, 0, (int)w.players.size() - 1)]; bool team = w.mode != MD_FFA && who.team == Me().team;
                if (a == 500) { Cue("arc.gull", e.at, 0.5f); break; }   // (the chicken squawks: everyone hears that one)
                if (a == 501) { if (team || e.who == S.me) { S.pops.push_back({"*PFFFRRT*", {240, 140, 180, 255}, S.t}); Cue("hit.song", e.at, 0.6f); } break; }
                if (!team && e.who != S.me) break;   // (joke sounds are for your own team)
                if (a >= 3000) { S.feed.push_back({NameOf(e.who) + " crowned " + NameOf(a - 3000), S.t, {250, 210, 60, 255}}); break; }
                if (a >= 2000) { if (e.who == S.me) S.pops.push_back({"Prize: " + C.prizes[std::clamp(a - 2000, 0, (int)C.prizes.size() - 1)], {200, 160, 255, 255}, S.t}); break; }
                if (a >= 1000) { S.dadLine = C.dadJokes[std::clamp(a - 1000, 0, (int)C.dadJokes.size() - 1)]; S.dadT = 7; Cue("ui.chalk", S.cam.position, 0.5f); break; }
                const std::string& key = C.jokes[std::clamp(a, 0, (int)C.jokes.size() - 1)].key;
                if (key == "horn") Cue("hub.embark", e.at, 0.6f); else if (key == "kazoo") Cue("hit.song", e.at, 0.6f); else if (key == "juice") Cue("arc.current", e.at, 0.6f);
                else if (key == "confetti") { for (int k = 0; k < 40; k++) S.fx.push_back({0, Vector3Add(e.at, {0, 1.6f, 0}), {GetRandomValue(-30, 30) * 0.1f, GetRandomValue(10, 40) * 0.1f, GetRandomValue(-30, 30) * 0.1f}, 0, 2.0f, BALLC[k % 6]}); Cue("hit.cast", e.at, 0.4f); }
                break; }
            case EV_FLAG_TAKE: S.feed.push_back({NameOf(e.who) + " took the " + std::string(TEAM_NAME[(int)e.a & 1]) + " flag", S.t, TEAM[(int)e.a & 1]}); Cue("mus.phase", S.cam.position, 0.5f); break;
            case EV_FLAG_DROP: S.feed.push_back({std::string(TEAM_NAME[(int)e.a & 1]) + " flag dropped", S.t, TEAM[(int)e.a & 1]}); break;
            case EV_FLAG_RETURN: S.feed.push_back({std::string(TEAM_NAME[(int)e.a & 1]) + " flag returned", S.t, TEAM[(int)e.a & 1]}); break;
            case EV_CAPTURE: S.bannerT = 3; S.banner = std::string(TEAM_NAME[(int)e.a & 1]) + " CAPTURE!"; S.bannerCol = TEAM[(int)e.a & 1]; Cue("mus.crit", S.cam.position, 0.8f); break;
            case EV_PLANT: S.bannerT = 3; S.banner = "BOMB PLANTED"; S.bannerCol = {255, 90, 70, 255}; Cue("mus.drum", S.cam.position, 0.8f); break;
            case EV_DEFUSE: S.bannerT = 3; S.banner = "BOMB DISARMED"; S.bannerCol = {120, 220, 255, 255}; Cue("mus.bell", S.cam.position, 0.8f); break;
            case EV_BOOM: S.shake = 1.0f; S.bannerT = 3; S.banner = "BOOM!"; S.bannerCol = {255, 160, 60, 255}; Cue("mus.boss", S.cam.position, 1.0f); for (int k = 0; k < 80; k++) S.fx.push_back({0, Vector3Add(e.at, {0, 0.6f, 0}), {GetRandomValue(-60, 60) * 0.1f, GetRandomValue(20, 80) * 0.1f, GetRandomValue(-60, 60) * 0.1f}, 0, 2.5f, BALLC[k % 6]}); break;
            case EV_SPLASH: Cue("imp.stone", e.at, 0.7f); for (int k = 0; k < 18; k++) S.fx.push_back({1, e.at, {GetRandomValue(-30, 30) * 0.1f, GetRandomValue(10, 40) * 0.1f, GetRandomValue(-30, 30) * 0.1f}, 0, 0.5f, {120, 230, 120, 255}}); break;
            case EV_ROUND: if (e.who >= 0 && e.a > 0) { S.bannerT = 3; S.banner = std::string(TEAM_NAME[e.who & 1]) + " take the round"; S.bannerCol = TEAM[e.who & 1]; } else if (e.a == 0 && e.who < 0) { S.bannerT = 1.5f; S.banner = "GO!"; S.bannerCol = {255, 230, 100, 255}; Cue("mus.bell", S.cam.position, 0.6f); } break;
            case EV_SLIDE: Cue("arc.wave", e.at, mine ? 0.7f : 0.3f); break;
            case EV_CLIMB: break;
            case EV_LAND: if (mine) { if (e.a < 0) Cue("arc.shell", e.at, 0.6f); else Cue("exp.step.stone", e.at, 0.5f); } break;
            case EV_RELOAD: if (mine) Cue("hub.latch", S.cam.position, 0.4f); break;
            case EV_DRY: if (mine) { Cue("ui.error", S.cam.position, 0.5f); S.pops.push_back({"Out of darts: hold Q to vacuum", {255, 200, 120, 255}, S.t}); } break;
            default: break;
        }
    }
    while (S.feed.size() > 6) S.feed.pop_front();
    while (!S.feed.empty() && S.t - S.feed.front().t > 7) S.feed.pop_front();
    S.pops.erase(std::remove_if(S.pops.begin(), S.pops.end(), [](const Pop& p) { return S.t - p.t > 1.6f; }), S.pops.end());
}

// ---------------------------------------------------------------- input
void Gather() {
    Player& p = Me(); Input in; const Config& C = Cfg();
    bool menu = S.storeOpen || S.loadout;
    Vector2 md = MouseLook(!S.shot && !menu && W().phase != PH_OVER);
    float sens = 0.0026f * (p.in.aim ? 0.6f : 1.0f);
    S.camYaw += md.x * sens; S.camPitch = std::clamp(S.camPitch - md.y * sens, -1.45f, 1.45f);
    S.swayX = std::clamp(S.swayX * 0.85f - md.x * 0.00008f, -0.03f, 0.03f); S.swayY = std::clamp(S.swayY * 0.85f + md.y * 0.00008f, -0.03f, 0.03f);
    in.yaw = S.camYaw; in.pitch = S.camPitch;
    if (!menu) {
        in.moveX = (IsKeyDown(KEY_W) ? 1.0f : 0) - (IsKeyDown(KEY_S) ? 1.0f : 0); in.moveZ = (IsKeyDown(KEY_D) ? 1.0f : 0) - (IsKeyDown(KEY_A) ? 1.0f : 0);
        in.jump = IsKeyPressed(KEY_SPACE); in.crouch = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_C);
        in.fire = IsMouseButtonDown(MOUSE_BUTTON_LEFT); in.aim = IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
        in.knife = IsKeyPressed(KEY_V) || IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE); in.reload = IsKeyPressed(KEY_R); in.use = IsKeyPressed(KEY_E);
        in.vacuum = IsKeyDown(KEY_Q); in.grab = IsKeyPressed(KEY_F) || IsKeyDown(KEY_F);
        if (IsKeyPressed(KEY_ONE)) in.slot = 0; if (IsKeyPressed(KEY_TWO)) in.slot = 1;
        float wheel = GetMouseWheelMove(); if (wheel != 0 && p.gun < G_COUNT) in.slot = 1 - p.wield;
        // the three chosen rewards on 4, 5, 6
        int k = 0; for (int r = 0; r < 6; r++) if ((p.rewardPick >> r) & 1) { if ((k == 0 && IsKeyPressed(KEY_FOUR)) || (k == 1 && IsKeyPressed(KEY_FIVE)) || (k == 2 && IsKeyPressed(KEY_SIX))) in.reward = r; k++; }
        // joke items: T picks one you own, G uses it
        if (IsKeyPressed(KEY_T)) { for (int s = 1; s <= (int)C.jokes.size(); s++) { int j = (S.jokeSel + s) % (int)C.jokes.size(); if (p.jokes[j] > 0) { S.jokeSel = j; break; } } }
        if (IsKeyPressed(KEY_G) && p.jokes[S.jokeSel] > 0) in.joke = S.jokeSel;
    }
    if (IsKeyPressed(KEY_B) && p.inStore) S.storeOpen = !S.storeOpen;
    if (!p.inStore) S.storeOpen = false;
    S.scoreboard = IsKeyDown(KEY_TAB);
    if (IsKeyPressed(KEY_H)) S.help = !S.help;
    if (IsKeyPressed(KEY_L) && (!p.alive || W().phase == PH_WARMUP)) S.loadout = !S.loadout;
    p.in = in;
}

// ---------------------------------------------------------------- the HUD
void Bar(float x, float y, float w, float h, float k, Color c, Color bg = {20, 24, 34, 200}) { DrawRectangleRounded({x, y, w, h}, 0.4f, 6, bg); if (k > 0) DrawRectangleRounded({x + 2, y + 2, (w - 4) * std::clamp(k, 0.0f, 1.0f), h - 4}, 0.4f, 6, c); }
void DrawStore(Game& g) {
    const Config& C = Cfg(); Player& p = Me(); float cx = SCREEN_W / 2.0f;
    Rectangle r{cx - 420, 64, 840, 600}; DrawRectangleRounded(r, 0.04f, 8, Color{18, 22, 36, 236}); DrawRectangleRoundedLinesEx(r, 0.04f, 8, 3, {250, 206, 42, 255});
    DrawTextCenteredBold("THE STORE", cx, r.y + 12, 30, {250, 206, 42, 255});
    DrawTextCentered(TextFormat("Score to spend: %d     (your leaderboard total stays %d)     B closes", p.cash, p.score), cx, r.y + 48, 15, {210, 220, 236, 255});
    if (p.storeT < C.storeSafe) DrawTextCentered(TextFormat("Safe for %.1f s", C.storeSafe - p.storeT), cx, r.y + 68, 14, {140, 255, 160, 255});
    auto buy = [&](int code) { Input& in = p.in; in.buy = code; };
    float y = r.y + 88;
    for (int gi = 1; gi < G_COUNT; gi++) {
        const GunDef& gd = C.guns[gi]; float x = r.x + 20 + ((gi - 1) % 2) * 410, yy = y + ((gi - 1) / 2) * 58;
        bool own = p.gun == gi, can = p.cash >= gd.cost && !own;
        Rectangle b{x, yy, 396, 52}; bool hov = CheckCollisionPointRec(GetMousePosition(), b);
        DrawRectangleRounded(b, 0.2f, 6, own ? Color{40, 90, 60, 255} : hov && can ? Color{60, 70, 110, 255} : Color{32, 38, 58, 255});
        TxtBold(gd.name, x + 10, yy + 6, 17, can || own ? WHITE : Color{150, 150, 160, 255});
        Txt(TextFormat("%s   %g dmg%s   mag %d   %s", own ? "OWNED" : TextFormat("%d", gd.cost), gd.dmg, gd.pellets > 1 ? TextFormat(" x%d", gd.pellets) : "", gd.mag, gd.role.c_str()), x + 10, yy + 28, 12, own ? Color{150, 255, 170, 255} : can ? Color{250, 210, 90, 255} : Color{150, 150, 160, 255});
        if (hov && can && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) buy(gi);
    }
    float y2 = y + 5 * 58 + 4;
    if (Button({r.x + 20, y2, 250, 36}, TextFormat("Darts x%d  (%d)", C.dartPack, C.dartPackCost), p.cash >= C.dartPackCost && p.reserve < C.dartMax, 15)) buy(100);
    if (W().mode == MD_BOMB && p.team != W().attackers && Button({r.x + 290, y2, 250, 36}, TextFormat("Disarm kit  (%d)", C.disarmKit), p.cash >= C.disarmKit && !p.kit, 15)) buy(101);
    TxtBold("Joke shelf (harmless: T picks, G uses)", r.x + 20, y2 + 46, 15, {240, 160, 210, 255});
    for (int j = 0; j < (int)C.jokes.size(); j++) {
        float x = r.x + 20 + (j % 4) * 200, yy = y2 + 68 + (j / 4) * 38;
        if (Button({x, yy, 190, 32}, TextFormat("%s %d%s", C.jokes[j].name.c_str(), C.jokes[j].cost, p.jokes[j] ? TextFormat(" (x%d)", p.jokes[j]) : ""), p.cash >= C.jokes[j].cost, 12)) buy(200 + j);
    }
    (void)g;
}
void DrawLoadout() {
    const Config& C = Cfg(); Player& p = Me(); float cx = SCREEN_W / 2.0f;
    Rectangle r{cx - 360, 120, 720, 420}; DrawRectangleRounded(r, 0.05f, 8, Color{18, 22, 36, 236}); DrawRectangleRoundedLinesEx(r, 0.05f, 8, 3, {250, 200, 80, 255});
    DrawTextCenteredBold("STREAK REWARDS: pick three", cx, r.y + 12, 26, {250, 200, 80, 255});
    DrawTextCentered("Score earned without being knocked out unlocks each once per life (L closes)", cx, r.y + 46, 14, {210, 220, 236, 255});
    static int pick = -1; if (pick < 0) pick = p.rewardPick;
    for (int i = 0; i < 6; i++) {
        const RewardDef& rd = C.rewards[i]; Rectangle b{r.x + 20, r.y + 74 + i * 52.0f, 680, 46}; bool on = (pick >> i) & 1, hov = CheckCollisionPointRec(GetMousePosition(), b);
        DrawRectangleRounded(b, 0.2f, 6, on ? Color{70, 60, 30, 255} : hov ? Color{40, 46, 70, 255} : Color{30, 34, 52, 255});
        TxtBold(TextFormat("%s  (%d)", rd.name.c_str(), rd.streak), b.x + 10, b.y + 4, 16, on ? Color{255, 220, 120, 255} : WHITE);
        DrawWrapped(rd.text, {b.x + 10, b.y + 24, 660, 20}, 11, {190, 200, 220, 255});
        if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { int bits = pick ^ (1 << i); int n = 0; for (int k = 0; k < 6; k++) n += (bits >> k) & 1; if (n <= 3) pick = bits; }
    }
    int n = 0; for (int k = 0; k < 6; k++) n += (pick >> k) & 1;
    if (Button({cx - 100, r.y + r.height - 50, 200, 36}, n == 3 ? "Use these three" : "Pick three", n == 3, 15)) { p.in.pickRewards = pick; S.loadout = false; }
}
void DrawScoreboard() {
    const World& w = W(); float cx = SCREEN_W / 2.0f; Rectangle r{cx - 380, w.phase == PH_OVER ? 190.0f : 110.0f, 760, 70 + 24.0f * w.players.size()};
    DrawRectangleRounded(r, 0.04f, 8, Color{12, 16, 26, 225});
    DrawTextCenteredBold(ModeName(w.mode), cx, r.y + 10, 24, WHITE);
    std::vector<int> ord; for (const auto& p : w.players) ord.push_back(p.id);
    std::sort(ord.begin(), ord.end(), [&](int a, int b) { const Player& A = w.players[a]; const Player& B = w.players[b]; if (w.mode != MD_FFA && A.team != B.team) return A.team < B.team; return A.score > B.score; });
    Txt("player", r.x + 20, r.y + 42, 13, {160, 170, 190, 255}); Txt("score   KOs   down   caps   gun", r.x + 330, r.y + 42, 13, {160, 170, 190, 255});
    for (size_t i = 0; i < ord.size(); i++) { const Player& p = w.players[ord[i]]; float y = r.y + 62 + i * 24; Color c = p.id == S.me ? WHITE : TeamCol(p);
        Txt(p.name + (p.bot ? "  (bot)" : ""), r.x + 20, y, 15, c); Txt(TextFormat("%5d  %4d  %5d  %5d   %s", p.score, p.kos, p.deaths, p.caps, p.gun < G_COUNT ? Cfg().guns[p.gun].name.c_str() : "starter"), r.x + 330, y, 15, c); }
}
void DrawHud(Game& g) {
    const Config& C = Cfg(); World& w = W(); Player& p = Me();
    float cx = SCREEN_W / 2.0f, cy = SCREEN_H / 2.0f;
    // the peek out of the balls when submerged: dark all round, a narrow slit
    if (p.alive && p.submerged) {
        DrawRectangle(0, 0, SCREEN_W, (int)cy - 26, Color{16, 22, 50, 255}); DrawRectangle(0, (int)cy + 26, SCREEN_W, SCREEN_H, Color{16, 22, 50, 255});
        uint32_t rr = 12345; auto rn = [&]() { rr ^= rr << 13; rr ^= rr >> 17; rr ^= rr << 5; return (rr & 0xFFFF) / 65535.0f; };
        for (int k = 0; k < 520; k++) { float x = rn() * (SCREEN_W + 120) - 60, y = rn() * SCREEN_H; float gap = fabsf(y - cy); if (gap < 26 + rn() * 30) continue; float rad = 34 + rn() * 26 + (gap > 200 ? 20 : 0); Color c = BALLC[(int)(rn() * 6) % 6]; float wob = sinf(S.t * 1.5f + k) * 2;
            DrawCircleV({x + wob, y}, rad, Shade(c, 0.55f)); DrawCircleV({x + wob - rad * 0.12f, y - rad * 0.12f}, rad * 0.86f, c); DrawCircleV({x + wob - rad * 0.35f, y - rad * 0.38f}, rad * 0.22f, Mix(c, WHITE, 0.55f)); }
        DrawTextCentered("submerged: hidden, and darts can't reach you (stand up to fight)", cx, SCREEN_H - 120.0f, 15, {230, 236, 255, 255}); }
    if (p.alive && p.po == PO_SLIDE) { for (int k = 0; k < 12; k++) { float a = S.t * 6 + k * 0.5f; DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{0, 0, 0, 0}); DrawLineEx({cx + cosf(a) * 260, cy + sinf(a) * 180}, {cx + cosf(a) * 640, cy + sinf(a) * 420}, 6, Fade(BALLC[k % 6], 0.35f)); } DrawTextCenteredBold("WHEEE", cx, cy - 160, 30, Fade(WHITE, 0.6f)); }
    if (S.flash > 0) DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{255, 60, 40, (unsigned char)(110 * S.flash)});
    if (S.hurtT > 0) { float rel = S.hurtYaw - S.camYaw; Vector2 d{sinf(rel), -cosf(rel)}; DrawCircleSector({cx + d.x * 150, cy + d.y * 150}, 26, atan2f(d.y, d.x) * RAD2DEG - 30, atan2f(d.y, d.x) * RAD2DEG + 30, 8, Fade({255, 70, 50, 255}, S.hurtT)); }
    // the mode's scoreboard at the top
    Rectangle sb{cx - 230, 8, 460, 54}; DrawRectangleRounded(sb, 0.3f, 6, Color{14, 18, 28, 210});
    const ModeDef& md = C.modes[w.mode];
    float left = w.phase == PH_PLAY ? std::max(0.0f, md.timeLimit - w.phaseT) : md.timeLimit;
    if (w.mode == MD_FFA) { int lead = w.Leader(); DrawTextCenteredBold(TextFormat("You %d   |   Leader %s %d   (to %d)", p.kos, lead >= 0 ? w.players[lead].name.c_str() : "-", lead >= 0 ? w.players[lead].kos : 0, md.toWin), cx, 14, 18, WHITE); }
    else {
        int a = w.mode == MD_TDM ? w.teamKOs[0] : w.mode == MD_CTF ? w.caps[0] : w.roundWins[0], b = w.mode == MD_TDM ? w.teamKOs[1] : w.mode == MD_CTF ? w.caps[1] : w.roundWins[1];
        TxtBold(TextFormat("%d", a), cx - 120 - MeasureTxt(TextFormat("%d", a), 30, true), 12, 30, TEAM[0]); TxtBold(TextFormat("%d", b), cx + 120, 12, 30, TEAM[1]);
        DrawTextCentered(TextFormat("to %d", md.toWin), cx, 42, 12, {170, 180, 200, 255});
        if (w.mode == MD_BOMB) DrawTextCentered(TextFormat("%s attack", TEAM_NAME[w.attackers]), cx, 42, 12, TEAM[w.attackers]);
    }
    if (w.mode == MD_BOMB) left = w.bomb.planted ? w.bomb.fuseT : std::max(0.0f, C.roundTime - (w.phase == PH_PLAY ? w.phaseT : 0));
    DrawTextCenteredBold(TextFormat("%d:%02d", (int)left / 60, (int)left % 60), cx, w.mode == MD_FFA ? 38 : 16, 20, w.mode == MD_BOMB && w.bomb.planted ? Color{255, 90, 70, 255} : WHITE);
    // the feed (top right)
    for (size_t i = 0; i < S.feed.size(); i++) { const Feed& f = S.feed[i]; Txt(f.text, SCREEN_W - 20 - MeasureTxt(f.text, 14), 74 + i * 20, 14, Fade(f.c, std::min(1.0f, (7 - (S.t - f.t)) / 1.5f))); }
    if (S.dadT > 0) { Rectangle d{cx - 300, 72, 600, 46}; DrawRectangleRounded(d, 0.3f, 6, Color{255, 250, 220, 230}); DrawWrapped(S.dadLine, {d.x + 12, d.y + 6, 576, 40}, 14, {60, 40, 20, 255}); }
    if (S.bannerT > 0) DrawTextCenteredBold(S.banner, cx, cy - 150, 46, Fade(S.bannerCol, std::min(1.0f, S.bannerT)));
    if (w.phase == PH_WARMUP) { DrawTextCenteredBold(TextFormat("%d", (int)ceilf(3 - w.phaseT)), cx, cy - 120, 60, {255, 230, 100, 255}); DrawTextCentered(TextFormat("%s: %s", ModeName(w.mode), md.goal.c_str()), cx, cy - 50, 16, WHITE); DrawTextCentered("L: pick your three streak rewards", cx, cy - 26, 14, {200, 210, 230, 255}); }
    for (size_t i = 0; i < S.pops.size(); i++) { float a = S.t - S.pops[i].t; DrawTextCenteredBold(S.pops[i].text, cx, cy + 60 + i * 28 - a * 24, 22, Fade(S.pops[i].col, 1 - a / 1.6f)); }
    if (w.phase == PH_OVER) {
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{0, 0, 0, 150});
        std::string who = w.mode == MD_FFA ? (w.winner >= 0 ? w.players[w.winner].name + " wins" : "A draw") : w.winner >= 0 ? std::string(TEAM_NAME[w.winner]) + " win" : "A draw";
        bool won = w.mode == MD_FFA ? w.winner == S.me : w.winner == p.team;
        DrawTextCenteredBold(won ? "YOU WIN!" : who, cx, 120, 50, won ? Color{255, 220, 90, 255} : WHITE);
        DrawScoreboard();
        if (S.net) {
            if (S.net->role == arcade::R_HOST) { if (Button({cx - 230, SCREEN_H - 90.0f, 200, 40}, "Rematch", true, 16)) { std::string why; S.net->Rematch(&why); return; } }
            else DrawTextCentered("Waiting for the host: a rematch, or back to the lobby", cx, SCREEN_H - 80.0f, 15, {200, 210, 230, 255});
            if (Button({cx + 30, SCREEN_H - 90.0f, 200, 40}, S.net->role == arcade::R_HOST ? "Back to the lobby" : "Leave", true, 16)) { LeaveBallPit(g); return; }
            return;
        }
        if (Button({cx - 230, SCREEN_H - 90.0f, 200, 40}, "Again", true, 16)) { StartBallPit(g, S.mode, S.players, S.skill); return; }
        if (Button({cx + 30, SCREEN_H - 90.0f, 200, 40}, "Back to the arcade", true, 16)) { LeaveBallPit(g); return; }
        return;
    }
    if (S.scoreboard) DrawScoreboard();
    if (!p.alive) {
        DrawRectangle(0, (int)cy - 70, SCREEN_W, 120, Color{0, 0, 0, 120});
        DrawTextCenteredBold("KNOCKED OUT", cx, cy - 60, 36, {255, 120, 100, 255});
        if (w.noRespawn) DrawTextCentered("No respawns this round: watch your team", cx, cy - 18, 16, WHITE);
        else DrawTextCentered(TextFormat("Back in %.1f s   (L: change your streak rewards)", std::max(0.0f, p.respawnT)), cx, cy - 18, 16, WHITE);
        if (S.loadout) DrawLoadout();
        return;
    }
    if (S.loadout) { DrawLoadout(); return; }
    // the crosshair (spread grows on the move, in the air and on the bridges), the hit marker
    {
        const GunDef& gd = w.Gun(p); float sp = gd.spread * (p.in.aim && gd.ads ? 0.45f : 1) * (p.onBridge ? C.bridgeSpread : 1) * (p.grounded ? 1 : 1.6f) * (Vector3Length({p.vel.x, 0, p.vel.z}) > 1 ? 1.25f : 1);
        float gap = 6 + sp * 7; Color cc = p.inTunnel ? Color{255, 200, 120, 255} : WHITE;
        if (p.po != PO_CANNON) for (int k = 0; k < 4; k++) { float a = k * PI / 2; DrawLineEx({cx + cosf(a) * gap, cy + sinf(a) * gap}, {cx + cosf(a) * (gap + 8), cy + sinf(a) * (gap + 8)}, 2, cc); }
        else DrawCircleLines((int)cx, (int)cy, 16, {255, 220, 120, 255});
        if (S.hitMark > 0) for (int k = 0; k < 4; k++) { float a = PI / 4 + k * PI / 2; DrawLineEx({cx + cosf(a) * 6, cy + sinf(a) * 6}, {cx + cosf(a) * 14, cy + sinf(a) * 14}, 3, {255, 230, 90, 255}); }
    }
    // health, ammo, the gun
    Bar(30, SCREEN_H - 50.0f, 260, 24, p.hp / C.health, p.hp > 35 ? Color{120, 230, 120, 255} : Color{255, 90, 70, 255});
    TxtBold(TextFormat("%d", (int)ceilf(p.hp)), 300, SCREEN_H - 52.0f, 24, WHITE);
    const GunDef& gd = w.Gun(p); int slot = p.wield == 1 && p.gun < G_COUNT ? 1 : 0;
    std::string gunLine = p.ball >= 0 ? std::string("BALL: one hit knocks out (click to throw)") : p.inTunnel ? std::string("Foam knife (tunnels: knife only)") : p.vacuuming ? std::string("Vacuuming darts...") : gd.name;
    TxtBold(gunLine, SCREEN_W - 30 - MeasureTxt(gunLine, 18, true), SCREEN_H - 84.0f, 18, p.ball >= 0 ? Color{255, 200, 120, 255} : WHITE);
    std::string am = TextFormat("%d  /  %d", p.mag[slot], p.reserve); TxtBold(am, SCREEN_W - 30 - MeasureTxt(am, 30, true), SCREEN_H - 58.0f, 30, p.mag[slot] == 0 ? Color{255, 120, 90, 255} : WHITE);
    if (p.reloadT > 0) { Bar(cx - 60, cy + 36, 120, 8, 1 - p.reloadT / gd.reload, {250, 210, 90, 255}); DrawTextCentered("reloading", cx, cy + 46, 13, {250, 210, 90, 255}); }
    if (gd.spinUp > 0 && p.spin > 0 && p.spin < gd.spinUp) Bar(cx - 60, cy + 36, 120, 8, p.spin / gd.spinUp, {120, 200, 255, 255});
    if (p.grabT > 0) Bar(cx - 60, cy + 36, 120, 8, p.grabT / C.grabTime, {120, 255, 160, 255});
    // the wallet and the streak with the three rewards (4, 5, 6)
    TxtBold(TextFormat("Score %d   spend %d", p.score, p.cash), 30, SCREEN_H - 84.0f, 16, {250, 210, 90, 255});
    { int k = 0; float x = 30; for (int r = 0; r < 6; r++) if ((p.rewardPick >> r) & 1) {
        const RewardDef& rd = C.rewards[r]; bool ready = (p.rewardReady >> r) & 1, used = (p.rewardUsed >> r) & 1;
        Rectangle b{x + k * 116.0f, SCREEN_H - 140.0f, 110, 46}; DrawRectangleRounded(b, 0.2f, 6, ready && !used ? Color{90, 70, 20, 230} : Color{20, 24, 36, 200});
        Bar(b.x + 4, b.y + 34, 102, 8, used ? 0 : (float)p.streak / rd.streak, ready && !used ? Color{255, 210, 90, 255} : Color{140, 150, 170, 255});
        TxtBold(TextFormat("%d", 4 + k), b.x + 6, b.y + 4, 15, ready && !used ? Color{255, 220, 120, 255} : Color{160, 170, 190, 255});
        DrawWrapped(rd.name, {b.x + 22, b.y + 3, 86, 30}, 11, used ? Color{110, 110, 120, 255} : WHITE); k++; } }
    if (p.jokes[S.jokeSel] > 0) TxtBold(TextFormat("G: %s (x%d)%s", C.jokes[S.jokeSel].name.c_str(), p.jokes[S.jokeSel], p.jokeCool > 0 ? TextFormat("  %.0fs", p.jokeCool) : ""), 30, SCREEN_H - 168.0f, 14, {240, 160, 210, 255});
    // prompts
    auto prompt = [&](const std::string& s) { DrawTextCenteredBold(s, cx, cy + 90, 17, {255, 240, 200, 255}); };
    if (p.po == PO_CANNON) {
        const Cannon& k = w.cannons[p.cannon];
        Bar(cx - 120, SCREEN_H - 120.0f, 240, 14, k.overheated ? k.coolT / C.cannonCool : k.heat / C.cannonHeat, k.overheated ? Color{255, 90, 60, 255} : Color{255, 200, 90, 255});
        DrawTextCentered(TextFormat("CANNON   hopper %d / %d   %s   (E or Space: get off)", k.hopper, C.hopperMax, k.overheated ? "OVERHEATED" : p.mountT < C.cannonMount ? "mounting..." : "hold LMB"), cx, SCREEN_H - 100.0f, 15, WHITE);
    } else if (p.inStore && !S.storeOpen) prompt("B: open the store");
    else if (w.CannonNear(p.pos) >= 0 && w.cannons[w.CannonNear(p.pos)].op < 0) prompt(TextFormat("E: get on the cannon (%d balls)", w.cannons[w.CannonNear(p.pos)].hopper));
    else if (p.ball < 0 && w.PitAt(p.pos, 0.8f) >= 0 && p.grabT <= 0) prompt("F: grab a ball");
    else if (w.mode == MD_BOMB && p.bomb) { for (const auto& s : w.arena.bombSites) if (s.team != p.team && Vector3Distance(s.p, p.pos) < 1.6f) prompt(p.plantT > 0 ? TextFormat("Planting... %.1f", C.plantTime - p.plantT) : "E (hold still): plant the bomb"); }
    else if (w.mode == MD_BOMB && w.bomb.planted && p.team != w.attackers && Vector3Distance(w.bomb.p, p.pos) < 1.6f) prompt(p.defuseT > 0 ? TextFormat("Disarming... %.1f", (p.kit ? C.defuseKit : C.defuseTime) - p.defuseT) : "E (hold still): disarm");
    if (p.carry >= 0) DrawTextCenteredBold("YOU HAVE THE FLAG: take it to your own roof", cx, 70, 18, TEAM[p.carry & 1]);
    if (p.drive >= 0) DrawTextCenteredBold(p.drive < (int)w.ents.size() && w.ents[p.drive].kind == E_TANK ? TextFormat("DART TANK   armor %d   (%.0f s)", (int)w.ents[p.drive].armor, w.ents[p.drive].life) : TextFormat("RC CAR   %d health   (%.0f s)   E: back to your body", (int)(p.drive < (int)w.ents.size() ? w.ents[p.drive].hp : 0), p.drive < (int)w.ents.size() ? w.ents[p.drive].life : 0.0f), cx, 70, 18, {255, 210, 90, 255});
    if (S.storeOpen) DrawStore(g);
    if (S.help && !S.storeOpen) {
        Rectangle r{SCREEN_W - 316.0f, 214, 300, 286}; DrawRectangleRounded(r, 0.06f, 6, Color{10, 14, 22, 200});
        const char* L[] = {"WASD move, Space jump, Ctrl crouch", "LMB fire / throw a ball, RMB aim", "1 / 2 (or the wheel): your guns", "R reload, V knife (from behind: KO)", "Q hold: vacuum up foam darts", "F grab a ball (any live ball KOs)", "E cannon, slide, plant, disarm", "B the store (at your base)", "4 5 6 streak rewards, L pick them", "T / G joke items, Tab scores", "Crouch in a pit: hide under the balls", "H hides this"};
        for (int i = 0; i < 12; i++) Txt(L[i], r.x + 12, r.y + 10 + i * 22, 14, i < 9 ? WHITE : Color{200, 210, 230, 255});
    }
}

void StepCamera(float dt) {
    World& w = W(); Player& p = Me();
    if (S.freeCam) return;
    S.recoil = std::max(0.0f, S.recoil - dt * 6); S.kick = std::max(0.0f, S.kick - dt); S.hitMark = std::max(0.0f, S.hitMark - dt); S.hurtT = std::max(0.0f, S.hurtT - dt); S.dadT = std::max(0.0f, S.dadT - dt);
    float spd = Vector3Length({p.vel.x, 0, p.vel.z}); if (p.grounded) S.bob += dt * spd * 1.8f;
    if (p.po == PO_DRIVE && p.drive >= 0 && p.drive < (int)w.ents.size()) {
        const Ent& e = w.ents[p.drive];
        if (e.kind == E_RCCAR) S.cam.position = Vector3Add(e.p, {-cosf(S.camYaw) * 0.9f, 0.55f, -sinf(S.camYaw) * 0.9f});
        else S.cam.position = Vector3Add(e.p, {-cosf(S.camYaw) * 3.2f, 2.4f, -sinf(S.camYaw) * 3.2f});
    } else if (!p.alive) {
        // knocked out: a slow orbit above where you fell
        S.cam.position = Vector3Lerp(S.cam.position, Vector3Add(p.pos, {cosf(S.t * 0.3f) * 4, 3.5f, sinf(S.t * 0.3f) * 4}), std::min(1.0f, dt * 2));
        Vector3 d = Vector3Subtract(Vector3Add(p.pos, {0, 0.8f, 0}), S.cam.position); S.camYaw = atan2f(d.z, d.x); S.camPitch = atan2f(d.y, sqrtf(d.x * d.x + d.z * d.z));
    } else {
        Vector3 eye = p.Eye();
        if (p.po == PO_SLIDE) { eye.y += 0.05f; }
        if (S.hostW == nullptr && S.net && S.me < (int)S.smooth.size()) {}
        S.cam.position = eye;
        if (p.onBridge) S.cam.position.y += sinf(S.t * 5) * 0.03f;   // (the bridge sways)
    }
    if (p.po == PO_CANNON) { S.camYaw = p.yaw; S.camPitch = p.pitch; }
    if (S.shake > 0) { S.cam.position.x += sinf(S.t * 70) * 0.04f * S.shake; S.cam.position.y += cosf(S.t * 61) * 0.04f * S.shake; S.shake = std::max(0.0f, S.shake - dt); }
    Vector3 look{cosf(S.camPitch) * cosf(S.camYaw), sinf(S.camPitch), cosf(S.camPitch) * sinf(S.camYaw)};
    S.cam.target = Vector3Add(S.cam.position, look); S.cam.up = {0, 1, 0};
    float wantFov = p.in.aim && w.Gun(p).ads && p.alive && p.ball < 0 ? (w.Gun(p).key == "long" ? 30.0f : 58.0f) : p.po == PO_SLIDE ? 92.0f : 78.0f;
    S.fov += (wantFov - S.fov) * std::min(1.0f, dt * 12); S.cam.fovy = S.fov; S.cam.projection = CAMERA_PERSPECTIVE;
    S.flash = std::max(0.0f, S.flash - dt * 2); S.bannerT = std::max(0.0f, S.bannerT - dt);
}
void Render(float dt) {
    rt::SceneLight L;
    L.fog = {200, 210, 226, 255}; L.fogDensity = 0.004f; L.fill = {150, 160, 180, 255}; L.rim = {200, 220, 255, 255}; L.key = {255, 248, 236, 255};
    L.surfaceY = 1e5f; L.time = S.t;
    L.moonDir = Vector3Normalize({0.25f, -1, 0.35f}); L.moon = {255, 250, 240, 255}; L.moonK = 0.55f;
    L.ambK = 0.95f; L.skyAmb = {210, 214, 226, 255}; L.seaAmb = {120, 110, 104, 255};
    L.outline = 0.7f; L.outlineTint = {20, 24, 40, 255}; L.stipple = 0.15f; L.grain = 0.2f; L.aoK = 0.45f; L.aoRadius = 0.5f; L.filmic = 0.0f; L.saturation = 1.2f;
    const Arena& a = W().arena;
    for (int i = -1; i <= 1; i++) for (int j = -1; j <= 1; j += 2) L.AddPoint({i * 18.0f, a.ceil - 1.5f, j * 6.0f}, 22, {255, 246, 230, 255}, 0.45f);
    rt::ApplyGameQuality();
    rt::RenderBegin(S.cam, L);
    rt::DrawStatic(HallModel(), MatrixIdentity(), WHITE);
    // the ceiling lights and the high windows of daylight
    for (int i = -4; i <= 4; i++) for (int j = -2; j <= 2; j++) { Vector3 c{i * 6.0f + 3, a.ceil - 0.06f, j * 5.0f}; rt::DrawCubeGlow(MatrixMultiply(MatrixScale(1.6f, 0.06f, 0.4f), MatrixTranslate(c.x, c.y, c.z)), {255, 250, 236, 255}, 1.4f); }
    for (int s = -1; s <= 1; s += 2) for (int k = -6; k <= 6; k++) rt::DrawCubeGlow(MatrixMultiply(MatrixScale(3.4f, 1.2f, 0.04f), MatrixTranslate(k * 4.4f, a.ceil - 2.2f, s * (a.halfZ - 0.03f))), {210, 230, 250, 255}, 0.7f);
    DrawPits(); DrawConveyor(); DrawCannons(); DrawPlayers(); DrawEnts(); DrawBallsAndDarts(); DrawFx(dt);
    // a banner over each tower in the team's colour
    for (int s = 0; s < 2; s++) rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.06f, 2.0f, 8.0f), MatrixTranslate((s ? 1 : -1) * (a.halfX - 0.1f), a.ceil - 4.5f, 0)), TEAM[s], 0.5f);
    DrawViewmodel();
    rt::RenderEnd();
}
}  // namespace

int gBallPitMode = MD_TDM, gBallPitFill = 8;
void StartBallPit(Game& g, int mode, int players, int skill) {
    S = BPScene{}; S.active = true; S.mode = std::clamp(mode, 0, MD_COUNT - 1); S.players = std::clamp(players, 2, S.mode == MD_BOMB ? 10 : 12); S.skill = std::clamp(skill, 0, 2);
    W().Init(S.mode, S.players, (uint32_t)GetRandomValue(1, 1 << 30));
    for (auto& p : W().players) { p.name = p.id == 0 ? "You" : BOT_NAMES[p.id % 12]; p.bot = p.id != 0; }
    S.botRng.resize(W().players.size()); for (size_t i = 0; i < S.botRng.size(); i++) S.botRng[i] = 99991u * (uint32_t)(i + 1) + (uint32_t)GetRandomValue(0, 1 << 20);
    S.camYaw = Me().yaw; S.camPitch = 0; S.evCountSeen = W().evCount;
    g.scene = Scene::BallPit;
}
void StartBallPitNet(Game& g, arcade::Session* net, const char* name) {
    S = BPScene{}; S.active = true; S.net = net; S.netName = name ? name : "Player"; S.help = true;
    g.scene = Scene::BallPit;
}
void LeaveBallPit(Game& g) {
    if (S.net) { if (S.net->role == arcade::R_HOST) S.net->BackToLobby(); else S.net->Leave(); S.net = nullptr; }
    S.hostW = nullptr; S.active = false; MouseLook(false); g.scene = Scene::Arcade;
}
static bool NetFrame(Game& g, float dt) {
    arcade::Session& N = *S.net;
    N.Update(GetTime(), dt);
    if (N.stage != arcade::S_PLAYING) { S.net = nullptr; S.hostW = nullptr; S.active = false; MouseLook(false); g.scene = Scene::Arcade; return false; }
    int seat = N.MyPlayer();
    if (N.role == arcade::R_HOST) { S.hostW = BallPitHostWorld(N.HostGame()); S.me = BallPitSeatPlayer(N.HostGame(), seat); }
    else if (N.stateVersion != S.seenVersion && !N.Snapshot().empty()) {
        S.seenVersion = N.stateVersion;
        Reader r(N.Snapshot()); ReadWorld(r, S.Wm, &S.evTotal);
        S.me = seat >= 0 && seat < (int)S.Wm.players.size() ? seat : 0;
        if (S.smooth.size() != S.Wm.players.size()) { S.smooth.clear(); for (const auto& p : S.Wm.players) S.smooth.push_back(p.pos); }
    }
    if (W().players.empty() || S.me < 0) { ClearBackground(Color{20, 24, 36, 255}); DrawTextCenteredBold("Into the play centre...", SCREEN_W / 2.0f, SCREEN_H / 2.0f - 12, 24, WHITE); return false; }
    if (!S.helloSent) { Writer o; o.U8(1); o.Str(S.netName); N.Act(o); S.helloSent = true; S.camYaw = Me().yaw; S.evCountSeen = W().evCount; }
    Gather();
    Writer iw; iw.U8(0); WriteInput(Me().in, iw); N.Act(iw);
    if (N.role != arcade::R_HOST) {
        for (size_t i = 0; i < S.smooth.size() && i < S.Wm.players.size(); i++) { Vector3& sp = S.smooth[i]; Vector3 to = S.Wm.players[i].pos; if (Vector3Distance(sp, to) > 2.5f) sp = to; else sp = Vector3Lerp(sp, to, std::min(1.0f, dt * 18)); }
        for (auto& b : S.Wm.balls) if (b.st == BS_LIVE) b.p = Vector3Add(b.p, Vector3Scale(b.v, dt));
        for (auto& d : S.Wm.darts) if (d.live) d.p = Vector3Add(d.p, Vector3Scale(d.v, dt));
    }
    return true;
}
void SceneBallPit(Game& g) {
    if (!S.active) StartBallPit(g, MD_TDM, 8, 1);
    float dt = S.shot ? 1 / 60.0f : std::min(GetFrameTime(), 1 / 20.0f);
    S.t += dt;
    if (S.net) {
        if (!NetFrame(g, dt)) return;
        ReadEvents(); StepCamera(dt);
        std::vector<Vector3> real; bool guest = !S.hostW && S.smooth.size() == S.Wm.players.size();
        if (guest) for (size_t i = 0; i < S.Wm.players.size(); i++) { real.push_back(S.Wm.players[i].pos); if ((int)i != S.me) S.Wm.players[i].pos = S.smooth[i]; }
        Render(dt);
        if (guest) for (size_t i = 0; i < real.size(); i++) S.Wm.players[i].pos = real[i];
        DrawHud(g);
        return;
    }
    if (!S.shot) {
        Gather();
        S.acc += dt; Input mine = Me().in; bool first = true;
        while (S.acc >= STEP) {
            for (auto& p : W().players) if (p.id != S.me) BotInput(W(), p.id, p.in, S.botRng[p.id], S.skill);
            Me().in = mine;
            if (!first) { Input& m = Me().in; m.jump = m.knife = m.reload = m.use = false; m.slot = m.buy = m.joke = m.reward = m.pickRewards = -1; }
            W().Step(); S.acc -= STEP; first = false;
        }
    }
    ReadEvents();
    StepCamera(dt);
    Render(dt);
    DrawHud(g);
}
bool BallPitActive() { return S.active; }
// --shots: 0 the atrium from your base, 1 a firefight on the bridge, 2 a cannon, 3 the store, 4 submerged, 5 the slide,
// 6 the ground floor and the pits, 7 the match over, 8 the rewards (tank, kids, drone), 9 CTF carrying the flag, 10 the
// arena from above (a debug overview), 11 knocked out
void DebugBallPitShot(Game& g, int which) {
    StartBallPit(g, which == 9 ? MD_CTF : which == 12 ? MD_BOMB : MD_TDM, 10, 2); S.shot = true; S.help = which == 0;
    World& w = W(); std::vector<uint32_t> r(w.players.size(), 5); for (size_t i = 0; i < r.size(); i++) r[i] = 77 + (uint32_t)i * 13;
    auto run = [&](float secs) { for (int i = 0; i < (int)(secs / STEP) && w.phase != PH_OVER; i++) { for (auto& p : w.players) if (p.id != S.me) BotInput(w, p.id, p.in, r[p.id], 2); w.Step(); } };
    Player& m = Me();
    auto place = [&](Vector3 at, float yaw, float pitch) { m.pos = at; m.vel = {}; m.po = PO_STAND; m.yaw = yaw; m.pitch = pitch; m.in = Input{}; m.in.yaw = yaw; m.in.pitch = pitch; S.camYaw = yaw; S.camPitch = pitch; m.alive = true; };
    if (which == 0) { run(3.5f); place({-24.5f, L2, 3}, 0.02f, -0.05f); }
    if (which == 1) { run(14); place({-12, L3, 2.5f}, 0.0f, -0.05f); m.gun = G_BURST; m.wield = 1; m.mag[1] = 12; }
    if (which == 2) { run(10); int c = 0; place(w.CannonSeat(c), w.arena.cannons[c].yaw0, 0.05f); m.po = PO_CANNON; m.cannon = c; w.cannons[c].op = m.id; m.mountT = 1; }
    if (which == 3) { run(3.5f); place(w.arena.stores[0].p, -PI / 2, 0); m.cash = 1300; m.inStore = true; m.storeT = 1; S.storeOpen = true; }
    if (which == 4) { run(6); place({-5, 0, -6}, 0.3f, 0.05f); m.in.crouch = true; m.po = PO_CROUCH; m.submerged = true; m.inPit = true; }
    if (which == 5) { run(6); place(w.arena.slides[0].pts[3], PI, -0.2f); m.po = PO_SLIDE; m.slide = 0; m.slideS = 4; }
    if (which == 6) { run(8); place({-17, 0, -2}, 0.25f, 0.12f); m.gun = G_FLYWHEEL; m.wield = 1; m.mag[1] = 20; }
    if (which == 7) { run(3.5f); w.phase = PH_OVER; w.winner = 0; w.teamKOs[0] = 50; w.teamKOs[1] = 41; for (auto& p : w.players) { p.score = 300 + p.id * 70; p.kos = 3 + p.id % 5; } }
    if (which == 8) { run(4); for (int k = 0; k < 3; k++) { Player& q = w.players[2 + k * 2]; q.rewardReady = 63; q.rewardPick = 63; q.in = Input{}; q.in.reward = k == 0 ? 5 : k == 1 ? 2 : 4; q.pos = {-6.0f + k * 4, 0, 11}; } w.Step(); run(2); place({-14, L2 + 0.0f, 10.0f}, -0.3f, -0.32f); m.pos = {-12, 0.0f, 6.5f}; S.camYaw = 0.6f; S.camPitch = -0.05f; m.in.yaw = 0.6f; m.yaw = 0.6f; }
    if (which == 9) { run(6); place({-12, L3, 2.5f}, 0, -0.1f); m.carry = 1; w.flags[1].carrier = m.id; w.flags[1].home_ = false; }
    if (which == 10) { run(3.5f); place({-36, 22, 0}, 0, -0.55f); m.alive = true; }
    if (which == 11) { run(5); w.KnockOut(m, 1, "a ball", false); for (int i = 0; i < 120; i++) StepCamera(1 / 60.0f); }
    if (which == 12) { run(5); place({22, L2, 6}, PI, 0); }
    // (a debug overview flies the camera out over the hall)
    if (which == 10) { Me().alive = true; }
    S.evCountSeen = w.evCount;
    StepCamera(1 / 60.0f);
    if (which == 10) { S.cam.position = {-29, 13, -13}; S.cam.target = {0, 2, 2}; S.cam.fovy = 80; }
    for (int i = 0; i < 20; i++) StepCamera(1 / 60.0f);
    if (which == 10) { S.cam.position = {-29, 13, -13}; S.cam.target = {0, 2, 2}; S.cam.fovy = 80; S.freeCam = true; }
}
