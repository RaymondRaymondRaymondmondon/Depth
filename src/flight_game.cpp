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
    Model terrain{}, palm{}, nest{}, sea{};
    Model body{}, head{}, beak{}, tail{}, wingIn{}, wingOut{};
    bool seaReady = false;
};
FlightScene S;

constexpr int SN = 120; constexpr float SC = 24;  // the sea grid round the eye: 120 cells of 24 m (the haze hides its edge)

Color Mix(Color a, Color b, float k) { k = std::clamp(k, 0.0f, 1.0f); return {(unsigned char)(a.r + (b.r - a.r) * k), (unsigned char)(a.g + (b.g - a.g) * k), (unsigned char)(a.b + (b.b - a.b) * k), 255}; }
Color Shade(Color c, float k) { return {(unsigned char)std::clamp(c.r * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.g * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.b * k, 0.0f, 255.0f), c.a}; }
float Hash(float x, float z) { float h = sinf(x * 12.9898f + z * 78.233f) * 43758.5453f; return h - floorf(h); }
void TwoSided(rt::MeshBuilder& mb, Vector3 a, Vector3 b, Vector3 c, Color col) { mb.Tri(a, b, c, col); mb.Tri(a, c, b, col); }

// ---------------------------------------------------------------- models
void BuildTerrain(rt::MeshBuilder& mb, const fl::Island& is) {
    // the island as one mesh: wet sand at the waterline, dry sand, grass, jungle green inland, grey rock where it's steep;
    // under the water the sand darkens with depth (the lagoon's turquoise is the sand seen through the water)
    const Color wet{196, 178, 132, 255}, sand{236, 220, 170, 255}, grass{118, 168, 74, 255}, jungle{58, 120, 58, 255}, rock{150, 146, 136, 255}, seabed{210, 200, 160, 255}, deep{60, 96, 104, 255};
    auto col = [&](float x, float z) {
        float h = is.Height(x, z), n = is.Normal(x, z).y, j = Hash(floorf(x * 0.5f), floorf(z * 0.5f)) * 0.08f - 0.04f;
        Color c;
        if (h < -0.2f) c = Mix(seabed, deep, std::clamp((-h - 0.2f) / 11, 0.0f, 1.0f));
        else if (h < 0.5f) c = wet;
        else if (h < 1.8f) c = sand;
        else if (h < 3.0f) c = Mix(sand, grass, (h - 1.8f) / 1.2f);
        else c = Mix(grass, jungle, std::clamp((h - 3) / 6, 0.0f, 1.0f));
        if (h > 0.5f && n < 0.8f) c = Mix(c, rock, (0.8f - n) / 0.2f);
        return Shade(c, 1 + j);
    };
    int step = 1;
    for (int zi = 0; zi < is.n - 1; zi += step) for (int xi = 0; xi < is.n - 1; xi += step) {
        float x0 = is.x0 + xi * is.cell, z0 = is.z0 + zi * is.cell, x1 = x0 + is.cell * step, z1 = z0 + is.cell * step;
        Vector3 a{x0, is.Height(x0, z0), z0}, b{x1, is.Height(x1, z0), z0}, c{x1, is.Height(x1, z1), z1}, d{x0, is.Height(x0, z1), z1};
        Color cc = col(x0 + is.cell * 0.5f, z0 + is.cell * 0.5f);
        mb.Tri(a, c, b, cc); mb.Tri(a, d, c, cc);
    }
}
void BuildPalm(rt::MeshBuilder& mb) {
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
    for (Model* m : {&S.terrain, &S.palm, &S.nest, &S.body, &S.head, &S.beak, &S.tail, &S.wingIn, &S.wingOut}) UnloadModel(*m);
    S.ready = false; S.readyFor = -1;
}
void EnsureModels() {
    if (!IsWindowReady()) return;
    if (S.ready && S.readyFor == (int)S.W.island.seed * 31 + S.W.me.def) return;
    FreeModels();
    { rt::MeshBuilder mb; BuildTerrain(mb, S.W.island); S.terrain = LoadModelFromMesh(mb.Build()); }
    { rt::MeshBuilder mb; BuildPalm(mb); S.palm = LoadModelFromMesh(mb.Build()); }
    { rt::MeshBuilder mb; BuildNest(mb); S.nest = LoadModelFromMesh(mb.Build()); }
    BuildBird(S.W.Def());
    S.ready = true; S.readyFor = (int)S.W.island.seed * 31 + S.W.me.def;
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
    Vector2 md = MouseLook(f.st != fl::FState::Dead);
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
    float sweep = S.fold * 1.2f, raise = S.flare * 0.6f;
    float half = d.span * 0.5f, inner = half * 0.45f;
    auto wing = [&](float side) {   // side 1 the right wing (out along -x), -1 the left (mirrored, out along +x)
        // after the mirror both wings use the right wing's angles times side: up/down about the body's axis (z),
        // back about y for the fold; the outer panel hangs off the wrist at the inner panel's end
        Matrix mir = side > 0 ? MatrixIdentity() : MatrixScale(-1, 1, 1);
        float up = shoulder + raise;
        Matrix sh = MatrixMultiply(MatrixRotateY(-sweep * 0.9f * side), MatrixRotateZ(-up * side));
        Matrix base = MatrixMultiply(MatrixTranslate(-L * 0.12f * side, L * 0.08f, L * 0.08f), W);
        Matrix shB = MatrixMultiply(sh, base);
        float inLen = inner * (1 - S.fold * 0.45f);   // (folded, the inner panel tucks in toward the body)
        rt::DrawStatic(S.wingIn, MatrixMultiply(MatrixMultiply(MatrixScale(1 - S.fold * 0.45f, 1, 1), mir), shB));
        Matrix el = MatrixMultiply(MatrixRotateY(-sweep * 1.4f * side), MatrixRotateZ(-wrist * side * (1 - S.fold)));
        Matrix outerM = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(1 - S.fold * 0.3f, 1, 1), mir), el), MatrixMultiply(MatrixTranslate(-inLen * side, 0, 0), shB));
        rt::DrawStatic(S.wingOut, outerM);
    };
    rt::DrawStatic(S.body, W);
    Matrix headM = MatrixMultiply(MatrixMultiply(MatrixRotateX(f.st == fl::FState::Strike ? 0.35f : -0.1f * f.pitch), MatrixTranslate(0, L * 0.1f, L * 0.5f)), W);
    rt::DrawStatic(S.head, headM);
    rt::DrawStatic(S.beak, MatrixMultiply(MatrixTranslate(0, 0, L * 0.17f * 1.0f), headM));
    float spread = 0.7f + 0.6f * S.flare + 0.2f * fabsf(f.bank);
    rt::DrawStatic(S.tail, MatrixMultiply(MatrixMultiply(MatrixScale(spread, 1, 1), MatrixMultiply(MatrixRotateX(-0.15f + 0.4f * S.flare), MatrixTranslate(0, 0, -L * 0.42f))), W));
    wing(1); wing(-1);
    // the talons' catch: carried head forward beneath the body
    if (f.carrySp >= 0 && w.eco.map) {
        const rt::CreatureModel& cm = rt::Creature(w.seaKey, w.eco.map->species[f.carrySp].name);
        Vector3 under = Vector3Transform({0, -L * 0.35f, 0}, W);
        rt::DrawCreature(cm, under, atan2f(cosf(f.yaw), sinf(f.yaw)), f.pitch * 0.5f, 1.0f, S.t * cm.freq * 1.5f, 0.6f);
    }
}
void DrawWorld(const fl::World& w, const Camera3D& cam, float dt) {
    const fl::Island& is = w.island;
    rt::DrawStatic(S.terrain, MatrixIdentity());
    rt::DrawWorldCube({w.island.x0 + 160, -12.6f, w.island.z0 + 160}, {5000, 0.4f, 5000}, Color{60, 96, 104, 255});   // (the sea floor beyond the island's shelf)
    for (size_t k = 0; k < is.palms.size(); k++) {
        const Vector3& p = is.palms[k];
        bool home = fabsf(p.x - is.nest.x) < 0.01f && fabsf(p.z - is.nest.z) < 0.01f;
        float h = home ? (is.nest.y - p.y) * 0.97f : 6 + 4 * Hash(p.x, p.z);   // (the nest's palm holds it in its crown)
        float sway = 0.03f * sinf(S.t * 0.8f + k);
        Matrix m = MatrixMultiply(MatrixMultiply(MatrixScale(h, h, h), MatrixRotateY(Hash(p.z, p.x) * 6.28f)), MatrixRotateZ(sway));
        rt::DrawStatic(S.palm, MatrixMultiply(m, MatrixTranslate(p.x, p.y - 0.2f, p.z)));
    }
    rt::DrawStatic(S.nest, MatrixTranslate(is.nest.x, is.nest.y - 0.05f, is.nest.z));
    // the nest's cache: the fish laid in it
    for (size_t k = 0; k < w.cache.size() && k < 8; k++) {
        const rt::CreatureModel& cm = rt::Creature(w.seaKey, w.eco.map->species[w.cache[k].sp].name);
        float a = k * 2.4f;
        rt::DrawCreature(cm, {is.nest.x + cosf(a) * 0.25f, is.nest.y + 0.08f, is.nest.z + sinf(a) * 0.25f}, a, 0, 0.6f, 0, 0);
    }
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
        DrawTextCentered(TextFormat("nest: %d fish", (int)w.cache.size()), c.x - 20, c.y + 110, 16, ink);
        DrawTextCentered(TextFormat("caught %d  missed %d  lost %d", w.fishCaught, w.fishMissed, w.fishLost), c.x - 70, c.y + 130, 14, dim);
    }
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
    } else if (f.st == fl::FState::Perched || f.st == fl::FState::Floating) {
        bool atNest = Vector3Distance(f.pos, w.island.nest) < 3;
        std::string tip = "Space: take off";
        if (f.carrySp >= 0 && atNest) tip += "    E: into the cache    F: eat it";
        else if (f.carrySp >= 0) tip += "    F: eat it here";
        else if (atNest && !w.cache.empty()) tip += "    F: eat from the cache";
        if (f.st == fl::FState::Floating) tip += "    (sharks hunt the water: don't sit long)";
        DrawTextCentered(tip, SCREEN_W / 2.0f, SCREEN_H - (S.help ? 110 : 60), 18, ink);
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
                               "Space: take off   E: drop a fish in the nest   F: eat   H: hide this   Esc: menu"};
        DrawRectangleRounded({SCREEN_W - 680.0f, SCREEN_H - 74.0f, 668, 62}, 0.2f, 6, Fade(Color{10, 20, 30, 255}, 0.45f));
        for (int k = 0; k < 3; k++) DrawTextCentered(lines[k], SCREEN_W - 346.0f, SCREEN_H - 70.0f + k * 18, 14, ink);
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
    float g = std::max(0.0f, w.island.Height(eye.x, eye.z));
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
    DrawWorld(w, S.cam, dt);
    UpdateSea(S.cam.position, S.t);
    rt::WaterLook wl;
    wl.deep = Mix(Color{8, 20, 34, 255}, Color{34, 150, 160, 255}, 1 - day.night);
    wl.zenith = day.zenith; wl.horizon = day.horizon;
    wl.boatPos = {1e6f, 1e6f}; wl.boatLen = 0.1f; wl.boatBeam = 0.1f;
    wl.alpha = 0.66f; wl.moonK = day.sunK * 0.8f; wl.crest = 0.04f;
    // blood in the water where a fish fought the talons
    if (f.st == fl::FState::Struggle) { wl.stains = 1; wl.stain[0] = {f.pos.x, f.pos.z, 1.5f, 0.8f}; }
    rt::DrawWater(S.sea, wl);
    rt::RenderEnd();
}

