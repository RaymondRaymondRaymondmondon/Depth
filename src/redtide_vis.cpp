// Red Tide's Visual Overhaul (Red_Tide_Reference/"Red Tide - Visual Overhaul Spec.pdf"; progress in
// docs/REDTIDE_VISUAL.md): the four divers on the shared humanoid figure (figure3d.h, tools/artgen/rt_divers.py), and
// the studio, the harness's fixed shots of each new asset (--shots shots/vis rvis_).
#include "redtide_vis.h"
#include "figure3d.h"
#include "game.h"
#include "raymath.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace rt {

const Model* DiverModel(int voice) {
    static const char* F[4] = {"shared/divers/diver_diver.glb", "shared/divers/diver_whaler.glb", "shared/divers/diver_stowaway.glb", "shared/divers/diver_mechanic.glb"};
    return LoadAsset(F[std::clamp(voice, 0, 3)]);
}
bool DiversReady() { return DiverModel(0) != nullptr; }

// a diver's own face, steady for the slot (the person inside the suit)
static Color DiverSkin(int voice) {
    static const Color SKIN[4] = {{222, 170, 130, 255}, {160, 108, 76, 255}, {238, 196, 160, 255}, {198, 140, 104, 255}};
    return SKIN[std::clamp(voice, 0, 3)];
}

// the body language from the quips (spec, "Personality in posture"): the Diver economical and upright, the Whaler
// heavy-shouldered and forward-leaning, the Stowaway loose with a little sway, the Mechanic tight and quick
static void Temperament(int voice, fig::Pose& P, float t) {
    switch (voice) {
        case 1: P.nod += 0.12f; P.reach = std::max(P.reach, 0.12f); break;
        case 2: P.look += 0.15f * sinf(t * 0.7f); P.nod += 0.05f * sinf(t * 0.9f); break;
        case 3: P.elbow = std::max(P.elbow, 0.15f); P.grip = std::max(P.grip, 0.6f); break;
        default: break;
    }
}

// the Locker's token skins as material sets on the same mesh (spec, "Skins"): the suit's canvas, and the helmet's
// metal and trim. Verdigris: green patina; Red Tide: deep red canvas, rust-red brass; Bone: bleached ivory; Pearl:
// nacre; Atlantean: bronze with gold. Nothing else changes: the silhouette is the suit's.
void DiverSkinColours(const std::string& suit, const std::string& helmet, std::vector<Recolor>& out) {
    if (suit == "verdigris") { out.push_back({"top", {70, 112, 96, 255}}); out.push_back({"trousers", {62, 100, 86, 255}}); }
    else if (suit == "redtide") { out.push_back({"top", {124, 30, 28, 255}}); out.push_back({"trousers", {104, 26, 24, 255}}); }
    else if (suit == "bone") { out.push_back({"top", {214, 204, 184, 255}}); out.push_back({"trousers", {196, 186, 166, 255}}); }
    else if (suit == "pearl") { out.push_back({"top", {200, 208, 216, 255}}); out.push_back({"trousers", {186, 194, 204, 255}}); }
    else if (suit == "atlantean") { out.push_back({"top", {44, 64, 120, 255}}); out.push_back({"trousers", {36, 52, 100, 255}}); }
    std::string h = helmet.size() > 2 && helmet[1] == '_' ? helmet.substr(2) : helmet;
    if (h == "verdigris") { out.push_back({"hat", {84, 150, 124, 255}}); out.push_back({"accent", {104, 164, 136, 255}}); }
    else if (h == "redtide") { out.push_back({"hat", {150, 58, 40, 255}}); out.push_back({"accent", {172, 72, 46, 255}}); }
    else if (h == "bone") { out.push_back({"hat", {226, 214, 190, 255}}); out.push_back({"accent", {196, 184, 160, 255}}); }
    else if (h == "pearl") { out.push_back({"hat", {228, 232, 238, 255}}); out.push_back({"accent", {206, 220, 236, 255}}); }
    else if (h == "atlantean") { out.push_back({"hat", {150, 104, 50, 255}}); out.push_back({"accent", {236, 186, 64, 255}}); }
}

