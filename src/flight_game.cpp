// The Flight's scene (stage 1): the Founder flown in third person over the tropical island, fishing the lagoon.
// The simulation is flight.cpp (headless); this file feeds it input and draws it with Red Tide's inked renderer in
// daylight (design doc, "Presentation": Depth's inked low-poly in daylight, a thin ink line, silhouettes do the work).
#include "game.h"
#include "flight.h"
#include "flight_net.h"
#include "arcade_session.h"
#include "net.h"
#include "redtide_render.h"
#include "input.h"
#include "sound.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include "flight_costumes.h"

namespace {

struct FlightScene {
    bool active = false, shot = false;
    fl::World W;
    std::string founder = "taloned";
    float aimYaw = 0, aimPitch = 0;          // where the player steers (the camera looks this way)
    float camYaw = 0, camPitch = -0.2f;      // the camera's own, eased onto the aim
    Vector2 steer{0, 0};                      // the strike's talon aim (-1..1), from the mouse
    float t = 0, flapPh = 0, flapAmt = 0, fold = 0, flare = 0;
    bool help = true;
    float logT = 0; size_t logSeen = 0;
    Camera3D cam{};
    // models
    bool ready = false; int readyFor = -1;
    Model palm{}, nest{}, sea{};
    std::vector<Model> terr;                  // one terrain per island (the whole map), or the one island
    bool chart = false; float chartZoom = 1; Vector2 chartAt{0, 0}; int chartMode = 0; fl::Alt chartAlt = fl::Alt::Mid;   // the chart (M): zoom, centre, order mode (0 scout, 1 fishers), a scout's height
    fl::MapOpts opts;
    Model body{}, head{}, beak{}, tail{}, wingIn{}, wingOut{};
    Model egg{}, chick{}, pile{};
    bool dangerReady = false; Model krakenArm{}, krakenHead{}, ape{};   // (stage 7: the monsters)
    bool panel = false;                       // the colony panel (Tab)
    int page = 0, selFlock = -1; bool raidChicks = false;   // (the panel's page: 0 the colony, 1 the flocks; the flock picked for chart orders)
    int plat = 0;                             // (the panel's fledging-plan row picked)
    int bTo = 1, bGive[fl::G_COUNT] = {}, bGet[fl::G_COUNT] = {}; float bTruce = 0;   // (the barter offer being drafted)
    int pelicanIsle = -1, hoverTree = -1;
    bool seaReady = false;
    // a networked match (stage 5): the session; the host draws its real world, a guest its mirror of the snapshots
    arcade::Session* net = nullptr;
    fl::World* live = nullptr;
    int seenVersion = -1; float sinceSnap = 0; bool helloSent = false; size_t flocksSeen = 0;
    std::string netFounder = "taloned", netName = "Founder";
};
FlightScene S;
fl::World& WD() { return S.live ? *S.live : S.W; }
// every order the player gives (the panels, the chart, G) goes through the same path, solo or networked
void Order(const Writer& w) { if (S.net) S.net->Act(w); else fl::ApplyOrder(WD(), 0, w); }

constexpr int SN = 120; constexpr float SC = 24;  // the sea grid round the eye: 120 cells of 24 m (the haze hides its edge)

Color Mix(Color a, Color b, float k) { k = std::clamp(k, 0.0f, 1.0f); return {(unsigned char)(a.r + (b.r - a.r) * k), (unsigned char)(a.g + (b.g - a.g) * k), (unsigned char)(a.b + (b.b - a.b) * k), 255}; }
Color Shade(Color c, float k) { return {(unsigned char)std::clamp(c.r * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.g * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.b * k, 0.0f, 255.0f), c.a}; }
float Hash(float x, float z) { float h = sinf(x * 12.9898f + z * 78.233f) * 43758.5453f; return h - floorf(h); }
void TwoSided(rt::MeshBuilder& mb, Vector3 a, Vector3 b, Vector3 c, Color col) { mb.Tri(a, b, c, col); mb.Tri(a, c, b, col); }

// ---------------------------------------------------------------- models
void BuildTerrain(rt::MeshBuilder& mb, const fl::Island& is, int step = 1) {
    // an island as one mesh, coloured by its type: wet sand at the waterline, dry sand, grass, jungle inland, rock where
    // it's steep (the stack's chalk-white with guano, the volcano's black basalt), coral on the reef garden's flat;
    // under the water the sand darkens with depth (a lagoon's turquoise is the sand seen through the water)
    const Color wet{196, 178, 132, 255}, sand{236, 220, 170, 255}, grass{118, 168, 74, 255}, jungle{58, 120, 58, 255}, seabed{210, 200, 160, 255}, deep{60, 96, 104, 255};
    Color rock{150, 146, 136, 255};
    fl::IsleType ty = is.type;
    if (ty == fl::IsleType::Stack) rock = {214, 210, 198, 255};
    if (ty == fl::IsleType::Volcano || ty == fl::IsleType::KrakenCove) rock = {70, 64, 62, 255};
    auto col = [&](float x, float z) {
        float h = is.Height(x, z), n = is.Normal(x, z).y, j = Hash(floorf(x * 0.5f), floorf(z * 0.5f)) * 0.08f - 0.04f;
        float r = Vector2Distance({x, z}, {is.c.x, is.c.z});
        Color c;
        if (h < -0.2f) {
            c = Mix(seabed, deep, std::clamp((-h - 0.2f) / 11, 0.0f, 1.0f));
            if (ty == fl::IsleType::ReefGarden && r < 100) { float k = Hash(floorf(x / 3), floorf(z / 3)); c = k < 0.33f ? Color{214, 120, 120, 255} : k < 0.66f ? Color{224, 180, 90, 255} : Color{150, 190, 170, 255}; }
        }
        else if (h < 0.5f) c = wet;
        else if (h < 1.8f) c = sand;
        else if (h < 3.0f) c = Mix(sand, grass, (h - 1.8f) / 1.2f);
        else c = Mix(grass, jungle, std::clamp((h - 3) / 6, 0.0f, 1.0f));
        if (ty == fl::IsleType::Volcano && h > 0.5f) c = Mix(Color{96, 86, 74, 255}, rock, std::clamp(h / 40, 0.0f, 1.0f));
        if (ty == fl::IsleType::Volcano && r < 20 && h < 47.5f) c = {96, 170, 150, 255};   // (the crater lake's strange green water)
        if (ty == fl::IsleType::Town && h > 1.0f) c = Mix(grass, Color{196, 184, 150, 255}, 0.35f);
        if (h > 0.5f && n < 0.8f) c = Mix(c, rock, (0.8f - n) / 0.2f);
        return Shade(c, 1 + j);
    };
    for (int zi = 0; zi + step < is.n; zi += step) for (int xi = 0; xi + step < is.n; xi += step) {
        float x0 = is.x0 + xi * is.cell, z0 = is.z0 + zi * is.cell, x1 = x0 + is.cell * step, z1 = z0 + is.cell * step;
        Vector3 a{x0, is.Height(x0, z0), z0}, b{x1, is.Height(x1, z0), z0}, c{x1, is.Height(x1, z1), z1}, d{x0, is.Height(x0, z1), z1};
        if (a.y < -20 && b.y < -20 && c.y < -20 && d.y < -20) continue;   // (the deep floor: the seabed plane covers it)
        Color cc = col(x0 + is.cell * step * 0.5f, z0 + is.cell * step * 0.5f);
        mb.Tri(a, c, b, cc); mb.Tri(a, d, c, cc);
    }
}void BuildPalm(rt::MeshBuilder& mb) {
    // a unit palm 1 m tall (scaled per tree): a leaning trunk in rings, a crown of drooping fronds, coconuts
    std::vector<Vector3> trunk;
    for (int k = 0; k <= 6; k++) { float u = k / 6.0f; trunk.push_back({0.12f * u * u, u, 0.03f * sinf(u * 3)}); }
    mb.Tube(trunk, 0.045f, 0.028f, 6, {132, 104, 72, 255}, {150, 122, 84, 255}, 0);
    Vector3 top = trunk.back();
    for (int f = 0; f < 7; f++) {
        float a = f * 2 * PI / 7 + 0.3f;
        Vector3 dir{cosf(a), 0, sinf(a)}, side{-sinf(a), 0, cosf(a)};
        Color g = f % 2 ? Color{70, 140, 60, 255} : Color{88, 158, 66, 255};
        Vector3 prev = top; float w = 0.0f;
        for (int s = 1; s <= 4; s++) {
            float u = s / 4.0f;
            Vector3 p = Vector3Add(top, Vector3Add(Vector3Scale(dir, 0.42f * u), Vector3{0, 0.1f * u - 0.32f * u * u, 0}));
            float nw = 0.09f * sinf(u * PI) + 0.01f;
            TwoSided(mb, Vector3Add(prev, Vector3Scale(side, w)), Vector3Add(p, Vector3Scale(side, nw)), Vector3Subtract(p, Vector3Scale(side, nw)), g);
            TwoSided(mb, Vector3Add(prev, Vector3Scale(side, w)), Vector3Subtract(p, Vector3Scale(side, nw)), Vector3Subtract(prev, Vector3Scale(side, w)), g);
            prev = p; w = nw;
        }
    }
    for (int k = 0; k < 3; k++) mb.Octa({top.x + 0.04f * cosf(k * 2.1f), top.y - 0.05f, top.z + 0.04f * sinf(k * 2.1f)}, 0.03f, {96, 70, 40, 255});
}
void BuildNest(rt::MeshBuilder& mb) {
    // twigs woven in a ring, a hollow of down
    for (int k = 0; k < 22; k++) {
        float a = k * 2 * PI / 22, b = a + 0.9f;
        float r = 0.55f + 0.08f * sinf(k * 3.1f), y = 0.08f * sinf(k * 1.7f);
        mb.Tube({{cosf(a) * r, y, sinf(a) * r}, {cosf(b) * (r - 0.05f), y + 0.1f, sinf(b) * (r - 0.05f)}}, 0.04f, 0.03f, 4, {112, 84, 52, 255}, {136, 104, 66, 255}, 0);
    }
    for (int k = 0; k < 10; k++) {   // the down in the hollow: a low disc
        float a0 = k * 2 * PI / 10, a1 = (k + 1) * 2 * PI / 10;
        mb.Tri({0, 0.02f, 0}, {cosf(a1) * 0.48f, 0.06f, sinf(a1) * 0.48f}, {cosf(a0) * 0.48f, 0.06f, sinf(a0) * 0.48f}, {214, 204, 180, 255});
    }
}
// The bird, in its own frame: forward +z, up +y, right -x (the Lathe's axis is z). Parts are drawn with matrices so the
// wings can flap (inner panel at the shoulder, outer at the wrist), sweep back for a dive and spread for a flare.
void BuildBird(const fl::FounderDef& d) {
    float L = 0.22f + d.span * 0.14f;   // body length from the wingspan
    Color back = d.back, belly = d.belly, acc = d.accent;
    { rt::MeshBuilder mb;
      mb.Lathe(L, 6, 10, [&](float u) { return L * 0.2f * sinf(std::max(0.05f, u) * PI * 0.92f + 0.12f); }, [&](float u) { return L * 0.22f * sinf(std::max(0.05f, u) * PI * 0.92f + 0.12f); }, back, belly);
      S.body = LoadModelFromMesh(mb.Build()); }
    { rt::MeshBuilder mb;
      float r = L * 0.17f;
      mb.Lathe(r * 2.2f, 4, 8, [&](float u) { return r * sinf(std::max(0.08f, u) * PI); }, [&](float u) { return r * 1.05f * sinf(std::max(0.08f, u) * PI); }, back, belly);
      if (d.crest > 0) for (int k = 0; k < 3; k++) TwoSided(mb, {0, r * 0.7f, -r * 0.2f - k * r * 0.4f}, {0, r * (0.9f + d.crest * 2), -r * 0.8f - k * r * 0.5f}, {0, r * 0.6f, -r * 0.7f - k * r * 0.4f}, acc);
      mb.Octa({r * 0.62f, r * 0.25f, r * 0.45f}, r * 0.16f, {18, 18, 20, 255}); mb.Octa({-r * 0.62f, r * 0.25f, r * 0.45f}, r * 0.16f, {18, 18, 20, 255});
      S.head = LoadModelFromMesh(mb.Build()); }
    { rt::MeshBuilder mb;
      float bl = std::max(0.05f, d.beak) * 1.1f;
      mb.Cone({0, 0, 0}, {0, -bl * 0.25f, bl}, L * 0.07f, 6, acc);
      S.beak = LoadModelFromMesh(mb.Build()); }
    { rt::MeshBuilder mb;   // a fan of tail feathers (spreads with the scale the draw gives it)
      float tl = std::max(0.1f, d.tail) * 0.5f;
      for (int k = -2; k <= 2; k++) {
          float a = k * 0.16f;
          Vector3 tip{sinf(a) * tl, 0, -cosf(a) * tl}, s{cosf(a) * tl * 0.12f, 0, sinf(a) * tl * 0.12f};
          TwoSided(mb, {0, 0, 0}, Vector3Add(tip, s), Vector3Subtract(tip, s), k == 0 ? Shade(back, 0.85f) : back);
      }
      S.tail = LoadModelFromMesh(mb.Build()); }
    // wings: built for the RIGHT side (out along -x); the left is drawn mirrored. Inner panel 45% of the half span from
    // the shoulder, outer panel the rest with fingered primaries.
    float half = d.span * 0.5f, inner = half * 0.45f, outer = half * 0.55f, chord = std::max(0.12f, d.span * 0.16f);
    { rt::MeshBuilder mb;
      Vector3 a{0, 0, chord * 0.35f}, b{-inner, 0, chord * 0.3f}, c{-inner, 0, -chord * 0.65f}, e{0, 0, -chord * 0.55f};
      TwoSided(mb, a, b, c, back); TwoSided(mb, a, c, e, back);
      TwoSided(mb, {0, -0.005f, chord * 0.2f}, {-inner * 0.9f, -0.005f, chord * 0.15f}, {-inner * 0.9f, -0.005f, -chord * 0.3f}, Shade(belly, 0.95f));   // (coverts under)
      S.wingIn = LoadModelFromMesh(mb.Build()); }
    { rt::MeshBuilder mb;
      Vector3 a{0, 0, chord * 0.3f}, c{0, 0, -chord * 0.65f};
      Vector3 tip{-outer, 0, -chord * 0.25f};
      TwoSided(mb, a, {-outer * 0.55f, 0, chord * 0.15f}, {-outer * 0.55f, 0, -chord * 0.7f}, back);
      TwoSided(mb, a, {-outer * 0.55f, 0, -chord * 0.7f}, c, back);
      for (int k = 0; k < 5; k++) {   // the primaries, each its own feather
          float u = k / 4.0f;
          Vector3 root{-outer * 0.5f, 0, chord * (0.15f - 0.8f * u)}, f{-outer * (0.92f + 0.08f * (1 - fabsf(u - 0.3f))), 0, chord * (0.05f - 0.75f * u) - chord * 0.2f * u};
          Vector3 w{0, 0, chord * 0.09f};
          TwoSided(mb, Vector3Add(root, w), f, Vector3Subtract(root, w), k == 0 ? acc : Shade(back, 0.8f + 0.04f * k));
      }
      (void)tip;
      S.wingOut = LoadModelFromMesh(mb.Build()); }
}
void FreeModels() {
    if (!S.ready) return;
    for (Model& m : S.terr) UnloadModel(m);
    S.terr.clear();
    for (Model* m : {&S.palm, &S.nest, &S.body, &S.head, &S.beak, &S.tail, &S.wingIn, &S.wingOut, &S.egg, &S.chick, &S.pile}) UnloadModel(*m);
    S.ready = false; S.readyFor = -1;
}
void FreePreview();
void EnsureModels() {
    if (!IsWindowReady()) return;
    FreePreview();   // (the wardrobe's own bird, if it was open)
    int key = (int)WD().island.seed * 31 + WD().me.def + (int)WD().isles.size() * 7919;
    if (S.ready && S.readyFor == key) return;
    FreeModels();
    if (WD().wholeMap) for (size_t i = 0; i < WD().isles.size(); i++) { rt::MeshBuilder mb; if (WD().isles[i].type != fl::IsleType::Wreck) BuildTerrain(mb, WD().isles[i], (int)i == WD().home ? 1 : 2); else mb.Tri({0, -30, 0}, {0.1f, -30, 0}, {0, -30, 0.1f}, BLACK); S.terr.push_back(LoadModelFromMesh(mb.Build())); }
    else { rt::MeshBuilder mb; BuildTerrain(mb, WD().island); S.terr.push_back(LoadModelFromMesh(mb.Build())); }
    { rt::MeshBuilder mb; BuildPalm(mb); S.palm = LoadModelFromMesh(mb.Build()); }
    { rt::MeshBuilder mb; BuildNest(mb); S.nest = LoadModelFromMesh(mb.Build()); }
    { rt::MeshBuilder mb; mb.Lathe(0.075f, 5, 8, [](float u) { return 0.028f * sinf(std::max(0.05f, u) * PI) * (1.1f - 0.25f * u); }, [](float u) { return 0.028f * sinf(std::max(0.05f, u) * PI) * (1.1f - 0.25f * u); }, {244, 238, 226, 255}, {226, 218, 204, 255}); S.egg = LoadModelFromMesh(mb.Build()); }
    { rt::MeshBuilder mb;   // a chick: a ball of down, a head, a gaping beak
      Color down = Mix(WD().Def().belly, Color{200, 196, 186, 255}, 0.6f);
      mb.Lathe(0.16f, 5, 8, [](float u) { return 0.075f * sinf(std::max(0.08f, u) * PI); }, [](float u) { return 0.07f * sinf(std::max(0.08f, u) * PI); }, down, Shade(down, 0.85f), {0, 0.07f, 0});
      mb.Lathe(0.09f, 4, 8, [](float u) { return 0.045f * sinf(std::max(0.08f, u) * PI); }, [](float u) { return 0.045f * sinf(std::max(0.08f, u) * PI); }, down, Shade(down, 0.9f), {0, 0.16f, 0.06f});
      mb.Cone({0, 0.16f, 0.1f}, {0, 0.17f, 0.15f}, 0.018f, 5, WD().Def().accent);
      mb.Octa({0.025f, 0.18f, 0.09f}, 0.008f, {18, 18, 20, 255}); mb.Octa({-0.025f, 0.18f, 0.09f}, 0.008f, {18, 18, 20, 255});
      S.chick = LoadModelFromMesh(mb.Build()); }
    { rt::MeshBuilder mb;   // a cache: a low platform of sticks
      for (int k = 0; k < 14; k++) { float a = k * 0.45f, r = 0.35f + 0.25f * Hash((float)k, 3); Vector3 c{cosf(a) * r * 0.5f, 0.03f + 0.025f * (k % 3), sinf(a) * r * 0.5f}; mb.Tube({Vector3Add(c, {cosf(a + 1.6f) * 0.45f, 0, sinf(a + 1.6f) * 0.45f}), Vector3Subtract(c, {cosf(a + 1.6f) * 0.45f, 0, sinf(a + 1.6f) * 0.45f})}, 0.03f, 0.025f, 4, {118, 90, 58, 255}, {140, 108, 70, 255}, 0); }
      S.pile = LoadModelFromMesh(mb.Build()); }
    BuildBird(WD().Def());
    S.ready = true; S.readyFor = key;
}
void EnsureSea() {
    if (S.seaReady || !IsWindowReady()) return;
    Mesh m{};
    m.vertexCount = SN * SN * 6; m.triangleCount = SN * SN * 2;
    m.vertices = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
    m.texcoords = (float*)MemAlloc(m.vertexCount * 2 * sizeof(float));
    m.colors = (unsigned char*)MemAlloc(m.vertexCount * 4);
    m.normals = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
    for (int i = 0; i < m.vertexCount * 3; i++) m.normals[i] = (i % 3 == 1) ? 1.0f : 0.0f;
    for (int i = 0; i < m.vertexCount; i++) { m.colors[i * 4] = 40; m.colors[i * 4 + 1] = 120; m.colors[i * 4 + 2] = 130; m.colors[i * 4 + 3] = 200; }
    UploadMesh(&m, true);
    S.sea = LoadModelFromMesh(m);
    S.seaReady = true;
}
float Swell(float x, float z, float t) { return 0.12f * sinf(x * 0.11f + t * 0.9f) + 0.08f * sinf(z * 0.17f - t * 1.2f) + 0.05f * sinf((x + z) * 0.31f + t * 1.7f); }
void UpdateSea(Vector3 eye, float t) {
    float cx = floorf(eye.x / SC) * SC - SN * SC / 2, cz = floorf(eye.z / SC) * SC - SN * SC / 2;
    static std::vector<float> H; H.resize((SN + 1) * (SN + 1));
    for (int j = 0; j <= SN; j++) for (int i = 0; i <= SN; i++) H[j * (SN + 1) + i] = Swell(cx + i * SC, cz + j * SC, t);
    Mesh& m = S.sea.meshes[0];
    float* v = m.vertices; int k = 0;
    auto put = [&](int i, int j) { v[k++] = cx + i * SC; v[k++] = H[j * (SN + 1) + i]; v[k++] = cz + j * SC; };
    for (int j = 0; j < SN; j++) for (int i = 0; i < SN; i++) { put(i, j); put(i + 1, j); put(i + 1, j + 1); put(i, j); put(i + 1, j + 1); put(i, j + 1); }
    UpdateMeshBuffer(m, 0, v, m.vertexCount * 3 * sizeof(float), 0);
    float* n = m.normals; int q = 0;
    auto hh = [&](int i, int j) { i = std::clamp(i, 0, SN); j = std::clamp(j, 0, SN); return H[j * (SN + 1) + i]; };
    auto nput = [&](int i, int j) { Vector3 nn = Vector3Normalize({-(hh(i + 1, j) - hh(i - 1, j)) / (2 * SC), 1.0f, -(hh(i, j + 1) - hh(i, j - 1)) / (2 * SC)}); n[q++] = nn.x; n[q++] = nn.y; n[q++] = nn.z; };
    for (int j = 0; j < SN; j++) for (int i = 0; i < SN; i++) { nput(i, j); nput(i + 1, j); nput(i + 1, j + 1); nput(i, j); nput(i + 1, j + 1); nput(i, j + 1); }
    UpdateMeshBuffer(m, 2, n, m.vertexCount * 3 * sizeof(float), 0);
}

// ---------------------------------------------------------------- the day
struct DayLook { Color zenith, horizon, sun, amb, cloud; Vector3 toSun; float sunK, ambK, night; };
DayLook Day(float ph) {
    // the sun rises in the east at 0.25, is overhead at 0.5, sets in the west at 0.75; the moon has the night
    float a = (ph - 0.25f) * 2 * PI;
    float elev = sinf(a);
    Vector3 toSun = Vector3Normalize({cosf(a), std::max(elev, -0.3f), 0.35f});
    float day = std::clamp(elev * 3 + 0.2f, 0.0f, 1.0f);
    float gold = std::clamp(1 - fabsf(elev) * 4, 0.0f, 1.0f) * (elev > -0.2f ? 1.0f : 0.0f);   // dawn and dusk
    DayLook L;
    Color zenDay{74, 146, 222, 255}, horDay{196, 224, 240, 255}, zenNight{8, 12, 30, 255}, horNight{22, 32, 54, 255};
    L.zenith = Mix(zenNight, zenDay, day); L.horizon = Mix(horNight, horDay, day);
    L.horizon = Mix(L.horizon, {250, 172, 98, 255}, gold * 0.8f);
    L.zenith = Mix(L.zenith, {82, 94, 158, 255}, gold * 0.5f);
    L.sun = Mix({255, 246, 226, 255}, {255, 176, 104, 255}, gold);
    L.cloud = Mix(Mix({40, 46, 62, 255}, {246, 246, 250, 255}, day), {250, 190, 150, 255}, gold * 0.7f);
    L.amb = Mix({40, 52, 80, 255}, {170, 200, 230, 255}, day);
    L.sunK = 0.2f + 1.05f * day; L.ambK = 0.4f + 0.45f * day; L.night = 1 - day;
    L.toSun = day > 0.05f ? toSun : Vector3Normalize({-0.4f, 0.6f, -0.3f});   // (at night the light is the moon)
    if (day <= 0.05f) L.sun = {150, 170, 210, 255};
    return L;
}

// ---------------------------------------------------------------- input
fl::FounderInput Gather(float dt) {
    fl::FounderInput in;
    fl::Founder& f = WD().me;
    if (S.shot) { in.yaw = S.aimYaw; in.pitch = S.aimPitch; return in; }
    if (IsKeyPressed(KEY_TAB)) { if (!S.panel) { S.panel = true; S.page = 0; } else if (S.page < (WD().seasons > 0 ? 3 : 2)) S.page++; else S.panel = false; S.chart = false; }   // (a long match has a fourth page)
    // G: the Founder takes the lead of the nearest flock of yours (or lets it go)
    if (IsKeyPressed(KEY_G) && f.st != fl::FState::Dead) { Writer o; fl::OrderLead(o); Order(o); }
    if (IsKeyPressed(KEY_M)) { S.chart = !S.chart; S.panel = false; }
    Vector2 md = MouseLook(f.st != fl::FState::Dead && !S.panel && !S.chart);   // (the panel and the chart take the pointer: the bird flies on its last heading)
    if (S.panel || S.chart) md = {0, 0};
    if (f.st == fl::FState::Strike) {
        // the strike: the mouse steers the talons over the water (the world crawls at a quarter speed)
        S.steer.x = std::clamp(S.steer.x + md.x * 0.006f, -1.0f, 1.0f);
        S.steer.y = std::clamp(S.steer.y - md.y * 0.006f, -1.0f, 1.0f);
        if (IsKeyDown(KEY_A)) S.steer.x = std::max(-1.0f, S.steer.x - dt * 4);
        if (IsKeyDown(KEY_D)) S.steer.x = std::min(1.0f, S.steer.x + dt * 4);
        if (IsKeyDown(KEY_W)) S.steer.y = std::min(1.0f, S.steer.y + dt * 4);
        if (IsKeyDown(KEY_S)) S.steer.y = std::max(-1.0f, S.steer.y - dt * 4);
    } else {
        S.steer = {0, 0};
        S.aimYaw += md.x * 0.0028f;
        S.aimPitch = std::clamp(S.aimPitch - md.y * 0.0028f, -1.4f, 0.85f);
        if (IsKeyDown(KEY_A)) S.aimYaw -= dt * 1.6f;
        if (IsKeyDown(KEY_D)) S.aimYaw += dt * 1.6f;
    }
    in.yaw = S.aimYaw; in.pitch = S.aimPitch; in.steer = S.steer;
    in.flap = IsKeyDown(KEY_W);
    in.sprint = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    in.brake = IsKeyDown(KEY_S);
    in.interact = IsKeyPressed(KEY_E);
    in.eat = IsKeyPressed(KEY_F);
    in.takeoff = IsKeyPressed(KEY_SPACE) || ((f.st == fl::FState::Perched || f.st == fl::FState::Floating) && IsKeyPressed(KEY_W));
    if (IsKeyPressed(KEY_H)) S.help = !S.help;
    return in;
}

// ---------------------------------------------------------------- drawing
Matrix BirdWorld(const fl::Founder& f, float scale) {
    // own frame (+z forward) -> +x forward, then bank, pitch, heading, position
    Matrix m = MatrixScale(scale, scale, scale);
    m = MatrixMultiply(m, MatrixRotateY(PI * 0.5f));
    m = MatrixMultiply(m, MatrixRotateX(f.bank));
    m = MatrixMultiply(m, MatrixRotateZ(f.pitch));
    m = MatrixMultiply(m, MatrixRotateY(-f.yaw));
    return MatrixMultiply(m, MatrixTranslate(f.pos.x, f.pos.y, f.pos.z));
}
// ---------------------------------------------------------------- costumes (stage 8; doc pp. 28-30)
// A costume is drawn as parts on the bird: a hat in the head's frame, extras in the body's, a tint, a beak, a scale and an
// effect. gLook is set by whoever draws a bird (the Founder's costume, or a colony's livery hat) and cleared after.
Color Tint(Color a, Color b) { return {(unsigned char)(a.r * b.r / 255), (unsigned char)(a.g * b.g / 255), (unsigned char)(a.b * b.b / 255), 255}; }
fl::World& WD();
Matrix PoseWorld(Vector3 pos, float yaw, float pitch, float bank, float scale);
void DrawBirdBody(const fl::FounderDef& d, Matrix W, float shoulder, float wrist, float fold, float flare, float headTilt, float tailSpread, Color tint);
const fl::CostumeLook* gLook = nullptr;
struct SideLook { const fl::CostumeDef* costume = nullptr; const fl::LiveryColour* colour = nullptr; const fl::LiveryHat* hat = nullptr; int best = 0; fl::CostumeLook hatLook; };
SideLook ParseLook(const std::string& s) {
    SideLook L;
    std::vector<std::string> p; size_t a = 0;
    for (size_t i = 0; i <= s.size(); i++) if (i == s.size() || s[i] == ';') { p.push_back(s.substr(a, i - a)); a = i + 1; }
    if (p.size() > 0 && !p[0].empty()) L.costume = fl::FindCostume(p[0]);
    if (p.size() > 1 && !p[1].empty()) L.colour = fl::FindLiveryColour(p[1]);
    if (p.size() > 2 && !p[2].empty()) L.hat = fl::FindLiveryHat(p[2]);
    if (p.size() > 3) L.best = atoi(p[3].c_str());
    if (L.hat) { L.hatLook.hat = L.hat->hat; L.hatLook.hatC = L.hat->c; }
    return L;
}
std::string MyLook() {
    const fl::FlightWardrobe& w = fl::Wardrobe();
    return w.costume + ";" + w.liveryColour + ";" + w.liveryHat + ";" + std::to_string(w.bestScore);
}
void Box(Matrix frame, Vector3 c, Vector3 size, Color col, float rotY = 0, float rotX = 0, float rotZ = 0) {
    Matrix m = MatrixMultiply(MatrixScale(size.x, size.y, size.z), MatrixMultiply(MatrixMultiply(MatrixRotateX(rotX), MatrixRotateZ(rotZ)), MatrixRotateY(rotY)));
    rt::DrawCubeM(MatrixMultiply(MatrixMultiply(m, MatrixTranslate(c.x, c.y, c.z)), frame), col);
}
void Glow(Matrix frame, Vector3 c, float s, Color col, float k) { rt::DrawCubeGlow(MatrixMultiply(MatrixMultiply(MatrixScale(s, s, s), MatrixTranslate(c.x, c.y, c.z)), frame), col, k); }
// the hat, in the head's frame (centre of the head at the origin, forward +z, up +y; r the head's radius)
void DrawHat(const std::string& h, Color c, Matrix H, float r) {
    Color dk = Shade(c, 0.7f);
    if (h == "sailor_cap") { Box(H, {0, r * 0.95f, -r * 0.1f}, {r * 1.7f, r * 0.28f, r * 1.7f}, c); Box(H, {0, r * 0.78f, -r * 0.1f}, {r * 1.75f, r * 0.12f, r * 1.75f}, Color{30, 40, 80, 255}); }
    else if (h == "beanie") { Box(H, {0, r * 0.85f, -r * 0.15f}, {r * 1.5f, r * 0.75f, r * 1.6f}, c); Box(H, {0, r * 0.5f, -r * 0.15f}, {r * 1.6f, r * 0.2f, r * 1.7f}, dk); Box(H, {0, r * 1.35f, -r * 0.15f}, {r * 0.4f, r * 0.4f, r * 0.4f}, WHITE); }
    else if (h == "bicorne") { Box(H, {0, r * 1.15f, -r * 0.1f}, {r * 3.0f, r * 0.95f, r * 0.55f}, c); Box(H, {r * 0.5f, r * 1.2f, r * 0.2f}, {r * 0.35f, r * 0.35f, r * 0.1f}, Color{200, 40, 40, 255}); Box(H, {0, r * 0.75f, -r * 0.1f}, {r * 3.0f, r * 0.12f, r * 0.6f}, Color{220, 180, 70, 255}); }
    else if (h == "hook_hat") { Box(H, {0, r * 1.0f, -r * 0.1f}, {r * 1.6f, r * 0.65f, r * 1.6f}, c); Box(H, {0, r * 0.7f, -r * 0.1f}, {r * 2.4f, r * 0.1f, r * 2.4f}, dk); for (int k = 0; k < 6; k++) { float a = k * 1.05f; Box(H, {cosf(a) * r * 1.0f, r * 1.05f, -r * 0.1f + sinf(a) * r * 1.0f}, {r * 0.08f, r * 0.45f, r * 0.08f}, Color{190, 196, 200, 255}, a, 0.4f); } }
    else if (h == "straw_hat") { Box(H, {0, r * 0.8f, -r * 0.1f}, {r * 3.2f, r * 0.1f, r * 3.2f}, c, 0.3f); Box(H, {0, r * 1.15f, -r * 0.1f}, {r * 1.3f, r * 0.65f, r * 1.3f}, c, 0.3f); Box(H, {0, r * 0.9f, -r * 0.1f}, {r * 1.35f, r * 0.15f, r * 1.35f}, Color{150, 60, 50, 255}, 0.3f); }
    else if (h == "bandana") { Box(H, {0, r * 0.7f, -r * 0.1f}, {r * 2.05f, r * 0.65f, r * 2.1f}, c); for (int s = -1; s <= 1; s += 2) Box(H, {r * 0.25f * s, r * 0.55f, -r * 1.3f}, {r * 0.3f, r * 0.12f, r * 0.8f}, c, 0.3f * s, 0.5f); }
    else if (h == "cowl") { Box(H, {0, r * 0.35f, -r * 0.35f}, {r * 2.3f, r * 2.3f, r * 2.0f}, c); Box(H, {0, r * 1.25f, -r * 1.1f}, {r * 0.9f, r * 0.7f, r * 0.9f}, c, 0, 0.5f); }
    else if (h == "periscope_cap") { Box(H, {0, r * 0.95f, -r * 0.1f}, {r * 1.6f, r * 0.35f, r * 1.6f}, c); Box(H, {0, r * 1.6f, -r * 0.1f}, {r * 0.22f, r * 1.0f, r * 0.22f}, Color{180, 150, 80, 255}); Box(H, {0, r * 2.05f, r * 0.15f}, {r * 0.22f, r * 0.22f, r * 0.6f}, Color{180, 150, 80, 255}); Box(H, {0, r * 2.05f, r * 0.47f}, {r * 0.18f, r * 0.18f, r * 0.05f}, Color{120, 200, 230, 255}); }
    else if (h == "comb") { for (int k = 0; k < 3; k++) Box(H, {0, r * (1.0f + 0.12f * (k == 1)), r * (0.3f - 0.4f * k)}, {r * 0.18f, r * 0.55f, r * 0.38f}, c); Box(H, {0, -r * 0.6f, r * 0.85f}, {r * 0.22f, r * 0.5f, r * 0.2f}, c); }
    else if (h == "crest_feathers") { for (int k = 0; k < 4; k++) Box(H, {0, r * 1.1f, -r * (0.1f + 0.3f * k)}, {r * 0.12f, r * (1.0f - 0.12f * k), r * 0.2f}, c, 0, -0.5f - 0.15f * k); }
    else if (h == "heron_hat") { Box(H, {0, r * 0.95f, -r * 0.1f}, {r * 1.4f, r * 0.3f, r * 1.4f}, c); for (int k = 0; k < 2; k++) Box(H, {0, r * 1.0f, -r * (1.0f + 0.4f * k)}, {r * 0.08f, r * 0.08f, r * 1.6f}, Color{30, 30, 34, 255}, 0, 0.2f + 0.15f * k); }
    else if (h == "bell_helmet" || h == "brass_helmet") {
        bool bell = h == "bell_helmet";
        Box(H, {0, r * 0.25f, 0}, {r * (bell ? 2.6f : 2.3f), r * (bell ? 2.8f : 2.3f), r * (bell ? 2.6f : 2.3f)}, c, 0.785f);
        Box(H, {0, r * 0.25f, r * (bell ? 1.32f : 1.18f)}, {r * 1.1f, r * 1.1f, r * 0.1f}, Color{60, 90, 110, 255});
        Box(H, {0, r * 0.25f, r * (bell ? 1.36f : 1.22f)}, {r * 1.3f, r * 0.15f, r * 0.08f}, dk);
        if (!bell) for (int s = -1; s <= 1; s += 2) Box(H, {r * 1.18f * s, r * 0.25f, 0}, {r * 0.1f, r * 0.7f, r * 0.7f}, Color{60, 90, 110, 255});
        Box(H, {0, -r * 0.95f, 0}, {r * 2.0f, r * 0.3f, r * 2.0f}, dk);
    }
    else if (h == "ape_hat") { Box(H, {0, r * 1.0f, -r * 0.2f}, {r * 2.0f, r * 1.1f, r * 1.9f}, c); Box(H, {0, r * 0.9f, r * 0.75f}, {r * 1.1f, r * 0.7f, r * 0.15f}, Color{140, 116, 96, 255}); for (int s = -1; s <= 1; s += 2) Box(H, {r * 0.3f * s, r * 1.05f, r * 0.84f}, {r * 0.16f, r * 0.16f, r * 0.05f}, Color{240, 220, 140, 255}); }
    else if (h == "crown") { Box(H, {0, r * 0.95f, -r * 0.05f}, {r * 1.5f, r * 0.35f, r * 1.5f}, c); for (int k = 0; k < 5; k++) { float a = k * 2 * PI / 5; Box(H, {cosf(a) * r * 0.6f, r * 1.3f, -r * 0.05f + sinf(a) * r * 0.6f}, {r * 0.22f, r * 0.45f, r * 0.22f}, c, a); Box(H, {cosf(a) * r * 0.62f, r * 1.0f, -r * 0.05f + sinf(a) * r * 0.62f}, {r * 0.18f, r * 0.18f, r * 0.18f}, k % 2 ? Color{200, 30, 60, 255} : Color{60, 120, 220, 255}, a); } }
    else if (h == "swoop_hair") { for (int k = 0; k < 5; k++) Box(H, {r * (0.5f - 0.2f * k), r * (1.0f + 0.08f * k), r * (0.5f - 0.35f * k)}, {r * 1.6f, r * 0.35f, r * 0.9f}, Mix(c, WHITE, 0.08f * k), 0.5f, -0.25f, 0.2f); Box(H, {r * 0.6f, r * 1.05f, r * 0.75f}, {r * 0.9f, r * 0.3f, r * 0.6f}, c, -0.4f, 0.3f); }
    else if (h == "top_hat") { Box(H, {0, r * 0.82f, -r * 0.1f}, {r * 1.9f, r * 0.1f, r * 1.9f}, c); Box(H, {0, r * 1.45f, -r * 0.1f}, {r * 1.1f, r * 1.3f, r * 1.1f}, c); Box(H, {0, r * 1.0f, -r * 0.1f}, {r * 1.15f, r * 0.18f, r * 1.15f}, Color{160, 40, 40, 255}); }
    else if (h == "shell") { for (int k = -2; k <= 2; k++) Box(H, {r * 0.25f * k, r * 1.05f, -r * 0.1f}, {r * 0.28f, r * 0.8f, r * 0.12f}, Mix(c, WHITE, 0.15f * (k & 1)), 0, -0.2f, -0.3f * k); }
}
// the extras, in the body's frame (forward +z, up +y; L the body's length), and some on the head
void DrawExtras(const fl::CostumeLook& lk, Matrix W, Matrix H, float L, float r, bool flying) {
    Color c = lk.extraC, dk = Shade(c, 0.7f);
    for (const std::string& e : lk.extras) {
        if (e == "neckerchief") { Box(W, {0, -L * 0.05f, L * 0.36f}, {L * 0.3f, L * 0.18f, L * 0.1f}, c); Box(W, {0, -L * 0.17f, L * 0.37f}, {L * 0.12f, L * 0.14f, L * 0.06f}, c, 0, 0, 0.785f); }
        else if (e == "lantern") { Box(W, {0, -L * 0.3f, L * 0.2f}, {L * 0.02f, L * 0.25f, L * 0.02f}, Color{60, 60, 60, 255}); Box(W, {0, -L * 0.5f, L * 0.2f}, {L * 0.14f, L * 0.18f, L * 0.14f}, Color{70, 60, 40, 255}); Glow(W, {0, -L * 0.5f, L * 0.2f}, L * 0.1f, Color{255, 220, 140, 255}, 2.0f); }
        else if (e == "epaulettes") for (int s = -1; s <= 1; s += 2) { Box(W, {L * 0.17f * s, L * 0.17f, L * 0.18f}, {L * 0.16f, L * 0.05f, L * 0.14f}, c); for (int k = 0; k < 3; k++) Box(W, {L * (0.12f + 0.05f * k) * s, L * 0.11f, L * 0.18f}, {L * 0.015f, L * 0.08f, L * 0.015f}, c); }
        else if (e == "goggles") for (int s = -1; s <= 1; s += 2) { Box(H, {r * 0.62f * s, r * 0.3f, r * 0.5f}, {r * 0.5f, r * 0.5f, r * 0.2f}, Color{110, 80, 50, 255}, 0.3f * s); Box(H, {r * 0.66f * s, r * 0.3f, r * 0.6f}, {r * 0.32f, r * 0.32f, r * 0.05f}, Color{150, 210, 230, 255}, 0.3f * s); }
        else if (e == "shell_necklace") for (int k = 0; k < 7; k++) { float a = (k - 3) * 0.32f; Box(W, {sinf(a) * L * 0.2f, -L * 0.1f + cosf(a * 1.5f) * L * 0.02f, L * 0.33f + cosf(a) * L * 0.03f}, {L * 0.05f, L * 0.05f, L * 0.03f}, k == 3 ? Color{250, 250, 255, 255} : c, a); }
        else if (e == "straw") for (int k = 0; k < 8; k++) { float s = k % 2 ? 1.0f : -1.0f; Box(W, {L * 0.2f * s, L * (0.05f - 0.03f * (k / 2)), L * (0.1f - 0.08f * (k / 2))}, {L * 0.15f, L * 0.015f, L * 0.015f}, Color{230, 200, 110, 255}, 0.4f * s, 0, 0.3f * s); }
        else if (e == "eyepatch") { Box(H, {r * 0.66f, r * 0.27f, r * 0.48f}, {r * 0.4f, r * 0.4f, r * 0.1f}, Color{20, 20, 22, 255}, 0.5f); Box(H, {0, r * 0.45f, 0}, {r * 2.05f, r * 0.08f, r * 2.05f}, Color{20, 20, 22, 255}, 0, 0.3f); }
        else if (e == "wooden_leg") Box(W, {L * 0.06f, -L * 0.38f, -L * 0.02f}, {L * 0.05f, L * 0.3f, L * 0.05f}, Color{140, 100, 60, 255});
        else if (e == "tentacle_scarf") { Box(W, {0, -L * 0.02f, L * 0.33f}, {L * 0.42f, L * 0.1f, L * 0.16f}, c); for (int k = 0; k < 4; k++) Box(W, {L * (0.1f - 0.03f * k), -L * (0.1f + 0.07f * k), L * (0.3f + 0.02f * sinf(S.t * 3 + k))}, {L * (0.06f - 0.01f * k), L * 0.08f, L * (0.06f - 0.01f * k)}, k % 2 ? dk : c, 0, 0, 0.3f * sinf(S.t * 2 + k)); }
        else if (e == "coat") for (int s = -1; s <= 1; s += 2) { Box(W, {L * 0.08f * s, -L * 0.05f, -L * 0.25f}, {L * 0.14f, L * 0.06f, L * 0.4f}, c, 0.1f * s, 0.25f + (flying ? 0.1f * sinf(S.t * 6 + s) : 0)); Box(W, {L * 0.11f * s, L * 0.05f, L * 0.1f}, {L * 0.04f, L * 0.04f, L * 0.04f}, Color{220, 190, 80, 255}); }
        else if (e == "beard") { Box(H, {0, -r * 0.5f, r * 0.6f}, {r * 0.9f, r * 0.9f, r * 0.5f}, Color{210, 210, 206, 255}, 0, -0.2f); }
        else if (e == "white_front") Box(W, {0, -L * 0.05f, L * 0.12f}, {L * 0.26f, L * 0.24f, L * 0.42f}, Color{246, 246, 244, 255});
        else if (e == "beak_stripes") { Box(H, {0, -r * 0.05f, r * 1.2f}, {r * 0.42f, r * 0.42f, r * 0.08f}, Color{250, 220, 80, 255}); Box(H, {0, -r * 0.05f, r * 1.5f}, {r * 0.34f, r * 0.34f, r * 0.08f}, Color{60, 70, 100, 255}); }
        else if (e == "long_leg") { Box(W, {0, -L * 0.55f, 0}, {L * 0.03f, L * 0.7f, L * 0.03f}, Color{240, 120, 140, 255}); Box(W, {0, -L * 0.9f, L * 0.05f}, {L * 0.04f, L * 0.02f, L * 0.14f}, Color{240, 120, 140, 255}); }
        else if (e == "iridescent_neck") Box(W, {0, L * 0.03f, L * 0.36f}, {L * 0.3f, L * 0.22f, L * 0.14f}, Mix(c, Color{170, 90, 170, 255}, 0.5f + 0.5f * sinf(S.t * 2)));
        else if (e == "fan_tail") for (int k = -3; k <= 3; k++) Box(W, {L * 0.07f * k, L * 0.2f, -L * 0.45f}, {L * 0.08f, L * 0.55f, L * 0.02f}, k % 2 ? Color{110, 80, 50, 255} : Color{170, 130, 80, 255}, 0, 0.3f, -0.18f * k);
        else if (e == "wattle") Box(H, {0, -r * 0.65f, r * 0.75f}, {r * 0.3f, r * 0.7f, r * 0.25f}, c);
        else if (e == "glasses") for (int s = -1; s <= 1; s += 2) { Box(H, {r * 0.62f * s, r * 0.27f, r * 0.55f}, {r * 0.48f, r * 0.48f, r * 0.06f}, c, 0.3f * s); Box(H, {r * 0.62f * s, r * 0.27f, r * 0.57f}, {r * 0.36f, r * 0.36f, r * 0.03f}, Color{200, 230, 240, 255}, 0.3f * s); }
        else if (e == "long_neck") Box(W, {0, L * 0.18f, L * 0.42f}, {L * 0.12f, L * 0.4f, L * 0.12f}, lk.tinted ? lk.tint : WHITE);
        else if (e == "pouch") Box(H, {0, -r * 0.65f, r * 1.1f}, {r * 0.5f, r * 0.6f, r * 1.1f}, c, 0, 0.15f);
        else if (e == "peacock_tail") for (int k = -4; k <= 4; k++) { Box(W, {L * 0.08f * k, L * 0.15f, -L * 0.75f}, {L * 0.1f, L * 0.02f, L * 0.75f}, c, -0.12f * k, -0.25f); Box(W, {L * 0.2f * k, L * 0.33f, -L * 1.1f}, {L * 0.09f, L * 0.03f, L * 0.09f}, Color{40, 80, 200, 255}, -0.12f * k, -0.25f); }
        else if (e == "chip") Box(H, {r * 0.15f, -r * 0.1f, r * 1.5f}, {r * 1.1f, r * 0.18f, r * 0.18f}, c, 0.4f);
        else if (e == "shiny") Glow(H, {0, -r * 0.15f, r * 1.4f}, r * 0.3f, c, 1.4f + 0.6f * sinf(S.t * 5));
        else if (e == "cape") Box(W, {0, L * 0.12f, -L * 0.3f}, {L * 0.45f, L * 0.03f, L * 0.8f}, c, 0, 0.08f + (flying ? 0.12f * sinf(S.t * 7) : 0.6f));
        else if (e == "kiwi_suit") Box(W, {0, 0, 0}, {L * 0.5f, L * 0.48f, L * 0.62f}, Color{130, 100, 70, 255}, 0.785f);
        else if (e == "bundle") { Box(H, {0, -r * 0.4f, r * 1.4f}, {r * 0.08f, r * 0.8f, r * 0.08f}, Color{200, 180, 140, 255}); Box(H, {0, -r * 1.2f, r * 1.4f}, {r * 1.0f, r * 0.9f, r * 0.9f}, c, 0.785f); }
        else if (e == "ruff") for (int k = 0; k < 10; k++) { float a = k * 2 * PI / 10; Box(W, {cosf(a) * L * 0.17f, sinf(a) * L * 0.17f + L * 0.02f, L * 0.33f}, {L * 0.1f, L * 0.05f, L * 0.08f}, c, 0, 0, a); }
        else if (e == "clock") { Box(W, {0, -L * 0.04f, L * 0.36f}, {L * 0.2f, L * 0.24f, L * 0.06f}, c); Box(W, {0, -L * 0.04f, L * 0.4f}, {L * 0.14f, L * 0.14f, L * 0.01f}, Color{240, 236, 220, 255}); Box(W, {0, -L * 0.04f, L * 0.41f}, {L * 0.01f, L * 0.07f, L * 0.01f}, BLACK, 0, 0, S.t * 0.5f); }
        else if (e == "scroll") { Box(W, {L * 0.05f, -L * 0.3f, -L * 0.02f}, {L * 0.04f, L * 0.04f, L * 0.16f}, c); Box(W, {L * 0.05f, -L * 0.3f, -L * 0.02f}, {L * 0.045f, L * 0.045f, L * 0.02f}, Color{180, 40, 40, 255}); }
        else if (e == "armband") for (int s = -1; s <= 1; s += 2) Box(W, {L * 0.2f * s, L * 0.06f, L * 0.08f}, {L * 0.08f, L * 0.1f, L * 0.1f}, c);
        else if (e == "lion_rear") { Box(W, {0, -L * 0.03f, -L * 0.3f}, {L * 0.42f, L * 0.4f, L * 0.45f}, c, 0.785f); for (int s = -1; s <= 1; s += 2) Box(W, {L * 0.12f * s, -L * 0.35f, -L * 0.32f}, {L * 0.08f, L * 0.3f, L * 0.08f}, c); Box(W, {0, L * 0.05f, -L * 0.75f}, {L * 0.03f, L * 0.03f, L * 0.5f}, c, 0, -0.3f); Box(W, {0, L * 0.2f, -L * 1.0f}, {L * 0.1f, L * 0.1f, L * 0.1f}, Color{120, 80, 40, 255}); }
        else if (e == "arms8") for (int k = 0; k < 8; k++) { float a = k * 2 * PI / 8; for (int j = 0; j < 3; j++) Box(W, {cosf(a) * L * (0.12f + 0.03f * j), -L * (0.15f + 0.12f * j), sinf(a) * L * 0.12f + L * 0.05f * sinf(S.t * 3 + k + j)}, {L * (0.06f - 0.012f * j), L * 0.12f, L * (0.06f - 0.012f * j)}, j % 2 ? dk : c, a); }
        else if (e == "worm_puppet") for (int j = 0; j < 7; j++) Box(W, {L * 0.05f * sinf(S.t * 2 + j * 0.8f), L * (0.25f + 0.13f * j), -L * 0.1f + L * 0.04f * j}, {L * (0.2f - 0.012f * j), L * 0.14f, L * (0.2f - 0.012f * j)}, j % 2 ? dk : c, j * 0.3f);
        else if (e == "fish_suit") { Box(W, {0, L * 0.22f, 0}, {L * 0.04f, L * 0.2f, L * 0.4f}, c, 0, 0.3f); for (int s = -1; s <= 1; s += 2) Box(W, {L * 0.2f * s, -L * 0.05f, L * 0.1f}, {L * 0.14f, L * 0.03f, L * 0.12f}, c, 0.5f * s); Box(W, {0, 0, -L * 0.55f}, {L * 0.03f, L * 0.42f, L * 0.22f}, c); }
        else if (e == "sub_hull") { Box(W, {0, 0, 0}, {L * 0.42f, L * 0.42f, L * 1.0f}, c); Box(W, {0, L * 0.3f, L * 0.05f}, {L * 0.14f, L * 0.22f, L * 0.26f}, Shade(c, 0.85f)); Box(W, {0, L * 0.47f, L * 0.1f}, {L * 0.03f, L * 0.12f, L * 0.03f}, Color{180, 150, 80, 255}); for (int k = 0; k < 3; k++) Box(W, {L * 0.21f, L * 0.05f, L * (0.25f - 0.2f * k)}, {L * 0.02f, L * 0.07f, L * 0.07f}, Color{230, 200, 120, 255}); }
        else if (e == "goat_horns") for (int s = -1; s <= 1; s += 2) { Box(H, {r * 0.45f * s, r * 1.0f, -r * 0.3f}, {r * 0.22f, r * 0.7f, r * 0.22f}, c, 0, -0.6f, 0.2f * s); Box(H, {r * 0.55f * s, r * 1.2f, -r * 0.85f}, {r * 0.18f, r * 0.5f, r * 0.18f}, c, 0, -1.5f, 0.2f * s); }
        else if (e == "goat_beard") Box(H, {0, -r * 0.75f, r * 0.55f}, {r * 0.25f, r * 0.75f, r * 0.25f}, c, 0, -0.2f);
        else if (e == "egg_body") rt::DrawStatic(S.egg, MatrixMultiply(MatrixMultiply(MatrixScale(L * 13.0f, L * 13.0f, L * 14.0f), MatrixRotateX(-0.3f)), W), c);
        else if (e == "sun_mask") { Box(H, {0, r * 0.2f, r * 0.75f}, {r * 1.7f, r * 1.7f, r * 0.1f}, c, 0, 0, 0.785f); for (int k = 0; k < 8; k++) { float a = k * PI / 4; Box(H, {cosf(a) * r * 1.2f, r * 0.2f + sinf(a) * r * 1.2f, r * 0.7f}, {r * 0.5f, r * 0.15f, r * 0.05f}, Color{250, 150, 40, 255}, 0, 0, a); } Glow(H, {0, r * 0.2f, r * 0.85f}, r * 0.4f, Color{255, 220, 120, 255}, 1.2f); }
        else if (e == "orca_patches") { for (int s = -1; s <= 1; s += 2) Box(H, {r * 0.6f * s, r * 0.5f, r * 0.2f}, {r * 0.3f, r * 0.25f, r * 0.5f}, c); Box(W, {0, -L * 0.12f, L * 0.1f}, {L * 0.24f, L * 0.1f, L * 0.5f}, c); }
        else if (e == "dorsal_fin") Box(W, {0, L * 0.35f, -L * 0.05f}, {L * 0.04f, L * 0.45f, L * 0.22f}, Color{24, 24, 30, 255}, 0, -0.35f);
        else if (e == "red_tie") { Box(W, {0, -L * 0.03f, L * 0.37f}, {L * 0.08f, L * 0.06f, L * 0.05f}, c); Box(W, {0, -L * 0.2f, L * 0.36f}, {L * 0.07f, L * 0.3f, L * 0.03f}, c); }
    }
}
// One bird's body with its wings posed: shoulder (up/down beat), wrist, fold (0 spread .. 1 folded along the body),
// flare (wings up and forward for a landing), head tilt and tail spread. Used for the Founder and every colony bird.
void DrawBirdBody(const fl::FounderDef& d, Matrix W, float shoulder, float wrist, float fold, float flare, float headTilt, float tailSpread, Color tint = WHITE) {
    float L = 0.22f + d.span * 0.14f;
    const fl::CostumeLook* lk = gLook;
    bool longNeck = false;
    if (lk) {
        if (lk->scale != 1) W = MatrixMultiply(MatrixScale(lk->scale, lk->scale, lk->scale), W);
        if (lk->tinted) tint = Tint(lk->tint, Mix(WHITE, tint, 0.35f));
        for (const auto& e : lk->extras) longNeck |= e == "long_neck";
    }
    float sweep = fold * 1.2f, raise = flare * 0.6f;
    float inner = d.span * 0.5f * 0.45f;
    auto wing = [&](float side) {   // side 1 the right wing (out along -x), -1 the left (mirrored, out along +x)
        // after the mirror both wings use the right wing's angles times side: up/down about the body's axis (z),
        // back about y for the fold; the outer panel hangs off the wrist at the inner panel's end
        Matrix mir = side > 0 ? MatrixIdentity() : MatrixScale(-1, 1, 1);
        float up = shoulder + raise;
        Matrix sh = MatrixMultiply(MatrixRotateY(-sweep * 0.9f * side), MatrixRotateZ(-up * side));
        Matrix base = MatrixMultiply(MatrixTranslate(-L * 0.12f * side, L * 0.08f, L * 0.08f), W);
        Matrix shB = MatrixMultiply(sh, base);
        float inLen = inner * (1 - fold * 0.45f);   // (folded, the inner panel tucks in toward the body)
        rt::DrawStatic(S.wingIn, MatrixMultiply(MatrixMultiply(MatrixScale(1 - fold * 0.45f, 1, 1), mir), shB), tint);
        Matrix el = MatrixMultiply(MatrixRotateY(-sweep * 1.4f * side), MatrixRotateZ(-wrist * side * (1 - fold)));
        rt::DrawStatic(S.wingOut, MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(1 - fold * 0.3f, 1, 1), mir), el), MatrixMultiply(MatrixTranslate(-inLen * side, 0, 0), shB)), tint);
    };
    rt::DrawStatic(S.body, W, tint);
    Matrix headM = MatrixMultiply(MatrixMultiply(MatrixRotateX(headTilt), MatrixTranslate(0, L * (longNeck ? 0.45f : 0.1f), L * (longNeck ? 0.56f : 0.5f))), W);
    rt::DrawStatic(S.head, headM, tint);
    float bs = lk ? lk->beak : 1.0f;
    rt::DrawStatic(S.beak, MatrixMultiply(MatrixMultiply(MatrixScale(bs, bs, bs), MatrixTranslate(0, 0, L * 0.17f)), headM), lk && lk->beakTinted ? lk->beakC : WHITE);
    rt::DrawStatic(S.tail, MatrixMultiply(MatrixMultiply(MatrixScale(tailSpread, 1, 1), MatrixMultiply(MatrixRotateX(-0.15f + 0.4f * flare), MatrixTranslate(0, 0, -L * 0.42f))), W), tint);
    wing(1); wing(-1);
    if (lk) {   // the costume's hat and extras
        float r = L * 0.17f;
        if (!lk->hat.empty()) DrawHat(lk->hat, lk->hatC, headM, r);
        DrawExtras(*lk, W, headM, L, r, fold < 0.5f);
    }
}
Matrix PoseWorld(Vector3 pos, float yaw, float pitch, float bank, float scale) {
    Matrix m = MatrixScale(scale, scale, scale);
    m = MatrixMultiply(m, MatrixRotateY(PI * 0.5f));
    m = MatrixMultiply(m, MatrixRotateX(bank));
    m = MatrixMultiply(m, MatrixRotateZ(pitch));
    m = MatrixMultiply(m, MatrixRotateY(-yaw));
    return MatrixMultiply(m, MatrixTranslate(pos.x, pos.y, pos.z));
}
// the costume's effects in the world (after the bird): embers, lightning on a dive, a blur, the gulls or chicks that follow
void DrawCostumeFx(const fl::CostumeLook& lk, Vector3 pos, Vector3 vel, float yaw, float L, bool diving, bool night) {
    if (lk.fx == "embers") for (int k = 0; k < 10; k++) { float ph = fmodf(S.t * 0.8f + k * 0.1f, 1.0f); Vector3 p = Vector3Add(pos, Vector3Add(Vector3Scale(vel, -0.12f * ph * 4), {sinf(k * 2.3f) * 0.3f, 0.4f * ph, cosf(k * 1.7f) * 0.3f})); rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.06f * (1 - ph), 0.06f * (1 - ph), 0.06f * (1 - ph)), MatrixTranslate(p.x, p.y, p.z)), Color{255, (unsigned char)(160 - 100 * ph), 40, 255}, 2.0f); }
    if (lk.fx == "lightning" && diving) { Vector3 a = pos; for (int k = 0; k < 6; k++) { Vector3 b = Vector3Add(a, {sinf(S.t * 40 + k * 3) * 0.5f, -0.7f, cosf(S.t * 37 + k * 2) * 0.5f}); Vector3 m = Vector3Scale(Vector3Add(a, b), 0.5f); float len = Vector3Distance(a, b); rt::DrawCubeGlow(MatrixMultiply(MatrixMultiply(MatrixScale(0.05f, len, 0.05f), MatrixRotateZ(atan2f(b.x - a.x, a.y - b.y))), MatrixTranslate(m.x, m.y, m.z)), Color{200, 220, 255, 255}, 3.0f); a = b; } }
    if (lk.fx == "blur") for (int k = 0; k < 6; k++) {   // (speed streaks off the wingtips)
        float side = k % 2 ? 1.0f : -1.0f, ph = fmodf(S.t * 3 + k * 0.17f, 1.0f);
        Vector3 f{cosf(yaw), 0, sinf(yaw)}, r{-f.z, 0, f.x};
        Vector3 p = Vector3Add(pos, Vector3Add(Vector3Scale(r, side * L * (0.9f + 0.2f * (k / 2))), Vector3Scale(f, -L * (0.5f + 1.5f * ph))));
        rt::DrawCubeGlow(MatrixMultiply(MatrixMultiply(MatrixScale(L * 0.03f, L * 0.03f, L * 0.6f), MatrixRotateY(-yaw + PI * 0.5f)), MatrixTranslate(p.x, p.y, p.z)), Color{160, 255, 220, 255}, 1.2f * (1 - ph));
    }
    if (lk.glow && night) rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.25f, 0.25f, 0.25f), MatrixTranslate(pos.x, pos.y - L * 0.5f, pos.z)), Color{255, 220, 150, 255}, 3.0f);
    if (!lk.follow.empty()) {
        bool chicks = lk.follow == "chicks";
        for (int k = 0; k < 3; k++) {
            float back = L * (4.5f + 2.6f * k), side = (k - 1) * L * 2.2f;
            Vector3 f{cosf(yaw), 0, sinf(yaw)}, r{-f.z, 0, f.x};
            Vector3 p = Vector3Add(pos, Vector3Add(Vector3Scale(f, -back), Vector3Add(Vector3Scale(r, side), {0, 0.3f * sinf(S.t * 2 + k), 0})));
            if (chicks) rt::DrawStatic(S.chick, MatrixMultiply(MatrixMultiply(MatrixScale(1.6f, 1.6f, 1.6f), MatrixRotateY(-yaw + PI * 0.5f)), MatrixTranslate(p.x, p.y - 0.15f, p.z)), Color{255, 230, 140, 255});
            else { const fl::CostumeLook* keep = gLook; gLook = nullptr; float b = sinf(S.t * 9 + k * 2);
                   DrawBirdBody(WD().Def(), PoseWorld(p, yaw, 0, 0, 0.45f), 0.5f * b, 0.3f * b, 0, 0, 0, 0.7f, Color{250, 250, 252, 255}); gLook = keep; }
        }
    }
}
void DrawCarried(const fl::World& w, Matrix W, float L, int sp, int twigs, float yaw, float pitch) {
    if (sp >= 0 && w.eco.map) {
        const rt::CreatureModel& cm = rt::Creature(w.seaKey, w.eco.map->species[sp].name);
        rt::DrawCreature(cm, Vector3Transform({0, -L * 0.35f, 0}, W), atan2f(cosf(yaw), sinf(yaw)), pitch * 0.5f, 1.0f, S.t * cm.freq * 1.5f, 0.6f);
    }
    else if (sp == -2) rt::DrawStatic(S.egg, MatrixMultiply(MatrixTranslate(0, -L * 0.35f, 0), W));   // (a stolen egg)
    else if (sp == -3) {   // a bomb: a gourd packed with guano and sulfur, its fuse smouldering
        rt::DrawCubeM(MatrixMultiply(MatrixScale(L * 0.45f, L * 0.45f, L * 0.45f), MatrixMultiply(MatrixTranslate(0, -L * 0.45f, 0), W)), Color{92, 70, 46, 255});
        rt::DrawCubeGlow(MatrixMultiply(MatrixScale(L * 0.08f, L * 0.08f, L * 0.08f), MatrixMultiply(MatrixTranslate(0, -L * 0.2f, 0), W)), Color{255, 160, 60, 255}, 1.5f + sinf(S.t * 20));
    }
    else if (sp == -4) rt::DrawCubeM(MatrixMultiply(MatrixScale(L * 0.4f, L * 0.5f, L * 0.4f), MatrixMultiply(MatrixTranslate(0, -L * 0.5f, 0), W)), Color{196, 150, 60, 255});   // (the wreck's bell)
    else if (sp == -5) rt::DrawCubeM(MatrixMultiply(MatrixScale(L * 0.35f, L * 0.25f, L * 0.35f), MatrixMultiply(MatrixTranslate(0, -L * 0.4f, 0), W)), Color{226, 204, 64, 255});   // (a lump of sulfur)
    for (int k = 0; k < twigs; k++) rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.6f, 0.03f, 0.03f), MatrixRotateY(0.4f + k * 0.5f)), MatrixMultiply(MatrixTranslate(0, -L * 0.3f - k * 0.03f, 0), W)), Color{120, 88, 54, 255});
}
void DrawBird(const fl::World& w, float dt) {
    const fl::Founder& f = w.me;
    const fl::FounderDef& d = w.Def();
    if (f.st == fl::FState::Dead || f.st == fl::FState::Under) return;
    float sc = f.chick ? 0.7f : 1.0f;
    Matrix W = BirdWorld(f, sc);
    float L = 0.22f + d.span * 0.14f;
    // the wing pose: flapping beats (heavier when tired), a glide holds them out with a little dihedral, a dive tucks
    // them back, a landing flares them; perched, they fold along the body
    bool flying = f.st == fl::FState::Fly || f.st == fl::FState::Strike || f.st == fl::FState::Struggle;
    float tired = 1 - std::clamp(f.stamina / std::max(1.0f, d.stamina), 0.0f, 1.0f);
    float rate = f.sprinting ? 4.2f : 3.0f - tired * 0.8f;
    float wantFlap = flying && (f.flapping || f.st == fl::FState::Struggle) ? 1.0f : 0.0f;
    S.flapAmt += (wantFlap - S.flapAmt) * std::min(1.0f, dt * 6);
    S.flapPh += dt * rate * 2 * PI * (0.3f + 0.7f * S.flapAmt) * (f.st == fl::FState::Struggle ? 1.8f : 1.0f);
    float wantFold = !flying ? 1.0f : f.st == fl::FState::Strike ? 0.85f : std::clamp((-f.pitch - 0.5f) * 1.6f, 0.0f, 0.9f);
    S.fold += (wantFold - S.fold) * std::min(1.0f, dt * 7);
    float wantFlare = flying && f.airspeed < 7 && f.st == fl::FState::Fly ? 1.0f : 0.0f;
    S.flare += (wantFlare - S.flare) * std::min(1.0f, dt * 5);
    float beat = sinf(S.flapPh);
    float shoulder = S.flapAmt * (0.25f + 0.75f * (1 - tired * 0.4f)) * 0.75f * beat + 0.08f * (1 - S.flapAmt) + 0.05f * sinf(S.t * 1.3f);
    float wrist = S.flapAmt * 0.45f * sinf(S.flapPh - 0.9f);
    SideLook look = ParseLook(const_cast<fl::World&>(w).LookOf(w.cur));
    gLook = look.costume ? &look.costume->look : nullptr;
    DrawBirdBody(d, W, shoulder, wrist, S.fold, S.flare, f.st == fl::FState::Strike ? 0.35f : -0.1f * f.pitch, 0.7f + 0.6f * S.flare + 0.2f * fabsf(f.bank));
    gLook = nullptr;
    DrawCarried(w, W, L, f.carrySp, f.carryTwigs, f.yaw, f.pitch);
    if (look.costume) DrawCostumeFx(look.costume->look, f.pos, f.vel, f.yaw, L, f.st == fl::FState::Strike || f.pitch < -0.6f, w.DayPhase() < 0.2f || w.DayPhase() > 0.85f);
}
// the colony's birds: smaller than the Founder (its species, the colony's look), posed from their motion; a warrior's
// role shows in its size and colouring (a small sleek Skirmisher, a dark Striker, a round pale Watcher, a gull-white
// Screamer, a black Flockmaster, a heavy Tank); a rival's birds wear its livery
void RoleLook(fl::Role r, float* scale, Color* tint) {
    switch (r) {
    case fl::Role::Skirmisher: *scale = 0.62f; *tint = {235, 235, 245, 255}; break;
    case fl::Role::Tank: *scale = 1.0f; *tint = {190, 196, 204, 255}; break;
    case fl::Role::Striker: *scale = 0.9f; *tint = {150, 120, 100, 255}; break;
    case fl::Role::Watcher: *scale = 0.8f; *tint = {226, 210, 180, 255}; break;
    case fl::Role::Screamer: *scale = 0.72f; *tint = {250, 250, 250, 255}; break;
    case fl::Role::Flockmaster: *scale = 0.86f; *tint = {90, 90, 100, 255}; break;
    default: *scale = 0.75f; *tint = WHITE; break;
    }
}
struct NameTag { Vector3 p; std::string name; Color c; };
std::vector<NameTag> gNames;   // (people's Founders and veterans, labelled after the 3D pass)
void DrawColonyBird(const fl::World& w, const fl::Bird& b, Color side, const SideLook* look = nullptr) {
    const fl::FounderDef& d = w.Def();
    float L = 0.22f + d.span * 0.14f;
    float sp = Vector3Length(b.vel);
    bool sitting = b.task == fl::Task::Sit || b.task == fl::Task::Eat || sp < 0.6f;
    bool diving = b.task == fl::Task::Dive || (b.flock >= 0 && b.vel.y < -6);
    float pitch = sitting ? 0 : std::clamp(asinf(std::clamp(b.vel.y / std::max(sp, 0.1f), -1.0f, 1.0f)), -1.2f, 0.6f);
    float sc; Color tint; RoleLook(b.role, &sc, &tint);
    if (b.netT > 0) tint = Tint(tint, {200, 200, 160, 255});
    Matrix W = PoseWorld(b.pos, b.yaw, pitch, b.netT > 0 ? 0.6f * sinf(S.t * 20) : 0, sc);
    float beat = sinf(b.flapPh);
    float shoulder = sitting ? 0 : diving ? 0.1f : 0.55f * beat + 0.05f;
    if (look && look->colour) side = Mix(WHITE, look->colour->c, 0.6f);   // (the colony's livery colour)
    gLook = look && look->hat ? &look->hatLook : nullptr;   // (and its hat)
    DrawBirdBody(d, W, shoulder, sitting ? 0 : 0.35f * sinf(b.flapPh - 0.9f), sitting ? 1.0f : diving ? 0.85f : 0.0f, 0, sitting ? 0.1f * sinf(S.t * 2 + b.id) : 0, 0.7f, Tint(tint, side));
    gLook = nullptr;
    DrawCarried(w, W, L * sc, b.carrySp, b.carryTwigs + b.carryShells, b.yaw, pitch);
    if (b.vet >= 0) {   // (a veteran: a feather in its cap, in its trait's colour)
        static const Color VC[5] = {{230, 60, 50, 255}, {80, 200, 90, 255}, {90, 170, 255, 255}, {240, 200, 60, 255}, {200, 120, 240, 255}};
        Matrix H = MatrixMultiply(MatrixTranslate(0, L * 0.25f, L * 0.42f), W);
        rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(L * 0.05f, L * 0.45f, L * 0.08f), MatrixRotateX(-0.5f)), H), VC[std::clamp(b.vet, 0, 4)]);
        if (Vector3Distance(b.pos, S.cam.position) < 40) gNames.push_back({Vector3Add(b.pos, {0, 0.9f, 0}), w.VetLabel(b), VC[std::clamp(b.vet, 0, 4)]});
    }
    if (b.role == fl::Role::Tank) rt::DrawCubeM(MatrixMultiply(MatrixScale(L * 0.5f, L * 0.08f, L * 0.7f), MatrixMultiply(MatrixTranslate(0, L * 0.22f, 0), W)), Color{214, 206, 190, 255});   // (shell armour on its back)
}
void DrawColony(const fl::World& w, const fl::Colony& c, const Camera3D& cam, Color side, const SideLook* look = nullptr) {
    // nests: built ones whole; under way, a flat ring that thickens with its twigs
    for (const auto& n : c.nests) {
        if (Vector3Distance(n.pos, cam.position) > 400) continue;
        float need = (float)((w.Def().key == "albatross" ? 2 : 1) * fl::Econ().nestTwigs);
        float k = n.built ? 1.0f : std::clamp(n.twigs / need, 0.05f, 1.0f);
        rt::DrawStatic(S.nest, MatrixMultiply(MatrixScale(0.6f + 0.4f * k, 0.3f + 0.7f * k, 0.6f + 0.4f * k), MatrixTranslate(n.pos.x, n.pos.y - 0.05f, n.pos.z)));
        if (n.built && n.shells >= fl::Econ().liningShells) rt::DrawStatic(S.egg, MatrixMultiply(MatrixScale(2.2f, 0.25f, 2.2f), MatrixTranslate(n.pos.x, n.pos.y + 0.03f, n.pos.z)), Color{236, 226, 210, 255});   // (the shell lining)
        // the courtship bowl: a row of cups beside the nest, lit as fish fill them
        if (n.built && n.mate < 0) for (int b = 0; b < n.bowlNeed; b++) {
            float a = b * 0.7f;
            Vector3 p{n.pos.x + 0.62f + 0.08f * cosf(a), n.pos.y + 0.06f, n.pos.z + 0.08f * sinf(a)};
            rt::DrawStatic(S.egg, MatrixMultiply(MatrixScale(0.6f, 0.35f, 0.6f), MatrixTranslate(p.x, p.y, p.z)), b < n.bowl ? Color{120, 200, 210, 255} : Color{90, 80, 70, 255});
        }
    }
    // eggs, chicks, mates, and the working birds
    for (const auto& b : c.birds) {
        if (!b.alive) continue;
        if (Vector3Distance(b.pos, cam.position) > 260) continue;
        if (b.stage == fl::BStage::Egg) {
            const fl::Nest& n = c.nests[b.nest];
            float a = b.id * 2.39f;
            rt::DrawStatic(S.egg, MatrixMultiply(MatrixRotateY(a), MatrixTranslate(n.pos.x + cosf(a) * 0.14f, n.pos.y + 0.08f, n.pos.z + sinf(a) * 0.14f)), b.chillT > 0.3f * fl::World::DAY ? Color{170, 180, 200, 255} : WHITE);
        } else if (b.stage == fl::BStage::Chick) {
            float bob = 0.04f * fabsf(sinf(S.t * 3 + b.id)), grow = 0.6f + 0.25f * std::min(1.0f, b.age / fl::Econ().chickDays);
            Matrix m = MatrixMultiply(MatrixMultiply(MatrixScale(grow, grow, grow), MatrixRotateY(b.id * 1.3f + 0.4f * sinf(S.t + b.id))), MatrixTranslate(b.pos.x, b.pos.y + bob, b.pos.z));
            rt::DrawStatic(S.chick, m, b.hunger < 0.25f ? Color{200, 170, 160, 255} : WHITE);
        } else DrawColonyBird(w, b, side, look);
    }
    // caches: a twig platform on the ground with its fish on it; the colony's twig stock beside the first
    for (size_t i = 0; i < c.caches.size(); i++) {
        const fl::Cache& ca = c.caches[i];
        if (Vector3Distance(ca.pos, cam.position) > 400) continue;
        float k = ca.built ? 1.0f : std::clamp(ca.twigs / fl::Econ().cacheTwigs, 0.1f, 1.0f);
        rt::DrawStatic(S.pile, MatrixMultiply(MatrixScale(k, k, k), MatrixTranslate(ca.pos.x, ca.pos.y, ca.pos.z)));
        for (size_t f = 0; f < ca.fish.size() && f < 10; f++) {
            const rt::CreatureModel& cm = rt::Creature(w.seaKey, w.eco.map->species[ca.fish[f].sp].name);
            float a = f * 2.4f, r = 0.15f + 0.07f * f;
            Color tint = ca.fish[f].age > 1.5f * fl::World::DAY ? Color{170, 180, 130, 255} : WHITE;   // (going off)
            rt::DrawCreature(cm, {ca.pos.x + cosf(a) * r, ca.pos.y + 0.15f + 0.02f * f, ca.pos.z + sinf(a) * r}, a, 0, 0.8f, 0, 0, tint);
        }
    }
    if (!c.caches.empty()) for (int k = 0; k < std::min(c.twigs, 24); k++) {
        Vector3 p = Vector3Add(c.caches[0].pos, {-1.4f + 0.05f * (k % 5), 0.05f + 0.035f * (k / 5), 0.9f});
        rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.7f, 0.035f, 0.035f), MatrixRotateY(k * 0.9f)), MatrixTranslate(p.x, p.y, p.z)), Color{122, 92, 58, 255});
    }
    // defences: a hedge of thorn (a ring of bramble round the cache and the home nest), a tower of lashed sticks
    for (const auto& s : c.builds) {
        if (Vector3Distance(s.pos, cam.position) > 400) continue;
        if (s.hp <= 0) continue;
        float k = s.built ? 1.0f : std::clamp(s.twigs / std::max(1, fl::StructureTwigs(s.kind)), 0.1f, 1.0f);
        if (s.kind == 0) for (int j = 0; j < 16; j++) {
            float a = j * 2 * PI / 16; Vector3 p = Vector3Add(s.pos, {cosf(a) * 7, 0, sinf(a) * 7}); p.y = w.HeightAt(p.x, p.z);
            rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(2.8f, 1.4f * k, 0.9f), MatrixRotateY(-a + PI / 2)), MatrixTranslate(p.x, p.y + 0.6f * k, p.z)), Color{70, 96, 52, 255});
        }
        else if (s.kind == fl::ST_ROOST) {   // a Roost: a broad low platform of sticks under a lean-to of fronds
            for (int j = 0; j < 6; j++) { float a = j * PI / 3; rt::DrawCubeM(MatrixMultiply(MatrixScale(0.3f, 3 * k, 0.3f), MatrixTranslate(s.pos.x + cosf(a) * 4, s.pos.y + 1.5f * k, s.pos.z + sinf(a) * 4)), Color{112, 86, 56, 255}); }
            rt::DrawCubeM(MatrixMultiply(MatrixScale(9, 0.4f, 9), MatrixTranslate(s.pos.x, s.pos.y + 0.3f, s.pos.z)), Color{132, 100, 64, 255});
            if (k >= 1) for (int j = 0; j < 5; j++) rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(10, 0.15f, 2.2f), MatrixRotateX(0.35f)), MatrixTranslate(s.pos.x, s.pos.y + 3.2f, s.pos.z - 4 + 2 * j)), Color{86, 120, 60, 255});
        }
        else if (s.kind == fl::ST_SHRINE) {   // a shrine: a cairn of shells and pearls with a feather totem
            for (int j = 0; j < 5; j++) rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(2.4f - 0.4f * j, 0.7f, 2.4f - 0.4f * j), MatrixRotateY(j * 0.6f)), MatrixTranslate(s.pos.x, s.pos.y + 0.35f + 0.7f * j * k, s.pos.z)), Color{(unsigned char)(220 - 10 * j), (unsigned char)(210 - 10 * j), 196, 255});
            if (k >= 1) { rt::DrawCubeM(MatrixMultiply(MatrixScale(0.2f, 4, 0.2f), MatrixTranslate(s.pos.x, s.pos.y + 5, s.pos.z)), Color{112, 86, 56, 255}); rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.5f, 0.5f, 0.5f), MatrixTranslate(s.pos.x, s.pos.y + 7.2f, s.pos.z)), Color{240, 240, 255, 255}, 0.8f + 0.4f * sinf(S.t * 2)); }
        }
        else if (s.kind == fl::ST_WORKS) {   // the Works: a guano pit, a sulfur heap, a smoking kiln of stones
            rt::DrawCubeM(MatrixMultiply(MatrixScale(4, 0.5f, 4), MatrixTranslate(s.pos.x - 3, s.pos.y + 0.1f, s.pos.z)), Color{226, 224, 210, 255});
            rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(2.5f, 1.2f, 2.5f), MatrixRotateY(0.7f)), MatrixTranslate(s.pos.x + 3, s.pos.y + 0.5f, s.pos.z)), Color{222, 200, 60, 255});
            rt::DrawCubeM(MatrixMultiply(MatrixScale(2, 3 * k, 2), MatrixTranslate(s.pos.x, s.pos.y + 1.5f * k, s.pos.z + 3)), Color{110, 104, 98, 255});
            if (k >= 1) { rt::DrawCubeGlow(MatrixMultiply(MatrixScale(1, 0.6f, 1), MatrixTranslate(s.pos.x, s.pos.y + 0.6f, s.pos.z + 4.1f)), Color{255, 140, 50, 255}, 1.2f);
                for (int j = 0; j < 4; j++) { float ph = fmodf(S.t * 0.3f + j * 0.25f, 1.0f); rt::DrawCubeM(MatrixMultiply(MatrixScale(1 + ph * 2, 1 + ph * 2, 1 + ph * 2), MatrixTranslate(s.pos.x + ph * 2, s.pos.y + 3.5f + ph * 10, s.pos.z + 3)), Mix(Color{120, 116, 112, 255}, Color{200, 200, 198, 255}, ph)); } }
        }
        else {
            for (int j = 0; j < 4; j++) { float a = j * PI / 2 + 0.78f; rt::DrawCubeM(MatrixMultiply(MatrixScale(0.25f, 16 * k, 0.25f), MatrixTranslate(s.pos.x + cosf(a) * 1.2f, s.pos.y + 8 * k, s.pos.z + sinf(a) * 1.2f)), Color{112, 86, 56, 255}); }
            if (k >= 1) rt::DrawCubeM(MatrixMultiply(MatrixScale(3.2f, 0.3f, 3.2f), MatrixTranslate(s.pos.x, s.pos.y + 16, s.pos.z)), Color{132, 100, 64, 255});
        }
    }
    // driftwood and shells on the beach (what the builders gather)
    for (const auto& s : c.twigSrc) {
        if (Vector3Distance(s.pos, cam.position) > 120) continue;
        if (s.shells) { for (int k = 0; k < (int)s.twigs; k++) rt::DrawStatic(S.egg, MatrixMultiply(MatrixScale(0.7f, 0.3f, 0.7f), MatrixTranslate(s.pos.x + 0.25f * k, s.pos.y, s.pos.z + 0.1f * k)), Color{236, 214, 200, 255}); }
        else if (s.cap <= 3.5f && s.twigs >= 1) rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(1.6f, 0.14f, 0.14f), MatrixRotateY(s.pos.x)), MatrixTranslate(s.pos.x, s.pos.y, s.pos.z)), Color{150, 132, 106, 255});
    }
}
// the war on screen: feathers where a blow lands, a net's flash, a bird falling into the sea (a splash, then blood on
// the water where it went in)
struct Feather { Vector3 p, v; float life, spin; Color c; };
struct Faller { Vector3 p, v; float yaw, spin; Color tint; fl::Role role; bool splashed; };
struct Stain { Vector2 p; float age; };
std::vector<Feather> gFeathers; std::vector<Faller> gFallers; std::vector<Stain> gStains;
size_t gFxSeen = 0;
void StepWarFx(const fl::World& w, float dt) {
    size_t end = w.warFxBase + w.warFx.size();
    if (gFxSeen < w.warFxBase) gFxSeen = w.warFxBase;
    for (; gFxSeen < end; gFxSeen++) {
        const auto& e = w.warFx[gFxSeen - w.warFxBase];
        if (Vector3Distance(e.p, S.cam.position) > 300) continue;
        Color c = w.SideColor(e.side);
        int n = e.kind == 1 ? 14 : e.kind == 2 ? 6 : 5;
        for (int k = 0; k < n; k++) { float a = Hash((float)gFxSeen, (float)k) * 6.28f, u = Hash((float)k, (float)gFxSeen); gFeathers.push_back({e.p, {cosf(a) * (1.5f + 2 * u), 1 + 2 * u, sinf(a) * (1.5f + 2 * u)}, 1.2f + u, a, e.kind == 2 ? Color{230, 230, 190, 255} : Mix(c, WHITE, 0.4f)}); }
        if (e.kind == 1) { float sc; Color tint; RoleLook(e.role, &sc, &tint); gFallers.push_back({e.p, {0, -2, 0}, e.yaw, 0, Tint(tint, c), e.role, false}); }
    }
    for (auto& f : gFeathers) { f.v.y -= 1.2f * dt; f.v = Vector3Scale(f.v, 1 - 1.5f * dt); f.p = Vector3Add(f.p, Vector3Scale(f.v, dt)); f.life -= dt; f.spin += dt * 6; }
    gFeathers.erase(std::remove_if(gFeathers.begin(), gFeathers.end(), [](const Feather& f) { return f.life <= 0; }), gFeathers.end());
    for (auto& f : gFallers) {
        if (f.splashed) continue;
        f.v.y -= 9.8f * dt; f.p = Vector3Add(f.p, Vector3Scale(f.v, dt)); f.spin += dt * 5;
        float g = std::max(0.0f, w.HeightAt(f.p.x, f.p.z));
        if (f.p.y <= g) { f.splashed = true; if (g <= 0.01f) { gStains.push_back({{f.p.x, f.p.z}, 0}); for (int k = 0; k < 10; k++) { float a = k * 0.63f; gFeathers.push_back({{f.p.x, 0.1f, f.p.z}, {cosf(a) * 2.5f, 3.5f, sinf(a) * 2.5f}, 0.7f, a, Color{230, 240, 245, 255}}); } } f.p.y = g; }
    }
    gFallers.erase(std::remove_if(gFallers.begin(), gFallers.end(), [](const Faller& f) { return f.splashed && f.p.y <= 0.01f; }), gFallers.end());
    for (auto& s : gStains) s.age += dt;
    gStains.erase(std::remove_if(gStains.begin(), gStains.end(), [](const Stain& s) { return s.age > 40; }), gStains.end());
    if (gFallers.size() > 60) gFallers.erase(gFallers.begin(), gFallers.begin() + 20);
}
void DrawWarFx(const fl::World& w) {
    for (const auto& f : gFeathers) rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.12f, 0.01f, 0.04f), MatrixRotateY(f.spin)), MatrixTranslate(f.p.x, f.p.y, f.p.z)), f.c);
    for (const auto& f : gFallers) {
        float sc; Color tint; RoleLook(f.role, &sc, &tint);
        Matrix W = PoseWorld(f.p, f.yaw, -0.6f, f.spin, sc);
        DrawBirdBody(w.Def(), W, 0.6f * sinf(f.spin * 2), 0.3f, 0.2f, 0, 0.5f, 0.6f, f.tint);   // (limp, tumbling)
    }
}
// the dangerous islands on screen (stage 7): the kraken's arms and its head when it surfaces, an arm snatching a bird;
// the great ape on skull island (asleep when fed) and its thrown rock; the volcano's plume, and its ash and lava in an
// eruption; the wreck's ghost light; a bomb's blast (a flash and a ball of smoke)
struct Blast { Vector3 p; float age; int kind; };
std::vector<Blast> gBlasts; size_t gBlastSeen = 0;
void BuildDangerModels() {
    if (S.dangerReady) return;
    {   // one kraken arm: a long tapering curl (unit height), dark red with paler suckers along its inside
        rt::MeshBuilder mb;
        std::vector<Vector3> spine;
        for (int k = 0; k <= 10; k++) { float u = k / 10.0f; spine.push_back({0.25f * sinf(u * 2.6f), u, 0.18f * (1 - cosf(u * 2.2f))}); }
        mb.Tube(spine, 0.11f, 0.015f, 7, Color{138, 40, 44, 255}, Color{170, 70, 66, 255}, 0);
        for (int k = 1; k < 9; k++) { const Vector3& p = spine[k]; mb.Octa({p.x + 0.06f, p.y, p.z - 0.07f}, 0.03f * (1 - k / 10.0f), Color{226, 180, 160, 255}); }
        S.krakenArm = LoadModelFromMesh(mb.Build());
    }
    {   // its head: a mantle rising out of the water, two great eyes
        rt::MeshBuilder mb;
        mb.Tube({{0, -1.0f, 0}, {0, 0, 0}, {0, 0.55f, -0.08f}, {0, 0.95f, -0.25f}, {0, 1.15f, -0.45f}}, 0.5f, 0.12f, 12, Color{128, 36, 40, 255}, Color{96, 28, 34, 255}, 0);
        mb.Octa({0.42f, 0.05f, 0.28f}, 0.09f, Color{250, 220, 120, 255}); mb.Octa({-0.42f, 0.05f, 0.28f}, 0.09f, Color{250, 220, 120, 255});
        mb.Octa({0.44f, 0.05f, 0.33f}, 0.04f, Color{20, 16, 14, 255}); mb.Octa({-0.44f, 0.05f, 0.33f}, 0.04f, Color{20, 16, 14, 255});
        S.krakenHead = LoadModelFromMesh(mb.Build());
    }
    {   // the great ape: a hunched body, a head, long arms on their knuckles (unit height, scaled up)
        rt::MeshBuilder mb;
        Color fur{58, 46, 40, 255}, face{120, 96, 80, 255};
        mb.Tube({{0, 0.25f, -0.1f}, {0, 0.7f, 0.0f}, {0, 1.05f, 0.12f}}, 0.4f, 0.32f, 9, Shade(fur, 0.85f), fur, 0);
        mb.Tube({{0, 1.1f, 0.18f}, {0, 1.3f, 0.26f}, {0, 1.42f, 0.28f}}, 0.2f, 0.12f, 8, fur, Shade(fur, 0.9f), 0);
        mb.Box({0, 1.24f, 0.4f}, {0.1f, 0.07f, 0.04f}, face);
        for (int sd = -1; sd <= 1; sd += 2) {
            mb.Tube({{0.36f * sd, 1.0f, 0.1f}, {0.55f * sd, 0.6f, 0.25f}, {0.5f * sd, 0.05f, 0.35f}}, 0.12f, 0.09f, 6, fur, Shade(fur, 0.85f), 0);
            mb.Tube({{0.18f * sd, 0.3f, -0.05f}, {0.25f * sd, 0.15f, 0.05f}, {0.22f * sd, 0.0f, 0.0f}}, 0.13f, 0.1f, 6, fur, Shade(fur, 0.85f), 0);
            mb.Octa({0.07f * sd, 1.3f, 0.38f}, 0.03f, Color{230, 200, 120, 255});
        }
        S.ape = LoadModelFromMesh(mb.Build());
    }
    S.dangerReady = true;
}
void DrawDangers(const fl::World& w, const Camera3D& cam, float dt) {
    if (!w.wholeMap) return;
    BuildDangerModels();
    // ---- the kraken: asleep, nothing shows; awake, arms test the air; surfaced, its head too
    const fl::Kraken& K = w.kraken;
    if (K.isle >= 0 && !K.dead) {
        Vector3 c = w.isles[K.isle].c;
        if (Vector3Distance(c, cam.position) < 900) {
            int arms = K.mood == 2 ? 8 : K.mood == 1 ? 4 : 0;
            for (int a = 0; a < arms; a++) {
                float ang = a * 2 * PI / arms + 0.3f * sinf(S.t * 0.4f + a);
                float r = 16 + 14 * Hash((float)a, 3.0f);
                float len = (K.mood == 2 ? 26 : 14) * (0.8f + 0.4f * Hash((float)a, 7.0f)) * (0.85f + 0.15f * sinf(S.t * 1.3f + a));
                Matrix m = MatrixMultiply(MatrixMultiply(MatrixScale(len * 0.6f, len, len * 0.6f), MatrixRotateZ(0.35f * sinf(S.t * 0.9f + a * 1.7f))), MatrixMultiply(MatrixRotateY(-ang), MatrixTranslate(c.x + cosf(ang) * r, -1.0f, c.z + sinf(ang) * r)));
                rt::DrawStatic(S.krakenArm, m);
            }
            if (K.mood == 2) rt::DrawStatic(S.krakenHead, MatrixMultiply(MatrixScale(14, 10, 14), MatrixTranslate(c.x, -4.5f + 0.6f * sinf(S.t * 0.7f), c.z)));
        }
        if (K.armT > 0) {   // an arm out of the water where a bird was taken
            float k = std::clamp(K.armT / 1.2f, 0.0f, 1.0f);
            float hgt = std::max(4.0f, K.arm.y + 4) * (0.4f + 0.6f * k);
            rt::DrawStatic(S.krakenArm, MatrixMultiply(MatrixScale(hgt * 0.5f, hgt, hgt * 0.5f), MatrixTranslate(K.arm.x, -1.0f, K.arm.z)));
        }
    }
    // ---- the great ape on skull island's summit, and its rock in the air
    const fl::Ape& A = w.ape;
    if (A.isle >= 0 && Vector3Distance(A.pos, cam.position) < 900) {
        bool sleeping = A.sleepT > 0;
        Matrix m = sleeping ? MatrixMultiply(MatrixMultiply(MatrixScale(7, 7, 7), MatrixRotateX(-PI * 0.45f)), MatrixTranslate(A.pos.x, A.pos.y + 1.5f, A.pos.z))
                            : MatrixMultiply(MatrixMultiply(MatrixScale(7, 7 * (1 + 0.02f * sinf(S.t * 1.7f)), 7), MatrixRotateY(0.6f * sinf(S.t * 0.15f))), MatrixTranslate(A.pos.x, A.pos.y - 0.5f, A.pos.z));
        rt::DrawStatic(S.ape, m);
        if (A.rockT > 0) {
            float k = std::clamp(1 - A.rockT, 0.0f, 1.0f);
            Vector3 p = Vector3Lerp(A.rockFrom, A.rockTo, k); p.y += 30 * sinf(k * PI);
            rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(1.6f, 1.4f, 1.5f), MatrixRotateY(S.t * 7)), MatrixTranslate(p.x, p.y, p.z)), Color{96, 90, 82, 255});
        }
    }
    // ---- the volcano: a plume always; in an eruption a column of ash and lava down its flanks; tremors shake the summit
    const fl::Volcano& V = w.volcano;
    if (V.isle >= 0) {
        const fl::Island& is = w.isles[V.isle];
        Vector3 top{is.hill.x, is.hill.y + 2, is.hill.z};
        if (Vector3Distance(top, cam.position) < 1400) {
            bool erupting = V.ashT > 0;
            int puffs = erupting ? 44 : 12;
            for (int k = 0; k < puffs; k++) {
                float ph = fmodf(S.t * (erupting ? 0.35f : 0.12f) + k / (float)puffs, 1.0f);
                float h = ph * (erupting ? 220 : 90);
                Vector3 p{top.x + sinf(k * 2.3f + S.t * 0.1f) * (4 + h * 0.18f) + w.wind.dir.x * h * 0.3f, top.y + h, top.z + cosf(k * 1.7f) * (4 + h * 0.18f) + w.wind.dir.y * h * 0.3f};
                float s = (erupting ? 12 : 5) + h * 0.1f;
                Color c = erupting ? Mix(Color{60, 50, 48, 255}, Color{130, 120, 116, 255}, ph) : Mix(Color{150, 146, 140, 255}, Color{210, 210, 206, 255}, ph);
                rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(s, s * 0.8f, s), MatrixRotateY(k * 0.7f)), MatrixTranslate(p.x, p.y, p.z)), c);
            }
            rt::DrawCubeGlow(MatrixMultiply(MatrixScale(5, 0.8f, 5), MatrixTranslate(top.x, top.y - 1.5f, top.z)), Color{255, 120, 40, 255}, erupting ? 2.0f : 0.8f);
            if (erupting) for (int k = 0; k < 18; k++) {
                float a = k * 0.7f, r = 8 + fmodf(k * 13.7f + S.t * 3, 70);
                Vector3 p{top.x + cosf(a) * r, 0, top.z + sinf(a) * r}; p.y = w.HeightAt(p.x, p.z) + 0.3f;
                if (p.y > 0.5f) rt::DrawCubeGlow(MatrixMultiply(MatrixScale(3, 0.6f, 3), MatrixTranslate(p.x, p.y, p.z)), Color{255, 110, 30, 255}, 1.5f);
            }
        }
    }
    // ---- the wreck's ghost light at night: lanterns along her rail
    if (w.wreck.isle >= 0 && !w.wreck.gone && w.wreck.ghostT > 0) {
        Vector3 c = w.isles[w.wreck.isle].hill;
        for (int k = 0; k < 5; k++) rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.7f, 1.0f, 0.7f), MatrixTranslate(c.x - 12 + 6 * k, c.y + 3 + 0.4f * sinf(S.t * 2 + k), c.z)), Color{120, 255, 190, 255}, 1.6f);
    }
    // ---- the neutral factions: the pirate band, the fleet's boats, the Grey Wings over their crag
    if (w.pirates.on && w.time >= w.pirates.scatterUntil && Vector3Distance(w.pirates.pos, cam.position) < 500)
        for (int k = 0; k < 8; k++) { float a = S.t * 0.8f + k * 0.785f; Vector3 p = Vector3Add(w.pirates.pos, {cosf(a) * 6, 2 * sinf(S.t + k), sinf(a) * 6}); float bt = sinf(S.t * 8 + k);
            DrawBirdBody(w.Def(), PoseWorld(p, a + PI * 0.5f, 0, 0.3f, 0.8f), 0.5f * bt, 0.3f * bt, 0, 0, 0, 0.9f, Color{40, 40, 46, 255}); }
    for (const auto& b : w.fleet) if (Vector3Distance(b.pos, cam.position) < 600) {
        float yaw = atan2f(b.pos.z, b.pos.x);
        rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(7, 1.4f, 2.4f), MatrixRotateY(yaw)), MatrixTranslate(b.pos.x, 0.3f, b.pos.z)), Color{150, 100, 60, 255});
        rt::DrawCubeM(MatrixMultiply(MatrixScale(0.25f, 6, 0.25f), MatrixTranslate(b.pos.x, 3.5f, b.pos.z)), Color{120, 90, 60, 255});
        rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.1f, 4, 3), MatrixRotateY(yaw)), MatrixTranslate(b.pos.x + 0.3f, 4, b.pos.z)), Color{236, 230, 214, 255});
    }
    if (w.grey.isle >= 0 && !w.grey.dead && Vector3Distance(w.grey.crag, cam.position) < 700)
        for (int k = 0; k < 3; k++) { float a = S.t * 0.3f + k * 2.1f; Vector3 p = w.grey.hunterT > 0 && k == 0 ? w.grey.hunter : Vector3Add(w.grey.crag, {cosf(a) * 20, 12.0f + 3.0f * k, sinf(a) * 20});
            DrawBirdBody(w.Def(), PoseWorld(p, a + PI * 0.5f, 0, 0.4f, 1.6f), 0.1f, 0, 0, 0, 0, 1.0f, Color{120, 90, 60, 255}); }
    // ---- the long match: relics glinting where they lie; the treasure ship's wreck; the Visitor on its island
    for (const auto& r : w.relicSpots) if (!r.taken && Vector3Distance(r.pos, cam.position) < 500) {
        float k = 0.6f + 0.4f * sinf(S.t * 3 + r.relic);
        rt::DrawCubeGlow(MatrixMultiply(MatrixMultiply(MatrixScale(0.7f, 0.7f, 0.7f), MatrixRotateY(S.t)), MatrixTranslate(r.pos.x, r.pos.y + 0.5f, r.pos.z)), Color{255, 230, 140, 255}, 1.5f * k);
        rt::DrawCubeM(MatrixMultiply(MatrixScale(1.6f, 0.4f, 1.6f), MatrixTranslate(r.pos.x, r.pos.y - 0.1f, r.pos.z)), Color{120, 110, 96, 255});
    }
    if (w.treasure > 0 && Vector3Distance(w.greatPos, cam.position) < 900) {
        Vector3 p = w.greatPos;
        rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(18, 3, 5), MatrixRotateZ(0.25f)), MatrixTranslate(p.x, -0.5f, p.z)), Color{96, 70, 48, 255});
        rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.5f, 12, 0.5f), MatrixRotateZ(0.5f)), MatrixTranslate(p.x + 2, 4, p.z)), Color{110, 84, 58, 255});
        rt::DrawCubeGlow(MatrixMultiply(MatrixScale(1.4f, 0.6f, 1.4f), MatrixTranslate(p.x - 3, 0.6f, p.z)), Color{240, 240, 255, 255}, 1.2f + 0.5f * sinf(S.t * 4));
    }
    if (w.legendFree >= 0 && Vector3Distance(w.legendPos, cam.position) < 600) {   // (a legendary bird: twice the size, gilded)
        float bob = 0.3f * sinf(S.t * 1.5f);
        DrawBirdBody(w.Def(), PoseWorld(Vector3Add(w.legendPos, {0, bob, 0}), S.t * 0.2f, 0, 0, 2.0f), 0, 0, 1.0f, 0, 0.1f * sinf(S.t), 0.8f, Color{255, 220, 140, 255});
        rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.4f, 0.4f, 0.4f), MatrixTranslate(w.legendPos.x, w.legendPos.y + 2.5f, w.legendPos.z)), Color{255, 240, 180, 255}, 2.0f);
    }
    // ---- bomb blasts
    size_t end = w.warFxBase + w.warFx.size();
    if (gBlastSeen < w.warFxBase || gBlastSeen > end) gBlastSeen = w.warFxBase;
    for (; gBlastSeen < end; gBlastSeen++) { const auto& e = w.warFx[gBlastSeen - w.warFxBase]; if (e.kind == 3 && Vector3Distance(e.p, cam.position) < 700) gBlasts.push_back({e.p, 0, (int)e.yaw}); }
    for (auto& b : gBlasts) b.age += dt;
    gBlasts.erase(std::remove_if(gBlasts.begin(), gBlasts.end(), [](const Blast& b) { return b.age > 3; }), gBlasts.end());
    for (const auto& b : gBlasts) {
        float k = b.age / 3, s = (b.kind == 2 ? 8 : 4) + 14 * sqrtf(k);
        if (b.age < 0.4f) rt::DrawCubeGlow(MatrixMultiply(MatrixScale(s * 1.2f, s, s * 1.2f), MatrixTranslate(b.p.x, b.p.y + 2, b.p.z)), b.kind == 1 ? Color{255, 120, 20, 255} : Color{255, 230, 160, 255}, 3.0f);
        for (int j = 0; j < 7; j++) {   // a ball of smoke: puffs rolling up and out, paling as they go
            float a = j * 0.9f + b.p.x, r = (j == 0 ? 0 : 0.5f) * s, ps = s * (0.45f - 0.03f * j) * (0.6f + k);
            rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(ps, ps * 0.8f, ps), MatrixRotateY(a + b.age)), MatrixTranslate(b.p.x + cosf(a) * r, b.p.y + 2 + 9 * k + (j % 3) * ps * 0.4f, b.p.z + sinf(a) * r)), Mix(Color{92, 86, 80, 255}, Color{176, 172, 166, 255}, k + 0.05f * j));
        }
        if (b.kind == 1 && b.age > 0.3f) for (int j = 0; j < 6; j++) { float a = j * 1.05f; rt::DrawCubeGlow(MatrixMultiply(MatrixScale(1.2f, 1.6f + sinf(S.t * 9 + j), 1.2f), MatrixTranslate(b.p.x + cosf(a) * 4, b.p.y + 0.8f, b.p.z + sinf(a) * 4)), Color{255, 140, 40, 255}, 2.0f); }
    }
}
void DrawWorld(const fl::World& w, const Camera3D& cam, float dt) {
    rt::DrawWorldCube({cam.position.x, -25.6f, cam.position.z}, {4000, 0.4f, 4000}, Color{52, 84, 96, 255});   // (the open sea's floor)
    size_t nIsles = w.wholeMap ? w.isles.size() : 1;   // (stage 1: the one island)
    for (size_t i = 0; i < nIsles; i++) {
        const fl::Island& is = w.wholeMap ? w.isles[i] : w.island;
        float d = Vector2Distance({cam.position.x, cam.position.z}, {is.c.x, is.c.z}) - is.radius;
        if (d > 1100) continue;   // (beyond the haze)
        if (i < S.terr.size()) rt::DrawStatic(S.terr[i], MatrixIdentity());
        // trees
        if (d < 450) for (size_t k = 0; k < is.palms.size(); k++) {
            const Vector3& p = is.palms[k];
            float h = k < is.palmH.size() ? is.palmH[k] : 7;
            float sway = 0.03f * sinf(S.t * 0.8f + k);
            Matrix m = MatrixMultiply(MatrixMultiply(MatrixScale(h, h, h), MatrixRotateY(Hash(p.z, p.x) * 6.28f)), MatrixRotateZ(sway));
            rt::DrawStatic(S.palm, MatrixMultiply(m, MatrixTranslate(p.x, p.y - 0.2f, p.z)));
        }
        // the town's houses, roofs, tower, docks, boats and woodpile; the wreck's hull and masts
        for (const auto& pr : is.props) {
            static const Color PC[] = {{226, 218, 200, 255}, {170, 70, 52, 255}, {200, 192, 176, 255}, {132, 100, 66, 255}, {116, 84, 56, 255}, {150, 112, 70, 255}, {78, 64, 52, 255}, {96, 80, 64, 255}};
            float bob = pr.kind == 4 ? 0.12f * sinf(S.t * 1.3f + pr.c.x) : pr.kind == 6 ? 0.25f * sinf(S.t * 0.4f) : 0;
            Matrix m = MatrixMultiply(MatrixMultiply(MatrixScale(pr.half.x * 2, pr.half.y * 2, pr.half.z * 2), MatrixRotateY(-pr.yaw)), MatrixTranslate(pr.c.x, pr.c.y + bob, pr.c.z));
            rt::DrawCubeM(m, PC[std::clamp(pr.kind, 0, 7)]);
        }
    }
    fl::World& wm = const_cast<fl::World&>(w);
    bool night = w.DayPhase() < 0.2f || w.DayPhase() > 0.85f;
    { SideLook mine = ParseLook(wm.LookOf(w.cur)); DrawColony(w, w.col, cam, WHITE, &mine); }
    for (int s = 0; s <= (int)w.sides.size(); s++) {
        if (s == w.cur) continue;
        SideLook look = ParseLook(wm.LookOf(s));
        Color sc = look.colour ? look.colour->c : w.SideColor(s);
        DrawColony(w, w.ColOf(s), cam, w.SideColor(s), &look);
        const fl::Founder& f = w.FounderOf(s);
        if (f.st == fl::FState::Dead || f.st == fl::FState::Under || Vector3Distance(f.pos, cam.position) > 260) continue;
        bool flying = f.st == fl::FState::Fly || f.st == fl::FState::Strike;
        gLook = look.costume ? &look.costume->look : nullptr;
        DrawBirdBody(w.Def(), PoseWorld(f.pos, f.yaw, flying ? f.pitch : 0, flying ? f.bank : 0, f.chick ? 0.7f : 1.0f), flying ? 0.5f * sinf(S.t * 7 + s) : 0, flying ? 0.25f : 0, flying ? 0.1f : 1.0f, 0, 0, 0.7f, sc);
        gLook = nullptr;
        if (look.costume) DrawCostumeFx(look.costume->look, f.pos, f.vel, f.yaw, 0.22f + w.Def().span * 0.14f, f.st == fl::FState::Strike, night);
        DrawCarried(w, PoseWorld(f.pos, f.yaw, 0, 0, 1.0f), 0.22f + w.Def().span * 0.14f, f.carrySp, f.carryTwigs, f.yaw, f.pitch);
        if (w.multi && Vector3Distance(f.pos, cam.position) < 120) {   // (a person's Founder: their name over it)
            Vector3 fwd = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
            if (Vector3DotProduct(Vector3Subtract(f.pos, cam.position), fwd) > 0) { gNames.push_back({Vector3Add(f.pos, {0, 1.6f, 0}), w.SideName(s) + (look.costume && look.costume->look.label == "best" ? TextFormat("  (best %d)", look.best) : ""), w.SideColor(s)}); }   // (the Founder's Crown shows the best score)
        }
    }
    DrawWarFx(w);
    DrawDangers(w, cam, dt);
    // the sea's life: what's near enough to see (the water drawn over it shows the shallow ones plainly)
    for (const auto& a : w.eco.agents) {
        if (!a.alive || a.diver >= 0) continue;
        if (Vector3Distance(a.pos, cam.position) > 110) continue;
        const rt::Species& sp = w.eco.map->species[a.sp];
        const rt::CreatureModel& cm = rt::Creature(w.seaKey, sp.name);
        float spd = Vector3Length(a.vel);
        Vector3 v = spd > 0.02f ? a.vel : Vector3{sinf(a.rng * 0.001f), 0, cosf(a.rng * 0.001f)};
        float yaw = atan2f(v.x, v.z), pitch = spd > 0.05f ? std::clamp(asinf(std::clamp(v.y / std::max(spd, 1e-3f), -1.0f, 1.0f)), -0.6f, 0.6f) : 0;
        float inten = std::clamp(0.35f + spd / std::max(0.5f, sp.speed), 0.2f, 1.6f);
        if (a.held > 0) inten = 1.6f;
        Color tint = a.wound > 0.3f ? Color{255, (unsigned char)(255 - a.wound * 120), (unsigned char)(255 - a.wound * 120), 255} : WHITE;
        rt::DrawCreature(cm, a.pos, yaw, pitch, 1.0f, S.t * cm.freq * (0.6f + inten * 0.6f) + (a.rng % 1000) * 0.01f, inten, tint);
    }
    DrawBird(w, dt);
}

