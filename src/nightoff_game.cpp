// A Night Off's scene (arcade game 6), stage 1: the Sodden Gull as a 3D space drawn with Red Tide's inked renderer under
// warm gaslight, the crew in shore clothes on the shared humanoid rig (figure3d), an over-the-shoulder camera with a free
// orbit, the drunk meter's animation (the sway, the weave, stumbles, the lean, a lagging and rolling camera), the
// bartender and his menu, and the morning-after screen. The rules are nightoff.cpp (headless).
#include "game.h"
#include "nightoff.h"
#include "figure3d.h"
#include "redtide_render.h"
#include "input.h"
#include "sound.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>

namespace {

struct NightScene {
    bool active = false, shot = false;
    no::Night N;
    int me = 0;
    float camYaw = PI * 0.5f, camPitch = -0.28f, camDist = 3.2f;
    Vector3 camAt{};          // the camera's lagging focus
    float t = 0, rollK = 0;
    bool menu = false, help = true;
    std::vector<float> walkPh;
    Camera3D cam{};
};
NightScene S;

Color Mix(Color a, Color b, float k) { k = std::clamp(k, 0.0f, 1.0f); return {(unsigned char)(a.r + (b.r - a.r) * k), (unsigned char)(a.g + (b.g - a.g) * k), (unsigned char)(a.b + (b.b - a.b) * k), 255}; }
Color Shade(Color c, float k) { return {(unsigned char)std::clamp(c.r * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.g * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.b * k, 0.0f, 255.0f), c.a}; }
no::Player& Me() { return S.N.players[std::clamp(S.me, 0, (int)S.N.players.size() - 1)]; }

// ---------------------------------------------------------------- the crew (doc p. 25: the Nautilus crew's silhouettes in shore clothes)
const Model* CrewModel(int crew) {
    static const char* F[6] = {"shared/crew/crew_diver.glb", "shared/crew/crew_bosun.glb", "shared/crew/crew_angler.glb", "shared/crew/crew_bosun.glb", "shared/crew/crew_medic.glb", "shared/crew/crew_medic.glb"};
    return rt::LoadAsset(F[std::clamp(crew, 0, 5)]);
}
struct Clothes { Color top, trousers, hat, skin; float build; };
Clothes ShoreClothes(int crew) {
    switch (crew) {
        case 0: return {{60, 84, 124, 255}, {52, 50, 58, 255}, {40, 44, 60, 255}, {214, 170, 140, 255}, 1.0f};    // the Diver: a navy pea coat
        case 1: return {{128, 62, 40, 255}, {70, 56, 44, 255}, {90, 70, 50, 255}, {196, 150, 118, 255}, 1.08f};   // the Whaler: a rust-red coat
        case 2: return {{150, 128, 84, 255}, {84, 74, 60, 255}, {120, 96, 60, 255}, {226, 186, 150, 255}, 0.92f}; // the Stowaway: a too-big cardigan
        case 3: return {{70, 96, 74, 255}, {60, 58, 54, 255}, {60, 60, 64, 255}, {170, 124, 96, 255}, 1.1f};     // the Mechanic: green work shirt
        case 4: return {{36, 44, 80, 255}, {40, 40, 50, 255}, {30, 32, 46, 255}, {220, 180, 150, 255}, 1.02f};    // the Captain: Nemo-blue
        default: return {{216, 214, 226, 255}, {80, 70, 90, 255}, {200, 60, 60, 255}, {232, 196, 170, 255}, 0.96f}; // the Nurse
    }
}
void DrawPerson(const Model* m, const Clothes& c, Vector3 feet, float yaw, fig::Pose P, float lean, float lurch, bool lying) {
    if (!m) return;
    fig::Build B; B.build = c.build;
    std::vector<Matrix> skin = fig::PoseFigure(*m, B, P, S.t);
    // the frame: feet on the floor facing yaw, leaning (the drunk sway) about its forward axis and pitched by a lurch
    Matrix frame = MatrixMultiply(MatrixMultiply(MatrixRotateX(lean), MatrixRotateZ(-lurch)), fig::Frame(feet, -yaw));
    if (lying) frame = MatrixMultiply(MatrixMultiply(MatrixRotateZ(PI * 0.5f), MatrixTranslate(0, 0.18f, 0)), fig::Frame(feet, -yaw));
    std::vector<rt::Recolor> rc = {{"top", c.top}, {"trousers", c.trousers}, {"hat", c.hat}, {"skin", c.skin}};
    rt::DrawPbrSkinned(*m, frame, skin, rc, 0.35f, WHITE);
}

// ---------------------------------------------------------------- the bar
Color FloorOf(const std::string& k) {
    if (k == "toilets") return {176, 176, 166, 255};
    if (k == "kitchen") return {140, 128, 108, 255};
    if (k == "yard") return {62, 92, 54, 255};
    if (k == "alley") return {62, 62, 68, 255};
    if (k == "street") return {74, 74, 82, 255};
    if (k == "dance") return {92, 54, 40, 255};
    if (k == "snug" || k == "cards") return {96, 40, 40, 255};   // (red carpet)
    return {116, 80, 50, 255};                                    // (oak boards)
}
void DrawBar(const no::Night& n) {
    const no::BarData& B = no::D().bar;
    // the dock beyond the street, the floors
    rt::DrawWorldCube({20, -0.4f, -22}, {120, 0.4f, 24}, {24, 46, 70, 255});
    for (const auto& r : B.rooms) { if (r.key == "front") continue; rt::DrawWorldCube({r.r.x + r.r.width / 2, -0.05f, r.r.y + r.r.height / 2}, {r.r.width, 0.1f, r.r.height}, FloorOf(r.key)); }
    // the walls: plaster over a dark wainscot, a brass rail at the top of the panelling
    for (const auto& w : B.walls) {
        Vector2 d = Vector2Subtract(w.b, w.a); float L = Vector2Length(d); if (L < 0.01f) continue;
        Vector2 c = Vector2Scale(Vector2Add(w.a, w.b), 0.5f);
        float ang = atan2f(d.y, d.x);
        auto slab = [&](float y0, float y1, Color col, float th) { rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(L + 0.3f, y1 - y0, th), MatrixRotateY(-ang)), MatrixTranslate(c.x, (y0 + y1) / 2, c.y)), col); };
        bool outer = (fabsf(w.a.y) < 0.01f && fabsf(w.b.y) < 0.01f) || (fabsf(w.a.y - 30) < 0.01f && fabsf(w.b.y - 30) < 0.01f) || (fabsf(w.a.x) < 0.01f && fabsf(w.b.x) < 0.01f) || (fabsf(w.a.x - 40) < 0.01f && fabsf(w.b.x - 40) < 0.01f);
        slab(0, 1.1f, {78, 50, 32, 255}, 0.32f);
        slab(1.1f, B.wallH, outer ? Color{150, 96, 70, 255} : Color{196, 172, 128, 255}, 0.3f);
        slab(1.08f, 1.16f, {200, 160, 80, 255}, 0.36f);
    }
    // windows on the street side: the blue dock night through them
    for (float x : {4.0f, 13.0f, 26.0f, 34.0f}) rt::DrawCubeGlow(MatrixMultiply(MatrixScale(1.6f, 1.3f, 0.36f), MatrixTranslate(x, 1.9f, 0)), {40, 70, 110, 255}, 0.6f);
    // the furniture
    for (const auto& b : B.boxes) {
        Vector3 c{b.r.x + b.r.width / 2, b.h / 2, b.r.y + b.r.height / 2}, s{b.r.width, b.h, b.r.height};
        const std::string& k = b.kind;
        if (k == "counter") { rt::DrawWorldCube(c, s, {84, 48, 30, 255}); rt::DrawWorldCube({c.x, b.h + 0.02f, c.z}, {s.x + 0.08f, 0.05f, s.z + 0.1f}, {210, 170, 80, 255}); }
        else if (k == "pool") { rt::DrawWorldCube({c.x, 0.4f, c.z}, {s.x, 0.8f, s.z}, {84, 50, 30, 255}); rt::DrawWorldCube({c.x, 0.82f, c.z}, {s.x - 0.2f, 0.04f, s.z - 0.2f}, {30, 110, 64, 255}); }
        else if (k == "slot") { rt::DrawWorldCube(c, s, {120, 40, 40, 255}); rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.1f, 0.5f, 0.6f), MatrixTranslate(b.r.x + b.r.width + 0.02f, 1.3f, c.z)), {255, 210, 120, 255}, 1.0f + 0.4f * sinf(S.t * 3 + c.z)); }
        else if (k == "table") { rt::DrawWorldCube({c.x, b.h - 0.03f, c.z}, {s.x, 0.06f, s.z}, {96, 62, 38, 255}); rt::DrawWorldCube({c.x, b.h / 2, c.z}, {0.12f, b.h, 0.12f}, {60, 40, 28, 255}); }
        else if (k == "hearth") {
            rt::DrawWorldCube(c, s, {80, 70, 66, 255});
            rt::DrawWorldCube({b.r.x - 0.05f, 0.35f, c.z}, {0.3f, 0.7f, 1.8f}, {20, 16, 14, 255});   // (the firebox)
            for (int j = 0; j < 9; j++) { float fl = 0.5f + 0.5f * sinf(S.t * (6 + j) + j * 2.1f); rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.12f, 0.14f + 0.18f * fl, 0.12f), MatrixTranslate(b.r.x - 0.15f, 0.15f + 0.1f * fl, c.z - 0.6f + j * 0.15f)), j % 2 ? Color{255, 150, 50, 255} : Color{255, 210, 90, 255}, 1.6f); }
        }
        else if (k == "stage") rt::DrawWorldCube(c, s, {70, 40, 30, 255});
        else if (k == "piano") rt::DrawWorldCube({c.x, b.h / 2 + 0.5f, c.z}, s, {24, 20, 22, 255});
        else if (k == "fryer") { rt::DrawWorldCube(c, s, {130, 130, 136, 255}); rt::DrawCubeGlow(MatrixMultiply(MatrixScale(s.x * 0.8f, 0.05f, s.z * 0.6f), MatrixTranslate(c.x, b.h + 0.02f, c.z)), {255, 170, 60, 255}, 0.5f); }
        else if (k == "stairs") { for (int i = 0; i < 6; i++) rt::DrawWorldCube({c.x, (i + 0.5f) * b.h / 6, b.r.y + b.r.height - (i + 0.5f) * b.r.height / 6}, {s.x, (i + 1) * b.h / 6, b.r.height / 6}, {100, 66, 40, 255}); }
        else if (k == "stall") rt::DrawWorldCube(c, {s.x, s.y, 0.08f}, {90, 70, 56, 255});
        else if (k == "sink") rt::DrawWorldCube(c, s, {220, 220, 214, 255});
        else if (k == "bins") rt::DrawWorldCube(c, s, {50, 60, 50, 255});
        else rt::DrawWorldCube(c, s, {100, 70, 50, 255});
    }
    // the stools along the bar, the bottles on the back shelf, the dartboard, the jukebox
    for (const auto& st : B.stools) { rt::DrawWorldCube({st.x, 0.38f, st.y}, {0.08f, 0.76f, 0.08f}, {60, 40, 30, 255}); rt::DrawWorldCube({st.x, 0.78f, st.y}, {0.42f, 0.07f, 0.42f}, {130, 40, 40, 255}); }
    rt::DrawWorldCube({17, 1.9f, 11.8f}, {9, 0.06f, 0.35f}, {90, 60, 36, 255}); rt::DrawWorldCube({17, 1.4f, 11.8f}, {9, 0.06f, 0.35f}, {90, 60, 36, 255});
    static const Color BC[5] = {{80, 140, 70, 255}, {150, 90, 40, 255}, {200, 200, 210, 255}, {120, 40, 50, 255}, {220, 180, 90, 255}};
    for (int i = 0; i < 34; i++) { float x = 12.9f + i * 0.25f; int row = i % 2; rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.08f, 0.3f, 0.08f), MatrixTranslate(x, 1.58f + row * 0.5f, 11.75f)), BC[(i * 7) % 5], 0.35f); }
    rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.06f, 0.5f, 0.5f), MatrixTranslate(B.dartboard.x + 0.08f, 1.7f, B.dartboard.y)), {200, 60, 50, 255}, 0.4f);
    rt::DrawWorldCube({B.jukebox.x, 0.7f, B.jukebox.y}, {0.9f, 1.4f, 0.6f}, {150, 90, 40, 255});
    rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.7f, 0.5f, 0.62f), MatrixTranslate(B.jukebox.x, 1.1f, B.jukebox.y)), {255, 150, 220, 255}, 0.9f + 0.3f * sinf(S.t * 2));
    // the lamps (their shades glow; the light itself is the scene's point lights)
    for (const auto& l : B.lamps) {   // a gas lamp on its chain: a brass cap, a warm glass
        if (l.z < 30) rt::DrawWorldCube({l.x, (l.y + B.wallH) / 2 + 0.1f, l.z}, {0.03f, B.wallH - l.y, 0.03f}, {60, 50, 40, 255});
        rt::DrawWorldCube({l.x, l.y + 0.12f, l.z}, {0.22f, 0.06f, 0.22f}, {180, 140, 70, 255});
        rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.14f, 0.18f, 0.14f), MatrixTranslate(l.x, l.y - 0.02f, l.z)), {255, 190, 110, 255}, 0.9f);
    }
    // the ceilings over the rooms (dark boards and beams); the yard, the alley and the street are open to the night
    for (const auto& r : B.rooms) {
        if (r.key == "yard" || r.key == "alley" || r.key == "street" || r.key == "front") continue;
        rt::DrawWorldCube({r.r.x + r.r.width / 2, B.wallH + 0.05f, r.r.y + r.r.height / 2}, {r.r.width, 0.1f, r.r.height}, {54, 36, 26, 255});
        for (float x = r.r.x + 1.5f; x < r.r.x + r.r.width; x += 3) rt::DrawWorldCube({x, B.wallH - 0.12f, r.r.y + r.r.height / 2}, {0.25f, 0.24f, r.r.height}, {70, 46, 30, 255});
    }
    // the yard's string lights, the street's lamp posts
    for (int i = 0; i < 24; i++) { float x = 1 + i * 1.65f; rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.08f, 0.08f, 0.08f), MatrixTranslate(x, 3.0f - 0.3f * sinf(i * 0.26f * PI), 38)), i % 3 == 0 ? Color{255, 120, 90, 255} : Color{255, 220, 140, 255}, 1.6f); }
    for (float x : {6.0f, 30.0f}) { rt::DrawWorldCube({x, 1.6f, -5}, {0.12f, 3.2f, 0.12f}, {30, 30, 34, 255}); rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.3f, 0.4f, 0.3f), MatrixTranslate(x, 3.3f, -5)), {255, 210, 140, 255}, 1.8f); }
    (void)n;
}
void DrawPeople(const no::Night& n) {
    // the bartender behind his counter, polishing a glass (or reaching for one)
    {
        const Model* m = rt::LoadAsset("shared/crew/crew_bosun.glb");
        Clothes c{{226, 222, 210, 255}, {40, 36, 34, 255}, {60, 50, 40, 255}, {200, 150, 120, 255}, 1.12f};
        fig::Pose P; P.breathe = S.t * 1.4f; P.blink = fmodf(S.t, 4.1f) < 0.12f ? 1.0f : 0.0f;
        if (n.bar.busyT > 0) { P.reach = 0.7f; P.elbow = 0.3f; } else { P.reach = 0.35f; P.elbow = 0.7f + 0.15f * sinf(n.bar.polishPh * 5); P.grip = 0.8f; }
        float yaw = -PI * 0.5f;   // (facing the room: -z)
        DrawPerson(m, c, {n.bar.pos.x, 0, n.bar.pos.y}, yaw, P, 0, 0, false);
    }
    if (S.walkPh.size() < n.players.size()) S.walkPh.resize(n.players.size(), 0);
    for (const auto& p : n.players) {
        if (p.st == no::State::Gone) continue;
        float spd = Vector2Length(p.vel), k = std::clamp(p.drunk / 100, 0.0f, 1.0f);
        S.walkPh[p.id] += spd * GetFrameTime() * 1.6f;
        fig::Pose P;
        P.walk = std::clamp(spd / 3.0f, 0.0f, 1.0f); P.walkPh = S.walkPh[p.id];
        P.breathe = S.t * (1.3f + k);
        P.blink = fmodf(S.t + p.id, 3.0f + 2 * k) < 0.12f + 0.25f * k ? 1.0f : 0.0f;   // (a slower blink)
        P.nod = 0.25f * k * k + 0.05f * sinf(p.swayPh * 0.7f) * k;   // (the head droops)
        P.look = 0.2f * sinf(p.swayPh * 0.5f) * k;
        if (p.st == no::State::Drinking) { float u = 1 - p.actT / 2.5f; P.reach = 0.25f; P.elbow = std::clamp(u * 3, 0.0f, 1.0f); P.grip = 0.9f; P.nod = -0.2f * P.elbow; }
        if (p.st == no::State::Eating) { P.reach = 0.6f; P.elbow = 0.5f + 0.3f * sinf(S.t * 6); P.nod = 0.25f; }
        if (p.st == no::State::Vomiting) { P.nod = 0.9f; P.reach = 0.4f; }
        float lean = sinf(p.swayPh) * (0.03f + 0.16f * k * k) + (p.stumbleT > 0 ? p.stumbleDir * 0.25f : 0);   // (the sway; a stumble throws it)
        if (p.st == no::State::Vomiting) lean = 0;
        float pitch = p.st == no::State::Vomiting ? 0.5f : p.lurch * 0.2f;
        Matrix dummy = MatrixIdentity(); (void)dummy;
        DrawPerson(CrewModel(p.crew), ShoreClothes(p.crew), {p.pos.x, 0, p.pos.y}, p.yaw, P, lean, pitch, p.st == no::State::PassedOut);
        // a glass in the hand while drinking
        if (p.st == no::State::Drinking) { Vector3 h{p.pos.x + cosf(p.yaw) * 0.3f, 1.2f + 0.3f * std::clamp((1 - p.actT / 2.5f) * 3, 0.0f, 1.0f), p.pos.y + sinf(p.yaw) * 0.3f}; rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.08f, 0.14f, 0.08f), MatrixTranslate(h.x, h.y, h.z)), {230, 170, 60, 255}, 0.5f); }
    }
}