void Start(Game& g, const std::string& founder, uint32_t seed, bool shot) {
    std::string why;
    if (!rt::DataOk(&why)) { g.scene = Scene::Arcade; return; }
    S.active = true; S.shot = shot; S.founder = founder;
    S.W.Init(founder, seed);
    S.aimYaw = S.W.me.yaw; S.aimPitch = 0.1f; S.camYaw = S.aimYaw; S.camPitch = -0.1f;
    S.t = 0; S.help = true;
    g.scene = Scene::Flight;
}
}  // namespace

void StartFlight(Game& g, const char* founder) { Start(g, founder ? founder : "taloned", (uint32_t)GetRandomValue(1, 1 << 30), false); }
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
}

// --shots: 0 cruising over the lagoon at dawn, 1 the strike, 2 at the nest with a fish, 3 noon from high over the island
void DebugFlightShot(Game& g, int which) {
    Start(g, which == 3 ? "albatross" : "taloned", 11, true);
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
    if (which == 2) { w.time = fl::World::DAY * 0.42f; f.st = fl::FState::Perched; f.pos = w.island.nest; f.carrySp = w.eco.map->SpeciesIndex("Mullet"); f.carrySize = 2; w.cache.push_back({w.eco.map->SpeciesIndex("Sardine"), 1, 0}); S.aimPitch = -0.25f; S.aimYaw = PI * 0.9f; }
    if (which == 3) { w.time = fl::World::DAY * 0.27f; f.pos = {40, 70, 120}; f.yaw = -PI * 0.6f; S.aimPitch = -0.35f; }
    S.aimYaw = which == 2 ? S.aimYaw : f.yaw; S.camYaw = S.aimYaw; S.camPitch = S.aimPitch * 0.8f - 0.12f;
}