void DrawBar(float x, float y, float w, float h, float k, Color c, const char* label) {
    DrawRectangleRounded({x - 2, y - 2, w + 4, h + 4}, 0.4f, 6, Fade(BLACK, 0.45f));
    DrawRectangleRounded({x, y, w * std::clamp(k, 0.0f, 1.0f), h}, 0.4f, 6, c);
    TxtBold(label, x + 6, y - 1, (int)h, Color{250, 250, 245, 230});
}
const char* AltBand(float y) { return y < 3 ? "wave-top" : y < 15 ? "low" : y < 60 ? "cruise" : "high"; }
void DrawHud(const fl::World& w) {
    const fl::Founder& f = w.me;
    const fl::FounderDef& d = w.Def();
    Color ink{250, 248, 236, 255}, dim{220, 230, 230, 200};
    // the strike's slow motion: the edges darken, the talons' aim on the water, the fish in reach ringed
    if (f.st == fl::FState::Strike) {
        for (int k = 0; k < 6; k++) DrawRectangleLinesEx({(float)k * 10, (float)k * 10, SCREEN_W - k * 20.0f, SCREEN_H - k * 20.0f}, 10, Fade(BLACK, 0.12f));
        Vector2 a = GetWorldToScreenEx(f.strikeAim, S.cam, SCREEN_W, SCREEN_H);
        DrawRing(a, 16, 19, 0, 360, 24, Color{255, 230, 120, 230});
        DrawLineEx({a.x - 26, a.y}, {a.x - 10, a.y}, 2, Color{255, 230, 120, 230}); DrawLineEx({a.x + 10, a.y}, {a.x + 26, a.y}, 2, Color{255, 230, 120, 230});
        for (const auto& ag : w.eco.agents) {
            if (!ag.alive || ag.diver >= 0 || ag.pos.y < -3) continue;
            if (Vector2Distance({ag.pos.x, ag.pos.z}, {f.strikeAim.x, f.strikeAim.z}) > 4.5f) continue;
            Vector2 p = GetWorldToScreenEx(ag.pos, S.cam, SCREEN_W, SCREEN_H);
            bool lift = w.eco.map->species[ag.sp].size <= f.Carry(d);
            DrawRing(p, 9, 11, 0, 360, 16, lift ? Color{140, 255, 170, 220} : Color{255, 120, 100, 220});
        }
        DrawTextCenteredBold("STRIKE", SCREEN_W / 2.0f, 90, 34, Color{255, 236, 160, 255});
        float k = 1 - f.strikeT / std::max(0.01f, f.strikeLen);
        DrawRectangle(SCREEN_W / 2 - 120, 132, (int)(240 * k), 6, Color{255, 236, 160, 220});
        DrawTextCentered("steer the talons with the mouse (or WASD)", SCREEN_W / 2.0f, 144, 15, dim);
    }
    // the other people's Founders, named
    for (const auto& n : gNames) {
        Vector2 p = GetWorldToScreenEx(n.p, S.cam, SCREEN_W, SCREEN_H);
        DrawTextCenteredBold(n.name, p.x, p.y, 15, n.c);
    }
    gNames.clear();
    // a networked match: the time left and the standings (top middle)
    if (w.multi) {
        float left = std::max(0.0f, w.matchLen - w.time);
        DrawRectangleRounded({SCREEN_W / 2.0f - 170, 8, 340, 30.0f + 17 * (w.sides.size() + 1)}, 0.15f, 6, Fade(Color{10, 20, 30, 255}, 0.45f));
        DrawTextCenteredBold(w.matchLen > 0 ? TextFormat("%d:%02d left   day %d", (int)left / 60, (int)left % 60, (int)(w.time / fl::World::DAY) + 1) : TextFormat("day %d", (int)(w.time / fl::World::DAY) + 1), SCREEN_W / 2.0f, 12, 17, ink);
        std::vector<int> order; for (int s = 0; s <= (int)w.sides.size(); s++) order.push_back(s);
        std::sort(order.begin(), order.end(), [&](int a, int b) { return w.Score(a).total > w.Score(b).total; });
        for (size_t k = 0; k < order.size(); k++) {
            int s = order[k];
            Color c = s == w.cur ? Color{255, 230, 140, 255} : Mix(w.SideColor(s), WHITE, 0.35f);
            std::string who = s == w.cur ? "you" : w.SideName(s);
            if (!w.HumanOf(s)) who += " (bot)"; else if (w.BotFlown(s)) who += " (AI standing in)";
            Txt(TextFormat("%d. %s", (int)k + 1, who.c_str()), SCREEN_W / 2.0f - 150, 34 + 17.0f * k, 14, c);
            Txt(TextFormat("%d", w.Score(s).total), SCREEN_W / 2.0f + 110, 34 + 17.0f * k, 14, c);
        }
    }
    // dark plates under the text (the sky is bright)
    DrawRectangleRounded({12, 10, 470, 62}, 0.2f, 6, Fade(Color{10, 20, 30, 255}, 0.5f));
    if (!w.log.empty()) DrawRectangleRounded({12, 76, 560, 94}, 0.15f, 6, Fade(Color{10, 20, 30, 255}, 0.35f));
    DrawRectangleRounded({SCREEN_W - 260.0f, 24, 248, 222}, 0.12f, 6, Fade(Color{10, 20, 30, 255}, 0.45f));
    DrawRectangleRounded({12, SCREEN_H - 132.0f, 560, 92}, 0.15f, 6, Fade(Color{10, 20, 30, 255}, 0.4f));
    // top left: who and what
    TxtBold(d.name + "  (" + d.bird + ")", 24, 18, 22, ink);
    std::string st = fl::FStateName(f.st);
    if (f.chick) st += ", chick-leader " + std::to_string(f.chickFish) + "/3";
    if (f.exhausted) st += ", winded";
    Txt(st, 24, 46, 17, dim);
    // the bars
    float bx = 24, by = SCREEN_H - 120;
    DrawBar(bx, by, 260, 16, f.stamina / std::max(0.1f, d.stamina * (f.chick ? 0.5f : 1.0f)), f.exhausted ? Color{200, 90, 80, 230} : Color{90, 200, 220, 230}, "breath");
    DrawBar(bx, by + 26, 260, 16, f.hunger, f.hunger < 0.25f ? Color{230, 90, 60, 230} : Color{240, 170, 70, 230}, "hunger");
    DrawBar(bx, by + 52, 260, 16, f.hp / std::max(1.0f, d.hp), Color{220, 220, 210, 230}, "health");
    // flight numbers
    Txt(TextFormat("%3.0f m  %s", std::max(0.0f, f.pos.y), AltBand(f.pos.y)), bx + 280, by - 2, 18, ink);
    Txt(TextFormat("%3.0f m/s", Vector3Length(f.vel)), bx + 280, by + 24, 18, ink);
    if (f.carrySp >= 0) Txt(TextFormat("carrying: %s (size %d)", w.eco.map->species[f.carrySp].name.c_str(), f.carrySize), bx + 280, by + 50, 18, Color{180, 255, 190, 255});
    // top right: the wind, the time, the cache
    {
        Vector2 c{SCREEN_W - 70.0f, 70};
        DrawCircleV(c, 40, Fade(BLACK, 0.35f)); DrawRing(c, 38, 41, 0, 360, 32, Fade(ink, 0.6f));
        Vector2 wv = w.WindAt();
        // relative to where you look: up the dial is ahead
        float a = atan2f(wv.y, wv.x) - S.camYaw - PI * 0.5f;
        float sp = Vector2Length(wv);
        Vector2 tip{c.x + cosf(a) * 32, c.y + sinf(a) * 32}, tail{c.x - cosf(a) * 28, c.y - sinf(a) * 28};
        DrawLineEx(tail, tip, 3, Color{200, 240, 255, 255});
        DrawTri(tip, {tip.x - cosf(a - 0.5f) * 12, tip.y - sinf(a - 0.5f) * 12}, {tip.x - cosf(a + 0.5f) * 12, tip.y - sinf(a + 0.5f) * 12}, Color{200, 240, 255, 255});
        DrawTextCentered(TextFormat("wind %.0f m/s", sp), c.x, c.y + 46, 15, ink);
        float ph = w.DayPhase();
        int hh = (int)(ph * 24), mm = (int)(fmodf(ph * 24, 1) * 60);
        bool rise = (ph > 0.17f && ph < 0.36f) || (ph > 0.66f && ph < 0.84f);
        const char* part = ph < 0.2f || ph > 0.86f ? "night" : ph < 0.32f ? "dawn" : ph < 0.68f ? "day" : "dusk";
        DrawTextCentered(TextFormat("%02d:%02d  %s", hh, mm, part), c.x - 20, c.y + 66, 17, ink);
        if (w.seasons > 0 && w.Season() >= 0) {   // (the long match: the season and the day of the match)
            static const Color SC[4] = {{170, 240, 160, 255}, {255, 220, 120, 255}, {240, 160, 90, 255}, {190, 220, 255, 255}};
            DrawTextCentered(TextFormat("%s, day %d of %d", w.SeasonNow().name.c_str(), w.GameDay(), fl::SeasonDays(w.seasons)), c.x - 20, c.y + 174, 14, SC[w.Season()]);   // (at the panel's foot: the rising fish has the line under the clock)
            if (w.col.decree >= 0 && w.col.decree < (int)fl::Decrees().size()) DrawTextCentered(TextFormat("decree: %s", fl::Decrees()[w.col.decree].name.c_str()), c.x - 20, c.y + 190, 13, Color{255, 226, 160, 255});
        }
        if (rise) DrawTextCentered("the fish are rising", c.x - 30, c.y + 88, 15, Color{180, 255, 220, 255});
        float dof = w.DaysOfFood();
        DrawTextCentered(TextFormat("colony: %d birds", w.Alive()), c.x - 20, c.y + 110, 16, ink);
        DrawTextCentered(TextFormat("food: %.1f days   Tab: colony", dof), c.x - 50, c.y + 130, 14, dof < 1 && w.Alive() > 1 ? Color{255, 140, 120, 255} : dim);
        DrawTextCentered(TextFormat("pearls %d   fervour %.0f%s", w.col.pearls, w.col.fervour, w.col.resTree >= 0 ? "   researching" : ""), c.x - 50, c.y + 148, 14, Color{255, 220, 150, 255});
        if (w.col.boomState) DrawTextCentered(w.col.boomState == 1 ? "BOOM" : "bust", c.x - 50, c.y + 166, 15, w.col.boomState == 1 ? Color{255, 220, 120, 255} : Color{255, 140, 120, 255});
    }
    // the colony's nests, labelled when near: the courtship bowl, or what's in the nest
    for (const auto& n : w.col.nests) {
        float d = Vector3Distance(S.cam.position, n.pos);
        if (d > 45) continue;
        Vector3 fwd = Vector3Normalize(Vector3Subtract(S.cam.target, S.cam.position));
        if (Vector3DotProduct(Vector3Subtract(n.pos, S.cam.position), fwd) < 0) continue;
        Vector2 p = GetWorldToScreenEx(Vector3Add(n.pos, {0, 1.2f, 0}), S.cam, SCREEN_W, SCREEN_H);
        int eggs = 0, chicks = 0; bool mate = false;
        for (const auto& b : w.col.birds) if (b.alive && b.nest == (int)(&n - &w.col.nests[0])) { eggs += b.stage == fl::BStage::Egg; chicks += b.stage == fl::BStage::Chick; mate = mate || b.stage == fl::BStage::Mate; }
        std::string s = !n.built ? TextFormat("nest: %.0f/%d twigs", n.twigs, fl::Econ().nestTwigs * (w.Def().key == "albatross" ? 2 : 1))
                      : n.mateT >= 0 ? "a mate is coming"
                      : !mate ? TextFormat("courtship bowl %d/%d", n.bowl, n.bowlNeed)
                      : TextFormat("mate  %d eggs  %d chicks", eggs, chicks);
        DrawTextCentered(s, p.x, p.y, 14, Fade(ink, std::clamp(1.3f - d / 45, 0.3f, 1.0f)));
    }
    // a raid: an enemy flock over your island
    if (!w.col.caches.empty()) for (int s = 0; s <= (int)w.sides.size(); s++) if (s != w.cur) for (const auto& fk : w.ColOf(s).flocks)
        if (!fk.retreating && Vector2Distance({fk.pos.x, fk.pos.z}, {w.col.caches[0].pos.x, w.col.caches[0].pos.z}) < 250 && fmodf(S.t, 1.2f) < 0.8f)
            DrawTextCenteredBold(TextFormat("RAID: %s's %s (%d) over your island", w.SideName(s).c_str(), fk.name.c_str(), (int)fk.members.size()), SCREEN_W / 2.0f, 104, 20, Color{255, 110, 90, 255});
    for (const auto& fk : w.col.flocks) if (fk.leader == -2) {
        DrawRectangle(SCREEN_W / 2 - 110, SCREEN_H - 152, 220, 10, Fade(BLACK, 0.5f));
        DrawRectangle(SCREEN_W / 2 - 110, SCREEN_H - 152, (int)(220 * fk.morale / 100), 10, fk.morale < 30 ? Color{230, 90, 70, 255} : Color{120, 220, 140, 255});
        DrawTextCentered(TextFormat("leading %s (%d): morale %.0f", fk.name.c_str(), (int)fk.members.size(), fk.morale), SCREEN_W / 2.0f, SCREEN_H - 172, 15, ink);
    }
    for (int s = 0; s <= (int)w.sides.size(); s++) for (const auto& b : w.ColOf(s).birds) {
        if (!b.alive || b.stage != fl::BStage::Adult || !fl::IsWarrior(b.role) || (b.tgtSide < 0 && b.hp >= fl::RoleOf(b.role).hp)) continue;
        if (Vector3Distance(b.pos, S.cam.position) > 70) continue;
        Vector3 fwd = Vector3Normalize(Vector3Subtract(S.cam.target, S.cam.position));
        if (Vector3DotProduct(Vector3Subtract(b.pos, S.cam.position), fwd) < 0) continue;
        Vector2 p = GetWorldToScreenEx(Vector3Add(b.pos, {0, 0.8f, 0}), S.cam, SCREEN_W, SCREEN_H);
        float k = std::clamp(b.hp / fl::RoleOf(b.role).hp, 0.0f, 1.0f);
        DrawRectangle((int)p.x - 12, (int)p.y, 24, 3, Fade(BLACK, 0.6f));
        DrawRectangle((int)p.x - 12, (int)p.y, (int)(24 * k), 3, s == w.cur ? Color{120, 230, 140, 255} : w.SideColor(s));
    }
    // starving: the colony panel's alarm, flashing
    if (w.Alive() > 1 && w.DaysOfFood() < 0.5f && w.FeedPerDayEstimate() < w.MouthsPerDay() && fmodf(S.t, 1.0f) < 0.6f)
        DrawTextCenteredBold("THE COLONY IS RUNNING OUT OF FOOD", SCREEN_W / 2.0f, 82, 20, Color{255, 120, 100, 255});
    // the nest's mark, always
    if (f.st != fl::FState::Dead) {
        Vector3 to = Vector3Subtract(w.island.nest, S.cam.position);
        Vector3 fwd = Vector3Normalize(Vector3Subtract(S.cam.target, S.cam.position));
        float dist = Vector3Distance(f.pos, w.island.nest);
        if (Vector3DotProduct(to, fwd) > 0 && dist > 6) {
            Vector2 p = GetWorldToScreenEx(Vector3Add(w.island.nest, {0, 1.5f, 0}), S.cam, SCREEN_W, SCREEN_H);
            if (p.x > 0 && p.x < SCREEN_W && p.y > 0 && p.y < SCREEN_H) {
                DrawPoly(p, 4, 9, 45, Fade(Color{255, 220, 120, 255}, 0.85f));
                DrawTextCentered(TextFormat("nest %.0f m", dist), p.x, p.y + 12, 14, ink);
            }
        }
    }
    // a school below: a fishing bird reads the water
    if (f.st == fl::FState::Fly && f.pos.y < 45) {
        int n = 0; w.FishNear(f.pos, 14, 2.5f, &n);
        if (n > 0) DrawTextCentered(TextFormat("fish below (%d near the surface): dive!", n), SCREEN_W / 2.0f, SCREEN_H - 170, 18, Color{180, 255, 220, 255});
    }
    // what to do now
    if (f.st == fl::FState::Dead) {
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.35f));
        DrawTextCenteredBold(TextFormat("The Founder is %s", f.lastCause.c_str()), SCREEN_W / 2.0f, SCREEN_H / 2.0f - 40, 30, ink);
        DrawTextCentered(TextFormat("back in the nest as a chick-leader in %.0f s", std::max(0.0f, f.respawnT)), SCREEN_W / 2.0f, SCREEN_H / 2.0f + 4, 20, dim);
    } else if (f.st == fl::FState::Perched || f.st == fl::FState::Floating || f.st == fl::FState::Fly) {
        std::string tip = w.InteractHint();
        bool sitting = f.st != fl::FState::Fly;
        if (sitting) {
            if (f.carrySp >= 0) tip += std::string(tip.empty() ? "" : "    ") + "F: eat it";
            else if (w.NearestCache(f.pos, true, false) >= 0 && Vector3Distance(f.pos, w.col.caches[w.NearestCache(f.pos, true, false)].pos) < 3.5f) tip += std::string(tip.empty() ? "" : "    ") + "F: eat from the cache";
            tip = "Space: take off    " + tip;
            if (f.st == fl::FState::Floating) tip += "    (sharks hunt the water: don't sit long)";
        }
        if (!tip.empty()) DrawTextCentered(tip, SCREEN_W / 2.0f, SCREEN_H - (S.help ? 110 : 60), 18, ink);
    } else if (f.st == fl::FState::Fainted) {
        DrawTextCenteredBold("Fainted from hunger...", SCREEN_W / 2.0f, SCREEN_H / 2.0f, 28, Color{255, 180, 140, 255});
    }
    if (w.weather.kind == 1) for (int k = 0; k < 160; k++) {   // rain
        float x = fmodf(Hash((float)k, 1.0f) * SCREEN_W + S.t * 160, (float)SCREEN_W), y = fmodf(Hash((float)k, 2.0f) * SCREEN_H + S.t * (900 + 300 * Hash((float)k, 3.0f)), (float)SCREEN_H);
        DrawLineEx({x, y}, {x - 6, y + 22}, 1.2f, Fade(Color{210, 220, 235, 255}, 0.35f));
    }
    // the dangers (stage 7): the weather, the volcano, the kraken and the ape when they're near
    if (w.wholeMap) {
        std::vector<std::pair<std::string, Color>> warn;
        if (w.greatEvent >= 0 && (w.GreatNow(w.greatEvent) || (w.greatEvent == fl::GE_TREASURE && w.treasure > 0) || (w.greatEvent == fl::GE_VISITOR && w.legendFree >= 0)))
            warn.push_back({fl::GreatEvents()[w.greatEvent].name + ": " + fl::GreatEvents()[w.greatEvent].what, Color{255, 200, 120, 255}});
        if (w.seasonEvent >= 0 && w.EventNow(w.seasonEvent)) warn.push_back({std::string(fl::SeasonEventName(w.seasonEvent)) + ": " + fl::Seasons()[w.seasonEvent].eventWhat, Color{255, 236, 160, 255}});
        if (w.weather.kind == 1) warn.push_back({"STORM: the wind runs wild; every flock makes for home", Color{200, 220, 255, 255}});
        if (w.weather.kind == 2) warn.push_back({"FOG: the sea is shut in; Watchers see half as far", Color{230, 236, 240, 255}});
        if (w.volcano.isle >= 0) {
            float d = Vector3Distance(f.pos, w.isles[w.volcano.isle].c);
            if (w.volcano.ashT > 0 && d < 600) warn.push_back({"ASH: the volcano erupts; birds near it are grounded", Color{255, 170, 110, 255}});
            else if (w.volcano.tremorT > 0 && d < 900) warn.push_back({"The volcano rumbles: an eruption is coming", Color{255, 200, 140, 255}});
        }
        if (w.kraken.isle >= 0 && !w.kraken.dead && w.kraken.mood > 0 && Vector3Distance(f.pos, w.isles[w.kraken.isle].c) < 400)
            warn.push_back({w.kraken.mood == 2 ? "THE KRAKEN HAS SURFACED: keep high, or fight it" : "The kraken stirs: its arms take low fliers", Color{255, 130, 130, 255}});
        if (w.ape.isle >= 0 && w.ape.sleepT <= 0 && Vector3Distance(f.pos, w.ape.pos) < 260) warn.push_back({"The great ape is awake: it throws at what flies near (feed it to calm it)", Color{240, 210, 170, 255}});
        float wy = 46;
        if (warn.size() > 2) warn.resize(2);   // (the two most pressing; the log has the rest)
        for (const auto& wn : warn) {
            int tw = MeasureText(wn.first.c_str(), 18) + 30;
            DrawRectangleRounded({SCREEN_W / 2.0f - tw / 2.0f, wy - 3, (float)tw, 26}, 0.4f, 6, Fade(BLACK, 0.5f));
            DrawTextCenteredBold(wn.first, SCREEN_W / 2.0f, wy, 18, Fade(wn.second, 0.75f + 0.25f * sinf(S.t * 4)));
            wy += 30;
        }
    }
    // the long match: the Founder grows (doc p38): three perks; one click picks one
    if (w.seasons > 0 && w.me.perkOffer[0] >= 0 && !S.chart) {
        const auto& P = fl::Perks();
        float cw = 250, ch = 96, x0 = SCREEN_W / 2.0f - cw * 1.5f - 12, y0 = 190;
        DrawTextCenteredBold(TextFormat("The Founder grows (level %d): choose a perk", w.me.perkLevel + 1), SCREEN_W / 2.0f, y0 - 28, 20, Color{200, 240, 255, 255});
        for (int k = 0; k < 3; k++) {
            int i = w.me.perkOffer[k]; if (i < 0 || i >= (int)P.size()) continue;
            Rectangle r{x0 + k * (cw + 12), y0, cw, ch};
            bool hover = CheckCollisionPointRec(GetMousePosition(), r);
            DrawRectangleRounded(r, 0.08f, 6, Fade(hover ? Color{34, 54, 70, 255} : Color{20, 28, 40, 255}, 0.92f));
            DrawRectangleRoundedLinesEx(r, 0.08f, 6, 2, hover ? Color{170, 230, 255, 255} : Color{110, 160, 200, 255});
            DrawTextCenteredBold(P[i].name, r.x + cw / 2, r.y + 10, 18, Color{200, 240, 255, 255});
            DrawWrapped(P[i].effect, {r.x + 12, r.y + 38, cw - 24, 52}, 14, Color{220, 236, 240, 255});
            if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { Writer o; fl::OrderPerk(o, k); Order(o); PlayCue("ui.click"); }
        }
    }
    // the long match: at dawn, today's three decrees (doc pp. 36-38): one click picks one; by mid-morning, a Day of Rest
    if (w.seasons > 0 && w.col.decree < 0 && w.col.offer[0] >= 0 && !S.chart) {
        const auto& D = fl::Decrees();
        float cw = 250, ch = 128, x0 = SCREEN_W / 2.0f - cw * 1.5f - 12, y0 = SCREEN_H - 330.0f;
        DrawTextCenteredBold(TextFormat("Dawn of day %d: choose today's decree", w.GameDay()), SCREEN_W / 2.0f, y0 - 30, 20, Color{255, 236, 170, 255});
        for (int k = 0; k < 3; k++) {
            int i = w.col.offer[k]; if (i < 0 || i >= (int)D.size()) continue;
            Rectangle r{x0 + k * (cw + 12), y0, cw, ch};
            bool hover = CheckCollisionPointRec(GetMousePosition(), r);
            DrawRectangleRounded(r, 0.08f, 6, Fade(hover ? Color{70, 54, 34, 255} : Color{24, 30, 40, 255}, 0.92f));
            DrawRectangleRoundedLinesEx(r, 0.08f, 6, 2, hover ? Color{255, 220, 140, 255} : Color{200, 170, 110, 255});
            DrawTextCenteredBold(D[i].name, r.x + cw / 2, r.y + 10, 18, Color{255, 230, 160, 255});
            DrawWrapped(D[i].effect, {r.x + 12, r.y + 38, cw - 24, 44}, 14, Color{220, 240, 220, 255});
            if (!D[i].tradeoff.empty()) DrawWrapped("but: " + D[i].tradeoff, {r.x + 12, r.y + 82, cw - 24, 40}, 13, Color{255, 180, 150, 255});
            if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { Writer o; fl::OrderDecree(o, k); Order(o); PlayCue("ui.click"); }
        }
    }
    // the log
    {
        float y = 80;
        int shown = 0;
        for (int k = (int)w.log.size() - 1; k >= 0 && shown < 4; k--, shown++) {
            float age = (float)(w.log.size() - 1 - k);
            Txt(w.log[k], 24, y + (3 - shown) * 22.0f, 16, Fade(ink, std::clamp(1 - age * 0.22f, 0.25f, 1.0f)));
        }
    }
    if (S.help) {
        const char* lines[] = {"Mouse: steer (look where you fly)   W: flap   Shift: sprint   S: flare / brake   A D: turn",
                               "Dive steeply at the water near fish to strike; steer the talons in the slow motion",
                               "Space: take off   E: use (bowl, cache, twigs, nest site)   F: eat   Tab: colony   M: chart   H: hide   Esc: menu"};
        DrawRectangleRounded({SCREEN_W - 680.0f, SCREEN_H - 74.0f, 668, 62}, 0.2f, 6, Fade(Color{10, 20, 30, 255}, 0.45f));
        for (int k = 0; k < 3; k++) DrawTextCentered(lines[k], SCREEN_W - 346.0f, SCREEN_H - 70.0f + k * 18, 14, ink);
    }
}

