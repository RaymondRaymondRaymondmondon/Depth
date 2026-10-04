// Mouthful's scene (arcade game 8): a mouth swum in third person through the reef shelf, drawn with Red Tide's inked
// renderer under water (design doc, "Presentation": Depth's inked low-poly, bright in the shallows and reef, dim in the
// blue, dark in the trench). The rules are mouthful.cpp (headless); this file feeds a person's input to their mouth and
// draws the round: the seabed, the coral, the web's fish, the mouths, the HUD, the fork, the crown and the results.
#include "game.h"
#include "mouthful.h"
#include "mouthful_net.h"
#include "arcade_session.h"
#include "sound.h"
#include "redtide_render.h"
#include "input.h"
#include "sound.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>

namespace {

struct MouthfulScene {
    bool active = false, shot = false;
    mf::World W;
    arcade::Session* net = nullptr; mf::World* live = nullptr;   // (a networked round: the host draws its real world, a guest its mirror)
    int seenVersion = -1; float sinceSnap = 0; bool helloSent = false; std::string netName = "Mouth";
    float aimYaw = 0, aimPitch = 0, camYaw = 0, camPitch = -0.15f, camDist = 2;
    float t = 0;
    bool help = true, board = false;
    Camera3D cam{};
    bool ready = false;
    std::vector<Model> floor;           // the seabed in chunks
    Model coral{}, kelp{}, props{};
    int me = 0;                         // the mouth this screen swims
    int forkClick = -1;                 // (a card clicked on the fork; applied with the next input)
    mf::Opts opts;
};
MouthfulScene S;
mf::World& WD() { return S.live ? *S.live : S.W; }


Color Mix(Color a, Color b, float k) { k = std::clamp(k, 0.0f, 1.0f); return {(unsigned char)(a.r + (b.r - a.r) * k), (unsigned char)(a.g + (b.g - a.g) * k), (unsigned char)(a.b + (b.b - a.b) * k), 255}; }
Color Shade(Color c, float k) { return {(unsigned char)std::clamp(c.r * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.g * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.b * k, 0.0f, 255.0f), c.a}; }
float Hash(float x, float z) { float h = sinf(x * 12.9898f + z * 78.233f) * 43758.5453f; return h - floorf(h); }
float Smooth01(float t) { return t * t * (3 - 2 * t); }
float Noise(float x, float z) {   // smooth value noise (patches without the grid showing)
    float xi = floorf(x), zi = floorf(z), fx = Smooth01(x - xi), fz = Smooth01(z - zi);
    float a = Hash(xi, zi), b = Hash(xi + 1, zi), c = Hash(xi, zi + 1), d = Hash(xi + 1, zi + 1);
    return (a + (b - a) * fx) * (1 - fz) + (c + (d - c) * fx) * fz;
}
void TwoSided(rt::MeshBuilder& mb, Vector3 a, Vector3 b, Vector3 c, Color col) { mb.Tri(a, b, c, col); mb.Tri(a, c, b, col); }

// ---------------------------------------------------------------- models
Color FloorColour(float x, float z, float y) {
    // sand in the shallows (seagrass in patches), the reef's coral rubble in warm mottles, the wall's dark rock, the
    // blue's pale silt, the trench's basalt; the brine pool a black mirror
    const Color sand{232, 218, 172, 255}, grass{98, 150, 84, 255}, rubble{200, 170, 140, 255}, rock{88, 92, 100, 255}, silt{176, 176, 160, 255}, basalt{46, 44, 52, 255};
    float n = Noise(x / 9, z / 9) * 0.7f + Noise(x / 3.5f + 7, z / 3.5f) * 0.3f, j = Noise(x * 0.8f, z * 0.8f) * 0.1f - 0.05f;
    Color c;
    int band = mf::BandAt({x, y + 1, z});
    if (x < -170) { c = Mix(sand, grass, std::clamp((0.32f - n) * 6, 0.0f, 1.0f)); if (Vector2Distance({x, z}, {-255, 70}) < 26) c = Mix(c, Color{210, 236, 220, 255}, 0.4f); }
    else if (x < -42) { static const Color R[] = {{226, 120, 112, 255}, {236, 172, 92, 255}, {180, 120, 190, 255}, {110, 190, 168, 255}}; float m2 = Noise(x / 2.5f + 3, z / 2.5f); c = Mix(rubble, R[(int)(Noise(x / 6 + 11, z / 6) * 4) % 4], std::clamp((m2 - 0.45f) * 4, 0.0f, 1.0f)); }
    else if (band == mf::B_TRENCH || y < -150) c = Mix(rock, basalt, std::clamp((-y - 120) / 120, 0.0f, 1.0f));
    else if (x < -6) c = rock;
    else c = silt;
    if (Vector2Distance({x, z}, {mf::BRINE.x, mf::BRINE.z}) < mf::BRINE_R) c = {20, 24, 30, 255};
    return Shade(c, 1 + j);
}
void BuildFloor() {
    const float step = 2.5f;
    const int chunks = 8;
    float cw = (mf::X1 - mf::X0) / chunks;
    for (int ch = 0; ch < chunks; ch++) {
        rt::MeshBuilder mb;
        float x0 = mf::X0 + ch * cw, x1 = x0 + cw;
        // the trench needs finer steps where the canyon walls are
        float st = x0 > 100 ? 3.0f : step;
        for (float z = mf::Z0 - 30; z < mf::Z1 + 30; z += st) for (float x = x0; x < x1 - 0.01f; x += st) {
            float xb = std::min(x + st, x1);
            Vector3 a{x, mf::FloorY(x, z), z}, b{xb, mf::FloorY(xb, z), z}, c{xb, mf::FloorY(xb, z + st), z + st}, d{x, mf::FloorY(x, z + st), z + st};
            Color ca = FloorColour(a.x, a.z, a.y), cb = FloorColour(b.x, b.z, b.y), cc = FloorColour(c.x, c.z, c.y), cd = FloorColour(d.x, d.z, d.y);
            mb.Tri(a, c, b, ca, cc, cb, {0, 0}, {0, 0}, {0, 0}); mb.Tri(a, d, c, ca, cd, cc, {0, 0}, {0, 0}, {0, 0});
        }
        // the arena's rim: a wall of rock where the reef's world ends
        S.floor.push_back(LoadModelFromMesh(mb.Build()));
    }
}
void BuildCoral() {
    rt::MeshBuilder mb;
    static const Color CC[] = {{232, 112, 104, 255}, {240, 176, 80, 255}, {176, 112, 196, 255}, {96, 196, 176, 255}, {236, 214, 196, 255}, {226, 90, 140, 255}};
    int k = 0;
    for (const auto& c : mf::Corals()) {
        Color col = CC[k++ % 6];
        // a head: a trunk of stacked lumps, branches, a brain-coral cap
        float h = c.h, r = c.r;
        mb.Lathe(h, 5, 8, [r](float u) { return r * (0.75f + 0.35f * sinf(u * 9)) * (1.05f - 0.3f * u); }, [r](float u) { return r * (0.75f + 0.35f * sinf(u * 9 + 1)) * (1.05f - 0.3f * u); },
                 col, Shade(col, 0.7f), {c.pos.x, c.pos.y + h * 0.5f - 0.3f, c.pos.z});
        for (int b = 0; b < 4; b++) {
            float a = b * 1.57f + k, bh = h * (0.5f + 0.15f * b);
            Vector3 p0{c.pos.x + cosf(a) * r * 0.6f, c.pos.y + bh, c.pos.z + sinf(a) * r * 0.6f};
            mb.Tube({p0, Vector3Add(p0, {cosf(a) * r * 0.7f, r * 0.9f, sinf(a) * r * 0.7f}), Vector3Add(p0, {cosf(a) * r * 0.9f, r * 1.6f, sinf(a) * r * 0.9f})}, r * 0.22f, r * 0.08f, 5, col, Mix(col, WHITE, 0.4f), 0);
        }
    }
    // the eel holes: dark mouths with a ring of rubble; the wall caves: deep recesses
    for (const auto& h : mf::Holes()) {
        Color d{18, 16, 20, 255};
        if (h.maxTier >= 5) { mb.Box(h.pos, {h.r * 0.6f, h.r, h.r * 1.1f}, d); continue; }
        mb.Lathe(h.r * 0.5f, 2, 8, [&](float u) { (void)u; return h.r * 0.75f; }, [&](float u) { (void)u; return h.r * 0.75f; }, d, d, {h.pos.x, h.pos.y - 0.2f, h.pos.z});
        for (int k2 = 0; k2 < 6; k2++) { float a = k2 * 1.05f; mb.Octa({h.pos.x + cosf(a) * h.r, h.pos.y - 0.1f, h.pos.z + sinf(a) * h.r}, 0.25f, {150, 130, 110, 255}); }
    }
    S.coral = LoadModelFromMesh(mb.Build());
}
void BuildKelp() {
    // the kelp curtain at the top of the wall, seagrass in the shallows
    rt::MeshBuilder mb;
    for (int i = 0; i < 160; i++) {
        float z = mf::Z0 + 4 + i * ((mf::Z1 - mf::Z0 - 8) / 160.0f) + Hash((float)i, 3) * 1.5f, x = -46 + Hash((float)i, 5) * 8;
        float y0 = mf::FloorY(x, z), h = 18 + Hash((float)i, 7) * 18;
        Color g = Mix(Color{70, 110, 50, 255}, Color{120, 140, 60, 255}, Hash((float)i, 9));
        Vector3 prev{x, y0, z};
        for (int s = 1; s <= 8; s++) {
            float u = s / 8.0f;
            Vector3 p{x + sinf(u * 3 + i) * 0.8f, y0 + h * u, z + cosf(u * 2 + i) * 0.4f};
            if (p.y > -1) break;
            float w = 0.6f;
            TwoSided(mb, Vector3Add(prev, {0, 0, -w}), Vector3Add(p, {0, 0, -w}), Vector3Add(p, {0, 0, w}), g);
            TwoSided(mb, Vector3Add(prev, {0, 0, -w}), Vector3Add(p, {0, 0, w}), Vector3Add(prev, {0, 0, w}), g);
            prev = p;
        }
    }
    for (int i = 0; i < 900; i++) {
        float x = -298 + Hash((float)i, 11) * 125, z = mf::Z0 + Hash((float)i, 13) * (mf::Z1 - mf::Z0);
        if (Noise(x / 9, z / 9) * 0.7f + Noise(x / 3.5f + 7, z / 3.5f) * 0.3f >= 0.3f) continue;   // (only on the patches the floor paints green)
        float y0 = mf::FloorY(x, z), h = 0.6f + Hash((float)i, 15) * 1.2f;
        Color g{84, 150, 76, 255};
        TwoSided(mb, {x - 0.06f, y0, z}, {x + 0.06f, y0, z}, {x + 0.1f, y0 + h, z + 0.15f}, g);
    }
    S.kelp = LoadModelFromMesh(mb.Build());
}
void BuildProps() {
    // the pier's pilings in the shallows, a wreck on the reef, the cleaning station's rock, vents and the brine pool's rim
    rt::MeshBuilder mb;
    Color wood{96, 80, 60, 255}, hull{70, 60, 52, 255};
    for (int i = 0; i < 10; i++) for (int s = 0; s < 2; s++) {
        float x = -270 + i * 6.0f, z = -40 + s * 5.0f, y0 = mf::FloorY(x, z);
        mb.Tube({{x, y0 - 0.5f, z}, {x, 1.5f, z}}, 0.35f, 0.3f, 6, wood, Shade(wood, 1.2f), 0);
    }
    { // the wreck: a broken hull on its side
        Vector3 c{-110, mf::FloorY(-110, -70) + 1.5f, -70};
        mb.Lathe(16, 8, 10, [](float u) { return 2.6f * sinf(std::max(0.1f, u) * PI) + 0.4f; }, [](float u) { return 2.0f * sinf(std::max(0.1f, u) * PI) + 0.3f; }, hull, Shade(hull, 0.7f), c);
        mb.Tube({Vector3Add(c, {0, 2, 2}), Vector3Add(c, {3, 9, 1})}, 0.3f, 0.2f, 5, wood, wood, 0);
    }
    { // the cleaning station: a flat-topped rock
        Vector3 c = mf::CLEANING; c.y = mf::FloorY(c.x, c.z);
        mb.Lathe(3, 3, 9, [](float u) { return 4.5f - u * 1.5f; }, [](float u) { return 4.5f - u * 1.5f; }, {150, 140, 130, 255}, {110, 100, 96, 255}, Vector3Add(c, {0, 1.2f, 0}));
    }
    for (int i = 0; i < 6; i++) {   // vents in the trench
        float x = 150 + Hash((float)i, 21) * 130, z = -50 + Hash((float)i, 23) * 100, y0 = mf::FloorY(x, z);
        mb.Cone({x, y0 - 1, z}, {x, y0 + 5 + Hash((float)i, 25) * 4, z}, 1.6f, 7, {60, 52, 50, 255});
    }
    S.props = LoadModelFromMesh(mb.Build());
}
void FreeModels() {
    if (!S.ready) return;
    for (Model& m : S.floor) UnloadModel(m);
    S.floor.clear();
    UnloadModel(S.coral); UnloadModel(S.kelp); UnloadModel(S.props);
    S.ready = false;
}
void EnsureModels() {
    if (S.ready || !IsWindowReady()) return;
    BuildFloor(); BuildCoral(); BuildKelp(); BuildProps();
    S.ready = true;
}

// ---------------------------------------------------------------- input
mf::Mouth& Me() { return WD().mouths[std::clamp(S.me, 0, (int)WD().mouths.size() - 1)]; }
void Gather(float dt) {
    mf::Mouth& m = Me();
    mf::Input in;
    if (IsKeyPressed(KEY_H)) S.help = !S.help;
    S.board = IsKeyDown(KEY_TAB) || (WD().HighTide() && !WD().over);   // (the final minute pins the food chain on screen)
    Vector2 md = MouseLook(!S.shot && m.alive && !WD().over && !m.pendingFork);
    S.aimYaw += md.x * 0.0026f;
    S.aimPitch = std::clamp(S.aimPitch - md.y * 0.0026f, -1.35f, 1.35f);
    if (IsKeyDown(KEY_A)) S.aimYaw -= dt * 1.4f;
    if (IsKeyDown(KEY_D)) S.aimYaw += dt * 1.4f;
    if (IsKeyDown(KEY_SPACE)) S.aimPitch = std::min(1.35f, S.aimPitch + dt * 1.3f);
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_C)) S.aimPitch = std::max(-1.35f, S.aimPitch - dt * 1.3f);
    in.yaw = S.aimYaw; in.pitch = S.aimPitch;
    in.swim = IsKeyDown(KEY_W);
    in.brake = IsKeyDown(KEY_S);
    in.boost = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    in.bite = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
    in.ability = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || IsKeyPressed(KEY_E) || IsKeyPressed(KEY_Q);
    if (m.pendingFork) for (int k = 0; k < (int)m.forkOpts.size() && k < 9; k++) if (IsKeyPressed(KEY_ONE + k)) in.fork = k;
    if (S.forkClick >= 0) { if (m.pendingFork) in.fork = S.forkClick; S.forkClick = -1; }
    // a walker jumps with Space
    const mf::FormDef& F = WD().FormOf(m);
    if (F.walker && IsKeyPressed(KEY_SPACE)) in.pitch = 1.2f;
    if (S.shot) { in = mf::Input{}; in.yaw = S.aimYaw; in.pitch = S.aimPitch; }
    m.in = in;
}

