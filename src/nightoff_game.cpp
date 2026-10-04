// A Night Off's scene (arcade game 6), stage 1: the Sodden Gull as a 3D space drawn with Red Tide's inked renderer under
// warm gaslight, the crew in shore clothes on the shared humanoid rig (figure3d), an over-the-shoulder camera with a free
// orbit, the drunk meter's animation (the sway, the weave, stumbles, the lean, a lagging and rolling camera), the
// bartender and his menu, and the morning-after screen. The rules are nightoff.cpp (headless).
#include "game.h"
#include "nightoff.h"
#include "nightoff_gamesui.h"
#include "nightoff_net.h"
#include "arcade_session.h"
#include "figure3d.h"
#include "redtide_render.h"
#include "input.h"
#include "sound.h"
#include <set>
#include "raymath.h"
#include <algorithm>
#include <cmath>

namespace {

struct NightScene {
    bool active = false, shot = false;
    no::Night N;                  // (our own night: solo, or a guest's mirror of the host's)
    arcade::Session* net = nullptr; no::Night* live = nullptr;   // (a networked night: the host draws its real night)
    std::string netName; int netCrew = 0; bool helloSent = false; int seenVersion = -1;
    no::NightProfile prof; bool profSaved = false; std::vector<std::string> remembered;   // (the profile: tomorrow's carry-overs)
    bool chatting = false; std::string chatBuf;
    bool wares = false;           // (the quiet man's list is open)
    int me = 0;
    float camYaw = PI * 0.5f, camPitch = -0.28f, camDist = 3.2f;
    Vector3 camAt{};          // the camera's lagging focus
    float t = 0, rollK = 0;
    bool menu = false, help = true;
    std::vector<float> walkPh;
    Camera3D cam{};
};
NightScene S;
bool gShotStart = false;   // (a debug shot is starting: no profile from disk)
no::Night& NW() { return S.live ? *S.live : S.N; }

Color Mix(Color a, Color b, float k) { k = std::clamp(k, 0.0f, 1.0f); return {(unsigned char)(a.r + (b.r - a.r) * k), (unsigned char)(a.g + (b.g - a.g) * k), (unsigned char)(a.b + (b.b - a.b) * k), 255}; }
Color Shade(Color c, float k) { return {(unsigned char)std::clamp(c.r * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.g * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.b * k, 0.0f, 255.0f), c.a}; }
no::Player& Me() { return NW().players[std::clamp(S.me, 0, (int)NW().players.size() - 1)]; }

// ---------------------------------------------------------------- the crew (doc p. 25: the Nautilus crew's silhouettes in shore clothes)
const Model* CrewModel(int crew) {
    static const char* F[6] = {"shared/crew/crew_diver.glb", "shared/crew/crew_bosun.glb", "shared/crew/crew_angler.glb", "shared/crew/crew_bosun.glb", "shared/crew/crew_medic.glb", "shared/crew/crew_medic.glb"};
    return rt::LoadAsset(F[std::clamp(crew, 0, 5)]);
}
struct Clothes { Color top, trousers, hat, skin; float build; };
// a patron actually sat down (stools, the snug, the card and dining tables; at the games, the dance floor and the corner they stand)
bool Seated(const no::Patron& c) { return c.sitting && (c.seatKind == "stool" || c.seatKind == "snug" || c.seatKind == "cards" || c.seatKind == "tables"); }
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
Vector2 HallucAt(const no::Player& me, int k) {   // (where the k-th person who isn't there stands: near you, drifting)
    uint32_t s = me.hallucSeed * (2654435761u + k * 40503u); float a = (s % 6283) / 1000.0f + S.t * 0.05f * (k % 2 ? 1 : -1), r = 2.2f + (s >> 13) % 100 / 60.0f;
    Vector2 at{me.pos.x + cosf(a) * r, me.pos.y + sinf(a) * r}; NW().Collide(at, 0.3f); return at;
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
    for (const auto& f : no::D().bar.floors) if (f.first == k) return f.second;   // (the bar's own floors: the Monkey's marble, carpet and roof tiles)
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
        slab(0, 1.1f, B.wainscot, 0.32f);
        slab(1.1f, B.wallH, outer ? B.outer : B.plaster, 0.3f);
        slab(1.08f, 1.16f, B.rail, 0.36f);
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
        else if (k == "table") {}   // (a prop: see DrawProps)
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
        // the Brass Monkey's furniture: armchairs, the chessboard, the stuffed marlin, the library shelves, the cage, the wine racks, the chaise, the velvet rope, the telescope, the roof's parapet
        else if (k == "armchair") { rt::DrawWorldCube({c.x, 0.25f, c.z}, {s.x, 0.5f, s.z}, {110, 30, 34, 255}); rt::DrawWorldCube({c.x + s.x * 0.4f, 0.65f, c.z}, {0.2f, 0.8f, s.z}, {96, 26, 30, 255}); for (float sd : {-1.0f, 1.0f}) rt::DrawWorldCube({c.x, 0.55f, c.z + sd * s.z * 0.42f}, {s.x, 0.25f, 0.16f}, {96, 26, 30, 255}); }
        else if (k == "chess") { rt::DrawWorldCube({c.x, 0.37f, c.z}, {0.15f, 0.74f, 0.15f}, {60, 40, 30, 255}); for (int i = 0; i < 4; i++) for (int j = 0; j < 4; j++) rt::DrawWorldCube({b.r.x + 0.11f + i * 0.22f, 0.76f, b.r.y + 0.11f + j * 0.22f}, {0.22f, 0.03f, 0.22f}, (i + j) % 2 ? Color{230, 220, 200, 255} : Color{40, 30, 26, 255}); rt::DrawWorldCube({c.x - 0.15f, 0.84f, c.z}, {0.05f, 0.12f, 0.05f}, {240, 236, 220, 255}); rt::DrawWorldCube({c.x + 0.2f, 0.84f, c.z + 0.1f}, {0.05f, 0.14f, 0.05f}, {30, 26, 24, 255}); }
        else if (k == "marlin") {   // the stuffed marlin on its plaque over the fire: it speaks at the seance
            rt::DrawWorldCube({c.x, 2.2f, c.z}, {0.08f, 0.7f, s.z}, {90, 60, 36, 255});
            rt::DrawWorldCube({c.x - 0.12f, 2.25f, c.z}, {0.16f, 0.32f, s.z * 0.62f}, {50, 80, 120, 255});
            rt::DrawWorldCube({c.x - 0.12f, 2.3f, c.z - s.z * 0.42f}, {0.06f, 0.05f, 0.5f}, {40, 60, 90, 255});   // (the bill)
            rt::DrawWorldCube({c.x - 0.12f, 2.42f, c.z + s.z * 0.18f}, {0.06f, 0.24f, 0.36f}, {40, 60, 90, 255});   // (the sail)
            rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.05f, 0.05f, 0.05f), MatrixTranslate(c.x - 0.22f, 2.3f, c.z - s.z * 0.22f)), {255, 220, 150, 255}, 0.8f + (NW().Hour() >= 24 && NW().Hour() < 24.4f ? 1.5f : 0));   // (its glass eye)
        }
        else if (k == "shelves") { rt::DrawWorldCube(c, s, {60, 40, 28, 255}); for (int r = 0; r < 4; r++) for (int i = 0; i < 9; i++) rt::DrawWorldCube({b.r.x + b.r.width + 0.03f, 0.35f + r * 0.6f, b.r.y + 0.2f + i * (b.r.height - 0.4f) / 8}, {0.06f, 0.4f, 0.22f}, (i * 3 + r) % 4 == 0 ? Color{120, 40, 40, 255} : (i + r) % 3 == 0 ? Color{40, 70, 50, 255} : Color{140, 110, 60, 255}); }
        else if (k == "cage") { rt::DrawWorldCube({c.x, 0.55f, c.z}, {s.x, 1.1f, s.z}, {70, 50, 34, 255}); for (int i = 0; i < 7; i++) rt::DrawWorldCube({b.r.x + i * s.x / 6, 1.65f, b.r.y - 0.02f}, {0.03f, 1.1f, 0.03f}, {200, 160, 80, 255}); }
        else if (k == "wine") { rt::DrawWorldCube(c, s, {70, 46, 30, 255}); for (int r = 0; r < 5; r++) for (int i = 0; i < 14; i++) rt::DrawWorldCube({b.r.x + 0.2f + i * (s.x - 0.4f) / 13, 0.3f + r * 0.4f, b.r.y - 0.02f}, {0.08f, 0.08f, 0.08f}, (i + r) % 3 ? Color{60, 20, 30, 255} : Color{140, 150, 90, 255}); }
        else if (k == "chaise") { rt::DrawWorldCube({c.x, 0.3f, c.z}, {s.x, 0.4f, s.z}, {180, 140, 150, 255}); rt::DrawWorldCube({b.r.x + 0.15f, 0.65f, c.z}, {0.3f, 0.5f, s.z}, {170, 128, 140, 255}); }
        else if (k == "rope") { rt::DrawWorldCube({c.x, 0.45f, c.z}, {0.08f, 0.9f, 0.08f}, {210, 170, 80, 255}); rt::DrawWorldCube({c.x, 0.92f, c.z}, {0.14f, 0.06f, 0.14f}, {230, 190, 100, 255}); rt::DrawWorldCube({c.x + (c.x < 19.5f ? 0.8f : -0.8f), 0.75f, c.z}, {1.6f, 0.06f, 0.06f}, {150, 20, 30, 255}); }
        else if (k == "telescope") { rt::DrawWorldCube({c.x, 0.6f, c.z}, {0.06f, 1.2f, 0.06f}, {60, 50, 40, 255}); rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(1.1f, 0.14f, 0.14f), MatrixRotateZ(0.35f)), MatrixTranslate(c.x, 1.35f, c.z)), {200, 160, 80, 255}); }
        else if (k == "parapet") rt::DrawWorldCube(c, s, {120, 110, 104, 255});
        else rt::DrawWorldCube(c, s, {100, 70, 50, 255});
    }
    // the stools along the bar, the bottles on the back shelf, the dartboard, the jukebox

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
        rt::DrawWorldCube({r.r.x + r.r.width / 2, B.wallH + 0.05f, r.r.y + r.r.height / 2}, {r.r.width, 0.1f, r.r.height}, B.ceiling);
        for (float x = r.r.x + 1.5f; x < r.r.x + r.r.width; x += 3) rt::DrawWorldCube({x, B.wallH - 0.12f, r.r.y + r.r.height / 2}, {0.25f, 0.24f, r.r.height}, B.beam);
    }
    if (B.key == "monkey") {
        // the long bar's mirror behind the bottles; chandeliers' crystals round each lamp
        rt::DrawCubeGlow(MatrixMultiply(MatrixScale(9.0f, 1.5f, 0.05f), MatrixTranslate(17, 2.1f, 11.93f)), {150, 170, 190, 255}, 0.25f);
        for (const auto& l : B.lamps) for (int i = 0; i < 8; i++) { float a = i * PI / 4 + S.t * 0.1f; rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.05f, 0.09f, 0.05f), MatrixTranslate(l.x + cosf(a) * 0.32f, l.y - 0.12f, l.z + sinf(a) * 0.32f)), {255, 240, 210, 255}, 0.7f); }
    }
    if (B.roof) {
        // the roof: a parapet round the terrace, the harbour far below, and the Sodden Gull across the water, lit
        rt::DrawWorldCube({-0.25f, 0.55f, 40}, {0.5f, 1.1f, 20}, {120, 110, 104, 255}); rt::DrawWorldCube({40.25f, 0.55f, 40}, {0.5f, 1.1f, 20}, {120, 110, 104, 255});
        rt::DrawWorldCube({20, -14, 110}, {260, 0.4f, 120}, {22, 40, 64, 255});
        for (int i = 0; i < 40; i++) rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.9f, 0.05f, 0.25f), MatrixTranslate(36 + (i % 8) * 2.6f + sinf(S.t * 0.7f + i) * 0.4f, -13.7f, 66 + (i / 8) * 3.0f)), {255, 190, 110, 255}, 0.6f);   // (the Gull's windows on the water)
        rt::DrawWorldCube({48, -6, 88}, {30, 16, 14}, {70, 52, 40, 255});
        rt::DrawWorldCube({48, 3.4f, 88}, {32, 2.8f, 16}, {44, 34, 28, 255});
        for (int i = 0; i < 7; i++) for (int j = 0; j < 2; j++) rt::DrawCubeGlow(MatrixMultiply(MatrixScale(2.2f, 2.0f, 0.3f), MatrixTranslate(36 + i * 4.0f, -9.0f + j * 5.0f, 80.9f)), {255, 190, 110, 255}, 1.6f + 0.2f * sinf(S.t + i));
        rt::DrawCubeGlow(MatrixMultiply(MatrixScale(9, 1.4f, 0.3f), MatrixTranslate(48, 0.9f, 80.9f)), {255, 120, 90, 255}, 1.8f);   // (its sign: THE SODDEN GULL)
    }
    // the yard's string lights, the street's lamp posts
    for (int i = 0; i < 24; i++) { float x = 1 + i * 1.65f; rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.08f, 0.08f, 0.08f), MatrixTranslate(x, 3.0f - 0.3f * sinf(i * 0.26f * PI), 38)), i % 3 == 0 ? Color{255, 120, 90, 255} : Color{255, 220, 140, 255}, 1.6f); }
    for (float x : {6.0f, 30.0f}) { rt::DrawWorldCube({x, 1.6f, -5}, {0.12f, 3.2f, 0.12f}, {30, 30, 34, 255}); rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.3f, 0.4f, 0.3f), MatrixTranslate(x, 3.3f, -5)), {255, 210, 140, 255}, 1.8f); }
    (void)n;
}
// a fighter's pose: fists up in a brawl, the haymaker's windup (seen coming), the swing, a block, a flinch
void FightPose(const no::Combat& C, fig::Pose& P, float& lean) {
    if (C.brawl >= 0) { P.grip = 1; P.elbow = std::max(P.elbow, 0.75f); P.reach = std::max(P.reach, 0.2f); }
    if (C.windT > 0) { bool hay = C.move == no::MV_HAYMAKER; P.reach = hay ? 0.05f : 0.3f; P.elbow = 1; P.grip = 1; lean -= hay ? 0.18f : 0.05f; }
    if (C.swingT > 0) { P.swingT = 1 - C.swingT; P.reach = 0.4f + 0.6f * C.swingT; P.elbow = 0.2f; P.grip = 1; lean += 0.12f * C.swingT; }
    if (C.blockT > 0) { P.elbow = 1; P.reach = 0.3f; P.grip = 1; P.nod = 0.2f; }
    if (C.grabbing.Valid()) { P.reach = 0.85f; P.elbow = 0.35f; P.grip = 1; }
    if (C.hitT > 0) { lean -= 0.5f * C.hitT; P.nod = -0.35f; P.shout = 0.6f; }
    if (C.stunT > 0) { P.nod = 0.4f; lean += 0.1f * sinf(S.t * 9); }
    if (C.smashT > 0) { P.reach = 0.6f; P.elbow = 0.9f - C.smashT * 0.8f; P.grip = 1; }
}
bool Floored(const no::Combat& C) { return C.downT > 0 || C.fallT > 0; }
void DrawProps(const no::Night& n) {
    auto cubeR = [](Vector3 c, Vector3 s, float yaw, float tilt, Color col) { rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(s.x, s.y, s.z), MatrixRotateZ(tilt)), MatrixRotateY(-yaw)), MatrixTranslate(c.x, c.y, c.z)), col); };
    for (size_t i = 0; i < n.props.size(); i++) {
        const no::Prop& p = n.props[i]; const std::string& k = p.kind;
        if (p.state == no::PS_GONE) continue;
        bool broken = p.state == no::PS_BROKEN, over = p.state == no::PS_OVER;
        float h = (float)(i * 2654435761u % 1000) / 1000.0f;   // (a scatter per prop)
        Vector3 at = p.pos;
        if (k == "table") {
            Color top{96, 62, 38, 255}, leg{60, 40, 28, 255};
            float th = std::max(0.06f, 0.0f), H = 0.8f;
            if (broken) { for (int j = 0; j < 4; j++) cubeR({at.x + (j % 2 - 0.5f) * p.size.x * 0.5f, 0.05f, at.z + (j / 2 - 0.5f) * p.size.y * 0.5f}, {p.size.x * 0.45f, 0.05f, 0.18f}, h * 6 + j, 0.2f * j, top); continue; }
            if (over) { cubeR({at.x + cosf(p.yaw) * 0.3f, p.size.x / 2, at.z + sinf(p.yaw) * 0.3f}, {0.06f, p.size.x, p.size.y}, p.yaw, 0, top); cubeR({at.x - cosf(p.yaw) * 0.1f, 0.45f, at.z - sinf(p.yaw) * 0.1f}, {0.8f, 0.1f, 0.1f}, p.yaw, 0, leg); continue; }
            rt::DrawWorldCube({at.x, H - th / 2, at.z}, {p.size.x, th, p.size.y}, top); rt::DrawWorldCube({at.x, H / 2, at.z}, {0.12f, H, 0.12f}, leg); continue;
        }
        if (k == "chair" || k == "stool") {
            Color seat = k == "stool" ? Color{130, 40, 40, 255} : Color{110, 72, 44, 255}, wood{70, 46, 30, 255};
            if (broken) { for (int j = 0; j < 3; j++) cubeR({at.x + (j - 1) * 0.2f, 0.04f, at.z + (h - 0.5f) * 0.3f}, {0.35f, 0.04f, 0.06f}, h * 9 + j * 1.3f, 0, j ? wood : seat); continue; }
            if (p.state == no::PS_HELD || p.state == no::PS_FLYING) { cubeR({at.x, at.y + 0.2f, at.z}, {0.42f, 0.06f, 0.42f}, p.yaw, PI * 0.5f, seat); cubeR({at.x, at.y - 0.1f, at.z}, {0.06f, 0.6f, 0.06f}, p.yaw, PI * 0.5f, wood); continue; }
            if (over) { cubeR({at.x, 0.22f, at.z}, {0.42f, 0.42f, 0.07f}, p.yaw + h, 0, seat); cubeR({at.x + 0.25f, 0.06f, at.z}, {0.5f, 0.06f, 0.06f}, p.yaw + h, 0, wood); continue; }
            float hs = k == "stool" ? 0.76f : 0.46f;
            rt::DrawWorldCube({at.x, hs / 2, at.z}, {0.07f, hs, 0.07f}, wood); rt::DrawWorldCube({at.x, hs, at.z}, {0.42f, 0.07f, 0.42f}, seat);
            if (k == "chair") cubeR({at.x - cosf(p.yaw) * 0.19f, hs + 0.25f, at.z - sinf(p.yaw) * 0.19f}, {0.05f, 0.5f, 0.42f}, p.yaw, 0, wood);
            continue;
        }
        if (k == "window") { if (broken) { rt::DrawWorldCube({at.x, at.y, 0.2f}, {1.5f, 1.2f, 0.08f}, {14, 18, 26, 255}); for (int j = 0; j < 5; j++) cubeR({at.x - 0.6f + j * 0.3f, at.y + 0.5f - (j % 3) * 0.15f, 0.22f}, {0.04f, 0.3f + 0.1f * (j % 2), 0.04f}, 0, 0.5f * (j % 3 - 1), {150, 180, 210, 255}); for (int j = 0; j < 6; j++) rt::DrawWorldCube({at.x - 0.7f + j * 0.28f, 0.02f, 0.6f + (j % 3) * 0.25f}, {0.12f, 0.02f, 0.08f}, {150, 180, 210, 255}); } continue; }
        if (k == "slot") { if (broken) { rt::DrawWorldCube({at.x + 0.42f, 1.3f, at.z}, {0.06f, 0.5f, 0.62f}, {20, 16, 16, 255}); if (fmodf(S.t + h * 3, 1.3f) < 0.08f) rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.08f, 0.08f, 0.08f), MatrixTranslate(at.x + 0.5f, 1.4f, at.z)), {255, 230, 140, 255}, 2.0f); } continue; }
        if (k == "kitty") { if (p.state == no::PS_OK) { rt::DrawWorldCube({at.x, at.y + 0.05f, at.z}, {0.3f, 0.1f, 0.2f}, {200, 180, 120, 255}); rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.26f, 0.04f, 0.16f), MatrixTranslate(at.x, at.y + 0.12f, at.z)), {120, 200, 120, 255}, 0.5f); } continue; }
        if (k == "bike") { if (p.state != no::PS_OK) continue; Color m{40, 40, 46, 255}, chrome{180, 180, 190, 255};
            cubeR({at.x, 0.55f, at.z}, {1.6f, 0.35f, 0.3f}, p.yaw, 0, m); cubeR({at.x + cosf(p.yaw) * 0.75f, 0.32f, at.z + sinf(p.yaw) * 0.75f}, {0.6f, 0.6f, 0.12f}, p.yaw, 0, {20, 20, 20, 255});
            cubeR({at.x - cosf(p.yaw) * 0.75f, 0.32f, at.z - sinf(p.yaw) * 0.75f}, {0.6f, 0.6f, 0.12f}, p.yaw, 0, {20, 20, 20, 255}); cubeR({at.x + cosf(p.yaw) * 0.6f, 0.95f, at.z + sinf(p.yaw) * 0.6f}, {0.08f, 0.08f, 0.7f}, p.yaw, 0, chrome); continue; }
        if (k == "coffin") { if (p.state == no::PS_OK) { rt::DrawWorldCube({at.x, at.y + 0.18f, at.z}, {2.0f, 0.36f, 0.6f}, {50, 34, 26, 255}); rt::DrawWorldCube({at.x, at.y + 0.37f, at.z}, {2.05f, 0.04f, 0.65f}, {170, 140, 70, 255}); } continue; }
        if (k == "mirror") { rt::DrawWorldCube({at.x, at.y, at.z}, {0.9f, 0.7f, 0.04f}, broken ? Color{60, 64, 70, 255} : Color{170, 190, 200, 255}); continue; }
        if (k == "piano") { if (broken) for (int j = 0; j < 6; j++) cubeR({at.x - 1 + j * 0.4f, 0.1f, at.z + (j % 2) * 0.4f}, {0.5f, 0.12f, 0.2f}, j, 0.3f, {24, 20, 22, 255}); continue; }
        // the small things: glasses, bottles, the weapons
        if (broken) { if (k == "glass" || k == "bottle" || k == "broken") for (int j = 0; j < 4; j++) rt::DrawWorldCube({at.x + (j - 1.5f) * 0.09f, 0.015f, at.z + (h - 0.5f) * 0.2f + j * 0.03f}, {0.05f, 0.015f, 0.04f}, k == "glass" ? Color{210, 220, 220, 255} : Color{60, 120, 70, 255}); continue; }
        float tilt = over ? PI * 0.5f : 0;
        float y = over ? 0.05f : at.y;
        if (k == "glass") rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.08f, 0.12f, 0.08f), MatrixTranslate(at.x, y + 0.06f, at.z)), {220, 190, 120, 255}, 0.25f);
        else if (k == "bottle" || k == "broken") { cubeR({at.x, y + 0.13f, at.z}, {0.08f, k == "broken" ? 0.16f : 0.24f, 0.08f}, p.yaw, tilt, {50, 110, 60, 255}); if (k == "bottle") cubeR({at.x, y + 0.3f, at.z}, {0.035f, 0.1f, 0.035f}, p.yaw, tilt, {50, 110, 60, 255}); }
        else if (k == "cue" || k == "jagged") { float L = k == "cue" ? 1.45f : 0.75f; bool up = p.state == no::PS_OK; cubeR({at.x, up ? at.y : y + 0.03f, at.z}, {up ? 0.035f : L, up ? L : 0.035f, 0.035f}, p.yaw, p.state == no::PS_HELD ? -0.9f : 0, {170, 120, 70, 255}); }
        else if (k == "dart") cubeR({at.x, y, at.z}, {0.15f, 0.02f, 0.02f}, p.yaw, 0, {200, 200, 210, 255});
        else if (k == "club") cubeR({at.x, y + (over || p.state == no::PS_OK ? 0 : 0.2f), at.z}, {0.04f, 0.95f, 0.04f}, p.yaw, over ? PI * 0.5f : (p.state == no::PS_HELD ? -0.9f : 0.1f), {150, 150, 160, 255});
        else if (k == "pan") { cubeR({at.x, y + 0.03f, at.z}, {0.3f, 0.05f, 0.3f}, p.yaw, tilt, {40, 40, 44, 255}); cubeR({at.x + cosf(p.yaw) * 0.3f, y + 0.04f, at.z + sinf(p.yaw) * 0.3f}, {0.3f, 0.03f, 0.04f}, p.yaw, 0, {40, 40, 44, 255}); }
        else if (k == "knife") cubeR({at.x, y + 0.02f, at.z}, {0.28f, 0.02f, 0.04f}, p.yaw, 0, {200, 205, 215, 255});
        else if (k == "shotgun") cubeR({at.x, y + 0.05f, at.z}, {0.95f, 0.08f, 0.08f}, p.yaw, p.state == no::PS_HELD ? -0.15f : 0, {70, 50, 36, 255});
    }
    if (n.goatOn) {
        Color wool{226, 222, 210, 255}, horn{120, 110, 90, 255}; float gy = n.goatYaw, st = sinf(n.goatPh * 6) * 0.1f;
        auto gp = [&](float fx, float fy, float fz, Vector3 s, Color c) { Vector2 f{cosf(gy), sinf(gy)}, r{-sinf(gy), cosf(gy)}; rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(s.x, s.y, s.z), MatrixRotateY(-gy)), MatrixTranslate(n.goatPos.x + f.x * fx + r.x * fz, fy, n.goatPos.y + f.y * fx + r.y * fz)), c); };
        gp(0, 0.55f, 0, {0.75f, 0.32f, 0.32f}, wool); gp(0.45f, 0.78f, 0, {0.26f, 0.24f, 0.2f}, wool); gp(0.5f, 0.96f, 0.06f, {0.05f, 0.18f, 0.05f}, horn); gp(0.5f, 0.96f, -0.06f, {0.05f, 0.18f, 0.05f}, horn); gp(0.62f, 0.66f, 0, {0.06f, 0.14f, 0.06f}, wool);
        for (int j = 0; j < 4; j++) gp((j < 2 ? 0.25f : -0.25f) + (j % 2 ? st : -st), 0.2f, (j % 2 ? 0.1f : -0.1f), {0.06f, 0.4f, 0.06f}, {90, 80, 70, 255});
    }
    // rain over the yard (and on the street): streaks near the camera
    if (n.raining) for (int j = 0; j < 160; j++) {
        float u = fmodf(j * 0.6180339f, 1.0f), v = fmodf(j * 0.7548777f, 1.0f), fall = fmodf(S.t * 7 + j * 0.37f, 4.0f);
        Vector3 at{S.cam.position.x - 10 + u * 20, 4 - fall, S.cam.position.z - 10 + v * 20};
        if (!(at.z > 30 || at.z < 0 || at.x < 0)) continue;   // (outside only)
        rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.01f, 0.35f, 0.01f), MatrixTranslate(at.x, at.y, at.z)), {170, 190, 220, 255}, 0.5f);
    }
    // the alley dog: a scruffy brown dog of boxes, asleep by the bins until someone shares their chips
    const no::Dog& d = n.dog;
    Color fur{124, 86, 52, 255}, dark{70, 48, 30, 255};
    float yaw = d.yaw, sl = d.sleeping && d.owner < 0 ? 1.0f : 0.0f, step = sinf(d.walkPh) * 0.12f;
    auto part = [&](float fx, float fy, float fz, Vector3 s, Color c) { Vector2 f{cosf(yaw), sinf(yaw)}, r{-sinf(yaw), cosf(yaw)}; rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(s.x, s.y, s.z), MatrixRotateY(-yaw)), MatrixTranslate(d.pos.x + f.x * fx + r.x * fz, fy, d.pos.y + f.y * fx + r.y * fz)), c); };
    part(0, 0.42f - 0.22f * sl, 0, {0.62f, 0.26f, 0.28f}, fur);
    part(0.36f, 0.6f - 0.42f * sl, 0, {0.24f, 0.22f, 0.22f}, fur); part(0.5f, 0.56f - 0.42f * sl, 0, {0.12f, 0.1f, 0.12f}, dark);
    part(-0.36f, 0.55f - 0.3f * sl + 0.05f * sinf(S.t * 12) * (1 - sl), 0, {0.22f, 0.06f, 0.06f}, dark);   // (the tail; it wags when it is yours)
    if (sl < 1) for (int j = 0; j < 4; j++) part((j < 2 ? 0.22f : -0.22f) + (j % 2 ? step : -step), 0.16f, (j % 2 ? 0.1f : -0.1f), {0.07f, 0.32f, 0.07f}, dark);
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
    static const char* PM[5] = {"shared/crew/crew_diver.glb", "shared/crew/crew_bosun.glb", "shared/crew/crew_angler.glb", "shared/crew/crew_medic.glb", "shared/crew/crew_medic.glb"};
    for (const auto& c : n.patrons) {
        if (!c.inside || c.gone) continue;
        if (Vector3Distance({c.pos.x, 1, c.pos.y}, S.cam.position) > 40) continue;
        const Model* m = rt::LoadAsset(PM[std::clamp(c.look.model, 0, 4)]);
        Clothes cl{c.look.top, Shade(c.look.top, 0.45f), c.look.hat, c.reg >= 0 ? Color{(unsigned char)(170 + (c.reg * 37) % 70), (unsigned char)(120 + (c.reg * 53) % 70), (unsigned char)(90 + (c.reg * 29) % 60), 255} : Color{200, 156, 126, 255}, c.look.build};
        fig::Pose P; float spd = Vector2Length(c.vel);
        P.walk = std::clamp(spd / 1.4f, 0.0f, 1.0f); P.walkPh = c.walkPh; P.breathe = S.t * 1.3f + c.id;
        P.blink = fmodf(S.t + c.id * 0.37f, 3.7f) < 0.12f ? 1.0f : 0.0f;
        P.sit = Seated(c) ? 1.0f : 0.0f;
        if (c.talkingTo >= 0) { P.look = 0.1f * sinf(S.t * 2 + c.id); P.shout = 0.3f + 0.3f * sinf(S.t * 9 + c.id); }
        if (c.drinkT > 0) { P.reach = 0.25f; P.elbow = 0.8f; P.grip = 0.9f; }
        float lean = sinf(S.t * 0.9f + c.id) * 0.06f * std::clamp(c.drunk / 100, 0.0f, 1.0f);
        FightPose(c.fight, P, lean);
        if (Floored(c.fight)) P.sit = 0;
        float fy = 0; for (const auto& b : no::D().bar.boxes) if (b.kind == "stage" && CheckCollisionPointRec(c.pos, {b.r.x, b.r.y, b.r.width, b.r.height})) fy = b.h;
        if (fy > 0 && c.ev >= 0) { P.reach = 0.5f; P.elbow = 0.6f + 0.3f * sinf(S.t * 8 + c.id); P.grip = 0.9f; P.nod = 0.15f * sinf(S.t * 4 + c.id); }   // (playing)
        Vector3 feet{c.pos.x, fy, c.pos.y};
        DrawPerson(m, cl, feet, c.yaw, P, lean, 0, Floored(c.fight));
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
        FightPose(p.fight, P, lean);
        if (p.emoteT > 0) { switch (p.emote) { case 1: P.reach = 0.35f; P.elbow = 1; P.grip = 1; break; case 2: P.reach = 1; P.elbow = 0; break; case 3: P.shout = 0.8f; P.nod = -0.2f; lean -= 0.1f; break; case 4: P.elbow = 0.6f; P.reach = 0.15f; P.nod = 0.1f; break; case 5: P.grip = 1; P.elbow = 1; P.reach = 0.4f; break; } }
        DrawPerson(CrewModel(p.crew), ShoreClothes(p.crew), {p.pos.x, 0, p.pos.y}, p.yaw, P, lean, pitch, p.st == no::State::PassedOut || p.st == no::State::Down || p.fight.fallT > 0);
        // a glass in the hand while drinking
        if (p.st == no::State::Drinking) { Vector3 h{p.pos.x + cosf(p.yaw) * 0.3f, 1.2f + 0.3f * std::clamp((1 - p.actT / 2.5f) * 3, 0.0f, 1.0f), p.pos.y + sinf(p.yaw) * 0.3f}; rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.08f, 0.14f, 0.08f), MatrixTranslate(h.x, h.y, h.z)), {230, 170, 60, 255}, 0.5f); }
    }
    // Angler's Light: people who aren't there (only you see them; they drift, and vanish when you come close)
    for (int k = 0; k < 3; k++) {
        const no::Player& me = Me(); if (!me.hallucSeed) break;
        Vector2 at = HallucAt(me, k); if (Vector2Distance(at, me.pos) < 1.3f) continue;
        fig::Pose P; P.breathe = S.t; P.look = sinf(S.t * 0.7f + k);
        Clothes cl{{(unsigned char)(120 + 40 * k), 150, 200, 255}, {70, 80, 120, 255}, {60, 60, 90, 255}, {200, 210, 230, 255}, 1};
        DrawPerson(CrewModel((int)(me.hallucSeed >> (k * 3)) % 6), cl, {at.x, 0, at.y}, atan2f(me.pos.y - at.y, me.pos.x - at.x), P, 0.05f * sinf(S.t + k), 0, false);
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
    // in a conversation the camera swings round to frame you both (from the side, looking at the pair's middle)
    int partner = p.talk.patron >= 0 ? p.talk.patron : p.flirt.patron;
    if (partner >= 0 && partner < (int)NW().patrons.size()) {
        const no::Patron& c = NW().patrons[partner];
        float toC = atan2f(c.pos.y - p.pos.y, c.pos.x - p.pos.x), want = toC - 0.75f;
        S.camYaw += atan2f(sinf(want - S.camYaw), cosf(want - S.camYaw)) * std::min(1.0f, dt * 3);
        head = {(p.pos.x + c.pos.x) / 2 - 0.55f * cosf(S.camYaw + PI / 2), 1.45f, (p.pos.y + c.pos.y) / 2 - 0.55f * sinf(S.camYaw + PI / 2)};
    }
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
    const no::Night& n = NW();
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
    DrawProps(n);
    DrawPeople(n);
    rt::RenderEnd();
}