// a small flat button for the panel (the game's Button is drawn for bigger plates)
bool SmallBtn(Rectangle r, const char* text, bool enabled = true) {
    bool hover = enabled && CheckCollisionPointRec(GetMousePosition(), r);
    DrawRectangleRounded(r, 0.3f, 4, enabled ? (hover ? Color{120, 96, 54, 255} : Color{82, 66, 40, 255}) : Color{44, 40, 36, 255});
    DrawRectangleRoundedLinesEx(r, 0.3f, 4, 1.5f, enabled ? Color{214, 180, 110, 255} : Color{90, 84, 76, 255});
    DrawTextCenteredBold(text, r.x + r.width / 2, r.y + (r.height - 16) / 2, 16, enabled ? Color{250, 240, 220, 255} : Color{130, 124, 116, 255});
    bool hit = hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (hit) PlayCue("ui.click");
    return hit;
}
// The colony panel (Tab; design doc p7): birds by role, mouths per day, feed per day, days of food, the stores, the
// nests, and the orders a stage-2 colony takes: the fledging plan, retraining, nests wanted, the fishers' ground.
void DrawColonyPanel(fl::World& w) {
    const fl::Economy& E = fl::Econ();
    Color ink{250, 248, 236, 255}, dim{200, 214, 214, 255}, good{170, 240, 180, 255}, bad{255, 140, 120, 255};
    float x = 20, y = 78, W = 440, H = 620;
    float fpd = w.FeedPerDayEstimate(), mouths = w.MouthsPerDay(), dof = w.DaysOfFood();
    bool starving = w.Alive() > 1 && dof < 1 && fpd < mouths;
    DrawRectangleRounded({x, y, W, H}, 0.05f, 6, Fade(Color{8, 18, 28, 255}, 0.86f));
    DrawRectangleRoundedLinesEx({x, y, W, H}, 0.05f, 6, 2, starving && fmodf(S.t, 1.0f) < 0.5f ? bad : Color{200, 170, 110, 255});
    TxtBold("The colony", x + 16, y + 10, 22, ink);
    Txt("Tab: flocks", x + W - 96, y + 16, 14, dim);
    float ly = y + 42;
    auto line = [&](const std::string& s, Color c) { Txt(s, x + 16, ly, 16, c); ly += 21; };
    line(TextFormat("Birds %d:  the Founder, %d adults, %d mates, %d chicks, %d eggs", w.Alive(), w.Count(fl::BStage::Adult), w.Count(fl::BStage::Mate), w.Count(fl::BStage::Chick), w.Count(fl::BStage::Egg)), ink);
    line(TextFormat("Feed per day %.0f   mouths per day %.0f", fpd, mouths), fpd >= mouths ? good : bad);
    int nfish = 0; for (const auto& c : w.col.caches) nfish += (int)c.fish.size();
    line(TextFormat("In store: %.0f feed (%d fish in %d caches) = %.1f days of food", w.CacheFeed(), nfish, (int)w.col.caches.size(), dof), dof < 1 ? bad : dof > 3 ? good : ink);
    line(TextFormat("Twigs %d   shells %d   pearls %d   wild mates left %d", w.col.twigs, w.col.shells, w.col.pearls, w.col.wildMates), dim);
    ly += 6;
    // roles: the count, and a button that retrains one bird into it (from the biggest other role; a day's retraining)
    TxtBold("Roles", x + 16, ly, 17, ink); Txt("fledging plan", x + 250, ly + 1, 15, dim); ly += 24;
    int nRoles = 0; for (int r = 1; r < (int)fl::Role::COUNT; r++) nRoles += w.RoleUnlocked((fl::Role)r);
    float rowH = nRoles > 11 ? 17.0f : 21.0f;
    for (int r = 1; r < (int)fl::Role::COUNT; r++) {
        fl::Role role = (fl::Role)r;
        if (!w.RoleUnlocked(role)) continue;
        int n = w.Count(fl::BStage::Adult, role), training = 0;
        for (const auto& b : w.col.birds) if (b.alive && b.retrainT > 0 && b.retrainTo == role) training++;
        Txt(TextFormat("%-8s %2d%s", fl::RoleName(role), n, training ? TextFormat(" (+%d)", training) : ""), x + 16, ly + 2, 14, fl::IsWarrior(role) ? Color{255, 200, 170, 255} : ink);
        if (SmallBtn({x + 168, ly, 66, 19}, "retrain", w.Count(fl::BStage::Adult) > n)) { Writer o; fl::OrderRetrain(o, role); Order(o); }
        float total = 0; for (int k = 1; k < (int)fl::Role::COUNT; k++) total += w.col.plan[k];
        Txt(TextFormat("%3.0f%%", 100 * w.col.plan[r] / std::max(0.01f, total)), x + 262, ly + 2, 14, ink);
        if (SmallBtn({x + 318, ly, 26, 19}, "-", w.col.plan[r] > 0.01f)) { Writer o; fl::OrderPlan(o, role, std::max(0.0f, w.col.plan[r] - 0.1f)); Order(o); }
        if (SmallBtn({x + 350, ly, 26, 19}, "+", true)) { Writer o; fl::OrderPlan(o, role, w.col.plan[r] + 0.1f); Order(o); }
        ly += rowH;
    }
    ly += 4;
    // nests
    int built = 0, under = 0, free = 0; for (const auto& n : w.col.nests) (n.built ? built : under)++;
    for (const auto& s : w.col.sites) free += s.nest < 0;
    TxtBold("Nests", x + 16, ly, 17, ink); ly += 24;
    Txt(TextFormat("%d built, %d under way, %d free sites", built, under, free), x + 16, ly + 3, 16, ink);
    Txt(TextFormat("builders raise up to %d", w.col.nestsWanted), x + 16, ly + 24, 15, dim);
    if (SmallBtn({x + 318, ly + 18, 26, 24}, "-", w.col.nestsWanted > 1)) { Writer o; fl::OrderNests(o, w.col.nestsWanted - 1); Order(o); }
    if (SmallBtn({x + 350, ly + 18, 26, 24}, "+", w.col.nestsWanted < (int)w.col.sites.size())) { Writer o; fl::OrderNests(o, w.col.nestsWanted + 1); Order(o); }
    ly += 52;
    if (w.seasons > 0) {   // (the long match: mates with traits; a picky bowl)
        const auto& MT = fl::MateTraits();
        std::string want = w.col.wantTrait < 0 ? std::string("any trait") : MT[w.col.wantTrait].name + " (" + MT[w.col.wantTrait].favorite + "): " + MT[w.col.wantTrait].effect;
        if (SmallBtn({x + 16, ly - 2, 26, 20}, "<", true)) { Writer o; fl::OrderWantTrait(o, w.col.wantTrait <= -1 ? fl::MT_COUNT - 1 : w.col.wantTrait - 1); Order(o); }
        if (SmallBtn({x + 46, ly - 2, 26, 20}, ">", true)) { Writer o; fl::OrderWantTrait(o, w.col.wantTrait >= fl::MT_COUNT - 1 ? -1 : w.col.wantTrait + 1); Order(o); }
        Txt("Mates wanted: " + want, x + 80, ly, 13, Color{255, 220, 170, 255}); ly += 22;
    }
    Txt("A mate comes to a nest whose courtship bowl you fill:", x + 16, ly, 14, dim); ly += 17;
    Txt(TextFormat("carry fish of size %d+ to it and press E (%d for the first).", E.courtMinSize, E.courtFish), x + 16, ly, 14, dim); ly += 26;
    // the fishers' ground
    TxtBold("Fishers fish", x + 16, ly, 17, ink);
    const auto& zones = w.eco.map->zones;
    std::string gname = w.col.ground < 0 ? "the best ground" : zones[w.col.ground].name;
    if (SmallBtn({x + 150, ly - 2, 26, 24}, "<", true)) { Writer o; fl::OrderGround(o, w.col.ground < 0 ? (int)zones.size() - 1 : w.col.ground - 1); Order(o); }
    Txt(gname, x + 184, ly + 1, 16, ink);
    if (SmallBtn({x + 350, ly - 2, 26, 24}, ">", true)) { Writer o; fl::OrderGround(o, w.col.ground + 1 >= (int)zones.size() ? -1 : w.col.ground + 1); Order(o); }
    ly += 26;
    if (w.col.ground >= 0) { Txt(TextFormat("its stock: %.0f%% of what it holds", w.StockOf(w.col.ground) * 100), x + 16, ly, 14, w.StockOf(w.col.ground) < 0.35f ? bad : dim); }
    else if (w.lagoonZone >= 0) Txt(TextFormat("the lagoon's stock: %.0f%%", w.StockOf(w.lagoonZone) * 100), x + 16, ly, 14, w.StockOf(w.lagoonZone) < 0.35f ? bad : dim);
    ly += 22;
    if (!w.col.deaths.empty()) {
        std::string d = "Deaths:"; for (const auto& p : w.col.deaths) d += TextFormat("  %s %d", p.first.c_str(), p.second);
        Txt(d, x + 16, ly, 14, bad);
    }
    if (w.col.leaderless) Txt("Leaderless: the old orders are running down.", x + 16, y + H - 24, 14, bad);
}