std::vector<Matrix> DrawDiverFigure(int voice, Matrix frame, fig::Pose P, float t, Color tint, const std::string& suit, const std::string& helmet) {
    const Model* m = DiverModel(voice);
    if (!m) return {};
    Temperament(voice, P, t);
    fig::Build B; B.build = voice == 3 ? 1.05f : 1.0f;
    std::vector<Matrix> skin = fig::PoseFigure(*m, B, P, t);
    std::vector<Recolor> rc = {{"skin", DiverSkin(voice)}};
    DiverSkinColours(suit, helmet, rc);
    DrawPbrSkinned(*m, frame, skin, rc, 0.35f, tint);
    return skin;
}

// first person: your own arms in your suit's gloves and sleeves, the body behind the eye (the arms reach forward to
// the gun), each fist closing on its grip (world points) by IK; gripL false: the left hand rests by the right
bool DrawFirstPersonArms(int voice, const Camera3D& cam, Vector3 gripR, Vector3 gripL, bool leftOn, float t, const std::string& suit, const std::string& helmet) {
    static const char* F[4] = {"shared/divers/diver_diver_fp.glb", "shared/divers/diver_whaler_fp.glb", "shared/divers/diver_stowaway_fp.glb", "shared/divers/diver_mechanic_fp.glb"};
    const Model* m = LoadAsset(F[std::clamp(voice, 0, 3)]);
    if (!m) return false;
    Vector3 f = Vector3Normalize(Vector3Subtract(cam.target, cam.position));
    Vector3 flat = Vector3Length(Vector3{f.x, 0, f.z}) > 1e-3f ? Vector3Normalize({f.x, 0, f.z}) : Vector3{1, 0, 0};
    Vector3 up{0, 1, 0}, rgt = Vector3Normalize(Vector3CrossProduct(flat, up));
    // (a viewmodel's scale: the crew's big stylised hands, this close to the eye, would fill the view at full size)
    const float VS = 0.7f;
    // (the shoulders just under the eye and a hand's breadth ahead, so the arms reach the grip with the elbows bent; the
    // torso stays below the bottom of the view)
    Vector3 o = Vector3Add(cam.position, Vector3Add(Vector3Scale(up, -1.21f), Vector3Add(Vector3Scale(flat, 0.1f), Vector3Scale(rgt, 0.04f))));
    Vector3 fx = Vector3Scale(flat, VS), uy = Vector3Scale(up, VS), rz = Vector3Scale(rgt, VS);
    Matrix frame = {fx.x, uy.x, rz.x, o.x, fx.y, uy.y, rz.y, o.y, fx.z, uy.z, rz.z, o.z, 0, 0, 0, 1};
    fig::Pose P; P.fp = true; P.grip = 0.9f; P.reach = 1.0f; P.elbow = 0.45f; P.breathe = t * 1.7f;
    Matrix inv = MatrixInvert(frame);
    P.ik[1] = true; P.target[1] = Vector3Transform(gripR, inv);
    P.ik[0] = true; P.target[0] = Vector3Transform(leftOn ? gripL : Vector3Add(gripR, Vector3Add(Vector3Scale(rgt, -0.09f), {0, -0.06f, 0})), inv);
    fig::Build B; B.build = voice == 3 ? 1.05f : 1.0f;
    std::vector<Matrix> skin = fig::PoseFigure(*m, B, P, t);
    std::vector<Recolor> rc = {{"skin", DiverSkin(voice)}};
    DiverSkinColours(suit, helmet, rc);
    DrawPbrSkinned(*m, frame, skin, rc, 0.35f, WHITE);
    return true;
}