// ---------------------------------------------------------------- the camera: over the shoulder, a free orbit; drink makes it lag, lurch and roll
bool WallBetween(Vector2 a, Vector2 b, Vector2* hit) {
    float best = 1; bool any = false;
    for (const auto& w : no::D().bar.walls) {
        Vector2 r = Vector2Subtract(b, a), s = Vector2Subtract(w.b, w.a);
        float den = r.x * s.y - r.y * s.x; if (fabsf(den) < 1e-6f) continue;
        Vector2 q = Vector2Subtract(w.a, a);
        float u = (q.x * s.y - q.y * s.x) / den, v = (q.x * r.y - q.y * r.x) / den;
        if (u > 0 && u < best && v >= -0.05f && v <= 1.05f) { best = u; any = true; }
    }
    if (any) *hit = Vector2Add(a, Vector2Scale(Vector2Subtract(b, a), std::max(0.0f, best - 0.08f)));
    return any;
}
void StepCamera(float dt) {
    no::Player& p = Me();
    float k = std::clamp(p.drunk / 100, 0.0f, 1.0f);
    Vector3 head{p.pos.x, p.st == no::State::PassedOut ? 0.4f : 1.55f, p.pos.y};
    float lag = std::max(1.5f, 10 - 9 * k);   // (camera lag grows with the meter)
    S.camAt = Vector3Lerp(S.camAt, head, std::min(1.0f, dt * lag));
    if (Vector3Distance(S.camAt, head) > 4) S.camAt = head;
    Vector3 back{-cosf(S.camPitch) * cosf(S.camYaw), -sinf(S.camPitch), -cosf(S.camPitch) * sinf(S.camYaw)};
    Vector3 right{-sinf(S.camYaw), 0, cosf(S.camYaw)};
    Vector3 eye = Vector3Add(Vector3Add(S.camAt, Vector3Scale(back, S.camDist)), Vector3Scale(right, 0.7f));   // (over the right shoulder)
    Vector2 hit; if (WallBetween({S.camAt.x, S.camAt.z}, {eye.x, eye.z}, &hit)) { eye.x = hit.x; eye.z = hit.y; }
    eye.y = std::clamp(eye.y, 0.4f, 3.1f);
    S.cam.position = eye;
    S.cam.target = Vector3Add(Vector3Add(S.camAt, Vector3Scale(right, 0.7f)), Vector3Scale(back, -4));   // (looking past you, so you stand left of the middle)
    // the lurch: the room tilts with the sway above 40, more as the meter climbs
    S.rollK += ((k > 0.4f ? (k - 0.4f) * 0.25f * sinf(p.swayPh * 0.9f) : 0) + p.lurch * 0.05f - S.rollK) * std::min(1.0f, dt * 3);
    S.cam.up = Vector3Normalize(Vector3Add({0, 1, 0}, Vector3Scale(right, S.rollK)));
    S.cam.fovy = 58 + (k > 0.6f ? 3 * sinf(S.t * 1.3f) * (k - 0.6f) * 2.5f : 0);
    S.cam.projection = CAMERA_PERSPECTIVE;
}
void Render(float dt) {
    StepCamera(dt);
    const no::Night& n = S.N;
    rt::SceneLight L;
    // gaslight: warm amber from the lamps, a cool blue at the windows, the room dimming as the night goes on (doc p. 6)
    float late = std::clamp((n.Hour() - 19) / 8, 0.0f, 1.0f);
    L.fog = {30, 22, 18, 255}; L.fogDensity = 0.012f + 0.01f * late;
    L.fill = {60, 44, 36, 255}; L.rim = {80, 110, 150, 255}; L.key = {255, 200, 140, 255};
    L.surfaceY = 1e5f; L.time = S.t;
    L.lampPos = {Me().pos.x + 1, 3.2f, Me().pos.y - 1}; L.lampDir = {0, -1, 0.05f}; L.lampRange = 9; L.lampCone = 0.9f;
    L.moonDir = Vector3Normalize({0.3f, -1, 0.4f}); L.moon = {255, 200, 150, 255}; L.moonK = 0.35f - 0.15f * late;
    L.ambK = 0.75f - 0.25f * late; L.skyAmb = {120, 90, 70, 255}; L.seaAmb = {40, 30, 26, 255};
    L.outline = 0.7f; L.outlineTint = {24, 16, 12, 255}; L.stipple = 0.4f; L.grain = 0.4f; L.aoK = 0.4f; L.aoRadius = 0.5f;
    L.filmic = 0.3f; L.saturation = 1.1f;
    // the nearest lamps light the room
    std::vector<std::pair<float, Vector3>> near;
    for (const auto& l : no::D().bar.lamps) near.push_back({Vector2Distance({l.x, l.z}, Me().pos), l});
    std::sort(near.begin(), near.end(), [](auto& a, auto& b) { return a.first < b.first; });
    for (size_t i = 0; i < near.size() && i < 6; i++) L.AddPoint(near[i].second, 9, {255, 190, 120, 255}, 0.9f - 0.3f * late);
    rt::ApplyGameQuality();
    rt::RenderBegin(S.cam, L);
    DrawBar(n);
    DrawPeople(n);
    rt::RenderEnd();
}