// The flocks (Tab, the second page; design doc p13): warriors in flocks with a leader, a formation, a height, a stance
// and a target; the colony's defences (a hedge, a tower). Targets are given on the chart (M) with a flock picked here.
void DrawFlockPanel(fl::World& w) {
    Color ink{250, 248, 236, 255}, dim{200, 214, 214, 255}, bad{255, 140, 120, 255}, good{170, 240, 180, 255};
    float x = 20, y = 78, W = 440, H = 620;
    DrawRectangleRounded({x, y, W, H}, 0.05f, 6, Fade(Color{8, 18, 28, 255}, 0.86f));
    DrawRectangleRoundedLinesEx({x, y, W, H}, 0.05f, 6, 2, Color{200, 110, 90, 255});
    TxtBold("Flocks", x + 16, y + 10, 22, ink);
    Txt("Tab: research", x + W - 110, y + 16, 14, dim);
    float ly = y + 42;
    // idle warriors, and a flock of them
    int idle[(int)fl::Role::COUNT] = {}, nIdle = 0;
    std::vector<int> ids;
    for (const auto& b : w.col.birds) if (b.alive && b.stage == fl::BStage::Adult && fl::IsWarrior(b.role) && b.role != fl::Role::Watcher && b.flock < 0 && b.retrainT <= 0) { idle[(int)b.role]++; nIdle++; ids.push_back(b.id); }
    std::string sum; for (int r = (int)fl::Role::Skirmisher; r < (int)fl::Role::COUNT; r++) if (idle[r]) sum += TextFormat("%d %s  ", idle[r], fl::RoleName((fl::Role)r));
    Txt(nIdle ? "Idle: " + sum : "No idle warriors: retrain some on the colony page, or set warriors in the fledging plan.", x + 16, ly, 14, nIdle ? ink : dim); ly += 20;
    if (SmallBtn({x + 16, ly, 250, 22}, "Form a flock of them", nIdle >= 2)) {
        bool strikers = idle[(int)fl::Role::Striker] > 0, skirm = idle[(int)fl::Role::Skirmisher] > 0;
        Writer o; fl::OrderFlockMake(o, ids, strikers && skirm ? fl::Formation::Hammer : fl::Formation::Chevron, strikers ? fl::Alt::High : fl::Alt::Mid, fl::Stance::RetreatHalf); Order(o);
    }
    ly += 30;
    // each flock
    for (auto& f : w.col.flocks) {
        if (ly > y + H - 150) break;
        bool sel = S.selFlock == f.id;
        DrawRectangleRounded({x + 10, ly - 4, W - 20, 96}, 0.1f, 4, Fade(sel ? Color{90, 60, 40, 255} : Color{30, 40, 50, 255}, 0.7f));
        int n[(int)fl::Role::COUNT] = {};
        for (int id : f.members) if (fl::Bird* b = w.FindBird(w.cur, id)) n[(int)b->role]++;
        std::string comp;
        for (int r = (int)fl::Role::Skirmisher; r < (int)fl::Role::COUNT; r++) if (n[r]) comp += TextFormat("%d %s ", n[r], fl::RoleAbbrev((fl::Role)r));
        TxtBold(f.name, x + 18, ly, 16, ink);
        if (f.stim != fl::STIM_NONE) Txt(f.stimT > 0 ? TextFormat("%s %.1f d", fl::StimName(f.stim), f.stimT / fl::World::DAY) : "crashing", x + W - 118, ly - 12, 12, f.stimT > 0 ? Color{255, 200, 120, 255} : bad);
        else { int have = 0; for (int s = 1; s < fl::STIM_COUNT; s++) if (w.col.stims[s] > 0) { have = s; break; }
               if (have && SmallBtn({x + W - 96, ly - 14, 76, 17}, TextFormat("+%s", fl::StimName(have)))) { Writer o; fl::OrderDose(o, f.id, have); Order(o); } }
        Txt(TextFormat("%s   led by %s", comp.c_str(), f.leader == -2 ? "the Founder" : f.leader >= 0 ? "a Flockmaster" : "no one"), x + 120, ly + 2, 13, dim);
        // morale
        DrawRectangle((int)x + 18, (int)ly + 22, 160, 8, Fade(BLACK, 0.5f));
        DrawRectangle((int)x + 18, (int)ly + 22, (int)(160 * f.morale / 100), 8, f.morale < 30 ? bad : f.morale < 50 ? Color{240, 200, 90, 255} : good);
        Txt(TextFormat("morale %.0f%s", f.morale, f.retreating ? ", retreating" : ""), x + 186, ly + 18, 13, f.retreating ? bad : dim);
        // formation, height, stance
        auto cyc = [&](float cx, const char* text, int& v, int nv, bool forms = false) {
            auto ok = [&](int k) { return !forms || fl::FormationUnlocked(w.col, (fl::Formation)k); };   // (formations want War 1)
            if (SmallBtn({cx, ly + 36, 18, 19}, "<")) { do v = (v + nv - 1) % nv; while (!ok(v)); }
            Txt(text, cx + 22, ly + 38, 13, ink);
            if (SmallBtn({cx + 108, ly + 36, 18, 19}, ">")) { do v = (v + 1) % nv; while (!ok(v)); }
        };
        int fo = (int)f.form, al = (int)f.alt, stc = (int)f.stance;
        cyc(x + 18, fl::FormationName(f.form), fo, (int)fl::Formation::COUNT, true);
        cyc(x + 158, TextFormat("%s %.0f m", fl::AltName(f.alt), fl::AltHeight(f.alt)), al, 3);
        cyc(x + 298, fl::StanceName(f.stance), stc, (int)fl::Stance::COUNT);
        if (fo != (int)f.form || al != (int)f.alt || stc != (int)f.stance) { Writer o; fl::OrderFlockSet(o, f.id, (fl::Formation)fo, (fl::Alt)al, (fl::Stance)stc); Order(o); }
        Txt(TargetText(w, f), x + 18, ly + 62, 13, Color{255, 220, 170, 255});
        if (SmallBtn({x + 240, ly + 60, 60, 20}, sel ? "picked" : "pick")) S.selFlock = f.id;
        if (SmallBtn({x + 304, ly + 60, 56, 20}, "home")) { Writer o; fl::OrderFlockHome(o, f.id); Order(o); }
        if (SmallBtn({x + 364, ly + 60, 56, 20}, "disband")) { Writer o; fl::OrderFlockDisband(o, f.id); Order(o); }
        ly += 104;
    }
    // defences
    ly = y + H - 190;
    TxtBold("Defences", x + 16, ly, 17, ink);
    if (!w.BuildUnlocked(fl::ST_HEDGE)) Txt("(hedges and towers want War 3)", x + 110, ly + 2, 13, dim);
    ly += 24;
    const fl::Structure* hedge = nullptr; const fl::Structure* tower = nullptr;
    for (const auto& s : w.col.builds) { if (s.kind == fl::ST_HEDGE) hedge = &s; else if (s.kind == fl::ST_TOWER) tower = &s; }
    int watchers = w.Count(fl::BStage::Adult, fl::Role::Watcher);
    auto row = [&](const char* name, const fl::Structure* s, int kind, const char* cost) {
        std::string st = !s ? "none" : s->built ? "built" : TextFormat("under way: %.0f of %d twigs", s->twigs, fl::StructureTwigs(kind));
        Txt(TextFormat("%s: %s", name, st.c_str()), x + 16, ly + 2, 14, s && s->built ? good : ink);
        if (!s && SmallBtn({x + W - 206, ly, 190, 20}, cost, w.BuildUnlocked(kind))) { Writer o; fl::OrderBuild(o, kind); Order(o); }
        ly += 24;
    };
    row("Hedge round the home cache", hedge, 0, "raise (10 twigs)");
    row("Tower for a Watcher", tower, 1, "raise (20 twigs, 5 shells)");
    // the Works (Bombing 1 or Chemistry 1): bombs for the Bombers, stimulants for the flocks
    {
        const fl::Structure* works = nullptr; for (const auto& s : w.col.builds) if (s.kind == fl::ST_WORKS) works = &s;
        if (!works || !works->built) row("The Works", works, fl::ST_WORKS, w.BuildUnlocked(fl::ST_WORKS) ? "raise the Works" : "(Bombing or Chemistry 1)");
        else {
            Txt(TextFormat("Works: %d bombs, %d blockbusters; guano %.0f, sulfur %.0f", w.col.bombs, w.col.blockbusters, w.col.guano, w.col.sulfur), x + 16, ly + 2, 14, ink); ly += 20;
            std::string st; for (int s = 1; s < fl::STIM_COUNT; s++) st += TextFormat("%s %d  ", fl::StimName(s), w.col.stims[s]);
            Txt(st, x + 16, ly + 2, 13, dim);
            if (w.col.HasTier(fl::Tree::Chemistry, 1)) {
                Txt("brew", x + W - 166, ly + 2, 13, dim);
                if (SmallBtn({x + W - 130, ly, 114, 19}, fl::StimName(w.col.brewFor))) { int nx = w.col.brewFor % (fl::STIM_COUNT - 1) + 1; Writer o; fl::OrderBrew(o, nx); Order(o); }
            }
            ly += 24;
        }
    }
    Txt(TextFormat("%d Watchers guard the nests (hedges stop Skirmishers; Strikers go over)", watchers), x + 16, ly + 2, 13, dim); ly += 22;
    DrawWrapped("Pick a flock, then click a target on the chart (M). G: the Founder leads the nearest flock (+20 morale, +10% speed).", {x + 16, ly, W - 32, 34}, 13, dim);
}

