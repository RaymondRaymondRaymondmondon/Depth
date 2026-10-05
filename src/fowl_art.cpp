// Fowl Play's art (see fowl_art.h). Everything is built from lit boxes and spheres through rt::, inked by RenderEnd.
#include "fowl_art.h"
#include "redtide_render.h"
#include "figure3d.h"
#include <algorithm>
#include <cmath>

namespace fpart {
using namespace fp;

static Model& Sphere() { static Model m = LoadModelFromMesh(GenMeshSphere(1, 12, 16)); return m; }
static Model& Cyl() { static Model m = LoadModelFromMesh(GenMeshCylinder(1, 1, 16)); return m; }   // (base at y 0, top at y 1)
static Model& Cone() { static Model m = LoadModelFromMesh(GenMeshCone(1, 1, 12)); return m; }
static Color Mx(Color a, Color b, float k) { k = std::clamp(k, 0.0f, 1.0f); return {(unsigned char)(a.r + (b.r - a.r) * k), (unsigned char)(a.g + (b.g - a.g) * k), (unsigned char)(a.b + (b.b - a.b) * k), 255}; }
static uint32_t H(uint32_t a) { a ^= a >> 16; a *= 0x7feb352d; a ^= a >> 15; a *= 0x846ca68b; a ^= a >> 16; return a; }
static float Hf(uint32_t a) { return (H(a) & 0xFFFF) / 65535.0f; }

// a frame from a centre and three axes (columns r, u, f)
static Matrix Frame(Vector3 c, Vector3 r, Vector3 u, Vector3 f) { return {r.x, u.x, f.x, c.x, r.y, u.y, f.y, c.y, r.z, u.z, f.z, c.z, 0, 0, 0, 1}; }
static Matrix Facing(Vector3 c, Vector3 fwd) {
    Vector3 f = Vector3Normalize(fwd); if (Vector3Length(f) < 0.5f) f = {0, 0, 1};
    Vector3 r = Vector3CrossProduct({0, 1, 0}, f); if (Vector3Length(r) < 0.05f) r = {1, 0, 0}; r = Vector3Normalize(r);
    Vector3 u = Vector3CrossProduct(f, r); return Frame(c, r, u, f);
}
static Matrix Local(Matrix frame, Vector3 at, Vector3 size, float rotX = 0, float rotY = 0, float rotZ = 0) {
    Matrix m = MatrixScale(size.x, size.y, size.z);
    if (rotZ) m = MatrixMultiply(m, MatrixRotateZ(rotZ));
    if (rotX) m = MatrixMultiply(m, MatrixRotateX(rotX));
    if (rotY) m = MatrixMultiply(m, MatrixRotateY(rotY));
    return MatrixMultiply(MatrixMultiply(m, MatrixTranslate(at.x, at.y, at.z)), frame);
}
// a unit cube with its edges rounded off (moulded toy plastic): the toy guns' parts use it instead of the hard cube
static Model& RoundCube() {
    static Model m{}; if (m.meshCount) return m;
    rt::MeshBuilder mb; const int n = 6; const float rad = 0.22f, in = 0.5f - rad;
    auto surf = [&](Vector3 c) { Vector3 q{std::clamp(c.x * 0.5f, -in, in), std::clamp(c.y * 0.5f, -in, in), std::clamp(c.z * 0.5f, -in, in)}; Vector3 d = Vector3Subtract(Vector3Scale(c, 0.5f), q); float L = Vector3Length(d); return Vector3Add(q, L > 1e-5f ? Vector3Scale(d, rad / L) : Vector3{0, rad, 0}); };
    for (int axis = 0; axis < 3; axis++) for (int s = -1; s <= 1; s += 2) {
        int ua = (axis + 1) % 3, va = (axis + 2) % 3;
        auto P = [&](int i, int j) { float c3[3]; c3[axis] = (float)s; c3[ua] = -1 + 2.0f * i / n; c3[va] = -1 + 2.0f * j / n; return surf({c3[0], c3[1], c3[2]}); };
        for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) {
            Vector3 a = P(i, j), b = P(i + 1, j), c = P(i + 1, j + 1), d = P(i, j + 1);
            Vector3 nn = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
            if ((&nn.x)[axis] * s >= 0) { mb.Tri(a, b, c, WHITE); mb.Tri(a, c, d, WHITE); } else { mb.Tri(a, c, b, WHITE); mb.Tri(a, d, c, WHITE); }
        }
    }
    m = LoadModelFromMesh(mb.Build()); return m;
}
static bool gRoundBoxes = false;   // (on while a toy gun draws)
static void Box(Matrix frame, Vector3 at, Vector3 size, Color c, float rx = 0, float ry = 0, float rz = 0) { if (gRoundBoxes) rt::DrawStatic(RoundCube(), Local(frame, at, size, rx, ry, rz), c); else rt::DrawCubeM(Local(frame, at, size, rx, ry, rz), c); }
static void Ball(Matrix frame, Vector3 at, Vector3 r, Color c, float glow = 0) { Matrix m = Local(frame, at, r); if (glow > 0) rt::DrawStaticGlow(Sphere(), m, c, glow); else rt::DrawStatic(Sphere(), m, c); }
static void Glow(Matrix frame, Vector3 at, Vector3 size, Color c, float g) { rt::DrawCubeGlow(Local(frame, at, size), c, g); }
static const Matrix ID = MatrixIdentity();
static float StallPosX(const World& w) { (void)w; return 0; }