// ---------------------------------------------------------------- input and the HUD
void Gather(float dt) {
    no::Player& p = Me();
    no::Input& in = p.in;
    in.moveX = in.moveZ = 0; in.run = false; in.faceYaw = S.camYaw; in.cheat = IsKeyDown(KEY_V);
    if (S.chatting) { MouseLook(false); return; }   // (typing a line: the keys are words, not moves)
    if (IsKeyPressed(KEY_H)) S.help = !S.help;
    bool canMove = p.st == no::State::Active && !S.menu && !S.wares && !S.shot && !S.chatting && p.talk.patron < 0 && p.flirt.patron < 0 && p.leavingT <= 0 && !nog::Blocking(p);
    if (p.talk.patron >= 0 && IsKeyPressed(KEY_ESCAPE)) in.say = 6;
    if (p.flirt.patron >= 0 && IsKeyPressed(KEY_ESCAPE)) { if (p.flirt.offer) in.offer = 2; else in.flirtSay = 6; }
    Vector2 md = MouseLook(!S.shot && !S.menu && !S.wares && !NW().over && p.talk.patron < 0 && p.flirt.patron < 0 && !nog::Blocking(p));
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
    if (canMove) {   // fighting (doc p. 15): LMB jab, RMB haymaker, F grab (again: throw), G shove, Q block, Space dodge, R pick up / put down, X throw it, C smash a bottle on the bar
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) in.attack = no::MV_JAB;
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) in.attack = no::MV_HAYMAKER;
        if (IsKeyPressed(KEY_F)) in.attack = no::MV_GRAB;
        if (IsKeyPressed(KEY_G)) in.attack = no::MV_SHOVE;
        if (IsKeyPressed(KEY_X)) in.attack = no::MV_THROW;
        in.block = IsKeyDown(KEY_Q);
        if (IsKeyPressed(KEY_SPACE)) in.dodge = true;
        if (IsKeyPressed(KEY_R)) in.pickUp = true;
        if (IsKeyPressed(KEY_C)) in.smash = true;
        if (IsKeyPressed(KEY_T)) { int near = NW().NearestPatron(p, 1.8f); if (near >= 0) in.flirtWith = near; }
        for (int k = 0; k < 6; k++) if (IsKeyPressed(KEY_F5 + k)) in.emote = k + 1;   // (toast, point, laugh, shrug, fists up, fall over)
        if (in.attack) p.yaw = S.camYaw;   // (you swing where you're looking)
    }
    if (IsKeyPressed(KEY_E) && p.st == no::State::Active && !nog::Blocking(p)) {
        int mach = 0, gk = NW().NearGame(p, &mach);
        bool byDog = Vector2Distance(p.pos, NW().dog.pos) < 1.6f && NW().dog.owner != p.id;
        if (S.menu) S.menu = false;
        else if (byDog) in.feedDog = true;
        else if (NW().NearServe(p) || NW().NearHatch(p)) S.menu = true;
        else if (NW().NearDoor(p)) in.leave = true;
        else if (gk >= 0 && p.talk.patron < 0) { if (gk <= no::GK_GOLF) nog::OpenMenu(gk, mach); else { in.startGame = gk; in.gameMachine = mach; in.gameOpp = -1; in.gameStake = 0; } }
        else if (p.talk.patron < 0 && p.flirt.patron < 0) { int near = NW().NearestPatron(p, 1.8f); if (near >= 0) in.talkTo = near; }
    }
    if (S.menu && (IsKeyPressed(KEY_ESCAPE) || !(NW().NearServe(p) || NW().NearHatch(p)))) S.menu = false;
    (void)dt;
}
void Bar(float x, float y, float w, float h, float k, Color c) { DrawRectangleRounded({x, y, w, h}, 0.5f, 6, Fade(Color{20, 12, 8, 255}, 0.75f)); if (k > 0) DrawRectangleRounded({x + 2, y + 2, std::max(2.0f, (w - 4) * std::clamp(k, 0.0f, 1.0f)), h - 4}, 0.5f, 6, c); }
void DrawHud() {
    no::Night& n = NW(); no::Player& p = Me();
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
        for (int w = 0; w < no::W_COUNT; w++) if (p.wareT[w] > 0) fx += "  " + no::Wares()[w].name;
        if (p.barkeepT > 0) fx += "  (you're the bartender)";
        Txt(fx, x, y + 90, 13, dim);
    }
    // names and mood faces over the patrons near you (traits too, with Absinthe's visions)
    for (const auto& c : n.patrons) {
        if (!c.inside || c.gone) continue;
        float dd = Vector2Distance(c.pos, p.pos); if (dd > 7) continue;
        Vector2 s = GetWorldToScreen({c.pos.x, (Seated(c) ? 1.55f : 1.95f) * c.look.height, c.pos.y}, S.cam);
        Vector3 toC = Vector3Subtract({c.pos.x, 1.5f, c.pos.y}, S.cam.position), fw = Vector3Subtract(S.cam.target, S.cam.position);
        if (Vector3DotProduct(toC, fw) <= 0 || s.x < 0 || s.x > SCREEN_W || s.y < 0 || s.y > SCREEN_H) continue;
        static const char* FACE[5] = {">:(", ":/", ":|", ":)", ":D"};
        int mi = c.mood < 20 ? 0 : c.mood < 40 ? 1 : c.mood < 60 ? 2 : c.mood < 80 ? 3 : 4;
        static const Color FC[5] = {{240, 90, 80, 255}, {240, 160, 90, 255}, {220, 220, 200, 255}, {170, 230, 150, 255}, {255, 220, 110, 255}};
        float a = std::clamp(1.4f - dd / 6, 0.0f, 1.0f);
        DrawTextCenteredBold(TextFormat("%s  %s", c.name.c_str(), FACE[mi]), s.x, s.y, 14, Fade(FC[mi], a));
        if (p.wareT[no::W_ANGLER] > 0 && c.thief) DrawTextCenteredBold("(a cooler, glowing)", s.x, s.y - 16, 13, Fade(Color{120, 255, 200, 255}, a));
        bool known = std::find(p.known.begin(), p.known.end(), c.name) != p.known.end();
        if (p.visionsT > 0 || known) {
            std::string tr = no::TypeName(c.type); for (int k = 0; k < (int)no::D().traitNames.size(); k++) if (c.Has(k)) tr += ", " + no::D().traitNames[k];
            DrawTextCentered(tr, s.x, s.y + 16, 12, Fade(Color{200, 190, 255, 255}, a));
            if (known) DrawTextCentered(c.secret, s.x, s.y + 30, 11, Fade(Color{255, 200, 160, 255}, a));
        }
    }
    // the people who aren't there have names too (and vanish when you come close)
    if (p.hallucSeed) for (int k = 0; k < 3; k++) {
        static const char* FAKE[6] = {"Mr. Haddock", "a pale sailor", "Aunt Marguerite", "the other you", "Captain Nobody", "a lady in green"};
        Vector2 at = HallucAt(p, k); float dd = Vector2Distance(at, p.pos); if (dd < 1.3f || dd > 7) continue;
        Vector3 toC = Vector3Subtract({at.x, 1.5f, at.y}, S.cam.position), fw = Vector3Subtract(S.cam.target, S.cam.position); if (Vector3DotProduct(toC, fw) <= 0) continue;
        Vector2 s = GetWorldToScreen({at.x, 1.95f, at.y}, S.cam);
        DrawTextCenteredBold(TextFormat("%s  :)", FAKE[(p.hallucSeed >> (k * 4)) % 6]), s.x, s.y, 14, Fade(Color{170, 230, 150, 255}, std::clamp(1.4f - dd / 6, 0.0f, 1.0f)));
    }
    // the conversation (doc p. 10): their line, what you said, four options (and listen, a drink, walk away)
    if (p.talk.patron >= 0) {
        const no::Patron& c = n.patrons[p.talk.patron];
        Rectangle r{SCREEN_W / 2.0f - 340, SCREEN_H - 212.0f, 680, 200};
        DrawRectangleRounded(r, 0.06f, 6, Fade(Color{24, 16, 12, 255}, 0.93f));
        DrawRectangleRoundedLinesEx(r, 0.06f, 6, 2, brass);
        TxtBold(TextFormat("%s (%s, %s)", c.name.c_str(), no::TypeName(c.type), n.MoodName(c.mood)), r.x + 16, r.y + 10, 17, brass);
        Txt(TextFormat("%d won, %d lost", p.talk.wins, p.talk.losses), r.x + r.width - 120, r.y + 12, 14, dim);
        if (!p.talk.myCaption.empty()) DrawWrapped(std::string("You: \"") + p.talk.myCaption + "\"" + (p.talk.substituted ? "  (that isn't what you meant to say)" : ""), {r.x + 16, r.y + 38, r.width - 32, 40}, 15, p.talk.substituted ? Color{255, 170, 150, 255} : dim);
        DrawWrapped(std::string("\"") + p.talk.theirLine + "\"", {r.x + 16, r.y + 80, r.width - 32, 44}, 17, ink);
        if (p.talk.over) DrawTextCenteredBold(p.talk.result, r.x + r.width / 2, r.y + 150, 16, Color{255, 220, 150, 255});
        else {
            if (c.type != no::T_STAFF && c.home != "none") { Rectangle fb{r.x + r.width - 132, r.y + 104, 116, 32}; if (Button(fb, "8 Flirt", true, 14) || IsKeyPressed(KEY_EIGHT)) p.in.flirtWith = p.talk.patron; }
            const char* OPT[7] = {"1 Ask", "2 Agree", "3 Joke", "4 Challenge", "5 Listen", "6 Buy a drink", "7 Leave"};
            for (int k = 0; k < 7; k++) {
                if (k == 4 && c.type != no::T_TALKER) continue;
                Rectangle b{r.x + 16 + k * 93.0f, r.y + 146, 88, 36};
                bool hov = CheckCollisionPointRec(GetMousePosition(), b);
                DrawRectangleRounded(b, 0.2f, 6, hov ? Color{90, 64, 40, 255} : Color{56, 40, 28, 255});
                DrawTextCentered(OPT[k], b.x + b.width / 2, b.y + 10, 13, ink);
                if (k < 4 && hov) DrawTextCentered(TextFormat("difficulty %.0f", n.Difficulty(c, k)), b.x + b.width / 2, b.y - 16, 12, dim);
                if ((hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) || IsKeyPressed(KEY_ONE + k)) p.in.say = k;
            }
        }
    }
    if (p.flirt.patron >= 0) {
        const no::Patron& c = n.patrons[p.flirt.patron]; const no::Flirt& F = p.flirt;
        Rectangle r{SCREEN_W / 2.0f - 360, SCREEN_H - 236.0f, 720, 224};
        DrawRectangleRounded(r, 0.06f, 6, Fade(Color{40, 14, 22, 255}, 0.93f));
        DrawRectangleRoundedLinesEx(r, 0.06f, 6, 2, Color{230, 140, 160, 255});
        TxtBold(TextFormat("%s  (%s)", c.name.c_str(), n.MoodName(c.mood)), r.x + 16, r.y + 10, 17, Color{240, 170, 190, 255});
        for (int k = 0; k < F.need; k++) DrawCircle((int)(r.x + r.width - 30 - k * 22), (int)r.y + 20, 7, k < F.wins ? Color{240, 120, 150, 255} : Fade(Color{240, 120, 150, 255}, 0.25f));
        if (!F.myCaption.empty()) DrawWrapped(std::string("You: \"") + F.myCaption + "\"" + (F.substituted ? "  (that is not what you meant to say)" : ""), {r.x + 16, r.y + 36, r.width - 32, 40}, 15, F.substituted ? Color{255, 170, 150, 255} : dim);
        DrawWrapped(std::string("\"") + F.theirLine + "\"", {r.x + 16, r.y + 76, r.width - 32, 40}, 17, ink);
        if (!F.tell.empty()) Txt(F.tell, r.x + 16, r.y + 116, 14, Color{200, 190, 255, 255});
        if (F.offer) {
            if (Button({r.x + r.width / 2 - 190, r.y + 160, 180, 40}, "Take it", true, 17) || IsKeyPressed(KEY_ONE)) p.in.offer = 1;
            if (Button({r.x + r.width / 2 + 10, r.y + 160, 180, 40}, "Decline (a friend)", true, 15) || IsKeyPressed(KEY_TWO)) p.in.offer = 2;
        } else if (F.over) DrawTextCenteredBold(F.result, r.x + r.width / 2, r.y + 170, 16, Color{255, 200, 210, 255});
        else {
            static const char* OPEN[4] = {"1 Compliment", "2 Joke", "3 Buy a drink", "4 Ask to dance"}, * BUILD[4] = {"1 Compliment", "2 Joke", "3 Ask about them", "4 Lean in"};
            static const int OPT_OPEN[4] = {0, 1, 2, 3}, OPT_BUILD[4] = {0, 1, 4, 5};
            for (int k = 0; k < 5; k++) {
                Rectangle b{r.x + 16 + k * 138.0f, r.y + 160, 130, 40};
                bool hov = CheckCollisionPointRec(GetMousePosition(), b);
                DrawRectangleRounded(b, 0.2f, 6, hov ? Color{110, 50, 66, 255} : Color{70, 30, 42, 255});
                DrawTextCentered(k == 4 ? "5 Walk away" : (F.round == 0 ? OPEN[k] : BUILD[k]), b.x + b.width / 2, b.y + 12, 14, ink);
                if (k < 4 && hov) DrawTextCentered(TextFormat("%.0f%% it lands", n.FlirtOdds(p, c, F.round == 0 ? OPT_OPEN[k] : OPT_BUILD[k]) * 100), b.x + b.width / 2, b.y - 18, 12, dim);
                if ((hov && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) || IsKeyPressed(KEY_ONE + k)) p.in.flirtSay = k == 4 ? 6 : k;
            }
        }
    }
    if (p.leavingT > 0) DrawTextCenteredBold(TextFormat("Leaving with %s... (%.0f s)", n.patrons[std::clamp(p.leavingWith, 0, (int)n.patrons.size() - 1)].name.c_str(), p.leavingT), SCREEN_W / 2.0f, SCREEN_H / 2.0f + 60, 20, Color{255, 190, 200, 255});
    // the prompts
    if (p.st == no::State::Active && !S.menu && p.talk.patron < 0 && p.flirt.patron < 0 && !nog::Blocking(p)) {
        int near = n.NearestPatron(p, 1.8f), gk = n.NearGame(p);
        std::string talkTo = near >= 0 ? "E: talk to " + n.patrons[near].name + (n.patrons[near].type != no::T_STAFF ? "   T: flirt" : "") : "";
        const char* prompt = n.NearServe(p) ? "E: order at the bar" : n.NearHatch(p) ? "E: order food" : n.NearDoor(p) ? "E: walk home (ends your night)" : gk >= 0 ? nog::Prompt(n, p, gk) : near >= 0 ? talkTo.c_str() : nullptr;
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
        if (!kitchen && Button({r.x + r.width / 2 - 170, r.y + r.height + 30, 340, 30}, "Buy him one and ask who's trouble tonight", true, 14)) { p.in.askTrouble = true; S.menu = false; }
    }
    // the sailors' emotes over their heads, and a black eye noted
    for (const auto& q : n.players) {
        if (q.st == no::State::Gone || q.emoteT <= 0) continue;
        static const char* EM[7] = {"", "Cheers!", "Him!", "HA!", "*shrug*", "Come on, then!", "Whoa-"};
        Vector3 w{q.pos.x, 2.3f, q.pos.y}; Vector3 toC = Vector3Subtract(w, S.cam.position), fw = Vector3Subtract(S.cam.target, S.cam.position);
        if (Vector3DotProduct(toC, fw) <= 0) continue;
        Vector2 s = GetWorldToScreen(w, S.cam); DrawTextCenteredBold(TextFormat("%s: %s", q.name.c_str(), EM[std::clamp(q.emote, 0, 6)]), s.x, s.y, 16, Color{255, 230, 160, 255});
    }
    if (p.blackEye) Txt("(a black eye from last night)", SCREEN_W - 290.0f, 112, 12, Color{200, 150, 150, 255});
    // the table talk (online): the session's chat, and your line as you type it
    if (S.net) {
        const auto& ch = S.net->chat; float y = SCREEN_H - 230.0f;
        for (int k = std::max(0, (int)ch.size() - 4); k < (int)ch.size(); k++) { Txt(ch[k], 18, y, 14, Color{180, 220, 255, 255}); y += 18; }
        if (S.chatting) { DrawRectangle(16, (int)y + 2, 520, 22, Fade(BLACK, 0.6f)); Txt("say: " + S.chatBuf + "_", 20, y + 4, 15, WHITE); }
        else Txt("Enter: talk to the table   F5-F10: toast, point, laugh, shrug, fists up, fall over", 18, y + 4, 12, dim);
    }
    // the room's talk: the bartender, the toasts
    { float y = SCREEN_H - 140; int shown = 0; for (int i = (int)n.say.size() - 1; i >= 0 && shown < 4; i--, shown++) { Txt(n.say[i], 18, y, 15, Fade(ink, 1 - shown * 0.2f)); y -= 20; } }
    if (S.help) {
        Rectangle r{18, 44, 400, 131};
        DrawRectangleRounded(r, 0.06f, 6, Fade(Color{20, 12, 8, 255}, 0.7f));
        const char* L[] = {"WASD: walk (Shift: hurry)   Mouse: look", "E: the bar, the hatch, a game, a patron; the door: home", "Every drink: charisma down, toughness up", "F5-F10: toast, point, laugh, shrug, fists up, fall over", "H: hide this"};
        for (int i = 0; i < 5; i++) Txt(L[i], r.x + 12, r.y + 10 + i * 23, 14, i < 2 ? ink : dim);
    }
    // the fight: the hits' words, health over everyone in a brawl near you, your own state, the police
    for (const auto& pp : n.pops) {
        Vector3 toC = Vector3Subtract(pp.pos, S.cam.position), fw = Vector3Subtract(S.cam.target, S.cam.position);
        if (Vector3DotProduct(toC, fw) <= 0) continue;
        Vector2 s = GetWorldToScreen(pp.pos, S.cam); float a = std::min(1.0f, pp.t);
        DrawTextCenteredBold(pp.text, s.x, s.y, 22 + (int)(8 * std::max(0.0f, pp.t - 1)), Fade(pp.col, a));
    }
    auto hpBar = [&](Vector2 at, float hh, const no::Combat& c, bool mine) {
        if (c.brawl < 0 && c.downT <= 0) return;
        Vector3 w{at.x, hh, at.y}; Vector3 toC = Vector3Subtract(w, S.cam.position), fw = Vector3Subtract(S.cam.target, S.cam.position);
        if (Vector3DotProduct(toC, fw) <= 0 || Vector2Distance(at, p.pos) > 12) return;
        Vector2 s = GetWorldToScreen(w, S.cam); float k = c.hpMax > 0 ? c.hp / c.hpMax : 0;
        DrawRectangle((int)s.x - 26, (int)s.y, 52, 6, Fade(BLACK, 0.6f)); DrawRectangle((int)s.x - 25, (int)s.y + 1, (int)(50 * std::clamp(k, 0.0f, 1.0f)), 4, mine || c.side == p.fight.side ? Color{120, 220, 120, 255} : Color{230, 90, 70, 255});
        if (c.downT > 0.5f) DrawTextCentered(TextFormat("out %.0f", c.downT), s.x, s.y - 16, 13, Color{255, 160, 140, 255});
    };
    for (const auto& c : n.patrons) if (c.inside && !c.gone) hpBar(c.pos, 2.15f * c.look.height, c.fight, false);
    if (p.fight.brawl >= 0 || p.fight.held >= 0 || p.st == no::State::Down) {
        Rectangle r{18, SCREEN_H - 250.0f, 300, 92};
        DrawRectangleRounded(r, 0.1f, 6, Fade(Color{20, 12, 8, 255}, 0.75f));
        TxtBold(p.st == no::State::Down ? TextFormat("Knocked out (%.0f s)", p.fight.downT) : p.fight.brawl >= 0 ? "In a fight" : "Armed", r.x + 12, r.y + 8, 16, Color{255, 190, 150, 255});
        Bar(r.x + 12, r.y + 32, 270, 12, p.fight.hpMax > 0 ? p.fight.hp / p.fight.hpMax : 0, Color{120, 220, 120, 255});
        std::string held = p.fight.held >= 0 && p.fight.held < (int)n.props.size() && n.props[p.fight.held].weapon >= 0 ? no::FD().weapons[n.props[p.fight.held].weapon].name : "bare fists";
        Txt(TextFormat("%s   (knockouts tonight: %d)", held.c_str(), p.fight.knockouts), r.x + 12, r.y + 50, 14, dim);
        Txt("LMB jab  RMB haymaker  F grab/throw  G shove  Q block  Space dodge  R pick up  X throw", r.x + 12, r.y + 70, 11, dim);
    }
    if (n.policeT > 0) DrawTextCenteredBold(TextFormat("The police are on their way: %d:%02d", (int)n.policeT / 60, (int)n.policeT % 60), SCREEN_W / 2.0f, 64, 18, Color{140, 180, 255, 255});
    if (n.policeInT > 0) DrawTextCenteredBold("THE POLICE ARE IN THE BAR", SCREEN_W / 2.0f, 64, 20, Color{140, 180, 255, 255});
    if (p.st == no::State::Active && !S.menu && p.talk.patron < 0 && !nog::Blocking(p) && p.fight.held < 0) {
        int k = n.NearestProp(p.pos, 1.6f, true);
        if (k >= 0 && n.props[k].weapon >= 0 && n.NearGame(p) < 0 && n.NearestPatron(p, 1.8f) < 0) DrawTextCentered(TextFormat("R: pick up %s", no::FD().weapons[n.props[k].weapon].name.c_str()), SCREEN_W / 2.0f, SCREEN_H - 64, 15, dim);
        if (Vector2Distance(p.pos, n.dog.pos) < 1.6f && n.dog.owner != p.id) DrawTextCenteredBold(TextFormat("E: share your chips with the dog (5)  [%d of %d]", n.dog.fed[std::clamp(p.id, 0, 5)], no::FD().dogFeeds), SCREEN_W / 2.0f, SCREEN_H - 90, 18, brass);
    }
    if (p.fight.held >= 0 && n.props[p.fight.held].kind == "bottle" && p.pos.x > 12 && p.pos.x < 21.5f && p.pos.y > 7.6f && p.pos.y < 9.2f) DrawTextCentered("C: smash it on the bar", SCREEN_W / 2.0f, SCREEN_H - 64, 15, Color{255, 170, 130, 255});
    {   // tonight's events, top left under the room
        int shown = 0; for (size_t i = 0; i < n.events.size(); i++) { const auto& e = n.events[i]; if (!e.started || e.done) continue; std::string label = "?";
            for (int id : e.people) if (id >= 0 && id < (int)n.patrons.size()) { label = n.patrons[id].secret.size() > 9 ? n.patrons[id].secret.substr(9) : n.patrons[id].secret; break; }
            if (e.people.empty()) label = n.lockIn ? "The lock-in" : n.goatOn ? "The goat" : "Something";
            TxtBold(label, 18, 40.0f + shown * 18, 14, Color{255, 200, 140, 255}); shown++; }
        if (n.raining) TxtBold("Rain", 18, 40.0f + shown * 18, 14, Color{170, 200, 240, 255});
    }
    if (p.st == no::State::Active && !S.menu && p.talk.patron < 0 && p.flirt.patron < 0 && !nog::Blocking(p)) {
        auto opts = n.EventOptions(p);
        for (int k = 0; k < (int)opts.size() && k < 3; k++) {
            DrawTextCenteredBold(TextFormat("F%d: %s", k + 1, opts[k].label.c_str()), SCREEN_W / 2.0f, SCREEN_H - 150.0f + k * 22, 16, Color{255, 210, 150, 255});
            if (IsKeyPressed(KEY_F1 + k)) { if (opts[k].act == 60) S.wares = true; else { p.in.evAct = opts[k].act; p.in.evArg = opts[k].arg; } }
        }
    }
    // the quiet man's list (doc pp. 36-37): eight doses, one of each a night; slip the Siren or the Cocktail into a friend's drink
    if (S.wares && !n.WaresHere(p)) S.wares = false;
    if (S.wares) {
        Rectangle r{SCREEN_W / 2.0f - 420, 70, 840, 470};
        DrawRectangleRounded(r, 0.04f, 6, Fade(Color{18, 14, 20, 255}, 0.95f)); DrawRectangleRoundedLinesEx(r, 0.04f, 6, 2, Color{150, 130, 190, 255});
        TxtBold("The quiet man's ledger", r.x + 18, r.y + 12, 20, Color{210, 190, 255, 255});
        Txt("\"One of each, and never above eighty. I have standards.\"", r.x + 18, r.y + 38, 14, dim);
        int near = -1; for (const auto& q : n.players) if (q.id != p.id && (q.st == no::State::Active || q.st == no::State::Drinking) && Vector2Distance(q.pos, p.pos) < 2.0f) { near = q.id; break; }
        for (int w = 0; w < no::W_COUNT; w++) {
            const no::WareDef& d = no::Wares()[w]; float y = r.y + 66 + w * 47;
            bool had = (p.wares >> w) & 1;
            TxtBold(TextFormat("%s  (%.0f)", d.name.c_str(), d.price), r.x + 18, y, 15, had ? dim : ink);
            Txt(d.effect, r.x + 200, y, 12, Color{190, 230, 190, 255}); DrawWrapped("but: " + d.catchText, {r.x + 200, y + 15, 420, 30}, 11, Color{240, 170, 150, 255});
            if (Button({r.x + r.width - 190, y - 2, 80, 26}, had ? "had it" : "Buy", !had && p.money >= d.price, 13)) p.in.evAct = 61 + w;
            if ((w == no::W_SIREN || w == no::W_COCKTAIL) && near >= 0 && Button({r.x + r.width - 104, y - 2, 88, 26}, "Slip it", p.money >= d.price && !((n.players[near].wares >> w) & 1), 13)) { p.in.evAct = 71 + w; p.in.evArg = near; }
        }
        if (near >= 0) Txt(TextFormat("(%s's glass is within reach.)", n.players[near].name.c_str()), r.x + 18, r.y + r.height - 26, 13, dim);
        if (Button({r.x + r.width - 120, r.y + r.height - 34, 100, 26}, "Done", true, 14) || IsKeyPressed(KEY_ESCAPE)) S.wares = false;
    }
    if (p.toastT > 0) DrawTextCenteredBold(p.toast, SCREEN_W / 2.0f, 70.0f, 16, Color{255, 190, 160, (unsigned char)(255 * std::min(1.0f, p.toastT))});
    if (p.skipT > 0) { DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, 0.85f)); DrawTextCenteredBold("Things are happening.", SCREEN_W / 2.0f, SCREEN_H / 2.0f - 10, 28, Color{200, 190, 255, 255}); DrawTextCentered("(You'll hear about them.)", SCREEN_W / 2.0f, SCREEN_H / 2.0f + 26, 16, dim); }
    // the bar games: the opponent-and-stake menu, or the game being played
    nog::Frame(n, p, S.shot ? 1 / 60.0f : GetFrameTime());
}
void DrawMorning(Game& g) {
    no::Night& n = NW();
    DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Color{236, 226, 200, 255});
    // the newspaper
    DrawTextCenteredBold("THE HARBOUR GAZETTE", SCREEN_W / 2.0f, 40, 40, Color{30, 26, 22, 255});
    DrawRectangle(120, 92, SCREEN_W - 240, 3, Color{30, 26, 22, 255});
    DrawTextCenteredBold(n.Headline(), SCREEN_W / 2.0f, 110, 30, Color{40, 30, 24, 255});
    DrawRectangle(120, 152, SCREEN_W - 240, 1, Color{30, 26, 22, 255});
    // a column per sailor (two rows of three at most): the ending, the story, the scoreboard
    int N = (int)n.players.size(), cols = N <= 1 ? 1 : N <= 2 ? 2 : 3;
    float colW = (SCREEN_W - 160.0f) / cols, y0 = 172;
    for (int i = 0; i < N; i++) {
        const no::Player& p = n.players[i];
        float x = 80 + (i % cols) * colW, y = y0 + (i / cols) * 270.0f, w = colW - 24;
        Color ink{40, 30, 24, 255}, soft{86, 70, 54, 255};
        TxtBold(TextFormat("%s (the %s)", p.name.c_str(), no::CrewName(p.crew)), x, y, 19, ink);
        Txt(no::EndingName(p.ending), x, y + 24, 15, Color{140, 50, 40, 255});
        float yy = y + 46;
        for (const auto& line : n.MorningStory(p)) { DrawWrapped(line, {x, yy, w, 60}, 14, soft); yy += 18.0f * (1 + (int)(MeasureText(line.c_str(), 14) / std::max(1.0f, w))); }
        yy += 6;
        int total = 0;
        for (const auto& l : n.ScoreBreakdown(p)) { Txt(l.what, x + 6, yy, 14, soft); Txt(TextFormat("%+d", l.points), x + w - 50, yy, 14, l.points >= 0 ? ink : Color{150, 50, 40, 255}); yy += 17; total += l.points; }
        DrawRectangle((int)x, (int)yy + 2, (int)w, 1, ink);
        TxtBold(TextFormat("Score %d", total), x + w - 110, yy + 6, 17, ink);
    }
    if (!S.remembered.empty()) {
        float y = SCREEN_H - 150.0f - 18 * std::min<size_t>(S.remembered.size(), 4);
        TxtBold(TextFormat("The Gull will remember (night %d):", S.prof.nights), 120, y, 15, Color{120, 50, 40, 255}); y += 20;
        for (size_t k = 0; k < S.remembered.size() && k < 4; k++) { Txt(S.remembered[k], 136, y, 13, Color{90, 70, 54, 255}); y += 18; }
    }
    if (Button({SCREEN_W / 2.0f - 120, SCREEN_H - 80.0f, 240, 40}, S.net ? "Back to the lobby" : "Back to the arcade", true, 16)) { LeaveNightOff(g); }
}