// ---------------------------------------------------------------- drawing
float CreatureYaw(Vector3 f) { return atan2f(f.x, f.z); }
Color Mul(Color a, Color b) { return {(unsigned char)(a.r * b.r / 255), (unsigned char)(a.g * b.g / 255), (unsigned char)(a.b * b.b / 255), 255}; }
void DrawSkinExtras(const mf::SkinDef& sk, Vector3 pos, Vector3 f, float L, float t) {
    // a skin's pattern, glow and hat, placed along the body: the head forward, the back up
    Vector3 up{0, 1, 0}, side = Vector3Normalize(Vector3CrossProduct(f, up));
    if (Vector3Length(side) < 0.1f) side = {1, 0, 0};
    up = Vector3Normalize(Vector3CrossProduct(side, f));
    auto at = [&](float along, float upK, float sideK) { return Vector3Add(pos, Vector3Add(Vector3Scale(f, along * L), Vector3Add(Vector3Scale(up, upK * L), Vector3Scale(side, sideK * L)))); };
    auto box = [&](Vector3 c, float s, Color col) { rt::DrawWorldCube(c, {s, s, s}, col); };
    auto glow = [&](Vector3 c, float s, Color col, float k) { rt::DrawCubeGlow(MatrixMultiply(MatrixScale(s, s, s), MatrixTranslate(c.x, c.y, c.z)), col, k); };
    const std::string& p = sk.pattern;
    if (p == "stripes") for (int k = 0; k < 4; k++) box(at(0.25f - k * 0.16f, 0.12f, 0), L * 0.06f, {30, 30, 34, 255});
    if (p == "spots" || p == "mottle") for (int k = 0; k < 6; k++) box(at(0.2f - k * 0.08f, 0.1f + 0.04f * (k % 2), (k % 3 - 1) * 0.08f), L * 0.04f, p == "spots" ? Color{40, 34, 30, 255} : Color{120, 90, 60, 255});
    if (p == "orca") { box(at(0.22f, 0.06f, 0.1f), L * 0.07f, {240, 240, 236, 255}); box(at(0.22f, 0.06f, -0.1f), L * 0.07f, {240, 240, 236, 255}); box(at(-0.05f, -0.1f, 0), L * 0.09f, {240, 240, 236, 255}); }
    if (p == "scales" || p == "blocks") for (int k = 0; k < 5; k++) box(at(0.15f - k * 0.1f, 0.13f, 0), L * 0.05f, p == "blocks" ? Color{250, 210, 60, 255} : Color{50, 120, 70, 255});
    if (sk.hasGlow) for (int k = 0; k < 7; k++) glow(at(0.3f - k * 0.09f, 0.05f + 0.06f * sinf(k * 2.1f), 0.12f * cosf(k * 1.7f)), L * 0.025f + 0.01f, sk.glow, 1.2f + 0.4f * sinf(t * 3 + k));
    const std::string& h = sk.hat;
    if (h == "bicorne") { Vector3 c = at(0.28f, 0.2f, 0); rt::DrawWorldCube(c, {L * 0.1f, L * 0.08f, L * 0.32f}, {24, 22, 26, 255}); box(Vector3Add(c, Vector3Scale(up, L * 0.04f)), L * 0.03f, {220, 180, 80, 255}); }
    if (h == "crown") for (int k = 0; k < 5; k++) { float a = k * 1.2566f; glow(Vector3Add(at(0.25f, 0.2f, 0), Vector3Add(Vector3Scale(side, cosf(a) * L * 0.06f), Vector3Scale(f, sinf(a) * L * 0.06f))), L * 0.035f, {255, 214, 90, 255}, 1.2f); }
    if (h == "toque") { Vector3 c = at(0.25f, 0.22f, 0); rt::DrawWorldCube(Vector3Add(c, Vector3Scale(up, L * 0.06f)), {L * 0.1f, L * 0.16f, L * 0.1f}, {246, 246, 240, 255}); }
    if (h == "monocle") glow(at(0.36f, 0.06f, 0.12f), L * 0.035f, {250, 240, 200, 255}, 0.8f);
    if (h == "bigeyes") { box(at(0.36f, 0.08f, 0.11f), L * 0.07f, {250, 250, 250, 255}); box(at(0.36f, 0.08f, -0.11f), L * 0.07f, {250, 250, 250, 255}); }
    if (h == "eyepatch") box(at(0.36f, 0.06f, 0.12f), L * 0.05f, {20, 20, 22, 255});
    if (h == "key") { Vector3 c = at(-0.05f, 0.2f, 0); rt::DrawWorldCube(c, {L * 0.03f, L * 0.12f, L * 0.03f}, {200, 170, 80, 255}); rt::DrawWorldCube(Vector3Add(c, Vector3Scale(up, L * 0.08f)), {L * 0.02f, L * 0.06f, L * 0.12f}, {200, 170, 80, 255}); }
    if (h == "streamers") for (int k = 0; k < 3; k++) { Color c3[3] = {{230, 60, 50, 255}, {250, 250, 250, 255}, {60, 90, 220, 255}}; Vector3 c = at(-0.45f - 0.1f * k, 0.05f * sinf(t * 4 + k), 0.08f * (k - 1)); rt::DrawWorldCube(c, {L * 0.05f, L * 0.02f, L * 0.05f}, c3[k]); }
    if (h == "chain") for (int k = 0; k < 4; k++) box(at(-0.45f - k * 0.08f, -0.05f, 0), L * 0.03f, {120, 120, 126, 255});
    if (h == "rivets") for (int k = 0; k < 6; k++) box(at(0.2f - k * 0.08f, 0.14f, (k % 2 ? 0.06f : -0.06f)), L * 0.025f, {170, 140, 70, 255});
    if (h == "ribs") for (int k = 0; k < 5; k++) rt::DrawWorldCube(at(0.15f - k * 0.08f, 0.0f, 0), {L * 0.02f, L * 0.24f, L * 0.22f}, {236, 232, 220, 255});
    if (h == "gems") for (int k = 0; k < 4; k++) glow(at(0.2f - k * 0.12f, 0.14f, 0), L * 0.03f, k % 2 ? Color{90, 220, 140, 255} : Color{255, 80, 140, 255}, 1.4f);
    if (h == "horns") { rt::DrawWorldCube(at(0.28f, 0.2f, 0.07f), {L * 0.03f, L * 0.12f, L * 0.03f}, {230, 210, 170, 255}); rt::DrawWorldCube(at(0.28f, 0.2f, -0.07f), {L * 0.03f, L * 0.12f, L * 0.03f}, {230, 210, 170, 255}); }
    if (h == "mask") glow(at(0.42f, 0.05f, 0), L * 0.09f, {255, 200, 80, 255}, 0.9f);
    if (h == "rice") rt::DrawWorldCube(at(0, 0.16f, 0), {L * 0.24f, L * 0.06f, L * 0.12f}, {250, 248, 240, 255});
    if (h == "box") rt::DrawWorldCube(pos, {L * 0.55f, L * 0.4f, L * 0.4f}, {196, 156, 104, 255});
    if (h == "sub") rt::DrawWorldCube(at(0, 0.2f, 0), {L * 0.12f, L * 0.08f, L * 0.3f}, {110, 116, 108, 255});
    if (h == "lamp") { rt::DrawWorldCube(at(0.32f, 0.22f, 0), {L * 0.015f, L * 0.12f, L * 0.015f}, {60, 60, 60, 255}); glow(at(0.4f, 0.3f, 0), L * 0.05f, {255, 240, 160, 255}, 2.0f); }
    if (h == "scar") glow(at(0.1f, 0.08f, 0.12f), L * 0.02f, {255, 90, 70, 255}, 1.0f);
    if (h == "fishhat") rt::DrawCreature(rt::Creature("mouthful_reef", "Snapper"), at(0.18f, 0.24f, 0), atan2f(f.x, f.z), 0, L * 0.35f / std::max(0.05f, rt::Creature("mouthful_reef", "Snapper").length), t * 3, 0.3f, WHITE);
}
void DrawMouth(const mf::World& w, const mf::Mouth& m, bool mine) {
    if (!m.alive) return;
    const mf::FormDef& F = w.FormOf(m);
    const mf::SkinDef* sk = mf::WornSkin(m.look, m.path);
    const rt::CreatureModel& cm = rt::Creature("mouthful_reef", sk && sk->special == "sharksuit" ? (m.tier >= 6 ? "Great White" : "Bull Shark") : F.art);   // (the Shark Suit: the panic is real)
    float L = w.Length(m);
    Vector3 f{cosf(m.pitch) * cosf(m.yaw), sinf(m.pitch), cosf(m.pitch) * sinf(m.yaw)};
    float spd = Vector3Length(m.vel);
    float inten = std::clamp(0.4f + spd / std::max(1.0f, w.Speed(m)), 0.3f, 1.8f);
    if (m.dashT > 0 || m.boosting) inten = 1.8f;
    Color tint = WHITE;
    if (m.hurtT > 0) tint = {255, 150, 140, 255};
    if (m.immuneT > 0) tint = Mix(WHITE, Color{255, 250, 200, 255}, 0.5f + 0.5f * sinf(S.t * 10));   // (the immunity glow)
    if (m.morphT > 0) tint = Mix(tint, Color{180, 255, 240, 255}, m.morphT);                        // (a fork's shimmer)
    // the tells (doc p. 15): eels coil before a dash, squid go pale before ink, sharks drop their pectorals (a dip of the
    // nose), crabs raise a claw (the front lifts), puffers draw in water (a swell); the blobfish has none
    float tellK = m.tellT > 0 ? m.tellT / 0.35f : 0, pitchAdd = 0, swell = 1, coil = 1;
    if (tellK > 0) switch (F.path) {
        case mf::P_EEL: coil = 1 + 2.2f * tellK; break;
        case mf::P_CEPH: tint = Mix(tint, Color{250, 248, 244, 255}, 0.75f * tellK); break;
        case mf::P_SHARK: pitchAdd = -0.35f * tellK; break;
        case mf::P_CRUST: pitchAdd = 0.4f * tellK; break;
        case mf::P_PUFFER: swell = 1 + 0.25f * tellK; break;
        default: break;
    }
    if (m.buriedT >= 2 || m.ambush) tint = Mix(tint, Color{120, 110, 90, 255}, 0.6f);
    if (F.ps == mf::PS_CAMO && m.stillT > 1) tint = Mix(tint, Color{150, 140, 120, 255}, 0.7f);
    if (sk && sk->hasTint) tint = Mul(tint, sk->tint);
    if (sk && sk->pattern == "sheen") tint = Mix(tint, WHITE, 0.25f + 0.2f * sinf(S.t * 2 + m.id));
    if (sk && sk->pattern == "pale") tint = Mix(tint, Color{220, 236, 244, 255}, 0.5f);
    float scale = L / std::max(0.05f, cm.length) * swell;
    if (F.ab == mf::AB_INFLATE && m.abT > 0) scale *= 1.0f;   // (Length already doubles it)
    float phase = S.t * cm.freq * (0.5f + inten * 0.5f) * coil + m.id * 1.7f;
    rt::DrawCreature(cm, m.pos, CreatureYaw(f), std::clamp(m.pitch + pitchAdd, -1.3f, 1.3f), scale, phase, std::min(2.0f, inten * coil), tint);
    if (sk) DrawSkinExtras(*sk, m.pos, f, L, S.t + m.id);
    // a burrowed crab or an ambushing stonefish shows only as a little mound of sand to a sharp eye
    if (m.buriedT >= 2 || m.ambush) { Vector3 p = m.pos; p.y = mf::FloorY(p.x, p.z) + 0.05f; rt::DrawWorldCube(p, {L * 0.7f, L * 0.12f, L * 0.7f}, mf::BandAt(p) == mf::B_SHALLOWS ? Color{228, 214, 170, 255} : Color{170, 150, 130, 255}); }
    if (m.immuneT > 0) for (int k = 0; k < 6; k++) { float a = k * 1.047f + S.t * 2.5f, r = L * 0.8f + 0.06f; rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.025f, 0.025f, 0.025f), MatrixTranslate(m.pos.x + cosf(a) * r, m.pos.y + sinf(a * 2) * r * 0.3f, m.pos.z + sinf(a) * r)), {255, 240, 170, 255}, 1.0f); }
    if (m.king) {   // the crown: a gold glow over the king, seen by everyone
        Vector3 c = Vector3Add(m.pos, {0, L * 0.45f + 0.2f, 0});
        for (int k = 0; k < 5; k++) { float a = k * 1.2566f + S.t; rt::DrawCubeGlow(MatrixMultiply(MatrixScale(L * 0.06f, L * 0.12f, L * 0.06f), MatrixTranslate(c.x + cosf(a) * L * 0.12f, c.y, c.z + sinf(a) * L * 0.12f)), {255, 214, 90, 255}, 1.6f); }
    }
    (void)mine;
}
void DrawWorld(const mf::World& w, const Camera3D& cam) {
    for (size_t i = 0; i < S.floor.size(); i++) rt::DrawStatic(S.floor[i], MatrixIdentity());
    rt::DrawStatic(S.coral, MatrixIdentity());
    rt::DrawStatic(S.kelp, MatrixIdentity());
    rt::DrawStatic(S.props, MatrixIdentity());
    // the surface seen from below: a bright skin of light
    rt::DrawCubeGlow(MatrixMultiply(MatrixScale(900, 0.2f, 600), MatrixTranslate(0, 0.15f, 0)), {150, 214, 222, 255}, 0.5f);
    // the brine pool's sheen and the vents' glow in the trench
    rt::DrawCubeGlow(MatrixMultiply(MatrixScale(mf::BRINE_R * 1.7f, 0.1f, mf::BRINE_R * 1.7f), MatrixTranslate(mf::BRINE.x, mf::FloorY(mf::BRINE.x, mf::BRINE.z) + 0.6f, mf::BRINE.z)), {40, 70, 90, 255}, 0.4f);
    for (int i = 0; i < 6; i++) {   // the vents' glow
        float x = 150 + Hash((float)i, 21) * 130, z = -50 + Hash((float)i, 23) * 100, y0 = mf::FloorY(x, z) + 5.5f + Hash((float)i, 25) * 4;
        if (Vector3Distance({x, y0, z}, cam.position) < 120) for (int j = 0; j < 16; j++) { float u = fmodf(S.t * 0.4f + j / 16.0f, 1.0f); rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.2f, 0.2f, 0.2f), MatrixTranslate(x + sinf(j * 2.1f + S.t) * u * 1.5f, y0 + u * 9, z + cosf(j * 1.7f) * u * 1.5f)), {255, 150, 80, 255}, 1.6f * (1 - u)); }
    }
    if (cam.position.y < -110) for (int k = 0; k < 90; k++) {   // bioluminescent motes drifting round the eye
        float a = k * 2.399f, r = 4 + (k % 9) * 2.2f, y = ((k * 37) % 23 - 11) * 1.1f;
        Vector3 q{cam.position.x + cosf(a + S.t * 0.05f) * r, cam.position.y + y + sinf(S.t * 0.3f + k) * 0.5f, cam.position.z + sinf(a + S.t * 0.05f) * r};
        rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.05f, 0.05f, 0.05f), MatrixTranslate(q.x, q.y, q.z)), k % 3 ? Color{90, 220, 230, 255} : Color{140, 255, 170, 255}, 1.2f);
    }
    // plankton: a fry's food, a shimmer of green motes
    for (const auto& p : w.plankton) {
        if (Vector3Distance(p.pos, cam.position) > 80) continue;
        for (int k = 0; k < 26; k++) {
            float a = k * 2.399f, r = p.r * sqrtf((k + 0.5f) / 26), y = (Hash((float)k, p.r) - 0.5f) * p.r;
            Vector3 q{p.pos.x + cosf(a + S.t * 0.2f) * r, p.pos.y + y, p.pos.z + sinf(a + S.t * 0.2f) * r};
            rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.08f, 0.08f, 0.08f), MatrixTranslate(q.x, q.y, q.z)), {170, 236, 170, 255}, 0.9f);
        }
    }
    // ink and toxin clouds
    for (const auto& c : w.clouds) {
        float k = std::clamp(c.t, 0.0f, 1.0f);
        Color col = c.kind == 0 ? Color{18, 14, 30, 255} : Color{150, 190, 60, 255};
        if (c.kind == 0 && c.owner >= 0 && c.owner < (int)w.mouths.size()) { const mf::SkinDef* os = mf::WornSkin(w.mouths[c.owner].look, w.mouths[c.owner].path); if (os && os->special == "purpleink") col = {70, 24, 100, 255}; }   // (Kraken Ink)
        // a billow of puffs: dense in the middle, thinning at the edge, swelling as it spreads
        for (int j = 0; j < 70; j++) {
            float a = j * 2.399f, rr = c.r * sqrtf((j + 0.5f) / 70) * (0.7f + 0.3f * (1 - k)), y = sinf(j * 1.3f) * c.r * 0.45f;
            Vector3 q{c.pos.x + cosf(a) * rr, c.pos.y + y, c.pos.z + sinf(a) * rr};
            float s = c.r * 0.22f * (1.2f - (float)j / 70) * std::max(0.3f, k);
            rt::DrawWorldCube(q, {s, s, s}, col);
        }
    }
    // the web's fish (near enough to see) and the leviathan
    for (int i = 0; i < (int)w.eco.agents.size(); i++) {
        const rt::Agent& a = w.eco.agents[i];
        if (!a.alive || a.diver >= 0) continue;
        float d = Vector3Distance(a.pos, cam.position);
        const rt::Species& sp = w.eco.map->species[a.sp];
        if (d > (i == w.leviathan ? 160.0f : sp.size >= 5 ? 110.0f : 70.0f)) continue;
        const rt::CreatureModel& cm = rt::Creature("mouthful_reef", sp.name);
        float spd = Vector3Length(a.vel);
        Vector3 v = spd > 0.02f ? a.vel : Vector3{sinf(a.rng * 0.001f), 0, cosf(a.rng * 0.001f)};
        float yaw = atan2f(v.x, v.z), pitch = spd > 0.05f ? std::clamp(asinf(std::clamp(v.y / std::max(spd, 1e-3f), -1.0f, 1.0f)), -0.6f, 0.6f) : 0;
        float inten = std::clamp(0.35f + spd / std::max(0.5f, sp.speed), 0.2f, 1.6f);
        Color tint = a.wound > 0.3f ? Color{255, (unsigned char)(255 - a.wound * 120), (unsigned char)(255 - a.wound * 120), 255} : WHITE;
        if (i == w.leviathan && w.levAwakeT <= 0) tint = Shade(tint, 0.7f);
        rt::DrawCreature(cm, a.pos, yaw, pitch, 1.0f, S.t * cm.freq * (0.6f + inten * 0.6f) + (a.rng % 1000) * 0.01f, inten, tint);
    }
    // ---- the dangers that aren't players
    const mf::Boat& B = w.boat;
    if (B.on) {
        // the boat: a dark hull on the bright skin of the surface, its shadow sliding over the floor below
        Matrix hull = MatrixMultiply(MatrixMultiply(MatrixScale(3.2f, 1.4f, 9), MatrixRotateY(0)), MatrixTranslate(B.pos.x, 0.2f, B.pos.z));
        rt::DrawCubeM(hull, {40, 36, 34, 255});
        rt::DrawWorldCube({B.pos.x, 0.4f, B.pos.z - B.dirZ * 4.8f}, {0.6f, 1.2f, 0.6f}, {30, 28, 28, 255});   // (the screw's housing)
        float fy = mf::FloorY(B.pos.x, B.pos.z);
        rt::DrawWorldCube({B.pos.x, fy + 0.05f, B.pos.z}, {4.5f, 0.05f, 11}, {20, 30, 34, 255});
        if (B.net) {   // the net: a curtain of mesh trailing behind and below
            Vector3 c = B.NetCentre();
            for (int i = -5; i <= 5; i++) rt::DrawWorldCube({c.x + i * 2.0f, -6.5f, c.z}, {0.05f, 13, 0.05f}, {210, 210, 190, 255});
            for (int j = 0; j < 7; j++) rt::DrawWorldCube({c.x, -1 - j * 2.0f, c.z}, {20, 0.05f, 0.05f}, {210, 210, 190, 255});
            rt::DrawWorldCube({c.x, -0.5f, (c.z + B.pos.z) * 0.5f}, {0.04f, 0.04f, fabsf(c.z - B.pos.z)}, {200, 200, 180, 255});
        }
        for (const auto& hk : B.hookList) {   // the hooks: a line down from the surface, a bait fish, a glint
            if (hk.gone) continue;
            rt::DrawWorldCube({hk.pos.x, hk.pos.y * 0.5f, hk.pos.z}, {0.02f, -hk.pos.y, 0.02f}, {220, 220, 220, 255});
            rt::DrawCreature(rt::Creature("mouthful_reef", "Sardine"), Vector3Add(hk.pos, {0, -0.1f, 0}), 1.57f, 0, 1.2f, S.t * 3, 0.4f, WHITE);
            rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.05f, 0.12f, 0.05f), MatrixTranslate(hk.pos.x, hk.pos.y - 0.25f, hk.pos.z)), {255, 250, 220, 255}, 1.0f + 0.6f * sinf(S.t * 7));
        }
        if (B.chum) for (int k = 0; k < 24; k++) {   // the chum line: red clouds in the wake
            float zz = B.pos.z - B.dirZ * (4 + k * 1.2f);
            Vector3 q{B.pos.x + sinf(k * 1.7f + S.t) * 1.5f, -1.0f - (k % 5) * 0.6f, zz};
            rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.5f, 0.3f, 0.5f), MatrixTranslate(q.x, q.y, q.z)), {170, 40, 30, 255}, 0.3f);
        }
    }
    if (w.bloom.on) for (int k = 0; k < 420; k++) {   // the red tide: a rust-red bloom of motes hanging in the water
        float a = k * 2.399f, r = w.bloom.r * sqrtf((k + 0.5f) / 420);
        Vector3 q{w.bloom.pos.x + cosf(a + S.t * 0.02f) * r, -1 - (k % 13) * 1.7f + sinf(S.t * 0.4f + k) * 0.4f, w.bloom.pos.z + sinf(a + S.t * 0.02f) * r};
        if (Vector3Distance(q, cam.position) < 50) rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.12f, 0.12f, 0.12f), MatrixTranslate(q.x, q.y, q.z)), {190, 60, 40, 255}, 0.4f);
    }
    if (w.fall.on || w.fall.done) {   // the whale fall: a carcass on the trench floor (its ribs bare as it's eaten)
        Vector3 c = w.fall.pos;
        if (Vector3Distance(c, cam.position) < 160) {
            float k = std::clamp(w.fall.left / 300, 0.0f, 1.0f);
            rt::DrawCreature(rt::Creature("mouthful_reef", "Orca"), c, 0.4f, 0, 2.4f, 0, 0, Color{(unsigned char)(150 + 60 * k), (unsigned char)(140 + 50 * k), (unsigned char)(140 + 40 * k), 255});
            for (int r = 0; r < 9; r++) rt::DrawWorldCube({c.x - 6 + r * 1.5f, c.y + 1.5f, c.z}, {0.25f, 3.5f, 0.25f}, {226, 220, 204, 255});
            // the scavengers' light: a swarm of glowing motes over the carcass, so it can be found in the dark
            for (int j = 0; j < 60; j++) { float a = j * 2.399f + S.t * 0.3f, r = 2 + (j % 7) * 1.1f; rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.06f, 0.06f, 0.06f), MatrixTranslate(c.x + cosf(a) * r, c.y + 1 + (j % 5) * 0.8f, c.z + sinf(a) * r)), j % 3 ? Color{120, 230, 220, 255} : Color{200, 255, 180, 255}, 1.4f); }
        }
    }
    {   // the eel garden: little heads poking up out of the sand
        float gy = mf::FloorY(mf::EEL_GARDEN.x, mf::EEL_GARDEN.z);
        if (Vector3Distance({mf::EEL_GARDEN.x, gy, mf::EEL_GARDEN.z}, cam.position) < 70) for (int k = 0; k < 28; k++) {
            float a = k * 2.399f, r = mf::EEL_GARDEN_R * sqrtf((k + 0.5f) / 28);
            float x = mf::EEL_GARDEN.x + cosf(a) * r, z = mf::EEL_GARDEN.z + sinf(a) * r, y = mf::FloorY(x, z);
            float h = 0.35f + 0.2f * sinf(S.t * 1.3f + k);
            rt::DrawWorldCube({x, y + h * 0.5f, z}, {0.06f, h, 0.06f}, {214, 196, 120, 255});
            rt::DrawWorldCube({x + 0.03f, y + h, z}, {0.09f, 0.07f, 0.07f}, {200, 180, 100, 255});
        }
    }
    // the corpses: a pale drifting body
    for (const auto& c : w.eco.corpses) if (c.active && Vector3Distance(c.pos, cam.position) < 50) rt::DrawWorldCube(c.pos, {0.25f, 0.12f, 0.4f}, {200, 190, 180, 255});
    for (const auto& m : w.mouths) DrawMouth(w, m, m.id == S.me);
}