// ---------------------------------------------------------------- the toy guns
Color PaintBody(int paint) {
    const auto& cs = D().cosmetics; std::string id = paint > 0 && paint < (int)cs.size() ? cs[paint].id : "";
    if (id == "paint_chrome") return {214, 220, 228, 255};
    if (id == "paint_gold") return {236, 190, 70, 255};
    if (id == "paint_camo") return {96, 112, 70, 255};
    if (id == "paint_pink") return {250, 140, 190, 255};
    if (id == "paint_mint") return {150, 230, 190, 255};
    if (id == "paint_ocean") return {70, 130, 200, 255};
    if (id == "paint_lava") return {230, 90, 40, 255};
    if (id == "paint_midnight") return {50, 50, 90, 255};
    if (id == "paint_bone") return {232, 224, 200, 255};
    if (id == "paint_golden_zapper") return {255, 206, 60, 255};
    return {178, 178, 184, 255};
}
static void DrawToyGunParts(int def, Matrix frame, int paint, float spin, float sc);
void DrawToyGun(int def, Matrix frame, int paint, float spin, float sc) {
    gRoundBoxes = true; DrawToyGunParts(def, frame, paint, spin, sc); gRoundBoxes = false;
}
static void DrawToyGunParts(int def, Matrix frame, int paint, float spin, float sc) {
    if (def < 0) return;
    const GunDef& G = D().guns[def]; const std::string& id = G.id;
    if (sc != 1) frame = MatrixMultiply(MatrixScale(sc, sc, sc), frame);
    Color body = PaintBody(paint), dark = Mx(body, {70, 70, 78, 255}, 0.55f), red{214, 44, 40, 255}, stampC{200, 30, 30, 255};
    auto pistol = [&](float len, float bulk) {
        Box(frame, {0, 0.035f, 0.05f}, {0.05f * bulk, 0.065f * bulk, 0.17f}, body);                       // the body
        Box(frame, {0, 0.035f, 0.05f + 0.085f + len / 2}, {0.034f * bulk, 0.034f * bulk, len}, dark);     // the barrel
        Box(frame, {0, -0.045f, -0.005f}, {0.044f * bulk, 0.11f, 0.055f}, dark, -0.3f);                  // the ribbed grip
        for (int k = 0; k < 4; k++) Box(frame, {0, -0.02f - k * 0.022f, -0.035f}, {0.047f * bulk, 0.006f, 0.012f}, Mx(dark, BLACK, 0.3f), -0.3f);
        Box(frame, {0, -0.012f, 0.028f}, {0.012f, 0.03f, 0.014f}, red);                                    // the stubby red trigger
        Box(frame, {0, -0.02f, 0.03f}, {0.02f, 0.006f, 0.05f}, dark);                                      // the guard
        for (int k = 0; k < 6; k++) Box(frame, {0, 0.071f * bulk, -0.02f + k * 0.025f}, {0.04f * bulk, 0.008f, 0.01f}, Mx(body, BLACK, 0.2f));   // the ribbed slide
        Box(frame, {0.026f * bulk, 0.04f, 0.06f}, {0.003f, 0.02f, 0.05f}, stampC);                         // the red Zappa stamp
        Ball(frame, {-0.026f * bulk, 0.02f, 0.0f}, {0.004f, 0.006f, 0.006f}, dark);                       // a screw boss
        Ball(frame, {-0.026f * bulk, 0.05f, 0.1f}, {0.004f, 0.006f, 0.006f}, dark);
    };
    auto longGun = [&](float barrel, float bodyL, bool stock, int mag, bool scope, float thick) {
        Box(frame, {0, 0.04f, 0.08f}, {0.06f, 0.08f, bodyL}, body);
        Box(frame, {0, 0.045f, 0.08f + bodyL / 2 + barrel / 2}, {0.035f * thick, 0.035f * thick, barrel}, dark);
        Box(frame, {0, -0.05f, 0.0f}, {0.046f, 0.11f, 0.055f}, dark, -0.25f);
        Box(frame, {0, -0.012f, 0.035f}, {0.012f, 0.03f, 0.014f}, red);
        if (stock) Box(frame, {0, 0.0f, 0.08f - bodyL / 2 - 0.12f}, {0.05f, 0.1f, 0.24f}, body, 0.12f);
        if (mag == 1) Box(frame, {0, -0.06f, 0.14f}, {0.045f, 0.14f, 0.07f}, dark, 0.15f);          // an oversized plastic box mag
        if (mag == 2) rt::DrawStatic(Cyl(), Local(frame, {0, -0.03f, 0.13f}, {0.09f, 0.05f, 0.09f}, PI / 2, 0, 0), dark);   // the drum (a toy biscuit tin)
        if (scope) { rt::DrawStatic(Cyl(), Local(frame, {0, 0.11f, -0.05f}, {0.028f, 0.28f, 0.028f}, PI / 2), dark); Box(frame, {0, 0.09f, 0.08f}, {0.02f, 0.04f, 0.02f}, body); }
        for (int k = 0; k < 8; k++) Box(frame, {0, 0.083f, 0.08f - bodyL / 2 + 0.02f + k * bodyL / 9}, {0.05f, 0.008f, 0.01f}, Mx(body, BLACK, 0.2f));
        Box(frame, {0.031f, 0.045f, 0.1f}, {0.003f, 0.025f, 0.07f}, stampC);
    };
    const std::string& T = G.type;
    if (id == "zapper") pistol(0.07f, 1);
    else if (id == "twin") { pistol(0.07f, 1); Matrix f2 = MatrixMultiply(MatrixTranslate(-0.42f, 0, 0), frame); std::swap(frame, f2); pistol(0.07f, 1); std::swap(frame, f2); }
    else if (id == "long") { pistol(0.22f, 1.05f); rt::DrawStatic(Cyl(), Local(frame, {0, 0.035f, 0.12f}, {0.05f, 0.06f, 0.05f}, PI / 2), body); }
    else if (id == "flare") { pistol(0.06f, 1.1f); Box(frame, {0, 0.035f, 0.2f}, {0.06f, 0.06f, 0.08f}, {240, 120, 40, 255}); }
    else if (id == "mega") { Matrix m = MatrixMultiply(MatrixScale(2.4f, 2.4f, 2.4f), frame); std::swap(frame, m); pistol(0.12f, 1.2f); std::swap(frame, m); }
    else if (T == "Shotgun") {
        bool dbl = id == "double";
        Box(frame, {0, 0.04f, 0.08f}, {0.065f, 0.08f, 0.2f}, body);
        for (int k = 0; k < (dbl ? 2 : 1); k++) Box(frame, {dbl ? (k ? 0.022f : -0.022f) : 0, 0.05f, 0.42f}, {0.045f, 0.045f, 0.46f}, dark);
        if (!dbl) Box(frame, {0, 0.0f, 0.36f}, {0.06f, 0.05f, 0.16f}, Mx(dark, BLACK, 0.2f));   // the pump
        if (id == "autoshot") Box(frame, {0, -0.07f, 0.14f}, {0.045f, 0.13f, 0.08f}, dark);
        Box(frame, {0, -0.05f, 0.0f}, {0.046f, 0.11f, 0.055f}, dark, -0.25f); Box(frame, {0, -0.012f, 0.035f}, {0.012f, 0.03f, 0.014f}, red);
        Box(frame, {0, 0.0f, -0.16f}, {0.05f, 0.1f, 0.24f}, body, 0.12f); Box(frame, {0.034f, 0.045f, 0.1f}, {0.003f, 0.025f, 0.07f}, stampC);
    }
    else if (id == "pocket") longGun(0.08f, 0.16f, false, 1, false, 1);
    else if (id == "tommy") longGun(0.22f, 0.24f, true, 2, false, 1.1f);
    else if (id == "carbine") longGun(0.32f, 0.26f, true, 1, false, 1);
    else if (id == "lever") { longGun(0.4f, 0.24f, true, 0, false, 1); Box(frame, {0, -0.04f, 0.08f}, {0.01f, 0.05f, 0.09f}, dark); }
    else if (id == "battle") longGun(0.3f, 0.3f, true, 1, false, 1.1f);
    else if (id == "sniper") longGun(0.55f, 0.3f, true, 1, true, 1);
    else if (id == "lmg" || id == "belt") {
        longGun(0.38f, 0.36f, true, 0, false, 1.4f);
        Box(frame, {0.07f, -0.02f, 0.06f}, {0.1f, 0.12f, 0.14f}, {120, 130, 80, 255});              // the ammo box (a toy lunchbox)
        if (id == "belt") for (int k = 0; k < 5; k++) Box(frame, {0.04f - k * 0.012f, 0.05f, 0.1f}, {0.01f, 0.02f, 0.03f}, {220, 180, 60, 255});
        for (int s = -1; s <= 1; s += 2) Box(frame, {s * 0.04f, -0.08f, 0.5f}, {0.012f, 0.16f, 0.012f}, dark, s * 0.3f);   // the bipod
    }
    else if (id == "crab") { longGun(0.12f, 0.24f, false, 0, false, 3.2f); Ball(frame, {0, 0.06f, 0.42f}, {0.07f, 0.05f, 0.05f}, {220, 70, 50, 255}); for (int s = -1; s <= 1; s += 2) Ball(frame, {s * 0.08f, 0.07f, 0.46f}, {0.03f, 0.02f, 0.035f}, {220, 70, 50, 255}); }
    else if (id == "gatling") {
        Box(frame, {0, 0.04f, 0.06f}, {0.12f, 0.12f, 0.22f}, body);
        for (int k = 0; k < 6; k++) { float a = spin + k * PI / 3; Box(frame, {cosf(a) * 0.04f, 0.05f + sinf(a) * 0.04f, 0.38f}, {0.022f, 0.022f, 0.44f}, dark); }
        Box(frame, {0.09f, 0.0f, -0.02f}, {0.02f, 0.02f, 0.1f}, red, spin);   // the crank
        Box(frame, {0, -0.05f, 0.0f}, {0.046f, 0.11f, 0.055f}, dark, -0.25f);
    }
    else if (id == "ray") { pistol(0.05f, 1.2f); Ball(frame, {0, 0.06f, 0.0f}, {0.05f, 0.05f, 0.05f}, {80, 255, 120, 255}, 1.0f); for (int k = 0; k < 3; k++) Box(frame, {0, 0.035f, 0.15f + k * 0.035f}, {0.08f - k * 0.015f, 0.08f - k * 0.015f, 0.012f}, {90, 220, 120, 255}); }
    else if (id == "net") { longGun(0.06f, 0.2f, false, 0, false, 2); Box(frame, {0, 0.045f, 0.3f}, {0.16f, 0.13f, 0.08f}, dark); Box(frame, {0, 0.045f, 0.33f}, {0.13f, 0.1f, 0.02f}, {200, 190, 150, 255}); }
    else if (id == "bread") { pistol(0.08f, 1.2f); Box(frame, {0, 0.11f, 0.06f}, {0.07f, 0.05f, 0.14f}, {210, 160, 90, 255}); }
    else if (id == "boomerang") { pistol(0.05f, 1.1f); Box(frame, {0, 0.1f, 0.12f}, {0.03f, 0.015f, 0.16f}, {250, 200, 60, 255}, 0, 0.5f); Box(frame, {0.05f, 0.1f, 0.07f}, {0.12f, 0.015f, 0.03f}, {250, 200, 60, 255}, 0, 0.5f); }
    else if (id == "bubble") { pistol(0.05f, 1.1f); for (int k = 0; k < 8; k++) { float a = k * PI / 4; Box(frame, {cosf(a) * 0.05f, 0.035f + sinf(a) * 0.05f, 0.2f}, {0.02f, 0.02f, 0.01f}, {250, 150, 200, 255}); } }
    else if (id == "magnet") { pistol(0.04f, 1.1f); for (int s = -1; s <= 1; s += 2) { Box(frame, {s * 0.05f, 0.04f, 0.22f}, {0.03f, 0.03f, 0.12f}, {220, 40, 40, 255}); Box(frame, {s * 0.05f, 0.04f, 0.29f}, {0.03f, 0.03f, 0.03f}, {230, 230, 235, 255}); } Box(frame, {0, 0.04f, 0.16f}, {0.13f, 0.03f, 0.03f}, {220, 40, 40, 255}); }
    else if (id == "firework") { longGun(0.3f, 0.2f, false, 0, false, 2.4f); Box(frame, {0, 0.05f, 0.5f}, {0.05f, 0.05f, 0.1f}, {230, 50, 60, 255}); }
    else if (id == "plunger") { longGun(0.3f, 0.22f, true, 0, false, 0.8f); rt::DrawStatic(Cone(), Local(frame, {0, 0.045f, 0.58f}, {0.06f, 0.06f, 0.06f}, -PI / 2), {200, 40, 40, 255}); }
    else if (id == "blower") { Ball(frame, {0, 0.06f, 0.08f}, {0.12f, 0.12f, 0.1f}, {240, 130, 40, 255}); Box(frame, {0, 0.06f, 0.36f}, {0.06f, 0.06f, 0.4f}, dark); Box(frame, {0, -0.06f, 0.0f}, {0.046f, 0.11f, 0.055f}, dark, -0.25f); }
    else if (id == "banana") { longGun(0.25f, 0.22f, false, 0, false, 2.6f); for (int k = 0; k < 4; k++) Box(frame, {(k - 1.5f) * 0.025f, 0.13f, 0.1f}, {0.02f, 0.02f, 0.14f}, {250, 220, 60, 255}, 0.3f); }
    else if (id == "honker") { pistol(0.04f, 1.1f); rt::DrawStatic(Cone(), Local(frame, {0, 0.035f, 0.32f}, {0.08f, 0.16f, 0.08f}, -PI / 2), {220, 180, 70, 255}); }
    else if (id == "lightning") { pistol(0.04f, 1.2f); for (int k = 0; k < 5; k++) rt::DrawStatic(Cyl(), Local(frame, {0, 0.035f, 0.14f + k * 0.03f}, {0.045f, 0.012f, 0.045f}, PI / 2), {200, 140, 60, 255}); Ball(frame, {0, 0.035f, 0.3f}, {0.03f, 0.03f, 0.03f}, {140, 200, 255, 255}, 1.2f); }
    else pistol(0.08f, 1);
}