// The helmet from inside (spec, "First person": "the brass HUD frames the screen, with faint glass reflections"): the
// front port's brass rim round the view, rivets on it, the helmet's dark copper in the corners, two faint streaks of
// reflection on the glass. Baked once into a texture at a third of the screen and drawn over the frame (under the
// HUD). wet: droplets on the glass (after an air pocket), 0..1.
static Texture2D gPortTex{};
void DrawHelmetPort(float wet, float t) {
    const int W = 640, H = 360;
    if (!gPortTex.id) {
        Image im = GenImageColor(W, H, BLANK);
        Color* px = (Color*)im.data;
        float rx = W * 0.56f, ry = H * 0.6f;   // the port: an oval a little wider than the view is tall
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            float dx = (x - W / 2.0f) / rx, dy = (y - H / 2.0f) / ry, r = sqrtf(dx * dx + dy * dy);
            Color c = BLANK;
            if (r > 1.0f) {
                // the helmet's inside: dark copper, lit faintly toward the rim
                float k = std::clamp((r - 1.0f) / 0.35f, 0.0f, 1.0f);
                c = {(unsigned char)(70 - 45 * k), (unsigned char)(44 - 30 * k), (unsigned char)(26 - 18 * k), 255};
            }
            if (r > 0.955f && r < 1.035f) {
                // the brass rim: a rounded band, bright along its inner edge, darker outward, a soft shadow inside it
                float u = (r - 0.955f) / 0.08f, lit = 0.55f + 0.45f * sinf(u * PI) * (0.7f + 0.3f * (-dy));
                c = {(unsigned char)std::min(255.0f, 170 * lit + 30), (unsigned char)std::min(255.0f, 126 * lit + 20), (unsigned char)(58 * lit + 10), 255};
            } else if (r > 0.92f && r <= 0.955f) {
                float u = (r - 0.92f) / 0.035f;
                c = {0, 0, 0, (unsigned char)(110 * u)};
            }
            px[y * W + x] = c;
        }
        // rivets round the rim
        for (int k = 0; k < 28; k++) {
            float a = k * 2 * PI / 28;
            int cx = (int)(W / 2.0f + cosf(a) * rx * 0.995f), cy = (int)(H / 2.0f + sinf(a) * ry * 0.995f);
            ImageDrawCircle(&im, cx, cy, 4, Color{96, 70, 30, 255});
            ImageDrawCircle(&im, cx - 1, cy - 1, 2, Color{230, 196, 120, 255});
        }
        // the glass's reflections: two faint diagonal streaks in the upper left
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            float s = (x * 0.8f + y) - 170, s2 = (x * 0.8f + y) - 215;
            float a = expf(-s * s / 60.0f) * 0.07f + expf(-s2 * s2 / 18.0f) * 0.05f;
            float dx = (x - W / 2.0f) / rx, dy = (y - H / 2.0f) / ry;
            if (dx * dx + dy * dy > 0.85f || x > W * 0.45f || y > H * 0.55f) continue;
            Color& c = px[y * W + x];
            if (c.a == 0) c = {230, 245, 250, (unsigned char)(a * 255)};
        }
        gPortTex = LoadTextureFromImage(im); UnloadImage(im);
        SetTextureFilter(gPortTex, TEXTURE_FILTER_BILINEAR);
    }
    DrawTexturePro(gPortTex, {0, 0, (float)W, (float)H}, {0, 0, (float)SCREEN_W, (float)SCREEN_H}, {0, 0}, 0, WHITE);
    if (wet > 0) {   // droplets running down the glass
        for (int k = 0; k < 24; k++) {
            float x = fmodf(k * 173.3f, 1180.0f) + 50, y = fmodf(k * 97.1f + t * (20 + k % 5 * 9), 640.0f) + 30;
            DrawCircleV({x, y}, 3 + k % 3, Fade(Color{220, 240, 250, 255}, 0.18f * wet));
            DrawCircleV({x - 1, y - 1}, 1.2f, Fade(WHITE, 0.4f * wet));
        }
    }
}