// The fourth page in a long match (Tab four times; doc pp. 35-48): the season, today's decree, the Founder's perks, the
// relics at the shrine and the legend, the veterans by name, and the neutral powers (hire the pirates, pay the Grey Wings)
void DrawLongPanel(fl::World& w) {
    Color ink{250, 248, 236, 255}, dim{200, 214, 214, 255}, gold{255, 220, 150, 255}, bad{255, 140, 120, 255};
    float x = 20, y = 78, W = 540, H = 620;
    DrawRectangleRounded({x, y, W, H}, 0.05f, 6, Fade(Color{8, 18, 28, 255}, 0.88f));
    DrawRectangleRoundedLinesEx({x, y, W, H}, 0.05f, 6, 2, Color{200, 180, 120, 255});
    TxtBold("The long match", x + 16, y + 10, 22, ink);
    Txt("Tab: close", x + W - 90, y + 16, 14, dim);
    float ly = y + 44;
    auto line = [&](const std::string& s, Color c, int size = 15) { DrawWrapped(s, {x + 16, ly, W - 32, 40}, size, c); ly += size + 7; };
    const fl::SeasonFx& S0 = w.SeasonNow();
    line(TextFormat("%s, day %d of %d. %s", S0.name.c_str(), w.GameDay(), fl::SeasonDays(w.seasons), S0.sea.c_str()), Color{170, 240, 160, 255});
    line("Rewards: " + S0.rewards, dim, 13);
    if (w.col.decree >= 0) line("Today's decree: " + fl::Decrees()[w.col.decree].name + " (" + fl::Decrees()[w.col.decree].effect + ")", gold);
    { std::string p; for (int i = 0; i < (int)fl::Perks().size(); i++) if ((w.me.perks >> i) & 1) p += (p.empty() ? "" : ", ") + fl::Perks()[i].name;
      line("The Founder's perks: " + (p.empty() ? std::string("none yet (days 5, 11 and 17)") : p), Color{200, 240, 255, 255}); }
    ly += 6; TxtBold("At the shrine", x + 16, ly, 17, ink); ly += 24;
    int nr = 0; for (int r = 0; r < fl::RL_COUNT; r++) if ((w.col.relics >> r) & 1) { line(fl::Relics()[r].name + ": " + fl::Relics()[r].effect, gold, 14); nr++; }
    if (!nr) line(TextFormat("No relics (three at most). %d still lie on the dangerous islands.", (int)std::count_if(w.relicSpots.begin(), w.relicSpots.end(), [](const fl::World::RelicSpot& s) { return !s.taken; })), dim, 13);
    if (w.col.legend >= 0) line(fl::Legends()[w.col.legend].name + (w.col.legendAlive ? ": " + fl::Legends()[w.col.legend].effect : std::string(" (lost)")), Color{255, 236, 170, 255}, 14);
    ly += 6; TxtBold("Veterans", x + 16, ly, 17, ink); ly += 24;
    int nv = 0; for (const auto& b : w.col.birds) if (b.alive && b.vet >= 0 && nv < 6) { line(w.VetLabel(b) + TextFormat(" (a %s, %d fights)", fl::RoleName(b.role), b.fights), ink, 13); nv++; }
    if (!nv) line("None yet: a warrior that survives three fights earns a name.", dim, 13);
    ly += 6; TxtBold("The neutral powers", x + 16, ly, 17, ink); ly += 24;
    if (w.pirates.on) {
        bool scattered = w.time < w.pirates.scatterUntil;
        line(scattered ? TextFormat("The Frigate Pirates are scattered (their captain fell) for %.1f more days.", (w.pirates.scatterUntil - w.time) / fl::World::DAY)
                       : w.pirates.target >= 0 && w.time < w.pirates.hireUntil ? "The Frigate Pirates are hired against " + w.SideName(w.pirates.target) + "." : std::string("The Frigate Pirates rove: they steal carried fish and raid full caches. A flock can kill their captain."), dim, 13);
        static int hireAt = 1;
        if (!scattered) {
            int n = (int)w.sides.size() + 1; if (hireAt == w.cur || hireAt >= n) hireAt = (w.cur + 1) % n;
            if (SmallBtn({x + 16, ly, 26, 20}, "<")) { do hireAt = (hireAt + n - 1) % n; while (hireAt == w.cur); }
            if (SmallBtn({x + 46, ly, 26, 20}, ">")) { do hireAt = (hireAt + 1) % n; while (hireAt == w.cur); }
            if (SmallBtn({x + 80, ly, 300, 20}, TextFormat("hire them against %s (%d fish)", w.SideName(hireAt).c_str(), fl::PirateHireFish()))) { Writer o; fl::OrderHire(o, hireAt); Order(o); }
            ly += 26;
        }
    }
    // diplomacy, lightly: a pact, a bounty, a loan, a broken truce (doc pp. 47-48)
    {
        static int with = 1; int n = (int)w.sides.size() + 1; if (with == w.cur || with >= n) with = (w.cur + 1) % n;
        ly += 4; TxtBold("Diplomacy", x + 16, ly, 17, ink);
        if (SmallBtn({x + 130, ly, 24, 20}, "<")) { do with = (with + n - 1) % n; while (with == w.cur); }
        Txt(w.SideName(with), x + 160, ly + 2, 15, w.SideColor(with));
        if (SmallBtn({x + 300, ly, 24, 20}, ">")) { do with = (with + 1) % n; while (with == w.cur); }
        ly += 26;
        if (w.col.pact >= 0) line("Feed pact with " + w.SideName(w.col.pact) + ": the better-fed colony sends fish down the line.", Color{170, 240, 180, 255}, 13);
        else if (SmallBtn({x + 16, ly, 150, 20}, "propose a feed pact")) { Writer o; fl::OrderPact(o, with); Order(o); }
        if (SmallBtn({x + 176, ly, 150, 20}, "bounty: 10 fish")) { Writer o; fl::OrderBounty(o, with, 10); Order(o); }
        if (!w.col.flocks.empty() && SmallBtn({x + 336, ly, 180, 20}, TextFormat("lend %s (8 fish)", w.col.flocks[0].name.c_str()))) { Writer o; fl::OrderLoan(o, with, w.col.flocks[0].id, 8); Order(o); }
        ly += 24;
        if (w.Truce(w.cur, with) && SmallBtn({x + 16, ly, 200, 20}, "break the truce (fervour -20)")) { Writer o; fl::OrderBreak(o, with); Order(o); }
        if (w.col.bounty > 0) line(TextFormat("A bounty of %d fish is on your Founder (posted by %s).", w.col.bounty, w.SideName(w.col.bountyBy).c_str()), bad, 13);
        ly += 4;
    }
    line(TextFormat("The Fishing Fleet: %d boats work the grounds, drop chum, and net birds that fly low near them.", (int)w.fleet.size()), dim, 13);
    if (w.grey.isle >= 0) {
        bool peace = w.cur < (int)w.grey.peaceUntil.size() && w.time < w.grey.peaceUntil[w.cur];
        line(w.grey.dead ? std::string("The Grey Wings' eagle is dead.") : "The Grey Wings hunt chicks and lone fishers from " + w.isles[w.grey.isle].name + (peace ? " (you have peace today)." : "."), w.grey.dead ? dim : bad, 13);
        if (!w.grey.dead && !peace && SmallBtn({x + 16, ly, 260, 20}, TextFormat("pay them tribute (%d fish)", fl::TributeFish()))) { Writer o; fl::OrderTribute(o); Order(o); }
    }
}
// The third page (Tab three times; design doc pp. 20-22, 25-27): research at the Roost, faith and fervour, trade at
// the towns and with the other colonies, and the founder's own button (the Tycoon's boom, the Sigma's corner).
void DrawLongPanel(fl::World& w);
void DrawSocietyPanel(fl::World& w) {
    Color ink{250, 248, 236, 255}, dim{200, 214, 214, 255}, good{170, 240, 180, 255}, bad{255, 140, 120, 255}, gold{255, 220, 150, 255};
    float x = 20, y = 78, W = 540, H = 640;
    const fl::Colony& C = w.col;
    DrawRectangleRounded({x, y, W, H}, 0.05f, 6, Fade(Color{8, 18, 28, 255}, 0.88f));
    DrawRectangleRoundedLinesEx({x, y, W, H}, 0.05f, 6, 2, Color{150, 170, 220, 255});
    TxtBold("Research, faith and trade", x + 16, y + 10, 22, ink);
    Txt("Tab closes", x + W - 86, y + 16, 14, dim);
    float ly = y + 40;
    Txt(TextFormat("pearls %d   shells %d   twigs %d   guano %.0f", C.pearls, C.shells, C.twigs, C.guano), x + 16, ly, 15, gold); ly += 22;
    // ---- research
    const fl::Structure* roost = w.Built(fl::ST_ROOST);
    bool roostLaid = false; for (const auto& s : C.builds) roostLaid |= s.kind == fl::ST_ROOST;
    TxtBold("Research", x + 16, ly, 17, ink);
    if (!roost) {
        Txt(roostLaid ? "the Roost is going up (builders)" : "research needs a Roost:", x + 110, ly + 2, 14, roostLaid ? dim : bad);
        if (!roostLaid && SmallBtn({x + W - 200, ly - 2, 184, 22}, TextFormat("raise it (%d twigs)", fl::StructureTwigs(fl::ST_ROOST)))) { Writer o; fl::OrderBuild(o, fl::ST_ROOST); Order(o); }
    } else if (C.resTree >= 0) {
        int n = C.tier[C.resTree] + 1;
        Txt(TextFormat("%s %d: %s", fl::TreeName((fl::Tree)C.resTree), n, fl::ResearchOf((fl::Tree)C.resTree, n).name.c_str()), x + 110, ly + 2, 14, gold);
        float k = 1 - C.resLeft / std::max(0.01f, C.resDays);
        DrawRectangle((int)(x + W - 150), (int)ly + 6, 130, 8, Fade(BLACK, 0.5f)); DrawRectangle((int)(x + W - 150), (int)ly + 6, (int)(130 * k), 8, gold);
    } else Txt("the Roost is idle: pick the next tier", x + 110, ly + 2, 14, dim);
    ly += 24;
    S.hoverTree = -1;
    for (int t = 0; t < (int)fl::Tree::COUNT; t++) {
        fl::Tree tr = (fl::Tree)t;
        int tier = C.tier[t];
        Rectangle row{x + 10, ly - 2, W - 20, 21};
        if (CheckCollisionPointRec(GetMousePosition(), row)) { S.hoverTree = t; DrawRectangleRec(row, Fade(Color{80, 90, 120, 255}, 0.4f)); }
        Txt(fl::TreeName(tr), x + 16, ly, 14, ink);
        for (int k = 0; k < 4; k++) DrawCircle((int)(x + 186 + k * 13), (int)ly + 8, 4.5f, k < tier ? gold : Fade(dim, 0.35f));
        if (tier < 4) {
            fl::ResearchCost c = fl::ResearchCostOf(tier + 1);
            Txt(fl::ResearchOf(tr, tier + 1).name, x + 244, ly, 13, dim);
            std::string why; bool can = w.CanResearch(tr, &why);
            if (SmallBtn({x + W - 100, ly - 1, 84, 19}, TextFormat("%dp %ds", c.pearls, c.shells), can)) { Writer o; fl::OrderResearch(o, tr); Order(o); }
        } else Txt("complete", x + 244, ly, 13, good);
        ly += 21;
    }
    if (S.hoverTree >= 0) {
        fl::Tree tr = (fl::Tree)S.hoverTree;
        int n = std::min(4, C.tier[S.hoverTree] + 1);
        std::string why; w.CanResearch(tr, &why);
        fl::ResearchCost c = fl::ResearchCostOf(n);
        DrawWrapped(TextFormat("%s %d, %s: %s. %.1f days%s.%s", fl::TreeName(tr), n, fl::ResearchOf(tr, n).name.c_str(), fl::ResearchOf(tr, n).what.c_str(), c.days, c.birds ? TextFormat(", a colony of %d", c.birds) : "", why.empty() ? "" : (" (" + why + ")").c_str()), {x + 16, ly, W - 32, 36}, 13, gold);
    }
    ly += 38;
    // ---- faith
    float band = w.FervourBand();
    static const char* BANDS[5] = {"low: the colony sleeps a full night; flocks break at 40", "normal", "high: half a night's rest, morale +10", "very high: two hours' rest, dusk and dawn fishing, morale +20", "ZEAL: no rest, no rout, conversion near the shrine; it eats 20% more"};
    TxtBold("Faith", x + 16, ly, 17, ink);
    DrawRectangle((int)x + 80, (int)ly + 4, 200, 10, Fade(BLACK, 0.5f));
    DrawRectangle((int)x + 80, (int)ly + 4, (int)(2 * std::min(C.fervour, w.BendNow().fervourCap)), 10, band >= 3 ? gold : Color{150, 190, 255, 255});
    Txt(TextFormat("fervour %.0f%s", C.fervour, C.prayerT > 0 ? " (+15 prayer)" : ""), x + 290, ly + 1, 14, ink);
    ly += 20;
    Txt(BANDS[(int)band], x + 16, ly, 13, dim); ly += 18;
    const fl::Structure* shrine = w.Built(fl::ST_SHRINE);
    bool shrineLaid = false; for (const auto& s : C.builds) shrineLaid |= s.kind == fl::ST_SHRINE;
    int priests = w.Count(fl::BStage::Adult, fl::Role::Priest);
    if (w.BendNow().noFaith) Txt("Your founder has no faith tree.", x + 16, ly, 13, dim);
    else if (!w.BuildUnlocked(fl::ST_SHRINE)) Txt("The shrine and its priests want Faith 1.", x + 16, ly, 13, dim);
    else if (!shrine) {
        Txt(shrineLaid ? "the shrine is going up" : "no shrine yet", x + 16, ly, 13, shrineLaid ? dim : bad);
        if (!shrineLaid && SmallBtn({x + W - 260, ly - 2, 244, 20}, TextFormat("raise a shrine (%d twigs, %d shells)", fl::StructureTwigs(fl::ST_SHRINE), fl::StructureShells(fl::ST_SHRINE)))) { Writer o; fl::OrderBuild(o, fl::ST_SHRINE); Order(o); }
    } else Txt(TextFormat("the shrine: %d priests (plan them on the colony page); land on it and press E to pray%s", priests, C.HasTier(fl::Tree::Faith, 2) ? " or offer a fish" : ""), x + 16, ly, 13, ink);
    ly += 24;
    // ---- trade
    TxtBold("Trade", x + 16, ly, 17, ink);
    int town = w.towns.empty() ? -1 : w.NearestTown(C.caches.empty() ? w.me.pos : C.caches[0].pos, 1e9f);
    if (town < 0) Txt("no town on this map", x + 80, ly + 2, 14, dim);
    else {
        const fl::Town& T = w.towns[town];
        float rep = w.cur < (int)T.rep.size() ? T.rep[w.cur] : 0;
        Txt(TextFormat("%s: fish pays %.2f; a pearl %.1f, a shell %.1f, a twig %.1f feed; your standing %+.0f", w.isles[T.isle].name.c_str(), w.FishPrice(town), w.SellPrice(town, fl::G_PEARLS), w.SellPrice(town, fl::G_SHELLS), w.SellPrice(town, fl::G_TWIGS), rep), x + 80, ly + 2, 13, ink);
    }
    ly += 22;
    Txt("Traders and your Founder (E at a dock with a fish) buy:", x + 16, ly + 2, 13, dim);
    for (int g = fl::G_TWIGS; g < fl::G_COUNT; g++) {
        Rectangle r{x + 336 + (g - 1) * 64.0f, ly, 58, 20};
        if (SmallBtn(r, fl::GoodName(g))) { Writer o; fl::OrderTradeFor(o, g); Order(o); }
        if (C.tradeFor == g) DrawRectangleLinesEx({r.x - 2, r.y - 2, r.width + 4, r.height + 4}, 2, Color{200, 60, 40, 255});
    }
    ly += 26;
    if (town >= 0 && Vector3Distance(w.me.pos, w.towns[town].dock) < 12) {   // the old pelican on the dock: a pearl for one true thing
        int n = (int)w.isles.size();
        if (S.pelicanIsle < 0 || S.pelicanIsle >= n) S.pelicanIsle = 0;
        if (SmallBtn({x + 16, ly, 22, 20}, "<")) S.pelicanIsle = (S.pelicanIsle + n - 1) % n;
        Txt(w.isles[S.pelicanIsle].name, x + 44, ly + 2, 13, ink);
        if (SmallBtn({x + 200, ly, 22, 20}, ">")) S.pelicanIsle = (S.pelicanIsle + 1) % n;
        if (SmallBtn({x + 232, ly, 230, 20}, "the old pelican (a pearl): the truth", C.pearls >= 1)) { Writer o; fl::OrderPelican(o, town, S.pelicanIsle); Order(o); }
        ly += 26;
    }
    // ---- the founder's own button
    const fl::Bend& B = w.BendNow();
    if (B.boom) {
        const char* st = C.boomState == 1 ? "BOOM" : C.boomState == 2 ? "bust" : C.boomCd > 0 ? "cooling" : "ready";
        Txt(TextFormat("The Tycoon's boom: %s%s", st, C.boomState ? TextFormat(" (%.1f days)", C.boomT / fl::World::DAY) : C.boomCd > 0 ? TextFormat(" (%.1f days)", C.boomCd / fl::World::DAY) : ""), x + 16, ly + 2, 14, C.boomState == 1 ? gold : C.boomState == 2 ? bad : ink);
        if (SmallBtn({x + W - 140, ly, 124, 20}, "BOOM", C.boomState == 0 && C.boomCd <= 0)) { Writer o; fl::OrderBoom(o); Order(o); }
        ly += 26;
    }
    if (B.cornering && town >= 0) {
        Txt(C.cornered ? "The market is cornered (once a match)." : "Corner the nearest town's catch (morning; sells at evening):", x + 16, ly + 2, 14, ink);
        if (!C.cornered && SmallBtn({x + W - 140, ly, 124, 20}, "corner it", w.DayPhase() > 0.2f && w.DayPhase() < 0.45f)) { Writer o; fl::OrderCorner(o, town); Order(o); }
        ly += 26;
    }
    // ---- barter with the other colonies (Trade 2)
    TxtBold("Barter", x + 16, ly, 17, ink);
    if (!C.HasTier(fl::Tree::Trade, 2)) { Txt("wants Trade 2", x + 90, ly + 2, 14, dim); ly += 22; }
    else {
        ly += 22;
        int N = (int)w.sides.size() + 1;
        if (S.bTo == w.cur || S.bTo >= N) S.bTo = (w.cur + 1) % N;
        if (SmallBtn({x + 16, ly, 22, 20}, "<")) { do S.bTo = (S.bTo + N - 1) % N; while (S.bTo == w.cur); }
        Txt(w.SideName(S.bTo), x + 44, ly + 2, 14, Mix(w.SideColor(S.bTo), WHITE, 0.4f));
        if (SmallBtn({x + 170, ly, 22, 20}, ">")) { do S.bTo = (S.bTo + 1) % N; while (S.bTo == w.cur); }
        if (SmallBtn({x + 210, ly, 22, 20}, "-")) S.bTruce = std::max(0.0f, S.bTruce - 1);
        Txt(TextFormat("truce %.0f d", S.bTruce), x + 238, ly + 2, 13, ink);
        if (SmallBtn({x + 310, ly, 22, 20}, "+")) S.bTruce = std::min(5.0f, S.bTruce + 1);
        if (SmallBtn({x + W - 110, ly, 94, 20}, "send offer")) { Writer o; fl::OrderBarter(o, S.bTo, S.bGive, S.bGet, S.bTruce); Order(o); }
        ly += 24;
        for (int side = 0; side < 2; side++) {
            int* v = side == 0 ? S.bGive : S.bGet;
            Txt(side == 0 ? "you give" : "you want", x + 16, ly + 2, 13, dim);
            for (int g = 0; g < fl::G_COUNT; g++) {
                float bx = x + 90 + g * 108.0f;
                if (SmallBtn({bx, ly, 18, 18}, "-")) v[g] = std::max(0, v[g] - (g == fl::G_PEARLS ? 1 : 5));
                Txt(TextFormat("%d %s", v[g], fl::GoodName(g)), bx + 22, ly + 2, 13, ink);
                if (SmallBtn({bx + 84, ly, 18, 18}, "+")) v[g] += g == fl::G_PEARLS ? 1 : 5;
            }
            ly += 22;
        }
    }
    for (const auto& o : w.offers) {
        if (o.state != 0 || o.to != w.cur || ly > y + H - 30) continue;
        std::string give, get;
        for (int g = 0; g < fl::G_COUNT; g++) { if (o.give[g]) give += TextFormat("%d %s ", o.give[g], fl::GoodName(g)); if (o.get[g]) get += TextFormat("%d %s ", o.get[g], fl::GoodName(g)); }
        if (o.pact) Txt(w.SideName(o.from) + " proposes a feed pact (a feed line both ways)", x + 16, ly + 2, 13, gold);
        else if (o.loanFlock >= 0) Txt(TextFormat("%s lends you a flock for a day, for %d fish", w.SideName(o.from).c_str(), o.get[fl::G_FISH]), x + 16, ly + 2, 13, gold);
        else Txt(TextFormat("%s gives %sfor %s%s", w.SideName(o.from).c_str(), give.empty() ? "nothing " : give.c_str(), get.empty() ? "nothing" : get.c_str(), o.truceDays > 0 ? TextFormat(", truce %.0f d", o.truceDays) : ""), x + 16, ly + 2, 13, gold);
        if (SmallBtn({x + W - 150, ly, 64, 20}, "accept")) { Writer a; fl::OrderAnswer(a, o.id, true); Order(a); }
        if (SmallBtn({x + W - 80, ly, 64, 20}, "refuse")) { Writer a; fl::OrderAnswer(a, o.id, false); Order(a); }
        ly += 24;
    }
}