// ---------------------------------------------------------------- the marsh at golden hour
void DrawMarsh(float t, bool night, float flare) {
    Color water = night ? Color{20, 30, 40, 255} : Color{96, 120, 128, 255};
    rt::DrawWorldCube({0, -0.15f, 90}, {240, 0.3f, 180}, water);
    // the sun's path on the water, and the far hill with the sun going down behind it
    if (!night) for (int k = 0; k < 10; k++) Glow(ID, {sinf(t * 0.6f + k) * 1.5f, 0.02f, 40.0f + k * 11}, {3.0f + k * 0.4f, 0.02f, 4}, {255, 190, 110, 255}, 0.5f - k * 0.04f);
    rt::DrawStatic(Sphere(), MatrixMultiply(MatrixScale(90, 18, 30), MatrixTranslate(0, -6, 175)), night ? Color{20, 26, 22, 255} : Color{86, 96, 64, 255});
    if (!night) { rt::DrawStaticGlow(Sphere(), MatrixMultiply(MatrixScale(22, 22, 22), MatrixTranslate(-40, 16, 230)), {255, 190, 110, 255}, 2.5f); }   // the sun, going down
    // the treeline: dark cones on the far bank
    for (int i = 0; i < 70; i++) { float x = -120 + i * 3.5f + Hf(i) * 2, z = 140 + Hf(i + 99) * 14, h = 7 + Hf(i + 7) * 7; rt::DrawStatic(Cone(), MatrixMultiply(MatrixScale(2.4f, h, 2.4f), MatrixTranslate(x, 0, z)), night ? Color{12, 20, 16, 255} : Color{46, 70, 46, 255}); }
    // dead trees standing in the water
    for (int i = 0; i < 6; i++) {
        float x = -70 + i * 28 + Hf(i + 3) * 10, z = 45 + Hf(i + 11) * 60, h = 8 + Hf(i) * 6;
        Color bark = night ? Color{30, 26, 24, 255} : Color{86, 72, 60, 255};
        rt::DrawStatic(Cyl(), MatrixMultiply(MatrixScale(0.3f, h, 0.3f), MatrixTranslate(x, 0, z)), bark);   // (round trunks and boughs, tapering)
        rt::DrawStatic(Cone(), MatrixMultiply(MatrixScale(0.3f, 1.6f, 0.3f), MatrixTranslate(x, h, z)), bark);
        for (int b = 0; b < 4; b++) { float a = Hf(i * 9 + b) * 6.28f, y = h * (0.45f + b * 0.13f); rt::DrawStatic(Cone(), MatrixMultiply(MatrixMultiply(MatrixScale(0.11f, 2.4f, 0.11f), MatrixRotateZ(-1.05f)), MatrixMultiply(MatrixRotateY(a), MatrixTranslate(x, y, z))), bark); }
    }
    // the reeds at the near edge (where the birds flush), swaying
    for (int i = 0; i < 170; i++) {   // clumps of reeds, fanned, with cattail heads
        float cx = -105 + Hf(i) * 210, cz = 6 + Hf(i + 1000) * 15, base = 0.9f + Hf(i + 2000) * 0.9f;
        int blades = 5 + (int)(Hf(i + 4000) * 5);
        for (int k = 0; k < blades; k++) {
            float a = (k - blades * 0.5f) * 0.12f + sinf(t * 1.3f + cx * 0.2f) * 0.07f, h = base * (0.7f + Hf(i * 13 + k) * 0.6f), dx = (Hf(i * 7 + k) - 0.5f) * 0.6f;
            Color c = Mx(night ? Color{24, 36, 24, 255} : Color{104, 132, 64, 255}, night ? Color{30, 30, 20, 255} : Color{170, 156, 86, 255}, Hf(i * 3 + k));
            rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixTranslate(0, 0.5f, 0), MatrixScale(0.05f, h, 0.05f)), MatrixRotateZ(a)), MatrixTranslate(cx + dx, 0, cz + (Hf(i * 5 + k) - 0.5f) * 0.6f)), c);
            if (k % 3 == 0) rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixTranslate(0, 0.5f, 0), MatrixScale(0.09f, 0.22f, 0.09f)), MatrixTranslate(0, h - 0.05f, 0)), MatrixMultiply(MatrixRotateZ(a), MatrixTranslate(cx + dx, 0, cz + (Hf(i * 5 + k) - 0.5f) * 0.6f))), night ? Color{30, 22, 16, 255} : Color{100, 62, 36, 255});
        }
    }
    // lily pads and a drifting log
    for (int i = 0; i < 30; i++) {   // (round pads, a few with a pale flower)
        Vector3 p{-60 + Hf(i + 77) * 120, 0.0f, 22 + Hf(i + 88) * 30};
        rt::DrawStatic(Cyl(), MatrixMultiply(MatrixScale(0.5f + 0.2f * Hf(i), 0.03f, 0.5f + 0.2f * Hf(i)), MatrixTranslate(p.x, p.y, p.z)), night ? Color{20, 40, 24, 255} : Color{70, 120, 60, 255});
        if (i % 4 == 0) rt::DrawStatic(Sphere(), MatrixMultiply(MatrixScale(0.12f, 0.08f, 0.12f), MatrixTranslate(p.x + 0.1f, 0.08f, p.z)), night ? Color{90, 80, 90, 255} : Color{240, 220, 230, 255});
    }
    rt::DrawStatic(Cyl(), MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixScale(0.28f, 5, 0.28f), MatrixRotateZ(PI / 2)), MatrixRotateY(0.4f)), MatrixTranslate(18 + sinf(t * 0.05f) * 3 + 2.3f, 0.1f, 34 + 1.0f)), {90, 70, 54, 255});   // the drifting log
    // the marsh's own life, which nobody's shooting at: frogs on the pads, two herons wading, dragonflies over the reeds
    // by day and fireflies by night
    for (int i = 0; i < 30; i += 7) {
        Vector3 p{-60 + Hf(i + 77) * 120, 0.0f, 22 + Hf(i + 88) * 30};
        float hop = fmodf(t * 0.1f + Hf(i) * 7, 7.0f) < 0.25f ? sinf(fmodf(t * 0.1f + Hf(i) * 7, 7.0f) / 0.25f * PI) * 0.3f : 0;
        Matrix f = MatrixMultiply(MatrixRotateY(Hf(i + 5) * 6.28f), MatrixTranslate(p.x, 0.06f + hop, p.z));
        Color fr = night ? Color{30, 50, 30, 255} : Color{90, 140, 60, 255};
        Ball(f, {0, 0.06f, 0}, {0.11f, 0.07f, 0.13f}, fr); Ball(f, {0, 0.1f, 0.1f}, {0.08f, 0.05f, 0.06f}, fr);
        for (int s = -1; s <= 1; s += 2) { Ball(f, {s * 0.05f, 0.14f, 0.12f}, {0.025f, 0.025f, 0.025f}, {220, 200, 80, 255}); Ball(f, {s * 0.1f, 0.03f, -0.04f}, {0.05f, 0.03f, 0.08f}, fr); }
    }
    for (int hn = 0; hn < 2; hn++) {   // a grey heron: long legs, an S neck, a dagger bill that now and then stabs down
        float hx = hn ? 26.0f : -31.0f, hz = hn ? 76.0f : 60.0f, yaw = hn ? 2.4f : 0.7f;
        float ph = fmodf(t * 0.13f + hn * 3.1f, 1.0f), stab = ph > 0.86f ? sinf((ph - 0.86f) / 0.14f * PI) : 0;
        Matrix f = MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(hx, 0, hz));
        Color g = night ? Color{50, 54, 60, 255} : Color{150, 156, 164, 255}, dk = night ? Color{30, 32, 36, 255} : Color{70, 74, 82, 255}, bill = {200, 170, 60, 255};
        for (int s = -1; s <= 1; s += 2) rt::DrawStatic(Cyl(), MatrixMultiply(MatrixScale(0.03f, 0.9f, 0.03f), MatrixMultiply(MatrixTranslate(s * 0.08f, -0.2f, 0), f)), {190, 150, 80, 255});
        Ball(f, {0, 0.9f, 0}, {0.22f, 0.24f, 0.4f}, g); Ball(f, {0, 0.95f, -0.3f}, {0.15f, 0.1f, 0.3f}, dk);   // body and folded wings
        Vector3 n0{0, 1.05f, 0.25f}, n1{0, 1.3f - stab * 0.5f, 0.38f + stab * 0.25f}, hd{0, 1.5f - stab * 0.9f, 0.45f + stab * 0.45f};
        rt::DrawStatic(Cyl(), MatrixMultiply(MatrixMultiply(MatrixScale(0.05f, Vector3Distance(n0, n1), 0.05f), MatrixRotateX(atan2f(n1.z - n0.z, n1.y - n0.y))), MatrixMultiply(MatrixTranslate(n0.x, n0.y, n0.z), f)), g);
        rt::DrawStatic(Cyl(), MatrixMultiply(MatrixMultiply(MatrixScale(0.045f, Vector3Distance(n1, hd), 0.045f), MatrixRotateX(atan2f(hd.z - n1.z, hd.y - n1.y))), MatrixMultiply(MatrixTranslate(n1.x, n1.y, n1.z), f)), g);
        Ball(f, hd, {0.07f, 0.07f, 0.09f}, g); Ball(f, {hd.x, hd.y + 0.04f, hd.z - 0.06f}, {0.02f, 0.02f, 0.12f}, dk);   // (the black crest)
        rt::DrawStatic(Cone(), MatrixMultiply(MatrixMultiply(MatrixScale(0.025f, 0.3f, 0.025f), MatrixRotateX(PI / 2 + 0.3f + stab * 0.9f)), MatrixMultiply(MatrixTranslate(hd.x, hd.y, hd.z + 0.05f), f)), bill);
    }
    if (!night) for (int k = 0; k < 8; k++) {   // dragonflies: darting, hovering, darting
        float seg = floorf(t * 0.7f + k * 0.37f), u = t * 0.7f + k * 0.37f - seg, e = u < 0.25f ? u / 0.25f : 1;
        Vector3 a{-40 + Hf((uint32_t)(seg + k * 31)) * 80, 1.2f + Hf((uint32_t)(seg + k * 13)) * 1.2f, 8 + Hf((uint32_t)(seg + k * 7)) * 14};
        Vector3 b{-40 + Hf((uint32_t)(seg + 1 + k * 31)) * 80, 1.2f + Hf((uint32_t)(seg + 1 + k * 13)) * 1.2f, 8 + Hf((uint32_t)(seg + 1 + k * 7)) * 14};
        Vector3 p = Vector3Lerp(a, b, e * e * (3 - 2 * e)); p.y += 0.04f * sinf(t * 40 + k);
        Ball(ID, p, {0.015f, 0.015f, 0.09f}, k % 2 ? Color{60, 160, 200, 255} : Color{200, 60, 50, 255});
        for (int s = -1; s <= 1; s += 2) Glow(ID, {p.x + s * 0.07f, p.y + 0.01f, p.z}, {0.07f, 0.004f, 0.025f}, {220, 236, 255, 255}, 0.3f + 0.2f * sinf(t * 60 + k));
    }
    if (night) for (int k = 0; k < 50; k++) {   // fireflies over the reeds, blinking out of step
        float bl = sinf(t * (1.3f + Hf(k) * 0.8f) + k * 2.1f); if (bl < 0.4f) continue;
        Vector3 p{-60 + Hf(k + 300) * 120 + sinf(t * 0.3f + k) * 1.5f, 0.8f + Hf(k + 400) * 2.0f + sinf(t * 0.5f + k) * 0.3f, 7 + Hf(k + 500) * 18};
        Ball(ID, p, {0.035f, 0.035f, 0.035f}, {220, 255, 120, 255}, 2.0f * (bl - 0.4f) / 0.6f);
    }
    (void)flare;
}