// ---------------------------------------------------------------- the light under water
void Light(rt::SceneLight& L, const Camera3D& cam, float dusk) {
    float depth = std::max(0.0f, -cam.position.y);
    int band = mf::BandAt(cam.position);
    Color shallow{70, 176, 186, 255}, blue{26, 92, 140, 255}, deep{10, 26, 46, 255}, trench{8, 16, 32, 255};
    Color fog = depth < 15 ? Mix(shallow, blue, depth / 60) : depth < 120 ? Mix(Mix(shallow, blue, 0.25f), deep, (depth - 15) / 105) : Mix(deep, trench, std::min(1.0f, (depth - 120) / 120));
    if (band == mf::B_SHALLOWS) fog = Mix(fog, Color{90, 190, 186, 255}, 0.4f);
    fog = Mix(fog, Color{14, 20, 40, 255}, dusk * 0.6f);
    const mf::World& W = WD();
    if (W.bloom.on && Vector2Distance({cam.position.x, cam.position.z}, {W.bloom.pos.x, W.bloom.pos.z}) < W.bloom.r) fog = Mix(fog, Color{120, 40, 30, 255}, 0.6f);
    L.fog = fog;
    L.fogDensity = band == mf::B_TRENCH ? 0.05f : band == mf::B_BLUE ? 0.022f : 0.026f;
    L.water = 1; L.surf = 3;
    L.absorb = depth < 45 ? Vector3{0.045f, 0.020f, 0.016f} : Vector3{0.075f, 0.030f, 0.024f};   // (the reef keeps its colour)
    L.depthDark = 0.010f; L.surfaceY = 0;
    L.causticK = depth < 40 ? 0.9f * (1 - depth / 40) : 0;
    L.bloom = 0.35f; L.lens = 0; L.inkFade = 1;
    L.outline = 0.6f; L.outlineTint = {(unsigned char)(fog.r * 0.35f), (unsigned char)(fog.g * 0.35f), (unsigned char)(fog.b * 0.35f), 255};
    L.stipple = 0; L.grain = 0.35f;
    L.aoK = 0.45f; L.aoRadius = 0.5f; L.filmic = 0.5f; L.exposure = 1.1f; L.saturation = 1.15f;
    float sun = std::clamp(expf(-depth * 0.025f), 0.1f, 1.0f) * (1 - 0.6f * dusk);
    L.moonDir = Vector3Normalize({0.2f, -1.0f, 0.15f});
    L.moon = {200, 236, 240, 255}; L.moonK = 0.85f * sun;
    L.ambK = 0.55f; L.skyAmb = Mix(fog, WHITE, 0.4f); L.seaAmb = Shade(fog, 0.6f);
    L.fill = Shade(fog, 0.6f); L.rim = Mix(fog, WHITE, 0.3f);
    // in the dark: a faint glow round your own eyes (the trench is lit only by what lives there)
    L.lampPos = cam.position; L.lampDir = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    L.lampRange = depth > 110 ? 30 : 1; L.key = depth > 110 ? Color{110, 170, 190, 255} : Color{0, 0, 0, 255};
    if (depth > 110) { L.fill = {30, 54, 70, 255}; L.rim = {60, 120, 140, 255}; L.ambK = 0.8f; }
    L.time = S.t;
    // shafts from the surface on a fixed grid round the eye
    if (depth < 60) {
        float cell = 11;
        int cx = (int)floorf(cam.position.x / cell), cz = (int)floorf(cam.position.z / cell);
        Color sc{220, 250, 240, 255};
        for (int dz = -1; dz <= 1 && L.nShafts < rt::SceneLight::MAX_SHAFTS; dz++) for (int dx = -1; dx <= 1 && L.nShafts < rt::SceneLight::MAX_SHAFTS; dx++) {
            uint32_t h = (uint32_t)(cx + dx) * 73856093u ^ (uint32_t)(cz + dz) * 19349663u;
            if ((h >> 7) % 3 == 0) continue;
            float ox = ((h >> 11) % 100) / 100.0f, oz = ((h >> 17) % 100) / 100.0f;
            L.AddShaft({(cx + dx + ox) * cell, 0, (cz + dz + oz) * cell}, Vector3Normalize({0.2f, -1.0f, 0.15f}), 0.7f + 0.6f * ox, 40, sc, 0.18f * sun);
        }
    }
}
void Render(float dt) {
    mf::World& w = WD();
    mf::Mouth& m = Me();
    EnsureModels();
    // the camera: behind the mouth along the aim; it pulls back as you grow (a king sees the reef, a fry the next rock)
    float k = std::min(1.0f, dt * 6);
    S.camYaw += atan2f(sinf(S.aimYaw - S.camYaw), cosf(S.aimYaw - S.camYaw)) * k;
    S.camPitch += (S.aimPitch * 0.85f - S.camPitch) * k;
    float L = w.Length(m);
    float want = 0.7f + L * 3.0f + (m.boosting ? L * 0.6f : 0);
    S.camDist += (want - S.camDist) * std::min(1.0f, dt * 2);
    Vector3 look{cosf(S.camPitch) * cosf(S.camYaw), sinf(S.camPitch), cosf(S.camPitch) * sinf(S.camYaw)};
    Vector3 focus = m.alive ? m.pos : Vector3{-250, -4, 0};
    Vector3 eye = Vector3Add(Vector3Subtract(focus, Vector3Scale(look, S.camDist)), {0, 0.25f + L * 0.5f, 0});
    eye.y = std::clamp(eye.y, mf::FloorY(eye.x, eye.z) + 0.4f, -0.25f);
    S.cam.position = eye;
    S.cam.target = Vector3Add(focus, Vector3Scale(look, 2 + L));
    S.cam.up = {0, 1, 0};
    S.cam.fovy = 64 + (m.boosting ? 6 : 0);
    S.cam.projection = CAMERA_PERSPECTIVE;
    float dusk = w.Dusk();
    rt::SceneLight Lt;
    Light(Lt, S.cam, dusk);
    rt::ApplyGameQuality();
    rt::RenderBegin(S.cam, Lt);
    DrawWorld(w, S.cam);
    rt::RenderEnd();
}