// ---------------------------------------------------------------- input and the HUD
void Gather(float dt) {
    no::Player& p = Me();
    no::Input& in = p.in;
    in.moveX = in.moveZ = 0; in.run = false;
    if (IsKeyPressed(KEY_H)) S.help = !S.help;
    bool canMove = p.st == no::State::Active && !S.menu && !S.shot;
    Vector2 md = MouseLook(!S.shot && !S.menu && !S.N.over);
    S.camYaw += md.x * 0.0025f; S.camPitch = std::clamp(S.camPitch - md.y * 0.002f, -0.9f, 0.35f);
    float wheel = GetMouseWheelMove(); S.camDist = std::clamp(S.camDist - wheel * 0.4f, 1.6f, 6.0f);
    if (canMove) {
        Vector2 f{cosf(S.camYaw), sinf(S.camYaw)}, r{-sinf(S.camYaw), cosf(S.camYaw)};
        Vector2 w{0, 0};
        if (IsKeyDown(KEY_W)) w = Vector2Add(w, f); if (IsKeyDown(KEY_S)) w = Vector2Subtract(w, f);
        if (IsKeyDown(KEY_D)) w = Vector2Add(w, r); if (IsKeyDown(KEY_A)) w = Vector2Subtract(w, r);
        in.moveX = w.x; in.moveZ = w.y; in.run = IsKeyDown(KEY_LEFT_SHIFT);
    }
    // E: the menu at the bar or the hatch; walk home at the door
    if (IsKeyPressed(KEY_E) && p.st == no::State::Active) {
        if (S.menu) S.menu = false;
        else if (S.N.NearServe(p) || S.N.NearHatch(p)) S.menu = true;
        else if (S.N.NearDoor(p)) in.leave = true;
    }
    if (S.menu && (IsKeyPressed(KEY_ESCAPE) || !(S.N.NearServe(p) || S.N.NearHatch(p)))) S.menu = false;
    (void)dt;
}
void Bar(float x, float y, float w, float h, float k, Color c) { DrawRectangleRounded({x, y, w, h}, 0.5f, 6, Fade(Color{20, 12, 8, 255}, 0.75f)); if (k > 0) DrawRectangleRounded({x + 2, y + 2, std::max(2.0f, (w - 4) * std::clamp(k, 0.0f, 1.0f)), h - 4}, 0.5f, 6, c); }
void DrawHud() {
    no::Night& n = S.N; no::Player& p = Me();
    Color ink{250, 238, 214, 255}, dim{210, 190, 160, 255}, brass{230, 190, 110, 255};
    DrawTextCenteredBold(n.Clock(), SCREEN_W / 2.0f, 12, 24, n.Hour() >= no::D().lastCallHour ? Color{255, 160, 120, 255} : ink);
    if (n.Hour() >= no::D().lastCallHour) DrawTextCentered("LAST CALL: prices double", SCREEN_W / 2.0f, 40, 14, Color{255, 170, 130, 255});
    Txt(no::RoomAt(p.pos), 18, 14, 16, dim);
    // the meter: the band, charisma and toughness (the trade, always the same)
    {
        float x = SCREEN_W - 300, y = 14;
        DrawRectangleRounded({x - 10, y - 4, 292, 128}, 0.08f, 6, Fade(Color{20, 12, 8, 255}, 0.7f));
        const no::Band& b = n.BandOf(p);
        Color bc = p.drunk < 20 ? Color{170, 220, 200, 255} : p.drunk < 40 ? Color{240, 210, 120, 255} : p.drunk < 60 ? Color{240, 160, 90, 255} : p.drunk < 80 ? Color{230, 100, 80, 255} : Color{200, 60, 120, 255};
        TxtBold(TextFormat("%s  %.0f", b.state.c_str(), p.drunk), x, y, 18, bc);
        Bar(x, y + 24, 270, 14, p.drunk / 100, bc);
        Txt(TextFormat("charisma %.0f%%   toughness %.0f%%", n.Charisma(p) * 100, n.Toughness(p) * 100), x, y + 44, 15, ink);
        Txt(TextFormat("wages %.0f   tab %.0f   (%.0f left)", p.money, p.tab, p.money - p.tab), x, y + 66, 15, ink);
        std::string fx;
        if (p.charBuffT > 0) fx += "confident  "; if (p.toughBuffT > 0) fx += "steady fists  "; if (p.honestT > 0) fx += "honest  "; if (p.visionsT > 0) fx += "visions  "; if (p.shakesT > 0) fx += "the shakes  "; if (p.hiccup) fx += "hiccups";
        Txt(fx, x, y + 90, 13, dim);
    }
    // the prompts
    if (p.st == no::State::Active && !S.menu) {
        const char* prompt = n.NearServe(p) ? "E: order at the bar" : n.NearHatch(p) ? "E: order food" : n.NearDoor(p) ? "E: walk home (ends your night)" : nullptr;
        if (prompt) DrawTextCenteredBold(prompt, SCREEN_W / 2.0f, SCREEN_H - 90, 18, brass);
    }
    if (p.st == no::State::Vomiting) DrawTextCenteredBold("...", SCREEN_W / 2.0f, SCREEN_H / 2.0f + 40, 30, Color{180, 220, 120, 255});
    // the menu: the bartender's chalkboard (or the cook's hatch)
    if (S.menu) {
        bool kitchen = n.NearHatch(p);
        const auto& dr = no::D().drinks;
        std::vector<int> items; for (int i = 0; i < (int)dr.size(); i++) if ((dr[i].where == "kitchen") == kitchen) items.push_back(i);
        Rectangle r{SCREEN_W / 2.0f - 300, 110, 600, 70.0f + 34 * items.size()};
        DrawRectangleRounded(r, 0.05f, 6, Fade(Color{24, 30, 26, 255}, 0.95f));
        DrawRectangleRoundedLinesEx(r, 0.05f, 6, 3, Color{120, 90, 60, 255});
        DrawTextCenteredBold(kitchen ? "Tam's hatch (he hates you)" : "Tonight at the Gull", r.x + r.width / 2, r.y + 12, 22, Color{240, 240, 230, 255});
        for (int k = 0; k < (int)items.size(); k++) {
            const no::DrinkDef& d = dr[items[k]];
            Rectangle row{r.x + 16, r.y + 50 + k * 34.0f, r.width - 32, 30};
            bool hov = CheckCollisionPointRec(GetMousePosition(), row);
            if (hov) DrawRectangleRounded(row, 0.2f, 6, Fade(WHITE, 0.08f));
            Txt(TextFormat("%d. %s", k + 1, d.name.c_str()), row.x + 6, row.y + 6, 17, Color{236, 236, 226, 255});
            Txt(TextFormat("%s%.0f", d.drunk >= 0 ? "+" : "", d.drunk), row.x + 330, row.y + 6, 15, d.drunk > 0 ? Color{240, 170, 120, 255} : Color{160, 220, 190, 255});
            float pr = n.PriceOf(items[k]);
            Txt(pr > 0 ? TextFormat("%.0f", pr) : "free", row.x + 400, row.y + 6, 17, Color{240, 210, 140, 255});
            if (hov && !d.line.empty()) DrawTextCentered(d.line, SCREEN_W / 2.0f, r.y + r.height + 8, 14, dim);
            if ((hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) || IsKeyPressed(KEY_ONE + k)) { p.in.order = items[k]; S.menu = false; }
        }
    }
    // the room's talk: the bartender, the toasts
    { float y = SCREEN_H - 140; int shown = 0; for (int i = (int)n.say.size() - 1; i >= 0 && shown < 4; i--, shown++) { Txt(n.say[i], 18, y, 15, Fade(ink, 1 - shown * 0.2f)); y -= 20; } }
    if (S.help) {
        Rectangle r{18, 44, 330, 108};
        DrawRectangleRounded(r, 0.06f, 6, Fade(Color{20, 12, 8, 255}, 0.7f));
        const char* L[] = {"WASD: walk (Shift: hurry)   Mouse: look", "E: order at the bar or the hatch; at the door, go home", "Every drink: charisma down, toughness up", "H: hide this"};
        for (int i = 0; i < 4; i++) Txt(L[i], r.x + 12, r.y + 10 + i * 23, 14, i < 2 ? ink : dim);
    }
}
void DrawMorning(Game& g) {
    no::Night& n = S.N;
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{236, 226, 200, 255});
    // the newspaper
    DrawTextCenteredBold("THE HARBOUR GAZETTE", SCREEN_W / 2.0f, 40, 40, Color{30, 26, 22, 255});
    DrawRectangle(120, 92, SCREEN_W - 240, 3, Color{30, 26, 22, 255});
    DrawTextCenteredBold(n.Headline(), SCREEN_W / 2.0f, 110, 30, Color{40, 30, 24, 255});
    DrawRectangle(120, 152, SCREEN_W - 240, 1, Color{30, 26, 22, 255});
    float y = 180;
    for (const auto& p : n.players) {
        TxtBold(TextFormat("%s (the %s): %s", p.name.c_str(), no::CrewName(p.crew), no::EndingName(p.ending)), 140, y, 20, Color{40, 30, 24, 255});
        DrawWrapped(n.MorningLine(p), {160, y + 28, SCREEN_W - 340.0f, 44}, 16, Color{70, 56, 44, 255});
        Txt(TextFormat("walked in with 200, kept %.0f, %d drinks, peak %.0f drunk, kidneys %d   score %d", p.money, p.drinks, p.peakDrunk, p.kidneys, n.Score(p)), 160, y + 74, 15, Color{90, 74, 58, 255});
        y += 110;
    }
    if (Button({SCREEN_W / 2.0f - 120, SCREEN_H - 80.0f, 240, 40}, "Back to the arcade", true, 16)) { S.active = false; g.scene = Scene::Arcade; }
}

}  // namespace

