// The Flight's scene (stage 1): the Founder flown in third person over the tropical island, fishing the lagoon.
// The simulation is flight.cpp (headless); this file feeds it input and draws it with Red Tide's inked renderer in
// daylight (design doc, "Presentation": Depth's inked low-poly in daylight, a thin ink line, silhouettes do the work).
#include "game.h"
#include "flight.h"
#include "redtide_render.h"
#include "input.h"
#include "sound.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>

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
    bool panel = false;                       // the colony panel (Tab)
    int page = 0, selFlock = -1; bool raidChicks = false;   // (the panel's page: 0 the colony, 1 the flocks; the flock picked for chart orders)
    int plat = 0;                             // (the panel's fledging-plan row picked)
    bool seaReady = false;
};
FlightScene S;

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
void EnsureModels() {
    if (!IsWindowReady()) return;
    int key = (int)S.W.island.seed * 31 + S.W.me.def + (int)S.W.isles.size() * 7919;
    if (S.ready && S.readyFor == key) return;
    FreeModels();
    if (S.W.wholeMap) for (size_t i = 0; i < S.W.isles.size(); i++) { rt::MeshBuilder mb; if (S.W.isles[i].type != fl::IsleType::Wreck) BuildTerrain(mb, S.W.isles[i], (int)i == S.W.home ? 1 : 2); else mb.Tri({0, -30, 0}, {0.1f, -30, 0}, {0, -30, 0.1f}, BLACK); S.terr.push_back(LoadModelFromMesh(mb.Build())); }
    else { rt::MeshBuilder mb; BuildTerrain(mb, S.W.island); S.terr.push_back(LoadModelFromMesh(mb.Build())); }
    { rt::MeshBuilder mb; BuildPalm(mb); S.palm = LoadModelFromMesh(mb.Build()); }
    { rt::MeshBuilder mb; BuildNest(mb); S.nest = LoadModelFromMesh(mb.Build()); }
    { rt::MeshBuilder mb; mb.Lathe(0.075f, 5, 8, [](float u) { return 0.028f * sinf(std::max(0.05f, u) * PI) * (1.1f - 0.25f * u); }, [](float u) { return 0.028f * sinf(std::max(0.05f, u) * PI) * (1.1f - 0.25f * u); }, {244, 238, 226, 255}, {226, 218, 204, 255}); S.egg = LoadModelFromMesh(mb.Build()); }
    { rt::MeshBuilder mb;   // a chick: a ball of down, a head, a gaping beak
      Color down = Mix(S.W.Def().belly, Color{200, 196, 186, 255}, 0.6f);
      mb.Lathe(0.16f, 5, 8, [](float u) { return 0.075f * sinf(std::max(0.08f, u) * PI); }, [](float u) { return 0.07f * sinf(std::max(0.08f, u) * PI); }, down, Shade(down, 0.85f), {0, 0.07f, 0});
      mb.Lathe(0.09f, 4, 8, [](float u) { return 0.045f * sinf(std::max(0.08f, u) * PI); }, [](float u) { return 0.045f * sinf(std::max(0.08f, u) * PI); }, down, Shade(down, 0.9f), {0, 0.16f, 0.06f});
      mb.Cone({0, 0.16f, 0.1f}, {0, 0.17f, 0.15f}, 0.018f, 5, S.W.Def().accent);
      mb.Octa({0.025f, 0.18f, 0.09f}, 0.008f, {18, 18, 20, 255}); mb.Octa({-0.025f, 0.18f, 0.09f}, 0.008f, {18, 18, 20, 255});
      S.chick = LoadModelFromMesh(mb.Build()); }
    { rt::MeshBuilder mb;   // a cache: a low platform of sticks
      for (int k = 0; k < 14; k++) { float a = k * 0.45f, r = 0.35f + 0.25f * Hash((float)k, 3); Vector3 c{cosf(a) * r * 0.5f, 0.03f + 0.025f * (k % 3), sinf(a) * r * 0.5f}; mb.Tube({Vector3Add(c, {cosf(a + 1.6f) * 0.45f, 0, sinf(a + 1.6f) * 0.45f}), Vector3Subtract(c, {cosf(a + 1.6f) * 0.45f, 0, sinf(a + 1.6f) * 0.45f})}, 0.03f, 0.025f, 4, {118, 90, 58, 255}, {140, 108, 70, 255}, 0); }
      S.pile = LoadModelFromMesh(mb.Build()); }
    BuildBird(S.W.Def());
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
    fl::Founder& f = S.W.me;
    if (S.shot) { in.yaw = S.aimYaw; in.pitch = S.aimPitch; return in; }
    if (IsKeyPressed(KEY_TAB)) { if (!S.panel) { S.panel = true; S.page = 0; } else if (S.page == 0) S.page = 1; else S.panel = false; S.chart = false; }
    // G: the Founder takes the lead of the nearest flock of yours (or lets it go)
    if (IsKeyPressed(KEY_G) && f.st != fl::FState::Dead && !f.chick) {
        fl::Flock* best = nullptr; float bd = 70;
        for (auto& fk : S.W.col.flocks) { float d = Vector3Distance(fk.pos, f.pos); if (d < bd) { bd = d; best = &fk; } }
        if (best && best->leader == -2) { best->leader = -1; S.W.ColonySay("You leave " + best->name + "."); }
        else if (best) { for (auto& fk : S.W.col.flocks) if (fk.leader == -2) fk.leader = -1; best->leader = -2; best->target = fl::Target::Home; S.W.ColonySay("You lead " + best->name + ": it follows you (+20 morale, +10% speed)."); }
        else S.W.ColonySay("No flock of yours within 70 m to lead.");
    }
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
// One bird's body with its wings posed: shoulder (up/down beat), wrist, fold (0 spread .. 1 folded along the body),
// flare (wings up and forward for a landing), head tilt and tail spread. Used for the Founder and every colony bird.
void DrawBirdBody(const fl::FounderDef& d, Matrix W, float shoulder, float wrist, float fold, float flare, float headTilt, float tailSpread, Color tint = WHITE) {
    float L = 0.22f + d.span * 0.14f;
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
    Matrix headM = MatrixMultiply(MatrixMultiply(MatrixRotateX(headTilt), MatrixTranslate(0, L * 0.1f, L * 0.5f)), W);
    rt::DrawStatic(S.head, headM, tint);
    rt::DrawStatic(S.beak, MatrixMultiply(MatrixTranslate(0, 0, L * 0.17f), headM));
    rt::DrawStatic(S.tail, MatrixMultiply(MatrixMultiply(MatrixScale(tailSpread, 1, 1), MatrixMultiply(MatrixRotateX(-0.15f + 0.4f * flare), MatrixTranslate(0, 0, -L * 0.42f))), W), tint);
    wing(1); wing(-1);
}
Matrix PoseWorld(Vector3 pos, float yaw, float pitch, float bank, float scale) {
    Matrix m = MatrixScale(scale, scale, scale);
    m = MatrixMultiply(m, MatrixRotateY(PI * 0.5f));
    m = MatrixMultiply(m, MatrixRotateX(bank));
    m = MatrixMultiply(m, MatrixRotateZ(pitch));
    m = MatrixMultiply(m, MatrixRotateY(-yaw));
    return MatrixMultiply(m, MatrixTranslate(pos.x, pos.y, pos.z));
}
void DrawCarried(const fl::World& w, Matrix W, float L, int sp, int twigs, float yaw, float pitch) {
    if (sp >= 0 && w.eco.map) {
        const rt::CreatureModel& cm = rt::Creature(w.seaKey, w.eco.map->species[sp].name);
        rt::DrawCreature(cm, Vector3Transform({0, -L * 0.35f, 0}, W), atan2f(cosf(yaw), sinf(yaw)), pitch * 0.5f, 1.0f, S.t * cm.freq * 1.5f, 0.6f);
    }
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
    DrawBirdBody(d, W, shoulder, wrist, S.fold, S.flare, f.st == fl::FState::Strike ? 0.35f : -0.1f * f.pitch, 0.7f + 0.6f * S.flare + 0.2f * fabsf(f.bank));
    DrawCarried(w, W, L, f.carrySp, f.carryTwigs, f.yaw, f.pitch);
}
// the colony's birds: smaller than the Founder (its species, the colony's look), posed from their motion; a warrior's
// role shows in its size and colouring (a small sleek Skirmisher, a dark Striker, a round pale Watcher, a gull-white
// Screamer, a black Flockmaster, a heavy Tank); a rival's birds wear its livery
Color Tint(Color a, Color b) { return {(unsigned char)(a.r * b.r / 255), (unsigned char)(a.g * b.g / 255), (unsigned char)(a.b * b.b / 255), 255}; }
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
void DrawColonyBird(const fl::World& w, const fl::Bird& b, Color side) {
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
    DrawBirdBody(d, W, shoulder, sitting ? 0 : 0.35f * sinf(b.flapPh - 0.9f), sitting ? 1.0f : diving ? 0.85f : 0.0f, 0, sitting ? 0.1f * sinf(S.t * 2 + b.id) : 0, 0.7f, Tint(tint, side));
    DrawCarried(w, W, L * sc, b.carrySp, b.carryTwigs + b.carryShells, b.yaw, pitch);
    if (b.role == fl::Role::Tank) rt::DrawCubeM(MatrixMultiply(MatrixScale(L * 0.5f, L * 0.08f, L * 0.7f), MatrixMultiply(MatrixTranslate(0, L * 0.22f, 0), W)), Color{214, 206, 190, 255});   // (shell armour on its back)
}
void DrawColony(const fl::World& w, const fl::Colony& c, const Camera3D& cam, Color side) {
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
        } else DrawColonyBird(w, b, side);
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
        float k = s.built ? 1.0f : std::clamp(s.twigs / std::max(1, fl::StructureTwigs(s.kind)), 0.1f, 1.0f);
        if (s.kind == 0) for (int j = 0; j < 16; j++) {
            float a = j * 2 * PI / 16; Vector3 p = Vector3Add(s.pos, {cosf(a) * 7, 0, sinf(a) * 7}); p.y = w.HeightAt(p.x, p.z);
            rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(2.8f, 1.4f * k, 0.9f), MatrixRotateY(-a + PI / 2)), MatrixTranslate(p.x, p.y + 0.6f * k, p.z)), Color{70, 96, 52, 255});
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
    DrawColony(w, w.col, cam, WHITE);
    for (size_t s = 0; s < w.sides.size(); s++) {
        DrawColony(w, w.sides[s].col, cam, w.sides[s].livery);
        const fl::Founder& f = w.sides[s].me;
        if (f.st != fl::FState::Dead && Vector3Distance(f.pos, cam.position) < 260) DrawBirdBody(w.Def(), PoseWorld(f.pos, f.yaw, 0, 0, 1.0f), 0.4f * sinf(S.t * 6 + s), 0.2f, 0.2f, 0, 0, 0.7f, w.sides[s].livery);
    }
    DrawWarFx(w);
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
    // dark plates under the text (the sky is bright)
    DrawRectangleRounded({12, 10, 470, 62}, 0.2f, 6, Fade(Color{10, 20, 30, 255}, 0.5f));
    if (!w.log.empty()) DrawRectangleRounded({12, 76, 560, 94}, 0.15f, 6, Fade(Color{10, 20, 30, 255}, 0.35f));
    DrawRectangleRounded({SCREEN_W - 260.0f, 24, 248, 196}, 0.12f, 6, Fade(Color{10, 20, 30, 255}, 0.45f));
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
        if (rise) DrawTextCentered("the fish are rising", c.x - 30, c.y + 88, 15, Color{180, 255, 220, 255});
        float dof = w.DaysOfFood();
        DrawTextCentered(TextFormat("colony: %d birds", w.Alive()), c.x - 20, c.y + 110, 16, ink);
        DrawTextCentered(TextFormat("food: %.1f days   Tab: colony", dof), c.x - 50, c.y + 130, 14, dof < 1 && w.Alive() > 1 ? Color{255, 140, 120, 255} : dim);
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
    if (!w.col.caches.empty()) for (int s = 1; s <= (int)w.sides.size(); s++) for (const auto& fk : w.sides[s - 1].col.flocks)
        if (!fk.retreating && Vector2Distance({fk.pos.x, fk.pos.z}, {w.col.caches[0].pos.x, w.col.caches[0].pos.z}) < 250 && fmodf(S.t, 1.2f) < 0.8f)
            DrawTextCenteredBold(TextFormat("RAID: %s's %s (%d) over your island", w.SideName(s).c_str(), fk.name.c_str(), (int)fk.members.size()), SCREEN_W / 2.0f, 104, 20, Color{255, 110, 90, 255});
    for (const auto& fk : w.col.flocks) if (fk.leader == -2) {
        DrawRectangle(SCREEN_W / 2 - 110, SCREEN_H - 152, 220, 10, Fade(BLACK, 0.5f));
        DrawRectangle(SCREEN_W / 2 - 110, SCREEN_H - 152, (int)(220 * fk.morale / 100), 10, fk.morale < 30 ? Color{230, 90, 70, 255} : Color{120, 220, 140, 255});
        DrawTextCentered(TextFormat("leading %s (%d): morale %.0f", fk.name.c_str(), (int)fk.members.size(), fk.morale), SCREEN_W / 2.0f, SCREEN_H - 172, 15, ink);
    }
    for (int s = 0; s <= (int)w.sides.size(); s++) for (const auto& b : (s == 0 ? w.col : w.sides[s - 1].col).birds) {
        if (!b.alive || b.stage != fl::BStage::Adult || !fl::IsWarrior(b.role) || (b.tgtSide < 0 && b.hp >= fl::RoleOf(b.role).hp)) continue;
        if (Vector3Distance(b.pos, S.cam.position) > 70) continue;
        Vector3 fwd = Vector3Normalize(Vector3Subtract(S.cam.target, S.cam.position));
        if (Vector3DotProduct(Vector3Subtract(b.pos, S.cam.position), fwd) < 0) continue;
        Vector2 p = GetWorldToScreenEx(Vector3Add(b.pos, {0, 0.8f, 0}), S.cam, SCREEN_W, SCREEN_H);
        float k = std::clamp(b.hp / fl::RoleOf(b.role).hp, 0.0f, 1.0f);
        DrawRectangle((int)p.x - 12, (int)p.y, 24, 3, Fade(BLACK, 0.6f));
        DrawRectangle((int)p.x - 12, (int)p.y, (int)(24 * k), 3, s == 0 ? Color{120, 230, 140, 255} : w.SideColor(s));
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
    line(TextFormat("Twigs %d   shells %d   wild mates left %d", w.col.twigs, w.col.shells, w.col.wildMates), dim);
    ly += 6;
    // roles: the count, and a button that retrains one bird into it (from the biggest other role; a day's retraining)
    TxtBold("Roles", x + 16, ly, 17, ink); Txt("fledging plan", x + 250, ly + 1, 15, dim); ly += 24;
    for (int r = 1; r < (int)fl::Role::COUNT; r++) {
        fl::Role role = (fl::Role)r;
        int n = w.Count(fl::BStage::Adult, role), training = 0;
        for (const auto& b : w.col.birds) if (b.alive && b.retrainT > 0 && b.retrainTo == role) training++;
        Txt(TextFormat("%-8s %2d%s", fl::RoleName(role), n, training ? TextFormat(" (+%d)", training) : ""), x + 16, ly + 2, 14, fl::IsWarrior(role) ? Color{255, 200, 170, 255} : ink);
        if (SmallBtn({x + 168, ly, 66, 19}, "retrain", w.Count(fl::BStage::Adult) > n)) {
            w.Retrain(role);
        }
        float total = 0; for (int k = 1; k < (int)fl::Role::COUNT; k++) total += w.col.plan[k];
        Txt(TextFormat("%3.0f%%", 100 * w.col.plan[r] / std::max(0.01f, total)), x + 262, ly + 2, 14, ink);
        if (SmallBtn({x + 318, ly, 26, 19}, "-", w.col.plan[r] > 0.01f)) w.col.plan[r] = std::max(0.0f, w.col.plan[r] - 0.1f);
        if (SmallBtn({x + 350, ly, 26, 19}, "+", true)) w.col.plan[r] += 0.1f;
        ly += 21;
    }
    ly += 4;
    // nests
    int built = 0, under = 0, free = 0; for (const auto& n : w.col.nests) (n.built ? built : under)++;
    for (const auto& s : w.col.sites) free += s.nest < 0;
    TxtBold("Nests", x + 16, ly, 17, ink); ly += 24;
    Txt(TextFormat("%d built, %d under way, %d free sites", built, under, free), x + 16, ly + 3, 16, ink);
    Txt(TextFormat("builders raise up to %d", w.col.nestsWanted), x + 16, ly + 24, 15, dim);
    if (SmallBtn({x + 318, ly + 18, 26, 24}, "-", w.col.nestsWanted > 1)) w.col.nestsWanted--;
    if (SmallBtn({x + 350, ly + 18, 26, 24}, "+", w.col.nestsWanted < (int)w.col.sites.size())) w.col.nestsWanted++;
    ly += 52;
    Txt("A mate comes to a nest whose courtship bowl you fill:", x + 16, ly, 14, dim); ly += 17;
    Txt(TextFormat("carry fish of size %d+ to it and press E (%d for the first).", E.courtMinSize, E.courtFish), x + 16, ly, 14, dim); ly += 26;
    // the fishers' ground
    TxtBold("Fishers fish", x + 16, ly, 17, ink);
    const auto& zones = w.eco.map->zones;
    std::string gname = w.col.ground < 0 ? "the best ground" : zones[w.col.ground].name;
    if (SmallBtn({x + 150, ly - 2, 26, 24}, "<", true)) w.col.ground = w.col.ground < 0 ? (int)zones.size() - 1 : w.col.ground - 1;
    Txt(gname, x + 184, ly + 1, 16, ink);
    if (SmallBtn({x + 350, ly - 2, 26, 24}, ">", true)) w.col.ground = w.col.ground + 1 >= (int)zones.size() ? -1 : w.col.ground + 1;
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
std::string TargetText(fl::World& w, const fl::Flock& f) {
    switch (f.target) {
    case fl::Target::Home: return "guarding home";
    case fl::Target::Cache: return "raiding " + w.SideName(f.tSide) + "'s caches";
    case fl::Target::Nests: return "taking " + w.SideName(f.tSide) + "'s chicks";
    case fl::Target::Ground: return f.tZone >= 0 ? "harassing " + w.eco.map->zones[f.tZone].name : "over a ground";
    case fl::Target::Flock: { fl::Flock* t = w.FindFlock(f.tSide, f.tFlock); return t ? "intercepting " + w.SideName(f.tSide) + "'s " + t->name : "intercepting"; }
    default: return "flying to a mark";
    }
}
void DrawFlockPanel(fl::World& w) {
    Color ink{250, 248, 236, 255}, dim{200, 214, 214, 255}, bad{255, 140, 120, 255}, good{170, 240, 180, 255};
    float x = 20, y = 78, W = 440, H = 620;
    DrawRectangleRounded({x, y, W, H}, 0.05f, 6, Fade(Color{8, 18, 28, 255}, 0.86f));
    DrawRectangleRoundedLinesEx({x, y, W, H}, 0.05f, 6, 2, Color{200, 110, 90, 255});
    TxtBold("Flocks", x + 16, y + 10, 22, ink);
    Txt("Tab closes", x + W - 86, y + 16, 14, dim);
    float ly = y + 42;
    // idle warriors, and a flock of them
    int idle[(int)fl::Role::COUNT] = {}, nIdle = 0;
    std::vector<int> ids;
    for (const auto& b : w.col.birds) if (b.alive && b.stage == fl::BStage::Adult && fl::IsWarrior(b.role) && b.role != fl::Role::Watcher && b.flock < 0 && b.retrainT <= 0) { idle[(int)b.role]++; nIdle++; ids.push_back(b.id); }
    std::string sum; for (int r = (int)fl::Role::Skirmisher; r < (int)fl::Role::COUNT; r++) if (idle[r]) sum += TextFormat("%d %s  ", idle[r], fl::RoleName((fl::Role)r));
    Txt(nIdle ? "Idle: " + sum : "No idle warriors: retrain some on the colony page, or set warriors in the fledging plan.", x + 16, ly, 14, nIdle ? ink : dim); ly += 20;
    if (SmallBtn({x + 16, ly, 250, 22}, "Form a flock of them", nIdle >= 2)) {
        bool strikers = idle[(int)fl::Role::Striker] > 0, skirm = idle[(int)fl::Role::Skirmisher] > 0;
        int f = w.MakeFlock(0, ids, strikers && skirm ? fl::Formation::Hammer : fl::Formation::Chevron, strikers ? fl::Alt::High : fl::Alt::Mid, fl::Stance::RetreatHalf);
        if (f >= 0) { S.selFlock = f; w.ColonySay(TextFormat("Flock %d formed: pick a target on the chart (M).", f)); }
    }
    ly += 30;
    // each flock
    for (auto& f : w.col.flocks) {
        if (ly > y + H - 150) break;
        bool sel = S.selFlock == f.id;
        DrawRectangleRounded({x + 10, ly - 4, W - 20, 96}, 0.1f, 4, Fade(sel ? Color{90, 60, 40, 255} : Color{30, 40, 50, 255}, 0.7f));
        int n[(int)fl::Role::COUNT] = {};
        for (int id : f.members) if (fl::Bird* b = w.FindBird(0, id)) n[(int)b->role]++;
        std::string comp; static const char* AB[] = {"", "", "", "", "", "Skm", "Tnk", "Str", "Wch", "Scr", "FM"};
        for (int r = (int)fl::Role::Skirmisher; r < (int)fl::Role::COUNT; r++) if (n[r]) comp += TextFormat("%d %s ", n[r], AB[r]);
        TxtBold(f.name, x + 18, ly, 16, ink);
        Txt(TextFormat("%s   led by %s", comp.c_str(), f.leader == -2 ? "the Founder" : f.leader >= 0 ? "a Flockmaster" : "no one"), x + 120, ly + 2, 13, dim);
        // morale
        DrawRectangle((int)x + 18, (int)ly + 22, 160, 8, Fade(BLACK, 0.5f));
        DrawRectangle((int)x + 18, (int)ly + 22, (int)(160 * f.morale / 100), 8, f.morale < 30 ? bad : f.morale < 50 ? Color{240, 200, 90, 255} : good);
        Txt(TextFormat("morale %.0f%s", f.morale, f.retreating ? ", retreating" : ""), x + 186, ly + 18, 13, f.retreating ? bad : dim);
        // formation, height, stance
        auto cyc = [&](float cx, const char* text, int& v, int nv) {
            if (SmallBtn({cx, ly + 36, 18, 19}, "<")) v = (v + nv - 1) % nv;
            Txt(text, cx + 22, ly + 38, 13, ink);
            if (SmallBtn({cx + 108, ly + 36, 18, 19}, ">")) v = (v + 1) % nv;
        };
        int fo = (int)f.form, al = (int)f.alt, stc = (int)f.stance;
        cyc(x + 18, fl::FormationName(f.form), fo, (int)fl::Formation::COUNT);
        cyc(x + 158, TextFormat("%s %.0f m", fl::AltName(f.alt), fl::AltHeight(f.alt)), al, 3);
        cyc(x + 298, fl::StanceName(f.stance), stc, (int)fl::Stance::COUNT);
        f.form = (fl::Formation)fo; f.alt = (fl::Alt)al; f.stance = (fl::Stance)stc;
        Txt(TargetText(w, f), x + 18, ly + 62, 13, Color{255, 220, 170, 255});
        if (SmallBtn({x + 240, ly + 60, 60, 20}, sel ? "picked" : "pick")) S.selFlock = f.id;
        if (SmallBtn({x + 304, ly + 60, 56, 20}, "home")) w.OrderFlock(0, f.id, fl::Target::Home, -1, -1, -1, -1, {});
        if (SmallBtn({x + 364, ly + 60, 56, 20}, "disband")) { f.retreating = true; f.target = fl::Target::Home; f.engagedT = 0; }
        ly += 104;
    }
    // defences
    ly = y + H - 140;
    TxtBold("Defences", x + 16, ly, 17, ink); ly += 24;
    const fl::Structure* hedge = nullptr; const fl::Structure* tower = nullptr;
    for (const auto& s : w.col.builds) { if (s.kind == 0) hedge = &s; else tower = &s; }
    int watchers = w.Count(fl::BStage::Adult, fl::Role::Watcher);
    auto row = [&](const char* name, const fl::Structure* s, int kind, const char* cost) {
        std::string st = !s ? "none" : s->built ? "built" : TextFormat("under way: %.0f of %d twigs", s->twigs, fl::StructureTwigs(kind));
        Txt(TextFormat("%s: %s", name, st.c_str()), x + 16, ly + 2, 14, s && s->built ? good : ink);
        if (!s && SmallBtn({x + W - 206, ly, 190, 20}, cost)) { fl::Structure n; n.kind = kind; n.pos = kind == 0 ? w.col.caches[0].pos : w.GroundAt(w.col.caches[0].pos.x + 6, w.col.caches[0].pos.z + 4); w.col.builds.push_back(n); w.ColonySay(std::string("Your builders will raise a ") + (kind == 0 ? "hedge." : "tower.")); }
        ly += 24;
    };
    row("Hedge round the home cache", hedge, 0, "raise (10 twigs)");
    row("Tower for a Watcher", tower, 1, "raise (20 twigs, 5 shells)");
    Txt(TextFormat("%d Watchers guard the nests (hedges stop Skirmishers; Strikers go over)", watchers), x + 16, ly + 2, 13, dim); ly += 22;
    DrawWrapped("Pick a flock, then click a target on the chart (M). G: the Founder leads the nearest flock (+20 morale, +10% speed).", {x + 16, ly, W - 32, 34}, 13, dim);
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
        if (s > 0 && K.SeenAt(fk.pos.x, fk.pos.z) < w.time - 2) continue;
        Vector2 p = toS(fk.pos.x, fk.pos.z);
        Color c = s == 0 ? Color{40, 110, 60, 255} : w.SideColor(s);
        DrawCircleV(p, 9, Fade(c, 0.85f)); DrawCircleLinesV(p, 9, ink);
        DrawTextCentered(TextFormat("%d", (int)fk.members.size()), p.x, p.y - 6, 12, WHITE);
        if (s == 0) DrawTextCentered(fk.name + (S.selFlock == fk.id ? " *" : ""), p.x, p.y + 11, 11, ink);
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
    if (SmallBtn({side.x + 12, y, 86, 24}, "Scout", true)) S.chartMode = 0;
    if (SmallBtn({side.x + 104, y, 86, 24}, "Fishers", true)) S.chartMode = 1;
    if (SmallBtn({side.x + 196, y, 86, 24}, "Flock", true)) S.chartMode = 2;
    DrawRectangleLinesEx({side.x + 12 + S.chartMode * 92.0f - 2, y - 2, 90, 28}, 2, Color{200, 60, 40, 255});
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
        if (SmallBtn({side.x + 12, y + 36, 150, 24}, "the best ground", true)) w.col.ground = -1;
    } else {
        fl::Flock* sf = w.FindFlock(0, S.selFlock);
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
        if (S.chartMode == 0) {
            if (!w.SendScout(isle, isle < 0 ? zone : -1, {at.x, 0, at.y}, S.chartAlt)) w.ColonySay(scouts ? "Every scout is out." : "No scouts in the colony.");
        } else if (S.chartMode == 1) { if (zone >= 0) { w.col.ground = zone; w.ColonySay("The fishers will work " + w.eco.map->zones[zone].name + "."); } }
        else if (fl::Flock* sf = w.FindFlock(0, S.selFlock)) {
            // an enemy flock where you clicked (one you can see now), an island (yours: guard it; a rival's: raid it), a ground, a mark
            int ts = -1, tf = -1; float bd = 40 / std::max(0.3f, S.chartZoom) * 3;
            for (int s = 1; s <= (int)w.sides.size(); s++) for (const auto& ef : w.ColOf(s).flocks) if (K.SeenAt(ef.pos.x, ef.pos.z) > w.time - 2 && Vector2Distance(at, {ef.pos.x, ef.pos.z}) < bd) { bd = Vector2Distance(at, {ef.pos.x, ef.pos.z}); ts = s; tf = ef.id; }
            int owner = -1; if (isle >= 0) for (int s = 1; s <= (int)w.sides.size(); s++) if (w.sides[s - 1].home == isle) owner = s;
            if (tf >= 0) w.OrderFlock(0, sf->id, fl::Target::Flock, ts, -1, -1, tf, {});
            else if (isle == w.home) w.OrderFlock(0, sf->id, fl::Target::Home, -1, -1, -1, -1, {});
            else if (owner > 0) w.OrderFlock(0, sf->id, S.raidChicks ? fl::Target::Nests : fl::Target::Cache, owner, isle, -1, -1, w.isles[isle].c);
            else if (zone >= 0 && isle < 0) w.OrderFlock(0, sf->id, fl::Target::Ground, -1, -1, zone, -1, {at.x, 0, at.y});
            else w.OrderFlock(0, sf->id, fl::Target::Point, -1, -1, -1, -1, {at.x, 0, at.y});
            if (sf->leader == -2) sf->leader = -1;
            w.ColonySay(sf->name + ": " + TargetText(w, *sf) + ".");
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
    fl::World& w = S.W;
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
    rt::ApplyGameQuality();
    rt::RenderBegin(S.cam, L);
    rt::SkyLook sk;
    sk.zenith = day.zenith; sk.horizon = day.horizon; sk.cloud = day.cloud;
    sk.moonDir = day.toSun; sk.moonPhase = 0.5f; sk.cloudCover = 0.32f; sk.stars = day.night; sk.time = S.t;
    rt::DrawSkyDome(sk);
    StepWarFx(w, dt);
    DrawWorld(w, S.cam, dt);
    UpdateSea(S.cam.position, S.t);
    rt::WaterLook wl;
    wl.deep = Mix(Color{8, 20, 34, 255}, Color{34, 150, 160, 255}, 1 - day.night);
    wl.zenith = day.zenith; wl.horizon = day.horizon;
    wl.boatPos = {1e6f, 1e6f}; wl.boatLen = 0.1f; wl.boatBeam = 0.1f;
    wl.alpha = 0.66f; wl.moonK = day.sunK * 0.8f; wl.crest = 0.04f;
    // blood in the water where a fish fought the talons
    if (f.st == fl::FState::Struggle) { wl.stains = 1; wl.stain[0] = {f.pos.x, f.pos.z, 1.5f, 0.8f}; }
    for (const auto& s : gStains) if (wl.stains < 8) wl.stain[wl.stains++] = {s.p.x, s.p.y, 0.8f + std::min(3.5f, s.age * 0.3f), std::clamp(1 - s.age / 40, 0.0f, 1.0f)};   // (where a bird went into the sea)
    rt::DrawWater(S.sea, wl);
    rt::RenderEnd();
}

void Start(Game& g, const std::string& founder, uint32_t seed, bool shot, const fl::MapOpts& o = fl::MapOpts{}) {
    std::string why;
    if (!rt::DataOk(&why)) { g.scene = Scene::Arcade; return; }
    S.active = true; S.shot = shot; S.founder = founder; S.opts = o;
    S.W.Init(founder, seed, o);
    S.chart = false; S.panel = false; S.chartZoom = 1; S.chartAt = {0, 0};
    S.aimYaw = S.W.me.yaw; S.aimPitch = 0.1f; S.camYaw = S.aimYaw; S.camPitch = -0.1f;
    S.t = 0; S.help = true;
    g.scene = Scene::Flight;
}
}  // namespace

void StartFlight(Game& g, const char* founder, int isleType, int arrangement, int players) {
    fl::MapOpts o; o.home = (fl::IsleType)std::clamp(isleType, 0, 3); o.arr = (fl::Arrangement)std::clamp(arrangement, 0, 3); o.players = std::clamp(players, 2, 6);
    Start(g, founder ? founder : "taloned", (uint32_t)GetRandomValue(1, 1 << 30), false, o);
}
const char* FlightIsleTypeName(int t) { return fl::IsleTypeName((fl::IsleType)std::clamp(t, 0, 3)); }
const char* FlightArrangementName(int a) { return fl::ArrangementName((fl::Arrangement)std::clamp(a, 0, 3)); }
void LeaveFlight(Game& g) { S.active = false; FreeModels(); g.scene = Scene::Arcade; }
const char* FlightFounderName(int i) { const auto& v = fl::Founders(); return i >= 0 && i < (int)v.size() ? v[i].name.c_str() : "?"; }
const char* FlightFounderKey(int i) { const auto& v = fl::Founders(); return i >= 0 && i < (int)v.size() ? v[i].key.c_str() : "taloned"; }
const char* FlightFounderLine(int i) { const auto& v = fl::Founders(); return i >= 0 && i < (int)v.size() ? v[i].playstyle.c_str() : ""; }
int FlightFounderCount() { return (int)fl::Founders().size(); }

void SceneFlight(Game& g) {
    if (!S.active) { StartFlight(g, "taloned"); if (!S.active) return; }
    float dt = S.shot ? 1 / 60.0f : std::min(GetFrameTime(), 1 / 30.0f);
    if (S.shot && S.W.me.st == fl::FState::Strike) S.W.me.strikeT = std::min(S.W.me.strikeT, 0.2f);   // (the shot holds the strike)
    fl::FounderInput in = Gather(dt);
    S.W.Step(dt, in);
    S.t += dt * S.W.timeScale;
    Render(dt * S.W.timeScale);
    DrawHud(S.W);
    if (S.panel) { if (S.page == 0) DrawColonyPanel(S.W); else DrawFlockPanel(S.W); }
    if (S.chart) DrawChart(S.W);
}

// --shots: 0 cruising over the lagoon at dawn, 1 the strike, 2 at the nest with a fish, 3 noon from high over the island
void DebugFlightShot(Game& g, int which) {
    fl::MapOpts o; o.home = which == 6 ? fl::IsleType::Stack : which == 7 ? fl::IsleType::Town : which == 8 ? fl::IsleType::Atoll : fl::IsleType::Tropical;
    Start(g, which == 3 ? "albatross" : "taloned", 11, true, o);
    fl::World& w = S.W;
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
    S.aimYaw = which == 2 ? S.aimYaw : f.yaw; S.camYaw = S.aimYaw; S.camPitch = S.aimPitch * 0.8f - 0.12f;
}