// ---------------------------------------------------------------- the sound (doc pp. 26-28): the state every frame, and what changed as cues
struct AudioMemo {
    std::set<long long> pops; std::vector<uint8_t> props; int marks = 0, big = 0, shot = 0, inPocket = 0, drinks = -1, pulls = 0, pokerHand = 0, pokerStreet = -1, bsCall = -2, sayN = 0, golfHole = -1;
    bool paid = false, reading = false, stumble = false, holed = false; float stepT = 0, millT = 0; std::vector<bool> ev; int st = -1; float myStack = -1;
};
AudioMemo gAM;
void NightAudioFrame(const no::Night& n, float dt) {
    const no::Player& p = Me();
    auto rel = [&](Vector2 at, float& pan, float& vol) {   // pan and loudness from where the camera is
        Vector3 r{-sinf(S.camYaw), 0, cosf(S.camYaw)}; Vector3 d = Vector3Subtract({at.x, 1.5f, at.y}, S.cam.position);
        float L = Vector3Length(d); pan = L > 0.01f ? std::clamp(Vector3DotProduct(d, r) / L, -1.0f, 1.0f) * 0.8f : 0; vol = 1 / (1 + L / 7);
    };
    NoAudio a; a.on = true;
    for (const auto& c : n.patrons) a.crowd += c.inside && !c.gone;
    a.hour = n.Hour(); a.drunk = p.drunk; a.raining = n.raining;
    std::string room = no::RoomAt(p.pos); a.outside = room == "The yard" || room == "the street" || room == "The alley";
    a.dogInside = n.dog.owner >= 0; a.cartel = n.EventOn("cartel"); a.wake = n.EventOn("wake");
    // the jukebox: a song a slot of nine game minutes, from the night's seed (so every guest hears the same); the sad one after midnight
    { int slot = (int)(n.Minutes() / 9); uint32_t h = (uint32_t)slot * 2654435761u ^ n.opts.seed; h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
      if (n.EventOn("band")) a.song = 2; else if (a.wake || n.EventOn("cartel")) a.song = -1; else if (h % 6 == 0) a.song = -1;
      else if (n.Hour() >= 24 && h % 3 == 0) { a.song = 5; a.singAlong = a.crowd > 4; } else a.song = (int)(h % 5);
      float pan, vol; rel(no::D().bar.jukebox, pan, vol); a.songPan = pan * 0.6f; a.songNear = std::clamp(0.35f + vol * 1.4f, 0.3f, 1.0f); }
    a.blackout = p.st == no::State::PassedOut; a.over = n.over ? 1 : 0;
    AudioNightOff(a);
    if (n.over) return;
    float pan = 0, vol = 1;
    // the fight's words become sounds
    std::set<long long> seen;
    for (const auto& pp : n.pops) {
        long long key = (long long)(pp.pos.x * 10) * 1000003LL + (long long)(pp.pos.z * 10) * 1009LL + (long long)pp.text.size() * 7 + pp.text[0];
        seen.insert(key);
        if (gAM.pops.count(key) || pp.t < 1.1f) continue;
        rel({pp.pos.x, pp.pos.z}, pan, vol);
        const std::string& w = pp.text;
        if (w == "CRASH!") NightOffCue(NOC_WINDOW, vol, pan);
        else if (w == "Smash!") NightOffCue(NOC_SMASH, vol, pan);
        else if (w == "Crack!") NightOffCue(NOC_CRASH, vol, pan);
        else if (w == "BLAM!") NightOffCue(NOC_SHOTGUN, 1, pan);
        else if (w == "*munch*" || w == "GRR!") NightOffCue(NOC_DOG, vol, pan);
        else if (w == "Whack!" || w == "Thud!" || w == "Pow!" || w == "Oof!" || w == "Bonk!" || w == "WALLOP!" || w == "K.O." || w == "Crack!") { NightOffCue(NOC_PUNCH, vol, pan, w == "WALLOP!" || w == "K.O." ? 0.7f : 1); if (p.drunk >= 80) NightOffCue(NOC_WHISTLE_SLIDE, vol * 0.7f, pan); }
    }
    gAM.pops = seen;
    // breakages
    if (gAM.props.size() != n.props.size()) gAM.props.assign(n.props.size(), 0);
    for (size_t i = 0; i < n.props.size(); i++) { uint8_t s = n.props[i].state; if (s == no::PS_BROKEN && gAM.props[i] != no::PS_BROKEN && gAM.props[i] != 0) { rel({n.props[i].pos.x, n.props[i].pos.z}, pan, vol); const std::string& k = n.props[i].kind; NightOffCue(k == "window" ? NOC_WINDOW : (k == "glass" || k == "bottle") ? NOC_SMASH : NOC_CRASH, vol, pan); } gAM.props[i] = s ? s : 9; if (s == 0) gAM.props[i] = 9; }
    // your night: drinks, steps, stumbles, hiccups
    if (gAM.drinks >= 0 && p.drinks > gAM.drinks) { NightOffCue(NOC_CLINK, 0.8f, 0.2f); NightOffCue(NOC_GULP, 0.6f, 0); }
    gAM.drinks = p.drinks;
    float spd = Vector2Length(p.vel);
    if (spd > 0.4f && p.st == no::State::Active) { gAM.stepT -= dt; if (gAM.stepT <= 0) { gAM.stepT = 0.42f * 3 / std::max(1.0f, spd); NightOffCue(NOC_STEP, p.drunk > 60 ? 0.5f : 0.3f, 0, p.drunk > 60 ? 0.8f : 1.0f); } }
    bool stum = p.stumbleT > 0; if (stum && !gAM.stumble) NightOffCue(NOC_STUMBLE, 0.7f, 0); gAM.stumble = stum;
    if (p.drunk > 45 && p.st == no::State::Active && GetRandomValue(0, 10000) < (int)(dt * 150)) NightOffCue(NOC_HICCUP, 0.5f, 0);
    if (room == "The toilets" && GetRandomValue(0, 10000) < (int)(dt * 300)) NightOffCue(NOC_TAP, 0.3f, -0.3f);
    if ((int)p.st != gAM.st && p.st == no::State::Gone) NightOffCue(NOC_DOOR, 0.7f, 0);
    gAM.st = (int)p.st;
    // the games
    const no::GameSeat& g = p.game;
    if (g.kind == no::GK_DARTS) { int m = (int)g.darts.marks.size(); if (m > gAM.marks || (m == 1 && gAM.marks == 3)) NightOffCue(NOC_DART, 0.8f, -0.2f); gAM.marks = m; int b = g.darts.big[0] + g.darts.big[1]; if (b > gAM.big) NightOffCue(NOC_CHEER, 0.9f, 0); gAM.big = b; } else { gAM.marks = 0; gAM.big = 0; }
    if (g.kind == no::GK_POOL) {
        if (g.shotSerial != gAM.shot) { NightOffCue(NOC_POOL_CLICK, 0.9f, 0); gAM.inPocket = 0; }
        gAM.shot = g.shotSerial;
        if (g.replayT > 0 && !g.pool.t.frames.empty()) { int f = std::clamp((int)(g.replayLen - g.replayT * 60), 0, (int)g.pool.t.frames.size() - 1); int in = 0; for (const auto& b : g.pool.t.frames[f]) in += b.x < -5; if (in > gAM.inPocket) NightOffCue(NOC_POCKET, 0.8f, 0); gAM.inPocket = in; }
    }
    if (g.kind == no::GK_GOLF) {
        if (g.golf.hole == 1) { gAM.millT -= dt; if (gAM.millT <= 0) { gAM.millT = 1.5f; NightOffCue(NOC_WINDMILL, 0.4f, 0.1f); } }
        bool holed = g.golf.lastHoled && g.golf.lastHole == 8; if (holed && !gAM.holed && g.replayT <= 0.1f) NightOffCue(NOC_SPLASH, 0.9f, 0); gAM.holed = holed;
        if (g.shotSerial != gAM.shot) { NightOffCue(NOC_POOL_CLICK, 0.5f, 0, 0.6f); gAM.shot = g.shotSerial; }
    }
    if (g.kind == no::GK_SLOTS) { if (g.pulls > gAM.pulls) { NightOffCue(NOC_REELS, 0.8f, 0); if (g.pull.pays >= 200) NightOffCue(NOC_JACKPOT, 0.9f, 0); } gAM.pulls = g.pulls; } else gAM.pulls = 0;
    if (g.kind == no::GK_SCRATCH || g.kind == no::GK_PIP) { if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && g.haveTicket && !g.paid && GetRandomValue(0, 100) < 20) NightOffCue(NOC_SCRATCH, 0.4f, 0); if (g.paid && !gAM.paid && g.ticket.prize > 0) NightOffCue(NOC_CHEER, 0.5f, 0); gAM.paid = g.paid; }
    if (g.kind == no::GK_FORTUNE) { if (g.haveReading && !gAM.reading) NightOffCue(NOC_CARD, 0.8f, 0, 1.0f); gAM.reading = g.haveReading; } else gAM.reading = false;
    if (g.kind == no::GK_POKER) {
        const no::cards::Poker& P = g.machine == 1 ? n.cartelHand : n.poker;
        if (P.hand != gAM.pokerHand) NightOffCue(NOC_SHUFFLE, 0.8f, 0);
        if (P.street != gAM.pokerStreet && P.street >= 1 && P.street <= 3) NightOffCue(NOC_CARD, 0.6f, 0, 0.7f);
        gAM.pokerHand = P.hand; gAM.pokerStreet = P.street;
        float st = -1; for (const auto& s : P.seats) if (s.kind == 0 && s.idx == p.id) st = (float)s.stack;
        if (gAM.myStack >= 0 && st >= 0 && st != gAM.myStack) NightOffCue(NOC_CHIPS, 0.7f, 0);
        gAM.myStack = st;
    } else gAM.myStack = -1;
    if (g.kind == no::GK_BULLSHIT) { if (n.bs.callSeat != gAM.bsCall && n.bs.callSeat >= 0) NightOffCue(NOC_SLAP, 0.9f, 0); gAM.bsCall = n.bs.callSeat; }
    // the events are heard as they walk in
    if (gAM.ev.size() != n.events.size()) gAM.ev.assign(n.events.size(), false);
    for (size_t i = 0; i < n.events.size(); i++) {
        const auto& e = n.events[i];
        if (e.started && !gAM.ev[i]) {
            std::string what = e.people.empty() ? (n.lockIn ? "lock-in" : "goat") : (e.people[0] < (int)n.patrons.size() ? n.patrons[e.people[0]].secret : "");
            if (what.find("bachelor party") != std::string::npos || what.find("rival") != std::string::npos) NightOffCue(NOC_PARTY_CHEER, 0.8f, -0.4f);
            else if (what.find("bachelorette") != std::string::npos) NightOffCue(NOC_WHISTLES, 0.8f, -0.4f);
            else if (what.find("biker") != std::string::npos) NightOffCue(NOC_ENGINES, 0.9f, 0.3f);
            else if (what.find("police") != std::string::npos) { NightOffCue(NOC_KNOCK, 0.9f, -0.5f); NightOffCue(NOC_POLICE_WHISTLE, 0.7f, -0.5f); }
            else if (what.find("robbery") != std::string::npos) NightOffCue(NOC_KITCHEN_DOOR, 1, 0.4f);
            else if (what.find("wake") != std::string::npos) NightOffCue(NOC_ORGAN, 0.8f, 0);
            else if (what.find("band") != std::string::npos) NightOffCue(NOC_TUNING, 0.8f, 0);
            else if (what == "lock-in") NightOffCue(NOC_BOLT, 1, -0.5f);
            else if (what == "goat") NightOffCue(NOC_GOAT, 0.9f, 0);
        }
        gAM.ev[i] = e.started;
    }
    if (n.goatOn && GetRandomValue(0, 10000) < (int)(dt * 200)) { rel(n.goatPos, pan, vol); NightOffCue(NOC_GOAT, vol, pan); }
    // the room's talk: a quoted line is spoken (a formant voice; the speaker's name sets the pitch)
    if ((int)n.say.size() < gAM.sayN) gAM.sayN = 0;
    for (int i = gAM.sayN; i < (int)n.say.size(); i++) {
        const std::string& s = n.say[i]; size_t q = s.find('"'); if (q == std::string::npos) continue;
        uint32_t h = 2166136261u; for (size_t k = 0; k < q; k++) h = (h ^ (uint8_t)s[k]) * 16777619u;
        NightOffVoice(0.7f + (h % 100) / 80.0f, 0.9f + (h % 7) / 10.0f, 3 + (int)std::min<size_t>(9, (s.size() - q) / 8), 0.6f, ((h >> 8) % 100) / 100.0f - 0.5f, s.find("HA") != std::string::npos);
    }
    gAM.sayN = (int)n.say.size();
}

}  // namespace