// ---------------------------------------------------------------- the porch and the room
void DrawPorch(const World& w, int me, float t) {
    Color plank{150, 104, 64, 255}, rail{120, 80, 50, 255}, post{100, 66, 40, 255}, wall{134, 92, 58, 255};
    rt::DrawWorldCube({0, -0.1f, 0}, {20, 0.2f, 4.4f}, plank);
    for (int i = 0; i < 20; i++) rt::DrawWorldCube({-9.5f + i, 0.005f, 0}, {0.03f, 0.01f, 4.4f}, Mx(plank, BLACK, 0.3f));
    rt::DrawWorldCube({0, 1.0f, 1.7f}, {18.4f, 0.12f, 0.18f}, rail);           // the rail
    rt::DrawWorldCube({0, 0.5f, 1.7f}, {18.4f, 0.08f, 0.08f}, rail);
    for (int s = 0; s <= MAX_PLAYERS; s++) {                                     // posts, partitions, the roof's posts
        float x = (s - 3.0f) * STALL_W;
        static const Model* pp = rt::LoadAsset("fowl/porchpost.glb"); static const Model* pt = rt::LoadAsset("fowl/partition.glb");
        if (pp) rt::DrawPbr(*pp, MatrixTranslate(x, 0, 1.7f), WHITE, 0.2f); else rt::DrawWorldCube({x, 1.9f, 1.7f}, {0.12f, 3.8f, 0.12f}, post);
        if (s > 0 && s < MAX_PLAYERS) { if (pt) rt::DrawPbr(*pt, MatrixTranslate(x, 0, 0.1f), WHITE, 0.2f); else rt::DrawWorldCube({x, 0.55f, 0.1f}, {0.06f, 1.1f, 3.0f}, Mx(rail, WHITE, 0.1f)); }   // the waist-high partition
    }
    rt::DrawWorldCube({0, 3.85f, 0.2f}, {20, 0.15f, 4.6f}, Mx(wall, BLACK, 0.2f));   // the roof
    // each stall: a numbered post (pips for the number), a hook, the ammo tray and a scoreboard flap; the flag
    for (int s = 0; s < MAX_PLAYERS; s++) {
        float x = (s - 2.5f) * STALL_W;
        // the stall's number: a painted enamel plaque with a rim, its number in round black pips
        rt::DrawStatic(RoundCube(), MatrixMultiply(MatrixScale(0.28f, 0.34f, 0.04f), MatrixTranslate(x - 1.2f, 1.25f, 1.56f)), {120, 40, 34, 255});
        rt::DrawStatic(RoundCube(), MatrixMultiply(MatrixScale(0.24f, 0.3f, 0.04f), MatrixTranslate(x - 1.2f, 1.25f, 1.545f)), {236, 226, 196, 255});
        for (int k = 0; k <= s; k++) rt::DrawStatic(Cyl(), MatrixMultiply(MatrixMultiply(MatrixScale(0.024f, 0.01f, 0.024f), MatrixRotateX(-PI / 2)), MatrixTranslate(x - 1.28f + (k % 3) * 0.08f, 1.32f - (k / 3) * 0.1f, 1.526f)), {40, 30, 24, 255});
        rt::DrawStatic(RoundCube(), MatrixMultiply(MatrixScale(0.5f, 0.06f, 0.22f), MatrixTranslate(x + 0.9f, 0.95f, 1.62f)), Mx(rail, BLACK, 0.2f));   // the ammo tray
        for (int k = 0; k < 5; k++) {   // shotgun cartridges stood in a row: red paper tubes on brass heads
            rt::DrawStatic(Cyl(), MatrixMultiply(MatrixScale(0.013f, 0.05f, 0.013f), MatrixTranslate(x + 0.72f + k * 0.08f, 0.98f, 1.62f)), {200, 50, 42, 255});
            rt::DrawStatic(Cyl(), MatrixMultiply(MatrixScale(0.014f, 0.016f, 0.014f), MatrixTranslate(x + 0.72f + k * 0.08f, 0.98f, 1.62f)), {210, 170, 80, 255});
        }
        if (s < (int)w.players.size()) {
            const Player& p = w.players[s];
            if (p.hook.Has()) DrawToyGun(p.hook.def, MatrixMultiply(MatrixRotateX(-PI / 2), MatrixTranslate(x + 1.35f, 1.6f, -1.5f)), p.paint, 0, 1);
            if (p.flag >= 0) { rt::DrawWorldCube({x + 1.3f, 2.5f, 1.6f}, {0.03f, 1.4f, 0.03f}, post); rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.5f, 0.3f, 0.02f), MatrixRotateY(sinf(t * 3 + s) * 0.3f)), MatrixTranslate(x + 1.55f, 3.0f, 1.6f)), Color{(unsigned char)(80 + 30 * s), 60, (unsigned char)(220 - 30 * s), 255}); }
            if (p.glitter) for (int k = 0; k < 24; k++) rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.03f, 0.03f, 0.03f), MatrixTranslate(x - 1.3f + Hf(k + s * 50) * 2.6f, 0.02f + Hf(k + 7 + s * 50) * 1.1f, -1 + Hf(k + 3 + s * 50) * 2.6f)), Color{(unsigned char)(150 + Hf(k) * 100), (unsigned char)(120 + Hf(k + 1) * 130), 255, 255}, 0.8f + 0.4f * sinf(t * 6 + k));
            // the cardboard moose (Cardboard Sightline), and the bagpiper (Loud Neighbor)
            if (p.cardboardT > 0) { Color cb{176, 140, 96, 255}; rt::DrawWorldCube({x, 1.2f, 6}, {1.6f, 1.4f, 0.04f}, cb); rt::DrawWorldCube({x - 0.9f, 2.0f, 6}, {0.6f, 0.5f, 0.04f}, cb); for (int s2 = -1; s2 <= 1; s2 += 2) rt::DrawWorldCube({x - 0.9f + s2 * 0.45f, 2.5f, 6}, {0.5f, 0.12f, 0.04f}, cb); for (int k = 0; k < 4; k++) rt::DrawWorldCube({x - 0.6f + k * 0.4f, 0.25f, 6}, {0.12f, 0.5f, 0.04f}, cb); }
        }
    }
    // the clubhouse wall behind, with a gate for each stall
    for (int s = 0; s <= MAX_PLAYERS; s++) {
        float x0 = s == 0 ? -15.0f : (s - 3.0f) * STALL_W - 1.5f + 0.75f, x1 = s == MAX_PLAYERS ? 15.0f : (s - 2.5f) * STALL_W - 0.7f;
        if (s == 0) x0 = -15, x1 = (0 - 2.5f) * STALL_W - 0.7f;
        else if (s == MAX_PLAYERS) { x0 = (MAX_PLAYERS - 1 - 2.5f) * STALL_W + 0.7f; x1 = 15; }
        else { x0 = (s - 1 - 2.5f) * STALL_W + 0.7f; x1 = (s - 2.5f) * STALL_W - 0.7f; }
        rt::DrawWorldCube({(x0 + x1) / 2, 1.6f, -2}, {x1 - x0, 3.2f, 0.3f}, wall);
    }
    bool open = w.phase == PH_INTER;
    for (int s = 0; s < MAX_PLAYERS; s++) { float x = (s - 2.5f) * STALL_W; rt::DrawWorldCube({x, 2.6f, -2}, {1.4f, 1.2f, 0.3f}, wall); if (!open) for (int k = 0; k < 4; k++) rt::DrawWorldCube({x - 0.5f + k * 0.33f, 1.0f, -2}, {0.06f, 2.0f, 0.06f}, Color{60, 60, 66, 255}); }   // (the gates: bars when shut)
    (void)me;
}
static void DrawCounterGuns(float x, float z, float t) {
    int n = 0; for (int i = 0; i < (int)D().guns.size(); i++) { if (D().guns[i].price <= 0) continue; float gx = x - 2.4f + (n % 8) * 0.62f, gy = 1.12f + (n / 8) * 0.0f, gz = z - 0.35f + (n / 8) * 0.24f; DrawToyGun(i, MatrixMultiply(MatrixRotateY(PI / 2), MatrixTranslate(gx, gy, gz)), 0, t, 0.8f); n++; }
}
static void DrawRobot(Vector3 at, float t) {   // Mr. Zappa: a toy gun stood on its grip, with eyes and little arms
    Matrix f = MatrixMultiply(MatrixScale(4.5f, 4.5f, 4.5f), MatrixMultiply(MatrixRotateX(-PI / 2), MatrixMultiply(MatrixRotateY(sinf(t * 0.7f) * 0.3f + PI), MatrixTranslate(at.x, at.y + 0.9f, at.z))));
    DrawToyGun(0, f, 0);
    Matrix h = MatrixTranslate(at.x, at.y + 1.75f, at.z + 0.18f);
    for (int s = -1; s <= 1; s += 2) { Ball(h, {s * 0.08f, 0, 0}, {0.06f, 0.06f, 0.03f}, WHITE); Ball(h, {s * 0.08f, 0, 0.025f}, {0.025f, 0.025f, 0.02f}, BLACK); }
    for (int s = -1; s <= 1; s += 2) rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.05f, 0.4f, 0.05f), MatrixRotateZ(s * (0.6f + 0.2f * sinf(t * 2 + s)))), MatrixTranslate(at.x + s * 0.25f, at.y + 1.2f, at.z)), {150, 150, 158, 255});
}
static void DrawRaccoon(Vector3 at, float t) {
    Color fur{120, 116, 112, 255}, mask{40, 40, 44, 255}, vest{170, 40, 120, 255};
    Matrix f = MatrixMultiply(MatrixRotateY(PI + sinf(t * 0.9f) * 0.2f), MatrixTranslate(at.x, at.y, at.z));
    Ball(f, {0, 0.75f, 0}, {0.32f, 0.42f, 0.26f}, vest); Ball(f, {0, 0.55f, 0}, {0.3f, 0.3f, 0.25f}, fur);
    Ball(f, {0, 1.25f, 0.02f}, {0.24f, 0.2f, 0.22f}, fur); Box(f, {0, 1.27f, 0.12f}, {0.36f, 0.08f, 0.12f}, mask);
    for (int s = -1; s <= 1; s += 2) { Ball(f, {s * 0.08f, 1.28f, 0.21f}, {0.03f, 0.03f, 0.02f}, WHITE); Ball(f, {s * 0.17f, 1.45f, 0}, {0.07f, 0.09f, 0.04f}, fur); }
    Ball(f, {0, 1.2f, 0.24f}, {0.04f, 0.03f, 0.04f}, BLACK);
    for (int k = 0; k < 4; k++) Ball(f, {0.1f, 0.3f - k * 0.04f, -0.3f - k * 0.1f}, {0.09f, 0.09f, 0.09f}, k % 2 ? mask : fur);   // the ringed tail
}
void DrawRoom(const World& w, float t) {
    Color floorC{120, 84, 56, 255}, wallC{150, 110, 72, 255}, wains{90, 60, 40, 255};
    rt::DrawWorldCube({0, -0.1f, -9.5f}, {30.6f, 0.2f, 15.2f}, floorC);
    for (int i = 0; i < 15; i++) rt::DrawWorldCube({0, 0.005f, -2.5f - i}, {30, 0.01f, 0.03f}, Mx(floorC, BLACK, 0.3f));
    rt::DrawWorldCube({0, 1.8f, -17.2f}, {30.6f, 3.6f, 0.3f}, wallC); rt::DrawWorldCube({-15.2f, 1.8f, -9.5f}, {0.3f, 3.6f, 15.4f}, wallC); rt::DrawWorldCube({15.2f, 1.8f, -9.5f}, {0.3f, 3.6f, 15.4f}, wallC);
    rt::DrawWorldCube({0, 0.5f, -17.0f}, {30.4f, 1.0f, 0.1f}, wains);
    rt::DrawWorldCube({0, 3.65f, -9.5f}, {30.6f, 0.15f, 15.4f}, Mx(wallC, BLACK, 0.35f));
    static const Model* decor = rt::LoadAsset("fowl/clubdecor.glb");
    if (decor) {   // beams, prints, windows, moose, decoys, lamp shades, rug, stove, coat rack (tools/artgen/fowl_props.py)
        rt::DrawPbr(*decor, MatrixIdentity(), WHITE, 0.2f);
        for (int i = 0; i < 4; i++) rt::DrawStaticGlow(Sphere(), MatrixMultiply(MatrixScale(0.14f, 0.1f, 0.14f), MatrixTranslate(-10.5f + i * 7, 3.02f, -9)), {255, 220, 160, 255}, 1.4f);
        for (int k = 0; k < 2; k++) {   // the window panes: dusk over the marsh, the sun's band low on the glass
            float x = k ? 5.5f : -1.5f;
            rt::DrawCubeGlow(MatrixMultiply(MatrixScale(1.76f, 0.46f, 0.02f), MatrixTranslate(x, 2.84f, -17.04f)), {120, 110, 170, 255}, 0.5f);
            rt::DrawCubeGlow(MatrixMultiply(MatrixScale(1.76f, 0.46f, 0.02f), MatrixTranslate(x, 2.36f, -17.04f)), {240, 150, 90, 255}, 0.7f);
        }
        float fl = 0.8f + 0.25f * sinf(t * 7.3f) * sinf(t * 3.1f);   // the stove's firelight through the grate
        rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.2f, 0.18f, 0.02f), MatrixTranslate(-8.6f, 0.545f, -16.04f)), {255, 140, 50, 255}, 1.6f * fl);
    } else
        for (int i = 0; i < 4; i++) rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.5f, 0.12f, 0.5f), MatrixTranslate(-10.5f + i * 7, 3.5f, -9)), {255, 220, 160, 255}, 1.2f);
    // 0 the gun counter: a glass case of guns, the pegboard of attachments, Mr. Zappa
    Vector3 s0 = World::Station(0);
    static const Model* gcase = rt::LoadAsset("fowl/guncase.glb"); static const Model* peg = rt::LoadAsset("fowl/pegboard.glb");
    if (gcase) { DrawCounterGuns(s0.x, s0.z, t); rt::DrawPbr(*gcase, MatrixTranslate(s0.x, 0, s0.z), WHITE, 0.2f); }
    else {
        rt::DrawWorldCube({s0.x, 0.5f, s0.z}, {5.6f, 1.0f, 1.0f}, {90, 60, 40, 255});
        rt::DrawCubeGlow(MatrixMultiply(MatrixScale(5.4f, 0.04f, 0.9f), MatrixTranslate(s0.x, 1.04f, s0.z)), {190, 220, 230, 255}, 0.15f);
        DrawCounterGuns(s0.x, s0.z, t);
    }
    if (peg) rt::DrawPbr(*peg, MatrixTranslate(s0.x, 0, s0.z), WHITE, 0.2f); else rt::DrawWorldCube({s0.x, 2.2f, s0.z - 1.5f}, {5, 1.6f, 0.06f}, {200, 180, 140, 255});
    for (int i = 0; i < 24; i++) {   // the attachment packs on their hooks: a printed card with a plastic bubble
        Vector3 c{s0.x - 2.2f + (i % 8) * 0.62f, 1.62f + (i / 8) * 0.45f, s0.z - 1.37f};
        Color pc = i % 3 == 0 ? Color{200, 50, 50, 255} : i % 3 == 1 ? Color{60, 110, 180, 255} : Color{230, 190, 60, 255};
        rt::DrawWorldCube(c, {0.2f, 0.26f, 0.01f}, pc);
        rt::DrawWorldCube({c.x, c.y - 0.03f, c.z + 0.025f}, {0.13f, 0.12f, 0.04f}, i % 2 ? Color{120, 120, 128, 255} : Color{40, 40, 44, 255});
    }
    DrawRobot({s0.x, 0, s0.z - 0.95f}, t);
    // 1 the mystery-gun machine: a big carnival gumball machine (tools/artgen/fowl_props.py), its crank turning
    Vector3 s1 = World::Station(1);
    const Model* gm = rt::LoadAsset("fowl/gumball.glb"); const Model* gc = rt::LoadAsset("fowl/gumball_crank.glb");
    if (gm && gc) {
        rt::DrawPbr(*gm, MatrixTranslate(s1.x, 0, s1.z), WHITE, 0.2f);
        bool turning = false; for (const auto& p : w.players) if (p.pendingCapsule >= 0) turning = true;
        float ang = turning ? t * 5.0f : 0.15f * sinf(t * 0.6f);
        rt::DrawPbr(*gc, MatrixMultiply(MatrixRotateZ(-ang), MatrixTranslate(s1.x, 1.38f, s1.z + 0.47f)), WHITE, 0.2f);
    } else {
    rt::DrawWorldCube({s1.x, 0.6f, s1.z}, {1.1f, 1.2f, 1.1f}, {200, 40, 40, 255});
    rt::DrawStaticGlow(Sphere(), MatrixMultiply(MatrixScale(0.75f, 0.75f, 0.75f), MatrixTranslate(s1.x, 1.85f, s1.z)), {220, 230, 240, 255}, 0.1f);
    for (int k = 0; k < 9; k++) rt::DrawStatic(Sphere(), MatrixMultiply(MatrixScale(0.16f, 0.16f, 0.16f), MatrixTranslate(s1.x - 0.35f + (k % 3) * 0.33f, 1.55f + (k / 3) * 0.25f, s1.z - 0.2f + (k % 2) * 0.3f)), k % 3 == 0 ? Color{240, 200, 60, 255} : k % 3 == 1 ? Color{70, 170, 230, 255} : Color{240, 120, 160, 255});
    rt::DrawStatic(Sphere(), MatrixMultiply(MatrixScale(0.22f, 0.22f, 0.22f), MatrixTranslate(s1.x, 2.62f, s1.z)), {200, 40, 40, 255});
    rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.06f, 0.5f, 0.06f), MatrixRotateX(t * 0.4f)), MatrixTranslate(s1.x + 0.6f, 0.9f, s1.z + 0.2f)), {220, 180, 70, 255});
    }
    // 2 the slot row: six one-armed bandits along the back wall, one a stall; the reels behind the glass are each
    // player's own, the arm drops on a pull, the crown's lamps chase
    const Model* sm = rt::LoadAsset("fowl/slot.glb"); const Model* sh = rt::LoadAsset("fowl/slot_handle.glb");
    static const Color SYM[5] = {{230, 210, 60, 255}, {150, 100, 60, 255}, {120, 124, 134, 255}, {240, 180, 40, 255}, {220, 40, 40, 255}};   // duck, dog, gun, bell, Zappa
    for (int k = 0; k < 6 && sm && sh; k++) {
        float x = -3 + k * 2.0f, z = -16.4f;
        const Player* pl = k < (int)w.players.size() ? &w.players[k] : nullptr; bool spin = pl && pl->slotT > 0;
        rt::DrawPbr(*sm, MatrixTranslate(x, 0, z), WHITE, 0.2f);
        float pull = spin ? std::max(0.0f, 1 - fabsf(pl->slotT - (D().slotPull - 0.25f)) * 3) : 0;
        rt::DrawPbr(*sh, MatrixMultiply(MatrixRotateX(pull * 1.1f), MatrixTranslate(x + 0.36f, 1.15f, z)), WHITE, 0.2f);
        for (int r = 0; r < 3; r++) {
            bool rolling = spin && pl->slotT > 0.4f + r * 0.5f;
            int sym = rolling ? (int)(t * 14 + r * 3 + k) % 5 : pl ? std::clamp(pl->reels[r], 0, 4) : (k + r * 2) % 5;
            float bob = rolling ? fmodf(t * 14, 1.0f) * 0.12f - 0.06f : 0;
            rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.13f, 0.19f, 0.004f), MatrixTranslate(x + (r - 1) * 0.17f, 1.2f, z + 0.288f)), {245, 240, 228, 255}, 0.35f);
            rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.08f, 0.08f, 0.004f), MatrixTranslate(x + (r - 1) * 0.17f, 1.2f + bob, z + 0.2895f)), SYM[sym], 0.6f);
        }
        for (int q = 0; q < 7; q++) { float a = PI * (q + 0.5f) / 7; bool on = ((int)(t * 6) + q + k) % 3 == 0 || (pl && pl->slotWin > 0 && pl->slotT <= 0 && fmodf(t, 0.4f) < 0.2f);
            rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.035f, 0.035f, 0.02f), MatrixTranslate(x + cosf(a) * 0.29f, 1.5f + sinf(a) * 0.29f, z + 0.275f)), on ? Color{255, 230, 140, 255} : Color{120, 90, 40, 255}, on ? 1.6f : 0.1f); }
        rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.48f, 0.18f, 0.004f), MatrixTranslate(x, 1.62f, z + 0.272f)), Color{255, 236, 190, 255}, 0.5f + 0.15f * sinf(t * 3 + k));
    }
    for (int k = 0; k < 6 && !(sm && sh); k++) {
        float x = -3 + k * 2.0f, z = -16.4f;
        rt::DrawWorldCube({x, 0.95f, z}, {1.3f, 1.9f, 0.9f}, {170, 30, 40, 255});
        rt::DrawCubeGlow(MatrixMultiply(MatrixScale(1.0f, 0.4f, 0.05f), MatrixTranslate(x, 1.25f, z + 0.46f)), {255, 240, 200, 255}, 0.5f);
        rt::DrawCubeGlow(MatrixMultiply(MatrixScale(1.2f, 0.25f, 0.05f), MatrixTranslate(x, 1.95f, z + 0.46f)), Color{(unsigned char)(200 + 55 * sinf(t * 4 + k)), 200, 60, 255}, 0.9f);
        rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.05f, 0.45f, 0.05f), MatrixRotateX(-0.3f)), MatrixTranslate(x + 0.72f, 1.3f, z + 0.1f)), {200, 200, 210, 255});
        rt::DrawStatic(Sphere(), MatrixMultiply(MatrixScale(0.08f, 0.08f, 0.08f), MatrixTranslate(x + 0.72f, 1.55f, z + 0.18f)), {220, 40, 40, 255});
    }
    // 3 the scratch-off counter: a dispenser and coins on strings
    Vector3 s3 = World::Station(3);
    static const Model* sc = rt::LoadAsset("fowl/scratchcounter.glb");
    if (sc) rt::DrawPbr(*sc, MatrixTranslate(s3.x, 0, s3.z), WHITE, 0.2f);
    else { rt::DrawWorldCube({s3.x, 0.55f, s3.z}, {2.4f, 1.1f, 0.9f}, {70, 110, 80, 255}); rt::DrawWorldCube({s3.x - 0.6f, 1.5f, s3.z - 0.2f}, {0.8f, 0.8f, 0.5f}, {220, 190, 70, 255}); }
    for (int k = 0; k < 4; k++) rt::DrawStatic(Cyl(), MatrixMultiply(MatrixScale(0.05f, 0.01f, 0.05f), MatrixTranslate(s3.x + 0.3f + k * 0.2f, 1.12f + sinf(t * 2 + k) * 0.02f, s3.z + 0.2f)), {230, 200, 90, 255});
    // 4 the Slop Shop: a garish booth with a neon sign, the raccoon in a vest
    Vector3 s4 = World::Station(4);
    static const Model* ss = rt::LoadAsset("fowl/slopshop.glb");
    if (ss) rt::DrawPbr(*ss, MatrixTranslate(s4.x, 0, s4.z), WHITE, 0.2f);
    else { rt::DrawWorldCube({s4.x + 0.9f, 0.6f, s4.z}, {1.0f, 1.2f, 3.0f}, {120, 50, 140, 255}); rt::DrawWorldCube({s4.x + 1.8f, 1.8f, s4.z}, {0.3f, 3.6f, 3.4f}, {80, 30, 100, 255}); }
    rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.08f, 0.5f, 2.6f), MatrixTranslate(s4.x + 1.6f, 2.9f, s4.z)), Color{255, (unsigned char)(80 + 60 * (sinf(t * 7) > 0.6f)), 200, 255}, 1.8f);
    DrawRaccoon({s4.x + 1.2f, 1.2f, s4.z}, t);
    // 5 the trophy wall
    Vector3 s5 = World::Station(5);
    static const Model* tw = rt::LoadAsset("fowl/trophywall.glb");
    if (tw) rt::DrawPbr(*tw, MatrixTranslate(0, 0, s5.z), WHITE, 0.2f);
    else rt::DrawWorldCube({-15.0f, 1.8f, s5.z}, {0.1f, 2.4f, 3.6f}, {70, 50, 34, 255});
    for (int k = 0; k < 6 && !tw; k++) { float z = s5.z - 1.4f + k * 0.56f; rt::DrawWorldCube({-14.85f, 1.4f + (k % 2) * 0.8f, z}, {0.2f, 0.06f, 0.4f}, {200, 170, 80, 255}); rt::DrawStatic(Cone(), MatrixMultiply(MatrixScale(0.12f, 0.3f, 0.12f), MatrixTranslate(-14.85f, 1.45f + (k % 2) * 0.8f, z)), {230, 190, 70, 255}); }
    // 6 the bar
    Vector3 s6 = World::Station(6);
    static const Model* cb = rt::LoadAsset("fowl/clubbar.glb");
    if (cb) rt::DrawPbr(*cb, MatrixTranslate(s6.x, 0, s6.z), WHITE, 0.2f);
    else { rt::DrawWorldCube({s6.x, 0.55f, s6.z}, {4.0f, 1.1f, 0.8f}, {84, 48, 30, 255}); rt::DrawWorldCube({s6.x, 1.12f, s6.z}, {4.1f, 0.06f, 0.9f}, {200, 160, 80, 255}); }
    for (int k = 0; k < 8 && !cb; k++) rt::DrawWorldCube({s6.x - 1.6f + k * 0.45f, 1.7f, s6.z - 0.9f}, {0.1f, 0.3f, 0.1f}, k % 2 ? Color{60, 120, 70, 255} : Color{140, 70, 40, 255});
    // the floor guns anyone can take
    for (const auto& f : w.floor) DrawToyGun(f.g.def, MatrixMultiply(MatrixRotateZ(PI / 2), MatrixTranslate(f.p.x, 0.05f, f.p.z)), 0);
}