// ---------------------------------------------------------------- the HUD
void Bar(float x, float y, float wd, float h, float k, Color c) {
    DrawRectangleRounded({x, y, wd, h}, 0.5f, 6, Fade(Color{4, 20, 28, 255}, 0.75f));
    if (k > 0) DrawRectangleRounded({x + 2, y + 2, std::max(2.0f, (wd - 4) * std::clamp(k, 0.0f, 1.0f)), h - 4}, 0.5f, 6, c);
}
void DrawHud(mf::World& w) {
    const mf::Data& d = mf::D();
    mf::Mouth& m = Me();
    const mf::FormDef& F = w.FormOf(m);
    Color ink{236, 250, 246, 255}, dim{170, 210, 210, 255}, gold{255, 214, 110, 255};
    // the clock and where you are
    int left = std::max(0, (int)(w.roundLen - w.time));
    bool high = w.time > w.roundLen - 60;
    DrawTextCenteredBold(TextFormat("%d:%02d%s", left / 60, left % 60, high ? "  HIGH TIDE" : w.time > w.roundLen * 2 / 3 ? "  dusk" : ""), SCREEN_W / 2.0f, 12, 24, high ? gold : ink);
    if (m.alive) Txt(TextFormat("%s, %.0f m", mf::BandName(mf::BandAt(m.pos)), -m.pos.y), 18, 14, 16, dim);
    {
        std::string warn;
        if (w.orcas.on && w.orcas.t > 0) warn = TextFormat("THE ORCA POD hunts the three biggest mouths (%.0f s)", w.orcas.t);
        else if (w.levAwakeT > 0) warn = "THE LEVIATHAN IS AWAKE";
        else if (w.fall.on) warn = TextFormat("A whale fall in the trench: %.0f mass of feast left", w.fall.left);
        else if (w.bloom.on) warn = TextFormat("Red tide over the reef (%.0f s)", w.bloom.t);
        else if (w.boat.on) warn = std::string("A boat overhead") + (w.boat.net ? ": a net" : "") + (w.boat.hooks ? ": hooks" : "") + (w.boat.chum ? ": a chum line" : "");
        if (!warn.empty()) DrawTextCenteredBold(warn, SCREEN_W / 2.0f, 42, 16, w.orcas.on || w.levAwakeT > 0 ? Color{255, 150, 130, 255} : Color{220, 236, 255, 255});
    }
    // the leaderboard (the top six and you), the crown beside the king
    {
        auto b = w.Board();
        float x = SCREEN_W - 270, y = 12;
        DrawRectangleRounded({x - 10, y - 4, 268, 24.0f * std::min(7, (int)b.size()) + 30}, 0.08f, 6, Fade(Color{4, 20, 28, 255}, 0.6f));
        TxtBold("The food chain", x, y, 16, gold); y += 22;
        int shown = 0;
        for (int r = 0; r < (int)b.size(); r++) {
            const mf::Mouth& o = w.mouths[b[r]];
            if (shown >= 6 && o.id != S.me) continue;
            Color c = o.id == S.me ? Color{255, 236, 160, 255} : o.alive ? ink : Color{150, 160, 160, 255};
            Txt(TextFormat("%d. %s%s", r + 1, o.king ? "\xE2\x99\x94 " : "", o.name.c_str()), x, y, 15, c);
            Txt(TextFormat("t%d  %d", o.tier, (int)mf::ScoreOf(w, o)), x + 180, y, 15, c);
            y += 22; shown++;
        }
    }
    // the kill feed
    {
        float y = 210;
        int n = 0;
        for (int i = (int)w.feed.size() - 1; i >= 0 && n < 6; i--) {
            const auto& f = w.feed[i];
            float age = w.time - f.t;
            if (age > 9) break;
            Color c = f.c; c.a = (unsigned char)(255 * std::clamp(1 - (age - 7) / 2, 0.0f, 1.0f));
            int wd = MeasureTxt(f.text, 15);
            Txt(f.text, SCREEN_W - 20 - wd, y, 15, c);
            y += 20; n++;
        }
    }
    if (!m.alive) {
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.35f));
        DrawTextCenteredBold("Gulp.", SCREEN_W / 2.0f, SCREEN_H / 2.0f - 60, 40, Color{255, 190, 170, 255});
        DrawTextCentered(TextFormat("%s. Back as a fry in %.0f s.", m.lastCause == "player" ? "Something bigger found you" : m.lastCause == "leviathan" ? "The Leviathan" : m.lastCause == "npc" ? "A shark" : m.lastCause.c_str(), std::max(0.0f, m.respawnT)), SCREEN_W / 2.0f, SCREEN_H / 2.0f, 18, ink);
        return;
    }
    // the reticle, and what's in front: swallow, fight, or flee
    {
        float cx = SCREEN_W / 2.0f, cy = SCREEN_H / 2.0f;
        DrawCircleLines((int)cx, (int)cy, 7, Fade(ink, 0.7f));
        // the ability's ring
        if (F.ab != mf::AB_NONE) {
            float k = F.cd > 0 ? 1 - std::clamp(m.abCd / F.cd, 0.0f, 1.0f) : 1;
            DrawRing({cx, cy}, 13, 16, -90, -90 + 360 * k, 32, k >= 1 ? Color{140, 240, 210, 220} : Color{140, 200, 200, 120});
        }
        Vector3 f{cosf(m.pitch) * cosf(m.yaw), sinf(m.pitch), cosf(m.pitch) * sinf(m.yaw)};
        int bestM = -1, bestA = -1; float bd = 18 + w.Length(m) * 3;
        for (const auto& o : w.mouths) { if (!o.alive || o.id == m.id) continue; Vector3 dv = Vector3Subtract(o.pos, m.pos); float L = Vector3Length(dv); if (L < bd && Vector3DotProduct(Vector3Scale(dv, 1 / std::max(0.01f, L)), f) > 0.9f) { bd = L; bestM = o.id; } }
        for (int i = 0; i < (int)w.eco.agents.size(); i++) { const auto& a = w.eco.agents[i]; if (!a.alive || a.diver >= 0) continue; if (fabsf(a.pos.x - m.pos.x) > bd || fabsf(a.pos.z - m.pos.z) > bd) continue; Vector3 dv = Vector3Subtract(a.pos, m.pos); float L = Vector3Length(dv); if (L < bd && Vector3DotProduct(Vector3Scale(dv, 1 / std::max(0.01f, L)), f) > 0.95f) { bd = L; bestA = i; bestM = -1; } }
        float tm = -1; std::string tn;
        if (bestM >= 0) { tm = w.mouths[bestM].mass; tn = w.mouths[bestM].name + " (" + w.FormOf(w.mouths[bestM]).name + ")"; }
        else if (bestA >= 0) { tm = w.MassOfAgent(bestA); tn = w.eco.map->species[w.eco.agents[bestA].sp].name; }
        if (tm >= 0) {
            bool swallow = bestM >= 0 ? mf::SwallowOk(w, m, w.mouths[bestM]) : tm < d.swallowBelow * m.mass;
            bool fight = !swallow && tm < m.mass;
            const char* verdict = swallow ? "SWALLOW" : fight ? "FIGHT" : "FLEE";
            Color vc = swallow ? Color{140, 240, 150, 255} : fight ? Color{255, 220, 120, 255} : Color{255, 120, 110, 255};
            DrawTextCenteredBold(verdict, cx, cy + 22, 16, vc);
            DrawTextCentered(TextFormat("%s, %.0f mass", tn.c_str(), tm), cx, cy + 40, 13, dim);
            if (bestM >= 0 && w.mouths[bestM].immuneT > 0) DrawTextCentered("(glowing: a bite marks you for the sharks)", cx, cy + 56, 12, Color{255, 220, 170, 255});
        }
    }
    // the mass bar with the tier marks, the form, stamina, the ability
    {
        float x = SCREEN_W / 2.0f - 300, y = SCREEN_H - 92, wd = 600;
        int t = m.tier;
        float m0 = d.tiers[t].mass, m1 = t < 8 ? d.tiers[t + 1].mass : d.tiers[8].mass * 2;
        Bar(x, y, wd, 20, (m.mass - m0) / std::max(1.0f, m1 - m0), m.king ? gold : Color{120, 220, 200, 255});
        { std::string s = TextFormat("Tier %d  %s  -  %.0f mass%s", t, F.name.c_str(), m.mass, t < 8 ? TextFormat(" (tier %d at %.0f)", t + 1, m1) : "");
          DrawTextCenteredBold(s, SCREEN_W / 2.0f + 1, y + 3, 15, Color{0, 0, 0, 200}); DrawTextCenteredBold(s, SCREEN_W / 2.0f, y + 2, 15, ink); }
        Bar(x, y + 26, 200, 10, m.stamina, m.boosting ? Color{255, 230, 140, 255} : Color{200, 230, 240, 255});
        Txt("stamina (Shift)", x + 206, y + 23, 12, dim);
        if (F.ab != mf::AB_NONE) Txt(TextFormat("RMB/E: %s%s", F.abilityText.c_str(), m.abCd > 0 ? TextFormat("  (%.0f s)", ceilf(m.abCd)) : ""), x, y + 42, 13, m.abCd > 0 ? dim : Color{160, 250, 220, 255});
        if (F.ps != mf::PS_NONE) Txt(F.passiveText, x, y + 60, 12, dim);
        if (m.immuneT > 0) DrawTextCenteredBold(TextFormat("Glowing: nothing can hurt you for %.0f s", m.immuneT), SCREEN_W / 2.0f, y - 26, 15, Color{255, 236, 170, 255});
        if (m.hidden) DrawTextCenteredBold("Hidden in a hole: only your size can reach you", SCREEN_W / 2.0f, y - 46, 14, Color{200, 230, 255, 255});
        if (m.blindT > 0) DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(Color{10, 6, 20, 255}, 0.6f));
        if (m.reverseT > 0) DrawTextCenteredBold("DAZZLED: your controls are reversed", SCREEN_W / 2.0f, SCREEN_H / 2.0f - 80, 18, Color{255, 200, 255, 255});
        if (m.poisonT > 0 || m.bleedT > 0) DrawTextCentered(m.poisonT > 0 ? "poisoned" : "bleeding", SCREEN_W / 2.0f, y - 64, 14, Color{220, 255, 140, 255});
        if (m.markT > 0) DrawTextCentered("the sharks have your scent", SCREEN_W / 2.0f, y - 82, 13, Color{255, 170, 150, 255});
    }
    // the fork: the choices as cards (1-6 or a click); the first is picked if you wait
    if (m.pendingFork) {
        int n = (int)m.forkOpts.size();
        float cw = n > 3 ? 200 : 260, gap = 12, total = n * cw + (n - 1) * gap;
        float x = SCREEN_W / 2.0f - total / 2, y = SCREEN_H / 2.0f - 170;
        DrawTextCenteredBold(m.pendingFork == 2 ? "THE FIRST FORK: choose a path for this life" : TextFormat("FORK %d: choose your %s form", m.pendingFork / 2, m.pendingFork == 4 ? "second" : "final"), SCREEN_W / 2.0f, y - 40, 22, gold);
        DrawTextCentered(TextFormat("(keys 1-%d or click; the first in %.0f s)", n, std::max(0.0f, m.forkT)), SCREEN_W / 2.0f, y - 14, 14, dim);
        for (int k = 0; k < n; k++) {
            const mf::FormDef& G = d.forms[m.forkOpts[k]];
            Rectangle r{x + k * (cw + gap), y, cw, 230};
            bool hov = CheckCollisionPointRec(GetMousePosition(), r);
            DrawRectangleRounded(r, 0.08f, 6, Fade(hov ? Color{24, 70, 78, 255} : Color{8, 30, 36, 255}, 0.92f));
            DrawRectangleRoundedLinesEx(r, 0.08f, 6, 2, hov ? gold : Color{120, 170, 160, 255});
            DrawRectangle((int)r.x + 12, (int)r.y + 12, 26, 26, G.base); DrawRectangle((int)r.x + 22, (int)r.y + 22, 16, 16, G.accent);
            TxtBold(TextFormat("%d. %s", k + 1, G.name.c_str()), r.x + 46, r.y + 14, 17, ink);
            Txt(m.pendingFork == 2 ? TextFormat("the %s path", mf::PathName(G.path)) : "", r.x + 46, r.y + 36, 13, dim);
            Txt(TextFormat("speed %.1f  HP %.1f  bite %.1f", G.speed, G.hp, G.bite), r.x + 12, r.y + 60, 13, Color{200, 240, 230, 255});
            DrawWrapped(G.abilityText.empty() ? "No ability." : G.abilityText, {r.x + 12, r.y + 84, cw - 24, 70}, 13, Color{160, 250, 220, 255});
            DrawWrapped(G.passiveText.empty() ? "" : G.passiveText, {r.x + 12, r.y + 156, cw - 24, 64}, 12, dim);
            if (hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) S.forkClick = k;
        }
    }
    if (S.help) {
        Rectangle r{18, 44, 330, 158};
        DrawRectangleRounded(r, 0.06f, 6, Fade(Color{4, 20, 28, 255}, 0.7f));
        const char* L[] = {"Mouse: steer   W: swim   S: stop", "Shift: boost (stamina)   Space/Ctrl: up/down", "Left click: bite   Right click or E: ability", "Smaller than 60% of you: swallowed whole.", "60-100%: a fight.  Bigger: flee.", "Tab: the whole food chain   H: hide this"};
        for (int i = 0; i < 6; i++) Txt(L[i], r.x + 12, r.y + 10 + i * 24, 14, i < 3 ? ink : dim);
    }
    if (S.board) {
        auto b = w.Board();
        Rectangle r{SCREEN_W / 2.0f - 330, 90, 660, 40.0f + 26 * b.size()};
        DrawRectangleRounded(r, 0.04f, 6, Fade(Color{4, 20, 28, 255}, 0.9f));
        for (int i = 0; i < (int)b.size(); i++) {
            const mf::Mouth& o = w.mouths[b[i]];
            float y = r.y + 16 + i * 26;
            Color c = o.id == S.me ? gold : ink;
            Txt(TextFormat("%d. %s%s", i + 1, o.name.c_str(), o.king ? "  (KING)" : ""), r.x + 20, y, 16, c);
            Txt(TextFormat("%s, tier %d", w.FormOf(o).name.c_str(), o.tier), r.x + 250, y, 16, c);
            Txt(TextFormat("%d kills  %d deaths  %d", o.kills, o.deaths, (int)mf::ScoreOf(w, o)), r.x + 460, y, 16, c);
        }
    }
}
void DrawResults(Game& g, mf::World& w) {
    static const mf::World* paidFor = nullptr; static float paidAt = -1; static int paid = 0;
    if (paidFor != &w || paidAt != w.time) { paid = mf::RoundTokens(w, S.me, !S.shot); paidFor = &w; paidAt = w.time; }
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.55f));
    auto b = w.Board();
    Rectangle r{SCREEN_W / 2.0f - 380, 90, 760, 180.0f + 28 * b.size()};
    DrawRectangleRounded(r, 0.05f, 6, Fade(Color{6, 24, 32, 255}, 0.94f));
    DrawRectangleRoundedLinesEx(r, 0.05f, 6, 2, Color{214, 180, 110, 255});
    DrawTextCenteredBold(w.winner == S.me ? "You were the biggest mouth in the water." : (w.winner >= 0 ? w.mouths[w.winner].name : std::string("Nobody")) + " wins the round.", SCREEN_W / 2.0f, r.y + 16, 26, Color{255, 226, 150, 255});
    float y = r.y + 60;
    for (int i = 0; i < (int)b.size(); i++) {
        const mf::Mouth& o = w.mouths[b[i]];
        Color c = o.id == S.me ? Color{255, 230, 140, 255} : Color{236, 246, 240, 255};
        Txt(TextFormat("%d. %s", i + 1, o.name.c_str()), r.x + 24, y, 16, c);
        Txt(TextFormat("best tier %d", o.bestTier), r.x + 260, y, 16, c);
        Txt(TextFormat("%d kills, %d deaths, crown %.0f s", o.kills, o.deaths, o.crownT), r.x + 380, y, 16, c);
        Txt(TextFormat("%d", (int)mf::ScoreOf(w, o)), r.x + 680, y, 16, c);
        y += 28;
    }
    DrawTextCenteredBold(TextFormat("+%d tokens for the wardrobe (%d in all)", paid, mf::MyWardrobe().tokens + (S.shot ? paid : 0)), SCREEN_W / 2.0f, r.y + r.height - 78, 16, Color{255, 220, 150, 255});
    if (Button({SCREEN_W / 2.0f - 120, r.y + r.height - 50, 240, 38}, "Back to the arcade", true, 16)) ::LeaveMouthful(g);
}

}  // namespace

