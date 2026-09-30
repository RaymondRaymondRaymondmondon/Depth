// ============================================================================
//  The Study's two calm scenes (Master Reference, "The Study: two calm scenes"): the Lamplight Lounge and the
//  Captain's Study. Darkest Dungeon ink, the rig, chains and the three-light setup, tuned down so they sit in the
//  corner of the eye: idle motion at 0.5 Hz or slower (the fire's flicker and the band's playing stay small), no
//  flashes, nothing appears suddenly, and the lower third is always a quiet floor for the desk rail and the cards.
// ============================================================================
#include "game.h"
#include "rig.h"
#include "study.h"
#include <algorithm>
#include <cmath>

Vector2 StudyDrawCat(Vector2 feet, float k, bool right, float t, bool asleep);   // salon.cpp
void RigRunAfterInk(Vector2 canvasToScreen);                                       // rigfigs.cpp

using namespace rig;

namespace study {
namespace {

float Hash(unsigned n) { n = (n << 13) ^ n; return ((n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffff) / 2147483647.0f; }
Color Mix(Color a, Color b, float k) { return {(unsigned char)(a.r + (b.r - a.r) * k), (unsigned char)(a.g + (b.g - a.g) * k), (unsigned char)(a.b + (b.b - a.b) * k), (unsigned char)(a.a + (b.a - a.a) * k)}; }
Vector2 V(float x, float y) { return {x, y}; }
Vector2 Off2(Vector2 p, float dx, float dy) { return {p.x + dx, p.y + dy}; }

// a figure drawn on the rig's canvas and inked, with its feet at `feet`
template <class F> void Figure(Vector2 feet, float facing, F&& draw) {
    Vector2 ff = FigureFeet();
    SetWorldOffset({feet.x - ff.x, feet.y - ff.y});
    SetFigureFacing(facing);
    BeginFigure();
    draw(ff);
    EndFigure(feet);
    SetFigureFacing(0);
    RigRunAfterInk({feet.x - ff.x, feet.y - ff.y});
}
// a chain that hangs from `anchor`, (re)hung the first time or after a jump
void Hang(Instance& in, size_t idx, Vector2 anchor, int n, float seg, Vector2 rest, Color col, float w0, float w1, Mat m = CLOTH) {
    if (in.chains.size() <= idx) in.chains.resize(idx + 1);
    Chain& c = in.chains[idx];
    float d = c.p.empty() ? 1e9f : fabsf(c.p[0].x - anchor.x) + fabsf(c.p[0].y - anchor.y);
    if (!c.live || d > 120) { c = Chain{}; c.Init(anchor, n, seg, rest); c.col = col; c.width0 = w0; c.width1 = w1; c.mat = m; c.stiff = 0.25f; c.grav = 300; c.damp = 0.9f; }
    c.Step(anchor, rest, in.dt, Current());
}
void Eyes(const Instance& in, Vector2 c, float sp, float r, float s, Color white, Color pupil) {
    for (int k = 0; k < 2; k++) {
        Vector2 e{c.x + (k ? sp : 0) * s, c.y};
        if (in.face.Closed()) { DrawLineEx({e.x - r * s, e.y}, {e.x + r * s, e.y}, 1.2f * s, Color{30, 20, 18, 255}); continue; }
        DrawCircleV(e, r * s, white);
        DrawCircleV({e.x + in.face.look.x * 0.4f * r * s, e.y + in.face.look.y * 0.3f * r * s}, r * 0.55f * s, pupil);
    }
}

// ---------------------------------------------------------------------------- shared set dressing
void Rivets(float x0, float x1, float y, float gap, Color c) { for (float x = x0; x <= x1; x += gap) { DrawCircleV({x, y}, 2.2f, Tone(c, -0.4f)); DrawCircleV({x - 0.5f, y - 0.5f}, 1.3f, Tone(c, 0.3f)); } }
void Books(Rectangle shelf, unsigned seed) {   // a row of spines, fixed per seed
    float x = shelf.x + 3;
    int i = 0;
    while (x < shelf.x + shelf.width - 8) {
        float w = 9 + 12 * Hash(seed + i * 3), h = shelf.height * (0.62f + 0.34f * Hash(seed + i * 7));
        static const Color C[7] = {{110, 40, 34, 255}, {44, 64, 90, 255}, {60, 80, 48, 255}, {120, 92, 50, 255}, {70, 46, 70, 255}, {40, 36, 32, 255}, {150, 120, 80, 255}};
        Color c = C[(int)(Hash(seed + i * 11) * 7) % 7];
        float lean = Hash(seed + i * 13) < 0.08f ? 6.0f : 0.0f;
        if (x + w > shelf.x + shelf.width - 3) break;
        Rectangle b{x, shelf.y + shelf.height - h, w, h};
        DrawTri({b.x + lean, b.y}, {b.x + b.width + lean, b.y}, {b.x + b.width, b.y + b.height}, c);
        DrawTri({b.x + lean, b.y}, {b.x + b.width, b.y + b.height}, {b.x, b.y + b.height}, c);
        DrawRectangle((int)(b.x + 2 + lean * 0.5f), (int)(b.y + h * 0.18f), (int)w - 4, 2, Tone(Pal::Brass, -0.2f));
        DrawRectangle((int)(b.x + 2), (int)(b.y + h * 0.78f), (int)w - 4, 2, Tone(Pal::Brass, -0.3f));
        DrawLineEx({b.x + b.width - 1, b.y}, {b.x + b.width - 1, b.y + b.height}, 1, Fade(BLACK, 0.5f));
        x += w + (Hash(seed + i * 17) < 0.1f ? 12 : 1);
        i++;
    }
}
void Bookcase(Rectangle r, unsigned seed) {
    Color wood{58, 36, 24, 255};
    DrawRectangleRec({r.x - 10, r.y - 12, r.width + 20, r.height + 12}, Tone(wood, -0.25f));
    DrawTiled(Tex::Wood, r, 1.0f, Tone(wood, -0.45f));
    int shelves = (int)(r.height / 88);
    for (int k = 0; k < shelves; k++) {
        float y = r.y + k * r.height / shelves;
        Books({r.x + 6, y + 6, r.width - 12, r.height / shelves - 16}, seed + k * 101);
        DrawRectangle((int)r.x - 6, (int)(y + r.height / shelves - 12), (int)r.width + 12, 12, wood);
        DrawRectangle((int)r.x - 6, (int)(y + r.height / shelves - 12), (int)r.width + 12, 3, Tone(wood, 0.25f));
    }
    DrawRectangle((int)r.x - 12, (int)r.y - 20, (int)r.width + 24, 14, Tone(wood, 0.1f));   // the cornice
    DrawRectangle((int)r.x - 10, (int)r.y, 8, (int)r.height, Tone(wood, -0.05f));
    DrawRectangle((int)(r.x + r.width + 2), (int)r.y, 8, (int)r.height, Tone(wood, -0.35f));
}

// ============================================================================ SCENE B: THE CAPTAIN'S STUDY
// A Victorian steampunk study around a big fireplace: the fire is the key light, the rainy window the fill, the green
// desk lamp the rim. The cat sleeps in the wingback. Rain only falls in the window when a rain layer plays, and the
// fire follows the Fire layer's size (embers when it is off).
// one tongue of flame: a teardrop (rounded at the base, widest low, drawn to a flicking tip) with a wave running up it
void Flame(Vector2 base, float w, float h, float t, float seed, Color c) {
    const int N = 14;
    Vector2 L[N + 1], R[N + 1];
    for (int i = 0; i <= N; i++) {
        float u = i / (float)N;
        float prof = powf(1 - u, 1.25f) * (0.72f + 0.28f * sinf(std::min(u / 0.22f, 1.0f) * PI * 0.5f));
        float sway = (sinf(t * 2.1f - u * 5.5f + seed) * 5 + sinf(t * 3.7f - u * 9 + seed * 2.3f) * 2) * powf(u, 1.4f);
        float half = w * 0.5f * prof * (0.93f + 0.07f * sinf(t * 2.9f + seed * 3 + u * 4));
        L[i] = {base.x - half + sway, base.y - h * u};
        R[i] = {base.x + half + sway, base.y - h * u};
    }
    for (int i = 0; i < N; i++) { DrawTri(L[i], R[i], R[i + 1], c); DrawTri(L[i], R[i + 1], L[i + 1], c); }
}
void Fire(Vector2 hearth, float size, float ft) {   // tongues rising and falling slowly out of phase, outer red to inner gold
    const float xs[7] = {-62, -38, -14, 8, 30, 52, 70}, ws[7] = {44, 52, 60, 64, 56, 46, 36};
    float h = 70 + 120 * size, wk = 0.6f + 0.4f * size;
    for (int layer = 0; layer < 3; layer++) {
        static const Color C[3] = {{176, 52, 22, 225}, {226, 116, 40, 232}, {252, 204, 110, 240}};
        float shrink = 1 - layer * 0.3f;
        for (int k = 0; k < 7; k++) {
            if (layer == 2 && (k == 0 || k == 6)) continue;
            float hk = h * (0.55f + 0.45f * (0.5f + 0.5f * sinf(ft * 1.1f + k * 2.1f))) * (k == 0 || k == 6 ? 0.6f : 1.0f);
            Flame({hearth.x + xs[k] * shrink, hearth.y - 14}, ws[k] * wk * shrink, hk * shrink, ft, k * 1.7f + layer, C[layer]);
        }
    }
    Flame({hearth.x, hearth.y - 16}, 40 * wk, h * 0.3f, ft, 11, Color{255, 240, 200, 245});
}void CaptainsStudy(const SceneInputs& in) {
    float t = in.t, ft = in.realT;   // the fire keeps real time, even under Reduce Motion
    SceneLight sl = SalonLight();
    sl.keyDir = {0.0f, -1.0f}; sl.key = {255, 180, 110, 255}; sl.fill = {90, 110, 150, 255}; sl.rim = {170, 210, 150, 255}; sl.fog = {26, 18, 16, 255};
    SetSceneLight(sl);
    SetInkLook(&SalonPalette(), 0.2f, 1234);
    SetPost(0.5f, 0.02f, 0.25f);
    // the walls: deep green paper above oak wainscot, the floor below
    DrawVGradient({0, 0, (float)SCREEN_W, 400}, Color{30, 44, 36, 255}, Color{22, 32, 26, 255});
    for (int x = 0; x < SCREEN_W; x += 64) for (int y = 20; y < 380; y += 64) { float o = (y / 64) % 2 ? 32.0f : 0.0f; DrawCircleLinesV({x + o + 16, (float)y}, 9, Fade(Color{70, 90, 60, 255}, 0.18f)); }
    DrawTiled(Tex::Wood, {0, 380, (float)SCREEN_W, 180}, 1.0f, Color{70, 44, 28, 255});
    for (int x = 20; x < SCREEN_W; x += 120) DrawRectangleLinesEx({(float)x, 396, 100, 150}, 2, Fade(Color{30, 18, 10, 255}, 0.6f));
    DrawRectangle(0, 378, SCREEN_W, 6, Color{96, 64, 40, 255});
    DrawTiled(Tex::Wood, {0, 560, (float)SCREEN_W, 160}, 0.8f, Color{56, 36, 24, 255});
    for (int y = 560; y < SCREEN_H; y += 22) DrawLine(0, y, SCREEN_W, y, Fade(BLACK, 0.3f));
    // the rug: the lower third, broad and quiet
    DrawEllipse(640, 660, 560, 90, Color{80, 30, 28, 255});
    DrawEllipseLines(640, 660, 540, 84, Fade(Pal::Brass, 0.35f));
    DrawEllipseLines(640, 660, 500, 74, Fade(Color{40, 60, 70, 255}, 0.5f));

    // bookshelves at both sides
    Bookcase({40, 40, 190, 520}, 11);
    Bookcase({1050, 40, 190, 520}, 29);

    // the tall window, night and rain
    Rectangle win{258, 60, 158, 380};
    DrawRectangleRec({win.x - 14, win.y - 14, win.width + 28, win.height + 28}, Color{54, 34, 22, 255});
    DrawVGradient(win, Color{14, 22, 40, 255}, Color{24, 36, 56, 255});
    DrawCircleV({win.x + 110, win.y + 70}, 16, Fade(Color{200, 210, 220, 255}, in.rain > 0.1f ? 0.08f : 0.35f));   // the moon, behind cloud when it rains
    if (in.rain < 0.05f) for (int k = 0; k < 14; k++) DrawCircleV({win.x + 10 + Hash(k) * 138, win.y + 10 + Hash(k + 40) * 200}, 1, Fade(WHITE, 0.25f + 0.1f * sinf(t * 0.4f + k)));
    DrawRectangle((int)(win.x + win.width / 2 - 3), (int)win.y, 6, (int)win.height, Color{54, 34, 22, 255});     // the mullions
    for (int k = 1; k < 4; k++) DrawRectangle((int)win.x, (int)(win.y + k * win.height / 4 - 3), (int)win.width, 6, Color{54, 34, 22, 255});
    DrawRectangle((int)win.x - 20, (int)(win.y + win.height + 10), (int)win.width + 40, 12, Color{80, 54, 34, 255});   // the sill

    // the fireplace: a brass-and-copper surround with a gauge and pipes into the wall, a stone firebox
    Color brass = Pal::Brass, copper = Pal::Copper, stone{70, 64, 60, 255};
    DrawRectangle(452, 170, 376, 392, Tone(stone, -0.2f));
    DrawTiled(Tex::Rock, {470, 190, 340, 360}, 0.8f, stone);
    DrawRectangle(430, 150, 420, 26, Tone(copper, -0.1f));                  // the mantel shelf
    DrawRectangle(430, 150, 420, 5, Tone(copper, 0.3f));
    DrawRectangle(446, 176, 20, 386, brass); DrawRectangle(814, 176, 20, 386, brass);   // brass pillars
    Rivets(456, 456, 200, 40, brass);
    for (float y = 196; y < 560; y += 36) { DrawCircleV({456, y}, 2.4f, Tone(brass, -0.4f)); DrawCircleV({824, y}, 2.4f, Tone(brass, -0.4f)); }
    Rectangle box{520, 300, 240, 250};
    DrawRectangleRounded({box.x - 14, box.y - 18, box.width + 28, box.height + 18}, 0.3f, 8, Tone(copper, -0.2f));   // an arched copper frame
    DrawRectangleRounded(box, 0.3f, 8, Color{16, 10, 8, 255});
    DrawRectangle((int)box.x, (int)(box.y + box.height - 40), (int)box.width, 40, Color{24, 16, 12, 255});
    for (int k = 0; k < 2; k++) { DrawRectangle(430 + k * 400, 250, 30, 14, copper); DrawRectangle(k ? 850 : 380, 252, 50, 10, Tone(copper, -0.2f)); }   // pipes into the wall
    DrawRectangle(380, 180, 10, 90, Tone(copper, -0.25f)); DrawRectangle(890, 180, 10, 90, Tone(copper, -0.25f));
    DrawGauge({840, 300}, 26, 0.45f + 0.03f * sinf(t * 0.2f), Color{230, 220, 196, 255});                    // the pressure gauge
    // the fire: logs, then flame ribbons back to front; with the Fire layer off, only embers
    Vector2 hearth{640, 540};
    for (int k = 0; k < 2; k++) ShadeLimb({hearth.x - 90 + k * 20.0f, hearth.y - 8 - k * 14.0f}, {hearth.x + 80 - k * 20.0f, hearth.y - 14 - k * 10.0f}, 13, 11, Color{70, 44, 30, 255});
    float size = in.fire;
    if (size > 0.02f) Fire(hearth, size, ft);
    for (int k = 0; k < 16; k++) { float x = hearth.x - 100 + k * 13; DrawCircleV({x, hearth.y - 2}, 5, Mix(Color{120, 30, 10, 255}, Color{255, 140, 50, 255}, 0.5f + 0.5f * sinf(ft * 0.7f + k * 1.3f))); }
    // the mantel: a pendulum clock, a model of the Nautilus, a globe
    Rectangle ck{530, 60, 64, 92};
    DrawRectangleRounded(ck, 0.25f, 6, Color{60, 36, 22, 255});
    DrawCircleV({ck.x + 32, ck.y + 26}, 20, Color{226, 216, 190, 255});
    DrawRing({ck.x + 32, ck.y + 26}, 19, 22, 0, 360, 32, brass);
    for (int k = 0; k < 12; k++) { float a = k * PI / 6; DrawCircleV({ck.x + 32 + cosf(a) * 15, ck.y + 26 + sinf(a) * 15}, 1.1f, Pal::Ink); }
    DrawLineEx({ck.x + 32, ck.y + 26}, {ck.x + 32 + cosf(-1.2f) * 11, ck.y + 26 + sinf(-1.2f) * 11}, 2, Pal::Ink);
    DrawLineEx({ck.x + 32, ck.y + 26}, {ck.x + 32 + cosf(0.4f) * 14, ck.y + 26 + sinf(0.4f) * 14}, 1.4f, Pal::Ink);
    DrawRectangle((int)ck.x + 16, (int)ck.y + 50, 32, 38, Color{30, 20, 14, 255});
    float pa = 0.22f * sinf(t * 3.14159f);   // a 2-second swing: 0.5 Hz, the calm limit
    Vector2 piv{ck.x + 32, ck.y + 52}, bob{piv.x + sinf(pa) * 30, piv.y + cosf(pa) * 30};
    DrawLineEx(piv, bob, 1.5f, brass); DrawCircleV(bob, 5, brass);
    // the Nautilus on its stand
    DrawRectangle(630, 140, 60, 10, Color{60, 36, 22, 255});
    DrawLineEx({645, 140}, {648, 128}, 2, brass); DrawLineEx({675, 140}, {672, 128}, 2, brass);
    ShadeLimb({612, 122}, {708, 122}, 7, 9, Color{100, 106, 110, 255});
    DrawTri({708, 122}, {716, 116}, {716, 128}, Color{90, 96, 100, 255});
    DrawRectangle(648, 108, 14, 10, Color{96, 100, 104, 255});
    for (int k = 0; k < 4; k++) DrawCircleV({630 + k * 18.0f, 121}, 1.6f, Color{255, 220, 150, 255});
    // the globe, turning very slowly
    Vector2 gc{770, 108};
    DrawLineEx({gc.x, gc.y + 26}, {gc.x, gc.y + 42}, 3, brass); DrawRectangle((int)gc.x - 18, (int)gc.y + 40, 36, 8, Color{60, 36, 22, 255});
    DrawCircleV(gc, 25, Color{70, 110, 120, 255});
    for (int k = 0; k < 4; k++) { float ph = fmodf(t * 0.03f + k * 0.25f, 1.0f); float x = gc.x - 25 + ph * 50; float w = 25 * sinf(ph * PI); DrawEllipse((int)x, (int)(gc.y - 6 + k * 5), w * 0.3f, 6, Fade(Color{140, 120, 70, 255}, 0.7f * sinf(ph * PI))); }
    DrawRing(gc, 25, 28, -30, 210, 24, brass);
    // the writing desk and its green-shaded lamp, by the right bookcase
    DrawRectangle(1040, 440, 220, 16, Color{70, 44, 28, 255});
    DrawRectangle(1052, 456, 14, 104, Color{50, 32, 20, 255}); DrawRectangle(1232, 456, 14, 104, Color{50, 32, 20, 255});
    DrawRectangle(1080, 430, 70, 10, Color{220, 210, 186, 255});                                   // papers
    DrawLineEx({1160, 440}, {1160, 400}, 3, brass);
    DrawTri({1128, 400}, {1192, 400}, {1160, 380}, Color{40, 110, 60, 255});                        // the shade
    DrawEllipse(1160, 400, 32, 5, Color{30, 90, 50, 255});
    // the wingback armchair, turned toward the fire, and the cat asleep in it
    Color leather{150, 58, 44, 255};
    auto Q = [](Vector2 a, Vector2 b, Vector2 c2, Vector2 d, Color col) { DrawTri(a, b, c2, col); DrawTri(a, c2, d, col); };
    Q({884, 292}, {968, 280}, {976, 452}, {884, 460}, Tone(leather, -0.05f));             // the tall back, buttoned
    for (int r = 0; r < 4; r++) for (int k = 0; k < 3; k++) DrawCircleV({900 + k * 26.0f + (r % 2) * 13, 312 + r * 34.0f}, 2.2f, Tone(leather, -0.45f));
    DrawLineEx({884, 292}, {968, 280}, 3, Tone(leather, 0.25f));                          // the back's rolled top
    Q({968, 280}, {998, 300}, {1000, 450}, {976, 452}, Tone(leather, -0.35f));            // the wing
    Q({862, 436}, {996, 436}, {996, 496}, {862, 496}, leather);                           // the seat's front
    DrawLineEx({862, 438}, {996, 438}, 3, Tone(leather, 0.3f));                           // the cushion's edge, catching the fire
    Q({852, 404}, {884, 404}, {884, 506}, {852, 506}, Tone(leather, -0.15f));             // the arm, toward the fire
    DrawCircleV({868, 404}, 16, Tone(leather, -0.1f));                                    // its scroll
    DrawCircleV({868, 404}, 9, Tone(leather, -0.35f));
    for (int k = 0; k < 8; k++) DrawCircleV({870 + k * 16.0f, 494}, 2, brass);
    DrawRectangle(866, 496, 10, 44, Color{40, 24, 16, 255}); DrawRectangle(980, 496, 10, 44, Color{40, 24, 16, 255});
    StudyDrawCat({928, 448}, 2.0f, false, in.realT, true);

    // the light: the fire is the key, the window the fill, the lamp the rim
    float flick = size > 0.02f ? 0.03f * sinf(ft * 5.1f) + 0.02f * sinf(ft * 8.3f + 1) : 0;
    LightsBegin(Color{84, 76, 76, 255});
    AddLight({640, 470}, 520 + 380 * size, Color{255, 170, 90, 255}, (0.4f + 0.65f * size) * (1 + flick));
    AddLight({337, 250}, 360, Color{120, 150, 210, 255}, 0.55f);
    AddLight({1160, 410}, 240, Color{190, 230, 160, 255}, 0.75f);
    AddLight({640, 120}, 300, Color{255, 200, 140, 255}, 0.2f + 0.3f * size);
    LightsEnd();
    InkPass(0.85f, 0.8f);
    // after the ink: the bright little things (embers, rain on the glass, the lamp's glow)
    if (size > 0.02f) for (int k = 0; k < 10; k++) {
        float life = fmodf(ft * 0.18f + Hash(k) , 1.0f);
        Vector2 p{hearth.x - 60 + Hash(k + 7) * 120 + sinf(ft * 0.6f + k) * 10, hearth.y - 30 - life * 220 * (0.5f + size)};
        DrawCircleV(p, 1.4f, Fade(Color{255, 190, 90, 255}, (1 - life) * 0.8f * size));
    }
    if (in.rain > 0.02f) {
        BeginScissorMode((int)(win.x * 2), (int)(win.y * 2), (int)(win.width * 2), (int)(win.height * 2));   // (the scene is supersampled 2x)
        int streaks = (int)(10 + 60 * in.rain);
        for (int k = 0; k < streaks; k++) {
            float speed = 180 + 120 * Hash(k * 3), x = win.x + Hash(k) * win.width, y = win.y + fmodf(in.realT * speed + Hash(k + 9) * 400, win.height + 30) - 30;
            DrawLineEx({x, y}, {x - 2, y + 14}, 1, Fade(Color{170, 190, 220, 255}, 0.25f + 0.25f * in.rain));
        }
        for (int k = 0; k < (int)(6 + 14 * in.rain); k++) {   // drops running slowly down the glass
            float x = win.x + 8 + Hash(k + 50) * (win.width - 16), y = win.y + fmodf(in.realT * (8 + 10 * Hash(k + 70)) + Hash(k + 60) * win.height, win.height);
            DrawCircleV({x, y}, 1.8f, Fade(Color{200, 215, 235, 255}, 0.45f));
            DrawLineEx({x, y - 10}, {x, y}, 1, Fade(Color{200, 215, 235, 255}, 0.15f));
        }
        EndScissorMode();
    }
    Glow({1160, 402}, 50, Color{160, 230, 150, 50});
    if (size > 0.02f) Glow({640, 480}, 160 * (0.5f + size), Color{255, 140, 60, (unsigned char)(40 + 30 * size)});
}

// ============================================================================ SCENE A: THE LAMPLIGHT LOUNGE
// A smoky undersea tavern in the lower decks. The amber stage lamp is the key, blue porthole light the fill, haze in
// the beam. The house band plays in time with the music engine's beat clock; between songs (or with the band's music
// off) they rest: a sip, tuning, a glance. The bartender is an octopus; the patrons sit in near-silhouette.
struct Beat { float pulse, phase, bar, barPh; int beatInBar; };
Beat BeatOf(const SceneInputs& in) {
    Beat b{};
    if (!in.bandPlaying) return b;
    double beat = in.beat;
    b.phase = (float)(beat - floor(beat));
    b.pulse = expf(-b.phase * 6);
    b.beatInBar = (int)fmod(beat, in.beatsPerBar);
    b.bar = (float)(beat / in.beatsPerBar);
    b.barPh = b.bar - floorf(b.bar);
    return b;
}

// the drowned sailor on the squeezebox: kelp in his beard (a chain), bellows open and close on each phrase
void Sailor(Vector2 feet, const SceneInputs& in, const Beat& bt) {
    Figure(feet, 1, [&](Vector2 ft) {
        Instance& I = Get(90001); Tick(I, in.t);
        float s = 0.95f, f = 1, t = in.t;
        Build b; b.thigh = 38; b.shin = 36; b.upper = 25; b.fore = 24; b.spine = 25; b.chest = 23; b.head = 12; b.shoulderW = 16; b.stanceF = 12; b.stanceB = -10;
        RPose P;
        P[C_HIPY] = 4; P[C_LEAN] = 0.1f; P[C_HEAD] = 0.1f;
        float open = in.bandPlaying ? 0.5f + 0.5f * sinf(bt.bar * PI) : 0.1f;   // a phrase is two bars: out, then in
        P[C_HFX] = 22 + 10 * open; P[C_HFY] = 32; P[C_HBX] = 6; P[C_HBY] = 34;
        P[C_HEAD] += in.bandPlaying ? 0.12f * sinf(bt.bar * PI * 2) : 0.05f * sinf(t * 0.4f);   // his head sways with the tune
        P[C_HIPY] += 1.5f * sinf(t * 1.4f);
        Solved S = SolveHumanoid(b, P, ft, s, f);
        Color coat{62, 78, 100, 255}, skin{160, 180, 168, 255}, cap{90, 30, 30, 255}, kelp{50, 90, 50, 255};
        Hang(I, 0, S.Head(4, 12), 6, 5 * s, {0.1f, 1}, kelp, 3, 1.4f);
        Hang(I, 1, S.Head(8, 11), 5, 5 * s, {0.2f, 1}, Tone(kelp, 0.2f), 2.4f, 1);
        Parts parts;
        parts.Add(-1, [&] { MLimb(S.p[HIP_B], S.p[KN_B], 9 * s, 7.5f * s, Tone(coat, -0.2f), CLOTH); MLimb(S.p[KN_B], S.p[AN_B], 7.5f * s, 6 * s, Tone(coat, -0.2f), CLOTH); MLimb(S.p[AN_B], Off2(S.p[AN_B], 10 * s, 2), 5 * s, 5 * s, Color{30, 24, 20, 255}, WET); });
        parts.Add(-0.9f, [&] { MLimb(S.p[HIP_F], S.p[KN_F], 9 * s, 7.5f * s, coat, CLOTH); MLimb(S.p[KN_F], S.p[AN_F], 7.5f * s, 6 * s, coat, CLOTH); MLimb(S.p[AN_F], Off2(S.p[AN_F], 10 * s, 2), 5 * s, 5 * s, Color{30, 24, 20, 255}, WET); });
        parts.Add(-0.5f, [&] { MLimb(S.p[SH_B], S.p[EL_B], 7 * s, 6 * s, Tone(coat, -0.15f), CLOTH); MLimb(S.p[EL_B], S.p[WR_B], 6 * s, 5 * s, Tone(coat, -0.15f), CLOTH); });
        parts.Add(0, [&] {
            MQuad(S.Chest(-17, -22), S.Chest(18, -22), S.Hips(14, 2), S.Hips(-14, 2), coat, CLOTH);   // the peacoat
            for (int k = 0; k < 3; k++) { MBall(S.Chest(9, -12 + k * 9.0f), 1.6f * s, Pal::Brass, METAL); MBall(S.Chest(15, -12 + k * 9.0f), 1.6f * s, Pal::Brass, METAL); }
            for (int k = 0; k < 4; k++) DrawCircleV(S.Chest(-10 + k * 7.0f, -4 + (k % 2) * 8.0f), 2 * s, Fade(Color{120, 150, 110, 255}, 0.5f));   // barnacles on the coat
        });
        parts.Add(0.4f, [&] {
            MBall(S.p[HEAD], 12.5f * s, skin, SKIN);
            MLimb(S.Head(8, -2), S.Head(12, 4), 1.8f * s, 2.6f * s, Tone(skin, -0.1f), SKIN);
            Eyes(I, S.Head(3, -2), 6, 1.8f, s, Color{210, 220, 200, 255}, Color{30, 40, 40, 255});
            MLimb(S.Head(-2, 8), S.Head(9, 12), 6 * s, 5 * s, Color{90, 100, 90, 255}, CLOTH);     // the beard
            I.chains[0].Draw(s); I.chains[1].Draw(s);
            MQuad(S.Head(-13, -12), S.Head(13, -13), S.Head(12, -4), S.Head(-12, -4), cap, CLOTH);   // a knit cap
            MBall(S.Head(0, -15), 3.5f * s, Tone(cap, 0.2f), CLOTH);
        });
        parts.Add(0.8f, [&] {   // the squeezebox between the hands: two carved end boxes and the bellows' folds
            Vector2 a = S.p[WR_B], c = S.p[WR_F];
            MQuad({a.x - 6 * s, a.y - 16 * s}, {a.x + 4 * s, a.y - 16 * s}, {a.x + 4 * s, a.y + 14 * s}, {a.x - 6 * s, a.y + 14 * s}, Color{110, 40, 30, 255}, CLOTH);
            MQuad({c.x - 4 * s, c.y - 16 * s}, {c.x + 6 * s, c.y - 16 * s}, {c.x + 6 * s, c.y + 14 * s}, {c.x - 4 * s, c.y + 14 * s}, Color{110, 40, 30, 255}, CLOTH);
            int folds = 6;
            for (int k = 0; k < folds; k++) {
                float u0 = k / (float)folds, u1 = (k + 1) / (float)folds;
                float x0 = a.x + 4 * s + (c.x - a.x - 8 * s) * u0, x1 = a.x + 4 * s + (c.x - a.x - 8 * s) * u1;
                MQuad({x0, a.y - 14 * s}, {x1, a.y - 13 * s}, {x1, a.y + 12 * s}, {x0, a.y + 13 * s}, k % 2 ? Color{200, 190, 160, 255} : Color{150, 140, 120, 255}, CLOTH);
            }
            MBall(a, 5 * s, skin, SKIN); MBall(c, 5 * s, skin, SKIN);
        });
        parts.Draw();
    });
}
// the merman on the curled brass horn: a long fin crest (chains), leans back on long notes, taps a tail fin on the beat
void Merman(Vector2 feet, const SceneInputs& in, const Beat& bt) {
    Figure(feet, -1, [&](Vector2 ft) {
        Instance& I = Get(90002); Tick(I, in.t);
        float s = 0.95f, f = -1, t = in.t;
        Build b; b.thigh = 30; b.shin = 30; b.upper = 25; b.fore = 24; b.spine = 28; b.chest = 24; b.head = 12; b.shoulderW = 17; b.stanceF = 4; b.stanceB = -4;
        RPose P;
        bool longNote = in.bandPlaying && fmodf(bt.bar, 4) >= 1 && fmodf(bt.bar, 4) < 3;
        P[C_LEAN] = longNote ? -0.16f : 0.02f + 0.02f * sinf(t * 0.5f);
        P[C_HEAD] = in.bandPlaying ? -0.25f : 0.1f;
        P[C_HFX] = in.bandPlaying ? 14 : 8; P[C_HFY] = in.bandPlaying ? -6 : 34; P[C_HBX] = in.bandPlaying ? 16 : -4; P[C_HBY] = in.bandPlaying ? 2 : 36;
        P[C_HIPY] = 16;
        Solved S = SolveHumanoid(b, P, ft, s, f);
        Color scale{60, 120, 110, 255}, skin{120, 160, 150, 255}, fin{170, 90, 60, 255};
        Hang(I, 0, S.Head(-4, -12), 6, 6 * s, {-0.8f * f, 0.6f}, fin, 4, 1.5f);
        Hang(I, 1, S.Head(-9, -6), 5, 6 * s, {-0.9f * f, 0.5f}, Tone(fin, -0.2f), 3.4f, 1.2f);
        float tap = in.bandPlaying ? bt.pulse * 6 : 0;
        Parts parts;
        parts.Add(-1, [&] {   // the tail: coiled on the stage boards, its fin tapping on the beat
            Vector2 h = S.p[HIPS];
            Vector2 p1{h.x - 6 * f * s, h.y + 26 * s}, p2{h.x + 16 * s * f, h.y + 44 * s}, p3{h.x + 38 * f * s, h.y + 40 * s - tap};
            MLimb(h, p1, 14 * s, 12 * s, scale, WET); MLimb(p1, p2, 12 * s, 9 * s, scale, WET); MLimb(p2, p3, 9 * s, 5 * s, Tone(scale, -0.1f), WET);
            DrawTri(p3, {p3.x + 14 * f * s, p3.y - 12 * s}, {p3.x + 16 * f * s, p3.y + 6 * s}, fin);
        });
        parts.Add(-0.5f, [&] { MLimb(S.p[SH_B], S.p[EL_B], 7 * s, 6 * s, Tone(skin, -0.15f), SKIN); MLimb(S.p[EL_B], S.p[WR_B], 6 * s, 5 * s, Tone(skin, -0.15f), SKIN); });
        parts.Add(0, [&] {
            MQuad(S.Chest(-17, -22), S.Chest(17, -22), S.Hips(13, 2), S.Hips(-13, 2), skin, SKIN);
            MQuad(S.Hips(-14, -6), S.Hips(14, -6), S.Hips(13, 4), S.Hips(-13, 4), scale, WET);
            I.chains[1].Draw(s);
        });
        parts.Add(0.4f, [&] {
            MBall(S.p[HEAD], 12 * s, skin, SKIN);
            Eyes(I, S.Head(3, -2), 6, 1.9f, s, Color{230, 230, 200, 255}, Color{20, 60, 60, 255});
            I.chains[0].Draw(s);
        });
        parts.Add(0.8f, [&] {   // the horn: a coil of brass to the mouth, a flared bell
            MLimb(S.p[SH_F], S.p[EL_F], 7 * s, 6 * s, skin, SKIN); MLimb(S.p[EL_F], S.p[WR_F], 6 * s, 5 * s, skin, SKIN);
            Vector2 c = in.bandPlaying ? S.Head(14, 6) : Off2(S.p[WR_F], 0, 10 * s);
            DrawRing(c, 8 * s, 11 * s, 0, 360, 24, Pal::Brass);
            Vector2 bell{c.x + 18 * f * s, c.y - (in.bandPlaying ? 14 : -6) * s};
            MLimb(c, bell, 3 * s, 9 * s, Pal::Brass, METAL);
            MBall(S.p[WR_F], 4.5f * s, skin, SKIN); MBall(S.p[WR_B], 4.5f * s, skin, SKIN);
        });
        parts.Draw();
    });
}

// the hermit crab in a diving-bell shell, on the upright bass: two claws pluck, eyestalks bob on the downbeat
void Crab(Vector2 feet, const SceneInputs& in, const Beat& bt) {
    Figure(feet, -1, [&](Vector2 ft) {
        Instance& I = Get(90003); Tick(I, in.t);
        float s = 1.0f, t = in.t;
        Color shell = Pal::Brass, body{190, 80, 50, 255};
        float bob = in.bandPlaying ? (bt.beatInBar == 0 ? bt.pulse * 5 : bt.pulse * 1.5f) : 1.5f * sinf(t * 0.5f);
        Vector2 c{ft.x + 10, ft.y - 50};
        Hang(I, 0, {c.x - 20, c.y - 44 - bob}, 5, 6, {-0.6f, -0.4f}, Tone(body, 0.2f), 1.6f, 0.6f);
        Hang(I, 1, {c.x - 8, c.y - 46 - bob}, 5, 6, {-0.3f, -0.6f}, Tone(body, 0.2f), 1.6f, 0.6f);
        // the bass, taller than him
        Vector2 bb{c.x - 58, ft.y - 60};
        ShadeBall({bb.x, bb.y}, 30, Color{120, 64, 30, 255});
        ShadeBall({bb.x, bb.y - 42}, 24, Color{130, 70, 34, 255});
        ShadeLimb({bb.x, bb.y - 60}, {bb.x + 4, bb.y - 170}, 5, 4, Color{40, 26, 18, 255});
        DrawCircleV({bb.x + 5, bb.y - 176}, 6, Color{60, 36, 22, 255});
        for (int k = -1; k <= 1; k++) DrawLineEx({bb.x + k * 3.0f, bb.y + 10}, {bb.x + 3 + k * 1.2f, bb.y - 168}, 1, Color{220, 210, 190, 255});
        // the shell: a riveted diving bell with a porthole
        ShadeBall({c.x + 22, c.y - 6}, 40, shell);
        DrawRing({c.x + 30, c.y - 14}, 11, 15, 0, 360, 24, Tone(shell, -0.35f));
        DrawCircleV({c.x + 30, c.y - 14}, 11, Color{40, 70, 80, 255});
        Rivets(c.x - 6, c.x + 54, c.y + 26, 10, shell);
        // legs, the body, the claws on the strings
        for (int k = 0; k < 3; k++) { Vector2 hip{c.x - 6 + k * 10.0f, c.y + 22}; Vector2 knee{hip.x - 10 + k * 4.0f, hip.y + 14}; ShadeLimb(hip, knee, 4, 3, body); ShadeLimb(knee, {knee.x - 4, ft.y - 1}, 3, 2, body); }
        ShadeBall({c.x - 16, c.y + 6}, 16, body);
        float pl = in.bandPlaying ? bt.pulse : 0;
        Vector2 claw1{bb.x + 14, bb.y - 20 + pl * 6}, claw2{bb.x + 10, bb.y - 120 + 2 * sinf(t * 0.4f)};
        ShadeLimb({c.x - 24, c.y + 2}, claw1, 5, 4, body); ShadeBall(claw1, 7, Tone(body, 0.1f));
        ShadeLimb({c.x - 22, c.y - 6}, claw2, 5, 4, body); ShadeBall(claw2, 7, Tone(body, 0.1f));
        // the eyestalks and their eyes (they blink)
        for (int k = 0; k < 2; k++) {
            Vector2 base{c.x - 20 + k * 10.0f, c.y - 8}, eye{base.x - 4 + k * 4.0f, base.y - 30 - bob};
            ShadeLimb(base, eye, 2.4f, 2, body);
            DrawCircleV(eye, 4.5f, I.face.Closed() ? Tone(body, -0.2f) : Color{20, 20, 24, 255});
            if (!I.face.Closed()) DrawCircleV({eye.x - 1, eye.y - 1.5f}, 1.2f, WHITE);
        }
        I.chains[0].Draw(1); I.chains[1].Draw(1);
    });
}

// the clockwork drummer at a brushed kit: brushes swirl, a small steam puff on beats 2 and 4 (drawn by the caller)
void Drummer(Vector2 feet, const SceneInputs& in, const Beat& bt) {
    Figure(feet, -1, [&](Vector2 ft) {
        Instance& I = Get(90004); Tick(I, in.t);
        float t = in.t;
        Color brass = Pal::Brass, dark{80, 70, 60, 255};
        Vector2 hip{ft.x + 10, ft.y - 60}, chest{hip.x, hip.y - 50}, head{chest.x - 4, chest.y - 36};
        // the stool and legs
        ShadeLimb({hip.x - 10, hip.y + 4}, {hip.x - 26, ft.y - 20}, 7, 6, dark); ShadeLimb({hip.x - 26, ft.y - 20}, {hip.x - 30, ft.y}, 6, 5, dark);
        DrawRectangle((int)hip.x - 6, (int)hip.y + 6, 30, 8, Color{60, 36, 22, 255});
        // the torso: a boiler, riveted, a winding key on its back
        ShadeQuad({chest.x - 20, chest.y - 24}, {chest.x + 20, chest.y - 24}, {hip.x + 18, hip.y + 4}, {hip.x - 18, hip.y + 4}, brass);
        Rivets(chest.x - 16, chest.x + 16, chest.y - 18, 8, brass);
        float key = t * 0.8f;
        Vector2 kc{chest.x + 24, chest.y - 4};
        DrawLineEx(kc, {kc.x + cosf(key) * 10, kc.y + sinf(key) * 10}, 3, Tone(brass, -0.2f)); DrawLineEx(kc, {kc.x - cosf(key) * 10, kc.y - sinf(key) * 10}, 3, Tone(brass, -0.2f));
        Hang(I, 0, {chest.x + 16, chest.y + 10}, 6, 5, {0.2f, 1}, Color{60, 58, 56, 255}, 1.6f, 1.2f, METAL);   // a dangling chain
        I.chains[0].Draw(1);
        // the head: a box with two lens eyes behind a shutter that blinks
        ShadeQuad({head.x - 14, head.y - 16}, {head.x + 14, head.y - 16}, {head.x + 14, head.y + 14}, {head.x - 14, head.y + 14}, Tone(brass, -0.1f));
        for (int k = 0; k < 2; k++) {
            Vector2 e{head.x - 12 + k * 9.0f, head.y - 2};
            DrawCircleV(e, 4.5f, Color{30, 26, 22, 255});
            DrawCircleV(e, 3, I.face.Closed() ? Tone(brass, -0.3f) : Color{255, 200, 120, 255});
        }
        DrawRectangle((int)head.x + 2, (int)head.y - 26, 5, 12, dark);   // the steam pipe
        // the kit: a snare in front, a small cymbal
        Vector2 sn{ft.x - 50, ft.y - 44};
        ShadeQuad({sn.x - 26, sn.y}, {sn.x + 26, sn.y}, {sn.x + 26, sn.y + 20}, {sn.x - 26, sn.y + 20}, Color{140, 40, 36, 255});
        DrawEllipse((int)sn.x, (int)sn.y, 26, 6, Color{220, 214, 196, 255});
        DrawLineEx({sn.x - 20, sn.y + 20}, {sn.x - 26, ft.y}, 2, dark); DrawLineEx({sn.x + 20, sn.y + 20}, {sn.x + 26, ft.y}, 2, dark);
        Vector2 cy{ft.x - 92, ft.y - 110};
        DrawLineEx({cy.x, cy.y}, {cy.x, ft.y}, 2, dark); DrawEllipse((int)cy.x, (int)cy.y, 24, 4, Color{200, 170, 90, 255});
        // arms and brushes: swirling circles on the head of the snare in time
        float ang = in.bandPlaying ? (float)(in.beat * PI) : 0.3f, r = in.bandPlaying ? 9.0f : 0;
        Vector2 b1{sn.x - 8 + cosf(ang) * r, sn.y - 2 + sinf(ang) * r * 0.3f}, b2{sn.x + 10 - cosf(ang) * r, sn.y - 2 - sinf(ang) * r * 0.3f};
        if (!in.bandPlaying) { b1 = {sn.x - 6, sn.y + 4}; b2 = {sn.x + 14, sn.y + 4}; }
        for (Vector2 b : {b1, b2}) {
            Vector2 sh{chest.x - 16, chest.y - 18}, el{(sh.x + b.x) * 0.5f + 6, (sh.y + b.y) * 0.5f + 16};
            ShadeLimb(sh, el, 6, 5, dark); ShadeLimb(el, {b.x + 10, b.y - 12}, 5, 4, dark);
            DrawLineEx({b.x + 10, b.y - 12}, b, 2, Color{120, 90, 60, 255});
            for (int k = -2; k <= 2; k++) DrawLineEx(b, {b.x - 6 + k * 2.0f, b.y + 4}, 0.8f, Color{200, 200, 190, 255});
        }
    });
}

// the octopus behind the bar, polishing glasses with two arms at a time
void Octopus(Vector2 feet, const SceneInputs& in) {
    Figure(feet, 1, [&](Vector2 ft) {
        Instance& I = Get(90005); Tick(I, in.t);
        float t = in.t;
        Color skin{150, 70, 90, 255};
        Vector2 head{ft.x, ft.y - 120 + 2 * sinf(t * 0.5f)};
        for (int k = 0; k < 4; k++) Hang(I, k, {head.x - 22 + k * 14.0f, head.y + 30}, 6, 9, {(k - 1.5f) * 0.3f, 1}, Tone(skin, -0.15f), 7, 2.5f, WET);
        for (int k = 0; k < 4; k++) I.chains[k].Draw(1);
        // two polishing arms: a slow circle each, a glass in each
        for (int k = 0; k < 2; k++) {
            float a = t * 1.6f + k * PI;   // (0.25 Hz)
            Vector2 tip{head.x + (k ? 44 : -40) + cosf(a) * 7, head.y + 40 + sinf(a) * 4};
            Vector2 mid{(head.x + tip.x) * 0.5f, head.y + 44};
            ShadeLimb({head.x + (k ? 14 : -14), head.y + 22}, mid, 7, 6, skin); ShadeLimb(mid, tip, 6, 4, skin);
            Vector2 g{tip.x + (k ? 4 : -4), tip.y - 10};
            DrawTri({g.x - 8, g.y - 12}, {g.x + 8, g.y - 12}, {g.x, g.y + 4}, Fade(Color{200, 230, 240, 255}, 0.55f));
            DrawLineEx({g.x, g.y + 4}, {g.x, g.y + 12}, 1.5f, Fade(Color{200, 230, 240, 255}, 0.6f));
            DrawCircleV({tip.x, tip.y - 4}, 5, Color{230, 226, 214, 255});   // the cloth
        }
        ShadeBall(head, 34, skin);
        ShadeBall({head.x, head.y - 18}, 26, Tone(skin, 0.1f));
        Eyes(I, {head.x - 14, head.y + 2}, 22, 4, 1, Color{240, 220, 150, 255}, Color{20, 20, 20, 255});
        DrawRectangle((int)head.x - 30, (int)head.y - 44, 60, 6, Color{30, 26, 24, 255});   // a little bowler
        ShadeQuad({head.x - 20, head.y - 70}, {head.x + 20, head.y - 70}, {head.x + 18, head.y - 44}, {head.x - 18, head.y - 44}, Color{34, 30, 28, 255});
    });
}

// a patron at a table: near-silhouette, slow breathing, a very occasional sip or a turned page
void Patron(int i, Vector2 feet, const SceneInputs& in) {
    Figure(feet, i % 2 ? 1.0f : -1.0f, [&](Vector2 ft) {
        Instance& I = Get(90100 + i); Tick(I, in.t);
        float t = in.t + i * 7.3f, f = i % 2 ? 1.0f : -1.0f;
        float br = 1.5f * sinf(t * 1.2f + i);                           // breathing, 0.19 Hz
        float cyc = fmodf(t + i * 13, 45.0f), act = cyc < 4 ? sinf(cyc / 4 * PI) : 0;   // once in 45 s: a sip, slow
        Color c = Mix(Color{58, 52, 60, 255}, Color{96, 80, 76, 255}, Hash(i) * 0.6f);
        Vector2 hip{ft.x, ft.y - 46}, sh{hip.x + 4 * f, hip.y - 56 - br}, head{sh.x + 3 * f, sh.y - 20};
        ShadeQuad({hip.x - 18, hip.y}, {hip.x + 18, hip.y}, {sh.x + 20, sh.y}, {sh.x - 20, sh.y}, c);
        ShadeBall(head, 13, Tone(c, 0.1f));
        if (i % 3 == 0) { DrawRectangle((int)head.x - 18, (int)head.y - 10, 36, 4, Tone(c, -0.3f)); ShadeQuad({head.x - 11, head.y - 26}, {head.x + 11, head.y - 26}, {head.x + 11, head.y - 10}, {head.x - 11, head.y - 10}, Tone(c, -0.25f)); }
        Hang(I, 0, {sh.x - 6 * f, sh.y + 4}, 5, 7, {-0.4f * f, 1}, i % 2 ? Color{120, 40, 40, 255} : Color{50, 70, 90, 255}, 4, 2);   // a scarf end
        I.chains[0].Draw(1);
        if (!I.face.Closed() && i % 2) DrawCircleV({head.x + 6 * f, head.y - 2}, 1.3f, Color{210, 190, 150, 200});   // one eye catching the lamp, and blinking
        Vector2 hand{sh.x + 22 * f, sh.y + 26 - act * 22};
        ShadeLimb({sh.x + 12 * f, sh.y + 6}, hand, 6, 5, Tone(c, 0.05f));
        DrawRectangle((int)hand.x - 3, (int)hand.y - 8, 7, 10, Fade(Color{220, 180, 90, 255}, 0.7f));   // a glass of something amber
        // the table
        DrawEllipse((int)(ft.x + 40 * f), (int)(ft.y - 44), 36, 9, Color{40, 26, 18, 255});
        DrawRectangle((int)(ft.x + 38 * f), (int)(ft.y - 44), 5, 44, Color{30, 20, 14, 255});
    });
}

void Lounge(const SceneInputs& in) {
    float t = in.t, rt = in.realT;
    Beat bt = BeatOf(in);
    SceneLight sl = SalonLight();
    sl.keyDir = {0.3f, -0.95f}; sl.key = {255, 190, 110, 255}; sl.fill = {70, 110, 160, 255}; sl.rim = {120, 170, 210, 255}; sl.fog = {20, 22, 30, 255};
    SetSceneLight(sl);
    SetInkLook(&SalonPalette(), 0.2f, 777);
    SetPost(0.55f, 0.02f, 0.25f);
    // the back: a curved riveted wall with three portholes onto the deep
    DrawVGradient({0, 0, (float)SCREEN_W, 470}, Color{40, 32, 30, 255}, Color{28, 22, 22, 255});
    DrawTiled(Tex::Metal, {0, 30, (float)SCREEN_W, 380}, 0.9f, Color{80, 64, 52, 255});
    for (int k = 0; k < 6; k++) DrawRectangleGradientH(k * 230 - 10, 30, 20, 380, Color{30, 24, 20, 255}, Color{90, 72, 56, 255});
    Rivets(0, SCREEN_W, 40, 22, Pal::Brass); Rivets(0, SCREEN_W, 400, 22, Pal::Brass);
    for (int k = 0; k < 3; k++) {
        Vector2 c{470.0f + k * 250, 190};
        DrawCircleV(c, 66, Color{12, 40, 60, 255});
        for (int f = 0; f < 3; f++) {   // slow fish crossing, fading in and out at the rim (nothing appears suddenly)
            float ph = fmodf(t * (0.012f + 0.004f * f) + Hash(k * 5 + f), 1.0f);
            float x = c.x - 80 + ph * 160, y = c.y - 30 + f * 26 + 5 * sinf(t * 0.3f + f);
            float a = std::clamp((60 - fabsf(x - c.x)) / 30, 0.0f, 1.0f) * 0.55f;
            DrawEllipse((int)x, (int)y, 10 - f * 2, 4, Fade(Color{20, 70, 90, 255}, a));
            DrawTri({x - 10, y}, {x - 16, y - 4}, {x - 16, y + 4}, Fade(Color{20, 70, 90, 255}, a));
        }
        DrawRing(c, 64, 80, 0, 360, 48, Tone(Pal::Brass, -0.1f));
        for (int b = 0; b < 12; b++) { float a = b * PI / 6; DrawCircleV({c.x + cosf(a) * 72, c.y + sinf(a) * 72}, 3, Tone(Pal::Brass, -0.4f)); }
    }
    // the bar along the left: shelves of bottles, hanging glass vessels, copper taps on the counter
    Color wood{70, 40, 26, 255};
    DrawRectangle(0, 110, 360, 250, Tone(wood, -0.4f));
    for (int s = 0; s < 3; s++) { DrawRectangle(10, 180 + s * 70, 340, 8, wood); for (int k = 0; k < 12; k++) { float x = 22 + k * 27; Color bc = Mix(Color{50, 90, 60, 255}, Color{140, 60, 40, 255}, Hash(s * 20 + k)); DrawRectangle((int)x, 180 + s * 70 - 34, 12, 34, Fade(bc, 0.85f)); DrawRectangle((int)x + 3, 180 + s * 70 - 44, 6, 10, Fade(bc, 0.85f)); } }
    for (int k = 0; k < 5; k++) {   // glass vessels hung from a pipe, swaying very slowly
        float x = 60 + k * 70, sw = 3 * sinf(t * 0.4f + k);
        DrawLineEx({x, 60}, {x + sw, 100}, 1, Color{60, 50, 40, 255});
        DrawCircleV({x + sw, 110}, 11, Fade(Color{140, 200, 190, 255}, 0.35f));
        DrawCircleLinesV({x + sw, 110}, 11, Fade(Color{220, 240, 230, 255}, 0.4f));
    }
    DrawRectangle(0, 56, 400, 8, Tone(Pal::Copper, -0.2f));
    Octopus({230, 405}, in);
    DrawRectangle(0, 380, 420, 150, wood);              // the counter's front
    DrawTiled(Tex::Wood, {0, 390, 420, 140}, 0.9f, Tone(wood, -0.1f));
    DrawRectangle(0, 372, 430, 14, Tone(wood, 0.3f));   // its top
    for (int k = 0; k < 3; k++) { float x = 80 + k * 90; DrawRectangle((int)x, 336, 8, 36, Pal::Copper); DrawCircleV({x + 4, 336}, 7, Pal::Copper); DrawRectangle((int)x - 6, 344, 20, 5, Tone(Pal::Copper, -0.3f)); }
    // the stage: raised boards right of centre under a single brass lamp
    DrawRectangle(660, 432, 580, 60, Color{60, 38, 24, 255});
    DrawTiled(Tex::Wood, {660, 424, 580, 16}, 0.8f, Color{110, 76, 46, 255});
    DrawRectangle(660, 438, 580, 4, Tone(Pal::Brass, -0.2f));
    DrawLineEx({950, 0}, {950, 60}, 2, Color{40, 34, 30, 255});
    DrawTri({920, 60}, {980, 60}, {950, 40}, Pal::Brass);
    DrawEllipse(950, 62, 30, 6, Tone(Pal::Brass, -0.3f));
    // the band
    Sailor({760, 430}, in, bt);
    Merman({880, 430}, in, bt);
    Crab({1030, 430}, in, bt);
    Drummer({1170, 430}, in, bt);
    // the floor and the patrons in the foreground
    DrawVGradient({0, 490, (float)SCREEN_W, 230}, Color{52, 38, 30, 255}, Color{26, 18, 14, 255});
    const float px[7] = {90, 280, 470, 640, 820, 1010, 1190};
    for (int i = 0; i < 7; i++) Patron(i, {px[i], 700.0f + (i % 2) * 10}, in);
    for (int i = 0; i < 7; i++) DrawCircleV({px[i] + (i % 2 ? 40.0f : -40.0f), 650.0f + (i % 2) * 10}, 3, Color{255, 200, 120, 255});   // candles

    // light: the amber stage lamp is the key, the portholes the fill, a warm glow over the bar
    LightsBegin(Color{84, 76, 84, 255});
    AddCone({950, 64}, PI / 2, 0.45f, 420, Color{255, 190, 110, 255});
    AddLight({950, 320}, 470, Color{255, 180, 100, 255}, 0.95f);
    for (int k = 0; k < 3; k++) AddLight({470.0f + k * 250, 190}, 220, Color{80, 150, 200, 255}, 0.55f);
    AddLight({200, 300}, 300, Color{255, 170, 100, 255}, 0.45f);
    for (int i = 0; i < 7; i++) AddLight({px[i] + (i % 2 ? 40.0f : -40.0f), 650.0f}, 160, Color{255, 180, 110, 255}, 0.6f);
    LightsEnd();
    InkPass(0.85f, 0.8f);
    // after the ink: haze and slow dust in the stage beam, marine snow in the portholes, the drummer's steam
    for (int k = 0; k < 40; k++) {
        float life = fmodf(t * 0.02f + Hash(k), 1.0f);
        float x = 950 + (Hash(k + 3) - 0.5f) * 260 * (0.3f + life) + sinf(t * 0.2f + k) * 8, y = 80 + life * 340;
        DrawCircleV({x, y}, 1.2f, Fade(Color{255, 220, 170, 255}, 0.35f * sinf(life * PI)));
    }
    for (int k = 0; k < 3; k++) for (int m = 0; m < 6; m++) { float ph = fmodf(t * 0.03f + Hash(k * 9 + m), 1.0f); DrawCircleV({470.0f + k * 250 + (Hash(m + k) - 0.5f) * 90, 130 + ph * 120}, 1, Fade(WHITE, 0.3f * sinf(ph * PI))); }
    if (in.bandPlaying && (bt.beatInBar == 1 || bt.beatInBar == 3)) {
        float u = bt.phase;
        for (int k = 0; k < 4; k++) DrawCircleV({1166 + k * 3.0f + u * 10, 274 - u * 30 - k * 4.0f}, 5 + u * 6, Fade(Color{230, 230, 230, 255}, 0.18f * (1 - u)));
    }
    DrawRectangleGradientV(0, 0, SCREEN_W, 120, Fade(Color{60, 50, 50, 255}, 0.12f), Fade(Color{60, 50, 50, 255}, 0.0f));   // smoke under the ceiling
    Glow({950, 62}, 60, Color{255, 200, 120, 70});
}
}  // namespace

const char* SceneName(int s) { return s == SC_LOUNGE ? "The Lamplight Lounge" : "The Captain's Study"; }

void DrawScene(int scene, const SceneInputs& in) {
    if (scene == SC_LOUNGE) Lounge(in); else CaptainsStudy(in);
    // Focus Dim: darker and less saturated (a grey pull, then a darkening)
    if (in.dim > 0.001f) {
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(Color{70, 70, 74, 255}, in.dim * 0.35f));
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, Fade(BLACK, in.dim * 0.55f));
    }
}
}