// ---------------------------------------------------------------- birds
void DrawBird(const Bird& b, float t, bool night) {
    if (b.st == BI_GONE && !b.retrieved) { if (b.p.y > -1 && b.killer >= 0) {} else return; }
    const BirdDef& B = D().birds[b.def]; float r = B.r * 1.3f;
    Vector3 fwd = b.st == BI_FLY ? b.v : Vector3{0, 0, 1}; if (Vector3Length(fwd) < 0.1f) fwd = {0, 0, 1};
    if (B.pattern == "ufo") {
        Matrix f = Facing(b.p, {1, 0, 0});
        rt::DrawStatic(Sphere(), Local(f, {0, 0, 0}, {r * 1.6f, r * 0.35f, r * 1.6f}), B.body);
        rt::DrawStaticGlow(Sphere(), Local(f, {0, r * 0.3f, 0}, {r * 0.7f, r * 0.55f, r * 0.7f}), B.head, 0.4f);
        for (int k = 0; k < 8; k++) { float a = k * PI / 4 + t * 2; Glow(f, {cosf(a) * r * 1.4f, -0.05f, sinf(a) * r * 1.4f}, {0.18f, 0.1f, 0.18f}, k % 2 ? Color{120, 255, 160, 255} : Color{255, 230, 120, 255}, 1.2f); }
        if (fmodf(b.t, 5.0f) < 2.5f && b.t > 2) rt::DrawCubeGlow(MatrixMultiply(MatrixScale(r * 1.4f, 26, r * 1.4f), MatrixTranslate(b.p.x, b.p.y - 13, b.p.z)), {120, 255, 160, 255}, 0.15f);
        return;
    }
    if (B.clay) { Matrix f = Facing(b.p, fwd); rt::DrawStatic(Cyl(), Local(f, {0, 0, 0}, {r, r * 0.25f, r}, 0, 0, b.t * 12), B.body); return; }
    float flap = b.st == BI_FLY ? sinf(t * (B.r < 0.3f ? 22.0f : B.r > 0.65f ? 6.0f : 12.0f) + b.seed) : 0.15f;
    Matrix f = Facing(b.p, fwd);
    if (b.st == BI_FALL) f = MatrixMultiply(MatrixMultiply(MatrixRotateZ(b.t * 9), MatrixRotateX(b.t * 5)), MatrixMultiply(MatrixTranslate(0, 0, 0), Facing(b.p, {0, 0, 1})));
    bool surprised = b.st == BI_HIT || b.st == BI_FALL;
    float gl = b.golden ? 0.6f : B.id == "phoenix" || B.id == "firebird" ? 1.0f : 0;
    Color body = B.body, wing = B.wing, head = B.head;
    if (b.bubbleT > 0) Ball(f, {0, 0, 0}, {r * 1.9f, r * 1.9f, r * 1.9f}, {250, 190, 230, 255}, 0.25f);
    if (B.pattern == "ground") {   // the turkey runs
        float step = sinf(t * 12 + b.seed);
        Ball(f, {0, 0.5f, 0}, {r * 0.8f, r * 0.7f, r}, body, 0); Ball(f, {0, 0.95f, r * 0.8f}, {r * 0.25f, r * 0.3f, r * 0.25f}, head);
        for (int k = 0; k < 7; k++) { float a = -0.9f + k * 0.3f; Box(f, {sinf(a) * r, 0.8f + cosf(a) * r * 0.7f, -r * 0.9f}, {r * 0.25f, r * 1.1f, 0.05f}, k % 2 ? wing : Mx(wing, {60, 40, 20, 255}, 0.4f), 0, 0, -a); }
        for (int s = -1; s <= 1; s += 2) Box(f, {s * 0.15f, 0.2f, step * s * 0.1f}, {0.05f, 0.4f, 0.05f}, {200, 160, 80, 255});
        return;
    }
    Ball(f, {0, 0, 0}, {r * 0.75f, r * 0.62f, r * 1.1f}, body, gl);
    // the head: forward and up (the swan's on a long neck)
    Vector3 hp = B.id == "swan" ? Vector3{0, r * 0.95f, r * 1.25f} : Vector3{0, r * 0.35f, r * 0.95f};
    if (B.id == "swan") for (int k = 0; k < 4; k++) Ball(f, {0, r * (0.25f + k * 0.2f), r * (0.9f + k * 0.1f)}, {r * 0.16f, r * 0.2f, r * 0.16f}, body);
    Ball(f, hp, {r * 0.42f, r * 0.4f, r * 0.42f}, head, gl);
    Box(f, Vector3Add(hp, {0, -r * 0.06f, r * 0.42f}), {r * 0.28f, r * 0.1f, r * 0.4f}, B.id == "crow" ? Color{40, 40, 40, 255} : B.id == "swan" ? Color{230, 130, 40, 255} : Color{236, 170, 50, 255});
    // the eyes (decoys have none: that's the tell); wide when hit; glowing at night
    if (!B.decoy) for (int s = -1; s <= 1; s += 2) {
        float ew = surprised ? 0.2f : 0.12f;
        Vector3 ep = Vector3Add(hp, {s * r * 0.3f, r * 0.12f, r * 0.2f});
        if (night) Ball(f, ep, {r * ew, r * ew, r * ew}, {255, 230, 120, 255}, 1.5f);
        else { Ball(f, ep, {r * ew, r * ew, r * ew * 0.6f}, WHITE); Ball(f, Vector3Add(ep, {s * r * 0.04f, 0, r * 0.03f}), {r * ew * 0.45f, r * ew * 0.45f, r * ew * 0.4f}, BLACK); }
    }
    if (B.armor) Ball(f, Vector3Add(hp, {0, r * 0.22f, 0}), {r * 0.48f, r * 0.3f, r * 0.48f}, {150, 150, 160, 255});   // the tiny helmet
    if (B.decoy) for (int k = 0; k < 3; k++) Box(f, {0, r * (0.3f - k * 0.25f), 0}, {r * 1.52f, 0.01f, r * 2.2f}, {110, 80, 50, 255});   // (wood grain)
    // the wings, flapping
    float span = r * (B.id == "swan" ? 2.4f : 1.8f);
    for (int s = -1; s <= 1; s += 2) {
        float a = s * (0.25f + flap * 0.85f);
        Matrix wf = MatrixMultiply(MatrixMultiply(MatrixMultiply(MatrixTranslate(s * span * 0.5f, 0, 0), MatrixScale(1, 1, 1)), MatrixRotateZ(a)), MatrixMultiply(MatrixTranslate(s * r * 0.4f, r * 0.2f, 0), f));
        rt::DrawCubeM(MatrixMultiply(MatrixScale(span, r * 0.08f, r * 0.9f), wf), wing);
        if (gl > 0) rt::DrawCubeGlow(MatrixMultiply(MatrixScale(span * 0.6f, r * 0.05f, r * 0.5f), wf), {255, 200, 80, 255}, gl);
    }
    Box(f, {0, 0, -r * 1.1f}, {r * 0.5f, r * 0.12f, r * 0.5f}, wing);   // the tail
    if (b.plungers > 0) { rt::DrawStatic(Cone(), Local(f, {0, r * 0.6f, 0}, {0.12f, 0.12f, 0.12f}), {200, 40, 40, 255}); Box(f, {0, r * 0.9f, 0}, {0.03f, 0.4f, 0.03f}, {150, 110, 60, 255}); }
    if (b.clampT > 0) Ball(f, {0, -r * 0.6f, 0}, {r * 0.4f, r * 0.25f, r * 0.35f}, {220, 70, 50, 255});
    if (b.breadT > 0) Box(f, Vector3Add(hp, {0, -r * 0.1f, r * 0.7f}), {r * 0.4f, r * 0.2f, r * 0.2f}, {210, 160, 90, 255});
    if (b.golden && b.st == BI_FLY && fmodf(t * 3 + b.seed, 1.0f) < 0.15f) Glow(f, {r * 0.4f, r * 0.5f, 0}, {r * 0.3f, r * 0.3f, r * 0.3f}, {255, 255, 220, 255}, 2.0f);   // (it glints)
}

