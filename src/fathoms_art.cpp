// Fathoms' code art (see fathoms_art.h). Everything is built once, lazily, from rt::MeshBuilder primitives: chunky,
// rounded low-poly figures in the Darkest-Dungeon-with-depth spirit the rest of the arcade uses, each faction in its
// own palette (Nautilus navy and brass, Islanders' tan and palm, the Brood's red shell, Merfolk teal, Atlantean stone
// and bronze, Clockwork iron and copper) with the owner's colour on sails, flags and sashes.
#include "fathoms_art.h"
#include "redtide_render.h"
#include "raymath.h"
#include <cmath>
#include <map>

namespace fa {
namespace {
using MB = rt::MeshBuilder;
Color Mix(Color a, Color b, float k) { k = std::clamp(k, 0.0f, 1.0f); return {(unsigned char)(a.r + (b.r - a.r) * k), (unsigned char)(a.g + (b.g - a.g) * k), (unsigned char)(a.b + (b.b - a.b) * k), 255}; }
Color Sh(Color c, float k) { return {(unsigned char)std::clamp(c.r * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.g * k, 0.0f, 255.0f), (unsigned char)std::clamp(c.b * k, 0.0f, 255.0f), 255}; }
void Box(MB& mb, Vector3 lo, Vector3 hi, Color c) {
    Vector3 v[8]; for (int i = 0; i < 8; i++) v[i] = {(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z};
    Color top = Sh(c, 1.08f), side = c, side2 = Sh(c, 0.88f), bot = Sh(c, 0.7f);
    mb.Quad(v[2], v[6], v[7], v[3], top); mb.Quad(v[0], v[1], v[5], v[4], bot);
    mb.Quad(v[0], v[4], v[6], v[2], side2); mb.Quad(v[1], v[3], v[7], v[5], side2);
    mb.Quad(v[0], v[2], v[3], v[1], side); mb.Quad(v[4], v[5], v[7], v[6], side);
}
void BoxC(MB& mb, Vector3 c, Vector3 h, Color col) { Box(mb, Vector3Subtract(c, h), Vector3Add(c, h), col); }
void Sphere(MB& mb, Vector3 c, float r, Color col, int seg = 8, int rings = 6, float sy = 1) {
    for (int i = 0; i < rings; i++) for (int j = 0; j < seg; j++) {
        float a0 = PI * i / rings - PI / 2, a1 = PI * (i + 1) / rings - PI / 2, b0 = 2 * PI * j / seg, b1 = 2 * PI * (j + 1) / seg;
        auto P = [&](float a, float b) { return Vector3{c.x + r * cosf(a) * cosf(b), c.y + r * sy * sinf(a), c.z + r * cosf(a) * sinf(b)}; };
        Color k = Sh(col, 0.8f + 0.3f * (sinf(a0) * 0.5f + 0.5f));
        mb.Tri(P(a0, b0), P(a1, b1), P(a1, b0), k); mb.Tri(P(a0, b0), P(a0, b1), P(a1, b1), k);
    }
}
void Cyl(MB& mb, Vector3 a, Vector3 b, float r, Color c, int segs = 8) { mb.Tube({a, b}, r, r, segs, c, c, 0); Vector3 d = Vector3Normalize(Vector3Subtract(b, a)); mb.Cone(b, Vector3Add(b, Vector3Scale(d, 0.001f)), r, segs, Sh(c, 1.08f)); mb.Cone(a, Vector3Subtract(a, Vector3Scale(d, 0.001f)), r, segs, Sh(c, 0.8f)); }
void Taper(MB& mb, Vector3 a, Vector3 b, float r0, float r1, Color c, int segs = 8) { mb.Tube({a, b}, r0, r1, segs, c, c, 0); }
void Two(MB& mb, Vector3 a, Vector3 b, Vector3 c, Color col) { mb.Tri(a, b, c, col); mb.Tri(a, c, b, col); }
void Sail(MB& mb, Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color col) { Two(mb, a, b, c, col); Two(mb, a, c, d, Sh(col, 0.94f)); }
// a hull lofted along x: the beam swells amidships, the bow is sharp, the stern square-ish
void Hull(MB& mb, float len, float beam, float depth, float freeboard, Color hull, Color deck, Color stripe) {
    const int N = 10; auto W = [&](float u) { float s = u < 0.5f ? sqrtf(std::max(0.0f, 1 - powf((0.5f - u) / 0.5f, 2.2f))) : 0.45f + 0.55f * sqrtf(std::max(0.0f, 1 - powf((u - 0.5f) / 0.5f, 3))); return beam * 0.5f * std::max(0.04f, s); };
    // u 0 = stern, 1 = bow; x runs -len/2 .. len/2
    for (int i = 0; i < N; i++) {
        float u0 = (float)i / N, u1 = (float)(i + 1) / N, x0 = -len / 2 + u0 * len, x1 = -len / 2 + u1 * len; float w0 = W(u0), w1 = W(u1);
        float k0 = -depth * (0.6f + 0.4f * sinf(u0 * PI)), k1 = -depth * (0.6f + 0.4f * sinf(u1 * PI));
        float f0 = freeboard + 0.12f * u0 * u0, f1 = freeboard + 0.12f * u1 * u1;
        for (int s = -1; s <= 1; s += 2) {
            Vector3 a{x0, f0, s * w0}, b{x1, f1, s * w1}, c{x1, k1 * 0.5f, s * w1 * 0.85f}, d{x0, k0 * 0.5f, s * w0 * 0.85f}, e{x1, k1, 0}, f{x0, k0, 0};
            Color hs = s > 0 ? hull : Sh(hull, 0.9f);
            Two(mb, a, b, c, hs); Two(mb, a, c, d, hs); Two(mb, d, c, e, Sh(hs, 0.8f)); Two(mb, d, e, f, Sh(hs, 0.8f));
            Two(mb, Vector3Lerp(a, d, 0.1f), Vector3Lerp(b, c, 0.1f), Vector3Lerp(b, c, 0.2f), stripe); Two(mb, Vector3Lerp(a, d, 0.1f), Vector3Lerp(b, c, 0.2f), Vector3Lerp(a, d, 0.2f), stripe);
        }
        Two(mb, {x0, f0 - 0.02f, -w0 * 0.92f}, {x1, f1 - 0.02f, -w1 * 0.92f}, {x1, f1 - 0.02f, w1 * 0.92f}, deck); Two(mb, {x0, f0 - 0.02f, -w0 * 0.92f}, {x1, f1 - 0.02f, w1 * 0.92f}, {x0, f0 - 0.02f, w0 * 0.92f}, deck);
    }
    float ws = W(0); Two(mb, {-len / 2, freeboard, -ws}, {-len / 2, freeboard, ws}, {-len / 2, -depth * 0.6f, 0}, Sh(hull, 0.85f));   // (the transom)
}
void Mast(MB& mb, Vector3 base, float h, Color wood, Color sail, float sw, bool square = true) {
    Cyl(mb, base, Vector3Add(base, {0, h, 0}), 0.035f, wood, 6);
    if (square) { for (int k = 0; k < 2; k++) { float y0 = base.y + h * (0.32f + k * 0.34f), y1 = y0 + h * 0.3f, w = sw * (1 - k * 0.2f); Cyl(mb, {base.x, y1, base.z - w}, {base.x, y1, base.z + w}, 0.02f, wood, 4); Sail(mb, {base.x + 0.02f, y1, base.z - w}, {base.x + 0.02f, y1, base.z + w}, {base.x + 0.08f, y0, base.z + w * 0.95f}, {base.x + 0.08f, y0, base.z - w * 0.95f}, sail); } }
    else Sail(mb, {base.x + 0.02f, base.y + h * 0.95f, base.z}, {base.x + 0.02f, base.y + h * 0.18f, base.z}, {base.x - sw * 1.6f, base.y + h * 0.18f, base.z}, {base.x - sw * 0.2f, base.y + h * 0.8f, base.z}, sail);
}
void Flag(MB& mb, Vector3 top, Color c, float s = 0.25f) { Cyl(mb, Vector3Add(top, {0, -s * 3, 0}), top, 0.015f, {80, 60, 40, 255}, 4); Sail(mb, Vector3Add(top, {0, -0.02f, 0}), Vector3Add(top, {-s * 1.3f, -0.05f, 0}), Vector3Add(top, {-s * 1.3f, -s * 0.8f, 0}), Vector3Add(top, {0, -s * 0.75f, 0}), c); }
void Funnel(MB& mb, Vector3 base, float h, float r, Color c) { Taper(mb, base, Vector3Add(base, {0, h, 0}), r, r * 0.9f, c); Cyl(mb, Vector3Add(base, {0, h - 0.04f, 0}), Vector3Add(base, {0, h + 0.02f, 0}), r * 1.12f, Sh(c, 0.6f)); }
void Cannon(MB& mb, Vector3 at, Vector3 dir, float len, Color c) { Vector3 tip = Vector3Add(at, Vector3Scale(dir, len)); Taper(mb, at, tip, 0.06f, 0.045f, c, 6); Sphere(mb, at, 0.07f, c, 6, 4); }

// ---------------------------------------------------------------- people
struct Look { Color skin, coat, trim, leg, team; int body = 0; };   // body: 0 human, 1 shell (Brood), 2 tail (Merfolk), 3 robot (Clockwork), 4 robed (Atlantean)
Look LookOf(int faction, Color team) {
    switch (faction) {
        case 0: return {{232, 190, 160, 255}, {36, 52, 92, 255}, {214, 166, 82, 255}, {40, 40, 52, 255}, team, 0};
        case 1: return {{176, 120, 80, 255}, {200, 170, 110, 255}, {84, 140, 70, 255}, {176, 120, 80, 255}, team, 0};
        case 2: return {{214, 86, 60, 255}, {190, 60, 44, 255}, {240, 180, 120, 255}, {170, 50, 40, 255}, team, 1};
        case 3: return {{96, 176, 168, 255}, {50, 120, 110, 255}, {200, 220, 160, 255}, {40, 110, 120, 255}, team, 2};
        case 4: return {{150, 170, 176, 255}, {120, 118, 100, 255}, {196, 158, 70, 255}, {90, 96, 100, 255}, team, 4};
        case 5: return {{150, 150, 158, 255}, {110, 96, 84, 255}, {200, 120, 70, 255}, {70, 70, 76, 255}, team, 3};
        default: return {{200, 170, 140, 255}, {120, 100, 80, 255}, {160, 140, 100, 255}, {80, 70, 60, 255}, team, 0};
    }
}
enum Hat { H_NONE, H_CAP, H_BELL, H_TRICORN, H_HOOD, H_HELM, H_CROWN, H_FEATHER, H_GOGGLES };
enum Weap { W_NONE, W_RIFLE, W_HARPOON, W_LANCE, W_CUTLASS, W_STAFF, W_SHOVEL, W_SPEAR, W_PISTOL, W_BAG, W_CLAW, W_TRIDENT, W_HAMMER };
// a chunky figure about 1.1 units tall at s = 1, facing +x
void Person(MB& mb, Vector3 at, float s, const Look& L, int hat, int weap, bool sash = true) {
    auto P = [&](float x, float y, float z) { return Vector3{at.x + x * s, at.y + y * s, at.z + z * s}; };
    if (L.body == 2) {   // a merfolk tail, curled forward
        mb.Tube({P(0, 0.42f, 0), P(-0.08f, 0.22f, 0), P(-0.02f, 0.06f, 0), P(0.16f, 0.03f, 0)}, 0.15f * s, 0.05f * s, 7, L.leg, Sh(L.leg, 0.8f), 0);
        Two(mb, P(0.16f, 0.03f, 0), P(0.3f, 0.12f, 0.12f), P(0.3f, 0.0f, -0.12f), L.trim);
    } else if (L.body == 4) {   // a robe to the ground
        Taper(mb, P(0, 0.02f, 0), P(0, 0.5f, 0), 0.2f * s, 0.15f * s, L.coat, 8);
    } else {
        Color lc = L.body == 3 ? Sh(L.leg, 1.1f) : L.leg;
        Cyl(mb, P(0, 0.48f, 0.08f), P(0.02f, 0.05f, 0.09f), 0.065f * s, lc, 6); Cyl(mb, P(0, 0.48f, -0.08f), P(-0.02f, 0.05f, -0.09f), 0.065f * s, lc, 6);
        BoxC(mb, P(0.05f, 0.03f, 0.09f), {0.09f * s, 0.035f * s, 0.06f * s}, Sh(lc, 0.6f)); BoxC(mb, P(0.03f, 0.03f, -0.09f), {0.09f * s, 0.035f * s, 0.06f * s}, Sh(lc, 0.6f));
    }
    // the body: a rounded barrel (a shell, or an iron box for the Clockwork)
    if (L.body == 3) { BoxC(mb, P(0, 0.68f, 0), {0.13f * s, 0.2f * s, 0.17f * s}, L.coat); Sphere(mb, P(0.1f, 0.72f, 0), 0.05f * s, {255, 170, 60, 255}, 6, 4); }
    else Sphere(mb, P(0, 0.68f, 0), 0.19f * s, L.coat, 9, 6, 1.3f);
    if (L.body == 1) for (int k = 0; k < 3; k++) BoxC(mb, P(0.02f, 0.56f + k * 0.12f, 0), {0.17f * s, 0.03f * s, 0.2f * s}, Sh(L.trim, 0.9f));   // (shell bands)
    if (sash) Two(mb, P(0.16f, 0.82f, -0.15f), P(0.17f, 0.52f, 0.16f), P(0.17f, 0.6f, 0.16f), L.team), Two(mb, P(0.16f, 0.82f, -0.15f), P(0.17f, 0.6f, 0.16f), P(0.17f, 0.9f, -0.12f), L.team);
    // arms
    Color ac = L.body == 1 ? L.trim : L.body == 3 ? Sh(L.coat, 1.15f) : L.coat;
    Cyl(mb, P(0, 0.82f, 0.2f), P(0.12f, 0.58f, 0.22f), 0.055f * s, ac, 6); Cyl(mb, P(0, 0.82f, -0.2f), P(0.14f, 0.6f, -0.2f), 0.055f * s, ac, 6);
    if (L.body == 1) { Sphere(mb, P(0.18f, 0.56f, 0.24f), 0.08f * s, L.trim, 6, 4); Sphere(mb, P(0.2f, 0.58f, -0.22f), 0.08f * s, L.trim, 6, 4); }   // (claws)
    else { Sphere(mb, P(0.14f, 0.56f, 0.22f), 0.045f * s, L.skin, 6, 4); Sphere(mb, P(0.16f, 0.58f, -0.2f), 0.045f * s, L.skin, 6, 4); }
    // the head
    Vector3 h = P(0.02f, 1.0f, 0);
    if (L.body == 3) { BoxC(mb, h, {0.1f * s, 0.09f * s, 0.1f * s}, Sh(L.coat, 1.1f)); Sphere(mb, P(0.12f, 1.01f, 0.04f), 0.025f * s, {255, 200, 80, 255}, 5, 3); Sphere(mb, P(0.12f, 1.01f, -0.04f), 0.025f * s, {255, 200, 80, 255}, 5, 3); }
    else if (L.body == 1) { Sphere(mb, h, 0.12f * s, L.skin, 8, 5, 0.85f); Cyl(mb, P(0.08f, 1.08f, 0.05f), P(0.25f, 1.25f, 0.12f), 0.008f * s, L.trim, 3); Cyl(mb, P(0.08f, 1.08f, -0.05f), P(0.25f, 1.25f, -0.12f), 0.008f * s, L.trim, 3); Sphere(mb, P(0.1f, 1.05f, 0.06f), 0.025f * s, {20, 20, 20, 255}, 4, 3); Sphere(mb, P(0.1f, 1.05f, -0.06f), 0.025f * s, {20, 20, 20, 255}, 4, 3); }
    else { Sphere(mb, h, 0.12f * s, L.skin, 8, 5); Sphere(mb, P(0.12f, 1.02f, 0.045f), 0.016f * s, {25, 25, 30, 255}, 4, 3); Sphere(mb, P(0.12f, 1.02f, -0.045f), 0.016f * s, {25, 25, 30, 255}, 4, 3); }
    switch (hat) {
        case H_CAP: Cyl(mb, P(0.01f, 1.08f, 0), P(0.01f, 1.15f, 0), 0.12f * s, L.coat, 8); BoxC(mb, P(0.11f, 1.08f, 0), {0.06f * s, 0.01f * s, 0.08f * s}, Sh(L.coat, 0.7f)); break;
        case H_BELL: Sphere(mb, P(0.02f, 1.02f, 0), 0.17f * s, {196, 150, 70, 255}, 9, 6); Cyl(mb, P(0.14f, 1.02f, 0), P(0.19f, 1.02f, 0), 0.075f * s, {150, 210, 220, 255}, 8); break;
        case H_TRICORN: Cyl(mb, P(0.01f, 1.08f, 0), P(0.01f, 1.12f, 0), 0.19f * s, {30, 26, 26, 255}, 3); Cyl(mb, P(0.01f, 1.1f, 0), P(0.01f, 1.2f, 0), 0.1f * s, {30, 26, 26, 255}, 8); break;
        case H_HOOD: Sphere(mb, P(-0.01f, 1.03f, 0), 0.15f * s, Sh(L.coat, 0.8f), 8, 5); break;
        case H_HELM: Sphere(mb, P(0.01f, 1.05f, 0), 0.14f * s, L.trim, 8, 5, 0.8f); Cyl(mb, P(0, 1.16f, 0), P(-0.05f, 1.3f, 0), 0.02f * s, L.team, 4); break;
        case H_CROWN: for (int k = 0; k < 5; k++) { float a = k * 1.256f; mb.Cone(P(0.01f + cosf(a) * 0.08f, 1.1f, sinf(a) * 0.08f), P(0.01f + cosf(a) * 0.09f, 1.24f, sinf(a) * 0.09f), 0.03f * s, 4, {240, 200, 60, 255}); } break;
        case H_FEATHER: for (int k = -1; k <= 1; k++) Two(mb, P(-0.02f, 1.08f, k * 0.05f), P(-0.06f, 1.38f, k * 0.12f), P(-0.1f, 1.1f, k * 0.05f), k ? L.team : L.trim); break;
        case H_GOGGLES: Cyl(mb, P(0.1f, 1.05f, 0.05f), P(0.15f, 1.05f, 0.05f), 0.035f * s, {200, 160, 60, 255}, 6); Cyl(mb, P(0.1f, 1.05f, -0.05f), P(0.15f, 1.05f, -0.05f), 0.035f * s, {200, 160, 60, 255}, 6); break;
        default: break;
    }
    // what they carry, in the right hand (+z side)
    Vector3 hand = P(0.14f, 0.58f, 0.24f); Color wood{120, 86, 52, 255}, steel{170, 174, 180, 255};
    switch (weap) {
        case W_RIFLE: Cyl(mb, Vector3Add(hand, {-0.15f * s, 0.05f * s, 0}), Vector3Add(hand, {0.42f * s, 0.12f * s, 0}), 0.025f * s, wood, 5); Cyl(mb, Vector3Add(hand, {0.1f * s, 0.11f * s, 0}), Vector3Add(hand, {0.5f * s, 0.14f * s, 0}), 0.015f * s, steel, 4); break;
        case W_HARPOON: Cyl(mb, Vector3Add(hand, {-0.2f * s, -0.15f * s, 0}), Vector3Add(hand, {0.35f * s, 0.55f * s, 0}), 0.018f * s, wood, 4); mb.Cone(Vector3Add(hand, {0.35f * s, 0.55f * s, 0}), Vector3Add(hand, {0.42f * s, 0.7f * s, 0}), 0.04f * s, 4, steel); break;
        case W_LANCE: Cyl(mb, Vector3Add(hand, {-0.3f * s, 0, 0}), Vector3Add(hand, {0.9f * s, 0.1f * s, 0}), 0.02f * s, wood, 4); mb.Cone(Vector3Add(hand, {0.9f * s, 0.1f * s, 0}), Vector3Add(hand, {1.05f * s, 0.12f * s, 0}), 0.04f * s, 4, steel); Two(mb, Vector3Add(hand, {0.7f * s, 0.1f * s, 0}), Vector3Add(hand, {0.55f * s, 0.18f * s, 0}), Vector3Add(hand, {0.6f * s, 0.04f * s, 0}), L.team); break;
        case W_CUTLASS: Cyl(mb, hand, Vector3Add(hand, {0.2f * s, 0.32f * s, 0}), 0.016f * s, steel, 4); break;
        case W_STAFF: Cyl(mb, Vector3Add(hand, {0, -0.55f * s, 0}), Vector3Add(hand, {0.02f * s, 0.55f * s, 0}), 0.02f * s, wood, 4); Sphere(mb, Vector3Add(hand, {0.02f * s, 0.6f * s, 0}), 0.06f * s, L.trim, 6, 4); break;
        case W_SHOVEL: Cyl(mb, Vector3Add(hand, {-0.1f * s, -0.4f * s, 0}), Vector3Add(hand, {0.05f * s, 0.25f * s, 0}), 0.018f * s, wood, 4); BoxC(mb, Vector3Add(hand, {-0.12f * s, -0.45f * s, 0}), {0.03f * s, 0.07f * s, 0.06f * s}, steel); break;
        case W_SPEAR: Cyl(mb, Vector3Add(hand, {-0.05f * s, -0.4f * s, 0}), Vector3Add(hand, {0.15f * s, 0.7f * s, 0}), 0.018f * s, wood, 4); mb.Cone(Vector3Add(hand, {0.15f * s, 0.7f * s, 0}), Vector3Add(hand, {0.18f * s, 0.85f * s, 0}), 0.035f * s, 4, {200, 190, 170, 255}); break;
        case W_PISTOL: BoxC(mb, Vector3Add(hand, {0.08f * s, 0.03f * s, 0}), {0.08f * s, 0.025f * s, 0.02f * s}, {60, 50, 46, 255}); break;
        case W_BAG: Sphere(mb, P(-0.18f, 0.7f, 0), 0.13f * s, {196, 180, 150, 255}, 6, 4); Two(mb, P(0.15f, 0.85f, 0.03f), P(0.15f, 0.7f, 0.03f), P(0.21f, 0.77f, 0.03f), {220, 50, 50, 255}); break;
        case W_TRIDENT: Cyl(mb, Vector3Add(hand, {0, -0.5f * s, 0}), Vector3Add(hand, {0.05f * s, 0.75f * s, 0}), 0.02f * s, {220, 190, 90, 255}, 4); for (int k = -1; k <= 1; k++) mb.Cone(Vector3Add(hand, {0.05f * s, 0.72f * s, k * 0.06f * s}), Vector3Add(hand, {0.06f * s, 0.92f * s, k * 0.08f * s}), 0.02f * s, 4, {230, 200, 100, 255}); break;
        case W_HAMMER: Cyl(mb, Vector3Add(hand, {0, -0.1f * s, 0}), Vector3Add(hand, {0.1f * s, 0.45f * s, 0}), 0.02f * s, wood, 4); BoxC(mb, Vector3Add(hand, {0.1f * s, 0.48f * s, 0}), {0.06f * s, 0.06f * s, 0.12f * s}, steel); break;
        default: break;
    }
}
// a mount: a horse for most, a sea-eel for the Merfolk, a crab for the Brood, a brass walker for the Clockwork
void Mount(MB& mb, Vector3 at, float s, int faction, Color team) {
    auto P = [&](float x, float y, float z) { return Vector3{at.x + x * s, at.y + y * s, at.z + z * s}; };
    if (faction == 3) { mb.Tube({P(-0.7f, 0.4f, 0), P(-0.3f, 0.5f, 0.1f), P(0.2f, 0.45f, -0.08f), P(0.6f, 0.6f, 0)}, 0.18f * s, 0.12f * s, 8, {40, 100, 80, 255}, {200, 220, 120, 255}, 0); Sphere(mb, P(0.65f, 0.62f, 0), 0.15f * s, {50, 120, 90, 255}, 7, 5); return; }
    if (faction == 2) { Sphere(mb, P(0, 0.45f, 0), 0.38f * s, {200, 70, 50, 255}, 9, 6, 0.55f); for (int k = -1; k <= 1; k += 2) for (int j = 0; j < 3; j++) Cyl(mb, P(-0.2f + j * 0.2f, 0.4f, k * 0.3f), P(-0.25f + j * 0.25f, 0.02f, k * 0.55f), 0.04f * s, {180, 60, 40, 255}, 4); return; }
    Color body = faction == 5 ? Color{150, 140, 120, 255} : faction == 1 ? Color{150, 110, 70, 255} : Color{110, 80, 60, 255};
    mb.Tube({P(-0.42f, 0.62f, 0), P(0, 0.66f, 0), P(0.4f, 0.66f, 0)}, 0.21f * s, 0.19f * s, 9, body, Sh(body, 0.85f), 0); Sphere(mb, P(-0.42f, 0.62f, 0), 0.21f * s, body, 7, 5); Sphere(mb, P(0.4f, 0.66f, 0), 0.19f * s, body, 7, 5);
    for (int k = -1; k <= 1; k += 2) { Cyl(mb, P(0.3f, 0.55f, k * 0.12f), P(0.32f, 0.02f, k * 0.12f), 0.05f * s, Sh(body, 0.85f), 5); Cyl(mb, P(-0.3f, 0.55f, k * 0.12f), P(-0.32f, 0.02f, k * 0.12f), 0.05f * s, Sh(body, 0.85f), 5); }
    Cyl(mb, P(0.35f, 0.7f, 0), P(0.6f, 0.95f, 0), 0.09f * s, body, 6); Sphere(mb, P(0.66f, 0.95f, 0), 0.12f * s, body, 7, 5, 0.8f);
    Two(mb, P(-0.2f, 0.82f, -0.2f), P(0.2f, 0.82f, -0.2f), P(0.0f, 0.62f, -0.24f), team); Two(mb, P(-0.2f, 0.82f, 0.2f), P(0.0f, 0.62f, 0.24f), P(0.2f, 0.82f, 0.2f), team);
}
void Barrel(MB& mb, Vector3 a, Vector3 b, float r, Color c) { mb.Tube({a, Vector3Lerp(a, b, 0.5f), b}, r * 0.85f, r * 0.85f, 9, c, Sh(c, 0.9f), 0); }

// ---------------------------------------------------------------- units
void BuildUnit(MB& mb, const UnitDef& d, int faction, Color team) {
    Look L = LookOf(faction, team); const std::string& k = d.key; Color wood{124, 88, 54, 255}, sail{236, 228, 206, 255}, iron{70, 72, 80, 255}, brass{200, 156, 70, 255};
    Color hullC = faction == 0 ? Color{40, 50, 80, 255} : faction == 1 ? Color{140, 100, 60, 255} : faction == 2 ? Color{150, 60, 50, 255} : faction == 3 ? Color{50, 110, 110, 255} : faction == 4 ? Color{110, 110, 104, 255} : faction == 5 ? Color{80, 80, 88, 255} : Color{110, 80, 50, 255};
    float s = 1.0f;
    if (k == "worker") Person(mb, {0, 0, 0}, 0.85f, L, faction == 1 ? H_NONE : faction == 5 ? H_GOGGLES : H_CAP, W_SHOVEL);
    else if (k == "rifleman" || k == "diving_marine") Person(mb, {0, 0, 0}, s, L, k == "diving_marine" ? H_BELL : faction == 0 ? H_CAP : faction == 4 ? H_HELM : H_NONE, W_RIFLE);
    else if (k == "harpooner") Person(mb, {0, 0, 0}, s, L, faction == 2 ? H_HELM : H_NONE, W_HARPOON);
    else if (k == "scout_rider" || k == "lancer" || k == "eel_rider") { Mount(mb, {0, 0, 0}, 1.0f, k == "eel_rider" ? 3 : faction, team); Person(mb, {-0.05f, 0.62f, 0}, 0.85f, L, k == "lancer" ? H_HELM : H_CAP, k == "scout_rider" ? W_CUTLASS : W_LANCE); }
    else if (k == "bell_guard") Person(mb, {0, 0, 0}, 1.15f, L, H_BELL, W_HAMMER);
    else if (k == "medic") Person(mb, {0, 0, 0}, s, L, H_CAP, W_BAG);
    else if (k == "mortar") { Cyl(mb, {-0.25f, 0.18f, -0.25f}, {-0.25f, 0.18f, 0.25f}, 0.18f, wood, 8); Taper(mb, {0, 0.25f, 0}, {0.35f, 0.75f, 0}, 0.18f, 0.14f, iron, 9); Person(mb, {-0.5f, 0, 0.35f}, 0.8f, L, H_CAP, W_NONE); }
    else if (k == "shaman") Person(mb, {0, 0, 0}, s, L, H_FEATHER, W_STAFF);
    else if (k == "dog_handler") { Person(mb, {0, 0, 0}, s, L, H_NONE, W_SPEAR); Sphere(mb, {0.4f, 0.25f, 0.35f}, 0.18f, {90, 70, 50, 255}, 7, 5, 0.7f); Sphere(mb, {0.6f, 0.32f, 0.35f}, 0.1f, {80, 60, 44, 255}, 6, 4); }
    else if (k == "spitter_shrimp") { mb.Tube({{-0.4f, 0.25f, 0}, {-0.1f, 0.4f, 0}, {0.2f, 0.45f, 0}, {0.4f, 0.35f, 0}}, 0.1f, 0.2f, 8, {230, 120, 90, 255}, {250, 170, 130, 255}, 0); for (int j = 0; j < 4; j++) for (int q = -1; q <= 1; q += 2) Cyl(mb, {-0.1f + j * 0.12f, 0.3f, q * 0.1f}, {-0.1f + j * 0.12f, 0.02f, q * 0.22f}, 0.02f, {200, 90, 70, 255}, 4); Cyl(mb, {0.4f, 0.4f, 0.05f}, {0.7f, 0.65f, 0.15f}, 0.008f, {240, 150, 110, 255}, 3); }
    else if (k == "lobster_knight") { Person(mb, {0, 0, 0}, 1.2f, L, H_HELM, W_CUTLASS); Sphere(mb, {0.3f, 0.75f, -0.3f}, 0.16f, {200, 60, 40, 255}, 7, 5, 0.6f); }
    else if (k == "siren") Person(mb, {0, 0, 0}, s, L, H_CROWN, W_NONE);
    else if (k == "deep_priest") Person(mb, {0, 0, 0}, s, L, H_HOOD, W_STAFF);
    else if (k == "legionnaire") Person(mb, {0, 0, 0}, 1.2f, L, H_HELM, W_SPEAR);
    else if (k == "rivet_golem") { Look g = LookOf(5, team); Person(mb, {0, 0, 0}, 1.5f, g, H_NONE, W_HAMMER); }
    else if (k == "tesla_walker") { for (int q = 0; q < 3; q++) { float a = q * 2.09f; Cyl(mb, {cosf(a) * 0.12f, 0.6f, sinf(a) * 0.12f}, {cosf(a) * 0.45f, 0, sinf(a) * 0.45f}, 0.04f, iron, 5); } Sphere(mb, {0, 0.75f, 0}, 0.25f, {140, 110, 80, 255}, 8, 6); Cyl(mb, {0, 0.95f, 0}, {0, 1.35f, 0}, 0.04f, brass, 5); Sphere(mb, {0, 1.4f, 0}, 0.1f, {140, 220, 255, 255}, 6, 4); }
    else if (k == "tribal_warrior" || k == "pirate" || k == "tribal_thrower" || k == "drowned_sailor") {
        Look n = k == "pirate" ? Look{{220, 180, 150, 255}, {150, 40, 40, 255}, {40, 30, 30, 255}, {60, 50, 40, 255}, {30, 30, 30, 255}, 0} : k == "drowned_sailor" ? Look{{140, 170, 150, 255}, {60, 80, 76, 255}, {100, 120, 110, 255}, {50, 60, 60, 255}, {40, 60, 60, 255}, 0} : Look{{150, 100, 66, 255}, {180, 150, 90, 255}, {200, 70, 50, 255}, {150, 100, 66, 255}, {200, 70, 50, 255}, 0};
        if (IsPlayer(0) && k == "tribal_warrior" && faction >= 0) n.team = team;
        Person(mb, {0, 0, 0}, s, n, k == "pirate" ? H_TRICORN : k == "drowned_sailor" ? H_CAP : H_FEATHER, k == "pirate" ? W_CUTLASS : W_SPEAR, k != "drowned_sailor");
    }
    else if (k == "chieftain" || k == "witch_doctor") { Look n{{150, 100, 66, 255}, {200, 160, 80, 255}, {220, 60, 40, 255}, {150, 100, 66, 255}, {240, 200, 60, 255}, 0}; Person(mb, {0, 0, 0}, 1.4f, n, k == "chieftain" ? H_FEATHER : H_HOOD, k == "chieftain" ? W_HAMMER : W_STAFF); }
    else if (k == "blackbeard") { Look n{{220, 180, 150, 255}, {30, 26, 30, 255}, {200, 160, 60, 255}, {40, 34, 30, 255}, {20, 20, 20, 255}, 0}; Person(mb, {0, 0, 0}, 1.4f, n, H_TRICORN, W_PISTOL); Sphere(mb, {0.13f * 1.4f, 0.95f * 1.4f, 0}, 0.1f * 1.4f, {20, 18, 18, 255}, 6, 4); }
    else if (k == "sentinel") { Look n{{120, 140, 140, 255}, {100, 110, 100, 255}, {180, 150, 70, 255}, {80, 90, 90, 255}, {60, 200, 190, 255}, 4}; Person(mb, {0, 0, 0}, 1.25f, n, H_HELM, W_SPEAR); }
    else if (k == "fire_imp") { Sphere(mb, {0, 0.4f, 0}, 0.25f, {230, 90, 30, 255}, 7, 5); mb.Cone({0, 0.55f, 0.1f}, {-0.05f, 0.85f, 0.15f}, 0.06f, 4, {255, 180, 60, 255}); mb.Cone({0, 0.55f, -0.1f}, {-0.05f, 0.85f, -0.15f}, 0.06f, 4, {255, 180, 60, 255}); }
    else if (k == "sun_god") { Look n{{230, 150, 60, 255}, {200, 90, 30, 255}, {255, 210, 80, 255}, {170, 70, 30, 255}, {255, 230, 120, 255}, 0}; Person(mb, {0, 0, 0}, 2.4f, n, H_CROWN, W_STAFF); for (int q = 0; q < 10; q++) { float a = q * 0.628f; mb.Cone({-0.1f, 2.4f, 0}, {-0.1f + cosf(a) * 0.0f, 2.4f + sinf(a) * 0.9f, cosf(a) * 0.9f}, 0.08f, 4, {255, 200, 60, 255}); } }
    else if (k == "kraken") { for (int q = 0; q < 8; q++) { float a = q * 0.785f; mb.Tube({{cosf(a) * 1.0f, -0.5f, sinf(a) * 1.0f}, {cosf(a) * 2.2f, 1.2f, sinf(a) * 2.2f}, {cosf(a + 0.4f) * 3.0f, 2.6f, sinf(a + 0.4f) * 3.0f}, {cosf(a + 0.9f) * 2.6f, 3.4f, sinf(a + 0.9f) * 2.6f}}, 0.45f, 0.08f, 8, {150, 40, 50, 255}, {230, 140, 120, 255}, 0); } Sphere(mb, {0, 0.6f, 0}, 1.6f, {130, 36, 50, 255}, 10, 7, 0.7f); Sphere(mb, {1.2f, 1.0f, 0.5f}, 0.25f, {250, 220, 80, 255}, 6, 4); Sphere(mb, {1.2f, 1.0f, -0.5f}, 0.25f, {250, 220, 80, 255}, 6, 4); }
    else if (k == "ghost_ship") { Color g{120, 170, 160, 255}; Hull(mb, 3.6f, 1.2f, 0.4f, 0.35f, Sh(g, 0.6f), Sh(g, 0.5f), {160, 230, 220, 255}); Mast(mb, {0.3f, 0.35f, 0}, 2.6f, Sh(g, 0.5f), {180, 230, 220, 255}, 0.7f); Mast(mb, {-0.9f, 0.35f, 0}, 2.0f, Sh(g, 0.5f), {170, 220, 210, 255}, 0.55f); }
    // ships
    else if (k == "fishing_boat") { Hull(mb, 1.5f, 0.6f, 0.2f, 0.2f, wood, Sh(wood, 1.2f), team); Mast(mb, {0.1f, 0.2f, 0}, 1.1f, wood, sail, 0.3f, false); BoxC(mb, {-0.45f, 0.3f, 0}, {0.12f, 0.1f, 0.15f}, {160, 140, 110, 255}); }
    else if (k == "scout_skiff") { Hull(mb, 1.2f, 0.4f, 0.14f, 0.14f, Sh(wood, 1.1f), Sh(wood, 1.3f), team); Mast(mb, {0.1f, 0.14f, 0}, 1.0f, wood, team, 0.28f, false); }
    else if (k == "transport" || k == "colony_ship" || k == "trade_ship") {
        float L2 = k == "colony_ship" ? 2.6f : 2.2f; Hull(mb, L2, 0.9f, 0.32f, 0.3f, hullC, Sh(wood, 1.2f), team);
        if (k == "transport") { BoxC(mb, {-0.2f, 0.42f, 0}, {0.6f, 0.12f, 0.35f}, Sh(wood, 0.9f)); Funnel(mb, {-0.75f, 0.3f, 0}, 0.6f, 0.1f, iron); }
        else { Mast(mb, {0.3f, 0.3f, 0}, 1.8f, wood, k == "trade_ship" ? Color{240, 220, 160, 255} : sail, 0.45f); Mast(mb, {-0.6f, 0.3f, 0}, 1.4f, wood, team, 0.35f); if (k == "colony_ship") { BoxC(mb, {0.6f, 0.45f, 0}, {0.2f, 0.15f, 0.2f}, {150, 120, 80, 255}); Sphere(mb, {-0.1f, 0.45f, 0.2f}, 0.14f, {140, 100, 60, 255}, 6, 4); } }
    }
    else if (k == "sloop" || k == "war_canoe") {
        if (k == "war_canoe") { Hull(mb, 2.2f, 0.45f, 0.18f, 0.16f, {150, 96, 54, 255}, {120, 80, 50, 255}, team); for (int q = -2; q <= 2; q++) Person(mb, {q * 0.3f, 0.05f, 0}, 0.5f, L, H_NONE, W_NONE, false); Two(mb, {1.1f, 0.2f, 0}, {1.35f, 0.65f, 0}, {1.0f, 0.4f, 0}, team); }
        else { Hull(mb, 1.9f, 0.7f, 0.26f, 0.24f, hullC, Sh(wood, 1.2f), team); Mast(mb, {0.15f, 0.24f, 0}, 1.7f, wood, sail, 0.42f); Flag(mb, {0.15f, 2.05f, 0}, team, 0.18f); Cannon(mb, {0.2f, 0.32f, 0.3f}, {0, 0, 1}, 0.2f, iron); Cannon(mb, {0.2f, 0.32f, -0.3f}, {0, 0, -1}, 0.2f, iron); }
    }
    else if (k == "gunboat") { Hull(mb, 2.2f, 0.8f, 0.28f, 0.28f, hullC, {90, 90, 96, 255}, team); BoxC(mb, {-0.2f, 0.42f, 0}, {0.35f, 0.14f, 0.25f}, Sh(hullC, 1.2f)); Funnel(mb, {-0.3f, 0.55f, 0}, 0.55f, 0.09f, iron); Cannon(mb, {0.55f, 0.42f, 0}, {1, 0.05f, 0}, 0.45f, iron); Flag(mb, {-0.75f, 0.95f, 0}, team, 0.16f); }
    else if (k == "torpedo_boat") { Hull(mb, 2.0f, 0.6f, 0.22f, 0.2f, Sh(hullC, 0.8f), {80, 80, 86, 255}, team); BoxC(mb, {-0.3f, 0.32f, 0}, {0.25f, 0.1f, 0.18f}, Sh(hullC, 1.1f)); Funnel(mb, {-0.4f, 0.4f, 0}, 0.4f, 0.07f, iron); for (int q = -1; q <= 1; q += 2) Cyl(mb, {0.1f, 0.28f, q * 0.18f}, {0.7f, 0.28f, q * 0.18f}, 0.05f, brass, 6); }
    else if (k == "ironclad") { Hull(mb, 3.2f, 1.2f, 0.4f, 0.35f, {58, 60, 66, 255}, {70, 72, 78, 255}, team); Box(mb, {-0.9f, 0.35f, -0.45f}, {0.9f, 0.75f, 0.45f}, {74, 76, 84, 255}); for (int q = 0; q < 2; q++) Funnel(mb, {-0.4f + q * 0.6f, 0.75f, 0}, 0.75f, 0.13f, {50, 48, 46, 255}); for (int q = -1; q <= 1; q++) { Cannon(mb, {q * 0.5f, 0.55f, 0.45f}, {0, 0, 1}, 0.3f, iron); Cannon(mb, {q * 0.5f, 0.55f, -0.45f}, {0, 0, -1}, 0.3f, iron); } Flag(mb, {-1.2f, 1.6f, 0}, team, 0.2f); }
    else if (k == "submarine") { mb.Tube({{-1.3f, 0.05f, 0}, {-0.6f, 0.2f, 0}, {0.6f, 0.2f, 0}, {1.4f, 0.05f, 0}}, 0.12f, 0.12f, 10, {48, 60, 90, 255}, {180, 150, 70, 255}, 0); Sphere(mb, {0, 0.12f, 0}, 0.36f, {52, 66, 96, 255}, 10, 6, 0.6f); BoxC(mb, {0.1f, 0.45f, 0}, {0.25f, 0.18f, 0.12f}, {60, 72, 100, 255}); for (int q = 0; q < 6; q++) mb.Cone({1.35f, 0.05f, 0}, {1.35f + 0.25f * cosf(q * 1.05f) * 0 + 0.3f, 0.05f + 0.2f * sinf(q * 1.05f), 0.2f * cosf(q * 1.05f)}, 0.04f, 3, brass); Sphere(mb, {0.8f, 0.25f, 0.2f}, 0.08f, {255, 220, 140, 255}, 6, 4); }
    else if (k == "ghost_worm") { mb.Tube({{-1.4f, -0.1f, 0}, {-0.6f, 0.3f, 0.2f}, {0.2f, 0.1f, -0.2f}, {0.9f, 0.45f, 0}, {1.2f, 0.7f, 0}}, 0.3f, 0.2f, 9, {200, 210, 220, 255}, {150, 160, 180, 255}, 0); Sphere(mb, {1.25f, 0.75f, 0}, 0.28f, {220, 230, 235, 255}, 8, 5); }
    else if (k == "airship") { Sphere(mb, {0, 1.6f, 0}, 0.9f, {200, 180, 140, 255}, 12, 7); for (int q = -1; q <= 1; q++) Cyl(mb, {-1.1f, 1.6f + q * 0.0f, q * 0.3f}, {1.1f, 1.6f, q * 0.3f}, 0.02f, {120, 90, 60, 255}, 4); BoxC(mb, {0, 0.6f, 0}, {0.6f, 0.15f, 0.2f}, wood); Two(mb, {-1.1f, 1.6f, 0}, {-1.5f, 2.2f, 0}, {-1.5f, 1.2f, 0}, team); Cyl(mb, {-0.6f, 0.6f, 0}, {-0.85f, 0.6f, 0}, 0.08f, brass, 6); }
    else if (k == "hero") {
        switch (faction) {
            case 0: { Look n = L; n.coat = {28, 40, 72, 255}; Person(mb, {0, 0, 0}, 1.4f, n, H_CAP, W_CUTLASS); Two(mb, {-0.1f, 1.2f, -0.25f}, {-0.35f, 0.3f, -0.3f}, {-0.1f, 0.3f, 0.3f}, Sh(n.coat, 0.8f)); break; }
            case 1: { Look n = L; n.coat = {230, 200, 120, 255}; Person(mb, {0, 0, 0}, 1.45f, n, H_CROWN, W_STAFF); for (int q = 0; q < 3; q++) Sphere(mb, {-0.4f, 0.4f + q * 0.18f, 0.3f}, 0.11f, {120, 90, 50, 255}, 6, 4); break; }
            case 2: { Mount(mb, {0, 0, 0}, 1.6f, 2, team); Sphere(mb, {0.5f, 1.0f, 0.45f}, 0.3f, {210, 60, 40, 255}, 8, 5, 0.6f); Sphere(mb, {0.5f, 1.0f, -0.45f}, 0.3f, {210, 60, 40, 255}, 8, 5, 0.6f); break; }
            case 3: { Mount(mb, {0, 0, 0}, 1.3f, 3, team); Look n = L; Person(mb, {-0.1f, 0.7f, 0}, 1.0f, n, H_CROWN, W_TRIDENT); break; }
            case 4: { Look n{{160, 180, 190, 255}, {60, 60, 70, 255}, {100, 220, 200, 255}, {60, 60, 70, 255}, team, 0}; Person(mb, {0, 0, 0}, 1.35f, n, H_NONE, W_NONE); Sphere(mb, {0.03f * 1.35f, 1.12f * 1.35f, 0}, 0.2f * 1.35f, {170, 190, 200, 255}, 8, 6, 1.2f); Sphere(mb, {0.2f, 1.55f, 0.08f}, 0.07f, {10, 10, 14, 255}, 5, 3); Sphere(mb, {0.2f, 1.55f, -0.08f}, 0.07f, {10, 10, 14, 255}, 5, 3); break; }
            default: { Look n = LookOf(5, team); Person(mb, {0, 0, 0}, 1.45f, n, H_GOGGLES, W_HAMMER); Cyl(mb, {-0.25f, 1.0f, 0}, {-0.25f, 1.5f, 0}, 0.07f, iron, 6); break; }
        }
    }
    else if (k == "ultimate") {
        switch (faction) {
            case 0: { mb.Tube({{-3.4f, 0.2f, 0}, {-1.5f, 0.6f, 0}, {1.5f, 0.6f, 0}, {3.6f, 0.2f, 0}}, 0.3f, 0.3f, 12, {40, 54, 90, 255}, {200, 160, 70, 255}, 0); Sphere(mb, {0, 0.4f, 0}, 0.9f, {44, 58, 96, 255}, 12, 7, 0.55f); for (int q = 0; q < 8; q++) mb.Cone({-3.4f, 0.25f, 0}, {-3.9f, 0.25f + 0.5f * sinf(q * 0.785f), 0.5f * cosf(q * 0.785f)}, 0.08f, 3, {180, 140, 60, 255}); for (int q = -1; q <= 1; q += 2) Two(mb, {-2.6f, 0.4f, 0}, {-3.2f, 0.4f, q * 1.0f}, {-2.0f, 0.4f, q * 0.3f}, {180, 140, 60, 255}); Sphere(mb, {2.6f, 0.65f, 0}, 0.35f, {255, 220, 120, 255}, 8, 5); break; }
            case 1: { Look n{{160, 100, 60, 255}, {220, 180, 90, 255}, {240, 80, 50, 255}, {160, 100, 60, 255}, team, 0}; Person(mb, {0, 0, 0}, 3.4f, n, H_FEATHER, W_HAMMER); break; }
            case 2: { Sphere(mb, {0, 1.1f, 0}, 1.4f, {220, 80, 60, 255}, 12, 7, 0.65f); mb.Tube({{-0.8f, 1.2f, 0}, {-2.2f, 1.5f, 0}, {-3.2f, 1.2f, 0}}, 0.9f, 0.35f, 10, {230, 110, 80, 255}, {250, 170, 120, 255}, 0); for (int q = -1; q <= 1; q += 2) { Cyl(mb, {0.8f, 1.2f, q * 0.8f}, {1.8f, 1.6f, q * 1.3f}, 0.15f, {200, 60, 40, 255}, 6); Sphere(mb, {2.0f, 1.7f, q * 1.4f}, 0.45f, {210, 60, 40, 255}, 8, 5, 0.6f); } Sphere(mb, {1.2f, 2.2f, 0}, 0.4f, {240, 200, 60, 255}, 6, 4); break; }
            case 3: { Look n{{80, 160, 160, 255}, {40, 100, 110, 255}, {230, 200, 90, 255}, {40, 100, 110, 255}, team, 2}; Person(mb, {0, 0, 0}, 3.6f, n, H_CROWN, W_TRIDENT); break; }
            case 4: { Look n{{60, 100, 80, 255}, {40, 60, 50, 255}, {120, 200, 120, 255}, {40, 60, 50, 255}, team, 0}; Person(mb, {0, 0, 0}, 3.4f, n, H_NONE, W_CLAW); for (int q = 0; q < 6; q++) mb.Tube({{0.4f * 3.4f, 1.0f * 3.4f, (q - 2.5f) * 0.12f}, {0.55f * 3.4f, 0.8f * 3.4f, (q - 2.5f) * 0.2f}, {0.6f * 3.4f, 0.6f * 3.4f, (q - 2.5f) * 0.25f}}, 0.12f, 0.03f, 6, {70, 120, 90, 255}, {120, 180, 130, 255}, 0); for (int q = -1; q <= 1; q += 2) Two(mb, {-0.3f, 3.2f, q * 0.3f}, {-1.4f, 4.6f, q * 2.0f}, {-1.0f, 2.4f, q * 1.6f}, {50, 80, 66, 255}); break; }
            default: { BoxC(mb, {0, 2.6f, 0}, {1.0f, 1.0f, 1.0f}, {90, 84, 80, 255}); for (int q = -1; q <= 1; q += 2) { Cyl(mb, {0, 1.6f, q * 0.6f}, {0.1f, 0, q * 0.7f}, 0.35f, {80, 76, 72, 255}, 8); Cyl(mb, {0, 3.0f, q * 1.05f}, {0.9f, 2.2f, q * 1.3f}, 0.25f, {100, 94, 88, 255}, 8); } for (int q = 0; q < 2; q++) Funnel(mb, {-0.5f, 3.6f, q * 0.6f - 0.3f}, 1.2f, 0.2f, {60, 58, 56, 255}); Sphere(mb, {1.0f, 2.9f, 0}, 0.25f, {255, 200, 80, 255}, 6, 4); for (int q = 0; q < 4; q++) Cannon(mb, {0, 2.4f, 0}, {cosf(q * 1.57f + 0.785f), 0, sinf(q * 1.57f + 0.785f)}, 1.4f, {60, 60, 66, 255}); break; }
        }
    }
    else Person(mb, {0, 0, 0}, s, L, H_NONE, W_SPEAR);
}

// ---------------------------------------------------------------- buildings (origin at the footprint's corner; size x size tiles)
struct Pal5 { Color wall, roof, trim, stone, accent; };
Pal5 PalOf(int f) {
    switch (f) {
        case 0: return {{196, 186, 166, 255}, {56, 74, 116, 255}, {200, 156, 70, 255}, {130, 126, 120, 255}, {80, 150, 190, 255}};
        case 1: return {{190, 150, 100, 255}, {170, 150, 80, 255}, {200, 70, 50, 255}, {140, 120, 100, 255}, {80, 150, 80, 255}};
        case 2: return {{200, 160, 140, 255}, {180, 64, 48, 255}, {240, 190, 140, 255}, {150, 120, 110, 255}, {230, 120, 90, 255}};
        case 3: return {{150, 200, 190, 255}, {50, 130, 130, 255}, {230, 210, 150, 255}, {110, 150, 150, 255}, {240, 130, 160, 255}};
        case 4: return {{176, 176, 168, 255}, {96, 110, 120, 255}, {200, 160, 70, 255}, {140, 140, 136, 255}, {90, 200, 190, 255}};
        case 5: return {{120, 116, 112, 255}, {70, 66, 64, 255}, {200, 120, 70, 255}, {96, 92, 90, 255}, {255, 170, 70, 255}};
        default: return {{170, 150, 120, 255}, {120, 90, 60, 255}, {200, 160, 80, 255}, {130, 120, 110, 255}, {200, 80, 60, 255}};
    }
}
void House(MB& mb, Vector3 lo, Vector3 hi, Color wall, Color roof, float roofH) {
    Box(mb, lo, hi, wall); float cx = (lo.x + hi.x) / 2, cz = (lo.z + hi.z) / 2; (void)cx;
    Vector3 a{lo.x - 0.08f, hi.y, lo.z - 0.08f}, b{hi.x + 0.08f, hi.y, lo.z - 0.08f}, c{hi.x + 0.08f, hi.y, hi.z + 0.08f}, d{lo.x - 0.08f, hi.y, hi.z + 0.08f}, r0{lo.x - 0.08f, hi.y + roofH, cz}, r1{hi.x + 0.08f, hi.y + roofH, cz};
    Two(mb, a, b, r1, roof); Two(mb, a, r1, r0, roof); Two(mb, d, r0, r1, Sh(roof, 0.85f)); Two(mb, d, r1, c, Sh(roof, 0.85f)); Two(mb, a, r0, d, Sh(wall, 0.9f)); Two(mb, b, c, r1, Sh(wall, 0.9f));
}
void Tower(MB& mb, Vector3 base, float r, float h, Color wall, Color roof) { Cyl(mb, base, Vector3Add(base, {0, h, 0}), r, wall, 10); mb.Cone(Vector3Add(base, {0, h, 0}), Vector3Add(base, {0, h + r * 1.4f, 0}), r * 1.25f, 10, roof); }
void BuildBuilding(MB& mb, const BuildingDef& d, int faction, Color team) {
    Pal5 P = PalOf(d.faction >= 0 ? d.faction : faction); float S = (float)d.size; const std::string& k = d.key; Color wood{124, 88, 54, 255}, iron{70, 72, 80, 255};
    auto pad = [&](Color c) { Box(mb, {0.05f, -0.2f, 0.05f}, {S - 0.05f, 0.12f, S - 0.05f}, c); };
    if (k == "harbor") { pad(P.stone); House(mb, {0.4f, 0.12f, 0.4f}, {2.6f, 1.3f, 2.4f}, P.wall, P.roof, 0.7f); Tower(mb, {3.2f, 0.12f, 0.8f}, 0.35f, 2.0f, P.wall, P.roof); Flag(mb, {3.2f, 3.0f, 0.8f}, team, 0.35f); Box(mb, {2.8f, 0.12f, 2.6f}, {3.9f, 0.4f, 3.9f}, wood); Cyl(mb, {3.6f, 0.4f, 3.4f}, {3.6f, 1.6f, 3.4f}, 0.06f, wood, 5); Cyl(mb, {3.6f, 1.6f, 3.4f}, {2.8f, 1.7f, 3.4f}, 0.04f, wood, 4); for (int q = 0; q < 3; q++) BoxC(mb, {0.6f + q * 0.4f, 0.3f, 3.3f}, {0.15f, 0.15f, 0.15f}, {150, 110, 70, 255}); }
    else if (k == "cottage") { pad(Sh(P.stone, 1.05f)); House(mb, {0.35f, 0.12f, 0.35f}, {1.65f, 0.85f, 1.65f}, P.wall, P.roof, 0.55f); Cyl(mb, {1.3f, 1.0f, 0.6f}, {1.3f, 1.55f, 0.6f}, 0.08f, P.stone, 6); }
    else if (k == "farmstead") { pad(P.stone); House(mb, {0.25f, 0.12f, 0.3f}, {1.4f, 0.9f, 1.7f}, {170, 80, 60, 255}, P.roof, 0.5f); Cyl(mb, {1.65f, 0.12f, 1.55f}, {1.65f, 1.4f, 1.55f}, 0.28f, {200, 190, 170, 255}, 10); Sphere(mb, {1.65f, 1.4f, 1.55f}, 0.28f, P.roof, 8, 4); }
    else if (k == "mine_shed") { pad(P.stone); House(mb, {0.3f, 0.12f, 0.3f}, {1.4f, 0.8f, 1.2f}, wood, P.roof, 0.4f); BoxC(mb, {1.5f, 0.3f, 1.5f}, {0.22f, 0.15f, 0.15f}, iron); for (int q = 0; q < 2; q++) Cyl(mb, {1.35f + q * 0.3f, 0.12f, 1.45f}, {1.35f + q * 0.3f, 0.12f, 1.55f}, 0.08f, {50, 50, 50, 255}, 6); }
    else if (k == "farm") { if (faction == 2) {} for (int r = 0; r < 6; r++) Box(mb, {0.15f, 0.0f, 0.2f + r * 0.45f}, {S - 0.15f, 0.12f, 0.45f + r * 0.45f}, r % 2 ? Color{110, 80, 50, 255} : Color{120, 170, 70, 255}); for (int r = 0; r < 6; r++) for (int c = 0; c < 5; c++) Sphere(mb, {0.4f + c * 0.55f, 0.18f, 0.32f + r * 0.45f}, 0.1f, {200, 190, 80, 255}, 5, 3); }
    else if (k == "dock") { Box(mb, {0, -0.3f, 0}, {S, 0.25f, S}, wood); for (int q = 0; q < 4; q++) Cyl(mb, {0.2f + (q % 2) * 2.6f, -1.0f, 0.2f + (q / 2) * 2.6f}, {0.2f + (q % 2) * 2.6f, 0.4f, 0.2f + (q / 2) * 2.6f}, 0.1f, Sh(wood, 0.8f), 6); House(mb, {0.3f, 0.25f, 0.3f}, {1.5f, 1.0f, 1.4f}, P.wall, P.roof, 0.5f); Cyl(mb, {2.3f, 0.25f, 2.3f}, {2.3f, 2.0f, 2.3f}, 0.07f, wood, 5); Cyl(mb, {2.3f, 2.0f, 2.3f}, {1.4f, 2.1f, 2.3f}, 0.05f, wood, 4); Flag(mb, {0.4f, 1.9f, 2.6f}, team, 0.3f); }
    else if (k == "barracks") { pad(P.stone); House(mb, {0.3f, 0.12f, 0.4f}, {2.7f, 1.2f, 1.8f}, P.wall, P.roof, 0.6f); Flag(mb, {2.6f, 2.6f, 2.5f}, team, 0.35f); for (int q = 0; q < 3; q++) { Cyl(mb, {0.5f + q * 0.7f, 0.12f, 2.5f}, {0.5f + q * 0.7f, 0.85f, 2.5f}, 0.05f, wood, 4); Sphere(mb, {0.5f + q * 0.7f, 0.9f, 2.5f}, 0.12f, {200, 180, 140, 255}, 6, 4); } }
    else if (k == "stable") { pad(Sh(P.stone, 1.1f)); House(mb, {0.3f, 0.12f, 0.3f}, {2.7f, 1.0f, 1.5f}, wood, P.roof, 0.5f); for (int q = 0; q < 6; q++) Cyl(mb, {0.2f + q * 0.52f, 0.12f, 2.7f}, {0.2f + q * 0.52f, 0.6f, 2.7f}, 0.04f, wood, 4); Cyl(mb, {0.2f, 0.5f, 2.7f}, {2.8f, 0.5f, 2.7f}, 0.03f, wood, 4); }
    else if (k == "lighthouse") { pad(P.stone); Taper(mb, {1, 0.12f, 1}, {1, 3.6f, 1}, 0.6f, 0.38f, {236, 232, 220, 255}, 10); for (int q = 0; q < 3; q++) Taper(mb, {1, 0.6f + q * 1.1f, 1}, {1, 0.95f + q * 1.1f, 1}, 0.59f - q * 0.07f, 0.55f - q * 0.07f, {200, 60, 50, 255}, 10); Cyl(mb, {1, 3.6f, 1}, {1, 4.2f, 1}, 0.32f, {255, 236, 170, 255}, 8); mb.Cone({1, 4.2f, 1}, {1, 4.65f, 1}, 0.45f, 10, P.roof); }
    else if (k == "watchtower") { for (int q = 0; q < 4; q++) Cyl(mb, {0.15f + (q % 2) * 0.7f, 0, 0.15f + (q / 2) * 0.7f}, {0.3f + (q % 2) * 0.4f, 2.0f, 0.3f + (q / 2) * 0.4f}, 0.05f, wood, 4); Box(mb, {0.05f, 2.0f, 0.05f}, {0.95f, 2.5f, 0.95f}, wood); mb.Cone({0.5f, 2.5f, 0.5f}, {0.5f, 3.0f, 0.5f}, 0.65f, 4, P.roof); }
    else if (k == "palisade") { for (int q = 0; q < 3; q++) { Cyl(mb, {0.2f + q * 0.3f, 0, 0.5f}, {0.2f + q * 0.3f, 1.1f, 0.5f}, 0.12f, wood, 6); mb.Cone({0.2f + q * 0.3f, 1.1f, 0.5f}, {0.2f + q * 0.3f, 1.35f, 0.5f}, 0.12f, 6, Sh(wood, 1.2f)); } }
    else if (k == "seawall") Box(mb, {0, 0, 0}, {1, 1.2f, 1}, P.stone);
    else if (k == "colony_hall") { pad(P.stone); House(mb, {0.3f, 0.12f, 0.3f}, {2.4f, 1.3f, 2.2f}, P.wall, P.roof, 0.6f); Tower(mb, {2.5f, 0.12f, 2.5f}, 0.3f, 1.7f, P.wall, P.roof); Flag(mb, {2.5f, 2.6f, 2.5f}, team, 0.3f); }
    else if (k == "workshop") { pad(P.stone); House(mb, {0.3f, 0.12f, 0.3f}, {2.6f, 1.2f, 2.2f}, P.wall, P.roof, 0.5f); Cyl(mb, {2.3f, 1.0f, 0.6f}, {2.3f, 2.6f, 0.6f}, 0.14f, P.stone, 8); for (int q = 0; q < 8; q++) { float a = q * 0.785f; BoxC(mb, {1.4f + cosf(a) * 0.45f, 0.7f + sinf(a) * 0.45f, 2.25f}, {0.07f, 0.07f, 0.05f}, P.trim); } Cyl(mb, {1.4f, 0.7f, 2.2f}, {1.4f, 0.7f, 2.3f}, 0.42f, P.trim, 12); }
    else if (k == "siege_works") { pad(P.stone); House(mb, {0.3f, 0.12f, 0.3f}, {2.4f, 1.0f, 1.6f}, wood, P.roof, 0.45f); Cannon(mb, {1.5f, 0.35f, 2.3f}, {1, 0.3f, 0}, 0.9f, iron); Cyl(mb, {1.3f, 0.3f, 2.1f}, {1.3f, 0.3f, 2.5f}, 0.22f, wood, 8); }
    else if (k == "exchange") { pad(P.stone); Box(mb, {0.3f, 0.12f, 0.3f}, {2.7f, 1.3f, 2.7f}, P.wall); Sphere(mb, {1.5f, 1.3f, 1.5f}, 0.9f, P.trim, 12, 6); for (int q = 0; q < 4; q++) Cyl(mb, {0.4f + q * 0.7f, 0.12f, 0.2f}, {0.4f + q * 0.7f, 1.3f, 0.2f}, 0.1f, Sh(P.wall, 1.1f), 6); Cyl(mb, {1.5f, 1.4f, 0.2f}, {1.5f, 1.4f, 0.1f}, 0.25f, {240, 200, 60, 255}, 10); }
    else if (k == "coastal_battery") { Box(mb, {0, 0, 0}, {S, 0.9f, S}, P.stone); for (int q = 0; q < 4; q++) BoxC(mb, {0.25f + (q % 2) * 1.5f, 1.05f, 0.25f + (q / 2) * 1.5f}, {0.2f, 0.15f, 0.2f}, P.stone); Cannon(mb, {1, 1.15f, 1}, {1, 0.05f, 0}, 1.2f, iron); Flag(mb, {0.3f, 2.2f, 0.3f}, team, 0.25f); }
    else if (k == "coaling_station") { pad(P.stone); for (int q = 0; q < 3; q++) Sphere(mb, {0.5f + q * 0.4f, 0.2f, 0.6f + (q % 2) * 0.6f}, 0.45f, {36, 34, 34, 255}, 7, 5, 0.6f); Cyl(mb, {1.7f, 0.12f, 1.6f}, {1.7f, 1.9f, 1.6f}, 0.07f, iron, 5); Cyl(mb, {1.7f, 1.9f, 1.6f}, {0.8f, 2.0f, 1.6f}, 0.05f, iron, 4); }
    else if (k == "chapel") { pad(P.stone); House(mb, {0.3f, 0.12f, 0.45f}, {1.5f, 1.1f, 1.55f}, P.wall, P.roof, 0.6f); Tower(mb, {1.65f, 0.12f, 1.0f}, 0.25f, 2.0f, P.wall, P.roof); Sphere(mb, {1.65f, 2.1f, 1.0f}, 0.12f, P.accent, 6, 4); }
    else if (k == "airship_hangar") { pad(P.stone); for (int q = 0; q < 6; q++) { float a = PI * q / 5; Box(mb, {0.2f, 0.12f + sinf(a) * 1.6f, 1.5f - cosf(a) * 1.3f - 0.15f}, {2.8f, 0.3f + sinf(a) * 1.6f, 1.5f - cosf(a) * 1.3f + 0.15f}, P.roof); } Box(mb, {0.15f, 0.12f, 0.15f}, {0.35f, 1.6f, 2.85f}, P.wall); }
    else if (k == "flak_tower") { Cyl(mb, {0.5f, 0, 0.5f}, {0.5f, 2.0f, 0.5f}, 0.4f, P.stone, 8); for (int q = 0; q < 2; q++) Cannon(mb, {0.5f, 2.15f, 0.4f + q * 0.2f}, Vector3Normalize({0.4f, 1, 0}), 0.7f, iron); }
    else if (k == "totem") { Taper(mb, {0.5f, 0, 0.5f}, {0.5f, 2.2f, 0.5f}, 0.22f, 0.18f, {150, 100, 60, 255}, 7); for (int q = 0; q < 3; q++) Sphere(mb, {0.68f, 0.5f + q * 0.6f, 0.5f}, 0.1f, q % 2 ? team : Color{220, 60, 40, 255}, 5, 3); Two(mb, {0.5f, 2.0f, 0.5f}, {0.5f, 1.6f, -0.2f}, {0.5f, 1.8f, 0.5f}, {80, 150, 80, 255}); Two(mb, {0.5f, 2.0f, 0.5f}, {0.5f, 1.8f, 0.5f}, {0.5f, 1.6f, 1.2f}, {80, 150, 80, 255}); }
    else if (k == "brood_pool") { Box(mb, {0.1f, -0.1f, 0.1f}, {1.9f, 0.18f, 1.9f}, {150, 120, 110, 255}); Box(mb, {0.3f, 0.18f, 0.3f}, {1.7f, 0.2f, 1.7f}, {80, 150, 150, 255}); for (int q = 0; q < 6; q++) Sphere(mb, {0.5f + (q % 3) * 0.5f, 0.24f, 0.6f + (q / 3) * 0.7f}, 0.1f, {240, 180, 120, 255}, 5, 3); }
    else if (k == "wonder") {
        pad(P.stone);
        switch (faction) {
            case 0: Box(mb, {0.2f, 0.12f, 0.6f}, {3.8f, 0.6f, 3.4f}, P.stone); mb.Tube({{0.6f, 0.9f, 2.0f}, {2.0f, 1.2f, 2.0f}, {3.5f, 0.9f, 2.0f}}, 0.6f, 0.4f, 12, {44, 58, 96, 255}, {200, 160, 70, 255}, 0); for (int q = 0; q < 3; q++) Cyl(mb, {0.5f + q * 1.4f, 0.6f, 0.7f}, {0.5f + q * 1.4f, 2.6f, 0.7f}, 0.08f, P.trim, 5); break;
            case 1: for (int q = 0; q < 4; q++) Box(mb, {0.2f + q * 0.4f, 0.12f + q * 0.6f, 0.2f + q * 0.4f}, {3.8f - q * 0.4f, 0.72f + q * 0.6f, 3.8f - q * 0.4f}, Sh(P.wall, 1 - q * 0.05f)); Sphere(mb, {2, 3.0f, 2}, 0.5f, {255, 210, 80, 255}, 8, 5); break;
            case 2: Sphere(mb, {2, 0.3f, 2}, 2.0f, {170, 90, 70, 255}, 12, 7, 0.6f); for (int q = 0; q < 5; q++) mb.Cone({1.0f + (q % 3) * 1.0f, 1.2f, 1.2f + (q / 3) * 1.4f}, {1.0f + (q % 3) * 1.0f, 2.8f + q * 0.2f, 1.2f + (q / 3) * 1.4f}, 0.3f, 6, {220, 120, 90, 255}); break;
            case 3: for (int q = 0; q < 9; q++) { float a = q * 0.7f; mb.Tube({{2, 0.1f, 2}, {2 + cosf(a) * 0.8f, 1.2f, 2 + sinf(a) * 0.8f}, {2 + cosf(a) * 1.3f, 2.2f + (q % 3) * 0.3f, 2 + sinf(a) * 1.3f}}, 0.25f, 0.08f, 6, {240, 130, 160, 255}, {255, 190, 200, 255}, 0); } BoxC(mb, {2, 0.6f, 2}, {0.7f, 0.5f, 0.7f}, {230, 210, 150, 255}); break;
            case 4: Taper(mb, {2, 0.12f, 2}, {2, 5.5f, 2}, 1.3f, 0.15f, P.wall, 8); for (int q = 0; q < 4; q++) Cyl(mb, {2 + cosf(q * 1.57f) * 1.6f, 0.12f, 2 + sinf(q * 1.57f) * 1.6f}, {2 + cosf(q * 1.57f) * 1.6f, 2.2f, 2 + sinf(q * 1.57f) * 1.6f}, 0.18f, Sh(P.wall, 1.1f), 6); Sphere(mb, {2, 5.6f, 2}, 0.25f, P.accent, 6, 4); break;
            default: Box(mb, {0.4f, 0.12f, 0.4f}, {3.6f, 2.0f, 3.6f}, P.wall); for (int q = 0; q < 4; q++) Funnel(mb, {0.9f + (q % 2) * 2.2f, 2.0f, 0.9f + (q / 2) * 2.2f}, 2.2f, 0.3f, {60, 58, 56, 255}); Box(mb, {1.4f, 0.5f, 3.6f}, {2.6f, 1.4f, 3.65f}, {255, 150, 50, 255}); break;
        }
    }
    else { pad(P.stone); House(mb, {0.3f, 0.12f, 0.3f}, {S - 0.3f, 1.0f, S - 0.3f}, P.wall, P.roof, 0.5f); }
}

// ---------------------------------------------------------------- resource nodes and sites
void BuildNode(MB& mb, int kind) {
    switch (kind) {
        case N_FISH: for (int q = 0; q < 5; q++) { float a = q * 1.26f, r = 0.25f + 0.1f * (q % 2); Vector3 c{cosf(a) * r, -0.1f, sinf(a) * r}; mb.Tube({Vector3Add(c, {-0.12f, 0, 0}), Vector3Add(c, {0.12f, 0, 0})}, 0.05f, 0.02f, 5, {190, 210, 220, 255}, {150, 170, 190, 255}, 0); } break;
        case N_GROVE: for (int q = 0; q < 3; q++) { float a = q * 2.1f; Vector3 b{cosf(a) * 0.25f, 0, sinf(a) * 0.25f}; Vector3 top = Vector3Add(b, {0.1f, 1.3f + q * 0.15f, 0}); mb.Tube({b, Vector3Add(b, {0.05f, 0.7f, 0}), top}, 0.07f, 0.05f, 5, {120, 90, 60, 255}, {140, 104, 70, 255}, 0); for (int l = 0; l < 6; l++) { float la = l * 1.047f; Two(mb, top, Vector3Add(top, {cosf(la) * 0.6f, -0.3f, sinf(la) * 0.6f}), Vector3Add(top, {cosf(la + 0.3f) * 0.45f, -0.15f, sinf(la + 0.3f) * 0.45f}), {70, 150, 60, 255}); } Sphere(mb, Vector3Add(top, {0.08f, -0.1f, 0.05f}), 0.08f, {230, 160, 50, 255}, 5, 3); } break;
        case N_KELP: for (int q = 0; q < 6; q++) { float a = q * 1.05f, r = 0.3f + 0.15f * (q % 2); Vector3 b{cosf(a) * r, -1.5f, sinf(a) * r}; mb.Tube({b, Vector3Add(b, {0.1f, 0.9f, 0.05f}), Vector3Add(b, {-0.05f, 1.6f, 0}), Vector3Add(b, {0.08f, 1.75f, 0})}, 0.05f, 0.02f, 4, {60, 120, 60, 255}, {120, 170, 70, 255}, 0); } break;
        case N_BRASS: for (int q = 0; q < 5; q++) { float a = q * 1.3f; mb.Octa({cosf(a) * 0.3f, 0.25f + 0.1f * (q % 2), sinf(a) * 0.3f}, 0.28f, q % 2 ? Color{210, 170, 70, 255} : Color{130, 120, 104, 255}); } break;
        case N_WRECK: { Color w{100, 80, 60, 255}; for (int s = -1; s <= 1; s += 2) { Two(mb, {-0.9f, 0.2f, s * 0.3f}, {0.6f, 0.4f, s * 0.35f}, {0.6f, -0.3f, s * 0.2f}, w); Two(mb, {-0.9f, 0.2f, s * 0.3f}, {0.6f, -0.3f, s * 0.2f}, {-0.9f, -0.3f, s * 0.15f}, w); } Cyl(mb, {0, 0.1f, 0}, {0.5f, 1.2f, 0.2f}, 0.05f, w, 4); break; }
        case N_COAL: for (int q = 0; q < 5; q++) { float a = q * 1.3f; mb.Octa({cosf(a) * 0.3f, 0.22f, sinf(a) * 0.3f}, 0.27f, q % 2 ? Color{40, 38, 40, 255} : Color{70, 66, 66, 255}); } break;
        case N_VENT: for (int q = 0; q < 4; q++) { float a = q * 1.57f; mb.Octa({cosf(a) * 0.4f, 0.15f, sinf(a) * 0.4f}, 0.25f, {70, 60, 80, 255}); } mb.Octa({0, 0.1f, 0}, 0.2f, {200, 90, 255, 255}); break;
        case N_PEARL: for (int q = 0; q < 4; q++) { float a = q * 1.57f; Sphere(mb, {cosf(a) * 0.25f, -0.2f, sinf(a) * 0.25f}, 0.14f, {200, 190, 200, 255}, 6, 3, 0.4f); } Sphere(mb, {0, -0.12f, 0}, 0.08f, {250, 245, 250, 255}, 5, 3); break;
        default: break;
    }
}
void BuildSite(MB& mb, int kind, int variant) {
    Color stone{130, 126, 118, 255}, wood{120, 86, 52, 255};
    switch (kind) {
        case S_COVE: { for (int q = 0; q < 6; q++) { float a = q * 1.047f; Box(mb, {cosf(a) * 3 - 0.6f, 0, sinf(a) * 3 - 0.6f}, {cosf(a) * 3 + 0.6f, 1.6f, sinf(a) * 3 + 0.6f}, stone); } Tower(mb, {0, 0, 0}, 0.9f, 2.6f, stone, {60, 40, 36, 255}); Flag(mb, {0, 4.6f, 0}, {20, 20, 22, 255}, 0.5f); Sphere(mb, {-0.35f, 4.35f, 0}, 0.1f, {230, 230, 220, 255}, 5, 3); break; }
        case S_TRIBE: { House(mb, {-1.6f, 0, -0.8f}, {1.6f, 1.2f, 0.8f}, {170, 130, 80, 255}, {190, 170, 90, 255}, 1.0f); for (int q = 0; q < 4; q++) { float a = q * 1.57f + 0.6f; Vector3 c{cosf(a) * 3, 0, sinf(a) * 3}; Cyl(mb, c, Vector3Add(c, {0, 0.8f, 0}), 0.55f, {160, 120, 70, 255}, 8); mb.Cone(Vector3Add(c, {0, 0.8f, 0}), Vector3Add(c, {0, 1.6f, 0}), 0.75f, 8, {190, 170, 90, 255}); } Taper(mb, {2.2f, 0, -1.6f}, {2.2f, 2.4f, -1.6f}, 0.25f, 0.2f, {150, 100, 60, 255}, 7); if (variant) Sphere(mb, {2.2f, 2.5f, -1.6f}, 0.25f, {240, 200, 60, 255}, 6, 4); break; }
        case S_ALTAR: { Box(mb, {-1.2f, 0, -1.2f}, {1.2f, 0.5f, 1.2f}, {90, 80, 76, 255}); Box(mb, {-0.6f, 0.5f, -0.6f}, {0.6f, 1.1f, 0.6f}, {110, 96, 88, 255}); mb.Octa({0, 1.5f, 0}, 0.35f, {255, 170, 60, 255}); for (int q = 0; q < 4; q++) Cyl(mb, {cosf(q * 1.57f) * 1.6f, 0, sinf(q * 1.57f) * 1.6f}, {cosf(q * 1.57f) * 1.6f, 1.8f, sinf(q * 1.57f) * 1.6f}, 0.18f, {100, 90, 84, 255}, 6); break; }
        case S_RUIN: { for (int q = 0; q < 7; q++) { float a = q * 0.9f, r = 1.8f; float h = 0.6f + (q * 37 % 5) * 0.35f; Cyl(mb, {cosf(a) * r, 0, sinf(a) * r}, {cosf(a) * r, h, sinf(a) * r}, 0.22f, {170, 176, 168, 255}, 8); } Box(mb, {-0.8f, 0, -0.5f}, {0.8f, 0.3f, 0.5f}, {150, 156, 150, 255}); if (variant) mb.Octa({0, 0.7f, 0}, 0.3f, {120, 230, 220, 255}); break; }
        default: break;
    }
}
Color Shrink(Color c) { return c; }
}  // namespace

Color PlayerColor(int c) { static const Color C[8] = {{220, 64, 56, 255}, {60, 120, 230, 255}, {80, 190, 90, 255}, {240, 200, 60, 255}, {170, 90, 220, 255}, {240, 140, 50, 255}, {40, 40, 44, 255}, {200, 200, 200, 255}}; return C[std::clamp(c, 0, 7)]; }
Color FactionTone(int f) { return PalOf(f).roof; }
const Model& UnitModel(int def, int faction, int color) {
    static std::map<int, Model> cache; int key = def * 64 + std::clamp(faction + 1, 0, 7) * 8 + std::clamp(color, 0, 7);
    auto it = cache.find(key); if (it != cache.end()) return it->second;
    MB mb; BuildUnit(mb, B().units[std::clamp(def, 0, (int)B().units.size() - 1)], faction, PlayerColor(color)); if (mb.Tris() == 0) Sphere(mb, {0, 0.4f, 0}, 0.3f, PlayerColor(color));
    return cache[key] = LoadModelFromMesh(mb.Build());
}
const Model& BuildingModel(int def, int faction, int color) {
    static std::map<int, Model> cache; int key = def * 64 + std::clamp(faction + 1, 0, 7) * 8 + std::clamp(color, 0, 7);
    auto it = cache.find(key); if (it != cache.end()) return it->second;
    MB mb; BuildBuilding(mb, B().buildings[std::clamp(def, 0, (int)B().buildings.size() - 1)], faction, PlayerColor(color));
    return cache[key] = LoadModelFromMesh(mb.Build());
}
const Model& NodeModel(int kind) {
    static std::map<int, Model> cache; auto it = cache.find(kind); if (it != cache.end()) return it->second;
    MB mb; BuildNode(mb, kind); if (mb.Tris() == 0) Sphere(mb, {0, 0, 0}, 0.1f, WHITE);
    return cache[kind] = LoadModelFromMesh(mb.Build());
}
const Model& SiteModel(int kind, int variant) {
    static std::map<int, Model> cache; int key = kind * 4 + variant; auto it = cache.find(key); if (it != cache.end()) return it->second;
    MB mb; BuildSite(mb, kind, variant); if (mb.Tris() == 0) Sphere(mb, {0, 0, 0}, 0.1f, WHITE);
    return cache[key] = LoadModelFromMesh(mb.Build());
}
const Model& RingModel() {
    static Model m{}; static bool made = false; if (made) return m;
    MB mb; const int N = 24; for (int i = 0; i < N; i++) { float a0 = 2 * PI * i / N, a1 = 2 * PI * (i + 1) / N; Vector3 i0{cosf(a0) * 0.85f, 0, sinf(a0) * 0.85f}, i1{cosf(a1) * 0.85f, 0, sinf(a1) * 0.85f}, o0{cosf(a0), 0, sinf(a0)}, o1{cosf(a1), 0, sinf(a1)}; Two(mb, i0, o0, o1, WHITE); Two(mb, i0, o1, i1, WHITE); }
    m = LoadModelFromMesh(mb.Build()); made = true; return m;
}

// ---------------------------------------------------------------- the terrain
static float DispH(const World& w, int x, int y) {
    x = std::clamp(x, 0, w.W - 1); y = std::clamp(y, 0, w.H - 1); int k = w.Idx(x, y); int t = w.tile[k];
    if (w.Land(t) || t == T_MOUNTAIN || t == T_LAVA) return 0.18f + std::max(0.0f, w.height[k] - 0.3f) * 0.55f;
    return t == T_SHALLOW ? -0.45f : t == T_KELP ? -1.2f : -1.6f - std::min(2.5f, -w.height[k] - 2) * 0.6f;
}
float TileY(const World& w, float x, float y) {
    int ix = (int)floorf(x - 0.5f), iy = (int)floorf(y - 0.5f); float fx = x - 0.5f - ix, fy = y - 0.5f - iy;
    float a = DispH(w, ix, iy), b = DispH(w, ix + 1, iy), c = DispH(w, ix, iy + 1), d = DispH(w, ix + 1, iy + 1);
    float h = (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy; return std::max(0.0f, h);
}
static Color TileCol(const World& w, int k, uint32_t hsh) {
    float n = 0.92f + 0.12f * ((hsh % 97) / 97.0f);
    if (w.isle[k] >= 0 && w.isle[k] < (int)w.islands.size() && w.islands[w.isle[k]].kind == I_VOLCANO && w.tile[k] == T_HILL) { float up = std::clamp((w.height[k] - 0.3f) / 7.0f, 0.0f, 1.0f); return Sh(Mix({110, 104, 84, 255}, {74, 64, 62, 255}, up * 1.4f), n); }   // (ash and black rock toward the crater)
    switch (w.tile[k]) {
        case T_BEACH: return Sh({214, 194, 146, 255}, n); case T_GRASS: return Sh({112, 152, 78, 255}, n); case T_JUNGLE: return Sh({62, 108, 58, 255}, n);
        case T_HILL: return Sh({140, 146, 92, 255}, n); case T_MOUNTAIN: return Sh({126, 118, 112, 255}, n); case T_LAVA: return {255, 120, 40, 255};
        case T_SHALLOW: return Sh({176, 170, 128, 255}, n); case T_KELP: return Sh({70, 116, 70, 255}, n); case T_CURRENT: return Sh({50, 86, 112, 255}, n);
        default: return Sh({44, 74, 94, 255}, n);
    }
}
void BuildTerrain(const World& w, TerrainMesh& t) {
    uint32_t key = w.set.seed * 31 + (uint32_t)w.W; int lava = 0; for (uint8_t x : w.tile) lava += x == T_LAVA; key = key * 131 + (uint32_t)lava;
    if (t.ready && t.key == key) return;
    if (t.ready) UnloadModel(t.model);
    // corner heights: the average of the four tiles round each corner (coasts slope smoothly into the sea)
    std::vector<float> C((w.W + 1) * (w.H + 1));
    for (int y = 0; y <= w.H; y++) for (int x = 0; x <= w.W; x++) { float s = DispH(w, x - 1, y - 1) + DispH(w, x, y - 1) + DispH(w, x - 1, y) + DispH(w, x, y); C[y * (w.W + 1) + x] = s / 4; }
    // corner colours: the average of the four tiles round each corner (no checkerboard; coasts blend into the shallows)
    std::vector<Vector3> CC((w.W + 1) * (w.H + 1));
    for (int y = 0; y <= w.H; y++) for (int x = 0; x <= w.W; x++) { Vector3 sum{0, 0, 0}; for (int q = 0; q < 4; q++) { int tx = std::clamp(x - 1 + (q & 1), 0, w.W - 1), ty = std::clamp(y - 1 + (q >> 1), 0, w.H - 1); uint32_t hh = (uint32_t)(tx * 73856093) ^ (uint32_t)(ty * 19349663); Color c = TileCol(w, w.Idx(tx, ty), hh); sum = Vector3Add(sum, {(float)c.r, (float)c.g, (float)c.b}); } CC[y * (w.W + 1) + x] = Vector3Scale(sum, 0.25f); }
    auto CO = [&](int x, int y, float k) { Vector3 v = CC[y * (w.W + 1) + x]; return Color{(unsigned char)std::clamp(v.x * k, 0.0f, 255.0f), (unsigned char)std::clamp(v.y * k, 0.0f, 255.0f), (unsigned char)std::clamp(v.z * k, 0.0f, 255.0f), 255}; };
    MB mb; t.base.clear();
    for (int y = 0; y < w.H; y++) for (int x = 0; x < w.W; x++) {
        int k = w.Idx(x, y); uint32_t hsh = (uint32_t)(x * 73856093) ^ (uint32_t)(y * 19349663);
        Vector3 a{(float)x, C[y * (w.W + 1) + x], (float)y}, b{(float)x + 1, C[y * (w.W + 1) + x + 1], (float)y}, cc{(float)x + 1, C[(y + 1) * (w.W + 1) + x + 1], (float)y + 1}, d{(float)x, C[(y + 1) * (w.W + 1) + x], (float)y + 1};
        if (w.tile[k] == T_MOUNTAIN) { float m = 0.35f + ((hsh >> 8) % 7) * 0.06f; a.y += m; b.y += m * 0.8f; cc.y += m; d.y += m * 0.9f; }
        float lit = 0.96f + 0.06f * ((hsh >> 4) % 5) / 4.0f;
        Color ca = CO(x, y, lit), cb = CO(x + 1, y, lit), ccc = CO(x + 1, y + 1, lit), cd = CO(x, y + 1, lit);
        if (w.tile[k] == T_LAVA) ca = cb = ccc = cd = {255, 120, 40, 255};
        mb.Tri(a, cc, b, ca, ccc, cb, {0, 0}, {0, 0}, {0, 0}); mb.Tri(a, d, cc, ca, cd, ccc, {0, 0}, {0, 0}, {0, 0});
        for (Color c : {ca, ccc, cb, ca, cd, ccc}) { t.base.push_back(c.r); t.base.push_back(c.g); t.base.push_back(c.b); t.base.push_back(255); }
    }    t.model = LoadModelFromMesh(mb.Build()); t.verts = t.model.meshes[0].vertexCount; t.ready = true; t.key = key;
}
void ShadeTerrain(const World& w, TerrainMesh& t, int viewer) {
    if (!t.ready || viewer < 0 || viewer >= (int)w.players.size()) return;
    const Player& P = w.players[viewer]; Mesh& m = t.model.meshes[0]; if (!m.colors || (int)t.base.size() < m.vertexCount * 4) return;
    for (int k = 0; k < w.W * w.H && k * 6 < m.vertexCount; k++) {
        float f = P.seen.empty() || P.seen[k] ? 1.0f : P.explored[k] ? 0.55f : 0.0f;
        for (int v = 0; v < 6; v++) { int i = (k * 6 + v) * 4; if (f == 0) { m.colors[i] = 6; m.colors[i + 1] = 8; m.colors[i + 2] = 12; } else { m.colors[i] = (unsigned char)(t.base[i] * f); m.colors[i + 1] = (unsigned char)(t.base[i + 1] * f); m.colors[i + 2] = (unsigned char)(t.base[i + 2] * f); } }
    }
    UpdateMeshBuffer(m, 3, m.colors, m.vertexCount * 4, 0);
}
void UnloadFathomsArt() {}
}  // namespace fa