// The chart (M; design doc pp. 18-19): the world as the colony has seen it. A hand-drawn chart: fog is cloud, old
// information fades to sepia; islands landed on in full, flown over as silhouettes; worked grounds with their yield and a
// shark fin; scouts' sightings with their age; wind; your birds. The report log on the right (click: centre on it), and
// orders: a scout to an island or a ground at a height, the fishers to a ground.
void DrawChart(fl::World& w) {
    const fl::Knowledge& K = w.know;
    if (K.nx == 0) { DrawTextCenteredBold("No chart (a lone island)", SCREEN_W / 2.0f, SCREEN_H / 2.0f, 24, WHITE); return; }
    Rectangle area{24, 24, SCREEN_W - 24 - 330.0f, SCREEN_H - 48.0f};
    Color paper{232, 220, 190, 255}, ink{58, 46, 34, 255}, faded{120, 104, 84, 255}, sea{176, 204, 202, 255}, fog{236, 232, 224, 255};
    DrawRectangleRec({0, 0, (float)SCREEN_W, (float)SCREEN_H}, Fade(BLACK, 0.55f));
    DrawRectangleRec(area, fog);
    float ww = K.nx * K.cell, wh = K.nz * K.cell;
    Vector2 mid{K.x0 + ww / 2, K.z0 + wh / 2};
    if (S.chartAt.x == 0 && S.chartAt.y == 0) S.chartAt = mid;
    float base = std::min(area.width / ww, area.height / wh);
    // zoom about the pointer; pan with the right button
    Vector2 m = GetMousePosition();
    bool over = CheckCollisionPointRec(m, area);
    float sc = base * S.chartZoom;
    auto toS = [&](float x, float z) { return Vector2{area.x + area.width / 2 + (x - S.chartAt.x) * sc, area.y + area.height / 2 + (z - S.chartAt.y) * sc}; };
    auto toW = [&](Vector2 p) { return Vector2{S.chartAt.x + (p.x - area.x - area.width / 2) / sc, S.chartAt.y + (p.y - area.y - area.height / 2) / sc}; };
    if (over) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            Vector2 before = toW(m);
            S.chartZoom = std::clamp(S.chartZoom * powf(1.2f, wheel), 1.0f, 10.0f);
            sc = base * S.chartZoom;
            Vector2 after = toW(m);
            S.chartAt = Vector2Add(S.chartAt, Vector2Subtract(before, after));
        }
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) { Vector2 d = GetMouseDelta(); S.chartAt = Vector2Subtract(S.chartAt, Vector2Scale(d, 1 / sc)); }
    }
    // (no scissor: the scene is supersampled behind a pushed matrix; what spills past the chart is masked after)
    // the fog: cloud where nobody has looked; a sepia wash over what was seen long ago
    {
        int i0 = std::max(0, (int)((toW({area.x, area.y}).x - K.x0) / K.cell)), i1 = std::min(K.nx - 1, (int)((toW({area.x + area.width, 0}).x - K.x0) / K.cell) + 1);
        int j0 = std::max(0, (int)((toW({0, area.y}).y - K.z0) / K.cell)), j1 = std::min(K.nz - 1, (int)((toW({0, area.y + area.height}).y - K.z0) / K.cell) + 1);
        float cs = K.cell * sc;
        for (int j = j0; j <= j1; j++) for (int i = i0; i <= i1; i++) {
            float s = K.seen[(size_t)j * K.nx + i];
            Vector2 p = toS(K.x0 + i * K.cell, K.z0 + j * K.cell);
            if (s < 0) { float h = Hash((float)i, (float)j); DrawRectangleV({p.x - 1, p.y - 1}, {cs + 2, cs + 2}, Color{(unsigned char)(226 + 14 * h), (unsigned char)(222 + 14 * h), (unsigned char)(214 + 12 * h), 255}); }
            else { float age = (w.time - s) / fl::World::DAY; DrawRectangleV({p.x - 0.5f, p.y - 0.5f}, {cs + 1, cs + 1}, Mix(sea, Color{196, 170, 126, 255}, std::clamp((age - 1) * 0.25f, 0.0f, 0.6f))); }
        }
    }
    // islands (under the fog: a cell nobody has seen hides what's there)
    auto isleFill = [&](fl::IsleType t) {
        switch (t) {
        case fl::IsleType::Stack: return Color{196, 190, 176, 255}; case fl::IsleType::Town: return Color{200, 180, 140, 255}; case fl::IsleType::Atoll: return Color{226, 210, 160, 255};
        case fl::IsleType::Volcano: return Color{120, 104, 92, 255}; case fl::IsleType::KrakenCove: return Color{130, 124, 116, 255}; case fl::IsleType::ReefGarden: return Color{226, 200, 150, 255};
        case fl::IsleType::Wreck: return Color{110, 90, 70, 255}; default: return Color{150, 180, 110, 255};
        }
    };
    for (size_t i = 0; i < w.isles.size(); i++) {
        const fl::Island& is = w.isles[i];
        int st = K.isle[i];
        if (st == 0) continue;
        Vector2 c = toS(is.c.x, is.c.z);
        Color fill = st == 2 ? isleFill(is.type) : Color{168, 156, 134, 255};
        // the reef garden's flat and the cove's water first, as rings
        if (is.type == fl::IsleType::ReefGarden && st == 2) DrawCircleLinesV(c, 100 * sc, Fade(ink, 0.5f));
        const auto& o = is.outline;
        for (size_t k = 0; k < o.size(); k++) { Vector2 a = toS(o[k].x, o[k].y), b = toS(o[(k + 1) % o.size()].x, o[(k + 1) % o.size()].y); if (Vector2Distance(a, c) > 0.5f || Vector2Distance(b, c) > 0.5f) DrawTri(c, a, b, fill); }
        if (is.type == fl::IsleType::Atoll || is.type == fl::IsleType::KrakenCove) {   // (the inner water)
            float r = is.type == fl::IsleType::Atoll ? 58.0f : 54.0f;
            DrawCircleV(c, r * sc, sea);
        }
        for (size_t k = 0; k < o.size(); k++) { Vector2 a = toS(o[k].x, o[k].y), b = toS(o[(k + 1) % o.size()].x, o[(k + 1) % o.size()].y); if (Vector2Distance(a, c) > 0.5f) DrawLineEx(a, b, 1.6f, ink); }
        if (is.type == fl::IsleType::Wreck) DrawRectanglePro({c.x, c.y, 8, 24}, {4, 12}, 11, ink);
        if (st == 2) for (const auto& s : is.sites) { Vector2 p = toS(s.x, s.z); DrawCircleV(p, 1.5f, Fade(ink, 0.6f)); }
    }
    // grounds your fishers and scouts have worked: the yield and a shark fin (darker: more sharks)
    for (int z = 0; z < (int)K.ground.size(); z++) {
        const fl::GroundInfo& g = K.ground[z];
        if (g.t < 0) continue;
        const rt::Zone& Z = w.eco.map->zones[z];
        Vector2 a = toS(Z.plan.x, Z.plan.y), b = toS(Z.plan.x + Z.plan.width, Z.plan.y + Z.plan.height);
        float age = (w.time - g.t) / fl::World::DAY;
        Color c = Fade(age > 1 ? faded : Color{40, 80, 110, 255}, 0.75f);
        DrawRectangleLinesEx({a.x, a.y, b.x - a.x, b.y - a.y}, 1, Fade(c, 0.5f));
        const char* y = g.stock > 0.75f ? "abundant" : g.stock > 0.45f ? "fair" : g.stock > 0.15f ? "thin" : "fished out";
        Vector2 ctr{(a.x + b.x) / 2, (a.y + b.y) / 2};
        if (b.x - a.x > 50) {
            DrawTextCentered(y, ctr.x, ctr.y - 7, 13, c);
            Color fin = Mix(Color{190, 190, 180, 255}, Color{20, 20, 20, 255}, g.predators);
            DrawTri({ctr.x + 26, ctr.y + 10}, {ctr.x + 36, ctr.y + 10}, {ctr.x + 34, ctr.y}, fin);
            if (age > 0.5f) DrawTextCentered(TextFormat("%.0f d ago", age), ctr.x, ctr.y + 7, 11, faded);
        }
    }
    // names and the scouts' sightings, over the fog where they're known
    for (size_t i = 0; i < w.isles.size(); i++) {
        const fl::Island& is = w.isles[i];
        if (K.isle[i] == 0) continue;
        Vector2 c = toS(is.c.x, is.c.z);
        std::string nm = K.isle[i] == 2 || K.sight[i].t >= 0 ? is.name : std::string("? ") + fl::IsleTypeName(is.type);
        DrawTextCentered(nm, c.x, c.y + is.radius * sc + 3, 13, ink);
        if (K.isle[i] == 2 || K.sight[i].t >= 0) { int h = w.HolderOf((int)i); if (h >= 0) { Vector2 fp{c.x + MeasureText(nm.c_str(), 13) / 2.0f + 6, c.y + is.radius * sc + 2}; DrawLineEx(fp, {fp.x, fp.y + 14}, 1.5f, ink); DrawTri(fp, {fp.x + 10, fp.y + 3}, {fp.x, fp.y + 6}, w.SideColor(h)); } }
        const fl::Sighting& s = K.sight[i];
        if (s.t >= 0) {
            float age = (w.time - s.t) / fl::World::DAY;
            Color c2 = age > 1 ? faded : Color{120, 30, 24, 255};
            DrawTextCentered(TextFormat("%s%d nests, %d caches, %s%d birds", s.exact ? "" : "~", s.nests, s.caches, s.exact ? "" : "~", s.birds), c.x, c.y + is.radius * sc + 17, 12, c2);
            DrawTextCentered(TextFormat("seen %s, %.1f days ago%s", fl::AltName((fl::Alt)s.alt), age, s.scouts > 1 ? ", cross-checked" : ""), c.x, c.y + is.radius * sc + 30, 11, faded);
        }
    }
    // the wind
    Vector2 wv = w.WindAt();
    for (float z = K.z0 + 150; z < K.z0 + wh; z += 400) for (float x = K.x0 + 150; x < K.x0 + ww; x += 400) {
        if (!K.Seen(x, z)) continue;
        Vector2 p = toS(x, z), d = Vector2Scale(Vector2Normalize(wv), 14);
        DrawLineEx(p, Vector2Add(p, d), 1.2f, Fade(ink, 0.35f));
        DrawCircleV(Vector2Add(p, d), 1.8f, Fade(ink, 0.35f));
    }
    // your birds: the Founder, the colony (by role), scouts and their targets
    for (const auto& b : w.col.birds) {
        if (!b.alive || b.stage != fl::BStage::Adult) continue;
        Vector2 p = toS(b.pos.x, b.pos.z);
        Color c = b.role == fl::Role::Fisher ? Color{40, 90, 150, 255} : b.role == fl::Role::Feeder ? Color{200, 120, 40, 255} : b.role == fl::Role::Builder ? Color{120, 84, 50, 255} : Color{170, 30, 30, 255};
        if (b.role == fl::Role::Scout && b.hasOrder) { Vector2 tgt = toS(b.scoutAt.x, b.scoutAt.z); DrawLineEx(p, tgt, 1, Fade(c, 0.6f)); DrawTextCentered(fl::AltName(b.alt), p.x, p.y - 14, 11, c); }
        DrawCircleV(p, b.role == fl::Role::Scout ? 3.0f : 2.0f, c);
    }
    // flocks: yours with their names; an enemy's where your birds can see it now
    for (int s = 0; s <= (int)w.sides.size(); s++) for (const auto& fk : w.ColOf(s).flocks) {
        if (fk.members.empty()) continue;
        if (s != w.cur && K.SeenAt(fk.pos.x, fk.pos.z) < w.time - 2) continue;
        Vector2 p = toS(fk.pos.x, fk.pos.z);
        Color c = s == w.cur ? Color{40, 110, 60, 255} : w.SideColor(s);
        DrawCircleV(p, 9, Fade(c, 0.85f)); DrawCircleLinesV(p, 9, ink);
        DrawTextCentered(TextFormat("%d", (int)fk.members.size()), p.x, p.y - 6, 12, WHITE);
        if (s == w.cur) DrawTextCentered(fk.name + (S.selFlock == fk.id ? " *" : ""), p.x, p.y + 11, 11, ink);
        else DrawTextCentered(w.SideName(s), p.x, p.y + 11, 11, c);
    }
    if (w.me.st != fl::FState::Dead) {
        Vector2 p = toS(w.me.pos.x, w.me.pos.z), f{cosf(w.me.yaw), sinf(w.me.yaw)};
        DrawTri(Vector2Add(p, Vector2Scale(f, 9)), Vector2Add(p, Vector2Scale({-f.y, f.x}, 5)), Vector2Add(p, Vector2Scale({f.y, -f.x}, 5)), Color{220, 160, 30, 255});
    }
    {   // mask what spilled past the chart's edge
        Color mk{14, 18, 24, 255};
        DrawRectangle(0, 0, SCREEN_W, (int)area.y, mk); DrawRectangle(0, (int)(area.y + area.height), SCREEN_W, SCREEN_H, mk);
        DrawRectangle(0, 0, (int)area.x, SCREEN_H, mk); DrawRectangle((int)(area.x + area.width), 0, SCREEN_W, SCREEN_H, mk);
    }
    DrawRectangleLinesEx(area, 3, ink);
    TxtBold("The Chart", area.x + 12, area.y + 8, 22, ink);
    Txt(TextFormat("%s, %d islands   M closes, wheel zooms, right-drag pans", fl::ArrangementName(w.opts.arr), (int)w.isles.size()), area.x + 140, area.y + 13, 14, faded);
    // the side: orders, then the report log
    Rectangle side{area.x + area.width + 12, area.y, 306, area.height};
    DrawRectangleRec(side, paper); DrawRectangleLinesEx(side, 2, ink);
    float y = side.y + 10;
    TxtBold("Orders", side.x + 12, y, 18, ink); y += 26;
    int idle = 0, scouts = 0; for (const auto& b : w.col.birds) if (b.alive && b.stage == fl::BStage::Adult && b.role == fl::Role::Scout) { scouts++; idle += !b.hasOrder && b.retrainT <= 0; }
    {   static const char* modes[4] = {"Scout", "Fishers", "Flock", "Outpost"};
        for (int k = 0; k < 4; k++) if (SmallBtn({side.x + 12 + k * 71.0f, y, 66, 24}, modes[k], true)) S.chartMode = k;
        DrawRectangleLinesEx({side.x + 12 + S.chartMode * 71.0f - 2, y - 2, 70, 28}, 2, Color{200, 60, 40, 255}); }
    y += 32;
    if (S.chartMode == 0) {
        static const fl::Alt alts[3] = {fl::Alt::High, fl::Alt::Mid, fl::Alt::Low};
        for (int k = 0; k < 3; k++) { if (SmallBtn({side.x + 12 + k * 64.0f, y, 58, 24}, fl::AltName(alts[k]), true)) S.chartAlt = alts[k]; if (S.chartAlt == alts[k]) DrawRectangleLinesEx({side.x + 10 + k * 64.0f, y - 2, 62, 28}, 2, Color{200, 60, 40, 255}); }
        y += 30;
        DrawWrapped(scouts == 0 ? "No scouts: retrain a bird as a Scout on the colony panel (Tab), or set some in the fledging plan." :
                    TextFormat("%d of %d scouts idle. Click an island or a ground. High sees far but counts nests to 30%%; mid and low count exactly.", idle, scouts),
                    {side.x + 12, y, side.width - 24, 60}, 13, ink);
    } else if (S.chartMode == 1) {
        DrawWrapped(TextFormat("Click a ground: the fishers work it (now: %s).", w.col.ground < 0 ? "the best ground" : w.eco.map->zones[w.col.ground].name.c_str()), {side.x + 12, y, side.width - 24, 40}, 13, ink);
        if (SmallBtn({side.x + 12, y + 36, 150, 24}, "the best ground", true)) { Writer o; fl::OrderGround(o, -1); Order(o); }
    } else if (S.chartMode == 3) {
        int pf = w.Count(fl::BStage::Adult, fl::Role::Pathfinder);
        DrawWrapped(!w.RoleUnlocked(fl::Role::Pathfinder) ? "Outposts want a Pathfinder (Trade 4), or the Founder: land on a free site of another island and press E." :
                    TextFormat("%d Pathfinders. Click another island: one flies out and lays a nest and a cache there%s. Holding an island scores; a dangerous one scores more.", pf, w.col.expandTo >= 0 ? TextFormat(" (now bound for %s)", w.isles[w.col.expandTo].name.c_str()) : ""),
                    {side.x + 12, y, side.width - 24, 70}, 13, ink);
    } else {
        fl::Flock* sf = w.FindFlock(w.cur, S.selFlock);
        if (!sf && !w.col.flocks.empty()) { S.selFlock = w.col.flocks[0].id; sf = &w.col.flocks[0]; }
        if (sf && SmallBtn({side.x + 12, y, 130, 22}, sf->name.c_str(), true)) { size_t k = 0; for (; k < w.col.flocks.size(); k++) if (w.col.flocks[k].id == sf->id) break; S.selFlock = w.col.flocks[(k + 1) % w.col.flocks.size()].id; }
        if (SmallBtn({side.x + 150, y, 140, 22}, S.raidChicks ? "raids take chicks" : "raids take fish", true)) S.raidChicks = !S.raidChicks;
        DrawWrapped(sf ? "Click: a rival island to raid, a ground to harass, an enemy flock to meet, your island to guard." : "No flocks: form one on the Flocks page (Tab twice).", {side.x + 12, y + 28, side.width - 24, 40}, 13, ink);
    }
    y += 76;
    // a click on the chart: an order
    if (over && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 at = toW(m);
        int isle = -1; for (int i = 0; i < (int)w.isles.size(); i++) if (Vector2Distance(at, {w.isles[i].c.x, w.isles[i].c.z}) < w.isles[i].radius + 15) isle = i;
        int zone = w.eco.ZoneAt({at.x, -1, at.y});
        if (S.chartMode == 0) { Writer o; fl::OrderScout(o, isle, isle < 0 ? zone : -1, {at.x, 0, at.y}, S.chartAlt); Order(o); }
        else if (S.chartMode == 1) { if (zone >= 0) { Writer o; fl::OrderGround(o, zone); Order(o); } }
        else if (S.chartMode == 3) { if (isle >= 0 && isle != w.home) { Writer o; fl::OrderFound(o, isle); Order(o); } }
        else if (fl::Flock* sf = w.FindFlock(w.cur, S.selFlock)) {
            // an enemy flock where you clicked (one you can see now), an island (yours: guard it; a rival's: raid it), a ground, a mark
            int ts = -1, tf = -1; float bd = 40 / std::max(0.3f, S.chartZoom) * 3;
            for (int s = 0; s <= (int)w.sides.size(); s++) if (s != w.cur) for (const auto& ef : w.ColOf(s).flocks) if (K.SeenAt(ef.pos.x, ef.pos.z) > w.time - 2 && Vector2Distance(at, {ef.pos.x, ef.pos.z}) < bd) { bd = Vector2Distance(at, {ef.pos.x, ef.pos.z}); ts = s; tf = ef.id; }
            int owner = isle >= 0 ? w.OwnerOf(isle) : -1;
            if (owner < 0 && isle >= 0) owner = w.HolderOf(isle);   // (an outpost)
            Writer o;
            if (tf >= 0) fl::OrderFlockTarget(o, sf->id, fl::Target::Flock, ts, -1, -1, tf, {});
            else if (isle == w.home) fl::OrderFlockTarget(o, sf->id, fl::Target::Home, -1, -1, -1, -1, {});
            else if (owner >= 0 && owner != w.cur) fl::OrderFlockTarget(o, sf->id, S.raidChicks ? fl::Target::Nests : fl::Target::Cache, owner, isle, -1, -1, w.isles[isle].c);
            else if (zone >= 0 && isle < 0) fl::OrderFlockTarget(o, sf->id, fl::Target::Ground, -1, -1, zone, -1, {at.x, 0, at.y});
            else fl::OrderFlockTarget(o, sf->id, fl::Target::Point, -1, -1, -1, -1, {at.x, 0, at.y});
            Order(o);
        }
    }
    TxtBold("Reports", side.x + 12, y, 18, ink); y += 24;
    for (int k = (int)K.log.size() - 1; k >= 0 && y < side.y + side.height - 30; k--) {
        const fl::Report& r = K.log[k];
        float day = r.t / fl::World::DAY;
        Rectangle row{side.x + 8, y - 2, side.width - 16, 44};
        bool hov = CheckCollisionPointRec(m, row);
        if (hov) DrawRectangleRec(row, Fade(Color{200, 170, 110, 255}, 0.35f));
        if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) { S.chartAt = {r.at.x, r.at.z}; S.chartZoom = 3; }
        Txt(TextFormat("day %d, %02d:00", (int)day + 1, (int)(fmodf(day, 1) * 24)), side.x + 12, y, 11, faded);
        DrawWrapped(r.text, {side.x + 12, y + 12, side.width - 24, 30}, 12, ink);
        y += 46;
    }
}