void StartNightOff(Game& g, int crew, int mode, int crowd, int bar, int season) {
    gAM = AudioMemo{};
    std::string why;
    if (!rt::DataOk(&why)) { g.scene = Scene::Arcade; return; }
    S.net = nullptr; S.live = nullptr; S.N.mirror = false;
    no::Opts o; o.players = 1; o.seed = (uint32_t)GetRandomValue(1, 1 << 30); o.mode = std::clamp(mode, 0, no::MD_COUNT - 1); o.crowd = std::clamp(crowd, 0, 3); o.bar = std::clamp(bar, 0, no::BAR_COUNT - 1); o.season = season;
    if (o.mode == no::MD_SOLO && o.crowd > 1) o.crowd = 1;   // (Solo: a Dead or Normal crowd)
    S.N.Init(o);
    S.N.players[0].crew = std::clamp(crew, 0, 5);
    S.prof = gShotStart ? no::NightProfile{} : no::LoadNightProfile("nightoff_profile.txt"); S.profSaved = false; S.remembered.clear();
    S.N.ApplyProfile(S.N.players[0], S.prof);
    S.me = 0; S.active = true; S.shot = false; S.menu = false; S.help = true; S.t = 0; S.walkPh.clear(); nog::Reset();
    S.camYaw = PI * 0.5f; S.camPitch = -0.28f; S.camDist = 3.2f; S.camAt = {Me().pos.x, 1.55f, Me().pos.y};
    g.scene = Scene::NightOff;
}
// a networked night (stage 6): the arcade's session launched A Night Off; the host draws its real night, a guest its mirror
void StartNightOffNet(Game& g, arcade::Session* net, const char* name, int crew) {
    std::string why;
    if (!rt::DataOk(&why)) { g.scene = Scene::Arcade; return; }
    S.net = net; S.live = nullptr; S.N = no::Night{};
    S.netName = name ? name : "Sailor"; S.netCrew = std::clamp(crew, 0, 5); S.helloSent = false; S.seenVersion = -1;
    S.prof = no::LoadNightProfile("nightoff_profile.txt"); S.profSaved = false; S.remembered.clear(); S.chatting = false; S.chatBuf.clear();
    S.me = std::max(0, net->MyPlayer()); S.active = true; S.shot = false; S.menu = false; S.help = true; S.t = 0; S.walkPh.clear(); nog::Reset();
    S.camYaw = PI * 0.5f; S.camPitch = -0.28f; S.camDist = 3.2f; S.camAt = {19.5f, 1.55f, 1.5f};
    g.scene = Scene::NightOff;
}
void LeaveNightOff(Game& g) {
    if (S.net) { if (S.net->role == arcade::R_HOST) S.net->BackToLobby(); else S.net->Leave(); }
    S.net = nullptr; S.live = nullptr;
    S.active = false; g.scene = Scene::Arcade;
}
std::string NightOffOpts(int mode, int crowd, bool pvp, int bar, int season) { return no::NightHostOpts(mode, crowd, pvp, 0, bar, season); }
void SceneNightOff(Game& g) {
    if (!S.active) { StartNightOff(g, 0); if (!S.active) return; }
    float dt = S.shot ? 1 / 60.0f : std::min(GetFrameTime(), 1 / 30.0f);
    if (S.net) {
        arcade::Session& N = *S.net;
        N.Update(GetTime(), dt);
        if (N.stage != arcade::S_PLAYING) { S.net = nullptr; S.live = nullptr; S.active = false; g.scene = Scene::Arcade; return; }
        S.me = std::max(0, N.MyPlayer());
        if (N.role == arcade::R_HOST) S.live = no::NightHostWorld(N.HostGame());
        else if (N.stateVersion != S.seenVersion && !N.Snapshot().empty()) {
            S.seenVersion = N.stateVersion;
            Reader r(N.Snapshot()); no::ReadNight(r, S.N);
        }
        no::Night& n = NW();
        if (n.players.empty() || S.me >= (int)n.players.size()) { ClearBackground(Color{20, 14, 10, 255}); DrawTextCenteredBold("Ashore, to the Sodden Gull...", SCREEN_W / 2.0f, SCREEN_H / 2.0f - 12, 24, Color{240, 210, 150, 255}); return; }
        if (!S.helloSent) { Writer o; no::OrderHello(o, S.netName, S.netCrew, no::ProfileSummary(S.prof)); N.Act(o); S.helloSent = true; S.camAt = {Me().pos.x, 1.55f, Me().pos.y}; }
        // the table talk: Enter to say something to everyone
        if (S.chatting) { int ch; while ((ch = GetCharPressed()) > 0) if (ch >= 32 && ch < 127 && S.chatBuf.size() < 80) S.chatBuf += (char)ch; if (IsKeyPressed(KEY_BACKSPACE) && !S.chatBuf.empty()) S.chatBuf.pop_back();
            if (IsKeyPressed(KEY_ENTER)) { if (!S.chatBuf.empty()) N.Chat(S.chatBuf); S.chatBuf.clear(); S.chatting = false; } if (IsKeyPressed(KEY_ESCAPE)) { S.chatting = false; S.chatBuf.clear(); } }
        else if (IsKeyPressed(KEY_ENTER) && Me().talk.patron < 0 && Me().flirt.patron < 0 && !nog::Blocking(Me())) S.chatting = true;
        if (n.over) { if (!S.profSaved) { S.remembered = n.ProfileAfter(Me(), S.prof); no::SaveNightProfile(S.prof, "nightoff_profile.txt"); S.profSaved = true; } NightAudioFrame(n, dt); DrawMorning(g); return; }
        Gather(dt);
        S.t += dt;
        Render(dt);
        DrawHud();
        // everything we did this frame goes to the host as an Input (the stick, the presses, the clicks in the panels)
        Writer iw; no::WriteInput(Me().in, iw); N.Act(iw);
        Me().in = no::Input{};
        NightAudioFrame(n, dt);
        return;
    }
    if (NW().over) { if (!S.shot && !S.profSaved) { S.remembered = NW().ProfileAfter(Me(), S.prof); no::SaveNightProfile(S.prof, "nightoff_profile.txt"); S.profSaved = true; } if (!S.shot) NightAudioFrame(NW(), dt); DrawMorning(g); return; }
    Gather(dt);
    if (!S.shot) {   // (solo: the bots of an empty seat, none; the night steps here)
        NW().Step(dt);
    }
    S.t += dt;
    Render(dt);
    DrawHud();
    if (!S.shot) NightAudioFrame(NW(), dt);
}
void NightOffMenuTick(float dt) {
    // (the game menu is open: a networked night goes on underneath, and our sailor stands still)
    if (!S.active || !S.net) return;
    no::Input in; Writer w; no::WriteInput(in, w); S.net->Act(w);
    S.net->Update(GetTime(), dt);
}bool NightOffOwnsEsc() { if (!S.active || NW().over) return false; const no::Player& p = Me(); return S.menu || p.talk.patron >= 0 || p.flirt.patron >= 0 || nog::Blocking(p); }
// --shots: 0 walking in at 7, 1 at the bar ordering (the menu), 2 hammered at midnight in the games room, 3 the snug,
// 4 passed out on the floor, 5 the morning paper
void DebugNightOffShot(Game& g, int which) {
    gShotStart = true; StartNightOff(g, which % 6, 0, 1, which >= 40 && which < 50 ? no::BAR_MONKEY : no::BAR_GULL); gShotStart = false;   // (shots never read the player's own profile)
    S.shot = true; S.help = which == 0;
    no::Night& n = NW(); no::Player& p = Me();
    auto at = [&](float x, float z, float yaw, float camYaw, float drunk) { p.pos = {x, z}; p.yaw = yaw; S.camYaw = camYaw; p.drunk = drunk; S.camAt = {x, 1.55f, z}; };
    if (which == 0) at(19.5f, 2.5f, PI * 0.5f, PI * 0.5f, 0);
    if (which == 1) { at(16.8f, 8.2f, PI * 0.5f, PI * 0.45f, 22); S.menu = true; n.t = 60 * no::SECONDS_PER_GAME_MINUTE * 1.5f; }
    if (which == 2) { at(6, 7, PI, PI * 0.95f, 72); n.t = 60 * no::SECONDS_PER_GAME_MINUTE * 5; p.swayPh = 1.2f; p.lurch = 0.6f; S.rollK = 0.12f; }
    if (which == 3) { at(34, 4, 0, -0.3f, 30); S.camPitch = -0.2f; }
    if (which == 4) { at(18, 6, 0.3f, PI * 0.3f, 100); n.Leave(p, no::E_PASSED_OUT, ""); }
    if (which == 6 || which == 7) {   // the room at 10 p.m.; a conversation with Old Marlow
        for (int i = 0; i < (int)(3 * 60 * no::SECONDS_PER_GAME_MINUTE / 0.1f); i++) n.Step(0.1f);
        at(18, 5.5f, PI * 0.5f, PI * 0.42f, 25); S.camPitch = -0.22f;
        if (which == 7) { int who = -1; for (const auto& c : n.patrons) if (c.inside && !c.gone && c.name == "Old Marlow") who = c.id; if (who < 0) for (const auto& c : n.patrons) if (c.inside && !c.gone && c.type == no::T_TALKER) who = c.id;
            if (who >= 0) { no::Patron& c = n.patrons[who]; p.pos = Vector2Add(c.pos, {cosf(c.yaw) * 1.1f, sinf(c.yaw) * 1.1f}); S.camYaw = atan2f(c.pos.y - p.pos.y, c.pos.x - p.pos.x) - 0.3f; S.camAt = {p.pos.x, 1.55f, p.pos.y}; n.StartTalk(p, who); n.TalkChoose(p, 0); } }
    }
    if (which >= 8 && which <= 14) {   // the bar games: 8 darts, 9 pool, 10 golf (the windmill), 11 slots, 12 a scratch-off, 13 the fortune teller, 14 the darts menu
        for (int i = 0; i < (int)(3 * 60 * no::SECONDS_PER_GAME_MINUTE / 0.1f); i++) n.Step(0.1f);
        static const int KIND[7] = {no::GK_DARTS, no::GK_POOL, no::GK_GOLF, no::GK_SLOTS, no::GK_SCRATCH, no::GK_FORTUNE, no::GK_DARTS};
        static const Vector2 AT[7] = {{2.6f, 7.5f}, {3.3f, 5.0f}, {12, 38}, {1.5f, 13.5f}, {0.6f, 11.8f}, {36.6f, 5.8f}, {2.6f, 7.5f}};
        int kind = KIND[which - 8]; at(AT[which - 8].x, AT[which - 8].y, PI, PI, which == 10 ? 30 : 12);
        if (which == 14) nog::OpenMenu(kind, 0);
        else {
            int mach = 0; n.NearGame(p, &mach);
            auto ch = n.Challengers(p, kind); int opp = (kind <= no::GK_GOLF && !ch.empty()) ? ch[0] : -1;
            n.StartGame(p, kind, mach, opp, opp >= 0 ? 20 : 0);
            no::GameSeat& gs = p.game; no::GRng r; r.s = 4242;
            if (kind == no::GK_DARTS) { for (int k = 0; k < 5; k++) { Vector2 a = gs.darts.turn == 0 ? gs.darts.BotAim() : gs.darts.BotAim(); gs.darts.Throw({a.x + r.N() * 24, a.y + r.N() * 24}); } gs.botT = 0; }
            if (kind == no::GK_POOL) { for (int k = 0; k < 3; k++) { if (gs.pool.ballInHand) { gs.pool.t.b[0].p = no::pool::BotPlace(gs.pool, r); gs.pool.ballInHand = false; } gs.pool.Play(no::pool::BotShot(gs.pool, 1, r)); } gs.pool.turn = 0; gs.pool.ballInHand = false; gs.replayT = 0; gs.botT = 0; }
            if (kind == no::GK_GOLF) { for (int k = 0; k < 6 && gs.golf.hole < 1; k++) { float a, pw; no::golf::BotShot(gs.golf, 1, r, a, pw); gs.golf.Shoot(a, pw); } gs.golf.turn = 0; gs.replayT = 0; gs.botT = 0; gs.golf.sim.t = 0.4f; }
            if (kind == no::GK_SLOTS) { p.in.gameAct = 1; n.GameAction(p); p.in.gameAct = 0; gs.spinT = 0; }
            if (kind == no::GK_SCRATCH) { p.in.gameAct = 5; n.GameAction(p); p.in.gameAct = 0; nog::DebugPrepare(kind); }
            if (kind == no::GK_FORTUNE) { p.in.gameAct = 1; n.GameAction(p); p.in.gameAct = 0; nog::DebugPrepare(kind); }
            gs.captionT = 0;
        }
    }
    if (which == 15 || which == 16) {   // a brawl in the games room; and the wreck it leaves
        for (int i = 0; i < (int)(4 * 60 * no::SECONDS_PER_GAME_MINUTE / 0.1f); i++) n.Step(0.1f);
        std::vector<int> crowd;
        for (auto& c : n.patrons) if (!c.gone && (c.name == "Cutter Jones" || c.name == "Boxer Mags" || c.name == "Captain Vane" || c.name == "Harrow" || c.name == "Big Ruth")) { c.inside = true; c.leaving = false; c.leaveH = 27; c.talkingTo = -1; c.playing = -1; crowd.push_back(c.id); }
        for (size_t k = 0; k < crowd.size(); k++) { no::Patron& c = n.patrons[crowd[k]]; c.pos = {4.0f + k * 1.1f, 6.5f + (k % 2) * 1.4f}; c.goal = c.pos; c.path.clear(); c.nextGoalT = 1e9f; }
        at(5.2f, 9.8f, -PI * 0.5f, -PI * 0.5f, 35); S.camPitch = -0.3f; S.camDist = 4.2f;
        if (!crowd.empty()) { no::Patron& f = n.patrons[crowd[0]]; f.pos = {5.2f, 8.7f}; f.goal = f.pos; p.in.attack = no::MV_HAYMAKER; }
        for (int k = 0; k < (which == 15 ? 140 : 2400); k++) n.Step(1 / 60.0f);
        if (which == 16) { for (auto& b : n.brawls) if (!b.over) n.EndBrawl(b); p.st = no::State::Active; p.fight = no::Combat{}; at(8.5f, 12.5f, -PI * 0.6f, -PI * 0.62f, 20); S.camPitch = -0.45f; S.camDist = 5.0f; }
    }
    if (which == 18 || which == 19) {   // a flirt with Dottie Finch (a tell you can read sober); the morning after taking her offer
        for (int i = 0; i < (int)(3 * 60 * no::SECONDS_PER_GAME_MINUTE / 0.1f); i++) n.Step(0.1f);
        int d = -1; for (auto& c : n.patrons) if (c.name == "Dottie Finch") d = c.id;
        if (d >= 0) {
            no::Patron& c = n.patrons[d]; c.inside = true; c.gone = false; c.leaving = false; c.talkingTo = -1; c.mood = 70; c.pos = {35.6f, 6.6f}; c.goal = c.pos; c.nextGoalT = 1e9f; c.sitting = false;
            at(34.6f, 6.0f, 0.5f, 0.2f, 15);
            n.StartFlirt(p, d); n.FlirtChoose(p, 1);
            if (p.flirt.tell.empty()) p.flirt.tell = c.name + " glances at the toilets' window.";
            if (which == 19) { p.name = "You"; p.flirt.offer = true; n.FlirtOffer(p, true); p.kidneys = 1; p.tab = 18; p.peakDrunk = 82; n.over = true; S.remembered = n.ProfileAfter(p, S.prof); }
            for (int k = 0; k < 30; k++) StepCamera(1 / 60.0f);
        }
    }
    if (which == 20) {   // a guest's view: a host's six-sailor night at 10 p.m., read from its snapshot into our mirror
        static no::Night hostNight;
        no::Opts o; o.players = 6; o.seed = 4321; o.startMinutes = 160; hostNight.Init(o);
        for (auto& q : hostNight.players) { q.bot = true; q.botStyle = q.id % 2; q.botDrinkTo = 50; }
        for (int i = 0; i < 400; i++) { for (auto& q : hostNight.players) hostNight.BotPlayer(q, 0.05f); hostNight.Step(0.05f); }
        int view = 2; for (const auto& q : hostNight.players) if (q.game.kind < 0 && q.talk.patron < 0 && q.flirt.patron < 0 && q.st == no::State::Active) { view = q.id; break; }
        Writer w; no::PackNight(hostNight, view, w); Reader r(w.b);
        S.N = no::Night{}; no::ReadNight(r, S.N); S.me = view;
        no::Player& me = Me(); me.in = no::Input{};
        S.camYaw = me.yaw; S.camAt = {me.pos.x, 1.55f, me.pos.y}; S.camPitch = -0.3f; S.camDist = 4.0f;
        for (int k = 0; k < 30; k++) StepCamera(1 / 60.0f);
        return;
    }
    if (which >= 21 && which <= 24) {   // the events: 21 the biker gang at the pool tables, 22 the robbery, 23 the band and the dance floor, 24 the police
        static const char* KEY[4] = {"bikers", "robbery", "band", "police"};
        n.events.clear(); n.rainH = which == 21 ? 21.0f : 99;
        n.ForceEvent(KEY[which - 21], 22.0f);
        for (int i = 0; i < (int)(3 * 60 * no::SECONDS_PER_GAME_MINUTE / 0.1f) + 60; i++) n.Step(0.1f);
        for (int i = 0; i < (which == 22 ? 30 : 400); i++) n.Step(0.05f);
        if (which == 21) { at(7.5f, 9.0f, -PI * 0.7f, -PI * 0.72f, 20); S.camPitch = -0.35f; S.camDist = 4.5f; }
        if (which == 22) { at(18.5f, 4.5f, PI * 0.5f, PI * 0.5f, 25); S.camPitch = -0.25f; S.camDist = 4.0f; }
        if (which == 23) { at(16, 15, PI * 0.5f, PI * 0.5f, 30); S.camPitch = -0.2f; n.StartGame(p, no::GK_DANCE, 0, -1, 0); }
        if (which == 24) { at(16.5f, 6.0f, PI * 0.5f, PI * 0.45f, 15); S.camPitch = -0.25f; S.camDist = 4.0f; }
        for (int k = 0; k < 30; k++) StepCamera(1 / 60.0f);
    }
    if (which == 25 || which == 26) {   // the card room: 25 hold'em with the regulars (a hand under way), 26 bullshit (a claim on the table)
        for (int i = 0; i < (int)(3 * 60 * no::SECONDS_PER_GAME_MINUTE / 0.1f); i++) n.Step(0.1f);
        p.st = no::State::Active; p.money = 200;
        if (which == 25) { at(34.5f, 11.8f, PI * 0.5f, PI * 0.5f, 25); n.StartGame(p, no::GK_POKER, 0, -1, 0);
            for (int k = 0; k < 2000 && !(n.poker.street >= 1 && n.poker.street <= 3 && n.poker.turn >= 0 && n.poker.seats[n.poker.turn].kind == 0); k++) {
                int me = -1; for (int s = 0; s < (int)n.poker.seats.size(); s++) if (n.poker.seats[s].kind == 0) me = s;
                if (me >= 0 && n.poker.turn == me && n.poker.street == 0) p.in.gameAct = 22;
                n.Step(0.05f);
            }
            for (auto& s : n.poker.seats) if (s.kind == 1 && !s.tell.empty() && !s.folded) { s.tellNow = true; break; } }
        if (which == 26) { at(34.5f, 21.2f, -PI * 0.5f, -PI * 0.5f, 25); n.StartGame(p, no::GK_BULLSHIT, 0, -1, 0);
            for (int k = 0; k < 2000 && !(n.bsOn && n.bs.window > 0 && n.bs.lastSeat >= 0 && n.bs.seats[n.bs.lastSeat].kind == 1); k++) {
                int me = -1; for (int s = 0; s < (int)n.bs.seats.size(); s++) if (n.bs.seats[s].kind == 0) me = s;
                if (me >= 0 && n.bsOn && n.bs.turn == me && n.bs.window <= 0) { std::vector<int> v; n.bs.BotPlay(me, v); int mask = 0; for (int x : v) mask |= 1 << x; p.in.gameAct = 25; p.in.gameStake = mask; }
                n.Step(0.05f);
            } }
        p.game.captionT = 0;
    }
    if (which == 17) {   // the alley dog, fed and following you in
        p.pos = Vector2Add(n.dog.pos, {0.6f, 0}); for (int k = 0; k < 3; k++) { p.in.feedDog = true; n.Step(0.02f); }
        for (int k = 0; k < 90; k++) { p.in.moveX = 1; n.Step(1 / 60.0f); }
        p.in.moveX = 0; at(p.pos.x, p.pos.y, PI, PI * 0.85f, 15); S.camPitch = -0.35f;
        for (int k = 0; k < 60; k++) n.Step(1 / 60.0f);
    }
    if (which == 28 || which == 29) {   // the cartel's ledger open; Angler's Light (people who aren't there, a thief glowing)
        n.events.clear(); n.ForceEvent("cartel", 21.0f);
        for (int i = 0; i < (int)(2 * 60 * no::SECONDS_PER_GAME_MINUTE / 0.1f) + 60; i++) n.Step(0.1f);
        for (const auto& c : n.patrons) if (c.role == "a large man" && c.inside) { at(c.pos.x + 0.9f, c.pos.y, PI, PI * 0.9f, 25); break; }
        p.money = 260; S.camPitch = -0.25f;
        if (which == 28) { p.wares = 1u << no::W_SALT; S.wares = true; }
        else { n.DoseWare(p, no::W_ANGLER); p.hallucSeed = 12345; at(18, 5.5f, PI * 0.5f, PI * 0.42f, 25); S.camPitch = -0.22f; for (auto& c : n.patrons) if (c.thief) { c.inside = true; c.gone = false; c.pos = {19.5f, 3.8f}; c.goal = c.pos; c.nextGoalT = 1e9f; break; } }
    }
    if (which >= 40 && which < 50) {   // the Brass Monkey: 40 the long bar, 41 the library at the seance, 42 the roof and the Gull across the water, 43 Horace at the rope, 44 the billiards room
        for (int i = 0; i < (int)(3 * 60 * no::SECONDS_PER_GAME_MINUTE / 0.1f); i++) n.Step(0.1f);
        if (which == 40) { at(18, 5.0f, PI * 0.5f, PI * 0.45f, 20); S.camPitch = -0.2f; }
        if (which == 41) { n.t = (24.02f - 19) * 60 * no::SECONDS_PER_GAME_MINUTE; for (auto& c : n.patrons) if (c.name == "Madame Ostrova") { c.inside = true; c.gone = false; c.pos = {35.6f, 5.6f}; c.goal = c.pos; c.nextGoalT = 1e9f; } at(33.0f, 6.0f, 0, 0.05f, 20); S.camPitch = -0.18f; n.seanceDone = false; n.Step(0.05f); }
        if (which == 42) { at(20, 46, PI * 0.5f, PI * 0.38f, 20); S.camPitch = -0.05f; S.camDist = 3.4f; }
        if (which == 43) { p.barred = true; at(19.5f, -2.2f, PI * 0.5f, PI * 0.5f, 20); S.camPitch = -0.2f; for (int k = 0; k < 10; k++) { p.in.moveZ = 1; p.ropeIn = true; n.Step(0.05f); } p.in = no::Input{}; }
        if (which == 44) { at(6.5f, 7.5f, PI, PI * 1.05f, 20); S.camPitch = -0.3f; }
    }
    if (which == 27) {   // emotes: you raise a glass, a shipmate laughs
        for (int i = 0; i < (int)(2 * 60 * no::SECONDS_PER_GAME_MINUTE / 0.1f); i++) n.Step(0.1f);
        at(18, 5.5f, PI * 0.5f, PI * 0.42f, 20); S.camPitch = -0.22f; p.emote = 1; p.emoteT = 2;
    }
    if (which == 5) { p.drinks = 7; p.peakDrunk = 88; n.Leave(p, no::E_PASSED_OUT, ""); n.over = true; }
    for (int i = 0; i < 30; i++) StepCamera(1 / 60.0f);
}