// ---------------------------------------------------------------- the dog
void DrawDog(const World& w, float t, float tallyX) {
    const Dog& d = w.dog; Color fur{150, 106, 60, 255}, dark{90, 60, 34, 255}, nose{30, 24, 22, 255};
    Vector3 at = d.p; bool sleep = w.phase == PH_INTER; float yaw = 0;
    if (sleep) at = Vector3Add(World::Station(6), {-1.0f, 0, 0.7f});
    if (!sleep && d.state == 0 && w.phase != PH_TALLY) return;
    if (w.phase == PH_TALLY) at = {tallyX, 0.0f, 4.0f};
    if (d.state == 2) { const Bird* hold = nullptr; for (const auto& b : w.birds) if (b.id == d.holding) hold = &b; if (hold) { Vector3 v = Vector3Subtract(hold->retrieved ? Vector3{d.p.x, 0, 4} : hold->p, d.p); yaw = atan2f(v.x, v.z); } }
    if (d.state == 3 || w.phase == PH_TALLY) yaw = PI;   // (facing the porch to laugh, or to hold them up)
    Matrix f = MatrixMultiply(MatrixRotateY(yaw), MatrixTranslate(at.x, at.y, at.z));
    bool swim = !sleep && w.phase != PH_TALLY && d.state == 2 && at.z > 6;
    float bob = swim ? -0.35f : 0;
    if (sleep) { Ball(f, {0, 0.25f, 0}, {0.45f, 0.25f, 0.3f}, fur); Ball(f, {0.35f, 0.25f, 0.1f}, {0.18f, 0.15f, 0.16f}, fur); for (int k = 0; k < 3; k++) rt::DrawCubeGlow(MatrixMultiply(MatrixScale(0.08f, 0.08f, 0.02f), MatrixTranslate(at.x + 0.4f + k * 0.15f, 0.6f + k * 0.18f + fmodf(t, 1) * 0.1f, at.z)), WHITE, 0.6f); if (w.players.size()) {} return; }
    // the scruffy hound: body, legs, the head with its ears, the tail
    float run = swim || d.state == 1 || d.state == 2 ? sinf(t * 14) : 0;
    Ball(f, {0, 0.55f + bob, 0}, {0.26f, 0.24f, 0.48f}, fur);
    for (int s = -1; s <= 1; s += 2) for (int e = -1; e <= 1; e += 2) Box(f, {s * 0.16f, 0.25f + bob, e * 0.3f}, {0.08f, 0.45f, 0.08f}, dark, run * s * e * 0.5f);
    float laugh = d.state == 3 ? sinf(t * 18) * 0.15f : 0;
    Matrix hf = MatrixMultiply(MatrixMultiply(MatrixRotateX(-0.2f + laugh), MatrixTranslate(0, 0.9f + bob, 0.45f)), f);
    Ball(hf, {0, 0, 0}, {0.2f, 0.2f, 0.22f}, fur); Box(hf, {0, -0.05f, 0.22f}, {0.14f, 0.12f, 0.2f}, fur); Ball(hf, {0, -0.02f, 0.33f}, {0.05f, 0.04f, 0.04f}, nose);
    for (int s = -1; s <= 1; s += 2) { Box(hf, {s * 0.19f, -0.05f, -0.02f}, {0.05f, 0.25f, 0.13f}, dark, 0, 0, s * 0.3f); Ball(hf, {s * 0.08f, 0.07f, 0.17f}, {0.04f, 0.04f, 0.02f}, WHITE); Ball(hf, {s * 0.08f, 0.07f, 0.19f}, {0.02f, 0.02f, 0.01f}, BLACK); }
    if (d.state == 3) Box(hf, {0, -0.13f, 0.2f}, {0.12f, 0.06f, 0.14f}, {200, 70, 80, 255});   // (the laugh: mouth open)
    Box(f, {0, 0.75f + bob, -0.5f}, {0.05f, 0.05f, 0.3f}, fur, -0.7f + sinf(t * 10) * 0.3f);
    // a dog outfit from the Slop Shop (whoever bought one gets it while their birds are fetched)
    for (const auto& p : w.players) if (p.dogCoat >= 0) {
        const std::string& id = D().cosmetics[p.dogCoat].id;
        if (id == "dog_duck") { Ball(f, {0, 0.66f + bob, 0}, {0.3f, 0.26f, 0.5f}, {250, 220, 70, 255}); Matrix hf2 = MatrixMultiply(MatrixTranslate(0, 0.9f + bob, 0.45f), f); Ball(hf2, {0, 0.14f, 0}, {0.22f, 0.12f, 0.22f}, {250, 220, 70, 255}); Box(hf2, {0, 0.1f, 0.3f}, {0.2f, 0.05f, 0.16f}, {240, 150, 40, 255}); }
        else { Color cc = id == "dog_bandana" ? Color{60, 90, 200, 255} : id == "dog_sweater" ? Color{60, 140, 80, 255} : id == "dog_raincoat" ? Color{250, 210, 50, 255} : id == "dog_sailor" ? Color{240, 240, 250, 255} : Color{200, 60, 60, 255}; if (id == "dog_bandana") Box(f, {0, 0.82f + bob, 0.38f}, {0.3f, 0.08f, 0.12f}, cc); else Ball(f, {0, 0.62f + bob, 0}, {0.28f, 0.2f, 0.4f}, cc); }
        break; }
    // holding up the birds at the tally
    if (w.phase == PH_TALLY) for (int s = -1; s <= 1; s += 2) { Box(f, {s * 0.25f, 1.15f, 0.25f}, {0.07f, 0.45f, 0.07f}, fur); Ball(f, {s * 0.25f, 1.45f, 0.25f}, {0.18f, 0.14f, 0.24f}, {70, 110, 60, 255}); }
}