void Render(float dt) {
    fl::World& w = WD();
    fl::Founder& f = w.me;
    EnsureModels(); EnsureSea();
    // ---- the camera: behind and above the bird along the aim; close in for the strike
    float k = std::min(1.0f, dt * 5);
    S.camYaw += atan2f(sinf(S.aimYaw - S.camYaw), cosf(S.aimYaw - S.camYaw)) * k;
    S.camPitch += (S.aimPitch * 0.8f - 0.12f - S.camPitch) * k;
    float span = w.Def().span;
    bool sitting = f.st == fl::FState::Perched || f.st == fl::FState::Floating || f.st == fl::FState::Fainted;
    static float distK = 1; distK += ((sitting ? 0.0f : 1.0f) - distK) * std::min(1.0f, dt * 2);
    float dist = (2.0f + span * 0.9f) + distK * (3.5f + span * 0.7f + std::min(Vector3Length(f.vel), 30.0f) * 0.08f);   // (close in at rest, back in flight)
    Vector3 look{cosf(S.camPitch) * cosf(S.camYaw), sinf(S.camPitch), cosf(S.camPitch) * sinf(S.camYaw)};
    Vector3 focus = f.pos;
    if (f.st == fl::FState::Dead) focus = w.island.nest;
    if (f.st == fl::FState::Strike) { focus = Vector3Lerp(f.pos, f.strikeAim, 0.5f); dist *= 0.75f; }
    Vector3 eye = Vector3Add(Vector3Subtract(focus, Vector3Scale(look, dist)), {0, 1.2f + span * 0.4f, 0});
    float g = std::max(0.0f, w.HeightAt(eye.x, eye.z));
    eye.y = std::max(eye.y, g + 0.8f);
    S.cam.position = eye;
    S.cam.target = Vector3Add(focus, Vector3Scale(look, 4));
    S.cam.up = {0, 1, 0};
    S.cam.fovy = 62 + std::clamp((Vector3Length(f.vel) - 10) * 0.6f, 0.0f, 16.0f);
    S.cam.projection = CAMERA_PERSPECTIVE;
    // ---- the light: the sun (the renderer's "moon" is its directional light), the sky's ambient, haze
    DayLook day = Day(w.DayPhase());
    rt::SceneLight L;
    L.fog = day.horizon; L.fogDensity = 0.0019f; L.fogBanks = 0;
    L.key = {0, 0, 0, 255}; L.lampRange = 1; L.lampPos = {0, -500, 0}; L.lampDir = {0, -1, 0};
    L.fill = Shade(day.amb, 0.5f); L.rim = Shade(day.horizon, 0.6f);
    L.moonDir = Vector3Negate(day.toSun); L.moon = day.sun; L.moonK = day.sunK;
    L.skyAmb = day.amb; L.seaAmb = Shade(day.amb, 0.45f); L.ambK = day.ambK;
    L.surfaceY = 1e5f; L.time = S.t;
    L.outline = 0.45f; L.outlineTint = {36, 44, 52, 255}; L.stipple = 0; L.grain = 0.25f;
    L.aoK = 0.5f; L.aoRadius = 0.5f;
    L.filmic = 0; L.saturation = 1.35f;   // (the filmic curve blows a daylit sky out to white: daylight goes without it)
    // the weather and the volcano's ash (stage 7): a storm darkens and closes the sky, fog shuts the sea in, ash greys it
    static float stormK = 0, fogK = 0, ashK = 0;
    { float e = std::min(1.0f, dt * 0.5f);
      stormK += ((w.weather.kind == 1 ? 1.0f : 0.0f) - stormK) * e; fogK += ((w.weather.kind == 2 ? 1.0f : 0.0f) - fogK) * e;
      float ashNear = 0; if (w.volcano.isle >= 0 && w.volcano.ashT > 0) ashNear = std::clamp(1 - (Vector3Distance(S.cam.position, w.isles[w.volcano.isle].c) - 150) / 350, 0.0f, 1.0f);
      ashK += (ashNear - ashK) * e; }
    L.fogDensity *= 1 + stormK * 1.6f + fogK * 5.0f + ashK * 3.0f;
    L.fog = Mix(L.fog, Color{96, 104, 112, 255}, stormK * 0.7f); L.fog = Mix(L.fog, Color{206, 212, 214, 255}, fogK * 0.8f); L.fog = Mix(L.fog, Color{112, 104, 98, 255}, ashK * 0.8f);
    L.moonK *= 1 - 0.6f * stormK - 0.35f * fogK - 0.4f * ashK;
    rt::ApplyGameQuality();
    rt::RenderBegin(S.cam, L);
    rt::SkyLook sk;
    sk.zenith = day.zenith; sk.horizon = day.horizon; sk.cloud = day.cloud;
    sk.moonDir = day.toSun; sk.moonPhase = 0.5f; sk.cloudCover = 0.32f + 0.6f * stormK + 0.3f * ashK; sk.zenith = Mix(sk.zenith, Color{80, 88, 98, 255}, stormK * 0.7f + ashK * 0.5f); sk.horizon = Mix(sk.horizon, L.fog, std::max(fogK, stormK) * 0.8f); sk.stars = day.night; sk.time = S.t;
    rt::DrawSkyDome(sk);
    StepWarFx(w, dt);
    DrawWorld(w, S.cam, dt);
    UpdateSea(S.cam.position, S.t);
    rt::WaterLook wl;
    wl.deep = Mix(Color{8, 20, 34, 255}, Color{34, 150, 160, 255}, 1 - day.night);
    wl.zenith = day.zenith; wl.horizon = day.horizon;
    wl.boatPos = {1e6f, 1e6f}; wl.boatLen = 0.1f; wl.boatBeam = 0.1f;
    wl.alpha = 0.66f; wl.moonK = day.sunK * 0.8f * (1 - 0.6f * stormK); wl.crest = 0.04f + 0.12f * stormK;
    // blood in the water where a fish fought the talons
    if (f.st == fl::FState::Struggle) { wl.stains = 1; wl.stain[0] = {f.pos.x, f.pos.z, 1.5f, 0.8f}; }
    for (const auto& s : gStains) if (wl.stains < 8) wl.stain[wl.stains++] = {s.p.x, s.p.y, 0.8f + std::min(3.5f, s.age * 0.3f), std::clamp(1 - s.age / 40, 0.0f, 1.0f)};   // (where a bird went into the sea)
    rt::DrawWater(S.sea, wl);
    rt::RenderEnd();
}

void ResetFlightSound();
void Start(Game& g, const std::string& founder, uint32_t seed, bool shot, const fl::MapOpts& o = fl::MapOpts{}) {
    std::string why;
    if (!rt::DataOk(&why)) { g.scene = Scene::Arcade; return; }
    S.active = true; S.shot = shot; S.founder = founder; S.opts = o;
    S.W.Init(founder, seed, o);
    ResetFlightSound();
    S.W.LookOf(0) = shot ? std::string() : MyLook();   // (your costume and livery; shots wear none unless they say)

    S.chart = false; S.panel = false; S.chartZoom = 1; S.chartAt = {0, 0};
    S.aimYaw = WD().me.yaw; S.aimPitch = 0.1f; S.camYaw = S.aimYaw; S.camPitch = -0.1f;
    S.t = 0; S.help = true;
    g.scene = Scene::Flight;
}
// ---------------------------------------------------------------- the Roost wardrobe (stage 8; doc pp. 28-30)
// From the arcade's Flight reel: the token shop, the egg crate (tap an egg), the colony liveries and what you own, with
// your Founder turning on a perch in the costume picked. Its own bird models (a preview), freed when a match starts.
struct WardrobeUi { int tab = 0, sel = -1, scroll = 0; std::string pick, msg; float msgT = 0, hatchT = 0; int hatchTier = -1; bool shotMode = false; };
WardrobeUi gWr;
int gPreviewFor = -1;
void FreePreview() {
    if (gPreviewFor < 0) return;
    for (Model* m : {&S.body, &S.head, &S.beak, &S.tail, &S.wingIn, &S.wingOut, &S.egg, &S.chick}) UnloadModel(*m);
    gPreviewFor = -1;
}
void BuildEggChick(const fl::FounderDef& d) {
    { rt::MeshBuilder mb; mb.Lathe(0.075f, 5, 8, [](float u) { return 0.028f * sinf(std::max(0.05f, u) * PI) * (1.1f - 0.25f * u); }, [](float u) { return 0.028f * sinf(std::max(0.05f, u) * PI) * (1.1f - 0.25f * u); }, {244, 238, 226, 255}, {226, 218, 204, 255}); S.egg = LoadModelFromMesh(mb.Build()); }
    { rt::MeshBuilder mb;
      Color down = Mix(d.belly, Color{200, 196, 186, 255}, 0.6f);
      mb.Lathe(0.16f, 5, 8, [](float u) { return 0.075f * sinf(std::max(0.08f, u) * PI); }, [](float u) { return 0.07f * sinf(std::max(0.08f, u) * PI); }, down, Shade(down, 0.85f), {0, 0.07f, 0});
      mb.Lathe(0.09f, 4, 8, [](float u) { return 0.045f * sinf(std::max(0.08f, u) * PI); }, [](float u) { return 0.045f * sinf(std::max(0.08f, u) * PI); }, down, Shade(down, 0.9f), {0, 0.16f, 0.06f});
      mb.Cone({0, 0.16f, 0.1f}, {0, 0.17f, 0.15f}, 0.018f, 5, d.accent);
      S.chick = LoadModelFromMesh(mb.Build()); }
}
void EnsurePreview(int fi) {
    if (!IsWindowReady()) return;
    if (S.ready) FreeModels();   // (a match's models: the preview builds its own bird)
    if (gPreviewFor == fi) return;
    FreePreview();
    const fl::FounderDef& d = fl::Founders()[std::clamp(fi, 0, (int)fl::Founders().size() - 1)];
    BuildBird(d); BuildEggChick(d);
    gPreviewFor = fi;
}
// the 3D: noon light over a sea-coloured backdrop; the birds placed by the caller
void PreviewBegin(Camera3D& cam) {
    DayLook day = Day(0.45f);
    rt::SceneLight L;
    L.fog = day.horizon; L.fogDensity = 0.0008f; L.fogBanks = 0;
    L.key = {0, 0, 0, 255}; L.lampRange = 1; L.lampPos = {0, -500, 0}; L.lampDir = {0, -1, 0};
    L.fill = Shade(day.amb, 0.5f); L.rim = Shade(day.horizon, 0.6f);
    L.moonDir = Vector3Negate(day.toSun); L.moon = day.sun; L.moonK = day.sunK;
    L.skyAmb = day.amb; L.seaAmb = Shade(day.amb, 0.45f); L.ambK = day.ambK;
    L.surfaceY = 1e5f; L.time = S.t;
    L.outline = 0.45f; L.outlineTint = {36, 44, 52, 255}; L.stipple = 0; L.grain = 0.25f;
    L.aoK = 0.5f; L.aoRadius = 0.5f; L.filmic = 0; L.saturation = 1.3f;
    rt::ApplyGameQuality();
    rt::RenderBegin(cam, L);
    rt::SkyLook sk; sk.zenith = day.zenith; sk.horizon = day.horizon; sk.cloud = day.cloud; sk.moonDir = day.toSun; sk.moonPhase = 0.5f; sk.cloudCover = 0.3f; sk.stars = 0; sk.time = S.t;
    rt::DrawSkyDome(sk);
    rt::DrawWorldCube({0, -60, 0}, {600, 1, 600}, Color{40, 130, 150, 255});
}
// one Founder on a perch, wearing a look (and, for a livery, three colony birds beside it)
void PreviewBird(const fl::FounderDef& d, Vector3 at, float yaw, const fl::CostumeLook* look, bool flying, const SideLook* livery = nullptr, float size = 1) {
    float L = (0.22f + d.span * 0.14f) * size;
    if (!flying) rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(L * 1.5f, L * 0.4f, L * 1.1f), MatrixRotateY(0.4f)), MatrixTranslate(at.x, at.y - L * 0.5f, at.z)), Color{150, 140, 120, 255});
    gLook = look;
    float b = sinf(S.t * 6);
    DrawBirdBody(d, PoseWorld(at, yaw, 0, 0, size), flying ? 0.5f * b : 0, flying ? 0.3f * b : 0, flying ? 0 : 1.0f, 0, 0.1f * sinf(S.t * 1.3f), 0.8f, WHITE);
    gLook = nullptr;
    if (look) DrawCostumeFx(*look, at, {cosf(yaw) * 4, 0, sinf(yaw) * 4}, yaw, L, false, false);
    if (livery && (livery->colour || livery->hat)) for (int k = 0; k < 3; k++) {
        Vector3 p{at.x - L * 2.0f + L * 2.0f * k, at.y - L * 0.3f, at.z - L * 3.2f};
        rt::DrawCubeM(MatrixMultiply(MatrixScale(L * 1.0f, L * 0.3f, L * 0.8f), MatrixTranslate(p.x, p.y - L * 0.4f, p.z)), Color{150, 140, 120, 255});
        gLook = livery->hat ? &livery->hatLook : nullptr;
        DrawBirdBody(d, PoseWorld(p, yaw + 0.3f * (k - 1), 0, 0, 0.75f), 0, 0, 1.0f, 0, 0.1f * sinf(S.t * 2 + k), 0.7f, livery->colour ? Mix(WHITE, livery->colour->c, 0.6f) : WHITE);
        gLook = nullptr;
    }
}
bool WardrobeRow(Rectangle r, const std::string& name, int tier, const std::string& right, bool sel, bool worn) {
    bool hover = CheckCollisionPointRec(GetMousePosition(), r);
    DrawRectangleRounded(r, 0.25f, 4, sel ? Color{90, 66, 40, 230} : hover ? Color{44, 52, 60, 230} : Color{24, 32, 40, 210});
    DrawRectangle((int)r.x + 4, (int)r.y + 5, 4, (int)r.height - 10, fl::CostumeTierColor(tier));
    TxtBold(name + (worn ? "  (worn)" : ""), r.x + 16, r.y + 5, 15, worn ? Color{255, 226, 150, 255} : Color{246, 242, 230, 255});
    Txt(right, r.x + r.width - 8 - MeasureText(right.c_str(), 13), r.y + 7, 13, Color{200, 214, 214, 255});
    bool hit = hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (hit) PlayCue("ui.click");
    return hit;
}
}  // namespace

bool FlightWardrobePage(int founder) {
    float dt = std::min(GetFrameTime(), 1 / 30.0f);
    S.t += dt;
    EnsurePreview(founder);
    const fl::FounderDef& d = fl::Founders()[std::clamp(founder, 0, (int)fl::Founders().size() - 1)];
    fl::FlightWardrobe& W = fl::Wardrobe();
    const fl::CostumeData& D = fl::Costumes();
    // what's on the perch: the row picked, else what's worn
    const fl::CostumeDef* show = fl::FindCostume(gWr.pick.empty() ? W.costume : gWr.pick);
    SideLook liv = ParseLook(";" + W.liveryColour + ";" + W.liveryHat);
    if (const fl::LiveryColour* c = fl::FindLiveryColour(gWr.pick)) liv.colour = c;
    if (const fl::LiveryHat* h = fl::FindLiveryHat(gWr.pick)) { liv.hat = h; liv.hatLook = fl::CostumeLook{}; liv.hatLook.hat = h->hat; liv.hatLook.hatC = h->c; }
    // ---- the 3D: the Founder turning on its perch
    Camera3D cam{}; cam.up = {0, 1, 0}; cam.fovy = 30; cam.projection = CAMERA_PERSPECTIVE;
    float sc = show ? std::min(show->look.scale, 1.6f) : 1.0f;
    float span = 0.6f + d.span * 0.35f;   // (frame the bird by its size, on the right half of the screen)
    cam.target = {-0.55f * span * sc, 0.1f * sc, -0.3f}; cam.position = {-0.55f * span * sc, 0.6f + 0.4f * sc, 2.7f * span * sc};
    PreviewBegin(cam);
    PreviewBird(d, {0, 0, 0}, -PI * 0.5f + S.t * 0.6f, show ? &show->look : nullptr, false, &liv);
    rt::RenderEnd();
    // ---- the page (the left half; the bird stays visible on the right)
    Color ink{250, 248, 236, 255}, dim{200, 214, 214, 255}, gold{255, 220, 150, 255};
    DrawRectangleRounded({16, 16, 480, SCREEN_H - 32.0f}, 0.03f, 6, Fade(Color{8, 18, 28, 255}, 0.88f));
    DrawRectangleRoundedLinesEx({16, 16, 480, SCREEN_H - 32.0f}, 0.03f, 6, 2, Color{214, 180, 110, 255});
    TxtBold("The Roost wardrobe", 32, 26, 24, ink);
    Txt(TextFormat("%s   %d tokens   %d crates   %d feathers", d.name.c_str(), W.tokens, W.crates, W.feathers), 32, 56, 14, gold);
    static const char* TABS[4] = {"Shop", "Eggs", "Liveries", "Mine"};
    for (int k = 0; k < 4; k++) { if (SmallBtn({32 + k * 112.0f, 80, 104, 26}, TABS[k])) { gWr.tab = k; gWr.pick.clear(); gWr.scroll = 0; } if (gWr.tab == k) DrawRectangleLinesEx({30 + k * 112.0f, 78, 108, 30}, 2, Color{200, 60, 40, 255}); }
    float y = 118;
    auto act = [&](const std::string& id, int price, bool buyable) {   // the buy / wear button under the list
        bool own = W.Owns(id), worn = W.costume == id || W.liveryColour == id || W.liveryHat == id;
        std::string why;
        if (!own && buyable) { if (SmallBtn({32, SCREEN_H - 82.0f, 220, 30}, TextFormat("Buy for %d tokens", price), W.tokens >= price)) { if (fl::BuyCostume(id, &why)) { gWr.msg = "Yours."; PlayCue("arc.win"); } else gWr.msg = why; gWr.msgT = 3; } }
        else if (own && SmallBtn({32, SCREEN_H - 82.0f, 220, 30}, worn ? "Take it off" : "Wear it")) { if (worn && fl::FindCostume(id)) fl::WearCostume(""); else fl::WearCostume(id); }
    };
    if (gWr.tab == 0 || gWr.tab == 3) {   // the shop, or everything you own
        int row = 0;
        for (const auto& c : D.costumes) {
            if (gWr.tab == 0 && c.tier != fl::CT_SHOP) continue;
            if (gWr.tab == 3 && !W.Owns(c.id)) continue;
            if (row++ < gWr.scroll) continue;
            if (y > SCREEN_H - 150) break;
            std::string right = W.Owns(c.id) ? std::string(fl::CostumeTierName(c.tier)) : TextFormat("%d tokens", c.price);
            if (WardrobeRow({32, y, 448, 30}, c.name, c.tier, right, gWr.pick == c.id, W.costume == c.id)) gWr.pick = c.id;
            y += 34;
        }
        if (row == 0) DrawWrapped(gWr.tab == 3 ? "Nothing yet: buy one in the shop, or hatch an egg." : "", {32, y, 440, 40}, 14, dim);
        if (GetMouseWheelMove() != 0 && GetMousePosition().x < 496) gWr.scroll = std::clamp(gWr.scroll - (int)GetMouseWheelMove(), 0, std::max(0, row - 10));
        if (const fl::CostumeDef* c = fl::FindCostume(gWr.pick)) { DrawWrapped(c->note, {32, SCREEN_H - 128.0f, 440, 40}, 14, dim); act(c->id, c->price, c->tier == fl::CT_SHOP); }
    } else if (gWr.tab == 1) {   // the egg crate: a nest of eggs; tap one
        DrawWrapped(TextFormat("One token, one crate. Tap an egg and it hatches a costume: common %d%%, rare %d%%, super rare %d%%, special %d%%. A costume you already have hatches a feather and nothing else.", D.odds[fl::CT_COMMON], D.odds[fl::CT_RARE], D.odds[fl::CT_SUPER], D.odds[fl::CT_SPECIAL]), {32, y, 440, 70}, 14, dim);
        y += 80;
        if (SmallBtn({32, y, 220, 30}, TextFormat("Buy a crate (%d token)", D.cratePrice), W.tokens >= D.cratePrice)) { std::string why; if (!fl::BuyCrate(&why)) { gWr.msg = why; gWr.msgT = 3; } }
        y += 50;
        Vector2 nc{256, y + 90};
        DrawEllipse((int)nc.x, (int)nc.y + 40, 190, 50, Color{120, 92, 60, 255});
        DrawEllipse((int)nc.x, (int)nc.y + 34, 170, 40, Color{90, 68, 44, 255});
        for (int k = 0; k < 5; k++) {
            Vector2 e{nc.x - 120 + k * 60.0f, nc.y + 10 + 6 * (k % 2)};
            bool have = k < W.crates;
            bool hover = have && CheckCollisionPointCircle(GetMousePosition(), e, 26);
            float wob = hover ? 3 * sinf(S.t * 20) : 0;
            DrawEllipse((int)(e.x + wob), (int)e.y, 22, 30, have ? (hover ? Color{255, 246, 226, 255} : Color{240, 232, 214, 255}) : Fade(Color{240, 232, 214, 255}, 0.18f));
            if (have) DrawEllipse((int)(e.x + wob - 6), (int)e.y - 10, 5, 8, Fade(WHITE, 0.6f));
            if (hover && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                fl::EggRoll r = fl::OpenEgg((uint32_t)(GetTime() * 1000) ^ (uint32_t)(W.feathers * 7919 + W.owned.size() * 104729));
                if (r.ok) { gWr.hatchT = 2.5f; gWr.hatchTier = r.tier; gWr.pick = r.duplicate ? "" : r.id; const fl::CostumeDef* c = fl::FindCostume(r.id);
                            gWr.msg = r.duplicate ? std::string("A feather: you already have ") + (c ? c->name : r.id) + "." : TextFormat("Hatched: %s (%s)!", c ? c->name.c_str() : r.id.c_str(), fl::CostumeTierName(r.tier)); gWr.msgT = 5;
                            PlayCue(r.tier >= fl::CT_SUPER && !r.duplicate ? "arc.win" : "ui.click"); }
            }
        }
        if (gWr.hatchT > 0) { gWr.hatchT -= dt; for (int k = 0; k < 14; k++) { float a = k * 0.45f, rr = 40 + 120 * (1 - gWr.hatchT / 2.5f); DrawCircleV({nc.x + cosf(a) * rr, nc.y + sinf(a) * rr * 0.6f}, 4, Fade(fl::CostumeTierColor(gWr.hatchTier), gWr.hatchT / 2.5f)); } }
        DrawTextCentered(W.crates ? TextFormat("%d crate%s to open", W.crates, W.crates == 1 ? "" : "s") : "No crates: buy one above", nc.x, nc.y + 100, 15, ink);
        if (const fl::CostumeDef* c = fl::FindCostume(gWr.pick)) act(c->id, 0, false);
    } else {   // liveries: a colour and a hat for every bird in the colony
        TxtBold("Colours (the whole colony)", 32, y, 15, gold); y += 22;
        for (const auto& c : D.colours) { std::string right = W.Owns(c.id) ? "owned" : TextFormat("%d tokens", c.price);
            Rectangle rr{32, y, 448, 26}; if (WardrobeRow(rr, c.name, fl::CT_SHOP, right, gWr.pick == c.id, W.liveryColour == c.id)) gWr.pick = c.id;
            DrawRectangle((int)rr.x + 200, (int)rr.y + 6, 40, 14, c.c); y += 29; }
        y += 8; TxtBold("Hats (every bird wears one)", 32, y, 15, gold); y += 22;
        for (const auto& h : D.hats) { std::string right = W.Owns(h.id) ? "owned" : TextFormat("%d tokens", h.price);
            if (WardrobeRow({32, y, 448, 26}, h.name, fl::CT_SHOP, right, gWr.pick == h.id, W.liveryHat == h.id)) gWr.pick = h.id; y += 29; }
        if (fl::FindLiveryColour(gWr.pick)) act(gWr.pick, fl::FindLiveryColour(gWr.pick)->price, true);
        else if (fl::FindLiveryHat(gWr.pick)) act(gWr.pick, fl::FindLiveryHat(gWr.pick)->price, true);
    }
    if (gWr.msgT > 0) { gWr.msgT -= dt; DrawTextCenteredBold(gWr.msg, 880, SCREEN_H - 110.0f, 20, Fade(gold, std::min(1.0f, gWr.msgT))); }
    if (show) { float tw = (float)MeasureText(show->name.c_str(), 24) + 60; DrawRectangleRounded({880 - tw / 2, 28, tw, 62}, 0.3f, 6, Fade(Color{8, 18, 28, 255}, 0.75f)); DrawTextCenteredBold(show->name, 880, 36, 24, fl::CostumeTierColor(show->tier)); DrawTextCentered(fl::CostumeTierName(show->tier), 880, 66, 14, dim); }
    DrawTextCentered("Tokens: 10 a match, 1 per 50 score, 5 for each first, 20 for a win", 880, SCREEN_H - 40.0f, 13, dim);
    return SmallBtn({SCREEN_W - 156.0f, 20, 140, 30}, "Back");
}
// --shots: the wardrobe (a shop costume on the perch), and the gallery of every costume (page by page)
void DebugFlightWardrobe(int tab, const char* pick) {
    fl::gWardrobeNoSave = true; fl::LoadWardrobe();
    fl::FlightWardrobe& W = fl::Wardrobe(); W.tokens = 340; W.crates = 3; W.feathers = 2; W.owned = {"admiral", "rubber_duck", "phoenix", "lagoon", "caps"}; W.costume = "admiral"; W.liveryColour = "lagoon"; W.liveryHat = "caps";
    gWr = WardrobeUi{}; gWr.tab = tab; gWr.pick = pick ? pick : "";
}
void DrawFlightCostumeGallery(int page) {
    EnsurePreview(0);
    const auto& C = fl::Costumes().costumes;
    const int PER = 14;
    Camera3D cam{}; cam.up = {0, 1, 0}; cam.fovy = 40; cam.projection = CAMERA_PERSPECTIVE; cam.target = {0, 0.3f, 0}; cam.position = {0, 2.2f, 9.5f};
    PreviewBegin(cam);
    struct Lab { Vector3 p; const fl::CostumeDef* c; };
    std::vector<Lab> labs;
    for (int i = 0; i < PER; i++) {
        int idx = page * PER + i; if (idx >= (int)C.size()) break;
        int row = i / 7, col = i % 7;
        Vector3 at{-5.25f + col * 1.75f, 1.3f - row * 2.6f, -row * 0.5f};
        const fl::CostumeDef& c = C[idx];
        fl::CostumeLook lk = c.look; if (lk.scale > 1.2f) lk.scale = 1.6f;   // (the Roc, shrunk to fit the page)
        PreviewBird(fl::Founders()[0], at, -PI * 0.5f + 0.5f, &lk, false, nullptr, lk.scale > 1.2f ? 1.6f : 2.3f);
        labs.push_back({Vector3Add(at, {0, -0.75f, 0}), &c});
    }
    rt::RenderEnd();
    for (const auto& l : labs) { Vector2 p = GetWorldToScreenEx(l.p, cam, SCREEN_W, SCREEN_H); float tw = (float)MeasureText(l.c->name.c_str(), 13) + 16; DrawRectangleRounded({p.x - tw / 2, p.y - 3, tw, 34}, 0.3f, 4, Fade(Color{8, 18, 28, 255}, 0.7f)); DrawTextCenteredBold(l.c->name, p.x, p.y, 13, fl::CostumeTierColor(l.c->tier)); DrawTextCentered(l.c->tier == fl::CT_SHOP ? TextFormat("%d tokens", l.c->price) : fl::CostumeTierName(l.c->tier), p.x, p.y + 15, 11, WHITE); }
    DrawTextCenteredBold(TextFormat("The Flight: Founder costumes, page %d of %d", page + 1, ((int)C.size() + PER - 1) / PER), SCREEN_W / 2.0f, 14, 22, WHITE);
}
namespace {
// ---------------------------------------------------------------- sound (stage 8; doc p32)
// The music and the beds read the world every frame; the effects come from what changed since the last one (the
// simulation never calls audio, so the tests and the network are untouched).
struct FlSnd {
    bool init = false; fl::FState st = fl::FState::Perched; int carry = -1, eggs = 0, chicks = 0, pearls = 0, retreating = 0, flocks = 0, krakenMood = 0, weather = 0;
    size_t fxSeen = 0, logSeen = 0; float ash = 0, rock = 0, warT = 0, struggleT = 0, flapPrev = 0; bool chart = false;
};
FlSnd gSnd;
void ResetFlightSound() { gSnd.init = false; }
float PanOf(Vector3 p) {
    Vector3 fwd = Vector3Normalize(Vector3Subtract(S.cam.target, S.cam.position)), right = Vector3Normalize(Vector3CrossProduct(fwd, S.cam.up));
    Vector3 d = Vector3Subtract(p, S.cam.position); float l = Vector3Length(d);
    return l < 0.01f ? 0 : std::clamp(Vector3DotProduct(d, right) / l, -1.0f, 1.0f);
}
float Near(Vector3 p, float range) { return std::clamp(1 - Vector3Distance(p, S.cam.position) / range, 0.0f, 1.0f); }
void FlightAudioFrame(const fl::World& w, float dt) {
    fl::World& W = const_cast<fl::World&>(w);
    const fl::Founder& f = w.me;
    FlAudio a; a.on = true;
    float ground = std::max(0.0f, w.HeightAt(f.pos.x, f.pos.z));
    a.altitude = std::max(0.0f, f.pos.y - ground); a.speed = Vector3Length(f.vel); a.wind = Vector2Length(W.WindAt());
    a.dayPhase = w.DayPhase(); a.colony = (float)w.Alive();
    a.storm = w.weather.kind == 1; a.fog = w.weather.kind == 2;
    a.underwater = f.st == fl::FState::Under || f.st == fl::FState::Struggle;
    a.over = w.over ? (w.winner == w.cur ? 1 : 2) : 0;
    float pitch = std::clamp(1.4f - w.Def().span * 0.25f, 0.6f, 1.5f);   // (a big bird's voice is lower)
    // the chorus: every colony's birds near you; your chicks' hunger near you
    int nearBirds = 0, hungry = 0;
    for (int s = 0; s <= (int)w.sides.size(); s++) for (const auto& b : W.ColOf(s).birds) {
        if (!b.alive || b.stage == fl::BStage::Egg) continue;
        float d = Vector3Distance(b.pos, f.pos);
        if (d < 260) nearBirds++;
        if (s == w.cur && b.stage == fl::BStage::Chick && b.hunger < 0.3f && d < 70) hungry++;
    }
    a.chorus = std::clamp(nearBirds / 40.0f, 0.0f, 1.0f); a.hungry = std::clamp(hungry / 4.0f, 0.0f, 1.0f);
    // the shore: the nearest island's edge, and its kind; a town; the cove
    if (w.wholeMap) {
        float best = 1e9f; int bi = -1;
        for (int i = 0; i < (int)w.isles.size(); i++) { float d = fabsf(Vector2Distance({f.pos.x, f.pos.z}, {w.isles[i].c.x, w.isles[i].c.z}) - w.isles[i].radius); if (d < best) { best = d; bi = i; } }
        if (bi >= 0) { a.surf = std::clamp(1 - best / 90.0f, 0.0f, 1.0f); fl::IsleType ty = w.isles[bi].type; a.surfType = ty == fl::IsleType::Stack || ty == fl::IsleType::Skull ? 1 : ty == fl::IsleType::Atoll || ty == fl::IsleType::ReefGarden ? 2 : 0; }
        for (const auto& t : w.towns) a.town = std::max(a.town, std::clamp(1 - Vector3Distance(t.dock, f.pos) / 220.0f, 0.0f, 1.0f));
        if (w.kraken.isle >= 0 && !w.kraken.dead) {
            float d = Vector3Distance(w.isles[w.kraken.isle].c, f.pos);
            if (w.kraken.mood == 0) a.cove = std::clamp(1 - d / 400.0f, 0.0f, 1.0f); else if (d < 450) a.kraken = w.kraken.mood;
        }
    } else { a.surf = std::clamp(1 - fabsf(Vector2Distance({f.pos.x, f.pos.z}, {w.island.c.x, w.island.c.z}) - w.island.radius) / 90.0f, 0.0f, 1.0f); }
    // ---- what changed: the effects
    if (!gSnd.init) { gSnd = FlSnd{}; gSnd.init = true; gSnd.st = f.st; gSnd.carry = f.carrySp; gSnd.fxSeen = w.warFxBase + w.warFx.size(); gSnd.logSeen = w.know.log.size(); gSnd.pearls = w.col.pearls; gSnd.krakenMood = w.kraken.mood; gSnd.weather = w.weather.kind; gSnd.ash = w.volcano.ashT; gSnd.rock = w.ape.rockT; }
    int eggs = 0, chicks = 0; for (const auto& b : w.col.birds) if (b.alive) { eggs += b.stage == fl::BStage::Egg; chicks += b.stage == fl::BStage::Chick; }
    if (f.st != gSnd.st) {
        if (f.st == fl::FState::Strike) FlightCue(FLC_DIVE, 0.8f, 0, pitch);
        else if ((f.st == fl::FState::Struggle || f.st == fl::FState::Under) && gSnd.st == fl::FState::Strike) FlightCue(FLC_SPLASH, 0.9f, 0);
        else if (f.st == fl::FState::Perched) FlightCue(FLC_LAND, 0.7f, 0);
        else if (f.st == fl::FState::Dead) FlightCue(FLC_DEATH, 0.9f, 0);
        else if (f.st == fl::FState::Fly && gSnd.st == fl::FState::Perched) FlightCue(FLC_CALL, 0.6f, 0, pitch);   // (the signature call on taking off)
        gSnd.st = f.st;
    }
    if (f.st == fl::FState::Struggle) { gSnd.struggleT -= dt; if (gSnd.struggleT <= 0) { gSnd.struggleT = 0.6f; FlightCue(FLC_STRUGGLE, 0.7f, 0); } }
    if (f.flapping && (f.st == fl::FState::Fly) && fmodf(S.flapPh, 2 * PI) < fmodf(gSnd.flapPrev, 2 * PI)) FlightCue(FLC_FLAP, 0.25f, 0, pitch);
    gSnd.flapPrev = S.flapPh;
    if (gSnd.carry >= 0 && f.carrySp < 0 && f.st == fl::FState::Perched) FlightCue(FLC_SLAP, 0.7f, 0);
    gSnd.carry = f.carrySp;
    if (eggs > gSnd.eggs && gSnd.init) FlightCue(FLC_EGG, 0.5f, 0);
    if (chicks > gSnd.chicks) FlightCue(FLC_HATCH, 0.6f, 0);
    gSnd.eggs = eggs; gSnd.chicks = chicks;
    if (w.col.pearls > gSnd.pearls) FlightCue(FLC_PEARL, 0.6f, 0);
    gSnd.pearls = w.col.pearls;
    // the war: blows, falls, nets and bombs near you; your flocks forming and routing
    size_t end = w.warFxBase + w.warFx.size();
    if (gSnd.fxSeen < w.warFxBase || gSnd.fxSeen > end) gSnd.fxSeen = w.warFxBase;
    for (; gSnd.fxSeen < end; gSnd.fxSeen++) {
        const auto& e = w.warFx[gSnd.fxSeen - w.warFxBase];
        float v = Near(e.p, e.kind == 3 ? 700.0f : 260.0f);
        if (v <= 0) continue;
        if (e.kind != 2) gSnd.warT = 5;
        int kind = e.kind == 0 ? FLC_HIT : e.kind == 1 ? FLC_FALL : e.kind == 2 ? FLC_NET : FLC_BOMB;
        FlightCue(kind, v, PanOf(e.p));
        if (e.kind == 3 && (int)e.yaw == 1) FlightCue(FLC_BURN, v, PanOf(e.p));
        if (e.kind == 0 && e.role == fl::Role::Striker && Hash((float)gSnd.fxSeen, 1.7f) < 0.3f) FlightCue(FLC_SHRIEK, v * 0.7f, PanOf(e.p));
        if (e.kind == 0 && e.role == fl::Role::Screamer && Hash((float)gSnd.fxSeen, 1.7f) < 0.3f) FlightCue(FLC_SCREAM, v * 0.8f, PanOf(e.p));
    }
    gSnd.warT = std::max(0.0f, gSnd.warT - dt);
    a.war = std::clamp(gSnd.warT / 3.0f, 0.0f, 1.0f);
    int retreating = 0; for (const auto& fk : w.col.flocks) retreating += fk.retreating;
    if (retreating > gSnd.retreating) FlightCue(FLC_ROUT, 0.8f, 0);
    if ((int)w.col.flocks.size() > gSnd.flocks) FlightCue(FLC_WINGBEATS, 0.8f, 0);
    gSnd.retreating = retreating; gSnd.flocks = (int)w.col.flocks.size();
    // the dangers
    if (w.kraken.isle >= 0 && w.kraken.mood == 2 && gSnd.krakenMood < 2) { float v = Near(w.isles[w.kraken.isle].c, 1200); if (v > 0) FlightCue(FLC_ROAR, 0.4f + 0.6f * v, PanOf(w.isles[w.kraken.isle].c)); }
    gSnd.krakenMood = w.kraken.mood;
    if (w.volcano.isle >= 0 && w.volcano.ashT > 0 && gSnd.ash <= 0) FlightCue(FLC_ERUPT, 0.3f + 0.7f * Near(w.isles[w.volcano.isle].c, 1500), PanOf(w.isles[w.volcano.isle].c));
    gSnd.ash = w.volcano.ashT;
    if (w.ape.isle >= 0 && w.ape.rockT > 0 && gSnd.rock <= 0) { float v = Near(w.ape.pos, 400); if (v > 0) FlightCue(FLC_ROCK, v, PanOf(w.ape.pos)); }
    gSnd.rock = w.ape.rockT;
    if (w.weather.kind == 1 && gSnd.weather != 1) FlightCue(FLC_THUNDER, 0.8f, 0);
    gSnd.weather = w.weather.kind;
    // the reports and the chart
    if (w.know.log.size() > gSnd.logSeen && S.t > 1) FlightCue(FLC_REPORT, 0.5f, 0);   // (a scout's report)
    gSnd.logSeen = w.know.log.size();
    if (S.chart != gSnd.chart) FlightCue(FLC_MAP, 0.6f, 0);
    gSnd.chart = S.chart;
    AudioFlight(a);
}
}  // namespace