// ---------------------------------------------------------------- the wardrobe (doc pp. 16-18): the shop, the crate, a skin worn per path, the profile
namespace {
int gWrPath = mf::P_SHARK; std::string gWrMsg; float gWrMsgT = 0; std::string gWrShow; int gWrShowTier = -1; float gWrClamT = 0;
void Swatch(float cx, float cy, float s, const mf::SkinDef* sk, int path, float t) {
    // a fish in 2D: the path's first form's colours under the skin's tint, its hat named on it
    const auto& F = mf::D().forms; Color base{200, 200, 200, 255}, belly{240, 240, 240, 255};
    for (const auto& fm : F) if (fm.path == path && fm.tier == 2) { base = fm.base; belly = fm.belly; break; }
    if (sk && sk->hasTint) { base = Mul(base, sk->tint); belly = Mul(belly, sk->tint); }
    float wob = sinf(t * 3) * s * 0.05f;
    DrawTriangle({cx - s * 0.9f, cy}, {cx - s * 1.4f, cy + s * 0.45f + wob}, {cx - s * 1.4f, cy - s * 0.45f + wob}, Shade(base, 0.8f));
    DrawTriangle({cx - s * 0.9f, cy}, {cx - s * 1.4f, cy - s * 0.45f + wob}, {cx - s * 1.4f, cy + s * 0.45f + wob}, Shade(base, 0.8f));
    DrawEllipse((int)cx, (int)cy, s, s * 0.55f, base);
    DrawEllipse((int)cx, (int)(cy + s * 0.18f), s * 0.8f, s * 0.3f, belly);
    DrawCircle((int)(cx + s * 0.55f), (int)(cy - s * 0.12f), s * 0.12f, WHITE); DrawCircle((int)(cx + s * 0.58f), (int)(cy - s * 0.12f), s * 0.06f, BLACK);
    if (sk && sk->pattern == "stripes") for (int k = 0; k < 3; k++) DrawRectangle((int)(cx - s * 0.5f + k * s * 0.35f), (int)(cy - s * 0.5f), (int)(s * 0.1f), (int)(s), Fade(BLACK, 0.6f));
    if (sk && sk->hasGlow) for (int k = 0; k < 6; k++) DrawCircle((int)(cx - s * 0.6f + k * s * 0.24f), (int)(cy + sinf(k * 2.0f) * s * 0.25f), s * 0.05f, sk->glow);
    if (sk && !sk->hat.empty()) DrawTextCentered(sk->hat == "bicorne" ? "(a bicorne)" : sk->hat == "crown" ? "(a crown)" : ("(" + sk->hat + ")").c_str(), cx, cy - s * 0.95f, 13, Color{255, 230, 170, 255});
}
}
bool MouthfulWardrobePage() {
    mf::Wardrobe& w = mf::MyWardrobe();
    float t = (float)GetTime();
    ClearBackground(Color{8, 34, 44, 255});
    Color ink{236, 250, 246, 255}, dim{170, 210, 210, 255}, gold{255, 214, 110, 255};
    DrawTextCenteredBold("The Mouthful wardrobe", SCREEN_W / 2.0f, 18, 30, gold);
    DrawTextCentered(TextFormat("%d tokens   %d crate%s   %d rounds, %d crowns%s", w.tokens, w.crates, w.crates == 1 ? "" : "s", w.rounds, w.crowns, w.blobKing ? "   THE BLOBFISH KING" : ""), SCREEN_W / 2.0f, 56, 16, dim);
    // the path tabs: a skin is worn on every form of one path
    for (int p = 0; p < mf::P_COUNT; p++) {
        Rectangle r{SCREEN_W / 2.0f - 450 + p * 150.0f, 84, 140, 30};
        bool on = p == gWrPath;
        DrawRectangleRounded(r, 0.3f, 6, on ? Color{30, 120, 118, 255} : Color{16, 60, 64, 255});
        DrawTextCenteredBold(TextFormat("%s (best %d)", mf::PathName(p), w.bestTier[p]), r.x + r.width / 2, r.y + 7, 14, on ? ink : dim);
        if (CheckCollisionPointRec(GetMousePosition(), r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) gWrPath = p;
    }
    const mf::SkinDef* worn = mf::FindSkin(w.worn[gWrPath]);
    Swatch(SCREEN_W / 2.0f, 190, 60, worn, gWrPath, t);
    DrawTextCentered(TextFormat("worn on the %s path: %s", mf::PathName(gWrPath), worn ? worn->name.c_str() : "as hatched"), SCREEN_W / 2.0f, 250, 15, ink);
    if (worn && Button({SCREEN_W / 2.0f - 60, 272, 120, 26}, "take it off", true, 13)) mf::WearSkin(gWrPath, "");
    // the shop (left)
    DrawTextCenteredBold("The token shop", 250, 300, 18, gold);
    int i = 0;
    for (const auto& s : mf::Skins()) {
        if (s.tier != 0) continue;
        Rectangle r{40, 328 + i * 36.0f, 420, 32};
        bool own = w.Owns(s.id), on = w.worn[gWrPath] == s.id;
        DrawRectangleRounded(r, 0.2f, 6, on ? Color{40, 110, 90, 255} : Color{14, 50, 58, 255});
        DrawRectangle((int)r.x + 6, (int)r.y + 6, 20, 20, s.hasTint ? s.tint : Color{230, 230, 230, 255});
        Txt(s.name, r.x + 34, r.y + 7, 15, ink);
        if (!own) { if (Button({r.x + r.width - 110, r.y + 3, 104, 26}, TextFormat("buy %d", s.price), w.tokens >= s.price, 13)) { std::string why; if (!mf::BuySkin(s.id, &why)) { gWrMsg = why; gWrMsgT = 3; } } }
        else if (Button({r.x + r.width - 110, r.y + 3, 104, 26}, on ? "worn" : "wear", !on, 13)) mf::WearSkin(gWrPath, s.id);
        if (CheckCollisionPointRec(GetMousePosition(), r)) DrawTextCentered(s.note, SCREEN_W / 2.0f, SCREEN_H - 30.0f, 14, dim);
        i++;
    }
    // the crate (right): every crate skin, the ones you own lit
    DrawTextCenteredBold("The crate (a clam that opens)", SCREEN_W - 330, 300, 18, gold);
    int k = 0;
    for (const auto& s : mf::Skins()) {
        if (s.tier == 0) continue;
        float x = SCREEN_W - 620 + (k % 5) * 116.0f, y = 328 + (k / 5) * 34.0f;
        Rectangle r{x, y, 110, 30};
        bool own = w.Owns(s.id), on = w.worn[gWrPath] == s.id;
        static const Color TC[5] = {{200, 200, 200, 255}, {180, 220, 200, 255}, {120, 180, 255, 255}, {220, 140, 255, 255}, {255, 210, 90, 255}};
        DrawRectangleRounded(r, 0.2f, 6, on ? Color{40, 110, 90, 255} : own ? Color{20, 64, 70, 255} : Color{10, 26, 30, 255});
        DrawRectangleRoundedLinesEx(r, 0.2f, 6, 1, Fade(TC[s.tier], own ? 1.0f : 0.35f));
        DrawTextCentered(s.name.size() > 15 ? s.name.substr(0, 14) + "." : s.name, x + 55, y + 8, 12, own ? ink : Color{90, 110, 110, 255});
        if (CheckCollisionPointRec(GetMousePosition(), r)) { DrawTextCentered(s.name + " (" + mf::SkinTierName(s.tier) + (s.earned ? ", earned only" : "") + "): " + s.note, SCREEN_W / 2.0f, SCREEN_H - 30.0f, 14, dim); if (own && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) mf::WearSkin(gWrPath, on ? "" : s.id); }
        k++;
    }
    float cy = 328 + 8 * 34.0f + 14;
    if (Button({SCREEN_W - 620.0f, cy, 200, 34}, TextFormat("Buy a crate (%d token%s)", mf::CratePrice(), mf::CratePrice() == 1 ? "" : "s"), w.tokens >= mf::CratePrice(), 14)) { std::string why; if (!mf::BuyCrate(&why)) { gWrMsg = why; gWrMsgT = 3; } }
    if (Button({SCREEN_W - 400.0f, cy, 200, 34}, TextFormat("Open a clam (%d)", w.crates), w.crates > 0, 14)) {
        mf::CrateRoll r = mf::OpenCrate((uint32_t)(GetTime() * 1000) ^ (uint32_t)w.rounds * 977u);
        if (r.ok) { const mf::SkinDef* s = mf::FindSkin(r.id); gWrShow = s ? (r.duplicate ? "A pearl of no value (you have " + s->name + ")" : s->name) : "an empty clam"; gWrShowTier = r.duplicate ? -1 : r.tier; gWrClamT = 3.5f; PlayCue("ui.click"); }
    }
    if (gWrClamT > 0) {
        gWrClamT -= GetFrameTime();
        float open = std::clamp((3.5f - gWrClamT) * 2, 0.0f, 1.0f);
        Vector2 c{SCREEN_W - 330.0f, cy + 90};
        DrawEllipse((int)c.x, (int)(c.y + 10), 70, 26, Color{180, 170, 200, 255});
        DrawEllipse((int)c.x, (int)(c.y - 10 - open * 26), 70, 26 - open * 10, Color{200, 190, 220, 255});
        if (open >= 1) DrawTextCenteredBold(gWrShow, c.x, c.y - 4, 16, gWrShowTier >= 3 ? gold : ink);
    }
    if (gWrMsgT > 0) { gWrMsgT -= GetFrameTime(); DrawTextCenteredBold(gWrMsg, SCREEN_W / 2.0f, SCREEN_H - 60.0f, 16, Color{255, 190, 150, 255}); }
    return Button({30, 20, 120, 34}, "Back", true, 16);
}
void DebugMouthfulWardrobe() { mf::gWardrobeNoSave = true; mf::Wardrobe& w = mf::MyWardrobe(); w.tokens = 240; w.crates = 2; w.rounds = 14; w.crowns = 3; if (!w.Owns("captain")) w.owned.push_back("captain"); if (!w.Owns("dragon")) w.owned.push_back("dragon"); if (!w.Owns("sushi")) w.owned.push_back("sushi"); w.worn[mf::P_SHARK] = "captain"; gWrPath = mf::P_SHARK; }

// ---------------------------------------------------------------- sound: the state each frame, and the effects diffed from the world
struct SoundMemo { std::vector<float> bite, tell, morph, hurt, mass; std::vector<bool> alive; int king = -2; bool levAwake = false, hooked = false; float swimT = 0; };
SoundMemo gSm;
void MouthfulAudioFrame(const mf::World& w, float dt) {
    const mf::Mouth& me = w.mouths[std::clamp(S.me, 0, (int)w.mouths.size() - 1)];
    MfAudio a; a.on = true;
    Vector3 ear = S.cam.position;
    a.band = mf::BandAt(me.alive ? me.pos : ear); a.depth = -(me.alive ? me.pos.y : ear.y); a.tier = me.tier; a.dusk = w.Dusk();
    a.highTide = w.HighTide() && !w.over ? w.roundLen - w.time : 0; a.dead = !me.alive; a.crown = w.king >= 0;
    a.over = w.over ? (w.winner == S.me ? 1 : 2) : 0;
    Vector3 right = Vector3Normalize(Vector3CrossProduct(Vector3Subtract(S.cam.target, S.cam.position), {0, 1, 0}));
    auto panOf = [&](Vector3 p) { return std::clamp(Vector3DotProduct(Vector3Normalize(Vector3Subtract(p, ear)), right), -1.0f, 1.0f); };
    // an apex shark within 40 m; the orca pod; a boat overhead; a blobfish in view
    for (int i = 0; i < (int)w.eco.agents.size(); i++) {
        const rt::Agent& g = w.eco.agents[i];
        if (!g.alive || g.diver >= 0) continue;
        float d = Vector3Distance(g.pos, ear);
        if (w.npcEats[g.sp] >= 6 && w.npcEats[g.sp] < 8 && d < 40 && 1 - d / 40 > a.apex) { a.apex = 1 - d / 40; a.apexPan = panOf(g.pos); }
    }
    if (w.orcas.on) for (int ai : w.orcas.agents) if (ai < (int)w.eco.agents.size() && w.eco.agents[ai].alive) a.orcas = std::max(a.orcas, std::clamp(1 - Vector3Distance(w.eco.agents[ai].pos, ear) / 120, 0.0f, 1.0f));
    if (w.boat.on) a.boat = std::clamp(1 - Vector2Distance({w.boat.pos.x, w.boat.pos.z}, {ear.x, ear.z}) / 60, 0.0f, 1.0f) * std::clamp(1 + ear.y / 40, 0.2f, 1.0f);
    for (const auto& o : w.mouths) if (o.alive && w.FormOf(o).path == mf::P_BLOB && Vector3Distance(o.pos, ear) < 25) a.blobfish = true;
    AudioMouthful(a);
    // the effects: every mouth near enough to hear, by what changed since the last frame
    size_t n = w.mouths.size();
    if (gSm.bite.size() != n) { gSm = SoundMemo{}; gSm.bite.assign(n, 0); gSm.tell.assign(n, 0); gSm.morph.assign(n, 0); gSm.hurt.assign(n, 0); gSm.mass.assign(n, 0); gSm.alive.assign(n, true); for (size_t i = 0; i < n; i++) gSm.mass[i] = w.mouths[i].mass; gSm.king = w.king; }
    for (size_t i = 0; i < n; i++) {
        const mf::Mouth& o = w.mouths[i];
        float d = Vector3Distance(o.pos, ear), vol = std::clamp(1.2f - d / 35, 0.0f, 1.0f) * (o.id == S.me ? 1.0f : 0.8f);
        float pitch = 1.6f / (0.6f + w.Length(o));   // (bigger mouths, lower)
        if (vol > 0.02f) {
            if (o.biteAnim > gSm.bite[i] + 0.05f) {
                bool chef = false; if (const mf::SkinDef* os = mf::WornSkin(o.look, o.path)) chef = os->special == "chop";
                bool ate = o.mass > gSm.mass[i] + 0.5f;
                MouthfulCue(o.king ? MFC_CHOMP : ate ? (chef ? MFC_CLAW : MFC_GULP) : MFC_SNAP, vol, panOf(o.pos), pitch);   // (the Chef: every swallow is a chop)
            }
            if (o.hurtT > gSm.hurt[i] + 0.05f) MouthfulCue(MFC_CRUNCH, vol, panOf(o.pos), pitch);
            if (o.tellT > gSm.tell[i] + 0.05f) {
                static const int AB[mf::AB_COUNT] = {-1, MFC_DASH, MFC_DASH, MFC_DASH, MFC_DASH, MFC_INK, MFC_INK, MFC_INK, MFC_DASH, MFC_FRENZY, MFC_DASH, MFC_CLAW, MFC_DASH, MFC_CLAW, MFC_DASH, MFC_DASH, MFC_SLAM, MFC_CLAW, MFC_INTAKE, MFC_INK, MFC_POP, -1};   // (the stonefish's ambush: no sound)
                int k = AB[std::clamp((int)w.FormOf(o).ab, 0, mf::AB_COUNT - 1)];
                if (k >= 0) MouthfulCue(k, vol, panOf(o.pos), pitch);
            }
            if (o.morphT > gSm.morph[i] + 0.3f) MouthfulCue(MFC_FORK, vol, panOf(o.pos));
        }
        if (o.id == S.me && gSm.alive[i] != o.alive) MouthfulCue(o.alive ? MFC_RESPAWN : MFC_GULP, 1, 0, 0.6f);
        gSm.bite[i] = o.biteAnim; gSm.tell[i] = o.tellT; gSm.morph[i] = o.morphT; gSm.hurt[i] = o.hurtT; gSm.mass[i] = o.mass; gSm.alive[i] = o.alive;
    }
    // your own tail: a stroke now and then while swimming, heavier with the tier
    if (me.alive && Vector3Length(me.vel) > 1) { gSm.swimT += dt; if (gSm.swimT > 0.35f + 0.08f * me.tier) { gSm.swimT = 0; MouthfulCue(MFC_SWIM, me.boosting ? 0.8f : 0.45f, 0, 1.6f / (0.6f + w.Length(me))); } }
    // the crown: taken (a fanfare), lost (a crash); the leviathan's note; hooked; netted
    if (w.king != gSm.king) { if (w.king >= 0) MouthfulCue(MFC_FANFARE, 0.9f, 0); else if (gSm.king >= 0) MouthfulCue(MFC_CRASH, 0.9f, 0); gSm.king = w.king; }
    bool lev = w.levAwakeT > 0; if (lev && !gSm.levAwake) MouthfulCue(MFC_LEVIATHAN, mf::BandAt(ear) == mf::B_TRENCH ? 1.0f : 0.35f, 0); gSm.levAwake = lev;
    bool hooked = false; for (const auto& hk : w.boat.hookList) hooked |= hk.held == S.me; if (hooked && !gSm.hooked) MouthfulCue(MFC_HOOK, 1, 0); gSm.hooked = hooked;
}
void LeaveMouthful(Game& g) {
    AudioMouthful(MfAudio{});
    if (S.net) { if (S.net->role == arcade::R_HOST) S.net->BackToLobby(); else S.net->Leave(); }
    S.net = nullptr; S.live = nullptr;
    S.active = false; FreeModels(); g.scene = Scene::Arcade;
}
// a networked round (stage 3): the arcade's session launched Mouthful; the host draws its real world, a guest its mirror
void StartMouthfulNet(Game& g, arcade::Session* net, const char* name) {
    std::string why;
    if (!rt::DataOk(&why)) { g.scene = Scene::Arcade; return; }
    S.net = net; S.live = nullptr; S.W = mf::World{};
    S.seenVersion = -1; S.sinceSnap = 0; S.helloSent = false; S.netName = name ? name : "Mouth";
    S.active = true; S.shot = false; S.help = true; S.t = 0; S.me = std::max(0, net->MyPlayer());
    S.aimYaw = 0; S.aimPitch = 0; S.camYaw = 0; S.camPitch = 0; S.camDist = 1;
    g.scene = Scene::Mouthful;
}
void StartMouthful(Game& g, int bots, float minutes, int botLevel) {
    std::string why;
    if (!rt::DataOk(&why)) { g.scene = Scene::Arcade; return; }
    S.net = nullptr; S.live = nullptr; S.W.mirror = false;
    S.opts = mf::Opts{}; S.opts.humans = 1; S.opts.bots = std::clamp(bots, 0, 11); S.opts.minutes = minutes; S.opts.botLevel = botLevel; S.opts.seed = (uint32_t)GetRandomValue(1, 1 << 30);
    WD().Init(S.opts);
    WD().mouths[0].look = mf::LookString();
    S.me = 0; S.active = true; S.shot = false; S.help = true; S.t = 0;
    S.aimYaw = WD().mouths[0].yaw; S.aimPitch = 0; S.camYaw = S.aimYaw; S.camPitch = 0; S.camDist = 1;
    g.scene = Scene::Mouthful;
}
void StartMouthfulMode(Game& g, int bots, float minutes, int botLevel, int mode, int path) {
    StartMouthful(g, bots, minutes, botLevel);
    if (!S.active) return;
    S.opts.mode = mode; S.opts.path = path;
    if (mode == mf::M_SOLO_TANK) S.opts.bots = 11;
    WD().Init(S.opts);
    WD().mouths[0].look = mf::LookString();
    S.aimYaw = S.camYaw = WD().mouths[0].yaw;
}
void SceneMouthful(Game& g) {
    if (!S.active) { StartMouthful(g, 11, 15, 0); if (!S.active) return; }
    float dt = S.shot ? 1 / 60.0f : std::min(GetFrameTime(), 1 / 30.0f);
    if (S.net) {
        arcade::Session& N = *S.net;
        N.Update(GetTime(), dt);
        if (N.stage != arcade::S_PLAYING) { S.net = nullptr; S.live = nullptr; S.active = false; FreeModels(); g.scene = Scene::Arcade; return; }
        S.me = std::max(0, N.MyPlayer());
        if (N.role == arcade::R_HOST) S.live = mf::MouthfulHostWorld(N.HostGame());
        else {
            S.sinceSnap += dt;
            if (N.stateVersion != S.seenVersion && !N.Snapshot().empty()) {
                S.seenVersion = N.stateVersion;
                Reader r(N.Snapshot());
                if (mf::ReadWorld(r, S.W, S.W.mirror)) S.sinceSnap = 0;
            } else if (S.W.mirror && S.sinceSnap < 0.3f) {
                // between snapshots everything swims on along its last heading
                for (auto& a : S.W.eco.agents) if (a.alive && a.diver < 0) a.pos = Vector3Add(a.pos, Vector3Scale(a.vel, dt));
                for (auto& o : S.W.mouths) if (o.alive && o.id != S.me) o.pos = Vector3Add(o.pos, Vector3Scale(o.vel, dt));
            }
        }
        mf::World& w = WD();
        if (w.mouths.empty() || (!S.live && !w.mirror)) { ClearBackground(Color{30, 110, 130, 255}); DrawTextCenteredBold("Into the water...", SCREEN_W / 2.0f, SCREEN_H / 2.0f - 12, 24, WHITE); return; }
        if (!S.helloSent) { Writer o; mf::OrderHello(o, S.netName, mf::LookString()); N.Act(o); S.helloSent = true; S.aimYaw = S.camYaw = w.mouths[S.me].yaw; }
        Gather(dt);
        Writer iw; mf::WriteInput(Me().in, iw); N.Act(iw);
        if (!S.live) w.Predict(Me(), Me().in, dt);   // (a guest swims its own mouth ahead of the host)
        S.t += dt;
        Render(dt);
        MouthfulAudioFrame(w, dt);
        DrawHud(w);
        if (w.over) DrawResults(g, w);
        return;
    }
    Gather(dt);
    if (!S.shot) WD().Step(dt);
    S.t += dt;
    Render(dt);
    if (!S.shot) MouthfulAudioFrame(WD(), dt);
    DrawHud(WD());
    if (WD().over) DrawResults(g, WD());
}
std::string MouthfulOpts(int minutes, int botLevel, int fill, int mode, int path) { return mf::MouthfulHostOpts(minutes, botLevel, fill, mode, path); }
void MouthfulMenuTick(float dt) {
    // (the game menu is open: a networked round goes on underneath, and our mouth drifts on its last heading)
    if (!S.active || !S.net) { if (S.active && !S.shot) WD().Step(dt); return; }
    mf::Input in; in.yaw = S.aimYaw; in.pitch = S.aimPitch;
    Writer w; mf::WriteInput(in, w); S.net->Act(w);
    S.net->Update(GetTime(), dt);
}
// --shots: 0 a fry in the shallows among minnows, 1 the reef, 2 the wall, 3 the blue with tuna and a shark, 4 the trench
// and the leviathan, 5 the first fork, 6 a king with the crown, 7 the results, 8 a line-up of forms
void DebugMouthfulShot(Game& g, int which) {
    StartMouthful(g, 11, 15, 2);
    S.shot = true; S.help = which == 0;
    mf::World& w = WD();
    mf::Mouth& m = Me();
    m.immuneT = which == 0 ? 6 : 0;
    auto place = [&](Vector3 p, float yaw, float pitch, float mass, const char* form) {
        m.pos = p; m.yaw = yaw; m.pitch = pitch; m.mass = mass; m.tier = w.TierOfMass(mass);
        if (form) { int f = mf::FormIndex(form); if (f >= 0) { m.form = f; m.path = mf::D().forms[f].path; } }
        S.aimYaw = S.camYaw = yaw; S.aimPitch = S.camPitch = pitch;
        if (m.agent >= 0) w.eco.agents[m.agent].pos = p;
    };
    auto gather = [&](const char* sp, int n, Vector3 c, float r) {   // bring some of the web into view
        int idx = w.eco.map->SpeciesIndex(sp), k = 0;
        for (auto& a : w.eco.agents) if (a.alive && a.sp == idx && k < n) { a.pos = {c.x + cosf(k * 2.4f) * r * (0.4f + 0.6f * (k % 3) / 2.0f), c.y + sinf(k * 1.3f) * r * 0.3f, c.z + sinf(k * 2.4f) * r * (0.4f + 0.6f * (k % 3) / 2.0f)}; a.pos.y = std::max(a.pos.y, mf::FloorY(a.pos.x, a.pos.z) + 0.5f); a.vel = {0.3f, 0, 0.1f}; k++; }
    };
    for (int i = 0; i < 40; i++) w.Step(1 / 20.0f);   // (the web settles)
    if (which == 0) { place({-250, -3, 10}, 0.2f, -0.05f, 18, nullptr); gather("Minnow", 20, {-246, -3.5f, 11}, 4); }
    if (which == 1) { place({-120, -14, 20}, 0.3f, -0.15f, 140, "reef_squid"); gather("Sardine", 30, {-112, -13, 23}, 6); gather("Snapper", 4, {-110, -17, 18}, 5); }
    if (which == 2) { place({-34, -60, 0}, 0.0f, -0.25f, 400, "conger"); gather("Mackerel", 12, {-20, -62, 4}, 8); }
    if (which == 3) { place({40, -30, 0}, 0.0f, 0.0f, 900, "bull"); gather("Tuna", 10, {58, -30, 3}, 10); gather("Great White", 1, {75, -26, -8}, 1); }
    if (which == 4) { place({200, -230, 0}, 0.4f, -0.4f, 2600, "great_white_p"); }
    if (which == 5) { place({-230, -4, 0}, 0.2f, 0, 35, nullptr); m.pendingFork = 0; w.GrowCheck(m); }
    if (which == 6) { place({60, -40, 10}, 0.5f, -0.1f, 5200, "tiger_p"); w.GrowCheck(m); w.king = m.id; m.king = true; gather("Tuna", 8, {75, -40, 18}, 10); }
    if (which == 7) { for (auto& o : w.mouths) { o.massEaten = 200 + o.id * 300.0f; o.kills = o.id % 4; o.bestTier = 2 + o.id % 6; } w.time = w.roundLen; w.over = true; w.winner = w.Leader(); place({-200, -6, 0}, 0, 0, 300, "dogfish"); }
    if (which == 8) {
        // a line-up: one mouth of every path's forms, side by side in the blue
        place({20, -40, -14}, PI * 0.5f, 0, 600, "bull");
        int k = 0;
        for (auto& o : w.mouths) { if (o.id == m.id) continue; int fi = 1 + (k * 3) % ((int)mf::D().forms.size() - 1); o.form = fi; o.path = mf::D().forms[fi].path; o.mass = 300; o.tier = w.TierOfMass(o.mass); o.pos = {12.0f + (k % 6) * 3.2f, -40 + (k / 6) * 2.5f, 0}; o.yaw = PI; o.pitch = 0; o.vel = {0, 0, 0}; o.immuneT = 0; o.alive = true; k++; }
    }
    for (auto& o : w.mouths) if (o.agent >= 0) w.eco.agents[o.agent].pos = o.pos;
    if (which == 10) {   // the boat with a net and hooks, from below in the shallows
        w.boat = mf::Boat{}; w.boat.on = true; w.boat.net = true; w.boat.hooks = true; w.boat.dirZ = 1; w.boat.pos = {-230, 0, 12};
        for (int k = 0; k < 4; k++) { mf::Hook h; h.pos = {-236 + k * 4.0f, -3.5f - k * 0.7f, 12}; w.boat.hookList.push_back(h); }
        place({-232, -6.5f, -2}, PI * 0.5f, 0.35f, 60, "moray");
    }
    if (which == 11) {   // the orca pod in the blue
        place({60, -30, 0}, 0, 0.05f, 2600, "tiger_p"); w.GrowCheck(m);
        w.orcas.at = w.time; w.StepEvents(0.01f);
        for (size_t k = 0; k < w.orcas.agents.size(); k++) { auto& a = w.eco.agents[w.orcas.agents[k]]; a.pos = {72.0f + k * 3, -28 - k * 1.5f, -4.0f + k * 5}; a.vel = {-6, 0, 1}; }
    }
    if (which == 12) { w.bloomAt = w.time; w.StepEvents(0.01f); place(Vector3Add(w.bloom.pos, {-20, 0, 0}), 0, -0.1f, 140, "reef_squid"); m.pos.y = std::max(m.pos.y, mf::FloorY(m.pos.x, m.pos.z) + 3); }
    if (which == 13) { w.fallAt = w.time; w.StepEvents(0.01f); w.fall.left = 180; place(Vector3Add(w.fall.pos, {-14, 6, 2}), 0, -0.35f, 1600, "croc"); gather("Rattail", 6, w.fall.pos, 5); }
    if (which == 15) {   // a line-up in skins: every shop skin and some of the crate's, each on a different form
        place({20, -40, -14}, PI * 0.5f, 0, 600, "bull");
        std::vector<const mf::SkinDef*> sk; for (const auto& s : mf::Skins()) if (s.tier == 0 || s.tier >= 3) sk.push_back(&s);
        int k = 0;
        for (auto& o : w.mouths) {
            if (o.id == m.id) continue;
            int fi = 1 + (k * 5) % ((int)mf::D().forms.size() - 1); o.form = fi; o.path = mf::D().forms[fi].path; o.mass = 600; o.tier = w.TierOfMass(o.mass);
            o.pos = {20 + ((k % 4) - 1.5f) * 2.8f, -40.5f + (k / 4) * 1.6f, -14 + 7.0f + (k / 4) * 2.5f}; o.yaw = -PI * 0.5f + 0.5f; o.pitch = 0; o.vel = {0, 0, 0}; o.immuneT = 0; o.alive = true;
            if (k >= 8) { o.pos = {200, -200, 0}; }
            o.look.clear(); for (int p = 0; p < mf::P_COUNT; p++) { if (p) o.look += ";"; o.look += sk[(k + p) % sk.size()]->id; }
            k++;
        }
    }
    if (which == 14) { w.time = w.duskAt + 40; place({-120, -14, 20}, 0.3f, -0.1f, 400, "octopus"); gather("Sardine", 30, {-110, -10, 23}, 6); }
    if (which == 9) {
        // a guest's screen: the round run a while by a host, mirrored from the snapshot for player 2
        static mf::World host; mf::Opts o; o.humans = 2; o.bots = 10; o.seed = 4242; o.minutes = 15; o.botLevel = 2;
        host.Init(o);
        for (int i = 0; i < 2400; i++) host.Step(1 / 20.0f);
        host.mouths[1].alive = true;
        Writer wr; mf::WriteWorld(host, 1, wr); Reader r(wr.b);
        S.W = mf::World{}; mf::ReadWorld(r, S.W, false);
        S.me = 1; S.aimYaw = S.camYaw = S.W.mouths[1].yaw; S.aimPitch = S.camPitch = 0;
    }
    S.camDist = 0.7f + WD().Length(Me()) * 3.0f;
}