void StartNightOff(Game& g, int crew) {
    std::string why;
    if (!rt::DataOk(&why)) { g.scene = Scene::Arcade; return; }
    no::Opts o; o.players = 1; o.seed = (uint32_t)GetRandomValue(1, 1 << 30);
    S.N.Init(o);
    S.N.players[0].crew = std::clamp(crew, 0, 5);
    S.me = 0; S.active = true; S.shot = false; S.menu = false; S.help = true; S.t = 0; S.walkPh.clear();
    S.camYaw = PI * 0.5f; S.camPitch = -0.28f; S.camDist = 3.2f; S.camAt = {Me().pos.x, 1.55f, Me().pos.y};
    g.scene = Scene::NightOff;
}
void LeaveNightOff(Game& g) { S.active = false; g.scene = Scene::Arcade; }
void SceneNightOff(Game& g) {
    if (!S.active) { StartNightOff(g, 0); if (!S.active) return; }
    float dt = S.shot ? 1 / 60.0f : std::min(GetFrameTime(), 1 / 30.0f);
    if (S.N.over) { DrawMorning(g); return; }
    Gather(dt);
    if (!S.shot) S.N.Step(dt);
    S.t += dt;
    Render(dt);
    DrawHud();
}
void NightOffMenuTick(float) {}
// --shots: 0 walking in at 7, 1 at the bar ordering (the menu), 2 hammered at midnight in the games room, 3 the snug,
// 4 passed out on the floor, 5 the morning paper
void DebugNightOffShot(Game& g, int which) {
    StartNightOff(g, which % 6);
    S.shot = true; S.help = which == 0;
    no::Night& n = S.N; no::Player& p = Me();
    auto at = [&](float x, float z, float yaw, float camYaw, float drunk) { p.pos = {x, z}; p.yaw = yaw; S.camYaw = camYaw; p.drunk = drunk; S.camAt = {x, 1.55f, z}; };
    if (which == 0) at(19.5f, 2.5f, PI * 0.5f, PI * 0.5f, 0);
    if (which == 1) { at(16.8f, 8.2f, PI * 0.5f, PI * 0.45f, 22); S.menu = true; n.t = 60 * no::SECONDS_PER_GAME_MINUTE * 1.5f; }
    if (which == 2) { at(6, 7, PI, PI * 0.95f, 72); n.t = 60 * no::SECONDS_PER_GAME_MINUTE * 5; p.swayPh = 1.2f; p.lurch = 0.6f; S.rollK = 0.12f; }
    if (which == 3) { at(34, 4, 0, -0.3f, 30); S.camPitch = -0.2f; }
    if (which == 4) { at(18, 6, 0.3f, PI * 0.3f, 100); n.Leave(p, no::E_PASSED_OUT, ""); }
    if (which == 5) { p.drinks = 7; p.peakDrunk = 88; n.Leave(p, no::E_PASSED_OUT, ""); n.over = true; }
    for (int i = 0; i < 30; i++) StepCamera(1 / 60.0f);
}