void StartFlight(Game& g, const char* founder, int isleType, int arrangement, int players, int seasons) {
    fl::MapOpts o; o.home = (fl::IsleType)std::clamp(isleType, 0, 3); o.arr = (fl::Arrangement)std::clamp(arrangement, 0, 3); o.players = std::clamp(players, 2, 6); o.seasons = seasons;
    Start(g, founder ? founder : "taloned", (uint32_t)GetRandomValue(1, 1 << 30), false, o);
}
const char* FlightIsleTypeName(int t) { return fl::IsleTypeName((fl::IsleType)std::clamp(t, 0, 3)); }
const char* FlightArrangementName(int a) { return fl::ArrangementName((fl::Arrangement)std::clamp(a, 0, 3)); }
void LeaveFlight(Game& g) {
    if (S.net) { if (S.net->role == arcade::R_HOST) S.net->BackToLobby(); else S.net->Leave(); }
    S.net = nullptr; S.live = nullptr;
    S.active = false; FreeModels(); g.scene = Scene::Arcade;
}
// Network play (stage 5): the arcade's session launched the Flight. The host draws its real world; a guest draws its
// mirror of the host's snapshots, flying its own Founder ahead of them. Either way the Founder is flown by input and
// the colony run by orders.
void StartFlightNet(Game& g, arcade::Session* net, const char* founderKey, const char* name) {
    std::string why;
    if (!rt::DataOk(&why)) { g.scene = Scene::Arcade; return; }
    S.net = net; S.live = nullptr;
    S.W = fl::World{};
    S.seenVersion = -1; S.sinceSnap = 0; S.helloSent = false; S.flocksSeen = 0; gSnd.init = false;
    S.netFounder = founderKey ? founderKey : "taloned"; S.netName = name ? name : "Founder";
    S.active = true; S.shot = false;
    S.chart = false; S.panel = false; S.chartZoom = 1; S.chartAt = {0, 0};
    S.aimYaw = PI * 0.5f; S.aimPitch = 0.1f; S.camYaw = S.aimYaw; S.camPitch = -0.1f;
    S.t = 0; S.help = true;
    g.scene = Scene::Flight;
}
// (the game menu is open: the session and the world go on underneath)
void FlightMenuTick(float dt) {
    if (!S.active || !S.net) return;
    fl::FounderInput in; in.yaw = S.aimYaw; in.pitch = S.aimPitch;
    Writer w; fl::WriteInput(in, w); S.net->Act(w);
    S.net->Update(GetTime(), dt);
}
// the end of a match: the standings, the winner, the way out
static void DrawResults(Game& g, fl::World& w) {
    Color ink{250, 248, 236, 255}, dim{200, 214, 214, 255};
    static int paid = -1; static const fl::World* paidFor = nullptr;
    if (paidFor != &w || paid < 0) {   // (the match's tokens, once: 10, per 50 score, firsts, a win)
        uint32_t firsts = 0;
        if (w.ColOf(w.cur).krakenKill) firsts |= 1;
        for (int i = 0; i < (int)w.isles.size(); i++) if (fl::IsDangerous(w.isles[i].type) && w.HolderOf(i) == w.cur) firsts |= 2;
        for (int k = 0; k < (int)fl::Tree::COUNT; k++) if (w.ColOf(w.cur).tier[k] >= 4) firsts |= 4;
        paid = S.shot ? fl::MatchTokens(w.Score(w.cur).total, w.winner == w.cur, firsts) : fl::AwardMatch(w.Score(w.cur).total, w.winner == w.cur, firsts);
        paidFor = &w;
    }
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.55f));
    Rectangle r{SCREEN_W / 2.0f - 450, 110, 900, 150.0f + 30 * (w.sides.size() + 1)};
    DrawRectangleRounded(r, 0.06f, 6, Fade(Color{8, 18, 28, 255}, 0.92f));
    DrawRectangleRoundedLinesEx(r, 0.06f, 6, 2, Color{214, 180, 110, 255});
    DrawTextCenteredBold(w.winner == w.cur ? "Your colony wins!" : w.SideName(w.winner) + " wins", SCREEN_W / 2.0f, r.y + 14, 30, Color{255, 226, 150, 255});
    DrawTextCentered(w.overReason, SCREEN_W / 2.0f, r.y + 52, 16, dim);
    float y = r.y + 82;
    static const char* COLS[] = {"birds", "nests", "island", "cache", "kills", "Founder", "research", "pearls", "faith", "thefts", "total"};
    for (int c = 0; c < 11; c++) Txt(COLS[c], r.x + 220 + c * 60.0f, y, 14, dim);
    y += 22;
    std::vector<int> order; for (int s = 0; s <= (int)w.sides.size(); s++) order.push_back(s);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return w.Score(a).total > w.Score(b).total; });
    for (int s : order) {
        fl::ScoreCard c = w.Score(s);
        Color col = s == w.cur ? Color{255, 230, 140, 255} : Mix(w.SideColor(s), WHITE, 0.35f);
        TxtBold(s == w.cur ? "You" : w.SideName(s), r.x + 24, y, 17, col);
        int v[11] = {c.birds, c.nests, c.isles, c.cache, c.kills, c.founder, c.research, c.pearls, c.faith, c.thefts, c.total};
        for (int k = 0; k < 11; k++) Txt(TextFormat("%d", v[k]), r.x + 220 + k * 60.0f, y, 17, k == 10 ? col : ink);
        y += 30;
    }
    DrawTextCenteredBold(TextFormat("+%d tokens for the Roost wardrobe (%d in all)", paid, fl::Wardrobe().tokens + (S.shot ? paid : 0)), SCREEN_W / 2.0f, r.y + r.height - 78, 16, Color{255, 220, 150, 255});
    bool host = !S.net || S.net->role == arcade::R_HOST;
    if (Button({SCREEN_W / 2.0f - 120, r.y + r.height - 50, 240, 38}, S.net ? (host ? "Back to the lobby" : "Leave the table") : "Back to the arcade", true, 16)) { paid = -1; LeaveFlight(g); }
}
const char* FlightFounderName(int i) { const auto& v = fl::Founders(); return i >= 0 && i < (int)v.size() ? v[i].name.c_str() : "?"; }
const char* FlightFounderKey(int i) { const auto& v = fl::Founders(); return i >= 0 && i < (int)v.size() ? v[i].key.c_str() : "taloned"; }
const char* FlightFounderLine(int i) { const auto& v = fl::Founders(); return i >= 0 && i < (int)v.size() ? v[i].playstyle.c_str() : ""; }
int FlightFounderCount() { return (int)fl::Founders().size(); }

void SceneFlight(Game& g) {
    if (!S.active) { StartFlight(g, "taloned"); if (!S.active) return; }
    float dt = S.shot ? 1 / 60.0f : std::min(GetFrameTime(), 1 / 30.0f);
    if (S.net) {
        // ---- network play: the session first (the host's world steps inside it), then the snapshot into the mirror
        arcade::Session& N = *S.net;
        N.Update(GetTime(), dt);
        if (N.stage != arcade::S_PLAYING) { S.net = nullptr; S.live = nullptr; S.active = false; FreeModels(); g.scene = Scene::Arcade; return; }
        if (N.role == arcade::R_HOST) S.live = fl::FlightHostWorld(N.HostGame());
        else {
            S.sinceSnap += dt;
            if (N.stateVersion != S.seenVersion && !N.Snapshot().empty()) {
                S.seenVersion = N.stateVersion;
                Reader r(N.Snapshot());
                if (fl::ReadWorld(r, S.W, S.W.mirror)) S.sinceSnap = 0;
            } else if (S.W.mirror && S.sinceSnap < 0.3f) {
                // between snapshots everything else flies and swims on along its last heading
                for (auto& a : S.W.eco.agents) if (a.alive && a.diver < 0) a.pos = Vector3Add(a.pos, Vector3Scale(a.vel, dt));
                for (int s = 0; s <= (int)S.W.sides.size(); s++) {
                    for (auto& b : S.W.ColOf(s).birds) if (b.alive) b.pos = Vector3Add(b.pos, Vector3Scale(b.vel, dt));
                    if (s != S.W.cur) { fl::Founder& f = S.W.FounderOf(s); if (f.st == fl::FState::Fly) f.pos = Vector3Add(f.pos, Vector3Scale(f.vel, dt)); }
                }
            }
        }
        fl::World& w = WD();
        if (!w.wholeMap || (!S.live && !w.mirror)) {
            ClearBackground(Color{120, 170, 200, 255});
            DrawTextCenteredBold("Taking wing...", SCREEN_W / 2.0f, SCREEN_H / 2.0f - 12, 24, WHITE);
            return;
        }
        if (!S.helloSent) {   // (your name and your founder, once: the founder counts in the match's first 30 s)
            Writer o; fl::OrderHello(o, S.netName, S.netFounder, MyLook()); N.Act(o); S.helloSent = true;
            S.aimYaw = w.me.yaw; S.camYaw = S.aimYaw;
        }
        if (w.col.flocks.size() > S.flocksSeen && !w.col.flocks.empty()) S.selFlock = w.col.flocks.back().id;   // (a flock just formed is the one picked)
        S.flocksSeen = w.col.flocks.size();
        fl::FounderInput in = Gather(dt);
        if (w.over) in = fl::FounderInput{in.yaw, in.pitch};
        Writer iw; fl::WriteInput(in, iw); N.Act(iw);
        if (!S.live) w.PredictFounder(dt, in);   // (a guest flies its own bird ahead of the host)
        if (!S.live) { w.fogNow = true; w.StepFog(dt); }
        S.t += dt;
        Render(dt);
        if (!S.shot || getenv("DEPTH_FLAUDIO")) FlightAudioFrame(w, dt);
        DrawHud(w);
        if (S.panel) { if (S.page == 0) DrawColonyPanel(w); else if (S.page == 1) DrawFlockPanel(w); else if (S.page == 2) DrawSocietyPanel(w); else DrawLongPanel(w); }
        if (S.chart) DrawChart(w);
        // a person lost: their colony runs on its last orders, then the AI stands in
        for (int k = 0; k < arcade::MAX_PLAYERS; k++) if (N.seats[k].used && N.seats[k].lost && !N.seats[k].ai && fmodf(S.t, 1.4f) < 1.0f)
            DrawTextCenteredBold(TextFormat("%s is lost: their colony runs on its last orders (the AI in %d s)", N.seats[k].name.c_str(), (int)N.pauseLeft), SCREEN_W / 2.0f, SCREEN_H - 200, 16, Color{255, 200, 140, 255});
        if (w.over) DrawResults(g, w);
        return;
    }
    if (S.shot && WD().me.st == fl::FState::Strike) WD().me.strikeT = std::min(WD().me.strikeT, 0.2f);   // (the shot holds the strike)
    fl::FounderInput in = Gather(dt);
    if (S.W.col.flocks.size() > S.flocksSeen && !S.W.col.flocks.empty()) S.selFlock = S.W.col.flocks.back().id;
    S.flocksSeen = S.W.col.flocks.size();
    WD().Step(dt, in);
    S.t += dt * WD().timeScale;
    Render(dt * WD().timeScale);
    if (!S.shot || getenv("DEPTH_FLAUDIO")) FlightAudioFrame(WD(), dt);
    DrawHud(WD());
    if (S.panel) { if (S.page == 0) DrawColonyPanel(WD()); else if (S.page == 1) DrawFlockPanel(WD()); else if (S.page == 2) DrawSocietyPanel(WD()); else DrawLongPanel(WD()); }
    if (S.chart) DrawChart(WD());
    if (WD().over) DrawResults(g, WD());
}

// --shots: 0 cruising over the lagoon at dawn, 1 the strike, 2 at the nest with a fish, 3 noon from high over the island
void DebugFlightShot(Game& g, int which) {
    fl::MapOpts o; o.seasons = which == 21 || which == 22 ? 4 : 0; o.home = which == 6 ? fl::IsleType::Stack : which == 7 ? fl::IsleType::Town : which == 8 ? fl::IsleType::Atoll : fl::IsleType::Tropical;
    Start(g, which == 3 ? "albatross" : "taloned", 11, true, o);
    fl::World& w = WD();
    fl::Founder& f = w.me;
    f.st = fl::FState::Fly; f.airspeed = 11; f.yaw = PI * 0.5f; f.pitch = 0;
    S.help = which == 0;
    if (which == 0) { w.time = fl::World::DAY * 0.06f; f.pos = {-30, 14, 20}; f.flapping = true; }
    if (which == 1) {
        w.time = fl::World::DAY * 0.3f;
        int fi = w.FishNear({0, 0, 70}, 30, 3);
        Vector3 at = fi >= 0 ? w.eco.agents[fi].pos : Vector3{0, -0.6f, 70};
        f.pos = {at.x - 3, 3, at.z - 2}; f.vel = {6, -14, 4}; f.airspeed = 16; f.pitch = -1.0f;
        S.aimPitch = -0.9f;
        w.Step(1 / 60.0f, fl::FounderInput{f.yaw, -1.0f});
        if (f.st != fl::FState::Strike) { f.st = fl::FState::Strike; f.strikeT = 0.1f; f.strikeAt = f.strikeAim = {at.x, 0, at.z}; w.timeScale = 0.25f; }
    }
    if (which == 2) { w.time = fl::World::DAY * 0.42f; f.st = fl::FState::Perched; f.pos = w.island.nest; f.carrySp = w.eco.map->SpeciesIndex("Mullet"); f.carrySize = 2; w.Cache0().push_back({w.eco.map->SpeciesIndex("Sardine"), 1, 0}); S.aimPitch = -0.25f; S.aimYaw = PI * 0.9f; }
    if (which == 4 || which == 5) {   // a colony grown by the bot Founder for six days, seen at the nest and through its panel
        w.founderBot = true; f.st = fl::FState::Fly; f.pos = Vector3Add(w.island.nest, {0, 2, 0}); f.hunger = 1;
        w.col.restBelow = 0.45f;
        for (float tt = 0; tt < fl::World::DAY * 6.3f; tt += 0.1f) { w.col.nestsWanted = std::max(2, w.Count(fl::BStage::Mate) + 1); w.Step(0.1f, fl::FounderInput{}); }
        w.founderBot = false;
        Vector3 home = w.island.nest;   // (hovering off the home nest, looking at it and the palms round it)
        f.st = fl::FState::Fly; f.pos = Vector3Add(home, {-7, 2.5f, -5}); f.yaw = atan2f(home.z - f.pos.z, home.x - f.pos.x); f.airspeed = 6; f.pitch = -0.15f; f.carrySp = -1;
        S.aimYaw = f.yaw; S.aimPitch = -0.3f; S.panel = which == 5; S.help = false;
    }
    if (which == 3) { w.time = fl::World::DAY * 0.27f; f.pos = {40, 70, 120}; f.yaw = -PI * 0.6f; S.aimPitch = -0.35f; }
    if (which >= 6 && which <= 8) {   // the other starting islands, from the air at noon
        w.time = fl::World::DAY * 0.45f; S.help = false;
        Vector3 c = w.island.c; float r = w.island.radius;
        f.pos = {c.x - r * 1.5f, which == 6 ? 75.0f : 45.0f, c.z + r * 1.6f}; f.yaw = atan2f(c.z - f.pos.z, c.x - f.pos.x); f.airspeed = 10;
        S.aimPitch = which == 6 ? -0.2f : -0.35f;
    }
    if (which >= 10 && which <= 12) {   // war: two flocks meeting over the water; your flocks' page; the chart with flocks
        w.time = fl::World::DAY * 0.3f; S.help = false;
        for (int k = 0; k < 20; k++) w.Cache0().push_back({w.eco.map->SpeciesIndex("Mullet"), 2, 0});
        Vector3 other{}; for (const auto& is : w.isles) if (is.start == 1) other = is.c;
        Vector3 mid = which == 11 ? w.col.caches[0].pos : Vector3Lerp(w.island.c, other, 0.3f);
        auto add = [&](int side, fl::Role r, Vector3 p) { fl::Colony& C = w.ColOf(side); fl::Bird b; b.id = C.nextId++; b.stage = fl::BStage::Adult; b.role = r; b.hp = fl::RoleOf(r).hp; b.fight = 25; b.hunger = 1; b.pos = p; C.birds.push_back(b); return b.id; };
        static const fl::Role mix[7] = {fl::Role::Striker, fl::Role::Striker, fl::Role::Skirmisher, fl::Role::Skirmisher, fl::Role::Skirmisher, fl::Role::Tank, fl::Role::Screamer};
        std::vector<int> a, b;
        for (int k = 0; k < 7; k++) { a.push_back(add(0, mix[k], Vector3Add(mid, {-30.0f + k * 2, 40, -6}))); b.push_back(add(1, mix[k], Vector3Add(mid, {30.0f + k * 2, 25, 6}))); }
        int fa = w.MakeFlock(0, a, fl::Formation::Hammer, fl::Alt::Mid, fl::Stance::Raid), fb = w.MakeFlock(1, b, fl::Formation::Chevron, fl::Alt::Low, fl::Stance::Raid);
        if (which == 11) { std::vector<int> c; for (int k = 0; k < 4; k++) c.push_back(add(0, k < 2 ? fl::Role::Skirmisher : fl::Role::Tank, Vector3Add(mid, {(float)k, 3, 2}))); w.MakeFlock(0, c, fl::Formation::Wall, fl::Alt::Mid, fl::Stance::Hold); add(0, fl::Role::Striker, mid); add(0, fl::Role::Watcher, mid); fl::Structure h; h.kind = 0; h.pos = w.col.caches[0].pos; h.twigs = 6; w.col.builds.push_back(h); S.panel = true; S.page = 1; S.selFlock = fa; }
        else {
            w.OrderFlock(0, fa, fl::Target::Flock, 1, -1, -1, fb, {}); w.OrderFlock(1, fb, fl::Target::Flock, 0, -1, -1, fa, {});
            for (float tt = 0; tt < 9; tt += 0.05f) w.Step(0.05f, fl::FounderInput{});
            fl::Flock* F = w.FindFlock(0, fa);
            Vector3 at = F ? F->pos : mid;
            f.st = fl::FState::Fly; f.pos = Vector3Add(at, {-14, 16, -10}); f.yaw = atan2f(at.z - f.pos.z, at.x - f.pos.x); f.airspeed = 9;
            S.aimYaw = f.yaw; S.aimPitch = -0.75f;
            if (which == 12) { for (auto& c : w.know.seen) c = w.time; S.chart = true; S.chartMode = 2; S.selFlock = fa; }
        }
    }    if (which == 9) {   // the chart, after two scouts have been out and the Founder has flown a circuit
        w.time = fl::World::DAY * 0.4f; S.help = false;
        for (int k = 0; k < 2; k++) { fl::Bird b; b.id = w.col.nextId++; b.stage = fl::BStage::Adult; b.role = fl::Role::Scout; b.pos = w.col.caches[0].pos; w.col.birds.push_back(b); }
        int t1 = -1, t2 = -1; for (int i = 0; i < (int)w.isles.size(); i++) { if (w.isles[i].start == 1) t1 = i; if (w.isles[i].type == fl::IsleType::KrakenCove) t2 = i; }
        w.SendScout(t1, -1, {}, fl::Alt::High); w.SendScout(t2, -1, {}, fl::Alt::Mid);
        w.founderBot = true;
        for (float tt = 0; tt < fl::World::DAY * 1.2f; tt += 0.1f) w.Step(0.1f, fl::FounderInput{});
        w.founderBot = false;
        S.chart = true;
    }
    if (which == 13) {   // the research, faith and trade page of a colony some days in
        w.time = fl::World::DAY * 4.4f; S.help = false;
        fl::Structure r; r.kind = fl::ST_ROOST; r.built = true; r.pos = w.GroundAt(w.col.caches[0].pos.x - 5, w.col.caches[0].pos.z + 3); w.col.builds.push_back(r);
        fl::Structure sh; sh.kind = fl::ST_SHRINE; sh.built = true; sh.pos = w.GroundAt(w.col.caches[0].pos.x + 4, w.col.caches[0].pos.z - 5); w.col.builds.push_back(sh);
        w.col.tier[(int)fl::Tree::Nesting] = 2; w.col.tier[(int)fl::Tree::Fishing] = 1; w.col.tier[(int)fl::Tree::War] = 2; w.col.tier[(int)fl::Tree::Faith] = 1; w.col.tier[(int)fl::Tree::Trade] = 2;
        w.col.resTree = (int)fl::Tree::Caches; w.col.resDays = 0.5f; w.col.resLeft = 0.2f;
        w.col.pearls = 7; w.col.shells = 26; w.col.twigs = 14; w.col.guano = 31; w.col.fervour = 58;
        int give[fl::G_COUNT] = {0, 0, 20, 0}, get[fl::G_COUNT] = {0, 0, 0, 3};
        w.ColOf(1).tier[(int)fl::Tree::Trade] = 2; w.ColOf(1).shells = 40;
        w.MakeOffer(1, 0, give, get, 2);
        S.panel = true; S.page = 2;
    }
    if (which == 14) {   // the end of a match: the standings
        S.help = false;
        w.multi = true; w.matchLen = 60; w.time = 61; w.name0 = "You";
        if (w.sides.size() >= 2) { w.sides[0].name = "Ana"; w.sides[1].name = "Bot 2"; }
        for (int k = 0; k < 8; k++) w.Cache0().push_back({w.eco.map->SpeciesIndex("Mullet"), 2, 0});
        w.col.tier[(int)fl::Tree::Nesting] = 2; w.col.pearls = 4;
        w.Step(0.1f, fl::FounderInput{f.yaw});
    }
    if (which == 15) {   // a guest's screen in a networked match: a host, this guest and an AI colony over the in-memory transport
        static arcade::Session host, guest;
        host.Leave(); guest.Leave();
        std::string err;
        arcade::Profile ph{"Host", 1}, pg{"Guest", 2};
        host.Host(ph, arcade::G_FLIGHT, &err, 47798, net::MakeMemoryTransport(), false);
        host.gameOpts = "0:0:30";
        guest.Join(pg, "mem:47798", &err, 0, net::MakeMemoryTransport());
        double t = 0;
        auto pump = [&](int frames) { for (int i = 0; i < frames; i++) { t += 1 / 30.0; host.Update(t, 1 / 30.0f); guest.Update(t, 1 / 30.0f); } };
        for (int i = 0; i < 300 && guest.stage != arcade::S_LOBBY; i++) pump(1);
        host.AddAI(); guest.SetReady(true);
        for (int i = 0; i < 300 && !host.seats[1].ready; i++) pump(1);
        std::string why; host.Launch(&why);
        for (int i = 0; i < 300 && (guest.stage != arcade::S_PLAYING || guest.Snapshot().empty()); i++) pump(1);
        if (fl::World* hw = fl::FlightHostWorld(host.HostGame())) {
            int me = std::max(0, guest.MyPlayer());
            // the guest over its island at mid-morning, the host's Founder flying nearby
            hw->time = fl::World::DAY * 0.32f;
            fl::Founder& G = hw->FounderOf(me); fl::Island& is = hw->isles[hw->HomeOf(me)];
            G.st = fl::FState::Fly; G.pos = {is.c.x - 30, 26, is.c.z + 40}; G.yaw = atan2f(is.c.z - G.pos.z, is.c.x - G.pos.x); G.airspeed = 11; G.pitch = -0.1f;
            fl::Founder& H = hw->FounderOf(0); H.st = fl::FState::Fly; H.pos = Vector3Add(G.pos, {cosf(G.yaw) * 9 + 3, 2, sinf(G.yaw) * 9 - 2}); H.yaw = G.yaw + 0.3f; H.airspeed = 11;
        }
        pump(15);
        StartFlightNet(g, &guest, "swift", "Guest");
        S.shot = true; S.help = false;
        return;
    }
    if (which >= 16 && which <= 18) {   // the dangerous islands: the kraken surfaced, the ape awake and throwing, the volcano erupting
        w.time = fl::World::DAY * 0.4f; S.help = false;
        fl::IsleType want = which == 16 ? fl::IsleType::KrakenCove : which == 17 ? fl::IsleType::Skull : fl::IsleType::Volcano;
        int I = -1; for (int i = 0; i < (int)w.isles.size(); i++) if (w.isles[i].type == want) I = i;
        if (I >= 0) {
            const fl::Island& is = w.isles[I];
            if (which == 16) { w.kraken.mood = 2; w.kraken.armT = 1.0f; w.kraken.arm = Vector3Add(is.c, {-30, 8, 10}); }
            if (which == 17) { w.ape.sleepT = 0; w.ape.rockT = 0.45f; w.ape.rockFrom = w.ape.pos; w.ape.rockTo = Vector3Add(w.ape.pos, {-60, -10, 50}); }
            if (which == 18) { w.volcano.ashT = 60; w.volcano.tremorT = 0; }
            float dist = which == 18 ? is.radius * 2.6f + 120 : which == 16 ? is.radius * 0.9f : 70;
            Vector3 c = which == 17 ? w.ape.pos : is.c;
            f.st = fl::FState::Fly; f.pos = {c.x - dist * 0.8f, which == 18 ? 70.0f : which == 16 ? 85.0f : c.y + 18, c.z + dist * 0.6f};
            f.yaw = atan2f(c.z - f.pos.z, c.x - f.pos.x); f.airspeed = 10;
            S.aimPitch = which == 18 ? 0.05f : which == 16 ? -0.6f : -0.25f;
            for (int k = 0; k < 40; k++) { S.t += 0.05f; }
        }
    }
    if (which == 19) {   // a storm over home: the Roost, the shrine and the Works raised; an enemy bomb going off by the cache
        w.time = fl::World::DAY * 0.36f; S.help = false;
        Vector3 c0 = w.col.caches[0].pos;
        int kinds[3] = {fl::ST_ROOST, fl::ST_SHRINE, fl::ST_WORKS}; Vector2 off[3] = {{-9, 5}, {6, -8}, {10, 8}};
        for (int k = 0; k < 3; k++) { fl::Structure s; s.kind = kinds[k]; s.built = true; s.pos = w.GroundAt(c0.x + off[k].x, c0.z + off[k].y); s.twigs = 30; w.col.builds.push_back(s); }
        w.weather.kind = 1; w.weather.t = 60;
        w.warFx.push_back({w.GroundAt(c0.x + 18, c0.z - 14), 3, 1, fl::Role::Bomber, 1.0f});
        f.st = fl::FState::Fly; f.pos = {c0.x - 34, c0.y + 20, c0.z + 30}; f.yaw = atan2f(c0.z - f.pos.z, c0.x - f.pos.x); f.airspeed = 9;
        S.aimPitch = -0.35f;
    }
    if (which == 21 || which == 22) {   // a long match: dawn of day 5 (a perk and a decree to pick); or the Visitor and a relic
        S.help = false;
        w.time = 4.02f * fl::World::DAY; w.StepPerks(0.1f); w.StepDecrees(0.1f);
        if (which == 22) { w.me.perkOffer[0] = -1; w.col.decree = 0; w.legendFree = fl::LG_FISHER_KING; w.time = 4.4f * fl::World::DAY; }
        Vector3 at = !w.relicSpots.empty() ? w.relicSpots[0].pos : w.island.c;
        if (which == 22) { w.legendPos = Vector3Add(at, {14, 6, 6}); }
        f.st = fl::FState::Fly; f.pos = Vector3Add(at, {-22, 14, 18}); f.yaw = atan2f(at.z - f.pos.z, at.x - f.pos.x); f.airspeed = 9;
        S.aimPitch = -0.35f;
    }
    if (which == 20) {   // the chart's outpost orders, with a rival holding the kraken's cove
        w.time = fl::World::DAY * 0.5f; S.help = false;
        for (auto& c : w.know.seen) c = w.time;
        for (size_t i = 0; i < w.isles.size(); i++) w.know.isle[i] = 2;
        int I = -1; for (int i = 0; i < (int)w.isles.size(); i++) if (w.isles[i].type == fl::IsleType::KrakenCove) I = i;
        if (I >= 0 && !w.sides.empty()) w.WithSide(1, [&] { w.FoundOutpost(I, w.isles[I].c); for (auto& n : w.col.nests) if (n.isle == I) n.built = true; });
        w.col.tier[(int)fl::Tree::Trade] = 4;
        S.chart = true; S.chartMode = 3;
    }
    S.aimYaw = which == 2 ? S.aimYaw : f.yaw; S.camYaw = S.aimYaw; S.camPitch = S.aimPitch * 0.8f - 0.12f;
}