// ---------------------------------------------------------------- the water's particles
const Texture2D& SoftDot() {
    static Texture2D t{};
    if (!t.id) {
        Image im = GenImageColor(32, 32, BLANK);
        for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
            float dx = (x - 15.5f) / 15.5f, dy = (y - 15.5f) / 15.5f, r = sqrtf(dx * dx + dy * dy);
            float a = std::clamp(1 - r, 0.0f, 1.0f); a = a * a * (3 - 2 * a);
            ImageDrawPixel(&im, x, y, Color{255, 255, 255, (unsigned char)(a * 255)});
        }
        t = LoadTextureFromImage(im); UnloadImage(im);
        SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    }
    return t;
}
// a bubble: a bright rim, a clear middle and a fleck of highlight up and to the left
static const Texture2D& BubbleTex() {
    static Texture2D t{};
    if (!t.id) {
        Image im = GenImageColor(32, 32, BLANK);
        for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) {
            float dx = (x - 15.5f) / 15.5f, dy = (y - 15.5f) / 15.5f, r = sqrtf(dx * dx + dy * dy);
            if (r > 1) continue;
            float rim = std::clamp((r - 0.72f) / 0.2f, 0.0f, 1.0f) * std::clamp((1 - r) / 0.08f, 0.0f, 1.0f);
            float hl = std::max(0.0f, 1 - sqrtf((dx + 0.35f) * (dx + 0.35f) + (dy + 0.4f) * (dy + 0.4f)) / 0.22f);
            float a = std::clamp(0.12f + rim * 0.75f + hl, 0.0f, 1.0f);
            ImageDrawPixel(&im, x, y, Color{235, 248, 255, (unsigned char)(a * 255)});
        }
        t = LoadTextureFromImage(im); UnloadImage(im);
        SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
    }
    return t;
}
struct Bubble { Vector3 p; float r, life, ph; };
static std::vector<Bubble> gBubbles;
static uint32_t gFxRng = 99;
static float FR() { gFxRng = gFxRng * 1664525u + 1013904223u; return (gFxRng >> 8) / 16777216.0f; }
void FxBubbles(Vector3 at, int n, float spread, float size) {
    for (int k = 0; k < n && gBubbles.size() < 600; k++)
        gBubbles.push_back({{at.x + (FR() - 0.5f) * spread, at.y + (FR() - 0.5f) * spread, at.z + (FR() - 0.5f) * spread}, size * (0.5f + FR()), 4 + FR() * 3, FR() * 6.3f});
}
void FxStep(float dt, float surfaceY) {
    for (auto& b : gBubbles) {
        float rise = 0.55f + b.r * 18;                 // (bigger bubbles climb faster)
        b.p.y += rise * dt;
        b.ph += dt * (5 + b.r * 30);
        b.p.x += sinf(b.ph) * 0.25f * dt; b.p.z += cosf(b.ph * 0.8f) * 0.25f * dt;
        b.r *= 1 + 0.02f * dt;                         // (they swell as the pressure falls)
        b.life -= dt;
        if (b.p.y > surfaceY) b.life = 0;
    }
    gBubbles.erase(std::remove_if(gBubbles.begin(), gBubbles.end(), [](const Bubble& b) { return b.life <= 0; }), gBubbles.end());
}
void FxDrawBubbles(const Camera3D& cam) {
    const Texture2D& tx = BubbleTex();
    for (const auto& b : gBubbles) {
        if (Vector3Distance(b.p, cam.position) > 30) continue;
        float a = std::min(1.0f, b.life * 2);
        DrawBillboard(cam, tx, b.p, b.r * 2, Fade(WHITE, 0.85f * a));
    }
}
void FxClear() { gBubbles.clear(); }