// ---------------------------------------------------------------- projectiles
void DrawProjs(const World& w, float t) {
    for (const auto& q : w.projs) {
        Matrix f = Facing(q.p, q.v);
        switch (q.kind) {
            case PJ_CRAB: Ball(f, {0, 0, 0}, {0.22f, 0.12f, 0.18f}, {220, 70, 50, 255}); for (int s = -1; s <= 1; s += 2) Ball(f, {s * 0.22f, 0.04f, 0.12f}, {0.08f, 0.05f, 0.1f}, {220, 70, 50, 255}); break;
            case PJ_BEAM: Ball(f, {0, 0, 0}, {1.3f, 1.3f, 2.2f}, {90, 255, 120, 255}, 1.0f); break;
            case PJ_NET: for (int k = 0; k < 6; k++) Box(f, {(k - 2.5f) * 0.25f, 0, 0}, {0.03f, 1.4f, 0.03f}, {200, 190, 150, 255}); for (int k = 0; k < 6; k++) Box(f, {0, (k - 2.5f) * 0.25f, 0}, {1.4f, 0.03f, 0.03f}, {200, 190, 150, 255}); break;
            case PJ_BREAD: Box(f, {0, 0, 0}, {0.18f, 0.12f, 0.26f}, {210, 160, 90, 255}, t * 8); break;
            case PJ_BOOMERANG: Box(f, {0, 0, 0}, {0.5f, 0.05f, 0.12f}, {250, 200, 60, 255}, 0, t * 20); Box(f, {0, 0, 0}, {0.12f, 0.05f, 0.5f}, {250, 200, 60, 255}, 0, t * 20); break;
            case PJ_BUBBLE: Ball(f, {0, 0, 0}, {0.35f, 0.35f, 0.35f}, {250, 190, 230, 255}, 0.4f); break;
            case PJ_FIREWORK: Box(f, {0, 0, 0}, {0.1f, 0.1f, 0.4f}, {230, 50, 60, 255}); Glow(f, {0, 0, -0.3f}, {0.16f, 0.16f, 0.2f}, {255, 200, 80, 255}, 2.0f); break;
            case PJ_PLUNGER: Box(f, {0, 0, -0.3f}, {0.03f, 0.03f, 0.6f}, {160, 120, 70, 255}); rt::DrawStatic(Cone(), Local(f, {0, 0, 0.05f}, {0.1f, 0.1f, 0.1f}, PI / 2), {200, 40, 40, 255}); break;
            case PJ_BANANA: case PJ_BANANA_BIT: Box(f, {0, 0, 0}, {0.07f, 0.07f, q.kind == PJ_BANANA ? 0.4f : 0.25f}, {250, 220, 60, 255}, 0.4f, t * 6); break;
            case PJ_FLARE: Ball(f, {0, 0, 0}, {0.15f, 0.15f, 0.15f}, {255, 90, 60, 255}, 3.0f); break;
        }
    }
}

// ---------------------------------------------------------------- the other shooters
void DrawPlayerFigure(const World& w, const Player& p, float t) {
    const Model* m = rt::LoadAsset(p.id % 3 == 0 ? "shared/crew/crew_diver.glb" : p.id % 3 == 1 ? "shared/crew/crew_bosun.glb" : "shared/crew/crew_angler.glb"); if (!m) return;
    fig::Pose P; fig::Build B; B.build = 1.0f + 0.05f * (p.id % 3);
    P.reach = 0.85f; P.elbow = 0.7f; P.grip = 1; P.crouch = p.crouchK * 0.6f; P.breathe = t * 2 + p.id;
    P.look = 0; P.nod = -p.pitch * 0.6f;
    Vector3 feet = p.pos; float yaw = p.yaw;
    // walking the room
    if (p.room) { P.reach = 0.1f; P.elbow = 0.1f; P.walk = 0.6f; P.walkPh = t * 6 + p.id; }
    std::vector<Matrix> skin = fig::PoseFigure(*m, B, P, t);
    Matrix frame = fig::Frame(feet, yaw - PI / 2);   // (fig::Frame faces (cos a, 0, -sin a); our yaw faces (sin, 0, cos))
    static const Color TOPS[6] = {{200, 80, 60, 255}, {60, 120, 200, 255}, {80, 160, 90, 255}, {220, 170, 60, 255}, {150, 90, 190, 255}, {80, 170, 180, 255}};
    std::vector<rt::Recolor> rc = {{"top", TOPS[p.stall % 6]}, {"trousers", {70, 66, 60, 255}}, {"hat", TOPS[p.stall % 6]}, {"skin", {220, 176, 140, 255}}};
    rt::DrawPbrSkinned(*m, frame, skin, rc, 0.35f, WHITE);
    Vector3 fist = fig::FistWorld(*m, skin, frame, 1);
    if (!p.room && p.G().Has() && p.gunDropT <= 0) {
        Vector3 look = p.Look(); Vector3 r = Vector3Normalize(Vector3CrossProduct({0, 1, 0}, look)); Vector3 u = Vector3CrossProduct(look, r);
        Matrix g = Frame(fist, r, u, look);
        DrawToyGun(p.G().def, g, p.paint, p.G().spin * 20 + t * 0, 1.0f);
    }
    // the hat (knocked off: on the boards)
    if (p.hat >= 0) {
        const std::string& id = D().cosmetics[p.hat].id;
        Vector3 head = Vector3Add(p.pos, {0, 1.82f - p.crouchK * 0.4f, 0}); if (p.hatOff) head = {p.pos.x + 0.4f, 0.05f, p.pos.z + 0.3f};
        Color c = id == "hat_cowboy" ? Color{140, 90, 50, 255} : id == "hat_duck" ? Color{240, 200, 60, 255} : id == "hat_bucket" ? Color{90, 110, 80, 255} : id == "hat_top" ? Color{30, 30, 36, 255} : id == "hat_beanie" ? Color{220, 60, 60, 255} : id == "hat_fishing" ? Color{180, 170, 120, 255} : Color{240, 200, 60, 255};
        if (id == "hat_zappa") DrawToyGun(0, MatrixMultiply(MatrixMultiply(MatrixScale(1.4f, 1.4f, 1.4f), MatrixRotateY(p.yaw)), MatrixTranslate(head.x, head.y + 0.1f, head.z)), 0);
        else {
            float brim = id == "hat_cowboy" ? 0.32f : id == "hat_top" ? 0.2f : id == "hat_beanie" ? 0.14f : 0.22f, crown = id == "hat_top" ? 0.3f : id == "hat_crown" ? 0.14f : 0.12f;
            rt::DrawStatic(Cyl(), MatrixMultiply(MatrixScale(brim, 0.03f, brim), MatrixTranslate(head.x, head.y, head.z)), c);
            rt::DrawStatic(Cyl(), MatrixMultiply(MatrixScale(0.14f, crown, 0.14f), MatrixTranslate(head.x, head.y, head.z)), c);
            if (id == "hat_duck") rt::DrawWorldCube({head.x + sinf(p.yaw) * 0.2f, head.y + 0.02f, head.z + cosf(p.yaw) * 0.2f}, {0.18f, 0.03f, 0.18f}, {240, 150, 40, 255});
            if (id == "hat_beanie") { float a = t * 20; rt::DrawCubeM(MatrixMultiply(MatrixMultiply(MatrixScale(0.3f, 0.01f, 0.04f), MatrixRotateY(a)), MatrixTranslate(head.x, head.y + 0.16f, head.z)), {60, 140, 220, 255}); }
            if (id == "hat_crown") for (int k = 0; k < 5; k++) { float a = k * 1.2566f; rt::DrawWorldCube({head.x + cosf(a) * 0.12f, head.y + 0.17f, head.z + sinf(a) * 0.12f}, {0.04f, 0.07f, 0.04f}, c); }
        }
    }
    // bees round the head
    if (p.beesT > 0) for (int k = 0; k < 6; k++) { float a = t * (5 + k) + k; rt::DrawStatic(Sphere(), MatrixMultiply(MatrixScale(0.03f, 0.03f, 0.03f), MatrixTranslate(p.pos.x + cosf(a) * 0.35f, 1.8f + sinf(a * 1.3f) * 0.2f, p.pos.z + sinf(a) * 0.35f)), k % 2 ? Color{250, 210, 40, 255} : Color{30, 30, 30, 255}); }
    // a bagpiper beside the stall
    if (p.bagpipe) { Vector3 bp = Vector3Add(w.StallPos(p.stall), {1.0f, 0, -0.6f}); rt::DrawWorldCube({bp.x, 0.6f, bp.z}, {0.4f, 1.2f, 0.3f}, {40, 90, 60, 255}); rt::DrawWorldCube({bp.x, 1.35f, bp.z}, {0.3f, 0.3f, 0.3f}, {220, 176, 140, 255}); rt::DrawStatic(Sphere(), MatrixMultiply(MatrixScale(0.25f, 0.2f, 0.2f), MatrixTranslate(bp.x + 0.2f, 1.1f, bp.z + 0.15f)), {150, 40, 50, 255}); for (int k = 0; k < 3; k++) rt::DrawWorldCube({bp.x + 0.25f + k * 0.08f, 1.5f + k * 0.05f, bp.z}, {0.03f, 0.6f, 0.03f}, {40, 30, 26, 255}); }
}

}  // namespace fpart