// ---------------------------------------------------------------- the studio
// 0 the four divers front, side and back; 1 their faces through the ports; 2 swimming (the flutter kick, mid-stroke)
void DrawRedTideStudio(int which, float t) {
    SceneLight L;
    // a clear, bright patch of shallow water: the sun from above, the sea's blue fill, a little haze
    L.fog = {38, 92, 104, 255}; L.fogDensity = 0.01f;
    L.fill = {40, 96, 112, 255}; L.rim = {120, 200, 220, 255}; L.key = {255, 236, 200, 255};
    L.surfaceY = 6; L.time = t;
    L.lampRange = 14; L.lampCone = 0.55f;
    L.moonDir = Vector3Normalize({-0.3f, -1.0f, -0.2f}); L.moon = {200, 230, 235, 255}; L.moonK = 0.55f;
    L.ambK = 0.55f; L.skyAmb = {60, 116, 136, 255}; L.seaAmb = {16, 34, 40, 255};
    L.filmic = 1; L.exposure = 0.85f; L.aoK = 0.6f; L.outline = 0.35f; L.outlineTint = {16, 44, 52, 255}; L.stipple = 0;
    Camera3D cam{}; cam.fovy = 40; cam.projection = CAMERA_PERSPECTIVE; cam.up = {0, 1, 0};
    auto lamp = [&](Vector3 at, Vector3 target) { L.lampPos = at; L.lampDir = Vector3Normalize(Vector3Subtract(target, at)); };
    const float FRONT = -PI / 2, SIDE = 0, BACK = PI / 2;
    if (which == 0) {
        cam.position = {0, 1.3f, 10.5f}; cam.target = {0, 1.0f, 0}; cam.fovy = 34;
        lamp({-3, 5, 7}, {0, 1, 0});
        RenderBegin(cam, L);
        for (int d = 0; d < 4; d++) for (int v = 0; v < 3; v++) {
            float x = (d - 1.5f) * 2.6f + (v - 1) * 0.78f;
            fig::Pose P; P.breathe = t * 1.4f + d; P.blink = 0;
            DrawDiverFigure(d, fig::Frame({x, 0, v == 1 ? -0.3f : 0.0f}, v == 0 ? FRONT : v == 1 ? SIDE : BACK), P, t, WHITE);
        }
    } else if (which == 1) {
        cam.position = {0, 1.66f, 3.2f}; cam.target = {0, 1.6f, 0}; cam.fovy = 30;
        lamp({-1.2f, 2.6f, 2.6f}, {0, 1.6f, 0});
        RenderBegin(cam, L);
        for (int d = 0; d < 4; d++) {
            fig::Pose P; P.breathe = t * 1.4f + d; P.shout = d == 2 ? 0.6f : 0;
            DrawDiverFigure(d, fig::Frame({(d - 1.5f) * 0.62f, 0, 0}, FRONT + (d - 1.5f) * 0.12f), P, t, WHITE);
        }
    } else if (which == 3) {
        // the skins: each diver as issued and in the Locker's five suit-and-helmet sets
        cam.position = {0, -0.4f, 13.5f}; cam.target = {0, -0.6f, 0}; cam.fovy = 40;
        lamp({-3, 5, 9}, {0, 1, 0});
        RenderBegin(cam, L);
        static const char* SK[6] = {"", "verdigris", "redtide", "bone", "pearl", "atlantean"};
        for (int d = 0; d < 4; d++) for (int s = 0; s < 6; s++) {
            fig::Pose P; P.breathe = t * 1.4f + d + s;
            Vector3 at{(s - 2.5f) * 1.25f, (1.5f - d) * 2.05f - 1.6f, 0};
            DrawDiverFigure(d, MatrixMultiply(MatrixScale(0.9f, 0.9f, 0.9f), fig::Frame(at, FRONT + 0.35f)), P, t, WHITE, SK[s], std::string("h_") + SK[s]);
        }
    } else if (which == 2) {
        cam.position = {0, 2.2f, 8.5f}; cam.target = {0, 1.4f, 0}; cam.fovy = 38;
        lamp({-3, 6, 6}, {0, 1, 0});
        RenderBegin(cam, L);
        for (int d = 0; d < 4; d++) {
            fig::Pose P; P.breathe = t * 2.2f + d; P.swim = 1; P.kickPh = t * 5 + d * 1.3f;
            // the body lies along the stroke: pitched forward about its hips, drifting
            Matrix tip = MatrixMultiply(MatrixMultiply(MatrixTranslate(0, -1.0f, 0), MatrixRotateZ(-1.25f)), MatrixTranslate(0, 1.0f, 0));
            DrawDiverFigure(d, MatrixMultiply(tip, fig::Frame({(d - 1.5f) * 2.4f, 0.6f + 0.25f * (d % 2), 0}, d % 2 ? -0.5f : 0.5f)), P, t, WHITE);
        }
    }
    RenderEnd();
}

} // namespace rt
